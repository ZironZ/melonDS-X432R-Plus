/*
    ArtCNN - https://github.com/Artoriuz/ArtCNN
    Model weights Copyright (c) 2024 João Chrisóstomo

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

    GLSL compute port Copyright 2026 ZironZ. The port is part of melonDS and is
    distributed under GPL-3.0-or-later.
*/

#version 430 core
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : enable
#ifdef GL_EXT_shader_explicit_arithmetic_types_float16
#   define V4 f16vec4
#   define M4 f16mat4
#   define F float16_t
#else
#   define V4 vec4
#   define M4 mat4
#   define F float
#endif

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, binding = 0) writeonly uniform image2D out_image;

uniform sampler2D LUMA_raw;
uniform sampler2D conv2d_raw;
uniform sampler2D conv2d_1_raw;
uniform sampler2D conv2d_2_raw;
uniform sampler2D conv2d_3_raw;
uniform sampler2D conv2d_4_raw;
uniform sampler2D conv2d_5_raw;
uniform sampler2D conv2d_6_raw;

uniform float LUMA_mul;
uniform float conv2d_mul;
uniform float conv2d_1_mul;
uniform float conv2d_2_mul;
uniform float conv2d_3_mul;
uniform float conv2d_4_mul;
uniform float conv2d_5_mul;
uniform float conv2d_6_mul;

uniform vec2 LUMA_size;
uniform vec2 conv2d_size;
uniform vec2 conv2d_1_size;
uniform vec2 conv2d_2_size;
uniform vec2 conv2d_3_size;
uniform vec2 conv2d_4_size;
uniform vec2 conv2d_5_size;
uniform vec2 conv2d_6_size;
uniform vec2 LUMA_pt;
uniform vec2 conv2d_pt;
uniform vec2 conv2d_1_pt;
uniform vec2 conv2d_2_pt;
uniform vec2 conv2d_3_pt;
uniform vec2 conv2d_4_pt;
uniform vec2 conv2d_5_pt;
uniform vec2 conv2d_6_pt;

vec4 artcnn_texel_fetch(sampler2D tex, ivec2 pos)
{
    ivec2 size = textureSize(tex, 0);
    if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, size)))
        return vec4(0.0);
    return texelFetch(tex, pos, 0);
}

