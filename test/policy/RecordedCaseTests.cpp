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

// Recorded-case table tests lifted from whole-scene renderer-details CSVs.
//
// Generate blocks with test/policy/lift-policy-rows.ps1 against a CSV whose
// on-screen behavior was visually verified (ideally recorded through the
// --renderer-test harness so the case is reproducible), then paste them here.
// Keep each block's provenance comment (CSV name + frame window); when a
// policy change legitimately breaks a recorded case, the fix is to re-run
// the case and re-lift, never to hand-edit the expected values.

#include "PolicyTestHarness.h"
#include "WholeSceneCapturePolicy.h"
#include "WholeSceneScalePolicy.h"

using namespace melonDS;

// Recorded from Hotel Dusk's TV fade-back reversions at frames 2198 and 2214
// in recorded-csv/hotel-dusk/20260725-174931-tv-fade-scaling-on/
// whole-scene-frame-times-20260725-174931-renderer-details.csv.
// The retained epoch belongs to bank 3 and Source3D 126509, while the current
// frame is Source3D 126510 and DISP_CAPCNT has already armed bank 2 to consume
// that current Engine A composite. Current pixels must not inherit bank 3.
POLICY_TEST(Recorded_HotelDuskTVFadeUsesCurrent3DAndRequestedCaptureBank)
{
    CaptureEpochOverlayCurrentInputs inputs = {};
    inputs.CurrentSource3DSerial = 126510;
    inputs.CurrentSource3DSceneHash = 2029878419u;
    inputs.CurrentPresentationHash = 2047307595u;
    inputs.EpochCaptureBank = 3;
    inputs.EpochSource3DSerial = 126509;
    inputs.EpochSource3DSceneHash = 4094556902u;
    inputs.CaptureRequestConsumesCurrentComposite = true;
    inputs.CaptureRequestBank = 2;

    const CaptureEpochOverlayCurrentPlan plan =
        MakeCaptureEpochOverlayCurrentPlan(inputs);
    CHECK_EQ(plan.BackgroundSource, SourceABackgroundSource::ParentOutputTex3D);
    CHECK_EQ(plan.BackgroundEpochSerial, 0u);
    CHECK_EQ(plan.Source3DSerial, 126510u);
    CHECK_EQ(plan.Source3DSceneHash, 2029878419u);
    CHECK_EQ(plan.PresentationHash, 2047307595u);
    CHECK(plan.CanPublishRouteProduct);
    CHECK_EQ(plan.CaptureBank, 2u);
}

POLICY_TEST(CaptureEpochOverlayCurrentPublicationRequiresCausalBank)
{
    CaptureEpochOverlayCurrentInputs inputs = {};
    inputs.CurrentSource3DSerial = 126510;
    inputs.CurrentSource3DSceneHash = 2029878419u;
    inputs.CurrentPresentationHash = 2047307595u;
    inputs.EpochCaptureBank = 3;
    inputs.EpochSource3DSerial = 126509;
    inputs.EpochSource3DSceneHash = 4094556902u;

    CaptureEpochOverlayCurrentPlan plan =
        MakeCaptureEpochOverlayCurrentPlan(inputs);
    CHECK(!plan.CanPublishRouteProduct);
    CHECK_EQ(plan.CaptureBank, 0xFFFFFFFFu);

    inputs.CurrentSource3DSerial = inputs.EpochSource3DSerial;
    inputs.CurrentSource3DSceneHash = inputs.EpochSource3DSceneHash;
    plan = MakeCaptureEpochOverlayCurrentPlan(inputs);
    CHECK(plan.CanPublishRouteProduct);
    CHECK_EQ(plan.CaptureBank, 3u);
}

// Recorded from Lufia's title/menu flicker at frame 554 in
// recorded-csv/lufia/20260722-001903/
// whole-scene-frame-times-20260722-001903-renderer-details.csv.
// The old renderer selected BackgroundProduct + current overlay even though
// its exact-event route product and full-product candidate identify capture
// event 2896, bank 2, presentation hash and Source3D scene identically.
POLICY_TEST(Recorded_LufiaExactRouteProductSameEvent)
{
    SourceAExactRouteProductPreferenceInputs inputs = {};
    inputs.DirectFinalBottomConsumer = true;
    inputs.SubEngineCaptureBackedBGOnly = true;
    inputs.HasRouteProduct = true;
    inputs.RouteProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
    inputs.RouteProductProof = WholeSceneCaptureProofKind::ExactCaptureEvent;
    inputs.RouteProductEventSerial = 2896;
    inputs.RouteProductCaptureBank = 2;
    inputs.RouteProductCapturePresentationHash = 4117928085u;
    inputs.RouteProductSource3DSerial = 2895;
    inputs.RouteProductSource3DSceneHash = 744483524u;
    inputs.HasFullProduct = true;
    inputs.FullProductEventValid = true;
    inputs.FullProductEventSerial = 2896;
    inputs.FullProductCaptureBank = 2;
    inputs.FullProductCapturePresentationHash = 4117928085u;
    inputs.FullProductSource3DSerial = 2895;
    inputs.FullProductSource3DSceneHash = 744483524u;
    inputs.FullProductEventFullEquivalent = true;
    inputs.FullProductEventCleanEngineA2DOutput = true;
    inputs.FullProductEventAccepted = true;

    CHECK(ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));

    SourceACaptureResolutionInputs resolution = {};
    resolution.HasRouteProduct = true;
    resolution.RouteProductNeedsRePresentation = true;
    resolution.CanUseCurrentOverlay = true;
    resolution.HasFullProduct = true;
    resolution.PreferExactRouteProduct =
        ShouldPreferSourceAExactRouteProductForDirectBottom(inputs);
    CHECK_EQ(ChooseSourceACaptureResolutionKind(resolution),
             SourceACaptureResolutionKind::RouteProduct);
}

