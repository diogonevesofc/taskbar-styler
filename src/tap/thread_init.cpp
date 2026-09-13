// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/thread_init.h>

#include <atomic>
#include <cwchar>

#include <tap/log.h>

namespace styler::tap {
namespace {

thread_local bool t_initialized = false;
std::atomic<HWINEVENTHOOK> g_host_hook{nullptr};

struct RunParam {
    ThreadProc proc;
    void* param;
    HWND target;
};

UINT RunMessage() {
    static const UINT msg =
        RegisterWindowMessageW(L"TaskbarStyler_RunOnWindowThread");
    return msg;
}

BOOL CALLBACK EnumHostsProc(HWND hWnd, LPARAM lParam) {
    auto* out = reinterpret_cast<std::vector<HWND>*>(lParam);

    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hWnd, &pid) || pid != GetCurrentProcessId()) {
        return TRUE;
    }

    wchar_t cls[64]{};
    if (GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) == 0) {
        return TRUE;
    }

    if (_wcsicmp(cls, L"XamlExplorerHostIslandWindow") == 0 ||
        _wcsicmp(cls, L"Shell_InputSwitchTopLevelWindow") == 0) {
        out->push_back(hWnd);
    }
    return TRUE;
}

BOOL CALLBACK FindTrayProc(HWND hWnd, LPARAM lParam) {
    DWORD pid = 0;
    wchar_t cls[64]{};
    if (GetWindowThreadProcessId(hWnd, &pid) && pid == GetCurrentProcessId() &&
        GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) &&
        _wcsicmp(cls, L"Shell_TrayWnd") == 0) {
        *reinterpret_cast<HWND*>(lParam) = hWnd;
        return FALSE;
    }
    return TRUE;
}

void WINAPI InitThunk(void*) {
    InitializeForCurrentThread();
}

void CALLBACK HostCreatedProc(HWINEVENTHOOK, DWORD event, HWND hWnd,
                              LONG idObject, LONG, DWORD, DWORD) {
    if (event != EVENT_OBJECT_CREATE || idObject != OBJID_WINDOW || !hWnd) {
        return;
    }

    wchar_t cls[64]{};
    if (GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) == 0) {
        return;
    }
    if (_wcsicmp(cls, L"XamlExplorerHostIslandWindow") != 0) {
        return;
    }

    STYLER_LOG(LogLevel::Info, L"new XAML host %p", hWnd);
    RunOnWindowThread(hWnd, InitThunk, nullptr);
}

}  // namespace

std::vector<HWND> GetXamlHostWnds() {
    std::vector<HWND> hosts;
    EnumWindows(EnumHostsProc, reinterpret_cast<LPARAM>(&hosts));
    return hosts;
}

HWND GetTaskbarUiWnd() {
    HWND tray = nullptr;
    EnumWindows(FindTrayProc, reinterpret_cast<LPARAM>(&tray));
    if (!tray) {
        return nullptr;
    }
    return FindWindowExW(tray, nullptr,
                         L"Windows.UI.Composition.DesktopWindowContentBridge",
                         nullptr);
}

bool RunOnWindowThread(HWND hWnd, ThreadProc proc, void* param) {
    DWORD thread_id = GetWindowThreadProcessId(hWnd, nullptr);
    if (thread_id == 0) {
        return false;
    }
    if (thread_id == GetCurrentThreadId()) {
        proc(param);
        return true;
    }

    RunParam rp{proc, param, hWnd};

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (code == HC_ACTION) {
                const auto* cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
                if (cwp->message == RunMessage()) {
                    auto* p = reinterpret_cast<RunParam*>(cwp->lParam);
                    // Registered window messages are process-global: this
                    // hook is scoped to a thread, not a window, so any other
                    // window on the same thread - or, in principle, an
                    // unrelated HWND_BROADCAST reusing this message id -
                    // would otherwise be treated as if it carried our own
                    // RunParam. Upstream has the same hole; this closes it
                    // for our own concurrent RunOnWindowThread calls that
                    // happen to share a thread_id, which is the realistic
                    // case.
                    if (cwp->hwnd == p->target) {
                        p->proc(p->param);
                    }
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        },
        nullptr, thread_id);

    if (!hook) {
        return false;
    }

    SendMessageW(hWnd, RunMessage(), 0, reinterpret_cast<LPARAM>(&rp));
    UnhookWindowsHookEx(hook);
    return true;
}

void InitializeForCurrentThread() {
    if (t_initialized) {
        return;
    }
    t_initialized = true;
    STYLER_LOG(LogLevel::Info, L"initialized for thread %lu",
               GetCurrentThreadId());
}

void UninitializeForCurrentThread() {
    if (!t_initialized) {
        return;
    }
    t_initialized = false;
    STYLER_LOG(LogLevel::Info, L"uninitialized for thread %lu",
               GetCurrentThreadId());
}

bool IsInitializedForCurrentThread() {
    return t_initialized;
}

void StartHostWatch() {
    HWINEVENTHOOK hook =
        SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
                        HostCreatedProc, GetCurrentProcessId(), 0,
                        WINEVENT_OUTOFCONTEXT);
    STYLER_LOG(LogLevel::Info, L"host watch %s",
               hook ? L"started" : L"FAILED to start");

    // exchange (not check-then-act) so two concurrent SetSite calls cannot
    // both read "no hook yet" and both install one, leaking whichever hook
    // gets overwritten without ever being unhooked - same reasoning as
    // OpenDiagnostics's g_session.exchange (visual_tree_watcher.cpp).
    HWINEVENTHOOK previous = g_host_hook.exchange(hook);
    if (previous) {
        UnhookWinEvent(previous);
    }
}

void StopHostWatch() {
    HWINEVENTHOOK hook = g_host_hook.exchange(nullptr);
    if (hook) {
        UnhookWinEvent(hook);
    }
}

}  // namespace styler::tap
