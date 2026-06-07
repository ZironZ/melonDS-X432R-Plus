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

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "OpenGLSupport.h"
#include "GPU2D.h"
#include "RendererDebug.h"
#include "RendererSettings.h"

namespace melonDS
{
class GLRenderer;

class GLRenderer2D : public Renderer2D
{
public:
    GLRenderer2D(melonDS::GPU2D& gpu2D, GLRenderer& parent);
    ~GLRenderer2D() override;
    bool Init() override;
    void Reset() override;

    bool InitShaders();
    bool InitShaders(GLRenderer2D& other);
    void DeleteShaders();

    void PostSavestate();

    void SetScaleFactor(int scale);
    void SetRenderSettings(int scale, const RendererSettings::WholeScene2DScaleSettings& settings);
    void SetWholeSceneScaleRequested(bool enable);
    void SetWholeSceneScaleSourceBoundaryGuard(bool enable);
    void SetWholeSceneScaleMode(RendererSettings::WholeScene2DScaleMode mode);
    void SetWholeSceneScaleAlgorithm(RendererSettings::GLScaleAlgorithm algorithm);
    void SetWholeSceneScaleFragmentationFallback(RendererSettings::WholeScene2DFragmentationFallback fallback);
    void SetWholeSceneScaleExactFinalFallback(bool enable);
    void SetWholeSceneScaleForegroundOverlay(bool enable);
    void SetWholeSceneScaleCaptureBacked(bool enable);
    void SetWholeSceneScaleDebugTint(bool enable);
    void SetWholeSceneScaleNoWrapFilterTaps(bool enable);
    void SetWholeSceneScaleFinalUpscaleRender3DNative(bool enable);
    void SetWholeSceneScaleFinalUpscale3DFilter(RendererSettings::FinalUpscale3DDownsampleFilter filter);
    void SetWholeSceneScaleFinalUpscale3DCoverageAware(bool enable);
    void SetWholeSceneScaleFinalUpscale3DRepresentativeSemantics(bool enable);
    void SetWholeSceneScaleFinalUpscale3DSplitSemantics(bool enable);
    void SetWholeSceneScaleFinalUpscale3DSharpenSplitCoverage(bool enable);
    void SetWholeSceneScaleOverlayLegacyUnderlay(bool enable);
    void SetWholeSceneScaleHybridWindowEdgeAssist(bool enable);
    void SetWholeSceneScaleHybridTarget2AlphaBlendAssist(bool enable);
    void SetWholeSceneScaleHybridNativeEffectGuard(bool enable);
    void SetWholeSceneScaleHybridForeground2DBase(bool enable);
    void SetWholeSceneScaleHybridCleanLegacyCandidate(bool enable);
    void SetWholeSceneDebugPoison(bool source3D, bool native3DResolve, bool native3DResolveAlpha);
    void SetWholeSceneDebugViewsActive(bool active);
    bool ReadWholeSceneDebugView(WholeScene2DDebugView view,
                                 int& width,
                                 int& height,
                                 std::vector<u32>& rgba,
                                 std::string* status = nullptr) const;
    void AppendWholeSceneTimingCSVHeader(std::string& header, const char* prefix) const;
    void AppendWholeSceneTimingCSVRow(std::string& row) const;
    void ResetWholeSceneUpdateTiming();

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override;
    void VBlankEnd() override;

private:
    friend class GLRenderer;
    GLRenderer& Parent;

    int ScaleFactor;
    int ScreenW, ScreenH;

    GLuint LayerPreShader;
    GLint LayerPreCurBGULoc;

    GLuint ScanlineConfigUBO;
    GLuint SpriteScanlineConfigUBO;

    GLuint SpritePreShader;
    GLuint SpritePreVtxBuffer;
    GLuint SpritePreVtxArray;
    u16* SpritePreVtxData;

    GLuint SpriteShader;
    GLint SpriteRenderTransULoc;
    GLint SpriteFilterModeULoc;
    GLuint SpriteVtxBuffer;
    GLuint SpriteVtxArray;
    u16* SpriteVtxData;

    GLuint CompositorShader;
    GLuint CompositorConfigUBO;
    GLint CompositorScaleULoc;
    GLint CompositorOBJNativeResolutionULoc;
    GLint CompositorLayerFilterModeULoc;
    GLint CompositorLayerFilterNoWrapULoc;
    GLint CompositorDebugTintULoc;
    GLint CompositorSplit3DSemanticsULoc;
    GLint CompositorSharpenSplit3DCoverageULoc;

