// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GPU3D_TEXTURECACHELIFETIME_H
#define GPU3D_TEXTURECACHELIFETIME_H

#include <cstddef>
#include <vector>
#include "types.h"

namespace melonDS
{

// Keys are ordered oldest to newest, and every use updates LastUsedFrame and
// moves its key to the end. A pending draw keeps its array layer alive even
// when the frame's working set exceeds the nominal palette-variant budget.
// Returns whether a real cache entry was removed (cached draw lists then need
// rebuilding). Missing keys can be discarded without releasing storage twice.
template <typename CacheT, typename ReleaseT>
bool TrimTexturePaletteVariants(std::vector<u64>& keys, CacheT& cache,
                                size_t budget, u64 protectedFrame, ReleaseT release)
{
    bool changed = false;
    while (keys.size() > budget)
    {
        auto entry = cache.find(keys.front());
        if (entry != cache.end() && entry->second.LastUsedFrame >= protectedFrame)
            break;

        keys.erase(keys.begin());
        if (entry != cache.end())
        {
            release(entry->second);
            cache.erase(entry);
            changed = true;
        }
    }
    return changed;
}

}
#endif
