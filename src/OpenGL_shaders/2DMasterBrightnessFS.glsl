#version 140

uniform sampler2D SourceTex;
uniform int uBrightMode;
uniform int uBrightFactor;

smooth in vec2 fTexcoord;

out vec4 oColor;

ivec3 MasterBrightness(ivec3 color, int brightmode, int evy)
{
    if (brightmode == 1)
    {
        color += (((0x3F - color) * evy) >> 4);
    }
    else if (brightmode == 2)
    {
        color -= (((color * evy) + 0xF) >> 4);
    }

    return color;
}

void main()
{
    vec4 source = texture(SourceTex, fTexcoord);
    ivec4 color = ivec4(source * 255.0) >> 2;
    color.rgb = MasterBrightness(color.rgb, uBrightMode, uBrightFactor);
    color.rgb = (color.rgb << 2) | (color.rgb >> 6);
    oColor = vec4(vec3(color.rgb) / 255.0, source.a);
}
