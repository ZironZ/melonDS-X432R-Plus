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

#ifndef GPU3D_TEXCACHE_MIP_H
#define GPU3D_TEXCACHE_MIP_H

#include "GPU3D_TexcacheDecode.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace melonDS
{

struct TexcacheMipLevel
{
    TexcacheMipLevel(u32 width, u32 height, RGB6RepackPolicy repackPolicy)
        : Width(width), Height(height), RepackPolicy(repackPolicy)
    {}

    std::vector<u32> PreviewRGBA8;
    std::vector<u32> Packed;
    u32 Width;
    u32 Height;
    RGB6RepackPolicy RepackPolicy;
};

struct TexcacheMipChain
{
    std::vector<TexcacheMipLevel> Levels;

    TexcacheMipLevel& AddLevel(u32 width, u32 height, RGB6RepackPolicy repackPolicy)
    {
        Levels.emplace_back(width, height, repackPolicy);
        return Levels.back();
    }

    void Clear()
    {
        Levels.clear();
    }
};

struct TexcacheMipIslandStats
{
    size_t OpaqueTexelCount = 0;
    size_t LargestIslandTexelCount = 0;
    int IslandCount = 0;
};

inline TexcacheMipIslandStats TextureMipAnalyzeAlphaIslands(u32 width, u32 height, const u32* level)
{
    TexcacheMipIslandStats stats = {};
    std::vector<int> labels(static_cast<size_t>(width) * static_cast<size_t>(height), -1);
    std::vector<size_t> stack;
    int nextLabel = 0;

    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (((level[index] >> 24) & 0xFF) == 0 || labels[index] >= 0)
                continue;

            labels[index] = nextLabel;
            stack.push_back(index);
            size_t islandSize = 0;

            while (!stack.empty())
            {
                size_t currentIndex = stack.back();
                stack.pop_back();
                islandSize++;

                u32 cx = static_cast<u32>(currentIndex % width);
                u32 cy = static_cast<u32>(currentIndex / width);
                for (s32 ny = std::max<s32>(0, static_cast<s32>(cy) - 1);
                     ny <= std::min<s32>(static_cast<s32>(height) - 1, static_cast<s32>(cy) + 1); ny++)
                {
                    for (s32 nx = std::max<s32>(0, static_cast<s32>(cx) - 1);
                         nx <= std::min<s32>(static_cast<s32>(width) - 1, static_cast<s32>(cx) + 1); nx++)
                    {
                        size_t neighborIndex =
                            static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                        if (((level[neighborIndex] >> 24) & 0xFF) == 0 || labels[neighborIndex] >= 0)
                            continue;

                        labels[neighborIndex] = nextLabel;
                        stack.push_back(neighborIndex);
                    }
                }
            }

            stats.OpaqueTexelCount += islandSize;
            stats.LargestIslandTexelCount = std::max(stats.LargestIslandTexelCount, islandSize);
            nextLabel++;
        }
    }

    stats.IslandCount = nextLabel;
    return stats;
}

inline bool TextureMipShouldUseConservativeAtlasFallback(u32 width, u32 height, const u32* level)
{
    if (width == 0 || height == 0)
        return true;

    TexcacheMipIslandStats stats = TextureMipAnalyzeAlphaIslands(width, height, level);
    if (stats.OpaqueTexelCount == 0)
        return true;

    float dominantIslandFraction =
        static_cast<float>(stats.LargestIslandTexelCount) / static_cast<float>(stats.OpaqueTexelCount);
    float aspectRatio =
        static_cast<float>(std::max(width, height)) /
        static_cast<float>(std::max<u32>(1, std::min(width, height)));

    if (stats.IslandCount >= 8)
        return true;
    if (dominantIslandFraction < 0.60f)
        return true;
    if (aspectRatio >= 4.0f && stats.IslandCount > 1)
        return true;
    if (stats.IslandCount >= 4 && dominantIslandFraction < 0.80f)
        return true;

    return false;
}

inline void TextureMipPadTransparentRGB(u32 width, u32 height, u32* data, bool quality)
{
    if (quality)
        PadTransparentTextureRGB(width, height, data);
    else
        PadTransparentTextureRGBFast(width, height, data);
}

