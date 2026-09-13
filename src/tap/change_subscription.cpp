// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/change_subscription.h>

#include <atomic>
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

struct Subscription {
    StandingCallback* callback = nullptr;  // Our reference.
    IVisualTreeService3* service = nullptr;
};

// Heap-leaked for the same reason as g_session: no namespace-scope
// destructor may run at DLL_PROCESS_DETACH (Plano 2, Ruling 11).
auto* const g_subscription = new std::atomic<Subscription*>{nullptr};

}  // namespace

HRESULT StartSubscription() {
    if (g_subscription->load()) {
        return S_FALSE;
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

    // The initial flood arrives inside this call, on this thread.
    hr = service->AdviseVisualTreeChange(sub->callback);
    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"AdviseVisualTreeChange failed 0x%08X",
                   static_cast<unsigned>(hr));
        g_subscription->store(nullptr);
        // Advise can register, walk, and then fail: unadvise regardless and
        // keep the callback alive if that fails too.
        if (SUCCEEDED(service->UnadviseVisualTreeChange(sub->callback))) {
            sub->callback->Release();
        }
        service->Release();
        delete sub;
        return hr;
    }
    STYLER_LOG(LogLevel::Info, L"subscription started on thread %lu",
               GetCurrentThreadId());
    return S_OK;
}

void StopSubscription() {
    Subscription* sub = g_subscription->exchange(nullptr);
    if (!sub) {
        return;
    }
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
}

}  // namespace styler::tap
