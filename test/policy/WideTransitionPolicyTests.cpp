// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideTransitionPolicy.h"
#include "WideTransitionMask.h"

using namespace melonDS;
namespace
{
struct Cover
{
    Polygon Poly {};
    Vertex Vertices[4] {};
    Cover(int left, int right, int top = 0, int bottom = 192)
    {
        Poly.WideOrthographic = true;
        Poly.NumVertices = 4;
        Poly.Attr = 31 << 16;
        for (unsigned i = 0; i < 4; ++i)
        {
            Poly.Vertices[i] = &Vertices[i];
            auto& v = Vertices[i];
            v.NativeSourceValid = true;
            v.NativeSourcePosition[0] = (i == 0 || i == 3 ? left : right) * 16;
            v.NativeSourcePosition[1] = (i < 2 ? top : bottom) * 16;
            v.Position[3] = 4096;
            Poly.FinalZ[i] = 500;
            Poly.FinalW[i] = 4096;
        }
    }
};
}

POLICY_TEST(WideTransitionExtendsOnlyPairedOuterEdges)
{
    Cover left(0, 40), right(210, 256);
    Polygon* polys[] = {&left.Poly, &right.Poly};
    const auto plan = BuildWideTransitionPlan(polys, 2, true);
    CHECK_EQ(plan.size(), 2u);
    CHECK_EQ(WideTransitionX(plan, &left.Poly, &left.Vertices[0], 172, 1368), 0);
    CHECK_EQ(WideTransitionX(plan, &left.Poly, &left.Vertices[1], 332, 1368), 332);
    CHECK_EQ(WideTransitionX(plan, &right.Poly, &right.Vertices[0], 1012, 1368), 1012);
    CHECK_EQ(WideTransitionX(plan, &right.Poly, &right.Vertices[1], 1196, 1368), 1368);
    CHECK_EQ(left.Vertices[0].NativeSourcePosition[0], 0);
    CHECK_EQ(WideTransitionY(plan, &left.Poly, &left.Vertices[0], 192, 1152), 0);
    CHECK_EQ(WideTransitionY(plan, &right.Poly, &right.Vertices[2], 960, 1152), 1152);
    CHECK_EQ(BuildWideTransitionPlan(polys, 2, false).size(), 0u);
    CHECK_EQ(BuildWideTransitionPlan(polys, 1, true).size(), 0u);
}

namespace
{
struct EdgeLine : Cover
{
    EdgeLine(unsigned axis = 1, bool high = false) : Cover(0, 256)
    {
        Poly.NumVertices = 3;
        Poly.Type = 1;
        Poly.Attr = 30 << 16;
        Poly.Translucent = true;
        for (unsigned i = 0; i < 3; ++i)
        {
            Vertices[i].NativeSourcePosition[axis] = high ? (axis ? 3072 : 4096) : 0;
            Vertices[i].NativeSourcePosition[1-axis] = i == 1 ? (axis ? 4096 : 3072) : 0;
        }
    }
};
}

POLICY_TEST(NativeEdgeLinesFollowTheOuterCanvasOnlyWhenBordersAreEnabled)
{
    for (unsigned axis : {0u, 1u}) for (bool high : {false, true})
    {
        EdgeLine line(axis, high);
        Polygon* polys[] = {&line.Poly};
        CHECK_EQ(BuildWideTransitionPlan(polys, 1, true).size(), 0u);
        CHECK_EQ(BuildWideTransitionPlan(polys, 1, false, true).size(), 0u);
        const auto plan = BuildWideTransitionPlan(polys, 1, true, true);
        CHECK_EQ(plan.size(), 1u);
        CHECK_EQ(plan.at(&line.Poly), (axis ? 3u : 12u) | (1u << (axis*2+high)));
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto& v = line.Vertices[i];
            CHECK_EQ(WideTransitionX(plan, &line.Poly, &v, 123, 1536), v.NativeSourcePosition[0] ? 1536 : 0);
            CHECK_EQ(WideTransitionY(plan, &line.Poly, &v, 123, 1152), v.NativeSourcePosition[1] ? 1152 : 0);
            CHECK_EQ(v.NativeSourcePosition[axis], high ? (axis ? 3072 : 4096) : 0);
        }
        CHECK_EQ(line.Poly.Attr, 30u << 16); // Retain alpha/depth state and geometry.
        CHECK_EQ(line.Poly.NumVertices, 3u);
    }
}

