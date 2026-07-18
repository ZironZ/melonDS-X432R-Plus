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

#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QEventLoop>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

#include "EmuInstance.h"
#include "EmuThread.h"
#include "GPU.h"
#include "TextureScalingDebugDialog.h"
#include "Window.h"

TextureScalingDebugDialog* TextureScalingDebugDialog::currentDlg = nullptr;

class TexturePreviewLabel final : public QLabel
{
public:
    TexturePreviewLabel(const QString& emptyText, const QString& exportPrefix, QWidget* parent)
        : QLabel(parent)
        , EmptyText(emptyText)
        , ExportPrefix(exportPrefix)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(220, 160);
        setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        QLabel::setText(EmptyText);
    }

    void SetEmptyText(const QString& emptyText)
    {
        EmptyText = emptyText;
        if (CurrentImage.isNull())
            QLabel::setText(EmptyText);
    }

    void SetImage(const QImage& image)
    {
        CurrentImage = image;
        UpdatePixmap();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        UpdatePixmap();
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (CurrentImage.isNull())
        {
            QLabel::contextMenuEvent(event);
            return;
        }

        QMenu menu(this);
        QAction* copyAction = menu.addAction("Copy image");
        QAction* saveAction = menu.addAction("Save as PNG...");
        QAction* chosenAction = menu.exec(event->globalPos());

        if (chosenAction == copyAction)
        {
            if (QClipboard* clipboard = QGuiApplication::clipboard())
                clipboard->setImage(CurrentImage);
        }
        else if (chosenAction == saveAction)
        {
            const QString defaultPath = QString("%1-%2.png")
                                            .arg(ExportPrefix,
                                                 QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
            const QString path = QFileDialog::getSaveFileName(this,
                                                              "Save texture preview",
                                                              defaultPath,
                                                              "PNG image (*.png)");
            if (!path.isEmpty())
                CurrentImage.save(path, "PNG");
        }
    }

private:
    void UpdatePixmap()
    {
        if (CurrentImage.isNull())
        {
            clear();
            QLabel::setText(EmptyText);
            return;
        }

        QLabel::setText(QString());
        setPixmap(QPixmap::fromImage(CurrentImage).scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }

    QImage CurrentImage;
    QString EmptyText;
    QString ExportPrefix;
};

namespace
{
QString AlgorithmLabel(int algorithmIndex)
{
    using ScaleAlgorithm = melonDS::RendererSettings::GLScaleAlgorithm;

    switch (melonDS::RendererSettings::GetGLScaleAlgorithm(algorithmIndex))
    {
    case ScaleAlgorithm::Spline36: return "Spline36 (GPU)";
    case ScaleAlgorithm::ArtCNNDN: return "ArtCNN DN (GPU)";
    case ScaleAlgorithm::XBRZ: return "xBRZ (GPU)";
    case ScaleAlgorithm::ArtCNN: return "ArtCNN (GPU)";
    case ScaleAlgorithm::NNEDI3: return "NNEDI3 nns16 8x4 RGB (GPU compute)";
    case ScaleAlgorithm::CuNNy4x32: return "CuNNy 4x32 NVL RGB (GPU compute)";
    }

    return "Unknown";
}

QString FormatFrameStats(const melonDS::TextureScalingDebugFrameStats& stats)
{
    return QStringLiteral(
               "  Cache hits: %1\n"
               "  Cache misses: %2\n"
               "  Secondary cache hits: %3\n"
               "  Secondary cache stores: %4\n"
               "  Secondary cache evictions: %5\n"
               "  Invalidations: %6\n"
               "  Uploads: %7\n"
               "  Scaled uploads: %8\n"
               "  GPU scaler uploads: %9\n"
               "  GPU direct repack uploads: %10\n"
               "  GPU readback fallback uploads: %11\n"
               "  Frequent-change fallback uploads: %12\n"
               "  Deferred unscaled uploads: %13\n"
               "  Deferred promotion uploads: %14\n"
               "  xBRZ uploads: %15\n"
               "  Spline36 uploads: %16\n"
               "  Source texels: %17\n"
               "  Uploaded texels: %18")
        .arg(stats.CacheHits)
        .arg(stats.CacheMisses)
        .arg(stats.SecondaryCacheHits)
        .arg(stats.SecondaryCacheStores)
        .arg(stats.SecondaryCacheEvictions)
        .arg(stats.Invalidations)
        .arg(stats.Uploads)
        .arg(stats.ScaledUploads)
        .arg(stats.GPUScaledUploads)
        .arg(stats.GPUDirectRepackUploads)
        .arg(stats.GPULegacyReadbackUploads)
        .arg(stats.FrequentChangeFallbackUploads)
        .arg(stats.DeferredUnscaledUploads)
        .arg(stats.DeferredPromotionUploads)
        .arg(stats.XBRZScaledUploads)
        .arg(stats.Spline36ScaledUploads)
        .arg(stats.SourcePixels)
        .arg(stats.UploadPixels);
}

QString FormatMiB(melonDS::u64 bytes)
{
    return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

QString FormatStats(const melonDS::TextureScalingDebugStats& stats)
{
    const QString renderer = stats.UsesComputeBackend ? "OpenGL (Compute shader)" : "OpenGL (Classic)";
    const QString scaling = stats.TextureScalingEnabled ? "enabled" : "disabled";
    const QString frequentChangePolicy = stats.FrequentChangePolicyEnabled ? "on" : "off";
    const QString deferredScaling = stats.DeferredScalingEnabled ? "on" : "off";
    const QString legacyAlpha = stats.LegacyAlphaHandling ? "on" : "off";
    const QString qualityAlpha = stats.QualityAlphaHandling ? "on" : "off";
    const QString cleanerTransparentEdges = stats.AlphaXBRZ ? "on" : "off";
    const QString readableCache = stats.ReadableTextureCache ? "on" : "off";
    const QString spline36Alpha = stats.Spline36Alpha ? "on" : "off";

    return QStringLiteral(
               "Renderer: %1\n"
               "Texture scaling: %2\n"
               "Scale factor: %3x\n"
               "Algorithm: %4\n"
               "Frequent-change policy: %5\n"
               "Deferred scaling: %6\n"
               "Classic GPU alpha handling: %7\n"
               "Quality alpha handling: %8\n"
               "Cleaner transparent edges: %9\n"
               "Readable texture cache: %10\n"
               "Spline36 alpha: %11\n"
               "\n"
               "Cache entries: %12\n"
               "Secondary cache entries: %13 / %14\n"
               "Secondary cache texels: %15 / %16\n"
               "Secondary cache approx bytes: %17 / %18\n"
               "Free layers: %19\n"
               "Total layers: %20\n"
               "Array textures: %21\n"
               "Frames observed: %22\n"
               "\n"
               "Last frame:\n%23\n"
               "\n"
               "Totals since reset:\n%24")
        .arg(renderer)
        .arg(scaling)
        .arg(stats.ScaleFactor)
        .arg(AlgorithmLabel(stats.AlgorithmIndex))
        .arg(frequentChangePolicy)
        .arg(deferredScaling)
        .arg(legacyAlpha)
        .arg(qualityAlpha)
        .arg(cleanerTransparentEdges)
        .arg(readableCache)
        .arg(spline36Alpha)
        .arg(stats.CacheEntries)
        .arg(stats.SecondaryCacheEntries)
        .arg(stats.SecondaryCacheMaxEntries)
        .arg(stats.SecondaryCacheTexels)
        .arg(stats.SecondaryCacheMaxTexels)
        .arg(FormatMiB(stats.SecondaryCacheApproxBytes))
        .arg(FormatMiB(stats.SecondaryCacheMaxApproxBytes))
        .arg(stats.FreeLayers)
        .arg(stats.TotalLayers)
        .arg(stats.TextureArrays)
        .arg(stats.FramesObserved)
        .arg(FormatFrameStats(stats.LastFrame))
        .arg(FormatFrameStats(stats.Totals));
}

QString FormatLastMiss(const melonDS::TextureScalingDebugLastMiss& miss)
{
    if (!miss.Valid)
        return "Last miss: none captured yet.";

    QStringList reasonParts;
    if (miss.InvalidatedByTexture)
        reasonParts.append("texture VRAM invalidation");
    if (miss.InvalidatedByPalette)
        reasonParts.append("palette invalidation");
    if (reasonParts.isEmpty())
        reasonParts.append("fresh miss");

    QStringList scalerParts;
    if (miss.UsedFrequentChangeFallback)
        scalerParts.append("frequent-change fallback (nearest)");
    if (miss.UsedDeferredUnscaledUpload)
        scalerParts.append("deferred unscaled upload");
    if (miss.UsedGPUScaler)
        scalerParts.append(miss.UsedGPUDirectRepack ? "GPU scaler (direct repack)" : "GPU scaler (readback fallback)");
    if (miss.UsedXBRZ)
        scalerParts.append("xBRZ");
    if (miss.UsedSpline36)
        scalerParts.append("Spline36");
    if (scalerParts.isEmpty())
        scalerParts.append(miss.UsedScaling ? "scaled" : "unscaled upload");

    return QStringLiteral(
               "Last miss\n"
               "  texParam: 0x%1\n"
               "  palBase: 0x%2\n"
               "  Source size: %3x%4\n"
               "  Result size: %5x%6\n"
               "  Scale factor: %7x\n"
               "  Algorithm: %8\n"
               "  Upload path: %9\n"
               "  Cause: %10")
        .arg(QString::number(miss.TexParam, 16).rightJustified(8, '0'))
        .arg(QString::number(miss.PalBase, 16).rightJustified(8, '0'))
        .arg(miss.SourceWidth)
        .arg(miss.SourceHeight)
        .arg(miss.ResultWidth)
        .arg(miss.ResultHeight)
        .arg(miss.ScaleFactor)
        .arg(AlgorithmLabel(miss.AlgorithmIndex))
        .arg(scalerParts.join(", "))
        .arg(reasonParts.join(", "));
}

QString FormatFrameTextureLabel(const melonDS::TextureScalingDebugFrameTexture& texture, int index)
{
    QStringList tags;
    if (texture.CacheMiss)
        tags.append("miss");
    else if (texture.CacheHit)
        tags.append("hit");
    if (texture.SecondaryCacheHit)
        tags.append("secondary");
    if (texture.UsedScaling)
        tags.append("scaled");
    if (texture.BinaryAlphaTexture || texture.SourceBinaryAlpha)
        tags.append("alpha");
    if (texture.FrequentChangeHot)
        tags.append("hot");
    if (texture.DeferredScalePending)
        tags.append("deferred");

    return QString("#%1 0x%2 %3x%4 -> %5x%6 refs %7%8")
        .arg(index + 1)
        .arg(QString::number(texture.TexParam, 16).rightJustified(8, '0'))
        .arg(texture.SourceWidth)
        .arg(texture.SourceHeight)
        .arg(texture.ResultWidth)
        .arg(texture.ResultHeight)
        .arg(texture.ReferenceCount)
        .arg(tags.isEmpty() ? QString() : QString(" [%1]").arg(tags.join(", ")));
}

QString FormatFrameTexture(const melonDS::TextureScalingDebugFrameTexture& texture, int index, int total)
{
    QStringList pathParts;
    if (texture.CacheMiss)
        pathParts.append("cache miss");
    if (texture.CacheHit)
        pathParts.append("cache hit");
    if (texture.SecondaryCacheHit)
        pathParts.append("secondary cache");
    if (texture.UsedGPUScaler)
        pathParts.append(texture.UsedGPUDirectRepack ? "GPU scaler/direct upload" : "GPU scaler/readback upload");
    if (texture.UsedFrequentChangeFallback)
        pathParts.append("frequent-change fallback");
    if (texture.UsedDeferredUnscaledUpload)
        pathParts.append("deferred unscaled upload");
    if (texture.UsedXBRZ)
        pathParts.append("xBRZ");
    if (texture.UsedSpline36)
        pathParts.append("Spline36");
    if (pathParts.isEmpty())
        pathParts.append(texture.UsedScaling ? "scaled upload" : "native upload/cache");

    QStringList repeatParts;
    repeatParts.append((texture.RepeatMode & (1 << 0)) ? "S repeat" : "S clamp");
    repeatParts.append((texture.RepeatMode & (1 << 1)) ? "T repeat" : "T clamp");
    if (texture.RepeatMode & (1 << 2))
        repeatParts.append("S mirror");
    if (texture.RepeatMode & (1 << 3))
        repeatParts.append("T mirror");

    QString sampling = "full texture";
    if (texture.SamplingBoundsValid)
    {
        sampling = QString("%1 [%2,%3] - [%4,%5]")
            .arg(texture.SamplingBoundsEdgeExtendMargins ? "edge extend" : "subrect")
            .arg(texture.SamplingX0)
            .arg(texture.SamplingY0)
            .arg(texture.SamplingX1)
            .arg(texture.SamplingY1);
    }

    return QStringLiteral(
               "Frame texture %1 of %2\n"
               "  texParam: 0x%3\n"
               "  palBase: 0x%4\n"
               "  key: 0x%5\n"
               "  base key: 0x%6\n"
               "  Format: %7\n"
               "  Source size: %8x%9\n"
               "  Result size: %10x%11\n"
               "  Scale factor: %12x\n"
               "  Algorithm: %13\n"
               "  References this frame: %14\n"
               "  Upload/cache path: %15\n"
               "  Repeat mode: %16\n"
               "  Sampling: %17\n"
               "  Binary alpha texture: %18\n"
               "  Source binary alpha: %19\n"
               "  Source has transparent alpha: %20\n"
               "  Deferred scale pending: %21\n"
               "  Frequent-change hot: %22\n"
               "  Source preview: %23\n"
               "  Result preview: %24")
        .arg(index + 1)
        .arg(total)
        .arg(QString::number(texture.TexParam, 16).rightJustified(8, '0'))
        .arg(QString::number(texture.PalBase, 16).rightJustified(8, '0'))
        .arg(QString::number(texture.Key, 16).rightJustified(16, '0'))
        .arg(QString::number(texture.BaseKey, 16).rightJustified(16, '0'))
        .arg(texture.Format)
        .arg(texture.SourceWidth)
        .arg(texture.SourceHeight)
        .arg(texture.ResultWidth)
        .arg(texture.ResultHeight)
        .arg(texture.ScaleFactor)
        .arg(AlgorithmLabel(texture.AlgorithmIndex))
        .arg(texture.ReferenceCount)
        .arg(pathParts.join(", "))
        .arg(repeatParts.join(", "))
        .arg(sampling)
        .arg(texture.BinaryAlphaTexture ? "yes" : "no")
        .arg(texture.SourceBinaryAlpha ? "yes" : "no")
        .arg(texture.SourceHasTransparentAlpha ? "yes" : "no")
        .arg(texture.DeferredScalePending ? "yes" : "no")
        .arg(texture.FrequentChangeHot ? "yes" : "no")
        .arg(texture.SourceRGBA.empty() ? "unavailable" : "captured")
        .arg(texture.ResultRGBA.empty() ? "unavailable" : "captured");
}
}

TextureScalingDebugDialog::TextureScalingDebugDialog(QWidget* parent)
    : QDialog(parent)
    , mainWindow(static_cast<MainWindow*>(parent))
    , emuThread(mainWindow ? mainWindow->getEmuInstance()->getEmuThread() : nullptr)
    , displayedMissSequence(0)
    , displayedFrameTextureSequence(0)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("3D Texture Scaling Debug");
    resize(640, 520);

    auto* mainLayout = new QVBoxLayout(this);
    auto* controlsLayout = new QHBoxLayout();

    cbAutoRefresh = new QCheckBox("Auto-capture new misses", this);
    cbAutoRefresh->setChecked(false);

    btnRefresh = new QPushButton("Refresh", this);
    btnReset = new QPushButton("Reset counters", this);
    btnCopy = new QPushButton("Copy", this);
    btnCaptureFrame = new QPushButton("Capture frame textures", this);
    btnCopy->setEnabled(false);

    controlsLayout->addWidget(cbAutoRefresh);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(btnCaptureFrame);
    controlsLayout->addWidget(btnRefresh);
    controlsLayout->addWidget(btnReset);
    controlsLayout->addWidget(btnCopy);

    lblStatus = new QLabel(this);
    lblStatus->setWordWrap(true);

    txtStats = new QPlainTextEdit(this);
    txtStats->setReadOnly(true);
    txtStats->setLineWrapMode(QPlainTextEdit::NoWrap);
    txtStats->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    txtMissInfo = new QPlainTextEdit(this);
    txtMissInfo->setReadOnly(true);
    txtMissInfo->setLineWrapMode(QPlainTextEdit::NoWrap);
    txtMissInfo->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    txtMissInfo->setPlainText("Last miss: none captured yet.");
    txtMissInfo->setMaximumBlockCount(32);

    auto* frameTextureLayout = new QHBoxLayout();
    cbFrameTexture = new QComboBox(this);
    cbFrameTexture->setMinimumContentsLength(36);
    cbFrameTexture->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cbFrameTexture->setEnabled(false);
    frameTextureLayout->addWidget(new QLabel("Frame texture:", this));
    frameTextureLayout->addWidget(cbFrameTexture, 1);

    txtFrameTextureInfo = new QPlainTextEdit(this);
    txtFrameTextureInfo->setReadOnly(true);
    txtFrameTextureInfo->setLineWrapMode(QPlainTextEdit::NoWrap);
    txtFrameTextureInfo->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    txtFrameTextureInfo->setPlainText("Frame texture browser: capture a frame to browse referenced 3D textures.");
    txtFrameTextureInfo->setMaximumBlockCount(48);

    auto* previewsLayout = new QHBoxLayout();
    auto makePreviewColumn = [&](const QString& title, TexturePreviewLabel*& previewLabel, const QString& exportPrefix)
    {
        auto* column = new QVBoxLayout();
        auto* titleLabel = new QLabel(title, this);
        previewLabel = new TexturePreviewLabel("Unavailable", exportPrefix, this);
        column->addWidget(titleLabel);
        column->addWidget(previewLabel, 1);
        previewsLayout->addLayout(column, 1);
    };
    makePreviewColumn("Decoded source", lblSourcePreview, "texture-scaling-last-miss-source");
    makePreviewColumn("Scaled result", lblResultPreview, "texture-scaling-last-miss-result");

    mainLayout->addLayout(controlsLayout);
    mainLayout->addWidget(lblStatus);
    mainLayout->addWidget(txtStats, 1);
    mainLayout->addWidget(txtMissInfo);
    mainLayout->addLayout(frameTextureLayout);
    mainLayout->addWidget(txtFrameTextureInfo);
    mainLayout->addLayout(previewsLayout, 1);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(125);

    connect(cbAutoRefresh, &QCheckBox::toggled, this, &TextureScalingDebugDialog::requestRefresh);
    connect(btnRefresh, &QPushButton::clicked, this, &TextureScalingDebugDialog::updateStats);
    connect(btnReset, &QPushButton::clicked, this, &TextureScalingDebugDialog::resetStats);
    connect(btnCopy, &QPushButton::clicked, this, &TextureScalingDebugDialog::copyStats);
    connect(btnCaptureFrame, &QPushButton::clicked, this, &TextureScalingDebugDialog::captureFrameTextures);
    connect(cbFrameTexture, qOverload<int>(&QComboBox::currentIndexChanged), this, &TextureScalingDebugDialog::selectFrameTexture);
    connect(refreshTimer, &QTimer::timeout, this, &TextureScalingDebugDialog::onRefreshTimer);

    refreshTimer->start();
    setCaptureEnabled(false);
    setFrameCaptureEnabled(false);
    updateStatsImpl(true);
}

TextureScalingDebugDialog::~TextureScalingDebugDialog()
{
    setCaptureEnabled(false);
    setFrameCaptureEnabled(false);
    closeDlg();
}

void TextureScalingDebugDialog::closeEvent(QCloseEvent* event)
{
    setCaptureEnabled(false);
    setFrameCaptureEnabled(false);
    closeDlg();
    QDialog::closeEvent(event);
}

void TextureScalingDebugDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePreviewPixmaps();
}

