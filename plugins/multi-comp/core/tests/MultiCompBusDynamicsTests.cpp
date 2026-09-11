// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../MultiCompDSP.hpp"
#include "MultiCompBusAttackFixtures.hpp"
#include "MultiCompBusKneeFixtures.hpp"
#include "MultiCompBusRecoveryFixtures.hpp"
#include "MultiCompBusStorageFixtures.hpp"
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
constexpr int rate = 48000;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void configure(DSP& dsp, int ratio, int attack, int release, float threshold = 0)
{
    dsp.setMode(3);
    dsp.setOversampling(1);
    dsp.setParameter(P::NoiseEnable, 0);
    dsp.setParameter(P::AutoMakeup, 0);
    dsp.setParameter(P::TruePeakEnable, 0);
    dsp.setParameter(P::BusRatio, static_cast<float>(ratio));
    dsp.setParameter(P::BusAttack, static_cast<float>(attack));
    dsp.setParameter(P::BusRelease, static_cast<float>(release));
    dsp.setParameter(P::BusThreshold, threshold);
    dsp.setParameter(P::BusMakeup, 0);
    dsp.setParameter(P::SidechainHP, 0);
    dsp.prepare(rate, 256);
}
std::vector<float> sine(double seconds, float db, double frequency = 1000)
{
    std::vector<float> result(static_cast<size_t>(std::lround(seconds * rate)));
    const float amplitude = std::pow(10.0f, db * 0.05f);
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = amplitude * static_cast<float>(std::sin(2 * pi * frequency * i / rate));
    return result;
}
std::vector<float> render(DSP& dsp, const std::vector<float>& input, int channels = 2)
{
    std::vector<float> output(input.size()), right(256);
    for (size_t pos = 0; pos < input.size(); pos += 256)
    {
        const int n = static_cast<int>(std::min<size_t>(256, input.size() - pos));
        const float* in[] = {input.data() + pos, input.data() + pos};
        float* out[] = {output.data() + pos, right.data()};
        dsp.processBlock(in, out, channels, n);
    }
    return output;
}
// Fit sine/cosine plus constant and linear trend. The nuisance terms remove
// coupling-filter step tails from the low carrier during recovery. This is the
// same extraction used on the native UAD WAVs, after each device's latency.
std::array<double, 2> carrier(const std::vector<float>& audio, int begin,
                              int count = 48, double frequency = 1000,
                              int sampleRate = rate)
{
    double matrix[4][5]{};
    for (int i = 0; i < count; ++i)
    {
        const double phase = 2 * pi * frequency * i / sampleRate;
        const double basis[] = {std::sin(phase), std::cos(phase), 1.0,
                                2.0 * i / (count - 1) - 1.0};
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c) matrix[r][c] += basis[r] * basis[c];
            matrix[r][4] += basis[r] * audio[static_cast<size_t>(begin + i)];
        }
    }
    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::abs(matrix[r][col]) > std::abs(matrix[pivot][col])) pivot = r;
        for (int c = col; c < 5; ++c) std::swap(matrix[col][c], matrix[pivot][c]);
        const double divisor = matrix[col][col];
        for (int c = col; c < 5; ++c) matrix[col][c] /= divisor;
        for (int r = 0; r < 4; ++r)
            if (r != col)
            {
                const double factor = matrix[r][col];
                for (int c = col; c < 5; ++c) matrix[r][c] -= factor * matrix[col][c];
            }
    }
    return {std::hypot(matrix[0][4], matrix[1][4]), std::atan2(matrix[1][4], matrix[0][4])};
}

