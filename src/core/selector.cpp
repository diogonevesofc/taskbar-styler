// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/selector.h>

#include <limits>
#include <string>

#include "detail/text.h"

namespace styler {
namespace {

// `s` is already known to contain only ASCII digits (the caller checked via
// find_first_not_of). Upstream parses this with std::stoi, which throws
// std::out_of_range past INT_MAX; a hand-edited theme can write an index
// with far more digits than any real visual tree needs
// (e.g. `Grid[99999999999]`), which would silently overflow a naive
// digit-by-digit accumulation (undefined behavior for `int`). Guard both the
// digit count (an int has at most 10 digits) and the running total, and
// throw ParseError instead - mirroring stoi's failure mode without pulling
// in <charconv>/<stdexcept> conversions for a single-purpose parse.
int ParseIndex(std::wstring_view s) {
    constexpr size_t kMaxIntDigits = 10;  // INT_MAX = 2147483647, 10 digits.
    if (s.empty() || s.size() > kMaxIntDigits) {
        throw ParseError("Bad target syntax, index out of range");
    }
    long long value = 0;
    for (wchar_t c : s) {
        value = value * 10 + (c - L'0');
        if (value > std::numeric_limits<int>::max()) {
            throw ParseError("Bad target syntax, index out of range");
        }
    }
    return static_cast<int>(value);
}

// Mirrors upstream's per-chain validations (AddElementCustomizationRulesFor-
// SingleTarget, vendor/upstream/...:18859-18917), applied here to `parts` in
// the same left-to-right (leftmost ancestor first, matched element last)
// order ParseSelector already builds. Traversed back-to-front (matched
// element first) to match upstream's rbegin/rend walk exactly, since
// "adjacent to another '*'" and "first" are defined relative to that order.
//
// Deliberately NOT ported: upstream's ":root' must be followed by a
// non-wildcard target part" (same function, prevIsWildcard branch under
// Kind::Root). Zero shipped chains put '*' immediately after ':root', so it
// is out of this fix's measured scope; adding it is future work, not a
// silent gap - see docs/superpowers/specs, spec §7.6.
void ValidateChainStructure(const std::vector<ElementMatcher>& parts) {
    const size_t n = parts.size();
    bool has_visual_state_group = false;

    for (size_t idx = 0; idx < n; ++idx) {
        size_t i = n - 1 - idx;  // n-1, n-2, ..., 0: matched element first.
        const auto& matcher = parts[i];
        bool is_first = (i == n - 1);       // The matched (last) element.
        bool is_leftmost = (i == 0);
        bool prev_is_wildcard =
            (i + 1 < n) && parts[i + 1].kind == ElementMatcher::Kind::Wildcard;

        switch (matcher.kind) {
            case ElementMatcher::Kind::Wildcard:
                if (is_first) {
                    throw ParseError(
                        "Bad target syntax, '*' can't be the matched "
                        "element");
                }
                if (is_leftmost) {
                    throw ParseError(
                        "Bad target syntax, '*' can't be the leftmost "
                        "target part");
                }
                if (prev_is_wildcard) {
                    throw ParseError(
                        "Bad target syntax, '*' can't be adjacent to "
                        "another '*'");
                }
                break;

            case ElementMatcher::Kind::Root:
                if (is_first) {
                    throw ParseError(
                        "Bad target syntax, ':root' can't be the matched "
                        "element");
                }
                if (!is_leftmost) {
                    throw ParseError(
                        "Bad target syntax, ':root' must be the leftmost "
                        "target part");
                }
                break;

            default:
                break;
        }

        if (matcher.visual_state_group.has_value()) {
            if (has_visual_state_group) {
                throw ParseError(
                    "Element type can't have more than one visual state "
                    "group");
            }
            has_visual_state_group = true;
        }
    }
}

}  // namespace

ElementMatcher ParseElementMatcher(std::wstring_view str) {
    ElementMatcher result;

    auto trimmed = detail::Trim(str);
    if (trimmed == L"*") {
        result.kind = ElementMatcher::Kind::Wildcard;
        return result;
    }
    if (trimmed == L":root") {
        result.kind = ElementMatcher::Kind::Root;
        return result;
    }

    auto i = trimmed.find_first_of(L"#@[");
    result.type = detail::Trim(trimmed.substr(0, i));
    if (result.type.empty()) {
        throw ParseError("Bad target syntax, empty type");
    }

    while (i != std::wstring_view::npos) {
        auto next = trimmed.find_first_of(L"#@[", i + 1);
        auto part = trimmed.substr(
            i + 1, next == std::wstring_view::npos ? next : next - (i + 1));

        switch (trimmed[i]) {
            case L'#': {
                if (!result.name.empty()) {
                    throw AmbiguousMatcherError(
                        "Bad target syntax, more than one name");
                }
                result.name = detail::Trim(part);
                if (result.name.empty()) {
                    throw ParseError("Bad target syntax, empty name");
                }
                break;
            }

            case L'@': {
                if (result.visual_state_group.has_value()) {
                    throw ParseError(
                        "Bad target syntax, more than one visual state group");
                }
                result.visual_state_group = std::wstring(detail::Trim(part));
                break;
            }

            case L'[': {
                auto rule = detail::Trim(part);
                if (rule.empty() || rule.back() != L']') {
                    throw ParseError("Bad target syntax, missing ']'");
                }
                rule = detail::Trim(rule.substr(0, rule.size() - 1));
                if (rule.empty()) {
                    throw ParseError("Bad target syntax, empty property");
                }

                if (rule.find_first_not_of(L"0123456789") ==
                    std::wstring_view::npos) {
                    result.one_based_index = ParseIndex(rule);
                    break;
                }

                auto eq = rule.find(L'=');
                if (eq == std::wstring_view::npos) {
                    throw ParseError(
                        "Bad target syntax, missing '=' in property");
                }

                auto key = detail::Trim(rule.substr(0, eq));
                auto value = detail::Trim(rule.substr(eq + 1));
                if (key.empty()) {
                    throw ParseError("Bad target syntax, empty property name");
                }

                result.property_filters.emplace_back(std::wstring(key),
                                                     std::wstring(value));
                break;
            }

            default:
                break;
        }

        i = next;
    }

    return result;
}

std::vector<ElementMatcher> ParseSelector(std::wstring_view str) {
    // Split on '>' only at bracket depth zero. This differs from upstream's
    // " > " (space-surrounded) split. We support hand-written themes without
    // spaces (e.g. "Grid>Rectangle") while protecting '>' inside property
    // filters (e.g. "Grid[Tag=A>B]"). Behavior on all 2396 shipped selectors
    // is identical to both strategies.
    //
    // The depth guard below is `<= 0`, not `== 0`: an unmatched ']' (e.g.
    // hand-edited "Grid]>Rectangle") drives depth negative, and `== 0` would
    // never see zero again, so the whole string collapses into one matcher
    // that can never match anything instead of splitting at the stray '>'.
    std::vector<ElementMatcher> parts;

    size_t pos = 0;
    while (pos <= str.size()) {
        int bracket_depth = 0;
        size_t sep = std::wstring_view::npos;

        for (size_t i = pos; i < str.size(); ++i) {
            if (str[i] == L'[') {
                ++bracket_depth;
            } else if (str[i] == L']') {
                --bracket_depth;
            } else if (str[i] == L'>' && bracket_depth <= 0) {
                sep = i;
                break;
            }
        }

        auto piece = str.substr(
            pos, sep == std::wstring_view::npos ? sep : sep - pos);
        parts.push_back(ParseElementMatcher(piece));

        if (sep == std::wstring_view::npos) {
            break;
        }
        pos = sep + 1;
    }

    ValidateChainStructure(parts);
    return parts;
}

std::vector<std::wstring_view> SplitTargetString(std::wstring_view target) {
    std::vector<std::wstring_view> result;

    size_t part_begin = 0;
    bool in_property = false;
    for (size_t i = 0; i < target.size(); ++i) {
        switch (target[i]) {
            case L'[':
                in_property = true;
                break;

            case L']':
                in_property = false;
                break;

            case L',':
                if (!in_property) {
                    result.push_back(target.substr(part_begin, i - part_begin));
                    part_begin = i + 1;
                }
                break;

            default:
                break;
        }
    }

    result.push_back(target.substr(part_begin));

    return result;
}

std::vector<std::vector<ElementMatcher>> ParseSelectorGroups(
    std::wstring_view str) {
    std::vector<std::vector<ElementMatcher>> groups;
    for (auto chain : SplitTargetString(str)) {
        try {
            groups.push_back(ParseSelector(chain));
        } catch (const AmbiguousMatcherError&) {
            // Drop just this chain; sibling chains from the same target
            // still get a chance to parse. An all-or-nothing catch here
            // would throw away good chains alongside the bad one for any
            // multi-chain target - see the header comment.
        }
    }
    return groups;
}

}  // namespace styler
