// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

#include "WholeSceneCompositorInput.h"
#include "WholeSceneScalePolicy.h"

namespace melonDS
{

// The OutputPlan owns immutable selection, admission, execution dispatch and
// transform intent. Runtime transform completion and observation live in the
// execution trace.
constexpr u32 WholeSceneOutputPlanVersion = 46;

enum class WholeSceneCompositorFallbackPolicy : u8
{
    None = 0,
    PreparedRecipe = 1,
    PreparedRecipeOrStrictCandidateBindings = 2,
};

enum class WholeSceneCompositorExecutionDisposition : u8
{
    NotEvaluated = 0,
    PlannedRecipe = 1,
    PreparedRecipeFallback = 2,
    StrictCandidateBindingsFallback = 3,
    Unavailable = 4,
};

enum class WholeSceneCompositorExecutionReason : u8
{
    NotEvaluated = 0,
    PlannedRecipeMatched = 1,
    PlannedRecipeNotBound = 2,
    PreparedRecipeMismatch = 3,
    PreparedRecipeRejected = 4,
    FallbackNotAuthorized = 5,
    PreparedProductsUnavailable = 6,
    DependencyMismatch = 7,
    RenderFailure = 8,
};

enum class WholeSceneStrictAffinePresentationKind : u8
{
    NotEvaluated = 0,
    OrderedOperands = 1,
    AtomicOrdinaryBand = 2,
    NativeStackComposite = 3,
    StrictCandidate = 4,
};

enum class WholeSceneStrictAffinePresentationReason : u8
{
    NotEvaluated = 0,
    OrderedProductsReady = 1,
    AtomicBandProductsReady = 2,
    NativeStackReady = 3,
    NoPresentationProductsReady = 4,
};

enum class WholeSceneOutputPlanRecipeBindingReason : u8
{
    NotEvaluated = 0,
    Bound = 1,
    InvalidPlan = 2,
    WrongPath = 3,
    RecipeRejected = 4,
    ScopeMismatch = 5,
    ScaleMismatch = 6,
    DependencyMissing = 7,
    DependencyMismatch = 8,
};

enum class WholeSceneOutputPlanDecisionSource : u8
{
    None,
    PathDecision,
    ScaleDecision,
};

// A bounded coarse execution shape selected by OutputPlan. Render paths still
// implement the pixels; this record owns how the selected work is dispatched.
enum class WholeSceneOutputExecutionKind : u8
{
    None = 0,
    Current = 1,
    SelectedRepresentation = 2,
    CaptureBackedPlan = 3,
    PhysicalFinalPostprocessNativeInput = 5,
};

enum class WholeSceneOutputInsertionStage : u8
{
    UnclassifiedLegacy,
    SourceModel,
    HighResComposition,
    CompositorProductUpscale,
    DisplayProductUpscale,
    NativeEnhancedPracticalFloor,
    NativeExactFloor,
};

enum class WholeSceneOutputCorrectnessClass : u8
{
    Unclassified,
    NativeExact,
    NativeEnhanced,
    ModelCorrectHighRes,
    HeuristicHighRes,
};

enum class WholeSceneOutputAdmissionReason : u8
{
    NotEvaluated,
    SelectedRepresentationAdmitted,
    // Transitional parity retention is not a capability admission. The
    // selected representation remains active only to preserve the qualified
    // renderer baseline while EvidenceUnavailableChannels records the debt.
    SelectedRepresentationRetainedForParityWithEvidenceDebt,
    UnsupportedChannelsDemotedToFinalNative,
    UnsupportedChannelsDemotedToNativeStack,
    UnsupportedChannelsNoFallback,
    UnsupportedChannelsDemotedToNativeExactFloor,
};

enum class WholeSceneOutputRepresentationKind : u8
{
    None,
    Current,
    NativeStack,
    HighResCompositor,
    FinalNative,
    OverlayOperator,
    ConservativeHybrid,
    CaptureBackedHandoff,
    SourceACaptureReplacement,
    CaptureEpochOverlay,
    PhysicalFinalPostprocessInput,
    CurrentVRAMDisplay,
    NativeExactFloor,
    StrictAffineHighRes,
};

constexpr u32 WholeSceneOutputRepresentationBit(WholeSceneOutputRepresentationKind kind)
{
    return kind == WholeSceneOutputRepresentationKind::None
        ? 0u
        : 1u << (static_cast<u32>(kind) - 1u);
}

enum WholeSceneOutputRepresentationCapability : u32
{
    OutputRepresentationCapabilityNone = 0,
    OutputRepresentationCapabilityCompletedScene = 1u << 0,
    OutputRepresentationCapabilityTerminalPresentation = 1u << 1,
    OutputRepresentationCapabilityNativeSemanticOwnership = 1u << 2,
    OutputRepresentationCapabilityEndpointReconstruction = 1u << 3,
    OutputRepresentationCapabilitySelectorResolved = 1u << 4,
    OutputRepresentationCapabilityHighResGeometry = 1u << 5,
    OutputRepresentationCapabilityNativeExact = 1u << 6,
};

// Value-only contract for a completed output candidate. It is deliberately
// separate from CompositorInputProduct: terminal Native Stack and Endpoint
// Overlay results may be selected for presentation, but may never masquerade
// as unresolved semantic layers in a later compositor recipe.
struct WholeSceneOutputRepresentationProduct
{
    bool Valid = false;
    WholeSceneOutputRepresentationKind Kind =
        WholeSceneOutputRepresentationKind::None;
    bool TerminalCompleted = false;
    bool CompositorInputEligible = false;
    WholeSceneCompositorCoordinateDomain CoordinateDomain =
        WholeSceneCompositorCoordinateDomain::OutputPresentation;
    WholeSceneCompositorCoverageAuthority CoverageAuthority =
        WholeSceneCompositorCoverageAuthority::CompletedScene;
    u32 CapabilityMask = OutputRepresentationCapabilityNone;
    int ValidYStart = 0;
    int ValidYEnd = 0;
    WholeSceneCompositorLifetimeKind Lifetime =
        WholeSceneCompositorLifetimeKind::ScanlineRange;
    int OutputScale = 1;
    WholeSceneOutputInsertionStage InsertionStage =
        WholeSceneOutputInsertionStage::UnclassifiedLegacy;
    WholeSceneOutputCorrectnessClass CorrectnessClass =
        WholeSceneOutputCorrectnessClass::Unclassified;
    u64 SourceGeneration = 0;
    u64 RowEpochIdentity = 0;
    u64 ProductIdentity = 0;
};

enum class WholeSceneOutputSelectorKind : u8
{
    None,
    OverlayOperator,
    ConservativeHybrid,
};

enum class WholeSceneOutputChannel : u64
{
    TextBGGeometry = 1ull << 0,
    AffineBGGeometry = 1ull << 1,
    Direct3D = 1ull << 2,
    OBJGeometry = 1ull << 3,
    AffineOBJGeometry = 1ull << 4,
    Priority = 1ull << 5,
    Windows = 1ull << 6,
    OBJWindow = 1ull << 7,
    Mosaic = 1ull << 8,
    Palette = 1ull << 9,
    ColorEffect = 1ull << 10,
    AlphaBlend = 1ull << 11,
    SemiTransparentOBJ = 1ull << 12,
    DisplayCapture = 1ull << 13,
    CaptureFeedback = 1ull << 14,
    VRAMDisplay = 1ull << 15,
    DisplaySelection = 1ull << 16,
    MasterBrightness = 1ull << 17,
    PhysicalRouting = 1ull << 18,
};

constexpr u64 WholeSceneOutputAllChannelMask = (1ull << 19) - 1ull;

enum class WholeSceneOutputTransformKind : u8
{
    None,
    CompositorColorEffect,
    DisplayCapture,
    DisplaySelection,
    MasterBrightness,
    PhysicalRouting,
    PhysicalPresentationBrightness,
};

constexpr u32 WholeSceneOutputTransformBit(WholeSceneOutputTransformKind kind)
{
    return kind == WholeSceneOutputTransformKind::None
        ? 0u
        : 1u << (static_cast<u32>(kind) - 1u);
}

enum class WholeScenePhysicalTarget : u8
{
    Unknown,
    Top,
    Bottom,
    Mixed,
};

enum class WholeSceneCaptureInputKind : u8
{
    None,
    Output2DPresentation,
    Output3D,
    NativeEngineOutput,
    NativeResolved3D,
};

enum class WholeSceneFinalPassPurpose : u8
{
    ScaledPresentation,
    PhysicalNativeInput,
    NativeFallback,
    PhysicalPostprocessOutput,
};

constexpr u32 WholeSceneMaxOutputTransforms = 6;

struct WholeSceneOutputTransform
{
    WholeSceneOutputTransformKind Kind = WholeSceneOutputTransformKind::None;
    WholeSceneOutputInsertionStage OwningStage =
        WholeSceneOutputInsertionStage::UnclassifiedLegacy;
    int YStart = 0;
    int YEnd = 0;
    u32 State = 0;
    u32 AuxState = 0;
    WholeScenePhysicalTarget Target = WholeScenePhysicalTarget::Unknown;
};

struct WholeSceneOutputTransformExecutionTrace
{
    bool ExecutionObserved = false;
    bool LegacyApplied = false;
    int ObservedYStart = 0;
    int ObservedYEnd = 0;
    u32 ObservedState = 0;
    u32 ObservedAuxState = 0;
    WholeScenePhysicalTarget ObservedTarget = WholeScenePhysicalTarget::Unknown;
    bool PlanExecuted = false;
    bool TraceAgreement = false;
};

struct WholeSceneCaptureInputTrace
{
    bool Observed = false;
    int YStart = 0;
    int YEnd = 0;
    u32 CaptureCnt = 0;
    WholeSceneCaptureInputKind Kind = WholeSceneCaptureInputKind::None;
    WholeSceneOutputRepresentationKind PresentationRepresentation =
        WholeSceneOutputRepresentationKind::None;
    bool UsesSelectedPresentation = false;
    bool AuthoritativePreMaster = false;
    bool NativeSized = false;
    bool UsesSourceB = false;
    bool SourceBTrackedCapture = false;
    bool SourceBSameDestination = false;
    bool EventValid = false;
    u64 EventSerial = 0;
    u32 EventSourceKind = 0;
    u32 EventProductMask = 0;
    u32 EventRejectReason = 0;
};

struct WholeScenePresentationTrace
{
    bool Observed = false;
    WholeSceneFinalPassPurpose Purpose =
        WholeSceneFinalPassPurpose::ScaledPresentation;
    int YStart = 0;
    int YEnd = 0;
    u32 DisplayMode = 0;
    bool VRAMDisplayReplacement = false;
    bool MasterBrightnessAlreadyApplied = false;
    bool MasterBrightnessAppliedInFinalPass = false;
    bool MasterBrightnessExecutedByPlan = false;
    WholeScenePhysicalTarget PhysicalTarget = WholeScenePhysicalTarget::Unknown;
};

struct WholeSceneOutputSelectorPlan
{
    WholeSceneOutputSelectorKind Kind = WholeSceneOutputSelectorKind::None;
    HybridSourceDecisionReason LegacyReason = HybridSourceDecisionReason::None;
    bool Configured = false;
    bool ConservativeHybridRequested = false;
    bool EffectiveConservativeHybrid = false;
    bool OverlaySuppressedDirect3D = false;
    bool ActiveDirect3D = false;
    bool RenderNativeFallback = false;
    bool RenderForeground2DBase = false;
    bool RenderForegroundCandidate = false;
    bool WindowEdgeAssist = false;
    bool Target2AlphaBlendAssist = false;
    bool NativeEffectGuard = false;
    bool CleanLegacyCandidateEnabled = false;
    HybridCleanLegacyBlockReason CleanLegacyBlockReason =
        HybridCleanLegacyBlockReason::PathModeOrSettingUnavailable;
};

enum class WholeSceneOutputEffectOperandSource : u8
{
    None,
    NativeStackMetadata,
};

enum class WholeSceneOutputEffectProof : u32
{
    None = 0,
    ConservativeHybridAlphaBothTargets = 1u << 0,
    ConservativeHybridDirect3DDarken = 1u << 1,
    ConservativeHybridMode1OBJZeroSecond = 1u << 2,
};

// Step 4.4 separates known effect operands from proven high-resolution
// ownership. The CPU supplies stable register masks; the GPU retains per-pixel
// top/second/special-source selection in NativeMetaTex.
struct WholeSceneOutputEffectEvidence
{
    bool Available = false;
    WholeSceneOutputEffectOperandSource OperandSource =
        WholeSceneOutputEffectOperandSource::None;
    u32 ProofMask = 0;
    u64 CandidateChannelMask = 0;
    u64 ProvenOwnershipChannelMask = 0;
    u16 BlendCnt = 0;
    u8 BlendMode = 0;
    u8 Target1Mask = 0;
    u8 Target2Mask = 0;
    u8 EVA = 0;
    u8 EVB = 0;
    u8 EVY = 0;
    bool Direct3DTarget1 = false;
    bool Direct3DTarget2 = false;
    // Bit 0: OBJ mode 1 (semi-transparent palette/texture alpha).
    // Bit 1: OBJ mode 3 (bitmap alpha).
    u8 SemiTransparentOBJModeMask = 0;
    bool OBJTarget1 = false;
    bool OBJTarget2 = false;
};

struct WholeSceneOutputPlan
{
    u32 Version = WholeSceneOutputPlanVersion;
    bool Valid = false;
    int YStart = 0;
    int YEnd = 0;
    int OutputScale = 1;

