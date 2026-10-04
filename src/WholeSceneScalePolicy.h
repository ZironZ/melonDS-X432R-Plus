// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"
#include "WholeSceneCapturePolicy.h"

namespace melonDS
{

enum class WholeSceneScaleEligibility : u8
{
    ScreenUnavailable,
    EngineANotSupportedYet,
    MainEngineVRAMDisplay,
    MainEngineDisplayFIFO,
    CaptureActive,
    CaptureBackedBG,
    CaptureBackedOBJ,
    UnsupportedDisplayMode,
    Eligible,
};

enum class WholeSceneNative3DSource
{
    None,
    NativeRendered,
    HighResResolved,
};

enum class WholeSceneRenderPath
{
    None,
    Current,
    LegacyNativeUpscale,
    HighResCompositor,
    FinalNativeUpscale,
    OverlayOperatorUpscale,
    ConservativeHybridUpscale,
    CaptureBackedHandoff,
    SourceACaptureReplacement,
    CaptureEpochOverlay,
    PhysicalFinalPostprocessInput,
    NativeExactFloor,
    StrictAffineHighRes,
};

enum class WholeSceneCurrentPathReason
{
    None,
    DirectCurrent,
    HybridPresentationGuard,
    FragmentationOrUnsafeFrame,
    NativeProductFinalizerIncomplete,
    CaptureBackedLiveBeforeCapturedPhase,
    ScalePathUnavailable,
};

enum class WholeScenePathDecisionReason : u8
{
    None = 0,
    CaptureBackedProducerDuringHybridGuard = 1,
    HybridPresentationGuard = 2,
    CaptureBackedBeforeGeneralFallbacks = 3,
    PhysicalFinalPostprocessNativeInput = 5,
    SplitLegacyFallback = 6,
    CaptureBackedAfterGeneralFallbacks = 7,
    FragmentationOrUnsafeFrameCurrentFallback = 8,
    WholeSceneScale = 9,
    ScalePathUnavailable = 10,
};

enum class WholeSceneScaleDecisionReason : u8
{
    None,
    HighResCompositor,
    SourceACaptureOnlyReplacement,
    CaptureBackedBeforeGeneralFallbacks,
    HybridFragmentationFinalUpscale,
    OverlayOperator,
    FinalNativeUpscale,
    LegacyNativeUpscale,
    StrictAffineHighRes,
};

enum class StrictAffineHighResBlockReason : u8
{
    None = 0,
    FeatureDisabled,
    NotConservativeHybrid,
    ScalePathUnavailable,
    ScaleFactorOne,
    IncompleteRange,
    DisplayModeNotComposited,
    SnapshotIncoherent,
    CandidateTargetUnavailable,
    HighResolutionGeometryNotRequired,
    NotExactlyOneVisibleLayer,
    VisibleLayerNotTiledAffineBG,
    UnsupportedActiveChannel,
    NotExactlyOneAffineBG,
    UnsupportedCompanionLayer,
    UnsupportedAlphaBlendConfiguration,
};

enum class StrictAffineDebugProductClass : u8
{
    None = 0,
    ProductionEligible,
    DualExtendedAffineWithOBJ,
};

enum class StrictAffineSourceEnhancementDecision : u8
{
    Disabled = 0,
    StrictAffinePathNotSelected,
    AlgorithmUnsupported,
    Spline36Inline,
    CuNNyCachedAffineSources,
    NNEDI3CachedAffineSources,
    XBRZCachedAffineSources,
    ArtCNNCachedAffineSources,
    Spline36CachedAffineSources,
    NativeCachedAffineSources,
};

enum StrictAffineUnderlayRejectReason : u32
{
    StrictAffineUnderlayRejectNone = 0,
    StrictAffineUnderlayRejectInvalidRange = 1u << 0,
    StrictAffineUnderlayRejectLowerBG = 1u << 1,
    StrictAffineUnderlayRejectLowerOBJ = 1u << 2,
    StrictAffineUnderlayRejectBackdropVaries = 1u << 3,
    StrictAffineUnderlayRejectTarget1Mismatch = 1u << 4,
    StrictAffineUnderlayRejectTarget2Mismatch = 1u << 5,
};

enum StrictAffineOperandExcludedOverlayRejectReason : u32
{
    StrictAffineOperandExcludedOverlayRejectNone = 0,
    StrictAffineOperandExcludedOverlayRejectNoPlan = 1u << 0,
    StrictAffineOperandExcludedOverlayRejectNoDirect3D = 1u << 1,
    StrictAffineOperandExcludedOverlayRejectAffineBG = 1u << 2,
    StrictAffineOperandExcludedOverlayRejectRange = 1u << 3,
    StrictAffineOperandExcludedOverlayRejectWindow = 1u << 4,
    StrictAffineOperandExcludedOverlayRejectEffect = 1u << 5,
    StrictAffineOperandExcludedOverlayRejectOrdering = 1u << 6,
    StrictAffineOperandExcludedOverlayRejectSpecialOBJ = 1u << 7,
    StrictAffineOperandExcludedOverlayRejectNoAffineOperand = 1u << 8,
    StrictAffineOperandExcludedOverlayRejectRender = 1u << 9,
};

enum StrictAffineResolvedOBJRejectReason : u32
{
    StrictAffineResolvedOBJRejectNone = 0,
    StrictAffineResolvedOBJRejectRange = 1u << 0,
    StrictAffineResolvedOBJRejectUnsupportedSprite = 1u << 1,
    StrictAffineResolvedOBJRejectNoOrdinaryOBJ = 1u << 2,
    StrictAffineResolvedOBJRejectScale = 1u << 3,
    StrictAffineResolvedOBJRejectStorage = 1u << 4,
    StrictAffineResolvedOBJRejectRGBScale = 1u << 5,
    StrictAffineResolvedOBJRejectAlphaScale = 1u << 6,
};

struct StrictAffineSourceEnhancementInputs
{
    bool Enabled = false;
    bool StrictAffinePathSelected = false;
    bool InlineSpline36Available = false;
    bool CachedArtCNNAvailable = false;
    bool CachedCuNNyAvailable = false;
    bool CachedNNEDI3Available = false;
    bool CachedXBRZAvailable = false;
};

StrictAffineSourceEnhancementDecision ChooseStrictAffineSourceEnhancement(
    const StrictAffineSourceEnhancementInputs& inputs);

constexpr bool IsStrictAffineCachedSource(StrictAffineSourceEnhancementDecision decision)
{
    return decision == StrictAffineSourceEnhancementDecision::ArtCNNCachedAffineSources ||
           decision == StrictAffineSourceEnhancementDecision::CuNNyCachedAffineSources ||
           decision == StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources ||
           decision == StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources ||
           decision == StrictAffineSourceEnhancementDecision::Spline36CachedAffineSources ||
           decision == StrictAffineSourceEnhancementDecision::NativeCachedAffineSources;
}

constexpr int StrictAffineCachedSourceScale(
    StrictAffineSourceEnhancementDecision decision, int outputScale)
{
    return IsStrictAffineCachedSource(decision) ? (outputScale >= 4 ? 4 : 2) : 1;
}

enum class AffineOBJPresentationTopClass : u8
{
    Unsupported = 0,
    Backdrop,
    BG,
    OBJ,
};

struct AffineOBJPresentationPriorityInputs
{
    u8 CandidatePriority = 0;
    u8 CandidateOAMIndex = 0;
    AffineOBJPresentationTopClass TopClass =
        AffineOBJPresentationTopClass::Unsupported;
    u8 TopPriority = 0;
    u8 TopOAMIndex = 0;
    bool TopIsSameAffineCandidate = false;
};

// Pure mirror of the bounded presentation shader rule. OBJ wins over BG at
// equal numeric priority; OBJ/OBJ ties are resolved by lower OAM index, while
// an identical affine native winner is the candidate rather than an occluder.
constexpr bool DoesAffineOBJPresentationCandidateWin(
    const AffineOBJPresentationPriorityInputs& inputs)
{
    switch (inputs.TopClass)
    {
    case AffineOBJPresentationTopClass::Backdrop:
        return true;
    case AffineOBJPresentationTopClass::BG:
        return inputs.CandidatePriority <= inputs.TopPriority;
    case AffineOBJPresentationTopClass::OBJ:
        if (inputs.TopIsSameAffineCandidate)
            return true;
        return (static_cast<u16>(inputs.CandidatePriority) * 128u +
                inputs.CandidateOAMIndex) <
               (static_cast<u16>(inputs.TopPriority) * 128u +
                inputs.TopOAMIndex);
    default:
        return false;
    }
}

struct StrictAffineHighResEligibilityInputs
{
    bool FeatureEnabled = false;
    bool ConservativeHybridMode = false;
    bool ScalePathAvailable = false;
    int OutputScale = 1;
    int YStart = 0;
    int YEnd = 0;
    bool DisplayModeComposited = false;
    bool SnapshotCoherent = false;
    bool CandidateTargetAvailable = false;
    bool HighResolutionGeometryRequired = true;
    u32 VisibleLayerMask = 0;
    u32 TiledAffineBGMask = 0;
    u32 ExtendedTiledAffineBGMask = 0;
    u32 TextTiledBGMask = 0;
    u32 Direct3DLayerMask = 0;
    u32 AlphaBlendTarget1Mask = 0;
    u32 AlphaBlendTarget2Mask = 0;
    u32 ColorEffectMode = 0;
    u32 ColorEffectFactor = 0;
    bool HasOrdinaryOBJ = false;
    bool HasSemiTransparentOrdinaryOBJ = false;
    bool HasSemiTransparentAffineOBJ = false;
    bool HasBitmapOrdinaryOBJ = false;
    bool HasBitmapAffineOBJ = false;
    bool HasUnsupportedSemiTransparentOBJ = false;
    bool AlphaBlendOrdinaryOBJPrioritySafe = false;
    u64 RequiredChannels = 0;
};

enum class DeferredScanlineStrictAffineBlockReason : u8
{
    None = 0,
    FeatureDisabled,
    NotConservativeHybrid,
    ScaleFactorOne,
    CandidateTargetUnavailable,
    NativeFrameIncomplete,
    NativeRowIdentityInvalid,
    NativeRowMissing,
    NativeRowSourceMismatch,
    LiveSourceMismatch,
    RouteOrPresentationMismatch,
    UnsupportedRowState,
    UnsupportedOrdinaryOBJPresentation,
    HighResolutionGeometryNotRequired,
};

struct DeferredScanlineStrictAffineInputs
{
    static constexpr u32 MaxHistoricalSourceEpochs = 3;

