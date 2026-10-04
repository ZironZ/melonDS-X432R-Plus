// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolicyTestHarness.h"
#include "WholeSceneCompositorInput.h"

using namespace melonDS;

namespace
{

WholeSceneCompositorRecipeInputs CompleteCapabilities()
{
    WholeSceneCompositorRecipeInputs inputs;
    inputs.Capabilities.CompletedBase = true;
    inputs.Capabilities.OrdinaryOBJBand = true;
    inputs.Capabilities.AffineOBJGroup = true;
    inputs.Capabilities.SemiTransparentOBJ = true;
    inputs.CompletedBaseIdentity = 0xBACEu;
    inputs.SourceGeneration = 0x1234u;
    inputs.RowEpochIdentity = 0x45504F4348u;
    return inputs;
}

WholeSceneSemanticCompositorRecipeInputs SemanticInputs()
{
    WholeSceneSemanticCompositorRecipeInputs inputs;
    inputs.RowEpochIdentity = 0x45504F4348u;
    return inputs;
}

WholeSceneOBJOperand Operand(WholeSceneOBJOperandKind kind,
                             int index,
                             u64 oamMask)
{
    WholeSceneOBJOperand operand;
    operand.Kind = kind;
    operand.BandIndex = kind == WholeSceneOBJOperandKind::OrdinaryBand
        ? index : -1;
    operand.PresentationIndex = index;
    operand.MinTotalPriority = index * 2;
    operand.MaxTotalPriority = index * 2 + 1;
    operand.OAMMask[0] = oamMask;
    return operand;
}

POLICY_TEST(CompositorRecipeNamesPokemonProductsAndOrder)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 0x504F4B45u;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 0, 0x1C00u),
        Operand(WholeSceneOBJOperandKind::AffinePartition, 1, 0x03F0u),
    };

    const auto recipe = BuildWholeSceneOBJCompositorRecipe(
        plan, CompleteCapabilities());

    CHECK(recipe.Ready);
    CHECK_EQ(recipe.Inputs.size(), 3);
    CHECK_EQ(recipe.Inputs[0].Kind,
             WholeSceneCompositorInputKind::CompletedBG3DBase);
    CHECK_EQ(recipe.Inputs[1].Kind,
             WholeSceneCompositorInputKind::OrdinaryOBJBand);
    CHECK_EQ(recipe.Inputs[1].ProductIndex, 0);
    CHECK_EQ(recipe.Inputs[1].CoverageAuthority,
             WholeSceneCompositorCoverageAuthority::ReconstructedRGBA);
    CHECK_EQ(recipe.Inputs[1].PresentationKind,
             WholeSceneCompositorPresentationKind::ReconstructedSurface);
    CHECK_EQ(recipe.Inputs[2].Kind,
             WholeSceneCompositorInputKind::AffineOBJGroup);
    CHECK_EQ(recipe.Inputs[2].PresentationKind,
             WholeSceneCompositorPresentationKind::AffineOutputGrid);
    CHECK_EQ(recipe.Inputs[2].OAMMask[0], 0x03F0u);
    CHECK_EQ(recipe.SourceGeneration, 0x1234u);
}

POLICY_TEST(CompositorRecipeIdentityDeclaresLocalizedAffinePresentation)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 0x53554250u;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::AffinePartition, 0, 0x40u),
    };
    auto outputGridInputs = CompleteCapabilities();
    auto subpixelInputs = outputGridInputs;
    subpixelInputs.AffineOBJSubpixel2x = true;

    const auto outputGrid = BuildWholeSceneOBJCompositorRecipe(
        plan, outputGridInputs);
    const auto subpixel = BuildWholeSceneOBJCompositorRecipe(
        plan, subpixelInputs);

    CHECK(outputGrid.Ready);
    CHECK(subpixel.Ready);
    CHECK_EQ(outputGrid.Inputs[1].PresentationKind,
             WholeSceneCompositorPresentationKind::AffineOutputGrid);
    CHECK_EQ(subpixel.Inputs[1].PresentationKind,
             WholeSceneCompositorPresentationKind::AffineSubpixel2x);
    CHECK_EQ(WholeSceneCompositorRecipePresentationCount(
                 subpixel,
                 WholeSceneCompositorPresentationKind::AffineSubpixel2x),
             1u);
    CHECK(outputGrid.Inputs[1].ProductIdentity !=
          subpixel.Inputs[1].ProductIdentity);
    CHECK(outputGrid.RecipeHash != subpixel.RecipeHash);
}

