#version 140

uniform sampler2D Source;
uniform sampler2D InfoTex;
uniform vec2 uOutputSize;

in vec2 fTexcoord;
out vec4 oColor;

const int BLEND_NONE = 0;
const int BLEND_NORMAL = 1;
const int BLEND_DOMINANT = 2;

const float LUMINANCE_WEIGHT = 1.0;
const float EQUAL_COLOR_TOLERANCE = 30.0 / 255.0;
const float STEEP_DIRECTION_THRESHOLD = 2.2;

vec4 FetchTexture(sampler2D tex, ivec2 center, ivec2 offset)
{
    ivec2 size = textureSize(tex, 0);
    ivec2 pos = clamp(center + offset, ivec2(0), size - ivec2(1));
    return texelFetch(tex, pos, 0);
}

float DistYCbCr(vec4 pixA, vec4 pixB)
{
    const vec3 w = vec3(0.2627, 0.6780, 0.0593);
    const float scaleB = 0.5 / (1.0 - w.b);
    const float scaleR = 0.5 / (1.0 - w.r);

    vec4 diff = pixA - pixB;
    float Y = dot(diff.rgb, w);
    float Cb = scaleB * (diff.b - Y);
    float Cr = scaleR * (diff.r - Y);
    float colorDist = ((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr);

    return sqrt((pixA.a * pixB.a * colorDist) + (diff.a * diff.a));
}

float GetLeftRatio(vec2 centerPos, vec2 origin, vec2 direction, vec2 scale)
{
    vec2 p0 = centerPos - origin;
    vec2 proj = direction * (dot(p0, direction) / dot(direction, direction));
    vec2 distv = p0 - proj;
    vec2 orth = vec2(-direction.y, direction.x);
    float side = sign(dot(p0, orth));
    float v = side * length(distv * scale);

    return smoothstep(-sqrt(2.0) * 0.5, sqrt(2.0) * 0.5, v);
}

void main()
{
    ivec2 srcSize = textureSize(Source, 0);
    ivec2 outputSize = ivec2(uOutputSize);
    ivec2 outputPixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), outputSize - ivec2(1));

    vec2 srcPixel = ((vec2(outputPixel) + vec2(0.5)) * vec2(srcSize)) / uOutputSize;
    ivec2 center = clamp(ivec2(floor(srcPixel)), ivec2(0), srcSize - ivec2(1));
    vec2 pos = fract(srcPixel) - vec2(0.5);
    vec2 scale = uOutputSize / vec2(srcSize);

    vec4 B = FetchTexture(Source, center, ivec2( 0, -1));
    vec4 D = FetchTexture(Source, center, ivec2(-1,  0));
    vec4 E = FetchTexture(Source, center, ivec2( 0,  0));
    vec4 F = FetchTexture(Source, center, ivec2( 1,  0));
    vec4 H = FetchTexture(Source, center, ivec2( 0,  1));

    ivec4 info = ivec4(floor((FetchTexture(InfoTex, center, ivec2(0)) * 255.0) + 0.5));
    ivec4 blendResult = info % 4;
    ivec4 doLineBlend = (info / 4) % 4;
    ivec4 haveShallowLine = (info / 16) % 4;
    ivec4 haveSteepLine = (info / 64) % 4;

    vec4 res = E;

    if (blendResult.z > BLEND_NONE)
    {
        vec2 origin = vec2(0.0, 1.0 / sqrt(2.0));
        vec2 direction = vec2(1.0, -1.0);
        if (doLineBlend.z > 0)
        {
            origin = (haveShallowLine.z > 0) ? vec2(0.0, 0.25) : vec2(0.0, 0.5);
            direction.x += float(haveShallowLine.z);
            direction.y -= float(haveSteepLine.z);
        }

        vec4 blendPix = mix(H, F, step(DistYCbCr(E, F), DistYCbCr(E, H)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }

    if (blendResult.w > BLEND_NONE)
    {
        vec2 origin = vec2(-1.0 / sqrt(2.0), 0.0);
        vec2 direction = vec2(1.0, 1.0);
        if (doLineBlend.w > 0)
        {
            origin = (haveShallowLine.w > 0) ? vec2(-0.25, 0.0) : vec2(-0.5, 0.0);
            direction.y += float(haveShallowLine.w);
            direction.x += float(haveSteepLine.w);
        }

        vec4 blendPix = mix(H, D, step(DistYCbCr(E, D), DistYCbCr(E, H)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }

    if (blendResult.y > BLEND_NONE)
    {
        vec2 origin = vec2(1.0 / sqrt(2.0), 0.0);
        vec2 direction = vec2(-1.0, -1.0);
        if (doLineBlend.y > 0)
        {
            origin = (haveShallowLine.y > 0) ? vec2(0.25, 0.0) : vec2(0.5, 0.0);
            direction.y -= float(haveShallowLine.y);
            direction.x -= float(haveSteepLine.y);
        }

        vec4 blendPix = mix(F, B, step(DistYCbCr(E, B), DistYCbCr(E, F)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }

    if (blendResult.x > BLEND_NONE)
    {
        vec2 origin = vec2(0.0, -1.0 / sqrt(2.0));
        vec2 direction = vec2(-1.0, 1.0);
        if (doLineBlend.x > 0)
        {
            origin = (haveShallowLine.x > 0) ? vec2(0.0, -0.25) : vec2(0.0, -0.5);
            direction.x -= float(haveShallowLine.x);
            direction.y += float(haveSteepLine.x);
        }

        vec4 blendPix = mix(D, B, step(DistYCbCr(E, B), DistYCbCr(E, D)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }

    oColor = res;
}
