/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "GPU_OpenGL.h"

#include <algorithm>
#include <assert.h>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "NDS.h"
#include "GPU.h"

#include <cmath>

namespace melonDS
{

namespace
{
u64 ElapsedUS(std::chrono::steady_clock::time_point start)
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
}
}

#include "OpenGL_shaders/3DClearVS.h"
#include "OpenGL_shaders/3DClearFS.h"
#include "OpenGL_shaders/3DClearBitmapVS.h"
#include "OpenGL_shaders/3DClearBitmapFS.h"
#include "OpenGL_shaders/3DRenderVS.h"
#include "OpenGL_shaders/3DRenderFS.h"
#include "OpenGL_shaders/3DFinalPassVS.h"
#include "OpenGL_shaders/3DFinalPassEdgeFS.h"
#include "OpenGL_shaders/3DFinalPassFogFS.h"

static bool AddUniqueTexcoord(s16 value, s16 values[4], int& count)
{
    for (int i = 0; i < count; i++)
    {
        if (values[i] == value)
            return true;
    }
    if (count >= 2)
        return false;

    values[count++] = value;
    return true;
}

static bool AddUniquePositionCoord(s32 value, s32 values[4], int& count)
{
    for (int i = 0; i < count; i++)
    {
        if (values[i] == value)
            return true;
    }
    if (count >= 2)
        return false;

    values[count++] = value;
    return true;
}

static bool BuildSpriteUVInsetBounds(const Polygon* poly, u32 texWidth, u32 texHeight,
                                     TextureSpriteUVInsetBounds& bounds)
{
    bounds = {};
    if (poly->Type == 1 || poly->NumVertices != 4)
        return false;
    if (((poly->TexParam >> 16) & 0xF) != 0)
        return false;
    if (((poly->TexParam >> 30) & 0x3) != 0)
        return false;

    s32 uniqueX[4] = {};
    s32 uniqueY[4] = {};
    int uniqueXCount = 0;
    int uniqueYCount = 0;
    s16 uniqueU[4] = {};
    s16 uniqueV[4] = {};
    int uniqueUCount = 0;
    int uniqueVCount = 0;
    const s32 maxTexU = static_cast<s32>(texWidth << 4);
    const s32 maxTexV = static_cast<s32>(texHeight << 4);
    s32 minU = 0x7FFFFFFF, minV = 0x7FFFFFFF;
    s32 maxU = -0x7FFFFFFF, maxV = -0x7FFFFFFF;

    for (u32 i = 0; i < poly->NumVertices; i++)
    {
        const Vertex* vtx = poly->Vertices[i];
        if (!AddUniquePositionCoord(vtx->FinalPosition[0], uniqueX, uniqueXCount) ||
            !AddUniquePositionCoord(vtx->FinalPosition[1], uniqueY, uniqueYCount))
            return false;

        const s32 u = vtx->TexCoords[0];
        const s32 v = vtx->TexCoords[1];
        if (u < 0 || v < 0 || u > maxTexU || v > maxTexV)
            return false;
        if (!AddUniqueTexcoord(static_cast<s16>(u), uniqueU, uniqueUCount) ||
            !AddUniqueTexcoord(static_cast<s16>(v), uniqueV, uniqueVCount))
            return false;

        minU = std::min(minU, u);
        minV = std::min(minV, v);
        maxU = std::max(maxU, u);
        maxV = std::max(maxV, v);
    }

    if (uniqueXCount != 2 || uniqueYCount != 2 ||
        uniqueUCount != 2 || uniqueVCount != 2)
        return false;
    if (maxU - minU < 32 || maxV - minV < 32)
        return false;

    bounds.Valid = true;
    bounds.U0 = static_cast<u16>(minU);
    bounds.V0 = static_cast<u16>(minV);
    bounds.U1 = static_cast<u16>(maxU);
    bounds.V1 = static_cast<u16>(maxV);
    return true;
}

static bool BuildSafeTextureSamplingBounds(const Polygon* poly, u32 texWidth, u32 texHeight, TextureSamplingBounds& bounds)
{
    bounds = {};
    if (poly->Type == 1 || poly->NumVertices != 4)
        return false;
    if (((poly->TexParam >> 16) & 0xF) != 0)
        return false;

    const s32 maxTexU = static_cast<s32>(texWidth << 4);
    const s32 maxTexV = static_cast<s32>(texHeight << 4);
    s32 minU = 0x7FFFFFFF, minV = 0x7FFFFFFF;
    s32 maxU = -0x7FFFFFFF, maxV = -0x7FFFFFFF;
    s16 uniqueU[4] = {};
    s16 uniqueV[4] = {};
    int uniqueUCount = 0;
    int uniqueVCount = 0;

    for (u32 i = 0; i < poly->NumVertices; i++)
    {
        const s32 u = poly->Vertices[i]->TexCoords[0];
        const s32 v = poly->Vertices[i]->TexCoords[1];
        if (u < 0 || v < 0 || u > maxTexU || v > maxTexV)
            return false;
        if (!AddUniqueTexcoord(static_cast<s16>(u), uniqueU, uniqueUCount) ||
            !AddUniqueTexcoord(static_cast<s16>(v), uniqueV, uniqueVCount))
            return false;

        minU = std::min(minU, u);
        minV = std::min(minV, v);
        maxU = std::max(maxU, u);
        maxV = std::max(maxV, v);
    }

    if (uniqueUCount != 2 || uniqueVCount != 2 || minU >= maxU || minV >= maxV)
        return false;

    u32 x0 = static_cast<u32>(minU >> 4);
    u32 y0 = static_cast<u32>(minV >> 4);
    u32 x1 = std::min<u32>(texWidth, static_cast<u32>((maxU + 15) >> 4));
    u32 y1 = std::min<u32>(texHeight, static_cast<u32>((maxV + 15) >> 4));

    if (x0 > 0) x0--;
    if (y0 > 0) y0--;
    if (x1 < texWidth) x1++;
    if (y1 < texHeight) y1++;
    if (x0 >= x1 || y0 >= y1)
        return false;
    if (x0 == 0 && y0 == 0 && x1 == texWidth && y1 == texHeight)
        return false;

    bounds.Valid = true;
    bounds.X0 = static_cast<u16>(x0);
    bounds.Y0 = static_cast<u16>(y0);
    bounds.X1 = static_cast<u16>(x1);
    bounds.Y1 = static_cast<u16>(y1);
    return true;
}

static bool IsLargeTranslucentTextureDraw(const Polygon* poly)
{
    if (poly->Type == 1 || poly->NumVertices < 3)
        return false;
    if (!poly->Translucent && (((poly->Attr >> 16) & 0x1F) == 31))
        return false;

    s32 minX = 0x7FFFFFFF, minY = 0x7FFFFFFF;
    s32 maxX = -0x7FFFFFFF, maxY = -0x7FFFFFFF;
    for (u32 i = 0; i < poly->NumVertices; i++)
    {
        const Vertex* vtx = poly->Vertices[i];
        const s32 x = static_cast<s32>(vtx->FinalPosition[0]);
        const s32 y = static_cast<s32>(vtx->FinalPosition[1]);
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }

    const s32 width = maxX - minX;
    const s32 height = maxY - minY;
    const s64 area = static_cast<s64>(width) * static_cast<s64>(height);
    return (width >= 80 && height >= 40) ||
           (width >= 160 && height >= 24) ||
           (width >= 40 && height >= 80) ||
           (area >= 4096 && (width >= 64 || height >= 64));
}

static s32 ClampTextureCoordinate(s32 coord, s32 maxCoord)
{
    return std::min<s32>(std::max<s32>(coord, 0), maxCoord);
}

struct TextureFrameEdgeExtendCandidate
{
    u32 TexParam = 0;
    u32 TexPalette = 0;
    u32 Width = 0;
    u32 Height = 0;
    bool Valid = true;
    bool Seen = false;
    u32 X0 = 0;
    u32 Y0 = 0;
    u32 X1 = 0;
    u32 Y1 = 0;
};

using TextureFrameEdgeExtendCandidateMap = std::unordered_map<u64, size_t>;

static u64 MakeTextureFrameEdgeExtendCandidateKey(u32 texParam, u32 texPalette)
{
    return (static_cast<u64>(texParam) << 32) | texPalette;
}

static bool GetTextureDrawBounds(const Polygon* poly, u32 texWidth, u32 texHeight,
                                 u32& x0, u32& y0, u32& x1, u32& y1)
{
    if (poly->Type == 1 || poly->NumVertices < 3 || poly->NumVertices > 10)
        return false;

    const s32 maxTexU = static_cast<s32>(texWidth << 4);
    const s32 maxTexV = static_cast<s32>(texHeight << 4);
    s32 minU = 0x7FFFFFFF, minV = 0x7FFFFFFF;
    s32 maxU = -0x7FFFFFFF, maxV = -0x7FFFFFFF;

    for (u32 i = 0; i < poly->NumVertices; i++)
    {
        const s32 u = ClampTextureCoordinate(poly->Vertices[i]->TexCoords[0], maxTexU);
        const s32 v = ClampTextureCoordinate(poly->Vertices[i]->TexCoords[1], maxTexV);
        minU = std::min(minU, u);
        minV = std::min(minV, v);
        maxU = std::max(maxU, u);
        maxV = std::max(maxV, v);
    }

    if (minU >= maxU || minV >= maxV)
        return false;

    x0 = static_cast<u32>(std::max<s32>(0, minU >> 4));
    y0 = static_cast<u32>(std::max<s32>(0, minV >> 4));
    x1 = std::min<u32>(texWidth, static_cast<u32>((maxU + 15) >> 4));
    y1 = std::min<u32>(texHeight, static_cast<u32>((maxV + 15) >> 4));
    return x0 < x1 && y0 < y1;
}

static TextureFrameEdgeExtendCandidate& FindOrAddEdgeExtendCandidate(
    std::vector<TextureFrameEdgeExtendCandidate>& candidates,
    TextureFrameEdgeExtendCandidateMap& candidateMap,
    u32 texParam, u32 texPalette, u32 width, u32 height)
{
    const u64 key = MakeTextureFrameEdgeExtendCandidateKey(texParam, texPalette);
    auto it = candidateMap.find(key);
    if (it != candidateMap.end())
        return candidates[it->second];

    candidates.push_back({});
    candidateMap.emplace(key, candidates.size() - 1);
    TextureFrameEdgeExtendCandidate& candidate = candidates.back();
    candidate.TexParam = texParam;
    candidate.TexPalette = texPalette;
    candidate.Width = width;
    candidate.Height = height;
    return candidate;
}

