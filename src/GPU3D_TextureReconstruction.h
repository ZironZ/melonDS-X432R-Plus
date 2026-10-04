// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU3D.h"
#include "GPU3D_TextureTypes.h"
#include "WideMelon.h"
#include <algorithm>
#include <array>
#include <vector>

namespace melonDS
{
// Source-domain contract: adjacent, non-overlapping quads on a common pixel grid
// with an axis-aligned native-sized UV footprint and matching layout/material/plane.
// These are source recipes, never flattened scene layers or emulated VRAM.
struct TextureReconstructionPiece
{
    int PolygonIndex, X, Y, Width, Height, U, V;
    u32 TexParam;
    bool FlipX = false, FlipY = false;
    int PhaseX = 0, PhaseY = 0; // common screen translation, in sixteenths
};
struct TextureReconstructionGroup
{
    struct ContextSpan
    {
        int Donor, Destination, Source, Count;
    };
    u32 TexParam = 0, Palette = 0;
    int X = 0, Y = 0, Width = 0, Height = 0;
    int PhaseX = 0, PhaseY = 0;
    static constexpr int Border = 2;
    std::vector<TextureReconstructionPiece> Pieces;
    std::vector<TextureReconstructionPiece> ContextPieces;
    std::vector<bool> ContextAdjacent;
    // Geometry-only work prepared with the cached plan. No decoded pixels or
    // pointers into live polygon/vertex RAM are retained here.
    std::vector<int> SourceOrder;
    std::vector<u8> Domain;
    std::vector<ContextSpan> ContextSpans;
    bool HasFractionalAlpha() const { return ((TexParam >> 26) & 7) == 1 || ((TexParam >> 26) & 7) == 6; }
};
struct TextureReconstructionPlan
{
    static constexpr size_t MaxGroups = 64;
    static constexpr size_t MaxFractionalGroups = 16;
    std::vector<TextureReconstructionGroup> Groups;
    std::vector<int> Membership;
};

// Orient native artwork in screen space before reconstruction. Reversing the
// copied texels keeps flipped members on the same sampling grid as neighbors.
inline void CopyTextureReconstructionPiece(const TextureReconstructionGroup& group,
    const TextureReconstructionPiece& piece, int atlasWidth, const u32* atlas,
    u32* source, u8* domain)
{
    for (int y = 0; y < piece.Height; y++)
    {
        const int sy = piece.V + (piece.FlipY ? piece.Height-1-y : y);
        const auto* row = atlas + sy*atlasWidth + piece.U;
        const int dst = (piece.Y-group.Y+group.Border+y)*group.Width + piece.X-group.X+group.Border;
        if (piece.FlipX) std::reverse_copy(row, row+piece.Width, source+dst);
        else std::copy_n(row, piece.Width, source+dst);
        if (domain) std::fill_n(domain+dst, piece.Width, 1);
    }
}

// Outside the geometry's source domain is unknown context, not transparent
// artwork. Extend that context without changing any authored texel, including
// authored transparent texels. Rasterization still clips to the original quads.
inline void ExtendTextureReconstructionDomain(int width, int height,
    std::vector<u32>& source, std::vector<u8> domain)
{
    std::vector<int> queue;
    queue.reserve(source.size());
    for (int i = 0; i < int(domain.size()); i++) if (domain[i]) queue.push_back(i);
    for (size_t head = 0; head < queue.size(); head++)
    {
        int i = queue[head], x = i % width, y = i / width;
        auto visit = [&](int next)
        {
            if (domain[next]) return;
            domain[next] = 1;
            source[next] = source[i];
            queue.push_back(next);
        };
        if (x > 0) visit(i - 1);
        if (x + 1 < width) visit(i + 1);
        if (y > 0) visit(i - width);
        if (y + 1 < height) visit(i + width);
    }
}

inline int TextureReconstructionPhase(int position)
{
    return (position % 16 + 16) % 16;
}

inline bool DescribeTextureReconstructionPiece(const Polygon& p, int index,
                                               TextureReconstructionPiece& piece, bool allowFractional = false,
                                               bool nativeSourceCoordinates = false)
{
    if (WideMelon::Enabled() && !nativeSourceCoordinates)
    {
        if (p.NumVertices != 4) return false;
        Polygon original = p;
        std::array<Vertex,4> vertices;
        for (int i = 0; i < 4; i++)
        {
            if (!p.Vertices[i]->NativeSourceValid) return false;
            vertices[i] = *p.Vertices[i];
            for (int axis = 0; axis < 2; axis++)
            {
                const int position = vertices[i].NativeSourcePosition[axis];
                vertices[i].HiresPosition[axis] = position;
                vertices[i].FinalPosition[axis] = (position-TextureReconstructionPhase(position))/16;
            }
            original.Vertices[i] = &vertices[i];
        }
        return DescribeTextureReconstructionPiece(original, index, piece, allowFractional, true);
    }
    const u32 format = (p.TexParam >> 26) & 7;
    // Fractional indexed artwork is independently opt-in. Capture/direct
    // color and compressed formats remain ineligible.
    const bool fractional = format == 1 || format == 6;
    if (!allowFractional && fractional) return false;
    if ((p.Type != 0 && !(p.Type == 1 && fractional)) || p.NumVertices != 4 || p.Degenerate || p.IsShadow || p.IsShadowMask ||
        (format < 1 || format > 6 || format == 5) || (!fractional && ((p.TexParam >> 16) & 15) != 0) ||
        ((p.Attr >> 16) & 31) != 31 || (p.Attr & 0x30) != 0)
        return false;
    if (p.UnclippedSource.Valid)
    {
        Polygon original = p;
        original.UnclippedSource.Valid = false;
        std::array<Vertex,4> vertices;
        for (int i = 0; i < 4; i++)
        {
            vertices[i] = *p.Vertices[i];
            vertices[i].Clipped = false;
            for (int axis = 0; axis < 2; axis++)
            {
                vertices[i].HiresPosition[axis] = p.UnclippedSource.Position[i][axis];
                const int position = p.UnclippedSource.Position[i][axis];
                vertices[i].FinalPosition[axis] = (position - TextureReconstructionPhase(position)) / 16;
                vertices[i].TexCoords[axis] = p.UnclippedSource.UV[i][axis];
            }
            original.Vertices[i] = &vertices[i];
        }
        if (!DescribeTextureReconstructionPiece(original, index, piece, allowFractional, nativeSourceCoordinates)) return false;
        // Wrapped viewport coordinates are not a clipped subset of this source.
        for (int i = 0; i < 4; i++)
        {
            const auto& v = *p.Vertices[i];
            if (v.HiresPosition[0] < piece.X*16+piece.PhaseX || v.HiresPosition[0] > (piece.X+piece.Width)*16+piece.PhaseX ||
                v.HiresPosition[1] < piece.Y*16+piece.PhaseY || v.HiresPosition[1] > (piece.Y+piece.Height)*16+piece.PhaseY)
                return false;
        }
        return true;
    }
    int x0 = p.Vertices[0]->HiresPosition[0], x1 = x0;
    int y0 = p.Vertices[0]->HiresPosition[1], y1 = y0;
    int u0 = p.Vertices[0]->TexCoords[0], u1 = u0;
    int v0 = p.Vertices[0]->TexCoords[1], v1 = v0;
    const int phaseX = TextureReconstructionPhase(x0), phaseY = TextureReconstructionPhase(y0);
    for (int i = 0; i < 4; i++)
    {
        const Vertex& v = *p.Vertices[i];
        int x = v.HiresPosition[0], y = v.HiresPosition[1];
        // A common fractional translation does not change the native-sized
        // source grid. Never snap vertices or admit independently warped edges.
        if (v.Clipped || TextureReconstructionPhase(x) != phaseX || TextureReconstructionPhase(y) != phaseY ||
            x-phaseX != v.FinalPosition[0] * 16 || y-phaseY != v.FinalPosition[1] * 16 ||
            (v.TexCoords[0] % 16) || (v.TexCoords[1] % 16) ||
            p.FinalZ[i] != p.FinalZ[0] || p.FinalW[i] != p.FinalW[0] || p.FinalW[i] <= 0)
            return false;
        for (int c = 0; c < 3; c++)
            if (v.FinalColor[c] != p.Vertices[0]->FinalColor[c]) return false;
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        y0 = std::min(y0, y); y1 = std::max(y1, y);
        u0 = std::min(u0, int(v.TexCoords[0])); u1 = std::max(u1, int(v.TexCoords[0]));
        v0 = std::min(v0, int(v.TexCoords[1])); v1 = std::max(v1, int(v.TexCoords[1]));
    }
    if (x0 == x1 || y0 == y1) return false;
    // Some native-sized quads use inclusive last-texel endpoints. Admit that
    // exact footprint too, then evaluate it on the assembled source grid.
    // This is not a tolerance for arbitrary scaling or fractional UVs.
    if ((u1-u0 != x1-x0 && (x1-x0 <= 16 || u1-u0 != x1-x0-16)) ||
        (v1-v0 != y1-y0 && (y1-y0 <= 16 || v1-v0 != y1-y0-16))) return false;
    const bool flipX = (p.Vertices[0]->HiresPosition[0] == x0) != (p.Vertices[0]->TexCoords[0] == u0);
    const bool flipY = (p.Vertices[0]->HiresPosition[1] == y0) != (p.Vertices[0]->TexCoords[1] == v0);
    unsigned corners = 0;
    for (int i = 0; i < 4; i++)
    {
        int x = p.Vertices[i]->HiresPosition[0], y = p.Vertices[i]->HiresPosition[1];
        if ((x != x0 && x != x1) || (y != y0 && y != y1)) return false;
        if (p.Vertices[i]->TexCoords[0] != ((x == x0) != flipX ? u0 : u1) ||
            p.Vertices[i]->TexCoords[1] != ((y == y0) != flipY ? v0 : v1)) return false;
        int corner = (x == x1 ? 1 : 0) | (y == y1 ? 2 : 0);
        if (corners & (1u << corner)) return false;
        corners |= 1u << corner;
        const auto* next = p.Vertices[(i + 1) % 4];
        if (x != next->HiresPosition[0] && y != next->HiresPosition[1]) return false;
    }
    piece = {index, (x0-phaseX)/16, (y0-phaseY)/16, (x1-x0)/16, (y1-y0)/16, u0/16, v0/16, p.TexParam, flipX, flipY, phaseX, phaseY};
    if (fractional)
    {
        // A reversed full-period sprite commonly runs from the last texel to
        // the exclusive -1 endpoint. Its drawn samples are width-1 through 0.
        if (piece.FlipX && piece.U == -1 && piece.Width == int(TextureWidth(p.TexParam)) &&
            u1 == (int(TextureWidth(p.TexParam))-1)*16 && (p.TexParam & (1u<<16))) piece.U = 0;
        if (piece.FlipY && piece.V == -1 && piece.Height == int(TextureHeight(p.TexParam)) &&
            v1 == (int(TextureHeight(p.TexParam))-1)*16 && (p.TexParam & (1u<<17))) piece.V = 0;
        if ((p.TexParam & (1u<<16)) && (piece.U != 0 || piece.Width != int(TextureWidth(p.TexParam)))) return false;
        if ((p.TexParam & (1u<<17)) && (piece.V != 0 || piece.Height != int(TextureHeight(p.TexParam)))) return false;
    }
    return piece.U >= 0 && piece.V >= 0 &&
        piece.U + piece.Width <= int(TextureWidth(p.TexParam)) &&
        piece.V + piece.Height <= int(TextureHeight(p.TexParam));
}

inline bool SameTextureReconstructionState(const Polygon& a, const Polygon& b, bool compareDepth = true)
{
    // The source address selects artwork, not a material operation. Each
    // member retains its address for decoding into the common source domain.
    // Fractional footprints validate their dimensions and repeat period
    // separately, allowing upper/lower sprite pieces from different atlases.
    const u32 format = (a.TexParam >> 26) & 7;
    const u32 materialMask = (format == 1 || format == 6) ? 0xFC000000u : 0xFFFF0000u;
    return ((a.TexParam ^ b.TexParam) & materialMask) == 0 &&
        a.TexPalette == b.TexPalette && a.Attr == b.Attr &&
        a.WBuffer == b.WBuffer && a.FacingView == b.FacingView &&
        (!compareDepth || a.FinalZ[0] == b.FinalZ[0]) && a.FinalW[0] == b.FinalW[0] &&
        std::equal(a.Vertices[0]->FinalColor, a.Vertices[0]->FinalColor + 3, b.Vertices[0]->FinalColor);
}

inline TextureReconstructionPlan BuildTextureReconstructionLayerPlan(const std::vector<Polygon*>& polygons, bool fractionalOnly)
{
    TextureReconstructionPlan plan;
    plan.Membership.assign(polygons.size(), -1);
    std::vector<TextureReconstructionPiece> pieces;
    for (size_t i = 0; i < polygons.size(); i++)
    {
        TextureReconstructionPiece piece;
        if (!DescribeTextureReconstructionPiece(*polygons[i], int(i), piece, fractionalOnly)) continue;
        const u32 format = (piece.TexParam >> 26) & 7;
        if ((format == 1 || format == 6) == fractionalOnly) pieces.push_back(piece);
    }
    // Bound the experimental planner's pair comparisons; rejection uses the
    // unchanged per-texture path, never a partially assembled group.
    if (pieces.size() > 512) return plan;
    std::vector<int> parent(pieces.size());
    std::vector<bool> overlap(pieces.size(), false);
    for (size_t i = 0; i < pieces.size(); i++) parent[i] = int(i);
    auto root = [&](int i) { while (parent[i] != i) i = parent[i]; return i; };
    for (size_t i = 0; i < pieces.size(); i++) for (size_t j = 0; j < i; j++)
    {
        const auto& a = pieces[i]; const auto& b = pieces[j];
        if (a.PhaseX != b.PhaseX || a.PhaseY != b.PhaseY) continue;
        if (!SameTextureReconstructionState(*polygons[a.PolygonIndex], *polygons[b.PolygonIndex])) continue;
        int dx = std::min(a.X+a.Width, b.X+b.Width) - std::max(a.X,b.X);
        int dy = std::min(a.Y+a.Height, b.Y+b.Height) - std::max(a.Y,b.Y);
        if (dx > 0 && dy > 0) { overlap[i] = overlap[j] = true; }
        if ((dx == 0 && dy > 0) || (dy == 0 && dx > 0)) parent[root(int(i))] = root(int(j));
    }
    for (size_t i = 0; i < pieces.size(); i++)
    {
        if (root(int(i)) != int(i)) continue;
        TextureReconstructionGroup group;
        bool valid = true;
        int x1 = pieces[i].X, y1 = pieces[i].Y;
        group.X = x1; group.Y = y1;
        group.PhaseX = pieces[i].PhaseX; group.PhaseY = pieces[i].PhaseY;
        for (size_t j = 0; j < pieces.size(); j++) if (root(int(j)) == int(i))
        {
            const auto& p = pieces[j]; valid &= !overlap[j]; group.Pieces.push_back(p);
            group.X = std::min(group.X,p.X); group.Y = std::min(group.Y,p.Y);
            x1 = std::max(x1,p.X+p.Width); y1 = std::max(y1,p.Y+p.Height);
        }
        group.Width = x1-group.X + 2*group.Border; group.Height = y1-group.Y + 2*group.Border;
        if (!valid || group.Pieces.size() < 2 || group.Width > 512 || group.Height > 512 ||
            group.Width*group.Height > 128*1024 || plan.Groups.size() >= (fractionalOnly ? TextureReconstructionPlan::MaxFractionalGroups : TextureReconstructionPlan::MaxGroups)) continue;
        const Polygon& p = *polygons[pieces[i].PolygonIndex];
        group.TexParam = p.TexParam; group.Palette = p.TexPalette;
        for (const auto& member : group.Pieces) plan.Membership[member.PolygonIndex] = int(plan.Groups.size());
        plan.Groups.push_back(std::move(group));
    }
    return plan;
}

inline TextureReconstructionPlan BuildTextureReconstructionPlan(const std::vector<Polygon*>& polygons,
    bool shareEdgeContext = false, bool includeFractional = false)
{
    auto plan = BuildTextureReconstructionLayerPlan(polygons, false);
    if (shareEdgeContext && !plan.Groups.empty())
    {
        std::vector<TextureReconstructionPiece> pieces;
        for (size_t i = 0; i < polygons.size(); i++)
        {
            TextureReconstructionPiece piece;
            if (DescribeTextureReconstructionPiece(*polygons[i], int(i), piece)) pieces.push_back(piece);
        }
        // Collect possible scaler-context donors, without
        // changing membership, bounds, geometry, or original per-polygon depth.
        for (auto& group : plan.Groups)
        {
            const Polygon& first = *polygons[group.Pieces[0].PolygonIndex];
            for (const auto& donor : pieces)
            {
                if (donor.PhaseX != group.PhaseX || donor.PhaseY != group.PhaseY) continue;
                if (plan.Membership[donor.PolygonIndex] == plan.Membership[group.Pieces[0].PolygonIndex]) continue;
                if (!SameTextureReconstructionState(first, *polygons[donor.PolygonIndex], false)) continue;
                if (donor.X >= group.X+group.Width-group.Border || donor.X+donor.Width <= group.X-group.Border ||
                    donor.Y >= group.Y+group.Height-group.Border || donor.Y+donor.Height <= group.Y-group.Border) continue;
                bool adjacent = false;
                for (const auto& member : group.Pieces)
                {
                    int dx=std::min(member.X+member.Width,donor.X+donor.Width)-std::max(member.X,donor.X);
                    int dy=std::min(member.Y+member.Height,donor.Y+donor.Height)-std::max(member.Y,donor.Y);
                    adjacent |= (dx==0 && dy>0) || (dy==0 && dx>0);
                }
                group.ContextPieces.push_back(donor);
                group.ContextAdjacent.push_back(adjacent);
            }
        }
    }
    // Preserve binary membership and its budget; fractional pieces form a
    // separate plan and never donate cross-depth context.
    if (includeFractional)
    {
        auto additional = BuildTextureReconstructionLayerPlan(polygons, true);
        for (auto& group : additional.Groups)
        {
            for (const auto& piece : group.Pieces) plan.Membership[piece.PolygonIndex] = int(plan.Groups.size());
            plan.Groups.push_back(std::move(group));
        }
    }
    return plan;
}

// Competing donors block a texel even if they are not edge-adjacent. Authored
// transparency is protected. This never chooses a frontmost scene layer.
inline std::vector<int> TextureReconstructionContextOwners(const TextureReconstructionGroup& group,
    const std::vector<u8>& domain)
{
    std::vector<u8> halo(domain.size(), 0);
    for (const auto& member : group.Pieces)
        for (int y = std::max(0, member.Y-group.Y+group.Border-2); y < std::min(group.Height, member.Y-group.Y+group.Border+member.Height+2); y++)
        for (int x = std::max(0, member.X-group.X+group.Border-2); x < std::min(group.Width, member.X-group.X+group.Border+member.Width+2); x++)
            halo[y*group.Width+x] = 1;
    std::vector<int> owner(domain.size(), -1);
    for (int n = 0; n < int(group.ContextPieces.size()); n++)
    {
        const auto& piece = group.ContextPieces[n];
        for (int y = std::max(piece.Y, group.Y-group.Border); y < std::min(piece.Y+piece.Height, group.Y+group.Height-group.Border); y++)
        for (int x = std::max(piece.X, group.X-group.Border); x < std::min(piece.X+piece.Width, group.X+group.Width-group.Border); x++)
        {
            const int i = (y-group.Y+group.Border)*group.Width+x-group.X+group.Border;
            if (!domain[i] && halo[i]) owner[i] = owner[i] == -1 ? n : -2;
        }
    }
    for (auto& n : owner) if (n >= 0 && !group.ContextAdjacent[n]) n = -1;
    return owner;
}

inline void PrepareTextureReconstructionGroup(TextureReconstructionGroup& group)
{
    group.SourceOrder.clear();
    group.ContextSpans.clear();
    group.Domain.assign(group.Width*group.Height, 0);
    for (int n = 0; n < int(group.Pieces.size()); n++)
    {
        group.SourceOrder.push_back(n);
        const auto& piece = group.Pieces[n];
        for (int y = 0; y < piece.Height; y++)
        {
            const int dst = (piece.Y-group.Y+group.Border+y)*group.Width + piece.X-group.X+group.Border;
            std::fill_n(group.Domain.data()+dst, piece.Width, 1);
        }
    }
    std::stable_sort(group.SourceOrder.begin(), group.SourceOrder.end(),
        [&](int a, int b) { return group.Pieces[a].TexParam < group.Pieces[b].TexParam; });
    if (group.ContextPieces.empty()) return;
    const auto owner = TextureReconstructionContextOwners(group, group.Domain);
    // Store horizontal runs instead of retaining/scanning full ownership and
    // halo maps each frame. Donor order and flipped source addressing match
    // the original per-texel copy exactly.
    for (int n = 0; n < int(group.ContextPieces.size()); n++)
    {
        const auto& piece = group.ContextPieces[n];
        const int width = TextureWidth(piece.TexParam);
        for (int y = std::max(piece.Y, group.Y-group.Border); y < std::min(piece.Y+piece.Height, group.Y+group.Height-group.Border); y++)
        {
            const int end = std::min(piece.X+piece.Width, group.X+group.Width-group.Border);
            for (int x = std::max(piece.X, group.X-group.Border); x < end;)
            {
                const int dst = (y-group.Y+group.Border)*group.Width+x-group.X+group.Border;
                if (owner[dst] != n) { x++; continue; }
                int count = 1;
                while (x+count < end && owner[dst+count] == n) count++;
                const int u = piece.U+(piece.FlipX ? piece.Width-1-(x-piece.X) : x-piece.X);
                const int v = piece.V+(piece.FlipY ? piece.Height-1-(y-piece.Y) : y-piece.Y);
                group.ContextSpans.push_back({n, dst, v*width+u, count});
                std::fill_n(group.Domain.data()+dst, count, 1);
                x += count;
            }
        }
    }
}

inline bool SameTextureReconstructionRecipe(const TextureReconstructionGroup& a,
    const TextureReconstructionGroup& b)
{
    if (a.TexParam != b.TexParam || a.Palette != b.Palette || a.X != b.X || a.Y != b.Y ||
        a.PhaseX != b.PhaseX || a.PhaseY != b.PhaseY ||
        a.Width != b.Width || a.Height != b.Height || a.ContextAdjacent != b.ContextAdjacent ||
        a.Pieces.size() != b.Pieces.size() || a.ContextPieces.size() != b.ContextPieces.size()) return false;
    auto samePiece = [](const auto& x, const auto& y)
    {
        return x.PolygonIndex == y.PolygonIndex && x.X == y.X && x.Y == y.Y &&
            x.Width == y.Width && x.Height == y.Height && x.U == y.U && x.V == y.V &&
            x.TexParam == y.TexParam && x.FlipX == y.FlipX && x.FlipY == y.FlipY &&
            x.PhaseX == y.PhaseX && x.PhaseY == y.PhaseY;
    };
    return std::equal(a.Pieces.begin(), a.Pieces.end(), b.Pieces.begin(), samePiece) &&
        std::equal(a.ContextPieces.begin(), a.ContextPieces.end(), b.ContextPieces.begin(), samePiece);
}

// Cache only the last plan. Compare exact, explicitly serialized planner
// inputs, not hashes, struct padding, pointer identities, or scene IDs.
// Texture/palette bytes are deliberately absent: Texcache still decodes the
// current VRAM snapshot and compares assembled pixels on EVERY use.
class TextureReconstructionPlanCache
{
public:
    const TextureReconstructionPlan& Get(const std::vector<Polygon*>& polygons,
        bool shareEdgeContext, bool includeFractional, u32 maxPreparedTexels = 128*1024)
    {
        Pending.clear();
        auto add = [&](u32 value) { Pending.push_back(value); };
        add(WideMelon::Width());
        add(shareEdgeContext); add(includeFractional); add(maxPreparedTexels); add(u32(polygons.size()));
        for (const auto* p : polygons)
        {
            add(p->Type); add(p->NumVertices); add(p->Degenerate);
            add(p->IsShadow); add(p->IsShadowMask); add(p->TexParam);
            add(p->TexPalette); add(p->Attr); add(p->WBuffer); add(p->FacingView);
            if (p->NumVertices != 4) continue;
            for (int i = 0; i < 4; i++)
            {
                const auto& v = *p->Vertices[i];
                add(v.NativeSourceValid);
                if (v.NativeSourceValid)
                    for (auto coordinate : v.NativeSourcePosition) add(coordinate);
                for (int axis = 0; axis < 2; axis++)
                {
                    add(v.HiresPosition[axis]); add(v.FinalPosition[axis]); add(v.TexCoords[axis]);
                }
                for (auto color : v.FinalColor) add(color);
                add(v.Clipped); add(p->FinalZ[i]); add(p->FinalW[i]);
            }
            add(p->UnclippedSource.Valid);
            if (p->UnclippedSource.Valid)
                for (int i = 0; i < 4; i++) for (int axis = 0; axis < 2; axis++)
                {
                    add(p->UnclippedSource.Position[i][axis]); add(p->UnclippedSource.UV[i][axis]);
                }
        }
        if (Pending != Snapshot)
        {
            auto previous = std::move(Plan);
            Plan = BuildTextureReconstructionPlan(polygons, shareEdgeContext, includeFractional);
            // Keep the existing per-product area rejection ahead of expensive
            // preparation, including at high scale factors and source-mip mode.
            for (auto& group : Plan.Groups)
                if (u32(group.Width*group.Height) <= maxPreparedTexels)
                {
                    // Animation may change one part of the plan. Retain the
                    // exact unchanged groups instead of rebuilding every map.
                    auto old = std::find_if(previous.Groups.begin(), previous.Groups.end(),
                        [&](const auto& candidate) {
                            return !candidate.Domain.empty() && SameTextureReconstructionRecipe(group, candidate);
                        });
                    if (old != previous.Groups.end())
                    {
                        group.SourceOrder = std::move(old->SourceOrder);
                        group.Domain = std::move(old->Domain);
                        group.ContextSpans = std::move(old->ContextSpans);
                    }
                    else PrepareTextureReconstructionGroup(group);
                }
            Snapshot.swap(Pending);
        }
        return Plan;
    }
    void Reset()
    {
        Plan = {};
        std::vector<u32>().swap(Snapshot);
        std::vector<u32>().swap(Pending);
    }
private:
    TextureReconstructionPlan Plan;
    std::vector<u32> Snapshot, Pending;
};

inline void RemapTextureReconstructionPolygon(const Polygon& source, const TextureReconstructionGroup& group,
                                               Polygon& copy, std::array<Vertex,4>& vertices)
{
    // Continue one source grid through every member, including clipped pieces.
    // Restarting an inclusive-endpoint UV slope per tile leaves internal seams.
    copy = source;
    for (int i = 0; i < 4; i++)
    {
        vertices[i] = *source.Vertices[i];
        const auto* position = WideMelon::Enabled() && vertices[i].NativeSourceValid
            ? vertices[i].NativeSourcePosition : vertices[i].HiresPosition;
        vertices[i].TexCoords[0] = s16(position[0] - group.X*16 - group.PhaseX + group.Border*16);
        vertices[i].TexCoords[1] = s16(position[1] - group.Y*16 - group.PhaseY + group.Border*16);
        copy.Vertices[i] = &vertices[i];
    }
}
}
