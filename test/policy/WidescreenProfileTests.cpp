// Projection invariant adapted from WideMelon's tests/profile_test.cpp.
// Copyright (C) 2026 WideMelon contributors
// Modifications Copyright (C) 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "WideMelon.h"
#include "frontend/ScreenLayout.h"
#include <cmath>
#include <limits>

POLICY_TEST(RotatedWidescreenKeepsWorldScaleAndCenter)
{
    for (int height : {192, 256, 342, 456, 768})
        for (int y : {-100000, -10000, 0, 10000, 100000})
        {
            const double actual = (WideMelon::ProjectY(y, height) / 65536.0 + 1) * height / 2;
            const double expected = (y / 65536.0 + 1) * 96 + (height - 192) / 2.0;
            CHECK(std::abs(actual - expected) < 0.01);
        }
    for (int height : {-1, 0, 190, 193, 769}) CHECK_EQ(WideMelon::ValidateHeight(height), 192);
}

POLICY_TEST(RotatedWidescreenCropsUnselectedDisplay)
{
    const int oldHeight = WideMelon::ViewHeight, oldTarget = WideMelon::Target;
    WideMelon::ViewHeight = 456;
    WideMelon::Target = WideMelon::Top;
    CHECK_EQ(WideMelon::DisplayHeight(0), 456);
    CHECK_EQ(WideMelon::DisplayHeight(1), 192);
    CHECK_EQ(WideMelon::ContentHeight(0), 1.f);
    CHECK(std::abs(WideMelon::ContentHeight(1) * 456 - 192) < 0.001f);
    WideMelon::ViewHeight = oldHeight;
    WideMelon::Target = oldTarget;
}

POLICY_TEST(RotatedWidescreenTouchStaysInNativeCenter)
{
    for (int height : {342, 456}) for (int rotation = 0; rotation < 4; ++rotation)
    for (int kind = 0; kind < 4; ++kind) for (bool swap : {false, true})
    {
        ScreenLayout layout;
        layout.Setup(2000, 1600, ScreenLayoutType(kind), ScreenRotation(rotation),
            screenSizing_Even, 8, false, swap, 1.f, 1.f, true, 1.f, height / 192.f);
        float matrices[kMaxScreenTransforms * 6]; int screens[kMaxScreenTransforms];
        const int count = layout.GetScreenTransforms(matrices, screens);
        for (int i = 0; i < count; ++i)
        {
            if (screens[i] != 1) continue;
            const float* m = matrices + i * 6;
            for (int y : {10, 96, 180})
            {
                const float sy = (y - 96.f) * 192.f / height + 96.f;
                int xh = int(std::lround(128 * m[0] + sy * m[2] + m[4]));
                int yh = int(std::lround(128 * m[1] + sy * m[3] + m[5]));
                CHECK(layout.GetTouchCoords(xh, yh, false, 256, height));
                CHECK(std::abs(xh - 128) <= 1);
                CHECK(std::abs(yh - y) <= 1);
            }
        }
    }
}

POLICY_TEST(WidescreenKeepsWorldScaleAndCenter)
{
    for (int width : {256, 308, 342, 448, 682, 768})
    {
        for (int x : {-100000, -50000, -10000, 0, 10000, 50000, 100000})
        {
            const double actual = (WideMelon::ProjectX(x, width) / 65536.0 + 1) * width / 2;
            const double expected = (x / 65536.0 + 1) * 128 + (width - 256) / 2.0;
            CHECK(std::abs(actual - expected) < 0.01);
        }
        if (width > 256) CHECK(WideMelon::ProjectX(70000, width) < 65536);
    }
}

POLICY_TEST(WidescreenRejectsInvalidDimensions)
{
    for (const char* value : {"", "0", "-1", "257", "769", "10000", "garbage", "342x", "999999999999999999999999"})
        CHECK_EQ(WideMelon::ParseWidth(value), 256);
    CHECK_EQ(WideMelon::ParseWidth(nullptr), 256);
    CHECK_EQ(WideMelon::ParseWidth("342"), 342);
    CHECK_EQ(WideMelon::ParseWidth("768"), 768);
}

