// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/selector.h>

#include <charconv>
#include <string>

namespace styler {
namespace {

constexpr std::wstring_view kWhitespace = L" \t\r\n";

std::wstring_view Trim(std::wstring_view s) {
    auto first = s.find_first_not_of(kWhitespace);
    if (first == std::wstring_view::npos) {
        return {};
    }
    auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

int ParseIndex(std::wstring_view s) {
    int value = 0;
    for (wchar_t c : s) {
        value = value * 10 + (c - L'0');
    }
    return value;
}

}  // namespace

ElementMatcher ParseElementMatcher(std::wstring_view str) {
    ElementMatcher result;

    auto trimmed = Trim(str);
    if (trimmed == L"*") {
        result.kind = ElementMatcher::Kind::Wildcard;
        return result;
    }
    if (trimmed == L":root") {
        result.kind = ElementMatcher::Kind::Root;
        return result;
    }

    auto i = trimmed.find_first_of(L"#@[");
    result.type = Trim(trimmed.substr(0, i));
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
                    throw ParseError("Bad target syntax, more than one name");
                }
                result.name = Trim(part);
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
                result.visual_state_group = std::wstring(Trim(part));
                break;
            }

            case L'[': {
                auto rule = Trim(part);
                if (rule.empty() || rule.back() != L']') {
                    throw ParseError("Bad target syntax, missing ']'");
                }
                rule = Trim(rule.substr(0, rule.size() - 1));
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

                auto key = Trim(rule.substr(0, eq));
                auto value = Trim(rule.substr(eq + 1));
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
    std::vector<ElementMatcher> parts;

    size_t pos = 0;
    while (pos <= str.size()) {
        auto sep = str.find(L'>', pos);
        auto piece = str.substr(
            pos, sep == std::wstring_view::npos ? sep : sep - pos);
        parts.push_back(ParseElementMatcher(piece));

        if (sep == std::wstring_view::npos) {
            break;
        }
        pos = sep + 1;
    }

    return parts;
}

}  // namespace styler
