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

#ifndef GPU_OPENGL_H
#define GPU_OPENGL_H

#include <atomic>
#include <vector>

#include "OpenGLSupport.h"
#include "GPU.h"
#include "GPU2D_OpenGL.h"
#include "GPU3D_OpenGL.h"
#include "GPU3D_Compute.h"

namespace melonDS
{

class GLRenderer : public Renderer
{
public:
    GLRenderer(melonDS::NDS& nds, bool compute);
    ~GLRenderer() override;
    bool Init() override;
    void Reset() override;
    void Stop() override;

    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void Start3DRendering() override;
    void Finish3DRendering() override;
    void Restart3DRendering() override;

    void VBlank() override;
    void VBlankEnd() override;

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SetVRAMCaptureSyncReason(u32 reason) override;
    void NotifyVRAMWrite(u32 bank, u32 offset, u32 bytes, bool changed) override;
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete, bool invalidate) override;

    bool GetFramebuffers(void** top, void** bottom) override;
    bool ReadWholeScene2DDebugView(int screen,
                                   WholeScene2DDebugView view,
                                   int& width,
                                   int& height,
                                   std::vector<u32>& rgba,
                                   std::string* status = nullptr) override;
    bool SetWholeScene2DDebugPoison(bool source3D,
                                    bool native3DResolve,
                                    bool native3DResolveAlpha,
                                    std::string* status = nullptr) override;
    bool SetWholeScene2DDebugViewsActive(bool active,
                                         std::string* status = nullptr) override;
    bool ReadWholeScene2DTimingCSV(std::string& header,
                                   std::string& row) override;
    void SetWholeScene2DTimingFrame(u64 frame, bool valid) override;
    bool SetWholeScene2DRollingDebugCapture(bool enabled,
                                            int frameCount,
                                            std::string* status = nullptr) override;
    bool ReadWholeScene2DCurrentFinalDebugFrame(WholeScene2DFinalDebugFrame& frame,
                                                std::string* status = nullptr) override;
    bool ReadWholeScene2DRollingDebugFrames(std::vector<WholeScene2DFinalDebugFrame>& frames,
                                            std::string* status = nullptr) override;
    bool ReadTextureScalingDebugStats(TextureScalingDebugStats& stats,
                                      std::string* status = nullptr) override;
    bool ResetTextureScalingDebugStats(std::string* status = nullptr) override;
    bool ReadTextureScalingDebugLastMiss(TextureScalingDebugLastMiss& miss,
                                         std::string* status = nullptr) override;
    bool SetTextureScalingDebugCaptureEnabled(bool enabled,
                                              std::string* status = nullptr) override;
    bool ReadTextureScalingDebugFrameTextures(TextureScalingDebugFrameTextures& frame,
                                              std::string* status = nullptr) override;
    bool SetTextureScalingDebugFrameCaptureEnabled(bool enabled,
                                                   std::string* status = nullptr) override;

    void SwapBuffers() override;

    bool NeedsShaderCompile() override;
    void ShaderCompileStep(int& current, int& count) override;

private:
    friend class GLRenderer2D;
    friend class GLRenderer3D;
    friend class ComputeRenderer3D;

    bool IsCompute;

    int ScaleFactor;
    int ScreenW, ScreenH;
    bool PhysicalFinalUpscale;

    GLuint RectVtxBuffer;
    GLuint RectVtxArray;

    GLuint OutputTex3D;
    GLuint OutputTex2D[2];

    struct sFinalPassConfig
    {
        u32 uScreenSwap[192];
        u32 uScaleFactor;
        u32 uAuxLayer;
        u32 uDispModeA;
        u32 uDispModeB;
        u32 uBrightModeA;
        u32 uBrightModeB;
        u32 uBrightFactorA;
        u32 uBrightFactorB;
        float uAuxColorFactor;
        u32 __pad0[3];
    } FinalPassConfig;

    GLuint FPShader;
    GLuint FPConfigUBO;

