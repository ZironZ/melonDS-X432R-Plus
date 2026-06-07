/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef RENDERERDEBUG_H
#define RENDERERDEBUG_H

#include "types.h"

namespace melonDS
{

enum class WholeScene2DDebugView : u8
{
    NativeFinal = 0,
    NativeExactFinal,
    Native3DResolve,
    Native3DSemantics,
    FinalNative3DInput,
    NativeTopColor,
    NativeSecondColor,
    NativeMeta,
    NativeBG0Color,
    NativeBG1Color,
    NativeBG2Color,
    NativeBG3Color,
    NativeOBJColor,
    NativeOBJFlags,
    NativeOBJCoverage,
    Native3DStackRole,
    Upscaled3DStackRole,
    UpscaledTopColor,
    UpscaledSecondColor,
    UpscaledMeta,
    UpscaledCoverage,
    HighResBG0Color,
    HighResBG1Color,
    HighResBG2Color,
    HighResBG3Color,
    HighResBG0Meta,
    HighResBG1Meta,
    HighResBG2Meta,
    HighResBG3Meta,
    HighResOBJColor,
    HighResOBJFlags,
    HighResOBJCoverage,
    Direct3D,
    OverlayOperatorColor,
    OverlayUnderlayWeight,
    OverlayReconstructedNative,
    OverlayReconstructionError,
    OverlayValidityConfidence,
    OverlayOwnershipReason,
    HybridSelector,
    HybridCoverageMiss,
    HybridForegroundAlpha,
    HybridFinalSource,
    SandwichLower2D,
    SandwichUpper2D,
    SandwichEligibility,
    OverlayEnhancedUnderlay,
    OverlayTrueNativeFinal,
    OverlayFinalResult,
    FinalTop,
    FinalBottom,
};

}

#endif
