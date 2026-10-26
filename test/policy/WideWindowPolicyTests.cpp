// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideWindowPolicy.h"
#include "WideCoverPolicy.h"
#include <array>

using namespace melonDS;
namespace
{
struct WindowRow
{
    u32 WinRegs = 0xFFFFFFFF, WinMask = 0, OBJWindowEnabled = 0;
    s32 WinPos[4] = {256, 256, 256, 256};
    u32 BGPrio[4] = {}, EnableOBJ = 1, Enable3D = 1;
    u32 BlendCnt = 0, BlendCoef[4] = {}, BackColor = 0;
};
struct NarrowBorderSettings
{
    int Width = WideMelon::ViewWidth, Height = WideMelon::ViewHeight;
    bool Expand = WideMelon::ExpandNarrowBorders, Extend = WideMelon::ExtendWindows;
    NarrowBorderSettings()
    {
        WideMelon::ViewWidth = 342;
        WideMelon::ViewHeight = 192;
        WideMelon::ExpandNarrowBorders = WideMelon::ExtendWindows = true;
    }
    ~NarrowBorderSettings()
    {
        WideMelon::ViewWidth = Width;
        WideMelon::ViewHeight = Height;
        WideMelon::ExpandNarrowBorders = Expand;
        WideMelon::ExtendWindows = Extend;
    }
};
}

POLICY_TEST(WideNarrowBorderOptInUsesInteriorVisibilityAndEffects)
{
    NarrowBorderSettings settings;
    WindowRow row;
    row.WinRegs = 0x3FFFFF00;
    row.WinMask = 2;
    row.WinPos[0] = 1; row.WinPos[1] = 255;
    row.BlendCnt = (2 << 6) | 1;
    row.BlendCoef[2] = 16;
    for (bool right : {false, true})
    {
        const u32 policy = MakeWideWindowEdgePolicy(row, 0, right, false);
        CHECK(policy & WideNarrowBorder);
        CHECK_EQ(policy & 3, WideEdgeVisible);
        CHECK_EQ((policy >> 2) & 3, 2u);
        CHECK_EQ((policy >> 4) & 31, 16u);
    }
    // The actual DS window and interior-pixel policy remain unchanged.
    CHECK_EQ(WideWindowPermissions(row, 0, false), 0u);
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 1, false) & WideNarrowBorder, 0u);
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false, true) & WideNarrowBorder, 0u);
    WideMelon::ExpandNarrowBorders = false;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 1u);
    WideMelon::ExpandNarrowBorders = true;
    WideMelon::ExtendWindows = false;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false), 0u);
    WideMelon::ExtendWindows = true;
    WideMelon::ViewWidth = 256;
    CHECK(!CanExpandWideNarrowBorder(row, false));
    WideMelon::ViewWidth = 342;
    WideMelon::ViewHeight = 342;
    CHECK(CanExpandWideNarrowBorder(row, false));
}

POLICY_TEST(RectangularNarrowBorderCombinesBothAxesWithoutChangingEdgeEffects)
{
    NarrowBorderSettings settings;
    WideMelon::ViewWidth = 384; WideMelon::ViewHeight = 288;
    std::array<WindowRow,192> rows;
    for (int y = 0; y < 192; ++y)
    {
        auto& row=rows[y];row.WinRegs=0x3FFFFF00;
        if(y>0&&y<191){row.WinMask=2;row.WinPos[0]=1;row.WinPos[1]=255;}
    }
    CHECK(CanExpandVerticalNarrowBorder(rows.data()));
    CHECK_EQ(WideMelon::CaptureOverlap(),2); CHECK_EQ(WideMelon::VerticalCaptureOverlap(),2);
    for (int y : {0,96,191}) for(int x : {0,128,255})
    {
        rows[y].BlendCnt=(3<<6)|1; rows[y].BlendCoef[2]=12;
        const u32 policy=MakeRectangularWindowPolicy(rows.data(),0,x,y,true);
        CHECK_EQ(policy&3,WideEdgeVisible);
        CHECK_EQ((policy>>2)&3,3u); CHECK_EQ((policy>>4)&31,12u);
        const bool border=x==0||x==255||y==0||y==191;
        CHECK_EQ(bool(policy&WideNarrowBorder),border);
        CHECK_EQ(MakeRectangularWindowPolicy(rows.data(),0,x,y,true,true)&WideNarrowBorder,0u);
    }
    CHECK_EQ(rows[0].WinMask,0u); CHECK_EQ(rows[96].WinPos[0],1);
    WideMelon::ExpandNarrowBorders=false;
    CHECK_EQ(MakeRectangularWindowPolicy(rows.data(),0,0,0,false)&3,1u);
}

