// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RendererTestEnvironment.h"
#include <QJsonArray>
#include <QTimeZone>

bool RendererTestEnvironment::parse(const QJsonObject& root, bool hasSavestate,
                                   RendererTestEnvironment& result, QString& error)
{
    RendererTestEnvironment env;
    auto reject = [&](const QString& reason) { error = reason; return false; };
    if (root.contains("purpose") && !root.value("purpose").isString())
        return reject("purpose must be comparison, performance, or diagnostic.");
    env.purpose = root.value("purpose").toString("comparison");
    if (env.purpose != "comparison" && env.purpose != "performance" && !env.diagnostic())
        return reject("purpose must be comparison, performance, or diagnostic.");
    for (const char* field : {"rtc", "captures"})
        if (root.contains(field) && !root.value(field).isObject())
            return reject(QString("%1 must be an object.").arg(field));

    const QJsonObject rtc = root.value("rtc").toObject();
    if (rtc.contains("mode") && !rtc.value("mode").isString())
        return reject("rtc.mode must be savestate, fixed, or host.");
    env.rtcMode = rtc.value("mode").toString(hasSavestate ? "savestate" : "fixed");
    if (env.rtcMode != "savestate" && env.rtcMode != "fixed" && env.rtcMode != "host")
        return reject("rtc.mode must be savestate, fixed, or host.");
    if (env.rtcMode == "savestate" && !hasSavestate)
        return reject("rtc.mode=savestate requires a savestate.");
    if (env.rtcMode == "host" && !env.diagnostic())
        return reject("Host RTC is allowed only with purpose=diagnostic; comparison runs require deterministic time.");
    if (rtc.contains("dateTime") && env.rtcMode != "fixed")
        return reject("rtc.dateTime is allowed only with rtc.mode=fixed.");

    const QString dateTime = rtc.value("dateTime").toString("2000-01-01T12:00:00");
    // These are DS civil-time components, independent of the host timezone/DST.
    const QDate date = QDate::fromString(dateTime.left(10), "yyyy-MM-dd");
    const QTime time = QTime::fromString(dateTime.mid(11), "HH:mm:ss");
    if ((rtc.contains("dateTime") && !rtc.value("dateTime").isString()) ||
        dateTime.size() != 19 || dateTime[10] != 'T' || !date.isValid() || !time.isValid() ||
        date.year() < 2000 || date.year() > 2099)
        return reject("rtc.dateTime must be a valid DS time: yyyy-MM-ddTHH:mm:ss, years 2000-2099, without a timezone.");
    env.fixedDateTime = QDateTime(date, time, QTimeZone("UTC"));

    const QJsonObject captures = root.value("captures").toObject();
    for (const char* field : {"captureLumaProbe", "wholeSceneCsv", "rollingDump",
                              "profileDebugReads", "testDebugFrameHandoff", "testDebugProductValidity"})
        if (captures.contains(field) && !captures.value(field).isBool())
            return reject(QString("captures.%1 must be a boolean.").arg(field));
    env.captureLumaProbe = captures.value("captureLumaProbe").toBool(false);
    if (env.captureLumaProbe && !env.diagnostic())
        return reject("captures.captureLumaProbe requires purpose=diagnostic; its synchronous readback invalidates performance comparisons.");
    env.imageReadbacks = !captures.value("frames").toArray().isEmpty() ||
        captures.value("rollingDump").toBool() || captures.value("profileDebugReads").toBool() ||
        captures.value("testDebugFrameHandoff").toBool() || captures.value("testDebugProductValidity").toBool();
    if (env.purpose == "performance" && env.imageReadbacks)
        return reject("Performance runs cannot request captures.frames, rollingDump, or debug readback tests; run image comparisons separately.");
    result = env;
    error.clear();
    return true;
}

QJsonObject RendererTestEnvironment::toJson() const
{
    return {{"schemaVersion", 1}, {"purpose", purpose},
            {"captureLumaProbeRequested", captureLumaProbe}, {"imageReadbacksRequested", imageReadbacks},
            {"rtcMode", rtcMode}, {"hostRtcSyncSuppressed", deterministicRTC()},
            {"bootRtcSeed", fixedDateTime.toString("yyyy-MM-ddTHH:mm:ss")}};
}

QString RendererTestEnvironment::validateObserved(bool probeEnabled, quint64 probeReadbacks, quint64 hostRTCWrites) const
{
    if (probeEnabled != captureLumaProbe)
        return "Capture-luma probe state changed during the replay; results are not comparable.";
    if (!captureLumaProbe && probeReadbacks != 0)
        return "Unexpected capture-luma readback contaminated the replay.";
    if (deterministicRTC() && hostRTCWrites != 0)
        return "A host RTC write contaminated the deterministic replay.";
    return {};
}
