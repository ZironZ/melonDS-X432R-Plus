// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolicyTestHarness.h"
#include "frontend/qt_sdl/AudioLowPass.h"
#include "frontend/qt_sdl/AudioSpeed.h"
#include "frontend/qt_sdl/AudioStreamRamp.h"
#include "frontend/qt_sdl/AudioTimeStretch.h"

#include <memory>

namespace
{
constexpr double SampleRate = 48000.0;
constexpr double ToneHz = 440.0;

void FillTone(std::vector<int16_t>& samples, int frames, int64_t first, double frequency)
{
    samples.resize(frames * 2);
    for (int i = 0; i < frames; ++i)
    {
        samples[i * 2] = static_cast<int16_t>(std::lround(
            12000.0 * std::sin(2.0 * M_PI * frequency * (first + i) / SampleRate)));
        samples[i * 2 + 1] = -samples[i * 2];
    }
}

double Energy(const std::vector<int16_t>& samples)
{
    double energy = 0.0;
    for (int16_t sample : samples) energy += static_cast<double>(sample) * sample;
    return energy / samples.size();
}

struct SyncCadence
{
    double fps;
    int missingFrames;
};

SyncCadence SimulateAudioSync(int callbackFrames, int renderUS)
{
    // WASAPI was observed requesting 480 frames every 10 ms at 48 kHz,
    // splitting these requests into back-to-back SDL callbacks. The producer
    // cannot render another game frame between callbacks in the same burst.
    // Model normal-speed audioSync, which waits for fill < callbackFrames.
    constexpr int durationUS = 20'000'000;
    int nextDevice = 10'000, nextFrame = renderUS;
    int fill = 0, deviceStaged = 0, frames = 0, missing = 0;
    while (std::min(nextDevice, nextFrame) < durationUS)
    {
        if (nextDevice <= nextFrame)
        {
            const int now = nextDevice;
            deviceStaged += 480;
            while (deviceStaged > 0)
            {
                deviceStaged -= callbackFrames;
                const int got = std::min(fill, callbackFrames);
                missing += callbackFrames - got;
                fill -= got;
            }
            nextDevice += 10'000;
            if (nextFrame == durationUS && fill < callbackFrames)
                nextFrame = now + renderUS;
        }
        else
        {
            const int now = nextFrame;
            ++frames;
            // 48 kHz / 60 FPS, with the SPU's 2048-frame ring capacity.
            fill = std::min(2047, fill + 800);
            nextFrame = fill < callbackFrames ? now + renderUS : durationUS;
        }
    }
    return {frames / 20.0, missing};
}
}

POLICY_TEST(AudioSyncMaintainsNormalSpeedWithBatchedCallbacks)
{
    // Cover the measured boot and saved-scene work times without depending
    // on an audio device, wall-clock scheduling, or a game ROM.
    for (int renderUS : {2500, 7000})
    {
        const auto cadence = SimulateAudioSync(kAudioOutputBufferFrames, renderUS);
        CHECK(std::abs(cadence.fps - 60.0) < 0.1);
        CHECK_EQ(cadence.missingFrames, 0);

        // Negative control: the small-buffer regression must be reproduced.
        const auto small = SimulateAudioSync(256, renderUS);
        CHECK(std::abs(small.fps - 50.0) < 0.1);
        CHECK(small.missingFrames > 0);
    }
}

POLICY_TEST(AudioSpeedSettings)
{
    CHECK(!audioIsOffSpeed(60.0, 60.0));
    CHECK(audioIsOffSpeed(30.0, 60.0));
    CHECK(std::abs(audioComputeOutputSkew(INTERNAL_FRAME_RATE) - 1.0) < 1e-12);
    CHECK(audioComputeOutputSkew(0.0001) >= kMinOutputSkew);
    CHECK(audioComputeStretchRatio(1e10, 256, 0, 4096) <= kMaxStretchRatio);
    CHECK(audioComputeStretchRatio(0.0, 256, 0, 4096) >= kMinStretchRatio);
    CHECK_EQ(audioComputeLowPassCutoff(120, 60, 12000, 21600), 6000);
    CHECK_EQ(audioComputeLowPassCutoff(30, 60, 12000, 21600), 21600);
    CHECK_EQ(audioComputeLowPassCutoff(120, 60, kSpeedUpLowPassOff, 21600), 21600);
    CHECK_EQ(AudioTimeStretch::TargetInputFill(1e10), AudioTimeStretch::kInputCapacity / 2);
}

