// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WholeScene2DDebugExport.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

using namespace WholeSceneDebugExport;

void Check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

QByteArray Read(const QString& path)
{
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly), "read output");
    return file.readAll();
}

Snapshot MakeSnapshot(const QString& directory)
{
    Snapshot snapshot;
    snapshot.Directory = directory;
    snapshot.Header = "Test snapshot\nTiming frame: 42\n";
    snapshot.ViewsInSubdirectory = true;
    snapshot.IncludeFinalEvidence = true;
    snapshot.CurrentFinalAvailable = true;
    auto& frame = snapshot.CurrentFinal;
    frame.Width = frame.Height = 2;
    frame.Serial = 7;
    frame.TimingFrameValid = true;
    frame.TimingFrame = 42;
    frame.TopRGBA = {0xff0000ff, 0xff00ff00, 0xffff0000, 0};
    frame.BottomRGBA = {0xffffffff, 0xff030201, 0xff060504, 0x80706050};
    snapshot.RollingFrames.push_back(frame);

    CapturedView view;
    view.Available = true;
    view.Width = view.Height = 2;
    view.FileStem = "test-color";
    view.Status = "Exact status\nsecond line";
    view.Image = QImage(reinterpret_cast<const uchar*>(frame.TopRGBA.data()),
                        2, 2, QImage::Format_RGBA8888).copy();
    snapshot.Views.push_back(view);
    view.Available = false;
    view.FileStem = "unavailable";
    view.Status = "Deliberately unavailable";
    snapshot.Views.push_back(view);
    return snapshot;
}

void Verify(const Snapshot& snapshot)
{
    QDir directory(snapshot.Directory);
    for (const auto& view : snapshot.Views)
    {
        const auto path = directory.filePath("views/" + view.FileStem + ".png");
        if (!view.Available)
            Check(!QFile::exists(path), "unavailable view skipped");
        else
            Check(QImage(path).convertToFormat(QImage::Format_RGBA8888) ==
                  view.Image.convertToFormat(QImage::Format_RGBA8888), "PNG pixel preservation");
    }
    Check(QImage(directory.filePath("current-final/current-final-top.png"))
              .pixelColor(0, 0) == QColor(255, 0, 0), "final channels");
    Check(QImage(directory.filePath("rolling-final/rolling-000-serial000007-bottom.png"))
              .pixelColor(1, 1) == QColor(80, 96, 112, 128), "rolling alpha/channels");
    for (const char* name : {"affine-obj-summary.csv", "affine-obj-sprites.csv",
                            "affine-obj-groups.csv", "ordinary-obj-band-summary.csv",
                            "ordinary-obj-band-members.csv", "ordinary-obj-bands.csv"})
        Check(!Read(directory.filePath(name)).isEmpty(), "evidence CSV retained");

    const auto manifest = Read(directory.filePath("manifest.txt")).replace("\r\n", "\n");
    for (const char* text : {"Timing frame: 42", "Exact status\n  second line",
                            "Deliberately unavailable", "Export timings"})
        Check(manifest.contains(text), "manifest retained");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    Check(temporary.isValid(), "temporary directory");
    auto snapshot = MakeSnapshot(temporary.path() + "/async");
    if (app.arguments().size() == 3 && app.arguments()[1] == "--fixture")
    {
        QDir source(app.arguments()[2] + "/views");
        const auto files = source.entryList({"*.png"}, QDir::Files);
        Check(!files.empty(), "fixture contains debug views");
        for (const auto& name : files)
        {
            CapturedView view;
            view.FileStem = name.left(name.size() - 4);
            view.Image = QImage(source.filePath(name));
            Check(!view.Image.isNull(), "load fixture image");
            view.Width = view.Image.width();
            view.Height = view.Image.height();
            view.Available = true;
            snapshot.Views.push_back(std::move(view));
        }
    }

    // Application shutdown joins the worker even before its queued callback runs.
    if (app.arguments().contains("--shutdown-test"))
    {
        Job job;
        Check(bool(job), "reserve shutdown job");
        job.start(snapshot, &app, [](const Result&) {});
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
        app.exec();
        Verify(snapshot);
        std::puts("PASS shutdown drain");
        return 0;
    }
    {
        Job first;
        Check(bool(first), "reserve capture");
        Job second;
        Check(!second, "reject nested capture");
    }

    int ticks = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
    heartbeat.start(1);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app, [] { Check(false, "export timeout"); });
    timeout.start(120000);

    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    Job job;
    Check(bool(job), "abandoned reservation released");
    job.start(snapshot, &app, [&](const Result& result) {
        Check(QThread::currentThread() == app.thread(), "UI-thread completion");
        Check(result.Success, qPrintable(result.Message));
        loop.quit();
    });
    {
        Job duplicate;
        Check(!duplicate, "bounded queue while writing");
    }
    loop.exec();
    std::printf("Async: %lld ms, %d UI heartbeats, %zu views\n",
                static_cast<long long>(elapsed.elapsed()), ticks, snapshot.Views.size());
    if (snapshot.Views.size() > 2)
        Check(ticks > 0, "UI advances during full export");
    Verify(snapshot);

    auto failed = MakeSnapshot(temporary.path() + "/failure");
    QDir().mkpath(failed.Directory + "/manifest.txt");
    Check(!Write(failed).Success, "manifest failure reported");
    failed.Directory = temporary.path() + "/png-failure";
    QDir().mkpath(failed.Directory + "/views/test-color.png");
    Check(!Write(failed).Success, "PNG failure reported");
    failed.Directory = temporary.path() + "/csv-failure";
    QDir().mkpath(failed.Directory + "/affine-obj-summary.csv");
    Check(!Write(failed).Success, "CSV failure reported");
    failed.Directory = temporary.path() + "/incomplete-frame";
    failed.CurrentFinal.TopRGBA.clear();
    Check(!Write(failed).Success, "incomplete pixel data reported");

    // Closing a window suppresses its callback, but must not abandon the export.
    auto* recipient = new QObject;
    auto orphan = MakeSnapshot(temporary.path() + "/orphan");
    Job orphanJob;
    Check(bool(orphanJob), "completed reservation released");
    orphanJob.start(orphan, recipient, [](const Result&) { Check(false, "stale recipient"); });
    delete recipient;
    QEventLoop wait;
    QTimer probe;
    QObject::connect(&probe, &QTimer::timeout, &app, [&] {
        Job available;
        if (available)
            wait.quit();
    });
    probe.start(1);
    wait.exec();
    Verify(orphan);
    std::puts("PASS pixels/metadata, failures, queue bound and recipient lifetime");
}
