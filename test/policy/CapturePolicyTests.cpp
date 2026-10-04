// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

// Table tests for WholeSceneCapturePolicy. Each block encodes a behavior that
// was established from recorded evidence in a specific game; the referenced
// case is the reason the rule exists. Changing an expectation here means
// re-running that game's capture, not editing the test.

#include "PolicyTestHarness.h"
#include "WholeSceneCapturePolicy.h"

using namespace melonDS;

namespace
{

u16 MakeBlendCnt(u32 mode, u32 firstTargetMask, u32 secondTargetMask = 0)
{
    return static_cast<u16>((secondTargetMask << 8) | (mode << 6) | firstTargetMask);
}

u16 MakeBrightness(u32 mode, u32 factor)
{
    return static_cast<u16>((mode << 14) | factor);
}

WholeSceneCaptureProductUseInputs MakeRawContentUseInputs()
{
    WholeSceneCaptureProductUseInputs inputs = {};
    inputs.PolicyAccepted = true;
    inputs.HasTexture = true;
    inputs.RequestKind = WholeSceneCaptureRequestKind::CapturedLayerConsumer;
    inputs.ProductKind = WholeSceneCaptureProductKind::RouteProduct;
    inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
    inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
    inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
    return inputs;
}

CaptureBackedRouteProductIdentity MakeEpochIdentity()
{
    CaptureBackedRouteProductIdentity identity = {};
    identity.BackgroundEpochSerial = 41;
    identity.CaptureBank = 1;
    identity.CapturePresentationHash = 0x1234;
    identity.CurrentOverlayPresentationHash = 0x5678;
    return identity;
}

CaptureBackedRouteProductIdentity MakeSource3DIdentity()
{
    CaptureBackedRouteProductIdentity identity = MakeEpochIdentity();
    identity.Source3DSerial = 900;
    identity.Source3DSceneHash = 0xABCD;
    return identity;
}

SourceAExactRouteProductPreferenceInputs MakeExactRouteProductPreferenceInputs()
{
    SourceAExactRouteProductPreferenceInputs inputs = {};
    inputs.DirectFinalBottomConsumer = true;
    inputs.SubEngineCaptureBackedBGOnly = true;
    inputs.HasRouteProduct = true;
    inputs.RouteProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
    inputs.RouteProductProof = WholeSceneCaptureProofKind::ExactCaptureEvent;
    inputs.RouteProductEventSerial = 2896;
    inputs.RouteProductCaptureBank = 2;
    inputs.RouteProductCapturePresentationHash = 0xF5709C15u;
    inputs.RouteProductSource3DSerial = 2895;
    inputs.RouteProductSource3DSceneHash = 0x2C6164C4u;
    inputs.HasFullProduct = true;
    inputs.FullProductEventValid = true;
    inputs.FullProductEventSerial = 2896;
    inputs.FullProductCaptureBank = 2;
    inputs.FullProductCapturePresentationHash = 0xF5709C15u;
    inputs.FullProductSource3DSerial = 2895;
    inputs.FullProductSource3DSceneHash = 0x2C6164C4u;
    inputs.FullProductEventFullEquivalent = true;
    inputs.FullProductEventCleanEngineA2DOutput = true;
    inputs.FullProductEventAccepted = true;
    return inputs;
}

bool ShouldPreferSourceAExactRouteProductForDirectBottom(
    const SourceAExactRouteProductPreferenceInputs& inputs)
{
    return ChooseSourceAExactProductPreference({}, inputs) ==
           SourceACaptureSelectionPreference::ExactRouteProduct;
}

}

// Hotel Dusk fade-out, B halves: BLDCNT mode 3 (brightness decrease) with all
// six first-target bits and EVY ramping 0..16 is a frame-global darken that a
// direct-final replacement blit must reproduce, because it bypasses the
// consuming engine's compositor. Gate: mode 2/3 && EVY>0 && first-target mask
// == 0x3F exactly.
POLICY_TEST(ConsumerBrightnessColorEffectGate)
{
    // Decrease -> master-brightness-style mode 2 packing.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x3F), 8),
             MakeBrightness(2, 8));
    // Increase -> mode 1 packing.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(2, 0x3F), 5),
             MakeBrightness(1, 5));
    // EVY clamps at 16, like the hardware.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x3F), 20),
             MakeBrightness(2, 16));
    // Full Hotel Dusk ramp endpoint.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x3F), 16),
             MakeBrightness(2, 16));

    // EVY 0 is inactive even with the full target mask.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x3F), 0), 0);
    // A partial-target brightness effect must not become a full-output
    // transform (missing backdrop bit, missing OBJ bit).
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x1F), 8), 0);
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x2F), 8), 0);
    // Alpha blending (modes 0/1) is never a frame-global brightness effect.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(0, 0x3F), 8), 0);
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(1, 0x3F), 8), 0);
    // Second-target bits do not affect the frame-global decision.
    CHECK_EQ(ConsumerFullScreenBrightnessColorEffect(MakeBlendCnt(3, 0x3F, 0x3F), 8),
             MakeBrightness(2, 8));
}

// Raw products are pre-presentation content. Current capture-event, route and
// source-3D provenance select the product before this admission policy runs;
// no retained effect-history input exists here.
POLICY_TEST(RawContentAdmissionUsesPreselectedContentProvenance)
{
    WholeSceneCaptureProductUseInputs inputs = MakeRawContentUseInputs();
    CHECK(CanUseWholeSceneCaptureProduct(inputs).Accepted);

    inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
    inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
    CHECK(CanUseWholeSceneCaptureProduct(inputs).Accepted);
}