    GLuint NativePrepassShader;
    GLint NativePrepassScaleULoc;
    GLint NativePrepassDebugLayerULoc;
    GLuint NativeUpscaleShader;
    GLint NativeUpscaleScaleULoc;
    GLint NativeUpscaleLegacyFilterULoc;
    GLuint NativeBoundaryGuardShader;
    GLuint NativeResolveShader;
    GLuint Native3DResolveShader;
    GLint Native3DResolveFilterModeULoc;
    GLint Native3DResolveCoverageAwareULoc;
    GLint Native3DResolveRepresentativeSemanticsULoc;
    GLint Native3DResolveSplitSemanticsULoc;
    GLuint OverlayEndpointShader;
    GLint OverlayEndpointWhiteULoc;
    GLuint OverlayCompositeShader;
    GLint OverlayCompositeScaleULoc;
    GLint OverlayCompositeDebugTintULoc;
    GLint OverlayCompositeLegacyUnderlayULoc;
    GLint OverlayCompositeCoverageAwareULoc;
    GLint OverlayCompositeDirect3DPresentationSpaceULoc;
    GLuint OverlayHybridCompositeShader;
    GLint OverlayHybridCompositeScaleULoc;
    GLint OverlayHybridCompositeDebugTintULoc;
    GLint OverlayHybridCompositeLegacyUnderlayULoc;
    GLint OverlayHybridCompositeCoverageAwareULoc;
    GLint OverlayHybridCompositeDirect3DPresentationSpaceULoc;
    GLint OverlayHybridCompositeConservativeHybridULoc;
    GLint OverlayHybridCompositeWindowEdgeAssistULoc;
    GLint OverlayHybridCompositeTarget2AlphaBlendAssistULoc;
    GLint OverlayHybridCompositeNativeEffectGuardULoc;
    GLint OverlayHybridCompositeForeground2DBaseULoc;
    GLint OverlayHybridCompositeLegacyCandidateULoc;
    GLint OverlayHybridCompositeForceOverlayAssistULoc;
    GLint OverlayHybridCompositeDebugModeULoc;
    GLuint OverlayDebugShader;
    GLint OverlayDebugModeULoc;
    GLint OverlayDebugLegacyUnderlayULoc;
    GLint OverlayDebugCoverageAwareULoc;
    GLint NativeResolveScaleULoc;
    GLint NativeResolveUseExactFinalFallbackULoc;
    GLint NativeResolveUseForegroundOverlayULoc;
    GLint NativeResolveDebugTintULoc;
    GLint NativeResolveNativeExactOutputULoc;
    GLuint ArtCNNRGBToYUVAShader;
    GLuint ArtCNNConvShaders[RendererSettings::GLArtCNNModelCount][7];
    GLuint ArtCNNDepthToSpaceShaders[RendererSettings::GLArtCNNModelCount];
    GLuint ArtCNNSpline36Shader;
    GLuint ArtCNNYUVAToRGBA2xShader;
    GLuint NNEDI3Pass1Shader;
    GLuint NNEDI3Pass2Shader;
    GLuint XBRZPreprocessShader;
    GLuint XBRZFreescaleShader;

    // base index for a BG layer within the BG texture arrays
    // based on BG type and size
    const u8 BGBaseIndex[4][4] = {
        {2, 10, 6, 14},     // text mode
        {0, 4, 16, 20},     // rotscale
        {0, 4, 12, 16},     // bitmap
        {18, 19, 12, 16},   // large bitmap
    };

    GLuint LayerConfigUBO;
    GLuint SpriteConfigUBO;

    GLuint VRAMTex_BG;
    GLuint VRAMTex_OBJ;
    GLuint PalTex_BG;
    GLuint PalTex_OBJ;
    GLuint BlankColorTex;

    GLuint MosaicTex;

    GLuint AllBGLayerFB[22];
    GLuint AllBGLayerTex[22];
    GLuint AllBGLayerMetaTex[22];

    GLuint BGLayerFB[4];
    GLuint BGLayerTex[4];
    GLuint BGLayerMetaTex[4];

    GLuint SpriteFB;
    GLuint SpriteTex;

    GLuint OBJLayerFB;
    GLuint OBJLayerTex;
    GLuint OBJDepthTex;

    GLuint OutputFB;
    GLuint OutputTex;

    GLuint NativeOBJLayerFB;
    GLuint NativeOBJLayerTex;
    GLuint NativeOBJDepthTex;

    GLuint NativeOutputFB;
    GLuint NativeOutputTex;
    GLuint NativeTopColorTex;
    GLuint NativeSecondColorTex;
    GLuint NativeMetaTex;
    GLuint NativeLayerDebugFB;
    GLuint NativeLayerDebugTex;
    GLuint NativeExactFinalFB;
    GLuint NativeExactFinalTex;
    GLuint NativeDirect3DFB;
    GLuint NativeDirect3DTex;
    GLuint NativeDirect3DSemanticsTex;
    GLuint NativeDirect3DCompositorTex;
    GLuint NativeOverlayBlack3DTex;
    GLuint NativeOverlayWhite3DTex;
    GLuint NativeOverlayTrueFinalTex;
    GLuint NativeOverlayReconstructedTex;
    GLuint NativeOverlayErrorTex;
    GLuint NativeOverlayConfidenceTex;

    GLuint UpscaledStateFB;
    GLuint UpscaledTopColorTex;
    GLuint UpscaledSecondColorTex;
    GLuint UpscaledMetaTex;
    GLuint UpscaledCoverageTex;
    GLuint UpscaledExactFinalTex;
    GLuint UpscaledGuardColorTex;
    GLuint UpscaledOverlayUnderlayWeightTex;
    GLuint UpscaledOverlayOwnershipTex;
    GLuint HybridForegroundTex;
    GLuint HybridNativeFallbackTex;
    GLuint Hybrid2DBaseTex;
    GLuint HybridLegacyCandidateTex;
    GLuint HybridSelectorTex;
    GLuint HybridCoverageMissTex;
    GLuint HybridForegroundAlphaTex;
    GLuint HybridFinalSourceTex;
    static constexpr int kCaptureBackedHandoffRouteSlots = 2;
    GLuint WholeSceneSourceABlitFB;
    GLuint CaptureBackedHandoff3DFB[kCaptureBackedHandoffRouteSlots];
    GLuint CaptureBackedHandoff3DTex[kCaptureBackedHandoffRouteSlots];

