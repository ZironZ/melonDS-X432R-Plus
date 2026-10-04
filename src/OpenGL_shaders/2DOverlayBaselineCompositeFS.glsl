// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D OverlayBlackTex;
uniform sampler2D OverlayWhiteTex;
uniform sampler2D OverlayOpaqueWhiteTex;
uniform sampler2D Direct3DTexture;
uniform sampler2D Native3DSemanticsTex;
uniform sampler2D Native3DOperatorTex;
uniform bool uDebugTintBySource;
uniform bool uLegacyUnderlayEndpoint;
uniform bool uDirect3DPresentationSpace;
uniform bool uStraightDirect3DColor;
uniform bool uExplicit3DOperator;
uniform int uScaleFactor;

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

smooth in vec2 fTexcoord;

out vec4 oColor;

ivec2 Direct3DCoord(ivec2 coord)
{
    ivec2 outputSize = textureSize(OverlayBlackTex, 0);
    ivec2 directSize = textureSize(Direct3DTexture, 0);
    vec2 coordScale = vec2(directSize) / vec2(max(outputSize, ivec2(1)));
    ivec2 directCoord = ivec2(floor((vec2(coord) + vec2(0.5)) * coordScale));

    if (!uDirect3DPresentationSpace)
    {
        int line = clamp((coord.y * 192) / max(outputSize.y, 1), 0, 191);
        int bgOffsetX = uScanline[line].BGOffset[0].x;
        float directScaleX = float(directSize.x) / 256.0;
        directCoord.x += int(round(float(bgOffsetX) * directScaleX));
    }

    return directCoord;
}

vec4 FetchDirect3DRaw(ivec2 directCoord)
{
    ivec2 directSize = textureSize(Direct3DTexture, 0);
    if (any(lessThan(directCoord, ivec2(0))) || any(greaterThanEqual(directCoord, directSize)))
        return vec4(0.0);

    return texelFetch(Direct3DTexture, directCoord, 0);
}

vec4 FetchDirect3D(ivec2 coord)
{
    return FetchDirect3DRaw(Direct3DCoord(coord));
}

vec3 Direct3DCompositorEndpoint(vec4 direct3D)
{
    // The native 2D compositor converts layer RGB through 6-bit color before
    // final output. Convert raw 3D RGB into the same endpoint domain so tiny
    // raw 3D discontinuities do not become overlay operator seams.
    const float whiteEndpoint = 252.0 / 255.0;
    vec3 quantized = floor(clamp(direct3D.rgb, 0.0, 1.0) * 255.0 / 4.0) * (4.0 / 255.0);
    return clamp(quantized / whiteEndpoint, 0.0, 1.0);
}

vec3 Direct3DUnderlayColor(vec4 direct3D)
{
    if (uStraightDirect3DColor)
        return clamp(direct3D.rgb, 0.0, 1.0);

    if (uDirect3DPresentationSpace)
        return clamp(direct3D.rgb, 0.0, 1.0) * clamp(direct3D.a, 0.0, 1.0);

    if (uLegacyUnderlayEndpoint)
        return clamp(direct3D.rgb, 0.0, 1.0) * clamp(direct3D.a, 0.0, 1.0);

    return Direct3DCompositorEndpoint(direct3D);
}

bool PresentationEndpointLooksPassThrough(vec3 overlayBlack, vec3 underWeight)
{
    float overlay = max(max(overlayBlack.r, overlayBlack.g), overlayBlack.b);
    float minUnder = min(min(underWeight.r, underWeight.g), underWeight.b);
    float channelSpread = max(max(abs(underWeight.r - underWeight.g),
                                  abs(underWeight.r - underWeight.b)),
                              abs(underWeight.g - underWeight.b));
    return overlay <= (4.0 / 255.0) &&
           minUnder >= 0.95 &&
           channelSpread <= (4.0 / 255.0);
}

