// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#include "SectionReverbEngine.h"
#include "SectionReverbTables.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace duskverb
{
namespace
{

using sectionreverb::Rate;

// ---- Model tables ----

// Input diffusers per side: delay in samples at 44.1 kHz and Size 4 m, allpass gain at Diffusion 100.
struct DiffuserTap { int delay; float gain; };
// Tail taps: the output side, the delay (samples at 44.1 kHz and Size 4 m), the lane, and whether
// the tap reads the lattice ring (else the raw kernel output).
struct TailTap { int output, delay, lane, lattice; };
// A section's inputs: which side's feed and which of its taps (side -1: none). Inputs 0 and 1 are
// injected through the cross one-pole, 2 and 3 added straight into the ring.
struct TailInput { int side, tap; };
// Sections: the loop length and the lattice span (samples at 44.1 kHz and Size 4 m), the damping
// threshold, the inputs and the taps.
struct SectionTable
{
    int maxDelay, latticeDelay, threshold, tapCount;
    TailInput in[4];
    TailTap taps[SectionReverbEngine::kMaxSectionTaps];
};
// A reflection or echo: the line it reads, the output it feeds, the input its feedback returns to.
struct VoiceRoute { int line, output, feedback; };

struct ModelTable
{
    float rtTableSizeMeters;          // the Size the reverb-time table is read at (the model's default)
    bool roomEarly;                   // the early pattern stage, else the long line
    int diffuserCount;
    DiffuserTap diffusers[2][SectionReverbEngine::kMaxDiffusers];
    bool lastSpreadTapToOutput;
    int reflectionCount;
    VoiceRoute reflections[4];
    int echoCount;
    VoiceRoute echoes[4];
    float tapGainDb, latticeGain;
    bool correctLattice;
    int sectionCount;
    SectionTable sections[SectionReverbEngine::kMaxSections];
};

constexpr TailInput kNoInput { -1, 0 };

constexpr ModelTable kModels[kSectionReverbModels] = {
    // Room
    { 58.0f, true, 2, { { { 45, -0.641f }, { 74, -0.5625f } }, { { 39, -0.641f }, { 61, -0.5625f } } },
      false, 0, {}, 0, {}, 5.0f, 0.6f, true, 2,
      { { 811, 214, 1, 7, { { 0, 0 }, kNoInput, kNoInput, kNoInput },
          { { 0, 27, 0, 0 }, { 1, 200, 3, 0 }, { 0, 264, 2, 0 }, { 1, 435, 1, 1 }, { 0, 534, 0, 1 }, { 1, 596, 2, 1 }, { 0, 725, 3, 1 } } },
        { 924, 331, 2, 7, { { 1, 0 }, kNoInput, kNoInput, kNoInput },
          { { 1, 33, 1, 0 }, { 0, 175, 2, 0 }, { 1, 307, 3, 0 }, { 0, 485, 0, 1 }, { 1, 623, 0, 1 }, { 0, 680, 1, 1 }, { 1, 906, 2, 1 } } } } },
    // Chamber
    { 24.0f, false, 2, { { { 38, 0.707f }, { 89, 0.707f } }, { { 45, 0.707f }, { 74, 0.707f } } },
      true, 2, { { 0, 0, 0 }, { 1, 1, 0 } }, 2, { { 0, 0, 0 }, { 1, 1, 1 } }, 14.0f, 0.55f, true, 6,
      { { 850, 233, 0, 9, { { 1, 3 }, kNoInput, { 0, 0 }, kNoInput },
          { { 0, 24, 0, 0 }, { 1, 133, 1, 0 }, { 0, 240, 2, 0 }, { 1, 304, 3, 0 }, { 0, 412, 1, 1 }, { 1, 520, 2, 1 }, { 0, 647, 3, 1 }, { 1, 695, 0, 1 }, { 0, 819, 0, 1 } } },
        { 803, 154, 1, 9, { kNoInput, kNoInput, { 1, 2 }, kNoInput },
          { { 1, 36, 0, 0 }, { 0, 112, 3, 0 }, { 1, 197, 2, 0 }, { 0, 293, 1, 0 }, { 1, 374, 3, 1 }, { 0, 509, 2, 1 }, { 1, 562, 1, 1 }, { 0, 661, 0, 1 }, { 1, 762, 0, 1 } } },
        { 728, 105, 2, 9, { kNoInput, kNoInput, { 1, 1 }, kNoInput },
          { { 0, 46, 2, 0 }, { 1, 102, 1, 0 }, { 0, 201, 0, 0 }, { 1, 283, 3, 0 }, { 0, 365, 3, 1 }, { 1, 464, 2, 1 }, { 0, 496, 1, 1 }, { 1, 593, 0, 1 }, { 0, 712, 2, 1 } } },
        { 944, 371, 0, 9, { { 0, 3 }, kNoInput, { 1, 0 }, kNoInput },
          { { 1, 38, 3, 0 }, { 0, 194, 0, 0 }, { 1, 246, 1, 0 }, { 0, 351, 2, 0 }, { 1, 452, 0, 1 }, { 0, 555, 1, 1 }, { 1, 656, 2, 1 }, { 0, 813, 3, 1 }, { 1, 888, 2, 1 } } },
        { 881, 135, 1, 9, { kNoInput, kNoInput, { 0, 2 }, kNoInput },
          { { 0, 83, 1, 0 }, { 1, 163, 2, 0 }, { 0, 258, 3, 0 }, { 1, 313, 0, 0 }, { 0, 474, 2, 1 }, { 1, 572, 1, 1 }, { 0, 670, 0, 1 }, { 1, 725, 3, 1 }, { 0, 843, 1, 1 } } },
        { 911, 119, 2, 9, { kNoInput, kNoInput, { 0, 1 }, kNoInput },
          { { 1, 87, 0, 0 }, { 0, 150, 3, 0 }, { 1, 236, 2, 0 }, { 0, 376, 1, 0 }, { 1, 428, 3, 1 }, { 0, 538, 2, 1 }, { 1, 650, 1, 1 }, { 0, 786, 0, 1 }, { 1, 845, 2, 1 } } } } },
    // Vintage Plate
    { 16.0f, false, 3, { { { 8, 0.75f }, { 27, 0.656f }, { 46, 0.656f } }, { { 13, 0.75f }, { 23, 0.656f }, { 35, 0.656f } } },
      false, 2, { { 0, 0, 0 }, { 1, 1, 0 } }, 4, { { 0, 0, 0 }, { 1, 1, 1 }, { 0, 0, 0 }, { 1, 1, 1 } }, 10.9f, 0.5f, false, 6,
      {
        { 728, 228, 0, 7, { kNoInput, kNoInput, { 1, 3 }, { 1, 1 } },
          { { 0, 26, 0, 1 }, { 1, 134, 1, 1 }, { 0, 288, 2, 1 }, { 1, 376, 3, 1 }, { 0, 472, 3, 1 }, { 1, 539, 0, 1 }, { 0, 658, 1, 1 } } },
        { 972, 117, 1, 7, { kNoInput, kNoInput, { 1, 2 }, { 0, 4 } },
          { { 1, 46, 2, 1 }, { 0, 240, 1, 1 }, { 1, 300, 0, 1 }, { 0, 496, 3, 1 }, { 1, 582, 1, 1 }, { 0, 811, 2, 1 }, { 1, 927, 3, 1 } } },
        { 850, 428, 2, 7, { kNoInput, kNoInput, { 0, 0 }, kNoInput },
          { { 0, 97, 2, 1 }, { 1, 163, 3, 1 }, { 0, 278, 0, 1 }, { 1, 434, 1, 1 }, { 0, 524, 1, 1 }, { 1, 647, 0, 1 }, { 0, 747, 3, 1 } } },
        { 850, 226, 0, 7, { kNoInput, kNoInput, { 0, 3 }, { 0, 1 } },
          { { 1, 62, 0, 1 }, { 0, 213, 3, 1 }, { 1, 296, 2, 1 }, { 0, 379, 1, 1 }, { 1, 530, 3, 1 }, { 0, 704, 0, 1 }, { 1, 802, 1, 1 } } },
        { 850, 129, 1, 7, { kNoInput, kNoInput, { 0, 2 }, { 1, 4 } },
          { { 0, 70, 1, 1 }, { 1, 196, 3, 1 }, { 0, 318, 3, 1 }, { 1, 404, 1, 1 }, { 0, 579, 0, 1 }, { 1, 674, 2, 1 }, { 0, 800, 2, 1 } } },
        { 850, 285, 2, 7, { kNoInput, kNoInput, { 1, 0 }, kNoInput },
          { { 1, 34, 2, 1 }, { 0, 154, 3, 1 }, { 1, 346, 0, 1 }, { 0, 438, 1, 1 }, { 1, 590, 1, 1 }, { 0, 626, 3, 1 }, { 1, 788, 3, 1 } } } } },
    // Hall
    { 32.0f, false, 2, { { { 38, 0.75f }, { 89, 0.656f } }, { { 45, 0.75f }, { 74, 0.656f } } },
      true, 4, { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } }, 0, {}, 14.75f, 0.55f, true, 6,
      { { 817, 153, 0, 7, { { 1, 3 }, kNoInput, { 0, 0 }, kNoInput },
          { { 0, 28, 0, 0 }, { 1, 150, 1, 0 }, { 0, 304, 2, 0 }, { 1, 413, 3, 1 }, { 0, 484, 1, 1 }, { 1, 604, 2, 1 }, { 0, 737, 3, 1 } } },
        { 747, 89, 1, 7, { { 1, 2 }, kNoInput, kNoInput, kNoInput },
          { { 1, 35, 3, 0 }, { 0, 180, 2, 0 }, { 1, 290, 1, 0 }, { 0, 381, 0, 1 }, { 1, 463, 0, 1 }, { 0, 618, 3, 1 }, { 1, 690, 2, 1 } } },
        { 944, 249, 2, 7, { { 1, 1 }, kNoInput, kNoInput, kNoInput },
          { { 0, 39, 1, 0 }, { 1, 170, 2, 0 }, { 0, 328, 3, 0 }, { 1, 474, 0, 1 }, { 0, 556, 2, 1 }, { 1, 739, 0, 1 }, { 0, 887, 0, 1 } } },
        { 803, 141, 0, 7, { { 0, 3 }, kNoInput, { 1, 0 }, kNoInput },
          { { 1, 72, 0, 0 }, { 0, 132, 3, 0 }, { 1, 258, 2, 0 }, { 0, 417, 1, 1 }, { 1, 550, 3, 1 }, { 0, 642, 1, 1 }, { 1, 778, 1, 1 } } },
        { 728, 81, 1, 7, { { 0, 2 }, kNoInput, kNoInput, kNoInput },
          { { 0, 33, 2, 0 }, { 1, 146, 1, 0 }, { 0, 226, 0, 0 }, { 1, 358, 3, 1 }, { 0, 501, 1, 1 }, { 1, 553, 1, 1 }, { 0, 670, 3, 1 } } },
        { 911, 283, 2, 7, { { 0, 1 }, kNoInput, kNoInput, kNoInput },
          { { 1, 95, 3, 0 }, { 0, 218, 0, 0 }, { 1, 372, 1, 0 }, { 0, 471, 2, 1 }, { 1, 573, 0, 1 }, { 0, 685, 0, 1 }, { 1, 829, 2, 1 } } } } },
    { 26.0f, false, 3, {
        {{45, -0.75f}, {74, -0.656f}, {7, -0.656f}},
        {{61, -0.75f}, {39, -0.656f}, {10, -0.656f}},
    },
    true,
    2, {{0, 0, 0}, {1, 1, 0}},
    2, {{0, 0, 0}, {1, 1, 1}},
    13.2f, -0.5f, false, 6,
    {
        { 728, 228, 0, 7,
          {{1, 3}, {-1, 0}, {0, 0}, {-1, 0}},
          {{0, 26, 0, 1}, {1, 134, 1, 1}, {0, 288, 2, 1}, {1, 376, 3, 1}, {0, 472, 3, 1}, {1, 539, 0, 1}, {0, 658, 1, 1}} },
        { 972, 117, 1, 7,
          {{1, 2}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{1, 46, 2, 1}, {0, 240, 1, 1}, {1, 300, 0, 1}, {0, 496, 3, 1}, {1, 582, 1, 1}, {0, 811, 2, 1}, {1, 927, 3, 1}} },
        { 850, 428, 2, 7,
          {{1, 1}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{0, 97, 2, 1}, {1, 163, 3, 1}, {0, 278, 0, 1}, {1, 434, 1, 1}, {0, 524, 1, 1}, {1, 647, 0, 1}, {0, 747, 3, 1}} },
        { 850, 226, 0, 7,
          {{0, 3}, {-1, 0}, {1, 0}, {-1, 0}},
          {{1, 62, 0, 1}, {0, 213, 3, 1}, {1, 296, 2, 1}, {0, 379, 1, 1}, {1, 530, 3, 1}, {0, 704, 0, 1}, {1, 802, 1, 1}} },
        { 850, 129, 1, 7,
          {{0, 2}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{0, 70, 1, 1}, {1, 196, 3, 1}, {0, 318, 3, 1}, {1, 404, 1, 1}, {0, 579, 0, 1}, {1, 674, 2, 1}, {0, 800, 2, 1}} },
        { 850, 285, 2, 7,
          {{0, 1}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{1, 34, 2, 1}, {0, 154, 3, 1}, {1, 346, 0, 1}, {0, 438, 1, 1}, {1, 590, 1, 1}, {0, 626, 3, 1}, {1, 788, 3, 1}} },
    } },
    { 40.0f, false, 2, {
        {{38, 0.75f}, {89, 0.656f}},
        {{45, 0.75f}, {74, 0.656f}},
    },
    true,
    4, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
    0, {},
    13.0f, 0.55f, false, 6,
    {
        { 817, 153, 0, 7,
          {{1, 3}, {-1, 0}, {0, 0}, {-1, 0}},
          {{0, 28, 0, 1}, {1, 150, 1, 1}, {0, 304, 2, 1}, {1, 413, 3, 1}, {0, 484, 1, 1}, {1, 604, 2, 1}, {0, 737, 3, 1}} },
        { 747, 89, 1, 7,
          {{1, 2}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{1, 35, 3, 1}, {0, 180, 2, 1}, {1, 290, 1, 1}, {0, 381, 0, 1}, {1, 463, 0, 1}, {0, 618, 3, 1}, {1, 690, 2, 1}} },
        { 944, 249, 2, 7,
          {{1, 1}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{0, 39, 1, 1}, {1, 170, 2, 1}, {0, 328, 3, 1}, {1, 474, 0, 1}, {0, 556, 2, 1}, {1, 739, 0, 1}, {0, 887, 0, 1}} },
        { 803, 141, 0, 7,
          {{0, 3}, {-1, 0}, {1, 0}, {-1, 0}},
          {{1, 72, 0, 1}, {0, 132, 3, 1}, {1, 258, 2, 1}, {0, 417, 1, 1}, {1, 550, 3, 1}, {0, 642, 1, 1}, {1, 778, 1, 1}} },
        { 728, 81, 1, 7,
          {{0, 2}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{0, 33, 2, 1}, {1, 146, 1, 1}, {0, 226, 0, 1}, {1, 358, 3, 1}, {0, 501, 1, 1}, {1, 553, 1, 1}, {0, 670, 3, 1}} },
        { 911, 283, 2, 7,
          {{0, 1}, {-1, 0}, {-1, 0}, {-1, 0}},
          {{1, 95, 3, 1}, {0, 218, 0, 1}, {1, 372, 1, 1}, {0, 471, 2, 1}, {1, 573, 0, 1}, {0, 685, 0, 1}, {1, 829, 2, 1}} },
    } },
    { 38.0f, false, 2, { { {72, 0.75f}, {116, 0.65625f} }, { {84, 0.75f}, {139, 0.65625f} } },
      true, 2, {{0, 0, 0}, {1, 1, 0}}, 0, {}, -1.5f, 0, false, 0, {} },
};

const ModelTable& modelTable (int model) noexcept
{
    return kModels[model >= 0 && model < kSectionReverbModels ? model : 0];
}

// The chamber's spread taps per side: their offsets (samples at 44.1 kHz) at Spread 100.
constexpr int kSpreadOffsets[2][SectionReverbEngine::kSpreadTaps] = {
    { 0, 2390, 8154, 16444, 0 },
    { 878, 4497, 13488, 20736, 0 },
};

constexpr float kGroupLevel = 0.7071068f;      // the early groups at rest
constexpr float kFeedLevel = 0.574456265f;     // the early output into the tail
constexpr float kHalfPower = 0x1.6a09e6p-1f;   // the tail lanes at rest
constexpr float kRow[4][4] = {
    { 0.0f, kHalfPower, 1.0f, kHalfPower },
    { kHalfPower, 1.0f, kHalfPower, 0.0f },
    { 1.0f, kHalfPower, 0.0f, kHalfPower },
    { kHalfPower, 0.0f, kHalfPower, 1.0f },
};
constexpr int kGroupOrder[4] = { 0, 3, 2, 1 };
constexpr float kPhaseDeg[4] = { 0.0f, 45.0f, 90.0f, 135.0f };
constexpr float kZeroBlock[SectionReverbEngine::kBlock] {};

// The reverb time each loop-gain step gives, over the pass time.
const std::array<float, 401> kRtDisplayFactors = [] {
    std::array<float, 401> factors {};
    factors[0] = 1.0f;
    float gain = 0.0f;
    for (unsigned step = 1; step < factors.size(); ++step)
    {
        gain += 0.99f / 400.0f;
        factors[step] = 1.0f - 3.0f / std::log10 (gain);
    }
    return factors;
}();

float tablePass (int model) noexcept
{
    return 1.28f * (modelTable (model).rtTableSizeMeters / 335.28f);
}

// ---- DSP helpers ----

float nestedMaxGain(float size) noexcept
{
    constexpr float sizes[] {2, 3, 4, 8, 12, 16, 20, 24, 28, 32};
    constexpr float gains[] {.83f, .88f, .935f, .962f, .975f, .9817f, .985f, .9871f, .989f, .99f};
    for (unsigned i = 1; i < std::size(sizes); ++i)
        if (size <= sizes[i])
            return gains[i-1] + (gains[i] - gains[i-1]) * (size - sizes[i-1]) / (sizes[i] - sizes[i-1]);
    return gains[std::size(gains)-1];
}

float onePoleCoeff (float hz, const Rate& rate) noexcept
{
    if (hz >= rate.nyquist)
        return 1.0f;
    return 1.0f - std::exp ((-6.2831855f * hz) / rate.sr);
}

float dbToAmp (float db) noexcept
{
    return std::pow (10.0f, db / 20.0f);
}

float shelfBlend (float db) noexcept
{
    if (db <= -96.0f)
        return 0.0f;
    return std::pow (10.0f, db * 0.5f / 20.0f);
}

unsigned ringLength (float size) noexcept
{
    return (static_cast<unsigned> (size) + 31u) & ~31u;
}

unsigned readOffset (unsigned write, unsigned delay, unsigned length) noexcept
{
    return write >= delay ? write - delay : write + length - delay;
}

// The lattice gains' correction for the loop gain (both gains move by the same amount; the
// exponent is the lattice span's share of the section).
float latticeCorrection (const ModelTable& model, const SectionTable& section, float rtGain) noexcept
{
    if (! model.correctLattice || rtGain == 1.0f)
        return 0.0f;
    const float gain = std::abs (model.latticeGain);
    const double squared = gain * gain;
    const float ratio = static_cast<float> ((1.0 - squared) / ((1.0 + squared) - 2.0 * gain));
    const float delay = static_cast<float> (section.maxDelay);
    const float ap = static_cast<float> (section.latticeDelay);
    const float exponent = ((ratio * ap + (delay - ap)) / delay) - 1.0f;
    const float decay = std::pow (rtGain, exponent);
    const float correction = static_cast<float> ((1.0 - decay) * (1.0 - gain) / (1.0 + decay));
    return std::copysign (correction, model.latticeGain);
}

void computeTapSlope (float db, float masterGain, int taps, float* out) noexcept
{
    static constexpr float kTable[21] = {
        -18.0f, -12.0f, -9.0f, -8.0f, -7.0f, -6.0f, -5.0f, -4.5f, -4.0f, -3.5f, -3.0f,
        -2.5f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.5f, 6.0f, 10.0f, 15.0f };
    int idx = static_cast<int> (db + 10.0f);
    idx = std::min (std::max (idx, 0), 20);
    float r = std::pow (10.0f, kTable[idx] / 20.0f);
    if (taps > 1)
        r = std::pow (r, 1.0f / static_cast<float> (taps - 1));
    float sum = 0.0f;
    float value = 1.0f;
    for (int i = 0; i < taps; ++i)
    {
        sum += value;
        value *= r;
    }
    value = std::pow (10.0f, (std::log10 (sum) * 20.0f) / -20.0f);
    for (int i = 0; i < taps; ++i)
    {
        out[i] = value * masterGain;
        value *= r;
    }
}

// |sin|, from a polynomial sine and cosine per quadrant.
float modulationMagnitude (float radians) noexcept
{
    const double x = std::fabs (static_cast<double> (radians));
    constexpr double halfPi = 0x1.921fb54442d18p+0;
    const auto sine = [] (double a) {
        const double z = a * a;
        if (std::fabs (a) < 0x1.002aa4p-7)
            return a * (1.0 + z * (-1.0 / 6.0));
        const double v = z * a;
        const double w = z * z;
        return (a + v * (z * (1.0 / 120.0) - 1.0 / 6.0)) + (v * w) * (z * (1.0 / 362880.0) - 1.0 / 5040.0);
    };
    const auto cosine = [] (double a) {
        const double z = a * a;
        if (std::fabs (a) < 0x1.002p-7)
            return 1.0 - 0.5 * z;
        return 1.0 + z * (-0.5 + z * (1.0 / 24.0 + z * (-1.0 / 720.0 + z * (1.0 / 40320.0 - z * (1.0 / 3628800.0)))));
    };
    if (x <= 0x1.921fb4p-1f)
        return static_cast<float> (sine (x));
    if (x <= 0x1.2d97c6p+1f)
        return static_cast<float> (cosine (x - halfPi));
    if (x <= 0x1.f6a7a2p+1f)
        return static_cast<float> (std::fabs (sine (x - 2 * halfPi)));
    if (x <= 0x1.5fdbbep+2f)
        return static_cast<float> (cosine (x - 3 * halfPi));
    if (x <= 0x1.c463aap+2f)
        return static_cast<float> (std::fabs (sine (x - 4 * halfPi)));
    return static_cast<float> (std::fabs (std::sin (x)));
}

struct BiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };

