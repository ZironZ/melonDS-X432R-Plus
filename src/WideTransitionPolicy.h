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

// Optional border interpretation: a collapsed, untextured black triangle
// spanning exactly one native edge follows that edge to the outer canvas.
// Keep its depth/material state; do not discard arbitrary degenerate polygons.
inline unsigned WideNativeEdgeLine(const Polygon& poly)
{
    if (!poly.WideOrthographic || poly.NumVertices != 3 || poly.Type != 1 ||
        poly.Degenerate || poly.IsShadow || poly.IsShadowMask ||
        ((poly.TexParam >> 26) & 7) || !(poly.Attr & (31u << 16)) ||
        (poly.Attr & 0x8030)) return 0;
    const auto& first = *poly.Vertices[0];
    if (first.Position[3] <= 0) return 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto& v = *poly.Vertices[i];
        if (!v.NativeSourceValid || v.Clipped ||
            v.Position[2] != first.Position[2] || v.Position[3] != first.Position[3] ||
            poly.FinalZ[i] != poly.FinalZ[0] || poly.FinalW[i] != poly.FinalW[0]) return 0;
        for (unsigned c = 0; c < 3; ++c) if (v.FinalColor[c] != 0) return 0;
    }
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        const unsigned other = 1 - axis;
        const s32 edge = first.NativeSourcePosition[axis];
        const s32 extent = axis ? 3072 : 4096;
        const s32 span = other ? 3072 : 4096;
        if (edge != 0 && edge != extent) continue;
        unsigned endpoints = 0;
        bool valid = true;
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto& v = *poly.Vertices[i];
            const s32 position = v.NativeSourcePosition[other];
            valid &= v.NativeSourcePosition[axis] == edge && (position == 0 || position == span);
            endpoints |= position == 0 ? 1 : position == span ? 2 : 0;
        }
        if (valid && endpoints == 3)
            return (axis ? 3u : 12u) | (1u << (axis * 2 + (edge != 0)));
    }
    return 0;
}

