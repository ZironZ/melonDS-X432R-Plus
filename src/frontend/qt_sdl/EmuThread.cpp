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

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <optional>
#include <vector>
#include <string>
#include <algorithm>

#include <QMutexLocker>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>

#include <SDL2/SDL.h>

#include "main.h"
#include "WideMelon.h"

#include "types.h"
#include "version.h"

#include "ScreenLayout.h"

#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "GPU.h"
#include "SPU.h"
#include "Wifi.h"
#include "Platform.h"
#include "LocalMP.h"
#include "Config.h"
#include "RTC.h"
#include "DSi.h"
#include "DSi_I2C.h"
#include "GPU_Soft.h"
#include "GPU_OpenGL.h"

#include "Savestate.h"

#include "EmuInstance.h"
#include "RendererTest.h"
#include "Screen.h"

using namespace melonDS;

namespace
{
QString wholeSceneCompanionLogPath(const QString& filename, const QString& suffix)
{
    const QFileInfo info(filename);
    const QString baseName = info.completeBaseName().isEmpty()
        ? info.fileName()
        : info.completeBaseName();
    const QString extension = info.suffix().isEmpty() ? QStringLiteral("csv") : info.suffix();
    return info.dir().filePath(QStringLiteral("%1-%2.%3").arg(baseName, suffix, extension));
}
}

EmuThread::EmuThread(EmuInstance* inst, QObject* parent) : QThread(parent)
{
    emuInstance = inst;

    emuStatus = emuStatus_Paused;
    emuPauseStack = emuPauseStackRunning;
    emuActive = false;
    wholeSceneTimingLogEnabled.store(false);
    wholeSceneTimingLogFrame = 0;
    wholeSceneTimingLogHeaderWritten = false;
    wholeSceneRendererLogHeaderWritten = false;
    wholeSceneTimingLastTouching = false;
}

bool EmuThread::startWholeSceneTimingLog(const QString& filename, QString& errorstr)
{
    QMutexLocker lock(&wholeSceneTimingLogMutex);

    wholeSceneTimingLogEnabled.store(false);
    SetScreenPresentationTimingEnabled(false);
    if (wholeSceneTimingLogFile.isOpen())
    {
        if (!wholeSceneTimingLogBuffer.isEmpty())
        {
            wholeSceneTimingLogFile.write(wholeSceneTimingLogBuffer.toUtf8());
            wholeSceneTimingLogBuffer.clear();
        }
        wholeSceneTimingLogFile.close();
    }
    if (wholeSceneRendererLogFile.isOpen())
    {
        if (!wholeSceneRendererLogBuffer.isEmpty())
        {
            wholeSceneRendererLogFile.write(wholeSceneRendererLogBuffer.toUtf8());
            wholeSceneRendererLogBuffer.clear();
        }
        wholeSceneRendererLogFile.close();
    }

    wholeSceneTimingLogFile.setFileName(filename);
    if (!wholeSceneTimingLogFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        errorstr = wholeSceneTimingLogFile.errorString();
        return false;
    }

    wholeSceneRendererLogFile.setFileName(wholeSceneCompanionLogPath(filename, QStringLiteral("renderer-details")));
    if (!wholeSceneRendererLogFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        errorstr = wholeSceneRendererLogFile.errorString();
        wholeSceneTimingLogFile.close();
        return false;
    }

    wholeSceneTimingLogBuffer.clear();
    wholeSceneRendererLogBuffer.clear();
    wholeSceneTimingLogFrame = 0;
    wholeSceneTimingLogHeaderWritten = false;
    wholeSceneRendererLogHeaderWritten = false;
    wholeSceneTimingLastTouching = emuInstance && emuInstance->isTouching;
    wholeSceneTimingLogEnabled.store(true);
    return true;
}

QString EmuThread::stopWholeSceneTimingLog()
{
    QMutexLocker lock(&wholeSceneTimingLogMutex);

    wholeSceneTimingLogEnabled.store(false);
    SetScreenPresentationTimingEnabled(false);
    const QString filename = wholeSceneTimingLogFile.fileName();
    if (wholeSceneTimingLogFile.isOpen())
    {
        if (!wholeSceneTimingLogBuffer.isEmpty())
        {
            wholeSceneTimingLogFile.write(wholeSceneTimingLogBuffer.toUtf8());
            wholeSceneTimingLogBuffer.clear();
        }
        wholeSceneTimingLogFile.close();
    }
    if (wholeSceneRendererLogFile.isOpen())
    {
        if (!wholeSceneRendererLogBuffer.isEmpty())
        {
            wholeSceneRendererLogFile.write(wholeSceneRendererLogBuffer.toUtf8());
            wholeSceneRendererLogBuffer.clear();
        }
        wholeSceneRendererLogFile.close();
    }
    return filename;
}

