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

#pragma once

#ifdef OGLRENDERER_ENABLED
#include "GPU3D.h"
#include "GPU3D_TextureTypes.h"
#include "OpenGLSupport.h"
#include "GPU3D_TexcacheOpenGL.h"
#include "NonStupidBitfield.h"
#include "RendererSettings.h"
#include "TextureScalingDebug.h"

#include <string>

namespace melonDS
{
class GLRenderer;

class GLRenderer3D : public Renderer3D
{
public:
    GLRenderer3D(melonDS::GPU3D& gpu3D, GLRenderer& parent) noexcept;
    ~GLRenderer3D() override;
    bool Init() override;
    void Reset() override;

    void SetRenderSettings(int scale, bool betterpolygons, bool readableTextureCache, bool msaa,
                           const RendererSettings::TextureFilterSettings& textureFilter,
                           const RendererSettings::TextureScalingSettings& textureScaling) noexcept;
    void SetBetterPolygons(bool betterpolygons) noexcept;
    void SetScaleFactor(int scale) noexcept;
    void SetReadableTextureCache(bool readableTextureCache) noexcept;
    [[nodiscard]] bool GetBetterPolygons() const noexcept { return BetterPolygons; }
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }

    void RenderFrame() override;
    u32* GetLine(int line) override;
    void ResetRenderFrameTiming();
    void AppendRenderFrameTimingCSVHeader(std::string& header, const char* prefix) const;
    void AppendRenderFrameTimingCSVRow(std::string& row) const;
    bool GetTextureScalingDebugStats(TextureScalingDebugStats& stats, std::string* status = nullptr);
    bool ResetTextureScalingDebugStats(std::string* status = nullptr);
    bool GetTextureScalingDebugLastMiss(TextureScalingDebugLastMiss& miss, std::string* status = nullptr);
    bool SetTextureScalingDebugCaptureEnabled(bool enabled, std::string* status = nullptr);
    bool GetTextureScalingDebugFrameTextures(TextureScalingDebugFrameTextures& frame, std::string* status = nullptr);
    bool SetTextureScalingDebugFrameCaptureEnabled(bool enabled, std::string* status = nullptr);

private:
    GLRenderer& Parent;

    // GL version requirements
    // * texelFetch: 3.0 (GLSL 1.30)     (3.2/1.50 for MS)
    // * UBO: 3.1

    struct RendererPolygon
    {
        Polygon* PolyData;

        u32 NumIndices;
        u32 IndicesOffset;
        GLuint PrimType;

        u32 NumEdgeIndices;
        u32 EdgeIndicesOffset;

        u32 RenderKey;

        GLuint TexID;
        u32 TexRepeat;
        bool BinaryAlphaTexture;
    };

    //GLCompositor CurGLCompositor;
    RendererPolygon PolygonList[2048] {};

    bool TexEnable;
    TexcacheOpenGL Texcache;

    struct RenderFramePhaseTiming
    {
        u64 TotalUS = 0;
        u64 MaxUS = 0;
        u32 Count = 0;
    };

    struct RenderFrameTimingState
    {
        RenderFramePhaseTiming TextureCacheUpdate;
        RenderFramePhaseTiming CaptureInfo;
        RenderFramePhaseTiming ClearBitmapUpload;
        RenderFramePhaseTiming ShaderConfig;
        RenderFramePhaseTiming ClearPlane;
        RenderFramePhaseTiming PolygonBuild;
        RenderFramePhaseTiming EdgeExtendAccumulate;
        RenderFramePhaseTiming EdgeExtendBoundsLookup;
        RenderFramePhaseTiming EdgeExtendTextureLookup;
        RenderFramePhaseTiming BufferUpload;
        RenderFramePhaseTiming SceneRender;
        RenderFramePhaseTiming MSAAResolveOnly;
    } RenderFrameTiming;

    void AddRenderFrameTiming(RenderFramePhaseTiming& phase, u64 elapsedUS);

