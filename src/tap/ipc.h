// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <string>

// Shared between the TAP (inside explorer) and the CLI/Tray. Nothing here
// links to either side: header-only on purpose.
namespace styler::tap {

// Auto-reset event. The TAP creates it at SetSite and waits on it; a writer
// sets it after rewriting config.json. Local\ scopes it to the session.
constexpr wchar_t kReloadEventName[] = L"Local\\TaskbarStyler.Reload";

// %APPDATA%\TaskbarStyler\config.json (spec section 4.2). Empty when the
// folder cannot be resolved.
inline std::wstring ConfigPath() {
    wchar_t* appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                    &appdata))) {
        return L"";
    }
    std::wstring path(appdata);
    CoTaskMemFree(appdata);
    path += L"\\TaskbarStyler\\config.json";
    return path;
}

}  // namespace styler::tap