void timing(bool onlyAuto = false)
{
    // Independent UADx captures at ratio 4, threshold 0, HR16, attack 1 ms.
    // Two bursts (150 ms / 2 s) expose the different Auto-release memories.
    auto input = sine(10.15, -54);
    for (size_t i = 0; i < input.size(); ++i)
        if ((i >= 96000 && i < 103200) || (i >= 199200 && i < 295200))
            input[i] = std::pow(10.0f, -12.0f * 0.05f)
                     * static_cast<float>(std::sin(2 * pi * 1000 * i / rate));
    constexpr double times[] = {2.002, 2.03, 2.16, 2.25, 2.45, 6.16, 6.25, 6.45};
    constexpr double reference[5][8] = {
        {-9.48242502, -10.94820357, -9.61831584, -3.11348729, -0.23159271, -9.61798560, -3.11338660, -0.23158619},
        {-9.51509744, -11.08765121, -10.14691300, -4.68941260, -0.81937825, -10.14660090, -4.68925700, -0.81935372},
        {-9.54817204, -11.27251060, -10.76266207, -7.18941732, -2.91102930, -10.76239109, -7.18926831, -2.91094554},
        {-9.56261569, -11.39715516, -11.11726710, -8.91559466, -5.44345809, -11.11699669, -8.91541701, -5.44333422},
        {-9.52759791, -10.71844315, -8.97292564, -3.21035119, -2.35401691, -10.88505330, -9.42381973, -8.82309611},
    };
    for (int release = onlyAuto ? 4 : 0; release < 5; ++release)
        for (int channels : {1, 2})
        {
            DSP dsp; configure(dsp, 1, 2, release);
            // Match the native capture history now that silent Auto builds bias.
            if (release == 4) render(dsp, std::vector<float>(5 * rate), channels);
            const auto output = render(dsp, input, channels);
            double worst = 0;
            for (size_t i = 0; i < std::size(times); ++i)
            {
                const auto measured = carrier(output, static_cast<int>(std::lround(times[i] * rate))
                                                       + dsp.getLatencySamples());
                const float db = times[i] < 2.15 ? -12.0f : -54.0f;
                const double gain = 20 * std::log10(measured[0] / std::pow(10.0, db * 0.05));
                if (!std::isfinite(gain)) { worst = std::numeric_limits<double>::infinity(); break; }
                worst = std::max(worst, std::abs(gain - reference[release][i]));
            }
            std::printf("BUS timing release %d, %dch: worst %.6f dB\n", release, channels, worst);
            require(worst < 0.6, "BUS attack and fixed/Auto recovery match measured UAD traces");
        }
}
void lowFrequency(bool neutral)
{
    DSP dsp; configure(dsp, neutral ? 2 : 1, 0, 0, neutral ? 15.0f : 0.0f);
    const auto output = render(dsp, sine(3.2, -12, 30));
    const auto measured = carrier(output, 3 * rate + dsp.getLatencySamples(), 4800, 30);
    const double gain = 20 * std::log10(measured[0] / std::pow(10.0, -12 * 0.05));
    const double phase = measured[1] * 180 / pi;
    std::printf("BUS 30 Hz %s: gain %.6f dB, phase %.6f degrees\n", neutral ? "neutral" : "compressed", gain, phase);
    if (neutral)
        require(std::abs(gain - 0.004) < 0.08 && std::abs(phase - 6.32) < 0.3,
                "BUS coupling poles match the measured neutral bass gain and phase");
    else
        require(std::abs(gain + 11.014565) < 0.4,
                "SC Filter Off preserves measured bass-triggered BUS compression");
}

template<size_t N>
double fixtureError(const DSP& dsp, const std::vector<float>& input,
                    const std::vector<float>& output,
                    const std::array<int, N>& timesMs, const std::array<double, N>& reference,
                    double frequency = 1000)
{
    double worst = 0;
    for (size_t point = 0; point < N; ++point)
    {
        const int sample = timesMs[point] * rate / 1000;
        const double source = carrier(input, sample, 48, frequency)[0];
        const double result = carrier(output, sample + dsp.getLatencySamples(), 48, frequency)[0];
        const double error = std::abs(20 * std::log10(result / source) - reference[point]);
        if (source < 0.0001 || !std::isfinite(error)) return std::numeric_limits<double>::infinity();
        worst = std::max(worst, error);
    }
    return worst;
}

