#version 140

uniform sampler2D OverlayBlackTex;
uniform sampler2D OverlayWhiteTex;
uniform sampler2D Direct3DTexture;
uniform sampler2D NativeMetaTex;
uniform sampler2D NativeTopColorTex;
uniform sampler2D HybridForegroundTex;
uniform sampler2D HybridNativeFallbackTex;
uniform sampler2D NativeRole3DTex;
uniform sampler2D Hybrid2DBaseTex;
uniform sampler2D HybridLegacyCandidateTex;
uniform bool uDebugTintBySource;
uniform bool uLegacyUnderlayEndpoint;
uniform bool uCoverageAwareUnderlay;
uniform bool uDirect3DPresentationSpace;
uniform bool uConservativeHybrid;
uniform bool uHybridWindowEdgeAssist;
uniform bool uHybridTarget2AlphaBlendAssist;
uniform bool uHybridNativeEffectGuard;
uniform bool uHybridForeground2DBase;
uniform bool uHybridLegacyCandidate;
uniform bool uHybridForceOverlayAssist;
uniform int uHybridDebugMode;
uniform int uScaleFactor;

layout(std140) uniform ubCompositorConfig
{
    ivec4 uBGPrio;
    bool uEnableOBJ;
    bool uEnable3D;
    int uBlendCnt;
    int uBlendEffect;
    ivec3 uBlendCoef;
};

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

struct StackEffectInfo
{
    int effect;
    int eva;
    int evb;
    int evy;
};

ivec4 QuantizeColor(vec4 color)
{
    return ivec4(color * 255.0) >> ivec4(2, 2, 2, 3);
}

int SourceRank(int mask)
{
    if (mask == (1 << 3)) return 0;
    if (mask == (1 << 2)) return 1;
    if (mask == (1 << 1)) return 2;
    if (mask == (1 << 0)) return 3;
    if (mask == (1 << 4)) return 4;
    if (mask == 0x20) return -1;
    if (mask == 0) return -2;
    return -2;
}

bool IsInFront(int prioA, int maskA, int prioB, int maskB)
{
    if (maskA == 0)
        return false;
    if (maskB == 0)
        return true;

    if (prioA != prioB)
        return prioA < prioB;

    return SourceRank(maskA) > SourceRank(maskB);
}

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
    ivec2 directCoord = Direct3DCoord(coord);
    vec4 direct3D = FetchDirect3DRaw(directCoord);

    if (!uCoverageAwareUnderlay || uLegacyUnderlayEndpoint || direct3D.a > 0.0001)
        return direct3D;

    ivec2 directSize = textureSize(Direct3DTexture, 0);
    vec3 coveredRGB = vec3(0.0);
    float coveredWeight = 0.0;
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            vec4 sampleColor = texelFetch(Direct3DTexture,
                                          clamp(directCoord + ivec2(x, y), ivec2(0), directSize - ivec2(1)),
                                          0);
            float coverage = clamp(sampleColor.a, 0.0, 1.0);
            coveredRGB += sampleColor.rgb * coverage;
            coveredWeight += coverage;
        }
    }

    if (coveredWeight > 0.0001)
        direct3D.rgb = coveredRGB / coveredWeight;

    return direct3D;
}

ivec2 NativeCoord(ivec2 coord)
{
    ivec2 outputSize = textureSize(OverlayBlackTex, 0);
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    return clamp((coord * nativeSize) / max(outputSize, ivec2(1)), ivec2(0), nativeSize - ivec2(1));
}

StackEffectInfo ResolveStackEffectInfo(int specialType,
                                       int sourceMask1,
                                       int sourceMask2,
                                       ivec4 topColor,
                                       bool blendAllowed)
{
    StackEffectInfo info;
    info.effect = 0;
    info.eva = 0;
    info.evb = 0;
    info.evy = uBlendCoef[2];

    if ((specialType != 0) && ((uBlendCnt & (sourceMask2 << 8)) != 0))
    {
        if (specialType == 1)
        {
            info.effect = 4;
            info.eva = (topColor.a & 0x1F) + 1;
            info.evb = 32 - info.eva;
        }
        else if (specialType == 2)
        {
            info.effect = 1;
            info.eva = uBlendCoef[0];
            info.evb = uBlendCoef[1];
        }
        else
        {
            info.effect = 1;
            info.eva = topColor.a;
            info.evb = 16 - info.eva;
        }
    }
    else if (((uBlendCnt & sourceMask1) != 0) && blendAllowed)
    {
        info.effect = uBlendEffect;
        if (info.effect == 1)
        {
            if ((uBlendCnt & (sourceMask2 << 8)) != 0)
            {
                info.eva = uBlendCoef[0];
                info.evb = uBlendCoef[1];
            }
            else
            {
                info.effect = 0;
            }
        }
        else if (info.effect != 2 && info.effect != 3)
        {
            info.effect = 0;
        }
    }

    if ((info.effect == 2 || info.effect == 3) && info.evy == 0)
        info.effect = 0;

    return info;
}

