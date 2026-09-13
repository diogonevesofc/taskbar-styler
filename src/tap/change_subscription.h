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
// Subscription lifetime protocol (fixes a use-after-free found in review,
// twice - the first pass left the two failure paths below unarbitrated):
// the advise thread can still be inside AdviseVisualTreeChange when a
// concurrent SetSite calls StopSubscription - e.g. a second `load` without
// restarting Explorer reached Stop about 385 ms into the first Advise in one
// measurement, and Task 5's style work only lengthens that window. Stop must
// never wait for the advise thread to finish (Advise's own walk marshals
// onto the UI thread Stop itself runs on, from SetSite - waiting there is a
// deadlock), so both sides can be trying to tear down the same Subscription
// at once. Ownership is arbitrated by one std::atomic<int> `state` on
// Subscription (Advising / Advised / Stopped). Every path that can end a
// Subscription's life - the advise thread on success, the advise thread on
// failure, StartSubscription when CreateThread itself fails, and
// StopSubscription - goes through the same two-step arbitration before it
// may delete anything: first find out (via g_subscription) whether a Stop
// has already taken this Subscription out of circulation, and if so, race
// the `state` CAS below instead of assuming either side's outcome:
//
//   - The advise thread, once AdviseVisualTreeChange returns - whether it
//     succeeded or failed - CASes Advising -> Advised (Advised here means
//     "the attempt is over, tear down through the normal path", not
//     literally "still advised"; StopSubscription only needs to know
//     whether it, or the advise side, ended up responsible). Success: a
//     later (or already-waiting) StopSubscription finds Advised and tears
//     the Subscription down - calling Unadvise regardless of whether Advise
//     itself succeeded is safe, matching tree_export.cpp's own
//     "unadvise can register, walk, and then fail" stance. Failure (state
//     already reads Stopped): a concurrent Stop got there first, found
//     Advising, and - per its own rule below - left the Subscription alone;
//     the thread itself now does the Unadvise-or-leak and deletes it.
//   - If AdviseVisualTreeChange itself fails, or CreateThread never manages
//     to start the advise thread at all, whoever is holding the Subscription
//     first CASes g_subscription from it to nullptr - never a plain store,
//     which could blank a *newer* subscription a concurrent Stop-then-Start
//     already raised. If that CAS succeeds, no Stop ever saw this
//     Subscription (g_subscription still pointed at it): full ownership,
//     tear it down directly. If it fails, a StopSubscription already
//     exchanged it out and is committed to reading its `state` - deleting it
//     here regardless, as the first version of this fix did, races that
//     read. Instead, run the exact same Advising -> Advised race the success
//     path runs: win, and the Stop holding the Subscription will find
//     Advised and do the teardown itself (only the job's own two references
//     are released here); lose (state already Stopped), and Stop already
//     left it alone for us - tear it down ourselves, the same as the
//     no-Stop-ever-saw-it case.
//   - StopSubscription first exchanges g_subscription for nullptr (detaching
//     it so nothing else can reach it - a concurrent second Stop finds
//     nullptr and no-ops), then CASes Advising -> Stopped on what it found.
//     Success: the advise attempt is still unresolved (in flight, or racing
//     the CAS above); Stop does nothing else - no Unadvise, no Release, no
//     delete - and returns StopResult::Deferred: the Subscription is still
//     live and will keep reporting until whichever side loses the race
//     above tears it down. Callers must not treat Deferred as "stopped" -
//     see StopSubscription's own comment. Failure (state already reads
//     Advised): the advise side is done with this Subscription; Stop does
//     the Unadvise-or-leak, releases it, deletes it, and returns
//     StopResult::Stopped.
//
// Whichever side ends up owning the teardown never dereferences the shared
// Subscription's callback or service pointers before that point - only
// after the arbitration above has settled who owns it. Until then, the
// advise thread works through its own AdviseJob, holding its own AddRef'd
// copies of both, independent of the Subscription's; the Subscription* it
// also carries is used only as an identity for the CASes above.
//
// Idempotent: a second Start with a live subscription is a no-op that
// returns S_FALSE.
HRESULT StartSubscription();

// None: nothing was subscribed. Stopped: unadvised (or leaked, if Unadvise
// itself failed) and torn down - a fresh StartSubscription is safe to call
// immediately. Deferred: the advise thread was still inside
// AdviseVisualTreeChange, so Stop left the old subscription running rather
// than wait (waiting would deadlock - see StartSubscription's comment); the
// old callback keeps reporting against the old session, which it holds
// alive through its own shared_ptr, until it tears itself down once Advise
// returns. Every caller must check for Deferred and skip whatever it was
// about to do next that assumes the subscription is gone - today that is
// SetSite not reopening the diagnostics session, and it will be true of
// Task 7's reload path (Stop then Start again) the same way: starting a new
// subscription while the old one is still deciding its own fate leaves two
// advise threads targeting what may become the same session.
enum class StopResult { None, Stopped, Deferred };

// Unadvises. If Unadvise fails the callback object is leaked on purpose -
// XAML may still call into it, and a freed vtable inside explorer is worse
// than one small leak (same stance as ReleaseOnExit in tree_export.cpp).
StopResult StopSubscription();

}  // namespace styler::tap