void attackParity(bool shiftedPhase = false)
{
    auto input = sine(10.15, -54);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const bool loud = (i >= 96000 && i < 103200) || (i >= 199200 && i < 295200);
        const float amplitude = std::pow(10.0f, (loud ? -12.0f : -54.0f) * 0.05f);
        input[i] = amplitude * static_cast<float>(std::sin(2 * pi * 1000 * i / rate
                                                          + (shiftedPhase ? pi / 2 : 0)));
    }
    const auto* rows = shiftedPhase ? busfixtures::phases : busfixtures::attacks;
    const size_t count = shiftedPhase ? std::size(busfixtures::phases) : std::size(busfixtures::attacks);
    double worst = 0;
    for (size_t row = 0; row < count; ++row)
      for (int channels : {1, 2})
    {
        const auto& fixture = rows[row];
        DSP dsp; configure(dsp, fixture.ratio, fixture.attack, fixture.release);
        const auto output = render(dsp, input, channels);
        const double error = fixtureError(dsp, input, output, busfixtures::attackTimesMs, fixture.gain);
        worst = std::max(worst, error);
        std::printf("BUS %s ratio %d attack %d release %d %dch: worst %.6f dB\n",
                    shiftedPhase ? "phase" : "attack", fixture.ratio, fixture.attack, fixture.release, channels, error);
    }
    std::printf("BUS %s total worst %.6f dB\n", shiftedPhase ? "phase" : "attack", worst);
    require(worst < 0.6, shiftedPhase ? "BUS attacks match independent phase-shifted UAD bursts"
                                     : "BUS attacks and interacting fixed releases match UAD across all ratios");
}

void twoToOneTransientHoldout()
{
    std::vector<float> input(14 * rate);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const double seconds = double(i) / rate;
        const double db = seconds >= 1.2 && seconds < 1.29 ? -21
            : seconds >= 4.3 && seconds < 4.65 ? -12
            : seconds >= 8.0 && seconds < 9.4 ? -6 : -54;
        input[i] = float(std::pow(10.0, db / 20)
            * std::sin(2 * pi * 997 * i / rate + 0.37));
    }
    double worst = 0;
    for (const auto& fixture : busknee::transients)
      for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, 0, fixture.attack, 0, 2.5f);
        const auto output = render(dsp, input, channels);
        const double error = fixtureError(dsp, input, output,
            busknee::transientTimesMs, fixture.gain, 997);
        worst = std::max(worst, error);
        std::printf("BUS 2:1 independent transient attack %d %dch: worst %.6f dB\n",
                    fixture.attack, channels, error);
    }
    require(worst < 0.25, "BUS 2:1 predicts independent UAD burst levels, phase and threshold within 0.25 dB");
}

void levelHoldout()
{
    auto input = sine(12, -60);
    constexpr int starts[] = {72000, 240000, 408000};
    constexpr float levels[] = {-24, -18, -9};
    for (int burst = 0; burst < 3; ++burst)
        for (int i = starts[burst]; i < starts[burst] + 24000; ++i)
            input[static_cast<size_t>(i)] = std::pow(10.0f, levels[burst] * 0.05f)
                * static_cast<float>(std::sin(2 * pi * 1000 * i / rate));
    double worst = 0;
    for (const auto& fixture : busfixtures::levels)
      for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, fixture.ratio, fixture.attack, 0);
        const auto output = render(dsp, input, channels);
        const double error = fixtureError(dsp, input, output, busfixtures::levelTimesMs, fixture.gain);
        worst = std::max(worst, error);
        std::printf("BUS level holdout ratio %d attack %d %dch: worst %.6f dB\n",
                    fixture.ratio, fixture.attack, channels, error);
    }
    std::printf("BUS level holdout total worst %.6f dB\n", worst);
    require(worst < 0.65, "BUS attack model predicts independent UAD burst levels within 0.65 dB");
}

