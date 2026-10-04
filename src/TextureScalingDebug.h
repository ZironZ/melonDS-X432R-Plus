// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTURESCALINGDEBUG_H
#define TEXTURESCALINGDEBUG_H

#include <vector>

#include "types.h"

namespace melonDS
{

struct TextureScalingDebugFrameStats
{
    u64 CacheHits = 0;
    u64 CacheMisses = 0;
    u64 SecondaryCacheHits = 0;
    u64 SecondaryCacheStores = 0;
    u64 SecondaryCacheEvictions = 0;
    u64 Invalidations = 0;
    u64 Uploads = 0;
    u64 ScaledUploads = 0;
    u64 GPUScaledUploads = 0;
    u64 GPUDirectRepackUploads = 0;
    u64 GPULegacyReadbackUploads = 0;
    u64 FrequentChangeFallbackUploads = 0;
    u64 DeferredUnscaledUploads = 0;
    u64 DeferredPromotionUploads = 0;
    u64 XBRZScaledUploads = 0;
    u64 Spline36ScaledUploads = 0;
    u64 SourcePixels = 0;
    u64 UploadPixels = 0;
};

struct TextureScalingDebugStats
{
    bool UsesOpenGLRenderer = false;
    bool UsesComputeBackend = false;
    bool TextureScalingEnabled = false;
    bool FrequentChangePolicyEnabled = false;
    bool DeferredScalingEnabled = false;
    bool LegacyAlphaHandling = false;
    bool QualityAlphaHandling = false;
    int AlphaAlgorithmIndex = 0;
    bool ReadableTextureCache = false;
    int AlgorithmIndex = 0;
    u32 ScaleFactor = 1;
    u32 CacheEntries = 0;
    u32 SecondaryCacheEntries = 0;
    u32 SecondaryCacheMaxEntries = 0;
    u32 FreeLayers = 0;
    u32 TotalLayers = 0;
    u32 TextureArrays = 0;
    u64 SecondaryCacheTexels = 0;
    u64 SecondaryCacheMaxTexels = 0;
    u64 SecondaryCacheApproxBytes = 0;
    u64 SecondaryCacheMaxApproxBytes = 0;
    u64 FramesObserved = 0;
    u64 LastMissSequence = 0;
    u32 ReconstructionEntries = 0;
    u32 ReconstructionHits = 0;
    u32 ReconstructionBuilds = 0;
    u64 ReconstructionBuiltTexels = 0;
    TextureScalingDebugFrameStats LastFrame;
    TextureScalingDebugFrameStats Totals;
};

struct TextureScalingDebugLastMiss
{
    bool Valid = false;
    bool UsedScaling = false;
    bool UsedGPUScaler = false;
    bool UsedGPUDirectRepack = false;
    bool UsedFrequentChangeFallback = false;
    bool UsedDeferredUnscaledUpload = false;
    bool UsedXBRZ = false;
    bool UsedSpline36 = false;
    bool InvalidatedByTexture = false;
    bool InvalidatedByPalette = false;
    int AlgorithmIndex = 0;
    u32 ScaleFactor = 1;
    u32 TexParam = 0;
    u32 PalBase = 0;
    u32 SourceWidth = 0;
    u32 SourceHeight = 0;
    u32 ResultWidth = 0;
    u32 ResultHeight = 0;
    u64 Sequence = 0;
    std::vector<u32> SourceRGBA;
    std::vector<u32> ResultRGBA;
};

struct TextureScalingDebugFrameTexture
{
    bool Valid = false;
    bool CacheHit = false;
    bool CacheMiss = false;
    bool SecondaryCacheHit = false;
    bool UsedScaling = false;
    bool UsedGPUScaler = false;
    bool UsedGPUDirectRepack = false;
    bool UsedFrequentChangeFallback = false;
    bool UsedDeferredUnscaledUpload = false;
    bool UsedXBRZ = false;
    bool UsedSpline36 = false;
    bool BinaryAlphaTexture = false;
    bool SourceBinaryAlpha = false;
    bool SourceHasTransparentAlpha = false;
    bool DeferredScalePending = false;
    bool FrequentChangeHot = false;
    bool SamplingBoundsValid = false;
    bool SamplingBoundsEdgeExtendMargins = false;
    int AlgorithmIndex = 0;
    u32 ScaleFactor = 1;
    u32 TexParam = 0;
    u32 PalBase = 0;
    u32 SourceWidth = 0;
    u32 SourceHeight = 0;
    u32 ResultWidth = 0;
    u32 ResultHeight = 0;
    u32 Format = 0;
    u32 RepeatMode = 0;
    u32 ReferenceCount = 0;
    u16 SamplingX0 = 0;
    u16 SamplingY0 = 0;
    u16 SamplingX1 = 0;
    u16 SamplingY1 = 0;
    u64 Key = 0;
    u64 BaseKey = 0;
    std::vector<u32> SourceRGBA;
    std::vector<u32> ResultRGBA;
};

struct TextureScalingDebugFrameTextures
{
    bool Valid = false;
    bool CapturePending = false;
    bool CaptureActive = false;
    u64 Sequence = 0;
    u64 FrameIndex = 0;
    std::vector<TextureScalingDebugFrameTexture> Textures;
};

}

#endif
