// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

// Table tests for WholeSceneScalePolicy: the frame-level path decision
// precedence that the renderer-details CSV serializes as a_path/b_path.

#include "PolicyTestHarness.h"
#include "WholeSceneScalePolicy.h"
#include "WholeSceneOutputPlan.h"

using namespace melonDS;

namespace
{

StrictAffineHighResEligibilityInputs MakeStrictAffineEligibleInputs()
{
    StrictAffineHighResEligibilityInputs inputs = {};
    inputs.FeatureEnabled = true;
    inputs.ConservativeHybridMode = true;
    inputs.ScalePathAvailable = true;
    inputs.OutputScale = 4;
    inputs.YStart = 0;
    inputs.YEnd = 192;
    inputs.DisplayModeComposited = true;
    inputs.SnapshotCoherent = true;
    inputs.CandidateTargetAvailable = true;
    inputs.HighResolutionGeometryRequired = true;
    inputs.VisibleLayerMask = 1u << 3;
    inputs.TiledAffineBGMask = 1u << 3;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    return inputs;
}

POLICY_TEST(AffineOBJPresentationPriorityMatchesDSOrdering)
{
    AffineOBJPresentationPriorityInputs inputs = {};
    inputs.CandidatePriority = 2;
    inputs.CandidateOAMIndex = 30;

    inputs.TopClass = AffineOBJPresentationTopClass::Backdrop;
    CHECK(DoesAffineOBJPresentationCandidateWin(inputs));

    inputs.TopClass = AffineOBJPresentationTopClass::BG;
    inputs.TopPriority = 2;
    CHECK(DoesAffineOBJPresentationCandidateWin(inputs));
    inputs.TopPriority = 1;
    CHECK(!DoesAffineOBJPresentationCandidateWin(inputs));

    inputs.TopClass = AffineOBJPresentationTopClass::OBJ;
    inputs.TopPriority = 2;
    inputs.TopOAMIndex = 31;
    CHECK(DoesAffineOBJPresentationCandidateWin(inputs));
    inputs.TopOAMIndex = 30;
    CHECK(!DoesAffineOBJPresentationCandidateWin(inputs));
    inputs.TopIsSameAffineCandidate = true;
    CHECK(DoesAffineOBJPresentationCandidateWin(inputs));
    inputs.TopIsSameAffineCandidate = false;
    inputs.TopOAMIndex = 29;
    CHECK(!DoesAffineOBJPresentationCandidateWin(inputs));

    inputs.TopClass = AffineOBJPresentationTopClass::Unsupported;
    CHECK(!DoesAffineOBJPresentationCandidateWin(inputs));
}

}

POLICY_TEST(StrictAffineHighResAdmitsCompleteSingleAffineScene)
{
    const StrictAffineHighResEligibilityInputs inputs =
        MakeStrictAffineEligibleInputs();
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);
    CHECK(CanUseStrictAffineHighResCandidate(inputs));
}

POLICY_TEST(StrictAffineHighResAdmitsFullFrameMasterBrightness)
{
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::MasterBrightness);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    // Strict affine remains a full-frame representation. A scanline band
    // must continue through the existing partial-range fallback until the
    // compositor has band-scoped source and transform lifetimes.
    inputs.YStart = 1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::IncompleteRange);
}

POLICY_TEST(StrictAffineDebugProductsUseScaleOneAsPairedControl)
{
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.OutputScale = 1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::ScaleFactorOne);
    CHECK(CanProduceStrictAffineDebugProducts(inputs));
    CHECK_EQ(ChooseStrictAffineDebugProductClass(inputs),
             StrictAffineDebugProductClass::ProductionEligible);

    inputs.FeatureEnabled = false;
    CHECK(!CanProduceStrictAffineDebugProducts(inputs));

    inputs = MakeStrictAffineEligibleInputs();
    inputs.YEnd = 191;
    CHECK(!CanProduceStrictAffineDebugProducts(inputs));

    inputs = MakeStrictAffineEligibleInputs();
    inputs.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::Windows);
    CHECK(!CanProduceStrictAffineDebugProducts(inputs));
}

POLICY_TEST(StrictAffineHighResAdmitsDualExtendedAffineOBJ)
{
    // Mario Kart race map, whole-scene-2d-both-20260803-223157, Engine B.
    auto inputs = MakeStrictAffineEligibleInputs();
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;
    constexpr u32 obj = 1u << 4;
    inputs.VisibleLayerMask = bg2 | bg3 | obj;
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = bg2 | bg3;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    CHECK_EQ(inputs.RequiredChannels, 0x5023Aull);

    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);
    CHECK(CanUseStrictAffineHighResCandidate(inputs));
    CHECK_EQ(ChooseStrictAffineDebugProductClass(inputs),
             StrictAffineDebugProductClass::DualExtendedAffineWithOBJ);
    CHECK(CanProduceStrictAffineDebugProducts(inputs));

    auto rejected = inputs;
    rejected.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::Windows);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(rejected),
             StrictAffineDebugProductClass::None);

    rejected = inputs;
    rejected.VisibleLayerMask &= ~obj;
    rejected.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::OBJGeometry);
    rejected.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(rejected),
             StrictAffineDebugProductClass::ProductionEligible);

    rejected = inputs;
    rejected.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::OBJGeometry);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(rejected),
             StrictAffineDebugProductClass::None);

    rejected = inputs;
    rejected.ExtendedTiledAffineBGMask = bg3;
    CHECK_EQ(ChooseStrictAffineDebugProductClass(rejected),
             StrictAffineDebugProductClass::None);

    rejected = inputs;
    rejected.YEnd = 191;
    CHECK_EQ(ChooseStrictAffineDebugProductClass(rejected),
             StrictAffineDebugProductClass::None);

    // The same race map alternates between affine and ordinary OBJ frames.
    auto ordinaryOBJ = inputs;
    ordinaryOBJ.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    CHECK_EQ(ordinaryOBJ.RequiredChannels, 0x5022Aull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(ordinaryOBJ),
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(ordinaryOBJ),
             StrictAffineDebugProductClass::DualExtendedAffineWithOBJ);

    // The later race-map recording alternates into mode-1 ordinary OBJ.
    // Binary native presence plus authored EVA/EVB is a modeled operator;
    // it does not grant affine or bitmap-alpha OBJ presentation coverage.
    auto semiTransparentOrdinaryOBJ = ordinaryOBJ;
    semiTransparentOrdinaryOBJ.HasOrdinaryOBJ = true;
    semiTransparentOrdinaryOBJ.HasSemiTransparentOrdinaryOBJ = true;
    semiTransparentOrdinaryOBJ.AlphaBlendTarget2Mask = 0x3Fu;
    semiTransparentOrdinaryOBJ.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ);
    CHECK_EQ(semiTransparentOrdinaryOBJ.RequiredChannels, 0x5122Aull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(
                 semiTransparentOrdinaryOBJ),
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(
                 semiTransparentOrdinaryOBJ),
             StrictAffineDebugProductClass::DualExtendedAffineWithOBJ);

    auto unprovenSemiTransparentOBJ = semiTransparentOrdinaryOBJ;
    unprovenSemiTransparentOBJ.HasUnsupportedSemiTransparentOBJ = true;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(
                 unprovenSemiTransparentOBJ),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    auto semiTransparentAffineOBJ = inputs;
    semiTransparentAffineOBJ.HasSemiTransparentAffineOBJ = true;
    semiTransparentAffineOBJ.AlphaBlendTarget2Mask = 0x3Fu;
    semiTransparentAffineOBJ.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ);
    CHECK_EQ(semiTransparentAffineOBJ.RequiredChannels, 0x5123Aull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(
                 semiTransparentAffineOBJ),
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(
                 semiTransparentAffineOBJ),
             StrictAffineDebugProductClass::DualExtendedAffineWithOBJ);

    auto bitmapAlphaOBJ = semiTransparentAffineOBJ;
    bitmapAlphaOBJ.HasUnsupportedSemiTransparentOBJ = true;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(bitmapAlphaOBJ),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);
}

