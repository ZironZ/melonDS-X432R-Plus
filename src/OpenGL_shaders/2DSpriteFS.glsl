#version 140

uniform sampler2D SpriteTex;
uniform sampler2DArray Capture128Tex;
uniform sampler2DArray Capture256Tex;
uniform int uSpriteFilterMode;

struct sOAM
{
    ivec2 Position;
    bvec2 Flip;
    ivec2 Size;
    ivec2 BoundSize;
    int OBJMode;
    int Type;
    int PalOffset;
    int TileOffset;
    int TileStride;
    int Rotscale;
    int BGPrio;
    bool Mosaic;
};

layout(std140) uniform ubSpriteConfig
{
    int uVRAMMask;
    ivec4 uRotscale[32];
    sOAM uOAM[128];
};

layout(std140) uniform ubSpriteScanlineConfig
{
    ivec4 uMosaicLine[48];
};

uniform bool uRenderTransparent;

flat in int fSpriteIndex;
smooth in vec2 fPosition;
smooth in vec2 fTexcoord;

out vec4 oColor;
out vec4 oFlags;
out vec4 oCoverage;

const int SpriteFilter_Nearest = 0;
const int SpriteFilter_Spline36 = 1;

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

vec4 FetchSpriteTexel(int sprite, ivec2 coord)
{
    ivec2 basecoord = ivec2((sprite & 0xF) * 64, (sprite >> 4) * 64);
    ivec2 size = uOAM[sprite].Size;

    if (any(lessThan(coord, ivec2(0))) || any(greaterThanEqual(coord, size)))
        return vec4(0.0);

    return texelFetch(SpriteTex, basecoord + coord, 0);
}

vec4 GetSpritePixelNearest(int sprite, vec2 coord)
{
    return FetchSpriteTexel(sprite, ivec2(coord));
}

vec4 GetSpritePixelCoverage(int sprite, vec2 coord, out float coverage)
{
    vec4 nearestColor = GetSpritePixelNearest(sprite, coord);
    if (nearestColor.a <= 0.0)
    {
        coverage = 0.0;
        return vec4(0.0);
    }

    vec2 srcCoord = coord - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);
    float wx0 = 1.0 - frac.x;
    float wx1 = frac.x;
    float wy0 = 1.0 - frac.y;
    float wy1 = frac.y;

    coverage = 0.0;

    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 2; x++)
        {
            vec4 sampleColor = FetchSpriteTexel(sprite, baseCoord + ivec2(x, y));
            if (sampleColor.a <= 0.0)
                continue;

            float weight = (x == 0 ? wx0 : wx1) * (y == 0 ? wy0 : wy1);
            coverage += weight;
        }
    }

    return nearestColor;
}

vec4 GetSpritePixelSpline36(int sprite, vec2 coord)
{
    vec4 nearestColor = GetSpritePixelNearest(sprite, coord);
    if (nearestColor.a <= 0.0)
        return vec4(0.0);

    vec2 srcCoord = coord - vec2(0.5);
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

    vec3 accum = vec3(0.0);
    vec3 minColor = vec3(1.0);
    vec3 maxColor = vec3(0.0);
    float totalWeight = 0.0;

    for (int y = 0; y < 6; y++)
    {
        float wyNorm = wy[y] / wysum;
        for (int x = 0; x < 6; x++)
        {
            float weight = (wx[x] / wxsum) * wyNorm;
            vec4 sampleColor = FetchSpriteTexel(sprite, baseCoord + ivec2(x - 2, y - 2));
            if (sampleColor.a <= 0.0)
                continue;

            accum += sampleColor.rgb * weight;
            minColor = min(minColor, sampleColor.rgb);
            maxColor = max(maxColor, sampleColor.rgb);
            totalWeight += weight;
        }
    }

    if (totalWeight <= 0.00001)
        return nearestColor;

    vec3 color = clamp(accum / totalWeight, minColor, maxColor);
    return vec4(clamp(color, 0.0, 1.0), nearestColor.a);
}

