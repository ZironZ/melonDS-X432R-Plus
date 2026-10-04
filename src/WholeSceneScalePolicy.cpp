// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WholeSceneScalePolicy.h"
#include "WholeSceneOutputPlan.h"

namespace melonDS
{

namespace
{

constexpr u32 StrictAffineOBJLayer = 1u << 4;

constexpr u64 StrictAffineAllowedChannels =
    static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::Priority) |
    static_cast<u64>(WholeSceneOutputChannel::Palette) |
    static_cast<u64>(WholeSceneOutputChannel::ColorEffect) |
    static_cast<u64>(WholeSceneOutputChannel::AlphaBlend) |
    static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ) |
    static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
    static_cast<u64>(WholeSceneOutputChannel::MasterBrightness) |
    static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

constexpr u64 StrictAffineOBJOnlyAllowedChannels =
    static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::Direct3D) |
    static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
    static_cast<u64>(WholeSceneOutputChannel::Priority) |
    static_cast<u64>(WholeSceneOutputChannel::Palette) |
    static_cast<u64>(WholeSceneOutputChannel::AlphaBlend) |
    static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ) |
    static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
    static_cast<u64>(WholeSceneOutputChannel::MasterBrightness) |
    static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

int LayerCount(u32 mask)
{
    int count = 0;
    while (mask != 0)
    {
        mask &= mask - 1u;
        count++;
    }
    return count;
}

bool HasStrictAffineSupportedAlphaBlend(
    const StrictAffineHighResEligibilityInputs& inputs,
    bool objVisible)
{
    const u64 alphaBlend =
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend);
    if ((inputs.RequiredChannels & alphaBlend) == 0)
        return true;

    if (inputs.AlphaBlendTarget1Mask == 0 ||
        inputs.AlphaBlendTarget2Mask == 0)
    {
        return false;
    }

    // Affine OBJ remains in the output-resolution candidate and can
    // participate in the compositor's exact DS blend arithmetic. Ordinary
    // OBJ is reconstructed afterward from the native stack, so keep it out
    // of the blend operands and require it to win ahead of every active
    // target-1 BG. That preserves its edge reconstruction without allowing
    // an omitted ordinary OBJ to become the candidate's second operand.
    constexpr u32 objTarget = 1u << 4;
    if (objVisible && inputs.HasOrdinaryOBJ)
    {
        if (((inputs.AlphaBlendTarget1Mask |
              inputs.AlphaBlendTarget2Mask) & objTarget) != 0)
        {
            return false;
        }
        if (!inputs.AlphaBlendOrdinaryOBJPrioritySafe)
            return false;
    }

    return true;
}

bool HasStrictAffineSupportedColorEffect(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    const u64 colorEffect =
        static_cast<u64>(WholeSceneOutputChannel::ColorEffect);
    if ((inputs.RequiredChannels & colorEffect) == 0)
        return true;

    // Brightness up/down is a winner-local, resolution-independent DS
    // compositor operation. The strict tiled-affine compositor retains the
    // winning semantic layer at output resolution, so arbitrary Target-1
    // subsets are reproducible without treating an unaffected upper layer as
    // part of the affected underlay. Other special-effect modes remain
    // separate capabilities.
    return (inputs.ColorEffectMode == 2 ||
            inputs.ColorEffectMode == 3) &&
           inputs.ColorEffectFactor > 0 &&
           inputs.ColorEffectFactor <= 16 &&
           inputs.AlphaBlendTarget1Mask != 0;
}

bool HasStrictAffineTiledSceneChannels(
    const StrictAffineHighResEligibilityInputs& inputs,
    u32 affine,
    u32 text,
    bool objVisible)
{
    const u64 semiTransparentOBJ =
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ);
    if ((inputs.RequiredChannels & semiTransparentOBJ) != 0)
    {
        // Mode-1 transparency is authored material blending, whether the
        // sprite geometry is ordinary or affine. Native/binary presence can
        // drive exact DS EVA/EVB arithmetic over the high-res underlay
        // without inventing presentation alpha. Bitmap OBJ's per-pixel alpha
        // remains a separate, unsupported capability.
        if (!objVisible ||
            (!inputs.HasSemiTransparentOrdinaryOBJ &&
             !inputs.HasSemiTransparentAffineOBJ) ||
            inputs.HasBitmapOrdinaryOBJ ||
            inputs.HasBitmapAffineOBJ ||
            inputs.HasUnsupportedSemiTransparentOBJ ||
            inputs.AlphaBlendTarget2Mask == 0)
        {
            return false;
        }
    }

    u64 requiredGeometry =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry);
    if (text != 0)
        requiredGeometry |=
            static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry);
    if (objVisible)
        requiredGeometry |=
            static_cast<u64>(WholeSceneOutputChannel::OBJGeometry);

    if ((inputs.RequiredChannels & requiredGeometry) != requiredGeometry)
        return false;
    if ((inputs.RequiredChannels & ~StrictAffineAllowedChannels) != 0)
        return false;

    const u64 textGeometry =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry);
    const u64 objGeometry =
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    if (text == 0 && (inputs.RequiredChannels & textGeometry) != 0)
        return false;
    if (!objVisible && (inputs.RequiredChannels & objGeometry) != 0)
        return false;

    const int visibleCount = LayerCount(affine | text) +
        (objVisible ? 1 : 0);
    const u64 priority =
        static_cast<u64>(WholeSceneOutputChannel::Priority);
    return visibleCount <= 1 || (inputs.RequiredChannels & priority) != 0;
}