POLICY_TEST(StrictAffineHighResAdmitsEffectFreeDirect3DAffineOBJOverlay)
{
    // Pokemon Diamond menu, renderer-details 20260804-032108, Engine A:
    // high-resolution Direct3D under tiled text BGs and an opaque affine OBJ.
    // The scene is stable and full-frame; the rotating Poke Ball is the affine
    // geometry while the existing whole-scene reconstruction owns text and
    // ordinary-OBJ winners.
    auto inputs = MakeStrictAffineEligibleInputs();
    constexpr u32 bg0 = 1u << 0;
    constexpr u32 bg1 = 1u << 1;
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;
    constexpr u32 obj = 1u << 4;
    inputs.VisibleLayerMask = bg0 | bg1 | bg2 | bg3 | obj;
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = 0;
    inputs.TextTiledBGMask = bg1 | bg2 | bg3;
    inputs.Direct3DLayerMask = bg0;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Direct3D) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    CHECK_EQ(inputs.RequiredChannels, 0x5023Dull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(inputs),
             StrictAffineDebugProductClass::ProductionEligible);

    auto rejected = inputs;
    rejected.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    rejected = inputs;
    rejected.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::Windows);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    rejected = inputs;
    rejected.Direct3DLayerMask = bg1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::NotExactlyOneAffineBG);

    rejected = inputs;
    rejected.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::NotExactlyOneAffineBG);
}

POLICY_TEST(StrictAffineHighResRejectsConfigurationAndRangeFailures)
{
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.FeatureEnabled = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::FeatureDisabled);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.ConservativeHybridMode = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::NotConservativeHybrid);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.ScalePathAvailable = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::ScalePathUnavailable);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.OutputScale = 1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::ScaleFactorOne);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.YStart = 32;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::IncompleteRange);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.DisplayModeComposited = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::DisplayModeNotComposited);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.SnapshotCoherent = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::SnapshotIncoherent);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.CandidateTargetAvailable = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::CandidateTargetUnavailable);
}

POLICY_TEST(StrictAffineHighResRejectsEveryUnmodeledSceneClass)
{
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask |= (1u << 1) | (1u << 2);
    inputs.TiledAffineBGMask |= (1u << 1) | (1u << 2);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::NotExactlyOneAffineBG);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.TiledAffineBGMask = 0;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::VisibleLayerNotTiledAffineBG);

    constexpr WholeSceneOutputChannel rejectedChannels[] = {
        WholeSceneOutputChannel::Direct3D,
        WholeSceneOutputChannel::OBJGeometry,
        WholeSceneOutputChannel::AffineOBJGeometry,
        WholeSceneOutputChannel::Windows,
        WholeSceneOutputChannel::OBJWindow,
        WholeSceneOutputChannel::Mosaic,
        WholeSceneOutputChannel::SemiTransparentOBJ,
        WholeSceneOutputChannel::DisplayCapture,
        WholeSceneOutputChannel::CaptureFeedback,
        WholeSceneOutputChannel::VRAMDisplay,
    };
    for (WholeSceneOutputChannel channel : rejectedChannels)
    {
        inputs = MakeStrictAffineEligibleInputs();
        inputs.RequiredChannels |= static_cast<u64>(channel);
        CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
                 StrictAffineHighResBlockReason::UnsupportedActiveChannel);
    }
}

POLICY_TEST(StrictAffineHighResAdmitsBitmapAffineOBJOverTextUnderlay)
{
    // Lego Harry Potter Years 5-7, renderer-details 20260822-135526,
    // Engine B: BG0/BG1 are tiled text, all visible OBJ are direct-color
    // bitmap material, and the selected 32x32 spell icon is affine. Its
    // PalOffset alpha must be resolved against BG1 before presentation
    // coverage softens the transformed contour.
    auto inputs = MakeStrictAffineEligibleInputs();
    constexpr u32 bg0 = 1u << 0;
    constexpr u32 bg1 = 1u << 1;
    constexpr u32 obj = 1u << 4;
    inputs.VisibleLayerMask = bg0 | bg1 | obj;
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = 0;
    inputs.TextTiledBGMask = bg0 | bg1;
    inputs.Direct3DLayerMask = 0;
    inputs.HasOrdinaryOBJ = true;
    inputs.HasBitmapOrdinaryOBJ = true;
    inputs.HasBitmapAffineOBJ = true;
    inputs.AlphaBlendTarget1Mask = obj;
    inputs.AlphaBlendTarget2Mask = bg1;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend) |
        static_cast<u64>(WholeSceneOutputChannel::SemiTransparentOBJ) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    CHECK_EQ(inputs.RequiredChannels, 0x51A39ull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    auto rejected = inputs;
    rejected.HasBitmapAffineOBJ = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    rejected = inputs;
    rejected.AlphaBlendTarget2Mask = 0;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    rejected = inputs;
    rejected.HasSemiTransparentAffineOBJ = true;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);
}

