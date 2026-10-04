// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QTimer>

#include "RendererTest.h"
#include "RendererTestAssertions.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "GPU.h"
#include "WholeSceneScalePolicy.h"
#include "Window.h"
#include "WholeScene2DDebugDialog.h"

namespace
{
struct NamedDebugView
{
    const char* Name;
    melonDS::WholeScene2DDebugView View;
};

#define RENDERER_TEST_DEBUG_VIEW(name) {#name, melonDS::WholeScene2DDebugView::name}
constexpr NamedDebugView DebugViews[] = {
    RENDERER_TEST_DEBUG_VIEW(NativeFinal),
    RENDERER_TEST_DEBUG_VIEW(NativeExactFinal),
    RENDERER_TEST_DEBUG_VIEW(Native3DResolve),
    RENDERER_TEST_DEBUG_VIEW(Native3DSemantics),
    RENDERER_TEST_DEBUG_VIEW(FinalNative3DInput),
    RENDERER_TEST_DEBUG_VIEW(NativeTopColor),
    RENDERER_TEST_DEBUG_VIEW(NativeSecondColor),
    RENDERER_TEST_DEBUG_VIEW(NativeMeta),
    RENDERER_TEST_DEBUG_VIEW(NativeBG0Color),
    RENDERER_TEST_DEBUG_VIEW(NativeBG1Color),
    RENDERER_TEST_DEBUG_VIEW(NativeBG2Color),
    RENDERER_TEST_DEBUG_VIEW(NativeBG3Color),
    RENDERER_TEST_DEBUG_VIEW(NativeOBJColor),
    RENDERER_TEST_DEBUG_VIEW(NativeOBJFlags),
    RENDERER_TEST_DEBUG_VIEW(NativeOBJCoverage),
    RENDERER_TEST_DEBUG_VIEW(Native3DStackRole),
    RENDERER_TEST_DEBUG_VIEW(Upscaled3DStackRole),
    RENDERER_TEST_DEBUG_VIEW(UpscaledTopColor),
    RENDERER_TEST_DEBUG_VIEW(UpscaledSecondColor),
    RENDERER_TEST_DEBUG_VIEW(UpscaledMeta),
    RENDERER_TEST_DEBUG_VIEW(UpscaledCoverage),
    RENDERER_TEST_DEBUG_VIEW(HighResBG0Color),
    RENDERER_TEST_DEBUG_VIEW(HighResBG1Color),
    RENDERER_TEST_DEBUG_VIEW(HighResBG2Color),
    RENDERER_TEST_DEBUG_VIEW(HighResBG3Color),
    RENDERER_TEST_DEBUG_VIEW(HighResBG0Meta),
    RENDERER_TEST_DEBUG_VIEW(HighResBG1Meta),
    RENDERER_TEST_DEBUG_VIEW(HighResBG2Meta),
    RENDERER_TEST_DEBUG_VIEW(HighResBG3Meta),
    RENDERER_TEST_DEBUG_VIEW(HighResOBJColor),
    RENDERER_TEST_DEBUG_VIEW(HighResOBJFlags),
    RENDERER_TEST_DEBUG_VIEW(HighResOBJCoverage),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineCandidate),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineNativeReference),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineDownsampleDifference),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineEnhancedBG2Source),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineEnhancedBG3Source),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineEnhancedOBJSource),
    RENDERER_TEST_DEBUG_VIEW(NativeOBJSourceAtlas),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineEnhancedOBJSourceAtlas),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineAssembledOBJSourceAtlas),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineTransformedOBJColor),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineTransformedOBJCoverage),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOrdinaryOBJBand0Color),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOrdinaryOBJBand0Coverage),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOrdinaryOBJBand0Scaled),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineResolvedOrdinaryOBJ),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOverlapSemantic),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOverlapUnderlay),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOverlapDecisions),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOverlapSubpixelColor),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOverlapSubpixelFlags),
    RENDERER_TEST_DEBUG_VIEW(StrictAffineOverlapSubpixelCoverage),
    RENDERER_TEST_DEBUG_VIEW(Direct3D),
    RENDERER_TEST_DEBUG_VIEW(OverlayOperatorColor),
    RENDERER_TEST_DEBUG_VIEW(OverlayUnderlayWeight),
    RENDERER_TEST_DEBUG_VIEW(OverlayReconstructedNative),
    RENDERER_TEST_DEBUG_VIEW(OverlayReconstructionError),
    RENDERER_TEST_DEBUG_VIEW(OverlayValidityConfidence),
    RENDERER_TEST_DEBUG_VIEW(OverlayOwnershipReason),
    RENDERER_TEST_DEBUG_VIEW(HybridSelector),
    RENDERER_TEST_DEBUG_VIEW(HybridCoverageMiss),
    RENDERER_TEST_DEBUG_VIEW(HybridForegroundAlpha),
    RENDERER_TEST_DEBUG_VIEW(HybridFinalSource),
    RENDERER_TEST_DEBUG_VIEW(SandwichLower2D),
    RENDERER_TEST_DEBUG_VIEW(SandwichUpper2D),
    RENDERER_TEST_DEBUG_VIEW(SandwichEligibility),
    RENDERER_TEST_DEBUG_VIEW(OverlayEnhancedUnderlay),
    RENDERER_TEST_DEBUG_VIEW(OverlayTrueNativeFinal),
    RENDERER_TEST_DEBUG_VIEW(OverlayFinalResult),
    RENDERER_TEST_DEBUG_VIEW(FinalTop),
    RENDERER_TEST_DEBUG_VIEW(FinalBottom),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayRaw),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayRawBank0),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayRawBank1),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayRawBank2),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayRawBank3),
    RENDERER_TEST_DEBUG_VIEW(CaptureOutput256Bank0),
    RENDERER_TEST_DEBUG_VIEW(CaptureOutput256Bank1),
    RENDERER_TEST_DEBUG_VIEW(CaptureOutput256Bank2),
    RENDERER_TEST_DEBUG_VIEW(CaptureOutput256Bank3),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureFullBank0),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureFullBank1),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureFullBank2),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureFullBank3),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureBackgroundBank0),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureBackgroundBank1),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureBackgroundBank2),
    RENDERER_TEST_DEBUG_VIEW(HighResDisplayCaptureBackgroundBank3),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayEpochBank0),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayEpochBank1),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayEpochBank2),
    RENDERER_TEST_DEBUG_VIEW(MainVRAMDisplayEpochBank3),
};
#undef RENDERER_TEST_DEBUG_VIEW

