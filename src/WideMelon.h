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
enum ViewMethod { Off = 0, Renderer = 1, GamePatch = 2 };
inline int SelectedMethod = Renderer;
inline int PatchAspectWidth = 16, PatchAspectHeight = 10;
inline bool GameFixes = false;
constexpr int ValidateMethod(int method) { return method >= Off && method <= GamePatch ? method : Renderer; }
constexpr int ValidateHeight(int height)
{
    return height >= 192 && height <= 768 && height % 2 == 0 ? height : 192;
}
struct ViewExtent
{
    int Width = 256;
    int Height = 192;
    bool Valid = false;
};
// The smallest requested-aspect rectangle containing the native view is 100%.
// Expansion grows both axes without changing native scene/HUD pixel scale.
// Quantize each extent upward to an even pixel; reject unsupported sizes rather
// than clamping one axis and silently changing the requested aspect.
constexpr ViewExtent ResolveCustomView(int aspectWidth, int aspectHeight, int fovPercent)
{
    if (aspectWidth < 1 || aspectWidth > 32 || aspectHeight < 1 || aspectHeight > 32 ||
        fovPercent < 100 || fovPercent > 300) return {};
    auto even = [](int64_t numerator, int64_t denominator) {
        return int(2 * ((numerator + 2 * denominator - 1) / (2 * denominator)));
    };
    ViewExtent result;
    if (192 * aspectWidth >= 256 * aspectHeight)
    {
        result.Width = even(int64_t(192) * aspectWidth * fovPercent, int64_t(aspectHeight) * 100);
        result.Height = even(int64_t(192) * fovPercent, 100);
    }
    else
    {
        result.Width = even(int64_t(256) * fovPercent, 100);
        result.Height = even(int64_t(256) * aspectHeight * fovPercent, int64_t(aspectWidth) * 100);
    }
    result.Valid = result.Width <= 768 && result.Height <= 768;
    return result;
}
struct Configuration
{
    int Width = 256;
    int Height = 192;
    int Target = Top;
    bool ExtendWindows = true;
    bool ExpandNarrowBorders = false;
    int Method = Renderer;
    int PatchWidth = 16, PatchHeight = 10;
    bool CompatibilityFixes = false;

    bool Enabled() const { return Width > 256 || Height > 192; }
    bool NeedsPresentation() const { return Enabled() || Method == GamePatch; }
    bool operator==(const Configuration& other) const
    {
        return Width == other.Width && Height == other.Height && Target == other.Target &&
               ExtendWindows == other.ExtendWindows && ExpandNarrowBorders == other.ExpandNarrowBorders &&
               Method == other.Method && PatchWidth == other.PatchWidth && PatchHeight == other.PatchHeight &&
               CompatibilityFixes == other.CompatibilityFixes;
    }
    bool operator!=(const Configuration& other) const { return !(*this == other); }
};
inline Configuration CurrentConfiguration()
{
    return {ViewWidth, ViewHeight, Target, ExtendWindows, ExpandNarrowBorders, SelectedMethod,
        PatchAspectWidth, PatchAspectHeight, GameFixes};
}
inline Configuration ResolveConfiguration(int configuredWidth, int displayTarget = Top, int configuredHeight = 192,
                                          bool extendWindows = true, bool expandNarrowBorders = false,
                                          bool custom = false, int aspectWidth = 4, int aspectHeight = 3, int fovPercent = 100,
                                          int method = Renderer, int patchWidth = 16, int patchHeight = 10,
                                          bool fixes = false)
{
    Configuration result;
    if (custom)
    {
        const auto extent = ResolveCustomView(aspectWidth, aspectHeight, fovPercent);
        configuredWidth = extent.Valid ? extent.Width : 256;
        configuredHeight = extent.Valid ? extent.Height : 192;
    }
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
    result.Target = ValidateDisplayTarget(displayTarget);
    result.ExtendWindows = extendWindows;
    result.ExpandNarrowBorders = expandNarrowBorders;
    result.Method = ValidateMethod(method);
    result.PatchWidth = patchWidth;
    result.PatchHeight = patchHeight;
    result.CompatibilityFixes = fixes;
    // A game code owns projection in patch mode. Never compound it with our
    // expanded geometry, capture surfaces or development environment overrides.
    if (result.Method != Renderer) { result.Width = 256; result.Height = 192; }
    return result;
}
inline void ApplyConfiguration(const Configuration& config)
{
    ViewWidth = config.Width;
    ViewHeight = config.Height;
    Target = config.Target;
    ExtendWindows = config.ExtendWindows;
    ExpandNarrowBorders = config.ExpandNarrowBorders;
    SelectedMethod = config.Method;
    PatchAspectWidth = config.PatchWidth;
    PatchAspectHeight = config.PatchHeight;
    GameFixes = config.CompatibilityFixes;
}
inline void Configure(int configuredWidth, int displayTarget = Top, int configuredHeight = 192,
                      bool extendWindows = true, bool expandNarrowBorders = false,
                      bool custom = false, int aspectWidth = 4, int aspectHeight = 3, int fovPercent = 100,
                      int method = Renderer, int patchWidth = 16, int patchHeight = 10,
                      bool fixes = false)
{
    ApplyConfiguration(ResolveConfiguration(configuredWidth, displayTarget, configuredHeight,
                                            extendWindows, expandNarrowBorders,
                                            custom, aspectWidth, aspectHeight, fovPercent,
                                            method, patchWidth, patchHeight, fixes));
}
inline bool NeedsPresentation() { return CurrentConfiguration().NeedsPresentation(); }
inline int Width() { return ViewWidth; }
inline int Height() { return ViewHeight; }
inline bool Vertical() { return Height() > 192; }
inline bool Rectangular() { return Width() > 256 && Height() > 192; }
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
