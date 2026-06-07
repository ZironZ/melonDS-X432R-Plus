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
#include <chrono>
#include "NDS.h"
#include "GPU_OpenGL.h"

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

namespace
{
u64 ElapsedUS(std::chrono::steady_clock::time_point start)
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
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

    glGenTextures(4, HighResDisplayCaptureBackgroundTex);
    glGenFramebuffers(4, HighResDisplayCaptureBackgroundFB);
    glGenFramebuffers(1, &HighResDisplayCaptureBackgroundReadFB);
    glGenTextures(4, HighResDisplayCaptureFullTex);
    glGenFramebuffers(4, HighResDisplayCaptureFullFB);
    glGenFramebuffers(1, &HighResDisplayCaptureFullReadFB);
    glGenTextures(2, ActiveCaptureBackgroundEpochTex);
    glGenFramebuffers(2, ActiveCaptureBackgroundEpochFB);
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
    glDeleteTextures(4, HighResDisplayCaptureBackgroundTex);
    glDeleteFramebuffers(4, HighResDisplayCaptureBackgroundFB);
    glDeleteFramebuffers(1, &HighResDisplayCaptureBackgroundReadFB);
    glDeleteTextures(4, HighResDisplayCaptureFullTex);
    glDeleteFramebuffers(4, HighResDisplayCaptureFullFB);
    glDeleteFramebuffers(1, &HighResDisplayCaptureFullReadFB);
    glDeleteTextures(2, ActiveCaptureBackgroundEpochTex);
    glDeleteFramebuffers(2, ActiveCaptureBackgroundEpochFB);

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
    memset(ActiveCaptureBackgroundEpoch, 0, sizeof(ActiveCaptureBackgroundEpoch));
    memset(CaptureOutput256Valid, 0, sizeof(CaptureOutput256Valid));
    HighResDisplayCaptureEventSerial = 0;

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
    ResetWholeSceneFrameTiming();

    Rend2D_A->Reset();
    Rend2D_B->Reset();
    Rend3D->Reset();
}

void GLRenderer::ResetWholeSceneFrameTiming()
{
    WholeSceneFrameTiming = {};
    FinalPassInvalidCaptureReseed = false;
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
    memset(ActiveCaptureBackgroundEpoch, 0, sizeof(ActiveCaptureBackgroundEpoch));

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
                                              GLuint subInputTex)
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

        FinalPassConfig.uScaleFactor = ScaleFactor;
        FinalPassConfig.uDispModeA = (DispCntA >> 16) & 0x3;
        FinalPassConfig.uDispModeB = (DispCntB >> 16) & 0x1;
        FinalPassConfig.uBrightModeA = (MasterBrightnessA >> 14) & 0x3;
        FinalPassConfig.uBrightModeB = (MasterBrightnessB >> 14) & 0x3;
        FinalPassConfig.uBrightFactorA = std::min(MasterBrightnessA & 0x1F, 16);
        FinalPassConfig.uBrightFactorB = std::min(MasterBrightnessB & 0x1F, 16);

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
        u32 modeA = (DispCntA >> 16) & 0x3;
        bool invalidTrackedFeedbackReseed = false;
        if ((modeA == 2) && (vramcap != -1))
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
        else if (modeA >= 2)
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
    RenderFinalPassToFramebuffer(ystart, yend,
                                 FPOutputFB[backbuf],
                                 ScreenW, ScreenH, ScaleFactor,
                                 OutputTex2D[0],
                                 OutputTex2D[1]);

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
    RecordHighResDisplayCaptureEvent(capture);

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

    AddWholeScenePhaseTiming(WholeSceneFrameTiming.DoCapture, ElapsedUS(phaseStart));
}


