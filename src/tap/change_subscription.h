// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

namespace styler::tap {

// Subscribes to IVisualTreeService3::AdviseVisualTreeChange for the life of
// the session.
//
// Two upstream findings shape this (both from
// vendor/upstream/windows-11-taskbar-styler.wh.cpp, and both reproduced here
// the hard way - see spike-standing-crash.md):
//
// - Composition diagnostics corrupt the heap (vendor:10904-10914). XAML
//   creates a second, unrelated diagnostics object for every
//   Windows.UI.Composition.* visual it reports (a DirectComposition visual,
//   not a XAML element), and that object rebuilds a process-wide walker with
//   no locking whenever one is added on ANY explorer UI thread - Task View,
//   for example - while another thread is inside the same code. Upstream
//   keeps it from ever being created by answering XAML's one registry read
//   (made once, from inside AdviseVisualTreeChange) through inline hooks on
//   RegOpenKeyExW/RegQueryValueExW; this project allows no injection APIs,
//   so StartSubscription instead requires the real
//   HKLM\Software\Microsoft\XAML\Debug\DisableCompositionDiag value already
//   be 1 - written once, with the user's consent, by `taskbar-styler setup`
//   (elevated) - and fails closed (E_NOT_VALID_STATE, no subscription;
//   exporting the tree still works) when it is not. change_subscription.cpp
//   also never resolves a reported Windows.UI.Composition.* handle even
//   when the value is set, as defense in depth: that filter alone stops the
//   deterministic crash (resolving one such handle is what kills the
//   process - E7), but only the registry value stops the probabilistic heap
//   race, since that race happens inside
//   XamlDiagnostics::CreateCompVisualDiag, before our callback ever runs.
// - Calling AdviseVisualTreeChange from the calling (UI) thread hangs in
//   Advising::RunOnUIThread "sometimes" (vendor:11013-11030) - measured here
//   too (E3-E5: process stayed alive but stopped responding, not a crash).
//   Upstream calls Advise from a new thread instead; StartSubscription does
//   the same via CreateThread and returns once that thread exists, without
//   waiting for the initial flood - it still arrives synchronously inside
//   Advise, just on that new thread rather than the caller's.
//
// Idempotent: a second Start with a live subscription is a no-op that
// returns S_FALSE.
HRESULT StartSubscription();

// Unadvises. If Unadvise fails the callback object is leaked on purpose -
// XAML may still call into it, and a freed vtable inside explorer is worse
// than one small leak (same stance as ReleaseOnExit in tree_export.cpp).
void StopSubscription();

}  // namespace styler::tap
