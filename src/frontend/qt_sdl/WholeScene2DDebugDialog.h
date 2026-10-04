// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WHOLESCENE2DDEBUGDIALOG_H
#define WHOLESCENE2DDEBUGDIALOG_H

#include "WholeScene2DDebugExport.h"
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
namespace melonDS { class Renderer; }

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
    using CapturedView = WholeSceneDebugExport::CapturedView;

    static bool dumpFrame(MainWindow* parent, const QString& timingCsvPath,
                          qulonglong timingFrame, bool includeViews,
                          QString* exportPath, QString* errorText);
    static std::vector<CapturedView> captureViews(melonDS::Renderer& renderer, MainWindow* window);
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