static void AccumulateTextureFrameEdgeExtendCandidate(
    const Polygon* poly, bool texEnable,
    std::vector<TextureFrameEdgeExtendCandidate>& candidates,
    TextureFrameEdgeExtendCandidateMap& candidateMap)
{
    const u32 texParam = poly->TexParam & ~0xC00F0000;
    const u32 textype = (texParam >> 26) & 0x7;
    if (!texEnable || !textype)
        return;

    const u32 texWidth = TextureWidth(texParam);
    const u32 texHeight = TextureHeight(texParam);
    TextureFrameEdgeExtendCandidate& candidate =
        FindOrAddEdgeExtendCandidate(candidates, candidateMap, texParam, poly->TexPalette, texWidth, texHeight);

    u32 x0, y0, x1, y1;
    const bool eligible = ((poly->TexParam >> 16) & 0xF) == 0 &&
        GetTextureDrawBounds(poly, texWidth, texHeight, x0, y0, x1, y1);
    if (!eligible)
    {
        candidate.Valid = false;
        return;
    }

    if (!candidate.Seen)
    {
        candidate.Seen = true;
        candidate.X0 = x0;
        candidate.Y0 = y0;
        candidate.X1 = x1;
        candidate.Y1 = y1;
    }
    else
    {
        candidate.X0 = std::min(candidate.X0, x0);
        candidate.Y0 = std::min(candidate.Y0, y0);
        candidate.X1 = std::max(candidate.X1, x1);
        candidate.Y1 = std::max(candidate.Y1, y1);
    }
}

static bool FindTextureFrameEdgeExtendBounds(const std::vector<TextureFrameEdgeExtendCandidate>& candidates,
                                             const TextureFrameEdgeExtendCandidateMap& candidateMap,
                                             u32 texParam, u32 texPalette, TextureSamplingBounds& bounds)
{
    const u64 key = MakeTextureFrameEdgeExtendCandidateKey(texParam, texPalette);
    auto it = candidateMap.find(key);
    if (it == candidateMap.end() || it->second >= candidates.size())
        return false;

    const TextureFrameEdgeExtendCandidate& candidate = candidates[it->second];
    if (!candidate.Valid || !candidate.Seen)
        return false;
    if (candidate.Width == 0 || candidate.Height == 0 ||
        candidate.X0 >= candidate.X1 || candidate.Y0 >= candidate.Y1)
        return false;
    if (candidate.X0 == 0 && candidate.Y0 == 0 &&
        candidate.X1 == candidate.Width && candidate.Y1 == candidate.Height)
        return false;

    const u32 horizontalTailThreshold = std::max<u32>(16, candidate.Width / 8);
    const u32 verticalTailThreshold = std::max<u32>(16, candidate.Height / 8);
    const bool hasSignificantTail =
        candidate.X0 >= horizontalTailThreshold ||
        candidate.Width - candidate.X1 >= horizontalTailThreshold ||
        candidate.Y0 >= verticalTailThreshold ||
        candidate.Height - candidate.Y1 >= verticalTailThreshold;
    if (!hasSignificantTail)
        return false;

    bounds = {};
    bounds.Valid = true;
    bounds.EdgeExtendMargins = true;
    bounds.X0 = static_cast<u16>(candidate.X0);
    bounds.Y0 = static_cast<u16>(candidate.Y0);
    bounds.X1 = static_cast<u16>(candidate.X1);
    bounds.Y1 = static_cast<u16>(candidate.Y1);
    return true;
}

static bool TextureBoundsRemapCoordinates(const TextureSamplingBounds& bounds)
{
    return bounds.Valid && !bounds.EdgeExtendMargins;
}

static u32 PackTextureCoords(s32 u, s32 v, const TextureSamplingBounds& bounds)
{
    if (TextureBoundsRemapCoordinates(bounds))
    {
        const s32 x0 = static_cast<s32>(bounds.X0) << 4;
        const s32 y0 = static_cast<s32>(bounds.Y0) << 4;
        const s32 x1 = static_cast<s32>(bounds.X1) << 4;
        const s32 y1 = static_cast<s32>(bounds.Y1) << 4;
        u = std::min<s32>(std::max<s32>(u, x0), x1) - x0;
        v = std::min<s32>(std::max<s32>(v, y0), y1) - y0;
    }

    return static_cast<u16>(u) | (static_cast<u32>(static_cast<u16>(v)) << 16);
}

static u32 PackTextureSizeRepeatAndForceNearest(u32 width, u32 height, u32 texRepeat, bool forceNearestTexture)
{
    return (width & 0x7FF) |
           ((texRepeat & 0xF) << 11) |
           (forceNearestTexture ? 0x8000 : 0) |
           (height << 16);
}

bool GLRenderer3D::BuildRenderShader(bool wbuffer)
{
    std::string shaderdefs;
    if (wbuffer)
        shaderdefs += "#define WBuffer\n";
    if (TextureFilter.Anisotropy > 1)
    {
        shaderdefs += "#define FILTERABLE_TEXTURE_CACHE\n";
        shaderdefs += "#define TEXTURE_ANISOTROPY ";
        shaderdefs += std::to_string(TextureFilter.Anisotropy);
        shaderdefs += "\n";
    }

    char shadername[32];
    snprintf(shadername, sizeof(shadername), "RenderShader%c", wbuffer?'W':'Z');

    std::string vsbuf = k3DRenderVS;
    if (wbuffer)
    {
        auto pos = vsbuf.find('\n') + 1;
        vsbuf = vsbuf.substr(0, pos) + "#define WBuffer\n" + vsbuf.substr(pos);
    }

    std::string fsbuf = k3DRenderFS;
    if (!shaderdefs.empty())
    {
        auto pos = fsbuf.find('\n') + 1;
        fsbuf = fsbuf.substr(0, pos) + shaderdefs + fsbuf.substr(pos);
    }

    GLuint prog;
    bool ret = OpenGL::CompileVertexFragmentProgram(prog,
        vsbuf, fsbuf,
        shadername,
        {{"vPosition", 0}, {"vColor", 1}, {"vTexcoord", 2}, {"vPolygonAttr", 3}, {"vTexcoordInsetBounds", 4}},
        {{"oColor", 0}, {"oAttr", 1}});

    if (!ret) return false;

    GLint uni_id = glGetUniformBlockIndex(prog, "uConfig");
    glUniformBlockBinding(prog, uni_id, 0);

    glUseProgram(prog);

    uni_id = glGetUniformLocation(prog, "CurTexture");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(prog, "Capture128Texture");
    glUniform1i(uni_id, 1);
    uni_id = glGetUniformLocation(prog, "Capture256Texture");
    glUniform1i(uni_id, 2);
    TextureNormalizeULoc[(int)wbuffer] = glGetUniformLocation(prog, "uTextureNormalize");
    BinaryAlphaTextureULoc[(int)wbuffer] = glGetUniformLocation(prog, "uBinaryAlphaTexture");
    if (ReadableTextureCache)
        glUniform4f(TextureNormalizeULoc[(int)wbuffer], 255.0f, 255.0f, 255.0f, 255.0f);
    else
        glUniform4f(TextureNormalizeULoc[(int)wbuffer], 63.0f, 63.0f, 63.0f, 31.0f);
    if (BinaryAlphaTextureULoc[(int)wbuffer] >= 0)
        glUniform1i(BinaryAlphaTextureULoc[(int)wbuffer], 0);

    RenderShader[(int)wbuffer] = prog;

    return true;
}

void GLRenderer3D::UseRenderShader(bool wbuffer)
{
    int flags = (int)wbuffer;
    if (CurShaderID == flags) return;
    glUseProgram(RenderShader[flags]);
    CurShaderID = flags;

    RenderModeULoc = glGetUniformLocation(RenderShader[flags], "uRenderMode");
}