POLICY_TEST(WideNarrowBorderFillingRequiresVisibleSourceAndDestination)
{
    const u32 expanded = WideEdgeVisible | WideNarrowBorder;
    CHECK(ShouldFillWideNarrowBorder(expanded, 0, false));
    CHECK(ShouldFillWideNarrowBorder(WideEdgeVisible, expanded, true));
    CHECK(ShouldFillWideNarrowBorder(expanded, WideEdgeVisible, true));
    CHECK(!ShouldFillWideNarrowBorder(1, expanded, true)); // Consuming mask still clips this side.
    CHECK(!ShouldFillWideNarrowBorder(0, expanded, true));
    CHECK(!ShouldFillWideNarrowBorder(expanded, 1, true));
    CHECK(!ShouldFillWideNarrowBorder(expanded, 0, true));
    CHECK(!ShouldFillWideNarrowBorder(WideEdgeVisible, expanded, false));
    CHECK(!ShouldFillWideNarrowBorder(WideEdgeVisible, WideEdgeVisible, true));
}

POLICY_TEST(WideFeedbackRestartRequiresCompleteDarkening)
{
    const u32 black = WideEdgeVisible | (3 << 2) | (16 << 4);
    CHECK(IsWidePolicyFullyDark(black));
    CHECK(IsWidePolicyFullyDark(black & ~2u)); // The transparent-3D backdrop path too.
    CHECK(IsWidePolicyFullyDark(black | (0xFFFFu << 9)));
    CHECK(!IsWidePolicyFullyDark(black & ~1u));
    CHECK(!IsWidePolicyFullyDark(black | WideBlend3DBackdrop));
    for (u32 coefficient = 0; coefficient < 16; ++coefficient)
        CHECK(!IsWidePolicyFullyDark(WideEdgeVisible | (3 << 2) | (coefficient << 4)));
    for (u32 effect = 0; effect < 3; ++effect)
        CHECK(!IsWidePolicyFullyDark(WideEdgeVisible | (effect << 2) | (16 << 4)));
}

POLICY_TEST(RotatedNarrowBorderPreservesEdgeEffectsAndOtherAxisClipping)
{
    NarrowBorderSettings settings;
    WideMelon::ViewWidth = 256;
    WideMelon::ViewHeight = 456;
    std::array<WindowRow, 192> rows;
    for (int y = 0; y < 192; ++y)
    {
        rows[y].WinRegs = 0x3FFFFF00;
        if (y > 0 && y < 191)
        {
            rows[y].WinMask = 2;
            rows[y].WinPos[0] = 32; rows[y].WinPos[1] = 224;
        }
    }
    CHECK(CanExpandVerticalNarrowBorder(rows.data()));
    CHECK_EQ(WideMelon::VerticalCaptureOverlap(), 2);
    CHECK_EQ(WideMelon::CaptureOverlap(), 0);
    for (int side : {0, 1})
    {
        rows[side ? 191 : 0].BlendCnt = (3 << 6) | 1;
        rows[side ? 191 : 0].BlendCoef[2] = 12;
        const u32 policy = MakeVerticalWindowPolicy(rows.data(), 0, 128, side, true);
        CHECK_EQ(policy & 3, WideEdgeVisible);
        CHECK(policy & WideNarrowBorder);
        CHECK_EQ((policy >> 2) & 3, 3u);
        CHECK_EQ((policy >> 4) & 31, 12u);
        CHECK_EQ(MakeVerticalWindowPolicy(rows.data(), 0, 0, side, true) & 3, 1u);
        CHECK_EQ(MakeVerticalWindowPolicy(rows.data(), 0, 255, side, true) & WideNarrowBorder, 0u);
        CHECK_EQ(MakeVerticalWindowPolicy(rows.data(), 0, 128, side, false) & 3, 1u);
        CHECK_EQ(MakeVerticalWindowPolicy(rows.data(), 0, 128, side, true, true) & WideNarrowBorder, 0u);
        CHECK_EQ(rows[side ? 191 : 0].WinMask, 0u);
    }
    const auto valid = rows;
    for (int y : {0, 1, 96, 190, 191})
    {
        rows = valid;
        rows[y].WinMask ^= 2;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows = valid;
        rows[y].OBJWindowEnabled = 1;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows[y].OBJWindowEnabled = 3; // Empty OAM window is proven harmless.
        CHECK(CanExpandVerticalNarrowBorder(rows.data()));
        rows[y].WinRegs ^= 1;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
    }
    rows = valid;
    rows[90].WinPos[0] = 31;
    CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
    rows = valid;
    WideMelon::ExpandNarrowBorders = false;
    CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
    CHECK_EQ(WideMelon::VerticalCaptureOverlap(), 0);
    WideMelon::ExpandNarrowBorders = true;
    WideMelon::ExtendWindows = false;
    CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
}

