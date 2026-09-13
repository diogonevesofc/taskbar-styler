// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler::tap {

enum class LogLevel { Error = 0, Info = 1, Debug = 2 };

void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();

// The %LOCALAPPDATA%\TaskbarStyler directory, created if it does not exist
// yet. Empty if LOCALAPPDATA is not set. Shared by LogPath (log.cpp) and
// tap_boundary.cpp's visual-tree.txt export path, so there is exactly one
// place that creates the directory rather than one call site relying on the
// other having already run first.
std::wstring StylerDataDir();

// Called on the hot path: the visual tree snapshot (tree_export.cpp) walks
// hundreds of elements per export. Must stay a plain integer comparison.
bool LogEnabled(LogLevel level);

void LogLine(LogLevel level, std::wstring_view line);

// wsprintf-style. Never call directly; use STYLER_LOG, which checks the level
// BEFORE evaluating the arguments.
void LogLineFormatted(LogLevel level, const wchar_t* fmt, ...);

}  // namespace styler::tap

// The guard is the point: without it, a Debug call pays for argument
// evaluation and formatting even when logging is off.
#define STYLER_LOG(level, ...)                                   \
    do {                                                         \
        if (::styler::tap::LogEnabled(level)) {                  \
            ::styler::tap::LogLineFormatted(level, __VA_ARGS__); \
        }                                                        \
    } while (0)