POLICY_TEST(StrictAffineHighResYieldsToEquivalentNativeGeometry)
{
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.HighResolutionGeometryRequired = false;

    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::
                 HighResolutionGeometryNotRequired);
    CHECK(!CanUseStrictAffineHighResCandidate(inputs));

    // The strict render remains useful as a counterfactual debug export.
    CHECK_EQ(ChooseStrictAffineDebugProductClass(inputs),
             StrictAffineDebugProductClass::ProductionEligible);
}

POLICY_TEST(StrictAffineGeometryDemandHoldsForContinuousBGTransformLifetime)
{
    constexpr u32 bg3 = 1u << 3;
    StrictAffineGeometryDemandInputs inputs = {};
    inputs.VisibleAffineBGMask = bg3;
    inputs.IdentityEquivalentAffineBGMask = bg3;

    auto result = UpdateStrictAffineGeometryDemand(inputs);
    CHECK_EQ(result.TransformedAffineBGMask, 0u);
    CHECK(!result.HighResolutionGeometryRequired);

    inputs.IdentityEquivalentAffineBGMask = 0;
    result = UpdateStrictAffineGeometryDemand(inputs);
    CHECK_EQ(result.TransformedAffineBGMask, bg3);
    CHECK(result.HighResolutionGeometryRequired);

    // A brief return to identity does not change presentation paths mid-motion.
    inputs.IdentityEquivalentAffineBGMask = bg3;
    inputs.PreviousTransformedAffineBGMask = result.TransformedAffineBGMask;
    result = UpdateStrictAffineGeometryDemand(inputs);
    CHECK_EQ(result.TransformedAffineBGMask, bg3);
    CHECK(result.HighResolutionGeometryRequired);

    // The renderer supplies this only after several consecutive exact frames.
    inputs.SettledIdentityAffineBGMask = bg3;
    result = UpdateStrictAffineGeometryDemand(inputs);
    CHECK_EQ(result.TransformedAffineBGMask, 0u);
    CHECK(!result.HighResolutionGeometryRequired);

    // Disabling/repurposing the BG also clears the hold.
    inputs.VisibleAffineBGMask = 0;
    inputs.IdentityEquivalentAffineBGMask = 0;
    inputs.SettledIdentityAffineBGMask = 0;
    inputs.PreviousTransformedAffineBGMask = result.TransformedAffineBGMask;
    result = UpdateStrictAffineGeometryDemand(inputs);
    CHECK_EQ(result.TransformedAffineBGMask, 0u);
    CHECK(!result.HighResolutionGeometryRequired);
}

POLICY_TEST(StrictAffineGeometryDemandAlwaysIncludesAffineOBJ)
{
    StrictAffineGeometryDemandInputs inputs = {};
    inputs.HasAffineOBJ = true;
    const auto result = UpdateStrictAffineGeometryDemand(inputs);
    CHECK_EQ(result.TransformedAffineBGMask, 0u);
    CHECK(result.HighResolutionGeometryRequired);
}

POLICY_TEST(StrictAffineOBJWindowRequiresEnabledMode2Participant)
{
    CHECK(!HasStrictAffineActiveOBJWindow(false, true, true));
    CHECK(!HasStrictAffineActiveOBJWindow(true, false, true));
    CHECK(!HasStrictAffineActiveOBJWindow(true, true, false));
    CHECK(HasStrictAffineActiveOBJWindow(true, true, true));
}

POLICY_TEST(StrictAffineCompanionTextBGMaskIncludesEnabledUnderlays)
{
    constexpr u32 bg0 = 1u << 0;
    constexpr u32 bg1 = 1u << 1;
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;

    // BG3 represents the higher transformed affine surface. Both ordinary
    // text sources remain eligible even though BG2 is exposed underneath it.
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 bg0 | bg2 | bg3,
                 bg0 | bg1 | bg2,
                 true,
                 0,
                 false,
                 false,
                 false),
             bg0 | bg2);
}

POLICY_TEST(StrictAffineCompanionTextBGMaskRetainsAdmissionGates)
{
    constexpr u32 textBGs = (1u << 0) | (1u << 2);
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 textBGs, textBGs, false, 0, false, false, false),
             0u);
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 textBGs, textBGs, true, 1, false, false, false),
             0u);
}

POLICY_TEST(StrictAffineCompanionTextBGMaskAllowsProvenInertAlphaBlend)
{
    constexpr u32 bg1 = 1u << 1;
    constexpr u32 obj = 1u << 4;
    constexpr u32 backdrop = 1u << 5;
    StrictAffineHighResEligibilityInputs inputs = {};
    inputs.VisibleLayerMask = bg1 | obj;
    inputs.TextTiledBGMask = bg1;
    inputs.AlphaBlendTarget1Mask = 0;
    inputs.AlphaBlendTarget2Mask = backdrop;
    inputs.ColorEffectMode = 1;

    CHECK(HasStrictAffineInertAlphaBlend(inputs));
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 bg1 | obj, bg1, true, 1, false, false,
                 HasStrictAffineInertAlphaBlend(inputs)),
             bg1);

    inputs.HasSemiTransparentOrdinaryOBJ = true;
    CHECK(!HasStrictAffineInertAlphaBlend(inputs));
    inputs.HasSemiTransparentOrdinaryOBJ = false;
    inputs.HasBitmapAffineOBJ = true;
    CHECK(!HasStrictAffineInertAlphaBlend(inputs));
}

POLICY_TEST(StrictAffineCompanionTextBGMaskAllowsResolvedBitmapMaterial)
{
    constexpr u32 bg0 = 1u << 0;
    constexpr u32 bg1 = 1u << 1;
    constexpr u32 obj = 1u << 4;
    StrictAffineHighResEligibilityInputs inputs = {};
    inputs.VisibleLayerMask = bg0 | bg1 | obj;
    inputs.TextTiledBGMask = bg0 | bg1;
    inputs.HasBitmapAffineOBJ = true;
    inputs.AlphaBlendTarget2Mask = bg1;
    inputs.ColorEffectMode = 1;

    CHECK(HasStrictAffineResolvedBitmapOBJMaterial(inputs));
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 bg0 | bg1, bg0 | bg1, true, 1,
                 HasStrictAffineResolvedBitmapOBJMaterial(inputs), false,
                 false),
             bg0 | bg1);

    inputs.HasSemiTransparentAffineOBJ = true;
    CHECK(!HasStrictAffineResolvedBitmapOBJMaterial(inputs));
}

