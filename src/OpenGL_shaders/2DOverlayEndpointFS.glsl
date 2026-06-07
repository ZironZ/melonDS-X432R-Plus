#version 140

uniform sampler2D Source3DTex;
uniform bool uWhiteEndpoint;

smooth in vec2 fTexcoord;

out vec4 oColor;

void main()
{
    ivec2 size = textureSize(Source3DTex, 0);
    ivec2 coord = clamp(ivec2(floor(fTexcoord * vec2(size))), ivec2(0), size - ivec2(1));
    float alpha = clamp(texelFetch(Source3DTex, coord, 0).a, 0.0, 1.0);
    float value = uWhiteEndpoint ? 1.0 : 0.0;

    oColor = vec4(vec3(value), alpha);
}
