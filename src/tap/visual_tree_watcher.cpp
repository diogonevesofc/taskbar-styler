// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/visual_tree_watcher.h>

#include <atomic>
#include <memory>

#include <tap/log.h>
#include <tap/handle_ledger.h>

namespace styler::tap {
namespace {
auto* const g_handle_ledger = new HandleLedger;
std::atomic<std::uint64_t> g_next_owner{1};
}


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
    : diagnostics_(diagnostics), hooks_(hooks), owner_(g_next_owner.fetch_add(1)) {}

DiagnosticsSession::~DiagnosticsSession() {
    if (hooks_) {
        hooks_->Release();
    }
    if (diagnostics_) {
        diagnostics_->Release();
    }
}

HRESULT DiagnosticsSession::ReleaseElementHandle(InstanceHandle handle) const {
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
//
// Intentionally leaked: never destroyed. std::atomic<shared_ptr<T>> is not
// trivially destructible, so a namespace-scope instance would register a
// destructor that runs at DLL_PROCESS_DETACH - calling COM Release() on
// XAML objects under the loader lock during shell teardown, on a graceful
// explorer exit (logoff, shutdown, "Exit Explorer"). That is the classic
// shutdown-hang/AV shape. This module never tears down by design (see
// DllCanUnloadNow returning S_FALSE, tap_boundary.cpp) - leaking one
// session at process exit is the cheaper and safer half of that trade, and
// is exactly what the old plain-pointer version did too (it was trivially
// destructible, so nothing ran at exit and the session simply leaked).
auto* const g_session =
    new std::atomic<std::shared_ptr<DiagnosticsSession>>{nullptr};

// Handles successfully released so far this process. Monotonic by
// construction: only ReleaseHandle's success path touches it, and only ever
// increments it.
std::atomic<long> g_released_handles{0};

// Written once by OpenDiagnostics, read from any thread afterwards.
auto* const g_init_data =
    new std::atomic<std::shared_ptr<const std::wstring>>{
        std::make_shared<const std::wstring>()};

}  // namespace

void DiagnosticsSession::Observe(InstanceHandle handle) const noexcept {
    try {
        std::lock_guard lock(observation_mutex_);
        g_handle_ledger->Observe(owner_, handle, retired_);
    } catch (...) {
        g_handle_ledger->MarkIncomplete();
    }
}

void DiagnosticsSession::Retire() noexcept {
    try {
        std::lock_guard lock(observation_mutex_);
        retired_ = true;
        g_handle_ledger->MarkOwnerRetired(owner_);
    } catch (...) {
        g_handle_ledger->MarkIncomplete();
    }
}

void MarkHandleObservationIncomplete() noexcept {
    g_handle_ledger->MarkIncomplete();
}

bool ReleaseHandle(const std::shared_ptr<DiagnosticsSession>& owner,
                   InstanceHandle handle) {
    if (!handle) return true;
    if (!owner) {
        MarkHandleObservationIncomplete();
        return false;
    }
    auto token = g_handle_ledger->BeginRelease(owner->owner(), handle);
    const bool tracked = token.status == HandleReleaseStatus::Started;
    if (!tracked && token.status != HandleReleaseStatus::Untracked) return false;
    // Accounting allocation failure must not veto the real cleanup. An
    // untracked release remains explicitly incomplete and removes no record.
    if (!tracked) MarkHandleObservationIncomplete();
    if (!owner->has_hooks()) {
        if (tracked) g_handle_ledger->CompleteRelease(token, HandleReleaseOutcome::Unavailable);
        return false;
    }
    HRESULT hr = E_FAIL;
    try {
        hr = owner->ReleaseElementHandle(handle);
    } catch (...) {
        if (tracked) g_handle_ledger->CompleteRelease(token, HandleReleaseOutcome::Failed);
        throw;
    }
    if (tracked) g_handle_ledger->CompleteRelease(token, SUCCEEDED(hr)
        ? HandleReleaseOutcome::Succeeded : HandleReleaseOutcome::Failed);
    if (SUCCEEDED(hr)) {
        g_released_handles.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    STYLER_LOG(LogLevel::Error, L"UnregisterInstance(%llu) failed 0x%08X",
               static_cast<unsigned long long>(handle), static_cast<unsigned>(hr));
    return false;
}

void LogHandleObservation() {
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        MarkHandleObservationIncomplete();
        return;
    }
    GetSystemTimeAsFileTime(&now);
    auto value = [](FILETIME ft) {
        return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) |
               ft.dwLowDateTime;
    };
    const auto snapshot = g_handle_ledger->Snapshot();
    STYLER_LOG(LogLevel::Info,
        L"diagnostics handles: pid=%lu created=%llu observed=%zu incomplete=%u residual=%zu utc=%llu",
        GetCurrentProcessId(), value(created), snapshot.outstanding,
        snapshot.complete ? 0u : 1u, snapshot.residual_outstanding, value(now));
}
long ReleasedHandleCount() {
    return g_released_handles.load(std::memory_order_relaxed);
}

std::shared_ptr<DiagnosticsSession> AcquireSession() {
    return g_session->load();
}

std::wstring InitializationData() {
    return *g_init_data->load();
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

    {
        BSTR data = nullptr;
        std::wstring value;
        if (SUCCEEDED(diagnostics->GetInitializationData(&data)) && data) {
            value.assign(data, SysStringLen(data));
            SysFreeString(data);
        }
        g_init_data->store(std::make_shared<const std::wstring>(std::move(value)));
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
    } catch (...) {
        // Not just std::bad_alloc: anything escaping make_shared must still
        // release what was already QI'd, or it leaks both interfaces into a
        // process that never restarts. Fail closed.
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
    std::shared_ptr<DiagnosticsSession> previous = g_session->exchange(session);
    if (previous) {
        previous->Retire();
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
    std::shared_ptr<DiagnosticsSession> session = g_session->exchange(nullptr);
    if (!session) {
        return;
    }
    // `session` is the last reference this function holds; if it is also the
    // last reference anywhere, dropping it here (end of scope) closes the
    // session. A concurrent ReleaseHandle holding its own reference keeps it
    // alive until that call returns - never a dangling access.
    session->Retire();
    STYLER_LOG(LogLevel::Info, L"diagnostics session closed");
    LogHandleObservation();
}

}  // namespace styler::tap