    GLuint ArtCNNYUVTex;
    GLuint ArtCNNYUVFB;
    GLuint ArtCNNConv0Tex;
    GLuint ArtCNNConv0FB;
    GLuint ArtCNNConvWorkTex[2];
    GLuint ArtCNNConvWorkFB[2];
    GLuint ArtCNNPackedTex;
    GLuint ArtCNNPackedFB;
    GLuint NNEDI3Vertical4xTex;
    GLuint ArtCNNLuma2xTex;
    GLuint ArtCNNLuma2xFB;
    GLuint NNEDI3Luma4xTex;
    GLuint NNEDI3Luma4xCorrectedTex;
    GLuint NNEDI3YUVA4xTex;
    GLuint ArtCNNYUVA2xTex;
    GLuint ArtCNNRGBA2xTex;
    GLuint NNEDI3RGBA4xTex;
    GLuint ArtCNNOutputFB;
    GLuint NNEDI3VerticalTex;
    GLuint NNEDI3VerticalFB;
    GLuint XBRZInfoTex;
    GLuint XBRZInfoFB;

    enum class WholeSceneScaleEligibility : u8
    {
        ScreenUnavailable,
        EngineANotSupportedYet,
        MainEngineVRAMDisplay,
        MainEngineDisplayFIFO,
        CaptureActive,
        CaptureBackedBG,
        CaptureBackedOBJ,
        UnsupportedDisplayMode,
        Eligible,
    };

    bool WholeSceneScaleRequested;
    bool WholeSceneScaleSourceBoundaryGuard;
    RendererSettings::WholeScene2DScaleMode WholeSceneScaleMode;
    RendererSettings::GLScaleAlgorithm WholeSceneScaleAlgorithm;
    RendererSettings::WholeScene2DFragmentationFallback WholeSceneScaleFragmentationFallback;
    bool WholeSceneScaleExactFinalFallback;
    bool WholeSceneScaleForegroundOverlay;
    bool WholeSceneScaleCaptureBacked;
    bool WholeSceneScaleDebugTint;
    bool WholeSceneScaleNoWrapFilterTaps;
    bool WholeSceneScaleFinalUpscaleRender3DNative;
    RendererSettings::FinalUpscale3DDownsampleFilter WholeSceneScaleFinalUpscale3DFilter;
    bool WholeSceneScaleFinalUpscale3DCoverageAware;
    bool WholeSceneScaleFinalUpscale3DRepresentativeSemantics;
    bool WholeSceneScaleFinalUpscale3DSplitSemantics;
    bool WholeSceneScaleFinalUpscale3DSharpenSplitCoverage;
    bool WholeSceneScaleOverlayLegacyUnderlay;
    bool WholeSceneScaleHybridWindowEdgeAssist;
    bool WholeSceneScaleHybridTarget2AlphaBlendAssist;
    bool WholeSceneScaleHybridNativeEffectGuard;
    bool WholeSceneScaleHybridForeground2DBase;
    bool WholeSceneScaleHybridCleanLegacyCandidate;
    std::atomic_bool WholeSceneDebugViewsActive;
    WholeSceneScaleEligibility WholeSceneScaleState;

    enum class WholeSceneNative3DSource
    {
        None,
        NativeRendered,
        HighResLinearSampled,
        HighResResolved,
    };

    enum class WholeSceneRenderPath
    {
        None,
        Current,
        LegacyNativeUpscale,
        HighResCompositor,
        FinalNativeUpscale,
        OverlayOperatorUpscale,
        ConservativeHybridUpscale,
        CaptureBackedHandoff,
        SourceACaptureReplacement,
        CaptureEpochOverlay,
        PhysicalFinalPostprocessInput,
    };

    enum class WholeSceneOverlayEndpointFinalMode
    {
        None,
        MetadataResolve,
        ExactCompositor,
    };

    enum class SourceACaptureReplacementMode
    {
        None,
        FullProduct,
        CurrentOverlay,
        FullProductAfterOverlayFailed,
    };

    enum class SourceAProductChoiceReason
    {
        None,
        UsedFullProductKeyMatch,
        UsedFullProductNoOverlayVisible,
        UsedBackgroundUnderlayCurrentOverlay,
        RejectedFullProductKeyMismatch,
        RejectedCurrentOverlayKeyMismatch,
        RejectedMissingBackgroundProduct,
        RejectedMissingFullProduct,
        ReusedPreviousRouteProduct,
        FallbackNormalHybrid,
        FallbackFinalImage,
        UsedFullProductRouteBridge,
    };

    struct WholeSceneRenderTrace
    {
        WholeSceneRenderPath Path = WholeSceneRenderPath::None;
        WholeSceneNative3DSource Native3DSource = WholeSceneNative3DSource::None;
        WholeSceneOverlayEndpointFinalMode OverlayEndpointFinalMode = WholeSceneOverlayEndpointFinalMode::None;
        SourceACaptureReplacementMode SourceACaptureMode = SourceACaptureReplacementMode::None;
        SourceAProductChoiceReason SourceAProductChoice = SourceAProductChoiceReason::None;
        u64 SourceABackgroundEpochSerial = 0;
        u32 SourceACapturePresentationHash = 0;
        u32 SourceACurrentPresentationHash = 0;
        bool SourceAFullProductKeyMatch = false;
        int YStart = 0;
        int YEnd = 0;
        u64 RenderTimeUS = 0;
        int OutputTex3D = 0;
        int NativeStage3D = 0;
        bool HighRes3D = false;
        bool Linear3D = false;
        bool Resolve3D = false;
        bool NativeExactFinalValid = false;
        bool PhysicalFinalNativeInputValid = false;
        bool Native3DResolveValid = false;
        bool Native3DSemanticsValid = false;
        bool OverlayTrueFinalValid = false;
        bool HybridFragmentationFallback = false;
        bool CurrentFragmentationFallback = false;
        u32 NativeChunkAccumulationPasses = 0;
        u32 FullFrameFinalizerPasses = 0;
        u32 NativeProductValidRows = 0;
        bool NativeProductsFrameComplete = false;
        bool NativeProductEpochValid = true;
        bool NativeProductFinalizerPathSeen = false;
    };

