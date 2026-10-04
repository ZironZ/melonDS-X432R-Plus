// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QFile>
#include <QCoreApplication>
#include <array>
#include <cstdio>
#include <cstdlib>

static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Run the production merge against adjoining pieces and conflicting semantics.
// The caller owns a current OpenGL context.
void RunResolvedOBJMergeTests()
{
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    QOpenGLShaderProgram shader;
    Check(shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 140\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"), "merge vertex");
    QFile file(QString(SHADER_SOURCE_DIR) + "/2DResolvedOBJMergeFS.glsl");
    Check(file.open(QIODevice::ReadOnly), "merge source");
    QByteArray source = file.readAll();
    if (QCoreApplication::arguments().contains("--legacy-obj-boundary"))
        source.replace("priority != boundaryPriority",
                       "priority != boundaryPriority || spriteIndex != boundarySpriteIndex");
    if (QCoreApplication::arguments().contains("--opaque-only-obj-boundary"))
        source.replace("material > 1", "material != 0");
    Check(shader.addShaderFromSourceCode(QOpenGLShader::Fragment, source), "merge fragment");
    Check(shader.link() && shader.bind(), "merge link");
    shader.setUniformValue("SemanticOBJLayerTex", 0);
    shader.setUniformValue("OrdinaryPresentationTex", 1);
    shader.setUniformValue("NativeOrdinaryOBJLayerTex", 2);
    shader.setUniformValue("uScaleFactor", 4);
    auto texture = [&](int size, bool array, const unsigned char* data) {
        GLuint id; gl->glGenTextures(1, &id);
        GLenum target = array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
        gl->glBindTexture(target, id);
        gl->glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (array) gl->glTexImage3D(target, 0, GL_RGBA8, size, size, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        else gl->glTexImage2D(target, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        return id;
    };
    GLuint fb, vao; gl->glGenFramebuffers(1, &fb); gl->glGenVertexArrays(1, &vao);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fb); gl->glBindVertexArray(vao);
    std::array<GLuint, 3> outputs;
    const char* names[] = {"oColor", "oFlags", "oCoverage"};
    std::array<GLenum, 3> attachments;
    for (int i = 0; i < 3; i++)
    {
        outputs[i] = texture(8, false, nullptr);
        int location = gl->glGetFragDataLocation(shader.programId(), names[i]);
        Check(location >= 0 && location < 3, "merge output location");
        attachments[i] = GL_COLOR_ATTACHMENT0 + location;
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i], GL_TEXTURE_2D, outputs[i], 0);
    }
    GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    gl->glDrawBuffers(3, buffers);
    Check(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "merge framebuffer");
    gl->glViewport(0, 0, 8, 8);
    for (int scenario = 0; scenario < 21; scenario++)
    {
        std::array<unsigned char, 2*2*4*3> native {};
        std::array<unsigned char, 8*8*4*3> semantic {};
        std::array<unsigned char, 8*8*4> presentation {};
        for (int x = 0; x < 2; x++)
        {
            native[x*4+3] = 255;
            native[16+x*4+3] = 2;
            native[32+x*4] = 255;
            native[32+x*4+2] = x ? 9 : 5;
        }
        if (scenario == 1) { native[34] = 9; native[38] = 5; }
        if (scenario == 2) native[23] = 1; // different BG priority
        if (scenario == 3) native[20] = 1; // special alpha
        if (scenario == 4) native[21] = 255; // mosaic
        if (scenario == 5) native[22] = 255; // OBJ window
        if (scenario == 6) native[37] = 255; // affine contributor
        if (scenario == 7) native.fill(0); // no source support
        if (scenario == 8) { native[11] = 255; native[15] = 255; } // no exterior
        if (scenario >= 12)
        {
            native[16] = native[20] = 1; // mode-1 material
            native[34] = native[38] = 51; // one unambiguous sprite
            if (scenario == 14) native[20] = 0; // mixed material
            if (scenario == 15) native[38] = 52; // different special sprite
            if (scenario == 16) native[23] = 1; // different priority
            if (scenario == 17) native[16] = native[20] = 2; // bitmap alpha
            if (scenario == 18) native[21] = 255; // mosaic
            if (scenario == 19) native[22] = 255; // OBJ window
            if (scenario == 20) native[37] = 255; // affine
        }
        for (int i = 0; i < 64; i++)
        {
            presentation[i*4] = 80; presentation[i*4+3] = scenario == 9 || scenario == 13 ? 1 : 81;
            if (scenario == 10 || scenario == 11)
            {
                semantic[i*4] = 120; semantic[i*4+3] = 255;
                semantic[256+i*4] = 1; semantic[256+i*4+3] = 1;
                semantic[512+i*4] = 255;
                semantic[512+i*4+1] = scenario == 11 ? 255 : 0;
                semantic[512+i*4+2] = 3;
            }
        }
        GLuint inputs[] = {texture(8, true, semantic.data()), texture(8, false, presentation.data()), texture(2, true, native.data())};
        for (int i = 0; i < 3; i++)
        { gl->glActiveTexture(GL_TEXTURE0+i); gl->glBindTexture(i == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY, inputs[i]); }
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        std::array<std::array<unsigned char, 4>, 3> result;
        for (int i = 0; i < 3; i++)
        { gl->glReadBuffer(attachments[i]); gl->glReadPixels(3, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result[i].data()); }
        if (scenario == 0 || scenario == 1 || scenario == 9)
        {
            Check(result[0][0] == 80 && result[0][3] == 255, "continuous ordinary exterior across OAM seam");
            Check(result[1][3] == 2 && result[2][2] == 5, "same priority and frontmost contributing identity");
            Check(result[2][0] == (scenario == 9 ? 1 : 81), "preserve fractional and weak coverage");
        }
        else if (scenario < 9)
            Check(result[0][3] == 0 && result[2][0] == 0, "reject incompatible or unsupported exterior");
        else if (scenario == 10)
            Check(result[0][0] == 80 && result[1][0] == 1 && result[1][3] == 1 && result[2][2] == 3, "preserve existing ordinary winner semantics");
        else if (scenario == 11)
            Check(result[0][0] == 120 && result[2][0] == 255 && result[2][1] == 255 && result[2][2] == 3, "preserve existing affine winner");
        else if (scenario == 12 || scenario == 13)
        {
            Check(result[0][0] == 80 && result[0][3] == 255,
                  "semi-transparent sprite admits reconstructed exterior");
            Check(result[1][0] == 1 && result[1][3] == 2 && result[2][2] == 51,
                  "exterior retains DS blend material, priority and identity");
            Check(result[2][0] == (scenario == 13 ? 1 : 81) && result[2][3] == 255,
                  "semi-transparent coverage stays separate from DS blending");
        }
        else
            Check(result[0][3] == 0 && result[2][0] == 0,
                  "reject ambiguous or unsupported semi-transparent exterior");
        Check(gl->glGetError() == GL_NO_ERROR, "merge GPU errors");
        gl->glDeleteTextures(3, inputs);
    }
    gl->glDeleteTextures(3, outputs.data()); gl->glDeleteFramebuffers(1, &fb); gl->glDeleteVertexArrays(1, &vao);
    std::puts("21 resolved OBJ merge cases passed");
}
