// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "types.h"
#include "WholeSceneOBJBandPolicy.h"

namespace melonDS
{

enum class WholeSceneCompositorRecipeKind : u8
{
    OrderedPresentation = 0,
    SemanticResolve = 1,
    OperandExcludedOverlayBase = 2,
};

// Value-only descriptions of products admitted to a compositor recipe.  They
// deliberately contain no renderer handles: specialized caches own storage,
// while this contract owns product meaning, order and compatibility.
enum class WholeSceneCompositorInputKind : u8
{
    CompletedBG3DBase = 0,
    LogicalBG = 1,
    Direct3D = 2,
    OrdinaryOBJBand = 3,
    AffineOBJGroup = 4,
    SemiTransparentOBJ = 5,
    SemanticOBJSurface = 6,
    ResolvedOBJSurface = 7,
};

enum class WholeSceneCompositorInputStage : u8
{
    CompletedSceneBase = 0,
    PresentationOperand = 1,
    SemanticCompositorInput = 2,
};

enum class WholeSceneCompositorCoordinateDomain : u8
{
    NativeLogical = 0,
    OutputPresentation = 1,
};

enum class WholeSceneCompositorCoverageAuthority : u8
{
    CompletedScene = 0,
    ReconstructedRGBA = 1,
    AffinePlacement = 2,
    NativeSemanticEffect = 3,
    NativeSemanticOwnership = 4,
    ResolvedOAM = 5,
};

// The representation an ordered operand is expected to consume. Coverage
// authority alone cannot distinguish ordinary output-grid affine placement
// from a real subpixel presentation product.
enum class WholeSceneCompositorPresentationKind : u8
{
    None = 0,
    CompletedScene = 1,
    ReconstructedSurface = 2,
    AffineOutputGrid = 3,
    AffineSubpixel2x = 4,
    DSSpecialEffect = 5,
    DSSpecialEffectSubpixel2x = 6,
};

enum class WholeSceneCompositorLifetimeKind : u8
{
    ScanlineRange = 0,
    FullFrameSnapshot = 1,
};

enum class WholeSceneCompositorEffectRole : u8
{
    None = 0,
    EffectsResolvedInProduct = 1,
    SemanticEffectOperand = 2,
    OrderedPresentationOperand = 3,
    OrderedSpecialEffectOperand = 4,
};

enum class WholeSceneCompositorEffectOwner : u8
{
    None = 0,
    SemanticResolver = 1,
    OrderedResolver = 2,
    SplitSemanticAndOrdered = 3,
};

struct WholeSceneCompositorEffectState
{
    u64 ChannelMask = 0;
    u16 BlendCnt = 0;
    u8 EVA = 0;
    u8 EVB = 0;
    u8 EVY = 0;
    u8 SemiTransparentOBJModeMask = 0;
    u64 StateHash = 0;
};

enum WholeSceneCompositorInputCapability : u32
{
    CompositorInputCapabilityNone = 0,
    CompositorInputCapabilityDestination = 1u << 0,
    CompositorInputCapabilityReconstructedCoverage = 1u << 1,
    CompositorInputCapabilityAffineRaster = 1u << 2,
    CompositorInputCapabilityDSSpecialEffect = 1u << 3,
    CompositorInputCapabilitySemanticLayer = 1u << 4,
    CompositorInputCapabilityEnhancedSource = 1u << 5,
    CompositorInputCapabilityResolvedOAM = 1u << 6,
};

struct WholeSceneCompositorInputProduct
{
    WholeSceneCompositorInputKind Kind =
        WholeSceneCompositorInputKind::CompletedBG3DBase;
    WholeSceneCompositorInputStage Stage =
        WholeSceneCompositorInputStage::CompletedSceneBase;
    WholeSceneCompositorCoordinateDomain CoordinateDomain =
        WholeSceneCompositorCoordinateDomain::OutputPresentation;
    WholeSceneCompositorCoverageAuthority CoverageAuthority =
        WholeSceneCompositorCoverageAuthority::CompletedScene;
    WholeSceneCompositorPresentationKind PresentationKind =
        WholeSceneCompositorPresentationKind::None;
    u32 CapabilityMask = CompositorInputCapabilityNone;
    int ProductIndex = -1;
    int PresentationIndex = -1;
    int MinTotalPriority = -1;
    int MaxTotalPriority = -1;
    u32 OBJMode = 0;
    u64 OAMMask[2] {};
    u64 SourceGeneration = 0;
    // Identity of the exact ordered scanline-state sequence used to produce
    // this range. SourceGeneration covers art; RowEpochIdentity covers how
    // that art is interpreted and composed on each row.
    u64 RowEpochIdentity = 0;
    u64 ProductIdentity = 0;
    int ValidYStart = 0;
    int ValidYEnd = 0;
    WholeSceneCompositorLifetimeKind Lifetime =
        WholeSceneCompositorLifetimeKind::ScanlineRange;
    WholeSceneCompositorEffectRole EffectRole =
        WholeSceneCompositorEffectRole::None;
};

enum WholeSceneCompositorRecipeRejection : u32
{
    CompositorRecipeRejectNone = 0,
    CompositorRecipeRejectOperandPlan = 1u << 0,
    CompositorRecipeRejectCompletedBase = 1u << 1,
    CompositorRecipeRejectOrdinaryBand = 1u << 2,
    CompositorRecipeRejectAffineGroup = 1u << 3,
    CompositorRecipeRejectSemiTransparentOBJ = 1u << 4,
    CompositorRecipeRejectInvalidOperand = 1u << 5,
    CompositorRecipeRejectInputLimit = 1u << 6,
    CompositorRecipeRejectLogicalBG = 1u << 7,
    CompositorRecipeRejectDirect3D = 1u << 8,
    CompositorRecipeRejectSemanticOBJ = 1u << 9,
    CompositorRecipeRejectInvalidScope = 1u << 10,
    CompositorRecipeRejectInvalidSemanticMask = 1u << 11,
    CompositorRecipeRejectInvalidLifetime = 1u << 12,
    CompositorRecipeRejectInvalidEffectState = 1u << 13,
    CompositorRecipeRejectInvalidRowEpoch = 1u << 14,
};

struct WholeSceneCompositorRecipeCapabilities
{
    bool CompletedBase = false;
    bool OrdinaryOBJBand = false;
    bool AffineOBJGroup = false;
    bool SemiTransparentOBJ = false;
};

struct WholeSceneCompositorRecipeInputs
{
    WholeSceneCompositorRecipeCapabilities Capabilities;
    bool AffineOBJSubpixel2x = false;
    u64 CompletedBaseIdentity = 0;
    // The ordered presentation is only meaningful over this exact semantic
    // base recipe.  Keeping the dependency separate from the storage/product
    // identity lets OutputPlan validate composition order directly.
    u64 CompletedBaseRecipeHash = 0;
    u64 SourceGeneration = 0;
    u64 RowEpochIdentity = 0;
    int YStart = 0;
    int YEnd = 192;
    int OutputScale = 1;
    WholeSceneCompositorEffectState EffectState;
};

struct WholeSceneSemanticCompositorRecipeInputs
{
    int YStart = 0;
    int YEnd = 192;
    int OutputScale = 1;
    u32 EnabledBGMask = 0;
    u32 BGProductsAvailableMask = 0;
    u32 Direct3DBGMask = 0;
    u32 EnhancedBGMask = 0;
    u32 ReconstructedBGCoverageMask = 0;
    bool Direct3DProductAvailable = false;
    bool OBJEnabled = false;
    bool OBJProductAvailable = false;
    bool ResolvedOBJProduct = false;
    u64 BGSourceGeneration[4] {};
    u64 OBJSourceGeneration = 0;
    u64 RowEpochIdentity = 0;
    WholeSceneCompositorEffectState EffectState;
};

struct WholeSceneCompositorRecipe
{
    WholeSceneCompositorRecipeKind Kind =
        WholeSceneCompositorRecipeKind::OrderedPresentation;
    bool Ready = false;
    u32 RejectionMask = CompositorRecipeRejectNone;
    u64 OperandPlanHash = 0;
    u64 DependencyRecipeHash = 0;
    u64 SourceGeneration = 0;
    u64 RowEpochIdentity = 0;
    u64 RecipeHash = 0;
    int YStart = 0;
    int YEnd = 0;
    int OutputScale = 1;
    WholeSceneCompositorEffectState EffectState;
    WholeSceneCompositorEffectOwner EffectOwner =
        WholeSceneCompositorEffectOwner::None;
    // OrderedPresentation uses Inputs[0] as the completed destination and the
    // remainder in back-to-front order. SemanticResolve supplies an unordered
    // set whose priority and top/second ownership are resolved per pixel.
    std::vector<WholeSceneCompositorInputProduct> Inputs;
};

enum WholeSceneOperandExcludedOverlayRejection : u32
{
    OperandExcludedOverlayRejectNone = 0,
    OperandExcludedOverlayRejectOrderedRecipe = 1u << 0,
    OperandExcludedOverlayRejectNoDirect3D = 1u << 1,
    OperandExcludedOverlayRejectAffineBG = 1u << 2,
    OperandExcludedOverlayRejectRange = 1u << 3,
    OperandExcludedOverlayRejectWindow = 1u << 4,
    OperandExcludedOverlayRejectEffect = 1u << 5,
    OperandExcludedOverlayRejectOrdering = 1u << 6,
    OperandExcludedOverlayRejectSpecialOBJ = 1u << 7,
    OperandExcludedOverlayRejectNoAffineOperand = 1u << 8,
};

struct WholeSceneOperandExcludedOverlayInputs
{
    bool Requested = false;
    bool OrderedProductsAvailable = true;
    bool Direct3D = false;
    bool AffineBG = false;
    bool WindowActive = false;
    bool EffectActive = false;
    int YStart = 0;
    int YEnd = 192;
    int OutputScale = 1;
};

// Describes the native overlay base that removes the ordered suffix beginning
// at FirstExcludedOperand.  The base is then the destination of that exact
// suffix; it is not an independently interchangeable whole-scene image.
struct WholeSceneOperandExcludedOverlayRecipe
{
    bool Requested = false;
    bool Ready = false;
    u32 RejectionMask = OperandExcludedOverlayRejectNone;
    u64 SemanticBaseRecipeHash = 0;
    u64 OrderedPresentationRecipeHash = 0;
    u64 RowEpochIdentity = 0;
    u64 RecipeHash = 0;
    int YStart = 0;
    int YEnd = 0;
    int OutputScale = 1;
    std::size_t FirstExcludedOperand = 0;
    u64 ExcludedOAMMask[2] {};
    WholeSceneCompositorEffectState EffectState;
};

// Ordinary sprites must not change reconstruction when unrelated affine
// geometry starts or stops moving. Planning and rendering share this policy.
inline bool ShouldResolveOrdinaryOBJSurface(
    bool objVisible, bool hasOrdinaryOBJ, bool scalingEnabled,
    bool activeOBJWindow, int ystart, int yend)
{
    return objVisible && hasOrdinaryOBJ && scalingEnabled &&
        !activeOBJWindow && ystart == 0 && yend == 192;
}

WholeSceneCompositorEffectState BuildWholeSceneCompositorEffectState(
    u64 channelMask,
    u16 blendCnt,
    u8 eva,
    u8 evb,
    u8 evy,
    u8 semiTransparentOBJModeMask);

bool WholeSceneCompositorRecipeProductsCoverScope(
    const WholeSceneCompositorRecipe& recipe);

WholeSceneCompositorRecipe BuildWholeSceneOBJCompositorRecipe(
    const WholeSceneOBJOperandPlan& operandPlan,
    const WholeSceneCompositorRecipeInputs& inputs,
    int inputLimit = WholeSceneOBJOperandLimit + 1);

WholeSceneCompositorRecipe BuildWholeSceneSemanticCompositorRecipe(
    const WholeSceneSemanticCompositorRecipeInputs& inputs,
    int inputLimit = 5);

WholeSceneOperandExcludedOverlayRecipe
BuildWholeSceneOperandExcludedOverlayRecipe(
    const WholeSceneCompositorRecipe& orderedPresentation,
    const WholeSceneOperandExcludedOverlayInputs& inputs);

u32 WholeSceneCompositorRecipeEnhancedBGMask(
    const WholeSceneCompositorRecipe& recipe);

u32 WholeSceneCompositorRecipeReconstructedBGCoverageMask(
    const WholeSceneCompositorRecipe& recipe);

bool WholeSceneCompositorRecipeHasInput(
    const WholeSceneCompositorRecipe& recipe,
    WholeSceneCompositorInputKind kind);

u32 WholeSceneCompositorRecipePresentationCount(
    const WholeSceneCompositorRecipe& recipe,
    WholeSceneCompositorPresentationKind kind);

}
