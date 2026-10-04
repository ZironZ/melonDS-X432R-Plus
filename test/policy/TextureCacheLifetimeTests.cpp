// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GPU3D_TextureCacheLifetime.h"
#include <algorithm>
#include <unordered_map>

using namespace melonDS;

namespace
{
// Like the renderer, queued draws retain array-layer indices, not ownership of
// cache entries. Released layers are immediately reused by subsequent uploads.
struct DeferredDrawCache
{
    struct Entry { u64 LastUsedFrame; size_t Slot; };
    std::unordered_map<u64, Entry> Entries;
    std::vector<u64> Keys;
    std::vector<u64> Slots;
    std::vector<size_t> FreeSlots;
    std::vector<std::pair<size_t, u64>> Draws;
    size_t Releases = 0;

    bool Trim(u64 protectedFrame)
    {
        return TrimTexturePaletteVariants(Keys, Entries, 16, protectedFrame,
            [this](const Entry& entry) {
                FreeSlots.push_back(entry.Slot);
                ++Releases;
            });
    }

    void Draw(u64 key, u64 frame)
    {
        auto it = Entries.find(key);
        const bool inserted = it == Entries.end();
        if (it == Entries.end())
        {
            size_t slot;
            if (FreeSlots.empty())
            {
                slot = Slots.size();
                Slots.push_back(key);
            }
            else
            {
                slot = FreeSlots.back();
                FreeSlots.pop_back();
                Slots[slot] = key;
            }
            it = Entries.emplace(key, Entry{frame, slot}).first;
        }
        it->second.LastUsedFrame = frame;
        Keys.erase(std::remove(Keys.begin(), Keys.end(), key), Keys.end());
        Keys.push_back(key);
        Draws.emplace_back(it->second.Slot, key);
        if (inserted)
            Trim(frame);
    }

    void CheckDraws() const
    {
        for (const auto& draw : Draws)
            CHECK_EQ(Slots[draw.first], draw.second);
    }
};
}

POLICY_TEST(TexturePaletteBudgetPreservesPendingDraws)
{
    DeferredDrawCache cache;
    // More variants than the old limit, all referenced in one pending frame.
    for (u64 key = 0; key < 40; ++key)
        cache.Draw(key, 1);
    cache.CheckDraws();
    CHECK_EQ(cache.Releases, 0u);
    CHECK_EQ(cache.Entries.size(), 40u);

    // Reusing an identical frame performs no lookups. Its last rendered epoch
    // must remain protected across arbitrarily many Update calls.
    for (unsigned update = 0; update < 8; ++update)
        CHECK(!cache.Trim(1));
    cache.CheckDraws();

    // A new frame reuses the entire working set without reupload churn.
    cache.Draws.clear();
    for (u64 key = 0; key < 40; ++key)
        cache.Draw(key, 10);
    cache.CheckDraws();
    CHECK_EQ(cache.Releases, 0u);
}

POLICY_TEST(TexturePaletteBudgetReclaimsOnlyInactiveLayers)
{
    DeferredDrawCache cache;
    for (u64 key = 0; key < 40; ++key)
        cache.Draw(key, 1);
    cache.Draws.clear();

    // Cache hits must pin old entries as well as new uploads. A different
    // frame keeps one old variant and uploads enough others to reuse slots.
    cache.Draw(0, 2);
    for (u64 key = 40; key < 60; ++key)
        cache.Draw(key, 2);
    cache.CheckDraws();
    CHECK_EQ(cache.Entries.count(0), 1u);
    CHECK_EQ(cache.Entries.size(), 21u);
    CHECK_EQ(cache.Releases, 39u);
    CHECK(!cache.Trim(2));

    // The next rendered frame stops using all but one variant. Boundary
    // cleanup returns true so cached draw lists cannot retain recycled slots.
    cache.Draws.clear();
    cache.Draw(0, 3);
    CHECK(cache.Trim(3));
    CHECK_EQ(cache.Entries.size(), 16u);
    cache.CheckDraws();
    CHECK(!cache.Trim(3));
}

POLICY_TEST(TexturePaletteIdleCleanupInvalidatesCachedDrawLists)
{
    DeferredDrawCache cache;
    for (u64 key = 0; key < 40; ++key)
        cache.Draw(key, 1);
    cache.Draws.clear();
    // A later frame renders no textured polygons; the old excess can go.
    CHECK(cache.Trim(2));
    CHECK_EQ(cache.Entries.size(), 16u);
    CHECK_EQ(cache.Releases, 24u);
    CHECK(!cache.Trim(2));
}

POLICY_TEST(TexturePaletteStaleKeysDoNotDoubleReleaseLayers)
{
    DeferredDrawCache cache;
    for (u64 key = 0; key < 16; ++key)
        cache.Draw(key, 1);
    cache.Keys.insert(cache.Keys.begin(), 1000);
    CHECK(!cache.Trim(2));
    CHECK_EQ(cache.Releases, 0u);
    CHECK_EQ(cache.Keys.size(), 16u);
    cache.CheckDraws();
}