// Hotel Dusk manual-dialog frame 426 is the stronger negative fixture. The
// route and full candidate are an exact identity match, but the captured event
// contains OBJ/dialog content. FullCaptureProduct must remain authoritative;
// the Lufia preference must not freeze that presentation as a route product.
POLICY_TEST(Recorded_HotelDuskDialogRejectsExactRoutePreference)
{
    SourceAExactRouteProductPreferenceInputs inputs = {};
    inputs.DirectFinalBottomConsumer = true;
    inputs.SubEngineCaptureBackedBGOnly = true;
    inputs.HasRouteProduct = true;
    inputs.RouteProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
    inputs.RouteProductProof = WholeSceneCaptureProofKind::ExactCaptureEvent;
    inputs.RouteProductEventSerial = 30753;
    inputs.RouteProductCaptureBank = 2;
    inputs.RouteProductCapturePresentationHash = 2089871461u;
    inputs.RouteProductSource3DSerial = 30869;
    inputs.RouteProductSource3DSceneHash = 830609358u;
    inputs.HasFullProduct = true;
    inputs.FullProductEventValid = true;
    inputs.FullProductEventSerial = 30753;
    inputs.FullProductCaptureBank = 2;
    inputs.FullProductCapturePresentationHash = 2089871461u;
    inputs.FullProductSource3DSerial = 30869;
    inputs.FullProductSource3DSceneHash = 830609358u;
    inputs.FullProductEventFullEquivalent = true;
    inputs.FullProductEventCleanEngineA2DOutput = true;
    inputs.FullProductEventAccepted = true;
    inputs.FullProductEventSourceOBJVisible = true;

    CHECK(!ShouldPreferSourceAExactRouteProductForDirectBottom(inputs));
}

// Lifted from recorded-csv/hotel-dusk/20260702-085248/
// whole-scene-frame-times-20260702-085248-renderer-details.csv
// frames 600-640, engine(s): a,b - generated by lift-policy-rows.ps1, do not hand-edit values.
POLICY_TEST(Recorded_HotelDuskDialogSmoke)
{
    {
        // frames 600,602,604,606..640 (21 rows), engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::BackgroundProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ActiveBackgroundEpoch;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x6C71051Au;
        inputs.RequestPresentationHash = 0x6C71051Au;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frames 601,603,605,607..639 (20 rows), engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::BackgroundProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ActiveBackgroundEpoch;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0xE3E66E50u;
        inputs.RequestPresentationHash = 0xE3E66E50u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frames 601,603,605,607..639 (20 rows), engine B: path=8, effect_action=DisplayAsIs
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::DirectFinalConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::FullCaptureProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::DirectFinalPresentationMatch;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::AlreadyPresented;
        inputs.ProductPresentationHash = 0x6C71051Au;
        inputs.RequestPresentationHash = 0x6C71051Au;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
        CHECK_EQ(decision.RequiresRePresentation, false);
    }
}

