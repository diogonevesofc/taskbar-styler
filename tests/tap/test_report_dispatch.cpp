// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include <tap/owned_release_queue.h>
#include <tap/report_dispatch.h>

using namespace styler::tap;

TEST_CASE("reports before styling initialization retain ownership until a later drain") {
    OwnedReleaseQueue<int> pending;
    auto observed_owner = std::make_shared<int>(1);
    auto current_owner = observed_owner;
    std::weak_ptr<int> original_owner = observed_owner;
    bool initialized = false;
    int style_calls = 0;
    int release_calls = 0;

    DispatchObservedReport(initialized,
        [&] { ++style_calls; },
        [&] {
            pending.Add(1, 42, observed_owner);
            pending.Add(1, 7, observed_owner);
            pending.Add(1, 7, observed_owner);
        });

    CHECK_FALSE(initialized);
    CHECK(style_calls == 0);
    CHECK(release_calls == 0);
    CHECK(pending.size() == 2);

    // Replacing the current session cannot change who owns the queued report.
    current_owner = std::make_shared<int>(2);
    observed_owner.reset();
    CHECK_FALSE(original_owner.expired());
    CHECK(pending.Drain([](const auto&, auto) { return false; },
        [&](const auto& owner, auto handle) {
            CHECK((owner == original_owner.lock()));
            CHECK((owner != current_owner));
            CHECK((handle == 7 || handle == 42));
            ++release_calls;
            return true;
        }) == 2);
    CHECK(release_calls == 2);
    CHECK(pending.empty());
    CHECK(original_owner.expired());
}

TEST_CASE("initialized reports perform style work before deferring release") {
    std::vector<char> order;
    DispatchObservedReport(true,
        [&] { order.push_back('S'); },
        [&] { order.push_back('Q'); });
    CHECK((order == std::vector<char>{'S', 'Q'}));
}

TEST_CASE("style failure cannot bypass deferred report ownership") {
    OwnedReleaseQueue<int> pending;
    auto owner = std::make_shared<int>(1);
    CHECK_THROWS_AS(DispatchObservedReport(true,
        [] { throw std::runtime_error("style failed"); },
        [&] { pending.Add(1, 42, owner); }), std::runtime_error);
    CHECK(pending.size() == 1);
    CHECK(pending.Drain([](const auto&, auto) { return false; },
                       [](const auto&, auto) { return true; }) == 1);
}

TEST_CASE("enqueue failure remains visible to the callback error boundary") {
    for (const bool initialized : {false, true}) {
        int style_calls = 0;
        CHECK_THROWS_AS(DispatchObservedReport(initialized,
            [&] { ++style_calls; },
            [] { throw std::runtime_error("enqueue failed"); }), std::runtime_error);
        CHECK(style_calls == (initialized ? 1 : 0));
    }
}
