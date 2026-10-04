// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolicyTestHarness.h"
#include "WholeSceneOBJBandPolicy.h"
#include "WholeSceneConnectedOBJPolicy.h"
#include "WholeSceneOpaqueAssemblyPolicy.h"

using namespace melonDS;

POLICY_TEST(OpaqueAssemblyPreservesNestedNativeOrderAndRejectsInterference)
{
    ConnectedOBJSourceInput s[4];
    s[0].OAM=0; s[0].MatrixIndex=0; s[0].Priority=1;
    s[0].X=16; s[0].Y=16;
    s[0].Width=s[0].Height=s[0].BoundWidth=s[0].BoundHeight=32;
    s[0].Matrix[0]=s[0].Matrix[3]=256;
    s[1]=s[0]; s[1].OAM=1; s[1].X=100;
    s[2]=s[0]; s[2].OAM=2; s[2].X=s[2].Y=0;
    s[2].Width=s[2].Height=s[2].BoundWidth=s[2].BoundHeight=64;
    s[2].Palette=224; s[2].Tile=6144;
    auto groups=PlanOpaqueOBJAssemblies(s,3);
    CHECK_EQ(groups.size(),1u); CHECK_EQ(groups[0].Root,2);
    CHECK_EQ(groups[0].Members.size(),2u);
    s[1].X=20; // Moving portrait behind this intervening sprite changes order.
    CHECK(PlanOpaqueOBJAssemblies(s,3).empty());
    s[1].X=100; s[1].Mode=1; // Separate shadow outside the moved rectangle.
    CHECK_EQ(PlanOpaqueOBJAssemblies(s,3).size(),1u);
    s[2].Mode=1; // A blend operator may never become the assembly root.
    CHECK(PlanOpaqueOBJAssemblies(s,3).empty());
}

POLICY_TEST(OpaqueAssemblyTracksTransformsWrapAndWholeCoincidentStack)
{
    ConnectedOBJSourceInput s[3];
    s[0].OAM=3; s[0].MatrixIndex=0; s[0].Priority=1;
    s[0].X=0; s[0].Y=-3;
    s[0].Width=s[0].Height=s[0].BoundWidth=s[0].BoundHeight=64;
    s[0].Matrix[0]=s[0].Matrix[3]=192;
    s[0].Matrix[1]=128; s[0].Matrix[2]=-128;
    s[1]=s[0]; s[1].OAM=1; s[1].Palette=80; s[1].Y+=256;
    s[2]=s[0]; s[2].OAM=0; s[2].X+=16; s[2].Y+=16;
    s[2].Width=s[2].Height=s[2].BoundWidth=s[2].BoundHeight=32;
    auto groups=PlanOpaqueOBJAssemblies(s,3);
    CHECK_EQ(groups.size(),1u); CHECK_EQ(groups[0].Members.size(),3u);
    CHECK_EQ(groups[0].Members[0],2); CHECK_EQ(groups[0].Members[2],0);
    s[2].X++; // Similar placement is insufficient.
    CHECK_EQ(PlanOpaqueOBJAssemblies(s,3)[0].Members.size(),2u);
    s[2].X--; s[2].Mosaic=true;
    CHECK_EQ(PlanOpaqueOBJAssemblies(s,3)[0].Members.size(),2u);
    s[1].Matrix[0]++; s[2].MatrixIndex=-1;
    CHECK(PlanOpaqueOBJAssemblies(s,3).empty());
}

