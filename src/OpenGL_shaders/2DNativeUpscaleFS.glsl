// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D NativeTopColorTex;
uniform sampler2D NativeSecondColorTex;
uniform sampler2D NativeMetaTex;

uniform int uScaleFactor;
uniform bool uLegacyFilterBehavior;

smooth in vec4 fTexcoord;

out vec4 oTopColor;
out vec4 oSecondColor;
out vec4 oMeta;
out vec4 oCoverage;

float Spline36Weight(float x)
{
    x = abs(x);

    if (x < 1.0)
    {
        return (((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0);
    }
    else if (x < 2.0)
    {
        return (((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0);
    }
    else if (x < 3.0)
    {
        return (((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0);
    }

    return 0.0;
}

ivec4 ReadMeta(ivec2 coord)
{
    ivec2 metaSize = textureSize(NativeMetaTex, 0);
    ivec2 clampedCoord = clamp(coord, ivec2(0), metaSize - ivec2(1));
    return ivec4(texelFetch(NativeMetaTex, clampedCoord, 0) * 255.0 + 0.5);
}

bool MatchesTopClass(ivec4 sampleMeta, ivec4 centerMeta)
{
    int samplePackedInfo = sampleMeta.b;
    int centerPackedInfo = centerMeta.b;

    return (sampleMeta.r == centerMeta.r) &&
           (((samplePackedInfo >> 2) & 0x7) == ((centerPackedInfo >> 2) & 0x7)) &&
           ((samplePackedInfo & 0x3) == (centerPackedInfo & 0x3));
}

bool MatchesSecondClass(ivec4 sampleMeta, ivec4 centerMeta)
{
    int samplePackedInfo = sampleMeta.b;
    int centerPackedInfo = centerMeta.b;

    return (sampleMeta.g == centerMeta.g) &&
           (((samplePackedInfo >> 5) & 0x7) == ((centerPackedInfo >> 5) & 0x7));
}

vec4 SampleSpline36(sampler2D source, vec2 dstCoord, ivec2 centerCoord, ivec4 centerMeta, bool secondLayer, out float exactCoverage)
{
    ivec2 srcSize = textureSize(source, 0);
    vec2 srcCoord = (dstCoord + vec2(0.5)) / float(uScaleFactor) - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);

    float wx[6];
    float wy[6];
    float wxsum = 0.0;
    float wysum = 0.0;

    for (int i = 0; i < 6; i++)
    {
        wx[i] = Spline36Weight(float(i - 2) - frac.x);
        wy[i] = Spline36Weight(float(i - 2) - frac.y);
        wxsum += wx[i];
        wysum += wy[i];
    }

    vec4 color = vec4(0.0);
    vec4 minColor = vec4(1.0);
    vec4 maxColor = vec4(0.0);
    float totalWeight = 0.0;
    float coverageNumerator = 0.0;
    float coverageDenominator = 0.0;
    for (int y = 0; y < 6; y++)
    {
        float wyNorm = wy[y] / wysum;
        int sampleY = clamp(baseCoord.y + y - 2, 0, srcSize.y - 1);

        for (int x = 0; x < 6; x++)
        {
            float weight = (wx[x] / wxsum) * wyNorm;
            int sampleX = clamp(baseCoord.x + x - 2, 0, srcSize.x - 1);
            ivec2 sampleCoord = ivec2(sampleX, sampleY);
            ivec4 sampleMeta = ReadMeta(sampleCoord);
            bool exactMatch = secondLayer
                ? MatchesSecondClass(sampleMeta, centerMeta)
                : MatchesTopClass(sampleMeta, centerMeta);

            float coverageWeight = max(weight, 0.0);
            coverageDenominator += coverageWeight;
            if (exactMatch)
                coverageNumerator += coverageWeight;

            float classWeight = 1.0;
            if (!uLegacyFilterBehavior)
            {
                classWeight = exactMatch ? 1.0 : 0.0;
                if (classWeight <= 0.0)
                    continue;
            }

            vec4 sampleColor = texelFetch(source, ivec2(sampleX, sampleY), 0);
            sampleColor.rgb *= sampleColor.a;
            float weightedTap = weight * classWeight;
            color += sampleColor * weightedTap;
            minColor = min(minColor, sampleColor);
            maxColor = max(maxColor, sampleColor);
            totalWeight += weightedTap;
        }
    }

    exactCoverage = uLegacyFilterBehavior ? 1.0 : clamp(coverageNumerator / max(coverageDenominator, 0.00001), 0.0, 1.0);

    if (totalWeight <= 0.00001)
        return texelFetch(source, centerCoord, 0);

    color /= totalWeight;
    if (!uLegacyFilterBehavior)
        color = clamp(color, minColor, maxColor);
    if (color.a > 0.00001)
        color.rgb /= color.a;

    return clamp(color, 0.0, 1.0);
}

void main()
{
    vec2 dstCoord = floor(fTexcoord.zw);
    ivec2 metaCoord = ivec2(dstCoord) / uScaleFactor;
    ivec4 centerMeta = ReadMeta(metaCoord);
    float topCoverage = 1.0;
    float secondCoverage = 1.0;

    oTopColor = SampleSpline36(NativeTopColorTex, dstCoord, metaCoord, centerMeta, false, topCoverage);
    oSecondColor = SampleSpline36(NativeSecondColorTex, dstCoord, metaCoord, centerMeta, true, secondCoverage);
    oMeta = vec4(centerMeta) / 255.0;
    oCoverage = vec4(topCoverage, secondCoverage, 0.0, 1.0);
}
