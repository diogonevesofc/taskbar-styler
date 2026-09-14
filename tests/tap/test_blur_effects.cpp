// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include <tap/blur_effects.h>

using namespace styler::tap;

TEST_CASE("effect source returns its interface instead of canonical IUnknown") {
    auto flood = winrt::make_self<FloodEffect>();
    auto blur = winrt::make_self<GaussianBlurEffect>();
    blur->Source = flood.as<wge::IGraphicsEffectSource>();

    // These effects implement IGraphicsEffect first, so their source is a
    // separate interface subobject. The old IUnknown cast loses this offset.
    void* expected = winrt::get_abi(blur->Source);
    auto identity = blur->Source.as<::IUnknown>();
    REQUIRE(expected != static_cast<void*>(identity.get()));

    auto* raw = reinterpret_cast<awge::IGraphicsEffectSource*>(uintptr_t{1});
    REQUIRE(blur->GetSource(0, &raw) == S_OK);
    winrt::com_ptr<awge::IGraphicsEffectSource> source;
    source.attach(raw);
    CHECK(static_cast<void*>(source.get()) == expected);

    winrt::com_ptr<awge::IGraphicsEffectSource> queried;
    CHECK(source->QueryInterface(__uuidof(awge::IGraphicsEffectSource),
                                 queried.put_void()) == S_OK);
    CHECK(queried.get() == source.get());
}

TEST_CASE("noise bitmap writes complete on an STA and clones keep separate cursors") {
    struct ApartmentScope {
        ApartmentScope() {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        }
        ~ApartmentScope() { winrt::uninit_apartment(); }
    };
    // CreateNoiseStream retains a thread-local WinRT stream. Initialize this
    // guard first and destroy it last, so the cached stream releases while
    // its COM apartment is still alive, including after the test returns.
    thread_local ApartmentScope apartment;

    auto first = CreateNoiseStream(0.5f);
    REQUIRE(first);
    constexpr uint32_t header_size = sizeof(BITMAPFILEHEADER) +
                                     sizeof(BITMAPINFOHEADER);
    constexpr uint32_t bitmap_size = header_size + 256 * 256 * 4;
    CHECK(first.Size() == bitmap_size);
    CHECK(first.Position() == 0);

    wss::DataReader reader(first);
    auto load = reader.LoadAsync(header_size);
    REQUIRE(load.Status() == wf::AsyncStatus::Completed);
    REQUIRE(load.GetResults() == header_size);
    std::array<uint8_t, header_size> bytes{};
    reader.ReadBytes(bytes);
    reader.DetachStream();

    BITMAPFILEHEADER file_header{};
    BITMAPINFOHEADER info_header{};
    std::memcpy(&file_header, bytes.data(), sizeof(file_header));
    std::memcpy(&info_header, bytes.data() + sizeof(file_header),
                sizeof(info_header));
    CHECK(file_header.bfType == 0x4D42);
    CHECK(file_header.bfSize == bitmap_size);
    CHECK(file_header.bfOffBits == header_size);
    CHECK(info_header.biWidth == 256);
    CHECK(info_header.biHeight == 256);
    CHECK(info_header.biBitCount == 32);

    first.Seek(123);
    auto second = CreateNoiseStream(0.5f);
    CHECK(second.Size() == bitmap_size);
    CHECK(second.Position() == 0);
    CHECK(first.Position() == 123);
}
