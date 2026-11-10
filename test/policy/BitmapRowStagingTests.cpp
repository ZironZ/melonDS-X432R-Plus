// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "GLBitmapRowStaging.h"
using namespace melonDS;

POLICY_TEST(BitmapRowsPreserveExactNativeSamplingAndRejectOtherGeometry)
{
    for (u32 type : {4u,5u})
    {
        BitmapRowSampling row{type,256,256,16384,0,0,256,0,0,1,false};
        const u32 stride = type == 5 ? 512 : 256;
        for (int y : {0,1,191,255,256,3840,65535})
        {
            row.Y=y;
            CHECK_EQ(BitmapRowSourceAddress(row, 256*1024), 16384+(y>>8)*stride);
        }
        row.Y=0;
        for (int field=0; field<10; ++field)
        {
            auto changed=row;
            switch(field) {
            case 0: changed.Type=3; break;
            case 1: changed.Width=512; break;
            case 2: changed.Height=128; break;
            case 3: changed.X=1; break;
            case 4: changed.Y=-1; break;
            case 5: changed.A=255; break;
            case 6: changed.B=1; break;
            case 7: changed.C=1; break;
            case 8: changed.D=2; break;
            case 9: changed.Mosaic=true; break;
            }
            CHECK_EQ(BitmapRowSourceAddress(changed,256*1024),-1);
        }
        row.D=0; CHECK_EQ(BitmapRowSourceAddress(row,256*1024),16384);
        row.D=-1; CHECK_EQ(BitmapRowSourceAddress(row,256*1024),-1);
        row.D=1; row.Y=65536; CHECK_EQ(BitmapRowSourceAddress(row,256*1024),-1);
        row.Y=0; row.MapOffset=128*1024-stride;
        CHECK_EQ(BitmapRowSourceAddress(row,128*1024),128*1024-stride);
        row.MapOffset++; CHECK_EQ(BitmapRowSourceAddress(row,128*1024),-1);
        row.MapOffset=0xFFFFFF00; CHECK_EQ(BitmapRowSourceAddress(row,128*1024),-1);
    }
}

POLICY_TEST(BitmapRowsRespectTheMainAndSubEngineAddressSpaces)
{
    for (u32 type : {4u, 5u})
    {
        const u32 stride = type == 5 ? 512 : 256;
        BitmapRowSampling row{type, 256, 256, 256 * 1024, 0, 255 * 256,
                              256, 0, 0, 1, false};
        CHECK_EQ(BitmapRowSourceAddress(row, 512 * 1024), 256 * 1024 + 255 * stride);
        CHECK_EQ(BitmapRowSourceAddress(row, 128 * 1024), -1);
        row.MapOffset = 512 * 1024 - 256 * stride;
        CHECK_EQ(BitmapRowSourceAddress(row, 512 * 1024), 512 * 1024 - stride);
        row.MapOffset++;
        CHECK_EQ(BitmapRowSourceAddress(row, 512 * 1024), -1);
    }
}