POLICY_TEST(CompositorRecipeNamesMarioSpecialEffectOperand)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 0x4D415249u;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::AffinePartition, 0, 1ull << 59),
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 1, 1ull << 54),
        Operand(WholeSceneOBJOperandKind::SemiTransparentOBJ, 2, 1ull << 52),
        Operand(WholeSceneOBJOperandKind::AffinePartition, 3, 1ull << 51),
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 0, 1ull << 40),
    };

    const auto recipe = BuildWholeSceneOBJCompositorRecipe(
        plan, CompleteCapabilities());

    CHECK(recipe.Ready);
    CHECK_EQ(recipe.Inputs.size(), 6);
    CHECK_EQ(recipe.Inputs[3].Kind,
             WholeSceneCompositorInputKind::SemiTransparentOBJ);
    CHECK_EQ(recipe.Inputs[3].CoverageAuthority,
             WholeSceneCompositorCoverageAuthority::NativeSemanticEffect);
    CHECK_EQ(recipe.Inputs[3].PresentationKind,
             WholeSceneCompositorPresentationKind::DSSpecialEffect);
    CHECK((recipe.Inputs[3].CapabilityMask &
           CompositorInputCapabilityDSSpecialEffect) != 0);
}

POLICY_TEST(CompositorRecipeAdmitsLargeAffinePlanWithSemanticOnlySpecials)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 0x4F424A32u;
    for (int index = 0; index < 34; index++)
    {
        const bool special = index < 11;
        auto operand = Operand(
            special ? WholeSceneOBJOperandKind::SemiTransparentOBJ
                    : WholeSceneOBJOperandKind::AffinePartition,
            index,
            1ull << (index & 63));
        if (special)
            operand.PresentationIndex = -1;
        plan.BackToFront.push_back(operand);
    }

    auto inputs = CompleteCapabilities();
    inputs.AffineOBJSubpixel2x = true;
    const auto recipe = BuildWholeSceneOBJCompositorRecipe(plan, inputs);

    CHECK(recipe.Ready);
    CHECK_EQ(recipe.Inputs.size(), 35);
    CHECK_EQ(recipe.Inputs[1].Kind,
             WholeSceneCompositorInputKind::SemiTransparentOBJ);
    CHECK_EQ(recipe.Inputs[1].PresentationIndex, -1);
    CHECK_EQ(recipe.Inputs[1].PresentationKind,
             WholeSceneCompositorPresentationKind::
                 DSSpecialEffectSubpixel2x);
    CHECK_EQ(recipe.Inputs[12].PresentationKind,
             WholeSceneCompositorPresentationKind::AffineSubpixel2x);
}

POLICY_TEST(CompositorRecipeRejectsUnsupportedProductWithoutChangingOrder)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 0, 1ull << 10),
        Operand(WholeSceneOBJOperandKind::AffinePartition, 1, 1ull << 11),
    };
    auto inputs = CompleteCapabilities();
    inputs.Capabilities.AffineOBJGroup = false;

    const auto recipe = BuildWholeSceneOBJCompositorRecipe(plan, inputs);

    CHECK(!recipe.Ready);
    CHECK((recipe.RejectionMask & CompositorRecipeRejectAffineGroup) != 0);
    CHECK_EQ(recipe.Inputs.size(), 3);
    CHECK_EQ(recipe.Inputs[1].Kind,
             WholeSceneCompositorInputKind::OrdinaryOBJBand);
    CHECK_EQ(recipe.Inputs[2].Kind,
             WholeSceneCompositorInputKind::AffineOBJGroup);
}