POLICY_TEST(RawContentPresentationIsOwnedDownstream)
{
    WholeSceneCaptureProductUseInputs inputs = MakeRawContentUseInputs();
    inputs.ProductPresentationHash = 0x1111;
    inputs.RequestPresentationHash = 0x2222;

    const WholeSceneCaptureProductUseDecision decision =
        CanUseWholeSceneCaptureProduct(inputs);
    CHECK(decision.Accepted);
    CHECK(decision.PresentationCompatible);

    // A zero hash on either side is equally irrelevant to raw content.
    inputs.ProductPresentationHash = 0;
    CHECK(CanUseWholeSceneCaptureProduct(inputs).PresentationCompatible);
    inputs.ProductPresentationHash = 0x1111;
    inputs.RequestPresentationHash = 0;
    CHECK(CanUseWholeSceneCaptureProduct(inputs).PresentationCompatible);
}

// Lufia title/menu kind-5 (FullCaptureProduct): AlreadyPresented products are
// only usable when the presentation hashes are nonzero and match. Hash
// equality is a content/composition gate, never proof an effect was applied.
POLICY_TEST(AlreadyPresentedRequiresPresentationHashMatch)
{
    WholeSceneCaptureProductUseInputs inputs = MakeRawContentUseInputs();
    inputs.ProductKind = WholeSceneCaptureProductKind::FullCaptureProduct;
    inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
    inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::AlreadyPresented;

    inputs.ProductPresentationHash = 0x1111;
    inputs.RequestPresentationHash = 0x1111;
    CHECK(CanUseWholeSceneCaptureProduct(inputs).Accepted);

    inputs.RequestPresentationHash = 0x2222;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);

    // Zero hashes never count as a match for AlreadyPresented content.
    inputs.ProductPresentationHash = 0;
    inputs.RequestPresentationHash = 0;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);
}

POLICY_TEST(ProductUseGateConditions)
{
    WholeSceneCaptureProductUseInputs inputs = MakeRawContentUseInputs();
    inputs.PolicyAccepted = false;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);

    inputs = MakeRawContentUseInputs();
    inputs.HasTexture = false;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);

    inputs = MakeRawContentUseInputs();
    inputs.ProductKind = WholeSceneCaptureProductKind::None;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);

    inputs = MakeRawContentUseInputs();
    inputs.RenderAction = WholeSceneCaptureRenderAction::None;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);

    inputs = MakeRawContentUseInputs();
    inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::Fallback;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);
}

POLICY_TEST(CaptureProductUseRequiresCoherentRowScope)
{
    WholeSceneCaptureProductUseInputs inputs = MakeRawContentUseInputs();
    inputs.RequestYStart = 48;
    inputs.RequestYEnd = 96;
    CHECK(CanUseWholeSceneCaptureProduct(inputs).Accepted);
    CHECK(CanUseWholeSceneCaptureProduct(inputs).RowScopeCompatible);

    inputs.ProductYStart = 64;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).RowScopeCompatible);

    inputs = MakeRawContentUseInputs();
    inputs.RenderAction =
        WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
    inputs.RequestYStart = 48;
    inputs.RequestYEnd = 96;
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).Accepted);
    CHECK(!CanUseWholeSceneCaptureProduct(inputs).RowScopeCompatible);

    inputs.RequestYStart = 0;
    inputs.RequestYEnd = 192;
    CHECK(CanUseWholeSceneCaptureProduct(inputs).Accepted);
    CHECK(CanUseWholeSceneCaptureProduct(inputs).RowScopeCompatible);
}

