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
#include "WholeSceneScalePolicy.h"

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
    GLint OverlayCompositeDirect3DPresentationSpaceULoc;
    GLuint OverlayHybridCompositeShader;
    GLint OverlayHybridCompositeScaleULoc;
    GLint OverlayHybridCompositeDebugTintULoc;
    GLint OverlayHybridCompositeLegacyUnderlayULoc;
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
    GLuint MasterBrightnessShader;
    GLint MasterBrightnessModeULoc;
    GLint MasterBrightnessFactorULoc;
    GLint NativeResolveScaleULoc;
    GLint NativeResolveUseExactFinalFallbackULoc;
    GLint NativeResolveUseForegroundOverlayULoc;
    GLint NativeResolveDebugTintULoc;
    GLint NativeResolveNativeExactOutputULoc;
    GLuint RGBAToYUVAShader;
    GLuint ArtCNNConvShaders[RendererSettings::GLArtCNNModelCount][7];
    GLuint ArtCNNDepthToSpaceShaders[RendererSettings::GLArtCNNModelCount];
    GLuint Spline36Shader;
    GLuint ArtCNNYUVAToRGBA2xShader;
    GLuint AlphaReplaceShader;
    GLuint NNEDI3VerticalComputeShader {};
    GLuint NNEDI3HorizontalComputeShader {};
    GLuint XBRZPreprocessShader;
    GLuint XBRZFreescaleShader;
    GLRenderer2D* CuNNyShaderOwner;
    GLRenderer2D* ArtCNNShaderOwner;
    GLRenderer2D* NNEDI3ComputeShaderOwner;
    GLuint CuNNyInShaders[RendererSettings::GLCuNNyModelCount] {};
    GLuint CuNNyConvShaders[RendererSettings::GLCuNNyModelCount][RendererSettings::GLCuNNyMaxConvPasses] {};
    GLuint CuNNyOutShaders[RendererSettings::GLCuNNyModelCount] {};
    bool CuNNyProgramsReady = false;
    bool CuNNyProgramsFailed = false;
    bool ArtCNNComputeProgramsReady = false;
    bool ArtCNNComputeProgramsFailed = false;
    bool NNEDI3ComputeProgramsReady = false;
    bool NNEDI3ComputeProgramsFailed = false;

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
    GLuint NativeOverlayBlackCapture128Tex;
    GLuint NativeOverlayWhiteCapture128Tex;
    GLuint NativeOverlayBlackCapture256Tex;
    GLuint NativeOverlayWhiteCapture256Tex;
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

    struct CaptureBackedRouteGLResources
    {
        GLuint Handoff3DFB = 0;
        GLuint Handoff3DTex = 0;
        GLuint ProductFB = 0;
        GLuint ProductTex = 0;
        GLuint EventProductFB = 0;
        GLuint EventProductTex = 0;
    };

    CaptureBackedRouteGLResources CaptureBackedRouteGL[kCaptureBackedHandoffRouteSlots];

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
    GLuint NNEDI3Luma4xTex;
    GLuint ArtCNNYUVA2xTex;
    GLuint ArtCNNRGBA2xTex;
    GLuint ArtCNNOutputFB;
    GLuint NNEDI3VerticalTex;
    GLuint XBRZInfoTex;
    GLuint XBRZInfoFB;
    GLuint CuNNyWorkTex[2] {};
    u32 CuNNyWorkTexWidth[2] {};
    u32 CuNNyWorkTexHeight[2] {};

    using WholeSceneScaleEligibility = ::melonDS::WholeSceneScaleEligibility;
    using WholeSceneNative3DSource = ::melonDS::WholeSceneNative3DSource;
    using WholeSceneRenderPath = ::melonDS::WholeSceneRenderPath;
    using WholeSceneCurrentPathReason = ::melonDS::WholeSceneCurrentPathReason;
    using WholeSceneOverlayEndpointFinalMode = ::melonDS::WholeSceneOverlayEndpointFinalMode;
    using SourceACaptureReplacementMode = ::melonDS::SourceACaptureReplacementMode;
    using SourceAProductChoiceReason = ::melonDS::SourceAProductChoiceReason;
    using VisibleOBJCaptureDebug = ::melonDS::VisibleOBJCaptureDebug;
    using WholeSceneRenderTrace = ::melonDS::WholeSceneRenderTrace;
    using WholeSceneDebugPoisonState = ::melonDS::WholeSceneDebugPoisonState;
    using WholeSceneUpdatePhaseTiming = ::melonDS::WholeSceneUpdatePhaseTiming;
    using WholeSceneUpdateTimingState = ::melonDS::WholeSceneUpdateTimingState;
    using WholeSceneUpdateDebugTrace = ::melonDS::WholeSceneUpdateDebugTrace;

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

    using WholeSceneCaptureAuthority = ::melonDS::WholeSceneCaptureAuthority;
    using WholeSceneCaptureBackedPlanRole = ::melonDS::WholeSceneCaptureBackedPlanRole;
    using WholeSceneCaptureRequestKind = ::melonDS::WholeSceneCaptureRequestKind;
    using WholeSceneCaptureProductKind = ::melonDS::WholeSceneCaptureProductKind;
    using WholeSceneCaptureEffectOwner = ::melonDS::WholeSceneCaptureEffectOwner;
    using WholeSceneCaptureEffectAction = ::melonDS::WholeSceneCaptureEffectAction;
    using WholeSceneCaptureProofKind = ::melonDS::WholeSceneCaptureProofKind;
    using WholeSceneCaptureRenderAction = ::melonDS::WholeSceneCaptureRenderAction;
    using WholeSceneCaptureBackedPlanKind = ::melonDS::WholeSceneCaptureBackedPlanKind;
    using WholeSceneCaptureBackedPlanStage = ::melonDS::WholeSceneCaptureBackedPlanStage;
    using SourceABackgroundSource = ::melonDS::SourceABackgroundSource;
    using CaptureBackedRouteProductLookupSource = ::melonDS::CaptureBackedRouteProductLookupSource;
    using CaptureBackedHandoffPhase = ::melonDS::CaptureBackedHandoffPhase;
    using CaptureBackedHandoffReuseReason = ::melonDS::CaptureBackedHandoffReuseReason;
    using CaptureBackedRoutePresentationMode = ::melonDS::CaptureBackedRoutePresentationMode;
    using CaptureBackedHandoffRouteKey = ::melonDS::CaptureBackedHandoffRouteKey;
    using CaptureBackedRoutePresentationState = ::melonDS::CaptureBackedRoutePresentationState;
    using CaptureBackedRouteProductIdentity = ::melonDS::CaptureBackedRouteProductIdentity;
    using CaptureBackedRouteProductState = ::melonDS::CaptureBackedRouteProductState;
    using CaptureBackedRouteEventProductState = ::melonDS::CaptureBackedRouteEventProductState;
    using CaptureBackedRoutePendingEventState = ::melonDS::CaptureBackedRoutePendingEventState;
    using CaptureBackedRouteProductEventQuery = ::melonDS::CaptureBackedRouteProductEventQuery;
    using CaptureBackedRouteProductStateQuery = ::melonDS::CaptureBackedRouteProductStateQuery;
    using SourceACaptureResolutionKind = ::melonDS::SourceACaptureResolutionKind;
    using SourceACaptureResolutionInputs = ::melonDS::SourceACaptureResolutionInputs;
    using WholeSceneCaptureRequest = ::melonDS::WholeSceneCaptureRequest;
    using WholeSceneCaptureProductRef = ::melonDS::WholeSceneCaptureProductRef;
    using WholeSceneCaptureBackedPlan = ::melonDS::WholeSceneCaptureBackedPlan;
    using WholeSceneCapturePolicyResult = ::melonDS::WholeSceneCapturePolicyResult;
    using SourceACaptureResolution = ::melonDS::SourceACaptureResolution;
    using HandoffCaptureResolution = ::melonDS::HandoffCaptureResolution;

    struct SourceACaptureReplacementChoice
    {
        // Keep product/overlay selection separate from the GL blit/composite path.
        GLuint FullProductTex = 0;
        GLuint BackgroundTex = 0;
        int CaptureBank = -1;
        bool MainEngineCapturedBGOnly = false;
        bool SubEngineCapturedSourceAOnly = false;
        bool SubEngineCaptureBackedBGOnly = false;
        bool SubEngineCapturedOBJOnly = false;
        bool CanUseCurrentOverlay = false;
        GLRenderer2D* MainRenderer = nullptr;
        GLuint RouteProductTex = 0;
        int RouteSlot = -1;
        u64 BackgroundEpochSerial = 0;
        u64 BackgroundSource3DSerial = 0;
        u32 BackgroundSource3DSceneHash = 0;
        u64 RouteProductBackgroundEpochSerial = 0;
        u64 RouteProductSource3DSerial = 0;
        u32 RouteProductSource3DSceneHash = 0;
        u64 RouteProductCapturedEventSerial = 0;
        u32 RouteProductCaptureBank = 0xFFFFFFFFu;
        u32 RouteProductCapturePresentationHash = 0;
        u32 RouteProductCurrentPresentationHash = 0;
        u32 RouteProductStableFrames = 0;
        WholeSceneCaptureProductPresentationClass RouteProductPresentationClass =
            WholeSceneCaptureProductPresentationClass::None;
        u16 RouteProductStoredMasterBrightness = 0;
        bool RouteProductHasStoredEffectState = false;
        bool RouteProductLookupAttempted = false;
        bool RouteProductLookupSuccess = false;
        u32 RouteProductLookupResultSource = 0;
        int RouteProductLookupSlot = -1;
        u64 RouteProductLookupEventSerial = 0;
        u32 RouteProductLookupCaptureBank = 0xFFFFFFFFu;
        u32 RouteProductLookupCapturePresentationHash = 0;
        u64 RouteProductLookupSource3DSerial = 0;
        u32 RouteProductLookupSource3DSceneHash = 0;
        bool RouteProductLookupEventProductValid = false;
        u64 RouteProductLookupEventProductCapturedSerial = 0;
        u32 RouteProductLookupEventProductCaptureBank = 0xFFFFFFFFu;
        u32 RouteProductLookupEventProductCurrentPresentationHash = 0;
        u64 RouteProductLookupEventProductSource3DSerial = 0;
        u32 RouteProductLookupEventProductSource3DSceneHash = 0;
        bool RouteProductLookupProductValid = false;
        u64 RouteProductLookupProductCapturedSerial = 0;
        u32 RouteProductLookupProductCaptureBank = 0xFFFFFFFFu;
        u32 RouteProductLookupProductCurrentPresentationHash = 0;
        u64 RouteProductLookupProductSource3DSerial = 0;
        u32 RouteProductLookupProductSource3DSceneHash = 0;
        u32 CapturePresentationHash = 0;
        u32 CurrentPresentationHash = 0;
        bool FullProductKeyMatch = true;
        WholeSceneCaptureProductKind RouteProductKind = WholeSceneCaptureProductKind::None;
        WholeSceneCaptureProofKind RouteProductProof = WholeSceneCaptureProofKind::None;
        int FullProductCaptureBank = -1;
        int FullProductTexID = 0;
        bool FullProductEventValid = false;
        u64 FullProductEventSerial = 0;
        u64 FullProductEventSource3DSerial = 0;
        u32 FullProductEventSource3DSceneHash = 0;
        u32 FullProductEventSourcePresentationHash = 0;
        u32 FullProductEventSourceKind = 0;
        u32 FullProductEventProductMask = 0;
        u32 FullProductEventRejectReason = 0;
        int FullProductEventDstBlock = -1;
        int FullProductEventDstOffset = -1;
        bool FullProductEventSourceOBJ = false;
        bool FullProductEventScreenSwap = false;
        bool FullProductEventMainFinalBottom = false;
        bool PreferExactFullProduct = false;
        bool PreferExactRouteProduct = false;
        bool AllowExactFullProductCapturePresentation = false;
        bool DirectFinalDisplayConsumer = false;
        bool DirectFinalBottomConsumer = false;
        bool ActiveDisplayCaptureSourceA2D = false;
        bool ActiveFullDisplayCaptureSourceA = false;
        int ActiveDisplayCaptureDstBank = -1;
        int ActiveDisplayCaptureDstOffset = -1;
    };

    struct GLCaptureProductResolution
    {
        GLuint Tex = 0;
        bool Accepted = false;
        WholeSceneCaptureProductKind ProductKind = WholeSceneCaptureProductKind::None;
        SourceABackgroundSource BackgroundSource = SourceABackgroundSource::None;
        WholeSceneCaptureRenderAction RenderAction = WholeSceneCaptureRenderAction::None;
        WholeSceneCaptureProductPresentationClass PresentationClass =
            WholeSceneCaptureProductPresentationClass::None;
        bool PresentationCompatible = false;
        bool RequiresRePresentation = false;
    };

    struct GLCaptureProductSources
    {
        GLuint RouteProductTex = 0;
        WholeSceneCaptureProductPresentationClass RouteProductPresentationClass =
            WholeSceneCaptureProductPresentationClass::None;
        u16 RouteProductStoredMasterBrightness = 0;
        bool RouteProductHasStoredEffectState = false;
        GLuint FullProductTex = 0;
        GLuint BackgroundTex = 0;
        u16 BackgroundStoredMasterBrightness = 0;
        bool BackgroundHasStoredEffectState = false;
        GLuint Direct3DTex = 0;
    };

    struct GLCaptureProductTraceIdentity
    {
        int CaptureBank = -1;
        u64 BackgroundEpochSerial = 0;
        u64 Source3DSerial = 0;
        u32 Source3DSceneHash = 0;
        u64 CaptureEventSerial = 0;
        u32 CapturePresentationHash = 0;
        u32 CurrentPresentationHash = 0;
    };

    struct HandoffBackgroundChoice
    {
        GLuint Tex = 0;
        u64 BackgroundEpochSerial = 0;
        u64 Source3DSerial = 0;
        u32 Source3DSceneHash = 0;
        u32 PresentationHash = 0;
        u16 StoredMasterBrightness = 0;
        bool HasStoredEffectState = false;
        SourceABackgroundSource Source = SourceABackgroundSource::None;
        WholeSceneCaptureAuthority Authority = WholeSceneCaptureAuthority::None;
    };

    struct HandoffBackgroundResolveResult
    {
        HandoffBackgroundChoice Background;
        bool Finished = false;
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
    u32 WholeSceneNativeProductEpochInvalidReason;
    bool WholeSceneNativeProductEligibilityInitialized;
    WholeSceneScaleEligibility WholeSceneNativeProductFrameEligibility;
    WholeSceneScaleEligibility WholeSceneNativeProductLastEligibility;
    bool WholeSceneNativeProductPathInitialized;
    WholeSceneRenderPath WholeSceneNativeProductFramePath;
    WholeSceneRenderPath WholeSceneNativeProductLastPath;
    bool WholeSceneNativeProductFinalizerPathSeen;
    bool WholeSceneOverlayEndpointsValid;
    GLuint WholeSceneOverlayEndpointSourceTex;

    struct CaptureBackedRouteProductWrite
    {
        int RouteSlot = -1;
        GLuint SourceTex = 0;
        CaptureBackedRouteProductIdentity Identity;
        int YStart = 0;
        int YEnd = 192;
        WholeSceneCaptureProductPresentationClass PresentationClass =
            WholeSceneCaptureProductPresentationClass::None;
    };

    struct CaptureBackedRouteProductLookup
    {
        GLuint Tex = 0;
        bool Valid = false;
        CaptureBackedRouteProductLookupSource Source = CaptureBackedRouteProductLookupSource::None;
        CaptureBackedRouteProductIdentity Identity;
        u64 CapturedEventSerial = 0;
        u32 StableFrames = 0;
        WholeSceneCaptureProductPresentationClass PresentationClass =
            WholeSceneCaptureProductPresentationClass::None;
        u16 StoredMasterBrightness = 0;
        bool HasStoredEffectState = false;
    };

    struct CaptureBackedRouteState
    {
        bool Handoff3DValid = false;
        bool HasCapturedPhase = false;
        CaptureBackedHandoffRouteKey HandoffLatchedKey;
        CaptureBackedRoutePresentationState Presentation;
        CaptureBackedRouteProductState Product;
        CaptureBackedRouteEventProductState EventProduct;
        CaptureBackedRoutePendingEventState PendingEvent;
    };

    struct CaptureBackedHandoffFrameState
    {
        CaptureBackedHandoffRouteKey CurrentKey;
        CaptureBackedHandoffReuseReason ReuseDecision = CaptureBackedHandoffReuseReason::None;
        u32 FrameSerial = 0;
        bool BackgroundUpdated = false;
        u8 CurrentSlot = 0;
    };

    CaptureBackedHandoffFrameState CaptureBackedHandoff;
    CaptureBackedRouteState CaptureBackedRoute[kCaptureBackedHandoffRouteSlots];

    WholeSceneUpdateTimingState WholeSceneUpdateTiming;

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
    bool IsWholeSceneHybridPresentationGuardActive(int ystart, int yend) const;
    bool CanBypassWholeSceneHybridPresentationGuardForDirect2D(int ystart, int yend) const;
    bool ShouldUseWholeSceneHybridOverlayForSuppressedDirect3DAlphaBlend() const;
    bool IsWholeSceneCaptureBackedHandoffGuardActive() const;
    bool IsWholeSceneCaptureBackedHandoffCandidate() const;
    bool ShouldUseWholeSceneCaptureBackedHandoffForRange(int ystart, int yend) const;
    void UpdateWholeSceneCaptureBackedHandoffGuard();
    CaptureBackedHandoffPhase CurrentCaptureBackedHandoffPhase() const;
    CaptureBackedHandoffRouteKey BuildCaptureBackedHandoffRouteKey(int ystart, int yend) const;
    int BeginCaptureBackedHandoffRoute(int ystart, int yend);
    int CaptureBackedHandoffRouteSlot(const CaptureBackedHandoffRouteKey& key) const;
    bool IsStableCaptureBackedHandoffLiveUpdate(const CaptureBackedHandoffRouteKey& key) const;
    bool CanReuseCaptureBackedHandoffSnapshot(const CaptureBackedHandoffRouteKey& key,
                                              int slot,
                                              CaptureBackedHandoffReuseReason& reason) const;
    void ClearCaptureBackedRouteProductState(int slot);
    void ClearCaptureBackedRouteState(int slot);
    void LatchCaptureBackedHandoffSnapshot(int slot, const CaptureBackedHandoffRouteKey& key);
    void MarkCaptureBackedRouteCapturedPhase(int slot);
    void InvalidateCaptureBackedHandoffSnapshot(int slot = -1);
    void UpdateCaptureBackedRoutePresentation(int slot,
                                              CaptureBackedRoutePresentationMode mode,
                                              u64 serial,
                                              u32 captureBank,
                                              u32 source3DSceneHash,
                                              u32 sourcePresentationHash,
                                              u32 currentOverlayPresentationHash = 0);
    void InvalidateCaptureBackedRouteProduct(int slot = -1);
    bool StoreCaptureBackedRouteProduct(const CaptureBackedRouteProductWrite& write);
    bool StoreRawCaptureBackedRouteProduct(int routeSlot,
                                           GLuint sourceTex,
                                           u64 backgroundEpochSerial,
                                           u64 source3DSerial,
                                           u32 source3DSceneHash,
                                           u32 captureBank,
                                           u32 capturePresentationHash,
                                           u32 currentOverlayPresentationHash,
                                           int ystart,
                                           int yend);
    bool StoreCurrentOverlayCaptureBackedRouteProduct(int routeSlot,
                                                      GLuint sourceTex,
                                                      u64 backgroundEpochSerial,
                                                      u64 source3DSerial,
                                                      u32 source3DSceneHash,
                                                      u32 captureBank,
                                                      u32 capturePresentationHash,
                                                      u32 currentOverlayPresentationHash,
                                                      int ystart,
                                                      int yend);
    void NoteCaptureBackedRouteProductCaptured(int slot,
                                               u64 captureEventSerial,
                                               u32 captureBank,
                                               u32 capturePresentationHash,
                                               u64 source3DSerial,
                                               u32 source3DSceneHash);
    CaptureBackedRouteProductLookup FindCaptureBackedRouteProductForEvent(
        const CaptureBackedRouteProductEventQuery& query) const;
    CaptureBackedRouteProductLookup FindCaptureBackedRouteProductForSource3DScene(
        const CaptureBackedRouteProductEventQuery& query) const;
    CaptureBackedRouteProductLookup FindCaptureBackedRouteProductForState(
        const CaptureBackedRouteProductStateQuery& query) const;
    bool TryStoreCaptureBackedRouteEventProduct(int slot);
    VisibleOBJCaptureDebug BuildVisibleOBJCaptureDebug() const;
    int VisibleSingleDisplayCaptureBank() const;
    int VisibleSingleDisplayCaptureOBJBank() const;
    int VisibleSingleHighResCaptureBank() const;
    GLuint VisibleHighResCaptureBackgroundTex() const;
    GLuint VisibleHighResCaptureFullTex() const;
    u32 VisibleFullDisplayCaptureBGLayerMask(bool accept3DSourceA) const;
    u32 VisibleSourceAOnlyFullDisplayCaptureBGLayerMask() const;
    u32 VisibleFullDisplayCaptureFromSourceABGLayerMask() const;
    bool HasOnlyFullDisplayCaptureFromSourceABGLayers() const;
    bool HasWholeSceneHighResCaptureBackedOBJReplacement(int ystart, int yend) const;
    bool CanUseWholeSceneCaptureOnlyHighResPath(int ystart, int yend) const;
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
    void SyncPendingDisplayCapturesForFlatVRAMBGs();
    bool FlatVRAMLayerHasFullWholeSceneCaptureProvenance(int layer) const;
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
    bool IsBenignMainVRAMDisplayNativeProductEligibilityTransition(
        WholeSceneScaleEligibility eligibility) const;
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
    void RecordOutputPresentationMasterBrightness(WholeSceneCaptureEffectOwner owner,
                                                  u16 masterBrightness);
    HybridSourceDecision ChooseHybridSourceDecision() const;
    void RecordWholeSceneCaptureSemantics(WholeSceneCaptureBackedPlanRole role,
                                          WholeSceneCaptureRequestKind request,
                                          WholeSceneCaptureProductKind product,
                                          WholeSceneCaptureProofKind proof,
                                          WholeSceneCaptureRenderAction action);
    static WholeSceneCaptureRequest MakeSourceAConsumerCaptureRequest(
        const SourceACaptureReplacementChoice& choice,
        WholeSceneCaptureRequestKind kind,
        int ystart,
        int yend,
        u64 captureEventSerial = 0);
    static SourceACaptureResolution MakeSourceARouteProductResolution(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    static SourceACaptureResolution MakeSourceABackgroundOverlayResolution(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    static SourceACaptureResolution MakeSourceARejectedResolution(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    static SourceACaptureResolution MakeSourceAFullProductResolution(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    static HandoffCaptureResolution MakeHandoffRouteProductResolution(
        const WholeSceneCaptureRequest& baseRequest,
        const CaptureBackedRouteProductLookup& routeProduct,
        u32 captureBank,
        u64 captureEventSerial,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        u32 currentPresentationHash);
    static HandoffCaptureResolution MakeHandoffFullProductResolution(
        const WholeSceneCaptureRequest& baseRequest,
        u32 captureBank,
        u64 captureEventSerial,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        u32 currentPresentationHash);
    static HandoffCaptureResolution MakeHandoffBackgroundOverlayResolution(
        const WholeSceneCaptureRequest& baseRequest,
        SourceABackgroundSource backgroundSource,
        WholeSceneCaptureAuthority authority,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        u32 currentPresentationHash);
    static HandoffCaptureResolution MakeHandoffHybridResolution(
        const WholeSceneCaptureRequest& baseRequest,
        SourceABackgroundSource backgroundSource,
        WholeSceneCaptureAuthority authority,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        bool hasHighResBackground);
    void RecordWholeSceneCaptureResolution(const WholeSceneCaptureRequest& request,
                                           WholeSceneCapturePolicyResult result);
    void RecordSourceACaptureResolution(const SourceACaptureResolution& resolution);
    void RecordHandoffCaptureResolution(const HandoffCaptureResolution& resolution);
    void RecordSourceACaptureChoiceDebug(const SourceACaptureReplacementChoice& choice);
    void RecordSourceABackgroundTrace(u64 requestBackgroundEpochSerial,
                                      SourceABackgroundSource effectiveBackgroundSource,
                                      u64 effectiveBackgroundEpochSerial,
                                      u32 capturePresentationHash);
    void RecordSourceACaptureProductTrace(SourceACaptureReplacementMode mode,
                                          u64 requestBackgroundEpochSerial,
                                          SourceABackgroundSource effectiveBackgroundSource,
                                          u64 effectiveBackgroundEpochSerial,
                                          u32 capturePresentationHash,
                                          u32 currentPresentationHash,
                                          SourceAProductChoiceReason productChoice,
                                          bool fullProductKeyMatch = false);
    void RecordSourceARouteProductTrace(const CaptureBackedRouteProductIdentity& identity,
                                        u64 capturedEventSerial,
                                        u32 stableFrames,
                                        WholeSceneCaptureProductPresentationClass presentationClass =
                                            WholeSceneCaptureProductPresentationClass::RawContent);
    void RecordSourceARouteProductTrace(const CaptureBackedRouteProductState& product);
    void RecordSourceARouteProductTrace(const SourceACaptureReplacementChoice& choice);
    static GLCaptureProductSources SourceACaptureProductSources(
        const SourceACaptureReplacementChoice& choice);
    static GLCaptureProductSources RouteCaptureProductSources(
        GLuint routeProductTex,
        WholeSceneCaptureProductPresentationClass presentationClass,
        u16 storedMasterBrightness,
        bool hasStoredEffectState);
    static GLCaptureProductSources FullCaptureProductSources(GLuint fullProductTex);
    static GLCaptureProductSources BackgroundCaptureProductSources(
        GLuint backgroundTex,
        u16 storedMasterBrightness = 0,
        bool hasStoredEffectState = false);
    GLCaptureProductResolution ResolveCaptureProduct(
        const WholeSceneCapturePolicyResult& result,
        const WholeSceneCaptureRequest& request,
        const GLCaptureProductSources& sources);
    void RecordChosenCaptureProductTrace(const GLCaptureProductResolution& product,
                                         const GLCaptureProductTraceIdentity& identity);
    static HandoffBackgroundChoice MakeHandoffSnapshotBackgroundChoice(GLuint texture);
    static HandoffBackgroundChoice MakeCaptureEventBackgroundChoice(
        GLuint texture,
        SourceABackgroundSource source,
        u64 backgroundEpochSerial = 0,
        u64 source3DSerial = 0,
        u32 source3DSceneHash = 0,
        u32 presentationHash = 0);
    HandoffBackgroundChoice UseActiveEpochHandoffBackground(
        int handoffSlot,
        u64 backgroundEpochSerial,
        u32 captureBank,
        u64 source3DSerial,
        u32 source3DSceneHash,
        u32 presentationHash);
    GLCaptureProductResolution RecordSourceARouteProductChoiceTrace(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    GLCaptureProductResolution RecordSourceABackgroundOverlayChoiceTrace(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    GLCaptureProductResolution RecordSourceAFullProductChoiceTrace(
        const SourceACaptureReplacementChoice& choice,
        int ystart,
        int yend);
    void RecordSourceARejectedChoiceTrace(const SourceACaptureReplacementChoice& choice,
                                          int ystart,
                                          int yend);
    GLCaptureProductResolution RecordHandoffRouteProductTrace(
        const WholeSceneCaptureRequest& baseRequest,
        const CaptureBackedRouteProductLookup& routeProduct,
        GLuint routeProductTex,
        u32 captureBank,
        u64 captureEventSerial,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        u32 currentPresentationHash,
        int ystart,
        int yend);
    GLCaptureProductResolution RecordHandoffFullProductTrace(
        const WholeSceneCaptureRequest& baseRequest,
        GLuint fullProductTex,
        u32 captureBank,
        u64 captureEventSerial,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        u32 currentPresentationHash,
        int ystart,
        int yend);
    GLCaptureProductResolution RecordHandoffBackgroundOverlayTrace(
        const WholeSceneCaptureRequest& baseRequest,
        SourceABackgroundSource backgroundSource,
        WholeSceneCaptureAuthority authority,
        GLuint backgroundTex,
        u64 backgroundEpochSerial,
        u32 capturePresentationHash,
        u32 currentPresentationHash,
        u16 storedMasterBrightness,
        bool hasStoredEffectState,
        int ystart,
        int yend);
    void RecordHandoffHybridTrace(const WholeSceneCaptureRequest& baseRequest,
                                  SourceABackgroundSource backgroundSource,
                                  WholeSceneCaptureAuthority authority,
                                  GLuint direct3DTex,
                                  bool highRes3D,
                                  u64 backgroundEpochSerial,
                                  u32 capturePresentationHash,
                                  bool hasHighResBackground,
                                  int ystart,
                                  int yend);
    GLCaptureProductResolution RecordCaptureEpochOverlayTrace(
        int routeSlot,
        u32 captureBank,
        SourceABackgroundSource backgroundSource,
        GLuint backgroundTex,
        u64 requestBackgroundEpochSerial,
        u64 routeProductBackgroundSerial,
        u32 routeProductPresentationHash,
        u32 currentPresentationHash,
        u16 storedMasterBrightness,
        bool hasStoredEffectState,
        int ystart,
        int yend);
    void RenderScreenWholeSceneFinalizeFinalUpscaleFullFrame();
    void RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(bool conservativeHybrid,
                                                               int ystart = 0,
                                                               int yend = 192);
    WholeSceneScaleEligibility ClassifyWholeSceneScalePath() const;
    bool CanScaleMainEngineVRAMDisplayCaptureSourceA(u32 dispmode) const;
    bool CanUseWholeSceneMixedSourceACaptureBGPath() const;
    bool CanUseWholeSceneMixedCaptureBackedOBJOverlayPath() const;
    bool CanUseSourceABackgroundCurrentOverlayPath() const;
    bool CanUseSourceAExactFullProductBridgePath(int ystart, int yend) const;
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
    bool CanUseWholeSceneCuNNyPath() const;
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
                              bool preserveCompositorConfig = false,
                              GLuint capture128Tex = 0,
                              GLuint capture256Tex = 0);
    void RenderNativePrepass(int ystart, int yend);
    void RenderScreenPhysicalFinalPostprocessNativeInput(int ystart, int yend);
    void PrepareFinalUpscaleNative3DInput(GLuint& direct3DTex,
                                          GLuint& direct3DCoverageTex,
                                          bool& highRes3D);
    void RenderNativeLayerDebugView(int debugLayer) const;
    void RenderNativeExactFinal(int ystart, int yend, bool debugTint = false, GLuint direct3DTex = 0, GLuint direct3DCoverageTex = 0);
    void RenderNativeExactFinalToTexture(GLuint targetTex, int ystart, int yend, bool debugTint = false, GLuint direct3DTex = 0, GLuint direct3DCoverageTex = 0, bool preserveCompositorConfig = false, GLuint capture128Tex = 0, GLuint capture256Tex = 0, bool forceOBJDisabled = false);
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
                                                            int yend,
                                                            bool applyMasterBrightness = false,
                                                            GLuint* rawCompositeTex = nullptr,
                                                            bool* outputMasterBrightnessApplied = nullptr);
    bool ApplyMasterBrightnessToTexture(GLuint targetTex,
                                        GLuint sourceTex,
                                        int width,
                                        int height,
                                        int ystart,
                                        int yend,
                                        u16 masterBrightness);
    bool RenderCurrentLayersOverHighResCaptureBGToTexture(GLuint targetTex,
                                                          int ystart,
                                                          int yend);
    bool RenderCurrentBGOverlayOverHighResCaptureOBJToTexture(GLuint targetTex,
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
    void RenderFullscreenPassToTexture(GLuint shader, GLuint targetTex, int width, int height, GLuint source0, GLuint source1);
    void RenderFullscreenPass(GLuint shader, GLuint outputFB, int width, int height, GLuint source0, GLuint source1);
    void RenderArtCNNComputePass(GLuint shader, GLuint targetTex, int pass);
    bool RenderArtCNN2x(int modelIndex, GLuint sourceTex, GLuint targetTex);
    bool RenderArtCNN2xGuarded(int modelIndex, GLuint sourceTex, GLuint targetTex, bool secondLayer);
    void RenderSpline36(GLuint sourceTex, GLuint targetTex, int width, int height, float sourceShiftX = 0.0f, float sourceShiftY = 0.0f);
    bool RenderNNEDI32x(GLuint sourceTex, GLuint targetTex);
    bool RenderNNEDI32xGuarded(GLuint sourceTex, GLuint targetTex, bool secondLayer);
    void RenderXBRZ(GLuint sourceTex, GLuint targetTex);
    void RenderXBRZGuarded(GLuint sourceTex, GLuint targetTex, bool secondLayer);
    bool EnsureCuNNyPrograms();
    bool EnsureArtCNNComputePrograms();
    bool EnsureNNEDI3ComputePrograms();
    bool EnsureCuNNyWorkTexture(int index, int width, int height);
    void CopyCuNNyProgramsFrom(const GLRenderer2D& other);
    void CopyArtCNNProgramsFrom(const GLRenderer2D& other);
    void CopyNNEDI3ComputeProgramsFrom(const GLRenderer2D& other);
    void RenderNNEDI3ComputePass(GLuint shader, GLuint sourceTex, GLuint targetTex,
                                 int sourceWidth, int sourceHeight);
    void RenderCuNNyComputePass(GLuint shader, GLuint sourceTex, GLuint baseTex, GLuint targetTex,
                                int sourceWidth, int sourceHeight,
                                int nativeWidth, int nativeHeight);
    bool RenderCuNNy2x(int modelIndex, GLuint sourceTex, GLuint targetTex);
    bool RenderCuNNy2xGuarded(int modelIndex, GLuint sourceTex, GLuint targetTex, bool secondLayer);
    void RenderNativeBoundaryGuard(GLuint scaledTex, GLuint nativeTex, GLuint targetTex, bool secondLayer);
    void RenderNativeResolve(int ystart, int yend);
    void RenderScreenCurrent(int ystart, int yend,
                             bool currentFragmentationFallback = false,
                             WholeSceneCurrentPathReason reason = WholeSceneCurrentPathReason::DirectCurrent);
    void RenderScreenWholeSceneLegacy(int ystart, int yend, bool cleanHybridCandidate = false);
    void RenderScreenWholeSceneHighRes(int ystart, int yend);
    WholeSceneScaleDecision ChooseWholeSceneScaleDecision(int ystart, int yend) const;
    WholeScenePathDecision ChooseWholeScenePathDecision(int ystart, int yend) const;
    WholeSceneCaptureBackedPlan ChooseWholeSceneCaptureBackedPlan(int ystart, int yend) const;
    void RenderScreenWholeSceneCaptureBackedPlan(const WholeSceneCaptureBackedPlan& plan, int ystart, int yend);
    void RenderScreenWholeSceneCaptureBackedHybridFallback(int ystart, int yend);
    void RenderScreenWholeSceneSourceACaptureReplacement(int ystart, int yend);
    bool TryRenderHandoffBackgroundOverlayProduct(const WholeSceneCaptureRequest& request,
                                                  int handoffSlot,
                                                  const HandoffBackgroundChoice& background,
                                                  int ystart,
                                                  int yend);
    bool TryPrepareStableHandoffLiveBackground(int handoffSlot,
                                               HandoffBackgroundChoice& background,
                                               int ystart,
                                               int yend);
    HandoffBackgroundResolveResult ResolveCapturedHandoffBackgroundChoice(
        const WholeSceneCaptureRequest& request,
        int handoffSlot,
        int ystart,
        int yend);
    void RenderHandoffHybridComposite(const WholeSceneCaptureRequest& request,
                                      const HandoffBackgroundChoice& background,
                                      int ystart,
                                      int yend);
    SourceACaptureReplacementChoice ChooseSourceACaptureReplacement(int ystart, int yend);
    void ResolveSourceARouteProductChoice(SourceACaptureReplacementChoice& choice,
                                          u64 captureEventSerial,
                                          u64 source3DSerial,
                                          u32 source3DSceneHash,
                                          int ystart,
                                          int yend) const;
    void ApplyRouteProductLookupToSourceAChoice(SourceACaptureReplacementChoice& choice,
                                                const CaptureBackedRouteProductLookup& lookup) const;
    CaptureBackedRouteProductLookup ResolveHandoffExactRouteProduct(int routeSlot,
                                                                    u64 captureEventSerial,
                                                                    u64 eventSource3DSerial,
                                                                    u32 eventSource3DSceneHash,
                                                                    u32 captureBank,
                                                                    u64 backgroundEpochSerial,
                                                                    u64 backgroundSource3DSerial,
                                                                    u32 backgroundSource3DSceneHash,
                                                                    u32 capturePresentationHash,
                                                                    u32 currentPresentationHash,
                                                                    int ystart,
                                                                    int yend) const;
    void BlitWholeSceneCaptureProduct(GLuint sourceTex,
                                      int ystart,
                                      int yend,
                                      u16 presentationMasterBrightness,
                                      bool applyPresentationMasterBrightness = false,
                                      WholeSceneCaptureEffectOwner presentationEffectOwner =
                                          WholeSceneCaptureEffectOwner::None);
    void BlitWholeSceneHandoffProduct(const GLCaptureProductResolution& product,
                                      int ystart,
                                      int yend);
    void BlitWholeSceneSourceAProduct(const GLCaptureProductResolution& product,
                                      int ystart,
                                      int yend);
    void BlitWholeSceneSourceAReplacement(GLuint sourceTex,
                                           int ystart,
                                           int yend,
                                           bool applySourceAMasterBrightness = false);
    void RenderScreenWholeSceneCaptureEpochOverlay(int ystart, int yend);
    void RenderScreenWholeSceneFinalUpscale(int ystart, int yend, bool hybridFragmentationFallback = false);
    void RenderScreenWholeSceneOverlayOperator(int ystart, int yend);
    void RenderScreenWholeSceneCaptureBackedHandoff(int ystart, int yend);
    void RenderScreenWholeSceneFinalizeFullFrame();
    void RenderScreenWholeScene(int ystart, int yend);
    void RenderScreen(int ystart, int yend);
};

}
