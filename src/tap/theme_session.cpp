// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/theme_session.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <mutex>
#include <sstream>
#include <system_error>

#include <styler/config.h>
#include <styler/matcher.h>
#include <styler/theme_loader.h>
#include <tap/change_subscription.h>
#include <tap/command_mailbox.h>
#include <tap/release_queue.h>
#include <tap/tree_export.h>
#include <tap/site.h>
#include <tap/ipc.h>
#include <tap/log.h>
#include <tap/style_engine.h>
#include <tap/thread_init.h>
#include <tap/visual_tree_watcher.h>

namespace styler::tap {
namespace {

// A theme id names a file under themes/; keep it to what the converter
// emits (letters, digits, '_', '&', '.', '-') so a config cannot point
// outside that directory.
bool ValidThemeId(const std::wstring& id) {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    for (wchar_t c : id) {
        bool ok = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'a' && c <= L'z') || c == L'_' || c == L'&' ||
                  c == L'-' || c == L'.';
        if (!ok) {
            return false;
        }
    }
    return id.find(L"..") == std::wstring::npos;
}

LogLevel ParseLevel(const std::wstring& s, LogLevel fallback) {
    if (s == L"error") return LogLevel::Error;
    if (s == L"info") return LogLevel::Info;
    if (s == L"debug") return LogLevel::Debug;
    return fallback;
}

// Upstream vendor:IsOsFeatureEnabled - a read-only query to ntdll for one OS
// feature flag's rollout state, by GetProcAddress (no injection: this never
// writes to another process). nullopt when it cannot be determined (older
// ntdll, or the query itself failing) - callers treat that as "off".
std::optional<bool> IsOsFeatureEnabled(std::uint32_t feature_id) {
    enum FEATURE_ENABLED_STATE {
        FEATURE_ENABLED_STATE_DEFAULT = 0,
        FEATURE_ENABLED_STATE_DISABLED = 1,
        FEATURE_ENABLED_STATE_ENABLED = 2,
    };
#pragma pack(push, 1)
    struct RTL_FEATURE_CONFIGURATION {
        unsigned int featureId;
        unsigned __int32 group : 4;
        FEATURE_ENABLED_STATE enabledState : 2;
        unsigned __int32 enabledStateOptions : 1;
        unsigned __int32 unused1 : 1;
        unsigned __int32 variant : 6;
        unsigned __int32 variantPayloadKind : 2;
        unsigned __int32 unused2 : 16;
        unsigned int payload;
    };
#pragma pack(pop)
    using Fn = int(NTAPI*)(UINT32, int, INT64*, RTL_FEATURE_CONFIGURATION*);
    static Fn query = []() -> Fn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        return ntdll ? reinterpret_cast<Fn>(
                           GetProcAddress(ntdll, "RtlQueryFeatureConfiguration"))
                     : nullptr;
    }();
    if (!query) {
        return std::nullopt;
    }
    RTL_FEATURE_CONFIGURATION feature{};
    INT64 change_stamp = 0;
    if (query(feature_id, 1, &change_stamp, &feature) < 0) {
        return std::nullopt;
    }
    switch (feature.enabledState) {
        case FEATURE_ENABLED_STATE_DISABLED: return false;
        case FEATURE_ENABLED_STATE_ENABLED: return true;
        default: return std::nullopt;
    }
}

void WINAPI RestoreThunk(void*) {
    // A timed-out send may finish later. Do not let an old restore undo a
    // theme installed by a subsequent, successful reload.
    if (!CurrentTheme()) {
        RestoreAllOnThisThread();
    }
}