// The Source-A resolution ordering. A route/current composition-hash mismatch
// can select a current-overlay rebuild when that representation is available.
// Treating a mismatch plus no-overlay as rejection would demote entire fades
// to fallback because the identity hash also contains changing presentation
// registers; the route product must win when no reconstruction is available.
POLICY_TEST(SourceAResolutionOrdering)
{
    SourceACaptureResolutionInputs inputs = {};

    // DQ V Square Enix logo: exact full product preference wins outright.
    inputs.Preference = SourceACaptureSelectionPreference::ExactFullProduct;
    inputs.HasFullProduct = true;
    inputs.HasRouteProduct = true;
    inputs.CanUseCurrentOverlay = true;
    inputs.FullProductPresentationProof =
        SourceAFullProductPresentationProof::ExactFullEquivalentDirectFinal;
    const SourceACaptureSelectionDecision exactFullDecision =
        ChooseSourceACaptureSelectionDecision(inputs);
    CHECK_EQ(exactFullDecision.Primary,
             SourceACaptureResolutionKind::FullProduct);
    CHECK_EQ(exactFullDecision.PrimaryReason,
             SourceACaptureSelectionReason::ExactFullProductPreference);
    CHECK_EQ(exactFullDecision.FullProductPresentationProof,
             SourceAFullProductPresentationProof::
                 ExactFullEquivalentDirectFinal);

    // Lufia title/menu: strict same-event proof makes the exact route product
    // authoritative over a reconstruction that would otherwise alternate.
    inputs = {};
    inputs.HasRouteProduct = true;
    inputs.RouteProductCompositionMismatch = true;
    inputs.CanUseCurrentOverlay = true;
    inputs.Preference = SourceACaptureSelectionPreference::ExactRouteProduct;
    const SourceACaptureSelectionDecision exactRouteDecision =
        ChooseSourceACaptureSelectionDecision(inputs);
    CHECK_EQ(exactRouteDecision.Primary,
             SourceACaptureResolutionKind::RouteProduct);
    CHECK_EQ(exactRouteDecision.PrimaryReason,
             SourceACaptureSelectionReason::ExactRouteProductPreference);

    // Composition differs and the overlay path is available: rebuild.
    inputs = {};
    inputs.HasRouteProduct = true;
    inputs.RouteProductCompositionMismatch = true;
    inputs.CanUseCurrentOverlay = true;
    const SourceACaptureSelectionDecision overlayDecision =
        ChooseSourceACaptureSelectionDecision(inputs);
    CHECK_EQ(overlayDecision.Primary,
             SourceACaptureResolutionKind::BackgroundOverlay);
    CHECK_EQ(overlayDecision.PrimaryReason,
             SourceACaptureSelectionReason::RouteProductCompositionRebuild);
    CHECK_EQ(overlayDecision.AfterOverlayFailure,
             SourceACaptureResolutionKind::RouteProduct);
    CHECK_EQ(overlayDecision.AfterOverlayFailureReason,
             SourceACaptureSelectionReason::RouteProductAvailable);

    // Composition differs but no overlay exists: the route product still displays.
    // NOT RejectedFallback — see the note above.
    inputs.CanUseCurrentOverlay = false;
    CHECK_EQ(ChooseSourceACaptureSelectionDecision(inputs).Primary,
             SourceACaptureResolutionKind::RouteProduct);

    // AllowCurrentOverlay=false must behave like overlay-unavailable.
    inputs.CanUseCurrentOverlay = true;
    inputs.AllowCurrentOverlay = false;
    CHECK_EQ(ChooseSourceACaptureSelectionDecision(inputs).Primary,
             SourceACaptureResolutionKind::RouteProduct);

    // No route product: overlay, then full product, then rejection.
    inputs = {};
    inputs.CanUseCurrentOverlay = true;
    const SourceACaptureSelectionDecision overlayOnlyDecision =
        ChooseSourceACaptureSelectionDecision(inputs);
    CHECK_EQ(overlayOnlyDecision.Primary,
             SourceACaptureResolutionKind::BackgroundOverlay);
    CHECK_EQ(overlayOnlyDecision.PrimaryReason,
             SourceACaptureSelectionReason::CurrentOverlayAvailable);
    CHECK_EQ(overlayOnlyDecision.AfterOverlayFailure,
             SourceACaptureResolutionKind::RejectedFallback);
    CHECK_EQ(overlayOnlyDecision.AfterOverlayFailureReason,
             SourceACaptureSelectionReason::NoUsableProduct);

    inputs = {};
    inputs.HasFullProduct = true;
    CHECK_EQ(ChooseSourceACaptureSelectionDecision(inputs).Primary,
             SourceACaptureResolutionKind::FullProduct);

    inputs = {};
    CHECK_EQ(ChooseSourceACaptureSelectionDecision(inputs).Primary,
             SourceACaptureResolutionKind::RejectedFallback);
}

// Lufia's direct-bottom BG half has an exact event route product and a valid
// full-product candidate describing the same clean, full-equivalent capture.
// Only that complete current-frame proof may override overlay reconstruction.
POLICY_TEST(ExactRouteProductSameEventPreference)
{
    SourceAExactRouteProductPreferenceInputs inputs =
        MakeExactRouteProductPreferenceInputs();
    CHECK(ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.DirectFinalBottomConsumer = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.SubEngineCaptureBackedBGOnly = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.HasRouteProduct = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductKind = WholeSceneCaptureProductKind::RouteProduct;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductProof = WholeSceneCaptureProofKind::RouteStateIdentity;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductEventSerial++;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductCaptureBank = 1;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductCapturePresentationHash ^= 1;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductSource3DSerial++;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductSource3DSceneHash ^= 1;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductSource3DSerial = 0;
    inputs.RouteProductSource3DSceneHash = 0;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.RouteProductSource3DSerial = 0;
    inputs.RouteProductSource3DSceneHash = 0;
    inputs.FullProductSource3DSerial = 0;
    inputs.FullProductSource3DSceneHash = 0;
    CHECK(ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.HasFullProduct = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.FullProductEventValid = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.FullProductEventFullEquivalent = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.FullProductEventCleanEngineA2DOutput = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.FullProductEventAccepted = false;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    inputs = MakeExactRouteProductPreferenceInputs();
    inputs.FullProductEventSourceOBJVisible = true;
    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));
}

POLICY_TEST(ExactProductPreferenceOrdering)
{
    SourceAExactFullProductPreferenceInputs fullInputs = {};
    fullInputs.DirectFinalBottomConsumer = true;
    fullInputs.SubEngineCapturedSourceAOnly = true;
    fullInputs.HasFullProduct = true;
    fullInputs.FullProductEventValid = true;
    fullInputs.FullProductKeyMatch = true;
    fullInputs.FullProductEventRouteMatches = true;
    fullInputs.FullProductEventFullEquivalent = true;
    fullInputs.FullProductEventCleanEngineA2DOutput = true;
    fullInputs.FullProductEventAccepted = true;
    const SourceAExactRouteProductPreferenceInputs routeInputs =
        MakeExactRouteProductPreferenceInputs();

    CHECK_EQ(ChooseSourceAExactProductPreference(fullInputs, routeInputs),
             SourceACaptureSelectionPreference::ExactFullProduct);

    fullInputs.FullProductKeyMatch = false;
    fullInputs.FullProductEventRouteMatches = false;
    CHECK_EQ(ChooseSourceAExactProductPreference(fullInputs, routeInputs),
             SourceACaptureSelectionPreference::ExactRouteProduct);

    fullInputs.FullProductEventSourceOBJVisible = true;
    CHECK_EQ(ChooseSourceAExactProductPreference(fullInputs, routeInputs),
             SourceACaptureSelectionPreference::ExactFullProduct);
}

