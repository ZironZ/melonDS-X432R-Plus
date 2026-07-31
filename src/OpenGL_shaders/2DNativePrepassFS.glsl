// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D BGLayerTex[4];
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
uniform int uDebugLayer;

smooth in vec4 fTexcoord;

out vec4 oFinalColor;
out vec4 oTopColor;
out vec4 oSecondColor;
out vec4 oMeta;

int MosaicX = 0;

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

vec4 BG0CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[0];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[0])
        bgpos = floor(bgpos) - vec2(MosaicX, 0);

    return BG0Fetch(bgpos / vec2(uBGConfig[0].Size));
}

vec4 BG1CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[1];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[1])
        bgpos = floor(bgpos) - vec2(MosaicX, 0);

    return BG1Fetch(bgpos / vec2(uBGConfig[1].Size));
}

vec4 BG2CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[2];
    vec2 bgpos;
    if (uBGConfig[2].Type >= 2)
    {
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[0]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[2])
        bgpos = floor(bgpos) - vec2(MosaicX, 0);

    if (uBGConfig[2].Type >= 7)
    {
        bgpos.y += uBGConfig[2].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[2].Size), uBGConfig[2].TileOffset);

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

    return BG2Fetch(bgpos / vec2(uBGConfig[2].Size));
}

vec4 BG3CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[3];
    vec2 bgpos;
    if (uBGConfig[3].Type >= 2)
    {
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[1]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[3])
        bgpos = floor(bgpos) - vec2(MosaicX, 0);

    if (uBGConfig[3].Type >= 7)
    {
        bgpos.y += uBGConfig[3].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[3].Size), uBGConfig[3].TileOffset);

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

    return BG3Fetch(bgpos / vec2(uBGConfig[3].Size));
}

