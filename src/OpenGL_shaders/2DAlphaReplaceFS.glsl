#version 140

uniform sampler2D Source;
uniform sampler2D AlphaSource;

smooth in vec2 fTexcoord;

out vec4 oColor;

void main()
{
    vec4 color = texture(Source, fTexcoord);
    vec4 alpha = texture(AlphaSource, fTexcoord);
    oColor = vec4(color.rgb, alpha.a);
}
