// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU3D.h"
#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace melonDS
{
// Presentation-only extension of solid orthographic covers. Never move an
// inside edge, change the DS geometry, or infer coverage from textured alpha.
using WideTransitionPlan = std::unordered_map<const Polygon*, unsigned>;

inline WideTransitionPlan BuildWideTransitionPlan(Polygon* const* polygons, unsigned count, bool enabled)
{
    WideTransitionPlan result;
    if (!enabled) return result;
    struct Candidate
    {
        const Polygon* Poly;
        std::array<s64, 11> Key;
        s32 Left, Right;
        unsigned Edges;
    };
    std::vector<Candidate> candidates;
    for (unsigned p = 0; p < count; ++p)
    {
        const auto& poly = *polygons[p];
        if (!poly.WideOrthographic || poly.NumVertices != 4 || poly.Degenerate || poly.Type ||
            poly.Translucent || poly.IsShadow || poly.IsShadowMask ||
            ((poly.TexParam >> 26) & 7) || ((poly.Attr >> 16) & 31) != 31 ||
            (poly.Attr & 0x8030)) continue; // No fog, toon shading, or shadow modes.
        const auto& first = *poly.Vertices[0];
        if (first.Position[3] <= 0) continue;
        s32 left = 4096, right = 0;
        bool valid = true;
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto& v = *poly.Vertices[i];
            valid &= v.NativeSourceValid && !v.Clipped &&
                v.Position[2] == first.Position[2] && v.Position[3] == first.Position[3] &&
                poly.FinalZ[i] == poly.FinalZ[0] && poly.FinalW[i] == poly.FinalW[0];
            for (unsigned c = 0; c < 3; ++c) valid &= v.FinalColor[c] == first.FinalColor[c];
            left = std::min(left, v.NativeSourcePosition[0]);
            right = std::max(right, v.NativeSourcePosition[0]);
        }
        if (!valid || left < 0 || right > 4096 || left >= right || (left != 0 && right != 4096)) continue;
        unsigned corners = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto& v = *poly.Vertices[i];
            const auto& next = *poly.Vertices[(i + 1) % 4];
            const s32 x = v.NativeSourcePosition[0], y = v.NativeSourcePosition[1];
            if ((x != left && x != right) || (y != 0 && y != 3072) ||
                (x != next.NativeSourcePosition[0] && y != next.NativeSourcePosition[1]))
                valid = false;
            corners |= 1u << ((x == right ? 1 : 0) + (y == 3072 ? 2 : 0));
        }
        if (!valid || corners != 15) continue;
        candidates.push_back({&poly, {poly.Attr, poly.WBuffer, poly.FacingView,
            first.Position[2], first.Position[3], poly.FinalZ[0], poly.FinalW[0],
            first.FinalColor[0], first.FinalColor[1], first.FinalColor[2], poly.TexParam},
            left, right, (left == 0 ? 1u : 0u) | (right == 4096 ? 2u : 0u)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.Key < b.Key; });
    for (size_t begin = 0, end; begin < candidates.size(); begin = end)
    {
        end = begin + 1;
        while (end < candidates.size() && candidates[end].Key == candidates[begin].Key) ++end;
        const auto& a = candidates[begin];
        bool accept = end == begin + 1 && a.Edges == 3;
        if (end == begin + 2)
        {
            const auto& b = candidates[begin + 1];
            accept = (a.Edges == 1 && b.Edges == 2 && a.Right <= b.Left) ||
                     (b.Edges == 1 && a.Edges == 2 && b.Right <= a.Left);
        }
        if (accept)
            for (size_t i = begin; i < end; ++i) result.emplace(candidates[i].Poly, candidates[i].Edges);
    }
    return result;
}

inline s32 WideTransitionX(const WideTransitionPlan& plan, const Polygon* poly,
                          const Vertex* vertex, s32 x, s32 outputWidth)
{
    // Almost all scenes have no qualifying covers.
    if (plan.empty()) return x;
    const auto it = plan.find(poly);
    if (it == plan.end()) return x;
    if ((it->second & 1) && vertex->NativeSourcePosition[0] == 0) return 0;
    if ((it->second & 2) && vertex->NativeSourcePosition[0] == 4096) return outputWidth;
    return x;
}
}