// Kingdom Hearts post-battle screen swap: a direct-final product recorded for
// one screen arrangement must not be re-presented after LCD swap or final-
// bottom routing changes.
POLICY_TEST(DirectFinalRouteMatchRejectsSwapChange)
{
    DirectFinalRouteMatchInputs inputs = {};
    inputs.EventScreenSwap = false;
    inputs.CurrentScreenSwap = false;
    inputs.EventMainFinalBottom = true;
    inputs.CurrentMainFinalBottom = true;
    CHECK(DoesDirectFinalRouteMatch(inputs));

    inputs.CurrentScreenSwap = true;
    CHECK(!DoesDirectFinalRouteMatch(inputs));

    inputs.CurrentScreenSwap = false;
    inputs.CurrentMainFinalBottom = false;
    CHECK(!DoesDirectFinalRouteMatch(inputs));

    // Swapped on both sides is a match again — the guard is about *change*.
    inputs = {};
    inputs.EventScreenSwap = true;
    inputs.CurrentScreenSwap = true;
    CHECK(DoesDirectFinalRouteMatch(inputs));
}

// Mario Kart path 8: exact-event route product reuse is Mario Kart's only
// capture path. Event queries are full-screen only and demand the exact
// serial; the identity match uses Source3D serial+hash when present,
// otherwise the background epoch serial must equal the event serial.
POLICY_TEST(ExactEventQueryUsability)
{
    CaptureBackedRouteProductEventQuery query =
        MakeCaptureBackedRouteProductEventQuery(0, 77, 0, 0, 1, 0x1234, 0, 192);
    CHECK(IsCaptureBackedRouteProductEventQueryUsable(query, 2, true));

    CHECK(!IsCaptureBackedRouteProductEventQueryUsable(query, 0, true));
    CHECK(!IsCaptureBackedRouteProductEventQueryUsable(query, 2, false));

    query.YEnd = 96;
    CHECK(!IsCaptureBackedRouteProductEventQueryUsable(query, 2, true));
    query.YEnd = 192;

    query.CaptureEventSerial = 0;
    CHECK(!IsCaptureBackedRouteProductEventQueryUsable(query, 2, true));
    query.CaptureEventSerial = 77;

    query.CaptureBank = 4;
    CHECK(!IsCaptureBackedRouteProductEventQueryUsable(query, 2, true));
    query.CaptureBank = 1;

    query.CapturePresentationHash = 0;
    CHECK(!IsCaptureBackedRouteProductEventQueryUsable(query, 2, true));
}

POLICY_TEST(ExactEventProductMatch)
{
    CaptureBackedRouteEventProductState product = {};
    product.Valid = true;
    product.Identity = MakeEpochIdentity();
    product.CapturedEventSerial = 41;

    // The epoch-only identity matches when the epoch serial equals the event
    // serial being consumed; the query's capture hash is compared against the
    // stored current-overlay presentation hash.
    CaptureBackedRouteProductEventQuery query =
        MakeCaptureBackedRouteProductEventQuery(0, 41, 0, 0, 1, 0x5678, 0, 192);
    CHECK(DoesCaptureBackedRouteEventProductMatchQuery(product, query));

    query.CaptureEventSerial = 42;
    CHECK(!DoesCaptureBackedRouteEventProductMatchQuery(product, query));
    query.CaptureEventSerial = 41;

    query.CaptureBank = 2;
    CHECK(!DoesCaptureBackedRouteEventProductMatchQuery(product, query));
    query.CaptureBank = 1;

    query.CapturePresentationHash = 0x9999;
    CHECK(!DoesCaptureBackedRouteEventProductMatchQuery(product, query));

    // Source3D-identity products match on serial+hash of the 3D scene.
    product.Identity = MakeSource3DIdentity();
    query = MakeCaptureBackedRouteProductEventQuery(0, 41, 900, 0xABCD, 1, 0x5678, 0, 192);
    CHECK(DoesCaptureBackedRouteEventProductMatchQuery(product, query));

    query.Source3DSerial = 901;
    CHECK(!DoesCaptureBackedRouteEventProductMatchQuery(product, query));
}

POLICY_TEST(RouteProductIdentityValidity)
{
    CHECK(IsValidCaptureBackedRouteProductIdentity(MakeEpochIdentity()));
    CHECK(IsValidCaptureBackedRouteProductIdentity(MakeSource3DIdentity()));

    CaptureBackedRouteProductIdentity identity = MakeEpochIdentity();
    identity.CaptureBank = 4;
    CHECK(!IsValidCaptureBackedRouteProductIdentity(identity));

    identity = MakeEpochIdentity();
    identity.CapturePresentationHash = 0;
    CHECK(!IsValidCaptureBackedRouteProductIdentity(identity));

    identity = MakeEpochIdentity();
    identity.CurrentOverlayPresentationHash = 0;
    CHECK(!IsValidCaptureBackedRouteProductIdentity(identity));

    identity = MakeEpochIdentity();
    identity.BackgroundEpochSerial = 0;
    CHECK(!IsValidCaptureBackedRouteProductIdentity(identity));

    // Source3D identity needs both serial and hash; hash alone is not enough.
    identity.Source3DSceneHash = 0xABCD;
    CHECK(!IsValidCaptureBackedRouteProductIdentity(identity));
    identity.Source3DSerial = 900;
    CHECK(IsValidCaptureBackedRouteProductIdentity(identity));
}

POLICY_TEST(IdentityMatchesEventRules)
{
    // Source3D identity: the 3D scene proof overrides the event serial.
    CHECK(CaptureBackedRouteProductIdentityMatchesEvent(
        MakeSource3DIdentity(), 999, 900, 0xABCD));
    CHECK(!CaptureBackedRouteProductIdentityMatchesEvent(
        MakeSource3DIdentity(), 999, 901, 0xABCD));
    CHECK(!CaptureBackedRouteProductIdentityMatchesEvent(
        MakeSource3DIdentity(), 999, 0, 0));

    // Epoch identity: the epoch serial must equal the consumed event serial.
    CHECK(CaptureBackedRouteProductIdentityMatchesEvent(
        MakeEpochIdentity(), 41, 0, 0));
    CHECK(!CaptureBackedRouteProductIdentityMatchesEvent(
        MakeEpochIdentity(), 42, 0, 0));

    // Event serial 0 never matches anything.
    CHECK(!CaptureBackedRouteProductIdentityMatchesEvent(
        MakeEpochIdentity(), 0, 0, 0));
}