BiquadCoeffs designBiquad (int type, float hz, float bandwidthOctaves, const Rate& rate, float maxBwCap) noexcept
{
    BiquadCoeffs c;
    if (hz < 1.0f)
        hz = 1.0f;
    if (hz > rate.nyquist)
        hz = rate.nyquist;
    const float w0 = 6.2831855f * hz * rate.invSr;
    const float sinw = std::sin (w0);
    const float cosw = std::cos (w0);
    if (sinw <= 0.0f)
        return c;
    const float maxBw = (maxBwCap > 0.0f) ? maxBwCap : rate.nyquist * 0.5f / hz;
    const float requested = (type == 2 || type == 3) ? 3.0f : bandwidthOctaves;
    const float bw = std::min (requested, maxBw);
    const float alpha = sinw * std::sinh (0.3465736f * bw * w0 / sinw);
    const float inv = 1.0f / (1.0f + alpha);
    switch (type)
    {
        case 3:   // high-pass
            c.b0 = (1.0f + cosw) * 0.5f * inv;
            c.b1 = -(1.0f + cosw) * inv;
            c.b2 = c.b0;
            break;
        case 4:   // band-pass
            c.b0 = alpha * inv;
            c.b1 = 0.0f;
            c.b2 = -alpha * inv;
            break;
        case 5:   // notch
            c.b0 = inv;
            c.b1 = -2.0f * cosw * inv;
            c.b2 = inv;
            break;
        default:  // low-pass
            c.b0 = (1.0f - cosw) * 0.5f * inv;
            c.b1 = (1.0f - cosw) * inv;
            c.b2 = c.b0;
            break;
    }
    c.a1 = -2.0f * cosw * inv;
    c.a2 = (1.0f - alpha) * inv;
    return c;
}

} // namespace