float Explicit3DOperatorAlpha(ivec2 outputCoord, vec4 direct3D)
{
    ivec2 outputSize = textureSize(OverlayBlackTex, 0);
    ivec2 nativeSize = textureSize(Native3DSemanticsTex, 0);
    ivec2 nativeCoord = ivec2(floor((vec2(outputCoord) + vec2(0.5)) *
                                    vec2(nativeSize) /
                                    vec2(max(outputSize, ivec2(1)))));
    nativeCoord = clamp(nativeCoord, ivec2(0), nativeSize - ivec2(1));
    vec4 semantics = texelFetch(Native3DSemanticsTex, nativeCoord, 0);
    vec4 operatorSemantics = texelFetch(Native3DOperatorTex,
                                        nativeCoord, 0);

    // At a genuine exterior cell, retain resolved high-resolution coverage.
    // Inside a fully present native 3D cell, however, alpha is not implicitly
    // transparency: the DS compositor uses it only for the special BG0 blend
    // when the selected underlay is Target 2. The probe records that exact
    // decision. Otherwise any nonzero Direct3D texel is the selected sample.
    if (semantics.r < (254.5 / 255.0))
        return clamp(direct3D.a, 0.0, 1.0);

    float presence = direct3D.a > (0.5 / 255.0) ? 1.0 : 0.0;
    if (operatorSemantics.r < 0.5)
        return presence;

    // Match the compositor's 5-bit alpha conversion and mandatory +1 used
    // by the Direct3D special blend instead of treating RGBA8 alpha as a
    // conventional continuous opacity value.
    if (presence > 0.0)
    {
        float alpha5 = floor(clamp(direct3D.a, 0.0, 1.0) * 255.0 + 0.5) / 8.0;
        alpha5 = floor(alpha5);
        return clamp((alpha5 + 1.0) / 32.0, 0.0, 1.0);
    }
    return 0.0;
}

void main()
{
    ivec2 size = textureSize(OverlayBlackTex, 0);
    ivec2 coord = clamp(ivec2(floor(fTexcoord * vec2(size))), ivec2(0), size - ivec2(1));

    vec3 overlayBlack = clamp(texelFetch(OverlayBlackTex, coord, 0).rgb, 0.0, 1.0);
    vec3 overlayWhite = clamp(texelFetch(OverlayWhiteTex, coord, 0).rgb, 0.0, 1.0);
    vec3 underWeight = clamp(overlayWhite - overlayBlack, 0.0, 1.0);

    vec4 direct3D = FetchDirect3D(coord);
    vec3 directColor = Direct3DUnderlayColor(direct3D);
    vec3 finalColor = clamp(overlayBlack + (underWeight * directColor), 0.0, 1.0);
    if (uExplicit3DOperator)
    {
        // The three scaled inputs were composed with Direct3D absent, opaque
        // black, and opaque white. None contains the native-resolved 3D alpha
        // boundary. Reconstruct the opaque color response first, then apply
        // the actual high-resolution Direct3D alpha exactly once.
        vec3 absent3D = overlayBlack;
        vec3 opaqueBlack = overlayWhite;
        vec3 opaqueWhite = clamp(
            texelFetch(OverlayOpaqueWhiteTex, coord, 0).rgb, 0.0, 1.0);
        vec3 colorWeight = clamp(opaqueWhite - opaqueBlack, 0.0, 1.0);
        vec3 opaqueResult = clamp(
            opaqueBlack + (colorWeight * directColor), 0.0, 1.0);
        finalColor = mix(absent3D, opaqueResult,
                         Explicit3DOperatorAlpha(coord, direct3D));
    }
    else if (uDirect3DPresentationSpace && PresentationEndpointLooksPassThrough(overlayBlack, underWeight))
        finalColor = directColor;

    if (uDebugTintBySource)
    {
        float under = max(max(underWeight.r, underWeight.g), underWeight.b);
        float overlay = max(max(overlayBlack.r, overlayBlack.g), overlayBlack.b);
        vec3 tint = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), under);
        finalColor = mix(finalColor * 0.2, tint * max(max(under, overlay), 0.35), 0.85);
    }

    oColor = vec4(finalColor, 1.0);
}