POLICY_TEST(StrictAffineCompanionTextBGMaskAllowsResolvedTextBGAlphaBlend)
{
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;
    constexpr u32 obj = 1u << 4;
    constexpr u32 backdrop = 1u << 5;

    StrictAffineHighResEligibilityInputs inputs = {};
    inputs.VisibleLayerMask = bg2 | bg3 | obj;
    inputs.TextTiledBGMask = bg2;
    inputs.ExtendedTiledAffineBGMask = bg3;
    inputs.HasOrdinaryOBJ = true;
    inputs.AlphaBlendOrdinaryOBJPrioritySafe = true;
    inputs.AlphaBlendTarget1Mask = bg2;
    inputs.AlphaBlendTarget2Mask = bg3 | backdrop;
    inputs.ColorEffectMode = 1;

    CHECK(HasStrictAffineResolvedTextBGAlphaBlend(inputs));
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 bg2 | bg3 | obj, bg2, true, 1, false,
                 HasStrictAffineResolvedTextBGAlphaBlend(inputs), false),
             bg2);
    CHECK_EQ(ChooseStrictAffineResolvedTextBGBilinearCoverageMask(
                 bg2, inputs.AlphaBlendTarget1Mask, true,
                 HasStrictAffineResolvedTextBGAlphaBlend(inputs)),
             bg2);
    CHECK_EQ(ChooseStrictAffineResolvedTextBGBilinearCoverageMask(
                 bg2, inputs.AlphaBlendTarget1Mask, false,
                 HasStrictAffineResolvedTextBGAlphaBlend(inputs)),
             0u);
    CHECK_EQ(ChooseStrictAffineConstantBackdropCompositeMask(
                 bg3, true, 1,
                 inputs.AlphaBlendTarget1Mask,
                 inputs.AlphaBlendTarget2Mask,
                 0,
                 HasStrictAffineResolvedTextBGAlphaBlend(inputs)),
             bg3);
    CHECK_EQ(ChooseStrictAffineConstantBackdropCompositeMask(
                 bg3, true, 1,
                 inputs.AlphaBlendTarget1Mask,
                 inputs.AlphaBlendTarget2Mask,
                 0,
                 false),
             0u);

    // OBJ as a blend operand would no longer be the bounded BG2-over-BG3
    // equation and must not silently admit companion source reconstruction.
    inputs.AlphaBlendTarget2Mask |= obj;
    CHECK(!HasStrictAffineResolvedTextBGAlphaBlend(inputs));
    CHECK_EQ(ChooseStrictAffineCompanionTextBGMask(
                 bg2 | bg3 | obj, bg2, true, 1, false,
                 HasStrictAffineResolvedTextBGAlphaBlend(inputs), false),
             0u);
    CHECK_EQ(ChooseStrictAffineResolvedTextBGBilinearCoverageMask(
                 bg2, inputs.AlphaBlendTarget1Mask, true,
                 HasStrictAffineResolvedTextBGAlphaBlend(inputs)),
             0u);
}

POLICY_TEST(StrictAffineHighResAdmitsSelectiveBrightnessEffects)
{
    constexpr u32 bg0 = 1u << 0;
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;
    constexpr u32 obj = 1u << 4;

    // Capability-shaped equivalent of a pause UI: an unaffected text-BG
    // overlay remains above two affine BGs and OBJ/backdrop participants that
    // are selectively darkened. No game identity participates in admission.
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask = bg0 | bg2 | bg3 | obj;
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = bg2 | bg3;
    inputs.TextTiledBGMask = bg0;
    inputs.HasOrdinaryOBJ = true;
    inputs.AlphaBlendTarget1Mask = 0x3Eu;
    inputs.AlphaBlendTarget2Mask = 0x3Fu;
    inputs.ColorEffectMode = 3;
    inputs.ColorEffectFactor = 7;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::ColorEffect) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    CHECK_EQ(inputs.RequiredChannels, 0x5063Bull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    inputs.ColorEffectMode = 2;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    inputs.ColorEffectMode = 1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    inputs.ColorEffectMode = 3;
    inputs.ColorEffectFactor = 0;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    inputs.ColorEffectFactor = 7;
    inputs.AlphaBlendTarget1Mask = 0;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);
}

POLICY_TEST(StrictAffineHighResAdmitsSafeDSQuantizedAlphaBlend)
{
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;
    constexpr u32 obj = 1u << 4;
    constexpr u32 backdrop = 1u << 5;

    // Super Mario 64 DS bottom-screen touch overlay: text BG2 is blended
    // over extended tiled-affine BG3/backdrop. Ordinary OBJ remains in front
    // of the target-1 BG and neither ordinary nor affine OBJ is a blend
    // operand in this scene.
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask = bg2 | bg3 | obj;
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = bg3;
    inputs.TextTiledBGMask = bg2;
    inputs.HasOrdinaryOBJ = true;
    inputs.AlphaBlendOrdinaryOBJPrioritySafe = true;
    inputs.AlphaBlendTarget1Mask = bg2;
    inputs.AlphaBlendTarget2Mask = bg3 | backdrop;
    inputs.RequiredChannels =
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::AlphaBlend) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);
    CHECK_EQ(inputs.RequiredChannels, 330299ull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    // BLDCNT is programmed two frames before BG2 is enabled. Inactive target
    // bits are harmless and should not make the output leave path 12 early.
    inputs.VisibleLayerMask = bg3 | obj;
    inputs.TextTiledBGMask = 0;
    inputs.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry);
    CHECK_EQ(inputs.RequiredChannels, 330298ull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    auto rejected = inputs;
    rejected.VisibleLayerMask = bg2 | bg3 | obj;
    rejected.TextTiledBGMask = bg2;
    rejected.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry);
    rejected.AlphaBlendOrdinaryOBJPrioritySafe = false;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::
                 UnsupportedAlphaBlendConfiguration);

    rejected = inputs;
    rejected.AlphaBlendTarget1Mask |= obj;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::
                 UnsupportedAlphaBlendConfiguration);

    rejected = inputs;
    rejected.AlphaBlendTarget2Mask |= obj;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::
                 UnsupportedAlphaBlendConfiguration);

    rejected = inputs;
    rejected.AlphaBlendTarget1Mask = 0;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::
                 UnsupportedAlphaBlendConfiguration);

    // With no ordinary OBJ reconstruction, the output-resolution compositor
    // directly models affine BG/OBJ alpha operands without the priority gate.
    inputs.VisibleLayerMask = bg2 | bg3;
    inputs.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::OBJGeometry);
    inputs.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry);
    inputs.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry);
    inputs.TextTiledBGMask = bg2;
    inputs.HasOrdinaryOBJ = false;
    inputs.AlphaBlendOrdinaryOBJPrioritySafe = false;
    inputs.AlphaBlendTarget1Mask = bg2;
    inputs.AlphaBlendTarget2Mask = bg3 | backdrop;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);
}

