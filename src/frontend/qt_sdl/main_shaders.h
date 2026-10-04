/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef MAIN_SHADERS_H
#define MAIN_SHADERS_H

const char* kScreenVS = R"(#version 140

uniform vec2 uScreenSize;
uniform mat2x3 uTransform;

in vec2 vPosition;
in vec3 vTexcoord;

smooth out vec3 fTexcoord;

void main()
{
    vec4 fpos;

    fpos.xy = vec3(vPosition, 1.0) * uTransform;

    fpos.xy = ((fpos.xy * 2.0) / uScreenSize) - 1.0;
    fpos.y *= -1;
    fpos.z = 0.0;
    fpos.w = 1.0;

    gl_Position = fpos;
    fTexcoord = vTexcoord;
}
)";

const char* kScreenFS = R"(#version 140
uniform float uContentWidth;
uniform float uContentHeight;

uniform sampler2DArray ScreenTex;
uniform sampler2DArray LCDGhostingHistoryTex;
uniform float uSharpenAmount;
uniform int uLCDGhostingMode;
uniform ivec4 uLCDGhostingHistorySlots;

smooth in vec3 fTexcoord;

out vec4 oColor;

bool SameColor(vec3 a, vec3 b)
{
    return all(equal(a, b));
}

vec3 GetScreenColor(vec3 texcoord)
{
    vec3 current = texture(ScreenTex, texcoord).rgb;
    if (uLCDGhostingMode == 0)
        return current;

    float screen = texcoord.z;
    vec3 previous1 = texture(LCDGhostingHistoryTex,
                             vec3(texcoord.xy, float(uLCDGhostingHistorySlots.x * 2) + screen)).rgb;
    vec3 previous2 = texture(LCDGhostingHistoryTex,
                             vec3(texcoord.xy, float(uLCDGhostingHistorySlots.y * 2) + screen)).rgb;
    vec3 previous3 = texture(LCDGhostingHistoryTex,
                             vec3(texcoord.xy, float(uLCDGhostingHistorySlots.z * 2) + screen)).rgb;

    if (uLCDGhostingMode == 1)
    {
        bool alternating =
            (SameColor(current, previous2) || SameColor(previous1, previous3)) &&
            !SameColor(current, previous1) &&
            !SameColor(current, previous3) &&
            !SameColor(previous1, previous2);

        return alternating ? (current + previous1) * 0.5 : current;
    }

    vec3 previous4 = texture(LCDGhostingHistoryTex,
                             vec3(texcoord.xy, float(uLCDGhostingHistorySlots.w * 2) + screen)).rgb;
    // Four historical samples are enough for this approximation because r^5
    // contributes less than half a percent.
    const float response = 0.333;
    vec3 color = current;
    float responseFactor = response;
    color += (previous1 - color) * responseFactor;
    responseFactor *= response;
    color += (previous2 - color) * responseFactor;
    responseFactor *= response;
    color += (previous3 - color) * responseFactor;
    responseFactor *= response;
    color += (previous4 - color) * responseFactor;
    return color;
}

void main()
{
    vec3 coord = fTexcoord;
    coord.x = (coord.x - 0.5) * uContentWidth + 0.5;
    coord.y = (coord.y - 0.5) * uContentHeight + 0.5;
    vec3 color = GetScreenColor(coord);

    if (uSharpenAmount > 0.0)
    {
        vec2 texel = 1.0 / vec2(textureSize(ScreenTex, 0).xy);
        vec3 left = GetScreenColor(coord + vec3(-texel.x, 0.0, 0.0));
        vec3 right = GetScreenColor(coord + vec3(texel.x, 0.0, 0.0));
        vec3 up = GetScreenColor(coord + vec3(0.0, -texel.y, 0.0));
        vec3 down = GetScreenColor(coord + vec3(0.0, texel.y, 0.0));

        vec3 edge = color * 4.0 - left - right - up - down;
        color = clamp(color + edge * uSharpenAmount, 0.0, 1.0);
    }

    oColor = vec4(color, 1.0);
}
)";

#endif // MAIN_SHADERS_H