    GLuint FPVertexBufferID;
    GLuint FPVertexArrayID;

    GLuint AuxInputTex;                 // aux input (VRAM and mainmem FIFO)

    // texture/fb for display capture VRAM input
    GLuint CaptureVRAMTex;
    GLuint CaptureVRAMFB;

    GLuint FPOutputTex[2];               // final output
    GLuint FPOutputFB[2];
    GLuint NativeFPOutputTex[2];         // native physical final output before postprocess upscale
    GLuint NativeFPOutputFB[2];
    GLuint NativeFPOutputLayerReadFB[2];
    GLuint PhysicalFinalNativeTex[2];    // temporary 2D top/bottom sources for postprocess upscale
    GLuint PhysicalFinalNativeFB[2];
    GLuint PhysicalFinalScaledTex[2];    // temporary 2D top/bottom scaled results
    GLuint PhysicalFinalScaledFB[2];
    GLuint PhysicalFinalOutputLayerFB[2];

    struct RollingFinalDebugSlot
    {
        bool Valid = false;
        u64 Serial = 0;
        WholeScene2DFinalDebugFrame Metadata;
    };

    bool RollingFinalDebugCaptureEnabled;
    int RollingFinalDebugCapacity;
    int RollingFinalDebugWriteIndex;
    u64 RollingFinalDebugSerial;
    bool WholeSceneTimingFrameValid;
    u64 WholeSceneTimingFrame;
    GLuint RollingFinalDebugTex;
    GLuint RollingFinalDebugFB;
    int RollingFinalDebugWidth;
    int RollingFinalDebugHeight;
    std::vector<RollingFinalDebugSlot> RollingFinalDebugSlots;

    struct sCaptureConfig
    {
        float uInvCaptureSize[2];
        u32 uSrcALayer;
        u32 uSrcBLayer;
        u32 uSrcBOffset;
        u32 uDstMode;
        u32 uBlendFactors[2];
        float uSrcAOffset[192];
        float uSrcBColorFactor;
        u32 __pad0[3];
    } CaptureConfig;

    struct sLastDisplayCaptureDebug
    {
        bool Valid;
        u32 CaptureCnt;
        int YStart;
        int YEnd;
        int DstWidth;
        int DstHeight;
        u32 SrcA;
        u32 SrcB;
        u32 SrcBBlock;
        u32 SrcBOffset;
        u32 DstBlock;
        u32 DstOffset;
        u32 CapSize;
        u32 DstMode;
        u32 EVA;
        u32 EVB;
        bool UsesSrcB;
        bool SrcBUsesTrackedCapture;
        bool SrcBSameDstBank;
        int SrcBTrackedLayer;
        bool FinalNativeSourceA;
        u32 FinalNativeSourceAKind;
    } LastDisplayCaptureDebug;
    sLastDisplayCaptureDebug LastDisplayCapture256Debug[4];
    sLastDisplayCaptureDebug LastDisplayCapture128Debug[16];

    // Mean luma of the display-capture content stored this frame. Register and
    // presentation-hash traces cannot see a fade that lives inside the captured
    // content itself; this is the only field that can.
    struct sCaptureNativeLumaDebug
    {
        bool Valid;
        u32 CaptureCnt;
        u32 DstBlock;
        u32 DstOffset;
        int LumaX1000;
    } LastCaptureNativeLumaDebug = {};

    enum class HighResCaptureSourceKind : u8
    {
        None = 0,
        CleanOutput3D = 1,
        CleanEngineA2DOutput = 2,
        RecursiveHandoffOutput = 3,
        RecursiveSourceReplacementOutput = 4,
        NativeOnlyOutput2D = 5,
        Unknown = 6,
        DerivedMainVRAMDisplayEpoch = 7,
    };

