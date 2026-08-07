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

#ifndef RENDERERSETTINGS_H
#define RENDERERSETTINGS_H

#include "types.h"

namespace melonDS
{

struct RendererSettings
{
    enum class GLScaleAlgorithm : u8
    {
        Spline36 = 0,
        ArtCNNDN = 1,
        XBRZ = 2,
        ArtCNN = 3,
        NNEDI3 = 4,
        CuNNy4x32 = 5,
    };

    static constexpr int GLArtCNNModelCount = 2;
    static constexpr int GLCuNNyModelCount = 1;
    static constexpr int GLCuNNyMaxConvPasses = 4;

    static constexpr GLScaleAlgorithm GetGLScaleAlgorithm(int value)
    {
        switch (value)
        {
        case static_cast<int>(GLScaleAlgorithm::ArtCNNDN):
            return GLScaleAlgorithm::ArtCNNDN;
        case static_cast<int>(GLScaleAlgorithm::XBRZ):
            return GLScaleAlgorithm::XBRZ;
        case static_cast<int>(GLScaleAlgorithm::ArtCNN):
            return GLScaleAlgorithm::ArtCNN;
        case static_cast<int>(GLScaleAlgorithm::NNEDI3):
            return GLScaleAlgorithm::NNEDI3;
        case static_cast<int>(GLScaleAlgorithm::CuNNy4x32):
            return GLScaleAlgorithm::CuNNy4x32;
        default:
            return GLScaleAlgorithm::Spline36;
        }
    }

    static constexpr int GetGLScaleAlgorithmIndex(GLScaleAlgorithm value)
    {
        return static_cast<int>(value);
    }

    static constexpr bool IsGLArtCNNAlgorithm(GLScaleAlgorithm value)
    {
        return value == GLScaleAlgorithm::ArtCNN ||
               value == GLScaleAlgorithm::ArtCNNDN;
    }

    static constexpr bool IsGLNNEDI3Algorithm(GLScaleAlgorithm value)
    {
        return value == GLScaleAlgorithm::NNEDI3;
    }

    static constexpr bool IsGLCuNNyAlgorithm(GLScaleAlgorithm value)
    {
        return value == GLScaleAlgorithm::CuNNy4x32;
    }

    static constexpr int GetGLCuNNyModelIndex(GLScaleAlgorithm)
    {
        return 0;
    }

    static constexpr int GetGLArtCNNModelIndex(GLScaleAlgorithm value)
    {
        return value == GLScaleAlgorithm::ArtCNN ? 1 : 0;
    }

    enum class WholeScene2DScaleMode : u8
    {
        LegacyNativeUpscale = 0,
        HighResCompositor = 1,
        FinalNativeUpscale = 2,
        OverlayOperatorUpscale = 3,
        ConservativeHybridUpscale = 4,
    };

    enum class FinalUpscale3DDownsampleFilter : u8
    {
        Area = 0,
        Linear = 1,
        Tent = 2,
    };

    enum class WholeScene2DFragmentationFallback : u8
    {
        Off = 0,
        FinalNative = 1,
        CurrentForSevere = 2,
        AutoCurrentForBitmap = 3,
    };

    static constexpr FinalUpscale3DDownsampleFilter GetFinalUpscale3DDownsampleFilter(int value)
    {
        switch (value)
        {
        case static_cast<int>(FinalUpscale3DDownsampleFilter::Linear):
            return FinalUpscale3DDownsampleFilter::Linear;
        case static_cast<int>(FinalUpscale3DDownsampleFilter::Tent):
            return FinalUpscale3DDownsampleFilter::Tent;
        default:
            return FinalUpscale3DDownsampleFilter::Area;
        }
    }

    static constexpr int GetFinalUpscale3DDownsampleFilterIndex(FinalUpscale3DDownsampleFilter value)
    {
        return static_cast<int>(value);
    }

    static constexpr WholeScene2DFragmentationFallback GetWholeScene2DFragmentationFallback(int value)
    {
        switch (value)
        {
        case static_cast<int>(WholeScene2DFragmentationFallback::Off):
            return WholeScene2DFragmentationFallback::Off;
        case static_cast<int>(WholeScene2DFragmentationFallback::FinalNative):
            return WholeScene2DFragmentationFallback::FinalNative;
        case static_cast<int>(WholeScene2DFragmentationFallback::CurrentForSevere):
            return WholeScene2DFragmentationFallback::CurrentForSevere;
        case static_cast<int>(WholeScene2DFragmentationFallback::AutoCurrentForBitmap):
            return WholeScene2DFragmentationFallback::AutoCurrentForBitmap;
        default:
            return WholeScene2DFragmentationFallback::Off;
        }
    }