// Lifted from recorded-csv/hotel-dusk/20260702-075645/
// whole-scene-frame-times-20260702-075645-renderer-details.csv
// frames 2188-2208, engine(s): a - generated by lift-policy-rows.ps1, do not hand-edit values.
POLICY_TEST(Recorded_HotelDuskFadeReleaseExemption)
{
    {
        // frame 2188, engine A: path=7, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0xC88173D1u;
        inputs.RequestPresentationHash = 0x6751C8B6u;
        inputs.HasStoredEffectState = true;
        inputs.StoredEffectActive = true;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2189, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x1AAB90E3u;
        inputs.RequestPresentationHash = 0x1AAB90E3u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2190, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x325AEFDDu;
        inputs.RequestPresentationHash = 0x325AEFDDu;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2191, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0xA2B15AEu;
        inputs.RequestPresentationHash = 0xA2B15AEu;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2192, engine A: path=7, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x325AEFDDu;
        inputs.RequestPresentationHash = 0x2E3937EDu;
        inputs.HasStoredEffectState = true;
        inputs.StoredEffectActive = true;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2193, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x159B994u;
        inputs.RequestPresentationHash = 0x159B994u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2194, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x6C248582u;
        inputs.RequestPresentationHash = 0x6C248582u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2195, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0xF8D128D7u;
        inputs.RequestPresentationHash = 0xF8D128D7u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2196, engine A: path=7, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x6C248582u;
        inputs.RequestPresentationHash = 0x8186643Cu;
        inputs.HasStoredEffectState = true;
        inputs.StoredEffectActive = true;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2197, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0xB85B7FFDu;
        inputs.RequestPresentationHash = 0xB85B7FFDu;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2198, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x3033B023u;
        inputs.RequestPresentationHash = 0x3033B023u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = true;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frames 2199,2203,2207..2207 (3 rows), engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x95B8B558u;
        inputs.RequestPresentationHash = 0x95B8B558u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2200, engine A: path=7, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x3033B023u;
        inputs.RequestPresentationHash = 0x927E5EB3u;
        inputs.HasStoredEffectState = true;
        inputs.StoredEffectActive = true;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frames 2201,2205..2205 (2 rows), engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x68E2CD58u;
        inputs.RequestPresentationHash = 0x68E2CD58u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2202, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x731EF21Eu;
        inputs.RequestPresentationHash = 0x731EF21Eu;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2204, engine A: path=7, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x731EF21Eu;
        inputs.RequestPresentationHash = 0xE1859FA7u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2206, engine A: path=9, effect_action=CompositeCurrentOverlay
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::LiveOverlayProducer;
        inputs.ProductKind = WholeSceneCaptureProductKind::ParentOutput3D;
        inputs.ProofKind = WholeSceneCaptureProofKind::RouteStateIdentity;
        inputs.RenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x85EE5DDEu;
        inputs.RequestPresentationHash = 0x85EE5DDEu;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
    {
        // frame 2208, engine A: path=7, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::HandoffConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x85EE5DDEu;
        inputs.RequestPresentationHash = 0xC4D7B7EFu;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
}

// Regression case: stored brightness is present, but
// exact-event proof keeps the raw event product admissible after release.
// Lifted from recorded-csv/dragon-quest-v/20260721-200656/
// whole-scene-frame-times-20260721-200656-renderer-details.csv
// frames 55-55, engine(s): b - generated by lift-policy-rows.ps1, do not hand-edit values.
POLICY_TEST(Recorded_DQVSquareEnixLogo)
{
    {
        // frame 55, engine B: path=8, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::CapturedLayerConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0x4BA787E5u;
        inputs.RequestPresentationHash = 0xBB2E540u;
        inputs.HasStoredEffectState = true;
        inputs.StoredEffectActive = true;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
}

// Lifted from recorded-csv/mario-kart/20260721-235549/
// whole-scene-frame-times-20260721-235549-renderer-details.csv
// frames 665-665, engine(s): b - generated by lift-policy-rows.ps1, do not hand-edit values.
POLICY_TEST(Recorded_MarioKartPath8Capture)
{
    {
        // frame 665, engine B: path=8, effect_action=ApplyOnBlit
        WholeSceneCaptureProductUseInputs inputs = {};
        inputs.PolicyAccepted = true;
        inputs.HasTexture = true;
        inputs.RequestKind = WholeSceneCaptureRequestKind::CapturedLayerConsumer;
        inputs.ProductKind = WholeSceneCaptureProductKind::RouteEventProduct;
        inputs.ProofKind = WholeSceneCaptureProofKind::ExactCaptureEvent;
        inputs.RenderAction = WholeSceneCaptureRenderAction::BlitExactProduct;
        inputs.PresentationClass = WholeSceneCaptureProductPresentationClass::RawContent;
        inputs.ProductPresentationHash = 0xF552E219u;
        inputs.RequestPresentationHash = 0xF661CE96u;
        inputs.HasStoredEffectState = false;
        inputs.StoredEffectActive = false;
        inputs.ConsumeEffectActive = false;

        const WholeSceneCaptureProductUseDecision decision =
            CanUseWholeSceneCaptureProduct(inputs);
        CHECK_EQ(decision.Accepted, true);
        CHECK_EQ(decision.EffectPhaseIncompatible, false);
    }
}

// Recorded in recorded-csv/kingdom-hearts/20260722-011554/
// whole-scene-frame-times-20260722-011554-renderer-details.csv.
// Kingdom Hearts post-battle return, both engines: frames 834-845 are the
// direct-output screen-swap excursion guarded on Current; frame 846 releases
// to the normal whole-scene scale decision before the baseline route returns
// at frame 847. The renderer-details CSV does not serialize the policy input
// boolean directly, but path=1/current_reason=2 records the guard decision.
POLICY_TEST(Recorded_KingdomHeartsPostBattleScreenSwapGuard)
{
    WholeScenePathDecisionInputs inputs = {};
    inputs.CanUseScalePath = true;
    inputs.ConservativeHybridMode = true;

    // Frames 834-845, engines A and B: eligibility=8, path=1,
    // current_path_reason=2 (HybridPresentationGuard).
    inputs.HybridPresentationGuardActive = true;
    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::HybridPresentationGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason,
             WholeSceneCurrentPathReason::HybridPresentationGuard);

    // Frame 846, engines A and B: the final route is still in the temporary
    // screen arrangement, but the guard has released and path 6 resumes.
    inputs.HybridPresentationGuardActive = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::WholeSceneScale);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::None);
    CHECK_EQ(decision.CurrentReason, WholeSceneCurrentPathReason::None);
}
