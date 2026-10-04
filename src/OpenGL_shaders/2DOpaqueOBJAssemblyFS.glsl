#version 140
// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
uniform sampler2D Source;
uniform int uMemberCount;
// xy is the native atlas origin, zw the member's native dimensions.
uniform ivec4 uMembers[128];
uniform ivec2 uRootSize;
out vec4 oColor;
void main()
{
    ivec2 p=ivec2(gl_FragCoord.xy);
    oColor=vec4(0.0);
    if (any(greaterThanEqual(p,uRootSize))) return;
    // First present normal OAM wins. There are no fractional/native blend
    // operators in this source assembly; all alpha here is decoded presence.
    for (int i=0;i<uMemberCount;i++)
    {
        ivec4 m=uMembers[i];
        ivec2 q=p-(uRootSize-m.zw)/2;
        if (any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,m.zw))) continue;
        vec4 color=texelFetch(Source,m.xy+q,0);
        if (color.a>0.0) { oColor=color; return; }
    }
}
