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

#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>
#include "NDS.h"
#include "GPU_OpenGL.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

namespace
{
constexpr u32 VRAMCaptureInvalidationPreWriteSync = 1;
constexpr u32 MainVRAMDisplayCaptureBlockBytes = 64 * 512;
constexpr u32 MainVRAMDisplayCaptureVisibleBytes = 192 * 512;
constexpr u32 MainVRAMDisplayCaptureRowBytes = 512;

u64 ElapsedUS(std::chrono::steady_clock::time_point start)
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
}

bool IsFullWholeSceneSourcePath(WholeSceneRenderPath path)
{
    return path == WholeSceneRenderPath::HighResCompositor ||
           path == WholeSceneRenderPath::OverlayOperatorUpscale ||
           path == WholeSceneRenderPath::ConservativeHybridUpscale;
}

}

#include "OpenGL_shaders/FinalPassVS.h"
#include "OpenGL_shaders/FinalPassFS.h"
#include "OpenGL_shaders/CaptureVS.h"
#include "OpenGL_shaders/CaptureFS.h"
#include "OpenGL_shaders/CaptureDownscaleVS.h"
#include "OpenGL_shaders/CaptureDownscaleFS.h"


GLRenderer::GLRenderer(melonDS::NDS& nds, bool compute)
    : Renderer(nds.GPU)
{
    AuxInputBuffer[0] = new u16[256 * 256];
    AuxInputBuffer[1] = new u16[256 * 192];

    Rend2D_A = std::make_unique<GLRenderer2D>(GPU.GPU2D_A, *this);
    Rend2D_B = std::make_unique<GLRenderer2D>(GPU.GPU2D_B, *this);

    // TODO, eventually: figure out a nicer way to support different 3D renderers?
    IsCompute = compute;
    if (IsCompute)
        Rend3D = std::make_unique<ComputeRenderer3D>(GPU.GPU3D, *this);
    else
        Rend3D = std::make_unique<GLRenderer3D>(GPU.GPU3D, *this);

    ScaleFactor = 0;
    PhysicalFinalUpscale = false;
    memset(PhysicalFinalNativeRowValid, 0, sizeof(PhysicalFinalNativeRowValid));
    PhysicalFinalNativeValidRows = 0;
    PhysicalFinalPostprocessApplied = false;
    PhysicalFinalPostprocessRejectReason = 0;
    memset(PhysicalFinalNativeInputValid, 0, sizeof(PhysicalFinalNativeInputValid));
    memset(PhysicalFinalNativeInputPath, 0, sizeof(PhysicalFinalNativeInputPath));
    FinalCaptureSourceDebug = {};
    LastFinalPresentationState = {};
    FinalPresentationScreenSwapExcursionBaseline = {};
    FinalPresentationStateStableScanlines = 0;
    FinalPresentationScreenSwapExcursionScanlines = 0;
    FinalPresentationScreenSwapExcursionActive = false;
    FinalPresentationTransitionGuardFrames = 0;
    Output3DSerial = 0;
    Output3DSceneHash = 0;
    memset(MainVRAMDisplayExactProductEvent, 0, sizeof(MainVRAMDisplayExactProductEvent));
    VRAMDisplayWriteDebug = {};
    WholeSceneDebugViewsActive.store(false, std::memory_order_relaxed);
    RollingFinalDebugCaptureEnabled = false;
    RollingFinalDebugCapacity = 0;
    RollingFinalDebugWriteIndex = 0;
    RollingFinalDebugSerial = 0;
    WholeSceneTimingFrameValid = false;
    WholeSceneTimingFrame = 0;
    RollingFinalDebugTex = 0;
    RollingFinalDebugFB = 0;
    RollingFinalDebugWidth = 0;
    RollingFinalDebugHeight = 0;
}

#define glTexParams(target, wrap) \
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap); \
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap); \
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST); \
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

bool GLRenderer::Init()
{
    assert(glEnable != nullptr);

    GLint uniloc;

    // compile shaders

    if (!OpenGL::CompileVertexFragmentProgram(FPShader,
                                              kFinalPassVS, kFinalPassFS,
                                              "2DFinalPassShader",
                                              {{"vPosition", 0}},
                                              {{"oTopColor", 0}, {"oBottomColor", 1}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(CaptureShader,
                                              kCaptureVS, kCaptureFS,
                                              "2DCaptureShader",
                                              {{"vPosition", 0}, {"vTexcoord", 1}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(CapDownShader,
                                              kCaptureDownscaleVS, kCaptureDownscaleFS,
                                              "2DCaptureDownscaleShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    // vertex buffers

    const float rectvertices[2*2*3] = {
            0, 1,   1, 0,   1, 1,
            0, 1,   0, 0,   1, 0
    };

    glGenBuffers(1, &RectVtxBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, RectVtxBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectvertices), rectvertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &RectVtxArray);
    glBindVertexArray(RectVtxArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    float vertices[12][2];
#define SETVERTEX(i, x, y) \
    vertices[i][0] = x; \
    vertices[i][1] = y;

    SETVERTEX(0, -1, 1);
    SETVERTEX(1, 1, -1);
    SETVERTEX(2, 1, 1);
    SETVERTEX(3, -1, 1);
    SETVERTEX(4, -1, -1);
    SETVERTEX(5, 1, -1);

#undef SETVERTEX

    // final pass vertex data: 2x position, 2x texcoord
    glGenBuffers(1, &FPVertexBufferID);
    glBindBuffer(GL_ARRAY_BUFFER, FPVertexBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices[0], GL_STATIC_DRAW);

    glGenVertexArrays(1, &FPVertexArrayID);
    glBindVertexArray(FPVertexArrayID);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenFramebuffers(2, &FPOutputFB[0]);
    glGenFramebuffers(2, &NativeFPOutputFB[0]);
    glGenFramebuffers(2, &NativeFPOutputLayerReadFB[0]);
    glGenFramebuffers(2, &PhysicalFinalNativeFB[0]);
    glGenFramebuffers(2, &PhysicalFinalScaledFB[0]);
    glGenFramebuffers(2, &PhysicalFinalOutputLayerFB[0]);
    glGenTextures(1, &RollingFinalDebugTex);
    glGenFramebuffers(1, &RollingFinalDebugFB);

    // capture vertex data: 2x position, 2x texcoord
    glGenBuffers(1, &CaptureVtxBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, CaptureVtxBuffer);
    glBufferData(GL_ARRAY_BUFFER, 2 * 6 * 4 * sizeof(u16), nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &CaptureVtxArray);
    glBindVertexArray(CaptureVtxArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribIPointer(0, 2, GL_SHORT, 4 * sizeof(u16), (void*)0);
    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribIPointer(1, 2, GL_SHORT, 4 * sizeof(u16), (void*)(2 * sizeof(u16)));

    // textures / framebuffers

    glGenTextures(1, &AuxInputTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, AuxInputTex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB5_A1, 256, 256, 2, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

    glGenTextures(1, &CaptureVRAMTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureVRAMTex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glGenFramebuffers(1, &CaptureVRAMFB);

    glGenTextures(2, FPOutputTex);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[i]);
        glTexParams(GL_TEXTURE_2D_ARRAY, GL_CLAMP_TO_EDGE);
    }

    glGenTextures(2, NativeFPOutputTex);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, NativeFPOutputTex[i]);
        glTexParams(GL_TEXTURE_2D_ARRAY, GL_CLAMP_TO_EDGE);
    }

    glGenTextures(2, PhysicalFinalNativeTex);
    glGenTextures(2, PhysicalFinalScaledTex);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, PhysicalFinalNativeTex[i]);
        glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, PhysicalFinalScaledTex[i]);
        glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);
    }

    glGenTextures(1, &CaptureOutput256Tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glGenFramebuffers(4, CaptureOutput256FB);

    glGenTextures(1, &CaptureOutput128Tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput128Tex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_REPEAT);
    glGenFramebuffers(16, CaptureOutput128FB);

    glGenTextures(1, &CaptureSyncTex);
    glBindTexture(GL_TEXTURE_2D, CaptureSyncTex);
    glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 256, 256, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

    glGenFramebuffers(1, &CaptureSyncFB);
    glBindFramebuffer(GL_FRAMEBUFFER, CaptureSyncFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureSyncTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glGenTextures(1, &CaptureLumaProbeTex);
    glBindTexture(GL_TEXTURE_2D, CaptureLumaProbeTex);
    glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &CaptureLumaProbeFB);
    glBindFramebuffer(GL_FRAMEBUFFER, CaptureLumaProbeFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureLumaProbeTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glGenTextures(4, HighResDisplayCaptureBackgroundTex);
    glGenFramebuffers(4, HighResDisplayCaptureBackgroundFB);
    glGenFramebuffers(1, &HighResDisplayCaptureBackgroundReadFB);
    glGenTextures(4, HighResDisplayCaptureFullTex);
    glGenFramebuffers(4, HighResDisplayCaptureFullFB);
    glGenFramebuffers(1, &HighResDisplayCaptureFullReadFB);
    glGenTextures(2, ActiveCaptureBackgroundEpochTex);
    glGenFramebuffers(2, ActiveCaptureBackgroundEpochFB);
    glGenTextures(4, MainVRAMDisplayEpochTex);
    glGenFramebuffers(4, MainVRAMDisplayEpochFB);
    for (int i = 0; i < 4; i++)
    {
        glBindTexture(GL_TEXTURE_2D, HighResDisplayCaptureBackgroundTex[i]);
        glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, HighResDisplayCaptureBackgroundFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, HighResDisplayCaptureBackgroundTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, HighResDisplayCaptureFullTex[i]);
        glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, HighResDisplayCaptureFullFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, HighResDisplayCaptureFullTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, MainVRAMDisplayEpochTex[i]);
        glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, MainVRAMDisplayEpochFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, MainVRAMDisplayEpochTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, ActiveCaptureBackgroundEpochTex[i]);
        glTexParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, ActiveCaptureBackgroundEpochFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ActiveCaptureBackgroundEpochTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    // UBOs

    glGenBuffers(1, &FPConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, FPConfigUBO);
    static_assert((sizeof(sFinalPassConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sFinalPassConfig), nullptr, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 30, FPConfigUBO);

    glGenBuffers(1, &CaptureConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, CaptureConfigUBO);
    static_assert((sizeof(sCaptureConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sCaptureConfig), nullptr, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 31, CaptureConfigUBO);

    // shader config

    glUseProgram(FPShader);

    uniloc = glGetUniformLocation(FPShader, "MainInputTexA");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(FPShader, "MainInputTexB");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(FPShader, "AuxInputTex");
    glUniform1i(uniloc, 2);

    uniloc = glGetUniformBlockIndex(FPShader, "ubFinalPassConfig");
    glUniformBlockBinding(FPShader, uniloc, 30);


    glUseProgram(CaptureShader);

    uniloc = glGetUniformLocation(CaptureShader, "InputTexA");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(CaptureShader, "InputTexB");
    glUniform1i(uniloc, 1);

    uniloc = glGetUniformBlockIndex(CaptureShader, "ubCaptureConfig");
    glUniformBlockBinding(CaptureShader, uniloc, 31);


    glUseProgram(CapDownShader);

    uniloc = glGetUniformLocation(CapDownShader, "InputTex");
    glUniform1i(uniloc, 0);

    CapDownInputLayerULoc = glGetUniformLocation(CapDownShader, "uInputLayer");


    auto rend2DA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    if (!rend2DA->InitShaders()) return false;
    auto rend2DB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    if (!rend2DB->InitShaders(*rend2DA)) return false;

    if (!Rend2D_A->Init()) return false;
    if (!Rend2D_B->Init()) return false;
    if (!Rend3D->Init()) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

GLRenderer::~GLRenderer()
{
    glDeleteProgram(FPShader);
    glDeleteProgram(CaptureShader);
    glDeleteProgram(CapDownShader);

    glDeleteBuffers(1, &RectVtxBuffer);
    glDeleteVertexArrays(1, &RectVtxArray);

    glDeleteBuffers(1, &FPVertexBufferID);
    glDeleteVertexArrays(1, &FPVertexArrayID);

    glDeleteBuffers(1, &CaptureVtxBuffer);
    glDeleteVertexArrays(1, &CaptureVtxArray);

    glDeleteFramebuffers(2, FPOutputFB);
    glDeleteFramebuffers(2, NativeFPOutputFB);
    glDeleteFramebuffers(2, NativeFPOutputLayerReadFB);
    glDeleteFramebuffers(2, PhysicalFinalNativeFB);
    glDeleteFramebuffers(2, PhysicalFinalScaledFB);
    glDeleteFramebuffers(2, PhysicalFinalOutputLayerFB);
    glDeleteTextures(1, &RollingFinalDebugTex);
    glDeleteFramebuffers(1, &RollingFinalDebugFB);
    glDeleteTextures(1, &AuxInputTex);
    glDeleteTextures(1, &CaptureVRAMTex);
    glDeleteTextures(2, FPOutputTex);
    glDeleteTextures(2, NativeFPOutputTex);
    glDeleteTextures(2, PhysicalFinalNativeTex);
    glDeleteTextures(2, PhysicalFinalScaledTex);

    delete[] AuxInputBuffer[0];
    delete[] AuxInputBuffer[1];

    glDeleteTextures(1, &CaptureOutput256Tex);
    glDeleteFramebuffers(4, CaptureOutput256FB);
    glDeleteTextures(1, &CaptureOutput128Tex);
    glDeleteFramebuffers(16, CaptureOutput128FB);
    glDeleteTextures(1, &CaptureSyncTex);
    glDeleteFramebuffers(1, &CaptureSyncFB);
    glDeleteTextures(1, &CaptureLumaProbeTex);
    glDeleteFramebuffers(1, &CaptureLumaProbeFB);
    glDeleteTextures(4, HighResDisplayCaptureBackgroundTex);
    glDeleteFramebuffers(4, HighResDisplayCaptureBackgroundFB);
    glDeleteFramebuffers(1, &HighResDisplayCaptureBackgroundReadFB);
    glDeleteTextures(4, HighResDisplayCaptureFullTex);
    glDeleteFramebuffers(4, HighResDisplayCaptureFullFB);
    glDeleteFramebuffers(1, &HighResDisplayCaptureFullReadFB);
    glDeleteTextures(2, ActiveCaptureBackgroundEpochTex);
    glDeleteFramebuffers(2, ActiveCaptureBackgroundEpochFB);
    glDeleteTextures(4, MainVRAMDisplayEpochTex);
    glDeleteFramebuffers(4, MainVRAMDisplayEpochFB);

    glDeleteBuffers(1, &FPConfigUBO);
    glDeleteBuffers(1, &CaptureConfigUBO);

    auto rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2D->DeleteShaders();
}

void GLRenderer::Reset()
{
    memset(&FinalPassConfig, 0, sizeof(FinalPassConfig));
    memset(&CaptureConfig, 0, sizeof(CaptureConfig));
    memset(&LastDisplayCaptureDebug, 0, sizeof(LastDisplayCaptureDebug));
    memset(LastDisplayCapture256Debug, 0, sizeof(LastDisplayCapture256Debug));
    memset(LastDisplayCapture128Debug, 0, sizeof(LastDisplayCapture128Debug));
    memset(&LastHighResDisplayCaptureEvent, 0, sizeof(LastHighResDisplayCaptureEvent));
    memset(HighResDisplayCapture256Event, 0, sizeof(HighResDisplayCapture256Event));
    memset(MainVRAMDisplayExactProductEvent, 0, sizeof(MainVRAMDisplayExactProductEvent));
    memset(ActiveCaptureBackgroundEpoch, 0, sizeof(ActiveCaptureBackgroundEpoch));
    memset(MainVRAMDisplayEpoch, 0, sizeof(MainVRAMDisplayEpoch));
    MainVRAMDisplayEpochInvalidationDebug = {};
    PendingVRAMCaptureSyncReason = 0;
    memset(CaptureOutput256Valid, 0, sizeof(CaptureOutput256Valid));
    HighResDisplayCaptureEventSerial = 0;
    Output3DSerial = 0;
    Output3DSceneHash = 0;
    ClearRollingFinalDebugCapture();

    AuxUsageMask = 0;

    DispCntA = 0;
    DispCntB = 0;
    MasterBrightnessA = 0;
    MasterBrightnessB = 0;
    CaptureCnt = 0;

    NeedPartialRender = false;
    LastLine = 0;
    LastCapLine = 0;
    Aux0VRAMCap = -1;
    FinalPassInvalidCaptureReseed = false;
    LastFinalPresentationState = {};
    FinalPresentationScreenSwapExcursionBaseline = {};
    FinalPresentationStateStableScanlines = 0;
    FinalPresentationScreenSwapExcursionScanlines = 0;
    FinalPresentationScreenSwapExcursionActive = false;
    FinalPresentationTransitionGuardFrames = 0;
    ResetWholeSceneFrameTiming();

    Rend2D_A->Reset();
    Rend2D_B->Reset();
    Rend3D->Reset();
}

void GLRenderer::ResetWholeSceneFrameTiming()
{
    WholeSceneFrameTiming = {};
    LastCaptureNativeLumaDebug = {};
    if (WholeSceneTimingCSVActiveFrames > 0)
        WholeSceneTimingCSVActiveFrames--;
    MainVRAMDisplayEpochInvalidationDebug = {};
    VRAMDisplayWriteDebug = {};
    VRAMDisplayWriteDebug.DisplayBank = 0xFFFFFFFFu;
    VRAMDisplayWriteDebug.DisplayFirstOffset = 0xFFFFFFFFu;
    VRAMDisplayWriteDebug.DisplayDirtyYStart = 192;
    VRAMDisplayWriteDebug.DisplayDirtyYEnd = 0;
    FinalPassInvalidCaptureReseed = false;
    if (FinalPresentationTransitionGuardFrames > 0)
        FinalPresentationTransitionGuardFrames--;
    memset(PhysicalFinalNativeRowValid, 0, sizeof(PhysicalFinalNativeRowValid));
    PhysicalFinalNativeValidRows = 0;
    PhysicalFinalPostprocessApplied = false;
    PhysicalFinalPostprocessRejectReason = 0;
    memset(PhysicalFinalNativeInputValid, 0, sizeof(PhysicalFinalNativeInputValid));
    memset(PhysicalFinalNativeInputPath, 0, sizeof(PhysicalFinalNativeInputPath));
    FinalCaptureSourceDebug = {};

    if (auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get()))
        rendA->ResetWholeSceneUpdateTiming();
    if (auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get()))
        rendB->ResetWholeSceneUpdateTiming();
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        rend3d->ResetRenderFrameTiming();
}

void GLRenderer::AddWholeScenePhaseTiming(sPhaseTiming& phase, u64 elapsedUS)
{
    phase.TotalUS += elapsedUS;
    if (elapsedUS > phase.MaxUS)
        phase.MaxUS = elapsedUS;
    phase.Count++;
}

void GLRenderer::Stop()
{
    // TODO clear buffers
    // TODO: do we even need this anymore?
}

void GLRenderer::PostSavestate()
{
    Reset();

    auto rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2D->PostSavestate();
    rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    rend2D->PostSavestate();
}


void GLRenderer::SetRenderSettings(RendererSettings& settings)
{
    SetScaleFactor(settings.ScaleFactor);
    PhysicalFinalUpscale =
        settings.WholeScene2D.Enabled &&
        settings.WholeScene2D.Mode == RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale &&
        settings.ScaleFactor > 1;

    auto rend2d = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2d->SetRenderSettings(settings.ScaleFactor, settings.WholeScene2D);

    rend2d = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    rend2d->SetRenderSettings(settings.ScaleFactor, settings.WholeScene2D);

    const bool finalLikeWholeSceneMode =
        settings.WholeScene2D.Mode == RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale ||
        settings.WholeScene2D.Mode == RendererSettings::WholeScene2DScaleMode::OverlayOperatorUpscale ||
        settings.WholeScene2D.Mode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;

    int scale3D = settings.ScaleFactor;
    if (settings.WholeScene2D.Enabled &&
        finalLikeWholeSceneMode &&
        settings.WholeScene2D.FinalUpscaleRender3DNative)
    {
        scale3D = 1;
    }

    if (IsCompute)
    {
        auto rend3d = dynamic_cast<ComputeRenderer3D *>(Rend3D.get());
        rend3d->SetRenderSettings(scale3D, settings.HiresCoordinates, settings.MSAA,
                                  settings.TextureFilter, settings.TextureScaling);
    }
    else
    {
        auto rend3d = dynamic_cast<GLRenderer3D *>(Rend3D.get());
        rend3d->SetRenderSettings(scale3D, settings.BetterPolygons, settings.ReadableTextureCache,
                                  settings.MSAA, settings.TextureFilter, settings.TextureScaling);
    }
}


void GLRenderer::SetScaleFactor(int scale)
{
    if (scale == ScaleFactor)
        return;

    ScaleFactor = scale;
    ScreenW = 256 * scale;
    ScreenH = 192 * scale;

    const GLenum fbassign2[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256*ScaleFactor, 256*ScaleFactor, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    memset(CaptureOutput256Valid, 0, sizeof(CaptureOutput256Valid));

    for (int i = 0; i < 4; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureOutput256FB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureOutput256Tex, 0, i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput128Tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 128*ScaleFactor, 128*ScaleFactor, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int i = 0; i < 16; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureOutput128FB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureOutput128Tex, 0, i);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    memset(&LastHighResDisplayCaptureEvent, 0, sizeof(LastHighResDisplayCaptureEvent));
    memset(HighResDisplayCapture256Event, 0, sizeof(HighResDisplayCapture256Event));
    memset(MainVRAMDisplayExactProductEvent, 0, sizeof(MainVRAMDisplayExactProductEvent));
    memset(ActiveCaptureBackgroundEpoch, 0, sizeof(ActiveCaptureBackgroundEpoch));
    memset(MainVRAMDisplayEpoch, 0, sizeof(MainVRAMDisplayEpoch));
    MainVRAMDisplayEpochInvalidationDebug = {};
    PendingVRAMCaptureSyncReason = 0;

    for (int i = 0; i < 4; i++)
    {
        glBindTexture(GL_TEXTURE_2D, HighResDisplayCaptureBackgroundTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, HighResDisplayCaptureBackgroundFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, HighResDisplayCaptureBackgroundTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, HighResDisplayCaptureFullTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, HighResDisplayCaptureFullFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, HighResDisplayCaptureFullTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, MainVRAMDisplayEpochTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, MainVRAMDisplayEpochFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, MainVRAMDisplayEpochTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, ActiveCaptureBackgroundEpochTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, ActiveCaptureBackgroundEpochFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ActiveCaptureBackgroundEpochTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureVRAMTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256*ScaleFactor, 256*ScaleFactor, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, CaptureVRAMFB);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureVRAMTex, 0, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[i]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, ScreenW, ScreenH, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, FPOutputFB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, FPOutputTex[i], 0, 0);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, FPOutputTex[i], 0, 1);
        glDrawBuffers(2, fbassign2);

        glBindTexture(GL_TEXTURE_2D_ARRAY, NativeFPOutputTex[i]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256, 192, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, NativeFPOutputFB[i]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeFPOutputTex[i], 0, 0);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, NativeFPOutputTex[i], 0, 1);
        glDrawBuffers(2, fbassign2);
    }

    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, PhysicalFinalNativeTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, PhysicalFinalNativeFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, PhysicalFinalNativeTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, PhysicalFinalScaledTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, PhysicalFinalScaledFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, PhysicalFinalScaledTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    if (RollingFinalDebugCaptureEnabled)
        EnsureRollingFinalDebugStorage();
    else
        ClearRollingFinalDebugCapture();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void GLRenderer::DrawScanline(u32 line)
{
    if (line == 0)
        ResetWholeSceneFrameTiming();
    const auto phaseStart = std::chrono::steady_clock::now();

    u32 dispcnt_a_diff = DispCntA ^ GPU.GPU2D_A.DispCnt;
    u32 dispcnt_b_diff = DispCntB ^ GPU.GPU2D_B.DispCnt;
    u32 capturecnt_diff = CaptureCnt ^ GPU.CaptureCnt;

    bool need_render = false;
    bool need_capture = false;

    if (dispcnt_a_diff & 0xF0000)
        need_render = true;
    else if (dispcnt_b_diff & 0x10000)
        need_render = true;
    else if (MasterBrightnessA != GPU.MasterBrightnessA ||
             MasterBrightnessB != GPU.MasterBrightnessB)
        need_render = true;

    if (GPU.CaptureEnable && (capturecnt_diff & 0x7FFFFFFF))
    {
        need_render = true;
        need_capture = true;
    }

    UpdateFinalPresentationTransitionGuard();

    NeedPartialRender = need_render;
    Rend2D_A->DrawScanline(line);
    Rend2D_B->DrawScanline(line);

    if (need_render && (line > 0))
    {
        RenderScreen(LastLine, line);
        LastLine = line;
    }

    if (need_capture && (line > 0))
    {
        DoCapture(LastCapLine, line);
        LastCapLine = line;
    }

    DispCntA = GPU.GPU2D_A.DispCnt;
    DispCntB = GPU.GPU2D_B.DispCnt;
    MasterBrightnessA = GPU.MasterBrightnessA;
    MasterBrightnessB = GPU.MasterBrightnessB;
    CaptureCnt = GPU.CaptureCnt;

    FinalPassConfig.uScreenSwap[line] = GPU.ScreenSwap;

    u32 dispcnt = GPU.GPU2D_A.DispCnt;
    u32 dispmode = (dispcnt >> 16) & 0x3;
    u32 capcnt = GPU.CaptureCnt;
    u32 capsel = (capcnt >> 29) & 0x3;
    u32 capA = (capcnt >> 24) & 0x1;
    u32 capB = (capcnt >> 25) & 0x1;
    bool checkcap = GPU.CaptureEnable && (capsel != 0);

    if (GPU.CaptureEnable && (capsel != 1))
    {
        if (capA == 0)
            CaptureConfig.uSrcAOffset[line] = 0;
        else
        {
            int xpos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
            xpos -= ((xpos & 0x100) << 1);
            CaptureConfig.uSrcAOffset[line] = (float)xpos / 256.f;
        }
    }

    if ((dispmode == 2) || (checkcap && (capB == 0)))
    {
        AuxUsageMask |= (1<<0);

        u32 vrambank = (dispcnt >> 18) & 0x3;
        u32 vramoffset = line * 256;
        u32 outoffset = line * 256;
        if (dispmode != 2)
        {
            u32 yoff = ((capcnt >> 26) & 0x3) << 14;
            vramoffset += yoff;
            outoffset += yoff;
        }

        vramoffset &= 0xFFFF;
        outoffset &= 0xFFFF;

        u16* adst = &AuxInputBuffer[0][outoffset];

        if (GPU.VRAMMap_LCDC & (1<<vrambank))
        {
            u16* vram = (u16*)GPU.VRAM[vrambank];

            for (int i = 0; i < 256; i++)
            {
                adst[i] = vram[vramoffset];
                vramoffset++;
            }
        }
        else
        {
            for (int i = 0; i < 256; i++)
            {
                adst[i] = 0;
            }
        }
    }

    if ((dispmode == 3) || (checkcap && (capB == 1)))
    {
        AuxUsageMask |= (1<<1);

        u16* adst = &AuxInputBuffer[1][line * 256];
        for (int i = 0; i < 256; i++)
        {
            adst[i] = GPU.DispFIFOBuffer[i];
        }
    }

    AddWholeScenePhaseTiming(WholeSceneFrameTiming.DrawScanline, ElapsedUS(phaseStart));
}

void GLRenderer::DrawSprites(u32 line)
{
    const auto phaseStart = std::chrono::steady_clock::now();
    Rend2D_A->DrawSprites(line);
    Rend2D_B->DrawSprites(line);
    AddWholeScenePhaseTiming(WholeSceneFrameTiming.DrawSprites, ElapsedUS(phaseStart));
}

void GLRenderer::Start3DRendering()
{
    const auto phaseStart = std::chrono::steady_clock::now();
    Rend3D->RenderFrame();
    bool output3DUpdated = true;
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        output3DUpdated = !rend3d->WasLastRenderFrameSkipped();
    else if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        output3DUpdated = !rend3d->WasLastRenderFrameSkipped();
    if (output3DUpdated)
        Output3DSerial++;
    Output3DSceneHash = GPU.GPU3D.GetRenderSceneHash();
    AddWholeScenePhaseTiming(WholeSceneFrameTiming.Start3D, ElapsedUS(phaseStart));
}

void GLRenderer::Finish3DRendering()
{
    const auto phaseStart = std::chrono::steady_clock::now();
    Rend3D->FinishRendering();
    AddWholeScenePhaseTiming(WholeSceneFrameTiming.Finish3D, ElapsedUS(phaseStart));
}

void GLRenderer::Restart3DRendering()
{
    const auto phaseStart = std::chrono::steady_clock::now();
    Rend3D->RestartFrame();
    AddWholeScenePhaseTiming(WholeSceneFrameTiming.Restart3D, ElapsedUS(phaseStart));
}


void GLRenderer::RenderFinalPassToFramebuffer(int ystart,
                                              int yend,
                                              GLuint targetFB,
                                              int viewportW,
                                              int viewportH,
                                              int framebufferScale,
                                              GLuint mainInputTex,
                                              GLuint subInputTex,
                                              bool mainInputReplacesVRAMDisplay)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, viewportW, viewportH);

    // TODO: adjust incoming vertices instead of doing this?
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * framebufferScale, viewportW, (yend-ystart) * framebufferScale);

    int vramcap = -1;
    if (AuxUsageMask & (1<<0))
    {
        u32 vrambank = (DispCntA >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<vrambank))
            vramcap = GPU.GetCaptureBlock_LCDC(vrambank << 17);
    }
    Aux0VRAMCap = vramcap;

    if (!GPU.ScreensEnabled)
    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    else
    {
        glUseProgram(FPShader);

        const u32 modeA = (DispCntA >> 16) & 0x3;
        FinalPassConfig.uScaleFactor = ScaleFactor;
        FinalPassConfig.uDispModeA = (mainInputReplacesVRAMDisplay && modeA == 2) ? 1 : modeA;
        FinalPassConfig.uDispModeB = (DispCntB >> 16) & 0x1;
        FinalPassConfig.uBrightModeA = (MasterBrightnessA >> 14) & 0x3;
        FinalPassConfig.uBrightModeB = (MasterBrightnessB >> 14) & 0x3;
        FinalPassConfig.uBrightFactorA = std::min(MasterBrightnessA & 0x1F, 16);
        FinalPassConfig.uBrightFactorB = std::min(MasterBrightnessB & 0x1F, 16);

        const auto packedMasterBrightness = [](u16 masterBrightness)
        {
            const u32 mode = (masterBrightness >> 14) & 0x3;
            const u32 factor = std::min<u32>(masterBrightness & 0x1F, 16);
            return (mode << 8) | factor;
        };
        const auto masterBrightnessActive = [](u16 masterBrightness)
        {
            const u32 mode = (masterBrightness >> 14) & 0x3;
            const u32 factor = std::min<u32>(masterBrightness & 0x1F, 16);
            return (mode == 1 || mode == 2) && factor > 0;
        };
        const auto outputHasBrightnessProof =
            [&](const GLRenderer2D* renderer,
                u16 masterBrightness,
                WholeSceneCaptureEffectOwner effectOwner)
        {
            if (!renderer || !masterBrightnessActive(masterBrightness))
                return false;

            const auto& trace = renderer->WholeSceneTrace;
            return ystart >= trace.YStart &&
                   yend <= trace.YEnd &&
                   trace.OutputPresentationMasterBrightnessApplied &&
                   trace.OutputPresentationEffectOwner == static_cast<u32>(effectOwner) &&
                   trace.OutputPresentationEffectState == packedMasterBrightness(masterBrightness);
        };

        const auto* rendA = dynamic_cast<const GLRenderer2D*>(Rend2D_A.get());
        const bool mainInputBrightnessAlreadyApplied =
            outputHasBrightnessProof(rendA,
                                     MasterBrightnessA,
                                     WholeSceneCaptureEffectOwner::CurrentEngine) ||
            outputHasBrightnessProof(rendA,
                                     MasterBrightnessA,
                                     WholeSceneCaptureEffectOwner::SourceA);
        if (mainInputBrightnessAlreadyApplied)
        {
            FinalPassConfig.uBrightModeA = 0;
            FinalPassConfig.uBrightFactorA = 0;
        }

        const auto* rendB = dynamic_cast<const GLRenderer2D*>(Rend2D_B.get());
        const bool subInputBrightnessAlreadyApplied =
            outputHasBrightnessProof(rendB,
                                     MasterBrightnessB,
                                     WholeSceneCaptureEffectOwner::CurrentEngine) ||
            (rendB &&
             rendB->WholeSceneTrace.Path ==
                 GLRenderer2D::WholeSceneRenderPath::SourceACaptureReplacement &&
             outputHasBrightnessProof(rendB,
                                      MasterBrightnessA,
                                      WholeSceneCaptureEffectOwner::SourceA));
        if (subInputBrightnessAlreadyApplied)
        {
            FinalPassConfig.uBrightModeB = 0;
            FinalPassConfig.uBrightFactorB = 0;
        }

        if (AuxUsageMask)
        {
            glBindTexture(GL_TEXTURE_2D_ARRAY, AuxInputTex);
            if ((AuxUsageMask & (1<<0)) && (vramcap == -1))
            {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 256, 256, 1, GL_RGBA,
                                GL_UNSIGNED_SHORT_1_5_5_5_REV, AuxInputBuffer[0]);
            }
            if (AuxUsageMask & (1<<1))
            {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, 256, 192, 1, GL_RGBA,
                                GL_UNSIGNED_SHORT_1_5_5_5_REV, AuxInputBuffer[1]);
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mainInputTex);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, subInputTex);

        glActiveTexture(GL_TEXTURE2);
        bool invalidTrackedFeedbackReseed = false;
        if (!mainInputReplacesVRAMDisplay && (modeA == 2) && (vramcap != -1))
        {
            const u32 trackedBank = static_cast<u32>(vramcap) >> 2;
            const u32 trackedOffset = static_cast<u32>(vramcap) & 0x3u;
            const u32 capcnt = CaptureCnt;
            const u32 captureSrcB = (capcnt >> 25) & 0x1u;
            const u32 captureDstBlock = (capcnt >> 16) & 0x3u;
            const u32 captureDstOffset = (capcnt >> 18) & 0x3u;
            const u32 captureCapSize = (capcnt >> 20) & 0x3u;
            const u32 captureDstMode = (capcnt >> 29) & 0x3u;
            const u32 captureEVA = std::min(capcnt & 0x1Fu, 16u);
            invalidTrackedFeedbackReseed =
                trackedBank < 4 &&
                !CaptureOutput256Valid[trackedBank] &&
                captureSrcB == 0 &&
                captureDstBlock == trackedBank &&
                captureDstOffset == trackedOffset &&
                captureCapSize != 0 &&
                captureDstMode == 2 &&
                captureEVA > 0;

            if (invalidTrackedFeedbackReseed)
            {
                FinalPassConfig.uDispModeA = 1;
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
                FinalPassConfig.uAuxLayer = vramcap >> 2;
                FinalPassConfig.uAuxColorFactor = 63.75f;
            }
        }
        else if (!mainInputReplacesVRAMDisplay && modeA >= 2)
        {
            glBindTexture(GL_TEXTURE_2D_ARRAY, AuxInputTex);
            FinalPassConfig.uAuxLayer = (modeA - 2);
            FinalPassConfig.uAuxColorFactor = 62.f;
        }
        FinalPassInvalidCaptureReseed = FinalPassInvalidCaptureReseed || invalidTrackedFeedbackReseed;

        glBindBuffer(GL_UNIFORM_BUFFER, FPConfigUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FinalPassConfig), &FinalPassConfig);

        glBindBuffer(GL_ARRAY_BUFFER, FPVertexBufferID);
        glBindVertexArray(FPVertexArrayID);
        glDrawArrays(GL_TRIANGLES, 0, 2*3);
    }

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer::RenderMainVRAMDisplayNativeFallbackUpscale(int backbuf, int ystart, int yend)
{
    auto* scaler = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    if (!scaler)
        return;

    const int y0 = std::max(0, std::min(192, ystart));
    const int y1 = std::max(y0, std::min(192, yend));
    if (y0 >= y1)
        return;

    bool mainBottom = IsEngineRoutedToFinalBottom(0, y0, y1);
    const int layer = mainBottom ? 1 : 0;

    RenderFinalPassToFramebuffer(ystart, yend,
                                 NativeFPOutputFB[backbuf],
                                 256, 192, 1,
                                 OutputTex2D[0],
                                 OutputTex2D[1]);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, NativeFPOutputLayerReadFB[layer]);
    glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              NativeFPOutputTex[backbuf], 0, layer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PhysicalFinalNativeFB[layer]);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glBlitFramebuffer(0, y0, 256, y1,
                      0, y0, 256, y1,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    scaler->RenderNativeFinalUpscaleToTexture(PhysicalFinalNativeTex[layer],
                                              PhysicalFinalScaledTex[layer]);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, PhysicalFinalScaledFB[layer]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PhysicalFinalOutputLayerFB[layer]);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              FPOutputTex[backbuf], 0, layer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glBlitFramebuffer(0, y0 * ScaleFactor, ScreenW, y1 * ScaleFactor,
                      0, y0 * ScaleFactor, ScreenW, y1 * ScaleFactor,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

bool GLRenderer::EnsureRollingFinalDebugStorage()
{
    if (!RollingFinalDebugCaptureEnabled || RollingFinalDebugCapacity <= 0)
        return false;

    if (RollingFinalDebugTex == 0)
        glGenTextures(1, &RollingFinalDebugTex);
    if (RollingFinalDebugFB == 0)
        glGenFramebuffers(1, &RollingFinalDebugFB);

    if (RollingFinalDebugWidth == ScreenW &&
        RollingFinalDebugHeight == ScreenH &&
        static_cast<int>(RollingFinalDebugSlots.size()) == RollingFinalDebugCapacity)
    {
        return true;
    }

    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevArrayBinding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevArrayBinding);

    glBindTexture(GL_TEXTURE_2D_ARRAY, RollingFinalDebugTex);
    glTexParams(GL_TEXTURE_2D_ARRAY, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,
                 0,
                 GL_RGBA,
                 ScreenW,
                 ScreenH,
                 RollingFinalDebugCapacity * 2,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);
    glBindTexture(GL_TEXTURE_2D_ARRAY, prevArrayBinding);
    glActiveTexture(prevActiveTexture);

    RollingFinalDebugWidth = ScreenW;
    RollingFinalDebugHeight = ScreenH;
    RollingFinalDebugWriteIndex = 0;
    RollingFinalDebugSlots.assign(RollingFinalDebugCapacity, {});
    return true;
}