void CalcSpriteMosaic(in ivec2 coord, out ivec4 objflags, out vec4 objcolor)
{
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
            vec4 color = texelFetch(OBJLayerTex, ivec3(curpos * uScaleFactor, 0), 0);
            ivec4 flags = ivec4(texelFetch(OBJLayerTex, ivec3(curpos * uScaleFactor, 1), 0) * 255.0);

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

vec4 BackdropColor(int line)
{
    ivec3 color = ConvertColor(uScanline[line].BackColor) << 2;
    return vec4(vec3(color) / 255.0, 1.0);
}

void main()
{
    ivec2 coord = ivec2(fTexcoord.zw);
    vec2 bgcoord = vec2(fTexcoord.x, fract(fTexcoord.y));
    int xpos = int(fTexcoord.x);
    int line = int(fTexcoord.y);

    if (uScanline[line].MosaicSize.x > 0)
        MosaicX = texelFetch(MosaicTex, ivec2(bgcoord.x, uScanline[line].MosaicSize.x), 0).r;

    vec4 layercol[6];
    layercol[0] = BG0CalcAndFetch(bgcoord, line);
    layercol[1] = BG1CalcAndFetch(bgcoord, line);
    layercol[2] = BG2CalcAndFetch(bgcoord, line);
    layercol[3] = BG3CalcAndFetch(bgcoord, line);

    ivec4 objflags;
    if (uScanline[line].MosaicSize.z > 0)
        CalcSpriteMosaic(ivec2(fTexcoord.xy), objflags, layercol[4]);
    else
    {
        layercol[4] = texelFetch(OBJLayerTex, ivec3(coord, 0), 0);
        layercol[5] = texelFetch(OBJLayerTex, ivec3(coord, 1), 0);
        objflags = ivec4(layercol[5] * 255.0);
    }

    int winmask = uScanline[line].WinMask;
    bool inside_win0;
    bool inside_win1;

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

    if (uDebugLayer >= 0)
    {
        vec4 debugColor = vec4(0);

        if (uDebugLayer < 4)
        {
            int bg = uDebugLayer;
            if ((uBGPrio[bg] >= 0) && (layercol[bg].a > 0.0) && ((winsel & (1u << bg)) != 0u))
                debugColor = layercol[bg];
        }
        else if (uDebugLayer == 4)
        {
            if (uEnableOBJ && (layercol[4].a > 0.0) && ((winsel & (1u << 4)) != 0u))
                debugColor = layercol[4];
        }
        else if (uDebugLayer == 5)
        {
            if (uEnableOBJ && (layercol[4].a > 0.0) && ((winsel & (1u << 4)) != 0u))
                debugColor = vec4(objflags) / 255.0;
        }
        else if (uDebugLayer == 6)
        {
            debugColor = vec4(0, 0, 0, 1);
            if (uEnableOBJ && (layercol[4].a > 0.0) && ((winsel & (1u << 4)) != 0u))
                debugColor = vec4(vec3(layercol[4].a), 1.0);
        }

        oFinalColor = debugColor;
        oTopColor = debugColor;
        oSecondColor = vec4(0);
        oMeta = vec4(0);
        return;
    }

    vec4 raw1 = BackdropColor(line);
    vec4 raw2 = vec4(0);
    int sourceMask1 = 0x20;
    int sourceMask2 = 0;
    int sourcePrio1 = 4;
    int sourcePrio2 = 4;
    int specialType = 0;

    ivec4 col1 = ivec4(ConvertColor(uScanline[line].BackColor), 0x20);
    ivec4 col2 = ivec4(0);

    for (int prio = 3; prio >= 0; prio--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if ((uBGPrio[bg] == prio) && (layercol[bg].a > 0) && ((winsel & (1u << bg)) != 0u))
            {
                raw2 = raw1;
                sourceMask2 = sourceMask1;
                sourcePrio2 = sourcePrio1;
                col2 = col1;

                raw1 = layercol[bg];
                sourceMask1 = (1 << bg);
                sourcePrio1 = prio;
                col1 = ivec4(layercol[bg] * 255.0) >> ivec4(2, 2, 2, 3);
                specialType = ((bg == 0) && uEnable3D) ? 1 : 0;
            }
        }

        if (uEnableOBJ && (objflags.a == prio) && (layercol[4].a > 0) && ((winsel & (1u << 4)) != 0u))
        {
            raw2 = raw1;
            sourceMask2 = sourceMask1;
            sourcePrio2 = sourcePrio1;
            col2 = col1;

            raw1 = layercol[4];
            sourceMask1 = (1 << 4);
            sourcePrio1 = prio;
            col1 = ivec4(layercol[4] * 255.0) >> ivec4(2, 2, 2, 3);
            specialType = (objflags.r != 0) ? (objflags.r + 1) : 0;
        }
    }

    int effect = 0;
    int eva = 0;
    int evb = 0;
    int evy = uBlendCoef[2];
    int sourceMask2Shifted = sourceMask2 << 8;
    bool bg0Allowed = ((winsel & (1u << 0)) != 0u);
    bool blendAllowed = ((winsel & (1u << 5)) != 0u);

    if ((specialType != 0) && ((uBlendCnt & sourceMask2Shifted) != 0))
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
            if ((uBlendCnt & sourceMask2Shifted) != 0)
            {
                eva = uBlendCoef[0];
                evb = uBlendCoef[1];
            }
            else
                effect = 0;
        }
    }

    if ((effect == 2 || effect == 3) && evy == 0)
        effect = 0;

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

    float foregroundAlpha = (sourceMask1 == 0x20) ? 0.0 : 1.0;
    vec3 finalColor = vec3(col1.rgb << 2) / 255.0;
    oFinalColor = vec4(finalColor * foregroundAlpha, foregroundAlpha);
    oTopColor = raw1;
    oSecondColor = raw2;
    int packedInfo = specialType | (sourcePrio1 << 2) | (sourcePrio2 << 5);
    int packedFlags = (bg0Allowed ? 1 : 0) | (blendAllowed ? 2 : 0) | ((effect & 0x7) << 2);
    oMeta = vec4(float(sourceMask1), float(sourceMask2), float(packedInfo), float(packedFlags)) / 255.0;
}