    struct WholeSceneDebugPoisonState
    {
        bool Source3D = false;
        bool Native3DResolve = false;
        bool Native3DResolveAlpha = false;
    };

    struct SourceACaptureReplacementChoice
    {
        // Keep product/overlay selection separate from the GL blit/composite path.
        GLuint FullProductTex = 0;
        GLuint BackgroundTex = 0;
        int CaptureBank = -1;
        bool SubEngineCapturedSourceAOnly = false;
        bool CanUseCurrentOverlay = false;
        GLRenderer2D* MainRenderer = nullptr;
        u64 BackgroundEpochSerial = 0;
        u32 CapturePresentationHash = 0;
        u32 CurrentPresentationHash = 0;
        bool FullProductKeyMatch = true;
    };

    WholeSceneRenderTrace WholeSceneTrace;
    WholeSceneDebugPoisonState WholeSceneDebugPoison;
    u32 WholeSceneCurrentFramePartialComposites;
    u32 WholeScenePreviousFramePartialComposites;
    u32 WholeSceneHybridFragmentationGuardFrames;
    u32 WholeSceneCurrentFragmentationGuardFrames;
    u32 WholeSceneCaptureBackedHandoffGuardFrames;
    bool WholeSceneHybridFragmentationGuardTripped;
    bool WholeSceneCurrentFragmentationGuardTripped;
    bool WholeSceneFullFrameFinalizerUnsafeFrame;
    u32 WholeSceneNativeChunkAccumulationPasses;
    u32 WholeSceneFullFrameFinalizerPasses;
    u32 WholeSceneNativeProductValidRows;
    bool WholeSceneNativeProductsFrameComplete;
    bool WholeSceneNativeProductRowValid[192];
    bool WholeSceneNativeProductEpochValid;
    bool WholeSceneNativeProductEligibilityInitialized;
    WholeSceneScaleEligibility WholeSceneNativeProductFrameEligibility;
    bool WholeSceneNativeProductPathInitialized;
    WholeSceneRenderPath WholeSceneNativeProductFramePath;
    bool WholeSceneNativeProductFinalizerPathSeen;
    bool WholeSceneOverlayEndpointsValid;
    GLuint WholeSceneOverlayEndpointSourceTex;
    bool CaptureBackedHandoff3DValid[kCaptureBackedHandoffRouteSlots];

    enum class CaptureBackedHandoffPhase : u8
    {
        None = 0,
        Live3D = 1,
        CapturedBitmap = 2,
        Other = 3,
    };

    enum class CaptureBackedHandoffReuseReason : u8
    {
        None = 0,
        UpdatedLive3D = 1,
        ExactKeyMatch = 2,
        AllowedLiveToCapturePair = 3,
        RejectedNoSnapshot = 4,
        RejectedScreenSwapChanged = 5,
        RejectedEngineChanged = 6,
        RejectedPhysicalScreenChanged = 7,
        RejectedRouteChanged = 8,
        RejectedPhaseNotEquivalent = 9,
        RejectedEpochChanged = 10,
        RejectedYRangeChanged = 11,
        RejectedUnstableLivePhase = 12,
        UsedCaptureEventBackground = 13,
        UsedCaptureEventFullProduct = 14,
    };

    struct CaptureBackedHandoffRouteKey
    {
        u8 Engine = 0;
        bool ScreenSwap = false;
        bool EngineFinalTop = false;
        bool EngineFinalBottom = false;
        u32 DisplayMode = 0;
        u32 BGMode = 0;
        u32 LayerEnable = 0;
        u32 VisibleBitmapMask = 0;
        u32 BGUploadRows = 0;
        u32 FrameSerial = 0;
        int YStart = 0;
        int YEnd = 192;
        CaptureBackedHandoffPhase Phase = CaptureBackedHandoffPhase::None;
    };

    CaptureBackedHandoffRouteKey CaptureBackedHandoffCurrentKey;
    CaptureBackedHandoffRouteKey CaptureBackedHandoffLatchedKey[kCaptureBackedHandoffRouteSlots];
    CaptureBackedHandoffReuseReason CaptureBackedHandoffReuseDecision;
    u32 CaptureBackedHandoffFrameSerial;
    bool CaptureBackedHandoffBackgroundUpdated;
    bool CaptureBackedHandoffRouteHasCapturedPhase[kCaptureBackedHandoffRouteSlots];
    u8 CaptureBackedHandoffCurrentSlot;

    struct WholeSceneUpdatePhaseTiming
    {
        u64 TotalUS = 0;
        u64 MaxUS = 0;
        u32 Count = 0;
    };