POLICY_TEST(VerticalNarrowBorderAcceptsOnlyOneRowGuardsOnEitherEdge)
{
    NarrowBorderSettings settings;
    WideMelon::ViewWidth = 384; WideMelon::ViewHeight = 288;
    for (int window : {0, 1}) for (unsigned excluded : {1u, 2u, 3u})
    {
        std::array<WindowRow, 192> rows;
        WindowRow inside;
        inside.WinRegs = window ? 0xFF3FFF00 : 0x3FFFFF00;
        inside.WinMask = 2u << (window * 3);
        inside.WinPos[window * 2] = 1;
        inside.WinPos[window * 2 + 1] = 255;
        rows.fill(inside);
        for (int side : {0, 1}) if (excluded & (1u << side))
        {
            auto& edge = rows[side ? 191 : 0];
            edge.WinMask = 0;
            std::fill(std::begin(edge.WinPos), std::end(edge.WinPos), 256);
        }
        CHECK(CanExpandVerticalNarrowBorder(rows.data()));
        const auto valid = rows;
        for (int side : {0, 1})
        {
            const int y = side ? 191 : 0;
            rows[y].BlendCnt = (3 << 6) | 1;
            rows[y].BlendCoef[2] = 7;
            for (int x : {0, 128, 255})
            {
                const u32 policy = MakeRectangularWindowPolicy(rows.data(), 0, x, y, true);
                CHECK_EQ(policy & 3, WideEdgeVisible);
                CHECK_EQ((policy >> 2) & 3, 3u);
                CHECK_EQ((policy >> 4) & 31, 7u);
                CHECK_EQ(bool(policy & WideNarrowBorder), x != 128 || bool(excluded & (1u << side)));
            }
            const u32 vertical = MakeVerticalWindowPolicy(rows.data(), 0, 128, side, true);
            CHECK_EQ(vertical & 3, WideEdgeVisible);
            CHECK_EQ(bool(vertical & WideNarrowBorder), bool(excluded & (1u << side)));
            CHECK_EQ(rows[y].WinMask, valid[y].WinMask); // Native permissions stay unchanged.
        }
        rows = valid;
        rows[(excluded & 1) ? 1 : 190].WinMask = 0; // Two excluded rows are not a guard.
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows = valid; rows[96].WinMask = 0;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows = valid; rows[96].WinPos[window * 2]++;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows = valid; rows[0].WinRegs ^= 1;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows = valid; rows[191].OBJWindowEnabled = 1;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        rows = valid; WideMelon::ExpandNarrowBorders = false;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        WideMelon::ExpandNarrowBorders = true; WideMelon::ExtendWindows = false;
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
        WideMelon::ExtendWindows = true;
        rows.fill(inside); // No excluded edge: do not claim a border repair.
        CHECK(!CanExpandVerticalNarrowBorder(rows.data()));
    }
}

