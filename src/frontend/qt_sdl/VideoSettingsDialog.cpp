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

#include <QFileDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QWhatsThis>
#include <QtGlobal>

#include "types.h"
#include "Platform.h"
#include "Config.h"
#include "GPU.h"
#include "RendererSettings.h"
#include "main.h"

#include "VideoSettingsDialog.h"
#include "ui_VideoSettingsDialog.h"


inline bool VideoSettingsDialog::UsesGL()
{
    auto& cfg = emuInstance->getGlobalConfig();
    return cfg.GetBool("Screen.UseGL") || (cfg.GetInt("3D.Renderer") != renderer3D_Software);
}

VideoSettingsDialog* VideoSettingsDialog::currentDlg = nullptr;

namespace
{
using GLScaleAlgorithm = melonDS::RendererSettings::GLScaleAlgorithm;
using WholeScene2DScaleMode = melonDS::RendererSettings::WholeScene2DScaleMode;
using WholeScene2DFragmentationFallback = melonDS::RendererSettings::WholeScene2DFragmentationFallback;
using FinalUpscale3DDownsampleFilter = melonDS::RendererSettings::FinalUpscale3DDownsampleFilter;
using TextureFilterMipDepth = melonDS::RendererSettings::TextureFilterMipDepth;

int ReadScaleAlgorithmConfig(Config::Table& cfg, const char* algorithmKey, bool allowXBRZ)
{
    auto algorithm = melonDS::RendererSettings::GetGLScaleAlgorithm(cfg.GetInt(algorithmKey));
    if (!allowXBRZ && algorithm == GLScaleAlgorithm::XBRZ)
        algorithm = GLScaleAlgorithm::Spline36;
    return melonDS::RendererSettings::GetGLScaleAlgorithmIndex(algorithm);
}

void WriteScaleAlgorithmConfig(Config::Table& cfg, const char* algorithmKey, int index, bool allowXBRZ)
{
    auto algorithm = melonDS::RendererSettings::GetGLScaleAlgorithm(index);
    if (!allowXBRZ && algorithm == GLScaleAlgorithm::XBRZ)
        algorithm = GLScaleAlgorithm::Spline36;
    cfg.SetInt(algorithmKey, melonDS::RendererSettings::GetGLScaleAlgorithmIndex(algorithm));
}

void PopulateScaleAlgorithmCombo(QComboBox* combo, std::initializer_list<GLScaleAlgorithm> algorithms)
{
    int item = 0;
    for (GLScaleAlgorithm algorithm : algorithms)
        combo->setItemData(item++, melonDS::RendererSettings::GetGLScaleAlgorithmIndex(algorithm));
}

int ReadScaleAlgorithmCombo(const QComboBox* combo)
{
    QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : combo->currentIndex();
}

void SetScaleAlgorithmCombo(QComboBox* combo, int algorithmIndex)
{
    int comboIndex = combo->findData(algorithmIndex);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void PopulateWholeScene2DScaleModeCombo(QComboBox* combo, bool advanced, int selectedMode)
{
    QSignalBlocker blocker(combo);
    combo->clear();

    auto addMode = [combo](const char* name, WholeScene2DScaleMode mode)
    {
        combo->addItem(name, melonDS::RendererSettings::GetWholeScene2DScaleModeIndex(mode));
    };

    addMode("Hybrid Upscale", WholeScene2DScaleMode::ConservativeHybridUpscale);
    addMode("Postprocessing Upscale", WholeScene2DScaleMode::FinalNativeUpscale);

    if (advanced)
    {
        addMode("Presentation Overlay Upscale", WholeScene2DScaleMode::OverlayOperatorUpscale);
        addMode("High-resolution Compositor", WholeScene2DScaleMode::HighResCompositor);
        addMode("Native Stack Upscale", WholeScene2DScaleMode::LegacyNativeUpscale);
    }
    else
    {
        const auto selected = melonDS::RendererSettings::GetWholeScene2DScaleMode(selectedMode);
        switch (selected)
        {
        case WholeScene2DScaleMode::OverlayOperatorUpscale:
            addMode("Presentation Overlay Upscale (custom)", selected);
            break;
        case WholeScene2DScaleMode::HighResCompositor:
            addMode("High-resolution Compositor (custom)", selected);
            break;
        case WholeScene2DScaleMode::LegacyNativeUpscale:
            addMode("Native Stack Upscale (custom)", selected);
            break;
        default:
            break;
        }
    }

    int comboIndex = combo->findData(selectedMode);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

int ReadWholeScene2DScaleModeCombo(const QComboBox* combo)
{
    QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : combo->currentIndex();
}

void SetWholeScene2DScaleModeCombo(QComboBox* combo, int modeIndex)
{
    int comboIndex = combo->findData(modeIndex);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void PopulateWholeScene2DFragmentationFallbackCombo(QComboBox* combo)
{
    combo->setItemData(0, melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(WholeScene2DFragmentationFallback::AutoCurrentForBitmap));
    combo->setItemData(1, melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(WholeScene2DFragmentationFallback::FinalNative));
    combo->setItemData(2, melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(WholeScene2DFragmentationFallback::CurrentForSevere));
    combo->setItemData(3, melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(WholeScene2DFragmentationFallback::Off));
}

int ReadWholeScene2DFragmentationFallbackCombo(const QComboBox* combo)
{
    QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : combo->currentIndex();
}

void SetWholeScene2DFragmentationFallbackCombo(QComboBox* combo, int fallbackIndex)
{
    int comboIndex = combo->findData(fallbackIndex);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void PopulateFinalUpscale3DFilterCombo(QComboBox* combo)
{
    combo->setItemData(0, melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilterIndex(FinalUpscale3DDownsampleFilter::Area));
    combo->setItemData(1, melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilterIndex(FinalUpscale3DDownsampleFilter::Linear));
    combo->setItemData(2, melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilterIndex(FinalUpscale3DDownsampleFilter::Tent));
}

int ReadFinalUpscale3DFilterCombo(const QComboBox* combo)
{
    QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : combo->currentIndex();
}

void SetFinalUpscale3DFilterCombo(QComboBox* combo, int filterIndex)
{
    int comboIndex = combo->findData(filterIndex);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void PopulateAnisotropyCombo(QComboBox* combo)
{
    static const int levels[] = {1, 2, 4, 8, 16};
    for (int i = 0; i < 5; i++)
        combo->setItemData(i, levels[i]);
}

int ReadAnisotropyCombo(const QComboBox* combo)
{
    QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : 1;
}

void SetAnisotropyCombo(QComboBox* combo, int anisotropy)
{
    int comboIndex = combo->findData(anisotropy);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void PopulateTextureFilterMipDepthCombo(QComboBox* combo)
{
    combo->setItemData(0, melonDS::RendererSettings::GetTextureFilterMipDepthIndex(TextureFilterMipDepth::Full));
    combo->setItemData(1, melonDS::RendererSettings::GetTextureFilterMipDepthIndex(TextureFilterMipDepth::Min8));
    combo->setItemData(2, melonDS::RendererSettings::GetTextureFilterMipDepthIndex(TextureFilterMipDepth::Min16));
    combo->setItemData(3, melonDS::RendererSettings::GetTextureFilterMipDepthIndex(TextureFilterMipDepth::Min32));
}

int ReadTextureFilterMipDepthCombo(const QComboBox* combo)
{
    QVariant data = combo->currentData();
    return data.isValid() ? data.toInt() : combo->currentIndex();
}

void SetTextureFilterMipDepthCombo(QComboBox* combo, int mipDepthIndex)
{
    int comboIndex = combo->findData(mipDepthIndex);
    combo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void SetLayoutVisible(QLayout* layout, bool visible)
{
    for (int i = 0; i < layout->count(); i++)
    {
        QLayoutItem* item = layout->itemAt(i);
        if (QWidget* widget = item->widget())
            widget->setVisible(visible);
        if (QLayout* childLayout = item->layout())
            SetLayoutVisible(childLayout, visible);
    }
}

void SetCompactGridOptions(QGridLayout* layout, std::initializer_list<std::pair<QWidget*, bool>> options, int columns)
{
    for (const auto& option : options)
    {
        layout->removeWidget(option.first);
        option.first->setVisible(option.second);
    }

    int visibleIndex = 0;
    for (const auto& option : options)
    {
        if (!option.second)
            continue;

        layout->addWidget(option.first, visibleIndex / columns, visibleIndex % columns);
        visibleIndex++;
    }
}

void CopyWhatsThis(QWidget* target, const QWidget* source)
{
    if (target && source && target->whatsThis().isEmpty())
        target->setWhatsThis(source->whatsThis());
}
}

void VideoSettingsDialog::setEnabled()
{
    auto& cfg = emuInstance->getGlobalConfig();
    int renderer = cfg.GetInt("3D.Renderer");

    bool softwareRenderer = renderer == renderer3D_Software;
    bool advancedSettings = UsesGL() && ui->cbAdvancedVideoSettings->isChecked();
    const int configuredWholeSceneMode = melonDS::RendererSettings::GetWholeScene2DScaleModeIndex(
        melonDS::RendererSettings::GetWholeScene2DScaleMode(cfg.GetInt("3D.GL.WholeScene2DScaleMode")));
    PopulateWholeScene2DScaleModeCombo(ui->cbxWholeScene2DScaleMode, advancedSettings, configuredWholeSceneMode);
    const int wholeSceneModeIndex = configuredWholeSceneMode;

    auto wholeSceneAlgorithm = melonDS::RendererSettings::GetGLScaleAlgorithm(ReadScaleAlgorithmCombo(ui->cbxWholeScene2DScaleAlgorithm));
    auto wholeSceneMode = melonDS::RendererSettings::GetWholeScene2DScaleMode(wholeSceneModeIndex);
    bool wholeSceneEnabled = UsesGL() && ui->cbWholeScene2DScale->isChecked();
    bool wholeSceneLegacyMode = wholeSceneMode == melonDS::RendererSettings::WholeScene2DScaleMode::LegacyNativeUpscale;
    bool wholeSceneHighResMode = wholeSceneMode == melonDS::RendererSettings::WholeScene2DScaleMode::HighResCompositor;
    bool wholeSceneFinalMode = wholeSceneMode == melonDS::RendererSettings::WholeScene2DScaleMode::FinalNativeUpscale;
    bool wholeSceneOverlayMode = wholeSceneMode == melonDS::RendererSettings::WholeScene2DScaleMode::OverlayOperatorUpscale;
    bool wholeSceneHybridMode = wholeSceneMode == melonDS::RendererSettings::WholeScene2DScaleMode::ConservativeHybridUpscale;
    bool wholeSceneFinalLikeMode = wholeSceneFinalMode || wholeSceneOverlayMode || wholeSceneHybridMode;
    bool showFinalUpscaleRender3DNative =
        wholeSceneEnabled && (wholeSceneFinalMode || (advancedSettings && wholeSceneFinalLikeMode));
    bool anisotropicFiltering = UsesGL() && (ReadAnisotropyCombo(ui->cbx3DTextureAnisotropy) > 1);
    bool alphaAwareFiltering = anisotropicFiltering && ui->cb3DTextureFilterMipmapAlphaHandling->isChecked();
    bool textureScalingVisible = UsesGL() && ui->cb3DTextureScaling->isChecked();
    auto textureScalingAlgorithm = melonDS::RendererSettings::GetGLScaleAlgorithm(ReadScaleAlgorithmCombo(ui->cbx3DTextureScalingAlgorithm));
    bool alphaXBRZTextureAlgorithm =
        textureScalingAlgorithm == melonDS::RendererSettings::GLScaleAlgorithm::Spline36 ||
        melonDS::RendererSettings::IsGLArtCNNAlgorithm(textureScalingAlgorithm) ||
        melonDS::RendererSettings::IsGLNNEDI3Algorithm(textureScalingAlgorithm) ||
        melonDS::RendererSettings::IsGLCuNNyAlgorithm(textureScalingAlgorithm);
    bool spline36AlphaTextureAlgorithm = alphaXBRZTextureAlgorithm;
    bool spriteUVInsetApplicable = UsesGL() && (anisotropicFiltering || textureScalingVisible);
    bool smart2DFilteringApplicable = UsesGL() && (anisotropicFiltering || textureScalingVisible);
    bool edgeExtendApplicable = UsesGL() && textureScalingVisible;
    bool spriteUVInsetVisible = advancedSettings && spriteUVInsetApplicable;
    bool smart2DFilteringVisible = advancedSettings && smart2DFilteringApplicable;
    bool translucentTextureGuardApplicable = UsesGL() && anisotropicFiltering;
    bool translucentTextureGuardVisible = advancedSettings && translucentTextureGuardApplicable;
    bool highRes3D = ui->cbxGLResolution->currentIndex() > 0;
    bool wholeSceneFinal3DResolve =
        wholeSceneEnabled &&
        wholeSceneFinalLikeMode &&
        highRes3D &&
        !ui->cbWholeScene2DScaleFinalUpscaleRender3DNative->isChecked();
    bool wholeSceneSplit3D =
        wholeSceneFinal3DResolve &&
        ui->cbWholeScene2DScaleFinalUpscale3DSplitSemantics->isChecked();
    bool wholeSceneSupportsSourceBoundaryGuard =
        wholeSceneAlgorithm == melonDS::RendererSettings::GLScaleAlgorithm::Spline36 ||
        melonDS::RendererSettings::IsGLArtCNNAlgorithm(wholeSceneAlgorithm) ||
        melonDS::RendererSettings::IsGLCuNNyAlgorithm(wholeSceneAlgorithm) ||
        wholeSceneAlgorithm == melonDS::RendererSettings::GLScaleAlgorithm::NNEDI3 ||
        wholeSceneAlgorithm == melonDS::RendererSettings::GLScaleAlgorithm::XBRZ;
    bool gpuTextureAlgorithm = UsesGL() && ui->cb3DTextureScaling->isChecked();

    ui->btnRecommendedDefaults->setEnabled(!softwareRenderer);
    ui->cbAdvancedVideoSettings->setEnabled(!softwareRenderer);
    ui->cbGLDisplay->setEnabled(softwareRenderer);
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer);
    ui->cbxGLResolution->setEnabled(!softwareRenderer);
    ui->cbBetterPolygons->setEnabled(renderer == renderer3D_OpenGL);
    ui->cbReadable3DTextureCache->setVisible(advancedSettings);
    ui->cbReadable3DTextureCache->setEnabled(renderer == renderer3D_OpenGL);
    ui->cbx3DTextureAnisotropy->setEnabled(UsesGL());
    ui->lbl3DTextureAnisotropy->setEnabled(UsesGL());
    ui->cb3DTexture2DAtlasProtection->setVisible(
        UsesGL() && !advancedSettings && (anisotropicFiltering || textureScalingVisible));
    ui->cb3DTexture2DAtlasProtection->setEnabled(UsesGL());
    ui->cbxComputeHiResCoords->setVisible(true);
    SetLayoutVisible(ui->horizontalLayout3DTextureFilterMipDepth, advancedSettings && anisotropicFiltering);
    ui->cb3DTextureFilterBinaryAlphaHandling->setVisible(advancedSettings && anisotropicFiltering);
    ui->cb3DTextureFilterMipmapAlphaHandling->setVisible(advancedSettings && anisotropicFiltering);
    ui->cb3DTextureFilterMipmapTopologyHandling->setVisible(advancedSettings && alphaAwareFiltering);
    ui->cb3DTextureFilterMipmapSubrectHandling->setVisible(advancedSettings && alphaAwareFiltering);
    ui->cb3DTextureLosslessRGB6Repack->setVisible(advancedSettings && alphaAwareFiltering);
    ui->cb3DTextureFilterSmart2D->setVisible(smart2DFilteringVisible);
    ui->cb3DTextureFilterTranslucentGuard->setVisible(translucentTextureGuardVisible);
    ui->cb3DTextureFilterSpriteUVInset->setVisible(spriteUVInsetVisible);
    ui->cb3DHighPrecisionTextureCoordinates->setVisible(
        advancedSettings && renderer == renderer3D_OpenGLCompute);
    ui->cbx3DTextureFilterMipDepth->setEnabled(anisotropicFiltering);
    ui->lbl3DTextureFilterMipDepth->setEnabled(anisotropicFiltering);
    ui->cb3DTextureFilterBinaryAlphaHandling->setEnabled(anisotropicFiltering);
    ui->cb3DTextureFilterMipmapAlphaHandling->setEnabled(anisotropicFiltering);
    ui->cb3DTextureFilterMipmapTopologyHandling->setEnabled(alphaAwareFiltering);
    ui->cb3DTextureFilterMipmapSubrectHandling->setEnabled(alphaAwareFiltering);
    ui->cb3DTextureLosslessRGB6Repack->setEnabled(alphaAwareFiltering);
    ui->cb3DTextureFilterSmart2D->setEnabled(smart2DFilteringApplicable);
    ui->cb3DTextureFilterTranslucentGuard->setEnabled(translucentTextureGuardApplicable);
    ui->cb3DTextureFilterSpriteUVInset->setEnabled(spriteUVInsetApplicable);
    ui->cb3DHighPrecisionTextureCoordinates->setEnabled(
        renderer == renderer3D_OpenGLCompute && highRes3D);
    ui->cb3DTextureScaling->setEnabled(UsesGL());
    SetLayoutVisible(ui->horizontalLayout3DTextureScalingAlgorithm, UsesGL() && ui->cb3DTextureScaling->isChecked());
    SetCompactGridOptions(ui->gridLayout3DTextureScalingOptions, {
        {ui->cb3DTextureScalingAlphaXBRZ, textureScalingVisible && alphaXBRZTextureAlgorithm},
        {ui->cb3DTextureScalingFrequentChangePolicy, textureScalingVisible},
        {ui->cb3DTextureScalingDeferred, textureScalingVisible},
        {ui->cb3DTextureScalingLegacyAlphaHandling, advancedSettings && textureScalingVisible},
        {ui->cb3DTextureScalingQualityAlphaHandling, advancedSettings && textureScalingVisible},
        {ui->cb3DTextureScalingSpline36Alpha, advancedSettings && textureScalingVisible && spline36AlphaTextureAlgorithm},
        {ui->cb3DTextureScalingNativeMipFloor, advancedSettings && textureScalingVisible && anisotropicFiltering},
        {ui->cb3DTextureScalingSourceMips, advancedSettings && textureScalingVisible && anisotropicFiltering},
        {ui->cb3DTextureScalingEdgeExtendUnusedMargins, advancedSettings && edgeExtendApplicable},
    }, 3);
    ui->cbx3DTextureScalingAlgorithm->setEnabled(UsesGL() && ui->cb3DTextureScaling->isChecked());
    ui->lbl3DTextureScalingAlgorithm->setEnabled(UsesGL() && ui->cb3DTextureScaling->isChecked());
    ui->cb3DTextureScalingFrequentChangePolicy->setEnabled(UsesGL() && ui->cb3DTextureScaling->isChecked());
    ui->cb3DTextureScalingDeferred->setEnabled(UsesGL() && ui->cb3DTextureScaling->isChecked());
    ui->cb3DTextureScalingNativeMipFloor->setEnabled(
        UsesGL() && ui->cb3DTextureScaling->isChecked() && anisotropicFiltering);
    ui->cb3DTextureScalingSourceMips->setEnabled(
        UsesGL() && ui->cb3DTextureScaling->isChecked() && anisotropicFiltering);
    ui->cb3DTextureScalingEdgeExtendUnusedMargins->setEnabled(edgeExtendApplicable);
    ui->cb3DTextureScalingLegacyAlphaHandling->setEnabled(UsesGL() && ui->cb3DTextureScaling->isChecked() && gpuTextureAlgorithm);
    ui->cb3DTextureScalingQualityAlphaHandling->setEnabled(UsesGL() && ui->cb3DTextureScaling->isChecked() &&
                                                           gpuTextureAlgorithm && !ui->cb3DTextureScalingLegacyAlphaHandling->isChecked());
    ui->cb3DTextureScalingAlphaXBRZ->setEnabled(
        UsesGL() && ui->cb3DTextureScaling->isChecked() && alphaXBRZTextureAlgorithm);
    ui->cb3DTextureScalingSpline36Alpha->setEnabled(
        UsesGL() && ui->cb3DTextureScaling->isChecked() && spline36AlphaTextureAlgorithm);
    ui->cbWholeScene2DScale->setEnabled(UsesGL());
    SetLayoutVisible(ui->horizontalLayoutWholeScene2DScaleMode, wholeSceneEnabled);
    SetLayoutVisible(ui->horizontalLayoutWholeScene2DScaleAlgorithm, wholeSceneEnabled);
    SetLayoutVisible(ui->horizontalLayoutWholeScene2DScaleFragmentationFallback, wholeSceneEnabled);
    SetCompactGridOptions(ui->gridLayoutWholeScene2DScaleOptions, {
        {ui->cbWholeScene2DScaleExactFinalFallback, advancedSettings && wholeSceneEnabled && wholeSceneLegacyMode},
        {ui->cbWholeScene2DScaleForegroundOverlay, advancedSettings && wholeSceneEnabled && wholeSceneLegacyMode},
        {ui->cbWholeScene2DScaleCaptureBacked, advancedSettings && wholeSceneEnabled},
        {ui->cbWholeScene2DScaleSourceBoundaryGuard, advancedSettings && wholeSceneEnabled && wholeSceneLegacyMode &&
                                                       wholeSceneSupportsSourceBoundaryGuard},
        {ui->cbWholeScene2DScaleDebugTint, advancedSettings && wholeSceneEnabled},
        {ui->cbWholeScene2DScaleNoWrapFilterTaps, advancedSettings && wholeSceneEnabled && wholeSceneHighResMode &&
                                                 wholeSceneAlgorithm == melonDS::RendererSettings::GLScaleAlgorithm::Spline36},
        {ui->cbWholeScene2DScaleFinalUpscaleRender3DNative, showFinalUpscaleRender3DNative},
        {ui->cbWholeScene2DScaleFinalUpscale3DCoverageAware, advancedSettings && wholeSceneFinal3DResolve},
        {ui->cbWholeScene2DScaleFinalUpscale3DRepresentativeSemantics, advancedSettings && wholeSceneFinal3DResolve},
        {ui->cbWholeScene2DScaleFinalUpscale3DSplitSemantics, advancedSettings && wholeSceneFinal3DResolve},
        {ui->cbWholeScene2DScaleFinalUpscale3DSharpenSplitCoverage, advancedSettings && wholeSceneSplit3D},
        {ui->cbWholeScene2DScaleOverlayLegacyUnderlay, advancedSettings && wholeSceneEnabled && (wholeSceneOverlayMode || wholeSceneHybridMode)},
        {ui->cbWholeScene2DScaleHybridWindowEdgeAssist, advancedSettings && wholeSceneEnabled && wholeSceneHybridMode},
        {ui->cbWholeScene2DScaleHybridTarget2AlphaBlendAssist, advancedSettings && wholeSceneEnabled && wholeSceneHybridMode},
        {ui->cbWholeScene2DScaleHybridNativeEffectGuard, advancedSettings && wholeSceneEnabled && wholeSceneHybridMode},
        {ui->cbWholeScene2DScaleHybridForeground2DBase, advancedSettings && wholeSceneEnabled && wholeSceneHybridMode},
        {ui->cbWholeScene2DScaleHybridCleanLegacyCandidate, advancedSettings && wholeSceneEnabled && wholeSceneHybridMode},
    }, 3);
    SetLayoutVisible(ui->horizontalLayoutWholeScene2DScaleFinalUpscale3DFilter, advancedSettings && wholeSceneFinal3DResolve);
    ui->cbxWholeScene2DScaleMode->setEnabled(wholeSceneEnabled);
    ui->lblWholeScene2DScaleMode->setEnabled(wholeSceneEnabled);
    ui->cbWholeScene2DScaleSourceBoundaryGuard->setEnabled(wholeSceneEnabled && wholeSceneLegacyMode &&
                                                           wholeSceneSupportsSourceBoundaryGuard);
    ui->cbxWholeScene2DScaleAlgorithm->setEnabled(wholeSceneEnabled);
    ui->lblWholeScene2DScaleAlgorithm->setEnabled(wholeSceneEnabled);
    ui->cbxWholeScene2DScaleFragmentationFallback->setEnabled(wholeSceneEnabled);
    ui->lblWholeScene2DScaleFragmentationFallback->setEnabled(wholeSceneEnabled);
    ui->cbWholeScene2DScaleExactFinalFallback->setEnabled(wholeSceneEnabled && wholeSceneLegacyMode);
    ui->cbWholeScene2DScaleForegroundOverlay->setEnabled(wholeSceneEnabled && wholeSceneLegacyMode);
    ui->cbWholeScene2DScaleCaptureBacked->setEnabled(wholeSceneEnabled);
    ui->cbWholeScene2DScaleDebugTint->setEnabled(wholeSceneEnabled);
    ui->cbWholeScene2DScaleNoWrapFilterTaps->setEnabled(
        wholeSceneEnabled && wholeSceneHighResMode &&
        wholeSceneAlgorithm == melonDS::RendererSettings::GLScaleAlgorithm::Spline36);
    ui->cbWholeScene2DScaleFinalUpscaleRender3DNative->setEnabled(showFinalUpscaleRender3DNative);
    ui->cbWholeScene2DScaleFinalUpscale3DCoverageAware->setEnabled(wholeSceneFinal3DResolve);
    ui->cbWholeScene2DScaleFinalUpscale3DRepresentativeSemantics->setEnabled(wholeSceneFinal3DResolve);
    ui->cbWholeScene2DScaleFinalUpscale3DSplitSemantics->setEnabled(wholeSceneFinal3DResolve);
    ui->cbWholeScene2DScaleFinalUpscale3DSharpenSplitCoverage->setEnabled(wholeSceneSplit3D);
    ui->cbWholeScene2DScaleOverlayLegacyUnderlay->setEnabled(wholeSceneEnabled && (wholeSceneOverlayMode || wholeSceneHybridMode));
    ui->cbWholeScene2DScaleHybridWindowEdgeAssist->setEnabled(wholeSceneEnabled && wholeSceneHybridMode);
    ui->cbWholeScene2DScaleHybridTarget2AlphaBlendAssist->setEnabled(wholeSceneEnabled && wholeSceneHybridMode);
    ui->cbWholeScene2DScaleHybridNativeEffectGuard->setEnabled(wholeSceneEnabled && wholeSceneHybridMode);
    ui->cbWholeScene2DScaleHybridForeground2DBase->setEnabled(wholeSceneEnabled && wholeSceneHybridMode);
    ui->cbWholeScene2DScaleHybridCleanLegacyCandidate->setEnabled(wholeSceneEnabled && wholeSceneHybridMode);
    ui->lblWholeScene2DScaleFinalUpscale3DFilter->setEnabled(wholeSceneFinal3DResolve);
    ui->cbxWholeScene2DScaleFinalUpscale3DFilter->setEnabled(wholeSceneFinal3DResolve);
    ui->cbxComputeHiResCoords->setEnabled(renderer == renderer3D_OpenGLCompute);
    ui->cbGLMSAA->setEnabled(UsesGL());

    ui->scrollAreaWidgetContentsOpenGLRenderer->setMinimumHeight(ui->groupBox_3->sizeHint().height());
}

void VideoSettingsDialog::updateLosslessRGB6RepackForTextureScalingFilteringConflict()
{
    const bool alphaAwareFiltering =
        UsesGL() &&
        (ReadAnisotropyCombo(ui->cbx3DTextureAnisotropy) > 1) &&
        ui->cb3DTextureFilterMipmapAlphaHandling->isChecked();
    const bool conflict = alphaAwareFiltering && ui->cb3DTextureScaling->isChecked();

    if (alphaAwareFiltering)
        ui->cb3DTextureLosslessRGB6Repack->setChecked(!conflict);
}

void VideoSettingsDialog::applyTexture2DAtlasProtection(bool enabled)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterSmart2D", enabled);
    cfg.SetBool("3D.GL.TextureFilterTranslucentGuard", enabled);
    cfg.SetBool("3D.GL.TextureFilterSpriteUVInset", enabled);
    cfg.SetBool("3D.GL.TextureScalingEdgeExtendUnusedMargins", enabled);

    {
        QSignalBlocker blocker(ui->cb3DTextureFilterSmart2D);
        ui->cb3DTextureFilterSmart2D->setChecked(enabled);
    }
    {
        QSignalBlocker blocker(ui->cb3DTextureFilterTranslucentGuard);
        ui->cb3DTextureFilterTranslucentGuard->setChecked(enabled);
    }
    {
        QSignalBlocker blocker(ui->cb3DTextureFilterSpriteUVInset);
        ui->cb3DTextureFilterSpriteUVInset->setChecked(enabled);
    }
    {
        QSignalBlocker blocker(ui->cb3DTextureScalingEdgeExtendUnusedMargins);
        ui->cb3DTextureScalingEdgeExtendUnusedMargins->setChecked(enabled);
    }
    {
        QSignalBlocker blocker(ui->cb3DTexture2DAtlasProtection);
        ui->cb3DTexture2DAtlasProtection->setChecked(enabled);
    }
}

void VideoSettingsDialog::syncTexture2DAtlasProtectionCheckbox()
{
    const int enabledCount =
        static_cast<int>(ui->cb3DTextureFilterSmart2D->isChecked()) +
        static_cast<int>(ui->cb3DTextureFilterTranslucentGuard->isChecked()) +
        static_cast<int>(ui->cb3DTextureFilterSpriteUVInset->isChecked()) +
        static_cast<int>(ui->cb3DTextureScalingEdgeExtendUnusedMargins->isChecked());
    const Qt::CheckState state =
        enabledCount == 0 ? Qt::Unchecked :
        enabledCount == 4 ? Qt::Checked :
        Qt::PartiallyChecked;

    QSignalBlocker blocker(ui->cb3DTexture2DAtlasProtection);
    ui->cb3DTexture2DAtlasProtection->setCheckState(state);
}

VideoSettingsDialog::VideoSettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::VideoSettingsDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    ui->cb3DTexture2DAtlasProtection->setTristate(true);

    QPushButton* whatsThisButton = ui->buttonBox->addButton("?", QDialogButtonBox::HelpRole);
    whatsThisButton->setAutoDefault(false);
    whatsThisButton->setDefault(false);
    whatsThisButton->setToolTip(tr("What's This?"));
    whatsThisButton->setWhatsThis(tr("Click this button, then click a video setting to show what it does."));
    connect(whatsThisButton, &QPushButton::clicked, this, [] { QWhatsThis::enterWhatsThisMode(); });

    CopyWhatsThis(ui->label_3, ui->cbxGLResolution);
    CopyWhatsThis(ui->lbl3DTextureAnisotropy, ui->cbx3DTextureAnisotropy);
    CopyWhatsThis(ui->lbl3DTextureFilterMipDepth, ui->cbx3DTextureFilterMipDepth);
    CopyWhatsThis(ui->lbl3DTextureScalingAlgorithm, ui->cbx3DTextureScalingAlgorithm);
    CopyWhatsThis(ui->lblWholeScene2DScaleMode, ui->cbxWholeScene2DScaleMode);
    CopyWhatsThis(ui->lblWholeScene2DScaleAlgorithm, ui->cbxWholeScene2DScaleAlgorithm);
    CopyWhatsThis(ui->lblWholeScene2DScaleFinalUpscale3DFilter, ui->cbxWholeScene2DScaleFinalUpscale3DFilter);
    CopyWhatsThis(ui->lblWholeScene2DScaleFragmentationFallback, ui->cbxWholeScene2DScaleFragmentationFallback);

    emuInstance = ((MainWindow*)parent)->getEmuInstance();
    auto& cfg = emuInstance->getGlobalConfig();
    oldAdvancedVideoSettings = cfg.GetBool("3D.GL.AdvancedVideoSettings");
    {
        QSignalBlocker blocker(ui->cbAdvancedVideoSettings);
        ui->cbAdvancedVideoSettings->setChecked(oldAdvancedVideoSettings != 0);
    }

    PopulateScaleAlgorithmCombo(ui->cbx3DTextureScalingAlgorithm, {
        GLScaleAlgorithm::Spline36,
        GLScaleAlgorithm::XBRZ,
        GLScaleAlgorithm::ArtCNN,
        GLScaleAlgorithm::ArtCNNDN,
        GLScaleAlgorithm::NNEDI3,
        GLScaleAlgorithm::CuNNy4x32,
    });
    PopulateAnisotropyCombo(ui->cbx3DTextureAnisotropy);
    PopulateTextureFilterMipDepthCombo(ui->cbx3DTextureFilterMipDepth);
    PopulateWholeScene2DScaleModeCombo(ui->cbxWholeScene2DScaleMode,
                                       ui->cbAdvancedVideoSettings->isChecked(),
                                       melonDS::RendererSettings::GetWholeScene2DScaleModeIndex(
                                           WholeScene2DScaleMode::ConservativeHybridUpscale));
    PopulateWholeScene2DFragmentationFallbackCombo(ui->cbxWholeScene2DScaleFragmentationFallback);
    PopulateFinalUpscale3DFilterCombo(ui->cbxWholeScene2DScaleFinalUpscale3DFilter);
    PopulateScaleAlgorithmCombo(ui->cbxWholeScene2DScaleAlgorithm, {
        GLScaleAlgorithm::Spline36,
        GLScaleAlgorithm::XBRZ,
        GLScaleAlgorithm::ArtCNN,
        GLScaleAlgorithm::ArtCNNDN,
        GLScaleAlgorithm::NNEDI3,
        GLScaleAlgorithm::CuNNy4x32,
    });

    connect(ui->btnRecommendedDefaults, &QPushButton::clicked,
            this, &VideoSettingsDialog::applyRecommendedDefaults);

    oldRenderer = cfg.GetInt("3D.Renderer");
    oldGLDisplay = cfg.GetBool("Screen.UseGL");
    oldVSync = cfg.GetBool("Screen.VSync");
    oldVSyncInterval = cfg.GetInt("Screen.VSyncInterval");
    oldSoftThreaded = cfg.GetBool("3D.Soft.Threaded");
    oldGLScale = cfg.GetInt("3D.GL.ScaleFactor");
    oldGLBetterPolygons = cfg.GetBool("3D.GL.BetterPolygons");
    oldReadable3DTextureCache = cfg.GetBool("3D.GL.ReadableTextureCache");
    oldHighPrecisionTextureCoordinates = cfg.GetBool("3D.GL.TextureScalingHighPrecisionCoordinates");
    oldTextureFilter.Anisotropy = cfg.GetInt("3D.GL.TextureAnisotropy");
    oldTextureFilter.BinaryAlphaHandling = cfg.GetBool("3D.GL.TextureFilterBinaryAlphaHandling");
    oldTextureFilter.TopologyAwareMipHandling = cfg.GetBool("3D.GL.TextureFilterMipmapPremultipliedAlphaHandling");
    oldTextureFilter.MipmapSubrectHandling = cfg.GetBool("3D.GL.TextureFilterMipmapSubrectHandling");
    oldTextureFilter.Smart2DFiltering = cfg.GetBool("3D.GL.TextureFilterSmart2D");
    oldTextureFilter.TranslucentTextureFilteringGuard = cfg.GetBool("3D.GL.TextureFilterTranslucentGuard");
    oldTextureFilter.SpriteUVInset = cfg.GetBool("3D.GL.TextureFilterSpriteUVInset");
    oldTextureFilter.MipmapAlphaHandling = cfg.GetBool("3D.GL.TextureFilterMipmapAlphaHandling");
    oldTextureFilter.MipDepth = melonDS::RendererSettings::GetTextureFilterMipDepthIndex(
        melonDS::RendererSettings::GetTextureFilterMipDepth(cfg.GetInt("3D.GL.TextureFilterMipDepth")));
    oldTextureFilter.LosslessRGB6Repack = cfg.GetBool("3D.GL.TextureLosslessRGB6Repack");
    oldTextureScaling.Enabled = cfg.GetBool("3D.GL.TextureScaling");
    oldTextureScaling.Algorithm = ReadScaleAlgorithmConfig(cfg, "3D.GL.TextureScalingAlgorithm", true);
    oldTextureScaling.FrequentChangePolicy = cfg.GetBool("3D.GL.TextureScalingFrequentChangePolicy");
    oldTextureScaling.Deferred = cfg.GetBool("3D.GL.TextureScalingDeferred");
    oldTextureScaling.NativeMipFloor = cfg.GetBool("3D.GL.TextureScalingNativeMipFloor");
    oldTextureScaling.SourceMips = cfg.GetBool("3D.GL.TextureScalingSourceMips");
    oldTextureScaling.EdgeExtendUnusedMargins = cfg.GetBool("3D.GL.TextureScalingEdgeExtendUnusedMargins");
    oldTextureScaling.LegacyAlphaHandling = cfg.GetBool("3D.GL.TextureScalingLegacyAlphaHandling");
    oldTextureScaling.QualityAlphaHandling = cfg.GetBool("3D.GL.TextureScalingQualityAlphaHandling");
    oldTextureScaling.AlphaXBRZ = cfg.GetBool("3D.GL.TextureScalingAlphaXBRZ");
    oldTextureScaling.Spline36Alpha = cfg.GetBool("3D.GL.TextureScalingSpline36Alpha");
    oldWholeScene2D.Enabled = cfg.GetBool("3D.GL.WholeScene2DScale");
    oldWholeScene2D.SourceBoundaryGuard = cfg.GetBool("3D.GL.WholeScene2DScaleSourceBoundaryGuard");
    oldWholeScene2D.Mode = melonDS::RendererSettings::GetWholeScene2DScaleModeIndex(
        melonDS::RendererSettings::GetWholeScene2DScaleMode(cfg.GetInt("3D.GL.WholeScene2DScaleMode")));
    oldWholeScene2D.Algorithm = ReadScaleAlgorithmConfig(cfg, "3D.GL.WholeScene2DScaleAlgorithm", true);
    oldWholeScene2D.FragmentationFallback = melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(
        melonDS::RendererSettings::GetWholeScene2DFragmentationFallback(cfg.GetInt("3D.GL.WholeScene2DScaleFragmentationFallback")));
    oldWholeScene2D.ExactFinalFallback = cfg.GetBool("3D.GL.WholeScene2DScaleExactFinalFallback");
    oldWholeScene2D.ForegroundOverlay = cfg.GetBool("3D.GL.WholeScene2DScaleForegroundOverlay");
    oldWholeScene2D.CaptureBacked = cfg.GetBool("3D.GL.WholeScene2DScaleCaptureBacked");
    oldWholeScene2D.DebugTint = cfg.GetBool("3D.GL.WholeScene2DScaleDebugTint");
    oldWholeScene2D.NoWrapFilterTaps = cfg.GetBool("3D.GL.WholeScene2DScaleNoWrapFilterTaps");
    oldWholeScene2D.FinalUpscaleRender3DNative = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscaleRender3DNative");
    oldWholeScene2D.FinalUpscale3DFilter = melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilterIndex(
        melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilter(cfg.GetInt("3D.GL.WholeScene2DScaleFinalUpscale3DFilter")));
    oldWholeScene2D.FinalUpscale3DCoverageAware = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DCoverageAware");
    oldWholeScene2D.FinalUpscale3DRepresentativeSemantics = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DRepresentativeSemantics");
    oldWholeScene2D.FinalUpscale3DSplitSemantics = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSplitSemantics");
    oldWholeScene2D.FinalUpscale3DSharpenSplitCoverage = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSharpenSplitCoverage");
    oldWholeScene2D.OverlayLegacyUnderlay = cfg.GetBool("3D.GL.WholeScene2DScaleOverlayLegacyUnderlay");
    oldWholeScene2D.HybridWindowEdgeAssist = cfg.GetBool("3D.GL.WholeScene2DScaleHybridWindowEdgeAssist");
    oldWholeScene2D.HybridTarget2AlphaBlendAssist = cfg.GetBool("3D.GL.WholeScene2DScaleHybridTarget2AlphaBlendAssist");
    oldWholeScene2D.HybridNativeEffectGuard = cfg.GetBool("3D.GL.WholeScene2DScaleHybridNativeEffectGuard");
    oldWholeScene2D.HybridForeground2DBase = cfg.GetBool("3D.GL.WholeScene2DScaleHybridForeground2DBase");
    oldWholeScene2D.HybridCleanLegacyCandidate = cfg.GetBool("3D.GL.WholeScene2DScaleHybridCleanLegacyCandidate");
    oldHiresCoordinates = cfg.GetBool("3D.GL.HiresCoordinates");
    oldGLMSAA = cfg.GetBool("3D.GL.MSAA");

    grp3DRenderer = new QButtonGroup(this);
    grp3DRenderer->addButton(ui->rb3DSoftware, renderer3D_Software);
    grp3DRenderer->addButton(ui->rb3DOpenGL,   renderer3D_OpenGL);
    grp3DRenderer->addButton(ui->rb3DCompute,  renderer3D_OpenGLCompute);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    connect(grp3DRenderer, SIGNAL(buttonClicked(int)), this, SLOT(onChange3DRenderer(int)));
#else
    connect(grp3DRenderer, SIGNAL(idClicked(int)), this, SLOT(onChange3DRenderer(int)));
#endif
    grp3DRenderer->button(oldRenderer)->setChecked(true);

#ifndef OGLRENDERER_ENABLED
    ui->rb3DOpenGL->setEnabled(false);
#endif

#ifdef __APPLE__
    ui->rb3DCompute->setEnabled(false);
#endif

    ui->cbGLDisplay->setChecked(oldGLDisplay != 0);

    ui->cbVSync->setChecked(oldVSync != 0);
    ui->sbVSyncInterval->setValue(oldVSyncInterval);

    ui->cbSoftwareThreaded->setChecked(oldSoftThreaded);

    for (int i = 1; i <= 16; i++)
        ui->cbxGLResolution->addItem(QString("%1x native (%2x%3)").arg(i).arg(256*i).arg(192*i));
    ui->cbxGLResolution->setCurrentIndex(oldGLScale-1);

    ui->cbBetterPolygons->setChecked(oldGLBetterPolygons != 0);
    ui->cbReadable3DTextureCache->setChecked(oldReadable3DTextureCache != 0);
    SetAnisotropyCombo(ui->cbx3DTextureAnisotropy, oldTextureFilter.Anisotropy);
    ui->cb3DTextureFilterBinaryAlphaHandling->setChecked(oldTextureFilter.BinaryAlphaHandling != 0);
    ui->cb3DTextureFilterMipmapTopologyHandling->setChecked(oldTextureFilter.TopologyAwareMipHandling != 0);
    ui->cb3DTextureFilterMipmapSubrectHandling->setChecked(oldTextureFilter.MipmapSubrectHandling != 0);
    ui->cb3DTextureFilterSmart2D->setChecked(oldTextureFilter.Smart2DFiltering != 0);
    ui->cb3DTextureFilterTranslucentGuard->setChecked(oldTextureFilter.TranslucentTextureFilteringGuard != 0);
    ui->cb3DTextureFilterSpriteUVInset->setChecked(oldTextureFilter.SpriteUVInset != 0);
    ui->cb3DTextureFilterMipmapAlphaHandling->setChecked(oldTextureFilter.MipmapAlphaHandling != 0);
    SetTextureFilterMipDepthCombo(ui->cbx3DTextureFilterMipDepth, oldTextureFilter.MipDepth);
    ui->cb3DTextureLosslessRGB6Repack->setChecked(oldTextureFilter.LosslessRGB6Repack != 0);
    {
        // Loading a saved enabled state is not the same as the user turning
        // texture scaling on. Preserve an explicit lossless-repack override
        // across dialog reopenings; the state-change handler still applies the
        // recommended unchecked state on a real off-to-on interaction.
        QSignalBlocker blocker(ui->cb3DTextureScaling);
        ui->cb3DTextureScaling->setChecked(oldTextureScaling.Enabled != 0);
    }
    ui->cb3DHighPrecisionTextureCoordinates->setChecked(
        oldHighPrecisionTextureCoordinates != 0);
    SetScaleAlgorithmCombo(ui->cbx3DTextureScalingAlgorithm, oldTextureScaling.Algorithm);
    ui->cb3DTextureScalingFrequentChangePolicy->setChecked(oldTextureScaling.FrequentChangePolicy != 0);
    ui->cb3DTextureScalingDeferred->setChecked(oldTextureScaling.Deferred != 0);
    ui->cb3DTextureScalingNativeMipFloor->setChecked(oldTextureScaling.NativeMipFloor != 0);
    ui->cb3DTextureScalingSourceMips->setChecked(oldTextureScaling.SourceMips != 0);
    ui->cb3DTextureScalingEdgeExtendUnusedMargins->setChecked(oldTextureScaling.EdgeExtendUnusedMargins != 0);
    ui->cb3DTextureScalingLegacyAlphaHandling->setChecked(oldTextureScaling.LegacyAlphaHandling != 0);
    ui->cb3DTextureScalingQualityAlphaHandling->setChecked(oldTextureScaling.QualityAlphaHandling != 0);
    ui->cb3DTextureScalingAlphaXBRZ->setChecked(oldTextureScaling.AlphaXBRZ != 0);
    ui->cb3DTextureScalingSpline36Alpha->setChecked(oldTextureScaling.Spline36Alpha != 0);
    syncTexture2DAtlasProtectionCheckbox();
    ui->cbWholeScene2DScale->setChecked(oldWholeScene2D.Enabled != 0);
    ui->cbWholeScene2DScaleSourceBoundaryGuard->setChecked(oldWholeScene2D.SourceBoundaryGuard != 0);
    SetWholeScene2DScaleModeCombo(ui->cbxWholeScene2DScaleMode, oldWholeScene2D.Mode);
    SetScaleAlgorithmCombo(ui->cbxWholeScene2DScaleAlgorithm, oldWholeScene2D.Algorithm);
    SetWholeScene2DFragmentationFallbackCombo(ui->cbxWholeScene2DScaleFragmentationFallback, oldWholeScene2D.FragmentationFallback);
    ui->cbWholeScene2DScaleExactFinalFallback->setChecked(oldWholeScene2D.ExactFinalFallback != 0);
    ui->cbWholeScene2DScaleForegroundOverlay->setChecked(oldWholeScene2D.ForegroundOverlay != 0);
    ui->cbWholeScene2DScaleCaptureBacked->setChecked(oldWholeScene2D.CaptureBacked != 0);
    ui->cbWholeScene2DScaleDebugTint->setChecked(oldWholeScene2D.DebugTint != 0);
    ui->cbWholeScene2DScaleNoWrapFilterTaps->setChecked(oldWholeScene2D.NoWrapFilterTaps != 0);
    ui->cbWholeScene2DScaleFinalUpscaleRender3DNative->setChecked(oldWholeScene2D.FinalUpscaleRender3DNative != 0);
    ui->cbWholeScene2DScaleFinalUpscale3DCoverageAware->setChecked(oldWholeScene2D.FinalUpscale3DCoverageAware != 0);
    ui->cbWholeScene2DScaleFinalUpscale3DRepresentativeSemantics->setChecked(oldWholeScene2D.FinalUpscale3DRepresentativeSemantics != 0);
    ui->cbWholeScene2DScaleFinalUpscale3DSplitSemantics->setChecked(oldWholeScene2D.FinalUpscale3DSplitSemantics != 0);
    ui->cbWholeScene2DScaleFinalUpscale3DSharpenSplitCoverage->setChecked(oldWholeScene2D.FinalUpscale3DSharpenSplitCoverage != 0);
    ui->cbWholeScene2DScaleOverlayLegacyUnderlay->setChecked(oldWholeScene2D.OverlayLegacyUnderlay != 0);
    ui->cbWholeScene2DScaleHybridWindowEdgeAssist->setChecked(oldWholeScene2D.HybridWindowEdgeAssist != 0);
    ui->cbWholeScene2DScaleHybridTarget2AlphaBlendAssist->setChecked(oldWholeScene2D.HybridTarget2AlphaBlendAssist != 0);
    ui->cbWholeScene2DScaleHybridNativeEffectGuard->setChecked(oldWholeScene2D.HybridNativeEffectGuard != 0);
    ui->cbWholeScene2DScaleHybridForeground2DBase->setChecked(oldWholeScene2D.HybridForeground2DBase != 0);
    ui->cbWholeScene2DScaleHybridCleanLegacyCandidate->setChecked(oldWholeScene2D.HybridCleanLegacyCandidate != 0);
    SetFinalUpscale3DFilterCombo(ui->cbxWholeScene2DScaleFinalUpscale3DFilter, oldWholeScene2D.FinalUpscale3DFilter);
    ui->cbxComputeHiResCoords->setChecked(oldHiresCoordinates != 0);
    ui->cbGLMSAA->setChecked(oldGLMSAA != 0);

    if (!oldVSync)
        ui->sbVSyncInterval->setEnabled(false);
    setVsyncControlEnable(UsesGL());

    setEnabled();
    resize(1066, 835);
}

VideoSettingsDialog::~VideoSettingsDialog()
{
    delete ui;
}

void VideoSettingsDialog::on_VideoSettingsDialog_accepted()
{
    Config::Save();

    closeDlg();
}

void VideoSettingsDialog::on_VideoSettingsDialog_rejected()
{
    if (!((MainWindow*)parent())->getEmuInstance())
    {
        closeDlg();
        return;
    }

    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.Renderer", oldRenderer);
    cfg.SetBool("Screen.UseGL", oldGLDisplay);
    cfg.SetBool("Screen.VSync", oldVSync);
    cfg.SetInt("Screen.VSyncInterval", oldVSyncInterval);
    cfg.SetBool("3D.Soft.Threaded", oldSoftThreaded);
    cfg.SetInt("3D.GL.ScaleFactor", oldGLScale);
    cfg.SetBool("3D.GL.BetterPolygons", oldGLBetterPolygons);
    cfg.SetBool("3D.GL.ReadableTextureCache", oldReadable3DTextureCache);
    cfg.SetBool("3D.GL.TextureScalingHighPrecisionCoordinates", oldHighPrecisionTextureCoordinates);
    cfg.SetBool("3D.GL.AdvancedVideoSettings", oldAdvancedVideoSettings);
    cfg.SetInt("3D.GL.TextureAnisotropy", oldTextureFilter.Anisotropy);
    cfg.SetBool("3D.GL.TextureFilterBinaryAlphaHandling", oldTextureFilter.BinaryAlphaHandling);
    cfg.SetBool("3D.GL.TextureFilterMipmapPremultipliedAlphaHandling", oldTextureFilter.TopologyAwareMipHandling);
    cfg.SetBool("3D.GL.TextureFilterMipmapSubrectHandling", oldTextureFilter.MipmapSubrectHandling);
    cfg.SetBool("3D.GL.TextureFilterSmart2D", oldTextureFilter.Smart2DFiltering);
    cfg.SetBool("3D.GL.TextureFilterTranslucentGuard", oldTextureFilter.TranslucentTextureFilteringGuard);
    cfg.SetBool("3D.GL.TextureFilterSpriteUVInset", oldTextureFilter.SpriteUVInset);
    cfg.SetBool("3D.GL.TextureFilterMipmapAlphaHandling", oldTextureFilter.MipmapAlphaHandling);
    cfg.SetInt("3D.GL.TextureFilterMipDepth", oldTextureFilter.MipDepth);
    cfg.SetBool("3D.GL.TextureLosslessRGB6Repack", oldTextureFilter.LosslessRGB6Repack);
    cfg.SetBool("3D.GL.TextureScaling", oldTextureScaling.Enabled);
    WriteScaleAlgorithmConfig(cfg, "3D.GL.TextureScalingAlgorithm", oldTextureScaling.Algorithm, true);
    cfg.SetBool("3D.GL.TextureScalingFrequentChangePolicy", oldTextureScaling.FrequentChangePolicy);
    cfg.SetBool("3D.GL.TextureScalingDeferred", oldTextureScaling.Deferred);
    cfg.SetBool("3D.GL.TextureScalingNativeMipFloor", oldTextureScaling.NativeMipFloor);
    cfg.SetBool("3D.GL.TextureScalingSourceMips", oldTextureScaling.SourceMips);
    cfg.SetBool("3D.GL.TextureScalingEdgeExtendUnusedMargins", oldTextureScaling.EdgeExtendUnusedMargins);
    cfg.SetBool("3D.GL.TextureScalingLegacyAlphaHandling", oldTextureScaling.LegacyAlphaHandling);
    cfg.SetBool("3D.GL.TextureScalingQualityAlphaHandling", oldTextureScaling.QualityAlphaHandling);
    cfg.SetBool("3D.GL.TextureScalingAlphaXBRZ", oldTextureScaling.AlphaXBRZ);
    cfg.SetBool("3D.GL.TextureScalingSpline36Alpha", oldTextureScaling.Spline36Alpha);
    cfg.SetBool("3D.GL.WholeScene2DScale", oldWholeScene2D.Enabled);
    cfg.SetBool("3D.GL.WholeScene2DScaleSourceBoundaryGuard", oldWholeScene2D.SourceBoundaryGuard);
    cfg.SetInt("3D.GL.WholeScene2DScaleMode", oldWholeScene2D.Mode);
    WriteScaleAlgorithmConfig(cfg, "3D.GL.WholeScene2DScaleAlgorithm", oldWholeScene2D.Algorithm, true);
    cfg.SetInt("3D.GL.WholeScene2DScaleFragmentationFallback", oldWholeScene2D.FragmentationFallback);
    cfg.SetBool("3D.GL.WholeScene2DScaleExactFinalFallback", oldWholeScene2D.ExactFinalFallback);
    cfg.SetBool("3D.GL.WholeScene2DScaleForegroundOverlay", oldWholeScene2D.ForegroundOverlay);
    cfg.SetBool("3D.GL.WholeScene2DScaleCaptureBacked", oldWholeScene2D.CaptureBacked);
    cfg.SetBool("3D.GL.WholeScene2DScaleDebugTint", oldWholeScene2D.DebugTint);
    cfg.SetBool("3D.GL.WholeScene2DScaleNoWrapFilterTaps", oldWholeScene2D.NoWrapFilterTaps);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscaleRender3DNative", oldWholeScene2D.FinalUpscaleRender3DNative);
    cfg.SetInt("3D.GL.WholeScene2DScaleFinalUpscale3DFilter", oldWholeScene2D.FinalUpscale3DFilter);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DCoverageAware", oldWholeScene2D.FinalUpscale3DCoverageAware);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DRepresentativeSemantics", oldWholeScene2D.FinalUpscale3DRepresentativeSemantics);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSplitSemantics", oldWholeScene2D.FinalUpscale3DSplitSemantics);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSharpenSplitCoverage", oldWholeScene2D.FinalUpscale3DSharpenSplitCoverage);
    cfg.SetBool("3D.GL.WholeScene2DScaleOverlayLegacyUnderlay", oldWholeScene2D.OverlayLegacyUnderlay);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridWindowEdgeAssist", oldWholeScene2D.HybridWindowEdgeAssist);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridTarget2AlphaBlendAssist", oldWholeScene2D.HybridTarget2AlphaBlendAssist);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridNativeEffectGuard", oldWholeScene2D.HybridNativeEffectGuard);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridForeground2DBase", oldWholeScene2D.HybridForeground2DBase);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridCleanLegacyCandidate", oldWholeScene2D.HybridCleanLegacyCandidate);
    cfg.SetBool("3D.GL.HiresCoordinates", oldHiresCoordinates);
    cfg.SetBool("3D.GL.MSAA", oldGLMSAA);

