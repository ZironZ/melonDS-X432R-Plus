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
                GLRendererLifetimeTestAccess::ExtendedBGPaletteSlots(renderer, false);
                GLRendererLifetimeTestAccess::ExtendedBGPaletteSlots(renderer, true);
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