    bool FeatureEnabled = false;
    bool ConservativeHybridMode = false;
    int OutputScale = 1;
    bool CandidateTargetAvailable = false;
    bool NativeFrameComplete = false;
    bool NativeRowIdentityValid = false;
    bool RouteAndPresentationUniform = false;
    bool AllRowsSupported = false;
    bool OrdinaryOBJPresentationRequired = false;
    bool OrdinaryOBJPresentationSupported = false;
    bool HighResolutionGeometryRequired = false;
    u64 LiveSourceGeneration = 0;
    u64 LiveRowSourceGeneration[192] = {};
    bool HistoricalSourceAvailable = false;
    int HistoricalSourceYEnd = 0;
    u64 HistoricalSourceGeneration = 0;
    u64 HistoricalRowSourceGeneration[192] = {};
    u32 HistoricalSourceEpochCount = 0;
    bool HistoricalSourceEpochAvailable[MaxHistoricalSourceEpochs] = {};
    int HistoricalSourceEpochYStart[MaxHistoricalSourceEpochs] = {};
    int HistoricalSourceEpochYEnd[MaxHistoricalSourceEpochs] = {};
    u64 HistoricalSourceEpochGeneration[MaxHistoricalSourceEpochs] = {};
    u64 HistoricalSourceEpochRowGeneration[MaxHistoricalSourceEpochs][192] = {};
    bool RowValid[192] = {};
    u64 RowSourceGeneration[192] = {};
};

struct DeferredScanlineStrictAffineAssessment
{
    bool Eligible = false;
    DeferredScanlineStrictAffineBlockReason BlockReason =
        DeferredScanlineStrictAffineBlockReason::FeatureDisabled;
    u64 SourceGeneration = 0;
    bool HistoricalSourceUsed = false;
    int HistoricalSourceYEnd = 0;
    u64 HistoricalSourceGeneration = 0;
    u32 HistoricalSourceEpochCount = 0;
};

DeferredScanlineStrictAffineAssessment
AssessDeferredScanlineStrictAffine(
    const DeferredScanlineStrictAffineInputs& inputs);

// Direct-color bitmap OBJ is the one currently admitted alpha-blend material
// whose semantic operator consumes the real high-resolution target-2 winner.
// Companion text-BG reconstruction is therefore an input to that exact
// material equation rather than the unsupported fractional third operand
// presented by general mode-1 EVA/EVB blending.
constexpr bool HasStrictAffineResolvedBitmapOBJMaterial(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    return inputs.ColorEffectMode == 1 &&
           inputs.HasBitmapAffineOBJ &&
           !inputs.HasSemiTransparentOrdinaryOBJ &&
           !inputs.HasSemiTransparentAffineOBJ &&
           !inputs.HasUnsupportedSemiTransparentOBJ &&
           inputs.AlphaBlendTarget2Mask != 0;
}

// Enhanced text-BG RGB can remain an ordinary semantic operand when the
// strict compositor already has a complete, bounded EVA/EVB equation. The
// reconstructed alpha is deliberately not part of this proof: native BG
// ownership selects target 1, then the existing compositor blends that
// reconstructed color against the real affine-BG/backdrop target 2.
constexpr bool HasStrictAffineResolvedTextBGAlphaBlend(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    constexpr u32 backdrop = 1u << 5;
    const u32 visibleText =
        inputs.VisibleLayerMask & inputs.TextTiledBGMask & 0xFu;
    const u32 visibleAffine = inputs.VisibleLayerMask &
        (inputs.TiledAffineBGMask | inputs.ExtendedTiledAffineBGMask) & 0xFu;
    const u32 target1 = inputs.AlphaBlendTarget1Mask;
    const u32 target2 = inputs.AlphaBlendTarget2Mask;

    return inputs.ColorEffectMode == 1 &&
           target1 != 0 && target2 != 0 &&
           (target1 & visibleText) != 0 &&
           (target1 & ~visibleText) == 0 &&
           (target2 & (visibleAffine | backdrop)) != 0 &&
           (target2 & ~(visibleAffine | backdrop)) == 0 &&
           !inputs.HasSemiTransparentOrdinaryOBJ &&
           !inputs.HasSemiTransparentAffineOBJ &&
           !inputs.HasBitmapOrdinaryOBJ &&
           !inputs.HasBitmapAffineOBJ &&
           !inputs.HasUnsupportedSemiTransparentOBJ &&
           (!inputs.HasOrdinaryOBJ ||
            inputs.AlphaBlendOrdinaryOBJPrioritySafe);
}

// BLDCNT may retain alpha-blend mode while there is no possible target-1
// participant. Normal OBJ do not implicitly become target 1; only mode-1 and
// bitmap-material OBJ do. In that state the blend equation is inert and does
// not prevent source-RGB reconstruction of an ordinary text BG.
constexpr bool HasStrictAffineInertAlphaBlend(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    return inputs.ColorEffectMode == 1 &&
           inputs.AlphaBlendTarget1Mask == 0 &&
           !inputs.HasSemiTransparentOrdinaryOBJ &&
           !inputs.HasSemiTransparentAffineOBJ &&
           !inputs.HasBitmapOrdinaryOBJ &&
           !inputs.HasBitmapAffineOBJ &&
           !inputs.HasUnsupportedSemiTransparentOBJ;
}

// Once the bounded semantic blend is complete, bilinear native-source
// coverage may interpolate between that completed target-1 result and the
// actual exposed target-2 winner. This is presentation coverage, not a change
// to DS blend coefficients or layer ownership.
constexpr u32 ChooseStrictAffineResolvedTextBGBilinearCoverageMask(
    u32 enhancedTextBGMask,
    u32 alphaBlendTarget1Mask,
    bool sourceEnhanced,
    bool resolvedTextBGAlphaBlend)
{
    if (!sourceEnhanced || !resolvedTextBGAlphaBlend)
    {
        return 0;
    }

    return enhancedTextBGMask & alphaBlendTarget1Mask & 0xFu;
}

constexpr u32 ChooseStrictAffineConstantBackdropCompositeMask(
    u32 constantBackdropProofBGMask,
    bool bilinearPresentationEnabled,
    u32 blendEffect,
    u32 alphaBlendTarget1Mask,
    u32 alphaBlendTarget2Mask,
    u32 colorEffectFactor,
    bool resolvedTextBGAlphaBlend)
{
    const bool brightnessEffect =
        (blendEffect == 2 || blendEffect == 3) && colorEffectFactor > 0;
    const bool ordinaryGateOpen =
        (alphaBlendTarget1Mask == 0 || brightnessEffect) &&
        (alphaBlendTarget2Mask == 0 || alphaBlendTarget2Mask == 0x3Fu);

    // In the bounded text-BG equation the affine BG and backdrop have equal
    // Target-2 membership (part of the supplied proof). Folding their smooth
    // boundary first is therefore distributive with the later EVA/EVB blend.
    const bool boundedAlphaBlendGateOpen =
        blendEffect == 1 && resolvedTextBGAlphaBlend;
    return bilinearPresentationEnabled &&
           (ordinaryGateOpen || boundedAlphaBlendGateOpen)
        ? (constantBackdropProofBGMask & 0xFu)
        : 0u;
}

struct StrictAffineGeometryDemandInputs
{
    u32 VisibleAffineBGMask = 0;
    u32 IdentityEquivalentAffineBGMask = 0;
    u32 PreviousTransformedAffineBGMask = 0;
    u32 SettledIdentityAffineBGMask = 0;
    bool HasAffineOBJ = false;
};

struct StrictAffineGeometryDemandResult
{
    u32 TransformedAffineBGMask = 0;
    bool HighResolutionGeometryRequired = false;
};

// A transformed BG remains strict until the renderer has observed a stable
// run of exact-identity frames. A momentary identity crossing therefore cannot
// switch paths in the middle of a real affine animation.
constexpr StrictAffineGeometryDemandResult UpdateStrictAffineGeometryDemand(
    const StrictAffineGeometryDemandInputs& inputs)
{
    const u32 visible = inputs.VisibleAffineBGMask & 0xFu;
    const u32 identityEquivalent =
        inputs.IdentityEquivalentAffineBGMask & visible;
    const u32 transformed =
        (inputs.PreviousTransformedAffineBGMask & visible &
         ~inputs.SettledIdentityAffineBGMask) |
        (visible & ~identityEquivalent);
    return {
        transformed,
        inputs.HasAffineOBJ || transformed != 0,
    };
}

// The DISPCNT enable bit alone does not create an OBJ-window mask. Treat the
// channel as active only while OBJ rendering is enabled and a mode-2 sprite is
// actually present in the renderer's current OAM inputs.
constexpr bool HasStrictAffineActiveOBJWindow(
    bool objWindowEnabled,
    bool objLayerEnabled,
    bool objWindowParticipant)
{
    return objWindowEnabled && objLayerEnabled && objWindowParticipant;
}

constexpr u32 ChooseStrictAffineCompanionTextBGMask(
    u32 layerEnable,
    u32 ordinaryTextBGMask,
    bool enhancementEnabled,
    u32 blendEffect,
    bool resolvedBitmapOBJMaterial,
    bool resolvedTextBGAlphaBlend,
    bool inertAlphaBlend)
{
    // Fractional text-BG presentation under general EVA/EVB blending still
    // lacks the required third-operand ordering contract. Source-RGB-only
    // reconstruction is safe for the bounded text-BG equation proven above.
    // Bitmap OBJ is also already resolved semantically
    // against target 2, so leaving that target native would make its admitted
    // high-resolution operand internally mixed-resolution.
    if (!enhancementEnabled ||
        (blendEffect == 1 && !resolvedBitmapOBJMaterial &&
         !resolvedTextBGAlphaBlend && !inertAlphaBlend))
        return 0;

    return layerEnable & ordinaryTextBGMask & 0xFu;
}

StrictAffineHighResBlockReason ChooseStrictAffineHighResBlockReason(
    const StrictAffineHighResEligibilityInputs& inputs);

StrictAffineDebugProductClass ChooseStrictAffineDebugProductClass(
    const StrictAffineHighResEligibilityInputs& inputs);

bool CanProduceStrictAffineDebugProducts(
    const StrictAffineHighResEligibilityInputs& inputs);

inline bool CanUseStrictAffineHighResCandidate(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    return ChooseStrictAffineHighResBlockReason(inputs) ==
           StrictAffineHighResBlockReason::None;
}

enum class HybridSourceDecisionReason : u8
{
    None,
    OverlayOperator,
    ConservativeHybridWithDirect3D,
    ConservativeHybridWithoutDirect3D,
    SuppressedDirect3DOverlay,
};

enum class WholeSceneOverlayEndpointFinalMode
{
    None,
    MetadataResolve,
    ExactCompositor,
};

inline bool WholeSceneRenderPathUsesFullFrameFinalizer(WholeSceneRenderPath path)
{
    return path == WholeSceneRenderPath::FinalNativeUpscale ||
           path == WholeSceneRenderPath::OverlayOperatorUpscale ||
           path == WholeSceneRenderPath::ConservativeHybridUpscale;
}

inline WholeSceneRenderPath WholeSceneRenderPathForCaptureBackedPlanKind(
    WholeSceneCaptureBackedPlanKind kind)
{
    switch (kind)
    {
    case WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff:
        return WholeSceneRenderPath::CaptureBackedHandoff;
    case WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement:
        return WholeSceneRenderPath::SourceACaptureReplacement;
    case WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay:
        return WholeSceneRenderPath::CaptureEpochOverlay;
    case WholeSceneCaptureBackedPlanKind::None:
    default:
        return WholeSceneRenderPath::None;
    }
}

inline const char* WholeSceneNative3DSourceDescription(
    WholeSceneNative3DSource source,
    bool splitSemantics)
{
    switch (source)
    {
    case WholeSceneNative3DSource::NativeRendered:
        return "native-rendered Direct3D texture.";
    case WholeSceneNative3DSource::HighResResolved:
        return splitSemantics
            ? "high-resolution Direct3D visual RGB with separate visual coverage and native material alpha/presence."
            : "high-resolution Direct3D visual RGB with a single native compositor alpha.";
    case WholeSceneNative3DSource::None:
    default:
        return "not generated this frame.";
    }
}

inline const char* WholeSceneRenderPathName(WholeSceneRenderPath path)
{
    switch (path)
    {
    case WholeSceneRenderPath::Current:
        return "current native/high-res renderer";
    case WholeSceneRenderPath::LegacyNativeUpscale:
        return "native stack upscale";
    case WholeSceneRenderPath::HighResCompositor:
        return "high-resolution compositor";
    case WholeSceneRenderPath::FinalNativeUpscale:
        return "postprocessing upscale";
    case WholeSceneRenderPath::OverlayOperatorUpscale:
        return "presentation overlay upscale";
    case WholeSceneRenderPath::ConservativeHybridUpscale:
        return "conservative hybrid overlay upscale";
    case WholeSceneRenderPath::CaptureBackedHandoff:
        return "capture-backed 3D plus UI handoff";
    case WholeSceneRenderPath::SourceACaptureReplacement:
        return "source-A capture high-resolution replacement";
    case WholeSceneRenderPath::CaptureEpochOverlay:
        return "capture-epoch background plus current overlay";
    case WholeSceneRenderPath::PhysicalFinalPostprocessInput:
        return "physical final postprocess input";
    case WholeSceneRenderPath::NativeExactFloor:
        return "native-exact nearest floor";
    case WholeSceneRenderPath::StrictAffineHighRes:
        return "strict high-resolution affine BG";
    case WholeSceneRenderPath::None:
    default:
        return "none";
    }
}

inline const char* WholeSceneOverlayEndpointFinalModeName(
    WholeSceneOverlayEndpointFinalMode mode)
{
    switch (mode)
    {
    case WholeSceneOverlayEndpointFinalMode::MetadataResolve:
        return "metadata resolve";
    case WholeSceneOverlayEndpointFinalMode::ExactCompositor:
        return "exact compositor";
    case WholeSceneOverlayEndpointFinalMode::None:
    default:
        return "none";
    }
}

struct VisibleOBJCaptureDebug
{
    bool Found = false;
    bool MixedBank = false;
    bool CurrentSourceAOnly = false;
    bool CurrentFullSourceA = false;
    bool FullScreen = false;
    bool FullWidthTopStrip = false;
    bool ProductAvailable = false;
    bool EventValid = false;
    bool EventSourceOBJ = false;
    int Bank = -1;
    int Type3Count = 0;
    int Type4Count = 0;
    int CaptureSpriteCount = 0;
    int NonCaptureSpriteCount = 0;
    int CoverageArea = 0;
    int MinX = 0;
    int MinY = 0;
    int MaxX = 0;
    int MaxY = 0;
    u64 EventSerial = 0;
    u32 EventProductMask = 0;
    u32 EventRejectReason = 0;
    u32 RejectReason = 0;
};

enum class WholeSceneNativeProductRowIdentityInvalidReason : u8
{
    None,
    DescriptorScopeMismatch,
    OverlapIdentityMismatch,
};

enum WholeSceneNativeProductEpochInvalidReason : u32
{
    WholeSceneNativeProductEpochInvalidNone = 0,
    WholeSceneNativeProductEpochInvalidEligibility = 1u << 0,
    WholeSceneNativeProductEpochInvalidPath = 1u << 1,
    WholeSceneNativeProductEpochInvalidMidFrameState = 1u << 2,
};

struct WholeSceneNativeProductFinalizerInputs
{
    bool FinalizerPathSeen = false;
    bool EpochValid = false;
    u32 EpochInvalidReason = WholeSceneNativeProductEpochInvalidNone;
    bool RowIdentityValid = false;
    u32 IdentityRows = 0;
    u64 FrameIdentity = 0;
    bool FrameComplete = false;
};

// Admission and finalization must agree on which state transitions can
// retain native rows for a later full-frame reconstruction.
bool CanDeferWholeSceneNativeProductEpoch(bool epochValid, u32 invalidReason);

bool CanFinalizeWholeSceneRowOwnedNativeProduct(
    const WholeSceneNativeProductFinalizerInputs& inputs);

struct WholeSceneRenderTrace
{
    WholeSceneRenderPath Path = WholeSceneRenderPath::None;
    WholeSceneCurrentPathReason CurrentReason = WholeSceneCurrentPathReason::None;
    WholeSceneNative3DSource Native3DSource = WholeSceneNative3DSource::None;
    WholeSceneOverlayEndpointFinalMode OverlayEndpointFinalMode = WholeSceneOverlayEndpointFinalMode::None;
    WholeSceneCaptureAuthority CaptureAuthority = WholeSceneCaptureAuthority::None;
    WholeSceneCaptureBackedPlanRole CaptureRole = WholeSceneCaptureBackedPlanRole::None;
    WholeSceneCaptureRequestKind CaptureRequestKind = WholeSceneCaptureRequestKind::None;
    WholeSceneCaptureProductKind CaptureProductKind = WholeSceneCaptureProductKind::None;
    WholeSceneCaptureProofKind CaptureProofKind = WholeSceneCaptureProofKind::None;
    WholeSceneCaptureRenderAction CaptureRenderAction = WholeSceneCaptureRenderAction::None;
    SourceABackgroundSource EffectiveSourceABackgroundSource = SourceABackgroundSource::None;
    u64 SourceABackgroundEpochSerial = 0;
    u64 EffectiveSourceABackgroundEpochSerial = 0;
    u64 SourceARouteProductBackgroundEpochSerial = 0;
    u64 SourceARouteProductSource3DSerial = 0;
    u32 SourceARouteProductSource3DSceneHash = 0;
    u64 SourceARouteProductCapturedEventSerial = 0;
    u32 SourceARouteProductCapturePresentationHash = 0;
    u32 SourceARouteProductCurrentPresentationHash = 0;
    u32 SourceARouteProductStableFrames = 0;
    u32 SourceARouteProductPresentationClass = 0;
    bool RouteProductLookupAttempted = false;
    bool RouteProductLookupSuccess = false;
    u32 RouteProductLookupResultSource = 0;
    int RouteProductLookupSlot = -1;
    u64 RouteProductLookupEventSerial = 0;
    u32 RouteProductLookupCaptureBank = 0xFFFFFFFFu;
    u32 RouteProductLookupCapturePresentationHash = 0;
    u64 RouteProductLookupSource3DSerial = 0;
    u32 RouteProductLookupSource3DSceneHash = 0;
    bool RouteProductLookupEventProductValid = false;
    u64 RouteProductLookupEventProductCapturedSerial = 0;
    u32 RouteProductLookupEventProductCaptureBank = 0xFFFFFFFFu;
    u32 RouteProductLookupEventProductCurrentPresentationHash = 0;
    u64 RouteProductLookupEventProductSource3DSerial = 0;
    u32 RouteProductLookupEventProductSource3DSceneHash = 0;
    bool RouteProductLookupProductValid = false;
    u64 RouteProductLookupProductCapturedSerial = 0;
    u32 RouteProductLookupProductCaptureBank = 0xFFFFFFFFu;
    u32 RouteProductLookupProductCurrentPresentationHash = 0;
    u64 RouteProductLookupProductSource3DSerial = 0;
    u32 RouteProductLookupProductSource3DSceneHash = 0;
    bool RouteEventPublishAttempted = false;
    bool RouteEventPublishSuccess = false;
    u32 RouteEventPublishRejectReason = 0;
    int RouteEventPublishSlot = -1;
    u64 RouteEventPublishPendingEventSerial = 0;
    u32 RouteEventPublishPendingCaptureBank = 0xFFFFFFFFu;
    u32 RouteEventPublishPendingPresentationHash = 0;
    u64 RouteEventPublishPendingSource3DSerial = 0;
    u32 RouteEventPublishPendingSource3DSceneHash = 0;
    bool RouteEventPublishProductValid = false;
    bool RouteEventPublishProductTexValid = false;
    bool RouteEventPublishEventProductTexValid = false;
    bool RouteEventPublishEventProductFBValid = false;
    u64 RouteEventPublishProductBackgroundEpochSerial = 0;
    u64 RouteEventPublishProductSource3DSerial = 0;
    u32 RouteEventPublishProductSource3DSceneHash = 0;
    u64 RouteEventPublishProductCapturedEventSerial = 0;
    u32 RouteEventPublishProductCaptureBank = 0xFFFFFFFFu;
    u32 RouteEventPublishProductCapturePresentationHash = 0;
    u32 RouteEventPublishProductCurrentPresentationHash = 0;
    u32 RouteEventPublishProductPresentationClass = 0;
    u32 SourceACapturePresentationHash = 0;
    u32 SourceACurrentPresentationHash = 0;
    int SourceAChosenProductTex = 0;
    int SourceAChosenProductCaptureBank = -1;
    u64 SourceAChosenProductBackgroundEpochSerial = 0;
    u64 SourceAChosenProductSource3DSerial = 0;
    u32 SourceAChosenProductSource3DSceneHash = 0;
    u64 SourceAChosenProductCaptureEventSerial = 0;
    u32 SourceAChosenProductCapturePresentationHash = 0;
    u32 SourceAChosenProductCurrentPresentationHash = 0;
    u32 SourceAChosenProductKind = 0;
    u32 SourceAChosenProductRenderAction = 0;
    u32 SourceAChosenProductPresentationClass = 0;
    bool CaptureProductUseAccepted = false;
    bool CaptureProductRowScopeCompatible = false;
    int CaptureProductValidYStart = 0;
    int CaptureProductValidYEnd = 0;
    bool OutputPresentationMasterBrightnessApplied = false;
    u32 OutputPresentationEffectOwner = 0;
    u32 OutputPresentationEffectState = 0;
    int OutputPresentationTex = 0;
    int SourceAFullProductCaptureBank = -1;
    int SourceAFullProductTex = 0;
    bool SourceAFullProductEventValid = false;
    u64 SourceAFullProductEventSerial = 0;
    u64 SourceAFullProductEventSource3DSerial = 0;
    u32 SourceAFullProductEventSource3DSceneHash = 0;
    u32 SourceAFullProductEventSourcePresentationHash = 0;
    u32 SourceAFullProductEventSourceKind = 0;
    u32 SourceAFullProductEventProductMask = 0;
    u32 SourceAFullProductEventRejectReason = 0;
    int SourceAFullProductEventDstBlock = -1;
    int SourceAFullProductEventDstOffset = -1;
    bool SourceAFullProductEventSourceOBJ = false;
    bool SourceAFullProductEventScreenSwap = false;
    bool SourceAFullProductEventMainFinalBottom = false;
    bool DirectFinalDisplayConsumer = false;
    bool DirectFinalBottomConsumer = false;
    bool ActiveDisplayCaptureSourceA2D = false;
    bool ActiveFullDisplayCaptureSourceA = false;
    int ActiveDisplayCaptureDstBank = -1;
    int ActiveDisplayCaptureDstOffset = -1;
    int YStart = 0;
    int YEnd = 0;
    u64 RenderTimeUS = 0;
    int OutputTex3D = 0;
    int NativeStage3D = 0;
    bool HighRes3D = false;
    bool Linear3D = false;
    bool Resolve3D = false;
    bool DeferredScanlineStrictAffineEvaluated = false;
    bool DeferredScanlineStrictAffineEligible = false;
    DeferredScanlineStrictAffineBlockReason
        DeferredScanlineStrictAffineReason =
            DeferredScanlineStrictAffineBlockReason::FeatureDisabled;
    bool DeferredStrictAffineHistoricalSourceAttempted = false;
    bool DeferredStrictAffineHistoricalSourceValid = false;
    bool DeferredStrictAffineHistoricalSourceUsed = false;
    int DeferredStrictAffineHistoricalSourceYEnd = 0;
    u32 DeferredStrictAffineHistoricalNativeBGMask = 0;
    u32 DeferredStrictAffineHistoricalEnhancedBGMask = 0;
    u32 DeferredStrictAffineRowSourceEpochCount = 0;
    int DeferredStrictAffineFirstSourceTransitionY = -1;
    u64 DeferredStrictAffineFirstRowSourceGeneration = 0;
    u64 DeferredStrictAffineLiveSourceGeneration = 0;
    StrictAffineDebugProductClass StrictAffineDebugClass =
        StrictAffineDebugProductClass::None;
    StrictAffineSourceEnhancementDecision StrictAffineSourceEnhancement =
        StrictAffineSourceEnhancementDecision::Disabled;
    u32 StrictAffineEnhancedOBJActiveSlots = 0;
    u32 StrictAffineEnhancedOBJCacheHits = 0;
    u32 StrictAffineEnhancedOBJCacheMisses = 0;
    u32 StrictAffineEnhancedOBJNeuralDispatches = 0;
    u32 StrictAffineEnhancedOBJFallbacks = 0;
    bool StrictAffineOrdinaryOBJNativeStack = false;
    bool StrictAffineResolvedOBJRequested = false;
    bool StrictAffineResolvedOBJOrdinaryProductReady = false;
    bool StrictAffineResolvedOBJOrdinaryProductReused = false;
    bool StrictAffineResolvedOBJMergeExecuted = false;
    u32 StrictAffineResolvedOBJRejectionMask = 0;
    bool StrictAffineOBJOperandPlanRequested = false;
    bool StrictAffineOBJOperandPlanNoAffineExtension = false;
    bool StrictAffineOBJOperandPlanSpecialIsolationReady = false;
    bool StrictAffineOBJOperandPlanCleanBG3DBase = false;
    u32 StrictAffineOBJOperandPlanRejectionMask = 0;
    u32 StrictAffineOBJOperandPlanOperandCount = 0;
    u32 StrictAffineOBJOperandPlanPresentationProductCount = 0;
    u64 StrictAffineOBJOperandPlanHash = 0;
    bool StrictAffineOperandExcludedOverlayRequested = false;
    u32 StrictAffineOperandExcludedOverlayRejectionMask = 0;
    u64 StrictAffineOperandExcludedOverlayOAMHash = 0;
    u32 StrictAffineEnhancedBGActiveLayers = 0;
    u32 StrictAffineEnhancedBGCacheHits = 0;
    u32 StrictAffineEnhancedBGCacheMisses = 0;
    u32 StrictAffineEnhancedBGNeuralDispatches = 0;
    u32 StrictAffineEnhancedBGFallbacks = 0;
    u32 StrictAffineUnderlayCandidateBGMask = 0;
    u32 StrictAffineConstantBackdropProofBGMask = 0;
    u32 StrictAffineConstantBackdropCompositeBGMask = 0;
    u32 StrictAffineBG2UnderlayRejectReasons = 0;
    u32 StrictAffineBG3UnderlayRejectReasons = 0;
    u32 StrictAffineBG2UnderlayBackdropColor = 0;
    u32 StrictAffineBG3UnderlayBackdropColor = 0;
    bool StrictAffineDeferredTextBGAARequested = false;
    bool StrictAffineDeferredTextBGAAExecuted = false;
    bool StrictAffineDeferredOrdinaryOBJCandidateRequested = false;
    bool StrictAffineDeferredOrdinaryOBJCandidateProductReady = false;
    bool StrictAffineDeferredOrdinaryOBJCandidateEvaluated = false;
    bool StrictAffineCandidateValid = false;
    bool StrictAffineNativeReferenceValid = false;
    bool NativeExactFinalValid = false;
    bool PhysicalFinalNativeInputValid = false;
    bool Native3DResolveValid = false;
    bool Native3DSemanticsValid = false;
    bool OverlayTrueFinalValid = false;
    bool HybridFragmentationFallback = false;
    bool CurrentFragmentationFallback = false;
    u32 NativeChunkAccumulationPasses = 0;
    u32 FullFrameFinalizerPasses = 0;
    u32 NativeProductValidRows = 0;
    bool NativeProductsFrameComplete = false;
    bool NativeProductRowIdentityValid = true;
    WholeSceneNativeProductRowIdentityInvalidReason
        NativeProductRowIdentityInvalidReason =
            WholeSceneNativeProductRowIdentityInvalidReason::None;
    u32 NativeProductIdentityRows = 0;
    u64 NativeProductFrameIdentity = 0;
    bool NativeProductEpochValid = true;
    u32 NativeProductEpochInvalidReason = 0;
    WholeSceneScaleEligibility NativeProductFrameEligibility = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneScaleEligibility NativeProductLastEligibility = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneRenderPath NativeProductFramePath = WholeSceneRenderPath::None;
    WholeSceneRenderPath NativeProductLastPath = WholeSceneRenderPath::None;
    bool NativeProductFinalizerPathSeen = false;
    VisibleOBJCaptureDebug VisibleOBJCapture;
};

struct WholeSceneDebugPoisonState
{
    bool Source3D = false;
    bool Native3DResolve = false;
    bool Native3DResolveAlpha = false;
};

struct WholeScenePathDecision
{
    WholeScenePathDecisionReason Reason = WholeScenePathDecisionReason::None;
    WholeSceneRenderPath Path = WholeSceneRenderPath::None;
    WholeSceneCurrentPathReason CurrentReason = WholeSceneCurrentPathReason::None;
    WholeSceneCaptureBackedPlan CapturePlan;
    bool HybridFragmentationFallback = false;
};

struct WholeScenePathDecisionInputs
{
    WholeSceneCaptureBackedPlan CapturePlan;
    bool CurrentFallbackAvailable = false;
    bool PhysicalFinalPostprocessNativeInputAvailable = false;
    bool HybridPresentationGuardActive = false;
    bool SplitLegacyFallbackAvailable = false;
    bool CanUseScalePath = false;
};

WholeScenePathDecision ChooseWholeScenePathDecision(
    const WholeScenePathDecisionInputs& inputs);

struct WholeSceneScaleDecision
{
    WholeSceneScaleDecisionReason Reason = WholeSceneScaleDecisionReason::None;
    WholeSceneRenderPath Path = WholeSceneRenderPath::None;
    WholeSceneCaptureBackedPlan CapturePlan;
    bool HybridFragmentationFallback = false;
};

struct WholeSceneOverlayScaleDecisionInputs
{
    WholeSceneCaptureBackedPlan CapturePlan;
    bool ConservativeHybridMode = false;
    bool HybridFragmentationGuardActive = false;
};

WholeSceneScaleDecision ChooseWholeSceneOverlayScaleDecision(
    const WholeSceneOverlayScaleDecisionInputs& inputs);

struct HybridSourceDecision
{
    HybridSourceDecisionReason Reason = HybridSourceDecisionReason::None;
    WholeSceneRenderPath Path = WholeSceneRenderPath::None;
    bool ConservativeHybridRequested = false;
    bool OverlaySuppressedDirect3D = false;
    bool EffectiveConservativeHybrid = false;
    bool ActiveDirect3D = false;
    bool RenderNativeFallback = false;
    bool RenderForeground2DBase = false;
    bool RenderForegroundCandidate = false;
};

struct HybridSourceDecisionInputs
{
    bool ConservativeHybridRequested = false;
    bool OverlaySuppressedDirect3D = false;
    bool ActiveDirect3D = false;
    bool Foreground2DBaseEnabled = false;
};

HybridSourceDecision ChooseHybridSourceDecision(
    const HybridSourceDecisionInputs& inputs);

// Returns whether a zero-weight Direct3D target1 state needs the
// presentation-overlay path rather than Conservative Hybrid. The caller has
// already established a composited main-engine frame with visible Direct3D.
// visibleNativeLayerMask uses the BLDCNT BG/OBJ bit layout; backdrop visibility
// is implicit when it is selected as target2.
bool ShouldUseSuppressedDirect3DPresentationOverlay(
    u16 blendCnt,
    u8 eva,
    u8 evb,
    u32 visibleNativeLayerMask);

struct IncompleteFinalizerFallbackInputs
{
    int LastLine = 0;
    bool EarlierCurrentChunksValid = false;
};

inline int ChooseIncompleteFinalizerCurrentStart(
    const IncompleteFinalizerFallbackInputs& inputs)
{
    // Unsafe/fragmented frames have already committed their earlier ranges
    // through the current compositor, so keep those chunks. Otherwise the
    // incomplete native-product path has not populated a complete current
    // output and the fallback must rebuild the frame from row zero.
    return inputs.EarlierCurrentChunksValid ? inputs.LastLine : 0;
}

struct CaptureBackedBrightnessReleaseInputs
{
    bool WholeSceneScaleRequested = false;
    int Line = 0;
    u16 PreviousMasterBrightness = 0;
    u16 CurrentMasterBrightness = 0;
    u32 CrossingFullWholeSceneCaptureMask = 0;
};

inline bool ShouldPlanPhysicalBrightnessForCaptureBackedRelease(
    const CaptureBackedBrightnessReleaseInputs& inputs)
{
    if (!inputs.WholeSceneScaleRequested ||
        inputs.Line <= 0 ||
        inputs.CrossingFullWholeSceneCaptureMask == 0)
    {
        return false;
    }

    const u32 previousMode = (inputs.PreviousMasterBrightness >> 14) & 0x3u;
    const u32 previousFactor =
        std::min<u32>(inputs.PreviousMasterBrightness & 0x1Fu, 16u);
    const u32 currentMode = (inputs.CurrentMasterBrightness >> 14) & 0x3u;
    const u32 currentFactor =
        std::min<u32>(inputs.CurrentMasterBrightness & 0x1Fu, 16u);

    return previousMode == 1 &&
           previousFactor == 16 &&
           currentMode == 0 &&
           currentFactor == 0;
}

// Frame-level admission gate for the hybrid clean-legacy candidate: the
// high-resolution legacy compositor output offered to the per-pixel selector.
// Each reason is one checklist item; the checklist is an (informal) channel
// partition — a candidate is admissible only when every effect channel
// downstream of it is either reproduced by the native-stack path or provably
// inactive this frame.
enum class HybridCleanLegacyBlockReason : u8
{
    None = 0,
    PathModeOrSettingUnavailable,
    SubEngine,
    DisplayModeNotComposited,
    NoVisibleDirect3D,
    FinalUpscaleNative3D,
    CaptureTransportActive,
    OBJWindowActive,
    UnsupportedAlphaBlendState,
    CaptureBackedBGLayer,
    CaptureBackedSprite,
};

struct HybridCleanLegacyEligibilityInputs
{
    bool ScalePathAvailable = false;
    bool ConservativeHybridMode = false;
    bool CandidateEnabled = false;
    bool MainEngine = false;
    u32 DispCnt = 0;
    u32 LayerEnable = 0;
    bool HasRenderedPolygons = false;
    bool FinalUpscaleRender3DNative = false;
    bool CaptureTransportActive = false;
    u16 BlendCnt = 0;
    u8 EVA = 0;
    u8 EVB = 0;
    // Any enabled BG layer classified as capture-backed (layer type >= 7) /
    // any capture-classified sprite (type >= 3), computed by the renderer.
    bool AnyEnabledCaptureBackedBGLayer = false;
    bool AnyCaptureBackedSprite = false;
};

// The validated-alpha-states rule from the NSMB target1 case matrix: which
// BLDCNT mode-1 states leave the clean-legacy candidate coherent.
bool IsCleanLegacyAlphaBlendStateValidated(u16 blendCnt, u8 eva, u8 evb);

HybridCleanLegacyBlockReason ChooseHybridCleanLegacyBlockReason(
    const HybridCleanLegacyEligibilityInputs& inputs);

inline bool CanUseHybridCleanLegacyCandidate(
    const HybridCleanLegacyEligibilityInputs& inputs)
{
    return ChooseHybridCleanLegacyBlockReason(inputs) ==
           HybridCleanLegacyBlockReason::None;
}

struct WholeSceneUpdatePhaseTiming
{
    u64 TotalUS = 0;
    u64 MaxUS = 0;
    u32 Count = 0;
};

struct WholeSceneUpdateTimingState
{
    WholeSceneUpdatePhaseTiming StateDiff;
    WholeSceneUpdatePhaseTiming VRAMFlatten;
    WholeSceneUpdatePhaseTiming LayerDirty;
    WholeSceneUpdatePhaseTiming Classify;
    WholeSceneUpdatePhaseTiming SpriteRender;
    WholeSceneUpdatePhaseTiming PartialComposite;
    WholeSceneUpdatePhaseTiming RegisterCache;
    WholeSceneUpdatePhaseTiming BGUpload;
    WholeSceneUpdatePhaseTiming BGPaletteUpload;
    WholeSceneUpdatePhaseTiming LayerPrerender;
    WholeSceneUpdatePhaseTiming OBJPrerender;
    WholeSceneUpdatePhaseTiming VBlankSpriteRender;
    WholeSceneUpdatePhaseTiming VBlankComposite;
    WholeSceneUpdatePhaseTiming NativePrepass;
    WholeSceneUpdatePhaseTiming NativeExactFinal;
    WholeSceneUpdatePhaseTiming OverlayBlackExactFinal;
    WholeSceneUpdatePhaseTiming OverlayWhiteExactFinal;
    WholeSceneUpdatePhaseTiming OverlayTrueExactFinal;
    WholeSceneUpdatePhaseTiming OverlayEndpoint;
    WholeSceneUpdatePhaseTiming Hybrid2DBaseCandidate;
    WholeSceneUpdatePhaseTiming HybridLegacyCandidate;
    WholeSceneUpdatePhaseTiming HybridForegroundCandidate;
    WholeSceneUpdatePhaseTiming FinalizerUpscale;
    WholeSceneUpdatePhaseTiming FinalizerComposite;
};

static constexpr int kWholeSceneDebugRangeRecordLimit = 16;

struct WholeSceneUpdateDebugTrace
{
    u32 LayerDirtyEvents = 0;
    u32 LayerDirtyMask = 0;
    u32 LayerDirtyOverflow = 0;
    int LayerDirtyLine[kWholeSceneDebugRangeRecordLimit] = {};
    u8 LayerDirtyEventMask[kWholeSceneDebugRangeRecordLimit] = {};
    u32 RegisterLayerDirtyMask = 0;
    u32 VRAMLayerDirtyMask = 0;
    u32 PaletteLayerDirtyMask = 0;
    u32 DeferredLayerDirtyMask = 0;
    u32 InactiveDeferredLayerDirtyMask = 0;
    u32 FlatVRAMCaptureSyncLayerMask = 0;
    u32 CoveredBitmapMask = 0;
    u32 ContributingLayerMask = 0;