bool IsStrictAffineOBJOnlyTopology(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    constexpr u64 affineOBJ =
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    constexpr u64 direct3D =
        static_cast<u64>(WholeSceneOutputChannel::Direct3D);
    constexpr u64 objGeometry =
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry);

    const u32 visible = inputs.VisibleLayerMask;
    const u32 text = visible & inputs.TextTiledBGMask;
    const u32 direct3DLayer = visible & inputs.Direct3DLayerMask;
    const bool objVisible = (visible & StrictAffineOBJLayer) != 0;
    const u32 affineBG = visible &
        (inputs.TiledAffineBGMask | inputs.ExtendedTiledAffineBGMask);
    const u32 modeledVisible = text | direct3DLayer |
        (objVisible ? StrictAffineOBJLayer : 0u);

    // Affine OBJ geometry does not require an affine BG or Direct3D underlay.
    // Admit any reproducible tiled-text/optional-BG0-3D stack and let the
    // semantic compositor plus native-stack merger preserve the lower scene.
    return affineBG == 0 &&
           (direct3DLayer == 0 || direct3DLayer == 1u) &&
           objVisible &&
           visible == modeledVisible &&
           (inputs.RequiredChannels & (objGeometry | affineOBJ)) ==
               (objGeometry | affineOBJ) &&
           ((direct3DLayer != 0) ==
            ((inputs.RequiredChannels & direct3D) != 0));
}

bool HasStrictAffineOBJOnlyChannels(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    if ((inputs.RequiredChannels &
         ~StrictAffineOBJOnlyAllowedChannels) != 0)
    {
        return false;
    }

    const u32 visible = inputs.VisibleLayerMask;
    const u32 text = visible & inputs.TextTiledBGMask;
    const u64 textGeometry =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry);
    if ((text != 0) != ((inputs.RequiredChannels & textGeometry) != 0))
        return false;

    const u32 direct3DLayer = visible & inputs.Direct3DLayerMask;
    const u64 direct3D =
        static_cast<u64>(WholeSceneOutputChannel::Direct3D);
    if ((direct3DLayer != 0) !=
        ((inputs.RequiredChannels & direct3D) != 0))
    {
        return false;
    }

    const u64 alphaBlend =
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend);
    const u64 semiTransparentOBJ =
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ);
    const bool materialChannelsActive =
        (inputs.RequiredChannels & (alphaBlend | semiTransparentOBJ)) != 0;
    if (materialChannelsActive)
    {
        // This first generic material extension is deliberately exact and
        // narrow: bitmap OBJ carries its own 0..16 alpha, the strict semantic
        // compositor evaluates it against the real target-2 winner, and
        // reconstructed affine coverage is applied only after that completed
        // color exists. Mode-1 EVA/EVB remains a separate capability here.
        if (inputs.HasUnsupportedSemiTransparentOBJ ||
            inputs.HasSemiTransparentOrdinaryOBJ ||
            inputs.HasSemiTransparentAffineOBJ ||
            !inputs.HasBitmapAffineOBJ ||
            inputs.AlphaBlendTarget2Mask == 0)
        {
            return false;
        }
    }
    else if (inputs.HasBitmapOrdinaryOBJ || inputs.HasBitmapAffineOBJ ||
             inputs.HasUnsupportedSemiTransparentOBJ)
    {
        return false;
    }

    const int visibleCount = LayerCount(visible & 0xFu) + 1;
    const u64 priority =
        static_cast<u64>(WholeSceneOutputChannel::Priority);
    return visibleCount <= 1 || (inputs.RequiredChannels & priority) != 0;
}