inline void TextureMipApplyBinaryAlphaCutout(std::vector<u32>& level)
{
    for (u32& color : level)
    {
        if ((color >> 24) >= 128)
            color |= 0xFF000000;
        else
            color = 0;
    }
}

inline void TextureMipPackLevels(TexcacheMipChain& chain, int outputFmt)
{
    for (auto& level : chain.Levels)
    {
        level.Packed.resize(static_cast<size_t>(level.Width) * static_cast<size_t>(level.Height));
        switch (outputFmt)
        {
        case outputFmt_RGB6A5:
            ConvertRGBA8BufferToOutput<outputFmt_RGB6A5>(level.Width, level.Height,
                                                         level.PreviewRGBA8.data(), level.Packed.data(),
                                                         false, level.RepackPolicy);
            break;
        case outputFmt_RGBA8:
            ConvertRGBA8BufferToOutput<outputFmt_RGBA8>(level.Width, level.Height,
                                                        level.PreviewRGBA8.data(), level.Packed.data(), false,
                                                        level.RepackPolicy);
            break;
        case outputFmt_BGRA8:
            ConvertRGBA8BufferToOutput<outputFmt_BGRA8>(level.Width, level.Height,
                                                        level.PreviewRGBA8.data(), level.Packed.data(), false,
                                                        level.RepackPolicy);
            break;
        }
    }
}

template <typename TexLoaderT>
bool TextureMipBuildSourceScaledChain(TexcacheMipChain& chain,
                                      TexLoaderT& texLoader,
                                      u32 width,
                                      u32 height,
                                      u32 scaledWidth,
                                      u32 scaledHeight,
                                      u32 effectiveScaleFactor,
                                      const u32* mipRGBA8,
                                      RGB6RepackPolicy mipRepackPolicy,
                                      const u32* sourceMipNativeRGBA8,
                                      RGB6RepackPolicy sourceMipNativeRepackPolicy,
                                      bool sourceScaledMipLevels,
                                      bool sourceBinaryAlphaTextureKnown,
                                      bool sourceBinaryAlphaTextureValue,
                                      int outputFmt)
{
    if (!sourceScaledMipLevels || sourceMipNativeRGBA8 == nullptr)
        return false;

    chain.Clear();
    auto& topLevel = chain.AddLevel(scaledWidth, scaledHeight, mipRepackPolicy);
    topLevel.PreviewRGBA8.assign(
        mipRGBA8,
        mipRGBA8 + (static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight)));

    if (sourceBinaryAlphaTextureKnown && sourceBinaryAlphaTextureValue && texLoader.UseFilterableMipAlphaHandling())
        TextureMipPadTransparentRGB(scaledWidth, scaledHeight, chain.Levels.back().PreviewRGBA8.data(),
                                    texLoader.UseQualityAlphaHandling());

    std::vector<u32> scaledMipRGBA8;
    for (u32 levelScale = effectiveScaleFactor >> 1; levelScale > 0; levelScale >>= 1)
    {
        const u32 nextWidth = width * levelScale;
        const u32 nextHeight = height * levelScale;
        const u32 minMipDimension = texLoader.FilterableMipMinDimension();
        if (minMipDimension > 1 && (nextWidth < minMipDimension || nextHeight < minMipDimension))
            break;

        if (levelScale > 1)
        {
            if (!texLoader.ProcessTextureGPUScale(width, height, levelScale, sourceMipNativeRGBA8, scaledMipRGBA8))
                return false;

            auto& level = chain.AddLevel(nextWidth, nextHeight, sourceMipNativeRepackPolicy);
            level.PreviewRGBA8 = scaledMipRGBA8;
        }
        else
        {
            auto& level = chain.AddLevel(width, height, sourceMipNativeRepackPolicy);
            level.PreviewRGBA8.assign(
                sourceMipNativeRGBA8,
                sourceMipNativeRGBA8 + (static_cast<size_t>(width) * static_cast<size_t>(height)));
        }

        if (sourceBinaryAlphaTextureKnown && sourceBinaryAlphaTextureValue)
        {
            TextureMipApplyBinaryAlphaCutout(chain.Levels.back().PreviewRGBA8);

            if (texLoader.UseFilterableMipAlphaHandling())
                TextureMipPadTransparentRGB(chain.Levels.back().Width, chain.Levels.back().Height,
                                            chain.Levels.back().PreviewRGBA8.data(),
                                            texLoader.UseQualityAlphaHandling());
        }

        if (levelScale == 1)
            break;
    }

    TextureMipPackLevels(chain, outputFmt);
    return true;
}

