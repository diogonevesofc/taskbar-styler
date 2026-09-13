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

// Owns one open session's IXamlDiagnostics and (when available)
// IXamlDiagnosticsTestHooks. Both are released exactly once, from the
// destructor, so teardown has a single gate instead of Release() calls
// scattered across failure paths that one of them could skip.
class DiagnosticsSession {
public:
    DiagnosticsSession(IXamlDiagnostics* diagnostics,
                        IXamlDiagnosticsTestHooks* hooks)
        : diagnostics_(diagnostics), hooks_(hooks) {}

    DiagnosticsSession(const DiagnosticsSession&) = delete;
    DiagnosticsSession& operator=(const DiagnosticsSession&) = delete;

    ~DiagnosticsSession() {
        if (hooks_) {
            hooks_->Release();
        }
        if (diagnostics_) {
            diagnostics_->Release();
        }
    }

    IXamlDiagnostics* diagnostics() const { return diagnostics_; }
    IXamlDiagnosticsTestHooks* hooks() const { return hooks_; }

private:
    IXamlDiagnostics* diagnostics_;
    IXamlDiagnosticsTestHooks* hooks_;
};

// The current session, or nullptr. Written only by OpenDiagnostics/
// CloseDiagnostics below; read through Diagnostics()/ReleaseHandle - never
// reach for this directly from another translation unit. std::atomic because
// the TAP is called from several explorer UI threads (same reasoning as
// g_site in tap_boundary.cpp - see site.h).
std::atomic<DiagnosticsSession*> g_session{nullptr};

// Handles successfully released so far this process. Monotonic by
// construction: only ReleaseHandle's success path touches it, and only ever
// increments it.
std::atomic<long> g_released_handles{0};

}  // namespace

void ReleaseHandle(InstanceHandle handle) {
    if (!handle) {
        return;  // A root element's parent handle is 0; nothing to release.
    }

    DiagnosticsSession* session = g_session.load(std::memory_order_acquire);
    if (!session || !session->hooks()) {
        return;  // No session open, or hooks unavailable - warned once above.
    }

    HRESULT hr = session->hooks()->UnregisterInstance(handle);
    if (SUCCEEDED(hr)) {
        g_released_handles.fetch_add(1, std::memory_order_relaxed);
    } else {
        STYLER_LOG(LogLevel::Error, L"UnregisterInstance(%llu) failed 0x%08X",
                   static_cast<unsigned long long>(handle),
                   static_cast<unsigned>(hr));
    }
}

long LiveHandleCount() {
    return g_released_handles.load(std::memory_order_relaxed);
}

IXamlDiagnostics* Diagnostics() {
    DiagnosticsSession* session = g_session.load(std::memory_order_acquire);
    return session ? session->diagnostics() : nullptr;
}

HRESULT OpenDiagnostics(IUnknown* site) {
    if (!site) {
        return E_INVALIDARG;
    }

    if (g_session.load(std::memory_order_acquire)) {
        STYLER_LOG(LogLevel::Info,
                   L"OpenDiagnostics called with a session already open; "
                   L"closing and reopening");
        CloseDiagnostics();
    }

    IXamlDiagnostics* diagnostics = nullptr;
    HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&diagnostics));
    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"QI IXamlDiagnostics failed 0x%08X",
                   static_cast<unsigned>(hr));
        return hr;
    }

    IXamlDiagnosticsTestHooks* hooks = nullptr;
    if (FAILED(diagnostics->QueryInterface(
            IID_IXamlDiagnosticsTestHooks,
            reinterpret_cast<void**>(&hooks)))) {
        // Not fatal, but the user must know: without it every reported
        // element leaks for the life of the explorer process.
        STYLER_LOG(LogLevel::Error,
                   L"IXamlDiagnosticsTestHooks unavailable - elements will "
                   L"leak; report this, it means Windows changed");
        hooks = nullptr;
    }

    auto* session = new (std::nothrow) DiagnosticsSession(diagnostics, hooks);
    if (!session) {
        if (hooks) {
            hooks->Release();
        }
        diagnostics->Release();
        return E_OUTOFMEMORY;
    }

    g_session.store(session, std::memory_order_release);
    STYLER_LOG(LogLevel::Info, L"diagnostics session open");
    return S_OK;
}

void CloseDiagnostics() {
    DiagnosticsSession* session =
        g_session.exchange(nullptr, std::memory_order_acq_rel);
    if (!session) {
        return;
    }
    delete session;  // Releases hooks and diagnostics exactly once.
    STYLER_LOG(LogLevel::Info, L"diagnostics session closed");
}

}  // namespace styler::tap