POLICY_TEST(CompositorRecipeIdentityIncludesSourceGeneration)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 7;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 0, 1ull << 10),
    };
    auto firstInputs = CompleteCapabilities();
    auto secondInputs = firstInputs;
    secondInputs.SourceGeneration++;

    const auto first = BuildWholeSceneOBJCompositorRecipe(plan, firstInputs);
    const auto second = BuildWholeSceneOBJCompositorRecipe(plan, secondInputs);

    CHECK(first.Ready);
    CHECK(second.Ready);
    CHECK(first.RecipeHash != second.RecipeHash);
    CHECK(first.Inputs[1].ProductIdentity != second.Inputs[1].ProductIdentity);
}

POLICY_TEST(OrderedCompositorRecipeNamesSemanticBaseDependency)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 9;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::AffinePartition, 0, 1ull << 12),
    };
    auto inputs = CompleteCapabilities();
    inputs.CompletedBaseRecipeHash = 0x51554F54u;

    const auto recipe = BuildWholeSceneOBJCompositorRecipe(plan, inputs);

    CHECK(recipe.Ready);
    CHECK_EQ(recipe.DependencyRecipeHash, 0x51554F54u);
}

POLICY_TEST(OperandExcludedOverlayRecipeNamesOrderedSuffix)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 10;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 0, 1ull << 10),
        Operand(WholeSceneOBJOperandKind::AffinePartition, 1, 1ull << 11),
        Operand(WholeSceneOBJOperandKind::OrdinaryBand, 2, 1ull << 12),
    };
    auto orderedInputs = CompleteCapabilities();
    orderedInputs.CompletedBaseRecipeHash = 0x53454D41u;
    const auto ordered = BuildWholeSceneOBJCompositorRecipe(
        plan, orderedInputs);
    WholeSceneOperandExcludedOverlayInputs overlayInputs;
    overlayInputs.Requested = true;
    overlayInputs.Direct3D = true;
    overlayInputs.OutputScale = 4;

    const auto overlay = BuildWholeSceneOperandExcludedOverlayRecipe(
        ordered, overlayInputs);

    CHECK(overlay.Ready);
    CHECK_EQ(overlay.SemanticBaseRecipeHash, 0x53454D41u);
    CHECK_EQ(overlay.OrderedPresentationRecipeHash, ordered.RecipeHash);
    CHECK_EQ(overlay.FirstExcludedOperand, 1);
    CHECK_EQ(overlay.ExcludedOAMMask[0],
             (1ull << 11) | (1ull << 12));
}

POLICY_TEST(OperandExcludedOverlayRecipeRejectsSpecialSuffix)
{
    WholeSceneOBJOperandPlan plan;
    plan.Ready = true;
    plan.PlanHash = 11;
    plan.BackToFront = {
        Operand(WholeSceneOBJOperandKind::AffinePartition, 0, 1ull << 11),
        Operand(WholeSceneOBJOperandKind::SemiTransparentOBJ, 1, 1ull << 12),
    };
    auto orderedInputs = CompleteCapabilities();
    orderedInputs.CompletedBaseRecipeHash = 0x53454D42u;
    const auto ordered = BuildWholeSceneOBJCompositorRecipe(
        plan, orderedInputs);
    WholeSceneOperandExcludedOverlayInputs overlayInputs;
    overlayInputs.Requested = true;
    overlayInputs.Direct3D = true;

    const auto overlay = BuildWholeSceneOperandExcludedOverlayRecipe(
        ordered, overlayInputs);

    CHECK(!overlay.Ready);
    CHECK((overlay.RejectionMask &
           OperandExcludedOverlayRejectSpecialOBJ) != 0);
}

