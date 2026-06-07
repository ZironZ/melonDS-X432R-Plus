#version 140

uniform vec2 uOutputSize;

out vec4 oColor;
uniform sampler2D conv2d_6;

ivec2 artcnn_output_pixel(vec2 output_size)
{
    ivec2 size_i = ivec2(output_size);
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy));
    return clamp(pixel, ivec2(0), size_i - ivec2(1));
}

bool artcnn_in_bounds(ivec2 coord, ivec2 texture_size)
{
    return all(greaterThanEqual(coord, ivec2(0))) && all(lessThan(coord, texture_size));
}

float artcnn_fetch_scalar(sampler2D tex, ivec2 pos)
{
    ivec2 tex_size = textureSize(tex, 0);
    return artcnn_in_bounds(pos, tex_size) ? texelFetch(tex, pos, 0).x : 0.0;
}

vec4 artcnn_fetch4(sampler2D tex, ivec2 pos)
{
    ivec2 tex_size = textureSize(tex, 0);
    return artcnn_in_bounds(pos, tex_size) ? texelFetch(tex, pos, 0) : vec4(0.0);
}

void main()
{
    ivec2 output_pixel = artcnn_output_pixel(uOutputSize);
    ivec2 base = output_pixel / 2;
    ivec2 phase = output_pixel - (base * 2);
    vec4 packedValue = artcnn_fetch4(conv2d_6, base);
    int phaseIndex = phase.y * 2 + phase.x;
    float value =
        (phaseIndex == 0) ? packedValue.x :
        (phaseIndex == 1) ? packedValue.y :
        (phaseIndex == 2) ? packedValue.z :
                            packedValue.w;
    oColor = vec4(clamp(value, 0.0, 1.0), 0.0, 0.0, 1.0);
}
