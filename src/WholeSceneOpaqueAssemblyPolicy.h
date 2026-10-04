// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WholeSceneConnectedOBJPolicy.h"
#include <array>
#include <vector>

namespace melonDS
{
struct OpaqueOBJAssembly
{
    int Root = -1;
    std::vector<int> Members; // Compact indices, original OAM order.
};

// Inputs retain all sprite bounds; ineligible members have MatrixIndex = -1.
// Callers admit normal presentation color without windows or OBJ alpha blending.
// Combine only nested, concentric
// sources with an identical nonsingular transform. Moving a member to the root's
// draw position must not cross an intersecting external sprite. This proves a
// rendering equivalence, not an authored object identity.
inline std::vector<OpaqueOBJAssembly> PlanOpaqueOBJAssemblies(
    const ConnectedOBJSourceInput* s, int count)
{
    count = std::clamp(count, 0, 128);
    auto eligible = [](const ConnectedOBJSourceInput& a) {
        return a.OAM >= 0 && a.MatrixIndex >= 0 && a.MatrixIndex < 32 &&
            a.Mode == 0 && a.Type <= 1 && !a.Mosaic &&
            a.Width > 0 && a.Height > 0 && a.Width <= 64 && a.Height <= 64 &&
            s64(a.Matrix[0]) * a.Matrix[3] != s64(a.Matrix[1]) * a.Matrix[2];
    };
    auto contains = [&](int root, int member) {
        const auto& a=s[root]; const auto& b=s[member];
        if (!eligible(a) || !eligible(b) || a.Priority != b.Priority ||
            a.MatrixIndex != b.MatrixIndex || a.OAM < b.OAM ||
            a.Width < b.Width || a.Height < b.Height ||
            a.BoundWidth < b.BoundWidth || a.BoundHeight < b.BoundHeight ||
            (a.Width-b.Width)%2 || (a.Height-b.Height)%2 ||
            (2*(a.X-b.X)+a.BoundWidth-b.BoundWidth)%1024 ||
            (2*(a.Y-b.Y)+a.BoundHeight-b.BoundHeight)%512)
            return false;
        for (int c=0;c<4;c++) if (a.Matrix[c]!=b.Matrix[c]) return false;
        return true;
    };
    auto intersects = [&](int a, int b) {
        // Include the native OBJ wrap copies, not just the current screen cut.
        for (int dx : {-512,0,512}) for (int dy : {-256,0,256})
            if (s[a].X < s[b].X+dx+s[b].BoundWidth &&
                s[b].X+dx < s[a].X+s[a].BoundWidth &&
                s[a].Y < s[b].Y+dy+s[b].BoundHeight &&
                s[b].Y+dy < s[a].Y+s[a].BoundHeight) return true;
        return false;
    };
    // Try deeper roots first, but do not discard a whole nested stack when
    // one member cannot move that far in OAM order. A foreground subgroup
    // may still be assembled at its own root without crossing the blocker.
    std::vector<int> order;
    for (int i = 0; i < count; ++i) if (eligible(s[i])) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return s[a].OAM > s[b].OAM; });
    std::array<bool, 128> assigned {};
    std::vector<OpaqueOBJAssembly> result;
    for (int root : order)
    {
        if (assigned[root]) continue;
        std::array<bool, 128> member {};
        for (int i : order) member[i] = !assigned[i] && contains(root, i);
        // Back-to-front removal propagates blockers to earlier members in
        // one pass: every possible blocker has a larger OAM index.
        for (int i : order) if (member[i])
            for (int j = 0; j < count; ++j)
                if (!member[j] && s[j].OAM > s[i].OAM && s[j].OAM < s[root].OAM && intersects(i, j))
                {
                    member[i] = false;
                    break;
                }
        OpaqueOBJAssembly group;
        group.Root = root;
        for (auto it = order.rbegin(); it != order.rend(); ++it)
            if (member[*it]) group.Members.push_back(*it);
        if (group.Members.size() < 2) continue;
        for (int i : group.Members) assigned[i] = true;
        result.push_back(std::move(group));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Root < b.Root; });
    return result;
}
}