    u32 StateDirtyEvents = 0;
    u32 StateDirtyReasonMask = 0;
    u32 StateDirtyDispCntDiff = 0;
    u32 StateDirtyLayerEnableDiff = 0;
    u16 StateDirtyBGCntDiff[4] = {};
    u32 StateDirtyMiscDiffMask = 0;

    u32 FullFrameUnsafeEvents = 0;
    u32 FullFrameUnsafeReasonMask = 0;
    int FullFrameUnsafeFirstLine = -1;
    int FullFrameUnsafeLastLine = -1;
    u32 FullFrameUnsafeLayerMask = 0;
    u32 FullFrameUnsafeDispCntDiff = 0;
    u32 FullFrameUnsafeLayerEnableDiff = 0;
    u16 FullFrameUnsafeBGCntDiff[4] = {};
    u32 FullFrameUnsafeMiscDiffMask = 0;

    u32 BGUploadCalls = 0;
    u32 BGUploadRangeCount = 0;
    u32 BGUploadRangeOverflow = 0;
    u32 BGUploadRows = 0;
    int BGUploadFirstRow = -1;
    int BGUploadLastRow = -1;
    int BGUploadLine[kWholeSceneDebugRangeRecordLimit] = {};
    int BGUploadStartRow[kWholeSceneDebugRangeRecordLimit] = {};
    int BGUploadEndRow[kWholeSceneDebugRangeRecordLimit] = {};

