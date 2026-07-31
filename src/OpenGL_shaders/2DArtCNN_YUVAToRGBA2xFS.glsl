// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source;
uniform sampler2D artcnn_luma;
uniform sampler2D AlphaSource;
uniform bool uUseAlphaSource;

smooth in vec2 fTexcoord;

out vec4 oColor;

vec4 YUVAToRGBA(vec4 yuva)
{
    vec4 rgba = vec4(0.0);
    float u = yuva.y - 0.5;
    float v = yuva.z - 0.5;

    rgba.r = yuva.x + v * 1.400;
    rgba.g = yuva.x - u * 0.343 - v * 0.711;
    rgba.b = yuva.x + u * 1.765;
    rgba.a = yuva.w;
    return rgba;
}

void main()
{
    vec4 inputY = texture(artcnn_luma, fTexcoord);
    vec4 inputYUVA = texture(Source, fTexcoord);
    vec4 inputAlpha = uUseAlphaSource ? texture(AlphaSource, fTexcoord) : inputYUVA;
    vec4 yuva = vec4(inputY.x, inputYUVA.y, inputYUVA.z, inputAlpha.a);
    oColor = clamp(YUVAToRGBA(yuva), 0.0, 1.0);
}
