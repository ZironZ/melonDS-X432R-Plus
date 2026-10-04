// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D BaseTex;
uniform sampler2DArray PresentationOBJLayerTex;
uniform sampler2D UpscaledMetaTex;
uniform sampler2DArray NativeOBJLayerTex;
uniform sampler2DArray StrictAffineOBJLayerTex;
uniform sampler2D AffineUnderlayTex;
uniform sampler2DArray SubpixelPresentationOBJLayerTex;
uniform int uScaleFactor;
uniform bool uSubpixelPresentationCoverage;
uniform bool uPremultipliedSubpixelPresentation;

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

ivec3 CompletePresentationForeground(vec4 color, vec4 flags,
                                     bool effectsEnabled)
{
    ivec3 foreground = ivec3(clamp(color.rgb, 0.0, 1.0) * 255.0) >>
                       ivec3(2);
    int specialType = int(flags.r * 255.0 + 0.5);
    bool objIsTarget1 = (uBlendCnt & (1 << 4)) != 0;
    if (specialType == 0 && effectsEnabled && objIsTarget1)
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

bool CandidateWins(ivec2 coord, vec4 presentationFlags,
                   vec4 presentationCoverage,
                   out bool candidateIsSemanticAffineTop,
                   out bool effectsEnabled)
{
    int candidatePriority = int(presentationFlags.a * 255.0 + 0.5);
    int candidateIndex = int(presentationCoverage.b * 255.0 + 0.5);
    int candidateTotalPriority = candidatePriority * 128 + candidateIndex;

    candidateIsSemanticAffineTop = false;
    effectsEnabled = false;
    vec4 affineCoverage = texelFetch(StrictAffineOBJLayerTex,
                                     ivec3(coord, 2), 0);
    if (affineCoverage.g > 0.5)
    {
        vec4 affineFlags = texelFetch(StrictAffineOBJLayerTex,
                                      ivec3(coord, 1), 0);
        int affinePriority = int(affineFlags.a * 255.0 + 0.5);
        int affineIndex = int(affineCoverage.b * 255.0 + 0.5);
        int affineTotalPriority = affinePriority * 128 + affineIndex;
        if (candidateTotalPriority > affineTotalPriority)
            return false;
        candidateIsSemanticAffineTop =
            candidateTotalPriority == affineTotalPriority;
    }

    ivec4 meta = ivec4(texelFetch(UpscaledMetaTex, coord, 0) *
                        255.0 + 0.5);
    effectsEnabled = (meta.a & 0x2) != 0;
    int sourceMask = meta.r;
    int topPriority = (meta.b >> 2) & 0x7;
    const int objMask = 1 << 4;
    const int backdropMask = 1 << 5;

    if (sourceMask == backdropMask)
        return true;
    if (sourceMask == objMask)
    {
        ivec2 nativeCoord = coord / uScaleFactor;
        vec4 nativeFlags = texelFetch(NativeOBJLayerTex,
                                      ivec3(nativeCoord, 1), 0);
        vec4 nativeCoverage = texelFetch(NativeOBJLayerTex,
                                         ivec3(nativeCoord, 2), 0);
        int nativePriority = int(nativeFlags.a * 255.0 + 0.5);
        int nativeIndex = int(nativeCoverage.b * 255.0 + 0.5);
        int nativeTotalPriority = nativePriority * 128 + nativeIndex;
        bool sameNativeAffine = nativeCoverage.g > 0.5 &&
                                candidateTotalPriority == nativeTotalPriority;
        return sameNativeAffine ||
               candidateTotalPriority < nativeTotalPriority;
    }
    if ((sourceMask & 0xF) != 0)
    {
        // OBJ is in front of a BG at equal numeric priority.
        return candidatePriority <= topPriority;
    }
    return false;
}

vec4 CompositeDirectCoverage(vec4 underlayColor,
                             vec4 presentationColor,
                             vec4 presentationFlags,
                             float presentationCoverage,
                             bool effectsEnabled)
{
    float coverage = clamp(presentationCoverage, 0.0, 1.0);
    ivec3 underlay = ivec3(clamp(underlayColor.rgb, 0.0, 1.0) *
                           255.0) >> ivec3(2);
    ivec3 foreground = CompletePresentationForeground(
        presentationColor, presentationFlags, effectsEnabled);
    vec3 mixed = mix(vec3(underlay), vec3(foreground), coverage);
    ivec3 quantized = ivec3(clamp(mixed + vec3(0.5),
                                  vec3(0.0), vec3(63.0)));
    return vec4(vec3(quantized << 2) / 255.0, underlayColor.a);
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 baseColor = texelFetch(BaseTex, coord, 0);
    vec4 affineUnderlay = texelFetch(AffineUnderlayTex, coord, 0);

    if (uSubpixelPresentationCoverage)
    {
        ivec2 baseSubpixel = coord * 2;
        ivec2 offsets[4] = ivec2[4](
            ivec2(0, 0), ivec2(1, 0),
            ivec2(0, 1), ivec2(1, 1));
        vec4 semanticAffineCoverage = texelFetch(
            StrictAffineOBJLayerTex, ivec3(coord, 2), 0);
        vec4 noCandidateColor = semanticAffineCoverage.g > 0.5
            ? affineUnderlay : baseColor;
        vec4 sum = vec4(0.0);
        bool unsupportedPremultipliedMaterial = false;

        for (int tap = 0; tap < 4; tap++)
        {
            ivec2 subpixel = baseSubpixel + offsets[tap];
            vec4 color = texelFetch(SubpixelPresentationOBJLayerTex,
                                    ivec3(subpixel, 0), 0);
            vec4 flags = texelFetch(SubpixelPresentationOBJLayerTex,
                                    ivec3(subpixel, 1), 0);
            vec4 coverage = texelFetch(SubpixelPresentationOBJLayerTex,
                                       ivec3(subpixel, 2), 0);
            if (coverage.g <= 0.5)
            {
                sum += noCandidateColor;
                continue;
            }

            int specialType = int(flags.r * 255.0 + 0.5);
            if (uPremultipliedSubpixelPresentation && specialType != 0)
            {
                // Layer 0 is a union of normal affine OBJ only. Layers 1/2
                // retain the depth-selected semantic winner, so a special
                // material here cannot safely consume the normal union.
                // Preserve the already-resolved semantic result instead of
                // treating the unsupported candidate as a transparent hole.
                unsupportedPremultipliedMaterial = true;
                break;
            }

            bool candidateIsSemanticAffineTop = false;
            bool effectsEnabled = false;
            if (!CandidateWins(coord, flags, coverage,
                               candidateIsSemanticAffineTop,
                               effectsEnabled))
            {
                sum += baseColor;
                continue;
            }

            // AffineUnderlayTex is the completed scene with affine OBJ
            // removed and reconstructed ordinary/UI OBJ restored. Each tap
            // therefore produces a finished color before the box resolve.
            float presentationCoverage =
                uPremultipliedSubpixelPresentation
                    ? clamp(color.a, 0.0, 1.0)
                    : clamp(coverage.r, 0.0, 1.0);
            vec4 presentationColor = color;
            if (uPremultipliedSubpixelPresentation)
            {
                presentationColor.rgb /= max(
                    presentationCoverage, 0.00001);
                presentationColor.rgb = clamp(
                    presentationColor.rgb, 0.0, 1.0);
                presentationColor.a = 1.0;
            }
            sum += CompositeDirectCoverage(affineUnderlay,
                                           presentationColor, flags,
                                           presentationCoverage,
                                           effectsEnabled);
        }

        if (unsupportedPremultipliedMaterial)
        {
            oColor = baseColor;
            return;
        }

        oColor = vec4(clamp((sum * 0.25).rgb, 0.0, 1.0), baseColor.a);
        return;
    }

    vec4 presentationColor = texelFetch(PresentationOBJLayerTex,
                                        ivec3(coord, 0), 0);
    vec4 presentationFlags = texelFetch(PresentationOBJLayerTex,
                                        ivec3(coord, 1), 0);
    vec4 presentationCoverage = texelFetch(PresentationOBJLayerTex,
                                           ivec3(coord, 2), 0);
    // This pass owns the complete presentation edge. Semantic presence stays
    // binary in the strict-affine compositor; reconstructed presentation
    // coverage is applied exactly once here for native-present and recovered
    // samples.
    if (presentationCoverage.g <= 0.5)
    {
        oColor = baseColor;
        return;
    }

    bool candidateIsSemanticAffineTop = false;
    bool effectsEnabled = false;
    if (!CandidateWins(coord, presentationFlags, presentationCoverage,
                       candidateIsSemanticAffineTop,
                       effectsEnabled))
    {
        oColor = baseColor;
        return;
    }

    vec4 underlayColor = candidateIsSemanticAffineTop
        ? affineUnderlay
        : baseColor;

    // Subpixel and bilinear modes already supply a reconstructed RGB sample
    // and scalar presentation coverage. Apply the complete affine candidate
    // once over its resolved lower stack; do not split its native body and
    // exterior fringe between two ownership systems.
    oColor = CompositeDirectCoverage(underlayColor,
                                     presentationColor,
                                     presentationFlags,
                                     presentationCoverage.r,
                                     effectsEnabled);
}
