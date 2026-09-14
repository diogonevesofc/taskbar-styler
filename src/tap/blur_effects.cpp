// SPDX-License-Identifier: GPL-3.0-or-later

// INITGUID must be defined before d2d1effects.h is first pulled in (via
// tap/winrt_common.h below): without it, guiddef.h's DEFINE_GUID only
// declares `extern const GUID`, leaving the five CLSID_D2D1* symbols this
// file references with no storage and failing at link time with LNK2001.
// With INITGUID, DEFINE_GUID emits a DECLSPEC_SELECTANY definition instead,
// which the linker folds safely if another translation unit ever does the
// same. Not caught by the plan's compile-only probe (`cl /c`), which never
// links. Scoped to this one TU - no other source references these symbols
// yet.
#define INITGUID
#include <tap/blur_effects.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace styler::tap {
namespace {

// The two casts every GetProperty / GetSource needs. Kept here so the reason
// lives in one place: cppwinrt's abi_t<T> and the MIDL ABI::...::T the SDK
// interop header declares are the same vtable under two C++ type names.
ABI::Windows::Foundation::IPropertyValue* DetachPropertyValue(
    wf::IInspectable const& boxed) {
    return reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(
        boxed.as<winrt::impl::abi_t<wf::IPropertyValue>>().detach());
}

HRESULT DetachSource(wge::IGraphicsEffectSource const& source,
                     awge::IGraphicsEffectSource** out) {
    if (!source) {
        *out = nullptr;
        return E_BOUNDS;
    }
    // Preserve the actual source interface, not the object's canonical
    // IUnknown identity (which can have a different address/vtable).
    // copy_to_abi requires a null destination, even if our caller's out
    // parameter initially contains something else.
    void* raw = nullptr;
    winrt::copy_to_abi(source, raw);
    *out = static_cast<awge::IGraphicsEffectSource*>(raw);
    return S_OK;
}

}  // namespace

winrt::Windows::UI::Color ToWinRtColor(const styler::BlurColor& c) {
    return winrt::Windows::UI::Color{c.a, c.r, c.g, c.b};
}

// ---------------------------------------------------------------- noise ----

wss::IRandomAccessStream CreateNoiseStream(float density) {
    thread_local float t_cached_density = std::numeric_limits<float>::quiet_NaN();
    thread_local wss::InMemoryRandomAccessStream t_cached{nullptr};

    if (t_cached && density == t_cached_density) {
        return t_cached.CloneStream();
    }

    // 256x256 keeps the tiling seam out of sight at taskbar sizes.
    constexpr int kSize = 256;
    constexpr DWORD kBpp = 32;
    constexpr DWORD kRowSize = kSize * (kBpp / 8);
    constexpr DWORD kDataSize = kRowSize * kSize;

    BITMAPFILEHEADER file_header{
        .bfType = 0x4D42,  // "BM"
        .bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + kDataSize,
        .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER),
    };
    BITMAPINFOHEADER info_header{
        .biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = kSize,
        .biHeight = kSize,
        .biPlanes = 1,
        .biBitCount = kBpp,
        .biSizeImage = kDataSize,
    };

    // NoiseDensity is parsed from theme JSON with wcstod (src/core/blur.cpp),
    // which accepts "nan"/"inf" text, and std::clamp does not filter
    // non-finite input (NaN compares false against both bounds and would
    // pass straight through). Normalize a non-finite or non-positive density
    // to the neutral default before clamping.
    if (!(density > 0.0f)) {
        density = 1.0f;
    }

    // Density shapes the grey distribution through a power curve; precompute
    // it over the 256 possible samples instead of calling pow 65536 times.
    const float safe_density = std::clamp(density, 0.001f, 1.0f);
    const float exponent = 1.0f / safe_density;
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        lut[static_cast<size_t>(i)] = static_cast<uint8_t>(
            std::pow(i / 255.0f, exponent) * 255.0f);
    }

    // Fixed seed: the noise must be identical on every thread and every run,
    // or two taskbars would show visibly different grain.
    std::mt19937 rng(0);
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<uint8_t> pixels(kDataSize);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        uint8_t grey = lut[static_cast<size_t>(dist(rng))];
        pixels[i] = grey;
        pixels[i + 1] = grey;
        pixels[i + 2] = grey;
        // Fully opaque; NoiseOpacity is applied downstream by the
        // ColorMatrixEffect, so the texture itself stays neutral.
        pixels[i + 3] = 0xFF;
    }

    wss::InMemoryRandomAccessStream stream;
    wss::DataWriter writer(stream);
    writer.WriteBytes(winrt::array_view<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&file_header),
        reinterpret_cast<const uint8_t*>(&file_header) + sizeof(file_header)));
    writer.WriteBytes(winrt::array_view<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&info_header),
        reinterpret_cast<const uint8_t*>(&info_header) + sizeof(info_header)));
    writer.WriteBytes(pixels);

    // In-memory writes normally finish inline. Never block this XAML STA if
    // that changes: let the brush's existing error path choose its fallback.
    // GetResults also propagates an already-completed Error/Canceled result,
    // so a failed write cannot poison the per-thread bitmap cache.
    auto store = writer.StoreAsync();
    if (store.Status() == wf::AsyncStatus::Started) {
        store.Cancel();
        winrt::throw_hresult(E_PENDING);
    }
    store.GetResults();
    writer.DetachStream();
    stream.Seek(0);

    t_cached = stream;
    t_cached_density = density;
    return t_cached.CloneStream();
}