bool ParseDebugView(const QString& name, melonDS::WholeScene2DDebugView& view)
{
    for (const NamedDebugView& candidate : DebugViews)
    {
        if (name == QString::fromLatin1(candidate.Name))
        {
            view = candidate.View;
            return true;
        }
    }
    return false;
}

QString DebugViewExportName(const QString& name)
{
    QString result;
    for (const QChar ch : name)
    {
        if (ch.isUpper() && !result.isEmpty())
            result += '-';
        result += ch.toLower();
    }
    return result;
}

bool SaveRGBAImage(const QString& path,
                   int width,
                   int height,
                   const std::vector<melonDS::u32>& rgba)
{
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
    {
        return false;
    }

    QImage image(reinterpret_cast<const uchar*>(rgba.data()),
                 width,
                 height,
                 QImage::Format_RGBA8888);
    return image.save(path, "PNG");
}
}

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

        const QJsonValue keysValue = stepObject.value("keys");
        const QJsonValue releaseKeysValue = stepObject.value("releaseKeys");
        if (!releaseKeysValue.isUndefined() && !releaseKeysValue.isBool())
        {
            errorstr = QString("Manifest %1 step releaseKeys must be a boolean.").arg(manifestPath);
            delete runner;
            return nullptr;
        }
        step.releaseKeys = releaseKeysValue.toBool(false);
        if (!keysValue.isUndefined())
        {
            if (!keysValue.isArray())
            {
                errorstr = QString("Manifest %1 step keys must be an array.").arg(manifestPath);
                delete runner;
                return nullptr;
            }
            if (step.releaseKeys)
            {
                errorstr = QString("Manifest %1 step cannot specify both keys and releaseKeys.")
                    .arg(manifestPath);
                delete runner;
                return nullptr;
            }

            step.hasKeys = true;
            for (const QJsonValue& keyValue : keysValue.toArray())
            {
                if (!keyValue.isString())
                {
                    errorstr = QString("Manifest %1 step keys entries must be strings.")
                        .arg(manifestPath);
                    delete runner;
                    return nullptr;
                }

                const QString keyName = keyValue.toString();
                int keyIndex = -1;
                for (int i = 0; i < 12; i++)
                {
                    if (keyName.compare(QString::fromLatin1(EmuInstance::buttonNames[i]),
                                        Qt::CaseInsensitive) == 0)
                    {
                        keyIndex = i;
                        break;
                    }
                }
                if (keyIndex < 0)
                {
                    errorstr = QString("Manifest %1 step names unknown DS key %2.")
                        .arg(manifestPath, keyName);
                    delete runner;
                    return nullptr;
                }
                step.keyMask &= ~(1u << keyIndex);
            }
        }
        runner->steps.push_back(step);
    }

    const QJsonObject captures = root.value("captures").toObject();
    runner->captureWholeSceneCsv = captures.value("wholeSceneCsv").toBool(true);
    runner->rollingDumpAtEnd = captures.value("rollingDump").toBool(false);
    runner->profileDebugReads = captures.value("profileDebugReads").toBool(false);
    runner->testDebugFrameHandoff = captures.value("testDebugFrameHandoff").toBool(false);
    runner->testDebugProductValidity = captures.value("testDebugProductValidity").toBool(false);

    const QJsonValue frameCapturesValue = captures.value("frames");
    if (!frameCapturesValue.isUndefined() && !frameCapturesValue.isArray())
    {
        errorstr = QString("Manifest %1 field captures.frames must be an array.").arg(manifestPath);
        delete runner;
        return nullptr;
    }

    long long scheduledFrames = runner->warmupFrames;
    for (const RendererTestStep& step : runner->steps)
        scheduledFrames += step.frames;

    QSet<qulonglong> requestedFrames;
    const QJsonArray frameCaptures = frameCapturesValue.toArray();
    for (const QJsonValue& frameCaptureValue : frameCaptures)
    {
        if (!frameCaptureValue.isObject())
        {
            errorstr = QString("Manifest %1 captures.frames entries must be objects.").arg(manifestPath);
            delete runner;
            return nullptr;
        }

        const QJsonObject frameCaptureObject = frameCaptureValue.toObject();
        const QJsonValue frameValue = frameCaptureObject.value("frame");
        if (!frameValue.isDouble())
        {
            errorstr = QString("Manifest %1 exact-frame capture is missing a numeric frame.").arg(manifestPath);
            delete runner;
            return nullptr;
        }

        const double frameNumber = frameValue.toDouble(-1);
        if (!std::isfinite(frameNumber) ||
            frameNumber < 0 || frameNumber >= scheduledFrames ||
            std::floor(frameNumber) != frameNumber)
        {
            errorstr = QString("Manifest %1 exact-frame capture %2 is outside scheduled frames 0-%3.")
                .arg(manifestPath)
                .arg(frameNumber)
                .arg(std::max<long long>(scheduledFrames - 1, 0));
            delete runner;
            return nullptr;
        }
        const qint64 frame = static_cast<qint64>(frameNumber);
        if (requestedFrames.contains(static_cast<qulonglong>(frame)))
        {
            errorstr = QString("Manifest %1 requests exact frame %2 more than once.")
                .arg(manifestPath)
                .arg(frame);
            delete runner;
            return nullptr;
        }
        requestedFrames.insert(static_cast<qulonglong>(frame));

        RendererTestFrameCapture capture;
        capture.frame = static_cast<qulonglong>(frame);

        const QJsonValue surfacesValue = frameCaptureObject.value("surfaces");
        if (!surfacesValue.isUndefined() && !surfacesValue.isArray())
        {
            errorstr = QString("Manifest %1 exact frame %2 surfaces must be an array.")
                .arg(manifestPath)
                .arg(frame);
            delete runner;
            return nullptr;
        }
        for (const QJsonValue& surfaceValue : surfacesValue.toArray())
        {
            const QString surface = surfaceValue.toString();
            if (surface == "finalTop")
                capture.finalTop = true;
            else if (surface == "finalBottom")
                capture.finalBottom = true;
            else
            {
                errorstr = QString("Manifest %1 exact frame %2 has unknown surface \"%3\".")
                    .arg(manifestPath)
                    .arg(frame)
                    .arg(surface);
                delete runner;
                return nullptr;
            }
        }

        const QJsonValue debugViewsValue = frameCaptureObject.value("debugViews");
        if (!debugViewsValue.isUndefined() && !debugViewsValue.isArray())
        {
            errorstr = QString("Manifest %1 exact frame %2 debugViews must be an array.")
                .arg(manifestPath)
                .arg(frame);
            delete runner;
            return nullptr;
        }
        for (const QJsonValue& debugViewValue : debugViewsValue.toArray())
        {
            const QString debugViewName = debugViewValue.toString();
            melonDS::WholeScene2DDebugView debugView;
            if (!ParseDebugView(debugViewName, debugView))
            {
                errorstr = QString("Manifest %1 exact frame %2 has unknown debug view \"%3\".")
                    .arg(manifestPath)
                    .arg(frame)
                    .arg(debugViewName);
                delete runner;
                return nullptr;
            }
            if (!capture.debugViews.contains(debugView))
            {
                capture.debugViews.push_back(debugView);
                capture.debugViewNames.push_back(debugViewName);
            }
        }

        if (!capture.finalTop && !capture.finalBottom && capture.debugViews.isEmpty())
        {
            errorstr = QString("Manifest %1 exact frame %2 requests no surfaces or debug views.")
                .arg(manifestPath)
                .arg(frame);
            delete runner;
            return nullptr;
        }

        runner->exactFrameCaptures.push_back(std::move(capture));
    }

    if (!runner->exactFrameCaptures.isEmpty() && !runner->captureWholeSceneCsv)
    {
        errorstr = QString("Manifest %1 exact-frame captures require captures.wholeSceneCsv=true.")
            .arg(manifestPath);
        delete runner;
        return nullptr;
    }

    const QJsonValue expectationsValue = root.value("expectations");
    if (!expectationsValue.isUndefined())
    {
        if (!expectationsValue.isString() || expectationsValue.toString().trimmed().isEmpty())
        {
            errorstr = QString("Manifest %1 expectations must be a non-empty path string.")
                .arg(manifestPath);
            delete runner;
            return nullptr;
        }

        runner->expectationsPath = expectationsValue.toString();
        if (QFileInfo(runner->expectationsPath).isRelative())
        {
            runner->expectationsPath = QFileInfo(runner->manifestPath).dir()
                .absoluteFilePath(runner->expectationsPath);
        }
        runner->expectationsPath = QFileInfo(runner->expectationsPath).absoluteFilePath();
        if (!RendererTestAssertions::load(runner->expectationsPath,
                                          runner->assertionSpecs,
                                          errorstr))
        {
            delete runner;
            return nullptr;
        }
        if (!runner->assertionSpecs.isEmpty() && !runner->captureWholeSceneCsv)
        {
            errorstr = QString("Manifest %1 CSV assertions require captures.wholeSceneCsv=true.")
                .arg(manifestPath);
            delete runner;
            return nullptr;
        }
    }

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

    const qulonglong frame = static_cast<qulonglong>(framesRun.load(std::memory_order_relaxed));
    // A timing log can be one presented frame ahead of the replay counter
    // immediately after a savestate load. Auxiliary selector/debug textures
    // must be enabled before that frame is rendered, so pre-arm the next
    // possible timing frame as well. onFramePresented still selects and
    // exports only the exact renderer-reported timing frame.
    armExactFrameCapture(inst, frame + 1);
    armExactFrameCapture(inst, frame);

    // inputProcess() refreshes inputMask from the physical mappings once per
    // outer emulation loop. Reapply the scripted active-low DS key mask on
    // every emulated frame so a held replay step remains deterministic.
    if (keyScheduleActive)
        inst->inputMask = scheduledKeyMask;

    framesRemaining--;
    framesRun.fetch_add(1, std::memory_order_relaxed);
}

