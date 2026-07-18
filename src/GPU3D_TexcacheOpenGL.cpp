#include "GPU3D_TexcacheOpenGL.h"
#include "OpenGL_shaders/2DAlphaReplaceFS.h"
#include "OpenGL_shaders/2DMidpointAlphaFS.h"
#include "OpenGL_shaders/3DAlphaAwareMipFS.h"
#include "OpenGL_shaders/3DTexcacheRepackFS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv0CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv1CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv2CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv3CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv4CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv5CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_Conv6CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16_DepthToSpaceCS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv0CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv1CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv2CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv3CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv4CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv5CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_Conv6CS.h"
#include "OpenGL_shaders/2DArtCNN_C4F16DN_DepthToSpaceCS.h"
#include "OpenGL_shaders/2DCuNNy4x32_InCS.h"
#include "OpenGL_shaders/2DCuNNy4x32_Conv1CS.h"
#include "OpenGL_shaders/2DCuNNy4x32_Conv2CS.h"
#include "OpenGL_shaders/2DCuNNy4x32_Conv3CS.h"
#include "OpenGL_shaders/2DCuNNy4x32_Conv4CS.h"
#include "OpenGL_shaders/2DCuNNy4x32_OutShuffleCS.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

extern const char* k2DNNEDI3_VerticalCS;
extern const char* k2DNNEDI3_HorizontalCS;

namespace melonDS
{
extern const char* k2DFullscreenPassVS;
extern const char* k2DRGBAToYUVAFS;
extern const char* k2DSpline36FS;
extern const char* k2DArtCNN_YUVAToRGBA2xFS;
extern const char* k2DXBRZ_PreprocessFS;
extern const char* k2DXBRZ_FreescaleFS;

namespace
{
int MipmapLevelCount(u32 width, u32 height, u32 minDimension)
{
    int levels = 1;
    u32 levelWidth = width;
    u32 levelHeight = height;
    while (levelWidth > 1 || levelHeight > 1)
    {
        u32 nextWidth = std::max<u32>(1, levelWidth >> 1);
        u32 nextHeight = std::max<u32>(1, levelHeight >> 1);
        if (minDimension > 1 && (nextWidth < minDimension || nextHeight < minDimension))
            break;

        levelWidth = nextWidth;
        levelHeight = nextHeight;
        levels++;
    }
    return levels;
}

int NativeLimitedMipmapLevelCount(u32 width, u32 height, u32 scaleFactor, u32 minDimension)
{
    int levels = MipmapLevelCount(width, height, minDimension);
    int nativeScaleLevels = 1;
    while (scaleFactor > 1)
    {
        scaleFactor >>= 1;
        nativeScaleLevels++;
    }
    return std::min(levels, nativeScaleLevels);
}

bool IsPowerOfTwo(u32 value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

const char* kArtCNNModelLabels[RendererSettings::GLArtCNNModelCount] = {
    "C4F16DN",
    "C4F16",
};

const char* kArtCNNConvShaderSources[RendererSettings::GLArtCNNModelCount][7] = {
    {
        ::k2DArtCNN_C4F16DN_Conv0CS,
        ::k2DArtCNN_C4F16DN_Conv1CS,
        ::k2DArtCNN_C4F16DN_Conv2CS,
        ::k2DArtCNN_C4F16DN_Conv3CS,
        ::k2DArtCNN_C4F16DN_Conv4CS,
        ::k2DArtCNN_C4F16DN_Conv5CS,
        ::k2DArtCNN_C4F16DN_Conv6CS,
    },
    {
        ::k2DArtCNN_C4F16_Conv0CS,
        ::k2DArtCNN_C4F16_Conv1CS,
        ::k2DArtCNN_C4F16_Conv2CS,
        ::k2DArtCNN_C4F16_Conv3CS,
        ::k2DArtCNN_C4F16_Conv4CS,
        ::k2DArtCNN_C4F16_Conv5CS,
        ::k2DArtCNN_C4F16_Conv6CS,
    },
};

const char* kArtCNNDepthToSpaceSources[RendererSettings::GLArtCNNModelCount] = {
    ::k2DArtCNN_C4F16DN_DepthToSpaceCS,
    ::k2DArtCNN_C4F16_DepthToSpaceCS,
};

const char* kCuNNyModelLabels[RendererSettings::GLCuNNyModelCount] = {
    "4x32",
};

struct CuNNyModelInfo
{
    const char* InShader;
    const char* ConvShaders[RendererSettings::GLCuNNyMaxConvPasses];
    const char* OutShader;
    int ConvPasses;
    int WorkScaleX;
    int WorkScaleY;
    int FinalWorkScaleX;
    int FinalWorkScaleY;
    bool RGB;
};

const CuNNyModelInfo kCuNNyModels[RendererSettings::GLCuNNyModelCount] = {
    {
        k2DCuNNy4x32_InCS,
        {
            k2DCuNNy4x32_Conv1CS,
            k2DCuNNy4x32_Conv2CS,
            k2DCuNNy4x32_Conv3CS,
            k2DCuNNy4x32_Conv4CS,
        },
        k2DCuNNy4x32_OutShuffleCS,
        4,
        4, 2,
        4, 2,
        true,
    },
};

void SetUniform1iIfPresent(GLuint shader, const char* name, GLint value)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform1i(loc, value);
}

void SetUniform1fIfPresent(GLuint shader, const char* name, GLfloat value)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform1f(loc, value);
}

void SetUniform2iIfPresent(GLuint shader, const char* name, GLint x, GLint y)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform2i(loc, x, y);
}

void SetUniform2fIfPresent(GLuint shader, const char* name, GLfloat x, GLfloat y)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform2f(loc, x, y);
}

void SetUniform4fIfPresent(GLuint shader, const char* name, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    const GLint loc = glGetUniformLocation(shader, name);
    if (loc >= 0)
        glUniform4f(loc, x, y, z, w);
}

constexpr const char* kCuNNyWorkSamplerUniforms[] = {
    "in_raw",
    "conv1_raw",
    "conv2_raw",
    "conv3_raw",
    "conv4_raw",
    "conv5_raw",
    "conv6_raw",
    "conv7_raw",
    "conv8_raw",
};

constexpr const char* kCuNNyMulUniforms[] = {
    "LUMA_mul",
    "MAIN_mul",
    "in_mul",
    "conv1_mul",
    "conv2_mul",
    "conv3_mul",
    "conv4_mul",
    "conv5_mul",
    "conv6_mul",
    "conv7_mul",
    "conv8_mul",
};

constexpr const char* kCuNNyWorkPointUniforms[] = {
    "in_pt",
    "conv1_pt",
    "conv2_pt",
    "conv3_pt",
    "conv4_pt",
    "conv5_pt",
    "conv6_pt",
    "conv7_pt",
    "conv8_pt",
};

void SetCuNNyProgramDefaults(GLuint shader)
{
    glUseProgram(shader);
    SetUniform1iIfPresent(shader, "LUMA_raw", 1);
    SetUniform1iIfPresent(shader, "MAIN_raw", 1);
    for (const char* name : kCuNNyWorkSamplerUniforms)
        SetUniform1iIfPresent(shader, name, 0);
    for (const char* name : kCuNNyMulUniforms)
        SetUniform4fIfPresent(shader, name, 1.0f, 1.0f, 1.0f, 1.0f);
}

constexpr const char* kArtCNNSamplerUniforms[] = {
    "LUMA_raw",
    "conv2d_raw",
    "conv2d_1_raw",
    "conv2d_2_raw",
    "conv2d_3_raw",
    "conv2d_4_raw",
    "conv2d_5_raw",
    "conv2d_6_raw",
};
constexpr int kArtCNNSamplerUniformCount = sizeof(kArtCNNSamplerUniforms) / sizeof(kArtCNNSamplerUniforms[0]);

constexpr const char* kArtCNNMulUniforms[] = {
    "LUMA_mul",
    "conv2d_mul",
    "conv2d_1_mul",
    "conv2d_2_mul",
    "conv2d_3_mul",
    "conv2d_4_mul",
    "conv2d_5_mul",
    "conv2d_6_mul",
};

void SetArtCNNComputeProgramDefaults(GLuint shader)
{
    glUseProgram(shader);
    for (int i = 0; i < (int)kArtCNNSamplerUniformCount; i++)
        SetUniform1iIfPresent(shader, kArtCNNSamplerUniforms[i], i);
    for (const char* name : kArtCNNMulUniforms)
        SetUniform1fIfPresent(shader, name, 1.0f);
}

