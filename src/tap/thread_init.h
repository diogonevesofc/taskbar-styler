// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <vector>

namespace styler::tap {

// The taskbar is not one window. The main UI is a
// Windows.UI.Composition.DesktopWindowContentBridge child of Shell_TrayWnd;
// flyouts and secondary-monitor taskbars are XamlExplorerHostIslandWindow; the
// language switcher is Shell_InputSwitchTopLevelWindow. Each runs on its own
// thread and must be initialized from that thread.
std::vector<HWND> GetXamlHostWnds();
HWND GetTaskbarUiWnd();

// One message-only window per initialized thread. Unlike the shell's XAML
// hosts, these remain available while a flyout is closed and its thread lives.
std::vector<HWND> GetInitializedThreadWnds();

using ThreadProc = void(WINAPI*)(void* parameter);

// Runs `proc` on the thread owning `hWnd`, via a WH_CALLWNDPROC hook and a
// registered message. Documented API - no code is patched.
//
// `false` means not confirmed - a timeout or a dispatch failure - not
// "proc did not run": on that path `proc` may still run later, on the
// target thread, after this call has already returned. `param` must
// therefore never point at storage the caller could free in the
// meantime - every current caller passes nullptr.
bool RunOnWindowThread(HWND hWnd, ThreadProc proc, void* param);

void InitializeForCurrentThread();
void UninitializeForCurrentThread();
bool IsInitializedForCurrentThread();

// SetWinEventHook on EVENT_OBJECT_CREATE, filtered to this process. The OS
// tells us when a new window appears, so we never patch CreateWindowExW the way
// upstream does. This is the last place a code patch could have crept in - see
// spec section 6.2.
void StartHostWatch();
void StopHostWatch();

}  // namespace styler::tap
