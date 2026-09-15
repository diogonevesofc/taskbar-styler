// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <xamlom.h>
#include <memory>

namespace styler::tap {
class DiagnosticsSession;

// The deferred-release queue, one per UI thread. Reports arrive from inside
// XAML's own Enter/Leave walks; releasing a handle there re-enters the
// diagnostics while the tree is being mutated and destroys an element the
// walk is still visiting. So a report only queues, and the drain runs on the
// thread's dispatcher once the burst of reports has been quiet for
// kQuietMs. Mirrors upstream vendor:18281-18430.
void QueueRelease(const std::shared_ptr<DiagnosticsSession>& owner, InstanceHandle handle);

// Arms the one-shot timer for the remaining quiet interval. A burst that ends
// in complete idleness still drains without needing another tree report.
void FlushReleasesIfQuiet();

// Drains synchronously. Only legal OUTSIDE any XAML callback, on the queue's
// own thread (e.g. from the timer tick). Never call it from
// OnVisualTreeChange.
void FlushReleasesNow();

// Rearms the "apply (as of first drain)" log line in FlushReleasesNow so the
// NEXT drain reports it again. Call once per theme change (from
// RestoreAllOnThisThread), so a reload's real apply counts get their own
// log line instead of staying silent after this thread's very first one.
void ResetInitialApplyLogged();
// Called only after the standing subscription is quiescent, on this queue's
// UI thread. Retains failed releases for a later explicit attempt.
bool StopReleaseQueueOnThisThread();

}  // namespace styler::tap