ivec4 ApplyStackEffect(ivec4 topColor, ivec4 secondColor, StackEffectInfo effect)
{
    ivec4 outColor = topColor;

    if (effect.effect == 1)
    {
        outColor = ((topColor * effect.eva) + (secondColor * effect.evb) + 0x8) >> 4;
        outColor = min(outColor, 0x3F);
    }
    else if (effect.effect == 2)
    {
        outColor = topColor + ((((0x3F - topColor) * effect.evy) + 0x8) >> 4);
    }
    else if (effect.effect == 3)
    {
        outColor = topColor - (((topColor * effect.evy) + 0x7) >> 4);
    }
    else if (effect.effect == 4)
    {
        outColor = ((topColor * effect.eva) + (secondColor * effect.evb) + 0x10) >> 5;
    }

    outColor.rgb = clamp(outColor.rgb, ivec3(0), ivec3(0x3F));
    return outColor;
}

int MaxRGBDelta(ivec4 a, ivec4 b)
{
    ivec3 delta = abs(a.rgb - b.rgb);
    return max(max(delta.r, delta.g), delta.b);
}

StackEffectInfo ResolveInsertedDirect3DEffect(int sourceMask1,
                                              int sourceMask2,
                                              int packedInfo,
                                              bool blendAllowed,
                                              bool direct3DTop,
                                              bool direct3DSecond,
                                              vec4 native3D,
                                              ivec2 nativeCoord)
{
    const int direct3DMask = (1 << 0);
    int stackMask1 = sourceMask1;
    int stackMask2 = sourceMask2;
    int stackSpecialType = packedInfo & 0x3;
    ivec4 topColor = QuantizeColor(texelFetch(NativeTopColorTex, nativeCoord, 0));

    if (direct3DTop)
    {
        stackMask2 = stackMask1;
        stackMask1 = direct3DMask;
        stackSpecialType = 1;
        topColor = QuantizeColor(native3D);
    }
    else if (direct3DSecond)
    {
        stackMask2 = direct3DMask;
    }

    return ResolveStackEffectInfo(stackSpecialType,
                                  stackMask1,
                                  stackMask2,
                                  topColor,
                                  blendAllowed);
}

bool Direct3DForegroundEffectCanUseHybrid(StackEffectInfo effect)
{
    return effect.effect == 0 || effect.effect == 2 || effect.effect == 3 || effect.effect == 4;
}

bool NativeAlphaBlendExcludesDirect3D()
{
    const int direct3DMask = (1 << 0);
    return uBlendEffect == 1 &&
           (uBlendCnt & direct3DMask) == 0 &&
           (uBlendCnt & (direct3DMask << 8)) == 0;
}

bool AlphaBlendHasNoTarget1()
{
    return uBlendEffect == 1 &&
           (uBlendCnt & 0x3F) == 0;
}

bool Direct3DTarget1AlphaBlendZeroed()
{
    const int direct3DMask = (1 << 0);
    const int twoDLayerMask = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);
    return uBlendEffect == 1 &&
           (uBlendCnt & direct3DMask) == direct3DMask &&
           (uBlendCnt & (direct3DMask << 8)) == 0 &&
           (uBlendCnt & (twoDLayerMask << 8)) != 0 &&
           uBlendCoef[0] == 0 &&
           uBlendCoef[1] == 16;
}

