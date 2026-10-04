// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QCoreApplication>
#include <QFile>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void Require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Exercise the actual compositor's sampling function with a colored finite
// map, including fractional boundary positions and several full map periods.
// No ROM or game-specific geometry is needed. Caller owns the GL context.
void RunEnhancedBGSamplingTests()
{
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    QFile file(QString(SHADER_SOURCE_DIR) + "/2DCompositorFS.glsl");
    Require(file.open(QIODevice::ReadOnly), "compositor source");
    QByteArray source = file.readAll();
    source.replace("\r\n", "\n");
    source.replace("void main()", "void UnusedCompositorMain()");
    if (QCoreApplication::arguments().contains("--broken-bg-domain"))
    {
        // Reintroduce the old infinite edge coverage, keeping RGB unchanged.
        source.replace("if (clampLayer)\n    {\n        c00.a *=", "if (false)\n    {\n        c00.a *=");
        source.replace("if (clampLayer)\n            pointColor.a *=", "if (false)\n            pointColor.a *=");
    }
    if (QCoreApplication::arguments().contains("--broken-bg-wrap"))
        source.replace("normalizedCoord = fract(normalizedCoord);", "normalizedCoord = normalizedCoord;");
    source += R"(
uniform sampler2D TestSource;
uniform vec2 TestPosition;
uniform bool TestClamp;
void main() {
    oColor = FetchLayerEnhanced(TestSource, TestSource, TestPosition,
        ivec2(4), TestClamp, true, true, false, false, vec3(0));
}
)";
    QOpenGLShaderProgram shader;
    Require(shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 140\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}"), "BG sampling vertex");
    bool compiled = shader.addShaderFromSourceCode(QOpenGLShader::Fragment, source);
    if (!compiled) std::fprintf(stderr, "%s\n", shader.log().toUtf8().constData());
    Require(compiled && shader.link() && shader.bind(), "BG sampling shader");
    std::array<float, 4*4*4> pixels {};
    for (int y=0; y<4; ++y) for (int x=0; x<4; ++x)
    {
        auto* p = &pixels[(y*4+x)*4];
        p[0]=(x+1)/5.f; p[1]=(y+1)/5.f; p[2]=0.25f;
        p[3]=(x+y+1)/8.f; // Nonuniform alpha tests real interpolation, not just clipping.
    }
    GLuint textures[2], fb, vao;
    gl->glGenTextures(2, textures); gl->glGenFramebuffers(1, &fb); gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao); gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
    gl->glActiveTexture(GL_TEXTURE0);
    for (int i=0; i<2; ++i)
    {
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, i==0?4:1, i==0?4:1, 0, GL_RGBA, GL_FLOAT, i==0?pixels.data():nullptr);
    }
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[1], 0);
    Require(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, "BG sampling framebuffer");
    GLenum output=GL_COLOR_ATTACHMENT0; gl->glDrawBuffers(1, &output); gl->glReadBuffer(output);
    gl->glViewport(0,0,1,1); gl->glDisable(GL_BLEND); gl->glDisable(GL_SCISSOR_TEST);
    gl->glBindTexture(GL_TEXTURE_2D, textures[0]);
    shader.setUniformValue("TestSource",0);
    int cases=0;
    const std::array<std::array<float,2>,9> positions {{{1.25f,2.25f},{1.25f,-1.75f},
        {5.25f,6.25f},{-6.75f,-5.75f},{-0.25f,1.25f},{3.9f,2.25f},
        {4.25f,4.25f},{0.f,0.f},{4.f,2.25f}}};
    for (int alpha=-1; alpha<=4; ++alpha) for (bool linear : {false,true})
    for (bool noWrap : {false,true}) for (bool finite : {false,true})
    for (auto pos : positions)
    {
        shader.setUniformValue("uExplicitAlphaReconstruction",alpha);
        shader.setUniformValue("uExplicitLinearRGB",linear);
        shader.setUniformValue("uLayerFilterNoWrap",noWrap);
        shader.setUniformValue("TestClamp",finite);
        shader.setUniformValue("TestPosition",pos[0],pos[1]);
        for (GLenum axis : {GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T})
            gl->glTexParameteri(GL_TEXTURE_2D,axis,finite?GL_CLAMP_TO_BORDER:GL_REPEAT);
        gl->glDrawArrays(GL_TRIANGLES,0,3);
        std::array<float,4> actual {};
        gl->glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,actual.data());
        Require(gl->glGetError()==GL_NO_ERROR,"BG sampling GL error");
        auto fetch = [&](int x,int y,bool legacy) {
            bool outside=x<0||y<0||x>=4||y>=4;
            if (finite && outside && legacy) return std::array<float,4>{};
            if (finite || (!legacy && noWrap)) {x=std::clamp(x,0,3);y=std::clamp(y,0,3);}
            else {x=(x%4+4)%4;y=(y%4+4)%4;}
            std::array<float,4> p; std::copy_n(&pixels[(y*4+x)*4],4,p.begin());
            if (finite && outside) p[3]=0;
            return p;
        };
        auto center=pos;
        if (!finite) for (auto& v:center) v-=std::floor(v/4.f)*4.f;
        auto point=fetch(int(std::floor(center[0])),int(std::floor(center[1])),alpha<0);
        int bx=int(std::floor(center[0]-0.5f)),by=int(std::floor(center[1]-0.5f));
        float fx=center[0]-0.5f-bx,fy=center[1]-0.5f-by;
        auto a=fetch(bx,by,false),b=fetch(bx+1,by,false),c=fetch(bx,by+1,false),d=fetch(bx+1,by+1,false);
        auto expected=point;
        if (alpha>=0) for (int channel=0; channel<4; ++channel)
        {
            bool interpolate=channel==3 ? alpha!=0 && alpha!=4 : linear;
            if (interpolate) expected[channel]=(a[channel]*(1-fx)+b[channel]*fx)*(1-fy)+(c[channel]*(1-fx)+d[channel]*fx)*fy;
        }
        if (expected[3]<=0) expected={};
        for (int channel=0; channel<4; ++channel) if (std::abs(expected[channel]-actual[channel])>0.0001f)
        {
            std::fprintf(stderr,"alpha=%d linear=%d noWrap=%d finite=%d pos=%g,%g channel=%d expected=%g actual=%g\n",
                alpha,linear,noWrap,finite,pos[0],pos[1],channel,expected[channel],actual[channel]);
            Require(false,"enhanced BG sampling domain/wrap/interpolation");
        }
        ++cases;
    }
    gl->glDeleteTextures(2,textures); gl->glDeleteFramebuffers(1,&fb); gl->glDeleteVertexArrays(1,&vao);
    std::printf("%d enhanced BG sampling cases passed.\n",cases);
}
