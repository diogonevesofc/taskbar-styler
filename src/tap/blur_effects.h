// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <vector>

#include <styler/blur.h>
#include <tap/winrt_common.h>

namespace styler::tap {

winrt::Windows::UI::Color ToWinRtColor(const styler::BlurColor& c);

// A 256x256 tiled grayscale noise bitmap, as a stream Composition's
// LoadedImageSurface can read. Cached per density on the calling thread, so
// the 65k-pixel generation happens once per distinct NoiseDensity per XAML
// host thread. Callers get an independent clone (own seek cursor).
// vendor:12412-12470.
wss::IRandomAccessStream CreateNoiseStream(float density);

// --- The five D2D effects the blur graph needs ------------------------------
//
// Composition accepts any object implementing IGraphicsEffect plus
// IGraphicsEffectD2D1Interop as an effect description: the interop interface
// names the D2D CLSID, the property indices, and the sources. Nothing here
// touches D2D itself - these are pure descriptions the compositor realises.
//
// Note the shape every one of them shares, which is NOT obvious:
//   * the interop methods carry NO `override` and NO `IFACEMETHODIMP` -
//     winrt::implements dispatches through a CRTP shim, and `override` fails
//     to compile with C3668;
//   * GetProperty and GetSource take the MIDL ABI pointer types from the SDK
//     header (ABI::Windows::Foundation::IPropertyValue**,
//     awge::IGraphicsEffectSource**), not winrt::impl::abi_t<...>**. The two
//     are binary-identical but distinct C++ types, so the value is detached
//     as abi_t and reinterpret_cast across. Using abi_t in the signature
//     leaves the SDK's pure virtual unimplemented (C2259).
// Both were measured by this plan's compile probe, not assumed.

struct GaussianBlurEffect
    : winrt::implements<GaussianBlurEffect, wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
    wge::IGraphicsEffectSource Source{nullptr};
    float BlurAmount = 0.0f;
    D2D1_GAUSSIANBLUR_OPTIMIZATION Optimization =
        D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED;
    D2D1_BORDER_MODE BorderMode = D2D1_BORDER_MODE_SOFT;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    // Upstream leaves this on its type-name default for every effect it
    // never explicitly renames (vendor:13060); Task 4's noise graph relies
    // on that so two effects don't end up sharing an empty "" name.
    winrt::hstring m_name{L"GaussianBlurEffect"};
};

struct ColorMatrixEffect
    : winrt::implements<ColorMatrixEffect, wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
    wge::IGraphicsEffectSource Source{nullptr};
    // 5x4, row-major: rows 0-3 scale R/G/B/A, row 4 is the offset.
    // Identity by default.
    std::array<float, 20> Matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
                                1, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    D2D1_COLORMATRIX_ALPHA_MODE AlphaMode =
        D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    BOOL ClampOutput = FALSE;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    // vendor:13222 - see the comment on GaussianBlurEffect::m_name.
    winrt::hstring m_name{L"ColorMatrixEffect"};
};

struct CompositeEffect
    : winrt::implements<CompositeEffect, wge::IGraphicsEffect,
                        wge::IGraphicsEffectSource,
                        awge::IGraphicsEffectD2D1Interop> {
    std::vector<wge::IGraphicsEffectSource> Sources;
    D2D1_COMPOSITE_MODE Mode = D2D1_COMPOSITE_MODE_SOURCE_OVER;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    // vendor:12666 - see the comment on GaussianBlurEffect::m_name. Upstream
    // leaves the root Composite on this default (never calls its Name()
    // setter), so Task 4's brush must match it exactly.
    winrt::hstring m_name{L"CompositeEffect"};
};

struct FloodEffect : winrt::implements<FloodEffect, wge::IGraphicsEffect,
                                      wge::IGraphicsEffectSource,
                                      awge::IGraphicsEffectD2D1Interop> {
    winrt::Windows::UI::Color Color{};

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    // vendor:12791 - see the comment on GaussianBlurEffect::m_name.
    winrt::hstring m_name{L"FloodEffect"};
};

struct BorderEffect : winrt::implements<BorderEffect, wge::IGraphicsEffect,
                                       wge::IGraphicsEffectSource,
                                       awge::IGraphicsEffectD2D1Interop> {
    wge::IGraphicsEffectSource Source{nullptr};
    D2D1_BORDER_EDGE_MODE ExtendX = D2D1_BORDER_EDGE_MODE_WRAP;
    D2D1_BORDER_EDGE_MODE ExtendY = D2D1_BORDER_EDGE_MODE_WRAP;

    winrt::hstring Name() const noexcept { return m_name; }
    void Name(winrt::hstring name) { m_name = std::move(name); }

    HRESULT GetEffectId(GUID* id) noexcept;
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept;
    HRESULT GetPropertyCount(UINT* count) noexcept;
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) noexcept;
    HRESULT GetSource(UINT index, awge::IGraphicsEffectSource** source) noexcept;
    HRESULT GetSourceCount(UINT* count) noexcept;

   private:
    // vendor:12918 - see the comment on GaussianBlurEffect::m_name. Upstream
    // leaves the Border on this default (never calls its Name() setter), so
    // Task 4's brush must match it exactly.
    winrt::hstring m_name{L"BorderEffect"};
};

}  // namespace styler::tap
