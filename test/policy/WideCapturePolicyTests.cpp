// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideCapturePolicy.h"
#include "WideCaptureLayout.h"
using namespace melonDS;

POLICY_TEST(NativeCompositionWideCaptureKeepsSeparateMargins)
{
    // Mario Kart captures BG0 3D plus a text BG and OBJ in Native Stack or
    // Postprocessing. The margin source still belongs to that frame.
    CHECK(CanRetainNativeCompositionWideSource(true, false, true, 3, 6, 0));
    CHECK(CanRetainNativeCompositionWideSource(true, false, true, 1, 6, 0));
    CHECK(CanRetainNativeCompositionWideSource(true, false, true, 15, 0x01010106, 0));
    // A 3D-only capture ignores the 2D layer configuration.
    CHECK(CanRetainNativeCompositionWideSource(true, true, false, 0, 0, 15));
    CHECK(!CanRetainNativeCompositionWideSource(false, true, true, 1, 6, 0));
    CHECK(!CanRetainNativeCompositionWideSource(false, false, true, 3, 6, 0));
    CHECK(!CanRetainNativeCompositionWideSource(true, false, false, 3, 6, 0));
    CHECK(!CanRetainNativeCompositionWideSource(true, false, true, 2, 6, 0));
    CHECK(!CanRetainNativeCompositionWideSource(true, false, true, 3, 6, 2));
    CHECK(!CanRetainNativeCompositionWideSource(true, false, true, 3, 0, 0));
    for (u32 type : {2u, 3u, 4u, 5u, 6u, 7u, 8u})
        for (int layer = 1; layer < 4; ++layer)
            CHECK(!CanRetainNativeCompositionWideSource(true, false, true,
                1u | (1u << layer), 6u | (type << (layer * 8)), 0));
    // Disabled layers do not contribute to the captured composition.
    CHECK(CanRetainNativeCompositionWideSource(true, false, true, 1, 0x08080806, 0));
}

POLICY_TEST(WideCaptureOverlapPreservesSourceCoordinates)
{
    for (int width : {258, 342, 448, 682, 768})
        for (int overlap : {0, 2})
            for (int scale : {1, 2, 4, 8, 16})
            {
                const WideCaptureStrips strips {width, overlap};
                const int nativeLeft = (width - 256) / 2;
                CHECK_EQ(strips.StripWidth(), nativeLeft + overlap);
                CHECK_EQ(strips.RightSource(), nativeLeft + 256 - overlap);
                CHECK_EQ(strips.RightSource() + strips.StripWidth(), width);
                // Every texel in either packed strip maps to its original
                // viewport position, including the retained native overlap.
                for (int p = 0; p < strips.PackedWidth() * scale; ++p)
                {
                    const bool right = p >= strips.StripWidth() * scale;
                    const int source = p + (right ? strips.Gap() * scale : 0);
                    CHECK(source >= 0 && source < width * scale);
                    CHECK_EQ(source - (right ? strips.Gap() * scale : 0), p);
                }
            }
}

POLICY_TEST(WideFeedbackRequiresTrackedUnshiftedHistory)
{
    CHECK(CanUseWideFeedbackSource(false, true, 0, true, false, 7, 7));
    CHECK(!CanUseWideFeedbackSource(false, true, 0, true, false, 7, 8));
    CHECK(!CanUseWideFeedbackSource(true, true, 0, true, false, 7, 7));
    CHECK(!CanUseWideFeedbackSource(false, false, 0, true, false, 7, 7));
    CHECK(!CanUseWideFeedbackSource(false, true, 1, true, false, 7, 7));
    CHECK(!CanUseWideFeedbackSource(false, true, 0, false, false, 7, 7));
}

POLICY_TEST(WideFeedbackRetainsSameBankImageButNeverInventsOne)
{
    CHECK(CanUseWideFeedbackSource(false, true, 0, true, true, 7, 8));
    CHECK(CanUseWideFeedbackSource(false, true, 0, true, true, 7, 0));
    CHECK(!CanUseWideFeedbackSource(false, true, 0, true, true, 0, 8));
    CHECK(!CanUseWideFeedbackSource(false, true, 0, true, false, 0, 0));
    CHECK(!CanUseWideFeedbackSource(false, true, 0, false, true, 7, 8));
    CHECK(!CanUseWideFeedbackSource(false, false, 0, true, true, 7, 8));
    CHECK(!CanUseWideFeedbackSource(false, true, 1, true, true, 7, 8));
}

POLICY_TEST(WideVRAMDisplayRequiresMatchingCompletedCapture)
{
    CHECK(CanPresentWideVRAMCapture(true, true, true, 7, 7, 0));
    CHECK(!CanPresentWideVRAMCapture(true, true, true, 7, 8, 0));
    CHECK(!CanPresentWideVRAMCapture(true, true, true, 0, 0, 0));
    CHECK(!CanPresentWideVRAMCapture(false, true, true, 7, 7, 0));
    CHECK(!CanPresentWideVRAMCapture(true, false, true, 7, 7, 0));
    CHECK(!CanPresentWideVRAMCapture(true, true, false, 7, 7, 0));
    CHECK(CanPresentWideVRAMCapture(true, true, true, 0, 0, 7));
    CHECK(!CanPresentWideVRAMCapture(true, false, true, 0, 0, 7));
    CHECK(!CanPresentWideVRAMCapture(false, true, true, 0, 0, 7));
    CHECK(!CanPresentWideVRAMCapture(true, true, false, 0, 0, 7));
}

POLICY_TEST(WideCaptureIgnoresOnlyEntirelyUnmappedDirectColorBitmaps)
{
    u32 mapping[32] = {};
    CHECK(IsUnmappedWideBitmap(5, mapping, 32));
    for (u32 type : {0u,1u,2u,3u,4u,6u,7u,8u})
        CHECK(!IsUnmappedWideBitmap(type, mapping, 32));
    for (int i = 0; i < 32; ++i)
    {
        mapping[i] = 4;
        CHECK(!IsUnmappedWideBitmap(5, mapping, 32));
        mapping[i] = 0;
    }
}