POLICY_TEST(WideNarrowBorderRejectsAmbiguousAndIntentionalRegions)
{
    NarrowBorderSettings settings;
    WindowRow row;
    row.WinRegs = 0x3FFFFF00;
    row.WinMask = 2;
    row.WinPos[0] = 1; row.WinPos[1] = 255;
    CHECK(CanExpandWideNarrowBorder(row, false));
    row.WinPos[0] = 2;
    CHECK(!CanExpandWideNarrowBorder(row, false));
    row.WinPos[0] = 0;
    CHECK(!CanExpandWideNarrowBorder(row, false));
    row.WinPos[0] = 1;
    for (u32 mask : {0u, 3u, 4u, 5u, 18u})
    {
        row.WinMask = mask;
        CHECK(!CanExpandWideNarrowBorder(row, false));
    }
    row.WinMask = 2;
    CHECK(!CanExpandWideNarrowBorder(row, true));
    row.OBJWindowEnabled = 3; // Enabled register, but no window-mode sprites.
    CHECK(CanExpandWideNarrowBorder(row, true));
    row.OBJWindowEnabled = 0;
    row.WinRegs = 0x3FFF0000; // Unknown recorded OBJ coverage.
    CHECK(!CanExpandWideNarrowBorder(row, false));
    row.WinRegs = 0x21FFFF10; // Interior would remove an outside layer.
    CHECK(!CanExpandWideNarrowBorder(row, false));
    row.WinRegs = 0x21FFFF21; // No exclusion to relax.
    CHECK(!CanExpandWideNarrowBorder(row, false));
    row.WinRegs = 0xFF3FFF00;
    row.WinMask = 16;
    row.WinPos[0] = row.WinPos[1] = 256;
    row.WinPos[2] = 1; row.WinPos[3] = 255;
    CHECK(CanExpandWideNarrowBorder(row, false));
    CHECK(MakeWideWindowEdgePolicy(row, 0, false, false) & WideNarrowBorder);
}

POLICY_TEST(WideWindowEmptyOBJProofDoesNotIgnoreRealMasks)
{
    WindowRow row;
    row.WinRegs = 0xFFFF003F;
    row.OBJWindowEnabled = 1;
    CHECK_EQ(WideWindowPermissions(row, 0, true), 0x100u);
    row.OBJWindowEnabled = 3;
    CHECK_EQ(WideWindowPermissions(row, 0, true), 0x3Fu);
    // A rectangular mask remains authoritative even with no OBJ masks.
    row.WinRegs = 0x10FF003F;
    row.WinMask = 2;
    row.WinPos[0] = 0; row.WinPos[1] = 256;
    CHECK_EQ(WideWindowPermissions(row, 0, true), 0x10u);
    row.OBJWindowEnabled = 1;
    row.WinMask = 0;
    CHECK_EQ(WideWindowPermissions(row, 0, true), 0x100u);
}

POLICY_TEST(WideWindowOAMProofIncludesDisabledAndAffineMasks)
{
    u16 oam[512] = {};
    CHECK(WideOBJWindowProof(oam) & 2);
    oam[0] = 1 << 10; // Semi-transparent OBJ is not a window.
    oam[4] = 3 << 10; // Bitmap OBJ is not a window.
    CHECK(WideOBJWindowProof(oam) & 2);
    oam[127 * 4] = (2 << 10) | (1 << 9);
    CHECK(!(WideOBJWindowProof(oam) & 2));
    oam[127 * 4] = (2 << 10) | (1 << 8) | (1 << 9);
    CHECK(!(WideOBJWindowProof(oam) & 2));
    oam[127 * 4] = 0;
    CHECK(WideOBJWindowProof(oam) & 2);
}

POLICY_TEST(WideWindowInteriorSpriteMasksLeaveOnlyProvenEdgesClear)
{
    NarrowBorderSettings settings;
    u16 oam[512] = {};
    oam[0] = (2 << 10) | 5;
    oam[1] = (1 << 14) | 5; // A 16x16 item-box mask at (5,5).
    WindowRow row;
    row.WinRegs = 0x3F3F322F;
    row.OBJWindowEnabled = 1 | WideOBJWindowProof(oam);
    CHECK_EQ(row.OBJWindowEnabled & 15u, 13u);
    CHECK_EQ(WideWindowPermissions(row, 0, true), 0x2Fu);
    CHECK_EQ(WideWindowPermissions(row, 255, true), 0x2Fu);
    CHECK_EQ(WideWindowPermissions(row, 5, true), 0x100u);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, true) & 3, WideEdgeVisible);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, true, true) & 3, WideEdgeVisible);
    // Geometric absence must not override rectangular windows or the toggle.
    row.WinRegs = 0x303F322F;
    row.WinMask = 2; row.WinPos[0] = 0; row.WinPos[1] = 64;
    CHECK_EQ(WideWindowPermissions(row, 0, true), 0x30u);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, true) & 3, 1u);
    WideMelon::ExtendWindows = false;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, true, true), 0u);
}

