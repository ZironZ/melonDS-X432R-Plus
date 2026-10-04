#version 140

uniform sampler2D SpriteTex;
uniform sampler2D EnhancedSpriteTex;
uniform sampler2D AssembledSpriteTex;
uniform bool uUseAssembledSource;
uniform int uAssembledSlots[128];
uniform sampler2DArray Capture128Tex;
uniform sampler2DArray Capture256Tex;
uniform int uSpriteFilterMode;
uniform bool uAffineSourceEnhancementOnly;
uniform bool uUseEnhancedSpriteSource;
uniform int uEnhancedSpriteSourceScale;
// xy: member origin in the joined source; zw: joined source extent.
// Zero extent selects the isolated source behavior.
uniform ivec4 uEnhancedSpriteSourceRegions[128];
uniform bool uLinearEnhancedSpriteSource;
uniform int uExplicitAlphaReconstruction = -1;
uniform bool uXBRZPresentationOnly;
uniform int uAffineOBJPresentationCoverageMode;
uniform bool uOrderedPresentationBand;

const int AffineOBJCoverage_None = 0;
const int AffineOBJCoverage_ScalerAlpha = 1;
const int AffineOBJCoverage_BilinearScalerAlpha = 3;
const int AffineOBJCoverage_NativeAlpha = 5;
const int AffineOBJCoverage_ScalerContour = 6;
const int AffineOBJCoverage_Spline36Contour = 7;

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

bool ShouldFilterSprite(int sprite)
{
    return uSpriteFilterMode == SpriteFilter_Spline36 &&
           (!uAffineSourceEnhancementOnly || uOAM[sprite].Rotscale != -1);
}

bool ShouldUseEnhancedSprite(int sprite)
{
    return uUseEnhancedSpriteSource && uOAM[sprite].Rotscale != -1 &&
           uOAM[sprite].Type < 3;
}

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

vec4 FetchEnhancedSpriteTexel(int sprite, ivec2 coord);

ivec4 EnhancedSpriteRegion(int sprite)
{
    ivec4 region = uEnhancedSpriteSourceRegions[sprite];
    return region.z > 0 ? region : ivec4(0, 0, uOAM[sprite].Size);
}

vec4 GetSpritePixelEnhanced(int sprite, vec2 coord,
                            out float presentationCoverage)
{
    vec4 nearestColor = GetSpritePixelNearest(sprite, coord);
    ivec2 scale = ivec2(uEnhancedSpriteSourceScale);
    ivec2 size = uOAM[sprite].Size * scale;
    vec4 enhancedColor;
    if (uLinearEnhancedSpriteSource)
    {
        vec2 source = coord * vec2(scale) - vec2(0.5);
        ivec2 base = ivec2(floor(source));
        vec2 frac = fract(source);
        vec4 c00 = FetchEnhancedSpriteTexel(sprite, base);
        vec4 c10 = FetchEnhancedSpriteTexel(sprite, base + ivec2(1, 0));
        vec4 c01 = FetchEnhancedSpriteTexel(sprite, base + ivec2(0, 1));
        vec4 c11 = FetchEnhancedSpriteTexel(sprite, base + ivec2(1, 1));
        enhancedColor = mix(mix(c00, c10, frac.x),
                            mix(c01, c11, frac.x), frac.y);
    }
    else
    {
        ivec2 enhancedCoord = clamp(ivec2(coord * vec2(scale)),
                                    ivec2(0), size - 1);
        enhancedColor = FetchEnhancedSpriteTexel(sprite, enhancedCoord);
    }
    presentationCoverage = enhancedColor.a;
    if (uExplicitAlphaReconstruction >= 0)
    {
        // RGB interpolation and alpha interpolation are separate decisions.
        if (uExplicitAlphaReconstruction == 0 || uExplicitAlphaReconstruction == 4)
            presentationCoverage = FetchEnhancedSpriteTexel(sprite, ivec2(floor(coord * vec2(scale)))).a;
        else
        {
            vec2 p = coord * vec2(scale) - vec2(0.5);
            ivec2 b = ivec2(floor(p));
            vec2 f = fract(p);
            presentationCoverage = mix(mix(FetchEnhancedSpriteTexel(sprite, b).a,
                FetchEnhancedSpriteTexel(sprite, b + ivec2(1, 0)).a, f.x),
                mix(FetchEnhancedSpriteTexel(sprite, b + ivec2(0, 1)).a,
                FetchEnhancedSpriteTexel(sprite, b + ivec2(1, 1)).a, f.x), f.y);
        }
    }
    return vec4(enhancedColor.rgb, nearestColor.a);
}