    WholeSceneOutputPlanDecisionSource DecisionSource =
        WholeSceneOutputPlanDecisionSource::None;
    WholeScenePathDecisionReason PathReason = WholeScenePathDecisionReason::None;
    WholeSceneScaleDecisionReason ScaleReason = WholeSceneScaleDecisionReason::None;
    StrictAffineHighResBlockReason StrictAffineBlockReason =
        StrictAffineHighResBlockReason::FeatureDisabled;
    WholeSceneRenderPath SelectedPath = WholeSceneRenderPath::None;
    WholeSceneCurrentPathReason CurrentReason = WholeSceneCurrentPathReason::None;
    WholeSceneOutputExecutionKind ExecutionKind =
        WholeSceneOutputExecutionKind::None;
    int ExecutionYStart = 0;
    int ExecutionYEnd = 0;
    bool ExecutionCurrentFragmentationFallback = false;
    WholeSceneCaptureBackedPlan CapturePlan;
    WholeSceneCaptureRepresentationEvidence RepresentationEvidence;

    u32 ConsideredRepresentationMask = 0;
    WholeSceneOutputRepresentationKind SelectedRepresentation =
        WholeSceneOutputRepresentationKind::None;
    WholeSceneOutputRepresentationProduct SelectedRepresentationProduct;
    WholeSceneOutputInsertionStage InsertionStage =
        WholeSceneOutputInsertionStage::UnclassifiedLegacy;
    WholeSceneOutputCorrectnessClass CorrectnessClass =
        WholeSceneOutputCorrectnessClass::Unclassified;

