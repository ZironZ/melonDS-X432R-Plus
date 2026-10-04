// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WholeSceneCapturePolicy.h"

namespace melonDS
{

bool HasCaptureBackedRouteProductSource3DIdentity(
    const CaptureBackedRouteProductIdentity& identity)
{
    return identity.Source3DSerial != 0 &&
           identity.Source3DSceneHash != 0;
}

bool HasCaptureBackedRouteProductBackgroundEpochIdentity(
    const CaptureBackedRouteProductIdentity& identity)
{
    return identity.BackgroundEpochSerial != 0;
}

bool IsValidCaptureBackedRouteProductIdentity(
    const CaptureBackedRouteProductIdentity& identity)
{
    return (HasCaptureBackedRouteProductSource3DIdentity(identity) ||
            HasCaptureBackedRouteProductBackgroundEpochIdentity(identity)) &&
           identity.CaptureBank < 4 &&
           identity.CapturePresentationHash != 0 &&
           identity.CurrentOverlayPresentationHash != 0;
}

bool SameCaptureBackedRouteProductIdentity(
    const CaptureBackedRouteProductIdentity& a,
    const CaptureBackedRouteProductIdentity& b)
{
    const bool source3DMatches =
        HasCaptureBackedRouteProductSource3DIdentity(a) &&
        HasCaptureBackedRouteProductSource3DIdentity(b) &&
        a.Source3DSerial == b.Source3DSerial &&
        a.Source3DSceneHash == b.Source3DSceneHash;
    const bool backgroundEpochMatches =
        HasCaptureBackedRouteProductBackgroundEpochIdentity(a) &&
        a.BackgroundEpochSerial == b.BackgroundEpochSerial &&
        (!HasCaptureBackedRouteProductSource3DIdentity(a) ||
         !HasCaptureBackedRouteProductSource3DIdentity(b));

    return a.BackgroundEpochSerial == b.BackgroundEpochSerial &&
           (source3DMatches || backgroundEpochMatches) &&
           a.CaptureBank == b.CaptureBank &&
           a.CapturePresentationHash == b.CapturePresentationHash &&
           a.CurrentOverlayPresentationHash == b.CurrentOverlayPresentationHash;
}

bool CaptureBackedRouteProductIdentityMatchesEvent(
    const CaptureBackedRouteProductIdentity& identity,
    u64 captureEventSerial,
    u64 source3DSerial,
    u32 source3DSceneHash)
{
    if (!IsValidCaptureBackedRouteProductIdentity(identity) ||
        captureEventSerial == 0)
    {
        return false;
    }

    if (HasCaptureBackedRouteProductSource3DIdentity(identity))
    {
        return source3DSerial != 0 &&
               source3DSceneHash != 0 &&
               identity.Source3DSerial == source3DSerial &&
               identity.Source3DSceneHash == source3DSceneHash;
    }

    return identity.BackgroundEpochSerial == captureEventSerial;
}

CaptureEpochOverlayCurrentPlan MakeCaptureEpochOverlayCurrentPlan(
    const CaptureEpochOverlayCurrentInputs& inputs)
{
    CaptureEpochOverlayCurrentPlan plan = {};
    plan.Source3DSerial = inputs.CurrentSource3DSerial;
    plan.Source3DSceneHash = inputs.CurrentSource3DSceneHash;
    plan.PresentationHash = inputs.CurrentPresentationHash;

    const bool hasCurrentIdentity =
        plan.Source3DSerial != 0 &&
        plan.Source3DSceneHash != 0 &&
        plan.PresentationHash != 0;
    if (!hasCurrentIdentity)
        return plan;

    if (inputs.CaptureRequestConsumesCurrentComposite &&
        inputs.CaptureRequestBank < 4)
    {
        plan.CanPublishRouteProduct = true;
        plan.CaptureBank = inputs.CaptureRequestBank;
        return plan;
    }

    const bool currentMatchesEpoch =
        inputs.EpochCaptureBank < 4 &&
        inputs.EpochSource3DSerial != 0 &&
        inputs.EpochSource3DSceneHash != 0 &&
        inputs.CurrentSource3DSerial == inputs.EpochSource3DSerial &&
        inputs.CurrentSource3DSceneHash == inputs.EpochSource3DSceneHash;
    if (currentMatchesEpoch)
    {
        plan.CanPublishRouteProduct = true;
        plan.CaptureBank = inputs.EpochCaptureBank;
    }

    return plan;
}

CaptureBackedRouteProductEventQuery MakeCaptureBackedRouteProductEventQuery(
    int routeSlot,
    u64 captureEventSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 captureBank,
    u32 capturePresentationHash,
    int ystart,
    int yend)
{
    CaptureBackedRouteProductEventQuery query = {};
    query.RouteSlot = routeSlot;
    query.CaptureEventSerial = captureEventSerial;
    query.Source3DSerial = source3DSerial;
    query.Source3DSceneHash = source3DSceneHash;
    query.CaptureBank = captureBank;
    query.CapturePresentationHash = capturePresentationHash;
    query.YStart = ystart;
    query.YEnd = yend;
    return query;
}

CaptureBackedRouteProductStateQuery MakeCaptureBackedRouteProductStateQuery(
    int routeSlot,
    u64 backgroundEpochSerial,
    u64 source3DSerial,
    u32 source3DSceneHash,
    u32 captureBank,
    u32 capturePresentationHash,
    u32 currentOverlayPresentationHash,
    int ystart,
    int yend)
{
    CaptureBackedRouteProductStateQuery query = {};
    query.RouteSlot = routeSlot;
    query.Identity.BackgroundEpochSerial = backgroundEpochSerial;
    query.Identity.Source3DSerial = source3DSerial;
    query.Identity.Source3DSceneHash = source3DSceneHash;
    query.Identity.CaptureBank = captureBank;
    query.Identity.CapturePresentationHash = capturePresentationHash;
    query.Identity.CurrentOverlayPresentationHash = currentOverlayPresentationHash;
    query.YStart = ystart;
    query.YEnd = yend;
    return query;
}

bool IsCaptureBackedRouteProductEventQueryUsable(
    const CaptureBackedRouteProductEventQuery& query,
    int routeSlotCount,
    bool hasRouteProductTexture)
{
    return query.RouteSlot >= 0 &&
           query.RouteSlot < routeSlotCount &&
           query.YStart == 0 &&
           query.YEnd == 192 &&
           query.CaptureEventSerial != 0 &&
           query.CaptureBank < 4 &&
           query.CapturePresentationHash != 0 &&
           hasRouteProductTexture;
}

bool DoesCaptureBackedRouteEventProductMatchQuery(
    const CaptureBackedRouteEventProductState& product,
    const CaptureBackedRouteProductEventQuery& query)
{
    return product.Valid &&
           product.CapturedEventSerial == query.CaptureEventSerial &&
           CaptureBackedRouteProductIdentityMatchesEvent(product.Identity,
                                                         query.CaptureEventSerial,
                                                         query.Source3DSerial,
                                                         query.Source3DSceneHash) &&
           product.Identity.CaptureBank == query.CaptureBank &&
           product.Identity.CurrentOverlayPresentationHash ==
               query.CapturePresentationHash;
}

bool DoesCaptureBackedRouteProductMatchEventQuery(
    const CaptureBackedRouteProductState& product,
    const CaptureBackedRouteProductEventQuery& query)
{
    return product.Valid &&
           product.CapturedEventSerial == query.CaptureEventSerial &&
           CaptureBackedRouteProductIdentityMatchesEvent(product.Identity,
                                                         query.CaptureEventSerial,
                                                         query.Source3DSerial,
                                                         query.Source3DSceneHash) &&
           product.Identity.CaptureBank == query.CaptureBank &&
           product.Identity.CurrentOverlayPresentationHash ==
               query.CapturePresentationHash;
}

bool DoesCaptureBackedRouteProductMatchSource3DSceneQuery(
    const CaptureBackedRouteProductState& product,
    const CaptureBackedRouteProductEventQuery& query)
{
    return product.Valid &&
           query.Source3DSceneHash != 0 &&
           HasCaptureBackedRouteProductSource3DIdentity(product.Identity) &&
           product.Identity.Source3DSceneHash == query.Source3DSceneHash &&
           product.Identity.CaptureBank == query.CaptureBank &&
           product.Identity.CurrentOverlayPresentationHash ==
               query.CapturePresentationHash;
}

bool ShouldApplyHandoffPresentationEffect(
    WholeSceneCaptureProductPresentationClass productClass,
    WholeSceneCaptureRequestKind requestKind)
{
    return requestKind == WholeSceneCaptureRequestKind::HandoffConsumer &&
           productClass == WholeSceneCaptureProductPresentationClass::RawContent;
}

u16 ConsumerFullScreenBrightnessColorEffect(u16 blendCnt, u8 evy)
{
    // Only BLDCNT modes 2 (increase) and 3 (decrease) are brightness effects,
    // and they are only frame-global when every first-target bit including
    // the backdrop is set. A partial-target brightness effect must not become
    // a full-output transform.
    const u32 mode = (blendCnt >> 6) & 0x3;
    if (mode != 2 && mode != 3)
        return 0;
    if ((blendCnt & 0x3F) != 0x3F)
        return 0;

    const u32 factor = evy > 16 ? 16 : evy;
    if (factor == 0)
        return 0;

    // Same math as master brightness up/down, so reuse its packing:
    // BLDCNT mode 2 (increase) -> brightness mode 1, mode 3 -> mode 2.
    const u32 brightnessMode = (mode == 2) ? 1 : 2;
    return static_cast<u16>((brightnessMode << 14) | factor);
}

bool IsGuaranteedFullScreenBrightnessEndpoint(
    u16 blendCnt,
    u8 evy,
    bool windowingActive)
{
    const u32 secondTargets = (blendCnt >> 8) & 0x3F;
    if (evy < 16 || windowingActive || secondTargets != 0)
        return false;

    return ConsumerFullScreenBrightnessColorEffect(blendCnt, 16) != 0;
}

bool DoesCaptureProductPresentationMatchRequest(
    u32 productPresentationHash,
    u32 requestPresentationHash)
{
    return productPresentationHash != 0 &&
           requestPresentationHash != 0 &&
           productPresentationHash == requestPresentationHash;
}

bool CanUseCaptureProductAsPresented(
    WholeSceneCaptureProductPresentationClass productClass,
    u32 productPresentationHash,
    u32 requestPresentationHash)
{
    if (productClass == WholeSceneCaptureProductPresentationClass::RawContent)
        return true;

    if (productClass == WholeSceneCaptureProductPresentationClass::AlreadyPresented)
    {
        return DoesCaptureProductPresentationMatchRequest(productPresentationHash,
                                                          requestPresentationHash);
    }

    return false;
}

bool IsStorableCaptureBackedRouteProductClass(
    WholeSceneCaptureProductPresentationClass productClass)
{
    return productClass == WholeSceneCaptureProductPresentationClass::RawContent ||
           productClass == WholeSceneCaptureProductPresentationClass::AlreadyPresented;
}

WholeSceneCaptureProductUseDecision CanUseWholeSceneCaptureProduct(
    const WholeSceneCaptureProductUseInputs& inputs)
{
    WholeSceneCaptureProductUseDecision decision = {};

    const bool requestRangeValid =
        inputs.RequestYStart >= 0 &&
        inputs.RequestYEnd <= 192 &&
        inputs.RequestYStart < inputs.RequestYEnd;
    const bool productRangeValid =
        inputs.ProductYStart >= 0 &&
        inputs.ProductYEnd <= 192 &&
        inputs.ProductYStart < inputs.ProductYEnd;
    const bool productCoversRequest =
        productRangeValid && requestRangeValid &&
        inputs.ProductYStart <= inputs.RequestYStart &&
        inputs.ProductYEnd >= inputs.RequestYEnd;
    const bool fullFrameOverlayScope =
        inputs.RenderAction !=
            WholeSceneCaptureRenderAction::CompositeCurrentOverlay ||
        (inputs.RequestYStart == 0 && inputs.RequestYEnd == 192);
    decision.RowScopeCompatible =
        productCoversRequest && fullFrameOverlayScope;

    if (!inputs.PolicyAccepted ||
        !inputs.HasTexture ||
        inputs.ProductKind == WholeSceneCaptureProductKind::None ||
        inputs.RenderAction == WholeSceneCaptureRenderAction::None ||
        !decision.RowScopeCompatible)
    {
        return decision;
    }

    switch (inputs.PresentationClass)
    {
    case WholeSceneCaptureProductPresentationClass::RawContent:
        decision.Accepted = true;
        // Raw products carry content, not a completed presentation. Their
        // downstream transforms are owned by the selected OutputPlan, so a
        // presentation-hash comparison cannot prove compatibility here.
        decision.PresentationCompatible = true;
        return decision;
    case WholeSceneCaptureProductPresentationClass::AlreadyPresented:
        decision.PresentationCompatible =
            DoesCaptureProductPresentationMatchRequest(inputs.ProductPresentationHash,
                                                       inputs.RequestPresentationHash);
        decision.Accepted = decision.PresentationCompatible;
        return decision;
    case WholeSceneCaptureProductPresentationClass::None:
    case WholeSceneCaptureProductPresentationClass::Fallback:
    case WholeSceneCaptureProductPresentationClass::Unknown:
        return decision;
    }

    return decision;
}

WholeSceneCaptureProductKind CaptureProductKindForRouteLookup(
    CaptureBackedRouteProductLookupSource source)
{
    switch (source)
    {
    case CaptureBackedRouteProductLookupSource::ExactEventProduct:
        return WholeSceneCaptureProductKind::RouteEventProduct;
    case CaptureBackedRouteProductLookupSource::ExactEventRouteProduct:
        return WholeSceneCaptureProductKind::RouteProduct;
    case CaptureBackedRouteProductLookupSource::RouteStateProduct:
    case CaptureBackedRouteProductLookupSource::Source3DSceneProduct:
        return WholeSceneCaptureProductKind::RouteStateProduct;
    case CaptureBackedRouteProductLookupSource::None:
        return WholeSceneCaptureProductKind::None;
    }

    return WholeSceneCaptureProductKind::None;
}

WholeSceneCaptureProofKind CaptureProofKindForRouteLookup(
    CaptureBackedRouteProductLookupSource source)
{
    switch (source)
    {
    case CaptureBackedRouteProductLookupSource::ExactEventProduct:
    case CaptureBackedRouteProductLookupSource::ExactEventRouteProduct:
        return WholeSceneCaptureProofKind::ExactCaptureEvent;
    case CaptureBackedRouteProductLookupSource::RouteStateProduct:
        return WholeSceneCaptureProofKind::RouteStateIdentity;
    case CaptureBackedRouteProductLookupSource::Source3DSceneProduct:
        return WholeSceneCaptureProofKind::Source3DSceneIdentity;
    case CaptureBackedRouteProductLookupSource::None:
        return WholeSceneCaptureProofKind::None;
    }

    return WholeSceneCaptureProofKind::None;
}

WholeSceneCaptureProductKind CaptureProductKindForBackgroundSource(
    SourceABackgroundSource source)
{
    switch (source)
    {
    case SourceABackgroundSource::ActiveCaptureEpochTex:
    case SourceABackgroundSource::CaptureEventBackgroundTex:
        return WholeSceneCaptureProductKind::BackgroundProduct;
    case SourceABackgroundSource::ParentOutputTex3D:
        return WholeSceneCaptureProductKind::ParentOutput3D;
    case SourceABackgroundSource::RouteProduct:
        return WholeSceneCaptureProductKind::RouteProduct;
    case SourceABackgroundSource::FullCaptureProduct:
        return WholeSceneCaptureProductKind::FullCaptureProduct;
    case SourceABackgroundSource::HandoffSnapshot:
        return WholeSceneCaptureProductKind::HandoffSnapshot;
    case SourceABackgroundSource::None:
        return WholeSceneCaptureProductKind::None;
    }

    return WholeSceneCaptureProductKind::None;
}

WholeSceneCaptureProofKind CaptureProofKindForBackgroundSource(
    SourceABackgroundSource source)
{
    switch (source)
    {
    case SourceABackgroundSource::ActiveCaptureEpochTex:
    case SourceABackgroundSource::CaptureEventBackgroundTex:
        return WholeSceneCaptureProofKind::ActiveBackgroundEpoch;
    case SourceABackgroundSource::ParentOutputTex3D:
    case SourceABackgroundSource::RouteProduct:
        return WholeSceneCaptureProofKind::RouteStateIdentity;
    case SourceABackgroundSource::FullCaptureProduct:
        return WholeSceneCaptureProofKind::ExactCaptureEvent;
    case SourceABackgroundSource::HandoffSnapshot:
        return WholeSceneCaptureProofKind::HandoffRouteKey;
    case SourceABackgroundSource::None:
        return WholeSceneCaptureProofKind::None;
    }

    return WholeSceneCaptureProofKind::None;
}

static SourceACaptureResolutionKind ChooseSourceACaptureResolutionKindForInputs(
    const SourceACaptureResolutionInputs& inputs,
    SourceACaptureSelectionReason& reason)
{
    if (inputs.Preference ==
            SourceACaptureSelectionPreference::ExactFullProduct &&
        inputs.HasFullProduct)
    {
        reason = SourceACaptureSelectionReason::ExactFullProductPreference;
        return SourceACaptureResolutionKind::FullProduct;
    }

    if (inputs.Preference ==
            SourceACaptureSelectionPreference::ExactRouteProduct &&
        inputs.HasRouteProduct)
    {
        reason = SourceACaptureSelectionReason::ExactRouteProductPreference;
        return SourceACaptureResolutionKind::RouteProduct;
    }

    if (inputs.HasRouteProduct &&
        inputs.RouteProductCompositionMismatch &&
        inputs.AllowCurrentOverlay &&
        inputs.CanUseCurrentOverlay)
    {
        reason = SourceACaptureSelectionReason::RouteProductCompositionRebuild;
        return SourceACaptureResolutionKind::BackgroundOverlay;
    }

    if (inputs.HasRouteProduct)
    {
        reason = SourceACaptureSelectionReason::RouteProductAvailable;
        return SourceACaptureResolutionKind::RouteProduct;
    }

    if (inputs.AllowCurrentOverlay && inputs.CanUseCurrentOverlay)
    {
        reason = SourceACaptureSelectionReason::CurrentOverlayAvailable;
        return SourceACaptureResolutionKind::BackgroundOverlay;
    }

    if (!inputs.HasFullProduct)
    {
        reason = SourceACaptureSelectionReason::NoUsableProduct;
        return SourceACaptureResolutionKind::RejectedFallback;
    }

    reason = SourceACaptureSelectionReason::FullProductAvailable;
    return SourceACaptureResolutionKind::FullProduct;
}

SourceACaptureSelectionDecision ChooseSourceACaptureSelectionDecision(
    const SourceACaptureResolutionInputs& inputs)
{
    SourceACaptureSelectionDecision decision = {};
    decision.FullProductPresentationProof =
        inputs.FullProductPresentationProof;
    decision.Primary = ChooseSourceACaptureResolutionKindForInputs(
        inputs, decision.PrimaryReason);

    SourceACaptureResolutionInputs afterOverlayFailure = inputs;
    afterOverlayFailure.AllowCurrentOverlay = false;
    decision.AfterOverlayFailure =
        ChooseSourceACaptureResolutionKindForInputs(
            afterOverlayFailure, decision.AfterOverlayFailureReason);
    return decision;
}

SourceAFullProductPresentationProof ProveSourceAFullProductPresentation(
    const SourceAFullProductPresentationProofInputs& inputs)
{
    const bool directFinalConsumer =
        inputs.DirectFinalBottomConsumer ||
        (inputs.DirectFinalDisplayConsumer && inputs.SubEngineCapturedOBJOnly) ||
        (inputs.DirectFinalDisplayConsumer &&
         inputs.MainEngineCapturedBGOnly &&
         inputs.FullProductEventRouteMatches);
    const bool captureBackedSource =
        inputs.SubEngineCapturedSourceAOnly ||
        inputs.MainEngineCapturedBGOnly;
    if (!directFinalConsumer ||
        !captureBackedSource ||
        !inputs.HasFullProduct ||
        !inputs.FullProductEventValid ||
        !inputs.FullProductEventFullEquivalent ||
        !inputs.FullProductEventCleanEngineA2DOutput ||
        !inputs.FullProductEventAccepted)
    {
        return SourceAFullProductPresentationProof::None;
    }

    return SourceAFullProductPresentationProof::ExactFullEquivalentDirectFinal;
}

static bool CanPreferSourceAExactRouteProductForDirectBottom(
    const SourceAExactRouteProductPreferenceInputs& inputs)
{
    if (!inputs.DirectFinalBottomConsumer ||
        !inputs.SubEngineCaptureBackedBGOnly ||
        !inputs.HasRouteProduct ||
        inputs.RouteProductKind != WholeSceneCaptureProductKind::RouteEventProduct ||
        inputs.RouteProductProof != WholeSceneCaptureProofKind::ExactCaptureEvent ||
        !inputs.HasFullProduct ||
        !inputs.FullProductEventValid ||
        !inputs.FullProductEventFullEquivalent ||
        !inputs.FullProductEventCleanEngineA2DOutput ||
        !inputs.FullProductEventAccepted ||
        inputs.FullProductEventSourceOBJVisible)
    {
        return false;
    }

    if (inputs.RouteProductEventSerial == 0 ||
        inputs.RouteProductEventSerial != inputs.FullProductEventSerial ||
        inputs.RouteProductCaptureBank >= 4 ||
        inputs.RouteProductCaptureBank != inputs.FullProductCaptureBank ||
        inputs.RouteProductCapturePresentationHash == 0 ||
        inputs.RouteProductCapturePresentationHash !=
            inputs.FullProductCapturePresentationHash)
    {
        return false;
    }

    const bool routeHasSource3D =
        inputs.RouteProductSource3DSerial != 0 ||
        inputs.RouteProductSource3DSceneHash != 0;
    const bool fullHasSource3D =
        inputs.FullProductSource3DSerial != 0 ||
        inputs.FullProductSource3DSceneHash != 0;
    if (routeHasSource3D != fullHasSource3D)
        return false;

    if (routeHasSource3D &&
        (inputs.RouteProductSource3DSerial == 0 ||
         inputs.RouteProductSource3DSceneHash == 0 ||
         inputs.RouteProductSource3DSerial != inputs.FullProductSource3DSerial ||
         inputs.RouteProductSource3DSceneHash != inputs.FullProductSource3DSceneHash))
    {
        return false;
    }

    return true;
}

static bool CanPreferSourceAExactFullProductForDirectBottom(
    const SourceAExactFullProductPreferenceInputs& inputs)
{
    if (!inputs.DirectFinalBottomConsumer ||
        !inputs.SubEngineCapturedSourceAOnly ||
        !inputs.HasFullProduct ||
        !inputs.FullProductEventValid ||
        !inputs.FullProductEventFullEquivalent ||
        !inputs.FullProductEventCleanEngineA2DOutput ||
        !inputs.FullProductEventAccepted)
    {
        return false;
    }

    if (inputs.FullProductKeyMatch &&
        inputs.FullProductEventRouteMatches)
    {
        return true;
    }

    return inputs.FullProductEventSourceOBJVisible;
}

SourceACaptureSelectionPreference ChooseSourceAExactProductPreference(
    const SourceAExactFullProductPreferenceInputs& fullProductInputs,
    const SourceAExactRouteProductPreferenceInputs& routeProductInputs)
{
    if (CanPreferSourceAExactFullProductForDirectBottom(fullProductInputs))
        return SourceACaptureSelectionPreference::ExactFullProduct;

    if (CanPreferSourceAExactRouteProductForDirectBottom(routeProductInputs))
        return SourceACaptureSelectionPreference::ExactRouteProduct;

    return SourceACaptureSelectionPreference::None;
}

bool DoesDirectFinalRouteMatch(
    const DirectFinalRouteMatchInputs& inputs)
{
    return inputs.EventScreenSwap == inputs.CurrentScreenSwap &&
           inputs.EventMainFinalBottom == inputs.CurrentMainFinalBottom;
}

bool CanUseSourceACurrentOverlay(
    const SourceACurrentOverlayEligibilityInputs& inputs)
{
    return inputs.HasBackgroundTexture &&
           !inputs.PreferExactFullProductForDirectBottom &&
           inputs.SourceIsCleanEngineA2DOutput &&
           inputs.RendererCanCompositeCurrentOverlay &&
           !inputs.SubEngineDirectFinalTopConsumer;
}

bool CanScaleMainVRAMDisplayCaptureSourceA(
    const MainVRAMDisplayCaptureScaleInputs& inputs)
{
    if (!inputs.MainEngine || inputs.DisplayMode != 2)
        return false;
    if (!inputs.ConservativeHybridMode)
        return false;
    if (!inputs.CaptureBackedScalingEnabled)
        return false;

    if (!inputs.CaptureEnabled)
        return inputs.HasAcceptedDisplayReplacement;

    const u32 srcA = (inputs.CaptureCnt >> 24) & 0x1;
    if (srcA != 0)
        return inputs.HasAcceptedDisplayReplacement;

    const u32 capsize = (inputs.CaptureCnt >> 20) & 0x3;
    if (capsize != 3)
        return inputs.HasAcceptedDisplayReplacement;

    const u32 dstblock = (inputs.CaptureCnt >> 16) & 0x3;
    if (dstblock != inputs.DisplayBank)
        return inputs.HasAcceptedDisplayReplacement;

    const u32 dstmode = (inputs.CaptureCnt >> 29) & 0x3;
    u32 eva = inputs.CaptureCnt & 0x1F;
    if (eva > 16)
        eva = 16;

    if (dstmode == 0)
        return true;
    return ((dstmode == 2 || dstmode == 3) && eva > 0) ||
           inputs.HasAcceptedDisplayReplacement;
}

bool CanUseMainVRAMDisplayExactEventReplacement(
    const MainVRAMDisplayExactEventReplacementInputs& inputs)
{
    return inputs.EventRecordValid &&
           inputs.EventDstOffsetZero &&
           inputs.EventAccepted &&
           inputs.EventFullEquivalent &&
           inputs.EventAcceptedSource &&
           inputs.HasFullTexture &&
           inputs.EventRouteMatches &&
           (!inputs.EventSourceOBJVisible || inputs.EventSourceRenderedFullWholeScene);
}

bool DoesMainVRAMDisplayEventMatchNativeCapture(
    const MainVRAMDisplayEventMatchInputs& inputs)
{
    return inputs.NativeCaptureRecordValid &&
           inputs.HighResEventRecordValid &&
           inputs.NativeCaptureDstOffset == inputs.HighResEventDstOffset &&
           inputs.NativeCaptureCnt == inputs.HighResEventCaptureCnt &&
           inputs.HighResEventDstOffset == 0;
}

bool IsMainVRAMDisplayMixedOrOBJReject(
    const MainVRAMDisplayMixedOrOBJRejectInputs& inputs)
{
    return inputs.DirtyOrPartialSourceReject &&
           inputs.NativeOnlyOutput2DSource &&
           inputs.SourceDirect3DVisible &&
           inputs.SourceHasNoVisibleBitmap &&
           inputs.SourceBG0Visible &&
           (inputs.SourceOBJVisible || inputs.SourceOtherBGVisible);
}

int MainVRAMDisplayEpochReplacementRejectReason(
    const MainVRAMDisplayEpochReplacementInputs& inputs)
{
    if (!inputs.EpochValid ||
        inputs.EpochCaptureBank != inputs.DisplayBank ||
        inputs.EpochDstOffset != 0)
    {
        return 9;
    }

    if (inputs.EpochHasFullDirtyRows)
        return 10;

    if (!inputs.EpochFullEquivalent)
        return 6;

    if (!inputs.EpochAcceptedSource)
        return 7;

    if (!inputs.HasEpochTexture)
        return 6;

    return 0;
}

bool DoesHandoffEpochMatchVisibleBank(
    const HandoffVisibleEpochMatchInputs& inputs)
{
    return inputs.VisibleDisplayCaptureBankValid &&
           inputs.EpochValid &&
           inputs.EpochCaptureBank == inputs.VisibleDisplayCaptureBank &&
           inputs.ConsumerRouteSlotMatches &&
           inputs.HasBackgroundEpochTexture &&
           inputs.ProductHasBackground3DUnderlay &&
           inputs.SourceIsCleanEngineA2DOutput &&
           inputs.SourceDirect3DVisible &&
           inputs.SourceOnlyBG0Visible &&
           inputs.SourceBGModeMatchesCurrent &&
           inputs.SourceHasNoVisibleBitmap;
}

bool IsHandoffEventRecentForEpoch(
    const HandoffEventRecencyInputs& inputs)
{
    return inputs.EpochMatchesVisibleBank &&
           inputs.VisibleEventValid &&
           inputs.VisibleEventSerial >= inputs.EpochSerial &&
           inputs.VisibleEventSerial - inputs.EpochSerial <= inputs.MaxSerialAge;
}

bool IsHandoffExactEpochForVisibleBank(
    const HandoffExactEpochInputs& inputs)
{
    return inputs.RecentEpochForVisibleBank &&
           inputs.VisibleEventSerial == inputs.EpochSerial &&
           inputs.VisibleEventAccepted &&
           inputs.VisibleEventFullEquivalent;
}

bool IsHandoffRoutePresentationStable(
    const HandoffRoutePresentationStableInputs& inputs)
{
    return inputs.PresentationValid &&
           inputs.PresentationCaptureBank == inputs.VisibleDisplayCaptureBank &&
           inputs.PresentationSourceHash == inputs.EpochSourceHash &&
           inputs.StableFrames >= inputs.RequiredStableFrames;
}

bool ShouldUseHandoffRouteBackgroundOverlay(
    const HandoffRouteBackgroundOverlayInputs& inputs)
{
    return inputs.RecentEpochForVisibleBank &&
           inputs.RoutePresentationStable &&
           !inputs.CapturedBitmapUpdatedThisFrame;
}

bool IsHandoffStableLiveCandidate(
    const HandoffStableLiveCandidateInputs& inputs)
{
    return inputs.Direct3DOnlyLiveBackground &&
           inputs.HasProvenRouteProduct &&
           (inputs.RouteHasCapturedPhase ||
            (inputs.CurrentCaptureEventMatchesRoute &&
             inputs.CurrentCaptureEventFresh)) &&
           inputs.HasNoBGUpload &&
           inputs.NativeProductEpochValid;
}

bool ShouldUseHandoffPresentation(
    const HandoffPresentationEligibilityInputs& inputs)
{
    if (inputs.Phase == CaptureBackedHandoffPhase::CapturedBitmap)
    {
        return inputs.HasOnlyFullDisplaySourceACapture ||
               inputs.RouteHasCapturedPhase;
    }

    if (inputs.Phase == CaptureBackedHandoffPhase::Live3D)
    {
        return inputs.RouteHasCapturedPhase ||
               (inputs.CurrentCaptureEventMatchesRoute &&
                inputs.CurrentCaptureEventFresh);
    }

    return false;
}

WholeSceneCaptureBackedPlan MakeWholeSceneCaptureBackedHandoffPlan()
{
    WholeSceneCaptureBackedPlan plan = {};
    plan.Kind = WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff;
    plan.Stage = WholeSceneCaptureBackedPlanStage::BeforeGeneralFallbacks;
    plan.Role = WholeSceneCaptureBackedPlanRole::RouteHandoff;
    plan.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
    return plan;
}

WholeSceneCaptureBackedPlan MakeWholeSceneSourceACaptureReplacementPlan()
{
    WholeSceneCaptureBackedPlan plan = {};
    plan.Kind = WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement;
    plan.Stage = WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks;
    plan.Role = WholeSceneCaptureBackedPlanRole::RouteConsumer;
    plan.RequestKind = WholeSceneCaptureRequestKind::CapturedLayerConsumer;
    return plan;
}

WholeSceneCaptureBackedPlan MakeWholeSceneCaptureEpochOverlayPlan(
    int routeSlot,
    bool canRunDuringHybridPresentationGuard)
{
    WholeSceneCaptureBackedPlan plan = {};
    plan.Kind = WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay;
    plan.Stage = WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks;
    plan.Role = WholeSceneCaptureBackedPlanRole::RouteProducer;
    plan.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
    plan.CaptureEpochOverlayRouteSlot = routeSlot;
    plan.CanRunDuringHybridPresentationGuard = canRunDuringHybridPresentationGuard;
    return plan;
}

WholeSceneCaptureRequest MakeWholeSceneCaptureRequest(
    WholeSceneCaptureBackedPlanRole role,
    WholeSceneCaptureRequestKind kind,
    int ystart,
    int yend,
    int routeSlot)
{
    WholeSceneCaptureRequest request = {};
    request.Role = role;
    request.Kind = kind;
    request.RouteSlot = routeSlot;
    request.YStart = ystart;
    request.YEnd = yend;
    return request;
}

void FillWholeSceneCaptureRequestIdentity(
    WholeSceneCaptureRequest& request,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    bool directFinalBottomConsumer)
{
    request.CaptureBank = captureBank;
    request.CaptureEventSerial = captureEventSerial;
    request.BackgroundEpochSerial = backgroundEpochSerial;
    request.CapturePresentationHash = capturePresentationHash;
    request.CurrentPresentationHash = currentPresentationHash;
    request.DirectFinalBottomConsumer = directFinalBottomConsumer;
}

WholeSceneCaptureRequest MakeWholeSceneCaptureRequestWithIdentity(
    WholeSceneCaptureBackedPlanRole role,
    WholeSceneCaptureRequestKind kind,
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    bool directFinalBottomConsumer)
{
    WholeSceneCaptureRequest request = MakeWholeSceneCaptureRequest(
        role,
        kind,
        ystart,
        yend,
        routeSlot);
    FillWholeSceneCaptureRequestIdentity(request,
                                         captureBank,
                                         captureEventSerial,
                                         backgroundEpochSerial,
                                         capturePresentationHash,
                                         currentPresentationHash,
                                         directFinalBottomConsumer);
    return request;
}

WholeSceneCaptureRequest MakeWholeSceneCaptureRequestWithIdentity(
    const WholeSceneCaptureRequest& baseRequest,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    bool directFinalBottomConsumer)
{
    WholeSceneCaptureRequest request = baseRequest;
    FillWholeSceneCaptureRequestIdentity(request,
                                         captureBank,
                                         captureEventSerial,
                                         backgroundEpochSerial,
                                         capturePresentationHash,
                                         currentPresentationHash,
                                         directFinalBottomConsumer);
    return request;
}

WholeSceneCaptureRequest MakeCapturedLayerConsumerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    bool directFinalBottomConsumer)
{
    return MakeWholeSceneCaptureRequestWithIdentity(
        WholeSceneCaptureBackedPlanRole::RouteConsumer,
        WholeSceneCaptureRequestKind::CapturedLayerConsumer,
        ystart,
        yend,
        routeSlot,
        captureBank,
        captureEventSerial,
        backgroundEpochSerial,
        capturePresentationHash,
        currentPresentationHash,
        directFinalBottomConsumer);
}

