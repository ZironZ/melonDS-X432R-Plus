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

#include "WholeSceneScalePolicy.h"

namespace melonDS
{

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

    if (inputs.ChunkedUnsafeOverlayAvailable)
    {
        decision.Reason = WholeScenePathDecisionReason::ChunkedUnsafeOverlay;
        decision.ConservativeHybrid = inputs.ConservativeHybridMode;
        decision.Path = decision.ConservativeHybrid
            ? WholeSceneRenderPath::ConservativeHybridUpscale
            : WholeSceneRenderPath::OverlayOperatorUpscale;
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