bool IsStrictAffineDualExtendedAffineWithOBJTopology(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    const u32 extendedAffine =
        inputs.VisibleLayerMask & inputs.ExtendedTiledAffineBGMask;
    const u32 extendedWithoutLowest = extendedAffine & (extendedAffine - 1u);
    const bool exactlyTwoExtendedAffine =
        extendedWithoutLowest != 0 &&
        (extendedWithoutLowest & (extendedWithoutLowest - 1u)) == 0;
    return exactlyTwoExtendedAffine &&
           inputs.VisibleLayerMask == (extendedAffine | StrictAffineOBJLayer) &&
           (extendedAffine & inputs.TiledAffineBGMask) == 0 &&
           (extendedAffine & inputs.TextTiledBGMask) == 0;
}

bool HasStrictAffineDualExtendedAffineWithOBJChannels(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    const u32 extendedAffine =
        inputs.VisibleLayerMask & inputs.ExtendedTiledAffineBGMask;
    return HasStrictAffineTiledSceneChannels(inputs,
                                             extendedAffine,
                                             0,
                                             true) &&
           HasStrictAffineSupportedAlphaBlend(inputs, true);
}

StrictAffineHighResBlockReason ChooseStrictAffineHighResCommonBlockReason(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    if (!inputs.FeatureEnabled)
        return StrictAffineHighResBlockReason::FeatureDisabled;
    if (!inputs.ConservativeHybridMode)
        return StrictAffineHighResBlockReason::NotConservativeHybrid;
    if (!inputs.ScalePathAvailable)
        return StrictAffineHighResBlockReason::ScalePathUnavailable;
    if (inputs.OutputScale <= 1)
        return StrictAffineHighResBlockReason::ScaleFactorOne;
    if (inputs.YStart != 0 || inputs.YEnd != 192)
        return StrictAffineHighResBlockReason::IncompleteRange;
    if (!inputs.DisplayModeComposited)
        return StrictAffineHighResBlockReason::DisplayModeNotComposited;
    if (!inputs.SnapshotCoherent)
        return StrictAffineHighResBlockReason::SnapshotIncoherent;
    if (!inputs.CandidateTargetAvailable)
        return StrictAffineHighResBlockReason::CandidateTargetUnavailable;
    return StrictAffineHighResBlockReason::None;
}

}

StrictAffineHighResBlockReason ChooseStrictAffineHighResBlockReason(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    const StrictAffineHighResBlockReason commonBlockReason =
        ChooseStrictAffineHighResCommonBlockReason(inputs);
    if (commonBlockReason != StrictAffineHighResBlockReason::None)
        return commonBlockReason;

    if (!inputs.HighResolutionGeometryRequired)
    {
        return StrictAffineHighResBlockReason::
            HighResolutionGeometryNotRequired;
    }

    const u32 visible = inputs.VisibleLayerMask;
    if (visible == 0)
        return StrictAffineHighResBlockReason::NotExactlyOneVisibleLayer;

    if (IsStrictAffineOBJOnlyTopology(inputs))
    {
        return HasStrictAffineOBJOnlyChannels(inputs)
            ? StrictAffineHighResBlockReason::None
            : StrictAffineHighResBlockReason::UnsupportedActiveChannel;
    }

    // Both DS tiled-affine encodings use the same output-resolution geometry
    // path. Keep admission capability-shaped: one or two affine BGs, any
    // remaining tiled text BGs, and optional OBJ are safe when the active
    // semantic channels stay inside the strict compositor's modeled set.
    const u32 affine = visible &
        (inputs.TiledAffineBGMask | inputs.ExtendedTiledAffineBGMask);
    if (affine == 0 && LayerCount(visible) == 1)
        return StrictAffineHighResBlockReason::VisibleLayerNotTiledAffineBG;
    if (affine == 0 || LayerCount(affine) > 2)
        return StrictAffineHighResBlockReason::NotExactlyOneAffineBG;

    const u32 text = visible & inputs.TextTiledBGMask;
    const bool objVisible = (visible & StrictAffineOBJLayer) != 0;
    const u32 modeledVisible = affine | text |
        (objVisible ? StrictAffineOBJLayer : 0u);
    if (visible != modeledVisible)
        return StrictAffineHighResBlockReason::UnsupportedCompanionLayer;

    if (!HasStrictAffineSupportedAlphaBlend(inputs, objVisible))
    {
        return StrictAffineHighResBlockReason::
            UnsupportedAlphaBlendConfiguration;
    }

    if (!HasStrictAffineSupportedColorEffect(inputs))
        return StrictAffineHighResBlockReason::UnsupportedActiveChannel;

    if (!HasStrictAffineTiledSceneChannels(inputs,
                                           affine,
                                           text,
                                           objVisible))
    {
        return StrictAffineHighResBlockReason::UnsupportedActiveChannel;
    }

    return StrictAffineHighResBlockReason::None;
}