// ------------------------------------------------------- GaussianBlur ------

HRESULT GaussianBlurEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1GaussianBlur;
    return S_OK;
}

HRESULT GaussianBlurEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    const std::wstring_view n(name);
    if (n == L"BlurAmount") {
        *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    if (n == L"Optimization") {
        *index = D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    if (n == L"BorderMode") {
        *index = D2D1_GAUSSIANBLUR_PROP_BORDER_MODE;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT GaussianBlurEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 3;
    return S_OK;
}

HRESULT GaussianBlurEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    switch (index) {
        case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateSingle(BlurAmount));
            return S_OK;
        case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:
            *value = DetachPropertyValue(wf::PropertyValue::CreateUInt32(
                static_cast<uint32_t>(Optimization)));
            return S_OK;
        case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:
            *value = DetachPropertyValue(wf::PropertyValue::CreateUInt32(
                static_cast<uint32_t>(BorderMode)));
            return S_OK;
        default:
            return E_BOUNDS;
    }
} catch (...) {
    return winrt::to_hresult();
}

HRESULT GaussianBlurEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index != 0) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Source, source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT GaussianBlurEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

// --------------------------------------------------------- ColorMatrix -----

HRESULT ColorMatrixEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1ColorMatrix;
    return S_OK;
}

HRESULT ColorMatrixEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    const std::wstring_view n(name);
    if (n == L"ColorMatrix") {
        *index = D2D1_COLORMATRIX_PROP_COLOR_MATRIX;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    if (n == L"AlphaMode") {
        *index = D2D1_COLORMATRIX_PROP_ALPHA_MODE;
        // DIRECT, not COLORMATRIX_ALPHA_MODE: we box a raw
        // D2D1_COLORMATRIX_ALPHA_MODE value ourselves (GetProperty below),
        // so no further mapping conversion is needed. Matches upstream
        // (vendor:13253-13259) - the brief had this wrong.
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    if (n == L"ClampOutput") {
        *index = D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT ColorMatrixEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 3;
    return S_OK;
}

HRESULT ColorMatrixEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    switch (index) {
        case D2D1_COLORMATRIX_PROP_COLOR_MATRIX:
            *value = DetachPropertyValue(wf::PropertyValue::CreateSingleArray(
                winrt::array_view<const float>(Matrix.data(),
                                               Matrix.data() + Matrix.size())));
            return S_OK;
        case D2D1_COLORMATRIX_PROP_ALPHA_MODE:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(AlphaMode)));
            return S_OK;
        case D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateBoolean(ClampOutput != FALSE));
            return S_OK;
        default:
            return E_BOUNDS;
    }
} catch (...) {
    return winrt::to_hresult();
}