POLICY_TEST(StrictAffineHighResAdmitsSafeTiledCompanions)
{
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask |= 1u << 1;
    inputs.TextTiledBGMask = 1u << 1;
    inputs.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask |= 1u << 1;
    inputs.TiledAffineBGMask |= 1u << 1;
    inputs.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::Priority);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask |= 1u << 1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::UnsupportedCompanionLayer);
}

POLICY_TEST(StrictAffineHighResAdmitsGeneralSafeAffineTiledScenes)
{
    constexpr u32 bg0 = 1u << 0;
    constexpr u32 bg1 = 1u << 1;
    constexpr u32 bg2 = 1u << 2;
    constexpr u32 bg3 = 1u << 3;
    constexpr u32 obj = 1u << 4;
    constexpr u64 common =
        static_cast<u64>(WholeSceneOutputChannel::AffineBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Palette) |
        static_cast<u64>(WholeSceneOutputChannel::DisplaySelection) |
        static_cast<u64>(WholeSceneOutputChannel::PhysicalRouting);

    // Super Mario 64 DS bottom-screen map: one extended tiled-affine BG3
    // with ordinary OBJ HUD/markers.
    auto inputs = MakeStrictAffineEligibleInputs();
    inputs.VisibleLayerMask = bg3 | obj;
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = bg3;
    inputs.RequiredChannels = common |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority);
    CHECK_EQ(inputs.RequiredChannels, 0x5022Aull);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);
    CHECK_EQ(ChooseStrictAffineDebugProductClass(inputs),
             StrictAffineDebugProductClass::ProductionEligible);

    // The ordinary tiled-affine encoding with the same OBJ topology is the
    // same modeled geometry class.
    inputs.TiledAffineBGMask = bg3;
    inputs.ExtendedTiledAffineBGMask = 0;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    // A lone extended tiled-affine BG needs no title-specific exception.
    inputs.TiledAffineBGMask = 0;
    inputs.ExtendedTiledAffineBGMask = bg3;
    inputs.VisibleLayerMask = bg3;
    inputs.RequiredChannels = common;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    // The same bounded renderer path safely covers mixed ordinary/extended
    // affine BGs, tiled text BGs, and opaque OBJ when priority is active.
    inputs.VisibleLayerMask = bg0 | bg1 | bg2 | bg3 | obj;
    inputs.TiledAffineBGMask = bg2;
    inputs.ExtendedTiledAffineBGMask = bg3;
    inputs.TextTiledBGMask = bg0 | bg1;
    inputs.RequiredChannels = common |
        static_cast<u64>(WholeSceneOutputChannel::TextBGGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::OBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::AffineOBJGeometry) |
        static_cast<u64>(WholeSceneOutputChannel::Priority);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(inputs),
             StrictAffineHighResBlockReason::None);

    auto rejected = inputs;
    rejected.RequiredChannels &=
        ~static_cast<u64>(WholeSceneOutputChannel::Priority);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    rejected = inputs;
    rejected.RequiredChannels |=
        static_cast<u64>(WholeSceneOutputChannel::Windows);
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::UnsupportedActiveChannel);

    rejected = inputs;
    rejected.ExtendedTiledAffineBGMask |= bg1;
    rejected.TextTiledBGMask &= ~bg1;
    CHECK_EQ(ChooseStrictAffineHighResBlockReason(rejected),
             StrictAffineHighResBlockReason::NotExactlyOneAffineBG);
}

POLICY_TEST(StrictAffineSourceEnhancementIsOptInAndPostAuthority)
{
    StrictAffineSourceEnhancementInputs inputs = {};
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::Disabled);

    inputs.Enabled = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::StrictAffinePathNotSelected);

    inputs.StrictAffinePathSelected = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::AlgorithmUnsupported);

    inputs.InlineSpline36Available = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::Spline36Inline);

    inputs.InlineSpline36Available = false;
    inputs.CachedArtCNNAvailable = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::ArtCNNCachedAffineSources);

    inputs.CachedArtCNNAvailable = false;
    inputs.CachedCuNNyAvailable = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::CuNNyCachedAffineSources);

    inputs.CachedCuNNyAvailable = false;
    inputs.CachedNNEDI3Available = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources);

    inputs.CachedNNEDI3Available = false;
    inputs.CachedXBRZAvailable = true;
    CHECK_EQ(ChooseStrictAffineSourceEnhancement(inputs),
             StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources);
}

POLICY_TEST(StrictAffineCachedSourceScaleCompletesSupportedChainsAtFourTimes)
{
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::ArtCNNCachedAffineSources,
                 2),
             2);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::ArtCNNCachedAffineSources,
                 4),
             4);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::CuNNyCachedAffineSources,
                 2),
             2);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::CuNNyCachedAffineSources,
                 4),
             4);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources,
                 2),
             2);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources,
                 3),
             2);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources,
                 4),
             4);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::NNEDI3CachedAffineSources,
                 6),
             4);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources,
                 2),
             2);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources,
                 3),
             2);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources,
                 4),
             4);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::XBRZCachedAffineSources,
                 6),
             4);
    CHECK_EQ(StrictAffineCachedSourceScale(
                 StrictAffineSourceEnhancementDecision::Spline36Inline,
                 4),
             1);
}

POLICY_TEST(PathDecisionGuardBeatsEverythingExceptProducer)
{
    // The hybrid presentation guard forces Current for consumers...
    WholeScenePathDecisionInputs inputs = {};
    inputs.HybridPresentationGuardActive = true;
    inputs.CanUseScalePath = true;
    inputs.CapturePlan = MakeWholeSceneSourceACaptureReplacementPlan();

    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::HybridPresentationGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason, WholeSceneCurrentPathReason::HybridPresentationGuard);

    // ...but a producer plan that is allowed during the guard still runs, so
    // the capture epoch keeps updating while presentation is held (A9 must
    // keep producing; only consumption is guarded).
    inputs.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(0);
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::CaptureEpochOverlay);

    // A producer plan with the permission revoked is guarded like a consumer.
    inputs.CapturePlan = MakeWholeSceneCaptureEpochOverlayPlan(0, false);
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::HybridPresentationGuard);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
}