bool EmuThread::flushWholeSceneTimingLog(QString& filename, u64& nextFrame, QString& errorstr)
{
    QMutexLocker lock(&wholeSceneTimingLogMutex);

    filename.clear();
    nextFrame = wholeSceneTimingLogFrame;
    errorstr.clear();

    if (!wholeSceneTimingLogEnabled.load() || !wholeSceneTimingLogFile.isOpen())
    {
        errorstr = "Whole-scene timing log is not active.";
        return false;
    }

    if (!wholeSceneTimingLogBuffer.isEmpty())
    {
        if (wholeSceneTimingLogFile.write(wholeSceneTimingLogBuffer.toUtf8()) < 0)
        {
            errorstr = wholeSceneTimingLogFile.errorString();
            return false;
        }
        wholeSceneTimingLogBuffer.clear();
    }
    if (!wholeSceneRendererLogBuffer.isEmpty())
    {
        if (wholeSceneRendererLogFile.write(wholeSceneRendererLogBuffer.toUtf8()) < 0)
        {
            errorstr = wholeSceneRendererLogFile.errorString();
            return false;
        }
        wholeSceneRendererLogBuffer.clear();
    }

    if (!wholeSceneTimingLogFile.flush())
    {
        errorstr = wholeSceneTimingLogFile.errorString();
        return false;
    }
    if (wholeSceneRendererLogFile.isOpen() && !wholeSceneRendererLogFile.flush())
    {
        errorstr = wholeSceneRendererLogFile.errorString();
        return false;
    }

    filename = wholeSceneTimingLogFile.fileName();
    nextFrame = wholeSceneTimingLogFrame;
    return true;
}

void EmuThread::appendWholeSceneTimingLog(u32 nlines,
                                          u32 keyMask,
                                          u64 runFrameUS,
                                          u64 drawScreenUS,
                                          u64 presentPreSwapUS,
                                          u64 presentSwapUS,
                                          u32 presentSwapCount,
                                          bool touchActive,
                                          bool touchPress,
                                          bool touchRelease,
                                          int touchX,
                                          int touchY,
                                          u64 totalUS)
{
    if (!wholeSceneTimingLogEnabled.load())
        return;

    std::string rendererHeader;
    std::string rendererRow;
    const bool rendererTimingAvailable =
        emuInstance && emuInstance->nds &&
        emuInstance->nds->GPU.GetRenderer().ReadWholeScene2DTimingCSV(rendererHeader, rendererRow);

    QMutexLocker lock(&wholeSceneTimingLogMutex);
    if (!wholeSceneTimingLogEnabled.load() || !wholeSceneTimingLogFile.isOpen())
        return;

    if (!wholeSceneTimingLogHeaderWritten)
    {
        wholeSceneTimingLogBuffer += "frame,total_us,runframe_us,drawscreen_us,present_pre_swap_us,present_swap_us,present_swap_count,nlines,key_mask,touch_active,touch_press,touch_release,touch_x,touch_y,renderer_timing";
        wholeSceneTimingLogBuffer += "\n";
        wholeSceneTimingLogHeaderWritten = true;
    }
    if (!wholeSceneRendererLogHeaderWritten &&
        wholeSceneRendererLogFile.isOpen() &&
        rendererTimingAvailable &&
        !rendererHeader.empty())
    {
        wholeSceneRendererLogBuffer += "frame,renderer_timing,";
        wholeSceneRendererLogBuffer += QString::fromStdString(rendererHeader);
        wholeSceneRendererLogBuffer += "\n";
        wholeSceneRendererLogHeaderWritten = true;
    }

    const u64 timingFrame = wholeSceneTimingLogFrame++;
    wholeSceneTimingLogBuffer += QString::number(timingFrame);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(totalUS);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(runFrameUS);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(drawScreenUS);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(presentPreSwapUS);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(presentSwapUS);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(presentSwapCount);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(nlines);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(keyMask & 0xFFF);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += touchActive ? "1" : "0";
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += touchPress ? "1" : "0";
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += touchRelease ? "1" : "0";
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(touchX);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += QString::number(touchY);
    wholeSceneTimingLogBuffer += ",";
    wholeSceneTimingLogBuffer += rendererTimingAvailable ? "1" : "0";
    if (wholeSceneRendererLogHeaderWritten)
    {
        wholeSceneRendererLogBuffer += QString::number(timingFrame);
        wholeSceneRendererLogBuffer += ",";
        wholeSceneRendererLogBuffer += rendererTimingAvailable ? "1" : "0";
        if (rendererTimingAvailable && !rendererRow.empty())
        {
            wholeSceneRendererLogBuffer += ",";
            wholeSceneRendererLogBuffer += QString::fromStdString(rendererRow);
        }
        wholeSceneRendererLogBuffer += "\n";
    }
    wholeSceneTimingLogBuffer += "\n";

    if (wholeSceneTimingLogBuffer.size() >= 65536)
    {
        wholeSceneTimingLogFile.write(wholeSceneTimingLogBuffer.toUtf8());
        wholeSceneTimingLogBuffer.clear();
    }
    if (wholeSceneRendererLogBuffer.size() >= 65536)
    {
        wholeSceneRendererLogFile.write(wholeSceneRendererLogBuffer.toUtf8());
        wholeSceneRendererLogBuffer.clear();
    }
}

void EmuThread::attachWindow(MainWindow* window)
{
    connect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    connect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    connect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    connect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    connect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    connect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    connect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    connect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        connect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        connect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}

void EmuThread::detachWindow(MainWindow* window)
{
    disconnect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    disconnect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    disconnect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    disconnect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    disconnect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    disconnect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    disconnect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    disconnect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        disconnect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        disconnect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}

