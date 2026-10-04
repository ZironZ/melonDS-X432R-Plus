// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.h"
#include <bitset>

namespace melonDS
{
// Text BGs only, with the ordinary palette. Validate every referenced tile,
// not just a few matching edge pixels. Transparent holes are allowed, but all
// opaque artwork must have exactly one colour. No cross-frame proof cache:
// palette changes and rewritten tilemaps take effect on the current draw.
struct WideTextCoverSource
{
    const u8* VRAM;
    u32 Mask;
    const u8* Palette;
    u32 Width, Height, Map, Tiles;
    bool EightBit;

    u16 Read16(u32 address) const
    {
        return VRAM[address & Mask] | (VRAM[(address + 1) & Mask] << 8);
    }

    u16 PaletteColor(u32 index) const
    {
        return Palette[index * 2] | (Palette[index * 2 + 1] << 8);
    }

    u32 PixelIndex(u16 tile, u32 x, u32 y) const
    {
        if (tile & 0x400) x = 7 - x;
        if (tile & 0x800) y = 7 - y;
        const u32 pixel = y * 8 + x;
        const u32 offset = Tiles + (tile & 1023) * (EightBit ? 64 : 32);
        const u32 value = VRAM[(offset + (EightBit ? pixel : pixel / 2)) & Mask];
        return EightBit ? value : (value >> ((pixel & 1) * 4)) & 15;
    }

    bool OpaqueAt(u32 x, u32 y) const
    {
        x &= Width - 1; y &= Height - 1;
        const u32 block = (x / 256) + (y / 256) * (Width / 256);
        const u32 entry = block * 1024 + ((y & 255) / 8) * 32 + (x & 255) / 8;
        return PixelIndex(Read16(Map + entry * 2), x & 7, y & 7) != 0;
    }

    bool UniformColor(u32& color) const
    {
        if ((Width != 256 && Width != 512) || (Height != 256 && Height != 512)) return false;
        std::bitset<16384> checked;
        bool found = false;
        for (u32 entry = 0; entry < Width * Height / 64; ++entry)
        {
            const u16 tile = Read16(Map + entry * 2);
            const u32 bank = EightBit ? 0 : tile >> 12;
            const u32 key = (tile & 1023) + bank * 1024;
            if (checked[key]) continue;
            checked[key] = true;
            for (u32 y = 0; y < 8; ++y)
                for (u32 x = 0; x < 8; ++x)
                {
                    const u32 index = PixelIndex(tile, x, y);
                    if (!index) continue;
                    const u32 value = PaletteColor(bank * 16 + index);
                    if (found && value != color) return false;
                    color = value;
                    found = true;
                }
        }
        return found;
    }
};
}
