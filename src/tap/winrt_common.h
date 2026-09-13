// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The one place that pulls C++/WinRT in. Every other TAP source includes
// this, never winrt/*.h directly, so the include set (and the mandatory
// Windows.Foundation.Collections.h - without it IVector::GetAt fails with
// C3779) is decided once. These headers cost ~15-20 s per translation unit;
// keep the number of TUs that include this small.
#ifndef WINRT_LEAN_AND_MEAN
#define WINRT_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <inspectable.h>
#include <xamlom.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace styler::tap {

namespace wf = winrt::Windows::Foundation;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxm = winrt::Windows::UI::Xaml::Media;

// Wraps a WinRT object obtained from a raw diagnostics out-parameter without
// touching its reference count: GetIInspectableFromHandle / GetUiLayer
// already AddRef'd it, and com_ptr::attach takes that reference over.
inline wf::IInspectable InspectableFromRaw(::IInspectable* raw) {
    winrt::com_ptr<::IInspectable> owned;
    owned.attach(raw);
    return owned.as<wf::IInspectable>();
}

}  // namespace styler::tap