POLICY_TEST(SemanticCompositorRecipeNamesCurrentResolvedInputs)
{
    auto inputs = SemanticInputs();
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.OutputScale = 4;
    inputs.EnabledBGMask = 0xFu;
    inputs.BGProductsAvailableMask = 0xEu;
    inputs.Direct3DBGMask = 1u << 0;
    inputs.EnhancedBGMask = (1u << 2) | (1u << 3);
    inputs.ReconstructedBGCoverageMask = 1u << 2;
    inputs.Direct3DProductAvailable = true;
    inputs.OBJEnabled = true;
    inputs.OBJProductAvailable = true;
    inputs.ResolvedOBJProduct = true;
    inputs.BGSourceGeneration[1] = 11;
    inputs.BGSourceGeneration[2] = 12;
    inputs.BGSourceGeneration[3] = 13;
    inputs.OBJSourceGeneration = 21;

    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(inputs);

    CHECK(recipe.Ready);
    CHECK_EQ(recipe.Kind, WholeSceneCompositorRecipeKind::SemanticResolve);
    CHECK_EQ(recipe.Inputs.size(), 5);
    CHECK_EQ(recipe.Inputs[0].Kind,
             WholeSceneCompositorInputKind::Direct3D);
    CHECK_EQ(recipe.Inputs[3].Kind,
             WholeSceneCompositorInputKind::LogicalBG);
    CHECK_EQ(recipe.Inputs[4].Kind,
             WholeSceneCompositorInputKind::ResolvedOBJSurface);
    CHECK_EQ(WholeSceneCompositorRecipeEnhancedBGMask(recipe), 0xCu);
    CHECK_EQ(
        WholeSceneCompositorRecipeReconstructedBGCoverageMask(recipe),
        1u << 2);
    CHECK(WholeSceneCompositorRecipeHasInput(
        recipe, WholeSceneCompositorInputKind::ResolvedOBJSurface));
}

POLICY_TEST(OrdinarySpriteReconstructionDoesNotRequireAffineGeometry)
{
    // A text-only scene still reconstructs ordinary sprites. Turning a
    // separate affine sprite to an identity matrix must not change their input.
    auto inputs = SemanticInputs();
    inputs.EnabledBGMask = 1u << 2;
    inputs.BGProductsAvailableMask = inputs.EnabledBGMask;
    inputs.OBJEnabled = true;
    inputs.OBJProductAvailable = true;
    inputs.ResolvedOBJProduct = ShouldResolveOrdinaryOBJSurface(
        true, true, true, false, 0, 192);

    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(inputs);
    CHECK(recipe.Ready);
    CHECK(WholeSceneCompositorRecipeHasInput(
        recipe, WholeSceneCompositorInputKind::ResolvedOBJSurface));
}

POLICY_TEST(OrdinarySpriteReconstructionRetainsScopeAndWindowGuards)
{
    CHECK(!ShouldResolveOrdinaryOBJSurface(false, true, true, false, 0, 192));
    CHECK(!ShouldResolveOrdinaryOBJSurface(true, false, true, false, 0, 192));
    CHECK(!ShouldResolveOrdinaryOBJSurface(true, true, false, false, 0, 192));
    CHECK(!ShouldResolveOrdinaryOBJSurface(true, true, true, true, 0, 192));
    CHECK(!ShouldResolveOrdinaryOBJSurface(true, true, true, false, 1, 192));
    CHECK(!ShouldResolveOrdinaryOBJSurface(true, true, true, false, 0, 191));
}

POLICY_TEST(SemanticCompositorRecipeRejectsUnavailableResolvedOBJ)
{
    auto inputs = SemanticInputs();
    inputs.EnabledBGMask = 1u << 2;
    inputs.BGProductsAvailableMask = 1u << 2;
    inputs.OBJEnabled = true;
    inputs.ResolvedOBJProduct = true;

    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(inputs);

    CHECK(!recipe.Ready);
    CHECK((recipe.RejectionMask & CompositorRecipeRejectSemanticOBJ) != 0);
    CHECK(WholeSceneCompositorRecipeHasInput(
        recipe, WholeSceneCompositorInputKind::ResolvedOBJSurface));
}

POLICY_TEST(SemanticCompositorRecipeRejectsCoverageWithoutEnhancedSource)
{
    auto inputs = SemanticInputs();
    inputs.EnabledBGMask = 1u << 3;
    inputs.BGProductsAvailableMask = 1u << 3;
    inputs.ReconstructedBGCoverageMask = 1u << 3;

    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(inputs);

    CHECK(!recipe.Ready);
    CHECK((recipe.RejectionMask &
           CompositorRecipeRejectInvalidSemanticMask) != 0);
}

