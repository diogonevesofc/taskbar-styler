// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <filesystem>

#include <styler/blur.h>
#include <styler/matcher.h>
#include <styler/theme_loader.h>

using styler::BlurSpec;
using styler::ParseWindhawkBlur;

TEST_CASE("ParseWindhawkBlur ignores anything that is not a blur element") {
    CHECK_FALSE(ParseWindhawkBlur(L"Transparent").has_value());
    CHECK_FALSE(ParseWindhawkBlur(L"<SolidColorBrush Color=\"#FF0000\"/>")
                    .has_value());
    CHECK_FALSE(ParseWindhawkBlur(L"").has_value());
}

TEST_CASE("ParseWindhawkBlur reads the shipped shape") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>");
    REQUIRE(spec.has_value());
    CHECK(spec->blur_amount == doctest::Approx(18.0));
    CHECK(spec->tint.a == 0x25);
    CHECK(spec->tint.r == 0x32);
    CHECK(spec->tint.g == 0x32);
    CHECK(spec->tint.b == 0x32);
    CHECK_FALSE(spec->tint_opacity.has_value());
    CHECK(spec->tint_theme_resource.empty());
}

TEST_CASE("ParseWindhawkBlur folds TintOpacity into the tint alpha") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"10\" TintColor=\"#909090\" "
        L"TintOpacity=\"0.2\"/>");
    REQUIRE(spec.has_value());
    CHECK(spec->tint.a == 51);  // 0.2 * 255
    REQUIRE(spec->tint_opacity.has_value());
    CHECK(*spec->tint_opacity == 51);
}

TEST_CASE("ParseWindhawkBlur keeps a theme resource tint as a key") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"5\" "
        L"TintColor=\"{ThemeResource SystemChromeMediumColor}\" "
        L"TintOpacity=\"0.7\" />");
    REQUIRE(spec.has_value());
    CHECK(spec->tint_theme_resource == L"SystemChromeMediumColor");
    CHECK(spec->tint.a == 178);
}

TEST_CASE("ParseWindhawkBlur accepts the Blur synonym and short colors") {
    auto spec = ParseWindhawkBlur(L"<Blur BlurAmount=\"1\" TintColor=\"#8ABC\"/>");
    REQUIRE(spec.has_value());
    CHECK(spec->tint.a == 0x88);
    CHECK(spec->tint.r == 0xAA);
    CHECK(spec->tint.g == 0xBB);
    CHECK(spec->tint.b == 0xCC);
}

TEST_CASE("ParseWindhawkBlur reads noise, saturation and luminosity") {
    auto spec = ParseWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"30\" TintColor=\"#cc2a2e32\" "
        L"TintSaturation=\"1.8\" TintLuminosityOpacity=\"0.4\" "
        L"NoiseOpacity=\"0.03\" NoiseDensity=\"0.6\"/>");
    REQUIRE(spec.has_value());
    CHECK(*spec->tint_saturation == doctest::Approx(1.8));
    CHECK(*spec->tint_luminosity_opacity == doctest::Approx(0.4));
    CHECK(*spec->noise_opacity == doctest::Approx(0.03));
    CHECK(*spec->noise_density == doctest::Approx(0.6));
}

TEST_CASE("ParseWindhawkBlur fails closed on a malformed blur") {
    CHECK_THROWS_AS(ParseWindhawkBlur(L"<WindhawkBlur Blur=\"1\"/>"),
                    styler::ParseError);
    CHECK_THROWS_AS(ParseWindhawkBlur(L"<WindhawkBlur BlurAmount=\"18px\"/>"),
                    styler::ParseError);
    CHECK_THROWS_AS(ParseWindhawkBlur(L"<WindhawkBlur BlurAmount=\"1\">"),
                    styler::ParseError);
    CHECK_THROWS_AS(
        ParseWindhawkBlur(L"<WindhawkBlur TintColor=\"{ThemeResource }\"/>"),
        styler::ParseError);
}

// The corpus is the real specification: every blur the 55 shipped themes
// contain must parse, and every one must reach a PreparedStyle with a spec.
TEST_CASE("every blur in the shipped corpus parses") {
    namespace fs = std::filesystem;
    int specs = 0;
    int themes_with_blur = 0;
    for (const auto& entry : fs::directory_iterator(STYLER_THEMES_DIR)) {
        if (entry.path().extension() != ".json" ||
            entry.path().filename() == "credits.json") {
            continue;
        }
        styler::Theme theme = styler::LoadThemeFromFile(entry.path().wstring());
        styler::ResolvedTheme resolved = styler::PrepareTheme(theme);
        if (resolved.blur_specs > 0) {
            ++themes_with_blur;
        }
        specs += resolved.blur_specs;
        // Fail closed: a blur that only got the approximation means the
        // parser rejected markup the corpus actually ships.
        CHECK_MESSAGE(resolved.blur_approximations == 0,
                      "unparsed blur in ", entry.path().filename().string());
    }
    CHECK(themes_with_blur == 32);
    // Measured, not the brief's estimated 272 - see task-2-report.md for the
    // two concrete causes: (1) an alias constant whose own value is another
    // `$Name` (Command_Center's, FrostedAcrylic's and WindowGlass*'s
    // "Background", 5 distinct constants counted) resolves,
    // after ResolveConstants's nested-constant expansion, to a second
    // independent blur tag under a different name, and PrepareTheme counts
    // once per distinct constant name - correctly one MORE than counting
    // only the constants that spell out "WindhawkBlur" literally; (2) eight
    // theme files (OS26_Liquid_Glass_variant_* x6, One_UI_8_5_variant_Dock,
    // One_UI_8_5_variant_Taskbar) carry the byte-for-byte identical upstream
    // authoring typo `Fill:=<<WindhawkBlur .../>` (double '<'), which does
    // not match the tag prefix at all and is correctly left alone - same as
    // any other unrecognized XAML value - rather than counted as either a
    // spec or an approximation.
    CHECK(specs == 269);
}
