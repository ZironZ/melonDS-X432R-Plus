#version 140

uniform sampler2D BGLayerTex[4];
uniform sampler2D BGLayerMetaTex[4];
uniform sampler2D Direct3DCoverageTex;
uniform sampler2DArray OBJLayerTex;
uniform sampler2DArray Capture128Tex;
uniform sampler2DArray Capture256Tex;
uniform isampler2D MosaicTex;

struct sBGConfig
{
    ivec2 Size;
    int Type;
    int PalOffset;
    int TileOffset;
    int MapOffset;
    bool Clamp;
};

layout(std140) uniform ubBGConfig
{
    int uVRAMMask;
    sBGConfig uBGConfig[4];
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

layout(std140) uniform ubCompositorConfig
{
    ivec4 uBGPrio;
    bool uEnableOBJ;
    bool uEnable3D;
    int uBlendCnt;
    int uBlendEffect;
    ivec3 uBlendCoef;
};

uniform int uScaleFactor;
uniform bool uOBJNativeResolution;
uniform int uLayerFilterMode;
uniform bool uLayerFilterNoWrap;
uniform bool uDebugTintBySource;
uniform bool uSplit3DSemantics;
uniform bool uSharpenSplit3DCoverage;

smooth in vec4 fTexcoord;

out vec4 oColor;

int MosaicX = 0;

const int LayerFilter_Nearest = 0;
const int LayerFilter_Spline36 = 1;

const int DebugPath_Backdrop = 0;
const int DebugPath_FilteredBG = 1;
const int DebugPath_UnfilteredBG = 2;
const int DebugPath_CaptureBG = 3;
const int DebugPath_OBJ = 4;
const int DebugPath_Direct3D = 5;
const int DebugPath_FilteredOBJ = 6;

ivec2 OBJTexCoordFromNative(ivec2 coord)
{
    return uOBJNativeResolution ? coord : coord * uScaleFactor;
}

ivec2 OBJTexCoordFromOutput(ivec2 coord)
{
    return uOBJNativeResolution ? ivec2(fTexcoord.xy) : coord;
}

ivec3 ConvertColor(int col)
{
    ivec3 ret;
    ret.r = (col & 0x1F) << 1;
    ret.g = ((col & 0x3E0) >> 4) | (col >> 15);
    ret.b = (col & 0x7C00) >> 9;
    return ret;
}

vec4 BG0Fetch(vec2 coord)
{
    return texture(BGLayerTex[0], coord);
}

vec4 BG1Fetch(vec2 coord)
{
    return texture(BGLayerTex[1], coord);
}

vec4 BG2Fetch(vec2 coord)
{
    return texture(BGLayerTex[2], coord);
}

vec4 BG3Fetch(vec2 coord)
{
    return texture(BGLayerTex[3], coord);
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

vec3 DebugPathTint(int sourceMask, int specialType, int sourcePath)
{
    vec3 tint = DebugTintColor(sourceMask, specialType);

    if (sourcePath == DebugPath_UnfilteredBG)
        return mix(tint, vec3(1.0, 0.0, 0.0), 0.65);
    if (sourcePath == DebugPath_CaptureBG)
        return vec3(0.0, 1.0, 1.0);
    if (sourcePath == DebugPath_OBJ)
        return vec3(1.0, 1.0, 0.2);
    if (sourcePath == DebugPath_FilteredOBJ)
        return vec3(0.6, 1.0, 0.0);
    if (sourcePath == DebugPath_Direct3D)
        return vec3(1.0, 0.0, 1.0);
    if (sourcePath == DebugPath_Backdrop)
        return vec3(0.6, 0.6, 0.6);

    return tint;
}

int BGDebugPath(bool direct3D, bool capture)
{
    if (direct3D)
        return DebugPath_Direct3D;
    if (capture)
        return DebugPath_CaptureBG;
    if (uLayerFilterMode == LayerFilter_Spline36)
        return DebugPath_FilteredBG;
    return DebugPath_UnfilteredBG;
}

int LayerDebugPath(int bg)
{
    if (bg == 0)
        return BGDebugPath(uEnable3D, false);
    if (bg == 1)
        return BGDebugPath(false, false);
    if (bg == 2)
        return BGDebugPath(false, uBGConfig[2].Type >= 7);
    return BGDebugPath(false, uBGConfig[3].Type >= 7);
}

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

vec2 WrapLayerPosition(vec2 coord, ivec2 texSize)
{
    vec2 size = vec2(texSize);
    return coord - (floor(coord / size) * size);
}

ivec2 WrapLayerTexel(ivec2 coord, ivec2 texSize)
{
    return ivec2(WrapLayerPosition(vec2(coord), texSize));
}

vec4 FetchLayerTexelFiltered(sampler2D source, ivec2 coord, ivec2 texSize, bool clampLayer)
{
    if (clampLayer || uLayerFilterNoWrap)
    {
        if (any(lessThan(coord, ivec2(0))) || any(greaterThanEqual(coord, texSize)))
            return vec4(0.0);

        return texelFetch(source, coord, 0);
    }

    ivec2 wrappedCoord = WrapLayerTexel(coord, texSize);
    if (any(lessThan(wrappedCoord, ivec2(0))) || any(greaterThanEqual(wrappedCoord, texSize)))
        return vec4(0.0);

    return texelFetch(source, wrappedCoord, 0);
}

vec4 FetchLayerSpline36(sampler2D source, vec2 bgpos, ivec2 texSize, bool clampLayer)
{
    vec4 nearestColor = texture(source, bgpos / vec2(texSize));
    if (nearestColor.a <= 0.0)
        return vec4(0.0);

    vec2 filterPos = (!clampLayer && uLayerFilterNoWrap) ? WrapLayerPosition(bgpos, texSize) : bgpos;
    vec2 srcCoord = filterPos - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);

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

    vec3 accum = vec3(0.0);
    vec3 minColor = vec3(1.0);
    vec3 maxColor = vec3(0.0);
    float totalWeight = 0.0;

    for (int y = 0; y < 6; y++)
    {
        float wyNorm = wy[y] / wysum;
        for (int x = 0; x < 6; x++)
        {
            float weight = (wx[x] / wxsum) * wyNorm;
            vec4 sampleColor = FetchLayerTexelFiltered(source, baseCoord + ivec2(x - 2, y - 2),
                                                       texSize, clampLayer);
            if (sampleColor.a <= 0.0)
                continue;

            accum += sampleColor.rgb * weight;
            minColor = min(minColor, sampleColor.rgb);
            maxColor = max(maxColor, sampleColor.rgb);
            totalWeight += weight;
        }
    }

    if (totalWeight <= 0.00001)
        return nearestColor;

    vec3 color = clamp(accum / totalWeight, minColor, maxColor);
    return vec4(clamp(color, 0.0, 1.0), nearestColor.a);
}

vec4 BG0FetchFiltered(vec2 bgpos)
{
    if (uLayerFilterMode == LayerFilter_Spline36 && !uEnable3D)
        return FetchLayerSpline36(BGLayerTex[0], bgpos, uBGConfig[0].Size, uBGConfig[0].Clamp);

    return BG0Fetch(bgpos / vec2(uBGConfig[0].Size));
}

vec4 BG1FetchFiltered(vec2 bgpos)
{
    if (uLayerFilterMode == LayerFilter_Spline36)
        return FetchLayerSpline36(BGLayerTex[1], bgpos, uBGConfig[1].Size, uBGConfig[1].Clamp);

    return BG1Fetch(bgpos / vec2(uBGConfig[1].Size));
}

vec4 BG2FetchFiltered(vec2 bgpos)
{
    if (uLayerFilterMode == LayerFilter_Spline36)
        return FetchLayerSpline36(BGLayerTex[2], bgpos, uBGConfig[2].Size, uBGConfig[2].Clamp);

    return BG2Fetch(bgpos / vec2(uBGConfig[2].Size));
}

vec4 BG3FetchFiltered(vec2 bgpos)
{
    if (uLayerFilterMode == LayerFilter_Spline36)
        return FetchLayerSpline36(BGLayerTex[3], bgpos, uBGConfig[3].Size, uBGConfig[3].Clamp);

    return BG3Fetch(bgpos / vec2(uBGConfig[3].Size));
}

vec2 BG0CalcPos(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[0];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[0])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    return bgpos;
}