    static constexpr int GetWholeScene2DFragmentationFallbackIndex(WholeScene2DFragmentationFallback value)
    {
        return static_cast<int>(value);
    }

    static constexpr WholeScene2DScaleMode GetWholeScene2DScaleMode(int value)
    {
        switch (value)
        {
        case static_cast<int>(WholeScene2DScaleMode::HighResCompositor):
            return WholeScene2DScaleMode::HighResCompositor;
        case static_cast<int>(WholeScene2DScaleMode::FinalNativeUpscale):
            return WholeScene2DScaleMode::FinalNativeUpscale;
        case static_cast<int>(WholeScene2DScaleMode::OverlayOperatorUpscale):
            return WholeScene2DScaleMode::OverlayOperatorUpscale;
        case static_cast<int>(WholeScene2DScaleMode::ConservativeHybridUpscale):
            return WholeScene2DScaleMode::ConservativeHybridUpscale;
        default:
            return WholeScene2DScaleMode::LegacyNativeUpscale;
        }
    }

    static constexpr int GetWholeScene2DScaleModeIndex(WholeScene2DScaleMode value)
    {
        return static_cast<int>(value);
    }

    enum class TextureFilterMipDepth : u8
    {
        Full = 0,
        Min8 = 1,
        Min16 = 2,
        Min32 = 3,
    };

    static constexpr TextureFilterMipDepth GetTextureFilterMipDepth(int value)
    {
        switch (value)
        {
        case static_cast<int>(TextureFilterMipDepth::Min8):
            return TextureFilterMipDepth::Min8;
        case static_cast<int>(TextureFilterMipDepth::Min16):
            return TextureFilterMipDepth::Min16;
        case static_cast<int>(TextureFilterMipDepth::Min32):
            return TextureFilterMipDepth::Min32;
        default:
            return TextureFilterMipDepth::Full;
        }
    }

    static constexpr int GetTextureFilterMipDepthIndex(TextureFilterMipDepth value)
    {
        return static_cast<int>(value);
    }

    static constexpr u32 GetTextureFilterMipMinDimension(TextureFilterMipDepth value)
    {
        switch (value)
        {
        case TextureFilterMipDepth::Min8:
            return 8;
        case TextureFilterMipDepth::Min16:
            return 16;
        case TextureFilterMipDepth::Min32:
            return 32;
        default:
            return 1;
        }
    }

    // scale factor, for renderers that support upscaling
    int ScaleFactor;

    struct WholeScene2DScaleSettings
    {
        // request the experimental whole-scene 2D GL scaling path
        bool Enabled = false;

        // protect whole-scene scaled colors near native source/class boundaries
        bool SourceBoundaryGuard = false;

        // select the whole-scene 2D composition strategy
        WholeScene2DScaleMode Mode = WholeScene2DScaleMode::LegacyNativeUpscale;

        // select the whole-scene 2D GL scaling algorithm
        GLScaleAlgorithm Algorithm = GLScaleAlgorithm::Spline36;

        // fallback policy when many scanline chunks make whole-scene
        // scaling too expensive for one frame
        WholeScene2DFragmentationFallback FragmentationFallback =
            WholeScene2DFragmentationFallback::Off;

        // trust an upscaled exact native-final image for blend-defined pixels
        bool ExactFinalFallback = false;

        // scale resolved native 2D foreground as an overlay over direct 3D
        bool ForegroundOverlay = false;

        // allow experimental whole-scene 2D scaling for capture-backed UI
        // presentation overlays, while still rejecting unsafe full-screen
        // copied final-output capture buffers
        bool CaptureBacked = false;

        // tint whole-scene output by resolved source classification for debugging
        bool DebugTint = false;

        // prevent Spline36 taps from wrapping around repeating BG layers in the
        // high-resolution compositor
        bool NoWrapFilterTaps = true;

        // render direct 3D at 1x when final-native-upscale mode is active
        bool FinalUpscaleRender3DNative = false;

