// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source;

smooth in vec2 fTexcoord;

out vec4 oColor;

vec4 RGBAToYUVA(vec4 rgba)
{
    vec4 yuva = vec4(0.0);
    yuva.x = rgba.r * 0.299 + rgba.g * 0.587 + rgba.b * 0.114;
    yuva.y = rgba.r * -0.169 + rgba.g * -0.331 + rgba.b * 0.500 + 0.500;
    yuva.z = rgba.r * 0.500 + rgba.g * -0.419 + rgba.b * -0.081 + 0.500;
    yuva.w = rgba.a;
    return yuva;
}

void main()
{
    oColor = RGBAToYUVA(texture(Source, fTexcoord));
}