vec4 GetEnhancedPresentationSample(int sprite, vec2 coord,
                                   out float presentationCoverage)
{
    vec2 size = vec2(uOAM[sprite].Size);
    if (any(lessThan(coord, vec2(0.0))) ||
        any(greaterThanEqual(coord, size)))
    {
        presentationCoverage = 0.0;
        return vec4(0.0);
    }
    return GetSpritePixelEnhanced(sprite, coord, presentationCoverage);
}

vec4 FetchEnhancedSpriteTexel(int sprite, ivec2 coord)
{
    ivec2 scale = ivec2(uEnhancedSpriteSourceScale);
    ivec2 basecoord = ivec2((sprite & 0xF) * 64,
                            (sprite >> 4) * 64) * scale;
    ivec4 region = EnhancedSpriteRegion(sprite);
    ivec2 size = region.zw * scale;
    coord += region.xy * scale;
    coord = clamp(coord, ivec2(0), size - ivec2(1));
    if (uUseAssembledSource && uAssembledSlots[sprite] >= 0)
    {
        int slot=uAssembledSlots[sprite];
        ivec2 assembledBase=ivec2((slot&15)*64,(slot>>4)*64)*scale;
        return texelFetch(AssembledSpriteTex,assembledBase+coord,0);
    }
    return texelFetch(EnhancedSpriteTex, basecoord + coord, 0);
}

vec4 FetchEnhancedSpriteTexelOrZero(int sprite, ivec2 coord)
{
    ivec2 scale = ivec2(uEnhancedSpriteSourceScale);
    ivec2 basecoord = ivec2((sprite & 0xF) * 64,
                            (sprite >> 4) * 64) * scale;
    ivec4 region = EnhancedSpriteRegion(sprite);
    ivec2 size = region.zw * scale;
    coord += region.xy * scale;
    if (any(lessThan(coord, ivec2(0))) ||
        any(greaterThanEqual(coord, size)))
        return vec4(0.0);
    if (uUseAssembledSource && uAssembledSlots[sprite] >= 0)
    {
        int slot=uAssembledSlots[sprite];
        ivec2 assembledBase=ivec2((slot&15)*64,(slot>>4)*64)*scale;
        return texelFetch(AssembledSpriteTex,assembledBase+coord,0);
    }
    return texelFetch(EnhancedSpriteTex, basecoord + coord, 0);
}

vec4 GetBilinearEnhancedPresentationSample(int sprite, vec2 coord,
                                           out float presentationCoverage)
{
    // Joined pixels are filtering context, not permission for this OAM
    // member to draw another member's source rectangle. Double-size affine
    // bounds can expose that area even when the original member was empty.
    if (uEnhancedSpriteSourceRegions[sprite].z > 0 &&
        (any(lessThan(coord, vec2(0.0))) ||
         any(greaterThanEqual(coord, vec2(uOAM[sprite].Size)))))
    {
        presentationCoverage = 0.0;
        return vec4(0.0);
    }
    float scale = float(uEnhancedSpriteSourceScale);
    vec2 source = coord * scale - vec2(0.5);
    ivec2 base = ivec2(floor(source));
    vec2 frac = fract(source);
    vec4 c00 = FetchEnhancedSpriteTexelOrZero(sprite, base);
    vec4 c10 = FetchEnhancedSpriteTexelOrZero(sprite,
                                               base + ivec2(1, 0));
    vec4 c01 = FetchEnhancedSpriteTexelOrZero(sprite,
                                               base + ivec2(0, 1));
    vec4 c11 = FetchEnhancedSpriteTexelOrZero(sprite,
                                               base + ivec2(1, 1));
    presentationCoverage = clamp(
        mix(mix(c00.a, c10.a, frac.x),
            mix(c01.a, c11.a, frac.x), frac.y), 0.0, 1.0);

    // Preserve xBRZ's edge-directed RGB decision. Only alpha is bilinear;
    // choose the nearest cache color, or the strongest contributing neighbor
    // when the bilinear footprint extends beyond native presentation support.
    ivec2 nearestCoord = ivec2(floor(coord * scale));
    vec4 representative = FetchEnhancedSpriteTexelOrZero(sprite,
                                                           nearestCoord);
    if (representative.a <= 0.0)
    {
        representative = c00;
        if (c10.a > representative.a) representative = c10;
        if (c01.a > representative.a) representative = c01;
        if (c11.a > representative.a) representative = c11;
    }
    return representative;
}