    struct WholeSceneUpdateTimingState
    {
        WholeSceneUpdatePhaseTiming StateDiff;
        WholeSceneUpdatePhaseTiming VRAMFlatten;
        WholeSceneUpdatePhaseTiming LayerDirty;
        WholeSceneUpdatePhaseTiming Classify;
        WholeSceneUpdatePhaseTiming SpriteRender;
        WholeSceneUpdatePhaseTiming PartialComposite;
        WholeSceneUpdatePhaseTiming RegisterCache;
        WholeSceneUpdatePhaseTiming BGUpload;
        WholeSceneUpdatePhaseTiming BGPaletteUpload;
        WholeSceneUpdatePhaseTiming LayerPrerender;
        WholeSceneUpdatePhaseTiming OBJPrerender;
        WholeSceneUpdatePhaseTiming VBlankSpriteRender;
        WholeSceneUpdatePhaseTiming VBlankComposite;
        WholeSceneUpdatePhaseTiming NativePrepass;
        WholeSceneUpdatePhaseTiming NativeExactFinal;
        WholeSceneUpdatePhaseTiming OverlayBlackExactFinal;
        WholeSceneUpdatePhaseTiming OverlayWhiteExactFinal;
        WholeSceneUpdatePhaseTiming OverlayTrueExactFinal;
        WholeSceneUpdatePhaseTiming OverlayEndpoint;
        WholeSceneUpdatePhaseTiming Hybrid2DBaseCandidate;
        WholeSceneUpdatePhaseTiming HybridLegacyCandidate;
        WholeSceneUpdatePhaseTiming HybridForegroundCandidate;
        WholeSceneUpdatePhaseTiming FinalizerUpscale;
        WholeSceneUpdatePhaseTiming FinalizerComposite;
    } WholeSceneUpdateTiming;

    static constexpr int kWholeSceneDebugRangeRecordLimit = 16;

    struct WholeSceneUpdateDebugTrace
    {
        u32 LayerDirtyEvents = 0;
        u32 LayerDirtyMask = 0;
        u32 LayerDirtyOverflow = 0;
        int LayerDirtyLine[kWholeSceneDebugRangeRecordLimit] = {};
        u8 LayerDirtyEventMask[kWholeSceneDebugRangeRecordLimit] = {};
        u32 RegisterLayerDirtyMask = 0;
        u32 VRAMLayerDirtyMask = 0;
        u32 PaletteLayerDirtyMask = 0;
        u32 DeferredLayerDirtyMask = 0;
        u32 InactiveDeferredLayerDirtyMask = 0;
        u32 CoveredBitmapMask = 0;
        u32 ContributingLayerMask = 0;

        u32 StateDirtyEvents = 0;
        u32 StateDirtyReasonMask = 0;
        u32 StateDirtyDispCntDiff = 0;
        u32 StateDirtyLayerEnableDiff = 0;
        u16 StateDirtyBGCntDiff[4] = {};
        u32 StateDirtyMiscDiffMask = 0;

        u32 FullFrameUnsafeEvents = 0;
        u32 FullFrameUnsafeReasonMask = 0;
        int FullFrameUnsafeFirstLine = -1;
        int FullFrameUnsafeLastLine = -1;
        u32 FullFrameUnsafeLayerMask = 0;
        u32 FullFrameUnsafeDispCntDiff = 0;
        u32 FullFrameUnsafeLayerEnableDiff = 0;
        u16 FullFrameUnsafeBGCntDiff[4] = {};
        u32 FullFrameUnsafeMiscDiffMask = 0;

        u32 BGUploadCalls = 0;
        u32 BGUploadRangeCount = 0;
        u32 BGUploadRangeOverflow = 0;
        u32 BGUploadRows = 0;
        int BGUploadFirstRow = -1;
        int BGUploadLastRow = -1;
        int BGUploadLine[kWholeSceneDebugRangeRecordLimit] = {};
        int BGUploadStartRow[kWholeSceneDebugRangeRecordLimit] = {};
        int BGUploadEndRow[kWholeSceneDebugRangeRecordLimit] = {};

        u32 LayerPrerenderCalls = 0;
        u32 LayerPrerenderMask = 0;
        u32 LayerPrerenderBitmapMask = 0;
        u32 LayerPrerenderOverflow = 0;
        int LayerPrerenderLine[kWholeSceneDebugRangeRecordLimit] = {};
        u8 LayerPrerenderEventMask[kWholeSceneDebugRangeRecordLimit] = {};
        u32 VisibleBitmapDirtyMask = 0;
        u32 VisibleBitmapVRAMDirtyMask = 0;
        u32 VisibleBitmapDirtyBeforeLineMask = 0;
        u32 VisibleBitmapDirtyAfterLineMask = 0;
        u32 VisibleBitmapDirtyCrossesLineMask = 0;
        u32 VisibleBitmapRowLimitedPrerenderMask = 0;
        u32 VisibleBitmapCoveredDirtyDeferredMask = 0;
        int VisibleBitmapDirtyFirstRow = -1;
        int VisibleBitmapDirtyLastRow = -1;
        int VisibleBitmapLayerFirstRow[4] = {-1, -1, -1, -1};
        int VisibleBitmapLayerLastRow[4] = {-1, -1, -1, -1};
        int VisibleBitmapRowLimitedFirstRow[4] = {-1, -1, -1, -1};
        int VisibleBitmapRowLimitedLastRow[4] = {-1, -1, -1, -1};

        u32 PartialCompositeRangeCount = 0;
        u32 PartialCompositeRangeOverflow = 0;
        int PartialCompositeStart[kWholeSceneDebugRangeRecordLimit] = {};
        int PartialCompositeEnd[kWholeSceneDebugRangeRecordLimit] = {};
    };

    WholeSceneUpdateDebugTrace WholeSceneCurrentUpdateDebugTrace;
    WholeSceneUpdateDebugTrace WholeScenePreviousUpdateDebugTrace;

    // std140 compliant config struct for the layer shader
    struct sLayerConfig
    {
        u32 uVRAMMask;
        u32 __pad0[3];
        struct sBGConfig
        {
            u32 Size[2];
            u32 Type;
            u32 PalOffset;
            u32 TileOffset;
            u32 MapOffset;
            u32 Clamp;
            u32 __pad0[1];
        } uBGConfig[4];
    } LayerConfig;

