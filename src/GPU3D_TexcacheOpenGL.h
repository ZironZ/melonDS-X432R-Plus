#ifndef GPU3D_TEXCACHEOPENGL
#define GPU3D_TEXCACHEOPENGL

#include "GPU3D_Texcache.h"
#include "OpenGLSupport.h"
#include "RendererSettings.h"

#include <string>
#include <vector>

namespace melonDS
{

template <typename, typename>
class Texcache;

class TexcacheOpenGLLoader
{
public:
    TexcacheOpenGLLoader(bool compute) : IsCompute(compute), PreferredOutput(outputFmt_RGB6A5) {}
    ~TexcacheOpenGLLoader();

    GLuint GenerateTexture(u32 width, u32 height, u32 layers, u32 scaleFactor);
    void UploadTexture(GLuint handle, u32 width, u32 height, u32 layer, void* data);
    void UploadTextureLevel(GLuint handle, u32 width, u32 height, u32 layer, u32 level, void* data);
    void DeleteTexture(GLuint handle);
    int PreferredOutputFormat() const { return PreferredOutput; }
    bool SetPreferredOutputFormat(int outputFmt)
    {
        if (PreferredOutput == outputFmt)
            return false;
        PreferredOutput = outputFmt;
        return true;
    }
    bool FilterableSamplingEnabled() const { return FilterableSampling; }
    bool SetFilterableSampling(bool enabled)
    {
        if (FilterableSampling == enabled)
            return false;
        FilterableSampling = enabled;
        return true;
    }
    bool UseFilterableMipAlphaHandling() const { return FilterableMipAlphaHandling; }
    bool SetFilterableMipAlphaHandling(bool enabled)
    {
        if (FilterableMipAlphaHandling == enabled)
            return false;
        FilterableMipAlphaHandling = enabled;
        return true;
    }
    bool UseFilterableMipTopologyHandling() const { return FilterableMipTopologyHandling; }
    bool SetFilterableMipTopologyHandling(bool enabled)
    {
        if (FilterableMipTopologyHandling == enabled)
            return false;
        FilterableMipTopologyHandling = enabled;
        return true;
    }
    bool UseFilterableMipSubrectHandling() const { return FilterableMipSubrectHandling; }
    bool SetFilterableMipSubrectHandling(bool enabled)
    {
        if (FilterableMipSubrectHandling == enabled)
            return false;
        FilterableMipSubrectHandling = enabled;
        return true;
    }
    bool SetNativeMipFloor(bool enabled)
    {
        if (NativeMipFloor == enabled)
            return false;
        NativeMipFloor = enabled;
        return true;
    }
    bool UseNativeMipFloor() const { return NativeMipFloor; }
    bool SetSourceMipScaling(bool enabled)
    {
        if (SourceMipScaling == enabled)
            return false;
        SourceMipScaling = enabled;
        return true;
    }
    bool UseSourceMipScaling() const { return SourceMipScaling; }
    u32 FilterableMipMinDimension() const
    {
        return RendererSettings::GetTextureFilterMipMinDimension(FilterableMipDepth);
    }
    bool SetFilterableMipDepth(RendererSettings::TextureFilterMipDepth mipDepth)
    {
        if (FilterableMipDepth == mipDepth)
            return false;
        FilterableMipDepth = mipDepth;
        return true;
    }
    RendererSettings::GLScaleAlgorithm ScalingAlgorithm() const { return ScaleAlgorithm; }
    bool SetScalingAlgorithm(RendererSettings::GLScaleAlgorithm algorithm)
    {
        if (ScaleAlgorithm == algorithm)
            return false;
        ScaleAlgorithm = algorithm;
        return true;
    }
    bool SetQualityAlphaHandling(bool qualityAlphaHandling)
    {
        if (QualityAlphaHandling == qualityAlphaHandling)
            return false;
        QualityAlphaHandling = qualityAlphaHandling;
        return true;
    }
    bool UseQualityAlphaHandling() const { return QualityAlphaHandling; }
    bool SetLosslessRGB6Repack(bool losslessRGB6Repack)
    {
        if (LosslessRGB6Repack == losslessRGB6Repack)
            return false;
        LosslessRGB6Repack = losslessRGB6Repack;
        return true;
    }
    bool UseLosslessRGB6Repack() const { return LosslessRGB6Repack; }
    bool SetLegacyAlphaHandling(bool legacyAlphaHandling)
    {
        if (LegacyAlphaHandling == legacyAlphaHandling)
            return false;
        LegacyAlphaHandling = legacyAlphaHandling;
        return true;
    }
    bool UseLegacyAlphaHandling() const { return LegacyAlphaHandling; }
    bool ProcessTextureGPUScaleToTexture(u32 width, u32 height, u32 scaleFactor, const u32* sourceRGBA, GLuint& outputTexture);
    bool ProcessTextureGPUScaleToCacheLayer(u32 width, u32 height, u32 scaleFactor, const u32* sourceRGBA,
                                            int outputFmt, bool binaryAlpha,
                                            GLuint targetArrayTexture, u32 targetLayer,
                                            std::vector<u32>* outputPreviewRGBA = nullptr);
    bool ProcessTextureGPUScale(u32 width, u32 height, u32 scaleFactor, const u32* sourceRGBA, std::vector<u32>& outputRGBA);
    bool ReadTextureLayerPreviewRGBA8(GLuint sourceArrayTexture, u32 layer, u32 width, u32 height,
                                      int outputFmt, std::vector<u32>& outputRGBA);

private:
    int ArtCNNModelIndex() const;
    bool EnsureArtCNNPrograms();
    bool EnsureArtCNNBuffers();
    bool EnsureArtCNNTextures(u32 width, u32 height, u32 scaleFactor);
    void DeleteArtCNNResources();
    std::string BuildTexcacheRepackShaderSource(bool filterable) const;
    void RenderArtCNNPass(GLuint shader, GLuint outputTex, int width, int height, GLuint source0, GLuint source1);
    void RenderArtCNNSpline36(GLuint sourceTex, GLuint targetTex, int width, int height, float sourceShiftX = 0.0f, float sourceShiftY = 0.0f);
    void RenderXBRZ(GLuint sourceTex, GLuint targetTex, int width, int height, u32 scaleFactor);
    bool ReadScaledTextureRGBA8(GLuint sourceTex, u32 width, u32 height, std::vector<u32>& outputRGBA);
    void RenderArtCNNRepackToArrayLayer(GLuint sourceTex, GLuint targetArrayTexture, u32 targetLayer,
                                        int width, int height, int outputFmt, bool binaryAlpha);
    bool ReadTextureArrayLayerPreviewRGBA8(GLuint sourceArrayTexture, u32 layer, u32 width, u32 height,
                                           int outputFmt, std::vector<u32>& outputRGBA);