RendererTestFrameCapture* RendererTestRunner::captureForFrame(qulonglong frame)
{
    for (RendererTestFrameCapture& capture : exactFrameCaptures)
    {
        if (capture.frame == frame)
            return &capture;
    }
    return nullptr;
}

void RendererTestRunner::armExactFrameCapture(EmuInstance* inst, qulonglong frame)
{
    RendererTestFrameCapture* capture = captureForFrame(frame);
    if (!capture || capture->frameObserved || capture->debugViews.isEmpty() ||
        !inst || !inst->getNDS())
        return;

    std::string status;
    capture->debugViewsArmed =
        inst->getNDS()->GPU.GetRenderer().SetWholeScene2DDebugViewsActive(true, &status);
    if (!capture->debugViewsArmed)
        capture->error = QString::fromStdString(status);
}

void RendererTestRunner::onFramePresented(EmuInstance* inst,
                                          qulonglong timingFrame,
                                          bool timingFrameValid)
{
    RendererTestFrameCapture* capture = captureForFrame(timingFrame);
    if (!capture || capture->frameObserved)
        return;

    capture->frameObserved = true;
    if (!timingFrameValid)
    {
        capture->error = "The renderer did not expose a valid timing frame for this capture.";
        if (inst && inst->getNDS() && !capture->debugViews.isEmpty())
            inst->getNDS()->GPU.GetRenderer().SetWholeScene2DDebugViewsActive(false);
        return;
    }
    if (!inst || !inst->getNDS())
    {
        capture->error = "The emulator renderer was unavailable for this capture.";
        return;
    }

    auto& renderer = inst->getNDS()->GPU.GetRenderer();
    bool ok = true;

    if (capture->finalTop || capture->finalBottom)
    {
        std::string status;
        capture->finalCaptured =
            renderer.ReadWholeScene2DCurrentFinalDebugFrame(capture->finalFrame, &status);
        if (!capture->finalCaptured)
        {
            capture->error = QString::fromStdString(status);
            ok = false;
        }
        else if (!capture->finalFrame.TimingFrameValid ||
                 capture->finalFrame.TimingFrame != capture->frame)
        {
            capture->error = QString("Final-frame metadata reported timing frame %1 instead of %2.")
                .arg(capture->finalFrame.TimingFrameValid
                         ? QString::number(capture->finalFrame.TimingFrame)
                         : QStringLiteral("unavailable"))
                .arg(capture->frame);
            ok = false;
        }
    }

    melonDS::WholeScene2DDebugReadContext context;
    for (int i = 0; i < capture->debugViews.size(); i++)
    {
        bool viewCaptured = false;
        QStringList unavailable;
        for (int screen = 0; screen < 2; screen++)
        {
            RendererTestCapturedDebugView result;
            result.screen = screen;
            result.view = capture->debugViews[i];
            result.name = capture->debugViewNames[i];

            std::string status;
            QElapsedTimer readTimer;
            readTimer.start();
            result.captured = renderer.ReadWholeScene2DDebugView(screen,
                                                                 result.view,
                                                                 result.width,
                                                                 result.height,
                                                                 result.rgba,
                                                                 &status, &context);
            result.readCachedStatusMs = readTimer.nsecsElapsed() / 1000000.0;
            if (profileDebugReads)
            {
                int width = 0, height = 0;
                std::vector<melonDS::u32> rgba;
                std::string uncachedStatus;
                readTimer.restart();
                const bool uncached = renderer.ReadWholeScene2DDebugView(
                    screen, result.view, width, height, rgba, &uncachedStatus);
                result.readWithStatusMs = readTimer.nsecsElapsed() / 1000000.0;
                const bool uncachedMatches = uncached == result.captured && uncachedStatus == status &&
                    (!uncached || (width == result.width && height == result.height && rgba == result.rgba));
                readTimer.restart();
                const bool captured = renderer.ReadWholeScene2DDebugView(
                    screen, result.view, width, height, rgba, nullptr);
                result.readWithoutStatusMs = readTimer.nsecsElapsed() / 1000000.0;
                result.profilePixelsMatch = uncachedMatches && captured == result.captured &&
                    (!captured || (width == result.width && height == result.height &&
                                   rgba == result.rgba));
                std::string repeatedStatus;
                readTimer.restart();
                const bool repeated = renderer.ReadWholeScene2DDebugView(
                    screen, result.view, width, height, rgba, &repeatedStatus);
                result.readWithStatusRepeatMs = readTimer.nsecsElapsed() / 1000000.0;
                result.profilePixelsMatch = result.profilePixelsMatch &&
                    repeated == result.captured && repeatedStatus == status &&
                    (!repeated || (width == result.width && height == result.height &&
                                  rgba == result.rgba));
                if (!result.profilePixelsMatch)
                {
                    ok = false;
                    capture->error = "Debug profiling changed view pixels, status or availability.";
                }
            }
            result.status = QString::fromStdString(status);
            viewCaptured |= result.captured;
            if (!result.captured)
                unavailable.push_back(QString("engine %1: %2")
                    .arg(screen == 0 ? "A" : "B", result.status));
            capture->capturedDebugViews.push_back(std::move(result));
        }

        if (!viewCaptured)
        {
            ok = false;
            if (capture->error.isEmpty())
            {
                capture->error = QString("Debug view %1 was unavailable on every engine (%2).")
                    .arg(capture->debugViewNames[i], unavailable.join("; "));
            }
        }
    }

    if (!capture->debugViews.isEmpty())
        renderer.SetWholeScene2DDebugViewsActive(false);

    capture->captureComplete = ok;
}

