// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <atomic>
#include <barrier>
#include <memory_resource>
#include <new>
#include <thread>
#include <vector>

#include <tap/handle_ledger.h>

using styler::tap::HandleLedger;
using styler::tap::HandleReleaseOutcome;
using styler::tap::HandleReleaseStatus;

TEST_CASE("handle ledger deduplicates observations and ignores zero") {
    HandleLedger ledger;
    CHECK_FALSE(ledger.Observe(1, 0));
    CHECK_FALSE(ledger.Observe(0, 1));
    CHECK(ledger.Observe(1, 10));
    CHECK(ledger.Observe(1, 10));
    CHECK(ledger.Observe(1, 20));
    const auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 2);
    CHECK(snapshot.successful_releases == 0);
    CHECK(snapshot.complete);
}

TEST_CASE("handle ledger counts a report after successful release again") {
    HandleLedger ledger;
    REQUIRE(ledger.Observe(1, 10));
    auto token = ledger.BeginRelease(1, 10);
    REQUIRE(token.revision != 0);
    CHECK(token.status == HandleReleaseStatus::Started);
    CHECK(ledger.BeginRelease(1, 10).revision == 0);
    CHECK(ledger.BeginRelease(1, 10).status == HandleReleaseStatus::InFlight);
    ledger.CompleteRelease(token, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 0);
    CHECK(ledger.Snapshot().successful_releases == 1);

    REQUIRE(ledger.Observe(1, 10));
    ledger.CompleteRelease(token, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 1);
    CHECK(ledger.Snapshot().successful_releases == 1);
    auto second = ledger.BeginRelease(1, 10);
    CHECK(second.revision != token.revision);
    ledger.CompleteRelease(second, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 0);
    CHECK(ledger.Snapshot().successful_releases == 2);
}

TEST_CASE("failed and unavailable releases remain outstanding and can retry") {
    HandleLedger ledger;
    REQUIRE(ledger.Observe(1, 10));
    auto token = ledger.BeginRelease(1, 10);
    ledger.CompleteRelease(token, HandleReleaseOutcome::Failed);
    auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 1);
    CHECK(snapshot.failed_releases == 1);
    CHECK(snapshot.successful_releases == 0);

    const auto failed = token;
    token = ledger.BeginRelease(1, 10);
    REQUIRE(token.revision != 0);
    CHECK(token.revision != failed.revision);
    ledger.CompleteRelease(failed, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().successful_releases == 0);
    CHECK(ledger.BeginRelease(1, 10).revision == 0);
    ledger.CompleteRelease(token, HandleReleaseOutcome::Unavailable);
    snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 1);
    CHECK(snapshot.failed_releases == 1);
    CHECK(snapshot.successful_releases == 0);
    token = ledger.BeginRelease(1, 10);
    ledger.CompleteRelease(token, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 0);
}

TEST_CASE("a reentrant report survives completion of the earlier release") {
    HandleLedger ledger;
    REQUIRE(ledger.Observe(1, 10));
    const auto first = ledger.BeginRelease(1, 10);
    REQUIRE(ledger.Observe(1, 10));
    ledger.CompleteRelease(first, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 1);
    CHECK(ledger.Snapshot().successful_releases == 1);
    auto second = ledger.BeginRelease(1, 10);
    REQUIRE(second.revision != 0);
    CHECK(second.revision != first.revision);
    ledger.CompleteRelease(first, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.BeginRelease(1, 10).revision == 0);
    ledger.CompleteRelease(second, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 0);
    CHECK(ledger.Snapshot().successful_releases == 2);
}

TEST_CASE("retired logical owners retain residuals and cannot erase new owners") {
    HandleLedger ledger;
    REQUIRE(ledger.Observe(1, 10));
    const auto old = ledger.BeginRelease(1, 10);
    ledger.MarkOwnerRetired(1);
    REQUIRE(ledger.Observe(1, 10));
    REQUIRE(ledger.Observe(2, 10));
    REQUIRE(ledger.Observe(1, 20, true));
    auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 3);
    CHECK(snapshot.residual_outstanding == 2);
    ledger.CompleteRelease(old, HandleReleaseOutcome::Unavailable);
    CHECK(ledger.Snapshot().residual_outstanding == 2);

    auto retry = ledger.BeginRelease(1, 10);
    ledger.CompleteRelease(retry, HandleReleaseOutcome::Succeeded);
    snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 2);
    CHECK(snapshot.residual_outstanding == 1);
    CHECK(ledger.BeginRelease(2, 10).revision != 0);
}