namespace sectionreverb
{

Rate Rate::make (double sampleRate) noexcept
{
    Rate s;
    s.sr = static_cast<float> (sampleRate);
    s.invSr = 1.0f / s.sr;
    s.srMs = s.sr / 1000.0f;
    s.srScale = s.sr / 44100.0f;
    s.lenScale = std::max (1.0f, s.srScale);
    s.sr32 = s.sr * 0.03125f;
    s.nyquist = s.sr * 0.5f;
    return s;
}

void Ramp::reset (float value) noexcept
{
    target_ = current_ = value;
    step_ = 0.0f;
    blocksLeft_ = 0;
}

void Ramp::setTarget (float target, float msPerUnit, const Rate& rate) noexcept
{
    const float delta = target - current_;
    const float samples = std::fabs (delta) * msPerUnit * rate.srMs;
    auto blocks = static_cast<std::uint32_t> (samples * (1.0f / 32.0f));
    if (blocks == 0)
        blocks = 1;
    target_ = target;
    step_ = delta / static_cast<float> (blocks);
    blocksLeft_ = blocks;
}

void Ramp::arm (float target, float msPerUnit, const Rate& rate) noexcept
{
    if (target == current_)
        reset (target);
    else
        setTarget (target, msPerUnit, rate);
}

void Ramp::advanceBlock() noexcept
{
    if (blocksLeft_ == 0)
        return;
    if (--blocksLeft_ == 0)
        current_ = target_;
    else
        current_ += step_;
}

void OutputFilter::SampleRamp::arm (float t, float rateMs, const Rate& rate) noexcept
{
    const float delta = t - current;
    if (delta == 0.0f || rateMs == 0.0f)
    {
        snap (t);
        return;
    }
    int samples = static_cast<int> (std::fabs (delta) * rateMs * rate.srMs + 0.5f);
    samples = (samples + 31) & ~31;
    if (samples == 0)
        samples = 32;
    target = t;
    step = delta / static_cast<float> (samples);
    samplesLeft = samples;
    pending = true;
}

void OutputFilter::SampleRamp::fillBlock (float* out) noexcept
{
    if (samplesLeft == 0)
    {
        if (pending)
        {
            for (int i = 0; i < 32; ++i)
                out[i] = current;
            pending = false;
        }
        return;
    }
    float v = current;
    for (int i = 0; i < 32; ++i)
    {
        v += step;
        out[i] = v;
    }
    samplesLeft -= 32;
    if (samplesLeft <= 0)
    {
        current = target;
        step = 0.0f;
        samplesLeft = 0;
        pending = true;
    }
    else
    {
        current = v;
    }
}

void OutputFilter::SampleRamp::fillExact (float* out) const noexcept
{
    for (int i = 0; i < 32; ++i)
        out[i] = current;
}

void OutputFilter::setRate (const Rate& rate) noexcept
{
    rate_ = rate;
    reset();
}

void OutputFilter::set (const SectionReverbFilter& settings) noexcept
{
    const int t = (settings.type >= 0 && settings.type <= 5) ? settings.type : 0;
    if (t != type_)
    {
        type_ = t;
        typePending_ = true;
    }
    freqTarget_ = std::min (std::max (settings.hz, 1.0f), 100000.0f);
    maxBw_ = 0.5f * rate_.nyquist / freqTarget_;
    bwTarget_ = settings.octaves;
    shelfDb_ = settings.shelfDb;
}

void OutputFilter::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
        lp_[ch] = x1_[ch] = x2_[ch] = y1_[ch] = y2_[ch] = 0.0f;
    gainRamp_.reset (1.0f);
    typeState_ = 0;
    activeType_ = type_;
    freqCurrent_ = freqTarget_;
    bwCurrent_ = bwTarget_;
    primed_ = false;
    typePending_ = false;
    typeGain_ = 1.0f;
    maxBw_ = 0.5f * rate_.nyquist / std::max (freqTarget_, 1.0f);
    const BiquadCoeffs c = designBiquad (activeType_, freqCurrent_, bwCurrent_, rate_, maxBw_);
    biquad_[0].snap (c.b0);
    biquad_[1].snap (c.b1);
    biquad_[2].snap (c.b2);
    biquad_[3].snap (c.a1);
    biquad_[4].snap (c.a2);
    onePoleRamp_.reset (onePoleCoeff (freqCurrent_, rate_));
    onePole_ = onePoleRamp_.current();
    shelfRamp_.reset (shelfBlend (shelfDb_));
    shelf_ = shelfRamp_.current();
    for (int k = 0; k < 5; ++k)
        for (int i = 0; i < 32; ++i)
            coefBlock_[k][i] = biquad_[k].current;
}

void OutputFilter::armCoeffs (bool immediate, bool refill) noexcept
{
    const BiquadCoeffs c = designBiquad (activeType_, freqCurrent_, bwCurrent_, rate_, maxBw_);
    const float v[5] = { c.b0, c.b1, c.b2, c.a1, c.a2 };
    for (int k = 0; k < 5; ++k)
    {
        if (immediate)
            biquad_[k].snap (v[k]);
        else
            biquad_[k].arm (v[k], 10.0f, rate_);   // biquad coefficients: 10 ms
    }
    const float a = onePoleCoeff (freqCurrent_, rate_);
    if (immediate)
        onePoleRamp_.reset (a);
    else
        onePoleRamp_.arm (a, 200.0f, rate_);       // one-pole coefficient: 200 ms
    if (refill)
        for (int k = 0; k < 5; ++k)
            biquad_[k].fillExact (coefBlock_[k]);
}

void OutputFilter::beginBlock() noexcept
{
    for (int k = 0; k < 5; ++k)
        biquad_[k].fillBlock (coefBlock_[k]);

    if (! primed_)
    {
        // The first block after a reset snaps every coefficient to the current settings.
        activeType_ = type_;
        freqCurrent_ = freqTarget_;
        bwCurrent_ = bwTarget_;
        typePending_ = false;
        armCoeffs (true, true);
        gainRamp_.reset (1.0f);
        shelfRamp_.reset (shelfBlend (shelfDb_));
        typeState_ = 0;
        primed_ = true;
    }
    else
    {
        const float shelfTarget = shelfBlend (shelfDb_);
        if (shelfTarget != shelfRamp_.target())
            shelfRamp_.arm (shelfTarget, 200.0f, rate_);

        // A type change fades out (300 ms), switches, and fades back in.
        switch (typeState_)
        {
            case 0:
                if (typePending_)
                {
                    typePending_ = false;
                    if (type_ != activeType_)
                    {
                        gainRamp_.arm (0.0f, 300.0f, rate_);
                        typeState_ = 1;
                    }
                }
                else
                {
                    bool settled = true;
                    for (int k = 0; k < 5; ++k)
                        settled = settled && biquad_[k].settled();
                    if ((freqTarget_ != freqCurrent_ || bwTarget_ != bwCurrent_) && settled)
                    {
                        freqCurrent_ = freqTarget_;
                        bwCurrent_ = bwTarget_;
                        armCoeffs (false, false);
                    }
                }
                break;
            case 1:
                if (! gainRamp_.ramping())
                    typeState_ = 2;
                else
                    gainRamp_.advanceBlock();
                break;
            case 2:
                typeState_ = 3;
                break;
            case 3:
                freqCurrent_ = freqTarget_;
                bwCurrent_ = bwTarget_;
                activeType_ = type_;
                armCoeffs (true, true);
                gainRamp_.arm (1.0f, 300.0f, rate_);
                typeState_ = 4;
                break;
            case 4:
                if (! gainRamp_.ramping())
                    typeState_ = 0;
                else
                    gainRamp_.advanceBlock();
                break;
            default:
                typeState_ = 0;
                break;
        }
    }

    if (activeType_ <= 1)
        onePoleRamp_.advanceBlock();
    onePole_ = onePoleRamp_.current();
    shelf_ = shelfRamp_.current();
    shelfRamp_.advanceBlock();
    typeGain_ = gainRamp_.current();
}

void OutputFilter::process (float* buffer, int channel) noexcept
{
    const float dry = shelf_;
    const float wet = 1.0f - shelf_;
    const float gain = typeGain_;
    if (activeType_ <= 1)
    {
        const float a = onePole_;
        const float b = 1.0f - a;
        float y = lp_[channel];
        if (activeType_ == 0)
        {
            for (int i = 0; i < 32; ++i)
            {
                const float x = gain * buffer[i];
                y = a * x + b * y;
                buffer[i] = wet * y + dry * x;
            }
        }
        else
        {
            for (int i = 0; i < 32; ++i)
            {
                const float x = gain * buffer[i];
                y = a * x + b * y;
                buffer[i] = wet * (x - y) + dry * x;
            }
        }
        lp_[channel] = y;
        return;
    }
    float x1 = x1_[channel], x2 = x2_[channel], y1 = y1_[channel], y2 = y2_[channel];
    for (int i = 0; i < 32; ++i)
    {
        const float x = buffer[i];
        const float y = gain * (coefBlock_[0][i] * x + coefBlock_[1][i] * x1 + coefBlock_[2][i] * x2
                                - coefBlock_[3][i] * y1 - coefBlock_[4][i] * y2);
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        buffer[i] = wet * y + dry * x;
    }
    x1_[channel] = x1;
    x2_[channel] = x2;
    y1_[channel] = y1;
    y2_[channel] = y2;
}

void LineVoice::sync (bool snap) noexcept
{
    if (snap)
    {
        retime = fading = levelDirty = feedbackDirty = false;
        appliedDelay = delay;
        direct.reset (gain);
        feedback.reset (fb);
    }
    else
    {
        retime |= delay != requestedDelay;
        levelDirty |= gain != previousGain;
        feedbackDirty |= fb != previousFeedback;
    }
    requestedDelay = delay;
    previousGain = gain;
    previousFeedback = fb;
}

// The read head advances before a deferred retime is serviced; the new read is relative to this
// block's start.
void LineVoice::endBlock (const Rate& rate, std::uint32_t length) noexcept
{
    direct.advanceBlock();
    feedback.advanceBlock();
    if (fading)
    {
        if (direct.ramping() || feedback.ramping())
            return;
        appliedDelay = (delay + 32u) % length;
        fading = retime = false;
        levelDirty = feedbackDirty = true;
    }
    else if (retime)
    {
        direct.arm (0.0f, 25.0f, rate);
        feedback.arm (0.0f, 25.0f, rate);
        fading = true;
        return;
    }
    if (levelDirty)
        direct.arm (gain, 25.0f, rate);
    if (feedbackDirty)
        feedback.arm (fb, 25.0f, rate);
    levelDirty = feedbackDirty = false;
}

} // namespace sectionreverb

// ---- Engine ----

float SectionReverbEngine::rtSecondsForStep (int model, int step) noexcept
{
    return 0.1f + tablePass (model) * kRtDisplayFactors[static_cast<std::size_t> (std::clamp (step, 0, 400))];
}

float SectionReverbEngine::rtGain (int model, float seconds) noexcept
{
    const float factor = (seconds - 0.1f) / tablePass (model);
    const auto begin = kRtDisplayFactors.begin();
    const auto end = kRtDisplayFactors.end();
    auto closest = std::lower_bound (begin, end, factor);
    if (closest == end)
        --closest;
    else if (closest != begin && factor - closest[-1] < *closest - factor)
        --closest;
    return 0.99f * (static_cast<float> (closest - begin) / 400.0f);
}

