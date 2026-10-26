// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RENDERERTESTENVIRONMENT_H
#define RENDERERTESTENVIRONMENT_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>

// Replay-only policy. Never changes the user's persisted RTC configuration.
struct RendererTestEnvironment
{
    QString purpose = "comparison";
    QString rtcMode;
    QDateTime fixedDateTime;
    bool captureLumaProbe = false;
    bool imageReadbacks = false;

    bool deterministicRTC() const { return rtcMode != "host"; }
    bool diagnostic() const { return purpose == "diagnostic"; }
    static bool parse(const QJsonObject& root, bool hasSavestate,
                      RendererTestEnvironment& result, QString& error);
    QJsonObject toJson() const;
    QString validateObserved(bool probeEnabled, quint64 probeReadbacks, quint64 hostRTCWrites) const;
};

#endif