        // select how high-res direct 3D is resolved into the native final
        // image in final-native-upscale mode
        FinalUpscale3DDownsampleFilter FinalUpscale3DFilter = FinalUpscale3DDownsampleFilter::Area;

        // resolve high-res direct 3D RGB from alpha-covered samples only
        // when final-native-upscale mode downsamples 3D to native
        bool FinalUpscale3DCoverageAware = false;

        // use representative native-stage direct 3D alpha/presence instead
        // of the visual resolve alpha when high-res 3D is resolved to native
        bool FinalUpscale3DRepresentativeSemantics = true;

        // keep visual 3D coverage separate from native-stage material
        // alpha/presence when high-res 3D is resolved to native
        bool FinalUpscale3DSplitSemantics = false;

        // bias split 3D coverage toward native-like hard ownership instead
        // of using raw high-res visual coverage as opacity
        bool FinalUpscale3DSharpenSplitCoverage = false;

        // use the legacy raw direct3D*alpha endpoint in presentation overlay mode
        // instead of matching the native compositor color endpoint
        bool OverlayLegacyUnderlay = false;

        // allow conservative hybrid overlay assist across safe window-excluded
        // 2D edges; experimental and off by default for cross-game testing
        bool HybridWindowEdgeAssist = false;

        // allow conservative hybrid overlay assist when Direct3D is the
        // target2/underlay of a native alpha blend; experimental and off
        // by default for cross-game testing
        bool HybridTarget2AlphaBlendAssist = false;

        // fall back near native 2D special-effect cells instead of letting
        // high-resolution Direct3D replacement split the effect; experimental
        // and off by default for cross-game testing
        bool HybridNativeEffectGuard = false;

        // replace only a narrow Direct3D-absent fallback halo beside safe
        // foreground Direct3D with a high-resolution 2D-only base;
        // experimental and off by default for cross-game testing
        bool HybridForeground2DBase = false;

        // use the legacy whole-scene candidate instead of the hybrid
        // overlay/finalizer path for simple direct-3D + 2D scenes
        bool HybridCleanLegacyCandidate = false;

        bool operator==(const WholeScene2DScaleSettings& other) const
        {
            return Enabled == other.Enabled &&
                SourceBoundaryGuard == other.SourceBoundaryGuard &&
                Mode == other.Mode &&
                Algorithm == other.Algorithm &&
                FragmentationFallback == other.FragmentationFallback &&
                ExactFinalFallback == other.ExactFinalFallback &&
                ForegroundOverlay == other.ForegroundOverlay &&
                CaptureBacked == other.CaptureBacked &&
                DebugTint == other.DebugTint &&
                NoWrapFilterTaps == other.NoWrapFilterTaps &&
                FinalUpscaleRender3DNative == other.FinalUpscaleRender3DNative &&
                FinalUpscale3DFilter == other.FinalUpscale3DFilter &&
                FinalUpscale3DCoverageAware == other.FinalUpscale3DCoverageAware &&
                FinalUpscale3DRepresentativeSemantics == other.FinalUpscale3DRepresentativeSemantics &&
                FinalUpscale3DSplitSemantics == other.FinalUpscale3DSplitSemantics &&
                FinalUpscale3DSharpenSplitCoverage == other.FinalUpscale3DSharpenSplitCoverage &&
                OverlayLegacyUnderlay == other.OverlayLegacyUnderlay &&
                HybridWindowEdgeAssist == other.HybridWindowEdgeAssist &&
                HybridTarget2AlphaBlendAssist == other.HybridTarget2AlphaBlendAssist &&
                HybridNativeEffectGuard == other.HybridNativeEffectGuard &&
                HybridForeground2DBase == other.HybridForeground2DBase &&
                HybridCleanLegacyCandidate == other.HybridCleanLegacyCandidate;
        }

        bool operator!=(const WholeScene2DScaleSettings& other) const
        {
            return !(*this == other);
        }
    };

    struct TextureFilterSettings
    {
        // anisotropic filtering level for cached 3D textures; 1 disables it
        int Anisotropy = 1;

        // snap filtered binary-alpha cutout textures back to hard coverage
        bool BinaryAlphaHandling = false;

        // use alpha-island topology to bypass the heavy binary-alpha filterable mip path
        // for fragmented or atlas-like textures, falling back to normal filtered mips
        bool TopologyAwareMipHandling = false;

        // crop simple draw UV bounds into separate texture variants for alpha-aware filterable mipmaps
        bool MipmapSubrectHandling = false;

