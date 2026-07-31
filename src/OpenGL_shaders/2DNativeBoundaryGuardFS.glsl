// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D ScaledColorTex;
uniform sampler2D NativeColorTex;
uniform sampler2D UpscaledCoverageTex;

uniform int uScaleFactor;
uniform bool uSecondLayer;

smooth in vec4 fTexcoord;

out vec4 oColor;

void main()
{
    ivec2 coord = ivec2(floor(fTexcoord.zw));
    ivec2 nativeCoord = coord / uScaleFactor;

    vec4 scaledColor = texelFetch(ScaledColorTex, coord, 0);
    vec4 nativeColor = texelFetch(NativeColorTex, nativeCoord, 0);
    vec4 coverage = texelFetch(UpscaledCoverageTex, coord, 0);

    float sourceCoverage = clamp(uSecondLayer ? coverage.g : coverage.r, 0.0, 1.0);
    float scaledWeight = smoothstep(0.45, 0.95, sourceCoverage);

    oColor = clamp(mix(nativeColor, scaledColor, scaledWeight), 0.0, 1.0);
}