SectionReverbConfig SectionReverbEngine::sanitized (const SectionReverbConfig& in) noexcept
{
    const auto finite = [] (float v, float fallback) { return std::isfinite (v) ? v : fallback; };
    const SectionReverbConfig d {};
    SectionReverbConfig c = in;
    c.model = std::clamp (c.model, 0, kSectionReverbModels - 1);
    c.diffusion = std::clamp (finite (c.diffusion, d.diffusion), 0.0f, 100.0f);
    c.sizeMeters = std::clamp (finite (c.sizeMeters, d.sizeMeters), 2.0f, 80.0f);
    c.reverbTime = std::clamp (finite (c.reverbTime, d.reverbTime), rtSecondsForStep (c.model, 0), rtSecondsForStep (c.model, 400));
    c.bassRt = std::clamp (finite (c.bassRt, d.bassRt), 0.2f, 4.0f);
    c.bassXoverHz = std::clamp (finite (c.bassXoverHz, d.bassXoverHz), 20.0f, 20000.0f);
    c.rtHiCutHz = std::clamp (finite (c.rtHiCutHz, d.rtHiCutHz), 40.0f, 20000.0f);
    c.damping = std::clamp (c.damping, 0, 2);
    c.tapSlope = std::clamp (c.tapSlope, -10, 10);
    c.spinHz = std::round (std::clamp (finite (c.spinHz, d.spinHz), 0.0f, 4.0f) * 10.0f) / 10.0f;
    c.wanderMs = std::round (std::clamp (finite (c.wanderMs, d.wanderMs), 0.0f, 24.0f) * 10.0f) / 10.0f;
    c.tailWidthDeg = std::clamp (finite (c.tailWidthDeg, 0.0f), 0.0f, 360.0f);
    c.earlyLevelDb = std::clamp (finite (c.earlyLevelDb, d.earlyLevelDb), -96.0f, 12.0f);
    c.reverbLevelDb = std::clamp (finite (c.reverbLevelDb, d.reverbLevelDb), -96.0f, 12.0f);
    for (SectionReverbFilter* f : { &c.earlyFilter, &c.reverbFilter })
    {
        f->type = std::clamp (f->type, 0, 5);
        f->hz = std::clamp (finite (f->hz, 20000.0f), 20.0f, 20000.0f);
        f->octaves = std::clamp (finite (f->octaves, 1.0f), 0.1f, 4.0f);
        f->shelfDb = std::clamp (finite (f->shelfDb, -96.0f), -96.0f, 12.0f);
    }

    c.definition = std::clamp (finite (c.definition, d.definition), 0.0f, 100.0f);
    c.chorusDepth = std::clamp (finite (c.chorusDepth, d.chorusDepth), 0.0f, 100.0f);
    c.chorusRate = std::round (std::clamp (finite (c.chorusRate, d.chorusRate), 0.0f, 10.0f));
    c.chorusType = std::clamp (c.chorusType, 0, 2);
    c.pattern = std::clamp (c.pattern, 0, kSectionPatternCount - 1);
    c.earlyPredelayMs = std::clamp (finite (c.earlyPredelayMs, 0.0f), 0.0f, 1000.0f);
    c.reverbPredelayMs = std::clamp (finite (c.reverbPredelayMs, 0.0f), 0.0f, 1000.0f);
    {
        const float scale = std::clamp (finite (c.roomScale, d.roomScale), 0.5f, 2.0f);
        // Round to float at each scale step in fast-math builds.
        const int step = static_cast<int> (std::round ((scale - 0.5f) * 10.0f));
        const float normalized = static_cast<float> (static_cast<double> (step) / 15.0);
        c.roomScale = static_cast<float> (0.5 + 1.5 * double (normalized));
    }
    c.roomBassXoverHz = std::clamp (finite (c.roomBassXoverHz, d.roomBassXoverHz), 40.0f, 20000.0f);
    c.roomBassBoostDb = std::clamp (finite (c.roomBassBoostDb, d.roomBassBoostDb), -12.0f, 12.0f);
    c.reverse = c.reverse != 0 ? 1 : 0;

    c.predelayMs = std::clamp (finite (c.predelayMs, d.predelayMs), 0.0f, 1000.0f);
    c.shape = std::clamp (finite (c.shape, d.shape), 0.0f, 64.0f);
    c.spread = std::clamp (finite (c.spread, d.spread), 0.0f, 100.0f);
    c.reflectionTimePercent = std::clamp (finite (c.reflectionTimePercent, 100.0f), 0.0f, 200.0f);
    c.echoTimePercent = std::clamp (finite (c.echoTimePercent, 100.0f), 0.0f, 200.0f);
    c.echoFeedbackPercent = std::clamp (finite (c.echoFeedbackPercent, 100.0f), 0.0f, 200.0f);
    for (auto& r : c.reflections)
    {
        r.delayMs = std::clamp (finite (r.delayMs, 0.0f), 0.0f, 2000.0f);
        r.inverted = r.inverted != 0 ? 1 : 0;
        r.gainDb = std::clamp (finite (r.gainDb, -96.0f), -96.0f, 12.0f);
    }
    for (auto& e : c.echoes)
    {
        e.delayMs = std::clamp (finite (e.delayMs, 0.0f), 0.0f, 2000.0f);
        e.gainInverted = e.gainInverted != 0 ? 1 : 0;
        e.gainDb = std::clamp (finite (e.gainDb, -96.0f), -96.0f, 12.0f);
        e.feedbackInverted = e.feedbackInverted != 0 ? 1 : 0;
        e.feedbackDb = std::clamp (finite (e.feedbackDb, -96.0f), -96.0f, 12.0f);
    }
    return c;
}

void SectionReverbEngine::prepare (double sampleRate)
{
    rate_ = Rate::make (sampleRate > 0.0 ? sampleRate : 48000.0);
    // Every buffer is sized for the largest model; reset() sets each one's length to its model's.
    for (int ch = 0; ch < 2; ++ch)
        for (int t = 0; t < kMaxDiffusers; ++t)
        {
            std::uint32_t capacity = 0;
            for (const ModelTable& m : kModels)
                if (t < m.diffuserCount)
                {
                    const auto raw = static_cast<std::uint32_t> (static_cast<float> (m.diffusers[ch][t].delay) * 20.0f * rate_.lenScale) + 0x40u;
                    capacity = std::max (capacity, ((raw + 31u) & ~31u) - 32u);
                }
            stage1_[ch][t].buffer.assign (std::max (capacity, 1u), 0.0f);
        }
    for (int s = 0; s < kMaxSections; ++s)
    {
        std::uint32_t capacity = 0;
        for (const ModelTable& m : kModels)
            if (s < m.sectionCount)
            {
                // Long enough for the largest Size (80 m: 20 times the 4 m delays).
                const auto raw = static_cast<std::uint32_t> ((static_cast<float> (m.sections[s].maxDelay) * 20.0f + 1080.0f) * rate_.lenScale + 64.0f);
                capacity = std::max (capacity, (raw + 31u) & ~31u);
            }
        Section& x = sections_[static_cast<std::size_t> (s)];
        x.ring.assign (capacity + 32u, 0.0f);
        x.rawRing.assign (capacity + 32u, 0.0f);
    }
    inputLength_ = ringLength (132300.0f * rate_.lenScale);
    feedLength_ = ringLength (90000.0f * rate_.lenScale) + 32;
    for (auto& line : inputLine_)
        line.assign (inputLength_ + 32, 0.0f);
    for (auto& line : feedLine_)
        line.assign (feedLength_ + 32, 0.0f);
    const std::uint32_t lineAlloc = (static_cast<std::uint32_t> (90000.0f * rate_.lenScale + 64.0f) + 31u) & ~31u;
    lineLength_ = lineAlloc - 32u;
    for (auto& line : lines_)
        line.assign (lineAlloc, 0.0f);
    auditorium_.prepare(rate_);
    earlyFilter_.setRate (rate_);
    reverbFilter_.setRate (rate_);
    prepared_ = true;
    reset();
}

void SectionReverbEngine::configure (const SectionReverbConfig& config) noexcept
{
    const SectionReverbConfig next = sanitized (config);
    if (! prepared_ || ! ranSinceClear_ || next.model != config_.model)
    {
        // Nothing has played since the last reset, or the model changes: install the values and
        // start from them.
        config_ = next;
        if (prepared_)
            reset();
        return;
    }
    apply (next);
}

void SectionReverbEngine::clear() noexcept
{
    if (prepared_)
        reset();
}

void SectionReverbEngine::reset() noexcept
{
    const ModelTable& model = modelTable (config_.model);
    previousWetPeak_ = 0.0f;
    if (auditoriumModel())
    {
        auditorium_.configure(config_, true);
        auditorium_.reset();
    }
    ranSinceClear_ = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill_n (inBus_[ch], kBlock, 0.0f);
        std::fill_n (outBus_[ch], kBlock, 0.0f);
        std::fill_n (early_[ch], kBlock, -0.0f);
        std::fill_n (echoFeedback_[ch], kBlock, -0.0f);
        for (int t = 0; t < kMaxDiffusers; ++t)
        {
            Allpass& ap = stage1_[ch][t];
            std::fill (ap.buffer.begin(), ap.buffer.end(), 0.0f);
            ap.write = 0;
            ap.length = 1;
            if (t < model.diffuserCount)
            {
                const auto raw = static_cast<std::uint32_t> (static_cast<float> (model.diffusers[ch][t].delay) * 20.0f * rate_.lenScale) + 0x40u;
                ap.length = ((raw + 31u) & ~31u) - 32u;
            }
        }
    }
    setStage1ReadHeads (true);
    stage1SizeBlend_.reset (1.0f);
    stage1SizePending_ = false;
    stage1SizeState_ = SizeChange::Idle;
    diffusion_.reset (config_.diffusion * 0.01f);
    acc_ = 0;

    // Chamber line: the spread taps, their levels and the reflection and echo voices.
    for (auto& line : lines_)
        std::fill (line.begin(), line.end(), 0.0f);
    linePhase_ = 0;
    for (SpreadSide& side : spread_)
    {
        std::fill_n (side.tap, kSpreadTaps, 0u);
        for (auto& bus : side.bus)
            std::fill_n (bus, kBlock, 0.0f);
    }
    spreadState_ = SpreadChange::Idle;
    shapeDirty_ = spreadDirty_ = false;
    setShapeTargets();
    for (unsigned k = 0; k < static_cast<unsigned> (spreadTapCount()); ++k)
    {
        positionSpreadTap (k);
        spreadGain_[k].reset (shapeTarget_[k]);
    }

    // Room early pattern.
    for (auto& line : inputLine_)
        std::fill (line.begin(), line.end(), 0.0f);
    for (auto& line : feedLine_)
        std::fill (line.begin(), line.end(), 0.0f);
    inputPos_ = feedPos_ = activeTaps_ = patternLength_ = 0;
    low_[0] = low_[1] = 0.0f;
    std::fill_n (groupGain_, 4, kGroupLevel);
    sweepStep_ = -1;
    bassPole_.reset (onePoleCoeff (config_.roomBassXoverHz, rate_));
    bassGain_.reset (dbToAmp (config_.roomBassBoostDb) - 1.0f);
    patternInputGain_.reset (0.0f);
    patternState_ = PatternState::Load;
    selectPending_ = tapsDirty_ = true;
    updateFeedDelay();
    std::copy_n (feedDelay_, 2, oldFeedDelay_);
    feedFade_.reset (1.0f);
    feedDirty_ = false;

    resetTail();
    updateLanes (true);
    setTapSlope (static_cast<float> (config_.tapSlope));
    reapplyTapSlope_ = true;
    configureDamping (true, true);
    spinOn_ = config_.spinHz > 0.0f;
    secondary_.rate = config_.spinHz * 0.27f;
    secondary_.rateJitter = 0.25f;
    mod_.rate = config_.spinHz;
    mod_.armed = true;

    earlyRamp_.reset (dbToAmp (config_.earlyLevelDb));
    earlyFilter_.set (config_.earlyFilter);
    reverbFilter_.set (config_.reverbFilter);
    earlyFilter_.reset();
    reverbFilter_.reset();
    mixSRamp_.reset (std::sin (1.5707963f));   // fully wet
    const float tw = config_.tailWidthDeg * 6.2831855f / 360.0f;
    twCRamp_.reset (std::cos (tw));
    twSRamp_.reset (std::sin (tw));
    reverbRamp_.reset (dbToAmp (config_.reverbLevelDb));
    updateVoices (true);
}

void SectionReverbEngine::apply (const SectionReverbConfig& c) noexcept
{
    // Each changed value takes its own path, in this order.
    const SectionReverbConfig old = config_;
    config_ = c;
    if (auditoriumModel()) auditorium_.configure(c, false);
    if (c.diffusion != old.diffusion)
        diffusion_.arm (std::min (std::max (c.diffusion * 0.01f, 0.0f), 1.0f), 200.0f, rate_);
    if (c.predelayMs != old.predelayMs || c.spread != old.spread)
        spreadDirty_ = true;
    if (c.shape != old.shape)
    {
        setShapeTargets();
        shapeDirty_ = true;
    }
    if (c.pattern != old.pattern)
        selectPending_ = true;
    if (c.earlyPredelayMs != old.earlyPredelayMs || c.reverse != old.reverse)
        tapsDirty_ = true;
    if (c.reverbPredelayMs != old.reverbPredelayMs)
        feedDirty_ = true;
    if (c.roomScale != old.roomScale)
        tapsDirty_ = feedDirty_ = true;
    if (c.roomBassXoverHz != old.roomBassXoverHz)
        bassPole_.setTarget (onePoleCoeff (c.roomBassXoverHz, rate_), 200.0f, rate_);
    if (c.roomBassBoostDb != old.roomBassBoostDb)
        bassGain_.setTarget (dbToAmp (c.roomBassBoostDb) - 1.0f, 200.0f, rate_);
    if (c.sizeMeters != old.sizeMeters)
    {
        stage1SizePending_ = true;
        readSizeScale_ = sizeScale() * rate_.srScale;
        requestSize();
        if (nestedModel()) { updateLanes(false); reapplyTapSlope_ = true; }
    }
    if (c.reverbTime != old.reverbTime)
    {
        // Reverb Time first, at the old BassRT; a BassRT change follows as its own step.
        config_.bassRt = old.bassRt;
        const float previousGain = rtG_;
        updateLanes (false);
        if (rtG_ != previousGain)
            reapplyTapSlope_ = true;
        config_.bassRt = c.bassRt;
    }
    if (c.bassRt != old.bassRt)
    {
        updateLanes (false);
        reapplyTapSlope_ = true;
    }
    if (c.bassXoverHz != old.bassXoverHz)
        xovLane_.arm (onePoleCoeff (std::min (std::max (c.bassXoverHz, 20.0f), 10000.0f), rate_), 200.0f, rate_);
    if (c.rtHiCutHz != old.rtHiCutHz)
        configureDamping (false, true);
    if (c.damping != old.damping)
        configureDamping (false, false);
    if (c.tapSlope != old.tapSlope)
        setTapSlope (static_cast<float> (c.tapSlope));
    if (c.spinHz != old.spinHz)
    {
        spinOn_ = c.spinHz > 0.0f;
        if (secondary_.kick == 0)
        {
            secondary_.rate = c.spinHz * 0.27f;
            secondary_.rateJitter = 0.25f;
            secondary_.armed = true;
        }
        if (mod_.kick == 0)
        {
            mod_.rate = c.spinHz;
            mod_.rateJitter = 0.0f;
            mod_.armed = true;
        }
    }
    if (c.tailWidthDeg != old.tailWidthDeg)
        armMix();
    if (c.earlyLevelDb != old.earlyLevelDb)
        earlyRamp_.arm (dbToAmp (c.earlyLevelDb), 200.0f, rate_);
    if (c.reverbLevelDb != old.reverbLevelDb)
        reverbRamp_.arm (dbToAmp (c.reverbLevelDb), 200.0f, rate_);
    if (std::memcmp (c.reflections.data(), old.reflections.data(), sizeof c.reflections) != 0
        || std::memcmp (c.echoes.data(), old.echoes.data(), sizeof c.echoes) != 0
        || c.reflectionTimePercent != old.reflectionTimePercent || c.echoTimePercent != old.echoTimePercent
        || c.echoFeedbackPercent != old.echoFeedbackPercent)
        updateVoices (false);
    // The output filters read their settings every block.
}

void SectionReverbEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
{
    if (! prepared_ || numSamples <= 0)
        return;
    ranSinceClear_ = true;
    const float* in[2] = { inL, inR };
    float* out[2] = { outL, outR };
    int pos = 0;
    while (pos < numSamples)
    {
        const int take = std::min (numSamples - pos, kBlock - acc_);
        for (int ch = 0; ch < 2; ++ch)
        {
            // The input chunk is taken before the output overwrites it (in place is allowed).
            if (in[ch] != nullptr)
                std::memcpy (inBus_[ch] + acc_, in[ch] + pos, static_cast<std::size_t> (take) * sizeof (float));
            else
                std::memset (inBus_[ch] + acc_, 0, static_cast<std::size_t> (take) * sizeof (float));
            std::memcpy (out[ch] + pos, outBus_[ch] + acc_, static_cast<std::size_t> (take) * sizeof (float));
        }
        acc_ += take;
        pos += take;
        if (acc_ == kBlock)
        {
            processBlock();
            acc_ = 0;
        }
    }
}

void SectionReverbEngine::processBlock() noexcept
{
    // Hall II limits its input before diffusion. Classify NaNs by their
    // bits since fast-math builds may assume that every float is a number.
    if (config_.model == static_cast<int> (SectionReverbModel::Hall))
        for (auto& channel : inBus_)
            for (float& x : channel)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof (bits));
                x = (bits & 0x7fffffffu) > 0x7f800000u ? 0.0f : std::clamp (x, -4.0f, 4.0f);
            }
    // Every silent input channel block draws one number from the tail's generator.
    for (int ch = 0; ch < 2; ++ch)
        if (std::all_of (std::begin (inBus_[ch]), std::end (inBus_[ch]), [] (float v) { return v == 0.0f; }))
        {
            if (auditoriumModel()) auditorium_.nextRandom();
            else nextRandom();
        }
    if (auditoriumModel())
    {
        lineBlock(inBus_);
        float send[2][kBlock], filtered[2][kBlock];
        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                auto& bus = spread_[ch].bus;
                send[ch][i] = bus[0][i] + bus[1][i] + bus[2][i];
                bus[4][i] = bus[3][i];
                peak = std::max(peak, std::fabs(inBus_[ch][i]));
            }
        auditorium_.filterInput(send, filtered);
        stage1Block(filtered);
        auditorium_.processBlock(diffused_, lane_, peak, previousWetPeak_);
        mixBlock();
        return;
    }
    stage1Block(inBus_);
    if (roomModel())
        earlyBlock();
    else
        lineBlock(diffused_);
    tailBlock();
    mixBlock();
}

// ---- Input diffusers ----

void SectionReverbEngine::setStage1ReadHeads (bool snap) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    const float sizeFactor = sizeScale();
    for (int ch = 0; ch < 2; ++ch)
        for (int t = 0; t < model.diffuserCount; ++t)
        {
            Allpass& ap = stage1_[ch][t];
            const float base = static_cast<float> (model.diffusers[ch][t].delay) * rate_.srScale;
            const auto offset = static_cast<std::uint32_t> (sizeFactor * base);
            ap.newRead = (ap.write + ap.length - offset) % ap.length;
            if (snap)
                ap.oldRead = ap.newRead;
        }
}

// A moved Size crossfades the diffusers' reads to the new lengths and back to one read.
void SectionReverbEngine::advanceStage1Size() noexcept
{
    switch (stage1SizeState_)
    {
        case SizeChange::Idle:
            if (stage1SizePending_)
            {
                stage1SizePending_ = false;
                setStage1ReadHeads (false);
                stage1SizeBlend_.arm (0.0f, 200.0f, rate_);
                stage1SizeState_ = SizeChange::FadeToNew;
            }
            break;
        case SizeChange::FadeToNew:
            if (! stage1SizeBlend_.ramping())
                stage1SizeState_ = SizeChange::Wait;
            break;
        case SizeChange::Wait:
            stage1SizeState_ = SizeChange::Adopt;
            break;
        case SizeChange::Adopt:
            for (auto& side : stage1_)
                for (Allpass& ap : side)
                    ap.oldRead = ap.newRead;
            stage1SizeState_ = SizeChange::Return;
            break;
        case SizeChange::Return:
            stage1SizeBlend_.arm (1.0f, 200.0f, rate_);
            stage1SizeState_ = SizeChange::FadeToOld;
            break;
        case SizeChange::FadeToOld:
            if (! stage1SizeBlend_.ramping())
                stage1SizeState_ = SizeChange::Idle;
            break;
    }
    stage1SizeBlend_.advanceBlock();
}

void SectionReverbEngine::stage1Block(const float input[2][kBlock]) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    advanceStage1Size();
    diffusion_.advanceBlock();
    const float oldWeight = stage1SizeBlend_.current();
    const float newWeight = 1.0f - oldWeight;
    for (int ch = 0; ch < 2; ++ch)
    {
        float x[kBlock];
        for (int i = 0; i < kBlock; ++i)
            x[i] = input[ch][i] + echoFeedback_[ch][i];
        for (int t = 0; t < model.diffuserCount; ++t)
        {
            Allpass& ap = stage1_[ch][t];
            const float g = diffusion_.current() * model.diffusers[ch][t].gain;
            float* buf = ap.buffer.data();
            for (int i = 0; i < kBlock; ++i)
            {
                // At zero diffusion the allpass is a pure delay.
                const float d = oldWeight * buf[ap.oldRead] + newWeight * buf[ap.newRead];
                const float y = x[i] - g * d;
                buf[ap.write] = y;
                x[i] = g * y + d;
                if (++ap.oldRead >= ap.length) ap.oldRead = 0;
                if (++ap.newRead >= ap.length) ap.newRead = 0;
                if (++ap.write >= ap.length) ap.write = 0;
            }
        }
        std::copy_n (x, kBlock, diffused_[ch]);
    }
}

// ---- Room: early pattern and tail feed ----

unsigned SectionReverbEngine::roomPredelaySamples (bool early) const noexcept
{
    const float value = early ? config_.earlyPredelayMs : config_.reverbPredelayMs;
    if (early)
        return static_cast<unsigned> (value * rate_.srMs);
    // Round the scaled rate before converting the feed delay to whole samples.
    // At 48 kHz a 5 ms feed at scale 1.8 starts at sample 431.
    const float samplesPerMs = static_cast<float> (double (config_.roomScale) * (double (rate_.sr) / 1000.0));
    return static_cast<unsigned> (value * samplesPerMs);
}

void SectionReverbEngine::loadPattern() noexcept
{
    const SectionPattern& pattern = kSectionPatterns[config_.pattern];
    activeTaps_ = static_cast<unsigned> (std::min (pattern.count, kMaxPatternTaps));
    nominalLength_ = static_cast<float> (pattern.length);
    for (unsigned i = 0; i < activeTaps_; ++i)
    {
        // Consecutive taps go to the four groups in turn.
        const SectionPatternTap& p = pattern.taps[i];
        const unsigned j = (i % 4) * (activeTaps_ / 4) + i / 4;
        taps_[j] = { p.source, p.output, 0, p.time, p.gain };
    }
}

void SectionReverbEngine::retargetEarly (unsigned group, unsigned initialLag) noexcept
{
    const float scale = config_.roomScale * rate_.srScale;
    patternLength_ = static_cast<unsigned> (nominalLength_ * scale);
    const unsigned lead = roomPredelaySamples (true) + initialLag;
    const unsigned count = activeTaps_ / 4;
    for (unsigned i = group * count; i < (group + 1) * count; ++i)
    {
        EarlyTap& tap = taps_[i];
        const int forward = static_cast<int> (tap.time * scale);
        // Reversed, a few patterns reach past their stated end: a negative offset then reads
        // ahead in the circular history.
        const int when = config_.reverse != 0 ? static_cast<int> (patternLength_) - forward : forward;
        const int offset = when + static_cast<int> (lead);
        tap.delay = static_cast<unsigned> (offset < 0 ? offset + static_cast<int> (inputLength_) : offset) % inputLength_;
    }
}

// After a reset or a moved pattern the four groups sweep once through their rows (a quarter wave,
// four null crossings, the release), each group moving its taps at its null; then they rest at
// half power.
void SectionReverbEngine::advanceEarlyGroups() noexcept
{
    const auto arm = [this] {
        if (! tapsDirty_)
            return;
        tapsDirty_ = false;
        sweepStep_ = 0;
        sweepPhase_ = 0.0f;
        phasePeriod_ = std::max (1u, static_cast<unsigned> (rate_.sr32 / 12));
        phaseBlocks_ = phasePeriod_ + 1;
    };
    if (sweepStep_ < 0)
    {
        arm();
        return;
    }
    if (phaseBlocks_ > phasePeriod_)
    {
        --phaseBlocks_;
        return;
    }
    if (--phaseBlocks_ != 0)
    {
        sweepPhase_ += 45.0f / static_cast<float> (phasePeriod_);
        const float previousThird = groupGain_[2];
        for (unsigned g = 0; g < 4; ++g)
        {
            if (sweepStep_ == 0 && g % 2 == 0)
                continue;
            if (sweepStep_ == 5 && g % 2 != 0)
            {
                if (g == 3)
                    groupGain_[g] = previousThird;
                continue;
            }
            groupGain_[g] = std::fabs (std::sin ((sweepPhase_ + 45.0f * static_cast<float> (g)) * 0.01745329252f));
        }
        return;
    }
    if (sweepStep_ == 5)
    {
        std::fill_n (groupGain_, 4, kGroupLevel);
        sweepStep_ = -1;
        arm();
        return;
    }
    static constexpr unsigned rows[] = { 1, 1, 2, 3, 0 };
    static constexpr unsigned order[] = { 0, 3, 2, 1 };
    static constexpr float weights[][4] = {
        { 0, kGroupLevel, 1, kGroupLevel }, { kGroupLevel, 1, kGroupLevel, 0 },
        { 1, kGroupLevel, 0, kGroupLevel }, { kGroupLevel, 0, kGroupLevel, 1 } };
    const unsigned row = rows[sweepStep_++];
    sweepPhase_ = static_cast<float> (row) * 45;
    std::copy_n (weights[row], 4, groupGain_);
    retargetEarly (order[row]);
    phaseBlocks_ = phasePeriod_ = std::max (1u, static_cast<unsigned> (rate_.sr32 * 0.25f));
}

void SectionReverbEngine::updateFeedDelay() noexcept
{
    const unsigned lead = roomPredelaySamples (false);
    feedDelay_[0] = std::min (lead, feedLength_ - 1);
    feedDelay_[1] = std::min (lead + 439u * static_cast<unsigned> (rate_.srScale), feedLength_ - 1);
}

