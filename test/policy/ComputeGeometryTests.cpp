// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolicyTestHarness.h"
#include "GPU3D_ComputeGeometry.h"

using namespace melonDS;

namespace
{

// Each edge chain must bracket every raster scanline. A native-grid bottom
// can stop one chain early when two vertices separate at higher resolution.
bool CoversScanlines(const s32 positions[][2], u32 count, u32 top, u32 bottom)
{
    s32 first = positions[0][1], last = first;
    for (u32 i = 1; i < count; i++)
    {
        if (positions[i][1] < first) first = positions[i][1];
        if (positions[i][1] > last) last = positions[i][1];
    }
    for (u32 step : {1U, count - 1})
    {
        u32 current = top, next = (top + step) % count;
        for (s32 y = first; y < last; y++)
        {
            u32 advances = 0;
            while (y >= positions[next][1] && current != bottom)
            {
                if (++advances > count) return false;
                current = next;
                next = (current + step) % count;
            }
            if (positions[current][1] > y || positions[next][1] <= y)
                return false;
        }
    }
    return true;
}

}

POLICY_TEST(ComputeExtremaSeparateNativeScanlineTies)
{
    // At 1x, vertices 0/1 tie at the top and 2/3 tie at the bottom.
    // At 4x, vertex 1 is higher and vertex 3 is lower. Starting at 0 or
    // stopping at 2 would use an edge outside its vertical extent.
    const s32 native[][2] = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};
    const s32 scaled[][2] = {{40, 43}, {80, 40}, {80, 80}, {40, 83}};
    u32 top, bottom;
    SelectComputePolygonExtrema(native, 4, top, bottom);
    CHECK_EQ(top, 0U);
    CHECK_EQ(bottom, 2U);
    CHECK(!CoversScanlines(scaled, 4, top, bottom));
    SelectComputePolygonExtrema(scaled, 4, top, bottom);
    CHECK_EQ(top, 1U);
    CHECK_EQ(bottom, 3U);
    CHECK(CoversScanlines(scaled, 4, top, bottom));
}

POLICY_TEST(ComputeExtremaCoverBothWindingsAndClippedPolygons)
{
    // Includes horizontal top/bottom edges and a ten-vertex clipped polygon.
    const s32 shape[10][2] = {
        {20, 0}, {40, 0}, {60, 10}, {70, 30}, {60, 50},
        {40, 60}, {20, 60}, {0, 50}, {-10, 30}, {0, 10}};
    for (u32 count : {3U, 4U, 10U})
    for (u32 offset = 0; offset < count; offset++)
    for (u32 step : {1U, count - 1})
    {
        s32 positions[10][2];
        for (u32 i = 0; i < count; i++)
        {
            u32 source = (offset + i * step) % count;
            positions[i][0] = shape[source][0];
            positions[i][1] = shape[source][1];
        }
        u32 top, bottom;
        SelectComputePolygonExtrema(positions, count, top, bottom);
        CHECK(CoversScanlines(positions, count, top, bottom));
    }
}

POLICY_TEST(ComputeExtremaKeepEndpointTieRules)
{
    const s32 rectangle[][2] = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};
    u32 top, bottom;
    SelectComputePolygonExtrema(rectangle, 4, top, bottom);
    CHECK_EQ(top, 0U);
    CHECK_EQ(bottom, 2U);
    const s32 collapsed[][2] = {{10, 10}, {10, 10}, {10, 10}};
    SelectComputePolygonExtrema(collapsed, 3, top, bottom);
    CHECK_EQ(top, 0U);
    CHECK_EQ(bottom, 0U);
}
