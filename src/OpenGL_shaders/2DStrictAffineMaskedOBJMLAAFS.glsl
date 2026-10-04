// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2D FinalColorTex;
uniform sampler2D OBJMaskTex;
uniform int uScaleFactor;
uniform bool uDebugCoverage;

out vec4 oColor;

const int maxSearchSteps = 12;

ivec2 ClampCoord(ivec2 coord)
{
    return clamp(coord, ivec2(0), textureSize(FinalColorTex, 0) - ivec2(1));
}

bool MaskAt(ivec2 uncheckedCoord)
{
    return texelFetch(OBJMaskTex, ClampCoord(uncheckedCoord), 0).r > 0.5;
}

vec4 ResolveOutput(vec4 centerColor, vec4 oppositeColor,
                   bool centerMask, float blendWeight)
{
    if (uDebugCoverage)
    {
        float selectedCoverage = centerMask
            ? 1.0 - blendWeight : blendWeight;
        return vec4(vec3(selectedCoverage), 1.0);
    }
    return vec4(mix(centerColor.rgb, oppositeColor.rgb, blendWeight),
                centerColor.a);
}

bool PairContinues(ivec2 coord, ivec2 normal, bool centerMask)
{
    return MaskAt(coord) == centerMask &&
           MaskAt(coord + normal) != centerMask;
}

int SearchEdgeEnd(ivec2 coord, ivec2 tangent, ivec2 normal,
                  bool centerMask, int searchLimit)
{
    for (int distance = 1; distance <= maxSearchSteps; distance++)
    {
        if (distance > searchLimit ||
            !PairContinues(coord + tangent * distance,
                           normal, centerMask))
            return distance;
    }
    return searchLimit + 1;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 centerColor = texelFetch(FinalColorTex, coord, 0);
    bool centerMask = MaskAt(coord);

    bool north = MaskAt(coord + ivec2(0, 1));
    bool south = MaskAt(coord + ivec2(0, -1));
    bool east = MaskAt(coord + ivec2(1, 0));
    bool west = MaskAt(coord + ivec2(-1, 0));

    if (north == centerMask && south == centerMask &&
        east == centerMask && west == centerMask)
    {
        oColor = ResolveOutput(centerColor, centerColor,
                               centerMask, 0.0);
        return;
    }

    bool northWest = MaskAt(coord + ivec2(-1, 1));
    bool northEast = MaskAt(coord + ivec2(1, 1));
    bool southWest = MaskAt(coord + ivec2(-1, -1));
    bool southEast = MaskAt(coord + ivec2(1, -1));
    float c = centerMask ? 1.0 : 0.0;
    float n = north ? 1.0 : 0.0;
    float s = south ? 1.0 : 0.0;
    float e = east ? 1.0 : 0.0;
    float w = west ? 1.0 : 0.0;
    float nw = northWest ? 1.0 : 0.0;
    float ne = northEast ? 1.0 : 0.0;
    float sw = southWest ? 1.0 : 0.0;
    float se = southEast ? 1.0 : 0.0;

    // Classify the dominant contour direction from the semantic mask rather
    // than from color/luma. These are the orientation terms used by the
    // neighborhood stage of morphological post-process AA.
    float horizontalEnergy = abs(-2.0 * c + n + s) * 2.0 +
                             abs(-2.0 * w + nw + sw) +
                             abs(-2.0 * e + ne + se);
    float verticalEnergy = abs(-2.0 * c + w + e) * 2.0 +
                           abs(-2.0 * n + nw + ne) +
                           abs(-2.0 * s + sw + se);
    bool horizontalEdge = horizontalEnergy >= verticalEnergy;

    ivec2 normal;
    ivec2 tangent;
    if (horizontalEdge)
    {
        bool northDiffers = north != centerMask;
        bool southDiffers = south != centerMask;
        if (northDiffers == southDiffers)
        {
            oColor = ResolveOutput(centerColor, centerColor,
                                   centerMask, 0.0);
            return;
        }
        normal = northDiffers ? ivec2(0, 1) : ivec2(0, -1);
        tangent = ivec2(1, 0);
    }
    else
    {
        bool eastDiffers = east != centerMask;
        bool westDiffers = west != centerMask;
        if (eastDiffers == westDiffers)
        {
            oColor = ResolveOutput(centerColor, centerColor,
                                   centerMask, 0.0);
            return;
        }
        normal = eastDiffers ? ivec2(1, 0) : ivec2(-1, 0);
        tangent = ivec2(0, 1);
    }

    int searchLimit = clamp(uScaleFactor * 2, 4, maxSearchSteps);
    int negativeDistance = SearchEdgeEnd(coord, -tangent, normal,
                                         centerMask, searchLimit);
    int positiveDistance = SearchEdgeEnd(coord, tangent, normal,
                                         centerMask, searchLimit);

    // A long uninterrupted axis-aligned edge should stay crisp. For a
    // bounded staircase run, inspect which semantic side fills the pair just
    // beyond each endpoint. Opposite endpoint fills describe a diagonal: its
    // area contribution is a directional ramp across the complete run, not
    // merely a symmetric blur at the two corners.
    float totalDistance = float(negativeDistance + positiveDistance);
    float nearestEnd = float(min(negativeDistance, positiveDistance));
    float position = float(negativeDistance) / totalDistance;
    float blendWeight = 0.0;

    bool negativeEndFound = negativeDistance <= searchLimit;
    bool positiveEndFound = positiveDistance <= searchLimit;
    bool negativeEndClosed = false;
    bool positiveEndClosed = false;
    bool negativeEndMask = centerMask;
    bool positiveEndMask = centerMask;

    if (negativeEndFound)
    {
        ivec2 endCoord = coord - tangent * negativeDistance;
        bool sideA = MaskAt(endCoord);
        bool sideB = MaskAt(endCoord + normal);
        negativeEndClosed = sideA == sideB;
        negativeEndMask = sideA;
    }
    if (positiveEndFound)
    {
        ivec2 endCoord = coord + tangent * positiveDistance;
        bool sideA = MaskAt(endCoord);
        bool sideB = MaskAt(endCoord + normal);
        positiveEndClosed = sideA == sideB;
        positiveEndMask = sideA;
    }

    if (negativeEndClosed && positiveEndClosed &&
        negativeEndMask != positiveEndMask)
    {
        // The endpoint whose continuation is the opposite semantic side cuts
        // progressively farther into this pixel pair. The opposite pixel in
        // the pair evaluates the complementary ramp, preserving line weight.
        blendWeight = negativeEndMask != centerMask
            ? 0.5 * (1.0 - position)
            : 0.5 * position;
    }
    else
    {
        // U/L endpoints and search-limited corners retain a conservative
        // triangular area response, but only when the discovered endpoint
        // actually continues as the opposite semantic side.
        float cornerWeight = max(0.0, 0.5 - nearestEnd / totalDistance);
        if ((negativeEndClosed && negativeEndMask != centerMask) ||
            (positiveEndClosed && positiveEndMask != centerMask))
            blendWeight = cornerWeight;
    }

    if (blendWeight <= 0.0)
    {
        oColor = ResolveOutput(centerColor, centerColor,
                               centerMask, 0.0);
        return;
    }

    ivec2 oppositeCoord = ClampCoord(coord + normal);
    vec4 oppositeColor = texelFetch(FinalColorTex, oppositeCoord, 0);

    // This is deliberately a convex blend of the exact two semantic sides
    // of this boundary. Search taps influence only the geometric weight; no
    // distant color can leak into the result or create filter overshoot.
    oColor = ResolveOutput(centerColor, oppositeColor,
                           centerMask, blendWeight);
}
