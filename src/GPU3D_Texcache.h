#ifndef GPU3D_TEXCACHE
#define GPU3D_TEXCACHE

#include "GPU.h"
#include "GPU3D_TexcacheDecode.h"
#include "GPU3D_TexcacheDebug.h"
#include "GPU3D_TexcacheMip.h"
#include "GPU3D_TexcacheScale.h"
#include "GPU3D_TextureTypes.h"

#include <algorithm>
#include <assert.h>
#include <unordered_map>
#include <vector>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

namespace melonDS
{

template <typename TexLoaderT, typename TexHandleT>
class Texcache
{
private:
    struct TexArrayEntry
    {
        TexHandleT TextureID;
        u32 Layer;
        u8 StorageBucket;
        bool Dedicated;
    };

    struct TexCacheEntry
    {
        u32 LastVariant; // very cheap way to make variant lookup faster

        u64 BaseKey;
        u32 RawTexParam;
        u32 PalBase;
        u32 TextureRAMStart[2], TextureRAMSize[2];
        u32 TexPalStart, TexPalSize;
        u32 SourceWidth = 0;
        u32 SourceHeight = 0;
        u32 ResultWidth = 0;
        u32 ResultHeight = 0;
        u32 ScaleFactor = 1;
        int OutputFormat = outputFmt_RGB6A5;
        u8 WidthLog2, HeightLog2;
        u8 StorageBucket;
        TexArrayEntry Texture;
        bool BinaryAlphaTexture = false;
        bool UsedFrequentChangeFallback = false;
        bool DeferredScalePending = false;
        bool DeferredScaleQueued = false;
        u32 DeferredUseCount = 0;
        u64 DeferredFirstSeenFrame = 0;
        u64 CreatedFrame = 0;

        u64 TextureHash[2];
        u64 TexPalHash;
    };

    struct FrequentChangeEntry
    {
        u32 BurstCount = 0;
        u64 LastInvalidationFrame = 0;
        bool Hot = false;
        bool SecondaryCacheReuseSeen = false;
    };

    struct SecondaryCacheEntry
    {
        TexCacheEntry Entry;
        u64 LastUsedFrame = 0;
        u64 TexelCost = 0;
    };

public:
    Texcache(melonDS::GPU& gpu, const TexLoaderT& texloader)
        : GPU(gpu), TexLoader(texloader) // probably better if this would be a move constructor???
    {}

    u64 MaskedHash(u8* vram, u32 vramSize, u32 addr, u32 size)
    {
        u64 hash = 0;

        while (size > 0)
        {
            u32 pieceSize;
            if (addr + size > vramSize)
                // wraps around, only do the part inside
                pieceSize = vramSize - addr;
            else
                // fits completely inside
                pieceSize = size;

            hash = XXH64(&vram[addr], pieceSize, hash);

            addr += pieceSize;
            addr &= (vramSize - 1);
            assert(size >= pieceSize);
            size -= pieceSize;
        }

        return hash;
    }

    bool CheckInvalid(u32 start, u32 size, u64 oldHash, u64* dirty, u8* vram, u32 vramSize)
    {
        u32 startBit = start / VRAMDirtyGranularity;
        u32 bitsCount = ((start + size + VRAMDirtyGranularity - 1) / VRAMDirtyGranularity) - startBit;
    
        u32 startEntry = startBit >> 6;
        u64 entriesCount = ((startBit + bitsCount + 0x3F) >> 6) - startEntry;
        for (u32 j = startEntry; j < startEntry + entriesCount; j++)
        {
            if (GetRangedBitMask(j, startBit, bitsCount) & dirty[j & ((vramSize / VRAMDirtyGranularity)-1)])
            {
                if (MaskedHash(vram, vramSize, start, size) != oldHash)
                    return true;
            }
        }

        return false;
    }

    bool Update(u8& clrBitmapDirty)
    {
        FrameIndex++;
        Debug.RotateFrame(FrameIndex);
        EdgeExtendNewVariantsThisFrame = 0;
        EdgeExtendThrottledVariantsThisFrame = 0;
        FrequentChangeFallbackPromotionsThisFrame = 0;
        FrequentChangeFallbackPromotionTexelsThisFrame = 0;
        bool cacheStateChanged = false;
        std::unordered_map<u64, bool> frequentChangeInvalidatedBases;

        auto textureDirty = GPU.VRAMDirty_Texture.DeriveState(GPU.VRAMMap_Texture, GPU);
        auto texPalDirty = GPU.VRAMDirty_TexPal.DeriveState(GPU.VRAMMap_TexPal, GPU);

        bool textureChanged = GPU.MakeVRAMFlat_TextureCoherent(textureDirty);
        bool texPalChanged = GPU.MakeVRAMFlat_TexPalCoherent(texPalDirty);

        clrBitmapDirty = 0;

        if (textureChanged || texPalChanged)
        {
            // check if slots 2 and 3 are dirty (for the clear bitmap)
            for (u32 j = (0x40000/(VRAMDirtyGranularity*64)); j < (0x60000/(VRAMDirtyGranularity*64)); j++)
            {
                if (textureDirty.Data[j])
                {
                    clrBitmapDirty |= (1<<0);
                    break;
                }
            }
            for (u32 j = (0x60000/(VRAMDirtyGranularity*64)); j < (0x80000/(VRAMDirtyGranularity*64)); j++)
            {
                if (textureDirty.Data[j])
                {
                    clrBitmapDirty |= (1<<1);
                    break;
                }
            }

            //printf("check invalidation %d\n", TexCache.size());
            for (auto it = Cache.begin(); it != Cache.end();)
            {
                TexCacheEntry& entry = it->second;
                u8 invalidateReason = 0;
                if (textureChanged)
                {
                    for (u32 i = 0; i < 2; i++)
                    {
                        if (CheckInvalid(entry.TextureRAMStart[i], entry.TextureRAMSize[i],
                                entry.TextureHash[i],
                                textureDirty.Data,
                                GPU.VRAMFlat_Texture, sizeof(GPU.VRAMFlat_Texture)))
                        {
                            invalidateReason |= TextureScalingDebugTracker::InvalidationReason_Texture;
                            break;
                        }
                    }
                }

                if (texPalChanged && entry.TexPalSize > 0)
                {
                    if (CheckInvalid(entry.TexPalStart, entry.TexPalSize,
                            entry.TexPalHash,
                            texPalDirty.Data,
                            GPU.VRAMFlat_TexPal, sizeof(GPU.VRAMFlat_TexPal)))
                        invalidateReason |= TextureScalingDebugTracker::InvalidationReason_Palette;
                }

                if (invalidateReason == 0)
                {
                    it++;
                    continue;
                }

                Debug.MarkInvalidated(entry.BaseKey, invalidateReason);
                if (frequentChangeInvalidatedBases.emplace(entry.BaseKey, true).second)
                    TrackFrequentChangeInvalidation(entry.BaseKey);
                Debug.CountEvent(&TextureScalingDebugFrameStats::Invalidations);
                if ((invalidateReason & TextureScalingDebugTracker::InvalidationReason_Texture) == 0)
                {
                    it++;
                    continue;
                }

                if (!ArchiveSecondaryCacheEntry(it->first, entry))
                    ReleaseStoragePlace(entry);
                RemoveVariantKey(entry.BaseKey, it->first);
                it = Cache.erase(it);
            }
        }
        if (ProcessDeferredScaleQueue())
            cacheStateChanged = true;
        if (PromoteStableFrequentChangeEntries())
            cacheStateChanged = true;

        return textureChanged || texPalChanged || cacheStateChanged || Debug.IsFrameTextureCaptureActive();
    }

    TexArrayEntry AcquireStoragePlace(u32 widthLog2, u32 heightLog2, u32 scaledWidth, u32 scaledHeight, u8 storageBucket,
                                      bool dedicated, u32 scaleFactor)
    {
        if (dedicated)
            return TexArrayEntry{TexLoader.GenerateTexture(scaledWidth, scaledHeight, 1, scaleFactor), 0, storageBucket, true};

        auto& texArrays = TexArrays[widthLog2][heightLog2][storageBucket];
        auto& freeTextures = FreeTextures[widthLog2][heightLog2][storageBucket];

        if (freeTextures.empty())
        {
            texArrays.resize(texArrays.size() + 1);
            TexHandleT& array = texArrays[texArrays.size() - 1];

            u32 layers = std::max<u32>(1, std::min<u32>((8 * 1024 * 1024) / (scaledWidth * scaledHeight * 4), 64));

            // allocate new array texture
            array = TexLoader.GenerateTexture(scaledWidth, scaledHeight, layers, scaleFactor);

            for (u32 i = 0; i < layers; i++)
                freeTextures.push_back(TexArrayEntry{array, i, storageBucket, false});
        }

        TexArrayEntry storagePlace = freeTextures.back();
        freeTextures.pop_back();
        return storagePlace;
    }