POLICY_TEST(OpaqueAssemblyRetainsForegroundSubgroupWithoutCrossingBlocker)
{
    ConnectedOBJSourceInput s[4];
    s[0].OAM=0; s[0].MatrixIndex=0; s[0].Priority=1;
    s[0].X=s[0].Y=16;
    s[0].Width=s[0].Height=s[0].BoundWidth=s[0].BoundHeight=32;
    s[0].Matrix[0]=s[0].Matrix[3]=256;
    s[1]=s[0]; s[1].OAM=1; s[1].X=s[1].Y=0;
    s[1].Width=s[1].Height=s[1].BoundWidth=s[1].BoundHeight=64;
    s[2]=s[0]; s[2].OAM=2; s[2].X=60;
    s[3]=s[1]; s[3].OAM=3;
    // Highlight bounds cross an intervening card, so moving all three
    // sources to the rear backing is unsafe. Portrait + highlight is safe.
    auto groups=PlanOpaqueOBJAssemblies(s,4);
    CHECK_EQ(groups.size(),1u); CHECK_EQ(groups[0].Root,1);
    CHECK_EQ(groups[0].Members.size(),2u);
    CHECK_EQ(groups[0].Members[0],0); CHECK_EQ(groups[0].Members[1],1);
    s[2].X=100;
    groups=PlanOpaqueOBJAssemblies(s,4);
    CHECK_EQ(groups.size(),1u); CHECK_EQ(groups[0].Root,3);
    CHECK_EQ(groups[0].Members.size(),3u);
    // A blocker between portrait and highlight must still stop their merge.
    s[2].X=20; s[2].OAM=1; s[1].OAM=2; s[3].Mode=1;
    CHECK(PlanOpaqueOBJAssemblies(s,4).empty());
    // Non-normal operators never join a group, even with the refined planner.
    s[2].X=100; s[1].Mode=1; s[3].Mode=1;
    CHECK(PlanOpaqueOBJAssemblies(s,4).empty());
}

POLICY_TEST(ConnectedOBJSourceFFTBodyFeetWrapAndTranslation)
{
    ConnectedOBJSourceInput body;
    body.OAM = 50; body.MatrixIndex = 5;
    body.X = 96; body.Y = 104; body.Width = 32; body.Height = 32;
    body.BoundWidth = body.BoundHeight = 64;
    body.Matrix[0] = body.Matrix[3] = 128;
    body.Tile = 3200; body.Stride = 128; body.Palette = 192; body.Priority = 1;
    auto feet = body;
    feet.OAM = 51; feet.Y = -88; feet.Height = 8; feet.BoundHeight = 16; feet.Tile = 3712;
    ConnectedOBJSourcePair pair;
    CHECK(PlanConnectedOBJSourcePair(body, feet, pair));
    CHECK_EQ(pair.Width, 32); CHECK_EQ(pair.Height, 40);
    CHECK_EQ(pair.X[1], 0); CHECK_EQ(pair.Y[1], 32);
    body.X += 17; feet.X += 17; body.Y += 20; feet.Y += 20;
    CHECK(PlanConnectedOBJSourcePair(body, feet, pair));
    CHECK_EQ(pair.Y[1], 32);
    feet.Y++;
    CHECK(!PlanConnectedOBJSourcePair(body, feet, pair));
}

POLICY_TEST(ConnectedOBJSourceRejectsMaterialLayoutAndPaletteMismatches)
{
    ConnectedOBJSourceInput a;
    a.OAM = 1; a.MatrixIndex = 0; a.Width = a.Height = 16;
    a.BoundWidth = a.BoundHeight = 16;
    a.Matrix[0] = a.Matrix[3] = 256; a.Stride = 64;
    auto b = a; b.OAM = 2; b.Y = 16; b.Tile = 128;
    ConnectedOBJSourcePair pair;
    CHECK(PlanConnectedOBJSourcePair(a, b, pair));
    auto bad = b; bad.Mode = 1;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
    bad = b; bad.Palette = 16;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
    bad = b; bad.Priority = 1;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
    bad = b; bad.Tile += 32;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
    bad = b; bad.Y = 8;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
    bad = b; bad.Mosaic = true;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
    bad = b; bad.OAM = 3;
    CHECK(!PlanConnectedOBJSourcePair(a, bad, pair));
}

