// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <string>

#include <styler/constants.h>

using styler::ApplyStyleConstants;
using styler::ResolveConstants;

TEST_CASE("substitutes a constant anywhere in the value") {
    auto c = ResolveConstants({{L"Bg", L"Red"}});
    CHECK(ApplyStyleConstants(L"Fill:=$Bg", c) == L"Fill:=Red");
    CHECK(ApplyStyleConstants(L"<Brush Color=\"$Bg\"/>", c) ==
          L"<Brush Color=\"Red\"/>");
}

TEST_CASE("an unmatched dollar passes through as a literal") {
    auto c = ResolveConstants({{L"Bg", L"Red"}});
    CHECK(ApplyStyleConstants(L"Text=$Nope and $", c) == L"Text=$Nope and $");
}

TEST_CASE("the longest matching name wins regardless of map order") {
    // Aeris declares themeColor before themeColorOpacity and references the
    // long one; upstream sorts by length so the long name wins.
    auto c = ResolveConstants(
        {{L"themeColor", L"#FF0000"}, {L"themeColorOpacity", L"0.5"}});
    CHECK(ApplyStyleConstants(L"$themeColorOpacity", c) == L"0.5");
    CHECK(ApplyStyleConstants(L"$themeColor", c) == L"#FF0000");
    CHECK(ApplyStyleConstants(L"$themeColorX", c) == L"#FF0000X");
}

TEST_CASE("a constant may reference another constant") {
    // WindowGlass: Background=$Glass, Glass=<WindhawkBlur .../>.
    auto c = ResolveConstants(
        {{L"Background", L"$Glass"}, {L"Glass", L"<AcrylicBrush/>"}});
    CHECK(ApplyStyleConstants(L"Fill:=$Background", c) ==
          L"Fill:=<AcrylicBrush/>");
}

TEST_CASE("a self-referencing constant does not loop forever") {
    auto c = ResolveConstants({{L"A", L"$A"}});
    CHECK(ApplyStyleConstants(L"$A", c) == L"$A");
}

TEST_CASE("resolved constants are sorted longest name first") {
    auto c = ResolveConstants({{L"a", L"1"}, {L"abc", L"3"}, {L"ab", L"2"}});
    REQUIRE(c.size() == 3);
    CHECK(c[0].first == L"abc");
    CHECK(c[1].first == L"ab");
    CHECK(c[2].first == L"a");
}