constexpr unsigned kReloadCommand = 1;
constexpr unsigned kExportCommand = 2;
constexpr unsigned kContinueCommand = 4;
constexpr unsigned kSiteCommand = 8;
auto* const g_mailbox = new CommandMailbox;
std::atomic<HWND> g_command_window{nullptr};
struct SiteRequest {
    std::uint64_t version = 0;
    std::shared_ptr<IUnknown> site;
};
auto* const g_site_request = new std::atomic<std::shared_ptr<SiteRequest>>;
auto* const g_site_request_mutex = new std::mutex;
std::uint64_t g_site_version = 0;
struct CommandEvent {
    HANDLE event = nullptr;
    HANDLE wait = nullptr;
    std::uint64_t generation = 0;
    unsigned command = 0;
};
struct ReloadWatch { CommandEvent reload; CommandEvent export_tree; };
auto* const g_reload = new std::atomic<ReloadWatch*>{nullptr};
std::atomic<bool> g_export_running{false};
std::atomic<bool> g_exporting_tree{false};
std::atomic<HRESULT> g_export_result{S_OK};
std::atomic<std::uint64_t> g_drain_generation{0};
std::atomic<bool> g_drain_failed{false};
thread_local bool t_pending_transition = false;
thread_local bool t_export_requested = false;
thread_local bool t_driving = false;
thread_local std::uint64_t t_site_version = 0;
thread_local std::uint64_t t_command_revision = 0;

void PostCommand(unsigned command, std::uint64_t generation) noexcept {
    try {
        if (g_mailbox->Push(generation, command) &&
            !PostThreadCommand(g_command_window.load(), 0, generation)) {
            g_mailbox->PostFailed(generation);
            STYLER_LOG(LogLevel::Error, L"posting TAP command failed");
        }
    } catch (...) {
    }
}
void NotifyContinuation() noexcept {
    try {
        PostCommand(kContinueCommand, g_mailbox->generation());
    } catch (...) {
    }
}
void CALLBACK OnCommandSignaled(void* context, BOOLEAN) {
    // No COM, no synchronous send, and no UI dependency in this callback.
    try {
        const auto* event = static_cast<CommandEvent*>(context);
        PostCommand(event->command, event->generation);
    } catch (...) {
    }
}
void DriveTransition();
void OnCommand(unsigned, std::uint64_t generation) {
    const unsigned commands = g_mailbox->Take(generation);
    if (!commands) return;
    if (commands & (kReloadCommand | kSiteCommand | kExportCommand)) {
        t_pending_transition = true;
        ++t_command_revision;
    }
    if ((commands & kExportCommand) && !g_exporting_tree.load()) t_export_requested = true;
    if (commands & kReloadCommand) STYLER_LOG(LogLevel::Info, L"reload requested");
    DriveTransition();
}
void WINAPI InitializeControl(void*) {
    const HWND window = SetCommandHandlerForCurrentThread(OnCommand);
    g_command_window.store(window);
    if (window) StartReloadWatch();
}
void WINAPI InitializeHost(void*) { InitializeForCurrentThread(); }
void WINAPI DrainThread(void* encoded_generation) {
    const auto generation = reinterpret_cast<std::uintptr_t>(encoded_generation);
    if (generation != g_drain_generation.load() || CurrentTheme()) return;
    if (!StopReleaseQueueOnThisThread()) g_drain_failed.store(true);
}
bool DrainAllThreads() {
    const auto generation = g_drain_generation.fetch_add(1) + 1;
    g_drain_failed.store(false);
    auto* encoded = reinterpret_cast<void*>(static_cast<std::uintptr_t>(generation));
    DrainThread(encoded);
    bool dispatched = true;
    for (HWND window : GetInitializedThreadWnds()) {
        if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() &&
            !RunOnWindowThread(window, DrainThread, encoded)) dispatched = false;
    }
    return dispatched && !g_drain_failed.load();
}
void StartExportWorker(bool export_tree) {
    g_exporting_tree.store(export_tree);
    g_export_running.store(true);
    g_export_result.store(S_OK);
    HANDLE thread = CreateThread(nullptr, 0, [](void* parameter) -> DWORD {
        HRESULT hr = E_FAIL;
        try {
            hr = parameter ? ExportTreeToFile(StylerDataDir() + L"\\visual-tree.txt")
                           : StopPendingSnapshot();
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"export worker threw");
        }
        if (SUCCEEDED(hr) && SnapshotNeedsStop()) hr = E_FAIL;
        g_export_result.store(hr);
        g_export_running.store(false);
        g_exporting_tree.store(false);
        NotifyContinuation();
        return 0;
    }, export_tree ? reinterpret_cast<void*>(1) : nullptr, 0, nullptr);
    if (!thread) {
        g_export_running.store(false);
        g_exporting_tree.store(false);
        g_export_result.store(HRESULT_FROM_WIN32(GetLastError()));
        t_pending_transition = false;
        STYLER_LOG(LogLevel::Error, L"cannot start export worker");
    } else {
        CloseHandle(thread);
    }
}
}  // namespace