    bool AdmissionEvaluated = false;
    bool AdmissionChangedSelection = false;
    WholeSceneOutputAdmissionReason AdmissionReason =
        WholeSceneOutputAdmissionReason::NotEvaluated;
    WholeSceneRenderPath InitialSelectedPath = WholeSceneRenderPath::None;
    WholeSceneCurrentPathReason InitialCurrentReason =
        WholeSceneCurrentPathReason::None;
    WholeSceneOutputRepresentationKind InitialSelectedRepresentation =
        WholeSceneOutputRepresentationKind::None;
    u64 AdmissionRejectedUnsupportedChannels = 0;
    u64 AdmissionRejectedEvidenceUnavailableChannels = 0;

    // Step 2 owns population of the channel partition.  Keeping the masks in
    // the Step-1 record prevents trace and executor schemas from diverging.
    bool ChannelsAssessed = false;
    u64 RequiredChannels = 0;
    u64 ReproducedChannels = 0;
    u64 InactiveChannels = 0;
    u64 UnsupportedChannels = 0;
    u64 EvidenceUnavailableChannels = 0;

    u32 TransformCount = 0;
    u32 PlannedTransformMask = 0;
    std::array<WholeSceneOutputTransform, WholeSceneMaxOutputTransforms> Transforms;

    WholeSceneOutputSelectorPlan Selector;
    WholeSceneOutputEffectEvidence EffectEvidence;
    WholeSceneCompositorEffectState CompositorEffectState;

