// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/visual_tree_watcher.h>

#include <atomic>
#include <memory>
#include <new>

#include <tap/log.h>

namespace styler::tap {

// {735941A2-3EE3-495A-8DA9-972627003075}
// Private and undocumented; read from the vendored upstream at line 10946.
// Confirmed to still match before trusting this constant (2026-09-12).
constexpr GUID IID_IXamlDiagnosticsTestHooks = {
    0x735941a2,
    0x3ee3,
    0x495a,
    {0x8d, 0xa9, 0x97, 0x26, 0x27, 0x00, 0x30, 0x75}};

// Full definition of the type forward-declared in visual_tree_watcher.h.
struct IXamlDiagnosticsTestHooks : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE UnregisterInstance(
        InstanceHandle handle) = 0;
};

DiagnosticsSession::DiagnosticsSession(IXamlDiagnostics* diagnostics,
                                        IXamlDiagnosticsTestHooks* hooks)
    : diagnostics_(diagnostics), hooks_(hooks) {}

DiagnosticsSession::~DiagnosticsSession() {
    if (hooks_) {
        hooks_->Release();
    }
    if (diagnostics_) {
        diagnostics_->Release();
    }
}

HRESULT DiagnosticsSession::ReleaseElementHandle(InstanceHandle handle) const {
    if (!hooks_) {
        return S_FALSE;  // No hooks on this session; nothing to call.
    }
    return hooks_->UnregisterInstance(handle);
}

namespace {

// The current session, or an empty pointer. Written only by
// OpenDiagnostics/CloseDiagnostics below; read through
// AcquireSession()/ReleaseHandle - never reach for this directly from
// another translation unit. A load() here is itself a strong reference: the
// session it points to cannot be destroyed while that reference is held,
// even if another thread calls CloseDiagnostics concurrently (the TAP is
// called from several explorer UI threads - see g_site in
// tap_boundary.cpp/site.h for the same reasoning). This is what closed the
// heap use-after-free a plain std::atomic<DiagnosticsSession*> had: that
// scheme synchronized the pointer's value, not the pointee's lifetime.
std::atomic<std::shared_ptr<DiagnosticsSession>> g_session{nullptr};

// Handles successfully released so far this process. Monotonic by
// construction: only ReleaseHandle's success path touches it, and only ever
// increments it.
std::atomic<long> g_released_handles{0};

}  // namespace

void ReleaseHandle(InstanceHandle handle) {
    if (!handle) {
        return;  // A root element's parent handle is 0; nothing to release.
    }

    std::shared_ptr<DiagnosticsSession> session = g_session.load();
    if (!session) {
        return;  // No session open.
    }

    HRESULT hr = session->ReleaseElementHandle(handle);
    if (hr == S_FALSE) {
        return;  // Hooks unavailable on this session - warned once already,
                 // in OpenDiagnostics.
    }
    if (SUCCEEDED(hr)) {
        g_released_handles.fetch_add(1, std::memory_order_relaxed);
    } else {
        STYLER_LOG(LogLevel::Error, L"UnregisterInstance(%llu) failed 0x%08X",
                   static_cast<unsigned long long>(handle),
                   static_cast<unsigned>(hr));
    }
}

long ReleasedHandleCount() {
    return g_released_handles.load(std::memory_order_relaxed);
}

std::shared_ptr<DiagnosticsSession> AcquireSession() {
    return g_session.load();
}

HRESULT OpenDiagnostics(IUnknown* site) {
    if (!site) {
        return E_INVALIDARG;
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

    std::shared_ptr<DiagnosticsSession> session;
    try {
        session = std::make_shared<DiagnosticsSession>(diagnostics, hooks);
    } catch (const std::bad_alloc&) {
        if (hooks) {
            hooks->Release();
        }
        diagnostics->Release();
        return E_OUTOFMEMORY;
    }

    // exchange (not load-then-store) so two concurrent opens cannot each
    // read "no session yet" and both install one, leaking whichever session
    // gets overwritten without ever being closed. Whichever caller's
    // exchange runs second gets the other's session back as `previous` and
    // releases it below - exactly once, however many opens race.
    std::shared_ptr<DiagnosticsSession> previous = g_session.exchange(session);
    if (previous) {
        STYLER_LOG(LogLevel::Info,
                   L"OpenDiagnostics called with a session already open; "
                   L"closing and reopening");
    }
    // `previous` goes out of scope here. If this was its last reference, its
    // destructor (via DiagnosticsSession's) releases the old interfaces now;
    // if a concurrent ReleaseHandle is still holding a reference to it, this
    // merely drops our count and that call finishes safely on its own copy.

    STYLER_LOG(LogLevel::Info, L"diagnostics session open");
    return S_OK;
}

void CloseDiagnostics() {
    std::shared_ptr<DiagnosticsSession> session = g_session.exchange(nullptr);
    if (!session) {
        return;
    }
    // `session` is the last reference this function holds; if it is also the
    // last reference anywhere, dropping it here (end of scope) closes the
    // session. A concurrent ReleaseHandle holding its own reference keeps it
    // alive until that call returns - never a dangling access.
    STYLER_LOG(LogLevel::Info, L"diagnostics session closed");
}

}  // namespace styler::tap
