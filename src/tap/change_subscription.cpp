// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/change_subscription.h>

#include <atomic>
#include <cwchar>
#include <memory>

#include <tap/element_registry.h>
#include <tap/log.h>
#include <tap/release_queue.h>
#include <tap/report_dispatch.h>
#include <tap/style_engine.h>
#include <tap/thread_init.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>
#include <tap/subscription_state.h>

namespace styler::tap {
namespace {

// spike-standing-crash E7/E8a: a Windows.UI.Composition.* handle is never
// resolved - see change_subscription.h for why. Its handles are still
// queued for release exactly like any other Add.
constexpr wchar_t kCompositionPrefix[] = L"Windows.UI.Composition.";
constexpr size_t kCompositionPrefixLen =
    (sizeof(kCompositionPrefix) / sizeof(wchar_t)) - 1;

std::atomic<void(*)()> g_completion{nullptr};
void NotifyCompletion() noexcept {
    try {
        if (auto notify = g_completion.load()) notify();
    } catch (...) {
    }
}
struct CallbackActivity {
    std::atomic<unsigned> active{0};
    std::atomic<bool> enabled{true};
};
struct CallbackScope {
    std::shared_ptr<CallbackActivity> activity;
    explicit CallbackScope(std::shared_ptr<CallbackActivity> value) : activity(std::move(value)) {
        activity->active.fetch_add(1);
    }
    ~CallbackScope() {
        if (activity->active.fetch_sub(1) == 1 && !activity->enabled.load()) NotifyCompletion();
    }
};
// The standing callback. Heap-allocated with a real reference count: XAML
// holds one reference while advised, we hold one while subscribed. Unlike
// tree_export.cpp's stack snapshot this object lives for hours.
class StandingCallback : public IVisualTreeServiceCallback2 {
public:
    StandingCallback(std::shared_ptr<DiagnosticsSession> session,
                     std::shared_ptr<CallbackActivity> activity)
        : session_(std::move(session)), activity_(std::move(activity)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown ||
            riid == __uuidof(IVisualTreeServiceCallback) ||
            riid == __uuidof(IVisualTreeServiceCallback2)) {
            *ppv = static_cast<IVisualTreeServiceCallback2*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++ref_);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = static_cast<ULONG>(--ref_);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    // Everything here runs on the reporting UI thread, inside XAML's walk.
    // Order matters and mirrors upstream vendor:11100-11185: style work
    // first (its own try, so the bookkeeping below still runs if it
    // throws), then arm the drain, then queue this report's handles.
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation, VisualElement element,
        VisualMutationType mutationType) override {
        CallbackScope scope(activity_);
        session_->Observe(element.Handle);
        if (mutationType == Add) session_->Observe(relation.Parent);
        try {
            const bool initialized = IsInitializedForCurrentThread();
            DispatchObservedReport(initialized, [&] {
                try {
                    if (activity_->enabled.load() && mutationType == Add) {
                        if (element.Type &&
                            wcsncmp(element.Type, kCompositionPrefix,
                                    kCompositionPrefixLen) == 0) {
                            // vendor:10904-10914: resolving this handle is what
                            // crashes the process. Never call
                            // GetIInspectableFromHandle on it.
                            STYLER_LOG(LogLevel::Debug,
                                       L"skipped composition visual %s", element.Type);
                        } else {
                            ::IInspectable* raw = nullptr;
                            HRESULT hr = session_->diagnostics()->GetIInspectableFromHandle(
                                element.Handle, &raw);
                            if (SUCCEEDED(hr) && raw) {
                                wf::IInspectable obj = InspectableFromRaw(raw);
                                ElementId id = GetOrCreateElementId(element.Handle, obj);
                                if (id != ElementId::None) {
                                    if (auto fe = obj.try_as<wux::FrameworkElement>()) {
                                        OnElementAdded(id, fe, element.Type);
                                    }
                                }
                            }
                        }
                    } else if (mutationType == Remove) {
                        OnElementRemoved(FindElementId(element.Handle));
                    }
                } catch (winrt::hresult_error const& ex) {
                    STYLER_LOG(LogLevel::Error, L"report hresult 0x%08X",
                               static_cast<unsigned>(ex.code()));
                } catch (...) {
                    STYLER_LOG(LogLevel::Error, L"report threw");
                }
            }, [&] {
                // Host discovery can lag the first report. QueueRelease creates
                // a release-only endpoint without marking this thread styled.
                if (mutationType == Add) {
                    QueueRelease(session_, element.Handle);
                    QueueRelease(session_, relation.Parent);
                } else if (mutationType == Remove) {
                    // Queued, never released here: this report arrives from
                    // inside the Leave walk still visiting the removed subtree.
                    QueueRelease(session_, element.Handle);
                    if (initialized) ForgetElementId(element.Handle);
                }
            });
            if (!initialized) {
                STYLER_LOG(LogLevel::Debug, L"queued report before styling initialization on thread %lu",
                           GetCurrentThreadId());
            }
        } catch (...) {
            MarkHandleObservationIncomplete();
            // Nothing above should reach here; if it does, XAML must not
            // see an error - it would stop reporting.
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle handle, VisualElementState, LPCWSTR) noexcept override {
        CallbackScope scope(activity_);
        session_->Observe(handle);
        try {
            QueueRelease(session_, handle);
        } catch (...) {
            MarkHandleObservationIncomplete();
        }
        return S_OK;
    }

private:
    std::atomic<long> ref_{1};
    std::shared_ptr<DiagnosticsSession> session_;
    std::shared_ptr<CallbackActivity> activity_;
};

struct Subscription {
    StandingCallback* callback = nullptr;
    IVisualTreeService3* service = nullptr;
    std::shared_ptr<CallbackActivity> activity = std::make_shared<CallbackActivity>();
    SubscriptionState state;
    ~Subscription() {
        if (callback) callback->Release();
        if (service) service->Release();
    }
};
// A stopping/failed subscription remains published. Every worker and stopper
// takes strong ownership before touching it; no COM call occurs under a lock.
auto* const g_subscription = new std::atomic<std::shared_ptr<Subscription>>;

bool CompleteStop(const std::shared_ptr<Subscription>& sub) {
    if (sub->activity->active.load() != 0) return false;
    if (sub->state.CompleteQuiescence()) {
        auto expected = sub;
        g_subscription->compare_exchange_strong(expected, nullptr);
        STYLER_LOG(LogLevel::Info, L"subscription stopped");
        NotifyCompletion();
    }
    return sub->state.phase() == SubscriptionPhase::Stopped;
}

StopResult Unadvise(const std::shared_ptr<Subscription>& sub) {
    sub->activity->enabled.store(false);
    HRESULT hr = E_FAIL;
    try {
        hr = sub->service->UnadviseVisualTreeChange(sub->callback);
    } catch (...) {
        MarkHandleObservationIncomplete();
    }
    sub->state.UnadviseFinished(SUCCEEDED(hr));
    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error,
            L"UnadviseVisualTreeChange failed 0x%08X - ownership retained; retry required",
            static_cast<unsigned>(hr));
        NotifyCompletion();
        return StopResult::Failed;
    }
    return CompleteStop(sub) ? StopResult::Stopped : StopResult::Deferred;
}
}  // namespace

