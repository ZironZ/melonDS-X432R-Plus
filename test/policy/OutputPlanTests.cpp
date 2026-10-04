// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <string>

#include "PolicyTestHarness.h"
#include "WholeSceneOutputPlan.h"

using namespace melonDS;

namespace
{

WholeSceneSemanticCompositorRecipeInputs QualifiedSemanticInputs()
{
    WholeSceneSemanticCompositorRecipeInputs inputs;
    inputs.RowEpochIdentity = 0x45504F4348u;
    return inputs;
}

WholeSceneCompositorRecipeInputs QualifiedOrderedInputs()
{
    WholeSceneCompositorRecipeInputs inputs;
    inputs.RowEpochIdentity = 0x45504F4348u;
    return inputs;
}

const WholeSceneOutputTransformExecutionTrace* FindTransformTrace(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace,
    WholeSceneOutputTransformKind kind)
{
    for (u32 i = 0; i < plan.TransformCount; i++)
    {
        if (plan.Transforms[i].Kind == kind)
            return &trace.Transforms[i];
    }
    return nullptr;
}

}

POLICY_TEST(OutputPlanResolvesNestedScaleDecisionAndAssignsStage)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 8;
    inputs.YEnd = 160;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::HighResCompositor;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::HighResCompositor;
    inputs.ConsideredRepresentationMask = WholeSceneOutputRepresentationBit(
        WholeSceneOutputRepresentationKind::Current);

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    const WholeSceneOutputExecutionTrace trace = {};

    CHECK(plan.Valid);
    CHECK(trace.TransformsShadowOnly);
    CHECK_EQ(plan.Version, WholeSceneOutputPlanVersion);
    CHECK_EQ(plan.DecisionSource, WholeSceneOutputPlanDecisionSource::ScaleDecision);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::HighResCompositor);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::HighResCompositor);
    CHECK((plan.ConsideredRepresentationMask &
           WholeSceneOutputRepresentationBit(
               WholeSceneOutputRepresentationKind::HighResCompositor)) != 0);
    CHECK_EQ(plan.InsertionStage,
             WholeSceneOutputInsertionStage::HighResComposition);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK(plan.ChannelsAssessed);
    CHECK_EQ(plan.SelectedRepresentationProduct.Kind,
             plan.SelectedRepresentation);
}

POLICY_TEST(OutputPlanKeepsNativeStackAsTerminalCompletedRepresentation)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.RepresentationSourceGeneration = 0x4E4154495645u;
    inputs.RepresentationRowEpochIdentity = 0x45504F4348u;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::LegacyNativeUpscale;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::LegacyNativeUpscale;

    const auto plan = BuildWholeSceneOutputPlan(inputs);
    const auto& product = plan.SelectedRepresentationProduct;

    CHECK(product.Valid);
    CHECK_EQ(product.Kind, WholeSceneOutputRepresentationKind::NativeStack);
    CHECK(product.TerminalCompleted);
    CHECK(!product.CompositorInputEligible);
    CHECK((product.CapabilityMask &
           OutputRepresentationCapabilityNativeSemanticOwnership) != 0);
    CHECK((product.CapabilityMask &
           OutputRepresentationCapabilityEndpointReconstruction) == 0);
    CHECK_EQ(product.CoverageAuthority,
             WholeSceneCompositorCoverageAuthority::CompletedScene);
}

POLICY_TEST(OutputPlanKeepsEndpointOverlayAsTerminalCompletedRepresentation)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.RepresentationSourceGeneration = 0x4F5645524C4159u;
    inputs.RepresentationRowEpochIdentity = 0x45504F4348u;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::OverlayOperatorUpscale;

    const auto plan = BuildWholeSceneOutputPlan(inputs);
    const auto& product = plan.SelectedRepresentationProduct;

    CHECK(product.Valid);
    CHECK_EQ(product.Kind,
             WholeSceneOutputRepresentationKind::OverlayOperator);
    CHECK(product.TerminalCompleted);
    CHECK(!product.CompositorInputEligible);
    CHECK((product.CapabilityMask &
           OutputRepresentationCapabilityEndpointReconstruction) != 0);
    CHECK((product.CapabilityMask &
           OutputRepresentationCapabilitySelectorResolved) != 0);

    auto nextInputs = inputs;
    nextInputs.RepresentationSourceGeneration++;
    const auto next = BuildWholeSceneOutputPlan(nextInputs);
    CHECK(product.ProductIdentity !=
          next.SelectedRepresentationProduct.ProductIdentity);
}

POLICY_TEST(OutputRecipePreparationAcceptsAndObservesStrictSemanticRecipe)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YStart = 8;
    planInputs.YEnd = 160;
    planInputs.OutputScale = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(planInputs);

    auto recipeInputs = QualifiedSemanticInputs();
    recipeInputs.YStart = 8;
    recipeInputs.YEnd = 160;
    recipeInputs.OutputScale = 4;
    recipeInputs.EnabledBGMask = 1u << 2;
    recipeInputs.BGProductsAvailableMask = 1u << 2;
    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(recipeInputs);

    const auto binding =
        PrepareWholeSceneCompositorRecipeBinding(plan, recipe);
    CHECK(binding.Evaluated);
    CHECK(binding.Bound);
    CHECK_EQ(binding.Reason,
             WholeSceneOutputPlanRecipeBindingReason::Bound);
    CHECK_EQ(binding.Recipe.RecipeHash, recipe.RecipeHash);

    const auto outcome =
        ResolveWholeSceneCompositorExecution(plan, binding, recipe);
    CHECK_EQ(outcome.Disposition,
             WholeSceneCompositorExecutionDisposition::PlannedRecipe);
    CHECK_EQ(outcome.Reason,
             WholeSceneCompositorExecutionReason::PlannedRecipeMatched);
    WholeSceneOutputExecutionTrace trace;
    ObserveWholeSceneOutputPlanCompositorRecipeExecution(
        binding, trace, outcome);
    CHECK(trace.CompositorRecipeExecutionObserved);
    CHECK(trace.CompositorRecipeParity);
    CHECK_EQ(trace.ExecutedCompositorRecipe.RecipeHash,
             recipe.RecipeHash);
    CHECK_EQ(trace.ExecutedCompositorRecipe.Inputs.size(),
             recipe.Inputs.size());
}

POLICY_TEST(OutputPlanClassifiesPreparedSemanticRecipeFallback)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YEnd = 192;
    planInputs.OutputScale = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(planInputs);

    auto plannedInputs = QualifiedSemanticInputs();
    plannedInputs.YEnd = 192;
    plannedInputs.OutputScale = 4;
    plannedInputs.EnabledBGMask = 1u << 2;
    plannedInputs.BGProductsAvailableMask = 1u << 2;
    const auto planned = BuildWholeSceneSemanticCompositorRecipe(plannedInputs);
    const auto binding =
        PrepareWholeSceneCompositorRecipeBinding(plan, planned);
    CHECK(binding.Bound);

    auto preparedInputs = plannedInputs;
    preparedInputs.BGSourceGeneration[2] = 7;
    const auto prepared =
        BuildWholeSceneSemanticCompositorRecipe(preparedInputs);
    const auto outcome =
        ResolveWholeSceneCompositorExecution(plan, binding, prepared);
    CHECK_EQ(outcome.Disposition,
             WholeSceneCompositorExecutionDisposition::PreparedRecipeFallback);
    CHECK_EQ(outcome.Reason,
             WholeSceneCompositorExecutionReason::PreparedRecipeMismatch);
    CHECK_EQ(outcome.EffectiveRecipe.RecipeHash, prepared.RecipeHash);
}

POLICY_TEST(OutputPlanClassifiesRejectedSemanticRecipeAsStrictCandidateBindingsFallback)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YEnd = 192;
    planInputs.OutputScale = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(planInputs);

    auto plannedInputs = QualifiedSemanticInputs();
    plannedInputs.YEnd = 192;
    plannedInputs.OutputScale = 4;
    plannedInputs.EnabledBGMask = 1u << 2;
    plannedInputs.BGProductsAvailableMask = 1u << 2;
    const auto planned = BuildWholeSceneSemanticCompositorRecipe(plannedInputs);
    const auto binding =
        PrepareWholeSceneCompositorRecipeBinding(plan, planned);
    CHECK(binding.Bound);

    auto rejectedInputs = plannedInputs;
    rejectedInputs.BGProductsAvailableMask = 0;
    const auto rejected =
        BuildWholeSceneSemanticCompositorRecipe(rejectedInputs);
    CHECK(!rejected.Ready);
    const auto outcome =
        ResolveWholeSceneCompositorExecution(plan, binding, rejected);
    CHECK_EQ(outcome.Disposition,
             WholeSceneCompositorExecutionDisposition::
                 StrictCandidateBindingsFallback);
    CHECK_EQ(outcome.Reason,
             WholeSceneCompositorExecutionReason::PreparedRecipeRejected);
    CHECK_EQ(outcome.EffectiveRecipe.RejectionMask,
             rejected.RejectionMask);
}

POLICY_TEST(OutputPlanRejectsSemanticRecipeWithWrongScale)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YStart = 0;
    planInputs.YEnd = 192;
    planInputs.OutputScale = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(planInputs);

    auto recipeInputs = QualifiedSemanticInputs();
    recipeInputs.OutputScale = 2;
    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(recipeInputs);

    const auto binding =
        PrepareWholeSceneCompositorRecipeBinding(plan, recipe);
    CHECK(!binding.Bound);
    CHECK_EQ(binding.Reason,
             WholeSceneOutputPlanRecipeBindingReason::ScaleMismatch);
}

POLICY_TEST(OutputPlanRejectsSemanticRecipeWithWrongEffectState)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YStart = 0;
    planInputs.YEnd = 192;
    planInputs.OutputScale = 4;
    planInputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::ColorEffect);
    planInputs.BlendCnt = 0x0484u;
    planInputs.EVY = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(planInputs);

    auto recipeInputs = QualifiedSemanticInputs();
    recipeInputs.OutputScale = 4;
    recipeInputs.EffectState = BuildWholeSceneCompositorEffectState(
        planInputs.RequiredChannels,
        planInputs.BlendCnt,
        0,
        0,
        5,
        0);
    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(recipeInputs);

    const auto binding =
        PrepareWholeSceneCompositorRecipeBinding(plan, recipe);
    CHECK(!binding.Bound);
    CHECK_EQ(binding.Reason,
             WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch);
    CHECK_EQ(binding.Recipe.RecipeHash, recipe.RecipeHash);
    CHECK_EQ(binding.Recipe.EffectState.StateHash,
             recipe.EffectState.StateHash);
}

