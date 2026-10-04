// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideDisplayPolicy.h"
using namespace melonDS;

POLICY_TEST(WideDisplayBrightnessIsIndependentOfSourceAvailability)
{
    for (int engine : {0, 1})
        for (u32 mode : {1u, 2u, 3u})
            for (u16 brightness : {u16(0), u16(0x400C), u16(0x801F), u16(0xC010)})
            {
                if (engine && mode != 1) continue;
                const auto p = WideDisplayPresentation(true, true, false, engine, mode << 16, brightness);
                CHECK_EQ(p[0], -1);
                CHECK_EQ(p[1], brightness >> 14);
                CHECK_EQ(p[2], std::min<int>(brightness & 31, 16));
            }
}

POLICY_TEST(WideDisplayPowerAndBlankingMatchNativePresentation)
{
    for (int engine : {0, 1})
    {
        const auto powerOff = WideDisplayPresentation(false, true, false, engine, 1 << 16, 0x4010);
        CHECK_EQ(powerOff[0], 0);
        CHECK_EQ(powerOff[1], 0);
        const auto displayOff = WideDisplayPresentation(true, true, true, engine, 0, 0x8010);
        CHECK_EQ(displayOff[0], 63);
        CHECK_EQ(displayOff[1], 0);
        const auto blank = WideDisplayPresentation(true, true, true, engine, 1 << 16, 0x8008);
        CHECK_EQ(blank[0], 63);
        CHECK_EQ(blank[1], 2);
        CHECK_EQ(blank[2], 8);
        const auto disabledUnit = WideDisplayPresentation(true, false, true, engine, 1 << 16, 0x4004);
        CHECK_EQ(disabledUnit[0], engine ? 63 : 0);
        CHECK_EQ(disabledUnit[1], 1);
        // VRAM/FIFO presentation bypasses the disabled or blanked compositor.
        if (!engine)
            for (u32 mode : {2u, 3u})
                CHECK_EQ(WideDisplayPresentation(true, false, true, engine, mode << 16, 0)[0], -1);
        else
        {
            CHECK_EQ(WideDisplayPresentation(true, true, false, engine, 2 << 16, 0x8010)[0], 63);
            CHECK_EQ(WideDisplayPresentation(true, true, false, engine, 2 << 16, 0x8010)[1], 0);
        }
    }
}