POLICY_TEST(ConnectedOBJSourceSupportsHorizontalAndExactRotatedJoins)
{
    ConnectedOBJSourceInput a;
    a.OAM = 4; a.MatrixIndex = 2; a.Width = a.Height = 16;
    a.BoundWidth = a.BoundHeight = 16;
    a.Matrix[0] = a.Matrix[3] = 256; a.Stride = 128;
    auto b = a; b.OAM = 5; b.X = 16; b.Tile = 64;
    ConnectedOBJSourcePair pair;
    CHECK(PlanConnectedOBJSourcePair(a, b, pair));
    CHECK_EQ(pair.Width, 32); CHECK_EQ(pair.Height, 16);
    CHECK_EQ(pair.X[1], 16);
    a.Matrix[0] = a.Matrix[3] = 0;
    a.Matrix[1] = 256; a.Matrix[2] = -256;
    b = a; b.OAM = 5; b.X = -16; b.Tile = 256;
    CHECK(PlanConnectedOBJSourcePair(a, b, pair));
    CHECK_EQ(pair.Y[1], 16);
    a.Width = a.Height = 64; a.BoundWidth = a.BoundHeight = 64;
    a.Matrix[0] = a.Matrix[3] = 256; a.Matrix[1] = a.Matrix[2] = 0;
    b = a; b.OAM = 5; b.Y = 64; b.Tile = 1024;
    CHECK(!PlanConnectedOBJSourcePair(a, b, pair));
    CHECK(PlanConnectedOBJSourcePair(a, b, pair, 128));
}

namespace
{

WholeSceneOBJBandInput Ordinary(int oamIndex, u32 priority = 1)
{
    WholeSceneOBJBandInput input;
    input.RenderedIndex = oamIndex;
    input.OAMIndex = oamIndex;
    input.Priority = priority;
    return input;
}

WholeSceneOBJBandInput Affine(int oamIndex, int groupKey,
                              u32 priority = 1, bool groupReady = true)
{
    auto input = Ordinary(oamIndex, priority);
    input.IsAffine = true;
    input.AffineGroupKey = groupKey;
    input.AffineGroupReady = groupReady;
    return input;
}

WholeSceneOBJBandInput WithBounds(WholeSceneOBJBandInput input,
                                  int x, int y,
                                  int width = 8, int height = 8)
{
    input.PositionX = x;
    input.PositionY = y;
    input.BoundWidth = width;
    input.BoundHeight = height;
    return input;
}

POLICY_TEST(OBJBandClassifierSplitsOrdinaryEntriesAroundAffineOrdering)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(10),
        Affine(20, 3),
        Ordinary(30),
    });

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.AffineCount, 1);
    CHECK_EQ(result.Summary.OrdinaryCount, 2);
    CHECK_EQ(result.Summary.BandCount, 2);
    CHECK_EQ(result.Members[0].BandIndex, 0);
    CHECK_EQ(result.Members[1].BandIndex, 1);
    CHECK_EQ(result.Members[0].AffineBehindOAMMask[0], 1ull << 20);
    CHECK_EQ(result.Members[1].AffineInFrontOAMMask[0], 1ull << 20);
}

POLICY_TEST(OBJBandClassifierCombinesEntriesWithIdenticalAffineRelation)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(5, 0),
        Ordinary(60, 0),
        Affine(20, 4, 1),
    });

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.BandCount, 1);
    CHECK_EQ(result.Bands[0].MemberCount, 2);
    CHECK_EQ(result.Bands[0].MemberOAMMask[0], (1ull << 5) | (1ull << 60));
}

POLICY_TEST(OBJBandClassifierUsesDSOAMOrderAtEqualPriority)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(7, 2),
        Affine(64, 1, 2),
        Ordinary(90, 2),
    });

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.BandCount, 2);
    CHECK_EQ(result.Bands[0].MinTotalPriority, 2 * 128 + 7);
    CHECK_EQ(result.Bands[1].MinTotalPriority, 2 * 128 + 90);
    CHECK_EQ(result.Members[1].AffineInFrontOAMMask[1], 1ull << 0);
}

POLICY_TEST(OBJBandClassifierReindexesMembersAfterPrioritySorting)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(5, 2),
        Affine(20, 6, 1),
        Ordinary(90, 0),
    });

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.BandCount, 2);
    CHECK_EQ(result.Bands[0].MinTotalPriority, 90);
    CHECK_EQ(result.Bands[1].MinTotalPriority, 2 * 128 + 5);
    CHECK_EQ(result.Members[0].BandIndex, 1);
    CHECK_EQ(result.Members[1].BandIndex, 0);
}