void autoHoldout()
{
    auto input = sine(21.64, -36);
    constexpr int starts[] = {96000, 289920, 510720};
    constexpr int ends[] = {97920, 318720, 750720};
    for (int burst = 0; burst < 3; ++burst)
        for (int i = starts[burst]; i < ends[burst]; ++i)
            input[static_cast<size_t>(i)] = std::pow(10.0f, -12.0f * 0.05f)
                * static_cast<float>(std::sin(2 * pi * 1000 * i / rate));
    bool withinBounds = true;
    for (const auto& fixture : busfixtures::autoHolds)
      for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, fixture.ratio, fixture.attack, 4);
        render(dsp, std::vector<float>(5 * rate), channels);
        const auto output = render(dsp, input, channels);
        const double error = fixtureError(dsp, input, output, busfixtures::autoTimesMs, fixture.gain);
        withinBounds &= error < 0.6;
        std::printf("BUS Auto holdout ratio %d %dch: worst %.6f dB\n", fixture.ratio, channels, error);
    }
    require(withinBounds, "BUS Auto holdouts preserve measured recovery across burst durations and ratios");
}
void autoIndependent()
{
    std::vector<float> input(20 * rate);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const bool first = i >= 96000 && i < 100320;
        const bool second = i >= 288000 && i < 304800;
        const bool third = i >= 528000 && i < 595200;
        const float level = second ? -9.0f : (first || third ? -18.0f : -54.0f);
        input[i] = std::pow(10.0f, level * 0.05f)
            * static_cast<float>(std::sin(2 * pi * 1000 * i / rate + 0.37));
    }
    double worst = 0;
    for (const auto& fixture : busfixtures::autoIndependent)
      for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, fixture.ratio, fixture.attack, 4, fixture.threshold);
        render(dsp, std::vector<float>(5 * rate), channels);
        const auto output = render(dsp, input, channels);
        const double error = fixtureError(dsp, input, output,
                                          busfixtures::autoIndependentTimesMs, fixture.gain);
        worst = std::max(worst, error);
        std::printf("BUS independent Auto ratio %d attack %d threshold %.0f %dch: worst %.6f dB\n",
                    fixture.ratio, fixture.attack, fixture.threshold, channels, error);
    }
    std::printf("BUS independent Auto total worst %.6f dB\n", worst);
    // The coupled control now passes the same 0.6 dB bound as the original
    // recovery set; these independent levels and thresholds must retain it.
    require(worst < 0.6, "BUS independent Auto levels and thresholds remain within 0.6 dB");
}
void autoReset(bool prepareInstead = false)
{
    DSP active, fresh; configure(active, 1, 2, 4); configure(fresh, 1, 2, 4);
    render(active, sine(2, -12));
    render(active, sine(0.5, -36));
    const float retained = active.getGainReduction();
    require(retained < -3, "BUS Auto reset probe has real retained reduction");
    if (prepareInstead) { active.prepare(rate, 256); active.prepare(rate, 256); }
    else active.reset();
    const auto quiet = sine(0.2, -36);
    const auto a = render(active, quiet), b = render(fresh, quiet);
    float error = 0, peak = 0;
    for (size_t i = 0; i < a.size(); ++i)
    { error = std::max(error, std::abs(a[i] - b[i])); peak = std::max(peak, std::abs(b[i])); }
    std::printf("BUS Auto %s: retained %.6f dB, fresh peak %.9f, reset delta %.9f\n",
                prepareInstead ? "prepare twice" : "reset", retained, peak, error);
    require(peak > 0.01f && error < 1.0e-7f, "reset clears the BUS Auto slow reservoir and all coupled histories");
}
void autoReleaseSwitch(bool external)
{
    DSP dsp; configure(dsp, 1, 2, 4);
    render(dsp, sine(2, -12));
    if (external) dsp.setExternalSidechain(true);
    else dsp.setParameter(P::BusRelease, 0);
    const auto quiet = sine(1, -36);
    std::array<float, 256> left{}, right{};
    for (size_t pos = 0; pos < quiet.size(); pos += 256)
    {
        const int n = static_cast<int>(std::min<size_t>(256, quiet.size() - pos));
        const float* in[] = {quiet.data() + pos, quiet.data() + pos};
        float* out[] = {left.data(), right.data()};
        if (external) dsp.processBlockExternal(in, in, out, 2, n);
        else dsp.processBlock(in, out, 2, n);
    }
    dsp.setExternalSidechain(false);
    dsp.setParameter(P::BusRelease, 4);
    const auto audio = render(dsp, sine(0.2, -36));
    const auto measured = carrier(audio, static_cast<int>(0.19 * rate) + dsp.getLatencySamples());
    const double gain = 20 * std::log10(measured[0] / std::pow(10.0, -36 * 0.05));
    std::printf("BUS Auto return from %s: %.6f dB\n", external ? "external detector" : "fixed release", gain);
    if (external)
    {
        // Both detector sources now drive the same Auto reservoirs. Quiet
        // external detection must continue recovery rather than erase it.
        DSP uninterrupted; configure(uninterrupted, 1, 2, 4);
        render(uninterrupted, sine(2, -12));
        render(uninterrupted, quiet);
        const auto control = render(uninterrupted, sine(0.2, -36));
        const double reference = 20 * std::log10(carrier(control,
            static_cast<int>(0.19 * rate) + uninterrupted.getLatencySamples())[0]
            / std::pow(10.0, -36 * 0.05));
        std::printf("BUS continuous Auto reference %.6f, external handoff difference %.6f dB\n", reference, gain-reference);
        require(reference < -3 && std::abs(gain-reference) < 0.03,
            "external detection preserves active Auto recovery across source changes");
    }
    else
        require(std::abs(gain) < 0.1, "inactive BUS Auto reservoir cannot reapply stale reduction");
}