    emit updateVideoSettings(old_gl != UsesGL());

    closeDlg();
}

void VideoSettingsDialog::setVsyncControlEnable(bool hasOGL)
{
    ui->cbVSync->setEnabled(hasOGL);
    ui->sbVSyncInterval->setEnabled(hasOGL);
}

void VideoSettingsDialog::applyRecommendedDefaults()
{
    const bool textureFiltering = ReadAnisotropyCombo(ui->cbx3DTextureAnisotropy) > 1;
    const bool textureScaling = ui->cb3DTextureScaling->isChecked();
    const bool wholeScene2DScaling = ui->cbWholeScene2DScale->isChecked();

    auto& cfg = emuInstance->getGlobalConfig();
    const int renderer = cfg.GetInt("3D.Renderer");
    const bool classicRenderer = renderer == renderer3D_OpenGL;
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
    cfg.SetBool("3D.GL.ReadableTextureCache", false);
    cfg.SetBool("3D.GL.HiresCoordinates", true);
    cfg.SetBool("3D.GL.MSAA", false);

    if (textureFiltering)
        cfg.SetInt("3D.GL.TextureAnisotropy", 16);
    cfg.SetInt("3D.GL.TextureFilterMipDepth",
               melonDS::RendererSettings::GetTextureFilterMipDepthIndex(TextureFilterMipDepth::Min32));
    cfg.SetBool("3D.GL.TextureFilterBinaryAlphaHandling", true);
    cfg.SetBool("3D.GL.TextureFilterMipmapAlphaHandling", true);
    cfg.SetBool("3D.GL.TextureFilterMipmapPremultipliedAlphaHandling", false);
    cfg.SetBool("3D.GL.TextureFilterMipmapSubrectHandling", false);
    cfg.SetBool("3D.GL.TextureLosslessRGB6Repack", !textureScaling);

    cfg.SetBool("3D.GL.TextureScaling", textureScaling);
    cfg.SetBool("3D.GL.TextureScalingLegacyAlphaHandling", false);
    cfg.SetBool("3D.GL.TextureScalingQualityAlphaHandling", false);
    cfg.SetBool("3D.GL.TextureScalingAlphaXBRZ", false);
    cfg.SetBool("3D.GL.TextureScalingSpline36Alpha", false);
    cfg.SetBool("3D.GL.TextureScalingFrequentChangePolicy", true);
    cfg.SetBool("3D.GL.TextureScalingDeferred", false);
    cfg.SetBool("3D.GL.TextureScalingNativeMipFloor", false);
    cfg.SetBool("3D.GL.TextureScalingSourceMips", false);

    cfg.SetBool("3D.GL.WholeScene2DScale", wholeScene2DScaling);
    cfg.SetBool("3D.GL.WholeScene2DScaleSourceBoundaryGuard", false);
    cfg.SetInt("3D.GL.WholeScene2DScaleMode",
               melonDS::RendererSettings::GetWholeScene2DScaleModeIndex(
                   WholeScene2DScaleMode::ConservativeHybridUpscale));
    cfg.SetInt("3D.GL.WholeScene2DScaleFragmentationFallback",
               melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(
                   WholeScene2DFragmentationFallback::Off));
    cfg.SetBool("3D.GL.WholeScene2DScaleExactFinalFallback", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleForegroundOverlay", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleCaptureBacked", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleDebugTint", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleNoWrapFilterTaps", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscaleRender3DNative", false);
    cfg.SetInt("3D.GL.WholeScene2DScaleFinalUpscale3DFilter",
               melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilterIndex(
                   FinalUpscale3DDownsampleFilter::Area));
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DCoverageAware", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DRepresentativeSemantics", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSplitSemantics", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSharpenSplitCoverage", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleOverlayLegacyUnderlay", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridWindowEdgeAssist", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridTarget2AlphaBlendAssist", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridNativeEffectGuard", false);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridForeground2DBase", true);
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridCleanLegacyCandidate", true);

    auto setChecked = [](auto* widget, bool checked)
    {
        QSignalBlocker blocker(widget);
        widget->setChecked(checked);
    };
    auto setComboIndex = [](QComboBox* combo, int index)
    {
        QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    };

    setComboIndex(ui->cbxGLResolution, 3);
    setChecked(ui->cbBetterPolygons, true);
    setChecked(ui->cbReadable3DTextureCache, false);
    setChecked(ui->cbxComputeHiResCoords, true);
    setChecked(ui->cbGLMSAA, false);

    if (textureFiltering)
    {
        QSignalBlocker blocker(ui->cbx3DTextureAnisotropy);
        SetAnisotropyCombo(ui->cbx3DTextureAnisotropy, 16);
    }
    {
        QSignalBlocker blocker(ui->cbx3DTextureFilterMipDepth);
        SetTextureFilterMipDepthCombo(
            ui->cbx3DTextureFilterMipDepth,
            melonDS::RendererSettings::GetTextureFilterMipDepthIndex(TextureFilterMipDepth::Min32));
    }
    setChecked(ui->cb3DTextureFilterBinaryAlphaHandling, true);
    setChecked(ui->cb3DTextureFilterMipmapAlphaHandling, true);
    setChecked(ui->cb3DTextureFilterMipmapTopologyHandling, false);
    setChecked(ui->cb3DTextureFilterMipmapSubrectHandling, false);
    setChecked(ui->cb3DTextureLosslessRGB6Repack, !textureScaling);

    setChecked(ui->cb3DTextureScaling, textureScaling);
    setChecked(ui->cb3DTextureScalingLegacyAlphaHandling, false);
    setChecked(ui->cb3DTextureScalingQualityAlphaHandling, false);
    setChecked(ui->cb3DTextureScalingAlphaXBRZ, false);
    setChecked(ui->cb3DTextureScalingSpline36Alpha, false);
    setChecked(ui->cb3DTextureScalingFrequentChangePolicy, true);
    setChecked(ui->cb3DTextureScalingDeferred, false);
    setChecked(ui->cb3DTextureScalingNativeMipFloor, false);
    setChecked(ui->cb3DTextureScalingSourceMips, false);

    setChecked(ui->cbWholeScene2DScale, wholeScene2DScaling);
    setChecked(ui->cbWholeScene2DScaleSourceBoundaryGuard, false);
    {
        QSignalBlocker blocker(ui->cbxWholeScene2DScaleMode);
        SetWholeScene2DScaleModeCombo(
            ui->cbxWholeScene2DScaleMode,
            melonDS::RendererSettings::GetWholeScene2DScaleModeIndex(
                WholeScene2DScaleMode::ConservativeHybridUpscale));
    }
    {
        QSignalBlocker blocker(ui->cbxWholeScene2DScaleFragmentationFallback);
        SetWholeScene2DFragmentationFallbackCombo(
            ui->cbxWholeScene2DScaleFragmentationFallback,
            melonDS::RendererSettings::GetWholeScene2DFragmentationFallbackIndex(
                WholeScene2DFragmentationFallback::Off));
    }
    setChecked(ui->cbWholeScene2DScaleExactFinalFallback, false);
    setChecked(ui->cbWholeScene2DScaleForegroundOverlay, false);
    setChecked(ui->cbWholeScene2DScaleCaptureBacked, true);
    setChecked(ui->cbWholeScene2DScaleDebugTint, false);
    setChecked(ui->cbWholeScene2DScaleNoWrapFilterTaps, true);
    setChecked(ui->cbWholeScene2DScaleFinalUpscaleRender3DNative, false);
    {
        QSignalBlocker blocker(ui->cbxWholeScene2DScaleFinalUpscale3DFilter);
        SetFinalUpscale3DFilterCombo(
            ui->cbxWholeScene2DScaleFinalUpscale3DFilter,
            melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilterIndex(
                FinalUpscale3DDownsampleFilter::Area));
    }
    setChecked(ui->cbWholeScene2DScaleFinalUpscale3DCoverageAware, true);
    setChecked(ui->cbWholeScene2DScaleFinalUpscale3DRepresentativeSemantics, false);
    setChecked(ui->cbWholeScene2DScaleFinalUpscale3DSplitSemantics, true);
    setChecked(ui->cbWholeScene2DScaleFinalUpscale3DSharpenSplitCoverage, false);
    setChecked(ui->cbWholeScene2DScaleOverlayLegacyUnderlay, false);
    setChecked(ui->cbWholeScene2DScaleHybridWindowEdgeAssist, true);
    setChecked(ui->cbWholeScene2DScaleHybridTarget2AlphaBlendAssist, true);
    setChecked(ui->cbWholeScene2DScaleHybridNativeEffectGuard, false);
    setChecked(ui->cbWholeScene2DScaleHybridForeground2DBase, true);
    setChecked(ui->cbWholeScene2DScaleHybridCleanLegacyCandidate, true);

    setVsyncControlEnable(UsesGL());
    setEnabled();
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::onChange3DRenderer(int renderer)
{
    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.Renderer", renderer);

    setEnabled();

    emit updateVideoSettings(old_gl != UsesGL());
}

void VideoSettingsDialog::on_cbGLDisplay_stateChanged(int state)
{
    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", (state != 0));

    setVsyncControlEnable(UsesGL());

    emit updateVideoSettings(old_gl != UsesGL());
}

void VideoSettingsDialog::on_cbVSync_stateChanged(int state)
{
    bool vsync = (state != 0);
    ui->sbVSyncInterval->setEnabled(vsync);

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.VSync", vsync);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_sbVSyncInterval_valueChanged(int val)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("Screen.VSyncInterval", val);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbSoftwareThreaded_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.Soft.Threaded", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxGLResolution_currentIndexChanged(int idx)
{
    // prevent a spurious change
    if (ui->cbxGLResolution->count() < 16) return;

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.ScaleFactor", idx+1);

    setVsyncControlEnable(UsesGL());
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbBetterPolygons_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.BetterPolygons", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbReadable3DTextureCache_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.ReadableTextureCache", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbx3DTextureAnisotropy_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.TextureAnisotropy", ReadAnisotropyCombo(ui->cbx3DTextureAnisotropy));
    updateLosslessRGB6RepackForTextureScalingFilteringConflict();
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterMipmapAlphaHandling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterMipmapAlphaHandling", (state != 0));
    updateLosslessRGB6RepackForTextureScalingFilteringConflict();
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterMipmapTopologyHandling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterMipmapPremultipliedAlphaHandling", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterMipmapSubrectHandling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterMipmapSubrectHandling", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTexture2DAtlasProtection_stateChanged(int state)
{
    applyTexture2DAtlasProtection(state != 0);
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterSmart2D_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterSmart2D", (state != 0));
    syncTexture2DAtlasProtectionCheckbox();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterTranslucentGuard_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterTranslucentGuard", (state != 0));
    syncTexture2DAtlasProtectionCheckbox();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterSpriteUVInset_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterSpriteUVInset", (state != 0));
    syncTexture2DAtlasProtectionCheckbox();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureLosslessRGB6Repack_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureLosslessRGB6Repack", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureFilterBinaryAlphaHandling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureFilterBinaryAlphaHandling", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbx3DTextureFilterMipDepth_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.TextureFilterMipDepth", ReadTextureFilterMipDepthCombo(ui->cbx3DTextureFilterMipDepth));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScaling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScaling", (state != 0));
    updateLosslessRGB6RepackForTextureScalingFilteringConflict();
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DHighPrecisionTextureCoordinates_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingHighPrecisionCoordinates", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbx3DTextureScalingAlgorithm_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    WriteScaleAlgorithmConfig(cfg, "3D.GL.TextureScalingAlgorithm",
                              ReadScaleAlgorithmCombo(ui->cbx3DTextureScalingAlgorithm), true);
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingLegacyAlphaHandling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingLegacyAlphaHandling", (state != 0));
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingQualityAlphaHandling_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingQualityAlphaHandling", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingAlphaXBRZ_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingAlphaXBRZ", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingSpline36Alpha_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingSpline36Alpha", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingDeferred_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingDeferred", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingNativeMipFloor_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingNativeMipFloor", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingSourceMips_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingSourceMips", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingEdgeExtendUnusedMargins_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingEdgeExtendUnusedMargins", (state != 0));
    syncTexture2DAtlasProtectionCheckbox();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cb3DTextureScalingFrequentChangePolicy_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.TextureScalingFrequentChangePolicy", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScale_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScale", (state != 0));
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleSourceBoundaryGuard_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleSourceBoundaryGuard", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxWholeScene2DScaleMode_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.WholeScene2DScaleMode", ReadWholeScene2DScaleModeCombo(ui->cbxWholeScene2DScaleMode));
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxWholeScene2DScaleAlgorithm_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    WriteScaleAlgorithmConfig(cfg, "3D.GL.WholeScene2DScaleAlgorithm",
                              ReadScaleAlgorithmCombo(ui->cbxWholeScene2DScaleAlgorithm), true);
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxWholeScene2DScaleFragmentationFallback_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.WholeScene2DScaleFragmentationFallback",
               ReadWholeScene2DFragmentationFallbackCombo(ui->cbxWholeScene2DScaleFragmentationFallback));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleExactFinalFallback_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleExactFinalFallback", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleForegroundOverlay_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleForegroundOverlay", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleCaptureBacked_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleCaptureBacked", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleDebugTint_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleDebugTint", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleNoWrapFilterTaps_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleNoWrapFilterTaps", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleFinalUpscaleRender3DNative_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscaleRender3DNative", (state != 0));
    setEnabled();

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleFinalUpscale3DCoverageAware_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DCoverageAware", (state != 0));

    setEnabled();
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleFinalUpscale3DRepresentativeSemantics_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DRepresentativeSemantics", (state != 0));

    setEnabled();
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleFinalUpscale3DSplitSemantics_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSplitSemantics", (state != 0));

    setEnabled();
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleFinalUpscale3DSharpenSplitCoverage_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSharpenSplitCoverage", (state != 0));

    setEnabled();
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleOverlayLegacyUnderlay_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleOverlayLegacyUnderlay", (state != 0));

    setEnabled();
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleHybridWindowEdgeAssist_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridWindowEdgeAssist", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleHybridTarget2AlphaBlendAssist_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridTarget2AlphaBlendAssist", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleHybridNativeEffectGuard_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridNativeEffectGuard", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleHybridForeground2DBase_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridForeground2DBase", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbWholeScene2DScaleHybridCleanLegacyCandidate_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.WholeScene2DScaleHybridCleanLegacyCandidate", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxWholeScene2DScaleFinalUpscale3DFilter_currentIndexChanged(int idx)
{
    Q_UNUSED(idx);
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.WholeScene2DScaleFinalUpscale3DFilter",
               ReadFinalUpscale3DFilterCombo(ui->cbxWholeScene2DScaleFinalUpscale3DFilter));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxComputeHiResCoords_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.HiresCoordinates", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbGLMSAA_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.MSAA", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbAdvancedVideoSettings_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.AdvancedVideoSettings", state != 0);

    setEnabled();
    emit updateVideoSettings(false);
}