    u32 LayerPrerenderCalls = 0;
    u32 LayerPrerenderMask = 0;
    u32 LayerPrerenderBitmapMask = 0;
    u32 LayerPrerenderOverflow = 0;
    int LayerPrerenderLine[kWholeSceneDebugRangeRecordLimit] = {};
    u8 LayerPrerenderEventMask[kWholeSceneDebugRangeRecordLimit] = {};
    u32 VisibleBitmapDirtyMask = 0;
    u32 VisibleBitmapVRAMDirtyMask = 0;
    u32 VisibleBitmapDirtyBeforeLineMask = 0;
    u32 VisibleBitmapDirtyAfterLineMask = 0;
    u32 VisibleBitmapDirtyCrossesLineMask = 0;
    u32 VisibleBitmapRowLimitedPrerenderMask = 0;
    u32 VisibleBitmapCoveredDirtyDeferredMask = 0;
    int VisibleBitmapDirtyFirstRow = -1;
    int VisibleBitmapDirtyLastRow = -1;
    int VisibleBitmapLayerFirstRow[4] = {-1, -1, -1, -1};
    int VisibleBitmapLayerLastRow[4] = {-1, -1, -1, -1};
    int VisibleBitmapRowLimitedFirstRow[4] = {-1, -1, -1, -1};
    int VisibleBitmapRowLimitedLastRow[4] = {-1, -1, -1, -1};

    u32 PartialCompositeRangeCount = 0;
    u32 PartialCompositeRangeOverflow = 0;
    int PartialCompositeStart[kWholeSceneDebugRangeRecordLimit] = {};
    int PartialCompositeEnd[kWholeSceneDebugRangeRecordLimit] = {};
};

}
