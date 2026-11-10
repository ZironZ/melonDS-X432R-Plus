// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/glad/glad.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include "NDS.h"
#include "GPU_OpenGL.h"
#include "GPU_Soft.h"
#include "WideMelon.h"
#include "WideCaptureLayout.h"

namespace
{
void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

// Intercept the real GL entry points, rather than mocking renderer behavior.
// Each namespace has its own live-name set. Zero is harmless; deleting any
// other untracked name is an error, including a duplicate or borrowed name.
struct Names
{
    const char* Label;
    std::set<GLuint> Live;
    size_t Created = 0;
    void Add(GLuint id) { if (id) { Check(Live.insert(id).second, "duplicate live GL allocation"); ++Created; } }
    void Remove(GLuint id)
    {
        if (id && !Live.erase(id))
            throw std::runtime_error(std::string("invalid/double deletion: ") + Label + " " + std::to_string(id));
    }
};
Names Textures{"texture"}, Buffers{"buffer"}, Framebuffers{"framebuffer"},
      VertexArrays{"vertex array"}, Samplers{"sampler"}, Programs{"program"}, Shaders{"shader"};

#define TRACK_ARRAY(Suffix, State) \
    auto RealGen##Suffix = glad_glGen##Suffix; \
    auto RealDelete##Suffix = glad_glDelete##Suffix; \
    void APIENTRY Gen##Suffix(GLsizei count, GLuint* ids) \
    { RealGen##Suffix(count, ids); for (int i = 0; i < count; ++i) State.Add(ids[i]); } \
    void APIENTRY Delete##Suffix(GLsizei count, const GLuint* ids) \
    { for (int i = 0; i < count; ++i) State.Remove(ids[i]); RealDelete##Suffix(count, ids); }
TRACK_ARRAY(Textures, Textures)
TRACK_ARRAY(Buffers, Buffers)
TRACK_ARRAY(Framebuffers, Framebuffers)
TRACK_ARRAY(VertexArrays, VertexArrays)
TRACK_ARRAY(Samplers, Samplers)
#undef TRACK_ARRAY

auto RealCreateShader = glad_glCreateShader;
auto RealDeleteShader = glad_glDeleteShader;
auto RealCreateProgram = glad_glCreateProgram;
auto RealDeleteProgram = glad_glDeleteProgram;
auto RealCompileShader = glad_glCompileShader;
auto RealLinkProgram = glad_glLinkProgram;
int FailCompileAt = 0, CompileCalls = 0;
bool FailLink = false, FailCreateShader = false, FailCreateProgram = false;
auto RealDrawArrays = glad_glDrawArrays;
size_t DrawArraysCalls = 0;
void APIENTRY DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    ++DrawArraysCalls;
    RealDrawArrays(mode, first, count);
}

auto RealTexSubImage2D = glad_glTexSubImage2D;
struct UploadRectangle { GLint Y; GLsizei Width, Height; };
bool CollectVRAMUploads = false;
std::vector<UploadRectangle> VRAMUploads;
void APIENTRY TexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
                           GLsizei width, GLsizei height, GLenum format,
                           GLenum type, const void* data)
{
    if (CollectVRAMUploads)
    {
        Check(target == GL_TEXTURE_2D && format == GL_RED_INTEGER && type == GL_UNSIGNED_BYTE,
              "BG upload format");
        VRAMUploads.push_back({y,width,height});
    }
    RealTexSubImage2D(target,level,x,y,width,height,format,type,data);
}

auto RealTexImage2D = glad_glTexImage2D;
bool FailScratchAllocation = false;
size_t TextureStorageCalls = 0;
auto RealMapBufferRange = glad_glMapBufferRange;
auto RealUnmapBuffer = glad_glUnmapBuffer;
bool FailUniformMap = false, FailUniformUnmap = false;
size_t UniformUnmaps = 0;
GLbitfield UniformMapFlags = 0;
void* APIENTRY MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr size, GLbitfield flags)
{
    UniformMapFlags = flags;
    return FailUniformMap ? nullptr : RealMapBufferRange(target, offset, size, flags);
}
GLboolean APIENTRY UnmapBuffer(GLenum target)
{
    ++UniformUnmaps;
    const auto result = RealUnmapBuffer(target);
    return FailUniformUnmap ? GL_FALSE : result;
}
void APIENTRY TexImage2D(GLenum target, GLint level, GLint format, GLsizei width,
                        GLsizei height, GLint border, GLenum inputFormat,
                        GLenum type, const void* data)
{
    ++TextureStorageCalls;
    // Produce a real GL allocation error, then verify production cleans up.
    RealTexImage2D(target, level, format, FailScratchAllocation ? -1 : width,
                   height, border, inputFormat, type, data);
}

GLuint APIENTRY CreateShader(GLenum type)
{
    if (FailCreateShader) return 0;
    GLuint id = RealCreateShader(type); Shaders.Add(id); return id;
}
void APIENTRY DeleteShader(GLuint id) { Shaders.Remove(id); RealDeleteShader(id); }
GLuint APIENTRY CreateProgram()
{
    if (FailCreateProgram) return 0;
    GLuint id = RealCreateProgram(); Programs.Add(id); return id;
}
void APIENTRY DeleteProgram(GLuint id) { Programs.Remove(id); RealDeleteProgram(id); }
void APIENTRY CompileShader(GLuint id)
{
    if (++CompileCalls == FailCompileAt)
    {
        const char* invalid = "#version 430 core\n deliberate compilation failure";
        glShaderSource(id, 1, &invalid, nullptr);
    }
    RealCompileShader(id);
}
void APIENTRY LinkProgram(GLuint id)
{
    if (FailLink)
    {
        // Linking a program without attached shaders must fail.
        GLuint attached[8]{};
        GLsizei count = 0;
        glGetAttachedShaders(id, 8, &count, attached);
        for (int i = 0; i < count; ++i) glDetachShader(id, attached[i]);
        RealLinkProgram(id);
        // The helper will detach its inputs after linking.
        for (int i = 0; i < count; ++i) glAttachShader(id, attached[i]);
    }
    else RealLinkProgram(id);
}

void InstallHooks()
{
#define HOOK(Name) Real##Name = glad_gl##Name; glad_gl##Name = Name
    HOOK(GenTextures); HOOK(DeleteTextures);
    HOOK(GenBuffers); HOOK(DeleteBuffers);
    HOOK(GenFramebuffers); HOOK(DeleteFramebuffers);
    HOOK(GenVertexArrays); HOOK(DeleteVertexArrays);
    HOOK(GenSamplers); HOOK(DeleteSamplers);
    HOOK(CreateShader); HOOK(DeleteShader);
    HOOK(CreateProgram); HOOK(DeleteProgram);
    HOOK(CompileShader); HOOK(LinkProgram);
    HOOK(TexImage2D); HOOK(MapBufferRange); HOOK(UnmapBuffer); HOOK(DrawArrays); HOOK(TexSubImage2D);
#undef HOOK
}

void CheckReleased(const char* phase)
{
    glUseProgram(0); // A deleted current program remains alive until unbound.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glFinish();
    for (auto* state : {&Textures, &Buffers, &Framebuffers, &VertexArrays, &Samplers, &Programs, &Shaders})
        if (!state->Live.empty())
            throw std::runtime_error(std::string(phase) + ": leaked " + std::to_string(state->Live.size()) + " " + state->Label);
    Check(glGetError() == GL_NO_ERROR, "GL error during lifetime test");
    std::printf("[GL lifetime] %s: all resources released\n", phase);
    std::fflush(stdout);
}

template<class T, class... Args>
void PoisonedConstruction(Args&&... args)
{
    void* storage = ::operator new(sizeof(T));
    std::memset(storage, 0xA5, sizeof(T));
    T* value = new (storage) T(std::forward<Args>(args)...);
    value->~T();
    ::operator delete(storage);
}

void TestShaderHelpers()
{
    const std::string vs = "#version 430 core\nvoid main(){gl_Position=vec4(0);}";
    const std::string fs = "#version 430 core\nout vec4 color; void main(){color=vec4(1);}";
    const std::string cs = "#version 430 core\nlayout(local_size_x=1) in; void main(){}";
    auto createProgram = glad_glCreateProgram;
    glad_glCreateProgram = nullptr;
    GLuint missing = 0xA5A5A5A5;
    Check(!melonDS::OpenGL::CompileComputeProgram(missing, cs, "unloaded") && missing == 0,
          "compute helper without loaded entry points");
    missing = 0xA5A5A5A5;
    Check(!melonDS::OpenGL::CompileVertexFragmentProgram(missing, vs, fs, "unloaded", {}, {}) && missing == 0,
          "graphics helper without loaded entry points");
    glad_glCreateProgram = createProgram;
    for (bool compute : {false, true})
    {
        for (int failure = 0; failure < 6; ++failure)
        {
            CompileCalls = 0;
            FailCompileAt = failure < 2 ? failure + 1 : 0;
            FailLink = failure == 2;
            FailCreateShader = failure == 3;
            FailCreateProgram = failure == 4;
            if (compute && failure == 1) continue;
            GLuint result = 0xA5A5A5A5;
            bool ok = compute
                ? melonDS::OpenGL::CompileComputeProgram(result, cs, "lifetime compute")
                : melonDS::OpenGL::CompileVertexFragmentProgram(result, vs, fs, "lifetime graphics", {}, {{"color", 0}});
            Check(ok == (failure == 5), "shader helper success/failure result");
            if (ok) { Check(glIsProgram(result), "successful program is live"); glDeleteProgram(result); }
            else Check(result == 0, "failed helper returned a stale program name");
            FailCompileAt = 0; FailLink = FailCreateShader = FailCreateProgram = false;
            CheckReleased("shader helper");
        }
    }
}
}