HRESULT LoadConfiguredTheme() {
    styler::Config config;
    std::wstring path = ConfigPath();
    if (!path.empty()) {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        if (in) {
            std::stringstream buf;
            buf << in.rdbuf();
            try {
                config = styler::ParseConfigJson(buf.str());
            } catch (const styler::ParseError& ex) {
                STYLER_LOG(LogLevel::Error, L"config.json invalid: %S", ex.what());
                SetTheme(nullptr);
                return E_INVALIDARG;
            } catch (const std::exception& ex) {
                STYLER_LOG(LogLevel::Error, L"config.json failed: %S", ex.what());
                SetTheme(nullptr);
                return E_INVALIDARG;
            }
        } else {
            // Distinguish "no config yet" (normal on a first run - path
            // simply does not exist, fall through to "no theme configured"
            // below) from "a config exists but this process could not read
            // it" (a permissions problem or a locked file - worth an Error,
            // not a silent, misleading "no theme configured").
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                STYLER_LOG(LogLevel::Error,
                           L"config.json exists but could not be opened: %s",
                           path.c_str());
                SetTheme(nullptr);
                return E_ACCESSDENIED;
            }
        }
    }
    if (!config.log_level.empty()) {
        SetLogLevel(ParseLevel(config.log_level, GetLogLevel()));
    }
    if (config.theme.empty()) {
        SetTheme(nullptr);
        STYLER_LOG(LogLevel::Info, L"no theme configured");
        return S_FALSE;
    }
    if (!ValidThemeId(config.theme)) {
        STYLER_LOG(LogLevel::Error, L"theme id rejected: %s", config.theme.c_str());
        SetTheme(nullptr);
        return E_INVALIDARG;
    }
    std::wstring dir = InitializationData();
    if (dir.empty()) {
        STYLER_LOG(LogLevel::Error, L"no themes directory was passed at load");
        SetTheme(nullptr);
        return E_NOT_VALID_STATE;
    }
    std::filesystem::path file = std::filesystem::path(dir) / (config.theme + L".json");
    try {
        styler::Theme theme = styler::LoadThemeFromFile(file);
        if (theme.os_feature_variant) {
            const auto& v = *theme.os_feature_variant;
            if (IsOsFeatureEnabled(v.feature_id).value_or(false) &&
                ValidThemeId(v.theme_id)) {
                STYLER_LOG(LogLevel::Info, L"feature %u on: using variant %s",
                           v.feature_id, v.theme_id.c_str());
                theme = styler::LoadThemeFromFile(
                    std::filesystem::path(dir) / (v.theme_id + L".json"));
            }
        }
        auto prepared = std::make_shared<const styler::ResolvedTheme>(
            styler::PrepareTheme(theme));
        for (const auto& line : prepared->diagnostics) {
            STYLER_LOG(LogLevel::Error, L"%s", line.c_str());
        }
        STYLER_LOG(LogLevel::Info,
                   L"theme %s: %zu rules prepared, %d captures, %d "
                   L"dynamic values, %d blur brushes, %d blur approximations",
                   prepared->id.c_str(), prepared->rules.size(),
                   prepared->captures, prepared->dynamic_values,
                   prepared->blur_specs, prepared->blur_approximations);
        SetTheme(prepared);
        return S_OK;
    } catch (const styler::ParseError& ex) {
        STYLER_LOG(LogLevel::Error, L"theme %s rejected: %S", config.theme.c_str(),
                   ex.what());
    } catch (const std::exception& ex) {
        STYLER_LOG(LogLevel::Error, L"theme %s failed: %S", config.theme.c_str(),
                   ex.what());
    }
    SetTheme(nullptr);
    return E_FAIL;
}

