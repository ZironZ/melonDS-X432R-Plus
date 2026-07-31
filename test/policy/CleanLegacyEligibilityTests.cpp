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

// Table tests for the hybrid clean-legacy candidate admission gate
// (ChooseHybridCleanLegacyBlockReason), extracted from GPU2D_OpenGL.cpp as a
// policy-test boundary. The alpha-blend rows cover the NSMB target1/no-target1
// case matrix.
//
// Not covered here (selector-level, not frame-gate-level): EBA skip halo,
// Advance Wars yellow-vs-orange, Pokemon Diamond intro negative control —
// those live in the per-pixel selector and need recorded cases through the
// replay harness.

#include "PolicyTestHarness.h"
#include "WholeSceneScalePolicy.h"

using namespace melonDS;

namespace
{

u32 MakeDispCnt(u32 dispmode, bool bg0Direct3D = true, bool objWindow = false)
{
    u32 dispcnt = dispmode << 16;
    if (bg0Direct3D)
        dispcnt |= 1u << 3;
    if (objWindow)
        dispcnt |= 1u << 15;
    return dispcnt;
}

u16 MakeBlendCnt(u32 mode, u32 target1, u32 target2)
{
    return static_cast<u16>((target2 << 8) | (mode << 6) | target1);
}

// A fully-admissible frame: the Kingdom Hearts menu shape (Direct3D visible
// on the main engine, composited display, no capture, no OBJ window, no
// blending, no capture-classified layers/sprites).
HybridCleanLegacyEligibilityInputs MakeEligibleInputs()
{
    HybridCleanLegacyEligibilityInputs inputs = {};
    inputs.ScalePathAvailable = true;
    inputs.ConservativeHybridMode = true;
    inputs.CandidateEnabled = true;
    inputs.MainEngine = true;
    inputs.DispCnt = MakeDispCnt(1);
    inputs.LayerEnable = 0x1F;
    inputs.HasRenderedPolygons = true;
    return inputs;
}

}

POLICY_TEST(CleanLegacyEligibleBaseline)
{
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(MakeEligibleInputs()),
             HybridCleanLegacyBlockReason::None);
    CHECK(CanUseHybridCleanLegacyCandidate(MakeEligibleInputs()));
}

POLICY_TEST(CleanLegacyChecklistBlockers)
{
    HybridCleanLegacyEligibilityInputs inputs = MakeEligibleInputs();
    inputs.ScalePathAvailable = false;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::PathModeOrSettingUnavailable);

    inputs = MakeEligibleInputs();
    inputs.ConservativeHybridMode = false;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::PathModeOrSettingUnavailable);

    inputs = MakeEligibleInputs();
    inputs.CandidateEnabled = false;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::PathModeOrSettingUnavailable);

    inputs = MakeEligibleInputs();
    inputs.MainEngine = false;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::SubEngine);

    // Display modes 0 (forced blank), 2 (VRAM display), 3 (FIFO) are not
    // composited output.
    inputs = MakeEligibleInputs();
    inputs.DispCnt = MakeDispCnt(0);
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::DisplayModeNotComposited);
    inputs.DispCnt = MakeDispCnt(2);
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::DisplayModeNotComposited);

    // Direct3D must be visible: DispCnt BG0 bit, LayerEnable bit 0, and at
    // least one rendered polygon.
    inputs = MakeEligibleInputs();
    inputs.DispCnt = MakeDispCnt(1, false);
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::NoVisibleDirect3D);
    inputs = MakeEligibleInputs();
    inputs.LayerEnable = 0x1E;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::NoVisibleDirect3D);
    inputs = MakeEligibleInputs();
    inputs.HasRenderedPolygons = false;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::NoVisibleDirect3D);

    inputs = MakeEligibleInputs();
    inputs.FinalUpscaleRender3DNative = true;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::FinalUpscaleNative3D);

    inputs = MakeEligibleInputs();
    inputs.CaptureTransportActive = true;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::CaptureTransportActive);

    inputs = MakeEligibleInputs();
    inputs.DispCnt = MakeDispCnt(1, true, true);
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::OBJWindowActive);

    inputs = MakeEligibleInputs();
    inputs.AnyEnabledCaptureBackedBGLayer = true;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::CaptureBackedBGLayer);

    inputs = MakeEligibleInputs();
    inputs.AnyCaptureBackedSprite = true;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::CaptureBackedSprite);
}

