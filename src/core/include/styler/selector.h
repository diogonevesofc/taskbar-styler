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

// A narrower ParseError: a selector segment encodes the same decoration (a
// `#Name`) more than once, almost always because two whole matchers were
// concatenated without a separator between them - as in one shipped
// upstream authoring bug (LiquidGlass2's SnapLayoutControl/LayoutBorder
// target glues two segments with a plain space instead of '>'; see
// theme_loader.cpp). Code that wants to treat that one class of error as
// "this rule never matches" instead of failing the whole theme can catch
// this specifically; every other malformed-selector case (empty type, empty
// name, unmatched bracket, ...) is a plain ParseError and still fails
// closed. `catch (const ParseError&)` still catches this too.
class AmbiguousMatcherError : public ParseError {
public:
    using ParseError::ParseError;
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

// Splits a raw target string on the commas that separate independent
// selector chains, e.g. "ParentClass > Class#Name1, ParentClass > Class#Name2"
// is two chains. A comma inside a `[Property=Value]` filter does not split -
// mirrors upstream's SplitTargetString (vendor/upstream/...:18823) exactly,
// including its simple in-bracket flag (no nesting support, matching every
// shipped target).
std::vector<std::wstring_view> SplitTargetString(std::wstring_view target);

// Splits `str` into comma-separated chains via SplitTargetString, then
// parses each chain with ParseSelector. A rule's target matches an element
// if ANY of the returned chains match it - the comma is an OR, not an AND.
// 624 of the 2396 shipped rules (53 of 55 themes) use more than one chain.
std::vector<std::vector<ElementMatcher>> ParseSelectorGroups(
    std::wstring_view str);

}  // namespace styler
