// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform vec2 uOutputSize;
uniform sampler2D Source;
uniform sampler2D AlphaSource;

out vec4 oColor;

float SampleMidpointAlpha(vec2 dstPixelCenter)
{
    ivec2 alphaSizeI = textureSize(AlphaSource, 0);
    vec2 alphaSize = vec2(alphaSizeI);
    vec2 srcCoord = (dstPixelCenter * alphaSize / uOutputSize) - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);
    ivec2 maxCoord = alphaSizeI - ivec2(1);

    ivec2 c00 = clamp(baseCoord, ivec2(0), maxCoord);
    ivec2 c10 = clamp(baseCoord + ivec2(1, 0), ivec2(0), maxCoord);
    ivec2 c01 = clamp(baseCoord + ivec2(0, 1), ivec2(0), maxCoord);
    ivec2 c11 = clamp(baseCoord + ivec2(1, 1), ivec2(0), maxCoord);

    float a00 = texelFetch(AlphaSource, c00, 0).a;
    float a10 = texelFetch(AlphaSource, c10, 0).a;
    float a01 = texelFetch(AlphaSource, c01, 0).a;
    float a11 = texelFetch(AlphaSource, c11, 0).a;

    float ax0 = mix(a00, a10, frac.x);
    float ax1 = mix(a01, a11, frac.x);
    return mix(ax0, ax1, frac.y);
}

void main()
{
    ivec2 dstCoord = ivec2(gl_FragCoord.xy);
    ivec2 colorSize = textureSize(Source, 0);
    vec4 color = texelFetch(Source, clamp(dstCoord, ivec2(0), colorSize - ivec2(1)), 0);
    float alpha = SampleMidpointAlpha(gl_FragCoord.xy);

    oColor = vec4(color.rgb, clamp(alpha, 0.0, 1.0));
}
