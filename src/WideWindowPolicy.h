// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.h"
#include "WideMelon.h"
#include <algorithm>

namespace melonDS
{
// Extra presentation pixels continue the rectangular-window region at the
// nearest native edge. The native 256-pixel image and capture VRAM are unchanged.
// Scanline WinMask/WinPos already encode vertical activity and wrapped windows,
// including the horizontal latch carried from the previous row.
// Packed for WidePresentation: valid, visible, effect[2], factor[5], backdrop[16],
// 3D/backdrop blend, backdrop brightness mode[2] for zero-alpha 3D samples,
// and optional narrow-border presentation repair.
constexpr u32 WideEdgeVisible = 3;
constexpr u32 WideBlend3DBackdrop = 1u << 25;
constexpr u32 WideNarrowBorder = 1u << 28;

inline bool IsWidePolicyFullyDark(u32 policy)
{
    // A full BLDCNT darken is independent of both the scene and backdrop color.
    // Material-alpha blending takes precedence, so it is not a black proof.
    return (policy & 1) && !(policy & WideBlend3DBackdrop) &&
        ((policy >> 2) & 3) == 3 && ((policy >> 4) & 31) == 16;
}

inline bool ShouldFillWideNarrowBorder(u32 current, u32 captured, bool usesCapture)
{
    return (current & 3) == WideEdgeVisible &&
        (!usesCapture || (captured & 3) == WideEdgeVisible) &&
        ((current | (usesCapture ? captured : 0)) & WideNarrowBorder);
}

// Optional presentation heuristic, not a hardware rule. Recognize only a
// single active, nonwrapped rectangle with one excluded column on each side.
// Leave vertical clipping, other rectangles and actual OBJ windows alone.
template <typename Row>
bool CanExpandWideNarrowBorder(const Row& row, bool objWindowEnabled)
{
    if (!WideMelon::ExpandNarrowBorders || !WideMelon::ExtendWindows ||
        WideMelon::Width() <= 256 || WideMelon::Vertical()) return false;
    if (!(row.OBJWindowEnabled & 2) &&
        (objWindowEnabled || ((row.WinRegs >> 8) & 255) != 255)) return false;
    for (int window = 0; window < 2; ++window)
    {
        if (row.WinMask != (2u << (window * 3)) ||
            row.WinPos[window * 2] != 1 || row.WinPos[window * 2 + 1] != 255) continue;
        const u32 inside = (row.WinRegs >> (24 - window * 8)) & 0x3F;
        const u32 outside = row.WinRegs & 0x3F;
        // Only relax an exclusion, never suppress a layer/effect from outside.
        if ((inside & outside) == outside && inside != outside) return true;
    }
    return false;
}

// Cache only geometric absence proofs when OAM changes. Include disabled and
// vertically offscreen masks, and the complete affine double-size bounds.
// Bits match the scanline OBJWindowEnabled field: no masks, clear left edge,
// clear right edge. Texture transparency is deliberately not part of the proof.
inline u32 WideOBJWindowProof(const u16* oam)
{
    u32 proof = 2 | 4 | 8;
    constexpr int widths[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    for (int i = 0; i < 128; ++i)
    {
        const u16 attr0 = oam[i * 4], attr1 = oam[i * 4 + 1];
        if (((attr0 >> 10) & 3) != 2) continue;
        proof &= ~2u;
        const int shape = attr0 >> 14;
        if (shape == 3) return 0; // Undefined shape: retain the conservative fallback.
        const int x = (attr1 & 511) >= 256 ? (attr1 & 511) - 512 : attr1 & 511;
        int width = widths[shape][attr1 >> 14];
        if (((attr0 >> 8) & 3) == 3) width *= 2;
        if (x <= 0) proof &= ~4u;
        if (x + width > 255) proof &= ~8u;
    }
    return proof;
}

// The rotated counterpart is a rectangle excluding just rows 0 and 191.
// Prove its bounds from the recorded scanlines, including vertical activity,
// rather than using end-of-frame registers that may have changed mid-frame.
template <typename Row>
bool CanExpandVerticalNarrowBorder(const Row* rows)
{
    if (!WideMelon::ExpandNarrowBorders || !WideMelon::ExtendWindows ||
        !WideMelon::Vertical()) return false;
    const auto& inside = rows[1];
    const int window = inside.WinMask == 2 ? 0 : inside.WinMask == 16 ? 1 : -1;
    if (window < 0 || inside.WinPos[window * 2] >= inside.WinPos[window * 2 + 1]) return false;
    const u32 permissions = (inside.WinRegs >> (24 - window * 8)) & 0x3F;
    const u32 outside = inside.WinRegs & 0x3F;
    if (permissions == outside || (permissions & outside) != outside) return false;
    for (int y = 0; y < 192; ++y)
    {
        const auto& row = rows[y];
        if (row.WinRegs != inside.WinRegs ||
            (!(row.OBJWindowEnabled & 2) &&
             (row.OBJWindowEnabled || ((row.WinRegs >> 8) & 255) != 255))) return false;
        if (y == 0 || y == 191)
        {
            if (row.WinMask != 0) return false;
        }
        else
        {
            if (row.WinMask != inside.WinMask) return false;
            for (int i = 0; i < 4; ++i)
                if (row.WinPos[i] != inside.WinPos[i]) return false;
        }
    }
    return true;
}

template <typename Row>
u32 WideWindowPermissions(const Row& row, int x, bool objWindowEnabled)
{
    auto inside = [&](int window)
    {
        const int region = x < row.WinPos[window * 2] ? 0 :
                           x < row.WinPos[window * 2 + 1] ? 1 : 2;
        return (row.WinMask & (1u << (window * 3 + region))) != 0;
    };
    u32 permissions = row.WinRegs;
    if (inside(0)) permissions >>= 24;
    else if (inside(1)) permissions >>= 16;
    else if (!(row.OBJWindowEnabled & (2u | (x == 0 ? 4u : x == 255 ? 8u : 0u))) &&
             (objWindowEnabled || ((row.WinRegs >> 8) & 255) != 255))
        return 0x100; // No proof of OBJ-window coverage outside the native image.

    return permissions & 255;
}

template <typename Row>
u32 MakeWideWindowPolicy(const Row& row, int layer, int x, bool objWindowEnabled, bool backdropOnly = false,
                         int coverLayer = -1, u32 coverColor = 0, int permissionsOverride = -1)
{
    // With extensions disabled, keep only the original unwindowed route.
    // Ordinary brightness effects still apply to that route.
    if (!WideMelon::ExtendWindows && (row.WinRegs != 0xFFFFFFFFu || objWindowEnabled)) return 0;
    const bool narrowBorder = (x == 0 || x == 255) && CanExpandWideNarrowBorder(row, objWindowEnabled);
    const u32 permissions = permissionsOverride >= 0 ? static_cast<u32>(permissionsOverride) :
        WideWindowPermissions(row, narrowBorder ? (x == 0 ? 1 : 254) : x, objWindowEnabled);
    if (permissions == 0x100) return 0;

    const bool enabled = layer == 4 ? row.EnableOBJ != 0 :
        row.BGPrio[layer] != 0xFFFFFFFFu && (layer != 0 || row.Enable3D);
    bool visible = !backdropOnly && enabled && (permissions & (1u << layer));
    int target = visible ? layer : 5;
    u32 color = row.BackColor;
    // A proven opaque covering BG is the visible operand, even when the
    // widened source is still enabled underneath it. Keep its effect target.
    if (!backdropOnly && layer < 4 && coverLayer >= 0 && coverLayer < 4 &&
        row.BGPrio[coverLayer] != 0xFFFFFFFFu && (permissions & (1u << coverLayer)) &&
        (!visible || row.BGPrio[coverLayer] < row.BGPrio[layer] ||
         (row.BGPrio[coverLayer] == row.BGPrio[layer] && coverLayer < layer)))
    {
        visible = false;
        target = coverLayer;
        color = coverColor;
    }
    u32 effect = (permissions & 0x20) && (row.BlendCnt & (1u << target)) ?
        (row.BlendCnt >> 6) & 3 : 0;
    u32 extra = 0;
    // A lowest-priority 3D layer can only have backdrop beneath it. Other
    // layers at the same priority win over BG0 only for OBJ, not higher BG IDs.
    bool backdropBelow3D = visible && layer == 0 && row.Enable3D &&
        (row.BlendCnt & (1u << 13)) && (!row.EnableOBJ || row.BGPrio[0] == 3);
    for (int bg = 1; bg < 4 && backdropBelow3D; ++bg)
        if ((permissions & (1u << bg)) && row.BGPrio[bg] != 0xFFFFFFFFu &&
            row.BGPrio[bg] >= row.BGPrio[0]) backdropBelow3D = false;
    if (backdropBelow3D)
    {
        extra = WideBlend3DBackdrop;
        const u32 backdropEffect = (permissions & 0x20) && (row.BlendCnt & 0x20) ?
            (row.BlendCnt >> 6) & 3 : 0;
        if (backdropEffect >= 2) extra |= backdropEffect << 26;
        effect = 0; // DS 3D material-alpha blending takes precedence over BLDCNT effects.
    }
    if (effect == 1) return 0; // A second blend operand has no widened proof.
    return 1u | (visible ? 2u : 0u) | (effect << 2) |
        (std::min<u32>(row.BlendCoef[2], 16) << 4) | ((color & 0xFFFF) << 9) | extra |
        (narrowBorder && visible ? WideNarrowBorder : 0u);
}

template <typename Row>
u32 MakeWideWindowEdgePolicy(const Row& row, int layer, bool right, bool objWindowEnabled)
{
    return MakeWideWindowPolicy(row, layer, right ? 255 : 0, objWindowEnabled);
}

template <typename Row>
u32 MakeVerticalWindowPolicy(const Row* rows, int layer, int x, int side,
                            bool expandBorder, bool backdropOnly = false)
{
    const auto& row = rows[side ? 191 : 0];
    const u32 original = WideWindowPermissions(row, x, row.OBJWindowEnabled != 0);
    const u32 permissions = expandBorder ? WideWindowPermissions(rows[1], x, rows[1].OBJWindowEnabled != 0) : original;
    u32 policy = MakeWideWindowPolicy(row, layer, x, row.OBJWindowEnabled != 0, backdropOnly, -1, 0, permissions);
    if (expandBorder && (policy & 3) == WideEdgeVisible &&
        original != permissions) policy |= WideNarrowBorder;
    return policy;
}
}