POLICY_TEST(WideWindowBoundsProofRespectsShapesAffineSizeAndSignedX)
{
    u16 oam[512] = {};
    const int widths[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    for (int shape = 0; shape < 3; ++shape)
        for (int size = 0; size < 4; ++size)
            for (int type = 0; type < 4; ++type)
            {
                const int width = widths[shape][size] * (type == 3 ? 2 : 1);
                oam[0] = (shape << 14) | (2 << 10) | (type << 8) | 240;
                oam[1] = (size << 14) | (255 - width);
                CHECK_EQ(WideOBJWindowProof(oam) & 15u, 12u); // Stops just before x=255.
                ++oam[1];
                CHECK_EQ(WideOBJWindowProof(oam) & 15u, 4u); // Right edge may be masked.
                oam[1] = size << 14;
                CHECK_EQ(WideOBJWindowProof(oam) & 15u, 8u); // Left edge may be masked.
                oam[1] |= 511;
                CHECK_EQ(WideOBJWindowProof(oam) & 15u, 8u); // x=-1, not x=511.
            }
    // Union all masks, including the last OAM slot; no stale proof after a move.
    oam[0] = 2 << 10; oam[1] = 5;
    oam[508] = 2 << 10; oam[509] = 250;
    CHECK_EQ(WideOBJWindowProof(oam) & 15u, 4u);
    oam[1] = 0;
    CHECK_EQ(WideOBJWindowProof(oam) & 15u, 0u);
    oam[0] |= 3 << 14;
    CHECK_EQ(WideOBJWindowProof(oam), 0u);
}

POLICY_TEST(WideWindowInteriorOBJMaskLeavesTopAndBottomMarginsVisible)
{
    NarrowBorderSettings settings;
    WideMelon::ViewWidth = 384; WideMelon::ViewHeight = 288;
    u16 oam[512] = {};
    oam[0] = (2 << 10) | 5;
    oam[1] = (1 << 14) | 5;
    std::array<WindowRow,192> rows;
    for (auto& row : rows)
    {
        row.WinRegs = 0x3F3F322F;
        row.WinMask = 16;
        row.WinPos[2] = 64; row.WinPos[3] = 255;
        row.OBJWindowEnabled = 1 | WideOBJWindowProof(oam);
        row.BlendCnt = (3 << 6) | 1;
        row.BlendCoef[2] = 7;
    }
    CHECK(rows[0].OBJWindowEnabled & WideOBJClearTop);
    CHECK(rows[191].OBJWindowEnabled & WideOBJClearBottom);
    // Reproduce the native x=1..63 unknown-policy strip above the HUD.
    for (int x : {1,16,32,63})
    {
        CHECK_EQ(WideWindowPermissions(rows[0],x,true),0x100u);
        for (int side : {0,1})
        {
            const auto policy = MakeVerticalWindowPolicy(rows.data(),0,x,side,false);
            CHECK_EQ(policy & 3,WideEdgeVisible);
            CHECK_EQ((policy >> 2) & 3,3u);
            CHECK_EQ((policy >> 4) & 31,7u);
            CHECK_EQ(policy & WideNarrowBorder,0u);
            CHECK_EQ(MakeRectangularWindowPolicy(rows.data(),0,x,side ? 191 : 0,false),policy);
        }
        CHECK_EQ(MakeRectangularWindowPolicy(rows.data(),0,x,96,false),0u);
    }
    // Restoring just the old horizontal proofs reproduces the black box.
    for (auto& row : rows) row.OBJWindowEnabled &= 15u;
    CHECK_EQ(MakeVerticalWindowPolicy(rows.data(),0,16,0,false),0u);
    CHECK_EQ(MakeRectangularWindowPolicy(rows.data(),0,16,0,false),0u);
}

POLICY_TEST(WideWindowVerticalOBJProofRespectsBoundsWrapAndAffineSize)
{
    u16 oam[512] = {};
    constexpr int heights[3][4] = {{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    for (int shape=0;shape<3;++shape)
        for (int size=0;size<4;++size)
            for (int type=0;type<4;++type)
                for (int y : {0,1,64,127,128,180,191,192,200,240,255})
                {
                    const int height=heights[shape][size]*(type==3 ? 2 : 1);
                    oam[0]=(shape<<14)|(2<<10)|(type<<8)|y;
                    oam[1]=(size<<14)|32;
                    const u32 proof=WideOBJWindowProof(oam);
                    if (proof & WideOBJClearTop) CHECK(((256-y)&255)>=height);
                    if (proof & WideOBJClearBottom) CHECK(((191-y)&255)>=height);
                    if (y==0) CHECK_EQ(proof & WideOBJClearTop,0u);
                    if (y==191) CHECK_EQ(proof & WideOBJClearBottom,0u);
                    if (y==1 && height<=128) CHECK(proof & WideOBJClearTop);
                    if (y+height>256) CHECK_EQ(proof & WideOBJClearTop,0u);
                }
    // Disabled/unknown masks retain conservative bounds, including slot 127.
    oam[0]=0;
    CHECK_EQ(WideOBJWindowProof(oam),62u);
    oam[508]=(2<<10)|(1<<9);
    oam[509]=32;
    CHECK_EQ(WideOBJWindowProof(oam) & WideOBJClearTop,0u);
    oam[508]=(2<<10)|(3<<8)|180;
    oam[509]=(3<<14)|32;
    CHECK_EQ(WideOBJWindowProof(oam) & (WideOBJClearTop|WideOBJClearBottom),0u);
    oam[508] |= 3<<14;
    CHECK_EQ(WideOBJWindowProof(oam),0u);
}

POLICY_TEST(WideWindowVerticalOBJProofUsesBoundarySnapshotAndKeepsRealMasks)
{
    NarrowBorderSettings settings;
    WideMelon::ViewWidth=384; WideMelon::ViewHeight=288;
    std::array<WindowRow,192> rows;
    for (auto& row : rows)
    {
        row.WinRegs=0x3F3F322F;
        row.OBJWindowEnabled=1;
    }
    rows[0].OBJWindowEnabled |= WideOBJClearTop;
    rows[191].OBJWindowEnabled |= WideOBJClearBottom;
    CHECK_EQ(MakeVerticalWindowPolicy(rows.data(),0,16,0,false)&3,WideEdgeVisible);
    CHECK_EQ(MakeVerticalWindowPolicy(rows.data(),0,16,1,false)&3,WideEdgeVisible);
    // The same boundary proof must preserve a known opaque covering BG,
    // rather than expose the 3D layer underneath it.
    rows[0].BGPrio[0]=2; rows[0].BGPrio[1]=1;
    const auto permissions=WideWindowPermissions(rows[0],16,true,WideOBJClearTop);
    const auto cover=MakeWideWindowPolicy(rows[0],0,16,true,false,1,0x1234,permissions);
    CHECK_EQ(cover & 3,1u);
    CHECK_EQ((cover >> 9) & 0xFFFF,0x1234u);
    rows[0].OBJWindowEnabled=1|WideOBJClearBottom;
    rows[191].OBJWindowEnabled=1|WideOBJClearTop;
    CHECK_EQ(MakeVerticalWindowPolicy(rows.data(),0,16,0,false),0u);
    CHECK_EQ(MakeRectangularWindowPolicy(rows.data(),0,16,191,false),0u);
    // Rectangular windows retain priority over absence proofs and effects.
    auto& top=rows[0];
    top.OBJWindowEnabled |= WideOBJClearTop;
    top.WinRegs=0x303F322F; top.WinMask=2;
    top.WinPos[0]=0; top.WinPos[1]=64;
    CHECK_EQ(MakeVerticalWindowPolicy(rows.data(),0,16,0,false)&3,1u);
    WideMelon::ExtendWindows=false;
    CHECK_EQ(MakeVerticalWindowPolicy(rows.data(),0,100,0,false),0u);
}

POLICY_TEST(WideWindow3DBlendRequiresKnownBackdropOperand)
{
    WindowRow row;
    row.BGPrio[0] = 3;
    row.BGPrio[1] = 0; row.BGPrio[2] = 1; row.BGPrio[3] = 0xFFFFFFFF;
    row.BlendCnt = 0x3B45; // Source-A composition in a dual-screen capture.
    row.BlendCoef[0] = 10; row.BlendCoef[1] = 5;
    CHECK(MakeWideWindowPolicy(row, 0, 0, false) & WideBlend3DBackdrop);
    row.BGPrio[3] = 3; // BG3 can now be directly behind 3D.
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false), 0u);
    row.BGPrio[3] = 0xFFFFFFFF;
    row.BGPrio[0] = 2; // An OBJ may be behind the 3D layer.
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false), 0u);
    row.EnableOBJ = 0;
    CHECK(MakeWideWindowPolicy(row, 0, 0, false) & WideBlend3DBackdrop);
    row.BlendCnt &= ~(1u << 13); // Backdrop is not a blend target.
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false), 0u);
    row.Enable3D = 0;
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false) & WideBlend3DBackdrop, 0u);
    row.Enable3D = 1;
    row.BlendCnt = (1u << 13) | (3u << 6) | 0x21;
    row.BlendCoef[2] = 8;
    const u32 policy = MakeWideWindowPolicy(row, 0, 0, false);
    CHECK(policy & WideBlend3DBackdrop);
    CHECK_EQ((policy >> 26) & 3, 3u);
    CHECK_EQ((policy >> 2) & 3, 0u); // Material alpha takes precedence for present pixels.
}