void SetSubscriptionCompletion(void (*callback)()) { g_completion.store(callback); }

HRESULT StartSubscription() {
    if (g_subscription->load()) return E_PENDING;
    DWORD disabled = 0;
    DWORD size = sizeof(disabled);
    const LONG result = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\XAML\\Debug", L"DisableCompositionDiag",
        RRF_RT_REG_DWORD, nullptr, &disabled, &size);
    if (result != ERROR_SUCCESS || disabled != 1) {
        STYLER_LOG(LogLevel::Error, L"composition diagnostics enabled; setup required; not subscribing");
        return E_NOT_VALID_STATE;
    }
    auto session = AcquireSession();
    if (!session) return E_NOT_VALID_STATE;
    auto sub = std::make_shared<Subscription>();
    HRESULT hr = session->diagnostics()->QueryInterface(
        __uuidof(IVisualTreeService3), reinterpret_cast<void**>(&sub->service));
    if (FAILED(hr) || !sub->service) return FAILED(hr) ? hr : E_NOINTERFACE;
    sub->callback = new StandingCallback(session, sub->activity);
    std::shared_ptr<Subscription> expected;
    if (!g_subscription->compare_exchange_strong(expected, sub)) return E_PENDING;
    auto* job = new (std::nothrow) std::shared_ptr<Subscription>(sub);
    if (!job) {
        sub->state.AdviseFinished(false);
        Unadvise(sub);
        return E_OUTOFMEMORY;
    }
    HANDLE thread = CreateThread(nullptr, 0, [](void* context) -> DWORD {
        std::unique_ptr<std::shared_ptr<Subscription>> holder(
            static_cast<std::shared_ptr<Subscription>*>(context));
        const auto sub = *holder;
        try {
            const HRESULT hr = sub->service->AdviseVisualTreeChange(sub->callback);
            if (FAILED(hr)) {
                STYLER_LOG(LogLevel::Error, L"AdviseVisualTreeChange failed 0x%08X", static_cast<unsigned>(hr));
            }
            if (sub->state.AdviseFinished(SUCCEEDED(hr))) {
                Unadvise(sub);
            } else {
                STYLER_LOG(LogLevel::Info, L"subscription started on thread %lu", GetCurrentThreadId());
            }
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"advise worker threw");
            if (sub->state.AdviseFinished(false)) Unadvise(sub);
        }
        NotifyCompletion();
        return 0;
    }, job, 0, nullptr);
    if (!thread) {
        const auto error = GetLastError();
        delete job;
        sub->state.AdviseFinished(false);
        Unadvise(sub);
        return HRESULT_FROM_WIN32(error);
    }
    CloseHandle(thread);
    STYLER_LOG(LogLevel::Info, L"advise thread created");
    return S_OK;
}

StopResult StopSubscription() {
    const auto sub = g_subscription->load();
    if (!sub) return StopResult::None;
    sub->activity->enabled.store(false);
    switch (sub->state.RequestStop()) {
    case StopAction::Stopped: return StopResult::Stopped;
    case StopAction::Unadvise: return Unadvise(sub);
    case StopAction::Quiesce:
        return CompleteStop(sub) ? StopResult::Stopped : StopResult::Deferred;
    default:
        STYLER_LOG(LogLevel::Info, L"subscription stop deferred - advise/callback still in flight");
        return StopResult::Deferred;
    }
}

}  // namespace styler::tap
