// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU3D.h"
#include "GPU3D_TextureTypes.h"
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace melonDS
{
// A presentation-only copy of a proved, uniform-colour cutout layer. The
// original polygon keeps its native samples; its copy draws only the margins.
struct WideTransitionMask
{
    std::array<s32, 4> Bounds; // native 1/16 pixels: left, top, right, bottom
    bool Margin = false;
    std::array<unsigned, 2> UVScale {1,1};
};

struct WideTransitionMaskScene
{
    struct Copy { Polygon Poly; std::array<Vertex, 4> Vertices; };
    std::vector<Polygon*> Polygons;
    std::vector<std::unique_ptr<Copy>> Copies;
    std::unordered_map<const Polygon*, WideTransitionMask> Masks;
};

inline std::array<unsigned, 2> WideMaskUVScale(const Polygon& p)
{
    std::array<unsigned, 2> result {};
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        int maximum = 0;
        for (unsigned i = 0; i < 4; ++i)
            maximum = std::max(maximum, int(p.UnclippedSource.Valid ? p.UnclippedSource.UV[i][axis] : p.Vertices[i]->TexCoords[axis]));
        const unsigned unit = (axis ? TextureHeight(p.TexParam) : TextureWidth(p.TexParam))*16;
        const unsigned scale = maximum/unit;
        if (maximum%unit || !scale || scale > 8 || (scale & (scale-1)) ||
            (scale > 1 && !(p.TexParam & (1u << (16+axis))))) return {};
        result[axis] = scale;
    }
    return result;
}

inline bool WideMaskRectangle(const Polygon& p, std::array<s32, 4>& b, bool textured)
{
    if (!p.WideOrthographic || p.Degenerate || p.Type || p.IsShadow || p.IsShadowMask ||
        p.NumVertices != 4 || (p.Attr & 0x8030) || ((p.Attr >> 16) & 31) != 31)
        return false;
    const auto& first = *p.Vertices[0];
    if (first.Position[3] <= 0) return false;
    b = {32767, 32767, -32768, -32768};
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto& v = *p.Vertices[i];
        if (!v.NativeSourceValid || v.Position[2] != first.Position[2] ||
            v.Position[3] != first.Position[3] || p.FinalZ[i] != p.FinalZ[0] || p.FinalW[i] != p.FinalW[0])
            return false;
        for (unsigned c = 0; c < 3; ++c)
            if (v.FinalColor[c] != first.FinalColor[c]) return false;
        if (v.Clipped && !(textured && p.UnclippedSource.Valid)) return false;
        const auto* xy = textured && p.UnclippedSource.Valid ? p.UnclippedSource.Position[i] : v.NativeSourcePosition;
        b[0] = std::min(b[0], xy[0]); b[1] = std::min(b[1], xy[1]);
        b[2] = std::max(b[2], xy[0]); b[3] = std::max(b[3], xy[1]);
    }
    if (b[0] < -32768 || b[1] < -32768 || b[2] > 32767 || b[3] > 32767 ||
        b[0] >= b[2] || b[1] >= b[3]) return false;
    unsigned corners = 0;
    const auto uvScale = textured ? WideMaskUVScale(p) : std::array<unsigned,2>{1,1};
    if (!uvScale[0] || !uvScale[1]) return false;
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto* xy = textured && p.UnclippedSource.Valid ? p.UnclippedSource.Position[i] : p.Vertices[i]->NativeSourcePosition;
        const auto* next = textured && p.UnclippedSource.Valid ? p.UnclippedSource.Position[(i+1)%4] : p.Vertices[(i+1)%4]->NativeSourcePosition;
        if ((xy[0] != b[0] && xy[0] != b[2]) || (xy[1] != b[1] && xy[1] != b[3]) ||
            (xy[0] != next[0] && xy[1] != next[1])) return false;
        corners |= 1u << ((xy[0] == b[2] ? 1 : 0) + (xy[1] == b[3] ? 2 : 0));
        if (textured)
        {
            const auto* uv = p.UnclippedSource.Valid ? p.UnclippedSource.UV[i] : p.Vertices[i]->TexCoords;
            if (uv[0] != (xy[0] == b[0] ? 0 : int(TextureWidth(p.TexParam)*uvScale[0])*16) ||
                uv[1] != (xy[1] == b[1] ? 0 : int(TextureHeight(p.TexParam)*uvScale[1])*16)) return false;
        }
    }
    return corners == 15;
}

inline bool WideMaskTexture(const std::vector<u32>& pixels, unsigned width, unsigned height, u32& colour)
{
    if (width < 2 || height < 2 || pixels.size() != size_t(width)*height) return false;
    colour = pixels.front() & 0xFFFFFF;
    for (size_t corner : {size_t(0), size_t(width-1), size_t(width)*(height-1), pixels.size()-1})
        if (pixels[corner] != (colour | 0xFF000000)) return false;
    bool opening = false;
    for (u32 pixel : pixels)
    {
        if (!(pixel >> 24)) opening = true;
        else if ((pixel & 0xFFFFFF) != colour) return false;
    }
    return opening;
}