inline float TextureMipAlphaCoverage(const std::vector<u32>& level)
{
    if (level.empty())
        return 0.0f;

    size_t covered = 0;
    for (u32 color : level)
    {
        if (((color >> 24) & 0xFF) >= 128)
            covered++;
    }
    return static_cast<float>(covered) / static_cast<float>(level.size());
}

inline float TextureMipScaledCoverage(const std::vector<u32>& level, float scale)
{
    if (level.empty())
        return 0.0f;

    size_t covered = 0;
    for (u32 color : level)
    {
        float alpha = std::min(255.0f, static_cast<float>((color >> 24) & 0xFF) * scale);
        if (alpha >= 128.0f)
            covered++;
    }
    return static_cast<float>(covered) / static_cast<float>(level.size());
}

inline void TextureMipBuildAlphaIslands(u32 width, u32 height, const std::vector<u32>& level, std::vector<int>& labels)
{
    labels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), -1);
    int nextLabel = 0;
    std::vector<size_t> stack;

    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (((level[index] >> 24) & 0xFF) == 0 || labels[index] >= 0)
                continue;

            labels[index] = nextLabel;
            stack.push_back(index);

            while (!stack.empty())
            {
                size_t currentIndex = stack.back();
                stack.pop_back();
                u32 cx = static_cast<u32>(currentIndex % width);
                u32 cy = static_cast<u32>(currentIndex / width);

                for (s32 ny = std::max<s32>(0, static_cast<s32>(cy) - 1);
                     ny <= std::min<s32>(static_cast<s32>(height) - 1, static_cast<s32>(cy) + 1); ny++)
                {
                    for (s32 nx = std::max<s32>(0, static_cast<s32>(cx) - 1);
                         nx <= std::min<s32>(static_cast<s32>(width) - 1, static_cast<s32>(cx) + 1); nx++)
                    {
                        size_t neighborIndex =
                            static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                        if (((level[neighborIndex] >> 24) & 0xFF) == 0 || labels[neighborIndex] >= 0)
                            continue;

                        labels[neighborIndex] = nextLabel;
                        stack.push_back(neighborIndex);
                    }
                }
            }

            nextLabel++;
        }
    }
}

