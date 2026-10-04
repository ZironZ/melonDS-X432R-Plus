// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <cstring>
#include <vector>
#include "NonStupidBitfield.h"

namespace melonDS
{

// Renderer-local shadow of bytes actually uploaded to the BG VRAM texture.
// Dirty memory is not necessarily changed memory (for example HBlank DMA
// rewriting the same tilemap). Never suppress a block until it was uploaded.
class GLBGVRAMUploadCache
{
public:
    static constexpr u32 BlockSize = 512;

    void Reset() { Valid.Clear(); }

    void FilterUnchanged(const u8* source, u32 byteSize, NonStupidBitField<1024>& dirty)
    {
        assert(byteSize == 128 * 1024 || byteSize == 512 * 1024);
        if (Bytes.size() != byteSize)
        {
            Bytes.resize(byteSize);
            Reset();
        }
        const u32 blocks = byteSize / BlockSize;
        for (auto it = dirty.Begin(); it != dirty.End(); ++it)
        {
            const u32 block = *it;
            if (block >= blocks) break;
            const u32 offset = block * BlockSize;
            if (Valid[block] && std::memcmp(source + offset, Bytes.data() + offset, BlockSize) == 0)
                dirty[block] = false;
        }
        // The sub engine mirrors its 128 KiB address space in BG range tests.
        for (u32 first = blocks; first < 1024; first += blocks)
            std::memcpy(&dirty.Data[first / 64], dirty.Data, blocks / 8);
    }

    void RecordUpload(const u8* source, u32 offset, u32 length)
    {
        assert(offset % BlockSize == 0 && length % BlockSize == 0);
        assert(offset <= Bytes.size() && length <= Bytes.size() - offset);
        std::memcpy(Bytes.data() + offset, source + offset, length);
        Valid.SetRange(offset / BlockSize, length / BlockSize);
    }

private:
    std::vector<u8> Bytes;
    NonStupidBitField<1024> Valid;
};

}
