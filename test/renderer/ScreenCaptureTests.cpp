// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenCapture.h"
#include "main_shaders.h"
#include "WideMelon.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    Check(context.create(), "create GL context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    Check(context.makeCurrent(&surface), "make context current");
    Check(gladLoadGLLoader([](const char* name) -> void* {
        return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
    }), "load GL functions");
    QOpenGLShaderProgram program;
    Check(program.addShaderFromSourceCode(QOpenGLShader::Vertex, kScreenVS), "presentation vertex shader");
    Check(program.addShaderFromSourceCode(QOpenGLShader::Fragment, kScreenFS), "presentation fragment shader");
    program.bindAttributeLocation("vPosition", 0);
    program.bindAttributeLocation("vTexcoord", 1);
    Check(program.link(), "link production presentation shader");
    program.bind();
    program.setUniformValue("ScreenTex", 0);
    program.setUniformValue("LCDGhostingHistoryTex", 1);
    program.setUniformValue("uLCDGhostingMode", 0);
    program.setUniformValue("uSharpenAmount", 0.f);

    const float vertices[] = {
        0,0,0,0,0, 0,192,0,1,0, 256,192,1,1,0,
        0,0,0,0,0, 256,192,1,1,0, 256,0,1,0,0,
        0,0,0,0,1, 0,192,0,1,1, 256,192,1,1,1,
        0,0,0,0,1, 256,192,1,1,1, 256,0,1,0,1};
    GLuint vao, vbo, source, history;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(2*sizeof(float)));
    glGenTextures(1, &source);
    glGenTextures(1, &history);
    auto configure = [](GLuint texture, int unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    configure(history, 1);
    configure(source, 0);
    QString error;
    std::array<QImage, 2> images;
    for (int width : {256, 342, 682}) for (int scale : {1, 2, 4}) for (int target : {0, 1, 2})
    {
        WideMelon::ViewWidth = width;
        WideMelon::Target = target;
        const int w = width * scale, h = 192 * scale;
        std::vector<unsigned char> pixels(w*h*2*4);
        for (int screen = 0; screen < 2; screen++) for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
        {
            const int pos = ((screen*h+y)*w+x)*4;
            pixels[pos] = x % 251;
            pixels[pos+1] = y % 253;
            pixels[pos+2] = screen ? 211 : 31;
            pixels[pos+3] = 255;
        }
        configure(source, 0);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // Capture must ignore, then restore, unrelated caller state.
        glViewport(7, 9, 101, 103);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 1, 1);
        glEnablei(GL_BLEND, 0);
        glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        glPixelStorei(GL_PACK_ROW_LENGTH, w+10);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
        glPixelStorei(GL_PACK_SKIP_ROWS, 3);
        Check(ReadScreenPresentation(program.programId(), vao, source, images, &error), qPrintable(error));
        GLint viewport[4], pack;
        glGetIntegerv(GL_VIEWPORT, viewport);
        Check(viewport[0]==7 && viewport[1]==9 && viewport[2]==101 && viewport[3]==103, "viewport restored");
        glGetIntegerv(GL_PACK_ROW_LENGTH, &pack);
        Check(pack==w+10 && glIsEnabled(GL_SCISSOR_TEST) && glIsEnabledi(GL_BLEND, 0), "caller state restored");
        for (int screen = 0; screen < 2; screen++)
        {
            const int expectedWidth=WideMelon::DisplayWidth(screen)*scale, left=(w-expectedWidth)/2;
            Check(images[screen].width()==expectedWidth && images[screen].height()==h, "physical display dimensions");
            for (int y : {0, 1, h/2, h-1}) for (int x : {0, 1, expectedWidth/2, expectedWidth-1})
                Check(images[screen].pixelColor(x,y)==QColor((x+left)%251,y%253,screen?211:31), "correct layer, center crop, orientation and wings");
        }
    }
    // A moving black/white alternation should retain the live LCD shader's
    // temporal blend. Re-reading the capture must not advance that history.
    WideMelon::ViewWidth=256;WideMelon::Target=WideMelon::Both;
    const int w=256,h=192;
    std::vector<unsigned char> current(w*h*2*4,255), previous(w*h*8*4,0);
    for(int layer=0;layer<8;layer++) for(int i=0;i<w*h;i++) {
        const int pos=(layer*w*h+i)*4;
        previous[pos]=previous[pos+1]=previous[pos+2]=(layer/2)%2 ? 255 : 0;
        previous[pos+3]=255;
    }
    configure(history,1);glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,w,h,8,0,GL_RGBA,GL_UNSIGNED_BYTE,previous.data());
    configure(source,0);glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,w,h,2,0,GL_RGBA,GL_UNSIGNED_BYTE,current.data());
    program.setUniformValue("uLCDGhostingMode",1);
    glUniform4i(program.uniformLocation("uLCDGhostingHistorySlots"),0,1,2,3);
    Check(ReadScreenPresentation(program.programId(),vao,source,images,&error),qPrintable(error));
    Check(std::abs(images[0].pixelColor(5,5).red()-128)<=1,"LCD ghosting included");
    const auto first=images;
    Check(ReadScreenPresentation(program.programId(),vao,source,images,&error),qPrintable(error));
    Check(images==first,"repeated capture preserves temporal result");
    program.setUniformValue("uLCDGhostingMode",0);
    for(int i=0;i<w*h*2;i++)for(int c=0;c<3;c++)current[i*4+c]=(i%w==w/2)?160:128;
    configure(source,0);glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,w,h,2,0,GL_RGBA,GL_UNSIGNED_BYTE,current.data());
    program.setUniformValue("uSharpenAmount",0.5f);
    Check(ReadScreenPresentation(program.programId(),vao,source,images,&error),qPrintable(error));
    Check(std::abs(images[0].pixelColor(w/2,h/2).red()-192)<=1,"screen sharpening included");
    Check(glGetError()==GL_NO_ERROR,"no GL errors");
    std::puts("Screen capture: 27 width/scale/target cases, GL state, orientation, sharpening, and temporal effects passed.");
    glDeleteTextures(1,&source);glDeleteTextures(1,&history);
    glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);
    return 0;
}