bool Direct3DTarget1AlphaBlendContributes()
{
    const int direct3DMask = (1 << 0);
    const int nonDirect3DMask = 0x3F ^ direct3DMask;
    return uBlendEffect == 1 &&
           (uBlendCnt & direct3DMask) == direct3DMask &&
           (uBlendCnt & (nonDirect3DMask << 8)) != 0 &&
           uBlendCoef[0] > 0 &&
           uBlendCoef[1] > 0;
}

bool Direct3DTarget1AlphaBlendZeroCoefficient()
{
    const int direct3DMask = (1 << 0);
    const int nonDirect3DMask = 0x3F ^ direct3DMask;
    return uBlendEffect == 1 &&
           (uBlendCnt & direct3DMask) == direct3DMask &&
           (uBlendCnt & (nonDirect3DMask << 8)) != 0 &&
           uBlendCoef[0] == 0 &&
           uBlendCoef[1] == 0;
}

bool Direct3DTarget1AlphaBlendConfigured()
{
    const int direct3DMask = (1 << 0);
    return uBlendEffect == 1 &&
           (uBlendCnt & direct3DMask) == direct3DMask;
}

bool Direct3DTarget2AlphaBlendContributes()
{
    const int direct3DMask = (1 << 0);
    const int nonDirect3DMask = 0x3F ^ direct3DMask;
    return uBlendEffect == 1 &&
           (uBlendCnt & direct3DMask) == 0 &&
           (uBlendCnt & nonDirect3DMask) != 0 &&
           (uBlendCnt & (direct3DMask << 8)) == (direct3DMask << 8) &&
           uBlendCoef[0] > 0 &&
           uBlendCoef[1] > 0;
}

bool NativeCellHas2DOnlyAlphaBlendEffect(ivec2 nativeCoord)
{
    vec4 meta = texelFetch(NativeMetaTex, nativeCoord, 0);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    int nativeEffect = (packedFlags >> 2) & 0x7;
    return nativeEffect != 0 && NativeAlphaBlendExcludesDirect3D();
}

bool NativeEffectBlocksHybridSelection(int nativeEffect,
                                       bool direct3DTop,
                                       StackEffectInfo effect,
                                       bool modeledDirect3DTarget2AlphaBlend)
{
    if (AlphaBlendHasNoTarget1())
        return false;

    if (nativeEffect == 0 || modeledDirect3DTarget2AlphaBlend)
        return false;

    if (uHybridTarget2AlphaBlendAssist && NativeAlphaBlendExcludesDirect3D())
        return false;

    // A native 2D effect below foreground Direct3D should not by itself force
    // native fallback. Block only when Direct3D is not safely foreground, or
    // when the inserted Direct3D stack would need unmodeled effect handling.
    return !(direct3DTop && Direct3DForegroundEffectCanUseHybrid(effect));
}

int HybridNativeSelectionRole(ivec2 nativeCoord)
{
    vec4 native3D = texelFetch(NativeRole3DTex, nativeCoord, 0);
    if (native3D.a <= 0.0001)
        return 0;

    vec4 meta = texelFetch(NativeMetaTex, nativeCoord, 0);
    int sourceMask1 = int((meta.r * 255.0) + 0.5);
    int sourceMask2 = int((meta.g * 255.0) + 0.5);
    int packedInfo = int((meta.b * 255.0) + 0.5);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    int sourcePrio1 = (packedInfo >> 2) & 0x7;
    int sourcePrio2 = (packedInfo >> 5) & 0x7;
    bool bg0Allowed = (packedFlags & 0x1) != 0;
    bool blendAllowed = (packedFlags & 0x2) != 0;
    int nativeEffect = (packedFlags >> 2) & 0x7;

    int direct3DPrio = uBGPrio[0];
    const int direct3DMask = (1 << 0);
    if (!uEnable3D || !bg0Allowed || direct3DPrio < 0)
        return 0;

    bool direct3DTop = IsInFront(direct3DPrio, direct3DMask, sourcePrio1, sourceMask1);
    bool direct3DSecond = !direct3DTop && IsInFront(direct3DPrio, direct3DMask, sourcePrio2, sourceMask2);
    if (!direct3DTop && !direct3DSecond)
        return 0;

    StackEffectInfo effect = ResolveInsertedDirect3DEffect(sourceMask1,
                                                          sourceMask2,
                                                          packedInfo,
                                                          blendAllowed,
                                                          direct3DTop,
                                                          direct3DSecond,
                                                          native3D,
                                                          nativeCoord);
    bool modeledDirect3DTarget2AlphaBlend = uHybridTarget2AlphaBlendAssist && direct3DSecond && effect.effect == 1;
    if (NativeEffectBlocksHybridSelection(nativeEffect,
                                          direct3DTop,
                                          effect,
                                          modeledDirect3DTarget2AlphaBlend))
        return 0;

    if (direct3DTop)
        return Direct3DForegroundEffectCanUseHybrid(effect) ? 1 : 0;

    return (effect.effect == 0 || modeledDirect3DTarget2AlphaBlend) ? 2 : 0;
}