// A new pattern fades the early input out, lets the lines drain, loads and places the taps, and
// fades back in; a moved reverb pre-delay crossfades the feed's reads.
void SectionReverbEngine::beginEarlyBlock() noexcept
{
    switch (patternState_)
    {
        case PatternState::Active:
            if (selectPending_)
            {
                patternInputGain_.setTarget (0, 25, rate_);
                patternState_ = PatternState::FadeOut;
            }
            break;
        case PatternState::FadeOut:
            patternInputGain_.advanceBlock();
            if (! patternInputGain_.ramping())
            {
                drainRemaining_ = patternLength_ + 32;
                patternState_ = PatternState::Drain;
            }
            break;
        case PatternState::Drain:
            if (drainRemaining_ <= 32)
                patternState_ = PatternState::Load;
            else
                drainRemaining_ -= 32;
            break;
        case PatternState::Load:
            loadPattern();
            selectPending_ = false;
            patternState_ = PatternState::Map;
            break;
        case PatternState::Map:
            patternState_ = PatternState::Position;
            break;
        case PatternState::Position:
            for (unsigned g = 0; g < 4; ++g)
                retargetEarly (g, 32);
            patternState_ = PatternState::Enable;
            break;
        case PatternState::Enable:
            patternInputGain_.setTarget (1, 25, rate_);
            patternState_ = PatternState::FadeIn;
            break;
        case PatternState::FadeIn:
            patternInputGain_.advanceBlock();
            if (! patternInputGain_.ramping())
                patternState_ = PatternState::Active;
            break;
    }

    if (feedFade_.ramping())
        feedFade_.advanceBlock();
    else if (feedFade_.current() == 0.0f)
    {
        std::copy_n (feedDelay_, 2, oldFeedDelay_);
        feedFade_.reset (1);
    }
    else if (feedDirty_)
    {
        updateFeedDelay();
        feedFade_.setTarget (0, 25, rate_);
        feedDirty_ = false;
    }
}

void SectionReverbEngine::earlyBlock() noexcept
{
    beginEarlyBlock();
    const bool active = patternState_ == PatternState::Active || patternState_ == PatternState::FadeOut
                        || patternState_ == PatternState::Drain || patternState_ == PatternState::FadeIn;
    const float a = bassPole_.current(), b = bassGain_.current(), gain = patternInputGain_.current();
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill_n (early_[ch], kBlock, 0.0f);
        float* dest = inputLine_[static_cast<std::size_t> (ch)].data() + inputPos_;
        if (active)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float sample = diffused_[ch][i];
                low_[ch] = sample * a + low_[ch] * (1.0f - a);
                dest[i] = (low_[ch] * b + sample) * gain;
            }
        }
        else
        {
            std::fill_n (dest, kBlock, 0.0f);
        }
        if (inputPos_ == 0)
            std::copy_n (dest, kBlock, dest + inputLength_);
    }
    if (active)
    {
        advanceEarlyGroups();
        const unsigned perGroup = activeTaps_ / 4;
        for (unsigned j = 0; j < activeTaps_; ++j)
        {
            const EarlyTap& tap = taps_[j];
            const float weight = tap.gain * groupGain_[j / perGroup];
            const float* source = inputLine_[tap.source].data() + readOffset (inputPos_, tap.delay, inputLength_);
            float* dest = early_[tap.output];
            for (int i = 0; i < kBlock; ++i)
                dest[i] += source[i] * weight;
        }
        bassPole_.advanceBlock();
        bassGain_.advanceBlock();
    }
    const float oldWeight = feedFade_.current();
    for (int ch = 0; ch < 2; ++ch)
    {
        float* line = feedLine_[static_cast<std::size_t> (ch)].data();
        std::copy_n (early_[ch], kBlock, line + feedPos_);
        if (feedPos_ == 0)
            std::copy_n (early_[ch], kBlock, line + feedLength_);
        const float* prior = line + readOffset (feedPos_, oldFeedDelay_[ch], feedLength_);
        const float* next = line + readOffset (feedPos_, feedDelay_[ch], feedLength_);
        for (int i = 0; i < kBlock; ++i)
            feed_[ch][i] = prior[i] * (oldWeight * kFeedLevel) + next[i] * (1.0f - oldWeight);
    }
    inputPos_ = (inputPos_ + kBlock) % inputLength_;
    feedPos_ = (feedPos_ + kBlock) % feedLength_;
}


// ---- Long line (Chamber, Vintage Plate): spread taps, reflections and echoes ----

unsigned SectionReverbEngine::linePredelaySamples() const noexcept
{
    const float ms = std::min (std::max (config_.predelayMs, 0.0f), 1000.0f);
    return static_cast<unsigned> (ms * rate_.srMs);
}

void SectionReverbEngine::setShapeTargets() noexcept
{
    if (auditoriumModel())
    {
        sectionreverb::auditoriumShapeGains(config_.shape, shapeTarget_);
        return;
    }
    const int idx = std::min (std::max (static_cast<int> (config_.shape), 0), 64);
    for (int k = 0; k < spreadTapCount(); ++k)
        shapeTarget_[k] = kSectionShapeCurves[k][idx];
}

void SectionReverbEngine::positionSpreadTap (unsigned k) noexcept
{
    const float spreadNorm = std::min (std::max (config_.spread * 0.01f, 0.0f), 1.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        constexpr int auditoriumOffsets[2][4] {{0, 2390, 8154, 0}, {0, 4497, 13488, 0}};
        const int base = auditoriumModel() ? auditoriumOffsets[ch][k] : kSpreadOffsets[ch][k];
        const unsigned offset = static_cast<unsigned> (spreadNorm * rate_.srScale * static_cast<float> (base)) + linePredelaySamples();
        spread_[ch].tap[k] = (linePhase_ + lineLength_ - offset % lineLength_) % lineLength_;
    }
}

// A moved Spread or pre-delay fades each spread tap out, moves it and fades it back in, one after
// another; a moved Shape glides the five levels together.
void SectionReverbEngine::advanceSpread() noexcept
{
    switch (spreadState_)
    {
        case SpreadChange::Idle:
            if (spreadDirty_)
            {
                spreadDirty_ = false;
                movingTap_ = 0;
                spreadGain_[0].arm (0.0f, 25.0f, rate_);
                spreadState_ = SpreadChange::FadeOut;
            }
            else if (shapeDirty_)
            {
                shapeDirty_ = false;
                for (int k = 0; k < spreadTapCount(); ++k)
                    spreadGain_[k].arm (shapeTarget_[k], 25.0f, rate_);
                spreadState_ = SpreadChange::Shape;
            }
            break;
        case SpreadChange::FadeOut:
            spreadGain_[movingTap_].advanceBlock();
            if (! spreadGain_[movingTap_].ramping())
                spreadState_ = SpreadChange::Move;
            break;
        case SpreadChange::Move:
            positionSpreadTap (movingTap_);
            spreadGain_[movingTap_].arm (shapeTarget_[movingTap_], 25.0f, rate_);
            spreadState_ = SpreadChange::FadeIn;
            break;
        case SpreadChange::FadeIn:
            spreadGain_[movingTap_].advanceBlock();
            if (! spreadGain_[movingTap_].ramping())
            {
                if (++movingTap_ == static_cast<unsigned> (spreadTapCount()))
                    spreadState_ = SpreadChange::Idle;
                else
                {
                    spreadGain_[movingTap_].arm (0.0f, 25.0f, rate_);
                    spreadState_ = SpreadChange::FadeOut;
                }
            }
            break;
        case SpreadChange::Shape:
            spreadState_ = SpreadChange::Idle;
            for (int k = 0; k < spreadTapCount(); ++k)
            {
                spreadGain_[k].advanceBlock();
                if (spreadGain_[k].ramping())
                    spreadState_ = SpreadChange::Shape;
            }
            break;
    }
}

void SectionReverbEngine::updateVoices (bool snap) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    const float maxDelay = static_cast<float> (lineLength_ - 32u);
    const float reflectionMaster = config_.reflectionTimePercent / 100.0f;
    for (int i = 0; i < model.reflectionCount; ++i)
    {
        Voice& v = reflections_[i];
        v.line = static_cast<std::uint32_t> (model.reflections[i].line);
        v.outCh = static_cast<std::uint32_t> (model.reflections[i].output);
        // L-L, R-R, L-R, R-L
        const int which = v.line == v.outCh ? (v.line == 0 ? 0 : 1) : (v.line == 0 ? 2 : 3);
        const SectionReverbReflection& r = config_.reflections[static_cast<std::size_t> (which)];
        v.gain = (r.inverted != 0 ? -1.0f : 1.0f) * dbToAmp (r.gainDb);
        v.delay = static_cast<std::uint32_t> (std::clamp (r.delayMs * reflectionMaster * rate_.srMs, 0.0f, maxDelay));
        v.sync (snap);
    }
    const float feedbackMaster = config_.echoFeedbackPercent * 0.01f;
    const float timeMaster = config_.echoTimePercent / 100.0f;
    for (int e = 0; e < model.echoCount; ++e)
    {
        Voice& v = echoes_[e];
        const SectionReverbEcho& echo = config_.echoes[static_cast<std::size_t> (e)];
        v.line = static_cast<std::uint32_t> (model.echoes[e].line);
        v.outCh = static_cast<std::uint32_t> (model.echoes[e].output);
        v.gain = (echo.gainInverted != 0 ? -1.0f : 1.0f) * dbToAmp (echo.gainDb);
        // Below unity: a feedback at the top of its range would otherwise ring on for good.
        v.fb = (echo.feedbackInverted != 0 ? -1.0f : 1.0f) * std::min (dbToAmp (echo.feedbackDb) * feedbackMaster, 0.98f);
        v.delay = static_cast<std::uint32_t> (std::min (std::max (echo.delayMs * timeMaster * rate_.srMs, 0.0f), maxDelay));
    }
    // Each echo's feedback stays under 0.98, but two echoes feeding the same input would still add
    // up past unity and ring on for good: the feedback summed into each input is held to 0.98 too.
    // At any feedback the factory settings use the sums are far below that, and nothing changes.
    for (std::uint32_t input = 0; input < 2; ++input)
    {
        float sum = 0.0f;
        for (int e = 0; e < model.echoCount; ++e)
            if (static_cast<std::uint32_t> (model.echoes[e].feedback) == input)
                sum += std::fabs (echoes_[e].fb);
        if (sum > 0.98f)
            for (int e = 0; e < model.echoCount; ++e)
                if (static_cast<std::uint32_t> (model.echoes[e].feedback) == input)
                    echoes_[e].fb *= 0.98f / sum;
    }
    for (int e = 0; e < model.echoCount; ++e)
        echoes_[e].sync (snap);
}

void SectionReverbEngine::lineBlock(const float input[2][kBlock]) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    advanceSpread();
    for (int ch = 0; ch < 2; ++ch)
    {
        float* line = lines_[static_cast<std::size_t> (ch)].data();
        std::copy_n (input[ch], kBlock, line + linePhase_);
        if (linePhase_ == 0)
            std::copy_n (input[ch], kBlock, line + lineLength_);
        for (int k = 0; k < spreadTapCount(); ++k)
        {
            const std::uint32_t tap = spread_[ch].tap[k];
            const float gain = spreadGain_[k].current();
            for (int i = 0; i < kBlock; ++i)
                spread_[ch].bus[k][i] = line[tap + static_cast<std::uint32_t> (i)] * gain;
        }
        for (auto& tap : spread_[ch].tap)
            tap = (tap + kBlock) % lineLength_;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill_n (early_[ch], kBlock, -0.0f);
        std::fill_n (echoFeedback_[ch], kBlock, -0.0f);
    }
    for (int r = 0; r < model.reflectionCount; ++r)
    {
        Voice& v = reflections_[r];
        const float* line = lines_[v.line].data();
        float* dst = early_[v.outCh];
        const std::uint32_t idx = (linePhase_ + lineLength_ - (v.appliedDelay % lineLength_)) % lineLength_;
        for (int i = 0; i < kBlock; ++i)
            dst[i] += line[idx + static_cast<std::uint32_t> (i)] * v.direct.current();
        v.endBlock (rate_, lineLength_);
    }
    for (int e = 0; e < model.echoCount; ++e)
    {
        Voice& v = echoes_[e];
        const float* line = lines_[v.line].data();
        float* out = early_[v.outCh];
        float* fb = echoFeedback_[model.echoes[e].feedback];
        const std::uint32_t idx = (linePhase_ + lineLength_ - (v.appliedDelay % lineLength_)) % lineLength_;
        for (int i = 0; i < kBlock; ++i)
        {
            const float s = line[idx + static_cast<std::uint32_t> (i)];
            out[i] += s * v.direct.current();
            fb[i] += s * v.feedback.current();
        }
        v.endBlock (rate_, lineLength_);
    }
    linePhase_ = (linePhase_ + kBlock) % lineLength_;
}

// ---- Tail ----

