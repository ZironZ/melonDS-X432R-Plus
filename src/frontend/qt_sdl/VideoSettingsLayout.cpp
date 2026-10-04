// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoSettingsDialog.h"
#include "ui_VideoSettingsDialog.h"
#include "Window.h"
#include "RendererSettings.h"
#include <QFormLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
QString WrappedHelp(const QString& text)
{
    if (text.isEmpty() || text.startsWith(QLatin1Char('<')))
        return text;
    return QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

void SetHelp(QWidget* widget, const QString& text)
{
    // Qt otherwise chooses different wrapping widths for tooltips and '?'.
    // Keep an existing wrapper when copying help to labels or refreshing it.
    const QString prefix = QStringLiteral("<qt><table width=\"360\" cellspacing=\"0\" cellpadding=\"0\"><tr><td>");
    const QString help = text.isEmpty() || text.startsWith(prefix) ? text
        : prefix + WrappedHelp(text) + QStringLiteral("</td></tr></table></qt>");
    widget->setWhatsThis(help);
    widget->setToolTip(help);
    for (auto* label : widget->window()->findChildren<QLabel*>())
    {
        if (label->buddy() != widget)
            continue;
        label->setWhatsThis(help);
        label->setToolTip(help);
    }
}

QLabel* Hint(const QString& text, QLayout* layout)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(label);
    return label;
}

void MoveLayout(QLayout* source, QLayout* item, QBoxLayout* destination)
{
    source->removeItem(item);
    item->setParent(nullptr);
    destination->addLayout(item);
}

QString AlgorithmHint(int value)
{
    using Algorithm = melonDS::RendererSettings::GLScaleAlgorithm;
    switch (melonDS::RendererSettings::GetGLScaleAlgorithm(value))
    {
    case Algorithm::ArtCNNDN: return VideoSettingsDialog::tr("Gives a smoother, cleaner look, sometimes at the expense of fine detail.");
    case Algorithm::ArtCNN: return VideoSettingsDialog::tr("Gives a sharp, detailed look, but can make noisy textures and rough edges stand out.");
    case Algorithm::NNEDI3: return VideoSettingsDialog::tr("Smooths edges and diagonals with less reshaping than xBRZ. It can look soft, especially around small text.");
    case Algorithm::CuNNy4x32: return VideoSettingsDialog::tr("Gives a crisp look with more visible pixel structure than ArtCNN in some scenes. Text and fine details can look cleaner or rougher depending on the artwork.");
    case Algorithm::XBRZ: return VideoSettingsDialog::tr("Rounds off pixel-art shapes. It can look clean and crisp, but small details may become smudged or distorted.");
    default: return VideoSettingsDialog::tr("The fastest option. It smooths pixel edges but leaves the underlying blockiness visible.");
    }
}

QString ModeHelp(int value)
{
    using Mode = melonDS::RendererSettings::WholeScene2DScaleMode;
    switch (melonDS::RendererSettings::GetWholeScene2DScaleMode(value))
    {
    case Mode::FinalNativeUpscale:
        return VideoSettingsDialog::tr("Postprocessing upscales the completed screen as one image. Try this if Hybrid has visual problems; 3D usually looks softer. Render 3D at native resolution can help with mismatched 2D and 3D edges.");
    case Mode::OverlayOperatorUpscale:
        return VideoSettingsDialog::tr("Presentation Overlay upscales a 2D overlay over high-resolution 3D in supported scenes. An advanced comparison mode; complex effects may retain their original rendering or show artifacts.");
    case Mode::HighResCompositor:
        return VideoSettingsDialog::tr("High-resolution Compositor combines 2D layers directly at higher resolution. An advanced comparison mode with limited support for complex effects; use Hybrid or Postprocessing for normal play.");
    case Mode::LegacyNativeUpscale:
        return VideoSettingsDialog::tr("Native Stack upscales the original top and underlying layers before combining them. An advanced comparison mode useful for simple scenes; complex blending may look wrong.");
    default:
        return VideoSettingsDialog::tr("Hybrid improves 2D artwork while preserving sharp 3D where possible. Some effects keep their original detail. Try Postprocessing if a game has visual problems.");
    }
}
}