void GLRenderer::ClearRollingFinalDebugCapture()
{
    RollingFinalDebugWriteIndex = 0;
    RollingFinalDebugSlots.assign(RollingFinalDebugCapacity > 0 ? RollingFinalDebugCapacity : 0, {});
}

void GLRenderer::SetWholeScene2DTimingFrame(u64 frame, bool valid)
{
    WholeSceneTimingFrame = frame;
    WholeSceneTimingFrameValid = valid;
}

WholeScene2DFinalDebugFrame GLRenderer::CaptureRollingFinalDebugMetadata(u64 serial) const
{
    auto fillEngine = [](const GLRenderer2D* renderer) -> WholeScene2DEngineDebugIdentity
    {
        WholeScene2DEngineDebugIdentity identity;
        if (!renderer)
            return identity;

        const auto& trace = renderer->WholeSceneTrace;
        identity.Path = static_cast<int>(trace.Path);
        identity.ProductChoice = static_cast<int>(trace.SourceAProductChoice);
        identity.SourceAResolutionMode = static_cast<int>(trace.SourceACaptureMode);
        identity.ChosenProductKind = static_cast<int>(trace.SourceAChosenProductKind);
        identity.ChosenProductRenderAction = static_cast<int>(trace.SourceAChosenProductRenderAction);
        identity.ChosenProductTex = trace.SourceAChosenProductTex;
        identity.ChosenProductCaptureBank = trace.SourceAChosenProductCaptureBank;
        identity.ChosenProductBackgroundEpochSerial = trace.SourceAChosenProductBackgroundEpochSerial;
        identity.ChosenProductSource3DSerial = trace.SourceAChosenProductSource3DSerial;
        identity.ChosenProductCaptureEventSerial = trace.SourceAChosenProductCaptureEventSerial;
        identity.ChosenProductCapturePresentationHash = trace.SourceAChosenProductCapturePresentationHash;
        identity.ChosenProductCurrentPresentationHash = trace.SourceAChosenProductCurrentPresentationHash;
        identity.RequestCapturePresentationHash = trace.SourceACapturePresentationHash;
        identity.RequestCurrentPresentationHash = trace.SourceACurrentPresentationHash;
        return identity;
    };

    WholeScene2DFinalDebugFrame frame;
    frame.Serial = serial;
    frame.TimingFrameValid = WholeSceneTimingFrameValid;
    frame.TimingFrame = WholeSceneTimingFrame;

    const int finalDispModeA = (DispCntA >> 16) & 0x3;
    const int finalDispModeB = (DispCntB >> 16) & 0x1;
    const int finalScreenSwap = FinalPassConfig.uScreenSwap[0] ? 1 : 0;
    const int finalMainVRAMBank = finalDispModeA == 2 ? static_cast<int>((DispCntA >> 18) & 0x3) : -1;
    bool finalVRAMDisplayUsedEpoch = false;
    const bool finalVRAMDisplayReplacementEligible =
        finalDispModeA == 2 &&
        finalMainVRAMBank >= 0 &&
        CanUseMainVRAMDisplayHighResCaptureReplacement(static_cast<u32>(finalMainVRAMBank),
                                                       nullptr,
                                                       nullptr,
                                                       &finalVRAMDisplayUsedEpoch);
    (void)finalVRAMDisplayUsedEpoch;

    const int finalMainSource =
        FinalPassInvalidCaptureReseed ? 6 :
        (finalDispModeA == 0) ? 0 :
        (finalDispModeA == 1) ? 1 :
        (finalDispModeA == 2 && (Aux0VRAMCap != -1 || finalVRAMDisplayReplacementEligible)) ? 3 :
        (finalDispModeA == 2) ? 4 :
        (finalDispModeA == 3) ? 5 : -1;
    const int finalSubSource = finalDispModeB == 0 ? 0 : 2;
    frame.FinalTopSource = finalScreenSwap ? finalMainSource : finalSubSource;
    frame.FinalBottomSource = finalScreenSwap ? finalSubSource : finalMainSource;

    frame.EngineA = fillEngine(dynamic_cast<const GLRenderer2D*>(Rend2D_A.get()));
    frame.EngineB = fillEngine(dynamic_cast<const GLRenderer2D*>(Rend2D_B.get()));
    return frame;
}

void GLRenderer::CaptureRollingFinalDebugFrame(int backbuf)
{
    if (!RollingFinalDebugCaptureEnabled || !EnsureRollingFinalDebugStorage())
        return;

    GLint prevReadFB = 0;
    GLint prevDrawFB = 0;
    GLint prevReadBuffer = GL_COLOR_ATTACHMENT0;
    GLint prevDrawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_NONE};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFB);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFB);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
    glGetIntegerv(GL_DRAW_BUFFER0, &prevDrawBuffers[0]);
    glGetIntegerv(GL_DRAW_BUFFER1, &prevDrawBuffers[1]);

    const int slot = RollingFinalDebugWriteIndex;
    const int topLayer = slot * 2;
    const int bottomLayer = topLayer + 1;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, FPOutputFB[backbuf]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, RollingFinalDebugFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, RollingFinalDebugTex, 0, topLayer);
    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, RollingFinalDebugTex, 0, bottomLayer);
    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    const u64 serial = RollingFinalDebugSerial++;
    RollingFinalDebugSlots[slot].Valid = true;
    RollingFinalDebugSlots[slot].Serial = serial;
    RollingFinalDebugSlots[slot].Metadata = CaptureRollingFinalDebugMetadata(serial);
    RollingFinalDebugWriteIndex = (RollingFinalDebugWriteIndex + 1) % RollingFinalDebugCapacity;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFB);
    glReadBuffer(prevReadBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFB);
    if (prevDrawFB != 0 && prevDrawBuffers[1] != GL_NONE)
    {
        const GLenum drawBuffers[2] = {
            static_cast<GLenum>(prevDrawBuffers[0]),
            static_cast<GLenum>(prevDrawBuffers[1]),
        };
        glDrawBuffers(2, drawBuffers);
    }
    else
    {
        glDrawBuffer(prevDrawBuffers[0]);
    }
}

bool GLRenderer::CanRenderPhysicalFinalUpscaleForRange(int ystart, int yend) const
{
    if (!PhysicalFinalUpscale)
        return false;

    auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    if (!rendA || !rendB)
        return false;

    auto rendererHasPhysicalFinalInputForRange = [](const GLRenderer2D* renderer)
    {
        return renderer->WholeSceneTrace.Path == GLRenderer2D::WholeSceneRenderPath::PhysicalFinalPostprocessInput &&
               renderer->WholeSceneTrace.PhysicalFinalNativeInputValid;
    };

    const u32 dispModeA = (DispCntA >> 16) & 0x3u;
    const u32 dispModeB = (DispCntB >> 16) & 0x1u;
    const bool mainNeedsEngineInput = dispModeA == 1;
    const bool subNeedsEngineInput = dispModeB != 0;

    return (!mainNeedsEngineInput || rendererHasPhysicalFinalInputForRange(rendA)) &&
           (!subNeedsEngineInput || rendererHasPhysicalFinalInputForRange(rendB));
}

bool GLRenderer::CanUpscaleMainVRAMDisplayNativeFallbackForRange(int ystart, int yend) const
{
    if (ScaleFactor <= 1 ||
        ystart > 0 ||
        yend < 192 ||
        ((DispCntA >> 16) & 0x3u) != 2)
    {
        return false;
    }

    bool screenSwap = GPU.ScreenSwap;
    if (!GetFinalPassScreenSwapForRange(ystart, yend, screenSwap))
        return false;

    const auto* rendA = dynamic_cast<const GLRenderer2D*>(Rend2D_A.get());
    return rendA &&
           rendA->WholeSceneScaleRequested &&
           rendA->WholeSceneScaleCaptureBacked &&
           rendA->WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
}

void GLRenderer::RenderScreen(int ystart, int yend)
{
    const auto phaseStart = std::chrono::steady_clock::now();

    const int backbuf = BackBuffer;
    auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    PhysicalFinalNativeInputValid[0] = rendA && rendA->WholeSceneTrace.PhysicalFinalNativeInputValid;
    PhysicalFinalNativeInputValid[1] = rendB && rendB->WholeSceneTrace.PhysicalFinalNativeInputValid;
    PhysicalFinalNativeInputPath[0] = rendA ? static_cast<int>(rendA->WholeSceneTrace.Path) : 0;
    PhysicalFinalNativeInputPath[1] = rendB ? static_cast<int>(rendB->WholeSceneTrace.Path) : 0;

    if (CanRenderPhysicalFinalUpscaleForRange(ystart, yend))
    {
        const GLuint nativeMainInput =
            PhysicalFinalNativeInputValid[0] ? rendA->NativeOutputTex : OutputTex2D[0];
        const GLuint nativeSubInput =
            PhysicalFinalNativeInputValid[1] ? rendB->NativeOutputTex : OutputTex2D[1];
        RenderFinalPassToFramebuffer(ystart, yend,
                                     NativeFPOutputFB[backbuf],
                                     256, 192, 1,
                                     nativeMainInput,
                                     nativeSubInput);

        const int y0 = std::max(0, std::min(192, ystart));
        const int y1 = std::max(y0, std::min(192, yend));
        for (int y = y0; y < y1; y++)
        {
            if (!PhysicalFinalNativeRowValid[y])
            {
                PhysicalFinalNativeRowValid[y] = true;
                PhysicalFinalNativeValidRows++;
            }
        }
    }

    // Keep the existing per-engine scaled final pass populated. Display capture
    // and non-postprocess fallbacks still depend on OutputTex2D semantics.
    GLuint scaledMainInput = OutputTex2D[0];
    bool scaledMainInputReplacesVRAMDisplay = false;
    int scaledMainVRAMDisplayBank = -1;
    ResetFinalVRAMDisplayTrace(FinalVRAMDisplayRenderTrace, -1);
    if (((DispCntA >> 16) & 0x3) == 2)
    {
        const u32 displayBank = (DispCntA >> 18) & 0x3;
        ResetFinalVRAMDisplayTrace(FinalVRAMDisplayRenderTrace,
                                   static_cast<int>(displayBank));

        GLuint replacementTex = 0;
        int replacementRejectReason = 0;
        bool replacementUsedEpoch = false;
        if (CanUseMainVRAMDisplayHighResCaptureReplacement(displayBank,
                                                           &replacementTex,
                                                           &replacementRejectReason,
                                                           &replacementUsedEpoch,
                                                           &FinalVRAMDisplayRenderTrace))
        {
            scaledMainInput = replacementTex;
            scaledMainInputReplacesVRAMDisplay = true;
            scaledMainVRAMDisplayBank = static_cast<int>(displayBank);
            RecordFinalVRAMDisplayTraceAccepted(displayBank,
                                                replacementTex,
                                                replacementUsedEpoch);
        }
        else
        {
            RecordFinalVRAMDisplayTraceRejected(replacementRejectReason);
        }
    }
    else
    {
        RecordFinalVRAMDisplayTraceRejected(1);
    }

    RenderFinalPassToFramebuffer(ystart, yend,
                                 FPOutputFB[backbuf],
                                 ScreenW, ScreenH, ScaleFactor,
                                 scaledMainInput,
                                 OutputTex2D[1],
                                 scaledMainInputReplacesVRAMDisplay);
    if (scaledMainInputReplacesVRAMDisplay &&
        scaledMainVRAMDisplayBank >= 0 &&
        scaledMainVRAMDisplayBank < 4)
    {
        const auto& epoch = MainVRAMDisplayEpoch[scaledMainVRAMDisplayBank];
        if (epoch.HasDirtyRows && epoch.DirtyYStart < epoch.DirtyYEnd)
            RenderMainVRAMDisplayNativeFallbackUpscale(backbuf, epoch.DirtyYStart, epoch.DirtyYEnd);
    }
    else if (CanUpscaleMainVRAMDisplayNativeFallbackForRange(ystart, yend))
    {
        RenderMainVRAMDisplayNativeFallbackUpscale(backbuf, ystart, yend);
    }

    AddWholeScenePhaseTiming(WholeSceneFrameTiming.RenderScreen, ElapsedUS(phaseStart));
}

