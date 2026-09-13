// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <inspectable.h>
#include <xamlom.h>

namespace styler::tap {

// Holds the diagnostics session against a XAML host: the QI'd IXamlDiagnostics
// and, when available, the private IXamlDiagnosticsTestHooks used to release
// handles.
//
// This file does NOT subscribe to change notifications
// (IVisualTreeServiceCallback2 / AdviseVisualTreeChange). Releasing a handle
// from inside that callback is unsafe: the report arrives from inside
// explorer's Leave walk, which is still visiting the subtree being removed,
// so releasing there destroys it mid-walk - upstream calls this out as "the
// one thing that isn't safe" and solves it by queueing the release and
// draining the queue on the host's dispatcher thread
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:11168, :18379, :18404).
// Plano 2 has no such drain and nothing here consumes a per-element change
// stream anyway (Task 5 walks the tree on demand through
// IVisualTreeService3::GetVisualRoots/GetChildren) - the subscription, and
// the deferred-release drain it requires, is deferred to Plano 3, which is
// the first plan that actually needs live change notifications.

// Opens the diagnostics session against `site`'s IXamlDiagnostics. If a
// session is already open, it is closed first. Returns a real HRESULT; on
// any failure no session is left open (Diagnostics() returns nullptr).
HRESULT OpenDiagnostics(IUnknown* site);

// Closes the session opened by OpenDiagnostics, if any. Safe to call when no
// session is open.
void CloseDiagnostics();

// The open session's IXamlDiagnostics, or nullptr if no session is open (no
// OpenDiagnostics call yet, it failed, or CloseDiagnostics ran since). This
// is a live, non-owning pointer: the caller does not Release it, and must not
// cache it past a call that could race a concurrent CloseDiagnostics - AddRef
// a private copy to hold it longer than one call (same rule as SiteOrNull(),
// see site.h).
IXamlDiagnostics* Diagnostics();

// Releases one handle the diagnostics layer reported - e.g. a handle Task 5's
// tree walk got back from IVisualTreeService3::GetChildren. Every handle the
// diagnostics layer hands out stays registered on its side and explorer.exe
// leaks for as long as it runs until this is called (spec section 7.2). Safe
// to call with handle == 0 (a root element's parent handle): that is not a
// real handle, and the underlying vtable is private and undocumented, not
// something to probe with a null handle. Also a safe no-op while no session
// is open, or IXamlDiagnosticsTestHooks is unavailable (warned once, in
// OpenDiagnostics).
void ReleaseHandle(InstanceHandle handle);

// Count of handles successfully released so far this process, via
// ReleaseHandle. Monotonic - it only increases - so it can serve as the leak
// observable spec section 7.2 asks for: a counter that could drift negative
// could never surface a stalled release path the way this one can.
long LiveHandleCount();

}  // namespace styler::tap
