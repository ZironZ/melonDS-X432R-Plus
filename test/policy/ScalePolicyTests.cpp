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

// Table tests for WholeSceneScalePolicy: the frame-level path decision
// precedence that the renderer-details CSV serializes as a_path/b_path.

#include "PolicyTestHarness.h"
#include "WholeSceneScalePolicy.h"

using namespace melonDS;

POLICY_TEST(PathDecisionGuardBeatsEverythingExceptProducer)
{
    // The hybrid presentation guard forces Current for consumers...
    WholeScenePathDecisionInputs inputs = {};
    inputs.HybridPresentationGuardActive = true;
    inputs.ChunkedUnsafeOverlayAvailable = true;
    inputs.CanUseScalePath = true;
    inputs.CapturePlan = MakeWholeSceneSourceACaptureReplacementPlan();

    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::HybridPresentationGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason, WholeSceneCurrentPathReason::HybridPresentationGuard);

    // ...but a producer plan that is allowed during the guard still runs, so
    // the capture epoch keeps updating while presentation is held (A9 must
    // keep producing; only consumption is guarded).
    inputs.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(0);
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::CaptureEpochOverlay);

    // A producer plan with the permission revoked is guarded like a consumer.
    inputs.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(0, false);
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::HybridPresentationGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
}

POLICY_TEST(PathDecisionHandoffRunsBeforeGeneralFallbacks)
{
    // CaptureBackedHandoff (path 7) outranks the chunked-unsafe overlay and
    // every later fallback.
    WholeScenePathDecisionInputs inputs = {};
    inputs.CapturePlan = MakeWholeSceneCaptureBackedHandoffPlan();
    inputs.ChunkedUnsafeOverlayAvailable = true;
    inputs.SplitLegacyFallbackAvailable = true;
    inputs.CurrentFallbackAvailable = true;
    inputs.CanUseScalePath = true;

    const WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::CaptureBackedHandoff);
}

POLICY_TEST(PathDecisionFallbackOrdering)
{
    // Chunked-unsafe overlay comes first among the general fallbacks and
    // respects the conservative-hybrid mode split.
    WholeScenePathDecisionInputs inputs = {};
    inputs.ChunkedUnsafeOverlayAvailable = true;
    inputs.PhysicalFinalPostprocessNativeInputAvailable = true;
    inputs.SplitLegacyFallbackAvailable = true;
    inputs.ConservativeHybridMode = true;

    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::ChunkedUnsafeOverlay);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::ConservativeHybridUpscale);
    CHECK(decision.ConservativeHybrid);

    inputs.ConservativeHybridMode = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::OverlayOperatorUpscale);

    // Then the physical-final postprocess native input.
    inputs.ChunkedUnsafeOverlayAvailable = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::PhysicalFinalPostprocessInput);

    // Then the split legacy fallback.
    inputs.PhysicalFinalPostprocessNativeInputAvailable = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::SplitLegacyFallback);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::LegacyNativeUpscale);
}

POLICY_TEST(PathDecisionAfterGeneralFallbacksAndTail)
{
    // Source-A capture replacement (path 8) is an after-general-fallbacks
    // plan: general fallbacks win when available, and the plan only fires
    // once none of them are.
    WholeScenePathDecisionInputs inputs = {};
    inputs.CapturePlan = MakeWholeSceneSourceACaptureReplacementPlan();
    inputs.SplitLegacyFallbackAvailable = true;

    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::SplitLegacyFallback);

    inputs.SplitLegacyFallbackAvailable = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::SourceACaptureReplacement);

    // Fragmentation current fallback.
    inputs.CapturePlan = {};
    inputs.CurrentFallbackAvailable = true;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::FragmentationOrUnsafeFrameCurrentFallback);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason,
             WholeSceneCurrentPathReason::FragmentationOrUnsafeFrame);

    // The whole-scene scale decision leaves Path to the scale chooser.
    inputs.CurrentFallbackAvailable = false;
    inputs.CanUseScalePath = true;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::WholeSceneScale);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::None);

    // Nothing available: Current/ScalePathUnavailable. This is a legitimate
    // long-lived state (Gormiti sits in it for 1200+ frames) — never key a
    // guard on this reason code.
    inputs.CanUseScalePath = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::ScalePathUnavailable);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason, WholeSceneCurrentPathReason::ScalePathUnavailable);
}

