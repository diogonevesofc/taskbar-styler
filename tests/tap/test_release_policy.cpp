// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/release_policy.h>

using styler::tap::HandlesToRelease;

TEST_CASE("deduplicates and sorts the pending handles") {
    auto out = HandlesToRelease({30, 10, 20, 10, 30}, [](auto) { return false; });
    CHECK(out == std::vector<unsigned long long>{10, 20, 30});
}

TEST_CASE("keeps handles that still have state held") {
    auto out = HandlesToRelease({1, 2, 3}, [](auto h) { return h == 2; });
    CHECK(out == std::vector<unsigned long long>{1, 3});
}

TEST_CASE("drops the zero handle a root parent reports") {
    auto out = HandlesToRelease({0, 5, 0}, [](auto) { return false; });
    CHECK(out == std::vector<unsigned long long>{5});
}
