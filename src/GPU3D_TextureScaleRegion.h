// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GPU3D_TEXTURESCALEREGION_H
#define GPU3D_TEXTURESCALEREGION_H

#include <algorithm>
#include "GPU3D_TextureTypes.h"

namespace melonDS
{

struct TextureScaleDispatch
{
    int X, Y, GroupsX, GroupsY;
};

// A work region, not a cropped texture or a change to its sampling coordinates.
// Only the bounds-keyed, unfiltered edge-extension variant may use partial work.
struct TextureScaleRegion
{
    int Width, Height;
    int X0, Y0, X1, Y1;

    bool Restricted() const
    {
        return X0 != 0 || Y0 != 0 || X1 != Width || Y1 != Height;
    }

    TextureScaleDispatch Dispatch(int width, int height) const
    {
        // Keep the original 8x8 workgroup grid, including in NNEDI3's 2x/4x
        // intermediate planes. CuNNy's packed feature planes dispatch in
        // native pixel coordinates, not in packed texture coordinates.
        const int x0 = (X0 * width / Width) / 8 * 8;
        const int y0 = (Y0 * height / Height) / 8 * 8;
        const int x1 = (X1 * width + Width - 1) / Width;
        const int y1 = (Y1 * height + Height - 1) / Height;
        return {x0, y0, (x1 - x0 + 7) / 8, (y1 - y0 + 7) / 8};
    }
};

inline TextureScaleRegion MakeTextureScaleRegion(int width, int height,
                                                 const TextureSamplingBounds* bounds)
{
    TextureScaleRegion result {width, height, 0, 0, width, height};
    if (!bounds || !bounds->Valid || !bounds->EdgeExtendMargins ||
        bounds->X0 >= bounds->X1 || bounds->Y0 >= bounds->Y1 ||
        bounds->X1 > width || bounds->Y1 > height)
        return result;

    // NNEDI3's four passes have at most 4/2-tap source offsets per axis,
    // with later offsets measured at 2x or 4x. Including the final spline
    // phase correction, their native dependency radius is below 16 pixels.
    // CuNNy4x32 has six radius-one stages followed by spline resampling.
    // 32 leaves an additional guard around the requested sampling bounds;
    // do not enable this for other models or filtered/mipmap consumers on
    // the assumption that they have the same dependency footprint.
    constexpr int halo = 32;
    result.X0 = std::max(0, int(bounds->X0) - halo);
    result.Y0 = std::max(0, int(bounds->Y0) - halo);
    result.X1 = std::min(width, int(bounds->X1) + halo);
    result.Y1 = std::min(height, int(bounds->Y1) + halo);
    return result;
}

}
#endif