void EmuThread::run()
{
    Config::Table& globalCfg = emuInstance->getGlobalConfig();
    u32 mainScreenPos[3];

    //emuInstance->updateConsole();
    // No carts are inserted when melonDS first boots

    mainScreenPos[0] = 0;
    mainScreenPos[1] = 0;
    mainScreenPos[2] = 0;
    autoScreenSizing = 0;

    //videoSettingsDirty = false;

    if (emuInstance->usesOpenGL())
    {
        emuInstance->initOpenGL(0);

        useOpenGL = true;
        videoRenderer = globalCfg.GetInt("3D.Renderer");
    }
    else
    {
        useOpenGL = false;
        videoRenderer = 0;
    }

    //updateRenderer();
    videoSettingsDirty = true;

    u32 nframes = 0;
    double perfCountsSec = 1.0 / SDL_GetPerformanceFrequency();
    double lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
    double frameLimitError = 0.0;
    double lastMeasureTime = lastTime;

    u32 winUpdateCount = 0, winUpdateFreq = 1;
    u8 dsiVolumeLevel = 0x1F;

    char melontitle[100];

    bool fastforward = false;
    bool slowmo = false;
    emuInstance->fastForwardToggled = false;
    emuInstance->slowmoToggled = false;

    while (emuStatus != emuStatus_Exit)
    {
        bool frameReady = false;
        if (emuInstance->instanceID == 0)
            MPInterface::Get().Process();

        emuInstance->inputProcess();

        if (emuInstance->hotkeyPressed(HK_FrameLimitToggle)) emit windowLimitFPSChange();

        if (emuInstance->hotkeyPressed(HK_Pause)) emuTogglePause();
        if (emuInstance->hotkeyPressed(HK_Reset))
            QMetaObject::invokeMethod(emuInstance->getMainWindow(), "onReset", Qt::QueuedConnection);
        if (emuInstance->hotkeyPressed(HK_FrameStep)) emuFrameStep();

        if (emuInstance->hotkeyPressed(HK_FullscreenToggle)) emit windowFullscreenToggle();

        if (emuInstance->hotkeyPressed(HK_SwapScreens)) emit swapScreensToggle();
        if (emuInstance->hotkeyPressed(HK_SwapScreenEmphasis)) emit screenEmphasisToggle();

        if (emuStatus == emuStatus_Running || emuStatus == emuStatus_FrameStep)
        {
            if (emuStatus == emuStatus_FrameStep) emuStatus = emuStatus_Paused;

            if (emuInstance->hotkeyPressed(HK_SolarSensorDecrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }
            if (emuInstance->hotkeyPressed(HK_SolarSensorIncrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }

            if (emuInstance->nds->ConsoleType == 1)
            {
                DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                double currentTime = SDL_GetPerformanceCounter() * perfCountsSec;

                // Handle power button
                if (emuInstance->hotkeyDown(HK_PowerButton))
                {
                    dsi->I2C.GetBPTWL()->SetPowerButtonHeld(currentTime);
                }
                else if (emuInstance->hotkeyReleased(HK_PowerButton))
                {
                    dsi->I2C.GetBPTWL()->SetPowerButtonReleased(currentTime);
                }

                // Handle volume buttons
                if (emuInstance->hotkeyDown(HK_VolumeUp))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Up);
                }
                else if (emuInstance->hotkeyReleased(HK_VolumeUp))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Up);
                }

                if (emuInstance->hotkeyDown(HK_VolumeDown))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Down);
                }
                else if (emuInstance->hotkeyReleased(HK_VolumeDown))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Down);
                }

                dsi->I2C.GetBPTWL()->ProcessVolumeSwitchInput(currentTime);
            }

            if (useOpenGL)
                emuInstance->makeCurrentGL();

            // update render settings if needed
            if (videoSettingsDirty)
            {
                emuInstance->renderLock.lock();
                if (useOpenGL)
                {
                    emuInstance->setVSyncGL(true);
                    videoRenderer = globalCfg.GetInt("3D.Renderer");
                }
#ifdef OGLRENDERER_ENABLED
                else
#endif
                {
                    videoRenderer = 0;
                }

                updateRenderer();

                videoSettingsDirty = false;
                emuInstance->renderLock.unlock();
            }

            // renderer-test harness: advance the replay schedule before this
            // frame's input is applied, so scripted touches are frame-exact
            // Shader compilation does not emulate a frame. Finish it before
            // starting the test schedule so cold caches cannot consume input
            // steps or shift the numbered captures.
            if (emuInstance->rendererTest)
                while (emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile())
                    compileShaders();
            if (emuInstance->rendererTest)
                emuInstance->rendererTest->onEmuFrame(emuInstance);

            // process input and hotkeys
            emuInstance->nds->SetKeyMask(emuInstance->inputMask);

            if (emuInstance->isTouching)
                emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
            else
                emuInstance->nds->ReleaseScreen();

            const bool frameTouching = emuInstance->isTouching;
            const bool frameTouchPress = frameTouching && !wholeSceneTimingLastTouching;
            const bool frameTouchRelease = !frameTouching && wholeSceneTimingLastTouching;
            const int frameTouchX = frameTouching ? emuInstance->touchX : -1;
            const int frameTouchY = frameTouching ? emuInstance->touchY : -1;
            wholeSceneTimingLastTouching = frameTouching;

            if (emuInstance->hotkeyPressed(HK_Lid))
            {
                bool lid = !emuInstance->nds->IsLidClosed();
                emuInstance->nds->SetLidClosed(lid);
                emuInstance->osdAddMessage(0, lid ? "Lid closed" : "Lid opened");
            }

            // auto screen layout
            {
                mainScreenPos[2] = mainScreenPos[1];
                mainScreenPos[1] = mainScreenPos[0];
                mainScreenPos[0] = emuInstance->nds->PowerControl9 >> 15;

                int guess;
                if (mainScreenPos[0] == mainScreenPos[2] &&
                    mainScreenPos[0] != mainScreenPos[1])
                {
                    // constant flickering, likely displaying 3D on both screens
                    // TODO: when both screens are used for 2D only...???
                    guess = screenSizing_Even;
                }
                else
                {
                    if (mainScreenPos[0] == 1)
                        guess = screenSizing_EmphTop;
                    else
                        guess = screenSizing_EmphBot;
                }

                if (guess != autoScreenSizing)
                {
                    autoScreenSizing = guess;
                    emit autoScreenSizingChange(autoScreenSizing);
                }
            }

            // RTC sync
            emuInstance->syncRTC();


            // emulate
            const u64 frameWorkStart = SDL_GetPerformanceCounter();
            const u64 runFrameStart = frameWorkStart;
            u32 nlines;
            if (emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile())
            {
                compileShaders();
                nlines = 1;
            }
            else
            {
                nlines = emuInstance->nds->RunFrame();
            }
            const u64 runFrameEnd = SDL_GetPerformanceCounter();

            if (emuInstance->ndsSave)
                emuInstance->ndsSave->CheckFlush();

            if (emuInstance->gbaSave)
                emuInstance->gbaSave->CheckFlush();

            if (emuInstance->firmwareSave)
                emuInstance->firmwareSave->CheckFlush();

            const bool timingLogActive = wholeSceneTimingLogEnabled.load();
            SetScreenPresentationTimingEnabled(timingLogActive);
            u64 timingFrame = 0;
            bool timingFrameValid = false;
            if (timingLogActive)
            {
                {
                    QMutexLocker lock(&wholeSceneTimingLogMutex);
                    timingFrame = wholeSceneTimingLogFrame;
                    timingFrameValid = wholeSceneTimingLogEnabled.load() && wholeSceneTimingLogFile.isOpen();
                }
                emuInstance->nds->GPU.GetRenderer().SetWholeScene2DTimingFrame(timingFrame, timingFrameValid);
                ResetScreenPresentationTiming();
            }
            else
            {
                emuInstance->nds->GPU.GetRenderer().SetWholeScene2DTimingFrame(0, false);
            }

            const u64 drawScreenStart = SDL_GetPerformanceCounter();
            emuInstance->drawScreen();
            const u64 drawScreenEnd = SDL_GetPerformanceCounter();
            const auto counterDeltaUS = [perfCountsSec](u64 start, u64 end)
            {
                return static_cast<u64>(((end - start) * perfCountsSec * 1000000.0) + 0.5);
            };
            const u64 drawScreenUS = counterDeltaUS(drawScreenStart, drawScreenEnd);
            const u64 presentSwapUS = timingLogActive ? GetScreenPresentationSwapUS() : 0;
            const u32 presentSwapCount = timingLogActive ? GetScreenPresentationSwapCount() : 0;
            const u64 presentPreSwapUS = (drawScreenUS > presentSwapUS) ? (drawScreenUS - presentSwapUS) : 0;
            appendWholeSceneTimingLog(nlines,
                                      emuInstance->inputMask,
                                      counterDeltaUS(runFrameStart, runFrameEnd),
                                      drawScreenUS,
                                      presentPreSwapUS,
                                      presentSwapUS,
                                      presentSwapCount,
                                      frameTouching,
                                      frameTouchPress,
                                      frameTouchRelease,
                                      frameTouchX,
                                      frameTouchY,
                                      counterDeltaUS(frameWorkStart, drawScreenEnd));

            frameReady = nlines > 1;
            if (emuInstance->rendererTest)
                emuInstance->rendererTest->onFramePresented(emuInstance, timingFrame, timingFrameValid);

