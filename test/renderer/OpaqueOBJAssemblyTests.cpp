// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QFile>
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>

void RunResolvedOBJMergeTests();
void RunEnhancedBGSamplingTests();

static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format; format.setVersion(4, 3); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format); Check(context.create(), "context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Check(context.makeCurrent(&surface), "make current");
    auto* gl = context.extraFunctions(); gl->initializeOpenGLFunctions();
    RunResolvedOBJMergeTests();
    RunEnhancedBGSamplingTests();
    auto compile = [&](QOpenGLShaderProgram& p, const char* name) {
        Check(p.addShaderFromSourceCode(QOpenGLShader::Vertex,
            "#version 140\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"), "vertex");
        QFile f(QString(SHADER_SOURCE_DIR) + '/' + name); Check(f.open(QIODevice::ReadOnly), "shader file");
        bool compiled=p.addShaderFromSourceCode(QOpenGLShader::Fragment, f.readAll());
        if (!compiled) std::fprintf(stderr,"%s: %s\n",name,p.log().toUtf8().constData());
        Check(compiled, "production fragment");
        Check(p.link(), "shader link");
    };
    GLuint vao, fb; gl->glGenVertexArrays(1, &vao); gl->glBindVertexArray(vao); gl->glGenFramebuffers(1, &fb);
    auto tex = [&](int size, bool array, const void* data) {
        GLuint id; gl->glGenTextures(1, &id);
        GLenum target = array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
        gl->glBindTexture(target, id);
        gl->glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (array) gl->glTexImage3D(target, 0, GL_RGBA8, size, size, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        else gl->glTexImage2D(target, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        return id;
    };
    auto bind = [&](int unit, GLenum target, GLuint id) {
        gl->glActiveTexture(GL_TEXTURE0 + unit); gl->glBindTexture(target, id);
    };
    auto draw = [&](QOpenGLShaderProgram& p, GLuint output, int size) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
        Check(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "output framebuffer");
        gl->glViewport(0, 0, size, size); gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        Check(gl->glGetError() == GL_NO_ERROR, "GPU draw");
    };
    QOpenGLShaderProgram assembly;
    compile(assembly,"2DOpaqueOBJAssemblyFS.glsl");
    std::array<unsigned char,8*8*4> atlas {};
    for (int y=0;y<4;y++) for (int x=4;x<8;x++)
    { atlas[(y*8+x)*4+2]=255; atlas[(y*8+x)*4+3]=255; }
    for (int y=0;y<2;y++) for (int x=0;x<2;x++)
    { atlas[(y*8+x)*4]=255; atlas[(y*8+x)*4+3]=255; }
    atlas[(1*8+1)*4]=0; atlas[(1*8+1)*4+1]=255; atlas[(1*8+1)*4+3]=0;
    GLuint source=tex(8,false,atlas.data()), assembled=tex(8,false,nullptr);
    for (int reverse=0;reverse<2;reverse++)
    {
        assembly.bind(); assembly.setUniformValue("Source",0);
        assembly.setUniformValue("uMemberCount",2);
        gl->glUniform2i(assembly.uniformLocation("uRootSize"),4,4);
        GLint members[8]={0,0,2,2,4,0,4,4};
        if (reverse) for (int c=0;c<4;c++) std::swap(members[c],members[c+4]);
        gl->glUniform4iv(assembly.uniformLocation("uMembers"),2,members);
        bind(0,GL_TEXTURE_2D,source); draw(assembly,assembled,8);
        std::array<unsigned char,8*8*4> pixels {};
        gl->glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for (int y=0;y<8;y++) for (int x=0;x<8;x++)
        {
            bool inside=x<4 && y<4;
            bool front=!reverse && x>=1 && x<3 && y>=1 && y<3 && !(x==2 && y==2);
            auto* p=&pixels[(y*8+x)*4];
            Check(p[0]==(front?255:0) && p[1]==0 && p[2]==(inside&&!front?255:0) &&
                p[3]==(inside?255:0),"assembled source order, transparent hole and extent");
        }
    }
    gl->glDeleteTextures(1,&source); gl->glDeleteTextures(1,&assembled);
    // A uniform-colored shape must keep that color everywhere independent
    // reconstruction supplies coverage, including outside native presence.
    // Arbitrary transparent palette RGB must not turn its fringe green/black.
    QOpenGLShaderProgram pad, spline;
    compile(pad, "2DTransparentRGBPadFS.glsl");
    compile(spline, "2DSpline36FS.glsl");
    const bool clippedRGB = app.arguments().contains("--clipped-spline-rgb");
    int supportCases = 0;
    for (int scale : {2, 4})
    for (int pattern = 0; pattern < 3; pattern++)
    {
        std::array<unsigned char, 16*16*4> input {};
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
        {
            bool inside = pattern == 0 ? x >= 8 : pattern == 1 ? x >= y :
                (x >= 6 && x < 10 && y >= 6 && y < 10);
            auto* p = &input[(y*16+x)*4];
            p[0] = inside ? 200 : 0; p[1] = inside ? 100 : 255;
            p[2] = inside ? 50 : 0; p[3] = inside ? 255 : 0;
        }
        GLuint raw = tex(16, false, input.data());
        GLuint prepared = tex(16, false, nullptr);
        pad.bind(); pad.setUniformValue("Source", 0);
        pad.setUniformValue("uPadRadius", 3);
        pad.setUniformValue("uPrecomposeBackdrop", false);
        gl->glUniform2i(pad.uniformLocation("uSourceSize"), 16, 16);
        bind(0, GL_TEXTURE_2D, raw); draw(pad, prepared, 16);
        std::array<unsigned char, 16*16*4> padded {};
        gl->glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, padded.data());
        for (int i = 0; i < 16*16; i++)
            Check(padded[i*4+3] == input[i*4+3], "RGB padding preserves native alpha");
        int size = 16 * scale;
        GLuint output = tex(size, false, nullptr);
        std::vector<unsigned char> alpha(size*size*4), rgb(size*size*4);
        spline.bind(); spline.setUniformValue("Source", 0);
        gl->glUniform2f(spline.uniformLocation("uOutputSize"), size, size);
        gl->glUniform2f(spline.uniformLocation("uSourceShift"), 0, 0);
        // Alpha uses the unchanged source mask, independently of RGB clipping.
        spline.setUniformValue("uTransparentSourceAware", false);
        spline.setUniformValue("uBoundedCoverageAlpha", true);
        bind(0, GL_TEXTURE_2D, prepared); draw(spline, output, size);
        gl->glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, alpha.data());
        spline.setUniformValue("uTransparentSourceAware", clippedRGB);
        spline.setUniformValue("uBoundedCoverageAlpha", false);
        draw(spline, output, size);
        gl->glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, rgb.data());
        int exteriorCovered = 0;
        for (int y = 0; y < size; y++) for (int x = 0; x < size; x++)
        {
            int i = (y*size+x)*4;
            if (!alpha[i+3]) continue;
            Check(std::abs(int(rgb[i])-200) <= 1 &&
                  std::abs(int(rgb[i+1])-100) <= 1 &&
                  std::abs(int(rgb[i+2])-50) <= 1,
                  "Spline36 RGB covers the independently reconstructed alpha fringe");
            exteriorCovered += input[((y/scale)*16+x/scale)*4+3] == 0;
        }
        Check(exteriorCovered > 0, "fixture exercises coverage beyond native presence");
        gl->glDeleteTextures(1, &raw); gl->glDeleteTextures(1, &prepared);
        gl->glDeleteTextures(1, &output);
        supportCases++;
    }
    std::puts("2 production-shader opaque assembly cases passed.");
    std::printf("%d Spline36 RGB-support cases passed.\n", supportCases);
    return 0;
}
