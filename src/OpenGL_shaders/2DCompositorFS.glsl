#version 140

uniform sampler2D BGLayerTex[4];
uniform sampler2D EnhancedBGLayerTex[4];
uniform sampler2D BGLayerMetaTex[4];
uniform sampler2D Direct3DCoverageTex;
uniform sampler2DArray OBJLayerTex;
uniform sampler2DArray XBRZPresentationOBJLayerTex;
uniform sampler2DArray Capture128Tex;
uniform sampler2DArray Capture256Tex;
uniform isampler2D MosaicTex;
uniform bool uPresentationContourPremultiplied;

struct sBGConfig
{
    ivec2 Size;
    int Type;
    int PalOffset;
    int TileOffset;
    int MapOffset;
    bool Clamp;
};

layout(std140) uniform ubBGConfig
{
    int uVRAMMask;
    sBGConfig uBGConfig[4];
};

struct sScanline
{
    ivec2 BGOffset[4];
    ivec4 BGRotscale[2];
    int BackColor;
    uint WinRegs;
    int WinMask;
    ivec4 WinPos;
    bvec4 BGMosaicEnable;
    ivec4 MosaicSize;
    ivec4 BGPrio;
    bool EnableOBJ;
    bool Enable3D;
    int BlendCnt;
    int BlendEffect;
    ivec3 BlendCoef;
};

layout(std140) uniform ubScanlineConfig
{
    sScanline uScanline[192];
};

layout(std140) uniform ubCompositorConfig
{
    ivec4 uBGPrio;
    bool uEnableOBJ;
    bool uEnable3D;
    int uBlendCnt;
    int uBlendEffect;
    ivec3 uBlendCoef;
};

uniform bool uUseScanlineCompositorState;
uniform bool uForceOBJDisabled;

ivec4 gBGPrio;
bool gEnableOBJ;
bool gEnable3D;
int gBlendCnt;
int gBlendEffect;
ivec3 gBlendCoef;

uniform int uScaleFactor;
uniform bool uOBJNativeResolution;
uniform int uLayerFilterMode;
uniform bool uLayerFilterNoWrap;
uniform bool uAffineSourceEnhancementOnly;
uniform int uEnhancedBGMask;
// 0 = normal output, 1 = source-path tint, 2 = semantic selector with
// presentation coverage blended between the selected owner and its underlay.
uniform int uDebugTintBySource;
uniform bool uSplit3DSemantics;
uniform bool uSharpenSplit3DCoverage;
uniform bool uUseEnhancedOBJPresentationCoverage;
uniform bool uUseXBRZPresentationContour;
uniform bool uBilinearAffineBGPresentation;
uniform int uExplicitAlphaReconstruction = -1;
uniform bool uExplicitLinearRGB;
uniform int uEnhancedBGCoverageMask;
uniform int uReconstructedBGCandidateMask;
uniform int uAffineConstantBackdropProofMask;
// Strict operand reconstruction probes the completed compositor with a
// geometry-independent Direct3D state. 0 keeps the bound texture, 1 removes
// Direct3D, 2 supplies opaque black, and 3 supplies opaque white.
uniform int uDirect3DEndpointMode;

smooth in vec4 fTexcoord;

out vec4 oColor;
out vec4 oPresentation;
out vec4 oPresentationUnderlay;
// Diagnostic MRTs are attached only while overlap debug views are active.
out vec4 oOverlapSemantic;
out vec4 oOverlapUnderlay;
out vec4 oOverlapDecisions;

int MosaicX = 0;

const int LayerFilter_Nearest = 0;
const int LayerFilter_Spline36 = 1;

const int DebugPath_Backdrop = 0;
const int DebugPath_FilteredBG = 1;
const int DebugPath_UnfilteredBG = 2;
const int DebugPath_CaptureBG = 3;
const int DebugPath_OBJ = 4;
const int DebugPath_Direct3D = 5;
const int DebugPath_FilteredOBJ = 6;

ivec2 OBJTexCoordFromNative(ivec2 coord)
{
    return uOBJNativeResolution ? coord : coord * uScaleFactor;
}

ivec2 OBJTexCoordFromOutput(ivec2 coord)
{
    return uOBJNativeResolution ? ivec2(fTexcoord.xy) : coord;
}

ivec3 ConvertColor(int col)
{
    ivec3 ret;
    ret.r = (col & 0x1F) << 1;
    ret.g = ((col & 0x3E0) >> 4) | (col >> 15);
    ret.b = (col & 0x7C00) >> 9;
    return ret;
}

vec4 BG0Fetch(vec2 coord)
{
    if (gEnable3D)
    {
        if (uDirect3DEndpointMode == 1)
            return vec4(0.0);
        if (uDirect3DEndpointMode == 2)
            return vec4(0.0, 0.0, 0.0, 1.0);
        if (uDirect3DEndpointMode == 3)
            return vec4(1.0);
    }
    return texture(BGLayerTex[0], coord);
}

vec4 BG1Fetch(vec2 coord)
{
    return texture(BGLayerTex[1], coord);
}

vec4 BG2Fetch(vec2 coord)
{
    return texture(BGLayerTex[2], coord);
}

vec4 BG3Fetch(vec2 coord)
{
    return texture(BGLayerTex[3], coord);
}

vec3 DebugTintColor(int sourceMask, int specialType)
{
    if (specialType == 1)
        return vec3(1.0, 0.0, 1.0);
    if (sourceMask == (1 << 0))
        return vec3(1.0, 0.35, 0.2);
    if (sourceMask == (1 << 1))
        return vec3(0.2, 1.0, 0.2);
    if (sourceMask == (1 << 2))
        return vec3(0.2, 0.7, 1.0);
    if (sourceMask == (1 << 3))
        return vec3(1.0, 0.75, 0.2);
    if (sourceMask == (1 << 4))
        return vec3(1.0, 1.0, 0.2);
    if (sourceMask == 0x20)
        return vec3(0.6, 0.6, 0.6);
    return vec3(1.0, 1.0, 1.0);
}

vec3 DebugPathTint(int sourceMask, int specialType, int sourcePath)
{
    vec3 tint = DebugTintColor(sourceMask, specialType);

    if (sourcePath == DebugPath_UnfilteredBG)
        return mix(tint, vec3(1.0, 0.0, 0.0), 0.65);
    if (sourcePath == DebugPath_CaptureBG)
        return vec3(0.0, 1.0, 1.0);
    if (sourcePath == DebugPath_OBJ)
        return vec3(1.0, 1.0, 0.2);
    if (sourcePath == DebugPath_FilteredOBJ)
        return vec3(0.6, 1.0, 0.0);
    if (sourcePath == DebugPath_Direct3D)
        return vec3(1.0, 0.0, 1.0);
    if (sourcePath == DebugPath_Backdrop)
        return vec3(0.6, 0.6, 0.6);

    return tint;
}

bool ShouldFilterBGLayer(int bg)
{
    if (uLayerFilterMode != LayerFilter_Spline36)
        return false;
    if (!uAffineSourceEnhancementOnly)
        return true;

    int type = uBGConfig[bg].Type;
    return type == 2 || type == 3 ||
           ((uEnhancedBGMask & (1 << bg)) != 0 &&
            (type == 0 || type == 1));
}

bool ShouldUseEnhancedBGLayer(int bg)
{
    int type = uBGConfig[bg].Type;
    return (uEnhancedBGMask & (1 << bg)) != 0 &&
           (type >= 0 && type <= 3) &&
           uLayerFilterMode != LayerFilter_Spline36;
}

bool UseEnhancedTextBGPresentation(int bg)
{
    int type = uBGConfig[bg].Type;
    return (uEnhancedBGCoverageMask & (1 << bg)) != 0 &&
           (ShouldUseEnhancedBGLayer(bg) || ShouldFilterBGLayer(bg)) &&
           (type == 0 || type == 1);
}

