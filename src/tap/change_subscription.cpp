// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/change_subscription.h>

#include <atomic>
#include <cwchar>
#include <memory>

#include <tap/element_registry.h>
#include <tap/log.h>
#include <tap/release_queue.h>
#include <tap/style_engine.h>
#include <tap/thread_init.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {

// spike-standing-crash E7/E8a: a Windows.UI.Composition.* handle is never
// resolved - see change_subscription.h for why. Its handles are still
// queued for release exactly like any other Add.
constexpr wchar_t kCompositionPrefix[] = L"Windows.UI.Composition.";
constexpr size_t kCompositionPrefixLen =
    (sizeof(kCompositionPrefix) / sizeof(wchar_t)) - 1;

// The standing callback. Heap-allocated with a real reference count: XAML
// holds one reference while advised, we hold one while subscribed. Unlike
// tree_export.cpp's stack snapshot this object lives for hours.
class StandingCallback : public IVisualTreeServiceCallback2 {
public:
    explicit StandingCallback(std::shared_ptr<DiagnosticsSession> session)
        : session_(std::move(session)) {}

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
        try {
            if (!IsInitializedForCurrentThread()) {
                // A XAML thread SetSite never initialized (not a taskbar
                // host). Upstream returns here too, before queueing: those
                // handles stay registered. Bounded by explorer's own life.
                STYLER_LOG(LogLevel::Debug, L"report on uninitialized thread %lu",
                           GetCurrentThreadId());
                return S_OK;
            }
            try {
                if (mutationType == Add) {
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

            FlushReleasesIfQuiet();

            if (mutationType == Add) {
                QueueRelease(element.Handle);
                QueueRelease(relation.Parent);
            } else if (mutationType == Remove) {
                // Queued, never released here: this report arrives from
                // inside the Leave walk still visiting the removed subtree.
                QueueRelease(element.Handle);
                ForgetElementId(element.Handle);
            }
        } catch (...) {
            // Nothing above should reach here; if it does, XAML must not
            // see an error - it would stop reporting.
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
        return S_OK;
    }

private:
    std::atomic<long> ref_{1};
    std::shared_ptr<DiagnosticsSession> session_;
};

// See change_subscription.h for the full protocol these three values and
// the two compare_exchange_strong call sites (one on Subscription::state
// below, one on g_subscription in StartSubscription's failure path and in
// StopSubscription) implement together.
constexpr int kAdvising = 0;
constexpr int kAdvised = 1;
constexpr int kStopped = 2;

struct Subscription {
    StandingCallback* callback = nullptr;  // Our reference.
    IVisualTreeService3* service = nullptr;
    std::atomic<int> state{kAdvising};
};

// What the advise thread actually owns. Deliberately separate from
// Subscription: the thread's own AddRef'd callback/service let it call
// Advise/Unadvise and log without ever dereferencing the shared
// Subscription's pointers while a concurrent StopSubscription could be
// racing to free them - see change_subscription.h. `sub` is carried only as
// an identity for the protocol's compare_exchange_strong calls.
struct AdviseJob {
    StandingCallback* callback;
    IVisualTreeService3* service;
    Subscription* sub;
};

// Heap-leaked for the same reason as g_session: no namespace-scope
// destructor may run at DLL_PROCESS_DETACH (Plano 2, Ruling 11).
auto* const g_subscription = new std::atomic<Subscription*>{nullptr};

// Tears a Subscription down after an advise attempt that will never result
// in a live subscription: either AdviseVisualTreeChange itself failed, or
// the advise thread could not even be started. `detached_ourselves` is the
// result of the caller's own g_subscription->compare_exchange_strong(sub,
// nullptr), already done before calling this - see change_subscription.h
// for why that must happen first and why this races `state` when it fails
// instead of deleting `sub` unconditionally (review found: the first
// version of this fix did exactly that, racing a StopSubscription already
// committed to reading sub->state).
void TearDownAfterFailedAdvise(Subscription* sub, bool detached_ourselves) {
    if (!detached_ourselves) {
        // A StopSubscription already exchanged `sub` out of g_subscription
        // and is about to (or already did) read sub->state. Race the same
        // Advising -> Advised transition the success path uses to signal
        // "the attempt is over, tear down through the normal path" - do not
        // just delete `sub` here, or that read is a use-after-free.
        int expected_state = kAdvising;
        if (sub->state.compare_exchange_strong(expected_state, kAdvised)) {
            // Won: the Stop holding `sub` will find Advised at its own CAS
            // and do the full teardown itself. Nothing left for us to touch.
            return;
        }
        // Lost: state already read Stopped, so Stop already left `sub`
        // alone for whoever loses this race - that is us now.
    }
    // Either we detached `sub` ourselves (no Stop ever saw it) or we just
    // lost the race above (Stop left it for us): full ownership either way.
    if (SUCCEEDED(sub->service->UnadviseVisualTreeChange(sub->callback))) {
        sub->callback->Release();
    } else {
        STYLER_LOG(LogLevel::Error,
                   L"UnadviseVisualTreeChange failed - leaking the callback");
    }
    sub->service->Release();
    delete sub;
}

}  // namespace

HRESULT StartSubscription() {
    if (g_subscription->load()) {
        return S_FALSE;
    }

    // Fail closed: see change_subscription.h for why. The Debug filter above
    // is defense in depth, not a substitute for this - it only stops the
    // deterministic crash, not the heap race, which happens before our
    // callback is ever called.
    DWORD disabled = 0;
    DWORD disabled_size = sizeof(disabled);
    LONG reg_st = RegGetValueW(
        HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\XAML\\Debug",
        L"DisableCompositionDiag", RRF_RT_REG_DWORD, nullptr, &disabled,
        &disabled_size);
    if (reg_st != ERROR_SUCCESS || disabled != 1) {
        STYLER_LOG(LogLevel::Error,
                   L"composition diagnostics are enabled - run \"taskbar-styler "
                   L"setup\" once (as administrator) to disable them; not "
                   L"subscribing");
        return E_NOT_VALID_STATE;
    }

    std::shared_ptr<DiagnosticsSession> session = AcquireSession();
    if (!session) {
        return E_NOT_VALID_STATE;
    }
    IVisualTreeService3* service = nullptr;
    HRESULT hr = session->diagnostics()->QueryInterface(
        __uuidof(IVisualTreeService3), reinterpret_cast<void**>(&service));
    if (FAILED(hr) || !service) {
        STYLER_LOG(LogLevel::Error, L"QI IVisualTreeService3 failed 0x%08X",
                   static_cast<unsigned>(hr));
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }
    auto* sub = new (std::nothrow) Subscription{};
    if (!sub) {
        service->Release();
        return E_OUTOFMEMORY;
    }
    sub->callback = new (std::nothrow) StandingCallback(session);
    if (!sub->callback) {
        service->Release();
        delete sub;
        return E_OUTOFMEMORY;
    }
    sub->service = service;

    Subscription* expected = nullptr;
    if (!g_subscription->compare_exchange_strong(expected, sub)) {
        sub->callback->Release();
        service->Release();
        delete sub;
        return S_FALSE;  // Lost the race to another SetSite.
    }

    // vendor:11013-11030: calling Advise from this thread hangs in
    // Advising::RunOnUIThread "sometimes" - measured here too
    // (spike-standing-crash.md E3-E5). Run it on a new thread instead, and
    // do not wait for it - see the header comment for the ownership
    // protocol this implements and why it is needed.
    auto* job = new (std::nothrow) AdviseJob{};
    if (!job) {
        Subscription* expected_sub = sub;
        g_subscription->compare_exchange_strong(expected_sub, nullptr);
        sub->callback->Release();
        service->Release();
        delete sub;
        return E_OUTOFMEMORY;
    }
    job->callback = sub->callback;
    job->callback->AddRef();  // The job's own reference.
    job->service = sub->service;
    job->service->AddRef();  // The job's own reference.
    job->sub = sub;

    HANDLE thread = CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            // Deliberately no CoInitializeEx here: mirrors upstream
            // (vendor:11017-11030), measured working without it.
            auto* job = static_cast<AdviseJob*>(param);
            HRESULT advise_hr =
                job->service->AdviseVisualTreeChange(job->callback);
            if (FAILED(advise_hr)) {
                STYLER_LOG(LogLevel::Error,
                           L"AdviseVisualTreeChange failed 0x%08X",
                           static_cast<unsigned>(advise_hr));
                // Never a plain store - see the header comment.
                Subscription* expected_sub = job->sub;
                bool detached =
                    g_subscription->compare_exchange_strong(expected_sub, nullptr);
                TearDownAfterFailedAdvise(job->sub, detached);
                job->callback->Release();  // The job's own ref, always ours.
                job->service->Release();
                delete job;
                return 0;
            }

            int expected_state = kAdvising;
            if (job->sub->state.compare_exchange_strong(expected_state,
                                                          kAdvised)) {
                // Live: a later StopSubscription will find kAdvised and tear
                // this down normally. Nothing left to do but release the
                // job's own references - the Subscription's stay held.
                STYLER_LOG(LogLevel::Info,
                           L"subscription started on thread %lu",
                           GetCurrentThreadId());
                job->callback->Release();
                job->service->Release();
                delete job;
                return 0;
            }
            // The CAS lost: state already read Stopped, meaning
            // StopSubscription ran while we were still inside Advise, found
            // kAdvising, and - per its own half of the protocol - left the
            // Subscription entirely alone for us to tear down now.
            STYLER_LOG(LogLevel::Info,
                       L"subscription started on thread %lu then stopped "
                       L"(StopSubscription raced the advise)",
                       GetCurrentThreadId());
            if (SUCCEEDED(job->sub->service->UnadviseVisualTreeChange(
                    job->sub->callback))) {
                job->sub->callback->Release();
                STYLER_LOG(LogLevel::Info, L"subscription stopped");
            } else {
                STYLER_LOG(LogLevel::Error,
                           L"UnadviseVisualTreeChange failed - leaking the "
                           L"callback");
            }
            job->sub->service->Release();
            delete job->sub;
            job->callback->Release();
            job->service->Release();
            delete job;
            return 0;
        },
        job, 0, nullptr);
    if (!thread) {
        DWORD err = GetLastError();
        STYLER_LOG(LogLevel::Error, L"CreateThread for Advise failed %lu", err);
        Subscription* expected_sub = sub;
        bool detached = g_subscription->compare_exchange_strong(expected_sub, nullptr);
        TearDownAfterFailedAdvise(sub, detached);
        job->callback->Release();
        job->service->Release();
        delete job;
        return HRESULT_FROM_WIN32(err);
    }
    CloseHandle(thread);
    STYLER_LOG(LogLevel::Info, L"advise thread created");
    return S_OK;
}

