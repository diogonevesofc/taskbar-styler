// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>
#include <inspectable.h>
#include <xamlom.h>

namespace styler::tap {

// Every element the diagnostics layer reports is registered on its side and
// must be released, or explorer.exe leaks for as long as it runs. The release
// path needs IXamlDiagnosticsTestHooks, a private interface obtained by QI with
// a hardcoded GUID; if a future Windows drops it we warn loudly rather than
// leak in silence (spec section 7.2).
long LiveHandleCount();

HRESULT StartWatching(IUnknown* site);
void StopWatching();

// The site's IXamlDiagnostics, or nullptr before StartWatching succeeded.
IXamlDiagnostics* Diagnostics();

}  // namespace styler::tap