POLICY_TEST(NativeEdgeLinesRejectUnprovenMaterialsAndIncompleteOrInteriorLines)
{
    for (unsigned problem = 0; problem < 23; ++problem)
    {
        EdgeLine line;
        switch (problem)
        {
        case 0: line.Poly.WideOrthographic = false; break;
        case 1: line.Poly.NumVertices = 4; break;
        case 2: line.Poly.Type = 0; break;
        case 3: line.Poly.Degenerate = true; break;
        case 4: line.Poly.IsShadow = true; break;
        case 5: line.Poly.IsShadowMask = true; break;
        case 6: line.Poly.TexParam = 6u << 26; break;
        case 7: line.Poly.Attr = 0; break;
        case 8: line.Poly.Attr |= 1 << 15; break;
        case 9: line.Poly.Attr |= 1 << 4; break;
        case 10: line.Vertices[0].Position[3] = 0; break;
        case 11: line.Vertices[1].NativeSourceValid = false; break;
        case 12: line.Vertices[1].Clipped = true; break;
        case 13: line.Vertices[1].Position[3]++; break;
        case 14: line.Vertices[1].Position[2]++; break;
        case 15: line.Poly.FinalZ[1]++; break;
        case 16: line.Poly.FinalW[1]++; break;
        case 17: for (auto& v : line.Vertices) v.FinalColor[0] = 1; break;
        case 18: for (auto& v : line.Vertices) v.NativeSourcePosition[1] = 16; break;
        case 19: line.Vertices[1].NativeSourcePosition[0] = 4095; break;
        case 20: line.Vertices[1].NativeSourcePosition[1] = 16; break;
        case 21: line.Vertices[0].NativeSourcePosition[0] = -16; break;
        case 22: line.Vertices[1].NativeSourcePosition[0] = 0; break;
        }
        Polygon* polys[] = {&line.Poly};
        CHECK_EQ(BuildWideTransitionPlan(polys, 1, true, true).size(), 0u);
    }
}

POLICY_TEST(VerticalTransitionExtendsPairedCoversWithoutMovingInnerEdges)
{
    Cover top(0,256,0,40), bottom(0,256,150,192);
    Polygon* polys[] = {&top.Poly,&bottom.Poly};
    const auto plan = BuildWideTransitionPlan(polys,2,true);
    CHECK_EQ(plan.size(),2u);
    CHECK_EQ(WideTransitionY(plan,&top.Poly,&top.Vertices[0],192,1152),0);
    CHECK_EQ(WideTransitionY(plan,&top.Poly,&top.Vertices[2],352,1152),352);
    CHECK_EQ(WideTransitionY(plan,&bottom.Poly,&bottom.Vertices[0],792,1152),792);
    CHECK_EQ(WideTransitionY(plan,&bottom.Poly,&bottom.Vertices[2],960,1152),1152);
    CHECK_EQ(WideTransitionX(plan,&top.Poly,&top.Vertices[0],256,1536),0);
    CHECK_EQ(WideTransitionX(plan,&bottom.Poly,&bottom.Vertices[1],1280,1536),1536);
    CHECK_EQ(BuildWideTransitionPlan(polys,1,true).size(),0u);
    bottom.Vertices[0].NativeSourcePosition[1]=30*16;
    bottom.Vertices[1].NativeSourcePosition[1]=30*16;
    CHECK_EQ(BuildWideTransitionPlan(polys,2,true).size(),0u);
}

POLICY_TEST(FourPieceTransitionFrameExtendsAllMarginsAndPreservesOpening)
{
    Cover left(0,80),right(180,256),top(80,180,0,60),bottom(80,180,130,192);
    Polygon* polys[]={&left.Poly,&right.Poly,&top.Poly,&bottom.Poly};
    const auto plan=BuildWideTransitionPlan(polys,4,true);
    CHECK_EQ(plan.size(),4u);
    CHECK_EQ(WideTransitionY(plan,&top.Poly,&top.Vertices[0],192,1152),0);
    CHECK_EQ(WideTransitionY(plan,&top.Poly,&top.Vertices[2],432,1152),432);
    CHECK_EQ(WideTransitionY(plan,&bottom.Poly,&bottom.Vertices[2],960,1152),1152);
    CHECK_EQ(WideTransitionX(plan,&top.Poly,&top.Vertices[0],576,1536),576);
    CHECK_EQ(BuildWideTransitionPlan(polys,3,true).size(),2u); // Unpaired gap cover stays native.
    top.Vertices[0].NativeSourcePosition[0]-=16;
    top.Vertices[3].NativeSourcePosition[0]-=16;
    CHECK_EQ(BuildWideTransitionPlan(polys,4,true).size(),2u); // Cannot infer a seam.
}