int HybridSelectionRole(ivec2 coord)
{
    return HybridNativeSelectionRole(NativeCoord(coord));
}

bool NativeCellIsSafeWindowExcludedDirect3D(ivec2 nativeCoord)
{
    if (!uEnable3D || uBGPrio[0] < 0)
        return false;

    vec4 meta = texelFetch(NativeMetaTex, nativeCoord, 0);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    bool bg0Allowed = (packedFlags & 0x1) != 0;
    int nativeEffect = (packedFlags >> 2) & 0x7;
    return !bg0Allowed && nativeEffect == 0;
}

int HybridNativeFallbackReason(ivec2 nativeCoord)
{
    vec4 meta = texelFetch(NativeMetaTex, nativeCoord, 0);
    int sourceMask1 = int((meta.r * 255.0) + 0.5);
    int sourceMask2 = int((meta.g * 255.0) + 0.5);
    int packedInfo = int((meta.b * 255.0) + 0.5);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    int sourcePrio1 = (packedInfo >> 2) & 0x7;
    int sourcePrio2 = (packedInfo >> 5) & 0x7;
    bool bg0Allowed = (packedFlags & 0x1) != 0;
    bool blendAllowed = (packedFlags & 0x2) != 0;
    int nativeEffect = (packedFlags >> 2) & 0x7;

    int direct3DPrio = uBGPrio[0];
    const int direct3DMask = (1 << 0);
    if (!uEnable3D || direct3DPrio < 0)
        return 1;
    if (!bg0Allowed)
        return nativeEffect == 0 ? 6 : 3;

    vec4 native3D = texelFetch(NativeRole3DTex, nativeCoord, 0);
    if (native3D.a <= 0.0001)
        return 2;

    bool direct3DTop = IsInFront(direct3DPrio, direct3DMask, sourcePrio1, sourceMask1);
    bool direct3DSecond = !direct3DTop && IsInFront(direct3DPrio, direct3DMask, sourcePrio2, sourceMask2);
    if (!direct3DTop && !direct3DSecond)
        return 4;

    StackEffectInfo effect = ResolveInsertedDirect3DEffect(sourceMask1,
                                                          sourceMask2,
                                                          packedInfo,
                                                          blendAllowed,
                                                          direct3DTop,
                                                          direct3DSecond,
                                                          native3D,
                                                          nativeCoord);
    bool modeledDirect3DTarget2AlphaBlend = uHybridTarget2AlphaBlendAssist && direct3DSecond && effect.effect == 1;
    if (NativeEffectBlocksHybridSelection(nativeEffect,
                                          direct3DTop,
                                          effect,
                                          modeledDirect3DTarget2AlphaBlend))
        return 3;

    if ((direct3DTop && Direct3DForegroundEffectCanUseHybrid(effect)) ||
        (direct3DSecond && (effect.effect == 0 || modeledDirect3DTarget2AlphaBlend)))
        return 0;

    return 5;
}

vec3 HybridFallbackReasonColor(int reason)
{
    if (reason == 1) return vec3(0.42);
    if (reason == 2) return vec3(0.0, 0.22, 1.0);
    if (reason == 3) return vec3(1.0, 0.86, 0.0);
    if (reason == 4) return vec3(0.42, 0.0, 1.0);
    if (reason == 5) return vec3(1.0, 0.46, 0.0);
    if (reason == 6) return vec3(0.0, 0.55, 0.42);
    return vec3(1.0, 0.0, 0.0);
}