    enum class HighResCaptureRejectReason : u8
    {
        None = 0,
        NotFullDisplay = 1,
        UnsupportedSize = 2,
        UsesSourceB = 3,
        BlendedOrFeedback = 4,
        SameBankReadWrite = 5,
        DirtyOrPartialSource = 6,
        RecursiveSource = 7,
        MissingSourceTexture = 8,
        RouteMismatch = 9,
        BankInvalidated = 10,
        EpochMismatch = 11,
        FinalNativePostprocessSource = 12,
    };

    enum HighResCaptureProductMask : u32
    {
        HighResCaptureProductFullEquivalent = 1u << 0,
        HighResCaptureProductBackground3DUnderlay = 1u << 1,
    };

    // Unlike the native capture records above, this is live renderer state.
    // It owns high-resolution products that later capture-backed paths may consume.
    struct sHighResDisplayCaptureEvent
    {
        bool Valid;
        u64 Serial;
        u64 Source3DSerial;
        u32 Source3DSceneHash;
        u32 CaptureCnt;
        int YStart;
        int YEnd;
        int DstWidth;
        int DstHeight;
        u32 SrcA;
        u32 SrcB;
        u32 DstBlock;
        u32 DstOffset;
        u32 CapSize;
        u32 DstMode;
        bool ScreenSwap;
        bool MainEngineFinalBottom;
        u32 MainDisplayMode;
        u32 SourceLayerEnable;
        u32 SourceBGMode;
        u32 SourceVisibleBitmapMask;
        bool SourceDirect3DVisible;
        bool SourceOBJVisible;
        u32 SourceWholeScenePath;
        int SourceWholeSceneYStart;
        int SourceWholeSceneYEnd;
        u32 SourceVisibleBGLayers;
        u32 SourceBGLayerTypes;
        bool SourceRenderedFullWholeScene;
        bool SourceDirect3DOnlyBackground;
        bool SourceTextBGShapeFullEquivalent;
        bool SourceTextBGFullEquivalent;
        bool SourceOBJOnlyDirtyOrPartial;
        u32 SourcePresentationHash;
        u16 SourceMasterBrightness;
        bool HasSourceEffectState;
        HighResCaptureSourceKind SourceKind;
        u32 ProductMask;
        HighResCaptureRejectReason RejectReason;
    } LastHighResDisplayCaptureEvent;
    sHighResDisplayCaptureEvent HighResDisplayCapture256Event[4];
    sHighResDisplayCaptureEvent MainVRAMDisplayExactProductEvent[4];
    u64 HighResDisplayCaptureEventSerial;
    u64 Output3DSerial;
    u32 Output3DSceneHash;
    GLuint HighResDisplayCaptureBackgroundTex[4];
    GLuint HighResDisplayCaptureBackgroundFB[4];
    GLuint HighResDisplayCaptureBackgroundReadFB;
    GLuint HighResDisplayCaptureFullTex[4];
    GLuint HighResDisplayCaptureFullFB[4];
    GLuint HighResDisplayCaptureFullReadFB;

    struct sCaptureBackgroundEpoch
    {
        bool Valid;
        u64 Serial;
        u64 Source3DSerial;
        u32 Source3DSceneHash;
        u32 CaptureBank;
        u32 DstBlock;
        u32 DstOffset;
        bool ScreenSwap;
        bool MainEngineFinalBottom;
        u32 ConsumerRouteSlot;
        HighResCaptureSourceKind SourceKind;
        u32 ProductMask;
        u32 SourceLayerEnable;
        u32 SourceBGMode;
        u32 SourceVisibleBitmapMask;
        bool SourceDirect3DVisible;
        bool SourceOBJVisible;
        u32 SourcePresentationHash;
        u16 StoredMasterBrightness;
        bool HasStoredEffectState;
    };
    sCaptureBackgroundEpoch ActiveCaptureBackgroundEpoch[2];
    GLuint ActiveCaptureBackgroundEpochTex[2];
    GLuint ActiveCaptureBackgroundEpochFB[2];