POLICY_TEST(OBJBandClassifierRejectsSpecialOrdinaryRoles)
{
    auto semitransparent = Ordinary(10);
    semitransparent.OBJMode = 1;
    auto mosaic = Ordinary(11);
    mosaic.Mosaic = true;

    const auto result = ClassifyWholeSceneOBJBands({
        semitransparent,
        mosaic,
        Affine(20, 2),
    });

    CHECK(!result.Summary.RecipeReady);
    CHECK((result.Summary.RejectionMask & OBJBandRejectNonNormalMode) != 0);
    CHECK((result.Summary.RejectionMask & OBJBandRejectMosaic) != 0);
    CHECK_EQ(result.Summary.RejectedOrdinaryCount, 2);
    CHECK_EQ(result.Summary.BandCount, 0);
}

POLICY_TEST(OBJBandClassifierOrdersAnOrdinaryEntryInsideSharedRotscaleRoles)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Affine(10, 7, 1, false),
        Ordinary(20),
        Affine(30, 7, 1, false),
    });

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.SplitAffineGroupMemberCount, 0);
    CHECK_EQ(result.Members[0].BandIndex, 0);
    u32 bgPriority[4] {};
    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        result, 0, bgPriority);
    CHECK(plan.Ready);
    CHECK_EQ(plan.BackToFront.size(), 3);
    CHECK_EQ(plan.BackToFront[0].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[1].Kind,
             WholeSceneOBJOperandKind::OrdinaryBand);
    CHECK_EQ(plan.BackToFront[2].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
}

POLICY_TEST(OBJBandClassifierAdmitsMultipartSharedRotscaleRoles)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(5),
        Affine(20, 8, 1, false),
        Affine(30, 8, 1, false),
    });

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.BandCount, 1);
}

POLICY_TEST(OBJBandClassifierAppliesBoundedBandLimit)
{
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(5),
        Affine(10, 1),
        Ordinary(15),
        Affine(20, 2),
        Ordinary(25),
    }, 2);

    CHECK(!result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.BandCount, 3);
    CHECK((result.Summary.RejectionMask & OBJBandRejectTooManyBands) != 0);
}

POLICY_TEST(OBJBandClassifierBuildsBandsAroundAffineBGWithoutAffineOBJ)
{
    WholeSceneOBJBandContext context;
    context.AffineBGMask = 1u << 2;
    context.BGPriority[2] = 2;
    context.OBJWindowEnabled = false;

    auto inactiveWindow = Ordinary(40, 0);
    inactiveWindow.OBJMode = 2;
    const auto result = ClassifyWholeSceneOBJBands({
        Ordinary(10, 1),
        Ordinary(20, 3),
        inactiveWindow,
    }, WholeSceneOBJBandLimit, context);

    CHECK(result.Summary.RecipeReady);
    CHECK_EQ(result.Summary.AffineCount, 0);
    CHECK_EQ(result.Summary.AffineBGMask, 1u << 2);
    CHECK_EQ(result.Summary.BandCount, 2);
    CHECK_EQ(result.Summary.SupportedOrdinaryCount, 2);
    CHECK_EQ(result.Summary.RejectedOrdinaryCount, 1);
    CHECK((result.Summary.RejectionMask &
           OBJBandRejectNonNormalMode) != 0);
    CHECK_EQ(result.Summary.BlockingRejectionMask, OBJBandRejectNone);
    CHECK_EQ(result.Bands[0].AffineBGBehindMask, 1u << 2);
    CHECK_EQ(result.Bands[1].AffineBGInFrontMask, 1u << 2);
}

POLICY_TEST(OBJBandForegroundRecipeRequiresEveryMemberAboveEveryBG)
{
    WholeSceneOBJBandContext context;
    context.AffineBGMask = 1u << 2;
    context.BGPriority[0] = 1;
    context.BGPriority[2] = 2;

    const auto foreground = ClassifyWholeSceneOBJBands({
        Ordinary(10, 0),
        Ordinary(20, 1),
    }, WholeSceneOBJBandLimit, context);
    CHECK(IsWholeSceneOBJBandSingleForegroundRecipe(
        foreground, (1u << 0) | (1u << 2), context.BGPriority));

    const auto belowOrdinaryBG = ClassifyWholeSceneOBJBands({
        Ordinary(10, 1),
    }, WholeSceneOBJBandLimit, context);
    u32 stricterPriority[4] {0, 0, 2, 0};
    CHECK(!IsWholeSceneOBJBandSingleForegroundRecipe(
        belowOrdinaryBG, (1u << 0) | (1u << 2), stricterPriority));

    const auto splitAroundAffineBG = ClassifyWholeSceneOBJBands({
        Ordinary(10, 1),
        Ordinary(20, 3),
    }, WholeSceneOBJBandLimit, context);
    CHECK(!IsWholeSceneOBJBandSingleForegroundRecipe(
        splitAroundAffineBG, 1u << 2, context.BGPriority));
}