void TextureScalingDebugDialog::requestRefresh()
{
    setCaptureEnabled(cbAutoRefresh->isChecked());
    if (cbAutoRefresh->isChecked())
        pollForNewMiss();
}

void TextureScalingDebugDialog::onRefreshTimer()
{
    if (!cbAutoRefresh->isChecked() || !isVisible())
        return;

    pollForNewMiss();
}

void TextureScalingDebugDialog::setStatusText(const QString& text)
{
    lblStatus->setText(text);
}

bool TextureScalingDebugDialog::setCaptureEnabled(bool enabled)
{
    if (!mainWindow || !mainWindow->getEmuInstance())
        return false;

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
        return false;

    std::string status;
    emuThread->borrowGL();
    const bool ok = nds->GPU.GetRenderer().SetTextureScalingDebugCaptureEnabled(enabled, &status);
    emuThread->returnGL();

    if (!ok && !status.empty())
        setStatusText(QString::fromStdString(status));
    return ok;
}

bool TextureScalingDebugDialog::setFrameCaptureEnabled(bool enabled)
{
    if (!mainWindow || !mainWindow->getEmuInstance())
        return false;

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
        return false;

    std::string status;
    emuThread->borrowGL();
    const bool ok = nds->GPU.GetRenderer().SetTextureScalingDebugFrameCaptureEnabled(enabled, &status);
    emuThread->returnGL();

    if (!ok && !status.empty())
        setStatusText(QString::fromStdString(status));
    return ok;
}

