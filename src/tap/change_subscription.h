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
//   waiting for the initial flood. Measured here: the flood does not arrive
//   on that new thread. XAML marshals the whole walk onto the UI thread that
//   actually owns the elements (the same thread SetSite runs on) - the new
//   thread's call to AdviseVisualTreeChange simply blocks until that
//   marshalled walk finishes, which is exactly the "Advising::RunOnUIThread"
//   the paragraph above names. This is why the IsInitializedForCurrentThread
//   guard in OnVisualTreeChange does not discard the flood: every report in
//   it runs on the already-initialized UI thread, never on the brand-new,
//   uninitialized advise thread.
//
// Subscription lifetime protocol (fixes a use-after-free found in review):
// the advise thread can still be inside AdviseVisualTreeChange when a
// concurrent SetSite calls StopSubscription - e.g. a second `load` without
// restarting Explorer reached Stop about 385 ms into the first Advise in one
// measurement, and Task 5's style work only lengthens that window. Stop must
// never wait for the advise thread to finish (Advise's own walk marshals
// onto the UI thread Stop itself runs on, from SetSite - waiting there is a
// deadlock), so both sides can be trying to tear down the same Subscription
// at once. Ownership is arbitrated by one std::atomic<int> `state` on
// Subscription (Advising / Advised / Stopped), one compare_exchange_strong
// per side, from Advising:
//
//   - The advise thread, once AdviseVisualTreeChange returns successfully,
//     CASes Advising -> Advised. Success: the subscription is now live and
//     untouched by the thread from here on; a later StopSubscription finds
//     Advised and tears it down exactly as it always has. Failure (the
//     state already reads Stopped): a concurrent Stop got there first and,
//     per its own rule below, left the Subscription alone - the thread
//     itself now does the Unadvise-or-leak, releases both its own and the
//     Subscription's references, and deletes it.
//   - If AdviseVisualTreeChange itself fails, the thread CASes g_subscription
//     from the Subscription it was given to nullptr - never a plain store,
//     which could blank a *newer* subscription a concurrent Stop-then-Start
//     already raised - and always tears the Subscription down itself: one
//     that never successfully advised has nothing for a later Stop to find,
//     regardless of whether a concurrent Stop ran (it would have found
//     Advising and, again, done nothing else).
//   - StopSubscription first exchanges g_subscription for nullptr (detaching
//     it so nothing else can reach it - a concurrent second Stop finds
//     nullptr and no-ops), then CASes Advising -> Stopped on what it found.
//     Success: the advise thread is still inside Advise; Stop does nothing
//     else - no Unadvise, no Release, no delete - and returns. Failure (the
//     state already reads Advised): Advise already completed and the thread
//     is done touching this Subscription; Stop does the Unadvise-or-leak and
//     releases it, as it always has.
//
// The advise thread never dereferences the shared Subscription's callback or
// service pointers while the outcome is still undecided - a concurrent
// StopSubscription racing the CAS above could free them under it. It works
// instead through its own AdviseJob, holding its own AddRef'd copies of
// both, independent of the Subscription's; the Subscription* it also carries
// is used only as an identity for the CASes above until the protocol decides
// which side owns the teardown - at that point, exactly one side is left
// holding it, and only then does it dereference callback/service to Unadvise
// and Release them.
//
// Idempotent: a second Start with a live subscription is a no-op that
// returns S_FALSE.
HRESULT StartSubscription();

// Unadvises. If Unadvise fails the callback object is leaked on purpose -
// XAML may still call into it, and a freed vtable inside explorer is worse
// than one small leak (same stance as ReleaseOnExit in tree_export.cpp).
void StopSubscription();

}  // namespace styler::tap