POLICY_TEST(PathDecisionHandoffRunsBeforeGeneralFallbacks)
{
    // CaptureBackedHandoff (path 7) outranks every general fallback.
    WholeScenePathDecisionInputs inputs = {};
    inputs.CapturePlan = MakeWholeSceneCaptureBackedHandoffPlan();
    inputs.SplitLegacyFallbackAvailable = true;
    inputs.CurrentFallbackAvailable = true;
    inputs.CanUseScalePath = true;

    const WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::CaptureBackedHandoff);
}

POLICY_TEST(PathDecisionFallbackOrdering)
{
    // Physical-final postprocess native input comes first among the remaining
    // general fallbacks.
    WholeScenePathDecisionInputs inputs = {};
    inputs.PhysicalFinalPostprocessNativeInputAvailable = true;
    inputs.SplitLegacyFallbackAvailable = true;

    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::PhysicalFinalPostprocessInput);

    // Then the split legacy fallback.
    inputs.PhysicalFinalPostprocessNativeInputAvailable = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::SplitLegacyFallback);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::LegacyNativeUpscale);
}

POLICY_TEST(PathDecisionAfterGeneralFallbacksAndTail)
{
    // Source-A capture replacement (path 8) is an after-general-fallbacks
    // plan: general fallbacks win when available, and the plan only fires
    // once none of them are.
    WholeScenePathDecisionInputs inputs = {};
    inputs.CapturePlan = MakeWholeSceneSourceACaptureReplacementPlan();
    inputs.SplitLegacyFallbackAvailable = true;

    WholeScenePathDecision decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::SplitLegacyFallback);

    inputs.SplitLegacyFallbackAvailable = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::SourceACaptureReplacement);

    // Fragmentation current fallback.
    inputs.CapturePlan = {};
    inputs.CurrentFallbackAvailable = true;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeScenePathDecisionReason::FragmentationOrUnsafeFrameCurrentFallback);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason,
             WholeSceneCurrentPathReason::FragmentationOrUnsafeFrame);

    // The whole-scene scale decision leaves Path to the scale chooser.
    inputs.CurrentFallbackAvailable = false;
    inputs.CanUseScalePath = true;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::WholeSceneScale);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::None);

    // Nothing available: Current/ScalePathUnavailable. This is a legitimate
    // long-lived state (Gormiti sits in it for 1200+ frames) — never key a
    // guard on this reason code.
    inputs.CanUseScalePath = false;
    decision = ChooseWholeScenePathDecision(inputs);
    CHECK_EQ(decision.Reason, WholeScenePathDecisionReason::ScalePathUnavailable);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::Current);
    CHECK_EQ(decision.CurrentReason, WholeSceneCurrentPathReason::ScalePathUnavailable);
}

POLICY_TEST(OverlayScaleDecisionBranches)
{
    // A before-general-fallbacks capture plan overrides the overlay choice.
    WholeSceneOverlayScaleDecisionInputs inputs = {};
    inputs.CapturePlan = MakeWholeSceneCaptureBackedHandoffPlan();
    inputs.ConservativeHybridMode = true;

    WholeSceneScaleDecision decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeSceneScaleDecisionReason::CaptureBackedBeforeGeneralFallbacks);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::CaptureBackedHandoff);

    // Hybrid fragmentation guard demotes to the final-image upscale floor.
    inputs.CapturePlan = {};
    inputs.HybridFragmentationGuardActive = true;
    decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Reason,
             WholeSceneScaleDecisionReason::HybridFragmentationFinalUpscale);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::FinalNativeUpscale);
    CHECK(decision.HybridFragmentationFallback);

    // Otherwise the mode split decides the overlay operator path.
    inputs.HybridFragmentationGuardActive = false;
    decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Reason, WholeSceneScaleDecisionReason::OverlayOperator);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::ConservativeHybridUpscale);

    inputs.ConservativeHybridMode = false;
    decision = ChooseWholeSceneOverlayScaleDecision(inputs);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::OverlayOperatorUpscale);
}

POLICY_TEST(HybridSourceDecisionQuadrants)
{
    // Conservative hybrid with active Direct3D: render the foreground
    // candidate (and the 2D base only when enabled).
    HybridSourceDecisionInputs inputs = {};
    inputs.ConservativeHybridRequested = true;
    inputs.ActiveDirect3D = true;

    HybridSourceDecision decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason, HybridSourceDecisionReason::ConservativeHybridWithDirect3D);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::ConservativeHybridUpscale);
    CHECK(decision.RenderForegroundCandidate);
    CHECK(!decision.RenderForeground2DBase);
    CHECK(!decision.RenderNativeFallback);

    inputs.Foreground2DBaseEnabled = true;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK(decision.RenderForeground2DBase);

    // Conservative hybrid without Direct3D: per-pixel native fallback only.
    inputs = {};
    inputs.ConservativeHybridRequested = true;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason,
             HybridSourceDecisionReason::ConservativeHybridWithoutDirect3D);
    CHECK(decision.RenderNativeFallback);
    CHECK(!decision.RenderForegroundCandidate);

    // Overlay-suppressed Direct3D turns hybrid off for the frame.
    inputs = {};
    inputs.ConservativeHybridRequested = true;
    inputs.OverlaySuppressedDirect3D = true;
    inputs.ActiveDirect3D = true;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason, HybridSourceDecisionReason::SuppressedDirect3DOverlay);
    CHECK(!decision.EffectiveConservativeHybrid);
    CHECK_EQ(decision.Path, WholeSceneRenderPath::OverlayOperatorUpscale);

    // Suppression is meaningless without a conservative-hybrid request.
    inputs.ConservativeHybridRequested = false;
    decision = ChooseHybridSourceDecision(inputs);
    CHECK_EQ(decision.Reason, HybridSourceDecisionReason::OverlayOperator);
    CHECK(!decision.OverlaySuppressedDirect3D);
}

