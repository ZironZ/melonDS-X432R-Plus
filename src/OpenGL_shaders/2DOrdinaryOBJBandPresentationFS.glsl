// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D BaseTex;
uniform sampler2DArray ScaledBandTex;
uniform sampler2D UpscaledMetaTex;
uniform int uBandLayer;
uniform bool uUseSeparateCoverage;
uniform bool uUseSubpixelPresentation;
uniform bool uUsePremultipliedPresentation;
uniform int uScaleFactor;
uniform int uOBJPriority;

struct sScanline
{
    ivec2 BGOffset[4];
    ivec4 BGRotscale[2];
    int BackColor;
    uint WinRegs;
    int WinMask;
    ivec4 WinPos;
    bvec4 BGMosaicEnable;
    ivec4 MosaicSize;
    ivec4 BGPrio;
    bool EnableOBJ;
    bool Enable3D;
    int BlendCnt;
    int BlendEffect;
    ivec3 BlendCoef;
};

layout(std140) uniform ubScanlineConfig
{
    sScanline uScanline[192];
};

layout(std140) uniform ubCompositorConfig
{
    ivec4 uBGPrio;
    bool uEnableOBJ;
    bool uEnable3D;
    int uBlendCnt;
    int uBlendEffect;
    ivec3 uBlendCoef;
};

out vec4 oColor;

uint WindowSelection(ivec2 coord)
{
    int scale = max(uScaleFactor, 1);
    int line = clamp(coord.y / scale, 0, 191);
    int xpos = coord.x / scale;
    int winmask = uScanline[line].WinMask;
    bool insideWin0;
    bool insideWin1;

    if (xpos < uScanline[line].WinPos[0])
        insideWin0 = (winmask & (1 << 0)) != 0;
    else if (xpos < uScanline[line].WinPos[1])
        insideWin0 = (winmask & (1 << 1)) != 0;
    else
        insideWin0 = (winmask & (1 << 2)) != 0;

    if (xpos < uScanline[line].WinPos[2])
        insideWin1 = (winmask & (1 << 3)) != 0;
    else if (xpos < uScanline[line].WinPos[3])
        insideWin1 = (winmask & (1 << 4)) != 0;
    else
        insideWin1 = (winmask & (1 << 5)) != 0;

    uint winsel = uScanline[line].WinRegs;
    if (insideWin1)
        winsel >>= 16;
    if (insideWin0)
        winsel = uScanline[line].WinRegs >> 24;
    return winsel;
}

bool NativeFrontBGBlocksOBJ(ivec2 coord)
{
    if (uOBJPriority < 0)
        return false;
    int scale = max(uScaleFactor, 1);
    int line = clamp(coord.y / scale, 0, 191);
    int sourceMask = int(texelFetch(UpscaledMetaTex, coord, 0).r *
                         255.0 + 0.5);
    for (int bg = 0; bg < 4; bg++)
    {
        if ((sourceMask & (1 << bg)) != 0 &&
            uScanline[line].BGPrio[bg] < uOBJPriority)
            return true;
    }
    return false;
}

ivec3 CompleteOBJForeground(ivec2 coord, vec3 color,
                            bool overrideEffectsEnabled,
                            bool effectsEnabledOverride)
{
    ivec3 foreground = ivec3(clamp(color, 0.0, 1.0) * 255.0) >>
                       ivec3(2);
    int packedFlags = int(texelFetch(UpscaledMetaTex, coord, 0).a *
                          255.0 + 0.5);
    bool effectsEnabled = overrideEffectsEnabled
        ? effectsEnabledOverride : (packedFlags & 0x2) != 0;
    bool objIsTarget1 = (uBlendCnt & (1 << 4)) != 0;
    if (effectsEnabled && objIsTarget1)
    {
        int evy = uBlendCoef[2];
        if (uBlendEffect == 2)
            foreground += ((((ivec3(0x3F) - foreground) * evy) +
                            ivec3(0x8)) >> ivec3(4));
        else if (uBlendEffect == 3)
            foreground -= (((foreground * evy) + ivec3(0x7)) >>
                           ivec3(4));
    }
    return clamp(foreground, ivec3(0), ivec3(0x3F));
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 base = texelFetch(BaseTex, coord, 0);
    uint winsel = WindowSelection(coord);
    if ((winsel & (1u << 4)) == 0u ||
        NativeFrontBGBlocksOBJ(coord))
    {
        oColor = base;
        return;
    }
    if (uUseSubpixelPresentation)
    {
        ivec2 baseSubpixel = coord * 2;
        int tapCount = 4;
        ivec2 offsets[4] = ivec2[4](
            ivec2(0, 0), ivec2(1, 0),
            ivec2(0, 1), ivec2(1, 1));
        vec3 sum = vec3(0.0);
        for (int tap = 0; tap < tapCount; tap++)
        {
            ivec2 subpixel = baseSubpixel + offsets[tap];
            vec4 color = texelFetch(ScaledBandTex,
                                    ivec3(subpixel, 0), 0);
            float coverage;
            vec3 foregroundColor;
            if (uUsePremultipliedPresentation)
            {
                coverage = clamp(color.a, 0.0, 1.0);
                foregroundColor = coverage > 0.00001
                    ? color.rgb / coverage : vec3(0.0);
            }
            else
            {
                vec4 coverageState = texelFetch(ScaledBandTex,
                                                ivec3(subpixel, 2), 0);
                coverage = coverageState.g > 0.5
                    ? clamp(coverageState.r, 0.0, 1.0)
                    : 0.0;
                foregroundColor = color.rgb;
            }

            // Match the localized affine-OBJ compositor: each subpixel is a
            // completed DS-precision foreground/underlay result before the
            // four colors are box-resolved.
            ivec3 underlay = ivec3(clamp(base.rgb, 0.0, 1.0) *
                                    255.0) >> ivec3(2);
            ivec3 foreground = CompleteOBJForeground(
                coord, foregroundColor,
                uUsePremultipliedPresentation,
                (winsel & (1u << 5)) != 0u);
            vec3 mixed = mix(vec3(underlay), vec3(foreground), coverage);
            ivec3 quantized = ivec3(clamp(mixed + vec3(0.5),
                                          vec3(0.0), vec3(63.0)));
            sum += vec3(quantized << 2) / 255.0;
        }
        oColor = vec4(clamp(sum / float(tapCount), 0.0, 1.0), base.a);
        return;
    }

    vec4 band = texelFetch(ScaledBandTex,
                           ivec3(coord, uBandLayer), 0);
    float coverage = clamp(band.a, 0.0, 1.0);
    vec3 foregroundColor = band.rgb;
    if (uUsePremultipliedPresentation)
    {
        foregroundColor = coverage > 0.00001
            ? band.rgb / coverage : vec3(0.0);
    }
    else if (uUseSeparateCoverage)
    {
        vec4 presentationCoverage = texelFetch(
            ScaledBandTex, ivec3(coord, 2), 0);
        coverage = presentationCoverage.g > 0.5
            ? clamp(presentationCoverage.r, 0.0, 1.0)
            : 0.0;
    }
    ivec3 foreground = CompleteOBJForeground(coord, foregroundColor,
                                               false, false);
    vec3 completedForeground = vec3(foreground << ivec3(2)) / 255.0;
    oColor = vec4(mix(base.rgb, completedForeground, coverage), base.a);
}