bool GLRenderer::RenderPhysicalFinalUpscale()
{
    PhysicalFinalPostprocessApplied = false;
    PhysicalFinalPostprocessRejectReason = 0;

    if (!PhysicalFinalUpscale)
    {
        PhysicalFinalPostprocessRejectReason = 1;
        return false;
    }
    if (PhysicalFinalNativeValidRows < 192)
    {
        PhysicalFinalPostprocessRejectReason = 2;
        return false;
    }

    auto* scaler = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    if (!scaler)
    {
        PhysicalFinalPostprocessRejectReason = 3;
        return false;
    }

    const int backbuf = BackBuffer;
    for (int layer = 0; layer < 2; layer++)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, NativeFPOutputLayerReadFB[layer]);
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  NativeFPOutputTex[backbuf], 0, layer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PhysicalFinalNativeFB[layer]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);

        glBlitFramebuffer(0, 0, 256, 192,
                          0, 0, 256, 192,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);

        scaler->RenderNativeFinalUpscaleToTexture(PhysicalFinalNativeTex[layer],
                                                  PhysicalFinalScaledTex[layer]);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, PhysicalFinalScaledFB[layer]);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, PhysicalFinalOutputLayerFB[layer]);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  FPOutputTex[backbuf], 0, layer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);

        glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                          0, 0, ScreenW, ScreenH,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    PhysicalFinalPostprocessApplied = true;
    return true;
}

void GLRenderer::VBlank()
{
    const auto phaseStart = std::chrono::steady_clock::now();

    Rend2D_A->VBlank();
    Rend2D_B->VBlank();

    RenderScreen(LastLine, 192);

    if (GPU.CaptureEnable)
        DoCapture(LastCapLine, 192);

    RenderPhysicalFinalUpscale();

    LastLine = 0;
    LastCapLine = 0;
    AddWholeScenePhaseTiming(WholeSceneFrameTiming.VBlank, ElapsedUS(phaseStart));
}

void GLRenderer::VBlankEnd()
{
    const auto phaseStart = std::chrono::steady_clock::now();
    AuxUsageMask = 0;
    AddWholeScenePhaseTiming(WholeSceneFrameTiming.VBlankEnd, ElapsedUS(phaseStart));
}


void GLRenderer::DoCapture(int ystart, int yend)
{
    const auto phaseStart = std::chrono::steady_clock::now();

    u32 dispcnt = DispCntA;
    u32 capcnt = CaptureCnt;
    u32 dispmode = (dispcnt >> 16) & 0x3;
    u32 srcA = (capcnt >> 24) & 0x1;
    u32 srcB = (capcnt >> 25) & 0x1;
    u32 srcBblock = (dispcnt >> 18) & 0x3;
    u32 srcBoffset = (dispmode == 2) ? 0 : ((capcnt >> 26) & 0x3);
    u32 dstblock = (capcnt >> 16) & 0x3;
    u32 dstoffset = (capcnt >> 18) & 0x3;
    u32 capsize = (capcnt >> 20) & 0x3;
    u32 dstmode = (capcnt >> 29) & 0x3;
    u32 eva = std::min(capcnt & 0x1F, 16u);
    u32 evb = std::min((capcnt >> 8) & 0x1F, 16u);

    // determine the region we're going to capture to

    int dstwidth, dstheight;

    if (capsize == 0)
    {
        dstwidth = 128;
        dstheight = 128;
    }
    else
    {
        dstwidth = 256;
        dstheight = 64 * capsize;
    }

    if (ystart >= dstheight)
    {
        AddWholeScenePhaseTiming(WholeSceneFrameTiming.DoCapture, ElapsedUS(phaseStart));
        return;
    }
    if (yend > dstheight)
        yend = dstheight;

    glUseProgram(CaptureShader);

    GLuint inputA;
    if (srcA)
        inputA = OutputTex3D;
    else
        inputA = OutputTex2D[0];

    FinalCaptureSourceDebug = {};
    FinalCaptureSourceDebug.SourceAKind =
        srcA ? FinalCaptureSourceKind::NormalOutputTex3D
             : FinalCaptureSourceKind::NormalOutputTex2D;

    auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    const bool finalNativeCaptureEligible =
        ystart == 0 &&
        yend >= 192 &&
        IsFullDisplayCaptureFromSourceAOnly(capcnt);
    const bool finalNativeRangeValid =
        PhysicalFinalUpscale &&
        finalNativeCaptureEligible &&
        rendA &&
        rendA->WholeSceneTrace.Path == GLRenderer2D::WholeSceneRenderPath::PhysicalFinalPostprocessInput &&
        rendA->WholeSceneTrace.PhysicalFinalNativeInputValid &&
        ystart >= rendA->WholeSceneTrace.YStart &&
        yend <= rendA->WholeSceneTrace.YEnd;

    if (finalNativeRangeValid)
    {
        if (!srcA)
        {
            FinalCaptureSourceDebug.SourceAKind = FinalCaptureSourceKind::FinalNativeEngineOutput;
            FinalCaptureSourceDebug.SourceANativeSized = true;
            FinalCaptureSourceDebug.SourceARangeValid = true;
            FinalCaptureSourceDebug.SourceAUsed = true;
            inputA = rendA->NativeOutputTex;
        }
        else if (rendA->WholeSceneTrace.Native3DResolveValid)
        {
            FinalCaptureSourceDebug.SourceAKind = FinalCaptureSourceKind::FinalNativeResolved3D;
            FinalCaptureSourceDebug.SourceANativeSized = true;
            FinalCaptureSourceDebug.SourceARangeValid = true;
            FinalCaptureSourceDebug.SourceAUsed = true;
            inputA = rendA->NativeDirect3DTex;
        }
    }

    bool useSrcB = (dstmode == 1) || (dstmode == 2 && evb > 0);

    GLuint inputB = AuxInputTex;
    u32 layerB = srcB;
    CaptureConfig.uSrcBColorFactor = 248.f;
    bool srcBUsesTrackedCapture = false;
    bool srcBSameDstBank = false;
    int srcBTrackedLayer = -1;

    int srcBVRAMCap = -1;
    if (useSrcB && srcB == 0 && (GPU.VRAMMap_LCDC & (1u << srcBblock)))
        srcBVRAMCap = GPU.GetCaptureBlock_LCDC((srcBblock << 17) + (srcBoffset << 15));

    const bool srcBWouldUseTrackedCapture =
        srcB == 0 &&
        srcBVRAMCap != -1 &&
        srcBblock < 4;
    const bool srcBHasTrackedCapture =
        srcBWouldUseTrackedCapture &&
        CaptureOutput256Valid[srcBblock];
    const bool reseedInvalidTrackedFeedback =
        useSrcB &&
        srcBWouldUseTrackedCapture &&
        !srcBHasTrackedCapture &&
        dstmode == 2 &&
        eva > 0;

    if (useSrcB && srcBHasTrackedCapture)
    {
        // hi-res VRAM
        srcBUsesTrackedCapture = true;
        srcBSameDstBank = dstblock == srcBblock;
        srcBTrackedLayer = srcBVRAMCap;
        if (dstblock == srcBblock)
        {
            // we are reading from the same block we are capturing to
            // on hardware, it would read the old VRAM contents, then write new stuff
            // but we can't do that with OpenGL
            // so we need to blit it to a temporary framebuffer

            int blitY0 = (srcBoffset * 64) + ystart;
            int blitY1 = (srcBoffset * 64) + yend;

            if (dstoffset != srcBoffset)
                Log(LogLevel::Error, "GPU_OpenGL: MISMATCHED VRAM OFFSETS ON SAME BANK!!! bank=%d src=%d dst=%d\n",
                       dstblock, srcBoffset, dstoffset);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureOutput256FB[srcBblock]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureVRAMFB);

            if (blitY1 > 256)
            {
                // wraparound
                glBlitFramebuffer(0, blitY0*ScaleFactor, 256*ScaleFactor, 256*ScaleFactor,
                                  0, blitY0*ScaleFactor, 256*ScaleFactor, 256*ScaleFactor,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBlitFramebuffer(0, 0, 256*ScaleFactor, (blitY1-256)*ScaleFactor,
                                  0, 0, 256*ScaleFactor, (blitY1-256)*ScaleFactor,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            else
            {
                // straightforward
                glBlitFramebuffer(0, blitY0*ScaleFactor, 256*ScaleFactor, blitY1*ScaleFactor,
                                  0, blitY0*ScaleFactor, 256*ScaleFactor, blitY1*ScaleFactor,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }

            inputB = CaptureVRAMTex;
            layerB = 0;
        }
        else
        {
            // if it's a different bank, we can just use it as-is
            inputB = CaptureOutput256Tex;
            layerB = srcBblock;
        }

        CaptureConfig.uSrcBColorFactor = 255.f;
    }

    sLastDisplayCaptureDebug capture = {};
    capture.Valid = true;
    capture.CaptureCnt = capcnt;
    capture.YStart = ystart;
    capture.YEnd = yend;
    capture.DstWidth = dstwidth;
    capture.DstHeight = dstheight;
    capture.SrcA = srcA;
    capture.SrcB = srcB;
    capture.SrcBBlock = srcBblock;
    capture.SrcBOffset = srcBoffset;
    capture.DstBlock = dstblock;
    capture.DstOffset = dstoffset;
    capture.CapSize = capsize;
    capture.DstMode = dstmode;
    capture.EVA = eva;
    capture.EVB = evb;
    capture.UsesSrcB = useSrcB;
    capture.SrcBUsesTrackedCapture = srcBUsesTrackedCapture;
    capture.SrcBSameDstBank = srcBSameDstBank;
    capture.SrcBTrackedLayer = srcBTrackedLayer;
    capture.FinalNativeSourceA = FinalCaptureSourceDebug.SourceAUsed;
    capture.FinalNativeSourceAKind = static_cast<u32>(FinalCaptureSourceDebug.SourceAKind);
    RecordDisplayCaptureDebug(capture);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (capsize == 0)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureOutput128FB[(dstblock << 2) | dstoffset]);
        glViewport(0, 0, 128*ScaleFactor, 128*ScaleFactor);
    }
    else
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureOutput256FB[dstblock]);
        glViewport(0, 0, 256*ScaleFactor, 256*ScaleFactor);
    }

    CaptureConfig.uInvCaptureSize[0] = 1.f / (float)dstwidth;
    CaptureConfig.uInvCaptureSize[1] = 1.f / (float)dstheight;

    CaptureConfig.uSrcALayer = srcA;

    if (srcB == 0)
        CaptureConfig.uSrcBOffset = 64 * srcBoffset;
    else
        CaptureConfig.uSrcBOffset = 0;

    CaptureConfig.uSrcBLayer = layerB;

    // After a renderer reset or savestate load, the CPU-side capture flags can
    // identify a feedback source before this GL capture layer has been rebuilt.
    // Do not seed the new high-res feedback chain from stale raw VRAM in that case.
    CaptureConfig.uDstMode = reseedInvalidTrackedFeedback ? 0 : dstmode;
    CaptureConfig.uBlendFactors[0] = eva;
    CaptureConfig.uBlendFactors[1] = evb;

    glBindBuffer(GL_UNIFORM_BUFFER, CaptureConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CaptureConfig), &CaptureConfig);

    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, inputB);
    if (useSrcB && !reseedInvalidTrackedFeedback && inputB == AuxInputTex)
    {
        if (srcB == 0)
        {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 256, 256, 1, GL_RGBA,
                            GL_UNSIGNED_SHORT_1_5_5_5_REV, AuxInputBuffer[0]);
        }
        else
        {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, 256, 192, 1, GL_RGBA,
                            GL_UNSIGNED_SHORT_1_5_5_5_REV, AuxInputBuffer[1]);
        }
    }

    u16 vtxbuf[12 * 4];
    u16* vptr = vtxbuf;
    int numvtx;

    // y0/y1 = coordinates in destination buffer
    // t0/t1 = coordinates in source buffers
    if (capsize == 0) dstoffset = 0;
    int y0 = (dstoffset * 64) + ystart;
    int y1 = (dstoffset * 64) + yend;
    int t0 = ystart;
    int t1 = yend;

    int bufferheight = (capsize == 0) ? 128 : 256;
    if (y1 > bufferheight)
    {
        // wraparound
        int y2 = bufferheight;
        int t2 = t0 + (y2 - y0);
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t2;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y2; *vptr++ = dstwidth;  *vptr++ = t2;
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t2;
        *vptr++ = 0;        *vptr++ = y0; *vptr++ = 0;         *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;

        y2 = y1 - bufferheight;
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = dstwidth; *vptr++ = 0;  *vptr++ = dstwidth;  *vptr++ = t2;
        *vptr++ = dstwidth; *vptr++ = y2; *vptr++ = dstwidth;  *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y2; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = 0;  *vptr++ = 0;         *vptr++ = t2;
        *vptr++ = dstwidth; *vptr++ = 0;  *vptr++ = dstwidth;  *vptr++ = t2;

        numvtx = 12;
    }
    else
    {
        // straightforward
        *vptr++ = 0;        *vptr++ = y1; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y1; *vptr++ = dstwidth;  *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y1; *vptr++ = 0;         *vptr++ = t1;
        *vptr++ = 0;        *vptr++ = y0; *vptr++ = 0;         *vptr++ = t0;
        *vptr++ = dstwidth; *vptr++ = y0; *vptr++ = dstwidth;  *vptr++ = t0;

        numvtx = 6;
    }

    glBindBuffer(GL_ARRAY_BUFFER, CaptureVtxBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, numvtx * 4 * sizeof(u16), vtxbuf);

    glBindVertexArray(CaptureVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, numvtx);

    if (dstblock < 4)
    {
        if (capsize == 0)
            CaptureOutput256Valid[dstblock] = false;
        else
            CaptureOutput256Valid[dstblock] = true;
    }

    if (WholeSceneTimingCSVActiveFrames > 0 && yend >= dstheight)
    {
        MeasureCaptureNativeLumaDebug(capcnt, dstblock, capture.DstOffset,
                                      dstwidth, dstheight, capsize,
                                      capsize == 0
                                          ? CaptureOutput128FB[(dstblock << 2) | capture.DstOffset]
                                          : CaptureOutput256FB[dstblock]);
    }

    RecordHighResDisplayCaptureEvent(capture);

    AddWholeScenePhaseTiming(WholeSceneFrameTiming.DoCapture, ElapsedUS(phaseStart));
}

void GLRenderer::MeasureCaptureNativeLumaDebug(u32 capcnt, u32 dstblock, u32 dstoffset,
                                               int dstwidth, int dstheight, u32 capsize,
                                               GLuint captureFB)
{
    const int bufferwidth = (capsize == 0) ? 128 : 256;
    const int bufferheight = (capsize == 0) ? 128 : 256;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, captureFB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureLumaProbeFB);
    glBlitFramebuffer(0, 0, bufferwidth * ScaleFactor, bufferheight * ScaleFactor,
                      0, 0, bufferwidth, bufferheight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    std::vector<u8> pixels((size_t)bufferwidth * bufferheight * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureLumaProbeFB);
    glReadPixels(0, 0, bufferwidth, bufferheight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, captureFB);

    // 128-size captures use a dedicated per-offset buffer starting at row 0;
    // 256-size captures land at dstoffset*64 in the shared bank buffer and can
    // wrap, matching the destination math in DoCapture.
    const int y0 = (capsize == 0) ? 0 : (int)((dstoffset & 3) * 64);
    u64 sum = 0;
    u64 count = 0;
    for (int row = 0; row < dstheight; row++)
    {
        const int y = (y0 + row) & (bufferheight - 1);
        const u8* line = &pixels[(size_t)y * bufferwidth * 4];
        for (int x = 0; x < dstwidth; x++)
        {
            sum += line[x * 4 + 0];
            sum += line[x * 4 + 1];
            sum += line[x * 4 + 2];
        }
        count += (u64)dstwidth * 3;
    }

    LastCaptureNativeLumaDebug.Valid = true;
    LastCaptureNativeLumaDebug.CaptureCnt = capcnt;
    LastCaptureNativeLumaDebug.DstBlock = dstblock;
    LastCaptureNativeLumaDebug.DstOffset = dstoffset;
    LastCaptureNativeLumaDebug.LumaX1000 = count ? (int)((sum * 1000) / count) : 0;
}


void GLRenderer::AllocCapture(u32 bank, u32 start, u32 len)
{
    if (bank < 4)
    {
        LastDisplayCapture256Debug[bank] = {};
        HighResDisplayCapture256Event[bank] = {};
        if (start != 0 || len != 3)
            MainVRAMDisplayExactProductEvent[bank] = {};
    }

    auto rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    rend2D->LayerConfigDirty = true;
    rend2D->SpriteConfigDirty = true;
    rend2D = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    rend2D->LayerConfigDirty = true;
    rend2D->SpriteConfigDirty = true;
}

void GLRenderer::DownscaleCapture(int width, int height, int layer)
{
    // downscale a hi-res capture buffer to 1x IR, and convert to RGBA5551
    // we need to do this with a shader so we can accurately downscale color components

    glUseProgram(CapDownShader);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureSyncFB);

    glViewport(0, 0, width, height);

    glActiveTexture(GL_TEXTURE0);
    if (width == 128)
        glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput128Tex);
    else
        glBindTexture(GL_TEXTURE_2D_ARRAY, CaptureOutput256Tex);
    glUniform1i(CapDownInputLayerULoc, layer);

    glBindBuffer(GL_ARRAY_BUFFER, RectVtxBuffer);
    glBindVertexArray(RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

void GLRenderer::SetVRAMCaptureSyncReason(u32 reason)
{
    PendingVRAMCaptureSyncReason = reason;
}

void GLRenderer::RecordVRAMDisplayWriteDebug(u32 bank, u32 offset, u32 bytes, bool changed)
{
    if (!WholeSceneDebugViewsActive.load(std::memory_order_relaxed) ||
        bank >= 4 ||
        bytes == 0)
        return;

    auto& debug = VRAMDisplayWriteDebug;
    debug.WriteCount++;
    debug.WriteBytes += bytes;
    debug.BankMask |= 1u << bank;
    if (changed)
    {
        debug.ChangedWriteCount++;
        debug.ChangedWriteBytes += bytes;
    }

    if (((DispCntA >> 16) & 0x3u) != 2)
        return;

    const u32 displayBank = (DispCntA >> 18) & 0x3u;
    debug.DisplayBank = displayBank;
    if (displayBank != bank)
        return;

    const u64 writeEnd = static_cast<u64>(offset) + bytes;
    if (offset >= MainVRAMDisplayCaptureVisibleBytes || writeEnd == 0)
        return;

    const u32 visibleStart = offset;
    const u32 visibleEnd = static_cast<u32>(std::min<u64>(writeEnd, MainVRAMDisplayCaptureVisibleBytes));
    if (visibleStart >= visibleEnd)
        return;

    const u32 visibleBytes = visibleEnd - visibleStart;
    debug.DisplayWriteCount++;
    debug.DisplayWriteBytes += visibleBytes;
    debug.DisplayFirstOffset = std::min(debug.DisplayFirstOffset, visibleStart);
    debug.DisplayLastEnd = std::max(debug.DisplayLastEnd, visibleEnd);

    if (!changed)
        return;

    debug.DisplayChangedWriteCount++;
    debug.DisplayChangedWriteBytes += visibleBytes;

    const u32 dirtyYStart = std::min<u32>(visibleStart / MainVRAMDisplayCaptureRowBytes, 191);
    const u32 dirtyYEnd = std::min<u32>(
        (visibleEnd + MainVRAMDisplayCaptureRowBytes - 1) / MainVRAMDisplayCaptureRowBytes,
        192);
    debug.DisplayDirtyYStart = std::min(debug.DisplayDirtyYStart, dirtyYStart);
    debug.DisplayDirtyYEnd = std::max(debug.DisplayDirtyYEnd, dirtyYEnd);
}

void GLRenderer::NotifyVRAMWrite(u32 bank, u32 offset, u32 bytes, bool changed)
{
    RecordVRAMDisplayWriteDebug(bank, offset, bytes, changed);

    if (!changed || bank >= 4)
        return;

    MainVRAMDisplayExactProductEvent[bank] = {};

    auto& epoch = MainVRAMDisplayEpoch[bank];
    if (!epoch.Valid)
        return;

    const u32 captureStart = epoch.DstOffset * MainVRAMDisplayCaptureBlockBytes;
    const u32 captureEnd = captureStart + MainVRAMDisplayCaptureVisibleBytes;
    const u64 writeEnd = static_cast<u64>(offset) + bytes;
    if (offset >= captureEnd || writeEnd <= captureStart)
        return;

    const u32 dirtyStartByte = offset > captureStart ? offset - captureStart : 0;
    const u32 dirtyEndByte = static_cast<u32>(
        std::min<u64>(writeEnd, captureEnd) - captureStart);
    const u32 dirtyYStart = std::min<u32>(dirtyStartByte / MainVRAMDisplayCaptureRowBytes, 191);
    const u32 dirtyYEnd = std::min<u32>(
        (dirtyEndByte + MainVRAMDisplayCaptureRowBytes - 1) / MainVRAMDisplayCaptureRowBytes,
        192);

    if (!epoch.HasDirtyRows)
    {
        epoch.HasDirtyRows = true;
        epoch.DirtyYStart = dirtyYStart;
        epoch.DirtyYEnd = dirtyYEnd;
    }
    else
    {
        epoch.DirtyYStart = std::min(epoch.DirtyYStart, dirtyYStart);
        epoch.DirtyYEnd = std::max(epoch.DirtyYEnd, dirtyYEnd);
    }
}

void GLRenderer::SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete, bool invalidate)
{
    if (!complete)
        Log(LogLevel::Error, "GPU_OpenGL: !!! READING VRAM AS IT IS BEING CAPTURED TO\n");

    const u32 reason = PendingVRAMCaptureSyncReason;
    PendingVRAMCaptureSyncReason = 0;

    if (invalidate && bank < 4)
    {
        LastDisplayCapture256Debug[bank] = {};
        HighResDisplayCapture256Event[bank] = {};
        if (reason != VRAMCaptureInvalidationPreWriteSync)
            InvalidateMainVRAMDisplayEpochForBank(bank, reason, start, len, complete);
    }

    u8* vram = GPU.VRAM[bank];

    glDisable(GL_DITHER);

    if (len == 0) // 128x128
    {
        DownscaleCapture(128, 128, (bank<<2) | start);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureSyncFB);

        glReadPixels(0, 0, 128, 128,
                     GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, &vram[start * 64 * 512]);

        for (u32 j = start * 64; j < (start+1) * 64; j++)
            GPU.VRAMDirty[bank][j] = true;
    }
    else
    {
        DownscaleCapture(256, 256, bank);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, CaptureSyncFB);

        u32 pos = start;
        for (u32 i = 0; i < len;)
        {
            u32 end = pos + len;
            if (end > 4)
                end = 4;

            glReadPixels(0, pos * 64, 256, (end - pos) * 64,
                         GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, &vram[pos * 64 * 512]);

            for (u32 j = pos * 64; j < end * 64; j++)
                GPU.VRAMDirty[bank][j] = true;

            i += (end - pos);
            pos += (end - pos);
            pos &= 3;
        }
    }
}


bool GLRenderer::GetFramebuffers(void** top, void** bottom)
{
    // since we use an array texture, we only need one of the pointer fields
    int frontbuf = BackBuffer ^ 1;
    *top = &FPOutputTex[frontbuf];
    *bottom = nullptr;
    return false;
}

void GLRenderer::SwapBuffers()
{
    CaptureRollingFinalDebugFrame(BackBuffer);
    Renderer::SwapBuffers();
}

