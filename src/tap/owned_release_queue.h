// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace styler::tap {

// Data only. The caller supplies the owner and performs release outside any
// reporting callback. Swapping the batch also preserves reentrant reports.
template <typename Owner>
class OwnedReleaseQueue {
public:
    void Add(std::uint64_t owner_id, std::uint64_t handle,
             std::shared_ptr<Owner> owner) {
        if (handle && owner) pending_.try_emplace({owner_id, handle}, std::move(owner));
    }

    template <typename Held, typename Release>
    std::size_t Drain(Held&& held, Release&& release) {
        decltype(pending_) batch;
        batch.swap(pending_);
        std::size_t released = 0;
        while (!batch.empty()) {
            auto entry = batch.extract(batch.begin());
            try {
                if (held(entry.mapped(), entry.key().second) ||
                    !release(entry.mapped(), entry.key().second)) {
                    pending_.insert(std::move(entry));
                } else {
                    ++released;
                }
            } catch (...) {
                pending_.insert(std::move(entry));
                pending_.merge(batch);
                throw;
            }
        }
        return released;
    }

    bool empty() const { return pending_.empty(); }
    std::size_t size() const { return pending_.size(); }

private:
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::shared_ptr<Owner>> pending_;
};

}  // namespace styler::tap
