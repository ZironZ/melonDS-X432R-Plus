// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSemaphore>
#include <QString>
#include <utility>

struct EmuThreadCommandResult
{
    int Value = 1;
    QString Error;
};

// One reply belongs to one queued command. Completing an unrelated command
// cannot release its waiter or overwrite its result. The queue keeps the reply
// alive until completion, even when its caller only waits for a later command.
class EmuThreadCommandReply
{
public:
    void complete(EmuThreadCommandResult result)
    {
        Result = std::move(result);
        Ready.release();
    }

    EmuThreadCommandResult wait()
    {
        Ready.acquire();
        return Result;
    }

private:
    QSemaphore Ready;
    EmuThreadCommandResult Result;
};