namespace melonDS
{
// The test reaches private scaler entry points without exposing them as a
// frontend API. It exercises production lazy publication and per-engine scratch.
struct GLRendererLifetimeTestAccess
{
    static void BitmapRowReference(GLRenderer& renderer, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        auto& gpu = engine.GPU;
        auto& regs = engine.GPU2D;
        const auto oldMode = engine.WholeSceneScaleMode;
        const auto oldAlgorithm = engine.WholeSceneScaleAlgorithm;
        const bool oldRequested = engine.WholeSceneScaleRequested;
        const bool oldPhysical = renderer.PhysicalFinalUpscale;
        const u32 bank = engineB ? 2 : 0;
        auto* mapping = engineB ? gpu.VRAMMap_BBG : gpu.VRAMMap_ABG;
        const size_t mappingCount = engineB ? std::size(gpu.VRAMMap_BBG) : std::size(gpu.VRAMMap_ABG);
        std::vector<u32> savedMapping(mapping, mapping + mappingCount);
        std::fill(mapping, mapping + mappingCount, 1u << bank);
        std::vector<u16> image(256 * 192);
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x)
                image[y * 256 + x] = 0x8000 | ((x ^ y) & 31) |
                    (((x / 3 + y) & 31) << 5) | (((x + y / 7) & 31) << 10);
        // A stationary text overlay with alternating opaque/transparent pixels.
        // Its map shares VRAM with the bitmap but lies outside the streamed row.
        std::fill(image.begin() + 16384, image.begin() + 17408, 0x8000);
        const auto render = [&](bool stream, int mode, RendererSettings::GLScaleAlgorithm algorithm) {
            renderer.Reset();
            engine.SetWholeSceneScaleMode(static_cast<RendererSettings::WholeScene2DScaleMode>(std::max(mode, 0)));
            engine.SetWholeSceneScaleAlgorithm(algorithm);
            engine.WholeSceneScaleRequested = mode >= 0;
            renderer.PhysicalFinalUpscale = mode == 2;
            gpu.ScreensEnabled = true;
            gpu.CaptureEnable = false;
            gpu.MasterBrightnessA = gpu.MasterBrightnessB = 0;
            gpu.PaletteDirty = 0xF;
            regs.Enabled = true;
            regs.ForcedBlank = 0;
            regs.DispCnt = (1u << 16) | (1u << 10) | (1u << 8) | 5;
            regs.LayerEnable = (1u << 2) | 1;
            regs.OBJEnable = false;
            regs.BlendCnt = 0;
            regs.BGCnt[0] = (6u << 2) | (16u << 8);
            regs.BGXPos[0] = regs.BGYPos[0] = 0;
            regs.BGCnt[2] = (1u << 7) | (1u << 2) | (1u << 14);
            regs.BGRotA[0] = 256;
            regs.BGRotB[0] = regs.BGRotC[0] = 0;
            regs.BGRotD[0] = stream ? 1 : 256;
            std::memcpy(gpu.VRAM[bank], image.data(), image.size() * sizeof(u16));
            std::memset(gpu.VRAM[bank] + 98304, 0x01, 32);
            reinterpret_cast<u16*>(gpu.Palette)[(engineB ? 512 : 0) + 129] = 31;
            gpu.VRAMDirty[bank] = NonStupidBitField<256>(0, 256);
            for (int y = 0; y < 192; ++y)
            {
                if (stream)
                {
                    std::memcpy(gpu.VRAM[bank], image.data() + y * 256, 512);
                    gpu.VRAMDirty[bank][0] = true;
                }
                regs.BGXRefInternal[0] = 0;
                regs.BGYRefInternal[0] = stream ? y : y * 256;
                engine.DrawSprites(y);
                engine.DrawScanline(y);
            }
            engine.VBlank();
            if (stream && !engine.BitmapRowDeferred)
                throw std::runtime_error("row stream not staged: engine " + std::to_string(engineB) +
                    " mode " + std::to_string(mode) + " type " + std::to_string(engine.LayerConfig.uBGConfig[2].Type) +
                    " source " + std::to_string(engine.BitmapRowSource(2, 0)) +
                    " path " + std::to_string(static_cast<int>(engine.WholeSceneTrace.Path)));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, engine.OutputFB);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            std::vector<u8> pixels(engine.ScreenW * engine.ScreenH * 4);
            glReadPixels(0, 0, engine.ScreenW, engine.ScreenH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        for (int mode = -1; mode < 5; ++mode)
            for (const auto algorithm : {RendererSettings::GLScaleAlgorithm::Spline36,
                                        RendererSettings::GLScaleAlgorithm::ArtCNNDN,
                                        RendererSettings::GLScaleAlgorithm::XBRZ,
                                        RendererSettings::GLScaleAlgorithm::ArtCNN,
                                        RendererSettings::GLScaleAlgorithm::NNEDI3,
                                        RendererSettings::GLScaleAlgorithm::CuNNy4x32})
            {
                // Source-space Spline36 requires neighbouring original rows:
                // its ordinary fallback cannot equal a contiguous bitmap.
                if (mode == 1 && algorithm == RendererSettings::GLScaleAlgorithm::Spline36) continue;
                const auto reference = render(false, mode, algorithm);
                const auto staged = render(true, mode, algorithm);
                if (reference != staged)
                    throw std::runtime_error("bitmap row reference differs: engine " + std::to_string(engineB) +
                        " mode " + std::to_string(mode) + " algorithm " + std::to_string(static_cast<int>(algorithm)));
            }
        const int oldScale = renderer.ScaleFactor;
        renderer.SetScaleFactor(1);
        const auto nativeReference = render(false, -1, RendererSettings::GLScaleAlgorithm::Spline36);
        Check(nativeReference == render(true, -1, RendererSettings::GLScaleAlgorithm::Spline36),
              "native resolution row staging preserves exact pixels");
        renderer.SetScaleFactor(oldScale);
        // An owned capture must keep its high-resolution detail without
        // enhancing native 2D text, or changing canonical native products.
        for (int mode : {-1, 2})
        {
            const auto ordinary = render(true, mode, RendererSettings::GLScaleAlgorithm::Spline36);
            std::vector<u8> source(engine.ScreenW * engine.ScreenH * 4);
            for (int y = 0; y < engine.ScreenH; ++y)
                for (int x = 0; x < engine.ScreenW; ++x)
                {
                    auto* pixel = source.data() + (y * engine.ScreenW + x) * 4;
                    pixel[0] = 0;pixel[1] = (x & 1) ? 255 : 0;pixel[2] = (x & 1) ? 0 : 255;pixel[3] = 255;
                }
            glBindTexture(GL_TEXTURE_2D, renderer.HighResDisplayCaptureFullTex[0]);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,engine.ScreenW,engine.ScreenH,GL_RGBA,GL_UNSIGNED_BYTE,source.data());
            const auto slot = renderer.CopiedCaptures.Store(0x18000,reinterpret_cast<const u8*>(image.data()));
            Check(renderer.RetainCopiedCaptureProduct(slot,0), "retain capture for presentation-domain test");
            engine.CopiedBitmapProof = {};
            for (u32 y = 0; y < 192; ++y)
                engine.CopiedBitmapProof.Observe(y, CaptureCopyTracker::Row{slot,renderer.CopiedCaptures.Get(slot).Generation,y});
            engine.CopiedBitmapLayer = 2;
            engine.CopiedBitmapPresentation = engine.CopiedBitmapPresentationState();
            std::copy(std::begin(engine.BGLayerArtworkGeneration),std::end(engine.BGLayerArtworkGeneration),engine.CopiedBitmapOtherSources);
            const auto readNative = [&] {
                glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.NativeOutputFB);glReadBuffer(GL_COLOR_ATTACHMENT0);
                std::vector<u8> pixels(256*192*4);
                glReadPixels(0,0,256,192,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
            };
            const auto canonical = readNative();
            Check(engine.RenderCopiedCaptureOverlay(), "copied bitmap can use the selected presentation domain");
            Check(readNative()==canonical, "copied presentation never overwrites canonical native pixels");
            Check(engine.CopiedBitmapNativePresentationValid == (mode == 2), "physical postprocessing alone uses private native input");
            if (mode == -1)
            {
                glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.OutputFB);glReadBuffer(GL_COLOR_ATTACHMENT0);
                std::vector<u8> actual(source.size());
                glReadPixels(0,0,engine.ScreenW,engine.ScreenH,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
                for (int y = 0; y < engine.ScreenH; ++y)
                    for (int x = 0; x < engine.ScreenW; ++x)
                    {
                        const auto offset = (y * engine.ScreenW + x) * 4;
                        const auto* expected = ((x / oldScale) & 1) ? source.data() : ordinary.data();
                        Check(std::memcmp(actual.data()+offset,expected+offset,4)==0,
                              "scaling off keeps native text pixels over high-resolution capture pixels");
                    }
            }
        }
        std::copy(savedMapping.begin(), savedMapping.end(), mapping);
        engine.SetWholeSceneScaleMode(oldMode);
        engine.SetWholeSceneScaleAlgorithm(oldAlgorithm);
        engine.WholeSceneScaleRequested = oldRequested;
        renderer.PhysicalFinalUpscale = oldPhysical;
        renderer.Reset();
        Check(glGetError() == GL_NO_ERROR, "bitmap row reference GL state");
        std::puts("[GL lifetime] streamed rows match complete bitmap across compositors and scalers");
    }