    void GetTexture(u32 texParam, u32 palBase, TexHandleT& textureHandle, u32& layer, u32*& helper,
                    bool* binaryAlphaTexture = nullptr, const TextureSamplingBounds* samplingBounds = nullptr)
    {
        u32 rawTexParam = texParam;
        // remove sampling and texcoord gen params
        texParam &= ~0xC00F0000;

        u32 fmt = (texParam >> 26) & 0x7;
        u32 widthLog2 = (texParam >> 20) & 0x7;
        u32 heightLog2 = (texParam >> 23) & 0x7;
        u32 width = 8 << widthLog2;
        u32 height = 8 << heightLog2;
        bool color0Transparent = texParam & (1 << 29);
        u32 addr = (texParam & 0xFFFF) * 8;
        TextureSamplingBounds activeSamplingBounds;
        bool hasSamplingBounds = NormalizeSamplingBounds(width, height, samplingBounds, activeSamplingBounds);
        bool useSubrectHandling =
            hasSamplingBounds && !activeSamplingBounds.EdgeExtendMargins &&
            TexLoader.UseFilterableMipSubrectHandling();
        bool useMarginExtension =
            TextureScalingEdgeExtendUnusedMargins && TextureScaleFactor > 1 &&
            hasSamplingBounds && activeSamplingBounds.EdgeExtendMargins;
        bool useSamplingBoundsKey = useSubrectHandling || useMarginExtension;

        u32 texPalStart = 0;
        u32 texPalSize = 0;
        if (fmt == 5)
        {
            texPalStart = palBase * 16;
            texPalSize = 0x10000;
        }
        else if (fmt != 7)
        {
            switch (fmt)
            {
            case 1:
            case 6:
                texPalSize = (fmt == 1) ? 32 * 2 : 8 * 2;
                texPalStart = (palBase * 16) & 0x1FFFF;
                break;
            case 2:
                texPalSize = 4 * 2;
                texPalStart = ((palBase * 16) >> 1) & 0x1FFFF;
                break;
            case 3:
                texPalSize = 16 * 2;
                texPalStart = (palBase * 16) & 0x1FFFF;
                break;
            case 4:
                texPalSize = 256 * 2;
                texPalStart = (palBase * 16) & 0x1FFFF;
                break;
            }
        }

        u64 baseKey = MakeBaseKey(texParam, fmt);
        u64 subrectKey = useSamplingBoundsKey ? MakeSubrectKey(activeSamplingBounds) : 0;
        u64 texPalHash = 0;
        if (texPalSize > 0)
            texPalHash = MaskedHash(GPU.VRAMFlat_TexPal, sizeof(GPU.VRAMFlat_TexPal), texPalStart, texPalSize);
        u64 key = MakeVariantKey(baseKey, texPalHash, texPalSize > 0, subrectKey);
        if (useMarginExtension && Cache.find(key) == Cache.end())
        {
            if (EdgeExtendNewVariantsThisFrame >= MaxEdgeExtendNewVariantsPerFrame)
            {
                EdgeExtendThrottledVariantsThisFrame++;
                activeSamplingBounds = {};
                useMarginExtension = false;
                useSamplingBoundsKey = false;
                subrectKey = 0;
                key = MakeVariantKey(baseKey, texPalHash, texPalSize > 0, subrectKey);
            }
            else
            {
                EdgeExtendNewVariantsThisFrame++;
            }
        }
        bool deferScaleOnMiss = DeferredScalingEnabled && !InDeferredPromotion && !useSamplingBoundsKey &&
            TextureScaleFactor > 1 && texPalSize > 0;
        u32 effectiveScaleFactor = deferScaleOnMiss ? 1 : TextureScaleFactor;
        u32 scaledWidth = width * effectiveScaleFactor;
        u32 scaledHeight = height * effectiveScaleFactor;
        u8 storageBucket = deferScaleOnMiss ? StorageBucketNative : StorageBucketScaled;
        //printf("%" PRIx64 " %" PRIx32 " %" PRIx32 "\n", key, texParam, palBase);

        assert(fmt != 0 && "no texture is not a texture format!");

        auto it = Cache.find(key);

        if (it != Cache.end())
        {
            if (ShouldPromoteFrequentChangeFallbackEntry(it->second))
            {
                const u64 promotionTexels =
                    static_cast<u64>(it->second.SourceWidth) * static_cast<u64>(it->second.SourceHeight);
                FrequentChangeFallbackPromotionsThisFrame++;
                FrequentChangeFallbackPromotionTexelsThisFrame += promotionTexels;

                const TexCacheEntry oldEntry = it->second;
                ReleaseStoragePlace(oldEntry);
                RemoveVariantKey(oldEntry.BaseKey, key);
                Cache.erase(it);

                InFrequentChangeFallbackPromotion = true;
                GetTexture(rawTexParam, palBase, textureHandle, layer, helper, binaryAlphaTexture, samplingBounds);
                InFrequentChangeFallbackPromotion = false;
                return;
            }

            TouchVariantKey(baseKey, key);
            if (it->second.DeferredScalePending)
            {
                it->second.DeferredUseCount++;
                if (IsDeferredScaleReady(it->second))
                    QueueDeferredScale(key, it->second);
            }
            Debug.CountEvent(&TextureScalingDebugFrameStats::CacheHits);
            textureHandle = it->second.Texture.TextureID;
            layer = it->second.Texture.Layer;
            helper = &it->second.LastVariant;
            if (binaryAlphaTexture)
                *binaryAlphaTexture = it->second.BinaryAlphaTexture;
            if (!InDeferredPromotion && Debug.IsFrameTextureCaptureActive())
            {
                TextureScalingDebugFrameTexture frameTexture = MakeDebugFrameTextureRecord(
                    key, baseKey, rawTexParam, palBase, fmt, it->second.SourceWidth, it->second.SourceHeight,
                    it->second.ResultWidth, it->second.ResultHeight, it->second.ScaleFactor,
                    activeSamplingBounds, addr, it->second.TextureRAMStart[1],
                    texPalStart, color0Transparent);
                frameTexture.CacheHit = true;
                frameTexture.BinaryAlphaTexture = it->second.BinaryAlphaTexture;
                frameTexture.UsedFrequentChangeFallback = it->second.UsedFrequentChangeFallback;
                frameTexture.DeferredScalePending = it->second.DeferredScalePending;
                frameTexture.FrequentChangeHot = IsFrequentChangeHot(baseKey);
                TexLoader.ReadTextureLayerPreviewRGBA8(it->second.Texture.TextureID,
                                                       it->second.Texture.Layer,
                                                       it->second.ResultWidth,
                                                       it->second.ResultHeight,
                                                       it->second.OutputFormat,
                                                       frameTexture.ResultRGBA);
                Debug.RecordFrameTexture(std::move(frameTexture));
            }
            return;
        }

        if (!InDeferredPromotion)
            Debug.CountEvent(&TextureScalingDebugFrameStats::CacheMisses);

        TextureScalingDebugLastMiss lastMiss = {};
        lastMiss.Valid = true;
        lastMiss.UsedScaling = effectiveScaleFactor > 1;
        lastMiss.AlgorithmIndex = RendererSettings::GetGLScaleAlgorithmIndex(TexLoader.ScalingAlgorithm());
        lastMiss.ScaleFactor = effectiveScaleFactor;
        lastMiss.TexParam = rawTexParam;
        lastMiss.PalBase = palBase;

        lastMiss.SourceWidth = width;
        lastMiss.SourceHeight = height;
        lastMiss.ResultWidth = scaledWidth;
        lastMiss.ResultHeight = scaledHeight;

        if (!InDeferredPromotion)
            Debug.PopulateInvalidation(baseKey, lastMiss);
        bool useFrequentChangeFallback = !deferScaleOnMiss && !InFrequentChangeFallbackPromotion &&
            ShouldUseFrequentChangeFallback(baseKey);

        TexCacheEntry entry = {0};
        entry.BaseKey = baseKey;
        entry.RawTexParam = rawTexParam;
        entry.PalBase = palBase;
        entry.StorageBucket = storageBucket;
        entry.BinaryAlphaTexture = false;
        entry.UsedFrequentChangeFallback = false;
        entry.DeferredScalePending = deferScaleOnMiss;
        entry.DeferredUseCount = deferScaleOnMiss ? 1 : 0;
        entry.DeferredFirstSeenFrame = FrameIndex;
        entry.CreatedFrame = FrameIndex;

        entry.TextureRAMStart[0] = addr;
        entry.WidthLog2 = widthLog2;
        entry.HeightLog2 = heightLog2;
        entry.TexPalStart = texPalStart;
        entry.TexPalSize = texPalSize;

        int outputFmt = TexLoader.PreferredOutputFormat();
        const TextureVRAMView vram = VRAMView();

        // apparently a new texture
        if (fmt == 7)
        {
            entry.TextureRAMSize[0] = width*height*2;

            switch (outputFmt)
            {
            case outputFmt_RGB6A5:
                ConvertBitmapTexture<outputFmt_RGB6A5>(width, height, DecodeBuffer(width * height), addr, vram);
                break;
            case outputFmt_RGBA8:
                ConvertBitmapTexture<outputFmt_RGBA8>(width, height, DecodeBuffer(width * height), addr, vram);
                break;
            case outputFmt_BGRA8:
                ConvertBitmapTexture<outputFmt_BGRA8>(width, height, DecodeBuffer(width * height), addr, vram);
                break;
            }
        }
        else if (fmt == 5)
        {
            u32 slot1addr = 0x20000 + ((addr & 0x1FFFC) >> 1);
            if (addr >= 0x40000)
                slot1addr += 0x10000;

            entry.TextureRAMSize[0] = width*height/16*4;
            entry.TextureRAMStart[1] = slot1addr;
            entry.TextureRAMSize[1] = width*height/16*2;

            switch (outputFmt)
            {
            case outputFmt_RGB6A5:
                ConvertCompressedTexture<outputFmt_RGB6A5>(width, height, DecodeBuffer(width * height), addr, slot1addr, entry.TexPalStart, vram);
                break;
            case outputFmt_RGBA8:
                ConvertCompressedTexture<outputFmt_RGBA8>(width, height, DecodeBuffer(width * height), addr, slot1addr, entry.TexPalStart, vram);
                break;
            case outputFmt_BGRA8:
                ConvertCompressedTexture<outputFmt_BGRA8>(width, height, DecodeBuffer(width * height), addr, slot1addr, entry.TexPalStart, vram);
                break;
            }
        }
        else
        {
            u32 texSize, palAddr = texPalStart, numPalEntries;
            switch (fmt)
            {
            case 1: texSize = width*height; numPalEntries = 32; break;
            case 6: texSize = width*height; numPalEntries = 8; break;
            case 2: texSize = width*height/4; numPalEntries = 4; break;
            case 3: texSize = width*height/2; numPalEntries = 16; break;
            case 4: texSize = width*height; numPalEntries = 256; break;
            }

            /*printf("creating texture | fmt: %d | %dx%d | %08x | %08x\n", fmt, width, height, addr, palAddr);
            svcSleepThread(1000*1000);*/

            entry.TextureRAMSize[0] = texSize;
            entry.TexPalSize = numPalEntries*2;

            //assert(entry.TexPalStart+entry.TexPalSize <= 128*1024*1024);

            switch (fmt)
            {
            case 1:
                switch (outputFmt)
                {
                case outputFmt_RGB6A5: ConvertAXIYTexture<outputFmt_RGB6A5, 3, 5>(width, height, DecodeBuffer(width * height), addr, palAddr, vram); break;
                case outputFmt_RGBA8: ConvertAXIYTexture<outputFmt_RGBA8, 3, 5>(width, height, DecodeBuffer(width * height), addr, palAddr, vram); break;
                case outputFmt_BGRA8: ConvertAXIYTexture<outputFmt_BGRA8, 3, 5>(width, height, DecodeBuffer(width * height), addr, palAddr, vram); break;
                }
                break;
            case 6:
                switch (outputFmt)
                {
                case outputFmt_RGB6A5: ConvertAXIYTexture<outputFmt_RGB6A5, 5, 3>(width, height, DecodeBuffer(width * height), addr, palAddr, vram); break;
                case outputFmt_RGBA8: ConvertAXIYTexture<outputFmt_RGBA8, 5, 3>(width, height, DecodeBuffer(width * height), addr, palAddr, vram); break;
                case outputFmt_BGRA8: ConvertAXIYTexture<outputFmt_BGRA8, 5, 3>(width, height, DecodeBuffer(width * height), addr, palAddr, vram); break;
                }
                break;
            case 2:
                switch (outputFmt)
                {
                case outputFmt_RGB6A5: ConvertNColorsTexture<outputFmt_RGB6A5, 2>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                case outputFmt_RGBA8: ConvertNColorsTexture<outputFmt_RGBA8, 2>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                case outputFmt_BGRA8: ConvertNColorsTexture<outputFmt_BGRA8, 2>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                }
                break;
            case 3:
                switch (outputFmt)
                {
                case outputFmt_RGB6A5: ConvertNColorsTexture<outputFmt_RGB6A5, 4>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                case outputFmt_RGBA8: ConvertNColorsTexture<outputFmt_RGBA8, 4>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                case outputFmt_BGRA8: ConvertNColorsTexture<outputFmt_BGRA8, 4>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                }
                break;
            case 4:
                switch (outputFmt)
                {
                case outputFmt_RGB6A5: ConvertNColorsTexture<outputFmt_RGB6A5, 8>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                case outputFmt_RGBA8: ConvertNColorsTexture<outputFmt_RGBA8, 8>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                case outputFmt_BGRA8: ConvertNColorsTexture<outputFmt_BGRA8, 8>(width, height, DecodeBuffer(width * height), addr, palAddr, color0Transparent, vram); break;
                }
                break;
            }
        }

        if (useSubrectHandling)
        {
            const u32 cropWidth = activeSamplingBounds.X1 - activeSamplingBounds.X0;
            const u32 cropHeight = activeSamplingBounds.Y1 - activeSamplingBounds.Y0;
            CropTexture(width, height, DecodeBufferStorage.data(), activeSamplingBounds, CroppedBufferStorage);
            DecodeBufferStorage.swap(CroppedBufferStorage);
            width = cropWidth;
            height = cropHeight;
            scaledWidth = width * effectiveScaleFactor;
            scaledHeight = height * effectiveScaleFactor;
        }
        else if (useMarginExtension)
        {
            EdgeExtendTextureMargins(width, height, DecodeBufferStorage.data(), activeSamplingBounds);
        }

        entry.SourceWidth = width;
        entry.SourceHeight = height;
        entry.ResultWidth = scaledWidth;
        entry.ResultHeight = scaledHeight;
        entry.ScaleFactor = effectiveScaleFactor;
        entry.OutputFormat = outputFmt;

        for (int i = 0; i < 2; i++)
        {
            if (entry.TextureRAMSize[i])
                entry.TextureHash[i] = MaskedHash(GPU.VRAMFlat_Texture, sizeof(GPU.VRAMFlat_Texture),
                    entry.TextureRAMStart[i], entry.TextureRAMSize[i]);
        }
        if (entry.TexPalSize)
            entry.TexPalHash = texPalHash;

        if (TryRestoreSecondaryCacheEntry(key, entry, textureHandle, layer, helper,
                binaryAlphaTexture, activeSamplingBounds, addr, fmt, color0Transparent))
            return;

        TexArrayEntry storagePlace =
            AcquireStoragePlace(widthLog2, heightLog2, scaledWidth, scaledHeight, storageBucket, useSubrectHandling,
                                effectiveScaleFactor);
        entry.Texture = storagePlace;
        lastMiss.SourceWidth = width;
        lastMiss.SourceHeight = height;
        lastMiss.ResultWidth = scaledWidth;
        lastMiss.ResultHeight = scaledHeight;

        const bool captureFrameTexture = !InDeferredPromotion && Debug.IsFrameTextureCaptureActive();
        const bool captureMissImages = !InDeferredPromotion && Debug.ConsumeLastMissImageCaptureRequest();
        const bool capturePreviewImages = captureMissImages || captureFrameTexture;
        u32* uploadData = DecodeBufferStorage.data();
        const u32* sourcePreviewData = nullptr;
        const u32* resultPreviewData = nullptr;
        bool uploadedDirectly = false;
        bool uploadedCustomMipChain = false;
        bool binaryAlphaTextureValue = false;
        bool binaryAlphaTextureKnown = false;
        bool sourceBinaryAlphaTextureValue = false;
        bool sourceBinaryAlphaTextureKnown = false;
        bool conservativeAtlasFallback = false;
        bool conservativeAtlasFallbackKnown = false;
        const u32* filterableMipSourceRGBA8 = nullptr;
        const u32* sourceMipNativeRGBA8 = nullptr;
        std::vector<u32> sourceMipNativeRGBA8Storage;
        TexcacheMipChain filterableMipChain;
        if (effectiveScaleFactor > 1)
        {
            bool usedCustomScaler = false;
            auto scalingAlgorithm = TexLoader.ScalingAlgorithm();
            const bool useGPUScaleAlgorithm = TextureScaleUsesGPUAlgorithm(scalingAlgorithm);
            if (!useSubrectHandling && !useFrequentChangeFallback && useGPUScaleAlgorithm)
            {
                bool binaryAlpha = false;
                u32* rgbaBuffer = DecodeRGBA8Buffer(width * height);
                TextureScaleDecodeSourceRGBA8(fmt, width, height, rgbaBuffer, addr, entry.TextureRAMStart[1],
                                              entry.TexPalStart, color0Transparent, vram);
                if (useMarginExtension)
                    EdgeExtendTextureMargins(width, height, rgbaBuffer, activeSamplingBounds);
                if (capturePreviewImages)
                    sourcePreviewData = rgbaBuffer;
                bool hasTransparentAlpha = false;
                binaryAlpha = TextureHasBinaryAlpha(width, height, rgbaBuffer, 255, &hasTransparentAlpha);
                binaryAlphaTextureValue = binaryAlpha && hasTransparentAlpha;
                binaryAlphaTextureKnown = true;
                sourceBinaryAlphaTextureValue = binaryAlphaTextureValue;
                sourceBinaryAlphaTextureKnown = true;
                if (binaryAlpha && hasTransparentAlpha && TexLoader.UseFilterableMipTopologyHandling())
                {
                    conservativeAtlasFallback = TextureMipShouldUseConservativeAtlasFallback(width, height, rgbaBuffer);
                    conservativeAtlasFallbackKnown = true;
                }
                bool useImprovedFilterableMipPath =
                    TexLoader.FilterableSamplingEnabled() &&
                    TexLoader.UseFilterableMipAlphaHandling() &&
                    (!TexLoader.UseFilterableMipTopologyHandling() || !conservativeAtlasFallback ||
                     useSubrectHandling) &&
                    binaryAlpha && hasTransparentAlpha;
                if (binaryAlpha && hasTransparentAlpha && !TexLoader.UseLegacyAlphaHandling() &&
                    (!useImprovedFilterableMipPath || effectiveScaleFactor > 1))
                {
                    if (TexLoader.UseQualityAlphaHandling())
                        PadTransparentTextureRGB(width, height, rgbaBuffer);
                    else
                        PadTransparentTextureRGBFast(width, height, rgbaBuffer);
                }
                sourceMipNativeRGBA8 = rgbaBuffer;

                if (!useImprovedFilterableMipPath &&
                    !(TexLoader.FilterableSamplingEnabled() && TexLoader.UseSourceMipScaling() && effectiveScaleFactor > 1 &&
                      (effectiveScaleFactor & (effectiveScaleFactor - 1)) == 0) &&
                    TexLoader.ProcessTextureGPUScaleToCacheLayer(width, height, effectiveScaleFactor, rgbaBuffer,
                        outputFmt, binaryAlpha, storagePlace.TextureID, storagePlace.Layer,
                        capturePreviewImages ? &ScaledRGBA8Storage : nullptr))
                {
                    if (capturePreviewImages && !ScaledRGBA8Storage.empty())
                        resultPreviewData = ScaledRGBA8Storage.data();
                    uploadData = nullptr;
                    uploadedDirectly = true;
                    usedCustomScaler = true;
                    lastMiss.UsedGPUScaler = true;
                    lastMiss.UsedGPUDirectRepack = true;
                    lastMiss.UsedXBRZ = scalingAlgorithm == RendererSettings::GLScaleAlgorithm::XBRZ;
                    lastMiss.UsedSpline36 = scalingAlgorithm == RendererSettings::GLScaleAlgorithm::Spline36;
                    Debug.CountEvent(&TextureScalingDebugFrameStats::ScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::GPUScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::GPUDirectRepackUploads);
                    if (lastMiss.UsedXBRZ)
                        Debug.CountEvent(&TextureScalingDebugFrameStats::XBRZScaledUploads);
                    if (lastMiss.UsedSpline36)
                        Debug.CountEvent(&TextureScalingDebugFrameStats::Spline36ScaledUploads);
                }
                else if (TexLoader.ProcessTextureGPUScale(width, height, effectiveScaleFactor, rgbaBuffer, ScaledRGBA8Storage))
                {
                    u32* scaledBuffer = ScaledBuffer(scaledWidth * scaledHeight);
                    TextureScalePackRGBA8ToOutput(outputFmt, scaledWidth, scaledHeight, ScaledRGBA8Storage.data(),
                                                  scaledBuffer, binaryAlpha);
                    uploadData = scaledBuffer;
                    resultPreviewData = ScaledRGBA8Storage.data();
                    filterableMipSourceRGBA8 = ScaledRGBA8Storage.data();
                    usedCustomScaler = true;
                    lastMiss.UsedGPUScaler = true;
                    lastMiss.UsedXBRZ = scalingAlgorithm == RendererSettings::GLScaleAlgorithm::XBRZ;
                    lastMiss.UsedSpline36 = scalingAlgorithm == RendererSettings::GLScaleAlgorithm::Spline36;
                    Debug.CountEvent(&TextureScalingDebugFrameStats::ScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::GPUScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::GPULegacyReadbackUploads);
                    if (lastMiss.UsedXBRZ)
                        Debug.CountEvent(&TextureScalingDebugFrameStats::XBRZScaledUploads);
                    if (lastMiss.UsedSpline36)
                        Debug.CountEvent(&TextureScalingDebugFrameStats::Spline36ScaledUploads);
                }
                else if (scalingAlgorithm == RendererSettings::GLScaleAlgorithm::XBRZ &&
                         TextureScaleRunXBRZ(width, height, effectiveScaleFactor, rgbaBuffer, ScaledRGBA8Storage))
                {
                    u32* scaledBuffer = ScaledBuffer(scaledWidth * scaledHeight);
                    TextureScalePackRGBA8ToOutput(outputFmt, scaledWidth, scaledHeight, ScaledRGBA8Storage.data(),
                                                  scaledBuffer, binaryAlpha);
                    uploadData = scaledBuffer;
                    resultPreviewData = ScaledRGBA8Storage.data();
                    filterableMipSourceRGBA8 = ScaledRGBA8Storage.data();
                    usedCustomScaler = true;
                    lastMiss.UsedXBRZ = true;
                    Debug.CountEvent(&TextureScalingDebugFrameStats::ScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::XBRZScaledUploads);
                }
            }

            if (!usedCustomScaler)
            {
                u32* scaledBuffer = ScaledBuffer(scaledWidth * scaledHeight);
                TextureScaleRunCPUFallback(outputFmt, width, height, DecodeBufferStorage.data(), effectiveScaleFactor,
                                           scaledBuffer, useFrequentChangeFallback);
                uploadData = scaledBuffer;
                if (useFrequentChangeFallback)
                {
                    entry.UsedFrequentChangeFallback = true;
                    lastMiss.UsedFrequentChangeFallback = true;
                    Debug.CountEvent(&TextureScalingDebugFrameStats::ScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::FrequentChangeFallbackUploads);
                }
                if (!useFrequentChangeFallback)
                {
                    lastMiss.UsedSpline36 = true;
                    Debug.CountEvent(&TextureScalingDebugFrameStats::ScaledUploads);
                    Debug.CountEvent(&TextureScalingDebugFrameStats::Spline36ScaledUploads);
                }
            }
        }
        else if (deferScaleOnMiss)
        {
            lastMiss.UsedDeferredUnscaledUpload = true;
            Debug.CountEvent(&TextureScalingDebugFrameStats::DeferredUnscaledUploads);
        }

        const bool sourceScaledMipLevels =
            TexLoader.FilterableSamplingEnabled() &&
            TexLoader.UseSourceMipScaling() &&
            effectiveScaleFactor > 1 &&
            (effectiveScaleFactor & (effectiveScaleFactor - 1)) == 0;

        if (!uploadedDirectly && uploadData &&
            TexLoader.FilterableSamplingEnabled() &&
            (TexLoader.UseFilterableMipAlphaHandling() || sourceScaledMipLevels))
        {
            if (sourceScaledMipLevels && sourceMipNativeRGBA8 == nullptr)
            {
                sourceMipNativeRGBA8Storage.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
                switch (outputFmt)
                {
                case outputFmt_RGB6A5:
                    ConvertOutputBufferToPreviewRGBA8<outputFmt_RGB6A5>(width, height, DecodeBufferStorage.data(),
                                                                        sourceMipNativeRGBA8Storage.data());
                    break;
                case outputFmt_RGBA8:
                    ConvertOutputBufferToPreviewRGBA8<outputFmt_RGBA8>(width, height, DecodeBufferStorage.data(),
                                                                       sourceMipNativeRGBA8Storage.data());
                    break;
                case outputFmt_BGRA8:
                    ConvertOutputBufferToPreviewRGBA8<outputFmt_BGRA8>(width, height, DecodeBufferStorage.data(),
                                                                       sourceMipNativeRGBA8Storage.data());
                    break;
                }
                sourceMipNativeRGBA8 = sourceMipNativeRGBA8Storage.data();
            }

            u32* mipRGBA8 = PreviewBuffer(static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight));
            if (filterableMipSourceRGBA8)
            {
                const size_t pixelCount = static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight);
                std::copy(filterableMipSourceRGBA8, filterableMipSourceRGBA8 + pixelCount, mipRGBA8);
                if (sourceBinaryAlphaTextureKnown && sourceBinaryAlphaTextureValue)
                {
                    for (size_t i = 0; i < pixelCount; i++)
                    {
                        u32 color = mipRGBA8[i];
                        if ((color >> 24) >= 128)
                            mipRGBA8[i] = color | 0xFF000000;
                        else
                            mipRGBA8[i] = 0;
                    }
                }
            }
            else
            {
                switch (outputFmt)
                {
                case outputFmt_RGB6A5:
                    ConvertOutputBufferToPreviewRGBA8<outputFmt_RGB6A5>(scaledWidth, scaledHeight, uploadData, mipRGBA8);
                    break;
                case outputFmt_RGBA8:
                    ConvertOutputBufferToPreviewRGBA8<outputFmt_RGBA8>(scaledWidth, scaledHeight, uploadData, mipRGBA8);
                    break;
                case outputFmt_BGRA8:
                    ConvertOutputBufferToPreviewRGBA8<outputFmt_BGRA8>(scaledWidth, scaledHeight, uploadData, mipRGBA8);
                    break;
                }
            }

