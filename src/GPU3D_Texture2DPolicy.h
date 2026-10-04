// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU3D.h"
#include "GPU3D_TextureTypes.h"
#include <algorithm>

namespace melonDS
{
// UI fills can stretch one texel or a tiny native patch across a large rectangle.
// Sampling a reconstructed atlas magnifies variation inside those texels.
// Use native nearest sampling for the whole draw, retaining its geometry.
// Do not classify captures, wrapped footprints, perspective surfaces or skewed
// UVs. This is independent of the >=2-texel sprite edge-inset policy.
inline bool IsStretchedTexture2DStrip(const Polygon& p)
{
    const u32 format = (p.TexParam >> 26) & 7;
    if (p.Type != 0 || p.NumVertices != 4 || p.Degenerate || p.IsShadow || p.IsShadowMask ||
        format == 0 || format == 5 || format == 7 || (p.TexParam & 0xC00C0000u)) return false;
    int x0 = p.Vertices[0]->HiresPosition[0], x1 = x0;
    int y0 = p.Vertices[0]->HiresPosition[1], y1 = y0;
    int u0 = p.Vertices[0]->TexCoords[0], u1 = u0;
    int v0 = p.Vertices[0]->TexCoords[1], v1 = v0;
    for (int i = 0; i < 4; i++)
    {
        const auto& v = *p.Vertices[i];
        if (p.FinalW[i] <= 0 || p.FinalW[i] != p.FinalW[0] || p.FinalZ[i] != p.FinalZ[0]) return false;
        x0 = std::min(x0, v.HiresPosition[0]); x1 = std::max(x1, v.HiresPosition[0]);
        y0 = std::min(y0, v.HiresPosition[1]); y1 = std::max(y1, v.HiresPosition[1]);
        u0 = std::min(u0, int(v.TexCoords[0])); u1 = std::max(u1, int(v.TexCoords[0]));
        v0 = std::min(v0, int(v.TexCoords[1])); v1 = std::max(v1, int(v.TexCoords[1]));
    }
    if (x0 == x1 || y0 == y1 || u0 < 0 || v0 < 0 ||
        u1 > int(TextureWidth(p.TexParam)*16) || v1 > int(TextureHeight(p.TexParam)*16)) return false;
    const Vertex* corners[4] = {};
    for (int i = 0; i < 4; i++)
    {
        const auto* v = p.Vertices[i];
        const int x = v->HiresPosition[0], y = v->HiresPosition[1];
        if ((x != x0 && x != x1) || (y != y0 && y != y1)) return false;
        const int corner = (x == x1 ? 1 : 0) | (y == y1 ? 2 : 0);
        if (corners[corner]) return false;
        corners[corner] = v;
        const auto* next = p.Vertices[(i+1)%4];
        if (x != next->HiresPosition[0] && y != next->HiresPosition[1]) return false;
    }
    auto constantAcross = [&](int channel, int axis)
    {
        return corners[0]->TexCoords[channel] == corners[axis]->TexCoords[channel] &&
            corners[3]->TexCoords[channel] == corners[3^axis]->TexCoords[channel];
    };
    const bool aligned = constantAcross(0, 2) && constantAcross(1, 1);
    const bool swapped = constantAcross(0, 1) && constantAcross(1, 2);
    if (!aligned && !swapped) return false;
    // Whole-texel patches of at most 2x2, magnified at least fourfold on
    // BOTH axes, are fill candidates rather than ordinary-sized artwork.
    // Repeat-enable bits are harmless here: the bounds check above keeps
    // this footprint within the first period. Mirror/generated UVs remain
    // excluded, and native sampling retains the draw's actual wrap state.
    const int du = u1-u0, dv = v1-v0;
    const bool smallPatch = u0%16 == 0 && v0%16 == 0 &&
        du > 0 && dv > 0 && du <= 32 && dv <= 32 && du%16 == 0 && dv%16 == 0;
    if (smallPatch &&
        ((aligned && x1-x0 >= 4*du && y1-y0 >= 4*dv) ||
         (swapped && y1-y0 >= 4*du && x1-x0 >= 4*dv))) return true;
    // Preserve the original clamp-only rule for long one-texel strips.
    if (p.TexParam & 0x00030000u) return false;
    auto singleTexel = [](int lo, int hi, int size)
    {
        // The upper endpoint of a nonzero footprint is exclusive. Reject
        // tiny ranges crossing a native texel boundary, and out-of-range constants.
        return lo < size*16 && hi-lo <= 16 && lo/16 == (hi > lo ? hi-1 : hi)/16;
    };
    const bool narrowU = singleTexel(u0, u1, TextureWidth(p.TexParam));
    const bool narrowV = singleTexel(v0, v1, TextureHeight(p.TexParam));
    return (aligned && ((narrowU && x1-x0 > 16) || (narrowV && y1-y0 > 16))) ||
        (swapped && ((narrowU && y1-y0 > 16) || (narrowV && x1-x0 > 16)));
}
}