#ifdef MELONCAP
            MelonCap::Update();
#endif // MELONCAP

            winUpdateCount++;
            if (winUpdateCount >= winUpdateFreq && !useOpenGL)
            {
                emit windowUpdate();
                winUpdateCount = 0;
            }
            
            if (emuInstance->hotkeyPressed(HK_FastForwardToggle)) emuInstance->fastForwardToggled = !emuInstance->fastForwardToggled;
            if (emuInstance->hotkeyPressed(HK_SlowMoToggle)) emuInstance->slowmoToggled = !emuInstance->slowmoToggled;

            if (emuInstance->hotkeyPressed(HK_AudioMuteToggle)) emuInstance->toggleAudioMute();

            bool enablefastforward = emuInstance->hotkeyDown(HK_FastForward) | emuInstance->fastForwardToggled;
            bool enableslowmo = emuInstance->hotkeyDown(HK_SlowMo) | emuInstance->slowmoToggled;

            if (useOpenGL)
            {
                // when using OpenGL: when toggling fast-forward or slowmo, change the vsync interval
                if ((enablefastforward || enableslowmo) && !(fastforward || slowmo))
                {
                    emuInstance->setVSyncGL(false);
                }
                else if (!(enablefastforward || enableslowmo) && (fastforward || slowmo))
                {
                    emuInstance->setVSyncGL(true);
                }
            }

            fastforward = enablefastforward;
            slowmo = enableslowmo;
            emuInstance->updateFastForwardMute(fastforward);

            if (slowmo) emuInstance->curFPS = emuInstance->slowmoFPS;
            else if (fastforward) emuInstance->curFPS = emuInstance->fastForwardFPS;
            else if (!emuInstance->doLimitFPS && !emuInstance->doAudioSync) emuInstance->curFPS = 1000.0;
            else emuInstance->curFPS = emuInstance->targetFPS;

            if (emuInstance->audioDSiVolumeSync && emuInstance->nds->ConsoleType == 1)
            {
                DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                u8 volumeLevel = dsi->I2C.GetBPTWL()->GetVolumeLevel();
                if (volumeLevel != dsiVolumeLevel)
                {
                    dsiVolumeLevel = volumeLevel;
                    emit syncVolumeLevel();
                }

                emuInstance->audioVolume = volumeLevel * (256.0 / 31.0);
            }

            if (emuInstance->doAudioSync && !(fastforward || slowmo))
                emuInstance->audioSync();

            double frametimeStep = nlines / (emuInstance->curFPS * 263.0);

            if (frametimeStep < 0.001) frametimeStep = 0.001;

            if (emuInstance->doLimitFPS)
            {
                double curtime = SDL_GetPerformanceCounter() * perfCountsSec;

                frameLimitError += frametimeStep - (curtime - lastTime);
                if (frameLimitError < -frametimeStep)
                    frameLimitError = -frametimeStep;
                if (frameLimitError > frametimeStep)
                    frameLimitError = frametimeStep;

                if (round(frameLimitError * 1000.0) > 0.0)
                {
                    SDL_Delay(round(frameLimitError * 1000.0));
                    double timeBeforeSleep = curtime;
                    curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                    frameLimitError -= curtime - timeBeforeSleep;
                }

                lastTime = curtime;
            }

            nframes++;
            if (nframes >= 30)
            {
                double time = SDL_GetPerformanceCounter() * perfCountsSec;
                double dt = time - lastMeasureTime;
                lastMeasureTime = time;

                u32 fps = round(nframes / dt);
                nframes = 0;

                float fpstarget = 1.0/frametimeStep;

                winUpdateFreq = fps / (u32)round(fpstarget);
                if (winUpdateFreq < 1)
                    winUpdateFreq = 1;
                    
                double actualfps = (59.8261 * 263.0) / nlines;
                snprintf(melontitle, sizeof(melontitle), "[%d/%.0f] melonDS %s", fps, actualfps, MELONDS_VERSION);
                changeWindowTitle(melontitle);
            }
        }
        else
        {
            // paused
            nframes = 0;
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
            lastMeasureTime = lastTime;

            emit windowUpdate();

            snprintf(melontitle, sizeof(melontitle), "melonDS %s", MELONDS_VERSION);
            changeWindowTitle(melontitle);

            SDL_Delay(75);

            emuInstance->drawScreen();
        }

        handleMessages(frameReady);
    }
}