WholeSceneCaptureRequest MakeDirectFinalConsumerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash,
    bool directFinalBottomConsumer)
{
    return MakeWholeSceneCaptureRequestWithIdentity(
        WholeSceneCaptureBackedPlanRole::RouteConsumer,
        WholeSceneCaptureRequestKind::DirectFinalConsumer,
        ystart,
        yend,
        routeSlot,
        captureBank,
        captureEventSerial,
        backgroundEpochSerial,
        capturePresentationHash,
        currentPresentationHash,
        directFinalBottomConsumer);
}

WholeSceneCaptureRequest MakeLiveOverlayProducerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot,
    u32 captureBank,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash)
{
    return MakeWholeSceneCaptureRequestWithIdentity(
        WholeSceneCaptureBackedPlanRole::RouteProducer,
        WholeSceneCaptureRequestKind::LiveOverlayProducer,
        ystart,
        yend,
        routeSlot,
        captureBank,
        0,
        backgroundEpochSerial,
        capturePresentationHash,
        currentPresentationHash);
}

WholeSceneCaptureRequest MakeHandoffConsumerCaptureRequest(
    int ystart,
    int yend,
    int routeSlot)
{
    return MakeWholeSceneCaptureRequest(WholeSceneCaptureBackedPlanRole::RouteHandoff,
                                        WholeSceneCaptureRequestKind::HandoffConsumer,
                                        ystart,
                                        yend,
                                        routeSlot);
}

