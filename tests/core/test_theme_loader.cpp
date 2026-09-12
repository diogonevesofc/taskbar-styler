// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <filesystem>

#include <styler/theme_loader.h>
#include <styler/utf.h>

using styler::LoadThemeFromFile;
using styler::LoadThemeFromJson;
using styler::ParseError;
using styler::ValueRule;

namespace {

constexpr const char* kMinimal = R"({
  "id": "T", "name": "T", "rules": []
})";

}  // namespace

TEST_CASE("round-trips utf-8 with non-ascii") {
    auto wide = styler::Utf8ToWide("Ninguém — ação");
    CHECK(styler::WideToUtf8(wide) == "Ninguém — ação");
}

TEST_CASE("loads a minimal theme") {
    auto theme = LoadThemeFromJson(kMinimal);
    CHECK(theme.id == L"T");
    CHECK(theme.rules.empty());
    CHECK_FALSE(theme.os_feature_variant.has_value());
}

TEST_CASE("loads constants, selectors and styles") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "constants": { "Bg": "<WindhawkBlur BlurAmount=\"18\"/>" },
      "rules": [
        { "target": "Grid#RootGrid > Rectangle", "styles": ["Fill:=$Bg"] }
      ]
    })");

    REQUIRE(theme.constants.count(L"Bg") == 1);
    REQUIRE(theme.rules.size() == 1);
    REQUIRE(theme.rules[0].selector.size() == 1);
    CHECK(theme.rules[0].selector[0].size() == 2);
    CHECK(theme.rules[0].selector[0][1].type == L"Rectangle");
    REQUIRE(theme.rules[0].styles.size() == 1);
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).is_xaml_value);
}

TEST_CASE("loads a comma-separated target as multiple selector chains") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "rules": [
        { "target": "Grid#A > Rectangle, Grid#B > Border", "styles": ["Fill=Red"] }
      ]
    })");

    REQUIRE(theme.rules.size() == 1);
    REQUIRE(theme.rules[0].selector.size() == 2);
    REQUIRE(theme.rules[0].selector[0].size() == 2);
    CHECK(theme.rules[0].selector[0][0].name == L"A");
    CHECK(theme.rules[0].selector[0][1].type == L"Rectangle");
    REQUIRE(theme.rules[0].selector[1].size() == 2);
    CHECK(theme.rules[0].selector[1][0].name == L"B");
    CHECK(theme.rules[0].selector[1][1].type == L"Border");
}

TEST_CASE("loads the os feature variant") {
    auto theme = LoadThemeFromJson(R"({
      "id": "Squircle", "name": "Squircle", "rules": [],
      "osFeatureVariant": { "featureId": 48660958, "themeId": "Squircle_WeatherOnTheRight" }
    })");

    REQUIRE(theme.os_feature_variant.has_value());
    CHECK(theme.os_feature_variant->feature_id == 48660958u);
    CHECK(theme.os_feature_variant->theme_id == L"Squircle_WeatherOnTheRight");
}

// Upstream's own ParseRule (vendor/upstream/...:18727) throws on a style
// with no '=', and AddElementCustomizationRules (vendor/upstream/...:18956)
// catches that per target and discards its whole customization - the target
// is already a no-op at runtime. A handful of shipped rules (LiquidGlass2's
// #DisplayName and #Iconlmage targets) carry exactly this: a literal empty
// style string. The loader skips it rather than failing the whole theme,
// while every other malformed style (see below) still fails closed.
TEST_CASE("skips a literal empty style string instead of failing the theme") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "rules": [ { "target": "Grid", "styles": [""] } ]
    })");
    REQUIRE(theme.rules.size() == 1);
    CHECK(theme.rules[0].styles.empty());
}

// Mirrors upstream's own AddElementCustomizationRules (vendor/upstream/...:
// 18956), which catches a bad target's selector error and discards just
// that target's customization rather than the whole theme. LiquidGlass2
// ships exactly this: two segments glued by a space instead of '>'.
TEST_CASE("keeps a rule with an unparseable selector instead of failing the theme") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "rules": [
        { "target": "Grid#A Grid#B", "styles": ["Fill=Red"] }
      ]
    })");
    REQUIRE(theme.rules.size() == 1);
    CHECK(theme.rules[0].target == L"Grid#A Grid#B");
    CHECK(theme.rules[0].selector.empty());
    REQUIRE(theme.rules[0].styles.size() == 1);
}

TEST_CASE("fails closed on malformed input") {
    CHECK_THROWS_AS(LoadThemeFromJson("{ not json"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "name": "T", "rules": [] })"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "id": "", "name": "T", "rules": [] })"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "id": "T", "name": "T",
      "rules": [ { "target": "#bad", "styles": [] } ] })"), ParseError);
    CHECK_THROWS_AS(LoadThemeFromJson(R"({ "id": "T", "name": "T",
      "rules": [ { "target": "Grid", "styles": ["NoEquals"] } ] })"), ParseError);
}

// The loader does NOT validate constant resolution. Upstream
// (ApplyStyleConstants, vendor/upstream/...:18536) substitutes `$Name` by
// prefix anywhere in a value and lets an unmatched `$` pass through as a
// literal. In the real data, 85 references are embedded mid-value and 10
// don't resolve against any constant — throwing here would reject the
// Luminosity_variant_Dock, Luminosity_variant_Compact and Fluid themes.
// Resolution belongs to the TAP, in Plano 2.
TEST_CASE("accepts an unresolved constant reference, like upstream does") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "rules": [ { "target": "Grid", "styles": ["Fill:=$Missing"] } ]
    })");
    REQUIRE(theme.rules.size() == 1);
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).value == L"$Missing");
}

TEST_CASE("accepts a constant embedded mid-value") {
    auto theme = LoadThemeFromJson(R"({
      "id": "T", "name": "T",
      "constants": { "Gap": "8" },
      "rules": [ { "target": "Grid", "styles": ["Margin=0,0,$Gap,0"] } ]
    })");
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).value == L"0,0,$Gap,0");
}

TEST_CASE("loads a theme from a file on disk") {
    auto path = std::filesystem::path(STYLER_TEST_DATA_DIR) / "valid_theme.json";
    auto theme = LoadThemeFromFile(path);

    CHECK(theme.id == L"TestTheme");
    CHECK(theme.name == L"Test Theme");
    REQUIRE(theme.constants.count(L"Bg") == 1);
    REQUIRE(theme.rules.size() == 1);
    REQUIRE(theme.rules[0].selector.size() == 1);
    CHECK(theme.rules[0].selector[0].size() == 2);
    REQUIRE(theme.rules[0].styles.size() == 2);
    CHECK(std::get<ValueRule>(theme.rules[0].styles[0]).value == L"$Bg");
}

TEST_CASE("throws when the theme file does not exist") {
    auto path = std::filesystem::path(STYLER_TEST_DATA_DIR) / "does_not_exist.json";
    CHECK_THROWS_AS(LoadThemeFromFile(path), ParseError);
}