    struct sSpriteConfig
    {
        u32 uVRAMMask;
        u32 __pad0[3];
        s32 uRotscale[32][4];
        struct sOAM
        {
            s32 Position[2];
            s32 Flip[2];
            s32 Size[2];
            s32 BoundSize[2];
            u32 OBJMode;
            u32 Type;
            u32 PalOffset;
            u32 TileOffset;
            u32 TileStride;
            u32 Rotscale;
            u32 BGPrio;
            u32 Mosaic;
        } uOAM[128];
    } SpriteConfig;
    int NumSprites;
    bool SpriteUseMosaic;

    struct sScanlineConfig
    {
        struct sScanline
        {
            s32 BGOffset[4][4];     // really [4][2]
            s32 BGRotscale[2][4];
            u32 BackColor;          // 96
            u32 WinRegs;            // 100
            u32 WinMask;            // 104
            u32 __pad0[1];
            s32 WinPos[4];
            u32 BGMosaicEnable[4];
            s32 MosaicSize[4];
        } uScanline[192];
    } ScanlineConfig;

    struct sSpriteScanlineConfig
    {
        s32 uMosaicLine[192];
    } SpriteScanlineConfig;

    struct sCompositorConfig
    {
        u32 uBGPrio[4];
        u32 uEnableOBJ;
        u32 uEnable3D;
        u32 uBlendCnt;
        u32 uBlendEffect;
        u32 uBlendCoef[4];
    } CompositorConfig;

    int LastLine;

    bool UnitEnabled;

    u32 DispCnt;
    u8 LayerEnable;
    u8 OBJEnable;
    u8 ForcedBlank;
    u16 BGCnt[4];
    u16 BlendCnt;
    u8 EVA, EVB, EVY;

    u32 BGVRAMRange[4][4];

    bool LayerConfigDirty;
    u8 DeferredLayerPrerenderDirty;
    int DeferredLayerPrerenderFirstRow[4];
    int DeferredLayerPrerenderLastRow[4];

    int LastSpriteLine;
    u16 OAM[512];

    u32 SpriteDispCnt;
    bool SpriteConfigDirty;
    bool SpriteDirty;

    u16 TempPalBuffer[256 * (1 + (4*16))];