DeferredScanlineStrictAffineAssessment
AssessDeferredScanlineStrictAffine(
    const DeferredScanlineStrictAffineInputs& inputs)
{
    DeferredScanlineStrictAffineAssessment assessment = {};
    auto reject = [&](DeferredScanlineStrictAffineBlockReason reason)
    {
        assessment.BlockReason = reason;
        return assessment;
    };

    if (!inputs.FeatureEnabled)
        return reject(DeferredScanlineStrictAffineBlockReason::FeatureDisabled);
    if (!inputs.ConservativeHybridMode)
        return reject(DeferredScanlineStrictAffineBlockReason::NotConservativeHybrid);
    if (inputs.OutputScale <= 1)
        return reject(DeferredScanlineStrictAffineBlockReason::ScaleFactorOne);
    if (!inputs.CandidateTargetAvailable)
        return reject(DeferredScanlineStrictAffineBlockReason::CandidateTargetUnavailable);
    if (!inputs.NativeFrameComplete)
        return reject(DeferredScanlineStrictAffineBlockReason::NativeFrameIncomplete);
    if (!inputs.NativeRowIdentityValid)
        return reject(DeferredScanlineStrictAffineBlockReason::NativeRowIdentityInvalid);
    if (!inputs.RouteAndPresentationUniform)
        return reject(DeferredScanlineStrictAffineBlockReason::RouteOrPresentationMismatch);
    if (!inputs.AllRowsSupported)
        return reject(DeferredScanlineStrictAffineBlockReason::UnsupportedRowState);
    if (inputs.OrdinaryOBJPresentationRequired &&
        !inputs.OrdinaryOBJPresentationSupported)
    {
        return reject(DeferredScanlineStrictAffineBlockReason::
                          UnsupportedOrdinaryOBJPresentation);
    }
    if (!inputs.HighResolutionGeometryRequired)
        return reject(DeferredScanlineStrictAffineBlockReason::HighResolutionGeometryNotRequired);

    const bool useLegacyHistoricalSource =
        inputs.HistoricalSourceEpochCount == 0 &&
        inputs.HistoricalSourceAvailable &&
        inputs.HistoricalSourceYEnd > 0 &&
        inputs.HistoricalSourceYEnd < 192 &&
        inputs.HistoricalSourceGeneration != 0 &&
        inputs.HistoricalSourceGeneration != inputs.LiveSourceGeneration;
    const u32 historicalEpochCount = useLegacyHistoricalSource
        ? 1u : inputs.HistoricalSourceEpochCount;
    if (historicalEpochCount >
        DeferredScanlineStrictAffineInputs::MaxHistoricalSourceEpochs)
    {
        return reject(DeferredScanlineStrictAffineBlockReason::
                          NativeRowSourceMismatch);
    }

    int historicalEpochYStart[
        DeferredScanlineStrictAffineInputs::MaxHistoricalSourceEpochs] = {};
    int historicalEpochYEnd[
        DeferredScanlineStrictAffineInputs::MaxHistoricalSourceEpochs] = {};
    u64 historicalEpochGeneration[
        DeferredScanlineStrictAffineInputs::MaxHistoricalSourceEpochs] = {};
    int expectedStart = 0;
    for (u32 epoch = 0; epoch < historicalEpochCount; epoch++)
    {
        const bool available = useLegacyHistoricalSource
            ? inputs.HistoricalSourceAvailable
            : inputs.HistoricalSourceEpochAvailable[epoch];
        const int ystart = useLegacyHistoricalSource
            ? 0 : inputs.HistoricalSourceEpochYStart[epoch];
        const int yend = useLegacyHistoricalSource
            ? inputs.HistoricalSourceYEnd
            : inputs.HistoricalSourceEpochYEnd[epoch];
        const u64 generation = useLegacyHistoricalSource
            ? inputs.HistoricalSourceGeneration
            : inputs.HistoricalSourceEpochGeneration[epoch];
        if (!available || ystart != expectedStart || yend <= ystart ||
            yend >= 192 || generation == 0)
        {
            return reject(DeferredScanlineStrictAffineBlockReason::
                              NativeRowSourceMismatch);
        }
        historicalEpochYStart[epoch] = ystart;
        historicalEpochYEnd[epoch] = yend;
        historicalEpochGeneration[epoch] = generation;
        expectedStart = yend;
    }

    const bool useHistoricalSource = historicalEpochCount != 0;
    u64 uniformSourceGeneration = 0;
    for (int y = 0; y < 192; y++)
    {
        if (!inputs.RowValid[y] || inputs.RowSourceGeneration[y] == 0)
            return reject(DeferredScanlineStrictAffineBlockReason::NativeRowMissing);
        const u64 liveRowSourceGeneration =
            inputs.LiveRowSourceGeneration[y] != 0
                ? inputs.LiveRowSourceGeneration[y]
                : inputs.LiveSourceGeneration;
        if (useHistoricalSource)
        {
            u64 expectedGeneration = liveRowSourceGeneration;
            for (u32 epoch = 0; epoch < historicalEpochCount; epoch++)
            {
                if (y < historicalEpochYStart[epoch] ||
                    y >= historicalEpochYEnd[epoch])
                {
                    continue;
                }
                const u64 rowGeneration = useLegacyHistoricalSource
                    ? inputs.HistoricalRowSourceGeneration[y]
                    : inputs.HistoricalSourceEpochRowGeneration[epoch][y];
                expectedGeneration = rowGeneration != 0
                    ? rowGeneration : historicalEpochGeneration[epoch];
                break;
            }
            if (inputs.RowSourceGeneration[y] != expectedGeneration)
                return reject(DeferredScanlineStrictAffineBlockReason::NativeRowSourceMismatch);
        }
        else if (inputs.LiveRowSourceGeneration[y] != 0)
        {
            if (inputs.RowSourceGeneration[y] != liveRowSourceGeneration)
                return reject(DeferredScanlineStrictAffineBlockReason::LiveSourceMismatch);
        }
        else if (uniformSourceGeneration == 0)
        {
            uniformSourceGeneration = inputs.RowSourceGeneration[y];
        }
        else if (uniformSourceGeneration != inputs.RowSourceGeneration[y])
        {
            return reject(DeferredScanlineStrictAffineBlockReason::NativeRowSourceMismatch);
        }
    }

    if (!useHistoricalSource && uniformSourceGeneration != 0 &&
        uniformSourceGeneration != inputs.LiveSourceGeneration)
        return reject(DeferredScanlineStrictAffineBlockReason::LiveSourceMismatch);

    assessment.Eligible = true;
    assessment.BlockReason = DeferredScanlineStrictAffineBlockReason::None;
    assessment.SourceGeneration = inputs.LiveSourceGeneration;
    assessment.HistoricalSourceUsed = useHistoricalSource;
    assessment.HistoricalSourceYEnd =
        useHistoricalSource
            ? historicalEpochYEnd[historicalEpochCount - 1] : 0;
    assessment.HistoricalSourceGeneration =
        useHistoricalSource ? historicalEpochGeneration[0] : 0;
    assessment.HistoricalSourceEpochCount = historicalEpochCount;
    return assessment;
}

