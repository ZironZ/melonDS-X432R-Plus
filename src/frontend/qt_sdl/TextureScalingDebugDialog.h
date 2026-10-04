// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTURESCALINGDEBUGDIALOG_H
#define TEXTURESCALINGDEBUGDIALOG_H

#include <QDialog>
#include <QImage>

#include "TextureScalingDebug.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTimer;
class QResizeEvent;
class MainWindow;
class EmuThread;
class TexturePreviewLabel;

class TextureScalingDebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TextureScalingDebugDialog(QWidget* parent);
    ~TextureScalingDebugDialog() override;

    static TextureScalingDebugDialog* currentDlg;
    static TextureScalingDebugDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            currentDlg->raise();
            return currentDlg;
        }

        currentDlg = new TextureScalingDebugDialog(parent);
        currentDlg->show();
        return currentDlg;
    }

    static void closeDlg()
    {
        currentDlg = nullptr;
    }

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void requestRefresh();
    void onRefreshTimer();
    void updateStats();
    void resetStats();
    void copyStats();
    void captureFrameTextures();
    void selectFrameTexture(int index);

private:
    void pollForNewMiss();
    void updateStatsImpl(bool force);
    void updatePreviewPixmaps();
    void updateFrameTextureList();
    void displayFrameTexture(int index);
    void setStatusText(const QString& text);
    bool setCaptureEnabled(bool enabled);
    bool setFrameCaptureEnabled(bool enabled);

    MainWindow* mainWindow;
    EmuThread* emuThread;

    QCheckBox* cbAutoRefresh;
    QComboBox* cbFrameTexture;
    QPushButton* btnRefresh;
    QPushButton* btnReset;
    QPushButton* btnCopy;
    QPushButton* btnCaptureFrame;
    QLabel* lblStatus;
    QPlainTextEdit* txtStats;
    QPlainTextEdit* txtMissInfo;
    QPlainTextEdit* txtFrameTextureInfo;
    TexturePreviewLabel* lblSourcePreview;
    TexturePreviewLabel* lblResultPreview;
    QTimer* refreshTimer;

    QString currentText;
    QImage currentSourceImage;
    QImage currentResultImage;
    std::vector<melonDS::TextureScalingDebugFrameTexture> currentFrameTextures;
    melonDS::u64 displayedMissSequence;
    melonDS::u64 displayedFrameTextureSequence;
};

#endif // TEXTURESCALINGDEBUGDIALOG_H
