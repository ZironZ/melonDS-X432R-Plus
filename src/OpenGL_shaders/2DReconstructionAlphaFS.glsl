// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source;
uniform vec2 uOutputSize;
uniform bool uBilinear;
out vec4 oColor;

float maskAt(ivec2 p)
{
    ivec2 size = textureSize(Source, 0);
    return texelFetch(Source, clamp(p, ivec2(0), size - 1), 0).a;
}

void main()
{
    vec2 p = gl_FragCoord.xy * vec2(textureSize(Source, 0)) / uOutputSize;
    float a = maskAt(ivec2(floor(p)));
    if (uBilinear)
    {
        p -= vec2(0.5);
        ivec2 b = ivec2(floor(p));
        vec2 f = fract(p);
        a = mix(mix(maskAt(b), maskAt(b + ivec2(1, 0)), f.x),
                mix(maskAt(b + ivec2(0, 1)), maskAt(b + ivec2(1, 1)), f.x), f.y);
    }
    // Mask-only input keeps reconstruction independent of the RGB artwork.
    oColor = vec4(clamp(a, 0.0, 1.0));
}