bool RendererTestRunner::advanceStep(EmuInstance* inst)
{
    currentStep++;
    if (currentStep >= steps.size())
    {
        inst->releaseScreen();
        keyScheduleActive = true;
        scheduledKeyMask = 0xFFF;
        inst->inputMask = scheduledKeyMask;
        return false;
    }

    const RendererTestStep& step = steps[currentStep];
    if (step.hasTouch)
        inst->touchScreen(step.touchX, step.touchY);
    else if (step.release)
        inst->releaseScreen();
    if (step.hasKeys)
    {
        keyScheduleActive = true;
        scheduledKeyMask = step.keyMask;
    }
    else if (step.releaseKeys)
    {
        keyScheduleActive = true;
        scheduledKeyMask = 0xFFF;
    }
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

    // Load and arm as one paused operation. Otherwise the emulation thread can
    // run extra unscheduled frames between loadState() and armed.store().
    emuThread->emuPause(false);
    if (!savestate.isEmpty())
    {
        if (!emuThread->loadState(savestate))
        {
            fail(QString("Failed to load savestate %1.").arg(savestate));
            emuThread->emuUnpause(false);
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
            emuThread->emuUnpause(false);
            finishRun();
            return;
        }
    }

    if (rollingDumpAtEnd || testDebugFrameHandoff)
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
    emuThread->emuUnpause(false);
}

