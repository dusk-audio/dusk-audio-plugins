#include "MultiCompOutputVu.hpp"
#include "MultiCompGainReductionNeedle.hpp"
#define MULTICOMP_UI_LOGIC_TEST
#include "MultiCompUI.cpp"
#undef MULTICOMP_UI_LOGIC_TEST
#include <cstdio>
#include <string>
#include <vector>

namespace
{
using namespace multicompp;
constexpr int opto = static_cast<int>(ParamId::OptoMeter);
constexpr int fet = static_cast<int>(ParamId::FetMeter);
constexpr int bypass = static_cast<int>(ParamId::Bypass);

StateValues defaults()
{
    StateValues values{};
    for (int i = 0; i < kTotalParamCount; i = nextControlParameter(i))
        values[static_cast<size_t>(i)] = resolveParameter(i,
            [](const Param& p) { return hostDefault(p); },
            [](const BandParam& p, int) { return hostDefault(p); });
    return values;
}

void feed(OutputVu& meter, const std::vector<float>& left, const std::vector<float>& right,
          int channels, int block)
{
    for (size_t offset = 0; offset < left.size(); offset += static_cast<size_t>(block))
    {
        const float* data[]{left.data() + offset, right.data() + offset};
        meter.process(data, channels, static_cast<int>(std::min(left.size() - offset, static_cast<size_t>(block))));
    }
}

bool response()
{
    bool ok = true;
    for (int rate : {44100, 48000, 96000})
    {
        std::vector<float> signal(static_cast<size_t>(rate));
        for (int i = 0; i < rate; ++i)
            signal[static_cast<size_t>(i)] = static_cast<float>(0.2 * std::sin(2 * 3.141592653589793 * 1000 * i / rate));
        float unitBlock = 0;
        for (int block : {1, 64, 257, 4096})
        {
            OutputVu meter; meter.prepare(rate);
            feed(meter, signal, signal, 2, block);
            const float level = meter.levelDb();
            if (block == 1) unitBlock = level;
            std::printf("VU rate=%d block=%d RMS=%.7f expected=-16.9897000 partitionDelta=%.9f\n",
                        rate, block, level, level - unitBlock);
            ok &= std::abs(level + 16.9897f) < 0.025f && std::abs(level - unitBlock) < 0.000001f;
        }
        OutputVu step; step.prepare(rate);
        std::vector<float> dc(static_cast<size_t>(rate * 3 / 10), 0.2f);
        feed(step, dc, dc, 2, 257);
        const float fraction = std::pow(10.0f, step.levelDb() * 0.05f) / (0.2f * 1.110720735f);
        std::printf("VU 300ms step rate=%d fraction=%.7f\n", rate, fraction);
        ok &= fraction > 0.985f && fraction < 0.995f;
    }
    return ok;
}

bool lifecycle()
{
    OutputVu meter; meter.prepare(48000);
    std::vector<float> silent(48000, 0.0f), loud(48000, 0.25f), opposite(48000, -0.25f);
    feed(meter, silent, loud, 2, 256);
    const float right = meter.levelDb();
    meter.process(nullptr, 2, 0);
    bool ok = right > -12 && meter.levelDb() == right;
    // A one-element pointer list makes an inactive-right-channel read invalid.
    const float* mono[]{silent.data()};
    meter.process(mono, 1, 256);
    const float monoSilence = meter.levelDb();
    const float* stereoSilence[]{silent.data(), silent.data()};
    meter.process(stereoSilence, 2, 1);
    const float restoredStereo = meter.levelDb();
    ok &= monoSilence == -120.0f && restoredStereo == -120.0f;
    feed(meter, loud, silent, 2, 256); const float left = meter.levelDb();
    meter.reset(); const float reset = meter.levelDb();
    meter.process(stereoSilence, 2, 1); const float afterReset = meter.levelDb();
    feed(meter, loud, opposite, 2, 256); const float antiphase = meter.levelDb();
    meter.prepare(96000); const float prepared = meter.levelDb();
    meter.prepare(96000); const float preparedTwice = meter.levelDb();
    feed(meter, silent, silent, 2, 256); const float afterSilence = meter.levelDb();
    const float invalid[]{NAN, INFINITY, -INFINITY, 0};
    const float* bad[]{invalid, invalid}; meter.process(bad, 2, 4);
    const float afterInvalid = meter.levelDb();
    // Two 48k buffers give a full second at the new 96k rate.
    feed(meter, loud, silent, 2, 256);
    feed(meter, loud, silent, 2, 256); const float recovered = meter.levelDb();
    ok &= std::abs(left - right) < 0.00001f && std::abs(antiphase - left) < 0.00001f
        && reset == -120 && afterReset == -120 && prepared == -120 && preparedTwice == -120
        && afterSilence == -120 && afterInvalid == -120 && std::abs(recovered - left) < 0.001f;
    std::printf("VU lifecycle right=%.6f left=%.6f antiphase=%.6f mono=%.1f stereo-restored=%.1f reset=%.1f reset-sample=%.1f prepare=%.1f twice=%.1f recovered=%.6f\n",
                right, left, antiphase, monoSilence, restoredStereo, reset, afterReset, prepared, preparedTwice, recovered);
    return ok;
}

// A needle's step response: 99% of its travel at the ballistics' time, 1.25%
// overshoot, at every sample rate and block size. The Opto and FET needle is the
// IEC 60268-17 VU movement (300 ms, linear in voltage); the VCA one reaches 99% in
// 140 ms and is linear in decibels, like its dial. The steps stay above the VCA
// law's knee, so they test the movement alone.
bool needleStep(const NeedleBallistics& b, const char* name, double expectedMs)
{
    bool ok = true;
    // Position on the movement's own axis: linear gain, or decibels.
    const auto axis = [&b](double db) { return b.linearInVoltage ? std::pow(10.0, db * 0.05) : db; };
    const auto travel = [&](const GainReductionNeedle& n, double from, double to) {
        return (axis(n.levelDb()) - axis(from)) / (axis(to) - axis(from));
    };
    float reference = 0;
    for (int rate : {44100, 48000, 96000, 192000})
    {
        GainReductionNeedle n{b}; n.prepare(rate);
        int first99 = -1; double peak = 0;
        for (int i = 1; i <= rate; ++i)   // one-sample blocks: the finest timing
        {
            n.process(-3.5f, 1);
            const double f = travel(n, 0.0, -3.5);
            if (first99 < 0 && f >= 0.99) first99 = i;
            peak = std::max(peak, f);
        }
        const double ms = 1000.0 * first99 / rate;
        int back99 = -1; double backPeakDb = 0;
        for (int i = 1; i <= rate; ++i)   // and back to rest
        {
            n.process(0.0f, 1);
            if (back99 < 0 && travel(n, -3.5, 0.0) >= 0.99) back99 = i;
            backPeakDb = std::max(backPeakDb, static_cast<double>(n.levelDb()));
        }
        const double backMs = 1000.0 * back99 / rate;
        float unitBlock = 0, worstPartition = 0;
        for (int block : {1, 64, 257, 4096})
        {
            GainReductionNeedle p{b}; p.prepare(rate);
            const int total = static_cast<int>(std::lround(rate * expectedMs * 0.001));
            for (int done = 0; done < total; done += block) p.process(-3.5f, std::min(block, total - done));
            if (block == 1) unitBlock = p.levelDb();
            worstPartition = std::max(worstPartition, std::abs(p.levelDb() - unitBlock));
        }
        if (rate == 44100) reference = unitBlock;
        std::printf("%s needle rate=%d 99%%=%.3f ms overshoot=%.4f%% return 99%%=%.3f ms return peak=%+.4f dB "
                    "at %.0f ms=%.6f dB partitionDelta=%.2g rateDelta=%.2g\n",
                    name, rate, ms, 100 * (peak - 1), backMs, backPeakDb, expectedMs, unitBlock, worstPartition,
                    unitBlock - reference);
        ok &= std::abs(ms - expectedMs) < 0.5 && peak > 1.010 && peak < 1.015
            && std::abs(backMs - expectedMs) < 0.5 && backPeakDb > 0
            && backPeakDb < (b.linearInVoltage ? 0.04 : 0.05)   // 1.25% of the 3.5 dB return: 0.036 / 0.044 dB
            && worstPartition < 1.0e-5f && std::abs(unitBlock - reference) < 1.0e-4f;
    }
    return ok;
}

bool needle()
{
    bool ok = needleStep(kVuNeedle, "VU", 300.0) && needleStep(kVcaNeedle, "VCA", 140.0);
    // The VCA dial's static law, settled: 1:1 to the knee, 0.817 dB per dB beyond (-20 dB reads -17.05).
    for (const auto& [gr, reads] : std::array<std::pair<float, float>, 3>{{{-3.0f, -3.0f}, {-20.0f, -17.0537f},
                                                                          {-30.0f, -25.2237f}}})
    {
        GainReductionNeedle settled{kVcaNeedle}; settled.prepare(48000);
        for (int i = 0; i < 20; ++i) settled.process(gr, 4800);
        std::printf("VCA needle law: %.1f dB settles at %.4f (expected %.4f)\n", gr, settled.levelDb(), reads);
        ok &= std::abs(settled.levelDb() - reads) < 1.0e-3f;
    }
    // The law alone (the UI's fallback without a plugin instance uses it on the raw reading).
    ok &= dialReading(kVcaNeedle, -3.0) == -3.0 && std::abs(dialReading(kVcaNeedle, -20.0) + 17.0537) < 1.0e-9
        && dialReading(kVcaNeedle, NAN) == 0.0 && dialReading(kVuNeedle, -20.0) == -20.0;
    // The VU needle has no law: it settles on the gain reduction itself.
    GainReductionNeedle unity{kVuNeedle}; unity.prepare(48000);
    for (int i = 0; i < 40; ++i) unity.process(-20.0f, 4800);
    ok &= std::abs(unity.levelDb() + 20.0f) < 1.0e-3f;
    // A kick's 10 ms of 6 dB gain reduction (15 blocks of 32 at 48 kHz) barely
    // moves the VU needle: -0.2492 dB, where a needle drawn from the cell reads 6 dB.
    GainReductionNeedle kick{kVuNeedle}; kick.prepare(48000);
    float deepest = 0;
    for (int beat = 0; beat < 4; ++beat)
        for (int block = 0; block < 24000 / 32; ++block)
        {
            kick.process(block < 15 ? -6.0f : 0.0f, 32);
            deepest = std::min(deepest, kick.levelDb());
        }
    // Lifecycle: empty blocks change nothing; a non-finite reading is no gain
    // reduction; reset() and prepare() put the needle at rest at once.
    for (const auto* b : {&kVuNeedle, &kVcaNeedle})
    {
        GainReductionNeedle life{*b}; life.prepare(48000);
        const float fresh = life.levelDb();
        life.process(-6.0f, 4800); const float moved = life.levelDb();
        life.process(-6.0f, 0); life.process(-6.0f, -5);
        const float afterEmpty = life.levelDb();
        life.reset(); const float reset = life.levelDb();
        life.process(NAN, 4800); life.process(INFINITY, 4800); life.process(-INFINITY, 4800);
        const float afterInvalid = life.levelDb();
        life.process(-6.0f, 4800); life.prepare(96000); const float prepared = life.levelDb();
        ok &= fresh == 0 && moved < -1 && afterEmpty == moved && reset == 0 && std::isfinite(afterInvalid)
            && std::abs(afterInvalid) < 1.0e-3f && prepared == 0;
        std::printf("%s needle lifecycle fresh=%.1f moved=%.4f empty=%.4f reset=%.1f invalid=%.6f prepare=%.1f\n",
                    b->linearInVoltage ? "VU" : "VCA", fresh, moved, afterEmpty, reset, afterInvalid, prepared);
    }
    ok &= std::abs(deepest + 0.249210f) < 1.0e-4f;
    // A mode switch snaps the new face's needle onto its reading at once; a VU
    // movement stops at zero volts and nothing publishes below -60 dB.
    GainReductionNeedle vu{kVuNeedle}, vca{kVcaNeedle}; vu.prepare(48000); vca.prepare(48000);
    vu.snap(-10.0f); vca.snap(-10.0f);
    const float snappedVu = vu.levelDb(), snappedVca = vca.levelDb();
    float lowest = 0;
    for (int i = 0; i < 96; ++i) { vu.process(-60.0f, 1000); lowest = std::min(lowest, vu.levelDb()); }   // 2 s
    std::printf("needle snap VU=%.4f VCA=%.4f (law -8.8837); -60 dB step lowest published %.3f, settles %.3f\n",
                snappedVu, snappedVca, lowest, vu.levelDb());
    ok &= std::abs(snappedVu + 10.0f) < 1.0e-4f && std::abs(snappedVca + 8.8837f) < 1.0e-4f
        && lowest >= -60.0f && std::abs(vu.levelDb() + 60.0f) < 1.0e-3f;
    std::printf("VU needle kick 10 ms x 6 dB deepest=%.4f dB\n", deepest);
    return ok;
}

bool calibration()
{
    bool ok = true;
    for (const auto& point : std::array<std::array<float, 3>, 4>{{
        {{1, 1, -12}}, {{1, 2, -18}}, {{0, 1, -17.0103f}}, {{0, 2, -21.0103f}}}})
        for (float offset : {-12.0f, -2.0f, 0.0f, 2.0f})
        {
            const float db = ui_detail::vintageMeterDb(point[0] != 0, static_cast<int>(point[1]), -9,
                                                       point[2] + offset);
            ok &= std::abs(db - offset) < 0.00001f;
        }
    ok &= ui_detail::vintageMeterDb(true, 0, -9, 12) == -9
        && ui_detail::vintageMeterDb(false, 0, -3, -120) == -3
        && ui_detail::vintageMeterDb(false, 1, -9, NAN) == -120;
    std::puts("VU calibration: OPTO +4/-18 RMS, +10/-12 RMS; FET +4/-21.0103 RMS, +8/-17.0103 RMS");
    return ok;
}

bool switching()
{
    bool ok = true;
    for (int row = 0; row < 4; ++row)
    {
        auto values = defaults(); values[fet] = 2; values[bypass] = row == 3 ? 0 : 1;
        const auto before = values;
        ui_detail::selectFetMeterButton(row, [&](uint32_t p, float v) { values[p] = v; });
        ok &= values[fet] == (row == 3 ? 2 : row) && values[bypass] == (row == 3 ? 1 : 0)
            && ui_detail::fetMeterButton(values[fet], values[bypass] != 0) == row;
        for (int i = 0; i < kTotalParamCount; ++i)
            if (i != fet && i != bypass) ok &= values[static_cast<size_t>(i)] == before[static_cast<size_t>(i)];
    }
    int writes = 0;
    ui_detail::selectFetMeterButton(-1, [&](uint32_t, float) { ++writes; });
    ui_detail::selectFetMeterButton(4, [&](uint32_t, float) { ++writes; });
    ok &= writes == 0 && ui_detail::vintageMeterChoice(0.6f) == 1 && ui_detail::vintageMeterChoice(1.6f) == 2;
    std::puts("FET switching: GR/+8/+4 enable, OFF bypasses and preserves reference; unrelated controls unchanged");
    return ok;
}

bool stateRoundtrip()
{
    auto values = defaults(); values[opto] = 1; values[fet] = 2;
    values[static_cast<int>(ParamId::BusHeadroom)] = 5;
    values[static_cast<int>(ParamId::BusFadeRate)] = 0.7f;
    values[static_cast<int>(ParamId::BusFade)] = 1;
    StateValues decoded{};
    const auto current = encodeState(values);
    bool ok = opto == 103 && fet == 104 && kMeterMaster == 95 && kExtensionBase == 100
        && decodeState(current, decoded) && decoded == values;
    auto v5 = current.substr(0, current.find(";opto_meter=")); v5.replace(0, 3, "v=5");
    ok &= decodeState(v5, decoded);
    auto expected = values; expected[opto] = 0; expected[fet] = 0;
    ok &= decoded == expected;
    auto v4 = current.substr(0, current.find(";bus_headroom=")); v4.replace(0, 3, "v=4");
    ok &= decodeState(v4, decoded);
    auto oldDefaults = defaults();
    for (int i = kExtensionBase; i < kTotalParamCount; ++i) expected[static_cast<size_t>(i)] = oldDefaults[static_cast<size_t>(i)];
    ok &= decoded == expected;
    for (int id : {opto, fet})
        ok &= resolveParameter(id, [](const Param& p) { return p.integer && p.min == 0 && p.max == 2
            && p.def == 0 && p.core == CoreParameter::None && snapHostValue(p, 1.6f) == 2; },
            [](const BandParam&, int) { return false; });
    std::puts("state: IDs 0..102 stable; v6 exact; v5 retains BUS values; v4 supplies appended defaults; enums snap");
    return ok;
}

bool stateReject()
{
    auto values = defaults(); values[opto] = 2; values[fet] = 1;
    const auto valid = encodeState(values);
    auto v5 = valid.substr(0, valid.find(";opto_meter=")); v5.replace(0, 3, "v=5");
    auto missing = valid.substr(0, valid.find(";fet_meter="));
    auto fractionalValues = values; fractionalValues[fet] = 0.5f;
    const auto fractional = encodeState(fractionalValues);
    auto future = valid; future.replace(0, 3, "v=7");
    bool ok = true;
    for (const auto& bad : {v5 + valid.substr(valid.find(";opto_meter=")), missing, fractional, future,
                            valid + valid.substr(valid.find(";opto_meter=")), valid + ";unknown=3f800000"})
    {
        auto destination = values;
        const bool accepted = decodeState(bad, destination);
        ok &= !accepted && destination == values;
    }
    std::puts("state rejection: mixed-version, missing, fractional, future, duplicate and unknown keys leave destination intact");
    return ok;
}

bool identity()
{
    bool ok = true;
    for (size_t p = 0; p < kFactoryPresets.size(); ++p)
    {
        auto values = defaults(); values[opto] = 1; values[fet] = 2;
        applyFactoryPresetToHostParameters(static_cast<uint32_t>(p), [&](int id, float v) { values[static_cast<size_t>(id)] = v; });
        ok &= values[opto] == 1 && values[fet] == 2;
        for (int id : {opto, fet}) ok &= !ui_detail::selectionOwnsParam(static_cast<int>(p), false, false, id);
    }
    for (int id : {opto, fet})
        ok &= !ui_detail::selectionOwnsParam(-1, true, false, id) && !ui_detail::selectionOwnsParam(-1, false, true, id);
    ok &= ui_detail::selectionOwnsParam(-1, true, false, static_cast<int>(ParamId::OptoGain))
        && ui_detail::selectionOwnsParam(-1, false, true, static_cast<int>(ParamId::FetOutput))
        && coreParamIndex(CoreParameter::None) == -1;
    std::puts("preset identity: display changes preserve names; audio edits still invalidate; factory recall preserves meter selections");
    return ok;
}
}

int main(int argc, char** argv)
{
    struct Test { const char* name; bool (*run)(); };
    const Test tests[]{{"response", response}, {"lifecycle", lifecycle}, {"needle", needle}, {"calibration", calibration},
                       {"switching", switching}, {"state-roundtrip", stateRoundtrip},
                       {"state-reject", stateReject}, {"identity", identity}};
    bool passed = true;
    int selected = 0;
    for (const auto& test : tests)
        if (argc == 1 || std::string(argv[1]) == test.name)
        {
            ++selected;
            const bool result = test.run();
            std::printf("%s: %s\n", result ? "PASS" : "FAIL", test.name);
            passed &= result;
        }
    return passed && selected > 0 ? 0 : 1;
}