    struct sMainVRAMDisplayEpoch
    {
        bool Valid;
        u64 Serial;
        u64 Source3DSerial;
        u32 Source3DSceneHash;
        u32 CaptureCnt;
        u32 CaptureBank;
        u32 DstOffset;
        bool ScreenSwap;
        bool MainEngineFinalBottom;
        HighResCaptureSourceKind SourceKind;
        u32 ProductMask;
        u32 SourceLayerEnable;
        u32 SourceBGMode;
        u32 SourceVisibleBitmapMask;
        bool SourceDirect3DVisible;
        bool SourceOBJVisible;
        u32 SourcePresentationHash;
        bool HasDirtyRows;
        u32 DirtyYStart;
        u32 DirtyYEnd;
    };
    sMainVRAMDisplayEpoch MainVRAMDisplayEpoch[4];
    GLuint MainVRAMDisplayEpochTex[4];
    GLuint MainVRAMDisplayEpochFB[4];
    struct sMainVRAMDisplayEpochInvalidationDebug
    {
        u32 Reason;
        u32 Bank;
        u32 Start;
        u32 Len;
        bool Complete;
    } MainVRAMDisplayEpochInvalidationDebug;
    u32 PendingVRAMCaptureSyncReason;

    struct sVRAMDisplayWriteDebug
    {
        u32 WriteCount;
        u32 ChangedWriteCount;
        u64 WriteBytes;
        u64 ChangedWriteBytes;
        u32 BankMask;
        u32 DisplayBank;
        u32 DisplayWriteCount;
        u32 DisplayChangedWriteCount;
        u64 DisplayWriteBytes;
        u64 DisplayChangedWriteBytes;
        u32 DisplayFirstOffset;
        u32 DisplayLastEnd;
        u32 DisplayDirtyYStart;
        u32 DisplayDirtyYEnd;
    } VRAMDisplayWriteDebug;
    std::atomic_bool WholeSceneDebugViewsActive;

    GLuint CaptureShader;
    GLuint CaptureConfigUBO;

    GLuint CaptureVtxBuffer;
    GLuint CaptureVtxArray;

    GLuint CaptureOutput256FB[4];
    GLuint CaptureOutput256Tex;
    bool CaptureOutput256Valid[4];
    GLuint CaptureOutput128FB[16];
    GLuint CaptureOutput128Tex;

    GLuint CapDownShader;
    GLint CapDownInputLayerULoc;

    GLuint CaptureSyncFB;
    GLuint CaptureSyncTex;

    // Native-size scratch target for the capture-content luma probe. Only
    // touched while the whole-scene timing CSV is being read.
    GLuint CaptureLumaProbeFB;
    GLuint CaptureLumaProbeTex;
    int WholeSceneTimingCSVActiveFrames = 0;

    u16* AuxInputBuffer[2];
    u8 AuxUsageMask;

    struct sPhaseTiming
    {
        u64 TotalUS = 0;
        u64 MaxUS = 0;
        u32 Count = 0;
    };

    struct sWholeSceneFrameTiming
    {
        sPhaseTiming DrawScanline;
        sPhaseTiming DrawSprites;
        sPhaseTiming RenderScreen;
        sPhaseTiming DoCapture;
        sPhaseTiming Start3D;
        sPhaseTiming Finish3D;
        sPhaseTiming Restart3D;
        sPhaseTiming VBlank;
        sPhaseTiming VBlankEnd;
    } WholeSceneFrameTiming;

    u32 DispCntA, DispCntB;
    u16 MasterBrightnessA, MasterBrightnessB;
    u32 CaptureCnt;

    bool NeedPartialRender;
    int LastLine;
    int LastCapLine;
    int Aux0VRAMCap;
    bool FinalPassInvalidCaptureReseed;
    bool PhysicalFinalNativeRowValid[192];
    u32 PhysicalFinalNativeValidRows;
    bool PhysicalFinalPostprocessApplied;
    u32 PhysicalFinalPostprocessRejectReason;
    bool PhysicalFinalNativeInputValid[2];
    int PhysicalFinalNativeInputPath[2];

