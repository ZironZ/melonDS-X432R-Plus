// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QFile>
#include <array>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "../../src/GPU3D_TextureScaleRegion.h"

namespace {
void Require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int RunTextureScaleRegionTests(bool corruptOrigin)
{
    using namespace melonDS;
    constexpr int width = 256, height = 128;
    const auto full = MakeTextureScaleRegion(width, height, nullptr);
    TextureSamplingBounds invalid {true, true, 10, 10, 9, 12};
    Require(!MakeTextureScaleRegion(width, height, &invalid).Restricted(), "reject inverted bounds");
    invalid = {true, true, 0, 0, 257, 128};
    Require(!MakeTextureScaleRegion(width, height, &invalid).Restricted(), "reject out-of-sheet bounds");
    invalid = {true, false, 16, 16, 24, 24};
    Require(!MakeTextureScaleRegion(width, height, &invalid).Restricted(), "only edge-extension bounds qualify");

    QSurfaceFormat format;
    format.setVersion(4, 3); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format);
    Require(context.create(), "OpenGL context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Require(context.makeCurrent(&surface), "offscreen context");
    auto* gl = context.extraFunctions(); gl->initializeOpenGLFunctions();
    GLuint fb, vao;
    gl->glGenFramebuffers(1, &fb); gl->glGenVertexArrays(1, &vao); gl->glBindVertexArray(vao);
    auto compile = [&](QOpenGLShaderProgram& shader, const char* name, bool compute) {
        if (!compute)
            Require(shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
                "#version 140\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"), "vertex shader");
        QFile file(QString(SHADER_SOURCE_DIR) + '/' + name);
        Require(file.open(QIODevice::ReadOnly), "read shader");
        auto source = file.readAll();
        const int start = source.indexOf("#version"), end = source.indexOf('\n', start);
        if (start > 0) { auto directive = source.mid(start, end-start+1); source.remove(start, directive.size()); source.prepend(directive); }
        Require(shader.addShaderFromSourceCode(compute ? QOpenGLShader::Compute : QOpenGLShader::Fragment, source), "compile shader");
        Require(shader.link(), "link shader");
    };
    QOpenGLShaderProgram vertical, horizontal, spline;
    compile(vertical, "2DNNEDI3_VerticalCS.glsl", true);
    compile(horizontal, "2DNNEDI3_HorizontalCS.glsl", true);
    compile(spline, "2DSpline36FS.glsl", false);
    std::array<std::unique_ptr<QOpenGLShaderProgram>, 6> cunny;
    int index = 0;
    for (const char* stage : {"In", "Conv1", "Conv2", "Conv3", "Conv4", "OutShuffle"})
    {
        cunny[index] = std::make_unique<QOpenGLShaderProgram>();
        compile(*cunny[index++], (QByteArray("2DCuNNy4x32_") + stage + "CS.glsl").constData(), true);
    }
    auto texture = [&](int w, int h, const std::vector<float>* source = nullptr) {
        GLuint id; gl->glGenTextures(1, &id); gl->glBindTexture(GL_TEXTURE_2D, id);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Poison unused intermediates: passing must not depend on zeroed or
        // fortuitously matching contents left behind by the previous draw.
        std::vector<float> poison(source ? 0 : size_t(w)*h*4, 0.8125f);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT,
                        source ? source->data() : poison.data());
        return id;
    };
    std::vector<float> pixels(width*height*4);
    uint32_t rng = 0x6714f392;
    for (auto& v : pixels) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; v = (rng&255)/255.f; }
    GLuint input = texture(width, height, &pixels);
    auto dispatch = [&](QOpenGLShaderProgram& program, GLuint from, GLuint to,
                        int w, int h, const TextureScaleRegion& region) {
        auto d = region.Dispatch(w, h);
        gl->glUniform2i(program.uniformLocation("uWorkOrigin"), corruptOrigin && region.Restricted() ? 0 : d.X, d.Y);
        gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, from);
        gl->glBindImageTexture(0, to, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        gl->glDispatchCompute(d.GroupsX, d.GroupsY, 1);
        gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    };
    auto render = [&](bool useCuNNy, int scale, const TextureScaleRegion& region, int alphaMode = 0) {
        GLuint current = input;
        int w = width, h = height;
        if (useCuNNy)
        {
            for (int stage = 0; stage < 6; ++stage)
            {
                auto& program = *cunny[stage]; program.bind();
                for (const char* prefix : {"LUMA", "MAIN", "in", "conv1", "conv2", "conv3", "conv4"})
                {
                    QByteArray p(prefix); bool base = p=="LUMA" || p=="MAIN";
                    gl->glUniform1i(program.uniformLocation((p+"_raw").constData()), base ? 1 : 0);
                    gl->glUniform4f(program.uniformLocation((p+"_mul").constData()), 1,1,1,1);
                    gl->glUniform2f(program.uniformLocation((p+"_pt").constData()), 1.f/(base?width:w), 1.f/(base?height:h));
                    if (base) gl->glUniform2f(program.uniformLocation((p+"_size").constData()), width, height);
                }
                gl->glActiveTexture(GL_TEXTURE1); gl->glBindTexture(GL_TEXTURE_2D, input);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                w = width * (stage==5 ? 2 : 4); h = height*2;
                GLuint target = texture(w, h);
                // texture() changes the current binding; restore base input.
                gl->glBindTexture(GL_TEXTURE_2D, input);
                dispatch(program, current, target, width, height, region);
                if (current != input) gl->glDeleteTextures(1, &current);
                current = target;
            }
        }
        else
        {
            for (int stage = 0; stage < (scale>=4 ? 4 : 2); ++stage)
            {
                bool v = (stage%2)==0; auto& program = v ? vertical : horizontal; program.bind();
                gl->glUniform1i(program.uniformLocation("Source"), 0);
                gl->glUniform2i(program.uniformLocation("uSrcSize"), w, h);
                gl->glUniform1i(program.uniformLocation("uPredictAlpha"), alphaMode != 0);
                gl->glUniform1i(program.uniformLocation("uAlphaOnly"), alphaMode == 2);
                gl->glUniform1i(program.uniformLocation("uBoundedCoverageAlpha"), alphaMode != 0);
                gl->glUniform1i(program.uniformLocation("uPremultiplyInput"), alphaMode == 3 && stage == 0);
                GLuint target = texture(v?w:w*2, v?h*2:h);
                dispatch(program, current, target, w, h, region);
                if (current != input) gl->glDeleteTextures(1, &current);
                current = target; if (v) h*=2; else w*=2;
            }
        }
        GLuint output = texture(width*scale, height*scale);
        spline.bind();
        gl->glUniform1i(spline.uniformLocation("uBoundedCoverageAlpha"), alphaMode != 0);
        gl->glUniform1i(spline.uniformLocation("uResolvePremultipliedRGB"), alphaMode == 3);
        gl->glUniform1i(spline.uniformLocation("Source"), 0);
        gl->glUniform2f(spline.uniformLocation("uOutputSize"), width*scale, height*scale);
        float shift = useCuNNy ? 0.f : scale>=4 ? -1.5f : -.5f;
        gl->glUniform2f(spline.uniformLocation("uSourceShift"), shift, shift);
        gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, current);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
        Require(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, "framebuffer complete");
        gl->glViewport(0, 0, width*scale, height*scale); gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        std::vector<float> result(size_t(width)*height*scale*scale*4);
        gl->glReadPixels(0,0,width*scale,height*scale,GL_RGBA,GL_FLOAT,result.data());
        gl->glDeleteTextures(1,&current); gl->glDeleteTextures(1,&output);
        Require(gl->glGetError()==GL_NO_ERROR, "GL pipeline error");
        return result;
    };
    const std::array<TextureSamplingBounds, 6> bounds {{
        {true,true,117,45,142,61}, {true,true,0,0,24,16},
        {true,true,230,107,256,128}, {true,true,35,0,79,128},
        {true,true,0,47,256,71}, {true,true,1,1,255,127}
    }};
    size_t checked = 0;
    for (bool useCuNNy : {false,true})
    for (int scale : {2,4,8})
    {
        // Alternate restricted and full dispatches on the same programs.
        // Full work must reset the origin after the previous partial draw.
        for (const auto& b : bounds)
        {
            auto partial = render(useCuNNy, scale, MakeTextureScaleRegion(width,height,&b));
            auto reference = render(useCuNNy, scale, full);
            for (int y=std::max(0,int(b.Y0)-8)*scale; y<std::min(height,int(b.Y1)+8)*scale; ++y)
            for (int x=std::max(0,int(b.X0)-8)*scale; x<std::min(width,int(b.X1)+8)*scale; ++x)
            for (int c=0; c<4; ++c)
            {
                const size_t i=(size_t(y)*width*scale+x)*4+c;
                Require(partial[i]==reference[i], "restricted result differs inside guarded sampling region");
                ++checked;
            }
        }
    }
    // The combined texture path must preserve RGB and exactly match a
    // separate alpha-only chain, including phase correction and region halos.
    for (int scale : {2,3,4,8})
    {
        const auto rgb = render(false, scale, full, 0);
        const auto rgba = render(false, scale, full, 1);
        const auto alpha = render(false, scale, full, 2);
        for (size_t i = 0; i < rgba.size(); ++i)
            Require(rgba[i] == (i % 4 == 3 ? alpha[i] : rgb[i]),
                    "combined NNEDI3 differs from independent RGB/alpha");
        for (const auto& b : bounds)
        {
            const auto partial = render(false, scale, MakeTextureScaleRegion(width,height,&b), 1);
            for (int y=std::max(0,int(b.Y0)-8)*scale; y<std::min(height,int(b.Y1)+8)*scale; ++y)
            for (int x=std::max(0,int(b.X0)-8)*scale; x<std::min(width,int(b.X1)+8)*scale; ++x)
            for (int c=0; c<4; ++c)
            {
                const size_t i=(size_t(y)*width*scale+x)*4+c;
                Require(partial[i]==rgba[i], "combined NNEDI3 region differs from full result");
            }
        }
    }
    std::puts("PASS: combined NNEDI3 RGB/alpha equals separate chains at 2x/3x/4x/8x");

    // Affine reconstruction previously extracted a monochrome mask before
    // running an additional alpha-only chain. Its alpha must match the RGB
    // chain even when hidden RGB is padded or premultiplied before scaling.
    std::vector<float> maskPixels = pixels;
    for (size_t i = 0; i < maskPixels.size(); i += 4)
        for (int c = 0; c < 3; ++c) maskPixels[i + c] = maskPixels[i + 3];
    const GLuint rgbaInput = input;
    const GLuint maskInput = texture(width, height, &maskPixels);
    for (int scale : {2,4})
    {
        input = maskInput;
        const auto maskAlpha = render(false, scale, full, 2);
        input = rgbaInput;
        for (int mode : {1,3})
        {
            const auto combined = render(false, scale, full, mode);
            for (size_t i = 3; i < combined.size(); i += 4)
                Require(combined[i] == maskAlpha[i],
                        "combined affine alpha differs from reconstructed source mask");
        }
    }
    gl->glDeleteTextures(1, &maskInput);
    std::puts("PASS: affine NNEDI3 alpha reuse matches mask-only reconstruction, straight and premultiplied");
    gl->glDeleteTextures(1,&input); gl->glDeleteFramebuffers(1,&fb); gl->glDeleteVertexArrays(1,&vao);
    std::printf("PASS: NNEDI3/CuNNy 2x/4x/8x region pipelines, %zu exact component comparisons\n",checked);
    return 0;
}
