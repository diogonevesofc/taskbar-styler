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

// Asks the XAML framework inside `pid` to load our TAP. This is the whole
// trick: the OS does the loading, so no injection API is used anywhere.
LoadResult LoadTap(DWORD pid, const std::wstring& tap_path);

std::wstring DescribeHresult(HRESULT hr);

}  // namespace styler::cli
