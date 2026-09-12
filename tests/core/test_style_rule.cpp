// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/style_rule.h>

using styler::CaptureRule;
using styler::ParseError;
using styler::ParseStyleRule;
using styler::ValueRule;

TEST_CASE("parses a plain value rule") {
    auto rule = ParseStyleRule(L"Visibility=Collapsed");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Visibility");
    CHECK(v.value == L"Collapsed");
    CHECK_FALSE(v.is_xaml_value);
    CHECK(v.visual_state.empty());
}

TEST_CASE("parses a xaml value rule") {
    auto rule = ParseStyleRule(L"Fill:=$CommonBgBrush");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Fill");
    CHECK(v.value == L"$CommonBgBrush");
    CHECK(v.is_xaml_value);
}

TEST_CASE("parses a visual state on a value rule") {
    auto rule = ParseStyleRule(L"Background@PointerOver=Red");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Background");
    CHECK(v.visual_state == L"PointerOver");
    CHECK(v.value == L"Red");
}

TEST_CASE("parses a visual state combined with a xaml value") {
    auto rule = ParseStyleRule(L"Background@PointerOver:=<SolidColorBrush Color=\"Red\"/>");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Background");
    CHECK(v.visual_state == L"PointerOver");
    CHECK(v.is_xaml_value);
    CHECK(v.value == L"<SolidColorBrush Color=\"Red\"/>");
}

TEST_CASE("keeps '=' inside the value") {
    auto rule = ParseStyleRule(L"Background:=<SolidColorBrush Color=\"Red\"/>");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.value == L"<SolidColorBrush Color=\"Red\"/>");
}

TEST_CASE("detects a dynamic value") {
    auto rule = ParseStyleRule(L"Width={{SomeExpr}}");
    CHECK(std::get<ValueRule>(rule).IsDynamic());

    auto plain = ParseStyleRule(L"Width=100");
    CHECK_FALSE(std::get<ValueRule>(plain).IsDynamic());
}

TEST_CASE("parses a capture rule") {
    auto rule = ParseStyleRule(L"Background=>SavedBg");
    auto& c = std::get<CaptureRule>(rule);
    CHECK(c.property_name == L"Background");
    CHECK(c.var_name == L"SavedBg");
}

TEST_CASE("trims whitespace around names and values") {
    auto rule = ParseStyleRule(L"  Visibility  =  Collapsed  ");
    auto& v = std::get<ValueRule>(rule);
    CHECK(v.property_name == L"Visibility");
    CHECK(v.value == L"Collapsed");
}

TEST_CASE("rejects malformed rules") {
    CHECK_THROWS_AS(ParseStyleRule(L"NoEqualsSign"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"=Value"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop:=>Var"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop@State=>Var"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop=>"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"=>Var"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop=>1Bad"), ParseError);
    CHECK_THROWS_AS(ParseStyleRule(L"Prop=>has-dash"), ParseError);
}