StopResult StopSubscription() {
    // Detach first so nothing else can reach this Subscription - including a
    // concurrent second StopSubscription, which will find nullptr here and
    // no-op below.
    Subscription* sub = g_subscription->exchange(nullptr);
    if (!sub) {
        return StopResult::None;
    }

    int expected_state = kAdvising;
    if (sub->state.compare_exchange_strong(expected_state, kStopped)) {
        // The advise attempt is still unresolved - either a live
        // AdviseVisualTreeChange call, which marshals its walk onto the UI
        // thread this function itself runs on from SetSite (so waiting for
        // it here would deadlock), or its failure handling racing this same
        // CAS right now (TearDownAfterFailedAdvise). Leave the Subscription
        // untouched either way; whichever side loses that race will see
        // kStopped and tear it down (see the header comment's protocol).
        STYLER_LOG(LogLevel::Info,
                   L"subscription stop deferred - advise still in flight");
        return StopResult::Deferred;
    }

    // expected_state came back Advised: the advise side is done touching
    // this Subscription. Tear it down, as always.
    HRESULT hr = sub->service->UnadviseVisualTreeChange(sub->callback);
    if (SUCCEEDED(hr)) {
        sub->callback->Release();
        STYLER_LOG(LogLevel::Info, L"subscription stopped");
    } else {
        STYLER_LOG(LogLevel::Error,
                   L"UnadviseVisualTreeChange failed 0x%08X - leaking the callback",
                   static_cast<unsigned>(hr));
    }
    sub->service->Release();
    delete sub;
    return StopResult::Stopped;
}

}  // namespace styler::tap
