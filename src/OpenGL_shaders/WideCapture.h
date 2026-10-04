// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
static const char* kWideCaptureFS = R"(#version 140
uniform sampler2D InputA;
uniform sampler2D InputB;
uniform int uWidth;
uniform int uOverlap;
uniform bool uFullWidth;
uniform bool uSourceBFullWidth;
uniform bvec2 uCompose;
uniform int uMode;
uniform ivec2 uBlend;
struct EdgeRow { uvec4 Current; uvec4 Captured; };
layout(std140) uniform ubWideWindows { EdgeRow uEdges[192]; };
smooth in vec3 fTexcoord;
out vec4 oColor;
ivec3 effect(ivec3 c, uint policy)
{
    int mode = int((policy>>2)&3u), factor = int((policy>>4)&31u);
    if (mode == 2) c += ((63-c)*factor+8)>>4;
    if (mode == 3) c -= (c*factor+7)>>4;
    return c;
}
ivec3 backdrop(uint policy)
{
    uint c = policy>>9;
    return ivec3((c&31u)*2u, ((c>>5)&31u)*2u+((c>>15)&1u), (c>>9)&62u);
}
ivec4 source(vec4 raw, uint policy, uint back)
{
    if (raw.a == 0.0) policy = back;
    if ((policy&1u) == 0u) return ivec4(0);
    ivec3 c = (policy&2u) == 0u ? backdrop(policy) : ivec3(raw.rgb*255.0)>>2;
    if ((policy&(1u<<25)) != 0u)
    {
        int a = (int(raw.a*255.0)>>3)+1;
        c = (c*a+backdrop(policy)*(32-a)+16)>>5;
    }
    else c = effect(c,policy);
    return ivec4((c<<2)|(c>>6),255);
}
void main()
{
    float gap = float(256 - 2*uOverlap);
    float packedWidth = float(uWidth) - gap;
    float x = fTexcoord.x*(uFullWidth ? float(uWidth) : packedWidth);
    if (!uFullWidth && x >= packedWidth*0.5) x += gap;
    vec2 expanded = vec2(x/float(uWidth),fTexcoord.y);
    int side = x < float(uWidth)*0.5 ? 0 : 1;
    vec2 packedCoord = vec2((side == 0 ? x : x-gap)/packedWidth,fTexcoord.y);
    int line = clamp(int(fTexcoord.y*192.0),0,191);
    vec4 a = texture(InputA,expanded);
    vec4 b = texture(InputB,uSourceBFullWidth ? expanded : packedCoord);
    ivec4 ca = uCompose.x ? source(a,uEdges[line].Current[side],uEdges[line].Captured[side]) : ivec4(a*255.0);
    ivec4 cb = uCompose.y ? source(b,uEdges[line].Current[2+side],uEdges[line].Captured[2+side]) : ivec4(b*255.0);
    ivec4 c = cb;
    if (uMode != 1)
    {
        ca >>= 3; cb >>= 3;
        int aa = ca.a > 0 ? 1 : 0, ab = cb.a > 0 ? 1 : 0;
        c.rgb = min((ca.rgb*aa*uBlend.x+cb.rgb*ab*uBlend.y+8)>>4,ivec3(31))<<3;
        c.a = ((uBlend.x>0 ? aa : 0)|(uBlend.y>0 ? ab : 0))*255;
    }
    oColor = vec4(c)/255.0;
}
)";