bool UseReconstructedBGCandidate(int bg)
{
    int type = uBGConfig[bg].Type;
    return (uReconstructedBGCandidateMask & (1 << bg)) != 0 &&
           ShouldUseEnhancedBGLayer(bg) &&
           (type == 0 || type == 1);
}

bool UseAnyEnhancedTextBGPresentation(int bg)
{
    return UseEnhancedTextBGPresentation(bg) ||
           UseReconstructedBGCandidate(bg);
}

bool UseBilinearAffineBGPresentation(int bg)
{
    int type = uBGConfig[bg].Type;
    int blendEffect = (gBlendCnt >> 6) & 0x3;
    int target1Mask = gBlendCnt & 0x3F;
    int target2Mask = (gBlendCnt >> 8) & 0x3F;
    // Target-2-only BLDCNT state cannot initiate an ordinary color effect.
    // Uniform target-2 membership also prevents forced 3D/OBJ alpha blending
    // from distinguishing reconstructed BG coverage from its underlay. Mario
    // Kart's affine course screen uses the all-target-2 form.
    bool target1Compatible = target1Mask == 0 ||
                             blendEffect == 2 || blendEffect == 3;
    bool completedTarget2Compatible =
        blendEffect == 1 &&
        (target2Mask & (1 << bg)) != 0 &&
        (uAffineConstantBackdropProofMask & (1 << bg)) != 0;
    bool target2Compatible = target2Mask == 0 ||
                             target2Mask == 0x3F ||
                             completedTarget2Compatible;
    return uBilinearAffineBGPresentation &&
           (target1Compatible || completedTarget2Compatible) &&
           target2Compatible &&
           (type == 2 || type == 3) &&
           (ShouldFilterBGLayer(bg) || ShouldUseEnhancedBGLayer(bg));
}

ivec4 ApplyBrightnessEffect(ivec4 color, int effect, int evy)
{
    if (effect == 2)
        return color + ((((0x3F - color) * evy) + 0x8) >> 4);
    if (effect == 3)
        return color - (((color * evy) + 0x7) >> 4);
    return color;
}

bool SupportsPresentationOBJMaterial(int specialType)
{
    if (specialType == 2)
        return true;
    if (specialType != 0)
        return false;

    // A normal OBJ only inherits BLDCNT's selected operation when OBJ is a
    // Target 1. Zero-strength brightness is also an identity operation. Do
    // not reject presentation coverage merely because the scanline selected
    // a nominal effect that cannot change this candidate.
    bool objIsTarget1 = (gBlendCnt & (1 << 4)) != 0;
    bool supportedBrightness =
        gBlendEffect == 2 || gBlendEffect == 3;
    return !objIsTarget1 || gBlendEffect == 0 || supportedBrightness;
}

ivec4 CompletePresentationOBJForeground(ivec4 foreground,
                                        int presentationSpecialType,
                                        ivec4 underlay,
                                        int underlayMask,
                                        bool effectsEnabled)
{
    if (presentationSpecialType == 2 &&
        (gBlendCnt & (underlayMask << 8)) != 0)
    {
        // Bitmap OBJ alpha is authored material. Resolve that equation before
        // presentation coverage; it replaces rather than combines with the
        // ordinary Target-1 operation.
        int materialAlpha = clamp(foreground.a, 0, 16);
        return min(((foreground * materialAlpha) +
                    (underlay * (16 - materialAlpha)) + 0x8) >> 4,
                   0x3F);
    }

    bool objIsTarget1 = (gBlendCnt & (1 << 4)) != 0;
    if (effectsEnabled && objIsTarget1 &&
        (gBlendEffect == 2 || gBlendEffect == 3))
    {
        return ApplyBrightnessEffect(foreground, gBlendEffect,
                                     gBlendCoef[2]);
    }
    return foreground;
}

ivec4 ResolveExposedWinner(ivec4 color, int colorMask, int specialType,
                           ivec4 underlay, int underlayMask,
                           bool effectsEnabled)
{
    int target2Mask = underlayMask << 8;
    if (specialType != 0 && (gBlendCnt & target2Mask) != 0)
    {
        if (specialType == 1)
        {
            // Direct3D material alpha uses five-bit coefficients.
            int eva = (color.a & 0x1F) + 1;
            int evb = 32 - eva;
            return min(((color * eva) + (underlay * evb) + 0x10) >> 5,
                       0x3F);
        }

        int eva = specialType == 2 ? gBlendCoef[0] : color.a;
        int evb = specialType == 2 ? gBlendCoef[1] : 16 - eva;
        return min(((color * eva) + (underlay * evb) + 0x8) >> 4,
                   0x3F);
    }

    if (!effectsEnabled || (gBlendCnt & colorMask) == 0)
        return color;

    if (gBlendEffect == 1)
    {
        if ((gBlendCnt & target2Mask) == 0)
            return color;
        return min(((color * gBlendCoef[0]) +
                    (underlay * gBlendCoef[1]) + 0x8) >> 4,
                   0x3F);
    }

    return ApplyBrightnessEffect(color, gBlendEffect, gBlendCoef[2]);
}

bool UseAffineConstantBackdropUnderlay(int bg)
{
    return UseBilinearAffineBGPresentation(bg) &&
           (uAffineConstantBackdropProofMask & (1 << bg)) != 0;
}

int BGDebugPath(int bg, bool direct3D, bool capture)
{
    if (direct3D)
        return DebugPath_Direct3D;
    if (capture)
        return DebugPath_CaptureBG;
    if (ShouldFilterBGLayer(bg) || ShouldUseEnhancedBGLayer(bg))
        return DebugPath_FilteredBG;
    return DebugPath_UnfilteredBG;
}

int LayerDebugPath(int bg)
{
    if (bg == 0)
        return BGDebugPath(0, gEnable3D, false);
    if (bg == 1)
        return BGDebugPath(1, false, false);
    if (bg == 2)
        return BGDebugPath(2, false, uBGConfig[2].Type >= 7);
    return BGDebugPath(3, false, uBGConfig[3].Type >= 7);
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

vec2 WrapLayerPosition(vec2 coord, ivec2 texSize)
{
    vec2 size = vec2(texSize);
    return coord - (floor(coord / size) * size);
}

ivec2 WrapLayerTexel(ivec2 coord, ivec2 texSize)
{
    return ivec2(WrapLayerPosition(vec2(coord), texSize));
}

vec4 FetchLayerTexelFiltered(sampler2D source, ivec2 coord, ivec2 texSize, bool clampLayer)
{
    if (clampLayer || uLayerFilterNoWrap)
    {
        if (any(lessThan(coord, ivec2(0))) || any(greaterThanEqual(coord, texSize)))
            return vec4(0.0);

        return texelFetch(source, coord, 0);
    }

    ivec2 wrappedCoord = WrapLayerTexel(coord, texSize);
    if (any(lessThan(wrappedCoord, ivec2(0))) || any(greaterThanEqual(wrappedCoord, texSize)))
        return vec4(0.0);

    return texelFetch(source, wrappedCoord, 0);
}

vec4 FetchLayerTexelForReconstruction(sampler2D source, ivec2 coord,
                                      ivec2 texSize, bool clampLayer,
                                      bool precomposeBackdrop,
                                      vec3 backdropColor)
{
    vec4 sampleColor = FetchLayerTexelFiltered(source, coord,
                                               texSize, clampLayer);
    if (!precomposeBackdrop)
        return sampleColor;

    return sampleColor.a > 0.0
        ? vec4(sampleColor.rgb, 1.0)
        : vec4(backdropColor, 1.0);
}

float FetchLayerBilinearCoverage(sampler2D source, vec2 bgpos,
                                 ivec2 texSize, bool clampLayer)
{
    vec2 filterPos = (!clampLayer && uLayerFilterNoWrap)
        ? WrapLayerPosition(bgpos, texSize) : bgpos;
    vec2 srcCoord = filterPos - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);

    float c00 = FetchLayerTexelFiltered(source, baseCoord,
                                        texSize, clampLayer).a;
    float c10 = FetchLayerTexelFiltered(source, baseCoord + ivec2(1, 0),
                                        texSize, clampLayer).a;
    float c01 = FetchLayerTexelFiltered(source, baseCoord + ivec2(0, 1),
                                        texSize, clampLayer).a;
    float c11 = FetchLayerTexelFiltered(source, baseCoord + ivec2(1, 1),
                                        texSize, clampLayer).a;
    return clamp(mix(mix(c00, c10, frac.x),
                     mix(c01, c11, frac.x), frac.y), 0.0, 1.0);
}