void GLRenderer::AllocCapture(u32 bank, u32 start, u32 len)
{
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

void GLRenderer::SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete)
{
    if (!complete)
        Log(LogLevel::Error, "GPU_OpenGL: !!! READING VRAM AS IT IS BEING CAPTURED TO\n");

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

bool GLRenderer::IsEngineRoutedToFinalBottom(u32 engine, int ystart, int yend) const
{
    bool screenSwap = GPU.ScreenSwap;
    GetFinalPassScreenSwapForRange(ystart, yend, screenSwap);
    return engine == (screenSwap ? 1u : 0u);
}

bool GLRenderer::HasMainVRAMDisplayCaptureFinalRoute() const
{
    const u32 finalDispModeA = (DispCntA >> 16) & 0x3u;
    if (finalDispModeA != 2 || Aux0VRAMCap < 0)
        return false;

    const int captureBank = Aux0VRAMCap >> 2;
    const u32 captureOffset = static_cast<u32>(Aux0VRAMCap & 0x3);
    if (captureBank < 0 || captureBank >= 4)
        return false;

    const auto& event = HighResDisplayCapture256Event[captureBank];
    return IsFullDisplayHighResCaptureEventRecord(event, static_cast<u32>(captureBank)) &&
           event.DstOffset == captureOffset;
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
        event.SourcePresentationHash = sourceRenderer->CapturePresentationHash();
    }

    return event;
}

void GLRenderer::ClassifyHighResDisplayCaptureEvent(const sLastDisplayCaptureDebug& capture,
                                                    GLRenderer2D* sourceRenderer,
                                                    bool fullDisplay,
                                                    bool sourceAOnly,
                                                    sHighResDisplayCaptureEvent& event)
{
    if (!fullDisplay)
        event.RejectReason = HighResCaptureRejectReason::NotFullDisplay;
    else if (!sourceAOnly)
    {
        event.RejectReason = capture.UsesSrcB
            ? HighResCaptureRejectReason::UsesSourceB
            : HighResCaptureRejectReason::BlendedOrFeedback;
    }
    else if (capture.SrcBSameDstBank)
        event.RejectReason = HighResCaptureRejectReason::SameBankReadWrite;

    if (event.RejectReason != HighResCaptureRejectReason::None)
        return;

    if (capture.SrcA)
    {
        event.SourceKind = HighResCaptureSourceKind::CleanOutput3D;
        event.ProductMask = HighResCaptureProductFullEquivalent |
                            HighResCaptureProductBackground3DUnderlay;
        return;
    }

    if (!sourceRenderer)
    {
        event.SourceKind = HighResCaptureSourceKind::Unknown;
        event.RejectReason = HighResCaptureRejectReason::MissingSourceTexture;
        return;
    }

    const u32 visibleBGLayers = sourceRenderer->LayerEnable & 0x0Fu;
    const bool direct3DOnlyBackground =
        event.SourceDirect3DVisible &&
        visibleBGLayers == (1u << 0) &&
        event.SourceVisibleBitmapMask == 0;
    if (direct3DOnlyBackground)
    {
        event.SourceKind = HighResCaptureSourceKind::CleanEngineA2DOutput;
        event.ProductMask = HighResCaptureProductFullEquivalent |
                            HighResCaptureProductBackground3DUnderlay;
    }
    else if (sourceRenderer->WholeSceneTrace.Path == GLRenderer2D::WholeSceneRenderPath::CaptureBackedHandoff)
    {
        event.SourceKind = HighResCaptureSourceKind::RecursiveHandoffOutput;
        event.RejectReason = HighResCaptureRejectReason::RecursiveSource;
    }
    else if (sourceRenderer->WholeSceneTrace.Path == GLRenderer2D::WholeSceneRenderPath::SourceACaptureReplacement ||
             sourceRenderer->WholeSceneTrace.Path == GLRenderer2D::WholeSceneRenderPath::CaptureEpochOverlay)
    {
        event.SourceKind = HighResCaptureSourceKind::RecursiveSourceReplacementOutput;
        event.RejectReason = HighResCaptureRejectReason::RecursiveSource;
    }
    else
    {
        bool direct3DWithTextBGFullEquivalent = event.SourceDirect3DVisible &&
                                                !event.SourceOBJVisible &&
                                                event.SourceVisibleBitmapMask == 0 &&
                                                (visibleBGLayers & (1u << 0)) != 0;
        for (int layer = 0; direct3DWithTextBGFullEquivalent && layer < 4; layer++)
        {
            if (!(visibleBGLayers & (1u << layer)))
                continue;

            const u32 type = sourceRenderer->LayerConfig.uBGConfig[layer].Type;
            if (layer == 0)
                direct3DWithTextBGFullEquivalent = type == 6;
            else
                direct3DWithTextBGFullEquivalent = type <= 1;
        }

        if (direct3DWithTextBGFullEquivalent)
        {
            event.SourceKind = HighResCaptureSourceKind::CleanEngineA2DOutput;
            event.ProductMask = HighResCaptureProductFullEquivalent;
        }
        else
        {
            event.SourceKind = HighResCaptureSourceKind::NativeOnlyOutput2D;
            event.RejectReason = HighResCaptureRejectReason::DirtyOrPartialSource;
        }
    }
}

