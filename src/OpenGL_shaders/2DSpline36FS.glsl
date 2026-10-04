// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform vec2 uOutputSize;
uniform vec2 uSourceShift;
uniform sampler2D Source;
uniform sampler2D AlphaSource;
uniform bool uUseCoverageSource;
uniform bool uTransparentSourceAware;
uniform bool uBoundedCoverageAlpha;
uniform bool uResolvePremultipliedRGB;
uniform bool uBilinearAlpha;

out vec4 oColor;

float Spline36Weight(float x)
{
    x = abs(x);

    if (x < 1.0)
        return (((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0);
    else if (x < 2.0)
        return (((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0);
    else if (x < 3.0)
        return (((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0);

    return 0.0;
}

void main()
{
    vec2 srcSize = vec2(textureSize(Source, 0));
    vec2 srcCoord = (gl_FragCoord.xy * srcSize / uOutputSize) - vec2(0.5) + uSourceShift;
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);
    ivec2 texSize = textureSize(Source, 0);
    ivec2 nearestCoord = clamp(ivec2(floor(srcCoord + vec2(0.5))),
                               ivec2(0), texSize - ivec2(1));
    vec4 nearestColor = texelFetch(Source, nearestCoord, 0);
    if (uTransparentSourceAware && nearestColor.a <= 0.0)
    {
        oColor = vec4(0.0);
        return;
    }

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

    vec4 accum = vec4(0.0);
    vec4 minColor = vec4(1.0);
    vec4 maxColor = vec4(0.0);
    float totalWeight = 0.0;

    for (int y = 0; y < 6; y++)
    {
        float wyNorm = wy[y] / wysum;
        int sampleY = clamp(baseCoord.y + y - 2, 0, texSize.y - 1);

        for (int x = 0; x < 6; x++)
        {
            float weight = (wx[x] / wxsum) * wyNorm;
            int sampleX = clamp(baseCoord.x + x - 2, 0, texSize.x - 1);
            vec4 sampleColor = texelFetch(Source, ivec2(sampleX, sampleY), 0);
            if (uUseCoverageSource)
                sampleColor.a = texelFetch(AlphaSource, ivec2(sampleX, sampleY), 0).a;
            if (uTransparentSourceAware && sampleColor.a <= 0.0)
                continue;
            accum += sampleColor * weight;
            minColor = min(minColor, sampleColor);
            maxColor = max(maxColor, sampleColor);
            totalWeight += weight;
        }
    }

    vec4 color;
    if (totalWeight <= 0.00001)
        color = uTransparentSourceAware
            ? nearestColor
            : texelFetch(Source, clamp(baseCoord, ivec2(0), texSize - ivec2(1)), 0);
    else
        color = accum / totalWeight;

    color = clamp(color, minColor, maxColor);
    color = clamp(color, 0.0, 1.0);
    if (uBoundedCoverageAlpha)
    {
        // Coverage interpolation must not create an exterior lobe solely
        // from the negative filter taps. Keep the reconstructed value inside
        // the range of the samples that bracket this position. Fractional
        // coverage remains fractional; there is no alpha cutoff.
        float minAlpha = 1.0;
        float maxAlpha = 0.0;
        for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
        {
            float alpha = texelFetch(Source,
                clamp(baseCoord + ivec2(x, y), ivec2(0), texSize - ivec2(1)), 0).a;
            if (uUseCoverageSource)
                alpha = texelFetch(AlphaSource,
                    clamp(baseCoord + ivec2(x, y), ivec2(0), texSize - ivec2(1)), 0).a;
            minAlpha = min(minAlpha, alpha);
            maxAlpha = max(maxAlpha, alpha);
        }
        color.a = clamp(color.a, minAlpha, maxAlpha);
    }
    if (uTransparentSourceAware)
        color.a = nearestColor.a;
    if (uBilinearAlpha)
    {
        // Keep the midpoint-alpha mapping independent of the RGB phase shift.
        // Texture alpha is finished here without a second output pass.
        vec2 alphaCoord = (gl_FragCoord.xy * vec2(texSize) / uOutputSize) - vec2(0.5);
        ivec2 alphaBase = ivec2(floor(alphaCoord));
        vec2 alphaFrac = fract(alphaCoord);
        ivec2 hi = texSize - ivec2(1);
        float a00 = texelFetch(Source, clamp(alphaBase, ivec2(0), hi), 0).a;
        float a10 = texelFetch(Source, clamp(alphaBase + ivec2(1, 0), ivec2(0), hi), 0).a;
        float a01 = texelFetch(Source, clamp(alphaBase + ivec2(0, 1), ivec2(0), hi), 0).a;
        float a11 = texelFetch(Source, clamp(alphaBase + ivec2(1, 1), ivec2(0), hi), 0).a;
        color.a = clamp(mix(mix(a00, a10, alphaFrac.x), mix(a01, a11, alphaFrac.x), alphaFrac.y), 0.0, 1.0);
    }
    if (uResolvePremultipliedRGB)
    {
        // Keep the existing straight-RGB texture contract. Only reconstructed
        // display alpha bounds color here; native ownership stays elsewhere.
        color.rgb = color.a > 0.0
            ? clamp(color.rgb, vec3(0.0), vec3(color.a)) / color.a
            : vec3(0.0);
    }
    oColor = color;
}
