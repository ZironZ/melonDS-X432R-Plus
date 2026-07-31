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

#include <stdio.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>

#include "RendererTest.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "Window.h"

RendererTestRunner::RendererTestRunner(QObject* parent) : QObject(parent)
{
}

RendererTestRunner* RendererTestRunner::fromManifest(const QString& manifestPath, QString& errorstr)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        errorstr = QString("Could not open manifest %1: %2").arg(manifestPath, file.errorString());
        return nullptr;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject())
    {
        errorstr = QString("Manifest %1 is not valid JSON: %2").arg(manifestPath, parseError.errorString());
        return nullptr;
    }

    const QJsonObject root = doc.object();

    auto* runner = new RendererTestRunner();
    runner->manifestPath = QFileInfo(manifestPath).absoluteFilePath();

    runner->rom = root.value("rom").toString();
    if (runner->rom.isEmpty())
    {
        errorstr = QString("Manifest %1 is missing the required \"rom\" field.").arg(manifestPath);
        delete runner;
        return nullptr;
    }
    if (!QFileInfo::exists(runner->rom))
    {
        errorstr = QString("ROM %1 does not exist.").arg(runner->rom);
        delete runner;
        return nullptr;
    }

    runner->savestate = root.value("savestate").toString();
    if (!runner->savestate.isEmpty() && !QFileInfo::exists(runner->savestate))
    {
        errorstr = QString("Savestate %1 does not exist.").arg(runner->savestate);
        delete runner;
        return nullptr;
    }

    runner->warmupFrames = root.value("warmupFrames").toInt(0);
    if (runner->warmupFrames < 0)
        runner->warmupFrames = 0;

    for (const QJsonValue& stepValue : root.value("steps").toArray())
    {
        const QJsonObject stepObject = stepValue.toObject();
        RendererTestStep step;
        step.frames = stepObject.value("frames").toInt(0);
        if (step.frames < 0)
            step.frames = 0;
        const QJsonArray touch = stepObject.value("touch").toArray();
        if (touch.size() == 2)
        {
            step.hasTouch = true;
            step.touchX = touch[0].toInt();
            step.touchY = touch[1].toInt();
        }
        step.release = stepObject.value("release").toBool(false);
        runner->steps.push_back(step);
    }

    const QJsonObject captures = root.value("captures").toObject();
    runner->captureWholeSceneCsv = captures.value("wholeSceneCsv").toBool(true);
    runner->rollingDumpAtEnd = captures.value("rollingDump").toBool(false);

    runner->outputDir = root.value("outputDir").toString();
    if (runner->outputDir.isEmpty())
    {
        const QFileInfo manifestInfo(runner->manifestPath);
        runner->outputDir = QString("%1/Test Exports/%2/%3")
            .arg(manifestInfo.dir().path(),
                 manifestInfo.completeBaseName(),
                 QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    }

    return runner;
}

void RendererTestRunner::begin(EmuInstance* inst)
{
    emuInstance = inst;

    connect(inst->getEmuThread(), &EmuThread::windowEmuStart,
            this, &RendererTestRunner::onEmuStart, Qt::QueuedConnection);
    connect(this, &RendererTestRunner::scheduleFinished,
            this, &RendererTestRunner::onScheduleDone, Qt::QueuedConnection);

    // Watchdog: a run that never boots or stalls must still terminate with a
    // verdict. Budget the schedule at a pessimistic 30 fps plus startup slack.
    long long totalFrames = warmupFrames;
    for (const RendererTestStep& step : steps)
        totalFrames += step.frames;
    const int watchdogMS = static_cast<int>(totalFrames * 1000 / 30 + 120000);
    QTimer::singleShot(watchdogMS, this, [this]() {
        if (!finished)
        {
            fail(QString("Watchdog timed out after %1 frames run.").arg(framesRun.load()));
            finishRun();
        }
    });

    printf("[renderer-test] case %s\n", manifestPath.toUtf8().constData());
    printf("[renderer-test] output %s\n", outputDir.toUtf8().constData());
}

void RendererTestRunner::onEmuFrame(EmuInstance* inst)
{
    if (!armed.load(std::memory_order_acquire) ||
        scheduleDone.load(std::memory_order_relaxed))
    {
        return;
    }

    while (framesRemaining <= 0)
    {
        if (!advanceStep(inst))
        {
            scheduleDone.store(true, std::memory_order_release);
            emit scheduleFinished();
            return;
        }
    }

    framesRemaining--;
    framesRun.fetch_add(1, std::memory_order_relaxed);
}