    bool IsScreenOn();
    std::string DescribeWholeSceneScaleState() const;
    void AppendWholeSceneModeStatus(std::string& status) const;
    void AppendWholeSceneRenderTrace(std::string& status) const;
    void AppendWholeSceneDisplayStateTrace(std::string& status) const;
    void AppendWholeSceneBitmapFMVTrace(std::string& status) const;
    void AppendWholeSceneTextureStatsTrace(std::string& status) const;
    void AddWholeSceneUpdateTiming(WholeSceneUpdatePhaseTiming& phase, u64 elapsedUS);
    void AppendWholeSceneUpdateTimingCSVHeader(std::string& header, const char* prefix) const;
    void AppendWholeSceneUpdateTimingCSVRow(std::string& row) const;
    bool IsWholeSceneHybridFragmentationGuardActive() const;
    bool IsWholeSceneCurrentFragmentationGuardActive() const;
    bool IsWholeSceneCaptureBackedHandoffGuardActive() const;
    bool IsWholeSceneCaptureBackedHandoffCandidate() const;
    bool ShouldUseWholeSceneCaptureBackedHandoffForRange(int ystart, int yend) const;
    void UpdateWholeSceneCaptureBackedHandoffGuard();
    CaptureBackedHandoffPhase CurrentCaptureBackedHandoffPhase() const;
    CaptureBackedHandoffRouteKey BuildCaptureBackedHandoffRouteKey(int ystart, int yend) const;
    int CaptureBackedHandoffRouteSlot(const CaptureBackedHandoffRouteKey& key) const;
    bool IsStableCaptureBackedHandoffLiveUpdate(const CaptureBackedHandoffRouteKey& key) const;
    bool CanReuseCaptureBackedHandoffSnapshot(const CaptureBackedHandoffRouteKey& key,
                                              int slot,
                                              CaptureBackedHandoffReuseReason& reason) const;
    void InvalidateCaptureBackedHandoffSnapshot(int slot = -1);
    int VisibleSingleHighResCaptureBank() const;
    GLuint VisibleHighResCaptureBackgroundTex() const;
    GLuint VisibleHighResCaptureFullTex() const;
    u32 VisibleFullDisplayCaptureBGLayerMask(bool accept3DSourceA) const;
    u32 VisibleSourceAOnlyFullDisplayCaptureBGLayerMask() const;
    u32 VisibleFullDisplayCaptureFromSourceABGLayerMask() const;
    bool HasOnlyFullDisplayCaptureFromSourceABGLayers() const;
    bool CanUseWholeSceneCaptureOnlyHighResPath() const;
    bool IsWholeSceneStreamingBitmapFragmentationCandidate() const;
    bool IsBitmapLikeBGLayer(int layer) const;
    bool IsFullScreenBitmapBGLayer(int layer) const;
    bool DirectColorBitmapLayerCoversRange(int layer, int ystart, int yend, const u8* bgvram, u32 bgvrammask) const;
    u8 CoveredByOpaqueBitmapBGLayerMask(int ystart, int yend, const u8* bgvram, u32 bgvrammask) const;
    u32 VisibleBitmapBGLayerMask() const;
    void RecordWholeSceneLayerDirty(int line, u8 layerPreDirty);
    void RecordWholeSceneBGUploadCall();
    void RecordWholeSceneBGUploadRange(int line, int startRow, int endRow);
    void RecordWholeSceneLayerPrerender(int line, u8 layerPreDirty);
    bool DirtyBitmapSourceRowsForLayer(int layer,
                                       const NonStupidBitField<1024>& bgDirty,
                                       int& firstRow,
                                       int& lastRow) const;
    bool VisibleBitmapDirtyRowsCoveredByOpaqueUpperLayer(int layer,
                                                         int firstRow,
                                                         int lastRow,
                                                         const u8* bgvram,
                                                         u32 bgvrammask) const;
    void MarkLayerPrerenderDeferred(int layer, int firstRow = -1, int lastRow = -1);
    void ClearLayerPrerenderDeferred(u8 layerMask);
    void RecordWholeSceneVisibleBitmapDirtyRows(int layer, int line, int firstRow, int lastRow);
    void UpdateCachedRegistersAndLayerConfig(u8 layerPreDirty);
    void UploadBGVRAM(NonStupidBitField<1024>& bgDirty, int line);
    void UploadBGPalette(u32 paletteDirty, NonStupidBitField<64>& bgExtPalDirty);
    void PrerenderDirtyLayers(u8 layerPreDirty,
                              u8 rowLimitedBitmapMask,
                              const int rowLimitedFirstRow[4],
                              const int rowLimitedLastRow[4],
                              int line);
    bool ShouldArmWholeSceneFinalNativeFragmentationGuard(u32 partialComposites) const;
    bool ShouldArmWholeSceneCurrentFragmentationGuard(u32 partialComposites) const;
    void CountWholeScenePartialComposite(int ystart, int yend);
    void ResetWholeSceneNativeProductTracking();
    void UpdateWholeSceneTraceFrameSplitState();
    bool WholeSceneRenderPathUsesFullFrameFinalizer(WholeSceneRenderPath path) const;
    void RecordWholeSceneNativeProductEligibility(WholeSceneScaleEligibility eligibility);
    void RecordWholeSceneNativeProductRenderPath(WholeSceneRenderPath path, int ystart, int yend);
    bool CanFinalizeWholeSceneNativeProducts() const;
    void RecordWholeSceneNativeProductChunk(int ystart, int yend);
    bool AreWholeSceneNativeProductsComplete() const;
    bool ShouldUseCompositorExactOverlayEndpoints() const;
    void ResetWholeSceneRenderTrace();
    void RecordWholeSceneRenderTrace(WholeSceneRenderPath path,
                                     int ystart,
                                     int yend,
                                     bool highRes3D = false,
                                     bool linear3D = false,
                                     bool resolve3D = false,
                                     GLuint nativeStage3DTex = 0,
                                     bool hybridFragmentationFallback = false,
                                     bool currentFragmentationFallback = false);
    void RenderScreenWholeSceneFinalizeFinalUpscaleFullFrame();
    void RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(bool conservativeHybrid,
                                                               int ystart = 0,
                                                               int yend = 192);
    WholeSceneScaleEligibility ClassifyWholeSceneScalePath() const;
    bool CanScaleMainEngineVRAMDisplayCaptureSourceA(u32 dispmode) const;
    bool HasOnlyCaptureBackedOBJPresentation() const;
    bool HasFullScreenSourceACaptureBackedOBJ() const;
    bool CanUseWholeSceneScalePath() const;
    bool CanUseWholeSceneLegacyPath() const;
    bool CanUseWholeSceneHighResPath() const;
    bool CanUseWholeSceneOverlayOperatorPath() const;
    bool CanUseWholeSceneFinalUpscalePath() const;
    bool CanUsePhysicalFinalPostprocessNativeInputPath() const;
    bool CanUseWholeSceneHybridCleanLegacyCandidatePath() const;
    bool CanUseWholeSceneSplitLegacyFallbackPath() const;
    bool CanUseWholeSceneFullFrameFinalizerPath() const;
    bool CanUseWholeSceneForegroundOverlayPath() const;
    bool CanUseWholeSceneArtCNNPath() const;
    bool CanUseWholeSceneNNEDI3Path() const;
    bool CanUseWholeSceneXBRZPath() const;
    int WholeSceneHighResLayerFilterMode() const;
    bool WholeSceneHighResLayerFilterNoWrap() const;
    int WholeSceneHighResSpriteFilterMode() const;
    int WholeSceneArtCNNModelIndex() const;

    void UpdateAndRender(int line);

    void UpdateScanlineConfig(int line);
    void UpdateLayerConfig();
    void UpdateOAM(int ystart, int yend);
    void UpdateCompositorConfig();

    void PrerenderSprites();
    void PrerenderLayer(int layer);
    void PrerenderLayerRows(int layer, int firstRow, int lastRow);

    void DoRenderSprites(int line);
    void DoRenderSpritesNative(int line);
    void RenderSprites(bool window, int ystart, int yend);