POLICY_TEST(SemanticCompositorRecipeIdentityIncludesRowScope)
{
    auto firstInputs = SemanticInputs();
    firstInputs.YStart = 0;
    firstInputs.YEnd = 96;
    firstInputs.EnabledBGMask = 1u << 2;
    firstInputs.BGProductsAvailableMask = 1u << 2;
    auto secondInputs = firstInputs;
    secondInputs.YStart = 96;
    secondInputs.YEnd = 192;

    const auto first = BuildWholeSceneSemanticCompositorRecipe(firstInputs);
    const auto second = BuildWholeSceneSemanticCompositorRecipe(secondInputs);

    CHECK(first.Ready);
    CHECK(second.Ready);
    CHECK(first.RecipeHash != second.RecipeHash);
}

POLICY_TEST(CompositorProductsCarryExactRowLifetime)
{
    auto inputs = SemanticInputs();
    inputs.YStart = 24;
    inputs.YEnd = 96;
    inputs.EnabledBGMask = (1u << 1) | (1u << 2);
    inputs.BGProductsAvailableMask = inputs.EnabledBGMask;

    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(inputs);

    CHECK(recipe.Ready);
    CHECK(WholeSceneCompositorRecipeProductsCoverScope(recipe));
    CHECK_EQ(recipe.Inputs.size(), 2);
    for (const auto& product : recipe.Inputs)
    {
        CHECK_EQ(product.ValidYStart, 24);
        CHECK_EQ(product.ValidYEnd, 96);
        CHECK_EQ(product.Lifetime,
                 WholeSceneCompositorLifetimeKind::ScanlineRange);
        CHECK_EQ(product.EffectRole,
                 WholeSceneCompositorEffectRole::SemanticEffectOperand);
        CHECK_EQ(product.RowEpochIdentity, inputs.RowEpochIdentity);
    }
}

POLICY_TEST(CompositorRecipeRejectsUnqualifiedRowEpoch)
{
    auto inputs = SemanticInputs();
    inputs.RowEpochIdentity = 0;
    inputs.EnabledBGMask = 1u << 2;
    inputs.BGProductsAvailableMask = inputs.EnabledBGMask;

    const auto recipe = BuildWholeSceneSemanticCompositorRecipe(inputs);

    CHECK(!recipe.Ready);
    CHECK((recipe.RejectionMask &
           CompositorRecipeRejectInvalidRowEpoch) != 0);
}

POLICY_TEST(CompositorRecipeIdentityIncludesRowEpoch)
{
    auto firstInputs = SemanticInputs();
    firstInputs.EnabledBGMask = 1u << 2;
    firstInputs.BGProductsAvailableMask = firstInputs.EnabledBGMask;
    auto secondInputs = firstInputs;
    secondInputs.RowEpochIdentity++;

    const auto first = BuildWholeSceneSemanticCompositorRecipe(firstInputs);
    const auto second = BuildWholeSceneSemanticCompositorRecipe(secondInputs);

    CHECK(first.Ready);
    CHECK(second.Ready);
    CHECK(first.RecipeHash != second.RecipeHash);
    CHECK(first.Inputs[0].ProductIdentity !=
          second.Inputs[0].ProductIdentity);
}

POLICY_TEST(CompositorRecipeIdentityIncludesEffectState)
{
    auto firstInputs = SemanticInputs();
    firstInputs.EnabledBGMask = 1u << 2;
    firstInputs.BGProductsAvailableMask = 1u << 2;
    firstInputs.EffectState = BuildWholeSceneCompositorEffectState(
        1u << 10, 0x0484u, 8, 8, 4, 0);
    auto secondInputs = firstInputs;
    secondInputs.EffectState = BuildWholeSceneCompositorEffectState(
        1u << 10, 0x0484u, 8, 8, 5, 0);

    const auto first = BuildWholeSceneSemanticCompositorRecipe(firstInputs);
    const auto second = BuildWholeSceneSemanticCompositorRecipe(secondInputs);

    CHECK(first.Ready);
    CHECK(second.Ready);
    CHECK(first.EffectState.StateHash != second.EffectState.StateHash);
    CHECK(first.RecipeHash != second.RecipeHash);
}

}
