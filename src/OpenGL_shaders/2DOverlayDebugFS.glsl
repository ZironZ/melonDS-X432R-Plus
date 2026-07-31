// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D OverlayBlackTex;
uniform sampler2D OverlayWhiteTex;
uniform sampler2D Direct3DTexture;
uniform sampler2D NativeFinalTex;
uniform int uDebugMode;
uniform bool uLegacyUnderlayEndpoint;

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
};

layout(std140) uniform ubScanlineConfig
{
    sScanline uScanline[192];
};

smooth in vec2 fTexcoord;

out vec4 oColor;

const int Debug_UnderlayWeight = 0;
const int Debug_ReconstructedNative = 1;
const int Debug_ReconstructionError = 2;
const int Debug_ValidityConfidence = 3;
const int Debug_OwnershipReason = 4;

ivec2 Direct3DCoord(ivec2 coord)
{
    ivec2 outputSize = textureSize(OverlayBlackTex, 0);
    ivec2 directSize = textureSize(Direct3DTexture, 0);
    int line = clamp((coord.y * 192) / max(outputSize.y, 1), 0, 191);
    int bgOffsetX = uScanline[line].BGOffset[0].x;

    vec2 coordScale = vec2(directSize) / vec2(max(outputSize, ivec2(1)));
    ivec2 directCoord = ivec2(floor((vec2(coord) + vec2(0.5)) * coordScale));

    float directScaleX = float(directSize.x) / 256.0;
    directCoord.x += int(round(float(bgOffsetX) * directScaleX));

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
    // Match the native compositor's 6-bit RGB endpoint domain. The overlay
    // black/white endpoints are extracted after that conversion, so native
    // reconstruction normally should not use raw 3D RGB directly.
    const float whiteEndpoint = 252.0 / 255.0;
    vec3 quantized = floor(clamp(direct3D.rgb, 0.0, 1.0) * 255.0 / 4.0) * (4.0 / 255.0);
    return clamp(quantized / whiteEndpoint, 0.0, 1.0);
}

vec3 Direct3DUnderlayColor(vec4 direct3D)
{
    if (uLegacyUnderlayEndpoint)
        return clamp(direct3D.rgb, 0.0, 1.0) * clamp(direct3D.a, 0.0, 1.0);

    return Direct3DCompositorEndpoint(direct3D);
}

vec3 Reconstruct(ivec2 coord, vec3 overlayBlack, vec3 underWeight)
{
    vec4 direct3D = FetchDirect3D(coord);
    vec3 directColor = Direct3DUnderlayColor(direct3D);
    return clamp(overlayBlack + (underWeight * directColor), 0.0, 1.0);
}

void main()
{
    ivec2 size = textureSize(OverlayBlackTex, 0);
    ivec2 coord = clamp(ivec2(floor(fTexcoord * vec2(size))), ivec2(0), size - ivec2(1));

    vec3 overlayBlack = clamp(texelFetch(OverlayBlackTex, coord, 0).rgb, 0.0, 1.0);
    vec3 overlayWhite = clamp(texelFetch(OverlayWhiteTex, coord, 0).rgb, 0.0, 1.0);
    vec3 underWeight = clamp(overlayWhite - overlayBlack, 0.0, 1.0);

    if (uDebugMode == Debug_UnderlayWeight)
    {
        oColor = vec4(underWeight, 1.0);
        return;
    }

    if (uDebugMode == Debug_OwnershipReason)
    {
        float under = max(max(underWeight.r, underWeight.g), underWeight.b);
        float overlay = max(max(overlayBlack.r, overlayBlack.g), overlayBlack.b);
        float channelSpread = max(max(abs(underWeight.r - underWeight.g),
                                      abs(underWeight.r - underWeight.b)),
                                  abs(underWeight.g - underWeight.b));

        if (channelSpread > 0.08)
        {
            // Per-channel underlay response. This is not a clean scalar
            // coverage decision and usually means tinting, quantization, or
            // a native compositor effect is influencing the operator.
            oColor = vec4(0.0, 1.0, 0.0, 1.0);
        }
        else if (under > 0.90 && overlay < 0.03)
        {
            // Mostly enhanced-underlay pass-through.
            oColor = vec4(0.0, 0.25, 1.0, 1.0);
        }
        else if (under > 0.90)
        {
            // Underlay mostly passes through, but the overlay adds visible
            // color. This can be legitimate additive/presentation content.
            oColor = vec4(0.0, 1.0, 1.0, 1.0);
        }
        else if (under < 0.10)
        {
            // Overlay owns the pixel and will hide the enhanced underlay.
            oColor = vec4(1.0, 0.0, 1.0, 1.0);
        }
        else
        {
            // Partial/translucent overlay over enhanced underlay.
            oColor = vec4(1.0, 0.85, 0.0, 1.0);
        }
        return;
    }

    vec3 reconstructed = Reconstruct(coord, overlayBlack, underWeight);
    if (uDebugMode == Debug_ReconstructedNative)
    {
        oColor = vec4(reconstructed, 1.0);
        return;
    }

    vec3 nativeFinal = clamp(texelFetch(NativeFinalTex, coord, 0).rgb, 0.0, 1.0);
    vec3 error = abs(nativeFinal - reconstructed);
    if (uDebugMode == Debug_ReconstructionError)
    {
        oColor = vec4(clamp(error * 8.0, 0.0, 1.0), 1.0);
        return;
    }

    float maxError = max(max(error.r, error.g), error.b);
    float confidence = 1.0 - clamp(maxError * 16.0, 0.0, 1.0);
    oColor = vec4(vec3(confidence), 1.0);
}
