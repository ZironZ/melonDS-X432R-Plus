/*
    Copyright 2016-2026 melonDS team

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
    None,
    CaptureBackedProducerDuringHybridGuard,
    HybridPresentationGuard,
    CaptureBackedBeforeGeneralFallbacks,
    ChunkedUnsafeOverlay,
    PhysicalFinalPostprocessNativeInput,
    SplitLegacyFallback,
    CaptureBackedAfterGeneralFallbacks,
    FragmentationOrUnsafeFrameCurrentFallback,
    WholeSceneScale,
    ScalePathUnavailable,
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
};

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

enum class SourceACaptureReplacementMode
{
    None,
    FullProduct,
    CurrentOverlay,
    FullProductAfterOverlayFailed,
};

enum class SourceAProductChoiceReason
{
    None,
    UsedFullProductKeyMatch,
    UsedFullProductNoOverlayVisible,
    UsedBackgroundUnderlayCurrentOverlay,
    RejectedFullProductKeyMismatch,
    RejectedCurrentOverlayKeyMismatch,
    RejectedMissingBackgroundProduct,
    RejectedMissingFullProduct,
    ReusedPreviousRouteProduct,
    FallbackNormalHybrid,
    FallbackFinalImage,
    UsedFullProductRouteBridge,
    DeferredLiveScenePromotion,
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

inline const char* SourceACaptureReplacementModeName(
    SourceACaptureReplacementMode mode)
{
    switch (mode)
    {
    case SourceACaptureReplacementMode::FullProduct:
        return "full captured product";
    case SourceACaptureReplacementMode::CurrentOverlay:
        return "captured 3D background plus current overlay";
    case SourceACaptureReplacementMode::FullProductAfterOverlayFailed:
        return "full captured product after current overlay unavailable/failed";
    case SourceACaptureReplacementMode::None:
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

struct WholeSceneRenderTrace
{
    WholeSceneRenderPath Path = WholeSceneRenderPath::None;
    WholeSceneCurrentPathReason CurrentReason = WholeSceneCurrentPathReason::None;
    WholeSceneNative3DSource Native3DSource = WholeSceneNative3DSource::None;
    WholeSceneOverlayEndpointFinalMode OverlayEndpointFinalMode = WholeSceneOverlayEndpointFinalMode::None;
    SourceACaptureReplacementMode SourceACaptureMode = SourceACaptureReplacementMode::None;
    SourceAProductChoiceReason SourceAProductChoice = SourceAProductChoiceReason::None;
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
    bool CaptureProductUsePresentationCompatible = false;
    bool CaptureProductPresentationHashMatch = false;
    u32 CaptureProductStoredEffectOwner = 0;
    u32 CaptureProductStoredEffectState = 0;
    u32 CaptureProductConsumeEffectOwner = 0;
    u32 CaptureProductConsumeEffectState = 0;
    u32 CaptureProductEffectAction = 0;
    bool CaptureProductEffectPhaseIncompatible = false;
    u32 CaptureProductFinalPassEffectOwner = 0;
    bool CaptureProductApplyEffectOnBlit = false;
    bool OutputPresentationMasterBrightnessApplied = false;
    u32 OutputPresentationEffectOwner = 0;
    u32 OutputPresentationEffectState = 0;
    int OutputPresentationTex = 0;
    bool SourceAFullProductKeyMatch = false;
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
    bool ConservativeHybrid = false;
    bool HybridFragmentationFallback = false;
};

struct WholeScenePathDecisionInputs
{
    WholeSceneCaptureBackedPlan CapturePlan;
    bool ChunkedUnsafeOverlayAvailable = false;
    bool CurrentFallbackAvailable = false;
    bool PhysicalFinalPostprocessNativeInputAvailable = false;
    bool HybridPresentationGuardActive = false;
    bool SplitLegacyFallbackAvailable = false;
    bool CanUseScalePath = false;
    bool ConservativeHybridMode = false;
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
