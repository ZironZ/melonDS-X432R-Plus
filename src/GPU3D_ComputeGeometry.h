// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

namespace melonDS
{

// Edge walking must use extrema from the same grid as its slope setup.
// Native-coordinate ties can separate when subpixel positions are scaled.
// Keep the native tie rules: first top vertex, rightmost bottom vertex.
inline void SelectComputePolygonExtrema(const s32 positions[][2], u32 count,
                                       u32& top, u32& bottom)
{
    top = bottom = 0;
    for (u32 i = 1; i < count; i++)
    {
        if (positions[i][1] < positions[top][1])
            top = i;
        if (positions[i][1] > positions[bottom][1] ||
            (positions[i][1] == positions[bottom][1] &&
             positions[i][0] > positions[bottom][0]))
            bottom = i;
    }
}

}