namespace {

class FailingResource final : public std::pmr::memory_resource {
public:
    bool fail = false;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (fail) {
            throw std::bad_alloc{};
        }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* pointer, std::size_t bytes,
                       std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

}  // namespace

TEST_CASE("allocation failure marks observations incomplete without losing known records") {
    FailingResource resource;
    HandleLedger ledger(&resource);
    REQUIRE(ledger.Observe(1, 10));
    resource.fail = true;
    CHECK_FALSE(ledger.Observe(1, 20));
    CHECK(ledger.BeginRelease(1, 20).status == HandleReleaseStatus::Untracked);
    auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 1);
    CHECK_FALSE(snapshot.complete);
    auto token = ledger.BeginRelease(1, 10);
    ledger.CompleteRelease(token, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().outstanding == 0);
    CHECK_FALSE(ledger.Snapshot().complete);
    resource.fail = false;
    REQUIRE(ledger.Observe(1, 30));
    CHECK_FALSE(ledger.Snapshot().complete);
}

TEST_CASE("explicit loss of coverage cannot be mistaken for an exact zero") {
    HandleLedger ledger;
    ledger.MarkIncomplete();
    const auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 0);
    CHECK_FALSE(snapshot.complete);
    CHECK(ledger.BeginRelease(1, 999).revision == 0);
    CHECK(ledger.BeginRelease(1, 999).status == HandleReleaseStatus::Untracked);
    ledger.CompleteRelease({}, HandleReleaseOutcome::Succeeded);
    CHECK(ledger.Snapshot().successful_releases == 0);
}

TEST_CASE("concurrent reports releases and snapshots remain coherent") {
    HandleLedger ledger;
    constexpr unsigned workers = 4;
    constexpr unsigned handles = 300;
    std::atomic<unsigned> completed{0};
    std::atomic<bool> coherent{true};
    std::vector<std::thread> threads;
    for (unsigned owner = 1; owner <= workers; ++owner) {
        threads.emplace_back([&, owner] {
            for (unsigned handle = 1; handle <= handles; ++handle) {
                ledger.Observe(owner, handle);
                ledger.Observe(owner, handle);
                auto token = ledger.BeginRelease(owner, handle);
                ledger.CompleteRelease(token, HandleReleaseOutcome::Succeeded);
            }
            ++completed;
        });
    }
    std::thread reader([&] {
        std::uint64_t sequence = 0;
        while (completed.load() != workers) {
            auto snapshot = ledger.Snapshot();
            if (!snapshot.complete || snapshot.sequence < sequence ||
                snapshot.outstanding > workers ||
                snapshot.successful_releases > workers * handles ||
                snapshot.residual_outstanding != 0) {
                coherent = false;
            }
            sequence = snapshot.sequence;
            std::this_thread::yield();
        }
    });
    for (auto& thread : threads) {
        thread.join();
    }
    reader.join();
    CHECK(coherent.load());
    auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 0);
    CHECK(snapshot.successful_releases == workers * handles);
    CHECK(snapshot.failed_releases == 0);
    CHECK(snapshot.complete);
}

TEST_CASE("concurrent duplicate reports and release attempts keep one registration") {
    HandleLedger ledger;
    constexpr unsigned workers = 4;
    std::barrier phase(static_cast<std::ptrdiff_t>(workers));
    std::atomic<unsigned> attempts{0};
    std::vector<std::thread> threads;
    for (unsigned worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&] {
            ledger.Observe(1, 10);
            phase.arrive_and_wait();
            auto token = ledger.BeginRelease(1, 10);
            if (token.revision) {
                ++attempts;
            }
            // All contenders observe the in-flight attempt before its
            // successful completion can erase the registration.
            phase.arrive_and_wait();
            ledger.CompleteRelease(token, HandleReleaseOutcome::Succeeded);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(attempts.load() == 1);
    auto snapshot = ledger.Snapshot();
    CHECK(snapshot.outstanding == 0);
    CHECK(snapshot.successful_releases == 1);
    CHECK(snapshot.complete);
}
