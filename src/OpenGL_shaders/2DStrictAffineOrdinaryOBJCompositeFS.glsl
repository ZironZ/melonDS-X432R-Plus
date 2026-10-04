// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D StrictAffineBaseTex;
uniform sampler2D UpscaledTopColorTex;
uniform sampler2D UpscaledSecondColorTex;
uniform sampler2D UpscaledMetaTex;
uniform sampler2D UpscaledCoverageTex;
uniform sampler2DArray NativeOBJLayerTex;
uniform sampler2DArray StrictAffineOBJLayerTex;
uniform int uScaleFactor;
uniform int uNativeStackBGMask;
uniform bool uDebugTintBySource;
uniform bool uBuildAffineUnderlay;
uniform uvec4 uOrdinaryEquivalentAffineMask;

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

ivec4 QuantizeColor(vec4 color)
{
    return ivec4(clamp(color, 0.0, 1.0) * 255.0) >> ivec4(2, 2, 2, 3);
}

bool IsOrdinaryEquivalentAffine(int spriteIndex)
{
    uint word = uOrdinaryEquivalentAffineMask[spriteIndex >> 5];
    return (word & (1u << uint(spriteIndex & 31))) != 0u;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 baseColor = texelFetch(StrictAffineBaseTex, coord, 0);
    vec4 meta = texelFetch(UpscaledMetaTex, coord, 0);
    int sourceMask1 = int((meta.r * 255.0) + 0.5);
    int sourceMask2 = int((meta.g * 255.0) + 0.5);
    const int objMask = 1 << 4;
    bool ordinaryOBJWinner = false;
    vec4 nativeOBJCoverage = vec4(0.0);
    if (sourceMask1 == objMask)
    {
        // Native OBJ layer 2 carries the class of the actual native winner.
        // The strict-affine base already contains affine OBJ at output
        // resolution, so only an ordinary native OBJ winner is reconstructed.
        ivec2 nativeCoord = coord / uScaleFactor;
        nativeOBJCoverage = texelFetch(NativeOBJLayerTex,
                                       ivec3(nativeCoord, 2), 0);
        int nativeOBJIndex = int(nativeOBJCoverage.b * 255.0 + 0.5);
        if (nativeOBJCoverage.g > 0.5 &&
            !IsOrdinaryEquivalentAffine(nativeOBJIndex))
        {
            if (uBuildAffineUnderlay && sourceMask2 == objMask)
            {
                vec4 secondColor = texelFetch(UpscaledSecondColorTex, coord, 0);
                vec4 coverage = texelFetch(UpscaledCoverageTex, coord, 0);
                float presentationCoverage = smoothstep(
                    0.15, 0.45, clamp(coverage.g, 0.0, 1.0));
                vec4 underlay = mix(baseColor, secondColor,
                                    presentationCoverage);
                ivec4 quantized = QuantizeColor(underlay);
                oColor = vec4(vec3(quantized.rgb << 2) / 255.0, 1.0);
                return;
            }
            oColor = baseColor;
            return;
        }
        ordinaryOBJWinner = true;
    }
    else if ((sourceMask1 & uNativeStackBGMask) == 0)
    {
        oColor = baseColor;
        return;
    }

    vec4 enhancedOBJ = texelFetch(UpscaledTopColorTex, coord, 0);
    vec4 coverage = texelFetch(UpscaledCoverageTex, coord, 0);
    float presentationCoverage = smoothstep(0.15, 0.45, clamp(coverage.r, 0.0, 1.0));
    int packedInfo = int((meta.b * 255.0) + 0.5);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    int specialType = packedInfo & 0x3;
    int ordinaryPriority = (packedInfo >> 2) & 0x7;
    bool blendAllowed = (packedFlags & 0x2) != 0;
    bool presentationAfterBrightness =
        (uBlendEffect == 2 || uBlendEffect == 3) &&
        (uBlendCnt & sourceMask1) != 0 && blendAllowed;
    vec4 topColor = presentationAfterBrightness
        ? enhancedOBJ
        : vec4(mix(baseColor.rgb, enhancedOBJ.rgb,
                   presentationCoverage),
               mix(baseColor.a, enhancedOBJ.a,
                   presentationCoverage));

    if (ordinaryOBJWinner && !uBuildAffineUnderlay)
    {
        // Affine and ordinary OBJ can cross within one native pixel after
        // affine placement. Preserve the DS OBJ ordering when that happens
        // instead of letting the native-stack branch cover the affine winner.
        vec4 affineCoverage = texelFetch(StrictAffineOBJLayerTex,
                                         ivec3(coord, 2), 0);
        if (affineCoverage.g > 0.5)
        {
            vec4 affineFlags = texelFetch(StrictAffineOBJLayerTex,
                                          ivec3(coord, 1), 0);
            int ordinaryIndex = int((nativeOBJCoverage.b * 255.0) + 0.5);
            int affineIndex = int((affineCoverage.b * 255.0) + 0.5);
            int affinePriority = int((affineFlags.a * 255.0) + 0.5);
            int ordinaryTotalPriority = ordinaryPriority * 128 + ordinaryIndex;
            int affineTotalPriority = affinePriority * 128 + affineIndex;
            if (affineTotalPriority < ordinaryTotalPriority)
            {
                oColor = baseColor;
                return;
            }
        }
    }

    ivec4 col1 = QuantizeColor(topColor);
    ivec4 col2 = QuantizeColor(baseColor);
    int effect = 0;
    int eva = 0;
    int evb = 0;
    int evy = uBlendCoef[2];

    if ((specialType != 0) &&
        ((uBlendCnt & (sourceMask2 << 8)) != 0))
    {
        if (specialType == 2)
        {
            effect = 1;
            eva = uBlendCoef[0];
            evb = uBlendCoef[1];
        }
        else
        {
            effect = 1;
            eva = col1.a;
            evb = 16 - eva;
        }
    }
    else if (((uBlendCnt & sourceMask1) != 0) && blendAllowed)
    {
        effect = uBlendEffect;
        if (effect == 1)
        {
            if ((uBlendCnt & (sourceMask2 << 8)) != 0)
            {
                eva = uBlendCoef[0];
                evb = uBlendCoef[1];
            }
            else
            {
                effect = 0;
            }
        }
    }

    if (effect == 1)
    {
        col1 = ((col1 * eva) + (col2 * evb) + 0x8) >> 4;
        col1 = min(col1, 0x3F);
    }
    else if (effect == 2)
    {
        col1 = col1 + ((((0x3F - col1) * evy) + 0x8) >> 4);
    }
    else if (effect == 3)
    {
        col1 = col1 - (((col1 * evy) + 0x7) >> 4);
    }

    vec3 finalColor = vec3(col1.rgb << 2) / 255.0;
    if (presentationAfterBrightness)
    {
        // StrictAffineBaseTex already contains the correctly processed
        // underlay. Apply brightness to the reconstructed OBJ alone, then
        // consume presentation coverage, avoiding a second brightness pass
        // over the underlay contribution at softened edges.
        finalColor = mix(baseColor.rgb, finalColor,
                         presentationCoverage);
    }
    if (uDebugTintBySource)
    {
        float luma = dot(finalColor, vec3(0.299, 0.587, 0.114));
        finalColor = mix(finalColor * 0.2,
                         vec3(1.0, 1.0, 0.2) * max(luma, 0.35),
                         0.85);
    }

    oColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