            bool hasTransparentAlpha = false;
            bool binaryAlpha = TextureHasBinaryAlpha(scaledWidth, scaledHeight, mipRGBA8, 255, &hasTransparentAlpha);
            const bool filterableBinaryAlpha = binaryAlpha && hasTransparentAlpha;
            if (sourceBinaryAlphaTextureKnown)
            {
                binaryAlphaTextureValue = sourceBinaryAlphaTextureValue;
                binaryAlphaTextureKnown = true;
            }
            else
            {
                binaryAlphaTextureValue = filterableBinaryAlpha;
                binaryAlphaTextureKnown = true;
            }

            if (TextureMipBuildSourceScaledChain(
                    filterableMipChain,
                    TexLoader,
                    width,
                    height,
                    scaledWidth,
                    scaledHeight,
                    effectiveScaleFactor,
                    mipRGBA8,
                    sourceMipNativeRGBA8,
                    sourceScaledMipLevels,
                    sourceBinaryAlphaTextureKnown,
                    sourceBinaryAlphaTextureValue,
                    outputFmt))
            {
                uploadData = filterableMipChain.PackedLevels[0].data();
                uploadedCustomMipChain = true;

                if (capturePreviewImages)
                    resultPreviewData = filterableMipChain.PreviewLevels[0].data();
            }