POLICY_TEST(OBJBandPreservedBehindRecipeKeepsSpecialRolesOrdered)
{
    WholeSceneOBJBandContext context;
    context.AffineBGMask = 1u << 2;
    context.BGPriority[2] = 2;

    auto behind = Ordinary(90, 2);
    behind.OBJMode = 1;
    const auto admitted = ClassifyWholeSceneOBJBands({
        Ordinary(10, 0),
        Ordinary(20, 1),
        behind,
    }, WholeSceneOBJBandLimit, context);

    CHECK(admitted.Summary.RecipeReady);
    CHECK_EQ(admitted.Summary.BandCount, 1);
    CHECK_EQ(admitted.Summary.RejectedOrdinaryCount, 1);
    CHECK_EQ(admitted.Bands[0].PreservedBehindOAMMask[1], 1ull << 26);
    CHECK(IsWholeSceneOBJBandSingleForegroundPreservedBehindRecipe(
        admitted, 1u << 2, context.BGPriority));

    auto inFront = Ordinary(5, 0);
    inFront.OBJMode = 3;
    const auto rejected = ClassifyWholeSceneOBJBands({
        inFront,
        Ordinary(10, 1),
    }, WholeSceneOBJBandLimit, context);
    CHECK(rejected.Summary.RecipeReady);
    CHECK(!IsWholeSceneOBJBandSingleForegroundPreservedBehindRecipe(
        rejected, 1u << 2, context.BGPriority));
}

POLICY_TEST(OBJBandTwoForegroundBandsCanSandwichOneSpecialRole)
{
    WholeSceneOBJBandContext context;
    context.AffineBGMask = 1u << 2;
    context.BGPriority[2] = 2;

    auto special = Ordinary(52, 2);
    special.OBJMode = 1;
    const auto admitted = ClassifyWholeSceneOBJBands({
        Ordinary(40, 1),
        special,
        Ordinary(53, 2),
        Affine(59, 1, 2),
    }, WholeSceneOBJBandLimit, context);

    CHECK(admitted.Summary.RecipeReady);
    CHECK_EQ(admitted.Summary.BandCount, 2);
    CHECK(IsWholeSceneOBJBandTwoForegroundBandsAroundPreservedSpecialRecipe(
        admitted, 1u << 2, context.BGPriority));

    special.OBJMode = 3;
    const auto bitmapRejected = ClassifyWholeSceneOBJBands({
        Ordinary(40, 1),
        special,
        Ordinary(53, 2),
        Affine(59, 1, 2),
    }, WholeSceneOBJBandLimit, context);
    CHECK(!IsWholeSceneOBJBandTwoForegroundBandsAroundPreservedSpecialRecipe(
        bitmapRejected, 1u << 2, context.BGPriority));

    auto partitionSpecial = Ordinary(52, 2);
    partitionSpecial.OBJMode = 1;
    const auto affinePartitioned = ClassifyWholeSceneOBJBands({
        Ordinary(40, 1),
        Affine(51, 1, 2),
        partitionSpecial,
        Ordinary(53, 2),
        Affine(59, 2, 2),
    }, WholeSceneOBJBandLimit, context);
    CHECK_EQ(affinePartitioned.Summary.BandCount, 2);
    CHECK_EQ(affinePartitioned.Bands[1].AffineInFrontOAMMask[0],
             1ull << 51);
    CHECK_EQ(affinePartitioned.Bands[1].AffineBehindOAMMask[0],
             1ull << 59);
    CHECK(IsWholeSceneOBJBandTwoForegroundBandsAroundPreservedSpecialRecipe(
        affinePartitioned, 1u << 2, context.BGPriority));
}

