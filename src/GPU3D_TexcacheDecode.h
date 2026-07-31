/*
    Copyright 2016-2026 melonDS team
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

#ifndef GPU3D_TEXCACHE_DECODE_H
#define GPU3D_TEXCACHE_DECODE_H

#include "GPU3D_TextureTypes.h"
#include "types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace melonDS
{

enum
{
    outputFmt_RGB6A5,
    outputFmt_RGBA8,
    outputFmt_BGRA8
};

inline float Spline36Weight(float x)
{
    x = std::abs(x);

    if (x < 1.0f)
        return (((13.0f / 11.0f * x - 453.0f / 209.0f) * x - 3.0f / 209.0f) * x + 1.0f);
    if (x < 2.0f)
        return (((-6.0f / 11.0f * x + 612.0f / 209.0f) * x - 1038.0f / 209.0f) * x + 540.0f / 209.0f);
    if (x < 3.0f)
        return (((1.0f / 11.0f * x - 159.0f / 209.0f) * x + 434.0f / 209.0f) * x - 384.0f / 209.0f);

    return 0.0f;
}

inline void UnpackTextureColor(u32 color, float channels[4])
{
    channels[0] = static_cast<float>(color & 0xFF);
    channels[1] = static_cast<float>((color >> 8) & 0xFF);
    channels[2] = static_cast<float>((color >> 16) & 0xFF);
    channels[3] = static_cast<float>((color >> 24) & 0xFF);
}

inline void PremultiplyTextureColor(float channels[4], const u8 channelMax[4])
{
    if (channelMax[3] == 0)
        return;

    float alphaScale = channels[3] / static_cast<float>(channelMax[3]);
    channels[0] *= alphaScale;
    channels[1] *= alphaScale;
    channels[2] *= alphaScale;
}

inline void UnpremultiplyTextureColor(float channels[4], const u8 channelMax[4])
{
    if (channelMax[3] == 0)
        return;

    if (channels[3] <= 0.00001f)
    {
        channels[0] = 0.0f;
        channels[1] = 0.0f;
        channels[2] = 0.0f;
        return;
    }

    float alphaScale = channels[3] / static_cast<float>(channelMax[3]);
    if (alphaScale <= 0.00001f)
    {
        channels[0] = 0.0f;
        channels[1] = 0.0f;
        channels[2] = 0.0f;
        return;
    }

    channels[0] /= alphaScale;
    channels[1] /= alphaScale;
    channels[2] /= alphaScale;
}

inline bool TextureHasBinaryAlpha(u32 width, u32 height, const u32* src, u8 alphaMax, bool* hasTransparentAlpha = nullptr)
{
    bool transparent = false;
    size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixels; i++)
    {
        u8 alpha = static_cast<u8>((src[i] >> 24) & 0xFF);
        if (alpha == 0)
            transparent = true;
        if (alpha != 0 && alpha != alphaMax)
        {
            if (hasTransparentAlpha)
                *hasTransparentAlpha = transparent;
            return false;
        }
    }
    if (hasTransparentAlpha)
        *hasTransparentAlpha = transparent;
    return true;
}

inline void PadTransparentTextureRGBIterations(u32 width, u32 height, u32* src, u32 maxIterations)
{
    size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels == 0 || maxIterations == 0)
        return;

    std::vector<u32> current(src, src + pixels);
    std::vector<u32> next = current;

    bool changed = true;
    for (u32 iter = 0; iter < maxIterations && changed; iter++)
    {
        changed = false;

        for (u32 y = 0; y < height; y++)
        {
            for (u32 x = 0; x < width; x++)
            {
                size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                if (((current[index] >> 24) & 0xFF) != 0)
                    continue;

                u32 r = 0;
                u32 g = 0;
                u32 b = 0;
                u32 count = 0;

                for (s32 ny = std::max<s32>(0, static_cast<s32>(y) - 1);
                     ny <= std::min<s32>(static_cast<s32>(height) - 1, static_cast<s32>(y) + 1); ny++)
                {
                    for (s32 nx = std::max<s32>(0, static_cast<s32>(x) - 1);
                         nx <= std::min<s32>(static_cast<s32>(width) - 1, static_cast<s32>(x) + 1); nx++)
                    {
                        if (nx == static_cast<s32>(x) && ny == static_cast<s32>(y))
                            continue;

                        u32 neighbor = current[static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx)];
                        if (((neighbor >> 24) & 0xFF) == 0)
                            continue;

                        r += neighbor & 0xFF;
                        g += (neighbor >> 8) & 0xFF;
                        b += (neighbor >> 16) & 0xFF;
                        count++;
                    }
                }

                if (count == 0)
                    continue;

                next[index] =
                    (current[index] & 0xFF000000) |
                    ((r / count) & 0xFF) |
                    (((g / count) & 0xFF) << 8) |
                    (((b / count) & 0xFF) << 16);
                changed = true;
            }
        }

        current.swap(next);
        next = current;
    }

    std::memcpy(src, current.data(), pixels * sizeof(u32));
}

inline void PadTransparentTextureRGBFast(u32 width, u32 height, u32* src)
{
    constexpr u32 MaxPaddingIterations = 6;
    PadTransparentTextureRGBIterations(width, height, src, MaxPaddingIterations);
}

inline void PadTransparentTextureRGB(u32 width, u32 height, u32* src)
{
    PadTransparentTextureRGBIterations(width, height, src, width + height);
}

inline u32 PackTextureColor(const float channels[4], const u8 channelMax[4])
{
    u32 color = 0;
    for (int i = 0; i < 4; i++)
    {
        float value = std::clamp(channels[i], 0.0f, static_cast<float>(channelMax[i]));
        color |= static_cast<u32>(std::lround(value)) << (i * 8);
    }

    return color;
}

inline u8 QuantizeRGB8ToRGB6LikeDS(float value)
{
    value = std::clamp(value, 0.0f, 255.0f);
    int value5 = std::clamp((int)std::lround(value / 8.0f), 0, 31);
    if (value5 == 0)
        return 0;
    return (u8)(value5 * 2 + 1);
}

inline u8 QuantizeRGB8ToRGB6Roundtrip(float value)
{
    value = std::clamp(value, 0.0f, 255.0f);
    return (u8)std::clamp((int)std::lround(value * (63.0f / 255.0f)), 0, 63);
}

template <int outputFmt>
inline void ConvertRGBA8BufferToOutput(u32 width, u32 height, const u32* src, u32* dst, bool binaryAlpha,
                                       bool losslessRGB6Repack = false)
{
    u8 channelMax[4];
    if constexpr (outputFmt == outputFmt_RGB6A5)
    {
        channelMax[0] = 63;
        channelMax[1] = 63;
        channelMax[2] = 63;
        channelMax[3] = 31;
    }
    else
    {
        channelMax[0] = 255;
        channelMax[1] = 255;
        channelMax[2] = 255;
        channelMax[3] = 255;
    }

    size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixels; i++)
    {
        float channels[4];
        UnpackTextureColor(src[i], channels);

        if (binaryAlpha)
        {
            channels[3] = channels[3] >= 127.5f ? 255.0f : 0.0f;
            if (channels[3] == 0.0f)
            {
                channels[0] = 0.0f;
                channels[1] = 0.0f;
                channels[2] = 0.0f;
            }
        }

        if constexpr (outputFmt == outputFmt_RGB6A5)
        {
            if (losslessRGB6Repack)
            {
                channels[0] = (float)QuantizeRGB8ToRGB6Roundtrip(channels[0]);
                channels[1] = (float)QuantizeRGB8ToRGB6Roundtrip(channels[1]);
                channels[2] = (float)QuantizeRGB8ToRGB6Roundtrip(channels[2]);
            }
            else
            {
                channels[0] = (float)QuantizeRGB8ToRGB6LikeDS(channels[0]);
                channels[1] = (float)QuantizeRGB8ToRGB6LikeDS(channels[1]);
                channels[2] = (float)QuantizeRGB8ToRGB6LikeDS(channels[2]);
            }
            channels[3] = channels[3] * (31.0f / 255.0f);
        }
        else if constexpr (outputFmt == outputFmt_BGRA8)
        {
            std::swap(channels[0], channels[2]);
        }

        dst[i] = PackTextureColor(channels, channelMax);
    }
}

template <int outputFmt>
inline void ConvertOutputBufferToPreviewRGBA8(u32 width, u32 height, const u32* src, u32* dst)
{
    size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixels; i++)
    {
        u32 color = src[i];
        if constexpr (outputFmt == outputFmt_RGBA8)
        {
            dst[i] = color;
        }
        else if constexpr (outputFmt == outputFmt_BGRA8)
        {
            u32 r = color & 0xFF;
            u32 g = (color >> 8) & 0xFF;
            u32 b = (color >> 16) & 0xFF;
            u32 a = (color >> 24) & 0xFF;
            dst[i] = b | (g << 8) | (r << 16) | (a << 24);
        }
        else
        {
            u32 r = color & 0xFF;
            u32 g = (color >> 8) & 0xFF;
            u32 b = (color >> 16) & 0xFF;
            u32 a = (color >> 24) & 0xFF;

            r = static_cast<u32>(std::lround((static_cast<float>(r) * 255.0f) / 63.0f));
            g = static_cast<u32>(std::lround((static_cast<float>(g) * 255.0f) / 63.0f));
            b = static_cast<u32>(std::lround((static_cast<float>(b) * 255.0f) / 63.0f));
            a = static_cast<u32>(std::lround((static_cast<float>(a) * 255.0f) / 31.0f));

            dst[i] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
}

template <int outputFmt>
void UpscaleTextureSpline36(u32 srcWidth, u32 srcHeight, const u32* src, u32 scaleFactor, u32* dst)
{
    if (scaleFactor <= 1)
    {
        std::memcpy(dst, src, srcWidth * srcHeight * sizeof(u32));
        return;
    }

    u8 channelMax[4];
    if constexpr (outputFmt == outputFmt_RGB6A5)
    {
        channelMax[0] = 63;
        channelMax[1] = 63;
        channelMax[2] = 63;
        channelMax[3] = 31;
    }
    else
    {
        channelMax[0] = 255;
        channelMax[1] = 255;
        channelMax[2] = 255;
        channelMax[3] = 255;
    }

    u32 dstWidth = srcWidth * scaleFactor;
    u32 dstHeight = srcHeight * scaleFactor;
    bool binaryAlpha = TextureHasBinaryAlpha(srcWidth, srcHeight, src, channelMax[3]);

    for (u32 dstY = 0; dstY < dstHeight; dstY++)
    {
        float srcY = (static_cast<float>(dstY) + 0.5f) / static_cast<float>(scaleFactor) - 0.5f;
        s32 baseY = static_cast<s32>(std::floor(srcY));
        float fracY = srcY - static_cast<float>(baseY);

        float wy[6];
        float wySum = 0.0f;
        for (int y = 0; y < 6; y++)
        {
            wy[y] = Spline36Weight(static_cast<float>(y - 2) - fracY);
            wySum += wy[y];
        }

        for (u32 dstX = 0; dstX < dstWidth; dstX++)
        {
            float srcX = (static_cast<float>(dstX) + 0.5f) / static_cast<float>(scaleFactor) - 0.5f;
            s32 baseX = static_cast<s32>(std::floor(srcX));
            float fracX = srcX - static_cast<float>(baseX);

            float wx[6];
            float wxSum = 0.0f;
            for (int x = 0; x < 6; x++)
            {
                wx[x] = Spline36Weight(static_cast<float>(x - 2) - fracX);
                wxSum += wx[x];
            }

            float accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float minColor[4] = {
                static_cast<float>(channelMax[0]),
                static_cast<float>(channelMax[1]),
                static_cast<float>(channelMax[2]),
                static_cast<float>(channelMax[3]),
            };
            float maxColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float totalWeight = 0.0f;

            for (int y = 0; y < 6; y++)
            {
                float wyNorm = wy[y] / wySum;
                s32 sampleY = std::clamp(baseY + y - 2, 0, static_cast<s32>(srcHeight) - 1);

                for (int x = 0; x < 6; x++)
                {
                    float weight = (wx[x] / wxSum) * wyNorm;
                    s32 sampleX = std::clamp(baseX + x - 2, 0, static_cast<s32>(srcWidth) - 1);

                    float sampleColor[4];
                    UnpackTextureColor(src[sampleY * srcWidth + sampleX], sampleColor);
                    PremultiplyTextureColor(sampleColor, channelMax);
                    for (int c = 0; c < 4; c++)
                    {
                        accum[c] += sampleColor[c] * weight;
                        minColor[c] = std::min(minColor[c], sampleColor[c]);
                        maxColor[c] = std::max(maxColor[c], sampleColor[c]);
                    }
                    totalWeight += weight;
                }
            }

            float finalColor[4];
            if (totalWeight <= 0.00001f)
            {
                UnpackTextureColor(src[std::clamp(baseY, 0, static_cast<s32>(srcHeight) - 1) * srcWidth +
                                       std::clamp(baseX, 0, static_cast<s32>(srcWidth) - 1)],
                    finalColor);
            }
            else
            {
                for (int c = 0; c < 4; c++)
                    finalColor[c] = std::clamp(accum[c] / totalWeight, minColor[c], maxColor[c]);
            }

            float filteredAlpha = finalColor[3];
            UnpremultiplyTextureColor(finalColor, channelMax);
            if (binaryAlpha)
            {
                finalColor[3] = filteredAlpha >= (static_cast<float>(channelMax[3]) * 0.5f)
                    ? static_cast<float>(channelMax[3])
                    : 0.0f;
                if (finalColor[3] == 0.0f)
                {
                    finalColor[0] = 0.0f;
                    finalColor[1] = 0.0f;
                    finalColor[2] = 0.0f;
                }
            }

            dst[dstY * dstWidth + dstX] = PackTextureColor(finalColor, channelMax);
        }
    }
}

template <int outputFmt>
void ConvertBitmapTexture(u32 width, u32 height, u32* output, u32 addr, const TextureVRAMView& vram);
template <int outputFmt>
void ConvertCompressedTexture(u32 width, u32 height, u32* output, u32 addr, u32 addrAux, u32 palAddr, const TextureVRAMView& vram);
template <int outputFmt, int X, int Y>
void ConvertAXIYTexture(u32 width, u32 height, u32* output, u32 addr, u32 palAddr, const TextureVRAMView& vram);
template <int outputFmt, int colorBits>
void ConvertNColorsTexture(u32 width, u32 height, u32* output, u32 addr, u32 palAddr, bool color0Transparent, const TextureVRAMView& vram);

}

#endif
