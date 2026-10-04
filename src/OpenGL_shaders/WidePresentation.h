// Side-area presentation adapted from WideMelon's FinalPassFS.glsl.
// Copyright (C) 2026 WideMelon contributors
// Modifications Copyright (C) 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
static const char* kWidePresentationFS = R"(#version 140
uniform sampler2D WideInputA;
uniform sampler2D WideInputB;
uniform ivec2 uWideModes;
uniform int uWideWidth;
uniform int uWideOverlap;
uniform bvec2 uWideDisplays;
struct WideWindowRow
{
    uvec4 Current;
    uvec4 Captured;
};
layout(std140) uniform ubWideWindows
{
    WideWindowRow uWideWindows[192];
};
uniform ivec3 uPresentation[2]; // Base override (-1: composed), master mode, factor.
layout(std140) uniform ubFinalPassConfig
{
    bvec4 uScreenSwap[48];
    int uScaleFactor;
    int uAuxLayer;
    int uDispModeA;
    int uDispModeB;
    int uBrightModeA;
    int uBrightModeB;
    int uBrightFactorA;
    int uBrightFactorB;
    float uAuxColorFactor;
};
smooth in vec3 fTexcoord;
out vec4 oTopColor;
out vec4 oBottomColor;
ivec3 effect(ivec3 color, ivec2 config)
{
    if (config.x == 2) color += ((63 - color) * config.y + 8) >> 4;
    else if (config.x == 3) color -= (color * config.y + 7) >> 4;
    return color;
}
ivec3 backdrop(uint policy)
{
    uint c = policy >> 9;
    return ivec3((c & 31u) << 1, ((c >> 5) & 31u) * 2u + ((c >> 15) & 1u), (c >> 9) & 62u);
}
ivec3 windowEffect(ivec3 color, uint policy, float alpha)
{
    if ((policy & (1u << 25)) != 0u)
    {
        if (alpha == 0.0) return effect(backdrop(policy),
            ivec2((policy >> 26) & 3u, (policy >> 4) & 31u));
        int a = (int(alpha * 255.0) >> 3) + 1;
        return (color * a + backdrop(policy) * (32-a) + 16) >> 5;
    }
    return effect(color, ivec2((policy >> 2) & 3u, (policy >> 4) & 31u));
}
ivec3 compose(vec4 raw, int engine, int line, int side)
{
    uint current = uWideWindows[line].Current[engine * 2 + side];
    if (uWideModes[engine] == 0 || (current & 1u) == 0u) return ivec3(0);
    ivec3 color = ivec3(raw.rgb * 255.0) >> 2;
    if ((current & 2u) == 0u)
        color = backdrop(current);
    else if (uWideModes[engine] >= 2)
    {
        uint captured = uWideWindows[line].Captured[engine * 2 + side];
        if ((captured & 1u) == 0u) return ivec3(0);
        if ((captured & 2u) == 0u) color = backdrop(captured);
        color = windowEffect(color, captured, raw.a);
        raw.a = 1.0; // Source-A composition is opaque after the captured backdrop.
    }
    return windowEffect(color, current, raw.a);
}
vec4 present(vec4 raw, int engine, int line, int side)
{
    // A missing/unsafe source is black content, not an exemption from the fade.
    ivec3 color = uPresentation[engine].x >= 0 ? ivec3(uPresentation[engine].x) :
        compose(raw, engine, line, side);
    ivec2 bright = uPresentation[engine].yz;
    if (bright.x == 1) color += ((63 - color) * bright.y) >> 4;
    else if (bright.x == 2) color -= (color * bright.y + 15) >> 4;
    color = (color << 2) | (color >> 6);
    return vec4(vec3(color) / 255.0, 1);
}
void main()
{
    float x = fTexcoord.x * float(uWideWidth);
    float gap = float(256 - 2 * uWideOverlap);
    int edge = x < float(uWideWidth) * 0.5 ? 0 : 1;
    vec2 captureCoord = vec2((edge == 0 ? x : x - gap) / (float(uWideWidth) - gap), fTexcoord.y);
    int line = clamp(int(fTexcoord.y * 192), 0, 191);
    vec4 mainColor = present(texture(WideInputA, uWideModes.x == 2 ? captureCoord : fTexcoord.xy), 0, line, edge);
    vec4 subColor = present(texture(WideInputB, uWideModes.y == 2 ? captureCoord : fTexcoord.xy), 1, line, edge);
    bool swap = uScreenSwap[line >> 2][line & 3];
    oTopColor = uWideDisplays.x ? (swap ? mainColor : subColor) : vec4(0, 0, 0, 1);
    oBottomColor = uWideDisplays.y ? (swap ? subColor : mainColor) : vec4(0, 0, 0, 1);
}
)";
