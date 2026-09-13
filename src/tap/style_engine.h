// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tap/element_registry.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// Called from the standing subscription, on the reporting thread, for every
// element reported as added (once it has an id and is a FrameworkElement)
// and removed. `reported_type` is the diagnostics' Type string - the matcher
// accepts it as an alternative to the runtime class name for the leaf.
void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type);
void OnElementRemoved(ElementId id);

// Whether this thread's engine holds customization state for `id`. The
// release drain keeps such handles held (release_policy.h).
bool ElementHasState(ElementId id);

}  // namespace styler::tap