POLICY_TEST(WideWindowUnwindowedEdgesKeepVisibleLayer)
{
    WindowRow row;
    for (int layer = 0; layer <= 4; ++layer)
        for (bool right : {false, true})
            CHECK_EQ(MakeWideWindowEdgePolicy(row, layer, right, false), WideEdgeVisible);
    row.BGPrio[2] = 0xFFFFFFFF;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 2, false, false) & 3, 1u);
    row.Enable3D = 0;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 1u);
}

POLICY_TEST(WideWindowDisabledExtensionsKeepOnlyUnwindowedRoute)
{
    struct Restore { bool Saved = WideMelon::ExtendWindows; ~Restore() { WideMelon::ExtendWindows = Saved; } } restore;
    WideMelon::ExtendWindows = false;
    WindowRow row;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false), WideEdgeVisible);
    row.BlendCnt = (3 << 6) | 1;
    row.BlendCoef[2] = 8;
    CHECK_EQ((MakeWideWindowEdgePolicy(row, 0, false, false) >> 2) & 3, 3u);
    row.WinRegs = 0x21FFFF00;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false), 0u);
    row.WinRegs = 0xFFFFFFFF;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, true), 0u);
}

POLICY_TEST(VerticalWindowMarginsRespectEveryColumn)
{
    WindowRow row;
    row.WinRegs = 0x21FFFF00;
    row.WinMask = 2;
    row.WinPos[0] = 40; row.WinPos[1] = 200;
    for (int x = 0; x < 256; ++x)
        CHECK_EQ(MakeWideWindowPolicy(row, 0, x, false) & 3u, x >= 40 && x < 200 ? 3u : 1u);
    row.WinMask = 0;
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 128, false) & 3u, 1u);
}

