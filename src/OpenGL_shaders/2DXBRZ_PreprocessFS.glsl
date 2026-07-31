/*
    xBRZ freescale, based on the RetroArch shader by hunterk:
    https://github.com/libretro/glsl-shaders/blob/master/xbrz/shaders/xbrz-freescale.glsl

    Hyllian's xBR vertex code and texel mapping
    Copyright (C) 2011/2016 Hyllian - sergiogdb@gmail.com

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

    This shader also uses code and/or concepts from xBRZ as it appears in the
    DeSmuME source code. The license for which is as follows:

    ***************************************************************************
    * This file is part of the HqMAME project. It is distributed under        *
    * GNU General Public License: http://www.gnu.org/licenses/gpl-3.0         *
    * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved         *
    *                                                                         *
    * Additionally and as a special exception, the author gives permission    *
    * to link the code of this program with the MAME library (or with modified*
    * versions of MAME that use the same license as MAME), and distribute     *
    * linked combinations including the two. You must obey the GNU General    *
    * Public License in all respects for all of the code used other than MAME.*
    * If you modify this file, you may extend this exception to your version  *
    * of the file, but you are not obligated to do so. If you do not wish to  *
    * do so, delete this exception statement from your version.               *
    ***************************************************************************

    Two-pass melonDS adaptation Copyright 2026 ZironZ and distributed under
    GPL-3.0-or-later.
*/

#version 140

uniform sampler2D Source;

in vec2 fTexcoord;
out vec4 oColor;

const int BLEND_NONE = 0;
const int BLEND_NORMAL = 1;
const int BLEND_DOMINANT = 2;

const float LUMINANCE_WEIGHT = 1.0;
const float EQUAL_COLOR_TOLERANCE = 30.0 / 255.0;
const float STEEP_DIRECTION_THRESHOLD = 2.2;
const float DOMINANT_DIRECTION_THRESHOLD = 3.6;

ivec4 TexKey(vec4 color)
{
    return ivec4(floor((color * 255.0) + 0.5));
}

bool EqualKey(ivec4 a, ivec4 b)
{
    return all(equal(a, b));
}

vec4 FetchSource(ivec2 center, ivec2 offset)
{
    ivec2 size = textureSize(Source, 0);
    ivec2 pos = clamp(center + offset, ivec2(0), size - ivec2(1));
    return texelFetch(Source, pos, 0);
}