            if (!uploadedCustomMipChain && binaryAlphaTextureValue)
            {
                const bool useTopologyAwareMipHandling = TexLoader.UseFilterableMipTopologyHandling();
                if (useTopologyAwareMipHandling && !conservativeAtlasFallbackKnown)
                {
                    conservativeAtlasFallback = TextureMipShouldUseConservativeAtlasFallback(scaledWidth, scaledHeight, mipRGBA8);
                    conservativeAtlasFallbackKnown = true;
                }
                const bool useAlphaAwareMipChain =
                    !useTopologyAwareMipHandling || !conservativeAtlasFallback || useSubrectHandling;
                const bool useImprovedMipColors = false;

                if (useAlphaAwareMipChain)
                {
                    if (TexLoader.UseQualityAlphaHandling())
                        PadTransparentTextureRGB(scaledWidth, scaledHeight, mipRGBA8);
                    else
                        PadTransparentTextureRGBFast(scaledWidth, scaledHeight, mipRGBA8);
                }

                if (useAlphaAwareMipChain)
                {
                    TextureMipBuildAlphaAwareChain(
                        filterableMipChain,
                        TexLoader,
                        scaledWidth,
                        scaledHeight,
                        effectiveScaleFactor,
                        mipRGBA8,
                        outputFmt,
                        useImprovedMipColors);
                    uploadData = filterableMipChain.PackedLevels[0].data();
                    uploadedCustomMipChain = true;

                    if (capturePreviewImages)
                        resultPreviewData = filterableMipChain.PreviewLevels[0].data();
                }
            }
        }

        if (!binaryAlphaTextureKnown && !uploadedDirectly && uploadData)
        {
            u32* preview = PreviewBuffer(static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight));
            switch (outputFmt)
            {
            case outputFmt_RGB6A5:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_RGB6A5>(scaledWidth, scaledHeight, uploadData, preview);
                break;
            case outputFmt_RGBA8:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_RGBA8>(scaledWidth, scaledHeight, uploadData, preview);
                break;
            case outputFmt_BGRA8:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_BGRA8>(scaledWidth, scaledHeight, uploadData, preview);
                break;
            }

