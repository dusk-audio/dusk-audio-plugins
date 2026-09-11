// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../MultiCompDSP.hpp"
#include "../../daf-plugin/MultiCompParams.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;
void require(bool ok, const char* name)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}

struct Rig
{
    DSP dsp;
    double rate;
    int block;
    long long sample = 0;
    explicit Rig(double sr = 48000, int bs = 256, int mode = 3) : rate(sr), block(bs)
    {
        dsp.setMode(mode);
        dsp.setParameter(P::TruePeakEnable, 0);
        dsp.setParameter(P::NoiseEnable, 0);
        dsp.setParameter(P::AutoMakeup, 0);
        dsp.setParameter(P::BusThreshold, 15);
        dsp.setParameter(P::BusMakeup, 0);
        dsp.setParameter(P::BusFadeRate, 1);
        dsp.setOversampling(0);
        dsp.prepare(rate, block);
    }
    double render(double seconds, float db = -30, int channels = 2, double windowSeconds = 0.002, bool external = false)
    {
        const int count = static_cast<int>(std::lround(seconds * rate));
        const float level = std::pow(10.0f, db * 0.05f);
        std::vector<float> input(static_cast<size_t>(block)), left(input.size()), right(input.size());
        const float* ins[] = {input.data(), input.data()};
        float* outs[] = {left.data(), right.data()};
        double power = 0;
        int measured = 0;
        const int window = static_cast<int>(rate * windowSeconds);
        for (int done = 0; done < count;)
        {
            const int n = std::min(block, count - done);
            for (int i = 0; i < n; ++i)
                input[static_cast<size_t>(i)] = level * std::sin(6.283185307179586 * 1000 * (sample + i) / rate);
            if (external) dsp.processBlockExternal(ins, ins, outs, channels, n);
            else dsp.processBlock(ins, outs, channels, n);
            for (int i = 0; i < n; ++i)
                if (done + i >= count - window)
                {
                    power += static_cast<double>(left[static_cast<size_t>(i)]) * left[static_cast<size_t>(i)];
                    ++measured;
                }
            sample += n;
            done += n;
        }
        return std::sqrt(power / std::max(1, measured));
    }
};

void staticParity()
{
    // Native UADx SSL G captures, internal detector, HR16, 1 kHz, 48 kHz.
    // See docs/multi-comp-2-bus-audio-2026-09-08.md for raw evidence/holdouts.
    struct Row { int ratio; float threshold, input, output; };
    constexpr Row rows[] = {
        {0, -15, -6, -34.382262f},
        {0, -15, -12, -36.6425f}, {1, -15, -12, -41.9300f},
        {2, -15, -12, -43.8315f}, {0, -5, -24, -35.5173f},
        {1, -5, -24, -34.3543f}, {2, -5, -24, -33.2673f},
        {0, 15, -12, -16.5533f}, {1, 15, -12, -14.9823f},
        {2, 15, -12, -14.9823f}
    };
    for (const auto& row : rows)
      for (int channels : {1, 2})
      for (int os : {0, 1, 2})
    {
        Rig rig;
        rig.dsp.setOversampling(os);
        rig.dsp.setParameter(P::BusThreshold, row.threshold);
        rig.dsp.setParameter(P::BusRatio, static_cast<float>(row.ratio));
        rig.dsp.setParameter(P::BusAttack, 0);
        rig.dsp.setParameter(P::BusRelease, 0);
        const double output = 20 * std::log10(rig.render(3, row.input, channels, 0.1));
        std::printf("BUS static ratio %d threshold %.0f input %.0f: %.4f dBFS, reference %.4f, error %.4f dB\n",
                    row.ratio, row.threshold, row.input, output, row.output, output - row.output);
        require(std::abs(output - row.output) < 0.6,
                "BUS steady compression matches measured UAD ratio/threshold law");
    }
}

