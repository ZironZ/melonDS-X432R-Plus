// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RendererTestEnvironment.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdio>
#include <stdexcept>

namespace
{
void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
RendererTestEnvironment Parse(const char* json, bool state = true)
{
    RendererTestEnvironment env;
    QString error;
    const bool ok = RendererTestEnvironment::parse(QJsonDocument::fromJson(json).object(), state, env, error);
    Check(ok, qPrintable(error));
    return env;
}
void Reject(const char* json, bool state = true)
{
    RendererTestEnvironment env;
    QString error;
    Check(!RendererTestEnvironment::parse(QJsonDocument::fromJson(json).object(), state, env, error) && !error.isEmpty(),
          "unsafe or malformed environment was accepted");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        const auto saved = Parse("{}");
        Check(saved.deterministicRTC() && saved.rtcMode == "savestate" && !saved.captureLumaProbe,
              "existing manifests must preserve the state's RTC and disable the probe");
        const auto boot = Parse("{}", false);
        Check(boot.rtcMode == "fixed" && boot.fixedDateTime.toString("yyyy-MM-ddTHH:mm:ss") == "2000-01-01T12:00:00",
              "fixed boot clock");
        const auto fixed = Parse(R"({"rtc":{"mode":"fixed","dateTime":"2024-02-29T02:30:45"}})");
        Check(fixed.fixedDateTime.date() == QDate(2024, 2, 29) && fixed.fixedDateTime.time() == QTime(2, 30, 45),
              "RTC must use explicit civil components, independent of host DST/timezone");
        Parse(R"({"purpose":"performance","captures":{"wholeSceneCsv":true}})");
        Parse(R"({"captures":{"frames":[{"frame":1,"surfaces":["finalTop"]}]}})");
        const auto diagnostic = Parse(R"({"purpose":"diagnostic","rtc":{"mode":"host"},"captures":{"captureLumaProbe":true}})");
        Check(!diagnostic.deterministicRTC() && diagnostic.captureLumaProbe && diagnostic.diagnostic(), "explicit diagnostics");
        for (const char* json : {
            R"({"purpose":"performance","captures":{"captureLumaProbe":true}})",
            R"({"captures":{"captureLumaProbe":true}})",
            R"({"rtc":{"mode":"host"}})",
            R"({"purpose":"performance","rtc":{"mode":"host"}})",
            R"({"purpose":"performance","captures":{"frames":[{"frame":1}]}})",
            R"({"purpose":"performance","captures":{"rollingDump":true}})",
            R"({"purpose":"performance","captures":{"profileDebugReads":true}})",
            R"({"purpose":"performance","captures":{"testDebugFrameHandoff":true}})",
            R"({"purpose":"performance","captures":{"testDebugProductValidity":true}})",
            R"({"captures":{"captureLumaProbe":"false"}})",
            R"({"purpose":"performnce"})", R"({"rtc":{"mode":"saved"}})",
            R"({"rtc":{"mode":"fixed","dateTime":"2023-02-29T12:00:00"}})",
            R"({"rtc":{"mode":"fixed","dateTime":"1999-12-31T12:00:00"}})",
            R"({"rtc":{"mode":"fixed","dateTime":"2024-01-01T12:00:00Z"}})",
            R"({"rtc":{"mode":"fixed","dateTime":12}})",
            R"({"rtc":{"dateTime":"2024-01-01T12:00:00"}})",
            R"({"rtc":true})"}) Reject(json);
        Reject(R"({"rtc":{"mode":"savestate"}})", false);
        Check(saved.validateObserved(false, 0, 0).isEmpty(), "safe effective environment");
        Check(!saved.validateObserved(true, 0, 0).isEmpty(), "unexpected probe enable must fail");
        Check(!saved.validateObserved(false, 1, 0).isEmpty(), "a readback must fail even after the probe is disabled");
        Check(!saved.validateObserved(false, 0, 1).isEmpty(), "an actual host clock write must fail");
        Check(diagnostic.validateObserved(true, 5, 7).isEmpty(), "declared diagnostics may collect luma and use host time");
        std::puts("[replay environment] PASS");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[replay environment] FAIL: %s\n", error.what());
        return 1;
    }
}
