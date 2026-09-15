// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/handle_ledger.h>

#include <functional>
#include <limits>

namespace styler::tap {

HandleLedger::HandleLedger(std::pmr::memory_resource* resource)
    : entries_(resource) {}

std::size_t HandleLedger::KeyHash::operator()(const Key& key) const noexcept {
    const auto owner = std::hash<std::uint64_t>{}(key.owner);
    const auto handle = std::hash<std::uint64_t>{}(key.handle);
    return owner ^ (handle + static_cast<std::size_t>(0x9e3779b9U) +
                    (owner << 6) + (owner >> 2));
}

std::uint64_t HandleLedger::NextRevisionLocked() noexcept {
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        MarkIncomplete();
        return 0;
    }
    return ++revision_;
}

bool HandleLedger::Observe(std::uint64_t owner, std::uint64_t handle,
                            bool retired) noexcept {
    if (!owner || !handle) {
        return false;
    }
    try {
        std::lock_guard lock(mutex_);
        const auto revision = NextRevisionLocked();
        if (!revision) {
            return false;
        }
        auto [it, inserted] = entries_.try_emplace(Key{owner, handle});
        (void)inserted;
        it->second.observation = revision;
        it->second.retired = it->second.retired || retired;
        ++sequence_;
        return true;
    } catch (...) {
        // An unrecorded report must remain visible as lost coverage, even
        // when allocation fails inside a noexcept XAML callback.
        MarkIncomplete();
        return false;
    }
}

void HandleLedger::MarkOwnerRetired(std::uint64_t owner) noexcept {
    try {
        std::lock_guard lock(mutex_);
        bool changed = false;
        for (auto& [key, entry] : entries_) {
            if (key.owner == owner && !entry.retired) {
                entry.retired = true;
                changed = true;
            }
        }
        if (changed) {
            ++sequence_;
        }
    } catch (...) {
        MarkIncomplete();
    }
}

HandleReleaseToken HandleLedger::BeginRelease(std::uint64_t owner,
                                              std::uint64_t handle) noexcept {
    try {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(Key{owner, handle});
        if (it == entries_.end()) {
            return {owner, handle, 0, HandleReleaseStatus::Untracked};
        }
        if (it->second.release_attempt) {
            return {owner, handle, 0, HandleReleaseStatus::InFlight};
        }
        const auto revision = NextRevisionLocked();
        if (!revision) {
            return {};
        }
        it->second.release_attempt = revision;
        it->second.release_observation = it->second.observation;
        ++sequence_;
        return {owner, handle, revision, HandleReleaseStatus::Started};
    } catch (...) {
        MarkIncomplete();
        return {};
    }
}

void HandleLedger::CompleteRelease(HandleReleaseToken token,
                                    HandleReleaseOutcome outcome) noexcept {
    if (!token.revision) {
        return;
    }
    try {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(Key{token.owner, token.handle});
        if (it == entries_.end() ||
            it->second.release_attempt != token.revision) {
            return;  // Duplicate completion, or an earlier attempt's result.
        }
        Entry& entry = it->second;
        entry.release_attempt = 0;
        ++sequence_;
        if (outcome == HandleReleaseOutcome::Succeeded) {
            ++successful_releases_;
            if (entry.observation == entry.release_observation) {
                entries_.erase(it);
            }
            // A report during the external call may have recreated the
            // registration. Keep that newer observation outstanding.
        } else if (outcome == HandleReleaseOutcome::Failed) {
            ++failed_releases_;
        }
    } catch (...) {
        MarkIncomplete();
    }
}

void HandleLedger::MarkIncomplete() noexcept {
    complete_.store(false, std::memory_order_relaxed);
}

HandleLedgerSnapshot HandleLedger::Snapshot() const noexcept {
    try {
        std::lock_guard lock(mutex_);
        std::size_t residual = 0;
        for (const auto& [key, entry] : entries_) {
            (void)key;
            if (entry.retired) {
                ++residual;
            }
        }
        return {sequence_, entries_.size(), residual, successful_releases_,
                failed_releases_, complete_.load(std::memory_order_relaxed)};
    } catch (...) {
        return {};  // Default snapshot explicitly has complete=false.
    }
}

}  // namespace styler::tap
