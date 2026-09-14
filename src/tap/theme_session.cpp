// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/theme_session.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <system_error>

#include <styler/config.h>
#include <styler/matcher.h>
#include <styler/theme_loader.h>
#include <tap/change_subscription.h>
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
    RestoreAllOnThisThread();
}

void WINAPI ReloadThunk(void*) {
    ReloadThemeOnUiThread();
}

struct ReloadWatch {
    HANDLE event = nullptr;
    HANDLE wait = nullptr;
};
// Heap-leaked like g_session (Plano 2, Ruling 11): no namespace-scope
// destructor runs at detach, so nothing here needs to survive one.
auto* const g_reload = new std::atomic<ReloadWatch*>{nullptr};

void CALLBACK OnReloadSignaled(void*, BOOLEAN) {
    // Thread-pool thread: only hop to the UI thread here.
    try {
        HWND ui = GetTaskbarUiWnd();
        if (!ui) {
            STYLER_LOG(LogLevel::Error, L"reload: taskbar UI window not found");
        } else {
            // A false return here means RunOnWindowThread already logged the
            // accurate reason itself (e.g. the timeout it reports on
            // SendMessageTimeoutW) - a second, less specific line on top of
            // it would only obscure that reason.
            RunOnWindowThread(ui, ReloadThunk, nullptr);
        }
    } catch (...) {
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

void ReloadThemeOnUiThread() {
    try {
        STYLER_LOG(LogLevel::Info, L"reload requested");
        // 0. Take the outgoing theme off before touching anything else.
        //    CurrentTheme() has exactly one reader (style_engine.cpp's
        //    OnElementAdded), so this makes every element re-reported as a
        //    side effect of the restore below - measured happening
        //    synchronously, on this same thread, while unapplying a
        //    property - inert instead of getting freshly styled with the
        //    theme that is on its way out (found in review: those
        //    re-reports land before LoadConfiguredTheme's own
        //    SetTheme(nullptr)/SetTheme(new) runs below, so without this
        //    they are matched against the OLD theme and never make it into
        //    RestoreAllOnThisThread's own snapshot - they survive, holding
        //    their handles, until some later reload happens to catch them).
        //    Doing this first also makes a Deferred return, just below,
        //    coherent: the taskbar ends up fully restored either way, never
        //    left mid-style with a theme that is no longer installed.
        SetTheme(nullptr);
        // 1. Restore, on every thread that may hold state. This thread
        //    first (direct call - it is the taskbar UI thread, the one
        //    SetSite ran on), then each other host through its own message
        //    loop.
        RestoreAllOnThisThread();
        for (HWND host : GetXamlHostWnds()) {
            if (GetWindowThreadProcessId(host, nullptr) != GetCurrentThreadId()) {
                RunOnWindowThread(host, RestoreThunk, nullptr);
            }
        }
        // 2. Drop the subscription so the re-advise below re-floods.
        //    Deferred means the advise thread is still inside
        //    AdviseVisualTreeChange (change_subscription.h) - starting a
        //    new subscription now would race it, and waiting for that
        //    thread here would deadlock (it marshals its walk onto this
        //    one). The config file is already written, so the next reload
        //    signal picks it up once the old subscription settles on its
        //    own.
        StopResult stop_result = StopSubscription();
        if (stop_result == StopResult::Deferred) {
            STYLER_LOG(LogLevel::Info,
                       L"reload ignored: previous subscription still "
                       L"advising - retry in a moment");
            return;
        }
        // 3. New theme (or none), then re-subscribe: the initial flood on
        //    this thread applies it to everything already on screen.
        LoadConfiguredTheme();
        HRESULT hr = StartSubscription();
        if (FAILED(hr)) {
            STYLER_LOG(LogLevel::Error, L"reload: StartSubscription 0x%08X",
                       static_cast<unsigned>(hr));
        }
        // No "reload applied: N elements" line here on purpose: reading
        // EngineStats now would always read zero, the same reason SetSite
        // never logs it either (tap_boundary.cpp's comment) - the flood is
        // XAML marshalling onto this thread, which cannot run until this
        // function returns to the message loop. RestoreAllOnThisThread just
        // above rearmed release_queue.cpp's first-drain log, so the real
        // count is one "apply (as of first drain)" log line away instead.
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"reload threw");
    }
}

HRESULT StartReloadWatch() {
    if (g_reload->load()) {
        return S_FALSE;
    }
    auto* watch = new (std::nothrow) ReloadWatch{};
    if (!watch) {
        return E_OUTOFMEMORY;
    }
    watch->event = CreateEventW(nullptr, FALSE, FALSE, kReloadEventName);
    if (!watch->event) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        delete watch;
        return hr;
    }
    if (!RegisterWaitForSingleObject(&watch->wait, watch->event, OnReloadSignaled,
                                     nullptr, INFINITE, WT_EXECUTEDEFAULT)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(watch->event);
        delete watch;
        return hr;
    }
    ReloadWatch* expected = nullptr;
    if (!g_reload->compare_exchange_strong(expected, watch)) {
        UnregisterWaitEx(watch->wait, INVALID_HANDLE_VALUE);
        CloseHandle(watch->event);
        delete watch;
        return S_FALSE;
    }
    STYLER_LOG(LogLevel::Info, L"reload watch started");
    return S_OK;
}

void StopReloadWatch() {
    ReloadWatch* watch = g_reload->exchange(nullptr);
    if (!watch) {
        return;
    }
    UnregisterWaitEx(watch->wait, INVALID_HANDLE_VALUE);  // Waits for a running callback.
    CloseHandle(watch->event);
    delete watch;
}

}  // namespace styler::tap