POLICY_TEST(FourPieceTransitionFrameAlsoAcceptsFullWidthTopAndBottom)
{
    Cover top(0,256,0,30),bottom(0,256,160,192),left(0,60,30,160),right(180,256,30,160);
    Polygon* polys[]={&top.Poly,&bottom.Poly,&left.Poly,&right.Poly};
    const auto plan=BuildWideTransitionPlan(polys,4,true);
    CHECK_EQ(plan.size(),4u);
    CHECK_EQ(WideTransitionX(plan,&left.Poly,&left.Vertices[0],256,1536),0);
    CHECK_EQ(WideTransitionX(plan,&right.Poly,&right.Vertices[1],1280,1536),1536);
    CHECK_EQ(WideTransitionY(plan,&left.Poly,&left.Vertices[0],312,1152),312);
    CHECK_EQ(WideTransitionX(plan,&left.Poly,&left.Vertices[1],496,1536),496);
}

POLICY_TEST(WideTransitionAcceptsFullCoverAndAnyUniformColour)
{
    Cover full(0, 256);
    for (auto& v : full.Vertices) v.FinalColor[0] = 63;
    Polygon* polys[] = {&full.Poly};
    CHECK_EQ(BuildWideTransitionPlan(polys, 1, true).size(), 1u);
    full.Vertices[2].FinalColor[0] = 62;
    CHECK_EQ(BuildWideTransitionPlan(polys, 1, true).size(), 0u);
}

POLICY_TEST(WideTransitionRejectsUnprovenMaterialsAndGeometry)
{
    for (unsigned problem = 0; problem < 12; ++problem)
    {
        Cover full(0, 256);
        switch (problem)
        {
        case 0: full.Poly.TexParam = 6u << 26; break;
        case 1: full.Poly.Attr = 16 << 16; break;
        case 2: full.Poly.WideOrthographic = false; break;
        case 3: full.Vertices[0].Clipped = true; break;
        case 4: full.Vertices[0].NativeSourceValid = false; break;
        case 5: full.Vertices[0].Position[2] = 1; break;
        case 6: full.Poly.FinalZ[0] = 501; break;
        case 7: full.Vertices[0].NativeSourcePosition[1] = 16; break;
        case 8: full.Vertices[0].NativeSourcePosition[0] = -16; break;
        case 9: full.Poly.IsShadow = true; break;
        case 10: full.Poly.Attr |= 1 << 15; break;
        case 11: std::swap(full.Poly.Vertices[1], full.Poly.Vertices[2]); break;
        }
        Polygon* polys[] = {&full.Poly};
        CHECK_EQ(BuildWideTransitionPlan(polys, 1, true).size(), 0u);
    }
}

POLICY_TEST(WideTransitionRejectsMismatchedOverlappingAndAmbiguousPairs)
{
    Cover left(0, 40), right(210, 256), duplicate(0, 20), overlap(20, 256);
    Polygon* polys[] = {&left.Poly, &right.Poly, &duplicate.Poly};
    CHECK_EQ(BuildWideTransitionPlan(polys, 3, true).size(), 0u);
    right.Poly.Attr |= 1 << 24;
    CHECK_EQ(BuildWideTransitionPlan(polys, 2, true).size(), 0u);
    polys[1] = &overlap.Poly;
    CHECK_EQ(BuildWideTransitionPlan(polys, 2, true).size(), 0u);
}

namespace
{
void MakeMask(Cover& cover)
{
    cover.Poly.Translucent = true;
    cover.Poly.TexParam = 6u << 26; // 8x8 cutout, no game-specific format/address
    for (auto& v : cover.Vertices)
    {
        v.TexCoords[0] = v.NativeSourcePosition[0] == cover.Vertices[0].NativeSourcePosition[0] ? 0 : 128;
        v.TexCoords[1] = v.NativeSourcePosition[1] == cover.Vertices[0].NativeSourcePosition[1] ? 0 : 128;
    }
}
bool DecodeMask(const Polygon&, std::vector<u32>& pixels)
{
    pixels.assign(64,0xFF000000);
    for (unsigned y = 2; y < 6; ++y)
        for (unsigned x = 2; x < 6; ++x) pixels[y*8+x] = 0;
    return true;
}
}

POLICY_TEST(CutoutMaskContinuesWhenTopThenBottomFillersDisappear)
{
    Cover left(0,48), right(208,256), bottom(48,208,159,192), mask(48,208,-1,159);
    MakeMask(mask);
    Polygon* polys[] = {&left.Poly,&right.Poly,&bottom.Poly,&mask.Poly};
    auto scene = BuildWideTransitionMaskScene(polys,4,true,384,288,DecodeMask);
    CHECK_EQ(scene.Polygons.size(),5u);
    CHECK_EQ(scene.Masks.size(),2u);
    CHECK_EQ(scene.Polygons[3] == &mask.Poly,true);
    CHECK_EQ(scene.Masks.at(&mask.Poly).Margin,false);
    CHECK_EQ(scene.Masks.at(scene.Polygons[4]).Margin,true);
    CHECK_EQ(mask.Vertices[0].NativeSourcePosition[1],-16); // source untouched
    CHECK_EQ(scene.Copies[0]->Vertices[0].NativeSourcePosition[0],-64*16);

    Cover tall(48,208,-24,196); MakeMask(tall);
    Polygon* withoutCaps[] = {&left.Poly,&right.Poly,&tall.Poly};
    CHECK_EQ(BuildWideTransitionMaskScene(withoutCaps,3,true,384,288,DecodeMask).Masks.size(),2u);
    Cover full(-2,258,-40,220); MakeMask(full);
    Polygon* alone[] = {&full.Poly};
    CHECK_EQ(BuildWideTransitionMaskScene(alone,1,true,384,288,DecodeMask).Masks.size(),2u);
    CHECK_EQ(BuildWideTransitionMaskScene(alone,1,false,384,288,DecodeMask).Masks.size(),0u);
}