void GLRenderer::StoreHighResDisplayCaptureEventProducts(const sLastDisplayCaptureDebug& capture,
                                                         sHighResDisplayCaptureEvent& event)
{
    if ((event.ProductMask & HighResCaptureProductBackground3DUnderlay) &&
        !StoreHighResDisplayCaptureBackgroundProduct(capture.DstBlock, OutputTex3D))
    {
        event.ProductMask &= ~HighResCaptureProductBackground3DUnderlay;
    }

    if (event.ProductMask & HighResCaptureProductFullEquivalent)
    {
        const GLuint fullSourceTex = capture.SrcA ? OutputTex3D : OutputTex2D[0];
        if (!StoreHighResDisplayCaptureFullProduct(capture.DstBlock, fullSourceTex))
            event.ProductMask &= ~HighResCaptureProductFullEquivalent;
    }

    if (event.ProductMask == 0 &&
        event.RejectReason == HighResCaptureRejectReason::None)
    {
        event.SourceKind = HighResCaptureSourceKind::Unknown;
        event.RejectReason = HighResCaptureRejectReason::MissingSourceTexture;
    }
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
            InvalidateCaptureBackgroundEpochForBank(capture.DstBlock);
        }
        LastHighResDisplayCaptureEvent = event;
        return;
    }

    if (event.RejectReason != HighResCaptureRejectReason::None ||
        !(event.ProductMask & HighResCaptureProductBackground3DUnderlay))
    {
        InvalidateCaptureBackgroundEpochForBank(capture.DstBlock);
        if (fullDisplay)
            InvalidateCaptureBackgroundEpoch();
    }

    HighResDisplayCapture256Event[capture.DstBlock] = event;
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

    if (capture.CapSize == 0)
    {
        event.RejectReason = HighResCaptureRejectReason::UnsupportedSize;
        PublishHighResDisplayCaptureEvent(capture, event, fullDisplay);
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
        return;
    }

    ClassifyHighResDisplayCaptureEvent(capture, sourceRenderer, fullDisplay, sourceAOnly, event);
    StoreHighResDisplayCaptureEventProducts(capture, event);
    PublishHighResDisplayCaptureEvent(capture, event, fullDisplay);
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
    add("capture256_valid_mask");
    add("capture256_full_source_a_mask");
    add("capture_event_serial");
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
    add("capture_event_source_presentation_hash");
    add("capture_event_final_native_source");
    add("capture_event_hybrid_product_suppressed");
    add("final_disp_mode_a");
    add("final_disp_mode_b");
    add("final_screen_swap");
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
    addInt(DisplayCapture256ValidMask());
    addInt(DisplayCapture256FullSourceAMask());
    addU64(LastHighResDisplayCaptureEvent.Serial);
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

    // Source kind values: 0 disabled/white, 1 A output, 2 B output,
    // 3 A VRAM tracked capture, 4 A VRAM raw aux copy, 5 A display FIFO,
    // 6 A output used to reseed an invalid tracked feedback capture.
    const int finalMainSource =
        FinalPassInvalidCaptureReseed ? 6 :
        (finalDispModeA == 0) ? 0 :
        (finalDispModeA == 1) ? 1 :
        (finalDispModeA == 2 && Aux0VRAMCap != -1) ? 3 :
        (finalDispModeA == 2) ? 4 :
        (finalDispModeA == 3) ? 5 : -1;
    const int finalSubSource = finalDispModeB == 0 ? 0 : 2;
    const int finalTopSource = finalScreenSwap ? finalMainSource : finalSubSource;
    const int finalBottomSource = finalScreenSwap ? finalSubSource : finalMainSource;

    // Final VRAM-display replacement reject reasons:
    // 0 eligible, 1 not main VRAM display, 2 no tracked capture source,
    // 3 invalid tracked bank, 4 no matching full-display event,
    // 5 capture event itself rejected, 6 missing full-equivalent product,
    // 7 unsupported product source kind, 8 mixed/OBJ full-frame capture.
    int finalVRAMDisplayCaptureBank = -1;
    int finalVRAMDisplayCaptureOffset = -1;
    int finalVRAMDisplayCaptureMatch = 0;
    u64 finalVRAMDisplayCaptureSerial = 0;
    u32 finalVRAMDisplayCaptureProductMask = 0;
    u32 finalVRAMDisplayCaptureSourceKind = static_cast<u32>(HighResCaptureSourceKind::None);
    u32 finalVRAMDisplayCaptureRejectReason = static_cast<u32>(HighResCaptureRejectReason::None);
    int finalVRAMDisplayReplacementEligible = 0;
    int finalVRAMDisplayReplacementRejectReason = 0;

    if (finalDispModeA != 2)
    {
        finalVRAMDisplayReplacementRejectReason = 1;
    }
    else if (Aux0VRAMCap == -1)
    {
        finalVRAMDisplayReplacementRejectReason = 2;
    }
    else
    {
        finalVRAMDisplayCaptureBank = Aux0VRAMCap >> 2;
        finalVRAMDisplayCaptureOffset = Aux0VRAMCap & 0x3;
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
                IsFullDisplayHighResCaptureEventRecord(event, finalVRAMDisplayCaptureBank) &&
                event.DstOffset == static_cast<u32>(finalVRAMDisplayCaptureOffset);

            if (!finalVRAMDisplayCaptureMatch)
            {
                finalVRAMDisplayReplacementRejectReason = 4;
            }
            else if (event.RejectReason != HighResCaptureRejectReason::None)
            {
                const bool mixedOrOBJFullFrameCapture =
                    event.RejectReason == HighResCaptureRejectReason::DirtyOrPartialSource &&
                    event.SourceKind == HighResCaptureSourceKind::NativeOnlyOutput2D &&
                    event.SourceDirect3DVisible &&
                    event.SourceVisibleBitmapMask == 0 &&
                    (event.SourceLayerEnable & (1u << 0)) &&
                    (event.SourceOBJVisible || (event.SourceLayerEnable & 0x0Eu));
                finalVRAMDisplayReplacementRejectReason =
                    mixedOrOBJFullFrameCapture ? 8 : 5;
            }
            else if (!(event.ProductMask & HighResCaptureProductFullEquivalent))
            {
                finalVRAMDisplayReplacementRejectReason = 6;
            }
            else if (event.SourceKind != HighResCaptureSourceKind::CleanOutput3D &&
                     event.SourceKind != HighResCaptureSourceKind::CleanEngineA2DOutput)
            {
                finalVRAMDisplayReplacementRejectReason = 7;
            }
            else
            {
                finalVRAMDisplayReplacementEligible = 1;
            }
        }
    }

    addInt(finalDispModeA);
    addInt(finalDispModeB);
    addInt(finalScreenSwap);
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
