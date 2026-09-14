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
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.ViewManagement.h>

// Composition + the D2D effect interop, for the WindhawkBlur brush
// (blur_effects.h, blur_brush.h). d2d1effects_2.h pulls d2d1effects_1.h and
// d2d1effects.h (the CLSIDs and the property enums) but NOT d2d1_1.h, which
// is where D2D1_COMPOSITE_MODE lives - measured: without the explicit
// include, blur_effects.h fails with C3646 on CompositeEffect::Mode.
// windows.graphics.effects.interop.h is the SDK's own declaration of
// IGraphicsEffectD2D1Interop - the upstream mod redeclares it by hand only
// because a Windhawk mod is a single file.
#include <d2d1_1.h>
#include <d2d1effects_2.h>
#include <windows.graphics.effects.interop.h>

#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Power.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

// Required for `.as<winrt::impl::abi_t<IPropertyValue>>()`, which is how an
// IGraphicsEffectD2D1Interop::GetProperty implementation hands a boxed value
// back across the ABI. Without it the cast does not compile
// (vendor:12490-12493, confirmed by the plan's probe).
template <>
inline constexpr winrt::guid winrt::impl::guid_v<
    winrt::impl::abi_t<winrt::Windows::Foundation::IPropertyValue>>{
    winrt::impl::guid_v<winrt::Windows::Foundation::IPropertyValue>};

namespace styler::tap {

namespace wf = winrt::Windows::Foundation;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wge = winrt::Windows::Graphics::Effects;
namespace awge = ABI::Windows::Graphics::Effects;
namespace wuc = winrt::Windows::UI::Composition;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wss = winrt::Windows::Storage::Streams;

// Wraps a WinRT object obtained from a raw diagnostics out-parameter without
// touching its reference count: GetIInspectableFromHandle / GetUiLayer
// already AddRef'd it, and com_ptr::attach takes that reference over.
inline wf::IInspectable InspectableFromRaw(::IInspectable* raw) {
    winrt::com_ptr<::IInspectable> owned;
    owned.attach(raw);
    return owned.as<wf::IInspectable>();
}

}  // namespace styler::tap
