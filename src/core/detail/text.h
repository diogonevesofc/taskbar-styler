// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace styler::detail {

// Matches upstream's TrimStringView (vendor/upstream/windows-11-taskbar-styler.wh.cpp:15117),
// including the vertical tab. Selectors and style rules are copy-pasted from
// WindhawkCompiler logs and hand-edited themes, both of which can carry
// whatever whitespace a text editor or clipboard happened to produce; trimming
// the same set upstream does keeps our parser's acceptance behavior identical
// to the mod we are re-implementing.
inline constexpr std::wstring_view kWhitespace = L" \t\r\v\n";

inline std::wstring_view Trim(std::wstring_view s) {
    auto first = s.find_first_not_of(kWhitespace);
    if (first == std::wstring_view::npos) {
        return {};
    }
    auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

}  // namespace styler::detail