inline WideTransitionPlan BuildWideTransitionPlan(Polygon* const* polygons, unsigned count, bool enabled,
                                                  bool expandEdgeLines = false)
{
    WideTransitionPlan result;
    if (!enabled) return result;
    struct Candidate
    {
        const Polygon* Poly;
        std::array<s64, 11> Key;
        s32 Left, Right, Top, Bottom;
        unsigned Edges;
    };
    std::vector<Candidate> candidates;
    for (unsigned p = 0; p < count; ++p)
    {
        const auto& poly = *polygons[p];
        if (expandEdgeLines)
        {
            const unsigned edges = WideNativeEdgeLine(poly);
            if (edges) { result.emplace(&poly, edges); continue; }
        }
        if (!poly.WideOrthographic || poly.NumVertices != 4 || poly.Degenerate || poly.Type ||
            poly.Translucent || poly.IsShadow || poly.IsShadowMask ||
            ((poly.TexParam >> 26) & 7) || ((poly.Attr >> 16) & 31) != 31 ||
            (poly.Attr & 0x8030)) continue; // No fog, toon shading, or shadow modes.
        const auto& first = *poly.Vertices[0];
        if (first.Position[3] <= 0) continue;
        s32 left = 4096, right = 0, top = 3072, bottom = 0;
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
            top = std::min(top, v.NativeSourcePosition[1]);
            bottom = std::max(bottom, v.NativeSourcePosition[1]);
        }
        if (!valid || left < 0 || right > 4096 || left >= right || top < 0 || bottom > 3072 || top >= bottom) continue;
        if (left != 0 && right != 4096 && top != 0 && bottom != 3072) continue;
        unsigned corners = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto& v = *poly.Vertices[i];
            const auto& next = *poly.Vertices[(i + 1) % 4];
            const s32 x = v.NativeSourcePosition[0], y = v.NativeSourcePosition[1];
            if ((x != left && x != right) || (y != top && y != bottom) ||
                (x != next.NativeSourcePosition[0] && y != next.NativeSourcePosition[1]))
                valid = false;
            corners |= 1u << ((x == right ? 1 : 0) + (y == bottom ? 2 : 0));
        }
        if (!valid || corners != 15) continue;
        candidates.push_back({&poly, {poly.Attr, poly.WBuffer, poly.FacingView,
            first.Position[2], first.Position[3], poly.FinalZ[0], poly.FinalW[0],
            first.FinalColor[0], first.FinalColor[1], first.FinalColor[2], poly.TexParam},
            left, right, top, bottom, (left == 0 ? 1u : 0u) | (right == 4096 ? 2u : 0u) |
                (top == 0 ? 4u : 0u) | (bottom == 3072 ? 8u : 0u)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.Key < b.Key; });
    for (size_t begin = 0, end; begin < candidates.size(); begin = end)
    {
        end = begin + 1;
        while (end < candidates.size() && candidates[end].Key == candidates[begin].Key) ++end;
        if (end == begin + 1 && candidates[begin].Edges == 15)
        {
            result.emplace(candidates[begin].Poly, 15);
            continue;
        }
        // Prove opposite covers independently on each axis. Each accepted
        // rectangle also spans the other native axis, so all of its outer
        // edges must reach the expanded canvas, including the corner regions.
        for (int axis = 0; axis < 2; ++axis)
        {
            const Candidate* pair[2] = {};
            unsigned matches = 0;
            for (size_t i = begin; i < end; ++i)
            {
                const auto& candidate = candidates[i];
                if (axis ? candidate.Left != 0 || candidate.Right != 4096 :
                           candidate.Top != 0 || candidate.Bottom != 3072) continue;
                if (matches < 2) pair[matches] = &candidate;
                ++matches;
            }
            if (matches != 2) continue;
            const auto& a = *pair[0];
            const auto& b = *pair[1];
            const unsigned mask = axis ? 12 : 3;
            const unsigned first = axis ? 4 : 1, last = axis ? 8 : 2;
            const bool ordered = (a.Edges & mask) == first && (b.Edges & mask) == last &&
                (axis ? a.Bottom <= b.Top : a.Right <= b.Left);
            const bool reversed = (b.Edges & mask) == first && (a.Edges & mask) == last &&
                (axis ? b.Bottom <= a.Top : b.Right <= a.Left);
            if (ordered || reversed)
            {
                result.emplace(a.Poly, a.Edges);
                result.emplace(b.Poly, b.Edges);
                // A four-piece frame can also have matching solid covers
                // across the gap between this pair (e.g. around an iris).
                // Require their edges to meet exactly and preserve every
                // inner edge. The textured opening itself is never changed.
                const auto& firstCover = ordered ? a : b;
                const auto& lastCover = ordered ? b : a;
                const Candidate* cross[2] = {};
                unsigned crossCount = 0;
                for (size_t i = begin; i < end; ++i)
                {
                    const auto& c = candidates[i];
                    const bool fits = axis ? c.Top == firstCover.Bottom && c.Bottom == lastCover.Top :
                                             c.Left == firstCover.Right && c.Right == lastCover.Left;
                    if (!fits) continue;
                    if (crossCount < 2) cross[crossCount] = &c;
                    ++crossCount;
                }
                if (crossCount != 2) continue;
                const auto& c = *cross[0];
                const auto& d = *cross[1];
                const unsigned crossMask = axis ? 3 : 12;
                const unsigned crossFirst = axis ? 1 : 4, crossLast = axis ? 2 : 8;
                const bool crossOrdered = (c.Edges & crossMask) == crossFirst &&
                    (d.Edges & crossMask) == crossLast && (axis ? c.Right <= d.Left : c.Bottom <= d.Top);
                const bool crossReversed = (d.Edges & crossMask) == crossFirst &&
                    (c.Edges & crossMask) == crossLast && (axis ? d.Right <= c.Left : d.Bottom <= c.Top);
                if (crossOrdered || crossReversed)
                {
                    result.emplace(c.Poly,c.Edges);
                    result.emplace(d.Poly,d.Edges);
                }
            }
        }
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

inline s32 WideTransitionY(const WideTransitionPlan& plan, const Polygon* poly,
                          const Vertex* vertex, s32 y, s32 outputHeight)
{
    if (plan.empty()) return y;
    const auto it = plan.find(poly);
    if (it == plan.end()) return y;
    if ((it->second & 4) && vertex->NativeSourcePosition[1] == 0) return 0;
    if ((it->second & 8) && vertex->NativeSourcePosition[1] == 3072) return outputHeight;
    return y;
}
}