StrictAffineDebugProductClass ChooseStrictAffineDebugProductClass(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    if (inputs.OutputScale < 1)
        return StrictAffineDebugProductClass::None;

    // A scale-1 run is the control side of the paired evaluator. Normalize
    // only the visibility-of-scaling prerequisite; every semantic and
    // resource requirement still applies.
    StrictAffineHighResEligibilityInputs debugInputs = inputs;
    if (debugInputs.OutputScale == 1)
        debugInputs.OutputScale = 2;
    // Debug products remain available as the counterfactual strict rendering
    // when production deliberately uses the equivalent native geometry.
    debugInputs.HighResolutionGeometryRequired = true;

    if (ChooseStrictAffineHighResCommonBlockReason(debugInputs) !=
        StrictAffineHighResBlockReason::None)
    {
        return StrictAffineDebugProductClass::None;
    }

    if (IsStrictAffineDualExtendedAffineWithOBJTopology(debugInputs) &&
        HasStrictAffineDualExtendedAffineWithOBJChannels(debugInputs))
    {
        return StrictAffineDebugProductClass::DualExtendedAffineWithOBJ;
    }

    if (CanUseStrictAffineHighResCandidate(debugInputs))
        return StrictAffineDebugProductClass::ProductionEligible;

    return StrictAffineDebugProductClass::None;
}

bool CanProduceStrictAffineDebugProducts(
    const StrictAffineHighResEligibilityInputs& inputs)
{
    return ChooseStrictAffineDebugProductClass(inputs) !=
           StrictAffineDebugProductClass::None;
}