bool GLRenderer::ReadFinalDebugFrameFromFramebuffer(int framebuffer,
                                                    u64 serial,
                                                    WholeScene2DFinalDebugFrame& frame,
                                                    std::string* status)
{
    frame = {};

    if (framebuffer < 0 || framebuffer >= 2 || FPOutputFB[framebuffer] == 0)
    {
        if (status)
            *status = "Final output framebuffer is unavailable.";
        return false;
    }

    GLint prevReadFB = 0;
    GLint prevReadBuffer = GL_COLOR_ATTACHMENT0;
    GLint prevPackAlignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFB);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

    frame = CaptureRollingFinalDebugMetadata(serial);
    frame.Width = ScreenW;
    frame.Height = ScreenH;
    const size_t pixelCount = static_cast<size_t>(frame.Width) *
                              static_cast<size_t>(frame.Height);
    frame.TopRGBA.resize(pixelCount);
    frame.BottomRGBA.resize(pixelCount);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, FPOutputFB[framebuffer]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,
                 0,
                 frame.Width,
                 frame.Height,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 frame.TopRGBA.data());

    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0,
                 0,
                 frame.Width,
                 frame.Height,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 frame.BottomRGBA.data());

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFB);
    glReadBuffer(prevReadBuffer);

    if (status)
    {
        *status = "Read current final output frame from buffer ";
        *status += std::to_string(framebuffer);
        *status += ".";
    }
    return true;
}

bool GLRenderer::SetWholeScene2DRollingDebugCapture(bool enabled,
                                                    int frameCount,
                                                    std::string* status)
{
    if (frameCount <= 0)
        frameCount = 60;
    if (frameCount > 180)
        frameCount = 180;

    RollingFinalDebugCaptureEnabled = enabled;
    RollingFinalDebugCapacity = enabled ? frameCount : 0;
    RollingFinalDebugWriteIndex = 0;
    RollingFinalDebugSerial = 0;
    RollingFinalDebugWidth = 0;
    RollingFinalDebugHeight = 0;
    RollingFinalDebugSlots.clear();

    if (!enabled)
    {
        if (RollingFinalDebugTex != 0)
        {
            glDeleteTextures(1, &RollingFinalDebugTex);
            RollingFinalDebugTex = 0;
        }
        if (RollingFinalDebugFB != 0)
        {
            glDeleteFramebuffers(1, &RollingFinalDebugFB);
            RollingFinalDebugFB = 0;
        }
    }

    if (enabled && !EnsureRollingFinalDebugStorage())
    {
        RollingFinalDebugCaptureEnabled = false;
        RollingFinalDebugCapacity = 0;
        if (status)
            *status = "Failed to allocate whole-scene rolling debug capture.";
        return false;
    }

    if (status)
    {
        if (enabled)
        {
            const u64 bytes =
                static_cast<u64>(ScreenW) *
                static_cast<u64>(ScreenH) *
                2u *
                static_cast<u64>(RollingFinalDebugCapacity) *
                4u;
            *status = "Whole-scene rolling debug capture enabled: ";
            *status += std::to_string(RollingFinalDebugCapacity);
            *status += " frames, ";
            *status += std::to_string(bytes / (1024u * 1024u));
            *status += " MiB GPU texture ring.";
        }
        else
        {
            *status = "Whole-scene rolling debug capture disabled.";
        }
    }

    return true;
}

bool GLRenderer::ReadWholeScene2DCurrentFinalDebugFrame(WholeScene2DFinalDebugFrame& frame,
                                                        std::string* status)
{
    return ReadFinalDebugFrameFromFramebuffer(BackBuffer ^ 1, 0, frame, status);
}

bool GLRenderer::ReadWholeScene2DRollingDebugFrames(std::vector<WholeScene2DFinalDebugFrame>& frames,
                                                    std::string* status)
{
    frames.clear();

    if (!RollingFinalDebugCaptureEnabled ||
        RollingFinalDebugCapacity <= 0 ||
        RollingFinalDebugWidth <= 0 ||
        RollingFinalDebugHeight <= 0)
    {
        if (status)
            *status = "Whole-scene rolling debug capture is not active.";
        return false;
    }

    std::vector<int> slotOrder;
    for (int i = 0; i < static_cast<int>(RollingFinalDebugSlots.size()); i++)
    {
        if (RollingFinalDebugSlots[i].Valid)
            slotOrder.push_back(i);
    }

    std::sort(slotOrder.begin(), slotOrder.end(), [this](int a, int b) {
        return RollingFinalDebugSlots[a].Serial < RollingFinalDebugSlots[b].Serial;
    });

    GLint prevPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, RollingFinalDebugFB);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    const size_t pixelCount = static_cast<size_t>(RollingFinalDebugWidth) *
                              static_cast<size_t>(RollingFinalDebugHeight);
    for (int slot : slotOrder)
    {
        WholeScene2DFinalDebugFrame frame = RollingFinalDebugSlots[slot].Metadata;
        frame.Width = RollingFinalDebugWidth;
        frame.Height = RollingFinalDebugHeight;
        frame.TopRGBA.resize(pixelCount);
        frame.BottomRGBA.resize(pixelCount);

        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER,
                                  GL_COLOR_ATTACHMENT0,
                                  RollingFinalDebugTex,
                                  0,
                                  slot * 2);
        glReadPixels(0,
                     0,
                     RollingFinalDebugWidth,
                     RollingFinalDebugHeight,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     frame.TopRGBA.data());

        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER,
                                  GL_COLOR_ATTACHMENT0,
                                  RollingFinalDebugTex,
                                  0,
                                  slot * 2 + 1);
        glReadPixels(0,
                     0,
                     RollingFinalDebugWidth,
                     RollingFinalDebugHeight,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     frame.BottomRGBA.data());

        frames.push_back(std::move(frame));
    }

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    if (status)
    {
        *status = "Read ";
        *status += std::to_string(frames.size());
        *status += " rolling final frames.";
    }

    return !frames.empty();
}

bool GLRenderer::IsEngineRoutedToFinalBottom(u32 engine) const
{
    return engine == (GPU.ScreenSwap ? 1u : 0u);
}

bool GLRenderer::GetFinalPassScreenSwapForRange(int ystart, int yend, bool& screenSwap) const
{
    int y0 = ystart;
    if (y0 < 0) y0 = 0;
    if (y0 > 191) y0 = 191;

    int y1 = yend;
    if (y1 < y0 + 1) y1 = y0 + 1;
    if (y1 > 192) y1 = 192;

    const bool firstSwap = FinalPassConfig.uScreenSwap[y0] != 0;
    for (int y = y0 + 1; y < y1; y++)
    {
        if ((FinalPassConfig.uScreenSwap[y] != 0) != firstSwap)
        {
            screenSwap = firstSwap;
            return false;
        }
    }

    screenSwap = firstSwap;
    return true;
}

void GLRenderer::UpdateFinalPresentationTransitionGuard()
{
    sFinalPresentationState state = {};
    state.Valid = true;
    state.ScreenSwap = GPU.ScreenSwap;
    state.DispModeA = (GPU.GPU2D_A.DispCnt >> 16) & 0x3u;
    state.DispModeB = (GPU.GPU2D_B.DispCnt >> 16) & 0x1u;

    const bool stateChanged =
        LastFinalPresentationState.Valid &&
        (state.ScreenSwap != LastFinalPresentationState.ScreenSwap ||
         state.DispModeA != LastFinalPresentationState.DispModeA ||
         state.DispModeB != LastFinalPresentationState.DispModeB);
    const bool screenSwapOnlyChanged =
        stateChanged &&
        state.ScreenSwap != LastFinalPresentationState.ScreenSwap &&
        state.DispModeA == LastFinalPresentationState.DispModeA &&
        state.DispModeB == LastFinalPresentationState.DispModeB;
    const bool stateMatchesExcursionBaseline =
        FinalPresentationScreenSwapExcursionActive &&
        state.ScreenSwap == FinalPresentationScreenSwapExcursionBaseline.ScreenSwap &&
        state.DispModeA == FinalPresentationScreenSwapExcursionBaseline.DispModeA &&
        state.DispModeB == FinalPresentationScreenSwapExcursionBaseline.DispModeB;
    const bool directOutputScreenSwapOnlyChanged =
        screenSwapOnlyChanged &&
        state.DispModeA == 1 &&
        state.DispModeB == 1 &&
        LastFinalPresentationState.DispModeA == 1 &&
        LastFinalPresentationState.DispModeB == 1;
    const auto masterBrightnessActive = [](u16 masterBrightness)
    {
        const u32 mode = (masterBrightness >> 14) & 0x3;
        const u32 factor = masterBrightness & 0x1F;
        return (mode == 1 || mode == 2) && factor > 0;
    };
    const bool directOutputFadeSwap =
        directOutputScreenSwapOnlyChanged &&
        GPU.MasterBrightnessA == GPU.MasterBrightnessB &&
        masterBrightnessActive(GPU.MasterBrightnessA);
    const u32 requiredStableScanlines =
        directOutputScreenSwapOnlyChanged ?
            kFinalPresentationDirectSwapGuardStableScanlines :
            kFinalPresentationStableRouteScanlines;
    const bool startsScreenSwapExcursion =
        screenSwapOnlyChanged &&
        !directOutputFadeSwap &&
        !FinalPresentationScreenSwapExcursionActive &&
        FinalPresentationStateStableScanlines >= requiredStableScanlines;

    // Master brightness is applied in the final pass; fade-only changes do not
    // alter which renderer owns the screen content.
    if (startsScreenSwapExcursion)
    {
        FinalPresentationScreenSwapExcursionBaseline = LastFinalPresentationState;
        FinalPresentationScreenSwapExcursionScanlines = 0;
        FinalPresentationScreenSwapExcursionActive = true;
    }
    else if (stateChanged && !screenSwapOnlyChanged)
    {
        FinalPresentationScreenSwapExcursionActive = false;
        FinalPresentationScreenSwapExcursionBaseline = {};
        FinalPresentationScreenSwapExcursionScanlines = 0;
        if (FinalPresentationTransitionGuardFrames < kFinalPresentationTransitionGuardFrames)
            FinalPresentationTransitionGuardFrames = kFinalPresentationTransitionGuardFrames;
    }

    if (FinalPresentationScreenSwapExcursionActive)
    {
        if (stateMatchesExcursionBaseline)
        {
            FinalPresentationScreenSwapExcursionActive = false;
            FinalPresentationScreenSwapExcursionBaseline = {};
            FinalPresentationScreenSwapExcursionScanlines = 0;
            if (FinalPresentationTransitionGuardFrames < kFinalPresentationTransitionGuardFrames)
                FinalPresentationTransitionGuardFrames = kFinalPresentationTransitionGuardFrames;
        }
        else
        {
            if (FinalPresentationTransitionGuardFrames < kFinalPresentationTransitionGuardFrames)
                FinalPresentationTransitionGuardFrames = kFinalPresentationTransitionGuardFrames;
            if (FinalPresentationScreenSwapExcursionScanlines < 0xFFFFFFFFu)
                FinalPresentationScreenSwapExcursionScanlines++;
            if (FinalPresentationScreenSwapExcursionScanlines >= kFinalPresentationScreenSwapExcursionMaxScanlines)
            {
                FinalPresentationScreenSwapExcursionActive = false;
                FinalPresentationScreenSwapExcursionBaseline = {};
                FinalPresentationScreenSwapExcursionScanlines = 0;
            }
        }
    }

    if (stateChanged)
    {
        LastFinalPresentationState = state;
        FinalPresentationStateStableScanlines = 1;
    }
    else
    {
        LastFinalPresentationState = state;
        if (FinalPresentationStateStableScanlines < 0xFFFFFFFFu)
            FinalPresentationStateStableScanlines++;
    }
}

bool GLRenderer::IsFinalPresentationTransitionGuardActiveForRange(int ystart, int yend) const
{
    bool screenSwap = false;
    if (!GetFinalPassScreenSwapForRange(ystart, yend, screenSwap))
        return true;

    return FinalPresentationTransitionGuardFrames > 0;
}

bool GLRenderer::IsFinalPresentationScreenSwapExcursionActiveForRange(int ystart, int yend) const
{
    if (!FinalPresentationScreenSwapExcursionActive)
        return false;

    bool screenSwap = false;
    if (!GetFinalPassScreenSwapForRange(ystart, yend, screenSwap))
        return true;

    if (!FinalPresentationScreenSwapExcursionBaseline.Valid)
        return true;

    return screenSwap != FinalPresentationScreenSwapExcursionBaseline.ScreenSwap;
}

bool GLRenderer::IsEngineRoutedToFinalBottom(u32 engine, int ystart, int yend) const
{
    bool screenSwap = GPU.ScreenSwap;
    GetFinalPassScreenSwapForRange(ystart, yend, screenSwap);
    return engine == (screenSwap ? 1u : 0u);
}

bool GLRenderer::IsMainVRAMDisplayFinalRouteForRange(int ystart, int yend) const
{
    const u32 finalDispModeA = (DispCntA >> 16) & 0x3u;
    return finalDispModeA == 2 && IsEngineRoutedToFinalBottom(0, ystart, yend);
}

bool GLRenderer::HasMainVRAMDisplayCaptureFinalRoute() const
{
    const u32 finalDispModeA = (DispCntA >> 16) & 0x3u;
    if (finalDispModeA != 2)
        return false;

    const u32 displayBank = (DispCntA >> 18) & 0x3u;
    return CanUseMainVRAMDisplayHighResCaptureReplacement(displayBank);
}

bool GLRenderer::IsAcceptedMainVRAMDisplayFullProductSource(HighResCaptureSourceKind sourceKind)
{
    return sourceKind == HighResCaptureSourceKind::CleanOutput3D ||
           sourceKind == HighResCaptureSourceKind::CleanEngineA2DOutput;
}

void GLRenderer::ResetFinalVRAMDisplayTrace(sFinalVRAMDisplayRenderTrace& trace, int displayBank)
{
    trace = {};
    trace.DisplayBank = displayBank;
}

void GLRenderer::RecordFinalVRAMDisplayTraceEvent(sFinalVRAMDisplayRenderTrace& trace,
                                                  const sHighResDisplayCaptureEvent& event,
                                                  GLuint fullTex,
                                                  bool eventMatches,
                                                  bool exactRouteMatches,
                                                  bool exactProductAvailable,
                                                  bool exactProductUsable)
{
    trace.EventValid = event.Valid ? 1 : 0;
    trace.EventSerial = event.Serial;
    trace.EventDstBlock = static_cast<int>(event.DstBlock);
    trace.EventDstOffset = static_cast<int>(event.DstOffset);
    trace.EventScreenSwap = event.ScreenSwap ? 1 : 0;
    trace.EventMainFinalBottom = event.MainEngineFinalBottom ? 1 : 0;
    trace.EventSourceOBJ = event.SourceOBJVisible ? 1 : 0;
    trace.EventSourceRenderedFullWholeScene = event.SourceRenderedFullWholeScene ? 1 : 0;
    trace.EventSourceKind = static_cast<u32>(event.SourceKind);
    trace.EventProductMask = event.ProductMask;
    trace.EventRejectReason = static_cast<u32>(event.RejectReason);
    trace.EventFullTex = static_cast<int>(fullTex);
    trace.EventMatchesNativeCapture = eventMatches ? 1 : 0;
    trace.ExactEventRouteMatches = exactRouteMatches ? 1 : 0;
    trace.ExactEventProductAvailable = exactProductAvailable ? 1 : 0;
    trace.ExactEventProductUsable = exactProductUsable ? 1 : 0;
}

void GLRenderer::RecordFinalVRAMDisplayTraceChosenEvent(sFinalVRAMDisplayRenderTrace& trace,
                                                        u32 displayBank,
                                                        GLuint texture,
                                                        const sHighResDisplayCaptureEvent& event)
{
    trace.ChosenBank = static_cast<int>(displayBank);
    trace.ChosenSerial = event.Serial;
    trace.ChosenSource3DSerial = event.Source3DSerial;
    trace.ChosenSource3DSceneHash = event.Source3DSceneHash;
    trace.ChosenSourcePresentationHash = event.SourcePresentationHash;
    trace.ChosenSourceKind = static_cast<u32>(event.SourceKind);
    trace.ChosenProductMask = event.ProductMask;
    trace.ChosenTex = static_cast<int>(texture);
}

void GLRenderer::RecordFinalVRAMDisplayTraceChosenEpoch(sFinalVRAMDisplayRenderTrace& trace,
                                                        u32 displayBank,
                                                        GLuint texture,
                                                        const sMainVRAMDisplayEpoch& epoch)
{
    trace.ChosenBank = static_cast<int>(displayBank);
    trace.ChosenSerial = epoch.Serial;
    trace.ChosenSource3DSerial = epoch.Source3DSerial;
    trace.ChosenSource3DSceneHash = epoch.Source3DSceneHash;
    trace.ChosenSourcePresentationHash = epoch.SourcePresentationHash;
    trace.ChosenSourceKind = static_cast<u32>(epoch.SourceKind);
    trace.ChosenProductMask = epoch.ProductMask;
    trace.ChosenTex = static_cast<int>(texture);
}

void GLRenderer::RecordFinalVRAMDisplayTraceAccepted(u32 displayBank,
                                                     GLuint texture,
                                                     bool usedEpoch)
{
    FinalVRAMDisplayRenderTrace.ReplacementEligible = 1;
    FinalVRAMDisplayRenderTrace.RejectReason = 0;
    FinalVRAMDisplayRenderTrace.UsedEpoch = usedEpoch ? 1 : 0;
    FinalVRAMDisplayRenderTrace.ChosenBank = static_cast<int>(displayBank);
    FinalVRAMDisplayRenderTrace.ChosenTex = static_cast<int>(texture);

    if (usedEpoch)
    {
        RecordFinalVRAMDisplayTraceChosenEpoch(FinalVRAMDisplayRenderTrace,
                                               displayBank,
                                               texture,
                                               MainVRAMDisplayEpoch[displayBank]);
    }
    else if (FinalVRAMDisplayRenderTrace.ChosenSerial == 0)
    {
        RecordFinalVRAMDisplayTraceChosenEvent(FinalVRAMDisplayRenderTrace,
                                               displayBank,
                                               texture,
                                               HighResDisplayCapture256Event[displayBank]);
    }
}

void GLRenderer::RecordFinalVRAMDisplayTraceRejected(int rejectReason)
{
    FinalVRAMDisplayRenderTrace.RejectReason = rejectReason;
}

bool GLRenderer::CanUseMainVRAMDisplayHighResCaptureReplacement(u32 displayBank,
                                                                GLuint* replacementTex,
                                                                int* rejectReason,
                                                                bool* usedEpoch,
                                                                sFinalVRAMDisplayRenderTrace* trace) const
{
    if (replacementTex)
        *replacementTex = 0;
    if (rejectReason)
        *rejectReason = 0;
    if (usedEpoch)
        *usedEpoch = false;
    if (trace)
    {
        ResetFinalVRAMDisplayTrace(*trace, static_cast<int>(displayBank));
    }

    const u32 finalDispModeA = (DispCntA >> 16) & 0x3u;
    if (finalDispModeA != 2)
    {
        if (rejectReason)
            *rejectReason = 1;
        return false;
    }

    if (displayBank >= 4)
    {
        if (rejectReason)
            *rejectReason = 3;
        return false;
    }

    const auto* rendA = dynamic_cast<const GLRenderer2D*>(Rend2D_A.get());
    if (!rendA ||
        !rendA->WholeSceneScaleRequested ||
        !rendA->WholeSceneScaleCaptureBacked ||
        rendA->WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale)
    {
        if (rejectReason)
            *rejectReason = 2;
        return false;
    }

    const auto& capture = LastDisplayCapture256Debug[displayBank];
    const auto& event = HighResDisplayCapture256Event[displayBank];
    const auto& retainedExactEvent = MainVRAMDisplayExactProductEvent[displayBank];
    MainVRAMDisplayEventMatchInputs eventMatchInputs = {};
    eventMatchInputs.NativeCaptureRecordValid =
        IsFullDisplaySourceACaptureRecord(capture, displayBank);
    eventMatchInputs.HighResEventRecordValid =
        IsFullDisplayHighResCaptureEventRecord(event, displayBank);
    eventMatchInputs.NativeCaptureDstOffset = capture.DstOffset;
    eventMatchInputs.HighResEventDstOffset = event.DstOffset;
    eventMatchInputs.NativeCaptureCnt = capture.CaptureCnt;
    eventMatchInputs.HighResEventCaptureCnt = event.CaptureCnt;
    const bool eventMatches =
        DoesMainVRAMDisplayEventMatchNativeCapture(eventMatchInputs);
    bool screenSwap = GPU.ScreenSwap;
    GetFinalPassScreenSwapForRange(0, 192, screenSwap);
    const bool mainEngineFinalBottom = IsEngineRoutedToFinalBottom(0, 0, 192);
    const sHighResDisplayCaptureEvent* exactEvent = &event;
    if (!IsFullDisplayHighResCaptureExactReplacementRecord(*exactEvent, displayBank) &&
        IsFullDisplayHighResCaptureExactReplacementRecord(retainedExactEvent, displayBank))
    {
        exactEvent = &retainedExactEvent;
    }
    const bool exactFullEventRouteMatches =
        exactEvent->ScreenSwap == screenSwap &&
        exactEvent->MainEngineFinalBottom == mainEngineFinalBottom;
    const bool exactFullEventRecordValid =
        IsFullDisplayHighResCaptureExactReplacementRecord(*exactEvent, displayBank);
    const bool exactFullEventDstOffsetZero = exactEvent->DstOffset == 0;
    const bool exactFullEventAccepted =
        exactEvent->RejectReason == HighResCaptureRejectReason::None;
    const bool exactFullEventFullEquivalent =
        (exactEvent->ProductMask & HighResCaptureProductFullEquivalent) != 0;
    const bool exactFullEventAcceptedSource =
        IsAcceptedMainVRAMDisplayFullProductSource(exactEvent->SourceKind);
    const bool exactFullEventHasFullTexture =
        HighResDisplayCaptureFullTex[displayBank] != 0;
    const bool exactFullEventProductAvailable =
        exactFullEventRecordValid &&
        exactFullEventDstOffsetZero &&
        exactFullEventAccepted &&
        exactFullEventFullEquivalent &&
        exactFullEventAcceptedSource &&
        exactFullEventHasFullTexture;
    MainVRAMDisplayExactEventReplacementInputs exactEventInputs = {};
    exactEventInputs.EventRecordValid = exactFullEventRecordValid;
    exactEventInputs.EventDstOffsetZero = exactFullEventDstOffsetZero;
    exactEventInputs.EventAccepted = exactFullEventAccepted;
    exactEventInputs.EventFullEquivalent = exactFullEventFullEquivalent;
    exactEventInputs.EventAcceptedSource = exactFullEventAcceptedSource;
    exactEventInputs.HasFullTexture = exactFullEventHasFullTexture;
    exactEventInputs.EventRouteMatches = exactFullEventRouteMatches;
    exactEventInputs.EventSourceOBJVisible = exactEvent->SourceOBJVisible;
    exactEventInputs.EventSourceRenderedFullWholeScene =
        exactEvent->SourceRenderedFullWholeScene;
    const bool exactFullEventProductUsable =
        CanUseMainVRAMDisplayExactEventReplacement(exactEventInputs);

    if (trace)
    {
        RecordFinalVRAMDisplayTraceEvent(*trace,
                                         *exactEvent,
                                         HighResDisplayCaptureFullTex[displayBank],
                                         eventMatches,
                                         exactFullEventRouteMatches,
                                         exactFullEventProductAvailable,
                                         exactFullEventProductUsable);
    }

    if (exactFullEventProductUsable)
    {
        if (replacementTex)
            *replacementTex = HighResDisplayCaptureFullTex[displayBank];
        if (trace)
            RecordFinalVRAMDisplayTraceChosenEvent(*trace,
                                                   displayBank,
                                                   HighResDisplayCaptureFullTex[displayBank],
                                                   *exactEvent);
        return true;
    }

    if (eventMatches)
    {
        if (event.RejectReason != HighResCaptureRejectReason::None)
        {
            if (rejectReason)
            {
                MainVRAMDisplayMixedOrOBJRejectInputs mixedRejectInputs = {};
                mixedRejectInputs.DirtyOrPartialSourceReject =
                    event.RejectReason == HighResCaptureRejectReason::DirtyOrPartialSource;
                mixedRejectInputs.NativeOnlyOutput2DSource =
                    event.SourceKind == HighResCaptureSourceKind::NativeOnlyOutput2D;
                mixedRejectInputs.SourceDirect3DVisible = event.SourceDirect3DVisible;
                mixedRejectInputs.SourceHasNoVisibleBitmap =
                    event.SourceVisibleBitmapMask == 0;
                mixedRejectInputs.SourceBG0Visible =
                    (event.SourceLayerEnable & (1u << 0)) != 0;
                mixedRejectInputs.SourceOBJVisible = event.SourceOBJVisible;
                mixedRejectInputs.SourceOtherBGVisible =
                    (event.SourceLayerEnable & 0x0Eu) != 0;
                const bool mixedOrOBJFullFrameCapture =
                    IsMainVRAMDisplayMixedOrOBJReject(mixedRejectInputs);
                *rejectReason = mixedOrOBJFullFrameCapture ? 8 : 5;
            }
            return false;
        }

        if (!(event.ProductMask & HighResCaptureProductFullEquivalent))
        {
            if (rejectReason)
                *rejectReason = 6;
            return false;
        }

        if (!IsAcceptedMainVRAMDisplayFullProductSource(event.SourceKind))
        {
            if (event.SourceKind != HighResCaptureSourceKind::DerivedMainVRAMDisplayEpoch)
            {
                if (rejectReason)
                    *rejectReason = 7;
                return false;
            }
        }
        else if (!HighResDisplayCaptureFullTex[displayBank])
        {
            if (rejectReason)
                *rejectReason = 6;
            return false;
        }
    }

    const auto& epoch = MainVRAMDisplayEpoch[displayBank];
    MainVRAMDisplayEpochReplacementInputs epochInputs = {};
    epochInputs.EpochValid = epoch.Valid;
    epochInputs.EpochCaptureBank = epoch.CaptureBank;
    epochInputs.DisplayBank = displayBank;
    epochInputs.EpochDstOffset = epoch.DstOffset;
    epochInputs.EpochHasFullDirtyRows =
        epoch.HasDirtyRows &&
        epoch.DirtyYStart == 0 &&
        epoch.DirtyYEnd >= 192;
    epochInputs.EpochFullEquivalent =
        (epoch.ProductMask & HighResCaptureProductFullEquivalent) != 0;
    epochInputs.EpochAcceptedSource =
        IsAcceptedMainVRAMDisplayFullProductSource(epoch.SourceKind);
    epochInputs.HasEpochTexture = MainVRAMDisplayEpochTex[displayBank] != 0;
    const int epochRejectReason =
        MainVRAMDisplayEpochReplacementRejectReason(epochInputs);
    if (epochRejectReason != 0)
    {
        if (rejectReason)
            *rejectReason = epochRejectReason;
        return false;
    }

    if (replacementTex)
        *replacementTex = MainVRAMDisplayEpochTex[displayBank];
    if (usedEpoch)
        *usedEpoch = true;
    return true;
}