    struct sFinalVRAMDisplayRenderTrace
    {
        int DisplayBank = -1;
        int ReplacementEligible = 0;
        int RejectReason = 0;
        int UsedEpoch = 0;
        int ChosenBank = -1;
        u64 ChosenSerial = 0;
        u64 ChosenSource3DSerial = 0;
        u32 ChosenSource3DSceneHash = 0;
        u32 ChosenSourcePresentationHash = 0;
        u32 ChosenSourceKind = 0;
        u32 ChosenProductMask = 0;
        int ChosenTex = 0;
        int EventValid = 0;
        u64 EventSerial = 0;
        int EventDstBlock = -1;
        int EventDstOffset = -1;
        int EventScreenSwap = 0;
        int EventMainFinalBottom = 0;
        int EventSourceOBJ = 0;
        int EventSourceRenderedFullWholeScene = 0;
        u32 EventSourceKind = 0;
        u32 EventProductMask = 0;
        u32 EventRejectReason = 0;
        int EventFullTex = 0;
        int EventMatchesNativeCapture = 0;
        int ExactEventRouteMatches = 0;
        int ExactEventProductAvailable = 0;
        int ExactEventProductUsable = 0;
    } FinalVRAMDisplayRenderTrace;

    static constexpr u32 kFinalPresentationTransitionGuardFrames = 2;
    static constexpr u32 kFinalPresentationStableRouteScanlines = 6 * 192;
    static constexpr u32 kFinalPresentationDirectSwapGuardStableScanlines = 48 * 192;
    static constexpr u32 kFinalPresentationScreenSwapExcursionMaxScanlines = 32 * 192;
    struct sFinalPresentationState
    {
        bool Valid;
        bool ScreenSwap;
        u32 DispModeA;
        u32 DispModeB;
    } LastFinalPresentationState, FinalPresentationScreenSwapExcursionBaseline;
    u32 FinalPresentationStateStableScanlines;
    u32 FinalPresentationScreenSwapExcursionScanlines;
    bool FinalPresentationScreenSwapExcursionActive;
    u32 FinalPresentationTransitionGuardFrames;

    enum class FinalCaptureSourceKind : u32
    {
        NormalOutputTex2D = 0,
        NormalOutputTex3D = 1,
        FinalNativeEngineOutput = 2,
        FinalNativeResolved3D = 3,
    };

    struct sFinalCaptureSourceDebug
    {
        FinalCaptureSourceKind SourceAKind;
        bool SourceANativeSized;
        bool SourceARangeValid;
        bool SourceAUsed;
    } FinalCaptureSourceDebug;

