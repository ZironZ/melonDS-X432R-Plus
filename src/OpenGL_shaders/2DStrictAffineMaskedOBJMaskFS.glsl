// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D UpscaledMetaTex;
uniform sampler2DArray NativeOBJLayerTex;
uniform sampler2DArray StrictAffineOBJLayerTex;
uniform int uScaleFactor;
uniform int uOwnerMask;

out vec4 oColor;

const int objMask = 1 << 4;
const int backdropMask = 1 << 5;

ivec2 ClampCoord(ivec2 coord)
{
    return clamp(coord, ivec2(0), textureSize(UpscaledMetaTex, 0) - ivec2(1));
}

bool AffineOBJWinsAt(ivec2 coord, ivec4 meta)
{
    vec4 affineCoverage = texelFetch(StrictAffineOBJLayerTex,
                                     ivec3(coord, 2), 0);
    if (affineCoverage.g <= 0.5)
        return false;

    vec4 affineFlags = texelFetch(StrictAffineOBJLayerTex,
                                  ivec3(coord, 1), 0);
    int affinePriority = int(affineFlags.a * 255.0 + 0.5);
    int affineIndex = int(affineCoverage.b * 255.0 + 0.5);
    int affineTotalPriority = affinePriority * 128 + affineIndex;

    int nativeMask = meta.r;
    int nativePriority = (meta.b >> 2) & 0x7;
    if (nativeMask == backdropMask)
        return true;

    if (nativeMask == objMask)
    {
        ivec2 nativeCoord = coord / uScaleFactor;
        vec4 nativeFlags = texelFetch(NativeOBJLayerTex,
                                      ivec3(nativeCoord, 1), 0);
        vec4 nativeCoverage = texelFetch(NativeOBJLayerTex,
                                         ivec3(nativeCoord, 2), 0);
        int nativeOBJPriority = int(nativeFlags.a * 255.0 + 0.5);
        int nativeOBJIndex = int(nativeCoverage.b * 255.0 + 0.5);
        int nativeTotalPriority = nativeOBJPriority * 128 + nativeOBJIndex;
        bool sameNativeAffine = nativeCoverage.g > 0.5 &&
                                affineTotalPriority == nativeTotalPriority;
        return sameNativeAffine || affineTotalPriority < nativeTotalPriority;
    }

    // OBJ wins ties against BG layers in the DS compositor.
    return (nativeMask & 0xF) != 0 && affinePriority <= nativePriority;
}

bool VisibleOwnerAt(ivec2 uncheckedCoord)
{
    ivec2 coord = ClampCoord(uncheckedCoord);
    ivec4 meta = ivec4(texelFetch(UpscaledMetaTex, coord, 0) * 255.0 + 0.5);

    // A positive owner is a set of ordinary BGs selected from the native
    // semantic top mask. Each pixel reports exactly one BG owner bit, while
    // the caller may request several enhanced text BGs in one union contour.
    // Preserve binary ownership and remove output samples where a newly placed
    // affine OBJ wins over one of those native BGs.
    if (uOwnerMask > 0)
        return (meta.r & uOwnerMask) != 0 && !AffineOBJWinsAt(coord, meta);

    bool affineWinner = AffineOBJWinsAt(coord, meta);
    if (uOwnerMask == 0)
        return affineWinner;

    bool ordinaryWinner = false;
    if (!affineWinner && meta.r == objMask)
    {
        ivec2 nativeCoord = coord / uScaleFactor;
        vec4 nativeCoverage = texelFetch(NativeOBJLayerTex,
                                         ivec3(nativeCoord, 2), 0);
        // Keep identity/ordinary-equivalent affine roles out of this mask.
        // They can cross into transformed affine treatment over time, while
        // genuinely ordinary UI OBJ remain in the reconstructed native stack.
        ordinaryWinner = nativeCoverage.g <= 0.5;
    }

    // Negative sentinel -1 selects ordinary/native-stack OBJ only; -2 selects
    // ordinary plus transformed affine OBJ. Positive values remain BG masks.
    return ordinaryWinner || (uOwnerMask == -2 && affineWinner);
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    float ownership = VisibleOwnerAt(coord) ? 1.0 : 0.0;
    oColor = vec4(ownership, 0.0, 0.0, 1.0);
}
