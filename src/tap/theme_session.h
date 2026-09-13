// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <string>

namespace styler::tap {

// %APPDATA%\TaskbarStyler\config.json (spec section 4.2). Empty if APPDATA
// cannot be resolved.
std::wstring ConfigPath();

// Reads the config, loads <InitializationData()>\<theme>.json, prepares it
// and installs it with SetTheme. S_FALSE when no theme is configured (and
// SetTheme(nullptr) was applied). Fails closed: a malformed config or theme
// installs no theme and logs why (spec section 7.6).
HRESULT LoadConfiguredTheme();

}  // namespace styler::tap
