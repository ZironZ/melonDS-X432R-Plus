// ArtCNN C4F16 DN - https://github.com/Artoriuz/ArtCNN
// Model weights by Artoriuz, ported to GLSL compute for melonDS X432R+.

#version 430 core
// Generated from ArtCNN_C4F16_DN.glsl, pass: Depth-To-Space.
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
