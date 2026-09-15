// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <memory>

namespace styler::tap {
// SetSite only queues an owned site. Main-UI transitions restore, quiesce and
// drain before replacing its diagnostics session. Null requests full teardown.
void RequestSessionChange(std::shared_ptr<IUnknown> site);

// Reads the config, loads <InitializationData()>\<theme>.json, prepares it
// and installs it with SetTheme. S_FALSE when no theme is configured (and
// SetTheme(nullptr) was applied). Fails closed: a malformed config or theme
// installs no theme and logs why (spec section 7.6).
HRESULT LoadConfiguredTheme();

// Creates the reload/export events. Pool callbacks only post coalesced,
// generation-checked work to the private main-UI window.
// Idempotent: a second call while a watch is active is a no-op (S_FALSE).
HRESULT StartReloadWatch();
void StopReloadWatch();

// Disables new styling and synchronously restores every initialized thread,
// including threads whose shell host windows have been destroyed. A false
// result leaves the theme disabled; callers must not install another theme
// or close the diagnostics session until a later attempt succeeds.
bool RestoreThemeOnAllThreads();

}  // namespace styler::tap
