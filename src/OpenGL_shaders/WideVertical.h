// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
static const char* kWideVerticalFS = R"(#version 140
uniform sampler2D InputA;
uniform sampler2D InputB;
uniform int uHeight;
uniform int uOverlap;
uniform int uOperation; // 0: physical displays, 1: source A, 2: B, 3: blend
uniform ivec2 uModes; // 0: absent, 1: live 3D, 2: captured, 3: direct VRAM
uniform ivec2 uBlend;
uniform bool uRaw3D;
uniform ivec3 uPresentation[2]; // Base override (-1: composed), master mode, factor.
uniform bvec2 uDisplays;
struct EdgeRow { uvec4 Current; uvec4 Captured; };
layout(std140) uniform ubWideWindows { EdgeRow uEdges[256]; };
layout(std140) uniform ubFinalPassConfig
{
    bvec4 uScreenSwap[48];
    int uScaleFactor; int uAuxLayer; int uDispModeA; int uDispModeB;
    int uBrightModeA; int uBrightModeB; int uBrightFactorA; int uBrightFactorB;
    float uAuxColorFactor;
};
smooth in vec3 fTexcoord;
out vec4 oTopColor;
out vec4 oBottomColor;
ivec3 effect(ivec3 c, int mode, int factor)
{
    if (mode == 2) c += ((63-c)*factor+8)>>4;
    if (mode == 3) c -= (c*factor+7)>>4;
    return c;
}
ivec4 source(vec4 raw, uint policy, uint backdropPolicy)
{
    if (raw.a == 0.0) policy = backdropPolicy;
    if ((policy & 1u) == 0u) return ivec4(0);
    uint bg = policy >> 9;
    ivec3 c = ivec3(raw.rgb*255.0)>>2;
    if ((policy & 2u) == 0u || raw.a == 0.0)
        c = ivec3((bg & 31u)*2u, ((bg>>5)&31u)*2u+((bg>>15)&1u), (bg>>9)&62u);
    if ((policy & (1u << 25)) != 0u)
    {
        ivec3 back = ivec3((bg & 31u)*2u, ((bg>>5)&31u)*2u+((bg>>15)&1u), (bg>>9)&62u);
        int a = (int(raw.a*255.0)>>3)+1;
        c = (c*a+back*(32-a)+16)>>5;
    }
    else c = effect(c, int((policy>>2)&3u), int((policy>>4)&31u));
    return ivec4((c<<2)|(c>>6), 255);
}
vec4 display(vec4 raw, uint policy, uint backdropPolicy, int engine)
{
    ivec3 c = ivec3(0);
    if (uPresentation[engine].x >= 0)
        c = ivec3(uPresentation[engine].x);
    else if (uModes[engine] != 0 && (policy & 1u) != 0u)
        c = (uModes[engine] == 3 ? ivec4(raw*255.0) : source(raw,policy,backdropPolicy)).rgb>>2;
    ivec2 m = uPresentation[engine].yz;
    if (m.x == 1) c += ((63-c)*m.y)>>4;
    if (m.x == 2) c -= (c*m.y+15)>>4;
    c = (c<<2)|(c>>6);
    return vec4(vec3(c)/255.0,1);
}
void main()
{
    int x = clamp(int(fTexcoord.x*256.0),0,255);
    float margin = float(uHeight-192)*0.5;
    float gap = 192.0 - 2.0*float(uOverlap);
    float packedHeight = float(uHeight)-gap;
    float fullY = uOperation == 0 ? fTexcoord.y*float(uHeight) : fTexcoord.y*packedHeight;
    if (uOperation != 0 && fullY >= packedHeight*0.5) fullY += gap;
    float y = fullY-margin;
    int side = fullY < float(uHeight)*0.5 ? 0 : 1;
    int line = clamp(int(y),0,191);
    vec2 packedCoord = vec2(fTexcoord.x, (side == 0 ? fullY : fullY-gap)/packedHeight);
    vec2 expanded = vec2(fTexcoord.x, fullY/float(uHeight));
    vec4 a = texture(InputA, uOperation != 0 || uModes.x == 1 ? expanded : packedCoord);
    vec4 b = texture(InputB, packedCoord);
    if (uOperation != 0)
    {
        ivec4 ca = uRaw3D ? ivec4(a*255.0) : source(a,uEdges[x].Current[side],uEdges[x].Captured[side]);
        ivec4 cb = ivec4(b*255.0);
        ivec4 c = uOperation == 2 ? cb : ca;
        if (uOperation == 3)
        {
            ca >>= 3; cb >>= 3;
            int aa = ca.a > 0 ? 1 : 0;
            int ab = cb.a > 0 ? 1 : 0;
            c.rgb = min((ca.rgb*aa*uBlend.x+cb.rgb*ab*uBlend.y+8)>>4,ivec3(31))<<3;
            c.a = ((uBlend.x>0 ? aa : 0)|(uBlend.y>0 ? ab : 0))*255;
        }
        oTopColor = vec4(c)/255.0;
        oBottomColor = oTopColor;
        return;
    }
    vec4 main = display(a,uEdges[x].Current[side],uEdges[x].Captured[side],0);
    vec4 sub = display(b,uEdges[x].Current[2+side],uEdges[x].Captured[2+side],1);
    bool swap = uScreenSwap[line>>2][line&3];
    oTopColor = uDisplays.x ? (swap ? main : sub) : vec4(0,0,0,1);
    oBottomColor = uDisplays.y ? (swap ? sub : main) : vec4(0,0,0,1);
}
)";
