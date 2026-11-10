// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EmuThreadCommand.h"
#include <QCoreApplication>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <stdexcept>

namespace
{
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void CheckIsolatedCompletion(bool sharedReply, int earlierValue, int requestedValue)
{
    auto earlier = std::make_shared<EmuThreadCommandReply>();
    auto requested = sharedReply ? earlier : std::make_shared<EmuThreadCommandReply>();
    earlier->complete({earlierValue, QStringLiteral("Earlier command")});

    QSemaphore started;
    auto waiter = std::async(std::launch::async, [&] {
        started.release();
        return requested->wait();
    });
    started.acquire();

    // Unrelated completed commands (including restart cleanup) cannot satisfy
    // this wait, even if their result would look like a successful ROM load.
    EmuThreadCommandReply cleanup;
    cleanup.complete({1, {}});
    const bool blocked = waiter.wait_for(std::chrono::milliseconds(75)) == std::future_status::timeout;
    if (!blocked)
    {
        waiter.get();
        Check(false, "Command returned before its own worker operation completed");
    }

    requested->complete({requestedValue, QStringLiteral("Requested command")});
    const auto result = waiter.get();
    Check(result.Value == requestedValue && result.Error == QStringLiteral("Requested command"),
          "Command received another operation's result or error");
    Check(earlier->wait().Value == earlierValue, "Another waiter consumed the earlier completion");
    Check(cleanup.wait().Value == 1, "Requested command consumed cleanup completion");
    Check(result.Error == QStringLiteral("Requested command"), "Cleanup overwrote the saved result");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        const bool sharedReply = app.arguments().contains(QStringLiteral("--shared-reply"));
        CheckIsolatedCompletion(sharedReply, 1, 0); // stale success must not mask a new failure
        CheckIsolatedCompletion(sharedReply, 0, 1); // stale failure must not reject a new success
        std::puts("Command-specific waits and results passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