WholeSceneCaptureRequest MakeHandoffConsumerCaptureRequest(
    const WholeSceneCaptureRequest& baseRequest,
    u32 captureBank,
    u64 captureEventSerial,
    u64 backgroundEpochSerial,
    u32 capturePresentationHash,
    u32 currentPresentationHash)
{
    return MakeWholeSceneCaptureRequestWithIdentity(baseRequest,
                                                    captureBank,
                                                    captureEventSerial,
                                                    backgroundEpochSerial,
                                                    capturePresentationHash,
                                                    currentPresentationHash);
}

WholeSceneCapturePolicyResult MakeWholeSceneCapturePolicyResult(
    WholeSceneCaptureProductKind product,
    WholeSceneCaptureProofKind proof,
    WholeSceneCaptureRenderAction action,
    WholeSceneCaptureProductPresentationClass presentationClass,
    WholeSceneCaptureAuthority authority,
    SourceABackgroundSource backgroundSource,
    bool accepted)
{
    WholeSceneCapturePolicyResult result = {};
    result.Accepted = accepted;
    result.ProductRef.Kind = product;
    result.ProductRef.BackgroundSource = backgroundSource;
    result.Authority = authority;
    result.ProofKind = proof;
    result.RenderAction = action;
    result.PresentationClass = presentationClass;
    return result;
}