void SetArtCNNSizeUniform(GLuint shader, const char* sizeName, const char* pointName, GLfloat width, GLfloat height)
{
    SetUniform2fIfPresent(shader, sizeName, width, height);
    SetUniform2fIfPresent(shader, pointName, 1.0f / width, 1.0f / height);
}

void SetArtCNNComputeSizeUniforms(GLuint shader, int nativeWidth, int nativeHeight)
{
    const GLfloat width = (GLfloat)nativeWidth;
    const GLfloat height = (GLfloat)nativeHeight;
    const GLfloat width2x = width * 2.0f;
    const GLfloat height2x = height * 2.0f;

    SetArtCNNSizeUniform(shader, "LUMA_size", "LUMA_pt", width, height);
    SetArtCNNSizeUniform(shader, "conv2d_size", "conv2d_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_1_size", "conv2d_1_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_2_size", "conv2d_2_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_3_size", "conv2d_3_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_4_size", "conv2d_4_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_5_size", "conv2d_5_pt", width2x, height2x);
    SetArtCNNSizeUniform(shader, "conv2d_6_size", "conv2d_6_pt", width, height);
}

void BindTextureUnit(int unit, GLuint texture)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}
}

TexcacheOpenGLLoader::~TexcacheOpenGLLoader()
{
    DeleteArtCNNResources();
}

GLuint TexcacheOpenGLLoader::GenerateTexture(u32 width, u32 height, u32 layers, u32 scaleFactor)
{
    GLuint texarray;
    glGenTextures(1, &texarray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texarray);
    const u32 minMipDimension = FilterableMipMinDimension();
    const int mipLevels = FilterableSampling
        ? ((NativeMipFloor || (SourceMipScaling && IsPowerOfTwo(scaleFactor))) && scaleFactor > 1
            ? NativeLimitedMipmapLevelCount(width, height, scaleFactor, minMipDimension)
            : MipmapLevelCount(width, height, minMipDimension))
        : 1;
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                    FilterableSampling ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER,
                    FilterableSampling ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);

    const GLenum internalFormat = FilterableSampling ? GL_RGBA8 : GL_RGBA8UI;
    const GLenum uploadFormat = FilterableSampling ? GL_RGBA : GL_RGBA_INTEGER;

    if (IsCompute)
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, mipLevels, internalFormat, width, height, layers);
    else if (FilterableSampling)
    {
        u32 levelWidth = width;
        u32 levelHeight = height;
        for (int level = 0; level < mipLevels; level++)
        {
            glTexImage3D(GL_TEXTURE_2D_ARRAY, level, internalFormat, levelWidth, levelHeight, layers, 0,
                         uploadFormat, GL_UNSIGNED_BYTE, nullptr);
            levelWidth = std::max<u32>(1, levelWidth >> 1);
            levelHeight = std::max<u32>(1, levelHeight >> 1);
        }
    }
    else
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, width, height, layers, 0,
                     uploadFormat, GL_UNSIGNED_BYTE, nullptr);

    return texarray;
}

void TexcacheOpenGLLoader::UploadTexture(GLuint handle, u32 width, u32 height, u32 layer, void* data)
{
    UploadTextureLevel(handle, width, height, layer, 0, data);
    QueueMipmapGeneration(handle);
}

void TexcacheOpenGLLoader::UploadTextureLevel(GLuint handle, u32 width, u32 height, u32 layer, u32 level, void* data)
{
    glBindTexture(GL_TEXTURE_2D_ARRAY, handle);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
        level, 0, 0, layer,
        width, height, 1,
        FilterableSampling ? GL_RGBA : GL_RGBA_INTEGER,
        GL_UNSIGNED_BYTE, data);
}

void TexcacheOpenGLLoader::DeleteTexture(GLuint handle)
{
    PendingMipmapTextures.erase(
        std::remove(PendingMipmapTextures.begin(), PendingMipmapTextures.end(), handle),
        PendingMipmapTextures.end());
    glDeleteTextures(1, &handle);
}

void TexcacheOpenGLLoader::QueueMipmapGeneration(GLuint handle)
{
    if (!FilterableSampling || handle == 0)
        return;

    if (std::find(PendingMipmapTextures.begin(), PendingMipmapTextures.end(), handle) == PendingMipmapTextures.end())
        PendingMipmapTextures.push_back(handle);
}

void TexcacheOpenGLLoader::FlushPendingMipmaps()
{
    if (!FilterableSampling || PendingMipmapTextures.empty())
    {
        PendingMipmapTextures.clear();
        return;
    }

    GLint prevActiveTexture = 0;
    GLint prevTextureArray = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTextureArray);

    for (GLuint handle : PendingMipmapTextures)
    {
        if (!glIsTexture(handle))
            continue;

        glBindTexture(GL_TEXTURE_2D_ARRAY, handle);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)prevTextureArray);
    glActiveTexture((GLenum)prevActiveTexture);
    PendingMipmapTextures.clear();
}

int TexcacheOpenGLLoader::ArtCNNModelIndex() const
{
    return RendererSettings::GetGLArtCNNModelIndex(ScaleAlgorithm);
}

int TexcacheOpenGLLoader::CuNNyModelIndex() const
{
    return RendererSettings::GetGLCuNNyModelIndex(ScaleAlgorithm);
}

std::string TexcacheOpenGLLoader::BuildTexcacheRepackShaderSource(bool filterable) const
{
    if (!filterable)
        return k3DTexcacheRepackFS;

    std::string source = k3DTexcacheRepackFS;
    auto pos = source.find('\n');
    if (pos == std::string::npos)
        return "#define FILTERABLE_TEXTURE_CACHE\n" + source;

    pos += 1;
    source.insert(pos, "#define FILTERABLE_TEXTURE_CACHE\n");
    return source;
}

bool TexcacheOpenGLLoader::EnsureArtCNNPrograms()
{
    if (ArtCNNProgramsReady)
        return true;
    if (ArtCNNProgramsFailed)
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(RGBAToYUVAShader,
            k2DFullscreenPassVS, k2DRGBAToYUVAFS,
            "3DRGBAToYUVAShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(ArtCNNRepackShader,
            k2DFullscreenPassVS, BuildTexcacheRepackShaderSource(false),
            "3DArtCNNRepackShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(ArtCNNRepackFilterableShader,
            k2DFullscreenPassVS, BuildTexcacheRepackShaderSource(true),
            "3DArtCNNRepackFilterableShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(Spline36Shader,
            k2DFullscreenPassVS, k2DSpline36FS,
            "3DSpline36Shader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(ArtCNNYUVAToRGBA2xShader,
            k2DFullscreenPassVS, k2DArtCNN_YUVAToRGBA2xFS,
            "3DArtCNNYUVAToRGBA2xShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(AlphaReplaceShader,
            k2DFullscreenPassVS, k2DAlphaReplaceFS,
            "3DAlphaReplaceShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(MidpointAlphaShader,
            k2DFullscreenPassVS, k2DMidpointAlphaFS,
            "3DMidpointAlphaShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(AlphaAwareMipShader,
            k2DFullscreenPassVS, k3DAlphaAwareMipFS,
            "3DAlphaAwareMipShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(XBRZPreprocessShader,
            k2DFullscreenPassVS, k2DXBRZ_PreprocessFS,
            "3DXBRZPreprocessShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    if (!OpenGL::CompileVertexFragmentProgram(XBRZFreescaleShader,
            k2DFullscreenPassVS, k2DXBRZ_FreescaleFS,
            "3DXBRZFreescaleShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        goto fail;

    glUseProgram(RGBAToYUVAShader);
    glUniform1i(glGetUniformLocation(RGBAToYUVAShader, "Source"), 0);

    glUseProgram(ArtCNNRepackShader);
    glUniform1i(glGetUniformLocation(ArtCNNRepackShader, "Source"), 0);
    glUseProgram(ArtCNNRepackFilterableShader);
    glUniform1i(glGetUniformLocation(ArtCNNRepackFilterableShader, "Source"), 0);

    glUseProgram(Spline36Shader);
    glUniform1i(glGetUniformLocation(Spline36Shader, "Source"), 0);

    glUseProgram(ArtCNNYUVAToRGBA2xShader);
    glUniform1i(glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "Source"), 0);
    glUniform1i(glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "artcnn_luma"), 1);
    glUniform1i(glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "AlphaSource"), 2);
    glUniform1i(glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "uUseAlphaSource"), 0);

    glUseProgram(AlphaReplaceShader);
    glUniform1i(glGetUniformLocation(AlphaReplaceShader, "Source"), 0);
    glUniform1i(glGetUniformLocation(AlphaReplaceShader, "AlphaSource"), 1);

    glUseProgram(MidpointAlphaShader);
    glUniform1i(glGetUniformLocation(MidpointAlphaShader, "Source"), 0);
    glUniform1i(glGetUniformLocation(MidpointAlphaShader, "AlphaSource"), 1);

    glUseProgram(AlphaAwareMipShader);
    glUniform1i(glGetUniformLocation(AlphaAwareMipShader, "Source"), 0);

    glUseProgram(XBRZPreprocessShader);
    glUniform1i(glGetUniformLocation(XBRZPreprocessShader, "Source"), 0);

    glUseProgram(XBRZFreescaleShader);
    glUniform1i(glGetUniformLocation(XBRZFreescaleShader, "Source"), 0);
    glUniform1i(glGetUniformLocation(XBRZFreescaleShader, "InfoTex"), 1);

    ArtCNNProgramsReady = true;
    return true;