    bool IsCompute;
    int PreferredOutput;
    bool FilterableSampling = false;
    bool FilterableMipAlphaHandling = false;
    bool FilterableMipTopologyHandling = false;
    bool FilterableMipSubrectHandling = false;
    bool NativeMipFloor = false;
    bool SourceMipScaling = false;
    RendererSettings::TextureFilterMipDepth FilterableMipDepth = RendererSettings::TextureFilterMipDepth::Full;
    RendererSettings::GLScaleAlgorithm ScaleAlgorithm = RendererSettings::GLScaleAlgorithm::Spline36;
    bool LegacyAlphaHandling = false;
    bool QualityAlphaHandling = false;
    bool LosslessRGB6Repack = false;
    bool ArtCNNProgramsReady = false;
    bool ArtCNNProgramsFailed = false;

    GLuint ArtCNNPassVBO = 0;
    GLuint ArtCNNPassVAO = 0;
    GLuint ArtCNNOutputFB = 0;

    GLuint ArtCNNRGBToYUVAShader = 0;
    GLuint ArtCNNRepackShader = 0;
    GLuint ArtCNNRepackFilterableShader = 0;
    GLuint ArtCNNConvShaders[RendererSettings::GLArtCNNModelCount][7] {};
    GLuint ArtCNNDepthToSpaceShaders[RendererSettings::GLArtCNNModelCount] {};
    GLuint ArtCNNSpline36Shader = 0;
    GLuint ArtCNNYUVAToRGBA2xShader = 0;
    GLuint NNEDI3Pass1Shader = 0;
    GLuint NNEDI3Pass2Shader = 0;
    GLuint XBRZPreprocessShader = 0;
    GLuint XBRZFreescaleShader = 0;

    GLuint ArtCNNSourceTex = 0;
    GLuint ArtCNNYUVTex = 0;
    GLuint ArtCNNConv0Tex = 0;
    GLuint ArtCNNConvWorkTex[2] {};
    GLuint ArtCNNPackedTex = 0;
    GLuint NNEDI3VerticalTex = 0;
    GLuint NNEDI3Vertical4xTex = 0;
    GLuint ArtCNNLuma2xTex = 0;
    GLuint NNEDI3Luma4xTex = 0;
    GLuint NNEDI3Luma4xCorrectedTex = 0;
    GLuint ArtCNNYUVA2xTex = 0;
    GLuint NNEDI3YUVA4xTex = 0;
    GLuint ArtCNNRGBA2xTex = 0;
    GLuint NNEDI3RGBA4xTex = 0;
    GLuint ArtCNNOutputTex = 0;
    GLuint XBRZInfoTex = 0;

    u32 ArtCNNWidth = 0;
    u32 ArtCNNHeight = 0;
    u32 ArtCNNScaleFactor = 0;
};

using TexcacheOpenGL = Texcache<TexcacheOpenGLLoader, GLuint>;

}

#endif