void GLRenderer::RecordDisplayCaptureDebug(const sLastDisplayCaptureDebug& capture)
{
    LastDisplayCaptureDebug = capture;

    if (capture.CapSize == 0)
    {
        LastDisplayCapture128Debug[(capture.DstBlock << 2) | capture.DstOffset] = capture;
        LastDisplayCapture256Debug[capture.DstBlock].Valid = false;
    }
    else
    {
        LastDisplayCapture256Debug[capture.DstBlock] = capture;
    }
}

GLRenderer::sHighResDisplayCaptureEvent GLRenderer::BuildHighResDisplayCaptureEventMetadata(
    const sLastDisplayCaptureDebug& capture,
    GLRenderer2D*& sourceRenderer)
{
    sHighResDisplayCaptureEvent event = {};
    event.Valid = true;
    event.Serial = ++HighResDisplayCaptureEventSerial;
    event.Source3DSerial = Output3DSerial;
    event.Source3DSceneHash = Output3DSceneHash;
    event.CaptureCnt = capture.CaptureCnt;
    event.YStart = capture.YStart;
    event.YEnd = capture.YEnd;
    event.DstWidth = capture.DstWidth;
    event.DstHeight = capture.DstHeight;
    event.SrcA = capture.SrcA;
    event.SrcB = capture.SrcB;
    event.DstBlock = capture.DstBlock;
    event.DstOffset = capture.DstOffset;
    event.CapSize = capture.CapSize;
    event.DstMode = capture.DstMode;
    event.ScreenSwap = GPU.ScreenSwap;
    GetFinalPassScreenSwapForRange(capture.YStart, capture.YEnd, event.ScreenSwap);
    event.MainEngineFinalBottom = IsEngineRoutedToFinalBottom(0, capture.YStart, capture.YEnd);
    event.MainDisplayMode = (DispCntA >> 16) & 0x3u;
    event.SourceKind = HighResCaptureSourceKind::Unknown;
    event.RejectReason = HighResCaptureRejectReason::None;

    sourceRenderer = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    if (sourceRenderer)
    {
        event.SourceLayerEnable = sourceRenderer->LayerEnable;
        event.SourceBGMode = sourceRenderer->DispCnt & 0x7u;
        event.SourceVisibleBitmapMask = sourceRenderer->VisibleBitmapBGLayerMask();
        event.SourceDirect3DVisible =
            (sourceRenderer->DispCnt & (1 << 3)) && (sourceRenderer->LayerEnable & (1 << 0));
        event.SourceOBJVisible =
            (sourceRenderer->LayerEnable & (1 << 4)) && sourceRenderer->OBJEnable && sourceRenderer->NumSprites > 0;
        event.SourceWholeScenePath = static_cast<u32>(sourceRenderer->WholeSceneTrace.Path);
        event.SourceWholeSceneYStart = sourceRenderer->WholeSceneTrace.YStart;
        event.SourceWholeSceneYEnd = sourceRenderer->WholeSceneTrace.YEnd;
        event.SourceVisibleBGLayers = sourceRenderer->LayerEnable & 0x0Fu;
        for (int layer = 0; layer < 4; layer++)
        {
            event.SourceBGLayerTypes |=
                (sourceRenderer->LayerConfig.uBGConfig[layer].Type & 0xFFu) << (layer * 8);
        }
        event.SourceRenderedFullWholeScene =
            sourceRenderer->WholeSceneTrace.YStart <= 0 &&
            sourceRenderer->WholeSceneTrace.YEnd >= 192 &&
            IsFullWholeSceneSourcePath(sourceRenderer->WholeSceneTrace.Path);
        event.SourcePresentationHash = sourceRenderer->CapturePresentationHash();
        event.SourceMasterBrightness = GPU.MasterBrightnessA;
        event.HasSourceEffectState = true;
    }

    return event;
}

void GLRenderer::ClassifyHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture,
                                                    GLRenderer2D* sourceRenderer,
                                                    bool fullDisplay,
                                                    bool sourceAOnly,
                                                    sHighResDisplayCaptureEvent& event)
{
    const bool trackedSourceBBlend =
        !sourceAOnly &&
        capture.UsesSrcB &&
        capture.SrcBUsesTrackedCapture &&
        capture.CapSize == 3 &&
        capture.DstOffset == 0;
    const bool allowBackgroundProduct = sourceAOnly;

    if (!fullDisplay)
        event.RejectReason = HighResCaptureRejectReason::NotFullDisplay;
    else if (!sourceAOnly && !trackedSourceBBlend)
    {
        event.RejectReason = capture.UsesSrcB
            ? HighResCaptureRejectReason::UsesSourceB
            : HighResCaptureRejectReason::BlendedOrFeedback;
    }
    else if (capture.SrcBSameDstBank && !trackedSourceBBlend)
        event.RejectReason = HighResCaptureRejectReason::SameBankReadWrite;

    if (event.RejectReason != HighResCaptureRejectReason::None)
        return;

    if (capture.SrcA)
    {
        event.SourceKind = HighResCaptureSourceKind::CleanOutput3D;
        event.ProductMask = HighResCaptureProductFullEquivalent;
        if (allowBackgroundProduct)
            event.ProductMask |= HighResCaptureProductBackground3DUnderlay;
        return;
    }

    if (!sourceRenderer)
    {
        event.SourceKind = HighResCaptureSourceKind::Unknown;
        event.RejectReason = HighResCaptureRejectReason::MissingSourceTexture;
        return;
    }

    const u32 visibleBGLayers = event.SourceVisibleBGLayers;
    const bool direct3DOnlyBackground =
        event.SourceDirect3DVisible &&
        visibleBGLayers == (1u << 0) &&
        event.SourceVisibleBitmapMask == 0;
    event.SourceDirect3DOnlyBackground = direct3DOnlyBackground;
    const auto sourcePath = static_cast<GLRenderer2D::WholeSceneRenderPath>(event.SourceWholeScenePath);
    if (direct3DOnlyBackground)
    {
        event.SourceKind = HighResCaptureSourceKind::CleanEngineA2DOutput;
        event.ProductMask = HighResCaptureProductFullEquivalent;
        if (allowBackgroundProduct)
            event.ProductMask |= HighResCaptureProductBackground3DUnderlay;
    }
    else if (sourcePath == GLRenderer2D::WholeSceneRenderPath::CaptureBackedHandoff)
    {
        event.SourceKind = HighResCaptureSourceKind::RecursiveHandoffOutput;
        event.RejectReason = HighResCaptureRejectReason::RecursiveSource;
    }
    else if (sourcePath == GLRenderer2D::WholeSceneRenderPath::SourceACaptureReplacement ||
             sourcePath == GLRenderer2D::WholeSceneRenderPath::CaptureEpochOverlay)
    {
        event.SourceKind = HighResCaptureSourceKind::RecursiveSourceReplacementOutput;
        event.RejectReason = HighResCaptureRejectReason::RecursiveSource;
    }
    else if (trackedSourceBBlend && event.SourceRenderedFullWholeScene)
    {
        event.SourceKind = HighResCaptureSourceKind::CleanEngineA2DOutput;
        event.ProductMask = HighResCaptureProductFullEquivalent;
    }
    else
    {
        bool direct3DWithTextBGShapeFullEquivalent =
            event.SourceDirect3DVisible &&
            event.SourceVisibleBitmapMask == 0 &&
            (visibleBGLayers & (1u << 0)) != 0;
        for (int layer = 0; direct3DWithTextBGShapeFullEquivalent && layer < 4; layer++)
        {
            if (!(visibleBGLayers & (1u << layer)))
                continue;

            const u32 type = sourceRenderer->LayerConfig.uBGConfig[layer].Type;
            if (layer == 0)
                direct3DWithTextBGShapeFullEquivalent = type == 6;
            else
                direct3DWithTextBGShapeFullEquivalent = type <= 1;
        }
        const bool direct3DWithTextBGFullEquivalent =
            direct3DWithTextBGShapeFullEquivalent && !event.SourceOBJVisible;
        const bool direct3DWithTextBGAndOBJFullEquivalent =
            direct3DWithTextBGShapeFullEquivalent &&
            event.SourceOBJVisible &&
            event.SourceRenderedFullWholeScene;
        event.SourceTextBGShapeFullEquivalent = direct3DWithTextBGShapeFullEquivalent;
        event.SourceTextBGFullEquivalent =
            direct3DWithTextBGFullEquivalent || direct3DWithTextBGAndOBJFullEquivalent;

        if (event.SourceTextBGFullEquivalent)
        {
            event.SourceKind = HighResCaptureSourceKind::CleanEngineA2DOutput;
            event.ProductMask = HighResCaptureProductFullEquivalent;
        }
        else
        {
            event.SourceKind = HighResCaptureSourceKind::NativeOnlyOutput2D;
            event.RejectReason = HighResCaptureRejectReason::DirtyOrPartialSource;
            event.SourceOBJOnlyDirtyOrPartial =
                direct3DWithTextBGShapeFullEquivalent && event.SourceOBJVisible;
        }
    }
}

void GLRenderer::StoreHighResDisplayCaptureEventProducts(const sLastDisplayCaptureDebug& capture,
                                                         sHighResDisplayCaptureEvent& event)
{
    if ((event.ProductMask & HighResCaptureProductBackground3DUnderlay) &&
        !(capture.SrcA
              ? StoreHighResDisplayCaptureBackgroundProductFromCaptureOutput(capture.DstBlock)
              : StoreHighResDisplayCaptureBackgroundProduct(capture.DstBlock, OutputTex3D)))
    {
        event.ProductMask &= ~HighResCaptureProductBackground3DUnderlay;
    }

    if (event.ProductMask & HighResCaptureProductFullEquivalent)
    {
        if (!StoreHighResDisplayCaptureFullProductFromCaptureOutput(capture.DstBlock))
            event.ProductMask &= ~HighResCaptureProductFullEquivalent;
    }

    if (event.ProductMask == 0 &&
        event.RejectReason == HighResCaptureRejectReason::None)
    {
        event.SourceKind = HighResCaptureSourceKind::Unknown;
        event.RejectReason = HighResCaptureRejectReason::MissingSourceTexture;
    }
}

bool GLRenderer::TryPromoteCaptureEventFromMainVRAMDisplayEpoch(const sLastDisplayCaptureDebug& capture,
                                                                bool fullDisplay,
                                                                bool sourceAOnly,
                                                                sHighResDisplayCaptureEvent& event)
{
    if (!fullDisplay ||
        !sourceAOnly ||
        capture.DstBlock >= 4 ||
        capture.DstOffset != 0 ||
        capture.UsesSrcB ||
        capture.SrcBUsesTrackedCapture ||
        capture.SrcBSameDstBank ||
        event.RejectReason != HighResCaptureRejectReason::DirtyOrPartialSource ||
        event.SourceKind != HighResCaptureSourceKind::NativeOnlyOutput2D ||
        event.ProductMask != 0)
    {
        return false;
    }

    const auto& epoch = MainVRAMDisplayEpoch[capture.DstBlock];
    if (!epoch.Valid ||
        epoch.CaptureBank != capture.DstBlock ||
        epoch.DstOffset != 0 ||
        epoch.ScreenSwap != event.ScreenSwap ||
        epoch.MainEngineFinalBottom != event.MainEngineFinalBottom ||
        epoch.HasDirtyRows ||
        !(epoch.ProductMask & HighResCaptureProductFullEquivalent) ||
        !IsAcceptedMainVRAMDisplayFullProductSource(epoch.SourceKind) ||
        !MainVRAMDisplayEpochTex[capture.DstBlock])
    {
        return false;
    }

    if (!StoreHighResDisplayCaptureFullProduct(capture.DstBlock,
                                               MainVRAMDisplayEpochTex[capture.DstBlock]))
    {
        return false;
    }

    event.Source3DSerial = epoch.Source3DSerial;
    event.Source3DSceneHash = epoch.Source3DSceneHash;
    event.SourceLayerEnable = epoch.SourceLayerEnable;
    event.SourceBGMode = epoch.SourceBGMode;
    event.SourceVisibleBitmapMask = epoch.SourceVisibleBitmapMask;
    event.SourceDirect3DVisible = epoch.SourceDirect3DVisible;
    event.SourceOBJVisible = epoch.SourceOBJVisible;
    event.SourcePresentationHash = epoch.SourcePresentationHash;
    event.SourceKind = HighResCaptureSourceKind::DerivedMainVRAMDisplayEpoch;
    event.ProductMask = HighResCaptureProductFullEquivalent;
    event.RejectReason = HighResCaptureRejectReason::None;
    return true;
}

void GLRenderer::PublishHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture,
                                                   const sHighResDisplayCaptureEvent& event,
                                                   bool fullDisplay)
{
    if (capture.CapSize == 0)
    {
        if (capture.DstBlock < 4)
        {
            HighResDisplayCapture256Event[capture.DstBlock] = {};
            MainVRAMDisplayExactProductEvent[capture.DstBlock] = {};
            InvalidateMainVRAMDisplayEpochForBank(capture.DstBlock, 2,
                                                   capture.DstOffset,
                                                   capture.CapSize,
                                                   true);
            InvalidateCaptureBackgroundEpochForBank(capture.DstBlock);
        }
        LastHighResDisplayCaptureEvent = event;
        return;
    }

    if (capture.DstBlock < 4 &&
        (!fullDisplay ||
         capture.DstOffset != 0 ||
         event.RejectReason != HighResCaptureRejectReason::None ||
         !(event.ProductMask & HighResCaptureProductFullEquivalent) ||
         !IsAcceptedMainVRAMDisplayFullProductSource(event.SourceKind) ||
         HighResDisplayCaptureFullTex[capture.DstBlock] == 0))
    {
        MainVRAMDisplayExactProductEvent[capture.DstBlock] = {};
    }

    if (event.RejectReason != HighResCaptureRejectReason::None ||
        !(event.ProductMask & HighResCaptureProductBackground3DUnderlay))
    {
        InvalidateCaptureBackgroundEpochForBank(capture.DstBlock);
        InvalidateMainVRAMDisplayEpochForBank(capture.DstBlock, 3,
                                               capture.DstOffset,
                                               capture.CapSize,
                                               true);
        if (fullDisplay)
            InvalidateCaptureBackgroundEpoch();
    }

    HighResDisplayCapture256Event[capture.DstBlock] = event;
    if (capture.DstBlock < 4 &&
        fullDisplay &&
        capture.DstOffset == 0 &&
        event.RejectReason == HighResCaptureRejectReason::None &&
        (event.ProductMask & HighResCaptureProductFullEquivalent) &&
        IsAcceptedMainVRAMDisplayFullProductSource(event.SourceKind) &&
        HighResDisplayCaptureFullTex[capture.DstBlock] != 0)
    {
        MainVRAMDisplayExactProductEvent[capture.DstBlock] = event;
    }
    if (event.RejectReason == HighResCaptureRejectReason::None &&
        (event.ProductMask & HighResCaptureProductFullEquivalent))
    {
        UpdateMainVRAMDisplayEpochFromEvent(event);
    }
    if (event.RejectReason == HighResCaptureRejectReason::None &&
        (event.ProductMask & HighResCaptureProductBackground3DUnderlay))
    {
        const int routeSlot = event.MainEngineFinalBottom ? 1 : 0;
        UpdateCaptureBackgroundEpochForRoute(routeSlot, event);
    }
    LastHighResDisplayCaptureEvent = event;
}

void GLRenderer::RecordHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture)
{
    GLRenderer2D* sourceRenderer = nullptr;
    sHighResDisplayCaptureEvent event =
        BuildHighResDisplayCaptureEventMetadata(capture, sourceRenderer);

    const bool fullDisplay =
        capture.CapSize == 3 &&
        capture.DstWidth == 256 &&
        capture.DstHeight == 192 &&
        capture.YStart == 0 &&
        capture.YEnd >= 192;
    const bool sourceAOnly = IsFullDisplayCaptureFromSourceAOnly(capture.CaptureCnt) && !capture.UsesSrcB;
    const auto storeCapturedRouteProduct = [&]()
    {
        if (!sourceRenderer ||
            !fullDisplay ||
            !sourceAOnly ||
            capture.DstBlock >= 4 ||
            event.RejectReason != HighResCaptureRejectReason::None ||
            !(event.ProductMask & HighResCaptureProductFullEquivalent) ||
            !event.SourceRenderedFullWholeScene ||
            !IsAcceptedMainVRAMDisplayFullProductSource(event.SourceKind) ||
            event.Source3DSerial == 0 ||
            event.Source3DSceneHash == 0 ||
            event.SourcePresentationHash == 0)
        {
            return;
        }

        const GLuint fullProductTex = HighResDisplayCaptureFullTex[event.DstBlock];
        if (!fullProductTex)
            return;

        sourceRenderer->StoreRawCaptureBackedRouteProduct(
            event.MainEngineFinalBottom ? 1 : 0,
            fullProductTex,
            0,
            event.Source3DSerial,
            event.Source3DSceneHash,
            event.DstBlock,
            event.SourcePresentationHash,
            event.SourcePresentationHash,
            0,
            192);
    };
    const auto tagCapturedRouteProduct = [&]()
    {
        if (!sourceRenderer ||
            !fullDisplay ||
            capture.DstBlock >= 4 ||
            event.RejectReason != HighResCaptureRejectReason::None ||
            !(event.ProductMask & HighResCaptureProductFullEquivalent))
        {
            return;
        }

        const int routeSlot = event.MainEngineFinalBottom ? 1 : 0;
        sourceRenderer->NoteCaptureBackedRouteProductCaptured(routeSlot,
                                                              event.Serial,
                                                              event.DstBlock,
                                                              event.SourcePresentationHash,
                                                              event.Source3DSerial,
                                                              event.Source3DSceneHash);
    };

    if (capture.CapSize == 0)
    {
        event.RejectReason = HighResCaptureRejectReason::UnsupportedSize;
        PublishHighResDisplayCaptureEvent(capture, event, fullDisplay);
        tagCapturedRouteProduct();
        return;
    }

    if (capture.DstBlock >= 4)
    {
        event.Valid = false;
        event.RejectReason = HighResCaptureRejectReason::BankInvalidated;
        LastHighResDisplayCaptureEvent = event;
        return;
    }

    if (capture.FinalNativeSourceA)
    {
        event.SourceKind = HighResCaptureSourceKind::Unknown;
        event.ProductMask = 0;
        event.RejectReason = HighResCaptureRejectReason::FinalNativePostprocessSource;
        PublishHighResDisplayCaptureEvent(capture, event, fullDisplay);
        tagCapturedRouteProduct();
        return;
    }

    ClassifyHighResDisplayCaptureEvent(capture, sourceRenderer, fullDisplay, sourceAOnly, event);
    StoreHighResDisplayCaptureEventProducts(capture, event);
    TryPromoteCaptureEventFromMainVRAMDisplayEpoch(capture, fullDisplay, sourceAOnly, event);
    PublishHighResDisplayCaptureEvent(capture, event, fullDisplay);
    storeCapturedRouteProduct();
    tagCapturedRouteProduct();
}

bool GLRenderer::StoreHighResDisplayCaptureBackgroundProduct(u32 captureBank, GLuint sourceTex)
{
    if (captureBank >= 4)
        return false;

    return StoreHighResDisplayCaptureProduct(HighResDisplayCaptureBackgroundFB[captureBank],
                                             HighResDisplayCaptureBackgroundTex[captureBank],
                                             HighResDisplayCaptureBackgroundReadFB,
                                             sourceTex);
}

bool GLRenderer::StoreHighResDisplayCaptureFullProduct(u32 captureBank, GLuint sourceTex)
{
    if (captureBank >= 4)
        return false;

    return StoreHighResDisplayCaptureProduct(HighResDisplayCaptureFullFB[captureBank],
                                             HighResDisplayCaptureFullTex[captureBank],
                                              HighResDisplayCaptureFullReadFB,
                                              sourceTex);
}

bool GLRenderer::StoreHighResDisplayCaptureBackgroundProductFromCaptureOutput(u32 captureBank)
{
    if (captureBank >= 4)
        return false;

    return StoreHighResDisplayCaptureProductFromFramebuffer(HighResDisplayCaptureBackgroundFB[captureBank],
                                                           HighResDisplayCaptureBackgroundTex[captureBank],
                                                           CaptureOutput256FB[captureBank]);
}

bool GLRenderer::StoreHighResDisplayCaptureFullProductFromCaptureOutput(u32 captureBank)
{
    if (captureBank >= 4)
        return false;

    return StoreHighResDisplayCaptureProductFromFramebuffer(HighResDisplayCaptureFullFB[captureBank],
                                                           HighResDisplayCaptureFullTex[captureBank],
                                                           CaptureOutput256FB[captureBank]);
}

bool GLRenderer::UpdateCaptureBackgroundEpochForRoute(int routeSlot,
                                                      const sHighResDisplayCaptureEvent& event)
{
    if (routeSlot < 0 ||
        routeSlot >= 2 ||
        event.DstBlock >= 4 ||
        !ActiveCaptureBackgroundEpochTex[routeSlot] ||
        !ActiveCaptureBackgroundEpochFB[routeSlot] ||
        !IsFullDisplayHighResCaptureEventRecord(event, event.DstBlock) ||
        !(event.ProductMask & HighResCaptureProductBackground3DUnderlay))
    {
        return false;
    }

    if (!StoreHighResDisplayCaptureProduct(ActiveCaptureBackgroundEpochFB[routeSlot],
                                           ActiveCaptureBackgroundEpochTex[routeSlot],
                                           HighResDisplayCaptureBackgroundReadFB,
                                           HighResDisplayCaptureBackgroundTex[event.DstBlock]))
    {
        return false;
    }

    auto& epoch = ActiveCaptureBackgroundEpoch[routeSlot];
    epoch.Valid = true;
    epoch.Serial = event.Serial;
    epoch.Source3DSerial = event.Source3DSerial;
    epoch.Source3DSceneHash = event.Source3DSceneHash;
    epoch.CaptureBank = event.DstBlock;
    epoch.DstBlock = event.DstBlock;
    epoch.DstOffset = event.DstOffset;
    epoch.ScreenSwap = event.ScreenSwap;
    epoch.MainEngineFinalBottom = event.MainEngineFinalBottom;
    epoch.ConsumerRouteSlot = static_cast<u32>(routeSlot);
    epoch.SourceKind = event.SourceKind;
    epoch.ProductMask = event.ProductMask;
    epoch.SourceLayerEnable = event.SourceLayerEnable;
    epoch.SourceBGMode = event.SourceBGMode;
    epoch.SourceVisibleBitmapMask = event.SourceVisibleBitmapMask;
    epoch.SourceDirect3DVisible = event.SourceDirect3DVisible;
    epoch.SourceOBJVisible = event.SourceOBJVisible;
    epoch.SourcePresentationHash = event.SourcePresentationHash;
    epoch.StoredMasterBrightness = event.SourceMasterBrightness;
    epoch.HasStoredEffectState = event.HasSourceEffectState;
    return true;
}

