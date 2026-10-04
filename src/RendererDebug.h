// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RENDERERDEBUG_H
#define RENDERERDEBUG_H

#include "types.h"
#include "WholeSceneOBJBandPolicy.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace melonDS
{

// A caller-owned batch of reads of ONE immutable frame from ONE renderer.
// Keep rendering/settings/source mutation excluded for its entire lifetime;
// discard it before releasing the GL borrow or advancing emulation.
// This caches shared descriptions only, never view-specific text or scratch images.
class WholeScene2DDebugReadContext
{
    friend class GLRenderer2D;
    std::array<std::optional<std::string>, 2> EngineStatus;
};

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
    StrictAffineCandidate,
    StrictAffineNativeReference,
    StrictAffineDownsampleDifference,
    StrictAffineEnhancedBG2Source,
    StrictAffineEnhancedBG3Source,
    StrictAffineEnhancedOBJSource,
    StrictAffineTransformedOBJColor,
    StrictAffineTransformedOBJCoverage,
    StrictAffineOrdinaryOBJBand0Color,
    StrictAffineOrdinaryOBJBand0Coverage,
    StrictAffineOrdinaryOBJBand0Scaled,
    StrictAffineResolvedOrdinaryOBJ,
    StrictAffineOverlapSemantic,
    StrictAffineOverlapUnderlay,
    StrictAffineOverlapDecisions,
    StrictAffineOverlapSubpixelColor,
    StrictAffineOverlapSubpixelFlags,
    StrictAffineOverlapSubpixelCoverage,
    NativeOBJSourceAtlas,
    StrictAffineEnhancedOBJSourceAtlas,
    StrictAffineAssembledOBJSourceAtlas,
};

struct WholeScene2DEngineDebugIdentity
{
    int Path = 0;
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

// CPU-side facts captured from the OAM state that produced an affine OBJ.
// These records deliberately describe source/placement state rather than
// inferring grouping from rendered colors.
struct WholeScene2DAffineOBJDebugRecord
{
    int RenderedIndex = -1;
    int OAMIndex = -1;
    u16 Attr0 = 0;
    u16 Attr1 = 0;
    u16 Attr2 = 0;
    int PositionX = 0;
    int PositionY = 0;
    int Width = 0;
    int Height = 0;
    int BoundWidth = 0;
    int BoundHeight = 0;
    int RotscaleIndex = -1;
    s32 Rotscale[4] {};
    u32 OBJMode = 0;
    u32 SourceType = 0;
    u32 PaletteOffset = 0;
    u32 TileOffset = 0;
    u32 TileStride = 0;
    u32 Priority = 0;
    bool Mosaic = false;
    bool DoubleSize = false;
    bool IdentityTransform = false;
    bool EnhancedSourceCacheValid = false;
    u64 SourceGeneration = 0;
    u32 EnhancedAlgorithm = 0;
    u32 EnhancedSourceScale = 0;
    int CandidateGroupMemberIndex = -1;
    int CandidateGroupMemberCount = 0;
    int CandidateGroupAnchorOAMIndex = -1;
    s64 CandidateGroupSourceOffsetX512 = 0;
    s64 CandidateGroupSourceOffsetY512 = 0;
};

enum WholeScene2DAffineOBJGroupRejection : u32
{
    AffineOBJGroupRejectNone = 0,
    AffineOBJGroupRejectUnsupportedSource = 1u << 0,
    AffineOBJGroupRejectNonNormalMode = 1u << 1,
    AffineOBJGroupRejectMosaic = 1u << 2,
    AffineOBJGroupRejectMixedMode = 1u << 3,
    AffineOBJGroupRejectMixedSourceType = 1u << 4,
    AffineOBJGroupRejectMixedPriority = 1u << 5,
    AffineOBJGroupRejectNoBoundContact = 1u << 6,
};

// A factual partition by the OAM rotscale-parameter index. This is a
// candidate reconstruction unit, not proof that its members form one authored
// object. Canonical source offsets are derived from screen-center differences
// through the current inverse OAM matrix; a stable layout hash across matrix
// motion is the temporal evidence a real shared group still needs.
struct WholeScene2DAffineOBJGroupDebugRecord
{
    int RotscaleIndex = -1;
    int AnchorRenderedIndex = -1;
    int AnchorOAMIndex = -1;
    int MemberCount = 0;
    s32 Rotscale[4] {};
    u64 MemberOAMMask[2] {};
    int TouchingPairs = 0;
    int OverlappingPairs = 0;
    int MinX = 0;
    int MinY = 0;
    int MaxX = 0;
    int MaxY = 0;
    bool AllNormalMode = false;
    bool AllSupportedSource = false;
    bool NoMosaic = false;
    bool SameMode = false;
    bool SameSourceType = false;
    bool SamePriority = false;
    bool AllEnhancedCachesValid = false;
    bool SingletonCacheControlReady = false;
    bool StructuralMultiSpriteCandidate = false;
    bool RequiresTemporalLayoutProof = false;
    u32 RejectionMask = AffineOBJGroupRejectNone;
    u64 SourceHash = 0;
    u64 CanonicalLayoutHash = 0;
};

struct WholeScene2DAffineOBJDebugSummary
{
    int Count = 0;
    int TransformedCount = 0;
    int DoubleSizeCount = 0;
    int SemiTransparentCount = 0;
    int BitmapCount = 0;
    int MosaicCount = 0;
    int DistinctRotscaleCount = 0;
    int SharedRotscaleSpriteCount = 0;
    int SingletonGroupCount = 0;
    int SharedGroupCount = 0;
    int SingletonCacheControlReadyCount = 0;
    int StructuralMultiSpriteCandidateGroupCount = 0;
    int RejectedSharedGroupCount = 0;
    int TouchingPairs = 0;
    int OverlappingPairs = 0;
    int SameRotscaleTouchingPairs = 0;
    int SameRotscaleOverlappingPairs = 0;
    int SameStateTouchingPairs = 0;
    int SameStateOverlappingPairs = 0;
    int MinX = 0;
    int MinY = 0;
    int MaxX = 0;
    int MaxY = 0;
    u32 OBJModeMask = 0;
    u32 SourceTypeMask = 0;
    u32 PriorityMask = 0;
    u64 SourceGeneration = 0;
    u64 SourceHash = 0;
    u64 TransformHash = 0;
    u64 GroupSourceHash = 0;
    u64 CanonicalGroupLayoutHash = 0;
};

struct WholeScene2DAffineOBJDebugEvidence
{
    WholeScene2DAffineOBJDebugSummary Summary;
    std::vector<WholeScene2DAffineOBJDebugRecord> Records;
    std::vector<WholeScene2DAffineOBJGroupDebugRecord> Groups;
    WholeSceneOBJBandClassification OrdinaryBands;
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
    WholeScene2DAffineOBJDebugEvidence EngineAAffineOBJ;
    WholeScene2DAffineOBJDebugEvidence EngineBAffineOBJ;
    int Width = 0;
    int Height = 0;
    std::vector<u32> TopRGBA;
    std::vector<u32> BottomRGBA;
};

}

#endif
