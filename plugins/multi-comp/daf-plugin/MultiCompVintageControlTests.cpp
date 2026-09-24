#include "MultiCompOutputVu.hpp"
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
    const Test tests[]{{"response", response}, {"lifecycle", lifecycle}, {"calibration", calibration},
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
