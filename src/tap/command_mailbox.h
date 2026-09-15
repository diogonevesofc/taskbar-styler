// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <mutex>

namespace styler::tap {
// One posted wakeup represents all pending commands of this generation.
// Retiring a generation also retires already-posted window messages.
class CommandMailbox {
public:
    std::uint64_t Restart() {
        std::lock_guard lock(mutex_);
        pending_ = 0;
        posted_ = false;
        return ++generation_;
    }
    std::uint64_t generation() const {
        std::lock_guard lock(mutex_);
        return generation_;
    }
    bool Push(std::uint64_t generation, unsigned flags) {
        std::lock_guard lock(mutex_);
        if (generation != generation_) return false;
        pending_ |= flags;
        if (posted_) return false;
        posted_ = true;
        return true;
    }
    unsigned Take(std::uint64_t generation) {
        std::lock_guard lock(mutex_);
        if (generation != generation_) return 0;
        posted_ = false;
        const auto flags = pending_;
        pending_ = 0;
        return flags;
    }
    unsigned Pending(std::uint64_t generation) const {
        std::lock_guard lock(mutex_);
        return generation == generation_ ? pending_ : 0;
    }
    void PostFailed(std::uint64_t generation) {
        std::lock_guard lock(mutex_);
        if (generation == generation_) posted_ = false;
    }
private:
    mutable std::mutex mutex_;
    std::uint64_t generation_ = 0;
    unsigned pending_ = 0;
    bool posted_ = false;
};
}  // namespace styler::tap
