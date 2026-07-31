// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source;
uniform int uOutputFormat;
uniform int uBinaryAlpha;
uniform int uPreserveTransparentRGB;
uniform int uLosslessRGB6Repack;

smooth in vec2 fTexcoord;
#ifdef FILTERABLE_TEXTURE_CACHE
out vec4 oColor;
#else
out uvec4 oColor;
#endif

uint QuantizeRGB8ToRGB6LikeDS(float value)
{
    value = clamp(value, 0.0, 255.0);
    int value5 = clamp(int(floor((value / 8.0) + 0.5)), 0, 31);
    if (value5 == 0)
        return 0u;
    return uint(value5 * 2 + 1);
}

uint QuantizeRGB8ToRGB6Roundtrip(float value)
{
    value = clamp(value, 0.0, 255.0);
    return uint(clamp(floor(value * (63.0 / 255.0) + 0.5), 0.0, 63.0));
}

uint QuantizeRGB8ToRGB6(float value)
{
    return uLosslessRGB6Repack != 0
        ? QuantizeRGB8ToRGB6Roundtrip(value)
        : QuantizeRGB8ToRGB6LikeDS(value);
}

uint QuantizeChannel(float value, float maxValue)
{
    return uint(clamp(floor(value * maxValue + 0.5), 0.0, maxValue));
}

void main()
{
    vec4 color = texture(Source, fTexcoord);

    if (uBinaryAlpha != 0)
    {
        color.a = color.a >= 0.5 ? 1.0 : 0.0;
        if (color.a == 0.0 && uPreserveTransparentRGB == 0)
            color.rgb = vec3(0.0);
    }

    if (uOutputFormat == 0)
    {
#ifdef FILTERABLE_TEXTURE_CACHE
        oColor = vec4(
            float(QuantizeRGB8ToRGB6(color.r * 255.0)),
            float(QuantizeRGB8ToRGB6(color.g * 255.0)),
            float(QuantizeRGB8ToRGB6(color.b * 255.0)),
            float(QuantizeChannel(color.a, 31.0))) / 255.0;
#else
        oColor = uvec4(
            QuantizeRGB8ToRGB6(color.r * 255.0),
            QuantizeRGB8ToRGB6(color.g * 255.0),
            QuantizeRGB8ToRGB6(color.b * 255.0),
            QuantizeChannel(color.a, 31.0));
#endif
    }
    else if (uOutputFormat == 1)
    {
#ifdef FILTERABLE_TEXTURE_CACHE
        oColor = vec4(
            float(QuantizeChannel(color.r, 255.0)),
            float(QuantizeChannel(color.g, 255.0)),
            float(QuantizeChannel(color.b, 255.0)),
            float(QuantizeChannel(color.a, 255.0))) / 255.0;
#else
        oColor = uvec4(
            QuantizeChannel(color.r, 255.0),
            QuantizeChannel(color.g, 255.0),
            QuantizeChannel(color.b, 255.0),
            QuantizeChannel(color.a, 255.0));
#endif
    }
    else
    {
#ifdef FILTERABLE_TEXTURE_CACHE
        oColor = vec4(
            float(QuantizeChannel(color.b, 255.0)),
            float(QuantizeChannel(color.g, 255.0)),
            float(QuantizeChannel(color.r, 255.0)),
            float(QuantizeChannel(color.a, 255.0))) / 255.0;
#else
        oColor = uvec4(
            QuantizeChannel(color.b, 255.0),
            QuantizeChannel(color.g, 255.0),
            QuantizeChannel(color.r, 255.0),
            QuantizeChannel(color.a, 255.0));
#endif
    }
}
