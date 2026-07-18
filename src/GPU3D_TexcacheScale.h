#ifndef GPU3D_TEXCACHE_SCALE_H
#define GPU3D_TEXCACHE_SCALE_H

#include "GPU3D_TexcacheDecode.h"
#include "RendererSettings.h"

#include "xbrz/xbrz.h"

#include <algorithm>
#include <vector>

namespace melonDS
{

inline bool TextureScaleUsesGPUAlgorithm(RendererSettings::GLScaleAlgorithm algorithm)
{
    return algorithm == RendererSettings::GLScaleAlgorithm::Spline36 ||
        algorithm == RendererSettings::GLScaleAlgorithm::XBRZ ||
        RendererSettings::IsGLArtCNNAlgorithm(algorithm) ||
        RendererSettings::IsGLNNEDI3Algorithm(algorithm) ||
        RendererSettings::IsGLCuNNyAlgorithm(algorithm);
}

inline void TextureScaleDecodeSourceRGBA8(u32 fmt,
                                          u32 width,
                                          u32 height,
                                          u32* rgbaBuffer,
                                          u32 addr,
                                          u32 slot1Addr,
                                          u32 texPalStart,
                                          bool color0Transparent,
                                          const TextureVRAMView& vram)
{
    switch (fmt)
    {
    case 7:
        ConvertBitmapTexture<outputFmt_RGBA8>(width, height, rgbaBuffer, addr, vram);
        break;
    case 5:
        ConvertCompressedTexture<outputFmt_RGBA8>(width, height, rgbaBuffer, addr, slot1Addr, texPalStart, vram);
        break;
    case 1:
        ConvertAXIYTexture<outputFmt_RGBA8, 3, 5>(width, height, rgbaBuffer, addr, texPalStart, vram);
        break;
    case 6:
        ConvertAXIYTexture<outputFmt_RGBA8, 5, 3>(width, height, rgbaBuffer, addr, texPalStart, vram);
        break;
    case 2:
        ConvertNColorsTexture<outputFmt_RGBA8, 2>(width, height, rgbaBuffer, addr, texPalStart, color0Transparent, vram);
        break;
    case 3:
        ConvertNColorsTexture<outputFmt_RGBA8, 4>(width, height, rgbaBuffer, addr, texPalStart, color0Transparent, vram);
        break;
    case 4:
        ConvertNColorsTexture<outputFmt_RGBA8, 8>(width, height, rgbaBuffer, addr, texPalStart, color0Transparent, vram);
        break;
    default:
        break;
    }
}

inline void TextureScalePackRGBA8ToOutput(int outputFmt,
                                          u32 width,
                                          u32 height,
                                          const u32* srcRGBA8,
                                          u32* dst,
                                          bool binaryAlpha)
{
    switch (outputFmt)
    {
    case outputFmt_RGB6A5:
        ConvertRGBA8BufferToOutput<outputFmt_RGB6A5>(width, height, srcRGBA8, dst, binaryAlpha);
        break;
    case outputFmt_RGBA8:
        ConvertRGBA8BufferToOutput<outputFmt_RGBA8>(width, height, srcRGBA8, dst, binaryAlpha);
        break;
    case outputFmt_BGRA8:
        ConvertRGBA8BufferToOutput<outputFmt_BGRA8>(width, height, srcRGBA8, dst, binaryAlpha);
        break;
    }
}

inline bool TextureScaleRunXBRZ(u32 width,
                                u32 height,
                                u32 scaleFactor,
                                const u32* sourceRGBA8,
                                std::vector<u32>& outputRGBA8)
{
    if (scaleFactor < 2 || scaleFactor > 6)
        return false;

    const u32 scaledWidth = width * scaleFactor;
    const u32 scaledHeight = height * scaleFactor;
    outputRGBA8.resize(static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight));
    xbrz::ScalerCfg xbrzConfig;
    xbrz::scale(
        scaleFactor,
        sourceRGBA8,
        outputRGBA8.data(),
        static_cast<int>(width),
        static_cast<int>(height),
        xbrz::ColorFormat::ARGB,
        xbrzConfig);
    return true;
}

inline void TextureScaleUpscaleNearest(u32 srcWidth, u32 srcHeight, const u32* src, u32 scaleFactor, u32* dst)
{
    for (u32 y = 0; y < srcHeight; y++)
    {
        const u32* srcRow = &src[y * srcWidth];
        for (u32 sy = 0; sy < scaleFactor; sy++)
        {
            u32* dstRow = &dst[(y * scaleFactor + sy) * (srcWidth * scaleFactor)];
            for (u32 x = 0; x < srcWidth; x++)
            {
                u32 value = srcRow[x];
                for (u32 sx = 0; sx < scaleFactor; sx++)
                    dstRow[x * scaleFactor + sx] = value;
            }
        }
    }
}

inline void TextureScaleRunCPUFallback(int outputFmt,
                                       u32 width,
                                       u32 height,
                                       const u32* source,
                                       u32 scaleFactor,
                                       u32* dst,
                                       bool nearest)
{
    if (nearest)
    {
        TextureScaleUpscaleNearest(width, height, source, scaleFactor, dst);
        return;
    }

    switch (outputFmt)
    {
    case outputFmt_RGB6A5:
        UpscaleTextureSpline36<outputFmt_RGB6A5>(width, height, source, scaleFactor, dst);
        break;
    case outputFmt_RGBA8:
        UpscaleTextureSpline36<outputFmt_RGBA8>(width, height, source, scaleFactor, dst);
        break;
    case outputFmt_BGRA8:
        UpscaleTextureSpline36<outputFmt_BGRA8>(width, height, source, scaleFactor, dst);
        break;
    }
}

}

#endif