void SetupDefaultTexParams(GLuint tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

GLRenderer3D::GLRenderer3D(melonDS::GPU3D& gpu3D, GLRenderer& parent) noexcept :
    Renderer3D(gpu3D), Parent(parent), Texcache(gpu3D.GPU, TexcacheOpenGLLoader(false))
{
    ClearBitmap[0] = new u32[256*256];
    ClearBitmap[1] = new u32[256*256];

    ScaleFactor = 0;
    BetterPolygons = false;
    TextureScaleFactor = 1;

    // GLRenderer3D::Init() will be used to actually initialize the renderer;
    // The various glDelete* functions silently ignore invalid IDs,
    // so we can just let the destructor clean up a half-initialized renderer.
}

bool GLRenderer3D::Init()
{
    GLint uni_id;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);

    glDepthRange(0, 1);
    glClearDepth(1.0);

    if (!OpenGL::CompileVertexFragmentProgram(ClearShaderPlain,
            k3DClearVS, k3DClearFS,
            "ClearShaderPlain",
            {{"vPosition", 0}},
            {{"oColor", 0}, {"oAttr", 1}}))
        return false;

    ClearUniformLoc[0] = glGetUniformLocation(ClearShaderPlain, "uColor");
    ClearUniformLoc[1] = glGetUniformLocation(ClearShaderPlain, "uDepth");
    ClearUniformLoc[2] = glGetUniformLocation(ClearShaderPlain, "uOpaquePolyID");
    ClearUniformLoc[3] = glGetUniformLocation(ClearShaderPlain, "uFogFlag");

    if (!OpenGL::CompileVertexFragmentProgram(ClearShaderBitmap,
              k3DClearBitmapVS, k3DClearBitmapFS,
              "ClearShaderBitmap",
              {{"vPosition", 0}},
              {{"oColor", 0}, {"oAttr", 1}}))
        return false;

    ClearBitmapULoc[0] = glGetUniformLocation(ClearShaderBitmap, "uClearBitmapOffset");
    ClearBitmapULoc[1] = glGetUniformLocation(ClearShaderBitmap, "uOpaquePolyID");

    glUseProgram(ClearShaderBitmap);
    uni_id = glGetUniformLocation(ClearShaderBitmap, "ClearBitmapColor");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(ClearShaderBitmap, "ClearBitmapDepth");
    glUniform1i(uni_id, 1);

    memset(RenderShader, 0, sizeof(RenderShader));

    if (!BuildRenderShader(false))
        return false;

    if (!BuildRenderShader(true))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(FinalPassEdgeShader,
            k3DFinalPassVS, k3DFinalPassEdgeFS,
            "FinalPassEdgeShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        return false;
    if (!OpenGL::CompileVertexFragmentProgram(FinalPassFogShader,
            k3DFinalPassVS, k3DFinalPassFogFS,
            "FinalPassFogShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        return false;

    uni_id = glGetUniformBlockIndex(FinalPassEdgeShader, "uConfig");
    glUniformBlockBinding(FinalPassEdgeShader, uni_id, 0);

    glUseProgram(FinalPassEdgeShader);
    uni_id = glGetUniformLocation(FinalPassEdgeShader, "DepthBuffer");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(FinalPassEdgeShader, "AttrBuffer");
    glUniform1i(uni_id, 1);

    uni_id = glGetUniformBlockIndex(FinalPassFogShader, "uConfig");
    glUniformBlockBinding(FinalPassFogShader, uni_id, 0);

    glUseProgram(FinalPassFogShader);
    uni_id = glGetUniformLocation(FinalPassFogShader, "DepthBuffer");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(FinalPassFogShader, "AttrBuffer");
    glUniform1i(uni_id, 1);


    memset(&ShaderConfig, 0, sizeof(ShaderConfig));

    glGenBuffers(1, &ShaderConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, ShaderConfigUBO);
    static_assert((sizeof(ShaderConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ShaderConfig), &ShaderConfig, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ShaderConfigUBO);


    float clearvtx[6*2] =
    {
        -1.0, -1.0,
        1.0, 1.0,
        -1.0, 1.0,

        -1.0, -1.0,
        1.0, -1.0,
        1.0, 1.0
    };

    glGenBuffers(1, &ClearVertexBufferID);
    glBindBuffer(GL_ARRAY_BUFFER, ClearVertexBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(clearvtx), clearvtx, GL_STATIC_DRAW);

    glGenVertexArrays(1, &ClearVertexArrayID);
    glBindVertexArray(ClearVertexArrayID);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)(0));

    // init textures for the clear bitmap
    glGenTextures(2, ClearBitmapTex);

    glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, 256, 256, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 256, 256, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);


    glGenBuffers(1, &VertexBufferID);
    glBindBuffer(GL_ARRAY_BUFFER, VertexBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexBuffer), nullptr, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    glEnableVertexAttribArray(0); // position
    glVertexAttribIPointer(0, 4, GL_UNSIGNED_SHORT, 9*4, (void*)(0));
    glEnableVertexAttribArray(1); // color
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, 9*4, (void*)(2*4));
    glEnableVertexAttribArray(2); // texcoords
    glVertexAttribIPointer(2, 2, GL_SHORT, 9*4, (void*)(3*4));
    glEnableVertexAttribArray(3); // attrib
    glVertexAttribIPointer(3, 3, GL_UNSIGNED_INT, 9*4, (void*)(4*4));
    glEnableVertexAttribArray(4); // sprite UV inset bounds
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, 9*4, (void*)(7*4));

    glGenBuffers(1, &IndexBufferID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IndexBufferID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(IndexBuffer), nullptr, GL_DYNAMIC_DRAW);

    glGenFramebuffers(1, &MainFramebuffer);
    glGenFramebuffers(1, &MainMSAAFramebuffer);

    // color buffers
    glGenTextures(1, &ColorBufferTex);
    SetupDefaultTexParams(ColorBufferTex);

    // depth/stencil buffer
    glGenTextures(1, &DepthBufferTex);
    SetupDefaultTexParams(DepthBufferTex);

    // attribute buffer
    // R: opaque polyID (for edgemarking)
    // G: edge flag
    // B: fog flag
    glGenTextures(1, &AttrBufferTex);
    SetupDefaultTexParams(AttrBufferTex);

    glGenTextures(1, &MSAAColorBufferTex);
    glGenTextures(1, &MSAADepthBufferTex);
    glGenTextures(1, &MSAAAttrBufferTex);

    Parent.OutputTex3D = ColorBufferTex;

    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

GLRenderer3D::~GLRenderer3D()
{
    assert(glDeleteTextures != nullptr);

    Texcache.Reset();

    glDeleteFramebuffers(1, &MainFramebuffer);
    glDeleteFramebuffers(1, &MainMSAAFramebuffer);
    glDeleteTextures(1, &ColorBufferTex);
    glDeleteTextures(1, &DepthBufferTex);
    glDeleteTextures(1, &AttrBufferTex);
    glDeleteTextures(1, &MSAAColorBufferTex);
    glDeleteTextures(1, &MSAADepthBufferTex);
    glDeleteTextures(1, &MSAAAttrBufferTex);

    glDeleteVertexArrays(1, &VertexArrayID);
    glDeleteBuffers(1, &VertexBufferID);
    glDeleteVertexArrays(1, &ClearVertexArrayID);
    glDeleteBuffers(1, &ClearVertexBufferID);
    glDeleteTextures(2, ClearBitmapTex);
    delete[] ClearBitmap[0];
    delete[] ClearBitmap[1];

    glDeleteBuffers(1, &ShaderConfigUBO);

    for (int i = 0; i < 2; i++)
    {
        if (!RenderShader[i]) continue;
        glDeleteProgram(RenderShader[i]);
    }
}

void GLRenderer3D::Reset()
{
    Texcache.Reset();
    ClearBitmapDirty = 0x3;
    LastRenderFrameSkipped = false;
}

void GLRenderer3D::SetBetterPolygons(bool betterpolygons) noexcept
{
    SetRenderSettings(ScaleFactor, betterpolygons, ReadableTextureCache, MSAA, TextureFilter, TextureScaling);
}

void GLRenderer3D::SetScaleFactor(int scale) noexcept
{
    SetRenderSettings(scale, BetterPolygons, ReadableTextureCache, MSAA, TextureFilter, TextureScaling);
}

void GLRenderer3D::SetReadableTextureCache(bool readableTextureCache) noexcept
{
    SetRenderSettings(ScaleFactor, BetterPolygons, readableTextureCache, MSAA, TextureFilter, TextureScaling);
}

void GLRenderer3D::SetRenderSettings(int scale, bool betterpolygons, bool readableTextureCache, bool msaa,
                                     const RendererSettings::TextureFilterSettings& textureFilter,
                                     const RendererSettings::TextureScalingSettings& textureScaling) noexcept
{
    const int textureScaleFactor = textureScaling.Enabled ? scale : 1;
    const bool scaleChanged = scale != ScaleFactor;
    bool textureCacheChanged = readableTextureCache != ReadableTextureCache;
    bool textureAnisotropyChanged = textureFilter.Anisotropy != TextureFilter.Anisotropy;
    bool textureScaleChanged = textureScaleFactor != TextureScaleFactor;
    bool textureFilterChanged = textureFilter != TextureFilter;
    bool textureScalingChanged = textureScaling != TextureScaling;
    bool msaaChanged = msaa != MSAA;

    if (!scaleChanged && betterpolygons == BetterPolygons && !textureScaleChanged && !textureCacheChanged &&
        !textureFilterChanged && !textureScalingChanged && !msaaChanged)
        return;

    // TODO set it for 2D renderer
    //CurGLCompositor.SetScaleFactor(scale);
    ScaleFactor = scale;
    BetterPolygons = betterpolygons;
    ReadableTextureCache = readableTextureCache;
    MSAA = msaa;
    TextureScaleFactor = textureScaleFactor;
    TextureFilter = textureFilter;
    TextureScaling = textureScaling;

    Texcache.ApplyTextureSettings(scale, textureFilter, textureScaling);
    if (Texcache.SetPreferredOutputFormat(ReadableTextureCache ? outputFmt_RGBA8 : outputFmt_RGB6A5))
        Texcache.Reset();

    if (textureAnisotropyChanged)
    {
        for (GLuint& shader : RenderShader)
        {
            if (shader != 0)
            {
                glDeleteProgram(shader);
                shader = 0;
            }
        }

        BuildRenderShader(false);
        BuildRenderShader(true);
    }

    for (int i = 0; i < 2; i++)
    {
        if (!RenderShader[i]) continue;
        glUseProgram(RenderShader[i]);
        if (ReadableTextureCache)
            glUniform4f(TextureNormalizeULoc[i], 255.0f, 255.0f, 255.0f, 255.0f);
        else
            glUniform4f(TextureNormalizeULoc[i], 63.0f, 63.0f, 63.0f, 31.0f);
    }
    CurShaderID = -1;

    ScreenW = 256 * scale;
    ScreenH = 192 * scale;

    glBindTexture(GL_TEXTURE_2D, ColorBufferTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindTexture(GL_TEXTURE_2D, DepthBufferTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, ScreenW, ScreenH, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glBindTexture(GL_TEXTURE_2D, AttrBufferTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ScreenW, ScreenH, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    GLenum fbassign[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    glBindFramebuffer(GL_FRAMEBUFFER, MainFramebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ColorBufferTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, DepthBufferTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, AttrBufferTex, 0);
    glDrawBuffers(2, fbassign);

    GLint maxSamples = 0;
    if (MSAA && glTexImage2DMultisample && glBlitFramebuffer)
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    MSAASamples = std::min(4, maxSamples);
    MSAAActive = MSAASamples > 1;

    if (MSAAActive)
    {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, MSAAColorBufferTex);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, MSAASamples, GL_RGBA8, ScreenW, ScreenH, GL_TRUE);

        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, MSAADepthBufferTex);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, MSAASamples, GL_DEPTH24_STENCIL8, ScreenW, ScreenH, GL_TRUE);

        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, MSAAAttrBufferTex);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, MSAASamples, GL_RGB8, ScreenW, ScreenH, GL_TRUE);

        glBindFramebuffer(GL_FRAMEBUFFER, MainMSAAFramebuffer);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, MSAAColorBufferTex, 0);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, MSAADepthBufferTex, 0);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, MSAAAttrBufferTex, 0);
        glDrawBuffers(2, fbassign);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //glLineWidth(scale);
    //glLineWidth(1.5);
}