POLICY_TEST(SuppressedDirect3DOverlayDistinguishesNSMBFromMetroid)
{
    const u32 bg1 = 1u << 1;
    const u32 bg2 = 1u << 2;
    const u32 bg3 = 1u << 3;
    const u32 obj = 1u << 4;

    // NSMB title:
    // Debug Exports/NSMB/whole-scene-2d-both-20260729-103206
    // Direct3D is target1 at zero weight and BG1-3 are target2 at full weight.
    // This is target2 pass-through and must remain in Conservative Hybrid so
    // the already-validated clean native-stack candidate can own the frame.
    CHECK(!ShouldUseSuppressedDirect3DPresentationOverlay(
        0x0E41,
        0,
        16,
        bg1 | bg2 | bg3 | obj));

    // Metroid Prime Hunters title:
    // Debug Exports/Metroid Prime Hunters/whole-scene-2d-both-20260729-110113
    // Both standard operands have zero weight, and current Direct3D contains a
    // different menu scene. Preserve the presentation-overlay override.
    CHECK(ShouldUseSuppressedDirect3DPresentationOverlay(
        0x2C41,
        0,
        0,
        bg2 | bg3));

    // Partial target2 weight is not pass-through and still needs the overlay
    // path until that blend is modeled inside Conservative Hybrid.
    CHECK(ShouldUseSuppressedDirect3DPresentationOverlay(
        0x0E41,
        0,
        8,
        bg1 | bg2 | bg3));

    // EBA retry-style contributing Direct3D is not a zero-weight case and must
    // not be caught by this frame-level override.
    CHECK(!ShouldUseSuppressedDirect3DPresentationOverlay(
        0x2D51,
        16,
        16,
        bg2 | bg3 | obj));
}

POLICY_TEST(RenderPathHelpers)
{
    CHECK(WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::FinalNativeUpscale));
    CHECK(WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::OverlayOperatorUpscale));
    CHECK(WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::ConservativeHybridUpscale));
    CHECK(!WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::CaptureBackedHandoff));
    CHECK(!WholeSceneRenderPathUsesFullFrameFinalizer(
        WholeSceneRenderPath::Current));

    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff),
             WholeSceneRenderPath::CaptureBackedHandoff);
    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement),
             WholeSceneRenderPath::SourceACaptureReplacement);
    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay),
             WholeSceneRenderPath::CaptureEpochOverlay);
    CHECK_EQ(WholeSceneRenderPathForCaptureBackedPlanKind(
                 WholeSceneCaptureBackedPlanKind::None),
             WholeSceneRenderPath::None);
}

POLICY_TEST(JokerIncompleteFinalizerRebuildsMissingCurrentPrefix)
{
    // Recorded Dragon Quest Monsters: Joker frame: the final display consumes
    // current Engine A output, but native products stop at row 101. No earlier
    // current chunks are valid, so starting at LastLine leaves rows 0-100 stale.
    IncompleteFinalizerFallbackInputs inputs = {};
    inputs.LastLine = 101;
    CHECK_EQ(ChooseIncompleteFinalizerCurrentStart(inputs), 0);

    // The May 24 fragmentation protection still applies to unsafe frames:
    // those earlier ranges were already rendered by the current compositor.
    inputs.EarlierCurrentChunksValid = true;
    CHECK_EQ(ChooseIncompleteFinalizerCurrentStart(inputs), 101);

    inputs.LastLine = 0;
    CHECK_EQ(ChooseIncompleteFinalizerCurrentStart(inputs), 0);
}

POLICY_TEST(DeferredNativeProductAdmissionMatchesFinalizerTransitions)
{
    // A startup display-off -> composited transition changes eligibility.
    // Deferring its later rows would leave output missing when the display
    // consumes a partial range, since the full-frame finalizer rejects it.
    CHECK(!CanDeferWholeSceneNativeProductEpoch(false,
        WholeSceneNativeProductEpochInvalidEligibility |
        WholeSceneNativeProductEpochInvalidMidFrameState));
    CHECK(!CanDeferWholeSceneNativeProductEpoch(false,
        WholeSceneNativeProductEpochInvalidPath |
        WholeSceneNativeProductEpochInvalidMidFrameState));

    // Preserve ordinary frames and partial register updates with valid rows.
    CHECK(CanDeferWholeSceneNativeProductEpoch(true, 0));
    CHECK(CanDeferWholeSceneNativeProductEpoch(false,
        WholeSceneNativeProductEpochInvalidMidFrameState));
    CHECK(!CanDeferWholeSceneNativeProductEpoch(false, 0));

    for (u32 reasons = 0; reasons < 16; reasons++)
    {
        for (bool valid : {false, true})
        {
            WholeSceneNativeProductFinalizerInputs inputs = {};
            inputs.FinalizerPathSeen = true;
            inputs.EpochValid = valid;
            inputs.EpochInvalidReason = reasons;
            inputs.RowIdentityValid = true;
            inputs.IdentityRows = 192;
            inputs.FrameIdentity = 1;
            inputs.FrameComplete = true;
            CHECK_EQ(CanDeferWholeSceneNativeProductEpoch(valid, reasons),
                     CanFinalizeWholeSceneRowOwnedNativeProduct(inputs));
        }
    }
}

POLICY_TEST(RowOwnedNativeProductFinalizerAdmitsOnlyMidFrameStateInvalidation)
{
    WholeSceneNativeProductFinalizerInputs inputs = {};
    inputs.FinalizerPathSeen = true;
    inputs.EpochValid = false;
    inputs.EpochInvalidReason =
        WholeSceneNativeProductEpochInvalidMidFrameState;
    inputs.RowIdentityValid = true;
    inputs.IdentityRows = 192;
    inputs.FrameIdentity = 1;
    inputs.FrameComplete = true;
    CHECK(CanFinalizeWholeSceneRowOwnedNativeProduct(inputs));

    inputs.EpochInvalidReason |=
        WholeSceneNativeProductEpochInvalidEligibility;
    CHECK(!CanFinalizeWholeSceneRowOwnedNativeProduct(inputs));

    inputs.EpochInvalidReason =
        WholeSceneNativeProductEpochInvalidMidFrameState |
        WholeSceneNativeProductEpochInvalidPath;
    CHECK(!CanFinalizeWholeSceneRowOwnedNativeProduct(inputs));

    inputs.EpochValid = true;
    inputs.EpochInvalidReason =
        WholeSceneNativeProductEpochInvalidNone;
    CHECK(CanFinalizeWholeSceneRowOwnedNativeProduct(inputs));

    inputs.EpochValid = false;
    inputs.EpochInvalidReason =
        WholeSceneNativeProductEpochInvalidMidFrameState;
    inputs.IdentityRows = 191;
    CHECK(!CanFinalizeWholeSceneRowOwnedNativeProduct(inputs));
}