POLICY_TEST(AudioStretchPreservesPitchAndStereoAcrossSpeeds)
{
    // Check the actual output frequency at slow-mo and up to 32x, rather
    // than merely testing the requested ratio. Opposite channels also expose
    // independent-channel splices or ring-wrap corruption.
    for (double ratio : {0.5, 1.0, 2.0, 8.0, 32.0})
    {
        auto stretch = std::make_unique<AudioTimeStretch>();
        std::vector<int16_t> input, output(512);
        int64_t written = 0;
        FillTone(input, 12288, written, ToneHz);
        CHECK_EQ(stretch->Write(input.data(), 12288), 12288);
        written += 12288;
        int frames = 0, crossings = 0;
        int16_t previous = 0;
        double energy = 0.0;
        for (int block = 0; block < 100; ++block)
        {
            int n = stretch->Read(output.data(), 256, ratio);
            CHECK_EQ(n, 256);
            if (block >= 4)
            {
                for (int i = 0; i < n; ++i)
                {
                    int16_t sample = output[i * 2];
                    CHECK(std::abs(static_cast<int>(sample) + output[i * 2 + 1]) <= 1);
                    if (previous <= 0 && sample > 0) ++crossings;
                    previous = sample;
                    energy += static_cast<double>(sample) * sample;
                    ++frames;
                }
            }
            const int offer = static_cast<int>(256 * ratio);
            FillTone(input, offer, written, ToneHz);
            int accepted = stretch->Write(input.data(), offer);
            CHECK_EQ(accepted, offer);
            written += accepted;
        }
        const double frequency = crossings * SampleRate / frames;
        if (std::abs(frequency - ToneHz) >= 15.0)
            std::printf("Audio ratio %.2f: expected %.2f Hz, got %.2f Hz\n", ratio, ToneHz, frequency);
        CHECK(std::abs(frequency - ToneHz) < 15.0);
        CHECK(energy / frames > 4e7);
    }
}

POLICY_TEST(AudioStretchOverflowAndDiscontinuity)
{
    auto stretch = std::make_unique<AudioTimeStretch>();
    std::vector<int16_t> input(AudioTimeStretch::kInputCapacity * 2, 12000);
    std::vector<int16_t> output(512);
    CHECK_EQ(stretch->Write(input.data(), AudioTimeStretch::kInputCapacity),
             AudioTimeStretch::kInputCapacity);
    CHECK_EQ(stretch->Write(input.data(), 1), 0);
    stretch->Read(output.data(), 256, 1.0);
    stretch->BeginSession();
    // Discard queued output on the next read. One analysis window of input
    // remains for the overlap; it must not keep old audio alive afterwards.
    stretch->Read(output.data(), 256, 1.0);
    std::fill(input.begin(), input.end(), 0);
    CHECK_EQ(stretch->Write(input.data(), 4096), 4096);
    CHECK_EQ(stretch->Read(output.data(), 256, 1.0), 256);
    stretch->Read(output.data(), 256, 1.0);
    stretch->Read(output.data(), 256, 1.0);
    CHECK(Energy(output) == 0.0);
    stretch->Reset();
    CHECK_EQ(stretch->Read(output.data(), 256, 1.0), 0);
    CHECK_EQ(stretch->TotalWritten(), 0);
}

POLICY_TEST(AudioLowPassBypassAndAttenuation)
{
    AudioLowPass filter;
    filter.Init(SampleRate);
    std::vector<int16_t> samples, original;
    FillTone(samples, 256, 0, 10000.0);
    original = samples;
    filter.Process(samples.data(), 256, filter.WideOpenCutoff(), 256 / SampleRate);
    CHECK(samples == original);
    double filteredEnergy = 0.0, sourceEnergy = 0.0;
    for (int block = 0; block < 150; ++block)
    {
        FillTone(samples, 256, block * 256, 10000.0);
        const double source = Energy(samples);
        filter.Process(samples.data(), 256, 2000.0, 256 / SampleRate);
        if (block >= 100)
        {
            sourceEnergy += source;
            filteredEnergy += Energy(samples);
        }
    }
    CHECK(filteredEnergy < sourceEnergy * 0.001);
    filter.ProcessMuted(48000, 2000.0, 1.0);
    std::fill(samples.begin(), samples.end(), 0);
    filter.Process(samples.data(), 256, 2000.0, 256 / SampleRate);
    CHECK(Energy(samples) == 0.0);
}

POLICY_TEST(AudioPauseTailAndResumeRamp)
{
    AudioStreamRamp ramp;
    std::vector<int16_t> samples;
    FillTone(samples, 2048, 0, ToneHz);
    ramp.Track(samples.data(), 2048);
    const int last = samples[samples.size() - 2];
    ramp.End();
    samples.resize(AudioStreamRamp::kTailFrames * 2);
    ramp.FillTail(samples.data(), AudioStreamRamp::kTailFrames);
    CHECK(std::abs(samples[0] - last) < 1000);
    CHECK_EQ(samples[samples.size() - 2], 0);
    CHECK(ramp.TailSpent());
    ramp.Begin();
    samples.assign((AudioStreamRamp::kMuteFrames + AudioStreamRamp::kRampInFrames) * 2, 12000);
    ramp.Track(samples.data(), samples.size() / 2);
    for (int i = 0; i < AudioStreamRamp::kMuteFrames * 2; ++i) CHECK_EQ(samples[i], 0);
    CHECK(samples[AudioStreamRamp::kMuteFrames * 2] < 10);
    CHECK_EQ(samples.back(), 12000);
    CHECK(!ramp.Ended());

    // Muting during a tail must discard it before tracking silence; Track
    // deliberately completes a tail even when its input happens to be zero.
    ramp.End();
    ramp.Reset();
    ramp.Begin();
    samples.assign(256 * 2, 0);
    ramp.Track(samples.data(), 256);
    CHECK(Energy(samples) == 0.0);
    CHECK(ramp.TailSpent());
}
