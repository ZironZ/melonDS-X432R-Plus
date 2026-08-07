/*
    Copyright 2026 ZironZ

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

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
