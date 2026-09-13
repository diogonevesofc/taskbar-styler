// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <guiddef.h>

namespace styler::tap {

// {B3A1F27C-6E45-4D8A-9F31-0C7E5A2D91B4}
// Registration-free: the XAML diagnostics layer LoadLibrary's our DLL by path
// and calls our exported DllGetClassObject with this CLSID. Nothing is written
// to the registry.
inline constexpr CLSID CLSID_TaskbarStylerTap = {
    0xb3a1f27c,
    0x6e45,
    0x4d8a,
    {0x9f, 0x31, 0x0c, 0x7e, 0x5a, 0x2d, 0x91, 0xb4}};

}  // namespace styler::tap
