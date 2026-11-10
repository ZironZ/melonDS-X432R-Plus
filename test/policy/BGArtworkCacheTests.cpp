// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GLBGArtworkCache.h"

using namespace melonDS;

namespace
{
struct Fixture
{
    GLBGVRAMUploadCache Upload;
    GLBGArtworkCache Art;
    std::vector<u8> VRAM;
    std::array<u16, 256 * 65> Palette{};
    GLBGArtworkCache::Config Config{256, 256, 0, 0, 65536, 0, 0};
    Fixture(u32 size = 512 * 1024) : VRAM(size)
    {
        std::fill(VRAM.begin()+65536, VRAM.begin()+65536+64, 0x11);
        Commit();
    }
    void Commit()
    {
        NonStupidBitField<1024> dirty(0, 1024);
        Upload.FilterUnchanged(VRAM.data(), VRAM.size(), dirty);
        Upload.RecordUpload(VRAM.data(), 0, VRAM.size());
    }
    bool Changed() { return Art.Update(Config, Upload, Palette.data(), Palette.size()); }
};
}

POLICY_TEST(BGArtworkDependenciesIgnoreUnusedMemoryAndPalette)
{
    for (u32 size : {128u * 1024, 512u * 1024})
    for (u32 type = 0; type < 4; ++type)
    {
        Fixture f(size); f.Config.Type = type;
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.VRAM.back() ^= 1; f.Commit();
        CHECK(!f.Changed());
        f.Palette[250] ^= 0xFFFF;
        CHECK(!f.Changed());
        const u32 index = type == 0 ? 1 : 0x11;
        f.Palette[index] ^= 0x8000;
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.VRAM[65536] = 0; f.Commit();
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.Palette[0] ^= 0x8000; // transparent RGB is still a reconstruction input
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.VRAM[65536] = 0x22; // CPU writes are irrelevant until actually uploaded
        CHECK(!f.Changed());
        f.Commit(); CHECK(f.Changed());
        f.Art.Reset(); CHECK(f.Changed());
        f.Upload.Reset(); CHECK(f.Changed()); CHECK(f.Changed());
    }
}

POLICY_TEST(BGArtworkDependenciesFollowTextScreenBlocksAndTileBanks)
{
    for (u32 type : {0u, 1u})
    for (u32 width : {256u, 512u})
    for (u32 height : {256u, 512u})
    {
        Fixture f;
        f.Config.Type = type; f.Config.Width = width; f.Config.Height = height;
        f.Config.PalOffset = type == 1 ? 49 : 0;
        const u32 last = width * height / 64 * 2 - 2;
        f.VRAM[last] = 1; f.VRAM[last+1] = 0xFC; // flips + last palette bank
        std::fill(f.VRAM.begin()+65536+(type == 0 ? 32 : 64),
                  f.VRAM.begin()+65536+(type == 0 ? 64 : 128), 0x22);
        f.Commit(); CHECK(f.Changed()); CHECK(!f.Changed());
        const u32 bank = type == 0 ? 15 * 16 : 64 * 256;
        f.Palette[bank+(type == 0 ? 2 : 0x22)] ^= 0x8000;
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.VRAM[last+1] ^= 4; f.Commit(); // horizontal flip changes placement
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.VRAM[65536+(type == 0 ? 32 : 64)] ^= 1; f.Commit();
        CHECK(f.Changed());
        f.Config.Clamp ^= 1; CHECK(f.Changed());
    }
}

POLICY_TEST(BGArtworkDependenciesWrapVRAMAndFailClosedForUnknownInputs)
{
    for (u32 size : {128u * 1024, 512u * 1024})
    for (u32 type : {2u, 3u})
    {
        Fixture f(size); f.Config.Type = type;
        f.Config.Width = f.Config.Height = 1024;
        f.Config.MapOffset = size - 32;
        f.Config.TileOffset = size - 32;
        f.Commit(); CHECK(f.Changed()); CHECK(!f.Changed());
        f.VRAM[0] ^= 1; f.Commit(); CHECK(f.Changed()); CHECK(!f.Changed());
        f.Config.MapOffset += size; f.Config.TileOffset += size;
        CHECK(f.Changed()); CHECK(!f.Changed());
        f.Config.Type = 4; CHECK(f.Changed()); CHECK(f.Changed());
        f.Config.Type = type; f.Config.PalOffset = 65;
        CHECK(f.Changed()); CHECK(f.Changed());
    }
}

POLICY_TEST(BGArtworkCurrentMemoryProofDoesNotAdvanceUploadedDependencies)
{
    for (u32 type = 0; type < 4; ++type)
    {
        Fixture f; f.Config.Type = type; CHECK(f.Changed());
        const auto matches = [&] { return f.Art.MatchesCurrent(f.Config, f.VRAM.data(),
            f.VRAM.size(), f.Palette.data(), f.Palette.size()); };
        CHECK(matches());
        f.VRAM.back() ^= 1; CHECK(matches());
        f.VRAM[65536] ^= 1; CHECK(!matches());
        // A speculative current-memory check must not alter the uploaded key.
        CHECK(!f.Changed());
        f.VRAM[65536] ^= 1; CHECK(matches());
        f.Palette[type == 0 ? 1 : 0x11] ^= 0x8000; CHECK(!matches());
        f.Palette[type == 0 ? 1 : 0x11] ^= 0x8000; CHECK(matches());
        CHECK(!f.Art.MatchesCurrent(f.Config, nullptr, f.VRAM.size(),
            f.Palette.data(), f.Palette.size()));
        CHECK(!f.Art.MatchesCurrent(f.Config, f.VRAM.data(), f.VRAM.size()/2,
            f.Palette.data(), f.Palette.size()));
        f.Art.Reset(); CHECK(!matches());
    }
}
