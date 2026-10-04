// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include "types.h"

namespace melonDS
{
// Presentation only: base color (-1 means composed content), master mode, factor.
// Match the native compositor/final pass, independently of widened-source validity.
// Display capture must not bake these final display effects into its source.
inline std::array<int, 3> WideDisplayPresentation(bool screensEnabled, bool unitEnabled,
    bool forcedBlank, int engine, u32 dispCnt, u16 masterBrightness)
{
    if (!screensEnabled) return {0, 0, 0};
    const u32 mode = (dispCnt >> 16) & (engine ? 1 : 3);
    if (mode == 0) return {63, 0, 0}; // Display off is white, unaffected by brightness.
    int base = -1;
    if (mode == 1)
    {
        if (!unitEnabled) base = engine ? 63 : 0;
        else if (forcedBlank) base = 63;
    }
    return {base, (masterBrightness >> 14) & 3, std::min<int>(masterBrightness & 31, 16)};
}
}
