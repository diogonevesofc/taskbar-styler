// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
struct IUnknown;
namespace styler::tap {
// An owned snapshot, safe across concurrent site replacement.
std::shared_ptr<IUnknown> AcquireSite();
}  // namespace styler::tap
