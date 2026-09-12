// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/selector.h>

using styler::AmbiguousMatcherError;
using styler::ElementMatcher;
using styler::ParseElementMatcher;
using styler::ParseError;
using styler::ParseSelector;
using styler::ParseSelectorGroups;
using styler::SplitTargetString;

TEST_CASE("parses a bare type") {
    auto m = ParseElementMatcher(L"Grid");
    CHECK(m.kind == ElementMatcher::Kind::Element);
    CHECK(m.type == L"Grid");
    CHECK(m.name.empty());
    CHECK(m.one_based_index == 0);
    CHECK_FALSE(m.visual_state_group.has_value());
}

TEST_CASE("parses a dotted type with a name") {
    auto m = ParseElementMatcher(L"Taskbar.TaskbarFrame#RootGrid");
    CHECK(m.type == L"Taskbar.TaskbarFrame");
    CHECK(m.name == L"RootGrid");
}

TEST_CASE("parses the wildcard") {
    auto m = ParseElementMatcher(L"*");
    CHECK(m.kind == ElementMatcher::Kind::Wildcard);
}

TEST_CASE("parses the root marker") {
    auto m = ParseElementMatcher(L":root");
    CHECK(m.kind == ElementMatcher::Kind::Root);
}

TEST_CASE("parses a one-based index") {
    auto m = ParseElementMatcher(L"Grid[2]");
    CHECK(m.type == L"Grid");
    CHECK(m.one_based_index == 2);
}

TEST_CASE("parses a visual state group") {
    auto m = ParseElementMatcher(L"Button@CommonStates");
    CHECK(m.type == L"Button");
    CHECK(m.visual_state_group == std::wstring(L"CommonStates"));
}

TEST_CASE("parses a property filter") {
    auto m = ParseElementMatcher(L"Grid[Tag=Chrome]");
    REQUIRE(m.property_filters.size() == 1);
    CHECK(m.property_filters[0].first == L"Tag");
    CHECK(m.property_filters[0].second == L"Chrome");
}

TEST_CASE("parses all decorations at once") {
    auto m = ParseElementMatcher(L"Grid#Root@CommonStates[3][Tag=X]");
    CHECK(m.type == L"Grid");
    CHECK(m.name == L"Root");
    CHECK(m.visual_state_group == std::wstring(L"CommonStates"));
    CHECK(m.one_based_index == 3);
    REQUIRE(m.property_filters.size() == 1);
}

TEST_CASE("trims surrounding whitespace") {
    auto m = ParseElementMatcher(L"  Grid#Root  ");
    CHECK(m.type == L"Grid");
    CHECK(m.name == L"Root");
}

TEST_CASE("splits a selector on '>'") {
    auto parts = ParseSelector(L"Taskbar.TaskbarFrame > Grid#RootGrid > Rectangle");
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].type == L"Taskbar.TaskbarFrame");
    CHECK(parts[1].name == L"RootGrid");
    CHECK(parts[2].type == L"Rectangle");
}

TEST_CASE("rejects malformed input") {
    CHECK_THROWS_AS(ParseElementMatcher(L"#OnlyName"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid#A#B"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid#"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid@A@B"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[2"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[]"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[Tag]"), ParseError);
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[=X]"), ParseError);
}

// AmbiguousMatcherError is a narrower ParseError specifically for "more than
// one #Name" - the one selector error ParseSelectorGroups tolerates
// (per chain). Asserted directly, not just via its ParseError base, so a
// future change that widens or narrows which errors get this subtype is
// caught here rather than only downstream in theme_loader tests.
TEST_CASE("more than one name throws the narrower AmbiguousMatcherError") {
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid#A#B"), AmbiguousMatcherError);
}

TEST_CASE("splits without spaces around the separator") {
    auto parts = ParseSelector(L"Grid#RootGrid>Rectangle");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].name == L"RootGrid");
    CHECK(parts[1].type == L"Rectangle");
}

TEST_CASE("does not split on '>' inside a property filter") {
    auto parts = ParseSelector(L"Grid[Tag=A>B] > Rectangle");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].type == L"Grid");
    REQUIRE(parts[0].property_filters.size() == 1);
    CHECK(parts[0].property_filters[0].second == L"A>B");
    CHECK(parts[1].type == L"Rectangle");
}

TEST_CASE("trims vertical tab, matching upstream") {
    auto m = ParseElementMatcher(L"\vGrid#Root\v");
    CHECK(m.type == L"Grid");
    CHECK(m.name == L"Root");
}

TEST_CASE("splits a target string on top-level commas") {
    auto parts = SplitTargetString(
        L"ParentClass > Class#Name1, ParentClass > Class#Name2");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0] == L"ParentClass > Class#Name1");
    CHECK(parts[1] == L" ParentClass > Class#Name2");
}

TEST_CASE("does not split a target string on a comma inside a property filter") {
    auto parts = SplitTargetString(L"Grid[Tag=A,B] > Rectangle, Grid#Other");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0] == L"Grid[Tag=A,B] > Rectangle");
    CHECK(parts[1] == L" Grid#Other");
}