fail:
    ArtCNNProgramsFailed = true;
    return false;
}

bool TexcacheOpenGLLoader::EnsureArtCNNComputePrograms()
{
    if (ArtCNNComputeProgramsReady)
        return true;
    if (ArtCNNComputeProgramsFailed)
        return false;

    for (int model = 0; model < RendererSettings::GLArtCNNModelCount; model++)
    {
        for (int pass = 0; pass < 7; pass++)
        {
            std::string shaderName = "3DArtCNN_" + std::string(kArtCNNModelLabels[model]) +
                                     "_Conv" + std::to_string(pass) + "ComputeShader";
            if (!OpenGL::CompileComputeProgram(ArtCNNConvShaders[model][pass],
                    kArtCNNConvShaderSources[model][pass],
                    shaderName.c_str()))
                goto fail;
            SetArtCNNComputeProgramDefaults(ArtCNNConvShaders[model][pass]);
        }

        std::string depthShaderName = "3DArtCNN_" + std::string(kArtCNNModelLabels[model]) +
                                      "_DepthToSpaceComputeShader";
        if (!OpenGL::CompileComputeProgram(ArtCNNDepthToSpaceShaders[model],
                kArtCNNDepthToSpaceSources[model],
                depthShaderName.c_str()))
            goto fail;
        SetArtCNNComputeProgramDefaults(ArtCNNDepthToSpaceShaders[model]);
    }

    ArtCNNComputeProgramsReady = true;
    return true;

fail:
    ArtCNNComputeProgramsFailed = true;
    return false;
}

bool TexcacheOpenGLLoader::EnsureNNEDI3ComputePrograms()
{
    if (NNEDI3ComputeProgramsReady)
        return true;
    if (NNEDI3ComputeProgramsFailed)
        return false;

    if (!OpenGL::CompileComputeProgram(NNEDI3VerticalComputeShader,
            ::k2DNNEDI3_VerticalCS,
            "3DNNEDI3VerticalComputeShader"))
        goto fail;
    glUseProgram(NNEDI3VerticalComputeShader);
    SetUniform1iIfPresent(NNEDI3VerticalComputeShader, "Source", 0);

    if (!OpenGL::CompileComputeProgram(NNEDI3HorizontalComputeShader,
            ::k2DNNEDI3_HorizontalCS,
            "3DNNEDI3HorizontalComputeShader"))
        goto fail;
    glUseProgram(NNEDI3HorizontalComputeShader);
    SetUniform1iIfPresent(NNEDI3HorizontalComputeShader, "Source", 0);

    NNEDI3ComputeProgramsReady = true;
    return true;

fail:
    if (NNEDI3VerticalComputeShader != 0)
    {
        glDeleteProgram(NNEDI3VerticalComputeShader);
        NNEDI3VerticalComputeShader = 0;
    }
    if (NNEDI3HorizontalComputeShader != 0)
    {
        glDeleteProgram(NNEDI3HorizontalComputeShader);
        NNEDI3HorizontalComputeShader = 0;
    }
    NNEDI3ComputeProgramsFailed = true;
    return false;
}

bool TexcacheOpenGLLoader::EnsureCuNNyPrograms()
{
    if (CuNNyProgramsReady)
        return true;
    if (CuNNyProgramsFailed)
        return false;

    for (int model = 0; model < RendererSettings::GLCuNNyModelCount; model++)
    {
        const CuNNyModelInfo& modelInfo = kCuNNyModels[model];
        std::string shaderName = "3DCuNNy_" + std::string(kCuNNyModelLabels[model]) + "_InShader";
        if (!OpenGL::CompileComputeProgram(CuNNyInShaders[model], modelInfo.InShader, shaderName.c_str()))
            goto fail;
        SetCuNNyProgramDefaults(CuNNyInShaders[model]);

        for (int pass = 0; pass < modelInfo.ConvPasses; pass++)
        {
            shaderName = "3DCuNNy_" + std::string(kCuNNyModelLabels[model]) +
                         "_Conv" + std::to_string(pass + 1) + "Shader";
            if (!OpenGL::CompileComputeProgram(CuNNyConvShaders[model][pass],
                    modelInfo.ConvShaders[pass], shaderName.c_str()))
                goto fail;
            SetCuNNyProgramDefaults(CuNNyConvShaders[model][pass]);
        }

        shaderName = "3DCuNNy_" + std::string(kCuNNyModelLabels[model]) + "_OutShuffleShader";
        if (!OpenGL::CompileComputeProgram(CuNNyOutShaders[model], modelInfo.OutShader, shaderName.c_str()))
            goto fail;
        SetCuNNyProgramDefaults(CuNNyOutShaders[model]);
    }

    CuNNyProgramsReady = true;
    return true;

fail:
    CuNNyProgramsFailed = true;
    return false;
}

bool TexcacheOpenGLLoader::EnsureArtCNNBuffers()
{
    if (ArtCNNOutputFB == 0)
        glGenFramebuffers(1, &ArtCNNOutputFB);

    if (FullscreenPassVBO == 0)
    {
        const float vertices[12] = {
            0.0f, 1.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
        };

        glGenBuffers(1, &FullscreenPassVBO);
        glBindBuffer(GL_ARRAY_BUFFER, FullscreenPassVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }

    if (FullscreenPassVAO == 0)
    {
        glGenVertexArrays(1, &FullscreenPassVAO);
        glBindVertexArray(FullscreenPassVAO);
        glBindBuffer(GL_ARRAY_BUFFER, FullscreenPassVBO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    }

    return true;
}

static void DeleteTextureIfNeeded(GLuint& tex)
{
    if (tex != 0)
    {
        glDeleteTextures(1, &tex);
        tex = 0;
    }
}

static void DeleteProgramIfNeeded(GLuint& program)
{
    if (program != 0)
    {
        glDeleteProgram(program);
        program = 0;
    }
}

static GLuint CreateArtCNNTexture(GLenum internalFormat, int width, int height)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GLenum format = internalFormat == GL_RGBA8 ? GL_RGBA : GL_RGBA;
    GLenum type = internalFormat == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
    return tex;
}

static void ResizeArtCNNTexture(GLuint tex, GLenum internalFormat, int width, int height)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GLenum format = internalFormat == GL_RGBA8 ? GL_RGBA : GL_RGBA;
    GLenum type = internalFormat == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
}

static bool IsColorAttachmentEnum(GLint buffer)
{
    return buffer >= GL_COLOR_ATTACHMENT0 && buffer <= GL_COLOR_ATTACHMENT31;
}

