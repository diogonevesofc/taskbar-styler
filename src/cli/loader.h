// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <string>

namespace styler::cli {

struct LoadResult {
    HRESULT hr = E_FAIL;
    DWORD pid = 0;
    std::wstring connection;  // The VisualDiagConnection name that took.
};

// PID of the explorer.exe that owns the taskbar, or 0.
DWORD FindTaskbarPid();

// Absolute path to TaskbarStyler.Tap.dll, expected next to this executable.
// Empty when it is not there.
std::wstring TapDllPath();

// Loads the TAP into `pid`. `init_data` is handed to the TAP verbatim through
// IXamlDiagnostics::GetInitializationData - today it carries the absolute
// path of the themes directory. May be empty.
LoadResult LoadTap(DWORD pid, const std::wstring& tap_path,
                   const std::wstring& init_data);

// <directory of this exe>\themes, or empty when that directory does not exist.
std::wstring ThemesDir();

std::wstring DescribeHresult(HRESULT hr);

}  // namespace styler::cli
