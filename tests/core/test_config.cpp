// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/config.h>
#include <styler/selector.h>  // ParseError

using styler::Config;
using styler::ParseConfigJson;
using styler::SerializeConfigJson;

TEST_CASE("parses theme and log level") {
    Config c = ParseConfigJson(R"({"theme":"TranslucentTaskbar","logLevel":"debug"})");
    CHECK(c.theme == L"TranslucentTaskbar");
    CHECK(c.log_level == L"debug");
}

TEST_CASE("missing fields default to empty") {
    Config c = ParseConfigJson("{}");
    CHECK(c.theme.empty());
    CHECK(c.log_level.empty());
}

TEST_CASE("malformed json is a ParseError") {
    CHECK_THROWS_AS(ParseConfigJson("{nope"), styler::ParseError);
    CHECK_THROWS_AS(ParseConfigJson(R"({"theme": 5})"), styler::ParseError);
}

TEST_CASE("serialize round-trips") {
    Config c;
    c.theme = L"Aeris";
    c.log_level = L"info";
    Config back = ParseConfigJson(SerializeConfigJson(c));
    CHECK(back.theme == L"Aeris");
    CHECK(back.log_level == L"info");
}
