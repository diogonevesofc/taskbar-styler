// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <mutex>
#include <unordered_map>

namespace styler::tap {

// IDs identify logical owners, not proven independent physical XAML caches.
// A token identifies one attempt, including retries of the same observation.
enum class HandleReleaseStatus { Untracked, InFlight, Started, Unavailable };

struct HandleReleaseToken {
    std::uint64_t owner = 0;
    std::uint64_t handle = 0;
    std::uint64_t revision = 0;  // Zero means no release was begun.
    HandleReleaseStatus status = HandleReleaseStatus::Unavailable;
};

enum class HandleReleaseOutcome { Succeeded, Failed, Unavailable };

struct HandleLedgerSnapshot {
    std::uint64_t sequence = 0;
    std::size_t outstanding = 0;
    std::size_t residual_outstanding = 0;
    std::uint64_t successful_releases = 0;
    std::uint64_t failed_releases = 0;
    bool complete = false;
};

// Pure accounting of observed registrations without confirmed release. No
// COM objects or callbacks enter this class; every snapshot is one locked
// copy. Entries exist only while outstanding, including retired owners.
class HandleLedger {
public:
    // The optional resource supports deterministic allocation-failure tests;
    // it must outlive this ledger and follow memory_resource's contract.
    explicit HandleLedger(
        std::pmr::memory_resource* resource = std::pmr::get_default_resource());

    // Reports of a retired owner must pass retired=true. The caller serializes
    // owner retirement and Observe so a previously read false flag cannot
    // insert a new entry after MarkOwnerRetired. No retired-owner history is
    // kept once that owner's outstanding entries have all been released.
    bool Observe(std::uint64_t owner, std::uint64_t handle,
                 bool retired = false) noexcept;
    void MarkOwnerRetired(std::uint64_t owner) noexcept;

    // Call outside XAML callbacks, just before the actual release. A second
    // attempt for an in-flight key returns InFlight. Untracked means a missing
    // observation, not a veto on actual cleanup: the caller can still release
    // without confirming a tracked removal, and must mark coverage incomplete.
    // Unavailable cannot establish whether another release is in flight.
    // Complete every Started token, including no-hooks/closed-owner paths.
    HandleReleaseToken BeginRelease(std::uint64_t owner,
                                     std::uint64_t handle) noexcept;
    void CompleteRelease(HandleReleaseToken token,
                         HandleReleaseOutcome outcome) noexcept;

    // Coverage loss is sticky; zero with complete=false is not an exact zero.
    void MarkIncomplete() noexcept;
    HandleLedgerSnapshot Snapshot() const noexcept;

private:
    struct Key {
        std::uint64_t owner;
        std::uint64_t handle;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };
    struct Entry {
        std::uint64_t observation = 0;
        std::uint64_t release_attempt = 0;
        std::uint64_t release_observation = 0;
        bool retired = false;
    };

    std::uint64_t NextRevisionLocked() noexcept;

    mutable std::mutex mutex_;
    std::pmr::unordered_map<Key, Entry, KeyHash> entries_;
    std::uint64_t revision_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t successful_releases_ = 0;
    std::uint64_t failed_releases_ = 0;
    std::atomic<bool> complete_{true};
};

}  // namespace styler::tap
