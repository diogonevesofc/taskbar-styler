// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/thread_init.h>

#include <atomic>
#include <cwchar>
#include <new>

#include <tap/log.h>

namespace styler::tap {
namespace {

thread_local bool t_initialized = false;
thread_local HWND t_dispatch_window = nullptr;
std::atomic<HWINEVENTHOOK> g_host_hook{nullptr};

constexpr wchar_t kDispatchWindowClass[] =
    L"TaskbarStyler_ThreadDispatch_63762654_9768_4CAB_871C_386A9193E22D";

LRESULT CALLBACK DispatchWindowProc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    // No XAML/COM work during window destruction: thread/apartment teardown
    // may already be in progress. The OS owns the window's thread lifetime.
    if (message == WM_NCDESTROY && t_dispatch_window == window) {
        t_dispatch_window = nullptr;
        t_initialized = false;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateDispatchWindow() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DispatchWindowProc),
                           &module)) {
        return nullptr;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = DispatchWindowProc;
    cls.hInstance = module;
    cls.lpszClassName = kDispatchWindowClass;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    // The shell host is deliberately not a parent/owner: closing TaskView
    // must not destroy the only way to restore this thread's retained state.
    return CreateWindowExW(0, kDispatchWindowClass, nullptr, 0,
                           0, 0, 0, 0, HWND_MESSAGE, nullptr, module, nullptr);
}

struct RunParam {
    ThreadProc proc;
    void* param;
    HWND target;
    std::atomic<bool> completed{false};
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

std::vector<HWND> GetInitializedThreadWnds() {
    std::vector<HWND> windows;
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window,
                                    kDispatchWindowClass, nullptr)) != nullptr) {
        DWORD pid = 0;
        if (GetWindowThreadProcessId(window, &pid) && pid == GetCurrentProcessId()) {
            windows.push_back(window);
        }
    }
    return windows;
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
        try {
            proc(param);
            return true;
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"RunOnWindowThread callback threw");
            return false;
        }
    }

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
                        try {
                            p->proc(p->param);
                            p->completed.store(true, std::memory_order_release);
                        } catch (...) {
                            // A callback exception must not escape into the
                            // shell's message loop. The caller sees false.
                        }
                    }
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        },
        nullptr, thread_id);

    if (!hook) {
        return false;
    }

    // Heap, not stack: see the timeout branch below. nothrow because this
    // runs under SetSite and the reload pool thread, neither of which may
    // let an exception out.
    auto* rp = new (std::nothrow) RunParam{proc, param, hWnd};
    if (!rp) {
        UnhookWindowsHookEx(hook);
        return false;
    }

    // SendMessageW without a timeout parks this thread forever when the
    // target UI thread is wedged - and the caller here is often the reload
    // pool thread or SetSite, neither of which may hang the shell. The
    // timeout is generous (a XAML host busy with a layout pass legitimately
    // takes a while) but finite, and SMTO_ABORTIFHUNG returns at once when
    // the window is already marked not-responding instead of waiting out
    // the full budget. Inherited from the Plano 2 ledger ("SendMessageW sem
    // timeout no thread_init.cpp") and repeated by the Plano 3 final review.
    constexpr UINT kRunTimeoutMs = 5000;
    DWORD_PTR result = 0;
    LRESULT sent = SendMessageTimeoutW(hWnd, RunMessage(), 0,
                                       reinterpret_cast<LPARAM>(rp),
                                       SMTO_ABORTIFHUNG, kRunTimeoutMs,
                                       &result);
    // Captured before UnhookWindowsHookEx: that call can set the
    // thread's last-error itself, which would otherwise overwrite
    // whatever SendMessageTimeoutW left behind by the time the log call
    // below reads it.
    DWORD err = GetLastError();
    UnhookWindowsHookEx(hook);

    if (!sent) {
        // Timed out or failed. `rp` is deliberately leaked: the message may
        // still be sitting in the target's queue, and a hook procedure that
        // already started on that thread keeps running after
        // UnhookWindowsHookEx returns - either one can still read `rp`. One
        // RunParam per timeout is the price of never freeing memory another
        // thread may be dereferencing inside explorer. Callers treat false
        // as "not dispatched" and log; none of them retries in a loop (spec
        // section 6.5).
        STYLER_LOG(LogLevel::Error,
                   L"RunOnWindowThread timed out or failed for hwnd %p "
                   L"(thread %lu, error %lu)",
                   hWnd, thread_id, err);
        return false;
    }
    // A successful synchronous send returns only after the target thread
    // finished dispatching the message, hooks included: nothing can reach
    // `rp` any more.
    const bool completed = rp->completed.load(std::memory_order_acquire);
    delete rp;
    if (!completed) {
        STYLER_LOG(LogLevel::Error,
                   L"RunOnWindowThread callback did not complete for hwnd %p",
                   hWnd);
    }
    return completed;
}

void InitializeForCurrentThread() {
    if (t_initialized) {
        return;
    }
    t_dispatch_window = CreateDispatchWindow();
    if (!t_dispatch_window) {
        const DWORD error = GetLastError();
        STYLER_LOG(LogLevel::Error,
                   L"thread %lu initialization failed: dispatch window error %lu",
                   GetCurrentThreadId(), error);
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
    if (t_dispatch_window) {
        DestroyWindow(t_dispatch_window);
    }
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