void RendererTestRunner::onScheduleDone()
{
    if (finished)
        return;

    if (testDebugProductValidity)
        checkDebugProductValidity();

    if (testDebugFrameHandoff)
    {
        auto* thread = emuInstance->getEmuThread();
        auto* window = emuInstance->getMainWindow();
        if (!window->hasOpenGL() || !thread->emuIsRunning())
            fail("Frame handoff test requires running OpenGL emulation.");
        else
        {
            // Hold longer than the old timeout: the acknowledgement must pin
            // the frame, not merely notify while emulation overwrites it.
            auto read = [&](melonDS::WholeScene2DFinalDebugFrame& frame) {
                window->makeCurrentGL();
                const bool ok = emuInstance->getNDS()->GPU.GetRenderer()
                    .ReadWholeScene2DCurrentFinalDebugFrame(frame);
                window->releaseGL();
                return ok && frame.TimingFrameValid;
            };
            for (bool paused : {false, true})
            {
                if (paused) thread->emuPause(false);
                thread->borrowGL();
                melonDS::WholeScene2DFinalDebugFrame before, after, held;
                bool ok = read(before);
                thread->borrowNextFrameGL();
                ok = read(after) && ok;
                QThread::msleep(300);
                ok = read(held) && ok;
                ok = ok && after.TimingFrame == before.TimingFrame + (paused ? 0 : 1)
                    && after.TimingFrame == held.TimingFrame && after.Serial == held.Serial
                    && after.Width == held.Width && after.Height == held.Height
                    && after.TopRGBA == held.TopRGBA && after.BottomRGBA == held.BottomRGBA
                    && thread->emuIsRunning() == !paused;
                thread->returnGL();
                if (paused) thread->emuUnpause(false);
                printf("[renderer-test] %s frame handoff: %s (%llu -> %llu)\n",
                       paused ? "paused" : "running", ok ? "PASS" : "FAIL",
                       static_cast<unsigned long long>(before.TimingFrame),
                       static_cast<unsigned long long>(after.TimingFrame));
                if (!ok) fail("Frame handoff advanced incorrectly or did not hold the acknowledged frame.");
            }
            // Fast returns exercise the condition-variable notification race.
            for (int i = 0; i < 32; ++i)
            {
                thread->borrowGL();
                thread->returnGL();
            }
            QString path, error;
            if (!WholeScene2DDebugDialog::dumpCurrentFrame(window, timingCsvPath,
                    static_cast<qulonglong>(framesRun.load()), &path, &error))
                fail("Frame handoff full dump: " + error);
            else
                printf("[renderer-test] frame handoff full dump: %s\n", path.toUtf8().constData());
        }
    }

    if (rollingDumpAtEnd)
    {
        QMetaObject::invokeMethod(emuInstance->getMainWindow(),
                                  "onDumpWholeScene2DRollingDebugFrames",
                                  Qt::DirectConnection);
    }

    finishRun();
}

