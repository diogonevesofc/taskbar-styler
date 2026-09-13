// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/theme_session.h>

#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <styler/config.h>
#include <styler/matcher.h>
#include <styler/theme_loader.h>
#include <tap/log.h>
#include <tap/style_engine.h>
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

}  // namespace

std::wstring ConfigPath() {
    wchar_t* appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        return L"";
    }
    std::wstring path(appdata);
    CoTaskMemFree(appdata);
    path += L"\\TaskbarStyler\\config.json";
    return path;
}

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
        auto prepared = std::make_shared<const styler::ResolvedTheme>(
            styler::PrepareTheme(theme));
        for (const auto& line : prepared->diagnostics) {
            STYLER_LOG(LogLevel::Error, L"%s", line.c_str());
        }
        STYLER_LOG(LogLevel::Info,
                   L"theme %s: %zu rules prepared, %d captures and %d dynamic "
                   L"values skipped, %d blur approximations",
                   prepared->id.c_str(), prepared->rules.size(),
                   prepared->skipped_captures, prepared->skipped_dynamic,
                   prepared->blur_approximations);
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

}  // namespace styler::tap