POLICY_TEST(OutputRecipePreparationAcceptsDependentOrderedAndOverlayRecipes)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YStart = 0;
    planInputs.YEnd = 192;
    planInputs.OutputScale = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan outputPlan = BuildWholeSceneOutputPlan(planInputs);

    auto semanticInputs = QualifiedSemanticInputs();
    semanticInputs.OutputScale = 4;
    semanticInputs.EnabledBGMask = 1u << 0;
    semanticInputs.Direct3DBGMask = 1u << 0;
    semanticInputs.Direct3DProductAvailable = true;
    const auto semantic =
        BuildWholeSceneSemanticCompositorRecipe(semanticInputs);
    const auto semanticBinding =
        PrepareWholeSceneCompositorRecipeBinding(outputPlan, semantic);
    CHECK(semanticBinding.Bound);

    WholeSceneOBJOperandPlan operandPlan;
    operandPlan.Ready = true;
    operandPlan.PlanHash = 0x0B1u;
    WholeSceneOBJOperand ordinary;
    ordinary.Kind = WholeSceneOBJOperandKind::OrdinaryBand;
    ordinary.BandIndex = 0;
    ordinary.PresentationIndex = 0;
    ordinary.OAMMask[0] = 1ull << 1;
    WholeSceneOBJOperand affine;
    affine.Kind = WholeSceneOBJOperandKind::AffinePartition;
    affine.PresentationIndex = 1;
    affine.OAMMask[0] = 1ull << 2;
    operandPlan.BackToFront = { ordinary, affine };
    auto orderedInputs = QualifiedOrderedInputs();
    orderedInputs.Capabilities.CompletedBase = true;
    orderedInputs.Capabilities.OrdinaryOBJBand = true;
    orderedInputs.Capabilities.AffineOBJGroup = true;
    orderedInputs.Capabilities.SemiTransparentOBJ = true;
    orderedInputs.CompletedBaseIdentity = 0xBACEu;
    orderedInputs.CompletedBaseRecipeHash = semantic.RecipeHash;
    orderedInputs.OutputScale = 4;
    const auto ordered = BuildWholeSceneOBJCompositorRecipe(
        operandPlan, orderedInputs);
    const auto orderedBinding =
        PrepareWholeSceneOrderedPresentationRecipeBinding(
            outputPlan, semanticBinding, semantic, ordered);
    CHECK(orderedBinding.Bound);

    WholeSceneOperandExcludedOverlayInputs overlayInputs;
    overlayInputs.Requested = true;
    overlayInputs.Direct3D = true;
    overlayInputs.OutputScale = 4;
    const auto overlay = BuildWholeSceneOperandExcludedOverlayRecipe(
        ordered, overlayInputs);
    const auto overlayBinding =
        PrepareWholeSceneOperandExcludedOverlayRecipeBinding(
            outputPlan, semanticBinding, orderedBinding, overlay);
    CHECK(overlayBinding.Bound);

    const auto orderedOutcome = ResolveWholeSceneOrderedPresentationExecution(
        outputPlan, orderedBinding, semantic, ordered, true);
    CHECK_EQ(orderedOutcome.Disposition,
             WholeSceneCompositorExecutionDisposition::PlannedRecipe);
    WholeSceneOutputExecutionTrace trace;
    ObserveWholeSceneOutputPlanOrderedPresentationRecipeExecution(
        orderedBinding, trace, orderedOutcome);
    const auto overlayOutcome =
        ResolveWholeSceneOperandExcludedOverlayExecution(
            outputPlan, overlayBinding, overlay);
    CHECK_EQ(overlayOutcome.Disposition,
             WholeSceneCompositorExecutionDisposition::PlannedRecipe);
    ObserveWholeSceneOutputPlanOperandExcludedOverlayRecipeExecution(
        overlayBinding, trace, overlayOutcome, true);
    CHECK(trace.OrderedPresentationRecipeParity);
    CHECK(trace.OrderedPresentationBaseRecipeParity);
    CHECK(trace.OperandExcludedOverlayRecipeParity);

    auto preparedInputs = orderedInputs;
    preparedInputs.SourceGeneration = 9;
    const auto preparedOrdered = BuildWholeSceneOBJCompositorRecipe(
        operandPlan, preparedInputs);
    const auto fallbackOutcome =
        ResolveWholeSceneOrderedPresentationExecution(
            outputPlan,
            orderedBinding,
            semantic,
            preparedOrdered,
            true);
    CHECK_EQ(fallbackOutcome.Disposition,
             WholeSceneCompositorExecutionDisposition::PreparedRecipeFallback);
    CHECK_EQ(fallbackOutcome.Reason,
             WholeSceneCompositorExecutionReason::PreparedRecipeMismatch);

    const auto unavailableOutcome =
        ResolveWholeSceneOrderedPresentationExecution(
            outputPlan, orderedBinding, semantic, ordered, false);
    CHECK_EQ(unavailableOutcome.Disposition,
             WholeSceneCompositorExecutionDisposition::Unavailable);
    CHECK_EQ(unavailableOutcome.Reason,
             WholeSceneCompositorExecutionReason::PreparedProductsUnavailable);

    auto unavailableOverlayInputs = overlayInputs;
    unavailableOverlayInputs.OrderedProductsAvailable = false;
    const auto unavailableOverlay = BuildWholeSceneOperandExcludedOverlayRecipe(
        ordered, unavailableOverlayInputs);
    const auto unavailableOverlayOutcome =
        ResolveWholeSceneOperandExcludedOverlayExecution(
            outputPlan, overlayBinding, unavailableOverlay);
    CHECK_EQ(unavailableOverlayOutcome.Disposition,
             WholeSceneCompositorExecutionDisposition::Unavailable);
    CHECK_EQ(unavailableOverlayOutcome.Reason,
             WholeSceneCompositorExecutionReason::PreparedProductsUnavailable);

    auto renderFailureOutcome = overlayOutcome;
    RecordWholeSceneOperandExcludedOverlayRenderFailure(renderFailureOutcome);
    CHECK_EQ(renderFailureOutcome.Disposition,
             WholeSceneCompositorExecutionDisposition::Unavailable);
    CHECK_EQ(renderFailureOutcome.Reason,
             WholeSceneCompositorExecutionReason::RenderFailure);
}

POLICY_TEST(StrictAffinePresentationExecutionPreservesFallbackPrecedence)
{
    WholeSceneStrictAffineProductAssessment assessment;
    auto outcome = ResolveWholeSceneStrictAffinePresentationExecution(assessment);
    CHECK_EQ(outcome.Kind,
             WholeSceneStrictAffinePresentationKind::StrictCandidate);
    CHECK_EQ(outcome.Reason, WholeSceneStrictAffinePresentationReason::
                                 NoPresentationProductsReady);
    CHECK(!outcome.AffineOBJInsertion);

    assessment.NativeStackReady = true;
    assessment.AffineOBJInsertionReady = true;
    outcome = ResolveWholeSceneStrictAffinePresentationExecution(assessment);
    CHECK_EQ(outcome.Kind,
             WholeSceneStrictAffinePresentationKind::NativeStackComposite);
    CHECK_EQ(outcome.Reason,
             WholeSceneStrictAffinePresentationReason::NativeStackReady);
    CHECK(outcome.AffineOBJInsertion);

    assessment.AtomicOrdinaryBandReady = true;
    outcome = ResolveWholeSceneStrictAffinePresentationExecution(assessment);
    CHECK_EQ(outcome.Kind,
             WholeSceneStrictAffinePresentationKind::AtomicOrdinaryBand);
    CHECK_EQ(outcome.Reason, WholeSceneStrictAffinePresentationReason::
                                 AtomicBandProductsReady);
    CHECK(!outcome.AffineOBJInsertion);

    assessment.OrderedOperandsReady = true;
    outcome = ResolveWholeSceneStrictAffinePresentationExecution(assessment);
    CHECK_EQ(outcome.Kind,
             WholeSceneStrictAffinePresentationKind::OrderedOperands);
    CHECK_EQ(outcome.Reason, WholeSceneStrictAffinePresentationReason::
                                 OrderedProductsReady);
    CHECK(!outcome.AffineOBJInsertion);

    WholeSceneOutputExecutionTrace trace;
    ObserveWholeSceneStrictAffinePresentationExecution(
        trace, assessment, outcome);
    CHECK_EQ(trace.StrictAffinePresentationExecutionKind, outcome.Kind);
    CHECK_EQ(trace.StrictAffinePresentationExecutionReason, outcome.Reason);
    CHECK_EQ(trace.StrictAffinePresentationAffineOBJInsertion,
             outcome.AffineOBJInsertion);
    CHECK(trace.StrictAffineProductAssessment.OrderedOperandsReady);
    CHECK(trace.StrictAffineProductAssessment.NativeStackReady);
}

POLICY_TEST(OutputPlanRejectsOrderedRecipeWithWrongSemanticDependency)
{
    WholeSceneOutputPlanInputs planInputs = {};
    planInputs.YEnd = 192;
    planInputs.OutputScale = 4;
    planInputs.PathDecision.Reason =
        WholeScenePathDecisionReason::WholeSceneScale;
    planInputs.HasScaleDecision = true;
    planInputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::StrictAffineHighRes;
    planInputs.ScaleDecision.Path = WholeSceneRenderPath::StrictAffineHighRes;
    WholeSceneOutputPlan outputPlan = BuildWholeSceneOutputPlan(planInputs);

    auto semanticInputs = QualifiedSemanticInputs();
    semanticInputs.OutputScale = 4;
    const auto semantic =
        BuildWholeSceneSemanticCompositorRecipe(semanticInputs);
    const auto semanticBinding =
        PrepareWholeSceneCompositorRecipeBinding(outputPlan, semantic);
    CHECK(semanticBinding.Bound);

    WholeSceneOBJOperandPlan operandPlan;
    operandPlan.Ready = true;
    operandPlan.PlanHash = 1;
    auto orderedInputs = QualifiedOrderedInputs();
    orderedInputs.Capabilities.CompletedBase = true;
    orderedInputs.CompletedBaseIdentity = 1;
    orderedInputs.CompletedBaseRecipeHash = semantic.RecipeHash + 1;
    orderedInputs.OutputScale = 4;
    const auto ordered = BuildWholeSceneOBJCompositorRecipe(
        operandPlan, orderedInputs);

    const auto orderedBinding =
        PrepareWholeSceneOrderedPresentationRecipeBinding(
            outputPlan, semanticBinding, semantic, ordered);
    CHECK(!orderedBinding.Bound);
    CHECK_EQ(orderedBinding.Reason,
             WholeSceneOutputPlanRecipeBindingReason::DependencyMismatch);
}