POLICY_TEST(OverlayScaleDecisionBranches)
{
    // A before-general-fallbacks capture plan overrides the overlay choice.
    WholeSceneOverlayScaleDecisionInputs inputs = {};
    inputs.CapturePlan = MakeWholeSceneCaptureBackedHandoffPlan();
    inputs.ConservativeHybridMode = true;

    WholeSceneScaleDecision decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeSceneScaleDecisionReason::CaptureBackedBeforeGeneralFallbacks);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::CaptureBackedHandoff);

    // Hybrid fragmentation guard demotes to the final-image upscale floor.
    inputs.CapturePlan = {};
    inputs.HybridFragmentationGuardActive = true;
    decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeSceneScaleDecisionReason::HybridFragmentationFinalUpscale);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::FinalNativeUpscale);
    CHECK(decision.HybridFragmentationFallback);

    // Otherwise the mode split decides the overlay operator path.
    inputs.HybridFragmentationGuardActive = false;
    decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Reason, WholeSceneScaleDecisionReason::OverlayOperator);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::ConservativeHybridUpscale);

    inputs.ConservativeHybridMode = false;
    decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::OverlayOperatorUpscale);
}

POLICY_TEST(HybridSourceDecisionQuadrants)
{
    // Conservative hybrid with active Direct3D: render the foreground
    // candidate (and the 2D base only when enabled).
    HybridSourceDecisionInputs inputs = {};
    inputs.ConservativeHybridRequested = true;
    inputs.ActiveDirect3D = true;

    HybridSourceDecision decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason, HybridSourceDecisionReason::ConservativeHybridWithDirect3D);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::ConservativeHybridUpscale);
    CHECK(decision.RenderForegroundCandidate);
    CHECK(!decision.RenderForeground2DBase);
    CHECK(!decision.RenderNativeFallback);

    inputs.Foreground2DBaseEnabled = true;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK(decision.RenderForeground2DBase);

    // Conservative hybrid without Direct3D: per-pixel native fallback only.
    inputs = {};
    inputs.ConservativeHybridRequested = true;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason,
             HybridSourceDecisionReason::ConservativeHybridWithoutDirect3D);
    CHECK(decision.RenderNativeFallback);
    CHECK(!decision.RenderForegroundCandidate);

    // Overlay-suppressed Direct3D turns hybrid off for the frame.
    inputs = {};
    inputs.ConservativeHybridRequested = true;
    inputs.OverlaySuppressedDirect3D = true;
    inputs.ActiveDirect3D = true;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason, HybridSourceDecisionReason::SuppressedDirect3DOverlay);
    CHECK(!decision.EffectiveConservativeHybrid);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::OverlayOperatorUpscale);

    // Suppression is meaningless without a conservative-hybrid request.
    inputs.ConservativeHybridRequested = false;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason, HybridSourceDecisionReason::OverlayOperator);
    CHECK(!decision.OverlaySuppressedDirect3D);
}

