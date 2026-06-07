#version 140

uniform vec2 uOutputSize;
uniform vec2 uSourceShift;
uniform sampler2D Source;

out vec4 oColor;

float Spline36Weight(float x)
{
    x = abs(x);

    if (x < 1.0)
        return (((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0);
    else if (x < 2.0)
        return (((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0);
    else if (x < 3.0)
        return (((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0);

    return 0.0;
}

void main()
{
    vec2 srcSize = vec2(textureSize(Source, 0));
    vec2 srcCoord = (gl_FragCoord.xy * srcSize / uOutputSize) - vec2(0.5) + uSourceShift;
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);

    float wx[6];
    float wy[6];
    float wxsum = 0.0;
    float wysum = 0.0;
    for (int i = 0; i < 6; i++)
    {
        wx[i] = Spline36Weight(float(i - 2) - frac.x);
        wy[i] = Spline36Weight(float(i - 2) - frac.y);
        wxsum += wx[i];
        wysum += wy[i];
    }

    ivec2 texSize = textureSize(Source, 0);
    vec4 accum = vec4(0.0);
    vec4 minColor = vec4(1.0);
    vec4 maxColor = vec4(0.0);
    float totalWeight = 0.0;

    for (int y = 0; y < 6; y++)
    {
        float wyNorm = wy[y] / wysum;
        int sampleY = clamp(baseCoord.y + y - 2, 0, texSize.y - 1);

        for (int x = 0; x < 6; x++)
        {
            float weight = (wx[x] / wxsum) * wyNorm;
            int sampleX = clamp(baseCoord.x + x - 2, 0, texSize.x - 1);
            vec4 sampleColor = texelFetch(Source, ivec2(sampleX, sampleY), 0);
            accum += sampleColor * weight;
            minColor = min(minColor, sampleColor);
            maxColor = max(maxColor, sampleColor);
            totalWeight += weight;
        }
    }

    vec4 color;
    if (totalWeight <= 0.00001)
        color = texelFetch(Source, clamp(baseCoord, ivec2(0), texSize - ivec2(1)), 0);
    else
        color = accum / totalWeight;

    color = clamp(color, minColor, maxColor);
    oColor = clamp(color, 0.0, 1.0);
}