    WholeSceneCompositorFallbackPolicy CompositorFallbackPolicy =
        WholeSceneCompositorFallbackPolicy::None;
    WholeSceneCompositorFallbackPolicy OrderedPresentationFallbackPolicy =
        WholeSceneCompositorFallbackPolicy::None;
    WholeSceneCompositorFallbackPolicy OperandExcludedOverlayFallbackPolicy =
        WholeSceneCompositorFallbackPolicy::None;
};

// Recipe intent is prepared after the immutable output decision because it
// depends on renderer-owned affine role and operand facts.  Keeping the
// accepted recipes in this value record prevents execution preparation from
// mutating WholeSceneOutputPlan.
struct WholeSceneCompositorRecipeBinding
{
    bool Evaluated = false;
    bool Bound = false;
    WholeSceneOutputPlanRecipeBindingReason Reason =
        WholeSceneOutputPlanRecipeBindingReason::NotEvaluated;
    WholeSceneCompositorRecipe Recipe;
};

struct WholeSceneOrderedPresentationRecipeBinding
{
    bool Evaluated = false;
    bool Bound = false;
    WholeSceneOutputPlanRecipeBindingReason Reason =
        WholeSceneOutputPlanRecipeBindingReason::NotEvaluated;
    WholeSceneCompositorRecipe SemanticBaseRecipe;
    WholeSceneCompositorRecipe Recipe;
};

struct WholeSceneOperandExcludedOverlayRecipeBinding
{
    bool Evaluated = false;
    bool Bound = false;
    WholeSceneOutputPlanRecipeBindingReason Reason =
        WholeSceneOutputPlanRecipeBindingReason::NotEvaluated;
    WholeSceneOperandExcludedOverlayRecipe Recipe;
};

struct WholeSceneOutputRecipePreparation
{
    WholeSceneCompositorRecipeBinding Compositor;
    WholeSceneOrderedPresentationRecipeBinding OrderedPresentation;
    WholeSceneOperandExcludedOverlayRecipeBinding OperandExcludedOverlay;
};

// Execution-time capability assessment produced after GPU resources are
// prepared. OutputPlan owns intent; this value record owns which strict-affine
// products actually exist for the current range.
struct WholeSceneStrictAffineProductAssessment
{
    bool SemanticRecipeReady = false;
    bool ResolvedOBJSurfaceReady = false;
    bool OrderedOperandsReady = false;
    bool OperandExcludedOverlayReady = false;
    bool AtomicOrdinaryBandReady = false;
    bool NativeStackReady = false;
    bool AffineOBJInsertionReady = false;
    bool AffineOBJSubpixel2xReady = false;
};

// Post-construction execution observations are deliberately separate from the
// selected plan. The plan states intent; this record states which resources and
// recipes actually ran and whether execution agreed with that intent.
struct WholeSceneOutputExecutionTrace
{
    bool TransformsShadowOnly = true;
    u32 ObservedTransformMask = 0;
    u32 PlanExecutedTransformMask = 0;
    u32 TransformDisagreementMask = 0;
    u32 UnexpectedObservedTransformMask = 0;
    std::array<WholeSceneOutputTransformExecutionTrace,
               WholeSceneMaxOutputTransforms> Transforms;
    WholeSceneCaptureInputTrace CaptureInput;
    WholeScenePresentationTrace Presentation;

    bool RepresentationEvidenceTraceObserved = false;
    bool RepresentationEvidenceContentTraceAgreement = false;
    bool RepresentationEvidencePresentationTraceComparable = false;
    bool RepresentationEvidencePresentationTraceAgreement = false;
    bool RepresentationEvidenceCaptureInputTraceComparable = false;
    bool RepresentationEvidenceCaptureInputTraceAgreement = false;

