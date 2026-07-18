#version 140

#ifdef FILTERABLE_TEXTURE_CACHE
uniform sampler2DArray CurTexture;
#else
uniform usampler2DArray CurTexture;
#endif
uniform sampler2DArray Capture128Texture;
uniform sampler2DArray Capture256Texture;
uniform vec4 uTextureNormalize;
uniform int uBinaryAlphaTexture;

layout(std140) uniform uConfig
{
    vec2 uScreenSize;
    int uDispCnt;
    vec4 uToonColors[32];
    vec4 uEdgeColors[8];
    vec4 uFogColor;
    float uFogDensity[34];
    int uFogOffset;
    int uFogShift;
};

uniform int uRenderMode; // 0=opaque 1=translucent 2=shadowmask

smooth in vec4 fColor;
smooth in vec2 fTexcoord;
flat in vec4 fTexcoordInsetBounds;
flat in ivec3 fPolygonAttr;
flat in int fTexRepeat;
flat in int fForceNearestTexture;

#ifdef WBuffer
smooth in float fZ;
#endif

out vec4 oColor;
out vec4 oAttr;

int WrapNearestCoord(float scaledCoord, int size, bool repeat, bool mirror)
{
    int coord = int(floor(scaledCoord));
    if (repeat)
    {
        if (mirror)
        {
            int period = size * 2;
            coord = coord % period;
            if (coord < 0)
                coord += period;
            if (coord >= size)
                coord = period - 1 - coord;
        }
        else
        {
            coord = coord % size;
            if (coord < 0)
                coord += size;
        }
    }
    else
    {
        coord = clamp(coord, 0, size - 1);
    }
    return coord;
}

ivec2 WrapNearestTexelCoord(vec2 texcoord, ivec2 texSize)
{
    return ivec2(
        WrapNearestCoord(texcoord.x * float(texSize.x), texSize.x, (fTexRepeat & 1) != 0, (fTexRepeat & 4) != 0),
        WrapNearestCoord(texcoord.y * float(texSize.y), texSize.y, (fTexRepeat & 2) != 0, (fTexRepeat & 8) != 0));
}

#ifdef FILTERABLE_TEXTURE_CACHE
vec4 SampleCachedTextureNearest(vec3 texcoord)
{
    ivec2 texSize = textureSize(CurTexture, 0).xy;
    ivec2 texelCoord = WrapNearestTexelCoord(texcoord.xy, texSize);
    return texelFetch(CurTexture, ivec3(texelCoord, int(texcoord.z)), 0) * (255.0 / uTextureNormalize);
}

vec4 SampleCachedTextureAtLod(vec3 texcoord, float lod)
{
    return textureLod(CurTexture, texcoord, lod) * (255.0 / uTextureNormalize);
}

vec4 SampleCachedTexture(vec3 texcoord)
{
    vec2 texSize = vec2(textureSize(CurTexture, 0).xy);
    vec2 dx = dFdx(texcoord.xy) * texSize;
    vec2 dy = dFdy(texcoord.xy) * texSize;
    float lenX = length(dx);
    float lenY = length(dy);
    float majorLen = max(lenX, lenY);
    float minorLen = max(min(lenX, lenY), 0.00001);

    if (majorLen <= 1.0)
        return SampleCachedTextureAtLod(texcoord, 0.0);

    vec2 majorVec = lenX >= lenY ? dx : dy;
    float sampleCountF = 1.0;
    float maxAniso = float(TEXTURE_ANISOTROPY);
    float effectiveMinor = minorLen;
    float ratio = majorLen / minorLen;
    if (ratio > 1.0)
    {
        sampleCountF = min(maxAniso, floor(ratio + 0.5));
        effectiveMinor = max(minorLen, majorLen / maxAniso);
        if (effectiveMinor < 1.0)
            sampleCountF = max(1.0, floor(sampleCountF * effectiveMinor + 0.5));
    }

    float lod = log2(max(effectiveMinor, 1.0));
    if (sampleCountF <= 1.0)
        return SampleCachedTextureAtLod(texcoord, lod);

    vec2 axisDir = length(majorVec) > 0.0 ? normalize(majorVec) / texSize : vec2(0.0);
    vec2 span = axisDir * (0.5 * majorLen);
    int sampleCount = int(sampleCountF);
    vec4 accum = vec4(0.0);
    for (int i = 0; i < TEXTURE_ANISOTROPY; i++)
    {
        if (i >= sampleCount)
            break;

        float t = (float(i) + 0.5) / sampleCountF;
        vec2 uv = texcoord.xy + mix(-span, span, t);
        accum += SampleCachedTextureAtLod(vec3(uv, texcoord.z), lod);
    }
    return accum / sampleCountF;
}
#else
vec4 SampleCachedTextureNearest(vec3 texcoord)
{
    ivec2 texSize = textureSize(CurTexture, 0).xy;
    ivec2 texelCoord = WrapNearestTexelCoord(texcoord.xy, texSize);
    return vec4(texelFetch(CurTexture, ivec3(texelCoord, int(texcoord.z)), 0)) / uTextureNormalize;
}

