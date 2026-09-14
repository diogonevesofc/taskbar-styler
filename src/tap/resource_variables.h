// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <styler/matcher.h>

namespace styler::tap {

// Application::Current().Resources() is per UI thread, so merging is too.
// Idempotent per theme: a second call with the same theme is a no-op; a
// different theme unmerges the previous one first. Called by the engine on
// the first element it styles on a thread (merging earlier, at window
// creation, does not stick - upstream's observation, vendor:18181).
void MergeResourceVariablesForThisThread(const styler::ResolvedTheme& theme);

// Restores every overridden application resource to its saved original and
// removes the merged theme dictionary. Safe when nothing was merged.
void UnmergeResourceVariablesForThisThread();

}  // namespace styler::tap