// Normal WIN0/WIN1 must not disqualify the whole frame: the per-pixel
// selector has window metadata and avoids candidate use in excluded regions.
// Only the OBJ window blocks (less predictable coverage).
POLICY_TEST(CleanLegacyNormalWindowsDoNotBlock)
{
    HybridCleanLegacyEligibilityInputs inputs = MakeEligibleInputs();
    inputs.DispCnt |= (1u << 13) | (1u << 14);
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::None);
}

// The NSMB target1 alpha matrix. BLDCNT mode 1 admission depends on which
// blend state is active; the boundary rows are where selector oscillation
// (resolution pop mid-fade) lives, so they must stay exactly here.
POLICY_TEST(CleanLegacyAlphaBlendMatrix)
{
    const u32 d3d = 1u << 0;
    const u32 bg1 = 1u << 1;
    const u32 obj = 1u << 4;
    const u32 backdrop = 1u << 5;

    // No target1 at all: blend cannot fire on anything, admissible.
    // (The NSMB no-target1-alpha variant.)
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, 0, bg1), 8, 8));

    // Direct3D target1 over 2D target2, both coefficients contributing: the
    // native-stack candidate has already resolved the native blend
    // coherently. (The NSMB target1-alpha variant.)
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d, bg1), 8, 8));
    // Same shape fully zeroed toward target2 (EVA=0/EVB=16)...
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d, bg1), 0, 16));
    // ...or with both coefficients zero.
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d, bg1), 0, 0));
    // 2D target1 blending against Direct3D target2, both contributing.
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, bg1, d3d), 8, 8));
    // OBJ counts as a 2D layer on either side.
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d | obj, bg1), 8, 8));

    // The half-blend state (EVA>0, EVB=0) is not a validated combination.
    CHECK(!IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d, bg1), 8, 0));
    // EVA=0/EVB<16 partial states are not validated either.
    CHECK(!IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d, bg1), 0, 8));
    // Direct3D target1 with an empty target2 has nothing coherent to resolve.
    CHECK(!IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d, 0), 8, 8));
    // Backdrop in target1 is outside the allowed mask.
    CHECK(!IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, d3d | backdrop, bg1), 8, 8));
    // A pure-2D blend with no Direct3D involvement on either side is not the
    // resolved-native-blend case.
    CHECK(!IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, bg1, bg1), 8, 8));
    // 2D target1 against Direct3D target2 needs both coefficients.
    CHECK(!IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(1, bg1, d3d), 8, 0));

    // Brightness effects (modes 2/3) never gate the candidate: the
    // native-stack path reproduces them as a native compositor transform.
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(2, d3d, 0), 8, 0));
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(3, 0x3F, 0), 0, 0));
    CHECK(IsCleanLegacyAlphaBlendStateValidated(MakeBlendCnt(0, d3d, bg1), 8, 0));

    // And through the full gate: an invalid alpha state maps to the
    // UnsupportedAlphaBlendState reason.
    HybridCleanLegacyEligibilityInputs inputs = MakeEligibleInputs();
    inputs.BlendCnt = MakeBlendCnt(1, d3d, bg1);
    inputs.EVA = 8;
    inputs.EVB = 0;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::UnsupportedAlphaBlendState);
    inputs.EVB = 8;
    CHECK_EQ(ChooseHybridCleanLegacyBlockReason(inputs),
             HybridCleanLegacyBlockReason::None);
}