vec4 SampleCachedTexture(vec3 texcoord)
{
    return vec4(texture(CurTexture, texcoord)) / uTextureNormalize;
}
#endif

vec4 FinalColor()
{
    vec4 col;
    vec4 vcol = fColor;
    int blendmode = (fPolygonAttr.x >> 4) & 0x3;

    if (blendmode == 2)
    {
        if ((uDispCnt & (1<<1)) == 0)
        {
            // toon
            vec3 tooncolor = uToonColors[int(vcol.r * 31)].rgb;
            vcol.rgb = tooncolor;
        }
        else
        {
            // highlight
            vcol.rgb = vcol.rrr;
        }
    }

    if (fPolygonAttr.y == 0xFFFF)
    {
        // no texture
        col = vcol;
    }
    else
    {
        vec2 texcoordXY = fTexcoord;
        if (fTexcoordInsetBounds.z > fTexcoordInsetBounds.x &&
            fTexcoordInsetBounds.w > fTexcoordInsetBounds.y)
        {
            texcoordXY = clamp(texcoordXY, fTexcoordInsetBounds.xy, fTexcoordInsetBounds.zw);
        }
        vec3 texcoord = vec3(texcoordXY, fPolygonAttr.y);
        vec4 tcol;
        if (fPolygonAttr.z == 0)
        {
            tcol = fForceNearestTexture != 0 ? SampleCachedTextureNearest(texcoord) : SampleCachedTexture(texcoord);
            if (uBinaryAlphaTexture != 0)
            {
                tcol.a = tcol.a >= 0.5 ? 1.0 : 0.0;
                if (tcol.a == 0.0)
                    tcol.rgb = vec3(0.0);
            }
        }
        else if (fPolygonAttr.z == 1)
            tcol = texture(Capture128Texture, texcoord);
        else
            tcol = texture(Capture256Texture, texcoord);

        if ((blendmode & 1) != 0)
        {
            // decal
            col.rgb = (tcol.rgb * tcol.a) + (vcol.rgb * (1.0-tcol.a));
            col.a = vcol.a;
        }
        else
        {
            // modulate
            col = vcol * tcol;
        }
    }

    if (blendmode == 2)
    {
        if ((uDispCnt & (1<<1)) != 0)
        {
            vec3 tooncolor = uToonColors[int(vcol.r * 31)].rgb;
            col.rgb = min(col.rgb + tooncolor, 1.0);
        }
    }

    return col.rgba;
}

void main()
{
    if (uRenderMode == 2)
    {
        oColor = vec4(0,0,0,1);
    }
    else
    {
        vec4 col = FinalColor();
        if (uRenderMode == 0)
        {
            // opaque pixels
            if (col.a < 30.5/31) discard;

            oAttr.r = float((fPolygonAttr.x >> 24) & 0x3F) / 63.0;
            oAttr.g = 0;
            oAttr.b = float((fPolygonAttr.x >> 15) & 0x1);
            oAttr.a = 1;
        }
        else
        {
            // translucent pixels
            if (col.a < 0.5/31) discard;
            if (col.a >= 30.5/31) discard;

            oAttr.b = 0;
            oAttr.a = 1;
        }

        oColor = col;
    }

#ifdef WBuffer
    gl_FragDepth = fZ;
#endif
}