POLICY_TEST(OutputPlanPreservesSelectedCapturePlan)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard;
    inputs.PathDecision.Path = WholeSceneRenderPath::CaptureEpochOverlay;
    inputs.PathDecision.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(2);
    auto& evidence = inputs.PathDecision.CapturePlan.Evidence;
    evidence.Available = true;
    evidence.ContentProven = true;
    evidence.PresentationProven = false;
    evidence.ProductRef.Kind = WholeSceneCaptureProductKind::ParentOutput3D;
    evidence.ProductRef.BackgroundSource =
        SourceABackgroundSource::ParentOutputTex3D;
    evidence.ProductRef.RouteSlot = 2;
    evidence.ProductRef.CaptureBank = 3;
    evidence.ProductRef.CaptureEventSerial = 0;
    evidence.ProductRef.BackgroundEpochSerial = 0;
    evidence.ProductRef.CapturePresentationHash = 0x1234;
    evidence.ProductRef.CurrentPresentationHash = 0x5678;
    evidence.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
    evidence.Source3DSerial = 53;
    evidence.Source3DSceneHash = 0x9ABC;
    evidence.SourceKind = 1;
    evidence.ProductMask = 5;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.DecisionSource, WholeSceneOutputPlanDecisionSource::PathDecision);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::CaptureEpochOverlay);
    CHECK_EQ(plan.CapturePlan.Kind,
             WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay);
    CHECK_EQ(plan.CapturePlan.Role,
             WholeSceneCaptureBackedPlanRole::RouteProducer);
    CHECK_EQ(plan.CapturePlan.CaptureEpochOverlayRouteSlot, 2);
    CHECK(plan.RepresentationEvidence.Available);
    CHECK(plan.RepresentationEvidence.ContentProven);
    CHECK(!plan.RepresentationEvidence.PresentationProven);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.Kind,
             WholeSceneCaptureProductKind::ParentOutput3D);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.BackgroundSource,
             SourceABackgroundSource::ParentOutputTex3D);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.RouteSlot, 2);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.CaptureBank, 3);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.CaptureEventSerial, 0);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.BackgroundEpochSerial, 0);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.CapturePresentationHash,
             0x1234);
    CHECK_EQ(plan.RepresentationEvidence.ProductRef.CurrentPresentationHash,
             0x5678);
    CHECK_EQ(plan.RepresentationEvidence.ProofKind,
             WholeSceneCaptureProofKind::RouteStateIdentity);
    CHECK_EQ(plan.RepresentationEvidence.Source3DSerial, 53);
    CHECK_EQ(plan.RepresentationEvidence.Source3DSceneHash, 0x9ABC);
    CHECK_EQ(plan.RepresentationEvidence.SourceKind, 1);
    CHECK_EQ(plan.RepresentationEvidence.ProductMask, 5);

    WholeSceneOutputExecutionTrace trace = {};
    ObserveWholeSceneCaptureRepresentationEvidence(plan, trace, evidence);
    CHECK(trace.RepresentationEvidenceTraceObserved);
    CHECK(trace.RepresentationEvidenceContentTraceAgreement);
    CHECK(!trace.RepresentationEvidencePresentationTraceComparable);
    CHECK(!trace.RepresentationEvidencePresentationTraceAgreement);

    WholeSceneCaptureRepresentationEvidence wrongScene = evidence;
    wrongScene.Source3DSceneHash++;
    ObserveWholeSceneCaptureRepresentationEvidence(plan, trace, wrongScene);
    CHECK(!trace.RepresentationEvidenceContentTraceAgreement);
}

POLICY_TEST(OutputPlanDoesNotPresentRejectedCaptureCandidateAsSelected)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.PathDecision.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(1);
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::OverlayOperatorUpscale;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::OverlayOperator);
    CHECK_EQ(plan.CapturePlan.Kind, WholeSceneCaptureBackedPlanKind::None);
}

POLICY_TEST(OutputPlanAdmitsCaptureEpochOverlayInputOnlyFromTwoPreExecutionProofs)
{
    const u64 displayCapture =
        static_cast<u64>(WholeSceneOutputChannel::DisplayCapture);
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard;
    inputs.PathDecision.Path = WholeSceneRenderPath::CaptureEpochOverlay;
    inputs.PathDecision.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(1);
    inputs.RequiredChannels = displayCapture;
    inputs.CaptureCnt = 0x80310010u;

    auto& evidence = inputs.PathDecision.CapturePlan.Evidence;
    evidence.Available = true;
    evidence.ContentProven = true;
    evidence.PresentationProven = true;
    evidence.ProductRef.Kind = WholeSceneCaptureProductKind::ParentOutput3D;
    evidence.ProductRef.BackgroundSource =
        SourceABackgroundSource::ParentOutputTex3D;
    evidence.ProductRef.RouteSlot = 1;
    evidence.ProductRef.CaptureBank = 1;
    evidence.ProductRef.BackgroundEpochSerial = 0;
    evidence.ProductRef.CapturePresentationHash = 0x1234;
    evidence.ProductRef.CurrentPresentationHash = 0x1234;
    evidence.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
    evidence.AuthorizationProofKind =
        WholeSceneCaptureProofKind::ActiveBackgroundEpoch;
    evidence.AuthorizationEpochSerial = 37;
    evidence.AuthorizationCaptureBank = 3;
    evidence.AuthorizationPresentationHash = 0x5678;
    evidence.Source3DSerial = 53;
    evidence.Source3DSceneHash = 0x9ABC;
    evidence.OutputEffectOwner =
        WholeSceneCaptureEffectOwner::CurrentEngineColorEffect;
    evidence.OutputEffectState = 0x4321;
    evidence.OutputPreMaster = true;
    evidence.CaptureInputProven = true;
    evidence.CaptureCnt = inputs.CaptureCnt;
    evidence.CaptureConsumesSelectedPresentation = true;
    evidence.CaptureUsesSourceB = false;

    const WholeSceneOutputPlan admitted = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(admitted.ReproducedChannels & displayCapture, displayCapture);
    CHECK_EQ(admitted.EvidenceUnavailableChannels & displayCapture, 0);
    CHECK_EQ(admitted.AdmissionReason,
             WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted);

    evidence.PresentationProven = false;
    const WholeSceneOutputPlan missingPhase = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(missingPhase.ReproducedChannels & displayCapture, 0);
    CHECK_EQ(missingPhase.EvidenceUnavailableChannels & displayCapture,
             displayCapture);
    CHECK_EQ(missingPhase.AdmissionReason,
             WholeSceneOutputAdmissionReason::
                 SelectedRepresentationRetainedForParityWithEvidenceDebt);

    evidence.PresentationProven = true;
    evidence.CaptureInputProven = false;
    evidence.CaptureUsesSourceB = true;
    const WholeSceneOutputPlan sourceBUnproven =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(sourceBUnproven.ReproducedChannels & displayCapture, 0);
    CHECK_EQ(sourceBUnproven.EvidenceUnavailableChannels & displayCapture,
             displayCapture);
}

POLICY_TEST(OutputPlanSerializesCurrentSelectorConfiguration)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.SelectorConfigured = true;
    inputs.HybridDecision.Reason =
        HybridSourceDecisionReason::ConservativeHybridWithDirect3D;
    inputs.HybridDecision.ConservativeHybridRequested = true;
    inputs.HybridDecision.EffectiveConservativeHybrid = true;
    inputs.HybridDecision.ActiveDirect3D = true;
    inputs.HybridDecision.RenderForegroundCandidate = true;
    inputs.WindowEdgeAssist = true;
    inputs.Target2AlphaBlendAssist = true;
    inputs.NativeEffectGuard = true;
    inputs.CleanLegacyCandidateEnabled = true;
    inputs.CleanLegacyBlockReason = HybridCleanLegacyBlockReason::None;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.Selector.Configured);
    CHECK_EQ(plan.Selector.Kind,
             WholeSceneOutputSelectorKind::ConservativeHybrid);
    CHECK(plan.Selector.RenderForegroundCandidate);
    CHECK(plan.Selector.WindowEdgeAssist);
    CHECK(plan.Selector.Target2AlphaBlendAssist);
    CHECK(plan.Selector.NativeEffectGuard);
    CHECK_EQ(plan.Selector.CleanLegacyBlockReason,
             HybridCleanLegacyBlockReason::None);
}

POLICY_TEST(OutputPlanExecutionObservationReportsParity)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::ScalePathUnavailable;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    inputs.PathDecision.CurrentReason = WholeSceneCurrentPathReason::ScalePathUnavailable;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace;
    ObserveWholeSceneOutputPlanExecution(
        plan,
        trace,
        WholeSceneRenderPath::Current,
        WholeSceneCurrentPathReason::ScalePathUnavailable,
        WholeSceneOutputExecutionKind::Current);
    CHECK(trace.ExecutionObserved);
    CHECK(trace.PathParity);
    CHECK(trace.CurrentReasonParity);
    CHECK(trace.DecisionParity);

    ObserveWholeSceneOutputPlanExecution(
        plan,
        trace,
        WholeSceneRenderPath::LegacyNativeUpscale,
        WholeSceneCurrentPathReason::None,
        WholeSceneOutputExecutionKind::SelectedRepresentation);
    CHECK(!trace.PathParity);
    CHECK(!trace.CurrentReasonParity);
    CHECK(!trace.DecisionParity);
}

POLICY_TEST(OutputPlanDescribesEveryExistingDispatcherShape)
{
    auto build = [](WholeScenePathDecisionReason reason,
                    WholeSceneRenderPath path,
                    WholeSceneCurrentPathReason currentReason =
                        WholeSceneCurrentPathReason::None)
    {
        WholeSceneOutputPlanInputs inputs = {};
        inputs.YStart = 0;
        inputs.YEnd = 192;
        inputs.PathDecision.Reason = reason;
        inputs.PathDecision.Path = path;
        inputs.PathDecision.CurrentReason = currentReason;
        if (reason == WholeScenePathDecisionReason::WholeSceneScale)
        {
            inputs.HasScaleDecision = true;
            inputs.ScaleDecision.Reason =
                WholeSceneScaleDecisionReason::HighResCompositor;
            inputs.ScaleDecision.Path = WholeSceneRenderPath::HighResCompositor;
        }
        if (reason ==
                WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard ||
            reason ==
                WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks ||
            reason ==
                WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks)
        {
            inputs.PathDecision.CapturePlan =
                MakeWholeSceneSourceACaptureReplacementPlan();
        }
        return BuildWholeSceneOutputPlan(inputs);
    };

    CHECK_EQ(build(WholeScenePathDecisionReason::None,
                   WholeSceneRenderPath::None).ExecutionKind,
             WholeSceneOutputExecutionKind::None);
    CHECK_EQ(build(WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard,
                   WholeSceneRenderPath::SourceACaptureReplacement).ExecutionKind,
             WholeSceneOutputExecutionKind::CaptureBackedPlan);
    CHECK_EQ(build(WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks,
                   WholeSceneRenderPath::SourceACaptureReplacement).ExecutionKind,
             WholeSceneOutputExecutionKind::CaptureBackedPlan);
    CHECK_EQ(build(WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks,
                   WholeSceneRenderPath::SourceACaptureReplacement).ExecutionKind,
             WholeSceneOutputExecutionKind::CaptureBackedPlan);
    CHECK_EQ(build(WholeScenePathDecisionReason::HybridPresentationGuard,
                   WholeSceneRenderPath::Current,
                   WholeSceneCurrentPathReason::HybridPresentationGuard).ExecutionKind,
             WholeSceneOutputExecutionKind::Current);
    CHECK_EQ(build(WholeScenePathDecisionReason::FragmentationOrUnsafeFrameCurrentFallback,
                   WholeSceneRenderPath::Current,
                   WholeSceneCurrentPathReason::FragmentationOrUnsafeFrame).ExecutionKind,
             WholeSceneOutputExecutionKind::Current);
    CHECK_EQ(build(WholeScenePathDecisionReason::ScalePathUnavailable,
                   WholeSceneRenderPath::Current,
                   WholeSceneCurrentPathReason::ScalePathUnavailable).ExecutionKind,
             WholeSceneOutputExecutionKind::Current);
    CHECK_EQ(build(WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput,
                   WholeSceneRenderPath::PhysicalFinalPostprocessInput).ExecutionKind,
             WholeSceneOutputExecutionKind::PhysicalFinalPostprocessNativeInput);
    CHECK_EQ(build(WholeScenePathDecisionReason::SplitLegacyFallback,
                   WholeSceneRenderPath::LegacyNativeUpscale).ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
    CHECK_EQ(build(WholeScenePathDecisionReason::WholeSceneScale,
                   WholeSceneRenderPath::None).ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
}

POLICY_TEST(OutputPlanSelectsOuterRouteFromFacts)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 12;
    inputs.YEnd = 88;
    inputs.SelectPathFromFacts = true;
    inputs.PathDecisionInputs.CanUseScalePath = true;
    inputs.PathDecisionInputs.HybridPresentationGuardActive = true;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason =
        WholeSceneScaleDecisionReason::HighResCompositor;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::HighResCompositor;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.PathReason,
             WholeScenePathDecisionReason::HybridPresentationGuard);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::Current);
    CHECK_EQ(plan.ExecutionKind, WholeSceneOutputExecutionKind::Current);

    inputs.PathDecisionInputs.HybridPresentationGuardActive = false;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.PathReason, WholeScenePathDecisionReason::WholeSceneScale);
    CHECK_EQ(plan.DecisionSource,
             WholeSceneOutputPlanDecisionSource::ScaleDecision);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::HighResCompositor);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);

    inputs.PathDecisionInputs.CanUseScalePath = false;
    inputs.PathDecisionInputs.CapturePlan =
        MakeWholeSceneSourceACaptureReplacementPlan();
    inputs.HasScaleDecision = false;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.PathReason,
             WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks);
    CHECK_EQ(plan.SelectedPath,
             WholeSceneRenderPath::SourceACaptureReplacement);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::CaptureBackedPlan);
}