void TextureScalingDebugDialog::pollForNewMiss()
{
    setCaptureEnabled(true);
    updateStatsImpl(false);
}

void TextureScalingDebugDialog::updateStats()
{
    updateStatsImpl(true);
}

void TextureScalingDebugDialog::captureFrameTextures()
{
    if (!mainWindow || !mainWindow->getEmuInstance())
    {
        setStatusText("No active emulator window.");
        return;
    }

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
    {
        setStatusText("The emulator is not actively rendering.");
        return;
    }

    currentFrameTextures.clear();
    displayedFrameTextureSequence = 0;
    updateFrameTextureList();
    txtFrameTextureInfo->setPlainText("Waiting for the next rendered 3D frame...");

    if (!setFrameCaptureEnabled(true))
        return;

    if (emuThread->emuIsRunning())
    {
        QEventLoop waitForFrame;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &waitForFrame, &QEventLoop::quit);
        connect(emuThread, &EmuThread::windowUpdate, &waitForFrame, &QEventLoop::quit);
        timeout.start(250);
        waitForFrame.exec();
    }

    melonDS::TextureScalingDebugFrameTextures frame;
    std::string status;
    emuThread->borrowGL();
    const bool ok = nds->GPU.GetRenderer().ReadTextureScalingDebugFrameTextures(frame, &status);
    emuThread->returnGL();

    setStatusText(QString::fromStdString(status));
    if (!ok)
        return;

    if (!frame.Valid)
    {
        txtFrameTextureInfo->setPlainText(frame.CapturePending || frame.CaptureActive
            ? "Frame texture capture is still waiting for a rendered 3D frame."
            : "No frame texture capture is available.");
        return;
    }

    displayedFrameTextureSequence = frame.Sequence;
    currentFrameTextures = std::move(frame.Textures);
    updateFrameTextureList();

    if (currentFrameTextures.empty())
        txtFrameTextureInfo->setPlainText("Captured frame contains no regular texture-cache textures.");
    else
        displayFrameTexture(0);
}

