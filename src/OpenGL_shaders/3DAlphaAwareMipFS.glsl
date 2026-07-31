// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source;
uniform ivec2 uSourceSize;

out vec4 oColor;

vec4 FetchBinaryAlphaTexel(ivec2 coord)
{
    ivec2 clampedCoord = clamp(coord, ivec2(0), uSourceSize - ivec2(1));
    vec4 color = texelFetch(Source, clampedCoord, 0);
    color.a = color.a >= 0.5 ? 1.0 : 0.0;
    if (color.a == 0.0)
        color.rgb = vec3(0.0);
    return color;
}

void main()
{
    ivec2 dst = ivec2(gl_FragCoord.xy);
    ivec2 srcBase = dst * 2;

    vec4 c0 = FetchBinaryAlphaTexel(srcBase + ivec2(0, 0));
    vec4 c1 = FetchBinaryAlphaTexel(srcBase + ivec2(1, 0));
    vec4 c2 = FetchBinaryAlphaTexel(srcBase + ivec2(0, 1));
    vec4 c3 = FetchBinaryAlphaTexel(srcBase + ivec2(1, 1));

    float alphaWeight = c0.a + c1.a + c2.a + c3.a;
    vec3 rgb = vec3(0.0);
    if (alphaWeight > 0.0)
    {
        rgb = (c0.rgb * c0.a + c1.rgb * c1.a + c2.rgb * c2.a + c3.rgb * c3.a) / alphaWeight;
    }

    float alpha = alphaWeight * 0.25;
    oColor = vec4(rgb, alpha);
}
