// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GPU3D_TexcacheMip.h"

using namespace melonDS;

POLICY_TEST(TexturePaddingUsesOnlyOriginalNontransparentNeighbors)
{
    // Partial-alpha neighbors contribute equally, as in the original padding.
    // Transparent RGB must not propagate into the next transparent pixel.
    const std::vector<u32> source = {0xFF0000FF, 0x00010203, 0x8000FF00,
                                     0x00040506, 0x00070809};
    const std::vector<u32> expected = {0xFF0000FF, 0x00007F7F, 0x8000FF00,
                                       0x0000FF00, 0x00070809};
    for (u32 iterations : {1u, 6u, 32u})
    {
        auto horizontal = source;
        PadTransparentTextureRGBIterations(5, 1, horizontal.data(), iterations);
        CHECK(horizontal == expected);
        auto vertical = source;
        PadTransparentTextureRGBIterations(1, 5, vertical.data(), iterations);
        CHECK(vertical == expected);
    }
    auto unchanged = source;
    PadTransparentTextureRGBIterations(5, 1, unchanged.data(), 0);
    CHECK(unchanged == source);
    PadTransparentTextureRGBIterations(0, 0, nullptr, 6);
}

POLICY_TEST(TexturePaddingIncludesDiagonalNeighborsAtSheetEdges)
{
    std::vector<u32> pixels = {0, 0x00010203, 0x00040506, 0x010C1824};
    PadTransparentTextureRGBFast(2, 2, pixels.data());
    CHECK_EQ(pixels[0], 0x000C1824u);
    CHECK_EQ(pixels[1], 0x000C1824u);
    CHECK_EQ(pixels[2], 0x000C1824u);
    CHECK_EQ(pixels[3], 0x010C1824u);
}

POLICY_TEST(TextureMipHistogramMatchesPixelCoverageAtEverySearchScale)
{
    std::vector<u32> pixels;
    std::array<size_t, 256> histogram = {};
    // Unequal counts exercise the weighted histogram, including alpha 0/255.
    for (u32 a = 0; a < 256; a++)
    {
        histogram[a] = 1 + a % 7;
        pixels.insert(pixels.end(), histogram[a], a << 24);
    }
    // Every scale reachable by the ten-step [0,16] binary search.
    for (int step = 0; step <= 1024; step++)
    {
        const float scale = step / 64.0f;
        size_t covered = 0;
        for (u32 color : pixels)
            if (std::min(255.0f, static_cast<float>(color >> 24) * scale) >= 128.0f)
                covered++;
        const float expected = static_cast<float>(covered) / static_cast<float>(pixels.size());
        CHECK_EQ(TextureMipScaledCoverage(histogram, pixels.size(), scale), expected);
    }
    CHECK_EQ(TextureMipScaledCoverage({}, 0, 1.0f), 0.0f);
}

POLICY_TEST(TextureMipRetainsUnroundedAlphaForFinalCoverageThreshold)
{
    // Keep the distinction between the rounded alpha used for coverage search
    // and the original average used when applying its result.
    std::vector<u32> src = {0xFF0000FF, 0, 0, 0};
    std::vector<u32> dst;
    TextureMipBuildBinaryAlphaLevel(2, 2, src, nullptr, 1, 1, dst, nullptr, false, false);
    CHECK_EQ(dst.size(), 1u);
    CHECK_EQ(dst[0], 0x000000FFu);
}
