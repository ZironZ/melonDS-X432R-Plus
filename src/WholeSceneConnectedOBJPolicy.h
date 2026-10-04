// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.h"
#include <algorithm>

namespace melonDS
{
struct ConnectedOBJSourceInput
{
    int OAM = -1, MatrixIndex = -1;
    int X = 0, Y = 0, Width = 0, Height = 0, BoundWidth = 0, BoundHeight = 0;
    int Matrix[4] {};
    u32 Mode = 0, Type = 0, Palette = 0, Tile = 0, Stride = 0, Priority = 0;
    bool Mosaic = false;
};

struct ConnectedOBJSourcePair
{
    int Members[2] {-1, -1};
    int X[2] {}, Y[2] {};
    int Width = 0, Height = 0;
};

// First experiment: two consecutive normal OAM entries forming one complete
// tiled source rectangle. Shared matrices alone are not evidence of a join.
inline bool PlanConnectedOBJSourcePair(const ConnectedOBJSourceInput& a,
                                       const ConnectedOBJSourceInput& b,
                                       ConnectedOBJSourcePair& result,
                                       int maximumExtent = 64)
{
    if (a.MatrixIndex < 0 || a.MatrixIndex != b.MatrixIndex ||
        b.OAM != a.OAM + 1 || a.Mode != 0 || b.Mode != 0 ||
        a.Type > 1 || a.Type != b.Type || a.Mosaic || b.Mosaic ||
        a.Priority != b.Priority || a.Palette != b.Palette || a.Stride != b.Stride ||
        a.Width <= 0 || a.Height <= 0 || b.Width <= 0 || b.Height <= 0)
        return false;
    for (int i = 0; i < 4; i++)
        if (a.Matrix[i] != b.Matrix[i]) return false;
    if (s64(a.Matrix[0]) * a.Matrix[3] == s64(a.Matrix[1]) * a.Matrix[2])
        return false;

    // OAM positions wrap at 512 horizontally and 256 vertically. Compare
    // centers on the nearest periodic image, then map exactly to source space.
    auto wrap = [](int value, int period) {
        return ((value + period / 2) % period + period) % period - period / 2;
    };
    s64 dx2 = wrap(2 * (b.X - a.X) + b.BoundWidth - a.BoundWidth, 1024);
    s64 dy2 = wrap(2 * (b.Y - a.Y) + b.BoundHeight - a.BoundHeight, 512);
    s64 sx512 = dx2 * a.Matrix[0] + dy2 * a.Matrix[1] + (a.Width - b.Width) * 256;
    s64 sy512 = dx2 * a.Matrix[2] + dy2 * a.Matrix[3] + (a.Height - b.Height) * 256;
    if (sx512 % 512 != 0 || sy512 % 512 != 0) return false;
    int sx = int(sx512 / 512), sy = int(sy512 / 512);
    bool vertical = sx == 0 && a.Width == b.Width && (sy == a.Height || sy == -b.Height);
    bool horizontal = sy == 0 && a.Height == b.Height && (sx == a.Width || sx == -b.Width);
    if (!vertical && !horizontal) return false;
    if (sx % 8 != 0 || sy % 8 != 0) return false;
    // The decoder must address the adjoining tiles of the same source layout.
    s64 expectedTile = s64(a.Tile) + (sy / 8) * s64(a.Stride) + (sx / 8) * (a.Type == 0 ? 32 : 64);
    if (expectedTile != b.Tile) return false;
    int minX = std::min(0, sx), minY = std::min(0, sy);
    result.Width = std::max(a.Width, sx + b.Width) - minX;
    result.Height = std::max(a.Height, sy + b.Height) - minY;
    if (result.Width > maximumExtent || result.Height > maximumExtent) return false;
    result.X[0] = -minX; result.Y[0] = -minY;
    result.X[1] = sx - minX; result.Y[1] = sy - minY;
    return true;
}
}