            bool hasTransparentAlpha = false;
            bool binaryAlpha = TextureHasBinaryAlpha(scaledWidth, scaledHeight, preview, 255, &hasTransparentAlpha);
            binaryAlphaTextureValue = binaryAlpha && hasTransparentAlpha;
            binaryAlphaTextureKnown = true;
        }

        if (capturePreviewImages && !sourcePreviewData)
        {
            u32* preview = PreviewBuffer(width * height);
            switch (outputFmt)
            {
            case outputFmt_RGB6A5:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_RGB6A5>(width, height, DecodeBufferStorage.data(), preview);
                break;
            case outputFmt_RGBA8:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_RGBA8>(width, height, DecodeBufferStorage.data(), preview);
                break;
            case outputFmt_BGRA8:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_BGRA8>(width, height, DecodeBufferStorage.data(), preview);
                break;
            }
            lastMiss.SourceRGBA.assign(preview, preview + (static_cast<size_t>(width) * static_cast<size_t>(height)));
        }
        else if (capturePreviewImages && sourcePreviewData)
            lastMiss.SourceRGBA.assign(sourcePreviewData, sourcePreviewData + (static_cast<size_t>(width) * static_cast<size_t>(height)));

        if (capturePreviewImages && !resultPreviewData && uploadData)
        {
            u32* preview = PreviewBuffer(scaledWidth * scaledHeight);
            switch (outputFmt)
            {
            case outputFmt_RGB6A5:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_RGB6A5>(scaledWidth, scaledHeight, uploadData, preview);
                break;
            case outputFmt_RGBA8:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_RGBA8>(scaledWidth, scaledHeight, uploadData, preview);
                break;
            case outputFmt_BGRA8:
                ConvertOutputBufferToPreviewRGBA8<outputFmt_BGRA8>(scaledWidth, scaledHeight, uploadData, preview);
                break;
            }
            lastMiss.ResultRGBA.assign(preview, preview + (static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight)));
        }
        else if (capturePreviewImages && resultPreviewData)
            lastMiss.ResultRGBA.assign(resultPreviewData, resultPreviewData + (static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight)));

        if (!uploadedDirectly && uploadData)
        {
            if (uploadedCustomMipChain)
            {
                for (size_t level = 0; level < filterableMipChain.PackedLevels.size(); level++)
                {
                    TexLoader.UploadTextureLevel(storagePlace.TextureID,
                                                 filterableMipChain.Widths[level],
                                                 filterableMipChain.Heights[level],
                                                 storagePlace.Layer,
                                                 static_cast<u32>(level),
                                                 filterableMipChain.PackedLevels[level].data());
                }
            }
            else
            {
                TexLoader.UploadTexture(storagePlace.TextureID, scaledWidth, scaledHeight, storagePlace.Layer, uploadData);
            }
        }
        Debug.CountEvent(&TextureScalingDebugFrameStats::Uploads);
        Debug.CountPixels(&TextureScalingDebugFrameStats::SourcePixels, static_cast<u64>(width) * static_cast<u64>(height));
        Debug.CountPixels(&TextureScalingDebugFrameStats::UploadPixels, static_cast<u64>(scaledWidth) * static_cast<u64>(scaledHeight));
        //printf("using storage place %d %d | %d %d (%d)\n", width, height, storagePlace.TexArrayIdx, storagePlace.LayerIdx, array.ImageDescriptor);

        textureHandle = storagePlace.TextureID;
        layer = storagePlace.Layer;
        entry.BinaryAlphaTexture = binaryAlphaTextureValue;
        EvictOldPaletteVariantIfNeeded(baseKey, key);
        helper = &Cache.emplace(std::make_pair(key, entry)).first->second.LastVariant;
        if (binaryAlphaTexture)
            *binaryAlphaTexture = binaryAlphaTextureValue;
        TouchVariantKey(baseKey, key);
        if (captureFrameTexture)
        {
            TextureScalingDebugFrameTexture frameTexture = MakeDebugFrameTextureRecord(
                key, baseKey, rawTexParam, palBase, fmt, width, height, scaledWidth, scaledHeight,
                effectiveScaleFactor, activeSamplingBounds, addr, entry.TextureRAMStart[1],
                entry.TexPalStart, color0Transparent);
            frameTexture.CacheMiss = true;
            frameTexture.BinaryAlphaTexture = binaryAlphaTextureValue;
            frameTexture.DeferredScalePending = entry.DeferredScalePending;
            frameTexture.FrequentChangeHot = IsFrequentChangeHot(baseKey);
            frameTexture.UsedGPUScaler = lastMiss.UsedGPUScaler;
            frameTexture.UsedGPUDirectRepack = lastMiss.UsedGPUDirectRepack;
            frameTexture.UsedFrequentChangeFallback = lastMiss.UsedFrequentChangeFallback;
            frameTexture.UsedDeferredUnscaledUpload = lastMiss.UsedDeferredUnscaledUpload;
            frameTexture.UsedXBRZ = lastMiss.UsedXBRZ;
            frameTexture.UsedSpline36 = lastMiss.UsedSpline36;
            frameTexture.SourceRGBA = lastMiss.SourceRGBA;
            frameTexture.ResultRGBA = lastMiss.ResultRGBA;
            if (frameTexture.ResultRGBA.empty())
            {
                TexLoader.ReadTextureLayerPreviewRGBA8(storagePlace.TextureID,
                                                       storagePlace.Layer,
                                                       scaledWidth,
                                                       scaledHeight,
                                                       outputFmt,
                                                       frameTexture.ResultRGBA);
            }
            if (!frameTexture.SourceRGBA.empty())
            {
                bool hasTransparentAlpha = false;
                frameTexture.SourceBinaryAlpha = TextureHasBinaryAlpha(frameTexture.SourceWidth,
                                                                       frameTexture.SourceHeight,
                                                                       frameTexture.SourceRGBA.data(),
                                                                       255,
                                                                       &hasTransparentAlpha);
                frameTexture.SourceHasTransparentAlpha = hasTransparentAlpha;
            }
            Debug.RecordFrameTexture(std::move(frameTexture));
        }
        if (!InDeferredPromotion)
            Debug.RecordLastMiss(std::move(lastMiss));
    }

    void Reset()
    {
        for (const auto& cacheEntry : Cache)
        {
            if (cacheEntry.second.Texture.Dedicated)
                TexLoader.DeleteTexture(cacheEntry.second.Texture.TextureID);
        }

        for (u32 i = 0; i < 8; i++)
        {
            for (u32 j = 0; j < 8; j++)
            {
                for (u32 bucket = 0; bucket < 2; bucket++)
                {
                    for (u32 k = 0; k < TexArrays[i][j][bucket].size(); k++)
                        TexLoader.DeleteTexture(TexArrays[i][j][bucket][k]);
                    TexArrays[i][j][bucket].clear();
                    FreeTextures[i][j][bucket].clear();
                }
            }
        }
        Cache.clear();
        SecondaryCache.clear();
        SecondaryCacheTexels = 0;
        FrequentChangeByBase.clear();
        VariantKeysByBase.clear();
        DeferredScaleQueue.clear();
        Debug.ResetCacheState();
        EdgeExtendNewVariantsThisFrame = 0;
        EdgeExtendThrottledVariantsThisFrame = 0;
        FrequentChangeFallbackPromotionsThisFrame = 0;
        FrequentChangeFallbackPromotionTexelsThisFrame = 0;
        InFrequentChangeFallbackPromotion = false;
        FrameIndex = 0;
    }

    void GetDebugStats(TextureScalingDebugStats& stats, bool usesComputeBackend, bool readableTextureCache) const
    {
        Debug.GetStats(
            stats,
            usesComputeBackend,
            readableTextureCache,
            TextureScaleFactor > 1,
            FrequentChangePolicyEnabled,
            DeferredScalingEnabled,
            TexLoader.UseLegacyAlphaHandling(),
            TexLoader.UseQualityAlphaHandling(),
            RendererSettings::GetGLScaleAlgorithmIndex(TexLoader.ScalingAlgorithm()),
            TextureScaleFactor,
            static_cast<u32>(Cache.size()),
            static_cast<u32>(SecondaryCache.size()),
            static_cast<u32>(SecondaryCacheMaxEntries),
            SecondaryCacheTexels,
            SecondaryCacheMaxTexels,
            CountFreeLayers(),
            CountTextureArrays());
    }

    void ResetDebugStats()
    {
        Debug.ResetCounters();
    }

    void GetDebugLastMiss(TextureScalingDebugLastMiss& miss)
    {
        Debug.GetLastMiss(miss);
    }

    void SetDebugLastMissImageCaptureEnabled(bool enabled)
    {
        Debug.SetLastMissImageCaptureEnabled(enabled);
    }

    void GetDebugFrameTextures(TextureScalingDebugFrameTextures& frame)
    {
        Debug.GetFrameTextures(frame);
    }

    void SetDebugFrameTextureCaptureEnabled(bool enabled)
    {
        Debug.SetFrameTextureCaptureEnabled(enabled);
    }

    void FinishDebugFrameTextureCapture()
    {
        Debug.FinishFrameTextureCapture();
    }

    u32 GetEdgeExtendNewVariantsThisFrame() const
    {
        return EdgeExtendNewVariantsThisFrame;
    }

    u32 GetEdgeExtendThrottledVariantsThisFrame() const
    {
        return EdgeExtendThrottledVariantsThisFrame;
    }

    bool ApplyTextureSettings(u32 renderScaleFactor,
                              const RendererSettings::TextureFilterSettings& filter,
                              const RendererSettings::TextureScalingSettings& scaling)
    {
        const u32 textureScaleFactor = scaling.Enabled ? renderScaleFactor : 1;
        bool changed = false;

        changed |= SetScaleFactor(textureScaleFactor);
        changed |= SetScalingAlgorithm(scaling.Algorithm);
        changed |= SetFrequentChangePolicyEnabled(scaling.FrequentChangePolicy);
        changed |= SetDeferredScalingEnabled(scaling.Deferred);
        changed |= SetNativeMipFloor(scaling.NativeMipFloor);
        changed |= SetSourceMipScaling(scaling.SourceMips);
        changed |= SetTextureScalingEdgeExtendUnusedMargins(scaling.EdgeExtendUnusedMargins);
        changed |= SetLegacyAlphaHandling(scaling.LegacyAlphaHandling);
        changed |= SetQualityAlphaHandling(scaling.QualityAlphaHandling);
        changed |= SetLosslessRGB6Repack(filter.LosslessRGB6Repack);
        changed |= SetFilterableMipTopologyHandling(filter.TopologyAwareMipHandling);
        changed |= SetFilterableMipSubrectHandling(filter.MipmapSubrectHandling);
        changed |= SetFilterableMipAlphaHandling(filter.MipmapAlphaHandling);
        changed |= SetFilterableMipDepth(filter.MipDepth);
        changed |= SetFilterableSampling(filter.Anisotropy > 1);

        if (changed)
            Reset();

        return changed;
    }

    bool SetPreferredOutputFormat(int outputFmt)
    {
        return TexLoader.SetPreferredOutputFormat(outputFmt);
    }
    bool SetFilterableSampling(bool enabled)
    {
        return TexLoader.SetFilterableSampling(enabled);
    }
    bool SetFilterableMipAlphaHandling(bool enabled)
    {
        return TexLoader.SetFilterableMipAlphaHandling(enabled);
    }
    bool SetFilterableMipTopologyHandling(bool enabled)
    {
        return TexLoader.SetFilterableMipTopologyHandling(enabled);
    }
    bool SetFilterableMipSubrectHandling(bool enabled)
    {
        return TexLoader.SetFilterableMipSubrectHandling(enabled);
    }
    bool SetFilterableMipDepth(RendererSettings::TextureFilterMipDepth mipDepth)
    {
        return TexLoader.SetFilterableMipDepth(mipDepth);
    }
    bool SetNativeMipFloor(bool enabled)
    {
        return TexLoader.SetNativeMipFloor(enabled);
    }
    bool SetSourceMipScaling(bool enabled)
    {
        return TexLoader.SetSourceMipScaling(enabled);
    }
    bool SetTextureScalingEdgeExtendUnusedMargins(bool enabled)
    {
        if (TextureScalingEdgeExtendUnusedMargins == enabled)
            return false;
        TextureScalingEdgeExtendUnusedMargins = enabled;
        return true;
    }
    bool SetScalingAlgorithm(RendererSettings::GLScaleAlgorithm algorithm)
    {
        return TexLoader.SetScalingAlgorithm(algorithm);
    }
    bool SetFrequentChangePolicyEnabled(bool enabled)
    {
        if (FrequentChangePolicyEnabled == enabled)
            return false;
        FrequentChangePolicyEnabled = enabled;
        return true;
    }
    bool SetDeferredScalingEnabled(bool enabled)
    {
        if (DeferredScalingEnabled == enabled)
            return false;
        DeferredScalingEnabled = enabled;
        return true;
    }
    bool SetQualityAlphaHandling(bool qualityAlphaHandling)
    {
        return TexLoader.SetQualityAlphaHandling(qualityAlphaHandling);
    }
    bool SetLosslessRGB6Repack(bool losslessRGB6Repack)
    {
        return TexLoader.SetLosslessRGB6Repack(losslessRGB6Repack);
    }
    bool SetLegacyAlphaHandling(bool legacyAlphaHandling)
    {
        return TexLoader.SetLegacyAlphaHandling(legacyAlphaHandling);
    }

    bool SetScaleFactor(u32 scaleFactor)
    {
        if (TextureScaleFactor == scaleFactor)
            return false;
        TextureScaleFactor = std::max<u32>(1, scaleFactor);
        return true;
    }

    u32 ScaleDimension(u32 dimension) const
    {
        return dimension * TextureScaleFactor;
    }

