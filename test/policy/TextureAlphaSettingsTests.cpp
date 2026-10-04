// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolicyTestHarness.h"
#include "RendererSettings.h"
using namespace melonDS;

POLICY_TEST(TextureAlphaSelectionAndMigration)
{
    using S = RendererSettings;
    using A = S::TextureAlpha;
    CHECK(S::TextureScalingSettings{}.Alpha == A::Bilinear);
    for (int i = 0; i < 4; ++i) CHECK(static_cast<int>(S::GetTextureAlpha(i)) == i);
    for (int i : {-100, -1, 4, 100}) CHECK(S::GetTextureAlpha(i) == A::Bilinear);
    CHECK(S::MigrateTextureAlpha(false, false, S::GLScaleAlgorithm::NNEDI3) == A::Bilinear);
    CHECK(S::MigrateTextureAlpha(false, true, S::GLScaleAlgorithm::NNEDI3) == A::Spline36);
    CHECK(S::MigrateTextureAlpha(true, true, S::GLScaleAlgorithm::NNEDI3) == A::XBRZ);
    CHECK(S::MigrateTextureAlpha(false, false, S::GLScaleAlgorithm::XBRZ) == A::XBRZ);
}

POLICY_TEST(TextureAlphaParticipatesInSettingsInvalidation)
{
    RendererSettings::TextureScalingSettings a, b;
    for (int i = 0; i < 4; ++i)
    {
        b.Alpha = RendererSettings::GetTextureAlpha(i);
        CHECK((a == b) == (i == 0));
    }
}