void GLRenderer::InvalidateCaptureBackgroundEpochForBank(u32 captureBank)
{
    if (captureBank >= 4)
        return;

    for (auto& epoch : ActiveCaptureBackgroundEpoch)
    {
        if (epoch.Valid && epoch.DstBlock == captureBank)
            epoch = {};
    }
}

void GLRenderer::InvalidateCaptureBackgroundEpoch(int routeSlot)
{
    if (routeSlot >= 0 && routeSlot < 2)
    {
        ActiveCaptureBackgroundEpoch[routeSlot] = {};
        return;
    }

    for (auto& epoch : ActiveCaptureBackgroundEpoch)
        epoch = {};
}

bool GLRenderer::StoreHighResDisplayCaptureProduct(GLuint dstFB,
                                                   GLuint dstTex,
                                                   GLuint readFB,
                                                   GLuint sourceTex)
{
    if (!dstFB ||
        !dstTex ||
        !readFB ||
        !sourceTex ||
        !glBlitFramebuffer)
    {
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sourceTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFB);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dstTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    return true;
}

bool GLRenderer::StoreHighResDisplayCaptureProductFromFramebuffer(GLuint dstFB,
                                                                  GLuint dstTex,
                                                                  GLuint sourceFB)
{
    if (!dstFB ||
        !dstTex ||
        !sourceFB ||
        !glBlitFramebuffer)
    {
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFB);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFB);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dstTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    return true;
}

bool GLRenderer::UpdateMainVRAMDisplayEpochFromEvent(const sHighResDisplayCaptureEvent& event)
{
    if (event.DstBlock >= 4 ||
        event.DstOffset != 0 ||
        event.RejectReason != HighResCaptureRejectReason::None ||
        !(event.ProductMask & HighResCaptureProductFullEquivalent) ||
        !IsAcceptedMainVRAMDisplayFullProductSource(event.SourceKind) ||
        !HighResDisplayCaptureFullTex[event.DstBlock] ||
        !MainVRAMDisplayEpochTex[event.DstBlock] ||
        !MainVRAMDisplayEpochFB[event.DstBlock])
    {
        if (event.DstBlock < 4)
            InvalidateMainVRAMDisplayEpochForBank(event.DstBlock, 4,
                                                   event.DstOffset,
                                                   event.CapSize,
                                                   true);
        return false;
    }

    if (event.SourceOBJVisible)
        return false;

    if (!StoreHighResDisplayCaptureProduct(MainVRAMDisplayEpochFB[event.DstBlock],
                                           MainVRAMDisplayEpochTex[event.DstBlock],
                                           HighResDisplayCaptureFullReadFB,
                                           HighResDisplayCaptureFullTex[event.DstBlock]))
    {
        InvalidateMainVRAMDisplayEpochForBank(event.DstBlock, 5,
                                               event.DstOffset,
                                               event.CapSize,
                                               true);
        return false;
    }

    auto& epoch = MainVRAMDisplayEpoch[event.DstBlock];
    epoch.Valid = true;
    epoch.Serial = event.Serial;
    epoch.Source3DSerial = event.Source3DSerial;
    epoch.Source3DSceneHash = event.Source3DSceneHash;
    epoch.CaptureCnt = event.CaptureCnt;
    epoch.CaptureBank = event.DstBlock;
    epoch.DstOffset = event.DstOffset;
    epoch.ScreenSwap = event.ScreenSwap;
    epoch.MainEngineFinalBottom = event.MainEngineFinalBottom;
    epoch.SourceKind = event.SourceKind;
    epoch.ProductMask = event.ProductMask;
    epoch.SourceLayerEnable = event.SourceLayerEnable;
    epoch.SourceBGMode = event.SourceBGMode;
    epoch.SourceVisibleBitmapMask = event.SourceVisibleBitmapMask;
    epoch.SourceDirect3DVisible = event.SourceDirect3DVisible;
    epoch.SourceOBJVisible = event.SourceOBJVisible;
    epoch.SourcePresentationHash = event.SourcePresentationHash;
    epoch.HasDirtyRows = false;
    epoch.DirtyYStart = 192;
    epoch.DirtyYEnd = 0;
    return true;
}

void GLRenderer::InvalidateMainVRAMDisplayEpochForBank(u32 captureBank,
                                                       u32 reason,
                                                       u32 start,
                                                       u32 len,
                                                       bool complete)
{
    if (captureBank >= 4)
        return;

    MainVRAMDisplayEpochInvalidationDebug.Reason = reason;
    MainVRAMDisplayEpochInvalidationDebug.Bank = captureBank;
    MainVRAMDisplayEpochInvalidationDebug.Start = start;
    MainVRAMDisplayEpochInvalidationDebug.Len = len;
    MainVRAMDisplayEpochInvalidationDebug.Complete = complete;
    MainVRAMDisplayEpoch[captureBank] = {};
}

bool GLRenderer::IsFullDisplaySourceACaptureRecord(const sLastDisplayCaptureDebug& capture,
                                                   u32 expectedBlock) const
{
    return capture.Valid &&
           capture.DstBlock == expectedBlock &&
           capture.DstWidth == 256 &&
           capture.DstHeight == 192 &&
           capture.YStart == 0 &&
           capture.YEnd >= 192 &&
           IsFullDisplayCaptureFromSourceAOnly(capture.CaptureCnt);
}

bool GLRenderer::IsFullDisplayHighResCaptureEventRecord(const sHighResDisplayCaptureEvent& event,
                                                        u32 expectedBlock) const
{
    return event.Valid &&
           event.DstBlock == expectedBlock &&
           event.DstWidth == 256 &&
           event.DstHeight == 192 &&
           event.YStart == 0 &&
           event.YEnd >= 192 &&
           IsFullDisplayCaptureFromSourceAOnly(event.CaptureCnt);
}

bool GLRenderer::IsFullDisplayHighResCaptureExactReplacementRecord(
    const sHighResDisplayCaptureEvent& event,
    u32 expectedBlock) const
{
    return event.Valid &&
           event.DstBlock == expectedBlock &&
           event.DstWidth == 256 &&
           event.DstHeight == 192 &&
           event.YStart == 0 &&
           event.YEnd >= 192;
}

u32 GLRenderer::DisplayCapture256ValidMask() const
{
    u32 mask = 0;
    for (u32 block = 0; block < 4; block++)
    {
        if (LastDisplayCapture256Debug[block].Valid)
            mask |= 1u << block;
    }

    return mask;
}

u32 GLRenderer::DisplayCapture256FullSourceAMask() const
{
    u32 mask = 0;
    for (u32 block = 0; block < 4; block++)
    {
        if (IsFullDisplaySourceACaptureRecord(LastDisplayCapture256Debug[block], block))
            mask |= 1u << block;
    }

    return mask;
}

u32 GLRenderer::HighResDisplayCapture256ValidMask() const
{
    u32 mask = 0;
    for (u32 block = 0; block < 4; block++)
    {
        if (HighResDisplayCapture256Event[block].Valid)
            mask |= 1u << block;
    }

    return mask;
}

u32 GLRenderer::HighResDisplayCapture256SourceKindMask(HighResCaptureSourceKind sourceKind) const
{
    u32 mask = 0;
    for (u32 block = 0; block < 4; block++)
    {
        if (HighResDisplayCapture256Event[block].Valid &&
            HighResDisplayCapture256Event[block].SourceKind == sourceKind)
        {
            mask |= 1u << block;
        }
    }

    return mask;
}

u32 GLRenderer::HighResDisplayCapture256ProductMask(u32 productMask) const
{
    u32 mask = 0;
    for (u32 block = 0; block < 4; block++)
    {
        if (HighResDisplayCapture256Event[block].Valid &&
            (HighResDisplayCapture256Event[block].ProductMask & productMask))
        {
            mask |= 1u << block;
        }
    }

    return mask;
}

bool GLRenderer::GetHighResDisplayCaptureEventForBG(u32 type,
                                                    u32 tileOffset,
                                                    u64& serial,
                                                    u32& sourceKind,
                                                    u32& productMask,
                                                    u32& rejectReason) const
{
    serial = 0;
    sourceKind = static_cast<u32>(HighResCaptureSourceKind::None);
    productMask = 0;
    rejectReason = static_cast<u32>(HighResCaptureRejectReason::None);

    if (type != 8)
    {
        rejectReason = static_cast<u32>(HighResCaptureRejectReason::UnsupportedSize);
        return false;
    }

    const u32 captureBank = tileOffset & 0x3;
    const auto& event = HighResDisplayCapture256Event[captureBank];
    serial = event.Serial;
    sourceKind = static_cast<u32>(event.SourceKind);
    productMask = event.ProductMask;
    rejectReason = static_cast<u32>(event.RejectReason);

    if (!IsFullDisplayHighResCaptureEventRecord(event, captureBank))
    {
        if (rejectReason == static_cast<u32>(HighResCaptureRejectReason::None))
            rejectReason = static_cast<u32>(HighResCaptureRejectReason::BankInvalidated);
        return false;
    }

    return true;
}

GLuint GLRenderer::GetHighResDisplayCaptureBackgroundTexForBG(u32 type,
                                                              u32 tileOffset,
                                                              u64& serial,
                                                              u32& sourceKind,
                                                              u32& productMask,
                                                              u32& rejectReason) const
{
    if (!GetHighResDisplayCaptureEventForBG(type, tileOffset, serial, sourceKind, productMask, rejectReason))
        return 0;

    if ((productMask & HighResCaptureProductBackground3DUnderlay) == 0)
        return 0;

    const u32 captureBank = tileOffset & 0x3;
    if (captureBank >= 4)
        return 0;

    return HighResDisplayCaptureBackgroundTex[captureBank];
}

GLuint GLRenderer::GetHighResDisplayCaptureFullTexForBG(u32 type,
                                                        u32 tileOffset,
                                                        u64& serial,
                                                        u32& sourceKind,
                                                        u32& productMask,
                                                        u32& rejectReason) const
{
    if (!GetHighResDisplayCaptureEventForBG(type, tileOffset, serial, sourceKind, productMask, rejectReason))
        return 0;

    if ((productMask & HighResCaptureProductFullEquivalent) == 0)
        return 0;

    const u32 captureBank = tileOffset & 0x3;
    if (captureBank >= 4)
        return 0;

    return HighResDisplayCaptureFullTex[captureBank];
}

bool GLRenderer::IsSourceAOnlyFullDisplayCapture(u32 capcnt) const
{
    const u32 srcA = (capcnt >> 24) & 0x1;
    if (srcA != 0)
        return false;

    return IsFullDisplayCaptureFromSourceAOnly(capcnt);
}

bool GLRenderer::IsFullDisplayCaptureFromSourceAOnly(u32 capcnt) const
{
    const u32 capsize = (capcnt >> 20) & 0x3;
    if (capsize != 3)
        return false;

    const u32 dstmode = (capcnt >> 29) & 0x3;
    const u32 eva = std::min(capcnt & 0x1F, 16u);
    const u32 evb = std::min((capcnt >> 8) & 0x1F, 16u);

    if (dstmode == 0)
        return true;
    return (dstmode == 2 || dstmode == 3) && eva > 0 && evb == 0;
}

bool GLRenderer::IsCurrentSourceAOnlyFullDisplayCaptureBlock(u32 captureBank) const
{
    const u32 currentDstBank = (GPU.CaptureCnt >> 16) & 0x3;
    if (GPU.CaptureEnable &&
        currentDstBank == captureBank &&
        IsSourceAOnlyFullDisplayCapture(GPU.CaptureCnt))
        return true;

    return LastDisplayCaptureDebug.Valid &&
           LastDisplayCaptureDebug.DstBlock == captureBank &&
           IsSourceAOnlyFullDisplayCapture(LastDisplayCaptureDebug.CaptureCnt);
}

bool GLRenderer::IsCurrentSourceAOnlyFullDisplayCaptureBG(u32 type, u32 tileOffset) const
{
    if (type != 8)
        return false;

    return IsCurrentSourceAOnlyFullDisplayCaptureBlock(tileOffset & 0x3);
}

bool GLRenderer::IsCurrentSourceAOnlyFullDisplayCaptureOBJ(u32 type, u32 tileStride) const
{
    if (type == 3)
        return IsCurrentSourceAOnlyFullDisplayCaptureBlock((tileStride >> 2) & 0x3);
    if (type == 4)
        return IsCurrentSourceAOnlyFullDisplayCaptureBlock(tileStride & 0x3);
    return false;
}

bool GLRenderer::IsCurrentFullDisplayCaptureFromSourceABlock(u32 captureBank) const
{
    const u32 currentDstBank = (GPU.CaptureCnt >> 16) & 0x3;
    if (GPU.CaptureEnable &&
        currentDstBank == captureBank &&
        IsFullDisplayCaptureFromSourceAOnly(GPU.CaptureCnt))
        return true;

    if (captureBank >= 4)
        return false;

    return IsFullDisplaySourceACaptureRecord(LastDisplayCapture256Debug[captureBank], captureBank);
}

bool GLRenderer::IsCurrentFullDisplayCaptureFromSourceABG(u32 type, u32 tileOffset) const
{
    if (type != 8)
        return false;

    return IsCurrentFullDisplayCaptureFromSourceABlock(tileOffset & 0x3);
}

std::string GLRenderer::DescribeFinalScreenSource(int layer) const
{
    int engineALines = 0;
    int engineBLines = 0;

    for (int line = 0; line < 192; line++)
    {
        const bool swapped = FinalPassConfig.uScreenSwap[line] != 0;
        const bool sourceIsEngineA = (layer == 0) ? swapped : !swapped;
        if (sourceIsEngineA)
            engineALines++;
        else
            engineBLines++;
    }

    if (engineALines == 192)
        return "engine A/main";
    if (engineBLines == 192)
        return "engine B/sub";

    return "mixed: engine A/main " + std::to_string(engineALines) +
           " lines, engine B/sub " + std::to_string(engineBLines) + " lines";
}

std::string GLRenderer::DescribeMainDisplayRoute() const
{
    const u32 modeA = (DispCntA >> 16) & 0x3;
    if (modeA == 0)
        return "engine A display disabled";
    if (modeA == 1)
        return "engine A BG/OBJ compositor output";
    if (modeA == 3)
        return "engine A display FIFO";

    const u32 vrambank = (DispCntA >> 18) & 0x3;
    std::string route = "engine A VRAM display: bank ";
    route += static_cast<char>('A' + vrambank);
    route += " / LCDC ";
    route += (GPU.VRAMMap_LCDC & (1u << vrambank)) ? "mapped" : "not mapped";

    if (Aux0VRAMCap >= 0)
    {
        route += ", tracked capture layer ";
        route += std::to_string(Aux0VRAMCap);
    }
    else
        route += ", no tracked capture layer";

    return route;
}

std::string GLRenderer::DescribeLastDisplayCapture() const
{
    if (!LastDisplayCaptureDebug.Valid)
        return "none since renderer reset";

    auto dstModeName = [](u32 mode) -> const char*
    {
        switch (mode)
        {
        case 0: return "source A only";
        case 1: return "source B only";
        case 2: return "source A+B blend";
        case 3: return "source A+B blend";
        default: return "unknown";
        }
    };

    const auto& cap = LastDisplayCaptureDebug;
    char capcntText[9];
    snprintf(capcntText, sizeof(capcntText), "%08X", cap.CaptureCnt);

    std::string text;
    text += "DISPCAPCNT: 0x";
    text += capcntText;
    text += "\n  destination: bank ";
    text += static_cast<char>('A' + cap.DstBlock);
    text += ", offset ";
    text += std::to_string(cap.DstOffset);
    text += ", size ";
    text += std::to_string(cap.DstWidth) + "x" + std::to_string(cap.DstHeight);
    text += ", captured lines ";
    text += std::to_string(cap.YStart) + "-" + std::to_string(cap.YEnd);

    text += "\n  mode: ";
    text += dstModeName(cap.DstMode);
    text += ", EVA ";
    text += std::to_string(cap.EVA);
    text += ", EVB ";
    text += std::to_string(cap.EVB);

    text += "\n  source A: ";
    text += cap.SrcA ? "3D renderer output" : "engine A 2D output";

    text += "\n  source B: ";
    if (cap.SrcB)
        text += "display FIFO";
    else
    {
        text += "VRAM bank ";
        text += static_cast<char>('A' + cap.SrcBBlock);
        text += ", offset ";
        text += std::to_string(cap.SrcBOffset);
    }

    text += "\n  source B used by mode: ";
    text += cap.UsesSrcB ? "yes" : "no";
    text += ", tracked capture input: ";
    if (cap.SrcBUsesTrackedCapture)
    {
        text += "yes, layer ";
        text += std::to_string(cap.SrcBTrackedLayer);
        text += cap.SrcBSameDstBank ? " (same destination bank blit)" : " (different bank)";
    }
    else
        text += "no";

    return text;
}

bool GLRenderer::ReadWholeScene2DDebugView(int screen,
                                           WholeScene2DDebugView view,
                                           int& width,
                                           int& height,
                                           std::vector<u32>& rgba,
                                           std::string* status)
{
    auto fixedBankIndex = [](WholeScene2DDebugView view,
                             WholeScene2DDebugView first) -> int
    {
        const int index = static_cast<int>(view) - static_cast<int>(first);
        return (index >= 0 && index < 4) ? index : -1;
    };
    auto readTexture2D = [&](GLuint tex,
                             int texWidth,
                             int texHeight) -> bool
    {
        if (!tex || texWidth <= 0 || texHeight <= 0)
            return false;

        GLint prevActiveTexture = GL_TEXTURE0;
        GLint prevBinding = 0;
        GLint prevPackAlignment = 4;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);
        glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

        width = texWidth;
        height = texHeight;
        rgba.resize(static_cast<size_t>(width) * height);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
        glBindTexture(GL_TEXTURE_2D, prevBinding);
        glActiveTexture(prevActiveTexture);
        return true;
    };
    auto readFramebuffer = [&](GLuint fb,
                               int fbWidth,
                               int fbHeight) -> bool
    {
        if (!fb || fbWidth <= 0 || fbHeight <= 0)
            return false;

        GLint prevReadFB = 0;
        GLint prevReadBuffer = GL_COLOR_ATTACHMENT0;
        GLint prevPackAlignment = 4;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFB);
        glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

        width = fbWidth;
        height = fbHeight;
        rgba.resize(static_cast<size_t>(width) * height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFB);
        glReadBuffer(prevReadBuffer);
        return true;
    };
    auto appendEventStatus = [](std::string& text,
                                const sHighResDisplayCaptureEvent& event)
    {
        text += "\n  event valid: ";
        text += event.Valid ? "yes" : "no";
        text += "\n  event serial: ";
        text += std::to_string(event.Serial);
        text += "\n  source 3D serial: ";
        text += std::to_string(event.Source3DSerial);
        text += "\n  source 3D scene hash: ";
        text += std::to_string(event.Source3DSceneHash);
        text += "\n  source presentation hash: ";
        text += std::to_string(event.SourcePresentationHash);
        text += "\n  source kind: ";
        text += std::to_string(static_cast<u32>(event.SourceKind));
        text += "\n  product mask: ";
        text += std::to_string(event.ProductMask);
        text += "\n  reject reason: ";
        text += std::to_string(static_cast<u32>(event.RejectReason));
        text += "\n  destination: bank ";
        text += static_cast<char>('A' + event.DstBlock);
        text += ", offset ";
        text += std::to_string(event.DstOffset);
        text += "\n  route bottom: ";
        text += event.MainEngineFinalBottom ? "yes" : "no";
    };

    const int rawBank = fixedBankIndex(view, WholeScene2DDebugView::MainVRAMDisplayRawBank0);
    if (rawBank >= 0)
    {
        width = 256;
        height = 192;
        rgba.assign(static_cast<size_t>(width) * height, 0xFF000000u);

        const u16* vram = reinterpret_cast<const u16*>(GPU.VRAM[rawBank]);
        auto expand5 = [](u16 value) -> u32
        {
            return (value << 3) | (value >> 2);
        };

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                const u16 pixel = vram[y * width + x];
                const u32 r = expand5(pixel & 0x1Fu);
                const u32 g = expand5((pixel >> 5) & 0x1Fu);
                const u32 b = expand5((pixel >> 10) & 0x1Fu);
                rgba[static_cast<size_t>(y) * width + x] =
                    r | (g << 8) | (b << 16) | 0xFF000000u;
            }
        }

        if (status)
        {
            const bool mapped = (GPU.VRAMMap_LCDC & (1u << rawBank)) != 0;
            *status = "Raw native-resolution CPU VRAM interpreted as a 256x192 display image.";
            *status += "\n  screen selector: ignored";
            *status += "\n  bank: ";
            *status += static_cast<char>('A' + rawBank);
            *status += mapped ? " mapped to LCDC" : " not mapped to LCDC";
            *status += "\n  main display route: ";
            *status += DescribeMainDisplayRoute();
        }
        return true;
    }

    const int captureOutputBank =
        fixedBankIndex(view, WholeScene2DDebugView::CaptureOutput256Bank0);
    if (captureOutputBank >= 0)
    {
        if (!CaptureOutput256Valid[captureOutputBank])
        {
            if (status)
            {
                *status = "No valid high-resolution 256x256 capture output for bank ";
                *status += static_cast<char>('A' + captureOutputBank);
                *status += ".";
            }
            return false;
        }

        if (!readFramebuffer(CaptureOutput256FB[captureOutputBank],
                             256 * ScaleFactor,
                             256 * ScaleFactor))
        {
            if (status)
                *status = "Capture output framebuffer is unavailable.";
            return false;
        }

        if (status)
        {
            *status = "High-resolution 256x256 display-capture output layer.";
            *status += "\n  screen selector: ignored";
            *status += "\n  bank: ";
            *status += static_cast<char>('A' + captureOutputBank);
            *status += "\n  size: ";
            *status += std::to_string(width) + "x" + std::to_string(height);
            appendEventStatus(*status, HighResDisplayCapture256Event[captureOutputBank]);
        }
        return true;
    }

    const int fullProductBank =
        fixedBankIndex(view, WholeScene2DDebugView::HighResDisplayCaptureFullBank0);
    if (fullProductBank >= 0)
    {
        const auto& event = HighResDisplayCapture256Event[fullProductBank];
        if (!event.Valid || !(event.ProductMask & HighResCaptureProductFullEquivalent))
        {
            if (status)
            {
                *status = "No valid full-equivalent high-resolution capture product for bank ";
                *status += static_cast<char>('A' + fullProductBank);
                *status += ".";
                appendEventStatus(*status, event);
            }
            return false;
        }
        if (!readTexture2D(HighResDisplayCaptureFullTex[fullProductBank], ScreenW, ScreenH))
        {
            if (status)
                *status = "Full-equivalent capture product texture is unavailable.";
            return false;
        }

        if (status)
        {
            *status = "High-resolution full-equivalent display-capture product.";
            *status += "\n  screen selector: ignored";
            *status += "\n  bank: ";
            *status += static_cast<char>('A' + fullProductBank);
            *status += "\n  size: ";
            *status += std::to_string(width) + "x" + std::to_string(height);
            appendEventStatus(*status, event);
        }
        return true;
    }

    const int backgroundProductBank =
        fixedBankIndex(view, WholeScene2DDebugView::HighResDisplayCaptureBackgroundBank0);
    if (backgroundProductBank >= 0)
    {
        const auto& event = HighResDisplayCapture256Event[backgroundProductBank];
        if (!event.Valid || !(event.ProductMask & HighResCaptureProductBackground3DUnderlay))
        {
            if (status)
            {
                *status = "No valid background/3D-underlay capture product for bank ";
                *status += static_cast<char>('A' + backgroundProductBank);
                *status += ".";
                appendEventStatus(*status, event);
            }
            return false;
        }
        if (!readTexture2D(HighResDisplayCaptureBackgroundTex[backgroundProductBank], ScreenW, ScreenH))
        {
            if (status)
                *status = "Background/3D-underlay capture product texture is unavailable.";
            return false;
        }

        if (status)
        {
            *status = "High-resolution background/3D-underlay display-capture product.";
            *status += "\n  screen selector: ignored";
            *status += "\n  bank: ";
            *status += static_cast<char>('A' + backgroundProductBank);
            *status += "\n  size: ";
            *status += std::to_string(width) + "x" + std::to_string(height);
            appendEventStatus(*status, event);
        }
        return true;
    }

    const int fixedEpochBank =
        fixedBankIndex(view, WholeScene2DDebugView::MainVRAMDisplayEpochBank0);
    if (fixedEpochBank >= 0)
    {
        const auto& epoch = MainVRAMDisplayEpoch[fixedEpochBank];
        if (!epoch.Valid || !MainVRAMDisplayEpochTex[fixedEpochBank])
        {
            if (status)
            {
                *status = "No valid main VRAM display epoch texture for bank ";
                *status += static_cast<char>('A' + fixedEpochBank);
                *status += ".";
            }
            return false;
        }
        if (!readTexture2D(MainVRAMDisplayEpochTex[fixedEpochBank], ScreenW, ScreenH))
        {
            if (status)
                *status = "Main VRAM display epoch texture is unavailable.";
            return false;
        }

        if (status)
        {
            *status = "Main VRAM display epoch texture before dirty-row native overlay.";
            *status += "\n  screen selector: ignored";
            *status += "\n  bank: ";
            *status += static_cast<char>('A' + fixedEpochBank);
            *status += "\n  size: ";
            *status += std::to_string(width) + "x" + std::to_string(height);
            *status += "\n  serial: ";
            *status += std::to_string(epoch.Serial);
            *status += "\n  source 3D serial: ";
            *status += std::to_string(epoch.Source3DSerial);
            *status += "\n  source 3D scene hash: ";
            *status += std::to_string(epoch.Source3DSceneHash);
            *status += "\n  source presentation hash: ";
            *status += std::to_string(epoch.SourcePresentationHash);
            *status += "\n  source kind: ";
            *status += std::to_string(static_cast<u32>(epoch.SourceKind));
            *status += "\n  product mask: ";
            *status += std::to_string(epoch.ProductMask);
            *status += "\n  dirty rows: ";
            if (epoch.HasDirtyRows)
                *status += std::to_string(epoch.DirtyYStart) + "-" + std::to_string(epoch.DirtyYEnd);
            else
                *status += "none";
        }
        return true;
    }

    if (view == WholeScene2DDebugView::MainVRAMDisplayRaw)
    {
        width = 256;
        height = 192;
        rgba.clear();

        const u32 finalDispModeA = (DispCntA >> 16) & 0x3u;
        if (finalDispModeA != 2)
        {
            if (status)
                *status = "Main engine is not currently using VRAM display mode.";
            return false;
        }

        const u32 displayBank = (DispCntA >> 18) & 0x3u;
        if (displayBank >= 4)
        {
            if (status)
                *status = "Main VRAM display bank is invalid.";
            return false;
        }

        rgba.assign(static_cast<size_t>(width) * height, 0xFF000000u);
        const bool mapped = (GPU.VRAMMap_LCDC & (1u << displayBank)) != 0;
        if (mapped)
        {
            const u16* vram = reinterpret_cast<const u16*>(GPU.VRAM[displayBank]);
            auto expand5 = [](u16 value) -> u32
            {
                return (value << 3) | (value >> 2);
            };

            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    const u16 pixel = vram[y * width + x];
                    const u32 r = expand5(pixel & 0x1Fu);
                    const u32 g = expand5((pixel >> 5) & 0x1Fu);
                    const u32 b = expand5((pixel >> 10) & 0x1Fu);
                    rgba[static_cast<size_t>(y) * width + x] =
                        r | (g << 8) | (b << 16) | 0xFF000000u;
                }
            }
        }

        if (status)
        {
            *status = "Raw native-resolution CPU VRAM selected by main engine VRAM display.";
            *status += "\n  screen selector: ignored";
            *status += "\n  bank: ";
            *status += static_cast<char>('A' + displayBank);
            *status += mapped ? " mapped" : " not mapped";
            *status += "\n  size: 256x192";
            *status += "\n  output 3D serial: ";
            *status += std::to_string(Output3DSerial);
            if (WholeSceneDebugViewsActive.load(std::memory_order_relaxed))
            {
                const auto& debug = VRAMDisplayWriteDebug;
                *status += "\n  VRAM writes this frame: ";
                *status += std::to_string(debug.WriteCount);
                *status += " total, ";
                *status += std::to_string(debug.ChangedWriteCount);
                *status += " changed";
                *status += "\n  visible display-bank writes: ";
                *status += std::to_string(debug.DisplayWriteCount);
                *status += " total, ";
                *status += std::to_string(debug.DisplayChangedWriteCount);
                *status += " changed";
                *status += "\n  visible changed rows: ";
                if (debug.DisplayDirtyYStart < debug.DisplayDirtyYEnd)
                    *status += std::to_string(debug.DisplayDirtyYStart) + "-" +
                               std::to_string(debug.DisplayDirtyYEnd);
                else
                    *status += "none";
            }
            else
            {
                *status += "\n  VRAM write coverage: inactive because whole-scene debug views are disabled";
            }
            *status += "\n  main display route: ";
            *status += DescribeMainDisplayRoute();
        }
        return true;
    }

    if (view == WholeScene2DDebugView::FinalTop || view == WholeScene2DDebugView::FinalBottom)
    {
        width = ScreenW;
        height = ScreenH;
        rgba.clear();

        if (width <= 0 || height <= 0)
        {
            if (status)
                *status = "Final physical screen output is not currently available.";
            return false;
        }

        const int layer = (view == WholeScene2DDebugView::FinalTop) ? 0 : 1;
        const int frontbuf = BackBuffer ^ 1;
        std::vector<u32> layers(width * height * 2);

        GLint prevActiveTexture = GL_TEXTURE0;
        GLint prevArrayBinding = 0;
        GLint prevPackAlignment = 4;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevArrayBinding);
        glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

        glBindTexture(GL_TEXTURE_2D_ARRAY, FPOutputTex[frontbuf]);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, layers.data());

        glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
        glBindTexture(GL_TEXTURE_2D_ARRAY, prevArrayBinding);
        glActiveTexture(prevActiveTexture);

        const size_t pixelsPerLayer = static_cast<size_t>(width) * height;
        rgba.assign(layers.begin() + (pixelsPerLayer * layer),
                    layers.begin() + (pixelsPerLayer * (layer + 1)));

        if (status)
        {
            *status = (layer == 0)
                ? "Final physical top screen after GL final pass."
                : "Final physical bottom screen after GL final pass.";

            *status += "\nFinal output";
            *status += (layer == 0) ? "\n  top source: " : "\n  bottom source: ";
            *status += DescribeFinalScreenSource(layer);
            *status += "\n  screen selector: ignored for final physical views";
            *status += "\n  main display route: ";
            *status += DescribeMainDisplayRoute();

            auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
            auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
            if (rendA || rendB)
            {
                *status += "\n\nEngine states";
                *status += "\n  A/main: ";
                *status += rendA ? rendA->DescribeWholeSceneScaleState() : "unavailable";
                *status += "\n  B/sub: ";
                *status += rendB ? rendB->DescribeWholeSceneScaleState() : "unavailable";
            }

            *status += "\n\nLast display capture\n  ";
            *status += DescribeLastDisplayCapture();
        }
        return true;
    }

    auto* rend2d = dynamic_cast<GLRenderer2D*>((screen == 0) ? Rend2D_A.get() : Rend2D_B.get());
    if (!rend2d)
    {
        width = 0;
        height = 0;
        rgba.clear();
        if (status)
            *status = "The selected screen does not use the OpenGL 2D renderer.";
        return false;
    }

    return rend2d->ReadWholeSceneDebugView(view, width, height, rgba, status);
}

