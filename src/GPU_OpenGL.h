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
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override;

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

    enum class HighResCaptureSourceKind : u8
    {
        None = 0,
        CleanOutput3D = 1,
        CleanEngineA2DOutput = 2,
        RecursiveHandoffOutput = 3,
        RecursiveSourceReplacementOutput = 4,
        NativeOnlyOutput2D = 5,
        Unknown = 6,
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
        u32 SourcePresentationHash;
        HighResCaptureSourceKind SourceKind;
        u32 ProductMask;
        HighResCaptureRejectReason RejectReason;
    } LastHighResDisplayCaptureEvent;
    sHighResDisplayCaptureEvent HighResDisplayCapture256Event[4];
    u64 HighResDisplayCaptureEventSerial;
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
    };
    sCaptureBackgroundEpoch ActiveCaptureBackgroundEpoch[2];
    GLuint ActiveCaptureBackgroundEpochTex[2];
    GLuint ActiveCaptureBackgroundEpochFB[2];

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
    bool IsEngineRoutedToFinalBottom(u32 engine) const;
    bool IsEngineRoutedToFinalBottom(u32 engine, int ystart, int yend) const;
    bool HasMainVRAMDisplayCaptureFinalRoute() const;
    bool CanRenderPhysicalFinalUpscaleForRange(int ystart, int yend) const;
    void RenderFinalPassToFramebuffer(int ystart,
                                      int yend,
                                      GLuint targetFB,
                                      int viewportW,
                                      int viewportH,
                                      int framebufferScale,
                                      GLuint mainInputTex,
                                      GLuint subInputTex);
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
    void PublishHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture,
                                           const sHighResDisplayCaptureEvent& event,
                                           bool fullDisplay);
    bool StoreHighResDisplayCaptureBackgroundProduct(u32 captureBank, GLuint sourceTex);
    bool StoreHighResDisplayCaptureFullProduct(u32 captureBank, GLuint sourceTex);
    bool StoreHighResDisplayCaptureProduct(GLuint dstFB, GLuint dstTex, GLuint readFB, GLuint sourceTex);
    bool UpdateCaptureBackgroundEpochForRoute(int routeSlot,
                                              const sHighResDisplayCaptureEvent& event);
    void InvalidateCaptureBackgroundEpochForBank(u32 captureBank);
    void InvalidateCaptureBackgroundEpoch(int routeSlot = -1);
    bool IsFullDisplaySourceACaptureRecord(const sLastDisplayCaptureDebug& capture,
                                           u32 expectedBlock) const;
    bool IsFullDisplayHighResCaptureEventRecord(const sHighResDisplayCaptureEvent& event,
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