void makeupHeadroomParity()
{
    // Fresh UAD native SSL G capture: 997 Hz, HR4, threshold +15,
    // ratio 10, attack 30 ms, release 1.2 s. Makeup is post-compressor.
    // Evidence: bus-completion-20260909/capture-matrix.json.
    constexpr double expected[2][4] = {
        {0.137697, 0.137697, 0.137697, 0.137697},
        {15.143017, 15.143017, 15.143017, 15.142986}
    };
    double worst = 0;
    for (int makeup : {0, 15})
      for (int channels : {1, 2})
    {
        Rig rig;
        rig.dsp.setOversampling(1);
        rig.dsp.setParameter(P::BusHeadroom, 0);
        rig.dsp.setParameter(P::BusRatio, 2);
        rig.dsp.setParameter(P::BusAttack, 5);
        rig.dsp.setParameter(P::BusRelease, 3);
        rig.dsp.setParameter(P::BusMakeup, static_cast<float>(makeup));
        rig.render(5, -200, channels);
        int segment = 0;
        long long position = 0;
        for (float db : {-30.0f, -18.0f, -6.0f, -0.1f})
        {
            float input[256], output[256], right[256];
            const float* ins[] = {input, input};
            float* outs[] = {output, right};
            double inputPower = 0, outputPower = 0;
            const double level = std::pow(10.0, db / 20.0);
            for (int offset = 0; offset < 240000;)
            {
                const int n = std::min(256, 240000 - offset);
                for (int i = 0; i < n; ++i)
                    input[i] = static_cast<float>(level * std::sin(
                        6.283185307179586 * 997 * (position + i) / 48000.0 + 0.37));
                rig.dsp.processBlock(ins, outs, channels, n);
                for (int i = 0; i < n; ++i)
                {
                    const int sample = offset + i;
                    if (sample >= 144000 && sample < 182400)
                        inputPower += double(input[i]) * input[i];
                    if (sample >= 144027 && sample < 182427)
                        outputPower += double(output[i]) * output[i];
                }
                offset += n;
                position += n;
            }
            const double gain = 10 * std::log10(outputPower / inputPower);
            const double error = std::abs(gain - expected[makeup == 15][segment++]);
            worst = std::isfinite(error) && outputPower > 0 ? std::max(worst, error) : INFINITY;
            std::printf("BUS makeup %d %dch input %.1f: gain %.6f, error %.6f dB\n",
                        makeup, channels, db, gain, error);
        }
    }
    std::printf("BUS makeup headroom worst %.6f dB\n", worst);
    require(worst < 0.02, "BUS makeup preserves measured floating-point output headroom");
}

void fadeControls()
{
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (int channels : {1, 2})
        {
            Rig rig(rate);
            const double unity = rig.render(0.5, -30, channels);
            require(unity > 0.01, "fade control has an audible unity reference");
            rig.dsp.setParameter(P::BusFade, 1);
            const double quarter = rig.render(0.25, -30, channels);
            const double half = rig.render(0.25, -30, channels);
            const double quarterDb = 20 * std::log10(quarter / unity);
            const double halfDb = 20 * std::log10(half / unity);
            std::printf("BUS fade %.0f Hz %dch: quarter %.4f dB, half %.4f dB\n", rate, channels, quarterDb, halfDb);
            require(std::abs(quarterDb + 22.05) < 0.6 && std::abs(halfDb + 53.83) < 0.8,
                    "BUS fade follows the captured UAD quarter/half contour");
            rig.dsp.setParameter(P::BusFade, 0);
            const double reversed = rig.render(0.25, -30, channels);
            const double restored = rig.render(0.25, -30, channels);
            require(std::abs(20 * std::log10(reversed / quarter)) < 0.6
                        && std::abs(20 * std::log10(restored / unity)) < 0.15,
                    "fade reversal retraces the current attenuation and returns to unity");
            rig.dsp.setParameter(P::BusFade, 1);
            rig.render(0.25, -30, channels);
            rig.dsp.setParameter(P::BusFadeRate, 2);
            rig.render(0.5, -30, channels);
            require(rig.dsp.getBusFadePosition() > 0.57f && rig.dsp.getBusFadePosition() < 0.58f,
                    "changing fade rate affects the remaining travel immediately");
            const double silence = rig.render(1.0, -30, channels);
            require(silence < 1.0e-9 && rig.dsp.getBusFadePosition() == 1.0f,
                    "completed fade reaches silence and publishes completion");
            rig.dsp.reset();
            rig.dsp.setParameter(P::BusFade, 0);
            require(rig.dsp.getBusFadePosition() == 0.0f && rig.render(0.1, -30, channels) > 0.01,
                    "reset clears a completed fade and its published state");
        }
}

