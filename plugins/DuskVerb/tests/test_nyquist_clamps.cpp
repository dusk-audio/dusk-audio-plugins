#include "../src/dsp/DattorroPlateVintage.h"
#include "../src/dsp/DattorroTank.h"
#include "../src/dsp/DenseEarlyField.h"
#include "../src/dsp/DenseHallReverb.h"
#include "../src/dsp/SustainBandLimiter.h"
#include "../src/dsp/VelvetTail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if ! defined(DUSKVERB_NYQUIST_TEST_HOOK)
#error "DuskVerbNyquistClampTest requires DUSKVERB_NYQUIST_TEST_HOOK"
#endif

namespace
{
constexpr int kBlockSize = 256;
constexpr std::array<double, 6> kStandardRates {
    44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
};

struct ClampCase
{
    const char* name;
    float frequency;
    bool hasTwentyHzFloor;
};

// One row per production call site. Repeated frequencies are intentional: the
// regression is a wiring inventory as well as a numeric comparison.
constexpr std::array<ClampCase, 11> kClampCases { {
    { "DattorroPlate prepare front LP", 20000.0f, false },
    { "DattorroPlate prepare post-main LP", 20000.0f, false },
    { "DattorroPlate prepare dense-field LP", 12000.0f, false },
    { "DattorroPlate live front LP", 20000.0f, false },
    { "DattorroPlate live post-main LP", 20000.0f, false },
    { "DattorroTank mode notch", 50000.0f, true },
    { "DenseEarlyField LP", 12000.0f, false },
    { "DenseHall low crossover", 2000.0f, false },
    { "DenseHall high crossover", 14000.0f, false },
    { "SustainBandLimiter peak cut", 18973.666f, false },
    { "VelvetTail crossover", 2000.0f, true },
} };

int failures = 0;
int callCountFailures = 0;

void check (bool condition, const char* description)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
    if (! condition)
        ++failures;
}

void testClampEquivalence()
{
    int mismatches = 0;
    for (double sampleRate : kStandardRates)
    {
        const float sr = static_cast<float> (sampleRate);
        for (const auto& clampCase : kClampCases)
        {
            const float requested = clampCase.hasTwentyHzFloor
                                  ? std::max (clampCase.frequency, 20.0f)
                                  : clampCase.frequency;
            const float legacy = std::min (requested, 0.45f * sr);
            const float shared = DspUtils::nyquistSafeHz (sampleRate, requested);
            if (std::memcmp (&legacy, &shared, sizeof (float)) != 0)
            {
                std::cerr << "mismatch: " << clampCase.name << " @ "
                          << sampleRate << " Hz\n";
                ++mismatches;
            }
        }
    }
    check (mismatches == 0,
           "all 11 call-site cases are byte-identical at six standard sample rates");

    constexpr std::array<float, 5> kIrregularRates {
        8000.125f, 44117.0f, 47999.5f, 123456.75f, 1000000.0f
    };
    int ceilingMismatches = 0;
    for (float sampleRate : kIrregularRates)
    {
        const float legacy = 0.45f * sampleRate;
        const float shared = DspUtils::nyquistSafeHz (
            static_cast<double> (sampleRate), 1000000.0f);
        if (std::memcmp (&legacy, &shared, sizeof (float)) != 0)
            ++ceilingMismatches;
    }
    check (ceilingMismatches == 0,
           "the promoted 0.45f ratio preserves arbitrary float-rate ceilings");

    check (DspUtils::nyquistSafeHz (2.0, 20.0f) == 0.9f,
           "shared ceiling wins after the preserved 20 Hz floor at a degenerate rate");
}

float nextNoise (std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (static_cast<std::int32_t> (state)) / 2147483648.0f;
}

std::vector<float> makeInput (int sampleCount)
{
    std::vector<float> input (static_cast<std::size_t> (sampleCount), 0.0f);
    std::uint32_t state = 0x12345678u;
    for (int i = 0; i < std::min (sampleCount, 2048); ++i)
        input[static_cast<std::size_t> (i)] = 0.2f * nextNoise (state);
    return input;
}

void appendStereo (const std::vector<float>& left, const std::vector<float>& right,
                   std::vector<float>& output)
{
    output.insert (output.end(), left.begin(), left.end());
    output.insert (output.end(), right.begin(), right.end());
}

void appendDattorroPlate (double sampleRate, std::vector<float>& output)
{
    constexpr int sampleCount = 32768;
    const auto input = makeInput (sampleCount);
    std::vector<float> left (static_cast<std::size_t> (sampleCount), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (sampleCount), 0.0f);

    DattorroPlateVintage plate;
    plate.setFrontLoad (1.0f, 12.0f, 75.0f, 20000.0f);
    plate.setPostMainTap (70.0f, 1.0f, 20000.0f);
    plate.setDenseField (1.0f, 4.0f, 700.0f);
    plate.prepare (sampleRate, kBlockSize);

    // Exercise the two prepared-instance coefficient updates as well as the
    // three designs in prepare(). Together these cover all five plate sites.
    plate.setFrontLoad (1.0f, 12.0f, 75.0f, 20000.0f);
    plate.setPostMainTap (70.0f, 1.0f, 20000.0f);

    for (int offset = 0; offset < sampleCount; offset += kBlockSize)
    {
        const int count = std::min (kBlockSize, sampleCount - offset);
        plate.process (input.data() + offset, input.data() + offset,
                       left.data() + offset, right.data() + offset, count);
    }
    appendStereo (left, right, output);
}