// A7 handoff epoch guards (Hotel Dusk dialog cadence): the visible display
// capture bank must have a recent causal event behind the epoch composite.
POLICY_TEST(HandoffEventRecency)
{
    HandoffEventRecencyInputs inputs = {};
    inputs.EpochMatchesVisibleBank = true;
    inputs.VisibleEventValid = true;
    inputs.EpochSerial = 10;

    inputs.VisibleEventSerial = 10;
    CHECK(IsHandoffEventRecentForEpoch(inputs));
    inputs.VisibleEventSerial = 12;
    CHECK(IsHandoffEventRecentForEpoch(inputs));

    // Too old: the epoch composite lags the visible event by > MaxSerialAge.
    inputs.VisibleEventSerial = 13;
    CHECK(!IsHandoffEventRecentForEpoch(inputs));

    // An epoch NEWER than the visible event is never recent (no latching:
    // future content must not be held against an older visible event).
    inputs.VisibleEventSerial = 9;
    CHECK(!IsHandoffEventRecentForEpoch(inputs));

    inputs.VisibleEventSerial = 11;
    inputs.EpochMatchesVisibleBank = false;
    CHECK(!IsHandoffEventRecentForEpoch(inputs));
    inputs.EpochMatchesVisibleBank = true;
    inputs.VisibleEventValid = false;
    CHECK(!IsHandoffEventRecentForEpoch(inputs));
}

POLICY_TEST(HandoffExactEpochAndStability)
{
    HandoffExactEpochInputs exact = {};
    exact.RecentEpochForVisibleBank = true;
    exact.VisibleEventSerial = 10;
    exact.EpochSerial = 10;
    exact.VisibleEventAccepted = true;
    exact.VisibleEventFullEquivalent = true;
    CHECK(IsHandoffExactEpochForVisibleBank(exact));

    exact.EpochSerial = 9;
    CHECK(!IsHandoffExactEpochForVisibleBank(exact));
    exact.EpochSerial = 10;
    exact.VisibleEventFullEquivalent = false;
    CHECK(!IsHandoffExactEpochForVisibleBank(exact));

    // Route presentation must be stable for >= 2 frames before the overlay
    // path may reuse it (chatter guard against selector oscillation).
    HandoffRoutePresentationStableInputs stable = {};
    stable.PresentationValid = true;
    stable.PresentationCaptureBank = 1;
    stable.VisibleDisplayCaptureBank = 1;
    stable.PresentationSourceHash = 0xAAAA;
    stable.EpochSourceHash = 0xAAAA;
    stable.StableFrames = 2;
    CHECK(IsHandoffRoutePresentationStable(stable));

    stable.StableFrames = 1;
    CHECK(!IsHandoffRoutePresentationStable(stable));
    stable.StableFrames = 2;
    stable.EpochSourceHash = 0xBBBB;
    CHECK(!IsHandoffRoutePresentationStable(stable));
    stable.EpochSourceHash = 0xAAAA;
    stable.PresentationCaptureBank = 2;
    CHECK(!IsHandoffRoutePresentationStable(stable));
}

POLICY_TEST(HandoffBackgroundOverlayGate)
{
    HandoffRouteBackgroundOverlayInputs inputs = {};
    inputs.RecentEpochForVisibleBank = true;
    inputs.RoutePresentationStable = true;
    inputs.CapturedBitmapUpdatedThisFrame = false;
    CHECK(ShouldUseHandoffRouteBackgroundOverlay(inputs));

    // A capture that just updated this frame takes priority over the overlay
    // reconstruction — consume the fresh event instead.
    inputs.CapturedBitmapUpdatedThisFrame = true;
    CHECK(!ShouldUseHandoffRouteBackgroundOverlay(inputs));

    inputs.CapturedBitmapUpdatedThisFrame = false;
    inputs.RoutePresentationStable = false;
    CHECK(!ShouldUseHandoffRouteBackgroundOverlay(inputs));
}

POLICY_TEST(HandoffLivePresentationRequiresCapturedPhaseEvidence)
{
    HandoffStableLiveCandidateInputs candidate = {};
    candidate.Direct3DOnlyLiveBackground = true;
    candidate.HasProvenRouteProduct = true;
    candidate.HasNoBGUpload = true;
    candidate.NativeProductEpochValid = true;

    // Mario Kart can publish exact route products for Engine B's display
    // capture cadence while Engine A remains a wholly live Hybrid scene. A
    // product alone must not turn that live route into an A7 handoff.
    CHECK(!IsHandoffStableLiveCandidate(candidate));

    HandoffPresentationEligibilityInputs presentation = {};
    presentation.Phase = CaptureBackedHandoffPhase::Live3D;
    CHECK(!ShouldUseHandoffPresentation(presentation));

    // Hotel Dusk also has a causal bootstrap case: the accepted event and
    // proven product are for the current physical route on this same frame.
    candidate.CurrentCaptureEventMatchesRoute = true;
    presentation.CurrentCaptureEventMatchesRoute = true;
    CHECK(!IsHandoffStableLiveCandidate(candidate));
    CHECK(!ShouldUseHandoffPresentation(presentation));

    // Exact identity is not enough if LastHighResDisplayCaptureEvent is an
    // old accepted event. Only a current/previous-frame publication can
    // bootstrap the live route without a captured phase.
    candidate.CurrentCaptureEventFresh = true;
    CHECK(IsHandoffStableLiveCandidate(candidate));
    presentation.CurrentCaptureEventFresh = true;
    CHECK(ShouldUseHandoffPresentation(presentation));
    candidate.CurrentCaptureEventMatchesRoute = false;
    candidate.CurrentCaptureEventFresh = false;
    presentation.CurrentCaptureEventMatchesRoute = false;
    presentation.CurrentCaptureEventFresh = false;

    // Hotel Dusk's live half remains eligible after the same physical route
    // has genuinely exhibited its captured-bitmap half.
    candidate.RouteHasCapturedPhase = true;
    CHECK(IsHandoffStableLiveCandidate(candidate));
    presentation.RouteHasCapturedPhase = true;
    CHECK(ShouldUseHandoffPresentation(presentation));

    // The first visible full-display capture is still allowed to establish
    // the route evidence used by later live frames.
    presentation = {};
    presentation.Phase = CaptureBackedHandoffPhase::CapturedBitmap;
    presentation.HasOnlyFullDisplaySourceACapture = true;
    CHECK(ShouldUseHandoffPresentation(presentation));
}