TEST_CASE("splits a target string with no comma into a single part") {
    auto parts = SplitTargetString(L"Grid > Rectangle");
    REQUIRE(parts.size() == 1);
    CHECK(parts[0] == L"Grid > Rectangle");
}

TEST_CASE("splits a target string with a trailing comma into an empty final part") {
    auto parts = SplitTargetString(L"Grid#A,");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0] == L"Grid#A");
    CHECK(parts[1].empty());
}

TEST_CASE("splits a target string with a doubled comma into an empty middle part") {
    auto parts = SplitTargetString(L"Grid#A,,Grid#B");
    REQUIRE(parts.size() == 3);
    CHECK(parts[0] == L"Grid#A");
    CHECK(parts[1].empty());
    CHECK(parts[2] == L"Grid#B");
}

TEST_CASE("parses each comma-separated part of a target into its own chain") {
    auto groups = ParseSelectorGroups(
        L"Taskbar.SearchBoxButton#A > Border#Bg, Taskbar.SearchBoxButton#A > Border#Bg2");
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].size() == 2);
    CHECK(groups[0][1].name == L"Bg");
    REQUIRE(groups[1].size() == 2);
    CHECK(groups[1][1].name == L"Bg2");
}

TEST_CASE("parses a single-chain target into one group") {
    auto groups = ParseSelectorGroups(L"Grid#RootGrid > Rectangle");
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].size() == 2);
    CHECK(groups[0][1].type == L"Rectangle");
}

// Mirrors upstream's per-target-part tolerance (vendor/upstream/...:18952):
// one bad chain in a multi-chain target must not take a good sibling chain
// down with it. An all-or-nothing catch would return an empty result here;
// this must return the one good chain instead.
TEST_CASE("drops only the ambiguous chain from a multi-chain target, keeps the rest") {
    auto groups = ParseSelectorGroups(L"Grid#A#B, Grid#Good > Rectangle");
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].size() == 2);
    CHECK(groups[0][0].name == L"Good");
    CHECK(groups[0][1].type == L"Rectangle");
}

TEST_CASE("returns an empty result when every chain of a target is ambiguous") {
    auto groups = ParseSelectorGroups(L"Grid#A#B");
    CHECK(groups.empty());
}

// F2: ParseIndex must guard against signed overflow instead of invoking UB.
// Upstream's std::stoi throws std::out_of_range past INT_MAX; a hand-edited
// theme is free to write far more digits than any real visual tree index.
TEST_CASE("an out-of-range numeric index throws instead of overflowing") {
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[99999999999]"), ParseError);
    // One past INT_MAX (2147483647): still 10 digits, must still throw.
    CHECK_THROWS_AS(ParseElementMatcher(L"Grid[2147483648]"), ParseError);
    // INT_MAX itself is the boundary and must still parse.
    auto m = ParseElementMatcher(L"Grid[2147483647]");
    CHECK(m.one_based_index == 2147483647);
}

// F2: the bracket-depth guard must be `<= 0`, not `== 0`. An unmatched ']'
// drives depth negative; `== 0` never sees zero again and the whole string
// collapses into one never-matching matcher instead of splitting at '>'.
TEST_CASE("an unmatched ']' does not swallow the '>' separator that follows") {
    auto parts = ParseSelector(L"Grid]>Rectangle");
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].type == L"Grid]");
    CHECK(parts[1].type == L"Rectangle");
}

// F3: the 6 upstream chain validations from AddElementCustomizationRulesFor-
// SingleTarget (vendor/upstream/...:18859-18917), one TEST_CASE per rule.
// Measured against the real corpus: 0 of 3123 shipped chains violate any of
// these, so none of this changes behavior on a single shipped theme.

TEST_CASE("'*' cannot be the matched (last) element of a chain") {
    CHECK_THROWS_AS(ParseSelector(L"Grid > *"), ParseError);
}

TEST_CASE("'*' cannot be the leftmost part of a chain") {
    CHECK_THROWS_AS(ParseSelector(L"* > Grid > Rectangle"), ParseError);
}

TEST_CASE("'*' cannot be adjacent to another '*'") {
    CHECK_THROWS_AS(ParseSelector(L"Grid > * > * > Rectangle"), ParseError);
}

TEST_CASE(":root cannot be the matched (last) element of a chain") {
    CHECK_THROWS_AS(ParseSelector(L"Grid > :root"), ParseError);
}

TEST_CASE(":root must be the leftmost part of a chain") {
    CHECK_THROWS_AS(ParseSelector(L"Grid > :root > Rectangle"), ParseError);
    // Leftmost is fine.
    auto parts = ParseSelector(L":root > Grid > Rectangle");
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].kind == ElementMatcher::Kind::Root);
}

TEST_CASE("at most one visual-state-group is allowed per chain") {
    CHECK_THROWS_AS(
        ParseSelector(L"Button@CommonStates > Grid@OtherStates"), ParseError);
    // One visual state group total is fine, even split across the chain.
    auto parts = ParseSelector(L"Grid > Button@CommonStates");
    REQUIRE(parts.size() == 2);
    CHECK(parts[1].visual_state_group == std::wstring(L"CommonStates"));
}