WholeSceneCapturePolicyResult MakeRouteProductCapturePolicyResult(
    CaptureBackedRouteProductLookupSource source,
    WholeSceneCaptureAuthority authority)
{
    return MakeWholeSceneCapturePolicyResult(
        CaptureProductKindForRouteLookup(source),
        CaptureProofKindForRouteLookup(source),
        WholeSceneCaptureRenderAction::BlitExactProduct,
        WholeSceneCaptureProductPresentationClass::RawContent,
        authority,
        SourceABackgroundSource::RouteProduct);
}

WholeSceneCapturePolicyResult MakeRouteProductCapturePolicyResult(
    WholeSceneCaptureProductKind product,
    WholeSceneCaptureProofKind proof,
    WholeSceneCaptureAuthority authority)
{
    return MakeWholeSceneCapturePolicyResult(product,
                                             proof,
                                             WholeSceneCaptureRenderAction::BlitExactProduct,
                                             WholeSceneCaptureProductPresentationClass::RawContent,
                                             authority,
                                             SourceABackgroundSource::RouteProduct);
}

WholeSceneCapturePolicyResult MakeBackgroundCapturePolicyResult(
    SourceABackgroundSource backgroundSource,
    WholeSceneCaptureRenderAction action,
    WholeSceneCaptureAuthority authority,
    WholeSceneCaptureProofKind proofOverride)
{
    return MakeWholeSceneCapturePolicyResult(
        CaptureProductKindForBackgroundSource(backgroundSource),
        proofOverride != WholeSceneCaptureProofKind::None
            ? proofOverride
            : CaptureProofKindForBackgroundSource(backgroundSource),
        action,
        WholeSceneCaptureProductPresentationClass::RawContent,
        authority,
        backgroundSource);
}