POLICY_TEST(OutputPlanSelectsNestedScaleRepresentationFromFacts)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.SelectPathFromFacts = true;
    inputs.PathDecisionInputs.CanUseScalePath = true;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    auto& candidates = inputs.ScaleCandidateInputs;
    candidates.HighResCompositorAvailable = true;
    candidates.SourceACaptureReplacementAvailable = true;
    candidates.OverlayOperatorAvailable = true;
    candidates.FinalNativeAvailable = true;
    candidates.ConservativeHybridMode = true;
    candidates.CapturePlan = MakeWholeSceneSourceACaptureReplacementPlan();

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::HighResCompositor);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::HighResCompositor);

    candidates.HighResCompositorAvailable = false;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::SourceACaptureOnlyReplacement);
    CHECK_EQ(plan.SelectedPath,
             WholeSceneRenderPath::SourceACaptureReplacement);
    CHECK_EQ(plan.CapturePlan.Kind,
             WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement);

    candidates.SourceACaptureReplacementAvailable = false;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::OverlayOperator);
    CHECK_EQ(plan.SelectedPath,
             WholeSceneRenderPath::ConservativeHybridUpscale);

    candidates.OverlayOperatorAvailable = false;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::FinalNativeUpscale);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::FinalNativeUpscale);

    candidates.FinalNativeAvailable = false;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::LegacyNativeUpscale);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::LegacyNativeUpscale);
}

POLICY_TEST(OutputPlanPrefersStrictAffineCandidateOverHybridFinalizer)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.OverlayOperatorAvailable = true;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::StrictAffineHighRes) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::ConservativeHybrid) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::FinalNative);
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::StrictAffineHighRes);
    CHECK_EQ(plan.StrictAffineBlockReason,
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::StrictAffineHighRes);
    CHECK_EQ(plan.InsertionStage,
             WholeSceneOutputInsertionStage::HighResComposition);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);

    // Scale policy has already proven this is mode-1 ordinary OBJ with
    // binary native presence and a modeled target-2 underlay. OutputPlan must
    // retain that semantic ownership instead of demoting the admitted path.
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    const WholeSceneOutputPlan semiTransparentOrdinaryOBJPlan =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(inputs.RequiredChannels, 0x5122Aull);
    CHECK_EQ(semiTransparentOrdinaryOBJPlan.SelectedPath,
             WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(semiTransparentOrdinaryOBJPlan.ReproducedChannels &
                 inputs.RequiredChannels,
             inputs.RequiredChannels);
    CHECK_EQ(semiTransparentOrdinaryOBJPlan.UnsupportedChannels, 0);
    CHECK_EQ(semiTransparentOrdinaryOBJPlan.EvidenceUnavailableChannels, 0);
}

POLICY_TEST(OutputPlanPreservesStrictAffineRejectionWhenUsingFallback)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = false;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::UnsupportedActiveChannel;
    inputs.ScaleCandidateInputs.OverlayOperatorAvailable = true;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.StrictAffineBlockReason,
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);
    CHECK_EQ(plan.SelectedPath,
             WholeSceneRenderPath::ConservativeHybridUpscale);
}

POLICY_TEST(OutputPlanReconstructsIdentityEquivalentAffineWholeScene)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.SelectPathFromFacts = true;
    inputs.PathDecisionInputs.CanUseScalePath = true;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.
        IdentityEquivalentAffineWholeSceneAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::HighResolutionGeometryNotRequired;
    inputs.ScaleCandidateInputs.OverlayOperatorAvailable = true;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ScaleReason,
             WholeSceneScaleDecisionReason::LegacyNativeUpscale);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::LegacyNativeUpscale);
    CHECK_EQ(plan.StrictAffineBlockReason,
             StrictAffineHighResBlockReason::
                 HighResolutionGeometryNotRequired);
}

POLICY_TEST(OutputPlanStrictAffineOwnsSelectiveBrightnessTransform)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.RequiredChannels = 0x5063Bull;
    inputs.BlendCnt = 0x3FFEu;
    inputs.EVY = 7;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::StrictAffineHighRes);
    CHECK_EQ(plan.ReproducedChannels & inputs.RequiredChannels,
             inputs.RequiredChannels);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
    CHECK((plan.PlannedTransformMask &
           WholeSceneOutputTransformBit(
               WholeSceneOutputTransformKind::CompositorColorEffect)) != 0);
}

POLICY_TEST(OutputPlanStrictAffineModelsTiledCompanionAndPriority)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
}

POLICY_TEST(OutputPlanStrictAffineModelsDualExtendedAffineAndOBJ)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(inputs.RequiredChannels, 0x5023Aull);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::StrictAffineHighRes);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK_EQ(plan.ReproducedChannels & inputs.RequiredChannels,
             inputs.RequiredChannels);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
}

POLICY_TEST(OutputPlanStrictAffineModelsSingleExtendedAffineAndOrdinaryOBJ)
{
    // Super Mario 64 DS bottom-screen map: extended tiled-affine BG3 with
    // ordinary opaque OBJ HUD and markers.
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(inputs.RequiredChannels, 0x5022Aull);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::StrictAffineHighRes);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK_EQ(plan.ReproducedChannels & inputs.RequiredChannels,
             inputs.RequiredChannels);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
}

POLICY_TEST(OutputPlanStrictAffineModelsMario64AlphaBlendOverlay)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(inputs.RequiredChannels, 330299ull);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::StrictAffineHighRes);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK_EQ(plan.ReproducedChannels & inputs.RequiredChannels,
             inputs.RequiredChannels);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
}

POLICY_TEST(OutputPlanStrictAffineModelsDirect3DAffineOBJOverlay)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.SelectScaleFromFacts = true;
    inputs.ScaleCandidateInputs.StrictAffineHighResAvailable = true;
    inputs.ScaleCandidateInputs.StrictAffineBlockReason =
        StrictAffineHighResBlockReason::None;
    inputs.ScaleCandidateInputs.ConservativeHybridMode = true;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Direct3D) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::MasterBrightness) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.MasterBrightness = static_cast<u16>((2u << 14) | 7u);
    inputs.MasterBrightnessYStart = 0;
    inputs.MasterBrightnessYEnd = 192;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(inputs.RequiredChannels, 0x7023Dull);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::StrictAffineHighRes);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::StrictAffineHighRes);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::ModelCorrectHighRes);
    CHECK_EQ(plan.ReproducedChannels & inputs.RequiredChannels,
             inputs.RequiredChannels);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
    CHECK((plan.PlannedTransformMask &
           WholeSceneOutputTransformBit(
               WholeSceneOutputTransformKind::MasterBrightness)) != 0);
}

POLICY_TEST(OutputPlanExecutionRecipeCarriesDispatcherParameters)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 74;
    inputs.YEnd = 192;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::HybridPresentationGuard;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    inputs.PathDecision.CurrentReason =
        WholeSceneCurrentPathReason::HybridPresentationGuard;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ExecutionKind, WholeSceneOutputExecutionKind::Current);
    CHECK_EQ(plan.ExecutionYStart, 0);
    CHECK_EQ(plan.ExecutionYEnd, 192);
    CHECK(!plan.ExecutionCurrentFragmentationFallback);

    inputs.OutputScale = 4;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::Current) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::FinalNative);
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK(plan.AdmissionChangedSelection);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
    CHECK_EQ(plan.ExecutionYStart, 74);
    CHECK_EQ(plan.ExecutionYEnd, 192);

    inputs.OutputScale = 1;
    inputs.RequiredChannels = 0;
    inputs.ConsideredRepresentationMask = 0;

    inputs.PathDecision.Reason = WholeScenePathDecisionReason::
        FragmentationOrUnsafeFrameCurrentFallback;
    inputs.PathDecision.CurrentReason =
        WholeSceneCurrentPathReason::FragmentationOrUnsafeFrame;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ExecutionYStart, 74);
    CHECK_EQ(plan.ExecutionYEnd, 192);
    CHECK(plan.ExecutionCurrentFragmentationFallback);

    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.PathDecision.CurrentReason = WholeSceneCurrentPathReason::None;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
}

POLICY_TEST(OutputPlanDeferredFinalizerFollowsAdmittedExecution)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks;
    inputs.PathDecision.Path = WholeSceneRenderPath::CaptureBackedHandoff;
    inputs.PathDecision.CapturePlan =
        MakeWholeSceneCaptureBackedHandoffPlan();

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::CaptureBackedPlan);
    CHECK(WholeSceneOutputExecutionSuppressesDeferredFinalizer(plan));

    // Mario Kart's OBJ-window channel rejects the handoff representation.
    // Admission demotes the same route to FinalNative; VBlank must therefore
    // run the deferred upscale that publishes NativeExactFinalTex to OutputTex.
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::OBJWindow);
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::CaptureBackedHandoff) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::FinalNative);
    plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.AdmissionChangedSelection);
    CHECK_EQ(plan.InitialSelectedPath,
             WholeSceneRenderPath::CaptureBackedHandoff);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::FinalNativeUpscale);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
    CHECK(!WholeSceneOutputExecutionSuppressesDeferredFinalizer(plan));

    inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    plan = BuildWholeSceneOutputPlan(inputs);
    CHECK(!WholeSceneOutputExecutionSuppressesDeferredFinalizer(plan));
}