void GLRenderer3D::SetupPolygon(GLRenderer3D::RendererPolygon* rp, Polygon* polygon) const
{
    rp->PolyData = polygon;

    // render key: depending on what we're drawing
    // opaque polygons:
    // - depthfunc
    // -- alpha=0
    // regular translucent polygons:
    // - depthfunc
    // -- depthwrite
    // --- polyID
    // ---- need opaque
    // shadow mask polygons:
    // - depthfunc?????
    // shadow polygons:
    // - depthfunc
    // -- depthwrite
    // --- polyID

    rp->RenderKey = (polygon->Attr >> 14) & 0x1; // bit14 - depth func
    if (!polygon->IsShadowMask)
    {
        if (polygon->Translucent)
        {
            if (polygon->IsShadow) rp->RenderKey |= 0x20000;
            else                   rp->RenderKey |= 0x10000;
            rp->RenderKey |= (polygon->Attr >> 10) & 0x2; // bit11 - depth write
            rp->RenderKey |= (polygon->Attr >> 13) & 0x4; // bit15 - fog
            rp->RenderKey |= (polygon->Attr & 0x3F000000) >> 16; // polygon ID
            if ((polygon->Attr & 0x001F0000) == 0x001F0000) // need opaque
                rp->RenderKey |= 0x4000;
        }
        else
        {
            if ((polygon->Attr & 0x001F0000) == 0)
                rp->RenderKey |= 0x2;
            rp->RenderKey |= (polygon->Attr & 0x3F000000) >> 16; // polygon ID
        }
    }
    else
    {
        rp->RenderKey |= 0x30000;
    }

    u32 textype = (polygon->TexParam >> 26) & 0x7;
    u32 texattr = (polygon->TexParam >> 16) & 0x3FF;
    if (TexEnable && (textype != 0))
        rp->RenderKey |= (0x80000 | (texattr << 20));
}

u32* GLRenderer3D::SetupVertex(const Polygon* poly, int vid, const Vertex* vtx, u32 vtxattr, u32 texlayer, u32 texwidth, u32 texheight,
                               bool forceNearestTexture, const TextureSamplingBounds& texBounds,
                               const TextureSpriteUVInsetBounds& spriteUVInsetBounds, u32* vptr) const
{
    u32 z = poly->FinalZ[vid];
    u32 w = poly->FinalW[vid];

    u32 alpha = (poly->Attr >> 16) & 0x1F;

    // Z should always fit within 16 bits, so it's okay to do this
    u32 zshift = 0;
    while (z > 0xFFFF) { z >>= 1; zshift++; }

    u32 x, y;
    if (ScaleFactor > 1)
    {
        x = (vtx->HiresPosition[0] * ScaleFactor) >> 4;
        y = (vtx->HiresPosition[1] * ScaleFactor) >> 4;
    }
    else
    {
        x = vtx->FinalPosition[0];
        y = vtx->FinalPosition[1];
    }

    // correct nearly-vertical edges that would look vertical on the DS
    /*{
        int vtopid = vid - 1;
        if (vtopid < 0) vtopid = poly->NumVertices-1;
        Vertex* vtop = poly->Vertices[vtopid];
        if (vtop->FinalPosition[1] >= vtx->FinalPosition[1])
        {
            vtopid = vid + 1;
            if (vtopid >= poly->NumVertices) vtopid = 0;
            vtop = poly->Vertices[vtopid];
        }
        if ((vtop->FinalPosition[1] < vtx->FinalPosition[1]) &&
            (vtx->FinalPosition[0] == vtop->FinalPosition[0]-1))
        {
            if (ScaleFactor > 1)
                x = (vtop->HiresPosition[0] * ScaleFactor) >> 4;
            else
                x = vtop->FinalPosition[0];
        }
    }*/

    *vptr++ = x | (y << 16);
    *vptr++ = z | (w << 16);

    *vptr++ =  (vtx->FinalColor[0] >> 1) |
              ((vtx->FinalColor[1] >> 1) << 8) |
              ((vtx->FinalColor[2] >> 1) << 16) |
              (alpha << 24);

    *vptr++ = PackTextureCoords(vtx->TexCoords[0], vtx->TexCoords[1], texBounds);

    *vptr++ = vtxattr | (zshift << 16);
    *vptr++ = texlayer;
    *vptr++ = PackTextureSizeRepeatAndForceNearest(texwidth, texheight, (poly->TexParam >> 16) & 0xF, forceNearestTexture);
    if (spriteUVInsetBounds.Valid)
    {
        *vptr++ = spriteUVInsetBounds.U0 | (static_cast<u32>(spriteUVInsetBounds.V0) << 16);
        *vptr++ = spriteUVInsetBounds.U1 | (static_cast<u32>(spriteUVInsetBounds.V1) << 16);
    }
    else
    {
        *vptr++ = 0;
        *vptr++ = 0;
    }

    return vptr;
}

