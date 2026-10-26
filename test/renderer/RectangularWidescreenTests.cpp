// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "glad/glad.h"
#include "OpenGL_shaders/WideRectangular.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
using Pixel = std::array<int,4>;
struct Edges { unsigned Current[4], Captured[4]; };

int main(int argc, char** argv)
{
    QGuiApplication app(argc,argv);
    QSurfaceFormat format; format.setVersion(3,2); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format); Check(context.create(),"context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Check(context.makeCurrent(&surface),"surface");
    Check(gladLoadGLLoader([](const char* name) -> void* {
        return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
    }),"GL loader");
    QOpenGLShaderProgram program;
    Check(program.addShaderFromSourceCode(QOpenGLShader::Vertex,R"(#version 140
        smooth out vec3 fTexcoord;
        void main() {
            vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
            fTexcoord=vec3(p,0); gl_Position=vec4(p*2-1,0,1);
        })"),"vertex");
    std::string source=kWideRectangularFS;
    if (app.arguments().contains("--broken-corner"))
    {
        const std::string rule="(horizontal & renderingBits) == (vertical & renderingBits) ? horizontal : 0u";
        source.replace(source.find(rule),rule.size(),"horizontal");
    }
    if (app.arguments().contains("--broken-repair-metadata"))
    {
        const std::string rule="(horizontal & renderingBits) == (vertical & renderingBits) ? horizontal : 0u";
        source.replace(source.find(rule),rule.size(),"horizontal == vertical ? horizontal : 0u");
    }
    Check(program.addShaderFromSourceCode(QOpenGLShader::Fragment,source.c_str()),"production rectangular shader");
    glBindFragDataLocation(program.programId(),0,"oTopColor");
    glBindFragDataLocation(program.programId(),1,"oBottomColor");
    Check(program.link() && program.bind(),"link");
    auto loc=[&](const char* name) { const int l=glGetUniformLocation(program.programId(),name); Check(l>=0,name); return l; };
    GLuint vao,ubo,windowUbo,fb,textures[4];
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&ubo); glGenBuffers(1,&windowUbo);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo); glBindBufferBase(GL_UNIFORM_BUFFER,1,windowUbo);
    const GLuint block=glGetUniformBlockIndex(program.programId(),"ubFinalPassConfig");
    glUniformBlockBinding(program.programId(),block,0);
    GLint bytes; glGetActiveUniformBlockiv(program.programId(),block,GL_UNIFORM_BLOCK_DATA_SIZE,&bytes);
    std::vector<unsigned> config((bytes+3)/4);
    glUniformBlockBinding(program.programId(),glGetUniformBlockIndex(program.programId(),"ubWideWindows"),1);
    std::array<Edges,448> edges{};
    GLint maxBlock; glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&maxBlock);
    Check(sizeof(edges)<=16384 && sizeof(edges)<=size_t(maxBlock),"GL 3.2 minimum UBO budget");
    glGenFramebuffers(1,&fb); glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glGenTextures(4,textures);
    const GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}; glDrawBuffers(2,outputs);
    glUniform1i(loc("InputA"),0); glUniform1i(loc("InputB"),1);
    int cases=0;
    for (const auto size : {std::array<int,2>{384,288},{512,288},{384,512}})
    for (int scale : {1,3})
    {
        const int w=size[0]*scale,h=size[1]*scale;
        std::vector<unsigned char> input[2];
        for (int t=0;t<4;++t)
        {
            glActiveTexture(GL_TEXTURE0+(t&1)); glBindTexture(GL_TEXTURE_2D,textures[t]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            if (t<2)
            {
                input[t].resize(w*h*4);
                for (int y=0;y<h;++y) for (int x=0;x<w;++x)
                {
                    const int p=(y*w+x)*4;
                    input[t][p]=((x/scale+t*7)%64)*4;
                    input[t][p+1]=((y/scale+t*13)%64)*4;
                    input[t][p+2]=((x/scale+y/scale+t*19)%64)*4;
                    input[t][p+3]=(x/scale)%7==0 ? 0 : (x/scale)%7==3 ? 128 : 248;
                }
            }
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,t<2?input[t].data():nullptr);
            if (t>=2) glFramebufferTexture2D(GL_FRAMEBUFFER,outputs[t-2],GL_TEXTURE_2D,textures[t],0);
        }
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"framebuffer");
        for (int t=0;t<2;++t) { glActiveTexture(GL_TEXTURE0+t); glBindTexture(GL_TEXTURE_2D,textures[t]); }
        glViewport(0,0,w,h); glUniform2i(loc("uSize"),size[0],size[1]);
        for (int scenario=0;scenario<16;++scenario)
        {
            // Cover, brightness, unknown corners, raw/composed capture A,
            // VRAM B, blended feedback, missing history with white master fade.
            const int operation=scenario==4||scenario==5?1:scenario==6?2:scenario==7?3:0;
            const bool raw=scenario==4;
            const unsigned policy=scenario==1?1u|(31u<<9):scenario==2?3u|(3u<<2)|(8u<<4):
                (scenario==9||scenario==15)?3u|(31u<<9)|(1u<<25):3u;
            for (auto& e : edges) for (int slot=0;slot<4;++slot)
            { e.Current[slot]=policy; e.Captured[slot]=1u|(31u<<9); }
            if (scenario==3) for (int x=0;x<256;++x) edges[192+x].Current[0]=edges[192+x].Current[2]=0;
            if (scenario==13)
            {
                for(int y : {0,1,190,191}) for(int slot=0;slot<4;++slot) edges[y].Current[slot]=1;
                for(int x=0;x<256;++x) for(int slot=0;slot<4;++slot) edges[192+x].Current[slot]=3u|(1u<<28);
            }
            // A repair flag describes provenance, not different rendering.
            // Reproduce Zelda's vertical seams and the transposed case with
            // material-alpha blending, checking every overlap/corner pixel.
            if (scenario==14)
            {
                for(int y=0;y<192;++y) for(int slot=0;slot<4;++slot) edges[y].Current[slot] |= 1u<<28;
                for(int x : {0,255}) for(int slot=0;slot<4;++slot) edges[192+x].Current[slot] |= 1u<<28;
            }
            if (scenario==15)
            {
                for(int x=0;x<256;++x) for(int slot=0;slot<4;++slot) edges[192+x].Current[slot] |= 1u<<28;
                for(int y : {0,191}) for(int slot=0;slot<4;++slot) edges[y].Current[slot] |= 1u<<28;
            }
            for (int y=0;y<192;++y) config[y]=(y&1);
            glBindBuffer(GL_UNIFORM_BUFFER,ubo); glBufferData(GL_UNIFORM_BUFFER,config.size()*4,config.data(),GL_STATIC_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER,windowUbo); glBufferData(GL_UNIFORM_BUFFER,sizeof(edges),edges.data(),GL_STATIC_DRAW);
            glUniform1i(loc("uOperation"),operation); glUniform1i(loc("uRaw3D"),raw);
            glUniform2i(loc("uOverlap"),scenario>=13?2:0,scenario>=13?2:0);
            glUniform2i(loc("uBlend"),8,8); glUniform2i(loc("uModes"),scenario==8?0:scenario==11?3:1,scenario==8?0:scenario==11?3:2);
            glUniform2i(loc("uDisplays"),1,scenario==10?0:1);
            glUniform3i(loc("uPresentation[0]"),scenario==12?63:-1,scenario==8?1:0,scenario==8?16:0);
            glUniform3i(loc("uPresentation[1]"),scenario==12?63:-1,scenario==8?1:0,scenario==8?16:0);
            glDrawArrays(GL_TRIANGLES,0,3);
            for (int output=0;output<2;++output)
            {
                std::vector<unsigned char> result(w*h*4); glReadBuffer(outputs[output]);
                glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
                for (int y=0;y<h;++y) for (int x=0;x<w;++x)
                {
                    const int nx=x/scale-(size[0]-256)/2, ny=y/scale-(size[1]-192)/2;
                    const int line=std::clamp(ny,0,191),engine=(line&1)?output:1-output;
                    const int p=(y*w+x)*4;
                    auto sourcePixel=[&](int t) { Pixel c{}; for(int i=0;i<4;++i)c[i]=input[t][p+i]; return c; };
                    auto composed=[&](Pixel c, bool valid) {
                        if (!valid) return Pixel{0,0,0,0};
                        const bool scene=scenario!=1 && c[3]!=0;
                        if (!scene) c=Pixel{248,0,0,255};
                        if (scenario==2 && scene) for(int i=0;i<3;++i) { const int v=c[i]>>2; c[i]=(v-((v*8+7)>>4))<<2; }
                        if ((scenario==9 || scenario==15) && scene)
                        {
                            const int a=(c[3]>>3)+1;
                            for(int i=0;i<3;++i) c[i]=(((c[i]>>2)*a+(i==0?62:0)*(32-a)+16)>>5)<<2;
                        }
                        c[3]=255; return c;
                    };
                    const bool valid=scenario!=3||ny>=0;
                    Pixel expected{};
                    if (operation)
                    {
                        const Pixel a=raw?sourcePixel(0):composed(sourcePixel(0),true),b=sourcePixel(1);
                        expected=operation==2?b:a;
                        if(operation==3)
                        {
                            for(int i=0;i<3;++i) expected[i]=std::min(31,(((a[3]?a[i]>>3:0)*8+(b[3]?b[i]>>3:0)*8+8)>>4))<<3;
                            expected[3]=(a[3]||b[3])?255:0;
                        }
                    }
                    else if(scenario==8||scenario==12) expected=Pixel{252,252,252,255};
                    else if(scenario==10&&output==1) expected=Pixel{0,0,0,255};
                    else if(scenario==11) { expected=sourcePixel(engine);expected[3]=255; }
                    else if(scenario==13&&(nx<0||nx>=256)&&(ny<2||ny>=190)) expected=Pixel{0,0,0,255};
                    else { expected=composed(sourcePixel(engine),valid); expected[3]=255; }
                    for(int i=0;i<4;++i)
                        if(result[p+i]!=expected[i])
                        {
                            std::fprintf(stderr,"case=%d size=%dx%d scale=%d output=%d pixel=%d,%d native=%d,%d channel=%d actual=%d expected=%d\n",
                                scenario,size[0],size[1],scale,output,x,y,nx,ny,i,result[p+i],expected[i]);
                            Check(false,"perimeter/corner/capture/swap/fade oracle");
                        }
                }
            }
            Check(glGetError()==GL_NO_ERROR,"draw/readback GL error"); ++cases;
        }
    }
    glDeleteTextures(4,textures); glDeleteFramebuffers(1,&fb); glDeleteBuffers(1,&ubo);
    glDeleteBuffers(1,&windowUbo); glDeleteVertexArrays(1,&vao);
    std::printf("PASS: %d rectangular production shader cases, both outputs, every pixel\n",cases);
}