POLICY_TEST(OBJOperandPlanExpressesPokemonBandBehindAffinePartition)
{
    const auto classification = ClassifyWholeSceneOBJBands({
        Affine(4, 4, 0),
        Affine(5, 5, 0),
        Affine(6, 6, 0),
        Affine(7, 7, 0),
        Affine(8, 8, 0),
        Affine(9, 9, 0),
        Ordinary(10, 0),
        Ordinary(11, 0),
        Ordinary(12, 0),
    });
    u32 bgPriority[4] {};
    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 0, bgPriority);

    CHECK(plan.Ready);
    CHECK_EQ(plan.BackToFront.size(), 2);
    CHECK_EQ(plan.BackToFront[0].Kind,
             WholeSceneOBJOperandKind::OrdinaryBand);
    CHECK_EQ(plan.BackToFront[0].BandIndex, 0);
    CHECK_EQ(plan.BackToFront[0].PresentationIndex, 0);
    CHECK_EQ(plan.BackToFront[0].OAMMask[0], 0x1C00ull);
    CHECK_EQ(plan.BackToFront[1].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[1].OAMMask[0], 0x3F0ull);
}

POLICY_TEST(OBJOperandPlanAdmitsAffineOnlyOrderedPartition)
{
    const auto classification = ClassifyWholeSceneOBJBands({
        Affine(4, 4, 0),
        Affine(5, 5, 0),
        Affine(6, 6, 0),
    });
    u32 bgPriority[4] {3, 3, 3, 3};
    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 2, bgPriority);

    CHECK(classification.Summary.RecipeReady);
    CHECK_EQ(classification.Summary.BandCount, 0);
    CHECK((classification.Summary.RejectionMask &
           OBJBandRejectNoOrdinaryOBJ) != 0);
    CHECK_EQ(classification.Summary.BlockingRejectionMask,
             OBJBandRejectNone);
    CHECK(plan.Ready);
    CHECK_EQ(plan.BackToFront.size(), 1);
    CHECK_EQ(plan.BackToFront[0].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[0].OAMMask[0], 0x70ull);
}

POLICY_TEST(OBJOperandPlanExpressesMarioInterleavedStack)
{
    auto special = Ordinary(52, 2);
    special.OBJMode = 1;
    const auto classification = ClassifyWholeSceneOBJBands({
        Ordinary(40, 1),
        Affine(51, 1, 2),
        special,
        Ordinary(53, 2),
        Ordinary(54, 2),
        Affine(59, 2, 2),
    });
    u32 bgPriority[4] {3, 3, 3, 3};
    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 2, bgPriority);

    CHECK(plan.Ready);
    CHECK_EQ(plan.BackToFront.size(), 5);
    CHECK_EQ(plan.BackToFront[0].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[0].OAMMask[0], 1ull << 59);
    CHECK_EQ(plan.BackToFront[1].Kind,
             WholeSceneOBJOperandKind::OrdinaryBand);
    CHECK_EQ(plan.BackToFront[1].BandIndex, 1);
    CHECK_EQ(plan.BackToFront[2].Kind,
             WholeSceneOBJOperandKind::SemiTransparentOBJ);
    CHECK_EQ(plan.BackToFront[2].OAMMask[0], 1ull << 52);
    CHECK_EQ(plan.BackToFront[2].PresentationIndex, 2);
    CHECK_EQ(plan.BackToFront[3].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[3].OAMMask[0], 1ull << 51);
    CHECK_EQ(plan.BackToFront[4].Kind,
             WholeSceneOBJOperandKind::OrdinaryBand);
    CHECK_EQ(plan.BackToFront[4].BandIndex, 0);
}

