// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.h"

namespace melonDS
{
inline bool CanRetainNativeCompositionWideSource(bool fullFrameNativeInput,
    bool capture3D, bool direct3DVisible, u32 visibleBGLayers,
    u32 bgLayerTypes, u32 bitmapMask)
{
    // A native center does not invalidate separately rendered side strips.
    // Only accept the same direct-3D/text-layer shape used by ordinary wide
    // captures. This grants no high-resolution replacement for the center.
    if (!fullFrameNativeInput) return false;
    if (capture3D) return true;
    if (!direct3DVisible || !(visibleBGLayers & 1u) || bitmapMask) return false;
    for (int layer = 0; layer < 4; ++layer)
    {
        if (!(visibleBGLayers & (1u << layer))) continue;
        const u32 type = (bgLayerTypes >> (layer * 8)) & 0xFFu;
        if (layer == 0 ? type != 6 : type > 1) return false;
    }
    return true;
}

inline bool CanUseWideFeedbackSource(bool fifo, bool tracked, unsigned offset,
    bool hasTexture, bool sameBank, u64 serial, u64 completedSerial)
{
    // Same-bank capture reads the retained pre-write image. Other banks must
    // still name the exact completed capture; unrelated VRAM is never history.
    return !fifo && tracked && offset == 0 && hasTexture && serial &&
        (sameBank || serial == completedSerial);
}

inline bool CanPresentWideVRAMCapture(bool mapped, bool nativeCaptureValid,
    bool hasTexture, u64 serial, u64 completedSerial, u64 pendingPreviousSerial)
{
    // Pending replacement retains the previous completed texture, never a
    // partially written image. Partial capture and writes clear that token.
    return mapped && nativeCaptureValid && hasTexture &&
        (pendingPreviousSerial || (serial && serial == completedSerial));
}

inline bool IsUnmappedWideBitmap(u32 type, const u32* mappings, unsigned count)
{
    // Unmapped direct-color bitmap reads are transparent zero. Palette/tile
    // backgrounds cannot use this proof: palette entry zero may be visible.
    if (type != 5) return false;
    for (unsigned i = 0; i < count; ++i)
        if (mappings[i]) return false;
    return true;
}
}