        // force nearest sampling for simple pixel-mapped 2D sprite-like 3D draws
        bool Smart2DFiltering = false;

        // force nearest sampling for large translucent textured draws that show filtered seams
        bool TranslucentTextureFilteringGuard = false;

        // inset simple sprite-like draw UVs before sampling, reducing atlas-edge bleed
        // from filtered or scaled 3D textures
        bool SpriteUVInset = false;

        // use transparent-edge RGB padding before generating filterable 3D texture mipmaps
        bool MipmapAlphaHandling = false;

        // stop filterable mipmap generation before levels smaller than this policy allows
        TextureFilterMipDepth MipDepth = TextureFilterMipDepth::Full;

        bool operator==(const TextureFilterSettings& other) const
        {
            return Anisotropy == other.Anisotropy &&
                BinaryAlphaHandling == other.BinaryAlphaHandling &&
                TopologyAwareMipHandling == other.TopologyAwareMipHandling &&
                MipmapSubrectHandling == other.MipmapSubrectHandling &&
                Smart2DFiltering == other.Smart2DFiltering &&
                TranslucentTextureFilteringGuard == other.TranslucentTextureFilteringGuard &&
                SpriteUVInset == other.SpriteUVInset &&
                MipmapAlphaHandling == other.MipmapAlphaHandling &&
                MipDepth == other.MipDepth;
        }

        bool operator!=(const TextureFilterSettings& other) const
        {
            return !(*this == other);
        }
    };

    struct TextureScalingSettings
    {
        // scale cached 3D textures to match the internal resolution factor
        bool Enabled = false;

        // select the 3D texture-cache scaling algorithm
        GLScaleAlgorithm Algorithm = GLScaleAlgorithm::Spline36;

        // keep frequently changing 3D textures temporarily unscaled
        bool FrequentChangePolicy = false;

        // upload palette-churned textures unscaled first, then upscale later once reused and stable
        bool Deferred = false;

        // limit filterable mipmap generation for scaled 3D textures so it does not go below native scale
        bool NativeMipFloor = false;

        // generate power-of-two scaled mip levels from the native source instead of downsampling the top scaled level
        bool SourceMips = false;

        // edge-extend unused texture margins before scaling
        bool EdgeExtendUnusedMargins = false;

        // use the old GPU 3D texture alpha-edge handling with no transparent RGB padding
        bool LegacyAlphaHandling = false;

        // use the full GPU 3D texture alpha-edge padding for comparison
        bool QualityAlphaHandling = false;

        // use xBRZ instead of Spline36 for eligible GPU texture-scaling alpha
        bool AlphaXBRZ = false;

        // use Spline36 instead of center-aligned linear alpha for GPU texture scaling
        bool Spline36Alpha = false;

        bool operator==(const TextureScalingSettings& other) const
        {
            return Enabled == other.Enabled &&
                Algorithm == other.Algorithm &&
                FrequentChangePolicy == other.FrequentChangePolicy &&
                Deferred == other.Deferred &&
                NativeMipFloor == other.NativeMipFloor &&
                SourceMips == other.SourceMips &&
                EdgeExtendUnusedMargins == other.EdgeExtendUnusedMargins &&
                LegacyAlphaHandling == other.LegacyAlphaHandling &&
                QualityAlphaHandling == other.QualityAlphaHandling &&
                AlphaXBRZ == other.AlphaXBRZ &&
                Spline36Alpha == other.Spline36Alpha;
        }

        bool operator!=(const TextureScalingSettings& other) const
        {
            return !(*this == other);
        }
    };

    WholeScene2DScaleSettings WholeScene2D;

    // decode classic GL 3D texture cache into readable RGBA8 for debugging
    bool ReadableTextureCache;

    TextureFilterSettings TextureFilter;
    TextureScalingSettings TextureScaling;

    // whether to use separate threads for rendering
    bool Threaded;

    // whether to use hi-res vertex coordinates when applying upscaling
    bool HiresCoordinates;

    // use higher-precision texture-coordinate interpolation whenever the
    // compute renderer rasterizes above native resolution
    bool HighPrecisionTextureCoordinates = true;

    // force 3D anti-aliasing in OpenGL renderers
    bool MSAA = false;

    // "improved polygon splitting" (regular OpenGL renderer)
    bool BetterPolygons;
};

}

#endif