    static void PostprocessingNativeArtwork(GLRenderer& renderer, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        const auto mode = engine.WholeSceneScaleMode;
        const bool requested = engine.WholeSceneScaleRequested;
        const bool affine = engine.WholeSceneScaleHybridStrictAffineHighRes;
        renderer.Reset();
        engine.UpdateLayerConfig(); // Select the native BG backing textures, as a real scanline does.
        engine.SetWholeSceneScaleMode(RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale);
        engine.WholeSceneScaleRequested = true;
        engine.WholeSceneScaleHybridStrictAffineHighRes = false;
        engine.LayerEnable = 1;
        engine.LayerConfig.uVRAMMask = engineB ? 127 : 511;
        auto& bg = engine.LayerConfig.uBGConfig[0];
        bg = {};
        bg.Type = 0;
        bg.Size[0] = bg.Size[1] = 16;
        bg.MapOffset = 8192;
        std::vector<u8> vram((engine.LayerConfig.uVRAMMask + 1) * 1024);
        std::fill(vram.begin(), vram.begin() + 32, 0x11);
        std::fill(std::begin(engine.UploadedBGPalette), std::end(engine.UploadedBGPalette), 0);
        engine.UploadedBGPalette[1] = 0x801F;
        engine.UploadedBGPalette[2] = 0xFC00;
        engine.UploadedBGPaletteValid = true;
        const auto upload = [&] {
            NonStupidBitField<1024> dirty(0, 1024);
            engine.BGVRAMUploadCache.FilterUnchanged(vram.data(), vram.size(), dirty);
            engine.BGVRAMUploadCache.RecordUpload(vram.data(), 0, vram.size());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, engine.VRAMTex_BG);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, engine.LayerConfig.uVRAMMask + 1,
                            GL_RED_INTEGER, GL_UNSIGNED_BYTE, vram.data());
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, engine.PalTex_BG);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 65, GL_RGBA,
                            GL_UNSIGNED_SHORT_1_5_5_5_REV, engine.UploadedBGPalette);
            glBindBuffer(GL_UNIFORM_BUFFER, engine.LayerConfigUBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(engine.LayerConfig), &engine.LayerConfig);
            const int rows[4] {};
            engine.PrerenderDirtyLayers(1, 0, rows, rows, 0);
        };
        const auto pixels = [&] {
            std::array<u8, 16 * 16 * 4 * 2> image;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, engine.BGLayerFB[0]);
            for (int attachment = 0; attachment < 2; ++attachment)
            {
                glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
                glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE,
                             image.data() + attachment * 16 * 16 * 4);
            }
            return image;
        };
        upload();
        const auto original = pixels();
        auto draws = DrawArraysCalls;
        vram[32768] ^= 1;
        upload();
        Check(DrawArraysCalls == draws && pixels() == original,
              "postprocessing retains exact native color/metadata with hybrid affine disabled");
        engine.UploadedBGPalette[1] ^= 0x8000;
        upload();
        Check(DrawArraysCalls == draws + 1 && pixels() != original,
              "postprocessing redraws a referenced palette high-bit change immediately");
        const auto palette = pixels();
        vram[0] = 0x22;
        upload();
        Check(DrawArraysCalls == draws + 2 && pixels() != palette,
              "postprocessing redraws referenced tile changes immediately");
        draws = DrawArraysCalls;
        engine.NativeBGPrerenderTexture[0] = 0;
        upload();
        Check(DrawArraysCalls == draws + 1, "postprocessing requires native backing ownership");
        engine.SetWholeSceneScaleMode(mode);
        engine.WholeSceneScaleRequested = requested;
        engine.WholeSceneScaleHybridStrictAffineHighRes = affine;
        renderer.Reset();
        Check(glGetError() == GL_NO_ERROR, "postprocessing native artwork GL state");
        std::puts("[GL lifetime] postprocess native artwork: reuse, palette, tile, ownership passed");
    }
    static void BitmapCoveragePriority(GLRenderer& renderer)
    {
        renderer.Reset();
        auto& engine = *static_cast<GLRenderer2D*>(renderer.Rend2D_B.get());
        engine.DispCnt = 1u << 16;
        engine.LayerEnable = (1u << 2) | (1u << 3);
        for (int layer : {2, 3})
        {
            auto& cfg = engine.LayerConfig.uBGConfig[layer];
            cfg.Type = 5;
            cfg.Size[0] = cfg.Size[1] = 256;
            cfg.MapOffset = 0;
            engine.BGCnt[layer] = (1u << 14) | (layer - 2);
            for (int y = 0; y < 192; ++y)
            {
                engine.ScanlineConfig.uScanline[y].BGOffset[layer][0] = 0;
                engine.ScanlineConfig.uScanline[y].BGOffset[layer][1] = y * 256;
                engine.ScanlineConfig.uScanline[y].BGRotscale[layer - 2][0] = 256;
                engine.ScanlineConfig.uScanline[y].BGRotscale[layer - 2][2] = 0;
            }
        }
        std::vector<u8> pixels(128 * 1024, 0x80);
        const auto covered = [&] { return engine.CoveredByOpaqueBitmapBGLayerMask(0, 192, pixels.data(), pixels.size() - 1); };
        Check(covered() == (1u << 3), "opaque upper bitmap covers lower-priority bitmap");
        engine.BGCnt[3] &= ~3u;
        Check(covered() == 0, "equal-priority bitmap is not treated as an opaque cover");
        engine.BGCnt[3] |= 1;
        pixels[1] = 0;
        Check(covered() == 0, "one transparent sample prevents opaque coverage");
        pixels[1] = 0x80;
        engine.DispCnt |= 1u << 15;
        Check(covered() == 0, "OBJ window retains ordinary bitmap rendering");
        engine.DispCnt &= ~(1u << 15);
        engine.BlendCnt = 1u << 6;
        Check(covered() == 0, "blending retains ordinary bitmap rendering");
        engine.BlendCnt = 0;
        engine.BGCnt[2] |= 1u << 6;
        Check(covered() == 0, "mosaic prevents an opaque bitmap proof");
        engine.BGCnt[2] &= ~(1u << 6);
        engine.LayerEnable = (1u << 0) | (1u << 2);
        engine.LayerConfig.uBGConfig[0].Type = 0;
        Check(covered() == 0, "a lone bitmap cannot suppress a text layer");
        renderer.Reset();
        std::puts("[GL lifetime] bitmap coverage: priority, alpha, windows, effects, mosaic passed");
    }
    static void CaptureSpriteInvalidation(GLRenderer& renderer)
    {
        renderer.Reset();
        auto& a = *static_cast<GLRenderer2D*>(renderer.Rend2D_A.get());
        auto& b = *static_cast<GLRenderer2D*>(renderer.Rend2D_B.get());
        a.SpriteConfigDirty = b.SpriteConfigDirty = false;
        renderer.AllocCapture(0, 0, 3);
        Check(!a.SpriteConfigDirty && !b.SpriteConfigDirty,
              "capture allocation does not invalidate empty decoded sprite lists");
        Check(a.LayerConfigDirty && b.LayerConfigDirty,
              "capture allocation still invalidates BG capture mappings");
        a.NumSprites = 1;
        renderer.AllocCapture(0, 0, 3);
        Check(a.SpriteConfigDirty && !b.SpriteConfigDirty,
              "capture allocation invalidates an active main-engine sprite consumer");
        a.SpriteConfigDirty = false;
        a.NumSprites = 0;
        b.NumSprites = 1;
        renderer.AllocCapture(0, 0, 3);
        Check(!a.SpriteConfigDirty && b.SpriteConfigDirty,
              "capture allocation invalidates an active sub-engine sprite consumer");
        b.NumSprites = 0;
        renderer.AllocCapture(0, 0, 3);
        Check(b.SpriteConfigDirty, "empty-list optimization preserves pending sprite invalidation");
        // A later guest OAM write must still start a fresh decode, even if the
        // previous capture allocation found no sprite consumer.
        auto& gpu = renderer.GPU;
        const auto enabled = gpu.ScreensEnabled;
        const auto oamDirty = gpu.OAMDirty;
        gpu.ScreensEnabled = false;
        a.SpriteDispCnt = a.GPU2D.DispCnt;
        a.SpriteConfigDirty = a.SpriteDirty = false;
        gpu.WriteOAM<u16>(0, gpu.ReadOAM<u16>(0));
        a.DrawSprites(0);
        Check(a.SpriteDirty && !(gpu.OAMDirty & 1),
              "guest OAM writes still invalidate an empty decoded sprite list");
        gpu.ScreensEnabled = enabled;
        gpu.OAMDirty = oamDirty;
        renderer.Reset();
        Check(glGetError() == GL_NO_ERROR, "capture sprite invalidation GL state");
        std::puts("[GL lifetime] capture invalidation: empty, active, pending, later OAM write passed");
    }
    static void CaptureColorPrecision(GLRenderer& renderer)
    {
        std::vector<u8> input(256*192*4);
        for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
            const size_t i=(y*256+x)*4;
            input[i]=x;input[i+1]=255-x;input[i+2]=(x*17)&255;input[i+3]=(x&1)?255:0;
        }
        GLuint texture=0;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,192,0,GL_RGBA,GL_UNSIGNED_BYTE,input.data());
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D_ARRAY,renderer.AuxInputTex);
        renderer.CaptureConfig={};renderer.CaptureConfig.uInvCaptureSize[0]=1.f/256;
        renderer.CaptureConfig.uInvCaptureSize[1]=1.f/192;
        glBindBuffer(GL_UNIFORM_BUFFER,renderer.CaptureConfigUBO);
        glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(renderer.CaptureConfig),&renderer.CaptureConfig);
        glBindBufferBase(GL_UNIFORM_BUFFER,31,renderer.CaptureConfigUBO);
        const u16 vertices[]={0,192,0,192,256,0,256,0,256,192,256,192,
                              0,192,0,192,0,0,0,0,256,0,256,0};
        glBindBuffer(GL_ARRAY_BUFFER,renderer.CaptureVtxBuffer);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(vertices),vertices);
        glBindVertexArray(renderer.CaptureVtxArray);glUseProgram(renderer.CaptureShader);
        glBindFramebuffer(GL_FRAMEBUFFER,renderer.CaptureOutput256FB[0]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        const int scale=renderer.ScaleFactor;
        glViewport(0,0,256*scale,256*scale);glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);glDisable(GL_DITHER);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        for (bool native : {false,true})
        {
            glUniform1i(renderer.CaptureQuantizeRGB555ULoc,native);
            glDrawArrays(GL_TRIANGLES,0,6);
            std::vector<u8> result(256*scale*192*scale*4);
            glReadPixels(0,0,256*scale,192*scale,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
            for(int y=0;y<192*scale;++y) for(int x=0;x<256*scale;++x)
                for(int c=0;c<4;++c) {
                    const u8 source=input[((y/scale)*256+x/scale)*4+c];
                    Check(result[(y*256*scale+x)*4+c]==(c==3||!native?source:(source&~7)),
                          "capture keeps enhanced precision and matches RGB555 in native presentation");
                }
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&texture);
        Check(glGetError()==GL_NO_ERROR,"capture precision shader GL state");
        std::puts("[GL lifetime] all 256 capture values: native RGB555 and enhanced precision preserved");
    }
    static void CopiedCaptureStorage(GLRenderer& renderer)
    {
        renderer.ClearCopiedCaptures();
        std::vector<u8> bytes(CaptureCopyTracker::ImageBytes, 0xFF);
        const auto source = renderer.HighResDisplayCaptureFullTex[0];
        const GLfloat red[] = {1,0,0,1}, blue[] = {0,0,1,1};
        auto fill = [&](const GLfloat* color) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.HighResDisplayCaptureFullFB[0]);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearBufferfv(GL_COLOR,0,color);
        };
        fill(red);
        const auto slot = renderer.CopiedCaptures.Store(0x18000, bytes.data());
        Check(renderer.RetainCopiedCaptureProduct(slot,0), "retain copied capture pixels");
        const auto texture = renderer.CopiedCaptureProducts[slot].Texture;
        const auto framebuffer = renderer.CopiedCaptureProducts[slot].Framebuffer;
        const auto created = Textures.Created;
        fill(blue);
        Check(renderer.RetainCopiedCaptureProduct(slot,0), "reuse copied capture storage");
        Check(Textures.Created==created && renderer.CopiedCaptureProducts[slot].Texture==texture,
              "copied capture refresh reuses its owned allocation");
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::array<u8,4> pixel{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        Check(pixel==std::array<u8,4>{0,0,255,255}, "copied capture contains current source pixels");
        // Ordinary captures are sufficient when no enhanced 2D product exists.
        // Keep the source FBO array and the owned 2D snapshot independent.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.CaptureOutput256FB[0]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearBufferfv(GL_COLOR,0,red);
        Check(renderer.RetainCopiedCaptureProduct(slot,0,false), "retain ordinary capture without a scaler");
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        Check(pixel==std::array<u8,4>{255,0,0,255}, "ordinary capture snapshot uses the correct source");
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.CaptureOutput256FB[0]);
        glClearBufferfv(GL_COLOR,0,blue);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        Check(pixel==std::array<u8,4>{255,0,0,255}, "later capture writes cannot overwrite retained pixels");
        Check(Textures.Created==created, "ordinary and enhanced snapshots reuse the same owned storage");
        const auto other = renderer.CopiedCaptures.Store(0x38000, bytes.data());
        FailScratchAllocation=true;
        Check(!renderer.RetainCopiedCaptureProduct(other,0), "failed copied capture allocation is rejected");
        FailScratchAllocation=false;
        while(glGetError()!=GL_NO_ERROR) {}
        Check(!renderer.CopiedCaptureProducts[other].Texture && !renderer.CopiedCaptureProducts[other].Framebuffer,
              "failed copied capture allocation releases partial ownership");
        Check(renderer.RetainCopiedCaptureProduct(other,0), "copied capture allocation can retry");
        renderer.Reset();
        Check(!glIsTexture(texture) && !glIsFramebuffer(framebuffer) && glIsTexture(source),
              "reset deletes copied products without deleting borrowed capture sources");
        Check(renderer.CopiedCaptures.Get(slot).Generation==0, "reset invalidates copied RAM provenance");
        const auto resized = renderer.CopiedCaptures.Store(0x18000, bytes.data());
        Check(renderer.RetainCopiedCaptureProduct(resized,0), "copied capture can restart after reset");
        const auto oldTexture=renderer.CopiedCaptureProducts[resized].Texture;
        const int oldScale=renderer.ScaleFactor;
        renderer.SetScaleFactor(oldScale+1);
        Check(!glIsTexture(oldTexture) && renderer.CopiedCaptures.Get(resized).Generation==0,
              "resolution changes invalidate copied products and provenance together");
        renderer.SetScaleFactor(oldScale);
        Check(glGetError()==GL_NO_ERROR,"copied capture resource GL state");
        std::puts("[GL lifetime] copied capture pixels, reuse, failed allocation, reset, resize passed");
    }
    static void CopiedWideCaptureStorage(GLRenderer& renderer)
    {
        auto& engine = *static_cast<GLRenderer2D*>(renderer.Rend2D_B.get());
        renderer.ClearCopiedCaptures();
        std::vector<u8> bytes(CaptureCopyTracker::ImageBytes, 0xFF);
        const auto slot = renderer.CopiedCaptures.Store(0x18000, bytes.data());
        auto& product = renderer.CopiedCaptureProducts[slot];
        auto& state = renderer.WideCapture[0];
        const WideCaptureStrips strips {WideMelon::Width(), WideMelon::CaptureOverlap()};
        const int width = WideMelon::Rectangular() ? WideMelon::Width() * renderer.WideSourceScale :
            WideMelon::Vertical() ? renderer.ScreenW : strips.PackedWidth() * renderer.ScaleFactor;
        const int height = WideMelon::Rectangular() ? WideMelon::Height() * renderer.WideSourceScale :
            WideMelon::Vertical() ? (WideMelon::Height()-192+2*WideMelon::VerticalCaptureOverlap()) * renderer.ScaleFactor :
            renderer.ScreenH;
        glGenTextures(1, &renderer.WideCaptureTex[0]);
        glBindTexture(GL_TEXTURE_2D, renderer.WideCaptureTex[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&renderer.WideCaptureFB[0]);
        glBindFramebuffer(GL_FRAMEBUFFER,renderer.WideCaptureFB[0]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer.WideCaptureTex[0],0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        const GLfloat red[]={1,0,0,1},blue[]={0,0,1,1};
        glClearBufferfv(GL_COLOR,0,red);
        state.Serial=product.EventSerial=renderer.HighResDisplayCapture256Event[0].Serial=73;
        state.FullWidth=WideMelon::Vertical();
        state.HorizontalBorders[50]=1;state.VerticalBorders[80]=2;
        renderer.RetainCopiedWideCaptureProduct(slot,0);
        const GLuint owned=product.WideTexture;
        const size_t allocations=Textures.Created;
        renderer.RetainCopiedWideCaptureProduct(slot,0);
        Check(product.WideTexture==owned && Textures.Created==allocations,
              "copied expanded capture reuses owned storage");
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,renderer.WideCaptureFB[0]);
        glClearBufferfv(GL_COLOR,0,blue);
        state={}; // Reading/reusing the source bank must not affect the RAM copy.
        glBindFramebuffer(GL_READ_FRAMEBUFFER,product.WideFramebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::array<u8,4> pixel{};glReadPixels(width-1,height-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        Check(pixel==std::array<u8,4>{255,0,0,255},"copied expanded pixels survive source bank reuse");
        Check(product.WideState.HorizontalBorders[50]==1 && product.WideState.VerticalBorders[80]==2,
              "copied margins retain their own border provenance");
        engine.CopiedBitmapProof.Slot=slot;engine.CopiedBitmapLayer=2;
        engine.CopiedBitmapAppliedGeneration=renderer.CopiedCaptures.Get(slot).Generation;
        engine.CopiedBitmapAppliedEvent=73;
        GLuint texture=0;int layer=-1;
        Check(renderer.ResolveWideCapture(engine,0,192,layer,texture)==&product.WideState &&
              texture==owned && layer==2,"verified copied frame selects its own expanded capture");
        Check(!renderer.ResolveWideCapture(engine,0,191,layer,texture),"partial display cannot borrow a full-frame copy");
        engine.CopiedBitmapAppliedEvent=74;
        Check(!renderer.ResolveWideCapture(engine,0,192,layer,texture),"different capture event cannot borrow old margins");
        engine.CopiedBitmapAppliedEvent=73;
        renderer.CopiedCaptures.Invalidate(slot);
        Check(!renderer.ResolveWideCapture(engine,0,192,layer,texture),"overwritten RAM invalidates copied margins");
        renderer.CopiedCaptures.Store(0x18000,bytes.data());
        Check(!renderer.ResolveWideCapture(engine,0,192,layer,texture),"slot reuse cannot match a previous generation");
        // Reusing a slot for a capture without margins must discard old metadata.
        renderer.RetainCopiedWideCaptureProduct(slot,0);
        Check(!product.WideState.Serial,"missing wide source clears prior margin provenance");
        engine.CopiedBitmapAppliedGeneration=renderer.CopiedCaptures.Get(slot).Generation;
        engine.CopiedBitmapAppliedEvent=product.EventSerial=0;
        Check(!renderer.ResolveWideCapture(engine,0,192,layer,texture),"missing capture serial cannot expose a reused texture");
        state.Serial=renderer.HighResDisplayCapture256Event[0].Serial=75;
        state.FullWidth=WideMelon::Vertical();
        const auto failed=renderer.CopiedCaptures.Store(0x38000,bytes.data());
        FailScratchAllocation=true;
        renderer.RetainCopiedWideCaptureProduct(failed,0);
        FailScratchAllocation=false;
        while(glGetError()!=GL_NO_ERROR) {}
        const auto& failedProduct=renderer.CopiedCaptureProducts[failed];
        Check(!failedProduct.WideTexture && !failedProduct.WideFramebuffer && !failedProduct.WideState.Serial,
              "failed expanded snapshot releases partial storage");
        const GLuint borrowed=renderer.WideCaptureTex[0];
        renderer.ClearCopiedCaptures();
        Check(!glIsTexture(owned) && glIsTexture(borrowed),"clearing copied margins leaves capture bank ownership intact");
        engine.CopiedBitmapAppliedGeneration=0;
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        Check(glGetError()==GL_NO_ERROR,"copied expanded capture GL state");
    }
    static void RectangularBorders(GLRenderer& renderer)
    {
        const int scale=renderer.ScaleFactor,w=WideMelon::Width()*scale,h=WideMelon::Height()*scale;
        const int left=(w-256*scale)/2,top=(h-192*scale)/2;
        const bool savedExpand=WideMelon::ExpandNarrowBorders;
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DITHER);
        const GLfloat scenery[]={20.f/255,40.f/255,60.f/255,1};
        const GLfloat native[]={220.f/255,80.f/255,100.f/255,1};
        const GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
        for(int mode=0;mode<3;++mode)
        {
            WideMelon::ExpandNarrowBorders=mode!=2;
            for(int y=0;y<192;++y) for(int screen=0;screen<2;++screen)
                renderer.WideNarrowBorderRows[y][screen]=mode==1?0:(y/(screen?11:7))%4;
            for(int x=0;x<256;++x) for(int screen=0;screen<2;++screen)
                renderer.WideNarrowBorderColumns[x][screen]=mode==1?0:(x/(screen?17:13))%4;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,renderer.WideOutputFB[renderer.BackBuffer]);
            glDrawBuffers(2,outputs);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,renderer.FPOutputFB[renderer.BackBuffer]);
            for(int screen=0;screen<2;++screen)
            {
                glColorMaski(screen,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
                glClearBufferfv(GL_COLOR,screen,scenery);
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,renderer.FPOutputFB[renderer.BackBuffer]);
            glDrawBuffers(2,outputs);
            for(int screen=0;screen<2;++screen) glClearBufferfv(GL_COLOR,screen,native);
            renderer.CopyWideCenter();
            glBindFramebuffer(GL_READ_FRAMEBUFFER,renderer.WideOutputFB[renderer.BackBuffer]);
            for(int screen=0;screen<2;++screen)
            {
                glReadBuffer(outputs[screen]);
                std::vector<u8> pixels(w*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                {
                    const int nx=(x-left)/scale,ny=(y-top)/scale;
                    const bool inside=x>=left&&x<left+256*scale&&y>=top&&y<top+192*scale;
                    const u8 rows=inside?renderer.WideNarrowBorderRows[ny][screen]:0;
                    const u8 cols=inside?renderer.WideNarrowBorderColumns[nx][screen]:0;
                    const bool border=mode!=2&&inside&&((nx<2&&(rows&1))||(nx>=254&&(rows&2))||
                        (ny<2&&(cols&1))||(ny>=190&&(cols&2)));
                    const std::array<u8,4> expected=inside&&!border?std::array<u8,4>{220,80,100,255}:
                        std::array<u8,4>{20,40,60,255};
                    Check(std::memcmp(pixels.data()+(y*w+x)*4,expected.data(),4)==0,
                        "rectangular native center preserves only proved border overlap on all four sides");
                }
            }
        }
        WideMelon::ExpandNarrowBorders=savedExpand;
        Check(glGetError()==GL_NO_ERROR,"rectangular four-side center copy GL state");
        std::puts("[GL lifetime] rectangular four-side center copy, corners, disabled option passed");
    }
    static void CaptureProbeIsolation(GLRenderer& renderer)
    {
        renderer.Reset(); // CSV diagnostics consume initialized OAM/engine state.
        std::string header, row;
        const auto originalCapture = renderer.CaptureCnt;
        renderer.CaptureCnt = 0; // Complete 128x128 source-A display capture.
        Check(!renderer.IsCaptureLumaProbeEnabled(), "content probe defaults off");
        for (int i = 0; i < 10; ++i)
        {
            renderer.ReadWholeScene2DTimingCSV(header, row);
            renderer.ResetWholeSceneFrameTiming();
            renderer.DoCapture(0, 128);
        }
        Check(renderer.GetCaptureLumaProbeReadbackCount() == 0 && !renderer.LastCaptureNativeLumaDebug.Valid,
              "CSV logging must never arm the content probe");
        renderer.SetCaptureLumaProbeEnabled(true);
        renderer.DoCapture(0, 64);
        Check(renderer.GetCaptureLumaProbeReadbackCount() == 0, "partial capture does not read back luma");
        renderer.DoCapture(64, 128);
        Check(renderer.GetCaptureLumaProbeReadbackCount() == 1 && renderer.LastCaptureNativeLumaDebug.Valid,
              "explicit diagnostic probe reads the completed capture");
        renderer.SetCaptureLumaProbeEnabled(false);
        renderer.ResetWholeSceneFrameTiming();
        renderer.ReadWholeScene2DTimingCSV(header, row);
        renderer.DoCapture(0, 128);
        Check(renderer.GetCaptureLumaProbeReadbackCount() == 1 && !renderer.LastCaptureNativeLumaDebug.Valid,
              "disabling the probe takes effect immediately, including while logging");
        renderer.CaptureCnt = originalCapture;
    }
    static void PoisonScratchAfterEviction(GLRenderer2D& engine)
    {
        // Evict all previous sizes, and leave visible garbage in every retained
        // allocation. Forced reconstruction must still reproduce current art.
        glActiveTexture(GL_TEXTURE0);
        for (int index = 0; index < 2; ++index)
            for (int i = 0; i <= engine.ReconstructionScratchEntries; ++i)
            {
                Check(engine.EnsureCuNNyWorkTexture(index,13+i,17), "scratch eviction before reconstruction");
                std::vector<float> poison((13+i)*17*4,1.0f);
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,13+i,17,GL_RGBA,GL_FLOAT,poison.data());
            }
        for (int i = 0; i <= engine.ReconstructionScratchEntries; ++i)
        {
            const int w = 13+i, h = 17;
            Check(engine.EnsureStrictAffineCuNNy2xTexture(w*2,h*2) &&
                  engine.EnsureReconstructionAlphaTextures(w,h,4), "result/alpha eviction before reconstruction");
            for (int role = 0; role < 4; ++role)
            {
                const GLuint texture = role == 0 ? engine.StrictAffineCuNNy2xTex : engine.ReconstructionAlphaWorkTex[role-1];
                const int scale = role == 0 ? 2 : role == 1 ? 1 : 4;
                std::vector<float> poison(w*h*scale*scale*4,1.0f);
                glBindTexture(GL_TEXTURE_2D,texture);
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w*scale,h*scale,GL_RGBA,GL_FLOAT,poison.data());
            }
        }
    }

    static void UniformUploads(GLRenderer& renderer)
    {
        auto& classic = static_cast<GLRenderer3D&>(*renderer.Rend3D);
        const auto saved = classic.ShaderConfig;
        for (int failure = 0; failure < 3; ++failure)
        {
            std::memset(&classic.ShaderConfig, 0x31 + failure, sizeof(classic.ShaderConfig));
            FailUniformMap = failure == 1;
            FailUniformUnmap = failure == 2;
            const auto unmaps = UniformUnmaps;
            classic.UploadShaderConfig();
            FailUniformMap = FailUniformUnmap = false;
            std::array<unsigned char, sizeof(classic.ShaderConfig)> actual{};
            glGetBufferSubData(GL_UNIFORM_BUFFER, 0, actual.size(), actual.data());
            Check(std::memcmp(actual.data(), &classic.ShaderConfig, actual.size()) == 0,
                  "uniform upload and fallback publish the complete current payload");
            Check(UniformMapFlags == (GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT),
                  "uniform upload permits replacement storage without unsynchronized writes");
            Check(UniformUnmaps - unmaps == (failure == 1 ? 0 : 1),
                  "failed uniform mapping must not unmap an unmapped buffer");
        }
        classic.ShaderConfig = saved;
        classic.UploadShaderConfig();
        Check(glGetError() == GL_NO_ERROR, "uniform upload failure recovery");
        std::puts("[GL lifetime] Classic uniform payload, map/unmap failure recovery passed");
    }

    static void ResultAlphaScratchStorage(GLRenderer& renderer, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        const auto acquire = [&](int w, int h) {
            glActiveTexture(GL_TEXTURE0);
            Check(engine.EnsureStrictAffineCuNNy2xTexture(w*2,h*2) &&
                  engine.EnsureReconstructionAlphaTextures(w,h,4), "result/alpha scratch allocation");
            std::array<GLuint,4> names{engine.StrictAffineCuNNy2xTex,
                engine.ReconstructionAlphaWorkTex[0],engine.ReconstructionAlphaWorkTex[1],engine.ReconstructionAlphaWorkTex[2]};
            for (int role = 0; role < 4; ++role)
            {
                glBindTexture(GL_TEXTURE_2D,names[role]);
                GLint actualW = 0, actualH = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&actualW);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&actualH);
                const int scale = role == 0 ? 2 : role == 1 ? 1 : 4;
                Check(actualW == w*scale && actualH == h*scale, "each scratch role preserves exact dimensions");
            }
            return names;
        };
        const int sizes[][2] = {{64,64},{256,192},{32,64}};
        std::array<GLuint,4> warm[3];
        for (int i = 0; i < 3; ++i) warm[i] = acquire(sizes[i][0],sizes[i][1]);
        const auto created = Textures.Created, storage = TextureStorageCalls;
        for (int repeat = 0; repeat < 3; ++repeat)
            for (int i = 0; i < 3; ++i)
                Check(acquire(sizes[i][0],sizes[i][1]) == warm[i], "warm result and alpha texture names persist");
        Check(Textures.Created == created && TextureStorageCalls == storage,
              "alternating sprite/background sizes perform no warm result/alpha allocation");
        Check(engine.EnsureStrictAffineCuNNy2xTexture(8,8), "result input allocation");
        const auto input = engine.StrictAffineCuNNy2xTex;
        std::array<float,8*8*4> pattern{}, actual{};
        for (size_t i = 0; i < pattern.size(); ++i) pattern[i] = float(i%16)/16;
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_FLOAT,pattern.data());
        for (int i = 0; i < 6; ++i)
            Check(engine.EnsureReconstructionAlphaTextures(13+i,17,4), "alpha eviction pressure");
        glBindTexture(GL_TEXTURE_2D,input);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,actual.data());
        Check(actual == pattern, "alpha allocation cannot evict a live RGB input");
        for (auto* set : {&engine.CuNNyResultScratch,&engine.AlphaScratch[0],&engine.AlphaScratch[1],&engine.AlphaScratch[2]})
        {
            u64 bytes = 0; int count = 0;
            for (auto& entry : set->Entries) { bytes += entry.Bytes(); count += entry.Texture != 0; }
            Check(bytes <= engine.ReconstructionResultBudget && count <= engine.ReconstructionScratchEntries,
                  "each result/alpha role obeys retention limits");
        }
        Check(engine.EnsureStrictAffineCuNNy2xTexture(1024,1025), "oversized result working texture");
        const auto large = engine.StrictAffineCuNNy2xTex;
        const auto largeStorage = TextureStorageCalls;
        Check(engine.EnsureStrictAffineCuNNy2xTexture(1024,1025) && engine.StrictAffineCuNNy2xTex == large &&
              TextureStorageCalls == largeStorage, "oversized result reused");
        for (auto& entry : engine.CuNNyResultScratch.Entries) Check(!entry.Texture, "oversized result drops retained small entries");
        Check(engine.EnsureStrictAffineCuNNy2xTexture(8,8) && !engine.CuNNyResultScratch.Large.Texture,
              "return to ordinary dimensions releases oversized result");
        const auto normal = engine.StrictAffineCuNNy2xTex;
        engine.ResolvedOrdinaryOBJValid = engine.ResolvedOrdinaryOBJArtworkChecked = true;
        engine.Reset();
        Check(!engine.ResolvedOrdinaryOBJValid && !engine.ResolvedOrdinaryOBJArtworkChecked,
              "reset invalidates ordinary output and dependency signature");
        engine.SetScaleFactor(3); engine.SetScaleFactor(2);
        Check(engine.EnsureStrictAffineCuNNy2xTexture(8,8) && engine.StrictAffineCuNNy2xTex == normal,
              "dimension-keyed result storage survives reset and scale changes");
        const auto beforeInvalid = Textures.Created;
        GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
        Check(!engine.EnsureReconstructionAlphaTextures(0,8,4) &&
              !engine.EnsureReconstructionAlphaTextures(8,8,3) &&
              !engine.EnsureReconstructionAlphaTextures(maximum,8,4) &&
              !engine.EnsureStrictAffineCuNNy2xTexture(maximum+1,8), "invalid result/alpha sizes rejected");
        Check(Textures.Created == beforeInvalid, "invalid result/alpha requests allocate nothing");
        const auto live = Textures.Live.size();
        FailScratchAllocation = true;
        const bool ok = engine.EnsureReconstructionAlphaTextures(63,61,4);
        FailScratchAllocation = false;
        Check(!ok && Textures.Live.size() <= live, "failed alpha allocation releases its name");
        Check(engine.EnsureReconstructionAlphaTextures(63,61,4), "alpha recovers after storage allocation failure");
        Check(glGetError() == GL_NO_ERROR, "result/alpha scratch GL operations");
        std::printf("[GL lifetime] bounded result/alpha scratch engine %c passed\n",engineB?'B':'A');
    }

    static void OrdinarySpriteSurface(GLRenderer& renderer, bool engineB,
                                      RendererSettings::GLScaleAlgorithm algorithm)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        engine.SetScaleFactor(2);
        engine.WholeSceneScaleRequested = true;
        engine.WholeSceneScaleHybridStrictAffineHighRes = true;
        engine.WholeSceneScaleHybridStrictAffineSourceEnhancement = true;
        engine.SetWholeSceneScaleHybridStrictAffineConnectedSources(false);
        engine.WholeSceneScaleAlgorithm = algorithm;
        engine.AffineAlpha = RendererSettings::ResolveAffineAlphaReconstruction(
            RendererSettings::AffineAlphaReconstruction::Automatic,algorithm);
        const auto enhancement = engine.CurrentStrictAffineSourceEnhancementDecision(true);
        const int model = RendererSettings::IsGLArtCNNAlgorithm(algorithm)
            ? RendererSettings::GetGLArtCNNModelIndex(algorithm)
            : RendererSettings::IsGLCuNNyAlgorithm(algorithm) ? RendererSettings::GetGLCuNNyModelIndex(algorithm) : -1;
        engine.NumSprites = 3;
        engine.SpriteUseMosaic = false;
        engine.SpriteConfig.uVRAMMask = engineB ? 127 : 255;
        const u32 mask = (engine.SpriteConfig.uVRAMMask<<10)|1023;
        engine.UploadedOBJVRAM.resize(mask+1);
        engine.SpriteConfig.uRotscale[0][0] = engine.SpriteConfig.uRotscale[0][3] = 256;
        engine.SpriteConfig.uRotscale[0][1] = engine.SpriteConfig.uRotscale[0][2] = 0;
        const auto upload = [&](bool bumpGeneration = true) {
            if (bumpGeneration) ++engine.SpriteSourceGeneration;
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,engine.VRAMTex_OBJ);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1024,engine.SpriteConfig.uVRAMMask+1,
                GL_RED_INTEGER,GL_UNSIGNED_BYTE,engine.UploadedOBJVRAM.data());
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,engine.PalTex_OBJ);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,17,GL_RGBA,GL_UNSIGNED_SHORT_1_5_5_5_REV,engine.UploadedOBJPalette);
            glBindBuffer(GL_UNIFORM_BUFFER,engine.SpriteConfigUBO);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(engine.SpriteConfig),&engine.SpriteConfig);
            engine.PrerenderSprites();
        };
        const auto prepare = [&](bool reused) {
            engine.WholeSceneTrace = {};
            Check(engine.RenderResolvedOrdinaryOBJProduct(0,192,enhancement,model), "ordinary surface reconstruction");
            Check(engine.WholeSceneTrace.StrictAffineResolvedOBJOrdinaryProductReused == reused,
                  "ordinary surface invalidates exactly when its dependencies change");
        };
        const auto read = [&] {
            std::vector<unsigned char> pixels(engine.ScreenW*engine.ScreenH*4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.ResolvedOrdinaryOBJFB);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,0,engine.ScreenW,engine.ScreenH,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            // Ownership/material metadata must survive hits too, not only RGBA.
            std::vector<unsigned char> metadata(256*192*4*3);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.ResolvedOrdinaryOBJNativeLayerFB);
            for (int layer = 0; layer < 3; ++layer)
            {
                glReadBuffer(GL_COLOR_ATTACHMENT0+layer);
                glReadPixels(0,0,256,192,GL_RGBA,GL_UNSIGNED_BYTE,metadata.data()+layer*256*192*4);
            }
            pixels.insert(pixels.end(),metadata.begin(),metadata.end());
            return pixels;
        };
        const auto forced = [&] {
            const auto cached = read();
            engine.ResolvedOrdinaryOBJValid = false;
            PoisonScratchAfterEviction(engine);
            prepare(false);
            Check(read() == cached, "ordinary surface cache equals forced current reconstruction after poisoned scratch");
        };
        for (int variant = 0; variant < 4; ++variant)
        {
            const bool eightBit = variant != 0;
            const u32 palette = !eightBit ? 32 : variant == 1 ? 0 : variant == 2 ? 1 : 16;
            std::fill(engine.UploadedOBJVRAM.begin(),engine.UploadedOBJVRAM.end(),eightBit?1:0x11);
            for (int p = 0; p < 17*256; ++p) engine.UploadedOBJPalette[p] = (p*71)&0xFFFF;
            for (int i = 0; i < 3; ++i)
            {
                auto& source = engine.SpriteConfig.uOAM[i]; source = {};
                source.Rotscale = i == 2 ? 0 : u32(-1);
                source.Type = eightBit ? 1 : 0; source.PalOffset = palette;
                source.Size[0] = source.Size[1] = source.BoundSize[0] = source.BoundSize[1] = 16;
                source.Position[0] = i*20;
                source.TileOffset = i == 0 ? mask-31 : i*4096;
                source.TileStride = eightBit ? 256 : 128;
                engine.SpriteOAMIndex[i] = i;
            }
            engine.ResolvedOrdinaryOBJValid = false;
            upload(); prepare(false); prepare(true); forced();
            const auto original = read();
            bool visible = false;
            for (size_t p = 3; p < size_t(engine.ScreenW*engine.ScreenH*4); p += 4) visible |= original[p] != 0;
            Check(visible, "ordinary surface fixture contains visible reconstructed artwork");
            upload(); prepare(true);
            engine.UploadedOBJVRAM[40000] ^= 255;
            engine.UploadedOBJVRAM[8192] ^= 255; // affine-only sprite
            engine.UploadedOBJPalette[1000] ^= 0xFFFF;
            upload(); prepare(true);
            Check(read() == original, "unrelated and affine-only writes preserve ordinary output");
            const u32 referenced = eightBit ? palette*256+1 : palette+1;
            engine.UploadedOBJPalette[referenced] ^= 0x8000;
            upload(); prepare(false); forced();
            engine.UploadedOBJPalette[referenced] ^= 0x1F;
            upload(); prepare(false);
            Check(read() != original, "ordinary palette animation reaches the output immediately");
            forced();
            // Different indices with identical RGB and opacity have identical
            // reconstruction inputs; do not lose this reuse opportunity.
            engine.UploadedOBJPalette[referenced+1] = engine.UploadedOBJPalette[referenced];
            upload(); prepare(true);
            engine.UploadedOBJVRAM[4096] = eightBit ? 2 : 0x22;
            upload(); prepare(true); forced();
            engine.UploadedOBJVRAM[4096] = 0;
            upload(); prepare(false); forced();
            const u32 transparent = eightBit ? palette*256 : palette;
            engine.UploadedOBJPalette[transparent] ^= 0x4210;
            upload(); prepare(false); forced();
            engine.SpriteConfig.uOAM[1].Position[0] += 3;
            upload(false); prepare(false); forced();
            engine.SpriteConfig.uOAM[1].Flip[0] = 1;
            upload(false); prepare(false); forced();
            engine.SpriteConfig.uOAM[1].BGPrio = 2;
            upload(false); prepare(false); forced();
            engine.SpriteConfig.uOAM[1].OBJMode = 1;
            upload(false); prepare(false); forced();
            engine.SpriteConfig.uOAM[1].Rotscale = 0;
            upload(false); prepare(false); forced();
            engine.SpriteConfig.uOAM[1].Rotscale = u32(-1);
            upload(false); prepare(false);
            engine.SpriteConfig.uOAM[1].Mosaic = 1;
            Check(!engine.RenderResolvedOrdinaryOBJProduct(0,192,enhancement,model), "unsupported ordinary material remains rejected");
            engine.SpriteConfig.uOAM[1].Mosaic = 0;
        }
        Check(glGetError() == GL_NO_ERROR, "ordinary surface cache GL operations");
        std::printf("[GL lifetime] ordinary surface engine %c algorithm %d passed\n",engineB?'B':'A',int(algorithm));
    }

    static void ReconstructionScratchStorage(GLRenderer& renderer, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        glActiveTexture(GL_TEXTURE0);
        const auto acquire = [&](int index, int w, int h) {
            Check(engine.EnsureCuNNyWorkTexture(index,w,h), "scratch allocation");
            GLint actualW = 0, actualH = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&actualW);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&actualH);
            Check(actualW == w && actualH == h, "scratch preserves exact sampling dimensions");
            return engine.CuNNyWorkTex[index];
        };
        // Animal Crossing: two CuNNy textures, then four NNEDI3 sizes. Once
        // warm, the whole sequence must allocate nothing and reuse every name.
        const int sizes[][3] = {{0,256,128},{1,256,128},{0,64,128},
                                {1,128,128},{0,128,256},{1,256,256}};
        GLuint warm[6]{};
        for (int i = 0; i < 6; ++i) warm[i] = acquire(sizes[i][0],sizes[i][1],sizes[i][2]);
        const auto created = Textures.Created;
        const auto storageCalls = TextureStorageCalls;
        for (int repeat = 0; repeat < 3; ++repeat)
            for (int i = 0; i < 6; ++i)
                Check(acquire(sizes[i][0],sizes[i][1],sizes[i][2]) == warm[i], "warm scratch reused");
        Check(Textures.Created == created, "warm reconstruction allocates no textures");
        Check(TextureStorageCalls == storageCalls, "warm reconstruction does not redefine texture storage");

        const GLuint input = acquire(1,8,8);
        std::array<float,8*8*4> pattern{}, read{};
        for (size_t i = 0; i < pattern.size(); ++i) pattern[i] = float(i%16)/16;
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_FLOAT,pattern.data());
        const auto bounded = [&] {
            for (auto& index : engine.CuNNyScratch)
            {
                u64 bytes = 0; int count = 0;
                for (auto& entry : index) { bytes += entry.Bytes(); count += entry.Texture != 0; }
                Check(bytes <= engine.ReconstructionScratchBudget, "retained scratch byte limit");
                Check(count <= engine.ReconstructionScratchEntries, "retained scratch entry limit");
            }
            Check(glIsTexture(input), "output eviction preserves other ping-pong input");
        };
        // Entry pressure, then byte pressure (two allocations cannot coexist).
        for (int i = 0; i < 12; ++i) { acquire(0,16+i,16); bounded(); }
        acquire(0,1024,1024); bounded();
        acquire(0,1024,1025); bounded();
        const GLuint recent = acquire(0,32,32);
        acquire(0,33,32); acquire(0,34,32); acquire(0,35,32);
        acquire(0,32,32); acquire(0,36,32);
        Check(acquire(0,32,32) == recent, "scratch eviction respects recency");
        bounded();

        const GLuint large = acquire(0,2048,1025);
        Check(acquire(0,2048,1025) == large, "oversized working allocation reused");
        for (auto& entry : engine.CuNNyScratch[0])
            Check(!entry.Texture, "oversized request releases ordinary pool for its index");
        bounded();
        acquire(0,2048,1026); bounded();
        acquire(0,8,8);
        Check(!engine.CuNNyLargeScratch[0].Texture, "oversized scratch released at ordinary sizes");
        glBindTexture(GL_TEXTURE_2D,input);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,read.data());
        Check(read == pattern, "eviction preserves input texels");
        engine.Reset();
        engine.SetScaleFactor(3); engine.SetScaleFactor(2);
        Check(acquire(1,8,8) == input, "reset and scale changes retain dimension-keyed scratch");

        const auto beforeInvalid = Textures.Created;
        Check(!engine.EnsureCuNNyWorkTexture(-1,8,8) &&
              !engine.EnsureCuNNyWorkTexture(2,8,8) &&
              !engine.EnsureCuNNyWorkTexture(0,0,8) &&
              !engine.EnsureCuNNyWorkTexture(0,8,-1), "invalid scratch requests rejected");
        GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
        Check(!engine.EnsureCuNNyWorkTexture(0,maximum+1,8), "unsupported scratch dimensions rejected");
        Check(Textures.Created == beforeInvalid, "invalid requests do not allocate");
        const auto live = Textures.Live.size();
        FailScratchAllocation = true;
        const bool allocated = engine.EnsureCuNNyWorkTexture(0,63,61);
        FailScratchAllocation = false;
        Check(!allocated && Textures.Live.size() == live, "failed scratch allocation releases its name");
        acquire(0,63,61);
        Check(glGetError() == GL_NO_ERROR, "scratch storage GL operations");
        std::printf("[GL lifetime] bounded scratch engine %c: reuse, eviction, dimensions, reset, failure passed\n",engineB?'B':'A');
    }

    static void BackgroundArtworkCache(GLRenderer& renderer, bool engineB,
                                       RendererSettings::GLScaleAlgorithm algorithm)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        engine.SetWholeSceneDebugViewsActive(true);
        engine.WholeSceneScaleRequested = true;
        engine.WholeSceneScaleHybridStrictAffineHighRes = true;
        engine.WholeSceneScaleAlgorithm = algorithm;
        engine.AffineAlpha = RendererSettings::ResolveAffineAlphaReconstruction(
            RendererSettings::AffineAlphaReconstruction::Automatic, algorithm);
        engine.WholeSceneScaleHybridStrictAffineSourceEnhancement = true;
        const auto enhancement = engine.CurrentStrictAffineSourceEnhancementDecision(true);
        const int model = RendererSettings::IsGLArtCNNAlgorithm(algorithm)
            ? RendererSettings::GetGLArtCNNModelIndex(algorithm)
            : RendererSettings::IsGLCuNNyAlgorithm(algorithm)
                ? RendererSettings::GetGLCuNNyModelIndex(algorithm) : -1;
        engine.LayerEnable = 4;
        engine.LayerConfig.uVRAMMask = engineB ? 127 : 511;
        std::vector<u8> vram((engine.LayerConfig.uVRAMMask+1)*1024);
        auto& bg = engine.LayerConfig.uBGConfig[2];
        const auto upload = [&] {
            NonStupidBitField<1024> dirty(0,1024);
            engine.BGVRAMUploadCache.FilterUnchanged(vram.data(),vram.size(),dirty);
            engine.BGVRAMUploadCache.RecordUpload(vram.data(),0,vram.size());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,engine.VRAMTex_BG);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1024,engine.LayerConfig.uVRAMMask+1,
                GL_RED_INTEGER,GL_UNSIGNED_BYTE,vram.data());
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D,engine.PalTex_BG);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,65,GL_RGBA,
                GL_UNSIGNED_SHORT_1_5_5_5_REV,engine.UploadedBGPalette);
            engine.UploadedBGPaletteValid = true;
            glBindBuffer(GL_UNIFORM_BUFFER,engine.LayerConfigUBO);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(engine.LayerConfig),&engine.LayerConfig);
            const int rows[4]{};
            engine.PrerenderDirtyLayers(4,0,rows,rows,0);
        };
        const auto readNative = [&] {
            std::vector<u8> pixels(bg.Size[0]*bg.Size[1]*4*2);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.BGLayerFB[2]);
            for (int channel = 0; channel < 2; ++channel)
            {
                glReadBuffer(GL_COLOR_ATTACHMENT0+channel);
                glReadPixels(0,0,bg.Size[0],bg.Size[1],GL_RGBA,GL_UNSIGNED_BYTE,
                    pixels.data()+channel*bg.Size[0]*bg.Size[1]*4);
            }
            return pixels;
        };
        const auto forcedNative = [&] {
            const auto exact = readNative();
            engine.NativeBGPrerenderTexture[2] = 0;
            const auto before = DrawArraysCalls;
            upload();
            Check(DrawArraysCalls == before+1, "lost native texture proof forces prerender");
            Check(readNative() == exact, "reused native BG color and metadata equal forced rendering");
        };
        const auto prepare = [&](StrictAffineSourceEnhancementDecision mode) {
            engine.WholeSceneTrace = {};
            Check(engine.PrepareStrictAffineEnhancedBGSources(mode,model,0,true,0,4,4) == 4,
                  "prepare enhanced tiled background");
        };
        const auto read = [&] {
            std::vector<u8> pixels(engine.EnhancedBGLayerWidth[2]*engine.EnhancedBGLayerHeight[2]*4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.EnhancedBGLayerFB[2]);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,0,engine.EnhancedBGLayerWidth[2],engine.EnhancedBGLayerHeight[2],
                GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            return pixels;
        };
        const auto forced = [&](StrictAffineSourceEnhancementDecision mode) {
            const auto cached = read();
            engine.EnhancedBGLayerSourceGeneration[2] = 0;
            PoisonScratchAfterEviction(engine);
            prepare(mode);
            Check(read() == cached,"cached background equals forced reconstruction byte for byte");
        };
        for (u32 type : {0u,1u,2u,3u})
        {
            bg = {}; bg.Type = type; bg.Size[0] = bg.Size[1] = 16;
            bg.TileOffset = vram.size()-32; bg.MapOffset = 8192;
            bg.PalOffset = type == 1 || type == 3 ? 49 : 0;
            std::fill(vram.begin(),vram.end(),0);
            std::fill(vram.end()-32,vram.end(),0x11);
            std::fill(vram.begin(),vram.begin()+128,0x11);
            if (type != 2)
                for (u32 y = 0; y < 2; ++y) for (u32 x = 0; x < 2; ++x)
                    vram[8193 + (y*(type <= 1 ? 32 : 2)+x)*2] = 0xF0;
            for (u32 i = 0; i < 256*65; ++i) engine.UploadedBGPalette[i] = i*71;
            engine.BGArtworkCache[2].Reset();
            upload(); prepare(enhancement); forced(enhancement);
            if (type <= 1)
            {
                engine.WholeSceneTrace.StrictAffineCandidateValid = true;
                engine.WholeSceneTrace.StrictAffineSourceEnhancement =
                    StrictAffineSourceEnhancementDecision::NativeCachedAffineSources;
                int width = 0, height = 0; std::vector<u32> debug;
                Check(engine.ReadWholeSceneDebugView(WholeScene2DDebugView::StrictAffineEnhancedBG2Source,
                      width,height,debug),
                      "text BG debug product retains its own reconstruction mode independently of affine sources");
            }
            const auto generation = engine.BGLayerArtworkGeneration[2];
            const auto native = engine.BGLayerSourceGeneration[2];
            vram[32768] ^= 1;
            engine.UploadedBGPalette[250] ^= 0x8000;
            const auto beforeReuse = DrawArraysCalls;
            upload();
            Check(DrawArraysCalls == beforeReuse, "unrelated writes skip native tile prerender");
            forcedNative();
            prepare(enhancement);
            Check(engine.BGLayerSourceGeneration[2] > native &&
                  engine.BGLayerArtworkGeneration[2] == generation &&
                  engine.WholeSceneTrace.StrictAffineEnhancedBGCacheHits == 1,
                  "unrelated uploaded writes reuse artwork while retaining native provenance");
            forced(enhancement);
            const u32 index = type == 0 ? 241 : type == 2 ? 17 : 64*256+17;
            engine.UploadedBGPalette[index] ^= 0x8000;
            const auto beforePalette = DrawArraysCalls;
            upload();
            Check(DrawArraysCalls == beforePalette+1, "referenced palette change redraws native tile immediately");
            forcedNative();
            prepare(enhancement);
            Check(engine.WholeSceneTrace.StrictAffineEnhancedBGCacheMisses == 1,
                  "referenced palette high bit invalidates the enhanced background");
            forced(enhancement);
            vram.back() ^= 0x11; // includes transparency and wrap across end of VRAM
            const auto beforeTile = DrawArraysCalls;
            upload();
            Check(DrawArraysCalls == beforeTile+1, "referenced tile change redraws native layer immediately");
            forcedNative();
            prepare(enhancement);
            Check(engine.WholeSceneTrace.StrictAffineEnhancedBGCacheMisses == 1,
                  "referenced tile animation invalidates immediately");
            forced(enhancement);
            vram[8192] = 1; if (type != 2) vram[8193] ^= 0x0C;
            upload(); prepare(enhancement);
            Check(engine.WholeSceneTrace.StrictAffineEnhancedBGCacheMisses == 1,
                  "tile selection and flips invalidate immediately");
            forced(enhancement);
            // Clamp can change without a native color change or prerender.
            bg.Clamp ^= 1; prepare(enhancement);
            Check(engine.WholeSceneTrace.StrictAffineEnhancedBGCacheMisses == 1,
                  "padding wrap/clamp is part of the reconstruction contract");
            forced(enhancement);
            prepare(StrictAffineSourceEnhancementDecision::NativeCachedAffineSources);
            Check(engine.WholeSceneTrace.StrictAffineEnhancedBGCacheMisses == 1,
                  "native RGB cache cannot alias a neural source at the same dimensions");
            forced(StrictAffineSourceEnhancementDecision::NativeCachedAffineSources);
            prepare(enhancement); forced(enhancement);

            // Reuse requires ownership of the backing texture, not just equal
            // source bytes. Another layer may select the same native storage.
            const auto exactNative = readNative();
            const auto otherBG = engine.LayerConfig.uBGConfig[3];
            const auto otherTex = engine.BGLayerTex[3];
            const auto otherMeta = engine.BGLayerMetaTex[3];
            const auto otherFB = engine.BGLayerFB[3];
            engine.LayerConfig.uBGConfig[3] = bg;
            engine.LayerConfig.uBGConfig[3].TileOffset += 32;
            engine.BGLayerTex[3] = engine.BGLayerTex[2];
            engine.BGLayerMetaTex[3] = engine.BGLayerMetaTex[2];
            engine.BGLayerFB[3] = engine.BGLayerFB[2];
            engine.BGArtworkCache[3].Reset();
            glBindBuffer(GL_UNIFORM_BUFFER,engine.LayerConfigUBO);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(engine.LayerConfig),&engine.LayerConfig);
            const int rows[4]{};
            engine.PrerenderDirtyLayers(8,0,rows,rows,0);
            Check(!engine.NativeBGPrerenderTexture[2], "another BG invalidates shared backing ownership");
            const auto beforeAlias = DrawArraysCalls;
            upload();
            Check(DrawArraysCalls == beforeAlias+1 && readNative() == exactNative,
                  "shared backing overwrite forces exact native restoration");
            engine.LayerConfig.uBGConfig[3] = otherBG;
            engine.BGLayerTex[3] = otherTex;
            engine.BGLayerMetaTex[3] = otherMeta;
            engine.BGLayerFB[3] = otherFB;
            engine.BGArtworkCache[3].Reset();
            engine.NativeBGPrerenderTexture[3] = 0;

            // A partial update must never publish a full native product.
            glUseProgram(engine.LayerPreShader);
            glBindBufferBase(GL_UNIFORM_BUFFER,20,engine.LayerConfigUBO);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,engine.VRAMTex_BG);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,engine.PalTex_BG);
            engine.PrerenderLayerRows(2,2,5);
            Check(!engine.NativeBGPrerenderTexture[2], "partial draw cannot publish full native reuse");
            const auto beforeFull = DrawArraysCalls;
            upload();
            Check(DrawArraysCalls == beforeFull+1 && readNative() == exactNative,
                  "full draw after partial update restores complete native product");
        }
        engine.SetWholeSceneDebugViewsActive(false);
    }
    static void SpriteArtworkCache(GLRenderer& renderer, bool engineB,
                                    RendererSettings::GLScaleAlgorithm algorithm)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        engine.WholeSceneScaleRequested = true;
        engine.WholeSceneScaleHybridStrictAffineHighRes = true;
        engine.WholeSceneScaleAlgorithm = algorithm;
        engine.AffineAlpha = RendererSettings::ResolveAffineAlphaReconstruction(
            RendererSettings::AffineAlphaReconstruction::Automatic, algorithm);
        engine.WholeSceneScaleHybridStrictAffineSourceEnhancement = true;
        engine.WholeScenePlan.SelectedPath = WholeSceneRenderPath::StrictAffineHighRes;
        const auto enhancement = engine.CurrentStrictAffineSourceEnhancementDecision(true);
        const int model = RendererSettings::IsGLArtCNNAlgorithm(algorithm)
            ? RendererSettings::GetGLArtCNNModelIndex(algorithm)
            : RendererSettings::IsGLCuNNyAlgorithm(algorithm)
                ? RendererSettings::GetGLCuNNyModelIndex(algorithm) : -1;
        engine.NumSprites = 2;
        engine.SpriteConfig.uVRAMMask = engineB ? 127 : 255;
        engine.UploadedOBJVRAM.resize((engine.SpriteConfig.uVRAMMask+1)*1024);
        engine.SpriteConfig.uRotscale[0][0] = engine.SpriteConfig.uRotscale[0][3] = 256;
        engine.SpriteConfig.uRotscale[0][1] = engine.SpriteConfig.uRotscale[0][2] = 0;
        const auto upload = [&] {
            ++engine.SpriteSourceGeneration;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, engine.VRAMTex_OBJ);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1024,engine.SpriteConfig.uVRAMMask+1,
                GL_RED_INTEGER,GL_UNSIGNED_BYTE,engine.UploadedOBJVRAM.data());
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, engine.PalTex_OBJ);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,17,GL_RGBA,
                GL_UNSIGNED_SHORT_1_5_5_5_REV,engine.UploadedOBJPalette);
            glBindBuffer(GL_UNIFORM_BUFFER,engine.SpriteConfigUBO);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(engine.SpriteConfig),&engine.SpriteConfig);
            engine.PrerenderSprites();
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.SpriteFB);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            for (int slot = 0; slot < 2; ++slot)
            {
                const auto& source = engine.SpriteConfig.uOAM[slot];
                const auto& native = engine.SpriteArtworkPixels[slot];
                std::vector<unsigned char> pixels(source.Size[0]*source.Size[1]*4);
                glReadPixels(slot*64,0,source.Size[0],source.Size[1],GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                Check(native.size()*4 == pixels.size(), "native sprite dependency image has the shader's dimensions");
                // Recover the exact DS 6-bit channel codes. The driver's
                // RGB5_A1 fetch and RGBA8 store can quantize the intermediate
                // float differently from a CPU float-to-byte calculation.
                const auto channel = [](u32 value) { return (value*63+127)/255; };
                for (size_t p = 0; p < native.size(); ++p)
                {
                    const u32 color = native[p];
                    const u32 r = (color&31)*2;
                    const u32 g = ((color>>5)&31)*2 + (source.Type == 2 ? 0 : (color>>15)&1);
                    const u32 b = ((color>>10)&31)*2;
                    const bool matches = channel(pixels[p*4]) == r && channel(pixels[p*4+1]) == g && channel(pixels[p*4+2]) == b &&
                          pixels[p*4+3] == ((source.Type == 2 ? color&(1u<<15) : color&(1u<<16)) ? 255 : 0);
                    Check(matches,
                          "exact dependency pixels match native SpritePreShader output, including transparency and green high bit");
                }
            }
        };
        const auto prepare = [&] {
            engine.WholeSceneTrace = {};
            Check(engine.PrepareStrictAffineEnhancedOBJSources(enhancement,model), "prepare enhanced sprite sources");
        };
        const auto read = [&] {
            std::array<unsigned char,2*64*32*4> pixels{};
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.EnhancedSpriteFB);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            for (int slot = 0; slot < 2; ++slot)
            {
                const int width = engine.EnhancedOBJSourceRegions[slot][2] ? 64 : 32;
                glReadPixels(slot*128,0,width,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()+slot*64*32*4);
            }
            return pixels;
        };
        const auto checkForcedRebuild = [&] {
            const auto reused = read();
            engine.InvalidateEnhancedOBJSources();
            PoisonScratchAfterEviction(engine);
            prepare();
            Check(read() == reused, "reused sprite pixels exactly match forced reconstruction of current artwork");
        };
        for (int variant = 0; variant < 5; ++variant)
        {
            const u32 type = variant == 0 ? 0 : variant == 4 ? 2 : 1;
            const u32 palette = type == 0 ? 32 : variant == 1 ? 0 : variant == 2 ? 1 : 16;
            engine.SetWholeSceneScaleHybridStrictAffineConnectedSources(false);
            std::fill(engine.UploadedOBJVRAM.begin(),engine.UploadedOBJVRAM.end(),type == 2 ? 0x91 : 0x11);
            for (int p = 0; p < 256*17; ++p) engine.UploadedOBJPalette[p] = (p*71) & 0xFFFF;
            const u32 mask = (engine.SpriteConfig.uVRAMMask<<10)|1023;
            for (int slot = 0; slot < 2; ++slot)
            {
                auto& source = engine.SpriteConfig.uOAM[slot];
                source = {};
                source.Type = type; source.Size[0] = source.Size[1] = 16;
                source.BoundSize[0] = source.BoundSize[1] = 16;
                source.TileStride = type == 0 ? 128 : 256;
                source.TileOffset = slot ? 8192 : type == 2 ? mask-31 : 0;
                source.PalOffset = palette;
                source.Position[0] = slot*16;
                engine.SpriteOAMIndex[slot] = slot;
            }
            upload(); prepare();
            const auto original = read();
            engine.WholeSceneTrace = {};
            Check(engine.PrepareStrictAffineEnhancedOBJSources(
                    StrictAffineSourceEnhancementDecision::NativeCachedAffineSources,-1) &&
                  engine.WholeSceneTrace.StrictAffineEnhancedOBJCacheMisses == 2,
                  "native RGB mode cannot reuse a neural/scaler reconstruction of the same artwork");
            prepare();
            Check(engine.WholeSceneTrace.StrictAffineEnhancedOBJCacheMisses == 2 && read() == original,
                  "restoring the scaler rebuilds its own source mode with exact original pixels");
            const auto key0 = engine.BuildEnhancedOBJSourceKey(0,enhancement);
            const auto key1 = engine.BuildEnhancedOBJSourceKey(1,enhancement);
            engine.UploadedOBJVRAM[mask/2] ^= 0xFF;
            engine.UploadedOBJPalette[type == 1 ? palette*256+200 : 47] ^= 0xFFFF;
            upload(); prepare();
            Check(engine.BuildEnhancedOBJSourceKey(0,enhancement) == key0 &&
                  engine.BuildEnhancedOBJSourceKey(1,enhancement) == key1 &&
                  engine.WholeSceneTrace.StrictAffineEnhancedOBJCacheHits == 2,
                  "unrelated writes and unused palette colours retain exact artwork cache hits");
            Check(read() == original, "unrelated writes preserve displayed sprite pixels");
            engine.UploadedOBJVRAM[engine.SpriteConfig.uOAM[0].TileOffset] ^= 0xFF;
            upload(); prepare();
            Check(!(engine.BuildEnhancedOBJSourceKey(0,enhancement) == key0) &&
                  engine.BuildEnhancedOBJSourceKey(1,enhancement) == key1 &&
                  engine.WholeSceneTrace.StrictAffineEnhancedOBJCacheMisses == 1,
                  "changed source bytes invalidate their own sprite, including VRAM wrap");
            Check(read() != original, "visible source animation changes the enhanced pixels immediately");
            checkForcedRebuild();
            const auto beforePalette = engine.BuildEnhancedOBJSourceKey(1,enhancement);
            const u32 paletteIndex = type == 0 ? 33 : palette*256+0x11;
            engine.UploadedOBJPalette[paletteIndex] ^= 0x8000;
            upload(); prepare();
            Check((engine.BuildEnhancedOBJSourceKey(1,enhancement) == beforePalette) == (type == 2),
                  "referenced palette changes invalidate indexed art; bitmap art ignores palettes");
            checkForcedRebuild();
            const auto beforeMotion = engine.BuildEnhancedOBJSourceKey(1,enhancement);
            engine.SpriteConfig.uOAM[1].Position[0] += 3;
            engine.SpriteConfig.uRotscale[0][0] = 251;
            upload(); prepare();
            Check(engine.BuildEnhancedOBJSourceKey(1,enhancement) == beforeMotion &&
                  engine.WholeSceneTrace.StrictAffineEnhancedOBJCacheHits == 2,
                  "position and affine matrix changes reuse source reconstruction");
            checkForcedRebuild();
        }
        engine.SpriteConfig.uRotscale[0][0] = 256;
        for (int slot = 0; slot < 2; ++slot)
        {
            auto& source = engine.SpriteConfig.uOAM[slot];
            source.Type = 0; source.TileOffset = slot*64; source.TileStride = 128;
            source.PalOffset = 0; source.Position[0] = slot*16;
        }
        engine.SetWholeSceneScaleHybridStrictAffineConnectedSources(true);
        upload(); prepare();
        Check(engine.ConnectedOBJSourceHashes[0] != 0, "test exercises a connected sprite pair");
        const auto pairKey = engine.BuildEnhancedOBJSourceKey(0,enhancement);
        engine.UploadedOBJVRAM[64] ^= 0xFF;
        upload();
        Check(!(engine.BuildEnhancedOBJSourceKey(0,enhancement) == pairKey),
              "connected key notices changed partner before pair preparation runs again");
        prepare();
        Check(!(engine.BuildEnhancedOBJSourceKey(0,enhancement) == pairKey) &&
              engine.WholeSceneTrace.StrictAffineEnhancedOBJNeuralDispatches == 1 &&
              engine.WholeSceneTrace.StrictAffineEnhancedOBJCacheMisses == 2,
              "non-root artwork change invalidates the connected pair atomically");
        checkForcedRebuild();
        engine.SetWholeSceneScaleHybridStrictAffineConnectedSources(false);
        // Concentric assemblies cache a union: every member's artwork must
        // participate, not just the root whose metadata supplies the key.
        engine.DispCnt = 0;
        engine.CompositorConfig.uBlendEffect = 0;
        for (auto& row : engine.ScanlineConfig.uScanline)
        { row.WinRegs = 0xFFFFFFFFu; row.BlendEffect = 0; }
        engine.SpriteConfig.uOAM[1].Position[0] = 0;
        upload(); prepare();
        const auto assemble = [&] {
            u32 mask[4] = {~0u,~0u,~0u,~0u};
            int assemblySlots[128];
            engine.WholeSceneExecutionTrace.OrderedOpaqueAssemblyRebuildCount = 0;
            Check(engine.PrepareOpaqueOBJAssemblies(mask,assemblySlots,0,192) && assemblySlots[1] == 0,
                  "test exercises a concentric assembly with sprite 1 as root");
            std::array<unsigned char,32*32*4> pixels{};
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.OpaqueOBJAssemblyFB);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            return pixels;
        };
        const auto assemblyPixels = assemble();
        const auto assemblyKey = engine.OpaqueOBJAssemblyKeys[0];
        const auto rootGeneration = engine.SpriteArtworkGeneration[1];
        engine.UploadedOBJVRAM[0] ^= 0xFF;
        upload(); prepare();
        const auto changedAssemblyPixels = assemble();
        Check(engine.SpriteArtworkGeneration[1] == rootGeneration &&
              !(engine.OpaqueOBJAssemblyKeys[0] == assemblyKey) &&
              engine.WholeSceneExecutionTrace.OrderedOpaqueAssemblyRebuildCount == 1,
              "non-root artwork change invalidates its assembly without changing root artwork");
        Check(changedAssemblyPixels != assemblyPixels, "assembly animation updates visible artwork immediately");
        engine.OpaqueOBJAssemblyValid[0] = false;
        Check(assemble() == changedAssemblyPixels, "assembly cache matches a forced reconstruction");
        // Compare native decoding against the actual shader with varied bytes,
        // transparency, all legal extents and tile/bitmap address wrap.
        for (size_t p = 0; p < engine.UploadedOBJVRAM.size(); ++p)
            engine.UploadedOBJVRAM[p] = (p*73+(p>>3)) & 255;
        for (u32 type : {0u,1u,2u}) for (int extent : {8,16,32,64})
        {
            for (int slot = 0; slot < 2; ++slot)
            {
                auto& source = engine.SpriteConfig.uOAM[slot];
                source.Type = type; source.Size[0] = source.Size[1] = extent;
                source.TileOffset = slot ? 8192 : (engine.SpriteConfig.uVRAMMask<<10)+992;
                source.TileStride = type == 2 ? 256 : 1024;
                source.PalOffset = type == 0 ? 240 : 16;
            }
            upload(); // verifies every native pixel against SpritePreShader
        }
        const auto beforeDisabled = engine.BuildEnhancedOBJSourceKey(0,enhancement);
        engine.WholeSceneScaleRequested = false;
        engine.UpdateSpriteArtworkGenerations();
        Check(engine.SpriteArtworkPixels[0].empty() &&
              !(engine.BuildEnhancedOBJSourceKey(0,enhancement) == beforeDisabled),
              "disabled artwork enhancement skips pixel comparison and fails closed for old cache entries");
        engine.WholeSceneScaleRequested = true;
    }
    static void HistoricalBGPreparation(GLRenderer& renderer, bool engineB,
                                        RendererSettings::GLScaleAlgorithm algorithm)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        engine.WholeSceneScaleHybridStrictAffineHighRes = true;
        engine.WholeSceneScaleMode = RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
        engine.WholeSceneScaleAlgorithm = algorithm;
        engine.AffineAlpha = RendererSettings::ResolveAffineAlphaReconstruction(
            RendererSettings::AffineAlphaReconstruction::Automatic, algorithm);
        engine.WholeSceneScaleHybridStrictAffineSourceEnhancement = true;
        engine.WholeSceneScaleHybridStrictAffineTopTextBG = true;
        const auto enhancement = engine.CurrentStrictAffineSourceEnhancementDecision(true);
        const int model = RendererSettings::IsGLArtCNNAlgorithm(algorithm)
            ? RendererSettings::GetGLArtCNNModelIndex(algorithm)
            : RendererSettings::IsGLCuNNyAlgorithm(algorithm)
                ? RendererSettings::GetGLCuNNyModelIndex(algorithm) : -1;
        engine.LayerEnable = 4;
        auto& bg = engine.LayerConfig.uBGConfig[2];
        bg.Type = 0; bg.Size[0] = bg.Size[1] = 16; bg.Clamp = 1;
        for (auto& row : engine.ScanlineConfig.uScanline)
        {
            for (auto& priority : row.BGPrio) priority = 0xFFFFFFFFu;
            row.BGPrio[2] = 0;
        }
        const GLfloat red[] = {1,0,0,1}, green[] = {0,1,0,1}, blue[] = {0,0,1,1};
        std::array<unsigned char,32*32*4> referenceEnhanced{};
        const auto fill = [&](const GLfloat* color, const GLfloat* meta) {
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,engine.BGLayerFB[2]);
            const GLenum outputs[] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
            glDrawBuffers(2,outputs);
            glClearBufferfv(GL_COLOR,0,color); glClearBufferfv(GL_COLOR,1,meta);
        };
        // Exercise rejected and still-supported frames on the actual GL path.
        // A stale enhanced copy from the preceding frame must never look ready.
        for (int scenario = 0; scenario < 9; ++scenario)
        {
            engine.ResetWholeSceneNativeProductTracking();
            engine.WholeSceneCurrentUpdateDebugTrace = {};
            auto& trace = engine.WholeSceneCurrentUpdateDebugTrace;
            switch (scenario)
            {
            case 0: trace.StateDirtyReasonMask = 1u<<8; break; // parent partial render (Joker)
            case 1: trace.StateDirtyMiscDiffMask = 1u<<6; break;
            case 2: trace.StateDirtyDispCntDiff = 1u<<3; break;
            case 3: trace.StateDirtyBGCntDiff[2] = 1; break;
            case 4: trace.StateDirtyReasonMask = 1u<<31; break; // unknown changes fail closed
            case 5: break; // next clean frame can still need historical data
            case 6: // allowed layer-enable and blend transitions
                trace.StateDirtyDispCntDiff = 0x1F00;
                trace.StateDirtyReasonMask = (1u<<2)|(1u<<7);
                trace.StateDirtyMiscDiffMask = 0x3C;
                break;
            case 7: // allowed changes cannot undo an accumulated rejection
                trace.StateDirtyReasonMask = (1u<<8)|(1u<<2)|(1u<<7);
                break;
            case 8: bg.Type = 2; break; // cold owning affine BGs retain eager preparation
            }
            fill(red,green);
            ++engine.BGLayerSourceGeneration[2];
            ++engine.BGLayerArtworkGeneration[2];
            if (scenario == 5)
                Check(engine.PrepareStrictAffineEnhancedBGSources(
                    enhancement,model,0,false,0,4,4) == 4, "prepare a reusable enhanced source");
            const auto oldEnhancedGeneration = engine.EnhancedBGLayerSourceGeneration[2];
            engine.PreserveDeferredStrictAffineHistoricalSourceEpoch(64,4,0,0,4,0);
            const auto& epoch = engine.DeferredStrictAffineHistoricalSourceEpochs[0];
            Check(epoch.Valid && epoch.NativeBGMask == 4 && epoch.YEnd == 64,
                  "historical native source/provenance survives early enhancement rejection");
            Check(epoch.EnhancedBGMask == (scenario == 5 || scenario == 8 ? 4u : 0u),
                  "tile cache hits and owning affine sources are prepared at the historical boundary");
            Check(scenario == 8 ? engine.EnhancedBGLayerSourceGeneration[2] == engine.BGLayerArtworkGeneration[2]
                                : engine.EnhancedBGLayerSourceGeneration[2] == oldEnhancedGeneration,
                  "only an owning affine source may eagerly reconstruct a cache miss");
            fill(blue,red); // later palette/source contents must not replace the snapshot
            if (scenario == 6)
            {
                Check(engine.PrepareDeferredStrictAffineHistoricalEnhancedBGs(
                    4,enhancement,model),
                    "selected historical source is reconstructed on demand");
                Check(epoch.EnhancedBGMask == 4 &&
                      engine.EnhancedBGLayerSourceGeneration[2] == oldEnhancedGeneration,
                      "on-demand reconstruction publishes only the historical product");
            }
            if (scenario == 5 || scenario == 6 || scenario == 8)
            {
                std::array<unsigned char,32*32*4> pixels{};
                glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.DeferredStrictAffineHistoricalEnhancedBGLayerFB[0][2]);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                if (scenario == 5) referenceEnhanced = pixels;
                else Check(pixels == referenceEnhanced,
                    "late reconstruction uses frozen red pixels, not the new blue source, with exact cached-image parity");
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER,engine.DeferredStrictAffineHistoricalBGLayerFB[0][2]);
            for (int attachment = 0; attachment < 2; ++attachment)
            {
                std::array<unsigned char,16*16*4> pixels{};
                glReadBuffer(GL_COLOR_ATTACHMENT0+attachment);
                glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                for (int p = 0; p < 16*16; ++p)
                    Check(pixels[p*4] == (attachment ? 0 : 255) &&
                          pixels[p*4+1] == (attachment ? 255 : 0) &&
                          pixels[p*4+2] == 0 && pixels[p*4+3] == 255,
                          "historical colors and ownership metadata remain frozen after a source mutation");
            }
        }
    }
    static void TightBGUploads(GLRenderer& renderer, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        u8* vram = nullptr; u32 mask = 0;
        engine.GPU2D.GetBGVRAM(vram,mask);
        const u32 size = mask+1, blocks = size/512;
        for (u32 i = 0; i < size; ++i) vram[i] = static_cast<u8>(i*71+(i>>9));
        engine.BGVRAMUploadCache.Reset();
        NonStupidBitField<1024> all(0,1024);
        engine.BGVRAMUploadCache.FilterUnchanged(vram,size,all);
        glActiveTexture(GL_TEXTURE0);
        engine.UploadBGVRAM(all,0);
        const auto readExact = [&] {
            std::vector<u8> gpu(size);
            glBindTexture(GL_TEXTURE_2D,engine.VRAMTex_BG);
            glGetTexImage(GL_TEXTURE_2D,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,gpu.data());
            Check(std::memcmp(gpu.data(),vram,size) == 0, "tight BG uploads preserve every VRAM byte");
            Check(engine.BGVRAMUploadCache.UploadedBytes() == gpu, "uploaded byte mirror matches real GL texture");
        };
        readExact();
        const auto update = [&](std::initializer_list<u32> changed,
                                std::initializer_list<UploadRectangle> expected) {
            NonStupidBitField<1024> dirty;
            for (u32 bit : changed) { vram[bit*512+17] ^= 0x55; dirty[bit] = true; }
            VRAMUploads.clear(); CollectVRAMUploads = true;
            engine.UploadBGVRAM(dirty,13);
            CollectVRAMUploads = false;
            Check(VRAMUploads.size() == expected.size(), "tight BG upload run count");
            size_t index = 0;
            for (const auto& rectangle : expected)
            {
                const auto& actual = VRAMUploads[index++];
                Check(actual.Y == rectangle.Y && actual.Width == rectangle.Width && actual.Height == rectangle.Height,
                      "tight BG upload rectangle");
            }
            readExact();
        };
        update({3},{{1,1024,1}});                     // Odd half of a row.
        update({0,1},{{0,1024,1}});                   // Both halves share a transfer.
        update({62,63,64,65},{{31,1024,2}});          // Cross old 32 KiB boundary.
        update({0,80,blocks-1},{{0,1024,41},{static_cast<GLint>(blocks/2-1),1024,1}});
        // Sparse dirty rows retain one transfer per old contiguous-word run.
        update({0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60},{{0,1024,31}});
        update({},{});
        Check(glGetError() == GL_NO_ERROR, "tight BG upload GL state");
    }

    static void ExtendedBGPaletteSlots(GLRenderer& renderer, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        auto& gpu = engine.GPU;
        auto& regs = engine.GPU2D;
        gpu.ScreensEnabled = true;
        gpu.PaletteDirty = 0;
        regs.Enabled = true;
        regs.ForcedBlank = 0;
        regs.DispCnt = (1u << 30) | (1u << 16) | 0xF00;
        regs.LayerEnable = 0xF;
        // Four 256-colour text BGs, each using its corresponding palette slot.
        for (auto& cnt : regs.BGCnt) cnt = 1u << 7;
        auto* mapping = engineB ? gpu.VRAMMap_BBGExtPal : gpu.VRAMMap_ABGExtPal;
        const u32 bank = engineB ? 7 : 4;
        for (int slot = 0; slot < 4; ++slot) mapping[slot] = 1u << bank;
        engine.UpdateAndRender(0);
        engine.UpdateAndRender(0);
        for (u32 bit = 0; bit < 64; ++bit)
        {
            engine.WholeSceneCurrentUpdateDebugTrace = {};
            gpu.VRAMDirty[bank][bit] = true;
            engine.UpdateAndRender(0);
            Check(engine.WholeSceneCurrentUpdateDebugTrace.PaletteLayerDirtyMask == (1u << (bit / 16)),
                  "extended palette update invalidates only its own BG slot");
            engine.WholeSceneCurrentUpdateDebugTrace = {};
            engine.UpdateAndRender(0);
            Check(engine.WholeSceneCurrentUpdateDebugTrace.PaletteLayerDirtyMask == 0,
                  "clean extended palettes do not invalidate BG layers");
        }
    }
    static void FailedLazyProgram(GLRenderer& renderer, int scaler, bool engineB)
    {
        auto& engine = static_cast<GLRenderer2D&>(*(engineB ? renderer.Rend2D_B : renderer.Rend2D_A));
        FailCompileAt = CompileCalls + 2;
        bool ok = scaler == 0 ? engine.EnsureArtCNNComputePrograms()
                : scaler == 1 ? engine.EnsureNNEDI3ComputePrograms()
                              : engine.EnsureCuNNyPrograms();
        FailCompileAt = 0;
        Check(!ok, "injected lazy scaler compilation failure");
    }
    static void BothScalers(GLRenderer& renderer, bool aFirst)
    {
        auto& a = static_cast<GLRenderer2D&>(*renderer.Rend2D_A);
        auto& b = static_cast<GLRenderer2D&>(*renderer.Rend2D_B);
        Scalers(aFirst ? a : b);
        Scalers(aFirst ? b : a);
    }
    static void Scalers(GLRenderer2D& engine)
    {
        Check(engine.EnsureArtCNNComputePrograms(), "lazy ArtCNN compilation");
        Check(engine.EnsureNNEDI3ComputePrograms(), "lazy NNEDI3 compilation");
        Check(engine.EnsureCuNNyPrograms(), "lazy CuNNy compilation");
        engine.EnsureReconstructionAlphaTextures(8, 8, 2);
        GLuint textures[2]{};
        glGenTextures(2, textures);
        for (GLuint texture : textures)
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 384, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        for (int algorithm = 0; algorithm < 6; ++algorithm)
        {
            engine.SetWholeSceneScaleAlgorithm(RendererSettings::GetGLScaleAlgorithm(algorithm));
            engine.RenderNativeFinalUpscaleToTexture(textures[0], textures[1]);
        }
        glDeleteTextures(2, textures);
    }
};
}