POLICY_TEST(OBJOperandPlanKeepsAffineMode1BetweenNormalAffinePartitions)
{
    auto special = Affine(52, 1, 2, false);
    special.OBJMode = 1;
    const auto classification = ClassifyWholeSceneOBJBands({
        Affine(51, 1, 2, false),
        special,
        Affine(59, 1, 2, false),
    });
    u32 bgPriority[4] {3, 3, 3, 3};
    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 2, bgPriority);

    CHECK(classification.Summary.RecipeReady);
    CHECK(plan.Ready);
    CHECK_EQ(plan.BackToFront.size(), 3);
    CHECK_EQ(plan.BackToFront[0].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[0].OAMMask[0], 1ull << 59);
    CHECK_EQ(plan.BackToFront[1].Kind,
             WholeSceneOBJOperandKind::SemiTransparentOBJ);
    CHECK_EQ(plan.BackToFront[1].OAMMask[0], 1ull << 52);
    CHECK_EQ(plan.BackToFront[1].PresentationIndex, -1);
    CHECK_EQ(plan.BackToFront[2].Kind,
             WholeSceneOBJOperandKind::AffinePartition);
    CHECK_EQ(plan.BackToFront[2].OAMMask[0], 1ull << 51);
}

POLICY_TEST(OBJOperandPlanRetainsSpecialAndAffineRolesBehindEnabledBG)
{
    auto special = Ordinary(20, 2);
    special.OBJMode = 1;
    const auto classification = ClassifyWholeSceneOBJBands({
        Ordinary(10, 0),
        special,
        Affine(30, 3, 2),
    });
    u32 bgPriority[4] {1, 0, 0, 0};
    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 0, bgPriority);

    CHECK(plan.Ready);
    CHECK((plan.RejectionMask &
           OBJOperandPlanRejectBehindEnabledBG) == 0);
}

POLICY_TEST(OBJOperandPlanAdmitsIsolatedSpecialsWithoutAffineOBJWhenEnabled)
{
    auto firstSpecial = WithBounds(Ordinary(52, 2), 64, 32);
    firstSpecial.OBJMode = 1;
    auto secondSpecial = WithBounds(Ordinary(53, 2), 96, 32);
    secondSpecial.OBJMode = 1;
    WholeSceneOBJBandContext context;
    context.AffineBGMask = 1u << 2;
    context.BGPriority[2] = 3;
    const auto classification = ClassifyWholeSceneOBJBands({
        WithBounds(Ordinary(40, 1), 8, 96, 16, 16),
        firstSpecial,
        secondSpecial,
        WithBounds(Ordinary(54, 2), 160, 96, 16, 16),
    }, WholeSceneOBJBandLimit, context);
    u32 bgPriority[4] {3, 3, 3, 3};

    const auto defaultPlan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 2, bgPriority);
    CHECK(!defaultPlan.Ready);
    CHECK((defaultPlan.RejectionMask &
           OBJOperandPlanRejectNoAffineOBJ) != 0);

    WholeSceneOBJOperandPlanOptions options;
    options.AllowNoAffineOBJ = true;
    options.SpecialIsolationMargin = 8;
    const auto isolatedPlan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 2, bgPriority, options);
    CHECK(isolatedPlan.Ready);
    CHECK_EQ(isolatedPlan.BackToFront.size(), 4);
    CHECK_EQ(isolatedPlan.BackToFront[1].Kind,
             WholeSceneOBJOperandKind::SemiTransparentOBJ);
    CHECK_EQ(isolatedPlan.BackToFront[2].Kind,
             WholeSceneOBJOperandKind::SemiTransparentOBJ);
}

POLICY_TEST(OBJOperandPlanRejectsOverlappingSpecialWithoutAffineOBJ)
{
    auto special = WithBounds(Ordinary(52, 2), 32, -8, 16, 16);
    special.OBJMode = 1;
    WholeSceneOBJBandContext context;
    context.AffineBGMask = 1u << 2;
    context.BGPriority[2] = 3;
    const auto classification = ClassifyWholeSceneOBJBands({
        special,
        WithBounds(Ordinary(53, 2), 40, 0, 16, 16),
    }, WholeSceneOBJBandLimit, context);
    u32 bgPriority[4] {3, 3, 3, 3};
    WholeSceneOBJOperandPlanOptions options;
    options.AllowNoAffineOBJ = true;
    options.SpecialIsolationMargin = 8;

    const auto plan = BuildWholeSceneOBJForegroundOperandPlan(
        classification, 1u << 2, bgPriority, options);
    CHECK(!plan.Ready);
    CHECK((plan.RejectionMask &
           OBJOperandPlanRejectUnisolatedSpecial) != 0);
}

}