bool GLRenderer::SetWholeScene2DDebugPoison(bool source3D,
                                            bool native3DResolve,
                                            bool native3DResolveAlpha,
                                            std::string* status)
{
    auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    if (!rendA && !rendB)
    {
        if (status)
            *status = "The active OpenGL renderer does not provide whole-scene 2D debug poison controls.";
        return false;
    }

    if (rendA)
        rendA->SetWholeSceneDebugPoison(source3D, native3DResolve, native3DResolveAlpha);
    if (rendB)
        rendB->SetWholeSceneDebugPoison(source3D, native3DResolve, native3DResolveAlpha);

    if (status)
    {
        *status = "Whole-scene 2D debug poison updated. Source 3D poison: ";
        *status += source3D ? "on" : "off";
        *status += ". Native 3D resolve poison: ";
        *status += native3DResolve ? "on" : "off";
        *status += ". Native 3D resolve alpha force: ";
        *status += native3DResolveAlpha ? "on." : "off.";
    }
    return true;
}

bool GLRenderer::SetWholeScene2DDebugViewsActive(bool active,
                                                 std::string* status)
{
    WholeSceneDebugViewsActive.store(active, std::memory_order_relaxed);

    auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    if (!rendA && !rendB)
    {
        if (status)
            *status = "The active OpenGL renderer does not provide whole-scene 2D debug view controls.";
        return false;
    }

    if (rendA)
        rendA->SetWholeSceneDebugViewsActive(active);
    if (rendB)
        rendB->SetWholeSceneDebugViewsActive(active);

    if (status)
    {
        *status = "Whole-scene 2D debug views ";
        *status += active ? "enabled." : "disabled.";
    }
    return true;
}

void GLRenderer::AppendWholeSceneFrameTimingCSVHeader(std::string& header)
{
    auto add = [&header](const char* name)
    {
        if (!header.empty())
            header += ",";
        header += name;
    };
    auto addName = [&header](const std::string& name)
    {
        if (!header.empty())
            header += ",";
        header += name;
    };
    auto addPhase = [&header](const char* name)
    {
        if (!header.empty())
            header += ",";
        header += name;
        header += "_us";
        header += ",";
        header += name;
        header += "_max_us";
        header += ",";
        header += name;
        header += "_count";
    };

    addPhase("phase_draw_scanline");
    addPhase("phase_draw_sprites");
    addPhase("phase_render_screen");
    addPhase("phase_do_capture");
    addPhase("phase_start_3d");
    addPhase("phase_finish_3d");
    addPhase("phase_restart_3d");
    addPhase("phase_vblank");
    addPhase("phase_vblank_end");
    add("renderer_capture_cnt");
    add("gpu_capture_cnt");
    add("gpu_capture_enable");
    add("last_capture_valid");
    add("last_capture_cnt");
    add("last_capture_ystart");
    add("last_capture_yend");
    add("last_capture_dst_width");
    add("last_capture_dst_height");
    add("last_capture_src_a");
    add("last_capture_src_b");
    add("last_capture_dst_block");
    add("last_capture_dst_offset");
    add("last_capture_cap_size");
    add("last_capture_dst_mode");
    add("last_capture_eva");
    add("last_capture_evb");
    add("last_capture_native_luma_valid");
    add("last_capture_native_luma_x1000");
    add("last_capture_native_luma_dst_block");
    add("capture256_valid_mask");
    add("capture256_full_source_a_mask");
    add("output_3d_serial");
    add("output_3d_scene_hash");
    add("capture_event_serial");
    add("capture_event_source_3d_serial");
    add("capture_event_source_3d_scene_hash");
    add("capture_event_valid_mask");
    add("capture_event_clean3d_mask");
    add("capture_event_clean_2d_output_mask");
    add("capture_event_background_product_mask");
    add("capture_event_full_product_mask");
    add("capture_event_source_kind");
    add("capture_event_product_mask");
    add("capture_event_reject_reason");
    add("capture_event_dst_block");
    add("capture_event_dst_offset");
    add("capture_event_screen_swap");
    add("capture_event_route_bottom");
    add("capture_event_source_layer_enable");
    add("capture_event_source_bitmap_mask");
    add("capture_event_source_direct3d");
    add("capture_event_source_obj");
    add("capture_event_source_path");
    add("capture_event_source_y_start");
    add("capture_event_source_y_end");
    add("capture_event_source_visible_bg_layers");
    add("capture_event_source_bg_layer_types");
    add("capture_event_source_full_whole_scene");
    add("capture_event_source_direct3d_only_background");
    add("capture_event_source_text_bg_shape_full_equivalent");
    add("capture_event_source_text_bg_full_equivalent");
    add("capture_event_source_obj_only_dirty_or_partial");
    add("capture_event_source_presentation_hash");
    add("capture_event_final_native_source");
    add("capture_event_hybrid_product_suppressed");
    add("final_disp_mode_a");
    add("final_disp_mode_b");
    add("final_screen_swap");
    add("final_master_brightness_a");
    add("final_master_brightness_b");
    add("final_bright_mode_a");
    add("final_bright_mode_b");
    add("final_bright_factor_a");
    add("final_bright_factor_b");
    add("final_main_input_brightness_applied");
    add("final_main_input_effect_owner");
    add("final_main_input_effect_state");
    add("final_main_input_tex");
    add("final_sub_input_brightness_applied");
    add("final_sub_input_effect_owner");
    add("final_sub_input_effect_state");
    add("final_sub_input_tex");
    add("final_main_vram_bank");
    add("final_aux0_vramcap");
    add("final_aux_layer");
    add("final_invalid_capture_reseed");
    add("final_main_source");
    add("final_top_source");
    add("final_bottom_source");
    add("final_vram_display_capture_bank");
    add("final_vram_display_capture_offset");
    add("final_vram_display_capture_match");
    add("final_vram_display_capture_serial");
    add("final_vram_display_capture_product_mask");
    add("final_vram_display_capture_source_kind");
    add("final_vram_display_capture_reject_reason");
    add("final_vram_display_replacement_eligible");
    add("final_vram_display_replacement_reject_reason");
    add("final_vram_display_chosen_bank");
    add("final_vram_display_chosen_serial");
    add("final_vram_display_chosen_source_3d_serial");
    add("final_vram_display_chosen_source_3d_scene_hash");
    add("final_vram_display_chosen_source_presentation_hash");
    add("final_vram_display_chosen_source_kind");
    add("final_vram_display_chosen_product_mask");
    add("final_vram_display_chosen_tex");
    add("final_vram_display_chosen_epoch");
    add("final_vram_display_render_time_bank");
    add("final_vram_display_render_time_eligible");
    add("final_vram_display_render_time_reject_reason");
    add("final_vram_display_render_time_used_epoch");
    add("final_vram_display_render_time_chosen_bank");
    add("final_vram_display_render_time_chosen_serial");
    add("final_vram_display_render_time_chosen_source_3d_serial");
    add("final_vram_display_render_time_chosen_source_3d_scene_hash");
    add("final_vram_display_render_time_chosen_source_presentation_hash");
    add("final_vram_display_render_time_chosen_source_kind");
    add("final_vram_display_render_time_chosen_product_mask");
    add("final_vram_display_render_time_chosen_tex");
    add("final_vram_display_render_time_event_valid");
    add("final_vram_display_render_time_event_serial");
    add("final_vram_display_render_time_event_dst_block");
    add("final_vram_display_render_time_event_dst_offset");
    add("final_vram_display_render_time_event_screen_swap");
    add("final_vram_display_render_time_event_main_final_bottom");
    add("final_vram_display_render_time_event_source_obj");
    add("final_vram_display_render_time_event_source_full_whole_scene");
    add("final_vram_display_render_time_event_source_kind");
    add("final_vram_display_render_time_event_product_mask");
    add("final_vram_display_render_time_event_reject_reason");
    add("final_vram_display_render_time_event_full_tex");
    add("final_vram_display_render_time_event_matches_native_capture");
    add("final_vram_display_render_time_exact_event_route_matches");
    add("final_vram_display_render_time_exact_event_product_available");
    add("final_vram_display_render_time_exact_event_product_usable");
    add("final_vram_display_epoch_valid");
    add("final_vram_display_epoch_used");
    add("final_vram_display_epoch_serial");
    add("final_vram_display_epoch_source_3d_serial");
    add("final_vram_display_epoch_source_3d_scene_hash");
    add("final_vram_display_epoch_source_kind");
    add("final_vram_display_epoch_product_mask");
    add("final_vram_display_epoch_dirty_rows");
    add("final_vram_display_epoch_dirty_y_start");
    add("final_vram_display_epoch_dirty_y_end");
    add("final_vram_display_epoch_invalidation_reason");
    add("final_vram_display_epoch_invalidation_bank");
    add("final_vram_display_epoch_invalidation_start");
    add("final_vram_display_epoch_invalidation_len");
    add("final_vram_display_epoch_invalidation_complete");
    for (int bank = 0; bank < 4; bank++)
    {
        const std::string prefix = "capture_bank" + std::to_string(bank) + "_";
        addName(prefix + "native_valid");
        addName(prefix + "native_capture_cnt");
        addName(prefix + "native_dst_block");
        addName(prefix + "native_dst_offset");
        addName(prefix + "native_cap_size");
        addName(prefix + "native_dst_mode");
        addName(prefix + "native_src_a");
        addName(prefix + "native_src_b");
        addName(prefix + "native_uses_src_b");
        addName(prefix + "native_src_b_block");
        addName(prefix + "native_src_b_offset");
        addName(prefix + "native_src_b_tracked_layer");
        addName(prefix + "native_src_b_same_dst");
        addName(prefix + "native_final_native_source_a");
        addName(prefix + "output256_valid");
        addName(prefix + "event_valid");
        addName(prefix + "event_serial");
        addName(prefix + "event_source_3d_serial");
        addName(prefix + "event_source_3d_scene_hash");
        addName(prefix + "event_source_presentation_hash");
        addName(prefix + "event_source_kind");
        addName(prefix + "event_product_mask");
        addName(prefix + "event_reject_reason");
        addName(prefix + "event_dst_block");
        addName(prefix + "event_dst_offset");
        addName(prefix + "event_screen_swap");
        addName(prefix + "event_main_final_bottom");
        addName(prefix + "event_source_layer_enable");
        addName(prefix + "event_source_bitmap_mask");
        addName(prefix + "event_source_direct3d");
        addName(prefix + "event_source_obj");
        addName(prefix + "event_source_path");
        addName(prefix + "event_source_y_start");
        addName(prefix + "event_source_y_end");
        addName(prefix + "event_source_visible_bg_layers");
        addName(prefix + "event_source_bg_layer_types");
        addName(prefix + "event_source_full_whole_scene");
        addName(prefix + "event_source_direct3d_only_background");
        addName(prefix + "event_source_text_bg_shape_full_equivalent");
        addName(prefix + "event_source_text_bg_full_equivalent");
        addName(prefix + "event_source_obj_only_dirty_or_partial");
        addName(prefix + "full_tex_id");
        addName(prefix + "background_tex_id");
        addName(prefix + "epoch_valid");
        addName(prefix + "epoch_serial");
        addName(prefix + "epoch_source_3d_serial");
        addName(prefix + "epoch_source_3d_scene_hash");
        addName(prefix + "epoch_source_presentation_hash");
        addName(prefix + "epoch_source_kind");
        addName(prefix + "epoch_product_mask");
        addName(prefix + "epoch_screen_swap");
        addName(prefix + "epoch_main_final_bottom");
        addName(prefix + "epoch_dirty_rows");
        addName(prefix + "epoch_dirty_y_start");
        addName(prefix + "epoch_dirty_y_end");
        addName(prefix + "epoch_tex_id");
    }
    add("vram_write_count");
    add("vram_write_changed_count");
    add("vram_write_bank_mask");
    add("vram_write_bytes");
    add("vram_write_changed_bytes");
    add("vram_display_write_bank");
    add("vram_display_write_count");
    add("vram_display_write_changed_count");
    add("vram_display_write_bytes");
    add("vram_display_write_changed_bytes");
    add("vram_display_write_first_offset");
    add("vram_display_write_last_end");
    add("vram_display_write_dirty_y_start");
    add("vram_display_write_dirty_y_end");
    add("physical_final_postprocess_enabled");
    add("physical_final_postprocess_applied");
    add("physical_final_postprocess_reject_reason");
    add("physical_final_native_rows");
    add("physical_final_a_native_input_valid");
    add("physical_final_b_native_input_valid");
    add("physical_final_a_input_path");
    add("physical_final_b_input_path");
    add("final_capture_source_a_kind");
    add("final_capture_source_a_native_sized");
    add("final_capture_source_a_range_valid");
    add("final_capture_source_a_used");
}