POLICY_TEST(OutputPlanPartitionsUnsupportedHighResChannels)
{
    const u64 affineOBJ = static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    const u64 objWindow = static_cast<u64>(WholeSceneOutputChannel::OBJWindow);
    const u64 alphaBlend = static_cast<u64>(WholeSceneOutputChannel::AlphaBlend);
    const u64 displaySelection = static_cast<u64>(WholeSceneOutputChannel::DisplaySelection);
    const u64 masterBrightness = static_cast<u64>(WholeSceneOutputChannel::MasterBrightness);
    const u64 physicalRouting = static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 12;
    inputs.YEnd = 88;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::HighResCompositor;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::HighResCompositor;
    inputs.RequiredChannels =
        affineOBJ | objWindow | alphaBlend |
        displaySelection | masterBrightness | physicalRouting;
    inputs.MasterBrightness = static_cast<u16>((2u << 14) | 7u);
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Bottom;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.UnsupportedChannels & (affineOBJ | objWindow),
             affineOBJ | objWindow);
    CHECK_EQ(plan.EvidenceUnavailableChannels & alphaBlend, alphaBlend);
    CHECK_EQ(plan.ReproducedChannels &
                 (displaySelection | masterBrightness | physicalRouting),
             displaySelection | masterBrightness | physicalRouting);
    CHECK_EQ(plan.RequiredChannels,
             plan.ReproducedChannels |
             plan.UnsupportedChannels |
             plan.EvidenceUnavailableChannels);
    CHECK(plan.AdmissionEvaluated);
    CHECK(!plan.AdmissionChangedSelection);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::UnsupportedChannelsNoFallback);
    CHECK_EQ(plan.AdmissionRejectedUnsupportedChannels,
             affineOBJ | objWindow);
    CHECK_EQ(plan.TransformCount, 3);
    CHECK((plan.PlannedTransformMask &
           WholeSceneOutputTransformBit(
               WholeSceneOutputTransformKind::MasterBrightness)) != 0);
}

POLICY_TEST(OutputPlanDemotesUnsupportedScaleCandidateToConsideredNativeStack)
{
    const u64 objWindow =
        static_cast<u64>(WholeSceneOutputChannel::OBJWindow);
    const u64 displaySelection =
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection);
    const u64 physicalRouting =
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::ConservativeHybrid) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::NativeStack);
    inputs.RequiredChannels = objWindow | displaySelection | physicalRouting;
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Top;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.AdmissionEvaluated);
    CHECK(plan.AdmissionChangedSelection);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::
                 UnsupportedChannelsDemotedToNativeStack);
    CHECK_EQ(plan.InitialSelectedPath,
             WholeSceneRenderPath::ConservativeHybridUpscale);
    CHECK_EQ(plan.InitialSelectedRepresentation,
             WholeSceneOutputRepresentationKind::ConservativeHybrid);
    CHECK_EQ(plan.AdmissionRejectedUnsupportedChannels, objWindow);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::LegacyNativeUpscale);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::NativeStack);
    CHECK_EQ(plan.InsertionStage,
             WholeSceneOutputInsertionStage::CompositorProductUpscale);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::NativeEnhanced);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK(!plan.Selector.Configured);
    CHECK_EQ(plan.ExecutionKind,
             WholeSceneOutputExecutionKind::SelectedRepresentation);
}

POLICY_TEST(OutputPlanPrefersConsideredFinalNativeForUnsupportedScaleCandidate)
{
    const u64 objWindow =
        static_cast<u64>(WholeSceneOutputChannel::OBJWindow);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::ConservativeHybrid) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::FinalNative) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::NativeStack);
    inputs.RequiredChannels = objWindow;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.AdmissionChangedSelection);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::
                 UnsupportedChannelsDemotedToFinalNative);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::FinalNativeUpscale);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::FinalNative);
    CHECK_EQ(plan.InsertionStage,
             WholeSceneOutputInsertionStage::CompositorProductUpscale);
    CHECK_EQ(plan.UnsupportedChannels, 0);
}

POLICY_TEST(OutputPlanDemotesUnsupportedCurrentPathCandidateToFinalNative)
{
    const u64 affineOBJ =
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::HybridPresentationGuard;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    inputs.PathDecision.CurrentReason =
        WholeSceneCurrentPathReason::HybridPresentationGuard;
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::Current) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::FinalNative);
    inputs.RequiredChannels = affineOBJ;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.AdmissionChangedSelection);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::
                 UnsupportedChannelsDemotedToFinalNative);
    CHECK_EQ(plan.InitialSelectedPath, WholeSceneRenderPath::Current);
    CHECK_EQ(plan.InitialCurrentReason,
             WholeSceneCurrentPathReason::HybridPresentationGuard);
    CHECK_EQ(plan.CurrentReason, WholeSceneCurrentPathReason::None);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::FinalNativeUpscale);
    CHECK_EQ(plan.UnsupportedChannels, 0);
}

POLICY_TEST(OutputPlanDemotesUnsupportedCurrentToNativeExactFloor)
{
    const u64 affineOBJ =
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 32;
    inputs.YEnd = 96;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::ScalePathUnavailable;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    inputs.PathDecision.CurrentReason =
        WholeSceneCurrentPathReason::ScalePathUnavailable;
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::Current) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::NativeExactFloor);
    inputs.RequiredChannels = affineOBJ;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.AdmissionChangedSelection);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::
                 UnsupportedChannelsDemotedToNativeExactFloor);
    CHECK_EQ(plan.InitialSelectedPath, WholeSceneRenderPath::Current);
    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::NativeExactFloor);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::NativeExactFloor);
    CHECK_EQ(plan.InsertionStage,
             WholeSceneOutputInsertionStage::NativeExactFloor);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::NativeExact);
    CHECK_EQ(plan.UnsupportedChannels, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels, 0);
}

POLICY_TEST(OutputPlanAdmitsConservativeHybridOBJWindowWithWindowMetadataAssist)
{
    const u64 objWindow =
        static_cast<u64>(WholeSceneOutputChannel::OBJWindow);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.ConsideredRepresentationMask =
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::ConservativeHybrid) |
        WholeSceneOutputRepresentationBit(
            WholeSceneOutputRepresentationKind::FinalNative);
    inputs.SelectorConfigured = true;
    inputs.HybridDecision.ConservativeHybridRequested = true;
    inputs.HybridDecision.EffectiveConservativeHybrid = true;
    inputs.WindowEdgeAssist = true;
    inputs.RequiredChannels = objWindow;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK(plan.AdmissionEvaluated);
    CHECK(!plan.AdmissionChangedSelection);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted);
    CHECK_EQ(plan.SelectedPath,
             WholeSceneRenderPath::ConservativeHybridUpscale);
    CHECK_EQ(plan.ReproducedChannels & objWindow, objWindow);
    CHECK_EQ(plan.UnsupportedChannels, 0);
}

POLICY_TEST(OutputPlanTreatsNo3DConservativeHybridEffectsAsNativeResolved)
{
    const u64 alphaBlend =
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend);
    const u64 colorEffect =
        static_cast<u64>(WholeSceneOutputChannel::ColorEffect);
    const u64 semiTransparentOBJ =
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ);
    const u64 nativeResolvedEffects =
        alphaBlend | colorEffect | semiTransparentOBJ;

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.SelectorConfigured = true;
    inputs.HybridDecision.ConservativeHybridRequested = true;
    inputs.HybridDecision.EffectiveConservativeHybrid = true;
    inputs.HybridDecision.ActiveDirect3D = false;
    inputs.HybridDecision.RenderNativeFallback = true;
    inputs.RequiredChannels = nativeResolvedEffects;
    inputs.BlendCnt = static_cast<u16>((1u << 6) | (1u << 0) | (1u << 9));
    inputs.EVA = 7;
    inputs.EVB = 9;
    inputs.SemiTransparentOBJModeMask = 3;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.ReproducedChannels & nativeResolvedEffects,
             nativeResolvedEffects);
    CHECK_EQ(plan.EvidenceUnavailableChannels & nativeResolvedEffects, 0);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted);
    CHECK(!plan.EffectEvidence.Available);

    inputs.HybridDecision.ActiveDirect3D = true;
    inputs.HybridDecision.RenderNativeFallback = false;
    const WholeSceneOutputPlan active3DPlan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(active3DPlan.EvidenceUnavailableChannels & nativeResolvedEffects,
             nativeResolvedEffects);
    CHECK_EQ(active3DPlan.AdmissionReason,
             WholeSceneOutputAdmissionReason::
                 SelectedRepresentationRetainedForParityWithEvidenceDebt);
    CHECK_EQ(active3DPlan.ReproducedChannels & nativeResolvedEffects, 0);
    CHECK_EQ(active3DPlan.RequiredChannels,
             active3DPlan.ReproducedChannels |
                 active3DPlan.UnsupportedChannels |
                 active3DPlan.EvidenceUnavailableChannels);
    CHECK(active3DPlan.EffectEvidence.Available);
    CHECK_EQ(active3DPlan.EffectEvidence.OperandSource,
             WholeSceneOutputEffectOperandSource::NativeStackMetadata);
    CHECK_EQ(active3DPlan.EffectEvidence.ProofMask, 0);
    CHECK_EQ(active3DPlan.EffectEvidence.CandidateChannelMask,
             nativeResolvedEffects);
    CHECK_EQ(active3DPlan.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(active3DPlan.EffectEvidence.BlendMode, 1);
    CHECK_EQ(active3DPlan.EffectEvidence.Target1Mask, 1);
    CHECK_EQ(active3DPlan.EffectEvidence.Target2Mask, 2);
    CHECK_EQ(active3DPlan.EffectEvidence.EVA, 7);
    CHECK_EQ(active3DPlan.EffectEvidence.EVB, 9);
    CHECK(active3DPlan.EffectEvidence.Direct3DTarget1);
    CHECK(!active3DPlan.EffectEvidence.Direct3DTarget2);
    CHECK_EQ(active3DPlan.EffectEvidence.SemiTransparentOBJModeMask, 3);
    CHECK(!active3DPlan.EffectEvidence.OBJTarget1);
    CHECK(!active3DPlan.EffectEvidence.OBJTarget2);
}

