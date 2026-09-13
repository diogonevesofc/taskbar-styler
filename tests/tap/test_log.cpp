// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/log.h>

using styler::tap::GetLogLevel;
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

    SetLogLevel(LogLevel::Info);  // restore the default
}

TEST_CASE("the default level is Info, not Error") {
    // SetSite / "loaded into ..." fire once per load, not once per element -
    // there is nothing on that path worth hiding behind an opt-in level. The
    // per-element hot path (OnVisualTreeChange) logs at Debug, which stays
    // gated regardless of this default.
    SetLogLevel(LogLevel::Info);
    CHECK(GetLogLevel() == LogLevel::Info);
    CHECK(LogEnabled(LogLevel::Info));
    CHECK_FALSE(LogEnabled(LogLevel::Debug));
}

TEST_CASE("SetLogLevel survives a subsequent GetLogLevel call") {
    // Regression test: GetLogLevel() must be a plain read of whatever
    // SetLogLevel() last stored. It must never silently overwrite that value
    // (a prior revision did this via a lazy-initialized config-file read
    // inside GetLogLevel() itself, which clobbered SetLogLevel() on the
    // first call any test made).
    SetLogLevel(LogLevel::Debug);
    CHECK(GetLogLevel() == LogLevel::Debug);
    CHECK(GetLogLevel() == LogLevel::Debug);  // second read, still unchanged

    SetLogLevel(LogLevel::Info);  // restore the default
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
