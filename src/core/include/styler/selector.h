// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace styler {

// Thrown for any malformed theme text: selectors, style rules, JSON shape.
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One segment of a selector chain.
struct ElementMatcher {
    enum class Kind {
        Element,   // A normal `Type#Name` matcher.
        Wildcard,  // `*`: matches zero or more intermediate ancestors.
        Root,      // `:root`: asserts the next element has no parent.
    };

    Kind kind = Kind::Element;
    std::wstring type;
    std::wstring name;
    std::optional<std::wstring> visual_state_group;
    int one_based_index = 0;  // 0 means unspecified.
    std::vector<std::pair<std::wstring, std::wstring>> property_filters;
};

ElementMatcher ParseElementMatcher(std::wstring_view str);

// Splits on '>' and parses each segment, outermost ancestor first.
std::vector<ElementMatcher> ParseSelector(std::wstring_view str);

}  // namespace styler