void GLRenderer::AppendWholeSceneFrameTimingCSVRow(std::string& row) const
{
    auto addInt = [&row](auto value)
    {
        if (!row.empty())
        row += ",";
        row += std::to_string(static_cast<int>(value));
    };
    auto addU64 = [&row](u64 value)
    {
        if (!row.empty())
            row += ",";
        row += std::to_string(value);
    };
    auto addPhase = [&row](const sPhaseTiming& phase)
    {
        if (!row.empty())
            row += ",";
        row += std::to_string(phase.TotalUS);
        row += ",";
        row += std::to_string(phase.MaxUS);
        row += ",";
        row += std::to_string(phase.Count);
    };

    addPhase(WholeSceneFrameTiming.DrawScanline);
    addPhase(WholeSceneFrameTiming.DrawSprites);
    addPhase(WholeSceneFrameTiming.RenderScreen);
    addPhase(WholeSceneFrameTiming.DoCapture);
    addPhase(WholeSceneFrameTiming.Start3D);
    addPhase(WholeSceneFrameTiming.Finish3D);
    addPhase(WholeSceneFrameTiming.Restart3D);
    addPhase(WholeSceneFrameTiming.VBlank);
    addPhase(WholeSceneFrameTiming.VBlankEnd);
    addInt(CaptureCnt);
    addInt(GPU.CaptureCnt);
    addInt(GPU.CaptureEnable);
    addInt(LastDisplayCaptureDebug.Valid);
    addInt(LastDisplayCaptureDebug.CaptureCnt);
    addInt(LastDisplayCaptureDebug.YStart);
    addInt(LastDisplayCaptureDebug.YEnd);
    addInt(LastDisplayCaptureDebug.DstWidth);
    addInt(LastDisplayCaptureDebug.DstHeight);
    addInt(LastDisplayCaptureDebug.SrcA);
    addInt(LastDisplayCaptureDebug.SrcB);
    addInt(LastDisplayCaptureDebug.DstBlock);
    addInt(LastDisplayCaptureDebug.DstOffset);
    addInt(LastDisplayCaptureDebug.CapSize);
    addInt(LastDisplayCaptureDebug.DstMode);
    addInt(LastDisplayCaptureDebug.EVA);
    addInt(LastDisplayCaptureDebug.EVB);
    addInt(LastCaptureNativeLumaDebug.Valid);
    addInt(LastCaptureNativeLumaDebug.LumaX1000);
    addInt(LastCaptureNativeLumaDebug.DstBlock);
    addInt(DisplayCapture256ValidMask());
    addInt(DisplayCapture256FullSourceAMask());
    addU64(Output3DSerial);
    addU64(Output3DSceneHash);
    addU64(LastHighResDisplayCaptureEvent.Serial);
    addU64(LastHighResDisplayCaptureEvent.Source3DSerial);
    addU64(LastHighResDisplayCaptureEvent.Source3DSceneHash);
    addInt(HighResDisplayCapture256ValidMask());
    addInt(HighResDisplayCapture256SourceKindMask(HighResCaptureSourceKind::CleanOutput3D));
    addInt(HighResDisplayCapture256SourceKindMask(HighResCaptureSourceKind::CleanEngineA2DOutput));
    addInt(HighResDisplayCapture256ProductMask(HighResCaptureProductBackground3DUnderlay));
    addInt(HighResDisplayCapture256ProductMask(HighResCaptureProductFullEquivalent));
    addInt(LastHighResDisplayCaptureEvent.SourceKind);
    addInt(LastHighResDisplayCaptureEvent.ProductMask);
    addInt(LastHighResDisplayCaptureEvent.RejectReason);
    addInt(LastHighResDisplayCaptureEvent.DstBlock);
    addInt(LastHighResDisplayCaptureEvent.DstOffset);
    addInt(LastHighResDisplayCaptureEvent.ScreenSwap);
    addInt(LastHighResDisplayCaptureEvent.MainEngineFinalBottom);
    addInt(LastHighResDisplayCaptureEvent.SourceLayerEnable);
    addInt(LastHighResDisplayCaptureEvent.SourceVisibleBitmapMask);
    addInt(LastHighResDisplayCaptureEvent.SourceDirect3DVisible);
    addInt(LastHighResDisplayCaptureEvent.SourceOBJVisible);
    addInt(LastHighResDisplayCaptureEvent.SourceWholeScenePath);
    addInt(LastHighResDisplayCaptureEvent.SourceWholeSceneYStart);
    addInt(LastHighResDisplayCaptureEvent.SourceWholeSceneYEnd);
    addInt(LastHighResDisplayCaptureEvent.SourceVisibleBGLayers);
    addInt(LastHighResDisplayCaptureEvent.SourceBGLayerTypes);
    addInt(LastHighResDisplayCaptureEvent.SourceRenderedFullWholeScene);
    addInt(LastHighResDisplayCaptureEvent.SourceDirect3DOnlyBackground);
    addInt(LastHighResDisplayCaptureEvent.SourceTextBGShapeFullEquivalent);
    addInt(LastHighResDisplayCaptureEvent.SourceTextBGFullEquivalent);
    addInt(LastHighResDisplayCaptureEvent.SourceOBJOnlyDirtyOrPartial);
    addU64(LastHighResDisplayCaptureEvent.SourcePresentationHash);
    addInt(LastDisplayCaptureDebug.FinalNativeSourceA);
    addInt(LastHighResDisplayCaptureEvent.RejectReason ==
           HighResCaptureRejectReason::FinalNativePostprocessSource);

    const int finalDispModeA = (DispCntA >> 16) & 0x3;
    const int finalDispModeB = (DispCntB >> 16) & 0x1;
    const int finalScreenSwap = FinalPassConfig.uScreenSwap[0] ? 1 : 0;
    const int finalMainVRAMBank = finalDispModeA == 2 ? static_cast<int>((DispCntA >> 18) & 0x3) : -1;
    const int finalAuxLayer =
        (finalDispModeA == 2 && Aux0VRAMCap != -1) ? (Aux0VRAMCap >> 2) :
        (finalDispModeA == 3) ? 1 :
        (finalDispModeA == 2) ? 0 : -1;

    // Final VRAM-display replacement reject reasons:
    // 0 eligible, 1 not main VRAM display, 2 no tracked capture source,
    // 3 invalid tracked bank, 4 no matching full-display event,
    // 5 capture event itself rejected, 6 missing full-equivalent product,
    // 7 unsupported product source kind, 8 mixed/OBJ full-frame capture,
    // 9 no valid main VRAM display epoch, 10 full native dirty-row coverage.
    int finalVRAMDisplayCaptureBank = -1;
    int finalVRAMDisplayCaptureOffset = -1;
    int finalVRAMDisplayCaptureMatch = 0;
    u64 finalVRAMDisplayCaptureSerial = 0;
    u32 finalVRAMDisplayCaptureProductMask = 0;
    u32 finalVRAMDisplayCaptureSourceKind = static_cast<u32>(HighResCaptureSourceKind::None);
    u32 finalVRAMDisplayCaptureRejectReason = static_cast<u32>(HighResCaptureRejectReason::None);
    int finalVRAMDisplayReplacementEligible = 0;
    int finalVRAMDisplayReplacementRejectReason = 0;
    int finalVRAMDisplayChosenBank = -1;
    u64 finalVRAMDisplayChosenSerial = 0;
    u64 finalVRAMDisplayChosenSource3DSerial = 0;
    u32 finalVRAMDisplayChosenSource3DSceneHash = 0;
    u32 finalVRAMDisplayChosenSourcePresentationHash = 0;
    u32 finalVRAMDisplayChosenSourceKind = static_cast<u32>(HighResCaptureSourceKind::None);
    u32 finalVRAMDisplayChosenProductMask = 0;
    int finalVRAMDisplayChosenTex = 0;
    int finalVRAMDisplayChosenEpoch = 0;
    int finalVRAMDisplayEpochValid = 0;
    int finalVRAMDisplayEpochUsed = 0;
    u64 finalVRAMDisplayEpochSerial = 0;
    u64 finalVRAMDisplayEpochSource3DSerial = 0;
    u32 finalVRAMDisplayEpochSource3DSceneHash = 0;
    u32 finalVRAMDisplayEpochSourceKind = static_cast<u32>(HighResCaptureSourceKind::None);
    u32 finalVRAMDisplayEpochProductMask = 0;
    int finalVRAMDisplayEpochDirtyRows = 0;
    int finalVRAMDisplayEpochDirtyYStart = -1;
    int finalVRAMDisplayEpochDirtyYEnd = -1;
    bool finalVRAMDisplayUsedEpoch = false;

    if (finalMainVRAMBank >= 0 && finalMainVRAMBank < 4)
    {
        const auto& epoch = MainVRAMDisplayEpoch[finalMainVRAMBank];
        finalVRAMDisplayEpochValid = epoch.Valid ? 1 : 0;
        finalVRAMDisplayEpochSerial = epoch.Serial;
        finalVRAMDisplayEpochSource3DSerial = epoch.Source3DSerial;
        finalVRAMDisplayEpochSource3DSceneHash = epoch.Source3DSceneHash;
        finalVRAMDisplayEpochSourceKind = static_cast<u32>(epoch.SourceKind);
        finalVRAMDisplayEpochProductMask = epoch.ProductMask;
        finalVRAMDisplayEpochDirtyRows = epoch.HasDirtyRows ? 1 : 0;
        finalVRAMDisplayEpochDirtyYStart = epoch.HasDirtyRows ? static_cast<int>(epoch.DirtyYStart) : -1;
        finalVRAMDisplayEpochDirtyYEnd = epoch.HasDirtyRows ? static_cast<int>(epoch.DirtyYEnd) : -1;
    }

    const bool finalVRAMDisplayPersistentReplacement =
        finalDispModeA == 2 &&
        finalMainVRAMBank >= 0 &&
        CanUseMainVRAMDisplayHighResCaptureReplacement(static_cast<u32>(finalMainVRAMBank),
                                                       nullptr,
                                                       &finalVRAMDisplayReplacementRejectReason,
                                                       &finalVRAMDisplayUsedEpoch);
    if (finalDispModeA != 2)
    {
        finalVRAMDisplayReplacementRejectReason = 1;
    }
    else
    {
        finalVRAMDisplayCaptureBank =
            Aux0VRAMCap != -1 ? (Aux0VRAMCap >> 2) : finalMainVRAMBank;
        finalVRAMDisplayCaptureOffset =
            Aux0VRAMCap != -1 ? (Aux0VRAMCap & 0x3) : 0;

        if (finalVRAMDisplayCaptureBank < 0 || finalVRAMDisplayCaptureBank >= 4)
        {
            finalVRAMDisplayReplacementRejectReason = 3;
        }
        else
        {
            const auto& event = HighResDisplayCapture256Event[finalVRAMDisplayCaptureBank];
            finalVRAMDisplayCaptureSerial = event.Serial;
            finalVRAMDisplayCaptureProductMask = event.ProductMask;
            finalVRAMDisplayCaptureSourceKind = static_cast<u32>(event.SourceKind);
            finalVRAMDisplayCaptureRejectReason = static_cast<u32>(event.RejectReason);
            finalVRAMDisplayCaptureMatch =
                IsFullDisplayHighResCaptureExactReplacementRecord(event, static_cast<u32>(finalVRAMDisplayCaptureBank)) &&
                event.DstOffset == static_cast<u32>(finalVRAMDisplayCaptureOffset);

            if (finalVRAMDisplayPersistentReplacement)
            {
                finalVRAMDisplayReplacementEligible = 1;
                finalVRAMDisplayEpochUsed = finalVRAMDisplayUsedEpoch ? 1 : 0;
                finalVRAMDisplayReplacementRejectReason = 0;

                if (finalVRAMDisplayUsedEpoch &&
                    finalMainVRAMBank >= 0 &&
                    finalMainVRAMBank < 4)
                {
                    const auto& chosen = MainVRAMDisplayEpoch[finalMainVRAMBank];
                    finalVRAMDisplayChosenBank = finalMainVRAMBank;
                    finalVRAMDisplayChosenSerial = chosen.Serial;
                    finalVRAMDisplayChosenSource3DSerial = chosen.Source3DSerial;
                    finalVRAMDisplayChosenSource3DSceneHash = chosen.Source3DSceneHash;
                    finalVRAMDisplayChosenSourcePresentationHash = chosen.SourcePresentationHash;
                    finalVRAMDisplayChosenSourceKind = static_cast<u32>(chosen.SourceKind);
                    finalVRAMDisplayChosenProductMask = chosen.ProductMask;
                    finalVRAMDisplayChosenTex = static_cast<int>(MainVRAMDisplayEpochTex[finalMainVRAMBank]);
                    finalVRAMDisplayChosenEpoch = 1;
                }
                else
                {
                    finalVRAMDisplayChosenBank = finalVRAMDisplayCaptureBank;
                    finalVRAMDisplayChosenSerial = event.Serial;
                    finalVRAMDisplayChosenSource3DSerial = event.Source3DSerial;
                    finalVRAMDisplayChosenSource3DSceneHash = event.Source3DSceneHash;
                    finalVRAMDisplayChosenSourcePresentationHash = event.SourcePresentationHash;
                    finalVRAMDisplayChosenSourceKind = static_cast<u32>(event.SourceKind);
                    finalVRAMDisplayChosenProductMask = event.ProductMask;
                    finalVRAMDisplayChosenTex =
                        static_cast<int>(HighResDisplayCaptureFullTex[finalVRAMDisplayCaptureBank]);
                }
            }
        }
    }

    // Source kind values: 0 disabled/white, 1 A output, 2 B output,
    // 3 A VRAM tracked capture, 4 A VRAM raw aux copy, 5 A display FIFO,
    // 6 A output used to reseed an invalid tracked feedback capture.
    const int finalMainSource =
        FinalPassInvalidCaptureReseed ? 6 :
        (finalDispModeA == 0) ? 0 :
        (finalDispModeA == 1) ? 1 :
        (finalDispModeA == 2 && (Aux0VRAMCap != -1 || finalVRAMDisplayReplacementEligible)) ? 3 :
        (finalDispModeA == 2) ? 4 :
        (finalDispModeA == 3) ? 5 : -1;
    const int finalSubSource = finalDispModeB == 0 ? 0 : 2;
    const int finalTopSource = finalScreenSwap ? finalMainSource : finalSubSource;
    const int finalBottomSource = finalScreenSwap ? finalSubSource : finalMainSource;
    const auto* rendA = dynamic_cast<const GLRenderer2D*>(Rend2D_A.get());
    const auto* rendB = dynamic_cast<const GLRenderer2D*>(Rend2D_B.get());

    addInt(finalDispModeA);
    addInt(finalDispModeB);
    addInt(finalScreenSwap);
    addInt(MasterBrightnessA);
    addInt(MasterBrightnessB);
    addInt(FinalPassConfig.uBrightModeA);
    addInt(FinalPassConfig.uBrightModeB);
    addInt(FinalPassConfig.uBrightFactorA);
    addInt(FinalPassConfig.uBrightFactorB);
    addInt(rendA ? rendA->WholeSceneTrace.OutputPresentationMasterBrightnessApplied : 0);
    addInt(rendA ? rendA->WholeSceneTrace.OutputPresentationEffectOwner : 0);
    addInt(rendA ? rendA->WholeSceneTrace.OutputPresentationEffectState : 0);
    addInt(rendA ? rendA->WholeSceneTrace.OutputPresentationTex : 0);
    addInt(rendB ? rendB->WholeSceneTrace.OutputPresentationMasterBrightnessApplied : 0);
    addInt(rendB ? rendB->WholeSceneTrace.OutputPresentationEffectOwner : 0);
    addInt(rendB ? rendB->WholeSceneTrace.OutputPresentationEffectState : 0);
    addInt(rendB ? rendB->WholeSceneTrace.OutputPresentationTex : 0);
    addInt(finalMainVRAMBank);
    addInt(Aux0VRAMCap);
    addInt(finalAuxLayer);
    addInt(FinalPassInvalidCaptureReseed);
    addInt(finalMainSource);
    addInt(finalTopSource);
    addInt(finalBottomSource);
    addInt(finalVRAMDisplayCaptureBank);
    addInt(finalVRAMDisplayCaptureOffset);
    addInt(finalVRAMDisplayCaptureMatch);
    addU64(finalVRAMDisplayCaptureSerial);
    addInt(finalVRAMDisplayCaptureProductMask);
    addInt(finalVRAMDisplayCaptureSourceKind);
    addInt(finalVRAMDisplayCaptureRejectReason);
    addInt(finalVRAMDisplayReplacementEligible);
    addInt(finalVRAMDisplayReplacementRejectReason);
    addInt(finalVRAMDisplayChosenBank);
    addU64(finalVRAMDisplayChosenSerial);
    addU64(finalVRAMDisplayChosenSource3DSerial);
    addU64(finalVRAMDisplayChosenSource3DSceneHash);
    addU64(finalVRAMDisplayChosenSourcePresentationHash);
    addInt(finalVRAMDisplayChosenSourceKind);
    addInt(finalVRAMDisplayChosenProductMask);
    addInt(finalVRAMDisplayChosenTex);
    addInt(finalVRAMDisplayChosenEpoch);
    addInt(FinalVRAMDisplayRenderTrace.DisplayBank);
    addInt(FinalVRAMDisplayRenderTrace.ReplacementEligible);
    addInt(FinalVRAMDisplayRenderTrace.RejectReason);
    addInt(FinalVRAMDisplayRenderTrace.UsedEpoch);
    addInt(FinalVRAMDisplayRenderTrace.ChosenBank);
    addU64(FinalVRAMDisplayRenderTrace.ChosenSerial);
    addU64(FinalVRAMDisplayRenderTrace.ChosenSource3DSerial);
    addU64(FinalVRAMDisplayRenderTrace.ChosenSource3DSceneHash);
    addU64(FinalVRAMDisplayRenderTrace.ChosenSourcePresentationHash);
    addInt(FinalVRAMDisplayRenderTrace.ChosenSourceKind);
    addInt(FinalVRAMDisplayRenderTrace.ChosenProductMask);
    addInt(FinalVRAMDisplayRenderTrace.ChosenTex);
    addInt(FinalVRAMDisplayRenderTrace.EventValid);
    addU64(FinalVRAMDisplayRenderTrace.EventSerial);
    addInt(FinalVRAMDisplayRenderTrace.EventDstBlock);
    addInt(FinalVRAMDisplayRenderTrace.EventDstOffset);
    addInt(FinalVRAMDisplayRenderTrace.EventScreenSwap);
    addInt(FinalVRAMDisplayRenderTrace.EventMainFinalBottom);
    addInt(FinalVRAMDisplayRenderTrace.EventSourceOBJ);
    addInt(FinalVRAMDisplayRenderTrace.EventSourceRenderedFullWholeScene);
    addInt(FinalVRAMDisplayRenderTrace.EventSourceKind);
    addInt(FinalVRAMDisplayRenderTrace.EventProductMask);
    addInt(FinalVRAMDisplayRenderTrace.EventRejectReason);
    addInt(FinalVRAMDisplayRenderTrace.EventFullTex);
    addInt(FinalVRAMDisplayRenderTrace.EventMatchesNativeCapture);
    addInt(FinalVRAMDisplayRenderTrace.ExactEventRouteMatches);
    addInt(FinalVRAMDisplayRenderTrace.ExactEventProductAvailable);
    addInt(FinalVRAMDisplayRenderTrace.ExactEventProductUsable);
    addInt(finalVRAMDisplayEpochValid);
    addInt(finalVRAMDisplayEpochUsed);
    addU64(finalVRAMDisplayEpochSerial);
    addU64(finalVRAMDisplayEpochSource3DSerial);
    addU64(finalVRAMDisplayEpochSource3DSceneHash);
    addInt(finalVRAMDisplayEpochSourceKind);
    addInt(finalVRAMDisplayEpochProductMask);
    addInt(finalVRAMDisplayEpochDirtyRows);
    addInt(finalVRAMDisplayEpochDirtyYStart);
    addInt(finalVRAMDisplayEpochDirtyYEnd);
    addInt(MainVRAMDisplayEpochInvalidationDebug.Reason);
    addInt(MainVRAMDisplayEpochInvalidationDebug.Bank);
    addInt(MainVRAMDisplayEpochInvalidationDebug.Start);
    addInt(MainVRAMDisplayEpochInvalidationDebug.Len);
    addInt(MainVRAMDisplayEpochInvalidationDebug.Complete);

    for (int bank = 0; bank < 4; bank++)
    {
        const auto& native = LastDisplayCapture256Debug[bank];
        const auto& event = HighResDisplayCapture256Event[bank];
        const auto& epoch = MainVRAMDisplayEpoch[bank];

        addInt(native.Valid);
        addInt(native.CaptureCnt);
        addInt(native.DstBlock);
        addInt(native.DstOffset);
        addInt(native.CapSize);
        addInt(native.DstMode);
        addInt(native.SrcA);
        addInt(native.SrcB);
        addInt(native.UsesSrcB);
        addInt(native.SrcBBlock);
        addInt(native.SrcBOffset);
        addInt(native.SrcBTrackedLayer);
        addInt(native.SrcBSameDstBank);
        addInt(native.FinalNativeSourceA);
        addInt(CaptureOutput256Valid[bank]);
        addInt(event.Valid);
        addU64(event.Serial);
        addU64(event.Source3DSerial);
        addU64(event.Source3DSceneHash);
        addU64(event.SourcePresentationHash);
        addInt(event.SourceKind);
        addInt(event.ProductMask);
        addInt(event.RejectReason);
        addInt(event.DstBlock);
        addInt(event.DstOffset);
        addInt(event.ScreenSwap);
        addInt(event.MainEngineFinalBottom);
        addInt(event.SourceLayerEnable);
        addInt(event.SourceVisibleBitmapMask);
        addInt(event.SourceDirect3DVisible);
        addInt(event.SourceOBJVisible);
        addInt(event.SourceWholeScenePath);
        addInt(event.SourceWholeSceneYStart);
        addInt(event.SourceWholeSceneYEnd);
        addInt(event.SourceVisibleBGLayers);
        addInt(event.SourceBGLayerTypes);
        addInt(event.SourceRenderedFullWholeScene);
        addInt(event.SourceDirect3DOnlyBackground);
        addInt(event.SourceTextBGShapeFullEquivalent);
        addInt(event.SourceTextBGFullEquivalent);
        addInt(event.SourceOBJOnlyDirtyOrPartial);
        addInt(HighResDisplayCaptureFullTex[bank]);
        addInt(HighResDisplayCaptureBackgroundTex[bank]);
        addInt(epoch.Valid);
        addU64(epoch.Serial);
        addU64(epoch.Source3DSerial);
        addU64(epoch.Source3DSceneHash);
        addU64(epoch.SourcePresentationHash);
        addInt(epoch.SourceKind);
        addInt(epoch.ProductMask);
        addInt(epoch.ScreenSwap);
        addInt(epoch.MainEngineFinalBottom);
        addInt(epoch.HasDirtyRows);
        addInt(epoch.HasDirtyRows ? static_cast<int>(epoch.DirtyYStart) : -1);
        addInt(epoch.HasDirtyRows ? static_cast<int>(epoch.DirtyYEnd) : -1);
        addInt(MainVRAMDisplayEpochTex[bank]);
    }

    const int vramDisplayWriteBank =
        VRAMDisplayWriteDebug.DisplayBank == 0xFFFFFFFFu
            ? -1
            : static_cast<int>(VRAMDisplayWriteDebug.DisplayBank);
    const int vramDisplayWriteFirstOffset =
        VRAMDisplayWriteDebug.DisplayFirstOffset == 0xFFFFFFFFu
            ? -1
            : static_cast<int>(VRAMDisplayWriteDebug.DisplayFirstOffset);
    const int vramDisplayWriteLastEnd =
        VRAMDisplayWriteDebug.DisplayWriteCount == 0
            ? -1
            : static_cast<int>(VRAMDisplayWriteDebug.DisplayLastEnd);
    const int vramDisplayWriteDirtyYStart =
        VRAMDisplayWriteDebug.DisplayDirtyYStart < VRAMDisplayWriteDebug.DisplayDirtyYEnd
            ? static_cast<int>(VRAMDisplayWriteDebug.DisplayDirtyYStart)
            : -1;
    const int vramDisplayWriteDirtyYEnd =
        VRAMDisplayWriteDebug.DisplayDirtyYStart < VRAMDisplayWriteDebug.DisplayDirtyYEnd
            ? static_cast<int>(VRAMDisplayWriteDebug.DisplayDirtyYEnd)
            : -1;
    addInt(VRAMDisplayWriteDebug.WriteCount);
    addInt(VRAMDisplayWriteDebug.ChangedWriteCount);
    addInt(VRAMDisplayWriteDebug.BankMask);
    addU64(VRAMDisplayWriteDebug.WriteBytes);
    addU64(VRAMDisplayWriteDebug.ChangedWriteBytes);
    addInt(vramDisplayWriteBank);
    addInt(VRAMDisplayWriteDebug.DisplayWriteCount);
    addInt(VRAMDisplayWriteDebug.DisplayChangedWriteCount);
    addU64(VRAMDisplayWriteDebug.DisplayWriteBytes);
    addU64(VRAMDisplayWriteDebug.DisplayChangedWriteBytes);
    addInt(vramDisplayWriteFirstOffset);
    addInt(vramDisplayWriteLastEnd);
    addInt(vramDisplayWriteDirtyYStart);
    addInt(vramDisplayWriteDirtyYEnd);

    addInt(PhysicalFinalUpscale);
    addInt(PhysicalFinalPostprocessApplied);
    addInt(PhysicalFinalPostprocessRejectReason);
    addInt(PhysicalFinalNativeValidRows);
    addInt(PhysicalFinalNativeInputValid[0]);
    addInt(PhysicalFinalNativeInputValid[1]);
    addInt(PhysicalFinalNativeInputPath[0]);
    addInt(PhysicalFinalNativeInputPath[1]);
    addInt(FinalCaptureSourceDebug.SourceAKind);
    addInt(FinalCaptureSourceDebug.SourceANativeSized);
    addInt(FinalCaptureSourceDebug.SourceARangeValid);
    addInt(FinalCaptureSourceDebug.SourceAUsed);
}

bool GLRenderer::ReadWholeScene2DTimingCSV(std::string& header,
                                           std::string& row)
{
    auto* rendA = dynamic_cast<GLRenderer2D*>(Rend2D_A.get());
    auto* rendB = dynamic_cast<GLRenderer2D*>(Rend2D_B.get());
    if (!rendA && !rendB)
    {
        header.clear();
        row.clear();
        return false;
    }

    header.clear();
    row.clear();
    // Arm the capture-content luma probe for the next few frames; it stays off
    // outside logging so normal rendering never pays the readback.
    WholeSceneTimingCSVActiveFrames = 8;
    AppendWholeSceneFrameTimingCSVHeader(header);
    AppendWholeSceneFrameTimingCSVRow(row);

    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
    {
        rend3d->AppendRenderFrameTimingCSVHeader(header, "gl3d");
        rend3d->AppendRenderFrameTimingCSVRow(row);
    }

    if (rendA)
    {
        rendA->AppendWholeSceneTimingCSVHeader(header, "a");
        rendA->AppendWholeSceneTimingCSVRow(row);
    }
    if (rendB)
    {
        rendB->AppendWholeSceneTimingCSVHeader(header, "b");
        rendB->AppendWholeSceneTimingCSVRow(row);
    }

    return true;
}

bool GLRenderer::ReadTextureScalingDebugStats(TextureScalingDebugStats& stats,
                                              std::string* status)
{
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        return rend3d->GetTextureScalingDebugStats(stats, status);
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        return rend3d->GetTextureScalingDebugStats(stats, status);

    stats = {};
    if (status)
        *status = "The selected renderer does not provide 3D texture scaling diagnostics.";
    return false;
}

bool GLRenderer::ResetTextureScalingDebugStats(std::string* status)
{
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        return rend3d->ResetTextureScalingDebugStats(status);
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        return rend3d->ResetTextureScalingDebugStats(status);

    if (status)
        *status = "The selected renderer does not provide 3D texture scaling diagnostics.";
    return false;
}

bool GLRenderer::ReadTextureScalingDebugLastMiss(TextureScalingDebugLastMiss& miss,
                                                 std::string* status)
{
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        return rend3d->GetTextureScalingDebugLastMiss(miss, status);
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        return rend3d->GetTextureScalingDebugLastMiss(miss, status);

    miss = {};
    if (status)
        *status = "The selected renderer does not provide 3D texture scaling last-miss data.";
    return false;
}

bool GLRenderer::SetTextureScalingDebugCaptureEnabled(bool enabled,
                                                      std::string* status)
{
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        return rend3d->SetTextureScalingDebugCaptureEnabled(enabled, status);
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        return rend3d->SetTextureScalingDebugCaptureEnabled(enabled, status);

    if (status)
        *status = "The selected renderer does not provide 3D texture scaling miss capture control.";
    return false;
}

bool GLRenderer::ReadTextureScalingDebugFrameTextures(TextureScalingDebugFrameTextures& frame,
                                                      std::string* status)
{
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        return rend3d->GetTextureScalingDebugFrameTextures(frame, status);
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        return rend3d->GetTextureScalingDebugFrameTextures(frame, status);

    frame = {};
    if (status)
        *status = "The selected renderer does not provide 3D texture frame browsing.";
    return false;
}

bool GLRenderer::SetTextureScalingDebugFrameCaptureEnabled(bool enabled,
                                                           std::string* status)
{
    if (auto* rend3d = dynamic_cast<GLRenderer3D*>(Rend3D.get()))
        return rend3d->SetTextureScalingDebugFrameCaptureEnabled(enabled, status);
    if (auto* rend3d = dynamic_cast<ComputeRenderer3D*>(Rend3D.get()))
        return rend3d->SetTextureScalingDebugFrameCaptureEnabled(enabled, status);

    if (status)
        *status = "The selected renderer does not provide 3D texture frame capture control.";
    return false;
}


bool GLRenderer::NeedsShaderCompile()
{
    return Rend3D->NeedsShaderCompile();
}

void GLRenderer::ShaderCompileStep(int& current, int& count)
{
    return Rend3D->ShaderCompileStep(current, count);
}

}
