// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/selector.h>

using styler::ElementMatcher;
using styler::ParseElementMatcher;
using styler::ParseError;
using styler::ParseSelector;

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