float FetchLayerDomainTexel(ivec2 coord, ivec2 texSize, bool clampLayer)
{
    if (!clampLayer && !uLayerFilterNoWrap)
        return 1.0;

    return any(lessThan(coord, ivec2(0))) ||
           any(greaterThanEqual(coord, texSize)) ? 0.0 : 1.0;
}

float FetchLayerBilinearDomainCoverage(vec2 bgpos, ivec2 texSize,
                                       bool clampLayer)
{
    vec2 filterPos = (!clampLayer && uLayerFilterNoWrap)
        ? WrapLayerPosition(bgpos, texSize) : bgpos;
    vec2 srcCoord = filterPos - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(srcCoord));
    vec2 frac = fract(srcCoord);

    float c00 = FetchLayerDomainTexel(baseCoord, texSize, clampLayer);
    float c10 = FetchLayerDomainTexel(baseCoord + ivec2(1, 0),
                                      texSize, clampLayer);
    float c01 = FetchLayerDomainTexel(baseCoord + ivec2(0, 1),
                                      texSize, clampLayer);
    float c11 = FetchLayerDomainTexel(baseCoord + ivec2(1, 1),
                                      texSize, clampLayer);
    return clamp(mix(mix(c00, c10, frac.x),
                     mix(c01, c11, frac.x), frac.y), 0.0, 1.0);
}

vec4 FetchLayerSpline36(sampler2D source, vec2 bgpos, ivec2 texSize,
                        bool clampLayer, bool presentationCoverage,
                        bool precomposeBackdrop, vec3 backdropColor)
{
    vec4 nearestColor = texture(source, bgpos / vec2(texSize));
    if (precomposeBackdrop && nearestColor.a <= 0.0)
        nearestColor = vec4(backdropColor, 1.0);
    float artCoverage = presentationCoverage && !precomposeBackdrop
        ? FetchLayerBilinearCoverage(source, bgpos, texSize, clampLayer)
        : nearestColor.a;
    float domainCoverage = precomposeBackdrop
        ? FetchLayerBilinearDomainCoverage(bgpos, texSize, clampLayer)
        : artCoverage;
    if (domainCoverage <= 0.0)
        return vec4(0.0);
    if (!precomposeBackdrop && artCoverage <= 0.0)
        return vec4(0.0);

    vec2 filterPos = (!clampLayer && uLayerFilterNoWrap) ? WrapLayerPosition(bgpos, texSize) : bgpos;
    vec2 srcCoord = filterPos - vec2(0.5);
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
            vec4 sampleColor = FetchLayerTexelForReconstruction(
                source, baseCoord + ivec2(x - 2, y - 2),
                texSize, clampLayer, precomposeBackdrop, backdropColor);
            if (sampleColor.a <= 0.0)
                continue;

            accum += sampleColor.rgb * weight;
            minColor = min(minColor, sampleColor.rgb);
            maxColor = max(maxColor, sampleColor.rgb);
            totalWeight += weight;
        }
    }

    vec3 color = totalWeight <= 0.00001
        ? nearestColor.rgb
        : clamp(accum / totalWeight, minColor, maxColor);
    color = clamp(color, 0.0, 1.0);
    return precomposeBackdrop
        ? vec4(color, domainCoverage)
        : vec4(color, artCoverage);
}

ivec2 ResolveEnhancedLayerTexel(ivec2 coord, ivec2 texSize,
                                bool clampLayer)
{
    if (!clampLayer && !uLayerFilterNoWrap)
        return WrapLayerTexel(coord, texSize);

    return clamp(coord, ivec2(0), texSize - ivec2(1));
}

vec4 FetchEnhancedLayerBilinear(sampler2D source, vec2 normalizedCoord,
                                bool clampLayer)
{
    ivec2 enhancedSize = textureSize(source, 0);
    vec2 texelPos = normalizedCoord * vec2(enhancedSize) - vec2(0.5);
    ivec2 baseCoord = ivec2(floor(texelPos));
    vec2 frac = fract(texelPos);

    vec4 c00 = texelFetch(source, ResolveEnhancedLayerTexel(
        baseCoord, enhancedSize, clampLayer), 0);
    vec4 c10 = texelFetch(source, ResolveEnhancedLayerTexel(
        baseCoord + ivec2(1, 0), enhancedSize, clampLayer), 0);
    vec4 c01 = texelFetch(source, ResolveEnhancedLayerTexel(
        baseCoord + ivec2(0, 1), enhancedSize, clampLayer), 0);
    vec4 c11 = texelFetch(source, ResolveEnhancedLayerTexel(
        baseCoord + ivec2(1, 1), enhancedSize, clampLayer), 0);

    // Clamping RGB gives the filter valid edge color, but must not extend
    // coverage outside a non-repeating DS background's finite rectangle.
    if (clampLayer)
    {
        c00.a *= FetchLayerDomainTexel(baseCoord, enhancedSize, true);
        c10.a *= FetchLayerDomainTexel(baseCoord + ivec2(1, 0), enhancedSize, true);
        c01.a *= FetchLayerDomainTexel(baseCoord + ivec2(0, 1), enhancedSize, true);
        c11.a *= FetchLayerDomainTexel(baseCoord + ivec2(1, 1), enhancedSize, true);
    }

    return mix(mix(c00, c10, frac.x),
               mix(c01, c11, frac.x), frac.y);
}

float ConstrainReconstructedCoverage(sampler2D source, vec2 bgpos,
                                     ivec2 texSize, bool clampLayer,
                                     float reconstructedCoverage)
{
    // Neural alpha can contain very small positive/negative ringing well away
    // from the authored contour.  Preserve its subpixel decision only where
    // the immediate native footprint actually straddles a coverage boundary:
    // unanimous transparent samples prove absence, and unanimous opaque
    // samples prove full coverage.  This is a local support/plateau contract,
    // not a magnitude threshold, so low but legitimate boundary coverage is
    // retained while distant temporal specks cannot become layer candidates.
    vec2 filterPos = (!clampLayer && uLayerFilterNoWrap)
        ? WrapLayerPosition(bgpos, texSize) : bgpos;
    ivec2 baseCoord = ivec2(floor(filterPos - vec2(0.5)));
    float c00 = FetchLayerTexelFiltered(source, baseCoord,
                                        texSize, clampLayer).a;
    float c10 = FetchLayerTexelFiltered(source, baseCoord + ivec2(1, 0),
                                        texSize, clampLayer).a;
    float c01 = FetchLayerTexelFiltered(source, baseCoord + ivec2(0, 1),
                                        texSize, clampLayer).a;
    float c11 = FetchLayerTexelFiltered(source, baseCoord + ivec2(1, 1),
                                        texSize, clampLayer).a;
    float minimumCoverage = min(min(c00, c10), min(c01, c11));
    float maximumCoverage = max(max(c00, c10), max(c01, c11));
    return clamp(reconstructedCoverage, minimumCoverage, maximumCoverage);
}

