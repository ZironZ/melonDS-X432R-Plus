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
// Generated from ArtCNN_C4F16.glsl, pass: Depth-To-Space.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, binding = 0) writeonly uniform image2D out_image;

uniform sampler2D conv2d_6_raw;
uniform vec2 LUMA_size;

vec4 artcnn_texel_fetch(sampler2D tex, ivec2 pos)
{
    ivec2 size = textureSize(tex, 0);
    if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, size)))
        return vec4(0.0);
    return texelFetch(tex, pos, 0);
}

void main()
{
    ivec2 output_pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 output_size = ivec2(LUMA_size) * ivec2(2, 2);
    if (any(greaterThanEqual(output_pixel, output_size)))
        return;

    ivec2 base = output_pixel / ivec2(2, 2);
    ivec2 phase = output_pixel - base * ivec2(2, 2);
    vec4 packed_value = artcnn_texel_fetch(conv2d_6_raw, base);
    int phase_index = phase.y * 2 + phase.x;
    float value =
        (phase_index == 0) ? packed_value.x :
        (phase_index == 1) ? packed_value.y :
        (phase_index == 2) ? packed_value.z :
                             packed_value.w;
    imageStore(out_image, output_pixel, vec4(clamp(value, 0.0, 1.0), 0.0, 0.0, 1.0));
}