bool TexcacheOpenGLLoader::EnsureArtCNNTextures(u32 width, u32 height, u32 scaleFactor)
{
    if (ArtCNNWidth == width && ArtCNNHeight == height && ArtCNNScaleFactor == scaleFactor &&
        ArtCNNSourceTex != 0)
        return true;

    DeleteTextureIfNeeded(ArtCNNSourceTex);
    DeleteTextureIfNeeded(ArtCNNYUVTex);
    DeleteTextureIfNeeded(ArtCNNConv0Tex);
    DeleteTextureIfNeeded(ArtCNNConvWorkTex[0]);
    DeleteTextureIfNeeded(ArtCNNConvWorkTex[1]);
    DeleteTextureIfNeeded(ArtCNNPackedTex);
    DeleteTextureIfNeeded(NNEDI3VerticalTex);
    DeleteTextureIfNeeded(NNEDI3Vertical4xTex);
    DeleteTextureIfNeeded(ArtCNNLuma2xTex);
    DeleteTextureIfNeeded(NNEDI3Luma4xTex);
    DeleteTextureIfNeeded(ArtCNNYUVA2xTex);
    DeleteTextureIfNeeded(ArtCNNRGBA2xTex);
    DeleteTextureIfNeeded(ArtCNNAlphaXBRZOutputTex);
    DeleteTextureIfNeeded(ArtCNNOutputWorkTex);
    DeleteTextureIfNeeded(ArtCNNOutputTex);
    DeleteTextureIfNeeded(XBRZInfoTex);

    u32 width2x = width * 2;
    u32 height2x = height * 2;
    u32 width4x = width * 4;
    u32 height4x = height * 4;
    u32 outputWidth = width * scaleFactor;
    u32 outputHeight = height * scaleFactor;

    ArtCNNSourceTex = CreateArtCNNTexture(GL_RGBA8, width, height);
    ArtCNNYUVTex = CreateArtCNNTexture(GL_RGBA16F, width, height);
    ArtCNNConv0Tex = CreateArtCNNTexture(GL_RGBA16F, width2x, height2x);
    ArtCNNConvWorkTex[0] = CreateArtCNNTexture(GL_RGBA16F, width2x, height2x);
    ArtCNNConvWorkTex[1] = CreateArtCNNTexture(GL_RGBA16F, width2x, height2x);
    ArtCNNPackedTex = CreateArtCNNTexture(GL_RGBA16F, width, height);
    NNEDI3VerticalTex = CreateArtCNNTexture(GL_RGBA16F, width, height2x);
    NNEDI3Vertical4xTex = CreateArtCNNTexture(GL_RGBA16F, width2x, height4x);
    ArtCNNLuma2xTex = CreateArtCNNTexture(GL_RGBA16F, width2x, height2x);
    NNEDI3Luma4xTex = CreateArtCNNTexture(GL_RGBA16F, width4x, height4x);
    ArtCNNYUVA2xTex = CreateArtCNNTexture(GL_RGBA16F, width2x, height2x);
    ArtCNNRGBA2xTex = CreateArtCNNTexture(GL_RGBA8, width2x, height2x);
    ArtCNNAlphaXBRZOutputTex = CreateArtCNNTexture(GL_RGBA8, outputWidth, outputHeight);
    ArtCNNOutputWorkTex = CreateArtCNNTexture(GL_RGBA8, outputWidth, outputHeight);
    ArtCNNOutputTex = CreateArtCNNTexture(GL_RGBA8, outputWidth, outputHeight);
    XBRZInfoTex = CreateArtCNNTexture(GL_RGBA8, width, height);

    ArtCNNWidth = width;
    ArtCNNHeight = height;
    ArtCNNScaleFactor = scaleFactor;
    return true;
}

bool TexcacheOpenGLLoader::EnsureCuNNyWorkTexture(int index, u32 width, u32 height)
{
    if (index < 0 || index >= 2)
        return false;

    if (CuNNyWorkTex[index] == 0)
    {
        CuNNyWorkTex[index] = CreateArtCNNTexture(GL_RGBA16F, width, height);
        CuNNyWorkTexWidth[index] = width;
        CuNNyWorkTexHeight[index] = height;
        return CuNNyWorkTex[index] != 0;
    }

    if (CuNNyWorkTexWidth[index] != width || CuNNyWorkTexHeight[index] != height)
    {
        ResizeArtCNNTexture(CuNNyWorkTex[index], GL_RGBA16F, width, height);
        CuNNyWorkTexWidth[index] = width;
        CuNNyWorkTexHeight[index] = height;
    }

    return true;
}

void TexcacheOpenGLLoader::DeleteArtCNNResources()
{
    DeleteTextureIfNeeded(ArtCNNSourceTex);
    DeleteTextureIfNeeded(ArtCNNYUVTex);
    DeleteTextureIfNeeded(ArtCNNConv0Tex);
    DeleteTextureIfNeeded(ArtCNNConvWorkTex[0]);
    DeleteTextureIfNeeded(ArtCNNConvWorkTex[1]);
    DeleteTextureIfNeeded(ArtCNNPackedTex);
    DeleteTextureIfNeeded(NNEDI3VerticalTex);
    DeleteTextureIfNeeded(NNEDI3Vertical4xTex);
    DeleteTextureIfNeeded(ArtCNNLuma2xTex);
    DeleteTextureIfNeeded(NNEDI3Luma4xTex);
    DeleteTextureIfNeeded(ArtCNNYUVA2xTex);
    DeleteTextureIfNeeded(ArtCNNRGBA2xTex);
    DeleteTextureIfNeeded(ArtCNNAlphaXBRZOutputTex);
    DeleteTextureIfNeeded(ArtCNNOutputWorkTex);
    DeleteTextureIfNeeded(ArtCNNOutputTex);
    DeleteTextureIfNeeded(XBRZInfoTex);
    DeleteTextureIfNeeded(CuNNyWorkTex[0]);
    DeleteTextureIfNeeded(CuNNyWorkTex[1]);
    CuNNyWorkTexWidth[0] = 0;
    CuNNyWorkTexWidth[1] = 0;
    CuNNyWorkTexHeight[0] = 0;
    CuNNyWorkTexHeight[1] = 0;

    if (ArtCNNOutputFB != 0)
    {
        glDeleteFramebuffers(1, &ArtCNNOutputFB);
        ArtCNNOutputFB = 0;
    }
    if (FullscreenPassVAO != 0)
    {
        glDeleteVertexArrays(1, &FullscreenPassVAO);
        FullscreenPassVAO = 0;
    }
    if (FullscreenPassVBO != 0)
    {
        glDeleteBuffers(1, &FullscreenPassVBO);
        FullscreenPassVBO = 0;
    }

    std::array<GLuint*, 4 + RendererSettings::GLArtCNNModelCount * 8> shaders = {
        &RGBAToYUVAShader,
        &ArtCNNConvShaders[0][0],
        &ArtCNNConvShaders[0][1],
        &ArtCNNConvShaders[0][2],
        &ArtCNNConvShaders[0][3],
        &ArtCNNConvShaders[0][4],
        &ArtCNNConvShaders[0][5],
        &ArtCNNConvShaders[0][6],
        &ArtCNNDepthToSpaceShaders[0],
        &ArtCNNConvShaders[1][0],
        &ArtCNNConvShaders[1][1],
        &ArtCNNConvShaders[1][2],
        &ArtCNNConvShaders[1][3],
        &ArtCNNConvShaders[1][4],
        &ArtCNNConvShaders[1][5],
        &ArtCNNConvShaders[1][6],
        &ArtCNNDepthToSpaceShaders[1],
        &Spline36Shader,
        &XBRZPreprocessShader,
        &XBRZFreescaleShader,
    };
    for (GLuint* shader : shaders)
    {
        if (*shader != 0)
        {
            glDeleteProgram(*shader);
            *shader = 0;
        }
    }
    if (ArtCNNYUVAToRGBA2xShader != 0)
    {
        glDeleteProgram(ArtCNNYUVAToRGBA2xShader);
        ArtCNNYUVAToRGBA2xShader = 0;
    }
    if (AlphaReplaceShader != 0)
    {
        glDeleteProgram(AlphaReplaceShader);
        AlphaReplaceShader = 0;
    }
    if (MidpointAlphaShader != 0)
    {
        glDeleteProgram(MidpointAlphaShader);
        MidpointAlphaShader = 0;
    }
    if (AlphaAwareMipShader != 0)
    {
        glDeleteProgram(AlphaAwareMipShader);
        AlphaAwareMipShader = 0;
    }
    if (ArtCNNRepackShader != 0)
    {
        glDeleteProgram(ArtCNNRepackShader);
        ArtCNNRepackShader = 0;
    }
    if (ArtCNNRepackFilterableShader != 0)
    {
        glDeleteProgram(ArtCNNRepackFilterableShader);
        ArtCNNRepackFilterableShader = 0;
    }

    for (int model = 0; model < RendererSettings::GLCuNNyModelCount; model++)
    {
        DeleteProgramIfNeeded(CuNNyInShaders[model]);
        for (int pass = 0; pass < RendererSettings::GLCuNNyMaxConvPasses; pass++)
            DeleteProgramIfNeeded(CuNNyConvShaders[model][pass]);
        DeleteProgramIfNeeded(CuNNyOutShaders[model]);
    }
    DeleteProgramIfNeeded(NNEDI3VerticalComputeShader);
    DeleteProgramIfNeeded(NNEDI3HorizontalComputeShader);

    ArtCNNProgramsReady = false;
    ArtCNNProgramsFailed = false;
    ArtCNNComputeProgramsReady = false;
    ArtCNNComputeProgramsFailed = false;
    NNEDI3ComputeProgramsReady = false;
    NNEDI3ComputeProgramsFailed = false;
    CuNNyProgramsReady = false;
    CuNNyProgramsFailed = false;
    ArtCNNWidth = 0;
    ArtCNNHeight = 0;
    ArtCNNScaleFactor = 0;
}