    bool GetFinalPassScreenSwapForRange(int ystart, int yend, bool& screenSwap) const;
    void UpdateFinalPresentationTransitionGuard();
    bool IsFinalPresentationTransitionGuardActiveForRange(int ystart, int yend) const;
    bool IsFinalPresentationScreenSwapExcursionActiveForRange(int ystart, int yend) const;
    bool IsEngineRoutedToFinalBottom(u32 engine) const;
    bool IsEngineRoutedToFinalBottom(u32 engine, int ystart, int yend) const;
    bool IsMainVRAMDisplayFinalRouteForRange(int ystart, int yend) const;
    bool HasMainVRAMDisplayCaptureFinalRoute() const;
    static bool IsAcceptedMainVRAMDisplayFullProductSource(HighResCaptureSourceKind sourceKind);
    static void ResetFinalVRAMDisplayTrace(sFinalVRAMDisplayRenderTrace& trace, int displayBank);
    static void RecordFinalVRAMDisplayTraceEvent(sFinalVRAMDisplayRenderTrace& trace,
                                                 const sHighResDisplayCaptureEvent& event,
                                                 GLuint fullTex,
                                                 bool eventMatches,
                                                 bool exactRouteMatches,
                                                 bool exactProductAvailable,
                                                 bool exactProductUsable);
    static void RecordFinalVRAMDisplayTraceChosenEvent(sFinalVRAMDisplayRenderTrace& trace,
                                                       u32 displayBank,
                                                       GLuint texture,
                                                       const sHighResDisplayCaptureEvent& event);
    static void RecordFinalVRAMDisplayTraceChosenEpoch(sFinalVRAMDisplayRenderTrace& trace,
                                                       u32 displayBank,
                                                       GLuint texture,
                                                       const sMainVRAMDisplayEpoch& epoch);
    void RecordFinalVRAMDisplayTraceAccepted(u32 displayBank,
                                             GLuint texture,
                                             bool usedEpoch);
    void RecordFinalVRAMDisplayTraceRejected(int rejectReason);
    bool CanUseMainVRAMDisplayHighResCaptureReplacement(u32 displayBank,
                                                        GLuint* replacementTex = nullptr,
                                                        int* rejectReason = nullptr,
                                                        bool* usedEpoch = nullptr,
                                                        sFinalVRAMDisplayRenderTrace* trace = nullptr) const;
    bool CanRenderPhysicalFinalUpscaleForRange(int ystart, int yend) const;
    bool CanUpscaleMainVRAMDisplayNativeFallbackForRange(int ystart, int yend) const;
    void RenderFinalPassToFramebuffer(int ystart,
                                      int yend,
                                      GLuint targetFB,
                                      int viewportW,
                                      int viewportH,
                                      int framebufferScale,
                                      GLuint mainInputTex,
                                      GLuint subInputTex,
                                      bool mainInputReplacesVRAMDisplay = false);
    void RenderMainVRAMDisplayNativeFallbackUpscale(int backbuf, int ystart, int yend);
    bool EnsureRollingFinalDebugStorage();
    void ClearRollingFinalDebugCapture();
    void CaptureRollingFinalDebugFrame(int backbuf);
    WholeScene2DFinalDebugFrame CaptureRollingFinalDebugMetadata(u64 serial) const;
    bool ReadFinalDebugFrameFromFramebuffer(int framebuffer,
                                            u64 serial,
                                            WholeScene2DFinalDebugFrame& frame,
                                            std::string* status = nullptr);
    bool RenderPhysicalFinalUpscale();
    void RecordDisplayCaptureDebug(const sLastDisplayCaptureDebug& capture);
    void RecordHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture);
    sHighResDisplayCaptureEvent BuildHighResDisplayCaptureEventMetadata(const sLastDisplayCaptureDebug& capture,
                                                                        GLRenderer2D*& sourceRenderer);
    void ClassifyHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture,
                                            GLRenderer2D* sourceRenderer,
                                            bool fullDisplay,
                                            bool sourceAOnly,
                                            sHighResDisplayCaptureEvent& event);
    void StoreHighResDisplayCaptureEventProducts(const sLastDisplayCaptureDebug& capture,
                                                 sHighResDisplayCaptureEvent& event);
    bool TryPromoteCaptureEventFromMainVRAMDisplayEpoch(const sLastDisplayCaptureDebug& capture,
                                                        bool fullDisplay,
                                                        bool sourceAOnly,
                                                        sHighResDisplayCaptureEvent& event);
    void PublishHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture,
                                           const sHighResDisplayCaptureEvent& event,
                                           bool fullDisplay);
    bool StoreHighResDisplayCaptureBackgroundProduct(u32 captureBank, GLuint sourceTex);
    bool StoreHighResDisplayCaptureFullProduct(u32 captureBank, GLuint sourceTex);
    bool StoreHighResDisplayCaptureBackgroundProductFromCaptureOutput(u32 captureBank);
    bool StoreHighResDisplayCaptureFullProductFromCaptureOutput(u32 captureBank);
    bool StoreHighResDisplayCaptureProduct(GLuint dstFB, GLuint dstTex, GLuint readFB, GLuint sourceTex);
    bool StoreHighResDisplayCaptureProductFromFramebuffer(GLuint dstFB, GLuint dstTex, GLuint sourceFB);
    bool UpdateMainVRAMDisplayEpochFromEvent(const sHighResDisplayCaptureEvent& event);
    void RecordVRAMDisplayWriteDebug(u32 bank, u32 offset, u32 bytes, bool changed);
    void InvalidateMainVRAMDisplayEpochForBank(u32 captureBank,
                                               u32 reason = 0,
                                               u32 start = 0,
                                               u32 len = 0,
                                               bool complete = false);
    bool UpdateCaptureBackgroundEpochForRoute(int routeSlot,
                                              const sHighResDisplayCaptureEvent& event);
    void InvalidateCaptureBackgroundEpochForBank(u32 captureBank);
    void InvalidateCaptureBackgroundEpoch(int routeSlot = -1);
    bool IsFullDisplaySourceACaptureRecord(const sLastDisplayCaptureDebug& capture,
                                           u32 expectedBlock) const;
    bool IsFullDisplayHighResCaptureEventRecord(const sHighResDisplayCaptureEvent& event,
                                                u32 expectedBlock) const;
    bool IsFullDisplayHighResCaptureExactReplacementRecord(const sHighResDisplayCaptureEvent& event,
                                                           u32 expectedBlock) const;
    u32 DisplayCapture256ValidMask() const;
    u32 DisplayCapture256FullSourceAMask() const;
    u32 HighResDisplayCapture256ValidMask() const;
    u32 HighResDisplayCapture256SourceKindMask(HighResCaptureSourceKind sourceKind) const;
    u32 HighResDisplayCapture256ProductMask(u32 productMask) const;
    bool GetHighResDisplayCaptureEventForBG(u32 type,
                                            u32 tileOffset,
                                            u64& serial,
                                            u32& sourceKind,
                                            u32& productMask,
                                            u32& rejectReason) const;
    GLuint GetHighResDisplayCaptureBackgroundTexForBG(u32 type,
                                                      u32 tileOffset,
                                                      u64& serial,
                                                      u32& sourceKind,
                                                      u32& productMask,
                                                      u32& rejectReason) const;
    GLuint GetHighResDisplayCaptureFullTexForBG(u32 type,
                                                u32 tileOffset,
                                                u64& serial,
                                                u32& sourceKind,
                                                u32& productMask,
                                                u32& rejectReason) const;
    bool IsSourceAOnlyFullDisplayCapture(u32 capcnt) const;
    bool IsFullDisplayCaptureFromSourceAOnly(u32 capcnt) const;
    bool IsCurrentSourceAOnlyFullDisplayCaptureBlock(u32 captureBank) const;
    bool IsCurrentSourceAOnlyFullDisplayCaptureBG(u32 type, u32 tileOffset) const;
    bool IsCurrentSourceAOnlyFullDisplayCaptureOBJ(u32 type, u32 tileStride) const;
    bool IsCurrentFullDisplayCaptureFromSourceABlock(u32 captureBank) const;
    bool IsCurrentFullDisplayCaptureFromSourceABG(u32 type, u32 tileOffset) const;
    std::string DescribeFinalScreenSource(int layer) const;
    std::string DescribeMainDisplayRoute() const;
    std::string DescribeLastDisplayCapture() const;
    void ResetWholeSceneFrameTiming();
    void MeasureCaptureNativeLumaDebug(u32 capcnt, u32 dstblock, u32 dstoffset,
                                       int dstwidth, int dstheight, u32 capsize,
                                       GLuint captureFB);
    void AddWholeScenePhaseTiming(sPhaseTiming& phase, u64 elapsedUS);
    static void AppendWholeSceneFrameTimingCSVHeader(std::string& header);
    void AppendWholeSceneFrameTimingCSVRow(std::string& row) const;

    void SetScaleFactor(int scale);

    void RenderScreen(int ystart, int yend);
    void DoCapture(int ystart, int yend);
    void DownscaleCapture(int width, int height, int layer);
};

}

#endif // GPU_OPENGL_H