    bool SourceASelectionObserved = false;
    SourceACaptureSelectionDecision SourceASelection;

    WholeSceneCompositorExecutionDisposition CompositorExecutionDisposition =
        WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason CompositorExecutionReason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    u64 PreparedCompositorRecipeHash = 0;
    bool CompositorRecipeExecutionObserved = false;
    WholeSceneCompositorRecipe ExecutedCompositorRecipe;
    u64 ExecutedCompositorRecipeHash = 0;
    bool CompositorRecipeParity = false;

    bool OrderedPresentationBaseRecipeExecutionObserved = false;
    WholeSceneCompositorExecutionDisposition
        OrderedPresentationExecutionDisposition =
            WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason OrderedPresentationExecutionReason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    u64 PreparedOrderedPresentationBaseRecipeHash = 0;
    u64 PreparedOrderedPresentationRecipeHash = 0;
    WholeSceneCompositorRecipe ExecutedOrderedPresentationBaseRecipe;
    u64 ExecutedOrderedPresentationBaseRecipeHash = 0;
    bool OrderedPresentationBaseRecipeParity = false;
    bool OrderedPresentationRecipeExecutionObserved = false;
    WholeSceneCompositorRecipe ExecutedOrderedPresentationRecipe;
    u64 ExecutedOrderedPresentationRecipeHash = 0;
    bool OrderedPresentationRecipeParity = false;

    bool OperandExcludedOverlayRecipeExecutionObserved = false;
    WholeSceneCompositorExecutionDisposition
        OperandExcludedOverlayExecutionDisposition =
            WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason OperandExcludedOverlayExecutionReason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    u64 PreparedOperandExcludedOverlayRecipeHash = 0;
    WholeSceneOperandExcludedOverlayRecipe
        ExecutedOperandExcludedOverlayRecipe;
    u64 ExecutedOperandExcludedOverlayRecipeHash = 0;
    bool OperandExcludedOverlayRecipeParity = false;

    WholeSceneStrictAffinePresentationKind
        StrictAffinePresentationExecutionKind =
            WholeSceneStrictAffinePresentationKind::NotEvaluated;
    WholeSceneStrictAffinePresentationReason
        StrictAffinePresentationExecutionReason =
            WholeSceneStrictAffinePresentationReason::NotEvaluated;
    bool StrictAffinePresentationAffineOBJInsertion = false;
    WholeSceneStrictAffineProductAssessment StrictAffineProductAssessment;
    u32 OrderedAffineOutputGridOperandCount = 0;
    u32 OrderedAffineSubpixel2xOperandCount = 0;
    u32 OrderedOpaqueAssemblyCount = 0;
    u32 OrderedOpaqueAssemblyRebuildCount = 0;

    bool ExecutionObserved = false;
    WholeSceneRenderPath ExecutedPath = WholeSceneRenderPath::None;
    WholeSceneCurrentPathReason ExecutedCurrentReason =
        WholeSceneCurrentPathReason::None;
    WholeSceneOutputExecutionKind ExecutedExecutionKind =
        WholeSceneOutputExecutionKind::None;
    bool PathParity = false;
    bool CurrentReasonParity = false;
    bool DecisionParity = false;
};

enum class WholeScenePhysicalSourceEngine : u8
{
    Unknown = 0,
    EngineA = 1,
    EngineB = 2,
    Mixed = 3,
};

enum WholeScenePhysicalPresentationChange : u32
{
    PhysicalPresentationChangeNone = 0,
    PhysicalPresentationChangeScope = 1u << 0,
    PhysicalPresentationChangeRoute = 1u << 1,
    PhysicalPresentationChangeRepresentation = 1u << 2,
    PhysicalPresentationChangeContentIdentity = 1u << 3,
    PhysicalPresentationChangeSourceGeneration = 1u << 4,
    PhysicalPresentationChangeCapability = 1u << 5,
    PhysicalPresentationChangeExecution = 1u << 6,
    PhysicalPresentationChangeCapture = 1u << 7,
    PhysicalPresentationChangeTransforms = 1u << 8,
    PhysicalPresentationChangeCompletion = 1u << 9,
};

enum class WholeScenePhysicalPresentationTransition : u8
{
    None = 0,
    First = 1,
    Stable = 2,
    Changed = 3,
    RouteHandoff = 4,
    RepresentationChange = 5,
    CaptureHandoff = 6,
    TransformChange = 7,
    CompletionChange = 8,
    ABAReversion = 9,
};

// Metadata-only evidence for one completed physical-screen presentation. It
// deliberately carries no texture, framebuffer, pixel buffer, or authority
// that a future plan could consume.
struct WholeScenePhysicalPresentationRecord
{
    bool Valid = false;
    bool CompletionValid = false;
    u64 Sequence = 0;
    u64 Frame = 0;
    int YStart = 0;
    int YEnd = 0;
    WholeScenePhysicalTarget Target = WholeScenePhysicalTarget::Unknown;
    WholeScenePhysicalSourceEngine SourceEngine =
        WholeScenePhysicalSourceEngine::Unknown;
    WholeSceneFinalPassPurpose Purpose =
        WholeSceneFinalPassPurpose::ScaledPresentation;
    u32 DisplayMode = 0;
    bool VRAMDisplayReplacement = false;

