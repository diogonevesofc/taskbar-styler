// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/release_policy.h>

#include <algorithm>

namespace styler::tap {

ReleaseResult HandlesToRelease(
    std::vector<unsigned long long> pending,
    const std::function<bool(unsigned long long)>& has_state) {
    std::sort(pending.begin(), pending.end());
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
    ReleaseResult result;
    result.to_release.reserve(pending.size());
    for (unsigned long long h : pending) {
        if (h == 0) {
            continue;  // Never a real handle - not counted as unique either.
        }
        ++result.unique_count;
        if (!has_state(h)) {
            result.to_release.push_back(h);
        }
    }
    return result;
}

}  // namespace styler::tap
