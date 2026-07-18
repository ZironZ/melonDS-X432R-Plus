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

#ifndef WHOLESCENE2DDEBUGDIALOG_H
#define WHOLESCENE2DDEBUGDIALOG_H

#include <QDialog>
#include <QImage>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class MainWindow;
class EmuThread;

class WholeScene2DDebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WholeScene2DDebugDialog(QWidget* parent);
    ~WholeScene2DDebugDialog() override;

    static WholeScene2DDebugDialog* currentDlg;
    static WholeScene2DDebugDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            currentDlg->raise();
            return currentDlg;
        }

        currentDlg = new WholeScene2DDebugDialog(parent);
        currentDlg->show();
        return currentDlg;
    }

    static void closeDlg()
    {
        currentDlg = nullptr;
    }

    static bool dumpCurrentFrame(MainWindow* parent,
                                 const QString& timingCsvPath,
                                 qulonglong timingFrame,
                                 QString* exportPath,
                                 QString* errorText);
    static bool dumpRollingFrames(MainWindow* parent,
                                  const QString& timingCsvPath,
                                  qulonglong timingFrame,
                                  QString* exportPath,
                                  QString* errorText);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void requestRefresh();
    void selectSnapshotView();
    void onRefreshTimer();
    void updatePreview();
    void copyPreview();
    void savePreview();
    void exportAllViews();
    void updateDebugPoison();

private:
    struct CapturedView
    {
        int Index = 0;
        int Screen = -1;
        int ViewValue = 0;
        QString ScreenName;
        QString Category;
        QString Label;
        QString FileStem;
        QString Status;
        QImage Image;
        int Width = 0;
        int Height = 0;
        bool Available = false;
    };

    void updatePreviewPixmap();
    void setStatusText(const QString& text);
    void setDetailsText(const QString& text);
    void setRendererStatusText(const QString& text);
    void populateViewList();
    void setRendererDebugViewsActive(bool active);
    bool captureRefreshSnapshot(QString* errorText = nullptr);
    bool displaySnapshotView(const QString& extraStatus = QString());
    const CapturedView* findSnapshotView(int screen, int viewValue) const;
    QString currentViewDescription() const;
    QString currentViewLabel() const;
    QString defaultExportFilename() const;

    MainWindow* mainWindow;
    EmuThread* emuThread;

    QComboBox* cbScreen;
    QComboBox* cbCategory;
    QComboBox* cbView;
    QCheckBox* cbAutoRefresh;
    QCheckBox* cbPoisonSource3D;
    QCheckBox* cbPoisonNative3DResolve;
    QCheckBox* cbPoisonNative3DResolveAlpha;
    QPushButton* btnRefresh;
    QPushButton* btnCopy;
    QPushButton* btnSave;
    QPushButton* btnExportAll;
    QLabel* lblDescription;
    QLabel* lblStatus;
    QPlainTextEdit* txtDetails;
    QLabel* lblPreview;
    QTimer* refreshTimer;

    QImage currentImage;
    std::vector<CapturedView> refreshSnapshot;
    QString refreshSnapshotStamp;
    QString refreshSnapshotScreenName;
    bool refreshSnapshotPoisonSource3D;
    bool refreshSnapshotPoisonNative3DResolve;
    bool refreshSnapshotPoisonNative3DResolveAlpha;
    bool pendingRefresh;
};

#endif // WHOLESCENE2DDEBUGDIALOG_H