// This opt-in test requires stable, full-frame fixtures. It exercises actual
// producers through settings changes, rather than setting validity bits itself.
void RendererTestRunner::checkDebugProductValidity()
{
    auto* thread = emuInstance->getEmuThread();
    auto* window = emuInstance->getMainWindow();
    if (!window->hasOpenGL() || !thread->emuIsRunning())
    {
        fail("Diagnostic validity test requires running OpenGL emulation.");
        return;
    }
    auto& cfg = emuInstance->getGlobalConfig();
    const char* modeKey = "3D.GL.WholeScene2DScaleMode";
    const char* affineKey = "3D.GL.WholeScene2DScaleHybridStrictAffineHighRes";
    const char* aaKey = "3D.GL.WholeScene2DScaleHybridStrictAffineMaskedOBJMLAA";
    const int originalMode = cfg.GetInt(modeKey);
    const bool originalAffine = cfg.GetBool(affineKey);
    const bool originalAA = cfg.GetBool(aaKey);
    using View = melonDS::WholeScene2DDebugView;
    const View views[] = {View::OverlayReconstructedNative, View::OverlayReconstructionError,
        View::OverlayValidityConfidence, View::OverlayUnderlayWeight, View::OverlayOwnershipReason,
        View::HybridSelector, View::HybridCoverageMiss, View::HybridForegroundAlpha, View::HybridFinalSource,
        View::StrictAffineOverlapSemantic, View::StrictAffineOverlapUnderlay,
        View::StrictAffineOverlapDecisions, View::StrictAffineOverlapSubpixelColor,
        View::StrictAffineOverlapSubpixelFlags, View::StrictAffineOverlapSubpixelCoverage};
    int producedOverlayChecks = 0;
    thread->borrowGL();
    auto& renderer = emuInstance->getNDS()->GPU.GetRenderer();
    auto check = [&](const char* phase, bool awaitingFrame, bool aaDisabled = false) {
        window->makeCurrentGL();
        melonDS::WholeScene2DFinalDebugFrame frame;
        if (!renderer.ReadWholeScene2DCurrentFinalDebugFrame(frame))
            fail("Diagnostic validity test could not read frame identity.");
        for (int screen = 0; screen < 2; ++screen)
        {
            const auto path = static_cast<melonDS::WholeSceneRenderPath>(
                screen == 0 ? frame.EngineA.Path : frame.EngineB.Path);
            const bool hybrid = path == melonDS::WholeSceneRenderPath::ConservativeHybridUpscale;
            const bool overlay = hybrid || path == melonDS::WholeSceneRenderPath::OverlayOperatorUpscale;
            const bool affine = path == melonDS::WholeSceneRenderPath::StrictAffineHighRes;
            for (int i = 0; i < 15; ++i)
            {
                int width = 0, height = 0;
                std::vector<melonDS::u32> rgba;
                std::string status;
                const bool available = renderer.ReadWholeScene2DDebugView(screen, views[i], width, height, rgba, &status);
                // Strict-affine foreground coverage is optional unless AA is
                // explicitly disabled, in which case it must be unavailable.
                // Overlap snapshots have a narrower producer than path 12.
                // Positive production is checked by the dedicated replay;
                // these phases enforce reset/disable and non-affine rejection.
                const bool optional = !awaitingFrame && affine &&
                    ((i == 7 && !aaDisabled) || i >= 9);
                const bool expected = !awaitingFrame && i < 9 && (i < 5 ? overlay :
                    (i == 5 ? hybrid || affine : hybrid));
                bool ok = optional || available == expected;
                if (available && i < 5)
                {
                    ++producedOverlayChecks;
                    ok = ok && width > 0 && height > 0 && !rgba.empty() &&
                        std::all_of(rgba.begin(), rgba.end(), [](melonDS::u32 pixel) { return (pixel >> 24) == 255; });
                }
                QJsonObject result;
                result["phase"] = phase;
                result["engine"] = screen == 0 ? "A" : "B";
                result["path"] = static_cast<int>(path);
                result["view"] = static_cast<int>(views[i]);
                result["available"] = available;
                result["passed"] = ok;
                result["status"] = QString::fromStdString(status);
                debugProductChecks.push_back(result);
                if (!ok) fail(QString("Diagnostic validity failed: %1 engine %2 view %3")
                    .arg(phase).arg(screen).arg(static_cast<int>(views[i])));
            }
        }
        window->releaseGL();
    };
    renderer.SetWholeScene2DDebugViewsActive(true);
    thread->borrowNextFrameGL();
    check("original path", false);
    cfg.SetInt(modeKey, 3); // Explicit overlay mode supplies a positive producer.
    cfg.SetBool(affineKey, false);
    thread->updateVideoSettings();
    thread->borrowNextFrameGL();
    check("overlay producer", false);
    cfg.SetInt(modeKey, originalMode);
    cfg.SetBool(affineKey, originalAffine);
    cfg.SetBool(aaKey, false);
    thread->updateVideoSettings();
    thread->borrowNextFrameGL();
    check("return from overlay, AA disabled", false, true);
    thread->returnGL();
    thread->emuPause(false);
    thread->borrowGL();
    renderer.SetWholeScene2DDebugViewsActive(false);
    renderer.SetWholeScene2DDebugViewsActive(true);
    check("paused disable/re-enable before a new frame", true);
    thread->returnGL();
    {
        // Exercise the actual dialog activation/close/destructor ownership
        // path while paused, without showing a window or pumping UI events.
        WholeScene2DDebugDialog dialog(window);
        dialog.close();
    }
    thread->emuUnpause(false);
    thread->borrowGL();
    check("after dialog close", true);
    renderer.SetWholeScene2DDebugViewsActive(true);
    cfg.SetBool(aaKey, originalAA);
    thread->updateVideoSettings();
    thread->borrowNextFrameGL();
    check("fresh frame after re-enable", false);
    renderer.SetWholeScene2DDebugViewsActive(false);
    thread->returnGL();
    if (producedOverlayChecks == 0)
        fail("Diagnostic validity test did not reach an overlay producer.");
    printf("[renderer-test] diagnostic validity: %d checks, %d produced overlay reads\n",
           static_cast<int>(debugProductChecks.size()), producedOverlayChecks);
}