void EmuThread::sendMessage(Message msg)
{
    msgMutex.lock();
    msgQueue.enqueue(msg);
    msgMutex.unlock();
}

void EmuThread::waitMessage(int num)
{
    if (QThread::currentThread() == this) return;
    msgSemaphore.acquire(num);
}

void EmuThread::waitAllMessages()
{
    if (QThread::currentThread() == this) return;
    while (!msgQueue.empty())
        msgSemaphore.acquire();
}

void EmuThread::handleMessages(bool frameReady)
{
    bool glborrow = false;

    msgMutex.lock();
    while (!msgQueue.empty())
    {
        // A debug frame request is queued while the preceding GL borrow is
        // held. Compilation-only iterations must not acknowledge stale images.
        if (msgQueue.head().type == msg_BorrowGLAfterFrame && !frameReady &&
            (emuStatus == emuStatus_Running || emuStatus == emuStatus_FrameStep))
            break;

        Message msg = msgQueue.dequeue();
        switch (msg.type)
        {
        case msg_Exit:
            emuStatus = emuStatus_Exit;
            emuPauseStack = emuPauseStackRunning;

            emuInstance->audioDisable();
            MPInterface::Get().End(emuInstance->instanceID);
            break;

        case msg_EmuRun:
            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;

            emuInstance->audioEnable();
            emit windowEmuStart();
            break;

        case msg_EmuPause:
            emuPauseStack++;
            if (emuPauseStack > emuPauseStackPauseThreshold) break;

            prevEmuStatus = emuStatus;
            emuStatus = emuStatus_Paused;

            if (prevEmuStatus != emuStatus_Paused)
            {
                emuInstance->audioDisable();
                emit windowEmuPause(true);
                emuInstance->osdAddMessage(0, "Paused");
            }
            break;

        case msg_EmuUnpause:
            if (emuPauseStack < emuPauseStackPauseThreshold) break;

            emuPauseStack--;
            if (emuPauseStack >= emuPauseStackPauseThreshold) break;

            emuStatus = prevEmuStatus;

            if (emuStatus != emuStatus_Paused)
            {
                emuInstance->audioEnable();
                emit windowEmuPause(false);
                emuInstance->osdAddMessage(0, "Resumed");
            }
            break;

        case msg_EmuStop:
            if (msg.param.value<bool>())
                emuInstance->nds->Stop();
            emuStatus = emuStatus_Paused;
            emuActive = false;

            emuInstance->audioDisable();
            emit windowEmuStop();
            break;

        case msg_EmuFrameStep:
            emuStatus = emuStatus_FrameStep;
            break;

        case msg_EmuReset:
            emuInstance->reset();

            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;

            emuInstance->audioEnable();
            emit windowEmuReset();
            emuInstance->osdAddMessage(0, "Reset");
            break;

        case msg_InitGL:
            emuInstance->initOpenGL(msg.param.value<int>());
            useOpenGL = true;
            break;

        case msg_DeInitGL:
            emuInstance->deinitOpenGL(msg.param.value<int>());
            if (msg.param.value<int>() == 0)
                useOpenGL = false;
            break;

        case msg_BorrowGL:
        case msg_BorrowGLAfterFrame:
            emuInstance->releaseGL();
            {
                QMutexLocker lock(&glBorrowMutex);
                glBorrowed = true;
            }
            glborrow = true;
            break;

        case msg_BootROM:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), true, msgError))
                break;

            assert(emuInstance->nds != nullptr);
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_BootFirmware:
            msgResult = 0;
            if (!emuInstance->bootToMenu(msgError))
                break;

            assert(emuInstance->nds != nullptr);
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_InsertCart:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), false, msgError))
                break;

            msgResult = 1;
            break;

        case msg_EjectCart:
            emuInstance->ejectCart();
            break;

        case msg_InsertGBACart:
            msgResult = 0;
            if (!emuInstance->loadGBAROM(msg.param.value<QStringList>(), msgError))
                break;

            msgResult = 1;
            break;

        case msg_InsertGBAAddon:
            msgResult = 0;
            emuInstance->loadGBAAddon(msg.param.value<int>(), msgError);
            msgResult = 1;
            break;

        case msg_EjectGBACart:
            emuInstance->ejectGBACart();
            break;

        case msg_SaveState:
            msgResult = emuInstance->saveState(msg.param.value<QString>().toStdString());
            break;

        case msg_LoadState:
            msgResult = emuInstance->loadState(msg.param.value<QString>().toStdString());
            break;

        case msg_UndoStateLoad:
            emuInstance->undoStateLoad();
            msgResult = 1;
            break;

        case msg_ImportSavefile:
            {
                msgResult = 0;
                auto f = Platform::OpenFile(msg.param.value<QString>().toStdString(), Platform::FileMode::Read);
                if (!f) break;

                u32 len = FileLength(f);

                std::unique_ptr<u8[]> data = std::make_unique<u8[]>(len);
                Platform::FileRewind(f);
                Platform::FileRead(data.get(), len, 1, f);

                assert(emuInstance->nds != nullptr);
                emuInstance->nds->SetNDSSave(data.get(), len);

                CloseFile(f);
                msgResult = 1;
            }
            break;

        case msg_EnableCheats:
            emuInstance->enableCheats(msg.param.value<bool>());
            break;
        }

        msgSemaphore.release();
        // Acknowledgement transfers ownership immediately. Leave any later
        // settings/state commands queued until the UI returns the borrow.
        if (glborrow) break;
    }
    msgMutex.unlock();

    if (glborrow)
    {
        glBorrowMutex.lock();
        while (glBorrowed)
            glBorrowCond.wait(&glBorrowMutex);
        glBorrowMutex.unlock();
    }
}