inline void TextureMipApplyIslandAwareFringe(u32 width,
                                             u32 height,
                                             std::vector<u32>& level,
                                             const std::vector<int>& labels)
{
    std::vector<u32> updated = level;
    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (((level[index] >> 24) & 0xFF) != 0)
                continue;

            int labelIds[8];
            float labelWeights[8] = {};
            for (int i = 0; i < 8; i++)
                labelIds[i] = -1;

            for (s32 ny = std::max<s32>(0, static_cast<s32>(y) - 1);
                 ny <= std::min<s32>(static_cast<s32>(height) - 1, static_cast<s32>(y) + 1); ny++)
            {
                for (s32 nx = std::max<s32>(0, static_cast<s32>(x) - 1);
                     nx <= std::min<s32>(static_cast<s32>(width) - 1, static_cast<s32>(x) + 1); nx++)
                {
                    if (nx == static_cast<s32>(x) && ny == static_cast<s32>(y))
                        continue;

                    size_t neighborIndex = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                    if (((level[neighborIndex] >> 24) & 0xFF) == 0)
                        continue;

                    int label = labels[neighborIndex];
                    if (label < 0)
                        continue;

                    int slot = -1;
                    for (int i = 0; i < 8; i++)
                    {
                        if (labelIds[i] == label)
                        {
                            slot = i;
                            break;
                        }
                        if (labelIds[i] < 0 && slot < 0)
                            slot = i;
                    }
                    if (slot >= 0)
                    {
                        if (labelIds[slot] < 0)
                            labelIds[slot] = label;
                        labelWeights[slot] += 1.0f;
                    }
                }
            }

            int dominantLabel = -1;
            float dominantWeight = 0.0f;
            for (int i = 0; i < 8; i++)
            {
                if (labelIds[i] >= 0 && labelWeights[i] > dominantWeight)
                {
                    dominantLabel = labelIds[i];
                    dominantWeight = labelWeights[i];
                }
            }
            if (dominantLabel < 0)
                continue;

            float accumR = 0.0f;
            float accumG = 0.0f;
            float accumB = 0.0f;
            float count = 0.0f;
            for (s32 ny = std::max<s32>(0, static_cast<s32>(y) - 1);
                 ny <= std::min<s32>(static_cast<s32>(height) - 1, static_cast<s32>(y) + 1); ny++)
            {
                for (s32 nx = std::max<s32>(0, static_cast<s32>(x) - 1);
                     nx <= std::min<s32>(static_cast<s32>(width) - 1, static_cast<s32>(x) + 1); nx++)
                {
                    if (nx == static_cast<s32>(x) && ny == static_cast<s32>(y))
                        continue;

                    size_t neighborIndex = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                    if (((level[neighborIndex] >> 24) & 0xFF) == 0 || labels[neighborIndex] != dominantLabel)
                        continue;

                    u32 neighbor = level[neighborIndex];
                    accumR += static_cast<float>(neighbor & 0xFF);
                    accumG += static_cast<float>((neighbor >> 8) & 0xFF);
                    accumB += static_cast<float>((neighbor >> 16) & 0xFF);
                    count += 1.0f;
                }
            }
            if (count <= 0.0f)
                continue;

            updated[index] =
                (updated[index] & 0xFF000000) |
                (static_cast<u32>(std::clamp(std::lround(accumR / count), 0l, 255l)) & 0xFF) |
                ((static_cast<u32>(std::clamp(std::lround(accumG / count), 0l, 255l)) & 0xFF) << 8) |
                ((static_cast<u32>(std::clamp(std::lround(accumB / count), 0l, 255l)) & 0xFF) << 16);
        }
    }

    level.swap(updated);
}