POLICY_TEST(CutoutMaskRequiresCompleteNativeCoverageAndMatchingFillColour)
{
    Cover left(0,48), right(208,256), bottom(48,208,159,192), mask(48,208,1,159);
    MakeMask(mask);
    Polygon* polys[] = {&left.Poly,&right.Poly,&bottom.Poly,&mask.Poly};
    CHECK_EQ(BuildWideTransitionMaskScene(polys,4,true,384,288,DecodeMask).Masks.size(),0u); // missing top filler
    mask.Vertices[0].NativeSourcePosition[1] = mask.Vertices[1].NativeSourcePosition[1] = 0;
    CHECK_EQ(BuildWideTransitionMaskScene(polys,4,true,384,288,DecodeMask).Masks.size(),2u);
    for (auto& v : bottom.Vertices) v.FinalColor[0] = 2;
    CHECK_EQ(BuildWideTransitionMaskScene(polys,4,true,384,288,DecodeMask).Masks.size(),0u);
}

POLICY_TEST(CutoutMaskRejectsArtworkOpenCornersSubrectanglesAndAmbiguousLayers)
{
    Cover mask(0,256); MakeMask(mask);
    Polygon* polys[] = {&mask.Poly};
    auto artwork = [](const Polygon& p, std::vector<u32>& pixels) { DecodeMask(p,pixels); pixels[1] |= 1; return true; };
    auto openCorner = [](const Polygon& p, std::vector<u32>& pixels) { DecodeMask(p,pixels); pixels[0] = 0; return true; };
    CHECK_EQ(BuildWideTransitionMaskScene(polys,1,true,384,288,artwork).Masks.size(),0u);
    CHECK_EQ(BuildWideTransitionMaskScene(polys,1,true,384,288,openCorner).Masks.size(),0u);
    mask.Vertices[1].TexCoords[0] = 64;
    CHECK_EQ(BuildWideTransitionMaskScene(polys,1,true,384,288,DecodeMask).Masks.size(),0u);
    mask.Vertices[1].TexCoords[0] = 128;
    Cover other(0,256); MakeMask(other);
    Polygon* ambiguous[] = {&mask.Poly,&other.Poly};
    CHECK_EQ(BuildWideTransitionMaskScene(ambiguous,2,true,384,288,DecodeMask).Masks.size(),0u);
}

POLICY_TEST(CutoutMaskRecognizesMirroredQuarterTextureAndClippedSource)
{
    Cover mask(-2,258,-40,220); MakeMask(mask);
    mask.Poly.TexParam |= 15u << 16;
    mask.Poly.UnclippedSource.Valid = true;
    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            mask.Poly.UnclippedSource.Position[i][axis] = mask.Vertices[i].NativeSourcePosition[axis];
            mask.Poly.UnclippedSource.UV[i][axis] = mask.Vertices[i].TexCoords[axis]*2;
        }
        mask.Vertices[i].Clipped = true;
        mask.Vertices[i].TexCoords[0] = 48; // clipping must not lose source mapping
    }
    auto quarter = [](const Polygon&,std::vector<u32>& pixels) {
        pixels.assign(64,0xFF000000);
        for (unsigned y = 4; y < 8; ++y)
            for (unsigned x = 4; x < 8; ++x) pixels[y*8+x] = 0;
        return true;
    };
    Polygon* polys[] = {&mask.Poly};
    const auto scene = BuildWideTransitionMaskScene(polys,1,true,384,288,quarter);
    CHECK_EQ(scene.Masks.size(),2u);
    CHECK_EQ(scene.Masks.at(&mask.Poly).UVScale[0],2u);
    CHECK_EQ(scene.Masks.at(&mask.Poly).UVScale[1],2u);
    CHECK(mask.Vertices[0].Clipped);
    mask.Poly.TexParam &= ~(3u << 18); // repeating the quarter without reflection is not a closed mask
    CHECK_EQ(BuildWideTransitionMaskScene(polys,1,true,384,288,quarter).Masks.size(),0u);
}
