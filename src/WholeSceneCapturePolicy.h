// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

namespace melonDS
{

enum class WholeSceneCaptureAuthority
{
    None,
    SourceAFullProduct,
    SourceABackgroundCurrentOverlay,
    CaptureEpochBackgroundCurrentOverlay,
    HandoffSnapshot,
    CaptureEventBackground,
    CaptureEventFullProduct,
};

enum class WholeSceneCaptureBackedPlanRole : u8
{
    None,
    RouteProducer,
    RouteConsumer,
    RouteHandoff,
};

enum class WholeSceneCaptureRequestKind : u8
{
    None,
    LiveOverlayProducer,
    CapturedLayerConsumer,
    HandoffConsumer,
    DirectFinalConsumer,
    MainVRAMDisplayConsumer,
};

enum class WholeSceneCaptureProductKind : u8
{
    None,
    RouteProduct,
    RouteEventProduct,
    RouteStateProduct,
    BackgroundProduct,
    FullCaptureProduct,
    HandoffSnapshot,
    ParentOutput3D,
};

enum class WholeSceneCaptureProductPresentationClass : u8
{
    None = 0,
    RawContent = 1,
    AlreadyPresented = 2,
    Fallback = 3,
    Unknown = 4,
};

enum class WholeSceneCaptureEffectOwner : u8
{
    None = 0,
    CurrentEngine = 1,
    SourceA = 2,
    FinalDisplay = 3,
    CapturedPresentation = 4,
    Unknown = 5,
    // BLDCNT/BLDY brightness color effect of the consuming engine, applied at
    // blit time. Distinct from CurrentEngine so the final-pass master
    // brightness suppression proof can never match it: the color effect and
    // master brightness are independent stages and both must apply.
    CurrentEngineColorEffect = 6,
};

enum class WholeSceneCaptureProofKind : u8
{
    None,
    ExactCaptureEvent,
    RouteStateIdentity,
    ActiveBackgroundEpoch,
    HandoffRouteKey,
    DirectFinalPresentationMatch,
    CurrentOverlayEligibility,
    Source3DSceneIdentity,
};

enum class WholeSceneCaptureRenderAction : u8
{
    None,
    BlitExactProduct,
    CompositeCurrentOverlay,
    RenderHandoffHybrid,
    RenderNormalHybridFallback,
};

enum class SourceABackgroundSource
{
    None,
    ActiveCaptureEpochTex,
    ParentOutputTex3D,
    RouteProduct,
    FullCaptureProduct,
    CaptureEventBackgroundTex,
    HandoffSnapshot,
};

enum class CaptureBackedRouteProductLookupSource : u8
{
    None,
    ExactEventProduct,
    ExactEventRouteProduct,
    RouteStateProduct,
    Source3DSceneProduct,
};

enum class CaptureBackedHandoffPhase : u8
{
    None = 0,
    Live3D = 1,
    CapturedBitmap = 2,
    Other = 3,
};

enum class CaptureBackedHandoffReuseReason : u8
{
    None = 0,
    UpdatedLive3D = 1,
    ExactKeyMatch = 2,
    AllowedLiveToCapturePair = 3,
    RejectedNoSnapshot = 4,
    RejectedScreenSwapChanged = 5,
    RejectedEngineChanged = 6,
    RejectedPhysicalScreenChanged = 7,
    RejectedRouteChanged = 8,
    RejectedPhaseNotEquivalent = 9,
    RejectedEpochChanged = 10,
    RejectedYRangeChanged = 11,
    RejectedUnstableLivePhase = 12,
    UsedCaptureEventBackground = 13,
    UsedCaptureEventFullProduct = 14,
};

enum class CaptureBackedRoutePresentationMode : u8
{
    None = 0,
    FullProduct = 1,
    BackgroundCurrentOverlay = 2,
    HandoffSnapshot = 3,
};

enum class SourceACaptureResolutionKind : u8
{
    None,
    RouteProduct,
    BackgroundOverlay,
    RejectedFallback,
    FullProduct,
};

enum class SourceACaptureSelectionReason : u8
{
    None,
    ExactFullProductPreference,
    ExactRouteProductPreference,
    RouteProductCompositionRebuild,
    RouteProductAvailable,
    CurrentOverlayAvailable,
    FullProductAvailable,
    NoUsableProduct,
};

enum class SourceACaptureSelectionPreference : u8
{
    None,
    ExactFullProduct,
    ExactRouteProduct,
};

enum class SourceAFullProductPresentationProof : u8
{
    None,
    ExactFullEquivalentDirectFinal,
};

enum class WholeSceneCaptureBackedPlanKind : u8
{
    None,
    CaptureBackedHandoff,
    SourceACaptureReplacement,
    CaptureEpochOverlay,
};

enum class WholeSceneCaptureBackedPlanStage : u8
{
    None,
    BeforeGeneralFallbacks,
    AfterGeneralFallbacks,
};

struct SourceACaptureResolutionInputs
{
    bool HasRouteProduct = false;
    bool RouteProductCompositionMismatch = false;
    bool CanUseCurrentOverlay = false;
    bool HasFullProduct = false;
    SourceACaptureSelectionPreference Preference =
        SourceACaptureSelectionPreference::None;
    SourceAFullProductPresentationProof FullProductPresentationProof =
        SourceAFullProductPresentationProof::None;
    bool AllowCurrentOverlay = true;
};

struct SourceACaptureSelectionDecision
{
    SourceACaptureResolutionKind Primary =
        SourceACaptureResolutionKind::RejectedFallback;
    SourceACaptureSelectionReason PrimaryReason =
        SourceACaptureSelectionReason::None;
    SourceACaptureResolutionKind AfterOverlayFailure =
        SourceACaptureResolutionKind::RejectedFallback;
    SourceACaptureSelectionReason AfterOverlayFailureReason =
        SourceACaptureSelectionReason::None;
    SourceAFullProductPresentationProof FullProductPresentationProof =
        SourceAFullProductPresentationProof::None;
};

struct SourceAFullProductPresentationProofInputs
{
    bool DirectFinalBottomConsumer = false;
    bool DirectFinalDisplayConsumer = false;
    bool SubEngineCapturedOBJOnly = false;
    bool MainEngineCapturedBGOnly = false;
    bool FullProductEventRouteMatches = false;
    bool SubEngineCapturedSourceAOnly = false;
    bool HasFullProduct = false;
    bool FullProductEventValid = false;
    bool FullProductEventFullEquivalent = false;
    bool FullProductEventCleanEngineA2DOutput = false;
    bool FullProductEventAccepted = false;
};

struct SourceAExactRouteProductPreferenceInputs
{
    bool DirectFinalBottomConsumer = false;
    bool SubEngineCaptureBackedBGOnly = false;
    bool HasRouteProduct = false;
    WholeSceneCaptureProductKind RouteProductKind = WholeSceneCaptureProductKind::None;
    WholeSceneCaptureProofKind RouteProductProof = WholeSceneCaptureProofKind::None;
    u64 RouteProductEventSerial = 0;
    u32 RouteProductCaptureBank = 0xFFFFFFFFu;
    u32 RouteProductCapturePresentationHash = 0;
    u64 RouteProductSource3DSerial = 0;
    u32 RouteProductSource3DSceneHash = 0;
    bool HasFullProduct = false;
    bool FullProductEventValid = false;
    u64 FullProductEventSerial = 0;
    u32 FullProductCaptureBank = 0xFFFFFFFFu;
    u32 FullProductCapturePresentationHash = 0;
    u64 FullProductSource3DSerial = 0;
    u32 FullProductSource3DSceneHash = 0;
    bool FullProductEventFullEquivalent = false;
    bool FullProductEventCleanEngineA2DOutput = false;
    bool FullProductEventAccepted = false;
    bool FullProductEventSourceOBJVisible = false;
};

struct SourceAExactFullProductPreferenceInputs
{
    bool DirectFinalBottomConsumer = false;
    bool SubEngineCapturedSourceAOnly = false;
    bool HasFullProduct = false;
    bool FullProductEventValid = false;
    bool FullProductKeyMatch = false;
    bool FullProductEventRouteMatches = false;
    bool FullProductEventFullEquivalent = false;
    bool FullProductEventCleanEngineA2DOutput = false;
    bool FullProductEventAccepted = false;
    bool FullProductEventSourceOBJVisible = false;
};

struct DirectFinalRouteMatchInputs
{
    bool EventScreenSwap = false;
    bool CurrentScreenSwap = false;
    bool EventMainFinalBottom = false;
    bool CurrentMainFinalBottom = false;
};

struct SourceACurrentOverlayEligibilityInputs
{
    bool HasBackgroundTexture = false;
    bool PreferExactFullProductForDirectBottom = false;
    bool SourceIsCleanEngineA2DOutput = false;
    bool RendererCanCompositeCurrentOverlay = false;
    bool SubEngineDirectFinalTopConsumer = false;
};

struct CaptureEpochOverlayCurrentInputs
{
    u64 CurrentSource3DSerial = 0;
    u32 CurrentSource3DSceneHash = 0;
    u32 CurrentPresentationHash = 0;
    u32 EpochCaptureBank = 0xFFFFFFFFu;
    u64 EpochSource3DSerial = 0;
    u32 EpochSource3DSceneHash = 0;
    bool CaptureRequestConsumesCurrentComposite = false;
    u32 CaptureRequestBank = 0xFFFFFFFFu;
};

struct CaptureEpochOverlayCurrentPlan
{
    SourceABackgroundSource BackgroundSource =
        SourceABackgroundSource::ParentOutputTex3D;
    u64 BackgroundEpochSerial = 0;
    u64 Source3DSerial = 0;
    u32 Source3DSceneHash = 0;
    u32 PresentationHash = 0;
    bool CanPublishRouteProduct = false;
    u32 CaptureBank = 0xFFFFFFFFu;
};

CaptureEpochOverlayCurrentPlan MakeCaptureEpochOverlayCurrentPlan(
    const CaptureEpochOverlayCurrentInputs& inputs);

struct WholeSceneCaptureProductUseInputs
{
    bool PolicyAccepted = false;
    bool HasTexture = false;
    WholeSceneCaptureRequestKind RequestKind = WholeSceneCaptureRequestKind::None;
    WholeSceneCaptureProductKind ProductKind = WholeSceneCaptureProductKind::None;
    WholeSceneCaptureProofKind ProofKind = WholeSceneCaptureProofKind::None;
    WholeSceneCaptureRenderAction RenderAction = WholeSceneCaptureRenderAction::None;
    WholeSceneCaptureProductPresentationClass PresentationClass =
        WholeSceneCaptureProductPresentationClass::None;
    u32 ProductPresentationHash = 0;
    u32 RequestPresentationHash = 0;
    int RequestYStart = 0;
    int RequestYEnd = 192;
    int ProductYStart = 0;
    int ProductYEnd = 192;
};

struct WholeSceneCaptureProductUseDecision
{
    bool Accepted = false;
    bool PresentationCompatible = false;
    bool RowScopeCompatible = false;
};

// Packed master-brightness-style state (mode<<14 | factor) for the consuming
// engine's frame-global BLDCNT brightness color effect, or 0 when BLDCNT does
// not select one. A direct-final capture-backed replacement bypasses the
// consuming engine's compositor, which is the stage that natively applies
// this effect, so the blit must reproduce it.
u16 ConsumerFullScreenBrightnessColorEffect(u16 blendCnt, u8 evy);

// Main-VRAM display bypasses the 2D compositor's BLDCNT brightness effect.
// Either full-screen endpoint may reject a one-frame-late high-resolution
// substitute, but this predicate never authorizes changing the native VRAM
// display's physical color.
bool IsGuaranteedFullScreenBrightnessEndpoint(
    u16 blendCnt,
    u8 evy,
    bool windowingActive);

struct MainVRAMDisplayCaptureScaleInputs
{
    bool MainEngine = false;
    u32 DisplayMode = 0;
    bool ConservativeHybridMode = false;
    bool CaptureBackedScalingEnabled = false;
    bool HasAcceptedDisplayReplacement = false;
    bool CaptureEnabled = false;
    u32 CaptureCnt = 0;
    u32 DisplayBank = 0xFFFFFFFFu;
};

struct MainVRAMDisplayExactEventReplacementInputs
{
    bool EventRecordValid = false;
    bool EventDstOffsetZero = false;
    bool EventAccepted = false;
    bool EventFullEquivalent = false;
    bool EventAcceptedSource = false;
    bool HasFullTexture = false;
    bool EventRouteMatches = false;
    bool EventSourceOBJVisible = false;
    bool EventSourceRenderedFullWholeScene = false;
};

struct MainVRAMDisplayEventMatchInputs
{
    bool NativeCaptureRecordValid = false;
    bool HighResEventRecordValid = false;
    u32 NativeCaptureDstOffset = 0xFFFFFFFFu;
    u32 HighResEventDstOffset = 0xFFFFFFFFu;
    u32 NativeCaptureCnt = 0;
    u32 HighResEventCaptureCnt = 0;
};

struct MainVRAMDisplayMixedOrOBJRejectInputs
{
    bool DirtyOrPartialSourceReject = false;
    bool NativeOnlyOutput2DSource = false;
    bool SourceDirect3DVisible = false;
    bool SourceHasNoVisibleBitmap = false;
    bool SourceBG0Visible = false;
    bool SourceOBJVisible = false;
    bool SourceOtherBGVisible = false;
};

struct MainVRAMDisplayEpochReplacementInputs
{
    bool EpochValid = false;
    u32 EpochCaptureBank = 0xFFFFFFFFu;
    u32 DisplayBank = 0xFFFFFFFFu;
    u32 EpochDstOffset = 0xFFFFFFFFu;
    bool EpochHasFullDirtyRows = false;
    bool EpochFullEquivalent = false;
    bool EpochAcceptedSource = false;
    bool HasEpochTexture = false;
};

struct HandoffVisibleEpochMatchInputs
{
    bool VisibleDisplayCaptureBankValid = false;
    bool EpochValid = false;
    u32 EpochCaptureBank = 0xFFFFFFFFu;
    u32 VisibleDisplayCaptureBank = 0xFFFFFFFFu;
    bool ConsumerRouteSlotMatches = false;
    bool HasBackgroundEpochTexture = false;
    bool ProductHasBackground3DUnderlay = false;
    bool SourceIsCleanEngineA2DOutput = false;
    bool SourceDirect3DVisible = false;
    bool SourceOnlyBG0Visible = false;
    bool SourceBGModeMatchesCurrent = false;
    bool SourceHasNoVisibleBitmap = false;
};

struct HandoffEventRecencyInputs
{
    bool EpochMatchesVisibleBank = false;
    bool VisibleEventValid = false;
    u64 VisibleEventSerial = 0;
    u64 EpochSerial = 0;
    u64 MaxSerialAge = 2;
};

struct HandoffExactEpochInputs
{
    bool RecentEpochForVisibleBank = false;
    u64 VisibleEventSerial = 0;
    u64 EpochSerial = 0;
    bool VisibleEventAccepted = false;
    bool VisibleEventFullEquivalent = false;
};

struct HandoffRoutePresentationStableInputs
{
    bool PresentationValid = false;
    u32 PresentationCaptureBank = 0xFFFFFFFFu;
    u32 VisibleDisplayCaptureBank = 0xFFFFFFFFu;
    u32 PresentationSourceHash = 0;
    u32 EpochSourceHash = 0;
    u32 StableFrames = 0;
    u32 RequiredStableFrames = 2;
};

struct HandoffRouteBackgroundOverlayInputs
{
    bool RecentEpochForVisibleBank = false;
    bool RoutePresentationStable = false;
    bool CapturedBitmapUpdatedThisFrame = false;
};

struct HandoffStableLiveCandidateInputs
{
    bool Direct3DOnlyLiveBackground = false;
    bool HasProvenRouteProduct = false;
    bool RouteHasCapturedPhase = false;
    bool CurrentCaptureEventMatchesRoute = false;
    bool CurrentCaptureEventFresh = false;
    bool HasNoBGUpload = false;
    bool NativeProductEpochValid = false;
};

struct HandoffPresentationEligibilityInputs
{
    CaptureBackedHandoffPhase Phase = CaptureBackedHandoffPhase::None;
    bool RouteHasCapturedPhase = false;
    bool CurrentCaptureEventMatchesRoute = false;
    bool CurrentCaptureEventFresh = false;
    bool HasOnlyFullDisplaySourceACapture = false;
};

struct CaptureBackedHandoffRouteKey
{
    u8 Engine = 0;
    bool ScreenSwap = false;
    bool EngineFinalTop = false;
    bool EngineFinalBottom = false;
    u32 DisplayMode = 0;
    u32 BGMode = 0;
    u32 LayerEnable = 0;
    u32 VisibleBitmapMask = 0;
    u32 BGUploadRows = 0;
    u32 FrameSerial = 0;
    int YStart = 0;
    int YEnd = 192;
    CaptureBackedHandoffPhase Phase = CaptureBackedHandoffPhase::None;
};

struct CaptureBackedRoutePresentationState
{
    bool Valid = false;
    CaptureBackedRoutePresentationMode Mode = CaptureBackedRoutePresentationMode::None;
    u64 Serial = 0;
    u32 CaptureBank = 0xFFFFFFFFu;
    u32 Source3DSceneHash = 0;
    u32 SourcePresentationHash = 0;
    u32 CurrentOverlayPresentationHash = 0;
    u32 StableFrames = 0;
};

struct CaptureBackedRouteProductIdentity
{
    u64 BackgroundEpochSerial = 0;
    u64 Source3DSerial = 0;
    u32 Source3DSceneHash = 0;
    u32 CaptureBank = 0xFFFFFFFFu;
    u32 CapturePresentationHash = 0;
    u32 CurrentOverlayPresentationHash = 0;
};

bool IsValidCaptureBackedRouteProductIdentity(
    const CaptureBackedRouteProductIdentity& identity);

bool SameCaptureBackedRouteProductIdentity(
    const CaptureBackedRouteProductIdentity& a,
    const CaptureBackedRouteProductIdentity& b);

bool CaptureBackedRouteProductIdentityMatchesEvent(
    const CaptureBackedRouteProductIdentity& identity,
    u64 captureEventSerial,
    u64 source3DSerial,
    u32 source3DSceneHash);

bool ShouldApplyHandoffPresentationEffect(
    WholeSceneCaptureProductPresentationClass productClass,
    WholeSceneCaptureRequestKind requestKind);

bool DoesCaptureProductPresentationMatchRequest(
    u32 productPresentationHash,
    u32 requestPresentationHash);

bool CanUseCaptureProductAsPresented(
    WholeSceneCaptureProductPresentationClass productClass,
    u32 productPresentationHash,
    u32 requestPresentationHash);

bool IsStorableCaptureBackedRouteProductClass(
    WholeSceneCaptureProductPresentationClass productClass);

WholeSceneCaptureProductUseDecision CanUseWholeSceneCaptureProduct(
    const WholeSceneCaptureProductUseInputs& inputs);

struct CaptureBackedRouteProductState
{
    bool Valid = false;
    CaptureBackedRouteProductIdentity Identity;
    u64 CapturedEventSerial = 0;
    u32 CapturedEventFrameSerial = 0;
    u32 StableFrames = 0;
    WholeSceneCaptureProductPresentationClass PresentationClass =
        WholeSceneCaptureProductPresentationClass::RawContent;
};

struct CaptureBackedRouteEventProductState
{
    bool Valid = false;
    CaptureBackedRouteProductIdentity Identity;
    u64 CapturedEventSerial = 0;
    u32 CapturedEventFrameSerial = 0;
    u32 StableFrames = 0;
    WholeSceneCaptureProductPresentationClass PresentationClass =
        WholeSceneCaptureProductPresentationClass::RawContent;
};

struct CaptureBackedRoutePendingEventState
{
    bool Valid = false;
    u64 CaptureEventSerial = 0;
    u32 CaptureEventFrameSerial = 0;
    u64 Source3DSerial = 0;
    u32 Source3DSceneHash = 0;
    u32 CaptureBank = 0xFFFFFFFFu;
    u32 CapturePresentationHash = 0;
};

struct CaptureBackedRouteProductEventQuery
{
    int RouteSlot = -1;
    u64 CaptureEventSerial = 0;
    u64 Source3DSerial = 0;
    u32 Source3DSceneHash = 0;
    u32 CaptureBank = 0xFFFFFFFFu;
    u32 CapturePresentationHash = 0;
    int YStart = 0;
    int YEnd = 192;
};

struct CaptureBackedRouteProductStateQuery
{
    int RouteSlot = -1;
    CaptureBackedRouteProductIdentity Identity;
    int YStart = 0;
    int YEnd = 192;
};

CaptureBackedRouteProductEventQuery MakeCaptureBackedRouteProductEventQuery(
    int routeSlot,
    u64 captureEventSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 captureBank,
    u32 capturePresentationHash,
    int ystart,
    int yend);

CaptureBackedRouteProductStateQuery MakeCaptureBackedRouteProductStateQuery(
    int routeSlot,
    u64 backgroundEpochSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 captureBank,
    u32 capturePresentationHash,
    u32 currentOverlayPresentationHash,
    int ystart,
    int yend);

bool IsCaptureBackedRouteProductEventQueryUsable(
    const CaptureBackedRouteProductEventQuery& query,
    int routeSlotCount,
    bool hasRouteProductTexture);

bool DoesCaptureBackedRouteEventProductMatchQuery(
    const CaptureBackedRouteEventProductState& product,
    const CaptureBackedRouteProductEventQuery& query);

bool DoesCaptureBackedRouteProductMatchEventQuery(
    const CaptureBackedRouteProductState& product,
    const CaptureBackedRouteProductEventQuery& query);

bool DoesCaptureBackedRouteProductMatchSource3DSceneQuery(
    const CaptureBackedRouteProductState& product,
    const CaptureBackedRouteProductEventQuery& query);

WholeSceneCaptureProductKind CaptureProductKindForRouteLookup(
    CaptureBackedRouteProductLookupSource source);

WholeSceneCaptureProofKind CaptureProofKindForRouteLookup(
    CaptureBackedRouteProductLookupSource source);

WholeSceneCaptureProductKind CaptureProductKindForBackgroundSource(
    SourceABackgroundSource source);

WholeSceneCaptureProofKind CaptureProofKindForBackgroundSource(
    SourceABackgroundSource source);

SourceACaptureSelectionDecision ChooseSourceACaptureSelectionDecision(
    const SourceACaptureResolutionInputs& inputs);

SourceAFullProductPresentationProof ProveSourceAFullProductPresentation(
    const SourceAFullProductPresentationProofInputs& inputs);

SourceACaptureSelectionPreference ChooseSourceAExactProductPreference(
    const SourceAExactFullProductPreferenceInputs& fullProductInputs,
    const SourceAExactRouteProductPreferenceInputs& routeProductInputs);

bool DoesDirectFinalRouteMatch(
    const DirectFinalRouteMatchInputs& inputs);

bool CanUseSourceACurrentOverlay(
    const SourceACurrentOverlayEligibilityInputs& inputs);

bool CanScaleMainVRAMDisplayCaptureSourceA(
    const MainVRAMDisplayCaptureScaleInputs& inputs);

bool CanUseMainVRAMDisplayExactEventReplacement(
    const MainVRAMDisplayExactEventReplacementInputs& inputs);

bool DoesMainVRAMDisplayEventMatchNativeCapture(
    const MainVRAMDisplayEventMatchInputs& inputs);

bool IsMainVRAMDisplayMixedOrOBJReject(
    const MainVRAMDisplayMixedOrOBJRejectInputs& inputs);

int MainVRAMDisplayEpochReplacementRejectReason(
    const MainVRAMDisplayEpochReplacementInputs& inputs);

bool DoesHandoffEpochMatchVisibleBank(
    const HandoffVisibleEpochMatchInputs& inputs);

bool IsHandoffEventRecentForEpoch(
    const HandoffEventRecencyInputs& inputs);

bool IsHandoffExactEpochForVisibleBank(
    const HandoffExactEpochInputs& inputs);

bool IsHandoffRoutePresentationStable(
    const HandoffRoutePresentationStableInputs& inputs);

bool ShouldUseHandoffRouteBackgroundOverlay(
    const HandoffRouteBackgroundOverlayInputs& inputs);

// A route product proves that capture infrastructure produced content; it
// does not prove that the current physical route alternates through a captured
// presentation. Stable live frames may maintain a known handoff only after
// that route has actually exhibited its captured phase or when the current
// accepted capture event and its proven product target that same route.
bool IsHandoffStableLiveCandidate(
    const HandoffStableLiveCandidateInputs& inputs);

bool ShouldUseHandoffPresentation(
    const HandoffPresentationEligibilityInputs& inputs);

struct WholeSceneCaptureRequest
{
    WholeSceneCaptureBackedPlanRole Role = WholeSceneCaptureBackedPlanRole::None;
    WholeSceneCaptureRequestKind Kind = WholeSceneCaptureRequestKind::None;
    int RouteSlot = -1;
    u32 CaptureBank = 0xFFFFFFFFu;
    u64 CaptureEventSerial = 0;
    u64 BackgroundEpochSerial = 0;
    u32 CapturePresentationHash = 0;
    u32 CurrentPresentationHash = 0;
    int YStart = 0;
    int YEnd = 192;
    bool DirectFinalBottomConsumer = false;
};

struct WholeSceneCaptureProductRef
{
    WholeSceneCaptureProductKind Kind = WholeSceneCaptureProductKind::None;
    SourceABackgroundSource BackgroundSource = SourceABackgroundSource::None;
    int RouteSlot = -1;
    u32 CaptureBank = 0xFFFFFFFFu;
    u64 CaptureEventSerial = 0;
    u64 BackgroundEpochSerial = 0;
    u32 CapturePresentationHash = 0;
    u32 CurrentPresentationHash = 0;
};

// Pre-execution authority carried with a representation candidate. Content
// identity and presentation/effect completeness are intentionally separate:
// capture causality can be proven while downstream effect ownership remains
// debt, and neither proof is inferred from post-execution texture observation.
struct WholeSceneCaptureRepresentationEvidence
{
    bool Available = false;
    bool ContentProven = false;
    bool PresentationProven = false;
    WholeSceneCaptureProductRef ProductRef;
    WholeSceneCaptureProofKind ProofKind = WholeSceneCaptureProofKind::None;
    WholeSceneCaptureProofKind AuthorizationProofKind =
        WholeSceneCaptureProofKind::None;
    u64 AuthorizationEpochSerial = 0;
    u32 AuthorizationCaptureBank = 0xFFFFFFFFu;
    u32 AuthorizationPresentationHash = 0;
    u64 Source3DSerial = 0;
    u32 Source3DSceneHash = 0;
    u32 SourceKind = 0;
    u32 ProductMask = 0;
    WholeSceneCaptureEffectOwner OutputEffectOwner =
        WholeSceneCaptureEffectOwner::None;
    u32 OutputEffectState = 0;
    bool OutputPreMaster = false;
    bool CaptureInputProven = false;
    u32 CaptureCnt = 0;
    bool CaptureConsumesSelectedPresentation = false;
    bool CaptureUsesSourceB = false;
    bool CanPublishRouteProduct = false;
};

struct WholeSceneCaptureBackedPlan
{
    WholeSceneCaptureBackedPlanKind Kind = WholeSceneCaptureBackedPlanKind::None;
    WholeSceneCaptureBackedPlanStage Stage = WholeSceneCaptureBackedPlanStage::None;
    WholeSceneCaptureBackedPlanRole Role = WholeSceneCaptureBackedPlanRole::None;
    WholeSceneCaptureRequestKind RequestKind = WholeSceneCaptureRequestKind::None;
    int CaptureEpochOverlayRouteSlot = -1;
    bool CanRunDuringHybridPresentationGuard = false;
    WholeSceneCaptureRepresentationEvidence Evidence;
};

WholeSceneCaptureBackedPlan MakeWholeSceneCaptureBackedHandoffPlan();

WholeSceneCaptureBackedPlan MakeWholeSceneSourceACaptureReplacementPlan();

WholeSceneCaptureBackedPlan MakeWholeSceneCaptureEpochOverlayPlan(
    int routeSlot,
    bool canRunDuringHybridPresentationGuard = true);

struct WholeSceneCapturePolicyResult
{
    bool Accepted = false;
    WholeSceneCaptureProductRef ProductRef;
    WholeSceneCaptureAuthority Authority = WholeSceneCaptureAuthority::None;
    WholeSceneCaptureProofKind ProofKind = WholeSceneCaptureProofKind::None;
    WholeSceneCaptureRenderAction RenderAction = WholeSceneCaptureRenderAction::None;
    WholeSceneCaptureProductPresentationClass PresentationClass =
        WholeSceneCaptureProductPresentationClass::None;
};

struct SourceACaptureResolution
{
    WholeSceneCaptureRequest Request;
    WholeSceneCapturePolicyResult Result;
};

struct HandoffCaptureResolution
{
    WholeSceneCaptureRequest Request;
    WholeSceneCapturePolicyResult Result;
};

WholeSceneCaptureRequest MakeWholeSceneCaptureRequest(
    WholeSceneCaptureBackedPlanRole role,
    WholeSceneCaptureRequestKind kind,
    int ystart,
    int yend,
    int routeSlot = -1);

void FillWholeSceneCaptureRequestIdentity(
    WholeSceneCaptureRequest& request,
    u32 captureBank = 0xFFFFFFFFu,
    u64 captureEventSerial = 0,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0,
    bool directFinalBottomConsumer = false);

WholeSceneCaptureRequest MakeWholeSceneCaptureRequestWithIdentity(
    WholeSceneCaptureBackedPlanRole role,
    WholeSceneCaptureRequestKind kind,
    int ystart,
    int yend,
    int routeSlot = -1,
    u32 captureBank = 0xFFFFFFFFu,
    u64 captureEventSerial = 0,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0,
    bool directFinalBottomConsumer = false);

WholeSceneCaptureRequest MakeWholeSceneCaptureRequestWithIdentity(
    const WholeSceneCaptureRequest& baseRequest,
    u32 captureBank = 0xFFFFFFFFu,
    u64 captureEventSerial = 0,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0,
    bool directFinalBottomConsumer = false);

WholeSceneCaptureRequest MakeCapturedLayerConsumerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank = 0xFFFFFFFFu,
    u64 captureEventSerial = 0,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0,
    bool directFinalBottomConsumer = false);

