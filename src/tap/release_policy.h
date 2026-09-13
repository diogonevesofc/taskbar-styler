// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <vector>

namespace styler::tap {

// The pure half of the deferred-release queue: which of the queued handles
// get released on this drain. A handle is queued once per report naming it
// (a parent once per child), so duplicates are collapsed; zero is never a
// handle; and a handle whose element still carries state stays held - an
// element released while styled would be destroyed with its customization
// state stranded, since no removal is reported for a handle whose runtime
// object is gone (upstream vendor:18318-18345).
std::vector<unsigned long long> HandlesToRelease(
    std::vector<unsigned long long> pending,
    const std::function<bool(unsigned long long)>& has_state);

}  // namespace styler::tap
