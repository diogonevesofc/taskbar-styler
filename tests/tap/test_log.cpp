// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/log.h>
#include <windows.h>
#include <string>
#include <vector>

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

TEST_CASE("diagnostic readers do not suppress native log records") {
    // Use the actual writer and the same sharing flags as the tray reader.
    // Initial writes also complete any pending rotation before taking a reader.
    styler::tap::LogLine(LogLevel::Info, L"shared-reader test setup");
    styler::tap::LogLine(LogLevel::Info, L"shared-reader test setup");
    const auto path = styler::tap::StylerDataDir() + L"\\log.txt";
    const HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(reader != INVALID_HANDLE_VALUE);
    struct Close { HANDLE value; ~Close() { CloseHandle(value); } } close{reader};
    LARGE_INTEGER before{};
    REQUIRE(GetFileSizeEx(reader, &before));
    const std::string marker = "shared-reader-record-" +
        std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    styler::tap::LogLine(LogLevel::Info, std::wstring(marker.begin(), marker.end()));
    LARGE_INTEGER after{};
    REQUIRE(GetFileSizeEx(reader, &after));
    REQUIRE(after.QuadPart > before.QuadPart);
    REQUIRE(SetFilePointerEx(reader, before, nullptr, FILE_BEGIN));
    std::vector<char> appended(static_cast<std::size_t>(after.QuadPart - before.QuadPart));
    DWORD read = 0;
    REQUIRE(ReadFile(reader, appended.data(), static_cast<DWORD>(appended.size()), &read, nullptr));
    CHECK(std::string(appended.data(), read).find(marker) != std::string::npos);
}
