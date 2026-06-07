#version 140

uniform sampler2D UpscaledTopColorTex;
uniform sampler2D UpscaledSecondColorTex;
uniform sampler2D UpscaledMetaTex;
uniform sampler2D UpscaledCoverageTex;
uniform sampler2D UpscaledExactFinalTex;
uniform sampler2D Direct3DTexture;
uniform bool uUseExactFinalFallback;
uniform bool uUseForegroundOverlay;
uniform bool uDebugTintBySource;
uniform bool uNativeExactOutput;
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

smooth in vec4 fTexcoord;

out vec4 oColor;

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

vec3 DebugTintColor(int sourceMask, int specialType)
{
    if (specialType == 1)
        return vec3(1.0, 0.0, 1.0);
    if (sourceMask == (1 << 0))
        return vec3(1.0, 0.35, 0.2);
    if (sourceMask == (1 << 1))
        return vec3(0.2, 1.0, 0.2);
    if (sourceMask == (1 << 2))
        return vec3(0.2, 0.7, 1.0);
    if (sourceMask == (1 << 3))
        return vec3(1.0, 0.75, 0.2);
    if (sourceMask == (1 << 4))
        return vec3(1.0, 1.0, 0.2);
    if (sourceMask == 0x20)
        return vec3(0.6, 0.6, 0.6);
    return vec3(1.0, 1.0, 1.0);
}

vec4 FetchDirect3D(ivec2 coord)
{
    int line = clamp(coord.y / uScaleFactor, 0, 191);
    ivec2 directCoord = coord + ivec2(uScanline[line].BGOffset[0].x * uScaleFactor, 0);
    ivec2 directSize = textureSize(Direct3DTexture, 0);

    if (any(lessThan(directCoord, ivec2(0))) || any(greaterThanEqual(directCoord, directSize)))
        return vec4(0.0);

    return texelFetch(Direct3DTexture, directCoord, 0);
}

