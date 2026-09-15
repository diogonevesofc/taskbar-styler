// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>
#include <latch>
#include <thread>
#include <stdexcept>
#include <tap/command_mailbox.h>
#include <tap/owned_release_queue.h>
#include <tap/subscription_state.h>

using namespace styler::tap;

TEST_CASE("second stop cannot forget an advise still in flight") {
    SubscriptionState state;
    std::latch entered(1), finish_advise(1), began_unadvise(1), finish_unadvise(1);
    bool worker_owns_unadvise = false;
    std::thread worker([&] {
        entered.count_down();
        finish_advise.wait();
        worker_owns_unadvise = state.AdviseFinished(true);
        began_unadvise.count_down();
        finish_unadvise.wait();
        state.UnadviseFinished(true);
    });
    entered.wait();
    CHECK(state.RequestStop() == StopAction::Deferred);
    CHECK(state.RequestStop() == StopAction::Deferred);
    CHECK(state.phase() == SubscriptionPhase::StopRequested);
    finish_advise.count_down();
    began_unadvise.wait();
    CHECK(worker_owns_unadvise);
    CHECK(state.RequestStop() == StopAction::Deferred);
    finish_unadvise.count_down();
    worker.join();
    CHECK(state.RequestStop() == StopAction::Quiesce);
    CHECK(state.CompleteQuiescence());
    CHECK_FALSE(state.CompleteQuiescence());
    CHECK(state.RequestStop() == StopAction::Stopped);
}

TEST_CASE("failed unadvise retains a retryable subscription") {
    SubscriptionState state;
    CHECK_FALSE(state.AdviseFinished(true));
    CHECK(state.RequestStop() == StopAction::Unadvise);
    state.UnadviseFinished(false);
    CHECK(state.phase() == SubscriptionPhase::Failed);
    CHECK_FALSE(state.CompleteQuiescence());
    CHECK(state.RequestStop() == StopAction::Unadvise);
    CHECK(state.RequestStop() == StopAction::Deferred);
    state.UnadviseFinished(true);
    CHECK(state.CompleteQuiescence());
}

TEST_CASE("failed advise also requires unadvise before ownership can end") {
    SubscriptionState state;
    CHECK(state.AdviseFinished(false));
    CHECK(state.RequestStop() == StopAction::Deferred);
    state.UnadviseFinished(false);
    CHECK_FALSE(state.CompleteQuiescence());
    CHECK(state.RequestStop() == StopAction::Unadvise);
}

TEST_CASE("old command messages cannot consume a new generation") {
    CommandMailbox mailbox;
    auto old = mailbox.Restart();
    CHECK(mailbox.Push(old, 1));
    CHECK_FALSE(mailbox.Push(old, 2));
    auto current = mailbox.Restart();
    CHECK(mailbox.Push(current, 4));
    CHECK(mailbox.Take(old) == 0);
    CHECK_FALSE(mailbox.Push(old, 8));
    CHECK(mailbox.Take(current) == 4);
    CHECK(mailbox.Take(current) == 0);
    CHECK(mailbox.Push(current, 1));
    CHECK_FALSE(mailbox.Push(current, 2));
    CHECK(mailbox.Pending(old) == 0);
    CHECK(mailbox.Pending(current) == 3);
    CHECK(mailbox.Take(current) == 3);
    CHECK(mailbox.Push(current, 8));
    mailbox.PostFailed(current);
    CHECK(mailbox.Push(current, 4));
    CHECK(mailbox.Take(current) == 12);
}

TEST_CASE("held handles remain queued until reset even without another report") {
    OwnedReleaseQueue<int> queue;
    auto owner = std::make_shared<int>(1);
    queue.Add(1, 42, owner);
    queue.Add(1, 42, owner);
    bool styled = true;
    int attempts = 0;
    auto held = [&](const auto&, auto) { return styled; };
    auto release = [&](const auto& observed_owner, auto handle) {
        CHECK((observed_owner == owner));
        CHECK(handle == 42);
        ++attempts;
        return true;
    };
    CHECK(queue.Drain(held, release) == 0);
    CHECK(queue.size() == 1);
    CHECK(attempts == 0);
    styled = false;
    CHECK(queue.Drain(held, release) == 1);
    CHECK(queue.empty());
    CHECK(attempts == 1);
}

TEST_CASE("failed releases and reentrant reports retain their original owners") {
    OwnedReleaseQueue<int> queue;
    auto old_owner = std::make_shared<int>(1);
    auto new_owner = std::make_shared<int>(2);
    queue.Add(1, 42, old_owner);
    queue.Add(2, 42, new_owner);
    auto unstyled = [](const auto&, auto) { return false; };
    CHECK(queue.Drain(unstyled, [&](const auto& owner, auto handle) {
        if (owner == old_owner) return false;
        queue.Add(2, handle, new_owner);
        return true;
    }) == 1);
    CHECK(queue.size() == 2);
    int old_attempts = 0, new_attempts = 0;
    CHECK(queue.Drain(unstyled, [&](const auto& owner, auto) {
        if (owner == old_owner) ++old_attempts;
        if (owner == new_owner) ++new_attempts;
        return true;
    }) == 2);
    CHECK(old_attempts == 1);
    CHECK(new_attempts == 1);
    CHECK(queue.empty());
}

TEST_CASE("release exception preserves the current and unvisited handles") {
    OwnedReleaseQueue<int> queue;
    auto owner = std::make_shared<int>(1);
    queue.Add(1, 1, owner);
    queue.Add(1, 2, owner);
    CHECK_THROWS_AS(queue.Drain([](const auto&, auto) { return false; },
        [&](const auto&, auto) -> bool {
            queue.Add(1, 3, owner);
            throw std::runtime_error("release failed");
        }), std::runtime_error);
    CHECK(queue.size() == 3);
    CHECK(queue.Drain([](const auto&, auto) { return false; },
                     [](const auto&, auto) { return true; }) == 3);
}