inline void TextureMipBuildBinaryAlphaLevel(u32 srcWidth,
                                            u32 srcHeight,
                                            const std::vector<u32>& srcLevel,
                                            const std::vector<int>* srcLabels,
                                            u32 dstWidth,
                                            u32 dstHeight,
                                            std::vector<u32>& dstLevel,
                                            std::vector<int>* dstLabels,
                                            bool useImprovedMipColors,
                                            bool qualityAlphaHandling)
{
    dstLevel.resize(static_cast<size_t>(dstWidth) * static_cast<size_t>(dstHeight));
    std::vector<float> avgAlpha(dstLevel.size());
    std::vector<int> dominantLabels(dstLevel.size(), -1);
    if (dstLabels)
        dstLabels->assign(dstLevel.size(), -1);

    for (u32 y = 0; y < dstHeight; y++)
    {
        for (u32 x = 0; x < dstWidth; x++)
        {
            float accumAlpha = 0.0f;
            float accumR = 0.0f;
            float accumG = 0.0f;
            float accumB = 0.0f;
            float fallbackR = 0.0f;
            float fallbackG = 0.0f;
            float fallbackB = 0.0f;
            u32 sampleCount = 0;
            int labelIds[4];
            float labelWeights[4] = {};
            float labelR[4] = {};
            float labelG[4] = {};
            float labelB[4] = {};
            float labelCount[4] = {};
            for (int i = 0; i < 4; i++)
                labelIds[i] = -1;

            for (u32 oy = 0; oy < 2; oy++)
            {
                u32 sy = std::min(srcHeight - 1, y * 2 + oy);
                for (u32 ox = 0; ox < 2; ox++)
                {
                    u32 sx = std::min(srcWidth - 1, x * 2 + ox);
                    size_t srcIndex = static_cast<size_t>(sy) * static_cast<size_t>(srcWidth) + static_cast<size_t>(sx);
                    u32 color = srcLevel[srcIndex];
                    float a = static_cast<float>((color >> 24) & 0xFF);
                    float r = static_cast<float>(color & 0xFF);
                    float g = static_cast<float>((color >> 8) & 0xFF);
                    float b = static_cast<float>((color >> 16) & 0xFF);
                    accumAlpha += a;
                    if (useImprovedMipColors)
                    {
                        if (a > 0.0f && srcLabels)
                        {
                            int label = (*srcLabels)[srcIndex];
                            if (label >= 0)
                            {
                                int slot = -1;
                                for (int i = 0; i < 4; i++)
                                {
                                    if (labelIds[i] == label)
                                    {
                                        slot = i;
                                        break;
                                    }
                                    if (labelIds[i] < 0 && slot < 0)
                                        slot = i;
                                }
                                if (slot >= 0)
                                {
                                    if (labelIds[slot] < 0)
                                        labelIds[slot] = label;
                                    labelWeights[slot] += a;
                                    labelR[slot] += r;
                                    labelG[slot] += g;
                                    labelB[slot] += b;
                                    labelCount[slot] += 1.0f;
                                }
                            }
                        }
                    }
                    else
                    {
                        fallbackR += r;
                        fallbackG += g;
                        fallbackB += b;
                        if (a > 0.0f)
                        {
                            accumR += r * a;
                            accumG += g * a;
                            accumB += b * a;
                        }
                    }
                    sampleCount++;
                }
            }

            float outR = 0.0f;
            float outG = 0.0f;
            float outB = 0.0f;
            int dominantLabel = -1;
            if (useImprovedMipColors)
            {
                int dominantSlot = -1;
                float dominantWeight = 0.0f;
                for (int i = 0; i < 4; i++)
                {
                    if (labelIds[i] >= 0 && labelWeights[i] > dominantWeight)
                    {
                        dominantSlot = i;
                        dominantWeight = labelWeights[i];
                    }
                }
                if (dominantSlot >= 0 && labelCount[dominantSlot] > 0.0f)
                {
                    dominantLabel = labelIds[dominantSlot];
                    outR = labelR[dominantSlot] / labelCount[dominantSlot];
                    outG = labelG[dominantSlot] / labelCount[dominantSlot];
                    outB = labelB[dominantSlot] / labelCount[dominantSlot];
                }
            }
            else
            {
                float accumWeight = 0.0f;
                for (u32 oy = 0; oy < 2; oy++)
                {
                    u32 sy = std::min(srcHeight - 1, y * 2 + oy);
                    for (u32 ox = 0; ox < 2; ox++)
                    {
                        u32 sx = std::min(srcWidth - 1, x * 2 + ox);
                        u32 color = srcLevel[static_cast<size_t>(sy) * static_cast<size_t>(srcWidth) + static_cast<size_t>(sx)];
                        float a = static_cast<float>((color >> 24) & 0xFF);
                        if (a > 0.0f)
                            accumWeight += a;
                    }
                }
                if (accumWeight > 0.0f)
                {
                    outR = accumR / accumWeight;
                    outG = accumG / accumWeight;
                    outB = accumB / accumWeight;
                }
                else if (sampleCount > 0)
                {
                    outR = fallbackR / static_cast<float>(sampleCount);
                    outG = fallbackG / static_cast<float>(sampleCount);
                    outB = fallbackB / static_cast<float>(sampleCount);
                }
            }

            size_t dstIndex = static_cast<size_t>(y) * static_cast<size_t>(dstWidth) + static_cast<size_t>(x);
            avgAlpha[dstIndex] = std::clamp(accumAlpha / static_cast<float>(sampleCount), 0.0f, 255.0f);
            dominantLabels[dstIndex] = dominantLabel;
            dstLevel[dstIndex] =
                (static_cast<u32>(std::clamp(std::lround(avgAlpha[dstIndex]), 0l, 255l)) << 24) |
                (static_cast<u32>(std::clamp(std::lround(outR), 0l, 255l)) & 0xFF) |
                ((static_cast<u32>(std::clamp(std::lround(outG), 0l, 255l)) & 0xFF) << 8) |
                ((static_cast<u32>(std::clamp(std::lround(outB), 0l, 255l)) & 0xFF) << 16);
        }
    }

    float targetCoverage = TextureMipAlphaCoverage(srcLevel);
    float low = 0.0f;
    float high = 16.0f;
    for (int iter = 0; iter < 10; iter++)
    {
        float mid = (low + high) * 0.5f;
        if (TextureMipScaledCoverage(dstLevel, mid) < targetCoverage)
            low = mid;
        else
            high = mid;
    }
    float coverageScale = high;

    for (size_t i = 0; i < dstLevel.size(); i++)
    {
        u32 color = dstLevel[i];
        float alpha = std::min(255.0f, avgAlpha[i] * coverageScale);
        if (useImprovedMipColors)
        {
            alpha = (alpha >= 128.0f) ? 255.0f : 0.0f;
            if (alpha > 0.0f && dstLabels && i < dstLabels->size())
                (*dstLabels)[i] = dominantLabels[i];
            else if (dstLabels && i < dstLabels->size())
                (*dstLabels)[i] = -1;
            dstLevel[i] = (color & 0x00FFFFFF) | (static_cast<u32>(alpha) << 24);
            if (alpha == 0.0f)
                dstLevel[i] &= 0xFF000000;
        }
        else
        {
            alpha = (alpha >= 128.0f) ? 255.0f : 0.0f;
            dstLevel[i] = (color & 0x00FFFFFF) | (static_cast<u32>(alpha) << 24);
        }
    }

    if (useImprovedMipColors)
    {
        if (dstLabels)
            TextureMipApplyIslandAwareFringe(dstWidth, dstHeight, dstLevel, *dstLabels);
    }
    else
    {
        TextureMipPadTransparentRGB(dstWidth, dstHeight, dstLevel.data(), qualityAlphaHandling);
    }
}