//!DESC ArtCNN C4F16 (Conv2D)
//!COMPUTE 24 32 12 16
//!HOOK LUMA
//!BIND LUMA
//!SAVE conv2d
//!WIDTH LUMA.w 2.0 *
//!HEIGHT LUMA.h 2.0 *
//!COMPONENTS 4
//!WHEN OUTPUT.w LUMA.w / 1.3 > OUTPUT.h LUMA.h / 1.3 > *
const ivec2 ksize = ivec2(3, 3);
const ivec2 offset = ksize / 2;
const ivec2 wg_size = ivec2(8, 8);
const ivec2 isize = ivec2(10, 10);
shared F inp[1][10][10];
void main() {
    const uvec2 local_xy = gl_LocalInvocationID.xy;
    ivec2 base = ivec2(gl_WorkGroupID) * wg_size;
    for (uint y = local_xy.y; y < isize.y; y += wg_size.y) {
        for (uint x = local_xy.x; x < isize.x; x += wg_size.x) {
            const ivec2 input_base = (base + ivec2(x,y) - offset) * ivec2(1, 1);
            inp[0][y][x] = F(LUMA_mul * artcnn_texel_fetch(LUMA_raw, input_base + ivec2(0, 0)).x);
        }
    }

    barrier();
    if (gl_GlobalInvocationID.x >= uint(LUMA_size.x) || gl_GlobalInvocationID.y >= uint(LUMA_size.y))
        return;
    V4 result0 = V4(-0.0027198044, -0.013629392, -0.015712878, -0.050803013);
    V4 result1 = V4(-0.02707489, -0.0062177293, 0.0026368732, -0.0029379292);
    V4 result2 = V4(0.03127001, -0.0039273943, -0.0040966137, -0.0016518718);
    V4 result3 = V4(0.0028380281, 0.00058883557, 0.013085538, -0.058857743);
    const F inp_0_0_0 = inp[0][local_xy.y + 0][local_xy.x + 0];
    const F inp_0_1_0 = inp[0][local_xy.y + 0][local_xy.x + 1];
    const F inp_0_2_0 = inp[0][local_xy.y + 0][local_xy.x + 2];
    const F inp_0_0_1 = inp[0][local_xy.y + 1][local_xy.x + 0];
    const F inp_0_1_1 = inp[0][local_xy.y + 1][local_xy.x + 1];
    const F inp_0_2_1 = inp[0][local_xy.y + 1][local_xy.x + 2];
    const F inp_0_0_2 = inp[0][local_xy.y + 2][local_xy.x + 0];
    const F inp_0_1_2 = inp[0][local_xy.y + 2][local_xy.x + 1];
    const F inp_0_2_2 = inp[0][local_xy.y + 2][local_xy.x + 2];
    result0 += V4(-0.016452063, -0.1258466, 0.013886958, 0.036870774) * inp_0_0_0;
    result0 += V4(0.04311634, 0.15515013, 0.12190506, 0.12543218) * inp_0_1_0;
    result0 += V4(-0.0049624983, 0.1029244, -0.10124424, 0.06448426) * inp_0_2_0;
    result0 += V4(0.001886782, 0.06120591, 0.020384936, 0.16804346) * inp_0_0_1;
    result0 += V4(-0.04256893, -0.07616671, -0.37889892, 0.27856478) * inp_0_1_1;
    result0 += V4(-0.20398517, -0.12900643, 0.113083735, 0.11175711) * inp_0_2_1;
    result0 += V4(0.009553091, 0.13118562, -0.031063978, 0.09478131) * inp_0_0_2;
    result0 += V4(0.066157505, -0.114692695, 0.22418123, -0.009412468) * inp_0_1_2;
    result0 += V4(0.15508306, 0.011386595, 0.014014352, 0.09318008) * inp_0_2_2;
    result1 += V4(0.08046117, -0.07086712, -0.102300294, 0.014950261) * inp_0_0_0;
    result1 += V4(-0.06476857, -0.014190924, -0.017589286, -0.19119741) * inp_0_1_0;
    result1 += V4(0.05054515, 0.115604624, 0.06517106, 0.13799176) * inp_0_2_0;
    result1 += V4(-0.045681432, 0.08269155, 0.10319298, -0.026858954) * inp_0_0_1;
    result1 += V4(0.11229104, -0.17059296, 0.13794285, 0.18026339) * inp_0_1_1;
    result1 += V4(-0.1267971, 0.23877597, -0.18725446, -0.12132741) * inp_0_2_1;
    result1 += V4(0.05785694, -0.015154775, 0.026422592, 0.002328838) * inp_0_0_2;
    result1 += V4(0.07150728, -0.22784448, -0.12155527, 0.027110105) * inp_0_1_2;
    result1 += V4(-0.08247087, 0.06362491, 0.08973536, -0.02196324) * inp_0_2_2;
    result2 += V4(-0.06092033, 0.1256232, -0.11233013, -0.061837807) * inp_0_0_0;
    result2 += V4(0.08898802, -0.028417582, 0.15791786, -0.01610648) * inp_0_1_0;
    result2 += V4(0.06330266, -0.009340407, 0.017859828, -0.007937439) * inp_0_2_0;
    result2 += V4(-0.17722517, 0.31189576, 0.32109433, 0.18112311) * inp_0_0_1;
    result2 += V4(-0.2903746, -0.72364086, -0.3329427, -0.08360631) * inp_0_1_1;
    result2 += V4(0.14228302, 0.11720193, -0.056604996, -0.027815754) * inp_0_2_1;
    result2 += V4(0.035853237, 0.118430145, -0.12544365, -0.02719196) * inp_0_0_2;
    result2 += V4(0.20537417, 0.07353585, 0.10881828, 0.1451791) * inp_0_1_2;
    result2 += V4(-0.1517126, -0.010349405, 0.018765846, -0.09707698) * inp_0_2_2;
    result3 += V4(0.052764144, -0.10130216, 0.22795214, -0.09385554) * inp_0_0_0;
    result3 += V4(-0.16102873, 0.18050277, 0.36273104, 0.1743911) * inp_0_1_0;
    result3 += V4(0.008320275, -0.031096114, 0.06665433, 0.047147725) * inp_0_2_0;
    result3 += V4(0.039706435, -0.0059984834, 0.026533028, -0.19475575) * inp_0_0_1;
    result3 += V4(0.017116806, -0.1657458, -0.4245533, 0.011194904) * inp_0_1_1;
    result3 += V4(0.03566397, 0.1254953, -0.16895337, 0.20406392) * inp_0_2_1;
    result3 += V4(-0.0622524, 0.11329407, -0.052762877, -0.081980705) * inp_0_0_2;
    result3 += V4(0.08946176, -0.05226282, -0.15308078, -0.0015630769) * inp_0_1_2;
    result3 += V4(-0.018317576, -0.06487258, -0.012865839, 0.13352033) * inp_0_2_2;
    const ivec2 output_base = ivec2(gl_GlobalInvocationID) * ivec2(2, 2);
    imageStore(out_image, output_base + ivec2(0, 0), result0);
    imageStore(out_image, output_base + ivec2(1, 0), result1);
    imageStore(out_image, output_base + ivec2(0, 1), result2);
    imageStore(out_image, output_base + ivec2(1, 1), result3);
}
