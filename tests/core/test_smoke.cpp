// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <styler/version.h>

TEST_CASE("core reports a version") {
    CHECK(styler::CoreVersion() == std::wstring_view(L"0.1.0"));
}