bool RestoreThemeOnAllThreads() {
    SetTheme(nullptr);
    bool complete = true;
    try {
        RestoreAllOnThisThread();
    } catch (...) {
        complete = false;
        STYLER_LOG(LogLevel::Error, L"restore on current thread failed");
    }
    // Each initialized thread owns a message-only window for its lifetime;
    // the transient XAML host HWND is not a reliable way to find TLS state.
    try {
        for (HWND window : GetInitializedThreadWnds()) {
            if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() &&
                !RunOnWindowThread(window, RestoreThunk, nullptr)) {
                complete = false;
            }
        }
    } catch (...) {
        complete = false;
    }
    if (!complete) {
        STYLER_LOG(LogLevel::Error,
                   L"restore incomplete: theme remains disabled; retry required");
    }
    return complete;
}

namespace {
void DriveTransition() {
    if (!t_pending_transition || t_driving || g_export_running.load()) return;
    const auto command_revision = t_command_revision;
    t_driving = true;
    struct End { ~End() { t_driving = false; } } end;
    try {
        // All transitions run on this one UI thread. Worker completions only
        // post messages; no wait blocks the UI needed by Advise's flood.
        if (!RestoreThemeOnAllThreads() || !StopHostWatch()) {
            t_pending_transition = false;
            return;
        }
        const auto stopped = StopSubscription();
        if (stopped == StopResult::Deferred) return;
        if (stopped == StopResult::Failed) {
            t_pending_transition = false;
            return;
        }
        if (SnapshotNeedsStop()) {
            if (FAILED(g_export_result.exchange(S_OK))) {
                t_pending_transition = false;
                STYLER_LOG(LogLevel::Error, L"snapshot cleanup incomplete; explicit retry required");
                return;
            }
            StartExportWorker(false);
            return;
        }
        if (!DrainAllThreads()) {
            t_pending_transition = false;
            STYLER_LOG(LogLevel::Error, L"transition incomplete: retained releases or thread timeout; retry required");
            return;
        }
        auto request = g_site_request->load();
        if (request && (request->version != t_site_version ||
                        (request->site && !AcquireSession()))) {
            CloseDiagnostics();
            t_site_version = request->version;
            if (request->site) {
                const HRESULT hr = OpenDiagnostics(request->site.get());
                if (FAILED(hr)) {
                    t_pending_transition = false;
                    STYLER_LOG(LogLevel::Error, L"OpenDiagnostics failed 0x%08X", static_cast<unsigned>(hr));
                    return;
                }
                // Preserve the CLI load contract, with a worker so the UI
                // remains free to process the synchronous snapshot flood.
                t_export_requested = true;
            }
        }
        if (!request || !request->site) {
            const auto latest = g_site_request->load();
            if ((latest && latest->version != t_site_version) ||
                t_command_revision != command_revision) {
                PostCommand(kContinueCommand, g_mailbox->generation());
                return;
            }
            t_pending_transition = false;
            t_export_requested = false;
            StopReloadWatch();
            LogHandleObservation();
            STYLER_LOG(LogLevel::Info, L"TAP detached: no subscription, host hook or release timer");
            return;
        }
        if (!AcquireSession()) {
            t_pending_transition = false;
            return;
        }
        if (t_export_requested) {
            t_export_requested = false;
            StartExportWorker(true);
            return;
        }
        const HRESULT export_result = g_export_result.exchange(S_OK);
        if (FAILED(export_result)) {
            STYLER_LOG(LogLevel::Error, L"export failed 0x%08X", static_cast<unsigned>(export_result));
        }
        const HRESULT theme_result = LoadConfiguredTheme();
        // A SetSite/config command reentered while restoring/reading XAML.
        // Never publish the outgoing work as final; process the latest request.
        const auto latest = g_site_request->load();
        if ((latest && latest->version != t_site_version) ||
            t_command_revision != command_revision ||
            (g_mailbox->Pending(g_mailbox->generation()) &
                (kReloadCommand | kExportCommand | kSiteCommand))) {
            PostCommand(kContinueCommand, g_mailbox->generation());
            return;
        }
        t_pending_transition = false;
        if (FAILED(theme_result) || !CurrentTheme()) {
            LogHandleObservation();
            STYLER_LOG(LogLevel::Info, L"TAP inactive: no subscription, host hook or release timer");
            return;
        }
        for (HWND host : GetXamlHostWnds()) {
            if (!RunOnWindowThread(host, InitializeHost, nullptr)) {
                SetTheme(nullptr);
                STYLER_LOG(LogLevel::Error, L"apply deferred: host initialization incomplete");
                return;
            }
        }
        if (!StartHostWatch()) {
            SetTheme(nullptr);
            STYLER_LOG(LogLevel::Error, L"apply failed: host watch unavailable");
            return;
        }
        const HRESULT hr = StartSubscription();
        if (FAILED(hr)) {
            SetTheme(nullptr);
            StopHostWatch();
            STYLER_LOG(LogLevel::Error, L"StartSubscription failed 0x%08X", static_cast<unsigned>(hr));
        }
    } catch (winrt::hresult_error const& ex) {
        t_pending_transition = false;
        STYLER_LOG(LogLevel::Error, L"transition hresult 0x%08X", static_cast<unsigned>(ex.code()));
    } catch (...) {
        t_pending_transition = false;
        STYLER_LOG(LogLevel::Error, L"transition threw");
    }
}
}  // namespace