void TextureScalingDebugDialog::updateStatsImpl(bool force)
{
    if (!mainWindow || !mainWindow->getEmuInstance())
    {
        displayedMissSequence = 0;
        currentText.clear();
        txtStats->clear();
        txtMissInfo->clear();
        currentFrameTextures.clear();
        displayedFrameTextureSequence = 0;
        updateFrameTextureList();
        txtFrameTextureInfo->setPlainText("Frame texture browser: no active emulator window.");
        currentSourceImage = QImage();
        currentResultImage = QImage();
        updatePreviewPixmaps();
        btnCopy->setEnabled(false);
        setStatusText("No active emulator window.");
        return;
    }

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
    {
        displayedMissSequence = 0;
        currentText.clear();
        txtStats->clear();
        txtMissInfo->clear();
        currentFrameTextures.clear();
        displayedFrameTextureSequence = 0;
        updateFrameTextureList();
        txtFrameTextureInfo->setPlainText("Frame texture browser: the emulator is not actively rendering.");
        currentSourceImage = QImage();
        currentResultImage = QImage();
        updatePreviewPixmaps();
        btnCopy->setEnabled(false);
        setStatusText("The emulator is not actively rendering.");
        return;
    }

    melonDS::TextureScalingDebugStats stats;
    std::string status;

    emuThread->borrowGL();
    const bool ok = nds->GPU.GetRenderer().ReadTextureScalingDebugStats(stats, &status);
    const bool sequenceChanged = ok && (stats.LastMissSequence != displayedMissSequence);
    melonDS::TextureScalingDebugLastMiss lastMiss;
    std::string missStatus;
    const bool needsMissSnapshot = force || sequenceChanged;
    const bool missOk = needsMissSnapshot
        ? nds->GPU.GetRenderer().ReadTextureScalingDebugLastMiss(lastMiss, &missStatus)
        : false;
    emuThread->returnGL();

    if (!ok)
    {
        displayedMissSequence = 0;
        currentText.clear();
        txtStats->clear();
        txtMissInfo->clear();
        currentFrameTextures.clear();
        displayedFrameTextureSequence = 0;
        updateFrameTextureList();
        txtFrameTextureInfo->setPlainText("Frame texture browser: unavailable for this renderer.");
        currentSourceImage = QImage();
        currentResultImage = QImage();
        lblSourcePreview->SetEmptyText("Unavailable");
        lblResultPreview->SetEmptyText("Unavailable");
        updatePreviewPixmaps();
        btnCopy->setEnabled(false);
        setStatusText(QString::fromStdString(status));
        return;
    }

    if (!force && !sequenceChanged)
        return;

    setStatusText(QString::fromStdString(status));
    currentText = FormatStats(stats);
    txtStats->setPlainText(currentText);
    btnCopy->setEnabled(true);
    displayedMissSequence = stats.LastMissSequence;

    if (needsMissSnapshot && missOk && lastMiss.Valid)
    {
        txtMissInfo->setPlainText(FormatLastMiss(lastMiss));

        if (!lastMiss.SourceRGBA.empty() && lastMiss.SourceWidth > 0 && lastMiss.SourceHeight > 0)
        {
            QImage image(reinterpret_cast<const uchar*>(lastMiss.SourceRGBA.data()),
                         static_cast<int>(lastMiss.SourceWidth),
                         static_cast<int>(lastMiss.SourceHeight),
                         QImage::Format_RGBA8888);
            currentSourceImage = image.copy();
        }
        else
            currentSourceImage = QImage();

        if (!lastMiss.ResultRGBA.empty() && lastMiss.ResultWidth > 0 && lastMiss.ResultHeight > 0)
        {
            QImage image(reinterpret_cast<const uchar*>(lastMiss.ResultRGBA.data()),
                         static_cast<int>(lastMiss.ResultWidth),
                         static_cast<int>(lastMiss.ResultHeight),
                         QImage::Format_RGBA8888);
            currentResultImage = image.copy();
        }
        else
            currentResultImage = QImage();

        const QString previewPlaceholder = "No preview captured for this miss";
        lblSourcePreview->SetEmptyText(previewPlaceholder);
        lblResultPreview->SetEmptyText(previewPlaceholder);
    }
    else if (needsMissSnapshot)
    {
        txtMissInfo->setPlainText(QString::fromStdString(missStatus));
        currentSourceImage = QImage();
        currentResultImage = QImage();
        lblSourcePreview->SetEmptyText("Unavailable");
        lblResultPreview->SetEmptyText("Unavailable");
    }

    updatePreviewPixmaps();
}

