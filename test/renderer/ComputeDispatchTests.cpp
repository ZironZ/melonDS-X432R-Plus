// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <vector>
#include "../../src/GPU3D_Compute_shaders.h"

namespace S = melonDS::ComputeRendererShaders;
static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void Compile(QOpenGLShaderProgram& shader, const std::string& source)
{
    bool ok = shader.addShaderFromSourceCode(QOpenGLShader::Compute, source.c_str()) && shader.link();
    if (!ok) std::fprintf(stderr, "%s\n", shader.log().toUtf8().constData());
    Check(ok, "production dispatch shader compilation");
}
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format; format.setVersion(4,3); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format); Check(context.create(), "GL context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Check(context.makeCurrent(&surface), "GL surface");
    auto* gl=context.extraFunctions(); gl->initializeOpenGLFunctions();
    // Include zero work, both sides of dispatch boundaries, the observed failing
    // batch size, and the maximum 21-bit work index supported by descriptors.
    const std::array<uint32_t,10> counts{0,1,65534,65535,65536,66636,131069,131070,131071,2097151};
    const uint32_t total=std::accumulate(counts.begin(),counts.end(),0U);
    constexpr int offsets=256*4, realCounts=offsets+256, sortCount=realCounts+256;
    std::array<uint32_t,sortCount+4> bins{};
    for(size_t v=0;v<counts.size();v++) { bins[v*4]=1; bins[v*4+1]=1; bins[v*4+2]=counts[v]; }
    bins[3]=total;
    std::array<uint32_t,148> meta{}; meta[1]=counts.size();
    GLuint buffers[3]; gl->glGenBuffers(3,buffers);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(bins),bins.data(),GL_DYNAMIC_DRAW);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,buffers[0]);
    gl->glBindBuffer(GL_UNIFORM_BUFFER,buffers[1]);
    gl->glBufferData(GL_UNIFORM_BUFFER,sizeof(meta),meta.data(),GL_STATIC_DRAW);
    gl->glBindBufferBase(GL_UNIFORM_BUFFER,0,buffers[1]);
    const std::string prefix="#version 430 core\n#define TileSize 8\n#define CoarseTileCountY 4\n#define ScreenWidth 256\n#define ScreenHeight 192\n";
    QOpenGLShaderProgram offsetsShader;
    Compile(offsetsShader,prefix+S::Common+S::CalcOffsets); offsetsShader.bind();
    gl->glDispatchCompute(1,1,1);
    gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);
    auto* mapped=gl->glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,sizeof(bins),GL_MAP_READ_BIT);
    Check(mapped!=nullptr,"bin header readback"); std::memcpy(bins.data(),mapped,sizeof(bins)); gl->glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    Check(bins[sortCount]==(total+31)/32 && bins[7]==total,"sort count and prefix total preserved");
    for(size_t v=0;v<counts.size();v++)
    {
        uint32_t x=bins[v*4],y=bins[v*4+1],z=bins[v*4+2];
        Check(x==1 && y>=1 && y<=65535 && z<=65535,"portable indirect dimensions");
        Check(y*z>=counts[v] && y*z-counts[v]<y,"minimal tail overdispatch");
        Check(bins[realCounts+v]==counts[v],"real batch size retained");
        if(counts[v]<=65535) Check(y==1 && z==counts[v],"small dispatch unchanged");
    }
    // Execute the generated indirect dispatches with the production index and
    // tail guard. Every real item must run once, including across batch offsets.
    std::string source=prefix+S::Common+S::BinningBuffer+R"(
layout(local_size_x=1) in;
layout(std430,binding=8) buffer Results { uint hits[]; };
uniform uint variant;
void main() {
    uint index;
    if (!GetRasteriseWorkIndex(variant, index)) return;
    atomicAdd(hits[SortedWorkOffset[variant]+index],1U);
}
)";
    if(app.arguments().contains("--broken-index"))
    {
        const std::string good="gl_WorkGroupID.y * gl_NumWorkGroups.z + gl_WorkGroupID.z";
        source.replace(source.find(good),good.size(),"gl_WorkGroupID.z");
    }
    QOpenGLShaderProgram dispatch; Compile(dispatch,source); dispatch.bind();
    std::vector<uint32_t> hits(total+64,0); for(size_t i=total;i<hits.size();i++) hits[i]=0xDEADBEEF;
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[2]);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER,hits.size()*4,hits.data(),GL_DYNAMIC_DRAW);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,buffers[2]);
    gl->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[0]);
    for(size_t v=0;v<counts.size();v++) { gl->glUniform1ui(dispatch.uniformLocation("variant"),GLuint(v)); gl->glDispatchComputeIndirect(v*16); }
    gl->glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    mapped=gl->glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,hits.size()*4,GL_MAP_READ_BIT);
    Check(mapped!=nullptr,"work coverage readback");
    auto* actual=static_cast<uint32_t*>(mapped);
    for(size_t i=0;i<hits.size();i++)
    {
        const uint32_t expected=i<total?1U:0xDEADBEEF;
        if(actual[i]!=expected)
            std::fprintf(stderr,"job %zu: expected %u, got %u\n",i,expected,actual[i]);
        Check(actual[i]==expected,"each job exactly once, tail untouched");
    }
    gl->glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    Check(gl->glGetError()==GL_NO_ERROR,"dispatch GL error"); gl->glDeleteBuffers(3,buffers);
    std::printf("PASS: %zu indirect batch boundaries, %u jobs exactly once\n",counts.size(),total);
}
