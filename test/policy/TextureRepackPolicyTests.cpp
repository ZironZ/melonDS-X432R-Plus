// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolicyTestHarness.h"

#include "GPU3D_TexcacheMip.h"

#include <cmath>

using namespace melonDS;

namespace
{

u8 ExpandRGB6ToRGB8(u8 value)
{
    return static_cast<u8>(std::lround(static_cast<float>(value) * 255.0f / 63.0f));
}

u32 MakeRGBA8(u8 value)
{
    return static_cast<u32>(value) |
        (static_cast<u32>(value) << 8) |
        (static_cast<u32>(value) << 16) |
        0xFF000000u;
}

}

POLICY_TEST(RGB6RepackPreservesEveryExpandedRGB6Value)
{
    for (int value6 = 0; value6 <= 63; value6++)
    {
        const u8 expanded = ExpandRGB6ToRGB8(static_cast<u8>(value6));
        CHECK_EQ(QuantizeRGB8ToRGB6Roundtrip(expanded), value6);
    }
}

POLICY_TEST(RGB6NativeExpansionPreservesEveryRGB5Anchor)
{
    for (int value5 = 0; value5 <= 31; value5++)
    {
        const u8 expected = value5 == 0 ? 0 : static_cast<u8>(value5 * 2 + 1);
        CHECK_EQ(QuantizeRGB8ToRGB6LikeDS(static_cast<float>(value5 * 8)), expected);
    }
}

POLICY_TEST(RGB6MipLevelsUseTheirOwnRepackPolicy)
{
    TexcacheMipChain chain;

    auto& expandedLevel = chain.AddLevel(1, 1, RGB6RepackPolicy::PreserveExpandedRGB6);
    expandedLevel.PreviewRGBA8.push_back(MakeRGBA8(ExpandRGB6ToRGB8(22)));

    auto& nativeLevel = chain.AddLevel(1, 1, RGB6RepackPolicy::NativeRGB5Expansion);
    nativeLevel.PreviewRGBA8.push_back(MakeRGBA8(10 * 8));

    TextureMipPackLevels(chain, outputFmt_RGB6A5);

    CHECK_EQ(chain.Levels[0].Packed[0] & 0xFF, 22);
    CHECK_EQ(chain.Levels[1].Packed[0] & 0xFF, 21);
}

POLICY_TEST(RGB6PolicyDoesNotAffectRGBA8OrBGRA8Output)
{
    constexpr u32 Source = 0x7F123456u;
    u32 nativeRGBA = 0;
    u32 preserveRGBA = 0;
    u32 nativeBGRA = 0;
    u32 preserveBGRA = 0;

    ConvertRGBA8BufferToOutput<outputFmt_RGBA8>(
        1, 1, &Source, &nativeRGBA, false, RGB6RepackPolicy::NativeRGB5Expansion);
    ConvertRGBA8BufferToOutput<outputFmt_RGBA8>(
        1, 1, &Source, &preserveRGBA, false, RGB6RepackPolicy::PreserveExpandedRGB6);
    ConvertRGBA8BufferToOutput<outputFmt_BGRA8>(
        1, 1, &Source, &nativeBGRA, false, RGB6RepackPolicy::NativeRGB5Expansion);
    ConvertRGBA8BufferToOutput<outputFmt_BGRA8>(
        1, 1, &Source, &preserveBGRA, false, RGB6RepackPolicy::PreserveExpandedRGB6);

    CHECK_EQ(nativeRGBA, preserveRGBA);
    CHECK_EQ(nativeBGRA, preserveBGRA);
}

namespace
{

template <int Format, RGB6RepackPolicy Policy>
void CheckMipLookupPacking(const std::vector<u32>& source)
{
    std::vector<u32> reference(source.size());
    std::vector<u32> actual(source.size());
    for (bool binaryAlpha : {false, true})
    {
        // Keep the non-mip conversion as the independent reference for rounding,
        // binary-alpha thresholds and the treatment of RGB behind zero alpha.
        ConvertRGBA8BufferToOutput<Format>(source.size(), 1, source.data(), reference.data(), binaryAlpha, Policy);
        ConvertRGBA8BufferToOutputLUT<Format>(source.size(), 1, source.data(), actual.data(), binaryAlpha, Policy);
        CHECK(actual == reference);
        actual = source;
        ConvertRGBA8BufferToOutputLUT<Format>(source.size(), 1, actual.data(), actual.data(), binaryAlpha, Policy);
        CHECK(actual == reference);
        ConvertRGBA8BufferToOutputLUT<Format>(0, 0, nullptr, nullptr, binaryAlpha, Policy);
    }
}

template <int Format>
void CheckMipLookupPolicies(const std::vector<u32>& source)
{
    CheckMipLookupPacking<Format, RGB6RepackPolicy::NativeRGB5Expansion>(source);
    CheckMipLookupPacking<Format, RGB6RepackPolicy::PreserveExpandedRGB6>(source);
}

}

POLICY_TEST(MipLookupPackingMatchesAllChannelAlphaPairs)
{
    std::vector<u32> source;
    source.reserve(256 * 256 * 2);
    for (u32 alpha = 0; alpha < 256; alpha++)
        for (u32 value = 0; value < 256; value++)
            source.push_back((alpha << 24) | (((value * 73) & 255) << 16) |
                             ((255 - value) << 8) | value);
    // Deterministic mixed colours also exercise channel ordering and packing.
    u32 random = 0xBADC0DE;
    for (u32 i = 0; i < 256 * 256; i++)
    {
        random = random * 1664525u + 1013904223u;
        source.push_back(random);
    }
    CheckMipLookupPolicies<outputFmt_RGB6A5>(source);
    CheckMipLookupPolicies<outputFmt_RGBA8>(source);
    CheckMipLookupPolicies<outputFmt_BGRA8>(source);
}

POLICY_TEST(MipChainPackingPreservesPerLevelPolicyAndPreview)
{
    const std::vector<u32> source = {0x00010203, 0x7F3F7FBF, 0x8091A2B3, 0xFFFFFFFF};
    for (int format : {outputFmt_RGB6A5, outputFmt_RGBA8, outputFmt_BGRA8})
    {
        TexcacheMipChain chain;
        for (auto policy : {RGB6RepackPolicy::NativeRGB5Expansion, RGB6RepackPolicy::PreserveExpandedRGB6})
            chain.AddLevel(2, 2, policy).PreviewRGBA8 = source;
        TextureMipPackLevels(chain, format);
        for (const auto& level : chain.Levels)
        {
            std::vector<u32> reference(source.size());
            switch (format)
            {
            case outputFmt_RGB6A5:
                ConvertRGBA8BufferToOutput<outputFmt_RGB6A5>(2, 2, source.data(), reference.data(), false, level.RepackPolicy);
                break;
            case outputFmt_RGBA8:
                ConvertRGBA8BufferToOutput<outputFmt_RGBA8>(2, 2, source.data(), reference.data(), false, level.RepackPolicy);
                break;
            case outputFmt_BGRA8:
                ConvertRGBA8BufferToOutput<outputFmt_BGRA8>(2, 2, source.data(), reference.data(), false, level.RepackPolicy);
                break;
            }
            CHECK(level.Packed == reference);
            CHECK(level.PreviewRGBA8 == source);
        }
    }
}