void RequestSessionChange(std::shared_ptr<IUnknown> site) {
    auto request = std::make_shared<SiteRequest>();
    request->site = std::move(site);
    std::shared_ptr<SiteRequest> previous;
    {
        std::lock_guard lock(*g_site_request_mutex);
        request->version = ++g_site_version;
        previous = g_site_request->exchange(request);
    }
    HWND ui = GetTaskbarUiWnd();
    if (!ui || !RunOnWindowThread(ui, InitializeControl, nullptr)) {
        STYLER_LOG(LogLevel::Error, L"cannot initialize TAP command window; session request retained");
        return;
    }
    PostCommand(kSiteCommand, g_mailbox->generation());
}

std::shared_ptr<IUnknown> AcquireSite() {
    const auto request = g_site_request->load();
    return request ? request->site : nullptr;
}

HRESULT StartReloadWatch() {
    if (g_reload->load()) return S_FALSE;
    const auto window = g_command_window.load();
    if (!window) return E_NOT_VALID_STATE;
    auto* watch = new (std::nothrow) ReloadWatch;
    if (!watch) return E_OUTOFMEMORY;
    const auto generation = g_mailbox->Restart();
    auto create = [&](CommandEvent& event, const wchar_t* name, unsigned command) {
        event.generation = generation;
        event.command = command;
        event.event = CreateEventW(nullptr, FALSE, FALSE, name);
        return event.event && RegisterWaitForSingleObject(&event.wait, event.event,
            OnCommandSignaled, &event, INFINITE, WT_EXECUTEDEFAULT);
    };
    if (!create(watch->reload, kReloadEventName, kReloadCommand) ||
        !create(watch->export_tree, kExportEventName, kExportCommand)) {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        for (auto* event : {&watch->reload, &watch->export_tree}) {
            if (event->wait) UnregisterWaitEx(event->wait, INVALID_HANDLE_VALUE);
            if (event->event) CloseHandle(event->event);
        }
        delete watch;
        return hr;
    }
    g_reload->store(watch);
    SetSubscriptionCompletion(NotifyContinuation);
    STYLER_LOG(LogLevel::Info, L"passive reload/export command watches started");
    return S_OK;
}

void StopReloadWatch() {
    g_mailbox->Restart();
    auto* watch = g_reload->exchange(nullptr);
    if (!watch) return;
    bool stopped = true;
    for (auto* event : {&watch->reload, &watch->export_tree}) {
        // OnCommandSignaled only posts, so this wait has no UI dependency.
        if (event->wait && !UnregisterWaitEx(event->wait, INVALID_HANDLE_VALUE)) stopped = false;
    }
    if (!stopped) {
        STYLER_LOG(LogLevel::Error, L"command wait removal failed; context retained");
        return;
    }
    CloseHandle(watch->reload.event);
    CloseHandle(watch->export_tree.event);
    delete watch;
    STYLER_LOG(LogLevel::Info, L"passive command watches stopped");
}

}  // namespace styler::tap
