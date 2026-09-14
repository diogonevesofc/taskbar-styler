// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/blur_rewrite.h>

#include <array>

#include "detail/text.h"

namespace styler {
namespace {

// Returns the `Name="..."` attribute text (including the quotes) or empty.
std::wstring_view FindAttribute(std::wstring_view body,
                                std::wstring_view name) {
    size_t pos = 0;
    while ((pos = body.find(name, pos)) != std::wstring_view::npos) {
        bool at_start =
            pos == 0 || body[pos - 1] == L' ' || body[pos - 1] == L'\t';
        size_t eq = pos + name.size();
        if (at_start && eq + 1 < body.size() && body[eq] == L'=' &&
            body[eq + 1] == L'"') {
            size_t close = body.find(L'"', eq + 2);
            if (close == std::wstring_view::npos) {
                return {};
            }
            return body.substr(pos, close + 1 - pos);
        }
        pos = eq;
    }
    return {};
}

}  // namespace

std::wstring RewriteWindhawkBlur(std::wstring_view value, bool* rewritten) {
    *rewritten = false;
    std::wstring_view s = detail::Trim(value);
    std::wstring_view body = detail::MatchBlurTagBody(s);
    if (body.empty()) {
        return std::wstring(value);
    }
    if (body.ends_with(L"/>")) {
        body.remove_suffix(2);
    } else if (body.ends_with(L">")) {
        body.remove_suffix(1);
    }

    static constexpr std::array<std::wstring_view, 4> kKept = {
        L"TintColor", L"TintOpacity", L"TintLuminosityOpacity",
        L"FallbackColor"};

    std::wstring out = L"<AcrylicBrush";
    for (std::wstring_view name : kKept) {
        std::wstring_view attr = FindAttribute(body, name);
        if (!attr.empty()) {
            out += L' ';
            out += attr;
        }
    }
    out += L"/>";
    *rewritten = true;
    return out;
}

}  // namespace styler