const float* SectionReverbEngine::tailInput (int section, int slot) const noexcept
{
    const TailInput& in = modelTable (config_.model).sections[section].in[slot];
    if (in.side < 0 || in.tap < 0 || in.side > 1 || in.tap > 4)
        return kZeroBlock;
    if (roomModel())
        return feed_[in.side];
    return spread_[in.side].bus[in.tap];
}

void SectionReverbEngine::resetTail() noexcept
{
    const ModelTable& model = modelTable (config_.model);
    const float scale = sizeScale() * rate_.srScale;
    for (int s = 0; s < model.sectionCount; ++s)
    {
        Section& x = sections_[static_cast<std::size_t> (s)];
        const SectionTable& table = model.sections[s];
        const auto raw = static_cast<std::uint32_t> ((static_cast<float> (table.maxDelay) * 20.0f + 1080.0f) * rate_.lenScale + 64.0f);
        x.length = ((raw + 31u) & ~31u) - 32u;
        std::fill (x.ring.begin(), x.ring.end(), 0.0f);
        std::fill (x.rawRing.begin(), x.rawRing.end(), 0.0f);
        std::fill_n (x.morph, kBlock, -0.0f);
        x.lp = x.cross = x.band = 0.0f;
        x.write = 0;
        x.maxDelay = static_cast<std::uint32_t> (table.maxDelay);
        x.delayScale = x.delayTarget = scale;
        if (nestedModel())
        {
            constexpr unsigned groups[] {0, 2, 1, 3, 0, 2};
            x.nested.reset(groups[s], static_cast<float>(table.latticeDelay), scale, x.write, x.length);
        }
        x.apDelay = static_cast<std::uint32_t> (static_cast<float> (table.latticeDelay) * x.delayScale);
        x.apIdx = (x.write + x.length - (x.apDelay % x.length)) % x.length;
        x.morphOffset = static_cast<std::uint32_t> (static_cast<float> (x.maxDelay) * x.delayScale);
        x.morphA = (x.write + x.length - (x.morphOffset % x.length)) % x.length;
        x.morphB = x.morphA;
        x.morphRamp.reset (1.0f);
        x.bandPole.reset (1.0f);
        x.crossPole = 1.0f;
        x.gain1.reset (model.latticeGain);
        x.gain2.reset (-model.latticeGain);
        x.gainFull1 = -model.latticeGain;
        x.gainFull2 = model.latticeGain;
        x.fadeState = 0;
        x.delayPending = false;
        x.gainPending = false;
    }
    std::memset (lane_, 0, sizeof lane_);
    mod_ = Mod {};
    for (int k = 0; k < 4; ++k)
    {
        std::fill_n (mod_.lane[k], kBlock, kHalfPower);
        mod_.cur[k] = mod_.tgt[k] = kHalfPower;
    }
    mod_.armed = true;
    secondary_ = mod_;
    readSizeScale_ = scale;
    rng_ = 1u;
    for (int g = 0; g < 4; ++g)
        retargetGroup (g, scale, 0.0f);
}

void SectionReverbEngine::updateLanes (bool snap) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    float g = rtGain (config_.model, config_.reverbTime);
    if (nestedModel()) g *= nestedMaxGain(config_.sizeMeters) / 0.99f;
    const float bassRt = std::min (std::max (config_.bassRt, 0.01f), 10.0f);
    const float delta = std::pow (g, 1.0f / bassRt) - g;
    rtG_ = g;
    rtDelta_ = delta;
    if (model.correctLattice)
        for (int s = 0; s < model.sectionCount; ++s)
        {
            Section& x = sections_[static_cast<std::size_t> (s)];
            const float correction = latticeCorrection (model, model.sections[s], g);
            x.gainFull1 = correction - model.latticeGain;
            x.gainFull2 = correction + model.latticeGain;
            if (snap)
            {
                x.gain1.reset (x.gainFull1);
                x.gain2.reset (x.gainFull2);
            }
            x.gainPending = true;
        }
    const float xov = onePoleCoeff (std::min (std::max (config_.bassXoverHz, 20.0f), 10000.0f), rate_);
    if (snap)
    {
        gLane_.reset (g);
        deltaLane_.reset (delta);
        xovLane_.reset (xov);
    }
    else
    {
        gLane_.arm (g, 40.0f, rate_);
        deltaLane_.arm (delta, 40.0f, rate_);
        xovLane_.arm (xov, 200.0f, rate_);
    }
}

void SectionReverbEngine::configureDamping (bool snap, bool updateCross) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    const float hc = onePoleCoeff (config_.rtHiCutHz, rate_);
    const int mode = std::clamp (config_.damping, 0, 2);
    for (int s = 0; s < model.sectionCount; ++s)
    {
        Section& x = sections_[static_cast<std::size_t> (s)];
        const float target = model.sections[s].threshold <= mode ? hc : 1.0f;
        if (snap || target == x.bandPole.current())
            x.bandPole.reset (target);
        else
            x.bandPole.setTarget (target, 200.0f, rate_);
        if (updateCross)
            x.crossPole = hc;
    }
}

void SectionReverbEngine::setTapSlope (float value) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    if (model.sectionCount == 0) return;
    computeTapSlope (value, std::pow (10.0f, model.tapGainDb / 20.0f), model.sections[0].tapCount, tapWeights_);
    // A tenth of the value is kept and re-applied after a decay change (and once after a reset),
    // so the re-applied slope reads from the table's middle.
    storedTapSlope_ = value / 10.0f;
}

void SectionReverbEngine::requestSize() noexcept
{
    const float target = sizeScale() * rate_.srScale;
    for (int s = 0; s < modelTable (config_.model).sectionCount; ++s)
    {
        auto& x = sections_[static_cast<std::size_t> (s)];
        x.delayTarget = target;
        x.delayPending = true;
        if (nestedModel())
        {
            if (x.nested.requestSize(target) == sectionreverb::NestedSection::MorphAction::FadeOut)
                x.morphRamp.arm(0.0f, 200.0f, rate_);
            x.delayScale = x.nested.sizeScale();
        }
    }
    mod_.kick = 3;
    secondary_.kick = 3;
    secondary_.armed = true;
    secondary_.rate = 0.5f;
    secondary_.rateJitter = 0.0f;
    mod_.rateJitter = 0.0f;
    mod_.rate = 0.5f;
    mod_.armed = true;
}

void SectionReverbEngine::swapMorph (Section& x) noexcept
{
    x.morphOffset = static_cast<std::uint32_t> (static_cast<float> (x.maxDelay) * x.delayScale);
    x.morphA = (x.write + x.length - (x.morphOffset % x.length)) % x.length;
    x.morphRamp.arm (1.0f, 200.0f, rate_);
}

void SectionReverbEngine::triggerDelay (Section& x, int s) noexcept
{
    x.delayPending = false;
    x.apDelay = static_cast<std::uint32_t> (static_cast<float> (modelTable (config_.model).sections[s].latticeDelay) * x.delayTarget);
    if (x.delayTarget <= x.delayScale)
    {
        if (x.delayTarget < x.delayScale)
        {
            x.gain1.arm (0.0f, 200.0f, rate_);
            x.gain2.arm (0.0f, 200.0f, rate_);
            x.fadeState = 0xb;
        }
        x.delayScale = x.delayTarget;
        return;
    }
    x.morphRamp.arm (0.0f, 200.0f, rate_);
    x.delayScale = x.delayTarget;
    x.fadeState = 2;
}

// A moved Size: the lattice gains fade out, the lattice span moves, they fade back in, and the
// section's read point crossfades to the new length (growing: read first; shrinking: lattice first).
void SectionReverbEngine::fadeBlock (Section& x, int s) noexcept
{
    const bool gainsIdle = ! x.gain1.ramping() && ! x.gain2.ramping();
    const bool morphIdle = ! x.morphRamp.ramping();
    const auto advanceGains = [&x] { x.gain1.advanceBlock(); x.gain2.advanceBlock(); };
    const auto alignLattice = [&x] { x.apIdx = (x.write + x.length - (x.apDelay % x.length)) % x.length; };
    switch (x.fadeState)
    {
        case 1:
            if (gainsIdle) x.fadeState = 0; else advanceGains();
            break;
        case 2:
            if (morphIdle) x.fadeState = 3; else x.morphRamp.advanceBlock();
            break;
        case 3:
            swapMorph (x);
            x.fadeState = 4;
            break;
        case 4:
            if (morphIdle) x.fadeState = 5; else x.morphRamp.advanceBlock();
            break;
        case 5:
            x.gain1.arm (0.0f, 200.0f, rate_);
            x.gain2.arm (0.0f, 200.0f, rate_);
            x.fadeState = 6;
            break;
        case 6:
            if (gainsIdle) x.fadeState = 7; else advanceGains();
            break;
        case 7:
            x.fadeState = 8;
            break;
        case 8:
            alignLattice();
            x.fadeState = 9;
            break;
        case 9:
            x.gain1.arm (x.gainFull1, 200.0f, rate_);
            x.gain2.arm (x.gainFull2, 200.0f, rate_);
            x.gainPending = false;
            x.fadeState = 10;
            break;
        case 10:
            if (gainsIdle) x.fadeState = 0x13; else advanceGains();
            break;
        case 0xb:
            if (gainsIdle) x.fadeState = 0xc; else advanceGains();
            break;
        case 0xc:
            x.fadeState = 0xd;
            break;
        case 0xd:
            alignLattice();
            x.fadeState = 0xe;
            break;
        case 0xe:
            x.gain1.arm (x.gainFull1, 200.0f, rate_);
            x.gain2.arm (x.gainFull2, 200.0f, rate_);
            x.gainPending = false;
            x.fadeState = 0xf;
            break;
        case 0xf:
            if (gainsIdle)
            {
                x.morphRamp.arm (0.0f, 200.0f, rate_);
                x.fadeState = 0x10;
            }
            else
            {
                advanceGains();
            }
            break;
        case 0x10:
            if (morphIdle) x.fadeState = 0x11; else x.morphRamp.advanceBlock();
            break;
        case 0x11:
            swapMorph (x);
            x.fadeState = 0x12;
            break;
        case 0x12:
            if (morphIdle) x.fadeState = 0x13; else x.morphRamp.advanceBlock();
            break;
        case 0x13:
            x.fadeState = 0;
            x.morphB = x.morphA;
            break;
        default:
            if (x.delayPending)
            {
                triggerDelay (x, s);
                break;
            }
            if (x.gainPending)
            {
                x.gainPending = false;
                x.gain1.arm (x.gainFull1, 200.0f, rate_);
                x.gain2.arm (x.gainFull2, 200.0f, rate_);
                x.fadeState = 1;
            }
            break;
    }
}

// Linear congruential generator (multiplier 214013, increment 2531011, 15-bit output), reseeded
// on reset.
int SectionReverbEngine::nextRandom() noexcept
{
    rng_ = rng_ * 214013u + 2531011u;
    return static_cast<int> ((rng_ >> 16) & 0x7fffu);
}

float SectionReverbEngine::rand01() noexcept
{
    return static_cast<float> (nextRandom()) / 32767.0f;
}


void SectionReverbEngine::retargetGroup (int group, float scale, float wander) noexcept
{
    const ModelTable& model = modelTable (config_.model);
    for (int s = 0; s < model.sectionCount; ++s)
    {
        Section& x = sections_[static_cast<std::size_t> (s)];
        const SectionTable& table = model.sections[s];
        for (int t = 0; t < table.tapCount; ++t)
        {
            const TailTap& tc = table.taps[t];
            if (tc.lane != group)
                continue;
            const int maxOff = static_cast<int> (x.length) - 32;
            const auto jitter = static_cast<float> (static_cast<int> (rand01() * wander));
            int off = static_cast<int> (static_cast<float> (tc.delay) * scale + jitter);
            off = std::clamp (off, 0, maxOff);
            int idx = static_cast<int> (x.write) - off;
            if (idx < 0)
                idx += static_cast<int> (x.length);
            x.tapRead[t] = static_cast<std::uint32_t> (idx);
        }
    }
}

void SectionReverbEngine::sineGen (Mod& mod, float degrees) noexcept
{
    for (int k = 0; k < 4; ++k)
        mod.tgt[k] = modulationMagnitude ((degrees + 45.0f * static_cast<float> (k)) * 6.2831855f / 360.0f);
}

void SectionReverbEngine::rampLanes (Mod& mod) noexcept
{
    for (int k = 0; k < 4; ++k)
    {
        const float from = mod.cur[k];
        const float to = mod.tgt[k];
        const float d = to - from;
        mod.cur[k] = to;
        float v = from;
        for (int i = 0; i < kBlock; ++i)
        {
            v += d * 0.03125f;
            mod.lane[k][i] = v;
        }
        mod.lane[k][kBlock - 1] = to;
    }
}