float BG0CoverageFetchFiltered(vec2 bgpos)
{
    if (!uSplit3DSemantics || !uEnable3D)
        return clamp(BG0FetchFiltered(bgpos).a, 0.0, 1.0);

    return clamp(texture(Direct3DCoverageTex, bgpos / vec2(uBGConfig[0].Size)).a, 0.0, 1.0);
}

float Split3DCoverageWeight(float coverage)
{
    coverage = clamp(coverage, 0.0, 1.0);
    if (!uSharpenSplit3DCoverage)
        return coverage;

    return smoothstep(0.20, 0.50, coverage);
}

vec4 BG0CalcAndFetch(vec2 coord, int line)
{
    vec2 bgpos = BG0CalcPos(coord, line);
    return BG0FetchFiltered(bgpos);
}

float BG0CalcAndFetchCoverage(vec2 coord, int line)
{
    vec2 bgpos = BG0CalcPos(coord, line);
    return BG0CoverageFetchFiltered(bgpos);
}

vec4 BG1CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[1];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[1])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    return BG1FetchFiltered(bgpos);
}

vec4 BG2CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[2];
    vec2 bgpos;
    if (uBGConfig[2].Type >= 2)
    {
        // rotscale BG
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[0]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        // text-mode BG
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[2])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    if (uBGConfig[2].Type >= 7)
    {
        // hi-res capture
        bgpos.y += uBGConfig[2].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[2].Size), uBGConfig[2].TileOffset);

        // due to the possible weirdness of display capture buffers,
        // we need to do custom wraparound handling
        if (uBGConfig[2].Clamp)
        {
            if (any(lessThan(capcoord.xy, vec2(0))) || any(greaterThanEqual(capcoord.xy, vec2(1))))
                return vec4(0);
        }

        if (uBGConfig[2].Type == 7)
            return texture(Capture128Tex, capcoord);
        else
            return texture(Capture256Tex, capcoord);
    }

    return BG2FetchFiltered(bgpos);
}

