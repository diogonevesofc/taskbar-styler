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

// Recognizes `<WindhawkBlur ...` or its `<Blur ...` synonym (spec section
// 5.3) at the front of an already-trimmed value - the same test both
// ParseWindhawkBlur and RewriteWindhawkBlur need, so it lives here instead
// of being copy-pasted in blur.cpp and blur_rewrite.cpp. Matched only when
// the tag name is followed by a space, '/' or '>', so `<BlurFoo` does not
// falsely match `<Blur`. Returns the tag body starting right after the tag
// name (always non-empty when matched, since a self-closing or opening tag
// needs at least that one delimiter character); empty when `s` is not a
// blur element at all.
inline std::wstring_view MatchBlurTagBody(std::wstring_view s) {
    for (std::wstring_view tag : {L"<WindhawkBlur", L"<Blur"}) {
        if (s.starts_with(tag) && s.size() > tag.size() &&
            (s[tag.size()] == L' ' || s[tag.size()] == L'/' ||
             s[tag.size()] == L'>')) {
            return s.substr(tag.size());
        }
    }
    return {};
}

}  // namespace styler::detail