void main()
{
    ivec2 coord = ivec2(fTexcoord.zw);

    if (uUseForegroundOverlay)
    {
        vec4 foreground = clamp(texelFetch(UpscaledExactFinalTex, coord, 0), 0.0, 1.0);
        float foregroundAlpha = clamp(foreground.a, 0.0, 1.0);
        vec4 meta = texelFetch(UpscaledMetaTex, coord, 0);
        int sourceMask1 = int((meta.r * 255.0) + 0.5);
        int packedInfo = int((meta.b * 255.0) + 0.5);
        int packedFlags = int((meta.a * 255.0) + 0.5);
        int sourcePrio1 = (packedInfo >> 2) & 0x7;
        bool bg0Allowed = (packedFlags & 0x1) != 0;

        int foregroundMask = sourceMask1;
        int foregroundPrio = sourcePrio1;
        ivec2 metaSize = textureSize(UpscaledMetaTex, 0);
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
            {
                ivec2 sampleCoord = clamp(coord + (ivec2(x, y) * uScaleFactor),
                                          ivec2(0), metaSize - ivec2(1));
                vec4 sampleMeta = texelFetch(UpscaledMetaTex, sampleCoord, 0);
                int sampleMask = int((sampleMeta.r * 255.0) + 0.5);
                if ((sampleMask == 0) || (sampleMask == 0x20))
                    continue;

                int samplePackedInfo = int((sampleMeta.b * 255.0) + 0.5);
                int samplePrio = (samplePackedInfo >> 2) & 0x7;
                if ((foregroundMask == 0) || (foregroundMask == 0x20) ||
                    IsInFront(samplePrio, sampleMask, foregroundPrio, foregroundMask))
                {
                    foregroundMask = sampleMask;
                    foregroundPrio = samplePrio;
                }
            }
        }

        int direct3DPrio = uBGPrio[0];
        bool direct3DEnabled = uEnable3D && (direct3DPrio >= 0) && bg0Allowed;
        vec4 direct3DColor = direct3DEnabled ? FetchDirect3D(coord) : vec4(0.0);
        float direct3DAlpha = clamp(direct3DColor.a, 0.0, 1.0);
        const int direct3DMask = (1 << 0);
        bool direct3DInFront = direct3DEnabled &&
                               (direct3DAlpha > 0.0) &&
                               IsInFront(direct3DPrio, direct3DMask, foregroundPrio, foregroundMask);

        vec3 finalColor = direct3DInFront
            ? (direct3DColor.rgb * direct3DAlpha) + (foreground.rgb * (1.0 - direct3DAlpha))
            : foreground.rgb + (direct3DColor.rgb * direct3DAlpha * (1.0 - foregroundAlpha));
        oColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
        return;
    }

    vec4 topColor = texelFetch(UpscaledTopColorTex, coord, 0);
    vec4 secondColor = texelFetch(UpscaledSecondColorTex, coord, 0);
    vec4 meta = texelFetch(UpscaledMetaTex, coord, 0);
    vec4 coverage = texelFetch(UpscaledCoverageTex, coord, 0);

    int sourceMask1 = int((meta.r * 255.0) + 0.5);
    int sourceMask2 = int((meta.g * 255.0) + 0.5);
    int packedInfo = int((meta.b * 255.0) + 0.5);
    int packedFlags = int((meta.a * 255.0) + 0.5);
    int specialType = packedInfo & 0x3;
    int sourcePrio1 = (packedInfo >> 2) & 0x7;
    int sourcePrio2 = (packedInfo >> 5) & 0x7;
    bool bg0Allowed = (packedFlags & 0x1) != 0;
    bool blendAllowed = (packedFlags & 0x2) != 0;
    int nativeEffect = (packedFlags >> 2) & 0x7;

    if (uUseExactFinalFallback && (nativeEffect != 0))
    {
        vec4 exactFinalColor = texelFetch(UpscaledExactFinalTex, coord, 0);
        vec3 finalColor = exactFinalColor.rgb;

        if (uDebugTintBySource)
        {
            vec3 tint = DebugTintColor(sourceMask1, specialType);
            float luma = dot(finalColor, vec3(0.299, 0.587, 0.114));
            finalColor = mix(finalColor * 0.2, tint * max(luma, 0.35), 0.85);
        }

        oColor = vec4(finalColor, 1.0);
        return;
    }

    int direct3DPrio = uBGPrio[0];
    bool direct3DEnabled = uEnable3D && (direct3DPrio >= 0) && bg0Allowed;

    if (direct3DEnabled)
    {
        vec4 direct3DColor = FetchDirect3D(coord);
        if (direct3DColor.a > 0.0)
        {
            const int direct3DMask = (1 << 0);

            if (IsInFront(direct3DPrio, direct3DMask, sourcePrio1, sourceMask1))
            {
                secondColor = topColor;
                sourceMask2 = sourceMask1;
                sourcePrio2 = sourcePrio1;

                topColor = direct3DColor;
                sourceMask1 = direct3DMask;
                sourcePrio1 = direct3DPrio;
                specialType = 1;
            }
            else if (IsInFront(direct3DPrio, direct3DMask, sourcePrio2, sourceMask2))
            {
                secondColor = direct3DColor;
                sourceMask2 = direct3DMask;
                sourcePrio2 = direct3DPrio;
            }
        }
    }

    if (specialType == 0 && !uNativeExactOutput)
    {
        float topCoverage = smoothstep(0.15, 0.45, clamp(coverage.r, 0.0, 1.0));
        topColor = vec4(mix(secondColor.rgb, topColor.rgb, topCoverage),
                        mix(secondColor.a, topColor.a, topCoverage));
    }

    ivec4 col1 = QuantizeColor(topColor);
    ivec4 col2 = QuantizeColor(secondColor);

    int effect = 0;
    int eva = 0;
    int evb = 0;
    int evy = uBlendCoef[2];

    if ((specialType != 0) && ((uBlendCnt & (sourceMask2 << 8)) != 0))
    {
        if (specialType == 1)
        {
            effect = 4;
            eva = (col1.a & 0x1F) + 1;
            evb = 32 - eva;
        }
        else if (specialType == 2)
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
                effect = 0;
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
    else if (effect == 4)
    {
        col1 = ((col1 * eva) + (col2 * evb) + 0x10) >> 5;
    }

    vec3 finalColor = vec3(col1.rgb << 2) / 255.0;
    if (uDebugTintBySource)
    {
        vec3 tint = DebugTintColor(sourceMask1, specialType);
        float luma = dot(finalColor, vec3(0.299, 0.587, 0.114));
        finalColor = mix(finalColor * 0.2, tint * max(luma, 0.35), 0.85);
    }

    oColor = vec4(finalColor, 1.0);
}