POLICY_TEST(OutputPlanProvesOnlyBoundedActive3DAlphaBlendOwnership)
{
    const u64 alphaBlend =
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.SelectorConfigured = true;
    inputs.HybridDecision.ConservativeHybridRequested = true;
    inputs.HybridDecision.EffectiveConservativeHybrid = true;
    inputs.HybridDecision.ActiveDirect3D = true;
    inputs.HybridDecision.RenderNativeFallback = false;
    inputs.Target2AlphaBlendAssist = true;
    inputs.RequiredChannels = alphaBlend;
    inputs.BlendCnt = static_cast<u16>((1u << 6) | 17u | (45u << 8));
    inputs.EVA = 16;
    inputs.EVB = 16;

    const WholeSceneOutputPlan proven = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(proven.EffectEvidence.ProofMask,
             static_cast<u32>(WholeSceneOutputEffectProof::
                 ConservativeHybridAlphaBothTargets));
    CHECK_EQ(proven.EffectEvidence.ProvenOwnershipChannelMask, alphaBlend);
    CHECK_EQ(proven.ReproducedChannels & alphaBlend, alphaBlend);
    CHECK_EQ(proven.EvidenceUnavailableChannels & alphaBlend, 0);
    CHECK_EQ(proven.AdmissionReason,
             WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted);

    inputs.Target2AlphaBlendAssist = false;
    const WholeSceneOutputPlan assistOff = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(assistOff.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(assistOff.EvidenceUnavailableChannels & alphaBlend, alphaBlend);

    inputs.Target2AlphaBlendAssist = true;
    inputs.BlendCnt &= ~(1u << 8);
    const WholeSceneOutputPlan target2Missing = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(target2Missing.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(target2Missing.EvidenceUnavailableChannels & alphaBlend, alphaBlend);

    inputs.BlendCnt |= (1u << 8);
    inputs.BlendCnt &= ~(1u << 0);
    const WholeSceneOutputPlan target1Missing = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(target1Missing.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(target1Missing.EvidenceUnavailableChannels & alphaBlend, alphaBlend);

    inputs.BlendCnt |= (1u << 0);
    inputs.BlendCnt &= ~(3u << 6);
    inputs.BlendCnt |= (2u << 6);
    const WholeSceneOutputPlan nonAlpha = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(nonAlpha.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(nonAlpha.EvidenceUnavailableChannels & alphaBlend, alphaBlend);
}

POLICY_TEST(OutputPlanProvesOnlyBoundedActive3DDarkenOwnership)
{
    const u64 colorEffect =
        static_cast<u64>(WholeSceneOutputChannel::ColorEffect);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.SelectorConfigured = true;
    inputs.HybridDecision.ConservativeHybridRequested = true;
    inputs.HybridDecision.EffectiveConservativeHybrid = true;
    inputs.HybridDecision.ActiveDirect3D = true;
    inputs.HybridDecision.RenderNativeFallback = false;
    inputs.RequiredChannels = colorEffect;
    inputs.BlendCnt = static_cast<u16>((3u << 6) | 31u | (31u << 8));
    inputs.EVY = 5;

    const WholeSceneOutputPlan proven = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(proven.EffectEvidence.ProofMask,
             static_cast<u32>(WholeSceneOutputEffectProof::
                 ConservativeHybridDirect3DDarken));
    CHECK_EQ(proven.EffectEvidence.ProvenOwnershipChannelMask, colorEffect);
    CHECK_EQ(proven.ReproducedChannels & colorEffect, colorEffect);
    CHECK_EQ(proven.EvidenceUnavailableChannels & colorEffect, 0);
    CHECK_EQ(proven.AdmissionReason,
             WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted);

    inputs.EVY = 0;
    const WholeSceneOutputPlan inactive = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(inactive.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(inactive.EvidenceUnavailableChannels & colorEffect, colorEffect);

    inputs.EVY = 5;
    inputs.BlendCnt &= ~(1u << 0);
    const WholeSceneOutputPlan direct3DNotTarget1 =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(direct3DNotTarget1.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(direct3DNotTarget1.EvidenceUnavailableChannels & colorEffect,
             colorEffect);

    inputs.BlendCnt |= (1u << 0);
    inputs.BlendCnt &= ~(3u << 6);
    inputs.BlendCnt |= (2u << 6);
    const WholeSceneOutputPlan brighten = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(brighten.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(brighten.EvidenceUnavailableChannels & colorEffect, colorEffect);

    inputs.BlendCnt &= ~(3u << 6);
    inputs.BlendCnt |= (3u << 6);
    inputs.ScaleDecision.Path = WholeSceneRenderPath::CaptureEpochOverlay;
    const WholeSceneOutputPlan captureEpochSelector =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(captureEpochSelector.EffectEvidence.ProvenOwnershipChannelMask,
             colorEffect);
    CHECK_EQ(captureEpochSelector.EvidenceUnavailableChannels & colorEffect, 0);

    inputs.HybridDecision.EffectiveConservativeHybrid = false;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::OverlayOperatorUpscale;
    const WholeSceneOutputPlan nonConservative =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(nonConservative.EffectEvidence.ProvenOwnershipChannelMask, 0);
    CHECK_EQ(nonConservative.EvidenceUnavailableChannels & colorEffect,
             colorEffect);
}

POLICY_TEST(OutputPlanProvesOnlyMode1OBJZeroSecondOwnership)
{
    const u64 semiTransparentOBJ =
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.SelectorConfigured = true;
    inputs.HybridDecision.ConservativeHybridRequested = true;
    inputs.HybridDecision.EffectiveConservativeHybrid = true;
    inputs.HybridDecision.ActiveDirect3D = true;
    inputs.HybridDecision.RenderNativeFallback = false;
    inputs.Target2AlphaBlendAssist = true;
    inputs.RequiredChannels = semiTransparentOBJ;
    inputs.BlendCnt = static_cast<u16>((3u << 6) | 31u | (31u << 8));
    inputs.EVA = 16;
    inputs.EVB = 0;
    inputs.EVY = 5;
    inputs.SemiTransparentOBJModeMask = 1;

    const WholeSceneOutputPlan proven = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(proven.EffectEvidence.ProofMask,
             static_cast<u32>(WholeSceneOutputEffectProof::
                 ConservativeHybridMode1OBJZeroSecond));
    CHECK_EQ(proven.EffectEvidence.ProvenOwnershipChannelMask,
             semiTransparentOBJ);
    CHECK_EQ(proven.ReproducedChannels & semiTransparentOBJ,
             semiTransparentOBJ);
    CHECK_EQ(proven.EvidenceUnavailableChannels & semiTransparentOBJ, 0);

    inputs.SemiTransparentOBJModeMask = 2;
    const WholeSceneOutputPlan mode3 = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(mode3.EffectEvidence.ProvenOwnershipChannelMask, 0);

    inputs.SemiTransparentOBJModeMask = 3;
    const WholeSceneOutputPlan mixedModes = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(mixedModes.EffectEvidence.ProvenOwnershipChannelMask, 0);

    inputs.SemiTransparentOBJModeMask = 1;
    inputs.EVB = 1;
    const WholeSceneOutputPlan contributingSecond =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(contributingSecond.EffectEvidence.ProvenOwnershipChannelMask, 0);

    inputs.EVB = 0;
    inputs.EVA = 15;
    const WholeSceneOutputPlan reducedFirst = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(reducedFirst.EffectEvidence.ProvenOwnershipChannelMask, 0);

    inputs.EVA = 16;
    inputs.BlendCnt &= ~(1u << (8 + 4));
    const WholeSceneOutputPlan objNotTarget2 = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(objNotTarget2.EffectEvidence.ProvenOwnershipChannelMask, 0);

    inputs.BlendCnt |= (1u << (8 + 4));
    inputs.BlendCnt &= ~(1u << 8);
    const WholeSceneOutputPlan direct3DNotTarget2 =
        BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(direct3DNotTarget2.EffectEvidence.ProvenOwnershipChannelMask, 0);

    inputs.BlendCnt |= (1u << 8);
    inputs.Target2AlphaBlendAssist = false;
    const WholeSceneOutputPlan assistOff = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(assistOff.EffectEvidence.ProvenOwnershipChannelMask, 0);
}

POLICY_TEST(OutputPlanCaptureObservationDoesNotCreateAuthorityAfterAdmission)
{
    const u64 displayCapture =
        static_cast<u64>(WholeSceneOutputChannel::DisplayCapture);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput;
    inputs.PathDecision.Path = WholeSceneRenderPath::PhysicalFinalPostprocessInput;
    inputs.RequiredChannels =
        displayCapture |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.CaptureCnt = 0xA1000010u;
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Bottom;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    CHECK_EQ(plan.EvidenceUnavailableChannels & displayCapture, displayCapture);

    WholeSceneCaptureInputTrace capture = {};
    capture.Observed = true;
    capture.YStart = 0;
    capture.YEnd = 192;
    capture.CaptureCnt = inputs.CaptureCnt;
    capture.Kind = WholeSceneCaptureInputKind::NativeEngineOutput;
    capture.AuthoritativePreMaster = true;
    capture.NativeSized = true;
    WholeSceneOutputExecutionTrace trace = {};
    ObserveWholeSceneCaptureInput(plan, trace, capture);

    CHECK_EQ(plan.ReproducedChannels & displayCapture, 0);
    CHECK_EQ(plan.EvidenceUnavailableChannels & displayCapture, displayCapture);
    CHECK(!trace.RepresentationEvidenceCaptureInputTraceComparable);
    CHECK(!trace.RepresentationEvidenceCaptureInputTraceAgreement);
    CHECK((trace.ObservedTransformMask &
           WholeSceneOutputTransformBit(
               WholeSceneOutputTransformKind::DisplayCapture)) != 0);
    CHECK_EQ(trace.TransformDisagreementMask, 0);
}

POLICY_TEST(OutputPlanClassifiesCurrentRawVRAMAsAuthoritativeDisplayProduct)
{
    const u64 vramDisplay =
        static_cast<u64>(WholeSceneOutputChannel::VRAMDisplay);
    const u64 displaySelection =
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection);
    const u64 physicalRouting =
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::ScalePathUnavailable;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    inputs.PathDecision.CurrentReason =
        WholeSceneCurrentPathReason::ScalePathUnavailable;
    inputs.RequiredChannels = vramDisplay | displaySelection | physicalRouting;
    inputs.DisplayMode = 2;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Top;

    const WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    CHECK_EQ(plan.SelectedPath, WholeSceneRenderPath::Current);
    CHECK_EQ(plan.SelectedRepresentation,
             WholeSceneOutputRepresentationKind::CurrentVRAMDisplay);
    CHECK_EQ(plan.InsertionStage,
             WholeSceneOutputInsertionStage::DisplayProductUpscale);
    CHECK_EQ(plan.CorrectnessClass,
             WholeSceneOutputCorrectnessClass::NativeEnhanced);
    CHECK_EQ(plan.ReproducedChannels & vramDisplay, vramDisplay);
    CHECK_EQ(plan.EvidenceUnavailableChannels & vramDisplay, 0);
    CHECK_EQ(plan.AdmissionReason,
             WholeSceneOutputAdmissionReason::SelectedRepresentationAdmitted);
}

POLICY_TEST(OutputPlanPresentationObservationRetainsTransformRange)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 37;
    inputs.YEnd = 141;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::ScalePathUnavailable;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::MasterBrightness) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.MasterBrightness = static_cast<u16>((1u << 14) | 11u);
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Top;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace = {};
    WholeScenePresentationTrace presentation = {};
    presentation.Observed = true;
    presentation.YStart = inputs.YStart;
    presentation.YEnd = inputs.YEnd;
    presentation.DisplayMode = inputs.DisplayMode;
    presentation.MasterBrightnessAppliedInFinalPass = true;
    presentation.PhysicalTarget = inputs.PhysicalTarget;
    ObserveWholeScenePresentation(plan, trace, presentation, inputs.MasterBrightness);

    CHECK_EQ(WholeSceneOutputUnobservedTransformMask(plan, trace), 0);
    CHECK_EQ(trace.TransformDisagreementMask, 0);

    presentation.PhysicalTarget = WholeScenePhysicalTarget::Bottom;
    ObserveWholeScenePresentation(plan, trace, presentation, inputs.MasterBrightness);
    CHECK((trace.TransformDisagreementMask &
           WholeSceneOutputTransformBit(
               WholeSceneOutputTransformKind::PhysicalRouting)) != 0);
}

POLICY_TEST(OutputPlanExecutesMasterBrightnessOnlyInsidePlannedRange)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 24;
    inputs.YEnd = 168;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::HighResCompositor;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::HighResCompositor;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::MasterBrightness) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.MasterBrightness = static_cast<u16>((2u << 14) | 9u);
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Bottom;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace = {};
    const WholeSceneOutputTransform* transform =
        ResolveWholeSceneOutputTransformForExecution(
            plan,
            WholeSceneOutputTransformKind::MasterBrightness,
            40,
            120);
    CHECK(transform != nullptr);
    CHECK_EQ(transform->State, inputs.MasterBrightness);
    CHECK_EQ(transform->AuxState, (2u << 8) | 9u);
    CHECK(ResolveWholeSceneOutputTransformForExecution(
              plan,
              WholeSceneOutputTransformKind::MasterBrightness,
              0,
              120) == nullptr);
    CHECK(ResolveWholeSceneOutputTransformForExecution(
              plan,
              WholeSceneOutputTransformKind::MasterBrightness,
              40,
              192) == nullptr);

    WholeScenePresentationTrace presentation = {};
    presentation.Observed = true;
    presentation.YStart = inputs.YStart;
    presentation.YEnd = inputs.YEnd;
    presentation.DisplayMode = inputs.DisplayMode;
    presentation.MasterBrightnessAppliedInFinalPass = true;
    presentation.MasterBrightnessExecutedByPlan = true;
    presentation.PhysicalTarget = inputs.PhysicalTarget;
    ObserveWholeScenePresentation(plan, trace, presentation, inputs.MasterBrightness);

    const u32 brightnessBit = WholeSceneOutputTransformBit(
        WholeSceneOutputTransformKind::MasterBrightness);
    CHECK(!trace.TransformsShadowOnly);
    CHECK_EQ(trace.PlanExecutedTransformMask, brightnessBit);
    CHECK_EQ(trace.TransformDisagreementMask, 0);
    CHECK_EQ(WholeSceneOutputUnobservedTransformMask(plan, trace), 0);

    transform = ResolveWholeSceneOutputTransformForExecution(
        plan,
        WholeSceneOutputTransformKind::MasterBrightness,
        inputs.YStart,
        inputs.YEnd);
    CHECK(transform != nullptr);
    const auto* transformTrace = FindTransformTrace(
        plan, trace, WholeSceneOutputTransformKind::MasterBrightness);
    CHECK(transformTrace != nullptr);
    CHECK(transformTrace->PlanExecuted);
    CHECK(!transformTrace->LegacyApplied);
    CHECK(transformTrace->TraceAgreement);
}

POLICY_TEST(OutputPlanUsesExplicitStableFrameBrightnessRange)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 89;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::MasterBrightness);
    inputs.MasterBrightness = 0x8010;
    inputs.MasterBrightnessYStart = 0;
    inputs.MasterBrightnessYEnd = 192;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace = {};
    const WholeSceneOutputTransform* transform =
        ResolveWholeSceneOutputTransformForExecution(
            plan,
            WholeSceneOutputTransformKind::MasterBrightness,
            0,
            192);
    CHECK(transform != nullptr);
    CHECK_EQ(transform->YStart, 0);
    CHECK_EQ(transform->YEnd, 192);
    CHECK_EQ(transform->State, 0x8010);

    CHECK(MarkWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::MasterBrightness,
        0,
        192,
        0x8010,
        (2u << 8) | 16u));
    WholeSceneOutputTransformObservation observation = {};
    observation.Kind = WholeSceneOutputTransformKind::MasterBrightness;
    observation.YStart = 0;
    observation.YEnd = 192;
    observation.State = 0x8010;
    observation.AuxState = (2u << 8) | 16u;
    observation.PlanApplied = true;
    ObserveWholeSceneOutputTransform(plan, trace, observation);

    CHECK_EQ(trace.TransformDisagreementMask, 0);
    CHECK_EQ(WholeSceneOutputUnobservedTransformMask(plan, trace), 0);
    const auto* transformTrace = FindTransformTrace(
        plan, trace, WholeSceneOutputTransformKind::MasterBrightness);
    CHECK(transformTrace != nullptr);
    CHECK(transformTrace->PlanExecuted);
    CHECK(transformTrace->TraceAgreement);
}

