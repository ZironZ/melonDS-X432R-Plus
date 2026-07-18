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

#include <algorithm>
#include <assert.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "GPU_OpenGL.h"
#include "GPU2D_OpenGL.h"
#include "GPU.h"
#include "GPU3D.h"
#include "OpenGL_shaders/2DNNEDI3_VerticalCS.h"
#include "OpenGL_shaders/2DNNEDI3_HorizontalCS.h"

extern const char* k2DCuNNy4x32_InCS;
extern const char* k2DCuNNy4x32_Conv1CS;
extern const char* k2DCuNNy4x32_Conv2CS;
extern const char* k2DCuNNy4x32_Conv3CS;
extern const char* k2DCuNNy4x32_Conv4CS;
extern const char* k2DCuNNy4x32_OutShuffleCS;

extern const char* k2DArtCNN_C4F16_Conv0CS;
extern const char* k2DArtCNN_C4F16_Conv1CS;
extern const char* k2DArtCNN_C4F16_Conv2CS;
extern const char* k2DArtCNN_C4F16_Conv3CS;
extern const char* k2DArtCNN_C4F16_Conv4CS;
extern const char* k2DArtCNN_C4F16_Conv5CS;
extern const char* k2DArtCNN_C4F16_Conv6CS;
extern const char* k2DArtCNN_C4F16_DepthToSpaceCS;
extern const char* k2DArtCNN_C4F16DN_Conv0CS;
extern const char* k2DArtCNN_C4F16DN_Conv1CS;
extern const char* k2DArtCNN_C4F16DN_Conv2CS;
extern const char* k2DArtCNN_C4F16DN_Conv3CS;
extern const char* k2DArtCNN_C4F16DN_Conv4CS;
extern const char* k2DArtCNN_C4F16DN_Conv5CS;
extern const char* k2DArtCNN_C4F16DN_Conv6CS;
extern const char* k2DArtCNN_C4F16DN_DepthToSpaceCS;

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

#include "OpenGL_shaders/2DLayerPreVS.h"
#include "OpenGL_shaders/2DLayerPreFS.h"
#include "OpenGL_shaders/2DSpritePreVS.h"
#include "OpenGL_shaders/2DSpritePreFS.h"
#include "OpenGL_shaders/2DSpriteVS.h"
#include "OpenGL_shaders/2DSpriteFS.h"
#include "OpenGL_shaders/2DCompositorVS.h"
#include "OpenGL_shaders/2DCompositorFS.h"
#include "OpenGL_shaders/2DNativePrepassFS.h"
#include "OpenGL_shaders/2DNativeUpscaleFS.h"
#include "OpenGL_shaders/2DNativeBoundaryGuardFS.h"
#include "OpenGL_shaders/2DNativeResolveFS.h"
#include "OpenGL_shaders/2DNative3DResolveFS.h"
#include "OpenGL_shaders/2DOverlayEndpointFS.h"
#include "OpenGL_shaders/2DOverlayBaselineCompositeFS.h"
#include "OpenGL_shaders/2DOverlayCompositeFS.h"
#include "OpenGL_shaders/2DOverlayDebugFS.h"
#include "OpenGL_shaders/2DMasterBrightnessFS.h"
#include "OpenGL_shaders/2DFullscreenPassVS.h"
#include "OpenGL_shaders/2DRGBAToYUVAFS.h"
#include "OpenGL_shaders/2DSpline36FS.h"
#include "OpenGL_shaders/2DArtCNN_YUVAToRGBA2xFS.h"
#include "OpenGL_shaders/2DAlphaReplaceFS.h"
#include "OpenGL_shaders/2DXBRZ_PreprocessFS.h"
#include "OpenGL_shaders/2DXBRZ_FreescaleFS.h"

namespace
{
constexpr u32 kWholeSceneDirtyDispCntLayer = 1u << 0;
constexpr u32 kWholeSceneDirtyDispCntCompositor = 1u << 1;
constexpr u32 kWholeSceneDirtyLayerEnable = 1u << 2;
constexpr u32 kWholeSceneDirtyUnitEnable = 1u << 3;
constexpr u32 kWholeSceneDirtyForcedBlank = 1u << 4;
constexpr u32 kWholeSceneDirtyBGCntLayer = 1u << 5;
constexpr u32 kWholeSceneDirtyBGCntCompositor = 1u << 6;
constexpr u32 kWholeSceneDirtyBlend = 1u << 7;
constexpr u32 kWholeSceneDirtyParentPartial = 1u << 8;

constexpr u32 kWholeSceneMiscUnitEnable = 1u << 0;
constexpr u32 kWholeSceneMiscForcedBlank = 1u << 1;
constexpr u32 kWholeSceneMiscBlendCnt = 1u << 2;
constexpr u32 kWholeSceneMiscEVA = 1u << 3;
constexpr u32 kWholeSceneMiscEVB = 1u << 4;
constexpr u32 kWholeSceneMiscEVY = 1u << 5;
constexpr u32 kWholeSceneMiscParentPartial = 1u << 6;

enum class CaptureRepresentationEffectOwner : u8
{
    None = 0,
    CurrentEngine = 1,
    SourceA = 2,
    FinalDisplay = 3,
    Unknown = 4,
};

enum class CaptureRepresentationEffectCompatibility : u8
{
    IncompatibleOrNoProduct = 0,
    Compatible = 1,
    UnknownNeedsProof = 2,
};

enum class CaptureRepresentationFallbackClass : u8
{
    None = 0,
    NativeCurrent = 1,
    PostprocessFinal = 2,
    Overlay = 3,
    ExactFullCapture = 4,
    MissingProduct = 5,
    NormalHybrid = 6,
};

bool CaptureRepresentationHasContentProof(const WholeSceneRenderTrace& trace)
{
    if (trace.CaptureProductKind == WholeSceneCaptureProductKind::None ||
        trace.CaptureRenderAction == WholeSceneCaptureRenderAction::None ||
        trace.CaptureRenderAction == WholeSceneCaptureRenderAction::RenderNormalHybridFallback)
    {
        return false;
    }

    if (trace.CaptureProofKind != WholeSceneCaptureProofKind::None)
        return true;

    return trace.CaptureProductKind == WholeSceneCaptureProductKind::FullCaptureProduct &&
           trace.SourceAFullProductEventValid;
}

bool CaptureRepresentationHasRouteProof(const WholeSceneRenderTrace& trace)
{
    switch (trace.CaptureProofKind)
    {
    case WholeSceneCaptureProofKind::ExactCaptureEvent:
    case WholeSceneCaptureProofKind::RouteStateIdentity:
    case WholeSceneCaptureProofKind::ActiveBackgroundEpoch:
    case WholeSceneCaptureProofKind::HandoffRouteKey:
    case WholeSceneCaptureProofKind::DirectFinalPresentationMatch:
    case WholeSceneCaptureProofKind::CurrentOverlayEligibility:
    case WholeSceneCaptureProofKind::Source3DSceneIdentity:
        return true;
    case WholeSceneCaptureProofKind::None:
    default:
        return false;
    }
}

CaptureRepresentationEffectOwner CaptureRepresentationEffectOwnerForTrace(
    const WholeSceneRenderTrace& trace)
{
    switch (trace.CaptureRequestKind)
    {
    case WholeSceneCaptureRequestKind::CapturedLayerConsumer:
        return CaptureRepresentationEffectOwner::SourceA;
    case WholeSceneCaptureRequestKind::DirectFinalConsumer:
    case WholeSceneCaptureRequestKind::MainVRAMDisplayConsumer:
        return CaptureRepresentationEffectOwner::FinalDisplay;
    case WholeSceneCaptureRequestKind::LiveOverlayProducer:
    case WholeSceneCaptureRequestKind::HandoffConsumer:
        return CaptureRepresentationEffectOwner::CurrentEngine;
    case WholeSceneCaptureRequestKind::None:
    default:
        break;
    }

    if (trace.Path == WholeSceneRenderPath::SourceACaptureReplacement)
        return CaptureRepresentationEffectOwner::SourceA;
    if (trace.Path == WholeSceneRenderPath::PhysicalFinalPostprocessInput)
        return CaptureRepresentationEffectOwner::FinalDisplay;
    if (trace.Path == WholeSceneRenderPath::CaptureBackedHandoff ||
        trace.Path == WholeSceneRenderPath::CaptureEpochOverlay)
        return CaptureRepresentationEffectOwner::CurrentEngine;

    return CaptureRepresentationEffectOwner::None;
}

CaptureRepresentationFallbackClass CaptureRepresentationFallbackForTrace(
    const WholeSceneRenderTrace& trace)
{
    if (trace.Path == WholeSceneRenderPath::Current)
        return CaptureRepresentationFallbackClass::NativeCurrent;
    if (trace.Path == WholeSceneRenderPath::PhysicalFinalPostprocessInput ||
        trace.SourceAProductChoice == SourceAProductChoiceReason::FallbackFinalImage)
        return CaptureRepresentationFallbackClass::PostprocessFinal;
    if (trace.Path == WholeSceneRenderPath::OverlayOperatorUpscale)
        return CaptureRepresentationFallbackClass::Overlay;
    if (trace.CaptureProductKind == WholeSceneCaptureProductKind::FullCaptureProduct)
        return CaptureRepresentationFallbackClass::ExactFullCapture;
    if (trace.CaptureRenderAction == WholeSceneCaptureRenderAction::RenderNormalHybridFallback ||
        trace.SourceAProductChoice == SourceAProductChoiceReason::FallbackNormalHybrid)
        return CaptureRepresentationFallbackClass::NormalHybrid;
    if (trace.CaptureProductKind == WholeSceneCaptureProductKind::None &&
        (trace.Path == WholeSceneRenderPath::SourceACaptureReplacement ||
         trace.Path == WholeSceneRenderPath::CaptureBackedHandoff ||
         trace.Path == WholeSceneRenderPath::CaptureEpochOverlay))
        return CaptureRepresentationFallbackClass::MissingProduct;

    return CaptureRepresentationFallbackClass::None;
}

WholeSceneCaptureProductPresentationClass CaptureRepresentationProductClassForTrace(
    const WholeSceneRenderTrace& trace)
{
    const auto chosenProductClass =
        static_cast<WholeSceneCaptureProductPresentationClass>(
            trace.SourceAChosenProductPresentationClass);
    if (chosenProductClass != WholeSceneCaptureProductPresentationClass::None)
        return chosenProductClass;

    return CaptureProductPresentationClassForProduct(trace.CaptureProductKind,
                                                     trace.CaptureRenderAction);
}

u32 HashPresentationValue(u32 hash, u32 value)
{
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

u32 PackedMasterBrightnessTraceState(u16 masterBrightness)
{
    const u32 mode = (masterBrightness >> 14) & 0x3u;
    const u32 factor = std::min<u32>(masterBrightness & 0x1Fu, 16u);
    return (mode << 8) | factor;
}

bool IsMasterBrightnessActive(u16 masterBrightness)
{
    const u32 mode = (masterBrightness >> 14) & 0x3u;
    const u32 factor = std::min<u32>(masterBrightness & 0x1Fu, 16u);
    return (mode == 1 || mode == 2) && factor > 0;
}

const char* kArtCNNModelLabels[RendererSettings::GLArtCNNModelCount] = {
    "C4F16DN",
    "C4F16",
};

const char* kArtCNNConvShaderSources[RendererSettings::GLArtCNNModelCount][7] = {
    {
        ::k2DArtCNN_C4F16DN_Conv0CS,
        ::k2DArtCNN_C4F16DN_Conv1CS,
        ::k2DArtCNN_C4F16DN_Conv2CS,
        ::k2DArtCNN_C4F16DN_Conv3CS,
        ::k2DArtCNN_C4F16DN_Conv4CS,
        ::k2DArtCNN_C4F16DN_Conv5CS,
        ::k2DArtCNN_C4F16DN_Conv6CS,
    },
    {
        ::k2DArtCNN_C4F16_Conv0CS,
        ::k2DArtCNN_C4F16_Conv1CS,
        ::k2DArtCNN_C4F16_Conv2CS,
        ::k2DArtCNN_C4F16_Conv3CS,
        ::k2DArtCNN_C4F16_Conv4CS,
        ::k2DArtCNN_C4F16_Conv5CS,
        ::k2DArtCNN_C4F16_Conv6CS,
    },
};

const char* kArtCNNDepthToSpaceSources[RendererSettings::GLArtCNNModelCount] = {
    ::k2DArtCNN_C4F16DN_DepthToSpaceCS,
    ::k2DArtCNN_C4F16_DepthToSpaceCS,
};

const char* kCuNNyModelLabels[RendererSettings::GLCuNNyModelCount] = {
    "4x32",
};

struct CuNNyModelInfo
{
    const char* InShader;
    const char* ConvShaders[RendererSettings::GLCuNNyMaxConvPasses];
    const char* OutShader;
    int ConvPasses;
    int WorkScaleX;
    int WorkScaleY;
    int FinalWorkScaleX;
    int FinalWorkScaleY;
    bool RGB;
};

const CuNNyModelInfo kCuNNyModels[RendererSettings::GLCuNNyModelCount] = {
    {
        ::k2DCuNNy4x32_InCS,
        {
            ::k2DCuNNy4x32_Conv1CS,
            ::k2DCuNNy4x32_Conv2CS,
            ::k2DCuNNy4x32_Conv3CS,
            ::k2DCuNNy4x32_Conv4CS,
        },
        ::k2DCuNNy4x32_OutShuffleCS,
        4,
        4, 2,
        4, 2,
        true,
    },
};

void SetUniform1iIfPresent(GLuint shader, const char* name, GLint value)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform1i(loc, value);
}

void SetUniform2iIfPresent(GLuint shader, const char* name, GLint x, GLint y)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform2i(loc, x, y);
}

void SetUniform1fIfPresent(GLuint shader, const char* name, GLfloat value)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform1f(loc, value);
}

void SetUniform2fIfPresent(GLuint shader, const char* name, GLfloat x, GLfloat y)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform2f(loc, x, y);
}

void SetUniform4fIfPresent(GLuint shader, const char* name, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform4f(loc, x, y, z, w);
}

constexpr const char* kCuNNyWorkSamplerUniforms[] = {
    "in_raw",
    "conv1_raw",
    "conv2_raw",
    "conv3_raw",
    "conv4_raw",
    "conv5_raw",
    "conv6_raw",
    "conv7_raw",
    "conv8_raw",
};

constexpr const char* kCuNNyMulUniforms[] = {
    "LUMA_mul",
    "MAIN_mul",
    "in_mul",
    "conv1_mul",
    "conv2_mul",
    "conv3_mul",
    "conv4_mul",
    "conv5_mul",
    "conv6_mul",
    "conv7_mul",
    "conv8_mul",
};

constexpr const char* kCuNNyWorkPointUniforms[] = {
    "in_pt",
    "conv1_pt",
    "conv2_pt",
    "conv3_pt",
    "conv4_pt",
    "conv5_pt",
    "conv6_pt",
    "conv7_pt",
    "conv8_pt",
};

void SetCuNNyProgramDefaults(GLuint shader)
{
    glUseProgram(shader);
    SetUniform1iIfPresent(shader, "LUMA_raw", 1);
    SetUniform1iIfPresent(shader, "MAIN_raw", 1);
    for (const char* name : kCuNNyWorkSamplerUniforms)
        SetUniform1iIfPresent(shader, name, 0);
    for (const char* name : kCuNNyMulUniforms)
        SetUniform4fIfPresent(shader, name, 1.0f, 1.0f, 1.0f, 1.0f);
}

constexpr const char* kArtCNNSamplerUniforms[] = {
    "LUMA_raw",
    "conv2d_raw",
    "conv2d_1_raw",
    "conv2d_2_raw",
    "conv2d_3_raw",
    "conv2d_4_raw",
    "conv2d_5_raw",
    "conv2d_6_raw",
};
constexpr int kArtCNNSamplerUniformCount = sizeof(kArtCNNSamplerUniforms) / sizeof(kArtCNNSamplerUniforms[0]);

constexpr const char* kArtCNNMulUniforms[] = {
    "LUMA_mul",
    "conv2d_mul",
    "conv2d_1_mul",
    "conv2d_2_mul",
    "conv2d_3_mul",
    "conv2d_4_mul",
    "conv2d_5_mul",
    "conv2d_6_mul",
};

void SetArtCNNComputeProgramDefaults(GLuint shader)
{
    glUseProgram(shader);
    for (int i = 0; i < (int)kArtCNNSamplerUniformCount; i++)
        SetUniform1iIfPresent(shader, kArtCNNSamplerUniforms[i], i);
    for (const char* name : kArtCNNMulUniforms)
        SetUniform1fIfPresent(shader, name, 1.0f);
}

void SetNNEDI3ComputeProgramDefaults(GLuint shader)
{
    glUseProgram(shader);
    SetUniform1iIfPresent(shader, "Source", 0);
}

void SetArtCNNSizeUniform(GLuint shader, const char* sizeName, const char* pointName, GLfloat width, GLfloat height)
{
    SetUniform2fIfPresent(shader, sizeName, width, height);
    SetUniform2fIfPresent(shader, pointName, 1.0f / width, 1.0f / height);
}

void SetArtCNNComputeSizeUniforms(GLuint shader, int nativeWidth, int nativeHeight)
{
    const GLfloat width = (GLfloat)nativeWidth;
    const GLfloat height = (GLfloat)nativeHeight;
    const GLfloat width2x = width * 2.0f;
    const GLfloat height2x = height * 2.0f;

    SetArtCNNSizeUniform(shader, "LUMA_size", "LUMA_pt", width, height);
    SetArtCNNSizeUniform(shader, "conv2d_size", "conv2d_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_1_size", "conv2d_1_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_2_size", "conv2d_2_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_3_size", "conv2d_3_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_4_size", "conv2d_4_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_5_size", "conv2d_5_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_6_size", "conv2d_6_pt", width, height);
}

void BindTextureUnit(int unit, GLuint texture)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

std::string FormatHex(u32 value, int digits)
{
    char text[16];
    snprintf(text, sizeof(text), "0x%0*X", digits, value);
    return text;
}

u64 ElapsedUS(std::chrono::steady_clock::time_point start)
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
}

constexpr u32 kWholeSceneHybridFragmentationGuardThreshold = 8;
constexpr u32 kWholeSceneStreamingBitmapFragmentationGuardThreshold =
    kWholeSceneHybridFragmentationGuardThreshold;
constexpr u32 kWholeSceneCurrentFragmentationGuardThreshold = 64;
constexpr u32 kWholeSceneHybridFragmentationGuardHoldFrames = 12;
constexpr u32 kWholeSceneCurrentFragmentationGuardHoldFrames = 12;

const char* DisplayModeName(bool subEngine, u32 mode)
{
    if (subEngine)
    {
        switch (mode & 0x1)
        {
        case 0: return "off";
        case 1: return "BG/OBJ";
        }
    }

    switch (mode & 0x3)
    {
    case 0: return "off";
    case 1: return "BG/OBJ";
    case 2: return "VRAM display";
    case 3: return "display FIFO";
    default: return "unknown";
    }
}

const char* BlendEffectName(u32 effect)
{
    switch (effect & 0x3)
    {
    case 0: return "none";
    case 1: return "alpha";
    case 2: return "brightness up";
    case 3: return "brightness down";
    default: return "unknown";
    }
}

std::string LayerEnableSummary(u32 bits)
{
    std::string text;
    const char* names[5] = {"BG0", "BG1", "BG2", "BG3", "OBJ"};
    for (int i = 0; i < 5; i++)
    {
        if (i)
            text += " ";
        text += names[i];
        text += "=";
        text += (bits & (1u << i)) ? "1" : "0";
    }
    return text;
}

const char* BGLayerTypeName(u32 type)
{
    switch (type)
    {
    case 0: return "text 16-color";
    case 1: return "text 256-color";
    case 2: return "affine";
    case 3: return "affine 256-color";
    case 4: return "bitmap";
    case 5: return "direct-color bitmap";
    case 6: return "Direct3D";
    case 7: return "capture-backed 128-wide bitmap";
    case 8: return "capture-backed 256-wide bitmap";
    default: return "unknown";
    }
}
}



GLRenderer2D::GLRenderer2D(melonDS::GPU2D& gpu2D, GLRenderer& parent)
    : Renderer2D(gpu2D), Parent(parent)
{
    ScaleFactor = 0;
    WholeSceneScaleRequested = false;
    WholeSceneScaleSourceBoundaryGuard = false;
    WholeSceneScaleMode = RendererSettings::WholeScene2DScaleMode::LegacyNativeUpscale;
    WholeSceneScaleAlgorithm = RendererSettings::GLScaleAlgorithm::Spline36;
    WholeSceneScaleFragmentationFallback = RendererSettings::WholeScene2DFragmentationFallback::Off;
    WholeSceneScaleExactFinalFallback = false;
    WholeSceneScaleForegroundOverlay = false;
    WholeSceneScaleCaptureBacked = false;
    WholeSceneScaleDebugTint = false;
    WholeSceneScaleNoWrapFilterTaps = true;
    WholeSceneScaleFinalUpscaleRender3DNative = false;
    WholeSceneScaleFinalUpscale3DFilter = RendererSettings::FinalUpscale3DDownsampleFilter::Area;
    WholeSceneScaleFinalUpscale3DCoverageAware = false;
    WholeSceneScaleFinalUpscale3DRepresentativeSemantics = true;
    WholeSceneScaleFinalUpscale3DSplitSemantics = false;
    WholeSceneScaleFinalUpscale3DSharpenSplitCoverage = false;
    WholeSceneScaleOverlayLegacyUnderlay = false;
    WholeSceneScaleHybridWindowEdgeAssist = false;
    WholeSceneScaleHybridTarget2AlphaBlendAssist = false;
    WholeSceneScaleHybridNativeEffectGuard = true;
    WholeSceneScaleHybridForeground2DBase = false;
    WholeSceneScaleHybridCleanLegacyCandidate = false;
    CuNNyShaderOwner = this;
    ArtCNNShaderOwner = this;
    NNEDI3ComputeShaderOwner = this;
    WholeSceneDebugViewsActive.store(false, std::memory_order_relaxed);
    WholeSceneScaleState = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneTrace = {};
    WholeSceneDebugPoison = {};
    WholeSceneUpdateTiming = {};
    WholeSceneCurrentUpdateDebugTrace = {};
    WholeScenePreviousUpdateDebugTrace = {};
    WholeSceneCurrentFramePartialComposites = 0;
    WholeScenePreviousFramePartialComposites = 0;
    WholeSceneHybridFragmentationGuardFrames = 0;
    WholeSceneCurrentFragmentationGuardFrames = 0;
    WholeSceneCaptureBackedHandoffGuardFrames = 0;
    WholeSceneHybridFragmentationGuardTripped = false;
    WholeSceneCurrentFragmentationGuardTripped = false;
    WholeSceneFullFrameFinalizerUnsafeFrame = false;
    WholeSceneNativeChunkAccumulationPasses = 0;
    WholeSceneFullFrameFinalizerPasses = 0;
    WholeSceneNativeProductValidRows = 0;
    WholeSceneNativeProductsFrameComplete = false;
    memset(WholeSceneNativeProductRowValid, 0, sizeof(WholeSceneNativeProductRowValid));
    WholeSceneNativeProductEpochValid = true;
    WholeSceneNativeProductEpochInvalidReason = 0;
    WholeSceneNativeProductEligibilityInitialized = false;
    WholeSceneNativeProductFrameEligibility = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneNativeProductLastEligibility = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneNativeProductPathInitialized = false;
    WholeSceneNativeProductFramePath = WholeSceneRenderPath::None;
    WholeSceneNativeProductLastPath = WholeSceneRenderPath::None;
    WholeSceneNativeProductFinalizerPathSeen = false;
    WholeSceneOverlayEndpointsValid = false;
    WholeSceneOverlayEndpointSourceTex = 0;
    WholeSceneSourceABlitFB = 0;
    for (int i = 0; i < kCaptureBackedHandoffRouteSlots; i++)
    {
        CaptureBackedRouteGL[i] = {};
        CaptureBackedRoute[i] = {};
    }
    CaptureBackedHandoff.CurrentKey = {};
    CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::None;
    CaptureBackedHandoff.FrameSerial = 0;
    CaptureBackedHandoff.BackgroundUpdated = false;
    CaptureBackedHandoff.CurrentSlot = 0;
    DeferredLayerPrerenderDirty = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        DeferredLayerPrerenderFirstRow[layer] = -1;
        DeferredLayerPrerenderLastRow[layer] = -1;
    }
}

#define glDefaultTexParams(target) \
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); \
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); \
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST); \
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

bool GLRenderer2D::InitShaders()
{
    GLint uniloc;

    // compile shaders

    if (!OpenGL::CompileVertexFragmentProgram(LayerPreShader,
                                              k2DLayerPreVS, k2DLayerPreFS,
                                              "2DLayerPreShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}, {"oMeta", 1}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(SpritePreShader,
                                              k2DSpritePreVS, k2DSpritePreFS,
                                              "2DSpritePreShader",
                                              {{"vPosition", 0}, {"vSpriteIndex", 1}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(SpriteShader,
                                              k2DSpriteVS, k2DSpriteFS,
                                              "2DSpriteShader",
                                              {{"vPosition", 0}, {"vTexcoord", 1}, {"vSpriteIndex", 2}},
                                              {{"oColor", 0}, {"oFlags", 1}, {"oCoverage", 2}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(CompositorShader,
                                              k2DCompositorVS, k2DCompositorFS,
                                              "2DCompositorShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(NativePrepassShader,
                                              k2DCompositorVS, k2DNativePrepassFS,
                                              "2DNativePrepassShader",
                                              {{"vPosition", 0}},
                                              {{"oFinalColor", 0}, {"oTopColor", 1},
                                               {"oSecondColor", 2}, {"oMeta", 3}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(NativeUpscaleShader,
                                              k2DCompositorVS, k2DNativeUpscaleFS,
                                              "2DNativeUpscaleShader",
                                              {{"vPosition", 0}},
                                              {{"oTopColor", 0}, {"oSecondColor", 1}, {"oMeta", 2}, {"oCoverage", 3}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(NativeBoundaryGuardShader,
                                              k2DCompositorVS, k2DNativeBoundaryGuardFS,
                                              "2DNativeBoundaryGuardShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(NativeResolveShader,
                                              k2DCompositorVS, k2DNativeResolveFS,
                                              "2DNativeResolveShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(Native3DResolveShader,
                                              k2DCompositorVS, k2DNative3DResolveFS,
                                              "2DNative3DResolveShader",
                                              {{"vPosition", 0}},
                                              {{"oVisualResolve", 0}, {"oSemantics", 1}, {"oCompositorInput", 2}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(OverlayEndpointShader,
                                              k2DFullscreenPassVS, k2DOverlayEndpointFS,
                                              "2DOverlayEndpointShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(OverlayCompositeShader,
                                              k2DFullscreenPassVS, k2DOverlayBaselineCompositeFS,
                                              "2DOverlayCompositeShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(OverlayHybridCompositeShader,
                                              k2DFullscreenPassVS, k2DOverlayCompositeFS,
                                              "2DOverlayHybridCompositeShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(OverlayDebugShader,
                                              k2DFullscreenPassVS, k2DOverlayDebugFS,
                                              "2DOverlayDebugShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(MasterBrightnessShader,
                                              k2DFullscreenPassVS, k2DMasterBrightnessFS,
                                              "2DMasterBrightnessShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(RGBAToYUVAShader,
                                              k2DFullscreenPassVS, k2DRGBAToYUVAFS,
                                              "2DRGBAToYUVAShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(Spline36Shader,
                                              k2DFullscreenPassVS, k2DSpline36FS,
                                              "2DSpline36Shader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(ArtCNNYUVAToRGBA2xShader,
                                              k2DFullscreenPassVS, k2DArtCNN_YUVAToRGBA2xFS,
                                              "2DArtCNNYUVAToRGBA2xShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(AlphaReplaceShader,
                                              k2DFullscreenPassVS, k2DAlphaReplaceFS,
                                              "2DAlphaReplaceShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(XBRZPreprocessShader,
                                              k2DFullscreenPassVS, k2DXBRZ_PreprocessFS,
                                              "2DXBRZPreprocessShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(XBRZFreescaleShader,
                                              k2DFullscreenPassVS, k2DXBRZ_FreescaleFS,
                                              "2DXBRZFreescaleShader",
                                              {{"vPosition", 0}},
                                              {{"oColor", 0}}))
        return false;

    // set up uniforms

    glUseProgram(LayerPreShader);

    uniloc = glGetUniformLocation(LayerPreShader, "VRAMTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(LayerPreShader, "PalTex");
    glUniform1i(uniloc, 1);

    uniloc = glGetUniformBlockIndex(LayerPreShader, "ubBGConfig");
    glUniformBlockBinding(LayerPreShader, uniloc, 20);

    LayerPreCurBGULoc = glGetUniformLocation(LayerPreShader, "uCurBG");


    glUseProgram(SpritePreShader);

    uniloc = glGetUniformLocation(SpritePreShader, "VRAMTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(SpritePreShader, "PalTex");
    glUniform1i(uniloc, 1);

    uniloc = glGetUniformBlockIndex(SpritePreShader, "ubSpriteConfig");
    glUniformBlockBinding(SpritePreShader, uniloc, 21);


    glUseProgram(SpriteShader);

    uniloc = glGetUniformLocation(SpriteShader, "SpriteTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(SpriteShader, "Capture128Tex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(SpriteShader, "Capture256Tex");
    glUniform1i(uniloc, 2);

    uniloc = glGetUniformBlockIndex(SpriteShader, "ubSpriteConfig");
    glUniformBlockBinding(SpriteShader, uniloc, 21);
    uniloc = glGetUniformBlockIndex(SpriteShader, "ubSpriteScanlineConfig");
    glUniformBlockBinding(SpriteShader, uniloc, 24);

    SpriteRenderTransULoc = glGetUniformLocation(SpriteShader, "uRenderTransparent");
    SpriteFilterModeULoc = glGetUniformLocation(SpriteShader, "uSpriteFilterMode");


    glUseProgram(CompositorShader);

    uniloc = glGetUniformLocation(CompositorShader, "BGLayerTex[0]");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerTex[1]");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerTex[2]");
    glUniform1i(uniloc, 2);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerTex[3]");
    glUniform1i(uniloc, 3);
    uniloc = glGetUniformLocation(CompositorShader, "OBJLayerTex");
    glUniform1i(uniloc, 4);
    uniloc = glGetUniformLocation(CompositorShader, "Capture128Tex");
    glUniform1i(uniloc, 5);
    uniloc = glGetUniformLocation(CompositorShader, "Capture256Tex");
    glUniform1i(uniloc, 6);
    uniloc = glGetUniformLocation(CompositorShader, "MosaicTex");
    glUniform1i(uniloc, 7);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerMetaTex[0]");
    glUniform1i(uniloc, 8);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerMetaTex[1]");
    glUniform1i(uniloc, 9);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerMetaTex[2]");
    glUniform1i(uniloc, 10);
    uniloc = glGetUniformLocation(CompositorShader, "BGLayerMetaTex[3]");
    glUniform1i(uniloc, 11);
    uniloc = glGetUniformLocation(CompositorShader, "Direct3DCoverageTex");
    glUniform1i(uniloc, 12);

    uniloc = glGetUniformBlockIndex(CompositorShader, "ubBGConfig");
    glUniformBlockBinding(CompositorShader, uniloc, 20);
    uniloc = glGetUniformBlockIndex(CompositorShader, "ubScanlineConfig");
    glUniformBlockBinding(CompositorShader, uniloc, 22);
    uniloc = glGetUniformBlockIndex(CompositorShader, "ubCompositorConfig");
    glUniformBlockBinding(CompositorShader, uniloc, 23);

    CompositorScaleULoc = glGetUniformLocation(CompositorShader, "uScaleFactor");
    CompositorOBJNativeResolutionULoc = glGetUniformLocation(CompositorShader, "uOBJNativeResolution");
    CompositorLayerFilterModeULoc = glGetUniformLocation(CompositorShader, "uLayerFilterMode");
    CompositorLayerFilterNoWrapULoc = glGetUniformLocation(CompositorShader, "uLayerFilterNoWrap");
    CompositorDebugTintULoc = glGetUniformLocation(CompositorShader, "uDebugTintBySource");
    CompositorSplit3DSemanticsULoc = glGetUniformLocation(CompositorShader, "uSplit3DSemantics");
    CompositorSharpenSplit3DCoverageULoc = glGetUniformLocation(CompositorShader, "uSharpenSplit3DCoverage");

    glUseProgram(NativePrepassShader);

    uniloc = glGetUniformLocation(NativePrepassShader, "BGLayerTex[0]");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(NativePrepassShader, "BGLayerTex[1]");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(NativePrepassShader, "BGLayerTex[2]");
    glUniform1i(uniloc, 2);
    uniloc = glGetUniformLocation(NativePrepassShader, "BGLayerTex[3]");
    glUniform1i(uniloc, 3);
    uniloc = glGetUniformLocation(NativePrepassShader, "OBJLayerTex");
    glUniform1i(uniloc, 4);
    uniloc = glGetUniformLocation(NativePrepassShader, "Capture128Tex");
    glUniform1i(uniloc, 5);
    uniloc = glGetUniformLocation(NativePrepassShader, "Capture256Tex");
    glUniform1i(uniloc, 6);
    uniloc = glGetUniformLocation(NativePrepassShader, "MosaicTex");
    glUniform1i(uniloc, 7);

    uniloc = glGetUniformBlockIndex(NativePrepassShader, "ubBGConfig");
    glUniformBlockBinding(NativePrepassShader, uniloc, 20);
    uniloc = glGetUniformBlockIndex(NativePrepassShader, "ubScanlineConfig");
    glUniformBlockBinding(NativePrepassShader, uniloc, 22);
    uniloc = glGetUniformBlockIndex(NativePrepassShader, "ubCompositorConfig");
    glUniformBlockBinding(NativePrepassShader, uniloc, 23);

    NativePrepassScaleULoc = glGetUniformLocation(NativePrepassShader, "uScaleFactor");
    NativePrepassDebugLayerULoc = glGetUniformLocation(NativePrepassShader, "uDebugLayer");
    glUniform1i(NativePrepassDebugLayerULoc, -1);

    glUseProgram(NativeUpscaleShader);

    uniloc = glGetUniformLocation(NativeUpscaleShader, "NativeTopColorTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(NativeUpscaleShader, "NativeSecondColorTex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(NativeUpscaleShader, "NativeMetaTex");
    glUniform1i(uniloc, 2);

    NativeUpscaleScaleULoc = glGetUniformLocation(NativeUpscaleShader, "uScaleFactor");
    NativeUpscaleLegacyFilterULoc = glGetUniformLocation(NativeUpscaleShader, "uLegacyFilterBehavior");

    glUseProgram(NativeBoundaryGuardShader);

    uniloc = glGetUniformLocation(NativeBoundaryGuardShader, "ScaledColorTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(NativeBoundaryGuardShader, "NativeColorTex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(NativeBoundaryGuardShader, "UpscaledCoverageTex");
    glUniform1i(uniloc, 2);

    glUseProgram(NativeResolveShader);

    uniloc = glGetUniformLocation(NativeResolveShader, "UpscaledTopColorTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(NativeResolveShader, "UpscaledSecondColorTex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(NativeResolveShader, "UpscaledMetaTex");
    glUniform1i(uniloc, 2);
    uniloc = glGetUniformLocation(NativeResolveShader, "UpscaledCoverageTex");
    glUniform1i(uniloc, 3);
    uniloc = glGetUniformLocation(NativeResolveShader, "UpscaledExactFinalTex");
    glUniform1i(uniloc, 4);
    uniloc = glGetUniformLocation(NativeResolveShader, "Direct3DTexture");
    glUniform1i(uniloc, 5);

    uniloc = glGetUniformBlockIndex(NativeResolveShader, "ubCompositorConfig");
    glUniformBlockBinding(NativeResolveShader, uniloc, 23);

    uniloc = glGetUniformBlockIndex(NativeResolveShader, "ubScanlineConfig");
    glUniformBlockBinding(NativeResolveShader, uniloc, 22);

    NativeResolveScaleULoc = glGetUniformLocation(NativeResolveShader, "uScaleFactor");
    NativeResolveUseExactFinalFallbackULoc = glGetUniformLocation(NativeResolveShader, "uUseExactFinalFallback");
    NativeResolveUseForegroundOverlayULoc = glGetUniformLocation(NativeResolveShader, "uUseForegroundOverlay");
    NativeResolveDebugTintULoc = glGetUniformLocation(NativeResolveShader, "uDebugTintBySource");
    NativeResolveNativeExactOutputULoc = glGetUniformLocation(NativeResolveShader, "uNativeExactOutput");

    glUseProgram(Native3DResolveShader);

    uniloc = glGetUniformLocation(Native3DResolveShader, "Source3DTex");
    glUniform1i(uniloc, 0);

    Native3DResolveFilterModeULoc = glGetUniformLocation(Native3DResolveShader, "uFilterMode");
    Native3DResolveCoverageAwareULoc = glGetUniformLocation(Native3DResolveShader, "uCoverageAware");
    Native3DResolveRepresentativeSemanticsULoc = glGetUniformLocation(Native3DResolveShader, "uRepresentativeSemantics");
    Native3DResolveSplitSemanticsULoc = glGetUniformLocation(Native3DResolveShader, "uSplitSemantics");

    glUseProgram(OverlayEndpointShader);

    uniloc = glGetUniformLocation(OverlayEndpointShader, "Source3DTex");
    glUniform1i(uniloc, 0);
    OverlayEndpointWhiteULoc = glGetUniformLocation(OverlayEndpointShader, "uWhiteEndpoint");

    glUseProgram(OverlayCompositeShader);

    uniloc = glGetUniformLocation(OverlayCompositeShader, "OverlayBlackTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(OverlayCompositeShader, "OverlayWhiteTex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(OverlayCompositeShader, "Direct3DTexture");
    glUniform1i(uniloc, 2);

    uniloc = glGetUniformBlockIndex(OverlayCompositeShader, "ubScanlineConfig");
    glUniformBlockBinding(OverlayCompositeShader, uniloc, 22);

    OverlayCompositeScaleULoc = glGetUniformLocation(OverlayCompositeShader, "uScaleFactor");
    OverlayCompositeDebugTintULoc = glGetUniformLocation(OverlayCompositeShader, "uDebugTintBySource");
    OverlayCompositeLegacyUnderlayULoc = glGetUniformLocation(OverlayCompositeShader, "uLegacyUnderlayEndpoint");
    OverlayCompositeCoverageAwareULoc = glGetUniformLocation(OverlayCompositeShader, "uCoverageAwareUnderlay");
    OverlayCompositeDirect3DPresentationSpaceULoc = glGetUniformLocation(OverlayCompositeShader, "uDirect3DPresentationSpace");

    glUseProgram(OverlayHybridCompositeShader);

    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "OverlayBlackTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "OverlayWhiteTex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "Direct3DTexture");
    glUniform1i(uniloc, 2);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "NativeMetaTex");
    glUniform1i(uniloc, 3);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "NativeTopColorTex");
    glUniform1i(uniloc, 4);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "HybridForegroundTex");
    glUniform1i(uniloc, 5);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "HybridNativeFallbackTex");
    glUniform1i(uniloc, 6);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "NativeRole3DTex");
    glUniform1i(uniloc, 7);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "Hybrid2DBaseTex");
    glUniform1i(uniloc, 8);
    uniloc = glGetUniformLocation(OverlayHybridCompositeShader, "HybridLegacyCandidateTex");
    glUniform1i(uniloc, 9);

    uniloc = glGetUniformBlockIndex(OverlayHybridCompositeShader, "ubScanlineConfig");
    glUniformBlockBinding(OverlayHybridCompositeShader, uniloc, 22);
    uniloc = glGetUniformBlockIndex(OverlayHybridCompositeShader, "ubCompositorConfig");
    glUniformBlockBinding(OverlayHybridCompositeShader, uniloc, 23);

    OverlayHybridCompositeScaleULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uScaleFactor");
    OverlayHybridCompositeDebugTintULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uDebugTintBySource");
    OverlayHybridCompositeLegacyUnderlayULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uLegacyUnderlayEndpoint");
    OverlayHybridCompositeCoverageAwareULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uCoverageAwareUnderlay");
    OverlayHybridCompositeDirect3DPresentationSpaceULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uDirect3DPresentationSpace");
    OverlayHybridCompositeConservativeHybridULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uConservativeHybrid");
    OverlayHybridCompositeWindowEdgeAssistULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridWindowEdgeAssist");
    OverlayHybridCompositeTarget2AlphaBlendAssistULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridTarget2AlphaBlendAssist");
    OverlayHybridCompositeNativeEffectGuardULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridNativeEffectGuard");
    OverlayHybridCompositeForeground2DBaseULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridForeground2DBase");
    OverlayHybridCompositeLegacyCandidateULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridLegacyCandidate");
    OverlayHybridCompositeForceOverlayAssistULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridForceOverlayAssist");
    OverlayHybridCompositeDebugModeULoc = glGetUniformLocation(OverlayHybridCompositeShader, "uHybridDebugMode");

    glUseProgram(OverlayDebugShader);

    uniloc = glGetUniformLocation(OverlayDebugShader, "OverlayBlackTex");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(OverlayDebugShader, "OverlayWhiteTex");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(OverlayDebugShader, "Direct3DTexture");
    glUniform1i(uniloc, 2);
    uniloc = glGetUniformLocation(OverlayDebugShader, "NativeFinalTex");
    glUniform1i(uniloc, 3);

    uniloc = glGetUniformBlockIndex(OverlayDebugShader, "ubScanlineConfig");
    glUniformBlockBinding(OverlayDebugShader, uniloc, 22);

    OverlayDebugModeULoc = glGetUniformLocation(OverlayDebugShader, "uDebugMode");
    OverlayDebugLegacyUnderlayULoc = glGetUniformLocation(OverlayDebugShader, "uLegacyUnderlayEndpoint");
    OverlayDebugCoverageAwareULoc = glGetUniformLocation(OverlayDebugShader, "uCoverageAwareUnderlay");

    glUseProgram(MasterBrightnessShader);
    uniloc = glGetUniformLocation(MasterBrightnessShader, "SourceTex");
    glUniform1i(uniloc, 0);
    MasterBrightnessModeULoc = glGetUniformLocation(MasterBrightnessShader, "uBrightMode");
    MasterBrightnessFactorULoc = glGetUniformLocation(MasterBrightnessShader, "uBrightFactor");

    glUseProgram(RGBAToYUVAShader);
    uniloc = glGetUniformLocation(RGBAToYUVAShader, "Source");
    glUniform1i(uniloc, 0);

    glUseProgram(Spline36Shader);
    uniloc = glGetUniformLocation(Spline36Shader, "Source");
    glUniform1i(uniloc, 0);

    glUseProgram(ArtCNNYUVAToRGBA2xShader);
    uniloc = glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "Source");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "artcnn_luma");
    glUniform1i(uniloc, 1);
    uniloc = glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "AlphaSource");
    glUniform1i(uniloc, 2);
    uniloc = glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "uUseAlphaSource");
    glUniform1i(uniloc, 0);

    glUseProgram(AlphaReplaceShader);
    uniloc = glGetUniformLocation(AlphaReplaceShader, "Source");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(AlphaReplaceShader, "AlphaSource");
    glUniform1i(uniloc, 1);

    glUseProgram(XBRZPreprocessShader);
    uniloc = glGetUniformLocation(XBRZPreprocessShader, "Source");
    glUniform1i(uniloc, 0);

    glUseProgram(XBRZFreescaleShader);
    uniloc = glGetUniformLocation(XBRZFreescaleShader, "Source");
    glUniform1i(uniloc, 0);
    uniloc = glGetUniformLocation(XBRZFreescaleShader, "InfoTex");
    glUniform1i(uniloc, 1);

    // generate mosaic lookup texture

    u8* mosaic_tex = new u8[256 * 16];
    for (int m = 0; m < 16; m++)
    {
        int mosx = 0;
        for (int x = 0; x < 256; x++)
        {
            mosaic_tex[(m * 256) + x] = mosx;

            if (mosx == m)
                mosx = 0;
            else
                mosx++;
        }
    }

    glGenTextures(1, &MosaicTex);
    glBindTexture(GL_TEXTURE_2D, MosaicTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8I, 256, 16, 0, GL_RED_INTEGER, GL_BYTE, mosaic_tex);

    delete[] mosaic_tex;
    return true;
}

bool GLRenderer2D::InitShaders(GLRenderer2D& other)
{
    LayerPreShader = other.LayerPreShader;
    SpritePreShader = other.SpritePreShader;
    SpriteShader = other.SpriteShader;
    CompositorShader = other.CompositorShader;
    NativePrepassShader = other.NativePrepassShader;
    NativeUpscaleShader = other.NativeUpscaleShader;
    NativeBoundaryGuardShader = other.NativeBoundaryGuardShader;
    NativeResolveShader = other.NativeResolveShader;
    Native3DResolveShader = other.Native3DResolveShader;
    OverlayEndpointShader = other.OverlayEndpointShader;
    OverlayCompositeShader = other.OverlayCompositeShader;
    OverlayHybridCompositeShader = other.OverlayHybridCompositeShader;
    OverlayDebugShader = other.OverlayDebugShader;
    MasterBrightnessShader = other.MasterBrightnessShader;
    RGBAToYUVAShader = other.RGBAToYUVAShader;
    ArtCNNShaderOwner = &other;
    CopyArtCNNProgramsFrom(other);
    Spline36Shader = other.Spline36Shader;
    ArtCNNYUVAToRGBA2xShader = other.ArtCNNYUVAToRGBA2xShader;
    AlphaReplaceShader = other.AlphaReplaceShader;
    NNEDI3ComputeShaderOwner = &other;
    CopyNNEDI3ComputeProgramsFrom(other);
    XBRZPreprocessShader = other.XBRZPreprocessShader;
    XBRZFreescaleShader = other.XBRZFreescaleShader;
    CuNNyShaderOwner = &other;
    CopyCuNNyProgramsFrom(other);

    LayerPreCurBGULoc = other.LayerPreCurBGULoc;
    SpriteRenderTransULoc = other.SpriteRenderTransULoc;
    SpriteFilterModeULoc = other.SpriteFilterModeULoc;
    CompositorScaleULoc = other.CompositorScaleULoc;
    CompositorOBJNativeResolutionULoc = other.CompositorOBJNativeResolutionULoc;
    CompositorLayerFilterModeULoc = other.CompositorLayerFilterModeULoc;
    CompositorLayerFilterNoWrapULoc = other.CompositorLayerFilterNoWrapULoc;
    CompositorDebugTintULoc = other.CompositorDebugTintULoc;
    CompositorSplit3DSemanticsULoc = other.CompositorSplit3DSemanticsULoc;
    CompositorSharpenSplit3DCoverageULoc = other.CompositorSharpenSplit3DCoverageULoc;
    NativePrepassScaleULoc = other.NativePrepassScaleULoc;
    NativePrepassDebugLayerULoc = other.NativePrepassDebugLayerULoc;
    NativeUpscaleScaleULoc = other.NativeUpscaleScaleULoc;
    NativeUpscaleLegacyFilterULoc = other.NativeUpscaleLegacyFilterULoc;
    NativeResolveScaleULoc = other.NativeResolveScaleULoc;
    NativeResolveUseExactFinalFallbackULoc = other.NativeResolveUseExactFinalFallbackULoc;
    NativeResolveUseForegroundOverlayULoc = other.NativeResolveUseForegroundOverlayULoc;
    NativeResolveDebugTintULoc = other.NativeResolveDebugTintULoc;
    NativeResolveNativeExactOutputULoc = other.NativeResolveNativeExactOutputULoc;
    Native3DResolveFilterModeULoc = other.Native3DResolveFilterModeULoc;
    Native3DResolveCoverageAwareULoc = other.Native3DResolveCoverageAwareULoc;
    Native3DResolveRepresentativeSemanticsULoc = other.Native3DResolveRepresentativeSemanticsULoc;
    Native3DResolveSplitSemanticsULoc = other.Native3DResolveSplitSemanticsULoc;
    OverlayEndpointWhiteULoc = other.OverlayEndpointWhiteULoc;
    OverlayCompositeScaleULoc = other.OverlayCompositeScaleULoc;
    OverlayCompositeDebugTintULoc = other.OverlayCompositeDebugTintULoc;
    OverlayCompositeLegacyUnderlayULoc = other.OverlayCompositeLegacyUnderlayULoc;
    OverlayCompositeCoverageAwareULoc = other.OverlayCompositeCoverageAwareULoc;
    OverlayCompositeDirect3DPresentationSpaceULoc = other.OverlayCompositeDirect3DPresentationSpaceULoc;
    OverlayHybridCompositeScaleULoc = other.OverlayHybridCompositeScaleULoc;
    OverlayHybridCompositeDebugTintULoc = other.OverlayHybridCompositeDebugTintULoc;
    OverlayHybridCompositeLegacyUnderlayULoc = other.OverlayHybridCompositeLegacyUnderlayULoc;
    OverlayHybridCompositeCoverageAwareULoc = other.OverlayHybridCompositeCoverageAwareULoc;
    OverlayHybridCompositeDirect3DPresentationSpaceULoc = other.OverlayHybridCompositeDirect3DPresentationSpaceULoc;
    OverlayHybridCompositeConservativeHybridULoc = other.OverlayHybridCompositeConservativeHybridULoc;
    OverlayHybridCompositeWindowEdgeAssistULoc = other.OverlayHybridCompositeWindowEdgeAssistULoc;
    OverlayHybridCompositeTarget2AlphaBlendAssistULoc = other.OverlayHybridCompositeTarget2AlphaBlendAssistULoc;
    OverlayHybridCompositeNativeEffectGuardULoc = other.OverlayHybridCompositeNativeEffectGuardULoc;
    OverlayHybridCompositeForeground2DBaseULoc = other.OverlayHybridCompositeForeground2DBaseULoc;
    OverlayHybridCompositeLegacyCandidateULoc = other.OverlayHybridCompositeLegacyCandidateULoc;
    OverlayHybridCompositeForceOverlayAssistULoc = other.OverlayHybridCompositeForceOverlayAssistULoc;
    OverlayHybridCompositeDebugModeULoc = other.OverlayHybridCompositeDebugModeULoc;
    OverlayDebugModeULoc = other.OverlayDebugModeULoc;
    OverlayDebugLegacyUnderlayULoc = other.OverlayDebugLegacyUnderlayULoc;
    OverlayDebugCoverageAwareULoc = other.OverlayDebugCoverageAwareULoc;
    MasterBrightnessModeULoc = other.MasterBrightnessModeULoc;
    MasterBrightnessFactorULoc = other.MasterBrightnessFactorULoc;

    MosaicTex = other.MosaicTex;

    return true;
}

bool GLRenderer2D::Init()
{
    const GLenum fbassign2[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    const GLenum fbassign3[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
    };
    const GLenum fbassign4[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
    };
    const GLenum fbassign5[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
        GL_COLOR_ATTACHMENT4,
    };

    // sprite prerender vertex data: 2x position, 1x sprite index
    int sprdatasize = (3 * 6) * 128;
    SpritePreVtxData = new u16[sprdatasize];

    glGenBuffers(1, &SpritePreVtxBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, SpritePreVtxBuffer);
    glBufferData(GL_ARRAY_BUFFER, sprdatasize * sizeof(u16), nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &SpritePreVtxArray);
    glBindVertexArray(SpritePreVtxArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribIPointer(0, 2, GL_SHORT, 3 * sizeof(u16), (void*)0);
    glEnableVertexAttribArray(1); // sprite index
    glVertexAttribIPointer(1, 1, GL_SHORT, 3 * sizeof(u16), (void*)(2 * sizeof(u16)));

    // sprite vertex data: 2x position, 2x texcoord, 1x index
    sprdatasize = (5 * 6) * 256;
    SpriteVtxData = new u16[sprdatasize];

    glGenBuffers(1, &SpriteVtxBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, SpriteVtxBuffer);
    glBufferData(GL_ARRAY_BUFFER, sprdatasize * sizeof(u16), nullptr, GL_STREAM_DRAW);

    glGenVertexArrays(1, &SpriteVtxArray);
    glBindVertexArray(SpriteVtxArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribIPointer(0, 2, GL_SHORT, 5 * sizeof(u16), (void*)0);
    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribIPointer(1, 2, GL_SHORT, 5 * sizeof(u16), (void*)(2 * sizeof(u16)));
    glEnableVertexAttribArray(2); // sprite index
    glVertexAttribIPointer(2, 1, GL_SHORT, 5 * sizeof(u16), (void*)(4 * sizeof(u16)));

    // generate textures to hold raw BG and OBJ VRAM and palettes

    int bgheight = (GPU2D.Num == 0) ? 512 : 128;
    int objheight = (GPU2D.Num == 0) ? 256 : 128;

    glGenTextures(1, &VRAMTex_BG);
    glBindTexture(GL_TEXTURE_2D, VRAMTex_BG);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 1024, bgheight, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &VRAMTex_OBJ);
    glBindTexture(GL_TEXTURE_2D, VRAMTex_OBJ);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 1024, objheight, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &PalTex_BG);
    glBindTexture(GL_TEXTURE_2D, PalTex_BG);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 256, 1+(4*16), 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

    glGenTextures(1, &PalTex_OBJ);
    glBindTexture(GL_TEXTURE_2D, PalTex_OBJ);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 256, 1+16, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, nullptr);

    glGenTextures(1, &BlankColorTex);
    glBindTexture(GL_TEXTURE_2D, BlankColorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    const u32 blankPixel = 0;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &blankPixel);

    // generate texture to hold pre-rendered BG layers

    glGenTextures(22, AllBGLayerTex);
    glGenTextures(22, AllBGLayerMetaTex);
    glGenFramebuffers(22, AllBGLayerFB);

    const u16 bgsizes[8][3] = {
        {128, 128, 2},
        {256, 256, 4},
        {256, 512, 4},
        {512, 256, 4},
        {512, 512, 4},
        {512, 1024, 1},
        {1024, 512, 1},
        {1024, 1024, 2}
    };

    int l = 0;
    for (int j = 0; j < 8; j++)
    {
        const u16* sz = bgsizes[j];

        for (int k = 0; k < sz[2]; k++)
        {
            glBindTexture(GL_TEXTURE_2D, AllBGLayerTex[l]);
            glDefaultTexParams(GL_TEXTURE_2D);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz[0], sz[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindTexture(GL_TEXTURE_2D, AllBGLayerMetaTex[l]);
            glDefaultTexParams(GL_TEXTURE_2D);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz[0], sz[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindFramebuffer(GL_FRAMEBUFFER, AllBGLayerFB[l]);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, AllBGLayerTex[l], 0);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, AllBGLayerMetaTex[l], 0);
            glDrawBuffers(2, fbassign2);

            l++;
        }
    }

    // generate texture to hold pre-rendered sprites

    glGenTextures(1, &SpriteTex);
    glBindTexture(GL_TEXTURE_2D, SpriteTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &SpriteFB);
    glBindFramebuffer(GL_FRAMEBUFFER, SpriteFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, SpriteTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    // generate texture to hold final (upscaled) sprites

    glGenTextures(1, &OBJLayerTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, OBJLayerTex);
    glDefaultTexParams(GL_TEXTURE_2D_ARRAY);

    glGenTextures(1, &OBJDepthTex);
    glBindTexture(GL_TEXTURE_2D, OBJDepthTex);
    glDefaultTexParams(GL_TEXTURE_2D);

    glGenFramebuffers(1, &OBJLayerFB);

    // generate texture for the compositor output

    glGenTextures(1, &OutputTex);
    glBindTexture(GL_TEXTURE_2D, OutputTex);
    glDefaultTexParams(GL_TEXTURE_2D);

    glGenFramebuffers(1, &OutputFB);

    Parent.OutputTex2D[GPU2D.Num] = OutputTex;

    // generate native-resolution textures for the whole-scene prepass path

    glGenTextures(1, &NativeOBJLayerTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, NativeOBJLayerTex);
    glDefaultTexParams(GL_TEXTURE_2D_ARRAY);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256, 192, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeOBJDepthTex);
    glBindTexture(GL_TEXTURE_2D, NativeOBJDepthTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, 256, 192, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);

    glGenFramebuffers(1, &NativeOBJLayerFB);
    glBindFramebuffer(GL_FRAMEBUFFER, NativeOBJLayerFB);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeOBJLayerTex, 0, 0);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, NativeOBJLayerTex, 0, 1);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, NativeOBJDepthTex, 0);
    glDrawBuffers(2, fbassign2);

    glGenTextures(1, &NativeOutputTex);
    glBindTexture(GL_TEXTURE_2D, NativeOutputTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeTopColorTex);
    glBindTexture(GL_TEXTURE_2D, NativeTopColorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeSecondColorTex);
    glBindTexture(GL_TEXTURE_2D, NativeSecondColorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeMetaTex);
    glBindTexture(GL_TEXTURE_2D, NativeMetaTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeLayerDebugTex);
    glBindTexture(GL_TEXTURE_2D, NativeLayerDebugTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeExactFinalTex);
    glBindTexture(GL_TEXTURE_2D, NativeExactFinalTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeDirect3DTex);
    glBindTexture(GL_TEXTURE_2D, NativeDirect3DTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeDirect3DSemanticsTex);
    glBindTexture(GL_TEXTURE_2D, NativeDirect3DSemanticsTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeDirect3DCompositorTex);
    glBindTexture(GL_TEXTURE_2D, NativeDirect3DCompositorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeOverlayBlack3DTex);
    glBindTexture(GL_TEXTURE_2D, NativeOverlayBlack3DTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeOverlayWhite3DTex);
    glBindTexture(GL_TEXTURE_2D, NativeOverlayWhite3DTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    auto initCaptureEndpointTex = [](GLuint& tex, int size, int layers, u32 color)
    {
        std::vector<u32> pixels(static_cast<size_t>(size) * size * layers, color);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        glDefaultTexParams(GL_TEXTURE_2D_ARRAY);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, size, size, layers, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    };
    initCaptureEndpointTex(NativeOverlayBlackCapture128Tex, 128, 16, 0xFF000000u);
    initCaptureEndpointTex(NativeOverlayWhiteCapture128Tex, 128, 16, 0xFFFFFFFFu);
    initCaptureEndpointTex(NativeOverlayBlackCapture256Tex, 256, 4, 0xFF000000u);
    initCaptureEndpointTex(NativeOverlayWhiteCapture256Tex, 256, 4, 0xFFFFFFFFu);

    glGenTextures(1, &NativeOverlayTrueFinalTex);
    glBindTexture(GL_TEXTURE_2D, NativeOverlayTrueFinalTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeOverlayReconstructedTex);
    glBindTexture(GL_TEXTURE_2D, NativeOverlayReconstructedTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeOverlayErrorTex);
    glBindTexture(GL_TEXTURE_2D, NativeOverlayErrorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &NativeOverlayConfidenceTex);
    glBindTexture(GL_TEXTURE_2D, NativeOverlayConfidenceTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &NativeOutputFB);
    glBindFramebuffer(GL_FRAMEBUFFER, NativeOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeOutputTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, NativeTopColorTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, NativeSecondColorTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, NativeMetaTex, 0);
    glDrawBuffers(4, fbassign4);

    glGenFramebuffers(1, &NativeLayerDebugFB);
    glBindFramebuffer(GL_FRAMEBUFFER, NativeLayerDebugFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeLayerDebugTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glGenFramebuffers(1, &NativeExactFinalFB);
    glBindFramebuffer(GL_FRAMEBUFFER, NativeExactFinalFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeExactFinalTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glGenFramebuffers(1, &NativeDirect3DFB);
    glBindFramebuffer(GL_FRAMEBUFFER, NativeDirect3DFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeDirect3DTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, NativeDirect3DSemanticsTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, NativeDirect3DCompositorTex, 0);
    glDrawBuffers(3, fbassign3);

    glGenTextures(1, &UpscaledTopColorTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledTopColorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledSecondColorTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledSecondColorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledMetaTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledMetaTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledCoverageTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledCoverageTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledExactFinalTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledExactFinalTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledGuardColorTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledGuardColorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledOverlayUnderlayWeightTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledOverlayUnderlayWeightTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &UpscaledOverlayOwnershipTex);
    glBindTexture(GL_TEXTURE_2D, UpscaledOverlayOwnershipTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridForegroundTex);
    glBindTexture(GL_TEXTURE_2D, HybridForegroundTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridNativeFallbackTex);
    glBindTexture(GL_TEXTURE_2D, HybridNativeFallbackTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &Hybrid2DBaseTex);
    glBindTexture(GL_TEXTURE_2D, Hybrid2DBaseTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridLegacyCandidateTex);
    glBindTexture(GL_TEXTURE_2D, HybridLegacyCandidateTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridSelectorTex);
    glBindTexture(GL_TEXTURE_2D, HybridSelectorTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridCoverageMissTex);
    glBindTexture(GL_TEXTURE_2D, HybridCoverageMissTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridForegroundAlphaTex);
    glBindTexture(GL_TEXTURE_2D, HybridForegroundAlphaTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &HybridFinalSourceTex);
    glBindTexture(GL_TEXTURE_2D, HybridFinalSourceTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &WholeSceneSourceABlitFB);

    for (int i = 0; i < kCaptureBackedHandoffRouteSlots; i++)
    {
        glGenTextures(1, &CaptureBackedRouteGL[i].Handoff3DTex);
        glGenFramebuffers(1, &CaptureBackedRouteGL[i].Handoff3DFB);
        glGenTextures(1, &CaptureBackedRouteGL[i].ProductTex);
        glGenFramebuffers(1, &CaptureBackedRouteGL[i].ProductFB);
        glGenTextures(1, &CaptureBackedRouteGL[i].EventProductTex);
        glGenFramebuffers(1, &CaptureBackedRouteGL[i].EventProductFB);

        glBindTexture(GL_TEXTURE_2D, CaptureBackedRouteGL[i].Handoff3DTex);
        glDefaultTexParams(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, CaptureBackedRouteGL[i].Handoff3DFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[i].Handoff3DTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, CaptureBackedRouteGL[i].ProductTex);
        glDefaultTexParams(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, CaptureBackedRouteGL[i].ProductFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[i].ProductTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, CaptureBackedRouteGL[i].EventProductTex);
        glDefaultTexParams(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, CaptureBackedRouteGL[i].EventProductFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[i].EventProductTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glGenFramebuffers(1, &UpscaledStateFB);
    glBindFramebuffer(GL_FRAMEBUFFER, UpscaledStateFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, UpscaledTopColorTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, UpscaledSecondColorTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, UpscaledMetaTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, UpscaledCoverageTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, UpscaledExactFinalTex, 0);
    glDrawBuffers(5, fbassign5);

    glGenTextures(1, &ArtCNNYUVTex);
    glBindTexture(GL_TEXTURE_2D, ArtCNNYUVTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 256, 192, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenFramebuffers(1, &ArtCNNYUVFB);
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNYUVFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ArtCNNYUVTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glGenTextures(1, &ArtCNNConv0Tex);
    glBindTexture(GL_TEXTURE_2D, ArtCNNConv0Tex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 384, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenFramebuffers(1, &ArtCNNConv0FB);
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNConv0FB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ArtCNNConv0Tex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glGenTextures(2, ArtCNNConvWorkTex);
    glGenFramebuffers(2, ArtCNNConvWorkFB);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, ArtCNNConvWorkTex[i]);
        glDefaultTexParams(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 384, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNConvWorkFB[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ArtCNNConvWorkTex[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glGenTextures(1, &ArtCNNPackedTex);
    glBindTexture(GL_TEXTURE_2D, ArtCNNPackedTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 256, 192, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenFramebuffers(1, &ArtCNNPackedFB);
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNPackedFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ArtCNNPackedTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glGenTextures(1, &NNEDI3VerticalTex);
    glBindTexture(GL_TEXTURE_2D, NNEDI3VerticalTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 256, 384, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenTextures(1, &NNEDI3Vertical4xTex);
    glBindTexture(GL_TEXTURE_2D, NNEDI3Vertical4xTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 768, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenTextures(1, &ArtCNNLuma2xTex);
    glBindTexture(GL_TEXTURE_2D, ArtCNNLuma2xTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 384, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenTextures(1, &NNEDI3Luma4xTex);
    glBindTexture(GL_TEXTURE_2D, NNEDI3Luma4xTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1024, 768, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenTextures(1, &ArtCNNYUVA2xTex);
    glBindTexture(GL_TEXTURE_2D, ArtCNNYUVA2xTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 384, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenTextures(1, &ArtCNNRGBA2xTex);
    glBindTexture(GL_TEXTURE_2D, ArtCNNRGBA2xTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 512, 384, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

    glGenFramebuffers(1, &ArtCNNOutputFB);

    glGenTextures(2, CuNNyWorkTex);
    for (int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, CuNNyWorkTex[i]);
        glDefaultTexParams(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        CuNNyWorkTexWidth[i] = 1;
        CuNNyWorkTexHeight[i] = 1;
    }

    glGenTextures(1, &XBRZInfoTex);
    glBindTexture(GL_TEXTURE_2D, XBRZInfoTex);
    glDefaultTexParams(GL_TEXTURE_2D);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &XBRZInfoFB);
    glBindFramebuffer(GL_FRAMEBUFFER, XBRZInfoFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, XBRZInfoTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    // generate UBOs

    glGenBuffers(1, &LayerConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, LayerConfigUBO);
    static_assert((sizeof(sLayerConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sLayerConfig), nullptr, GL_STREAM_DRAW);

    glGenBuffers(1, &SpriteConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, SpriteConfigUBO);
    static_assert((sizeof(sSpriteConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sSpriteConfig), nullptr, GL_STREAM_DRAW);

    glGenBuffers(1, &ScanlineConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, ScanlineConfigUBO);
    static_assert((sizeof(sScanlineConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sScanlineConfig), nullptr, GL_STREAM_DRAW);

    glGenBuffers(1, &SpriteScanlineConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, SpriteScanlineConfigUBO);
    static_assert((sizeof(sSpriteScanlineConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sSpriteScanlineConfig), nullptr, GL_STREAM_DRAW);

    glGenBuffers(1, &CompositorConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
    static_assert((sizeof(sCompositorConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(sCompositorConfig), nullptr, GL_STREAM_DRAW);

    return true;
}

void GLRenderer2D::DeleteShaders()
{
    glDeleteProgram(LayerPreShader);
    glDeleteProgram(SpritePreShader);
    glDeleteProgram(SpriteShader);
    glDeleteProgram(CompositorShader);
    glDeleteProgram(NativePrepassShader);
    glDeleteProgram(NativeUpscaleShader);
    glDeleteProgram(NativeBoundaryGuardShader);
    glDeleteProgram(NativeResolveShader);
    glDeleteProgram(Native3DResolveShader);
    glDeleteProgram(OverlayEndpointShader);
    glDeleteProgram(OverlayCompositeShader);
    glDeleteProgram(OverlayHybridCompositeShader);
    glDeleteProgram(OverlayDebugShader);
    glDeleteProgram(RGBAToYUVAShader);
    for (int model = 0; model < RendererSettings::GLArtCNNModelCount; model++)
    {
        for (int i = 0; i < 7; i++)
            glDeleteProgram(ArtCNNConvShaders[model][i]);
        glDeleteProgram(ArtCNNDepthToSpaceShaders[model]);
    }
    glDeleteProgram(Spline36Shader);
    glDeleteProgram(ArtCNNYUVAToRGBA2xShader);
    glDeleteProgram(AlphaReplaceShader);
    glDeleteProgram(NNEDI3VerticalComputeShader);
    glDeleteProgram(NNEDI3HorizontalComputeShader);
    glDeleteProgram(XBRZPreprocessShader);
    glDeleteProgram(XBRZFreescaleShader);
    for (int model = 0; model < RendererSettings::GLCuNNyModelCount; model++)
    {
        glDeleteProgram(CuNNyInShaders[model]);
        for (int pass = 0; pass < RendererSettings::GLCuNNyMaxConvPasses; pass++)
            glDeleteProgram(CuNNyConvShaders[model][pass]);
        glDeleteProgram(CuNNyOutShaders[model]);
    }
    CuNNyProgramsReady = false;
    CuNNyProgramsFailed = false;
    ArtCNNComputeProgramsReady = false;
    ArtCNNComputeProgramsFailed = false;
    NNEDI3ComputeProgramsReady = false;
    NNEDI3ComputeProgramsFailed = false;

    glDeleteTextures(1, &MosaicTex);
}

GLRenderer2D::~GLRenderer2D()
{
    glDeleteBuffers(1, &SpritePreVtxBuffer);
    glDeleteVertexArrays(1, &SpritePreVtxArray);

    glDeleteBuffers(1, &SpriteVtxBuffer);
    glDeleteVertexArrays(1, &SpriteVtxArray);

    glDeleteBuffers(1, &LayerConfigUBO);
    glDeleteBuffers(1, &SpriteConfigUBO);

    glDeleteTextures(1, &VRAMTex_BG);
    glDeleteTextures(1, &VRAMTex_OBJ);
    glDeleteTextures(1, &PalTex_BG);
    glDeleteTextures(1, &PalTex_OBJ);
    glDeleteTextures(1, &BlankColorTex);

    glDeleteTextures(22, AllBGLayerTex);
    glDeleteTextures(22, AllBGLayerMetaTex);
    glDeleteFramebuffers(22, AllBGLayerFB);

    glDeleteTextures(1, &SpriteTex);
    glDeleteFramebuffers(1, &SpriteFB);

    glDeleteTextures(1, &OBJLayerTex);
    glDeleteTextures(1, &OBJDepthTex);
    glDeleteFramebuffers(1, &OBJLayerFB);

    glDeleteTextures(1, &OutputTex);
    glDeleteFramebuffers(1, &OutputFB);

    glDeleteTextures(1, &NativeOBJLayerTex);
    glDeleteTextures(1, &NativeOBJDepthTex);
    glDeleteFramebuffers(1, &NativeOBJLayerFB);

    glDeleteTextures(1, &NativeOutputTex);
    glDeleteTextures(1, &NativeTopColorTex);
    glDeleteTextures(1, &NativeSecondColorTex);
    glDeleteTextures(1, &NativeMetaTex);
    glDeleteTextures(1, &NativeLayerDebugTex);
    glDeleteTextures(1, &NativeExactFinalTex);
    glDeleteTextures(1, &NativeDirect3DTex);
    glDeleteTextures(1, &NativeDirect3DSemanticsTex);
    glDeleteTextures(1, &NativeDirect3DCompositorTex);
    glDeleteTextures(1, &NativeOverlayBlack3DTex);
    glDeleteTextures(1, &NativeOverlayWhite3DTex);
    glDeleteTextures(1, &NativeOverlayBlackCapture128Tex);
    glDeleteTextures(1, &NativeOverlayWhiteCapture128Tex);
    glDeleteTextures(1, &NativeOverlayBlackCapture256Tex);
    glDeleteTextures(1, &NativeOverlayWhiteCapture256Tex);
    glDeleteTextures(1, &NativeOverlayTrueFinalTex);
    glDeleteTextures(1, &NativeOverlayReconstructedTex);
    glDeleteTextures(1, &NativeOverlayErrorTex);
    glDeleteTextures(1, &NativeOverlayConfidenceTex);
    glDeleteFramebuffers(1, &NativeOutputFB);
    glDeleteFramebuffers(1, &NativeLayerDebugFB);
    glDeleteFramebuffers(1, &NativeExactFinalFB);
    glDeleteFramebuffers(1, &NativeDirect3DFB);

    glDeleteTextures(1, &UpscaledTopColorTex);
    glDeleteTextures(1, &UpscaledSecondColorTex);
    glDeleteTextures(1, &UpscaledMetaTex);
    glDeleteTextures(1, &UpscaledCoverageTex);
    glDeleteTextures(1, &UpscaledExactFinalTex);
    glDeleteTextures(1, &UpscaledGuardColorTex);
    glDeleteTextures(1, &UpscaledOverlayUnderlayWeightTex);
    glDeleteTextures(1, &UpscaledOverlayOwnershipTex);
    glDeleteTextures(1, &HybridForegroundTex);
    glDeleteTextures(1, &HybridNativeFallbackTex);
    glDeleteTextures(1, &Hybrid2DBaseTex);
    glDeleteTextures(1, &HybridLegacyCandidateTex);
    glDeleteTextures(1, &HybridSelectorTex);
    glDeleteTextures(1, &HybridCoverageMissTex);
    glDeleteTextures(1, &HybridForegroundAlphaTex);
    glDeleteTextures(1, &HybridFinalSourceTex);
    glDeleteFramebuffers(1, &WholeSceneSourceABlitFB);
    for (int i = 0; i < kCaptureBackedHandoffRouteSlots; i++)
    {
        glDeleteTextures(1, &CaptureBackedRouteGL[i].Handoff3DTex);
        glDeleteFramebuffers(1, &CaptureBackedRouteGL[i].Handoff3DFB);
        glDeleteTextures(1, &CaptureBackedRouteGL[i].ProductTex);
        glDeleteFramebuffers(1, &CaptureBackedRouteGL[i].ProductFB);
        glDeleteTextures(1, &CaptureBackedRouteGL[i].EventProductTex);
        glDeleteFramebuffers(1, &CaptureBackedRouteGL[i].EventProductFB);
    }
    glDeleteFramebuffers(1, &UpscaledStateFB);

    glDeleteTextures(1, &ArtCNNYUVTex);
    glDeleteFramebuffers(1, &ArtCNNYUVFB);
    glDeleteTextures(1, &ArtCNNConv0Tex);
    glDeleteFramebuffers(1, &ArtCNNConv0FB);
    glDeleteTextures(2, ArtCNNConvWorkTex);
    glDeleteFramebuffers(2, ArtCNNConvWorkFB);
    glDeleteTextures(1, &ArtCNNPackedTex);
    glDeleteFramebuffers(1, &ArtCNNPackedFB);
    glDeleteTextures(1, &NNEDI3VerticalTex);
    glDeleteTextures(1, &NNEDI3Vertical4xTex);
    glDeleteTextures(1, &ArtCNNLuma2xTex);
    glDeleteTextures(1, &NNEDI3Luma4xTex);
    glDeleteTextures(1, &ArtCNNYUVA2xTex);
    glDeleteTextures(1, &ArtCNNRGBA2xTex);
    glDeleteFramebuffers(1, &ArtCNNOutputFB);
    glDeleteTextures(2, CuNNyWorkTex);
    glDeleteTextures(1, &XBRZInfoTex);
    glDeleteFramebuffers(1, &XBRZInfoFB);

    glDeleteBuffers(1, &ScanlineConfigUBO);
    glDeleteBuffers(1, &SpriteScanlineConfigUBO);
    glDeleteBuffers(1, &CompositorConfigUBO);
}

void GLRenderer2D::Reset()
{
    memset(BGLayerFB, 0, sizeof(BGLayerFB));
    memset(BGLayerTex, 0, sizeof(BGLayerTex));
    memset(BGLayerMetaTex, 0, sizeof(BGLayerMetaTex));

    memset(&LayerConfig, 0, sizeof(LayerConfig));
    memset(&SpriteConfig, 0, sizeof(SpriteConfig));
    memset(&ScanlineConfig, 0, sizeof(ScanlineConfig));
    memset(&SpriteScanlineConfig, 0, sizeof(SpriteScanlineConfig));
    memset(&CompositorConfig, 0, sizeof(CompositorConfig));

    int bgheight = (GPU2D.Num == 0) ? 512 : 128;
    int objheight = (GPU2D.Num == 0) ? 256 : 128;
    LayerConfig.uVRAMMask = bgheight - 1;
    SpriteConfig.uVRAMMask = objheight - 1;

    LastLine = 0;

    UnitEnabled = false;

    DispCnt = 0;
    LayerEnable = 0;
    OBJEnable = 0;
    ForcedBlank = 0;
    memset(BGCnt, 0, sizeof(BGCnt));
    BlendCnt = 0;
    EVA = 0; EVB = 0; EVY = 0;

    memset(BGVRAMRange, 0xFF, sizeof(BGVRAMRange));

    LayerConfigDirty = true;
    DeferredLayerPrerenderDirty = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        DeferredLayerPrerenderFirstRow[layer] = -1;
        DeferredLayerPrerenderLastRow[layer] = -1;
    }

    LastSpriteLine = 0;
    memset(OAM, 0, sizeof(OAM));
    NumSprites = 0;
    SpriteUseMosaic = false;

    SpriteDispCnt = 0;
    SpriteConfigDirty = true;
    SpriteDirty = true;

    memset(TempPalBuffer, 0, sizeof(TempPalBuffer));

    WholeSceneScaleState = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneTrace = {};
    WholeSceneDebugPoison = {};
    ResetWholeSceneNativeProductTracking();
}

void GLRenderer2D::PostSavestate()
{
    Reset();
}


void GLRenderer2D::SetScaleFactor(int scale)
{
    if (scale == ScaleFactor)
        return;

    ScaleFactor = scale;
    ScreenW = 256 * scale;
    ScreenH = 192 * scale;

    const GLenum fbassign2[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    const GLenum fbassign3[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    const GLenum fbassign5[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3,
        GL_COLOR_ATTACHMENT4,
    };
    const GLenum fbassign4[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};

    glUseProgram(CompositorShader);
    glUniform1i(CompositorScaleULoc, ScaleFactor);
    glUseProgram(NativeUpscaleShader);
    glUniform1i(NativeUpscaleScaleULoc, ScaleFactor);
    glUseProgram(NativeResolveShader);
    glUniform1i(NativeResolveScaleULoc, ScaleFactor);
    glUseProgram(OverlayCompositeShader);
    glUniform1i(OverlayCompositeScaleULoc, ScaleFactor);
    glUseProgram(OverlayHybridCompositeShader);
    glUniform1i(OverlayHybridCompositeScaleULoc, ScaleFactor);

    glBindTexture(GL_TEXTURE_2D_ARRAY, OBJLayerTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, ScreenW, ScreenH, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, OBJDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, ScreenW, ScreenH, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, OBJLayerFB);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, OBJLayerTex, 0, 0);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, OBJLayerTex, 0, 1);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, OBJLayerTex, 0, 2);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, OBJDepthTex, 0);
    glDrawBuffers(3, fbassign3);

    glBindTexture(GL_TEXTURE_2D, OutputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledTopColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledSecondColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledMetaTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledCoverageTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledExactFinalTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledGuardColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledOverlayUnderlayWeightTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, UpscaledOverlayOwnershipTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridForegroundTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridNativeFallbackTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, Hybrid2DBaseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridLegacyCandidateTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridSelectorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridCoverageMissTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridForegroundAlphaTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, HybridFinalSourceTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int i = 0; i < kCaptureBackedHandoffRouteSlots; i++)
    {
        glBindTexture(GL_TEXTURE_2D, CaptureBackedRouteGL[i].Handoff3DTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureBackedRouteGL[i].Handoff3DFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[i].Handoff3DTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, CaptureBackedRouteGL[i].ProductTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureBackedRouteGL[i].ProductFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[i].ProductTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glBindTexture(GL_TEXTURE_2D, CaptureBackedRouteGL[i].EventProductTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, CaptureBackedRouteGL[i].EventProductFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[i].EventProductTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    InvalidateCaptureBackedHandoffSnapshot();

    glBindFramebuffer(GL_FRAMEBUFFER, OutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, OutputTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_FRAMEBUFFER, UpscaledStateFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, UpscaledTopColorTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, UpscaledSecondColorTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, UpscaledMetaTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, UpscaledCoverageTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, UpscaledExactFinalTex, 0);
    glDrawBuffers(5, fbassign5);
}

void GLRenderer2D::SetRenderSettings(int scale, const RendererSettings::WholeScene2DScaleSettings& settings)
{
    SetScaleFactor(scale);
    SetWholeSceneScaleRequested(settings.Enabled);
    SetWholeSceneScaleSourceBoundaryGuard(settings.SourceBoundaryGuard);
    SetWholeSceneScaleMode(settings.Mode);
    SetWholeSceneScaleAlgorithm(settings.Algorithm);
    SetWholeSceneScaleFragmentationFallback(settings.FragmentationFallback);
    SetWholeSceneScaleExactFinalFallback(settings.ExactFinalFallback);
    SetWholeSceneScaleForegroundOverlay(settings.ForegroundOverlay);
    SetWholeSceneScaleCaptureBacked(settings.CaptureBacked);
    SetWholeSceneScaleDebugTint(settings.DebugTint);
    SetWholeSceneScaleNoWrapFilterTaps(settings.NoWrapFilterTaps);
    SetWholeSceneScaleFinalUpscaleRender3DNative(settings.FinalUpscaleRender3DNative);
    SetWholeSceneScaleFinalUpscale3DFilter(settings.FinalUpscale3DFilter);
    SetWholeSceneScaleFinalUpscale3DCoverageAware(settings.FinalUpscale3DCoverageAware);
    SetWholeSceneScaleFinalUpscale3DRepresentativeSemantics(settings.FinalUpscale3DRepresentativeSemantics);
    SetWholeSceneScaleFinalUpscale3DSplitSemantics(settings.FinalUpscale3DSplitSemantics);
    SetWholeSceneScaleFinalUpscale3DSharpenSplitCoverage(settings.FinalUpscale3DSharpenSplitCoverage);
    SetWholeSceneScaleOverlayLegacyUnderlay(settings.OverlayLegacyUnderlay);
    SetWholeSceneScaleHybridWindowEdgeAssist(settings.HybridWindowEdgeAssist);
    SetWholeSceneScaleHybridTarget2AlphaBlendAssist(settings.HybridTarget2AlphaBlendAssist);
    SetWholeSceneScaleHybridNativeEffectGuard(settings.HybridNativeEffectGuard);
    SetWholeSceneScaleHybridForeground2DBase(settings.HybridForeground2DBase);
    SetWholeSceneScaleHybridCleanLegacyCandidate(settings.HybridCleanLegacyCandidate);
}

void GLRenderer2D::SetWholeSceneScaleRequested(bool enable)
{
    WholeSceneScaleRequested = enable;
    if (!enable)
    {
        WholeSceneCurrentFramePartialComposites = 0;
        WholeScenePreviousFramePartialComposites = 0;
        WholeSceneCurrentUpdateDebugTrace = {};
        WholeScenePreviousUpdateDebugTrace = {};
        WholeSceneHybridFragmentationGuardFrames = 0;
        WholeSceneCurrentFragmentationGuardFrames = 0;
        WholeSceneCaptureBackedHandoffGuardFrames = 0;
        InvalidateCaptureBackedHandoffSnapshot();
        WholeSceneHybridFragmentationGuardTripped = false;
        WholeSceneCurrentFragmentationGuardTripped = false;
        ResetWholeSceneNativeProductTracking();
    }
}

void GLRenderer2D::SetWholeSceneScaleSourceBoundaryGuard(bool enable)
{
    WholeSceneScaleSourceBoundaryGuard = enable;
}

void GLRenderer2D::SetWholeSceneScaleMode(RendererSettings::WholeScene2DScaleMode mode)
{
    if (WholeSceneScaleMode != mode)
    {
        WholeSceneCurrentFramePartialComposites = 0;
        WholeScenePreviousFramePartialComposites = 0;
        WholeSceneCurrentUpdateDebugTrace = {};
        WholeScenePreviousUpdateDebugTrace = {};
        WholeSceneHybridFragmentationGuardFrames = 0;
        WholeSceneCurrentFragmentationGuardFrames = 0;
        WholeSceneCaptureBackedHandoffGuardFrames = 0;
        InvalidateCaptureBackedHandoffSnapshot();
        WholeSceneHybridFragmentationGuardTripped = false;
        WholeSceneCurrentFragmentationGuardTripped = false;
        ResetWholeSceneNativeProductTracking();
    }
    WholeSceneScaleMode = mode;
}

void GLRenderer2D::SetWholeSceneScaleAlgorithm(RendererSettings::GLScaleAlgorithm algorithm)
{
    WholeSceneScaleAlgorithm = algorithm;
}

void GLRenderer2D::SetWholeSceneScaleFragmentationFallback(RendererSettings::WholeScene2DFragmentationFallback fallback)
{
    if (WholeSceneScaleFragmentationFallback != fallback)
    {
        WholeSceneCurrentFramePartialComposites = 0;
        WholeScenePreviousFramePartialComposites = 0;
        WholeSceneCurrentUpdateDebugTrace = {};
        WholeScenePreviousUpdateDebugTrace = {};
        WholeSceneHybridFragmentationGuardFrames = 0;
        WholeSceneCurrentFragmentationGuardFrames = 0;
        WholeSceneCaptureBackedHandoffGuardFrames = 0;
        InvalidateCaptureBackedHandoffSnapshot();
        WholeSceneHybridFragmentationGuardTripped = false;
        WholeSceneCurrentFragmentationGuardTripped = false;
        ResetWholeSceneNativeProductTracking();
    }
    WholeSceneScaleFragmentationFallback = fallback;
}

void GLRenderer2D::SetWholeSceneScaleExactFinalFallback(bool enable)
{
    WholeSceneScaleExactFinalFallback = enable;
}

void GLRenderer2D::SetWholeSceneScaleForegroundOverlay(bool enable)
{
    WholeSceneScaleForegroundOverlay = enable;
}

void GLRenderer2D::SetWholeSceneScaleCaptureBacked(bool enable)
{
    if (WholeSceneScaleCaptureBacked != enable)
    {
        WholeSceneCaptureBackedHandoffGuardFrames = 0;
        InvalidateCaptureBackedHandoffSnapshot();
    }
    WholeSceneScaleCaptureBacked = enable;
}

void GLRenderer2D::SetWholeSceneScaleDebugTint(bool enable)
{
    WholeSceneScaleDebugTint = enable;
}

void GLRenderer2D::SetWholeSceneScaleNoWrapFilterTaps(bool enable)
{
    WholeSceneScaleNoWrapFilterTaps = enable;
}

void GLRenderer2D::SetWholeSceneScaleFinalUpscaleRender3DNative(bool enable)
{
    WholeSceneScaleFinalUpscaleRender3DNative = enable;
}

void GLRenderer2D::SetWholeSceneScaleFinalUpscale3DFilter(RendererSettings::FinalUpscale3DDownsampleFilter filter)
{
    WholeSceneScaleFinalUpscale3DFilter = filter;
}

void GLRenderer2D::SetWholeSceneScaleFinalUpscale3DCoverageAware(bool enable)
{
    WholeSceneScaleFinalUpscale3DCoverageAware = enable;
}

void GLRenderer2D::SetWholeSceneScaleFinalUpscale3DRepresentativeSemantics(bool enable)
{
    WholeSceneScaleFinalUpscale3DRepresentativeSemantics = enable;
}

void GLRenderer2D::SetWholeSceneScaleFinalUpscale3DSplitSemantics(bool enable)
{
    WholeSceneScaleFinalUpscale3DSplitSemantics = enable;
}

void GLRenderer2D::SetWholeSceneScaleFinalUpscale3DSharpenSplitCoverage(bool enable)
{
    WholeSceneScaleFinalUpscale3DSharpenSplitCoverage = enable;
}

void GLRenderer2D::SetWholeSceneScaleOverlayLegacyUnderlay(bool enable)
{
    WholeSceneScaleOverlayLegacyUnderlay = enable;
}

void GLRenderer2D::SetWholeSceneScaleHybridWindowEdgeAssist(bool enable)
{
    WholeSceneScaleHybridWindowEdgeAssist = enable;
}

void GLRenderer2D::SetWholeSceneScaleHybridTarget2AlphaBlendAssist(bool enable)
{
    WholeSceneScaleHybridTarget2AlphaBlendAssist = enable;
}

void GLRenderer2D::SetWholeSceneScaleHybridNativeEffectGuard(bool enable)
{
    WholeSceneScaleHybridNativeEffectGuard = enable;
}

void GLRenderer2D::SetWholeSceneScaleHybridForeground2DBase(bool enable)
{
    WholeSceneScaleHybridForeground2DBase = enable;
}

void GLRenderer2D::SetWholeSceneScaleHybridCleanLegacyCandidate(bool enable)
{
    if (WholeSceneScaleHybridCleanLegacyCandidate != enable)
    {
        WholeSceneCurrentFramePartialComposites = 0;
        WholeScenePreviousFramePartialComposites = 0;
        WholeSceneCurrentUpdateDebugTrace = {};
        WholeScenePreviousUpdateDebugTrace = {};
        WholeSceneHybridFragmentationGuardFrames = 0;
        WholeSceneCurrentFragmentationGuardFrames = 0;
        WholeSceneCaptureBackedHandoffGuardFrames = 0;
        InvalidateCaptureBackedHandoffSnapshot();
        WholeSceneHybridFragmentationGuardTripped = false;
        WholeSceneCurrentFragmentationGuardTripped = false;
        ResetWholeSceneNativeProductTracking();
    }
    WholeSceneScaleHybridCleanLegacyCandidate = enable;
}

void GLRenderer2D::SetWholeSceneDebugPoison(bool source3D, bool native3DResolve, bool native3DResolveAlpha)
{
    WholeSceneDebugPoison.Source3D = source3D;
    WholeSceneDebugPoison.Native3DResolve = native3DResolve;
    WholeSceneDebugPoison.Native3DResolveAlpha = native3DResolveAlpha;
}

void GLRenderer2D::SetWholeSceneDebugViewsActive(bool active)
{
    WholeSceneDebugViewsActive.store(active, std::memory_order_relaxed);
}

namespace
{
struct DebugRGB
{
    u8 r, g, b;
};

// Slow diagnostic readback used only by the whole-scene debug/status UI.
std::string FormatTextureContentStats(const char* label, GLuint texture);

DebugRGB DebugTintColorCPU(int sourceMask, int specialType)
{
    if (specialType == 1) return {255, 0, 255};
    if (sourceMask == (1 << 0)) return {255, 89, 51};
    if (sourceMask == (1 << 1)) return {51, 255, 51};
    if (sourceMask == (1 << 2)) return {51, 179, 255};
    if (sourceMask == (1 << 3)) return {255, 191, 51};
    if (sourceMask == (1 << 4)) return {255, 255, 51};
    if (sourceMask == 0x20) return {153, 153, 153};
    return {255, 255, 255};
}

int SourceRankCPU(int mask)
{
    if (mask == (1 << 3)) return 0;
    if (mask == (1 << 2)) return 1;
    if (mask == (1 << 1)) return 2;
    if (mask == (1 << 0)) return 3;
    if (mask == (1 << 4)) return 4;
    if (mask == 0x20) return -1;
    if (mask == 0) return -2;
    return -2;
}

bool IsInFrontCPU(int prioA, int maskA, int prioB, int maskB)
{
    if (maskA == 0)
        return false;
    if (maskB == 0)
        return true;

    if (prioA != prioB)
        return prioA < prioB;

    return SourceRankCPU(maskA) > SourceRankCPU(maskB);
}

enum class DebugStackEffectClass
{
    None,
    Native2D,
    Direct3D,
};

struct DebugQuantizedColor
{
    int r;
    int g;
    int b;
    int a;
};

struct DebugStackEffectInfo
{
    int effect = 0;
    int eva = 0;
    int evb = 0;
    int evy = 0;
};

DebugQuantizedColor QuantizeDebugColorCPU(u32 color)
{
    const u8* chan = reinterpret_cast<const u8*>(&color);
    return {
        chan[0] >> 2,
        chan[1] >> 2,
        chan[2] >> 2,
        chan[3] >> 3,
    };
}

DebugStackEffectInfo ResolveStackEffectInfoCPU(int blendCnt,
                                               int blendEffect,
                                               bool blendAllowed,
                                               int specialType,
                                               int sourceMask1,
                                               int sourceMask2,
                                               const DebugQuantizedColor& topColor,
                                               int eva,
                                               int evb,
                                               int evy)
{
    DebugStackEffectInfo info;
    info.evy = evy;

    if ((specialType != 0) && ((blendCnt & (sourceMask2 << 8)) != 0))
    {
        if (specialType == 1)
        {
            info.effect = 4;
            info.eva = (topColor.a & 0x1F) + 1;
            info.evb = 32 - info.eva;
        }
        else if (specialType == 2)
        {
            info.effect = 1;
            info.eva = eva;
            info.evb = evb;
        }
        else
        {
            info.effect = 1;
            info.eva = topColor.a;
            info.evb = 16 - info.eva;
        }
    }
    else if (((blendCnt & sourceMask1) != 0) && blendAllowed)
    {
        info.effect = blendEffect;
        if (info.effect == 1)
        {
            if ((blendCnt & (sourceMask2 << 8)) != 0)
            {
                info.eva = eva;
                info.evb = evb;
            }
            else
            {
                info.effect = 0;
            }
        }
        else if (info.effect != 2 && info.effect != 3)
        {
            info.effect = 0;
        }
    }

    if ((info.effect == 2 || info.effect == 3) && info.evy == 0)
        info.effect = 0;

    return info;
}

DebugQuantizedColor ApplyStackEffectCPU(const DebugQuantizedColor& topColor,
                                        const DebugQuantizedColor& secondColor,
                                        const DebugStackEffectInfo& effect)
{
    DebugQuantizedColor out = topColor;

    auto clamp6 = [](int value) -> int { return std::min<int>(0x3F, std::max<int>(0, value)); };
    auto alphaBlend16 = [&](int top, int second) -> int {
        return clamp6(((top * effect.eva) + (second * effect.evb) + 0x8) >> 4);
    };
    auto alphaBlend32 = [&](int top, int second) -> int {
        return clamp6(((top * effect.eva) + (second * effect.evb) + 0x10) >> 5);
    };
    auto brightnessUp = [&](int top) -> int {
        return clamp6(top + ((((0x3F - top) * effect.evy) + 0x8) >> 4));
    };
    auto brightnessDown = [&](int top) -> int {
        return clamp6(top - (((top * effect.evy) + 0x7) >> 4));
    };

    if (effect.effect == 1)
    {
        out.r = alphaBlend16(topColor.r, secondColor.r);
        out.g = alphaBlend16(topColor.g, secondColor.g);
        out.b = alphaBlend16(topColor.b, secondColor.b);
    }
    else if (effect.effect == 2)
    {
        out.r = brightnessUp(topColor.r);
        out.g = brightnessUp(topColor.g);
        out.b = brightnessUp(topColor.b);
    }
    else if (effect.effect == 3)
    {
        out.r = brightnessDown(topColor.r);
        out.g = brightnessDown(topColor.g);
        out.b = brightnessDown(topColor.b);
    }
    else if (effect.effect == 4)
    {
        out.r = alphaBlend32(topColor.r, secondColor.r);
        out.g = alphaBlend32(topColor.g, secondColor.g);
        out.b = alphaBlend32(topColor.b, secondColor.b);
    }

    return out;
}

int MaxDebugRGBDeltaCPU(const DebugQuantizedColor& a, const DebugQuantizedColor& b)
{
    return std::max({
        std::abs(a.r - b.r),
        std::abs(a.g - b.g),
        std::abs(a.b - b.b),
    });
}

DebugStackEffectClass ClassifyStackEffectCPU(int blendCnt,
                                             int blendEffect,
                                             bool blendAllowed,
                                             int specialType,
                                             int sourceMask1,
                                             int sourceMask2,
                                             int evy)
{
    constexpr int direct3DMask = 1 << 0;
    int effect = 0;
    bool usesDirect3D = false;

    if ((specialType != 0) && ((blendCnt & (sourceMask2 << 8)) != 0))
    {
        effect = (specialType == 1) ? 4 : 1;
        usesDirect3D = sourceMask1 == direct3DMask || sourceMask2 == direct3DMask;
    }
    else if (((blendCnt & sourceMask1) != 0) && blendAllowed)
    {
        effect = blendEffect;
        if (effect == 1)
        {
            if ((blendCnt & (sourceMask2 << 8)) != 0)
                usesDirect3D = sourceMask1 == direct3DMask || sourceMask2 == direct3DMask;
            else
                effect = 0;
        }
        else if (effect == 2 || effect == 3)
        {
            if (evy != 0)
                usesDirect3D = sourceMask1 == direct3DMask;
            else
                effect = 0;
        }
        else
        {
            effect = 0;
        }
    }

    if (effect == 0)
        return DebugStackEffectClass::None;
    return usesDirect3D ? DebugStackEffectClass::Direct3D : DebugStackEffectClass::Native2D;
}

u32 HashDebugColor(u32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

DebugRGB BGMetaDebugColorCPU(int sourceType, int sourceID)
{
    if (sourceType == 0)
        return {32, 32, 32};

    const u32 hash = HashDebugColor((static_cast<u32>(sourceType) << 24) ^ static_cast<u32>(sourceID));
    return {
        static_cast<u8>(64 + (hash & 0xBF)),
        static_cast<u8>(64 + ((hash >> 8) & 0xBF)),
        static_cast<u8>(64 + ((hash >> 16) & 0xBF)),
    };
}

const char* FinalUpscale3DFilterName(RendererSettings::FinalUpscale3DDownsampleFilter filter)
{
    switch (filter)
    {
    case RendererSettings::FinalUpscale3DDownsampleFilter::Linear:
        return "linear";
    case RendererSettings::FinalUpscale3DDownsampleFilter::Tent:
        return "tent";
    case RendererSettings::FinalUpscale3DDownsampleFilter::Area:
    default:
        return "area/box";
    }
}

bool IsHighResDebugView(WholeScene2DDebugView view)
{
    using DebugView = WholeScene2DDebugView;
    return view == DebugView::HighResBG0Color ||
           view == DebugView::HighResBG1Color ||
           view == DebugView::HighResBG2Color ||
           view == DebugView::HighResBG3Color ||
           view == DebugView::HighResBG0Meta ||
           view == DebugView::HighResBG1Meta ||
           view == DebugView::HighResBG2Meta ||
           view == DebugView::HighResBG3Meta ||
           view == DebugView::HighResOBJColor ||
           view == DebugView::HighResOBJFlags ||
           view == DebugView::HighResOBJCoverage;
}

bool IsOverlayDebugView(WholeScene2DDebugView view)
{
    using DebugView = WholeScene2DDebugView;
    return view == DebugView::OverlayOperatorColor ||
           view == DebugView::OverlayUnderlayWeight ||
           view == DebugView::OverlayReconstructedNative ||
           view == DebugView::OverlayReconstructionError ||
           view == DebugView::OverlayValidityConfidence ||
           view == DebugView::OverlayOwnershipReason ||
           view == DebugView::OverlayEnhancedUnderlay ||
           view == DebugView::OverlayTrueNativeFinal ||
           view == DebugView::OverlayFinalResult;
}

bool IsAuxiliaryWholeSceneDebugView(WholeScene2DDebugView view)
{
    using DebugView = WholeScene2DDebugView;
    return view == DebugView::OverlayUnderlayWeight ||
           view == DebugView::OverlayReconstructedNative ||
           view == DebugView::OverlayReconstructionError ||
           view == DebugView::OverlayValidityConfidence ||
           view == DebugView::OverlayOwnershipReason ||
           view == DebugView::HybridSelector ||
           view == DebugView::HybridCoverageMiss ||
           view == DebugView::HybridForegroundAlpha ||
           view == DebugView::HybridFinalSource ||
           view == DebugView::SandwichLower2D ||
           view == DebugView::SandwichUpper2D ||
           view == DebugView::SandwichEligibility;
}

int HighResDebugBGIndex(WholeScene2DDebugView view)
{
    using DebugView = WholeScene2DDebugView;
    switch (view)
    {
    case DebugView::HighResBG0Color:
    case DebugView::HighResBG0Meta:
        return 0;
    case DebugView::HighResBG1Color:
    case DebugView::HighResBG1Meta:
        return 1;
    case DebugView::HighResBG2Color:
    case DebugView::HighResBG2Meta:
        return 2;
    case DebugView::HighResBG3Color:
    case DebugView::HighResBG3Meta:
        return 3;
    default:
        return -1;
    }
}

int NativeLayerDebugIndex(WholeScene2DDebugView view)
{
    using DebugView = WholeScene2DDebugView;
    switch (view)
    {
    case DebugView::NativeBG0Color:
        return 0;
    case DebugView::NativeBG1Color:
        return 1;
    case DebugView::NativeBG2Color:
        return 2;
    case DebugView::NativeBG3Color:
        return 3;
    case DebugView::NativeOBJColor:
        return 4;
    case DebugView::NativeOBJFlags:
        return 5;
    case DebugView::NativeOBJCoverage:
        return 6;
    default:
        return -1;
    }
}

bool IsNativeLayerDebugView(WholeScene2DDebugView view)
{
    return NativeLayerDebugIndex(view) >= 0;
}
}

void GLRenderer2D::ResetWholeSceneRenderTrace()
{
    WholeSceneTrace = {};
    UpdateWholeSceneTraceFrameSplitState();
}

void GLRenderer2D::ResetWholeSceneNativeProductTracking()
{
    WholeSceneNativeChunkAccumulationPasses = 0;
    WholeSceneFullFrameFinalizerPasses = 0;
    WholeSceneNativeProductValidRows = 0;
    WholeSceneNativeProductsFrameComplete = false;
    memset(WholeSceneNativeProductRowValid, 0, sizeof(WholeSceneNativeProductRowValid));
    WholeSceneNativeProductEpochValid = true;
    WholeSceneNativeProductEpochInvalidReason = 0;
    WholeSceneNativeProductEligibilityInitialized = false;
    WholeSceneNativeProductFrameEligibility = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneNativeProductLastEligibility = WholeSceneScaleEligibility::ScreenUnavailable;
    WholeSceneNativeProductPathInitialized = false;
    WholeSceneNativeProductFramePath = WholeSceneRenderPath::None;
    WholeSceneNativeProductLastPath = WholeSceneRenderPath::None;
    WholeSceneNativeProductFinalizerPathSeen = false;
    WholeSceneOverlayEndpointsValid = false;
    WholeSceneOverlayEndpointSourceTex = 0;
    UpdateWholeSceneTraceFrameSplitState();
}

void GLRenderer2D::UpdateWholeSceneTraceFrameSplitState()
{
    WholeSceneTrace.NativeChunkAccumulationPasses = WholeSceneNativeChunkAccumulationPasses;
    WholeSceneTrace.FullFrameFinalizerPasses = WholeSceneFullFrameFinalizerPasses;
    WholeSceneTrace.NativeProductValidRows = WholeSceneNativeProductValidRows;
    WholeSceneTrace.NativeProductsFrameComplete = WholeSceneNativeProductsFrameComplete;
    WholeSceneTrace.NativeProductEpochValid = WholeSceneNativeProductEpochValid;
    WholeSceneTrace.NativeProductEpochInvalidReason = WholeSceneNativeProductEpochInvalidReason;
    WholeSceneTrace.NativeProductFrameEligibility = WholeSceneNativeProductFrameEligibility;
    WholeSceneTrace.NativeProductLastEligibility = WholeSceneNativeProductLastEligibility;
    WholeSceneTrace.NativeProductFramePath = WholeSceneNativeProductFramePath;
    WholeSceneTrace.NativeProductLastPath = WholeSceneNativeProductLastPath;
    WholeSceneTrace.NativeProductFinalizerPathSeen = WholeSceneNativeProductFinalizerPathSeen;
}

bool GLRenderer2D::IsBenignMainVRAMDisplayNativeProductEligibilityTransition(
    WholeSceneScaleEligibility eligibility) const
{
    if (GPU2D.Num ||
        WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleCaptureBacked ||
        WholeSceneFullFrameFinalizerUnsafeFrame ||
        WholeSceneCurrentFramePartialComposites != 0 ||
        WholeSceneNativeProductFrameEligibility != WholeSceneScaleEligibility::MainEngineVRAMDisplay ||
        eligibility != WholeSceneScaleEligibility::Eligible)
    {
        return false;
    }

    const u32 dispmode = (DispCnt >> 16) & 0x3u;
    if (dispmode != 2)
        return false;

    const u32 displayBank = (DispCnt >> 18) & 0x3u;
    return Parent.CanUseMainVRAMDisplayHighResCaptureReplacement(displayBank);
}

void GLRenderer2D::RecordWholeSceneNativeProductEligibility(WholeSceneScaleEligibility eligibility)
{
    if (!WholeSceneNativeProductEligibilityInitialized)
    {
        WholeSceneNativeProductEligibilityInitialized = true;
        WholeSceneNativeProductFrameEligibility = eligibility;
    }
    else if (WholeSceneNativeProductFrameEligibility != eligibility &&
             !IsBenignMainVRAMDisplayNativeProductEligibilityTransition(eligibility))
    {
        WholeSceneNativeProductEpochValid = false;
        if (WholeSceneNativeProductEpochInvalidReason == 0)
            WholeSceneNativeProductEpochInvalidReason = 1;
    }
    WholeSceneNativeProductLastEligibility = eligibility;

    UpdateWholeSceneTraceFrameSplitState();
}

void GLRenderer2D::RecordWholeSceneNativeProductRenderPath(WholeSceneRenderPath path, int ystart, int yend)
{
    if (ystart >= yend)
        return;

    if (!WholeSceneNativeProductPathInitialized)
    {
        WholeSceneNativeProductPathInitialized = true;
        WholeSceneNativeProductFramePath = path;
    }
    else if (WholeSceneNativeProductFramePath != path)
    {
        WholeSceneNativeProductEpochValid = false;
        if (WholeSceneNativeProductEpochInvalidReason == 0)
            WholeSceneNativeProductEpochInvalidReason = 2;
    }
    WholeSceneNativeProductLastPath = path;

    if (WholeSceneRenderPathUsesFullFrameFinalizer(path))
        WholeSceneNativeProductFinalizerPathSeen = true;

    UpdateWholeSceneTraceFrameSplitState();
}

bool GLRenderer2D::CanFinalizeWholeSceneNativeProducts() const
{
    return WholeSceneNativeProductFinalizerPathSeen &&
           WholeSceneNativeProductEpochValid &&
           WholeSceneNativeProductsFrameComplete;
}

bool GLRenderer2D::AreWholeSceneNativeProductsComplete() const
{
    if (WholeSceneNativeProductValidRows != 192)
        return false;

    for (bool valid : WholeSceneNativeProductRowValid)
    {
        if (!valid)
            return false;
    }

    return true;
}

bool GLRenderer2D::ShouldUseCompositorExactOverlayEndpoints() const
{
    if (WholeSceneNativeChunkAccumulationPasses != 1)
        return false;
    if (WholeSceneCurrentFramePartialComposites != 0)
        return false;

    const u32 blendEffect = (BlendCnt >> 6) & 0x3u;
    if (blendEffect != 1)
        return false;

    const u32 direct3DMask = 1u << 0;
    const u32 twoDLayerMask = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    const u32 blendTarget1 = BlendCnt & 0x3Fu;
    const u32 blendTarget2 = (BlendCnt >> 8) & 0x3Fu;

    return (blendTarget2 & direct3DMask) && (blendTarget1 & twoDLayerMask);
}

void GLRenderer2D::RecordWholeSceneNativeProductChunk(int ystart, int yend)
{
    ystart = std::clamp(ystart, 0, 192);
    yend = std::clamp(yend, 0, 192);
    if (ystart >= yend)
        return;

    WholeSceneNativeChunkAccumulationPasses++;
    for (int y = ystart; y < yend; y++)
    {
        if (WholeSceneNativeProductRowValid[y])
            continue;

        WholeSceneNativeProductRowValid[y] = true;
        WholeSceneNativeProductValidRows++;
    }

    WholeSceneNativeProductsFrameComplete = AreWholeSceneNativeProductsComplete();
    UpdateWholeSceneTraceFrameSplitState();
}

void GLRenderer2D::RecordWholeSceneRenderTrace(WholeSceneRenderPath path,
                                               int ystart,
                                               int yend,
                                               bool highRes3D,
                                               bool linear3D,
                                               bool resolve3D,
                                               GLuint nativeStage3DTex,
                                               bool hybridFragmentationFallback,
                                               bool currentFragmentationFallback)
{
    WholeSceneTrace.Path = path;
    WholeSceneTrace.YStart = ystart;
    WholeSceneTrace.YEnd = yend;
    WholeSceneTrace.OutputTex3D = static_cast<int>(Parent.OutputTex3D);
    WholeSceneTrace.NativeStage3D = static_cast<int>(nativeStage3DTex);
    WholeSceneTrace.HighRes3D = highRes3D;
    WholeSceneTrace.Linear3D = linear3D;
    WholeSceneTrace.Resolve3D = resolve3D;
    WholeSceneTrace.HybridFragmentationFallback = hybridFragmentationFallback;
    WholeSceneTrace.CurrentFragmentationFallback = currentFragmentationFallback;
    WholeSceneTrace.VisibleOBJCapture = BuildVisibleOBJCaptureDebug();
    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    WholeSceneTrace.DirectFinalDisplayConsumer = dispmode == 1;
    WholeSceneTrace.DirectFinalBottomConsumer =
        WholeSceneTrace.DirectFinalDisplayConsumer &&
        Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, ystart, yend);
    const u32 activeCapCnt = GPU.CaptureCnt;
    WholeSceneTrace.ActiveDisplayCaptureDstBank =
        GPU.CaptureEnable ? static_cast<int>((activeCapCnt >> 16) & 0x3u) : -1;
    WholeSceneTrace.ActiveDisplayCaptureDstOffset =
        GPU.CaptureEnable ? static_cast<int>((activeCapCnt >> 18) & 0x3u) : -1;
    WholeSceneTrace.ActiveDisplayCaptureSourceA2D =
        !GPU2D.Num &&
        GPU.CaptureEnable &&
        (((activeCapCnt >> 24) & 0x1u) == 0);
    WholeSceneTrace.ActiveFullDisplayCaptureSourceA =
        WholeSceneTrace.ActiveDisplayCaptureSourceA2D &&
        Parent.IsFullDisplayCaptureFromSourceAOnly(activeCapCnt);
    RecordWholeSceneNativeProductRenderPath(path, ystart, yend);
    UpdateWholeSceneTraceFrameSplitState();
}

void GLRenderer2D::RecordOutputPresentationMasterBrightness(WholeSceneCaptureEffectOwner owner,
                                                            u16 masterBrightness)
{
    if (!IsMasterBrightnessActive(masterBrightness))
        return;

    WholeSceneTrace.OutputPresentationMasterBrightnessApplied = true;
    WholeSceneTrace.OutputPresentationEffectOwner = static_cast<u32>(owner);
    WholeSceneTrace.OutputPresentationEffectState =
        PackedMasterBrightnessTraceState(masterBrightness);
    WholeSceneTrace.OutputPresentationTex = static_cast<int>(OutputTex);
}

HybridSourceDecision GLRenderer2D::ChooseHybridSourceDecision() const
{
    HybridSourceDecisionInputs inputs = {};
    inputs.ConservativeHybridRequested =
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
    inputs.OverlaySuppressedDirect3D =
        inputs.ConservativeHybridRequested &&
        ShouldUseWholeSceneHybridOverlayForSuppressedDirect3DAlphaBlend();
    inputs.ActiveDirect3D =
        !GPU2D.Num && (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0));
    inputs.Foreground2DBaseEnabled = WholeSceneScaleHybridForeground2DBase;

    return ::melonDS::ChooseHybridSourceDecision(inputs);
}

void GLRenderer2D::RecordWholeSceneCaptureSemantics(WholeSceneCaptureBackedPlanRole role,
                                                    WholeSceneCaptureRequestKind request,
                                                    WholeSceneCaptureProductKind product,
                                                    WholeSceneCaptureProofKind proof,
                                                    WholeSceneCaptureRenderAction action)
{
    WholeSceneTrace.CaptureRole = role;
    WholeSceneTrace.CaptureRequestKind = request;
    WholeSceneTrace.CaptureProductKind = product;
    WholeSceneTrace.CaptureProofKind = proof;
    WholeSceneTrace.CaptureRenderAction = action;
}

void GLRenderer2D::AppendWholeSceneModeStatus(std::string& status) const
{
    if (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::HighResCompositor)
        status += "\nMode: high-resolution compositor.";
    else if (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale ||
             WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::OverlayOperatorUpscale ||
             WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale)
    {
        if (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::OverlayOperatorUpscale ||
            WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale)
        {
            status += WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale
                ? "\nMode: conservative hybrid overlay upscale."
                : "\nMode: presentation overlay upscale.";
            if (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale)
            {
                status += "\nConservative hybrid selects high-resolution foreground Direct3D, uses overlay assist for clean 2D-over-3D, and falls back to true native presentation elsewhere.";
                if (WholeSceneScaleHybridWindowEdgeAssist)
                    status += "\nHybrid window-edge assist is enabled for safe window-excluded 2D regions and their visible operator boundaries.";
                if (WholeSceneScaleHybridTarget2AlphaBlendAssist)
                    status += "\nHybrid target2 alpha-blend assist is enabled for Direct3D under native alpha-blended 2D.";
                if (WholeSceneScaleHybridNativeEffectGuard)
                    status += "\nHybrid native-effect guard is enabled near native 2D special-effect regions.";
                if (WholeSceneScaleHybridForeground2DBase)
                    status += "\nHybrid foreground boundary 2D base is enabled for safe Direct3D-absent fallback halos.";
                if (WholeSceneScaleHybridCleanLegacyCandidate)
                    status += "\nHybrid clean native-stack candidate is enabled for simple Direct3D plus 2D frames.";
                if (ShouldUseWholeSceneHybridOverlayForSuppressedDirect3DAlphaBlend())
                    status += "\nHybrid is using presentation overlay because Direct3D is a zero-weight native alpha-blend target.";
                if (IsWholeSceneHybridFragmentationGuardActive())
                    status += "\nHybrid scanline fragmentation guard is active; fragmented chunks use postprocessing upscale.";
                if (IsWholeSceneCurrentFragmentationGuardActive())
                    status += "\nSevere scanline fragmentation guard is active; fragmented chunks use the current renderer.";
                if (IsWholeSceneCaptureBackedHandoffGuardActive())
                    status += "\nCapture-backed 3D/UI handoff guard is active; captured 3D background copies are ignored while live high-resolution 3D and OBJ/UI are composited separately.";
            }
            if (WholeSceneScaleOverlayLegacyUnderlay)
                status += "\nRaw overlay underlay endpoint is enabled.";
        }
        else
            status += "\nMode: postprocessing upscale.";

        if (WholeSceneScaleFinalUpscaleRender3DNative)
            status += "\n3D renders at native resolution.";
        else
        {
            status += "\nHigh-resolution 3D downsample filter: ";
            status += FinalUpscale3DFilterName(WholeSceneScaleFinalUpscale3DFilter);
            status += ".";
            if (WholeSceneScaleFinalUpscale3DCoverageAware)
                status += "\nCoverage-aware 3D resolve is enabled.";
            status += "\nNative-stage 3D semantics: ";
            status += WholeSceneScaleFinalUpscale3DRepresentativeSemantics ? "representative sample." : "visual resolve alpha.";
            status += "\nNative-stage 3D split: ";
            status += WholeSceneScaleFinalUpscale3DSplitSemantics
                ? "visual RGB/coverage separated from material alpha/presence."
                : "single compositor alpha.";
            if (WholeSceneScaleFinalUpscale3DSplitSemantics && WholeSceneScaleFinalUpscale3DSharpenSplitCoverage)
                status += "\nSplit 3D coverage sharpening is enabled.";
        }

        status += "\nNative-stage 3D input: ";
        status += WholeSceneNative3DSourceDescription(
            WholeSceneTrace.Native3DSource,
            WholeSceneScaleFinalUpscale3DSplitSemantics);
    }
    else
        status += "\nMode: native stack upscale.";

    if (CanUseWholeSceneForegroundOverlayPath())
        status += "\nResolved 2D foreground overlay is active.";
    else if (WholeSceneScaleForegroundOverlay)
        status += "\nResolved 2D foreground overlay is bypassed for this screen.";

    bool hasCaptureFlag = false;
    auto addCaptureFlag = [&](const std::string& flag)
    {
        status += hasCaptureFlag ? ", " : "\nCapture flags: ";
        status += flag;
        hasCaptureFlag = true;
    };

    if (!GPU2D.Num && GPU.CaptureEnable)
        addCaptureFlag("active display capture");

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (CanScaleMainEngineVRAMDisplayCaptureSourceA(dispmode))
        addCaptureFlag("main VRAM-display source-A capture");

    for (int layer = 0; layer < 4; layer++)
    {
        if (LayerConfig.uBGConfig[layer].Type >= 7)
            addCaptureFlag("capture-backed BG" + std::to_string(layer));
    }

    int captureBackedOBJCount = 0;
    for (int i = 0; i < NumSprites; i++)
    {
        if (SpriteConfig.uOAM[i].Type >= 3)
            captureBackedOBJCount++;
    }
    if (captureBackedOBJCount > 0)
        addCaptureFlag("capture-backed OBJ x" + std::to_string(captureBackedOBJCount));

    if (!hasCaptureFlag)
        status += "\nCapture flags: none.";
}

void GLRenderer2D::AppendWholeSceneRenderTrace(std::string& status) const
{
    auto yesNo = [](bool value) { return value ? "yes" : "no"; };

    status += "\n\nLast render trace:";
    status += "\n  engine: ";
    status += GPU2D.Num ? "B/sub" : "A/main";
    status += "\n  scanline chunk: ";
    status += std::to_string(WholeSceneTrace.YStart);
    status += "-";
    status += std::to_string(WholeSceneTrace.YEnd);
    status += "\n  actual path: ";
    status += WholeSceneRenderPathName(WholeSceneTrace.Path);
    if (WholeSceneTrace.Path == WholeSceneRenderPath::SourceACaptureReplacement ||
        WholeSceneTrace.Path == WholeSceneRenderPath::CaptureEpochOverlay)
    {
        status += "; replacement mode: ";
        status += SourceACaptureReplacementModeName(WholeSceneTrace.SourceACaptureMode);
        status += "; background epoch ";
        status += std::to_string(WholeSceneTrace.SourceABackgroundEpochSerial);
        status += "; product choice ";
        status += std::to_string(static_cast<int>(WholeSceneTrace.SourceAProductChoice));
    }
    status += "\n  hybrid fragmentation fallback: ";
    status += yesNo(WholeSceneTrace.HybridFragmentationFallback);
    status += "\n  current fragmentation fallback: ";
    status += yesNo(WholeSceneTrace.CurrentFragmentationFallback);
    status += "\n  current-frame partial composites: ";
    status += std::to_string(WholeSceneCurrentFramePartialComposites);
    status += "\n  previous-frame partial composites: ";
    status += std::to_string(WholeScenePreviousFramePartialComposites);
    status += "\n  native chunk accumulation passes: ";
    status += std::to_string(WholeSceneTrace.NativeChunkAccumulationPasses);
    status += "\n  full-frame finalizer passes: ";
    status += std::to_string(WholeSceneTrace.FullFrameFinalizerPasses);
    status += "\n  native product valid rows: ";
    status += std::to_string(WholeSceneTrace.NativeProductValidRows);
    status += "/192";
    status += "\n  native products complete: ";
    status += yesNo(WholeSceneTrace.NativeProductsFrameComplete);
    status += "\n  native product epoch valid: ";
    status += yesNo(WholeSceneTrace.NativeProductEpochValid);
    status += "\n  finalizer-capable path seen: ";
    status += yesNo(WholeSceneTrace.NativeProductFinalizerPathSeen);
    status += "\n  hybrid fragmentation guard frames: ";
    status += std::to_string(WholeSceneHybridFragmentationGuardFrames);
    status += "\n  current fragmentation guard frames: ";
    status += std::to_string(WholeSceneCurrentFragmentationGuardFrames);
    status += "\n  capture-backed handoff guard frames: ";
    status += std::to_string(WholeSceneCaptureBackedHandoffGuardFrames);
    const int handoffSlot = std::min<int>(CaptureBackedHandoff.CurrentSlot, kCaptureBackedHandoffRouteSlots - 1);
    const auto& handoffLatch = CaptureBackedRoute[handoffSlot].HandoffLatchedKey;
    status += "\n  capture-backed handoff snapshot valid: ";
    status += yesNo(CaptureBackedRoute[handoffSlot].Handoff3DValid);
    status += "; route slot ";
    status += std::to_string(handoffSlot);
    status += "; route has captured phase ";
    status += yesNo(CaptureBackedRoute[handoffSlot].HasCapturedPhase);
    status += "; updated this frame ";
    status += yesNo(CaptureBackedHandoff.BackgroundUpdated);
    status += "; reuse reason ";
    status += std::to_string(static_cast<int>(CaptureBackedHandoff.ReuseDecision));
    status += "\n  capture-backed handoff current phase/key: phase ";
    status += std::to_string(static_cast<int>(CaptureBackedHandoff.CurrentKey.Phase));
    status += " bgmode ";
    status += std::to_string(CaptureBackedHandoff.CurrentKey.BGMode);
    status += " layers ";
    status += LayerEnableSummary(CaptureBackedHandoff.CurrentKey.LayerEnable);
    status += " bitmap mask ";
    status += FormatHex(CaptureBackedHandoff.CurrentKey.VisibleBitmapMask, 1);
    status += " final bottom ";
    status += yesNo(CaptureBackedHandoff.CurrentKey.EngineFinalBottom);
    status += "\n  capture-backed handoff latched phase/key: phase ";
    status += std::to_string(static_cast<int>(handoffLatch.Phase));
    status += " bgmode ";
    status += std::to_string(handoffLatch.BGMode);
    status += " layers ";
    status += LayerEnableSummary(handoffLatch.LayerEnable);
    status += " bitmap mask ";
    status += FormatHex(handoffLatch.VisibleBitmapMask, 1);
    status += " final bottom ";
    status += yesNo(handoffLatch.EngineFinalBottom);
    status += "\n  output size: ";
    status += std::to_string(ScreenW);
    status += "x";
    status += std::to_string(ScreenH);
    status += " (scale ";
    status += std::to_string(ScaleFactor);
    status += "x)";
    status += "\n  render submit time: ";
    status += std::to_string(WholeSceneTrace.RenderTimeUS);
    status += " us";
    status += "\n  render-3D-native setting: ";
    status += yesNo(WholeSceneScaleFinalUpscaleRender3DNative);
    status += "\n  high-res 3D path expected: ";
    status += yesNo(WholeSceneTrace.HighRes3D);
    status += "\n  linear native-stage 3D sample: ";
    status += yesNo(WholeSceneTrace.Linear3D);
    status += "\n  ResolveDirect3DToNative called: ";
    status += yesNo(WholeSceneTrace.Resolve3D);
    status += "\n  poison high-res Direct3D source: ";
    status += yesNo(WholeSceneDebugPoison.Source3D);
    status += "\n  poison native 3D resolve: ";
    status += yesNo(WholeSceneDebugPoison.Native3DResolve);
    status += "\n  force native 3D resolve alpha: ";
    status += yesNo(WholeSceneDebugPoison.Native3DResolveAlpha);
    status += "\n  representative native-stage 3D semantics: ";
    status += yesNo(WholeSceneScaleFinalUpscale3DRepresentativeSemantics);
    status += "\n  split native-stage 3D coverage/material alpha: ";
    status += yesNo(WholeSceneScaleFinalUpscale3DSplitSemantics);
    status += "\n  sharpen split 3D coverage: ";
    status += yesNo(WholeSceneScaleFinalUpscale3DSharpenSplitCoverage);
    status += "\n  Parent.OutputTex3D id: ";
    status += std::to_string(WholeSceneTrace.OutputTex3D);
    status += "\n  native-stage Direct3D tex id: ";
    status += std::to_string(WholeSceneTrace.NativeStage3D);
    status += "\n  NativeExactFinal valid: ";
    status += yesNo(WholeSceneTrace.NativeExactFinalValid);
    status += "\n  Physical final native input valid: ";
    status += yesNo(WholeSceneTrace.PhysicalFinalNativeInputValid);
    status += "\n  Native3DResolve valid: ";
    status += yesNo(WholeSceneTrace.Native3DResolveValid);
    status += "\n  Native3DSemantics valid: ";
    status += yesNo(WholeSceneTrace.Native3DSemanticsValid);
    status += "\n  OverlayTrueNativeFinal valid: ";
    status += yesNo(WholeSceneTrace.OverlayTrueFinalValid);
    status += "\n  overlay endpoint final path: ";
    status += WholeSceneOverlayEndpointFinalModeName(WholeSceneTrace.OverlayEndpointFinalMode);
}

void GLRenderer2D::AppendWholeSceneTimingCSVHeader(std::string& header, const char* prefix) const
{
    auto add = [&header, prefix](const auto& name)
    {
        if (!header.empty())
            header += ",";
        header += prefix;
        header += "_";
        header += name;
    };
    auto addStateDebugTraceHeader = [&add](const char* tag)
    {
        add(std::string(tag) + "_state_dirty_events");
        add(std::string(tag) + "_state_dirty_reason_mask");
        add(std::string(tag) + "_state_dirty_dispcnt_diff");
        add(std::string(tag) + "_state_dirty_layer_enable_diff");
        add(std::string(tag) + "_state_dirty_bg0cnt_diff");
        add(std::string(tag) + "_state_dirty_bg1cnt_diff");
        add(std::string(tag) + "_state_dirty_bg2cnt_diff");
        add(std::string(tag) + "_state_dirty_bg3cnt_diff");
        add(std::string(tag) + "_state_dirty_misc_diff_mask");
        add(std::string(tag) + "_full_frame_unsafe");
        add(std::string(tag) + "_full_frame_unsafe_events");
        add(std::string(tag) + "_full_frame_unsafe_reason_mask");
        add(std::string(tag) + "_full_frame_unsafe_first_line");
        add(std::string(tag) + "_full_frame_unsafe_last_line");
        add(std::string(tag) + "_full_frame_unsafe_layer_mask");
        add(std::string(tag) + "_full_frame_unsafe_dispcnt_diff");
        add(std::string(tag) + "_full_frame_unsafe_layer_enable_diff");
        add(std::string(tag) + "_full_frame_unsafe_bg0cnt_diff");
        add(std::string(tag) + "_full_frame_unsafe_bg1cnt_diff");
        add(std::string(tag) + "_full_frame_unsafe_bg2cnt_diff");
        add(std::string(tag) + "_full_frame_unsafe_bg3cnt_diff");
        add(std::string(tag) + "_full_frame_unsafe_misc_diff_mask");
    };

    add("eligibility");
    add("path");
    add("current_path_reason");
    add("mode");
    add("frag_policy");
    add("hybrid_frag_guard");
    add("hybrid_frag_fallback");
    add("hybrid_frag_guard_frames");
    add("current_frag_guard");
    add("current_frag_fallback");
    add("current_frag_guard_frames");
    add("route_guard_active");
    add("route_guard_frames");
    add("route_slot");
    add("route_has_captured_phase");
    add("route_handoff_snapshot_valid");
    add("route_handoff_snapshot_updated");
    add("route_current_source_phase");
    add("route_latched_source_phase");
    add("route_reuse_reason");
    add("route_current_screen_swap");
    add("route_latched_screen_swap");
    add("route_current_final_bottom");
    add("route_latched_final_bottom");
    add("route_current_bg_mode");
    add("route_latched_bg_mode");
    add("route_current_layer_enable");
    add("route_latched_layer_enable");
    add("route_current_bitmap_mask");
    add("route_latched_bitmap_mask");
    add("route_current_bg_upload_rows");
    add("route_latched_frame_age");
    add("frame_partial_count");
    add("prev_frame_partial_count");
    add("native_chunk_accum_passes");
    add("full_frame_finalizer_passes");
    add("native_product_valid_rows");
    add("native_products_complete");
    add("native_product_epoch_valid");
    add("native_product_epoch_invalid_reason");
    add("native_product_frame_eligibility");
    add("native_product_last_eligibility");
    add("native_product_frame_path");
    add("native_product_last_path");
    add("native_product_finalizer_path_seen");
    add("source_a_resolution_mode");
    add("source_a_request_background_epoch_serial");
    add("source_a_resolved_background_source");
    add("source_a_resolved_background_epoch_serial");
    add("source_a_route_product_ref_background_epoch_serial");
    add("source_a_route_product_ref_source_3d_serial");
    add("source_a_route_product_ref_source_3d_scene_hash");
    add("source_a_route_product_ref_capture_event_serial");
    add("source_a_route_product_ref_capture_presentation_hash");
    add("source_a_route_product_ref_current_presentation_hash");
    add("source_a_route_product_ref_stable_frames");
    add("source_a_route_product_ref_class");
    add("route_product_lookup_attempted");
    add("route_product_lookup_success");
    add("route_product_lookup_result_source");
    add("route_product_lookup_slot");
    add("route_product_lookup_event_serial");
    add("route_product_lookup_capture_bank");
    add("route_product_lookup_capture_presentation_hash");
    add("route_product_lookup_source_3d_serial");
    add("route_product_lookup_source_3d_scene_hash");
    add("route_product_lookup_event_product_valid");
    add("route_product_lookup_event_product_captured_serial");
    add("route_product_lookup_event_product_capture_bank");
    add("route_product_lookup_event_product_current_presentation_hash");
    add("route_product_lookup_event_product_source_3d_serial");
    add("route_product_lookup_event_product_source_3d_scene_hash");
    add("route_product_lookup_product_valid");
    add("route_product_lookup_product_captured_serial");
    add("route_product_lookup_product_capture_bank");
    add("route_product_lookup_product_current_presentation_hash");
    add("route_product_lookup_product_source_3d_serial");
    add("route_product_lookup_product_source_3d_scene_hash");
    add("route_event_publish_attempted");
    add("route_event_publish_success");
    add("route_event_publish_reject_reason");
    add("route_event_publish_slot");
    add("route_event_publish_pending_event_serial");
    add("route_event_publish_pending_capture_bank");
    add("route_event_publish_pending_presentation_hash");
    add("route_event_publish_pending_source_3d_serial");
    add("route_event_publish_pending_source_3d_scene_hash");
    add("route_event_publish_product_valid");
    add("route_event_publish_product_tex_valid");
    add("route_event_publish_event_product_tex_valid");
    add("route_event_publish_event_product_fb_valid");
    add("route_event_publish_product_background_epoch_serial");
    add("route_event_publish_product_source_3d_serial");
    add("route_event_publish_product_source_3d_scene_hash");
    add("route_event_publish_product_captured_event_serial");
    add("route_event_publish_product_capture_bank");
    add("route_event_publish_product_capture_presentation_hash");
    add("route_event_publish_product_current_presentation_hash");
    add("route_event_publish_product_class");
    add("source_a_product_choice_reason");
    add("capture_policy_authority");
    add("capture_policy_role");
    add("capture_policy_request_kind");
    add("capture_policy_product_kind");
    add("capture_policy_proof_kind");
    add("capture_policy_render_action");
    add("capture_repr_request_role");
    add("capture_repr_physical_screen");
    add("capture_repr_source_engine");
    add("capture_repr_product_class");
    add("capture_repr_selected_product_kind");
    add("capture_repr_selected_event_serial");
    add("capture_repr_content_proof_match");
    add("capture_repr_route_proof_match");
    add("capture_repr_effect_owner");
    add("capture_repr_effect_state");
    add("capture_repr_effect_compatible");
    add("capture_repr_product_use_accepted");
    add("capture_repr_presentation_hash_match");
    add("capture_repr_stored_effect_owner");
    add("capture_repr_stored_effect_state");
    add("capture_repr_consume_effect_owner");
    add("capture_repr_consume_effect_state");
    add("capture_repr_effect_action");
    add("capture_repr_effect_phase_incompatible");
    add("capture_repr_final_pass_effect_owner");
    add("capture_repr_apply_effect_on_blit");
    add("capture_repr_output_brightness_applied");
    add("capture_repr_output_effect_owner");
    add("capture_repr_output_effect_state");
    add("capture_repr_output_tex");
    add("capture_repr_would_represent_raw_product");
    add("capture_repr_fallback_class");
    add("source_a_request_capture_presentation_hash");
    add("source_a_request_current_presentation_hash");
    add("source_a_chosen_product_tex");
    add("source_a_chosen_product_capture_bank");
    add("source_a_chosen_product_background_epoch_serial");
    add("source_a_chosen_product_source_3d_serial");
    add("source_a_chosen_product_source_3d_scene_hash");
    add("source_a_chosen_product_capture_event_serial");
    add("source_a_chosen_product_capture_presentation_hash");
    add("source_a_chosen_product_current_presentation_hash");
    add("source_a_chosen_product_kind");
    add("source_a_chosen_product_render_action");
    add("source_a_chosen_product_class");
    add("source_a_full_product_candidate_presentation_match");
    add("source_a_full_product_candidate_capture_bank");
    add("source_a_full_product_candidate_tex");
    add("source_a_full_product_candidate_event_valid");
    add("source_a_full_product_candidate_event_serial");
    add("source_a_full_product_candidate_event_source_3d_serial");
    add("source_a_full_product_candidate_event_source_3d_scene_hash");
    add("source_a_full_product_candidate_event_source_presentation_hash");
    add("source_a_full_product_candidate_event_source_kind");
    add("source_a_full_product_candidate_event_product_mask");
    add("source_a_full_product_candidate_event_reject_reason");
    add("source_a_full_product_candidate_event_dst_block");
    add("source_a_full_product_candidate_event_dst_offset");
    add("source_a_full_product_candidate_event_source_obj");
    add("source_a_full_product_candidate_event_screen_swap");
    add("source_a_full_product_candidate_event_main_final_bottom");
    add("direct_final_display_consumer");
    add("direct_final_bottom_consumer");
    add("active_display_capture_source_a_2d");
    add("active_full_display_capture_source_a");
    add("active_display_capture_dst_bank");
    add("active_display_capture_dst_offset");
    add("algorithm");
    add("scale");
    add("width");
    add("height");
    add("ystart");
    add("yend");
    add("submit_us");
    add("highres_3d");
    add("linear_3d");
    add("resolve_3d");
    add("native_exact_final");
    add("physical_final_native_input");
    add("native_3d_resolve");
    add("native_3d_semantics");
    add("overlay_true_final");
    add("overlay_endpoint_final_mode");
    add("dispcnt");
    add("layer_enable");
    add("obj_enable");
    add("blendcnt");
    add("bldalpha_eva");
    add("bldalpha_evb");
    add("bldy_evy");
    add("palette_hash");
    add("visible_bitmap_bg_mask");
    add("source_a_only_capture_bg_mask");
    add("full_source_a_capture_bg_mask");
    add("visible_capture_event_serial");
    add("visible_capture_event_match");
    add("visible_capture_event_source_kind");
    add("visible_capture_event_product_mask");
    add("visible_capture_event_reject_reason");
    add("visible_obj_capture_found");
    add("visible_obj_capture_bank");
    add("visible_obj_capture_mixed_bank");
    add("visible_obj_capture_current_source_a_only");
    add("visible_obj_capture_current_full_source_a");
    add("visible_obj_capture_fullscreen");
    add("visible_obj_capture_full_width_strip");
    add("visible_obj_capture_product_available");
    add("visible_obj_capture_event_valid");
    add("visible_obj_capture_event_source_obj");
    add("visible_obj_capture_event_product_mask");
    add("visible_obj_capture_event_reject_reason");
    add("visible_obj_capture_event_serial");
    add("visible_obj_capture_type3_count");
    add("visible_obj_capture_type4_count");
    add("visible_obj_capture_sprite_count");
    add("visible_obj_capture_non_capture_sprite_count");
    add("visible_obj_capture_coverage_area");
    add("visible_obj_capture_min_x");
    add("visible_obj_capture_min_y");
    add("visible_obj_capture_max_x");
    add("visible_obj_capture_max_y");
    add("visible_obj_capture_reject_reason");
    add("bg0_type");
    add("bg1_type");
    add("bg2_type");
    add("bg3_type");
    add("bg0_tile");
    add("bg1_tile");
    add("bg2_tile");
    add("bg3_tile");
    add("streaming_bitmap_candidate");
    add("frame_layer_dirty_events");
    add("frame_layer_dirty_mask");
    add("frame_bg_upload_calls");
    add("frame_bg_upload_ranges");
    add("frame_bg_upload_rows");
    add("frame_layer_prerender_calls");
    add("frame_layer_prerender_mask");
    add("frame_bitmap_prerender_mask");
    addStateDebugTraceHeader("frame");
    add("frame_register_layer_dirty_mask");
    add("frame_vram_layer_dirty_mask");
    add("frame_palette_layer_dirty_mask");
    add("frame_deferred_layer_dirty_mask");
    add("frame_inactive_deferred_layer_dirty_mask");
    add("frame_flat_vram_capture_sync_layer_mask");
    add("frame_covered_bitmap_mask");
    add("frame_contributing_layer_mask");
    add("frame_visible_bitmap_dirty_mask");
    add("frame_visible_bitmap_vram_dirty_mask");
    add("frame_visible_bitmap_dirty_before_line_mask");
    add("frame_visible_bitmap_dirty_after_line_mask");
    add("frame_visible_bitmap_dirty_crosses_line_mask");
    add("frame_visible_bitmap_row_limited_prerender_mask");
    add("frame_visible_bitmap_covered_dirty_deferred_mask");
    add("frame_visible_bitmap_dirty_first_row");
    add("frame_visible_bitmap_dirty_last_row");
    add("frame_bg0_visible_bitmap_dirty_first_row");
    add("frame_bg0_visible_bitmap_dirty_last_row");
    add("frame_bg1_visible_bitmap_dirty_first_row");
    add("frame_bg1_visible_bitmap_dirty_last_row");
    add("frame_bg2_visible_bitmap_dirty_first_row");
    add("frame_bg2_visible_bitmap_dirty_last_row");
    add("frame_bg3_visible_bitmap_dirty_first_row");
    add("frame_bg3_visible_bitmap_dirty_last_row");
    add("frame_bg0_visible_bitmap_row_limited_first_row");
    add("frame_bg0_visible_bitmap_row_limited_last_row");
    add("frame_bg1_visible_bitmap_row_limited_first_row");
    add("frame_bg1_visible_bitmap_row_limited_last_row");
    add("frame_bg2_visible_bitmap_row_limited_first_row");
    add("frame_bg2_visible_bitmap_row_limited_last_row");
    add("frame_bg3_visible_bitmap_row_limited_first_row");
    add("frame_bg3_visible_bitmap_row_limited_last_row");
    add("prev_layer_dirty_events");
    add("prev_layer_dirty_mask");
    add("prev_bg_upload_calls");
    add("prev_bg_upload_ranges");
    add("prev_bg_upload_rows");
    add("prev_layer_prerender_calls");
    add("prev_layer_prerender_mask");
    add("prev_bitmap_prerender_mask");
    addStateDebugTraceHeader("prev");
    add("prev_register_layer_dirty_mask");
    add("prev_vram_layer_dirty_mask");
    add("prev_palette_layer_dirty_mask");
    add("prev_deferred_layer_dirty_mask");
    add("prev_inactive_deferred_layer_dirty_mask");
    add("prev_flat_vram_capture_sync_layer_mask");
    add("prev_covered_bitmap_mask");
    add("prev_contributing_layer_mask");
    add("prev_visible_bitmap_dirty_mask");
    add("prev_visible_bitmap_vram_dirty_mask");
    add("prev_visible_bitmap_dirty_before_line_mask");
    add("prev_visible_bitmap_dirty_after_line_mask");
    add("prev_visible_bitmap_dirty_crosses_line_mask");
    add("prev_visible_bitmap_row_limited_prerender_mask");
    add("prev_visible_bitmap_covered_dirty_deferred_mask");
    add("prev_visible_bitmap_dirty_first_row");
    add("prev_visible_bitmap_dirty_last_row");
    add("prev_bg0_visible_bitmap_dirty_first_row");
    add("prev_bg0_visible_bitmap_dirty_last_row");
    add("prev_bg1_visible_bitmap_dirty_first_row");
    add("prev_bg1_visible_bitmap_dirty_last_row");
    add("prev_bg2_visible_bitmap_dirty_first_row");
    add("prev_bg2_visible_bitmap_dirty_last_row");
    add("prev_bg3_visible_bitmap_dirty_first_row");
    add("prev_bg3_visible_bitmap_dirty_last_row");
    add("prev_bg0_visible_bitmap_row_limited_first_row");
    add("prev_bg0_visible_bitmap_row_limited_last_row");
    add("prev_bg1_visible_bitmap_row_limited_first_row");
    add("prev_bg1_visible_bitmap_row_limited_last_row");
    add("prev_bg2_visible_bitmap_row_limited_first_row");
    add("prev_bg2_visible_bitmap_row_limited_last_row");
    add("prev_bg3_visible_bitmap_row_limited_first_row");
    add("prev_bg3_visible_bitmap_row_limited_last_row");
    AppendWholeSceneUpdateTimingCSVHeader(header, prefix);
}

void GLRenderer2D::AppendWholeSceneTimingCSVRow(std::string& row) const
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

    addInt(WholeSceneScaleState);
    addInt(WholeSceneTrace.Path);
    addInt(WholeSceneTrace.CurrentReason);
    addInt(WholeSceneScaleMode);
    addInt(WholeSceneScaleFragmentationFallback);
    addInt(IsWholeSceneHybridFragmentationGuardActive());
    addInt(WholeSceneTrace.HybridFragmentationFallback);
    addInt(WholeSceneHybridFragmentationGuardFrames);
    addInt(IsWholeSceneCurrentFragmentationGuardActive());
    addInt(WholeSceneTrace.CurrentFragmentationFallback);
    addInt(WholeSceneCurrentFragmentationGuardFrames);
    addInt(IsWholeSceneCaptureBackedHandoffGuardActive());
    addInt(WholeSceneCaptureBackedHandoffGuardFrames);
    const int handoffSlot = std::min<int>(CaptureBackedHandoff.CurrentSlot, kCaptureBackedHandoffRouteSlots - 1);
    const auto& handoffLatch = CaptureBackedRoute[handoffSlot].HandoffLatchedKey;
    addInt(handoffSlot);
    addInt(CaptureBackedRoute[handoffSlot].HasCapturedPhase);
    addInt(CaptureBackedRoute[handoffSlot].Handoff3DValid);
    addInt(CaptureBackedHandoff.BackgroundUpdated);
    addInt(CaptureBackedHandoff.CurrentKey.Phase);
    addInt(handoffLatch.Phase);
    addInt(CaptureBackedHandoff.ReuseDecision);
    addInt(CaptureBackedHandoff.CurrentKey.ScreenSwap);
    addInt(handoffLatch.ScreenSwap);
    addInt(CaptureBackedHandoff.CurrentKey.EngineFinalBottom);
    addInt(handoffLatch.EngineFinalBottom);
    addInt(CaptureBackedHandoff.CurrentKey.BGMode);
    addInt(handoffLatch.BGMode);
    addInt(CaptureBackedHandoff.CurrentKey.LayerEnable);
    addInt(handoffLatch.LayerEnable);
    addInt(CaptureBackedHandoff.CurrentKey.VisibleBitmapMask);
    addInt(handoffLatch.VisibleBitmapMask);
    addInt(CaptureBackedHandoff.CurrentKey.BGUploadRows);
    addInt(CaptureBackedRoute[handoffSlot].Handoff3DValid
        ? CaptureBackedHandoff.CurrentKey.FrameSerial - handoffLatch.FrameSerial
        : 0);
    addInt(WholeSceneCurrentFramePartialComposites);
    addInt(WholeScenePreviousFramePartialComposites);
    addInt(WholeSceneTrace.NativeChunkAccumulationPasses);
    addInt(WholeSceneTrace.FullFrameFinalizerPasses);
    addInt(WholeSceneTrace.NativeProductValidRows);
    addInt(WholeSceneTrace.NativeProductsFrameComplete);
    addInt(WholeSceneTrace.NativeProductEpochValid);
    addInt(WholeSceneTrace.NativeProductEpochInvalidReason);
    addInt(WholeSceneTrace.NativeProductFrameEligibility);
    addInt(WholeSceneTrace.NativeProductLastEligibility);
    addInt(WholeSceneTrace.NativeProductFramePath);
    addInt(WholeSceneTrace.NativeProductLastPath);
    addInt(WholeSceneTrace.NativeProductFinalizerPathSeen);
    addInt(WholeSceneTrace.SourceACaptureMode);
    addU64(WholeSceneTrace.SourceABackgroundEpochSerial);
    addInt(WholeSceneTrace.EffectiveSourceABackgroundSource);
    addU64(WholeSceneTrace.EffectiveSourceABackgroundEpochSerial);
    addU64(WholeSceneTrace.SourceARouteProductBackgroundEpochSerial);
    addU64(WholeSceneTrace.SourceARouteProductSource3DSerial);
    addU64(WholeSceneTrace.SourceARouteProductSource3DSceneHash);
    addU64(WholeSceneTrace.SourceARouteProductCapturedEventSerial);
    addU64(WholeSceneTrace.SourceARouteProductCapturePresentationHash);
    addU64(WholeSceneTrace.SourceARouteProductCurrentPresentationHash);
    addU64(WholeSceneTrace.SourceARouteProductStableFrames);
    addInt(WholeSceneTrace.SourceARouteProductPresentationClass);
    addInt(WholeSceneTrace.RouteProductLookupAttempted);
    addInt(WholeSceneTrace.RouteProductLookupSuccess);
    addInt(WholeSceneTrace.RouteProductLookupResultSource);
    addInt(WholeSceneTrace.RouteProductLookupSlot);
    addU64(WholeSceneTrace.RouteProductLookupEventSerial);
    addU64(WholeSceneTrace.RouteProductLookupCaptureBank);
    addU64(WholeSceneTrace.RouteProductLookupCapturePresentationHash);
    addU64(WholeSceneTrace.RouteProductLookupSource3DSerial);
    addU64(WholeSceneTrace.RouteProductLookupSource3DSceneHash);
    addInt(WholeSceneTrace.RouteProductLookupEventProductValid);
    addU64(WholeSceneTrace.RouteProductLookupEventProductCapturedSerial);
    addU64(WholeSceneTrace.RouteProductLookupEventProductCaptureBank);
    addU64(WholeSceneTrace.RouteProductLookupEventProductCurrentPresentationHash);
    addU64(WholeSceneTrace.RouteProductLookupEventProductSource3DSerial);
    addU64(WholeSceneTrace.RouteProductLookupEventProductSource3DSceneHash);
    addInt(WholeSceneTrace.RouteProductLookupProductValid);
    addU64(WholeSceneTrace.RouteProductLookupProductCapturedSerial);
    addU64(WholeSceneTrace.RouteProductLookupProductCaptureBank);
    addU64(WholeSceneTrace.RouteProductLookupProductCurrentPresentationHash);
    addU64(WholeSceneTrace.RouteProductLookupProductSource3DSerial);
    addU64(WholeSceneTrace.RouteProductLookupProductSource3DSceneHash);
    addInt(WholeSceneTrace.RouteEventPublishAttempted);
    addInt(WholeSceneTrace.RouteEventPublishSuccess);
    addInt(WholeSceneTrace.RouteEventPublishRejectReason);
    addInt(WholeSceneTrace.RouteEventPublishSlot);
    addU64(WholeSceneTrace.RouteEventPublishPendingEventSerial);
    addU64(WholeSceneTrace.RouteEventPublishPendingCaptureBank);
    addU64(WholeSceneTrace.RouteEventPublishPendingPresentationHash);
    addU64(WholeSceneTrace.RouteEventPublishPendingSource3DSerial);
    addU64(WholeSceneTrace.RouteEventPublishPendingSource3DSceneHash);
    addInt(WholeSceneTrace.RouteEventPublishProductValid);
    addInt(WholeSceneTrace.RouteEventPublishProductTexValid);
    addInt(WholeSceneTrace.RouteEventPublishEventProductTexValid);
    addInt(WholeSceneTrace.RouteEventPublishEventProductFBValid);
    addU64(WholeSceneTrace.RouteEventPublishProductBackgroundEpochSerial);
    addU64(WholeSceneTrace.RouteEventPublishProductSource3DSerial);
    addU64(WholeSceneTrace.RouteEventPublishProductSource3DSceneHash);
    addU64(WholeSceneTrace.RouteEventPublishProductCapturedEventSerial);
    addU64(WholeSceneTrace.RouteEventPublishProductCaptureBank);
    addU64(WholeSceneTrace.RouteEventPublishProductCapturePresentationHash);
    addU64(WholeSceneTrace.RouteEventPublishProductCurrentPresentationHash);
    addInt(WholeSceneTrace.RouteEventPublishProductPresentationClass);
    addInt(WholeSceneTrace.SourceAProductChoice);
    addInt(WholeSceneTrace.CaptureAuthority);
    addInt(WholeSceneTrace.CaptureRole);
    addInt(WholeSceneTrace.CaptureRequestKind);
    addInt(WholeSceneTrace.CaptureProductKind);
    addInt(WholeSceneTrace.CaptureProofKind);
    addInt(WholeSceneTrace.CaptureRenderAction);
    bool finalScreenSwap = GPU.ScreenSwap;
    const bool finalScreenSwapKnown =
        Parent.GetFinalPassScreenSwapForRange(WholeSceneTrace.YStart,
                                              WholeSceneTrace.YEnd,
                                              finalScreenSwap);
    const int physicalScreen =
        !finalScreenSwapKnown ? 0 :
        GPU2D.Num == 0 ? (finalScreenSwap ? 1 : 2) :
                         (finalScreenSwap ? 2 : 1);
    const u16 routeMasterBrightness = GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    const int routeBrightMode = (routeMasterBrightness >> 14) & 0x3;
    const int routeBrightFactor = std::min<int>(routeMasterBrightness & 0x1F, 16);
    const int routeEffectState = (routeBrightMode << 8) | routeBrightFactor;
    const bool routeEffectActive =
        (routeBrightMode == 1 || routeBrightMode == 2) &&
        routeBrightFactor > 0;
    const auto representationProductClass =
        CaptureRepresentationProductClassForTrace(WholeSceneTrace);
    const bool representationContentProof =
        CaptureRepresentationHasContentProof(WholeSceneTrace);
    const bool representationRouteProof =
        CaptureRepresentationHasRouteProof(WholeSceneTrace);
    const auto representationEffectOwner =
        CaptureRepresentationEffectOwnerForTrace(WholeSceneTrace);
    const bool selectedProductPresentationCompatible =
        CanUseCaptureProductAsPresented(
            representationProductClass,
            WholeSceneTrace.SourceAChosenProductCapturePresentationHash,
            WholeSceneTrace.SourceAChosenProductCurrentPresentationHash);
    const auto representationEffectCompatible =
        representationProductClass == WholeSceneCaptureProductPresentationClass::RawContent
            ? CaptureRepresentationEffectCompatibility::Compatible
            : representationProductClass == WholeSceneCaptureProductPresentationClass::AlreadyPresented
                ? (selectedProductPresentationCompatible
                    ? CaptureRepresentationEffectCompatibility::Compatible
                    : CaptureRepresentationEffectCompatibility::UnknownNeedsProof)
                : CaptureRepresentationEffectCompatibility::IncompatibleOrNoProduct;
    const bool wouldRepresentRawProduct =
        representationProductClass == WholeSceneCaptureProductPresentationClass::RawContent &&
        representationContentProof &&
        routeEffectActive;
    const u64 selectedProductEventSerial =
        WholeSceneTrace.SourceAChosenProductCaptureEventSerial != 0
            ? WholeSceneTrace.SourceAChosenProductCaptureEventSerial
            : WholeSceneTrace.SourceAFullProductEventSerial;
    addInt(WholeSceneTrace.CaptureRole);
    addInt(physicalScreen);
    addInt(GPU2D.Num ? 1 : 0);
    addInt(representationProductClass);
    addInt(WholeSceneTrace.CaptureProductKind);
    addU64(selectedProductEventSerial);
    addInt(representationContentProof);
    addInt(representationRouteProof);
    addInt(representationEffectOwner);
    addInt(routeEffectState);
    addInt(representationEffectCompatible);
    addInt(WholeSceneTrace.CaptureProductUseAccepted);
    addInt(WholeSceneTrace.CaptureProductPresentationHashMatch);
    addInt(WholeSceneTrace.CaptureProductStoredEffectOwner);
    addInt(WholeSceneTrace.CaptureProductStoredEffectState);
    addInt(WholeSceneTrace.CaptureProductConsumeEffectOwner);
    addInt(WholeSceneTrace.CaptureProductConsumeEffectState);
    addInt(WholeSceneTrace.CaptureProductEffectAction);
    addInt(WholeSceneTrace.CaptureProductEffectPhaseIncompatible);
    addInt(WholeSceneTrace.CaptureProductFinalPassEffectOwner);
    addInt(WholeSceneTrace.CaptureProductApplyEffectOnBlit);
    addInt(WholeSceneTrace.OutputPresentationMasterBrightnessApplied);
    addInt(WholeSceneTrace.OutputPresentationEffectOwner);
    addInt(WholeSceneTrace.OutputPresentationEffectState);
    addInt(WholeSceneTrace.OutputPresentationTex);
    addInt(wouldRepresentRawProduct);
    addInt(CaptureRepresentationFallbackForTrace(WholeSceneTrace));
    addU64(WholeSceneTrace.SourceACapturePresentationHash);
    addU64(WholeSceneTrace.SourceACurrentPresentationHash);
    addInt(WholeSceneTrace.SourceAChosenProductTex);
    addInt(WholeSceneTrace.SourceAChosenProductCaptureBank);
    addU64(WholeSceneTrace.SourceAChosenProductBackgroundEpochSerial);
    addU64(WholeSceneTrace.SourceAChosenProductSource3DSerial);
    addU64(WholeSceneTrace.SourceAChosenProductSource3DSceneHash);
    addU64(WholeSceneTrace.SourceAChosenProductCaptureEventSerial);
    addU64(WholeSceneTrace.SourceAChosenProductCapturePresentationHash);
    addU64(WholeSceneTrace.SourceAChosenProductCurrentPresentationHash);
    addInt(WholeSceneTrace.SourceAChosenProductKind);
    addInt(WholeSceneTrace.SourceAChosenProductRenderAction);
    addInt(WholeSceneTrace.SourceAChosenProductPresentationClass);
    addInt(WholeSceneTrace.SourceAFullProductKeyMatch);
    addInt(WholeSceneTrace.SourceAFullProductCaptureBank);
    addInt(WholeSceneTrace.SourceAFullProductTex);
    addInt(WholeSceneTrace.SourceAFullProductEventValid);
    addU64(WholeSceneTrace.SourceAFullProductEventSerial);
    addU64(WholeSceneTrace.SourceAFullProductEventSource3DSerial);
    addU64(WholeSceneTrace.SourceAFullProductEventSource3DSceneHash);
    addU64(WholeSceneTrace.SourceAFullProductEventSourcePresentationHash);
    addInt(WholeSceneTrace.SourceAFullProductEventSourceKind);
    addInt(WholeSceneTrace.SourceAFullProductEventProductMask);
    addInt(WholeSceneTrace.SourceAFullProductEventRejectReason);
    addInt(WholeSceneTrace.SourceAFullProductEventDstBlock);
    addInt(WholeSceneTrace.SourceAFullProductEventDstOffset);
    addInt(WholeSceneTrace.SourceAFullProductEventSourceOBJ);
    addInt(WholeSceneTrace.SourceAFullProductEventScreenSwap);
    addInt(WholeSceneTrace.SourceAFullProductEventMainFinalBottom);
    addInt(WholeSceneTrace.DirectFinalDisplayConsumer);
    addInt(WholeSceneTrace.DirectFinalBottomConsumer);
    addInt(WholeSceneTrace.ActiveDisplayCaptureSourceA2D);
    addInt(WholeSceneTrace.ActiveFullDisplayCaptureSourceA);
    addInt(WholeSceneTrace.ActiveDisplayCaptureDstBank);
    addInt(WholeSceneTrace.ActiveDisplayCaptureDstOffset);
    addInt(WholeSceneScaleAlgorithm);
    addInt(ScaleFactor);
    addInt(ScreenW);
    addInt(ScreenH);
    addInt(WholeSceneTrace.YStart);
    addInt(WholeSceneTrace.YEnd);
    addU64(WholeSceneTrace.RenderTimeUS);
    addInt(WholeSceneTrace.HighRes3D);
    addInt(WholeSceneTrace.Linear3D);
    addInt(WholeSceneTrace.Resolve3D);
    addInt(WholeSceneTrace.NativeExactFinalValid);
    addInt(WholeSceneTrace.PhysicalFinalNativeInputValid);
    addInt(WholeSceneTrace.Native3DResolveValid);
    addInt(WholeSceneTrace.Native3DSemanticsValid);
    addInt(WholeSceneTrace.OverlayTrueFinalValid);
    addInt(WholeSceneTrace.OverlayEndpointFinalMode);
    addInt(DispCnt);
    addInt(LayerEnable);
    addInt(OBJEnable);
    addInt(BlendCnt);
    addInt(GPU2D.EVA);
    addInt(GPU2D.EVB);
    addInt(GPU2D.EVY);
    {
        // Palette-side fades change no traced register or presentation hash;
        // this hash is the only trace that can see them.
        u32 paletteHash = 2166136261u;
        const u8* palette = &GPU.Palette[GPU2D.Num ? 0x400 : 0];
        for (int i = 0; i < 0x400; i++)
            paletteHash = (paletteHash ^ palette[i]) * 16777619u;
        addU64(paletteHash);
    }
    addInt(VisibleBitmapBGLayerMask());
    addInt(VisibleSourceAOnlyFullDisplayCaptureBGLayerMask());
    addInt(VisibleFullDisplayCaptureFromSourceABGLayerMask());
    u64 visibleCaptureEventSerial = 0;
    u32 visibleCaptureEventSourceKind = 0;
    u32 visibleCaptureEventProductMask = 0;
    u32 visibleCaptureEventRejectReason = 0;
    bool visibleCaptureEventMatch = false;
    int visibleCaptureEventLayers = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((LayerEnable & (1 << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        if (cfg.Type != 8)
            continue;

        visibleCaptureEventLayers++;
        if (visibleCaptureEventLayers == 1)
        {
            visibleCaptureEventMatch =
                Parent.GetHighResDisplayCaptureEventForBG(cfg.Type,
                                                           cfg.TileOffset,
                                                           visibleCaptureEventSerial,
                                                           visibleCaptureEventSourceKind,
                                                           visibleCaptureEventProductMask,
                                                           visibleCaptureEventRejectReason);
        }
        else
        {
            visibleCaptureEventMatch = false;
            visibleCaptureEventRejectReason = 9;
        }
    }
    addU64(visibleCaptureEventSerial);
    addInt(visibleCaptureEventMatch);
    addInt(visibleCaptureEventSourceKind);
    addInt(visibleCaptureEventProductMask);
    addInt(visibleCaptureEventRejectReason);
    const auto& visibleOBJCapture = WholeSceneTrace.VisibleOBJCapture;
    addInt(visibleOBJCapture.Found);
    addInt(visibleOBJCapture.Bank);
    addInt(visibleOBJCapture.MixedBank);
    addInt(visibleOBJCapture.CurrentSourceAOnly);
    addInt(visibleOBJCapture.CurrentFullSourceA);
    addInt(visibleOBJCapture.FullScreen);
    addInt(visibleOBJCapture.FullWidthTopStrip);
    addInt(visibleOBJCapture.ProductAvailable);
    addInt(visibleOBJCapture.EventValid);
    addInt(visibleOBJCapture.EventSourceOBJ);
    addInt(visibleOBJCapture.EventProductMask);
    addInt(visibleOBJCapture.EventRejectReason);
    addU64(visibleOBJCapture.EventSerial);
    addInt(visibleOBJCapture.Type3Count);
    addInt(visibleOBJCapture.Type4Count);
    addInt(visibleOBJCapture.CaptureSpriteCount);
    addInt(visibleOBJCapture.NonCaptureSpriteCount);
    addInt(visibleOBJCapture.CoverageArea);
    addInt(visibleOBJCapture.MinX);
    addInt(visibleOBJCapture.MinY);
    addInt(visibleOBJCapture.MaxX);
    addInt(visibleOBJCapture.MaxY);
    addInt(visibleOBJCapture.RejectReason);
    for (int layer = 0; layer < 4; layer++)
        addInt(LayerConfig.uBGConfig[layer].Type);
    for (int layer = 0; layer < 4; layer++)
        addU64(LayerConfig.uBGConfig[layer].TileOffset);
    addInt(IsWholeSceneStreamingBitmapFragmentationCandidate());
    addInt(WholeSceneCurrentUpdateDebugTrace.LayerDirtyEvents);
    addInt(WholeSceneCurrentUpdateDebugTrace.LayerDirtyMask);
    addInt(WholeSceneCurrentUpdateDebugTrace.BGUploadCalls);
    addInt(WholeSceneCurrentUpdateDebugTrace.BGUploadRangeCount);
    addInt(WholeSceneCurrentUpdateDebugTrace.BGUploadRows);
    addInt(WholeSceneCurrentUpdateDebugTrace.LayerPrerenderCalls);
    addInt(WholeSceneCurrentUpdateDebugTrace.LayerPrerenderMask);
    addInt(WholeSceneCurrentUpdateDebugTrace.LayerPrerenderBitmapMask);
    auto addStateDebugTrace = [&](const WholeSceneUpdateDebugTrace& trace)
    {
        addInt(trace.StateDirtyEvents);
        addInt(trace.StateDirtyReasonMask);
        addInt(trace.StateDirtyDispCntDiff);
        addInt(trace.StateDirtyLayerEnableDiff);
        for (int layer = 0; layer < 4; layer++)
            addInt(trace.StateDirtyBGCntDiff[layer]);
        addInt(trace.StateDirtyMiscDiffMask);
        addInt(trace.FullFrameUnsafeEvents > 0);
        addInt(trace.FullFrameUnsafeEvents);
        addInt(trace.FullFrameUnsafeReasonMask);
        addInt(trace.FullFrameUnsafeFirstLine);
        addInt(trace.FullFrameUnsafeLastLine);
        addInt(trace.FullFrameUnsafeLayerMask);
        addInt(trace.FullFrameUnsafeDispCntDiff);
        addInt(trace.FullFrameUnsafeLayerEnableDiff);
        for (int layer = 0; layer < 4; layer++)
            addInt(trace.FullFrameUnsafeBGCntDiff[layer]);
        addInt(trace.FullFrameUnsafeMiscDiffMask);
    };
    addStateDebugTrace(WholeSceneCurrentUpdateDebugTrace);
    auto addBitmapDebugTrace = [&](const WholeSceneUpdateDebugTrace& trace)
    {
        addInt(trace.RegisterLayerDirtyMask);
        addInt(trace.VRAMLayerDirtyMask);
        addInt(trace.PaletteLayerDirtyMask);
        addInt(trace.DeferredLayerDirtyMask);
        addInt(trace.InactiveDeferredLayerDirtyMask);
        addInt(trace.FlatVRAMCaptureSyncLayerMask);
        addInt(trace.CoveredBitmapMask);
        addInt(trace.ContributingLayerMask);
        addInt(trace.VisibleBitmapDirtyMask);
        addInt(trace.VisibleBitmapVRAMDirtyMask);
        addInt(trace.VisibleBitmapDirtyBeforeLineMask);
        addInt(trace.VisibleBitmapDirtyAfterLineMask);
        addInt(trace.VisibleBitmapDirtyCrossesLineMask);
        addInt(trace.VisibleBitmapRowLimitedPrerenderMask);
        addInt(trace.VisibleBitmapCoveredDirtyDeferredMask);
        addInt(trace.VisibleBitmapDirtyFirstRow);
        addInt(trace.VisibleBitmapDirtyLastRow);
        for (int layer = 0; layer < 4; layer++)
        {
            addInt(trace.VisibleBitmapLayerFirstRow[layer]);
            addInt(trace.VisibleBitmapLayerLastRow[layer]);
        }
        for (int layer = 0; layer < 4; layer++)
        {
            addInt(trace.VisibleBitmapRowLimitedFirstRow[layer]);
            addInt(trace.VisibleBitmapRowLimitedLastRow[layer]);
        }
    };
    addBitmapDebugTrace(WholeSceneCurrentUpdateDebugTrace);
    addInt(WholeScenePreviousUpdateDebugTrace.LayerDirtyEvents);
    addInt(WholeScenePreviousUpdateDebugTrace.LayerDirtyMask);
    addInt(WholeScenePreviousUpdateDebugTrace.BGUploadCalls);
    addInt(WholeScenePreviousUpdateDebugTrace.BGUploadRangeCount);
    addInt(WholeScenePreviousUpdateDebugTrace.BGUploadRows);
    addInt(WholeScenePreviousUpdateDebugTrace.LayerPrerenderCalls);
    addInt(WholeScenePreviousUpdateDebugTrace.LayerPrerenderMask);
    addInt(WholeScenePreviousUpdateDebugTrace.LayerPrerenderBitmapMask);
    addStateDebugTrace(WholeScenePreviousUpdateDebugTrace);
    addBitmapDebugTrace(WholeScenePreviousUpdateDebugTrace);
    AppendWholeSceneUpdateTimingCSVRow(row);
}

void GLRenderer2D::ResetWholeSceneUpdateTiming()
{
    CaptureBackedHandoff.FrameSerial++;
    CaptureBackedHandoff.BackgroundUpdated = false;
    CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::None;
    CaptureBackedHandoff.CurrentKey = {};

    WholeScenePreviousUpdateDebugTrace = WholeSceneCurrentUpdateDebugTrace;
    WholeScenePreviousFramePartialComposites = WholeSceneUpdateTiming.PartialComposite.Count;
    if (ShouldArmWholeSceneCurrentFragmentationGuard(WholeScenePreviousFramePartialComposites))
    {
        WholeSceneCurrentFragmentationGuardFrames =
            std::max(WholeSceneCurrentFragmentationGuardFrames,
                     kWholeSceneCurrentFragmentationGuardHoldFrames);
    }
    else if (WholeSceneCurrentFragmentationGuardFrames > 0)
    {
        WholeSceneCurrentFragmentationGuardFrames--;
    }

    if (ShouldArmWholeSceneFinalNativeFragmentationGuard(WholeScenePreviousFramePartialComposites))
    {
        WholeSceneHybridFragmentationGuardFrames =
            std::max(WholeSceneHybridFragmentationGuardFrames,
                     kWholeSceneHybridFragmentationGuardHoldFrames);
    }
    else if (WholeSceneHybridFragmentationGuardFrames > 0)
    {
        WholeSceneHybridFragmentationGuardFrames--;
    }

    if (WholeSceneCaptureBackedHandoffGuardFrames > 0)
    {
        WholeSceneCaptureBackedHandoffGuardFrames--;
        if (WholeSceneCaptureBackedHandoffGuardFrames == 0)
            InvalidateCaptureBackedHandoffSnapshot();
    }

    WholeSceneCurrentFramePartialComposites = 0;
    WholeSceneHybridFragmentationGuardTripped = false;
    WholeSceneCurrentFragmentationGuardTripped = false;
    WholeSceneFullFrameFinalizerUnsafeFrame = false;
    ResetWholeSceneNativeProductTracking();
    WholeSceneUpdateTiming = {};
    WholeSceneCurrentUpdateDebugTrace = {};
}

void GLRenderer2D::AddWholeSceneUpdateTiming(WholeSceneUpdatePhaseTiming& phase, u64 elapsedUS)
{
    phase.TotalUS += elapsedUS;
    if (elapsedUS > phase.MaxUS)
        phase.MaxUS = elapsedUS;
    phase.Count++;
}

bool GLRenderer2D::IsWholeSceneHybridFragmentationGuardActive() const
{
    if (WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::Off)
        return false;
    if (WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::AutoCurrentForBitmap)
        return false;

    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        WholeSceneHybridFragmentationGuardFrames == 0)
        return false;

    return true;
}

bool GLRenderer2D::IsWholeSceneCurrentFragmentationGuardActive() const
{
    if (WholeSceneCurrentFragmentationGuardFrames == 0)
        return false;

    if (WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::AutoCurrentForBitmap)
        return IsWholeSceneStreamingBitmapFragmentationCandidate();

    return WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::CurrentForSevere;
}

bool GLRenderer2D::IsWholeSceneHybridPresentationGuardActive(int ystart, int yend) const
{
    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !CanUseWholeSceneScalePath())
    {
        return false;
    }

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (!GPU2D.Num &&
        dispmode == 2 &&
        CanScaleMainEngineVRAMDisplayCaptureSourceA(dispmode))
    {
        return false;
    }

    if (!Parent.IsFinalPresentationTransitionGuardActiveForRange(ystart, yend))
        return false;

    return !CanBypassWholeSceneHybridPresentationGuardForDirect2D(ystart, yend);
}

bool GLRenderer2D::CanBypassWholeSceneHybridPresentationGuardForDirect2D(int ystart, int yend) const
{
    bool screenSwap = false;
    if (!Parent.GetFinalPassScreenSwapForRange(ystart, yend, screenSwap))
        return false;
    (void)screenSwap;

    if (Parent.IsFinalPresentationScreenSwapExcursionActiveForRange(ystart, yend))
        return false;

    // Master brightness is applied by the final pass, so it does not change
    // which direct-2D renderer owns the physical screen.
    if (GPU.CaptureEnable ||
        Parent.HasMainVRAMDisplayCaptureFinalRoute())
    {
        return false;
    }

    const auto* mainRenderer = dynamic_cast<const GLRenderer2D*>(Parent.Rend2D_A.get());
    const auto* subRenderer = dynamic_cast<const GLRenderer2D*>(Parent.Rend2D_B.get());
    if (!mainRenderer || !subRenderer)
        return false;

    const u32 mainDispMode = (mainRenderer->DispCnt >> 16) & 0x3u;
    const u32 subDispMode = (subRenderer->DispCnt >> 16) & 0x1u;
    if (mainDispMode != 1 || subDispMode != 1)
        return false;

    return mainRenderer->WholeSceneScaleMode ==
               RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale &&
           subRenderer->WholeSceneScaleMode ==
               RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale &&
           mainRenderer->CanUseWholeSceneScalePath() &&
           subRenderer->CanUseWholeSceneScalePath();
}

bool GLRenderer2D::ShouldUseWholeSceneHybridOverlayForSuppressedDirect3DAlphaBlend() const
{
    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !CanUseWholeSceneScalePath() ||
        GPU2D.Num)
        return false;

    const u32 dispmode = (DispCnt >> 16) & 0x3u;
    if (dispmode != 1)
        return false;

    const u32 direct3DMask = 1u << 0;
    const u32 bg2DLayerMask = (1u << 1) | (1u << 2) | (1u << 3);
    const u32 objMask = 1u << 4;
    const u32 backdropMask = 1u << 5;

    if (!(DispCnt & (1 << 3)) || !(LayerEnable & direct3DMask))
        return false;

    const u32 blendEffect = (BlendCnt >> 6) & 0x3u;
    if (blendEffect != 1 || EVA != 0)
        return false;

    const u32 blendTarget1 = BlendCnt & 0x3Fu;
    const u32 blendTarget2 = (BlendCnt >> 8) & 0x3Fu;
    u32 visibleNativeLayers = LayerEnable & bg2DLayerMask;
    if ((LayerEnable & objMask) && OBJEnable && NumSprites > 0)
        visibleNativeLayers |= objMask;

    const bool direct3DTarget1 = (blendTarget1 & direct3DMask) != 0;
    const bool visibleNativeTarget1 = (blendTarget1 & visibleNativeLayers) != 0;
    if (!direct3DTarget1 || visibleNativeTarget1 || (blendTarget2 & direct3DMask))
        return false;

    u32 visibleNativeTarget2 = visibleNativeLayers;
    if (blendTarget2 & backdropMask)
        visibleNativeTarget2 |= backdropMask;

    return (blendTarget2 & visibleNativeTarget2) != 0;
}

bool GLRenderer2D::IsWholeSceneCaptureBackedHandoffGuardActive() const
{
    return WholeSceneCaptureBackedHandoffGuardFrames > 0 &&
           WholeSceneScaleRequested &&
           WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale &&
           WholeSceneScaleCaptureBacked &&
           !GPU2D.Num;
}

bool GLRenderer2D::IsWholeSceneCaptureBackedHandoffCandidate() const
{
    if (!WholeSceneScaleRequested ||
        WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleCaptureBacked ||
        GPU2D.Num)
        return false;

    const u32 dispmode = (DispCnt >> 16) & 0x3u;
    if (dispmode != 1)
        return false;

    const bool objVisible = (LayerEnable & (1 << 4)) && OBJEnable && NumSprites > 0;
    if (!objVisible)
        return false;

    const u32 captureLayerMask = (1 << 2) | (1 << 3);
    const bool visibleDirect3DLayer = (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0));
    const u32 visibleBGLayers = LayerEnable & 0x0Fu;
    const bool direct3DOnlyLiveBackground =
        visibleDirect3DLayer && visibleBGLayers == (1u << 0);
    const bool visibleFullDisplaySourceACaptureBG =
        (VisibleFullDisplayCaptureFromSourceABGLayerMask() & captureLayerMask) != 0;
    const bool fullScreenBitmapUpload =
        WholeSceneCurrentUpdateDebugTrace.BGUploadRows >= 192 &&
        (WholeSceneCurrentUpdateDebugTrace.LayerDirtyMask & captureLayerMask);
    const bool maintainingKnownCaptureRoute =
        CaptureBackedRoute[0].HasCapturedPhase ||
        CaptureBackedRoute[1].HasCapturedPhase;
    const int currentRouteSlot = Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, 0, 192) ? 1 : 0;
    const bool hasProvenRouteProduct =
        CaptureBackedRoute[currentRouteSlot].Product.Valid &&
        CaptureBackedRoute[currentRouteSlot].Product.CapturedEventSerial;
    const bool stableKnownLiveRoute =
        direct3DOnlyLiveBackground &&
        hasProvenRouteProduct &&
        WholeSceneCurrentUpdateDebugTrace.BGUploadRows == 0 &&
        WholeSceneNativeProductEpochValid;

    return visibleFullDisplaySourceACaptureBG ||
           stableKnownLiveRoute ||
           (visibleDirect3DLayer && fullScreenBitmapUpload && maintainingKnownCaptureRoute);
}

bool GLRenderer2D::ShouldUseWholeSceneCaptureBackedHandoffForRange(int ystart, int yend) const
{
    if (!IsWholeSceneCaptureBackedHandoffGuardActive())
        return false;

    const CaptureBackedHandoffRouteKey key = BuildCaptureBackedHandoffRouteKey(ystart, yend);
    const int slot = CaptureBackedHandoffRouteSlot(key);
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
        return false;

    if (key.Phase == CaptureBackedHandoffPhase::CapturedBitmap)
        return HasOnlyFullDisplayCaptureFromSourceABGLayers() ||
               CaptureBackedRoute[slot].HasCapturedPhase;

    if (key.Phase == CaptureBackedHandoffPhase::Live3D)
        return CaptureBackedRoute[slot].HasCapturedPhase ||
               IsStableCaptureBackedHandoffLiveUpdate(key);

    return false;
}

void GLRenderer2D::UpdateWholeSceneCaptureBackedHandoffGuard()
{
    if (!IsWholeSceneCaptureBackedHandoffCandidate())
        return;

    WholeSceneCaptureBackedHandoffGuardFrames =
        std::max<u32>(WholeSceneCaptureBackedHandoffGuardFrames, 12);
}

GLRenderer2D::CaptureBackedHandoffPhase GLRenderer2D::CurrentCaptureBackedHandoffPhase() const
{
    const bool direct3DLayerVisible =
        !GPU2D.Num && (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0));
    const bool capturedBitmapVisible =
        (VisibleBitmapBGLayerMask() & ((1 << 2) | (1 << 3))) != 0;

    if (direct3DLayerVisible && !capturedBitmapVisible)
        return CaptureBackedHandoffPhase::Live3D;
    if (capturedBitmapVisible)
        return CaptureBackedHandoffPhase::CapturedBitmap;
    return CaptureBackedHandoffPhase::Other;
}

GLRenderer2D::CaptureBackedHandoffRouteKey GLRenderer2D::BuildCaptureBackedHandoffRouteKey(int ystart, int yend) const
{
    CaptureBackedHandoffRouteKey key = {};
    key.Engine = GPU2D.Num;
    bool finalPassScreenSwap = GPU.ScreenSwap;
    Parent.GetFinalPassScreenSwapForRange(ystart, yend, finalPassScreenSwap);
    key.ScreenSwap = finalPassScreenSwap;
    key.EngineFinalBottom = Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, ystart, yend);
    key.EngineFinalTop = !key.EngineFinalBottom;
    key.DisplayMode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    key.BGMode = DispCnt & 0x7u;
    key.LayerEnable = LayerEnable;
    key.VisibleBitmapMask = VisibleBitmapBGLayerMask();
    key.BGUploadRows = WholeSceneCurrentUpdateDebugTrace.BGUploadRows;
    key.FrameSerial = CaptureBackedHandoff.FrameSerial;
    key.YStart = ystart;
    key.YEnd = yend;
    key.Phase = CurrentCaptureBackedHandoffPhase();
    return key;
}

int GLRenderer2D::BeginCaptureBackedHandoffRoute(int ystart, int yend)
{
    CaptureBackedHandoff.CurrentKey = BuildCaptureBackedHandoffRouteKey(ystart, yend);
    const int handoffSlot = CaptureBackedHandoffRouteSlot(CaptureBackedHandoff.CurrentKey);
    CaptureBackedHandoff.CurrentSlot = static_cast<u8>(handoffSlot);
    CaptureBackedHandoff.BackgroundUpdated = false;
    CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::None;
    return handoffSlot;
}

int GLRenderer2D::CaptureBackedHandoffRouteSlot(const CaptureBackedHandoffRouteKey& key) const
{
    return key.EngineFinalBottom ? 1 : 0;
}

bool GLRenderer2D::IsStableCaptureBackedHandoffLiveUpdate(const CaptureBackedHandoffRouteKey& key) const
{
    return key.Phase == CaptureBackedHandoffPhase::Live3D &&
           key.BGUploadRows == 0 &&
           WholeSceneNativeProductEpochValid;
}

bool GLRenderer2D::CanReuseCaptureBackedHandoffSnapshot(const CaptureBackedHandoffRouteKey& key,
                                                        int slot,
                                                        CaptureBackedHandoffReuseReason& reason) const
{
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedRouteChanged;
        return false;
    }

    if (!CaptureBackedRoute[slot].Handoff3DValid)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedNoSnapshot;
        return false;
    }

    const auto& latched = CaptureBackedRoute[slot].HandoffLatchedKey;

    if (key.Engine != latched.Engine)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedEngineChanged;
        return false;
    }
    if (key.ScreenSwap != latched.ScreenSwap)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedScreenSwapChanged;
        return false;
    }
    if (key.EngineFinalTop != latched.EngineFinalTop ||
        key.EngineFinalBottom != latched.EngineFinalBottom)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedPhysicalScreenChanged;
        return false;
    }
    if (key.YStart != latched.YStart || key.YEnd != latched.YEnd)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedYRangeChanged;
        return false;
    }
    if (key.DisplayMode != latched.DisplayMode)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedRouteChanged;
        return false;
    }

    if (key.Phase == latched.Phase &&
        key.BGMode == latched.BGMode &&
        key.LayerEnable == latched.LayerEnable &&
        key.VisibleBitmapMask == latched.VisibleBitmapMask)
    {
        reason = CaptureBackedHandoffReuseReason::ExactKeyMatch;
        return true;
    }

    const bool liveToCapturePair =
        latched.Phase == CaptureBackedHandoffPhase::Live3D &&
        key.Phase == CaptureBackedHandoffPhase::CapturedBitmap &&
        (latched.LayerEnable & (1 << 0)) &&
        (latched.LayerEnable & (1 << 4)) &&
        (key.LayerEnable & (1 << 4)) &&
        (key.VisibleBitmapMask & ((1 << 2) | (1 << 3)));
    if (!liveToCapturePair)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedPhaseNotEquivalent;
        return false;
    }

    const u32 frameAge = key.FrameSerial - latched.FrameSerial;
    if (frameAge > 12)
    {
        reason = CaptureBackedHandoffReuseReason::RejectedEpochChanged;
        return false;
    }

    reason = CaptureBackedHandoffReuseReason::AllowedLiveToCapturePair;
    return true;
}

void GLRenderer2D::ClearCaptureBackedRouteProductState(int slot)
{
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
        return;

    CaptureBackedRoute[slot].Product = {};
    CaptureBackedRoute[slot].EventProduct = {};
    CaptureBackedRoute[slot].PendingEvent = {};
}

void GLRenderer2D::ClearCaptureBackedRouteState(int slot)
{
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
        return;

    CaptureBackedRoute[slot].Handoff3DValid = false;
    CaptureBackedRoute[slot].HandoffLatchedKey = {};
    CaptureBackedRoute[slot].Presentation = {};
    ClearCaptureBackedRouteProductState(slot);
    CaptureBackedRoute[slot].HasCapturedPhase = false;
}

void GLRenderer2D::LatchCaptureBackedHandoffSnapshot(int slot, const CaptureBackedHandoffRouteKey& key)
{
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
        return;

    CaptureBackedRoute[slot].Handoff3DValid = true;
    CaptureBackedRoute[slot].HandoffLatchedKey = key;
}

void GLRenderer2D::MarkCaptureBackedRouteCapturedPhase(int slot)
{
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
        return;

    CaptureBackedRoute[slot].HasCapturedPhase = true;
}

void GLRenderer2D::InvalidateCaptureBackedHandoffSnapshot(int slot)
{
    if (slot >= 0 && slot < kCaptureBackedHandoffRouteSlots)
    {
        ClearCaptureBackedRouteState(slot);
        return;
    }

    for (int i = 0; i < kCaptureBackedHandoffRouteSlots; i++)
        ClearCaptureBackedRouteState(i);
}

void GLRenderer2D::UpdateCaptureBackedRoutePresentation(int slot,
                                                        CaptureBackedRoutePresentationMode mode,
                                                        u64 serial,
                                                        u32 captureBank,
                                                        u32 source3DSceneHash,
                                                        u32 sourcePresentationHash,
                                                        u32 currentOverlayPresentationHash)
{
    if (slot < 0 || slot >= kCaptureBackedHandoffRouteSlots)
        return;

    auto& state = CaptureBackedRoute[slot].Presentation;
    const bool sameSource =
        state.Valid &&
        state.CaptureBank == captureBank &&
        state.Source3DSceneHash == source3DSceneHash &&
        state.SourcePresentationHash == sourcePresentationHash;
    const u32 stableFrames = sameSource ? std::min<u32>(state.StableFrames + 1, 0xFFFFu) : 0;

    state.Valid = true;
    state.Mode = mode;
    state.Serial = serial;
    state.CaptureBank = captureBank;
    state.Source3DSceneHash = source3DSceneHash;
    state.SourcePresentationHash = sourcePresentationHash;
    state.CurrentOverlayPresentationHash = currentOverlayPresentationHash;
    state.StableFrames = stableFrames;
}

void GLRenderer2D::InvalidateCaptureBackedRouteProduct(int slot)
{
    if (slot >= 0 && slot < kCaptureBackedHandoffRouteSlots)
    {
        ClearCaptureBackedRouteProductState(slot);
        return;
    }

    for (int i = 0; i < kCaptureBackedHandoffRouteSlots; i++)
        ClearCaptureBackedRouteProductState(i);
}

int GLRenderer2D::VisibleSingleDisplayCaptureBank() const
{
    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    if (visibleBGMask == 0)
        return -1;

    int visibleBank = -1;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((visibleBGMask & (1u << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        if (cfg.Type != 8)
            return -1;

        const int captureBank = static_cast<int>(cfg.TileOffset & 0x3);
        if (visibleBank >= 0 && visibleBank != captureBank)
            return -1;

        visibleBank = captureBank;
    }

    return visibleBank;
}

int GLRenderer2D::VisibleSingleDisplayCaptureOBJBank() const
{
    if ((LayerEnable & (1 << 4)) == 0 || !OBJEnable || NumSprites <= 0)
        return -1;
    if ((LayerEnable & 0x0Fu) != 0)
        return -1;

    int visibleBank = -1;
    bool found = false;
    for (int i = 0; i < NumSprites; i++)
    {
        const auto& sprite = SpriteConfig.uOAM[i];
        int captureBank = -1;
        if (sprite.Type == 3)
            captureBank = static_cast<int>((sprite.TileStride >> 2) & 0x3);
        else if (sprite.Type == 4)
            captureBank = static_cast<int>(sprite.TileStride & 0x3);
        else
            continue;

        const auto& event = Parent.HighResDisplayCapture256Event[captureBank];
        if (!Parent.IsFullDisplayHighResCaptureEventRecord(event, static_cast<u32>(captureBank)) ||
            !(event.ProductMask & GLRenderer::HighResCaptureProductFullEquivalent) ||
            !Parent.HighResDisplayCaptureFullTex[captureBank])
        {
            return -1;
        }

        if (visibleBank >= 0 && visibleBank != captureBank)
            return -1;

        visibleBank = captureBank;
        found = true;
    }

    return found ? visibleBank : -1;
}

int GLRenderer2D::VisibleSingleHighResCaptureBank() const
{
    int visibleBank = VisibleSingleDisplayCaptureBank();
    if (visibleBank < 0)
        visibleBank = VisibleSingleDisplayCaptureOBJBank();
    if (visibleBank < 0)
        return -1;

    const auto& event = Parent.HighResDisplayCapture256Event[visibleBank];
    if (!Parent.IsFullDisplayHighResCaptureEventRecord(event, static_cast<u32>(visibleBank)))
        return -1;

    return visibleBank;
}

GLuint GLRenderer2D::VisibleHighResCaptureBackgroundTex() const
{
    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    if (visibleBGMask == 0)
        return 0;

    GLuint backgroundTex = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((visibleBGMask & (1u << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        u64 serial = 0;
        u32 sourceKind = 0;
        u32 productMask = 0;
        u32 rejectReason = 0;
        const GLuint layerTex = Parent.GetHighResDisplayCaptureBackgroundTexForBG(cfg.Type,
                                                                                  cfg.TileOffset,
                                                                                  serial,
                                                                                  sourceKind,
                                                                                  productMask,
                                                                                  rejectReason);
        if (!layerTex)
            return 0;
        if (backgroundTex && backgroundTex != layerTex)
            return 0;

        backgroundTex = layerTex;
    }

    return backgroundTex;
}

GLuint GLRenderer2D::VisibleHighResCaptureFullTex() const
{
    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    if (visibleBGMask == 0)
    {
        const int objBank = VisibleSingleDisplayCaptureOBJBank();
        if (objBank < 0)
            return 0;
        return Parent.HighResDisplayCaptureFullTex[objBank];
    }

    GLuint fullTex = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((visibleBGMask & (1u << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        u64 serial = 0;
        u32 sourceKind = 0;
        u32 productMask = 0;
        u32 rejectReason = 0;
        const GLuint layerTex = Parent.GetHighResDisplayCaptureFullTexForBG(cfg.Type,
                                                                            cfg.TileOffset,
                                                                            serial,
                                                                            sourceKind,
                                                                            productMask,
                                                                            rejectReason);
        if (!layerTex)
            return 0;
        if (fullTex && fullTex != layerTex)
            return 0;

        fullTex = layerTex;
    }

    return fullTex;
}

u32 GLRenderer2D::VisibleFullDisplayCaptureBGLayerMask(bool accept3DSourceA) const
{
    u32 mask = 0;

    for (int layer = 0; layer < 4; layer++)
    {
        if ((LayerEnable & (1 << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        const bool isFullDisplayCapture = accept3DSourceA
            ? Parent.IsCurrentFullDisplayCaptureFromSourceABG(cfg.Type, cfg.TileOffset)
            : Parent.IsCurrentSourceAOnlyFullDisplayCaptureBG(cfg.Type, cfg.TileOffset);
        if (isFullDisplayCapture)
            mask |= 1u << layer;
    }

    return mask;
}

u32 GLRenderer2D::VisibleSourceAOnlyFullDisplayCaptureBGLayerMask() const
{
    return VisibleFullDisplayCaptureBGLayerMask(false);
}

u32 GLRenderer2D::VisibleFullDisplayCaptureFromSourceABGLayerMask() const
{
    return VisibleFullDisplayCaptureBGLayerMask(true);
}

bool GLRenderer2D::HasOnlyFullDisplayCaptureFromSourceABGLayers() const
{
    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    if (visibleBGMask == 0)
        return false;

    const u32 captureMask = VisibleFullDisplayCaptureFromSourceABGLayerMask();
    return captureMask != 0 && (visibleBGMask & ~captureMask) == 0;
}

bool GLRenderer2D::HasWholeSceneHighResCaptureBackedOBJReplacement(int ystart, int yend) const
{
    const VisibleOBJCaptureDebug debug = BuildVisibleOBJCaptureDebug();
    const bool fullFrameRange = ystart == 0 && yend == 192;
    const bool trackedVRAMDisplayRoute =
        fullFrameRange &&
        Parent.IsMainVRAMDisplayFinalRouteForRange(ystart, yend);
    const bool fullWidthSourceAProduct =
        debug.FullWidthTopStrip &&
        debug.CurrentFullSourceA &&
        debug.EventRejectReason == 0 &&
        debug.ProductAvailable &&
        trackedVRAMDisplayRoute;

    const bool exactOBJReplacement =
        debug.NonCaptureSpriteCount == 0 &&
        (debug.FullScreen || fullWidthSourceAProduct);

    return debug.Found &&
           !debug.MixedBank &&
           debug.CurrentSourceAOnly &&
           exactOBJReplacement &&
           debug.ProductAvailable;
}

bool GLRenderer2D::CanUseWholeSceneCaptureOnlyHighResPath(int ystart, int yend) const
{
    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleCaptureBacked)
        return false;

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (dispmode != 1)
        return false;

    if (GPU2D.Num && Parent.HasMainVRAMDisplayCaptureFinalRoute())
    {
        bool screenSwap = false;
        if (!Parent.GetFinalPassScreenSwapForRange(ystart, yend, screenSwap))
            return false;
    }

    const bool objVisible = (LayerEnable & (1 << 4)) && OBJEnable && NumSprites > 0;
    if (objVisible && !HasWholeSceneHighResCaptureBackedOBJReplacement(ystart, yend))
        return false;

    return VisibleHighResCaptureFullTex() != 0 ||
           CanUseSourceABackgroundCurrentOverlayPath();
}

bool GLRenderer2D::IsWholeSceneStreamingBitmapFragmentationCandidate() const
{
    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);

    if (!GPU2D.Num && dispmode == 2 && CanScaleMainEngineVRAMDisplayCaptureSourceA(dispmode))
        return true;

    if (dispmode != 1)
        return false;

    const bool visibleDirect3DLayer = !GPU2D.Num &&
                                      (DispCnt & (1 << 3)) &&
                                      (LayerEnable & (1 << 0));
    if (visibleDirect3DLayer)
        return false;

    if ((LayerEnable & 0xFu) == 0 && (LayerEnable & (1 << 4)) && OBJEnable && NumSprites > 0)
    {
        int minX = 256;
        int minY = 192;
        int maxX = 0;
        int maxY = 0;
        int area = 0;

        for (int i = 0; i < NumSprites; i++)
        {
            const auto& sprite = SpriteConfig.uOAM[i];
            const int x0 = std::max(0, sprite.Position[0]);
            const int y0 = std::max(0, sprite.Position[1]);
            const int x1 = std::min(256, sprite.Position[0] + sprite.BoundSize[0]);
            const int y1 = std::min(192, sprite.Position[1] + sprite.BoundSize[1]);
            if (x1 <= x0 || y1 <= y0)
                continue;

            minX = std::min(minX, x0);
            minY = std::min(minY, y0);
            maxX = std::max(maxX, x1);
            maxY = std::max(maxY, y1);
            area += (x1 - x0) * (y1 - y0);
        }

        const int boundsWidth = maxX - minX;
        const int boundsHeight = maxY - minY;
        constexpr int screenArea = 256 * 192;

        if ((boundsWidth >= 240 && boundsHeight >= 176) ||
            (area >= (screenArea * 3) / 4))
            return true;
    }

    for (int layer = 0; layer < 4; layer++)
    {
        if ((LayerEnable & (1 << layer)) && IsFullScreenBitmapBGLayer(layer))
            return true;
    }

    return false;
}

bool GLRenderer2D::IsBitmapLikeBGLayer(int layer) const
{
    const auto& cfg = LayerConfig.uBGConfig[layer];
    return (cfg.Type == 4) || (cfg.Type == 5) ||
           (cfg.Type == 7) || (cfg.Type == 8);
}

bool GLRenderer2D::IsFullScreenBitmapBGLayer(int layer) const
{
    const auto& cfg = LayerConfig.uBGConfig[layer];
    return IsBitmapLikeBGLayer(layer) && cfg.Size[0] >= 256 && cfg.Size[1] >= 192;
}

bool GLRenderer2D::DirectColorBitmapLayerCoversRange(int layer, int ystart, int yend, const u8* bgvram, u32 bgvrammask) const
{
    if (layer < 2 || ystart >= yend || bgvram == nullptr)
        return false;

    const auto& cfg = LayerConfig.uBGConfig[layer];
    if (cfg.Type != 5 || !IsFullScreenBitmapBGLayer(layer))
        return false;

    if (BGCnt[layer] & (1 << 6))
        return false;

    u32 xmask, ymask, yshift;
    switch ((BGCnt[layer] >> 14) & 0x3)
    {
    case 0: xmask = 0x07FFF; ymask = 0x07FFF; yshift = 7; break;
    case 1: xmask = 0x0FFFF; ymask = 0x0FFFF; yshift = 8; break;
    case 2: xmask = 0x1FFFF; ymask = 0x0FFFF; yshift = 9; break;
    default: xmask = 0x1FFFF; ymask = 0x1FFFF; yshift = 9; break;
    }

    const u32 ofxmask = (BGCnt[layer] & (1 << 13)) ? 0 : ~xmask;
    const u32 ofymask = (BGCnt[layer] & (1 << 13)) ? 0 : ~ymask;
    const int rotIndex = layer - 2;

    ystart = std::max(ystart, 0);
    yend = std::min(yend, 192);

    for (int y = ystart; y < yend; y++)
    {
        const auto& scanline = ScanlineConfig.uScanline[y];
        s32 rotX = scanline.BGOffset[layer][0];
        s32 rotY = scanline.BGOffset[layer][1];
        const s16 rotA = static_cast<s16>(scanline.BGRotscale[rotIndex][0]);
        const s16 rotC = static_cast<s16>(scanline.BGRotscale[rotIndex][2]);

        for (int x = 0; x < 256; x++)
        {
            if ((rotX & ofxmask) || (rotY & ofymask))
                return false;

            const u32 srcX = (rotX & xmask) >> 8;
            const u32 srcY = (rotY & ymask) >> 8;
            const u32 offset = cfg.MapOffset + (((srcY << yshift) + srcX) << 1);
            const u32 addr = offset & bgvrammask;
            const u16 color = bgvram[addr] | (bgvram[(addr + 1) & bgvrammask] << 8);
            if (!(color & 0x8000))
                return false;

            rotX += rotA;
            rotY += rotC;
        }
    }

    return true;
}

u8 GLRenderer2D::CoveredByOpaqueBitmapBGLayerMask(int ystart, int yend, const u8* bgvram, u32 bgvrammask) const
{
    if (ystart >= yend)
        return 0;

    if ((DispCnt & 0xE000) || ((BlendCnt >> 6) & 0x3))
        return 0;

    bool opaqueCover[4] = {};
    for (int layer = 0; layer < 4; layer++)
    {
        if ((LayerEnable & (1 << layer)) &&
            DirectColorBitmapLayerCoversRange(layer, ystart, yend, bgvram, bgvrammask))
            opaqueCover[layer] = true;
    }

    u8 covered = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if (!(LayerEnable & (1 << layer)) || !IsBitmapLikeBGLayer(layer))
            continue;

        const int prio = BGCnt[layer] & 0x3;
        for (int upper = 0; upper < 4; upper++)
        {
            if (upper == layer || !opaqueCover[upper])
                continue;
            if ((BGCnt[upper] & 0x3) < prio)
            {
                covered |= (1 << layer);
                break;
            }
        }
    }

    return covered;
}

u32 GLRenderer2D::VisibleBitmapBGLayerMask() const
{
    u32 mask = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((LayerEnable & (1 << layer)) && IsBitmapLikeBGLayer(layer))
            mask |= (1u << layer);
    }
    return mask;
}

void GLRenderer2D::RecordWholeSceneLayerDirty(int line, u8 layerPreDirty)
{
    if (!layerPreDirty)
        return;

    auto& trace = WholeSceneCurrentUpdateDebugTrace;
    trace.LayerDirtyEvents++;
    trace.LayerDirtyMask |= layerPreDirty;

    const u32 index = trace.LayerDirtyEvents - 1;
    if (index < kWholeSceneDebugRangeRecordLimit)
    {
        trace.LayerDirtyLine[index] = line;
        trace.LayerDirtyEventMask[index] = layerPreDirty;
    }
    else
        trace.LayerDirtyOverflow++;
}

void GLRenderer2D::RecordWholeSceneBGUploadCall()
{
    WholeSceneCurrentUpdateDebugTrace.BGUploadCalls++;
}

void GLRenderer2D::RecordWholeSceneBGUploadRange(int line, int startRow, int endRow)
{
    auto& trace = WholeSceneCurrentUpdateDebugTrace;
    trace.BGUploadRangeCount++;
    trace.BGUploadRows += std::max(0, endRow - startRow);
    if (trace.BGUploadFirstRow < 0 || startRow < trace.BGUploadFirstRow)
        trace.BGUploadFirstRow = startRow;
    if (endRow > trace.BGUploadLastRow)
        trace.BGUploadLastRow = endRow;

    const u32 index = trace.BGUploadRangeCount - 1;
    if (index < kWholeSceneDebugRangeRecordLimit)
    {
        trace.BGUploadLine[index] = line;
        trace.BGUploadStartRow[index] = startRow;
        trace.BGUploadEndRow[index] = endRow;
    }
    else
        trace.BGUploadRangeOverflow++;
}

void GLRenderer2D::RecordWholeSceneLayerPrerender(int line, u8 layerPreDirty)
{
    if (!layerPreDirty)
        return;

    auto& trace = WholeSceneCurrentUpdateDebugTrace;
    trace.LayerPrerenderCalls++;
    trace.LayerPrerenderMask |= layerPreDirty;
    trace.LayerPrerenderBitmapMask |= layerPreDirty & VisibleBitmapBGLayerMask();

    const u32 index = trace.LayerPrerenderCalls - 1;
    if (index < kWholeSceneDebugRangeRecordLimit)
    {
        trace.LayerPrerenderLine[index] = line;
        trace.LayerPrerenderEventMask[index] = layerPreDirty;
    }
    else
        trace.LayerPrerenderOverflow++;
}

bool GLRenderer2D::DirtyBitmapSourceRowsForLayer(int layer,
                                                 const NonStupidBitField<1024>& bgDirty,
                                                 int& firstRow,
                                                 int& lastRow) const
{
    firstRow = -1;
    lastRow = -1;

    if (layer < 0 || layer >= 4)
        return false;

    const auto& cfg = LayerConfig.uBGConfig[layer];
    if ((cfg.Type != 4 && cfg.Type != 5) ||
        cfg.Size[0] <= 0 || cfg.Size[1] <= 0)
        return false;

    const u32 bytesPerPixel = (cfg.Type == 5) ? 2 : 1;
    const u32 bytesPerRow = cfg.Size[0] * bytesPerPixel;
    if (bytesPerRow == 0)
        return false;

    const u32 vramBytes = GPU2D.Num ? (128 * 1024) : (512 * 1024);
    const u32 bitmapStart = cfg.MapOffset & (vramBytes - 1);
    const u32 bitmapBytes = cfg.Size[1] * bytesPerRow;
    if (bitmapBytes == 0)
        return false;

    // Keep the first detector narrow. Wrapping bitmap sources can be handled
    // later, but the first FMV test should not report misleading row spans.
    if (bitmapStart + bitmapBytes > vramBytes)
    {
        const u32 startBit = bitmapStart / VRAMDirtyGranularity;
        const u32 bitsCount = (vramBytes - bitmapStart + VRAMDirtyGranularity - 1) / VRAMDirtyGranularity;
        bool dirty = false;
        for (u32 bit = startBit; bit < startBit + bitsCount; bit++)
        {
            if (bgDirty[bit])
            {
                dirty = true;
                break;
            }
        }
        if (!dirty)
            return false;

        firstRow = 0;
        lastRow = cfg.Size[1];
        return true;
    }

    const u32 bitmapEnd = bitmapStart + bitmapBytes;
    const u32 dirtyBitLimit = GPU2D.Num ? 256 : 1024;
    for (u32 bit = 0; bit < dirtyBitLimit; bit++)
    {
        if (!bgDirty[bit])
            continue;

        const u32 dirtyStart = bit * VRAMDirtyGranularity;
        const u32 dirtyEnd = dirtyStart + VRAMDirtyGranularity;
        const u32 overlapStart = std::max(dirtyStart, bitmapStart);
        const u32 overlapEnd = std::min(dirtyEnd, bitmapEnd);
        if (overlapStart >= overlapEnd)
            continue;

        const int rowStart = std::clamp<int>((overlapStart - bitmapStart) / bytesPerRow,
                                             0, cfg.Size[1]);
        const int rowEnd = std::clamp<int>(((overlapEnd - bitmapStart) + bytesPerRow - 1) / bytesPerRow,
                                           0, cfg.Size[1]);
        if (rowStart >= rowEnd)
            continue;

        if (firstRow < 0 || rowStart < firstRow)
            firstRow = rowStart;
        if (rowEnd > lastRow)
            lastRow = rowEnd;
    }

    return firstRow >= 0 && lastRow > firstRow;
}

bool GLRenderer2D::VisibleBitmapDirtyRowsCoveredByOpaqueUpperLayer(int layer,
                                                                    int firstRow,
                                                                    int lastRow,
                                                                    const u8* bgvram,
                                                                    u32 bgvrammask) const
{
    if (layer < 0 || layer >= 4 || firstRow < 0 || lastRow <= firstRow)
        return false;
    if (!(LayerEnable & (1 << layer)) || !IsBitmapLikeBGLayer(layer))
        return false;

    firstRow = std::clamp(firstRow, 0, 192);
    lastRow = std::clamp(lastRow, firstRow, 192);
    if (lastRow <= firstRow)
        return false;

    const int prio = BGCnt[layer] & 0x3;
    for (int upper = 0; upper < 4; upper++)
    {
        if (upper == layer || !(LayerEnable & (1 << upper)))
            continue;
        if ((BGCnt[upper] & 0x3) >= prio)
            continue;
        if (DirectColorBitmapLayerCoversRange(upper, firstRow, lastRow, bgvram, bgvrammask))
            return true;
    }

    return false;
}

void GLRenderer2D::MarkLayerPrerenderDeferred(int layer, int firstRow, int lastRow)
{
    if (layer < 0 || layer >= 4)
        return;

    const u32 layerMask = 1u << layer;
    DeferredLayerPrerenderDirty |= layerMask;

    // No row range means an unknown/full pending prerender, used by the older
    // hidden-layer deferral path. Row ranges are for covered bitmap dirty rows.
    if (firstRow < 0 || lastRow <= firstRow)
    {
        DeferredLayerPrerenderFirstRow[layer] = -1;
        DeferredLayerPrerenderLastRow[layer] = -1;
        return;
    }

    if (DeferredLayerPrerenderFirstRow[layer] < 0 ||
        firstRow < DeferredLayerPrerenderFirstRow[layer])
        DeferredLayerPrerenderFirstRow[layer] = firstRow;
    if (lastRow > DeferredLayerPrerenderLastRow[layer])
        DeferredLayerPrerenderLastRow[layer] = lastRow;
}

void GLRenderer2D::ClearLayerPrerenderDeferred(u8 layerMask)
{
    DeferredLayerPrerenderDirty &= ~layerMask;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((layerMask & (1 << layer)) == 0)
            continue;

        DeferredLayerPrerenderFirstRow[layer] = -1;
        DeferredLayerPrerenderLastRow[layer] = -1;
    }
}

void GLRenderer2D::SyncPendingDisplayCapturesForFlatVRAMBGs()
{
    // Display captures stay GPU-side until something reads the VRAM they
    // landed in. Only direct-color bitmap BGs get capture-classified (layer
    // types 7/8) and sample the GPU-side capture texture; every other BG
    // shape is composited from the flat VRAM mirror, which still holds
    // pre-capture content until a sync runs. Read-sync capture blocks that
    // overlap such a layer so the flatten below picks up the real captured
    // data. A read sync keeps the GPU-side capture products valid, and
    // already-synced blocks return immediately.
    const u8 enabledLayers = (LayerEnable | GPU2D.LayerEnable) & 0xF;
    if (!enabledLayers)
        return;

    const int capturemask = GPU2D.Num ? 0x7 : 0x1F;
    int captureinfo[32];
    GPU2D.GetCaptureInfo_BG(captureinfo);

    u32 syncedLayerMask = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((enabledLayers & (1 << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        if (cfg.Type == 6 || cfg.Type == 7 || cfg.Type == 8)
            continue;

        const u32* rangeinfo = BGVRAMRange[layer];
        for (int r = 0; r < 4; r += 2)
        {
            if (rangeinfo[r] == 0xFFFFFFFF)
                continue;

            const u32 start = rangeinfo[r] >> 14;
            const u32 end = (rangeinfo[r] + rangeinfo[r + 1] + 0x3FFF) >> 14;
            for (u32 b = start; b < end; b++)
            {
                const int blk = captureinfo[b & capturemask];
                if (blk < 0 || !GPU.HasUnsyncedVRAMCaptureBlock(blk))
                    continue;

                GPU.SyncVRAMCaptureBlockForRead(blk);
                syncedLayerMask |= 1u << layer;
            }
        }
    }

    if (syncedLayerMask)
        WholeSceneCurrentUpdateDebugTrace.FlatVRAMCaptureSyncLayerMask |= syncedLayerMask;
}

void GLRenderer2D::RecordWholeSceneVisibleBitmapDirtyRows(int layer, int line, int firstRow, int lastRow)
{
    if (layer < 0 || layer >= 4 || firstRow < 0 || lastRow <= firstRow)
        return;

    auto& trace = WholeSceneCurrentUpdateDebugTrace;
    const u32 layerMask = 1u << layer;

    trace.VisibleBitmapVRAMDirtyMask |= layerMask;
    trace.VisibleBitmapLayerFirstRow[layer] = firstRow;
    trace.VisibleBitmapLayerLastRow[layer] = lastRow;

    if (trace.VisibleBitmapDirtyFirstRow < 0 || firstRow < trace.VisibleBitmapDirtyFirstRow)
        trace.VisibleBitmapDirtyFirstRow = firstRow;
    if (lastRow > trace.VisibleBitmapDirtyLastRow)
        trace.VisibleBitmapDirtyLastRow = lastRow;

    if (firstRow < line)
        trace.VisibleBitmapDirtyBeforeLineMask |= layerMask;
    if (lastRow > line)
        trace.VisibleBitmapDirtyAfterLineMask |= layerMask;
    if (firstRow < line && lastRow > line)
        trace.VisibleBitmapDirtyCrossesLineMask |= layerMask;
}

void GLRenderer2D::UpdateCachedRegistersAndLayerConfig(u8 layerPreDirty)
{
    UnitEnabled = GPU2D.Enabled;
    DispCnt = GPU2D.DispCnt;
    LayerEnable = GPU2D.LayerEnable;
    OBJEnable = GPU2D.OBJEnable;
    ForcedBlank = GPU2D.ForcedBlank;
    for (int layer = 0; layer < 4; layer++)
        BGCnt[layer] = GPU2D.BGCnt[layer];
    BlendCnt = GPU2D.BlendCnt;
    EVA = GPU2D.EVA;
    EVB = GPU2D.EVB;
    EVY = GPU2D.EVY;

    if (layerPreDirty || LayerConfigDirty)
        UpdateLayerConfig();
    LayerConfigDirty = false;
}

void GLRenderer2D::UploadBGVRAM(NonStupidBitField<1024>& bgDirty, int line)
{
    int dirtybits = GPU2D.Num ? 256 : 1024;
    if (!bgDirty.CheckRange(0, dirtybits))
        return;

    RecordWholeSceneBGUploadCall();

    // TODO: only do it for active layers?
    // this would require keeping track of the dirty state for areas not included in any layer

    u8 *vram;
    u32 vrammask;
    GPU2D.GetBGVRAM(vram, vrammask);

    glBindTexture(GL_TEXTURE_2D, VRAMTex_BG);

    int texlen = dirtybits >> 6;
    for (int i = 0; i < texlen; )
    {
        if (!bgDirty.Data[i])
        {
            i++;
            continue;
        }

        int start = i * 32;
        for (;;)
        {
            i++;
            if (i >= texlen) break;
            if (!bgDirty.Data[i]) break;
        }
        int end = i * 32;

        RecordWholeSceneBGUploadRange(line, start, end);

        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, start,
                        1024, end - start,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                        &vram[start * 1024]);
    }
}

void GLRenderer2D::UploadBGPalette(u32 paletteDirty, NonStupidBitField<64>& bgExtPalDirty)
{
    if (!paletteDirty && !bgExtPalDirty.CheckRange(0, 64))
        return;

    memcpy(&TempPalBuffer[0], &GPU.Palette[GPU2D.Num ? 0x400 : 0], 256*2);
    for (int s = 0; s < 4; s++)
    {
        for (int p = 0; p < 16; p++)
        {
            u16 *pal = GPU2D.GetBGExtPal(s, p);
            memcpy(&TempPalBuffer[(1 + ((s*16)+p)) * 256], pal, 256*2);
        }
    }

    glBindTexture(GL_TEXTURE_2D, PalTex_BG);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1+(4*16), GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
                    TempPalBuffer);
}

void GLRenderer2D::PrerenderDirtyLayers(u8 layerPreDirty,
                                         u8 rowLimitedBitmapMask,
                                         const int rowLimitedFirstRow[4],
                                         const int rowLimitedLastRow[4],
                                         int line)
{
    if (!layerPreDirty)
        return;

    RecordWholeSceneLayerPrerender(line, layerPreDirty);

    glUseProgram(LayerPreShader);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glBindBufferBase(GL_UNIFORM_BUFFER, 20, LayerConfigUBO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, VRAMTex_BG);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, PalTex_BG);

    for (int layer = 0; layer < 4; layer++)
    {
        if (!(layerPreDirty & (1 << layer)))
            continue;

        if ((rowLimitedBitmapMask & (1 << layer)) &&
            rowLimitedFirstRow[layer] >= 0 &&
            rowLimitedLastRow[layer] > rowLimitedFirstRow[layer])
            PrerenderLayerRows(layer, rowLimitedFirstRow[layer], rowLimitedLastRow[layer]);
        else
            PrerenderLayer(layer);
    }
}

bool GLRenderer2D::ShouldArmWholeSceneFinalNativeFragmentationGuard(u32 partialComposites) const
{
    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale)
        return false;

    switch (WholeSceneScaleFragmentationFallback)
    {
    case RendererSettings::WholeScene2DFragmentationFallback::FinalNative:
    case RendererSettings::WholeScene2DFragmentationFallback::CurrentForSevere:
        return partialComposites >= kWholeSceneHybridFragmentationGuardThreshold;
    case RendererSettings::WholeScene2DFragmentationFallback::AutoCurrentForBitmap:
    case RendererSettings::WholeScene2DFragmentationFallback::Off:
    default:
        return false;
    }
}

bool GLRenderer2D::ShouldArmWholeSceneCurrentFragmentationGuard(u32 partialComposites) const
{
    switch (WholeSceneScaleFragmentationFallback)
    {
    case RendererSettings::WholeScene2DFragmentationFallback::AutoCurrentForBitmap:
        return partialComposites >= kWholeSceneStreamingBitmapFragmentationGuardThreshold &&
               IsWholeSceneStreamingBitmapFragmentationCandidate();
    case RendererSettings::WholeScene2DFragmentationFallback::CurrentForSevere:
        return partialComposites >= kWholeSceneCurrentFragmentationGuardThreshold;
    case RendererSettings::WholeScene2DFragmentationFallback::FinalNative:
    case RendererSettings::WholeScene2DFragmentationFallback::Off:
    default:
        return false;
    }
}

void GLRenderer2D::CountWholeScenePartialComposite(int ystart, int yend)
{
    WholeSceneCurrentFramePartialComposites++;
    auto& trace = WholeSceneCurrentUpdateDebugTrace;
    trace.PartialCompositeRangeCount++;
    const u32 index = trace.PartialCompositeRangeCount - 1;
    if (index < kWholeSceneDebugRangeRecordLimit)
    {
        trace.PartialCompositeStart[index] = ystart;
        trace.PartialCompositeEnd[index] = yend;
    }
    else
        trace.PartialCompositeRangeOverflow++;

    if (!CanUseWholeSceneScalePath() ||
        WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::Off)
        return;

    if (!WholeSceneCurrentFragmentationGuardTripped &&
        ShouldArmWholeSceneCurrentFragmentationGuard(WholeSceneCurrentFramePartialComposites))
    {
        WholeSceneCurrentFragmentationGuardFrames =
            std::max(WholeSceneCurrentFragmentationGuardFrames,
                     kWholeSceneCurrentFragmentationGuardHoldFrames);
        WholeSceneCurrentFragmentationGuardTripped = true;
    }

    if (!WholeSceneHybridFragmentationGuardTripped &&
        ShouldArmWholeSceneFinalNativeFragmentationGuard(WholeSceneCurrentFramePartialComposites))
    {
        WholeSceneHybridFragmentationGuardFrames =
            std::max(WholeSceneHybridFragmentationGuardFrames,
                     kWholeSceneHybridFragmentationGuardHoldFrames);
        WholeSceneHybridFragmentationGuardTripped = true;
    }
}

void GLRenderer2D::AppendWholeSceneUpdateTimingCSVHeader(std::string& header, const char* prefix) const
{
    auto addPhase = [&header, prefix](const char* name)
    {
        if (!header.empty())
            header += ",";
        header += prefix;
        header += "_";
        header += name;
        header += "_us";
        header += ",";
        header += prefix;
        header += "_";
        header += name;
        header += "_max_us";
        header += ",";
        header += prefix;
        header += "_";
        header += name;
        header += "_count";
    };

    addPhase("update_state_diff");
    addPhase("update_vram_flatten");
    addPhase("update_layer_dirty");
    addPhase("update_classify");
    addPhase("update_sprite_render");
    addPhase("update_partial_composite");
    addPhase("update_register_cache");
    addPhase("update_bg_upload");
    addPhase("update_bg_palette_upload");
    addPhase("update_layer_prerender");
    addPhase("update_obj_prerender");
    addPhase("vblank_sprite_render");
    addPhase("vblank_composite");
    addPhase("native_prepass");
    addPhase("native_exact_final");
    addPhase("overlay_black_exact_final");
    addPhase("overlay_white_exact_final");
    addPhase("overlay_true_exact_final");
    addPhase("overlay_endpoint");
    addPhase("hybrid_2d_base_candidate");
    addPhase("hybrid_legacy_candidate");
    addPhase("hybrid_foreground_candidate");
    addPhase("finalizer_upscale");
    addPhase("finalizer_composite");
}

void GLRenderer2D::AppendWholeSceneUpdateTimingCSVRow(std::string& row) const
{
    auto addPhase = [&row](const WholeSceneUpdatePhaseTiming& phase)
    {
        if (!row.empty())
            row += ",";
        row += std::to_string(phase.TotalUS);
        row += ",";
        row += std::to_string(phase.MaxUS);
        row += ",";
        row += std::to_string(phase.Count);
    };

    addPhase(WholeSceneUpdateTiming.StateDiff);
    addPhase(WholeSceneUpdateTiming.VRAMFlatten);
    addPhase(WholeSceneUpdateTiming.LayerDirty);
    addPhase(WholeSceneUpdateTiming.Classify);
    addPhase(WholeSceneUpdateTiming.SpriteRender);
    addPhase(WholeSceneUpdateTiming.PartialComposite);
    addPhase(WholeSceneUpdateTiming.RegisterCache);
    addPhase(WholeSceneUpdateTiming.BGUpload);
    addPhase(WholeSceneUpdateTiming.BGPaletteUpload);
    addPhase(WholeSceneUpdateTiming.LayerPrerender);
    addPhase(WholeSceneUpdateTiming.OBJPrerender);
    addPhase(WholeSceneUpdateTiming.VBlankSpriteRender);
    addPhase(WholeSceneUpdateTiming.VBlankComposite);
    addPhase(WholeSceneUpdateTiming.NativePrepass);
    addPhase(WholeSceneUpdateTiming.NativeExactFinal);
    addPhase(WholeSceneUpdateTiming.OverlayBlackExactFinal);
    addPhase(WholeSceneUpdateTiming.OverlayWhiteExactFinal);
    addPhase(WholeSceneUpdateTiming.OverlayTrueExactFinal);
    addPhase(WholeSceneUpdateTiming.OverlayEndpoint);
    addPhase(WholeSceneUpdateTiming.Hybrid2DBaseCandidate);
    addPhase(WholeSceneUpdateTiming.HybridLegacyCandidate);
    addPhase(WholeSceneUpdateTiming.HybridForegroundCandidate);
    addPhase(WholeSceneUpdateTiming.FinalizerUpscale);
    addPhase(WholeSceneUpdateTiming.FinalizerComposite);
}

void GLRenderer2D::AppendWholeSceneDisplayStateTrace(std::string& status) const
{
    auto yesNo = [](bool value) { return value ? "yes" : "no"; };
    const u32 cachedDisplayMode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    const u32 liveDisplayMode = (GPU2D.DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    const u32 cachedEnabledLayers = (DispCnt >> 8) & 0x1Fu;
    const u32 liveEnabledLayers = (GPU2D.DispCnt >> 8) & 0x1Fu;
    const u16 masterBright = GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    const u32 capCnt = GPU.CaptureCnt;
    const u32 capDstMode = (capCnt >> 29) & 0x3u;
    const u32 capSrcA = (capCnt >> 24) & 0x1u;
    const u32 capSrcB = (capCnt >> 25) & 0x1u;
    const u32 capDstBank = (capCnt >> 16) & 0x3u;
    const u32 capDstOffset = (capCnt >> 18) & 0x3u;
    const u32 capSize = (capCnt >> 20) & 0x3u;
    const u32 blendEffect = (BlendCnt >> 6) & 0x3u;
    const u32 blendTarget1 = BlendCnt & 0x3Fu;
    const u32 blendTarget2 = (BlendCnt >> 8) & 0x3Fu;

    status += "\n\nDS display state trace:";
    status += "\n  DISPCNT cached/live: ";
    status += FormatHex(DispCnt, 8);
    status += " / ";
    status += FormatHex(GPU2D.DispCnt, 8);
    status += "\n  display mode cached/live: ";
    status += DisplayModeName(GPU2D.Num != 0, cachedDisplayMode);
    status += " / ";
    status += DisplayModeName(GPU2D.Num != 0, liveDisplayMode);
    status += "; BG mode ";
    status += std::to_string(DispCnt & 0x7u);
    status += "; BG0 3D cached/live: ";
    status += yesNo(DispCnt & (1u << 3));
    status += " / ";
    status += yesNo(GPU2D.DispCnt & (1u << 3));
    status += "\n  DISPCNT enable bits cached: ";
    status += LayerEnableSummary(cachedEnabledLayers);
    status += "\n  DISPCNT enable bits live: ";
    status += LayerEnableSummary(liveEnabledLayers);
    status += "\n  delayed layer enable cached/live: ";
    status += LayerEnableSummary(LayerEnable);
    status += " / ";
    status += LayerEnableSummary(GPU2D.LayerEnable);
    status += "; OBJ delayed cached/live: ";
    status += yesNo(OBJEnable);
    status += " / ";
    status += yesNo(GPU2D.OBJEnable);
    status += "; forced blank cached/live: ";
    status += yesNo(ForcedBlank);
    status += " / ";
    status += yesNo(GPU2D.ForcedBlank);

    status += "\n  BGCNT cached:";
    for (int layer = 0; layer < 4; layer++)
    {
        status += " BG";
        status += std::to_string(layer);
        status += "=";
        status += FormatHex(BGCnt[layer], 4);
        status += "(prio ";
        status += std::to_string(BGCnt[layer] & 0x3u);
        status += ")";
    }
    status += "\n  BGCNT live:";
    for (int layer = 0; layer < 4; layer++)
    {
        status += " BG";
        status += std::to_string(layer);
        status += "=";
        status += FormatHex(GPU2D.BGCnt[layer], 4);
        status += "(prio ";
        status += std::to_string(GPU2D.BGCnt[layer] & 0x3u);
        status += ")";
    }

    status += "\n  BLDCNT cached/live: ";
    status += FormatHex(BlendCnt, 4);
    status += " / ";
    status += FormatHex(GPU2D.BlendCnt, 4);
    status += "; effect ";
    status += BlendEffectName(blendEffect);
    status += "; target1 ";
    status += FormatHex(blendTarget1, 2);
    status += "; target2 ";
    status += FormatHex(blendTarget2, 2);
    status += "; EVA/EVB/EVY cached ";
    status += std::to_string(EVA);
    status += "/";
    status += std::to_string(EVB);
    status += "/";
    status += std::to_string(EVY);
    status += "; live ";
    status += std::to_string(GPU2D.EVA);
    status += "/";
    status += std::to_string(GPU2D.EVB);
    status += "/";
    status += std::to_string(GPU2D.EVY);

    status += "\n  windows enabled: WIN0=";
    status += yesNo(DispCnt & (1u << 13));
    status += " WIN1=";
    status += yesNo(DispCnt & (1u << 14));
    status += " OBJWIN=";
    status += yesNo(DispCnt & (1u << 15));
    status += "; WinCnt ";
    for (int i = 0; i < 4; i++)
    {
        if (i)
            status += "/";
        status += FormatHex(GPU2D.WinCnt[i], 2);
    }
    status += "; Win0 x";
    status += std::to_string(GPU2D.Win0Coords[0]);
    status += "-";
    status += std::to_string(GPU2D.Win0Coords[1]);
    status += " y";
    status += std::to_string(GPU2D.Win0Coords[2]);
    status += "-";
    status += std::to_string(GPU2D.Win0Coords[3]);
    status += " active ";
    status += FormatHex(GPU2D.Win0Active, 2);
    status += "; Win1 x";
    status += std::to_string(GPU2D.Win1Coords[0]);
    status += "-";
    status += std::to_string(GPU2D.Win1Coords[1]);
    status += " y";
    status += std::to_string(GPU2D.Win1Coords[2]);
    status += "-";
    status += std::to_string(GPU2D.Win1Coords[3]);
    status += " active ";
    status += FormatHex(GPU2D.Win1Active, 2);

    status += "\n  master brightness: ";
    status += FormatHex(masterBright, 4);
    status += " mode ";
    status += std::to_string((masterBright >> 14) & 0x3u);
    status += " factor ";
    status += std::to_string(std::min<u32>(masterBright & 0x1Fu, 16u));
    status += "\n  capture: enabled ";
    status += yesNo(GPU.CaptureEnable);
    status += "; CAPCNT ";
    status += FormatHex(capCnt, 8);
    status += "; dst mode ";
    status += std::to_string(capDstMode);
    status += "; srcA ";
    status += std::to_string(capSrcA);
    status += "; srcB ";
    status += std::to_string(capSrcB);
    status += "; dst bank/off ";
    status += std::to_string(capDstBank);
    status += "/";
    status += std::to_string(capDstOffset);
    status += "; size ";
    status += std::to_string(capSize);
    status += "\n  screen routing: swap ";
    status += yesNo(GPU.ScreenSwap);
    status += "; this engine final bottom ";
    status += yesNo(Parent.IsEngineRoutedToFinalBottom(GPU2D.Num));
}

void GLRenderer2D::AppendWholeSceneBitmapFMVTrace(std::string& status) const
{
    auto yesNo = [](bool value) { return value ? "yes" : "no"; };
    auto appendMask = [&status](u32 mask)
    {
        status += FormatHex(mask, 1);
    };
    auto appendRange = [&status](u32 start, u32 size)
    {
        if (start == 0xFFFFFFFF || size == 0xFFFFFFFF)
        {
            status += "none";
            return;
        }

        status += FormatHex(start, 6);
        status += "+";
        status += FormatHex(size, 6);
    };
    auto appendEventMaskList = [&status](const char* label,
                                         const int* lines,
                                         const u8* masks,
                                         u32 count,
                                         u32 overflow)
    {
        status += "\n    ";
        status += label;
        status += ": ";
        if (!count)
        {
            status += "none";
            return;
        }

        const u32 shown = std::min<u32>(count, kWholeSceneDebugRangeRecordLimit);
        for (u32 i = 0; i < shown; i++)
        {
            if (i)
                status += ", ";
            status += "L";
            status += std::to_string(lines[i]);
            status += "=";
            status += FormatHex(masks[i], 1);
        }
        if (overflow)
        {
            status += ", +";
            status += std::to_string(overflow);
            status += " more";
        }
    };
    auto appendLayerRows = [&status](const int firstRows[4], const int lastRows[4])
    {
        bool any = false;
        for (int layer = 0; layer < 4; layer++)
        {
            if (firstRows[layer] < 0 || lastRows[layer] <= firstRows[layer])
                continue;

            if (any)
                status += ", ";
            status += "BG";
            status += std::to_string(layer);
            status += ":";
            status += std::to_string(firstRows[layer]);
            status += "-";
            status += std::to_string(lastRows[layer]);
            any = true;
        }
        if (!any)
            status += "none";
    };
    auto appendFrameTrace = [&](const char* label, const WholeSceneUpdateDebugTrace& trace)
    {
        status += "\n  ";
        status += label;
        status += ":";
        status += "\n    layer dirty events: ";
        status += std::to_string(trace.LayerDirtyEvents);
        status += "; mask ";
        appendMask(trace.LayerDirtyMask);
        appendEventMaskList("layer dirty lines", trace.LayerDirtyLine,
                            trace.LayerDirtyEventMask,
                            trace.LayerDirtyEvents,
                            trace.LayerDirtyOverflow);
        status += "\n    state dirty events: ";
        status += std::to_string(trace.StateDirtyEvents);
        status += "; reason ";
        appendMask(trace.StateDirtyReasonMask);
        status += "; DISPCNT diff ";
        status += FormatHex(trace.StateDirtyDispCntDiff, 8);
        status += "; layer diff ";
        appendMask(trace.StateDirtyLayerEnableDiff);
        status += "; BGCNT diffs";
        for (int layer = 0; layer < 4; layer++)
        {
            status += " BG";
            status += std::to_string(layer);
            status += "=";
            status += FormatHex(trace.StateDirtyBGCntDiff[layer], 4);
        }
        status += "; misc ";
        appendMask(trace.StateDirtyMiscDiffMask);
        status += "\n    full-frame unsafe events: ";
        status += std::to_string(trace.FullFrameUnsafeEvents);
        status += "; reason ";
        appendMask(trace.FullFrameUnsafeReasonMask);
        status += "; lines ";
        if (trace.FullFrameUnsafeFirstLine >= 0)
        {
            status += std::to_string(trace.FullFrameUnsafeFirstLine);
            status += "-";
            status += std::to_string(trace.FullFrameUnsafeLastLine);
        }
        else
            status += "none";
        status += "; layer mask ";
        appendMask(trace.FullFrameUnsafeLayerMask);
        status += "; DISPCNT diff ";
        status += FormatHex(trace.FullFrameUnsafeDispCntDiff, 8);
        status += "; layer diff ";
        appendMask(trace.FullFrameUnsafeLayerEnableDiff);
        status += "; BGCNT diffs";
        for (int layer = 0; layer < 4; layer++)
        {
            status += " BG";
            status += std::to_string(layer);
            status += "=";
            status += FormatHex(trace.FullFrameUnsafeBGCntDiff[layer], 4);
        }
        status += "; misc ";
        appendMask(trace.FullFrameUnsafeMiscDiffMask);
        status += "\n    dirty reasons: register ";
        appendMask(trace.RegisterLayerDirtyMask);
        status += ", VRAM ";
        appendMask(trace.VRAMLayerDirtyMask);
        status += ", palette ";
        appendMask(trace.PaletteLayerDirtyMask);
        status += ", deferred ";
        appendMask(trace.DeferredLayerDirtyMask);
        status += ", inactive/covered deferred ";
        appendMask(trace.InactiveDeferredLayerDirtyMask);
        status += "\n    bitmap visibility masks: covered ";
        appendMask(trace.CoveredBitmapMask);
        status += ", contributing ";
        appendMask(trace.ContributingLayerMask);
        status += ", visible dirty ";
        appendMask(trace.VisibleBitmapDirtyMask);
        status += ", visible VRAM dirty ";
        appendMask(trace.VisibleBitmapVRAMDirtyMask);
        status += "\n    visible bitmap dirty rows: ";
        if (trace.VisibleBitmapDirtyFirstRow >= 0)
        {
            status += std::to_string(trace.VisibleBitmapDirtyFirstRow);
            status += "-";
            status += std::to_string(trace.VisibleBitmapDirtyLastRow);
        }
        else
            status += "none";
        status += "; per layer ";
        appendLayerRows(trace.VisibleBitmapLayerFirstRow, trace.VisibleBitmapLayerLastRow);
        status += "; row-limited prerender ";
        appendMask(trace.VisibleBitmapRowLimitedPrerenderMask);
        status += " ";
        appendLayerRows(trace.VisibleBitmapRowLimitedFirstRow, trace.VisibleBitmapRowLimitedLastRow);
        status += "; covered dirty deferred ";
        appendMask(trace.VisibleBitmapCoveredDirtyDeferredMask);
        status += "; before/current-line ";
        appendMask(trace.VisibleBitmapDirtyBeforeLineMask);
        status += "; after/current-line ";
        appendMask(trace.VisibleBitmapDirtyAfterLineMask);
        status += "; crosses/current-line ";
        appendMask(trace.VisibleBitmapDirtyCrossesLineMask);

        status += "\n    BG upload calls: ";
        status += std::to_string(trace.BGUploadCalls);
        status += "; ranges ";
        status += std::to_string(trace.BGUploadRangeCount);
        status += "; VRAM texture rows ";
        status += std::to_string(trace.BGUploadRows);
        if (trace.BGUploadRangeCount)
        {
            status += "; row span ";
            status += std::to_string(trace.BGUploadFirstRow);
            status += "-";
            status += std::to_string(trace.BGUploadLastRow);
        }

        status += "\n    BG upload ranges: ";
        if (!trace.BGUploadRangeCount)
            status += "none";
        else
        {
            const u32 shown = std::min<u32>(trace.BGUploadRangeCount, kWholeSceneDebugRangeRecordLimit);
            for (u32 i = 0; i < shown; i++)
            {
                if (i)
                    status += ", ";
                status += "L";
                status += std::to_string(trace.BGUploadLine[i]);
                status += ":";
                status += std::to_string(trace.BGUploadStartRow[i]);
                status += "-";
                status += std::to_string(trace.BGUploadEndRow[i]);
            }
            if (trace.BGUploadRangeOverflow)
            {
                status += ", +";
                status += std::to_string(trace.BGUploadRangeOverflow);
                status += " more";
            }
        }

        status += "\n    layer prerender calls: ";
        status += std::to_string(trace.LayerPrerenderCalls);
        status += "; mask ";
        appendMask(trace.LayerPrerenderMask);
        status += "; visible bitmap mask ";
        appendMask(trace.LayerPrerenderBitmapMask);
        appendEventMaskList("layer prerender lines", trace.LayerPrerenderLine,
                            trace.LayerPrerenderEventMask,
                            trace.LayerPrerenderCalls,
                            trace.LayerPrerenderOverflow);

        status += "\n    partial composite ranges: ";
        if (!trace.PartialCompositeRangeCount)
            status += "none";
        else
        {
            const u32 shown = std::min<u32>(trace.PartialCompositeRangeCount, kWholeSceneDebugRangeRecordLimit);
            for (u32 i = 0; i < shown; i++)
            {
                if (i)
                    status += ", ";
                status += std::to_string(trace.PartialCompositeStart[i]);
                status += "-";
                status += std::to_string(trace.PartialCompositeEnd[i]);
            }
            if (trace.PartialCompositeRangeOverflow)
            {
                status += ", +";
                status += std::to_string(trace.PartialCompositeRangeOverflow);
                status += " more";
            }
        }
    };

    status += "\n\nBitmap/FMV source trace:";
    status += "\n  visible bitmap BG mask: ";
    appendMask(VisibleBitmapBGLayerMask());
    status += "; streaming-bitmap candidate: ";
    status += yesNo(IsWholeSceneStreamingBitmapFragmentationCandidate());
    status += "; deferred prerender mask: ";
    appendMask(DeferredLayerPrerenderDirty);

    for (int layer = 0; layer < 4; layer++)
    {
        const auto& cfg = LayerConfig.uBGConfig[layer];
        const bool enabled = (LayerEnable & (1 << layer)) != 0;
        const bool bitmapLike = IsBitmapLikeBGLayer(layer);
        if (!enabled && !bitmapLike)
            continue;

        status += "\n  BG";
        status += std::to_string(layer);
        status += ": enabled ";
        status += yesNo(enabled);
        status += "; type ";
        status += BGLayerTypeName(cfg.Type);
        status += " (";
        status += std::to_string(cfg.Type);
        status += ")";
        status += "; size ";
        status += std::to_string(cfg.Size[0]);
        status += "x";
        status += std::to_string(cfg.Size[1]);
        status += "; full-screen bitmap ";
        status += yesNo(IsFullScreenBitmapBGLayer(layer));
        status += "; capture-backed ";
        status += yesNo(cfg.Type >= 7);
        status += "; BGCNT ";
        status += FormatHex(BGCnt[layer], 4);
        status += "; tile/map ";
        status += FormatHex(cfg.TileOffset, 6);
        status += "/";
        status += FormatHex(cfg.MapOffset, 6);
        status += "; VRAM ranges tile ";
        appendRange(BGVRAMRange[layer][0], BGVRAMRange[layer][1]);
        status += " map ";
        appendRange(BGVRAMRange[layer][2], BGVRAMRange[layer][3]);
    }

    appendFrameTrace("current frame update trace", WholeSceneCurrentUpdateDebugTrace);
    appendFrameTrace("previous frame update trace", WholeScenePreviousUpdateDebugTrace);
}

void GLRenderer2D::AppendWholeSceneTextureStatsTrace(std::string& status) const
{
    if (WholeSceneTrace.Path != WholeSceneRenderPath::FinalNativeUpscale &&
        WholeSceneTrace.Path != WholeSceneRenderPath::OverlayOperatorUpscale &&
        WholeSceneTrace.Path != WholeSceneRenderPath::ConservativeHybridUpscale)
        return;

    status += "\n\n3D content trace:";
    status += FormatTextureContentStats("Parent.OutputTex3D", Parent.OutputTex3D);
    if (WholeSceneTrace.Native3DResolveValid)
    {
        status += FormatTextureContentStats("NativeDirect3DTex", NativeDirect3DTex);
        status += FormatTextureContentStats("NativeDirect3DSemanticsTex", NativeDirect3DSemanticsTex);
        status += FormatTextureContentStats("NativeDirect3DCompositorTex", NativeDirect3DCompositorTex);
    }
    if (WholeSceneTrace.NativeExactFinalValid)
        status += FormatTextureContentStats("NativeExactFinalTex", NativeExactFinalTex);
    if (WholeSceneTrace.OverlayTrueFinalValid)
        status += FormatTextureContentStats("NativeOverlayTrueFinalTex", NativeOverlayTrueFinalTex);
    if (WholeSceneTrace.Path == WholeSceneRenderPath::ConservativeHybridUpscale)
    {
        status += FormatTextureContentStats("HybridForegroundTex", HybridForegroundTex);
        if (WholeSceneScaleHybridForeground2DBase)
            status += FormatTextureContentStats("Hybrid2DBaseTex", Hybrid2DBaseTex);
        if (WholeSceneScaleHybridCleanLegacyCandidate)
            status += FormatTextureContentStats("HybridLegacyCandidateTex", HybridLegacyCandidateTex);
        status += FormatTextureContentStats("HybridNativeFallbackTex", HybridNativeFallbackTex);
    }
}

std::string GLRenderer2D::DescribeWholeSceneScaleState() const
{
    auto withCoreTrace = [this](std::string status)
    {
        AppendWholeSceneRenderTrace(status);
        AppendWholeSceneDisplayStateTrace(status);
        AppendWholeSceneBitmapFMVTrace(status);
        return status;
    };

    if (!WholeSceneScaleRequested)
        return withCoreTrace("Whole-scene 2D scaling is disabled.");

    switch (WholeSceneScaleState)
    {
    case WholeSceneScaleEligibility::ScreenUnavailable:
        return withCoreTrace("The selected screen is currently unavailable for whole-scene 2D scaling.");
    case WholeSceneScaleEligibility::EngineANotSupportedYet:
        return withCoreTrace("The main 2D engine path is not supported for this debug view yet.");
    case WholeSceneScaleEligibility::MainEngineVRAMDisplay:
        return withCoreTrace("Main-engine VRAM display mode bypasses the whole-scene 2D path.");
    case WholeSceneScaleEligibility::MainEngineDisplayFIFO:
        return withCoreTrace("Display FIFO mode bypasses the whole-scene 2D path.");
    case WholeSceneScaleEligibility::CaptureActive:
        return withCoreTrace("Active display capture bypasses the whole-scene 2D path.");
    case WholeSceneScaleEligibility::CaptureBackedBG:
        return withCoreTrace("Capture-backed BG layers bypass the whole-scene 2D path.");
    case WholeSceneScaleEligibility::CaptureBackedOBJ:
        return withCoreTrace("Capture-backed OBJ layers bypass the whole-scene 2D path.");
    case WholeSceneScaleEligibility::UnsupportedDisplayMode:
        return withCoreTrace("The current display mode bypasses the whole-scene 2D path.");
    case WholeSceneScaleEligibility::Eligible:
    {
        std::string status = "Whole-scene 2D scaling is active.";
        AppendWholeSceneModeStatus(status);
        AppendWholeSceneRenderTrace(status);
        AppendWholeSceneDisplayStateTrace(status);
        AppendWholeSceneBitmapFMVTrace(status);
        AppendWholeSceneTextureStatsTrace(status);
        return status;
    }
    }

    return withCoreTrace("Whole-scene 2D scaling state is unknown.");
}

bool GLRenderer2D::ReadWholeSceneDebugView(WholeScene2DDebugView view,
                                           int& width,
                                           int& height,
                                           std::vector<u32>& rgba,
                                           std::string* status) const
{
    width = 0;
    height = 0;
    rgba.clear();

    GLuint texture = 0;
    GLenum textureTarget = GL_TEXTURE_2D;
    int textureLayer = -1;
    GLuint stackRoleMetaTexture = 0;
    GLuint stackRole3DTexture = 0;
    GLuint stackRole3DColorTexture = 0;
    GLuint stackRoleTopColorTexture = 0;
    bool stackRole3DUsesSemantics = false;
    bool stackRoleUpscaled = false;
    bool decodeMeta = false;
    bool decode3DStackRole = false;
    bool decodeCoverage = false;
    bool decodeBGMeta = false;
    bool decodeOBJFlags = false;
    bool decodeOBJCoverage = false;
    bool decodeAlphaCoverage = false;
    bool decodeSandwichProbe = false;
    int sandwichProbeMode = 0;
    bool useTextureLevelSize = false;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int nativeLayerDebugLayer = -1;

    switch (view)
    {
    case WholeScene2DDebugView::NativeFinal:
        texture = NativeOutputTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::NativeExactFinal:
        if (!WholeSceneTrace.NativeExactFinalValid)
        {
            if (status)
                *status = "The exact native final texture was not generated by the current whole-scene mode this frame.";
            return false;
        }
        texture = NativeExactFinalTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::Native3DResolve:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Native 3D resolve is only available for the main screen (A).";
            return false;
        }
        if (!WholeSceneTrace.Native3DResolveValid)
        {
            if (status)
                *status = "The native 3D resolve texture was not generated this frame. It is only used when high-resolution 3D is resolved before final-native or overlay compositing.";
            return false;
        }
        texture = NativeDirect3DTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::Native3DSemantics:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Native 3D semantics are only available for the main screen (A).";
            return false;
        }
        if (!WholeSceneTrace.Native3DSemanticsValid)
        {
            if (status)
                *status = "The native 3D semantics texture was not generated this frame. It is only used when high-resolution 3D is resolved before final-native or overlay compositing.";
            return false;
        }
        texture = NativeDirect3DSemanticsTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::FinalNative3DInput:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Final-native 3D input is only available for the main screen (A).";
            return false;
        }
        switch (WholeSceneTrace.Native3DSource)
        {
        case WholeSceneNative3DSource::NativeRendered:
            texture = Parent.OutputTex3D;
            useTextureLevelSize = true;
            break;
        case WholeSceneNative3DSource::HighResResolved:
            texture = NativeDirect3DCompositorTex;
            width = 256;
            height = 192;
            break;
        case WholeSceneNative3DSource::None:
        default:
            if (status)
                *status = "No native-stage 3D input was generated by the current whole-scene mode this frame.";
            return false;
        }
        break;
    case WholeScene2DDebugView::NativeTopColor:
        texture = NativeTopColorTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::NativeSecondColor:
        texture = NativeSecondColorTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::NativeMeta:
        texture = NativeMetaTex;
        width = 256;
        height = 192;
        decodeMeta = true;
        break;
    case WholeScene2DDebugView::NativeBG0Color:
    case WholeScene2DDebugView::NativeBG1Color:
    case WholeScene2DDebugView::NativeBG2Color:
    case WholeScene2DDebugView::NativeBG3Color:
    case WholeScene2DDebugView::NativeOBJColor:
    case WholeScene2DDebugView::NativeOBJFlags:
    case WholeScene2DDebugView::NativeOBJCoverage:
    {
        nativeLayerDebugLayer = NativeLayerDebugIndex(view);
        texture = NativeLayerDebugTex;
        width = 256;
        height = 192;
        decodeOBJFlags = view == WholeScene2DDebugView::NativeOBJFlags;
        break;
    }
    case WholeScene2DDebugView::Native3DStackRole:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Native 3D stack role is only meaningful for the main screen (A).";
            return false;
        }
        texture = NativeMetaTex;
        if (WholeSceneTrace.Native3DSemanticsValid)
        {
            stackRole3DTexture = NativeDirect3DSemanticsTex;
            stackRole3DColorTexture = NativeDirect3DCompositorTex;
            stackRole3DUsesSemantics = true;
        }
        else if (WholeSceneTrace.Native3DResolveValid)
        {
            stackRole3DTexture = NativeDirect3DTex;
            stackRole3DColorTexture = NativeDirect3DCompositorTex;
        }
        else
        {
            stackRole3DTexture = Parent.OutputTex3D;
            stackRole3DColorTexture = Parent.OutputTex3D;
        }
        stackRoleTopColorTexture = NativeTopColorTex;
        width = 256;
        height = 192;
        decode3DStackRole = true;
        break;
    case WholeScene2DDebugView::Upscaled3DStackRole:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Upscaled 3D stack role is only meaningful for the main screen (A).";
            return false;
        }
        texture = UpscaledMetaTex;
        stackRoleMetaTexture = NativeMetaTex;
        stackRole3DTexture = Parent.OutputTex3D;
        stackRole3DColorTexture = Parent.OutputTex3D;
        stackRoleTopColorTexture = NativeTopColorTex;
        width = ScreenW;
        height = ScreenH;
        decode3DStackRole = true;
        stackRoleUpscaled = true;
        break;
    case WholeScene2DDebugView::UpscaledTopColor:
        texture = UpscaledTopColorTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::UpscaledSecondColor:
        texture = UpscaledSecondColorTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::UpscaledMeta:
        texture = UpscaledMetaTex;
        width = ScreenW;
        height = ScreenH;
        decodeMeta = true;
        break;
    case WholeScene2DDebugView::UpscaledCoverage:
        texture = UpscaledCoverageTex;
        width = ScreenW;
        height = ScreenH;
        decodeCoverage = true;
        break;
    case WholeScene2DDebugView::HighResBG0Color:
    case WholeScene2DDebugView::HighResBG1Color:
    case WholeScene2DDebugView::HighResBG2Color:
    case WholeScene2DDebugView::HighResBG3Color:
    {
        int bg = HighResDebugBGIndex(view);
        texture = BGLayerTex[bg];
        useTextureLevelSize = true;
        logicalWidth = LayerConfig.uBGConfig[bg].Size[0];
        logicalHeight = LayerConfig.uBGConfig[bg].Size[1];
        break;
    }
    case WholeScene2DDebugView::HighResBG0Meta:
    case WholeScene2DDebugView::HighResBG1Meta:
    case WholeScene2DDebugView::HighResBG2Meta:
    case WholeScene2DDebugView::HighResBG3Meta:
    {
        int bg = HighResDebugBGIndex(view);
        texture = BGLayerMetaTex[bg];
        useTextureLevelSize = true;
        logicalWidth = LayerConfig.uBGConfig[bg].Size[0];
        logicalHeight = LayerConfig.uBGConfig[bg].Size[1];
        decodeBGMeta = true;
        break;
    }
    case WholeScene2DDebugView::HighResOBJColor:
        texture = NativeOBJLayerTex;
        textureTarget = GL_TEXTURE_2D_ARRAY;
        textureLayer = 0;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::HighResOBJFlags:
        texture = NativeOBJLayerTex;
        textureTarget = GL_TEXTURE_2D_ARRAY;
        textureLayer = 1;
        width = 256;
        height = 192;
        decodeOBJFlags = true;
        break;
    case WholeScene2DDebugView::HighResOBJCoverage:
        texture = NativeOBJLayerTex;
        textureTarget = GL_TEXTURE_2D_ARRAY;
        textureLayer = 0;
        width = 256;
        height = 192;
        decodeAlphaCoverage = true;
        break;
    case WholeScene2DDebugView::Direct3D:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Direct 3D is only composited through the main screen (A).";
            return false;
        }
        texture = Parent.OutputTex3D;
        useTextureLevelSize = true;
        break;
    case WholeScene2DDebugView::OverlayOperatorColor:
        texture = UpscaledExactFinalTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::OverlayUnderlayWeight:
        texture = UpscaledOverlayUnderlayWeightTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::OverlayReconstructedNative:
        texture = NativeOverlayReconstructedTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::OverlayReconstructionError:
        texture = NativeOverlayErrorTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::OverlayValidityConfidence:
        texture = NativeOverlayConfidenceTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::OverlayOwnershipReason:
        texture = UpscaledOverlayOwnershipTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::HybridSelector:
        if (WholeSceneTrace.Path != WholeSceneRenderPath::ConservativeHybridUpscale)
        {
            if (status)
                *status = "Hybrid selector debug requires conservative hybrid overlay upscale mode.";
            return false;
        }
        texture = HybridSelectorTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::HybridCoverageMiss:
        if (WholeSceneTrace.Path != WholeSceneRenderPath::ConservativeHybridUpscale)
        {
            if (status)
                *status = "Hybrid coverage miss debug requires conservative hybrid overlay upscale mode.";
            return false;
        }
        texture = HybridCoverageMissTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::HybridForegroundAlpha:
        if (WholeSceneTrace.Path != WholeSceneRenderPath::ConservativeHybridUpscale)
        {
            if (status)
                *status = "Hybrid foreground alpha debug requires conservative hybrid overlay upscale mode.";
            return false;
        }
        texture = HybridForegroundAlphaTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::HybridFinalSource:
        if (WholeSceneTrace.Path != WholeSceneRenderPath::ConservativeHybridUpscale)
        {
            if (status)
                *status = "Hybrid final source debug requires conservative hybrid overlay upscale mode.";
            return false;
        }
        texture = HybridFinalSourceTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::SandwichLower2D:
    case WholeScene2DDebugView::SandwichUpper2D:
    case WholeScene2DDebugView::SandwichEligibility:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "Sandwich probe debug is only meaningful for the main screen (A).";
            return false;
        }
        texture = UpscaledMetaTex;
        width = ScreenW;
        height = ScreenH;
        decodeSandwichProbe = true;
        sandwichProbeMode = (view == WholeScene2DDebugView::SandwichUpper2D) ? 1 :
                            (view == WholeScene2DDebugView::SandwichEligibility) ? 2 : 0;
        break;
    case WholeScene2DDebugView::OverlayEnhancedUnderlay:
        if (GPU2D.Num != 0)
        {
            if (status)
                *status = "The enhanced underlay is only available for the main screen (A).";
            return false;
        }
        texture = Parent.OutputTex3D;
        useTextureLevelSize = true;
        break;
    case WholeScene2DDebugView::OverlayTrueNativeFinal:
        if (!WholeSceneTrace.OverlayTrueFinalValid)
        {
            if (status)
                *status = "The overlay true native final texture was not generated this frame.";
            return false;
        }
        texture = NativeOverlayTrueFinalTex;
        width = 256;
        height = 192;
        break;
    case WholeScene2DDebugView::OverlayFinalResult:
        texture = OutputTex;
        width = ScreenW;
        height = ScreenH;
        break;
    case WholeScene2DDebugView::FinalTop:
    case WholeScene2DDebugView::FinalBottom:
    case WholeScene2DDebugView::MainVRAMDisplayRaw:
        if (status)
            *status = "Final physical and main VRAM display views are read after the GL final pass.";
        return false;
    }

    const bool requiresWholeScene = view != WholeScene2DDebugView::Direct3D;
    const bool highResDebugView = IsHighResDebugView(view);
    const bool nativeLayerDebugView = IsNativeLayerDebugView(view);
    const bool overlayDebugView = IsOverlayDebugView(view);
    if (requiresWholeScene && !CanUseWholeSceneScalePath())
    {
        if (status)
            *status = DescribeWholeSceneScaleState();
        return false;
    }

    if (highResDebugView && !CanUseWholeSceneHighResPath())
    {
        if (status)
            *status = "High-resolution compositor debug views require high-resolution compositor mode.";
        return false;
    }

    if (overlayDebugView && !CanUseWholeSceneOverlayOperatorPath())
    {
        if (status)
            *status = "Overlay operator debug views require presentation overlay upscale mode.";
        return false;
    }

    if (IsAuxiliaryWholeSceneDebugView(view) &&
        !WholeSceneDebugViewsActive.load(std::memory_order_relaxed))
    {
        if (status)
            *status = "This auxiliary debug view is generated only while the whole-scene debug dialog is active.";
        return false;
    }

    if (!highResDebugView && requiresWholeScene && CanUseWholeSceneHighResPath())
    {
        if (status)
            *status = "High-resolution compositor mode does not generate native-stack/upscaled debug textures.";
        return false;
    }

    if (nativeLayerDebugLayer >= 0)
        RenderNativeLayerDebugView(nativeLayerDebugLayer);

    if (!texture)
    {
        if (status)
            *status = "The selected debug texture is not currently available.";
        return false;
    }

    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevTextureBinding2D = 0;
    GLint prevTextureBinding2DArray = 0;
    GLint prevPackAlignment = 4;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding2D);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTextureBinding2DArray);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

    glBindTexture(textureTarget, texture);

    auto restoreReadbackState = [&]()
    {
        glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
        glBindTexture(GL_TEXTURE_2D, prevTextureBinding2D);
        glBindTexture(GL_TEXTURE_2D_ARRAY, prevTextureBinding2DArray);
        glActiveTexture(prevActiveTexture);
    };

    GLint textureWidth = 0;
    GLint textureHeight = 0;
    glGetTexLevelParameteriv(textureTarget, 0, GL_TEXTURE_WIDTH, &textureWidth);
    glGetTexLevelParameteriv(textureTarget, 0, GL_TEXTURE_HEIGHT, &textureHeight);
    if (textureWidth <= 0 || textureHeight <= 0)
    {
        restoreReadbackState();

        if (status)
            *status = "The selected debug texture is not readable in the current GL context.";
        return false;
    }

    if (useTextureLevelSize)
    {
        width = textureWidth;
        height = textureHeight;
    }
    else if (width > textureWidth || height > textureHeight)
    {
        restoreReadbackState();

        if (status)
            *status = "The selected debug texture is smaller than the expected debug view.";
        return false;
    }

    if (width <= 0 || height <= 0)
    {
        restoreReadbackState();

        if (status)
            *status = "The selected debug texture has no readable image.";
        return false;
    }

    rgba.resize(width * height);

    std::vector<u32> arrayData;
    u32* readback = rgba.data();
    if (textureTarget == GL_TEXTURE_2D_ARRAY)
    {
        arrayData.resize(width * height * 3);
        readback = arrayData.data();
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    while (glGetError() != GL_NO_ERROR) {}
    glGetTexImage(textureTarget, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    const GLenum readbackError = glGetError();
    if (readbackError != GL_NO_ERROR)
    {
        restoreReadbackState();

        if (status)
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer),
                          "Failed to read the selected debug texture (GL error 0x%04X).",
                          static_cast<unsigned int>(readbackError));
            *status = buffer;
        }
        return false;
    }

    if (textureTarget == GL_TEXTURE_2D_ARRAY)
    {
        const int layerOffset = width * height * textureLayer;
        for (int i = 0; i < width * height; i++)
            rgba[i] = arrayData[layerOffset + i];
    }

    std::vector<u32> stackRole3DRGBA;
    int stackRole3DWidth = 0;
    int stackRole3DHeight = 0;
    std::vector<u32> stackRole3DColorRGBA;
    int stackRole3DColorWidth = 0;
    int stackRole3DColorHeight = 0;
    std::vector<u32> stackRoleTopColorRGBA;
    int stackRoleTopColorWidth = 0;
    int stackRoleTopColorHeight = 0;
    std::vector<u32> stackRoleMetaRGBA;
    int stackRoleMetaWidth = 0;
    int stackRoleMetaHeight = 0;
    std::vector<u32> sandwichTopRGBA;
    std::vector<u32> sandwich2DBaseRGBA;
    std::vector<u32> sandwichNativeMetaRGBA;
    std::vector<u32> sandwichDirect3DRGBA;
    int sandwich2DBaseWidth = 0;
    int sandwich2DBaseHeight = 0;
    int sandwichNativeMetaWidth = 0;
    int sandwichNativeMetaHeight = 0;
    int sandwichDirect3DWidth = 0;
    int sandwichDirect3DHeight = 0;

    auto readTexture2D = [](GLuint sourceTexture,
                            std::vector<u32>& outRGBA,
                            int& outWidth,
                            int& outHeight) -> bool
    {
        outRGBA.clear();
        outWidth = 0;
        outHeight = 0;
        if (!sourceTexture)
            return false;

        glBindTexture(GL_TEXTURE_2D, sourceTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &outWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &outHeight);
        if (outWidth <= 0 || outHeight <= 0)
            return false;

        outRGBA.resize(outWidth * outHeight);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, outRGBA.data());
        return true;
    };

    if (decodeSandwichProbe)
    {
        int sandwichTopWidth = 0;
        int sandwichTopHeight = 0;
        readTexture2D(UpscaledTopColorTex, sandwichTopRGBA, sandwichTopWidth, sandwichTopHeight);
        readTexture2D(Hybrid2DBaseTex, sandwich2DBaseRGBA, sandwich2DBaseWidth, sandwich2DBaseHeight);
        readTexture2D(NativeMetaTex, sandwichNativeMetaRGBA, sandwichNativeMetaWidth, sandwichNativeMetaHeight);
        readTexture2D(Parent.OutputTex3D, sandwichDirect3DRGBA, sandwichDirect3DWidth, sandwichDirect3DHeight);

        if (sandwichTopWidth != width || sandwichTopHeight != height)
            sandwichTopRGBA.clear();
        if (sandwich2DBaseWidth != width || sandwich2DBaseHeight != height)
            sandwich2DBaseRGBA.clear();
    }

    if (decode3DStackRole && stackRoleMetaTexture)
    {
        glBindTexture(GL_TEXTURE_2D, stackRoleMetaTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &stackRoleMetaWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &stackRoleMetaHeight);
        if (stackRoleMetaWidth > 0 && stackRoleMetaHeight > 0)
        {
            stackRoleMetaRGBA.resize(stackRoleMetaWidth * stackRoleMetaHeight);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, stackRoleMetaRGBA.data());
        }
    }
    if (decode3DStackRole && stackRole3DColorTexture)
    {
        glBindTexture(GL_TEXTURE_2D, stackRole3DColorTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &stackRole3DColorWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &stackRole3DColorHeight);
        if (stackRole3DColorWidth > 0 && stackRole3DColorHeight > 0)
        {
            stackRole3DColorRGBA.resize(stackRole3DColorWidth * stackRole3DColorHeight);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, stackRole3DColorRGBA.data());
        }
    }
    if (decode3DStackRole && stackRoleTopColorTexture)
    {
        glBindTexture(GL_TEXTURE_2D, stackRoleTopColorTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &stackRoleTopColorWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &stackRoleTopColorHeight);
        if (stackRoleTopColorWidth > 0 && stackRoleTopColorHeight > 0)
        {
            stackRoleTopColorRGBA.resize(stackRoleTopColorWidth * stackRoleTopColorHeight);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, stackRoleTopColorRGBA.data());
        }
    }
    if (decode3DStackRole && stackRole3DTexture)
    {
        glBindTexture(GL_TEXTURE_2D, stackRole3DTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &stackRole3DWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &stackRole3DHeight);
        if (stackRole3DWidth > 0 && stackRole3DHeight > 0)
        {
            stackRole3DRGBA.resize(stackRole3DWidth * stackRole3DHeight);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, stackRole3DRGBA.data());
        }
    }

    restoreReadbackState();

    if (logicalWidth > 0 && logicalHeight > 0 &&
        logicalWidth <= width && logicalHeight <= height &&
        (logicalWidth != width || logicalHeight != height))
    {
        std::vector<u32> cropped(logicalWidth * logicalHeight);
        for (int y = 0; y < logicalHeight; y++)
            std::copy_n(rgba.begin() + y * width, logicalWidth, cropped.begin() + y * logicalWidth);

        rgba = std::move(cropped);
        width = logicalWidth;
        height = logicalHeight;
    }

    if (decodeAlphaCoverage)
    {
        for (u32& pixel : rgba)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            const u8 coverage = chan[3];
            chan[0] = coverage;
            chan[1] = coverage;
            chan[2] = coverage;
            chan[3] = 255;
        }
    }
    else if (decodeOBJCoverage)
    {
        for (u32& pixel : rgba)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            const u8 coverage = chan[0];
            chan[0] = coverage;
            chan[1] = coverage;
            chan[2] = coverage;
            chan[3] = 255;
        }
    }
    else if (decodeOBJFlags)
    {
        for (u32& pixel : rgba)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            const u8 mode = chan[0];
            const u8 mosaic = chan[1];
            const u8 objWindow = chan[2];
            const u8 priority = chan[3];
            chan[0] = mode == 1 ? 255 : (mode == 2 ? 180 : 0);
            chan[1] = mosaic ? 255 : static_cast<u8>(priority * 64);
            chan[2] = objWindow ? 255 : static_cast<u8>((3 - std::min<int>(priority, 3)) * 64);
            chan[3] = 255;
        }
    }
    else if (decodeBGMeta)
    {
        for (u32& pixel : rgba)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            const int sourceType = chan[0];
            const int sourceID = chan[1] | (chan[2] << 8) | (chan[3] << 16);
            const DebugRGB color = BGMetaDebugColorCPU(sourceType, sourceID);
            chan[0] = color.r;
            chan[1] = color.g;
            chan[2] = color.b;
            chan[3] = 255;
        }
    }
    else if (decodeCoverage)
    {
        for (u32& pixel : rgba)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            const u8 topCoverage = chan[0];
            const u8 secondCoverage = chan[1];
            chan[0] = topCoverage;
            chan[1] = secondCoverage;
            chan[2] = 0;
            chan[3] = 255;
        }
    }
    else if (decodeSandwichProbe)
    {
        if (sandwichNativeMetaRGBA.empty() || sandwichDirect3DRGBA.empty())
        {
            if (status)
                *status = "Sandwich probe inputs were not available for this frame.";
            return false;
        }

        if (sandwichProbeMode == 0 && sandwich2DBaseRGBA.empty())
        {
            if (status)
                *status = "Sandwich lower 2D candidate requires the hybrid foreground-boundary 2D base texture.";
            return false;
        }

        if (sandwichProbeMode == 1 && sandwichTopRGBA.empty())
        {
            if (status)
                *status = "Sandwich upper 2D candidate requires the upscaled top 2D texture.";
            return false;
        }

        const int direct3DPrio = (LayerEnable & (1 << 0)) ? (BGCnt[0] & 0x3) : -1;
        const bool direct3DDisplayEnabled = !GPU2D.Num && ((DispCnt & (1u << 3)) != 0) && (direct3DPrio >= 0);
        const int direct3DMask = 1 << 0;
        const int blendEffect = (BlendCnt >> 6) & 0x3;

        int countEligibleForeground = 0;
        int countEligibleUpper2D = 0;
        int countNoDirect3D = 0;
        int countDirect3DAbsent = 0;
        int countHidden = 0;
        int countNativeEffect = 0;
        int countEffectBlocked = 0;

        auto nativeMetaAt = [&](int x, int y) -> u32
        {
            const int sampleX = std::min<int>(
                sandwichNativeMetaWidth - 1,
                std::max<int>(0, (x * sandwichNativeMetaWidth) / std::max<int>(width, 1)));
            const int sampleY = std::min<int>(
                sandwichNativeMetaHeight - 1,
                std::max<int>(0, (y * sandwichNativeMetaHeight) / std::max<int>(height, 1)));
            return sandwichNativeMetaRGBA[sampleY * sandwichNativeMetaWidth + sampleX];
        };

        auto direct3DPresentAt = [&](int x, int y) -> bool
        {
            const int line = std::min<int>(191, std::max<int>(0, (y * 192) / std::max<int>(height, 1)));
            const int direct3DScale = std::max<int>(1, width / 256);
            const int directX = x + (ScanlineConfig.uScanline[line].BGOffset[0][0] * direct3DScale);
            const int directY = y;
            if (directX < 0 || directX >= width || directY < 0 || directY >= height)
                return false;

            const int sampleX = std::min<int>(
                sandwichDirect3DWidth - 1,
                std::max<int>(0, ((directX * 2 + 1) * sandwichDirect3DWidth) / std::max<int>(width * 2, 1)));
            const int sampleY = std::min<int>(
                sandwichDirect3DHeight - 1,
                std::max<int>(0, ((directY * 2 + 1) * sandwichDirect3DHeight) / std::max<int>(height * 2, 1)));
            const u32 directPixel = sandwichDirect3DRGBA[sampleY * sandwichDirect3DWidth + sampleX];
            const u8* directChan = reinterpret_cast<const u8*>(&directPixel);
            return directChan[3] != 0;
        };

        auto writeDebugColor = [](u32& pixel, DebugRGB color)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            chan[0] = color.r;
            chan[1] = color.g;
            chan[2] = color.b;
            chan[3] = 255;
        };

        auto clearPixel = [](u32& pixel)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            chan[0] = 0;
            chan[1] = 0;
            chan[2] = 0;
            chan[3] = 255;
        };

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                u32& pixel = rgba[y * width + x];
                const u32 metaPixel = nativeMetaAt(x, y);
                const u8* metaChan = reinterpret_cast<const u8*>(&metaPixel);
                const int sourceMask1 = metaChan[0];
                const int sourceMask2 = metaChan[1];
                const int packedInfo = metaChan[2];
                const int packedFlags = metaChan[3];
                const int sourcePrio1 = (packedInfo >> 2) & 0x7;
                const int sourcePrio2 = (packedInfo >> 5) & 0x7;
                const int nativeEffect = (packedFlags >> 2) & 0x7;
                const bool bg0Allowed = (packedFlags & 0x1) != 0;
                const bool blendAllowed = (packedFlags & 0x2) != 0;
                const bool direct3DEnabled = direct3DDisplayEnabled && bg0Allowed;
                const bool direct3DPresent = direct3DEnabled && direct3DPresentAt(x, y);
                const bool direct3DTop = direct3DPresent &&
                    IsInFrontCPU(direct3DPrio, direct3DMask, sourcePrio1, sourceMask1);
                const bool direct3DSecond = direct3DPresent && !direct3DTop &&
                    IsInFrontCPU(direct3DPrio, direct3DMask, sourcePrio2, sourceMask2);

                int stackMask1 = sourceMask1;
                int stackMask2 = sourceMask2;
                int stackSpecialType = packedInfo & 0x3;
                if (direct3DTop)
                {
                    stackMask2 = stackMask1;
                    stackMask1 = direct3DMask;
                    stackSpecialType = 1;
                }
                else if (direct3DSecond)
                {
                    stackMask2 = direct3DMask;
                }

                const DebugStackEffectInfo effectInfo = ResolveStackEffectInfoCPU(
                    BlendCnt,
                    blendEffect,
                    blendAllowed,
                    stackSpecialType,
                    stackMask1,
                    stackMask2,
                    {0, 0, 0, 0x1F},
                    EVA,
                    EVB,
                    EVY);
                const bool direct3DEffectAllowed =
                    effectInfo.effect == 0 ||
                    (direct3DTop && (effectInfo.effect == 2 ||
                                     effectInfo.effect == 3 ||
                                     effectInfo.effect == 4));
                const bool eligible =
                    direct3DPresent &&
                    (direct3DTop || direct3DSecond) &&
                    nativeEffect == 0 &&
                    direct3DEffectAllowed;

                if (sandwichProbeMode == 0)
                {
                    if (eligible && direct3DTop)
                        pixel = sandwich2DBaseRGBA[y * width + x];
                    else
                        clearPixel(pixel);
                    continue;
                }

                if (sandwichProbeMode == 1)
                {
                    if (eligible && direct3DSecond)
                        pixel = sandwichTopRGBA[y * width + x];
                    else
                        clearPixel(pixel);
                    continue;
                }

                if (!direct3DEnabled)
                {
                    writeDebugColor(pixel, {70, 70, 70});
                    countNoDirect3D++;
                }
                else if (!direct3DPresent)
                {
                    writeDebugColor(pixel, {24, 64, 160});
                    countDirect3DAbsent++;
                }
                else if (nativeEffect != 0)
                {
                    writeDebugColor(pixel, {255, 220, 0});
                    countNativeEffect++;
                }
                else if (!direct3DEffectAllowed)
                {
                    writeDebugColor(pixel, {255, 128, 0});
                    countEffectBlocked++;
                }
                else if (direct3DTop)
                {
                    writeDebugColor(pixel, {0, 220, 80});
                    countEligibleForeground++;
                }
                else if (direct3DSecond)
                {
                    writeDebugColor(pixel, {0, 220, 255});
                    countEligibleUpper2D++;
                }
                else
                {
                    writeDebugColor(pixel, {120, 0, 200});
                    countHidden++;
                }
            }
        }

        if (status)
        {
            if (sandwichProbeMode == 0)
                *status = "Sandwich lower 2D candidate. Shows the hybrid high-resolution 2D base below foreground Direct3D where the stack looks sandwich-eligible; black means ineligible or no lower candidate.\n";
            else if (sandwichProbeMode == 1)
                *status = "Sandwich upper 2D candidate. Shows the high-resolution 2D color above Direct3D where the stack looks sandwich-eligible; black means no upper 2D candidate or ineligible.\n";
            else
                *status = "Sandwich eligibility mask. Green = Direct3D over lower 2D, cyan = upper 2D over Direct3D, blue = Direct3D absent, purple = hidden/unknown, yellow/orange = native/effect hazard, gray = no Direct3D/window-excluded.\n";

            *status += "Eligibility counts: foreground ";
            *status += std::to_string(countEligibleForeground);
            *status += ", upper-2D-over-3D ";
            *status += std::to_string(countEligibleUpper2D);
            *status += ", Direct3D absent ";
            *status += std::to_string(countDirect3DAbsent);
            *status += ", hidden/unknown ";
            *status += std::to_string(countHidden);
            *status += ", no Direct3D/window-excluded ";
            *status += std::to_string(countNoDirect3D);
            *status += ", native-effect ";
            *status += std::to_string(countNativeEffect);
            *status += ", effect-blocked ";
            *status += std::to_string(countEffectBlocked);
            *status += ".\n";
        }
    }
    else if (decode3DStackRole)
    {
        int countNoDirect3D = 0;
        int countDirect3DFront = 0;
        int count2DAbove3D = 0;
        int countDirect3DHidden = 0;
        int countNativeEffect = 0;
        int countDirect3DEffect = 0;
        int countDirect3DEffectActive = 0;
        int countDirect3DEffectNeutral = 0;
        int countDirect3DEffectUnknown = 0;
        const int direct3DPrio = (LayerEnable & (1 << 0)) ? (BGCnt[0] & 0x3) : -1;
        const bool direct3DDisplayEnabled = !GPU2D.Num && ((DispCnt & (1u << 3)) != 0) && (direct3DPrio >= 0);
        const int direct3DMask = 1 << 0;
        const int blendEffect = (BlendCnt >> 6) & 0x3;
        const int blendTarget1 = BlendCnt & 0x3F;
        const int blendTarget2 = (BlendCnt >> 8) & 0x3F;
        const bool nativeEffectStateActive = (blendEffect == 1)
            ? ((blendTarget1 != 0) && (blendTarget2 != 0))
            : (((blendEffect == 2) || (blendEffect == 3)) && (blendTarget1 != 0));

        auto direct3DTexturePixelAt = [&](const std::vector<u32>& texRGBA,
                                          int texWidth,
                                          int texHeight,
                                          int x,
                                          int y,
                                          u32& sample) -> bool
        {
            if (texRGBA.empty() || texWidth <= 0 || texHeight <= 0)
                return false;

            const int line = std::min<int>(191, std::max<int>(0, (y * 192) / std::max<int>(height, 1)));
            const int direct3DScale = stackRoleUpscaled ? std::max<int>(1, width / 256) : 1;
            const int directX = x + (ScanlineConfig.uScanline[line].BGOffset[0][0] * direct3DScale);
            const int directY = y;
            if (directX < 0 || directX >= width || directY < 0 || directY >= height)
                return false;

            const int sampleX = std::min<int>(texWidth - 1, std::max<int>(0, ((directX * 2 + 1) * texWidth) / std::max<int>(width * 2, 1)));
            const int sampleY = std::min<int>(texHeight - 1, std::max<int>(0, ((directY * 2 + 1) * texHeight) / std::max<int>(height * 2, 1)));
            sample = texRGBA[sampleY * texWidth + sampleX];
            return true;
        };

        auto direct3DPresentAt = [&](int x, int y) -> bool
        {
            if (stackRole3DRGBA.empty() || stackRole3DWidth <= 0 || stackRole3DHeight <= 0)
                return false;

            const int line = std::min<int>(191, std::max<int>(0, (y * 192) / std::max<int>(height, 1)));
            const int direct3DScale = stackRoleUpscaled ? std::max<int>(1, width / 256) : 1;
            const int directX = x + (ScanlineConfig.uScanline[line].BGOffset[0][0] * direct3DScale);
            const int directY = y;
            if (directX < 0 || directX >= width || directY < 0 || directY >= height)
                return false;

            const int x0 = std::min<int>(stackRole3DWidth - 1, std::max<int>(0, (directX * stackRole3DWidth) / width));
            const int y0 = std::min<int>(stackRole3DHeight - 1, std::max<int>(0, (directY * stackRole3DHeight) / height));
            const int x1 = std::min<int>(stackRole3DWidth, std::max<int>(x0 + 1, ((directX + 1) * stackRole3DWidth + width - 1) / width));
            const int y1 = std::min<int>(stackRole3DHeight, std::max<int>(y0 + 1, ((directY + 1) * stackRole3DHeight + height - 1) / height));

            for (int sampleY = y0; sampleY < y1; sampleY++)
            {
                for (int sampleX = x0; sampleX < x1; sampleX++)
                {
                    const u32 sample = stackRole3DRGBA[sampleY * stackRole3DWidth + sampleX];
                    const u8* sampleChan = reinterpret_cast<const u8*>(&sample);
                    const u8 presence = stackRole3DUsesSemantics
                        ? std::max(sampleChan[0], sampleChan[2])
                        : sampleChan[3];
                    if (presence != 0)
                        return true;
                }
            }

            return false;
        };

        auto nativeRoleTexturePixelAt = [&](const std::vector<u32>& texRGBA,
                                            int texWidth,
                                            int texHeight,
                                            int x,
                                            int y,
                                            u32& sample) -> bool
        {
            if (texRGBA.empty() || texWidth <= 0 || texHeight <= 0)
                return false;

            const int sampleX = std::min<int>(
                texWidth - 1,
                std::max<int>(0, (x * texWidth) / std::max<int>(width, 1)));
            const int sampleY = std::min<int>(
                texHeight - 1,
                std::max<int>(0, (y * texHeight) / std::max<int>(height, 1)));
            sample = texRGBA[sampleY * texWidth + sampleX];
            return true;
        };

        auto stackRoleMetaAt = [&](int x, int y) -> u32
        {
            if (!stackRoleUpscaled || stackRoleMetaRGBA.empty() || stackRoleMetaWidth <= 0 || stackRoleMetaHeight <= 0)
                return rgba[y * width + x];

            const int metaX = std::min<int>(
                stackRoleMetaWidth - 1,
                std::max<int>(0, (x * stackRoleMetaWidth) / std::max<int>(width, 1)));
            const int metaY = std::min<int>(
                stackRoleMetaHeight - 1,
                std::max<int>(0, (y * stackRoleMetaHeight) / std::max<int>(height, 1)));
            return stackRoleMetaRGBA[metaY * stackRoleMetaWidth + metaX];
        };

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                u32& pixel = rgba[y * width + x];
                const u32 metaPixel = stackRoleMetaAt(x, y);
                const u8* metaChan = reinterpret_cast<const u8*>(&metaPixel);
                const int sourceMask1 = metaChan[0];
                const int sourceMask2 = metaChan[1];
                const int packedInfo = metaChan[2];
                const int packedFlags = metaChan[3];
                const int sourcePrio1 = (packedInfo >> 2) & 0x7;
                const int sourcePrio2 = (packedInfo >> 5) & 0x7;
                const int nativeEffect = (packedFlags >> 2) & 0x7;
                const bool bg0Allowed = (packedFlags & 0x1) != 0;
                const bool blendAllowed = (packedFlags & 0x2) != 0;
                const bool direct3DEnabled = direct3DDisplayEnabled && bg0Allowed;
                const bool direct3DPresent = direct3DEnabled && direct3DPresentAt(x, y);
                const bool direct3DTop = direct3DPresent &&
                    IsInFrontCPU(direct3DPrio, direct3DMask, sourcePrio1, sourceMask1);
                const bool direct3DSecond = direct3DPresent && !direct3DTop &&
                    IsInFrontCPU(direct3DPrio, direct3DMask, sourcePrio2, sourceMask2);

                if (!direct3DEnabled)
                    countNoDirect3D++;
                else if (direct3DTop)
                    countDirect3DFront++;
                else if (direct3DSecond)
                    count2DAbove3D++;
                else
                    countDirect3DHidden++;

                int stackMask1 = sourceMask1;
                int stackMask2 = sourceMask2;
                int stackSpecialType = packedInfo & 0x3;
                if (direct3DTop)
                {
                    stackMask2 = stackMask1;
                    stackMask1 = direct3DMask;
                    stackSpecialType = 1;
                }
                else if (direct3DSecond)
                {
                    stackMask2 = direct3DMask;
                }

                const DebugStackEffectClass stackEffect = ClassifyStackEffectCPU(
                    BlendCnt,
                    blendEffect,
                    blendAllowed,
                    stackSpecialType,
                    stackMask1,
                    stackMask2,
                    EVY);
                const bool native2DEffect = nativeEffect != 0 || stackEffect == DebugStackEffectClass::Native2D;
                const bool direct3DEffect = stackEffect == DebugStackEffectClass::Direct3D;
                bool direct3DEffectKnown = false;
                bool direct3DEffectNeutral = false;
                if (direct3DEffect)
                {
                    u32 topPixel = 0;
                    u32 secondPixel = 0;
                    u32 direct3DPixel = 0;
                    const bool hasDirect3DColor = direct3DTexturePixelAt(
                        stackRole3DColorRGBA,
                        stackRole3DColorWidth,
                        stackRole3DColorHeight,
                        x,
                        y,
                        direct3DPixel);

                    if (direct3DTop && hasDirect3DColor)
                    {
                        topPixel = direct3DPixel;
                        direct3DEffectKnown = nativeRoleTexturePixelAt(
                            stackRoleTopColorRGBA,
                            stackRoleTopColorWidth,
                            stackRoleTopColorHeight,
                            x,
                            y,
                            secondPixel);
                    }
                    else if (direct3DSecond && hasDirect3DColor)
                    {
                        secondPixel = direct3DPixel;
                        direct3DEffectKnown = nativeRoleTexturePixelAt(
                            stackRoleTopColorRGBA,
                            stackRoleTopColorWidth,
                            stackRoleTopColorHeight,
                            x,
                            y,
                            topPixel);
                    }

                    if (direct3DEffectKnown)
                    {
                        const DebugQuantizedColor topColor = QuantizeDebugColorCPU(topPixel);
                        const DebugQuantizedColor secondColor = QuantizeDebugColorCPU(secondPixel);
                        const DebugStackEffectInfo effectInfo = ResolveStackEffectInfoCPU(
                            BlendCnt,
                            blendEffect,
                            blendAllowed,
                            stackSpecialType,
                            stackMask1,
                            stackMask2,
                            topColor,
                            EVA,
                            EVB,
                            EVY);
                        if (effectInfo.effect != 0)
                        {
                            const DebugQuantizedColor effectColor = ApplyStackEffectCPU(topColor, secondColor, effectInfo);
                            direct3DEffectNeutral = MaxDebugRGBDeltaCPU(effectColor, topColor) <= 1;
                        }
                        else
                        {
                            direct3DEffectKnown = false;
                        }
                    }
                }

                DebugRGB color = {48, 48, 48};
                if (!direct3DEnabled)
                {
                    color = {48, 48, 48};
                }
                else if (direct3DEffect)
                {
                    color = direct3DEffectNeutral ? DebugRGB{160, 80, 0} : DebugRGB{255, 128, 0};
                    countDirect3DEffect++;
                    if (!direct3DEffectKnown)
                        countDirect3DEffectUnknown++;
                    else if (direct3DEffectNeutral)
                        countDirect3DEffectNeutral++;
                    else
                        countDirect3DEffectActive++;
                }
                else if (native2DEffect)
                {
                    color = {255, 220, 0};
                    countNativeEffect++;
                }
                else if (direct3DTop)
                {
                    color = {255, 0, 255};
                }
                else if (direct3DSecond)
                {
                    color = {0, 220, 255};
                }
                else
                {
                    color = {48, 96, 255};
                }

                u8* outChan = reinterpret_cast<u8*>(&pixel);
                outChan[0] = color.r;
                outChan[1] = color.g;
                outChan[2] = color.b;
                outChan[3] = 255;
            }
        }

        if (status)
        {
            *status = stackRoleUpscaled
                ? "Upscaled 3D stack role classification.\n"
                : "Native 3D stack role classification.\n";
            *status += "  magenta: Direct3D foreground\n";
            *status += "  cyan: non-3D 2D above Direct3D\n";
            *status += stackRoleUpscaled
                ? "  blue: Direct3D enabled but absent from this upscaled pixel, hidden below the top two layers, or unknown\n"
                : "  blue: Direct3D enabled but absent from this native pixel, hidden below the top two layers, or unknown\n";
            *status += "  yellow: native 2D special blend/effect region\n";
            *status += "  orange: Direct3D is targeted by a native blend/brightness/special effect and appears active or unknown\n";
            *status += "  dark orange: Direct3D is targeted, but native blend math appears visually neutral\n";
            *status += "  gray: BG0/Direct3D disabled or window-excluded for that native pixel\n";
            if (stackRoleUpscaled)
                *status += "Upscaled view uses native stack metadata per native cell plus high-resolution Direct3D coverage per upscaled pixel.\n";
            *status += "BLDCNT ownership: effect ";
            *status += BlendEffectName(blendEffect);
            *status += "; target1 ";
            *status += FormatHex(blendTarget1, 2);
            *status += "; target2 ";
            *status += FormatHex(blendTarget2, 2);
            *status += "; BG0 target1 ";
            *status += (blendTarget1 & direct3DMask) ? "yes" : "no";
            *status += "; BG0 target2 ";
            *status += (blendTarget2 & direct3DMask) ? "yes" : "no";
            *status += ".\n";
            if (!direct3DDisplayEnabled && nativeEffectStateActive)
            {
                *status += "Pure/non-3D native effect state is active; Direct3D is disabled for this role view, so pixels remain gray.\n";
            }
            *status += "Base role counts before effect coloring: foreground ";
            *status += std::to_string(countDirect3DFront);
            *status += ", 2D-over-3D ";
            *status += std::to_string(count2DAbove3D);
            *status += ", hidden/unknown ";
            *status += std::to_string(countDirect3DHidden);
            *status += ", no Direct3D/window-excluded ";
            *status += std::to_string(countNoDirect3D);
            *status += ".\n";
            *status += "Effect overlay counts: native-effect ";
            *status += std::to_string(countNativeEffect);
            *status += ", Direct3D-effect ";
            *status += std::to_string(countDirect3DEffect);
            *status += " (active/non-neutral ";
            *status += std::to_string(countDirect3DEffectActive);
            *status += ", neutral/no-op ";
            *status += std::to_string(countDirect3DEffectNeutral);
            *status += ", unknown ";
            *status += std::to_string(countDirect3DEffectUnknown);
            *status += ")";
            *status += ".\n";
        }
    }
    else if (decodeMeta)
    {
        for (u32& pixel : rgba)
        {
            u8* chan = reinterpret_cast<u8*>(&pixel);
            const int sourceMask1 = chan[0];
            const int sourceMask2 = chan[1];
            const int packedInfo = chan[2];
            const int specialType = packedInfo & 0x3;
            const DebugRGB topTint = DebugTintColorCPU(sourceMask1, specialType);
            const DebugRGB secondTint = DebugTintColorCPU(sourceMask2, 0);

            chan[0] = static_cast<u8>((topTint.r * 3 + secondTint.r) / 4);
            chan[1] = static_cast<u8>((topTint.g * 3 + secondTint.g) / 4);
            chan[2] = static_cast<u8>((topTint.b * 3 + secondTint.b) / 4);
            chan[3] = 255;
        }
    }

    if (status)
    {
        if (view == WholeScene2DDebugView::Direct3D)
            *status = "Direct 3D texture.";
        else if (view == WholeScene2DDebugView::NativeExactFinal)
        {
            *status = (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::OverlayOperatorUpscale)
                ? "Native exact final texture. In presentation overlay upscale mode this is the black-underlay endpoint used as overlay contribution."
                : "Native exact final texture used as the source for postprocessing upscale.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::Native3DResolve)
        {
            *status = "Native 3D visual resolve texture generated from high-resolution Direct3D before native final compositing. Alpha carries visual coverage.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::Native3DSemantics)
        {
            *status = "Native 3D semantics texture. Red is visual coverage, green is native material alpha, and blue is native presence.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::FinalNative3DInput)
        {
            *status = "3D texture used by the native-stage final-native/overlay compositor. In high-res resolved mode, RGB comes from the visual resolve and alpha comes from native 3D semantics.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::Native3DStackRole ||
                 view == WholeScene2DDebugView::Upscaled3DStackRole)
        {
            if (!status->empty())
                *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::OverlayTrueNativeFinal)
        {
            *status = "Overlay true native final texture: the native final result with the selected native-stage 3D underlay.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::HybridSelector)
        {
            *status = "Hybrid selector debug. Magenta = high-resolution foreground compositor, cyan = overlay assist. Fallback pixels are bright when the overlay operator is visible and dim otherwise: gray = no Direct3D, blue = Direct3D absent from the native cell, yellow = native 2D effect, purple = Direct3D hidden below the top two layers, orange = Direct3D effect-blocked, teal = safe window-excluded 3D fallback, red = unknown fallback.";
            *status += "\n";
            *status += WholeSceneScaleHybridWindowEdgeAssist
                ? "Hybrid window-edge assist is enabled, so teal fallback can become cyan overlay assist when selected by the shader.\n"
                : "Hybrid window-edge assist is disabled, so teal fallback remains true native presentation.\n";
            *status += WholeSceneScaleHybridTarget2AlphaBlendAssist
                ? "Hybrid target2 alpha-blend assist is enabled, so modeled Direct3D-under-2D alpha blends can become cyan overlay assist.\n"
                : "Hybrid target2 alpha-blend assist is disabled, so Direct3D-targeted alpha blends remain native fallback.\n";
            *status += WholeSceneScaleHybridNativeEffectGuard
                ? "Hybrid native-effect guard is enabled, so foreground Direct3D replacement falls back near native 2D special-effect cells.\n"
                : "Hybrid native-effect guard is disabled, so native 2D special-effect neighbors do not block foreground replacement.\n";
            *status += WholeSceneScaleHybridForeground2DBase
                ? "Hybrid foreground boundary 2D base is enabled, so narrow Direct3D-absent fallback halos beside safe foreground Direct3D can use a BG0/Direct3D-excluded high-resolution 2D base.\n"
                : "Hybrid foreground boundary 2D base is disabled, so Direct3D-absent fallback pixels use true native presentation.\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::HybridCoverageMiss)
        {
            *status = "Hybrid coverage miss debug. Red = high-resolution Direct3D coverage exists but the hybrid selector falls back to native presentation. Magenta = selected high-resolution foreground, cyan = overlay assist, and dim fallback colors identify fallback reasons with no high-resolution coverage at that pixel.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::HybridForegroundAlpha)
        {
            *status = "Hybrid foreground alpha debug. Magenta = selected high-resolution foreground with full Direct3D alpha, yellow/orange = selected foreground with fractional Direct3D alpha, red = selected foreground with zero Direct3D alpha, cyan = overlay assist, and dim blue = non-selected fallback.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::HybridFinalSource)
        {
            *status = "Hybrid final source debug. Cyan = overlay operator result, pink = hybrid foreground compositor texture, blue = high-resolution foreground-boundary 2D base, gray = native fallback.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (view == WholeScene2DDebugView::SandwichLower2D ||
                 view == WholeScene2DDebugView::SandwichUpper2D ||
                 view == WholeScene2DDebugView::SandwichEligibility)
        {
            if (!status->empty())
                *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (nativeLayerDebugView)
        {
            *status = "Native screen-space layer debug view after scroll/affine, mosaic, window selection, and layer enable checks.";
            *status += "\n";
            *status += DescribeWholeSceneScaleState();
        }
        else if (highResDebugView)
            *status = "High-resolution compositor debug texture.";
        else
            *status = DescribeWholeSceneScaleState();
    }

    return true;
}


bool GLRenderer2D::IsScreenOn()
{
    if (!GPU.ScreensEnabled) return false;
    if (!GPU2D.Enabled) return false;
    if (GPU2D.ForcedBlank) return false;

    u16 masterbright = GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    u16 brightmode = masterbright >> 14;
    u16 brightness = masterbright & 0x1F;
    if ((brightmode == 1 || brightmode == 2) && brightness >= 16)
        return false;

    u16 layers = GPU2D.LayerEnable | 0x20;
    u16 bldeffect = (GPU2D.BlendCnt >> 6) & 0x3;
    u16 bldlayers = GPU2D.BlendCnt & layers & 0x3F;
    if ((bldeffect == 2 || bldeffect == 3) && bldlayers == layers && GPU2D.EVY >= 16 &&
        !(GPU2D.DispCnt & 0xE000))
        return false;

    u32 dispmode = (GPU2D.DispCnt >> 16) & 0x3;
    if (dispmode != 1)
    {
        if (GPU2D.Num) return false;
        if (!GPU.CaptureEnable) return false;
    }

    return true;
}

GLRenderer2D::WholeSceneScaleEligibility GLRenderer2D::ClassifyWholeSceneScalePath() const
{
    if (!GPU.ScreensEnabled || !UnitEnabled || ForcedBlank)
        return WholeSceneScaleEligibility::ScreenUnavailable;

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (dispmode == 0)
        return WholeSceneScaleEligibility::ScreenUnavailable;

    const bool finalImageUpscale =
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale;
    const bool scaleMainVRAMCaptureSourceA = CanScaleMainEngineVRAMDisplayCaptureSourceA(dispmode);
    const bool finalUpscaleMainVRAMDisplay =
        !GPU2D.Num &&
        dispmode == 2 &&
        finalImageUpscale;

    if (!GPU2D.Num)
    {
        if (dispmode == 2 && !scaleMainVRAMCaptureSourceA && !finalUpscaleMainVRAMDisplay)
            return WholeSceneScaleEligibility::MainEngineVRAMDisplay;
        if (dispmode == 3)
            return WholeSceneScaleEligibility::MainEngineDisplayFIFO;

        // Capture-backed scenes can alternate between multiple internal
        // representations. The opt-in path is intended for presentation UI
        // overlays, not for active display captures or full-screen feedback
        // copies of another engine's final output.
        if (GPU.CaptureEnable && !WholeSceneScaleCaptureBacked)
            return WholeSceneScaleEligibility::CaptureActive;
    }

    if (dispmode != 1 && !scaleMainVRAMCaptureSourceA && !finalUpscaleMainVRAMDisplay)
        return WholeSceneScaleEligibility::UnsupportedDisplayMode;

    if (!GPU2D.Num &&
        WholeSceneScaleCaptureBacked &&
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale)
    {
        auto hasVisibleFullScreenBitmapBG = [](u32 dispCnt, const u16* bgCnt, u32 layerEnable)
        {
            int layerType[4] = {1, 1, 0, 0};
            switch (dispCnt & 0x7)
            {
            case 0: layerType[2] = 1; layerType[3] = 1; break;
            case 1: layerType[2] = 1; layerType[3] = 2; break;
            case 2: layerType[2] = 2; layerType[3] = 2; break;
            case 3: layerType[2] = 1; layerType[3] = 3; break;
            case 4: layerType[2] = 2; layerType[3] = 3; break;
            case 5: layerType[2] = 3; layerType[3] = 3; break;
            case 6: layerType[0] = 0; layerType[1] = 0; layerType[2] = 4; layerType[3] = 0; break;
            case 7: layerType[2] = 0; layerType[3] = 0; break;
            }

            for (int layer = 0; layer < 4; layer++)
            {
                if ((layerEnable & (1 << layer)) == 0)
                    continue;

                int width = 0;
                int height = 0;
                if (layerType[layer] == 3)
                {
                    if ((bgCnt[layer] & (1 << 7)) == 0)
                        continue;

                    switch (bgCnt[layer] >> 14)
                    {
                    case 0: width = 128; height = 128; break;
                    case 1: width = 256; height = 256; break;
                    case 2: width = 512; height = 256; break;
                    case 3: width = 512; height = 512; break;
                    }
                }
                else if (layerType[layer] == 4)
                {
                    width = 256;
                    height = 192;
                }
                else
                    continue;

                if (width >= 256 && height >= 192)
                    return true;
            }

            return false;
        };

        const u32 cachedEnabledLayers = (DispCnt >> 8) & 0x1Fu;
        const u32 liveEnabledLayers = (GPU2D.DispCnt >> 8) & 0x1Fu;
        const bool cachedDirect3DVisible = (DispCnt & (1 << 3)) && (cachedEnabledLayers & (1 << 0));
        const bool liveDirect3DVisible = (GPU2D.DispCnt & (1 << 3)) && (liveEnabledLayers & (1 << 0));
        const bool visibleLayerHandoff = (cachedEnabledLayers ^ liveEnabledLayers) & 0xFu;
        if (visibleLayerHandoff && (cachedDirect3DVisible || liveDirect3DVisible))
        {
            const bool cachedFullScreenBitmapVisible =
                hasVisibleFullScreenBitmapBG(DispCnt, BGCnt, cachedEnabledLayers);
            const bool liveFullScreenBitmapVisible =
                hasVisibleFullScreenBitmapBG(GPU2D.DispCnt, GPU2D.BGCnt, liveEnabledLayers);
            if (cachedFullScreenBitmapVisible || liveFullScreenBitmapVisible)
                return WholeSceneScaleEligibility::CaptureBackedBG;
        }
    }

    for (int layer = 0; layer < 4; layer++)
    {
        const auto& cfg = LayerConfig.uBGConfig[layer];
        if (cfg.Type >= 7)
        {
            const bool cachedLayerVisible = (LayerEnable & (1 << layer)) != 0;
            const bool liveLayerVisible = (GPU2D.LayerEnable & (1 << layer)) != 0;
            if (!WholeSceneScaleCaptureBacked ||
                (cachedLayerVisible && !liveLayerVisible) ||
                (Parent.IsCurrentSourceAOnlyFullDisplayCaptureBG(cfg.Type, cfg.TileOffset) &&
                 !CanUseWholeSceneMixedSourceACaptureBGPath()))
                return WholeSceneScaleEligibility::CaptureBackedBG;
        }
    }

    const bool mixedCaptureBackedOBJOverlayPath =
        CanUseWholeSceneMixedCaptureBackedOBJOverlayPath();
    for (int i = 0; i < NumSprites; i++)
    {
        if ((SpriteConfig.uOAM[i].Type >= 3) &&
            !mixedCaptureBackedOBJOverlayPath &&
            (!WholeSceneScaleCaptureBacked ||
             !Parent.IsEngineRoutedToFinalBottom(GPU2D.Num) ||
             HasOnlyCaptureBackedOBJPresentation() ||
             HasFullScreenSourceACaptureBackedOBJ()))
            return WholeSceneScaleEligibility::CaptureBackedOBJ;
    }

    return WholeSceneScaleEligibility::Eligible;
}

bool GLRenderer2D::HasOnlyCaptureBackedOBJPresentation() const
{
    bool foundCaptureBackedOBJ = false;
    for (int i = 0; i < NumSprites; i++)
    {
        if (SpriteConfig.uOAM[i].Type >= 3)
        {
            foundCaptureBackedOBJ = true;
            break;
        }
    }

    if (!foundCaptureBackedOBJ)
        return false;

    for (int layer = 0; layer < 4; layer++)
    {
        if (!(LayerEnable & (1 << layer)))
            continue;

        if (LayerConfig.uBGConfig[layer].Type < 7)
            return false;
    }

    return true;
}

bool GLRenderer2D::HasFullScreenSourceACaptureBackedOBJ() const
{
    int minX = 256;
    int minY = 192;
    int maxX = 0;
    int maxY = 0;
    int area = 0;
    bool found = false;

    for (int i = 0; i < NumSprites; i++)
    {
        const auto& sprite = SpriteConfig.uOAM[i];
        if (sprite.Type < 3 ||
            !Parent.IsCurrentSourceAOnlyFullDisplayCaptureOBJ(sprite.Type, sprite.TileStride))
            continue;

        const int x0 = std::max(0, sprite.Position[0]);
        const int y0 = std::max(0, sprite.Position[1]);
        const int x1 = std::min(256, sprite.Position[0] + sprite.BoundSize[0]);
        const int y1 = std::min(192, sprite.Position[1] + sprite.BoundSize[1]);
        if (x1 <= x0 || y1 <= y0)
            continue;

        found = true;
        minX = std::min(minX, x0);
        minY = std::min(minY, y0);
        maxX = std::max(maxX, x1);
        maxY = std::max(maxY, y1);
        area += (x1 - x0) * (y1 - y0);
    }

    if (!found)
        return false;

    const int boundsWidth = maxX - minX;
    const int boundsHeight = maxY - minY;
    constexpr int screenArea = 256 * 192;

    return (boundsWidth >= 240 && boundsHeight >= 176) ||
           (area >= (screenArea * 3) / 4);
}

bool GLRenderer2D::CanScaleMainEngineVRAMDisplayCaptureSourceA(u32 dispmode) const
{
    MainVRAMDisplayCaptureScaleInputs inputs = {};
    inputs.MainEngine = !GPU2D.Num;
    inputs.DisplayMode = dispmode;
    inputs.ConservativeHybridMode =
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
    inputs.CaptureBackedScalingEnabled = WholeSceneScaleCaptureBacked;
    inputs.CaptureEnabled = GPU.CaptureEnable;
    inputs.CaptureCnt = GPU.CaptureCnt;

    if (inputs.MainEngine &&
        inputs.DisplayMode == 2 &&
        inputs.ConservativeHybridMode &&
        inputs.CaptureBackedScalingEnabled)
    {
        inputs.DisplayBank = (DispCnt >> 18) & 0x3;
        inputs.HasAcceptedDisplayReplacement =
            Parent.CanUseMainVRAMDisplayHighResCaptureReplacement(inputs.DisplayBank);
    }

    return ::melonDS::CanScaleMainVRAMDisplayCaptureSourceA(inputs);
}

bool GLRenderer2D::CanUseWholeSceneMixedSourceACaptureBGPath() const
{
    if (!WholeSceneScaleCaptureBacked || GPU.CaptureEnable)
        return false;

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (dispmode != 1)
        return false;

    if (!GPU2D.Num && (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0)))
        return false;

    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    const u32 captureBGMask = VisibleSourceAOnlyFullDisplayCaptureBGLayerMask();
    if (captureBGMask == 0 || (visibleBGMask & ~captureBGMask) == 0)
        return false;

    for (int layer = 0; layer < 4; layer++)
    {
        if ((visibleBGMask & (1u << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        if (cfg.Type < 7)
            continue;

        if ((captureBGMask & (1u << layer)) == 0)
            return false;

        u64 serial = 0;
        u32 sourceKind = 0;
        u32 productMask = 0;
        u32 rejectReason = 0;
        const GLuint fullProductTex =
            Parent.GetHighResDisplayCaptureFullTexForBG(cfg.Type,
                                                         cfg.TileOffset,
                                                         serial,
                                                         sourceKind,
                                                         productMask,
                                                         rejectReason);
        if (!fullProductTex ||
            rejectReason != static_cast<u32>(GLRenderer::HighResCaptureRejectReason::None) ||
            sourceKind != static_cast<u32>(GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput) ||
            (productMask & GLRenderer::HighResCaptureProductFullEquivalent) == 0)
        {
            return false;
        }
    }

    return true;
}

bool GLRenderer2D::CanUseWholeSceneMixedCaptureBackedOBJOverlayPath() const
{
    if (!WholeSceneScaleRequested ||
        WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleCaptureBacked ||
        GPU.CaptureEnable)
    {
        return false;
    }

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (dispmode != 1)
        return false;

    if (!GPU2D.Num && (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0)))
        return false;

    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    if (visibleBGMask == 0 || VisibleBitmapBGLayerMask() != 0)
        return false;

    for (int layer = 0; layer < 4; layer++)
    {
        if ((visibleBGMask & (1u << layer)) == 0)
            continue;

        if (LayerConfig.uBGConfig[layer].Type >= 7)
            return false;
    }

    const VisibleOBJCaptureDebug debug = BuildVisibleOBJCaptureDebug();
    if (!debug.Found ||
        debug.RejectReason != 2 ||
        debug.Bank < 0 ||
        debug.Bank >= 4 ||
        debug.MixedBank ||
        debug.NonCaptureSpriteCount != 0 ||
        debug.CaptureSpriteCount == 0 ||
        (!debug.FullScreen && !debug.FullWidthTopStrip) ||
        !debug.CurrentFullSourceA ||
        !debug.EventValid ||
        debug.EventSourceOBJ ||
        debug.EventRejectReason != static_cast<u32>(GLRenderer::HighResCaptureRejectReason::None) ||
        (debug.EventProductMask & GLRenderer::HighResCaptureProductFullEquivalent) == 0 ||
        !debug.ProductAvailable)
    {
        return false;
    }

    const auto& event = Parent.HighResDisplayCapture256Event[debug.Bank];
    return event.SourceKind == GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput &&
           Parent.HighResDisplayCaptureFullTex[debug.Bank] != 0;
}

bool GLRenderer2D::CanUseSourceABackgroundCurrentOverlayPath() const
{
    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleCaptureBacked ||
        GPU.CaptureEnable)
    {
        return false;
    }

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (dispmode != 1)
        return false;

    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    const u32 captureBGMask = VisibleFullDisplayCaptureFromSourceABGLayerMask();
    if (visibleBGMask == 0 || (visibleBGMask & ~captureBGMask) != 0)
        return false;

    const int captureBank = VisibleSingleHighResCaptureBank();
    if (captureBank < 0 || captureBank >= 4)
        return false;

    const auto& event = Parent.HighResDisplayCapture256Event[captureBank];
    if (event.RejectReason != GLRenderer::HighResCaptureRejectReason::None ||
        event.SourceKind != GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput ||
        (event.ProductMask & GLRenderer::HighResCaptureProductBackground3DUnderlay) == 0 ||
        !VisibleHighResCaptureBackgroundTex())
    {
        return false;
    }

    const GLRenderer2D* mainRenderer = dynamic_cast<GLRenderer2D*>(Parent.Rend2D_A.get());
    return mainRenderer &&
           mainRenderer->CanRenderCurrentOverlayForCaptureSource(event.SourceLayerEnable,
                                                                 event.SourceBGMode,
                                                                 event.SourceVisibleBitmapMask,
                                                                 event.SourceDirect3DVisible);
}

bool GLRenderer2D::CanUseSourceAExactFullProductBridgePath(int ystart, int yend) const
{
    if (WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleCaptureBacked ||
        GPU.CaptureEnable)
    {
        return false;
    }

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    if (dispmode != 1 ||
        !Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, ystart, yend))
    {
        return false;
    }

    const bool objVisible = (LayerEnable & (1 << 4)) && OBJEnable && NumSprites > 0;
    if (objVisible)
        return false;

    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    const u32 captureBGMask = VisibleFullDisplayCaptureFromSourceABGLayerMask();
    if (visibleBGMask == 0 || (visibleBGMask & ~captureBGMask) != 0)
        return false;

    const int captureBank = VisibleSingleHighResCaptureBank();
    if (captureBank < 0 || captureBank >= 4 || !VisibleHighResCaptureFullTex())
        return false;

    const auto& event = Parent.HighResDisplayCapture256Event[captureBank];
    if (event.RejectReason != GLRenderer::HighResCaptureRejectReason::None ||
        event.SourceKind != GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput ||
        (event.ProductMask & GLRenderer::HighResCaptureProductFullEquivalent) == 0)
    {
        return false;
    }

    return true;
}

bool GLRenderer2D::CanUseWholeSceneScalePath() const
{
    return WholeSceneScaleRequested &&
           WholeSceneScaleState == WholeSceneScaleEligibility::Eligible;
}

bool GLRenderer2D::CanUseWholeSceneLegacyPath() const
{
    return CanUseWholeSceneScalePath() &&
           WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::LegacyNativeUpscale;
}

bool GLRenderer2D::CanUseWholeSceneHighResPath() const
{
    return CanUseWholeSceneScalePath() &&
           WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::HighResCompositor;
}

bool GLRenderer2D::CanUseWholeSceneOverlayOperatorPath() const
{
    return CanUseWholeSceneScalePath() &&
           (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::OverlayOperatorUpscale ||
            WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale);
}

bool GLRenderer2D::CanUseWholeSceneFinalUpscalePath() const
{
    return CanUseWholeSceneScalePath() &&
           WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale;
}

bool GLRenderer2D::CanUsePhysicalFinalPostprocessNativeInputPath() const
{
    if (!Parent.PhysicalFinalUpscale ||
        !WholeSceneScaleRequested ||
        WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale)
        return false;

    if (!GPU.ScreensEnabled || !UnitEnabled || ForcedBlank)
        return false;

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    return dispmode == 1;
}

bool GLRenderer2D::CanUseWholeSceneHybridCleanLegacyCandidatePath() const
{
    if (!CanUseWholeSceneScalePath() ||
        WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !WholeSceneScaleHybridCleanLegacyCandidate)
        return false;

    if (GPU2D.Num != 0)
        return false;

    const u32 dispmode = (DispCnt >> 16) & 0x3u;
    if (dispmode != 1)
        return false;

    const bool visibleDirect3DLayer = (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0));
    if (!visibleDirect3DLayer || GPU.GPU3D.RenderNumPolygons == 0)
        return false;

    if (WholeSceneScaleFinalUpscaleRender3DNative)
        return false;

    // Normal WIN0/WIN1 state should not disqualify the whole frame: the hybrid
    // selector already has per-cell window metadata and can avoid candidate use
    // in excluded regions. OBJ window is less predictable, so keep blocking it.
    if (GPU.CaptureEnable || (DispCnt & (1 << 15)))
        return false;

    // Brightness effects are allowed because the native-stack path already handles
    // them as a native compositor transform before resolving high-res 3D.
    // Simple BG0/Direct3D-as-target1 alpha blends are also allowed here: the
    // native-stack candidate has already resolved the native blend coherently, which
    // avoids high-resolution target1 reconstruction artifacts.
    const u32 blendEffect = (BlendCnt >> 6) & 0x3u;
    if (blendEffect == 1)
    {
        const u32 direct3DMask = 1u << 0;
        const u32 twoDLayerMask = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
        const u32 backdropMask = 1u << 5;
        const u32 target1AllowedMask = direct3DMask | twoDLayerMask;
        const u32 target2AllowedMask = direct3DMask | twoDLayerMask | backdropMask;
        const u32 blendTarget1 = BlendCnt & 0x3Fu;
        const u32 blendTarget2 = (BlendCnt >> 8) & 0x3Fu;
        const bool alphaBlendHasNoTarget1 = blendTarget1 == 0;
        const bool direct3DTarget1With2DTarget2 =
            (blendTarget1 & direct3DMask) != 0 &&
            (blendTarget1 & ~target1AllowedMask) == 0 &&
            (blendTarget2 & target2AllowedMask) != 0 &&
            (blendTarget2 & ~target2AllowedMask) == 0;
        const bool direct3DTarget1Zeroed =
            direct3DTarget1With2DTarget2 &&
            EVA == 0 &&
            EVB == 16;
        const bool direct3DTarget1Contributing =
            direct3DTarget1With2DTarget2 &&
            EVA > 0 &&
            EVB > 0;
        const bool direct3DTarget1ZeroCoefficient =
            direct3DTarget1With2DTarget2 &&
            EVA == 0 &&
            EVB == 0;
        const bool direct3DTarget2Contributing =
            (blendTarget1 & direct3DMask) == 0 &&
            (blendTarget1 & twoDLayerMask) != 0 &&
            (blendTarget2 & direct3DMask) != 0 &&
            EVA > 0 &&
            EVB > 0;

        if (!alphaBlendHasNoTarget1 &&
            !direct3DTarget1Zeroed &&
            !direct3DTarget1Contributing &&
            !direct3DTarget1ZeroCoefficient &&
            !direct3DTarget2Contributing)
            return false;
    }

    for (int layer = 0; layer < 4; layer++)
    {
        if ((LayerEnable & (1 << layer)) && LayerConfig.uBGConfig[layer].Type >= 7)
            return false;
    }

    for (int i = 0; i < NumSprites; i++)
    {
        if (SpriteConfig.uOAM[i].Type >= 3)
            return false;
    }

    return true;
}

bool GLRenderer2D::CanUseWholeSceneFullFrameFinalizerPath() const
{
    if (CanUseWholeSceneFinalUpscalePath())
        return true;

    if (!CanUseWholeSceneOverlayOperatorPath())
        return false;

    if (WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale &&
        IsWholeSceneHybridFragmentationGuardActive())
        return true;

    return true;
}

bool GLRenderer2D::CanUseWholeSceneSplitLegacyFallbackPath() const
{
    return CanUseWholeSceneScalePath() &&
           WholeSceneFullFrameFinalizerUnsafeFrame &&
           !IsWholeSceneCurrentFragmentationGuardActive() &&
           WholeSceneCurrentFramePartialComposites < kWholeSceneHybridFragmentationGuardThreshold;
}

bool GLRenderer2D::CanUseWholeSceneForegroundOverlayPath() const
{
    if (!WholeSceneScaleForegroundOverlay || !CanUseWholeSceneLegacyPath())
        return false;
    if (GPU2D.Num != 0)
        return false;

    const u32 dispmode = (DispCnt >> 16) & 0x3u;
    if (dispmode != 1)
        return false;
    if (!(DispCnt & (1 << 3)))
        return false;
    if (GPU.GPU3D.RenderNumPolygons == 0)
        return false;
    if (GPU.CaptureEnable)
        return false;

    const u32 direct3DMask = 1u << 0;
    const u32 twoDLayerMask = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    const u32 blendEffect = (BlendCnt >> 6) & 0x3u;
    const u32 blendTarget1 = BlendCnt & 0x3Fu;
    const u32 blendTarget2 = (BlendCnt >> 8) & 0x3Fu;

    // The foreground overlay carries exact 2D color plus binary coverage. It
    // cannot preserve DS alpha blending between BG0/direct-3D and 2D layers.
    if (blendEffect == 1 &&
        (((blendTarget1 & direct3DMask) && (blendTarget2 & twoDLayerMask)) ||
         ((blendTarget2 & direct3DMask) && (blendTarget1 & twoDLayerMask))))
        return false;
    if ((blendEffect == 2 || blendEffect == 3) && (blendTarget1 & direct3DMask))
        return false;

    for (int layer = 0; layer < 4; layer++)
    {
        if (LayerConfig.uBGConfig[layer].Type >= 7)
            return false;
    }

    for (int i = 0; i < NumSprites; i++)
    {
        if (SpriteConfig.uOAM[i].Type >= 3)
            return false;

        if ((blendTarget2 & direct3DMask) &&
            ((SpriteConfig.uOAM[i].OBJMode == 1) ||
             (SpriteConfig.uOAM[i].OBJMode == 3)))
            return false;
    }

    return true;
}

bool GLRenderer2D::CanUseWholeSceneArtCNNPath() const
{
    return (CanUseWholeSceneLegacyPath() ||
            CanUseWholeSceneHybridCleanLegacyCandidatePath() ||
            CanUseWholeSceneSplitLegacyFallbackPath()) &&
           RendererSettings::IsGLArtCNNAlgorithm(WholeSceneScaleAlgorithm) &&
           (ScaleFactor >= 2);
}

bool GLRenderer2D::CanUseWholeSceneNNEDI3Path() const
{
    return (CanUseWholeSceneLegacyPath() ||
            CanUseWholeSceneHybridCleanLegacyCandidatePath() ||
            CanUseWholeSceneSplitLegacyFallbackPath()) &&
           RendererSettings::IsGLNNEDI3Algorithm(WholeSceneScaleAlgorithm) &&
           (ScaleFactor >= 2);
}

bool GLRenderer2D::CanUseWholeSceneXBRZPath() const
{
    return (CanUseWholeSceneLegacyPath() ||
            CanUseWholeSceneHybridCleanLegacyCandidatePath() ||
            CanUseWholeSceneSplitLegacyFallbackPath()) &&
           WholeSceneScaleAlgorithm == RendererSettings::GLScaleAlgorithm::XBRZ &&
           (ScaleFactor > 1);
}

bool GLRenderer2D::CanUseWholeSceneCuNNyPath() const
{
    return (CanUseWholeSceneLegacyPath() ||
            CanUseWholeSceneHybridCleanLegacyCandidatePath() ||
            CanUseWholeSceneSplitLegacyFallbackPath()) &&
           RendererSettings::IsGLCuNNyAlgorithm(WholeSceneScaleAlgorithm) &&
           (ScaleFactor >= 2);
}

int GLRenderer2D::WholeSceneHighResLayerFilterMode() const
{
    if (!CanUseWholeSceneHighResPath())
        return 0;

    if (WholeSceneScaleAlgorithm == RendererSettings::GLScaleAlgorithm::Spline36)
        return 1;

    return 0;
}

bool GLRenderer2D::WholeSceneHighResLayerFilterNoWrap() const
{
    return WholeSceneHighResLayerFilterMode() != 0 && WholeSceneScaleNoWrapFilterTaps;
}

int GLRenderer2D::WholeSceneHighResSpriteFilterMode() const
{
    return WholeSceneHighResLayerFilterMode();
}

int GLRenderer2D::WholeSceneArtCNNModelIndex() const
{
    return RendererSettings::GetGLArtCNNModelIndex(WholeSceneScaleAlgorithm);
}


void GLRenderer2D::UpdateAndRender(int line)
{
    u32 palmask = 1 << (GPU2D.Num * 2);

    // check if any 'critical' registers were modified
    auto phaseStart = std::chrono::steady_clock::now();

    u32 dispcnt_diff;
    u8 layer_diff;
    u16 bgcnt_diff[4];

    dispcnt_diff = GPU2D.DispCnt ^ DispCnt;
    layer_diff = GPU2D.LayerEnable ^ LayerEnable;
    for (int layer = 0; layer < 4; layer++)
        bgcnt_diff[layer] = GPU2D.BGCnt[layer] ^ BGCnt[layer];

    u8 layer_pre_dirty = 0;
    bool comp_dirty = false;
    u32 state_dirty_reason_mask = 0;
    u32 state_dirty_misc_diff_mask = 0;
    bool screenon = IsScreenOn();

    if (UnitEnabled != GPU2D.Enabled)
    {
        state_dirty_reason_mask |= kWholeSceneDirtyUnitEnable;
        state_dirty_misc_diff_mask |= kWholeSceneMiscUnitEnable;
    }
    if (ForcedBlank != GPU2D.ForcedBlank)
    {
        state_dirty_reason_mask |= kWholeSceneDirtyForcedBlank;
        state_dirty_misc_diff_mask |= kWholeSceneMiscForcedBlank;
    }
    if (layer_diff & 0x1F)
        state_dirty_reason_mask |= kWholeSceneDirtyLayerEnable;

    if (dispcnt_diff & 0x8)
    {
        layer_pre_dirty |= 0x1;
        state_dirty_reason_mask |= kWholeSceneDirtyDispCntLayer;
    }
    if (dispcnt_diff & 0x7)
    {
        layer_pre_dirty |= 0xC;
        state_dirty_reason_mask |= kWholeSceneDirtyDispCntLayer;
    }
    if (dispcnt_diff & 0x7F000000)
    {
        layer_pre_dirty |= 0xF;
        state_dirty_reason_mask |= kWholeSceneDirtyDispCntLayer;
    }

    if (dispcnt_diff & 0x0000E008)
    {
        comp_dirty = true;
        state_dirty_reason_mask |= kWholeSceneDirtyDispCntCompositor;
    }
    else if (layer_diff & 0x1F)
    {
        comp_dirty = true;
        state_dirty_reason_mask |= kWholeSceneDirtyLayerEnable;
    }
    else if (UnitEnabled != GPU2D.Enabled)
    {
        comp_dirty = true;
        state_dirty_reason_mask |= kWholeSceneDirtyUnitEnable;
        state_dirty_misc_diff_mask |= kWholeSceneMiscUnitEnable;
    }
    else if (ForcedBlank != GPU2D.ForcedBlank)
    {
        comp_dirty = true;
        state_dirty_reason_mask |= kWholeSceneDirtyForcedBlank;
        state_dirty_misc_diff_mask |= kWholeSceneMiscForcedBlank;
    }

    for (int layer = 0; layer < 4; layer++)
    {
        u16 mask = 0xDFBC;
        if (layer < 2) mask |= (1 << 13);
        bool bgcntLayerDirty = false;
        if (bgcnt_diff[layer] & mask)
        {
            layer_pre_dirty |= (1 << layer);
            bgcntLayerDirty = true;
        }
        if (bgcnt_diff[layer] & (~mask))
        {
            comp_dirty = true;
            state_dirty_reason_mask |= kWholeSceneDirtyBGCntCompositor;
        }
        if (bgcntLayerDirty)
            state_dirty_reason_mask |= kWholeSceneDirtyBGCntLayer;
    }

    if (GPU2D.BlendCnt != BlendCnt)
        state_dirty_misc_diff_mask |= kWholeSceneMiscBlendCnt;
    if (GPU2D.EVA != EVA)
        state_dirty_misc_diff_mask |= kWholeSceneMiscEVA;
    if (GPU2D.EVB != EVB)
        state_dirty_misc_diff_mask |= kWholeSceneMiscEVB;
    if (GPU2D.EVY != EVY)
        state_dirty_misc_diff_mask |= kWholeSceneMiscEVY;
    if (state_dirty_misc_diff_mask &
        (kWholeSceneMiscBlendCnt | kWholeSceneMiscEVA |
         kWholeSceneMiscEVB | kWholeSceneMiscEVY))
    {
        comp_dirty = true;
        state_dirty_reason_mask |= kWholeSceneDirtyBlend;
    }

    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.StateDiff, ElapsedUS(phaseStart));

    // check if VRAM was modified, and flatten it as needed
    phaseStart = std::chrono::steady_clock::now();

    static_assert(VRAMDirtyGranularity == 512);
    NonStupidBitField<1024> bgDirty;
    NonStupidBitField<64> bgExtPalDirty;
    NonStupidBitField<16> objExtPalDirty;

    if (screenon)
    {
        SyncPendingDisplayCapturesForFlatVRAMBGs();

        if (GPU2D.Num == 0)
        {
            bgDirty = GPU.VRAMDirty_ABG.DeriveState(GPU.VRAMMap_ABG, GPU);
            GPU.MakeVRAMFlat_ABGCoherent(bgDirty);

            bgExtPalDirty = GPU.VRAMDirty_ABGExtPal.DeriveState(GPU.VRAMMap_ABGExtPal, GPU);
            GPU.MakeVRAMFlat_ABGExtPalCoherent(bgExtPalDirty);
            objExtPalDirty = GPU.VRAMDirty_AOBJExtPal.DeriveState(&GPU.VRAMMap_AOBJExtPal, GPU);
            GPU.MakeVRAMFlat_AOBJExtPalCoherent(objExtPalDirty);
        }
        else
        {
            auto _bgDirty = GPU.VRAMDirty_BBG.DeriveState(GPU.VRAMMap_BBG, GPU);
            GPU.MakeVRAMFlat_BBGCoherent(_bgDirty);
            for (int i = 0; i < 1024; i += 256)
                memcpy(&bgDirty.Data[i>>6], _bgDirty.Data, 256>>3);

            bgExtPalDirty = GPU.VRAMDirty_BBGExtPal.DeriveState(GPU.VRAMMap_BBGExtPal, GPU);
            GPU.MakeVRAMFlat_BBGExtPalCoherent(bgExtPalDirty);
            objExtPalDirty = GPU.VRAMDirty_BOBJExtPal.DeriveState(&GPU.VRAMMap_BOBJExtPal, GPU);
            GPU.MakeVRAMFlat_BOBJExtPalCoherent(objExtPalDirty);
        }
    }
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.VRAMFlatten, ElapsedUS(phaseStart));

    // for each layer, check if the VRAM and palettes involved are dirty
    phaseStart = std::chrono::steady_clock::now();

    u8* bgvram = nullptr;
    u32 bgvrammask = 0;
    if (screenon)
        GPU2D.GetBGVRAM(bgvram, bgvrammask);

    const u8 covered_layers = CoveredByOpaqueBitmapBGLayerMask(LastLine, line, bgvram, bgvrammask);
    const u8 register_layer_pre_dirty = layer_pre_dirty;
    const u8 visible_layers = (LayerEnable | GPU2D.LayerEnable) & 0xF;
    const u8 contributing_layers = visible_layers & ~covered_layers;
    u8 deferred_layer_pre_dirty = DeferredLayerPrerenderDirty & contributing_layers;
    u8 covered_deferred_layer_pre_dirty = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        const u8 layer_mask = 1 << layer;
        if ((deferred_layer_pre_dirty & layer_mask) == 0)
            continue;

        const int firstRow = DeferredLayerPrerenderFirstRow[layer];
        const int lastRow = DeferredLayerPrerenderLastRow[layer];
        if (firstRow < 0 || lastRow <= firstRow)
            continue;

        if (!VisibleBitmapDirtyRowsCoveredByOpaqueUpperLayer(layer, firstRow, lastRow, bgvram, bgvrammask))
            continue;

        deferred_layer_pre_dirty &= ~layer_mask;
        covered_deferred_layer_pre_dirty |= layer_mask;
    }
    layer_pre_dirty |= deferred_layer_pre_dirty;
    u8 vram_layer_pre_dirty = 0;
    u8 palette_layer_pre_dirty = 0;

    for (int layer = 0; layer < 4; layer++)
    {
        const u32* rangeinfo = BGVRAMRange[layer];

        // to consider: only check the tileset range that is actually used
        // (would require parsing the tilemap)
        for (int r = 0; r < 4; r+=2)
        {
            if (rangeinfo[r] == 0xFFFFFFFF)
                continue;

            bool dirty = false;
            u32 rstart = (rangeinfo[r] >> 9) & 0x3FF;
            u32 rcount = (rangeinfo[r+1] >> 9);
            if ((rstart + rcount) > 1024)
            {
                dirty = bgDirty.CheckRange(rstart, 1024-rstart) ||
                        bgDirty.CheckRange(0, rcount-(1024-rstart));
            }
            else
                dirty = bgDirty.CheckRange(rstart, rcount);

            if (dirty)
            {
                layer_pre_dirty |= (1 << layer);
                vram_layer_pre_dirty |= (1 << layer);
            }
        }

        auto& cfg = LayerConfig.uBGConfig[layer];
        if ((cfg.Type == 1 || cfg.Type == 3) && (cfg.PalOffset > 0))
        {
            u32 pal = cfg.PalOffset - 1;
            if (bgExtPalDirty.CheckRange(pal, pal + 16))
            {
                layer_pre_dirty |= (1 << layer);
                palette_layer_pre_dirty |= (1 << layer);
            }
        }
        else if (cfg.Type <= 4)
        {
            if (GPU.PaletteDirty & palmask)
            {
                layer_pre_dirty |= (1 << layer);
                palette_layer_pre_dirty |= (1 << layer);
            }
        }
    }

    // Hidden BG layers can be streamed while another layer is displayed.
    // Upload their VRAM now, but defer prerendering until they become visible.
    // Enabled bitmap BGs can use the same path when an opaque higher-priority
    // bitmap BG covers the chunk that would otherwise be composited.
    const u8 inactive_layer_pre_dirty =
        (layer_pre_dirty & ~register_layer_pre_dirty) & ~contributing_layers;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((inactive_layer_pre_dirty & (1 << layer)) == 0)
            continue;
        MarkLayerPrerenderDeferred(layer);
    }
    layer_pre_dirty &= ~inactive_layer_pre_dirty;

    auto& debugTrace = WholeSceneCurrentUpdateDebugTrace;
    const u32 visible_bitmap_mask = VisibleBitmapBGLayerMask();
    debugTrace.RegisterLayerDirtyMask |= register_layer_pre_dirty;
    debugTrace.VRAMLayerDirtyMask |= vram_layer_pre_dirty;
    debugTrace.PaletteLayerDirtyMask |= palette_layer_pre_dirty;
    debugTrace.DeferredLayerDirtyMask |= deferred_layer_pre_dirty;
    debugTrace.InactiveDeferredLayerDirtyMask |= inactive_layer_pre_dirty;
    debugTrace.VisibleBitmapCoveredDirtyDeferredMask |= covered_deferred_layer_pre_dirty;
    debugTrace.CoveredBitmapMask |= covered_layers;
    debugTrace.ContributingLayerMask |= contributing_layers;
    debugTrace.VisibleBitmapDirtyMask |= layer_pre_dirty & contributing_layers & visible_bitmap_mask;

    const u8 visible_bitmap_vram_dirty = vram_layer_pre_dirty & contributing_layers & visible_bitmap_mask;
    const u8 non_vram_layer_pre_dirty =
        register_layer_pre_dirty | palette_layer_pre_dirty | deferred_layer_pre_dirty;
    u8 row_limited_bitmap_prerender = 0;
    int row_limited_bitmap_first[4] = {-1, -1, -1, -1};
    int row_limited_bitmap_last[4] = {-1, -1, -1, -1};
    for (int layer = 0; layer < 4; layer++)
    {
        const u8 layer_mask = 1 << layer;
        if ((visible_bitmap_vram_dirty & layer_mask) == 0)
            continue;

        int firstRow = -1;
        int lastRow = -1;
        if (DirtyBitmapSourceRowsForLayer(layer, bgDirty, firstRow, lastRow))
        {
            RecordWholeSceneVisibleBitmapDirtyRows(layer, line, firstRow, lastRow);

            if ((non_vram_layer_pre_dirty & layer_mask) == 0 &&
                VisibleBitmapDirtyRowsCoveredByOpaqueUpperLayer(layer, firstRow, lastRow, bgvram, bgvrammask))
            {
                layer_pre_dirty &= ~layer_mask;
                MarkLayerPrerenderDeferred(layer, firstRow, lastRow);
                debugTrace.VisibleBitmapCoveredDirtyDeferredMask |= layer_mask;
                continue;
            }

            if ((non_vram_layer_pre_dirty & layer_mask) == 0)
            {
                row_limited_bitmap_prerender |= layer_mask;
                row_limited_bitmap_first[layer] = firstRow;
                row_limited_bitmap_last[layer] = lastRow;
                debugTrace.VisibleBitmapRowLimitedPrerenderMask |= layer_mask;
                debugTrace.VisibleBitmapRowLimitedFirstRow[layer] = firstRow;
                debugTrace.VisibleBitmapRowLimitedLastRow[layer] = lastRow;
            }
        }
    }

    if (layer_pre_dirty)
        comp_dirty = true;

    RecordWholeSceneLayerDirty(line, layer_pre_dirty);

    if (Parent.NeedPartialRender)
    {
        comp_dirty = true;
        state_dirty_reason_mask |= kWholeSceneDirtyParentPartial;
        state_dirty_misc_diff_mask |= kWholeSceneMiscParentPartial;
    }

    if (state_dirty_reason_mask)
    {
        auto& stateTrace = WholeSceneCurrentUpdateDebugTrace;
        stateTrace.StateDirtyEvents++;
        stateTrace.StateDirtyReasonMask |= state_dirty_reason_mask;
        stateTrace.StateDirtyDispCntDiff |= dispcnt_diff;
        stateTrace.StateDirtyLayerEnableDiff |= layer_diff;
        for (int layer = 0; layer < 4; layer++)
            stateTrace.StateDirtyBGCntDiff[layer] |= bgcnt_diff[layer];
        stateTrace.StateDirtyMiscDiffMask |= state_dirty_misc_diff_mask;
    }

    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.LayerDirty, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    WholeSceneScaleState = ClassifyWholeSceneScalePath();
    RecordWholeSceneNativeProductEligibility(WholeSceneScaleState);
    UpdateWholeSceneCaptureBackedHandoffGuard();
    if (line > 0 && register_layer_pre_dirty && CanUseWholeSceneFullFrameFinalizerPath())
    {
        WholeSceneFullFrameFinalizerUnsafeFrame = true;
        WholeSceneNativeProductEpochValid = false;
        if (WholeSceneNativeProductEpochInvalidReason == 0)
            WholeSceneNativeProductEpochInvalidReason = 3;
        auto& unsafeTrace = WholeSceneCurrentUpdateDebugTrace;
        unsafeTrace.FullFrameUnsafeEvents++;
        unsafeTrace.FullFrameUnsafeReasonMask |= state_dirty_reason_mask;
        if (unsafeTrace.FullFrameUnsafeFirstLine < 0 || line < unsafeTrace.FullFrameUnsafeFirstLine)
            unsafeTrace.FullFrameUnsafeFirstLine = line;
        if (line > unsafeTrace.FullFrameUnsafeLastLine)
            unsafeTrace.FullFrameUnsafeLastLine = line;
        unsafeTrace.FullFrameUnsafeLayerMask |= register_layer_pre_dirty;
        unsafeTrace.FullFrameUnsafeDispCntDiff |= dispcnt_diff;
        unsafeTrace.FullFrameUnsafeLayerEnableDiff |= layer_diff;
        for (int layer = 0; layer < 4; layer++)
            unsafeTrace.FullFrameUnsafeBGCntDiff[layer] |= bgcnt_diff[layer];
        unsafeTrace.FullFrameUnsafeMiscDiffMask |= state_dirty_misc_diff_mask;
    }
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.Classify, ElapsedUS(phaseStart));

    // if needed, render sprites

    if ((comp_dirty || SpriteDirty) && (line > 0))
    {
        phaseStart = std::chrono::steady_clock::now();
        DoRenderSprites(line);

        if (CanUseWholeSceneLegacyPath() || CanUseWholeSceneHighResPath() ||
            CanUseWholeSceneOverlayOperatorPath() || CanUseWholeSceneFinalUpscalePath() ||
            CanUsePhysicalFinalPostprocessNativeInputPath())
            DoRenderSpritesNative(line);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.SpriteRender, ElapsedUS(phaseStart));
    }

    // if needed, composite the previous screen section

    if (comp_dirty && (line > 0))
    {
        phaseStart = std::chrono::steady_clock::now();
        CountWholeScenePartialComposite(LastLine, line);
        RenderScreen(LastLine, line);
        LastLine = line;
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.PartialComposite, ElapsedUS(phaseStart));
    }

    // update registers
    phaseStart = std::chrono::steady_clock::now();

    UpdateCachedRegistersAndLayerConfig(layer_pre_dirty);

    UpdateScanlineConfig(line);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.RegisterCache, ElapsedUS(phaseStart));

    // update VRAM and palettes

    if (bgDirty)
    {
        phaseStart = std::chrono::steady_clock::now();
        UploadBGVRAM(bgDirty, line);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.BGUpload, ElapsedUS(phaseStart));
    }

    if ((GPU.PaletteDirty & palmask) || bgExtPalDirty.CheckRange(0, 64))
    {
        phaseStart = std::chrono::steady_clock::now();
        UploadBGPalette(GPU.PaletteDirty & palmask, bgExtPalDirty);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.BGPaletteUpload, ElapsedUS(phaseStart));
    }

    GPU.PaletteDirty &= ~palmask;

    if (layer_pre_dirty)
    {
        phaseStart = std::chrono::steady_clock::now();
        PrerenderDirtyLayers(layer_pre_dirty,
                             row_limited_bitmap_prerender,
                             row_limited_bitmap_first,
                             row_limited_bitmap_last,
                             line);
        ClearLayerPrerenderDeferred(layer_pre_dirty);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.LayerPrerender, ElapsedUS(phaseStart));
    }

    if (SpriteDirty)
    {
        phaseStart = std::chrono::steady_clock::now();
        // OAM and VRAM have already been updated prior
        // palette needs to be updated here though

        // TODO make this only do it over the required subsection?
        NumSprites = 0;
        SpriteUseMosaic = false;
        UpdateOAM(0, 192);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, VRAMTex_OBJ);

        memcpy(&TempPalBuffer[0], &GPU.Palette[GPU2D.Num ? 0x600 : 0x200], 256*2);
        {
            u16* pal = GPU2D.GetOBJExtPal();
            memcpy(&TempPalBuffer[256], pal, 256*16*2);
        }

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, PalTex_OBJ);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1+16, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, TempPalBuffer);

        PrerenderSprites();

        LastSpriteLine = line;
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OBJPrerender, ElapsedUS(phaseStart));
    }

    LayerConfigDirty = false;
    SpriteDirty = false;
}


void GLRenderer2D::DrawScanline(u32 line)
{
    UpdateAndRender(line);
}

void GLRenderer2D::VBlank()
{
    WholeSceneScaleState = ClassifyWholeSceneScalePath();
    RecordWholeSceneNativeProductEligibility(WholeSceneScaleState);
    UpdateWholeSceneCaptureBackedHandoffGuard();

    auto phaseStart = std::chrono::steady_clock::now();
    DoRenderSprites(192);
    if (CanUseWholeSceneLegacyPath() || CanUseWholeSceneHighResPath() ||
        CanUseWholeSceneOverlayOperatorPath() || CanUseWholeSceneFinalUpscalePath() ||
        CanUsePhysicalFinalPostprocessNativeInputPath())
        DoRenderSpritesNative(192);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.VBlankSpriteRender, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderScreen(LastLine, 192);
    const bool chunkedUnsafeOverlay =
        WholeSceneFullFrameFinalizerUnsafeFrame &&
        WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::Off &&
        CanUseWholeSceneOverlayOperatorPath();
    const bool captureBackedHandoff =
        CanUseWholeSceneScalePath() &&
        CanUseWholeSceneOverlayOperatorPath() &&
        ShouldUseWholeSceneCaptureBackedHandoffForRange(LastLine, 192);
    if (CanUseWholeSceneScalePath() && !chunkedUnsafeOverlay && !captureBackedHandoff)
        RenderScreenWholeSceneFinalizeFullFrame();
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.VBlankComposite, ElapsedUS(phaseStart));

    LastSpriteLine = 0;
    LastLine = 0;
}

void GLRenderer2D::VBlankEnd()
{
}


void GLRenderer2D::UpdateScanlineConfig(int line)
{
    auto& cfg = ScanlineConfig.uScanline[line];

    // update BG layer coordinates
    // Y coordinates are adjusted to account for vertical mosaic
    // horizontal mosaic will be done during compositing

    u32 bgmode = DispCnt & 0x7;
    bool xmosaic = (GPU2D.BGMosaicSize[0] > 0);

    if (DispCnt & (1<<3))
    {
        // 3D layer
        int xpos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
        cfg.BGOffset[0][0] = xpos - ((xpos & 0x100) << 1);
        cfg.BGOffset[0][1] = line;
        cfg.BGMosaicEnable[0] = false;
    }
    else
    {
        // text layer
        cfg.BGOffset[0][0] = GPU2D.BGXPos[0];
        if (GPU2D.BGCnt[0] & (1<<6))
        {
            cfg.BGOffset[0][1] = GPU2D.BGYPos[0] + GPU2D.BGMosaicLine;
            cfg.BGMosaicEnable[0] = xmosaic;
        }
        else
        {
            cfg.BGOffset[0][1] = GPU2D.BGYPos[0] + line;
            cfg.BGMosaicEnable[0] = false;
        }
    }

    // always a text layer
    cfg.BGOffset[1][0] = GPU2D.BGXPos[1];
    if (GPU2D.BGCnt[1] & (1<<6))
    {
        cfg.BGOffset[1][1] = GPU2D.BGYPos[1] + GPU2D.BGMosaicLine;
        cfg.BGMosaicEnable[1] = xmosaic;
    }
    else
    {
        cfg.BGOffset[1][1] = GPU2D.BGYPos[1] + line;
        cfg.BGMosaicEnable[1] = false;
    }

    if ((bgmode == 2) || (bgmode >= 4 && bgmode <= 6))
    {
        // rotscale layer
        cfg.BGOffset[2][0] = GPU2D.BGXRefInternal[0];
        cfg.BGOffset[2][1] = GPU2D.BGYRefInternal[0];
        cfg.BGRotscale[0][0] = GPU2D.BGRotA[0];
        cfg.BGRotscale[0][1] = GPU2D.BGRotB[0];
        cfg.BGRotscale[0][2] = GPU2D.BGRotC[0];
        cfg.BGRotscale[0][3] = GPU2D.BGRotD[0];
    }
    else
    {
        // text layer
        cfg.BGOffset[2][0] = GPU2D.BGXPos[2];
        if (GPU2D.BGCnt[2] & (1<<6))
            cfg.BGOffset[2][1] = GPU2D.BGYPos[2] + GPU2D.BGMosaicLine;
        else
            cfg.BGOffset[2][1] = GPU2D.BGYPos[2] + line;
    }

    if (GPU2D.BGCnt[2] & (1<<6))
        cfg.BGMosaicEnable[2] = xmosaic;
    else
        cfg.BGMosaicEnable[2] = false;

    if (bgmode >= 1 && bgmode <= 5)
    {
        // rotscale layer
        cfg.BGOffset[3][0] = GPU2D.BGXRefInternal[1];
        cfg.BGOffset[3][1] = GPU2D.BGYRefInternal[1];
        cfg.BGRotscale[1][0] = GPU2D.BGRotA[1];
        cfg.BGRotscale[1][1] = GPU2D.BGRotB[1];
        cfg.BGRotscale[1][2] = GPU2D.BGRotC[1];
        cfg.BGRotscale[1][3] = GPU2D.BGRotD[1];
    }
    else
    {
        // text layer
        cfg.BGOffset[3][0] = GPU2D.BGXPos[3];
        if (GPU2D.BGCnt[3] & (1<<6))
            cfg.BGOffset[3][1] = GPU2D.BGYPos[3] + GPU2D.BGMosaicLine;
        else
            cfg.BGOffset[3][1] = GPU2D.BGYPos[3] + line;
    }

    if (GPU2D.BGCnt[3] & (1<<6))
        cfg.BGMosaicEnable[3] = xmosaic;
    else
        cfg.BGMosaicEnable[3] = false;

    u16* pal = (u16*)&GPU.Palette[GPU2D.Num ? 0x400 : 0];
    cfg.BackColor = pal[0];

    // mosaic

    cfg.MosaicSize[0] = GPU2D.BGMosaicSize[0];
    cfg.MosaicSize[1] = GPU2D.BGMosaicSize[1];
    cfg.MosaicSize[2] = GPU2D.OBJMosaicSize[0];
    cfg.MosaicSize[3] = GPU2D.OBJMosaicSize[1];

    // windows

    //cfg.WinRegs = GPU2D.WinCnt[2] | (GPU2D.WinCnt[3] << 8) | (GPU2D.WinCnt[1] << 16) | (GPU2D.WinCnt[0] << 24);
    if (GPU2D.DispCnt & 0xE000)
        cfg.WinRegs = GPU2D.WinCnt[2];
    else
        cfg.WinRegs = 0xFF;

    if (GPU2D.DispCnt & (1<<15))
        cfg.WinRegs |= (GPU2D.WinCnt[3] << 8);
    else
        cfg.WinRegs |= 0xFF00;

    if (GPU2D.DispCnt & (1<<14))
        cfg.WinRegs |= (GPU2D.WinCnt[1] << 16);
    else
        cfg.WinRegs |= 0xFF0000;

    if (GPU2D.DispCnt & (1<<13))
        cfg.WinRegs |= (GPU2D.WinCnt[0] << 24);
    else
        cfg.WinRegs |= 0xFF000000;

    cfg.WinMask = 0;

    if ((GPU2D.DispCnt & (1<<13)) && (GPU2D.Win0Active & 0x1))
    {
        int x0 = GPU2D.Win0Coords[0];
        int x1 = GPU2D.Win0Coords[1];

        if (x0 <= x1)
        {
            cfg.WinPos[0] = x0;
            cfg.WinPos[1] = x1;
            if (GPU2D.Win0Active == 0x3)
                cfg.WinMask |= (1<<0);
            cfg.WinMask |= (1<<1);
            GPU2D.Win0Active &= ~0x2;
        }
        else
        {
            cfg.WinPos[0] = x1;
            cfg.WinPos[1] = x0;
            if (GPU2D.Win0Active == 0x3)
                cfg.WinMask |= (1<<0);
            cfg.WinMask |= (1<<2);
            GPU2D.Win0Active |= 0x2;
        }
    }
    else
    {
        cfg.WinPos[0] = 256;
        cfg.WinPos[1] = 256;
    }

    if ((GPU2D.DispCnt & (1<<14)) && (GPU2D.Win1Active & 0x1))
    {
        int x0 = GPU2D.Win1Coords[0];
        int x1 = GPU2D.Win1Coords[1];

        if (x0 <= x1)
        {
            cfg.WinPos[2] = x0;
            cfg.WinPos[3] = x1;
            if (GPU2D.Win1Active == 0x3)
                cfg.WinMask |= (1<<3);
            cfg.WinMask |= (1<<4);
            GPU2D.Win1Active &= ~0x2;
        }
        else
        {
            cfg.WinPos[2] = x1;
            cfg.WinPos[3] = x0;
            if (GPU2D.Win1Active == 0x3)
                cfg.WinMask |= (1<<3);
            cfg.WinMask |= (1<<5);
            GPU2D.Win1Active |= 0x2;
        }
    }
    else
    {
        cfg.WinPos[2] = 256;
        cfg.WinPos[3] = 256;
    }
};

void GLRenderer2D::UpdateLayerConfig()
{
    // determine which parts of VRAM were used for captures
    int capturemask = GPU2D.Num ? 0x7 : 0x1F;
    int captureinfo[32];
    GPU2D.GetCaptureInfo_BG(captureinfo);

    u32 tilebase, mapbase;
    if (!GPU2D.Num)
    {
        tilebase = ((GPU2D.DispCnt >> 24) & 0x7) << 16;
        mapbase = ((GPU2D.DispCnt >> 27) & 0x7) << 16;
    }
    else
    {
        tilebase = 0;
        mapbase = 0;
    }

    int layertype[4] = {1, 1, 0, 0};
    switch (GPU2D.DispCnt & 0x7)
    {
        case 0: layertype[2] = 1; layertype[3] = 1; break;
        case 1: layertype[2] = 1; layertype[3] = 2; break;
        case 2: layertype[2] = 2; layertype[3] = 2; break;
        case 3: layertype[2] = 1; layertype[3] = 3; break;
        case 4: layertype[2] = 2; layertype[3] = 3; break;
        case 5: layertype[2] = 3; layertype[3] = 3; break;
        case 6: layertype[0] = 0; layertype[1] = 0;
                layertype[2] = 4; layertype[3] = 0; break;
        case 7: layertype[2] = 0; layertype[3] = 0; break;
    }

    for (int layer = 0; layer < 4; layer++)
    {
        int type = layertype[layer];
        if (!type)
            continue;

        u16 bgcnt = GPU2D.BGCnt[layer];
        auto& cfg = LayerConfig.uBGConfig[layer];

        cfg.TileOffset = tilebase + (((bgcnt >> 2) & 0xF) << 14);
        cfg.MapOffset = mapbase + (((bgcnt >> 8) & 0x1F) << 11);
        cfg.PalOffset = 0;

        BGVRAMRange[layer][0] = cfg.TileOffset;
        BGVRAMRange[layer][2] = cfg.MapOffset;

        if ((layer == 0) && (GPU2D.DispCnt & (1<<3)))
        {
            // 3D layer

            cfg.Size[0] = 256; cfg.Size[1] = 192;
            cfg.Type = 6;
            cfg.Clamp = 1;

            BGVRAMRange[layer][0] = 0xFFFFFFFF;
            BGVRAMRange[layer][1] = 0xFFFFFFFF;
            BGVRAMRange[layer][2] = 0xFFFFFFFF;
            BGVRAMRange[layer][3] = 0xFFFFFFFF;
        }
        else if (type == 1)
        {
            // text layer

            u32 tilesz, mapsz;
            switch (bgcnt >> 14)
            {
                case 0: cfg.Size[0] = 256; cfg.Size[1] = 256; mapsz = 0x800; break;
                case 1: cfg.Size[0] = 512; cfg.Size[1] = 256; mapsz = 0x1000; break;
                case 2: cfg.Size[0] = 256; cfg.Size[1] = 512; mapsz = 0x1000; break;
                case 3: cfg.Size[0] = 512; cfg.Size[1] = 512; mapsz = 0x2000; break;
            }

            if (bgcnt & (1<<7))
            {
                // 256-color
                cfg.Type = 1;
                if (DispCnt & (1<<30))
                {
                    // extended palette
                    int paloff = layer;
                    if ((layer < 2) && (bgcnt & (1<<13)))
                        paloff += 2;
                    cfg.PalOffset = 1 + (16 * paloff);
                }

                tilesz = 0x10000;
            }
            else
            {
                // 16-color
                cfg.Type = 0;

                tilesz = 0x8000;
            }

            cfg.Clamp = 0;

            int n = BGBaseIndex[0][bgcnt >> 14] + layer;
            BGLayerTex[layer] = AllBGLayerTex[n];
            BGLayerMetaTex[layer] = AllBGLayerMetaTex[n];
            BGLayerFB[layer] = AllBGLayerFB[n];

            BGVRAMRange[layer][1] = tilesz;
            BGVRAMRange[layer][3] = mapsz;
        }
        else if (type == 2)
        {
            // affine layer

            u32 mapsz;
            switch (bgcnt >> 14)
            {
                case 0: cfg.Size[0] = 128; cfg.Size[1] = 128; mapsz = 0x100; break;
                case 1: cfg.Size[0] = 256; cfg.Size[1] = 256; mapsz = 0x400; break;
                case 2: cfg.Size[0] = 512; cfg.Size[1] = 512; mapsz = 0x1000; break;
                case 3: cfg.Size[0] = 1024; cfg.Size[1] = 1024; mapsz = 0x4000; break;
            }

            cfg.Type = 2;
            cfg.Clamp = !(bgcnt & (1<<13));

            int n = BGBaseIndex[1][bgcnt >> 14] + layer - 2;
            BGLayerTex[layer] = AllBGLayerTex[n];
            BGLayerMetaTex[layer] = AllBGLayerMetaTex[n];
            BGLayerFB[layer] = AllBGLayerFB[n];

            BGVRAMRange[layer][1] = 0x4000;
            BGVRAMRange[layer][3] = mapsz;
        }
        else if (type == 3)
        {
            // extended layer

            if (bgcnt & (1<<7))
            {
                // bitmap modes

                u32 mapsz;
                switch (bgcnt >> 14)
                {
                    case 0: cfg.Size[0] = 128; cfg.Size[1] = 128; mapsz = 0x4000; break;
                    case 1: cfg.Size[0] = 256; cfg.Size[1] = 256; mapsz = 0x10000; break;
                    case 2: cfg.Size[0] = 512; cfg.Size[1] = 256; mapsz = 0x20000; break;
                    case 3: cfg.Size[0] = 512; cfg.Size[1] = 512; mapsz = 0x40000; break;
                }

                u32 tileoffset = 0;
                u32 mapoffset = ((bgcnt >> 8) & 0x1F) << 14;

                BGVRAMRange[layer][0] = 0xFFFFFFFF;
                BGVRAMRange[layer][1] = 0xFFFFFFFF;
                BGVRAMRange[layer][2] = mapoffset;
                BGVRAMRange[layer][3] = mapsz;

                if (bgcnt & (1<<2))
                {
                    mapsz <<= 1;

                    int capblock = -1;
                    if ((cfg.Size[0] == 128) || (cfg.Size[0] == 256))
                    {
                        // if this is a direct color bitmap, and the width is 128 or 256
                        // then it might be a display capture
                        u32 startaddr = mapoffset;
                        u32 endaddr = startaddr + mapsz;

                        startaddr >>= 14;
                        endaddr = (endaddr + 0x3FFF) >> 14;

                        for (u32 b = startaddr; b < endaddr; b++)
                        {
                            int blk = captureinfo[b & capturemask];
                            if (blk == -1) continue;

                            capblock = blk;
                        }
                    }

                    if (capblock != -1)
                    {
                        if (cfg.Size[0] == 128)
                        {
                            cfg.Type = 7;
                            tileoffset = capblock;
                            mapoffset = (mapoffset >> 8) & 0x7F;
                        }
                        else
                        {
                            cfg.Type = 8;
                            tileoffset = capblock >> 2;
                            mapoffset = (mapoffset >> 9) & 0xFF;
                        }
                    }
                    else
                        cfg.Type = 5;
                }
                else
                    cfg.Type = 4;

                cfg.TileOffset = tileoffset;
                cfg.MapOffset = mapoffset;

                int n = BGBaseIndex[2][bgcnt >> 14] + layer - 2;
                BGLayerTex[layer] = AllBGLayerTex[n];
                BGLayerMetaTex[layer] = AllBGLayerMetaTex[n];
                BGLayerFB[layer] = AllBGLayerFB[n];
            }
            else
            {
                // rotscale w/ tiles

                u32 mapsz;
                switch (bgcnt >> 14)
                {
                    case 0: cfg.Size[0] = 128; cfg.Size[1] = 128; mapsz = 0x200; break;
                    case 1: cfg.Size[0] = 256; cfg.Size[1] = 256; mapsz = 0x800; break;
                    case 2: cfg.Size[0] = 512; cfg.Size[1] = 512; mapsz = 0x2000; break;
                    case 3: cfg.Size[0] = 1024; cfg.Size[1] = 1024; mapsz = 0x8000; break;
                }

                // this layer type is always 256-color
                cfg.Type = 3;
                if (DispCnt & (1<<30))
                {
                    // extended palette
                    int paloff = layer;
                    if ((layer < 2) && (bgcnt & (1<<13)))
                        paloff += 2;
                    cfg.PalOffset = 1 + (16 * paloff);
                }

                int n = BGBaseIndex[1][bgcnt >> 14] + layer - 2;
                BGLayerTex[layer] = AllBGLayerTex[n];
                BGLayerMetaTex[layer] = AllBGLayerMetaTex[n];
                BGLayerFB[layer] = AllBGLayerFB[n];

                BGVRAMRange[layer][1] = 0x10000;
                BGVRAMRange[layer][3] = mapsz;
            }

            cfg.Clamp = !(bgcnt & (1<<13));
        }
        else //if (type == 4)
        {
            // large layer

            u32 mapsz;
            switch (bgcnt >> 14)
            {
                case 0: cfg.Size[0] = 512; cfg.Size[1] = 1024; mapsz = 0x80000; break;
                case 1: cfg.Size[0] = 1024; cfg.Size[1] = 512; mapsz = 0x80000; break;
                case 2: cfg.Size[0] = 512; cfg.Size[1] = 256; mapsz = 0x20000; break;
                case 3: cfg.Size[0] = 512; cfg.Size[1] = 512; mapsz = 0x40000; break;
            }

            cfg.Type = 4;
            cfg.TileOffset = 0;
            cfg.MapOffset = 0;
            cfg.Clamp = !(bgcnt & (1<<13));

            int n = BGBaseIndex[3][bgcnt >> 14];
            BGLayerTex[layer] = AllBGLayerTex[n];
            BGLayerMetaTex[layer] = AllBGLayerMetaTex[n];
            BGLayerFB[layer] = AllBGLayerFB[n];

            BGVRAMRange[layer][0] = 0xFFFFFFFF;
            BGVRAMRange[layer][1] = 0xFFFFFFFF;
            BGVRAMRange[layer][3] = mapsz;
        }
    }

    glBindBuffer(GL_UNIFORM_BUFFER, LayerConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LayerConfig), &LayerConfig);
}

void GLRenderer2D::UpdateOAM(int ystart, int yend)
{
    auto& cfg = SpriteConfig;
    u16* oam = OAM;

    // determine which parts of VRAM were used for captures
    int capturemask = GPU2D.Num ? 0x7 : 0xF;
    int captureinfo[16];
    GPU2D.GetCaptureInfo_OBJ(captureinfo);

    for (int i = 0; i < 32; i++)
    {
        s16* rotscale = (s16*)&oam[(i * 16) + 3];
        auto& rotdst = cfg.uRotscale[i];

        rotdst[0] = rotscale[0];
        rotdst[1] = rotscale[4];
        rotdst[2] = rotscale[8];
        rotdst[3] = rotscale[12];
    }

    const u8 spritewidth[16] =
    {
        8, 16, 8, 8,
        16, 32, 8, 8,
        32, 32, 16, 8,
        64, 64, 32, 8
    };
    const u8 spriteheight[16] =
    {
        8, 8, 16, 8,
        16, 8, 32, 8,
        32, 16, 32, 8,
        64, 32, 64, 8
    };

    for (int sprnum = 0; sprnum < 128; sprnum++)
    {
        u16* attrib = &oam[sprnum * 4];

        u32 sprtype = (attrib[0] >> 8) & 0x3;
        if (sprtype == 2) // sprite disabled
            continue;

        // note on sprite position:
        // X > 255 is interpreted as negative (-256..-1)
        // Y > 127 is interpreted as both positive (128..255) and negative (-128..-1)

        s32 xpos = (s32)(attrib[1] << 23) >> 23;
        s32 ypos = (s32)(attrib[0] << 24) >> 24;

        u32 sizeparam = (attrib[0] >> 14) | ((attrib[1] & 0xC000) >> 12);
        s32 width = spritewidth[sizeparam];
        s32 height = spriteheight[sizeparam];
        s32 boundwidth = width;
        s32 boundheight = height;

        if (sprtype == 3)
        {
            // double-size rotscale sprite
            boundwidth <<= 1;
            boundheight <<= 1;
        }

        if (xpos <= -boundwidth)
            continue;

        bool yc0 = ((ypos + boundheight) > ystart) && (ypos < yend);
        bool yc1 = (((ypos&0xFF) + boundheight) > ystart) && ((ypos&0xFF) < yend);
        if (!(yc0 || yc1))
            continue;

        u32 sprmode = (attrib[0] >> 10) & 0x3;
        if (sprmode == 3)
        {
            if ((GPU2D.DispCnt & 0x60) == 0x60)
                continue;
            if ((attrib[2] >> 12) == 0)
                continue;
        }

        if (NumSprites >= 128)
        {
            Log(LogLevel::Error, "GPU2D_OpenGL: SPRITE BUFFER IS FULL!!!!!\n");
            break;
        }

        // add this sprite to the OAM array

        auto& sprcfg = cfg.uOAM[NumSprites];

        sprcfg.Position[0] = (u32)xpos;
        sprcfg.Position[1] = (u32)ypos;
        sprcfg.Size[0] = width;
        sprcfg.Size[1] = height;
        sprcfg.BoundSize[0] = boundwidth;
        sprcfg.BoundSize[1] = boundheight;

        if (sprtype & 1)
        {
            sprcfg.Flip[0] = 0;
            sprcfg.Flip[1] = 0;
            sprcfg.Rotscale = (attrib[1] >> 9) & 0x1F;
        }
        else
        {
            sprcfg.Flip[0] = !!(attrib[1] & (1<<12));
            sprcfg.Flip[1] = !!(attrib[1] & (1<<13));
            sprcfg.Rotscale = (u32)-1;
        }

        sprcfg.OBJMode = sprmode;
        sprcfg.Mosaic = !!(attrib[0] & (1<<12)) && (sprmode != 2);
        sprcfg.BGPrio = (attrib[2] >> 10) & 0x3;

        u32 tilenum = attrib[2] & 0x3FF;

        if (sprmode == 3)
        {
            // bitmap sprite

            sprcfg.Type = 2;

            if (GPU2D.DispCnt & (1<<6))
            {
                // 1D mapping
                sprcfg.TileOffset = tilenum << (7 + ((GPU2D.DispCnt >> 22) & 0x1));
                sprcfg.TileStride = width * 2;
            }
            else
            {
                bool is256 = !!(GPU2D.DispCnt & (1<<5));
                int capblock = -1;

                u32 tileoffset, tilestride;
                if (is256)
                {
                    // 2D mapping, 256 pixels
                    tileoffset = ((tilenum & 0x01F) << 4) + ((tilenum & 0x3E0) << 7);
                    tilestride = 256 * 2;
                }
                else
                {
                    // 2D mapping, 128 pixels
                    tileoffset = ((tilenum & 0x00F) << 4) + ((tilenum & 0x3F0) << 7);
                    tilestride = 128 * 2;
                }

                // if this is a direct color bitmap, and the width is 128 or 256
                // then it might be a display capture
                u32 startaddr = tileoffset;
                u32 endaddr = startaddr + (height * tilestride);

                startaddr >>= 14;
                endaddr = (endaddr + 0x3FFF) >> 14;

                for (u32 b = startaddr; b < endaddr; b++)
                {
                    int blk = captureinfo[b & capturemask];
                    if (blk == -1) continue;

                    capblock = blk;
                }

                if (capblock != -1)
                {
                    if (!is256)
                    {
                        sprcfg.Type = 3;
                        tilestride = capblock;
                        tileoffset &= 0x7FFF;
                    }
                    else
                    {
                        sprcfg.Type = 4;
                        tilestride = capblock >> 2;
                        tileoffset &= 0x1FFFF;
                    }
                }

                sprcfg.TileOffset = tileoffset;
                sprcfg.TileStride = tilestride;
            }

            sprcfg.PalOffset = 1 + (attrib[2] >> 12); // alpha
        }
        else
        {
            if (GPU2D.DispCnt & (1<<4))
            {
                // 1D mapping
                sprcfg.TileOffset = tilenum << (5 + ((GPU2D.DispCnt >> 20) & 0x3));
                sprcfg.TileStride = (width >> 3) * 32;
                if (attrib[0] & (1<<13))
                    sprcfg.TileStride <<= 1;
            }
            else
            {
                // 2D mapping
                sprcfg.TileOffset = tilenum << 5;
                sprcfg.TileStride = 32 * 32;
            }

            if (attrib[0] & (1<<13))
            {
                // 256-color sprite
                sprcfg.Type = 1;
                if (GPU2D.DispCnt & (1<<31))
                    sprcfg.PalOffset = 1 + (attrib[2] >> 12);
                else
                    sprcfg.PalOffset = 0;
            }
            else
            {
                // 16-color sprite
                sprcfg.Type = 0;
                sprcfg.PalOffset = (attrib[2] >> 12) << 4;
            }
        }

        NumSprites++;

        if (sprcfg.Mosaic && (GPU2D.OBJMosaicSize[0] > 0))
            SpriteUseMosaic = true;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, SpriteConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    0,
                    offsetof(sSpriteConfig, uOAM) + (NumSprites * sizeof(cfg.uOAM[0])),
                    &cfg);
}

void GLRenderer2D::UpdateCompositorConfig()
{
    // compositor info buffer
    for (int i = 0; i < 4; i++)
        CompositorConfig.uBGPrio[i] = -1;

    for (int layer = 0; layer < 4; layer++)
    {
        if (!(LayerEnable & (1 << layer)))
            continue;

        int prio = BGCnt[layer] & 0x3;
        CompositorConfig.uBGPrio[layer] = prio;
    }

    CompositorConfig.uEnableOBJ = !!(LayerEnable & (1<<4));

    CompositorConfig.uEnable3D = !!(DispCnt & (1<<3));

    CompositorConfig.uBlendCnt = BlendCnt;
    CompositorConfig.uBlendEffect = (BlendCnt >> 6) & 0x3;
    CompositorConfig.uBlendCoef[0] = EVA;
    CompositorConfig.uBlendCoef[1] = EVB;
    CompositorConfig.uBlendCoef[2] = EVY;

    glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CompositorConfig), &CompositorConfig);
}


void GLRenderer2D::PrerenderSprites()
{
    u16* vtxbuf = SpritePreVtxData;
    int vtxnum = 0;

    for (int i = 0; i < NumSprites; i++)
    {
        auto& sprite = SpriteConfig.uOAM[i];
        if (sprite.Type >= 3)
            continue;

        *vtxbuf++ = 0; *vtxbuf++ = 1; *vtxbuf++ = i;
        *vtxbuf++ = 1; *vtxbuf++ = 0; *vtxbuf++ = i;
        *vtxbuf++ = 1; *vtxbuf++ = 1; *vtxbuf++ = i;
        *vtxbuf++ = 0; *vtxbuf++ = 1; *vtxbuf++ = i;
        *vtxbuf++ = 0; *vtxbuf++ = 0; *vtxbuf++ = i;
        *vtxbuf++ = 1; *vtxbuf++ = 0; *vtxbuf++ = i;
        vtxnum += 6;
    }

    if (vtxnum == 0) return;

    glUseProgram(SpritePreShader);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glBindBufferBase(GL_UNIFORM_BUFFER, 21, SpriteConfigUBO);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SpriteFB);
    glViewport(0, 0, 1024, 512);

    glBindBuffer(GL_ARRAY_BUFFER, SpritePreVtxBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vtxnum * 3 * sizeof(u16), SpritePreVtxData);

    glBindVertexArray(SpritePreVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, vtxnum);
}

void GLRenderer2D::PrerenderLayer(int layer)
{
    auto& cfg = LayerConfig.uBGConfig[layer];

    if (cfg.Type >= 6)
        return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, BGLayerFB[layer]);

    glUniform1i(LayerPreCurBGULoc, layer);

    // set layer size
    glViewport(0, 0, cfg.Size[0], cfg.Size[1]);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

void GLRenderer2D::PrerenderLayerRows(int layer, int firstRow, int lastRow)
{
    if (layer < 0 || layer >= 4)
        return;

    auto& cfg = LayerConfig.uBGConfig[layer];
    if (cfg.Type >= 6 || cfg.Size[0] <= 0 || cfg.Size[1] <= 0)
        return;

    const int height = static_cast<int>(cfg.Size[1]);
    firstRow = std::clamp(firstRow, 0, height);
    lastRow = std::clamp(lastRow, firstRow, height);
    if (lastRow <= firstRow)
        return;

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, firstRow, cfg.Size[0], lastRow - firstRow);
    PrerenderLayer(layer);
    glDisable(GL_SCISSOR_TEST);
}


void GLRenderer2D::DoRenderSprites(int line)
{
    int ystart = LastSpriteLine;
    int yend = line;
    int spriteFilterMode = WholeSceneHighResSpriteFilterMode();

    glUseProgram(SpriteShader);
    glUniform1i(SpriteFilterModeULoc, spriteFilterMode);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glBindBufferBase(GL_UNIFORM_BUFFER, 21, SpriteConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 24, SpriteScanlineConfigUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, SpriteScanlineConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    ystart * sizeof(s32),
                    (yend - ystart) * sizeof(s32),
                    &SpriteScanlineConfig.uMosaicLine[ystart]);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OBJLayerFB);
    glViewport(0, 0, ScreenW, ScreenH);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, SpriteTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput128Tex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput256Tex);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * ScaleFactor, ScreenW, (yend-ystart) * ScaleFactor);

    // NOTE
    // this requires two passes for mosaic emulation, because mosaic flags get set for
    // transparent pixels too, and priority is only checked against opaque pixels

    glClearColor(0, 0, 0, 0);
    glClearDepth(1);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);

    if (SpriteUseMosaic)
    {
        glUniform1i(SpriteRenderTransULoc, 1);
        glColorMaski(1, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);

        RenderSprites(false, ystart, yend);
    }

    glUniform1i(SpriteRenderTransULoc, 0);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);

    RenderSprites(true, ystart, yend);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_FALSE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    RenderSprites(false, ystart, yend);

    if (spriteFilterMode != 0)
        glUniform1i(SpriteFilterModeULoc, 0);

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::DoRenderSpritesNative(int line)
{
    int ystart = LastSpriteLine;
    int yend = line;

    glUseProgram(SpriteShader);
    glUniform1i(SpriteFilterModeULoc, 0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glBindBufferBase(GL_UNIFORM_BUFFER, 21, SpriteConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 24, SpriteScanlineConfigUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, SpriteScanlineConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    ystart * sizeof(s32),
                    (yend - ystart) * sizeof(s32),
                    &SpriteScanlineConfig.uMosaicLine[ystart]);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, NativeOBJLayerFB);
    glViewport(0, 0, 256, 192);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, SpriteTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput128Tex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput256Tex);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart, 256, (yend-ystart));

    glClearColor(0, 0, 0, 0);
    glClearDepth(1);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);

    if (SpriteUseMosaic)
    {
        glUniform1i(SpriteRenderTransULoc, 1);
        glColorMaski(1, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);

        RenderSprites(false, ystart, yend);
    }

    glUniform1i(SpriteRenderTransULoc, 0);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);

    RenderSprites(true, ystart, yend);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_FALSE, GL_TRUE);

    RenderSprites(false, ystart, yend);

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::RenderSprites(bool window, int ystart, int yend)
{
    if (window)
    {
        if (!(GPU2D.DispCnt & (1<<15)))
            return;
    }

    u16* vtxbuf = SpriteVtxData;
    int vtxnum = 0;

    for (int i = 0; i < NumSprites; i++)
    {
        auto& sprite = SpriteConfig.uOAM[i];

        bool iswin = (sprite.OBJMode == 2);
        if (iswin != window)
            continue;

        s32 xpos = sprite.Position[0];
        s32 ypos = sprite.Position[1];
        s32 boundwidth = sprite.BoundSize[0];
        s32 boundheight = sprite.BoundSize[1];

        bool yc0 = ((ypos + boundheight) > ystart) && (ypos < yend);
        bool yc1 = (((ypos&0xFF) + boundheight) > ystart) && ((ypos&0xFF) < yend);

        if (yc0)
        {
            s32 x0 = xpos, x1 = xpos + boundwidth;
            s32 y0 = ypos, y1 = ypos + boundheight;

            *vtxbuf++ = x0; *vtxbuf++ = y1; *vtxbuf++ = 0; *vtxbuf++ = 1; *vtxbuf++ = i;
            *vtxbuf++ = x1; *vtxbuf++ = y0; *vtxbuf++ = 1; *vtxbuf++ = 0; *vtxbuf++ = i;
            *vtxbuf++ = x1; *vtxbuf++ = y1; *vtxbuf++ = 1; *vtxbuf++ = 1; *vtxbuf++ = i;
            *vtxbuf++ = x0; *vtxbuf++ = y1; *vtxbuf++ = 0; *vtxbuf++ = 1; *vtxbuf++ = i;
            *vtxbuf++ = x0; *vtxbuf++ = y0; *vtxbuf++ = 0; *vtxbuf++ = 0; *vtxbuf++ = i;
            *vtxbuf++ = x1; *vtxbuf++ = y0; *vtxbuf++ = 1; *vtxbuf++ = 0; *vtxbuf++ = i;
            vtxnum += 6;
        }

        if (yc1)
        {
            ypos &= 0xFF;
            s32 x0 = xpos, x1 = xpos + boundwidth;
            s32 y0 = ypos, y1 = ypos + boundheight;

            *vtxbuf++ = x0; *vtxbuf++ = y1; *vtxbuf++ = 0; *vtxbuf++ = 1; *vtxbuf++ = i;
            *vtxbuf++ = x1; *vtxbuf++ = y0; *vtxbuf++ = 1; *vtxbuf++ = 0; *vtxbuf++ = i;
            *vtxbuf++ = x1; *vtxbuf++ = y1; *vtxbuf++ = 1; *vtxbuf++ = 1; *vtxbuf++ = i;
            *vtxbuf++ = x0; *vtxbuf++ = y1; *vtxbuf++ = 0; *vtxbuf++ = 1; *vtxbuf++ = i;
            *vtxbuf++ = x0; *vtxbuf++ = y0; *vtxbuf++ = 0; *vtxbuf++ = 0; *vtxbuf++ = i;
            *vtxbuf++ = x1; *vtxbuf++ = y0; *vtxbuf++ = 1; *vtxbuf++ = 0; *vtxbuf++ = i;
            vtxnum += 6;
        }
    }

    if (vtxnum == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, SpriteVtxBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vtxnum * 5 * sizeof(u16), SpriteVtxData);

    glBindVertexArray(SpriteVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, vtxnum);
}

void GLRenderer2D::RenderCompositorPass(GLuint outputFB, GLuint objLayerTex,
                                        int viewportW, int viewportH,
                                        int ystart, int yend,
                                        int compositorScale,
                                        int layerFilterMode,
                                        bool layerFilterNoWrap,
                                        bool objNativeResolution,
                                        bool debugTint,
                                        GLuint direct3DTex,
                                        GLuint direct3DCoverageTex,
                                        bool forceOBJDisabled,
                                        bool preserveCompositorConfig,
                                        GLuint capture128Tex,
                                        GLuint capture256Tex)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, outputFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, viewportW, viewportH);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * compositorScale, viewportW, (yend-ystart) * compositorScale);

    if (ForcedBlank || !UnitEnabled)
    {
        if (!UnitEnabled)
        {
            if (GPU2D.Num)
                glClearColor(1, 1, 1, 1);
            else
                glClearColor(0, 0, 0, 1);
        }
        else
            glClearColor(1, 1, 1, 1);

        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
        return;
    }

    glUseProgram(CompositorShader);
    glUniform1i(CompositorScaleULoc, compositorScale);
    glUniform1i(CompositorOBJNativeResolutionULoc, objNativeResolution);
    glUniform1i(CompositorLayerFilterModeULoc, layerFilterMode);
    glUniform1i(CompositorLayerFilterNoWrapULoc, layerFilterNoWrap);
    glUniform1i(CompositorDebugTintULoc, debugTint);
    glUniform1i(CompositorSplit3DSemanticsULoc,
                WholeSceneScaleFinalUpscale3DSplitSemantics && direct3DCoverageTex != 0);
    glUniform1i(CompositorSharpenSplit3DCoverageULoc,
                WholeSceneScaleFinalUpscale3DSharpenSplitCoverage);

    glBindBufferBase(GL_UNIFORM_BUFFER, 20, LayerConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 23, CompositorConfigUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, ScanlineConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    ystart * sizeof(sScanlineConfig::sScanline),
                    (yend - ystart) * sizeof(sScanlineConfig::sScanline),
                    &ScanlineConfig.uScanline[ystart]);

    if (!preserveCompositorConfig)
        UpdateCompositorConfig();
    if (preserveCompositorConfig || forceOBJDisabled)
    {
        if (forceOBJDisabled)
            CompositorConfig.uEnableOBJ = false;
        glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CompositorConfig), &CompositorConfig);
    }

    for (int i = 0; i < 4; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);

        if ((i == 0) && (DispCnt & (1<<3)))
            glBindTexture(GL_TEXTURE_2D, direct3DTex ? direct3DTex : Parent.OutputTex3D);
        else
            glBindTexture(GL_TEXTURE_2D, BGLayerTex[i]);

        GLint wrapmode = LayerConfig.uBGConfig[i].Clamp ? GL_CLAMP_TO_BORDER : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapmode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapmode);
    }

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, objLayerTex);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, capture128Tex ? capture128Tex : Parent.CaptureOutput128Tex);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, capture256Tex ? capture256Tex : Parent.CaptureOutput256Tex);

    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, MosaicTex);

    for (int i = 0; i < 4; i++)
    {
        glActiveTexture(GL_TEXTURE8 + i);

        if ((i == 0) && (DispCnt & (1<<3)))
            glBindTexture(GL_TEXTURE_2D, BlankColorTex);
        else if (BGLayerMetaTex[i] != 0)
            glBindTexture(GL_TEXTURE_2D, BGLayerMetaTex[i]);
        else
            glBindTexture(GL_TEXTURE_2D, BlankColorTex);
    }

    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, direct3DCoverageTex ? direct3DCoverageTex :
                                    (direct3DTex ? direct3DTex : Parent.OutputTex3D));
    GLint direct3DWrapMode = LayerConfig.uBGConfig[0].Clamp ? GL_CLAMP_TO_BORDER : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, direct3DWrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, direct3DWrapMode);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    if (compositorScale != ScaleFactor)
        glUniform1i(CompositorScaleULoc, ScaleFactor);
    if (objNativeResolution)
        glUniform1i(CompositorOBJNativeResolutionULoc, 0);
    if (layerFilterMode != 0)
        glUniform1i(CompositorLayerFilterModeULoc, 0);
    if (layerFilterNoWrap)
        glUniform1i(CompositorLayerFilterNoWrapULoc, 0);
    if (debugTint)
        glUniform1i(CompositorDebugTintULoc, 0);
    if (WholeSceneScaleFinalUpscale3DSplitSemantics && direct3DCoverageTex != 0)
        glUniform1i(CompositorSplit3DSemanticsULoc, 0);

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::RenderNativePrepass(int ystart, int yend)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, NativeOutputFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, 256, 192);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart, 256, (yend-ystart));

    if (ForcedBlank || !UnitEnabled)
    {
        if (!UnitEnabled)
        {
            if (GPU2D.Num)
                glClearColor(1, 1, 1, 1);
            else
                glClearColor(0, 0, 0, 1);
        }
        else
            glClearColor(1, 1, 1, 1);

        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
        return;
    }

    glUseProgram(NativePrepassShader);
    glUniform1i(NativePrepassScaleULoc, 1);
    glUniform1i(NativePrepassDebugLayerULoc, -1);

    glBindBufferBase(GL_UNIFORM_BUFFER, 20, LayerConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 23, CompositorConfigUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, ScanlineConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    ystart * sizeof(sScanlineConfig::sScanline),
                    (yend - ystart) * sizeof(sScanlineConfig::sScanline),
                    &ScanlineConfig.uScanline[ystart]);

    UpdateCompositorConfig();

    for (int i = 0; i < 4; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        if ((i == 0) && !GPU2D.Num && (DispCnt & (1<<3)))
            glBindTexture(GL_TEXTURE_2D, BlankColorTex);
        else
            glBindTexture(GL_TEXTURE_2D, BGLayerTex[i]);

        GLint wrapmode = LayerConfig.uBGConfig[i].Clamp ? GL_CLAMP_TO_BORDER : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapmode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapmode);
    }

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, NativeOBJLayerTex);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput128Tex);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput256Tex);

    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, MosaicTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::RenderNativeLayerDebugView(int debugLayer) const
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, NativeLayerDebugFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, 256, 192);
    glDisable(GL_SCISSOR_TEST);

    if (ForcedBlank || !UnitEnabled)
    {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    glUseProgram(NativePrepassShader);
    glUniform1i(NativePrepassScaleULoc, 1);
    glUniform1i(NativePrepassDebugLayerULoc, debugLayer);

    glBindBufferBase(GL_UNIFORM_BUFFER, 20, LayerConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 23, CompositorConfigUBO);

    for (int i = 0; i < 4; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        if ((i == 0) && !GPU2D.Num && (DispCnt & (1<<3)))
            glBindTexture(GL_TEXTURE_2D, BlankColorTex);
        else
            glBindTexture(GL_TEXTURE_2D, BGLayerTex[i]);

        GLint wrapmode = LayerConfig.uBGConfig[i].Clamp ? GL_CLAMP_TO_BORDER : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapmode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapmode);
    }

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, NativeOBJLayerTex);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput128Tex);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput256Tex);

    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, MosaicTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    glUniform1i(NativePrepassDebugLayerULoc, -1);
}

void GLRenderer2D::RenderNativeExactFinal(int ystart, int yend, bool debugTint, GLuint direct3DTex, GLuint direct3DCoverageTex)
{
    RenderNativeExactFinalToTexture(NativeExactFinalTex, ystart, yend, debugTint, direct3DTex, direct3DCoverageTex);
}

void GLRenderer2D::RenderNativeExactFinalToTexture(GLuint targetTex, int ystart, int yend, bool debugTint, GLuint direct3DTex, GLuint direct3DCoverageTex, bool preserveCompositorConfig, GLuint capture128Tex, GLuint capture256Tex, bool forceOBJDisabled)
{
    glBindFramebuffer(GL_FRAMEBUFFER, NativeExactFinalFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    RenderCompositorPass(NativeExactFinalFB, NativeOBJLayerTex, 256, 192, ystart, yend,
                         1, 0, false, false, debugTint, direct3DTex,
                         direct3DCoverageTex, forceOBJDisabled, preserveCompositorConfig,
                         capture128Tex, capture256Tex);

    glBindFramebuffer(GL_FRAMEBUFFER, NativeExactFinalFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeExactFinalTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void GLRenderer2D::RenderNativeResolvedExactFinalToTexture(GLuint targetTex, GLuint direct3DTex)
{
    glBindFramebuffer(GL_FRAMEBUFFER, NativeExactFinalFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, 256, 192);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(NativeResolveShader);
    glUniform1i(NativeResolveScaleULoc, 1);
    glUniform1i(NativeResolveUseExactFinalFallbackULoc, false);
    glUniform1i(NativeResolveUseForegroundOverlayULoc, false);
    glUniform1i(NativeResolveDebugTintULoc, false);
    glUniform1i(NativeResolveNativeExactOutputULoc, true);
    glBindBufferBase(GL_UNIFORM_BUFFER, 23, CompositorConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, ScanlineConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    0,
                    192 * sizeof(sScanlineConfig::sScanline),
                    &ScanlineConfig.uScanline[0]);

    UpdateCompositorConfig();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, NativeTopColorTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, NativeSecondColorTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, NativeMetaTex);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, NativeMetaTex);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, NativeMetaTex);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, direct3DTex ? direct3DTex : BlankColorTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    glUniform1i(NativeResolveNativeExactOutputULoc, false);

    glBindFramebuffer(GL_FRAMEBUFFER, NativeExactFinalFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, NativeExactFinalTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void GLRenderer2D::RenderNativeUpscale(int ystart, int yend)
{
    const bool useForegroundOverlay = CanUseWholeSceneForegroundOverlayPath();

    if (CanUseWholeSceneArtCNNPath())
    {
        const int modelIndex = WholeSceneArtCNNModelIndex();
        if (WholeSceneScaleSourceBoundaryGuard)
            RenderNativeMetaCoverage(0, 192);
        bool rendered = RenderArtCNN2xGuarded(modelIndex, NativeTopColorTex, UpscaledTopColorTex, false) &&
                        RenderArtCNN2xGuarded(modelIndex, NativeSecondColorTex, UpscaledSecondColorTex, true);
        if (rendered && useForegroundOverlay)
            rendered = RenderArtCNN2x(modelIndex, NativeOutputTex, UpscaledExactFinalTex);
        else if (rendered && WholeSceneScaleExactFinalFallback)
            rendered = RenderArtCNN2x(modelIndex, NativeExactFinalTex, UpscaledExactFinalTex);
        if (rendered)
        {
            if (!WholeSceneScaleSourceBoundaryGuard)
                RenderNativeMetaCoverage(0, 192);
            return;
        }
    }

    if (CanUseWholeSceneNNEDI3Path())
    {
        if (WholeSceneScaleSourceBoundaryGuard)
            RenderNativeMetaCoverage(0, 192);
        bool rendered = RenderNNEDI32xGuarded(NativeTopColorTex, UpscaledTopColorTex, false) &&
                        RenderNNEDI32xGuarded(NativeSecondColorTex, UpscaledSecondColorTex, true);
        if (rendered && useForegroundOverlay)
            rendered = RenderNNEDI32x(NativeOutputTex, UpscaledExactFinalTex);
        else if (rendered && WholeSceneScaleExactFinalFallback)
            rendered = RenderNNEDI32x(NativeExactFinalTex, UpscaledExactFinalTex);
        if (rendered)
        {
            if (!WholeSceneScaleSourceBoundaryGuard)
                RenderNativeMetaCoverage(0, 192);
            return;
        }
    }

    if (CanUseWholeSceneCuNNyPath())
    {
        const int modelIndex = RendererSettings::GetGLCuNNyModelIndex(WholeSceneScaleAlgorithm);
        if (WholeSceneScaleSourceBoundaryGuard)
            RenderNativeMetaCoverage(0, 192);
        bool rendered = RenderCuNNy2xGuarded(modelIndex, NativeTopColorTex, UpscaledTopColorTex, false) &&
                        RenderCuNNy2xGuarded(modelIndex, NativeSecondColorTex, UpscaledSecondColorTex, true);
        if (rendered && useForegroundOverlay)
            rendered = RenderCuNNy2x(modelIndex, NativeOutputTex, UpscaledExactFinalTex);
        else if (rendered && WholeSceneScaleExactFinalFallback)
            rendered = RenderCuNNy2x(modelIndex, NativeExactFinalTex, UpscaledExactFinalTex);
        if (rendered)
        {
            if (!WholeSceneScaleSourceBoundaryGuard)
                RenderNativeMetaCoverage(0, 192);
            return;
        }
    }

    if (CanUseWholeSceneXBRZPath())
    {
        if (WholeSceneScaleSourceBoundaryGuard)
            RenderNativeMetaCoverage(0, 192);
        RenderXBRZGuarded(NativeTopColorTex, UpscaledTopColorTex, false);
        RenderXBRZGuarded(NativeSecondColorTex, UpscaledSecondColorTex, true);
        if (useForegroundOverlay)
            RenderXBRZ(NativeOutputTex, UpscaledExactFinalTex);
        else if (WholeSceneScaleExactFinalFallback)
            RenderXBRZ(NativeExactFinalTex, UpscaledExactFinalTex);
        if (!WholeSceneScaleSourceBoundaryGuard)
            RenderNativeMetaCoverage(0, 192);
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, UpscaledStateFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, ScreenW, ScreenH);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * ScaleFactor, ScreenW, (yend-ystart) * ScaleFactor);

    glUseProgram(NativeUpscaleShader);
    glUniform1i(NativeUpscaleScaleULoc, ScaleFactor);
    glUniform1i(NativeUpscaleLegacyFilterULoc, !WholeSceneScaleSourceBoundaryGuard);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, NativeTopColorTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, NativeSecondColorTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, NativeMetaTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    if (useForegroundOverlay)
        RenderSpline36(NativeOutputTex, UpscaledExactFinalTex, ScreenW, ScreenH);
    else if (WholeSceneScaleExactFinalFallback)
        RenderSpline36(NativeExactFinalTex, UpscaledExactFinalTex, ScreenW, ScreenH);

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::RenderNativeFinalUpscale(GLuint sourceTex)
{
    RenderNativeFinalUpscaleToTexture(sourceTex, OutputTex);
}

void GLRenderer2D::RenderNativeFinalUpscaleToTexture(GLuint sourceTex, GLuint targetTex)
{
    if (ScaleFactor <= 1)
    {
        RenderSpline36(sourceTex, targetTex, ScreenW, ScreenH);
        return;
    }

    if (RendererSettings::IsGLArtCNNAlgorithm(WholeSceneScaleAlgorithm))
    {
        if (RenderArtCNN2x(WholeSceneArtCNNModelIndex(), sourceTex, targetTex))
            return;
    }

    if (RendererSettings::IsGLNNEDI3Algorithm(WholeSceneScaleAlgorithm))
    {
        if (RenderNNEDI32x(sourceTex, targetTex))
            return;
    }

    if (RendererSettings::IsGLCuNNyAlgorithm(WholeSceneScaleAlgorithm))
    {
        if (RenderCuNNy2x(RendererSettings::GetGLCuNNyModelIndex(WholeSceneScaleAlgorithm), sourceTex, targetTex))
            return;
    }

    if (WholeSceneScaleAlgorithm == RendererSettings::GLScaleAlgorithm::XBRZ)
    {
        RenderXBRZ(sourceTex, targetTex);
        return;
    }

    RenderSpline36(sourceTex, targetTex, ScreenW, ScreenH);
}

u32 GLRenderer2D::CapturePresentationHash() const
{
    u32 hash = 2166136261u;
    auto mix = [&hash](u32 value)
    {
        hash = HashPresentationValue(hash, value);
    };

    mix(DispCnt);
    mix(LayerEnable);
    mix(OBJEnable);
    mix(BlendCnt);
    mix(EVA);
    mix(EVB);
    mix(EVY);
    mix(GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA);
    for (int i = 0; i < 4; i++)
        mix(BGCnt[i]);

    mix(SpriteUseMosaic);
    mix(SpriteConfig.uVRAMMask);
    mix(NumSprites);
    for (int i = 0; i < NumSprites; i++)
    {
        const auto& spr = SpriteConfig.uOAM[i];
        mix(static_cast<u32>(spr.Position[0]));
        mix(static_cast<u32>(spr.Position[1]));
        mix(static_cast<u32>(spr.Flip[0]));
        mix(static_cast<u32>(spr.Flip[1]));
        mix(static_cast<u32>(spr.Size[0]));
        mix(static_cast<u32>(spr.Size[1]));
        mix(static_cast<u32>(spr.BoundSize[0]));
        mix(static_cast<u32>(spr.BoundSize[1]));
        mix(spr.OBJMode);
        mix(spr.Type);
        mix(spr.PalOffset);
        mix(spr.TileOffset);
        mix(spr.TileStride);
        mix(spr.Rotscale);
        mix(spr.BGPrio);
        mix(spr.Mosaic);

        if (spr.Rotscale < 32)
        {
            for (int j = 0; j < 4; j++)
                mix(static_cast<u32>(SpriteConfig.uRotscale[spr.Rotscale][j]));
        }
    }

    return hash ? hash : 1;
}

bool GLRenderer2D::CanRenderCurrentOverlayForCaptureSource(u32 sourceLayerEnable,
                                                           u32 sourceBGMode,
                                                           u32 sourceVisibleBitmapMask,
                                                           bool sourceDirect3DVisible) const
{
    if (GPU2D.Num)
        return false;

    const u32 visibleBGLayers = LayerEnable & 0x0Fu;
    const u32 sourceVisibleBGLayers = sourceLayerEnable & 0x0Fu;
    const bool currentDirect3DVisible = (DispCnt & (1 << 3)) && (LayerEnable & (1 << 0));

    return sourceDirect3DVisible &&
           currentDirect3DVisible &&
           visibleBGLayers == (1u << 0) &&
           sourceVisibleBGLayers == (1u << 0) &&
           sourceBGMode == (DispCnt & 0x7u) &&
           sourceVisibleBitmapMask == 0 &&
           VisibleBitmapBGLayerMask() == 0;
}

bool GLRenderer2D::CanUseCaptureEpochBackgroundForLiveOverlay(int ystart, int yend, int& routeSlot) const
{
    routeSlot = -1;

    if (GPU2D.Num ||
        ystart != 0 ||
        yend != 192 ||
        WholeSceneScaleMode != RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale ||
        !CanUseWholeSceneScalePath() ||
        !CanUseWholeSceneOverlayOperatorPath())
    {
        return false;
    }

    const u32 visibleBGLayers = LayerEnable & 0x0Fu;
    if (visibleBGLayers != (1u << 0) ||
        !(DispCnt & (1 << 3)) ||
        !(LayerEnable & (1 << 4)) ||
        !OBJEnable ||
        VisibleBitmapBGLayerMask() != 0)
    {
        return false;
    }

    routeSlot = Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, ystart, yend) ? 1 : 0;
    const auto& epoch = Parent.ActiveCaptureBackgroundEpoch[routeSlot];
    if (!epoch.Valid ||
        epoch.ConsumerRouteSlot != static_cast<u32>(routeSlot) ||
        epoch.CaptureBank >= 4 ||
        !Parent.ActiveCaptureBackgroundEpochTex[routeSlot] ||
        !(epoch.ProductMask & GLRenderer::HighResCaptureProductBackground3DUnderlay) ||
        epoch.SourceKind != GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput)
    {
        return false;
    }

    if (!IsWholeSceneCaptureBackedHandoffGuardActive())
    {
        const auto& event = Parent.HighResDisplayCapture256Event[epoch.CaptureBank];
        if (!event.Valid ||
            event.Serial < epoch.Serial ||
            event.Serial - epoch.Serial > 2 ||
            event.RejectReason != GLRenderer::HighResCaptureRejectReason::None ||
            !(event.ProductMask & GLRenderer::HighResCaptureProductBackground3DUnderlay))
        {
            return false;
        }
    }

    const u32 epochVisibleBGLayers = epoch.SourceLayerEnable & 0x0Fu;
    return epoch.SourceDirect3DVisible &&
           epochVisibleBGLayers == (1u << 0) &&
           epoch.SourceBGMode == (DispCnt & 0x7u) &&
           epoch.SourceVisibleBitmapMask == 0;
}

bool GLRenderer2D::RenderCurrentOverlayOverHighResBackgroundToTexture(GLuint targetTex,
                                                                      GLuint highResBackgroundTex,
                                                                      int ystart,
                                                                      int yend,
                                                                      bool applyMasterBrightness,
                                                                      GLuint* rawCompositeTex,
                                                                      bool* outputMasterBrightnessApplied)
{
    if (outputMasterBrightnessApplied)
        *outputMasterBrightnessApplied = false;

    if (!targetTex || !highResBackgroundTex || ystart != 0 || yend != 192)
        return false;

    RenderNativePrepass(ystart, yend);

    GLuint nativeDirect3DTex = highResBackgroundTex;
    if (ScaleFactor > 1)
        nativeDirect3DTex = ResolveDirect3DToNative(highResBackgroundTex);

    UpdateCompositorConfig();
    const auto savedCompositorConfig = CompositorConfig;

    for (int layer = 0; layer < 4; layer++)
        CompositorConfig.uBGPrio[layer] = -1;

    CompositorConfig.uEnable3D = !GPU2D.Num && (DispCnt & (1 << 3));
    if (CompositorConfig.uEnable3D)
        CompositorConfig.uBGPrio[0] = BGCnt[0] & 0x3;
    CompositorConfig.uEnableOBJ = !!(LayerEnable & (1 << 4));

    glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CompositorConfig), &CompositorConfig);

    WholeSceneOverlayEndpointsValid = false;
    WholeSceneOverlayEndpointSourceTex = 0;
    EnsureWholeSceneOverlayEndpoints(nativeDirect3DTex);

    RenderNativeExactFinalToTexture(NativeExactFinalTex, ystart, yend, false,
                                    NativeOverlayBlack3DTex, nativeDirect3DTex, true);
    RenderNativeExactFinalToTexture(NativeOutputTex, ystart, yend, false,
                                    NativeOverlayWhite3DTex, nativeDirect3DTex, true);

    RenderNativeFinalUpscaleToTexture(NativeExactFinalTex, UpscaledExactFinalTex);
    RenderNativeFinalUpscaleToTexture(NativeOutputTex, UpscaledCoverageTex);

    const u16 masterBrightness = GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    const int brightMode = (masterBrightness >> 14) & 0x3;
    const int brightFactor = std::min<int>(masterBrightness & 0x1F, 16);
    const bool bakeMasterBrightness =
        applyMasterBrightness && (brightMode == 1 || brightMode == 2) && brightFactor > 0;
    const GLuint compositeTargetTex = bakeMasterBrightness ? HybridFinalSourceTex : targetTex;
    if (rawCompositeTex)
        *rawCompositeTex = compositeTargetTex;

    RenderOverlayComposite(UpscaledExactFinalTex,
                           UpscaledCoverageTex,
                           highResBackgroundTex,
                           false,
                           0,
                           0,
                           0,
                           0,
                           false,
                           0,
                           compositeTargetTex,
                           false,
                           0,
                           ystart,
                           yend,
                           true);

    bool masterBrightnessAppliedToOutput = false;
    const bool masterBrightnessApplied =
        !bakeMasterBrightness ||
        (masterBrightnessAppliedToOutput =
             ApplyMasterBrightnessToTexture(targetTex,
                                            compositeTargetTex,
                                            ScreenW,
                                            ScreenH,
                                            ystart,
                                            yend,
                                            masterBrightness));
    if (outputMasterBrightnessApplied)
        *outputMasterBrightnessApplied = masterBrightnessAppliedToOutput;

    CompositorConfig = savedCompositorConfig;
    glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CompositorConfig), &CompositorConfig);

    WholeSceneOverlayEndpointsValid = false;
    WholeSceneOverlayEndpointSourceTex = 0;
    return masterBrightnessApplied;
}

bool GLRenderer2D::ApplyMasterBrightnessToTexture(GLuint targetTex,
                                                  GLuint sourceTex,
                                                  int width,
                                                  int height,
                                                  int ystart,
                                                  int yend,
                                                  u16 masterBrightness)
{
    const int brightMode = (masterBrightness >> 14) & 0x3;
    const int brightFactor = std::min<int>(masterBrightness & 0x1F, 16);
    if (!targetTex || !sourceTex || targetTex == sourceTex ||
        brightFactor <= 0 || (brightMode != 1 && brightMode != 2))
    {
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (ystart * height) / 192, width, ((yend - ystart) * height) / 192);

    glUseProgram(MasterBrightnessShader);
    glUniform1i(MasterBrightnessModeULoc, brightMode);
    glUniform1i(MasterBrightnessFactorULoc, brightFactor);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    return true;
}

bool GLRenderer2D::RenderCurrentLayersOverHighResCaptureBGToTexture(GLuint targetTex,
                                                                    int ystart,
                                                                    int yend)
{
    if (!targetTex || ystart != 0 || yend != 192 ||
        !CanUseWholeSceneMixedSourceACaptureBGPath())
    {
        return false;
    }

    const u32 captureBGMask = VisibleSourceAOnlyFullDisplayCaptureBGLayerMask();
    GLuint highResUnderlayTex = 0;
    for (int layer = 0; layer < 4; layer++)
    {
        if ((captureBGMask & (1u << layer)) == 0)
            continue;

        const auto& cfg = LayerConfig.uBGConfig[layer];
        u64 serial = 0;
        u32 sourceKind = 0;
        u32 productMask = 0;
        u32 rejectReason = 0;
        const GLuint layerTex = Parent.GetHighResDisplayCaptureFullTexForBG(cfg.Type,
                                                                            cfg.TileOffset,
                                                                            serial,
                                                                            sourceKind,
                                                                            productMask,
                                                                            rejectReason);
        if (!layerTex)
            return false;
        if (highResUnderlayTex && highResUnderlayTex != layerTex)
            return false;

        highResUnderlayTex = layerTex;
    }

    if (!highResUnderlayTex)
        return false;

    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::SourceACaptureReplacement,
                                ystart, yend,
                                ScaleFactor > 1,
                                false,
                                false,
                                highResUnderlayTex);
    WholeSceneTrace.SourceACaptureMode = SourceACaptureReplacementMode::CurrentOverlay;
    WholeSceneTrace.SourceAProductChoice =
        SourceAProductChoiceReason::UsedBackgroundUnderlayCurrentOverlay;
    WholeSceneTrace.EffectiveSourceABackgroundSource =
        SourceABackgroundSource::FullCaptureProduct;
    WholeSceneTrace.CaptureProductKind = WholeSceneCaptureProductKind::FullCaptureProduct;
    WholeSceneTrace.CaptureRenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
    GLCaptureProductResolution product = {};
    product.Tex = highResUnderlayTex;
    product.Accepted = true;
    product.ProductKind = WholeSceneTrace.CaptureProductKind;
    product.BackgroundSource = SourceABackgroundSource::FullCaptureProduct;
    product.RenderAction = WholeSceneTrace.CaptureRenderAction;
    product.PresentationClass =
        CaptureProductPresentationClassForProduct(product.ProductKind,
                                                 product.RenderAction);
    RecordChosenCaptureProductTrace(product, {});

    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativePrepass(ystart, yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));

    WholeSceneOverlayEndpointsValid = false;
    WholeSceneOverlayEndpointSourceTex = 0;

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeExactFinalTex,
                                    ystart,
                                    yend,
                                    false,
                                    0,
                                    0,
                                    false,
                                    NativeOverlayBlackCapture128Tex,
                                    NativeOverlayBlackCapture256Tex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayBlackExactFinal, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeOutputTex,
                                    ystart,
                                    yend,
                                    false,
                                    0,
                                    0,
                                    false,
                                    NativeOverlayWhiteCapture128Tex,
                                    NativeOverlayWhiteCapture256Tex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayWhiteExactFinal, ElapsedUS(phaseStart));
    WholeSceneTrace.NativeExactFinalValid = true;

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeFinalUpscaleToTexture(NativeExactFinalTex, UpscaledExactFinalTex);
    RenderNativeFinalUpscaleToTexture(NativeOutputTex, UpscaledCoverageTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderOverlayComposite(UpscaledExactFinalTex,
                           UpscaledCoverageTex,
                           highResUnderlayTex,
                           false,
                           0,
                           0,
                           0,
                           0,
                           false,
                           0,
                           targetTex,
                           WholeSceneScaleDebugTint,
                           0,
                           ystart,
                           yend,
                           true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));

    return true;
}

bool GLRenderer2D::RenderCurrentBGOverlayOverHighResCaptureOBJToTexture(GLuint targetTex,
                                                                        int ystart,
                                                                        int yend)
{
    if (!targetTex || ystart != 0 || yend != 192 ||
        !CanUseWholeSceneMixedCaptureBackedOBJOverlayPath())
    {
        return false;
    }

    const VisibleOBJCaptureDebug debug = BuildVisibleOBJCaptureDebug();
    if (debug.Bank < 0 || debug.Bank >= 4)
        return false;

    const GLuint highResUnderlayTex = Parent.HighResDisplayCaptureFullTex[debug.Bank];
    if (!highResUnderlayTex)
        return false;

    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::SourceACaptureReplacement,
                                ystart, yend,
                                ScaleFactor > 1,
                                false,
                                false,
                                highResUnderlayTex);
    WholeSceneTrace.SourceACaptureMode = SourceACaptureReplacementMode::CurrentOverlay;
    WholeSceneTrace.SourceAProductChoice =
        SourceAProductChoiceReason::UsedBackgroundUnderlayCurrentOverlay;
    WholeSceneTrace.EffectiveSourceABackgroundSource =
        SourceABackgroundSource::FullCaptureProduct;
    WholeSceneTrace.CaptureProductKind = WholeSceneCaptureProductKind::FullCaptureProduct;
    WholeSceneTrace.CaptureRenderAction = WholeSceneCaptureRenderAction::CompositeCurrentOverlay;
    GLCaptureProductResolution product = {};
    product.Tex = highResUnderlayTex;
    product.Accepted = true;
    product.ProductKind = WholeSceneTrace.CaptureProductKind;
    product.BackgroundSource = SourceABackgroundSource::FullCaptureProduct;
    product.RenderAction = WholeSceneTrace.CaptureRenderAction;
    product.PresentationClass =
        CaptureProductPresentationClassForProduct(product.ProductKind,
                                                 product.RenderAction);
    RecordChosenCaptureProductTrace(product,
                                    {debug.Bank,
                                     0,
                                     0,
                                     0,
                                     debug.EventSerial,
                                     0,
                                     0});

    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativePrepass(ystart, yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));

    WholeSceneOverlayEndpointsValid = false;
    WholeSceneOverlayEndpointSourceTex = 0;

    const sScanlineConfig savedScanlineConfig = ScanlineConfig;
    auto uploadScanlineRange = [&]()
    {
        glBindBuffer(GL_UNIFORM_BUFFER, ScanlineConfigUBO);
        glBufferSubData(GL_UNIFORM_BUFFER,
                        ystart * sizeof(sScanlineConfig::sScanline),
                        (yend - ystart) * sizeof(sScanlineConfig::sScanline),
                        &ScanlineConfig.uScanline[ystart]);
    };
    auto setBackdrop = [&](u32 color)
    {
        for (int y = ystart; y < yend; y++)
            ScanlineConfig.uScanline[y].BackColor = color;
    };

    setBackdrop(0x0000);
    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeExactFinalTex,
                                    ystart,
                                    yend,
                                    false,
                                    0,
                                    0,
                                    false,
                                    0,
                                    0,
                                    true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayBlackExactFinal, ElapsedUS(phaseStart));

    setBackdrop(0x7FFF);
    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeOutputTex,
                                    ystart,
                                    yend,
                                    false,
                                    0,
                                    0,
                                    false,
                                    0,
                                    0,
                                    true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayWhiteExactFinal, ElapsedUS(phaseStart));
    WholeSceneTrace.NativeExactFinalValid = true;

    ScanlineConfig = savedScanlineConfig;
    uploadScanlineRange();

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeFinalUpscaleToTexture(NativeExactFinalTex, UpscaledExactFinalTex);
    RenderNativeFinalUpscaleToTexture(NativeOutputTex, UpscaledCoverageTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderOverlayComposite(UpscaledExactFinalTex,
                           UpscaledCoverageTex,
                           highResUnderlayTex,
                           false,
                           0,
                           0,
                           0,
                           0,
                           false,
                           0,
                           targetTex,
                           WholeSceneScaleDebugTint,
                           0,
                           ystart,
                           yend,
                           true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));

    return true;
}

void GLRenderer2D::RenderNativeMetaCoverage(int ystart, int yend)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, UpscaledStateFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, ScreenW, ScreenH);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * ScaleFactor, ScreenW, (yend-ystart) * ScaleFactor);

    glUseProgram(NativeUpscaleShader);
    glUniform1i(NativeUpscaleScaleULoc, ScaleFactor);
    glUniform1i(NativeUpscaleLegacyFilterULoc, !WholeSceneScaleSourceBoundaryGuard);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, NativeTopColorTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, NativeSecondColorTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, NativeMetaTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::RenderFullscreenPass(GLuint shader, GLuint outputFB, int width, int height, GLuint source0, GLuint source1)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, outputFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    GLint outputSizeLoc = glGetUniformLocation(shader, "uOutputSize");
    if (outputSizeLoc >= 0)
        glUniform2f(outputSizeLoc, (float)width, (float)height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, source1);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

void GLRenderer2D::RenderFullscreenPassToTexture(GLuint shader, GLuint targetTex, int width, int height, GLuint source0, GLuint source1)
{
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    RenderFullscreenPass(shader, ArtCNNOutputFB, width, height, source0, source1);
}

void GLRenderer2D::CopyArtCNNProgramsFrom(const GLRenderer2D& other)
{
    for (int model = 0; model < RendererSettings::GLArtCNNModelCount; model++)
    {
        for (int pass = 0; pass < 7; pass++)
            ArtCNNConvShaders[model][pass] = other.ArtCNNConvShaders[model][pass];
        ArtCNNDepthToSpaceShaders[model] = other.ArtCNNDepthToSpaceShaders[model];
    }
    ArtCNNComputeProgramsReady = other.ArtCNNComputeProgramsReady;
    ArtCNNComputeProgramsFailed = other.ArtCNNComputeProgramsFailed;
}

bool GLRenderer2D::EnsureArtCNNComputePrograms()
{
    if (ArtCNNShaderOwner != this)
    {
        if (ArtCNNShaderOwner == nullptr || !ArtCNNShaderOwner->EnsureArtCNNComputePrograms())
            return false;
        CopyArtCNNProgramsFrom(*ArtCNNShaderOwner);
        return ArtCNNComputeProgramsReady;
    }

    if (ArtCNNComputeProgramsReady)
        return true;
    if (ArtCNNComputeProgramsFailed)
        return false;

    for (int model = 0; model < RendererSettings::GLArtCNNModelCount; model++)
    {
        for (int pass = 0; pass < 7; pass++)
        {
            std::string shaderName = "2DArtCNN_" + std::string(kArtCNNModelLabels[model]) +
                                     "_Conv" + std::to_string(pass) + "ComputeShader";
            if (!OpenGL::CompileComputeProgram(ArtCNNConvShaders[model][pass],
                                               kArtCNNConvShaderSources[model][pass],
                                               shaderName.c_str()))
                goto fail;
            SetArtCNNComputeProgramDefaults(ArtCNNConvShaders[model][pass]);
        }

        std::string depthShaderName = "2DArtCNN_" + std::string(kArtCNNModelLabels[model]) +
                                      "_DepthToSpaceComputeShader";
        if (!OpenGL::CompileComputeProgram(ArtCNNDepthToSpaceShaders[model],
                                           kArtCNNDepthToSpaceSources[model],
                                           depthShaderName.c_str()))
            goto fail;
        SetArtCNNComputeProgramDefaults(ArtCNNDepthToSpaceShaders[model]);
    }

    ArtCNNComputeProgramsReady = true;
    return true;

fail:
    ArtCNNComputeProgramsFailed = true;
    return false;
}

void GLRenderer2D::CopyNNEDI3ComputeProgramsFrom(const GLRenderer2D& other)
{
    NNEDI3VerticalComputeShader = other.NNEDI3VerticalComputeShader;
    NNEDI3HorizontalComputeShader = other.NNEDI3HorizontalComputeShader;
    NNEDI3ComputeProgramsReady = other.NNEDI3ComputeProgramsReady;
    NNEDI3ComputeProgramsFailed = other.NNEDI3ComputeProgramsFailed;
}

bool GLRenderer2D::EnsureNNEDI3ComputePrograms()
{
    if (NNEDI3ComputeShaderOwner != this)
    {
        if (NNEDI3ComputeShaderOwner == nullptr || !NNEDI3ComputeShaderOwner->EnsureNNEDI3ComputePrograms())
            return false;
        CopyNNEDI3ComputeProgramsFrom(*NNEDI3ComputeShaderOwner);
        return NNEDI3ComputeProgramsReady;
    }

    if (NNEDI3ComputeProgramsReady)
        return true;
    if (NNEDI3ComputeProgramsFailed)
        return false;

    if (!OpenGL::CompileComputeProgram(NNEDI3VerticalComputeShader,
                                       ::k2DNNEDI3_VerticalCS,
                                       "2DNNEDI3VerticalComputeShader"))
        goto fail;
    SetNNEDI3ComputeProgramDefaults(NNEDI3VerticalComputeShader);

    if (!OpenGL::CompileComputeProgram(NNEDI3HorizontalComputeShader,
                                       ::k2DNNEDI3_HorizontalCS,
                                       "2DNNEDI3HorizontalComputeShader"))
        goto fail;
    SetNNEDI3ComputeProgramDefaults(NNEDI3HorizontalComputeShader);

    NNEDI3ComputeProgramsReady = true;
    Log(LogLevel::Info, "Compiled NNEDI3 compute scaler programs\n");
    return true;

fail:
    glDeleteProgram(NNEDI3VerticalComputeShader);
    glDeleteProgram(NNEDI3HorizontalComputeShader);
    NNEDI3VerticalComputeShader = 0;
    NNEDI3HorizontalComputeShader = 0;
    NNEDI3ComputeProgramsFailed = true;
    Log(LogLevel::Error, "Failed to compile NNEDI3 compute scaler programs\n");
    return false;
}

void GLRenderer2D::CopyCuNNyProgramsFrom(const GLRenderer2D& other)
{
    for (int model = 0; model < RendererSettings::GLCuNNyModelCount; model++)
    {
        CuNNyInShaders[model] = other.CuNNyInShaders[model];
        for (int pass = 0; pass < RendererSettings::GLCuNNyMaxConvPasses; pass++)
            CuNNyConvShaders[model][pass] = other.CuNNyConvShaders[model][pass];
        CuNNyOutShaders[model] = other.CuNNyOutShaders[model];
    }
    CuNNyProgramsReady = other.CuNNyProgramsReady;
    CuNNyProgramsFailed = other.CuNNyProgramsFailed;
}

bool GLRenderer2D::EnsureCuNNyPrograms()
{
    if (CuNNyShaderOwner != this)
    {
        if (CuNNyShaderOwner == nullptr || !CuNNyShaderOwner->EnsureCuNNyPrograms())
            return false;
        CopyCuNNyProgramsFrom(*CuNNyShaderOwner);
        return CuNNyProgramsReady;
    }

    if (CuNNyProgramsReady)
        return true;
    if (CuNNyProgramsFailed)
        return false;

    for (int model = 0; model < RendererSettings::GLCuNNyModelCount; model++)
    {
        const CuNNyModelInfo& modelInfo = kCuNNyModels[model];
        std::string shaderName = "2DCuNNy_" + std::string(kCuNNyModelLabels[model]) + "_InShader";
        if (!OpenGL::CompileComputeProgram(CuNNyInShaders[model], modelInfo.InShader, shaderName.c_str()))
            goto fail;
        SetCuNNyProgramDefaults(CuNNyInShaders[model]);

        for (int pass = 0; pass < modelInfo.ConvPasses; pass++)
        {
            shaderName = "2DCuNNy_" + std::string(kCuNNyModelLabels[model]) +
                         "_Conv" + std::to_string(pass + 1) + "Shader";
            if (!OpenGL::CompileComputeProgram(CuNNyConvShaders[model][pass],
                                               modelInfo.ConvShaders[pass],
                                               shaderName.c_str()))
                goto fail;
            SetCuNNyProgramDefaults(CuNNyConvShaders[model][pass]);
        }

        shaderName = "2DCuNNy_" + std::string(kCuNNyModelLabels[model]) + "_OutShuffleShader";
        if (!OpenGL::CompileComputeProgram(CuNNyOutShaders[model], modelInfo.OutShader, shaderName.c_str()))
            goto fail;
        SetCuNNyProgramDefaults(CuNNyOutShaders[model]);
    }

    CuNNyProgramsReady = true;
    return true;

fail:
    CuNNyProgramsFailed = true;
    return false;
}

bool GLRenderer2D::EnsureCuNNyWorkTexture(int index, int width, int height)
{
    if (index < 0 || index >= 2 || width <= 0 || height <= 0)
        return false;

    if (CuNNyWorkTex[index] == 0)
        glGenTextures(1, &CuNNyWorkTex[index]);

    glBindTexture(GL_TEXTURE_2D, CuNNyWorkTex[index]);
    if (CuNNyWorkTexWidth[index] != (u32)width || CuNNyWorkTexHeight[index] != (u32)height)
    {
        glDefaultTexParams(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        CuNNyWorkTexWidth[index] = width;
        CuNNyWorkTexHeight[index] = height;
    }

    return true;
}

void GLRenderer2D::RenderCuNNyComputePass(GLuint shader, GLuint sourceTex, GLuint baseTex, GLuint targetTex,
                                          int sourceWidth, int sourceHeight,
                                          int nativeWidth, int nativeHeight)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    SetUniform2fIfPresent(shader, "LUMA_size", (GLfloat)nativeWidth, (GLfloat)nativeHeight);
    SetUniform2fIfPresent(shader, "MAIN_size", (GLfloat)nativeWidth, (GLfloat)nativeHeight);
    SetUniform2fIfPresent(shader, "LUMA_pt", 1.0f / (GLfloat)nativeWidth, 1.0f / (GLfloat)nativeHeight);
    SetUniform2fIfPresent(shader, "MAIN_pt", 1.0f / (GLfloat)nativeWidth, 1.0f / (GLfloat)nativeHeight);
    for (const char* name : kCuNNyWorkPointUniforms)
        SetUniform2fIfPresent(shader, name, 1.0f / (GLfloat)sourceWidth, 1.0f / (GLfloat)sourceHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseTex);
    GLint baseMinFilter = GL_NEAREST;
    GLint baseMagFilter = GL_NEAREST;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &baseMinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &baseMagFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindImageTexture(0, targetTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((GLuint)((nativeWidth + 7) / 8), (GLuint)((nativeHeight + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_FRAMEBUFFER_BARRIER_BIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, baseMinFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, baseMagFilter);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
}

bool GLRenderer2D::RenderCuNNy2x(int modelIndex, GLuint sourceTex, GLuint targetTex)
{
    if (modelIndex < 0 || modelIndex >= RendererSettings::GLCuNNyModelCount)
        return false;
    if (!EnsureCuNNyPrograms())
        return false;

    RenderFullscreenPass(RGBAToYUVAShader, ArtCNNYUVFB, 256, 192, sourceTex, 0);
    RenderSpline36(ArtCNNYUVTex, ArtCNNYUVA2xTex, 512, 384);

    const CuNNyModelInfo& model = kCuNNyModels[modelIndex];
    int currentWidth = 256 * model.WorkScaleX;
    int currentHeight = 192 * model.WorkScaleY;
    if (!EnsureCuNNyWorkTexture(0, currentWidth, currentHeight))
        return false;

    GLuint cunnyBaseTex = model.RGB ? sourceTex : ArtCNNYUVTex;
    RenderCuNNyComputePass(CuNNyInShaders[modelIndex], cunnyBaseTex, cunnyBaseTex,
                           CuNNyWorkTex[0], 256, 192, 256, 192);

    GLuint currentTex = CuNNyWorkTex[0];
    int currentIndex = 0;
    for (int pass = 0; pass < model.ConvPasses; pass++)
    {
        const bool lastConv = pass == model.ConvPasses - 1;
        const int targetWidth = 256 * (lastConv ? model.FinalWorkScaleX : model.WorkScaleX);
        const int targetHeight = 192 * (lastConv ? model.FinalWorkScaleY : model.WorkScaleY);
        const int targetIndex = currentIndex == 0 ? 1 : 0;
        if (!EnsureCuNNyWorkTexture(targetIndex, targetWidth, targetHeight))
            return false;

        RenderCuNNyComputePass(CuNNyConvShaders[modelIndex][pass], currentTex, cunnyBaseTex,
                               CuNNyWorkTex[targetIndex], currentWidth, currentHeight,
                               256, 192);
        currentTex = CuNNyWorkTex[targetIndex];
        currentIndex = targetIndex;
        currentWidth = targetWidth;
        currentHeight = targetHeight;
    }

    RenderCuNNyComputePass(CuNNyOutShaders[modelIndex], currentTex, cunnyBaseTex,
                           ArtCNNLuma2xTex, currentWidth, currentHeight, 256, 192);

    GLuint rgba2xTarget = (ScaleFactor == 2) ? targetTex : ArtCNNRGBA2xTex;
    if (model.RGB)
        RenderFullscreenPassToTexture(AlphaReplaceShader, rgba2xTarget, 512, 384,
                                  ArtCNNLuma2xTex, ArtCNNYUVA2xTex);
    else
        RenderFullscreenPassToTexture(ArtCNNYUVAToRGBA2xShader, rgba2xTarget, 512, 384,
                                  ArtCNNYUVA2xTex, ArtCNNLuma2xTex);

    if (ScaleFactor > 2)
        RenderSpline36(ArtCNNRGBA2xTex, targetTex, ScreenW, ScreenH);
    return true;
}

void GLRenderer2D::RenderSpline36(GLuint sourceTex, GLuint targetTex, int width, int height, float sourceShiftX, float sourceShiftY)
{
    glUseProgram(Spline36Shader);
    GLint sourceShiftLoc = glGetUniformLocation(Spline36Shader, "uSourceShift");
    if (sourceShiftLoc >= 0)
        glUniform2f(sourceShiftLoc, sourceShiftX, sourceShiftY);

    RenderFullscreenPassToTexture(Spline36Shader, targetTex, width, height, sourceTex, 0);
}

void GLRenderer2D::RenderArtCNNComputePass(GLuint shader, GLuint targetTex, int pass)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    SetArtCNNComputeSizeUniforms(shader, 256, 192);

    for (int unit = 0; unit < (int)kArtCNNSamplerUniformCount; unit++)
        BindTextureUnit(unit, 0);

    switch (pass)
    {
    case 0:
        BindTextureUnit(0, ArtCNNYUVTex);
        break;
    case 1:
        BindTextureUnit(1, ArtCNNConv0Tex);
        break;
    case 2:
        BindTextureUnit(2, ArtCNNConvWorkTex[0]);
        break;
    case 3:
        BindTextureUnit(3, ArtCNNConvWorkTex[1]);
        break;
    case 4:
        BindTextureUnit(4, ArtCNNConvWorkTex[0]);
        break;
    case 5:
        BindTextureUnit(5, ArtCNNConvWorkTex[1]);
        break;
    case 6:
        BindTextureUnit(1, ArtCNNConv0Tex);
        BindTextureUnit(6, ArtCNNConvWorkTex[0]);
        break;
    default:
        BindTextureUnit(7, ArtCNNPackedTex);
        break;
    }

    glBindImageTexture(0, targetTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    const int dispatchWidth = (pass == 7) ? 512 : 256;
    const int dispatchHeight = (pass == 7) ? 384 : 192;
    glDispatchCompute((GLuint)((dispatchWidth + 7) / 8), (GLuint)((dispatchHeight + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_FRAMEBUFFER_BARRIER_BIT);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glActiveTexture(GL_TEXTURE0);
}

bool GLRenderer2D::RenderArtCNN2x(int modelIndex, GLuint sourceTex, GLuint targetTex)
{
    if (modelIndex < 0 || modelIndex >= RendererSettings::GLArtCNNModelCount)
        return false;
    if (!EnsureArtCNNComputePrograms())
        return false;

    RenderFullscreenPass(RGBAToYUVAShader, ArtCNNYUVFB, 256, 192, sourceTex, 0);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][0], ArtCNNConv0Tex, 0);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][1], ArtCNNConvWorkTex[0], 1);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][2], ArtCNNConvWorkTex[1], 2);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][3], ArtCNNConvWorkTex[0], 3);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][4], ArtCNNConvWorkTex[1], 4);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][5], ArtCNNConvWorkTex[0], 5);
    RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][6], ArtCNNPackedTex, 6);
    RenderArtCNNComputePass(ArtCNNDepthToSpaceShaders[modelIndex], ArtCNNLuma2xTex, 7);

    RenderSpline36(ArtCNNYUVTex, ArtCNNYUVA2xTex, 512, 384);

    GLuint rgba2xTarget = (ScaleFactor == 2) ? targetTex : ArtCNNRGBA2xTex;
    RenderFullscreenPassToTexture(ArtCNNYUVAToRGBA2xShader, rgba2xTarget, 512, 384, ArtCNNYUVA2xTex, ArtCNNLuma2xTex);

    if (ScaleFactor > 2)
        RenderSpline36(ArtCNNRGBA2xTex, targetTex, ScreenW, ScreenH);
    return true;
}

bool GLRenderer2D::RenderArtCNN2xGuarded(int modelIndex, GLuint sourceTex, GLuint targetTex, bool secondLayer)
{
    if (!WholeSceneScaleSourceBoundaryGuard)
        return RenderArtCNN2x(modelIndex, sourceTex, targetTex);

    if (!RenderArtCNN2x(modelIndex, sourceTex, UpscaledGuardColorTex))
        return false;

    RenderNativeBoundaryGuard(UpscaledGuardColorTex, sourceTex, targetTex, secondLayer);
    return true;
}

bool GLRenderer2D::RenderCuNNy2xGuarded(int modelIndex, GLuint sourceTex, GLuint targetTex, bool secondLayer)
{
    if (!WholeSceneScaleSourceBoundaryGuard)
        return RenderCuNNy2x(modelIndex, sourceTex, targetTex);

    if (!RenderCuNNy2x(modelIndex, sourceTex, UpscaledGuardColorTex))
        return false;

    RenderNativeBoundaryGuard(UpscaledGuardColorTex, sourceTex, targetTex, secondLayer);
    return true;
}

void GLRenderer2D::RenderNNEDI3ComputePass(GLuint shader, GLuint sourceTex, GLuint targetTex,
                                           int sourceWidth, int sourceHeight)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    SetUniform2iIfPresent(shader, "uSrcSize", sourceWidth, sourceHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);

    glBindImageTexture(0, targetTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((GLuint)((sourceWidth + 7) / 8), (GLuint)((sourceHeight + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_FRAMEBUFFER_BARRIER_BIT);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glActiveTexture(GL_TEXTURE0);
}

bool GLRenderer2D::RenderNNEDI32x(GLuint sourceTex, GLuint targetTex)
{
    if (!EnsureNNEDI3ComputePrograms())
        return false;

    RenderNNEDI3ComputePass(NNEDI3VerticalComputeShader, sourceTex, NNEDI3VerticalTex, 256, 192);
    RenderNNEDI3ComputePass(NNEDI3HorizontalComputeShader, NNEDI3VerticalTex, ArtCNNLuma2xTex, 256, 384);

    if (ScaleFactor >= 4)
    {
        RenderNNEDI3ComputePass(NNEDI3VerticalComputeShader, ArtCNNLuma2xTex, NNEDI3Vertical4xTex, 512, 384);
        RenderNNEDI3ComputePass(NNEDI3HorizontalComputeShader, NNEDI3Vertical4xTex, NNEDI3Luma4xTex, 512, 768);

        const int targetWidth = (ScaleFactor == 4) ? 1024 : ScreenW;
        const int targetHeight = (ScaleFactor == 4) ? 768 : ScreenH;
        RenderSpline36(NNEDI3Luma4xTex, targetTex, targetWidth, targetHeight, -1.5f, -1.5f);
        return true;
    }

    const int targetWidth = (ScaleFactor == 2) ? 512 : ScreenW;
    const int targetHeight = (ScaleFactor == 2) ? 384 : ScreenH;
    RenderSpline36(ArtCNNLuma2xTex, targetTex, targetWidth, targetHeight, -0.5f, -0.5f);
    return true;
}

void GLRenderer2D::RenderNativeBoundaryGuard(GLuint scaledTex, GLuint nativeTex, GLuint targetTex, bool secondLayer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, ScreenW, ScreenH);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(NativeBoundaryGuardShader);

    GLint scaleLoc = glGetUniformLocation(NativeBoundaryGuardShader, "uScaleFactor");
    if (scaleLoc >= 0)
        glUniform1i(scaleLoc, ScaleFactor);

    GLint secondLayerLoc = glGetUniformLocation(NativeBoundaryGuardShader, "uSecondLayer");
    if (secondLayerLoc >= 0)
        glUniform1i(secondLayerLoc, secondLayer);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scaledTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, nativeTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, UpscaledCoverageTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

bool GLRenderer2D::RenderNNEDI32xGuarded(GLuint sourceTex, GLuint targetTex, bool secondLayer)
{
    if (!WholeSceneScaleSourceBoundaryGuard)
        return RenderNNEDI32x(sourceTex, targetTex);

    if (!RenderNNEDI32x(sourceTex, UpscaledGuardColorTex))
        return false;
    RenderNativeBoundaryGuard(UpscaledGuardColorTex, sourceTex, targetTex, secondLayer);
    return true;
}

void GLRenderer2D::RenderXBRZ(GLuint sourceTex, GLuint targetTex)
{
    RenderFullscreenPass(XBRZPreprocessShader, XBRZInfoFB, 256, 192, sourceTex, 0);
    RenderFullscreenPassToTexture(XBRZFreescaleShader, targetTex, ScreenW, ScreenH, sourceTex, XBRZInfoTex);
}

void GLRenderer2D::RenderXBRZGuarded(GLuint sourceTex, GLuint targetTex, bool secondLayer)
{
    if (!WholeSceneScaleSourceBoundaryGuard)
    {
        RenderXBRZ(sourceTex, targetTex);
        return;
    }

    RenderXBRZ(sourceTex, UpscaledGuardColorTex);
    RenderNativeBoundaryGuard(UpscaledGuardColorTex, sourceTex, targetTex, secondLayer);
}

void GLRenderer2D::RenderNativeResolve(int ystart, int yend)
{
    RenderNativeResolveToTexture(OutputTex,
                                 ystart,
                                 yend,
                                 WholeSceneScaleDebugTint,
                                 WholeSceneScaleExactFinalFallback,
                                 CanUseWholeSceneForegroundOverlayPath());
}

void GLRenderer2D::RenderNativeResolveToTexture(GLuint targetTex,
                                                int ystart,
                                                int yend,
                                                bool debugTint,
                                                bool useExactFinalFallback,
                                                bool useForegroundOverlay)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (targetTex == OutputTex)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OutputFB);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, ScreenW, ScreenH);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * ScaleFactor, ScreenW, (yend-ystart) * ScaleFactor);

    glUseProgram(NativeResolveShader);
    glUniform1i(NativeResolveScaleULoc, ScaleFactor);
    glUniform1i(NativeResolveUseExactFinalFallbackULoc, useExactFinalFallback);
    glUniform1i(NativeResolveUseForegroundOverlayULoc, useForegroundOverlay);
    glUniform1i(NativeResolveDebugTintULoc, debugTint);
    glUniform1i(NativeResolveNativeExactOutputULoc, false);
    glBindBufferBase(GL_UNIFORM_BUFFER, 23, CompositorConfigUBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, ScanlineConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    ystart * sizeof(sScanlineConfig::sScanline),
                    (yend - ystart) * sizeof(sScanlineConfig::sScanline),
                    &ScanlineConfig.uScanline[ystart]);

    UpdateCompositorConfig();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, UpscaledTopColorTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, UpscaledSecondColorTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, UpscaledMetaTex);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, UpscaledCoverageTex);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, UpscaledExactFinalTex);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, Parent.OutputTex3D);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    glDisable(GL_SCISSOR_TEST);
}

GLuint GLRenderer2D::ResolveDirect3DToNative(GLuint sourceTex)
{
    if (!sourceTex)
        sourceTex = Parent.OutputTex3D;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, NativeDirect3DFB);
    const GLenum native3DResolveOutputs[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
    };
    glDrawBuffers(3, native3DResolveOutputs);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, 256, 192);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(Native3DResolveShader);
    int filterMode = 0;
    if (WholeSceneScaleFinalUpscale3DFilter == RendererSettings::FinalUpscale3DDownsampleFilter::Linear)
        filterMode = 1;
    else if (WholeSceneScaleFinalUpscale3DFilter == RendererSettings::FinalUpscale3DDownsampleFilter::Tent)
        filterMode = 2;
    glUniform1i(Native3DResolveFilterModeULoc, filterMode);
    glUniform1i(Native3DResolveCoverageAwareULoc, WholeSceneScaleFinalUpscale3DCoverageAware);
    glUniform1i(Native3DResolveRepresentativeSemanticsULoc, WholeSceneScaleFinalUpscale3DRepresentativeSemantics);
    glUniform1i(Native3DResolveSplitSemanticsULoc, WholeSceneScaleFinalUpscale3DSplitSemantics);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    return NativeDirect3DCompositorTex;
}

bool GLRenderer2D::UpdateCaptureBackedHandoff3DSnapshot(GLuint sourceTex)
{
    const int slot = std::min<int>(CaptureBackedHandoff.CurrentSlot, kCaptureBackedHandoffRouteSlots - 1);
    if (!sourceTex || !CaptureBackedRouteGL[slot].Handoff3DTex || !glBlitFramebuffer)
        return false;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sourceTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CaptureBackedRouteGL[slot].Handoff3DFB);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CaptureBackedRouteGL[slot].Handoff3DTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDisable(GL_SCISSOR_TEST);

    glBlitFramebuffer(0, 0, ScreenW, ScreenH,
                      0, 0, ScreenW, ScreenH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    LatchCaptureBackedHandoffSnapshot(slot, CaptureBackedHandoff.CurrentKey);
    return true;
}

void GLRenderer2D::PoisonWholeSceneDebugTexture(GLuint texture, int width, int height, bool forceAlphaOpaque)
{
    if (!texture || width <= 0 || height <= 0)
        return;

    GLint prevActiveTexture = 0;
    GLint prevTextureBinding2D = 0;
    GLint prevPackAlignment = 0;
    GLint prevUnpackAlignment = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding2D);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);

    std::vector<u8> rgba(width * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    const int cell = std::max(8, ScaleFactor * 8);
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            const bool checker = (((x / cell) ^ (y / cell)) & 1) != 0;
            const int offset = (y * width + x) * 4;
            rgba[offset + 0] = checker ? 255 : 0;
            rgba[offset + 1] = checker ? 0 : 255;
            rgba[offset + 2] = checker ? 255 : 0;
            if (forceAlphaOpaque)
                rgba[offset + 3] = 255;
        }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    glBindTexture(GL_TEXTURE_2D, prevTextureBinding2D);
    glActiveTexture(prevActiveTexture);
}

namespace
{
struct TextureContentStats
{
    bool Valid = false;
    int Width = 0;
    int Height = 0;
    int AlphaMin = 0;
    int AlphaMax = 0;
    double AlphaAvg = 0.0;
    double AlphaCoveredPct = 0.0;
    double IntraNativeCellDiff = 0.0;
    double NativeEdgeDiff = 0.0;
    u32 RGBHash = 0;
};

// Slow diagnostic readback. Keep this out of normal render paths.
void ReadTextureRGBA8(GLuint texture, int width, int height, std::vector<u8>& rgba)
{
    GLint prevActiveTexture = 0;
    GLint prevTextureBinding2D = 0;
    GLint prevPackAlignment = 0;

    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding2D);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

    rgba.resize(width * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    glBindTexture(GL_TEXTURE_2D, prevTextureBinding2D);
    glActiveTexture(prevActiveTexture);
}

TextureContentStats ReadTextureContentStats(GLuint texture)
{
    TextureContentStats stats;
    if (!texture)
        return stats;

    GLint prevActiveTexture = 0;
    GLint prevTextureBinding2D = 0;
    GLint prevPackAlignment = 0;

    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding2D);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);

    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &stats.Width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &stats.Height);

    if (stats.Width <= 0 || stats.Height <= 0 || stats.Width > 4096 || stats.Height > 4096)
    {
        glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
        glBindTexture(GL_TEXTURE_2D, prevTextureBinding2D);
        glActiveTexture(prevActiveTexture);
        return stats;
    }

    std::vector<u8> rgba(stats.Width * stats.Height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    glBindTexture(GL_TEXTURE_2D, prevTextureBinding2D);
    glActiveTexture(prevActiveTexture);

    stats.Valid = true;
    stats.AlphaMin = 255;
    stats.AlphaMax = 0;
    u64 alphaSum = 0;
    u64 covered = 0;
    u32 hash = 2166136261u;
    const int pixels = stats.Width * stats.Height;
    for (int i = 0; i < pixels; i++)
    {
        const int offset = i * 4;
        const int alpha = rgba[offset + 3];
        stats.AlphaMin = std::min(stats.AlphaMin, alpha);
        stats.AlphaMax = std::max(stats.AlphaMax, alpha);
        alphaSum += alpha;
        if (alpha > 8)
            covered++;

        hash ^= rgba[offset + 0];
        hash *= 16777619u;
        hash ^= rgba[offset + 1];
        hash *= 16777619u;
        hash ^= rgba[offset + 2];
        hash *= 16777619u;
    }
    stats.RGBHash = hash;
    stats.AlphaAvg = double(alphaSum) / double(std::max(pixels, 1));
    stats.AlphaCoveredPct = (double(covered) * 100.0) / double(std::max(pixels, 1));

    auto rgbDiff = [&](int a, int b)
    {
        const int ao = a * 4;
        const int bo = b * 4;
        return std::abs(int(rgba[ao + 0]) - int(rgba[bo + 0])) +
               std::abs(int(rgba[ao + 1]) - int(rgba[bo + 1])) +
               std::abs(int(rgba[ao + 2]) - int(rgba[bo + 2]));
    };

    u64 nativeEdgeSum = 0;
    u64 nativeEdgeCount = 0;
    for (int y = 0; y < stats.Height; y++)
    {
        for (int x = 0; x < stats.Width; x++)
        {
            const int i = y * stats.Width + x;
            if (x + 1 < stats.Width)
            {
                nativeEdgeSum += rgbDiff(i, i + 1);
                nativeEdgeCount++;
            }
            if (y + 1 < stats.Height)
            {
                nativeEdgeSum += rgbDiff(i, i + stats.Width);
                nativeEdgeCount++;
            }
        }
    }
    stats.NativeEdgeDiff = nativeEdgeCount ? double(nativeEdgeSum) / double(nativeEdgeCount * 3) : 0.0;

    if (stats.Width >= 256 && stats.Height >= 192 && (stats.Width % 256) == 0 && (stats.Height % 192) == 0)
    {
        const int scaleX = stats.Width / 256;
        const int scaleY = stats.Height / 192;
        if (scaleX > 1 || scaleY > 1)
        {
            u64 intraSum = 0;
            u64 intraCount = 0;
            for (int ny = 0; ny < 192; ny++)
            {
                for (int nx = 0; nx < 256; nx++)
                {
                    const int baseX = nx * scaleX;
                    const int baseY = ny * scaleY;
                    for (int y = 0; y < scaleY; y++)
                    {
                        for (int x = 0; x < scaleX; x++)
                        {
                            const int px = baseX + x;
                            const int py = baseY + y;
                            const int i = py * stats.Width + px;
                            if (x + 1 < scaleX)
                            {
                                intraSum += rgbDiff(i, i + 1);
                                intraCount++;
                            }
                            if (y + 1 < scaleY)
                            {
                                intraSum += rgbDiff(i, i + stats.Width);
                                intraCount++;
                            }
                        }
                    }
                }
            }
            stats.IntraNativeCellDiff = intraCount ? double(intraSum) / double(intraCount * 3) : 0.0;
        }
    }

    return stats;
}

std::string FormatTextureContentStats(const char* label, GLuint texture)
{
    TextureContentStats stats = ReadTextureContentStats(texture);
    std::string out = "\n  ";
    out += label;
    out += ": ";
    if (!stats.Valid)
        return out + "unavailable";

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "%dx%d, alpha min/max/avg %d/%d/%.1f, alpha covered %.1f%%, RGB hash %08X, edge %.2f, intra-native-cell %.2f",
                  stats.Width,
                  stats.Height,
                  stats.AlphaMin,
                  stats.AlphaMax,
                  stats.AlphaAvg,
                  stats.AlphaCoveredPct,
                  stats.RGBHash,
                  stats.NativeEdgeDiff,
                  stats.IntraNativeCellDiff);
    out += buffer;
    return out;
}
}

void GLRenderer2D::RenderOverlay3DEndpoint(GLuint targetTex, GLuint nativeDirect3DTex, bool whiteEndpoint)
{
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, 256, 192);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(OverlayEndpointShader);
    glUniform1i(OverlayEndpointWhiteULoc, whiteEndpoint);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, nativeDirect3DTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

void GLRenderer2D::EnsureWholeSceneOverlayEndpoints(GLuint nativeDirect3DTex)
{
    if (WholeSceneOverlayEndpointsValid &&
        WholeSceneOverlayEndpointSourceTex == nativeDirect3DTex)
    {
        return;
    }

    auto phaseStart = std::chrono::steady_clock::now();
    RenderOverlay3DEndpoint(NativeOverlayBlack3DTex, nativeDirect3DTex, false);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayEndpoint, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderOverlay3DEndpoint(NativeOverlayWhite3DTex, nativeDirect3DTex, true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayEndpoint, ElapsedUS(phaseStart));

    WholeSceneOverlayEndpointsValid = true;
    WholeSceneOverlayEndpointSourceTex = nativeDirect3DTex;
}

void GLRenderer2D::RenderOverlayComposite(GLuint overlayBlackTex,
                                          GLuint overlayWhiteTex,
                                          GLuint direct3DTex,
                                          bool conservativeHybrid,
                                          GLuint hybridForegroundTex,
                                          GLuint hybridNativeFallbackTex,
                                          GLuint hybrid2DBaseTex,
                                          GLuint hybridLegacyCandidateTex,
                                          bool useHybridLegacyCandidate,
                                          GLuint nativeRole3DTex,
                                          GLuint targetTex,
                                          bool forceDebugTint,
                                          int hybridDebugMode,
                                          int ystart,
                                          int yend,
                                          bool direct3DPresentationSpace)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (targetTex)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);
    }
    else
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OutputFB);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, ScreenW, ScreenH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, ystart * ScaleFactor, ScreenW, (yend - ystart) * ScaleFactor);

    const GLuint shader = conservativeHybrid ? OverlayHybridCompositeShader : OverlayCompositeShader;
    const GLint scaleULoc = conservativeHybrid ? OverlayHybridCompositeScaleULoc : OverlayCompositeScaleULoc;
    const GLint debugTintULoc = conservativeHybrid ? OverlayHybridCompositeDebugTintULoc : OverlayCompositeDebugTintULoc;
    const GLint legacyUnderlayULoc = conservativeHybrid ? OverlayHybridCompositeLegacyUnderlayULoc : OverlayCompositeLegacyUnderlayULoc;
    const GLint coverageAwareULoc = conservativeHybrid ? OverlayHybridCompositeCoverageAwareULoc : OverlayCompositeCoverageAwareULoc;
    const GLint direct3DPresentationSpaceULoc = conservativeHybrid ? OverlayHybridCompositeDirect3DPresentationSpaceULoc : OverlayCompositeDirect3DPresentationSpaceULoc;

    glUseProgram(shader);
    glUniform1i(scaleULoc, ScaleFactor);
    glUniform1i(debugTintULoc, forceDebugTint || WholeSceneScaleDebugTint);
    glUniform1i(legacyUnderlayULoc, WholeSceneScaleOverlayLegacyUnderlay);
    glUniform1i(coverageAwareULoc, WholeSceneScaleFinalUpscale3DCoverageAware);
    glUniform1i(direct3DPresentationSpaceULoc, direct3DPresentationSpace);
    if (conservativeHybrid)
    {
        const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
        const bool forceOverlayAssist = WholeSceneScaleCaptureBacked && !GPU2D.Num && dispmode == 2;
        glUniform1i(OverlayHybridCompositeConservativeHybridULoc, true);
        glUniform1i(OverlayHybridCompositeWindowEdgeAssistULoc, WholeSceneScaleHybridWindowEdgeAssist);
        glUniform1i(OverlayHybridCompositeTarget2AlphaBlendAssistULoc, WholeSceneScaleHybridTarget2AlphaBlendAssist);
        glUniform1i(OverlayHybridCompositeNativeEffectGuardULoc, WholeSceneScaleHybridNativeEffectGuard);
        glUniform1i(OverlayHybridCompositeForeground2DBaseULoc, WholeSceneScaleHybridForeground2DBase);
        glUniform1i(OverlayHybridCompositeLegacyCandidateULoc, useHybridLegacyCandidate);
        glUniform1i(OverlayHybridCompositeForceOverlayAssistULoc, forceOverlayAssist);
        glUniform1i(OverlayHybridCompositeDebugModeULoc, hybridDebugMode);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);
    if (conservativeHybrid)
        glBindBufferBase(GL_UNIFORM_BUFFER, 23, CompositorConfigUBO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayBlackTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, overlayWhiteTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, direct3DTex);

    if (conservativeHybrid)
    {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, NativeMetaTex);

        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, NativeTopColorTex);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, hybridForegroundTex);

        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, hybridNativeFallbackTex);

        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, nativeRole3DTex);

        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, hybrid2DBaseTex);

        glActiveTexture(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D, useHybridLegacyCandidate ? hybridLegacyCandidateTex : BlankColorTex);

    }

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    glDisable(GL_SCISSOR_TEST);
}

void GLRenderer2D::RenderOverlayDebugTexture(GLuint targetTex,
                                             int width,
                                             int height,
                                             GLuint overlayBlackTex,
                                             GLuint overlayWhiteTex,
                                             GLuint direct3DTex,
                                             GLuint nativeFinalTex,
                                             int debugMode)
{
    glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(OverlayDebugShader);
    glUniform1i(OverlayDebugModeULoc, debugMode);
    glUniform1i(OverlayDebugLegacyUnderlayULoc, WholeSceneScaleOverlayLegacyUnderlay);
    glUniform1i(OverlayDebugCoverageAwareULoc, WholeSceneScaleFinalUpscale3DCoverageAware);
    glBindBufferBase(GL_UNIFORM_BUFFER, 22, ScanlineConfigUBO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayBlackTex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, overlayWhiteTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, direct3DTex);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, nativeFinalTex);

    glBindBuffer(GL_ARRAY_BUFFER, Parent.RectVtxBuffer);
    glBindVertexArray(Parent.RectVtxArray);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
}

void GLRenderer2D::RenderScreenCurrent(int ystart, int yend,
                                       bool currentFragmentationFallback,
                                       WholeSceneCurrentPathReason reason)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::Current, ystart, yend,
                                false, false, false, Parent.OutputTex3D,
                                false, currentFragmentationFallback);
    WholeSceneTrace.CurrentReason = reason;
    RenderCompositorPass(OutputFB, OBJLayerTex, ScreenW, ScreenH, ystart, yend, ScaleFactor, 0, false, false, false);
}

void GLRenderer2D::RenderScreenWholeSceneLegacy(int ystart, int yend, bool cleanHybridCandidate)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::LegacyNativeUpscale, ystart, yend,
                                false, false, false, Parent.OutputTex3D);
    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativePrepass(ystart, yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));
    if (!cleanHybridCandidate && WholeSceneScaleExactFinalFallback && !CanUseWholeSceneForegroundOverlayPath())
    {
        phaseStart = std::chrono::steady_clock::now();
        RenderNativeExactFinal(ystart, yend);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativeExactFinal, ElapsedUS(phaseStart));
        WholeSceneTrace.NativeExactFinalValid = true;
        RecordWholeSceneNativeProductChunk(ystart, yend);
    }
    RenderNativeUpscale(ystart, yend);
    RenderNativeResolve(ystart, yend);
}

void GLRenderer2D::RenderScreenWholeSceneHighRes(int ystart, int yend)
{
    ResetWholeSceneRenderTrace();
    RecordWholeSceneRenderTrace(WholeSceneRenderPath::HighResCompositor, ystart, yend,
                                ScaleFactor > 1, false, false, Parent.OutputTex3D);
    RenderCompositorPass(OutputFB, OBJLayerTex, ScreenW, ScreenH, ystart, yend, ScaleFactor,
                         WholeSceneHighResLayerFilterMode(),
                         WholeSceneHighResLayerFilterNoWrap(),
                         false,
                         WholeSceneScaleDebugTint);
}

void GLRenderer2D::ApplyRouteProductLookupToSourceAChoice(SourceACaptureReplacementChoice& choice,
                                                          const CaptureBackedRouteProductLookup& lookup) const
{
    choice.RouteProductTex = lookup.Tex;
    if (!lookup.Valid)
        return;

    choice.RouteProductBackgroundEpochSerial = lookup.Identity.BackgroundEpochSerial;
    choice.RouteProductSource3DSerial = lookup.Identity.Source3DSerial;
    choice.RouteProductSource3DSceneHash = lookup.Identity.Source3DSceneHash;
    choice.RouteProductCapturedEventSerial = lookup.CapturedEventSerial;
    choice.RouteProductCapturePresentationHash = lookup.Identity.CapturePresentationHash;
    choice.RouteProductCurrentPresentationHash = lookup.Identity.CurrentOverlayPresentationHash;
    choice.RouteProductStableFrames = lookup.StableFrames;
    choice.RouteProductPresentationClass = lookup.PresentationClass;
    choice.RouteProductStoredMasterBrightness = lookup.StoredMasterBrightness;
    choice.RouteProductHasStoredEffectState = lookup.HasStoredEffectState;
    choice.RouteProductKind = CaptureProductKindForRouteLookup(lookup.Source);
    choice.RouteProductProof = CaptureProofKindForRouteLookup(lookup.Source);
}

GLRenderer2D::SourceACaptureReplacementChoice GLRenderer2D::ChooseSourceACaptureReplacement(int ystart, int yend)
{
    SourceACaptureReplacementChoice choice = {};
    choice.FullProductTex = VisibleHighResCaptureFullTex();
    choice.CaptureBank = VisibleSingleHighResCaptureBank();
    choice.FullProductCaptureBank = choice.CaptureBank;
    choice.FullProductTexID = static_cast<int>(choice.FullProductTex);
    choice.RouteSlot = Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, ystart, yend) ? 1 : 0;

    const u32 dispmode = (DispCnt >> 16) & (GPU2D.Num ? 0x1u : 0x3u);
    choice.DirectFinalDisplayConsumer = dispmode == 1;
    choice.DirectFinalBottomConsumer =
        choice.DirectFinalDisplayConsumer &&
        Parent.IsEngineRoutedToFinalBottom(GPU2D.Num, ystart, yend);

    const u32 activeCapCnt = GPU.CaptureCnt;
    choice.ActiveDisplayCaptureDstBank = GPU.CaptureEnable ? static_cast<int>((activeCapCnt >> 16) & 0x3u) : -1;
    choice.ActiveDisplayCaptureDstOffset = GPU.CaptureEnable ? static_cast<int>((activeCapCnt >> 18) & 0x3u) : -1;
    choice.ActiveDisplayCaptureSourceA2D =
        !GPU2D.Num &&
        GPU.CaptureEnable &&
        (((activeCapCnt >> 24) & 0x1u) == 0);
    choice.ActiveFullDisplayCaptureSourceA =
        choice.ActiveDisplayCaptureSourceA2D &&
        Parent.IsFullDisplayCaptureFromSourceAOnly(activeCapCnt);

    if (choice.CaptureBank >= 0 && choice.CaptureBank < 4)
    {
        const auto& selectedEvent = Parent.HighResDisplayCapture256Event[choice.CaptureBank];
        choice.FullProductEventValid =
            Parent.IsFullDisplayHighResCaptureEventRecord(selectedEvent,
                                                          static_cast<u32>(choice.CaptureBank));
        choice.FullProductEventSerial = selectedEvent.Serial;
        choice.FullProductEventSource3DSerial = selectedEvent.Source3DSerial;
        choice.FullProductEventSource3DSceneHash = selectedEvent.Source3DSceneHash;
        choice.FullProductEventSourcePresentationHash = selectedEvent.SourcePresentationHash;
        choice.FullProductEventSourceKind = static_cast<u32>(selectedEvent.SourceKind);
        choice.FullProductEventProductMask = selectedEvent.ProductMask;
        choice.FullProductEventRejectReason = static_cast<u32>(selectedEvent.RejectReason);
        choice.FullProductEventDstBlock = static_cast<int>(selectedEvent.DstBlock);
        choice.FullProductEventDstOffset = static_cast<int>(selectedEvent.DstOffset);
        choice.FullProductEventSourceOBJ = selectedEvent.SourceOBJVisible;
        choice.FullProductEventScreenSwap = selectedEvent.ScreenSwap;
        choice.FullProductEventMainFinalBottom = selectedEvent.MainEngineFinalBottom;
    }

    const u32 visibleBGMask = LayerEnable & 0x0Fu;
    const u32 sourceAOnlyCaptureBGMask = VisibleSourceAOnlyFullDisplayCaptureBGLayerMask();
    const bool objVisible = (LayerEnable & (1 << 4)) && OBJEnable && NumSprites > 0;
    const bool mainEngineCapturedBGOnly =
        !GPU2D.Num &&
        choice.DirectFinalDisplayConsumer &&
        choice.CaptureBank >= 0 &&
        choice.CaptureBank < 4 &&
        visibleBGMask != 0 &&
        (visibleBGMask & ~sourceAOnlyCaptureBGMask) == 0 &&
        !objVisible;
    choice.MainEngineCapturedBGOnly = mainEngineCapturedBGOnly;

    const bool subEngineFullFrameCaptureConsumer =
        GPU2D.Num &&
        ystart == 0 &&
        yend == 192 &&
        choice.CaptureBank >= 0;
    const bool subEngineCapturedBGOnly =
        subEngineFullFrameCaptureConsumer &&
        (LayerEnable & (1 << 4)) == 0;
    const VisibleOBJCaptureDebug objCaptureDebug = BuildVisibleOBJCaptureDebug();
    const bool fullFrameRange = ystart == 0 && yend == 192;
    const bool trackedVRAMDisplayRoute =
        fullFrameRange &&
        Parent.IsMainVRAMDisplayFinalRouteForRange(ystart, yend);
    const bool fullWidthSourceAProduct =
        objCaptureDebug.FullWidthTopStrip &&
        objCaptureDebug.CurrentFullSourceA &&
        objCaptureDebug.EventRejectReason == 0 &&
        objCaptureDebug.ProductAvailable &&
        trackedVRAMDisplayRoute;
    const bool exactOBJReplacement =
        objCaptureDebug.NonCaptureSpriteCount == 0 &&
        (objCaptureDebug.FullScreen || fullWidthSourceAProduct);
    const bool subEngineCapturedOBJOnly =
        subEngineFullFrameCaptureConsumer &&
        (LayerEnable & 0x0Fu) == 0 &&
        (LayerEnable & (1 << 4)) &&
        OBJEnable &&
        objCaptureDebug.Found &&
        !objCaptureDebug.MixedBank &&
        objCaptureDebug.CurrentSourceAOnly &&
        exactOBJReplacement &&
        objCaptureDebug.ProductAvailable;
    choice.SubEngineCapturedSourceAOnly =
        subEngineCapturedBGOnly || subEngineCapturedOBJOnly;
    choice.SubEngineCapturedOBJOnly = subEngineCapturedOBJOnly;

    if (!choice.SubEngineCapturedSourceAOnly &&
        !choice.MainEngineCapturedBGOnly)
        return choice;

    const auto& event = Parent.HighResDisplayCapture256Event[choice.CaptureBank];
    choice.BackgroundTex =
        (event.ProductMask & GLRenderer::HighResCaptureProductBackground3DUnderlay)
            ? Parent.HighResDisplayCaptureBackgroundTex[choice.CaptureBank]
            : 0;
    if (choice.BackgroundTex &&
        Parent.UpdateCaptureBackgroundEpochForRoute(choice.RouteSlot, event))
    {
        choice.BackgroundEpochSerial = Parent.ActiveCaptureBackgroundEpoch[choice.RouteSlot].Serial;
        choice.BackgroundSource3DSerial = Parent.ActiveCaptureBackgroundEpoch[choice.RouteSlot].Source3DSerial;
        choice.BackgroundSource3DSceneHash =
            Parent.ActiveCaptureBackgroundEpoch[choice.RouteSlot].Source3DSceneHash;
    }

    choice.MainRenderer = dynamic_cast<GLRenderer2D*>(Parent.Rend2D_A.get());
    choice.CapturePresentationHash = event.SourcePresentationHash;
    if (choice.MainRenderer)
    {
        choice.CurrentPresentationHash = choice.MainRenderer->CapturePresentationHash();
        choice.FullProductKeyMatch = CanUseCaptureProductAsPresented(
            WholeSceneCaptureProductPresentationClass::AlreadyPresented,
            choice.CapturePresentationHash,
            choice.CurrentPresentationHash);
    }
    else
    {
        choice.FullProductKeyMatch = false;
    }
    ResolveSourceARouteProductChoice(choice,
                                     event.Serial,
                                     event.Source3DSerial,
                                     event.Source3DSceneHash,
                                     ystart,
                                     yend);

    SourceAExactFullProductPreferenceInputs preferenceInputs = {};
    bool currentScreenSwap = GPU.ScreenSwap;
    Parent.GetFinalPassScreenSwapForRange(ystart, yend, currentScreenSwap);
    const bool currentMainEngineFinalBottom =
        Parent.IsEngineRoutedToFinalBottom(0, ystart, yend);
    DirectFinalRouteMatchInputs directFinalRouteInputs = {};
    directFinalRouteInputs.EventScreenSwap = choice.FullProductEventScreenSwap;
    directFinalRouteInputs.CurrentScreenSwap = currentScreenSwap;
    directFinalRouteInputs.EventMainFinalBottom = choice.FullProductEventMainFinalBottom;
    directFinalRouteInputs.CurrentMainFinalBottom = currentMainEngineFinalBottom;
    preferenceInputs.DirectFinalBottomConsumer = choice.DirectFinalBottomConsumer;
    preferenceInputs.SubEngineCapturedSourceAOnly = choice.SubEngineCapturedSourceAOnly;
    preferenceInputs.HasFullProduct = choice.FullProductTex != 0;
    preferenceInputs.FullProductEventValid = choice.FullProductEventValid;
    preferenceInputs.FullProductKeyMatch = choice.FullProductKeyMatch;
    preferenceInputs.FullProductEventRouteMatches =
        DoesDirectFinalRouteMatch(directFinalRouteInputs);
    preferenceInputs.FullProductEventFullEquivalent =
        (choice.FullProductEventProductMask & GLRenderer::HighResCaptureProductFullEquivalent) != 0;
    preferenceInputs.FullProductEventCleanEngineA2DOutput =
        choice.FullProductEventSourceKind ==
            static_cast<u32>(GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput);
    preferenceInputs.FullProductEventAccepted =
        choice.FullProductEventRejectReason ==
            static_cast<u32>(GLRenderer::HighResCaptureRejectReason::None);
    preferenceInputs.FullProductEventSourceOBJVisible =
        choice.FullProductEventSourceOBJ;
    const bool preferExactFullProductForDirectBottom =
        ShouldPreferSourceAExactFullProductForDirectBottom(preferenceInputs);
    choice.PreferExactFullProduct = preferExactFullProductForDirectBottom;
    choice.AllowExactFullProductCapturePresentation =
        (choice.DirectFinalBottomConsumer ||
         (choice.DirectFinalDisplayConsumer && choice.SubEngineCapturedOBJOnly) ||
         (choice.DirectFinalDisplayConsumer &&
          choice.MainEngineCapturedBGOnly &&
          preferenceInputs.FullProductEventRouteMatches)) &&
        (choice.SubEngineCapturedSourceAOnly || choice.MainEngineCapturedBGOnly) &&
        choice.FullProductTex != 0 &&
        choice.FullProductEventValid &&
        preferenceInputs.FullProductEventFullEquivalent &&
        preferenceInputs.FullProductEventCleanEngineA2DOutput &&
        preferenceInputs.FullProductEventAccepted;

    SourceACurrentOverlayEligibilityInputs overlayInputs = {};
    overlayInputs.HasBackgroundTexture = choice.BackgroundTex != 0;
    overlayInputs.PreferExactFullProductForDirectBottom = preferExactFullProductForDirectBottom;
    overlayInputs.SourceIsCleanEngineA2DOutput =
        event.SourceKind == GLRenderer::HighResCaptureSourceKind::CleanEngineA2DOutput;
    overlayInputs.RendererCanCompositeCurrentOverlay =
        overlayInputs.HasBackgroundTexture &&
        !overlayInputs.PreferExactFullProductForDirectBottom &&
        overlayInputs.SourceIsCleanEngineA2DOutput &&
        choice.MainRenderer &&
        choice.MainRenderer->CanRenderCurrentOverlayForCaptureSource(event.SourceLayerEnable,
                                                                     event.SourceBGMode,
                                                                     event.SourceVisibleBitmapMask,
                                                                     event.SourceDirect3DVisible);
    overlayInputs.SubEngineDirectFinalTopConsumer =
        GPU2D.Num != 0 &&
        choice.DirectFinalDisplayConsumer &&
        !choice.DirectFinalBottomConsumer;
    choice.CanUseCurrentOverlay = CanUseSourceACurrentOverlay(overlayInputs);
    return choice;
}

void GLRenderer2D::BlitWholeSceneCaptureProduct(GLuint sourceTex,
                                                int ystart,
                                                int yend,
                                                u16 presentationMasterBrightness,
                                                bool applyPresentationMasterBrightness,
                                                WholeSceneCaptureEffectOwner presentationEffectOwner)
{
    // A direct-final replacement stands in for content that natively passes
    // through the consuming engine's blend stage, so a frame-global BLDY
    // brightness effect must be reproduced here. It preempts the Source-A
    // master-brightness approximation below: natively the consuming engine's
    // color effect is the only compositor-stage transform on this content,
    // and the engine's own master brightness still runs in the final pass.
    const u16 consumerColorEffect =
        ConsumerFullScreenBrightnessColorEffect(BlendCnt, EVY);
    if (consumerColorEffect != 0 &&
        sourceTex != OutputTex &&
        ApplyMasterBrightnessToTexture(OutputTex,
                                       sourceTex,
                                       ScreenW,
                                       ScreenH,
                                       ystart,
                                       yend,
                                       consumerColorEffect))
    {
        RecordOutputPresentationMasterBrightness(
            WholeSceneCaptureEffectOwner::CurrentEngineColorEffect,
            consumerColorEffect);
        return;
    }

    const int brightMode = (presentationMasterBrightness >> 14) & 0x3;
    const int brightFactor = std::min<int>(presentationMasterBrightness & 0x1F, 16);
    if (applyPresentationMasterBrightness &&
        sourceTex != OutputTex &&
        (brightMode == 1 || brightMode == 2) &&
        brightFactor > 0 &&
        ApplyMasterBrightnessToTexture(OutputTex,
                                       sourceTex,
                                       ScreenW,
                                       ScreenH,
                                       ystart,
                                       yend,
                                       presentationMasterBrightness))
    {
        RecordOutputPresentationMasterBrightness(presentationEffectOwner,
                                                 presentationMasterBrightness);
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, WholeSceneSourceABlitFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sourceTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OutputFB);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    const int y0 = ystart * ScaleFactor;
    const int y1 = yend * ScaleFactor;
    glBlitFramebuffer(0, y0, ScreenW, y1,
                      0, y0, ScreenW, y1,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void GLRenderer2D::BlitWholeSceneSourceAReplacement(GLuint sourceTex,
                                                    int ystart,
                                                    int yend,
                                                    bool applySourceAMasterBrightness)
{
    BlitWholeSceneCaptureProduct(sourceTex,
                                 ystart,
                                 yend,
                                 GPU.MasterBrightnessA,
                                 applySourceAMasterBrightness,
                                 WholeSceneCaptureEffectOwner::SourceA);
}

void GLRenderer2D::BlitWholeSceneHandoffProduct(const GLCaptureProductResolution& product,
                                                int ystart,
                                                int yend)
{
    const bool applyPresentationEffect =
        ShouldApplyHandoffPresentationEffect(product.PresentationClass,
                                            WholeSceneTrace.CaptureRequestKind);
    const u16 currentMasterBrightness =
        GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    BlitWholeSceneCaptureProduct(product.Tex,
                                 ystart,
                                 yend,
                                 currentMasterBrightness,
                                 applyPresentationEffect,
                                 WholeSceneCaptureEffectOwner::CurrentEngine);
}

void GLRenderer2D::BlitWholeSceneSourceAProduct(const GLCaptureProductResolution& product,
                                                int ystart,
                                                int yend)
{
    const bool applyPresentationEffect =
        ShouldApplySourceAReplacementPresentationEffect(product.PresentationClass,
                                                       WholeSceneTrace.CaptureRequestKind,
                                                       GPU2D.Num != 0);
    BlitWholeSceneSourceAReplacement(product.Tex,
                                     ystart,
                                     yend,
                                     applyPresentationEffect);
}

void GLRenderer2D::RenderScreenWholeSceneCaptureBackedHybridFallback(int ystart, int yend)
{
    RenderScreenWholeSceneOverlayOperator(ystart, yend);
    RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(true, ystart, yend);
}

void GLRenderer2D::RenderScreenWholeSceneSourceACaptureReplacement(int ystart, int yend)
{
    if (CanUseWholeSceneMixedCaptureBackedOBJOverlayPath())
    {
        if (RenderCurrentBGOverlayOverHighResCaptureOBJToTexture(OutputTex, ystart, yend))
            return;

        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        return;
    }

    if (!GPU2D.Num &&
        CanUseWholeSceneMixedSourceACaptureBGPath())
    {
        if (RenderCurrentLayersOverHighResCaptureBGToTexture(OutputTex, ystart, yend))
            return;

        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        return;
    }

    const SourceACaptureReplacementChoice choice = ChooseSourceACaptureReplacement(ystart, yend);
    auto makeResolutionInputs = [&](bool allowCurrentOverlay = true)
    {
        SourceACaptureResolutionInputs inputs = {};
        inputs.HasRouteProduct = choice.RouteProductTex != 0;
        inputs.RouteProductNeedsRePresentation =
            choice.RouteProductTex != 0 &&
            choice.RouteProductPresentationClass ==
                WholeSceneCaptureProductPresentationClass::RawContent &&
            choice.RouteProductCurrentPresentationHash != 0 &&
            choice.CurrentPresentationHash != 0 &&
            !DoesCaptureProductPresentationMatchRequest(
                choice.RouteProductCurrentPresentationHash,
                choice.CurrentPresentationHash);
        inputs.CanUseCurrentOverlay = choice.CanUseCurrentOverlay;
        inputs.HasFullProduct = choice.FullProductTex != 0;
        inputs.PreferExactFullProduct = choice.PreferExactFullProduct;
        inputs.AllowCurrentOverlay = allowCurrentOverlay;
        return inputs;
    };

    SourceACaptureResolutionKind resolutionKind =
        ChooseSourceACaptureResolutionKind(makeResolutionInputs());

    if (resolutionKind == SourceACaptureResolutionKind::RouteProduct)
    {
        const GLCaptureProductResolution product =
            RecordSourceARouteProductChoiceTrace(choice, ystart, yend);
        if (!product.Accepted)
        {
            RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
            RecordSourceARejectedChoiceTrace(choice, ystart, yend);
            return;
        }

        BlitWholeSceneSourceAProduct(product, ystart, yend);
        return;
    }

    if (resolutionKind == SourceACaptureResolutionKind::BackgroundOverlay)
    {
        const GLCaptureProductResolution product =
            RecordSourceABackgroundOverlayChoiceTrace(choice, ystart, yend);
        if (!product.Accepted)
        {
            RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
            RecordSourceARejectedChoiceTrace(choice, ystart, yend);
            return;
        }

        bool outputMasterBrightnessApplied = false;
        if (choice.MainRenderer->RenderCurrentOverlayOverHighResBackgroundToTexture(OutputTex,
                                                                                    product.Tex,
                                                                                    ystart,
                                                                                    yend,
                                                                                    true,
                                                                                    nullptr,
                                                                                    &outputMasterBrightnessApplied))
        {
            if (outputMasterBrightnessApplied)
            {
                RecordOutputPresentationMasterBrightness(WholeSceneCaptureEffectOwner::SourceA,
                                                         GPU.MasterBrightnessA);
            }
            WholeSceneTrace.SourceACaptureMode = SourceACaptureReplacementMode::CurrentOverlay;
            return;
        }
        WholeSceneTrace.SourceACaptureMode = SourceACaptureReplacementMode::FullProductAfterOverlayFailed;
        WholeSceneTrace.SourceAProductChoice =
            SourceAProductChoiceReason::RejectedCurrentOverlayKeyMismatch;
        resolutionKind = ChooseSourceACaptureResolutionKind(makeResolutionInputs(false));
    }

    if (resolutionKind == SourceACaptureResolutionKind::RejectedFallback)
    {
        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        RecordSourceARejectedChoiceTrace(choice, ystart, yend);
        return;
    }

    if (resolutionKind != SourceACaptureResolutionKind::FullProduct)
    {
        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        RecordSourceARejectedChoiceTrace(choice, ystart, yend);
        return;
    }

    const GLCaptureProductResolution product =
        RecordSourceAFullProductChoiceTrace(choice, ystart, yend);
    if (!product.Accepted)
    {
        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        RecordSourceARejectedChoiceTrace(choice, ystart, yend);
        return;
    }

    BlitWholeSceneSourceAProduct(product, ystart, yend);
}

void GLRenderer2D::RenderScreenWholeSceneCaptureEpochOverlay(int ystart, int yend)
{
    int routeSlot = -1;
    if (!CanUseCaptureEpochBackgroundForLiveOverlay(ystart, yend, routeSlot))
    {
        RenderScreenWholeSceneOverlayOperator(ystart, yend);
        return;
    }

    const auto& epoch = Parent.ActiveCaptureBackgroundEpoch[routeSlot];
    const u32 currentPresentationHash = CapturePresentationHash();
    const bool useCurrentDirect3DBackground =
        currentPresentationHash != epoch.SourcePresentationHash;
    const SourceABackgroundSource backgroundSource =
        useCurrentDirect3DBackground
            ? SourceABackgroundSource::ParentOutputTex3D
            : SourceABackgroundSource::ActiveCaptureEpochTex;
    const GLuint backgroundTex =
        useCurrentDirect3DBackground ? Parent.OutputTex3D : Parent.ActiveCaptureBackgroundEpochTex[routeSlot];
    const u64 routeProductBackgroundSerial =
        useCurrentDirect3DBackground ? 0 : epoch.Serial;
    const u64 routeProductSource3DSerial =
        useCurrentDirect3DBackground ? Parent.Output3DSerial : epoch.Source3DSerial;
    const u32 routeProductSource3DSceneHash =
        useCurrentDirect3DBackground ? Parent.Output3DSceneHash : epoch.Source3DSceneHash;
    const u32 routeProductPresentationHash =
        useCurrentDirect3DBackground ? currentPresentationHash : epoch.SourcePresentationHash;
    const u16 backgroundStoredMasterBrightness =
        useCurrentDirect3DBackground ? 0 : epoch.StoredMasterBrightness;
    const bool backgroundHasStoredEffectState =
        !useCurrentDirect3DBackground && epoch.HasStoredEffectState;
    const auto& previousRoutePresentation = CaptureBackedRoute[routeSlot].Presentation;
    const bool canPromoteLiveRouteProduct =
        !useCurrentDirect3DBackground ||
        (routeProductSource3DSceneHash != 0 &&
         previousRoutePresentation.Valid &&
         previousRoutePresentation.Source3DSceneHash == routeProductSource3DSceneHash &&
         previousRoutePresentation.SourcePresentationHash == routeProductPresentationHash &&
         previousRoutePresentation.CurrentOverlayPresentationHash == currentPresentationHash);

    UpdateCaptureBackedRoutePresentation(routeSlot,
                                         CaptureBackedRoutePresentationMode::BackgroundCurrentOverlay,
                                         routeProductBackgroundSerial,
                                         epoch.CaptureBank,
                                         routeProductSource3DSceneHash,
                                         routeProductPresentationHash,
                                         currentPresentationHash);

    const GLCaptureProductResolution product =
        RecordCaptureEpochOverlayTrace(routeSlot,
                                       epoch.CaptureBank,
                                       backgroundSource,
                                       backgroundTex,
                                       epoch.Serial,
                                       routeProductBackgroundSerial,
                                       routeProductPresentationHash,
                                       currentPresentationHash,
                                       backgroundStoredMasterBrightness,
                                       backgroundHasStoredEffectState,
                                       ystart,
                                       yend);

    if (!product.Accepted)
    {
        RenderScreenWholeSceneOverlayOperator(ystart, yend);
        return;
    }

    GLuint rawRouteProductTex = OutputTex;

    bool outputMasterBrightnessApplied = false;
    if (RenderCurrentOverlayOverHighResBackgroundToTexture(OutputTex,
                                                           product.Tex,
                                                           ystart,
                                                           yend,
                                                           false,
                                                           &rawRouteProductTex,
                                                           &outputMasterBrightnessApplied))
    {
        if (outputMasterBrightnessApplied)
        {
            const u16 masterBrightness = GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
            RecordOutputPresentationMasterBrightness(WholeSceneCaptureEffectOwner::CurrentEngine,
                                                     masterBrightness);
        }
        StoreCurrentOverlayCaptureBackedRouteProduct(routeSlot,
                                                     rawRouteProductTex,
                                                     routeProductBackgroundSerial,
                                                     routeProductSource3DSerial,
                                                     routeProductSource3DSceneHash,
                                                     epoch.CaptureBank,
                                                     routeProductPresentationHash,
                                                     currentPresentationHash,
                                                     ystart,
                                                     yend);
        if (!canPromoteLiveRouteProduct)
        {
            WholeSceneTrace.SourceAProductChoice =
                SourceAProductChoiceReason::DeferredLiveScenePromotion;
        }

        return;
    }

    WholeSceneTrace.SourceAProductChoice = SourceAProductChoiceReason::FallbackNormalHybrid;
    RenderScreenWholeSceneOverlayOperator(ystart, yend);
}

bool GLRenderer2D::TryRenderHandoffBackgroundOverlayProduct(const WholeSceneCaptureRequest& request,
                                                            int handoffSlot,
                                                            const HandoffBackgroundChoice& background,
                                                            int ystart,
                                                            int yend)
{
    if (background.Authority != WholeSceneCaptureAuthority::CaptureEventBackground ||
        !background.Tex)
    {
        return false;
    }

    const u32 currentPresentationHash = CapturePresentationHash();
    const GLCaptureProductResolution product =
        RecordHandoffBackgroundOverlayTrace(request,
                                            background.Source,
                                            background.Authority,
                                            background.Tex,
                                            background.BackgroundEpochSerial,
                                            background.PresentationHash,
                                            currentPresentationHash,
                                            background.StoredMasterBrightness,
                                            background.HasStoredEffectState,
                                            ystart,
                                            yend);
    if (!product.Accepted)
        return false;

    GLuint rawRouteProductTex = OutputTex;

    bool outputMasterBrightnessApplied = false;
    if (RenderCurrentOverlayOverHighResBackgroundToTexture(OutputTex,
                                                           product.Tex,
                                                           ystart,
                                                           yend,
                                                           false,
                                                           &rawRouteProductTex,
                                                           &outputMasterBrightnessApplied))
    {
        if (outputMasterBrightnessApplied)
        {
            const u16 masterBrightness = GPU2D.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
            RecordOutputPresentationMasterBrightness(WholeSceneCaptureEffectOwner::CurrentEngine,
                                                     masterBrightness);
        }
        const u32 captureBank =
            handoffSlot >= 0 &&
            handoffSlot < kCaptureBackedHandoffRouteSlots &&
            Parent.ActiveCaptureBackgroundEpoch[handoffSlot].Valid
                ? Parent.ActiveCaptureBackgroundEpoch[handoffSlot].CaptureBank
                : 0xFFFFFFFFu;
        StoreCurrentOverlayCaptureBackedRouteProduct(handoffSlot,
                                                     rawRouteProductTex,
                                                     background.BackgroundEpochSerial,
                                                     background.Source3DSerial,
                                                     background.Source3DSceneHash,
                                                     captureBank,
                                                     background.PresentationHash,
                                                     currentPresentationHash,
                                                     ystart,
                                                     yend);

        return true;
    }

    WholeSceneTrace.SourceAProductChoice = SourceAProductChoiceReason::FallbackNormalHybrid;
    return false;
}

bool GLRenderer2D::TryPrepareStableHandoffLiveBackground(int handoffSlot,
                                                         HandoffBackgroundChoice& background,
                                                         int ystart,
                                                         int yend)
{
    auto phaseStart = std::chrono::steady_clock::now();
    const u32 visibleBGLayers = CaptureBackedHandoff.CurrentKey.LayerEnable & 0x0Fu;
    const bool direct3DOnlyLiveBackground =
        visibleBGLayers == (1u << 0) &&
        (DispCnt & (1 << 3));

    bool updatedSnapshot = false;
    if (direct3DOnlyLiveBackground)
    {
        updatedSnapshot = UpdateCaptureBackedHandoff3DSnapshot(Parent.OutputTex3D);
    }
    else
    {
        RenderCompositorPass(CaptureBackedRouteGL[handoffSlot].Handoff3DFB,
                             OBJLayerTex,
                             ScreenW,
                             ScreenH,
                             ystart,
                             yend,
                             ScaleFactor,
                             0,
                             false,
                             false,
                             false,
                             0,
                             0,
                             true);
        LatchCaptureBackedHandoffSnapshot(handoffSlot, CaptureBackedHandoff.CurrentKey);
        updatedSnapshot = true;
    }
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.HybridForegroundCandidate, ElapsedUS(phaseStart));

    if (!updatedSnapshot)
    {
        CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::RejectedNoSnapshot;
        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        return false;
    }

    CaptureBackedHandoff.BackgroundUpdated = true;
    CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::UpdatedLive3D;

    // Before a route has shown a captured-BG phase, keep live frames on
    // the current/native path. After that, use the same handoff compositor
    // for the route's stable live and captured phases to avoid path
    // alternation.
    if (!CaptureBackedRoute[handoffSlot].HasCapturedPhase)
    {
        RenderScreenCurrent(ystart, yend, true,
                            WholeSceneCurrentPathReason::CaptureBackedLiveBeforeCapturedPhase);
        return false;
    }

    background =
        MakeHandoffSnapshotBackgroundChoice(CaptureBackedRouteGL[handoffSlot].Handoff3DTex);
    return true;
}

void GLRenderer2D::RenderHandoffHybridComposite(const WholeSceneCaptureRequest& request,
                                                const HandoffBackgroundChoice& background,
                                                int ystart,
                                                int yend)
{
    const GLuint highResDirect3DTex =
        background.Tex ? background.Tex : Parent.OutputTex3D;

    GLuint nativeDirect3DTex = highResDirect3DTex;
    const bool highRes3D = ScaleFactor > 1;
    if (highRes3D)
    {
        nativeDirect3DTex = ResolveDirect3DToNative(highResDirect3DTex);
        WholeSceneTrace.Native3DResolveValid = true;
        WholeSceneTrace.Native3DSemanticsValid = true;
        WholeSceneTrace.Native3DSource = WholeSceneNative3DSource::HighResResolved;
    }
    else
        WholeSceneTrace.Native3DSource = WholeSceneNative3DSource::NativeRendered;

    const SourceABackgroundSource resolvedBackgroundSource =
        background.Tex ? background.Source : SourceABackgroundSource::ParentOutputTex3D;
    RecordHandoffHybridTrace(request,
                             resolvedBackgroundSource,
                             background.Authority,
                             highResDirect3DTex,
                             highRes3D,
                             background.BackgroundEpochSerial,
                             background.PresentationHash,
                             background.Tex != 0,
                             ystart,
                             yend);

    UpdateCompositorConfig();
    const auto savedCompositorConfig = CompositorConfig;

    // This path handles games that alternate between live BG0/3D and a
    // capture-backed bitmap copy of that same 3D scene.  Build a scaled UI
    // overlay from OBJ only, then composite it over the live high-res 3D.
    for (int layer = 0; layer < 4; layer++)
        CompositorConfig.uBGPrio[layer] = -1;

    // Replace capture-backed BGs with a synthetic BG0/3D underlay in the
    // native black/white endpoints. The actual background color comes from
    // highResDirect3DTex; the endpoints only need to reveal where current
    // OBJ/UI leaves that background visible.
    CompositorConfig.uEnable3D = !GPU2D.Num && (DispCnt & (1 << 3));
    if (CompositorConfig.uEnable3D)
        CompositorConfig.uBGPrio[0] = BGCnt[0] & 0x3;
    CompositorConfig.uEnableOBJ = !!(LayerEnable & (1 << 4));

    glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CompositorConfig), &CompositorConfig);

    EnsureWholeSceneOverlayEndpoints(nativeDirect3DTex);

    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeExactFinalTex, ystart, yend, false,
                                    NativeOverlayBlack3DTex, nativeDirect3DTex, true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayBlackExactFinal, ElapsedUS(phaseStart));
    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeOutputTex, ystart, yend, false,
                                    NativeOverlayWhite3DTex, nativeDirect3DTex, true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayWhiteExactFinal, ElapsedUS(phaseStart));
    WholeSceneTrace.NativeExactFinalValid = true;

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeFinalUpscaleToTexture(NativeExactFinalTex, UpscaledExactFinalTex);
    RenderNativeFinalUpscaleToTexture(NativeOutputTex, UpscaledCoverageTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderOverlayComposite(UpscaledExactFinalTex,
                           UpscaledCoverageTex,
                           highResDirect3DTex,
                           false,
                           0,
                           0,
                           0,
                           0,
                           false,
                           0,
                           0,
                           WholeSceneScaleDebugTint,
                           0,
                           ystart,
                           yend,
                           true);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));

    CompositorConfig = savedCompositorConfig;
    glBindBuffer(GL_UNIFORM_BUFFER, CompositorConfigUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CompositorConfig), &CompositorConfig);
}

void GLRenderer2D::PrepareFinalUpscaleNative3DInput(GLuint& direct3DTex,
                                                    GLuint& direct3DCoverageTex,
                                                    bool& highRes3D)
{
    direct3DTex = Parent.OutputTex3D;
    direct3DCoverageTex = 0;
    highRes3D = !WholeSceneScaleFinalUpscaleRender3DNative && ScaleFactor > 1;

    if (highRes3D && WholeSceneDebugPoison.Source3D)
        PoisonWholeSceneDebugTexture(Parent.OutputTex3D, ScreenW, ScreenH, false);

    if (highRes3D)
    {
        direct3DTex = ResolveDirect3DToNative();
        direct3DCoverageTex = NativeDirect3DTex;
        if (WholeSceneDebugPoison.Native3DResolve)
        {
            PoisonWholeSceneDebugTexture(NativeDirect3DTex, 256, 192, WholeSceneDebugPoison.Native3DResolveAlpha);
            PoisonWholeSceneDebugTexture(NativeDirect3DSemanticsTex, 256, 192, WholeSceneDebugPoison.Native3DResolveAlpha);
            PoisonWholeSceneDebugTexture(NativeDirect3DCompositorTex, 256, 192, WholeSceneDebugPoison.Native3DResolveAlpha);
        }
        WholeSceneTrace.Native3DResolveValid = true;
        WholeSceneTrace.Native3DSemanticsValid = true;
        WholeSceneTrace.Native3DSource = WholeSceneNative3DSource::HighResResolved;
    }
    else
        WholeSceneTrace.Native3DSource = WholeSceneNative3DSource::NativeRendered;
}

void GLRenderer2D::RenderScreenPhysicalFinalPostprocessNativeInput(int ystart, int yend)
{
    ResetWholeSceneRenderTrace();

    GLuint direct3DTex = 0;
    GLuint direct3DCoverageTex = 0;
    bool highRes3D = false;
    PrepareFinalUpscaleNative3DInput(direct3DTex, direct3DCoverageTex, highRes3D);

    RecordWholeSceneRenderTrace(WholeSceneRenderPath::PhysicalFinalPostprocessInput, ystart, yend,
                                highRes3D, false, highRes3D, direct3DTex);

    auto phaseStart = std::chrono::steady_clock::now();
    RenderCompositorPass(OutputFB, OBJLayerTex, ScreenW, ScreenH, ystart, yend, ScaleFactor,
                         WholeSceneHighResLayerFilterMode(),
                         WholeSceneHighResLayerFilterNoWrap(),
                         false,
                         false);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinalToTexture(NativeOutputTex, ystart, yend, WholeSceneScaleDebugTint,
                                    direct3DTex, direct3DCoverageTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativeExactFinal, ElapsedUS(phaseStart));
    WholeSceneTrace.PhysicalFinalNativeInputValid = true;
}

void GLRenderer2D::RenderScreenWholeSceneFinalUpscale(int ystart, int yend, bool hybridFragmentationFallback)
{
    ResetWholeSceneRenderTrace();

    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativePrepass(ystart, yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));

    GLuint direct3DTex = 0;
    GLuint direct3DCoverageTex = 0;
    bool highRes3D = false;
    PrepareFinalUpscaleNative3DInput(direct3DTex, direct3DCoverageTex, highRes3D);

    RecordWholeSceneRenderTrace(WholeSceneRenderPath::FinalNativeUpscale, ystart, yend,
                                highRes3D, false, highRes3D, direct3DTex,
                                hybridFragmentationFallback);

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeExactFinal(ystart, yend, WholeSceneScaleDebugTint, direct3DTex, direct3DCoverageTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativeExactFinal, ElapsedUS(phaseStart));
    WholeSceneTrace.NativeExactFinalValid = true;
    RecordWholeSceneNativeProductChunk(ystart, yend);
}

void GLRenderer2D::RenderScreenWholeSceneOverlayOperator(int ystart, int yend)
{
    ResetWholeSceneRenderTrace();
    const HybridSourceDecision sourceDecision = ChooseHybridSourceDecision();

    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativePrepass(ystart, yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));

    GLuint nativeDirect3DTex = Parent.OutputTex3D;
    GLuint highResDirect3DTex = Parent.OutputTex3D;
    const bool highRes3D = !WholeSceneScaleFinalUpscaleRender3DNative && ScaleFactor > 1;
    if (highRes3D && WholeSceneDebugPoison.Source3D)
        PoisonWholeSceneDebugTexture(Parent.OutputTex3D, ScreenW, ScreenH, false);

    if (highRes3D)
    {
        nativeDirect3DTex = ResolveDirect3DToNative();
        if (WholeSceneDebugPoison.Native3DResolve)
        {
            PoisonWholeSceneDebugTexture(NativeDirect3DTex, 256, 192, WholeSceneDebugPoison.Native3DResolveAlpha);
            PoisonWholeSceneDebugTexture(NativeDirect3DSemanticsTex, 256, 192, WholeSceneDebugPoison.Native3DResolveAlpha);
            PoisonWholeSceneDebugTexture(NativeDirect3DCompositorTex, 256, 192, WholeSceneDebugPoison.Native3DResolveAlpha);
        }
        WholeSceneTrace.Native3DResolveValid = true;
        WholeSceneTrace.Native3DSemanticsValid = true;
        WholeSceneTrace.Native3DSource = WholeSceneNative3DSource::HighResResolved;
    }
    else
        WholeSceneTrace.Native3DSource = WholeSceneNative3DSource::NativeRendered;

    if (sourceDecision.EffectiveConservativeHybrid &&
        WholeSceneScaleCaptureBacked &&
        sourceDecision.ActiveDirect3D)
    {
        CaptureBackedHandoffRouteKey handoffKey = BuildCaptureBackedHandoffRouteKey(ystart, yend);
        const int handoffSlot = CaptureBackedHandoffRouteSlot(handoffKey);
        const u32 visibleBGLayers = handoffKey.LayerEnable & 0x0Fu;
        const bool direct3DOnlyLiveBackground =
            visibleBGLayers == (1u << 0) &&
            (DispCnt & (1 << 3));

        if (handoffSlot >= 0 &&
            handoffSlot < kCaptureBackedHandoffRouteSlots &&
            direct3DOnlyLiveBackground &&
            IsStableCaptureBackedHandoffLiveUpdate(handoffKey))
        {
            CaptureBackedHandoff.CurrentKey = handoffKey;
            CaptureBackedHandoff.CurrentSlot = static_cast<u8>(handoffSlot);
            CaptureBackedHandoff.BackgroundUpdated = UpdateCaptureBackedHandoff3DSnapshot(highResDirect3DTex);
            if (CaptureBackedHandoff.BackgroundUpdated)
                CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::UpdatedLive3D;
        }
    }

    RecordWholeSceneRenderTrace(sourceDecision.Path, ystart, yend,
                                highRes3D, false, highRes3D, nativeDirect3DTex);

    if (sourceDecision.RenderNativeFallback)
    {
        phaseStart = std::chrono::steady_clock::now();
        RenderNativeExactFinal(ystart, yend);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativeExactFinal, ElapsedUS(phaseStart));
        WholeSceneTrace.NativeExactFinalValid = true;
    }

    RecordWholeSceneNativeProductChunk(ystart, yend);

    if (sourceDecision.EffectiveConservativeHybrid)
    {
        if (sourceDecision.RenderForeground2DBase)
        {
            auto phaseStart = std::chrono::steady_clock::now();
            glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, Hybrid2DBaseTex, 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            RenderCompositorPass(ArtCNNOutputFB, OBJLayerTex, ScreenW, ScreenH, ystart, yend, ScaleFactor,
                                 WholeSceneHighResLayerFilterMode(),
                                 WholeSceneHighResLayerFilterNoWrap(),
                                 false,
                                 false,
                                 BlankColorTex,
                                 BlankColorTex);
            AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.Hybrid2DBaseCandidate, ElapsedUS(phaseStart));
        }

        if (sourceDecision.RenderForegroundCandidate)
        {
            auto phaseStart = std::chrono::steady_clock::now();
            glBindFramebuffer(GL_FRAMEBUFFER, ArtCNNOutputFB);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, HybridForegroundTex, 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            RenderCompositorPass(ArtCNNOutputFB, OBJLayerTex, ScreenW, ScreenH, ystart, yend, ScaleFactor,
                                 WholeSceneHighResLayerFilterMode(),
                                 WholeSceneHighResLayerFilterNoWrap(),
                                 false,
                                 WholeSceneScaleDebugTint,
                                 highResDirect3DTex,
                                 0);
            AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.HybridForegroundCandidate, ElapsedUS(phaseStart));
        }
    }

}

void GLRenderer2D::RenderScreenWholeSceneCaptureBackedHandoff(int ystart, int yend)
{
    ResetWholeSceneRenderTrace();

    auto phaseStart = std::chrono::steady_clock::now();
    RenderNativePrepass(ystart, yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.NativePrepass, ElapsedUS(phaseStart));

    const int handoffSlot = BeginCaptureBackedHandoffRoute(ystart, yend);
    const WholeSceneCaptureRequest handoffRequestBase =
        ::melonDS::MakeHandoffConsumerCaptureRequest(ystart, yend, handoffSlot);

    HandoffBackgroundChoice handoffBackground = {};
    if (IsStableCaptureBackedHandoffLiveUpdate(CaptureBackedHandoff.CurrentKey))
    {
        if (!TryPrepareStableHandoffLiveBackground(handoffSlot,
                                                   handoffBackground,
                                                   ystart,
                                                   yend))
        {
            return;
        }
    }
    else if (CaptureBackedHandoff.CurrentKey.Phase == CaptureBackedHandoffPhase::Live3D)
    {
        // A live 3D phase with a bitmap upload is useful for the current frame,
        // but not safe as a reusable replacement for a captured BG phase.
        CaptureBackedHandoff.ReuseDecision = CaptureBackedHandoffReuseReason::RejectedUnstableLivePhase;
        RenderScreenWholeSceneCaptureBackedHybridFallback(ystart, yend);
        return;
    }
    else
    {
        const HandoffBackgroundResolveResult resolution =
            ResolveCapturedHandoffBackgroundChoice(handoffRequestBase, handoffSlot, ystart, yend);
        if (resolution.Finished)
            return;

        handoffBackground = resolution.Background;
    }

    if (TryRenderHandoffBackgroundOverlayProduct(handoffRequestBase,
                                                 handoffSlot,
                                                 handoffBackground,
                                                 ystart,
                                                 yend))
    {
        return;
    }

    RenderHandoffHybridComposite(handoffRequestBase, handoffBackground, ystart, yend);
}

void GLRenderer2D::RenderScreenWholeSceneFinalizeFinalUpscaleFullFrame()
{
    const auto phaseStart = std::chrono::steady_clock::now();
    RenderNativeFinalUpscale(NativeExactFinalTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));
    WholeSceneTrace.YStart = 0;
    WholeSceneTrace.YEnd = 192;
}

void GLRenderer2D::RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(bool conservativeHybrid,
                                                                          int ystart,
                                                                          int yend)
{
    const bool fullFrame = ystart == 0 && yend == 192;
    const GLuint highResDirect3DTex = Parent.OutputTex3D;
    GLuint nativeDirect3DTex = static_cast<GLuint>(WholeSceneTrace.NativeStage3D);
    if (nativeDirect3DTex == 0)
        nativeDirect3DTex = Parent.OutputTex3D;

    const bool useAccumulatedNativeHybridFallback =
        conservativeHybrid &&
        !(DispCnt & (1 << 3)) &&
        WholeSceneTrace.NativeExactFinalValid &&
        WholeSceneNativeChunkAccumulationPasses > 1;
    if (useAccumulatedNativeHybridFallback)
    {
        auto phaseStart = std::chrono::steady_clock::now();
        RenderNativeFinalUpscaleToTexture(NativeExactFinalTex, HybridNativeFallbackTex);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));
    }

    EnsureWholeSceneOverlayEndpoints(nativeDirect3DTex);

    const bool useCompositorExactEndpoints = ShouldUseCompositorExactOverlayEndpoints();
    const GLuint nativeDirect3DCoverageTex = WholeSceneTrace.Native3DResolveValid ? NativeDirect3DTex : 0;
    WholeSceneTrace.OverlayEndpointFinalMode = useCompositorExactEndpoints
        ? WholeSceneOverlayEndpointFinalMode::ExactCompositor
        : WholeSceneOverlayEndpointFinalMode::MetadataResolve;

    auto phaseStart = std::chrono::steady_clock::now();
    if (useCompositorExactEndpoints)
        RenderNativeExactFinalToTexture(NativeExactFinalTex, ystart, yend, false, NativeOverlayBlack3DTex, nativeDirect3DCoverageTex);
    else
        RenderNativeResolvedExactFinalToTexture(NativeExactFinalTex, NativeOverlayBlack3DTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayBlackExactFinal, ElapsedUS(phaseStart));
    phaseStart = std::chrono::steady_clock::now();
    if (useCompositorExactEndpoints)
        RenderNativeExactFinalToTexture(NativeOutputTex, ystart, yend, false, NativeOverlayWhite3DTex, nativeDirect3DCoverageTex);
    else
        RenderNativeResolvedExactFinalToTexture(NativeOutputTex, NativeOverlayWhite3DTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayWhiteExactFinal, ElapsedUS(phaseStart));
    phaseStart = std::chrono::steady_clock::now();
    if (useCompositorExactEndpoints)
        RenderNativeExactFinalToTexture(NativeOverlayTrueFinalTex, ystart, yend, false, nativeDirect3DTex, nativeDirect3DCoverageTex);
    else
        RenderNativeResolvedExactFinalToTexture(NativeOverlayTrueFinalTex, nativeDirect3DTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.OverlayTrueExactFinal, ElapsedUS(phaseStart));
    WholeSceneTrace.NativeExactFinalValid = true;
    WholeSceneTrace.OverlayTrueFinalValid = true;

    if (fullFrame && WholeSceneDebugViewsActive.load(std::memory_order_relaxed))
    {
        RenderOverlayDebugTexture(NativeOverlayReconstructedTex, 256, 192,
                                  NativeExactFinalTex, NativeOutputTex,
                                  nativeDirect3DTex, NativeOverlayTrueFinalTex, 1);
        RenderOverlayDebugTexture(NativeOverlayErrorTex, 256, 192,
                                  NativeExactFinalTex, NativeOutputTex,
                                  nativeDirect3DTex, NativeOverlayTrueFinalTex, 2);
        RenderOverlayDebugTexture(NativeOverlayConfidenceTex, 256, 192,
                                  NativeExactFinalTex, NativeOutputTex,
                                  nativeDirect3DTex, NativeOverlayTrueFinalTex, 3);
    }

    const bool useHybridLegacyCandidate =
        conservativeHybrid && CanUseWholeSceneHybridCleanLegacyCandidatePath();
    if (useHybridLegacyCandidate)
    {
        phaseStart = std::chrono::steady_clock::now();
        RenderNativeUpscale(ystart, yend);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.HybridLegacyCandidate, ElapsedUS(phaseStart));

        phaseStart = std::chrono::steady_clock::now();
        RenderNativeResolveToTexture(HybridLegacyCandidateTex,
                                     0,
                                     192,
                                     false,
                                     false,
                                     false);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.HybridLegacyCandidate, ElapsedUS(phaseStart));
    }

    phaseStart = std::chrono::steady_clock::now();
    RenderNativeFinalUpscaleToTexture(NativeExactFinalTex, UpscaledExactFinalTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));
    phaseStart = std::chrono::steady_clock::now();
    RenderNativeFinalUpscaleToTexture(NativeOutputTex, UpscaledCoverageTex);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));
    if (conservativeHybrid && !useAccumulatedNativeHybridFallback)
    {
        phaseStart = std::chrono::steady_clock::now();
        RenderNativeFinalUpscaleToTexture(NativeOverlayTrueFinalTex, HybridNativeFallbackTex);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerUpscale, ElapsedUS(phaseStart));
    }

    if (fullFrame && WholeSceneDebugViewsActive.load(std::memory_order_relaxed))
    {
        RenderOverlayDebugTexture(UpscaledOverlayUnderlayWeightTex, ScreenW, ScreenH,
                                  UpscaledExactFinalTex, UpscaledCoverageTex,
                                  highResDirect3DTex, NativeOverlayTrueFinalTex, 0);
        RenderOverlayDebugTexture(UpscaledOverlayOwnershipTex, ScreenW, ScreenH,
                                  UpscaledExactFinalTex, UpscaledCoverageTex,
                                  highResDirect3DTex, NativeOverlayTrueFinalTex, 4);
    }

    GLuint nativeRole3DTex = nativeDirect3DTex;
    if (WholeSceneTrace.Native3DSemanticsValid)
        nativeRole3DTex = NativeDirect3DSemanticsTex;
    phaseStart = std::chrono::steady_clock::now();
    RenderOverlayComposite(UpscaledExactFinalTex,
                           UpscaledCoverageTex,
                           highResDirect3DTex,
                           conservativeHybrid,
                           conservativeHybrid ? HybridForegroundTex : 0,
                           conservativeHybrid ? HybridNativeFallbackTex : 0,
                           conservativeHybrid ? Hybrid2DBaseTex : 0,
                           useHybridLegacyCandidate ? HybridLegacyCandidateTex : 0,
                           useHybridLegacyCandidate,
                           conservativeHybrid ? nativeRole3DTex : 0,
                           0,
                           false,
                           0,
                           ystart,
                           yend);
    AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));
    if (fullFrame && conservativeHybrid && WholeSceneDebugViewsActive.load(std::memory_order_relaxed))
    {
        phaseStart = std::chrono::steady_clock::now();
        RenderOverlayComposite(UpscaledExactFinalTex,
                               UpscaledCoverageTex,
                               highResDirect3DTex,
                               true,
                               HybridForegroundTex,
                               HybridNativeFallbackTex,
                               Hybrid2DBaseTex,
                               useHybridLegacyCandidate ? HybridLegacyCandidateTex : 0,
                               useHybridLegacyCandidate,
                               nativeRole3DTex,
                               HybridSelectorTex,
                               true,
                               0,
                               0,
                               192);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));
        phaseStart = std::chrono::steady_clock::now();
        RenderOverlayComposite(UpscaledExactFinalTex,
                               UpscaledCoverageTex,
                               highResDirect3DTex,
                               true,
                               HybridForegroundTex,
                               HybridNativeFallbackTex,
                               Hybrid2DBaseTex,
                               useHybridLegacyCandidate ? HybridLegacyCandidateTex : 0,
                               useHybridLegacyCandidate,
                               nativeRole3DTex,
                               HybridCoverageMissTex,
                               true,
                               1,
                               0,
                               192);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));
        phaseStart = std::chrono::steady_clock::now();
        RenderOverlayComposite(UpscaledExactFinalTex,
                               UpscaledCoverageTex,
                               highResDirect3DTex,
                               true,
                               HybridForegroundTex,
                               HybridNativeFallbackTex,
                               Hybrid2DBaseTex,
                               useHybridLegacyCandidate ? HybridLegacyCandidateTex : 0,
                               useHybridLegacyCandidate,
                               nativeRole3DTex,
                               HybridForegroundAlphaTex,
                               true,
                               2,
                               0,
                               192);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));
        phaseStart = std::chrono::steady_clock::now();
        RenderOverlayComposite(UpscaledExactFinalTex,
                               UpscaledCoverageTex,
                               highResDirect3DTex,
                               true,
                               HybridForegroundTex,
                               HybridNativeFallbackTex,
                               Hybrid2DBaseTex,
                               useHybridLegacyCandidate ? HybridLegacyCandidateTex : 0,
                               useHybridLegacyCandidate,
                               nativeRole3DTex,
                               HybridFinalSourceTex,
                               true,
                               3,
                               0,
                               192);
        AddWholeSceneUpdateTiming(WholeSceneUpdateTiming.FinalizerComposite, ElapsedUS(phaseStart));
    }

    WholeSceneTrace.YStart = ystart;
    WholeSceneTrace.YEnd = yend;
}

void GLRenderer2D::RenderScreenWholeSceneFinalizeFullFrame()
{
    const auto start = std::chrono::steady_clock::now();

    WholeSceneFullFrameFinalizerPasses++;
    WholeSceneNativeProductsFrameComplete = AreWholeSceneNativeProductsComplete();
    UpdateWholeSceneTraceFrameSplitState();

    if (!WholeSceneNativeProductFinalizerPathSeen)
        return;

    const bool allowCaptureBackedCadenceFinalize =
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale &&
        IsWholeSceneCaptureBackedHandoffGuardActive() &&
        WholeSceneTrace.Path == WholeSceneRenderPath::ConservativeHybridUpscale &&
        WholeSceneNativeProductsFrameComplete &&
        !WholeSceneFullFrameFinalizerUnsafeFrame;

    if (!CanFinalizeWholeSceneNativeProducts() && !allowCaptureBackedCadenceFinalize)
    {
        const WholeSceneCaptureBackedPlan capturePlan =
            ChooseWholeSceneCaptureBackedPlan(LastLine, 192);
        if (capturePlan.Stage == WholeSceneCaptureBackedPlanStage::AfterGeneralFallbacks)
            RenderScreenWholeSceneCaptureBackedPlan(capturePlan, LastLine, 192);
        else
            RenderScreenCurrent(LastLine, 192, true,
                                WholeSceneCurrentPathReason::NativeProductFinalizerIncomplete);

        const auto end = std::chrono::steady_clock::now();
        WholeSceneTrace.RenderTimeUS +=
            static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        UpdateWholeSceneTraceFrameSplitState();
        return;
    }

    switch (WholeSceneTrace.Path)
    {
    case WholeSceneRenderPath::FinalNativeUpscale:
        RenderScreenWholeSceneFinalizeFinalUpscaleFullFrame();
        break;
    case WholeSceneRenderPath::OverlayOperatorUpscale:
        RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(false);
        break;
    case WholeSceneRenderPath::ConservativeHybridUpscale:
        RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(true);
        break;
    default:
        break;
    }

    const auto end = std::chrono::steady_clock::now();
    WholeSceneTrace.RenderTimeUS +=
        static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    UpdateWholeSceneTraceFrameSplitState();
}

void GLRenderer2D::RenderScreenWholeScene(int ystart, int yend)
{
    const WholeSceneScaleDecision decision = ChooseWholeSceneScaleDecision(ystart, yend);
    switch (decision.Reason)
    {
    case WholeSceneScaleDecisionReason::HighResCompositor:
        RenderScreenWholeSceneHighRes(ystart, yend);
        break;
    case WholeSceneScaleDecisionReason::SourceACaptureOnlyReplacement:
        RenderScreenWholeSceneSourceACaptureReplacement(ystart, yend);
        break;
    case WholeSceneScaleDecisionReason::CaptureBackedBeforeGeneralFallbacks:
        RenderScreenWholeSceneCaptureBackedPlan(decision.CapturePlan, ystart, yend);
        break;
    case WholeSceneScaleDecisionReason::HybridFragmentationFinalUpscale:
        RenderScreenWholeSceneFinalUpscale(ystart, yend, true);
        break;
    case WholeSceneScaleDecisionReason::OverlayOperator:
        RenderScreenWholeSceneOverlayOperator(ystart, yend);
        break;
    case WholeSceneScaleDecisionReason::FinalNativeUpscale:
        RenderScreenWholeSceneFinalUpscale(ystart, yend);
        break;
    case WholeSceneScaleDecisionReason::LegacyNativeUpscale:
        RenderScreenWholeSceneLegacy(ystart, yend);
        break;
    case WholeSceneScaleDecisionReason::None:
        break;
    }
}

WholeSceneScaleDecision GLRenderer2D::ChooseWholeSceneScaleDecision(int ystart, int yend) const
{
    WholeSceneScaleDecision decision = {};

    if (CanUseWholeSceneHighResPath())
    {
        decision.Reason = WholeSceneScaleDecisionReason::HighResCompositor;
        decision.Path = WholeSceneRenderPath::HighResCompositor;
        return decision;
    }

    if (CanUseWholeSceneScalePath() &&
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale &&
        CanUseWholeSceneMixedSourceACaptureBGPath())
    {
        decision.Reason = WholeSceneScaleDecisionReason::SourceACaptureOnlyReplacement;
        decision.Path = WholeSceneRenderPath::SourceACaptureReplacement;
        return decision;
    }

    if (CanUseWholeSceneScalePath() &&
        CanUseWholeSceneMixedCaptureBackedOBJOverlayPath())
    {
        decision.Reason = WholeSceneScaleDecisionReason::SourceACaptureOnlyReplacement;
        decision.Path = WholeSceneRenderPath::SourceACaptureReplacement;
        return decision;
    }

    if (CanUseWholeSceneScalePath() &&
        CanUseWholeSceneCaptureOnlyHighResPath(ystart, yend))
    {
        decision.Reason = WholeSceneScaleDecisionReason::SourceACaptureOnlyReplacement;
        decision.Path = WholeSceneRenderPath::SourceACaptureReplacement;
        return decision;
    }

    if (CanUseWholeSceneOverlayOperatorPath())
    {
        WholeSceneOverlayScaleDecisionInputs inputs = {};
        inputs.CapturePlan = ChooseWholeSceneCaptureBackedPlan(ystart, yend);
        inputs.ConservativeHybridMode =
            WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
        inputs.HybridFragmentationGuardActive =
            inputs.ConservativeHybridMode && IsWholeSceneHybridFragmentationGuardActive();
        return ::melonDS::ChooseWholeSceneOverlayScaleDecision(inputs);
    }

    if (CanUseWholeSceneFinalUpscalePath())
    {
        decision.Reason = WholeSceneScaleDecisionReason::FinalNativeUpscale;
        decision.Path = WholeSceneRenderPath::FinalNativeUpscale;
        return decision;
    }

    decision.Reason = WholeSceneScaleDecisionReason::LegacyNativeUpscale;
    decision.Path = WholeSceneRenderPath::LegacyNativeUpscale;
    return decision;
}

GLRenderer2D::WholeSceneCaptureBackedPlan GLRenderer2D::ChooseWholeSceneCaptureBackedPlan(int ystart, int yend) const
{
    int captureEpochOverlayRouteSlot = -1;
    const bool captureEpochOverlayAvailable =
        CanUseCaptureEpochBackgroundForLiveOverlay(ystart, yend, captureEpochOverlayRouteSlot);

    if (!captureEpochOverlayAvailable &&
        CanUseWholeSceneScalePath() &&
        CanUseWholeSceneOverlayOperatorPath() &&
        ShouldUseWholeSceneCaptureBackedHandoffForRange(ystart, yend))
    {
        return MakeWholeSceneCaptureBackedHandoffPlan();
    }

    if (WholeSceneScaleRequested &&
        (WholeSceneScaleState == WholeSceneScaleEligibility::CaptureBackedBG ||
         WholeSceneScaleState == WholeSceneScaleEligibility::CaptureBackedOBJ) &&
        CanUseWholeSceneCaptureOnlyHighResPath(ystart, yend))
    {
        WholeSceneCaptureBackedPlan plan = MakeWholeSceneSourceACaptureReplacementPlan();
        if (CanUseSourceABackgroundCurrentOverlayPath() ||
            CanUseSourceAExactFullProductBridgePath(ystart, yend))
        {
            plan.Stage = WholeSceneCaptureBackedPlanStage::BeforeGeneralFallbacks;
        }
        return plan;
    }

    if (captureEpochOverlayAvailable)
    {
        return MakeWholeSceneCaptureEpochOverlayPlan(captureEpochOverlayRouteSlot);
    }

    return {};
}

void GLRenderer2D::RenderScreenWholeSceneCaptureBackedPlan(const WholeSceneCaptureBackedPlan& plan, int ystart, int yend)
{
    switch (plan.Kind)
    {
    case WholeSceneCaptureBackedPlanKind::CaptureBackedHandoff:
        RenderScreenWholeSceneCaptureBackedHandoff(ystart, yend);
        break;
    case WholeSceneCaptureBackedPlanKind::SourceACaptureReplacement:
        RenderScreenWholeSceneSourceACaptureReplacement(ystart, yend);
        break;
    case WholeSceneCaptureBackedPlanKind::CaptureEpochOverlay:
        RenderScreenWholeSceneCaptureEpochOverlay(ystart, yend);
        break;
    case WholeSceneCaptureBackedPlanKind::None:
        break;
    }
}

WholeScenePathDecision GLRenderer2D::ChooseWholeScenePathDecision(int ystart, int yend) const
{
    WholeScenePathDecisionInputs inputs = {};
    inputs.CapturePlan = ChooseWholeSceneCaptureBackedPlan(ystart, yend);
    inputs.CanUseScalePath = CanUseWholeSceneScalePath();
    inputs.ChunkedUnsafeOverlayAvailable =
        inputs.CanUseScalePath &&
        WholeSceneFullFrameFinalizerUnsafeFrame &&
        WholeSceneScaleFragmentationFallback == RendererSettings::WholeScene2DFragmentationFallback::Off &&
        CanUseWholeSceneOverlayOperatorPath() &&
        !IsWholeSceneCaptureBackedHandoffGuardActive();
    inputs.CurrentFallbackAvailable =
        inputs.CanUseScalePath &&
        (WholeSceneFullFrameFinalizerUnsafeFrame ||
         IsWholeSceneCurrentFragmentationGuardActive());
    inputs.PhysicalFinalPostprocessNativeInputAvailable =
        CanUsePhysicalFinalPostprocessNativeInputPath();
    inputs.HybridPresentationGuardActive =
        IsWholeSceneHybridPresentationGuardActive(ystart, yend);
    inputs.SplitLegacyFallbackAvailable = CanUseWholeSceneSplitLegacyFallbackPath();
    inputs.ConservativeHybridMode =
        WholeSceneScaleMode == RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;

    return ::melonDS::ChooseWholeScenePathDecision(inputs);
}

void GLRenderer2D::RenderScreen(int ystart, int yend)
{
    const auto start = std::chrono::steady_clock::now();

    const WholeScenePathDecision decision = ChooseWholeScenePathDecision(ystart, yend);
    switch (decision.Reason)
    {
    case WholeScenePathDecisionReason::CaptureBackedProducerDuringHybridGuard:
    case WholeScenePathDecisionReason::CaptureBackedBeforeGeneralFallbacks:
    case WholeScenePathDecisionReason::CaptureBackedAfterGeneralFallbacks:
        RenderScreenWholeSceneCaptureBackedPlan(decision.CapturePlan, ystart, yend);
        break;
    case WholeScenePathDecisionReason::HybridPresentationGuard:
        RenderScreenCurrent(0, 192, false, decision.CurrentReason);
        break;
    case WholeScenePathDecisionReason::ChunkedUnsafeOverlay:
        RenderScreenWholeSceneOverlayOperator(ystart, yend);
        RenderScreenWholeSceneFinalizeOverlayOperatorFullFrame(decision.ConservativeHybrid,
                                                              ystart,
                                                              yend);
        break;
    case WholeScenePathDecisionReason::PhysicalFinalPostprocessNativeInput:
        RenderScreenPhysicalFinalPostprocessNativeInput(ystart, yend);
        break;
    case WholeScenePathDecisionReason::SplitLegacyFallback:
        RenderScreenWholeSceneLegacy(ystart, yend);
        break;
    case WholeScenePathDecisionReason::FragmentationOrUnsafeFrameCurrentFallback:
        RenderScreenCurrent(ystart, yend, true, decision.CurrentReason);
        break;
    case WholeScenePathDecisionReason::WholeSceneScale:
        RenderScreenWholeScene(ystart, yend);
        break;
    case WholeScenePathDecisionReason::ScalePathUnavailable:
        RenderScreenCurrent(ystart, yend, false, decision.CurrentReason);
        break;
    case WholeScenePathDecisionReason::None:
        break;
    }

    const auto end = std::chrono::steady_clock::now();
    WholeSceneTrace.RenderTimeUS =
        static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

void GLRenderer2D::DrawSprites(u32 line)
{
    u32 oammask = 1 << GPU2D.Num;
    bool dirty = false;
    bool screenon = IsScreenOn();

    SpriteScanlineConfig.uMosaicLine[line] = GPU2D.OBJMosaicLine;

    u32 dispcnt_diff = GPU2D.DispCnt ^ SpriteDispCnt;
    SpriteDispCnt = GPU2D.DispCnt; // TODO CHECKME might not be right to do it here
    if (dispcnt_diff & 0x80F000F0)
        dirty = true;

    static_assert(VRAMDirtyGranularity == 512);
    NonStupidBitField<512> objDirty;

    if (screenon)
    {
        if (GPU2D.Num == 0)
        {
            objDirty = GPU.VRAMDirty_AOBJ.DeriveState(GPU.VRAMMap_AOBJ, GPU);
            GPU.MakeVRAMFlat_AOBJCoherent(objDirty);
        }
        else
        {
            auto _objDirty = GPU.VRAMDirty_BOBJ.DeriveState(GPU.VRAMMap_BOBJ, GPU);
            GPU.MakeVRAMFlat_BOBJCoherent(_objDirty);
            memcpy(objDirty.Data, _objDirty.Data, 256>>3);
        }
    }

    u8* vram; u32 vrammask;
    GPU2D.GetOBJVRAM(vram, vrammask);

    glBindTexture(GL_TEXTURE_2D, VRAMTex_OBJ);

    int texlen = (GPU2D.Num ? 256 : 512) >> 6;
    for (int i = 0; i < texlen; )
    {
        if (!objDirty.Data[i])
        {
            i++;
            continue;
        }

        int start = i * 32;
        for (;;)
        {
            i++;
            if (i >= texlen) break;
            if (!objDirty.Data[i]) break;
        }
        int end = i * 32;

        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, start,
                        1024, end - start,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                        &vram[start * 1024]);
        dirty = true;
    }

    if ((GPU.OAMDirty & oammask) || SpriteConfigDirty)
    {
        memcpy(OAM, &GPU.OAM[GPU2D.Num ? 0x400 : 0], 0x400);
        GPU.OAMDirty &= ~oammask;
        SpriteConfigDirty = false;
        dirty = true;
    }

    // DrawScanline() for the next scanline will be called after this
    // so it will be able to do the actual sprite rendering
    if (dirty)
        SpriteDirty = true;
}

}
