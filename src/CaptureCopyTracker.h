// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <vector>
#include "types.h"

namespace melonDS
{
// Explicit copy provenance, not image recognition. RAM writes need no extra
// write barrier: every consumed row must still equal the retained source bytes.
class CaptureCopyTracker
{
public:
    static constexpr u32 RowBytes = 512, ImageBytes = RowBytes * 192;
    static constexpr size_t Capacity = 4;
    struct Record
    {
        u32 Address = 0;
        u64 Generation = 0;
        std::vector<u8> Bytes;
    };
    struct Row
    {
        size_t Slot;
        u64 Generation;
        u32 Y;
    };
    void Reset()
    {
        for (auto& record : Records) record.Generation = 0;
        Links = {};
        // Never reuse a generation held by an in-flight frame proof.
    }
    size_t Store(u32 address, const u8* bytes)
    {
        size_t slot = 0;
        for (size_t i = 0; i < Capacity; ++i)
        {
            if (Records[i].Generation && Records[i].Address == address) { slot = i; break; }
            if (Records[i].Generation < Records[slot].Generation) slot = i;
        }
        auto& record = Records[slot];
        record.Address = address;
        record.Generation = ++Generation;
        record.Bytes.assign(bytes, bytes + ImageBytes);
        return slot;
    }
    void Invalidate(size_t slot) { Records[slot].Generation = 0; }
    void InvalidateRAMRange(u32 address, u32 bytes)
    {
        for (auto& record : Records)
            if (record.Generation && static_cast<u64>(address) < static_cast<u64>(record.Address) + ImageBytes &&
                static_cast<u64>(record.Address) < static_cast<u64>(address) + bytes)
                record.Generation = 0;
    }
    const Record& Get(size_t slot) const { return Records[slot]; }
    void NoteCopy(u32 source, u32 destination, u32 bytes)
    {
        for (auto& link : Links)
            if (link.Generation && static_cast<u64>(destination) < static_cast<u64>(link.Destination) + RowBytes &&
                static_cast<u64>(link.Destination) < static_cast<u64>(destination) + bytes)
                link.Generation = 0;
        if (bytes != RowBytes) return;
        for (size_t slot = 0; slot < Capacity; ++slot)
        {
            const auto& record = Records[slot];
            if (!record.Generation || source < record.Address ||
                static_cast<u64>(source) + RowBytes > static_cast<u64>(record.Address) + ImageBytes ||
                (source - record.Address) % RowBytes) continue;
            auto* link = &Links[0];
            for (auto& candidate : Links)
                if (candidate.Generation < link->Generation) link = &candidate;
            *link = {destination, slot, record.Generation, (source - record.Address) / RowBytes};
            return;
        }
    }
    std::optional<Row> Match(u32 destination, const u8* bytes) const
    {
        for (const auto& link : Links)
        {
            if (!link.Generation || link.Destination != destination) continue;
            const auto& record = Records[link.Slot];
            if (record.Generation == link.Generation &&
                !std::memcmp(bytes, record.Bytes.data() + link.Y * RowBytes, RowBytes))
                return Row{link.Slot, link.Generation, link.Y};
        }
        return {};
    }
private:
    struct Link
    {
        u32 Destination = 0;
        size_t Slot = 0;
        u64 Generation = 0;
        u32 Y = 0;
    };
    std::array<Record, Capacity> Records;
    std::array<Link, 8> Links {};
    u64 Generation = 0;
};

struct CopiedCaptureFrameProof
{
    int Rows = 0;
    size_t Slot = 0;
    u64 Generation = 0;
    bool Rejected = false, Consumed = false;
    void Observe(int line, const std::optional<CaptureCopyTracker::Row>& row)
    {
        if (Consumed) { Rejected = true; return; }
        if (Rejected) return;
        if (!row || line < 0 || line >= 192 || row->Y != static_cast<u32>(line) ||
            (Rows && (row->Slot != Slot || row->Generation != Generation)) ||
            (line != Rows && line != Rows - 1))
        { Rejected = true; return; }
        Slot = row->Slot;
        Generation = row->Generation;
        if (line == Rows) ++Rows;
    }
    bool Complete() const { return !Consumed && !Rejected && Rows == 192; }
    bool Consume()
    {
        const bool complete = Complete();
        Consumed = true;
        return complete;
    }
};
}