POLICY_TEST(WideWindowExtendsOnlyTheNativeEdgeRegion)
{
    WindowRow row;
    row.WinRegs = 0x21FFFF00; // WIN0: BG0/effects; outside: no layers/effects.
    row.WinMask = 2;
    row.WinPos[0] = 0; row.WinPos[1] = 200;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 3u);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, true, false) & 3, 1u);
    row.WinPos[0] = 40;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 1u);
    row.WinMask = 0; // Outside the vertical window span.
    row.WinPos[0] = 0;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 1u);
}

POLICY_TEST(WideWindowWrappedEdgesRespectHorizontalLatch)
{
    WindowRow row;
    row.WinRegs = 0x21FFFF00;
    row.WinPos[0] = 30; row.WinPos[1] = 220;
    row.WinMask = 5; // Wrapped region on both sides.
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 3u);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, true, false) & 3, 3u);
    row.WinMask = 4; // Left-edge latch not yet active on this scanline.
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false) & 3, 1u);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, true, false) & 3, 3u);
}

POLICY_TEST(WideWindowPriorityAndOBJUncertainty)
{
    WindowRow row;
    row.WinRegs = 0x0021FF00; // WIN0 masks BG0; WIN1 permits it.
    row.WinMask = 2 | 16;
    row.WinPos[0] = row.WinPos[2] = 0;
    row.WinPos[1] = row.WinPos[3] = 200;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, true) & 3, 1u);
    row.WinMask = 16;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, true) & 3, 3u);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, true, true), 0u);
    row.WinRegs = 0xFFFF0021; // Scanline recorded an OBJ window even if later disabled.
    row.WinMask = 0;
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false), 0u);
}