vec4 FetchLayerEnhanced(sampler2D source, sampler2D enhancedSource,
                         vec2 bgpos, ivec2 texSize, bool clampLayer,
                         bool presentationCoverage,
                         bool enhancedCoverage,
                         bool constrainEnhancedCoverage,
                         bool precomposeBackdrop, vec3 backdropColor)
{
    vec2 normalizedCoord = bgpos / vec2(texSize);
    vec4 nearestColor = texture(source, normalizedCoord);
    // A proven constant-backdrop product is already an opaque reconstruction
    // of the visible affine source.  Its boundary is therefore an RGB edge,
    // not an alpha edge.  When the bilinear presentation experiment is
    // requested, filter that completed RGB during affine placement instead of
    // applying native coverage a second time over its baked-in backdrop.
    vec4 enhancedColor = presentationCoverage && precomposeBackdrop
        ? FetchEnhancedLayerBilinear(enhancedSource, normalizedCoord,
                                     clampLayer)
        : texture(enhancedSource, normalizedCoord);
    if (uExplicitAlphaReconstruction >= 0)
    {
        // Map wrapping and filter-tap wrapping are separate. The native
        // sampler repeats the sample center for scrolling/affine maps even
        // when the reconstruction filter must not cross the sheet boundary.
        // Normalize before resolving taps, or out-of-range map positions
        // clamp to a single edge row/column and stretch it across the screen.
        if (!clampLayer)
            normalizedCoord = fract(normalizedCoord);
        vec4 linearColor = FetchEnhancedLayerBilinear(enhancedSource, normalizedCoord, clampLayer);
        vec4 pointColor = texelFetch(enhancedSource, ResolveEnhancedLayerTexel(
            ivec2(floor(normalizedCoord * vec2(textureSize(enhancedSource, 0)))),
            textureSize(enhancedSource, 0), clampLayer), 0);
        if (clampLayer)
            pointColor.a *= FetchLayerDomainTexel(
                ivec2(floor(normalizedCoord * vec2(textureSize(enhancedSource, 0)))),
                textureSize(enhancedSource, 0), true);
        enhancedColor.rgb = uExplicitLinearRGB ? linearColor.rgb : pointColor.rgb;
        enhancedColor.a = (uExplicitAlphaReconstruction == 0 || uExplicitAlphaReconstruction == 4)
            ? pointColor.a : linearColor.a;
        enhancedCoverage = enhancedCoverage || presentationCoverage;
    }
    float reconstructedCoverage = constrainEnhancedCoverage
        ? ConstrainReconstructedCoverage(source, bgpos, texSize, clampLayer,
                                         enhancedColor.a)
        : enhancedColor.a;
    float artCoverage = enhancedCoverage
        ? reconstructedCoverage
        : (presentationCoverage && !precomposeBackdrop
            ? FetchLayerBilinearCoverage(source, bgpos, texSize, clampLayer)
            : nearestColor.a);
    float domainCoverage = precomposeBackdrop
        ? FetchLayerBilinearDomainCoverage(bgpos, texSize, clampLayer)
        : artCoverage;
    if (domainCoverage <= 0.0)
        return vec4(0.0);

    return precomposeBackdrop
        ? vec4(enhancedColor.rgb, domainCoverage)
        : vec4(enhancedColor.rgb, artCoverage);
}

vec4 BG0FetchFiltered(vec2 bgpos)
{
    if (ShouldUseEnhancedBGLayer(0) && !gEnable3D)
        return FetchLayerEnhanced(BGLayerTex[0], EnhancedBGLayerTex[0],
                                  bgpos, uBGConfig[0].Size,
                                  uBGConfig[0].Clamp, false,
                                  UseAnyEnhancedTextBGPresentation(0),
                                  UseReconstructedBGCandidate(0),
                                  false, vec3(0.0));
    if (ShouldFilterBGLayer(0) && !gEnable3D)
        return FetchLayerSpline36(BGLayerTex[0], bgpos, uBGConfig[0].Size,
                                  uBGConfig[0].Clamp,
                                  UseEnhancedTextBGPresentation(0),
                                  false, vec3(0.0));

    return BG0Fetch(bgpos / vec2(uBGConfig[0].Size));
}

vec4 BG1FetchFiltered(vec2 bgpos)
{
    if (ShouldUseEnhancedBGLayer(1))
        return FetchLayerEnhanced(BGLayerTex[1], EnhancedBGLayerTex[1],
                                  bgpos, uBGConfig[1].Size,
                                  uBGConfig[1].Clamp, false,
                                  UseAnyEnhancedTextBGPresentation(1),
                                  UseReconstructedBGCandidate(1),
                                  false, vec3(0.0));
    if (ShouldFilterBGLayer(1))
        return FetchLayerSpline36(BGLayerTex[1], bgpos, uBGConfig[1].Size,
                                  uBGConfig[1].Clamp,
                                  UseEnhancedTextBGPresentation(1),
                                  false, vec3(0.0));

    return BG1Fetch(bgpos / vec2(uBGConfig[1].Size));
}

vec4 BG2FetchFiltered(vec2 bgpos, int line)
{
    bool reconstructedCoverage = UseReconstructedBGCandidate(2);
    bool presentationCoverage = UseBilinearAffineBGPresentation(2) ||
                                UseEnhancedTextBGPresentation(2) ||
                                reconstructedCoverage;
    bool precomposeBackdrop = UseAffineConstantBackdropUnderlay(2);
    vec3 backdropColor = vec3(ConvertColor(uScanline[line].BackColor)) / 63.0;
    if (ShouldUseEnhancedBGLayer(2))
        return FetchLayerEnhanced(BGLayerTex[2], EnhancedBGLayerTex[2],
                                  bgpos, uBGConfig[2].Size,
                                  uBGConfig[2].Clamp,
                                  presentationCoverage,
                                  reconstructedCoverage,
                                  reconstructedCoverage,
                                  precomposeBackdrop, backdropColor);
    if (ShouldFilterBGLayer(2))
        return FetchLayerSpline36(BGLayerTex[2], bgpos, uBGConfig[2].Size,
                                  uBGConfig[2].Clamp,
                                  presentationCoverage,
                                  precomposeBackdrop, backdropColor);

    return BG2Fetch(bgpos / vec2(uBGConfig[2].Size));
}

vec4 BG3FetchFiltered(vec2 bgpos, int line)
{
    bool reconstructedCoverage = UseReconstructedBGCandidate(3);
    bool presentationCoverage = UseBilinearAffineBGPresentation(3) ||
                                UseEnhancedTextBGPresentation(3) ||
                                reconstructedCoverage;
    bool precomposeBackdrop = UseAffineConstantBackdropUnderlay(3);
    vec3 backdropColor = vec3(ConvertColor(uScanline[line].BackColor)) / 63.0;
    if (ShouldUseEnhancedBGLayer(3))
        return FetchLayerEnhanced(BGLayerTex[3], EnhancedBGLayerTex[3],
                                  bgpos, uBGConfig[3].Size,
                                  uBGConfig[3].Clamp,
                                  presentationCoverage,
                                  reconstructedCoverage,
                                  reconstructedCoverage,
                                  precomposeBackdrop, backdropColor);
    if (ShouldFilterBGLayer(3))
        return FetchLayerSpline36(BGLayerTex[3], bgpos, uBGConfig[3].Size,
                                  uBGConfig[3].Clamp,
                                  presentationCoverage,
                                  precomposeBackdrop, backdropColor);

    return BG3Fetch(bgpos / vec2(uBGConfig[3].Size));
}

vec2 BG0CalcPos(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[0];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[0])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    return bgpos;
}

float BG0CoverageFetchFiltered(vec2 bgpos)
{
    if (gEnable3D)
    {
        if (uDirect3DEndpointMode == 1)
            return 0.0;
        if (uDirect3DEndpointMode == 2 || uDirect3DEndpointMode == 3)
            return 1.0;
    }
    if (!uSplit3DSemantics || !gEnable3D)
        return clamp(BG0FetchFiltered(bgpos).a, 0.0, 1.0);

    return clamp(texture(Direct3DCoverageTex, bgpos / vec2(uBGConfig[0].Size)).a, 0.0, 1.0);
}

