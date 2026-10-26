// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstring>
#include <vector>
#include "GLBGVRAMUploadCache.h"

namespace melonDS
{

// A conservative, exact dependency snapshot for 2DLayerPreFS tiled sources.
// Native prerenders and their history generations are deliberately independent.
// Unchanged dependencies prove unchanged color AND ownership/alpha metadata.
class GLBGArtworkCache
{
public:
    struct Config
    {
        u32 Width, Height, Type, PalOffset, TileOffset, MapOffset, Clamp;
        bool operator==(const Config& b) const
        {
            return Width == b.Width && Height == b.Height && Type == b.Type &&
                PalOffset == b.PalOffset && TileOffset == b.TileOffset &&
                MapOffset == b.MapOffset && Clamp == b.Clamp;
        }
    };

    void Reset() { Valid = false; }

    // Returns true unless the exact bytes used by the preceding source still
    // agree. Unknown/unuploaded inputs always invalidate; hashes never authorize
    // reuse. 32-byte chunks may conservatively include neighboring map entries.
    bool Update(const Config& config, const GLBGVRAMUploadCache& upload,
                const u16* palette, size_t paletteSize)
    {
        const auto& vram = upload.UploadedBytes();
        if (config.Type > 3 || config.Width < 8 || config.Height < 8 ||
            config.Width > 1024 || config.Height > 1024 ||
            (config.Width & 7) || (config.Height & 7) ||
            (config.Type <= 1 && (config.Width > 512 || config.Height > 512)) ||
            (config.Type != 2 && (config.MapOffset & 1)) ||
            (vram.size() != 128 * 1024 && vram.size() != 512 * 1024))
        {
            Reset();
            return true;
        }
        if (Valid && config == SourceConfig && VRAMSize == vram.size())
        {
            bool same = true;
            for (const auto& chunk : Chunks)
                if (!upload.IsUploaded(chunk.Address) ||
                    std::memcmp(vram.data() + chunk.Address, chunk.Bytes.data(), 32))
                { same = false; break; }
            if (same)
                for (const auto& entry : Palette)
                    if (entry.Index >= paletteSize || palette[entry.Index] != entry.Value)
                    { same = false; break; }
            if (same) return false;
        }

        Valid = true;
        SourceConfig = config;
        VRAMSize = vram.size();
        Chunks.clear(); Palette.clear();
        std::vector<bool> used(vram.size() / 32);
        std::vector<bool> usedPalette(paletteSize);
        // A tile can appear under several palette banks. Inspect each distinct
        // tile/bank pair once, rather than decoding every mapped native pixel.
        std::vector<bool> tiles(1024 * 16);
        const u32 mask = vram.size() - 1;
        const auto read = [&](u32 address) {
            address &= mask;
            used[address / 32] = true;
            return vram[address];
        };
        const auto pal = [&](u32 index) {
            if (index < paletteSize) usedPalette[index] = true;
            else Valid = false;
        };
        for (u32 y = 0; y < config.Height; y += 8)
        for (u32 x = 0; x < config.Width; x += 8)
        {
            u32 mapAddress;
            if (config.Type <= 1)
            {
                mapAddress = config.MapOffset + ((x >> 3) & 31) * 2 + ((y >> 3) & 31) * 64;
                if (config.Width == 512) mapAddress += (x >> 8) * 2048;
                if (config.Height == 512) mapAddress += (y >> 8) * (config.Width == 512 ? 4096 : 2048);
            }
            else
                mapAddress = config.MapOffset + ((x >> 3) + (y >> 3) * (config.Width >> 3)) *
                    (config.Type == 2 ? 1 : 2);
            u32 map = read(mapAddress);
            if (config.Type != 2) map |= read(mapAddress + 1) << 8;
            const u32 tile = config.Type == 2 ? map : map & 1023;
            const u32 bank = config.Type == 0 || (config.Type != 2 && config.PalOffset) ? map >> 12 : 0;
            if (tiles[tile * 16 + bank]) continue;
            tiles[tile * 16 + bank] = true;
            const bool fourBit = config.Type == 0;
            const u32 tileBytes = fourBit ? 32 : 64;
            for (u32 i = 0; i < tileBytes; ++i)
            {
                const u32 value = read(config.TileOffset + tile * tileBytes + i);
                if (fourBit)
                {
                    pal(config.PalOffset * 256 + bank * 16 + (value & 15));
                    pal(config.PalOffset * 256 + bank * 16 + (value >> 4));
                }
                else pal((config.PalOffset + bank) * 256 + value);
            }
        }
        for (u32 i = 0; i < used.size(); ++i)
            if (used[i])
            {
                const u32 address = i * 32;
                if (!upload.IsUploaded(address)) Valid = false;
                Chunk chunk{address, {}};
                std::memcpy(chunk.Bytes.data(), vram.data() + address, 32);
                Chunks.push_back(chunk);
            }
        for (u32 i = 0; i < usedPalette.size(); ++i)
            if (usedPalette[i]) Palette.push_back({i, palette[i]});
        return true;
    }

private:
    struct Chunk { u32 Address; std::array<u8, 32> Bytes; };
    struct PaletteEntry { u32 Index; u16 Value; };
    Config SourceConfig {};
    size_t VRAMSize = 0;
    bool Valid = false;
    std::vector<Chunk> Chunks;
    std::vector<PaletteEntry> Palette;
};

}