POLICY_TEST(HandoffEpochVisibleBankMatchIsStrict)
{
    HandoffVisibleEpochMatchInputs inputs = {};
    inputs.VisibleDisplayCaptureBankValid = true;
    inputs.EpochValid = true;
    inputs.EpochCaptureBank = 1;
    inputs.VisibleDisplayCaptureBank = 1;
    inputs.ConsumerRouteSlotMatches = true;
    inputs.HasBackgroundEpochTexture = true;
    inputs.ProductHasBackground3DUnderlay = true;
    inputs.SourceIsCleanEngineA2DOutput = true;
    inputs.SourceDirect3DVisible = true;
    inputs.SourceOnlyBG0Visible = true;
    inputs.SourceBGModeMatchesCurrent = true;
    inputs.SourceHasNoVisibleBitmap = true;
    CHECK(DoesHandoffEpochMatchVisibleBank(inputs));

    inputs.EpochCaptureBank = 2;
    CHECK(!DoesHandoffEpochMatchVisibleBank(inputs));
    inputs.EpochCaptureBank = 1;
    inputs.SourceOnlyBG0Visible = false;
    CHECK(!DoesHandoffEpochMatchVisibleBank(inputs));
    inputs.SourceOnlyBG0Visible = true;
    inputs.SourceBGModeMatchesCurrent = false;
    CHECK(!DoesHandoffEpochMatchVisibleBank(inputs));
}

// DQ V VRAM-display capture support: display mode 2 scaling admission reads
// DISPCAPCNT directly (srcA select, full capture size, destination block ==
// display bank, and a dst write mode that actually lands the content).
POLICY_TEST(MainVRAMDisplayCaptureScaleAdmission)
{
    auto makeCaptureCnt = [](u32 eva, u32 dstblock, u32 capsize, u32 srcA, u32 dstmode)
    {
        return (dstmode << 29) | (srcA << 24) | (capsize << 20) | (dstblock << 16) | eva;
    };

    MainVRAMDisplayCaptureScaleInputs inputs = {};
    inputs.MainEngine = true;
    inputs.DisplayMode = 2;
    inputs.ConservativeHybridMode = true;
    inputs.CaptureBackedScalingEnabled = true;
    inputs.CaptureEnabled = true;
    inputs.DisplayBank = 2;

    // Full-size source-A capture into the displayed bank, plain write.
    inputs.CaptureCnt = makeCaptureCnt(0, 2, 3, 0, 0);
    CHECK(CanScaleMainVRAMDisplayCaptureSourceA(inputs));

    // Blend write with EVA > 0 still lands content.
    inputs.CaptureCnt = makeCaptureCnt(8, 2, 3, 0, 2);
    CHECK(CanScaleMainVRAMDisplayCaptureSourceA(inputs));

    // Blend write with EVA 0 writes nothing new: only an already-accepted
    // replacement keeps the path.
    inputs.CaptureCnt = makeCaptureCnt(0, 2, 3, 0, 2);
    CHECK(!CanScaleMainVRAMDisplayCaptureSourceA(inputs));
    inputs.HasAcceptedDisplayReplacement = true;
    CHECK(CanScaleMainVRAMDisplayCaptureSourceA(inputs));
    inputs.HasAcceptedDisplayReplacement = false;

    // Capture into a different bank than the displayed one.
    inputs.CaptureCnt = makeCaptureCnt(0, 1, 3, 0, 0);
    CHECK(!CanScaleMainVRAMDisplayCaptureSourceA(inputs));

    // No capture running: only an accepted replacement keeps the path.
    inputs.CaptureEnabled = false;
    CHECK(!CanScaleMainVRAMDisplayCaptureSourceA(inputs));
    inputs.HasAcceptedDisplayReplacement = true;
    CHECK(CanScaleMainVRAMDisplayCaptureSourceA(inputs));
    inputs.HasAcceptedDisplayReplacement = false;
    inputs.CaptureEnabled = true;

    // Wrong display mode / engine / mode switches disable it outright.
    inputs.CaptureCnt = makeCaptureCnt(0, 2, 3, 0, 0);
    inputs.DisplayMode = 1;
    CHECK(!CanScaleMainVRAMDisplayCaptureSourceA(inputs));
    inputs.DisplayMode = 2;
    inputs.MainEngine = false;
    CHECK(!CanScaleMainVRAMDisplayCaptureSourceA(inputs));
    inputs.MainEngine = true;
    inputs.ConservativeHybridMode = false;
    CHECK(!CanScaleMainVRAMDisplayCaptureSourceA(inputs));
}

