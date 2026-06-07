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

#ifndef VIDEOSETTINGSDIALOG_H
#define VIDEOSETTINGSDIALOG_H

#include <QDialog>
#include <QButtonGroup>

namespace Ui { class VideoSettingsDialog; }
class VideoSettingsDialog;
class EmuInstance;

class VideoSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VideoSettingsDialog(QWidget* parent);
    ~VideoSettingsDialog();

    bool UsesGL();

    static VideoSettingsDialog* currentDlg;
    static VideoSettingsDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new VideoSettingsDialog(parent);
        currentDlg->show();
        return currentDlg;
    }
    static void closeDlg()
    {
        currentDlg = nullptr;
    }

signals:
    void updateVideoSettings(bool glchange);

private slots:
    void on_VideoSettingsDialog_accepted();
    void on_VideoSettingsDialog_rejected();

    void onChange3DRenderer(int renderer);
    void on_cbGLDisplay_stateChanged(int state);
    void on_cbVSync_stateChanged(int state);
    void on_sbVSyncInterval_valueChanged(int val);

    void on_cbxGLResolution_currentIndexChanged(int idx);
    void on_cbBetterPolygons_stateChanged(int state);
    void on_cbReadable3DTextureCache_stateChanged(int state);
    void on_cbx3DTextureAnisotropy_currentIndexChanged(int idx);
    void on_cb3DTextureFilterBinaryAlphaHandling_stateChanged(int state);
    void on_cb3DTextureFilterMipmapTopologyHandling_stateChanged(int state);
    void on_cb3DTextureFilterMipmapSubrectHandling_stateChanged(int state);
    void on_cb3DTextureFilterMipmapAlphaHandling_stateChanged(int state);
    void on_cbx3DTextureFilterMipDepth_currentIndexChanged(int idx);
    void on_cb3DTextureLosslessRGB6Repack_stateChanged(int state);
    void on_cb3DTextureScaling_stateChanged(int state);
    void on_cbx3DTextureScalingAlgorithm_currentIndexChanged(int idx);
    void on_cb3DTextureScalingFrequentChangePolicy_stateChanged(int state);
    void on_cb3DTextureScalingDeferred_stateChanged(int state);
    void on_cb3DTextureScalingNativeMipFloor_stateChanged(int state);
    void on_cb3DTextureScalingSourceMips_stateChanged(int state);
    void on_cb3DTextureScalingEdgeExtendUnusedMargins_stateChanged(int state);
    void on_cb3DTextureScalingLegacyAlphaHandling_stateChanged(int state);
    void on_cb3DTextureScalingQualityAlphaHandling_stateChanged(int state);
    void on_cbWholeScene2DScale_stateChanged(int state);
    void on_cbWholeScene2DScaleSourceBoundaryGuard_stateChanged(int state);
    void on_cbxWholeScene2DScaleMode_currentIndexChanged(int idx);
    void on_cbxWholeScene2DScaleAlgorithm_currentIndexChanged(int idx);
    void on_cbxWholeScene2DScaleFragmentationFallback_currentIndexChanged(int idx);
    void on_cbWholeScene2DScaleExactFinalFallback_stateChanged(int state);
    void on_cbWholeScene2DScaleForegroundOverlay_stateChanged(int state);
    void on_cbWholeScene2DScaleCaptureBacked_stateChanged(int state);
    void on_cbWholeScene2DScaleDebugTint_stateChanged(int state);
    void on_cbWholeScene2DScaleNoWrapFilterTaps_stateChanged(int state);
    void on_cbWholeScene2DScaleFinalUpscaleRender3DNative_stateChanged(int state);
    void on_cbWholeScene2DScaleFinalUpscale3DCoverageAware_stateChanged(int state);
    void on_cbWholeScene2DScaleFinalUpscale3DRepresentativeSemantics_stateChanged(int state);
    void on_cbWholeScene2DScaleFinalUpscale3DSplitSemantics_stateChanged(int state);
    void on_cbWholeScene2DScaleFinalUpscale3DSharpenSplitCoverage_stateChanged(int state);
    void on_cbWholeScene2DScaleOverlayLegacyUnderlay_stateChanged(int state);
    void on_cbWholeScene2DScaleHybridWindowEdgeAssist_stateChanged(int state);
    void on_cbWholeScene2DScaleHybridTarget2AlphaBlendAssist_stateChanged(int state);
    void on_cbWholeScene2DScaleHybridNativeEffectGuard_stateChanged(int state);
    void on_cbWholeScene2DScaleHybridForeground2DBase_stateChanged(int state);
    void on_cbWholeScene2DScaleHybridCleanLegacyCandidate_stateChanged(int state);
    void on_cbxWholeScene2DScaleFinalUpscale3DFilter_currentIndexChanged(int idx);
    void on_cbxComputeHiResCoords_stateChanged(int state);
    void on_cbGLMSAA_stateChanged(int state);
    void on_cbAdvancedVideoSettings_stateChanged(int state);

    void on_cbSoftwareThreaded_stateChanged(int state);
private:
    void setVsyncControlEnable(bool hasOGL);
    void setEnabled();
    void updateLosslessRGB6RepackForTextureScalingFilteringConflict();
    void applyRecommendedAdvancedSettings();
    void applyRecommendedDefaults();

    Ui::VideoSettingsDialog* ui;
    EmuInstance* emuInstance;

    QButtonGroup* grp3DRenderer;

    int oldRenderer;
    int oldGLDisplay;
    int oldVSync;
    int oldVSyncInterval;
    int oldSoftThreaded;
    int oldGLScale;
    int oldGLBetterPolygons;
    int oldReadable3DTextureCache;
    int oldAdvancedVideoSettings;

    struct TextureFilterSnapshot
    {
        int Anisotropy;
        int BinaryAlphaHandling;
        int TopologyAwareMipHandling;
        int MipmapSubrectHandling;
        int MipmapAlphaHandling;
        int MipDepth;
        int LosslessRGB6Repack;
    } oldTextureFilter;

    struct TextureScalingSnapshot
    {
        int Enabled;
        int Algorithm;
        int FrequentChangePolicy;
        int Deferred;
        int NativeMipFloor;
        int SourceMips;
        int EdgeExtendUnusedMargins;
        int LegacyAlphaHandling;
        int QualityAlphaHandling;
    } oldTextureScaling;

    struct WholeScene2DSnapshot
    {
        int Enabled;
        int SourceBoundaryGuard;
        int Mode;
        int Algorithm;
        int FragmentationFallback;
        int ExactFinalFallback;
        int ForegroundOverlay;
        int CaptureBacked;
        int DebugTint;
        int NoWrapFilterTaps;
        int FinalUpscaleRender3DNative;
        int FinalUpscale3DFilter;
        int FinalUpscale3DCoverageAware;
        int FinalUpscale3DRepresentativeSemantics;
        int FinalUpscale3DSplitSemantics;
        int FinalUpscale3DSharpenSplitCoverage;
        int OverlayLegacyUnderlay;
        int HybridWindowEdgeAssist;
        int HybridTarget2AlphaBlendAssist;
        int HybridNativeEffectGuard;
        int HybridForeground2DBase;
        int HybridCleanLegacyCandidate;
    } oldWholeScene2D;

    int oldHiresCoordinates;
    int oldGLMSAA;
};

#endif // VIDEOSETTINGSDIALOG_H

