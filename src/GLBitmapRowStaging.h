// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.h"

namespace melonDS
{
struct BitmapRowSampling
{
    u32 Type, Width, Height, MapOffset;
    s32 X, Y, A, B, C, D;
    bool Mosaic;
};

// A bounded raw-row representation. No approximation of affine sampling:
// each visible pixel samples one texel of this row at the native pixel center.
// Other sampling patterns retain the ordinary renderer.
inline int BitmapRowSourceAddress(const BitmapRowSampling& in, u32 byteSize)
{
    if ((in.Type != 4 && in.Type != 5) || in.Width != 256 || in.Height != 256 ||
        in.X != 0 || in.Y < 0 || in.Y >= 256 * 256 ||
        in.A != 256 || in.B != 0 || in.C != 0 || in.D < 0 || in.D > 1 || in.Mosaic)
        return -1;
    const u32 stride = in.Type == 5 ? 512 : 256;
    const u64 start = static_cast<u64>(in.MapOffset) + (in.Y >> 8) * stride;
    if (start + stride > byteSize) return -1;
    return static_cast<int>(start);
}
}
