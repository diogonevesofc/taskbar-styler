// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/log.h>

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>
#include <string>

namespace styler::tap {
namespace {

// Info-level events (SetSite, "loaded into ...") fire once per load, not per
// element - the hot path (the tree_export.cpp snapshot walk, hundreds of
// elements per export) would log at Debug if it ever needed to, which stays
// gated by this default. Nothing needs Error-only protection, so Info is
// the default rather than something opt-in.
std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_file_mutex;

constexpr long long kMaxLogBytes = 1024 * 1024;

std::wstring LogPath() {
    std::wstring dir = StylerDataDir();
    if (dir.empty()) {
        return {};
    }
    return dir + L"\\log.txt";
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

std::wstring StylerDataDir() {
    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    std::wstring dir = std::wstring(base) + L"\\TaskbarStyler";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void SetLogLevel(LogLevel level) {
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel GetLogLevel() {
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

    // The tray reads while TAP callbacks append. _wfopen_s denies sharing,
    // so even a FILE_SHARE_WRITE reader made entire lifecycle records vanish.
    FILE* f = _wfsopen(path.c_str(), L"a+, ccs=UTF-8", _SH_DENYNO);
    if (f) {
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