bool RendererTestRunner::advanceStep(EmuInstance* inst)
{
    currentStep++;
    if (currentStep >= steps.size())
    {
        inst->releaseScreen();
        return false;
    }

    const RendererTestStep& step = steps[currentStep];
    if (step.hasTouch)
        inst->touchScreen(step.touchX, step.touchY);
    else if (step.release)
        inst->releaseScreen();
    framesRemaining = step.frames;
    return true;
}

void RendererTestRunner::onEmuStart()
{
    if (armed.load(std::memory_order_relaxed) || finished)
        return;

    EmuThread* emuThread = emuInstance->getEmuThread();

    if (!QDir().mkpath(outputDir))
    {
        fail(QString("Could not create output directory %1.").arg(outputDir));
        finishRun();
        return;
    }

    if (!savestate.isEmpty())
    {
        if (!emuThread->loadState(savestate))
        {
            fail(QString("Failed to load savestate %1.").arg(savestate));
            finishRun();
            return;
        }
        printf("[renderer-test] savestate loaded: %s\n", savestate.toUtf8().constData());
    }

    if (captureWholeSceneCsv)
    {
        timingCsvPath = outputDir + "/whole-scene-frame-times.csv";
        QString errorstr;
        if (!emuThread->startWholeSceneTimingLog(timingCsvPath, errorstr))
        {
            fail(QString("Could not start whole-scene timing log: %1").arg(errorstr));
            finishRun();
            return;
        }
    }

    if (rollingDumpAtEnd)
    {
        // The MainWindow slot owns the GL borrow needed to (de)activate the
        // rolling capture ring; reuse it instead of duplicating that dance.
        QMetaObject::invokeMethod(emuInstance->getMainWindow(),
                                  "onToggleWholeScene2DRollingDebugCapture",
                                  Qt::DirectConnection,
                                  Q_ARG(bool, true));
    }

    currentStep = -1;
    framesRemaining = warmupFrames;
    armed.store(true, std::memory_order_release);
    printf("[renderer-test] armed: warmup %d, %d step(s)\n",
           warmupFrames, static_cast<int>(steps.size()));
}

void RendererTestRunner::onScheduleDone()
{
    if (finished)
        return;

    if (rollingDumpAtEnd)
    {
        QMetaObject::invokeMethod(emuInstance->getMainWindow(),
                                  "onDumpWholeScene2DRollingDebugFrames",
                                  Qt::DirectConnection);
    }

    finishRun();
}

void RendererTestRunner::fail(const QString& reason)
{
    runFailed = true;
    if (failReason.isEmpty())
        failReason = reason;
    printf("[renderer-test] FAIL: %s\n", reason.toUtf8().constData());
}

void RendererTestRunner::finishRun()
{
    if (finished)
        return;
    finished = true;

    EmuThread* emuThread = emuInstance ? emuInstance->getEmuThread() : nullptr;
    if (emuThread && emuThread->wholeSceneTimingLogActive())
        emuThread->stopWholeSceneTimingLog();

    writeVerdict();

    printf("[renderer-test] %s: %lld frame(s) run\n",
           runFailed ? "FAILED" : "COMPLETE",
           framesRun.load());

    if (emuInstance && emuInstance->getMainWindow())
        emuInstance->getMainWindow()->close();
}

void RendererTestRunner::writeVerdict()
{
    QJsonObject verdict;
    verdict["case"] = manifestPath;
    verdict["rom"] = rom;
    verdict["savestate"] = savestate;
    verdict["completed"] = !runFailed;
    verdict["failReason"] = failReason;
    verdict["framesRun"] = static_cast<qint64>(framesRun.load());
    verdict["warmupFrames"] = warmupFrames;
    verdict["steps"] = static_cast<int>(steps.size());
    verdict["wholeSceneCsv"] = captureWholeSceneCsv ? timingCsvPath : QString();
    verdict["rollingDumpRequested"] = rollingDumpAtEnd;
    verdict["assertions"] = QStringLiteral("none (capture-only harness)");

    QFile file(outputDir + "/verdict.json");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(verdict).toJson(QJsonDocument::Indented));
    else
        printf("[renderer-test] could not write verdict.json: %s\n",
               file.errorString().toUtf8().constData());
}
