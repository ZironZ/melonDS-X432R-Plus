// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideTransitionPolicy.h"

using namespace melonDS;
namespace
{
struct Cover
{
    Polygon Poly {};
    Vertex Vertices[4] {};
    Cover(int left, int right)
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
            v.NativeSourcePosition[1] = i < 2 ? 0 : 192 * 16;
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
    CHECK_EQ(BuildWideTransitionPlan(polys, 2, false).size(), 0u);
    CHECK_EQ(BuildWideTransitionPlan(polys, 1, true).size(), 0u);
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