template <typename TexLoaderT>
void TextureMipBuildAlphaAwareChain(TexcacheMipChain& chain,
                                    TexLoaderT& texLoader,
                                    u32 scaledWidth,
                                    u32 scaledHeight,
                                    u32 effectiveScaleFactor,
                                    const u32* mipRGBA8,
                                    RGB6RepackPolicy repackPolicy,
                                    int outputFmt,
                                    bool useImprovedMipColors)
{
    chain.Clear();
    std::vector<std::vector<int>> islandLabels;

    auto& topLevel = chain.AddLevel(scaledWidth, scaledHeight, repackPolicy);
    topLevel.PreviewRGBA8.assign(
        mipRGBA8,
        mipRGBA8 + (static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight)));
    if (useImprovedMipColors)
    {
        islandLabels.emplace_back();
        TextureMipBuildAlphaIslands(scaledWidth, scaledHeight, chain.Levels.back().PreviewRGBA8, islandLabels.back());
        TextureMipApplyIslandAwareFringe(scaledWidth, scaledHeight, chain.Levels.back().PreviewRGBA8, islandLabels.back());
    }

    size_t mipLevelLimit = static_cast<size_t>(-1);
    if (texLoader.UseNativeMipFloor() && effectiveScaleFactor > 1)
    {
        mipLevelLimit = 1;
        u32 levelScale = effectiveScaleFactor;
        while (levelScale > 1)
        {
            levelScale >>= 1;
            mipLevelLimit++;
        }
    }
    while ((chain.Levels.back().Width > 1 || chain.Levels.back().Height > 1) &&
           chain.Levels.size() < mipLevelLimit)
    {
        u32 nextWidth = std::max<u32>(1, chain.Levels.back().Width >> 1);
        u32 nextHeight = std::max<u32>(1, chain.Levels.back().Height >> 1);
        const u32 minMipDimension = texLoader.FilterableMipMinDimension();
        if (minMipDimension > 1 && (nextWidth < minMipDimension || nextHeight < minMipDimension))
            break;

        auto& nextLevel = chain.AddLevel(nextWidth, nextHeight, repackPolicy);
        if (useImprovedMipColors)
            islandLabels.emplace_back();
        TextureMipBuildBinaryAlphaLevel(
            chain.Levels[chain.Levels.size() - 2].Width,
            chain.Levels[chain.Levels.size() - 2].Height,
            chain.Levels[chain.Levels.size() - 2].PreviewRGBA8,
            useImprovedMipColors ? &islandLabels[islandLabels.size() - 2] : nullptr,
            nextWidth,
            nextHeight,
            nextLevel.PreviewRGBA8,
            useImprovedMipColors ? &islandLabels.back() : nullptr,
            useImprovedMipColors,
            texLoader.UseQualityAlphaHandling());
    }

    TextureMipPackLevels(chain, outputFmt);
}

}

#endif