void GLRenderer3D::BuildPolygons(GLRenderer3D::RendererPolygon* polygons, int npolys, int captureinfo[16])
{
    u32* vptr = &VertexBuffer[0];
    u32 vidx = 0;

    u32 iidx = 0;
    u32 eidx = EdgeIndicesOffset;

    u32 curtexparam = 0;
    u32 curtexpal = 0;
    GLuint curtexid = 0;
    u32 curtexlayer = (u32)-1;
    u32 curtexwidth = 0;
    u32 curtexheight = 0;
    TextureSamplingBounds curSamplingBounds;
    bool curBinaryAlphaTexture = false;
    std::vector<TextureFrameEdgeExtendCandidate> edgeExtendCandidates;
    TextureFrameEdgeExtendCandidateMap edgeExtendCandidateMap;
    if (TextureScaling.EdgeExtendUnusedMargins && TextureScaleFactor > 1)
    {
        auto edgeExtendPhaseStart = std::chrono::steady_clock::now();
        edgeExtendCandidates.reserve(npolys);
        edgeExtendCandidateMap.reserve(npolys);
        for (int i = 0; i < npolys; i++)
            AccumulateTextureFrameEdgeExtendCandidate(polygons[i].PolyData, TexEnable,
                                                      edgeExtendCandidates, edgeExtendCandidateMap);
        AddRenderFrameTiming(RenderFrameTiming.EdgeExtendAccumulate, ElapsedUS(edgeExtendPhaseStart));
    }

    for (int i = 0; i < npolys; i++)
    {
        RendererPolygon* rp = &polygons[i];
        Polygon* poly = rp->PolyData;

        rp->IndicesOffset = iidx;
        rp->NumIndices = 0;

        u32 vidx_first = vidx;

        u32 polyattr = poly->Attr;
        u32 texparam = poly->TexParam & ~0xC00F0000;
        u32 texpal = poly->TexPalette;
        TextureSamplingBounds samplingBounds;
        const u32 textypeForBounds = (texparam >> 26) & 0x7;
        if (TexEnable && textypeForBounds)
        {
            const u32 texWidth = TextureWidth(texparam);
            const u32 texHeight = TextureHeight(texparam);
            if (TextureFilter.MipmapSubrectHandling && TextureFilter.Anisotropy > 1 && TextureFilter.MipmapAlphaHandling)
                BuildSafeTextureSamplingBounds(poly, texWidth, texHeight, samplingBounds);
            if (!samplingBounds.Valid && TextureScaling.EdgeExtendUnusedMargins && TextureScaleFactor > 1)
            {
                auto edgeExtendPhaseStart = std::chrono::steady_clock::now();
                FindTextureFrameEdgeExtendBounds(edgeExtendCandidates, edgeExtendCandidateMap,
                                                 texparam, texpal, samplingBounds);
                AddRenderFrameTiming(RenderFrameTiming.EdgeExtendBoundsLookup, ElapsedUS(edgeExtendPhaseStart));
            }
        }

        u32 alpha = (polyattr >> 16) & 0x1F;

        u32 vtxattr = polyattr & 0x1F00C8F0;
        if (poly->FacingView) vtxattr |= (1<<8);
        if (poly->WBuffer)    vtxattr |= (1<<9);

        if ((texparam != curtexparam) || (texpal != curtexpal) || (samplingBounds != curSamplingBounds))
        {
            u32 textype = (texparam >> 26) & 0x7;
            if (TexEnable && (textype != 0))
            {
                // figure out which texture this polygon is going to use

                u32 texaddr = texparam & 0xFFFF;
                u32 texwidth = TextureWidth(texparam);
                u32 texheight = TextureHeight(texparam);
                int capblock = -1;
                if ((textype == 7) && ((texwidth == 128) || (texwidth == 256)))
                {
                    // if this is a direct color texture, and the width is 128 or 256
                    // then it might be a display capture
                    u32 startaddr = texaddr << 3;
                    u32 endaddr = startaddr + (texheight * texwidth * 2);

                    startaddr >>= 15;
                    endaddr = (endaddr + 0x7FFF) >> 15;

                    for (u32 b = startaddr; b < endaddr; b++)
                    {
                        int blk = captureinfo[b];
                        if (blk == -1) continue;

                        capblock = blk;
                    }
                }

                if (capblock != -1)
                {
                    if (texwidth == 128)
                    {
                        curtexid = -1;
                        curtexlayer = capblock | (((texaddr >> 5) & 0x7F) << 20);
                    }
                    else
                    {
                        curtexid = -2;
                        curtexlayer = (capblock >> 2) | (((texaddr >> 6) & 0xFF) << 20);
                    }
                    curtexwidth = texwidth;
                    curtexheight = texheight;
                    curBinaryAlphaTexture = false;
                }
                else
                {
                    u32* halp;
                    const bool edgeExtendTextureLookup =
                        samplingBounds.Valid && samplingBounds.EdgeExtendMargins;
                    auto edgeExtendPhaseStart = std::chrono::steady_clock::now();
                    bool textureCacheHit = false;
                    Texcache.GetTexture(texparam, texpal, curtexid, curtexlayer, halp, &curBinaryAlphaTexture,
                                        samplingBounds.Valid ? &samplingBounds : nullptr, &textureCacheHit);
                    if (edgeExtendTextureLookup)
                    {
                        const u64 elapsedUS = ElapsedUS(edgeExtendPhaseStart);
                        AddRenderFrameTiming(RenderFrameTiming.EdgeExtendTextureLookup, elapsedUS);
                        AddRenderFrameTiming(textureCacheHit
                                             ? RenderFrameTiming.EdgeExtendTextureLookupHit
                                             : RenderFrameTiming.EdgeExtendTextureLookupMiss,
                                             elapsedUS);
                    }
                    curtexlayer |= 0xFFFF0000;
                    if (TextureBoundsRemapCoordinates(samplingBounds))
                    {
                        curtexwidth = samplingBounds.X1 - samplingBounds.X0;
                        curtexheight = samplingBounds.Y1 - samplingBounds.Y0;
                    }
                    else
                    {
                        curtexwidth = texwidth;
                        curtexheight = texheight;
                    }
                }
            }
            else
            {
                // no texture
                curtexid = 0;
                curtexlayer = (u32)-1;
                curtexwidth = TextureWidth(texparam);
                curtexheight = TextureHeight(texparam);
                curBinaryAlphaTexture = false;
            }

            curtexparam = texparam;
            curtexpal = texpal;
            curSamplingBounds = samplingBounds;
        }

        rp->TexID = curtexid;
        rp->TexRepeat = (poly->TexParam >> 16) & 0xF;
        rp->BinaryAlphaTexture = curBinaryAlphaTexture;
        const TextureSamplingBounds vertexSamplingBounds =
            TextureBoundsRemapCoordinates(samplingBounds) ? samplingBounds : TextureSamplingBounds{};
        TextureSpriteUVInsetBounds spriteUVInsetBounds;
        const bool canClassifySpriteTexture =
            TexEnable && textypeForBounds &&
            !TextureBoundsRemapCoordinates(samplingBounds) &&
            curtexid != static_cast<GLuint>(-1) && curtexid != static_cast<GLuint>(-2);
        const bool spriteTexture =
            canClassifySpriteTexture &&
            BuildSpriteUVInsetBounds(poly, TextureWidth(texparam), TextureHeight(texparam), spriteUVInsetBounds);
        const bool translucentTextureGuard =
            TextureFilter.TranslucentTextureFilteringGuard &&
            TextureFilter.Anisotropy > 1 &&
            textypeForBounds == 6 &&
            IsLargeTranslucentTextureDraw(poly);
        const bool forceNearestTexture =
            canClassifySpriteTexture &&
            ((TextureFilter.Smart2DFiltering && spriteTexture) ||
             translucentTextureGuard);
        rp->DisableMSAA = canClassifySpriteTexture && translucentTextureGuard;
        if (!TextureFilter.SpriteUVInset)
        {
            spriteUVInsetBounds = {};
        }

        // assemble vertices
        if (poly->Type == 1) // line
        {
            rp->PrimType = GL_LINES;

            u32 lastx, lasty;
            int nout = 0;
            for (u32 j = 0; j < poly->NumVertices; j++)
            {
                Vertex* vtx = poly->Vertices[j];

                if (j > 0)
                {
                    if (lastx == vtx->FinalPosition[0] &&
                        lasty == vtx->FinalPosition[1]) continue;
                }

                lastx = vtx->FinalPosition[0];
                lasty = vtx->FinalPosition[1];

                vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, curtexwidth, curtexheight, forceNearestTexture,
                                   vertexSamplingBounds, spriteUVInsetBounds, vptr);

                IndexBuffer[iidx++] = vidx;
                rp->NumIndices++;

                vidx++;
                nout++;
                if (nout >= 2) break;
            }
        }
        else if (poly->NumVertices == 3) // regular triangle
        {
            rp->PrimType = GL_TRIANGLES;

            for (int j = 0; j < 3; j++)
            {
                Vertex* vtx = poly->Vertices[j];

                vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, curtexwidth, curtexheight, forceNearestTexture,
                                   vertexSamplingBounds, spriteUVInsetBounds, vptr);
                vidx++;
            }

            // build a triangle
            IndexBuffer[iidx++] = vidx_first;
            IndexBuffer[iidx++] = vidx - 2;
            IndexBuffer[iidx++] = vidx - 1;
            rp->NumIndices += 3;
        }
        else // quad, pentagon, etc
        {
            rp->PrimType = GL_TRIANGLES;

            if (!BetterPolygons)
            {
                // regular triangle-splitting

                for (u32 j = 0; j < poly->NumVertices; j++)
                {
                    Vertex* vtx = poly->Vertices[j];

                    vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, curtexwidth, curtexheight, forceNearestTexture,
                                       vertexSamplingBounds, spriteUVInsetBounds, vptr);

                    if (j >= 2)
                    {
                        // build a triangle
                        IndexBuffer[iidx++] = vidx_first;
                        IndexBuffer[iidx++] = vidx - 1;
                        IndexBuffer[iidx++] = vidx;
                        rp->NumIndices += 3;
                    }

                    vidx++;
                }
            }
            else
            {
                // attempt at 'better' splitting
                // this doesn't get rid of the error while splitting a bigger polygon into triangles
                // but we can attempt to reduce it

                u32 cX = 0, cY = 0;
                float cZ = 0;
                float cW = 0;

                float cR = 0, cG = 0, cB = 0;
                float cS = 0, cT = 0;

                for (u32 j = 0; j < poly->NumVertices; j++)
                {
                    Vertex* vtx = poly->Vertices[j];

                    cX += vtx->HiresPosition[0];
                    cY += vtx->HiresPosition[1];

                    float fw = (float)poly->FinalW[j] * poly->NumVertices;
                    cW += 1.0f / fw;

                    if (poly->WBuffer) cZ += poly->FinalZ[j] / fw;
                    else               cZ += poly->FinalZ[j];

                    cR += (vtx->FinalColor[0] >> 1) / fw;
                    cG += (vtx->FinalColor[1] >> 1) / fw;
                    cB += (vtx->FinalColor[2] >> 1) / fw;

                    cS += vtx->TexCoords[0] / fw;
                    cT += vtx->TexCoords[1] / fw;
                }

                cX /= poly->NumVertices;
                cY /= poly->NumVertices;

                cW = 1.0f / cW;

                if (poly->WBuffer) cZ *= cW;
                else               cZ /= poly->NumVertices;

                cR *= cW;
                cG *= cW;
                cB *= cW;

                cS *= cW;
                cT *= cW;

                cX = (cX * ScaleFactor) >> 4;
                cY = (cY * ScaleFactor) >> 4;

                u32 w = (u32)cW;

                u32 z = (u32)cZ;
                u32 zshift = 0;
                while (z > 0xFFFF) { z >>= 1; zshift++; }

                // build center vertex
                *vptr++ = cX | (cY << 16);
                *vptr++ = z | (w << 16);

                *vptr++ =  (u32)cR |
                          ((u32)cG << 8) |
                          ((u32)cB << 16) |
                          (alpha << 24);

                *vptr++ = PackTextureCoords(static_cast<s32>(cS), static_cast<s32>(cT), vertexSamplingBounds);

                *vptr++ = vtxattr | (zshift << 16);
                *vptr++ = curtexlayer;
                *vptr++ = PackTextureSizeRepeatAndForceNearest(curtexwidth, curtexheight, (poly->TexParam >> 16) & 0xF, forceNearestTexture);
                if (spriteUVInsetBounds.Valid)
                {
                    *vptr++ = spriteUVInsetBounds.U0 | (static_cast<u32>(spriteUVInsetBounds.V0) << 16);
                    *vptr++ = spriteUVInsetBounds.U1 | (static_cast<u32>(spriteUVInsetBounds.V1) << 16);
                }
                else
                {
                    *vptr++ = 0;
                    *vptr++ = 0;
                }

                vidx++;

                // build the final polygon
                for (u32 j = 0; j < poly->NumVertices; j++)
                {
                    Vertex* vtx = poly->Vertices[j];

                    vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, curtexwidth, curtexheight, forceNearestTexture,
                                       vertexSamplingBounds, spriteUVInsetBounds, vptr);

                    if (j >= 1)
                    {
                        // build a triangle
                        IndexBuffer[iidx++] = vidx_first;
                        IndexBuffer[iidx++] = vidx - 1;
                        IndexBuffer[iidx++] = vidx;
                        rp->NumIndices += 3;
                    }

                    vidx++;
                }

                IndexBuffer[iidx++] = vidx_first;
                IndexBuffer[iidx++] = vidx - 1;
                IndexBuffer[iidx++] = vidx_first + 1;
                rp->NumIndices += 3;
            }
        }

        rp->EdgeIndicesOffset = eidx;
        rp->NumEdgeIndices = 0;

        u32 vidx_cur = vidx_first;
        for (u32 j = 1; j < poly->NumVertices; j++)
        {
            IndexBuffer[eidx++] = vidx_cur;
            IndexBuffer[eidx++] = vidx_cur + 1;
            vidx_cur++;
            rp->NumEdgeIndices += 2;
        }
        IndexBuffer[eidx++] = vidx_cur;
        IndexBuffer[eidx++] = vidx_first;
        rp->NumEdgeIndices += 2;
    }

    NumVertices = vidx;
    NumIndices = iidx;
    NumEdgeIndices = eidx - EdgeIndicesOffset;
}

void GLRenderer3D::SetupPolygonTexture(const RendererPolygon* poly) const
{
    bool iscap = (poly->TexID == (GLuint)-1 || poly->TexID == (GLuint)-2);

    if (iscap)
    {
        if (poly->TexID == (GLuint)-1)
            glActiveTexture(GL_TEXTURE1);
        else
            glActiveTexture(GL_TEXTURE2);
    }
    else
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, poly->TexID);
    }

    GLint repeatS, repeatT;

    if (poly->TexRepeat & (1<<0))
        repeatS = (poly->TexRepeat & (1<<2)) ? GL_MIRRORED_REPEAT : GL_REPEAT;
    else
        repeatS = GL_CLAMP_TO_EDGE;

    if (poly->TexRepeat & (1<<1))
        repeatT = (poly->TexRepeat & (1<<3)) ? GL_MIRRORED_REPEAT : GL_REPEAT;
    else
        repeatT = GL_CLAMP_TO_EDGE;

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, repeatS);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, repeatT);

    if (TextureFilter.Anisotropy > 1 && BinaryAlphaTextureULoc[CurShaderID] >= 0)
        glUniform1i(BinaryAlphaTextureULoc[CurShaderID],
                    (TextureFilter.BinaryAlphaHandling && poly->BinaryAlphaTexture) ? 1 : 0);
}

int GLRenderer3D::RenderSinglePolygon(int i) const
{
    const RendererPolygon* rp = &PolygonList[i];

    SetupPolygonTexture(rp);
    if (MSAAActive && rp->DisableMSAA)
        glDisable(GL_MULTISAMPLE);
    glDrawElements(rp->PrimType, rp->NumIndices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));
    if (MSAAActive && rp->DisableMSAA)
        glEnable(GL_MULTISAMPLE);

    return 1;
}

