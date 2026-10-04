// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GPU3D_TEXTURETYPES_H
#define GPU3D_TEXTURETYPES_H

#include "types.h"

namespace melonDS
{

struct TextureVRAMView
{
    const u8* Texture;
    const u8* TexPal;

    template <typename T>
    T ReadTexture(u32 addr) const
    {
        return *(T*)&Texture[addr & 0x7FFFF];
    }

    template <typename T>
    T ReadTexPal(u32 addr) const
    {
        return *(T*)&TexPal[addr & 0x1FFFF];
    }
};

inline u32 TextureWidth(u32 texparam)
{
    return 8 << ((texparam >> 20) & 0x7);
}

inline u32 TextureHeight(u32 texparam)
{
    return 8 << ((texparam >> 23) & 0x7);
}

struct TextureSamplingBounds
{
    bool Valid = false;
    // Keep full texture dimensions/coordinates; only unused margins are extended.
    bool EdgeExtendMargins = false;
    u16 X0 = 0;
    u16 Y0 = 0;
    u16 X1 = 0;
    u16 Y1 = 0;
};

struct TextureSpriteUVInsetBounds
{
    bool Valid = false;
    u16 U0 = 0;
    u16 V0 = 0;
    u16 U1 = 0;
    u16 V1 = 0;
};

inline bool operator==(const TextureSamplingBounds& lhs, const TextureSamplingBounds& rhs)
{
    return lhs.Valid == rhs.Valid &&
        lhs.EdgeExtendMargins == rhs.EdgeExtendMargins &&
        lhs.X0 == rhs.X0 && lhs.Y0 == rhs.Y0 &&
        lhs.X1 == rhs.X1 && lhs.Y1 == rhs.Y1;
}

inline bool operator!=(const TextureSamplingBounds& lhs, const TextureSamplingBounds& rhs)
{
    return !(lhs == rhs);
}

}

#endif