bool OverlayOperatorHasVisible2D(vec3 overlayBlack, vec3 underWeight)
{
    float under = max(max(underWeight.r, underWeight.g), underWeight.b);
    float overlay = max(max(overlayBlack.r, overlayBlack.g), overlayBlack.b);
    float channelSpread = max(max(abs(underWeight.r - underWeight.g),
                                  abs(underWeight.r - underWeight.b)),
                              abs(underWeight.g - underWeight.b));
    return channelSpread > 0.08 || overlay >= 0.03 || under <= 0.90;
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

bool HasNearbyOverlayAssistCell(ivec2 nativeCoord)
{
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    ivec2 maxCoord = nativeSize - ivec2(1);

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            ivec2 sampleCoord = clamp(nativeCoord + ivec2(x, y), ivec2(0), maxCoord);
            if (HybridNativeSelectionRole(sampleCoord) == 2)
                return true;
        }
    }

    return false;
}

bool HasNearbyForegroundDirect3DCell(ivec2 nativeCoord, int radius)
{
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    ivec2 maxCoord = nativeSize - ivec2(1);

    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            ivec2 sampleCoord = clamp(nativeCoord + ivec2(x, y), ivec2(0), maxCoord);
            if (HybridNativeSelectionRole(sampleCoord) == 1)
                return true;
        }
    }

    return false;
}

bool HasNearbyNativeEffectFallbackCell(ivec2 nativeCoord)
{
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    ivec2 maxCoord = nativeSize - ivec2(1);

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            ivec2 sampleCoord = clamp(nativeCoord + ivec2(x, y), ivec2(0), maxCoord);
            if (HybridNativeFallbackReason(sampleCoord) == 3)
                return true;
        }
    }

    return false;
}

bool HasNearbyForegroundBoundaryHazardCell(ivec2 nativeCoord)
{
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    ivec2 maxCoord = nativeSize - ivec2(1);

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            ivec2 sampleCoord = clamp(nativeCoord + ivec2(x, y), ivec2(0), maxCoord);
            int role = HybridNativeSelectionRole(sampleCoord);
            if (role == 2)
                return true;
            if (role == 1)
                continue;

            int reason = HybridNativeFallbackReason(sampleCoord);
            if (reason != 2)
                return true;
        }
    }

    return false;
}

bool HasNearbySafeWindowExcludedDirect3DCell(ivec2 nativeCoord)
{
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    ivec2 maxCoord = nativeSize - ivec2(1);

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            ivec2 sampleCoord = clamp(nativeCoord + ivec2(x, y), ivec2(0), maxCoord);
            if (NativeCellIsSafeWindowExcludedDirect3D(sampleCoord))
                return true;
        }
    }

    return false;
}

bool NativeCellHasForegroundDirect3DEffect(ivec2 nativeCoord, int expectedEffect)
{
    vec4 native3D = texelFetch(NativeRole3DTex, nativeCoord, 0);
    if (native3D.a <= 0.0001)
        return false;

    vec4 meta = texelFetch(NativeMetaTex, nativeCoord, 0);
    int sourceMask1 = int((meta.r * 255.0) + 0.5);
    int sourceMask2 = int((meta.g * 255.0) + 0.5);
    int packedInfo = int((meta.b * 255.0) + 0.5);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    int sourcePrio1 = (packedInfo >> 2) & 0x7;
    int sourcePrio2 = (packedInfo >> 5) & 0x7;
    bool bg0Allowed = (packedFlags & 0x1) != 0;
    bool blendAllowed = (packedFlags & 0x2) != 0;

    int direct3DPrio = uBGPrio[0];
    const int direct3DMask = (1 << 0);
    if (!uEnable3D || !bg0Allowed || direct3DPrio < 0)
        return false;

    bool direct3DTop = IsInFront(direct3DPrio, direct3DMask, sourcePrio1, sourceMask1);
    bool direct3DSecond = !direct3DTop && IsInFront(direct3DPrio, direct3DMask, sourcePrio2, sourceMask2);
    if (!direct3DTop)
        return false;

    StackEffectInfo effect = ResolveInsertedDirect3DEffect(sourceMask1,
                                                          sourceMask2,
                                                          packedInfo,
                                                          blendAllowed,
                                                          direct3DTop,
                                                          direct3DSecond,
                                                          native3D,
                                                          nativeCoord);
    return effect.effect == expectedEffect;
}

