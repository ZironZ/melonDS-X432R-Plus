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
