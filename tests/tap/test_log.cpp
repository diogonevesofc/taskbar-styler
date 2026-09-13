// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/log.h>

using styler::tap::LogEnabled;
using styler::tap::LogLevel;
using styler::tap::SetLogLevel;

TEST_CASE("log level gates as a threshold") {
    SetLogLevel(LogLevel::Error);
    CHECK(LogEnabled(LogLevel::Error));
    CHECK_FALSE(LogEnabled(LogLevel::Info));
    CHECK_FALSE(LogEnabled(LogLevel::Debug));

    SetLogLevel(LogLevel::Info);
    CHECK(LogEnabled(LogLevel::Error));
    CHECK(LogEnabled(LogLevel::Info));
    CHECK_FALSE(LogEnabled(LogLevel::Debug));

    SetLogLevel(LogLevel::Debug);
    CHECK(LogEnabled(LogLevel::Debug));

    SetLogLevel(LogLevel::Error);  // restore the default
}

TEST_CASE("the macro does not evaluate its arguments when gated") {
    SetLogLevel(LogLevel::Error);

    int calls = 0;
    auto expensive = [&calls]() -> const wchar_t* {
        calls++;
        return L"x";
    };

    STYLER_LOG(LogLevel::Debug, L"%s", expensive());
    CHECK(calls == 0);

    STYLER_LOG(LogLevel::Error, L"%s", expensive());
    CHECK(calls == 1);
}