POLICY_TEST(WidescreenNativeProjectionIsExact)
{
    for (int x : {std::numeric_limits<int>::min(), -123456, 0, 123456, std::numeric_limits<int>::max()})
        CHECK_EQ(WideMelon::ProjectX(x, 256), x);
}

POLICY_TEST(WidescreenDisplaySelectionAndCropping)
{
    const int savedWidth=WideMelon::ViewWidth, savedTarget=WideMelon::Target;
    CHECK_EQ(WideMelon::ValidateDisplayTarget(-1),WideMelon::Top);
    CHECK_EQ(WideMelon::ValidateDisplayTarget(3),WideMelon::Top);
    for(int width : {256,308,342,448,682,768}) for(int target : {0,1,2})
    {
        WideMelon::ViewWidth=width; WideMelon::Target=target;
        for(int screen : {0,1})
        {
            const int displayed=(target==2||screen==target)?width:256;
            CHECK_EQ(WideMelon::DisplayWidth(screen),displayed);
            CHECK(std::abs(WideMelon::ContentWidth(screen)*width-displayed)<0.001f);
        }
    }
    WideMelon::ViewWidth=savedWidth; WideMelon::Target=savedTarget;
}

POLICY_TEST(WidescreenBottomTouchUsesNativeCenterAcrossLayouts)
{
    // Use the real display matrix to locate buttons and verify that the
    // inverse transform returns DS coordinates for mouse, pen and touch.
    for(int width : {256,308,342,682}) for(int rotation=0;rotation<4;rotation++)
    for(int kind=0;kind<4;kind++) for(bool swap : {false,true})
    {
        ScreenLayout layout;
        layout.Setup(2000,1600,ScreenLayoutType(kind),ScreenRotation(rotation),
            screenSizing_Even,8,false,swap,1.f,width/256.f,true);
        float matrices[kMaxScreenTransforms*6]; int screens[kMaxScreenTransforms];
        const int count=layout.GetScreenTransforms(matrices,screens);
        for(int i=0;i<count;i++)
        {
            const float* m=matrices+i*6;
            for(int cx : {0,256}) for(int cy : {0,192})
            {
                const float hx=cx*m[0]+cy*m[2]+m[4], hy=cx*m[1]+cy*m[3]+m[5];
                CHECK(hx>=-0.01f && hx<=2000.01f);
                CHECK(hy>=-0.01f && hy<=1600.01f);
            }
            if(screens[i]!=1) continue;
            const float aspect=std::hypot(m[0],m[1])/std::hypot(m[2],m[3]);
            CHECK(std::abs(aspect-width/256.f)<0.0001f);
            auto hostPoint=[&](float x,float y,int& hx,int& hy) {
                const float sx=(x-128.f)*256.f/width+128.f;
                hx=int(std::lround(sx*m[0]+y*m[2]+m[4]));
                hy=int(std::lround(sx*m[1]+y*m[3]+m[5]));
            };
            for(int px : {1,64,128,191,254})
            {
                int x,y; hostPoint(px+0.5f,96.5f,x,y);
                CHECK(layout.GetTouchCoords(x,y,false,width));
                CHECK(std::abs(x-px)<=1); CHECK(std::abs(y-96)<=1);
            }
            if(width>256)
            {
                for(int outside : {-8,264})
                {
                    int x,y; hostPoint(outside,96,x,y);
                    CHECK(!layout.GetTouchCoords(x,y,false,width));
                    // A drag that started in the center clamps at its edge.
                    hostPoint(128,96,x,y); CHECK(layout.GetTouchCoords(x,y,false,width));
                    hostPoint(outside,96,x,y); CHECK(layout.GetTouchCoords(x,y,true,width));
                    CHECK_EQ(x,outside<0?0:255);
                }
            }
        }
    }
}