bool HasNearbyForegroundDirect3DEffectCell(ivec2 nativeCoord, int expectedEffect, int radius)
{
    ivec2 nativeSize = textureSize(NativeMetaTex, 0);
    ivec2 maxCoord = nativeSize - ivec2(1);

    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            ivec2 sampleCoord = clamp(nativeCoord + ivec2(x, y), ivec2(0), maxCoord);
            if (HybridNativeSelectionRole(sampleCoord) == 1 &&
                NativeCellHasForegroundDirect3DEffect(sampleCoord, expectedEffect))
                return true;
        }
    }

    return false;
}

bool Direct3DHasAlphaBoundary(ivec2 coord, float centerAlpha)
{
    if (centerAlpha < 0.999)
    {
        if (centerAlpha > 0.0001)
            return true;
    }

    ivec2 directCoord = Direct3DCoord(coord);
    ivec2 directSize = textureSize(Direct3DTexture, 0);
    ivec2 maxCoord = directSize - ivec2(1);
    float minAlpha = centerAlpha;
    float maxAlpha = centerAlpha;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float alpha = texelFetch(Direct3DTexture,
                                     clamp(directCoord + ivec2(x, y), ivec2(0), maxCoord),
                                     0).a;
            minAlpha = min(minAlpha, alpha);
            maxAlpha = max(maxAlpha, alpha);
        }
    }

    return maxAlpha > 0.0001 && minAlpha < 0.999;
}

bool ForegroundAndNativeFallbackDisagree(ivec2 coord)
{
    vec3 foreground = clamp(texelFetch(HybridForegroundTex, coord, 0).rgb, 0.0, 1.0);
    vec3 nativeFallback = clamp(texelFetch(HybridNativeFallbackTex, coord, 0).rgb, 0.0, 1.0);
    vec3 delta = abs(foreground - nativeFallback);
    return max(max(delta.r, delta.g), delta.b) > (3.0 / 255.0);
}