vec4 BG3CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[3];
    vec2 bgpos;
    if (uBGConfig[3].Type >= 2)
    {
        // rotscale BG
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[1]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        // text-mode BG
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[3])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    if (uBGConfig[3].Type >= 7)
    {
        // hi-res capture
        bgpos.y += uBGConfig[3].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[3].Size), uBGConfig[3].TileOffset);

        // due to the possible weirdness of display capture buffers,
        // we need to do custom wraparound handling
        if (uBGConfig[3].Clamp)
        {
            if (any(lessThan(capcoord.xy, vec2(0))) || any(greaterThanEqual(capcoord.xy, vec2(1))))
                return vec4(0);
        }

        if (uBGConfig[3].Type == 7)
            return texture(Capture128Tex, capcoord);
        else
            return texture(Capture256Tex, capcoord);
    }

    return BG3FetchFiltered(bgpos);
}

void CalcSpriteMosaic(in ivec2 coord, out ivec4 objflags, out vec4 objcolor)
{
    objflags = ivec4(0);
    objcolor = vec4(0);

    for (int i = 0; i < 16; i++)
    {
        ivec2 curpos = ivec2(coord.x - 15 + i, coord.y);

        if (curpos.x < 0)
        {
            objflags = ivec4(0);
            objcolor = vec4(0);
        }
        else
        {
            int mosx = texelFetch(MosaicTex, ivec2(curpos.x, uScanline[curpos.y].MosaicSize.z), 0).r;
            ivec2 objcoord = OBJTexCoordFromNative(curpos);
            vec4 color = texelFetch(OBJLayerTex, ivec3(objcoord, 0), 0);
            ivec4 flags = ivec4(texelFetch(OBJLayerTex, ivec3(objcoord, 1), 0) * 255.0);

            bool latch = false;
            if (mosx == 0)
                latch = true;
            else if (flags.g == 0)
                latch = true;
            else if (objflags.g == 0)
                latch = true;
            else if (flags.a < objflags.a)
                latch = true;

            if (latch)
            {
                objflags = flags;
                objcolor = color;
            }
        }
    }
}