void TexcacheOpenGLLoader::RenderFullscreenPass(GLuint shader, GLuint outputTex, int width, int height, GLuint source0, GLuint source1,
                                            GLuint source2)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, outputTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    GLint outputSizeLoc = glGetUniformLocation(shader, "uOutputSize");
    if (outputSizeLoc >= 0)
        glUniform2f(outputSizeLoc, (float)width, (float)height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, source1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, source2);

    glBindBuffer(GL_ARRAY_BUFFER, FullscreenPassVBO);
    glBindVertexArray(FullscreenPassVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void TexcacheOpenGLLoader::RenderSpline36(GLuint sourceTex, GLuint targetTex, int width, int height, float sourceShiftX, float sourceShiftY)
{
    glUseProgram(Spline36Shader);
    GLint sourceShiftLoc = glGetUniformLocation(Spline36Shader, "uSourceShift");
    if (sourceShiftLoc >= 0)
        glUniform2f(sourceShiftLoc, sourceShiftX, sourceShiftY);

    RenderFullscreenPass(Spline36Shader, targetTex, width, height, sourceTex, 0);
}

void TexcacheOpenGLLoader::RenderXBRZ(GLuint sourceTex, GLuint targetTex, int width, int height, u32 scaleFactor)
{
    RenderFullscreenPass(XBRZPreprocessShader, XBRZInfoTex, width, height, sourceTex, 0);
    RenderFullscreenPass(XBRZFreescaleShader, targetTex, width * (int)scaleFactor, height * (int)scaleFactor, sourceTex, XBRZInfoTex);
}

void TexcacheOpenGLLoader::RenderMidpointAlphaReplace(GLuint colorTex, GLuint alphaTex, GLuint targetTex, int width, int height)
{
    RenderFullscreenPass(MidpointAlphaShader, targetTex, width, height, colorTex, alphaTex);
}

void TexcacheOpenGLLoader::RenderAlphaReplace(GLuint colorTex, GLuint alphaTex, GLuint targetTex, int width, int height)
{
    RenderFullscreenPass(AlphaReplaceShader, targetTex, width, height, colorTex, alphaTex);
}

void TexcacheOpenGLLoader::RenderArtCNNComputePass(GLuint shader, GLuint targetTex, int pass,
                                                     int nativeWidth, int nativeHeight)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    SetArtCNNComputeSizeUniforms(shader, nativeWidth, nativeHeight);

    for (int unit = 0; unit < (int)kArtCNNSamplerUniformCount; unit++)
        BindTextureUnit(unit, 0);

    switch (pass)
    {
    case 0:
        BindTextureUnit(0, ArtCNNYUVTex);
        break;
    case 1:
        BindTextureUnit(1, ArtCNNConv0Tex);
        break;
    case 2:
        BindTextureUnit(2, ArtCNNConvWorkTex[0]);
        break;
    case 3:
        BindTextureUnit(3, ArtCNNConvWorkTex[1]);
        break;
    case 4:
        BindTextureUnit(4, ArtCNNConvWorkTex[0]);
        break;
    case 5:
        BindTextureUnit(5, ArtCNNConvWorkTex[1]);
        break;
    case 6:
        BindTextureUnit(1, ArtCNNConv0Tex);
        BindTextureUnit(6, ArtCNNConvWorkTex[0]);
        break;
    default:
        BindTextureUnit(7, ArtCNNPackedTex);
        break;
    }

    glBindImageTexture(0, targetTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    const int dispatchWidth = (pass == 7) ? nativeWidth * 2 : nativeWidth;
    const int dispatchHeight = (pass == 7) ? nativeHeight * 2 : nativeHeight;
    glDispatchCompute((GLuint)((dispatchWidth + 7) / 8), (GLuint)((dispatchHeight + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_FRAMEBUFFER_BARRIER_BIT);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glActiveTexture(GL_TEXTURE0);
}

void TexcacheOpenGLLoader::RenderNNEDI3ComputePass(GLuint shader, GLuint sourceTex, GLuint targetTex,
                                                   int sourceWidth, int sourceHeight)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    SetUniform2iIfPresent(shader, "uSrcSize", sourceWidth, sourceHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);

    glBindImageTexture(0, targetTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((GLuint)((sourceWidth + 7) / 8), (GLuint)((sourceHeight + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_FRAMEBUFFER_BARRIER_BIT);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glActiveTexture(GL_TEXTURE0);
}

void TexcacheOpenGLLoader::RenderCuNNyComputePass(GLuint shader, GLuint sourceTex, GLuint baseTex, GLuint targetTex,
                                                  int sourceWidth, int sourceHeight,
                                                  int nativeWidth, int nativeHeight)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(shader);
    SetUniform2fIfPresent(shader, "LUMA_size", (GLfloat)nativeWidth, (GLfloat)nativeHeight);
    SetUniform2fIfPresent(shader, "MAIN_size", (GLfloat)nativeWidth, (GLfloat)nativeHeight);
    SetUniform2fIfPresent(shader, "LUMA_pt", 1.0f / (GLfloat)nativeWidth, 1.0f / (GLfloat)nativeHeight);
    SetUniform2fIfPresent(shader, "MAIN_pt", 1.0f / (GLfloat)nativeWidth, 1.0f / (GLfloat)nativeHeight);
    for (const char* name : kCuNNyWorkPointUniforms)
        SetUniform2fIfPresent(shader, name, 1.0f / (GLfloat)sourceWidth, 1.0f / (GLfloat)sourceHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseTex);
    GLint baseMinFilter = GL_NEAREST;
    GLint baseMagFilter = GL_NEAREST;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &baseMinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &baseMagFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindImageTexture(0, targetTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute((GLuint)((nativeWidth + 7) / 8), (GLuint)((nativeHeight + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_FRAMEBUFFER_BARRIER_BIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, baseTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, baseMinFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, baseMagFilter);
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
}

bool TexcacheOpenGLLoader::RenderCuNNy2x(int modelIndex, GLuint sourceBaseTex, GLuint targetTex,
                                         int width, int height)
{
    if (modelIndex < 0 || modelIndex >= RendererSettings::GLCuNNyModelCount)
        return false;

    const CuNNyModelInfo& model = kCuNNyModels[modelIndex];
    int currentWidth = width * model.WorkScaleX;
    int currentHeight = height * model.WorkScaleY;
    if (!EnsureCuNNyWorkTexture(0, currentWidth, currentHeight))
        return false;

    RenderCuNNyComputePass(CuNNyInShaders[modelIndex], sourceBaseTex, sourceBaseTex,
                           CuNNyWorkTex[0], width, height, width, height);

    GLuint currentTex = CuNNyWorkTex[0];
    int currentIndex = 0;
    for (int pass = 0; pass < model.ConvPasses; pass++)
    {
        const bool lastConv = pass == model.ConvPasses - 1;
        const int targetWidth = width * (lastConv ? model.FinalWorkScaleX : model.WorkScaleX);
        const int targetHeight = height * (lastConv ? model.FinalWorkScaleY : model.WorkScaleY);
        const int targetIndex = currentIndex == 0 ? 1 : 0;
        if (!EnsureCuNNyWorkTexture(targetIndex, targetWidth, targetHeight))
            return false;

        RenderCuNNyComputePass(CuNNyConvShaders[modelIndex][pass], currentTex, sourceBaseTex,
                               CuNNyWorkTex[targetIndex], currentWidth, currentHeight,
                               width, height);
        currentTex = CuNNyWorkTex[targetIndex];
        currentIndex = targetIndex;
        currentWidth = targetWidth;
        currentHeight = targetHeight;
    }

    RenderCuNNyComputePass(CuNNyOutShaders[modelIndex], currentTex, sourceBaseTex,
                           targetTex, currentWidth, currentHeight, width, height);
    return true;
}

void TexcacheOpenGLLoader::RenderAlphaAwareMipLevel(GLuint sourceTex, GLuint targetTex,
                                                    int sourceWidth, int sourceHeight,
                                                    int targetWidth, int targetHeight)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetTex, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, targetWidth, targetHeight);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(AlphaAwareMipShader);
    glUniform2i(glGetUniformLocation(AlphaAwareMipShader, "uSourceSize"), sourceWidth, sourceHeight);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);

    glBindBuffer(GL_ARRAY_BUFFER, FullscreenPassVBO);
    glBindVertexArray(FullscreenPassVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

bool TexcacheOpenGLLoader::ProcessTextureGPUScaleToTexture(u32 width, u32 height, u32 scaleFactor, const u32* sourceRGBA, GLuint& outputTexture)
{
    if (scaleFactor <= 1)
        return false;

    GLint prevDrawFramebuffer = 0;
    GLint prevReadFramebuffer = 0;
    GLint prevProgram = 0;
    GLint prevVAO = 0;
    GLint prevArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTex0 = 0;
    GLint prevTex1 = 0;
    GLint prevTex2 = 0;
    GLint prevViewport[4] = {};
    GLint prevDrawBuffer = 0;
    GLint prevReadBuffer = 0;
    GLint maxDrawBuffers = 1;
    std::vector<GLenum> prevDrawBuffers;
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencilTest = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepthMask = GL_TRUE;
    GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    maxDrawBuffers = std::max(1, maxDrawBuffers);
    prevDrawBuffers.resize((size_t)maxDrawBuffers, GL_NONE);
    for (GLint i = 0; i < maxDrawBuffers; i++)
    {
        GLint drawBuffer = GL_NONE;
        glGetIntegerv(GL_DRAW_BUFFER0 + i, &drawBuffer);
        prevDrawBuffers[(size_t)i] = (GLenum)drawBuffer;
    }
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glGetBooleani_v(GL_COLOR_WRITEMASK, 0, prevColorMask);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2);
    glActiveTexture(prevActiveTexture);

    auto restoreState = [&]()
    {
        GLint restoreDrawFramebuffer = prevDrawFramebuffer;
        GLint restoreReadFramebuffer = prevReadFramebuffer;
        if (restoreDrawFramebuffer != 0 && !glIsFramebuffer((GLuint)restoreDrawFramebuffer))
            restoreDrawFramebuffer = 0;
        if (restoreReadFramebuffer != 0 && !glIsFramebuffer((GLuint)restoreReadFramebuffer))
            restoreReadFramebuffer = 0;

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, restoreReadFramebuffer);

        GLint restoreDrawBuffer = prevDrawBuffer;
        GLint restoreReadBuffer = prevReadBuffer;
        if (restoreDrawFramebuffer == 0 && IsColorAttachmentEnum(restoreDrawBuffer))
            restoreDrawBuffer = GL_BACK;
        if (restoreReadFramebuffer == 0 && IsColorAttachmentEnum(restoreReadBuffer))
            restoreReadBuffer = GL_BACK;

        if (restoreDrawFramebuffer == 0)
        {
            glDrawBuffer(restoreDrawBuffer);
        }
        else if (!prevDrawBuffers.empty())
        {
            glDrawBuffers((GLsizei)prevDrawBuffers.size(), prevDrawBuffers.data());
        }
        else
        {
            glDrawBuffer(restoreDrawBuffer);
        }
        glReadBuffer(restoreReadBuffer);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (prevStencilTest) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        glDepthMask(prevDepthMask);
        glColorMaski(0, prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
        glUseProgram((prevProgram != 0 && glIsProgram((GLuint)prevProgram)) ? prevProgram : 0);
        glBindVertexArray((prevVAO != 0 && glIsVertexArray((GLuint)prevVAO)) ? prevVAO : 0);
        glBindBuffer(GL_ARRAY_BUFFER, (prevArrayBuffer != 0 && glIsBuffer((GLuint)prevArrayBuffer)) ? prevArrayBuffer : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (prevTex0 != 0 && glIsTexture((GLuint)prevTex0)) ? prevTex0 : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (prevTex1 != 0 && glIsTexture((GLuint)prevTex1)) ? prevTex1 : 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, (prevTex2 != 0 && glIsTexture((GLuint)prevTex2)) ? prevTex2 : 0);
        glActiveTexture(prevActiveTexture);
    };

    if (!EnsureArtCNNPrograms())
    {
        restoreState();
        return false;
    }
    if (!EnsureArtCNNBuffers())
    {
        restoreState();
        return false;
    }
    if (!EnsureArtCNNTextures(width, height, scaleFactor))
    {
        restoreState();
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, ArtCNNSourceTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, sourceRGBA);

    if (ScaleAlgorithm == RendererSettings::GLScaleAlgorithm::Spline36)
    {
        const bool replaceAlpha = AlphaXBRZ || !Spline36Alpha;
        GLuint finalTarget = replaceAlpha ? ArtCNNOutputWorkTex : ArtCNNOutputTex;
        RenderSpline36(ArtCNNSourceTex, finalTarget,
                             width * scaleFactor, height * scaleFactor, 0.0f, 0.0f);
        if (AlphaXBRZ)
        {
            RenderXBRZ(ArtCNNSourceTex, ArtCNNAlphaXBRZOutputTex, width, height, scaleFactor);
            RenderAlphaReplace(ArtCNNOutputWorkTex, ArtCNNAlphaXBRZOutputTex, ArtCNNOutputTex,
                               width * scaleFactor, height * scaleFactor);
        }
        else if (!Spline36Alpha)
            RenderMidpointAlphaReplace(ArtCNNOutputWorkTex, ArtCNNSourceTex, ArtCNNOutputTex,
                                       width * scaleFactor, height * scaleFactor);
    }
    else if (ScaleAlgorithm == RendererSettings::GLScaleAlgorithm::XBRZ)
    {
        RenderXBRZ(ArtCNNSourceTex, ArtCNNOutputTex, width, height, scaleFactor);
    }
    else
    {
        const bool useAlphaXBRZ =
            AlphaXBRZ &&
            (RendererSettings::IsGLNNEDI3Algorithm(ScaleAlgorithm) ||
             RendererSettings::IsGLArtCNNAlgorithm(ScaleAlgorithm) ||
             RendererSettings::IsGLCuNNyAlgorithm(ScaleAlgorithm));
        bool finalAlphaXBRZRendered = false;
        auto renderFinalAlphaXBRZ = [&]() -> GLuint
        {
            if (!finalAlphaXBRZRendered)
            {
                RenderXBRZ(ArtCNNSourceTex, ArtCNNAlphaXBRZOutputTex, width, height, scaleFactor);
                finalAlphaXBRZRendered = true;
            }
            return ArtCNNAlphaXBRZOutputTex;
        };
        bool finalAlphaSpline36Rendered = false;
        auto renderFinalAlphaSpline36 = [&]() -> GLuint
        {
            if (!finalAlphaSpline36Rendered)
            {
                RenderSpline36(ArtCNNSourceTex, ArtCNNAlphaXBRZOutputTex,
                                     width * scaleFactor, height * scaleFactor, 0.0f, 0.0f);
                finalAlphaSpline36Rendered = true;
            }
            return ArtCNNAlphaXBRZOutputTex;
        };
        auto renderYUVAToRGBA = [&](GLuint target, int targetWidth, int targetHeight,
                                    GLuint sourceYUVA, GLuint sourceLuma, GLuint alphaSource)
        {
            glUseProgram(ArtCNNYUVAToRGBA2xShader);
            GLint useAlphaSourceLoc = glGetUniformLocation(ArtCNNYUVAToRGBA2xShader, "uUseAlphaSource");
            if (useAlphaSourceLoc >= 0)
                glUniform1i(useAlphaSourceLoc, alphaSource != 0 ? 1 : 0);
            RenderFullscreenPass(ArtCNNYUVAToRGBA2xShader, target, targetWidth, targetHeight,
                             sourceYUVA, sourceLuma, alphaSource);
        };
        const bool useMidpointAlpha = !Spline36Alpha;
        auto finalAlphaTarget = [&](bool alphaXBRZ, bool spline36Alpha) -> GLuint
        {
            return (alphaXBRZ || useMidpointAlpha || spline36Alpha) ? ArtCNNOutputWorkTex : ArtCNNOutputTex;
        };
        auto renderFinalAlpha = [&](GLuint colorTex, bool alphaXBRZ, bool spline36Alpha)
        {
            if (alphaXBRZ)
            {
                RenderAlphaReplace(colorTex, renderFinalAlphaXBRZ(), ArtCNNOutputTex,
                                   width * scaleFactor, height * scaleFactor);
            }
            else if (useMidpointAlpha)
                RenderMidpointAlphaReplace(colorTex, ArtCNNSourceTex, ArtCNNOutputTex,
                                           width * scaleFactor, height * scaleFactor);
            else if (spline36Alpha)
            {
                RenderAlphaReplace(colorTex, renderFinalAlphaSpline36(), ArtCNNOutputTex,
                                   width * scaleFactor, height * scaleFactor);
            }
        };

        if (RendererSettings::IsGLNNEDI3Algorithm(ScaleAlgorithm))
        {
            if (!EnsureNNEDI3ComputePrograms())
            {
                restoreState();
                return false;
            }

            RenderNNEDI3ComputePass(NNEDI3VerticalComputeShader, ArtCNNSourceTex,
                                    NNEDI3VerticalTex, width, height);
            RenderNNEDI3ComputePass(NNEDI3HorizontalComputeShader, NNEDI3VerticalTex,
                                    ArtCNNLuma2xTex, width, height * 2);

            if (scaleFactor >= 4)
            {
                RenderNNEDI3ComputePass(NNEDI3VerticalComputeShader, ArtCNNLuma2xTex,
                                        NNEDI3Vertical4xTex, width * 2, height * 2);
                RenderNNEDI3ComputePass(NNEDI3HorizontalComputeShader, NNEDI3Vertical4xTex,
                                        NNEDI3Luma4xTex, width * 2, height * 4);

                GLuint finalTarget = finalAlphaTarget(useAlphaXBRZ, Spline36Alpha);
                RenderSpline36(NNEDI3Luma4xTex, finalTarget,
                                     width * scaleFactor, height * scaleFactor, -1.5f, -1.5f);
                renderFinalAlpha(finalTarget, useAlphaXBRZ, Spline36Alpha);
            }
            else
            {
                GLuint finalTarget = finalAlphaTarget(useAlphaXBRZ, Spline36Alpha);
                RenderSpline36(ArtCNNLuma2xTex, finalTarget,
                                     width * scaleFactor, height * scaleFactor, -0.5f, -0.5f);
                renderFinalAlpha(finalTarget, useAlphaXBRZ, Spline36Alpha);
            }
        }
        else if (RendererSettings::IsGLCuNNyAlgorithm(ScaleAlgorithm))
        {
            RenderFullscreenPass(RGBAToYUVAShader, ArtCNNYUVTex, width, height, ArtCNNSourceTex, 0);

            if (!EnsureCuNNyPrograms())
            {
                restoreState();
                return false;
            }

            const int cunnyModelIndex = CuNNyModelIndex();
            const CuNNyModelInfo& cunnyModel = kCuNNyModels[cunnyModelIndex];

            RenderSpline36(ArtCNNYUVTex, ArtCNNYUVA2xTex, width * 2, height * 2, 0.0f, 0.0f);
            GLuint cunnySource = cunnyModel.RGB ? ArtCNNSourceTex : ArtCNNYUVTex;
            if (!RenderCuNNy2x(cunnyModelIndex, cunnySource, ArtCNNLuma2xTex, width, height))
            {
                restoreState();
                return false;
            }

            GLuint rgba2xTarget = (scaleFactor == 2) ? finalAlphaTarget(useAlphaXBRZ, false) : ArtCNNRGBA2xTex;
            if (cunnyModel.RGB)
            {
                RenderAlphaReplace(ArtCNNLuma2xTex, ArtCNNYUVA2xTex,
                                   rgba2xTarget, width * 2, height * 2);
            }
            else
            {
                renderYUVAToRGBA(rgba2xTarget, width * 2, height * 2,
                                 ArtCNNYUVA2xTex, ArtCNNLuma2xTex, 0);
            }

            if (scaleFactor > 2)
            {
                GLuint finalTarget = finalAlphaTarget(useAlphaXBRZ, false);
                RenderSpline36(ArtCNNRGBA2xTex, finalTarget,
                                     width * scaleFactor, height * scaleFactor, 0.0f, 0.0f);
                renderFinalAlpha(finalTarget, useAlphaXBRZ, false);
            }
            else
                renderFinalAlpha(rgba2xTarget, useAlphaXBRZ, false);
        }
        else
        {
            RenderFullscreenPass(RGBAToYUVAShader, ArtCNNYUVTex, width, height, ArtCNNSourceTex, 0);

            const int modelIndex = ArtCNNModelIndex();
            if (!EnsureArtCNNComputePrograms())
            {
                restoreState();
                return false;
            }

            RenderSpline36(ArtCNNYUVTex, ArtCNNYUVA2xTex, width * 2, height * 2, 0.0f, 0.0f);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][0], ArtCNNConv0Tex, 0, width, height);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][1], ArtCNNConvWorkTex[0], 1, width, height);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][2], ArtCNNConvWorkTex[1], 2, width, height);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][3], ArtCNNConvWorkTex[0], 3, width, height);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][4], ArtCNNConvWorkTex[1], 4, width, height);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][5], ArtCNNConvWorkTex[0], 5, width, height);
            RenderArtCNNComputePass(ArtCNNConvShaders[modelIndex][6], ArtCNNPackedTex, 6, width, height);
            RenderArtCNNComputePass(ArtCNNDepthToSpaceShaders[modelIndex], ArtCNNLuma2xTex, 7, width, height);

            GLuint rgba2xTarget = (scaleFactor == 2) ? finalAlphaTarget(useAlphaXBRZ, false) : ArtCNNRGBA2xTex;
            renderYUVAToRGBA(rgba2xTarget, width * 2, height * 2,
                             ArtCNNYUVA2xTex, ArtCNNLuma2xTex, 0);

            if (scaleFactor > 2)
            {
                GLuint finalTarget = finalAlphaTarget(useAlphaXBRZ, false);
                RenderSpline36(ArtCNNRGBA2xTex, finalTarget,
                                     width * scaleFactor, height * scaleFactor, 0.0f, 0.0f);
                renderFinalAlpha(finalTarget, useAlphaXBRZ, false);
            }
            else
                renderFinalAlpha(rgba2xTarget, useAlphaXBRZ, false);
        }
    }

    outputTexture = ArtCNNOutputTex;
    restoreState();
    return true;
}

