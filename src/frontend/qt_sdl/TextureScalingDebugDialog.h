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
