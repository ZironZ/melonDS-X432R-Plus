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

WholeSceneCaptureProductPresentationClass CaptureProductPresentationClassForProduct(
    WholeSceneCaptureProductKind product,
    WholeSceneCaptureRenderAction action)
{
    if (action == WholeSceneCaptureRenderAction::RenderNormalHybridFallback)
        return WholeSceneCaptureProductPresentationClass::Fallback;

    switch (product)
    {
    case WholeSceneCaptureProductKind::RouteProduct:
    case WholeSceneCaptureProductKind::RouteEventProduct:
    case WholeSceneCaptureProductKind::RouteStateProduct:
    case WholeSceneCaptureProductKind::BackgroundProduct:
    case WholeSceneCaptureProductKind::HandoffSnapshot:
    case WholeSceneCaptureProductKind::ParentOutput3D:
        return WholeSceneCaptureProductPresentationClass::RawContent;
    case WholeSceneCaptureProductKind::FullCaptureProduct:
        return WholeSceneCaptureProductPresentationClass::AlreadyPresented;
    case WholeSceneCaptureProductKind::None:
    default:
        return WholeSceneCaptureProductPresentationClass::None;
    }
}

bool ShouldApplySourceAReplacementPresentationEffect(
    WholeSceneCaptureProductPresentationClass productClass,
    WholeSceneCaptureRequestKind requestKind,
    bool sourceEngineIsSub)
{
    if (!sourceEngineIsSub ||
        productClass != WholeSceneCaptureProductPresentationClass::RawContent)
    {
        return false;
    }

    return requestKind == WholeSceneCaptureRequestKind::CapturedLayerConsumer ||
           requestKind == WholeSceneCaptureRequestKind::DirectFinalConsumer;
}

bool ShouldApplyHandoffPresentationEffect(
    WholeSceneCaptureProductPresentationClass productClass,
    WholeSceneCaptureRequestKind requestKind)
{
    return requestKind == WholeSceneCaptureRequestKind::HandoffConsumer &&
           productClass == WholeSceneCaptureProductPresentationClass::RawContent;
}

bool IsMasterBrightnessEffectActive(u16 masterBrightness)
{
    const u32 mode = (masterBrightness >> 14) & 0x3;
    const u32 factor = masterBrightness & 0x1F;
    return (mode == 1 || mode == 2) && factor > 0;
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

    if (!inputs.PolicyAccepted ||
        !inputs.HasTexture ||
        inputs.ProductKind == WholeSceneCaptureProductKind::None ||
        inputs.RenderAction == WholeSceneCaptureRenderAction::None)
    {
        return decision;
    }

    switch (inputs.PresentationClass)
    {
    case WholeSceneCaptureProductPresentationClass::RawContent:
        // Weak route-state reuse can leak pre-fade pixels after the source
        // brightness effect releases. Exact capture-event proof is allowed:
        // that product represents the event being consumed, not just a similar
        // route state.
        if (inputs.HasStoredEffectState &&
            inputs.StoredEffectActive &&
            !inputs.ConsumeEffectActive &&
            inputs.ProofKind != WholeSceneCaptureProofKind::ExactCaptureEvent)
        {
            decision.EffectPhaseIncompatible = true;
            return decision;
        }
        decision.Accepted = true;
        decision.PresentationCompatible =
            inputs.ProductPresentationHash == 0 ||
            inputs.RequestPresentationHash == 0 ||
            DoesCaptureProductPresentationMatchRequest(inputs.ProductPresentationHash,
                                                       inputs.RequestPresentationHash);
        decision.RequiresRePresentation = !decision.PresentationCompatible;
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

SourceACaptureResolutionKind ChooseSourceACaptureResolutionKind(
    const SourceACaptureResolutionInputs& inputs)
{
    if (inputs.PreferExactFullProduct &&
        inputs.HasFullProduct)
    {
        return SourceACaptureResolutionKind::FullProduct;
    }

    if (inputs.PreferExactRouteProduct &&
        inputs.HasRouteProduct)
    {
        return SourceACaptureResolutionKind::RouteProduct;
    }

    if (inputs.HasRouteProduct &&
        inputs.RouteProductNeedsRePresentation &&
        inputs.AllowCurrentOverlay &&
        inputs.CanUseCurrentOverlay)
    {
        return SourceACaptureResolutionKind::BackgroundOverlay;
    }

    if (inputs.HasRouteProduct)
        return SourceACaptureResolutionKind::RouteProduct;

    if (inputs.AllowCurrentOverlay && inputs.CanUseCurrentOverlay)
        return SourceACaptureResolutionKind::BackgroundOverlay;

    if (!inputs.HasFullProduct)
        return SourceACaptureResolutionKind::RejectedFallback;

    return SourceACaptureResolutionKind::FullProduct;
}

bool ShouldPreferSourceAExactRouteProductForDirectBottom(
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

bool ShouldPreferSourceAExactFullProductForDirectBottom(
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

WholeSceneCaptureBackedPlan MakeWholeSceneCaptureBackedHandoffPlan()
{
    WholeSceneCaptureBackedPlan plan = {};
    plan.Kind = WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff;
    plan.Stage = WholeSceneCaptureBackedPlanStage::BeforeGeneralFallbacks;
    plan.Role = WholeSceneCaptureBackedPlanRole::RouteHandoff;
    plan.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
    plan.ProductKind = WholeSceneCaptureProductKind::HandoffSnapshot;
    plan.ProofKind = WholeSceneCaptureProofKind::HandoffRouteKey;
    plan.RenderAction = WholeSceneCaptureRenderAction::RenderHandoffHybrid;
    return plan;
}

WholeSceneCaptureBackedPlan MakeWholeSceneSourceACaptureReplacementPlan()
{
    WholeSceneCaptureBackedPlan plan = {};
    plan.Kind = WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement;
    plan.Stage = WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks;
    plan.Role = WholeSceneCaptureBackedPlanRole::RouteConsumer;
    plan.RequestKind = WholeSceneCaptureRequestKind::CapturedLayerConsumer;
    plan.ProductKind = WholeSceneCaptureProductKind::FullCaptureProduct;
    plan.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
    plan.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
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
    plan.ProductKind = WholeSceneCaptureProductKind::BackgroundProduct;
    plan.ProofKind = WholeSceneCaptureProofKind::ActiveBackgroundEpoch;
    plan.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
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
                                             authority,
                                             SourceABackgroundSource::FullCaptureProduct);
}

WholeSceneCapturePolicyResult MakeRejectedCapturePolicyResult(
    WholeSceneCaptureRenderAction action)
{
    return MakeWholeSceneCapturePolicyResult(WholeSceneCaptureProductKind::None,
                                             WholeSceneCaptureProofKind::None,
                                             action,
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
