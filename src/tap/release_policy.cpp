// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/release_policy.h>

#include <algorithm>

namespace styler::tap {

std::vector<unsigned long long> HandlesToRelease(
    std::vector<unsigned long long> pending,
    const std::function<bool(unsigned long long)>& has_state) {
    std::sort(pending.begin(), pending.end());
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
    std::vector<unsigned long long> out;
    out.reserve(pending.size());
    for (unsigned long long h : pending) {
        if (h != 0 && !has_state(h)) {
            out.push_back(h);
        }
    }
    return out;
}

}  // namespace styler::tap