// Prove that the textured rectangle and compatible solid fillers cover the
// native viewport. This accepts missing fillers only where the texture has
// already crossed that boundary; no game IDs, timing, or framebuffer colours.
inline bool WideMaskFrameCovered(const Polygon& mask, const std::array<s32, 4>& bounds,
    u32 colour, Polygon* const* polygons, unsigned count)
{
    std::vector<std::array<s32, 4>> rectangles {bounds};
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& p = *polygons[i];
        if (&p == &mask || ((p.TexParam >> 26) & 7) || p.Translucent || p.Attr != mask.Attr ||
            p.WBuffer != mask.WBuffer || p.FinalZ[0] != mask.FinalZ[0] || p.FinalW[0] != mask.FinalW[0]) continue;
        std::array<s32, 4> b;
        if (!WideMaskRectangle(p, b, false)) continue;
        bool sameColour = true;
        for (unsigned c = 0; c < 3; ++c)
        {
            const int expected = int((colour >> (8*c)) & 255) * (mask.Vertices[0]->FinalColor[c] >> 1) / 255;
            sameColour &= (p.Vertices[0]->FinalColor[c] >> 1) == expected;
        }
        if (sameColour) rectangles.push_back(b);
        if (rectangles.size() > 9) return false; // ambiguous/bounded work
    }
    std::vector<s32> xs {0, 4096}, ys {0, 3072};
    for (const auto& r : rectangles)
    {
        xs.push_back(std::clamp(r[0], 0, 4096)); xs.push_back(std::clamp(r[2], 0, 4096));
        ys.push_back(std::clamp(r[1], 0, 3072)); ys.push_back(std::clamp(r[3], 0, 3072));
    }
    std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end());
    for (size_t x = 1; x < xs.size(); ++x)
        for (size_t y = 1; y < ys.size(); ++y)
        {
            if (xs[x] == xs[x-1] || ys[y] == ys[y-1]) continue;
            bool covered = false;
            for (const auto& r : rectangles)
                covered |= r[0] <= xs[x-1] && r[2] >= xs[x] && r[1] <= ys[y-1] && r[3] >= ys[y];
            if (!covered) return false;
        }
    return true;
}

template<typename Decode>
WideTransitionMaskScene BuildWideTransitionMaskScene(Polygon* const* polygons, unsigned count,
    bool enabled, int width, int height, Decode decode)
{
    WideTransitionMaskScene scene;
    scene.Polygons.assign(polygons, polygons+count);
    if (!enabled || count >= 2048) return scene;
    unsigned budget = 256*256;
    std::vector<u32> pixels;
    // Fail closed on ambiguous multiple cutout layers.
    const Polygon* accepted = nullptr;
    WideTransitionMask acceptedMask {};
    for (unsigned i = 0; i < count; ++i)
    {
        auto& p = *polygons[i];
        const unsigned format = (p.TexParam >> 26) & 7;
        if (!p.Translucent || !format || format == 5) continue;
        std::array<s32, 4> b;
        if (!WideMaskRectangle(p, b, true) || b[0] >= 4096 || b[1] >= 3072 || b[2] <= 0 || b[3] <= 0) continue;
        const unsigned w = TextureWidth(p.TexParam), h = TextureHeight(p.TexParam);
        const auto scale = WideMaskUVScale(p);
        const unsigned width = w*scale[0], height = h*scale[1];
        if (width*height > budget) return scene;
        budget -= width*height;
        u32 colour;
        if (!decode(p, pixels) || pixels.size() != size_t(w)*h) continue;
        if (scale[0] != 1 || scale[1] != 1)
        {
            const auto source = pixels;
            pixels.resize(width*height);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                {
                    unsigned sx = x%w, sy = y%h;
                    if ((p.TexParam & (1u<<18)) && ((x/w)&1)) sx = w-1-sx;
                    if ((p.TexParam & (1u<<19)) && ((y/h)&1)) sy = h-1-sy;
                    pixels[y*width+x] = source[sy*w+sx];
                }
        }
        if (!WideMaskTexture(pixels, width, height, colour) ||
            !WideMaskFrameCovered(p, b, colour, polygons, count)) continue;
        if (accepted) return scene;
        accepted = &p;
        acceptedMask = {b, false, scale};
    }
    if (!accepted) return scene;
    auto copy = std::make_unique<WideTransitionMaskScene::Copy>();
    copy->Poly = *accepted;
    copy->Poly.UnclippedSource.Valid = false;
    copy->Poly.VTop = 0; copy->Poly.VBottom = 2;
    for (unsigned i = 0; i < 4; ++i)
    {
        auto& v = copy->Vertices[i];
        v = *accepted->Vertices[i];
        const auto* uv = accepted->UnclippedSource.Valid ? accepted->UnclippedSource.UV[i] : v.TexCoords;
        const bool right = uv[0] != 0, bottom = uv[1] != 0;
        v.HiresPosition[0] = right ? 4096 : 0;
        v.HiresPosition[1] = bottom ? 3072 : 0;
        v.FinalPosition[0] = right ? 256 : 0;
        v.FinalPosition[1] = bottom ? 192 : 0;
        v.NativeSourcePosition[0] = (right ? 4096 : 0) + (right ? width-256 : 256-width)*8;
        v.NativeSourcePosition[1] = (bottom ? 3072 : 0) + (bottom ? height-192 : 192-height)*8;
        v.Clipped = false;
        copy->Poly.Vertices[i] = &v;
        copy->Poly.FinalZ[i] = accepted->FinalZ[0]; copy->Poly.FinalW[i] = accepted->FinalW[0];
    }
    copy->Poly.VTop = copy->Poly.VBottom = 0;
    for (unsigned i = 1; i < 4; ++i)
    {
        if (copy->Vertices[i].FinalPosition[1] < copy->Vertices[copy->Poly.VTop].FinalPosition[1]) copy->Poly.VTop = i;
        if (copy->Vertices[i].FinalPosition[1] > copy->Vertices[copy->Poly.VBottom].FinalPosition[1]) copy->Poly.VBottom = i;
    }
    scene.Masks.emplace(accepted, acceptedMask);
    acceptedMask.Margin = true;
    scene.Masks.emplace(&copy->Poly, acceptedMask);
    const auto at = std::find(scene.Polygons.begin(), scene.Polygons.end(), accepted);
    scene.Polygons.insert(at+1, &copy->Poly);
    scene.Copies.push_back(std::move(copy));
    return scene;
}
}
