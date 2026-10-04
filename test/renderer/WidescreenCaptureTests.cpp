// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "glad/glad.h"
#include "OpenGL_shaders/WidePresentation.h"
#include "WideCaptureLayout.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char** argv)
{
    melonDS::WideCaptureLayout layout;
    for (int y : {0, 64, -128}) for (int x = 0; x < 256; x += 64)
        Check(layout.Add(x, y & 255, 64, 64, x, y & 255), "identity sprite assembly including wrapped Y");
    Check(layout.Complete(), "full screen assembly");
    Check(!layout.Add(0, 0, 64, 64, 0, 0), "reject competing overlap");
    melonDS::WideCaptureLayout partial;
    Check(partial.Add(0, 0, 256, 128, 0, 0) && !partial.Complete(), "reject partial screen copy");
    Check(!partial.Add(0, 128, 256, 64, 0, 0), "reject displaced source");
    Check(!partial.Add(0, 128, 256, 65, 0, 128), "reject out-of-display copy");
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    Check(context.create(), "create context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    Check(context.makeCurrent(&surface), "make current");
    Check(gladLoadGLLoader([](const char* name) -> void* {
        return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
    }), "load GL");
    QOpenGLShaderProgram program;
    Check(program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(#version 140
        smooth out vec3 fTexcoord;
        void main() {
            vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
            fTexcoord = vec3(p, 0);
            gl_Position = vec4(p * 2 - 1, 0, 1);
        })"), "vertex shader");
    Check(program.addShaderFromSourceCode(QOpenGLShader::Fragment, kWidePresentationFS), "production fragment shader");
    glBindFragDataLocation(program.programId(), 0, "oTopColor");
    glBindFragDataLocation(program.programId(), 1, "oBottomColor");
    Check(program.link(), "link shader");
    program.bind();
    program.setUniformValue("WideInputA", 0);
    program.setUniformValue("WideInputB", 1);
    GLuint vao, ubo, windowUbo, fbo, textures[4];
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    const GLuint block = glGetUniformBlockIndex(program.programId(), "ubFinalPassConfig");
    Check(block != GL_INVALID_INDEX, "final pass config block");
    glUniformBlockBinding(program.programId(), block, 0);
    GLint blockSize;
    glGetActiveUniformBlockiv(program.programId(), block, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
    std::vector<unsigned> config((blockSize + 3) / 4);
    glGenBuffers(1, &windowUbo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, windowUbo);
    const GLuint windowBlock = glGetUniformBlockIndex(program.programId(), "ubWideWindows");
    Check(windowBlock != GL_INVALID_INDEX, "wide window policy block");
    glUniformBlockBinding(program.programId(), windowBlock, 1);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(4, textures);
    const GLenum outputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, outputs);
    auto uniform = [&](const char* name) {
        const GLint location = glGetUniformLocation(program.programId(), name);
        Check(location >= 0, name);
        return location;
    };

    for (int width : {258, 342, 682}) for (int scale : {1, 4}) for (bool fullWidth : {false, true})
    {
        const int w = width * scale, h = 192 * scale, side = (width - 256) * scale / 2;
        for (int t = 0; t < 4; ++t)
        {
            glActiveTexture(GL_TEXTURE0 + (t & 1));
            glBindTexture(GL_TEXTURE_2D, textures[t]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const int tw = t == 1 && !fullWidth ? side * 2 : w;
            std::vector<unsigned char> pixels(tw * h * 4);
            for (int y = 0; y < h; ++y) for (int x = 0; x < tw; ++x)
            {
                const int p = (y * tw + x) * 4;
                pixels[p] = t == 1 ? 80 : (x % 64) * 4;
                pixels[p+1] = t == 1 ? (x % 64) * 4 : (y % 64) * 4;
                pixels[p+2] = t == 1 ? (y % 64) * 4 : 40;
                pixels[p+3] = 255;
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (t >= 2) glFramebufferTexture2D(GL_FRAMEBUFFER, outputs[t-2], GL_TEXTURE_2D, textures[t], 0);
        }
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer");
        for (int t = 0; t < 2; ++t) { glActiveTexture(GL_TEXTURE0+t); glBindTexture(GL_TEXTURE_2D, textures[t]); }
        glViewport(0, 0, w, h);
        glUniform1i(uniform("uWideWidth"), width);
        for (int target = 0; target < 3; ++target) for (int swap = 0; swap < 2; ++swap)
        for (int fade = 0; fade < 4; ++fade) for (int valid = 0; valid < 2; ++valid)
        {
            for (int y = 0; y < 192; ++y) config[y] = (y & 1) ^ swap;
            glBindBuffer(GL_UNIFORM_BUFFER, ubo);
            glBufferData(GL_UNIFORM_BUFFER, blockSize, config.data(), GL_DYNAMIC_DRAW);
            glUniform2i(uniform("uWideModes"), 1, valid ? (fullWidth ? 3 : 2) : 0);
            glUniform2i(uniform("uWideDisplays"), target != 1, target != 0);
            // Each std140 row has current/captured uvec4 policies, with two
            // sides per engine. Enable both sides; engine B first receives
            // captured brightening, then current darkening.
            std::vector<unsigned> windows(192 * 8, 3);
            for (int row = 0; row < 192; ++row)
            {
                windows[row*8+2] = windows[row*8+3] = 3 | (fade ? (3 << 2) | (7 << 4) : 0);
                windows[row*8+6] = windows[row*8+7] = 3 | (fade ? (2 << 2) | (5 << 4) : 0);
            }
            glBindBuffer(GL_UNIFORM_BUFFER, windowUbo);
            glBufferData(GL_UNIFORM_BUFFER, windows.size() * sizeof(unsigned), windows.data(), GL_DYNAMIC_DRAW);
            const int presentation[] = {-1, 2, fade == 2 ? 16 : 0,
                                        -1, fade == 3 ? 1 : 2, fade >= 2 ? 16 : 0};
            glUniform3iv(uniform("uPresentation"), 2, presentation);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            for (int screen = 0; screen < 2; ++screen) for (int y : {0, scale, h-1})
            for (int x : {0, side-1, w-side, w-1})
            {
                const bool main = screen == 0 ? config[y/scale] : !config[y/scale];
                const int cx = fullWidth || x < side ? x : x - 256 * scale;
                std::array<int, 3> expected = main ? std::array<int,3>{x%64, y%64, 10} : std::array<int,3>{20, cx%64, y%64};
                // Missing capture content is black before master brightness,
                // so it still reaches white during a full brighten.
                if (!main && !valid) expected = {0, 0, 0};
                for (int& c : expected)
                {
                    if (!main && valid && fade) { c += ((63-c)*5+8)>>4; c -= (c*7+7)>>4; }
                    if (fade == 2) c = 0;
                    if (!main && fade == 3) c = 63;
                    c *= 4;
                    if (target != 2 && target != screen) c = 0;
                }
                unsigned char pixel[4];
                glReadBuffer(outputs[screen]);
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                Check(pixel[0] == expected[0] && pixel[1] == expected[1] && pixel[2] == expected[2] && pixel[3] == 255,
                      "strip coordinates, per-line routing, target selection, invalidation or fade ordering");
            }
        }
    }
    Check(glGetError() == GL_NO_ERROR, "GL error");
    glDeleteTextures(4, textures);
    glDeleteFramebuffers(1, &fbo);
    glDeleteBuffers(1, &ubo);
    glDeleteBuffers(1, &windowUbo);
    glDeleteVertexArrays(1, &vao);
    std::puts("Widescreen capture presentation passed.");
}