int GLRenderer3D::RenderPolygonBatch(int i) const
{
    const RendererPolygon* rp = &PolygonList[i];
    GLuint primtype = rp->PrimType;
    u32 renderkey = rp->RenderKey;
    GLuint texid = rp->TexID;
    u32 texrepeat = rp->TexRepeat;
    int numpolys = 0;
    u32 numindices = 0;

    for (int iend = i; iend < NumFinalPolys; iend++)
    {
        const RendererPolygon* cur_rp = &PolygonList[iend];
        if (cur_rp->PrimType != primtype) break;
        if (cur_rp->RenderKey != renderkey) break;
        if (cur_rp->TexID != texid) break;
        if (cur_rp->TexRepeat != texrepeat) break;
        if (cur_rp->BinaryAlphaTexture != rp->BinaryAlphaTexture) break;
        if (cur_rp->DisableMSAA != rp->DisableMSAA) break;

        numpolys++;
        numindices += cur_rp->NumIndices;
    }

    SetupPolygonTexture(rp);
    if (MSAAActive && rp->DisableMSAA)
        glDisable(GL_MULTISAMPLE);
    glDrawElements(primtype, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));
    if (MSAAActive && rp->DisableMSAA)
        glEnable(GL_MULTISAMPLE);
    return numpolys;
}

int GLRenderer3D::RenderPolygonEdgeBatch(int i) const
{
    const RendererPolygon* rp = &PolygonList[i];
    u32 renderkey = rp->RenderKey;
    GLuint texid = rp->TexID;
    u32 texrepeat = rp->TexRepeat;
    int numpolys = 0;
    u32 numindices = 0;

    for (int iend = i; iend < NumFinalPolys; iend++)
    {
        const RendererPolygon* cur_rp = &PolygonList[iend];
        if (cur_rp->RenderKey != renderkey) break;
        if (cur_rp->TexID != texid) break;
        if (cur_rp->TexRepeat != texrepeat) break;
        if (cur_rp->BinaryAlphaTexture != rp->BinaryAlphaTexture) break;
        if (cur_rp->DisableMSAA != rp->DisableMSAA) break;

        numpolys++;
        numindices += cur_rp->NumEdgeIndices;
    }

    SetupPolygonTexture(rp);
    if (MSAAActive && rp->DisableMSAA)
        glDisable(GL_MULTISAMPLE);
    glDrawElements(GL_LINES, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->EdgeIndicesOffset * 2));
    if (MSAAActive && rp->DisableMSAA)
        glEnable(GL_MULTISAMPLE);
    return numpolys;
}

void GLRenderer3D::ResolveMSAAFramebuffer()
{
    if (!MSAAActive)
        return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, MainMSAAFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, MainFramebuffer);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, ScreenW, ScreenH, 0, 0, ScreenW, ScreenH, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glDrawBuffer(GL_COLOR_ATTACHMENT1);
    glBlitFramebuffer(0, 0, ScreenW, ScreenH, 0, 0, ScreenW, ScreenH, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBlitFramebuffer(0, 0, ScreenW, ScreenH, 0, 0, ScreenW, ScreenH,
                      GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);

    GLenum fbassign[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glBindFramebuffer(GL_FRAMEBUFFER, MainFramebuffer);
    glDrawBuffers(2, fbassign);
}

void GLRenderer3D::RenderSceneChunk(int y, int h)
{
    bool flags = GPU3D.RenderPolygonRAM[0]->WBuffer;
    UseRenderShader(flags);

    //if (h != 192) glScissor(0, y<<ScaleFactor, 256<<ScaleFactor, h<<ScaleFactor);

    GLboolean fogenable = (GPU3D.RenderDispCnt & (1<<7)) ? GL_TRUE : GL_FALSE;

    // TODO: proper 'equal' depth test!
    // (has margin of +-0x200 in Z-buffer mode, +-0xFF in W-buffer mode)
    // for now we're using GL_LEQUAL to make it work to some extent

    // STENCIL BUFFER VALUES
    // 1111'1111 : background (clear plane)
    // 1111'1110 : shadow mask against background
    // 00pp'pppp : opaque polygon ID p
    // 01pp'pppp : translucent polygon ID p
    // 1___'____ : shadow mask

    // POLYGON ID BASED RENDERING RULES
    // opaque polygons: polygon ID ignored
    // translucent polygons: if dst is opaque
    //                       OR if dst is translucent and polygon ID is different
    // shadow polygons: if (dst is opaque AND dst opaque polygon ID is different)
    //                  OR (dst is translucent and dst translucent polygon ID is different)

    // pass 1: opaque pixels

    glUniform1i(RenderModeULoc, RenderMode_Opaque);
    glLineWidth(1.0);

    glColorMaski(1, GL_TRUE, GL_TRUE, fogenable, GL_FALSE);

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    glBindVertexArray(VertexArrayID);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput128Tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput256Tex);

    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < NumFinalPolys; )
    {
        RendererPolygon* rp = &PolygonList[i];

        if (rp->PolyData->IsShadowMask) { i++; continue; }
        if (rp->PolyData->Translucent) { i++; continue; }

        if (rp->PolyData->Attr & (1<<14))
            glDepthFunc(GL_LEQUAL);
        else
            glDepthFunc(GL_LESS);

        u32 polyattr = rp->PolyData->Attr;
        u32 polyid = (polyattr >> 24) & 0x3F;

        glStencilFunc(GL_ALWAYS, polyid, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0xFF);

        i += RenderPolygonBatch(i);
    }

    // if edge marking is enabled, mark all opaque edges
    // TODO BETTER EDGE MARKING!!! THIS SUCKS
    /*if (RenderDispCnt & (1<<5))
    {
        UseRenderShader(flags | RenderFlag_Edge);
        glLineWidth(1.5);

        glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glColorMaski(1, GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);

        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);

        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0);

        for (int i = 0; i < NumFinalPolys; )
        {
            RendererPolygon* rp = &PolygonList[i];

            if (rp->PolyData->IsShadowMask) { i++; continue; }

            i += RenderPolygonEdgeBatch(i);
        }

        glDepthMask(GL_TRUE);
    }*/

    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    if (GPU3D.RenderDispCnt & (1<<3))
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    else
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ONE);

    glLineWidth(1.0);

    if (NumOpaqueFinalPolys > -1)
    {
        // pass 2: if needed, render translucent pixels that are against background pixels
        // when background alpha is zero, those need to be rendered with blending disabled

        if ((GPU3D.RenderClearAttr1 & 0x001F0000) == 0)
        {
            glDisable(GL_BLEND);

            for (int i = 0; i < NumFinalPolys; )
            {
                RendererPolygon* rp = &PolygonList[i];

                if (rp->PolyData->IsShadowMask)
                {
                    // draw actual shadow mask

                    glUniform1i(RenderModeULoc, RenderMode_ShadowMask);

                    glDisable(GL_BLEND);
                    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glDepthMask(GL_FALSE);

                    // render where stencil is 0xFF
                    // set to 0xFE where this polygon z-fails
                    glDepthFunc(GL_LESS);
                    glStencilFunc(GL_EQUAL, 0xFF, 0xFF);
                    glStencilOp(GL_KEEP, GL_INVERT, GL_KEEP);
                    glStencilMask(0x01);

                    i += RenderPolygonBatch(i);
                }
                else if (rp->PolyData->Translucent)
                {
                    bool needopaque = ((rp->PolyData->Attr & 0x001F0000) == 0x001F0000);

                    u32 polyattr = rp->PolyData->Attr;
                    u32 polyid = (polyattr >> 24) & 0x3F;

                    if (polyattr & (1<<14))
                        glDepthFunc(GL_LEQUAL);
                    else
                        glDepthFunc(GL_LESS);

                    if (needopaque)
                    {
                        glUniform1i(RenderModeULoc, RenderMode_Opaque);

                        glDisable(GL_BLEND);
                        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glColorMaski(1, GL_TRUE, GL_TRUE, fogenable, GL_FALSE);

                        // set stencil to the polygon's ID
                        glStencilFunc(GL_ALWAYS, polyid, 0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                        glStencilMask(0xFF);

                        glDepthMask(GL_TRUE);

                        RenderSinglePolygon(i);
                    }

                    glUniform1i(RenderModeULoc, RenderMode_Translucent);

                    GLboolean transfog;
                    if (!(polyattr & (1<<15))) transfog = fogenable;
                    else                       transfog = GL_FALSE;

                    if (rp->PolyData->IsShadow)
                    {
                        // shadow against clear-plane will only pass if its polyID matches that of the clear plane
                        u32 clrpolyid = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;
                        if (polyid != clrpolyid) { i++; continue; }

                        glEnable(GL_BLEND);
                        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                        // draw where shadow mask has previously been rendered (stencil=0xFE)
                        // when passing, set it to (polyID | 0x40)
                        // TODO might break bit0 of polyID
                        glStencilFunc(GL_EQUAL, 0xFE, 0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
                        glStencilMask(~(0x40|polyid)); // heheh

                        if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                        else                    glDepthMask(GL_FALSE);

                        i += needopaque ? RenderSinglePolygon(i) : RenderPolygonBatch(i);
                    }
                    else
                    {
                        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                        // draw on either background (0xFF) or shadowmask (0xFE)
                        // when passing, set it to (polyID | 0x40)
                        glStencilFunc(GL_EQUAL, 0xFF, 0xFE);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
                        glStencilMask(~(0x40|polyid)); // heheh

                        if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                        else                    glDepthMask(GL_FALSE);

                        i += needopaque ? RenderSinglePolygon(i) : RenderPolygonBatch(i);
                    }
                }
                else
                    i++;
            }

            glEnable(GL_BLEND);
            glStencilMask(0xFF);
        }

        // pass 3: translucent pixels

        for (int i = 0; i < NumFinalPolys; )
        {
            RendererPolygon* rp = &PolygonList[i];

            if (rp->PolyData->IsShadowMask)
            {
                // clear shadow bits in stencil buffer

                glStencilMask(0x80);
                glClear(GL_STENCIL_BUFFER_BIT);

                // draw actual shadow mask

                glUniform1i(RenderModeULoc, RenderMode_ShadowMask);

                glDisable(GL_BLEND);
                glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_FALSE);

                // set stencil bit7 where the shadowmask z-fails
                glDepthFunc(GL_LESS);
                glStencilFunc(GL_ALWAYS, 0x80, 0x80);
                glStencilOp(GL_KEEP, GL_REPLACE, GL_KEEP);

                i += RenderPolygonBatch(i);
            }
            else if (rp->PolyData->Translucent)
            {
                bool needopaque = ((rp->PolyData->Attr & 0x001F0000) == 0x001F0000);

                u32 polyattr = rp->PolyData->Attr;
                u32 polyid = (polyattr >> 24) & 0x3F;

                if (polyattr & (1<<14))
                    glDepthFunc(GL_LEQUAL);
                else
                    glDepthFunc(GL_LESS);

                if (needopaque)
                {
                    glUniform1i(RenderModeULoc, RenderMode_Opaque);

                    glDisable(GL_BLEND);
                    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glColorMaski(1, GL_TRUE, GL_TRUE, fogenable, GL_FALSE);

                    // set stencil to polyID
                    glStencilFunc(GL_ALWAYS, polyid, 0xFF);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    glStencilMask(0xFF);

                    glDepthMask(GL_TRUE);

                    RenderSinglePolygon(i);
                }

                glUniform1i(RenderModeULoc, RenderMode_Translucent);

                GLboolean transfog;
                if (!(polyattr & (1<<15))) transfog = fogenable;
                else                       transfog = GL_FALSE;

                if (rp->PolyData->IsShadow)
                {
                    glDisable(GL_BLEND);
                    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glDepthMask(GL_FALSE);

                    // render where polyID matches (ignoring other bits)
                    // clear bit7 where it passes
                    glStencilFunc(GL_EQUAL, polyid, 0x3F);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
                    glStencilMask(0x80);

                    RenderSinglePolygon(i);

                    glEnable(GL_BLEND);
                    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                    // render where bit7 is set (ie. shadow mask)
                    // set bit6 and replace polyID
                    glStencilFunc(GL_EQUAL, 0xC0|polyid, 0x80);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    glStencilMask(0x7F);

                    if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                    else                    glDepthMask(GL_FALSE);

                    i += RenderSinglePolygon(i);
                }
                else
                {
                    glEnable(GL_BLEND);
                    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                    // render where polyID and bit6 do not match
                    // set bit6 and set polyID
                    glStencilFunc(GL_NOTEQUAL, 0x40|polyid, 0x7F);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    glStencilMask(0x7F);

                    if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                    else                    glDepthMask(GL_FALSE);

                    i += needopaque ? RenderSinglePolygon(i) : RenderPolygonBatch(i);
                }
            }
            else
                i++;
        }
    }

    ResolveMSAAFramebuffer();

    if (GPU3D.RenderDispCnt & 0x00A0) // fog/edge enabled
    {
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);
        glStencilFunc(GL_ALWAYS, 0, 0);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, DepthBufferTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, AttrBufferTex);

        glBindBuffer(GL_ARRAY_BUFFER, ClearVertexBufferID);
        glBindVertexArray(ClearVertexArrayID);

        if (GPU3D.RenderDispCnt & (1<<5))
        {
            // edge marking
            // TODO: depth/polyid values at screen edges

            glUseProgram(FinalPassEdgeShader);

            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

            glDrawArrays(GL_TRIANGLES, 0, 2*3);
        }

        if (GPU3D.RenderDispCnt & (1<<7))
        {
            // fog

            glUseProgram(FinalPassFogShader);

            if (GPU3D.RenderDispCnt & (1<<6))
                glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            else
                glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA);

            {
                u32 c = GPU3D.RenderFogColor;
                u32 r = c & 0x1F;
                u32 g = (c >> 5) & 0x1F;
                u32 b = (c >> 10) & 0x1F;
                u32 a = (c >> 16) & 0x1F;

                glBlendColor((float)r/31.0, (float)g/31.0, (float)b/31.0, (float)a/31.0);
            }

            glDrawArrays(GL_TRIANGLES, 0, 2*3);
        }
    }
}


