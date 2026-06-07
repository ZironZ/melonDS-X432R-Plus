#ifndef GPU3D_TEXCACHE_DEBUG_H
#define GPU3D_TEXCACHE_DEBUG_H

#include "types.h"
#include "TextureScalingDebug.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace melonDS
{

class TextureScalingDebugTracker
{
public:
    static constexpr u8 InvalidationReason_Texture = 1 << 0;
    static constexpr u8 InvalidationReason_Palette = 1 << 1;

    void RotateFrame(u64 frameIndex)
    {
        if (CaptureFrameTexturesActive)
            FinishFrameTextureCapture();

        LastFrame = CurrentFrame;
        CurrentFrame = {};
        FramesObserved++;

        if (CaptureFrameTexturesRequested)
        {
            CaptureFrameTexturesRequested = false;
            CaptureFrameTexturesActive = true;
            CurrentFrameTextureFrameIndex = frameIndex;
            CurrentFrameTextures.clear();
            CurrentFrameTextureIndices.clear();
        }
    }

    void CountEvent(u64 TextureScalingDebugFrameStats::* field)
    {
        CurrentFrame.*field += 1;
        Totals.*field += 1;
    }

    void CountPixels(u64 TextureScalingDebugFrameStats::* field, u64 pixels)
    {
        CurrentFrame.*field += pixels;
        Totals.*field += pixels;
    }

    void MarkInvalidated(u64 key, u8 reason)
    {
        InvalidatedReasons[key] |= reason;
    }

    void PopulateInvalidation(u64 key, TextureScalingDebugLastMiss& miss)
    {
        if (auto invalidIt = InvalidatedReasons.find(key); invalidIt != InvalidatedReasons.end())
        {
            miss.InvalidatedByTexture = (invalidIt->second & InvalidationReason_Texture) != 0;
            miss.InvalidatedByPalette = (invalidIt->second & InvalidationReason_Palette) != 0;
            InvalidatedReasons.erase(invalidIt);
        }
    }

    void RecordLastMiss(TextureScalingDebugLastMiss&& miss)
    {
        miss.Sequence = ++LastMissSequence;
        LastMiss = std::move(miss);
    }

    void SetLastMissImageCaptureEnabled(bool enabled)
    {
        CaptureLastMissImages = enabled;
    }

    bool ConsumeLastMissImageCaptureRequest()
    {
        bool capture = CaptureLastMissImages;
        CaptureLastMissImages = false;
        return capture;
    }

    void SetFrameTextureCaptureEnabled(bool enabled)
    {
        CaptureFrameTexturesRequested = enabled;
        if (!enabled && CaptureFrameTexturesActive)
            FinishFrameTextureCapture();
    }

    bool IsFrameTextureCaptureActive() const
    {
        return CaptureFrameTexturesActive;
    }

    bool IsFrameTextureCaptureRequested() const
    {
        return CaptureFrameTexturesRequested;
    }

    bool HasFrameTexture(u64 key) const
    {
        return CurrentFrameTextureIndices.find(key) != CurrentFrameTextureIndices.end();
    }

    void RecordFrameTexture(TextureScalingDebugFrameTexture&& texture)
    {
        if (!CaptureFrameTexturesActive)
            return;

        texture.Valid = true;
        if (texture.ReferenceCount == 0)
            texture.ReferenceCount = 1;

        auto existing = CurrentFrameTextureIndices.find(texture.Key);
        if (existing != CurrentFrameTextureIndices.end())
        {
            TextureScalingDebugFrameTexture& dst = CurrentFrameTextures[existing->second];
            dst.ReferenceCount += texture.ReferenceCount;
            dst.CacheHit = dst.CacheHit || texture.CacheHit;
            dst.CacheMiss = dst.CacheMiss || texture.CacheMiss;
            dst.SecondaryCacheHit = dst.SecondaryCacheHit || texture.SecondaryCacheHit;
            dst.UsedGPUScaler = dst.UsedGPUScaler || texture.UsedGPUScaler;
            dst.UsedGPUDirectRepack = dst.UsedGPUDirectRepack || texture.UsedGPUDirectRepack;
            dst.UsedFrequentChangeFallback = dst.UsedFrequentChangeFallback || texture.UsedFrequentChangeFallback;
            dst.UsedDeferredUnscaledUpload = dst.UsedDeferredUnscaledUpload || texture.UsedDeferredUnscaledUpload;
            dst.UsedXBRZ = dst.UsedXBRZ || texture.UsedXBRZ;
            dst.UsedSpline36 = dst.UsedSpline36 || texture.UsedSpline36;
            dst.DeferredScalePending = dst.DeferredScalePending || texture.DeferredScalePending;
            dst.FrequentChangeHot = dst.FrequentChangeHot || texture.FrequentChangeHot;
            if (dst.SourceRGBA.empty() && !texture.SourceRGBA.empty())
                dst.SourceRGBA = std::move(texture.SourceRGBA);
            if (dst.ResultRGBA.empty() && !texture.ResultRGBA.empty())
                dst.ResultRGBA = std::move(texture.ResultRGBA);
            return;
        }

        CurrentFrameTextureIndices.emplace(texture.Key, CurrentFrameTextures.size());
        CurrentFrameTextures.push_back(std::move(texture));
    }

    void FinishFrameTextureCapture()
    {
        if (!CaptureFrameTexturesActive)
            return;

        CapturedFrameTextures = {};
        CapturedFrameTextures.Valid = true;
        CapturedFrameTextures.Sequence = ++FrameTextureCaptureSequence;
        CapturedFrameTextures.FrameIndex = CurrentFrameTextureFrameIndex;
        CapturedFrameTextures.Textures = std::move(CurrentFrameTextures);
        CurrentFrameTextures.clear();
        CurrentFrameTextureIndices.clear();
        CaptureFrameTexturesActive = false;
    }

    void ResetCacheState()
    {
        InvalidatedReasons.clear();
        LastMiss = {};
        LastMissSequence = 0;
        CurrentFrameTextures.clear();
        CurrentFrameTextureIndices.clear();
        CapturedFrameTextures = {};
        FrameTextureCaptureSequence = 0;
        CurrentFrameTextureFrameIndex = 0;
        CaptureFrameTexturesRequested = false;
        CaptureFrameTexturesActive = false;
    }

    void GetStats(TextureScalingDebugStats& stats,
                  bool usesComputeBackend,
                  bool readableTextureCache,
                  bool textureScalingEnabled,
                  bool frequentChangePolicyEnabled,
                  bool deferredScalingEnabled,
                  bool legacyAlphaHandling,
                  bool qualityAlphaHandling,
                  int algorithmIndex,
                  u32 scaleFactor,
                  u32 cacheEntries,
                  u32 secondaryCacheEntries,
                  u32 secondaryCacheMaxEntries,
                  u64 secondaryCacheTexels,
                  u64 secondaryCacheMaxTexels,
                  u32 freeLayers,
                  u32 textureArrays) const
    {
        stats = {};
        stats.UsesOpenGLRenderer = true;
        stats.UsesComputeBackend = usesComputeBackend;
        stats.TextureScalingEnabled = textureScalingEnabled;
        stats.FrequentChangePolicyEnabled = frequentChangePolicyEnabled;
        stats.DeferredScalingEnabled = deferredScalingEnabled;
        stats.LegacyAlphaHandling = legacyAlphaHandling;
        stats.QualityAlphaHandling = qualityAlphaHandling;
        stats.ReadableTextureCache = readableTextureCache;
        stats.AlgorithmIndex = algorithmIndex;
        stats.ScaleFactor = scaleFactor;
        stats.CacheEntries = cacheEntries;
        stats.SecondaryCacheEntries = secondaryCacheEntries;
        stats.SecondaryCacheMaxEntries = secondaryCacheMaxEntries;
        stats.FreeLayers = freeLayers;
        stats.TextureArrays = textureArrays;
        stats.TotalLayers = stats.CacheEntries + stats.SecondaryCacheEntries + stats.FreeLayers;
        stats.SecondaryCacheTexels = secondaryCacheTexels;
        stats.SecondaryCacheMaxTexels = secondaryCacheMaxTexels;
        stats.SecondaryCacheApproxBytes = secondaryCacheTexels * 4;
        stats.SecondaryCacheMaxApproxBytes = secondaryCacheMaxTexels * 4;
        stats.FramesObserved = FramesObserved;
        stats.LastMissSequence = LastMissSequence;
        stats.LastFrame = LastFrame;
        stats.Totals = Totals;
    }

    void ResetCounters()
    {
        CurrentFrame = {};
        LastFrame = {};
        Totals = {};
        FramesObserved = 0;
        ResetCacheState();
    }

    void GetLastMiss(TextureScalingDebugLastMiss& miss) const
    {
        miss = LastMiss;
    }

    void GetFrameTextures(TextureScalingDebugFrameTextures& frame) const
    {
        frame = CapturedFrameTextures;
        frame.CapturePending = CaptureFrameTexturesRequested;
        frame.CaptureActive = CaptureFrameTexturesActive;
    }

private:
    std::unordered_map<u64, u8> InvalidatedReasons;
    std::unordered_map<u64, size_t> CurrentFrameTextureIndices;
    TextureScalingDebugFrameStats CurrentFrame {};
    TextureScalingDebugFrameStats LastFrame {};
    TextureScalingDebugFrameStats Totals {};
    u64 FramesObserved = 0;
    u64 LastMissSequence = 0;
    u64 FrameTextureCaptureSequence = 0;
    u64 CurrentFrameTextureFrameIndex = 0;
    TextureScalingDebugLastMiss LastMiss {};
    TextureScalingDebugFrameTextures CapturedFrameTextures {};
    std::vector<TextureScalingDebugFrameTexture> CurrentFrameTextures;
    bool CaptureLastMissImages = false;
    bool CaptureFrameTexturesRequested = false;
    bool CaptureFrameTexturesActive = false;
};

}

#endif
