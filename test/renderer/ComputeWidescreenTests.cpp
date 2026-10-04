// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../../src/GPU3D_Compute_shaders.h"

namespace S = melonDS::ComputeRendererShaders;
static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void Compile(QOpenGLShaderProgram& shader, const std::string& source)
{
    bool ok = shader.addShaderFromSourceCode(QOpenGLShader::Compute, source.c_str()) && shader.link() && shader.bind();
    if (!ok) std::fprintf(stderr, "%s\n", shader.log().toUtf8().constData());
    Check(ok, "production Compute shader compilation");
}
static std::string Defines(int width, int storage, int height, int native, int tile=8)
{
    return "#version 430 core\n#define OutputWidth " + std::to_string(width)
        + "\n#define ScreenWidth " + std::to_string(storage)
        + "\n#define ScreenHeight " + std::to_string(height)
        + "\n#define OutputHeight " + std::to_string(height)
        + "\n#define NativeHeight " + std::to_string(height)
        + "\n#define NativeWidth " + std::to_string(native)
        + "\n#define TileSize " + std::to_string(tile)
        + "\n#define CoarseTileCountY 4\n#define ClearCoarseBinMaskLocalSize 64\n"
          "#define MaxWorkTiles 1\n#define CoarseTileArea 32\n";
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format; format.setVersion(4,3); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format); Check(context.create(), "GL context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Check(context.makeCurrent(&surface), "GL surface");
    auto* gl=context.extraFunctions(); gl->initializeOpenGLFunctions();
    const bool brokenCenter=app.arguments().contains("--broken-center");
    const bool brokenPadding=app.arguments().contains("--broken-padding");
    GLuint resultBuffer, metaBuffer, fb;
    gl->glGenBuffers(1,&resultBuffer); gl->glGenBuffers(1,&metaBuffer); gl->glGenFramebuffers(1,&fb);
    gl->glBindFramebuffer(GL_FRAMEBUFFER,fb);
    int cases=0;
    for (int baseWidth : {256,308,342,448,682,768}) for (int scale : {1,3,5,16})
    {
        const int width=baseWidth*scale, native=256*scale, storage=(width+255)/256*256, height=4, stride=storage*height;
        for (bool edges : {false,true})
        {
            std::vector<uint32_t> data(stride*6,0);
            std::array<uint32_t,148> meta{};
            // Polygon 1 at depth 100; clear polygon 0 lies behind it. Padding
            // deliberately has polygon 1, so treating it as a right neighbor
            // incorrectly suppresses marking the real right edge.
            meta[4+2]=63; // polygon 1's red edge color
            meta[141]=1000; // ClearDepth
            for (int y=0;y<height;y++) for(int x=0;x<storage;x++)
            {
                const int i=y*storage+x;
                data[i]=uint32_t((x%63)|((y+1)<<8)|(((x+y)%63)<<16)|(31<<24));
                data[i+2*stride]=100;
                data[i+4*stride]=(1U<<24)|(edges?1U:0U);
            }
            gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER,resultBuffer);
            gl->glBufferData(GL_SHADER_STORAGE_BUFFER,data.size()*4,data.data(),GL_STATIC_DRAW);
            gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,resultBuffer);
            gl->glBindBuffer(GL_UNIFORM_BUFFER,metaBuffer);
            gl->glBufferData(GL_UNIFORM_BUFFER,sizeof(meta),meta.data(),GL_STATIC_DRAW);
            gl->glBindBufferBase(GL_UNIFORM_BUFFER,0,metaBuffer);
            GLuint textures[2]; gl->glGenTextures(2,textures);
            for(int i=0;i<2;i++)
            {
                gl->glBindTexture(GL_TEXTURE_2D,textures[i]);
                gl->glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,i?native:width,height);
                gl->glBindImageTexture(i,textures[i],0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);
            }
            std::string source=Defines(width,storage,height,native)+"#define Widescreen\n"+(edges?"#define EdgeMarking\n":"")+S::Common+S::FinalPass;
            const std::string centerOffset="(OutputWidth - NativeWidth) / 2";
            if(brokenCenter) source.replace(source.find(centerOffset),centerOffset.size(),"0");
            QOpenGLShaderProgram shader; Compile(shader,source);
            gl->glDispatchCompute(storage/32,height,1);
            gl->glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
            std::vector<unsigned char> full(width*height*4),center(native*height*4);
            for(int i=0;i<2;i++)
            {
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
                Check(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"output framebuffer");
                gl->glReadPixels(0,0,i?native:width,height,GL_RGBA,GL_UNSIGNED_BYTE,i?center.data():full.data());
            }
            for(int y=0;y<height;y++) for(int x=0;x<width;x++)
            {
                const bool edge=edges&&(x==0||x==width-1||y==0||y==height-1);
                std::array<int,4> expected{edge?63:x%63,edge?0:y+1,edge?0:(x+y)%63,63};
                for(int c=0;c<4;c++) Check(std::abs(int(full[(y*width+x)*4+c])-int(std::lround(expected[c]*255.0/63.0)))<=1,"visible width/stride/edge pixel");
            }
            for(int y=0;y<height;y++) Check(std::memcmp(center.data()+y*native*4,full.data()+(y*width+(width-native)/2)*4,native*4)==0,"native center is exact");
            Check(gl->glGetError()==GL_NO_ERROR,"final pass GL error");
            gl->glDeleteTextures(2,textures); cases++;
        }
    }

    // 16:10 at 5x leaves half of the last 64-thread clear workgroup unused.
    const int tiles=(1664/16)*(960/16), header=256*4+256+256+4, guard=128;
    std::vector<uint32_t> bins(header+tiles*2+guard,0xDEADBEEF);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER,resultBuffer);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER,bins.size()*4,bins.data(),GL_STATIC_DRAW);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,resultBuffer);
    std::string clear=Defines(1540,1664,960,1280,16)+S::Common+S::ClearCoarseBinMask;
    const std::string tailGuard="if (gl_GlobalInvocationID.x >= uint(TilesPerLine * TileLines)) return;";
    if(brokenPadding) clear.replace(clear.find(tailGuard),tailGuard.size(),"");
    QOpenGLShaderProgram clearShader; Compile(clearShader,clear);
    gl->glDispatchCompute((tiles+63)/64,1,1); gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    auto* values=static_cast<uint32_t*>(gl->glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,bins.size()*4,GL_MAP_READ_BIT));
    Check(values!=nullptr,"clear readback");
    for(size_t i=0;i<bins.size();i++) Check(values[i]==(i>=header&&i<header+tiles*2?0U:0xDEADBEEFU),"coarse clear tail and guard");
    gl->glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

    // Directly exercise the production slope helpers without allocating a
    // full 16x ultrawide frame's work buffers. CPU oracle uses 64-bit integers.
    for(int width : {4096,6144,12288})
    {
        QOpenGLShaderProgram slope;
        Compile(slope,Defines(width,width,3072,4096)+"#define InterpSpans\n"+S::Common+S::YSpanSetupBuffer+R"(
layout(local_size_x=1) in;
layout(std430,binding=8) buffer Answer { ivec4 answer; };
uniform int xlen, ylen, negative, side, ypos;
void main() {
    YSpanSetup s;
    s.X0=negative!=0?xlen:0; s.X1=negative!=0?0:xlen;
    s.XMin=0; s.XMax=xlen-1; s.Y0=0; s.Y1=ylen;
    s.Increment=int(uint(xlen)*uint(262144/ylen));
    s.DxInitial=side!=0?(negative!=0?393216:int(uint(s.Increment)-131072U)):(negative!=0?int(uint(s.Increment)+131072U):131072);
    s.XCovIncr=(ylen<<10)/xlen;
    SlopeValue dx=CalculateDx(ypos,s);
    int len,cov; EdgeParams_XMajor(side!=0,dx,s,len,cov);
    answer=ivec4(CalculateX(dx,s),len,cov,SlopeIncrement(s)>262144?1:0);
})");
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER,resultBuffer);
        gl->glBufferData(GL_SHADER_STORAGE_BUFFER,16,nullptr,GL_DYNAMIC_READ);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,resultBuffer);
        for(int xlen : {width/2,width-1}) for(int ylen : {1,3,31,192,1000})
        for(int negative : {0,1}) for(int side : {0,1}) for(int y : {0,ylen-1})
        {
            if(width==4096&&xlen>2048) continue; // keep native oracle within the old arithmetic range
            slope.setUniformValue("xlen",xlen); slope.setUniformValue("ylen",ylen);
            slope.setUniformValue("negative",negative); slope.setUniformValue("side",side); slope.setUniformValue("ypos",y);
            gl->glDispatchCompute(1,1,1); gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            int64_t inc=int64_t(xlen)*(262144/ylen);
            int64_t dx=(side?(negative?393216:inc-131072):(negative?inc+131072:131072))+y*inc;
            int len=side!=negative?(dx>>18)-((dx-inc)>>18):((dx+inc)>>18)-(dx>>18);
            int startx=dx>>18; if(negative) startx=xlen-startx; if(side) startx=startx-len+1;
            uint64_t numerator=uint64_t(uint32_t(startx*1024+511))*ylen;
            if(width<=4096) numerator=uint32_t(numerator);
            uint32_t cov=0x80000000U|(((numerator/xlen)&1023)<<12)|(((ylen<<10)/xlen)&1023);
            std::array<int32_t,4> expected{std::clamp(int(negative?xlen-(dx>>18):dx>>18),0,xlen-1),len,int32_t(cov),1};
            auto* actual=gl->glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,16,GL_MAP_READ_BIT);
            Check(actual&&std::memcmp(actual,expected.data(),16)==0,"wide fixed-point slope/coverage");
            gl->glUnmapBuffer(GL_SHADER_STORAGE_BUFFER); cases++;
        }
    }
    Check(gl->glGetError()==GL_NO_ERROR,"Compute tests GL error");
    gl->glDeleteBuffers(1,&resultBuffer); gl->glDeleteBuffers(1,&metaBuffer); gl->glDeleteFramebuffers(1,&fb);
    std::printf("PASS: %d Compute output/center/edge/slope cases and partial clear guard\n",cases);
}