void GLRenderer3D::ResetRenderFrameTiming()
{
    RenderFrameTiming = {};
}

void GLRenderer3D::AddRenderFrameTiming(RenderFramePhaseTiming& phase, u64 elapsedUS)
{
    phase.TotalUS += elapsedUS;
    if (elapsedUS > phase.MaxUS)
        phase.MaxUS = elapsedUS;
    phase.Count++;
}

void GLRenderer3D::AppendRenderFrameTimingCSVHeader(std::string& header, const char* prefix) const
{
    auto addPhase = [&header, prefix](const char* name)
    {
        if (!header.empty())
            header += ",";
        header += prefix;
        header += "_";
        header += name;
        header += "_us";
        header += ",";
        header += prefix;
        header += "_";
        header += name;
        header += "_max_us";
        header += ",";
        header += prefix;
        header += "_";
        header += name;
        header += "_count";
    };
    auto addCounter = [&header, prefix](const char* name)
    {
        if (!header.empty())
            header += ",";
        header += prefix;
        header += "_";
        header += name;
    };

    addPhase("texcache_update");
    addPhase("capture_info");
    addPhase("clear_bitmap_upload");
    addPhase("shader_config");
    addPhase("clear_plane");
    addPhase("polygon_build");
    addPhase("edge_extend_accumulate");
    addPhase("edge_extend_bounds_lookup");
    addPhase("edge_extend_texture_lookup");
    addPhase("edge_extend_texture_lookup_hit");
    addPhase("edge_extend_texture_lookup_miss");
    addPhase("mipmap_flush");
    addCounter("edge_extend_new_variants");
    addCounter("edge_extend_throttled_variants");
    addCounter("edge_extend_suppressed_variants");
    addCounter("edge_extend_secondary_restore_hit");
    addCounter("edge_extend_secondary_restore_miss_no_entry");
    addCounter("edge_extend_secondary_restore_miss_mismatch");
    addCounter("edge_extend_secondary_restore_skipped");
    addCounter("edge_extend_secondary_store");
    addCounter("edge_extend_secondary_eviction");
    addPhase("texcache_pal_hash");
    addPhase("texcache_miss_total");
    addPhase("texcache_decode");
    addPhase("texcache_bounds_process");
    addPhase("texcache_texture_hash");
    addPhase("texcache_secondary_restore");
    addPhase("texcache_storage_alloc");
    addPhase("texcache_scale_source_decode");
    addPhase("texcache_scale_alpha_prep");
    addPhase("texcache_gpu_scale");
    addPhase("texcache_cpu_scale");
    addPhase("texcache_mip_prep");
    addPhase("texcache_binary_alpha_scan");
    addPhase("texcache_preview_capture");
    addPhase("texcache_upload");
    addPhase("texcache_cache_insert");
    addPhase("buffer_upload");
    addPhase("scene_render");
    addPhase("msaa_resolve_only");
}

void GLRenderer3D::AppendRenderFrameTimingCSVRow(std::string& row) const
{
    auto addPhase = [&row](const auto& phase)
    {
        if (!row.empty())
            row += ",";
        row += std::to_string(phase.TotalUS);
        row += ",";
        row += std::to_string(phase.MaxUS);
        row += ",";
        row += std::to_string(phase.Count);
    };
    auto addCounter = [&row](u32 value)
    {
        if (!row.empty())
            row += ",";
        row += std::to_string(value);
    };

    addPhase(RenderFrameTiming.TextureCacheUpdate);
    addPhase(RenderFrameTiming.CaptureInfo);
    addPhase(RenderFrameTiming.ClearBitmapUpload);
    addPhase(RenderFrameTiming.ShaderConfig);
    addPhase(RenderFrameTiming.ClearPlane);
    addPhase(RenderFrameTiming.PolygonBuild);
    addPhase(RenderFrameTiming.EdgeExtendAccumulate);
    addPhase(RenderFrameTiming.EdgeExtendBoundsLookup);
    addPhase(RenderFrameTiming.EdgeExtendTextureLookup);
    addPhase(RenderFrameTiming.EdgeExtendTextureLookupHit);
    addPhase(RenderFrameTiming.EdgeExtendTextureLookupMiss);
    addPhase(RenderFrameTiming.MipmapFlush);
    addCounter(Texcache.GetEdgeExtendNewVariantsThisFrame());
    addCounter(Texcache.GetEdgeExtendThrottledVariantsThisFrame());
    addCounter(Texcache.GetEdgeExtendSuppressedVariantsThisFrame());
    addCounter(Texcache.GetEdgeExtendSecondaryRestoreHitThisFrame());
    addCounter(Texcache.GetEdgeExtendSecondaryRestoreMissNoEntryThisFrame());
    addCounter(Texcache.GetEdgeExtendSecondaryRestoreMissMismatchThisFrame());
    addCounter(Texcache.GetEdgeExtendSecondaryRestoreSkippedThisFrame());
    addCounter(Texcache.GetEdgeExtendSecondaryStoreThisFrame());
    addCounter(Texcache.GetEdgeExtendSecondaryEvictionThisFrame());
    const auto& textureTiming = Texcache.GetTextureCacheFrameTiming();
    addPhase(textureTiming.PaletteHash);
    addPhase(textureTiming.MissTotal);
    addPhase(textureTiming.Decode);
    addPhase(textureTiming.BoundsProcess);
    addPhase(textureTiming.TextureHash);
    addPhase(textureTiming.SecondaryRestore);
    addPhase(textureTiming.StorageAlloc);
    addPhase(textureTiming.ScaleSourceDecode);
    addPhase(textureTiming.ScaleAlphaPrep);
    addPhase(textureTiming.GPUScale);
    addPhase(textureTiming.CPUScale);
    addPhase(textureTiming.MipPrep);
    addPhase(textureTiming.BinaryAlphaScan);
    addPhase(textureTiming.PreviewCapture);
    addPhase(textureTiming.Upload);
    addPhase(textureTiming.CacheInsert);
    addPhase(RenderFrameTiming.BufferUpload);
    addPhase(RenderFrameTiming.SceneRender);
    addPhase(RenderFrameTiming.MSAAResolveOnly);
}