int RunGLRendererLifetimeTests()
{
    using namespace melonDS;
    try
    {
        QSurfaceFormat format;
        format.setVersion(4, 3); format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context; context.setFormat(format);
        Check(context.create(), "create GL context");
        QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
        Check(context.makeCurrent(&surface), "make GL context current");
        Check(gladLoadGLLoader([](const char* name) -> void* {
            return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
        }), "load OpenGL entry points");
        InstallHooks();
        WideMelon::Configure(256, 2, 192, false, false);
        TestShaderHelpers();
        {
            // 0x8080 looks like an unsynced capture if the GPU flags are not
            // initialized. Construction must not sync through a null renderer.
            void* storage = ::operator new(sizeof(NDS));
            std::memset(storage, 0x80, sizeof(NDS));
            NDS* console = new (storage) NDS();
            console->~NDS();
            ::operator delete(storage);
        }
        CheckReleased("GPU construction with nonzero capture-flag storage");
        auto nds = std::make_unique<NDS>();
        nds->GPU.Reset();
        for (bool compute : {false, true})
        {
            PoisonedConstruction<GLRenderer>(*nds, compute);
            {
                GLRenderer parent(*nds, compute);
                PoisonedConstruction<GLRenderer2D>(nds->GPU.GPU2D_A, parent);
                PoisonedConstruction<GLRenderer2D>(nds->GPU.GPU2D_B, parent);
                PoisonedConstruction<GLRenderer3D>(nds->GPU.GPU3D, parent);
                PoisonedConstruction<ComputeRenderer3D>(nds->GPU.GPU3D, parent);
            }
            CheckReleased("nonzero-memory construction without Init");

            // Record all initialization compile calls, then inject failures in
            // parent shaders, early/late shared 2D shaders and classic 3D shaders.
            CompileCalls = 0;
            int initCompiles = 0;
            {
                GLRenderer renderer(*nds, compute);
                Check(renderer.Init(), "normal renderer initialization");
                initCompiles = CompileCalls;
                RendererSettings settings{};
                settings.ScaleFactor = 2;
                renderer.SetRenderSettings(settings);
                GLRendererLifetimeTestAccess::CaptureProbeIsolation(renderer);
                GLRendererLifetimeTestAccess::PostprocessingNativeArtwork(renderer, false);
                GLRendererLifetimeTestAccess::PostprocessingNativeArtwork(renderer, true);
                GLRendererLifetimeTestAccess::BitmapCoveragePriority(renderer);
                GLRendererLifetimeTestAccess::CaptureSpriteInvalidation(renderer);
                GLRendererLifetimeTestAccess::CaptureColorPrecision(renderer);
                GLRendererLifetimeTestAccess::CopiedCaptureStorage(renderer);
                GLRendererLifetimeTestAccess::BitmapRowReference(renderer, false);
                GLRendererLifetimeTestAccess::BitmapRowReference(renderer, true);
                if (!compute) GLRendererLifetimeTestAccess::UniformUploads(renderer);
                GLRendererLifetimeTestAccess::ResultAlphaScratchStorage(renderer, false);
                GLRendererLifetimeTestAccess::ResultAlphaScratchStorage(renderer, true);
                GLRendererLifetimeTestAccess::ReconstructionScratchStorage(renderer, false);
                GLRendererLifetimeTestAccess::ReconstructionScratchStorage(renderer, true);
                GLRendererLifetimeTestAccess::TightBGUploads(renderer, false);
                GLRendererLifetimeTestAccess::TightBGUploads(renderer, true);
                GLRendererLifetimeTestAccess::ExtendedBGPaletteSlots(renderer, false);
                GLRendererLifetimeTestAccess::ExtendedBGPaletteSlots(renderer, true);
                for (auto algorithm : {RendererSettings::GLScaleAlgorithm::CuNNy4x32,
                    RendererSettings::GLScaleAlgorithm::ArtCNN, RendererSettings::GLScaleAlgorithm::NNEDI3,
                    RendererSettings::GLScaleAlgorithm::XBRZ})
                {
                    GLRendererLifetimeTestAccess::OrdinarySpriteSurface(renderer, false, algorithm);
                    GLRendererLifetimeTestAccess::OrdinarySpriteSurface(renderer, true, algorithm);
                    GLRendererLifetimeTestAccess::HistoricalBGPreparation(renderer, false, algorithm);
                    GLRendererLifetimeTestAccess::HistoricalBGPreparation(renderer, true, algorithm);
                    GLRendererLifetimeTestAccess::SpriteArtworkCache(renderer, false, algorithm);
                    GLRendererLifetimeTestAccess::SpriteArtworkCache(renderer, true, algorithm);
                    GLRendererLifetimeTestAccess::BackgroundArtworkCache(renderer, false, algorithm);
                    GLRendererLifetimeTestAccess::BackgroundArtworkCache(renderer, true, algorithm);
                }
            }
            CheckReleased("normal initialization");
            nds->GPU.Reset();
            for (int failure : {1, 2, 8, 17, initCompiles - 1, initCompiles})
            {
                CompileCalls = 0; FailCompileAt = failure;
                nds->GPU.SetRenderer(std::make_unique<GLRenderer>(*nds, compute));
                Check(dynamic_cast<SoftRenderer*>(&nds->GPU.GetRenderer()), "failed initialization selects Software");
                FailCompileAt = 0;
                CheckReleased("failed initialization and software fallback");
            }
        }

        for (int scaler = 0; scaler < 3; ++scaler)
        {
            for (bool engineB : {false, true})
            {
                {
                    GLRenderer renderer(*nds, true);
                    Check(renderer.Init(), "initialize before lazy failure");
                    GLRendererLifetimeTestAccess::FailedLazyProgram(renderer, scaler, engineB);
                }
                CheckReleased("partial lazy scaler failure, either engine");
            }
        }

        // Recreate both backends in one surviving context, including scale
        // changes and interrupted asynchronous Compute shader compilation.
        for (int cycle = 0; cycle < 4; ++cycle)
        {
            for (bool compute : {false, true})
            {
                nds->GPU.SetRenderer(std::make_unique<GLRenderer>(*nds, compute));
                auto* renderer = dynamic_cast<GLRenderer*>(&nds->GPU.GetRenderer());
                Check(renderer != nullptr, "renderer switch succeeds");
                RendererSettings settings{};
                settings.ScaleFactor = 2;
                settings.HiresCoordinates = true;
                settings.WholeScene2D.Enabled = true;
                settings.WholeScene2D.Mode = RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
                renderer->SetRenderSettings(settings);
                int current = 0, count = 0;
                for (int i = 0; renderer->NeedsShaderCompile() && i < (cycle == 0 ? 3 : 100); ++i)
                    renderer->ShaderCompileStep(current, count);
                if (cycle != 0) Check(!renderer->NeedsShaderCompile(), "Compute compilation completes");
                GLRendererLifetimeTestAccess::BothScalers(*renderer, cycle % 2 != 0);
                settings.ScaleFactor = 1;
                renderer->SetRenderSettings(settings);
                settings.ScaleFactor = 2;
                renderer->SetRenderSettings(settings);
                if (compute) renderer->ShaderCompileStep(current, count);
                nds->GPU.SetRenderer(nullptr);
                CheckReleased("renderer/scaler/scale switches");
            }
        }
        nds.reset();
        // A game restart may change widescreen dimensions and capture overlap
        // without replacing the GL context. Teardown must precede the change.
        const auto originalWide = WideMelon::CurrentConfiguration();
        for (bool compute : {false, true})
        for (const auto config : {
            WideMelon::Configuration{342, 192, 0, true, false},
            WideMelon::Configuration{682, 192, 2, true, true},
            WideMelon::Configuration{256, 342, 1, true, true},
            WideMelon::Configuration{256, 342, 2, false, false},
            WideMelon::Configuration{384, 288, 2, true, true},
            WideMelon::Configuration{512, 288, 0, false, false},
            WideMelon::Configuration{384, 512, 1, true, false},
            WideMelon::Configuration{256, 192, 0, true, false}})
        {
            WideMelon::ApplyConfiguration(config);
            nds = std::make_unique<NDS>();
            nds->Reset();
            nds->SetRenderer(std::make_unique<GLRenderer>(*nds, compute));
            auto* renderer = dynamic_cast<GLRenderer*>(&nds->GetRenderer());
            Check(renderer != nullptr, "widescreen restart renderer initialization");
            RendererSettings settings{};
            settings.ScaleFactor = 2;
            renderer->SetRenderSettings(settings);
            int current = 0, count = 0;
            while (renderer->NeedsShaderCompile()) renderer->ShaderCompileStep(current, count);
            void* top = nullptr;
            void* bottom = nullptr;
            Check(!renderer->GetFramebuffers(&top, &bottom) && top, "widescreen restart GL framebuffer");
            glBindTexture(GL_TEXTURE_2D_ARRAY, *static_cast<GLuint*>(top));
            GLint width = 0, height = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &height);
            Check(width == config.Width * 2 && height == config.Height * 2,
                  "widescreen restart allocates the requested output dimensions");
            if (config.Width==384 && config.Height==288)
                GLRendererLifetimeTestAccess::RectangularBorders(*renderer);
            if (WideMelon::Enabled())
                GLRendererLifetimeTestAccess::CopiedWideCaptureStorage(*renderer);
            nds.reset();
            CheckReleased("widescreen restart");
        }
        WideMelon::ApplyConfiguration(originalWide);
        CheckReleased("shutdown");
        std::printf("[GL lifetime] PASS: %zu programs, %zu shaders, %zu textures, %zu buffers, %zu framebuffers, %zu vertex arrays, %zu samplers\n",
                    Programs.Created, Shaders.Created, Textures.Created, Buffers.Created,
                    Framebuffers.Created, VertexArrays.Created, Samplers.Created);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[GL lifetime] FAIL: %s\n", error.what());
        return 1;
    }
}