void EmuThread::changeWindowTitle(char* title)
{
    emit windowTitleChange(QString(title));
}

void EmuThread::initContext(int win)
{
    sendMessage({.type = msg_InitGL, .param = win});
    waitMessage();
}

void EmuThread::deinitContext(int win)
{
    sendMessage({.type = msg_DeInitGL, .param = win});
    waitMessage();
}

void EmuThread::borrowGL()
{
    sendMessage(msg_BorrowGL);
    waitMessage();
}

void EmuThread::returnGL()
{
    glBorrowMutex.lock();
    glBorrowed = false;
    glBorrowCond.wakeAll();
    glBorrowMutex.unlock();
}

void EmuThread::borrowNextFrameGL()
{
    sendMessage(msg_BorrowGLAfterFrame);
    returnGL();
    waitMessage();
}

void EmuThread::emuRun()
{
    sendMessage(msg_EmuRun);
    waitMessage();
}

void EmuThread::emuPause(bool broadcast)
{
    sendMessage(msg_EmuPause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Pause);
}

void EmuThread::emuUnpause(bool broadcast)
{
    sendMessage(msg_EmuUnpause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Unpause);
}

void EmuThread::emuTogglePause(bool broadcast)
{
    if (emuStatus == emuStatus_Paused)
        emuUnpause(broadcast);
    else
        emuPause(broadcast);
}

void EmuThread::emuStop(bool external)
{
    sendMessage({.type = msg_EmuStop, .param = external});
    waitMessage();
}

void EmuThread::emuExit()
{
    sendMessage(msg_Exit);
    waitAllMessages();
}

void EmuThread::emuFrameStep()
{
    if (emuPauseStack < emuPauseStackPauseThreshold)
        sendMessage(msg_EmuPause);
    sendMessage(msg_EmuFrameStep);
    waitAllMessages();
}

void EmuThread::emuReset()
{
    emuInstance->prepareConsoleRestart();
    sendMessage(msg_EmuReset);
    waitMessage();
    emuInstance->finishConsoleRestart();
}

bool EmuThread::emuIsRunning()
{
    return emuStatus == emuStatus_Running;
}

bool EmuThread::emuIsActive()
{
    return emuActive;
}