void TextureScalingDebugDialog::updateFrameTextureList()
{
    cbFrameTexture->blockSignals(true);
    cbFrameTexture->clear();

    for (int i = 0; i < static_cast<int>(currentFrameTextures.size()); i++)
        cbFrameTexture->addItem(FormatFrameTextureLabel(currentFrameTextures[i], i), i);

    cbFrameTexture->setEnabled(!currentFrameTextures.empty());
    if (!currentFrameTextures.empty())
        cbFrameTexture->setCurrentIndex(0);
    cbFrameTexture->blockSignals(false);
}

void TextureScalingDebugDialog::selectFrameTexture(int index)
{
    if (index < 0 || index >= static_cast<int>(currentFrameTextures.size()))
        return;

    displayFrameTexture(index);
}

void TextureScalingDebugDialog::displayFrameTexture(int index)
{
    if (index < 0 || index >= static_cast<int>(currentFrameTextures.size()))
        return;

    const auto& texture = currentFrameTextures[index];
    txtFrameTextureInfo->setPlainText(FormatFrameTexture(texture, index, static_cast<int>(currentFrameTextures.size())));

    if (!texture.SourceRGBA.empty() && texture.SourceWidth > 0 && texture.SourceHeight > 0)
    {
        QImage image(reinterpret_cast<const uchar*>(texture.SourceRGBA.data()),
                     static_cast<int>(texture.SourceWidth),
                     static_cast<int>(texture.SourceHeight),
                     QImage::Format_RGBA8888);
        currentSourceImage = image.copy();
    }
    else
        currentSourceImage = QImage();

    if (!texture.ResultRGBA.empty() && texture.ResultWidth > 0 && texture.ResultHeight > 0)
    {
        QImage image(reinterpret_cast<const uchar*>(texture.ResultRGBA.data()),
                     static_cast<int>(texture.ResultWidth),
                     static_cast<int>(texture.ResultHeight),
                     QImage::Format_RGBA8888);
        currentResultImage = image.copy();
    }
    else
        currentResultImage = QImage();

    lblSourcePreview->SetEmptyText("No source preview captured for this texture");
    lblResultPreview->SetEmptyText("No result preview captured for this texture");
    updatePreviewPixmaps();
}

void TextureScalingDebugDialog::resetStats()
{
    if (!mainWindow || !mainWindow->getEmuInstance())
        return;

    auto* nds = mainWindow->getEmuInstance()->getNDS();
    if (!emuThread || !nds || !emuThread->emuIsActive())
        return;

    std::string status;
    emuThread->borrowGL();
    const bool ok = nds->GPU.GetRenderer().ResetTextureScalingDebugStats(&status);
    emuThread->returnGL();

    setStatusText(QString::fromStdString(status));
    if (ok)
    {
        displayedMissSequence = 0;
        updateStatsImpl(true);
    }
}

void TextureScalingDebugDialog::copyStats()
{
    if (currentText.isEmpty())
        return;

    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(currentText);
        setStatusText("Copied 3D texture scaling diagnostics to the clipboard.");
    }
}

void TextureScalingDebugDialog::updatePreviewPixmaps()
{
    lblSourcePreview->SetImage(currentSourceImage);
    lblResultPreview->SetImage(currentResultImage);
}