WholeSceneCapturePolicyResult MakeFullProductCapturePolicyResult(
    WholeSceneCaptureAuthority authority,
    WholeSceneCaptureProofKind proof)
{
    return MakeWholeSceneCapturePolicyResult(WholeSceneCaptureProductKind::FullCaptureProduct,
                                             proof,
                                             WholeSceneCaptureRenderAction::BlitExactProduct,
                                             WholeSceneCaptureProductPresentationClass::AlreadyPresented,
                                             authority,
                                             SourceABackgroundSource::FullCaptureProduct);
}

WholeSceneCapturePolicyResult MakeRejectedCapturePolicyResult(
    WholeSceneCaptureRenderAction action)
{
    return MakeWholeSceneCapturePolicyResult(WholeSceneCaptureProductKind::None,
                                             WholeSceneCaptureProofKind::None,
                                             action,
                                             WholeSceneCaptureProductPresentationClass::Fallback,
                                             WholeSceneCaptureAuthority::None,
                                             SourceABackgroundSource::None,
                                             false);
}

void AttachCaptureRequestIdentityToProductRef(
    WholeSceneCapturePolicyResult& result,
    const WholeSceneCaptureRequest& request)
{
    result.ProductRef.RouteSlot = request.RouteSlot;
    result.ProductRef.CaptureBank = request.CaptureBank;
    result.ProductRef.CaptureEventSerial = request.CaptureEventSerial;
    result.ProductRef.BackgroundEpochSerial = request.BackgroundEpochSerial;
    result.ProductRef.CapturePresentationHash = request.CapturePresentationHash;
    result.ProductRef.CurrentPresentationHash = request.CurrentPresentationHash;
}

}
