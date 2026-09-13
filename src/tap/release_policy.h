// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <vector>

namespace styler::tap {

// The result of one release-policy pass. `unique_count` is the number of
// distinct real handles considered - after dedup, with the zero sentinel
// dropped - so a caller can compute how many of THOSE were held back for a
// reason (as opposed to duplicates or the sentinel, which unique_count
// already excludes): `unique_count - to_release.size()`. That subtraction is
// the release_queue.cpp "held" gauge; it is 0 whenever has_state never
// returns true, on any input, which is what makes it worth reviewing for
// (found in review: measuring it against the raw, undeduped queue length
// instead counted duplicate parent handles as "held").
struct ReleaseResult {
    std::vector<unsigned long long> to_release;
    size_t unique_count = 0;
};

// The pure half of the deferred-release queue: which of the queued handles
// get released on this drain. A handle is queued once per report naming it
// (a parent once per child), so duplicates are collapsed; zero is never a
// handle; and a handle whose element still carries state stays held - an
// element released while styled would be destroyed with its customization
// state stranded, since no removal is reported for a handle whose runtime
// object is gone (upstream vendor:18318-18345).
ReleaseResult HandlesToRelease(
    std::vector<unsigned long long> pending,
    const std::function<bool(unsigned long long)>& has_state);

}  // namespace styler::tap
