// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
static const char* kWideRectangularFS = R"(#version 140
uniform sampler2D InputA;
uniform sampler2D InputB;
uniform ivec2 uSize;
uniform ivec2 uOverlap;
uniform int uOperation; // 0: displays, 1: A, 2: B, 3: blend
uniform ivec2 uModes; // 0: absent, 1: live 3D, 2: composed capture, 3: VRAM
uniform ivec2 uBlend;
uniform bool uRaw3D;
uniform ivec3 uPresentation[2];
uniform bvec2 uDisplays;
struct EdgeRow { uvec4 Current; uvec4 Captured; };
// Native left/right rows, then native top/bottom columns. Corners require
// agreement between both axes, never a union of incompatible permissions.
layout(std140) uniform ubWideWindows { EdgeRow uEdges[448]; };
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
uint edgePolicy(vec2 native, int engine, bool backdrop)
{
    ivec2 p = clamp(ivec2(floor(native)), ivec2(0), ivec2(255,191));
    int h = engine*2 + (native.x < 128.0 ? 0 : 1);
    int v = engine*2 + (native.y < 96.0 ? 0 : 1);
    uint horizontal = backdrop ? uEdges[p.y].Captured[h] : uEdges[p.y].Current[h];
    uint vertical = backdrop ? uEdges[192+p.x].Captured[v] : uEdges[192+p.x].Current[v];
    bool useHorizontal = native.x < 0.0 || native.x >= 256.0 ||
        ((native.x < float(uOverlap.x) || native.x >= 256.0-float(uOverlap.x)) &&
         (uEdges[p.y].Current[h] & (1u<<28)) != 0u);
    bool useVertical = native.y < 0.0 || native.y >= 192.0 ||
        ((native.y < float(uOverlap.y) || native.y >= 192.0-float(uOverlap.y)) &&
         (uEdges[192+p.x].Current[v] & (1u<<28)) != 0u);
    if (!useVertical) return horizontal;
    if (!useHorizontal) return vertical;
    // Repair provenance does not change visibility, colour or effects. The
    // overlap can reach the same rendering policy through different proofs.
    const uint renderingBits = ~(1u<<28);
    return (horizontal & renderingBits) == (vertical & renderingBits) ? horizontal : 0u;
}
ivec3 backdrop(uint policy)
{
    uint c = policy >> 9;
    return ivec3((c & 31u)*2u, ((c>>5)&31u)*2u+((c>>15)&1u), (c>>9)&62u);
}
ivec3 effect(ivec3 c, int mode, int factor)
{
    if (mode == 2) c += ((63-c)*factor+8)>>4;
    if (mode == 3) c -= (c*factor+7)>>4;
    return c;
}
ivec4 compose(vec4 raw, uint policy, uint backdropPolicy)
{
    if (raw.a == 0.0 && (policy & 2u) != 0u) policy = backdropPolicy;
    if ((policy & 1u) == 0u) return ivec4(0);
    ivec3 c = ivec3(raw.rgb*255.0)>>2;
    if ((policy & 2u) == 0u || raw.a == 0.0) c = backdrop(policy);
    if ((policy & (1u<<25)) != 0u)
    {
        if (raw.a == 0.0) c = effect(c, int((policy>>26)&3u), int((policy>>4)&31u));
        else
        {
            int a = (int(raw.a*255.0)>>3)+1;
            c = (c*a+backdrop(policy)*(32-a)+16)>>5;
        }
    }
    else c = effect(c, int((policy>>2)&3u), int((policy>>4)&31u));
    return ivec4((c<<2)|(c>>6),255);
}
vec4 display(vec4 raw, vec2 native, int engine)
{
    uint policy = edgePolicy(native,engine,false);
    ivec3 c = ivec3(0);
    if (uPresentation[engine].x >= 0) c = ivec3(uPresentation[engine].x);
    else if (uModes[engine] != 0 && (policy & 1u) != 0u)
        c = (uModes[engine] == 3 ? ivec4(raw*255.0) :
             compose(raw,policy,edgePolicy(native,engine,true))).rgb>>2;
    ivec2 m = uPresentation[engine].yz;
    if (m.x == 1) c += ((63-c)*m.y)>>4;
    if (m.x == 2) c -= (c*m.y+15)>>4;
    c = (c<<2)|(c>>6);
    return vec4(vec3(c)/255.0,1);
}
void main()
{
    vec2 native = fTexcoord.xy*vec2(uSize) - vec2(uSize-ivec2(256,192))*0.5;
    vec4 a = texture(InputA,fTexcoord.xy);
    vec4 b = texture(InputB,fTexcoord.xy);
    if (uOperation != 0)
    {
        ivec4 ca = uRaw3D ? ivec4(a*255.0) :
            compose(a,edgePolicy(native,0,false),edgePolicy(native,0,true));
        ivec4 cb = ivec4(b*255.0);
        ivec4 c = uOperation == 2 ? cb : ca;
        if (uOperation == 3)
        {
            ca >>= 3; cb >>= 3;
            int aa = ca.a > 0 ? 1 : 0, ab = cb.a > 0 ? 1 : 0;
            c.rgb = min((ca.rgb*aa*uBlend.x+cb.rgb*ab*uBlend.y+8)>>4,ivec3(31))<<3;
            c.a = ((uBlend.x>0 ? aa : 0)|(uBlend.y>0 ? ab : 0))*255;
        }
        oTopColor = vec4(c)/255.0;
        oBottomColor = oTopColor;
        return;
    }
    int line = clamp(int(floor(native.y)),0,191);
    vec4 main = display(a,native,0), sub = display(b,native,1);
    bool swap = uScreenSwap[line>>2][line&3];
    oTopColor = uDisplays.x ? (swap ? main : sub) : vec4(0,0,0,1);
    oBottomColor = uDisplays.y ? (swap ? sub : main) : vec4(0,0,0,1);
}
)";
