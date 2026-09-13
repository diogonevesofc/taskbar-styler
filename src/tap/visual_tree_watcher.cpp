// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/visual_tree_watcher.h>

#include <atomic>
#include <new>

#include <tap/log.h>

namespace styler::tap {
namespace {

// {735941A2-3EE3-495A-8DA9-972627003075}
// Private and undocumented; read from the vendored upstream at line 10946.
// Confirmed to still match before trusting this constant (2026-09-12).
constexpr GUID IID_IXamlDiagnosticsTestHooks = {
    0x735941a2,
    0x3ee3,
    0x495a,
    {0x8d, 0xa9, 0x97, 0x26, 0x27, 0x00, 0x30, 0x75}};

struct IXamlDiagnosticsTestHooks : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE UnregisterInstance(
        InstanceHandle handle) = 0;
};

std::atomic<long> g_live_handles{0};
IXamlDiagnostics* g_diagnostics = nullptr;
IXamlDiagnosticsTestHooks* g_hooks = nullptr;

void ReleaseHandle(InstanceHandle handle) {
    if (!g_hooks) {
        return;  // Already warned once, in StartWatching.
    }
    if (SUCCEEDED(g_hooks->UnregisterInstance(handle))) {
        g_live_handles.fetch_sub(1, std::memory_order_relaxed);
    }
}

class VisualTreeWatcher : public IVisualTreeServiceCallback2 {
public:
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
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    // Called by XAML: catch-all, and always S_OK. An error return makes XAML
    // stop reporting changes.
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation, VisualElement element,
        VisualMutationType mutationType) override {
        try {
            if (mutationType == Add) {
                g_live_handles.fetch_add(1, std::memory_order_relaxed);
                STYLER_LOG(LogLevel::Debug, L"+ %s (handle %llu)",
                           element.Type ? element.Type : L"<unknown>",
                           static_cast<unsigned long long>(element.Handle));
            } else {
                STYLER_LOG(LogLevel::Debug, L"- handle %llu",
                           static_cast<unsigned long long>(element.Handle));
            }

            ReleaseHandle(element.Handle);
            if (mutationType == Add) {
                ReleaseHandle(relation.Parent);
            }
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"OnVisualTreeChange threw");
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
        return S_OK;
    }

private:
    LONG m_ref = 1;
};

VisualTreeWatcher* g_watcher = nullptr;

}  // namespace

long LiveHandleCount() {
    return g_live_handles.load(std::memory_order_relaxed);
}

IXamlDiagnostics* Diagnostics() {
    return g_diagnostics;
}

HRESULT StartWatching(IUnknown* site) {
    if (!site) {
        return E_INVALIDARG;
    }
    if (g_watcher) {
        return S_FALSE;
    }

    HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&g_diagnostics));
    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"QI IXamlDiagnostics failed 0x%08X",
                   static_cast<unsigned>(hr));
        return hr;
    }

    if (FAILED(g_diagnostics->QueryInterface(
            IID_IXamlDiagnosticsTestHooks,
            reinterpret_cast<void**>(&g_hooks)))) {
        // Not fatal, but the user must know: without it every reported element
        // leaks for the life of the explorer process.
        STYLER_LOG(LogLevel::Error,
                   L"IXamlDiagnosticsTestHooks unavailable - elements will "
                   L"leak; report this, it means Windows changed");
        g_hooks = nullptr;
    }

    IVisualTreeService3* service = nullptr;
    hr = g_diagnostics->QueryInterface(IID_PPV_ARGS(&service));
    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"QI IVisualTreeService3 failed 0x%08X",
                   static_cast<unsigned>(hr));
        return hr;
    }

    auto* watcher = new (std::nothrow) VisualTreeWatcher();
    if (!watcher) {
        service->Release();
        return E_OUTOFMEMORY;
    }

    hr = service->AdviseVisualTreeChange(watcher);
    service->Release();

    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"AdviseVisualTreeChange failed 0x%08X",
                   static_cast<unsigned>(hr));
        watcher->Release();
        return hr;
    }

    g_watcher = watcher;
    STYLER_LOG(LogLevel::Info, L"watching the visual tree");
    return S_OK;
}

void StopWatching() {
    if (!g_watcher) {
        return;
    }

    IVisualTreeService3* service = nullptr;
    if (g_diagnostics &&
        SUCCEEDED(g_diagnostics->QueryInterface(IID_PPV_ARGS(&service)))) {
        service->UnadviseVisualTreeChange(g_watcher);
        service->Release();
    }

    g_watcher->Release();
    g_watcher = nullptr;

    if (g_hooks) {
        g_hooks->Release();
        g_hooks = nullptr;
    }
    if (g_diagnostics) {
        g_diagnostics->Release();
        g_diagnostics = nullptr;
    }

    STYLER_LOG(LogLevel::Info, L"stopped watching");
}

}  // namespace styler::tap
