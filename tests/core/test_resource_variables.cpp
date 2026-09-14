// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/resource_variables.h>

using namespace styler;

TEST_CASE("plain key is an untyped string override") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables({{L"TaskbarHeight", L"40"}}, &diag);
    REQUIRE(v.size() == 1);
    CHECK(v[0].key == L"TaskbarHeight");
    CHECK(v[0].value == L"40");
    CHECK(v[0].theme == ResourceTheme::None);
    CHECK(v[0].type == ResourceValueType::String);
}

TEST_CASE("@Dark and @Light select the theme dictionary") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables(
        {{L"AdaptiveFill@Light", L"#FFFFFF"}, {L"AdaptiveFill@Dark", L"#000000"}}, &diag);
    REQUIRE(v.size() == 2);
    CHECK(v[0].key == L"AdaptiveFill");
    CHECK(v[0].theme == ResourceTheme::Dark);  // map order: "@Dark" < "@Light"
    CHECK(v[1].theme == ResourceTheme::Light);
}

TEST_CASE("a trailing colon marks a XAML value") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables({{L"Brush:", L"<SolidColorBrush Color=\"Red\"/>"}}, &diag);
    REQUIRE(v.size() == 1);
    CHECK(v[0].key == L"Brush");
    CHECK(v[0].type == ResourceValueType::Xaml);
}

TEST_CASE("a ThemeResource value becomes a reference to that key") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables(
        {{L"Accent1@Dark", L"{ThemeResource SystemAccentColorLight3}"}}, &diag);
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == ResourceValueType::ThemeResourceReference);
    CHECK(v[0].value == L"SystemAccentColorLight3");
}

TEST_CASE("an unknown theme suffix is dropped with a diagnostic") {
    std::vector<std::wstring> diag;
    auto v = ParseResourceVariables({{L"X@Blue", L"1"}}, &diag);
    CHECK(v.empty());
    CHECK(diag.size() == 1);
}
