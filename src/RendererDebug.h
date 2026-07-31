/*
    Copyright 2026 ZironZ

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

#include <vector>

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
    MainVRAMDisplayRaw,
    MainVRAMDisplayRawBank0,
    MainVRAMDisplayRawBank1,
    MainVRAMDisplayRawBank2,
    MainVRAMDisplayRawBank3,
    CaptureOutput256Bank0,
    CaptureOutput256Bank1,
    CaptureOutput256Bank2,
    CaptureOutput256Bank3,
    HighResDisplayCaptureFullBank0,
    HighResDisplayCaptureFullBank1,
    HighResDisplayCaptureFullBank2,
    HighResDisplayCaptureFullBank3,
    HighResDisplayCaptureBackgroundBank0,
    HighResDisplayCaptureBackgroundBank1,
    HighResDisplayCaptureBackgroundBank2,
    HighResDisplayCaptureBackgroundBank3,
    MainVRAMDisplayEpochBank0,
    MainVRAMDisplayEpochBank1,
    MainVRAMDisplayEpochBank2,
    MainVRAMDisplayEpochBank3,
};

struct WholeScene2DEngineDebugIdentity
{
    int Path = 0;
    int ProductChoice = 0;
    int SourceAResolutionMode = 0;
    int ChosenProductKind = 0;
    int ChosenProductRenderAction = 0;
    int ChosenProductTex = 0;
    int ChosenProductCaptureBank = -1;
    u64 ChosenProductBackgroundEpochSerial = 0;
    u64 ChosenProductSource3DSerial = 0;
    u64 ChosenProductCaptureEventSerial = 0;
    u64 ChosenProductCapturePresentationHash = 0;
    u64 ChosenProductCurrentPresentationHash = 0;
    u64 RequestCapturePresentationHash = 0;
    u64 RequestCurrentPresentationHash = 0;
};

struct WholeScene2DFinalDebugFrame
{
    u64 Serial = 0;
    bool TimingFrameValid = false;
    u64 TimingFrame = 0;
    int FinalTopSource = -1;
    int FinalBottomSource = -1;
    WholeScene2DEngineDebugIdentity EngineA;
    WholeScene2DEngineDebugIdentity EngineB;
    int Width = 0;
    int Height = 0;
    std::vector<u32> TopRGBA;
    std::vector<u32> BottomRGBA;
};

}

#endif