void recordCallCount (unsigned int expected, const char* description)
{
    if (DspUtils::nyquistSafeHzCallCount != expected)
    {
        std::cerr << "call-count mismatch: " << description << " (expected "
                  << expected << ", got " << DspUtils::nyquistSafeHzCallCount << ")\n";
        ++callCountFailures;
    }
    DspUtils::resetNyquistSafeHzCallCount();
}

void appendDattorroTank (double sampleRate, std::vector<float>& output)
{
    constexpr int sampleCount = 32768;
    const auto input = makeInput (sampleCount);
    std::vector<float> left (static_cast<std::size_t> (sampleCount), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (sampleCount), 0.0f);

    DattorroTank tank;
    tank.prepare (sampleRate, kBlockSize);
    tank.setModeNotch (50000.0f, -6.0f, 2.0f);
    for (int offset = 0; offset < sampleCount; offset += kBlockSize)
    {
        const int count = std::min (kBlockSize, sampleCount - offset);
        tank.process (input.data() + offset, input.data() + offset,
                      left.data() + offset, right.data() + offset, count);
    }
    appendStereo (left, right, output);
}

void appendDenseEarlyField (double sampleRate, std::vector<float>& output)
{
    constexpr int sampleCount = 8192;
    const auto input = makeInput (sampleCount);
    std::vector<float> left (static_cast<std::size_t> (sampleCount), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (sampleCount), 0.0f);

    DenseEarlyField field;
    field.prepare (sampleRate);
    field.setParams (1.0f, 0.0f, 700.0f);
    for (int i = 0; i < sampleCount; ++i)
        field.processSample (input[static_cast<std::size_t> (i)],
                             left[static_cast<std::size_t> (i)],
                             right[static_cast<std::size_t> (i)]);
    appendStereo (left, right, output);
}

void appendDenseHall (double sampleRate, std::vector<float>& output)
{
    constexpr int sampleCount = 32768;
    const auto input = makeInput (sampleCount);
    std::vector<float> left (static_cast<std::size_t> (sampleCount), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (sampleCount), 0.0f);

    DenseHallReverb hall;
    hall.setCrossoverFreq (2000.0f);
    hall.setHighCrossoverFreq (14000.0f);
    hall.prepare (sampleRate, kBlockSize);
    for (int offset = 0; offset < sampleCount; offset += kBlockSize)
    {
        const int count = std::min (kBlockSize, sampleCount - offset);
        hall.process (input.data() + offset, input.data() + offset,
                      left.data() + offset, right.data() + offset, count);
    }
    appendStereo (left, right, output);
}

void appendPeakCut (double sampleRate, std::vector<float>& output)
{
    constexpr int sampleCount = 2048;
    const auto input = makeInput (sampleCount);
    SustainBandLimiter::PeakCut cut;
    cut.design (18000.0f, 20000.0f, 12.0f, sampleRate);
    for (float sample : input)
        output.push_back (cut.band (sample));
}

void appendVelvetTail (double sampleRate, std::vector<float>& output)
{
    constexpr int sampleCount = 8192;
    const auto input = makeInput (sampleCount);
    std::vector<float> left (static_cast<std::size_t> (sampleCount), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (sampleCount), 0.0f);

    VelvetTail tail;
    tail.prepare (sampleRate, kBlockSize);
    tail.process (input.data(), input.data(), left.data(), right.data(), sampleCount);
    appendStereo (left, right, output);
}

std::vector<float> renderAllSites()
{
    std::vector<float> output;
    DspUtils::resetNyquistSafeHzCallCount();
    for (double sampleRate : kStandardRates)
    {
        appendDattorroPlate (sampleRate, output);
        recordCallCount (5, "DattorroPlate");
        appendDattorroTank (sampleRate, output);
        recordCallCount (1, "DattorroTank");
        appendDenseEarlyField (sampleRate, output);
        recordCallCount (1, "DenseEarlyField");
        appendDenseHall (sampleRate, output);
        recordCallCount (2, "DenseHall");
        appendPeakCut (sampleRate, output);
        recordCallCount (1, "SustainBandLimiter");
        appendVelvetTail (sampleRate, output);
        recordCallCount (3, "VelvetTail");
    }
    return output;
}
} // namespace

int main (int argc, char** argv)
{
    testClampEquivalence();
    const auto output = renderAllSites();
    check (callCountFailures == 0,
           "all 11 production sites route their designs through the shared alias");
    check (! output.empty(), "focused render produced output");
    check (std::all_of (output.begin(), output.end(),
                        [] (float sample) { return std::isfinite (sample); }),
           "focused render is finite at every standard sample rate");
    check (std::any_of (output.begin(), output.end(),
                        [] (float sample) { return sample != 0.0f; }),
           "focused render exercises non-silent filter responses");

    if (argc == 2)
    {
        std::ofstream stream (argv[1], std::ios::binary);
        stream.write (reinterpret_cast<const char*> (output.data()),
                      static_cast<std::streamsize> (output.size() * sizeof (float)));
        check (stream.good(), "focused render bytes written");
    }

    return failures == 0 ? 0 : 1;
}