StrictAffineSourceEnhancementDecision ChooseStrictAffineSourceEnhancement(
    const StrictAffineSourceEnhancementInputs& inputs)
{
    if (!inputs.Enabled)
        return StrictAffineSourceEnhancementDecision::Disabled;
    if (!inputs.StrictAffinePathSelected)
        return StrictAffineSourceEnhancementDecision::StrictAffinePathNotSelected;
    if (inputs.InlineSpline36Available)
        return StrictAffineSourceEnhancementDecision::Spline36Inline;
    if (inputs.CachedArtCNNAvailable)
        return StrictAffineSourceEnhancementDecision::ArtCNNCachedAffineSources;
    if (inputs.CachedCuNNyAvailable)
        return StrictAffineSourceEnhancementDecision::CuNNyCachedAffineSources;
    if (inputs.CachedNNEDI3Available)
        return StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources;
    if (inputs.CachedXBRZAvailable)
        return StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources;
    return StrictAffineSourceEnhancementDecision::AlgorithmUnsupported;
}

bool CanDeferWholeSceneNativeProductEpoch(bool epochValid, u32 invalidReason)
{
    return (epochValid && invalidReason == WholeSceneNativeProductEpochInvalidNone) ||
           (!epochValid && invalidReason == WholeSceneNativeProductEpochInvalidMidFrameState);
}

bool CanFinalizeWholeSceneRowOwnedNativeProduct(
    const WholeSceneNativeProductFinalizerInputs& inputs)
{
    return inputs.FinalizerPathSeen &&
           CanDeferWholeSceneNativeProductEpoch(inputs.EpochValid, inputs.EpochInvalidReason) &&
           inputs.RowIdentityValid &&
           inputs.IdentityRows == 192 &&
           inputs.FrameIdentity != 0 &&
           inputs.FrameComplete;
}

WholeScenePathDecision ChooseWholeScenePathDecision(
    const WholeScenePathDecisionInputs& inputs)
{
    WholeScenePathDecision decision = {};
    decision.CapturePlan = inputs.CapturePlan;

    const bool useCaptureBackedProducerDuringHybridGuard =
        inputs.HybridPresentationGuardActive &&
        decision.CapturePlan.Role == WholeSceneCaptureBackedPlanRole::RouteProducer &&
        decision.CapturePlan.CanRunDuringHybridPresentationGuard;

    if (useCaptureBackedProducerDuringHybridGuard)
    {
        decision.Reason = WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard;
        decision.Path = WholeSceneRenderPathForCaptureBackedPlanKind(decision.CapturePlan.Kind);
        return decision;
    }

    if (inputs.HybridPresentationGuardActive)
    {
        decision.Reason = WholeScenePathDecisionReason::HybridPresentationGuard;
        decision.Path = WholeSceneRenderPath::Current;
        decision.CurrentReason = WholeSceneCurrentPathReason::HybridPresentationGuard;
        return decision;
    }

    if (decision.CapturePlan.Stage == WholeSceneCaptureBackedPlanStage::BeforeGeneralFallbacks)
    {
        decision.Reason = WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks;
        decision.Path = WholeSceneRenderPathForCaptureBackedPlanKind(decision.CapturePlan.Kind);
        return decision;
    }

    if (inputs.PhysicalFinalPostprocessNativeInputAvailable)
    {
        decision.Reason = WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput;
        decision.Path = WholeSceneRenderPath::PhysicalFinalPostprocessInput;
        return decision;
    }

    if (inputs.SplitLegacyFallbackAvailable)
    {
        decision.Reason = WholeScenePathDecisionReason::SplitLegacyFallback;
        decision.Path = WholeSceneRenderPath::LegacyNativeUpscale;
        return decision;
    }

    if (decision.CapturePlan.Stage == WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks)
    {
        decision.Reason = WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks;
        decision.Path = WholeSceneRenderPathForCaptureBackedPlanKind(decision.CapturePlan.Kind);
        return decision;
    }

    if (inputs.CurrentFallbackAvailable)
    {
        decision.Reason = WholeScenePathDecisionReason::FragmentationOrUnsafeFrameCurrentFallback;
        decision.Path = WholeSceneRenderPath::Current;
        decision.CurrentReason = WholeSceneCurrentPathReason::FragmentationOrUnsafeFrame;
        return decision;
    }

    if (inputs.CanUseScalePath)
    {
        decision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
        return decision;
    }

    decision.Reason = WholeScenePathDecisionReason::ScalePathUnavailable;
    decision.Path = WholeSceneRenderPath::Current;
    decision.CurrentReason = WholeSceneCurrentPathReason::ScalePathUnavailable;
    return decision;
}

