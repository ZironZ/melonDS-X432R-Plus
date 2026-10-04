// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WholeSceneCompositorInput.h"

namespace melonDS
{

namespace
{

constexpr u64 kFNVOffset = 1469598103934665603ull;
constexpr u64 kFNVPrime = 1099511628211ull;

void HashValue(u64& hash, u64 value)
{
    for (int byte = 0; byte < 8; byte++)
    {
        hash ^= value & 0xFFu;
        hash *= kFNVPrime;
        value >>= 8;
    }
}

u64 BuildProductIdentity(const WholeSceneCompositorInputProduct& product)
{
    u64 hash = kFNVOffset;
    HashValue(hash, static_cast<u64>(product.Kind));
    HashValue(hash, static_cast<u64>(product.Stage));
    HashValue(hash, static_cast<u64>(product.CoordinateDomain));
    HashValue(hash, static_cast<u64>(product.CoverageAuthority));
    HashValue(hash, static_cast<u64>(product.PresentationKind));
    HashValue(hash, product.CapabilityMask);
    HashValue(hash, static_cast<u64>(product.ProductIndex + 1));
    HashValue(hash, static_cast<u64>(product.PresentationIndex + 1));
    HashValue(hash, static_cast<u64>(product.MinTotalPriority + 1));
    HashValue(hash, static_cast<u64>(product.MaxTotalPriority + 1));
    HashValue(hash, product.OBJMode);
    HashValue(hash, product.OAMMask[0]);
    HashValue(hash, product.OAMMask[1]);
    HashValue(hash, product.SourceGeneration);
    HashValue(hash, product.RowEpochIdentity);
    HashValue(hash, static_cast<u64>(product.ValidYStart));
    HashValue(hash, static_cast<u64>(product.ValidYEnd));
    HashValue(hash, static_cast<u64>(product.Lifetime));
    HashValue(hash, static_cast<u64>(product.EffectRole));
    return hash;
}

void AddProduct(WholeSceneCompositorRecipe& recipe,
                WholeSceneCompositorInputProduct product)
{
    product.ProductIdentity = BuildProductIdentity(product);
    recipe.Inputs.push_back(product);
}

}

WholeSceneCompositorEffectState BuildWholeSceneCompositorEffectState(
    u64 channelMask,
    u16 blendCnt,
    u8 eva,
    u8 evb,
    u8 evy,
    u8 semiTransparentOBJModeMask)
{
    WholeSceneCompositorEffectState state;
    state.ChannelMask = channelMask;
    state.BlendCnt = blendCnt;
    state.EVA = eva > 16 ? 16 : eva;
    state.EVB = evb > 16 ? 16 : evb;
    state.EVY = evy > 16 ? 16 : evy;
    state.SemiTransparentOBJModeMask =
        semiTransparentOBJModeMask & 0x3u;
    u64 hash = kFNVOffset;
    HashValue(hash, state.ChannelMask);
    HashValue(hash, state.BlendCnt);
    HashValue(hash, state.EVA);
    HashValue(hash, state.EVB);
    HashValue(hash, state.EVY);
    HashValue(hash, state.SemiTransparentOBJModeMask);
    state.StateHash = hash;
    return state;
}

bool WholeSceneCompositorRecipeProductsCoverScope(
    const WholeSceneCompositorRecipe& recipe)
{
    if (recipe.YStart < 0 || recipe.YEnd <= recipe.YStart ||
        recipe.YEnd > 192 || recipe.RowEpochIdentity == 0)
    {
        return false;
    }
    for (const auto& product : recipe.Inputs)
    {
        if (product.ValidYStart > recipe.YStart ||
            product.ValidYEnd < recipe.YEnd ||
            product.ValidYStart < 0 || product.ValidYEnd > 192 ||
            product.ValidYStart >= product.ValidYEnd)
        {
            return false;
        }
        if (recipe.RowEpochIdentity == 0 ||
            product.RowEpochIdentity != recipe.RowEpochIdentity)
        {
            return false;
        }
        const auto expectedLifetime =
            product.ValidYStart == 0 && product.ValidYEnd == 192
                ? WholeSceneCompositorLifetimeKind::FullFrameSnapshot
                : WholeSceneCompositorLifetimeKind::ScanlineRange;
        if (product.Lifetime != expectedLifetime)
            return false;
    }
    return true;
}

static WholeSceneCompositorLifetimeKind LifetimeForScope(int ystart, int yend)
{
    return ystart == 0 && yend == 192
        ? WholeSceneCompositorLifetimeKind::FullFrameSnapshot
        : WholeSceneCompositorLifetimeKind::ScanlineRange;
}

static WholeSceneCompositorEffectState CanonicalEffectState(
    const WholeSceneCompositorEffectState& state)
{
    return BuildWholeSceneCompositorEffectState(
        state.ChannelMask,
        state.BlendCnt,
        state.EVA,
        state.EVB,
        state.EVY,
        state.SemiTransparentOBJModeMask);
}

WholeSceneCompositorRecipe BuildWholeSceneOBJCompositorRecipe(
    const WholeSceneOBJOperandPlan& operandPlan,
    const WholeSceneCompositorRecipeInputs& inputs,
    int inputLimit)
{
    WholeSceneCompositorRecipe recipe;
    recipe.Kind = WholeSceneCompositorRecipeKind::OrderedPresentation;
    recipe.OperandPlanHash = operandPlan.PlanHash;
    recipe.DependencyRecipeHash = inputs.CompletedBaseRecipeHash;
    recipe.SourceGeneration = inputs.SourceGeneration;
    recipe.RowEpochIdentity = inputs.RowEpochIdentity;
    recipe.YStart = inputs.YStart;
    recipe.YEnd = inputs.YEnd;
    recipe.OutputScale = inputs.OutputScale;
    recipe.EffectState = CanonicalEffectState(inputs.EffectState);
    recipe.EffectOwner = WholeSceneCompositorEffectOwner::OrderedResolver;

    if (!operandPlan.Ready)
        recipe.RejectionMask |= CompositorRecipeRejectOperandPlan;
    if (inputs.YStart < 0 || inputs.YEnd <= inputs.YStart ||
        inputs.YEnd > 192 || inputs.OutputScale < 1)
    {
        recipe.RejectionMask |= CompositorRecipeRejectInvalidScope;
    }
    if (!inputs.Capabilities.CompletedBase)
        recipe.RejectionMask |= CompositorRecipeRejectCompletedBase;
    if (inputs.RowEpochIdentity == 0)
        recipe.RejectionMask |= CompositorRecipeRejectInvalidRowEpoch;
    if (inputLimit < 1 || operandPlan.BackToFront.size() + 1 >
            static_cast<std::size_t>(inputLimit))
    {
        recipe.RejectionMask |= CompositorRecipeRejectInputLimit;
    }

    WholeSceneCompositorInputProduct base;
    base.Kind = WholeSceneCompositorInputKind::CompletedBG3DBase;
    base.Stage = WholeSceneCompositorInputStage::CompletedSceneBase;
    base.CoordinateDomain =
        WholeSceneCompositorCoordinateDomain::OutputPresentation;
    base.CoverageAuthority =
        WholeSceneCompositorCoverageAuthority::CompletedScene;
    base.PresentationKind =
        WholeSceneCompositorPresentationKind::CompletedScene;
    base.CapabilityMask = CompositorInputCapabilityDestination;
    base.SourceGeneration = inputs.SourceGeneration;
    base.RowEpochIdentity = inputs.RowEpochIdentity;
    base.ValidYStart = inputs.YStart;
    base.ValidYEnd = inputs.YEnd;
    base.Lifetime = LifetimeForScope(inputs.YStart, inputs.YEnd);
    base.EffectRole =
        WholeSceneCompositorEffectRole::EffectsResolvedInProduct;
    base.ProductIdentity = BuildProductIdentity(base);
    HashValue(base.ProductIdentity, inputs.CompletedBaseIdentity);
    recipe.Inputs.push_back(base);

    for (const auto& operand : operandPlan.BackToFront)
    {
        WholeSceneCompositorInputProduct product;
        product.Stage = WholeSceneCompositorInputStage::PresentationOperand;
        product.CoordinateDomain =
            WholeSceneCompositorCoordinateDomain::OutputPresentation;
        product.ProductIndex = operand.BandIndex;
        product.PresentationIndex = operand.PresentationIndex;
        product.MinTotalPriority = operand.MinTotalPriority;
        product.MaxTotalPriority = operand.MaxTotalPriority;
        product.OBJMode = operand.OBJMode;
        product.OAMMask[0] = operand.OAMMask[0];
        product.OAMMask[1] = operand.OAMMask[1];
        product.SourceGeneration = inputs.SourceGeneration;
        product.RowEpochIdentity = inputs.RowEpochIdentity;
        product.ValidYStart = inputs.YStart;
        product.ValidYEnd = inputs.YEnd;
        product.Lifetime = LifetimeForScope(inputs.YStart, inputs.YEnd);

        switch (operand.Kind)
        {
        case WholeSceneOBJOperandKind::OrdinaryBand:
            product.Kind = WholeSceneCompositorInputKind::OrdinaryOBJBand;
            product.CoverageAuthority =
                WholeSceneCompositorCoverageAuthority::ReconstructedRGBA;
            product.PresentationKind =
                WholeSceneCompositorPresentationKind::ReconstructedSurface;
            product.CapabilityMask =
                CompositorInputCapabilityReconstructedCoverage;
            product.EffectRole =
                WholeSceneCompositorEffectRole::OrderedPresentationOperand;
            if (!inputs.Capabilities.OrdinaryOBJBand ||
                operand.BandIndex < 0 || operand.PresentationIndex < 0)
            {
                recipe.RejectionMask |=
                    inputs.Capabilities.OrdinaryOBJBand
                        ? CompositorRecipeRejectInvalidOperand
                        : CompositorRecipeRejectOrdinaryBand;
            }
            break;
        case WholeSceneOBJOperandKind::AffinePartition:
            product.Kind = WholeSceneCompositorInputKind::AffineOBJGroup;
            product.CoverageAuthority =
                WholeSceneCompositorCoverageAuthority::AffinePlacement;
            product.PresentationKind = inputs.AffineOBJSubpixel2x
                ? WholeSceneCompositorPresentationKind::AffineSubpixel2x
                : WholeSceneCompositorPresentationKind::AffineOutputGrid;
            product.CapabilityMask = CompositorInputCapabilityAffineRaster;
            product.EffectRole =
                WholeSceneCompositorEffectRole::OrderedPresentationOperand;
            if (!inputs.Capabilities.AffineOBJGroup ||
                (operand.OAMMask[0] | operand.OAMMask[1]) == 0)
            {
                recipe.RejectionMask |=
                    inputs.Capabilities.AffineOBJGroup
                        ? CompositorRecipeRejectInvalidOperand
                        : CompositorRecipeRejectAffineGroup;
            }
            break;
        case WholeSceneOBJOperandKind::SemiTransparentOBJ:
            product.Kind =
                WholeSceneCompositorInputKind::SemiTransparentOBJ;
            product.CoverageAuthority =
                WholeSceneCompositorCoverageAuthority::NativeSemanticEffect;
            product.PresentationKind =
                inputs.AffineOBJSubpixel2x &&
                    operand.PresentationIndex < 0
                    ? WholeSceneCompositorPresentationKind::
                          DSSpecialEffectSubpixel2x
                    : WholeSceneCompositorPresentationKind::DSSpecialEffect;
            product.CapabilityMask =
                CompositorInputCapabilityDSSpecialEffect;
            product.EffectRole =
                WholeSceneCompositorEffectRole::OrderedSpecialEffectOperand;
            recipe.EffectOwner =
                WholeSceneCompositorEffectOwner::SplitSemanticAndOrdered;
            if (!inputs.Capabilities.SemiTransparentOBJ ||
                operand.PresentationIndex < -1 ||
                (operand.OAMMask[0] | operand.OAMMask[1]) == 0)
            {
                recipe.RejectionMask |=
                    inputs.Capabilities.SemiTransparentOBJ
                        ? CompositorRecipeRejectInvalidOperand
                        : CompositorRecipeRejectSemiTransparentOBJ;
            }
            break;
        default:
            recipe.RejectionMask |= CompositorRecipeRejectInvalidOperand;
            continue;
        }
        AddProduct(recipe, product);
    }

    if (!WholeSceneCompositorRecipeProductsCoverScope(recipe))
        recipe.RejectionMask |= CompositorRecipeRejectInvalidLifetime;
    if (recipe.EffectState.StateHash == 0)
        recipe.RejectionMask |= CompositorRecipeRejectInvalidEffectState;

    u64 hash = kFNVOffset;
    HashValue(hash, static_cast<u64>(recipe.Kind));
    HashValue(hash, recipe.OperandPlanHash);
    HashValue(hash, recipe.DependencyRecipeHash);
    HashValue(hash, recipe.SourceGeneration);
    HashValue(hash, recipe.RowEpochIdentity);
    HashValue(hash, static_cast<u64>(recipe.YStart));
    HashValue(hash, static_cast<u64>(recipe.YEnd));
    HashValue(hash, static_cast<u64>(recipe.OutputScale));
    HashValue(hash, recipe.EffectState.StateHash);
    HashValue(hash, static_cast<u64>(recipe.EffectOwner));
    HashValue(hash, recipe.RejectionMask);
    HashValue(hash, recipe.Inputs.size());
    for (const auto& product : recipe.Inputs)
        HashValue(hash, product.ProductIdentity);
    recipe.RecipeHash = hash;
    recipe.Ready = recipe.RejectionMask == CompositorRecipeRejectNone;
    return recipe;
}

WholeSceneOperandExcludedOverlayRecipe
BuildWholeSceneOperandExcludedOverlayRecipe(
    const WholeSceneCompositorRecipe& orderedPresentation,
    const WholeSceneOperandExcludedOverlayInputs& inputs)
{
    WholeSceneOperandExcludedOverlayRecipe recipe;
    recipe.Requested = inputs.Requested;
    recipe.SemanticBaseRecipeHash =
        orderedPresentation.DependencyRecipeHash;
    recipe.OrderedPresentationRecipeHash =
        orderedPresentation.RecipeHash;
    recipe.RowEpochIdentity = orderedPresentation.RowEpochIdentity;
    recipe.YStart = inputs.YStart;
    recipe.YEnd = inputs.YEnd;
    recipe.OutputScale = inputs.OutputScale;
    recipe.FirstExcludedOperand = orderedPresentation.Inputs.size();
    recipe.EffectState = orderedPresentation.EffectState;

    if (!inputs.OrderedProductsAvailable ||
        !orderedPresentation.Ready ||
        orderedPresentation.Kind !=
            WholeSceneCompositorRecipeKind::OrderedPresentation ||
        orderedPresentation.Inputs.empty() ||
        orderedPresentation.DependencyRecipeHash == 0)
    {
        recipe.RejectionMask |= OperandExcludedOverlayRejectOrderedRecipe;
    }
    if (!inputs.Direct3D)
        recipe.RejectionMask |= OperandExcludedOverlayRejectNoDirect3D;
    if (inputs.AffineBG)
        recipe.RejectionMask |= OperandExcludedOverlayRejectAffineBG;
    if (inputs.YStart != 0 || inputs.YEnd != 192 || inputs.OutputScale < 1)
        recipe.RejectionMask |= OperandExcludedOverlayRejectRange;
    if (inputs.WindowActive)
        recipe.RejectionMask |= OperandExcludedOverlayRejectWindow;
    if (inputs.EffectActive)
        recipe.RejectionMask |= OperandExcludedOverlayRejectEffect;

    bool sawAffineOperand = false;
    for (std::size_t inputIndex = 1;
         inputIndex < orderedPresentation.Inputs.size();
         inputIndex++)
    {
        const auto& operand = orderedPresentation.Inputs[inputIndex];
        if (operand.Kind == WholeSceneCompositorInputKind::AffineOBJGroup)
        {
            if (!sawAffineOperand)
                recipe.FirstExcludedOperand = inputIndex - 1;
            sawAffineOperand = true;
        }
        else if (operand.Kind ==
                 WholeSceneCompositorInputKind::SemiTransparentOBJ)
        {
            recipe.RejectionMask |=
                OperandExcludedOverlayRejectSpecialOBJ;
        }

        if (sawAffineOperand)
        {
            recipe.ExcludedOAMMask[0] |= operand.OAMMask[0];
            recipe.ExcludedOAMMask[1] |= operand.OAMMask[1];
        }
    }
    if (!sawAffineOperand)
        recipe.RejectionMask |= OperandExcludedOverlayRejectNoAffineOperand;
    if (recipe.FirstExcludedOperand >=
        orderedPresentation.Inputs.size() - 1)
    {
        recipe.RejectionMask |= OperandExcludedOverlayRejectOrdering;
    }

    u64 hash = kFNVOffset;
    HashValue(hash, static_cast<u64>(
        WholeSceneCompositorRecipeKind::OperandExcludedOverlayBase));
    HashValue(hash, recipe.SemanticBaseRecipeHash);
    HashValue(hash, recipe.OrderedPresentationRecipeHash);
    HashValue(hash, recipe.RowEpochIdentity);
    HashValue(hash, static_cast<u64>(recipe.YStart));
    HashValue(hash, static_cast<u64>(recipe.YEnd));
    HashValue(hash, static_cast<u64>(recipe.OutputScale));
    HashValue(hash, recipe.EffectState.StateHash);
    HashValue(hash, recipe.FirstExcludedOperand);
    HashValue(hash, recipe.ExcludedOAMMask[0]);
    HashValue(hash, recipe.ExcludedOAMMask[1]);
    HashValue(hash, recipe.RejectionMask);
    recipe.RecipeHash = hash;
    recipe.Ready = recipe.Requested &&
        recipe.RejectionMask == OperandExcludedOverlayRejectNone;
    return recipe;
}

WholeSceneCompositorRecipe BuildWholeSceneSemanticCompositorRecipe(
    const WholeSceneSemanticCompositorRecipeInputs& inputs,
    int inputLimit)
{
    WholeSceneCompositorRecipe recipe;
    recipe.Kind = WholeSceneCompositorRecipeKind::SemanticResolve;
    recipe.YStart = inputs.YStart;
    recipe.YEnd = inputs.YEnd;
    recipe.OutputScale = inputs.OutputScale;
    recipe.SourceGeneration = inputs.OBJSourceGeneration;
    recipe.RowEpochIdentity = inputs.RowEpochIdentity;
    recipe.EffectState = CanonicalEffectState(inputs.EffectState);
    recipe.EffectOwner =
        WholeSceneCompositorEffectOwner::SemanticResolver;

    const u32 enabledBGMask = inputs.EnabledBGMask & 0xFu;
    const u32 direct3DBGMask = inputs.Direct3DBGMask & 0xFu;
    const u32 enhancedBGMask = inputs.EnhancedBGMask & 0xFu;
    const u32 reconstructedCoverageMask =
        inputs.ReconstructedBGCoverageMask & 0xFu;
    if (inputs.YStart < 0 || inputs.YEnd <= inputs.YStart ||
        inputs.YEnd > 192 || inputs.OutputScale < 1)
    {
        recipe.RejectionMask |= CompositorRecipeRejectInvalidScope;
    }
    if ((inputs.Direct3DBGMask & ~enabledBGMask) != 0 ||
        (inputs.EnhancedBGMask & ~enabledBGMask) != 0 ||
        (inputs.ReconstructedBGCoverageMask & ~enhancedBGMask) != 0 ||
        (direct3DBGMask & enhancedBGMask) != 0)
    {
        recipe.RejectionMask |= CompositorRecipeRejectInvalidSemanticMask;
    }
    if (inputs.RowEpochIdentity == 0)
        recipe.RejectionMask |= CompositorRecipeRejectInvalidRowEpoch;
    const u32 missingBGMask =
        (enabledBGMask & ~direct3DBGMask) &
        ~inputs.BGProductsAvailableMask;
    if (missingBGMask != 0)
        recipe.RejectionMask |= CompositorRecipeRejectLogicalBG;
    if (direct3DBGMask != 0 && !inputs.Direct3DProductAvailable)
        recipe.RejectionMask |= CompositorRecipeRejectDirect3D;
    if (inputs.OBJEnabled && !inputs.OBJProductAvailable)
        recipe.RejectionMask |= CompositorRecipeRejectSemanticOBJ;

    for (int layer = 0; layer < 4; layer++)
    {
        const u32 layerBit = 1u << layer;
        if ((enabledBGMask & layerBit) == 0)
            continue;

        WholeSceneCompositorInputProduct product;
        product.Stage =
            WholeSceneCompositorInputStage::SemanticCompositorInput;
        product.ProductIndex = layer;
        product.SourceGeneration = inputs.BGSourceGeneration[layer];
        product.RowEpochIdentity = inputs.RowEpochIdentity;
        product.CapabilityMask = CompositorInputCapabilitySemanticLayer;
        product.ValidYStart = inputs.YStart;
        product.ValidYEnd = inputs.YEnd;
        product.Lifetime = LifetimeForScope(inputs.YStart, inputs.YEnd);
        product.EffectRole =
            WholeSceneCompositorEffectRole::SemanticEffectOperand;
        if ((direct3DBGMask & layerBit) != 0)
        {
            product.Kind = WholeSceneCompositorInputKind::Direct3D;
            product.CoordinateDomain =
                WholeSceneCompositorCoordinateDomain::OutputPresentation;
            product.CoverageAuthority =
                WholeSceneCompositorCoverageAuthority::NativeSemanticOwnership;
        }
        else
        {
            product.Kind = WholeSceneCompositorInputKind::LogicalBG;
            product.CoordinateDomain =
                WholeSceneCompositorCoordinateDomain::NativeLogical;
            product.CoverageAuthority =
                (reconstructedCoverageMask & layerBit) != 0
                    ? WholeSceneCompositorCoverageAuthority::ReconstructedRGBA
                    : WholeSceneCompositorCoverageAuthority::NativeSemanticOwnership;
            if ((enhancedBGMask & layerBit) != 0)
            {
                product.CapabilityMask |=
                    CompositorInputCapabilityEnhancedSource;
            }
            if ((reconstructedCoverageMask & layerBit) != 0)
            {
                product.CapabilityMask |=
                    CompositorInputCapabilityReconstructedCoverage;
            }
        }
        AddProduct(recipe, product);
    }

    if (inputs.OBJEnabled)
    {
        WholeSceneCompositorInputProduct product;
        product.Kind = inputs.ResolvedOBJProduct
            ? WholeSceneCompositorInputKind::ResolvedOBJSurface
            : WholeSceneCompositorInputKind::SemanticOBJSurface;
        product.Stage =
            WholeSceneCompositorInputStage::SemanticCompositorInput;
        product.CoordinateDomain =
            WholeSceneCompositorCoordinateDomain::OutputPresentation;
        product.CoverageAuthority = inputs.ResolvedOBJProduct
            ? WholeSceneCompositorCoverageAuthority::ResolvedOAM
            : WholeSceneCompositorCoverageAuthority::NativeSemanticOwnership;
        product.CapabilityMask = CompositorInputCapabilitySemanticLayer;
        if (inputs.ResolvedOBJProduct)
        {
            product.CapabilityMask |=
                CompositorInputCapabilityResolvedOAM |
                CompositorInputCapabilityReconstructedCoverage;
        }
        product.ProductIndex = 4;
        product.SourceGeneration = inputs.OBJSourceGeneration;
        product.RowEpochIdentity = inputs.RowEpochIdentity;
        product.ValidYStart = inputs.YStart;
        product.ValidYEnd = inputs.YEnd;
        product.Lifetime = LifetimeForScope(inputs.YStart, inputs.YEnd);
        product.EffectRole =
            WholeSceneCompositorEffectRole::SemanticEffectOperand;
        AddProduct(recipe, product);
    }

    if (inputLimit < 0 ||
        recipe.Inputs.size() > static_cast<std::size_t>(inputLimit))
    {
        recipe.RejectionMask |= CompositorRecipeRejectInputLimit;
    }
    if (!WholeSceneCompositorRecipeProductsCoverScope(recipe))
        recipe.RejectionMask |= CompositorRecipeRejectInvalidLifetime;
    if (recipe.EffectState.StateHash == 0)
        recipe.RejectionMask |= CompositorRecipeRejectInvalidEffectState;

    u64 hash = kFNVOffset;
    HashValue(hash, static_cast<u64>(recipe.Kind));
    HashValue(hash, static_cast<u64>(recipe.YStart));
    HashValue(hash, static_cast<u64>(recipe.YEnd));
    HashValue(hash, static_cast<u64>(recipe.OutputScale));
    HashValue(hash, recipe.RowEpochIdentity);
    HashValue(hash, recipe.EffectState.StateHash);
    HashValue(hash, static_cast<u64>(recipe.EffectOwner));
    HashValue(hash, recipe.RejectionMask);
    HashValue(hash, recipe.Inputs.size());
    for (const auto& product : recipe.Inputs)
        HashValue(hash, product.ProductIdentity);
    recipe.RecipeHash = hash;
    recipe.Ready = recipe.RejectionMask == CompositorRecipeRejectNone;
    return recipe;
}

u32 WholeSceneCompositorRecipeEnhancedBGMask(
    const WholeSceneCompositorRecipe& recipe)
{
    u32 mask = 0;
    for (const auto& product : recipe.Inputs)
    {
        if (product.Kind == WholeSceneCompositorInputKind::LogicalBG &&
            product.ProductIndex >= 0 && product.ProductIndex < 4 &&
            (product.CapabilityMask &
             CompositorInputCapabilityEnhancedSource) != 0)
        {
            mask |= 1u << product.ProductIndex;
        }
    }
    return mask;
}

u32 WholeSceneCompositorRecipeReconstructedBGCoverageMask(
    const WholeSceneCompositorRecipe& recipe)
{
    u32 mask = 0;
    for (const auto& product : recipe.Inputs)
    {
        if (product.Kind == WholeSceneCompositorInputKind::LogicalBG &&
            product.ProductIndex >= 0 && product.ProductIndex < 4 &&
            product.CoverageAuthority ==
                WholeSceneCompositorCoverageAuthority::ReconstructedRGBA)
        {
            mask |= 1u << product.ProductIndex;
        }
    }
    return mask;
}

bool WholeSceneCompositorRecipeHasInput(
    const WholeSceneCompositorRecipe& recipe,
    WholeSceneCompositorInputKind kind)
{
    for (const auto& product : recipe.Inputs)
    {
        if (product.Kind == kind)
            return true;
    }
    return false;
}

u32 WholeSceneCompositorRecipePresentationCount(
    const WholeSceneCompositorRecipe& recipe,
    WholeSceneCompositorPresentationKind kind)
{
    u32 count = 0;
    for (const auto& product : recipe.Inputs)
    {
        if (product.PresentationKind == kind)
            count++;
    }
    return count;
}

}