// Lufia capture-backed OBJ: sources with visible OBJ are only replaceable when
// the event's source rendered the full whole scene; mixed/OBJ sources reject.
POLICY_TEST(MainVRAMDisplayExactEventReplacementOBJRule)
{
    MainVRAMDisplayExactEventReplacementInputs inputs = {};
    inputs.EventRecordValid = true;
    inputs.EventDstOffsetZero = true;
    inputs.EventAccepted = true;
    inputs.EventFullEquivalent = true;
    inputs.EventAcceptedSource = true;
    inputs.HasFullTexture = true;
    inputs.EventRouteMatches = true;
    inputs.EventSourceOBJVisible = false;
    CHECK(CanUseMainVRAMDisplayExactEventReplacement(inputs));

    inputs.EventSourceOBJVisible = true;
    CHECK(!CanUseMainVRAMDisplayExactEventReplacement(inputs));
    inputs.EventSourceRenderedFullWholeScene = true;
    CHECK(CanUseMainVRAMDisplayExactEventReplacement(inputs));

    inputs.EventRouteMatches = false;
    CHECK(!CanUseMainVRAMDisplayExactEventReplacement(inputs));
}

// Hotel Dusk Random Flash: when main-VRAM display reaches a guaranteed
// full-screen EVY=16 black endpoint, reject an exact-event high-resolution
// product without changing the native VRAM display's color. The guard must
// not catch ordinary fade steps or overridable effects.
POLICY_TEST(MainVRAMDisplayFullBlackEndpointBypass)
{
    CHECK(IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(3, 0x3F), 16, false));
    CHECK(IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(3, 0x3F), 20, false));

    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(3, 0x3F), 15, false));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(3, 0x1F), 16, false));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(3, 0x3F, 0x01), 16, false));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(3, 0x3F), 16, true));
}

// Hotel Dusk serials 210 and 1454: the prior capture product leaked for one
// frame when the game reached the symmetric full-white endpoint. VRAM display
// bypasses BLDCNT, so this classification rejects the one-frame-late high-res
// substitute; it must not turn the physical VRAM display white.
POLICY_TEST(MainVRAMDisplayFullWhiteEndpointBypass)
{
    CHECK(IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(2, 0x3F), 16, false));
    CHECK(IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(2, 0x3F), 20, false));

    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(2, 0x3F), 15, false));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(2, 0x1F), 16, false));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(2, 0x3F, 0x01), 16, false));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(2, 0x3F), 16, true));
    CHECK(!IsGuaranteedFullScreenBrightnessEndpoint(
        MakeBlendCnt(1, 0x3F), 16, false));
}

POLICY_TEST(MainVRAMDisplayEpochReplacementRejectReasons)
{
    MainVRAMDisplayEpochReplacementInputs inputs = {};
    inputs.EpochValid = true;
    inputs.EpochCaptureBank = 2;
    inputs.DisplayBank = 2;
    inputs.EpochDstOffset = 0;
    inputs.EpochFullEquivalent = true;
    inputs.EpochAcceptedSource = true;
    inputs.HasEpochTexture = true;
    CHECK_EQ(MainVRAMDisplayEpochReplacementRejectReason(inputs), 0);

    inputs.EpochCaptureBank = 1;
    CHECK_EQ(MainVRAMDisplayEpochReplacementRejectReason(inputs), 9);
    inputs.EpochCaptureBank = 2;

    inputs.EpochHasFullDirtyRows = true;
    CHECK_EQ(MainVRAMDisplayEpochReplacementRejectReason(inputs), 10);
    inputs.EpochHasFullDirtyRows = false;

    inputs.EpochFullEquivalent = false;
    CHECK_EQ(MainVRAMDisplayEpochReplacementRejectReason(inputs), 6);
    inputs.EpochFullEquivalent = true;

    inputs.EpochAcceptedSource = false;
    CHECK_EQ(MainVRAMDisplayEpochReplacementRejectReason(inputs), 7);
    inputs.EpochAcceptedSource = true;

    inputs.HasEpochTexture = false;
    CHECK_EQ(MainVRAMDisplayEpochReplacementRejectReason(inputs), 6);
}

POLICY_TEST(StorableCapturePresentationClasses)
{
    CHECK(IsStorableCaptureBackedRouteProductClass(
        WholeSceneCaptureProductPresentationClass::RawContent));
    CHECK(IsStorableCaptureBackedRouteProductClass(
        WholeSceneCaptureProductPresentationClass::AlreadyPresented));
    CHECK(!IsStorableCaptureBackedRouteProductClass(
        WholeSceneCaptureProductPresentationClass::Fallback));
    CHECK(!IsStorableCaptureBackedRouteProductClass(
        WholeSceneCaptureProductPresentationClass::Unknown));
}

// Policy results carry their presentation contract explicitly.  The GL
// adapter must not have to rediscover it from the selected product family.
POLICY_TEST(CapturePolicyResultCarriesPresentationClass)
{
    const WholeSceneCapturePolicyResult route =
        MakeRouteProductCapturePolicyResult(
            CaptureBackedRouteProductLookupSource::ExactEventProduct,
            WholeSceneCaptureAuthority::SourceABackgroundCurrentOverlay);
    CHECK_EQ(route.PresentationClass,
             WholeSceneCaptureProductPresentationClass::RawContent);

    const WholeSceneCapturePolicyResult background =
        MakeBackgroundCapturePolicyResult(
            SourceABackgroundSource::ActiveCaptureEpochTex,
            WholeSceneCaptureRenderAction::CompositeCurrentOverlay,
            WholeSceneCaptureAuthority::SourceABackgroundCurrentOverlay);
    CHECK_EQ(background.PresentationClass,
             WholeSceneCaptureProductPresentationClass::RawContent);

    const WholeSceneCapturePolicyResult full =
        MakeFullProductCapturePolicyResult(
            WholeSceneCaptureAuthority::SourceAFullProduct,
            WholeSceneCaptureProofKind::ExactCaptureEvent);
    CHECK_EQ(full.PresentationClass,
             WholeSceneCaptureProductPresentationClass::AlreadyPresented);

    const WholeSceneCapturePolicyResult rejected =
        MakeRejectedCapturePolicyResult(
            WholeSceneCaptureRenderAction::RenderNormalHybridFallback);
    CHECK_EQ(rejected.PresentationClass,
             WholeSceneCaptureProductPresentationClass::Fallback);
}

