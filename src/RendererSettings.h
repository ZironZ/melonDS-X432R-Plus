// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

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

    static constexpr bool ScaleAlgorithmRequiresCompute(GLScaleAlgorithm value)
    {
        return IsGLArtCNNAlgorithm(value) || IsGLNNEDI3Algorithm(value) ||
               IsGLCuNNyAlgorithm(value);
    }

    static constexpr GLScaleAlgorithm SupportedScaleAlgorithm(GLScaleAlgorithm value, bool compute)
    {
        return !compute && ScaleAlgorithmRequiresCompute(value) ? GLScaleAlgorithm::Spline36 : value;
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

    // Explicit display alpha choices are independent of RGB. Automatic follows
    // the selected upscaler and is resolved before choosing presentation paths.
    enum class AffineAlphaReconstruction : int
    {
        NativeMask = 0, Bilinear, Spline36, NNEDI3, XBRZ, Automatic
    };
    enum class AffineSampling : int
    {
        OutputGrid = 0, Supersample2x = 1
    };
    static constexpr AffineAlphaReconstruction GetAffineAlphaReconstruction(int value)
    {
        return value >= 0 && value <= 5
            ? static_cast<AffineAlphaReconstruction>(value)
            : AffineAlphaReconstruction::Bilinear;
    }
    static constexpr AffineAlphaReconstruction ResolveAffineAlphaReconstruction(
        AffineAlphaReconstruction alpha, GLScaleAlgorithm algorithm)
    {
        if (alpha != AffineAlphaReconstruction::Automatic) return alpha;
        switch (algorithm)
        {
        case GLScaleAlgorithm::Spline36: return AffineAlphaReconstruction::Spline36;
        case GLScaleAlgorithm::XBRZ: return AffineAlphaReconstruction::XBRZ;
        default: return AffineAlphaReconstruction::NNEDI3;
        }
    }
    static constexpr AffineAlphaReconstruction SupportedAffineAlpha(
        AffineAlphaReconstruction value, bool compute)
    {
        return !compute && value == AffineAlphaReconstruction::NNEDI3
            ? AffineAlphaReconstruction::Automatic : value;
    }
    static constexpr AffineSampling GetAffineSampling(int value)
    {
        return value >= 0 && value <= 1 ? static_cast<AffineSampling>(value)
                                        : AffineSampling::OutputGrid;
    }

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

        // render the deliberately narrow single tiled-affine-BG candidate at
        // output resolution before final-image scaling
        bool HybridStrictAffineHighRes = false;
        AffineAlphaReconstruction HybridAffineAlpha = AffineAlphaReconstruction::Automatic;
        AffineSampling HybridAffineSampling = AffineSampling::OutputGrid;
        bool HybridNNEDI3PremultipliedRGB = false;

        // optionally reconstruct affine BG/OBJ source color before path 12
        // samples it; separate from high-resolution affine geometry
        bool HybridStrictAffineSourceEnhancement = false;

        bool HybridStrictAffineConnectedSources = false;
        bool HybridStrictAffineOpaqueAssemblies = false;

        // reconstruct enabled ordinary text BG companions in an admitted
        // strict-affine scene with the selected source scaler;
        // analytic/neural families use binary ownership plus a thin semantic
        // AA contour, while xBRZ retains reconstructed RGBA
        bool HybridStrictAffineTopTextBG = false;

        // apply directional edge AA after composition
        bool HybridStrictAffineMaskedOBJMLAA = false;

        bool operator==(const WholeScene2DScaleSettings& other) const
        {
            return HybridAffineAlpha == other.HybridAffineAlpha &&
                HybridAffineSampling == other.HybridAffineSampling &&
                HybridNNEDI3PremultipliedRGB == other.HybridNNEDI3PremultipliedRGB &&
                Enabled == other.Enabled &&
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
                HybridCleanLegacyCandidate == other.HybridCleanLegacyCandidate &&
                HybridStrictAffineHighRes == other.HybridStrictAffineHighRes &&
                HybridStrictAffineSourceEnhancement == other.HybridStrictAffineSourceEnhancement &&
                HybridStrictAffineConnectedSources == other.HybridStrictAffineConnectedSources &&
                HybridStrictAffineOpaqueAssemblies == other.HybridStrictAffineOpaqueAssemblies &&
                HybridStrictAffineTopTextBG == other.HybridStrictAffineTopTextBG &&
                HybridStrictAffineMaskedOBJMLAA == other.HybridStrictAffineMaskedOBJMLAA;
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

    enum class TextureAlpha : u8
    {
        Bilinear = 0,
        Spline36 = 1,
        NNEDI3 = 2,
        XBRZ = 3,
    };

    static constexpr TextureAlpha GetTextureAlpha(int value)
    {
        return value >= 0 && value <= 3 ? static_cast<TextureAlpha>(value) : TextureAlpha::Bilinear;
    }

    static constexpr TextureAlpha SupportedTextureAlpha(TextureAlpha value, bool compute)
    {
        return !compute && value == TextureAlpha::NNEDI3 ? TextureAlpha::Bilinear : value;
    }

    static constexpr TextureAlpha MigrateTextureAlpha(bool xbrz, bool spline36, GLScaleAlgorithm rgb)
    {
        // Previously xBRZ RGB always supplied its own alpha.
        return xbrz || rgb == GLScaleAlgorithm::XBRZ ? TextureAlpha::XBRZ :
               spline36 ? TextureAlpha::Spline36 : TextureAlpha::Bilinear;
    }

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
        // Experimental source assembly for compatible pixel-aligned 3D quads.
        bool ReconstructCompatible3D = false;
        bool ReconstructCompatible3DEdgeContext = false;
        bool ReconstructCompatible3DFractionalAlpha = false;

        // use the old GPU 3D texture alpha-edge handling with no transparent RGB padding
        bool LegacyAlphaHandling = false;

        // use the full GPU 3D texture alpha-edge padding for comparison
        bool QualityAlphaHandling = false;

        TextureAlpha Alpha = TextureAlpha::Bilinear;

        bool operator==(const TextureScalingSettings& other) const
        {
            return Enabled == other.Enabled &&
                Algorithm == other.Algorithm &&
                FrequentChangePolicy == other.FrequentChangePolicy &&
                Deferred == other.Deferred &&
                NativeMipFloor == other.NativeMipFloor &&
                SourceMips == other.SourceMips &&
                EdgeExtendUnusedMargins == other.EdgeExtendUnusedMargins &&
                ReconstructCompatible3D == other.ReconstructCompatible3D &&
                ReconstructCompatible3DEdgeContext == other.ReconstructCompatible3DEdgeContext &&
                ReconstructCompatible3DFractionalAlpha == other.ReconstructCompatible3DFractionalAlpha &&
                LegacyAlphaHandling == other.LegacyAlphaHandling &&
                QualityAlphaHandling == other.QualityAlphaHandling &&
                Alpha == other.Alpha;
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

    // Apply before resolving Automatic alpha or choosing reconstruction paths.
    void ApplyComputeShaderSupport(bool compute)
    {
        WholeScene2D.Algorithm = SupportedScaleAlgorithm(WholeScene2D.Algorithm, compute);
        WholeScene2D.HybridAffineAlpha = SupportedAffineAlpha(WholeScene2D.HybridAffineAlpha, compute);
        if (!compute) WholeScene2D.HybridNNEDI3PremultipliedRGB = false;
        TextureScaling.Algorithm = SupportedScaleAlgorithm(TextureScaling.Algorithm, compute);
        TextureScaling.Alpha = SupportedTextureAlpha(TextureScaling.Alpha, compute);
    }
};

}

#endif