void fadeParity()
{
    // Independent native UADx captures, including rates excluded from the fit,
    // completed fade-ins, mid-fade reversals, and three sample rates. Times are
    // on the host automation clock; levels use the final 2 ms before each time.
    // Evidence: docs/multi-comp-2-bus-fade-2026-09-08.md.
    struct Row { double sr; float rate; double reverse; double time[3], db[3]; };
    constexpr Row rows[] = {
        {48000, 1, 3, {1.25, 1.5, 3.5}, {-21.92614093, -53.59167239, -28.50425893}},
        {48000, 3.5f, 5.5, {1.875, 2.75, 7.25}, {-18.38198541, -39.79822540, -37.33908859}},
        {48000, 6, 8, {2.5, 4, 11}, {-17.90474916, -37.85633992, -39.50736628}},
        {48000, 33, 35, {9.25, 17.5, 51.5}, {-20.07999820, -46.35570551, -32.78184069}},
        {48000, 60, 62, {16, 31, 92}, {-20.32026705, -46.21127579, -33.02310167}},
        {48000, 5, 3, {2.25, 3.5, 4.5}, {-18.02553215, -21.74171007, -7.25669460}},
        {48000, 12, 5.8, {4, 7, 9.4}, {-19.20885716, -23.10796322, -7.69191345}},
        {48000, 45, 19, {12.25, 23.5, 32.5}, {-20.44397139, -24.60325662, -8.11398136}},
        {44100, 3.5f, -1, {1.875, 2.75, 3.1}, {-18.44075944, -39.84384211, -53.81571709}},
        {96000, 33, -1, {9.25, 17.5, 20.8}, {-20.14768803, -45.59212379, -60.31276700}}
    };
    double worst = 0;
    for (const auto& row : rows)
      for (int channels : {1, 2})
    {
        Rig rig(row.sr);
        rig.dsp.setParameter(P::BusFadeRate, row.rate);
        const double unity = rig.render(1, -30, channels);
        rig.dsp.setParameter(P::BusFade, 1);
        double previous = 1;
        for (int point = 0; point < 3; ++point)
        {
            if (row.reverse > previous && row.reverse <= row.time[point])
            {
                rig.render(row.reverse - previous, -30, channels);
                rig.dsp.setParameter(P::BusFade, 0);
                previous = row.reverse;
            }
            const double level = rig.render(row.time[point] - previous, -30, channels);
            const double measured = 20 * std::log10(level / unity);
            const double error = std::abs(measured - row.db[point]);
            worst = std::isfinite(error) && unity > 0.01 ? std::max(worst, error) : INFINITY;
            std::printf("BUS fade parity %.0f Hz %dch rate %.1f time %.3f: %.6f dB, reference %.6f, error %.6f\n",
                        row.sr, channels, row.rate, row.time[point], measured, row.db[point], error);
            previous = row.time[point];
        }
    }
    std::printf("BUS fade parity worst %.6f dB\n", worst);
    require(worst < 0.6, "BUS fade matches UAD rates, full fade-ins and interrupted reversals");
}

