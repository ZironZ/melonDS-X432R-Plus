/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

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
    // Source is still scaled as a full texture; only unused edge margins are extended.
    bool EdgeExtendMargins = false;
    u16 X0 = 0;
    u16 Y0 = 0;
    u16 X1 = 0;
    u16 Y1 = 0;
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