POLICY_TEST(OutputPlanCompletionSuppressesMatchingPreappliedBrightness)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::MasterBrightness) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.MasterBrightness = static_cast<u16>((1u << 14) | 6u);
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Top;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace = {};
    CHECK(!MarkWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::MasterBrightness,
        inputs.YStart,
        inputs.YEnd,
        static_cast<u16>((1u << 14) | 5u),
        (1u << 8) | 5u));
    CHECK(MarkWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::MasterBrightness,
        inputs.YStart,
        inputs.YEnd,
        inputs.MasterBrightness,
        (1u << 8) | 6u));
    CHECK(IsWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::MasterBrightness,
        32,
        160,
        inputs.MasterBrightness,
        (1u << 8) | 6u));
    CHECK(!IsWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::MasterBrightness,
        32,
        160,
        static_cast<u16>((1u << 14) | 5u),
        (1u << 8) | 5u));

    WholeScenePresentationTrace presentation = {};
    presentation.Observed = true;
    presentation.YStart = inputs.YStart;
    presentation.YEnd = inputs.YEnd;
    presentation.DisplayMode = inputs.DisplayMode;
    presentation.MasterBrightnessAlreadyApplied = true;
    presentation.MasterBrightnessExecutedByPlan = true;
    presentation.PhysicalTarget = inputs.PhysicalTarget;
    ObserveWholeScenePresentation(plan, trace, presentation, inputs.MasterBrightness);

    const WholeSceneOutputTransform* transform =
        ResolveWholeSceneOutputTransformForExecution(
            plan,
            WholeSceneOutputTransformKind::MasterBrightness,
            inputs.YStart,
            inputs.YEnd);
    CHECK(transform != nullptr);
    const auto* transformTrace = FindTransformTrace(
        plan, trace, WholeSceneOutputTransformKind::MasterBrightness);
    CHECK(transformTrace != nullptr);
    CHECK(transformTrace->PlanExecuted);
    CHECK(!transformTrace->LegacyApplied);
    CHECK(transformTrace->TraceAgreement);
    CHECK_EQ(trace.TransformDisagreementMask, 0);
}

POLICY_TEST(OutputPlanExecutesProvenFullScreenEVYFromCompositorTransform)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::ColorEffect) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.BlendCnt = static_cast<u16>((3u << 6) | 0x3Fu);
    inputs.EVY = 8;
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Top;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace = {};
    const WholeSceneOutputTransform* transform =
        ResolveWholeSceneOutputTransformForExecution(
            plan,
            WholeSceneOutputTransformKind::CompositorColorEffect,
            inputs.YStart,
            inputs.YEnd);
    CHECK(transform != nullptr);
    CHECK_EQ(transform->State, inputs.BlendCnt);
    CHECK_EQ(transform->AuxState, inputs.EVY);
    CHECK(MarkWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::CompositorColorEffect,
        inputs.YStart,
        inputs.YEnd,
        inputs.BlendCnt,
        inputs.EVY));

    WholeSceneOutputTransformObservation observation = {};
    observation.Kind = WholeSceneOutputTransformKind::CompositorColorEffect;
    observation.YStart = inputs.YStart;
    observation.YEnd = inputs.YEnd;
    observation.State = inputs.BlendCnt;
    observation.AuxState = inputs.EVY;
    observation.PlanApplied = true;
    ObserveWholeSceneOutputTransform(plan, trace, observation);

    const u32 colorEffectBit = WholeSceneOutputTransformBit(
        WholeSceneOutputTransformKind::CompositorColorEffect);
    CHECK_EQ(trace.PlanExecutedTransformMask, colorEffectBit);
    CHECK_EQ(trace.TransformDisagreementMask, 0);
    const auto* transformTrace = FindTransformTrace(
        plan, trace, WholeSceneOutputTransformKind::CompositorColorEffect);
    CHECK(transformTrace != nullptr);
    CHECK(transformTrace->PlanExecuted);
    CHECK(!transformTrace->LegacyApplied);
    CHECK(transformTrace->TraceAgreement);
}

POLICY_TEST(OutputPlanExecutesFrameLevelPhysicalPresentationBrightness)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 141;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.HasScaleDecision = true;
    inputs.ScaleDecision.Reason = WholeSceneScaleDecisionReason::OverlayOperator;
    inputs.ScaleDecision.Path = WholeSceneRenderPath::ConservativeHybridUpscale;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    inputs.DisplayMode = 1;
    inputs.PhysicalTarget = WholeScenePhysicalTarget::Bottom;
    inputs.HasPhysicalPresentationBrightness = true;
    inputs.PhysicalPresentationBrightness = 0x4010;
    inputs.PhysicalPresentationBrightnessYStart = 0;
    inputs.PhysicalPresentationBrightnessYEnd = 192;

    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace = {};
    const WholeSceneOutputTransform* transform =
        ResolveWholeSceneOutputTransformForExecution(
            plan,
            WholeSceneOutputTransformKind::PhysicalPresentationBrightness,
            141,
            192);
    CHECK(transform != nullptr);
    CHECK_EQ(transform->YStart, 0);
    CHECK_EQ(transform->YEnd, 192);
    CHECK_EQ(transform->State, 0x4010);
    CHECK_EQ(transform->AuxState, 0x110);
    CHECK_EQ(transform->Target, WholeScenePhysicalTarget::Bottom);
    CHECK(MarkWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::PhysicalPresentationBrightness,
        141,
        192,
        0x4010,
        0x110));
    CHECK(IsWholeSceneOutputTransformPlanExecuted(
        plan,
        trace,
        WholeSceneOutputTransformKind::PhysicalPresentationBrightness,
        141,
        192,
        0x4010,
        0x110));
    WholeSceneOutputTransformObservation observation = {};
    observation.Kind =
        WholeSceneOutputTransformKind::PhysicalPresentationBrightness;
    observation.YStart = 141;
    observation.YEnd = 192;
    observation.State = 0x4010;
    observation.AuxState = 0x110;
    observation.Target = WholeScenePhysicalTarget::Bottom;
    observation.PlanApplied = true;
    ObserveWholeSceneOutputTransform(plan, trace, observation);
    const auto* transformTrace = FindTransformTrace(
        plan,
        trace,
        WholeSceneOutputTransformKind::PhysicalPresentationBrightness);
    CHECK(transformTrace != nullptr);
    CHECK(transformTrace->TraceAgreement);
    CHECK_EQ(trace.TransformDisagreementMask, 0);
    CHECK(!ResolveWholeSceneOutputTransformForExecution(
        plan,
        WholeSceneOutputTransformKind::PhysicalPresentationBrightness,
        0,
        193));
}