    bool BuildRenderShader(bool wbuffer);
    void UseRenderShader(bool wbuffer);
    void SetupPolygon(RendererPolygon* rp, Polygon* polygon) const;
    u32* SetupVertex(const Polygon* poly, int vid, const Vertex* vtx, u32 vtxattr, u32 texlayer, u32 texwidth, u32 texheight,
                     const TextureSamplingBounds& texBounds, u32* vptr) const;
    void BuildPolygons(RendererPolygon* polygons, int npolys, int captureinfo[16]);
    void SetupPolygonTexture(const RendererPolygon* poly) const;
    int RenderSinglePolygon(int i) const;
    int RenderPolygonBatch(int i) const;
    int RenderPolygonEdgeBatch(int i) const;
    void RenderSceneChunk(int y, int h);
    void ResolveMSAAFramebuffer();


    enum
    {
        RenderMode_Opaque = 0,
        RenderMode_Translucent,
        RenderMode_ShadowMask,
    };


    GLuint ClearShaderPlain {};
    GLuint ClearShaderBitmap {};

    GLuint RenderShader[2] {};
    GLint RenderModeULoc = 0;
    GLuint CurShaderID = -1;
    GLint TextureNormalizeULoc[2] {};
    GLint BinaryAlphaTextureULoc[2] {};
    GLuint FinalPassEdgeShader {};
    GLuint FinalPassFogShader {};

    // std140 compliant structure
    struct
    {
        float uScreenSize[2];       // vec2       0 / 2
        u32 uDispCnt;               // int        2 / 1
        u32 __pad0;
        float uToonColors[32][4];   // vec4[32]   4 / 128
        float uEdgeColors[8][4];    // vec4[8]    132 / 32
        float uFogColor[4];         // vec4       164 / 4
        float uFogDensity[34][4];   // float[34]  168 / 136
        u32 uFogOffset;             // int        304 / 1
        u32 uFogShift;              // int        305 / 1
        u32 _pad1[2];               // int        306 / 2
    } ShaderConfig {};

    GLuint ShaderConfigUBO {};
    int NumFinalPolys {}, NumOpaqueFinalPolys {};

    GLuint ClearVertexBufferID = 0, ClearVertexArrayID {};
    GLint ClearUniformLoc[4] {};

    GLint ClearBitmapULoc[2] {};
    GLuint ClearBitmapTex[2];
    u32* ClearBitmap[2];
    u8 ClearBitmapDirty;

    // vertex buffer
    // * XYZW: 4x16bit
    // * RGBA: 4x8bit
    // * ST: 2x16bit
    // * polygon data: 3x32bit (polygon/texture attributes)
    //
    // polygon attributes:
    // * bit4-7, 11, 14-15, 24-29: POLYGON_ATTR
    // * bit16-20: Z shift
    // * bit8: front-facing (?)
    // * bit9: W-buffering (?)

    GLuint VertexBufferID {};
    u32 VertexBuffer[10240 * 7] {};
    u32 NumVertices {};

    GLuint VertexArrayID {};
    GLuint IndexBufferID {};
    u16 IndexBuffer[2048 * 40] {};
    u32 NumIndices {}, NumEdgeIndices {};

    const u32 EdgeIndicesOffset = 2048 * 30;

    int ScaleFactor {};
    bool BetterPolygons {};
    bool ReadableTextureCache = false;
    bool MSAA = false;
    bool MSAAActive = false;
    int MSAASamples = 1;
    int TextureScaleFactor = 1;
    RendererSettings::TextureFilterSettings TextureFilter {};
    RendererSettings::TextureScalingSettings TextureScaling {};
    int ScreenW {}, ScreenH {};

    GLuint ColorBufferTex {}, DepthBufferTex {}, AttrBufferTex {};

    GLuint MainFramebuffer {};
    GLuint MainMSAAFramebuffer {};
    GLuint MSAAColorBufferTex {}, MSAADepthBufferTex {}, MSAAAttrBufferTex {};
};
}
#endif