    WholeSceneOutputRepresentationKind SelectedRepresentation =
        WholeSceneOutputRepresentationKind::None;
    WholeSceneOutputInsertionStage InsertionStage =
        WholeSceneOutputInsertionStage::UnclassifiedLegacy;
    WholeSceneOutputCorrectnessClass CorrectnessClass =
        WholeSceneOutputCorrectnessClass::Unclassified;
    u32 RepresentationCapabilityMask = 0;
    u64 ProductIdentity = 0;
    u64 SourceGeneration = 0;
    u64 RowEpochIdentity = 0;

    WholeSceneRenderPath SelectedPath = WholeSceneRenderPath::None;
    WholeSceneOutputExecutionKind SelectedExecutionKind =
        WholeSceneOutputExecutionKind::None;
    WholeSceneOutputAdmissionReason AdmissionReason =
        WholeSceneOutputAdmissionReason::NotEvaluated;
    u64 UnsupportedChannels = 0;
    u64 EvidenceUnavailableChannels = 0;

    bool ExecutionObserved = false;
    WholeSceneRenderPath ExecutedPath = WholeSceneRenderPath::None;
    WholeSceneOutputExecutionKind ExecutedExecutionKind =
        WholeSceneOutputExecutionKind::None;
    bool DecisionParity = false;
    WholeSceneCompositorExecutionDisposition CompositorDisposition =
        WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason CompositorReason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    WholeSceneStrictAffinePresentationKind StrictAffinePresentationKind =
        WholeSceneStrictAffinePresentationKind::NotEvaluated;
    WholeSceneStrictAffinePresentationReason StrictAffinePresentationReason =
        WholeSceneStrictAffinePresentationReason::NotEvaluated;

    WholeSceneCaptureInputKind CaptureInputKind =
        WholeSceneCaptureInputKind::None;
    bool CaptureEventValid = false;
    u64 CaptureEventSerial = 0;
    u32 CaptureEventSourceKind = 0;
    u32 CaptureEventProductMask = 0;
    u32 CaptureEventRejectReason = 0;

    bool SelectedCaptureEvidenceAvailable = false;
    bool SelectedCaptureContentProven = false;
    bool SelectedCapturePresentationProven = false;
    WholeSceneCaptureProductKind SelectedCaptureProductKind =
        WholeSceneCaptureProductKind::None;
    WholeSceneCaptureProofKind SelectedCaptureProofKind =
        WholeSceneCaptureProofKind::None;
    u32 SelectedCaptureBank = 0xFFFFFFFFu;
    u64 SelectedCaptureEventSerial = 0;
    u64 SelectedCaptureBackgroundEpochSerial = 0;
    u32 SelectedCapturePresentationHash = 0;
    u32 SelectedCaptureCurrentPresentationHash = 0;
    WholeSceneCaptureProofKind SelectedCaptureAuthorizationProofKind =
        WholeSceneCaptureProofKind::None;
    u64 SelectedCaptureAuthorizationEpochSerial = 0;
    u32 SelectedCaptureAuthorizationBank = 0xFFFFFFFFu;
    u64 SelectedCaptureSource3DSerial = 0;
    u32 SelectedCaptureSource3DSceneHash = 0;
    u32 SelectedCaptureSourceKind = 0;
    u32 SelectedCaptureProductMask = 0;

