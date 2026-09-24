// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../MultiCompDSP.hpp"
#include "MultiCompBusFrequencyFixtures.hpp"
#include "MultiCompBusKneeFixtures.hpp"
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
using Modes = duskaudio::MultiCompModes;
constexpr auto bus = duskaudio::MultiCompMode::Bus;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void configure(DSP& dsp, int rate, int os, int ratio = 1, int attack = 0,
               int release = 0, float threshold = 0)
{
    dsp.setMode(3); dsp.setOversampling(os);
    dsp.setParameter(P::NoiseEnable, 0); dsp.setParameter(P::AutoMakeup, 0);
    dsp.setParameter(P::TruePeakEnable, 0); dsp.setParameter(P::BusRatio, float(ratio));
    dsp.setParameter(P::BusAttack, float(attack)); dsp.setParameter(P::BusRelease, float(release));
    dsp.setParameter(P::BusThreshold, threshold); dsp.setParameter(P::BusMakeup, 0);
    dsp.setParameter(P::SidechainHP, 0); dsp.prepare(rate, 256);
}
std::vector<float> render(DSP& dsp, const std::vector<float>& input, int channels)
{
    std::vector<float> output(input.size()); std::array<float, 256> right{};
    for (size_t pos = 0; pos < input.size(); pos += right.size())
    {
        const int count = int(std::min(right.size(), input.size() - pos));
        const float* in[] = {input.data() + pos, input.data() + pos};
        float* out[] = {output.data() + pos, right.data()};
        dsp.processBlock(in, out, channels, count);
    }
    return output;
}
// Least-squares sine/cosine/DC fit also works for the detuned noninteger cycles.
double amplitude(const std::vector<float>& input, int start, int count,
                 double frequency, int rate)
{
    double a[3][4]{};
    for (int i = 0; i < count; ++i)
    {
        const double phase = 2 * pi * frequency * i / rate;
        const double basis[] = {std::sin(phase), std::cos(phase), 1};
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c) a[r][c] += basis[r] * basis[c];
            a[r][3] += basis[r] * input[size_t(start + i)];
        }
    }
    for (int col = 0; col < 3; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        for (int c = col; c < 4; ++c) std::swap(a[col][c], a[pivot][c]);
        const double divisor = a[col][col];
        for (int c = col; c < 4; ++c) a[col][c] /= divisor;
        for (int r = 0; r < 3; ++r)
            if (r != col)
            {
                const double scale = a[r][col];
                for (int c = col; c < 4; ++c) a[r][c] -= scale * a[col][c];
            }
    }
    return std::hypot(a[0][3], a[1][3]);
}
double errorDb(const std::vector<float>& input, const std::vector<float>& output,
               int start, int count, double frequency, int rate, double reference)
{
    const double source = amplitude(input, start, count, frequency, rate);
    const double gain = 20 * std::log10(amplitude(output, start, count, frequency, rate) / source);
    if (source < 0.001 || !std::isfinite(gain)) return std::numeric_limits<double>::infinity();
    return std::abs(gain - reference);
}
void frequencyParity()
{
    double worst = 0;
    for (int os : {0, 1, 2})
      for (int channels : {1, 2})
    {
        double localWorst = 0;
        for (const auto& tone : busfrequency::tones)
        {
            std::vector<float> input(size_t(tone.rate * 3));
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = float(std::pow(10.0, -12.0 / 20)
                    * std::sin(2 * pi * tone.frequency * i / tone.rate + tone.phase));
            DSP dsp; configure(dsp, tone.rate, os);
            const auto output = render(dsp, input, channels);
            localWorst = std::max(localWorst, errorDb(input, output, 2 * tone.rate,
                int(std::lround(0.8 * tone.rate)), tone.frequency, tone.rate, tone.referenceDb));
        }
        std::printf("BUS frequency os %d %dch: worst %.6f dB\n", os, channels, localWorst);
        worst = std::max(worst, localWorst);
    }
    require(worst < 0.4, "BUS native-clock phase and detune response matches UAD across rates and oversampling");
}
void frequencyHoldouts()
{
    constexpr int rate = 48000;
    constexpr double frequencies[] = {3750, 6000, 8000, 12000, 15000, 17321};
    constexpr double phases[] = {0.19, 0.37, pi / 12, pi / 4, 0.47, 0.71};
    std::vector<float> input(36 * rate);
    for (int segment = 0; segment < 12; ++segment)
        for (int i = 0; i < 3 * rate; ++i)
            input[size_t(segment * 3 * rate + i)] = float(std::pow(10.0, (segment < 6 ? -18.0 : -9.0) / 20)
                * std::sin(2 * pi * frequencies[segment % 6] * i / rate + phases[segment % 6]));
    double worst = 0;
    for (const auto& fixture : busfrequency::holdouts)
      for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, rate, 1, fixture.ratio, fixture.attack, fixture.release, fixture.threshold);
        const auto output = render(dsp, input, channels);
        double localWorst = 0;
        for (int segment = 0; segment < 12; ++segment)
            localWorst = std::max(localWorst, errorDb(input, output, (segment * 3 + 2) * rate,
                38400, frequencies[segment % 6], rate, fixture.gain[segment]));
        worst = std::max(worst, localWorst);
        std::printf("BUS frequency holdout ratio %d attack %d release %d %dch: worst %.6f dB\n",
                    fixture.ratio, fixture.attack, fixture.release, channels, localWorst);
    }
    require(worst < 0.5, "BUS independent frequency/level/ratio holdouts stay within 0.5 dB");
}
void kneeParity()
{
    constexpr int rate = 48000;
    constexpr int segmentSamples = rate / 2;
    // A half-second ascending step reproduces the three-second capture's
    // settled output within 0.000003 dB in the baseline control experiment.
    std::vector<float> input(std::size(busknee::denseOutputDb) * segmentSamples);
    for (size_t segment = 0; segment < std::size(busknee::denseOutputDb); ++segment)
        for (int i = 0; i < segmentSamples; ++i)
            input[segment * segmentSamples + size_t(i)] = float(
                std::pow(10.0, (-48.0 + segment) / 20)
                * std::sin(2 * pi * 1000 * i / rate));
    double worst = 0;
    for (int os : {0, 1, 2})
      for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, rate, os, 0, 0, 0, -5);
        const auto output = render(dsp, input, channels);
        double localWorst = 0;
        for (size_t segment = 0; segment < std::size(busknee::denseOutputDb); ++segment)
        {
            double energy = 0;
            for (int i = 19200; i < 23040; ++i)
            {
                const double sample = output[segment * segmentSamples + size_t(i)];
                energy += sample * sample;
            }
            const double db = 10 * std::log10(energy / 3840);
            localWorst = std::max(localWorst, std::isfinite(db)
                ? std::abs(db - busknee::denseOutputDb[segment])
                : std::numeric_limits<double>::infinity());
        }
        worst = std::max(worst, localWorst);
        std::printf("BUS 2:1 independent knee os %d %dch: worst %.6f dB\n", os, channels, localWorst);
    }
    require(worst < 0.15, "BUS 2:1 knee predicts the independent UAD threshold sweep within 0.15 dB");
}
// SC FILTER off->on must not re-enter the detector with stale history.
// renderBusOutput advances busSidechainHighPass only while the control is
// engaged, so while it is off the filter freezes at whatever it held when it
// last ran.  The reference rig leaves the filter engaged through the same
// silence, letting its history decay physically to zero; the candidate rig
// switches the filter off across that silence and back on before the probe.
// Any difference is history the detector should not have seen.
void sidechainFilterReengage(int channels, float frequency)
{
    const int rate = 48000;
    const auto tone = [rate](double seconds, double hz, double amplitude) {
        std::vector<float> signal(size_t(rate * seconds));
        for (size_t i = 0; i < signal.size(); ++i)
            signal[i] = float(amplitude * std::sin(2 * pi * hz * double(i) / rate));
        return signal;
    };
    // Loud and well below the high-pass corner, so it charges the filter hard.
    const auto charge = tone(1.0, 40.0, 0.9);
    const std::vector<float> silence(size_t(rate) * 5, 0.0f);
    const auto probe = tone(0.5, 1000.0, 0.05);
    DSP candidate, reference;
    for (DSP* dsp : {&candidate, &reference})
    {
        configure(*dsp, rate, 1, 2, 0, 0, -20);
        dsp->setParameter(P::SidechainHP, frequency);
        render(*dsp, charge, channels);
    }
    candidate.setParameter(P::SidechainHP, 0);          // SC FILTER off
    render(candidate, silence, channels);
    candidate.setParameter(P::SidechainHP, frequency);  // and back on
    render(reference, silence, channels);
    const auto a = render(candidate, probe, channels);
    const auto b = render(reference, probe, channels);
    double worst = 0, peak = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        worst = std::max(worst, std::abs(double(a[i]) - double(b[i])));
        peak = std::max(peak, std::abs(double(b[i])));
    }
    std::printf("BUS SC filter %.0f Hz off->on %dch: reference peak %.9f, sample difference %.9f\n",
                double(frequency), channels, peak, worst);
    require(peak > 1.0e-3 && worst < 1.0e-9,
            "re-engaging the BUS SC filter clears its stale high-pass history");
}
void phaseLifecycle(bool rateChange)
{
    duskaudio::MultiCompParameterState params;
    params.busRatio.store(1); params.busAttack.store(0); params.busRelease.store(0);
    params.busThreshold.store(0); params.noiseEnable.store(false);
    double worst = 0; float peak = 0;
    for (int target : {1, 2, 4})
    {
        const int initial = rateChange ? (target == 4 ? 2 : 4) : target;
        Modes used, fresh; used.prepare(48000, 256, initial); fresh.prepare(48000, 256, initial);
        // Match control history first: even silence advances the fixed-release
        // bias. The second sample is a skipped detector phase at both initial
        // 2x and 4x rates, so it changes only the used clock before setRate.
        fresh.process(bus, 0, 0, 0, params);
        used.process(bus, 0, 0, 0, params);
        used.process(bus, 0, 0, 0, params);
        if (rateChange) { used.setRate(48000, target); fresh.setRate(48000, target); }
        else { used.reset(); fresh.reset(); }
        for (int i = 0; i < 2048; ++i)
        {
            const float input = 0.25f * float(std::sin(2 * pi * 8000 * i / (48000 * target) + 0.21));
            const float a = used.process(bus, input, 0, 0, params);
            const float b = fresh.process(bus, input, 0, 0, params);
            worst = std::max(worst, double(std::abs(a - b))); peak = std::max(peak, std::abs(b));
        }
    }
    std::printf("BUS detector phase %s: peak %.9f, sample difference %.9f\n",
                rateChange ? "rate change" : "reset", peak, worst);
    require(peak > 0.01f && worst < 1.0e-7, rateChange
        ? "BUS oversampling changes realign the detector clock"
        : "BUS reset clears the detector clock phase");
}
}
int main(int argc, char** argv)
{
    if (argc == 2)
    {
        if (std::strcmp(argv[1], "--frequency") == 0) { frequencyParity(); return 0; }
        if (std::strcmp(argv[1], "--holdouts") == 0) { frequencyHoldouts(); return 0; }
        if (std::strcmp(argv[1], "--knee") == 0) { kneeParity(); return 0; }
        if (std::strcmp(argv[1], "--reengage") == 0)
        { sidechainFilterReengage(1, 200.0f); sidechainFilterReengage(2, 200.0f); sidechainFilterReengage(2, 80.0f); return 0; }
        if (std::strcmp(argv[1], "--reset") == 0) { phaseLifecycle(false); return 0; }
        if (std::strcmp(argv[1], "--rate") == 0) { phaseLifecycle(true); return 0; }
        return 2;
    }
    frequencyParity(); frequencyHoldouts(); kneeParity();
    sidechainFilterReengage(1, 200.0f); sidechainFilterReengage(2, 200.0f);
    sidechainFilterReengage(2, 80.0f);
    phaseLifecycle(false); phaseLifecycle(true);
    std::puts("Multi-Comp BUS frequency: PASS");
}