bool TexcacheOpenGLLoader::ReadScaledTextureRGBA8(GLuint sourceTex, u32 width, u32 height, std::vector<u32>& outputRGBA)
{
    if (sourceTex == 0 || ArtCNNOutputFB == 0)
        return false;

    GLint prevReadFramebuffer = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    outputRGBA.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sourceTex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outputRGBA.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFramebuffer);
    glReadBuffer(prevReadBuffer);
    return true;
}

void TexcacheOpenGLLoader::RenderArtCNNRepackToArrayLayer(GLuint sourceTex, GLuint targetArrayTexture, u32 targetLayer,
                                                          u32 mipLevel, int width, int height, int outputFmt,
                                                          bool binaryAlpha, bool queueMipmapGeneration)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetArrayTexture,
                              (GLint)mipLevel, (GLint)targetLayer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);

    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);

    GLuint repackShader = FilterableSampling ? ArtCNNRepackFilterableShader : ArtCNNRepackShader;
    glUseProgram(repackShader);
    glUniform1i(glGetUniformLocation(repackShader, "uOutputFormat"), outputFmt);
    glUniform1i(glGetUniformLocation(repackShader, "uBinaryAlpha"), binaryAlpha ? 1 : 0);
    glUniform1i(glGetUniformLocation(repackShader, "uLosslessRGB6Repack"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);

    glBindBuffer(GL_ARRAY_BUFFER, FullscreenPassVBO);
    glBindVertexArray(FullscreenPassVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (queueMipmapGeneration)
        QueueMipmapGeneration(targetArrayTexture);
}

bool TexcacheOpenGLLoader::RenderGPUAlphaAwareMipChain(GLuint level0Texture, GLuint targetArrayTexture, u32 targetLayer,
                                                       u32 width, u32 height, u32 scaleFactor, int outputFmt)
{
    if (!FilterableSampling || !FilterableMipAlphaHandling || level0Texture == 0 ||
        targetArrayTexture == 0 || AlphaAwareMipShader == 0)
        return false;

    const u32 minMipDimension = FilterableMipMinDimension();
    const int mipLevels =
        (NativeMipFloor || (SourceMipScaling && IsPowerOfTwo(scaleFactor))) && scaleFactor > 1
            ? NativeLimitedMipmapLevelCount(width, height, scaleFactor, minMipDimension)
            : MipmapLevelCount(width, height, minMipDimension);
    if (mipLevels <= 1)
        return true;

    GLuint mipScratch[2] = {};
    glGenTextures(2, mipScratch);
    if (mipScratch[0] == 0 || mipScratch[1] == 0)
    {
        if (mipScratch[0] != 0)
            glDeleteTextures(1, &mipScratch[0]);
        if (mipScratch[1] != 0)
            glDeleteTextures(1, &mipScratch[1]);
        return false;
    }

    GLuint sourceTex = level0Texture;
    u32 sourceWidth = width;
    u32 sourceHeight = height;
    int scratchIndex = 0;
    for (int level = 1; level < mipLevels; level++)
    {
        const u32 targetWidth = std::max<u32>(1, sourceWidth >> 1);
        const u32 targetHeight = std::max<u32>(1, sourceHeight >> 1);
        GLuint targetScratch = mipScratch[scratchIndex];

        ResizeArtCNNTexture(targetScratch, GL_RGBA8, static_cast<int>(targetWidth), static_cast<int>(targetHeight));
        RenderAlphaAwareMipLevel(sourceTex, targetScratch,
                                 static_cast<int>(sourceWidth), static_cast<int>(sourceHeight),
                                 static_cast<int>(targetWidth), static_cast<int>(targetHeight));
        RenderArtCNNRepackToArrayLayer(targetScratch, targetArrayTexture, targetLayer, static_cast<u32>(level),
                                       static_cast<int>(targetWidth), static_cast<int>(targetHeight),
                                       outputFmt, true, false);

        sourceTex = targetScratch;
        sourceWidth = targetWidth;
        sourceHeight = targetHeight;
        scratchIndex ^= 1;
    }

    glDeleteTextures(2, mipScratch);
    return true;
}

bool TexcacheOpenGLLoader::ReadTextureArrayLayerPreviewRGBA8(GLuint sourceArrayTexture, u32 layer, u32 width, u32 height,
                                                             int outputFmt, std::vector<u32>& outputRGBA)
{
    if (sourceArrayTexture == 0 || ArtCNNOutputFB == 0)
        return false;

    GLint prevReadFramebuffer = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    std::vector<u32> packedData(static_cast<size_t>(width) * static_cast<size_t>(height));
    outputRGBA.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ArtCNNOutputFB);
    glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sourceArrayTexture, 0, (GLint)layer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height,
                 FilterableSampling ? GL_RGBA : GL_RGBA_INTEGER,
                 GL_UNSIGNED_BYTE, packedData.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFramebuffer);
    glReadBuffer(prevReadBuffer);

    switch (outputFmt)
    {
    case outputFmt_RGB6A5:
        ConvertOutputBufferToPreviewRGBA8<outputFmt_RGB6A5>(width, height, packedData.data(), outputRGBA.data());
        break;
    case outputFmt_RGBA8:
        ConvertOutputBufferToPreviewRGBA8<outputFmt_RGBA8>(width, height, packedData.data(), outputRGBA.data());
        break;
    case outputFmt_BGRA8:
        ConvertOutputBufferToPreviewRGBA8<outputFmt_BGRA8>(width, height, packedData.data(), outputRGBA.data());
        break;
    default:
        return false;
    }

    return true;
}