vec4 GetSpritePixel(int sprite, vec2 coord)
{
    if (uSpriteFilterMode == SpriteFilter_Spline36)
        return GetSpritePixelSpline36(sprite, coord);

    return GetSpritePixelNearest(sprite, coord);
}

void main()
{
    vec4 col, flags = vec4(0);
    float coverage = 0.0;
    vec2 coord = fTexcoord;

    if (uOAM[fSpriteIndex].Mosaic)
    {
        int line = int(fPosition.y);
        int mosline = uMosaicLine[line>>2][line&0x3];

        float ymin = 0;
        if (uOAM[fSpriteIndex].Rotscale != -1)
            ymin = -float(uOAM[fSpriteIndex].Size.y) / 2.0;

        float mosy = coord.y - (line - mosline);
        if (coord.y >= ymin)
            coord.y = max(mosy, ymin);
    }

    if (uOAM[fSpriteIndex].Rotscale != -1)
    {
        // rotscale sprite
        // fTexcoord is based on the sprite center

        vec2 sprsize = vec2(uOAM[fSpriteIndex].Size);
        vec4 rotscale = vec4(uRotscale[uOAM[fSpriteIndex].Rotscale]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        coord = (coord * rsmatrix) + (sprsize / 2);
        if (any(lessThan(coord, vec2(0)))) discard;
        if (any(greaterThanEqual(coord, sprsize))) discard;
    }

    if (uRenderTransparent)
    {
        // set BG priority and mosaic flags for transparent pixels

        if (uOAM[fSpriteIndex].Mosaic)
            flags.g = 1;

        flags.a = float(uOAM[fSpriteIndex].BGPrio) / 255;

        oColor = vec4(0);
        oFlags = flags;
        oCoverage = vec4(0);
        return;
    }

    if (uOAM[fSpriteIndex].Type == 3)
    {
        coord += (ivec2(uOAM[fSpriteIndex].TileOffset) >> ivec2(1, 8));
        coord *= (1.0/128.0);
        col = texture(Capture256Tex, vec3(fract(coord), uOAM[fSpriteIndex].TileStride));
        coverage = col.a;
    }
    else if (uOAM[fSpriteIndex].Type == 4)
    {
        coord += (ivec2(uOAM[fSpriteIndex].TileOffset) >> ivec2(1, 9));
        coord *= (1.0/256.0);
        col = texture(Capture256Tex, vec3(fract(coord), uOAM[fSpriteIndex].TileStride));
        coverage = col.a;
    }
    else
    {
        col = GetSpritePixelCoverage(fSpriteIndex, coord, coverage);
        if (uSpriteFilterMode == SpriteFilter_Spline36)
        {
            vec4 filteredColor = GetSpritePixelSpline36(fSpriteIndex, coord);
            if (filteredColor.a > 0.0)
                col = filteredColor;
        }
    }

    if (col.a == 0) discard;

    // oFlags:
    // r = sprite blending flag
    // g = mosaic flag
    // b = OBJ window flag
    // a = BG prio

    if (uOAM[fSpriteIndex].OBJMode == 2)
    {
        // OBJ window
        // OBJ mosaic doesn't apply to "OBJ window" sprites
        flags.b = 1;
    }
    else
    {
        if (uOAM[fSpriteIndex].OBJMode == 1)
        {
            // semi-transparent sprite
            flags.r = 1.0 / 255;
        }
        else if (uOAM[fSpriteIndex].OBJMode == 3)
        {
            // bitmap sprite
            col.a = float(uOAM[fSpriteIndex].PalOffset) / 31;
            flags.r = 2.0 / 255;
        }

        if (uOAM[fSpriteIndex].Mosaic)
            flags.g = 1;

        flags.a = float(uOAM[fSpriteIndex].BGPrio) / 255;
    }

    oColor = col;
    oFlags = flags;
    oCoverage = vec4(coverage, 0.0, 0.0, 0.0);
}
