// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#version 140

uniform sampler2DArray SemanticOBJLayerTex;
uniform sampler2D OrdinaryPresentationTex;
uniform sampler2DArray NativeOrdinaryOBJLayerTex;
uniform int uScaleFactor;

out vec4 oColor;
out vec4 oFlags;
out vec4 oCoverage;

bool FetchNativeBoundaryCandidate(ivec2 coord,
                                  out vec4 candidateFlags,
                                  out vec4 candidateCoverage)
{
    ivec2 nativeSize = textureSize(NativeOrdinaryOBJLayerTex, 0).xy;
    vec2 sourcePos = (vec2(coord) + vec2(0.5)) /
                     float(uScaleFactor) - vec2(0.5);
    ivec2 p0 = ivec2(floor(sourcePos));
    ivec2 offsets[4] = ivec2[4](
        ivec2(0, 0), ivec2(1, 0),
        ivec2(0, 1), ivec2(1, 1));

    bool foundPresent = false;
    bool foundTransparent = false;
    int boundaryPriority = -1;
    int boundaryMaterial = -1;
    int boundarySpriteIndex = 128;
    candidateFlags = vec4(0.0);
    candidateCoverage = vec4(0.0);

    for (int tap = 0; tap < 4; tap++)
    {
        ivec2 sourceCoord = p0 + offsets[tap];
        if (any(lessThan(sourceCoord, ivec2(0))) ||
            any(greaterThanEqual(sourceCoord, nativeSize)))
        {
            foundTransparent = true;
            continue;
        }

        vec4 color = texelFetch(NativeOrdinaryOBJLayerTex,
                                ivec3(sourceCoord, 0), 0);
        if (color.a <= 0.0)
        {
            foundTransparent = true;
            continue;
        }

        vec4 flags = texelFetch(NativeOrdinaryOBJLayerTex,
                                ivec3(sourceCoord, 1), 0);
        vec4 coverage = texelFetch(NativeOrdinaryOBJLayerTex,
                                   ivec3(sourceCoord, 2), 0);
        int material = int(flags.r * 255.0 + 0.5);
        int priority = int(flags.a * 255.0 + 0.5);
        int spriteIndex = int(coverage.b * 255.0 + 0.5);

        // Normal and mode-1 ordinary OBJ use the compositor's existing DS
        // effect equations. Bitmap alpha, mosaic, OBJ-window and affine
        // samples still require their established semantic path.
        if (material > 1 || flags.g > 0.5 || flags.b > 0.5 ||
            coverage.g > 0.5)
            return false;

        if (!foundPresent)
        {
            foundPresent = true;
            boundaryPriority = priority;
            boundaryMaterial = material;
            boundarySpriteIndex = spriteIndex;
            candidateFlags = flags;
            candidateCoverage = coverage;
        }
        else if (priority != boundaryPriority || material != boundaryMaterial ||
                 (material == 1 && spriteIndex != boundarySpriteIndex))
        {
            // Do not invent one effect/ordering identity from conflicting
            // contributors. Mode-1 exterior is admitted only for one sprite;
            // its DS blend is resolved before applying presentation coverage.
            return false;
        }
        else if (spriteIndex < boundarySpriteIndex)
        {
            // Normal ordinary pieces with the same BG priority have the same
            // scene ordering and effect behavior. Their already-resolved
            // surface may have one continuous exterior across an OAM seam.
            // Retain the frontmost contributing identity for a deterministic
            // candidate; never replace an existing semantic OBJ winner here.
            boundarySpriteIndex = spriteIndex;
            candidateFlags = flags;
            candidateCoverage = coverage;
        }
    }

    return foundPresent && foundTransparent;
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 semanticColor = texelFetch(SemanticOBJLayerTex,
                                    ivec3(coord, 0), 0);
    vec4 semanticFlags = texelFetch(SemanticOBJLayerTex,
                                    ivec3(coord, 1), 0);
    vec4 semanticCoverage = texelFetch(SemanticOBJLayerTex,
                                       ivec3(coord, 2), 0);

    // Layer 2.g is written by the sprite resolver and identifies a hardware
    // affine winner.  Keep affine source reconstruction intact.  Ordinary
    // winners receive RGB and presentation coverage from the one native
    // OAM-resolved ordinary surface, while semantic presence, material alpha,
    // priority, mode and OAM identity remain those of the authoritative
    // output-resolution rasterization.
    bool ordinarySemanticWinner =
        semanticColor.a > 0.0 && semanticCoverage.g <= 0.5;
    if (ordinarySemanticWinner)
    {
        vec4 presentation = texelFetch(OrdinaryPresentationTex, coord, 0);
        if (presentation.a > (0.5 / 65535.0))
            semanticColor.rgb = presentation.rgb;
        semanticCoverage.r = clamp(presentation.a, 0.0, 1.0);

        // Layer 2.a is presentation capability, not semantic presence.  The
        // compositor consumes it only after the winner and DS effect have
        // been selected.
        semanticCoverage.a = 1.0;
    }
    else if (semanticColor.a <= 0.0)
    {
        vec4 presentation = texelFetch(OrdinaryPresentationTex, coord, 0);
        vec4 candidateFlags;
        vec4 candidateCoverage;
        if (presentation.a > (0.5 / 65535.0) &&
            FetchNativeBoundaryCandidate(coord,
                                         candidateFlags,
                                         candidateCoverage))
        {
            // This is a presentation candidate, not unconditional ownership.
            // Supplying its locally proven priority/OAM identity lets the
            // ordinary strict compositor decide against every BG, window and
            // effect at this exact output pixel. Presentation alpha is then
            // consumed only after the winner and underlay are known.
            semanticColor = vec4(presentation.rgb, 1.0);
            semanticFlags = candidateFlags;
            semanticCoverage = vec4(clamp(presentation.a, 0.0, 1.0),
                                    0.0,
                                    candidateCoverage.b,
                                    1.0);
        }
    }

    oColor = semanticColor;
    oFlags = semanticFlags;
    oCoverage = semanticCoverage;
}