WholeSceneCaptureRequest MakeDirectFinalConsumerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank = 0xFFFFFFFFu,
    u64 captureEventSerial = 0,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0,
    bool directFinalBottomConsumer = true);

WholeSceneCaptureRequest MakeLiveOverlayProducerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank = 0xFFFFFFFFu,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0);

WholeSceneCaptureRequest MakeHandoffConsumerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot = -1);

WholeSceneCaptureRequest MakeHandoffConsumerCaptureRequest(
    const WholeSceneCaptureRequest& baseRequest,
    u32 captureBank = 0xFFFFFFFFu,
    u64 captureEventSerial = 0,
    u64 backgroundEpochSerial = 0,
    u32 capturePresentationHash = 0,
    u32 currentPresentationHash = 0);

WholeSceneCapturePolicyResult MakeWholeSceneCapturePolicyResult(
    WholeSceneCaptureProductKind product,
    WholeSceneCaptureProofKind proof,
    WholeSceneCaptureRenderAction action,
    WholeSceneCaptureProductPresentationClass presentationClass,
    WholeSceneCaptureAuthority authority = WholeSceneCaptureAuthority::None,
    SourceABackgroundSource backgroundSource = SourceABackgroundSource::None,
    bool accepted = true);

WholeSceneCapturePolicyResult MakeRouteProductCapturePolicyResult(
    CaptureBackedRouteProductLookupSource source,
    WholeSceneCaptureAuthority authority);

WholeSceneCapturePolicyResult MakeRouteProductCapturePolicyResult(
    WholeSceneCaptureProductKind product,
    WholeSceneCaptureProofKind proof,
    WholeSceneCaptureAuthority authority);

WholeSceneCapturePolicyResult MakeBackgroundCapturePolicyResult(
    SourceABackgroundSource backgroundSource,
    WholeSceneCaptureRenderAction action,
    WholeSceneCaptureAuthority authority,
    WholeSceneCaptureProofKind proofOverride = WholeSceneCaptureProofKind::None);

WholeSceneCapturePolicyResult MakeFullProductCapturePolicyResult(
    WholeSceneCaptureAuthority authority,
    WholeSceneCaptureProofKind proof = WholeSceneCaptureProofKind::ExactCaptureEvent);

WholeSceneCapturePolicyResult MakeRejectedCapturePolicyResult(
    WholeSceneCaptureRenderAction action);

void AttachCaptureRequestIdentityToProductRef(
    WholeSceneCapturePolicyResult& result,
    const WholeSceneCaptureRequest& request);

}