double recoveryGain(const std::vector<float>& input, const std::vector<float>& output,
                    int sample, int latency, int sampleRate)
{
    const int count = static_cast<int>(std::lround(0.004 * sampleRate));
    const double a = carrier(input, sample, count, 997, sampleRate)[0];
    const double b = carrier(output, sample + latency, count, 997, sampleRate)[0];
    if (a < 0.0001 || b <= 0 || !std::isfinite(b))
        return std::numeric_limits<double>::infinity();
    return 20 * std::log10(b / a);
}

void fixedRecovery(bool neutralOnly)
{
    double worst = 0;
    for (const auto& fixture : busrecovery::fixtures)
      for (int channels : {1, 2})
      for (int oversampling : {0, 1, 2})
    {
        // All rates/releases at the normal 2x setting; cover the other
        // detector phases with the longest recovery at 48 kHz as well.
        if (oversampling != 1 && (fixture.rate != rate || fixture.release != 3)) continue;
        DSP dsp; configure(dsp, 2, 4, fixture.release, 2.5f);
        dsp.setOversampling(oversampling);
        dsp.prepare(fixture.rate, 256);
        render(dsp, std::vector<float>(5 * fixture.rate), channels);
        std::vector<float> input(5 * fixture.rate);
        for (size_t i = 0; i < input.size(); ++i)
        {
            const double seconds = double(i) / fixture.rate;
            const double db = seconds >= 1 && seconds < 2.3 ? -9 : -66;
            input[i] = float(std::pow(10.0, db / 20)
                * std::sin(2 * pi * 997 * i / fixture.rate + 0.37));
        }
        const auto output = render(dsp, input, channels);
        double error = 0;
        if (neutralOnly)
            error = std::abs(recoveryGain(input, output, int(0.8 * fixture.rate),
                dsp.getLatencySamples(), fixture.rate) - fixture.neutral);
        else
        {
            double first = 0;
            for (size_t i = 0; i < fixture.gain.size(); ++i)
            {
                const int sample = int(std::lround((2.3 + fixture.secondsAfterBurst[i]) * fixture.rate));
                const double gain = recoveryGain(input, output, sample,
                    dsp.getLatencySamples(), fixture.rate);
                if (i == 0) first = gain;
                // Remove starting depth to expose recovery shape. Absolute
                // compression depth remains guarded by the burst fixtures.
                const double measured = gain - first * fixture.decay[i];
                const double reference = fixture.gain[i] - fixture.gain[0] * fixture.decay[i];
                const double difference = std::abs(measured - reference);
                error = !std::isfinite(difference) ? std::numeric_limits<double>::infinity()
                    : std::max(error, difference);
            }
        }
        worst = std::max(worst, error);
        std::printf("BUS fixed %s %d Hz release %d os %d %dch: %.6f dB\n",
            neutralOnly ? "bias" : "recovery shape", fixture.rate, fixture.release,
            oversampling, channels, error);
    }
    std::printf("BUS fixed %s worst %.6f dB\n", neutralOnly ? "bias" : "recovery shape", worst);
    require(worst < 0.015, neutralOnly
        ? "BUS fixed releases reproduce the independent UAD below-threshold gain bias"
        : "BUS fixed recovery shape matches independent UAD traces within 0.015 dB");
}

