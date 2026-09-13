// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Forward-declared: this header only needs to name the site pointer's type,
// not pull in the full COM/XAML surface <objbase.h>/<windows.h> would bring.
struct IUnknown;

namespace styler::tap {

// Returns the current XAML diagnostics site, or nullptr if none is set.
//
// The underlying storage is written only by SetSite (in tap_boundary.cpp)
// and is synchronized (std::atomic) because the TAP is called from several
// explorer UI threads. Do not cache the returned pointer across calls: a
// concurrent SetSite(nullptr) can Release it the moment this function
// returns. AddRef it yourself if you need to hold onto it past this call.
IUnknown* SiteOrNull();

}  // namespace styler::tap
