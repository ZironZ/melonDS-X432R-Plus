/*
    Copyright 2026 ZironZ

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

#ifndef RENDERERTEST_H
#define RENDERERTEST_H

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

class EmuInstance;

// One scheduled input step of a renderer replay test. Touch (or release) is
// applied at the start of the step's first frame, before that frame's input
// is processed, so replays are frame-exact.
struct RendererTestStep
{
    int frames = 0;
    bool hasTouch = false;
    int touchX = 0;
    int touchY = 0;
    bool release = false;
};

// Replay harness for renderer test cases (melonDS --renderer-test case.json).
// Loads a ROM and optional savestate, runs a scripted frame schedule, and
// captures the whole-scene timing/renderer-details CSVs (and optionally a
// rolling debug frame dump) without GUI interaction. The manifest is local
// user data (ROM/savestate paths), not repo data.
//
// Manifest format:
// {
//   "rom": "I:/path/to/game.nds",            (required)
//   "savestate": "I:/path/to/state.mln",     (optional)
//   "warmupFrames": 30,                       (optional, default 0)
//   "steps": [                                (optional)
//     { "frames": 12 },
//     { "touch": [120, 80], "frames": 1 },
//     { "release": true, "frames": 90 }
//   ],
//   "captures": {
//     "wholeSceneCsv": true,                  (default true)
//     "rollingDump": false                    (default false; needs OpenGL)
//   },
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

    // UI thread, after the emu instance exists and the ROM boot was requested.
    void begin(EmuInstance* inst);

    // Emu thread, once per emulated frame, before the frame's input is applied.
    void onEmuFrame(EmuInstance* inst);

signals:
    void scheduleFinished();

private slots:
    void onEmuStart();
    void onScheduleDone();

private:
    explicit RendererTestRunner(QObject* parent = nullptr);

    bool advanceStep(EmuInstance* inst);
    void fail(const QString& reason);
    void finishRun();
    void writeVerdict();

    QString manifestPath;
    QString rom;
    QString savestate;
    QString outputDir;
    int warmupFrames = 0;
    bool captureWholeSceneCsv = true;
    bool rollingDumpAtEnd = false;
    QVector<RendererTestStep> steps;

    EmuInstance* emuInstance = nullptr;
    std::atomic_bool armed { false };
    std::atomic_bool scheduleDone { false };
    std::atomic<long long> framesRun { 0 };

    // Emu-thread schedule state; touched only after `armed` is set.
    int currentStep = -1;
    int framesRemaining = 0;

    bool finished = false;
    bool runFailed = false;
    QString failReason;
    QString timingCsvPath;
};

#endif // RENDERERTEST_H
