// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace melonDS
{
// The two packed side strips include a small overlap with the native viewport.
// Source and destination coordinates stay one-to-one at any renderer scale.
struct WideCaptureStrips
{
    int ViewWidth;
    int Overlap;
    constexpr int Gap() const { return 256 - 2 * Overlap; }
    constexpr int StripWidth() const { return (ViewWidth - Gap()) / 2; }
    constexpr int PackedWidth() const { return StripWidth() * 2; }
    constexpr int RightSource() const { return StripWidth() + Gap(); }
};

// Proof of an untransformed screen copy, independent of sprite size/order.
// Contained, disjoint rectangles whose total area is the display area leave
// neither holes nor competing owners. No pixel readback is required.
class WideCaptureLayout
{
public:
    bool Add(int x, int y, int width, int height, int sourceX, int sourceY)
    {
        if (Count == Pieces.size() || x != sourceX || y != sourceY ||
            x < 0 || y < 0 || width <= 0 || height <= 0 ||
            width > 256 - x || height > 192 - y) return false;
        for (unsigned i = 0; i < Count; ++i)
        {
            const auto& p = Pieces[i];
            if (x < p.X + p.Width && x + width > p.X &&
                y < p.Y + p.Height && y + height > p.Y) return false;
        }
        Pieces[Count++] = {x, y, width, height};
        Area += width * height;
        return true;
    }
    bool Complete() const { return Area == 256 * 192; }

private:
    struct Piece { int X, Y, Width, Height; };
    std::array<Piece, 128> Pieces;
    unsigned Count = 0;
    int Area = 0;
};
}