    u32 PlannedTransformMask = 0;
    u32 ObservedTransformMask = 0;
    u32 PlanExecutedTransformMask = 0;
    u32 TransformDisagreementMask = 0;
};

struct WholeScenePhysicalPresentationHistory
{
    std::array<WholeScenePhysicalPresentationRecord, 3> Recent;
    u32 Count = 0;
    u32 LastChangeMask = PhysicalPresentationChangeNone;
    u32 LastReversionMask = PhysicalPresentationChangeNone;
    WholeScenePhysicalPresentationTransition LastTransition =
        WholeScenePhysicalPresentationTransition::None;
};

struct WholeScenePhysicalPresentationLedger
{
    WholeScenePhysicalPresentationHistory Top;
    WholeScenePhysicalPresentationHistory Bottom;
    u64 UnknownTargetCount = 0;
    u64 MixedTargetCount = 0;
};

struct WholeSceneStrictAffinePresentationOutcome
{
    WholeSceneStrictAffinePresentationKind Kind =
        WholeSceneStrictAffinePresentationKind::NotEvaluated;
    WholeSceneStrictAffinePresentationReason Reason =
        WholeSceneStrictAffinePresentationReason::NotEvaluated;
    bool AffineOBJInsertion = false;
};

struct WholeSceneCompositorExecutionOutcome
{
    WholeSceneCompositorExecutionDisposition Disposition =
        WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason Reason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    WholeSceneCompositorRecipe EffectiveRecipe;
};

struct WholeSceneOrderedPresentationExecutionOutcome
{
    WholeSceneCompositorExecutionDisposition Disposition =
        WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason Reason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    WholeSceneCompositorRecipe EffectiveSemanticBaseRecipe;
    WholeSceneCompositorRecipe EffectiveRecipe;
};

struct WholeSceneOperandExcludedOverlayExecutionOutcome
{
    WholeSceneCompositorExecutionDisposition Disposition =
        WholeSceneCompositorExecutionDisposition::NotEvaluated;
    WholeSceneCompositorExecutionReason Reason =
        WholeSceneCompositorExecutionReason::NotEvaluated;
    WholeSceneOperandExcludedOverlayRecipe EffectiveRecipe;
};

struct WholeSceneScaleCandidateInputs
{
    bool HighResCompositorAvailable = false;
    bool StrictAffineHighResAvailable = false;
    bool IdentityEquivalentAffineWholeSceneAvailable = false;
    StrictAffineHighResBlockReason StrictAffineBlockReason =
        StrictAffineHighResBlockReason::FeatureDisabled;
    bool SourceACaptureReplacementAvailable = false;
    bool OverlayOperatorAvailable = false;
    bool FinalNativeAvailable = false;
    bool ConservativeHybridMode = false;
    bool HybridFragmentationGuardActive = false;
    WholeSceneCaptureBackedPlan CapturePlan;
};

struct WholeSceneOutputPlanInputs
{
    int YStart = 0;
    int YEnd = 0;
    bool SelectPathFromFacts = false;
    WholeScenePathDecisionInputs PathDecisionInputs;
    // Transitional direct injection remains available to focused policy tests
    // until the final authority-cleanup slice migrates those fixtures.
    WholeScenePathDecision PathDecision;
    bool SelectScaleFromFacts = false;
    WholeSceneScaleCandidateInputs ScaleCandidateInputs;
    bool HasScaleDecision = false;
    WholeSceneScaleDecision ScaleDecision;
    u32 ConsideredRepresentationMask = 0;
    bool SelectorConfigured = false;
    HybridSourceDecision HybridDecision;
    bool WindowEdgeAssist = false;
    bool Target2AlphaBlendAssist = false;
    bool NativeEffectGuard = false;
    bool CleanLegacyCandidateEnabled = false;
    HybridCleanLegacyBlockReason CleanLegacyBlockReason =
        HybridCleanLegacyBlockReason::PathModeOrSettingUnavailable;
    int OutputScale = 1;
    u64 RepresentationSourceGeneration = 0;
    u64 RepresentationRowEpochIdentity = 0;
    u64 RequiredChannels = 0;
    u16 BlendCnt = 0;
    u8 EVA = 0;
    u8 EVB = 0;
    u8 EVY = 0;
    u8 SemiTransparentOBJModeMask = 0;
    u16 MasterBrightness = 0;
    int MasterBrightnessYStart = -1;
    int MasterBrightnessYEnd = -1;
    u32 CaptureCnt = 0;
    u32 DisplayMode = 0;
    WholeScenePhysicalTarget PhysicalTarget = WholeScenePhysicalTarget::Unknown;
    bool HasPhysicalPresentationBrightness = false;
    u16 PhysicalPresentationBrightness = 0;
    int PhysicalPresentationBrightnessYStart = 0;
    int PhysicalPresentationBrightnessYEnd = 0;
};

struct WholeSceneOutputTransformObservation
{
    WholeSceneOutputTransformKind Kind = WholeSceneOutputTransformKind::None;
    int YStart = 0;
    int YEnd = 0;
    u32 State = 0;
    u32 AuxState = 0;
    WholeScenePhysicalTarget Target = WholeScenePhysicalTarget::Unknown;
    bool LegacyApplied = false;
    bool PlanApplied = false;
};

WholeSceneOutputRepresentationKind WholeSceneOutputRepresentationForPath(
    WholeSceneRenderPath path);

WholeSceneOutputPlan BuildWholeSceneOutputPlan(const WholeSceneOutputPlanInputs& inputs);

WholeSceneCompositorRecipeBinding PrepareWholeSceneCompositorRecipeBinding(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipe& recipe);

WholeSceneCompositorExecutionOutcome ResolveWholeSceneCompositorExecution(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipeBinding& binding,
    const WholeSceneCompositorRecipe& preparedRecipe);

void ObserveWholeSceneOutputPlanCompositorRecipeExecution(
    const WholeSceneCompositorRecipeBinding& binding,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneCompositorExecutionOutcome& outcome);

WholeSceneOrderedPresentationRecipeBinding
PrepareWholeSceneOrderedPresentationRecipeBinding(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipeBinding& compositorBinding,
    const WholeSceneCompositorRecipe& semanticBaseRecipe,
    const WholeSceneCompositorRecipe& recipe);

WholeSceneOrderedPresentationExecutionOutcome
ResolveWholeSceneOrderedPresentationExecution(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOrderedPresentationRecipeBinding& binding,
    const WholeSceneCompositorRecipe& preparedSemanticBaseRecipe,
    const WholeSceneCompositorRecipe& preparedRecipe,
    bool productsReady);

void ObserveWholeSceneOutputPlanOrderedPresentationRecipeExecution(
    const WholeSceneOrderedPresentationRecipeBinding& binding,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneOrderedPresentationExecutionOutcome& outcome);

WholeSceneOperandExcludedOverlayRecipeBinding
PrepareWholeSceneOperandExcludedOverlayRecipeBinding(
    const WholeSceneOutputPlan& plan,
    const WholeSceneCompositorRecipeBinding& compositorBinding,
    const WholeSceneOrderedPresentationRecipeBinding& orderedBinding,
    const WholeSceneOperandExcludedOverlayRecipe& recipe);

WholeSceneOperandExcludedOverlayExecutionOutcome
ResolveWholeSceneOperandExcludedOverlayExecution(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOperandExcludedOverlayRecipeBinding& binding,
    const WholeSceneOperandExcludedOverlayRecipe& preparedRecipe);

void RecordWholeSceneOperandExcludedOverlayRenderFailure(
    WholeSceneOperandExcludedOverlayExecutionOutcome& outcome);

void ObserveWholeSceneOutputPlanOperandExcludedOverlayRecipeExecution(
    const WholeSceneOperandExcludedOverlayRecipeBinding& binding,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneOperandExcludedOverlayExecutionOutcome& outcome,
    bool executionObserved);

WholeSceneStrictAffinePresentationOutcome
ResolveWholeSceneStrictAffinePresentationExecution(
    const WholeSceneStrictAffineProductAssessment& assessment);

void ObserveWholeSceneStrictAffinePresentationExecution(
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneStrictAffineProductAssessment& assessment,
    const WholeSceneStrictAffinePresentationOutcome& outcome);

// Some execution recipes finish the frame output during RenderScreen and must
// not be followed by the deferred VBlank finalizer. This decision must use the
// admitted execution recipe rather than the pre-admission route candidate.
bool WholeSceneOutputExecutionSuppressesDeferredFinalizer(
    const WholeSceneOutputPlan& plan);

const WholeSceneOutputTransform* ResolveWholeSceneOutputTransformForExecution(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputTransformKind kind,
    int ystart,
    int yend);

bool MarkWholeSceneOutputTransformPlanExecuted(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    WholeSceneOutputTransformKind kind,
    int ystart,
    int yend,
    u32 state,
    u32 auxState);

bool IsWholeSceneOutputTransformPlanExecuted(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace,
    WholeSceneOutputTransformKind kind,
    int ystart,
    int yend,
    u32 state,
    u32 auxState);

void AssessWholeSceneOutputPlan(
    WholeSceneOutputPlan& plan,
    const WholeSceneOutputPlanInputs& inputs);

void ObserveWholeSceneOutputTransform(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneOutputTransformObservation& observation);

void ObserveWholeSceneCaptureInput(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneCaptureInputTrace& captureInput);

void ObserveWholeSceneCaptureRepresentationEvidence(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeSceneCaptureRepresentationEvidence& observedEvidence);

void ObserveWholeScenePresentation(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    const WholeScenePresentationTrace& presentation,
    u16 masterBrightness);

u32 WholeSceneOutputUnobservedTransformMask(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace);

void ObserveWholeSceneOutputPlanExecution(
    const WholeSceneOutputPlan& plan,
    WholeSceneOutputExecutionTrace& trace,
    WholeSceneRenderPath executedPath,
    WholeSceneCurrentPathReason executedCurrentReason,
    WholeSceneOutputExecutionKind executedExecutionKind);

WholeScenePhysicalPresentationRecord BuildWholeScenePhysicalPresentationRecord(
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputExecutionTrace& trace,
    const WholeScenePresentationTrace& presentation,
    WholeScenePhysicalSourceEngine sourceEngine,
    u64 sequence,
    u64 frame,
    bool completionValid);

u32 CompareWholeScenePhysicalPresentations(
    const WholeScenePhysicalPresentationRecord& previous,
    const WholeScenePhysicalPresentationRecord& current);

void RecordWholeScenePhysicalPresentation(
    WholeScenePhysicalPresentationLedger& ledger,
    const WholeScenePhysicalPresentationRecord& record);

const WholeScenePhysicalPresentationHistory*
GetWholeScenePhysicalPresentationHistory(
    const WholeScenePhysicalPresentationLedger& ledger,
    WholeScenePhysicalTarget target);

void AppendWholeScenePhysicalPresentationCSVHeader(
    std::string& header,
    const char* prefix);

void AppendWholeScenePhysicalPresentationCSVRow(
    std::string& row,
    const WholeScenePhysicalPresentationHistory& history);

void AppendWholeSceneOutputPlanCSVHeader(
    std::string& header,
    const char* prefix);

void AppendWholeSceneOutputPlanCSVRow(
    std::string& row,
    const WholeSceneOutputPlan& plan,
    const WholeSceneOutputRecipePreparation& recipes,
    const WholeSceneOutputExecutionTrace& trace);

}