void fixedReleaseSwitch()
{
    double worst = 0;
    for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, 2, 4, 3);
        render(dsp, std::vector<float>(5 * rate), channels);
        auto input = sine(7.5, -66, 997);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = float(std::pow(10.0, -66.0 / 20)
                * std::sin(2 * pi * 997 * i / rate + 0.37));
        std::vector<float> output(input.size()), right(256);
        for (size_t pos = 0; pos < input.size(); pos += 256)
        {
            if (pos == 122880) dsp.setParameter(P::BusRelease, 0);
            if (pos == 245760) dsp.setParameter(P::BusRelease, 3);
            const int n = int(std::min<size_t>(256, input.size() - pos));
            const float* in[] = {input.data() + pos, input.data() + pos};
            float* out[] = {output.data() + pos, right.data()};
            dsp.processBlock(in, out, channels, n);
        }
        for (const auto& point : busrecovery::switches)
        {
            const double error = std::abs(recoveryGain(input, output,
                int(std::lround(point.seconds * rate)), dsp.getLatencySamples(), rate) - point.gain);
            worst = std::max(worst, error);
        }
    }
    std::printf("BUS fixed release switching worst %.6f dB\n", worst);
    require(worst < 0.015, "BUS release bias settles at the measured release rate in both directions");
}

void fixedReset(bool prepareInstead)
{
    DSP active, fresh;
    configure(active, 2, 4, 3); configure(fresh, 2, 4, 3);
    render(active, sine(1, -12));
    const float retained = active.getGainReduction();
    if (prepareInstead) { active.prepare(rate, 256); active.prepare(rate, 256); }
    else active.reset();
    const auto input = sine(0.1, -36);
    const auto a = render(active, input), b = render(fresh, input);
    float error = 0, peak = 0;
    for (size_t i = 0; i < a.size(); ++i)
    { error = std::max(error, std::abs(a[i] - b[i])); peak = std::max(peak, std::abs(b[i])); }
    std::printf("BUS fixed %s: retained %.6f dB, fresh peak %.9f, reset delta %.9f\n",
        prepareInstead ? "prepare twice" : "reset", retained, peak, error);
    require(retained < -3 && peak > 0.01f && error < 1.0e-7f,
        "reset and repeated prepare clear active BUS fixed control and its coupled histories");
}

void enterFixed(bool external)
{
    DSP dsp; configure(dsp, 2, 4, 4, -15);
    dsp.setExternalSidechain(external);
    const auto input = sine(1, -6);
    std::array<float, 256> left{}, right{};
    for (size_t pos = 0; pos < input.size(); pos += 256)
    {
        const int n = int(std::min<size_t>(256, input.size() - pos));
        const float* in[] = {input.data() + pos, input.data() + pos};
        float* out[] = {left.data(), right.data()};
        if (external) dsp.processBlockExternal(in, in, out, 2, n);
        else dsp.processBlock(in, out, 2, n);
    }
    const float before = dsp.getGainReduction();
    dsp.setExternalSidechain(false);
    dsp.setParameter(P::BusRelease, 2);
    dsp.setParameter(P::BusThreshold, 15);
    render(dsp, std::vector<float>(48));
    const float after = dsp.getGainReduction();
    std::printf("BUS enter fixed from %s: before %.6f, after %.6f dB\n",
        external ? "external" : "Auto", before, after);
    require(before < -3 && std::abs(after - before) < 0.2f,
        "entering fixed release inherits the active Auto or external control depth");
}