// Route lookup source -> product/proof mapping. Path-8 exact-event reuse is
// load-bearing in four games (Hotel Dusk, Lufia, Mario Kart, DQ V); its
// lookup sources must keep ExactCaptureEvent proof strength.
POLICY_TEST(RouteLookupKindAndProofMapping)
{
    CHECK_EQ(CaptureProductKindForRouteLookup(
                 CaptureBackedRouteProductLookupSource::ExactEventProduct),
             WholeSceneCaptureProductKind::RouteEventProduct);
    CHECK_EQ(CaptureProofKindForRouteLookup(
                 CaptureBackedRouteProductLookupSource::ExactEventProduct),
             WholeSceneCaptureProofKind::ExactCaptureEvent);
    CHECK_EQ(CaptureProofKindForRouteLookup(
                 CaptureBackedRouteProductLookupSource::ExactEventRouteProduct),
             WholeSceneCaptureProofKind::ExactCaptureEvent);
    CHECK_EQ(CaptureProofKindForRouteLookup(
                 CaptureBackedRouteProductLookupSource::RouteStateProduct),
             WholeSceneCaptureProofKind::RouteStateIdentity);
    CHECK_EQ(CaptureProofKindForRouteLookup(
                 CaptureBackedRouteProductLookupSource::Source3DSceneProduct),
             WholeSceneCaptureProofKind::Source3DSceneIdentity);

    CHECK_EQ(CaptureProofKindForBackgroundSource(
                 SourceABackgroundSource::FullCaptureProduct),
             WholeSceneCaptureProofKind::ExactCaptureEvent);
    CHECK_EQ(CaptureProofKindForBackgroundSource(
                 SourceABackgroundSource::ActiveCaptureEpochTex),
             WholeSceneCaptureProofKind::ActiveBackgroundEpoch);
    CHECK_EQ(CaptureProofKindForBackgroundSource(
                 SourceABackgroundSource::HandoffSnapshot),
             WholeSceneCaptureProofKind::HandoffRouteKey);
}

// The three capture-backed plans and their fixed structure. The handoff runs
// before general fallbacks; replacement and epoch overlay run after; only the
// epoch overlay producer may run during the hybrid presentation guard.
POLICY_TEST(CaptureBackedPlanInvariants)
{
    const WholeSceneCaptureBackedPlan handoff = MakeWholeSceneCaptureBackedHandoffPlan();
    CHECK_EQ(handoff.Kind, WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff);
    CHECK_EQ(handoff.Stage, WholeSceneCaptureBackedPlanStage::BeforeGeneralFallbacks);
    CHECK_EQ(handoff.Role, WholeSceneCaptureBackedPlanRole::RouteHandoff);
    CHECK(!handoff.CanRunDuringHybridPresentationGuard);

    const WholeSceneCaptureBackedPlan replacement = MakeWholeSceneSourceACaptureReplacementPlan();
    CHECK_EQ(replacement.Kind, WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement);
    CHECK_EQ(replacement.Stage, WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks);
    CHECK_EQ(replacement.Role, WholeSceneCaptureBackedPlanRole::RouteConsumer);
    CHECK(!replacement.CanRunDuringHybridPresentationGuard);

    const WholeSceneCaptureBackedPlan overlay = MakeWholeSceneCaptureEpochOverlayPlan(3);
    CHECK_EQ(overlay.Kind, WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay);
    CHECK_EQ(overlay.Stage, WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks);
    CHECK_EQ(overlay.Role, WholeSceneCaptureBackedPlanRole::RouteProducer);
    CHECK_EQ(overlay.CaptureEpochOverlayRouteSlot, 3);
    CHECK(overlay.CanRunDuringHybridPresentationGuard);
}

POLICY_TEST(SourceAFullProductPresentationProof)
{
    SourceAFullProductPresentationProofInputs inputs = {};
    inputs.DirectFinalBottomConsumer = true;
    inputs.SubEngineCapturedSourceAOnly = true;
    inputs.HasFullProduct = true;
    inputs.FullProductEventValid = true;
    inputs.FullProductEventFullEquivalent = true;
    inputs.FullProductEventCleanEngineA2DOutput = true;
    inputs.FullProductEventAccepted = true;
    CHECK_EQ(ProveSourceAFullProductPresentation(inputs),
             SourceAFullProductPresentationProof::
                 ExactFullEquivalentDirectFinal);

    inputs.DirectFinalBottomConsumer = false;
    CHECK_EQ(ProveSourceAFullProductPresentation(inputs),
             SourceAFullProductPresentationProof::None);

    inputs.DirectFinalDisplayConsumer = true;
    inputs.SubEngineCapturedOBJOnly = true;
    CHECK_EQ(ProveSourceAFullProductPresentation(inputs),
             SourceAFullProductPresentationProof::
                 ExactFullEquivalentDirectFinal);

    inputs.FullProductEventAccepted = false;
    CHECK_EQ(ProveSourceAFullProductPresentation(inputs),
             SourceAFullProductPresentationProof::None);
}