HRESULT ColorMatrixEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index != 0) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Source, source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT ColorMatrixEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

// ----------------------------------------------------------- Composite -----

HRESULT CompositeEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1Composite;
    return S_OK;
}

HRESULT CompositeEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    if (std::wstring_view(name) == L"Mode") {
        *index = D2D1_COMPOSITE_PROP_MODE;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CompositeEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

HRESULT CompositeEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    if (index != D2D1_COMPOSITE_PROP_MODE) {
        return E_BOUNDS;
    }
    *value = DetachPropertyValue(
        wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(Mode)));
    return S_OK;
} catch (...) {
    return winrt::to_hresult();
}

HRESULT CompositeEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index >= Sources.size()) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Sources[index], source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT CompositeEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = static_cast<UINT>(Sources.size());
    return S_OK;
}

// --------------------------------------------------------------- Flood -----

HRESULT FloodEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1Flood;
    return S_OK;
}

HRESULT FloodEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    if (std::wstring_view(name) == L"Color") {
        *index = D2D1_FLOOD_PROP_COLOR;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT FloodEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

HRESULT FloodEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    if (index != D2D1_FLOOD_PROP_COLOR) {
        return E_BOUNDS;
    }
    // Straight (non-premultiplied) RGBA, exactly as upstream sends it
    // (vendor:12847-12858). Deviating here changes how the tint composites
    // over the blur, which is the one thing this plan must not do.
    const float rgba[4] = {Color.R / 255.0f, Color.G / 255.0f,
                           Color.B / 255.0f, Color.A / 255.0f};
    *value = DetachPropertyValue(wf::PropertyValue::CreateSingleArray(
        winrt::array_view<const float>(rgba, rgba + 4)));
    return S_OK;
} catch (...) {
    return winrt::to_hresult();
}

HRESULT FloodEffect::GetSource(UINT,
                               awge::IGraphicsEffectSource** source) noexcept {
    if (!source) {
        return E_INVALIDARG;
    }
    *source = nullptr;
    return E_BOUNDS;  // Flood generates; it has no input.
}

HRESULT FloodEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 0;
    return S_OK;
}

// -------------------------------------------------------------- Border -----

HRESULT BorderEffect::GetEffectId(GUID* id) noexcept {
    if (!id) {
        return E_INVALIDARG;
    }
    *id = CLSID_D2D1Border;
    return S_OK;
}

HRESULT BorderEffect::GetNamedPropertyMapping(
    LPCWSTR name, UINT* index,
    awge::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept {
    if (!name || !index || !mapping) {
        return E_INVALIDARG;
    }
    const std::wstring_view n(name);
    if (n == L"ExtendX") {
        *index = D2D1_BORDER_PROP_EDGE_MODE_X;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    if (n == L"ExtendY") {
        *index = D2D1_BORDER_PROP_EDGE_MODE_Y;
        *mapping = awge::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT BorderEffect::GetPropertyCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 2;
    return S_OK;
}

HRESULT BorderEffect::GetProperty(
    UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept try {
    if (!value) {
        return E_INVALIDARG;
    }
    switch (index) {
        case D2D1_BORDER_PROP_EDGE_MODE_X:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(ExtendX)));
            return S_OK;
        case D2D1_BORDER_PROP_EDGE_MODE_Y:
            *value = DetachPropertyValue(
                wf::PropertyValue::CreateUInt32(static_cast<uint32_t>(ExtendY)));
            return S_OK;
        default:
            return E_BOUNDS;
    }
} catch (...) {
    return winrt::to_hresult();
}

HRESULT BorderEffect::GetSource(
    UINT index, awge::IGraphicsEffectSource** source) noexcept try {
    if (!source) {
        return E_INVALIDARG;
    }
    if (index != 0) {
        *source = nullptr;
        return E_BOUNDS;
    }
    return DetachSource(Source, source);
} catch (...) {
    return winrt::to_hresult();
}

HRESULT BorderEffect::GetSourceCount(UINT* count) noexcept {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = 1;
    return S_OK;
}

}  // namespace styler::tap