POLICY_TEST(WideWindowEffectsFollowRegionAndVisibleTarget)
{
    WindowRow row;
    row.BlendCnt = 1 | (3 << 6);
    row.BlendCoef[2] = 31;
    CHECK_EQ((MakeWideWindowEdgePolicy(row, 0, false, false) >> 2) & 3, 3u);
    CHECK_EQ((MakeWideWindowEdgePolicy(row, 0, false, false) >> 4) & 31, 16u);
    row.WinRegs = 0xFFFFFF01; // Effects disabled outside, BG0 visible.
    CHECK_EQ((MakeWideWindowEdgePolicy(row, 0, false, false) >> 2) & 3, 0u);
    row.WinRegs = 0xFFFFFF20; // BG0 masked; only backdrop is a target.
    CHECK_EQ((MakeWideWindowEdgePolicy(row, 0, false, false) >> 2) & 3, 0u);
    row.BlendCnt = 0x20 | (2 << 6);
    row.BackColor = 0x9234;
    u32 policy = MakeWideWindowEdgePolicy(row, 0, false, false);
    CHECK_EQ(policy & 3, 1u);
    CHECK_EQ((policy >> 2) & 3, 2u);
    CHECK_EQ((policy >> 9) & 0xFFFF, row.BackColor);
    row.BlendCnt = 0x20 | (1 << 6);
    CHECK_EQ(MakeWideWindowEdgePolicy(row, 0, false, false), 0u);
}

POLICY_TEST(WideCoverUsesItsOwnPriorityWindowAndEffectTarget)
{
    WindowRow row;
    row.BGPrio[0] = 2;
    row.BGPrio[2] = 0;
    row.BackColor = 0x6562;
    row.BlendCnt = (1u << 2) | (3u << 6);
    row.BlendCoef[2] = 7;
    u32 policy = MakeWideWindowPolicy(row, 0, 0, false, false, 2, 0x1234);
    CHECK_EQ(policy & 3, 1u);
    CHECK_EQ((policy >> 9) & 0xFFFF, 0x1234u);
    CHECK_EQ((policy >> 2) & 3, 3u);
    CHECK_EQ((policy >> 4) & 31, 7u);
    row.WinRegs = 0xFFFFFF21; // Cover disabled in this window region.
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false, false, 2, 0x1234) & 3, 3u);
    row.WinRegs = 0xFFFFFFFF;
    row.BGPrio[2] = 2; // BG0 wins a priority tie.
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false, false, 2, 0x1234) & 3, 3u);
    row.BGPrio[2] = 0;
    row.BlendCnt = (1u << 2) | (1u << 6);
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, false, false, 2, 0x1234), 0u);
    row.WinRegs = 0xFFFF0024;
    CHECK_EQ(MakeWideWindowPolicy(row, 0, 0, true, false, 2, 0x1234), 0u);
}

POLICY_TEST(WideCoverChecksAllReferencedArtworkAndCurrentPalette)
{
    std::array<u8, 65536> vram {};
    std::array<u8, 512> palette {};
    WideTextCoverSource source {vram.data(), 65535, palette.data(), 256, 256, 0, 8192, false};
    u32 color = 0;
    CHECK_EQ(source.UniformColor(color), false); // Entirely transparent.
    vram[8192] = 0x11;
    palette[2] = 9;
    CHECK_EQ(source.UniformColor(color), true);
    CHECK_EQ(color, 9u);
    CHECK_EQ(source.OpaqueAt(0, 0), true);
    CHECK_EQ(source.OpaqueAt(7, 0), false);
    vram[1] = 4; // Flip X in the first tile.
    CHECK_EQ(source.OpaqueAt(0, 0), false);
    CHECK_EQ(source.OpaqueAt(7, 0), true);
    vram[2046] = 1; // Distinct tile, far from the sampled screen edges.
    vram[8192 + 32] = 2;
    palette[4] = 10;
    CHECK_EQ(source.UniformColor(color), false);
    palette[4] = 9;
    CHECK_EQ(source.UniformColor(color), true);
    palette[2] = 8; // Palette-only change must invalidate the proof.
    CHECK_EQ(source.UniformColor(color), false);
    palette[2] = 9;
    vram[2047] = 16; // Same tile number, different palette bank.
    CHECK_EQ(source.UniformColor(color), false);
}

POLICY_TEST(WideCoverEightBitAndScreenBlockAddressing)
{
    std::array<u8, 65536> vram {};
    std::array<u8, 512> palette {};
    WideTextCoverSource source {vram.data(), 65535, palette.data(), 512, 512, 0, 8192, true};
    vram[6144] = 1; // Bottom-right screen block.
    vram[8192 + 64] = 200;
    palette[400] = 27;
    u32 color = 0;
    CHECK_EQ(source.UniformColor(color), true);
    CHECK_EQ(color, 27u);
    CHECK_EQ(source.OpaqueAt(256, 256), true);
    CHECK_EQ(source.OpaqueAt(0, 256), false);
    CHECK_EQ(source.OpaqueAt(768, 768), true);
    source.Height = 128;
    CHECK_EQ(source.UniformColor(color), false);
}