int EmuThread::bootROM(const QStringList& filename, QString& errorstr)
{
    emuInstance->prepareConsoleRestart();
    sendMessage({.type = msg_BootROM, .param = filename});
    waitMessage();
    emuInstance->finishConsoleRestart();
    if (!msgResult)
    {
        errorstr = msgError;
        return msgResult;
    }

    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::bootFirmware(QString& errorstr)
{
    emuInstance->prepareConsoleRestart();
    sendMessage(msg_BootFirmware);
    waitMessage();
    emuInstance->finishConsoleRestart();
    if (!msgResult)
    {
        errorstr = msgError;
        return msgResult;
    }

    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::insertCart(const QStringList& filename, bool gba, QString& errorstr)
{
    MessageType msgtype = gba ? msg_InsertGBACart : msg_InsertCart;

    sendMessage({.type = msgtype, .param = filename});
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

void EmuThread::ejectCart(bool gba)
{
    sendMessage(gba ? msg_EjectGBACart : msg_EjectCart);
    waitMessage();
}

int EmuThread::insertGBAAddon(int type, QString& errorstr)
{
    sendMessage({.type = msg_InsertGBAAddon, .param = type});
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

int EmuThread::saveState(const QString& filename)
{
    sendMessage({.type = msg_SaveState, .param = filename});
    waitMessage();
    return msgResult;
}

int EmuThread::loadState(const QString& filename)
{
    sendMessage({.type = msg_LoadState, .param = filename});
    waitMessage();
    return msgResult;
}

int EmuThread::undoStateLoad()
{
    sendMessage(msg_UndoStateLoad);
    waitMessage();
    return msgResult;
}

int EmuThread::importSavefile(const QString& filename)
{
    emuInstance->prepareConsoleRestart();
    sendMessage(msg_EmuReset);
    sendMessage({.type = msg_ImportSavefile, .param = filename});
    waitMessage(2);
    emuInstance->finishConsoleRestart();
    return msgResult;
}

void EmuThread::enableCheats(bool enable)
{
    sendMessage({.type = msg_EnableCheats, .param = enable});
    waitMessage();
}

void EmuThread::updateRenderer()
{
    auto& cfg = emuInstance->getGlobalConfig();
    const bool computeShaders = emuInstance->getMainWindow()->supportsComputeShaders();
    Config::ConstrainVideoSettings(cfg, computeShaders);
    if (!computeShaders && videoRenderer == renderer3D_OpenGLCompute)
        videoRenderer = renderer3D_OpenGL;
    if (WideMelon::Enabled() && videoRenderer == renderer3D_Software)
        videoRenderer = renderer3D_OpenGL;
    auto nds = emuInstance->nds;

    if (videoRenderer != lastVideoRenderer)
    {
        switch (videoRenderer)
        {
            case renderer3D_Software:
                nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));
                break;
            case renderer3D_OpenGL:
                nds->SetRenderer(std::make_unique<GLRenderer>(*nds, false));
                break;
            case renderer3D_OpenGLCompute:
                nds->SetRenderer(std::make_unique<GLRenderer>(*nds, true));
                break;
            default: __builtin_unreachable();
        }
    }
    lastVideoRenderer = videoRenderer;

    auto readScaleAlgorithm = [&cfg](const char* algorithmKey, bool allowXBRZ)
    {
        auto algorithm = melonDS::RendererSettings::GetGLScaleAlgorithm(cfg.GetInt(algorithmKey));
        if (!allowXBRZ && algorithm == melonDS::RendererSettings::GLScaleAlgorithm::XBRZ)
            algorithm = melonDS::RendererSettings::GLScaleAlgorithm::Spline36;
        return algorithm;
    };
    melonDS::RendererSettings settings = {
        .ScaleFactor = cfg.GetInt("3D.GL.ScaleFactor"),
        .WholeScene2D = {
            .Enabled = cfg.GetBool("3D.GL.WholeScene2DScale"),
            .SourceBoundaryGuard = cfg.GetBool("3D.GL.WholeScene2DScaleSourceBoundaryGuard"),
            .Mode = melonDS::RendererSettings::GetWholeScene2DScaleMode(cfg.GetInt("3D.GL.WholeScene2DScaleMode")),
            .Algorithm = readScaleAlgorithm("3D.GL.WholeScene2DScaleAlgorithm", true),
            .FragmentationFallback = melonDS::RendererSettings::GetWholeScene2DFragmentationFallback(
                cfg.GetInt("3D.GL.WholeScene2DScaleFragmentationFallback")),
            .ExactFinalFallback = cfg.GetBool("3D.GL.WholeScene2DScaleExactFinalFallback"),
            .ForegroundOverlay = cfg.GetBool("3D.GL.WholeScene2DScaleForegroundOverlay"),
            .CaptureBacked = cfg.GetBool("3D.GL.WholeScene2DScaleCaptureBacked"),
            .DebugTint = cfg.GetBool("3D.GL.WholeScene2DScaleDebugTint"),
            .NoWrapFilterTaps = cfg.GetBool("3D.GL.WholeScene2DScaleNoWrapFilterTaps"),
            .FinalUpscaleRender3DNative = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscaleRender3DNative"),
            .FinalUpscale3DFilter = melonDS::RendererSettings::GetFinalUpscale3DDownsampleFilter(
                cfg.GetInt("3D.GL.WholeScene2DScaleFinalUpscale3DFilter")),
            .FinalUpscale3DCoverageAware = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DCoverageAware"),
            .FinalUpscale3DRepresentativeSemantics = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DRepresentativeSemantics"),
            .FinalUpscale3DSplitSemantics = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSplitSemantics"),
            .FinalUpscale3DSharpenSplitCoverage = cfg.GetBool("3D.GL.WholeScene2DScaleFinalUpscale3DSharpenSplitCoverage"),
            .OverlayLegacyUnderlay = cfg.GetBool("3D.GL.WholeScene2DScaleOverlayLegacyUnderlay"),
            .HybridWindowEdgeAssist = cfg.GetBool("3D.GL.WholeScene2DScaleHybridWindowEdgeAssist"),
            .HybridTarget2AlphaBlendAssist = cfg.GetBool("3D.GL.WholeScene2DScaleHybridTarget2AlphaBlendAssist"),
            .HybridNativeEffectGuard = cfg.GetBool("3D.GL.WholeScene2DScaleHybridNativeEffectGuard"),
            .HybridForeground2DBase = cfg.GetBool("3D.GL.WholeScene2DScaleHybridForeground2DBase"),
            .HybridCleanLegacyCandidate = cfg.GetBool("3D.GL.WholeScene2DScaleHybridCleanLegacyCandidate"),
            .HybridStrictAffineHighRes = cfg.GetBool("3D.GL.WholeScene2DScaleHybridStrictAffineHighRes"),
        .HybridAffineAlpha = melonDS::RendererSettings::GetAffineAlphaReconstruction(cfg.GetInt("3D.GL.HybridAffineAlpha")),
        .HybridAffineSampling = melonDS::RendererSettings::GetAffineSampling(cfg.GetInt("3D.GL.HybridAffineSampling")),
        .HybridNNEDI3PremultipliedRGB = cfg.GetBool("3D.GL.HybridNNEDI3PremultipliedRGB"),
            .HybridStrictAffineSourceEnhancement = cfg.GetBool("3D.GL.WholeScene2DScaleHybridStrictAffineSourceEnhancement"),
            .HybridStrictAffineConnectedSources = cfg.GetBool("3D.GL.WholeScene2DScaleHybridStrictAffineConnectedSources"),
            .HybridStrictAffineOpaqueAssemblies = cfg.GetBool("3D.GL.WholeScene2DScaleHybridStrictAffineOpaqueAssemblies"),
            .HybridStrictAffineTopTextBG = cfg.GetBool("3D.GL.WholeScene2DScaleHybridStrictAffineTopTextBG"),
            .HybridStrictAffineMaskedOBJMLAA = cfg.GetBool("3D.GL.WholeScene2DScaleHybridStrictAffineMaskedOBJMLAA"),
        },
        .ReadableTextureCache = cfg.GetBool("3D.GL.ReadableTextureCache"),
        .TextureFilter = {
            .Anisotropy = cfg.GetInt("3D.GL.TextureAnisotropy"),
            .BinaryAlphaHandling = cfg.GetBool("3D.GL.TextureFilterBinaryAlphaHandling"),
            .TopologyAwareMipHandling = cfg.GetBool("3D.GL.TextureFilterMipmapPremultipliedAlphaHandling"),
            .MipmapSubrectHandling = cfg.GetBool("3D.GL.TextureFilterMipmapSubrectHandling"),
            .Smart2DFiltering = cfg.GetBool("3D.GL.TextureFilterSmart2D"),
            .TranslucentTextureFilteringGuard = cfg.GetBool("3D.GL.TextureFilterTranslucentGuard"),
            .SpriteUVInset = cfg.GetBool("3D.GL.TextureFilterSpriteUVInset"),
            .MipmapAlphaHandling = cfg.GetBool("3D.GL.TextureFilterMipmapAlphaHandling"),
            .MipDepth = melonDS::RendererSettings::GetTextureFilterMipDepth(cfg.GetInt("3D.GL.TextureFilterMipDepth")),
        },
        .TextureScaling = {
            .Enabled = cfg.GetBool("3D.GL.TextureScaling"),
            .Algorithm = readScaleAlgorithm("3D.GL.TextureScalingAlgorithm", true),
            .FrequentChangePolicy = cfg.GetBool("3D.GL.TextureScalingFrequentChangePolicy"),
            .Deferred = cfg.GetBool("3D.GL.TextureScalingDeferred"),
            .NativeMipFloor = cfg.GetBool("3D.GL.TextureScalingNativeMipFloor"),
            .SourceMips = cfg.GetBool("3D.GL.TextureScalingSourceMips"),
            .EdgeExtendUnusedMargins = cfg.GetBool("3D.GL.TextureScalingEdgeExtendUnusedMargins"),
            .ReconstructCompatible3D = cfg.GetBool("3D.GL.ReconstructCompatible3D"),
            .ReconstructCompatible3DEdgeContext = cfg.GetBool("3D.GL.ReconstructCompatible3DEdgeContext"),
            .ReconstructCompatible3DFractionalAlpha = cfg.GetBool("3D.GL.ReconstructCompatible3DFractionalAlpha"),
            .LegacyAlphaHandling = cfg.GetBool("3D.GL.TextureScalingLegacyAlphaHandling"),
            .QualityAlphaHandling = cfg.GetBool("3D.GL.TextureScalingQualityAlphaHandling"),
            .Alpha = RendererSettings::GetTextureAlpha(cfg.GetInt("3D.GL.TextureScalingAlpha")),
        },
        .Threaded = cfg.GetBool("3D.Soft.Threaded"),
        .HiresCoordinates = cfg.GetBool("3D.GL.HiresCoordinates"),
        .HighPrecisionTextureCoordinates = cfg.GetBool("3D.GL.TextureScalingHighPrecisionCoordinates"),
        .MSAA = cfg.GetBool("3D.GL.MSAA"),
        .BetterPolygons = cfg.GetBool("3D.GL.BetterPolygons")
    };

    nds->GetRenderer().SetRenderSettings(settings);
}

void EmuThread::compileShaders()
{
    auto& renderer = emuInstance->nds->GPU.GetRenderer();
    int currentShader, shadersCount;
    u64 startTime = SDL_GetPerformanceCounter();
    // kind of hacky to look at the wallclock, though it is easier than
    // than disabling vsync
    do
    {
        renderer.ShaderCompileStep(currentShader, shadersCount);
    }
    while (renderer.NeedsShaderCompile() &&
             (SDL_GetPerformanceCounter() - startTime) * perfCountsSec < 1.0 / 6.0);
    emuInstance->osdAddMessage(0, "Compiling shader %d/%d", currentShader+1, shadersCount);
}