bool ShouldUseNativeFallbackForForegroundAlphaBoundary(ivec2 coord,
                                                       ivec2 nativeCoord,
                                                       vec4 direct3D)
{
    return (NativeCellHasForegroundDirect3DEffect(nativeCoord, 4) ||
            Direct3DTarget1AlphaBlendConfigured()) &&
           Direct3DHasAlphaBoundary(coord, direct3D.a) &&
           !HasNearbyOverlayAssistCell(nativeCoord) &&
           !HasNearbyForegroundBoundaryHazardCell(nativeCoord) &&
           ForegroundAndNativeFallbackDisagree(coord);
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
    if (uDirect3DPresentationSpace)
        return clamp(direct3D.rgb, 0.0, 1.0) * clamp(direct3D.a, 0.0, 1.0);

    if (uLegacyUnderlayEndpoint)
        return clamp(direct3D.rgb, 0.0, 1.0) * clamp(direct3D.a, 0.0, 1.0);

    return Direct3DCompositorEndpoint(direct3D);
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
    if (!uConservativeHybrid &&
        uDirect3DPresentationSpace &&
        PresentationEndpointLooksPassThrough(overlayBlack, underWeight))
    {
        finalColor = directColor;
    }
    int hybridRole = 0;
    int hybridFallbackReason = 0;
    int hybridFinalSource = 0;
    bool hybridOverlayAssist = false;
    bool hybridOperatorVisible = false;
    bool hybridNativeEffectGuard = false;
    bool hybridForegroundSelected = false;
    bool hybridForegroundBoundary2DBase = false;
    bool hybridLegacyCandidateSelected = false;

    if (uConservativeHybrid)
    {
        hybridRole = HybridSelectionRole(coord);
        if (hybridRole == 0)
            hybridFallbackReason = HybridNativeFallbackReason(NativeCoord(coord));
        hybridOverlayAssist = hybridRole == 2;
        hybridOperatorVisible = OverlayOperatorHasVisible2D(overlayBlack, underWeight);
        if (!hybridOverlayAssist && hybridRole == 1 && hybridOperatorVisible)
        {
            ivec2 nativeCoord = NativeCoord(coord);
            hybridOverlayAssist = (uHybridTarget2AlphaBlendAssist && NativeCellHas2DOnlyAlphaBlendEffect(nativeCoord)) ||
                                  HasNearbyOverlayAssistCell(nativeCoord) ||
                                  (uHybridWindowEdgeAssist && HasNearbySafeWindowExcludedDirect3DCell(nativeCoord));
        }
        if (!hybridOverlayAssist && uHybridWindowEdgeAssist && hybridFallbackReason == 6)
            hybridOverlayAssist = true;

        ivec2 nativeCoord = NativeCoord(coord);
        if (!hybridOverlayAssist && uHybridNativeEffectGuard && hybridRole == 1)
        {
            hybridNativeEffectGuard = HasNearbyNativeEffectFallbackCell(nativeCoord);
            if (!hybridNativeEffectGuard && !uHybridLegacyCandidate)
            {
                hybridNativeEffectGuard =
                    ShouldUseNativeFallbackForForegroundAlphaBoundary(coord, nativeCoord, direct3D);
            }
            if (hybridNativeEffectGuard)
                hybridFallbackReason = 3;
        }

        hybridForegroundSelected =
            hybridRole == 1 &&
            !hybridOverlayAssist &&
            !hybridNativeEffectGuard;

        if (uHybridForceOverlayAssist)
        {
            hybridRole = 2;
            hybridFallbackReason = 0;
            hybridOverlayAssist = true;
            hybridNativeEffectGuard = false;
            hybridForegroundSelected = false;
            hybridForegroundBoundary2DBase = false;
            hybridLegacyCandidateSelected = false;
        }

        bool hybridLegacyForeground = false;
        bool hybridLegacyFallback = false;
        if (uHybridLegacyCandidate)
        {
            bool zeroedDirect3DAlphaBlend = Direct3DTarget1AlphaBlendZeroed();
            bool contributingDirect3DAlphaBlend = Direct3DTarget1AlphaBlendContributes();
            bool zeroCoefficientDirect3DAlphaBlend = Direct3DTarget1AlphaBlendZeroCoefficient();
            bool contributingDirect3DTarget2AlphaBlend = Direct3DTarget2AlphaBlendContributes();
            bool inactiveAlphaBlend = AlphaBlendHasNoTarget1();
            bool target1AlphaLegacyFallback =
                (zeroedDirect3DAlphaBlend ||
                 contributingDirect3DAlphaBlend ||
                 zeroCoefficientDirect3DAlphaBlend) &&
                hybridRole == 0 &&
                (hybridFallbackReason == 2 || hybridFallbackReason == 3);
            bool target2AlphaLegacyFallback =
                (contributingDirect3DTarget2AlphaBlend || inactiveAlphaBlend) &&
                hybridRole == 0 &&
                (hybridFallbackReason == 2 || hybridFallbackReason == 3);
            hybridLegacyForeground =
                hybridForegroundSelected;
            hybridLegacyFallback =
                target1AlphaLegacyFallback ||
                target2AlphaLegacyFallback ||
                (hybridRole == 0 &&
                 hybridFallbackReason == 2 &&
                 (zeroedDirect3DAlphaBlend ||
                  contributingDirect3DAlphaBlend ||
                  zeroCoefficientDirect3DAlphaBlend ||
                  !HasNearbyForegroundDirect3DEffectCell(nativeCoord, 4, 2)));

            hybridLegacyCandidateSelected =
                hybridLegacyForeground ||
                hybridLegacyFallback;
        }

        if (hybridLegacyCandidateSelected)
        {
            finalColor = clamp(texelFetch(HybridLegacyCandidateTex, coord, 0).rgb, 0.0, 1.0);
            hybridFinalSource = 5;
        }
        else if (hybridForegroundSelected)
        {
            finalColor = clamp(texelFetch(HybridForegroundTex, coord, 0).rgb, 0.0, 1.0);
            hybridFinalSource = 1;
        }
        else if (!hybridOverlayAssist)
        {
            vec3 hybridNativeFallbackColor = clamp(texelFetch(HybridNativeFallbackTex, coord, 0).rgb, 0.0, 1.0);
            vec3 hybrid2DBaseColor = vec3(0.0);

            if (uHybridForeground2DBase && hybridRole == 0 && hybridFallbackReason == 2)
            {
                hybrid2DBaseColor = clamp(texelFetch(Hybrid2DBaseTex, coord, 0).rgb, 0.0, 1.0);
                vec3 fallbackResidue = abs(hybridNativeFallbackColor - hybrid2DBaseColor);
                float residueStrength = max(max(fallbackResidue.r, fallbackResidue.g), fallbackResidue.b);

                hybridForegroundBoundary2DBase =
                    direct3D.a <= 0.0001 &&
                    residueStrength > (2.0 / 255.0) &&
                    HasNearbyForegroundDirect3DCell(nativeCoord, 2) &&
                    !HasNearbyForegroundDirect3DEffectCell(nativeCoord, 4, 2) &&
                    !HasNearbyForegroundBoundaryHazardCell(nativeCoord);
            }

            if (hybridForegroundBoundary2DBase)
            {
                finalColor = hybrid2DBaseColor;
                hybridFinalSource = 3;
            }
            else
            {
                finalColor = hybridNativeFallbackColor;
                hybridFinalSource = 4;
            }
        }
    }

    if (uDebugTintBySource)
    {
        if (uConservativeHybrid)
        {
            if (uHybridDebugMode == 1)
            {
                bool highResDirect3DCoverage = direct3D.a > 0.0001;
                vec3 tint = vec3(0.04, 0.04, 0.06);
                if (hybridForegroundSelected)
                    tint = vec3(1.0, 0.0, 1.0);
                else if (hybridOverlayAssist)
                    tint = vec3(0.0, 0.86, 1.0);
                else if (hybridLegacyCandidateSelected)
                    tint = vec3(0.0, 1.0, 0.28);
                else if (highResDirect3DCoverage)
                    tint = vec3(1.0, 0.0, 0.0);
                else
                    tint = HybridFallbackReasonColor(hybridFallbackReason) *
                           (hybridOperatorVisible ? 0.65 : 0.25);

                oColor = vec4(tint, 1.0);
                return;
            }

            if (uHybridDebugMode == 2)
            {
                float alpha = clamp(direct3D.a, 0.0, 1.0);
                vec3 tint = vec3(0.02, 0.03, 0.10);
                if (hybridForegroundSelected)
                {
                    if (alpha <= 0.0001)
                        tint = vec3(1.0, 0.0, 0.0);
                    else if (alpha >= 0.999)
                        tint = vec3(1.0, 0.0, 1.0);
                    else
                        tint = mix(vec3(1.0, 0.45, 0.0), vec3(1.0, 1.0, 0.0), alpha);
                }
                else if (hybridOverlayAssist)
                    tint = vec3(0.0, 0.86, 1.0);
                else if (hybridLegacyCandidateSelected)
                    tint = vec3(0.0, 1.0, 0.28);
                else
                    tint = HybridFallbackReasonColor(hybridFallbackReason) * 0.25;

                oColor = vec4(tint, 1.0);
                return;
            }

            if (uHybridDebugMode == 3)
            {
                vec3 tint = vec3(0.0, 0.86, 1.0);
                if (hybridFinalSource == 1)
                    tint = vec3(1.0, 0.0, 0.55);
                else if (hybridFinalSource == 3)
                    tint = vec3(0.0, 0.28, 1.0);
                else if (hybridFinalSource == 4)
                    tint = vec3(0.42);
                else if (hybridFinalSource == 5)
                    tint = vec3(0.0, 1.0, 0.28);

                oColor = vec4(tint, 1.0);
                return;
            }

            vec3 tint = vec3(0.19);
            if (hybridForegroundSelected)
                tint = vec3(1.0, 0.0, 1.0);
            else if (hybridOverlayAssist)
                tint = vec3(0.0, 0.86, 1.0);
            else if (hybridLegacyCandidateSelected)
                tint = vec3(0.0, 1.0, 0.28);
            else
                tint = HybridFallbackReasonColor(hybridFallbackReason) *
                       (hybridOperatorVisible ? 1.0 : 0.45);

            oColor = vec4(tint, 1.0);
            return;
        }

        float under = max(max(underWeight.r, underWeight.g), underWeight.b);
        float overlay = max(max(overlayBlack.r, overlayBlack.g), overlayBlack.b);
        vec3 tint = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), under);
        finalColor = mix(finalColor * 0.2, tint * max(max(under, overlay), 0.35), 0.85);
    }

    oColor = vec4(finalColor, 1.0);
}