void RendererTestRunner::writeExactFrameCaptures()
{
    if (exactFrameCapturesWritten)
        return;
    exactFrameCapturesWritten = true;

    if (exactFrameCaptures.isEmpty())
        return;

    QDir runDir(outputDir);
    const QString rootName = "exact-frame-captures";
    if (!runDir.mkpath(rootName))
    {
        fail(QString("Could not create exact-frame capture directory in %1.").arg(outputDir));
        return;
    }
    QDir captureRoot(runDir.filePath(rootName));

    for (RendererTestFrameCapture& capture : exactFrameCaptures)
    {
        bool ok = capture.captureComplete;
        if (!capture.frameObserved)
        {
            capture.error = QString("Exact frame %1 was never presented.").arg(capture.frame);
            ok = false;
        }
        else if (!capture.captureComplete && capture.error.isEmpty())
        {
            capture.error = QString("Exact frame %1 capture did not complete.").arg(capture.frame);
        }

        const QString frameDirName = QString("frame%1").arg(capture.frame, 6, 10, QChar('0'));
        if (!captureRoot.mkpath(frameDirName))
        {
            capture.error = QString("Could not create export directory for exact frame %1.")
                .arg(capture.frame);
            ok = false;
        }
        QDir frameDir(captureRoot.filePath(frameDirName));

        auto saveFinal = [&](bool requested,
                             const char* filename,
                             const std::vector<melonDS::u32>& rgba)
        {
            if (!requested)
                return;
            const QString path = frameDir.filePath(QString::fromLatin1(filename));
            if (!capture.finalCaptured ||
                !SaveRGBAImage(path,
                               capture.finalFrame.Width,
                               capture.finalFrame.Height,
                               rgba))
            {
                if (capture.error.isEmpty())
                {
                    capture.error = QString("Failed to save exact frame %1 surface %2.")
                        .arg(capture.frame)
                        .arg(QString::fromLatin1(filename));
                }
                ok = false;
                return;
            }
            capture.exportedFiles.push_back(runDir.relativeFilePath(path));
        };

        saveFinal(capture.finalTop, "final-top.png", capture.finalFrame.TopRGBA);
        saveFinal(capture.finalBottom, "final-bottom.png", capture.finalFrame.BottomRGBA);

        for (RendererTestCapturedDebugView& view : capture.capturedDebugViews)
        {
            if (!view.captured)
                continue;

            const QString filename = QString("engine-%1-%2.png")
                .arg(view.screen == 0 ? "a" : "b")
                .arg(DebugViewExportName(view.name));
            const QString path = frameDir.filePath(filename);
            if (!SaveRGBAImage(path, view.width, view.height, view.rgba))
            {
                if (capture.error.isEmpty())
                {
                    capture.error = QString("Failed to save exact frame %1 debug view %2 for engine %3.")
                        .arg(capture.frame)
                        .arg(view.name)
                        .arg(view.screen == 0 ? "A" : "B");
                }
                ok = false;
                continue;
            }
            view.exportedFile = runDir.relativeFilePath(path);
            capture.exportedFiles.push_back(view.exportedFile);
        }

        capture.exportComplete = ok;
        if (!ok)
            fail(capture.error);
    }
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

    writeExactFrameCaptures();

    if (!assertionSpecs.isEmpty())
    {
        const QFileInfo summaryInfo(timingCsvPath);
        const QString detailsPath = summaryInfo.dir().filePath(
            summaryInfo.completeBaseName() + "-renderer-details." + summaryInfo.suffix());
        const RendererTestAssertionRun assertionRun =
            RendererTestAssertions::evaluate(assertionSpecs,
                                             expectationsPath,
                                             outputDir,
                                             timingCsvPath,
                                             detailsPath);
        assertionResults = assertionRun.results;
        assertionsPassed = assertionRun.passed;
        assertionsFailed = assertionRun.failed;
        if (assertionRun.failed > 0)
            fail(assertionRun.firstFailure);
    }

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
    verdict["debugFrameHandoffTestRequested"] = testDebugFrameHandoff;
    verdict["debugProductChecks"] = debugProductChecks;
    verdict["exactFrameCapturesRequested"] = static_cast<int>(exactFrameCaptures.size());

    int exportedCaptureCount = 0;
    QJsonArray exactCaptures;
    for (const RendererTestFrameCapture& capture : exactFrameCaptures)
    {
        QJsonObject item;
        item["frame"] = static_cast<qint64>(capture.frame);
        item["frameObserved"] = capture.frameObserved;
        item["captureComplete"] = capture.captureComplete;
        item["exportComplete"] = capture.exportComplete;
        item["error"] = capture.error;

        QJsonArray surfaces;
        if (capture.finalTop)
            surfaces.push_back("finalTop");
        if (capture.finalBottom)
            surfaces.push_back("finalBottom");
        item["surfaces"] = surfaces;

        QJsonArray debugViews;
        for (const QString& debugView : capture.debugViewNames)
            debugViews.push_back(debugView);
        item["debugViews"] = debugViews;

        QJsonArray debugViewResults;
        for (const RendererTestCapturedDebugView& result : capture.capturedDebugViews)
        {
            QJsonObject viewResult;
            viewResult["engine"] = result.screen == 0 ? "A" : "B";
            viewResult["view"] = result.name;
            viewResult["captured"] = result.captured;
            viewResult["status"] = result.status;
            viewResult["width"] = result.width;
            viewResult["height"] = result.height;
            viewResult["file"] = result.exportedFile;
            if (profileDebugReads)
            {
                viewResult["readCachedStatusMs"] = result.readCachedStatusMs;
                viewResult["readWithStatusMs"] = result.readWithStatusMs;
                viewResult["readWithoutStatusMs"] = result.readWithoutStatusMs;
                viewResult["readWithStatusRepeatMs"] = result.readWithStatusRepeatMs;
                viewResult["profilePixelsAndStatusMatch"] = result.profilePixelsMatch;
            }
            debugViewResults.push_back(viewResult);
        }
        item["debugViewResults"] = debugViewResults;

        QJsonArray files;
        for (const QString& path : capture.exportedFiles)
            files.push_back(path);
        item["files"] = files;

        if (capture.finalCaptured)
        {
            item["timingFrameValid"] = capture.finalFrame.TimingFrameValid;
            item["timingFrame"] = static_cast<qint64>(capture.finalFrame.TimingFrame);
            item["width"] = capture.finalFrame.Width;
            item["height"] = capture.finalFrame.Height;
            item["finalTopSource"] = capture.finalFrame.FinalTopSource;
            item["finalBottomSource"] = capture.finalFrame.FinalBottomSource;
        }

        if (capture.exportComplete)
            exportedCaptureCount++;
        exactCaptures.push_back(item);
    }
    verdict["exactFrameCapturesExported"] = exportedCaptureCount;
    verdict["exactFrameCaptures"] = exactCaptures;
    verdict["expectations"] = expectationsPath;
    verdict["assertionsRequested"] = assertionSpecs.size();
    verdict["assertionsPassed"] = assertionsPassed;
    verdict["assertionsFailed"] = assertionsFailed;
    verdict["assertions"] = assertionResults;

    QFile file(outputDir + "/verdict.json");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(verdict).toJson(QJsonDocument::Indented));
    else
        printf("[renderer-test] could not write verdict.json: %s\n",
               file.errorString().toUtf8().constData());
}
