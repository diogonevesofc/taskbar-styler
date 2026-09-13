// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <cli/loader.h>

using styler::cli::DescribeHresult;

TEST_CASE("names the HRESULTs a user will actually hit") {
    CHECK(DescribeHresult(S_OK).find(L"S_OK") != std::wstring::npos);

    auto not_found = DescribeHresult(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    CHECK(not_found.find(L"ERROR_NOT_FOUND") != std::wstring::npos);

    auto denied = DescribeHresult(E_ACCESSDENIED);
    CHECK(denied.find(L"E_ACCESSDENIED") != std::wstring::npos);
}

TEST_CASE("an unknown HRESULT still carries its numeric value") {
    auto odd = DescribeHresult(static_cast<HRESULT>(0x8007ABCD));
    CHECK(odd.find(L"0x8007ABCD") != std::wstring::npos);
}
