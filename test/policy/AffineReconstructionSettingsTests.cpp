// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolicyTestHarness.h"
#include "RendererSettings.h"

using namespace melonDS;

POLICY_TEST(AffineReconstructionDefaultsToAutomaticAndValidatesChoices)
{
    using Settings = RendererSettings;
    const Settings::WholeScene2DScaleSettings defaults;
    CHECK(defaults.HybridAffineAlpha == Settings::AffineAlphaReconstruction::Automatic);
    CHECK(defaults.HybridAffineSampling == Settings::AffineSampling::OutputGrid);
    // Invalid values must select a supported mode.
    for (int value : {-100, -1, 6, 100})
        CHECK(Settings::GetAffineAlphaReconstruction(value) == Settings::AffineAlphaReconstruction::Bilinear);
    for (int value : {-100, -1, 2, 100})
        CHECK(Settings::GetAffineSampling(value) == Settings::AffineSampling::OutputGrid);
}

POLICY_TEST(AutomaticAffineAlphaFollowsSelectedScalerAndPreservesOverrides)
{
    using S = RendererSettings;
    using A = S::AffineAlphaReconstruction;
    CHECK(S::GetAffineAlphaReconstruction(5) == A::Automatic);
    const A expected[] = {A::Spline36, A::NNEDI3, A::XBRZ, A::NNEDI3, A::NNEDI3, A::NNEDI3};
    for (int rgb = 0; rgb < 6; ++rgb)
    {
        const auto algorithm = S::GetGLScaleAlgorithm(rgb);
        CHECK(S::ResolveAffineAlphaReconstruction(A::Automatic, algorithm) == expected[rgb]);
        for (int alpha = 0; alpha < 5; ++alpha)
        {
            const auto explicitAlpha = S::GetAffineAlphaReconstruction(alpha);
            CHECK(S::ResolveAffineAlphaReconstruction(explicitAlpha, algorithm) == explicitAlpha);
        }
    }
}

POLICY_TEST(AffineReconstructionPreservesExplicitSavedSelections)
{
    // Each explicit configuration value must select the corresponding mode.
    using Settings = RendererSettings;
    using Alpha = Settings::AffineAlphaReconstruction;
    const Alpha choices[] = {Alpha::NativeMask, Alpha::Bilinear, Alpha::Spline36, Alpha::NNEDI3, Alpha::XBRZ};
    for (int value = 0; value < 5; ++value)
        CHECK(Settings::GetAffineAlphaReconstruction(value) == choices[value]);
    CHECK(Settings::GetAffineSampling(0) == Settings::AffineSampling::OutputGrid);
    CHECK(Settings::GetAffineSampling(1) == Settings::AffineSampling::Supersample2x);
}

POLICY_TEST(ScalingWithoutComputeUsesSupportedRGBAndAlphaTogether)
{
    using S = RendererSettings;
    using A = S::AffineAlphaReconstruction;
    using T = S::TextureAlpha;
    // Cover imported configurations, including neural alpha with non-neural RGB.
    for (int rgb = 0; rgb < 6; ++rgb)
    for (int alpha = 0; alpha < 6; ++alpha)
    for (int textureAlpha = 0; textureAlpha < 4; ++textureAlpha)
    {
        S settings{};
        settings.ScaleFactor = 4;
        settings.WholeScene2D.Enabled = true;
        settings.TextureScaling.Enabled = true;
        settings.WholeScene2D.Algorithm = S::GetGLScaleAlgorithm(rgb);
        settings.TextureScaling.Algorithm = S::GetGLScaleAlgorithm(rgb);
        settings.WholeScene2D.HybridAffineAlpha = S::GetAffineAlphaReconstruction(alpha);
        settings.TextureScaling.Alpha = S::GetTextureAlpha(textureAlpha);
        settings.WholeScene2D.HybridNNEDI3PremultipliedRGB = true;

        auto supported = settings;
        supported.ApplyComputeShaderSupport(true);
        CHECK(supported.WholeScene2D == settings.WholeScene2D);
        CHECK(supported.TextureScaling == settings.TextureScaling);

        settings.ApplyComputeShaderSupport(false);
        const auto expectedRGB = rgb == 2 ? S::GLScaleAlgorithm::XBRZ : S::GLScaleAlgorithm::Spline36;
        CHECK(settings.WholeScene2D.Algorithm == expectedRGB);
        CHECK(settings.TextureScaling.Algorithm == expectedRGB);
        const auto expectedAlpha = alpha == 3 ? A::Automatic : S::GetAffineAlphaReconstruction(alpha);
        CHECK(settings.WholeScene2D.HybridAffineAlpha == expectedAlpha);
        CHECK(S::ResolveAffineAlphaReconstruction(expectedAlpha, expectedRGB) != A::NNEDI3);
        CHECK(settings.TextureScaling.Alpha == (textureAlpha == 2 ? T::Bilinear : S::GetTextureAlpha(textureAlpha)));
        CHECK(!settings.WholeScene2D.HybridNNEDI3PremultipliedRGB);
        CHECK(settings.ScaleFactor == 4);
        CHECK(settings.WholeScene2D.Enabled);
        CHECK(settings.TextureScaling.Enabled);

        const auto normalized = settings;
        settings.ApplyComputeShaderSupport(false);
        CHECK(settings.WholeScene2D == normalized.WholeScene2D);
        CHECK(settings.TextureScaling == normalized.TextureScaling);
    }
}