void fadeRateAutomation()
{
    // UAD reference: start 3.5 s, change to 10 s at 1.7, reverse at 3.7,
    // then change to 2 s at 5.0. The rate changes retain the current level.
    constexpr double times[] = {1.5, 2, 3, 4, 5.5, 6};
    constexpr double reference[] = {-10.46040153, -17.01823705, -24.71192275,
                                    -27.85996812, -0.78414808, 0};
    double worst = 0;
    for (float mix : {0.0f, 100.0f})
    {
        Rig rig;
        rig.dsp.setParameter(P::BusMix, mix);
        rig.dsp.setParameter(P::BusFadeRate, 3.5f);
        const double unity = rig.render(1);
        rig.dsp.setParameter(P::BusFade, 1);
        double previous = 1;
        for (int point = 0; point < 6; ++point)
        {
            for (double event : {1.7, 3.7, 5.0})
                if (event > previous && event <= times[point])
                {
                    rig.render(event - previous);
                    if (event == 3.7) rig.dsp.setParameter(P::BusFade, 0);
                    else rig.dsp.setParameter(P::BusFadeRate, event == 1.7 ? 10 : 2);
                    previous = event;
                }
            const double measured = 20 * std::log10(rig.render(times[point] - previous) / unity);
            const double error = std::abs(measured - reference[point]);
            worst = std::isfinite(error) && unity > 0.01 ? std::max(worst, error) : INFINITY;
            std::printf("BUS fade rate automation mix %.0f time %.3f: %.6f dB, error %.6f\n",
                        mix, times[point], measured, error);
            previous = times[point];
        }
    }
    require(worst < 0.15, "BUS fade rate automation retains level and fades both local mix endpoints");
}

void lifecycleControls()
{
    Rig rig;
    rig.render(0.2);
    rig.dsp.setParameter(P::BusFade, 1);
    rig.render(0.25);
    const float position = rig.dsp.getBusFadePosition();
    float sample = 0.01f;
    const float* in[] = {&sample, &sample};
    float* out[] = {&sample, &sample};
    rig.dsp.processBlock(in, out, 2, 0);
    rig.dsp.processBlock(in, out, 0, 1);
    require(position > 0.2f && rig.dsp.getBusFadePosition() == position,
            "empty and zero-channel blocks preserve an active fade");
    rig.dsp.setParameter(P::GlobalSidechainListen, 1);
    const double listen = rig.render(0.2);
    require(listen > 0.01 && rig.dsp.getBusFadePosition() == position,
            "sidechain listen stays audible and pauses active fade travel");
    rig.dsp.setParameter(P::GlobalSidechainListen, 0);
    rig.dsp.setMode(2);
    const double sibling = rig.render(0.2);
    require(sibling > 0.01 && rig.dsp.getBusFadePosition() == position,
            "leaving BUS preserves fade position without fading the sibling mode");
    rig.dsp.setMode(3);
    rig.render(0.1);
    const float resumedTravel = rig.dsp.getBusFadePosition() - position;
    require(resumedTravel > 0.12f && resumedTravel < 0.125f,
            "returning to BUS resumes active fade travel");
    rig.dsp.setParameter(P::Bypass, 1);
    rig.render(0.1);
    const float bypassPosition = rig.dsp.getBusFadePosition();
    const double dry = rig.render(0.2);
    require(bypassPosition > 0.3f && dry > 0.01
                && rig.dsp.getBusFadePosition() == bypassPosition,
            "settled bypass passes audible dry audio and pauses active fade");
    rig.dsp.setParameter(P::BusFade, 0);
    rig.dsp.setParameter(P::Bypass, 0);
    rig.dsp.prepare(48000, 256);
    rig.dsp.prepare(48000, 256);
    require(rig.dsp.getBusFadePosition() == 0 && rig.render(0.2) > 0.01,
            "repeated preparation clears both fade travel and its published state");
    std::printf("BUS lifecycle: active %.6f, bypass %.6f, dry %.8f, listen %.8f\n",
                position, bypassPosition, dry, listen);
}

