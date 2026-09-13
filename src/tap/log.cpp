// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/log.h>

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>

namespace styler::tap {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Error};
std::mutex g_file_mutex;

constexpr long long kMaxLogBytes = 1024 * 1024;

std::wstring ConfigDir() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    std::wstring dir = std::wstring(base) + L"\\TaskbarStyler";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring LogPath() {
    std::wstring dir = ConfigDir();
    if (dir.empty()) {
        return {};
    }
    return dir + L"\\log.txt";
}

// Nothing in this codebase ever calls SetLogLevel() outside of tests, so
// without this, Error is the only level anyone could ever observe once the
// TAP is loaded into explorer.exe - there is no IPC channel to reach a live
// TAP and raise it. This file is the knob instead of an environment
// variable: explorer.exe's environment is fixed at logon, long before this
// DLL is ever loaded into it, so an env var set afterwards would never be
// seen - a config file the CLI can write right before `load` will be, because
// this is the first time the DLL is loaded into that explorer process.
// Read once per process (see the call site in GetLogLevel()): a later edit to
// the file has no effect on a TAP already resident in a running explorer.
LogLevel LevelFromConfigFile() {
    std::wstring dir = ConfigDir();
    if (dir.empty()) {
        return LogLevel::Error;
    }
    std::wstring path = dir + L"\\loglevel.txt";

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) {
        return LogLevel::Error;
    }
    wchar_t buf[16]{};
    wchar_t* got = fgetws(buf, static_cast<int>(std::size(buf)), f);
    fclose(f);
    if (!got) {
        return LogLevel::Error;
    }

    size_t len = wcslen(buf);
    while (len > 0 && (buf[len - 1] == L'\n' || buf[len - 1] == L'\r' ||
                       buf[len - 1] == L' ')) {
        buf[--len] = L'\0';
    }

    if (_wcsicmp(buf, L"debug") == 0) {
        return LogLevel::Debug;
    }
    if (_wcsicmp(buf, L"info") == 0) {
        return LogLevel::Info;
    }
    return LogLevel::Error;
}

const wchar_t* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Error:
            return L"ERR ";
        case LogLevel::Info:
            return L"INFO";
        case LogLevel::Debug:
            return L"DBG ";
    }
    return L"????";
}

// One rotation, not a series: the old file is replaced. A styling tool does not
// need log archaeology, and unbounded growth inside explorer.exe is worse than
// losing yesterday's lines.
void RotateIfLarge(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        return;
    }
    long long size =
        (static_cast<long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size < kMaxLogBytes) {
        return;
    }
    std::wstring old = path + L".1";
    DeleteFileW(old.c_str());
    MoveFileW(path.c_str(), old.c_str());
}

}  // namespace

void SetLogLevel(LogLevel level) {
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel GetLogLevel() {
    // Applies the config-file level exactly once per process, the first time
    // anyone asks. A function-local static (not a global one) so the file
    // read happens on first use - same lazy pattern LogPath() already uses -
    // rather than during DllMain's static initialization, which callers
    // reach through a loader lock.
    static const LogLevel applied = [] {
        LogLevel level = LevelFromConfigFile();
        g_level.store(level, std::memory_order_relaxed);
        return level;
    }();
    (void)applied;
    return g_level.load(std::memory_order_relaxed);
}

bool LogEnabled(LogLevel level) {
    return static_cast<int>(level) <= static_cast<int>(GetLogLevel());
}

void LogLine(LogLevel level, std::wstring_view line) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t prefix[96]{};
    swprintf_s(prefix, L"[%02d:%02d:%02d.%03d %lu/%lu] %s ", st.wHour,
               st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(),
               GetCurrentThreadId(), LevelTag(level));

    std::wstring full = std::wstring(prefix) + std::wstring(line) + L"\n";

    // Always available, free when nobody listens, visible live in DebugView.
    OutputDebugStringW(full.c_str());

    static const std::wstring path = LogPath();
    if (path.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_file_mutex);
    RotateIfLarge(path);

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a+, ccs=UTF-8") == 0 && f) {
        fputws(full.c_str(), f);
        fclose(f);
    }
}

void LogLineFormatted(LogLevel level, const wchar_t* fmt, ...) {
    wchar_t buf[1024]{};

    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    LogLine(level, buf);
}

}  // namespace styler::tap
