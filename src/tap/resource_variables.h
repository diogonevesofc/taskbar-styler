// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <styler/matcher.h>

namespace styler::tap {

// Application::Current().Resources() is per UI thread, so merging is too.
// Idempotent per theme: a second call with the same theme is a no-op; a
// different theme unmerges the previous one first. Called by the engine on
// the first element it styles on a thread (merging earlier, at window
// creation, does not stick - upstream's observation, vendor:18181).
//
// Takes the shared_ptr, not a bare reference, and keeps a copy of it (same
// ABA fix as style_engine.cpp's t_cache_theme, found in review as I2):
// holding a ref keeps SetTheme(B) from freeing A's allocation out from under
// a raw `const ResolvedTheme*` that a later theme's address could legally
// reuse, aliasing this thread's "already merged" check to the wrong theme.
void MergeResourceVariablesForThisThread(
    const std::shared_ptr<const styler::ResolvedTheme>& theme);

// Restores every overridden application resource to its saved original and
// removes the merged theme dictionary. Safe when nothing was merged.
void UnmergeResourceVariablesForThisThread();

}  // namespace styler::tap
