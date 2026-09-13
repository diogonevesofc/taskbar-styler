// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tap/winrt_common.h>

namespace styler::tap {

// Identity for an element across reports. A handle is an address: a
// destroyed element can be replaced by one reporting the same handle, so an
// entry is only trusted while its weak reference still resolves to the
// element being asked about (upstream vendor:11841-11900).
enum class ElementId : unsigned long long { None = 0 };

// All thread_local: an element belongs to the UI thread that reported it.
ElementId GetOrCreateElementId(InstanceHandle handle,
                               wf::IInspectable const& element);
ElementId FindElementId(InstanceHandle handle);
void ForgetElementId(InstanceHandle handle);
// Erases the entry only if its element is gone; returns whether it did.
bool ForgetElementIdIfDead(InstanceHandle handle);
// Sweeps entries whose element died without a removal being reported
// (their diagnostics reference was handed back). Amortized: runs only once
// the map has doubled since the last sweep. Calls OnElementRemoved for each.
void ReapDeadElementIdsIfNeeded();

}  // namespace styler::tap