private:
    static constexpr size_t MaxPaletteVariantsPerBaseKey = 16;
    static constexpr u8 StorageBucketScaled = 0;
    static constexpr u8 StorageBucketNative = 1;
    static constexpr u64 FrequentChangeWindowFrames = 6;
    static constexpr u32 FrequentChangeThreshold = 3;
    static constexpr u64 FrequentChangeCooldownFrames = 12;
    static constexpr u32 FrequentChangeFallbackPromotionBudgetPerFrame = 2;
    static constexpr u64 FrequentChangeFallbackPromotionSourceTexelBudget = 32 * 1024;
    static constexpr u32 DeferredScaleMinUses = 2;
    static constexpr u64 DeferredScaleStableFrames = 8;
    static constexpr u32 DeferredScaleBudgetPerFrame = 1;
    static constexpr size_t SecondaryCacheMaxEntries = 32;
    static constexpr size_t SecondaryCacheMaxEntriesPerBase = 4;
    static constexpr u64 SecondaryCacheMaxTexels = 16 * 1024 * 1024;

    TextureVRAMView VRAMView() const
    {
        return { GPU.VRAMFlat_Texture, GPU.VRAMFlat_TexPal };
    }

    TextureScalingDebugFrameTexture MakeDebugFrameTextureRecord(u64 key,
                                                                u64 baseKey,
                                                                u32 rawTexParam,
                                                                u32 palBase,
                                                                u32 fmt,
                                                                u32 width,
                                                                u32 height,
                                                                u32 scaledWidth,
                                                                u32 scaledHeight,
                                                                u32 effectiveScaleFactor,
                                                                const TextureSamplingBounds& samplingBounds,
                                                                u32 addr,
                                                                u32 slot1Addr,
                                                                u32 texPalStart,
                                                                bool color0Transparent)
    {
        TextureScalingDebugFrameTexture texture = {};
        texture.Valid = true;
        texture.Key = key;
        texture.BaseKey = baseKey;
        texture.TexParam = rawTexParam;
        texture.PalBase = palBase;
        texture.SourceWidth = width;
        texture.SourceHeight = height;
        texture.ResultWidth = scaledWidth;
        texture.ResultHeight = scaledHeight;
        texture.ScaleFactor = effectiveScaleFactor;
        texture.UsedScaling = effectiveScaleFactor > 1;
        texture.AlgorithmIndex = RendererSettings::GetGLScaleAlgorithmIndex(TexLoader.ScalingAlgorithm());
        texture.Format = fmt;
        texture.RepeatMode = (rawTexParam >> 16) & 0xF;
        texture.ReferenceCount = 1;
        texture.SamplingBoundsValid = samplingBounds.Valid;
        texture.SamplingBoundsEdgeExtendMargins = samplingBounds.Valid && samplingBounds.EdgeExtendMargins;
        if (samplingBounds.Valid)
        {
            texture.SamplingX0 = samplingBounds.X0;
            texture.SamplingY0 = samplingBounds.Y0;
            texture.SamplingX1 = samplingBounds.X1;
            texture.SamplingY1 = samplingBounds.Y1;
        }

        if (!Debug.HasFrameTexture(key) && (!samplingBounds.Valid || samplingBounds.EdgeExtendMargins) &&
            width > 0 && height > 0)
        {
            texture.SourceRGBA.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            TextureScaleDecodeSourceRGBA8(fmt, width, height, texture.SourceRGBA.data(), addr, slot1Addr,
                                          texPalStart, color0Transparent, VRAMView());
            if (samplingBounds.Valid && samplingBounds.EdgeExtendMargins)
                EdgeExtendTextureMargins(width, height, texture.SourceRGBA.data(), samplingBounds);
            bool hasTransparentAlpha = false;
            texture.SourceBinaryAlpha = TextureHasBinaryAlpha(width, height, texture.SourceRGBA.data(),
                                                              255, &hasTransparentAlpha);
            texture.SourceHasTransparentAlpha = hasTransparentAlpha;
        }

        return texture;
    }

    void ReleaseStoragePlace(const TexCacheEntry& entry)
    {
        if (entry.Texture.Dedicated)
            TexLoader.DeleteTexture(entry.Texture.TextureID);
        else
            FreeTextures[entry.WidthLog2][entry.HeightLog2][entry.StorageBucket].push_back(entry.Texture);
    }

    static bool NormalizeSamplingBounds(u32 width, u32 height, const TextureSamplingBounds* src, TextureSamplingBounds& dst)
    {
        if (!src || !src->Valid)
            return false;

        u32 x0 = std::min<u32>(src->X0, width);
        u32 y0 = std::min<u32>(src->Y0, height);
        u32 x1 = std::min<u32>(src->X1, width);
        u32 y1 = std::min<u32>(src->Y1, height);
        if (x0 >= x1 || y0 >= y1)
            return false;
        if (x0 == 0 && y0 == 0 && x1 == width && y1 == height)
            return false;

        dst.Valid = true;
        dst.EdgeExtendMargins = src->EdgeExtendMargins;
        dst.X0 = static_cast<u16>(x0);
        dst.Y0 = static_cast<u16>(y0);
        dst.X1 = static_cast<u16>(x1);
        dst.Y1 = static_cast<u16>(y1);
        return true;
    }

    static void CropTexture(u32 width, u32 height, const u32* src, const TextureSamplingBounds& bounds, std::vector<u32>& dst)
    {
        const u32 cropWidth = bounds.X1 - bounds.X0;
        const u32 cropHeight = bounds.Y1 - bounds.Y0;
        dst.resize(static_cast<size_t>(cropWidth) * static_cast<size_t>(cropHeight));

        for (u32 y = 0; y < cropHeight; y++)
        {
            const u32* srcRow = &src[static_cast<size_t>(bounds.Y0 + y) * width + bounds.X0];
            u32* dstRow = &dst[static_cast<size_t>(y) * cropWidth];
            std::copy(srcRow, srcRow + cropWidth, dstRow);
        }
    }

    static void EdgeExtendTextureMargins(u32 width, u32 height, u32* data, const TextureSamplingBounds& bounds)
    {
        if (!bounds.Valid || !bounds.EdgeExtendMargins)
            return;

        const u32 x0 = std::min<u32>(bounds.X0, width);
        const u32 y0 = std::min<u32>(bounds.Y0, height);
        const u32 x1 = std::min<u32>(bounds.X1, width);
        const u32 y1 = std::min<u32>(bounds.Y1, height);
        if (x0 >= x1 || y0 >= y1)
            return;
        if (x0 == 0 && y0 == 0 && x1 == width && y1 == height)
            return;

        for (u32 y = 0; y < height; y++)
        {
            const u32 srcY = std::min<u32>(std::max<u32>(y, y0), y1 - 1);
            for (u32 x = 0; x < width; x++)
            {
                const u32 srcX = std::min<u32>(std::max<u32>(x, x0), x1 - 1);
                if (srcX == x && srcY == y)
                    continue;

                data[static_cast<size_t>(y) * width + x] =
                    data[static_cast<size_t>(srcY) * width + srcX];
            }
        }
    }

    static u64 MakeSubrectKey(const TextureSamplingBounds& bounds)
    {
        u64 payload =
            (static_cast<u64>(bounds.X0) << 0) |
            (static_cast<u64>(bounds.Y0) << 16) |
            (static_cast<u64>(bounds.X1) << 32) |
            (static_cast<u64>(bounds.Y1) << 48);
        if (bounds.EdgeExtendMargins)
            payload |= 0x8000000000000000ULL;
        return payload;
    }

    bool IsDeferredScaleReady(const TexCacheEntry& entry) const
    {
        if (!DeferredScalingEnabled || TextureScaleFactor <= 1 || !entry.DeferredScalePending)
            return false;
        if (entry.DeferredUseCount < DeferredScaleMinUses)
            return false;
        if (FrameIndex - entry.DeferredFirstSeenFrame < DeferredScaleStableFrames)
            return false;
        if (FrequentChangePolicyEnabled && IsFrequentChangeHot(entry.BaseKey))
            return false;
        return true;
    }

    void QueueDeferredScale(u64 key, TexCacheEntry& entry)
    {
        if (entry.DeferredScaleQueued)
            return;
        entry.DeferredScaleQueued = true;
        DeferredScaleQueue.push_back(key);
    }

    bool ShouldPromoteFrequentChangeFallbackEntry(const TexCacheEntry& entry) const
    {
        if (!entry.UsedFrequentChangeFallback || InFrequentChangeFallbackPromotion)
            return false;
        if (entry.DeferredScalePending || TextureScaleFactor <= 1)
            return false;
        if (entry.CreatedFrame >= FrameIndex)
            return false;
        if (FrequentChangeFallbackPromotionsThisFrame >= FrequentChangeFallbackPromotionBudgetPerFrame)
            return false;

        const u64 sourceTexels = static_cast<u64>(entry.SourceWidth) * static_cast<u64>(entry.SourceHeight);
        if (FrequentChangeFallbackPromotionsThisFrame > 0 &&
            FrequentChangeFallbackPromotionTexelsThisFrame + sourceTexels >
                FrequentChangeFallbackPromotionSourceTexelBudget)
            return false;

        return true;
    }

    void TrackFrequentChangeInvalidation(u64 baseKey)
    {
        if (!FrequentChangePolicyEnabled)
            return;

        FrequentChangeEntry& entry = FrequentChangeByBase[baseKey];
        if (entry.LastInvalidationFrame != 0 &&
            FrameIndex - entry.LastInvalidationFrame > FrequentChangeCooldownFrames)
            entry = {};

        if (entry.LastInvalidationFrame != 0 &&
            FrameIndex - entry.LastInvalidationFrame <= FrequentChangeWindowFrames)
            entry.BurstCount++;
        else
            entry.BurstCount = 1;

        entry.LastInvalidationFrame = FrameIndex;
        if (entry.BurstCount >= FrequentChangeThreshold)
            entry.Hot = true;
    }

    bool ProcessDeferredScaleQueue()
    {
        if (!DeferredScalingEnabled || DeferredScaleQueue.empty() || TextureScaleFactor <= 1)
            return false;

        bool changed = false;
        u32 promotions = 0;
        std::vector<u64> remaining;
        remaining.reserve(DeferredScaleQueue.size());

        for (u64 key : DeferredScaleQueue)
        {
            auto it = Cache.find(key);
            if (it == Cache.end())
                continue;

            TexCacheEntry& entry = it->second;
            if (!entry.DeferredScalePending)
                continue;

            if (promotions >= DeferredScaleBudgetPerFrame || !IsDeferredScaleReady(entry))
            {
                entry.DeferredScaleQueued = true;
                remaining.push_back(key);
                continue;
            }

            const TexCacheEntry oldEntry = entry;
            ReleaseStoragePlace(oldEntry);
            RemoveVariantKey(oldEntry.BaseKey, key);
            Cache.erase(it);
            FrequentChangeByBase.erase(oldEntry.BaseKey);

            TexHandleT textureHandle = {};
            u32 layer = 0;
            u32* helper = nullptr;
            InDeferredPromotion = true;
            GetTexture(oldEntry.RawTexParam, oldEntry.PalBase, textureHandle, layer, helper);
            InDeferredPromotion = false;

            Debug.CountEvent(&TextureScalingDebugFrameStats::DeferredPromotionUploads);
            promotions++;
            changed = true;
        }

        DeferredScaleQueue.swap(remaining);
        return changed;
    }

    bool PromoteStableFrequentChangeEntries()
    {
        if (!FrequentChangePolicyEnabled)
            return false;

        bool changed = false;
        for (auto it = FrequentChangeByBase.begin(); it != FrequentChangeByBase.end();)
        {
            if (!it->second.Hot || it->second.LastInvalidationFrame == 0 ||
                FrameIndex - it->second.LastInvalidationFrame <= FrequentChangeCooldownFrames)
            {
                ++it;
                continue;
            }

            auto variantsIt = VariantKeysByBase.find(it->first);
            if (variantsIt != VariantKeysByBase.end())
            {
                for (u64 key : variantsIt->second)
                {
                    auto cacheIt = Cache.find(key);
                    if (cacheIt == Cache.end())
                        continue;

                    const TexCacheEntry& entry = cacheIt->second;
                    ReleaseStoragePlace(entry);
                    Cache.erase(cacheIt);
                    changed = true;
                }
                VariantKeysByBase.erase(variantsIt);
            }

            it = FrequentChangeByBase.erase(it);
        }

        return changed;
    }

    bool ShouldUseFrequentChangeFallback(u64 baseKey)
    {
        if (!FrequentChangePolicyEnabled || TextureScaleFactor <= 1)
            return false;

        auto it = FrequentChangeByBase.find(baseKey);
        if (it == FrequentChangeByBase.end())
            return false;

        if (it->second.LastInvalidationFrame != 0 &&
            FrameIndex - it->second.LastInvalidationFrame > FrequentChangeCooldownFrames)
        {
            FrequentChangeByBase.erase(it);
            return false;
        }

        return it->second.Hot && !it->second.SecondaryCacheReuseSeen;
    }

    bool IsFrequentChangeHot(u64 baseKey) const
    {
        if (!FrequentChangePolicyEnabled || TextureScaleFactor <= 1)
            return false;

        auto it = FrequentChangeByBase.find(baseKey);
        if (it == FrequentChangeByBase.end())
            return false;

        if (it->second.LastInvalidationFrame != 0 &&
            FrameIndex - it->second.LastInvalidationFrame > FrequentChangeCooldownFrames)
            return false;

        return it->second.Hot;
    }

    u64 MakeBaseKey(u32 texParam, u32 fmt) const
    {
        u64 key = texParam;
        if (fmt == 5)
            key &= ~((u64)1 << 29);
        return key;
    }

    u64 MakeVariantKey(u64 baseKey, u64 texPalHash, bool hasPalette, u64 subrectKey) const
    {
        if (!hasPalette && subrectKey == 0)
            return baseKey;

        const u64 payload[3] = {baseKey, hasPalette ? texPalHash : 0, subrectKey};
        return XXH64(payload, sizeof(payload), 0);
    }

    void RemoveVariantKey(u64 baseKey, u64 key)
    {
        auto variantsIt = VariantKeysByBase.find(baseKey);
        if (variantsIt == VariantKeysByBase.end())
            return;

        auto& variants = variantsIt->second;
        variants.erase(std::remove(variants.begin(), variants.end(), key), variants.end());
        if (variants.empty())
            VariantKeysByBase.erase(variantsIt);
    }

    void TouchVariantKey(u64 baseKey, u64 key)
    {
        auto& variants = VariantKeysByBase[baseKey];
        variants.erase(std::remove(variants.begin(), variants.end(), key), variants.end());
        variants.push_back(key);
    }

    void EvictOldPaletteVariantIfNeeded(u64 baseKey, u64 keepKey)
    {
        auto variantsIt = VariantKeysByBase.find(baseKey);
        if (variantsIt == VariantKeysByBase.end())
            return;

        auto& variants = variantsIt->second;
        while (variants.size() >= MaxPaletteVariantsPerBaseKey)
        {
            u64 victimKey = variants.front();
            variants.erase(variants.begin());
            if (victimKey == keepKey)
                continue;

            auto cacheIt = Cache.find(victimKey);
            if (cacheIt == Cache.end())
                continue;

            const TexCacheEntry& victim = cacheIt->second;
            ReleaseStoragePlace(victim);
            Cache.erase(cacheIt);
        }

        if (variants.empty())
            VariantKeysByBase.erase(variantsIt);
    }

    u64 MakeSecondaryCacheKey(u64 key, const TexCacheEntry& entry) const
    {
        const u64 dimensions =
            (static_cast<u64>(entry.SourceWidth) << 48) |
            (static_cast<u64>(entry.SourceHeight) << 32) |
            (static_cast<u64>(entry.ResultWidth) << 16) |
            static_cast<u64>(entry.ResultHeight);
        const u64 settings =
            (static_cast<u64>(entry.ScaleFactor) << 32) |
            static_cast<u32>(entry.OutputFormat);
        const u64 payload[6] =
        {
            key,
            entry.TextureHash[0],
            entry.TextureHash[1],
            entry.TexPalHash,
            dimensions,
            settings
        };
        return XXH64(payload, sizeof(payload), 0);
    }

    u64 EstimateSecondaryCacheTexels(const TexCacheEntry& entry) const
    {
        return static_cast<u64>(entry.ResultWidth) * static_cast<u64>(entry.ResultHeight);
    }

    bool CanUseSecondaryCache(const TexCacheEntry& entry) const
    {
        return FrequentChangePolicyEnabled &&
            TextureScaleFactor > 1 &&
            entry.ScaleFactor > 1 &&
            entry.StorageBucket == StorageBucketScaled &&
            !entry.DeferredScalePending &&
            !entry.Texture.Dedicated &&
            entry.ResultWidth > 0 &&
            entry.ResultHeight > 0;
    }

    size_t CountSecondaryCacheEntriesForBase(u64 baseKey) const
    {
        size_t count = 0;
        for (const auto& cacheEntry : SecondaryCache)
        {
            if (cacheEntry.second.Entry.BaseKey == baseKey)
                count++;
        }
        return count;
    }

    bool EvictOldestSecondaryCacheEntry(u64 baseKey, bool filterBase)
    {
        auto victim = SecondaryCache.end();
        for (auto it = SecondaryCache.begin(); it != SecondaryCache.end(); ++it)
        {
            if (filterBase && it->second.Entry.BaseKey != baseKey)
                continue;
            if (victim == SecondaryCache.end() || it->second.LastUsedFrame < victim->second.LastUsedFrame)
                victim = it;
        }

        if (victim == SecondaryCache.end())
            return false;

        SecondaryCacheTexels -= std::min(SecondaryCacheTexels, victim->second.TexelCost);
        ReleaseStoragePlace(victim->second.Entry);
        SecondaryCache.erase(victim);
        Debug.CountEvent(&TextureScalingDebugFrameStats::SecondaryCacheEvictions);
        return true;
    }

    void TrimSecondaryCache(u64 baseKey)
    {
        while (CountSecondaryCacheEntriesForBase(baseKey) > SecondaryCacheMaxEntriesPerBase)
        {
            if (!EvictOldestSecondaryCacheEntry(baseKey, true))
                break;
        }

        while (SecondaryCache.size() > SecondaryCacheMaxEntries ||
               SecondaryCacheTexels > SecondaryCacheMaxTexels)
        {
            if (!EvictOldestSecondaryCacheEntry(0, false))
                break;
        }
    }

    bool ArchiveSecondaryCacheEntry(u64 key, const TexCacheEntry& entry)
    {
        if (!CanUseSecondaryCache(entry) ||
            entry.UsedFrequentChangeFallback ||
            !IsFrequentChangeHot(entry.BaseKey))
            return false;

        const u64 secondaryKey = MakeSecondaryCacheKey(key, entry);
        auto existing = SecondaryCache.find(secondaryKey);
        if (existing != SecondaryCache.end())
        {
            SecondaryCacheTexels -= std::min(SecondaryCacheTexels, existing->second.TexelCost);
            ReleaseStoragePlace(existing->second.Entry);
            SecondaryCache.erase(existing);
            Debug.CountEvent(&TextureScalingDebugFrameStats::SecondaryCacheEvictions);
        }

        SecondaryCacheEntry secondaryEntry = {};
        secondaryEntry.Entry = entry;
        secondaryEntry.LastUsedFrame = FrameIndex;
        secondaryEntry.TexelCost = EstimateSecondaryCacheTexels(entry);
        SecondaryCacheTexels += secondaryEntry.TexelCost;
        SecondaryCache.emplace(secondaryKey, secondaryEntry);
        Debug.CountEvent(&TextureScalingDebugFrameStats::SecondaryCacheStores);
        TrimSecondaryCache(entry.BaseKey);
        return true;
    }

    bool SecondaryCacheEntryMatches(const TexCacheEntry& cached, const TexCacheEntry& probe) const
    {
        return cached.BaseKey == probe.BaseKey &&
            cached.SourceWidth == probe.SourceWidth &&
            cached.SourceHeight == probe.SourceHeight &&
            cached.ResultWidth == probe.ResultWidth &&
            cached.ResultHeight == probe.ResultHeight &&
            cached.ScaleFactor == probe.ScaleFactor &&
            cached.OutputFormat == probe.OutputFormat &&
            cached.WidthLog2 == probe.WidthLog2 &&
            cached.HeightLog2 == probe.HeightLog2 &&
            cached.StorageBucket == probe.StorageBucket;
    }

    bool TryRestoreSecondaryCacheEntry(u64 key,
                                       const TexCacheEntry& probe,
                                       TexHandleT& textureHandle,
                                       u32& layer,
                                       u32*& helper,
                                       bool* binaryAlphaTexture,
                                       const TextureSamplingBounds& activeSamplingBounds,
                                       u32 addr,
                                       u32 fmt,
                                       bool color0Transparent)
    {
        if (!CanUseSecondaryCache(probe))
            return false;

        const u64 secondaryKey = MakeSecondaryCacheKey(key, probe);
        auto secondaryIt = SecondaryCache.find(secondaryKey);
        if (secondaryIt == SecondaryCache.end())
            return false;

        if (!SecondaryCacheEntryMatches(secondaryIt->second.Entry, probe))
            return false;

        TexCacheEntry restored = secondaryIt->second.Entry;
        SecondaryCacheTexels -= std::min(SecondaryCacheTexels, secondaryIt->second.TexelCost);
        SecondaryCache.erase(secondaryIt);

        const TexArrayEntry restoredTexture = restored.Texture;
        const bool restoredBinaryAlphaTexture = restored.BinaryAlphaTexture;
        const u32 restoredLastVariant = restored.LastVariant;
        restored = probe;
        restored.Texture = restoredTexture;
        restored.BinaryAlphaTexture = restoredBinaryAlphaTexture;
        restored.LastVariant = restoredLastVariant;
        restored.UsedFrequentChangeFallback = false;
        restored.DeferredScalePending = false;
        restored.DeferredScaleQueued = false;

        auto inserted = Cache.emplace(std::make_pair(key, restored));
        TexCacheEntry& entry = inserted.first->second;
        TouchVariantKey(entry.BaseKey, key);

        textureHandle = entry.Texture.TextureID;
        layer = entry.Texture.Layer;
        helper = &entry.LastVariant;
        if (binaryAlphaTexture)
            *binaryAlphaTexture = entry.BinaryAlphaTexture;

        Debug.CountEvent(&TextureScalingDebugFrameStats::SecondaryCacheHits);
        FrequentChangeByBase[entry.BaseKey].SecondaryCacheReuseSeen = true;

        if (!InDeferredPromotion && Debug.IsFrameTextureCaptureActive())
        {
            TextureScalingDebugFrameTexture frameTexture = MakeDebugFrameTextureRecord(
                key, entry.BaseKey, entry.RawTexParam, entry.PalBase, fmt, entry.SourceWidth, entry.SourceHeight,
                entry.ResultWidth, entry.ResultHeight, entry.ScaleFactor, activeSamplingBounds, addr,
                entry.TextureRAMStart[1], entry.TexPalStart, color0Transparent);
            frameTexture.CacheHit = true;
            frameTexture.SecondaryCacheHit = true;
            frameTexture.BinaryAlphaTexture = entry.BinaryAlphaTexture;
            frameTexture.FrequentChangeHot = IsFrequentChangeHot(entry.BaseKey);
            TexLoader.ReadTextureLayerPreviewRGBA8(entry.Texture.TextureID,
                                                   entry.Texture.Layer,
                                                   entry.ResultWidth,
                                                   entry.ResultHeight,
                                                   entry.OutputFormat,
                                                   frameTexture.ResultRGBA);
            Debug.RecordFrameTexture(std::move(frameTexture));
        }

        return true;
    }

    u32 CountFreeLayers() const
    {
        u32 count = 0;
        for (u32 i = 0; i < 8; i++)
        {
            for (u32 j = 0; j < 8; j++)
            {
                count += static_cast<u32>(FreeTextures[i][j][StorageBucketScaled].size());
                count += static_cast<u32>(FreeTextures[i][j][StorageBucketNative].size());
            }
        }
        return count;
    }

    u32 CountTextureArrays() const
    {
        u32 count = 0;
        for (u32 i = 0; i < 8; i++)
        {
            for (u32 j = 0; j < 8; j++)
            {
                count += static_cast<u32>(TexArrays[i][j][StorageBucketScaled].size());
                count += static_cast<u32>(TexArrays[i][j][StorageBucketNative].size());
            }
        }
        return count;
    }

    u32* DecodeBuffer(size_t pixels)
    {
        DecodeBufferStorage.resize(pixels);
        return DecodeBufferStorage.data();
    }

    u32* DecodeRGBA8Buffer(size_t pixels)
    {
        DecodeRGBA8Storage.resize(pixels);
        return DecodeRGBA8Storage.data();
    }

    u32* ScaledBuffer(size_t pixels)
    {
        ScaledBufferStorage.resize(pixels);
        return ScaledBufferStorage.data();
    }

    u32* PreviewBuffer(size_t pixels)
    {
        PreviewRGBA8Storage.resize(pixels);
        return PreviewRGBA8Storage.data();
    }

    melonDS::GPU& GPU;

    std::unordered_map<u64, TexCacheEntry> Cache;
    std::unordered_map<u64, SecondaryCacheEntry> SecondaryCache;
    std::unordered_map<u64, FrequentChangeEntry> FrequentChangeByBase;
    std::unordered_map<u64, std::vector<u64>> VariantKeysByBase;

    TexLoaderT TexLoader;

    std::vector<TexArrayEntry> FreeTextures[8][8][2];
    std::vector<TexHandleT> TexArrays[8][8][2];
    std::vector<u64> DeferredScaleQueue;
    std::vector<u32> DecodeBufferStorage;
    std::vector<u32> DecodeRGBA8Storage;
    std::vector<u32> CroppedBufferStorage;
    std::vector<u32> ScaledBufferStorage;
    std::vector<u32> ScaledRGBA8Storage;
    std::vector<u32> PreviewRGBA8Storage;
    u64 SecondaryCacheTexels = 0;
    u32 TextureScaleFactor = 1;
    bool FrequentChangePolicyEnabled = false;
    bool DeferredScalingEnabled = false;
    bool InDeferredPromotion = false;
    bool InFrequentChangeFallbackPromotion = false;
    u32 FrequentChangeFallbackPromotionsThisFrame = 0;
    u64 FrequentChangeFallbackPromotionTexelsThisFrame = 0;
    bool TextureScalingEdgeExtendUnusedMargins = false;
    static constexpr u32 MaxEdgeExtendNewVariantsPerFrame = 1;
    u32 EdgeExtendNewVariantsThisFrame = 0;
    u32 EdgeExtendThrottledVariantsThisFrame = 0;
    u64 FrameIndex = 0;
    TextureScalingDebugTracker Debug;
};

}

#endif
