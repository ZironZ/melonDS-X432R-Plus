// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QFile>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <QElapsedTimer>

namespace {
void Require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int RunScalerAlphaFusionTests(bool benchmark)
{
    QSurfaceFormat format;
    format.setVersion(4, 3); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format);
    Require(context.create(), "OpenGL context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Require(context.makeCurrent(&surface), "offscreen context");
    auto* gl = context.extraFunctions(); gl->initializeOpenGLFunctions();
    GLuint fb, vao;
    gl->glGenFramebuffers(1, &fb); gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao); gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
    gl->glDisable(GL_DITHER);
    auto compile = [&](QOpenGLShaderProgram& program, const char* name) {
        Require(program.addShaderFromSourceCode(QOpenGLShader::Vertex,
            "#version 140\nsmooth out vec2 fTexcoord; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);fTexcoord=p;gl_Position=vec4(p*2.-1.,0,1);}"), "vertex shader");
        QFile file(QString(SHADER_SOURCE_DIR) + '/' + name);
        Require(file.open(QIODevice::ReadOnly), "read production shader");
        auto source = file.readAll();
        int start = source.indexOf("#version"), end = source.indexOf('\n', start);
        if (start > 0) { auto directive = source.mid(start, end-start+1); source.remove(start, directive.size()); source.prepend(directive); }
        Require(program.addShaderFromSourceCode(QOpenGLShader::Fragment, source), "fragment shader");
        Require(program.link(), "shader link");
    };
    QOpenGLShaderProgram spline, preprocess, xbrz, midpoint, mask;
    compile(spline, "2DSpline36FS.glsl");
    compile(preprocess, "2DXBRZ_PreprocessFS.glsl");
    compile(xbrz, "2DXBRZ_FreescaleFS.glsl");
    compile(midpoint, "2DMidpointAlphaFS.glsl");
    compile(mask, "2DReconstructionAlphaFS.glsl");
    auto texture = [&](int w, int h, GLint internal, const void* pixels = nullptr) {
        GLuint id; gl->glGenTextures(1, &id); gl->glBindTexture(GL_TEXTURE_2D, id);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        return id;
    };
    auto uniform = [&](QOpenGLShaderProgram& p, const char* name, bool value) {
        p.bind(); gl->glUniform1i(p.uniformLocation(name), value);
    };
    auto draw = [&](QOpenGLShaderProgram& p, GLuint target, int w, int h, GLuint src, GLuint aux = 0) {
        p.bind(); gl->glUniform2f(p.uniformLocation("uOutputSize"), w, h);
        gl->glUniform1i(p.uniformLocation("Source"), 0);
        gl->glUniform1i(p.uniformLocation("AlphaSource"), 1);
        gl->glUniform1i(p.uniformLocation("InfoTex"), 1);
        gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, src);
        gl->glActiveTexture(GL_TEXTURE1); gl->glBindTexture(GL_TEXTURE_2D, aux);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        Require(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer");
        gl->glViewport(0, 0, w, h); gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    auto read = [&](GLuint target, int w, int h) {
        std::vector<float> result(size_t(w)*h*4);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, result.data());
        return result;
    };
    unsigned comparisons = 0;
    for (bool fractional : {false, true})
    for (int width : {17, 32})
    for (int scale : {2, 3, 4, 8})
    {
        const int height = 13, w = width*scale, h = height*scale;
        std::vector<uint8_t> pixels(size_t(width)*height*4);
        uint32_t rng = 0x427a571d;
        for (auto& v : pixels) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; v = rng&255; }
        if (!fractional)
            for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = pixels[i] < 128 ? 0 : 255;
        GLuint source = texture(width, height, GL_RGBA8, pixels.data());
        GLuint info = texture(width, height, GL_RGBA8);
        GLuint color = texture(w, h, GL_RGBA8), expected = texture(w, h, GL_RGBA8), actual = texture(w, h, GL_RGBA8);
        draw(preprocess, info, width, height, source);
        for (auto* shader : {&spline, &xbrz})
        {
            uniform(*shader, "uBilinearAlpha", false);
            uniform(spline, "uBoundedCoverageAlpha", false);
            draw(*shader, color, w, h, source, info);
            draw(midpoint, expected, w, h, color, source);
            uniform(*shader, "uBilinearAlpha", true);
            draw(*shader, actual, w, h, source, info);
            auto a = read(actual, w, h), e = read(expected, w, h);
            if (a != e) std::fprintf(stderr, "texture %s width=%d scale=%d fractional=%d\n", shader==&spline?"spline":"xbrz", width, scale, fractional);
            if (a != e)
            {
                unsigned count = 0;
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (a[i] == e[i]) continue;
                    if (count++ < 12)
                        std::fprintf(stderr, "  pixel=%zu channel=%zu actual=%.9f expected=%.9f\n",
                            i/4, i%4, a[i], e[i]);
                }
                std::fprintf(stderr, "  differences=%u\n", count);
            }
            Require(a == e, "fused Bilinear texture alpha equals separate output pass");
            uniform(*shader, "uBilinearAlpha", false);
            draw(*shader, actual, w, h, source, info);
            Require(read(actual,w,h) == read(color,w,h), "reset alpha mode retains original RGBA output");
            comparisons += 2;
        }
        // The old sprite path quantizes its mask to half float before scaling.
        // Fractional input detects accidentally bypassing that preparation.
        GLuint nativeMask = texture(width, height, GL_RGBA16F);
        GLuint rgb = texture(w,h,GL_RGBA16F), alpha = texture(w,h,GL_RGBA16F);
        GLuint spriteExpected = texture(w,h,GL_RGBA16F), spriteActual = texture(w,h,GL_RGBA16F);
        draw(mask,nativeMask,width,height,source);
        uniform(spline,"uBoundedCoverageAlpha",false);
        draw(spline,rgb,w,h,source);
        uniform(spline,"uBoundedCoverageAlpha",true);
        draw(spline,alpha,w,h,nativeMask);
        draw(midpoint,spriteExpected,w,h,rgb,alpha);
        uniform(spline,"uUseCoverageSource",true);
        draw(spline,rgb,w,h,source,nativeMask);
        uniform(spline,"uUseCoverageSource",false);
        draw(midpoint,spriteActual,w,h,rgb,rgb);
        auto a = read(spriteActual,w,h), e = read(spriteExpected,w,h);
        if (a != e) std::fprintf(stderr,"sprite width=%d scale=%d fractional=%d\n",width,scale,fractional);
        Require(a == e,"shared Spline36 sprite alpha equals separate mask reconstruction");
        ++comparisons;
        GLuint textures[] = {source,info,color,expected,actual,nativeMask,rgb,alpha,spriteExpected,spriteActual};
        gl->glDeleteTextures(10,textures);
    }
    if (benchmark)
    {
        constexpr int width = 256, height = 128, w = width*4, h = height*4;
        std::vector<uint8_t> pixels(width*height*4);
        uint32_t rng = 0x427a571d;
        for (auto& v : pixels) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; v = rng&255; }
        GLuint source = texture(width,height,GL_RGBA8,pixels.data());
        GLuint info = texture(width,height,GL_RGBA8), color = texture(w,h,GL_RGBA8), output = texture(w,h,GL_RGBA8);
        GLuint nativeMask = texture(width,height,GL_RGBA16F), rgb = texture(w,h,GL_RGBA16F);
        GLuint alpha = texture(w,h,GL_RGBA16F), sprite = texture(w,h,GL_RGBA16F);
        auto measure = [&](auto render) {
            for (int i=0; i<10; ++i) render();
            gl->glFinish();
            QElapsedTimer timer; timer.start();
            for (int i=0; i<100; ++i) render();
            gl->glFinish();
            return timer.nsecsElapsed()/100000.0;
        };
        for (int round=0; round<3; ++round)
        for (int method=0; method<3; ++method)
        {
            double times[2];
            for (int order=0; order<2; ++order)
            {
                bool fused = (order + round)%2;
                times[fused] = measure([&]() {
                    uniform(spline,"uUseCoverageSource",false);
                    uniform(spline,"uBoundedCoverageAlpha",false);
                    if (method < 2)
                    {
                        auto& shader = method==0 ? spline : xbrz;
                        if (method==1) draw(preprocess,info,width,height,source);
                        uniform(shader,"uBilinearAlpha",fused);
                        draw(shader,fused ? output : color,w,h,source,info);
                        if (!fused) draw(midpoint,output,w,h,color,source);
                    }
                    else
                    {
                        uniform(spline,"uBilinearAlpha",false);
                        draw(mask,nativeMask,width,height,source);
                        uniform(spline,"uBoundedCoverageAlpha",fused);
                        uniform(spline,"uUseCoverageSource",fused);
                        draw(spline,rgb,w,h,source,nativeMask);
                        if (!fused)
                        {
                            uniform(spline,"uBoundedCoverageAlpha",true);
                            draw(spline,alpha,w,h,nativeMask);
                        }
                        draw(midpoint,sprite,w,h,rgb,fused ? rgb : alpha);
                    }
                });
            }
            std::printf("BENCH %s separate=%.1f us shared=%.1f us\n",
                method==0 ? "Spline36 texture" : method==1 ? "xBRZ texture" : "Spline36 sprite",
                times[0],times[1]);
        }
        GLuint textures[] = {source,info,color,output,nativeMask,rgb,alpha,sprite};
        gl->glDeleteTextures(8,textures);
    }
    Require(gl->glGetError() == GL_NO_ERROR,"OpenGL errors");
    gl->glDeleteFramebuffers(1,&fb); gl->glDeleteVertexArrays(1,&vao);
    std::printf("PASS: %u exact scaler alpha comparisons\n", comparisons);
    return 0;
}