HybridSourceDecision ChooseHybridSourceDecision(
    const HybridSourceDecisionInputs& inputs)
{
    HybridSourceDecision decision = {};
    decision.ConservativeHybridRequested = inputs.ConservativeHybridRequested;
    decision.OverlaySuppressedDirect3D =
        inputs.ConservativeHybridRequested && inputs.OverlaySuppressedDirect3D;
    decision.EffectiveConservativeHybrid =
        inputs.ConservativeHybridRequested && !decision.OverlaySuppressedDirect3D;
    decision.ActiveDirect3D = inputs.ActiveDirect3D;
    decision.RenderNativeFallback =
        decision.EffectiveConservativeHybrid && !decision.ActiveDirect3D;
    decision.RenderForeground2DBase =
        decision.EffectiveConservativeHybrid &&
        decision.ActiveDirect3D &&
        inputs.Foreground2DBaseEnabled;
    decision.RenderForegroundCandidate =
        decision.EffectiveConservativeHybrid && decision.ActiveDirect3D;
    decision.Path = decision.EffectiveConservativeHybrid
        ? WholeSceneRenderPath::ConservativeHybridUpscale
        : WholeSceneRenderPath::OverlayOperatorUpscale;

    if (decision.OverlaySuppressedDirect3D)
        decision.Reason = HybridSourceDecisionReason::SuppressedDirect3DOverlay;
    else if (!decision.EffectiveConservativeHybrid)
        decision.Reason = HybridSourceDecisionReason::OverlayOperator;
    else if (decision.ActiveDirect3D)
        decision.Reason = HybridSourceDecisionReason::ConservativeHybridWithDirect3D;
    else
        decision.Reason = HybridSourceDecisionReason::ConservativeHybridWithoutDirect3D;

    return decision;
}

bool ShouldUseSuppressedDirect3DPresentationOverlay(
    u16 blendCnt,
    u8 eva,
    u8 evb,
    u32 visibleNativeLayerMask)
{
    const u32 blendEffect = (blendCnt >> 6) & 0x3u;
    if (blendEffect != 1 || eva != 0)
        return false;

    // EVA=0/EVB=16 is an exact target2 pass-through for the ordinary alpha
    // blend. The completed native-stack candidate already owns that result,
    // including any per-pixel Direct3D material-alpha behavior. Forcing the
    // presentation overlay here makes NSMB change reconstruction methods when
    // its title appears.
    if (evb >= 16)
        return false;

    const u32 direct3DMask = 1u << 0;
    const u32 backdropMask = 1u << 5;
    const u32 blendTarget1 = blendCnt & 0x3Fu;
    const u32 blendTarget2 = (blendCnt >> 8) & 0x3Fu;
    const bool direct3DTarget1 = (blendTarget1 & direct3DMask) != 0;
    const bool visibleNativeTarget1 =
        (blendTarget1 & visibleNativeLayerMask) != 0;

    if (!direct3DTarget1 ||
        visibleNativeTarget1 ||
        (blendTarget2 & direct3DMask))
    {
        return false;
    }

    // The backdrop is always available as a native target2 when BLDCNT selects
    // it, even though it is not represented in LayerEnable.
    const u32 visibleNativeTarget2 =
        visibleNativeLayerMask | backdropMask;
    return (blendTarget2 & visibleNativeTarget2) != 0;
}

bool IsCleanLegacyAlphaBlendStateValidated(u16 blendCnt, u8 eva, u8 evb)
{
    const u32 blendEffect = (blendCnt >> 6) & 0x3u;
    if (blendEffect != 1)
        return true;

    // Simple BG0/Direct3D-as-target1 alpha blends are allowed here: the
    // native-stack candidate has already resolved the native blend coherently,
    // which avoids high-resolution target1 reconstruction artifacts.
    const u32 direct3DMask = 1u << 0;
    const u32 twoDLayerMask = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    const u32 backdropMask = 1u << 5;
    const u32 target1AllowedMask = direct3DMask | twoDLayerMask;
    const u32 target2AllowedMask = direct3DMask | twoDLayerMask | backdropMask;
    const u32 blendTarget1 = blendCnt & 0x3Fu;
    const u32 blendTarget2 = (blendCnt >> 8) & 0x3Fu;
    const bool alphaBlendHasNoTarget1 = blendTarget1 == 0;
    const bool direct3DTarget1With2DTarget2 =
        (blendTarget1 & direct3DMask) != 0 &&
        (blendTarget1 & ~target1AllowedMask) == 0 &&
        (blendTarget2 & target2AllowedMask) != 0 &&
        (blendTarget2 & ~target2AllowedMask) == 0;
    const bool direct3DTarget1Zeroed =
        direct3DTarget1With2DTarget2 &&
        eva == 0 &&
        evb == 16;
    const bool direct3DTarget1Contributing =
        direct3DTarget1With2DTarget2 &&
        eva > 0 &&
        evb > 0;
    const bool direct3DTarget1ZeroCoefficient =
        direct3DTarget1With2DTarget2 &&
        eva == 0 &&
        evb == 0;
    const bool direct3DTarget2Contributing =
        (blendTarget1 & direct3DMask) == 0 &&
        (blendTarget1 & twoDLayerMask) != 0 &&
        (blendTarget2 & direct3DMask) != 0 &&
        eva > 0 &&
        evb > 0;

    return alphaBlendHasNoTarget1 ||
           direct3DTarget1Zeroed ||
           direct3DTarget1Contributing ||
           direct3DTarget1ZeroCoefficient ||
           direct3DTarget2Contributing;
}