float Split3DCoverageWeight(float coverage)
{
    coverage = clamp(coverage, 0.0, 1.0);
    if (!uSharpenSplit3DCoverage)
        return coverage;

    return smoothstep(0.20, 0.50, coverage);
}

vec4 BG0CalcAndFetch(vec2 coord, int line)
{
    vec2 bgpos = BG0CalcPos(coord, line);
    return BG0FetchFiltered(bgpos);
}

float BG0CalcAndFetchCoverage(vec2 coord, int line)
{
    vec2 bgpos = BG0CalcPos(coord, line);
    return BG0CoverageFetchFiltered(bgpos);
}

vec4 BG1CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[1];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[1])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    return BG1FetchFiltered(bgpos);
}

vec4 BG2CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[2];
    vec2 bgpos;
    if (uBGConfig[2].Type >= 2)
    {
        // rotscale BG
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[0]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        // text-mode BG
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[2])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    if (uBGConfig[2].Type >= 7)
    {
        // hi-res capture
        bgpos.y += uBGConfig[2].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[2].Size), uBGConfig[2].TileOffset);

        // due to the possible weirdness of display capture buffers,
        // we need to do custom wraparound handling
        if (uBGConfig[2].Clamp)
        {
            if (any(lessThan(capcoord.xy, vec2(0))) || any(greaterThanEqual(capcoord.xy, vec2(1))))
                return vec4(0);
        }

        if (uBGConfig[2].Type == 7)
            return texture(Capture128Tex, capcoord);
        else
            return texture(Capture256Tex, capcoord);
    }

    return BG2FetchFiltered(bgpos, line);
}

vec4 BG3CalcAndFetch(vec2 coord, int line)
{
    ivec2 bgoffset = uScanline[line].BGOffset[3];
    vec2 bgpos;
    if (uBGConfig[3].Type >= 2)
    {
        // rotscale BG
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[1]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        // text-mode BG
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[3])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    if (uBGConfig[3].Type >= 7)
    {
        // hi-res capture
        bgpos.y += uBGConfig[3].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[3].Size), uBGConfig[3].TileOffset);

        // due to the possible weirdness of display capture buffers,
        // we need to do custom wraparound handling
        if (uBGConfig[3].Clamp)
        {
            if (any(lessThan(capcoord.xy, vec2(0))) || any(greaterThanEqual(capcoord.xy, vec2(1))))
                return vec4(0);
        }

        if (uBGConfig[3].Type == 7)
            return texture(Capture128Tex, capcoord);
        else
            return texture(Capture256Tex, capcoord);
    }

    return BG3FetchFiltered(bgpos, line);
}

void CalcSpriteMosaic(in ivec2 coord, out ivec4 objflags, out vec4 objcolor)
{
    objflags = ivec4(0);
    objcolor = vec4(0);

    for (int i = 0; i < 16; i++)
    {
        ivec2 curpos = ivec2(coord.x - 15 + i, coord.y);

        if (curpos.x < 0)
        {
            objflags = ivec4(0);
            objcolor = vec4(0);
        }
        else
        {
            int mosx = texelFetch(MosaicTex, ivec2(curpos.x, uScanline[curpos.y].MosaicSize.z), 0).r;
            ivec2 objcoord = OBJTexCoordFromNative(curpos);
            vec4 color = texelFetch(OBJLayerTex, ivec3(objcoord, 0), 0);
            ivec4 flags = ivec4(texelFetch(OBJLayerTex, ivec3(objcoord, 1), 0) * 255.0);

            bool latch = false;
            if (mosx == 0)
                latch = true;
            else if (flags.g == 0)
                latch = true;
            else if (objflags.g == 0)
                latch = true;
            else if (flags.a < objflags.a)
                latch = true;

            if (latch)
            {
                objflags = flags;
                objcolor = color;
            }
        }
    }
}