void autoQuiet()
{
    double worst = 0;
    for (const auto& fixture : busstorage::quiet)
    for (int channels : {1, 2})
    for (int os : {0, 1, 2})
    {
        if (os != 1 && (fixture.rate != rate || fixture.ratio != 2 || fixture.warm != 0)) continue;
        DSP dsp; configure(dsp, fixture.ratio, 4, 4); dsp.setOversampling(os);
        dsp.prepare(fixture.rate, 256);
        render(dsp, std::vector<float>(size_t(fixture.warm * fixture.rate)), channels);
        std::vector<float> input(size_t(30 * fixture.rate));
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = float(std::pow(10.0, fixture.level / 20) * std::sin(2*pi*997*i/fixture.rate + .37));
        const auto output = render(dsp, input, channels);
        for (size_t i = 0; i < fixture.gain.size(); ++i)
        {
            const int pos = int(std::lround(busstorage::quietTimes[i] * fixture.rate));
            const int n = int(std::lround(.008 * fixture.rate));
            const double a = carrier(output, pos+dsp.getLatencySamples(), n, 997, fixture.rate)[0];
            const double b = carrier(input, pos, n, 997, fixture.rate)[0];
            const double error = a > 0 && b > 0 && std::isfinite(a)
                ? std::abs(20*std::log10(a/b)-fixture.gain[i]) : std::numeric_limits<double>::infinity();
            worst = std::max(worst, error);
        }
    }
    std::printf("BUS Auto quiet gain worst %.6f dB\n", worst);
    require(worst < 0.011, "Auto quiet gain follows native fast and slow bias at host rate");
}
void autoStorageShape()
{
    double worst = 0;
    double minimumComponent = std::numeric_limits<double>::infinity();
    for (int ratio : {1, 2})
    for (int channels : {1, 2})
    {
        DSP dsp; configure(dsp, ratio, ratio == 1 ? 3 : 5, 4, 2.5f);
        render(dsp, std::vector<float>(5 * rate), channels);
        std::vector<float> input(35 * rate);
        struct Event { double start, duration, db; };
        constexpr Event events[] = {{1,.06,-18},{6.06,.22,-18},{11.28,.9,-18},
                                    {17.18,.06,-9},{22.24,.22,-9},{27.46,.9,-9}};
        for (size_t i = 0; i < input.size(); ++i)
        {
            double db = -66;
            for (const auto& event : events)
                if (i >= size_t(std::lround(event.start*rate)) && i < size_t(std::lround((event.start+event.duration)*rate))) db=event.db;
            input[i] = float(std::pow(10.0, db/20)*std::sin(2*pi*997*i/rate+.61));
        }
        const auto output = render(dsp, input, channels);
        for (const auto& fixture : busstorage::recoveries)
        {
            if (fixture.ratio != ratio) continue;
            double gain[7]{};
            for (int j = 0; j < 7; ++j)
            {
                const int pos = (fixture.endMs + busstorage::recoveryPoints[j])*48;
                const double a = carrier(output, pos+dsp.getLatencySamples(), 48, 997)[0];
                const double b = carrier(input, pos, 48, 997)[0];
                // A dead or invalid render must fail the same shape guard.
                if (!(a > 0 && b > 0 && std::isfinite(a))) { worst=std::numeric_limits<double>::infinity(); continue; }
                gain[j] = 20*std::log10(a/b);
            }
            // Fit both measured poles plus a constant to ALL recovery samples.
            // Late-only extrapolation magnified <0.002 dB capture residuals
            // into a 0.049 dB failure even for an exact two-pole decay.
            double fitted[3]{}, native[3]{};
            for (int component = 0; component < 3; ++component)
                for (int j = 0; j < 7; ++j)
                {
                    const double weight = busstorage::recoveryFitWeights[component][j];
                    fitted[component] += weight * gain[j];
                    native[component] += weight * fixture.gain[size_t(j)];
                }
            // A unity-gain render or a single-pole release also has zero fit
            // residual. Require both measured decay components to be present;
            // independent burst/level gates constrain their absolute depth.
            for (int component = 0; component < 2; ++component)
            {
                const double fraction = fitted[component] / native[component];
                minimumComponent = std::isfinite(fraction)
                    ? std::min(minimumComponent, fraction) : -std::numeric_limits<double>::infinity();
            }
            for (int j = 0; j < 7; ++j)
            {
                const double t = (busstorage::recoveryPoints[j] + 0.5) / 1000.0;
                const double basis[] = {std::exp(-t / 0.04165904), std::exp(-t / 4.1588435), 1.0};
                double residual = gain[j] - fixture.gain[size_t(j)];
                for (int k = 0; k < 3; ++k) residual -= basis[k] * (fitted[k] - native[k]);
                worst = std::max(worst, std::abs(residual));
            }
        }
    }
    std::printf("BUS Auto all-point recovery shape worst %.6f dB, minimum native component %.6f\n",
        worst, minimumComponent);
    require(worst < 0.01, "Auto recovery keeps the measured coupled poles immediately after bursts");
    require(minimumComponent > 0.25, "Auto recovery retains both active native decay components");
}

}
int main(int argc, char** argv)
{
    if (argc == 2)
    {
        if (std::strcmp(argv[1], "--auto-quiet") == 0) { autoQuiet(); return 0; }
        if (std::strcmp(argv[1], "--storage-shape") == 0) { autoStorageShape(); return 0; }
        if (std::strcmp(argv[1], "--attack-parity") == 0) { attackParity(); return 0; }
        if (std::strcmp(argv[1], "--phase-holdout") == 0) { attackParity(true); return 0; }
        if (std::strcmp(argv[1], "--level-holdout") == 0) { levelHoldout(); return 0; }
        if (std::strcmp(argv[1], "--auto-holdout") == 0) { autoHoldout(); return 0; }
        if (std::strcmp(argv[1], "--auto-independent") == 0) { autoIndependent(); return 0; }
        if (std::strcmp(argv[1], "--two-to-one") == 0) { twoToOneTransientHoldout(); return 0; }
        if (std::strcmp(argv[1], "--timing") == 0) { timing(); return 0; }
        if (std::strcmp(argv[1], "--auto") == 0) { timing(true); return 0; }
        if (std::strcmp(argv[1], "--coupling") == 0) { lowFrequency(true); return 0; }
        if (std::strcmp(argv[1], "--detector") == 0) { lowFrequency(false); return 0; }
        if (std::strcmp(argv[1], "--reset") == 0) { autoReset(); return 0; }
        if (std::strcmp(argv[1], "--prepare") == 0) { autoReset(true); return 0; }
        if (std::strcmp(argv[1], "--switch") == 0) { autoReleaseSwitch(false); return 0; }
        if (std::strcmp(argv[1], "--external-switch") == 0) { autoReleaseSwitch(true); return 0; }
        if (std::strcmp(argv[1], "--recovery-bias") == 0) { fixedRecovery(true); return 0; }
        if (std::strcmp(argv[1], "--recovery-shape") == 0) { fixedRecovery(false); return 0; }
        if (std::strcmp(argv[1], "--release-switch") == 0) { fixedReleaseSwitch(); return 0; }
        if (std::strcmp(argv[1], "--fixed-reset") == 0) { fixedReset(false); return 0; }
        if (std::strcmp(argv[1], "--fixed-prepare") == 0) { fixedReset(true); return 0; }
        if (std::strcmp(argv[1], "--fixed-from-auto") == 0) { enterFixed(false); return 0; }
        if (std::strcmp(argv[1], "--fixed-from-external") == 0) { enterFixed(true); return 0; }
        return 2;
    }
    autoQuiet(); autoStorageShape();
    timing(); lowFrequency(true); lowFrequency(false); autoReset(); autoReset(true);
    attackParity(); attackParity(true); levelHoldout(); autoHoldout(); autoIndependent();
    twoToOneTransientHoldout();
    autoReleaseSwitch(false); autoReleaseSwitch(true);
    fixedRecovery(true); fixedRecovery(false); fixedReleaseSwitch();
    fixedReset(false); fixedReset(true); enterFixed(false); enterFixed(true);
    std::puts("Multi-Comp BUS dynamics: PASS");
}