HybridCleanLegacyBlockReason ChooseHybridCleanLegacyBlockReason(
    const HybridCleanLegacyEligibilityInputs& inputs)
{
    if (!inputs.ScalePathAvailable ||
        !inputs.ConservativeHybridMode ||
        !inputs.CandidateEnabled)
    {
        return HybridCleanLegacyBlockReason::PathModeOrSettingUnavailable;
    }

    if (!inputs.MainEngine)
        return HybridCleanLegacyBlockReason::SubEngine;

    const u32 dispmode = (inputs.DispCnt >> 16) & 0x3u;
    if (dispmode != 1)
        return HybridCleanLegacyBlockReason::DisplayModeNotComposited;

    const bool visibleDirect3DLayer =
        (inputs.DispCnt & (1 << 3)) && (inputs.LayerEnable & (1 << 0));
    if (!visibleDirect3DLayer || !inputs.HasRenderedPolygons)
        return HybridCleanLegacyBlockReason::NoVisibleDirect3D;

    if (inputs.FinalUpscaleRender3DNative)
        return HybridCleanLegacyBlockReason::FinalUpscaleNative3D;

    if (inputs.CaptureTransportActive)
        return HybridCleanLegacyBlockReason::CaptureTransportActive;

    // Normal WIN0/WIN1 state should not disqualify the whole frame: the hybrid
    // selector already has per-cell window metadata and can avoid candidate use
    // in excluded regions. OBJ window is less predictable, so keep blocking it.
    if (inputs.DispCnt & (1 << 15))
        return HybridCleanLegacyBlockReason::OBJWindowActive;

    // Brightness effects are allowed because the native-stack path already
    // handles them as a native compositor transform before resolving high-res
    // 3D; only alpha blending needs the validated-states rule.
    if (!IsCleanLegacyAlphaBlendStateValidated(inputs.BlendCnt, inputs.EVA, inputs.EVB))
        return HybridCleanLegacyBlockReason::UnsupportedAlphaBlendState;

    if (inputs.AnyEnabledCaptureBackedBGLayer)
        return HybridCleanLegacyBlockReason::CaptureBackedBGLayer;

    if (inputs.AnyCaptureBackedSprite)
        return HybridCleanLegacyBlockReason::CaptureBackedSprite;

    return HybridCleanLegacyBlockReason::None;
}

WholeSceneScaleDecision ChooseWholeSceneOverlayScaleDecision(
    const WholeSceneOverlayScaleDecisionInputs& inputs)
{
    WholeSceneScaleDecision decision = {};
    decision.CapturePlan = inputs.CapturePlan;

    if (decision.CapturePlan.Stage == WholeSceneCaptureBackedPlanStage::BeforeGeneralFallbacks)
    {
        decision.Reason = WholeSceneScaleDecisionReason::CaptureBackedBeforeGeneralFallbacks;
        decision.Path = WholeSceneRenderPathForCaptureBackedPlanKind(decision.CapturePlan.Kind);
        return decision;
    }

    if (inputs.ConservativeHybridMode && inputs.HybridFragmentationGuardActive)
    {
        decision.Reason = WholeSceneScaleDecisionReason::HybridFragmentationFinalUpscale;
        decision.Path = WholeSceneRenderPath::FinalNativeUpscale;
        decision.HybridFragmentationFallback = true;
        return decision;
    }

    decision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    decision.Path = inputs.ConservativeHybridMode
        ? WholeSceneRenderPath::ConservativeHybridUpscale
        : WholeSceneRenderPath::OverlayOperatorUpscale;
    return decision;
}

}