bool TexcacheOpenGLLoader::ReadTextureLayerPreviewRGBA8(GLuint sourceArrayTexture, u32 layer, u32 width, u32 height,
                                                        int outputFmt, std::vector<u32>& outputRGBA)
{
    if (!EnsureArtCNNBuffers())
        return false;

    return ReadTextureArrayLayerPreviewRGBA8(sourceArrayTexture, layer, width, height, outputFmt, outputRGBA);
}

bool TexcacheOpenGLLoader::ProcessTextureGPUScaleToCacheLayer(u32 width, u32 height, u32 scaleFactor, const u32* sourceRGBA,
                                                              int outputFmt, bool binaryAlpha,
                                                              GLuint targetArrayTexture, u32 targetLayer,
                                                              std::vector<u32>* outputPreviewRGBA,
                                                              bool alphaAwareMipChain,
                                                              bool allowFilterableBinaryAlphaDefaultMips)
{
    if (FilterableSampling && FilterableMipAlphaHandling && binaryAlpha &&
        !alphaAwareMipChain && !allowFilterableBinaryAlphaDefaultMips)
        return false;

    GLuint outputTexture = 0;
    if (!ProcessTextureGPUScaleToTexture(width, height, scaleFactor, sourceRGBA, outputTexture))
        return false;

    GLint prevDrawFramebuffer = 0;
    GLint prevReadFramebuffer = 0;
    GLint prevProgram = 0;
    GLint prevVAO = 0;
    GLint prevArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTex0 = 0;
    GLint prevViewport[4] = {};
    GLint prevDrawBuffer = 0;
    GLint prevReadBuffer = 0;
    GLint maxDrawBuffers = 1;
    std::vector<GLenum> prevDrawBuffers;
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencilTest = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepthMask = GL_TRUE;
    GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    maxDrawBuffers = std::max(1, maxDrawBuffers);
    prevDrawBuffers.resize((size_t)maxDrawBuffers, GL_NONE);
    for (GLint i = 0; i < maxDrawBuffers; i++)
    {
        GLint drawBuffer = GL_NONE;
        glGetIntegerv(GL_DRAW_BUFFER0 + i, &drawBuffer);
        prevDrawBuffers[(size_t)i] = (GLenum)drawBuffer;
    }
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glGetBooleani_v(GL_COLOR_WRITEMASK, 0, prevColorMask);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    glActiveTexture(prevActiveTexture);

    auto restoreState = [&]()
    {
        GLint restoreDrawFramebuffer = prevDrawFramebuffer;
        GLint restoreReadFramebuffer = prevReadFramebuffer;
        if (restoreDrawFramebuffer != 0 && !glIsFramebuffer((GLuint)restoreDrawFramebuffer))
            restoreDrawFramebuffer = 0;
        if (restoreReadFramebuffer != 0 && !glIsFramebuffer((GLuint)restoreReadFramebuffer))
            restoreReadFramebuffer = 0;

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreDrawFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, restoreReadFramebuffer);

        GLint restoreDrawBuffer = prevDrawBuffer;
        GLint restoreReadBuffer = prevReadBuffer;
        if (restoreDrawFramebuffer == 0 && IsColorAttachmentEnum(restoreDrawBuffer))
            restoreDrawBuffer = GL_BACK;
        if (restoreReadFramebuffer == 0 && IsColorAttachmentEnum(restoreReadBuffer))
            restoreReadBuffer = GL_BACK;

        if (restoreDrawFramebuffer == 0)
            glDrawBuffer(restoreDrawBuffer);
        else if (!prevDrawBuffers.empty())
            glDrawBuffers((GLsizei)prevDrawBuffers.size(), prevDrawBuffers.data());
        else
            glDrawBuffer(restoreDrawBuffer);
        glReadBuffer(restoreReadBuffer);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (prevStencilTest) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        glDepthMask(prevDepthMask);
        glColorMaski(0, prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
        glUseProgram((prevProgram != 0 && glIsProgram((GLuint)prevProgram)) ? prevProgram : 0);
        glBindVertexArray((prevVAO != 0 && glIsVertexArray((GLuint)prevVAO)) ? prevVAO : 0);
        glBindBuffer(GL_ARRAY_BUFFER, (prevArrayBuffer != 0 && glIsBuffer((GLuint)prevArrayBuffer)) ? prevArrayBuffer : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (prevTex0 != 0 && glIsTexture((GLuint)prevTex0)) ? prevTex0 : 0);
        glActiveTexture(prevActiveTexture);
    };

    if (alphaAwareMipChain &&
        !RenderGPUAlphaAwareMipChain(outputTexture, targetArrayTexture, targetLayer,
                                     width * scaleFactor, height * scaleFactor, scaleFactor, outputFmt))
    {
        restoreState();
        return false;
    }

    RenderArtCNNRepackToArrayLayer(outputTexture, targetArrayTexture, targetLayer, 0,
                                   width * scaleFactor, height * scaleFactor, outputFmt, binaryAlpha,
                                   !alphaAwareMipChain);

    if (outputPreviewRGBA != nullptr)
    {
        if (!ReadTextureArrayLayerPreviewRGBA8(targetArrayTexture, targetLayer,
                width * scaleFactor, height * scaleFactor,
                outputFmt, *outputPreviewRGBA))
            outputPreviewRGBA->clear();
    }

    restoreState();
    return true;
}

bool TexcacheOpenGLLoader::ProcessTextureGPUScale(u32 width, u32 height, u32 scaleFactor, const u32* sourceRGBA, std::vector<u32>& outputRGBA)
{
    GLuint outputTexture = 0;
    if (!ProcessTextureGPUScaleToTexture(width, height, scaleFactor, sourceRGBA, outputTexture))
        return false;

    return ReadScaledTextureRGBA8(outputTexture, width * scaleFactor, height * scaleFactor, outputRGBA);
}

}
