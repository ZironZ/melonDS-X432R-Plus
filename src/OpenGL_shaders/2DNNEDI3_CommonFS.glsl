uniform vec2 uOutputSize;
uniform sampler2D Source;

smooth in vec2 fTexcoord;
out vec4 oColor;

float nnedi3_core(vec4 samples[8]);

ivec2 nnedi3_output_pixel()
{
    ivec2 outputSize = ivec2(uOutputSize);
    ivec2 pixel = ivec2(floor(fTexcoord * uOutputSize));
    return clamp(pixel, ivec2(0), outputSize - ivec2(1));
}

ivec2 nnedi3_clamp_coord(ivec2 coord)
{
    ivec2 sourceSize = textureSize(Source, 0);
    return clamp(coord, ivec2(0), sourceSize - ivec2(1));
}

float nnedi3_fetch_component(ivec2 base, int dx, int dy, int comp)
{
    return texelFetch(Source, nnedi3_clamp_coord(base + ivec2(dx, dy)), 0)[comp];
}

float nnedi3_scalar(int comp)
{
    ivec2 outputPixel = nnedi3_output_pixel();
#if NNEDI3_VERTICAL
    ivec2 sourcePixel = ivec2(outputPixel.x, outputPixel.y >> 1);
    if ((outputPixel.y & 1) == 0)
        return texelFetch(Source, nnedi3_clamp_coord(sourcePixel), 0)[comp];

    vec4 samples[8];
    samples[0] = vec4(nnedi3_fetch_component(sourcePixel, -3, -1, comp), nnedi3_fetch_component(sourcePixel, -2, -1, comp), nnedi3_fetch_component(sourcePixel, -1, -1, comp), nnedi3_fetch_component(sourcePixel, 0, -1, comp));
    samples[1] = vec4(nnedi3_fetch_component(sourcePixel, 1, -1, comp), nnedi3_fetch_component(sourcePixel, 2, -1, comp), nnedi3_fetch_component(sourcePixel, 3, -1, comp), nnedi3_fetch_component(sourcePixel, 4, -1, comp));
    samples[2] = vec4(nnedi3_fetch_component(sourcePixel, -3, 0, comp), nnedi3_fetch_component(sourcePixel, -2, 0, comp), nnedi3_fetch_component(sourcePixel, -1, 0, comp), nnedi3_fetch_component(sourcePixel, 0, 0, comp));
    samples[3] = vec4(nnedi3_fetch_component(sourcePixel, 1, 0, comp), nnedi3_fetch_component(sourcePixel, 2, 0, comp), nnedi3_fetch_component(sourcePixel, 3, 0, comp), nnedi3_fetch_component(sourcePixel, 4, 0, comp));
    samples[4] = vec4(nnedi3_fetch_component(sourcePixel, -3, 1, comp), nnedi3_fetch_component(sourcePixel, -2, 1, comp), nnedi3_fetch_component(sourcePixel, -1, 1, comp), nnedi3_fetch_component(sourcePixel, 0, 1, comp));
    samples[5] = vec4(nnedi3_fetch_component(sourcePixel, 1, 1, comp), nnedi3_fetch_component(sourcePixel, 2, 1, comp), nnedi3_fetch_component(sourcePixel, 3, 1, comp), nnedi3_fetch_component(sourcePixel, 4, 1, comp));
    samples[6] = vec4(nnedi3_fetch_component(sourcePixel, -3, 2, comp), nnedi3_fetch_component(sourcePixel, -2, 2, comp), nnedi3_fetch_component(sourcePixel, -1, 2, comp), nnedi3_fetch_component(sourcePixel, 0, 2, comp));
    samples[7] = vec4(nnedi3_fetch_component(sourcePixel, 1, 2, comp), nnedi3_fetch_component(sourcePixel, 2, 2, comp), nnedi3_fetch_component(sourcePixel, 3, 2, comp), nnedi3_fetch_component(sourcePixel, 4, 2, comp));
#else
    ivec2 sourcePixel = ivec2(outputPixel.x >> 1, outputPixel.y);
    if ((outputPixel.x & 1) == 0)
        return texelFetch(Source, nnedi3_clamp_coord(sourcePixel), 0)[comp];

    vec4 samples[8];
    samples[0] = vec4(nnedi3_fetch_component(sourcePixel, -1, -3, comp), nnedi3_fetch_component(sourcePixel, -1, -2, comp), nnedi3_fetch_component(sourcePixel, -1, -1, comp), nnedi3_fetch_component(sourcePixel, -1, 0, comp));
    samples[1] = vec4(nnedi3_fetch_component(sourcePixel, -1, 1, comp), nnedi3_fetch_component(sourcePixel, -1, 2, comp), nnedi3_fetch_component(sourcePixel, -1, 3, comp), nnedi3_fetch_component(sourcePixel, -1, 4, comp));
    samples[2] = vec4(nnedi3_fetch_component(sourcePixel, 0, -3, comp), nnedi3_fetch_component(sourcePixel, 0, -2, comp), nnedi3_fetch_component(sourcePixel, 0, -1, comp), nnedi3_fetch_component(sourcePixel, 0, 0, comp));
    samples[3] = vec4(nnedi3_fetch_component(sourcePixel, 0, 1, comp), nnedi3_fetch_component(sourcePixel, 0, 2, comp), nnedi3_fetch_component(sourcePixel, 0, 3, comp), nnedi3_fetch_component(sourcePixel, 0, 4, comp));
    samples[4] = vec4(nnedi3_fetch_component(sourcePixel, 1, -3, comp), nnedi3_fetch_component(sourcePixel, 1, -2, comp), nnedi3_fetch_component(sourcePixel, 1, -1, comp), nnedi3_fetch_component(sourcePixel, 1, 0, comp));
    samples[5] = vec4(nnedi3_fetch_component(sourcePixel, 1, 1, comp), nnedi3_fetch_component(sourcePixel, 1, 2, comp), nnedi3_fetch_component(sourcePixel, 1, 3, comp), nnedi3_fetch_component(sourcePixel, 1, 4, comp));
    samples[6] = vec4(nnedi3_fetch_component(sourcePixel, 2, -3, comp), nnedi3_fetch_component(sourcePixel, 2, -2, comp), nnedi3_fetch_component(sourcePixel, 2, -1, comp), nnedi3_fetch_component(sourcePixel, 2, 0, comp));
    samples[7] = vec4(nnedi3_fetch_component(sourcePixel, 2, 1, comp), nnedi3_fetch_component(sourcePixel, 2, 2, comp), nnedi3_fetch_component(sourcePixel, 2, 3, comp), nnedi3_fetch_component(sourcePixel, 2, 4, comp));
#endif
    return nnedi3_core(samples);
}
