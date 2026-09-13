// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/blur_rewrite.h>

using styler::RewriteWindhawkBlur;

TEST_CASE("keeps the attributes AcrylicBrush understands, drops the rest") {
    bool rewritten = false;
    auto out = RewriteWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\" "
        L"TintOpacity=\"0.7\" TintLuminosityOpacity=\"0.3\" "
        L"TintSaturation=\"1.2\" NoiseOpacity=\"0.1\" NoiseDensity=\"0.8\" "
        L"FallbackColor=\"#FF202020\"/>",
        &rewritten);
    CHECK(rewritten);
    CHECK(out ==
          L"<AcrylicBrush TintColor=\"#25323232\" TintOpacity=\"0.7\" "
          L"TintLuminosityOpacity=\"0.3\" FallbackColor=\"#FF202020\"/>");
}

TEST_CASE("theme resource references survive") {
    bool rewritten = false;
    auto out = RewriteWindhawkBlur(
        L"<WindhawkBlur BlurAmount=\"5\" "
        L"TintColor=\"{ThemeResource SystemChromeMediumColor}\" />",
        &rewritten);
    CHECK(rewritten);
    CHECK(out ==
          L"<AcrylicBrush TintColor=\"{ThemeResource SystemChromeMediumColor}\"/>");
}

TEST_CASE("Blur is accepted as a synonym") {
    bool rewritten = false;
    CHECK(RewriteWindhawkBlur(L"<Blur BlurAmount=\"3\"/>", &rewritten) ==
          L"<AcrylicBrush/>");
    CHECK(rewritten);
}

TEST_CASE("anything else passes through untouched") {
    bool rewritten = true;
    CHECK(RewriteWindhawkBlur(L"<SolidColorBrush Color=\"Red\"/>", &rewritten) ==
          L"<SolidColorBrush Color=\"Red\"/>");
    CHECK_FALSE(rewritten);
    CHECK(RewriteWindhawkBlur(L"Transparent", &rewritten) == L"Transparent");
    CHECK_FALSE(rewritten);
}

TEST_CASE("leading whitespace is tolerated") {
    bool rewritten = false;
    CHECK(RewriteWindhawkBlur(
              L"  <WindhawkBlur BlurAmount=\"8\" TintColor=\"#761E1E1E\"/>",
              &rewritten) == L"<AcrylicBrush TintColor=\"#761E1E1E\"/>");
    CHECK(rewritten);
}