void headroomControls()
{
    Rig low, high;
    for (auto* rig : {&low, &high})
    {
        rig->dsp.setParameter(P::BusThreshold, -15);
        rig->dsp.setParameter(P::BusRatio, 2);
    }
    low.dsp.setParameter(P::BusHeadroom, 0);
    high.dsp.setParameter(P::BusHeadroom, 6);
    low.render(2, -6);
    high.render(2, -6);
    const float lo = low.dsp.getGainReduction(), hi = high.dsp.getGainReduction();
    std::printf("BUS headroom: HR4 %.4f dB, HR28 %.4f dB GR\n", lo, hi);
    require(hi < lo - 3.0f, "clockwise headroom increases actual BUS compression");
    for (int mode : {0, 1, 2, 4, 5, 6, 7})
    {
    Rig a(48000, 256, mode), b(48000, 256, mode);
    b.dsp.setParameter(P::BusHeadroom, 6);
    b.dsp.setParameter(P::BusFade, 1);
    const double x = a.render(0.5), y = b.render(0.5);
    require(x > 0.001 && x == y, "BUS headroom and fade leave every other mode output unchanged");
    }
}

void stateControls()
{
    using namespace multicompp;
    StateValues saved{};
    for (int i = 0; i < kTotalParamCount; i = nextControlParameter(i))
        saved[static_cast<size_t>(i)] = resolveParameter(i,
            [](const Param& d) { return hostDefault(d); },
            [](const BandParam& d, int) { return hostDefault(d); });
    const auto& rate = kExtensionParams[1];
    require(hostToPlain(rate, 0.25f) == 3.5f && hostToPlain(rate, 0.5f) == 6.0f
                && hostToPlain(rate, 0.75f) == 33.0f
                && std::abs(plainToHost(rate, 24.8f) - 0.674074f) < 0.00001f,
            "fade rate taper matches UAD parameter readbacks in both directions");
    saved[static_cast<size_t>(ParamId::BusHeadroom)] = 6;
    saved[static_cast<size_t>(ParamId::BusFadeRate)] = 0.5f;
    saved[static_cast<size_t>(ParamId::BusFade)] = 1;
    StateValues loaded{};
    const auto state = encodeState(saved);
    require(decodeState(state, loaded) && loaded == saved, "all new BUS controls round-trip in v5 state");
    auto old = state.substr(0, state.find(";bus_headroom="));
    old.replace(0, 3, "v=4");
    require(decodeState(old, loaded) && loaded[100] == 3 && loaded[102] == 0
                && loaded[101] == hostDefault(kExtensionParams[1]),
            "v4 state defaults headroom and fade without losing legacy controls");
    auto missing = state.substr(0, state.find(";bus_headroom="));
    const auto before = loaded;
    require(!decodeState(missing, loaded) && loaded == before,
            "v5 rejects missing BUS controls without a partial state write");
    require(kBandBase == 63 && kMeterMaster == 95 && kMeterBand3 == 99 && kExtensionBase == 100,
            "new controls retain every existing host parameter ID");
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--fade-parity") == 0)
    { fadeParity(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--fade-automation") == 0)
    { fadeRateAutomation(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--fade-controls") == 0)
    { fadeControls(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--headroom-parity") == 0)
    { makeupHeadroomParity(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--static") == 0)
    { staticParity(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--lifecycle") == 0)
    { lifecycleControls(); std::puts("Multi-Comp BUS lifecycle: PASS"); return 0; }
    staticParity();
    makeupHeadroomParity();
    lifecycleControls();
    fadeControls();
    fadeParity();
    fadeRateAutomation();
    headroomControls();
    stateControls();
    std::puts("Multi-Comp BUS controls: PASS");
}
