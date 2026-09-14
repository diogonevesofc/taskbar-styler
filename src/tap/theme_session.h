// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

namespace styler::tap {

// Reads the config, loads <InitializationData()>\<theme>.json, prepares it
// and installs it with SetTheme. S_FALSE when no theme is configured (and
// SetTheme(nullptr) was applied). Fails closed: a malformed config or theme
// installs no theme and logs why (spec section 7.6).
HRESULT LoadConfiguredTheme();

// Creates kReloadEventName (ipc.h) and waits on it from a thread-pool
// thread. Each signal runs ReloadThemeOnUiThread on the taskbar UI thread.
// Idempotent: a second call while a watch is active is a no-op (S_FALSE).
HRESULT StartReloadWatch();
void StopReloadWatch();

// Disables new styling and synchronously restores every initialized thread,
// including threads whose shell host windows have been destroyed. A false
// result leaves the theme disabled; callers must not install another theme
// or close the diagnostics session until a later attempt succeeds.
bool RestoreThemeOnAllThreads();

// Restore on every initialized host thread, drop the subscription, reload
// the configured theme, re-subscribe (the fresh initial flood re-applies to
// everything, including elements the old theme never touched). Must run on
// the taskbar UI thread - the thread SetSite ran on - because that is where
// the flood lands and only initialized threads apply styles.
void ReloadThemeOnUiThread();

}  // namespace styler::tap
