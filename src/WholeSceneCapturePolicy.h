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

enum class WholeSceneCaptureEffectAction : u8
{
    None = 0,
    DisplayAsIs = 1,
    ApplyOnBlit = 2,
    CompositeCurrentOverlay = 3,
    NeedsRePresentation = 4,
    Fallback = 5,
    Reject = 6,
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
    bool RouteProductNeedsRePresentation = false;
    bool CanUseCurrentOverlay = false;
    bool HasFullProduct = false;
    bool PreferExactFullProduct = false;
    bool AllowCurrentOverlay = true;
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
    bool HasStoredEffectState = false;
    bool StoredEffectActive = false;
    bool ConsumeEffectActive = false;
};

struct WholeSceneCaptureProductUseDecision
{
    bool Accepted = false;
    bool PresentationCompatible = false;
    bool RequiresRePresentation = false;
    bool EffectPhaseIncompatible = false;
};

bool IsMasterBrightnessEffectActive(u16 masterBrightness);

// Packed master-brightness-style state (mode<<14 | factor) for the consuming
// engine's frame-global BLDCNT brightness color effect, or 0 when BLDCNT does
// not select one. A direct-final capture-backed replacement bypasses the
// consuming engine's compositor, which is the stage that natively applies
// this effect, so the blit must reproduce it.
u16 ConsumerFullScreenBrightnessColorEffect(u16 blendCnt, u8 evy);

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

WholeSceneCaptureProductPresentationClass CaptureProductPresentationClassForProduct(
    WholeSceneCaptureProductKind product,
    WholeSceneCaptureRenderAction action);

bool ShouldApplySourceAReplacementPresentationEffect(
    WholeSceneCaptureProductPresentationClass productClass,
    WholeSceneCaptureRequestKind requestKind,
    bool sourceEngineIsSub);

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
    u32 StableFrames = 0;
    WholeSceneCaptureProductPresentationClass PresentationClass =
        WholeSceneCaptureProductPresentationClass::RawContent;
    // Meaningful only when HasStoredEffectState is true. Raw route products
    // are stored before final master brightness and should not claim this.
    u16 StoredMasterBrightness = 0;
    bool HasStoredEffectState = false;
};

struct CaptureBackedRouteEventProductState
{
    bool Valid = false;
    CaptureBackedRouteProductIdentity Identity;
    u64 CapturedEventSerial = 0;
    u32 StableFrames = 0;
    WholeSceneCaptureProductPresentationClass PresentationClass =
        WholeSceneCaptureProductPresentationClass::RawContent;
    u16 StoredMasterBrightness = 0;
    bool HasStoredEffectState = false;
};

struct CaptureBackedRoutePendingEventState
{
    bool Valid = false;
    u64 CaptureEventSerial = 0;
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

SourceACaptureResolutionKind ChooseSourceACaptureResolutionKind(
    const SourceACaptureResolutionInputs& inputs);

bool ShouldPreferSourceAExactFullProductForDirectBottom(
    const SourceAExactFullProductPreferenceInputs& inputs);

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

struct WholeSceneCaptureBackedPlan
{
    WholeSceneCaptureBackedPlanKind Kind = WholeSceneCaptureBackedPlanKind::None;
    WholeSceneCaptureBackedPlanStage Stage = WholeSceneCaptureBackedPlanStage::None;
    WholeSceneCaptureBackedPlanRole Role = WholeSceneCaptureBackedPlanRole::None;
    WholeSceneCaptureRequestKind RequestKind = WholeSceneCaptureRequestKind::None;
    WholeSceneCaptureProductKind ProductKind = WholeSceneCaptureProductKind::None;
    WholeSceneCaptureProofKind ProofKind = WholeSceneCaptureProofKind::None;
    WholeSceneCaptureRenderAction RenderAction = WholeSceneCaptureRenderAction::None;
    int CaptureEpochOverlayRouteSlot = -1;
    bool CanRunDuringHybridPresentationGuard = false;
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
