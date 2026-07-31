// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D Source3DTex;
uniform int uFilterMode;
uniform bool uCoverageAware;
uniform bool uRepresentativeSemantics;
uniform bool uSplitSemantics;

smooth in vec4 fTexcoord;

out vec4 oVisualResolve;
out vec4 oSemantics;
out vec4 oCompositorInput;

const int Filter_Area = 0;
const int Filter_Linear = 1;
const int Filter_Tent = 2;
const int MaxScale = 16;
const int MaxTentScale = MaxScale * 2;

struct ResolveResult
{
    vec4 color;
    float coverage;
};

vec4 FetchClamped(ivec2 coord)
{
    ivec2 size = textureSize(Source3DTex, 0);
    return texelFetch(Source3DTex, clamp(coord, ivec2(0), size - ivec2(1)), 0);
}

float SamplePresence(vec4 sampleColor)
{
    return sampleColor.a > 0.0001 ? 1.0 : 0.0;
}

ResolveResult ResolveColor(vec4 sum, float count, vec3 coveredRGBSum, float coveredWeight)
{
    vec4 color = sum / max(count, 1.0);
    if ((uCoverageAware || uSplitSemantics) && coveredWeight > 0.00001)
        color.rgb = coveredRGBSum / coveredWeight;

    return ResolveResult(color, coveredWeight / max(count, 1.0));
}

ResolveResult SampleArea(ivec2 dstCoord, ivec2 srcSize)
{
    ivec2 dstSize = ivec2(256, 192);
    ivec2 scale = max(srcSize / dstSize, ivec2(1));
    ivec2 start = dstCoord * scale;

    vec4 sum = vec4(0.0);
    vec3 coveredRGBSum = vec3(0.0);
    float coveredWeight = 0.0;
    int count = 0;
    for (int y = 0; y < MaxScale; y++)
    {
        if (y >= scale.y) break;
        for (int x = 0; x < MaxScale; x++)
        {
            if (x >= scale.x) break;
            vec4 sampleColor = FetchClamped(start + ivec2(x, y));
            float coverage = SamplePresence(sampleColor);
            sum += sampleColor;
            coveredRGBSum += sampleColor.rgb * coverage;
            coveredWeight += coverage;
            count++;
        }
    }

    return ResolveColor(sum, float(count), coveredRGBSum, coveredWeight);
}

ResolveResult SampleLinear(ivec2 dstCoord, ivec2 srcSize)
{
    vec2 dstSize = vec2(256.0, 192.0);
    vec2 srcCoord = ((vec2(dstCoord) + vec2(0.5)) / dstSize) * vec2(srcSize) - vec2(0.5);
    ivec2 base = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);

    vec4 sum = vec4(0.0);
    vec3 coveredRGBSum = vec3(0.0);
    float coveredWeight = 0.0;
    float total = 0.0;
    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 2; x++)
        {
            vec2 blend = vec2(x == 0 ? 1.0 - frac.x : frac.x,
                              y == 0 ? 1.0 - frac.y : frac.y);
            float weight = blend.x * blend.y;
            vec4 sampleColor = FetchClamped(base + ivec2(x, y));
            float coverage = SamplePresence(sampleColor) * weight;
            sum += sampleColor * weight;
            coveredRGBSum += sampleColor.rgb * coverage;
            coveredWeight += coverage;
            total += weight;
        }
    }

    return ResolveColor(sum, total, coveredRGBSum, coveredWeight);
}

ResolveResult SampleTent(ivec2 dstCoord, ivec2 srcSize)
{
    ivec2 dstSize = ivec2(256, 192);
    ivec2 scale = max(srcSize / dstSize, ivec2(1));
    vec2 center = (vec2(dstCoord) + vec2(0.5)) * vec2(scale);
    ivec2 base = ivec2(floor(center)) - scale;

    vec4 sum = vec4(0.0);
    vec3 coveredRGBSum = vec3(0.0);
    float coveredWeight = 0.0;
    float total = 0.0;
    for (int y = 0; y < MaxTentScale; y++)
    {
        if (y >= scale.y * 2) break;
        for (int x = 0; x < MaxTentScale; x++)
        {
            if (x >= scale.x * 2) break;

            ivec2 srcCoord = base + ivec2(x, y);
            vec2 sampleCenter = vec2(srcCoord) + vec2(0.5);
            vec2 d = abs(sampleCenter - center) / vec2(scale);
            float weight = max(0.0, 1.0 - d.x) * max(0.0, 1.0 - d.y);
            vec4 sampleColor = FetchClamped(srcCoord);
            float coverage = SamplePresence(sampleColor) * weight;
            sum += sampleColor * weight;
            coveredRGBSum += sampleColor.rgb * coverage;
            coveredWeight += coverage;
            total += weight;
        }
    }

    return ResolveColor(sum, total, coveredRGBSum, coveredWeight);
}

vec4 SampleRepresentative(ivec2 dstCoord, ivec2 srcSize)
{
    ivec2 dstSize = ivec2(256, 192);
    ivec2 scale = max(srcSize / dstSize, ivec2(1));
    ivec2 coord = (dstCoord * scale) + (scale / 2);
    return FetchClamped(coord);
}

void OutputNativeProducts(ResolveResult visualResolve, vec4 representative)
{
    float semanticAlpha = uRepresentativeSemantics
        ? clamp(representative.a, 0.0, 1.0)
        : clamp(visualResolve.color.a, 0.0, 1.0);
    float visualCoverage = clamp(visualResolve.coverage, 0.0, 1.0);
    float semanticPresence = semanticAlpha > 0.0001 ? 1.0 : 0.0;
    oVisualResolve = vec4(visualResolve.color.rgb, visualCoverage);
    oSemantics = vec4(visualCoverage, semanticAlpha, semanticPresence, semanticAlpha);
    oCompositorInput = vec4(visualResolve.color.rgb, semanticAlpha);
}

void main()
{
    ivec2 dstCoord = ivec2(floor(fTexcoord.xy));
    ivec2 srcSize = textureSize(Source3DTex, 0);

    if (srcSize == ivec2(256, 192))
    {
        vec4 color = texelFetch(Source3DTex, dstCoord, 0);
        OutputNativeProducts(ResolveResult(color, SamplePresence(color)), color);
        return;
    }

    ResolveResult visualResolve;
    if (uFilterMode == Filter_Linear)
        visualResolve = SampleLinear(dstCoord, srcSize);
    else if (uFilterMode == Filter_Tent)
        visualResolve = SampleTent(dstCoord, srcSize);
    else
        visualResolve = SampleArea(dstCoord, srcSize);

    OutputNativeProducts(visualResolve, SampleRepresentative(dstCoord, srcSize));
}