POLICY_TEST(PhysicalPresentationLedgerRecordsStableCompletedOutput)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::WholeSceneScale;
    inputs.PathDecision.Path = WholeSceneRenderPath::HighResCompositor;
    inputs.OutputScale = 4;
    inputs.RepresentationSourceGeneration = 17;
    inputs.RepresentationRowEpochIdentity = 31;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);

    WholeSceneOutputExecutionTrace trace = {};
    ObserveWholeSceneOutputPlanExecution(
        plan,
        trace,
        plan.SelectedPath,
        plan.CurrentReason,
        plan.ExecutionKind);

    WholeScenePresentationTrace presentation = {};
    presentation.Observed = true;
    presentation.YStart = 0;
    presentation.YEnd = 192;
    presentation.DisplayMode = 1;
    presentation.PhysicalTarget = WholeScenePhysicalTarget::Top;

    WholeScenePhysicalPresentationLedger ledger = {};
    auto first = BuildWholeScenePhysicalPresentationRecord(
        plan,
        trace,
        presentation,
        WholeScenePhysicalSourceEngine::EngineA,
        1,
        10,
        true);
    RecordWholeScenePhysicalPresentation(ledger, first);
    CHECK_EQ(ledger.Top.Count, 1u);
    CHECK_EQ(ledger.Top.LastTransition,
             WholeScenePhysicalPresentationTransition::First);
    CHECK(ledger.Top.Recent[0].CompletionValid);

    auto second = first;
    second.Sequence = 2;
    second.Frame = 11;
    RecordWholeScenePhysicalPresentation(ledger, second);
    CHECK_EQ(ledger.Top.Count, 2u);
    CHECK_EQ(ledger.Top.LastChangeMask,
             static_cast<u32>(PhysicalPresentationChangeNone));
    CHECK_EQ(ledger.Top.LastTransition,
             WholeScenePhysicalPresentationTransition::Stable);
}

POLICY_TEST(PhysicalPresentationLedgerExplainsRouteAndRepresentationChanges)
{
    WholeScenePhysicalPresentationRecord first = {};
    first.Valid = true;
    first.CompletionValid = true;
    first.Target = WholeScenePhysicalTarget::Bottom;
    first.SourceEngine = WholeScenePhysicalSourceEngine::EngineA;
    first.YEnd = 192;
    first.DisplayMode = 1;
    first.SelectedRepresentation =
        WholeSceneOutputRepresentationKind::HighResCompositor;

    WholeScenePhysicalPresentationLedger ledger = {};
    RecordWholeScenePhysicalPresentation(ledger, first);

    auto routeChange = first;
    routeChange.Sequence = 2;
    routeChange.SourceEngine = WholeScenePhysicalSourceEngine::EngineB;
    RecordWholeScenePhysicalPresentation(ledger, routeChange);
    CHECK((ledger.Bottom.LastChangeMask &
           PhysicalPresentationChangeRoute) != 0);
    CHECK_EQ(ledger.Bottom.LastTransition,
             WholeScenePhysicalPresentationTransition::RouteHandoff);

    auto representationChange = routeChange;
    representationChange.Sequence = 3;
    representationChange.SelectedRepresentation =
        WholeSceneOutputRepresentationKind::NativeExactFloor;
    RecordWholeScenePhysicalPresentation(ledger, representationChange);
    CHECK((ledger.Bottom.LastChangeMask &
           PhysicalPresentationChangeRepresentation) != 0);
    CHECK_EQ(ledger.Bottom.LastTransition,
             WholeScenePhysicalPresentationTransition::RepresentationChange);
}

POLICY_TEST(PhysicalPresentationLedgerIdentifiesABAReversionByField)
{
    WholeScenePhysicalPresentationRecord record = {};
    record.Valid = true;
    record.CompletionValid = true;
    record.Target = WholeScenePhysicalTarget::Top;
    record.SourceEngine = WholeScenePhysicalSourceEngine::EngineA;
    record.YEnd = 192;
    record.SelectedRepresentation =
        WholeSceneOutputRepresentationKind::HighResCompositor;
    record.ProductIdentity = 100;

    WholeScenePhysicalPresentationLedger ledger = {};
    RecordWholeScenePhysicalPresentation(ledger, record);
    record.Sequence = 2;
    record.SelectedRepresentation =
        WholeSceneOutputRepresentationKind::NativeStack;
    record.ProductIdentity = 200;
    RecordWholeScenePhysicalPresentation(ledger, record);
    record.Sequence = 3;
    record.SelectedRepresentation =
        WholeSceneOutputRepresentationKind::HighResCompositor;
    record.ProductIdentity = 100;
    RecordWholeScenePhysicalPresentation(ledger, record);

    CHECK_EQ(ledger.Top.LastTransition,
             WholeScenePhysicalPresentationTransition::ABAReversion);
    CHECK((ledger.Top.LastReversionMask &
           PhysicalPresentationChangeRepresentation) != 0);
    CHECK((ledger.Top.LastReversionMask &
           PhysicalPresentationChangeContentIdentity) != 0);
}

POLICY_TEST(PhysicalPresentationRecordCarriesSelectedCaptureProvenance)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.YEnd = 192;
    inputs.PathDecision.Reason =
        WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks;
    inputs.PathDecision.Path =
        WholeSceneRenderPath::SourceACaptureReplacement;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    auto& evidence = plan.CapturePlan.Evidence;
    evidence.Available = true;
    evidence.ContentProven = true;
    evidence.PresentationProven = true;
    evidence.ProductRef.Kind =
        WholeSceneCaptureProductKind::FullCaptureProduct;
    evidence.ProductRef.CaptureBank = 2;
    evidence.ProductRef.CaptureEventSerial = 91;
    evidence.ProductRef.BackgroundEpochSerial = 47;
    evidence.ProductRef.CapturePresentationHash = 0x1234u;
    evidence.ProductRef.CurrentPresentationHash = 0x5678u;
    evidence.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
    evidence.AuthorizationProofKind =
        WholeSceneCaptureProofKind::ExactCaptureEvent;
    evidence.AuthorizationEpochSerial = 46;
    evidence.AuthorizationCaptureBank = 2;
    evidence.Source3DSerial = 22;
    evidence.Source3DSceneHash = 0xDEADBEEFu;
    evidence.SourceKind = 3;
    evidence.ProductMask = 5;

    WholeScenePresentationTrace presentation = {};
    presentation.Observed = true;
    presentation.YEnd = 192;
    presentation.PhysicalTarget = WholeScenePhysicalTarget::Bottom;
    WholeSceneOutputExecutionTrace trace = {};
    const auto record = BuildWholeScenePhysicalPresentationRecord(
        plan,
        trace,
        presentation,
        WholeScenePhysicalSourceEngine::EngineA,
        1,
        1,
        true);

    CHECK(record.SelectedCaptureEvidenceAvailable);
    CHECK(record.SelectedCaptureContentProven);
    CHECK(record.SelectedCapturePresentationProven);
    CHECK_EQ(record.SelectedCaptureProductKind,
             WholeSceneCaptureProductKind::FullCaptureProduct);
    CHECK_EQ(record.SelectedCaptureEventSerial, 91u);
    CHECK_EQ(record.SelectedCaptureBackgroundEpochSerial, 47u);
    CHECK_EQ(record.SelectedCaptureAuthorizationEpochSerial, 46u);
    CHECK_EQ(record.SelectedCaptureSource3DSerial, 22u);
    CHECK_EQ(record.SelectedCaptureProductMask, 5u);

    auto changed = record;
    changed.SelectedCaptureEventSerial++;
    CHECK((CompareWholeScenePhysicalPresentations(record, changed) &
           PhysicalPresentationChangeCapture) != 0);
}

POLICY_TEST(PhysicalPresentationLedgerNeverAssignsMixedRouteToOneScreen)
{
    WholeScenePhysicalPresentationRecord record = {};
    record.Valid = true;
    record.Target = WholeScenePhysicalTarget::Mixed;
    record.SourceEngine = WholeScenePhysicalSourceEngine::Mixed;
    record.YEnd = 192;

    WholeScenePhysicalPresentationLedger ledger = {};
    RecordWholeScenePhysicalPresentation(ledger, record);
    CHECK_EQ(ledger.Top.Count, 0u);
    CHECK_EQ(ledger.Bottom.Count, 0u);
    CHECK_EQ(ledger.MixedTargetCount, 1u);
    CHECK(GetWholeScenePhysicalPresentationHistory(
              ledger,
              WholeScenePhysicalTarget::Mixed) == nullptr);
}

POLICY_TEST(PhysicalPresentationCSVHeaderAndRowStayAligned)
{
    WholeScenePhysicalPresentationHistory history = {};
    history.Count = 1;
    history.Recent[0].Valid = true;
    history.Recent[0].CompletionValid = true;
    history.Recent[0].Sequence = 9;
    history.Recent[0].Target = WholeScenePhysicalTarget::Top;

    std::string header;
    std::string row;
    AppendWholeScenePhysicalPresentationCSVHeader(header, "physical_top");
    AppendWholeScenePhysicalPresentationCSVRow(row, history);

    CHECK(header.find("physical_top_physical_transition") !=
          std::string::npos);
    CHECK(header.find("physical_top_physical_capture_event_serial") !=
          std::string::npos);
    CHECK_EQ(std::count(header.begin(), header.end(), ','),
             std::count(row.begin(), row.end(), ','));
}

POLICY_TEST(OutputPlanCSVHeaderAndRowStayAligned)
{
    WholeSceneOutputPlanInputs inputs = {};
    inputs.PathDecision.Reason = WholeScenePathDecisionReason::ScalePathUnavailable;
    inputs.PathDecision.Path = WholeSceneRenderPath::Current;
    WholeSceneOutputPlan plan = BuildWholeSceneOutputPlan(inputs);
    WholeSceneOutputExecutionTrace trace;
    ObserveWholeSceneOutputPlanExecution(
        plan,
        trace,
        WholeSceneRenderPath::Current,
        WholeSceneCurrentPathReason::None,
        WholeSceneOutputExecutionKind::Current);

    std::string header;
    std::string row;
    AppendWholeSceneOutputPlanCSVHeader(header, "a");
    AppendWholeSceneOutputPlanCSVRow(
        row, plan, WholeSceneOutputRecipePreparation{}, trace);

    CHECK(header.find("a_output_plan_selected_path") != std::string::npos);
    CHECK(header.find("a_output_execution_source_a_selection_observed") !=
          std::string::npos);
    CHECK(header.find("a_output_plan_capture_source_a_selection_bound") ==
          std::string::npos);
    CHECK(header.find("a_output_plan_decision_parity") != std::string::npos);
    CHECK_EQ(std::count(header.begin(), header.end(), ','),
             std::count(row.begin(), row.end(), ','));
}
