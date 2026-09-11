// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../MultiCompDSP.hpp"
#include "MultiCompBusProgrammeFixtures.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;
constexpr double pi = 3.14159265358979323846;
using Audio = std::array<std::vector<float>, 2>;
void require(bool pass, const char* message)
{
    if (!pass) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void configure(DSP& dsp, int rate, int ratio, int attack, int release, float threshold)
{
    dsp.setMode(3); dsp.setOversampling(0);
    dsp.setParameter(P::NoiseEnable, 0); dsp.setParameter(P::AutoMakeup, 0);
    dsp.setParameter(P::TruePeakEnable, 0); dsp.setParameter(P::BusMakeup, 0);
    dsp.setParameter(P::BusHeadroom, 3); dsp.setParameter(P::SidechainHP, 0);
    dsp.setParameter(P::BusRatio, float(ratio)); dsp.setParameter(P::BusAttack, float(attack));
    dsp.setParameter(P::BusRelease, float(release)); dsp.setParameter(P::BusThreshold, threshold);
    dsp.prepare(rate, 256);
    // Exercise the runtime coefficient update as well as initial prepare.
    dsp.setOversampling(1);
}
Audio render(DSP& dsp, const Audio& input, int channels = 2, bool external = false)
{
    Audio output{{std::vector<float>(input[0].size()), std::vector<float>(input[0].size())}};
    for (size_t pos = 0; pos < input[0].size(); pos += 256)
    {
        const int n = int(std::min<size_t>(256, input[0].size() - pos));
        const float* in[] = {input[0].data() + pos, input[1].data() + pos};
        float* out[] = {output[0].data() + pos, output[1].data() + pos};
        if (external) dsp.processBlockExternal(in, in, out, channels, n);
        else dsp.processBlock(in, out, channels, n);
    }
    return output;
}
Audio constant(int samples, float value)
{
    return {{std::vector<float>(size_t(samples), value), std::vector<float>(size_t(samples), value)}};
}
Audio source(int rate, int channels)
{
    Audio audio = constant(9 * rate, 0);
    struct Hit { double start, amplitude, decay; };
    constexpr Hit hits[] = {{.75,.12,.035},{1.15,.24,.065},{1.55,.16,.04},{2.8,.28,.09},
                           {4.1,.14,.06},{4.28,.20,.045},{4.46,.10,.08},{6,.25,.055}};
    for (size_t i = 0; i < audio[0].size(); ++i)
    {
        const double t = double(i) / rate;
        double left = .025 * std::sin(2 * pi * 91 * t + .2);
        double right = .020 * std::sin(2 * pi * 91 * t + .83);
        for (const auto& hit : hits)
        {
            const double u = t - hit.start;
            if (u < 0 || u >= .4) continue;
            const double value = hit.amplitude * (1 - std::exp(-u / .001)) * std::exp(-u / hit.decay)
                * std::sin(2 * pi * (85 * u + 1.325 * std::exp(-u / .025)) + .3);
            left += value; right += .63 * value;
        }
        audio[0][i] = float(left); audio[1][i] = float(channels == 1 ? left : right);
    }
    return audio;
}
void holdouts(bool automatic)
{
    double worst = 0;
    for (const auto& fixture : busprogramme::fixtures)
    {
        if ((fixture.release == 4) != automatic) continue;
        for (int os : {0, 1, 2})
        {
            if (os != 1 && fixture.channels != 1) continue;
            DSP dsp; configure(dsp, fixture.rate, fixture.ratio, fixture.attack,
                               fixture.release, fixture.threshold);
            dsp.setOversampling(os);
            render(dsp, constant(5 * fixture.rate, 0), fixture.channels);
            const auto input = source(fixture.rate, fixture.channels);
            const auto output = render(dsp, input, fixture.channels);
            double localWorst = 0;
            for (size_t point = 0; point < busprogramme::times.size(); ++point)
            {
                const int start = int(std::lround(busprogramme::times[point] * fixture.rate));
                const int count = int(std::lround(.025 * fixture.rate));
                double inputEnergy = 0, outputEnergy = 0;
                for (int ch = 0; ch < fixture.channels; ++ch)
                    for (int i = start; i < start + count; ++i)
                    {
                        const double a = input[ch][size_t(i)];
                        const double b = output[ch][size_t(i + dsp.getLatencySamples())];
                        inputEnergy += a * a; outputEnergy += b * b;
                    }
                const double error = std::abs(10 * std::log10(outputEnergy / inputEnergy) - fixture.gain[point]);
                localWorst = inputEnergy < 1e-6 || !std::isfinite(error)
                    ? std::numeric_limits<double>::infinity() : std::max(localWorst, error);
            }
            worst = std::max(worst, localWorst);
            std::printf("BUS bass programme %d Hz ratio %d release %d %dch os %d: %.6f dB\n",
                fixture.rate, fixture.ratio, fixture.release, fixture.channels, os, localWorst);
        }
    }
    std::printf("BUS bass programme %s worst %.6f dB\n", automatic ? "Auto" : "fixed", worst);
    // Worst fixed case is the independent mono 1x row, 0.186 dB after its
    // reference was corrected to a true mono capture (2026-09-10). The 0.22 dB
    // guard is a regression bound, not a 0.2 dB parity certification.
    require(worst < (automatic ? .4 : .22), automatic
        ? "BUS Auto predicts independent asymmetric bass transients within 0.4 dB"
        : "BUS fixed release predicts independent asymmetric bass transients within 0.22 dB");
}
void lifecycle(bool prepareAgain)
{
    float worst = 0, freshPeak = 0, chargedPeak = 0;
    for (int channels : {1, 2})
    {
        DSP used, fresh; configure(used, 48000, 2, 3, 2, 15); configure(fresh, 48000, 2, 3, 2, 0);
        // Uncompressed DC energizes coupling history without depending on a
        // retained compressor envelope; the post-reset probe is below threshold.
        const auto charged = render(used, constant(1440, .4f), channels);
        for (float value : charged[0]) chargedPeak = std::max(chargedPeak, std::abs(value));
        used.setParameter(P::BusThreshold, 0);
        if (prepareAgain) { used.prepare(48000, 256); used.prepare(48000, 256); }
        else used.reset();
        Audio input = constant(4800, 0);
        for (size_t i = 0; i < input[0].size(); ++i)
            input[0][i] = input[1][i] = .00316227766f * float(std::sin(2 * pi * 997 * i / 48000));
        const auto a = render(used, input, channels), b = render(fresh, input, channels);
        for (int ch = 0; ch < channels; ++ch)
            for (size_t i = 0; i < input[ch].size(); ++i)
            {
                worst = std::max(worst, std::abs(a[ch][i] - b[ch][i]));
                freshPeak = std::max(freshPeak, std::abs(b[ch][i]));
            }
    }
    std::printf("BUS coupling %s: charged peak %.9f, fresh peak %.9f, difference %.9f\n",
        prepareAgain ? "prepare twice" : "reset", chargedPeak, freshPeak, worst);
    require(chargedPeak > .2f && freshPeak > .001f && worst < 1e-7f,
        "BUS lifecycle clears energized feedback coupling history in mono and stereo");
}
void externalHandoff()
{
    // Compatibility witness after native external-sidechain calibration.
    // Keep internal feedback coupling advancing while the external source owns
    // the gain cell, so switching back does not discard its charged history.
    DSP external; configure(external, 48000, 2, 3, 4, 15);
    external.setExternalSidechain(true);
    render(external, constant(1440, .2f), 2, true);
    external.setExternalSidechain(false);
    external.setParameter(P::BusThreshold, 0); external.setParameter(P::BusRelease, 2);
    const auto output = render(external, source(48000, 2));
    constexpr int points[] = {32,40,64,96,128,160,192,224,256,320,384,512,768,1024,1536,2048};
    // Captured after calibration (bus-completion-20260909/); re-captured 2026-09-11
    // after the release-dependent charge offset (docs/multi-comp-2-bus-recal2-2026-09-11.md).
    // Re-captured for the complete BUS detector/law model (2026-09-11).
    constexpr double reference[2][16] = {
        {-0.0893694460392,-0.0834169387817,-0.0744125768542,-0.0672984868288,-0.0634469091892,-0.0621398203075,-0.0628421381116,-0.0648157224059,-0.0672592371702,-0.0711472928524,-0.0715723410249,-0.0607239566743,-0.0507512800395,-0.0552682578564,-0.0506264939904,-0.0466336607933},
        {-0.0812952145934,-0.0767870768905,-0.0713550746441,-0.0683850497007,-0.0680608972907,-0.0692847445607,-0.0711847394705,-0.0729508250952,-0.0740072578192,-0.0732129737735,-0.06886087358,-0.0551139861345,-0.056271199137,-0.0491328537464,-0.044148106128,-0.0400252826512}
    };    double worst = 0, peak = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 16; ++i)
        {
            const double value = output[ch][size_t(points[i])];
            peak = std::max(peak, std::abs(value));
            const double error = std::isfinite(value) ? std::abs(value-reference[ch][i])
                : std::numeric_limits<double>::infinity();
            worst = std::max(worst, error);
        }
    std::printf("BUS external coupling handoff: sampled peak %.9f, difference %.9f\n", peak, worst);
    require(peak > .01 && worst < 1e-7,
        "external detection preserves captured coupling history for internal reentry");
}
}
int main(int argc, char** argv)
{
    if (argc == 2)
    {
        if (std::strcmp(argv[1], "--fixed") == 0) { holdouts(false); return 0; }
        if (std::strcmp(argv[1], "--auto") == 0) { holdouts(true); return 0; }
        if (std::strcmp(argv[1], "--reset") == 0) { lifecycle(false); return 0; }
        if (std::strcmp(argv[1], "--prepare") == 0) { lifecycle(true); return 0; }
        if (std::strcmp(argv[1], "--external") == 0) { externalHandoff(); return 0; }
        return 2;
    }
    holdouts(false); holdouts(true); lifecycle(false); lifecycle(true); externalHandoff();
    std::puts("Multi-Comp BUS programme: PASS");
}
