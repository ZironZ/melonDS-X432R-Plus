// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D BaseTex;
uniform sampler2DArray SpecialOBJLayerTex;
uniform sampler2DArray ScaledPresentationTex;
uniform sampler2D UpscaledMetaTex;
uniform int uEVA;
uniform int uEVB;
uniform bool uUsePresentation;
uniform bool uUseSubpixelPresentation;
uniform int uPresentationLayer;
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

bool NativeUnderlayIsTarget2(ivec2 coord, int line)
{
    ivec4 meta = ivec4(texelFetch(UpscaledMetaTex, coord, 0) *
                        255.0 + 0.5);
    // Inside native OBJ coverage, its underlay is the second layer. At a
    // reconstructed fringe outside that coverage, the native top layer is
    // already the background beneath the presented sprite. Reading the
    // second layer there can select an empty slot and make the fringe opaque.
    int underlayMask = meta.r == (1 << 4) ? meta.g : meta.r;
    return (uScanline[line].BlendCnt & (underlayMask << 8)) != 0;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 base = texelFetch(BaseTex, coord, 0);
    if ((WindowSelection(coord) & (1u << 4)) == 0u ||
        NativeFrontBGBlocksOBJ(coord))
    {
        oColor = base;
        return;
    }
    if (uUseSubpixelPresentation)
    {
        // Affine mode-1 OBJ carries real DS material semantics, so do not
        // reconstruct an alpha surface for it. Rasterize its geometry at 2x,
        // run the native Target-2 equation for each covered sample, and only
        // then box-resolve. This shares the normal affine band's geometry grid
        // without adding another neural-scaler dispatch.
        ivec2 baseSubpixel = coord * 2;
        int tapCount = 4;
        ivec2 offsets[4] = ivec2[4](
            ivec2(0, 0), ivec2(1, 0),
            ivec2(0, 1), ivec2(1, 1));
        int line = clamp(coord.y / max(uScaleFactor, 1), 0, 191);
        bool underlayIsTarget2 = NativeUnderlayIsTarget2(coord, line);
        ivec3 underlay = ivec3(clamp(base.rgb, 0.0, 1.0) * 255.0) >>
                          ivec3(2);
        vec3 sum = vec3(0.0);
        for (int tap = 0; tap < tapCount; tap++)
        {
            ivec2 subpixel = baseSubpixel + offsets[tap];
            vec4 specialColor = texelFetch(
                ScaledPresentationTex, ivec3(subpixel, 0), 0);
            ivec4 specialFlags = ivec4(texelFetch(
                ScaledPresentationTex, ivec3(subpixel, 1), 0) *
                255.0 + 0.5);
            ivec3 resolvedColor = underlay;
            if (specialColor.a > 0.0 && specialFlags.r == 1)
            {
                ivec3 foreground = ivec3(clamp(specialColor.rgb,
                                                0.0, 1.0) *
                                          255.0) >> ivec3(2);
                resolvedColor = foreground;
                if (underlayIsTarget2)
                {
                    resolvedColor = ((foreground * uEVA) +
                                     (underlay * uEVB) +
                                     ivec3(8)) >> ivec3(4);
                    resolvedColor = min(resolvedColor, ivec3(63));
                }
            }
            sum += vec3(resolvedColor << ivec3(2)) / 255.0;
        }
        oColor = vec4(clamp(sum / float(tapCount), 0.0, 1.0), base.a);
        return;
    }
    vec4 specialColor = texelFetch(SpecialOBJLayerTex,
                                   ivec3(coord, 0), 0);
    ivec4 specialFlags = ivec4(texelFetch(SpecialOBJLayerTex,
                                          ivec3(coord, 1), 0) *
                                255.0 + 0.5);

    float presentationCoverage = 1.0;
    if (uUsePresentation)
    {
        vec4 presentation = texelFetch(
            ScaledPresentationTex,
            ivec3(coord, uPresentationLayer), 0);
        specialColor = presentation;
        presentationCoverage = clamp(presentation.a, 0.0, 1.0);
    }

    // The bounded policy supplies one normal-source mode-1 operand and proves
    // that BaseTex is its real target-2 stack. Material alpha remains the DS
    // EVA/EVB equation; reconstructed alpha is only exterior presentation
    // coverage for that already-proven operand.
    if (specialColor.a <= 0.0 ||
        (!uUsePresentation && specialFlags.r != 1))
    {
        oColor = base;
        return;
    }

    ivec3 foreground = ivec3(clamp(specialColor.rgb, 0.0, 1.0) *
                              255.0) >> ivec3(2);
    ivec3 underlay = ivec3(clamp(base.rgb, 0.0, 1.0) * 255.0) >>
                      ivec3(2);
    int line = clamp(coord.y / max(uScaleFactor, 1), 0, 191);
    bool underlayIsTarget2 = NativeUnderlayIsTarget2(coord, line);
    ivec3 resolvedColor = foreground;
    if (underlayIsTarget2)
    {
        resolvedColor = ((foreground * uEVA) + (underlay * uEVB) +
                         ivec3(8)) >> ivec3(4);
        resolvedColor = min(resolvedColor, ivec3(63));
    }
    vec3 resolved = vec3(resolvedColor << ivec3(2)) / 255.0;
    oColor = vec4(mix(base.rgb, resolved, presentationCoverage), base.a);
}