    void RenderCompositorPass(GLuint outputFB, GLuint objLayerTex,
                              int viewportW, int viewportH,
                              int ystart, int yend,
                              int compositorScale,
                              int layerFilterMode,
                              bool layerFilterNoWrap,
                              bool objNativeResolution,
                              bool debugTint,
                              GLuint direct3DTex = 0,
                              GLuint direct3DCoverageTex = 0,
                              bool forceOBJDisabled = false,
                              bool preserveCompositorConfig = false);
    void RenderNativePrepass(int ystart, int yend);
    void RenderScreenPhysicalFinalPostprocessNativeInput(int ystart, int yend);
    void PrepareFinalUpscaleNative3DInput(GLuint& direct3DTex,
                                          GLuint& direct3DCoverageTex,
                                          bool& highRes3D,
                                          bool& linear3D);
    void RenderNativeLayerDebugView(int debugLayer) const;
    void RenderNativeExactFinal(int ystart, int yend, bool debugTint = false, GLuint direct3DTex = 0, GLuint direct3DCoverageTex = 0);
    void RenderNativeExactFinalToTexture(GLuint targetTex, int ystart, int yend, bool debugTint = false, GLuint direct3DTex = 0, GLuint direct3DCoverageTex = 0, bool preserveCompositorConfig = false);
    void RenderNativeResolvedExactFinalToTexture(GLuint targetTex, GLuint direct3DTex);
    void RenderNativeUpscale(int ystart, int yend);
    void RenderNativeFinalUpscale(GLuint sourceTex);
    void RenderNativeFinalUpscaleToTexture(GLuint sourceTex, GLuint targetTex);
    bool CanRenderCurrentOverlayForCaptureSource(u32 sourceLayerEnable,
                                                 u32 sourceBGMode,
                                                 u32 sourceVisibleBitmapMask,
                                                 bool sourceDirect3DVisible) const;
    u32 CapturePresentationHash() const;
    bool CanUseCaptureEpochBackgroundForLiveOverlay(int ystart, int yend, int& routeSlot) const;
    bool RenderCurrentOverlayOverHighResBackgroundToTexture(GLuint targetTex,
                                                            GLuint highResBackgroundTex,
                                                            int ystart,
                                                            int yend);
    void RenderNativeResolveToTexture(GLuint targetTex,
                                      int ystart,
                                      int yend,
                                      bool debugTint,
                                      bool useExactFinalFallback,
                                      bool useForegroundOverlay);
    GLuint ResolveDirect3DToNative(GLuint sourceTex = 0);
    bool UpdateCaptureBackedHandoff3DSnapshot(GLuint sourceTex);
    void PoisonWholeSceneDebugTexture(GLuint texture, int width, int height, bool forceAlphaOpaque);
    void EnsureWholeSceneOverlayEndpoints(GLuint nativeDirect3DTex);
    void RenderOverlay3DEndpoint(GLuint targetTex, GLuint nativeDirect3DTex, bool whiteEndpoint);
    void RenderOverlayComposite(GLuint overlayBlackTex,
                                GLuint overlayWhiteTex,
                                GLuint direct3DTex,
                                bool conservativeHybrid = false,
                                GLuint hybridForegroundTex = 0,
                                GLuint hybridNativeFallbackTex = 0,
                                GLuint hybrid2DBaseTex = 0,
                                GLuint hybridLegacyCandidateTex = 0,
                                bool useHybridLegacyCandidate = false,
                                GLuint nativeRole3DTex = 0,
                                GLuint targetTex = 0,
                                bool forceDebugTint = false,
                                int hybridDebugMode = 0,
                                int ystart = 0,
                                int yend = 192,
                                bool direct3DPresentationSpace = false);
    void RenderOverlayDebugTexture(GLuint targetTex,
                                   int width,
                                   int height,
                                   GLuint overlayBlackTex,
                                   GLuint overlayWhiteTex,
                                   GLuint direct3DTex,
                                   GLuint nativeFinalTex,
                                   int debugMode);
    void RenderNativeMetaCoverage(int ystart, int yend);
    void RenderArtCNNPassToTexture(GLuint shader, GLuint targetTex, int width, int height, GLuint source0, GLuint source1);
    void RenderArtCNNPass(GLuint shader, GLuint outputFB, int width, int height, GLuint source0, GLuint source1);
    void RenderArtCNN2x(int modelIndex, GLuint sourceTex, GLuint targetTex);
    void RenderArtCNN2xGuarded(int modelIndex, GLuint sourceTex, GLuint targetTex, bool secondLayer);
    void RenderArtCNNSpline36(GLuint sourceTex, GLuint targetTex, int width, int height, float sourceShiftX = 0.0f, float sourceShiftY = 0.0f);
    void RenderNNEDI32x(GLuint sourceTex, GLuint targetTex);
    void RenderNNEDI32xGuarded(GLuint sourceTex, GLuint targetTex, bool secondLayer);
    void RenderXBRZ(GLuint sourceTex, GLuint targetTex);
    void RenderXBRZGuarded(GLuint sourceTex, GLuint targetTex, bool secondLayer);
    void RenderNativeBoundaryGuard(GLuint scaledTex, GLuint nativeTex, GLuint targetTex, bool secondLayer);
    void RenderNativeResolve(int ystart, int yend);
    void RenderScreenCurrent(int ystart, int yend, bool currentFragmentationFallback = false);
    void RenderScreenWholeSceneLegacy(int ystart, int yend, bool cleanHybridCandidate = false);
    void RenderScreenWholeSceneHighRes(int ystart, int yend);
    void RenderScreenWholeSceneSourceACaptureReplacement(int ystart, int yend);
    SourceACaptureReplacementChoice ChooseSourceACaptureReplacement(int ystart, int yend);
    void BlitWholeSceneSourceAReplacement(GLuint sourceTex, int ystart, int yend);
    void RenderScreenWholeSceneCaptureEpochOverlay(int ystart, int yend);
    void RenderScreenWholeSceneFinalUpscale(int ystart, int yend, bool hybridFragmentationFallback = false);
    void RenderScreenWholeSceneOverlayOperator(int ystart, int yend);
    void RenderScreenWholeSceneCaptureBackedHandoff(int ystart, int yend);
    void RenderScreenWholeSceneFinalizeFullFrame();
    void RenderScreenWholeScene(int ystart, int yend);
    void RenderScreen(int ystart, int yend);
};

}
