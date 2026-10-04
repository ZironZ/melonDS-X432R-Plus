// Widescreen projection adapted from pruefsumme/widemelon (607f1d9).
// Copyright (C) 2026 WideMelon contributors
// Modifications Copyright (C) 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>

namespace WideMelon
{
constexpr int ValidateWidth(int width)
{
    return width >= 256 && width <= 768 && width % 2 == 0 ? width : 256;
}
inline int ParseWidth(const char* value)
{
    if (!value) return 256;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return end != value && *end == '\0' && parsed >= 256 && parsed <= 768
        ? ValidateWidth(static_cast<int>(parsed)) : 256;
}
// Change only while all users are stopped and old geometry/GL allocations are gone.
inline int ViewWidth = 256;
// Vertical expansion is applied before the frontend rotates either display.
inline int ViewHeight = 192;
enum DisplayTarget { Top = 0, Bottom = 1, Both = 2 };
constexpr int ValidateDisplayTarget(int target)
{
    return target >= Top && target <= Both ? target : Top;
}
inline int Target = Top;
inline bool ExtendWindows = true;
inline bool ExpandNarrowBorders = false;
constexpr int ValidateHeight(int height)
{
    return height >= 192 && height <= 768 && height % 2 == 0 ? height : 192;
}
struct Configuration
{
    int Width = 256;
    int Height = 192;
    int Target = Top;
    bool ExtendWindows = true;
    bool ExpandNarrowBorders = false;

    bool Enabled() const { return Width > 256 || Height > 192; }
    bool operator==(const Configuration& other) const
    {
        return Width == other.Width && Height == other.Height && Target == other.Target &&
               ExtendWindows == other.ExtendWindows && ExpandNarrowBorders == other.ExpandNarrowBorders;
    }
    bool operator!=(const Configuration& other) const { return !(*this == other); }
};
inline Configuration CurrentConfiguration()
{
    return {ViewWidth, ViewHeight, Target, ExtendWindows, ExpandNarrowBorders};
}
inline Configuration ResolveConfiguration(int configuredWidth, int displayTarget = Top, int configuredHeight = 192,
                                          bool extendWindows = true, bool expandNarrowBorders = false)
{
    Configuration result;
    const char* env = std::getenv("WIDEMELON_VIEW_WIDTH");
    result.Width = env ? ParseWidth(env) : ValidateWidth(configuredWidth);
    result.Height = ValidateHeight(configuredHeight);
    const char* height = std::getenv("WIDEMELON_VIEW_HEIGHT");
    if (height)
    {
        char* end = nullptr;
        const long parsed = std::strtol(height, &end, 10);
        result.Height = end != height && *end == '\0' && parsed >= 192 && parsed <= 768
            ? ValidateHeight(static_cast<int>(parsed)) : 192;
    }
    if (result.Height > 192) result.Width = 256;
    result.Target = ValidateDisplayTarget(displayTarget);
    result.ExtendWindows = extendWindows;
    result.ExpandNarrowBorders = expandNarrowBorders;
    return result;
}
inline void ApplyConfiguration(const Configuration& config)
{
    ViewWidth = config.Width;
    ViewHeight = config.Height;
    Target = config.Target;
    ExtendWindows = config.ExtendWindows;
    ExpandNarrowBorders = config.ExpandNarrowBorders;
}
inline void Configure(int configuredWidth, int displayTarget = Top, int configuredHeight = 192,
                      bool extendWindows = true, bool expandNarrowBorders = false)
{
    ApplyConfiguration(ResolveConfiguration(configuredWidth, displayTarget, configuredHeight,
                                            extendWindows, expandNarrowBorders));
}
inline int Width() { return ViewWidth; }
inline int Height() { return ViewHeight; }
inline bool Vertical() { return Height() > 192; }
inline bool Enabled() { return Width() > 256 || ViewHeight > 192; }
// Retain real scene pixels beneath a narrow native mask and its filter fringe.
// Configuration is fixed before any capture textures are allocated.
inline int CaptureOverlap() { return ExpandNarrowBorders && ExtendWindows && Width() > 256 ? 2 : 0; }
inline int VerticalCaptureOverlap() { return ExpandNarrowBorders && ExtendWindows && Vertical() ? 2 : 0; }
inline bool WidenDisplay(int screen) { return Enabled() && (Target == Both || Target == screen); }
inline int DisplayWidth(int screen) { return WidenDisplay(screen) ? Width() : 256; }
inline int DisplayHeight(int screen) { return WidenDisplay(screen) ? Height() : 192; }
inline float ContentHeight(int screen) { return float(DisplayHeight(screen)) / Height(); }
inline float DisplayAspect(int screen) { return DisplayWidth(screen) / 256.f; }
inline float DisplayHeightAspect(int screen) { return DisplayHeight(screen) / 192.f; }
inline float ContentWidth(int screen) { return float(DisplayWidth(screen)) / Width(); }
inline int32_t ProjectX(int32_t x, int width)
{
    return static_cast<int32_t>(static_cast<int64_t>(x) * 256 / width);
}
inline int32_t ProjectX(int32_t x) { return ProjectX(x, Width()); }
inline int32_t ProjectY(int32_t y, int height)
{
    if (height == 192) return y;
    return static_cast<int32_t>(static_cast<int64_t>(y) * 192 / height);
}
inline int32_t ProjectY(int32_t y) { return ProjectY(y, Height()); }
}
