// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <inspectable.h>
#include <xamlom.h>

#include <memory>
#include <string>

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
// a one-shot snapshot instead) - the standing subscription, and
// the deferred-release drain it requires, is deferred to Plano 3, which is
// the first plan that actually needs live change notifications.

// Private and undocumented; obtained by QI with a hardcoded GUID inside
// OpenDiagnostics. Only forward-declared here so DiagnosticsSession can hold
// a pointer to one - fully defined in visual_tree_watcher.cpp, the only file
// that needs its vtable shape.
struct IXamlDiagnosticsTestHooks;

// Owns one open session's IXamlDiagnostics and (when available)
// IXamlDiagnosticsTestHooks; both are released exactly once, from the
// destructor.
//
// Reached only through std::shared_ptr (see AcquireSession() below): a
// thread holding one of these keeps the session - and the COM interfaces
// inside it - alive for as long as it holds the pointer, even if another
// thread calls CloseDiagnostics concurrently. A raw pointer handed back
// across a function-return boundary cannot be protected this way: by the
// time the caller can act on it (e.g. AddRef it), it may already be freed.
// That is why there is deliberately no "borrow a raw IXamlDiagnostics*"
// accessor at namespace scope - only AcquireSession(), which hands out a
// strong reference up front.
class DiagnosticsSession {
public:
    DiagnosticsSession(IXamlDiagnostics* diagnostics,
                        IXamlDiagnosticsTestHooks* hooks);
    ~DiagnosticsSession();

    DiagnosticsSession(const DiagnosticsSession&) = delete;
    DiagnosticsSession& operator=(const DiagnosticsSession&) = delete;

    IXamlDiagnostics* diagnostics() const { return diagnostics_; }

    // Whether this session has IXamlDiagnosticsTestHooks. Check this before
    // calling ReleaseElementHandle so its HRESULT can be the vtable's own
    // unmolested return value - S_FALSE is a real (if unlikely) thing
    // UnregisterInstance itself could return, so it cannot double as a
    // "no hooks" sentinel without the counter it feeds under-reporting.
    bool has_hooks() const { return hooks_ != nullptr; }

    // Releases one handle via IXamlDiagnosticsTestHooks::UnregisterInstance.
    // Only call when has_hooks() is true.
    HRESULT ReleaseElementHandle(InstanceHandle handle) const;

private:
    IXamlDiagnostics* diagnostics_;
    IXamlDiagnosticsTestHooks* hooks_;
};

// Opens the diagnostics session against `site`'s IXamlDiagnostics. If a
// session is already open, it is replaced (the old one is closed once the
// last reference to it - including any in-flight ReleaseHandle call - goes
// away). Returns a real HRESULT; on any failure no session is left open
// (AcquireSession() returns an empty pointer).
HRESULT OpenDiagnostics(IUnknown* site);

// Closes the currently open session, if any. Safe to call when no session is
// open.
void CloseDiagnostics();

// A strong reference to the open diagnostics session, or an empty pointer if
// none is open. Holding the returned pointer keeps the session and its
// IXamlDiagnostics alive for as long as you hold it, across a concurrent
// CloseDiagnostics on another thread.
std::shared_ptr<DiagnosticsSession> AcquireSession();

// Releases one handle the diagnostics layer reported - e.g. a handle Task 5's
// snapshot got back from the initial mutation flood. Every handle the
// diagnostics layer hands out stays registered on its side and explorer.exe
// leaks for as long as it runs until this is called (spec section 7.2). Safe
// to call with handle == 0 (a root element's parent handle): that is not a
// real handle, and the underlying vtable is private and undocumented, not
// something to probe with a null handle. Also a safe no-op while no session
// is open, or IXamlDiagnosticsTestHooks is unavailable on it.
void ReleaseHandle(InstanceHandle handle);

// Count of handles successfully released so far this process, via
// ReleaseHandle. Monotonic - it only increases.
//
// This is NOT the spec section 7.2 "live handle count" gauge - that gauge is
// not implemented in Plano 2. Plano 2's tree walk releases every handle it
// touches within the same walk (see ReleaseHandle above), so there is
// nothing left alive to count once a walk finishes; a released-count can
// only confirm the release path is firing, not surface a handle nobody
// released. The live gauge belongs to Plano 3, the first plan that holds
// elements across time (via change notifications) instead of releasing them
// immediately after use.
long ReleasedHandleCount();

// The initialization string the loader passed to InitializeXamlDiagnosticsEx
// (today: the absolute themes directory), read once by OpenDiagnostics.
// Empty when none was passed. Safe to call from any thread after
// OpenDiagnostics returned.
std::wstring InitializationData();

}  // namespace styler::tap