void GLRenderer3D::RenderFrame()
{
    LastRenderFrameSkipped = false;

    u8 clrBitmapDirty;
    auto phaseStart = std::chrono::steady_clock::now();
    bool textureCacheChanged = Texcache.Update(clrBitmapDirty);
    AddRenderFrameTiming(RenderFrameTiming.TextureCacheUpdate, ElapsedUS(phaseStart));

    if (!textureCacheChanged && GPU3D.RenderFrameIdentical)
    {
        LastRenderFrameSkipped = true;
        return;
    }

    // figure out which chunks of texture memory contain display captures
    int captureinfo[16];
    phaseStart = std::chrono::steady_clock::now();
    GPU.GetCaptureInfo_Texture(captureinfo);
    AddRenderFrameTiming(RenderFrameTiming.CaptureInfo, ElapsedUS(phaseStart));

    // if we're using a clear bitmap, set that up
    phaseStart = std::chrono::steady_clock::now();
    ClearBitmapDirty |= clrBitmapDirty;
    if (GPU3D.RenderDispCnt & (1<<14))
    {
        if (ClearBitmapDirty & (1<<0))
        {
            u16* vram = (u16*)&GPU.VRAMFlat_Texture[0x40000];
            for (int i = 0; i < 256*256; i++)
            {
                u16 color = vram[i];
                u32 r = (color << 1) & 0x3E; if (r) r++;
                u32 g = (color >> 4) & 0x3E; if (g) g++;
                u32 b = (color >> 9) & 0x3E; if (b) b++;
                u32 a = (color & 0x8000) ? 31 : 0;

                ClearBitmap[0][i] = r | (g << 8) | (b << 16) | (a << 24);
            }

            glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[0]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, ClearBitmap[0]);
        }

        if (ClearBitmapDirty & (1<<1))
        {
            u16* vram = (u16*)&GPU.VRAMFlat_Texture[0x60000];
            for (int i = 0; i < 256*256; i++)
            {
                u16 val = vram[i];
                u32 depth = ((val & 0x7FFF) * 0x200) + 0x1FF;
                u32 fog = (val & 0x8000) << 9;

                ClearBitmap[1][i] = depth | fog;
            }

            glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[1]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED_INTEGER, GL_UNSIGNED_INT, ClearBitmap[1]);
        }

        ClearBitmapDirty = 0;
    }
    AddRenderFrameTiming(RenderFrameTiming.ClearBitmapUpload, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    TexEnable = !!(GPU3D.RenderDispCnt & (1<<0));

    CurShaderID = -1;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, MSAAActive ? MainMSAAFramebuffer : MainFramebuffer);

    ShaderConfig.uScreenSize[0] = ScreenW;
    ShaderConfig.uScreenSize[1] = ScreenH;
    ShaderConfig.uDispCnt = GPU3D.RenderDispCnt;

    for (int i = 0; i < 32; i++)
    {
        u16 c = GPU3D.RenderToonTable[i];
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;

        ShaderConfig.uToonColors[i][0] = (float)r / 31.0;
        ShaderConfig.uToonColors[i][1] = (float)g / 31.0;
        ShaderConfig.uToonColors[i][2] = (float)b / 31.0;
    }

    for (int i = 0; i < 8; i++)
    {
        u16 c = GPU3D.RenderEdgeTable[i];
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;

        ShaderConfig.uEdgeColors[i][0] = (float)r / 31.0;
        ShaderConfig.uEdgeColors[i][1] = (float)g / 31.0;
        ShaderConfig.uEdgeColors[i][2] = (float)b / 31.0;
    }

    {
        u32 c = GPU3D.RenderFogColor;
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;
        u32 a = (c >> 16) & 0x1F;

        ShaderConfig.uFogColor[0] = (float)r / 31.0;
        ShaderConfig.uFogColor[1] = (float)g / 31.0;
        ShaderConfig.uFogColor[2] = (float)b / 31.0;
        ShaderConfig.uFogColor[3] = (float)a / 31.0;
    }

    for (int i = 0; i < 34; i++)
    {
        u8 d = GPU3D.RenderFogDensityTable[i];
        ShaderConfig.uFogDensity[i][0] = (float)d / 127.0;
    }

    ShaderConfig.uFogOffset = GPU3D.RenderFogOffset;
    ShaderConfig.uFogShift = GPU3D.RenderFogShift;

    glBindBuffer(GL_UNIFORM_BUFFER, ShaderConfigUBO);
    void* unibuf = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
    if (unibuf) memcpy(unibuf, &ShaderConfig, sizeof(ShaderConfig));
    glUnmapBuffer(GL_UNIFORM_BUFFER);

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);

    glViewport(0, 0, ScreenW, ScreenH);
    AddRenderFrameTiming(RenderFrameTiming.ShaderConfig, ElapsedUS(phaseStart));

    phaseStart = std::chrono::steady_clock::now();
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);

    glDepthFunc(GL_ALWAYS);
    glStencilFunc(GL_ALWAYS, 0xFF, 0xFF);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

    // clear buffers
    // TODO: check whether 'clear polygon ID' affects translucent polyID
    // (for example when alpha is 1..30)
    if (GPU3D.RenderDispCnt & (1<<14))
    {
        // clear bitmap
        glUseProgram(ClearShaderBitmap);

        u32 polyid = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;

        float bitmapoffset[2];
        u8 xoff = (GPU3D.RenderClearAttr2 >> 16) & 0xFF;
        u8 yoff = (GPU3D.RenderClearAttr2 >> 24) & 0xFF;
        bitmapoffset[0] = (float)xoff / 256.0;
        bitmapoffset[1] = (float)yoff / 256.0;

        glUniform2f(ClearBitmapULoc[0], bitmapoffset[0], bitmapoffset[1]);
        glUniform1ui(ClearBitmapULoc[1], polyid);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[1]);
    }
    else
    {
        // plain clear plane
        glUseProgram(ClearShaderPlain);

        u32 r = GPU3D.RenderClearAttr1 & 0x1F;
        u32 g = (GPU3D.RenderClearAttr1 >> 5) & 0x1F;
        u32 b = (GPU3D.RenderClearAttr1 >> 10) & 0x1F;
        u32 fog = (GPU3D.RenderClearAttr1 >> 15) & 0x1;
        u32 a = (GPU3D.RenderClearAttr1 >> 16) & 0x1F;
        u32 polyid = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;
        u32 z = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;

        /*if (r) r = r*2 + 1;
        if (g) g = g*2 + 1;
        if (b) b = b*2 + 1;*/

        glUniform4ui(ClearUniformLoc[0], r, g, b, a);
        glUniform1ui(ClearUniformLoc[1], z);
        glUniform1ui(ClearUniformLoc[2], polyid);
        glUniform1ui(ClearUniformLoc[3], fog);
    }

    glBindBuffer(GL_ARRAY_BUFFER, ClearVertexBufferID);
    glBindVertexArray(ClearVertexArrayID);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);
    AddRenderFrameTiming(RenderFrameTiming.ClearPlane, ElapsedUS(phaseStart));

    if (GPU3D.RenderNumPolygons)
    {
        phaseStart = std::chrono::steady_clock::now();
        int npolys = 0;
        int firsttrans = -1;
        for (u32 i = 0; i < GPU3D.RenderNumPolygons; i++)
        {
            if (GPU3D.RenderPolygonRAM[i]->Degenerate) continue;

            SetupPolygon(&PolygonList[npolys], GPU3D.RenderPolygonRAM[i]);
            if (firsttrans < 0 && GPU3D.RenderPolygonRAM[i]->Translucent)
                firsttrans = npolys;

            npolys++;
        }
        NumFinalPolys = npolys;
        NumOpaqueFinalPolys = firsttrans;

        BuildPolygons(&PolygonList[0], npolys, captureinfo);
        AddRenderFrameTiming(RenderFrameTiming.PolygonBuild, ElapsedUS(phaseStart));

        phaseStart = std::chrono::steady_clock::now();
        Texcache.FlushPendingMipmaps();
        AddRenderFrameTiming(RenderFrameTiming.MipmapFlush, ElapsedUS(phaseStart));

        phaseStart = std::chrono::steady_clock::now();
        glBindBuffer(GL_ARRAY_BUFFER, VertexBufferID);
        glBufferSubData(GL_ARRAY_BUFFER, 0, NumVertices*9*4, VertexBuffer);

        // bind to access the index buffer
        glBindVertexArray(VertexArrayID);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, NumIndices * 2, IndexBuffer);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, EdgeIndicesOffset * 2, NumEdgeIndices * 2, IndexBuffer + EdgeIndicesOffset);
        AddRenderFrameTiming(RenderFrameTiming.BufferUpload, ElapsedUS(phaseStart));

        phaseStart = std::chrono::steady_clock::now();
        RenderSceneChunk(0, 192);
        AddRenderFrameTiming(RenderFrameTiming.SceneRender, ElapsedUS(phaseStart));
    }
    else
    {
        phaseStart = std::chrono::steady_clock::now();
        ResolveMSAAFramebuffer();
        AddRenderFrameTiming(RenderFrameTiming.MSAAResolveOnly, ElapsedUS(phaseStart));
    }

    Texcache.FinishDebugFrameTextureCapture();
}

bool GLRenderer3D::GetTextureScalingDebugStats(TextureScalingDebugStats& stats, std::string* status)
{
    Texcache.GetDebugStats(stats, false, ReadableTextureCache);
    if (status)
        *status = "OpenGL (Classic) 3D texture cache diagnostics.";
    return true;
}

bool GLRenderer3D::ResetTextureScalingDebugStats(std::string* status)
{
    Texcache.ResetDebugStats();
    if (status)
        *status = "Reset OpenGL (Classic) 3D texture cache diagnostics.";
    return true;
}

bool GLRenderer3D::GetTextureScalingDebugLastMiss(TextureScalingDebugLastMiss& miss, std::string* status)
{
    Texcache.GetDebugLastMiss(miss);
    if (status)
        *status = miss.Valid ?
            "Last OpenGL (Classic) 3D texture cache miss." :
            "No OpenGL (Classic) 3D texture cache miss has been captured yet.";
    return true;
}

bool GLRenderer3D::SetTextureScalingDebugCaptureEnabled(bool enabled, std::string* status)
{
    Texcache.SetDebugLastMissImageCaptureEnabled(enabled);
    if (status)
        *status = enabled ?
            "OpenGL (Classic) 3D texture miss capture armed." :
            "OpenGL (Classic) 3D texture miss capture disabled.";
    return true;
}

bool GLRenderer3D::GetTextureScalingDebugFrameTextures(TextureScalingDebugFrameTextures& frame, std::string* status)
{
    Texcache.GetDebugFrameTextures(frame);
    if (status)
    {
        if (frame.Valid)
            *status = "Captured OpenGL (Classic) frame texture list.";
        else if (frame.CapturePending || frame.CaptureActive)
            *status = "OpenGL (Classic) frame texture capture is waiting for the next rendered frame.";
        else
            *status = "No OpenGL (Classic) frame texture capture has been captured yet.";
    }
    return true;
}

bool GLRenderer3D::SetTextureScalingDebugFrameCaptureEnabled(bool enabled, std::string* status)
{
    Texcache.SetDebugFrameTextureCaptureEnabled(enabled);
    if (status)
        *status = enabled ?
            "OpenGL (Classic) frame texture capture armed." :
            "OpenGL (Classic) frame texture capture disabled.";
    return true;
}

u32* GLRenderer3D::GetLine(int line)
{
    return nullptr;
}

}