vec4 CompositeLayers()
{
    ivec2 coord = ivec2(fTexcoord.zw);
    vec2 bgcoord = vec2(fTexcoord.x, fract(fTexcoord.y));
    int xpos = int(fTexcoord.x);
    int line = int(fTexcoord.y);

    if (uScanline[line].MosaicSize.x > 0)
        MosaicX = texelFetch(MosaicTex, ivec2(bgcoord.x, uScanline[line].MosaicSize.x), 0).r;

    ivec4 col1 = ivec4(ConvertColor(uScanline[line].BackColor), 0x20);
    int mask1 = 0x20;
    int debugPath1 = DebugPath_Backdrop;
    ivec4 col2 = ivec4(0);
    int mask2 = 0;
    float visualCoverage1 = 1.0;
    float visualCoverage2 = 0.0;
    bool specialcase = false;
    int specialType = 0;

    vec4 layercol[6];
    layercol[0] = BG0CalcAndFetch(bgcoord, line);
    layercol[1] = BG1CalcAndFetch(bgcoord, line);
    layercol[2] = BG2CalcAndFetch(bgcoord, line);
    layercol[3] = BG3CalcAndFetch(bgcoord, line);

    float direct3DVisualCoverage = BG0CalcAndFetchCoverage(bgcoord, line);

    ivec4 objflags;
    if (uScanline[line].MosaicSize.z > 0)
    {
        CalcSpriteMosaic(ivec2(fTexcoord.xy), objflags, layercol[4]);
    }
    else
    {
        ivec2 objcoord = OBJTexCoordFromOutput(coord);
        layercol[4] = texelFetch(OBJLayerTex, ivec3(objcoord, 0), 0);
        layercol[5] = texelFetch(OBJLayerTex, ivec3(objcoord, 1), 0);
        objflags = ivec4(layercol[5] * 255.0);
    }

    int winmask = uScanline[line].WinMask;
    bool inside_win0, inside_win1;

    if (xpos < uScanline[line].WinPos[0])
        inside_win0 = ((winmask & (1<<0)) != 0);
    else if (xpos < uScanline[line].WinPos[1])
        inside_win0 = ((winmask & (1<<1)) != 0);
    else
        inside_win0 = ((winmask & (1<<2)) != 0);

    if (xpos < uScanline[line].WinPos[2])
        inside_win1 = ((winmask & (1<<3)) != 0);
    else if (xpos < uScanline[line].WinPos[3])
        inside_win1 = ((winmask & (1<<4)) != 0);
    else
        inside_win1 = ((winmask & (1<<5)) != 0);

    uint winregs = uScanline[line].WinRegs;
    uint winsel = winregs;
    if (objflags.b > 0)
        winsel = winregs >> 8;
    if (inside_win1)
        winsel = winregs >> 16;
    if (inside_win0)
        winsel = winregs >> 24;

    for (int prio = 3; prio >= 0; prio--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if ((uBGPrio[bg] == prio) && (layercol[bg].a > 0) && ((winsel & (1u << bg)) != 0u))
            {
                col2 = col1;
                mask2 = mask1 << 8;
                visualCoverage2 = visualCoverage1;
                col1 = ivec4(layercol[bg] * 255.0) >> ivec4(2,2,2,3);
                mask1 = (1 << bg);
                visualCoverage1 = ((bg == 0) && uEnable3D) ? direct3DVisualCoverage : 1.0;
                debugPath1 = LayerDebugPath(bg);
                specialcase = (bg == 0) && uEnable3D;
                specialType = specialcase ? 1 : 0;
            }
        }

        if (uEnableOBJ && (objflags.a == prio) && (layercol[4].a > 0) && ((winsel & (1u << 4)) != 0u))
        {
            col2 = col1;
            mask2 = mask1 << 8;
            visualCoverage2 = visualCoverage1;
            col1 = ivec4(layercol[4] * 255.0) >> ivec4(2,2,2,3);
            mask1 = (1 << 4);
            visualCoverage1 = 1.0;
            debugPath1 = uLayerFilterMode == LayerFilter_Spline36 ? DebugPath_FilteredOBJ : DebugPath_OBJ;
            specialcase = (objflags.r != 0);
            specialType = objflags.r == 0 ? 0 : (objflags.r == 1 ? 2 : 3);
        }
    }

    int effect = 0;
    int eva, evb, evy = uBlendCoef[2];
    ivec4 preEffectCol1 = col1;
    ivec4 preEffectCol2 = col2;

    if (specialcase && (uBlendCnt & mask2) != 0)
    {
        if (mask1 == (1<<0))
        {
            // 3D layer blending
            effect = 4;
            eva = (col1.a & 0x1F) + 1;
            evb = 32 - eva;
        }
        else if (objflags.r == 1)
        {
            // semi-transparent sprite
            effect = 1;
            eva = uBlendCoef[0];
            evb = uBlendCoef[1];
        }
        else //if (objflags.r == 2)
        {
            // bitmap sprite
            effect = 1;
            eva = col1.a;
            evb = 16 - eva;
        }
    }
    else if (((uBlendCnt & mask1) != 0) && ((winsel & (1u << 5)) != 0u))
    {
        effect = uBlendEffect;
        if (effect == 1)
        {
            if ((uBlendCnt & mask2) != 0)
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
        // blending
        col1 = ((col1 * eva) + (col2 * evb) + 0x8) >> 4;
        col1 = min(col1, 0x3F);
    }
    else if (effect == 2)
    {
        // brightness up
        col1 = col1 + ((((0x3F - col1) * evy) + 0x8) >> 4);
    }
    else if (effect == 3)
    {
        // brightness down
        col1 = col1 - (((col1 * evy) + 0x7) >> 4);
    }
    else if (effect == 4)
    {
        // 3D layer blending
        col1 = ((col1 * eva) + (col2 * evb) + 0x10) >> 5;
    }

    if (uSplit3DSemantics)
    {
        bool direct3DTopBrightnessEffect = mask1 == (1 << 0) && (effect == 2 || effect == 3);
        if (mask1 == (1 << 0) && !direct3DTopBrightnessEffect)
        {
            vec4 mixed = mix(vec4(preEffectCol2), vec4(col1), Split3DCoverageWeight(visualCoverage1));
            col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
        }
        else if (mask2 == (1 << 8) && effect != 0)
        {
            vec4 mixed = mix(vec4(preEffectCol1), vec4(col1), Split3DCoverageWeight(visualCoverage2));
            col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
        }
    }

    vec3 finalColor = vec3(col1.rgb << 2) / 255.0;
    if (uDebugTintBySource)
    {
        vec3 tint = DebugPathTint(mask1, specialType, debugPath1);
        float luma = dot(finalColor, vec3(0.299, 0.587, 0.114));
        finalColor = mix(finalColor * 0.2, tint * max(luma, 0.35), 0.85);
    }

    return vec4(finalColor, 1);
}

void main()
{
    oColor = CompositeLayers();
}
