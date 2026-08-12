// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// rail_test.cpp — the amp's rail clipping, against the hardware that produced it.
//
// The reference numbers are the 1 kHz ladder measured on channel 1 on
// 2026-08-12 (dusk-audio-tools sessions/ch1-rail-20260812, 1 dB steps across the
// knee, pad 0, trim at the CCW stop). They are hardware, not a model's opinion
// of itself, so a regression here means the plugin stopped matching the console.
//
// Levels below are in the ladder's own units: dB relative to the fitted clip
// threshold. That keeps the gate independent of the plugin's input mapping, so
// changing where 0 dBFS sits cannot silently move this test's goalposts.

#include "RailClip.hpp"
#include "TestSupport.hpp"

#include <cmath>
#include <vector>

using namespace pre35;
using namespace pre35test;

namespace
{

constexpr double kSr = 384000.0;      // 48 kHz x 8, the core's internal rate
constexpr double kF0 = 1000.0;
constexpr double kSeconds = 1.0;

/** Fitted thresholds, as magnitudes relative to the positive one.
    Asymmetry measured at 0.196 dB; see fit_rail.py. */
constexpr double kPos = 1.0;
constexpr double kNeg = 0.97767;       // = 10^(-0.196/20)

/** Measured (dB over threshold, gain change dB, h3 dBc, h2 dBc).
    Threshold fitted at -25.326 dBFS on the ladder's drive axis. */
struct Row { double overDb, dGainDb, h3Dbc, h2Dbc; };
const Row kLadder[] = {
    {  0.326, -0.105, -39.2, -44.0 },
    {  1.326, -0.609, -25.0, -43.2 },
    {  2.326, -1.284, -19.7, -44.2 },
    {  3.326, -2.053, -16.7, -45.7 },
    {  5.326, -3.754, -13.5, -49.3 },
};

std::vector<float> renderTone(double overDb)
{
    RailClip rail;
    rail.setThresholds(kPos, kNeg);
    rail.reset();

    const size_t n = (size_t)std::llround(kSeconds * kSr);
    const double amp = kPos * std::pow(10.0, overDb / 20.0);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double x = amp * std::sin(2.0 * kPiT * kF0 * (double)i / kSr);
        out[i] = (float)rail.processSample(x);
    }
    return out;
}

} // namespace

int main()
{
    Report r("rail");

    // ---- the clean path must be exactly transparent -----------------------
    // Most material never reaches the rail. If the stage colours the signal
    // below threshold then every unclipped render is wrong, which is a far worse
    // failure than a few tenths of a dB of clipping accuracy.
    {
        RailClip rail;
        rail.setThresholds(kPos, kNeg);
        rail.reset();
        double worst = 0.0;
        for (int i = 0; i < 100000; ++i)
        {
            const double x = 0.9 * kNeg * std::sin(2.0 * kPiT * 997.0 * i / kSr);
            worst = std::max(worst, std::abs(rail.processSample(x) - x));
        }
        r.check(worst == 0.0, "below threshold is bit-exact",
                "worst deviation " + std::to_string(worst));
    }

    // ---- gain compression and harmonics vs the hardware -------------------
    double worstGain = 0.0, worstH3 = 0.0, worstH2 = 0.0;
    for (const Row& row : kLadder)
    {
        const std::vector<float> y = renderTone(row.overDb);
        const ThdResult t = measureThd(y, kF0, kSr, 0.1);

        const double amp = std::pow(10.0, row.overDb / 20.0);
        // measureThd's fundamental is a windowed 5-bin sum, so compare it with
        // the same measurement of an unclipped tone rather than to `amp`.
        std::vector<float> ref((size_t)std::llround(kSeconds * kSr));
        for (size_t i = 0; i < ref.size(); ++i)
            ref[i] = (float)(amp * std::sin(2.0 * kPiT * kF0 * (double)i / kSr));
        const ThdResult t0 = measureThd(ref, kF0, kSr, 0.1);

        const double dGain = linToDb(t.fundamental / t0.fundamental);
        worstGain = std::max(worstGain, std::abs(dGain - row.dGainDb));
        worstH3   = std::max(worstH3,   std::abs(t.thd3Dbc - row.h3Dbc));
        worstH2   = std::max(worstH2,   std::abs(t.thd2Dbc - row.h2Dbc));

        r.note(std::to_string(row.overDb) + " dB over: gain " +
               std::to_string(dGain) + " (meas " + std::to_string(row.dGainDb) +
               "), h3 " + std::to_string(t.thd3Dbc) + " (meas " +
               std::to_string(row.h3Dbc) + ")");
    }

    // Limits match fit_rail.py's gates, loosened only by what the rendering
    // path itself contributes: the fit works on an exact spectrum, this renders
    // and re-measures through a window.
    r.below(worstGain, 0.25, "gain compression vs hardware");
    r.below(worstH3,   3.0,  "h3 vs hardware");
    r.below(worstH2,   6.0,  "h2 vs hardware (asymmetry)");

    // ---- aliasing budget at the core's own rate ----------------------------
    // No antialiasing is applied; the 8x oversampling carries it. That is a
    // measured decision, not an omission (see RailClip.hpp), so it needs a gate
    // holding the consequence in place: drive a tone hard into the rail at the
    // internal rate and require the worst inharmonic product left in the audio
    // band to stay under budget.
    {
        const double f = 10000.0, amp = 2.0;
        const size_t n = (size_t)std::llround(0.25 * kSr);

        RailClip rail;
        rail.setThresholds(kPos, kNeg);

        std::vector<double> y(n);
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPiT * (double)i / (double)(n - 1));
            y[i] = rail.processSample(amp * std::sin(2.0 * kPiT * f * (double)i / kSr)) * w;
        }

        // Scan the audio band, skipping anything that is not fold-back. Two
        // exclusions, both of which this gate got wrong at first and both of
        // which made it measure window leakage instead of aliasing:
        //
        //   * The first bins. Asymmetric clipping produces a genuine DC offset
        //     (here -42.4 dBFS), and the Hann mainlobe is +/-2 bins wide, so bins
        //     1 and 2 carry its leakage. That leakage alone read -48.5 dBFS and
        //     was the number this gate reported.
        //   * Harmonics AT the band edge as well as below it. h2 sits at exactly
        //     20 kHz here, and `m * f < 20000.0` is false at 20000, so its
        //     leakage into the neighbouring bin (19996 Hz, -56.4 dBFS) was scored
        //     as fold-back too.
        //
        // What survives both is real: the worst is 6 kHz, which is the 39th
        // harmonic at 390 kHz folding about the 384 kHz internal rate.
        double worst = 0.0;
        for (int k = 3; k < (int)(20000.0 * (double)n / kSr); ++k)
        {
            const double freq = (double)k * kSr / (double)n;
            bool harmonic = false;
            for (int m = 1; m * f <= 20000.0; ++m)
                harmonic = harmonic || std::abs(freq - m * f) < 60.0;
            if (! harmonic)
                worst = std::max(worst, dftBinMag(y, k));
        }
        const double worstDb = linToDb(worst / (0.5 * (double)n));

        // The limit is set FROM this measurement (-62.9 dBFS) with a few dB of
        // margin, not from a target picked in advance. Its job is regression
        // detection: if the oversampling factor drops or the curve changes, this
        // moves. For context the distortion products being generated here sit
        // around -13 dBc, so the fold-back is ~50 dB below the thing it rides on.
        r.below(worstDb, -58.0, "worst in-band alias, 6 dB into the rail", "dBFS");
    }

    return r.exitCode();
}