POLICY_TEST(DeferredScanlineStrictAffineRequiresCompleteSourceStableRows)
{
    DeferredScanlineStrictAffineInputs inputs = {};
    inputs.FeatureEnabled = true;
    inputs.ConservativeHybridMode = true;
    inputs.OutputScale = 4;
    inputs.CandidateTargetAvailable = true;
    inputs.NativeFrameComplete = true;
    inputs.NativeRowIdentityValid = true;
    inputs.RouteAndPresentationUniform = true;
    inputs.AllRowsSupported = true;
    inputs.OrdinaryOBJPresentationSupported = true;
    inputs.HighResolutionGeometryRequired = true;
    inputs.LiveSourceGeneration = 41;
    for (int y = 0; y < 192; y++)
    {
        inputs.RowValid[y] = true;
        inputs.RowSourceGeneration[y] = 41;
        inputs.LiveRowSourceGeneration[y] = 41;
    }

    auto result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK(result.Eligible);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::None);
    CHECK_EQ(result.SourceGeneration, 41ull);

    inputs.RowValid[96] = false;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK(!result.Eligible);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::NativeRowMissing);

    inputs.RowValid[96] = true;
    inputs.RowSourceGeneration[96] = 42;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::LiveSourceMismatch);

    for (int y = 0; y < 96; y++)
    {
        inputs.RowSourceGeneration[y] = 40;
        inputs.HistoricalRowSourceGeneration[y] = 40;
    }
    for (int y = 96; y < 192; y++)
        inputs.RowSourceGeneration[y] = 41;
    inputs.HistoricalSourceAvailable = true;
    inputs.HistoricalSourceYEnd = 96;
    inputs.HistoricalSourceGeneration = 40;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK(result.Eligible);
    CHECK(result.HistoricalSourceUsed);
    CHECK_EQ(result.HistoricalSourceYEnd, 96);
    CHECK_EQ(result.HistoricalSourceGeneration, 40ull);

    inputs.RowSourceGeneration[95] = 41;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::NativeRowSourceMismatch);

    inputs.RowSourceGeneration[95] = 40;
    inputs.HistoricalSourceAvailable = false;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::LiveSourceMismatch);

    inputs.HistoricalSourceEpochCount = 2;
    inputs.HistoricalSourceEpochAvailable[0] = true;
    inputs.HistoricalSourceEpochAvailable[1] = true;
    inputs.HistoricalSourceEpochYStart[0] = 0;
    inputs.HistoricalSourceEpochYEnd[0] = 64;
    inputs.HistoricalSourceEpochYStart[1] = 64;
    inputs.HistoricalSourceEpochYEnd[1] = 96;
    inputs.HistoricalSourceEpochGeneration[0] = 39;
    inputs.HistoricalSourceEpochGeneration[1] = 40;
    for (int y = 0; y < 64; y++)
    {
        inputs.RowSourceGeneration[y] = 39;
        inputs.HistoricalSourceEpochRowGeneration[0][y] = 39;
    }
    for (int y = 64; y < 96; y++)
    {
        inputs.RowSourceGeneration[y] = 40;
        inputs.HistoricalSourceEpochRowGeneration[1][y] = 40;
    }
    for (int y = 96; y < 192; y++)
        inputs.RowSourceGeneration[y] = 41;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK(result.Eligible);
    CHECK_EQ(result.HistoricalSourceEpochCount, 2u);
    CHECK_EQ(result.HistoricalSourceYEnd, 96);

    inputs.HistoricalSourceEpochYStart[1] = 63;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::NativeRowSourceMismatch);
    inputs.HistoricalSourceEpochYStart[1] = 64;
    inputs.HistoricalSourceEpochAvailable[1] = false;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::NativeRowSourceMismatch);
    inputs.HistoricalSourceEpochCount =
        DeferredScanlineStrictAffineInputs::MaxHistoricalSourceEpochs + 1;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::NativeRowSourceMismatch);
    inputs.HistoricalSourceEpochCount = 0;
    inputs.HistoricalSourceAvailable = false;

    for (int y = 0; y < 192; y++)
    {
        inputs.RowSourceGeneration[y] = 41;
        inputs.LiveRowSourceGeneration[y] = 41;
    }

    inputs.RowSourceGeneration[96] = 41;
    inputs.LiveSourceGeneration = 43;
    for (int y = 0; y < 192; y++)
        inputs.LiveRowSourceGeneration[y] = 43;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::LiveSourceMismatch);

    inputs.LiveSourceGeneration = 41;
    for (int y = 0; y < 192; y++)
        inputs.LiveRowSourceGeneration[y] = 41;
    inputs.OrdinaryOBJPresentationRequired = true;
    inputs.OrdinaryOBJPresentationSupported = false;
    result = AssessDeferredScanlineStrictAffine(inputs);
    CHECK_EQ(result.BlockReason,
             DeferredScanlineStrictAffineBlockReason::
                 UnsupportedOrdinaryOBJPresentation);
}

POLICY_TEST(InfiniteSpaceCaptureBackedBrightnessReleasePlansPhysicalTransform)
{
    // Infinite Space: Engine B releases full-white brightness at line 141
    // while BG2 consumes a crossing capture generated by a full whole-scene
    // source. Plan the prior full-white physical transform through the affected
    // handoff; the clean handoff after it adopts the released state normally.
    CaptureBackedBrightnessReleaseInputs inputs = {};
    inputs.WholeSceneScaleRequested = true;
    inputs.Line = 141;
    inputs.PreviousMasterBrightness = 0x4010;
    inputs.CurrentMasterBrightness = 0;
    inputs.CrossingFullWholeSceneCaptureMask = 1u << 2;
    CHECK(ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));

    inputs.WholeSceneScaleRequested = false;
    CHECK(!ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));

    inputs.WholeSceneScaleRequested = true;
    inputs.CrossingFullWholeSceneCaptureMask = 0;
    CHECK(!ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));

    inputs.CrossingFullWholeSceneCaptureMask = 1u << 2;
    inputs.PreviousMasterBrightness = 0x400F;
    CHECK(!ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));

    inputs.PreviousMasterBrightness = 0x8010;
    CHECK(!ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));

    inputs.PreviousMasterBrightness = 0x4010;
    inputs.CurrentMasterBrightness = 0x4001;
    CHECK(!ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));

    inputs.CurrentMasterBrightness = 0;
    inputs.Line = 0;
    CHECK(!ShouldPlanPhysicalBrightnessForCaptureBackedRelease(inputs));
}
