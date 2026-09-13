// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/release_policy.h>

using styler::tap::HandlesToRelease;

TEST_CASE("deduplicates and sorts the pending handles") {
    auto result = HandlesToRelease({30, 10, 20, 10, 30}, [](auto) { return false; });
    CHECK(result.to_release == std::vector<unsigned long long>{10, 20, 30});
    CHECK(result.unique_count == 3);
}

TEST_CASE("keeps handles that still have state held") {
    auto result = HandlesToRelease({1, 2, 3}, [](auto h) { return h == 2; });
    CHECK(result.to_release == std::vector<unsigned long long>{1, 3});
    CHECK(result.unique_count == 3);
    // The "held" gauge release_queue.cpp reports: unique minus released.
    CHECK(result.unique_count - result.to_release.size() == 1);
}

TEST_CASE("drops the zero handle a root parent reports, and does not count it") {
    auto out = HandlesToRelease({0, 5, 0}, [](auto) { return false; });
    CHECK(out.to_release == std::vector<unsigned long long>{5});
    // Zero is not a real handle: it must not inflate "held" either.
    CHECK(out.unique_count == 1);
}

TEST_CASE("duplicate parent handles do not look held") {
    // Ten children of the same parent queue the parent ten times (once per
    // child, per release_queue.cpp's QueueRelease(relation.Parent)); none of
    // that repetition is "held" - it is exactly what dedup exists to
    // collapse. This is the case review flagged: measuring against the raw
    // queue length before dedup would report 9 "held" here for zero real
    // reason.
    std::vector<unsigned long long> pending{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,          // ten distinct children
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99  // their shared parent, x10
    };
    auto result = HandlesToRelease(pending, [](auto) { return false; });
    CHECK(result.unique_count == 11);  // 10 children + 1 parent
    CHECK(result.to_release.size() == 11);
    CHECK(result.unique_count - result.to_release.size() == 0);
}
