// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RENDERERTEST_H
#define RENDERERTEST_H

#include <QObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <vector>

#include "RendererDebug.h"
#include "RendererTestEnvironment.h"

class EmuInstance;

// One scheduled input step of a renderer replay test. Touch/key changes are
// applied at the start of the step's first frame, before that frame's input
// is applied, so replays are frame-exact.
struct RendererTestStep
{
    int frames = 0;
    bool hasTouch = false;
    int touchX = 0;
    int touchY = 0;
    bool release = false;
    bool hasKeys = false;
    melonDS::u32 keyMask = 0xFFF;
    bool releaseKeys = false;
};

struct RendererTestCapturedDebugView
{
    int screen = -1;
    melonDS::WholeScene2DDebugView view = melonDS::WholeScene2DDebugView::NativeFinal;
    QString name;
    QString status;
    QString exportedFile;
    int width = 0;
    int height = 0;
    std::vector<melonDS::u32> rgba;
    bool captured = false;
    double readCachedStatusMs = 0.0;
    double readWithStatusMs = 0.0;
    double readWithoutStatusMs = 0.0;
    double readWithStatusRepeatMs = 0.0;
    bool profilePixelsMatch = false;
};

struct RendererTestFrameCapture
{
    qulonglong frame = 0;
    bool finalTop = false;
    bool finalBottom = false;
    QVector<melonDS::WholeScene2DDebugView> debugViews;
    QVector<QString> debugViewNames;

    bool debugViewsArmed = false;
    bool frameObserved = false;
    bool finalCaptured = false;
    bool captureComplete = false;
    bool exportComplete = false;
    QString error;
    QStringList exportedFiles;
    melonDS::WholeScene2DFinalDebugFrame finalFrame;
    QVector<RendererTestCapturedDebugView> capturedDebugViews;
};

// Replay harness for renderer test cases (melonDS --renderer-test case.json).
// Loads a ROM and optional savestate, runs a scripted frame schedule, and
// captures the whole-scene timing/renderer-details CSVs (and optionally a
// rolling debug frame dump) without GUI interaction. Supply a manifest with
// paths to your own ROM and optional savestate.
//
// Manifest format:
// {
//   "rom": "path/to/game.nds",               (required)
//   "savestate": "path/to/state.mln",        (optional)
//   "purpose": "comparison",                 (default; also performance/diagnostic)
//   "rtc": { "mode": "savestate" },          (default with a state; otherwise fixed)
//     For fixed time: { "mode": "fixed", "dateTime": "2000-01-01T12:00:00" }
//     Host time requires purpose=diagnostic. Replay clocks advance in emulated time.
//   "warmupFrames": 30,                       (optional, default 0)
//   "steps": [                                (optional)
//     { "frames": 12 },
//     { "touch": [120, 80], "frames": 1 },
//     { "release": true, "frames": 90 },
//     { "keys": ["Start"], "frames": 1 },
//     { "releaseKeys": true, "frames": 90 }
//   ],
//   "captures": {
//     "wholeSceneCsv": true,                  (default true)
//     "captureLumaProbe": false,              (default false; true requires diagnostic)
//     "testDebugProductValidity": false,      (optional; full-frame diagnostic lifecycle tests)
//     "testDebugFrameHandoff": false,         (optional; GL ownership/full dump tests)
//     "profileDebugReads": false,             (optional; extra reads/timings)
//     "rollingDump": false,                   (default false; needs OpenGL)
//     "frames": [                             (optional; needs OpenGL)
//       {
//         "frame": 605,
//         "surfaces": ["finalTop", "finalBottom"],
//         "debugViews": ["HybridSelector", "HybridForegroundAlpha"]
//       }
//     ]
//   },
//   "expectations": "case.expectations.json",  (optional; relative paths are
//                                                 resolved beside the manifest)
//   "outputDir": "..."                        (optional; default
//                       <manifest dir>/Test Exports/<manifest stem>/<timestamp>)
// }
class RendererTestRunner : public QObject
{
    Q_OBJECT

public:
    // Returns nullptr and sets errorstr on a malformed manifest.
    static RendererTestRunner* fromManifest(const QString& manifestPath, QString& errorstr);

    QString romPath() const { return rom; }
    bool failed() const { return runFailed; }
    bool deterministicRTC() const { return environment.deterministicRTC(); }
    QDateTime bootRTCSeed() const { return environment.fixedDateTime; }
    void recordHostRTCWrite() { hostRTCWrites.fetch_add(1, std::memory_order_relaxed); }

    // UI thread, after the emu instance exists and the ROM boot was requested.
    void begin(EmuInstance* inst);

    // Emu thread, once per emulated frame, before the frame's input is applied.
    void onEmuFrame(EmuInstance* inst);

    // Emu thread, after drawScreen presented the timing frame while the GL
    // context is still current.
    void onFramePresented(EmuInstance* inst, qulonglong timingFrame, bool timingFrameValid);

signals:
    void scheduleFinished();

private slots:
    void onEmuStart();
    void onScheduleDone();

private:
    explicit RendererTestRunner(QObject* parent = nullptr);

    bool advanceStep(EmuInstance* inst);
    RendererTestFrameCapture* captureForFrame(qulonglong frame);
    void armExactFrameCapture(EmuInstance* inst, qulonglong frame);
    void writeExactFrameCaptures();
    void checkDebugProductValidity();
    void fail(const QString& reason);
    void finishRun();
    void writeVerdict();
    QString checkEnvironment(EmuInstance* inst);
    bool validateFrameEnvironment(EmuInstance* inst);

    QString manifestPath;
    QString rom;
    QString savestate;
    QString outputDir;
    RendererTestEnvironment environment;
    QJsonObject environmentRecord;
    bool environmentStarted = false;
    melonDS::u64 initialProbeReadbacks = 0;
    std::atomic<melonDS::u64> hostRTCWrites { 0 };
    std::atomic<melonDS::u64> observedProbeReadbacks { 0 };
    std::atomic<melonDS::u64> environmentFramesChecked { 0 };
    std::atomic_bool environmentViolation { false };
    int warmupFrames = 0;
    bool captureWholeSceneCsv = true;
    bool rollingDumpAtEnd = false;
    bool profileDebugReads = false;
    bool testDebugFrameHandoff = false;
    bool testDebugProductValidity = false;
    QJsonArray debugProductChecks;
    QVector<RendererTestStep> steps;
    QVector<RendererTestFrameCapture> exactFrameCaptures;
    QString expectationsPath;
    QJsonArray assertionSpecs;
    QJsonArray assertionResults;
    int assertionsPassed = 0;
    int assertionsFailed = 0;

    EmuInstance* emuInstance = nullptr;
    std::atomic_bool armed { false };
    std::atomic_bool scheduleDone { false };
    std::atomic<long long> framesRun { 0 };

    // Emu-thread schedule state; touched only after `armed` is set.
    int currentStep = -1;
    int framesRemaining = 0;
    bool keyScheduleActive = false;
    melonDS::u32 scheduledKeyMask = 0xFFF;

    bool finished = false;
    bool runFailed = false;
    bool exactFrameCapturesWritten = false;
    QString failReason;
    QString timingCsvPath;
};

#endif // RENDERERTEST_H
