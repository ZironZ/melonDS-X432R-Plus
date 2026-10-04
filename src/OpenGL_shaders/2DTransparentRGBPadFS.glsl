// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source;
uniform ivec2 uSourceSize;
uniform int uPadRadius;
uniform bool uPrecomposeBackdrop;
uniform int uBackdropColor;

out vec4 oColor;

ivec3 ConvertColor(int col)
{
    ivec3 ret;
    ret.r = (col & 0x1F) << 1;
    ret.g = ((col & 0x3E0) >> 4) | (col >> 15);
    ret.b = (col & 0x7C00) >> 9;
    return ret;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    ivec2 textureSizeI = textureSize(Source, 0);
    vec4 center = texelFetch(Source,
                             clamp(coord, ivec2(0), textureSizeI - ivec2(1)),
                             0);
    if (uPrecomposeBackdrop)
    {
        vec3 backdropColor = vec3(ConvertColor(uBackdropColor)) / 63.0;
        oColor = center.a > 0.0
            ? vec4(center.rgb, 1.0)
            : vec4(backdropColor, 1.0);
        return;
    }

    if (center.a > 0.0)
    {
        oColor = center;
        return;
    }

    // Supply deterministic hidden RGB across the selected reconstruction
    // footprint. Neural and directional scalers inspect more than the
    // immediate alpha frontier; leaving palette-entry-zero RGB inside that
    // footprint lets an arbitrary transparent palette color contaminate the
    // reconstructed contour. Search the original decoded source (never this
    // pass's output) and copy the nearest alpha-present color, averaging only
    // exact-distance ties so the extension has no directional bias. Alpha
    // remains the decoded source fact.
    const int MaxPadRadius = 8;
    int padRadius = clamp(uPadRadius, 1, MaxPadRadius);
    int nearestDistance = 0x7FFFFFFF;
    vec3 colorSum = vec3(0.0);
    float sampleCount = 0.0;
    for (int y = -MaxPadRadius; y <= MaxPadRadius; y++)
    {
        if (abs(y) > padRadius)
            continue;

        for (int x = -MaxPadRadius; x <= MaxPadRadius; x++)
        {
            if (abs(x) > padRadius || (x == 0 && y == 0))
                continue;

            ivec2 sampleCoord = coord + ivec2(x, y);
            if (any(lessThan(sampleCoord, ivec2(0))) ||
                any(greaterThanEqual(sampleCoord, uSourceSize)))
                continue;

            vec4 sampleColor = texelFetch(Source, sampleCoord, 0);
            if (sampleColor.a <= 0.0)
                continue;

            int distanceSquared = x * x + y * y;
            if (distanceSquared < nearestDistance)
            {
                nearestDistance = distanceSquared;
                colorSum = sampleColor.rgb;
                sampleCount = 1.0;
            }
            else if (distanceSquared == nearestDistance)
            {
                colorSum += sampleColor.rgb;
                sampleCount += 1.0;
            }
        }
    }

    if (sampleCount > 0.0)
    {
        oColor = vec4(colorSum / sampleCount, center.a);
        return;
    }

    // Transparent palette RGB has no semantic meaning. When no visible source
    // exists inside the reconstruction footprint, use a stable neutral value
    // rather than leaking palette entry zero into a nonlinear scaler.
    oColor = vec4(0.0, 0.0, 0.0, center.a);
}