float DistYCbCr(vec4 pixA, vec4 pixB)
{
    const vec3 w = vec3(0.2627, 0.6780, 0.0593);
    const float scaleB = 0.5 / (1.0 - w.b);
    const float scaleR = 0.5 / (1.0 - w.r);

    vec4 diff = pixA - pixB;
    float Y = dot(diff.rgb, w);
    float Cb = scaleB * (diff.b - Y);
    float Cr = scaleR * (diff.r - Y);
    float colorDist = ((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr);

    return sqrt((pixA.a * pixB.a * colorDist) + (diff.a * diff.a));
}

bool IsPixEqual(vec4 pixA, vec4 pixB)
{
    return DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE;
}

void main()
{
    ivec2 size = textureSize(Source, 0);
    ivec2 center = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - ivec2(1));

    vec4 A = FetchSource(center, ivec2(-1, -1));
    vec4 B = FetchSource(center, ivec2( 0, -1));
    vec4 C = FetchSource(center, ivec2( 1, -1));
    vec4 D = FetchSource(center, ivec2(-1,  0));
    vec4 E = FetchSource(center, ivec2( 0,  0));
    vec4 F = FetchSource(center, ivec2( 1,  0));
    vec4 G = FetchSource(center, ivec2(-1,  1));
    vec4 H = FetchSource(center, ivec2( 0,  1));
    vec4 I = FetchSource(center, ivec2( 1,  1));

    ivec4 kA = TexKey(A);
    ivec4 kB = TexKey(B);
    ivec4 kC = TexKey(C);
    ivec4 kD = TexKey(D);
    ivec4 kE = TexKey(E);
    ivec4 kF = TexKey(F);
    ivec4 kG = TexKey(G);
    ivec4 kH = TexKey(H);
    ivec4 kI = TexKey(I);

    ivec4 blendResult = ivec4(BLEND_NONE);

    if (!((EqualKey(kE, kF) && EqualKey(kH, kI)) || (EqualKey(kE, kH) && EqualKey(kF, kI))))
    {
        float distHF = DistYCbCr(G, E) + DistYCbCr(E, C) + DistYCbCr(FetchSource(center, ivec2(0, 2)), I) +
                       DistYCbCr(I, FetchSource(center, ivec2(2, 0))) + (4.0 * DistYCbCr(H, F));
        float distEI = DistYCbCr(D, H) + DistYCbCr(H, FetchSource(center, ivec2(1, 2))) + DistYCbCr(B, F) +
                       DistYCbCr(F, FetchSource(center, ivec2(2, 1))) + (4.0 * DistYCbCr(E, I));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * distHF) < distEI;
        blendResult.z = ((distHF < distEI) && !EqualKey(kE, kF) && !EqualKey(kE, kH))
            ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL)
            : BLEND_NONE;
    }

    if (!((EqualKey(kD, kE) && EqualKey(kG, kH)) || (EqualKey(kD, kG) && EqualKey(kE, kH))))
    {
        float distGE = DistYCbCr(FetchSource(center, ivec2(-2, 1)), D) + DistYCbCr(D, B) +
                       DistYCbCr(FetchSource(center, ivec2(-1, 2)), H) + DistYCbCr(H, F) +
                       (4.0 * DistYCbCr(G, E));
        float distDH = DistYCbCr(FetchSource(center, ivec2(-2, 0)), G) + DistYCbCr(G, FetchSource(center, ivec2(0, 2))) +
                       DistYCbCr(A, E) + DistYCbCr(E, I) + (4.0 * DistYCbCr(D, H));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * distDH) < distGE;
        blendResult.w = ((distGE > distDH) && !EqualKey(kE, kD) && !EqualKey(kE, kH))
            ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL)
            : BLEND_NONE;
    }

    if (!((EqualKey(kB, kC) && EqualKey(kE, kF)) || (EqualKey(kB, kE) && EqualKey(kC, kF))))
    {
        float distEC = DistYCbCr(D, B) + DistYCbCr(B, FetchSource(center, ivec2(1, -2))) +
                       DistYCbCr(H, F) + DistYCbCr(F, FetchSource(center, ivec2(2, -1))) +
                       (4.0 * DistYCbCr(E, C));
        float distBF = DistYCbCr(A, E) + DistYCbCr(E, I) + DistYCbCr(FetchSource(center, ivec2(0, -2)), C) +
                       DistYCbCr(C, FetchSource(center, ivec2(2, 0))) + (4.0 * DistYCbCr(B, F));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * distBF) < distEC;
        blendResult.y = ((distEC > distBF) && !EqualKey(kE, kB) && !EqualKey(kE, kF))
            ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL)
            : BLEND_NONE;
    }

    if (!((EqualKey(kA, kB) && EqualKey(kD, kE)) || (EqualKey(kA, kD) && EqualKey(kB, kE))))
    {
        float distDB = DistYCbCr(FetchSource(center, ivec2(-2, 0)), A) + DistYCbCr(A, FetchSource(center, ivec2(0, -2))) +
                       DistYCbCr(G, E) + DistYCbCr(E, C) + (4.0 * DistYCbCr(D, B));
        float distAE = DistYCbCr(FetchSource(center, ivec2(-2, -1)), D) + DistYCbCr(D, H) +
                       DistYCbCr(FetchSource(center, ivec2(-1, -2)), B) + DistYCbCr(B, F) +
                       (4.0 * DistYCbCr(A, E));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * distDB) < distAE;
        blendResult.x = ((distDB < distAE) && !EqualKey(kE, kD) && !EqualKey(kE, kB))
            ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL)
            : BLEND_NONE;
    }

    vec4 info = vec4(blendResult);

    if (blendResult.z == BLEND_DOMINANT || (blendResult.z == BLEND_NORMAL &&
        !((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) || (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
          (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) && IsPixEqual(F, C) && !IsPixEqual(E, I)))))
    {
        info.z += 4.0;

        float distFG = DistYCbCr(F, G);
        float distHC = DistYCbCr(H, C);

        if ((STEEP_DIRECTION_THRESHOLD * distFG <= distHC) && !EqualKey(kE, kG) && !EqualKey(kD, kG))
            info.z += 16.0;

        if ((STEEP_DIRECTION_THRESHOLD * distHC <= distFG) && !EqualKey(kE, kC) && !EqualKey(kB, kC))
            info.z += 64.0;
    }

    if (blendResult.w == BLEND_DOMINANT || (blendResult.w == BLEND_NORMAL &&
        !((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) || (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
          (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) && IsPixEqual(H, I) && !IsPixEqual(E, G)))))
    {
        info.w += 4.0;

        float distHA = DistYCbCr(H, A);
        float distDI = DistYCbCr(D, I);

        if ((STEEP_DIRECTION_THRESHOLD * distHA <= distDI) && !EqualKey(kE, kA) && !EqualKey(kB, kA))
            info.w += 16.0;

        if ((STEEP_DIRECTION_THRESHOLD * distDI <= distHA) && !EqualKey(kE, kI) && !EqualKey(kF, kI))
            info.w += 64.0;
    }

    if (blendResult.y == BLEND_DOMINANT || (blendResult.y == BLEND_NORMAL &&
        !((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) || (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
          (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) && IsPixEqual(B, A) && !IsPixEqual(E, C)))))
    {
        info.y += 4.0;

        float distBI = DistYCbCr(B, I);
        float distFA = DistYCbCr(F, A);

        if ((STEEP_DIRECTION_THRESHOLD * distBI <= distFA) && !EqualKey(kE, kI) && !EqualKey(kH, kI))
            info.y += 16.0;

        if ((STEEP_DIRECTION_THRESHOLD * distFA <= distBI) && !EqualKey(kE, kA) && !EqualKey(kD, kA))
            info.y += 64.0;
    }

    if (blendResult.x == BLEND_DOMINANT || (blendResult.x == BLEND_NORMAL &&
        !((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) || (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
          (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) && IsPixEqual(D, G) && !IsPixEqual(E, A)))))
    {
        info.x += 4.0;

        float distDC = DistYCbCr(D, C);
        float distBG = DistYCbCr(B, G);

        if ((STEEP_DIRECTION_THRESHOLD * distDC <= distBG) && !EqualKey(kE, kC) && !EqualKey(kF, kC))
            info.x += 16.0;

        if ((STEEP_DIRECTION_THRESHOLD * distBG <= distDC) && !EqualKey(kE, kG) && !EqualKey(kH, kG))
            info.x += 64.0;
    }

    oColor = info / 255.0;
}
