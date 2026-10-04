// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QString>
#include <functional>
#include <vector>
#include "RendererDebug.h"

class QObject;

namespace WholeSceneDebugExport
{
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
    qint64 ReadMilliseconds = 0;
};

// CPU-owned data only: writing must never access an emulator or GL context.
struct Snapshot
{
    QString Directory;
    QString Header;
    bool ViewsInSubdirectory = false;
    bool IncludeFinalEvidence = false;
    bool CurrentFinalAvailable = false;
    melonDS::WholeScene2DFinalDebugFrame CurrentFinal;
    std::vector<melonDS::WholeScene2DFinalDebugFrame> RollingFrames;
    std::vector<CapturedView> Views;
};

struct Result
{
    bool Success = false;
    QString Directory;
    QString Message;
};
Result Write(const Snapshot& snapshot);
// UI-thread reservation includes capture, which may pump events. One job
// prevents repeated hotkeys from accumulating large queued snapshots.
class Job
{
public:
    Job();
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    explicit operator bool() const { return Reserved; }
    void start(Snapshot snapshot, QObject* recipient,
               std::function<void(const Result&)> completed);
private:
    bool Reserved;
};
}