float GetSpritePixelBilinearAlpha(int sprite, vec2 coord)
{
    vec2 srcCoord = coord - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);
    float a00 = FetchSpriteTexel(sprite, baseCoord).a;
    float a10 = FetchSpriteTexel(sprite,
                                 baseCoord + ivec2(1, 0)).a;
    float a01 = FetchSpriteTexel(sprite,
                                 baseCoord + ivec2(0, 1)).a;
    float a11 = FetchSpriteTexel(sprite,
                                 baseCoord + ivec2(1, 1)).a;
    return clamp(mix(mix(a00, a10, frac.x),
                     mix(a01, a11, frac.x), frac.y), 0.0, 1.0);
}

vec4 GetAffineOBJPresentationColor(int sprite, vec2 coord);

vec4 GetBilinearAffineOBJPresentationSample(
    int sprite, vec2 coord, out float presentationCoverage)
{
    if (ShouldUseEnhancedSprite(sprite))
    {
        return GetBilinearEnhancedPresentationSample(
            sprite, coord, presentationCoverage);
    }

    presentationCoverage = GetSpritePixelBilinearAlpha(sprite, coord);
    return GetAffineOBJPresentationColor(sprite, coord);
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

vec4 GetSpritePixelSpline36Impl(int sprite, vec2 coord,
                                bool requireNativeCenter)
{
    vec4 nearestColor = GetSpritePixelNearest(sprite, coord);
    if (requireNativeCenter && nearestColor.a <= 0.0)
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

    if (abs(totalWeight) <= 0.00001)
        return nearestColor;

    vec3 color = clamp(accum / totalWeight, minColor, maxColor);
    return vec4(clamp(color, 0.0, 1.0), nearestColor.a);
}

vec4 GetSpritePixelSpline36(int sprite, vec2 coord)
{
    return GetSpritePixelSpline36Impl(sprite, coord, true);
}

vec4 GetSpritePixelSpline36Presentation(int sprite, vec2 coord)
{
    vec2 size = vec2(uOAM[sprite].Size);
    if (any(lessThan(coord, vec2(0.0))) ||
        any(greaterThanEqual(coord, size)))
        return vec4(0.0);
    return GetSpritePixelSpline36Impl(sprite, coord, false);
}

float GetSpritePixelSpline36Alpha(int sprite, vec2 coord)
{
    vec2 size = vec2(uOAM[sprite].Size);
    if (any(lessThan(coord, vec2(0.0))) ||
        any(greaterThanEqual(coord, size)))
        return 0.0;

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

    float alpha = 0.0;
    for (int y = 0; y < 6; y++)
    {
        float wyNorm = wy[y] / wysum;
        for (int x = 0; x < 6; x++)
        {
            float weight = (wx[x] / wxsum) * wyNorm;
            alpha += FetchSpriteTexel(
                sprite, baseCoord + ivec2(x - 2, y - 2)).a * weight;
        }
    }
    return clamp(alpha, 0.0, 1.0);
}

vec4 GetAffineOBJPresentationColor(int sprite, vec2 coord)
{
    if (ShouldUseEnhancedSprite(sprite))
    {
        float ignoredCoverage = 0.0;
        return GetEnhancedPresentationSample(sprite, coord,
                                             ignoredCoverage);
    }
    if (ShouldFilterSprite(sprite))
        return GetSpritePixelSpline36Presentation(sprite, coord);
    return GetSpritePixelNearest(sprite, coord);
}

float GetAffineOBJPresentationCoverage(int sprite, vec2 coord)
{
    if (uAffineOBJPresentationCoverageMode ==
        AffineOBJCoverage_ScalerContour)
    {
        // The cached scaler reconstructs the source silhouette, but its
        // fractional alpha is a signal prediction rather than affine
        // output-pixel area coverage. Convert the 0.5 iso-contour to binary
        // high-resolution support here. The ordered presentation policy then
        // owns sampling/integration after the affine transform instead of
        // compositing the predictor's soft fringe.
        float contour = 0.0;
        GetEnhancedPresentationSample(sprite, coord, contour);
        return contour >= 0.5 ? 1.0 : 0.0;
    }
    if (uAffineOBJPresentationCoverageMode ==
        AffineOBJCoverage_Spline36Contour)
    {
        return GetSpritePixelSpline36Alpha(sprite, coord) >= 0.5
            ? 1.0 : 0.0;
    }
    if (uAffineOBJPresentationCoverageMode ==
        AffineOBJCoverage_ScalerAlpha)
    {
        float coverage = 0.0;
        GetEnhancedPresentationSample(sprite, coord, coverage);
        return coverage;
    }
    return 0.0;
}

vec4 GetSpritePixel(int sprite, vec2 coord)
{
    if (ShouldFilterSprite(sprite))
        return GetSpritePixelSpline36(sprite, coord);

    return GetSpritePixelNearest(sprite, coord);
}

void main()
{
    vec4 col, flags = vec4(0);
    float coverage = 0.0;
    float nativePresence = 0.0;
    vec2 coord = fTexcoord;
    bool affineSourceCoordOutside = false;

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
        affineSourceCoordOutside = any(lessThan(coord, vec2(0))) ||
            any(greaterThanEqual(coord, sprsize));
        if (affineSourceCoordOutside &&
            !(uXBRZPresentationOnly &&
              uAffineOBJPresentationCoverageMode != AffineOBJCoverage_None))
            discard;
    }

    bool presentationSourceAvailable =
        ShouldUseEnhancedSprite(fSpriteIndex) ||
        uAffineOBJPresentationCoverageMode == AffineOBJCoverage_NativeAlpha;
    if (uXBRZPresentationOnly &&
        (uAffineOBJPresentationCoverageMode ==
             AffineOBJCoverage_BilinearScalerAlpha ||
         uAffineOBJPresentationCoverageMode ==
             AffineOBJCoverage_Spline36Contour))
    {
        presentationSourceAvailable = presentationSourceAvailable ||
            ShouldFilterSprite(fSpriteIndex);
    }
    if (uXBRZPresentationOnly &&
        (!presentationSourceAvailable ||
         (uOAM[fSpriteIndex].OBJMode != 0 &&
          uOAM[fSpriteIndex].OBJMode != 3) ||
         uOAM[fSpriteIndex].Mosaic))
        discard;

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
        if (uXBRZPresentationOnly)
        {
            nativePresence = affineSourceCoordOutside
                ? 0.0 : GetSpritePixelNearest(fSpriteIndex, coord).a;
            float presentationCoverage = 0.0;
            vec4 enhancedColor;
            if (uAffineOBJPresentationCoverageMode == 9)
            {
                // Exactly one observation on the requested output/subpixel grid.
                // The compositor, not this shader, resolves supersampled results.
                enhancedColor = GetEnhancedPresentationSample(fSpriteIndex, coord, presentationCoverage);
            }
            else if (uAffineOBJPresentationCoverageMode ==
                AffineOBJCoverage_BilinearScalerAlpha)
            {
                // One bilinear lookup is still a point observation of the
                // affine result. At the common 4x-output/4x-cache phase it
                // lands exactly on a cache texel center and is therefore
                // indistinguishable from nearest. Integrate the four 2x
                // quarter-pixel positions through the affine derivative so
                // this cheaper path represents the output pixel footprint
                // without allocating or composing a 2x presentation target.
                float centerCoverage = 0.0;
                enhancedColor = GetBilinearAffineOBJPresentationSample(
                    fSpriteIndex, coord, centerCoverage);
                vec2 dx = dFdx(coord) * 0.25;
                vec2 dy = dFdy(coord) * 0.25;
                vec2 offsets[4] = vec2[4](
                    -dx - dy, dx - dy, -dx + dy, dx + dy);
                float bestCoverage = centerCoverage;
                presentationCoverage = 0.0;
                for (int tap = 0; tap < 4; tap++)
                {
                    float tapCoverage = 0.0;
                    vec4 tapColor =
                        GetBilinearAffineOBJPresentationSample(
                            fSpriteIndex, coord + offsets[tap],
                            tapCoverage);
                    presentationCoverage += tapCoverage;
                    if (centerCoverage <= 0.0 &&
                        tapCoverage > bestCoverage)
                    {
                        enhancedColor = tapColor;
                        bestCoverage = tapCoverage;
                    }
                }
                presentationCoverage *= 0.25;
            }
            else if (uAffineOBJPresentationCoverageMode ==
                     AffineOBJCoverage_NativeAlpha)
            {
                // Source enhancement owns RGB only in the baseline
                // output-grid path. Native nearest alpha remains the DS
                // silhouette authority unless the user selects a
                // reconstructed contour or subpixel composition mode.
                enhancedColor = GetAffineOBJPresentationColor(
                    fSpriteIndex, coord);
                presentationCoverage = nativePresence;
            }
            else if (uAffineOBJPresentationCoverageMode ==
                         AffineOBJCoverage_ScalerContour ||
                     uAffineOBJPresentationCoverageMode ==
                         AffineOBJCoverage_Spline36Contour)
            {
                // The reconstructed field chooses a high-resolution binary
                // contour. Coverage is calculated by whichever output-grid
                // or localized-subpixel presentation policy invoked this
                // sample; do not treat the predictor's fractional value as
                // coverage or add a second hidden supersampling layer here.
                enhancedColor = GetAffineOBJPresentationColor(
                    fSpriteIndex, coord);
                presentationCoverage = GetAffineOBJPresentationCoverage(
                    fSpriteIndex, coord);
            }
            else if (uAffineOBJPresentationCoverageMode != AffineOBJCoverage_None)
            {
                float centerCoverage = GetAffineOBJPresentationCoverage(
                    fSpriteIndex, coord);
                enhancedColor = GetAffineOBJPresentationColor(
                    fSpriteIndex, coord);
                vec2 dx = dFdx(coord) * 0.25;
                vec2 dy = dFdy(coord) * 0.25;
                vec2 offsets[4] = vec2[4](
                    -dx - dy, dx - dy, -dx + dy, dx + dy);
                float bestCoverage = centerCoverage;
                presentationCoverage = 0.0;
                for (int tap = 0; tap < 4; tap++)
                {
                    float tapCoverage = GetAffineOBJPresentationCoverage(
                        fSpriteIndex, coord + offsets[tap]);
                    vec4 tapColor = GetAffineOBJPresentationColor(
                        fSpriteIndex, coord + offsets[tap]);
                    presentationCoverage += tapCoverage;
                    if (centerCoverage <= 0.0 &&
                        tapCoverage > bestCoverage)
                    {
                        enhancedColor = tapColor;
                        bestCoverage = tapCoverage;
                    }
                }
                presentationCoverage *= 0.25;
            }
            else
            {
                enhancedColor = GetEnhancedPresentationSample(
                    fSpriteIndex, coord, presentationCoverage);
            }
            // Keep the native owner even when reconstruction contracts its
            // contour. The normal-color union may contain an assembled lower
            // source here; dropping this owner would reject that union against
            // the unchanged semantic OBJ winner.
            if (presentationCoverage <= 0.0 && nativePresence <= 0.0)
                discard;
            col = vec4(enhancedColor.rgb, presentationCoverage);
            coverage = presentationCoverage;
        }
        else
        {
            col = GetSpritePixelCoverage(fSpriteIndex, coord, coverage);
            if (ShouldUseEnhancedSprite(fSpriteIndex))
            {
                float presentationCoverage = 0.0;
                vec4 enhancedColor = GetSpritePixelEnhanced(fSpriteIndex, coord,
                                                             presentationCoverage);
                if (enhancedColor.a > 0.0)
                {
                    col = enhancedColor;
                    coverage = presentationCoverage;
                }
            }
            else if (ShouldFilterSprite(fSpriteIndex))
            {
                vec4 filteredColor = GetSpritePixelSpline36(fSpriteIndex, coord);
                if (filteredColor.a > 0.0)
                    col = filteredColor;
            }
        }
    }

    if (col.a == 0 &&
        (!uXBRZPresentationOnly || nativePresence <= 0.0))
        discard;

    if (uOrderedPresentationBand)
    {
        // This mode is admitted only for normal, tiled, non-mosaic affine
        // OBJ. Preserve the reconstructed presentation contour as
        // premultiplied coverage so overlapping OAM entries can be composed
        // back-to-front before the completed scene is box-resolved.
        if (uOAM[fSpriteIndex].OBJMode != 0 ||
            uOAM[fSpriteIndex].Type >= 2 ||
            uOAM[fSpriteIndex].Mosaic ||
            uOAM[fSpriteIndex].Rotscale == -1)
            discard;
        float presentationAlpha = clamp(coverage, 0.0, 1.0);
        if (presentationAlpha <= 0.0)
            discard;
        oColor = vec4(col.rgb * presentationAlpha, presentationAlpha);
        oFlags = vec4(0.0);
        oCoverage = vec4(0.0);
        return;
    }

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
    float affineWinner = uOAM[fSpriteIndex].Rotscale != -1 ? 1.0 : 0.0;
    float spriteIndex = float(fSpriteIndex) / 255.0;
    oCoverage = vec4(coverage, affineWinner, spriteIndex,
                     uXBRZPresentationOnly ? nativePresence : 0.0);
}
