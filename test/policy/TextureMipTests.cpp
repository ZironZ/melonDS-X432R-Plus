// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GPU3D_TexcacheMip.h"

#include <cstring>
#include <limits>

using namespace melonDS;

POLICY_TEST(TextureMipChannelRoundingMatchesHalfwayBoundaries)
{
    for (int halfSteps = 0; halfSteps <= 510; halfSteps++)
    {
        const float value = halfSteps * 0.5f;
        for (float sample : {value,
                             std::nextafter(value, -std::numeric_limits<float>::infinity()),
                             std::nextafter(value, std::numeric_limits<float>::infinity())})
        {
            if (sample >= 0.0f && sample <= 255.0f)
                CHECK_EQ(TextureMipRoundChannel(sample), std::lround(sample));
        }
    }
    CHECK_EQ(TextureMipRoundChannel(-0.0f), 0u);
}

POLICY_TEST(TextureMipChannelRoundingMatchesFiniteFloatSamples)
{
    // Fine-spaced samples exercise every channel range, while deterministic
    // float bit patterns include small magnitudes and subnormal values.
    for (u32 value = 0; value <= 255 * 256; value++)
    {
        const float sample = value / 256.0f;
        CHECK_EQ(TextureMipRoundChannel(sample), std::lround(sample));
    }
    u32 random = 0xBADC0DE;
    for (u32 i = 0; i < 65536; i++)
    {
        random = random * 1664525u + 1013904223u;
        const u32 bits = random & 0x7FFFFFFFu;
        float sample;
        std::memcpy(&sample, &bits, sizeof(sample));
        if (std::isfinite(sample) && sample <= 255.0f)
            CHECK_EQ(TextureMipRoundChannel(sample), std::lround(sample));
    }
}

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

POLICY_TEST(TexturePaddingMatchesBoundedNeighborhoodReference)
{
    u32 random = 0xBADC0DE;
    for (u32 width : {1u, 2u, 3u, 7u, 16u})
    for (u32 height : {1u, 2u, 3u, 7u, 16u})
    for (u32 pattern = 0; pattern < 8; pattern++)
    {
        std::vector<u32> source(width * height);
        for (u32& color : source)
        {
            random = random * 1664525u + 1013904223u;
            color = random;
            if (pattern == 0 || (pattern > 1 && (random & 3) == 0))
                color &= 0x00FFFFFF;
        }
        auto expected = source;
        // Read only the original source, so traversal order cannot hide
        // accidental propagation through already-padded transparent pixels.
        for (u32 y = 0; y < height; y++)
        for (u32 x = 0; x < width; x++)
        {
            if (source[y * width + x] >> 24)
                continue;
            u32 sum[3] = {}, count = 0;
            for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                int nx = static_cast<int>(x) + dx;
                int ny = static_cast<int>(y) + dy;
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height))
                    continue;
                u32 color = source[ny * width + nx];
                if (!(color >> 24))
                    continue;
                for (u32 channel = 0; channel < 3; channel++)
                    sum[channel] += (color >> (channel * 8)) & 255;
                count++;
            }
            if (count)
                expected[y * width + x] = (sum[0] / count) |
                    ((sum[1] / count) << 8) | ((sum[2] / count) << 16);
        }
        for (u32 iterations : {1u, 6u})
        {
            auto actual = source;
            PadTransparentTextureRGBIterations(width, height, actual.data(), iterations);
            CHECK(actual == expected);
        }
    }
}

POLICY_TEST(TextureMipWeightedRGBMatchesFloatReference)
{
    u32 random = 0xBADC0DE;
    for (u32 alpha = 0; alpha < 256; alpha++)
    for (u32 pattern = 0; pattern < 32; pattern++)
    {
        std::vector<u32> source(4);
        float weighted[3] = {}, fallback[3] = {}, weight = 0.0f;
        for (u32 i = 0; i < 4; i++)
        {
            random = random * 1664525u + 1013904223u;
            const u32 a = pattern == 0 ? 0 : (alpha + i * pattern) & 255;
            source[i] = (random & 0x00FFFFFF) | (a << 24);
            weight += static_cast<float>(a);
            for (u32 channel = 0; channel < 3; channel++)
            {
                float value = static_cast<float>((source[i] >> (channel * 8)) & 255);
                fallback[channel] += value;
                weighted[channel] += value * static_cast<float>(a);
            }
        }
        u32 expectedRGB = 0;
        for (u32 channel = 0; channel < 3; channel++)
            expectedRGB |= static_cast<u32>(std::lround(weight > 0.0f ?
                weighted[channel] / weight : fallback[channel] / 4.0f)) << (channel * 8);
        std::vector<u32> actual;
        TextureMipBuildBinaryAlphaLevel(2, 2, source, nullptr, 1, 1, actual, nullptr, false, false);
        CHECK_EQ(actual[0] & 0x00FFFFFF, expectedRGB);
    }
}
