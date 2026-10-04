// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GLBGVRAMUploadCache.h"

using namespace melonDS;

POLICY_TEST(BGVRAMUploadCacheRequiresActualUpload)
{
    GLBGVRAMUploadCache cache;
    std::vector<u8> bytes(512 * 1024);
    NonStupidBitField<1024> dirty(0, 1024);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(dirty[0]);
    CHECK(dirty[1023]);
    cache.RecordUpload(bytes.data(), 0, 32768);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(!dirty.CheckRange(0, 64));
    CHECK(dirty[64]); // Untouched zero-initialized CPU bytes are not evidence.

    bytes[511] = 1;
    dirty[0] = true;
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(dirty[0]);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(dirty[0]); // Filtering alone must not acknowledge the changed byte.
    cache.RecordUpload(bytes.data(), 0, 32768);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(!dirty[0]);
}

POLICY_TEST(BGVRAMUploadCachePreservesChangesAndRemaps)
{
    GLBGVRAMUploadCache cache;
    std::vector<u8> bytes(512 * 1024, 7);
    NonStupidBitField<1024> dirty(0, 1024);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    cache.RecordUpload(bytes.data(), 0, bytes.size());
    // A different flattened backing pointer is safe only when bytes agree.
    auto remapped = bytes;
    remapped[0] = 0;
    remapped[512] = 0;
    remapped.back() = 0;
    cache.FilterUnchanged(remapped.data(), remapped.size(), dirty);
    CHECK(dirty[0]);
    CHECK(dirty[1]);
    CHECK(dirty[1023]);
    CHECK(!dirty.CheckRange(2, 1021));
    // A write reverted before upload no longer changes the GPU representation.
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(!dirty.CheckRange(0, 1024));
}

POLICY_TEST(BGVRAMUploadCacheResetInvalidatesZeroAndNonzeroBytes)
{
    GLBGVRAMUploadCache cache;
    std::vector<u8> bytes(512 * 1024, 0);
    NonStupidBitField<1024> dirty(0, 1024);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    cache.RecordUpload(bytes.data(), 0, bytes.size());
    cache.Reset();
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(dirty[0]);
    CHECK(dirty[1023]);
    // Changing the physical address-space size invalidates prior uploads too.
    cache.RecordUpload(bytes.data(), 0, bytes.size());
    bytes.resize(128 * 1024);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(dirty[0]);
    CHECK(dirty[255]);
}

POLICY_TEST(BGVRAMUploadCacheSubEngineMirrorsCanonicalDirtyBlocks)
{
    GLBGVRAMUploadCache cache;
    std::vector<u8> bytes(128 * 1024, 9);
    NonStupidBitField<1024> dirty(0, 1024);
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    cache.RecordUpload(bytes.data(), 0, bytes.size());
    bytes.back() = 8;
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    for (u32 first = 0; first < 1024; first += 256)
    {
        CHECK(!dirty.CheckRange(first, 255));
        CHECK(dirty[first + 255]);
    }
    cache.RecordUpload(bytes.data(), 0, bytes.size());
    cache.FilterUnchanged(bytes.data(), bytes.size(), dirty);
    CHECK(!dirty.CheckRange(0, 1024));
}