vec4 CompositeLayers()
{
    oPresentation = vec4(0.0);
    oPresentationUnderlay = vec4(0.0);
    oOverlapSemantic = vec4(0.0);
    oOverlapUnderlay = vec4(0.0);
    oOverlapDecisions = vec4(0.0);

    ivec2 coord = ivec2(fTexcoord.zw);
    vec2 bgcoord = vec2(fTexcoord.x, fract(fTexcoord.y));
    int xpos = int(fTexcoord.x);
    int line = int(fTexcoord.y);

    if (uUseScanlineCompositorState)
    {
        gBGPrio = uScanline[line].BGPrio;
        gEnableOBJ = uScanline[line].EnableOBJ && !uForceOBJDisabled;
        gEnable3D = uScanline[line].Enable3D;
        gBlendCnt = uScanline[line].BlendCnt;
        gBlendEffect = uScanline[line].BlendEffect;
        gBlendCoef = uScanline[line].BlendCoef;
    }
    else
    {
        gBGPrio = uBGPrio;
        gEnableOBJ = uEnableOBJ && !uForceOBJDisabled;
        gEnable3D = uEnable3D;
        gBlendCnt = uBlendCnt;
        gBlendEffect = uBlendEffect;
        gBlendCoef = uBlendCoef;
    }

    if (uScanline[line].MosaicSize.x > 0)
        MosaicX = texelFetch(MosaicTex, ivec2(bgcoord.x, uScanline[line].MosaicSize.x), 0).r;

    ivec4 col1 = ivec4(ConvertColor(uScanline[line].BackColor), 0x20);
    int mask1 = 0x20;
    int debugPath1 = DebugPath_Backdrop;
    ivec4 col2 = ivec4(0);
    int mask2 = 0;
    ivec4 col3 = ivec4(0);
    int mask3 = 0;
    float visualCoverage1 = 1.0;
    float visualCoverage2 = 0.0;
    bool specialcase = false;
    int specialType = 0;
    int specialType2 = 0;

    vec4 layercol[6];
    layercol[0] = BG0CalcAndFetch(bgcoord, line);
    layercol[1] = BG1CalcAndFetch(bgcoord, line);
    layercol[2] = BG2CalcAndFetch(bgcoord, line);
    layercol[3] = BG3CalcAndFetch(bgcoord, line);

    float direct3DVisualCoverage = BG0CalcAndFetchCoverage(bgcoord, line);

    ivec4 objflags;
    vec4 objcoverage = vec4(0.0);
    vec4 xbrzPresentationColor = vec4(0.0);
    vec4 xbrzPresentationFlags = vec4(0.0);
    vec4 xbrzPresentationCoverage = vec4(0.0);
    bool presentationContour2x = false;
    if (uScanline[line].MosaicSize.z > 0)
    {
        CalcSpriteMosaic(ivec2(fTexcoord.xy), objflags, layercol[4]);
    }
    else
    {
        ivec2 objcoord = OBJTexCoordFromOutput(coord);
        layercol[4] = texelFetch(OBJLayerTex, ivec3(objcoord, 0), 0);
        layercol[5] = texelFetch(OBJLayerTex, ivec3(objcoord, 1), 0);
        objcoverage = texelFetch(OBJLayerTex, ivec3(objcoord, 2), 0);
        objflags = ivec4(layercol[5] * 255.0);
        if (uUseXBRZPresentationContour)
        {
            presentationContour2x = all(equal(
                textureSize(XBRZPresentationOBJLayerTex, 0).xy,
                textureSize(OBJLayerTex, 0).xy * 2));
            if (!presentationContour2x)
            {
                xbrzPresentationColor = texelFetch(
                    XBRZPresentationOBJLayerTex, ivec3(objcoord, 0), 0);
                xbrzPresentationFlags = texelFetch(
                    XBRZPresentationOBJLayerTex, ivec3(objcoord, 1), 0);
                xbrzPresentationCoverage = texelFetch(
                    XBRZPresentationOBJLayerTex, ivec3(objcoord, 2), 0);
            }
        }
    }

    int winmask = uScanline[line].WinMask;
    bool inside_win0, inside_win1;

    if (xpos < uScanline[line].WinPos[0])
        inside_win0 = ((winmask & (1<<0)) != 0);
    else if (xpos < uScanline[line].WinPos[1])
        inside_win0 = ((winmask & (1<<1)) != 0);
    else
        inside_win0 = ((winmask & (1<<2)) != 0);

    if (xpos < uScanline[line].WinPos[2])
        inside_win1 = ((winmask & (1<<3)) != 0);
    else if (xpos < uScanline[line].WinPos[3])
        inside_win1 = ((winmask & (1<<4)) != 0);
    else
        inside_win1 = ((winmask & (1<<5)) != 0);

    uint winregs = uScanline[line].WinRegs;
    uint winsel = winregs;
    if (objflags.b > 0)
        winsel = winregs >> 8;
    if (inside_win1)
        winsel = winregs >> 16;
    if (inside_win0)
        winsel = winregs >> 24;

    for (int prio = 3; prio >= 0; prio--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if ((gBGPrio[bg] == prio) && (layercol[bg].a > 0) && ((winsel & (1u << bg)) != 0u))
            {
                col3 = col2;
                mask3 = mask2 >> 8;
                col2 = col1;
                mask2 = mask1 << 8;
                visualCoverage2 = visualCoverage1;
                specialType2 = specialType;
                col1 = ivec4(layercol[bg] * 255.0) >> ivec4(2,2,2,3);
                mask1 = (1 << bg);
                visualCoverage1 = ((bg == 0) && gEnable3D)
                    ? direct3DVisualCoverage
                    : (UseBilinearAffineBGPresentation(bg) ||
                       UseAnyEnhancedTextBGPresentation(bg)
                        ? clamp(layercol[bg].a, 0.0, 1.0) : 1.0);
                debugPath1 = LayerDebugPath(bg);
                specialcase = (bg == 0) && gEnable3D;
                specialType = specialcase ? 1 : 0;
            }
        }

        if (gEnableOBJ && (objflags.a == prio) && (layercol[4].a > 0) && ((winsel & (1u << 4)) != 0u))
        {
            col3 = col2;
            mask3 = mask2 >> 8;
            col2 = col1;
            mask2 = mask1 << 8;
            visualCoverage2 = visualCoverage1;
            specialType2 = specialType;
            col1 = ivec4(layercol[4] * 255.0) >> ivec4(2,2,2,3);
            mask1 = (1 << 4);
            visualCoverage1 =
                uUseEnhancedOBJPresentationCoverage &&
                    (objcoverage.g > 0.5 || objcoverage.a > 0.5)
                    ? clamp(objcoverage.r, 0.0, 1.0)
                    : 1.0;
            debugPath1 = uLayerFilterMode == LayerFilter_Spline36 &&
                         !uAffineSourceEnhancementOnly
                ? DebugPath_FilteredOBJ
                : DebugPath_OBJ;
            specialcase = (objflags.r != 0);
            specialType = objflags.r == 0 ? 0 : (objflags.r == 1 ? 2 : 3);
        }
    }

    int effect = 0;
    int eva, evb, evy = gBlendCoef[2];
    ivec4 preEffectCol1 = col1;
    ivec4 preEffectCol2 = col2;
    ivec4 preEffectCol3 = col3;

    if (specialcase && (gBlendCnt & mask2) != 0)
    {
        if (mask1 == (1<<0))
        {
            // 3D layer blending
            effect = 4;
            eva = (col1.a & 0x1F) + 1;
            evb = 32 - eva;
        }
        else if (objflags.r == 1)
        {
            // semi-transparent sprite
            effect = 1;
            eva = gBlendCoef[0];
            evb = gBlendCoef[1];
        }
        else //if (objflags.r == 2)
        {
            // bitmap sprite
            effect = 1;
            eva = col1.a;
            evb = 16 - eva;
        }
    }
    else if (((gBlendCnt & mask1) != 0) && ((winsel & (1u << 5)) != 0u))
    {
        effect = gBlendEffect;
        if (effect == 1)
        {
            if ((gBlendCnt & mask2) != 0)
            {
                eva = gBlendCoef[0];
                evb = gBlendCoef[1];
            }
            else
                effect = 0;
        }
    }

    if (effect == 1)
    {
        // blending
        col1 = ((col1 * eva) + (col2 * evb) + 0x8) >> 4;
        col1 = min(col1, 0x3F);
    }
    else if (effect == 2)
    {
        // brightness up
        col1 = ApplyBrightnessEffect(col1, effect, evy);
    }
    else if (effect == 3)
    {
        // brightness down
        col1 = ApplyBrightnessEffect(col1, effect, evy);
    }
    else if (effect == 4)
    {
        // 3D layer blending
        col1 = ((col1 * eva) + (col2 * evb) + 0x10) >> 5;
    }

    if (uSplit3DSemantics)
    {
        bool direct3DTopBrightnessEffect = mask1 == (1 << 0) && (effect == 2 || effect == 3);
        if (mask1 == (1 << 0) && !direct3DTopBrightnessEffect)
        {
            vec4 mixed = mix(vec4(preEffectCol2), vec4(col1), Split3DCoverageWeight(visualCoverage1));
            col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
        }
        else if (mask2 == (1 << 8) && effect != 0)
        {
            vec4 mixed = mix(vec4(preEffectCol1), vec4(col1), Split3DCoverageWeight(visualCoverage2));
            col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
        }
    }

    oOverlapSemantic = vec4(vec3(col1.rgb << 2) / 255.0, 1.0);
    // The semantic winner still carries straight RGB here. Complete an
    // ordinary OBJ's reconstructed coverage before an affine candidate uses
    // it as a visual base. Otherwise even a nearly transparent affine tap
    // can suppress the ordinary coverage below and expose opaque fringe RGB.
    // Keep preEffectCol1/2 and identity unchanged for DS material equations.
    bool ordinaryOBJCoverageCompleted =
        uUseEnhancedOBJPresentationCoverage && mask1 == (1 << 4) &&
        objcoverage.g <= 0.5 && objcoverage.a > 0.5;
    if (ordinaryOBJCoverageCompleted)
    {
        ivec4 presentationUnderlay = ResolveExposedWinner(
            preEffectCol2, mask2 >> 8, specialType2,
            preEffectCol3, mask3, (winsel & (1u << 5)) != 0u);
        oPresentation = vec4(vec3(col1.rgb << 2) / 255.0,
                             clamp(visualCoverage1, 0.0, 1.0));
        oPresentationUnderlay = vec4(
            vec3(presentationUnderlay.rgb << 2) / 255.0, 1.0);
        vec4 mixed = mix(vec4(presentationUnderlay), vec4(col1),
                         clamp(visualCoverage1, 0.0, 1.0));
        col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
    }

    bool usedXBRZPresentationContour = false;
    if (uUseXBRZPresentationContour && presentationContour2x &&
        (winsel & (1u << 4)) != 0u)
    {
        ivec2 baseSubpixel = coord * 2;
        ivec2 offsets[4] = ivec2[4](
            ivec2(0, 0), ivec2(1, 0),
            ivec2(0, 1), ivec2(1, 1));
        // Missing coverage can remove a semantic winner only when this
        // presentation represents its material. Normal-only premultiplied
        // bands omit special OBJ, so an absent candidate must preserve their
        // completed DS result rather than expose the background below them.
        bool replaceableSemanticAffineTop =
            mask1 == (1 << 4) && objcoverage.g > 0.5 &&
            SupportsPresentationOBJMaterial(objflags.r) &&
            (!uPresentationContourPremultiplied || objflags.r == 0);
        bool presentationEffectsEnabled =
            (winsel & (1u << 5)) != 0u;
        ivec4 completedSemanticUnderlay = ResolveExposedWinner(
            preEffectCol2,
            mask2 >> 8,
            specialType2,
            preEffectCol3,
            mask3,
            presentationEffectsEnabled);
        oOverlapUnderlay = vec4(vec3(completedSemanticUnderlay.rgb << 2) / 255.0, 1.0);
        vec4 noCandidateColor = replaceableSemanticAffineTop
            ? vec4(completedSemanticUnderlay) : vec4(col1);
        vec4 resolvedSum = vec4(0.0);
        bool anyAdmittedCandidate = false;
        bool unsupportedCandidateMaterial = false;

        for (int tap = 0; tap < 4; tap++)
        {
            ivec2 subpixel = baseSubpixel + offsets[tap];
            vec4 presentationColor = texelFetch(
                XBRZPresentationOBJLayerTex, ivec3(subpixel, 0), 0);
            vec4 presentationFlags = texelFetch(
                XBRZPresentationOBJLayerTex, ivec3(subpixel, 1), 0);
            vec4 presentationCoverage = texelFetch(
                XBRZPresentationOBJLayerTex, ivec3(subpixel, 2), 0);
            int presentationSpecialType =
                int(presentationFlags.r * 255.0 + 0.5);
            bool supportedMaterial =
                SupportsPresentationOBJMaterial(presentationSpecialType) &&
                (!uPresentationContourPremultiplied ||
                 presentationSpecialType == 0);
            if (presentationCoverage.g <= 0.5)
            {
                oOverlapDecisions[tap] = (replaceableSemanticAffineTop ? 1.0 : 2.0) / 255.0;
                resolvedSum += noCandidateColor;
                continue;
            }
            if (!supportedMaterial)
            {
                // This is not a geometric hole in the reconstructed
                // contour. The candidate exists, but this path cannot yet
                // reproduce its material/effect equation. Abort the whole
                // presentation replacement and retain the already-resolved
                // semantic winner instead of mistaking rejection for
                // transparency into the underlay.
                oOverlapDecisions[tap] = 3.0 / 255.0;
                unsupportedCandidateMaterial = true;
                break;
            }
            if (presentationColor.a <= 0.0)
            {
                oOverlapDecisions[tap] = 4.0 / 255.0;
                resolvedSum += noCandidateColor;
                continue;
            }

            int candidatePriority =
                int(presentationFlags.a * 255.0 + 0.5);
            int candidateIndex =
                int(presentationCoverage.b * 255.0 + 0.5);
            int candidateTotalPriority =
                candidatePriority * 128 + candidateIndex;

            bool blockedBySemanticOBJ = false;
            bool sameSemanticAffineOBJ = false;
            if (layercol[4].a > 0.0)
            {
                int semanticPriority = objflags.a;
                int semanticIndex =
                    int(objcoverage.b * 255.0 + 0.5);
                int semanticTotalPriority =
                    semanticPriority * 128 + semanticIndex;
                blockedBySemanticOBJ =
                    candidateTotalPriority > semanticTotalPriority;
                sameSemanticAffineOBJ = objcoverage.g > 0.5 &&
                    candidateTotalPriority == semanticTotalPriority;
            }

            bool candidateWins = !blockedBySemanticOBJ;
            if (candidateWins && mask1 == (1 << 4))
            {
                candidateWins = sameSemanticAffineOBJ ||
                    layercol[4].a <= 0.0 ||
                    candidateTotalPriority <= objflags.a * 128 +
                        int(objcoverage.b * 255.0 + 0.5);
            }
            else if (candidateWins && (mask1 & 0xF) != 0)
            {
                int topBG = mask1 == (1 << 0) ? 0 :
                            mask1 == (1 << 1) ? 1 :
                            mask1 == (1 << 2) ? 2 : 3;
                candidateWins = candidatePriority <= gBGPrio[topBG];
            }

            if (!candidateWins)
            {
                oOverlapDecisions[tap] = (blockedBySemanticOBJ ? 5.0 : 6.0) / 255.0;
                resolvedSum += vec4(col1);
                continue;
            }

            oOverlapDecisions[tap] =
                (sameSemanticAffineOBJ && mask1 == (1 << 4) ? 7.0 : 8.0) / 255.0;
            float presentationWeight = clamp(
                uPresentationContourPremultiplied
                    ? presentationColor.a
                    : presentationCoverage.r,
                0.0, 1.0);
            vec3 presentationRGB = presentationColor.rgb;
            if (uPresentationContourPremultiplied)
            {
                presentationRGB /= max(presentationWeight, 0.00001);
            }
            ivec4 presentationForeground = ivec4(
                vec4(clamp(presentationRGB, 0.0, 1.0), 1.0) * 255.0) >>
                ivec4(2, 2, 2, 3);
            ivec4 presentationMaterialUnderlay =
                sameSemanticAffineOBJ && mask1 == (1 << 4)
                    ? preEffectCol2 : preEffectCol1;
            ivec4 presentationUnderlay =
                sameSemanticAffineOBJ && mask1 == (1 << 4)
                    ? completedSemanticUnderlay : col1;
            int presentationUnderlayMask =
                sameSemanticAffineOBJ && mask1 == (1 << 4)
                    ? (mask2 >> 8) : mask1;
            presentationForeground = CompletePresentationOBJForeground(
                presentationForeground,
                presentationSpecialType,
                presentationMaterialUnderlay,
                presentationUnderlayMask,
                presentationEffectsEnabled);
            vec4 mixed = mix(vec4(presentationUnderlay),
                             vec4(presentationForeground),
                             presentationWeight);
            resolvedSum += vec4(ivec4(clamp(
                mixed + vec4(0.5), vec4(0.0), vec4(63.0))));
            anyAdmittedCandidate = true;
        }

        if (!unsupportedCandidateMaterial &&
            (anyAdmittedCandidate || replaceableSemanticAffineTop))
        {
            col1 = ivec4(clamp(resolvedSum * 0.25 + vec4(0.5),
                               vec4(0.0), vec4(63.0)));
            usedXBRZPresentationContour = true;
        }
    }
    else if (uUseXBRZPresentationContour &&
        xbrzPresentationColor.a > 0.0 &&
        xbrzPresentationCoverage.g > 0.5 &&
        (winsel & (1u << 4)) != 0u)
    {
        int presentationSpecialType =
            int(xbrzPresentationFlags.r * 255.0 + 0.5);
        bool supportedMaterial =
            SupportsPresentationOBJMaterial(presentationSpecialType) &&
            (!uPresentationContourPremultiplied ||
             presentationSpecialType == 0);
        int candidatePriority = int(xbrzPresentationFlags.a * 255.0 + 0.5);
        int candidateIndex = int(xbrzPresentationCoverage.b * 255.0 + 0.5);
        int candidateTotalPriority = candidatePriority * 128 + candidateIndex;

        bool blockedBySemanticOBJ = false;
        bool sameSemanticAffineOBJ = false;
        if (layercol[4].a > 0.0)
        {
            int semanticPriority = objflags.a;
            int semanticIndex = int(objcoverage.b * 255.0 + 0.5);
            int semanticTotalPriority = semanticPriority * 128 + semanticIndex;
            blockedBySemanticOBJ = candidateTotalPriority > semanticTotalPriority;
            sameSemanticAffineOBJ = objcoverage.g > 0.5 &&
                candidateTotalPriority == semanticTotalPriority;
        }

        bool candidateWins = supportedMaterial && !blockedBySemanticOBJ;
        if (candidateWins && mask1 == (1 << 4))
        {
            candidateWins = sameSemanticAffineOBJ ||
                layercol[4].a <= 0.0 ||
                candidateTotalPriority <= objflags.a * 128 +
                    int(objcoverage.b * 255.0 + 0.5);
        }
        else if (candidateWins && (mask1 & 0xF) != 0)
        {
            int topBG = mask1 == (1 << 0) ? 0 :
                        mask1 == (1 << 1) ? 1 :
                        mask1 == (1 << 2) ? 2 : 3;
            candidateWins = candidatePriority <= gBGPrio[topBG];
        }

        if (candidateWins)
        {
            bool presentationEffectsEnabled =
                (winsel & (1u << 5)) != 0u;
            ivec4 completedSemanticUnderlay = ResolveExposedWinner(
                preEffectCol2,
                mask2 >> 8,
                specialType2,
                preEffectCol3,
                mask3,
                presentationEffectsEnabled);
            vec3 presentationRGB = xbrzPresentationColor.rgb;
            if (uPresentationContourPremultiplied)
            {
                presentationRGB /= max(xbrzPresentationColor.a, 0.00001);
            }
            ivec4 presentationForeground = ivec4(
                vec4(clamp(presentationRGB, 0.0, 1.0), 1.0) * 255.0) >>
                ivec4(2, 2, 2, 3);
            ivec4 presentationMaterialUnderlay =
                sameSemanticAffineOBJ && mask1 == (1 << 4)
                    ? preEffectCol2 : preEffectCol1;
            ivec4 presentationUnderlay =
                sameSemanticAffineOBJ && mask1 == (1 << 4)
                    ? completedSemanticUnderlay : col1;
            int presentationUnderlayMask =
                sameSemanticAffineOBJ && mask1 == (1 << 4)
                    ? (mask2 >> 8) : mask1;
            presentationForeground = CompletePresentationOBJForeground(
                presentationForeground,
                presentationSpecialType,
                presentationMaterialUnderlay,
                presentationUnderlayMask,
                presentationEffectsEnabled);
            float presentationWeight = clamp(
                uPresentationContourPremultiplied
                    ? xbrzPresentationColor.a
                    : xbrzPresentationCoverage.r,
                0.0, 1.0);
            oPresentation = vec4(
                vec3(presentationForeground.rgb << 2) / 255.0,
                presentationWeight);
            oPresentationUnderlay = vec4(
                vec3(presentationUnderlay.rgb << 2) / 255.0, 1.0);
            vec4 mixed = mix(vec4(presentationUnderlay),
                             vec4(presentationForeground),
                             presentationWeight);
            col1 = ivec4(clamp(mixed + vec4(0.5),
                               vec4(0.0), vec4(63.0)));
            usedXBRZPresentationContour = true;
        }
    }

    // An empty output-grid contour is a valid presentation result, just as
    // an empty tap is in the 2x path. Remove only a semantic winner whose
    // material this presentation represents. Normal-only unions must retain
    // special OBJ operations (including shadows) that they do not contain.
    else if (uUseXBRZPresentationContour && !presentationContour2x &&
        xbrzPresentationColor.a <= 0.0 &&
        (winsel & (1u << 4)) != 0u &&
        mask1 == (1 << 4) && objcoverage.g > 0.5 &&
        SupportsPresentationOBJMaterial(objflags.r) &&
        (!uPresentationContourPremultiplied || objflags.r == 0))
    {
        col1 = ResolveExposedWinner(
            preEffectCol2, mask2 >> 8, specialType2,
            preEffectCol3, mask3, (winsel & (1u << 5)) != 0u);
        usedXBRZPresentationContour = true;
    }

    // Enhanced affine OBJ keeps native binary presence and priority above,
    // then uses its independently reconstructed alpha only to soften the
    // already-resolved winner over the semantic layer below. Effects remain
    // outside this first bounded capability.
    if (!ordinaryOBJCoverageCompleted && !usedXBRZPresentationContour &&
        uUseEnhancedOBJPresentationCoverage &&
        mask1 == (1 << 4) &&
        (objcoverage.g > 0.5 || objcoverage.a > 0.5))
    {
        // Apply presentation coverage after the exact DS winner/effect has
        // been evaluated. This also serves the unified ordinary-OBJ surface:
        // layer 2.a marks reconstructed presentation coverage without
        // changing semantic presence or the authored mode-1/bitmap alpha.
        ivec4 presentationUnderlay = ResolveExposedWinner(
            preEffectCol2,
            mask2 >> 8,
            specialType2,
            preEffectCol3,
            mask3,
            (winsel & (1u << 5)) != 0u);
        oPresentation = vec4(vec3(col1.rgb << 2) / 255.0,
                             clamp(visualCoverage1, 0.0, 1.0));
        oPresentationUnderlay = vec4(
            vec3(presentationUnderlay.rgb << 2) / 255.0, 1.0);
        vec4 mixed = mix(vec4(presentationUnderlay), vec4(col1),
                         clamp(visualCoverage1, 0.0, 1.0));
        col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
    }

    // Cheap affine-BG presentation experiment. Fractional native-alpha
    // coverage is allowed to extend into the bilinear footprint and is
    // consumed only after priority/window selection and winner-local color
    // effects. This is presentation, not literal DS ownership.
    if ((mask1 == (1 << 0) &&
         (UseAnyEnhancedTextBGPresentation(0) ||
          UseBilinearAffineBGPresentation(0))) ||
        (mask1 == (1 << 1) &&
         (UseAnyEnhancedTextBGPresentation(1) ||
          UseBilinearAffineBGPresentation(1))) ||
        (mask1 == (1 << 2) &&
         (UseAnyEnhancedTextBGPresentation(2) ||
          UseBilinearAffineBGPresentation(2))) ||
        (mask1 == (1 << 3) &&
         (UseAnyEnhancedTextBGPresentation(3) ||
          UseBilinearAffineBGPresentation(3))))
    {
        // Presentation coverage crosses a semantic layer boundary. Resolve
        // the second semantic winner as if this fractional candidate were
        // absent, including that winner's own special/material alpha or DS
        // Target-1 operation against the third ordered operand. Mixing raw
        // preEffectCol2 here leaks moving lower-layer content whenever its
        // completed result differs from the raw sample.
        int underlayTarget1Mask = mask2 >> 8;
        ivec4 presentationUnderlay = ResolveExposedWinner(
            preEffectCol2,
            underlayTarget1Mask,
            specialType2,
            preEffectCol3,
            mask3,
            (winsel & (1u << 5)) != 0u);
        vec4 mixed = mix(vec4(presentationUnderlay), vec4(col1),
                         clamp(visualCoverage1, 0.0, 1.0));
        col1 = ivec4(clamp(mixed + vec4(0.5), vec4(0.0), vec4(63.0)));
    }

    if (uDirect3DEndpointMode != 0)
    {
        // Endpoint reconstruction also needs the compositor's semantic
        // decision about Direct3D alpha. A nonzero 3D texel is normally an
        // ordinary selected BG0 sample; its 5-bit alpha becomes a blend
        // coefficient only when the selected layer below is a configured
        // Target 2. Export that decision alongside the opaque-black probe so
        // the high-resolution operator does not mistake internal 3D edge
        // coverage for transparency into a hidden 2D layer.
        bool direct3DUsesMaterialAlpha =
            mask1 == (1 << 0) && effect == 4;
        oPresentation = vec4(direct3DUsesMaterialAlpha ? 1.0 : 0.0,
                             mask1 == (1 << 0) ? 1.0 : 0.0,
                             0.0, 1.0);
        oPresentationUnderlay = vec4(0.0);
    }

    vec3 finalColor = vec3(col1.rgb << 2) / 255.0;
    if (uDebugTintBySource == 2)
    {
        int underlayMask = mask2 >> 8;
        int underlaySpecialType = underlayMask == (1 << 0) && gEnable3D
            ? 1 : 0;
        vec3 selectedTint = DebugTintColor(mask1, specialType);
        vec3 underlayTint = DebugTintColor(underlayMask,
                                           underlaySpecialType);
        finalColor = mix(underlayTint, selectedTint,
                         clamp(visualCoverage1, 0.0, 1.0));
    }
    else if (uDebugTintBySource == 1)
    {
        vec3 tint = DebugPathTint(mask1, specialType, debugPath1);
        float luma = dot(finalColor, vec3(0.299, 0.587, 0.114));
        finalColor = mix(finalColor * 0.2, tint * max(luma, 0.35), 0.85);
    }

    return vec4(finalColor, 1);
}

void main()
{
    oColor = CompositeLayers();
}