// Lane sequencer, mode 1: four rows then a release to half power (Spin at 0).
int SectionReverbEngine::seq1 (Mod& mod) noexcept
{
    if (mod.sub < 4)
    {
        if (mod.countdown == 0 || --mod.countdown == 0)
        {
            const int idx = mod.index;
            for (int k = 0; k < 4; ++k)
                mod.tgt[k] = kRow[idx][k];
            mod.phase = kPhaseDeg[idx];
            mod.index = (idx + 1) & 3;
            ++mod.sub;
            rampLanes (mod);
            return kGroupOrder[idx];
        }
        mod.phase += mod.step;
        sineGen (mod, mod.phase);
        rampLanes (mod);
        return -1;
    }
    if (mod.countdown == 0 || --mod.countdown == 0)
    {
        for (int k = 0; k < 4; ++k)
            mod.tgt[k] = kHalfPower;
        mod.mode = 0;
        rampLanes (mod);
        return -1;
    }
    mod.phase += mod.step;
    sineGen (mod, mod.phase);
    if ((mod.index & 1) == 0)
    {
        mod.tgt[0] = mod.cur[0];
        mod.tgt[2] = mod.cur[2];
    }
    else
    {
        mod.tgt[1] = mod.cur[1];
        mod.tgt[3] = mod.cur[2];
    }
    rampLanes (mod);
    return -1;
}

// Mode 2: the start of rotation, the even lanes held while the odd ones swing to the first row.
int SectionReverbEngine::seq2 (Mod& mod) noexcept
{
    if (mod.countdown == 0 || --mod.countdown == 0)
    {
        for (int k = 0; k < 4; ++k)
            mod.tgt[k] = kRow[1][k];
        mod.phase = 45.0f;
        mod.mode = 3;
        mod.index = 1;
        rampLanes (mod);
        return 3;
    }
    mod.phase += mod.step;
    sineGen (mod, mod.phase);
    mod.tgt[0] = mod.cur[0];
    mod.tgt[2] = mod.cur[2];
    rampLanes (mod);
    return -1;
}

// Mode 3: steady rotation; each row boundary moves one lane's taps.
int SectionReverbEngine::seq3 (Mod& mod) noexcept
{
    if (mod.countdown == 0)
    {
        if (mod.rowFlag)
        {
            mod.rowFlag = false;
            rampLanes (mod);
            return -1;
        }
        const int idx = mod.index;
        mod.phase = kPhaseDeg[idx];
        mod.index = (idx + 1) & 3;
        return kGroupOrder[idx];
    }
    if (--mod.countdown == 0)
    {
        for (int k = 0; k < 4; ++k)
            mod.tgt[k] = kRow[mod.index][k];
        mod.rowFlag = true;
    }
    else
    {
        mod.phase += mod.step;
        sineGen (mod, mod.phase);
    }
    rampLanes (mod);
    return -1;
}

void SectionReverbEngine::seqFallback (Mod& mod) noexcept
{
    if (! mod.armed)
    {
        mod.countdown = mod.period;
        return;
    }
    if (mod.mode == 3)
    {
        if (mod.rate == 0.0f)
        {
            mod.mode = 1;
            mod.sub = 0;
        }
        mod.armed = false;
    }
    else if (mod.mode != 1 && mod.mode != 2)
    {
        if (mod.rate != 0.0f)
        {
            mod.mode = 2;
            mod.index = 0;
            mod.phase = 0.0f;
            mod.armed = false;
        }
    }
    // Modes 1 and 2 keep the arm pending until their transition completes.
    if (mod.armed)
    {
        mod.countdown = mod.period;
        return;
    }
    float rate = mod.rate;
    if (rate == 0.0f)
    {
        mod.period = static_cast<int> (rate_.sr32 * 0.25f);
    }
    else
    {
        if (mod.rateJitter != 0.0f)
        {
            const float rnd = static_cast<float> (nextRandom() - 16383) / 16383.0f;
            rate += rate * mod.rateJitter * rnd;
            mod.armed = true;
        }
        mod.period = static_cast<int> (double (rate_.sr32) / (double (rate) * 4.0));
    }
    if (mod.period < 1)
        mod.period = 1;
    mod.countdown = mod.period;
    mod.step = 45.0f / static_cast<float> (mod.period);
}

int SectionReverbEngine::advanceSequencer (Mod& mod) noexcept
{
    int group = -1;
    switch (mod.mode)
    {
        case 1: group = seq1 (mod); break;
        case 2: group = seq2 (mod); break;
        case 3: group = seq3 (mod); break;
        default: break;
    }
    if (group >= 0 || mod.mode == 0)
        seqFallback (mod);
    return group;
}

void SectionReverbEngine::sequencerBlock() noexcept
{
    const int group = advanceSequencer (mod_);
    if (group >= 0)
        retargetGroup (group, readSizeScale_, wanderSamples());
    const int secondaryGroup = advanceSequencer (secondary_);
    if (nestedModel() && secondaryGroup >= 0)
        for (auto& x : sections_)
            x.nested.retarget(secondaryGroup, wanderSamples(), x.write, x.length, x.morphOffset,
                              [this] { return rand01(); });
    if (mod_.kick != 0 && --mod_.kick == 0)
    {
        mod_.rate = config_.spinHz;
        mod_.rateJitter = 0.0f;
        mod_.armed = true;
    }
    if (secondary_.kick != 0 && --secondary_.kick == 0)
    {
        secondary_.rate = config_.spinHz * 0.27f;
        secondary_.rateJitter = 0.25f;
        secondary_.armed = true;
    }
}


void SectionReverbEngine::tailBlock() noexcept
{
    const ModelTable& model = modelTable (config_.model);
    const float g = gLane_.current();
    const float delta = deltaLane_.current();
    const float xov = xovLane_.current();
    std::memset (lane_, 0, sizeof lane_);

    for (int s = 0; s < model.sectionCount; ++s)
    {
        Section& x = sections_[static_cast<std::size_t> (s)];
        const SectionTable& table = model.sections[s];
        const float a1 = x.bandPole.current();
        const float a2 = x.crossPole;
        x.bandPole.advanceBlock();

        // Kernel: the previous section's read is the loop input; inputs 0 and 1 are injected
        // through the cross one-pole, 2 and 3 added straight in.
        const Section& prev = sections_[static_cast<std::size_t> ((s + model.sectionCount - 1) % model.sectionCount)];
        const float* inA = tailInput (s, 0);
        const float* inB = tailInput (s, 1);
        const float* inC = tailInput (s, 2);
        const float* inD = tailInput (s, 3);
        const float sg = s == 0 ? -g : g;
        const float sd = s == 0 ? -delta : delta;
        const float a1c = 1.0f - a1;
        const float a2c = 1.0f - a2;
        float kout[kBlock];
        for (int i = 0; i < kBlock; ++i)
        {
            const float in = prev.morph[i];
            const float ab = inA[i] + inB[i];
            x.cross = ab * a2 + x.cross * a2c;
            x.lp = in * xov + x.lp * (1.0f - xov);
            x.band = (in * sg + x.lp * sd) * a1 + x.band * a1c;
            const float cd = inC[i] + inD[i];
            kout[i] = cd + (x.cross + x.band);
        }

        float* ring = x.ring.data();
        float* raw = x.rawRing.data();
        // Each ring keeps its first block mirrored past its end, so a 32-sample read never wraps.
        std::copy_n (kout, kBlock, ring + x.write);
        std::copy_n (kout, kBlock, raw + x.write);
        if (x.write == 0)
        {
            std::copy_n (kout, kBlock, ring + x.length);
            std::copy_n (kout, kBlock, raw + x.length);
        }

        const float inject1 = x.gain1.current();
        const float inject2 = x.gain2.current();
        for (std::uint32_t n = 0; n < static_cast<std::uint32_t> (kBlock); ++n)
        {
            // The lattice inside the ring: this sample and the one written apDelay ago exchange.
            const std::uint32_t wi = x.write + n;
            if (nestedModel())
                x.nested.apply(ring, x.length, wi, secondary_.lane[x.nested.shortGroup()][n],
                               secondary_.lane[x.nested.longGroup()][n], -model.latticeGain);
            else
            {
                const float v = ring[x.apIdx];
                const float w = v * inject1 + ring[wi];
                ring[wi] = w;
                if (wi < 32u)
                    ring[wi + x.length] = w;
                const float vNew = w * inject2 + v;
                ring[x.apIdx] = vNew;
                if (x.apIdx < 32u)
                    ring[x.apIdx + x.length] = vNew;
            }

            for (int t = 0; t < table.tapCount; ++t)
            {
                const TailTap& tc = table.taps[t];
                lane_[tc.output][n] += mod_.lane[tc.lane][n] * tapWeights_[t]
                                       * (tc.lattice ? ring : raw)[x.tapRead[t] + n];
            }
            if (++x.apIdx >= x.length)
                x.apIdx = 0;
        }

        const auto nextBlock = [length = x.length] (std::uint32_t index) {
            index += kBlock;
            return index < length ? index : index - length;
        };
        x.write = nextBlock (x.write);
        for (int t = 0; t < table.tapCount; ++t)
            x.tapRead[t] = nextBlock (x.tapRead[t]);

        const float c = x.morphRamp.current();
        for (int i = 0; i < kBlock; ++i)
            x.morph[i] = c * ring[x.morphA + i] + (1.0f - c) * ring[x.morphB + i];
        x.morphA = nextBlock (x.morphA);
        x.morphB = nextBlock (x.morphB);
        if (nestedModel())
        {
            using Action = sectionreverb::NestedSection::MorphAction;
            const auto action = x.nested.advanceSize(x.morphRamp.ramping());
            x.delayScale = x.nested.sizeScale();
            switch (action)
            {
                case Action::Step: x.morphRamp.advanceBlock(); break;
                case Action::Swap: swapMorph(x); break;
                case Action::FadeOut: x.morphRamp.arm(0.0f, 200.0f, rate_); break;
                case Action::Finish: x.morphB = x.morphA; break;
                case Action::None: break;
            }
        }
        else
            fadeBlock (x, s);
    }

    sequencerBlock();
    if (reapplyTapSlope_)
    {
        setTapSlope (storedTapSlope_);
        reapplyTapSlope_ = false;
    }
    gLane_.advanceBlock();
    deltaLane_.advanceBlock();
    xovLane_.advanceBlock();
}

// ---- Output ----

void SectionReverbEngine::armMix() noexcept
{
    const float tw = config_.tailWidthDeg * 6.2831855f / 360.0f;
    mixSRamp_.arm (std::sin (1.5707963f), 200.0f, rate_);
    twCRamp_.arm (std::cos (tw), 200.0f, rate_);
    twSRamp_.arm (std::sin (tw), 200.0f, rate_);
}

void SectionReverbEngine::mixBlock() noexcept
{
    const ModelTable& model = modelTable (config_.model);
    const float ms = mixSRamp_.current();
    const float cT = twCRamp_.current();
    const float sT = twSRamp_.current();
    const float gRvb = reverbRamp_.current();
    const float gEarly = earlyRamp_.current();
    earlyFilter_.set (config_.earlyFilter);
    earlyFilter_.beginBlock();
    reverbFilter_.set (config_.reverbFilter);
    reverbFilter_.beginBlock();

    float* left = lane_[0];
    float* right = lane_[1];
    const float widthSin = (ms * gRvb) * sT;
    const float widthCos = (ms * gRvb) * cT;
    const bool spreadOut = model.lastSpreadTapToOutput && ! model.roomEarly;
    for (int i = 0; i < kBlock; ++i)
    {
        // The last spread tap joins the tail ahead of the width rotation.
        const float a = spreadOut ? right[i] + spread_[1].bus[4][i] : right[i];
        const float b = spreadOut ? left[i] + spread_[0].bus[4][i] : left[i];
        left[i] = a * widthSin + b * widthCos;
        right[i] = a * widthCos + b * widthSin;
    }
    reverbFilter_.process (left, 0);
    reverbFilter_.process (right, 1);
    if (auditoriumModel())
    {
        previousWetPeak_ = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            previousWetPeak_ = std::max({previousWetPeak_, std::fabs(left[i]), std::fabs(right[i])});
    }
    for (int ch = 0; ch < 2; ++ch)
        earlyFilter_.process (early_[ch], ch);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
        {
            const float mixed = lane_[ch][i] + ms * gEarly * early_[ch][i];
            outBus_[ch][i] = std::fabs (mixed) < 1e-15f ? 0.0f : mixed;
        }
    earlyRamp_.advanceBlock();
    reverbRamp_.advanceBlock();
    mixSRamp_.advanceBlock();
    twCRamp_.advanceBlock();
    twSRamp_.advanceBlock();
}

} // namespace duskverb
