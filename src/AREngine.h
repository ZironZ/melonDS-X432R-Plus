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

#ifndef ARENGINE_H
#define ARENGINE_H

#include <vector>
#include "ARCodeFile.h"
#include <atomic>

namespace melonDS
{
namespace WideGamePatches { struct VisibilityProfile; }
class NDS;
class AREngine
{
public:
    AREngine(melonDS::NDS& nds);

    std::vector<ARCode> Cheats {};
    enum class WidescreenStatus { Unavailable, UnsupportedAspect, Waiting, Running, InvalidCode };
    void OnCartChanged();
    void ResetGamePatches();
    void RunGamePatches();
    float GamePatchAspect(int screen) const;
    WidescreenStatus GetWidescreenStatus() const { return PatchStatus.load(); }
private:
    friend class ARM;
    void RunCheats();
    bool RunCheat(const ARCode& arcode, u32* changedWrites = nullptr);

    melonDS::NDS& NDS;
    ARCode WidescreenCode {};
    const WideGamePatches::VisibilityProfile* VisibilityFix = nullptr;
    u32 ReportedCullMask = 0;
    bool ReportedCommunity = false;
    std::atomic<float> PatchAspect {1.f};
    std::atomic<WidescreenStatus> PatchStatus {WidescreenStatus::Unavailable};
};

}
#endif // ARENGINE_H