POLICY_TEST(SuppressedDirect3DOverlayDistinguishesNSMBFromMetroid)
{
    const u32 bg1 = 1u << 1;
    const u32 bg2 = 1u << 2;
    const u32 bg3 = 1u << 3;
    const u32 obj = 1u << 4;

    // NSMB title:
    // Debug Exports/NSMB/whole-scene-2d-both-20260729-103206
    // Direct3D is target1 at zero weight and BG1-3 are target2 at full weight.
    // This is target2 pass-through and must remain in Conservative Hybrid so
    // the already-validated clean native-stack candidate can own the frame.
    CHECK(!ShouldUseSuppressedDirect3DPresentationOverlay(
        0x0E41,
        0,
        16,
        bg1 | bg2 | bg3 | obj));

    // Metroid Prime Hunters title:
    // Debug Exports/Metroid Prime Hunters/whole-scene-2d-both-20260729-110113
    // Both standard operands have zero weight, and current Direct3D contains a
    // different menu scene. Preserve the presentation-overlay override.
    CHECK(ShouldUseSuppressedDirect3DPresentationOverlay(
        0x2C41,
        0,
        0,
        bg2 | bg3));

    // Partial target2 weight is not pass-through and still needs the overlay
    // path until that blend is modeled inside Conservative Hybrid.
    CHECK(ShouldUseSuppressedDirect3DPresentationOverlay(
        0x0E41,
        0,
        8,
        bg1 | bg2 | bg3));

    // EBA retry-style contributing Direct3D is not a zero-weight case and must
    // not be caught by this frame-level override.
    CHECK(!ShouldUseSuppressedDirect3DPresentationOverlay(
        0x2D51,
        16,
        16,
        bg2 | bg3 | obj));
}

POLICY_TEST(RenderPathHelpers)
{
    CHECK(WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::FinalNativeUpscale));
    CHECK(WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::OverlayOperatorUpscale));
    CHECK(WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::ConservativeHybridUpscale));
    CHECK(!WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::CaptureBackedHandoff));
    CHECK(!WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::Current));

    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff),
             WholeSceneRenderPath::CaptureBackedHandoff);
    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement),
             WholeSceneRenderPath::SourceACaptureReplacement);
    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay),
             WholeSceneRenderPath::CaptureEpochOverlay);
    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::None),
             WholeSceneRenderPath::None);
}

POLICY_TEST(JokerIncompleteFinalizerRebuildsMissingCurrentPrefix)
{
    // Recorded Dragon Quest Monsters: Joker frame: the final display consumes
    // current Engine A output, but native products stop at row 101. No earlier
    // current chunks are valid, so starting at LastLine leaves rows 0-100 stale.
    IncompleteFinalizerFallbackInputs inputs = {};
    inputs.LastLine = 101;
    CHECK_EQ(ChooseIncompleteFinalizerCurrentStart(inputs), 0);

    // The May 24 fragmentation protection still applies to unsafe frames:
    // those earlier ranges were already rendered by the current compositor.
    inputs.EarlierCurrentChunksValid = true;
    CHECK_EQ(ChooseIncompleteFinalizerCurrentStart(inputs), 101);

    inputs.LastLine = 0;
    CHECK_EQ(ChooseIncompleteFinalizerCurrentStart(inputs), 0);
}

POLICY_TEST(InfiniteSpaceCaptureBackedBrightnessReleaseHoldsWhiteMask)
{
    // Infinite Space: Engine B releases full-white brightness at line 141
    // while BG2 consumes a crossing capture generated by a full whole-scene
    // source. Keep the white presentation mask through the affected physical
    // handoff; the clean handoff after it adopts the released state normally.
    CaptureBackedBrightnessReleaseInputs inputs = {};
    inputs.WholeSceneScaleRequested = true;
    inputs.Line = 141;
    inputs.PreviousMasterBrightness = 0x4010;
    inputs.CurrentMasterBrightness = 0;
    inputs.CrossingFullWholeSceneCaptureMask = 1u << 2;
    CHECK(ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));

    inputs.WholeSceneScaleRequested = false;
    CHECK(!ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));

    inputs.WholeSceneScaleRequested = true;
    inputs.CrossingFullWholeSceneCaptureMask = 0;
    CHECK(!ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));

    inputs.CrossingFullWholeSceneCaptureMask = 1u << 2;
    inputs.PreviousMasterBrightness = 0x400F;
    CHECK(!ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));

    inputs.PreviousMasterBrightness = 0x8010;
    CHECK(!ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));

    inputs.PreviousMasterBrightness = 0x4010;
    inputs.CurrentMasterBrightness = 0x4001;
    CHECK(!ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));

    inputs.CurrentMasterBrightness = 0;
    inputs.Line = 0;
    CHECK(!ShouldHoldFullWhiteForCaptureBackedBrightnessRelease(inputs));
}
