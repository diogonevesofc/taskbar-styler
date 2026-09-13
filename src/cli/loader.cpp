// SPDX-License-Identifier: GPL-3.0-or-later
#include <cli/loader.h>

#include <cstdio>

#include <tap/clsid.h>

namespace styler::cli {
namespace {

using PfnInitializeXamlDiagnosticsEx =
    HRESULT(WINAPI*)(PCWSTR endPointName, DWORD pid,
                     PCWSTR wszDllXamlDiagnostics, PCWSTR wszTAPDllName,
                     CLSID tapClsid, PCWSTR wszInitializationData);

// The XAML framework hands out a fixed set of diagnostic connection slots.
// Visual Studio's Live Visual Tree occupies one while attached, so a free slot
// has to be searched for. Upstream does the same.
constexpr int kMaxConnectionAttempts = 10000;

}  // namespace

DWORD FindTaskbarPid() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return 0;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    return pid;
}

std::wstring TapDllPath() {
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        return {};
    }
    std::wstring path(exe);
    auto slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return {};
    }
    path.replace(slash + 1, std::wstring::npos, L"TaskbarStyler.Tap.dll");

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    return path;
}

LoadResult LoadTap(DWORD pid, const std::wstring& tap_path,
                   const std::wstring& init_data) {
    LoadResult result;
    result.pid = pid;

    HMODULE wux = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wux) {
        result.hr = HRESULT_FROM_WIN32(GetLastError());
        return result;
    }

    auto ixde = reinterpret_cast<PfnInitializeXamlDiagnosticsEx>(
        GetProcAddress(wux, "InitializeXamlDiagnosticsEx"));
    if (!ixde) {
        result.hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return result;
    }

    const HRESULT kNotFound = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    for (int i = 0; i < kMaxConnectionAttempts; i++) {
        wchar_t connection[64]{};
        swprintf_s(connection, L"VisualDiagConnection%d", i + 1);

        HRESULT hr = ixde(connection, pid, L"", tap_path.c_str(),
                          styler::tap::CLSID_TaskbarStylerTap,
                          init_data.empty() ? nullptr : init_data.c_str());

        if (hr == kNotFound) {
            continue;  // Slot busy; try the next.
        }

        result.hr = hr;
        result.connection = connection;
        return result;
    }

    result.hr = kNotFound;
    return result;
}

std::wstring ThemesDir() {
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        return L"";
    }
    std::wstring dir(exe);
    size_t slash = dir.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return L"";
    }
    dir.resize(slash + 1);
    dir += L"themes";
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return L"";
    }
    return dir;
}

std::wstring DescribeHresult(HRESULT hr) {
    const wchar_t* name = nullptr;

    if (hr == S_OK) {
        name = L"S_OK";
    } else if (hr == E_ACCESSDENIED) {
        name = L"E_ACCESSDENIED";
    } else if (hr == E_INVALIDARG) {
        name = L"E_INVALIDARG";
    } else if (hr == E_NOINTERFACE) {
        name = L"E_NOINTERFACE";
    } else if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        name = L"ERROR_NOT_FOUND";
    } else if (hr == HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)) {
        name = L"ERROR_PROC_NOT_FOUND";
    }

    wchar_t buf[128]{};
    if (name) {
        swprintf_s(buf, L"0x%08X (%s)", static_cast<unsigned>(hr), name);
    } else {
        swprintf_s(buf, L"0x%08X", static_cast<unsigned>(hr));
    }
    return buf;
}

}  // namespace styler::cli