void VideoSettingsDialog::setupTabbedLayout(QGroupBox* widescreenGroup)
{
    // Keep the controls, signal connections, and config bindings. Only their
    // containers change, so switching between simple and advanced is lossless.
    delete layout();
    auto* root = new QVBoxLayout(this);
    auto* toolbar = new QHBoxLayout;
    ui->btnRecommendedDefaults->setText(tr("Recommended settings"));
    ui->btnRecommendedDefaults->setAutoDefault(false);
    SetHelp(ui->btnRecommendedDefaults, tr("Apply a balanced starting point: 4x resolution, Hybrid screen upscaling with Spline36, screen filtering, and 4x texture filtering. Turns texture upscaling and Reduce 2D texture artifacts off.\n\nAlso resets advanced controls, enables sprite and background enhancements, and extends widescreen masks and transitions.\n\nKeeps your renderer, texture upscaling algorithm, widescreen aspect ratio and display selection, sharpening, and LCD ghosting. Cancel restores your previous settings."));
    ui->cbAdvancedVideoSettings->setWhatsThis(tr("Show detailed controls, Compatibility, and Experiments. Turning this off only hides controls; it does not change their values."));
    toolbar->addWidget(ui->btnRecommendedDefaults);
    toolbar->addStretch();
    toolbar->addWidget(ui->cbAdvancedVideoSettings);
    root->addLayout(toolbar);
    Hint(tr("Start with the recommended settings, then adjust each enhancement to suit your game."), root);

    settingsTabs = new QTabWidget(this);
    settingsTabs->setObjectName("videoSettingsTabs");
    root->addWidget(settingsTabs, 1);
    auto page = [this](const QString& title, const char* name, const QString& description) {
        auto* scroll = new QScrollArea;
        scroll->setObjectName(QString::fromLatin1(name));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* contents = new QWidget;
        auto* box = new QVBoxLayout(contents);
        box->setContentsMargins(12, 12, 12, 12);
        box->setSpacing(12);
        box->setAlignment(Qt::AlignTop);
        Hint(description, box);
        scroll->setWidget(contents);
        settingsTabs->addTab(scroll, title);
        return box;
    };
    auto* general = page(tr("General"), "videoGeneralPage",
        tr("Choose your renderer, resolution, display timing, and final image settings."));
    auto* textures = page(tr("3D textures"), "videoTexturesPage",
        tr("Filter angled surfaces, upscale textures, and reduce texture-edge artifacts. These options also affect 2D artwork drawn using 3D."));
    auto* screen = page(tr("2D && screen"), "videoScreenPage",
        tr("Improve text, sprites, menus, and mixed 2D/3D scenes."));
    auto* widescreen = page(tr("Widescreen"), "videoWidescreenPage",
        tr("Expand the view for supported games, including games played with rotated screens."));
    widescreen->addWidget(widescreenGroup);
    auto* compatibility = page(tr("Compatibility"), "videoCompatibilityPage",
        tr("Adjust how enhancements interact with the game's original rendering."));
    compatibilityPage = settingsTabs->widget(settingsTabs->count() - 1);
    auto* experiments = page(tr("Experiments"), "videoExperimentsPage",
        tr("Optional renderer experiments and diagnostic controls. Recommended settings also configures these; you do not need to tune them for normal use."));
    experimentsPage = settingsTabs->widget(settingsTabs->count() - 1);

    auto* rendering = new QGroupBox(tr("Rendering"));
    auto* renderingLayout = new QGridLayout(rendering);
    // The original button group retains renderer IDs, change handling, and
    // platform restrictions. Present it as a compact selector on this page.
    auto* renderer = new QComboBox;
    renderer->setObjectName("cbxVideoRenderer");
    const QList<QRadioButton*> renderers = {ui->rb3DCompute, ui->rb3DOpenGL, ui->rb3DSoftware};
    for (auto* button : renderers)
    {
        const int index = renderer->count();
        renderer->addItem(button->text(), grp3DRenderer->id(button));
        auto* model = qobject_cast<QStandardItemModel*>(renderer->model());
        model->item(index)->setEnabled(button->isEnabled());
        if (button->isChecked()) renderer->setCurrentIndex(index);
        connect(button, &QRadioButton::toggled, renderer, [renderer, index, button](bool checked) {
            if (!checked) return;
            QSignalBlocker block(renderer);
            renderer->setCurrentIndex(index);
            SetHelp(renderer, button->whatsThis());
        });
    }
    SetHelp(renderer, renderers[renderer->currentIndex()]->whatsThis());
    connect(renderer, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [renderers](int index) { if (index >= 0) renderers[index]->click(); });
    auto* rendererLabel = new QLabel(tr("Renderer:"));
    rendererLabel->setBuddy(renderer);
    renderingLayout->addWidget(rendererLabel, 0, 0);
    renderingLayout->addWidget(renderer, 0, 1);
    ui->label_3->setText(tr("Internal resolution:"));
    ui->cbxGLResolution->setMaximumWidth(QWIDGETSIZE_MAX);
    renderingLayout->addWidget(ui->label_3, 1, 0);
    renderingLayout->addWidget(ui->cbxGLResolution, 1, 1);
    const QString rendererHint = ui->rb3DCompute->isEnabled()
        ? tr("Compute is the suggested starting point. Higher resolution needs more GPU power.")
        : tr("Compute is unavailable here. Higher resolution needs more GPU power.");
    auto* resolutionHint = new QLabel(rendererHint);
    resolutionHint->setWordWrap(true);
    resolutionHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    renderingLayout->addWidget(ui->cbGLMSAA, 2, 0, 1, 2);
    renderingLayout->addWidget(resolutionHint, 3, 0, 1, 2);
    general->addWidget(rendering);

    auto* timing = new QGroupBox(tr("Display timing"));
    auto* timingLayout = new QGridLayout(timing);
    timingLayout->addWidget(ui->cbGLDisplay, 0, 0, 1, 2);
    timingLayout->addWidget(ui->cbVSync, 1, 0, 1, 2);
    timingLayout->addWidget(ui->label_2, 2, 0);
    timingLayout->addWidget(ui->sbVSyncInterval, 2, 1);
    general->addWidget(timing);

    presentationGroup = new QGroupBox(tr("Final image"));
    auto* presentation = new QFormLayout(presentationGroup);
    presentation->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* window = static_cast<MainWindow*>(parentWidget());
    auto& cfg = window->getWindowConfig();
    oldScreenFilter = cfg.GetBool("ScreenFilter");
    oldScreenSharpen = cfg.GetBool("ScreenSharpen");
    oldScreenSharpenStrength = cfg.GetInt("ScreenSharpenStrength");
    oldScreenGhosting = cfg.GetBool("ScreenLCDGhosting");
    oldScreenGhostingMode = cfg.GetInt("ScreenLCDGhostingMode");
    auto* filter = new QCheckBox(tr("Screen filtering"));
    filter->setObjectName("cbScreenFiltering");
    filter->setChecked(oldScreenFilter);
    filter->setWhatsThis(tr("Smooth the final image when fitting it to the window. This is separate from 3D texture filtering."));
    presentation->addRow(filter);
    auto* sharpening = new QComboBox;
    sharpening->setObjectName("cbxScreenSharpening");
    sharpening->addItems({tr("Off"), tr("Low"), tr("Medium"), tr("High"), tr("Very high")});
    sharpening->setCurrentIndex(std::clamp(oldScreenSharpenStrength == 0 && oldScreenSharpen ? 2 : oldScreenSharpenStrength, 0, 4));
    sharpening->setWhatsThis(tr("Sharpen the completed image. Higher strengths can introduce halos around edges."));
    presentation->addRow(tr("Screen sharpening:"), sharpening);
    presentation->labelForField(sharpening)->setWhatsThis(sharpening->whatsThis());
    auto* ghosting = new QComboBox;
    ghosting->setObjectName("cbxScreenGhosting");
    ghosting->addItems({tr("Off"), tr("Smart"), tr("Natural blur")});
    ghosting->setCurrentIndex(std::clamp(oldScreenGhostingMode == 0 && oldScreenGhosting ? 1 : oldScreenGhostingMode, 0, 2));
    ghosting->setWhatsThis(tr("Can reduce flicker in games that alternate images between frames. Smart blends alternating pixels selectively. Natural blur simulates a slower LCD response and can leave trails during motion."));
    presentation->addRow(tr("LCD ghosting:"), ghosting);
    presentation->labelForField(ghosting)->setWhatsThis(ghosting->whatsThis());
    connect(window, &MainWindow::screenPresentationSettingsChanged, this,
        [filter, sharpening, ghosting](bool filtering, int strength, int mode) {
            // Reflect menu changes without writing the config back or applying
            // the effect twice. Keep the opening snapshot for Cancel intact.
            QSignalBlocker filterBlock(filter), sharpenBlock(sharpening), ghostBlock(ghosting);
            filter->setChecked(filtering);
            sharpening->setCurrentIndex(strength);
            ghosting->setCurrentIndex(mode);
        });
    connect(filter, &QCheckBox::toggled, this, [window](bool value) {
        window->getWindowConfig().SetBool("ScreenFilter", value);
        window->refreshScreenPresentationSettings();
    });
    connect(sharpening, qOverload<int>(&QComboBox::currentIndexChanged), this, [window](int value) {
        window->getWindowConfig().SetInt("ScreenSharpenStrength", value);
        window->getWindowConfig().SetBool("ScreenSharpen", value != 0);
        window->refreshScreenPresentationSettings();
    });
    connect(ghosting, qOverload<int>(&QComboBox::currentIndexChanged), this, [window](int value) {
        window->getWindowConfig().SetInt("ScreenLCDGhostingMode", value);
        window->getWindowConfig().SetBool("ScreenLCDGhosting", value != 0);
        window->refreshScreenPresentationSettings();
    });
    general->addWidget(presentationGroup);

    for (auto* group : {ui->groupBoxTextureFiltering, ui->groupBoxTextureScaling})
    {
        group->setFlat(false);
        group->setStyleSheet(QString());
        textures->addWidget(group);
    }
    ui->groupBoxTextureFiltering->setTitle(tr("Texture filtering"));
    ui->groupBoxTextureScaling->setTitle(tr("Texture upscaling"));
    auto* filteringHint = new QLabel(tr("Reduces shimmer on distant or angled surfaces."));
    filteringHint->setWordWrap(true);
    int filterRow = 2;
    for (auto* control : {ui->cb3DTextureFilterBinaryAlphaHandling, ui->cb3DTextureFilterMipmapAlphaHandling,
            ui->cb3DTextureFilterMipmapTopologyHandling, ui->cb3DTextureFilterMipmapSubrectHandling,
            ui->cb3DTextureFilterTranslucentGuard})
    {
        ui->gridLayoutTextureFiltering->removeWidget(control);
        ui->gridLayoutTextureFiltering->addWidget(control, filterRow++, 0, 1, 3);
    }
    ui->gridLayoutTextureFiltering->addWidget(filteringHint, filterRow, 0, 1, 3);
    // Keep guidance on the control it explains, rather than stacking general
    // and algorithm-specific descriptions below the same selector.
    auto addAlgorithmHelp = [this](QComboBox* combo, QLabel* label) {
        const QString help = combo->whatsThis();
        auto update = [combo, label, help]() {
            const QString hint = AlgorithmHint(combo->currentData().toInt());
            const QString algorithmHelp = QStringLiteral("<p><b>%1:</b> %2</p>")
                .arg(combo->currentText().toHtmlEscaped(), hint.toHtmlEscaped());
            SetHelp(combo, WrappedHelp(help) + algorithmHelp);
            SetHelp(label, combo->whatsThis());
        };
        update();
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [update](int) { update(); });
        connect(ui->btnRecommendedDefaults, &QPushButton::clicked, this, update);
    };
    addAlgorithmHelp(ui->cbx3DTextureScalingAlgorithm, ui->lbl3DTextureScalingAlgorithm);
    for (auto* control : {ui->cb3DTextureScalingFrequentChangePolicy, ui->cb3DTextureScalingDeferred})
    {
        ui->gridLayout3DTextureScalingOptions->removeWidget(control);
        ui->verticalLayoutTextureScaling->addWidget(control);
    }

    auto* artifacts = new QGroupBox(tr("Seams and transparent edges"));
    auto* artifactLayout = new QVBoxLayout(artifacts);
    artifactOptionsLayout = new QGridLayout;
    artifactLayout->addLayout(artifactOptionsLayout);
    for (QWidget* control : std::initializer_list<QWidget*>{ui->cb3DTexture2DAtlasProtection,
            ui->cb3DTextureScalingAlphaXBRZ, ui->cb3DTextureFilterSmart2D,
            ui->cb3DTextureScalingEdgeExtendUnusedMargins, ui->reconstruction3DOptions})
        artifactOptionsLayout->addWidget(control);
    Hint(tr("Try these for lines, halos, or blocky cutout edges. They may cost performance and will not fix every game."), artifactLayout);
    textures->addWidget(artifacts);
    advancedTextureGroup = new QGroupBox(tr("Advanced texture controls"));
    auto* advancedTextures = new QVBoxLayout(advancedTextureGroup);
    textureAlphaRow = new QWidget;
    auto* alphaLayout = new QFormLayout(textureAlphaRow);
    alphaLayout->setContentsMargins(0, 0, 0, 0);
    textureAlphaCombo = new QComboBox;
    textureAlphaCombo->setObjectName("cbxTextureAlpha");
    for (const auto& name : {tr("Bilinear"), tr("Spline36"), tr("NNEDI3"), tr("xBRZ")})
        textureAlphaCombo->addItem(name, textureAlphaCombo->count());
    textureAlphaCombo->setCurrentIndex(oldTextureScaling.Alpha);
    textureAlphaCombo->setWhatsThis(tr("Choose how texture transparency and cutout edges are scaled, independently of color. Bilinear is recommended. Other algorithms can change edge smoothness and cost more processing. Cleaner transparent edges in simple mode selects xBRZ; turning it off selects Bilinear."));
    alphaLayout->addRow(tr("Transparency scaling:"), textureAlphaCombo);
    alphaLayout->labelForField(textureAlphaCombo)->setWhatsThis(textureAlphaCombo->whatsThis());
    advancedTextures->addWidget(textureAlphaRow);
    connect(textureAlphaCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int index) { if (index >= 0) setTextureAlpha(textureAlphaCombo->itemData(index).toInt()); });
    MoveLayout(ui->verticalLayoutTextureScaling, ui->gridLayout3DTextureScalingOptions, advancedTextures);
    ui->gridLayout3DTextureScalingOptions->setContentsMargins(0, 0, 0, 0);
    textures->addWidget(advancedTextureGroup);

    screen->addWidget(ui->groupBoxWholeScene2DScaling);
    addAlgorithmHelp(ui->cbxWholeScene2DScaleAlgorithm, ui->lblWholeScene2DScaleAlgorithm);
    ui->gridLayoutWholeScene2DScaling->addWidget(
        ui->cbWholeScene2DScaleFinalUpscaleRender3DNative, 8, 0, 1, 3);
    auto updateModeHelp = [this]() {
        SetHelp(ui->cbxWholeScene2DScaleMode,
                ModeHelp(ui->cbxWholeScene2DScaleMode->currentData().toInt()));
    };
    updateModeHelp();
    connect(ui->cbxWholeScene2DScaleMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [updateModeHelp](int) { updateModeHelp(); });
    // Recommended settings changes the selection with signals blocked.
    connect(ui->btnRecommendedDefaults, &QPushButton::clicked,
            this, updateModeHelp);
    // Avoid deeply nested panels; each enhancement has its own section.
    screen->addWidget(affineGroup);
    Hint(tr("Hybrid preserves sharp 3D where possible. If it has visual problems, try Postprocessing; 3D will look softer. Enable Advanced settings to fine-tune sprites, sampling, and assembly."), screen);
    compatibility->addWidget(compatibilityGroup);
    auto* rendererCompatibility = new QGroupBox(tr("Renderer options"));
    auto* rendererOptions = new QVBoxLayout(rendererCompatibility);
    rendererOptions->addWidget(ui->cbBetterPolygons);
    rendererOptions->addWidget(ui->cbxComputeHiResCoords);
    compatibility->addWidget(rendererCompatibility);
    general->addWidget(ui->groupBox_2);
    experiments->addWidget(experimentsGroup);
    experimentsGroup->setTitle(tr("Renderer experiments"));

    // Use one description for hover and '?' help, including label buddies.
    for (auto* widget : findChildren<QWidget*>())
    {
        if (!widget->whatsThis().isEmpty())
            SetHelp(widget, widget->whatsThis());
        else if (!widget->toolTip().isEmpty())
            SetHelp(widget, widget->toolTip());
    }
    SetHelp(ui->label_2, ui->sbVSyncInterval->whatsThis());

    root->addWidget(ui->buttonBox);
    ui->groupBox->hide();
    ui->scrollAreaOpenGLRenderer->hide();
    setMinimumSize(540, 480);
    resize(700, 700);
}

void VideoSettingsDialog::updateTabVisibility(bool advanced)
{
    if (!settingsTabs) return;
    // Removing tabs, rather than deleting pages, also supports Qt 5 and keeps
    // all values and connections intact when advanced controls are hidden.
    for (auto* page : {compatibilityPage, experimentsPage})
    {
        int index = settingsTabs->indexOf(page);
        if (!advanced && index >= 0)
        {
            if (settingsTabs->currentWidget() == page) settingsTabs->setCurrentIndex(0);
            settingsTabs->removeTab(index);
        }
        else if (advanced && index < 0)
            settingsTabs->addTab(page, page == compatibilityPage ? tr("Compatibility") : tr("Experiments"));
    }
    advancedTextureGroup->setVisible(advanced);
    ui->cbGLDisplay->setVisible(advanced);
    ui->label_2->setVisible(advanced);
    ui->sbVSyncInterval->setVisible(advanced);
    widescreenExtendWindows->setVisible(advanced);
    widescreenExpandNarrowBorders->setVisible(advanced);
    presentationGroup->setEnabled(UsesGL());
    ui->groupBox_2->setVisible(ui->rb3DSoftware->isChecked());
}
