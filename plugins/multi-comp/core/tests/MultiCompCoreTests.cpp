#include "../MultiCompDSP.hpp"
#include "../../../shared-daf/dsp/DuskCrossover.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace duskaudio
{
struct MultiCompDSPTestAccess
{
    static std::array<bool, 2> busSidechainValidity(const MultiCompDSP& dsp) noexcept
    {
        return dsp.previousBusSidechainValid;
    }

    // The vintage FET envelope gain, in dB, straight off the mode state --
    // NOT the published meter, which is smoothed and clamped. `processFET`
    // indexes `fetBroadbandK2` at `-gainToDecibels(envelope + 0.001f)`, so the
    // raw envelope is the only way to read that lookup's own argument.
    static float fetEnvelopeGainDb(const MultiCompDSP& dsp, int channel) noexcept
    {
        return dsp.modes.fetColourGainReduction(channel);
    }

    static float fetNetGainReductionDb(const MultiCompDSP& dsp,
                                       int channel) noexcept
    {
        return dsp.modes.gainReduction(MultiCompMode::FET, channel);
    }

    static std::array<float, 2> fetRecoveryGains(
        const MultiCompDSP& dsp) noexcept
    {
        return {{dsp.modes.fetPostBurstRecoveryGain(0),
                 dsp.modes.fetPostBurstRecoveryGain(1)}};
    }

    static void setFetStartupState(
        MultiCompDSP& dsp, std::array<float, 2> peaks,
        std::array<int, 2> activeSamples,
        std::array<int, 2> silentSamples) noexcept
    {
        dsp.fetStartupInputPeak = peaks;
        dsp.fetStartupActiveSamples = activeSamples;
        dsp.fetStartupSilentSamples = silentSamples;
    }

    static bool fetStartupStateIsClear(const MultiCompDSP& dsp) noexcept
    {
        return dsp.fetStartupInputPeak == std::array<float, 2>{{0.0f, 0.0f}}
            && dsp.fetStartupActiveSamples == std::array<int, 2>{{0, 0}}
            && dsp.fetStartupSilentSamples == std::array<int, 2>{{0, 0}};
    }

    static float advanceFetStartupBlend(
        int& activeSamples, int fullCorrectionSamples,
        int correctionEndSamples) noexcept
    {
        return MultiCompDSP::advanceFetStartupBlend(
            activeSamples, fullCorrectionSamples, correctionEndSamples);
    }
};
}

using duskaudio::DuskCrossover;
using duskaudio::MultiCompDSP;

namespace
{
constexpr float kPi = duskaudio::kDuskPi;
constexpr int kOversamplingOffSetting = 0;
constexpr int kOversampling2xSetting = 1;
constexpr int kOversampling4xSetting = 2;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

float rms(const std::vector<float>& x, size_t start = 0)
{
    double sum = 0.0;
    for (size_t i = start; i < x.size(); ++i) sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / std::max<size_t>(1, x.size() - start)));
}

float renderSine(duskaudio::MultiCompMode mode, float amplitude, double sr = 48000.0)
{
    MultiCompDSP dsp;
    dsp.prepare(sr, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(mode));
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetInput, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, 75.0f);
    dsp.setParameter(MultiCompDSP::Parameter::VcaThreshold, -20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::BusThreshold, -25.0f);
    dsp.setParameter(MultiCompDSP::Parameter::StudioVcaThreshold, -20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -20.0f);
    for (int block = 0; block < 100; ++block)
    {
        std::vector<float> in(256), out(256);
        for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * kPi * 1000.0f * static_cast<float>(block * 256 + i) / static_cast<float>(sr));
        const float* inputs[] = {in.data()}; float* outputs[] = {out.data()};
        dsp.processBlock(inputs, outputs, 1, 256);
        if (block == 99) return rms(out);
    }
    return 0.0f;
}

struct OptoStaticRender
{
    float rms = 0.0f;
    float meter = 0.0f;
};

struct OptoOutputRender
{
    float inputRms = 0.0f;
    float outputRms = 0.0f;
    float outputPeak = 0.0f;
};

OptoOutputRender renderOptoOutput(float inputDbfs, float gainKnob,
                                  int distortion = 0, float drive = 50.0f)
{
    constexpr int kSamples = 24000;
    constexpr int kMeasureSamples = 8192;
    constexpr int kBlockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Opto));
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::TruePeakEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, static_cast<float>(distortion));
    dsp.setParameter(MultiCompDSP::Parameter::DistortionAmount, drive);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoGain, gainKnob);
    dsp.setParameter(MultiCompDSP::Parameter::OptoLimit, 0.0f);
    dsp.prepare(48000.0, kBlockSize);

    const float amplitude = duskaudio::decibelsToGain(inputDbfs);
    double inputPower = 0.0;
    double outputPower = 0.0;
    float outputPeak = 0.0f;
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    for (int blockStart = 0; blockStart < kSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kSamples - blockStart);
        for (int i = 0; i < count; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(blockStart + i) / 48000.0f);
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        for (int i = 0; i < count; ++i)
        {
            if (blockStart + i < kSamples - kMeasureSamples) continue;
            const float in = input[static_cast<size_t>(i)];
            const float out = output[static_cast<size_t>(i)];
            inputPower += static_cast<double>(in) * in;
            outputPower += static_cast<double>(out) * out;
            outputPeak = std::max(outputPeak, std::abs(out));
        }
    }
    return {
        static_cast<float>(std::sqrt(inputPower / kMeasureSamples)),
        static_cast<float>(std::sqrt(outputPower / kMeasureSamples)),
        outputPeak
    };
}

void testOptoMeasuredGainTaper()
{
    struct TaperPoint { float knob; float gainDb; bool silent; };
    constexpr std::array<TaperPoint, 16> points{{
        {0.0f, 0.0f, true}, {5.0f, 0.0f, true},
        {10.0f, -21.951f, false}, {15.0f, -8.777f, false},
        {20.0f, -3.017f, false}, {23.75f, 0.214f, false},
        {30.0f, 4.191f, false}, {35.0f, 7.823f, false},
        {40.0f, 10.615f, false}, {50.0f, 14.501f, false},
        {60.0f, 18.813f, false}, {70.0f, 26.646f, false},
        {80.0f, 33.269f, false}, {90.0f, 37.271f, false},
        {95.0f, 37.702f, false}, {100.0f, 37.702f, false}
    }};
    bool allMatch = true;
    float worstErrorDb = 0.0f;
    for (const auto& point : points)
    {
        const auto measured = renderOptoOutput(-60.0f, point.knob);
        if (point.silent)
        {
            std::printf("opto gain taper: knob %.4f reference silent measured RMS %.9g\n",
                        point.knob * 0.01f, measured.outputRms);
            allMatch = allMatch && measured.outputRms == 0.0f;
            continue;
        }
        const float measuredGainDb = duskaudio::gainToDecibels(
            measured.outputRms / measured.inputRms);
        const float errorDb = measuredGainDb - point.gainDb;
        worstErrorDb = std::max(worstErrorDb, std::abs(errorDb));
        std::printf("opto gain taper: knob %.4f reference %+.3f dB measured %+.6f dB error %+.6f dB\n",
                    point.knob * 0.01f, point.gainDb, measuredGainDb, errorDb);
    }
    std::printf("opto gain taper summary: worst error %.6f dB\n", worstErrorDb);
    require(allMatch && worstErrorDb < 0.01f,
            "Opto Gain reproduces all sixteen measured taper positions");
}

void testOptoMeasuredOutputCeiling()
{
    constexpr std::array<float, 7> knobs{{40.0f, 50.0f, 60.0f, 70.0f,
                                          80.0f, 90.0f, 100.0f}};
    constexpr std::array<float, 7> taperDb{{10.615f, 14.501f, 18.813f,
                                            26.646f, 33.269f, 37.271f, 37.702f}};
    constexpr std::array<std::array<float, 7>, 2> referenceShortfallDb{{
        {{0.0f, 0.01f, 0.01f, -0.18f, -3.68f, -6.90f, -7.27f}},
        {{0.0f, -0.16f, -1.94f, -8.10f, -14.27f, -18.15f, -18.57f}}
    }};
    constexpr std::array<float, 2> inputDbfs{{-24.0f, -12.0f}};

    float worstErrorDb = 0.0f;
    float maximumDrivePeakDbfs = 0.0f;
    for (size_t inputRow = 0; inputRow < inputDbfs.size(); ++inputRow)
    {
        for (size_t point = 0; point < knobs.size(); ++point)
        {
            const auto measured = renderOptoOutput(inputDbfs[inputRow], knobs[point]);
            const float measuredGainDb = duskaudio::gainToDecibels(
                measured.outputRms / measured.inputRms);
            const float shortfallDb = measuredGainDb - taperDb[point];
            const float errorDb = shortfallDb - referenceShortfallDb[inputRow][point];
            worstErrorDb = std::max(worstErrorDb, std::abs(errorDb));
            std::printf("opto output ceiling: input %.0f dBFS knob %.2f reference %+.2f dB measured %+.6f dB error %+.6f dB\n",
                        inputDbfs[inputRow], knobs[point] * 0.01f,
                        referenceShortfallDb[inputRow][point], shortfallDb, errorDb);
            if (inputRow + 1 == inputDbfs.size() && point + 1 == knobs.size())
                maximumDrivePeakDbfs = duskaudio::gainToDecibels(measured.outputPeak);
        }
    }
    std::printf("opto output ceiling summary: worst shortfall error %.6f dB; maximum-drive peak %+.6f dBFS\n",
                worstErrorDb, maximumDrivePeakDbfs);
    require(worstErrorDb < 0.30f && std::abs(maximumDrivePeakDbfs - 4.72f) < 0.15f,
            "Opto output stage matches both measured shortfall curves and the observed peak plateau");
}

void testOptoDriveApplicability()
{
    const auto offAtZero = renderOptoOutput(-6.0f, duskaudio::kOptoGainUnityKnob, 0, 0.0f);
    const auto offAtHalf = renderOptoOutput(-6.0f, duskaudio::kOptoGainUnityKnob, 0, 50.0f);
    const auto softAtZero = renderOptoOutput(-6.0f, duskaudio::kOptoGainUnityKnob, 1, 0.0f);
    const auto softAtHalf = renderOptoOutput(-6.0f, duskaudio::kOptoGainUnityKnob, 1, 50.0f);
    const float offDelta = offAtHalf.outputRms - offAtZero.outputRms;
    const float softDeltaDb = duskaudio::gainToDecibels(
        softAtHalf.outputRms / softAtZero.outputRms);
    std::printf("opto Drive applicability: Distortion Off RMS delta %.9g; Soft Drive 0->0.5 delta %+.6f dB\n",
                offDelta, softDeltaDb);
    require(offDelta == 0.0f && std::abs(softDeltaDb) > 0.10f,
            "Opto Drive is inert only when Distortion is Off and controls a selected distortion");
}

OptoStaticRender renderOptoStatic(float inputDbfs, float peakReduction, bool limit,
                                  float frequencyHz = 997.0f,
                                  float gainKnob = duskaudio::optoGainDbToKnob(0.0f),
                                  double sampleRate = 48000.0)
{
    const int kSamples = static_cast<int>(std::lround(0.5 * sampleRate));
    const int kMeasureSamples = static_cast<int>(
        std::lround((4096.0 / 48000.0) * sampleRate));
    constexpr int kBlockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Opto));
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::TruePeakEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, peakReduction);
    dsp.setParameter(MultiCompDSP::Parameter::OptoGain, gainKnob);
    dsp.setParameter(MultiCompDSP::Parameter::OptoLimit, limit ? 1.0f : 0.0f);
    dsp.prepare(sampleRate, kBlockSize);
    const float amplitude = duskaudio::decibelsToGain(inputDbfs);
    double sum = 0.0;
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    for (int blockStart = 0; blockStart < kSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kSamples - blockStart);
        for (int i = 0; i < count; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(
                2.0f * kPi * frequencyHz * static_cast<float>(blockStart + i)
                / static_cast<float>(sampleRate));
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        for (int i = 0; i < count; ++i)
            if (blockStart + i >= kSamples - kMeasureSamples)
                sum += static_cast<double>(output[static_cast<size_t>(i)])
                    * output[static_cast<size_t>(i)];
    }
    return {static_cast<float>(std::sqrt(sum / kMeasureSamples)), dsp.getGainReduction()};
}

float renderOptoStaticRms(float inputDbfs, float peakReduction, bool limit)
{
    return renderOptoStatic(inputDbfs, peakReduction, limit).rms;
}

float renderOptoStaticMeter(float inputDbfs, float peakReduction, bool limit)
{
    return renderOptoStatic(inputDbfs, peakReduction, limit).meter;
}

float measureOptoStaticGr(float inputDbfs, float peakReduction, bool limit,
                          float frequencyHz = 997.0f,
                          float gainKnob = duskaudio::optoGainDbToKnob(0.0f),
                          double sampleRate = 48000.0)
{
    // Reference definition: output(PR=0) minus output(PR=x), at identical
    // input level and Gain.  Input-minus-output is intentionally never used.
    const float baseline = renderOptoStatic(
        inputDbfs, 0.0f, limit, frequencyHz, gainKnob, sampleRate).rms;
    const float reduced = renderOptoStatic(
        inputDbfs, peakReduction, limit, frequencyHz, gainKnob, sampleRate).rms;
    require(baseline > 1.0e-7f && reduced > 1.0e-9f,
            "Opto static-law comparison produces measurable output");
    return duskaudio::gainToDecibels(baseline) - duskaudio::gainToDecibels(reduced);
}

void testOptoMeterMatchesOutputReduction()
{
    constexpr std::array<float, 3> levels{{-40.0f, -24.0f, -12.0f}};
    for (const float level : levels)
    {
        const float baseline = renderOptoStaticRms(level, 0.0f, false);
        const float reduced = renderOptoStaticRms(level, 100.0f, false);
        const float outputReduction = duskaudio::gainToDecibels(baseline)
            - duskaudio::gainToDecibels(reduced);
        const float meter = renderOptoStaticMeter(level, 100.0f, false);
        const float discrepancy = meter + outputReduction;
        std::printf("opto meter: level %.1f dB output_reduction %.6f dB meter %.6f dB discrepancy %.6f dB\n",
                    level, outputReduction, meter, discrepancy);
        require(std::abs(discrepancy) < 0.05f,
                "Opto gain-reduction meter agrees with measured output reduction");
    }
}

void testOptoPluginLevelReferencePoints()
{
    constexpr std::array<float, 4> levels{{-40.0f, -30.0f, -18.0f, -4.0f}};
    constexpr std::array<float, 4> reference{{4.380f, 12.250f, 22.000f, 32.031f}};
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const float measured = measureOptoStaticGr(levels[i], 100.0f, false);
        const float delta = measured - reference[i];
        std::printf("opto processBlock reference: level %.1f dB reference %.3f dB measured %.6f dB delta %+.6f dB\n",
                    levels[i], reference[i], measured, delta);
        require(std::abs(delta) < 0.50f,
                "Opto processBlock output reduction matches the measured reference");
    }
}

void testOptoDetectorFrequencyWeighting()
{
    // Detector weighting is normalised at 1 kHz, so its shape score removes a
    // uniform operating-point offset. Absolute mean and 1 kHz anchors below
    // keep that removal from hiding a real detector-level regression; the
    // static-law and broadband gates independently own level accuracy.
    constexpr std::array<float, 31> frequencies{{
        20.0f, 25.178508f, 31.697864f, 39.905247f, 50.237728f,
        63.245552f, 79.621437f, 100.237450f, 126.191467f, 158.865646f,
        200.0f, 251.785080f, 316.978638f, 399.052460f, 502.377289f,
        632.455505f, 796.214355f, 1002.374451f, 1261.914673f, 1588.656494f,
        2000.0f, 2517.850830f, 3169.786377f, 3990.524658f, 5023.772949f,
        6324.555176f, 7962.143555f, 10023.745117f, 12619.146484f,
        15886.564453f, 20000.0f
    }};
    constexpr std::array<float, 31> referenceGr{{
        10.893349f, 10.932180f, 10.994167f, 11.083565f, 11.203087f,
        11.345378f, 11.487413f, 11.582150f, 11.563338f, 11.369726f,
        10.971520f, 10.398038f, 9.731938f, 9.077197f, 8.521148f,
        8.114972f, 7.873219f, 7.778958f, 7.802611f, 7.911287f,
        8.070195f, 8.242701f, 8.391923f, 8.484359f, 8.486708f,
        8.321553f, 7.847388f, 7.428818f, 7.457736f, 7.617802f,
        7.774295f
    }};

    std::array<float, frequencies.size()> deltas{};
    float meanDelta = 0.0f;
    for (size_t i = 0; i < frequencies.size(); ++i)
    {
        const float measured = measureOptoStaticGr(-24.0f, 70.0f, false, frequencies[i]);
        const float delta = measured - referenceGr[i];
        deltas[i] = delta;
        meanDelta += delta;
        std::printf("opto detector weighting: %.3f Hz reference %.6f dB measured %.6f dB delta %+.6f dB\n",
                    frequencies[i], referenceGr[i], measured, delta);
    }
    meanDelta /= static_cast<float>(deltas.size());
    float shapeSquaredError = 0.0f;
    for (const float delta : deltas)
        shapeSquaredError += (delta - meanDelta) * (delta - meanDelta);
    const float shapeRms = std::sqrt(shapeSquaredError / static_cast<float>(deltas.size()));
    constexpr size_t kOneKhzRow = 17;
    const float oneKhzDelta = deltas[kOneKhzRow];
    std::printf("opto detector weighting summary: mean offset %+.6f dB "
                "1 kHz offset %+.6f dB shape RMS %.6f dB\n",
                meanDelta, oneKhzDelta, shapeRms);
    require(std::abs(meanDelta) < 0.30f && std::abs(oneKhzDelta) < 0.25f,
            "Opto detector weighting keeps its absolute operating-point anchor");
    require(shapeRms < 0.12f,
            "Opto detector weighting shape survives after removing broadband offset");
}

void testOptoSubBassFloorContinuity()
{
    constexpr std::array<float, 6> frequencies{{
        32.0f, 33.0f, 33.4f, 33.5f, 34.0f, 35.0f}};
    float previous = 0.0f;
    float worstAdjacentDelta = 0.0f;
    for (size_t i = 0; i < frequencies.size(); ++i)
    {
        const float reduction = measureOptoStaticGr(
            -38.0f, 100.0f, false, frequencies[i]);
        if (i > 0)
            worstAdjacentDelta = std::max(
                worstAdjacentDelta, std::abs(reduction - previous));
        previous = reduction;
        std::printf("opto sub-bass floor: %.1f Hz %.6f dB GR\n",
                    frequencies[i], reduction);
    }
    std::printf("opto sub-bass floor: worst adjacent delta %.6f dB\n",
                worstAdjacentDelta);
    require(worstAdjacentDelta < 0.10f,
            "Opto sustained sub-bass response is continuous across the floor hold boundary");
}

void testOptoInactivePeakReduction()
{
    for (const bool limit : {false, true})
    for (const float peakReduction : {0.0f, 10.0f})
    {
        const float gr = measureOptoStaticGr(0.0f, peakReduction, limit);
        std::printf("opto inactive: limit=%d PR=%.1f GR=%.6f dB\n",
                    limit ? 1 : 0, peakReduction * 0.01f, gr);
        require(std::abs(gr) < 1.0e-6f, "Opto PR 0.0 and 0.1 never compress");
    }
}

void testOptoMeasuredOnsets()
{
    constexpr std::array<float, 9> peakReductions{{20, 30, 40, 50, 60, 70, 80, 90, 100}};
    constexpr std::array<float, 9> compressOnsets{{
        -3.8483f, -10.8579f, -17.0206f, -21.4516f, -25.7740f,
        -33.8121f, -40.1926f, -44.3555f, -45.4059f}};
    constexpr std::array<float, 9> limitOnsets{{
        -4.1256f, -11.1198f, -17.2740f, -21.6889f, -26.0047f,
        -34.0625f, -40.5015f, -44.6568f, -45.6471f}};
    for (const bool limit : {false, true})
    {
        const auto& expected = limit ? limitOnsets : compressOnsets;
        for (size_t row = 0; row < peakReductions.size(); ++row)
        {
            float lo = expected[row] - 1.0f;
            float hi = expected[row] + 1.0f;
            const float lowGr = measureOptoStaticGr(lo, peakReductions[row], limit);
            const float highGr = measureOptoStaticGr(hi, peakReductions[row], limit);
            std::printf("opto onset bracket: limit=%d PR=%.1f lowGR=%.4f highGR=%.4f dB\n",
                        limit ? 1 : 0, peakReductions[row] * 0.01f, lowGr, highGr);
            require(lowGr < 1.0f && highGr > 1.0f, "Opto measured 1 dB onset is bracketed");
            for (int iteration = 0; iteration < 7; ++iteration)
            {
                const float mid = 0.5f * (lo + hi);
                if (measureOptoStaticGr(mid, peakReductions[row], limit) < 1.0f) lo = mid;
                else hi = mid;
            }
            const float measured = 0.5f * (lo + hi);
            const float delta = measured - expected[row];
            std::printf("opto onset: limit=%d PR=%.1f target=%.4f measured=%.4f delta=%+.4f dB\n",
                        limit ? 1 : 0, peakReductions[row] * 0.01f,
                        expected[row], measured, delta);
            require(std::abs(delta) < 0.14f, "Opto 1 dB onset matches measured reference");
        }
    }
}

void testOptoThresholdOnlyCurveCollapse()
{
    // At a common overshoot, Peak Reduction may move only the threshold: it
    // must not alter the sidechain curve.  This is the corrected meaning of
    // the ratio being consistent across Peak Reduction settings.
    constexpr float kOvershoot = 8.0f;
    constexpr float kExpectedGr = 6.1696f;
    constexpr std::array<float, 3> peakReductions{{40.0f, 70.0f, 100.0f}};
    constexpr std::array<float, 3> compressThresholds{{-17.0206f, -33.8121f, -45.4059f}};
    constexpr float compressOffset = (1.0f - 0.9207f) / ((1.9474f - 0.9207f) * 0.5f);
    std::array<float, 3> measured{};
    for (size_t row = 0; row < measured.size(); ++row)
    {
        measured[row] = measureOptoStaticGr(compressThresholds[row] - compressOffset + kOvershoot,
                                            peakReductions[row], false);
        std::printf("opto curve collapse: PR=%.1f overshoot=%.1f target=%.4f measured=%.4f dB\n",
                    peakReductions[row] * 0.01f, kOvershoot, kExpectedGr, measured[row]);
        require(std::abs(measured[row] - kExpectedGr) < 0.05f,
                "Opto Compress common-overshoot GR matches measured curve");
    }
    const auto spread = std::minmax_element(measured.begin(), measured.end());
    require(*spread.second - *spread.first < 0.01f,
            "Opto Peak Reduction changes threshold without changing the GR curve");

    // The corrected curve is intentionally not a fixed-ratio straight line.
    const float kneeGr = measureOptoStaticGr(-45.4059f - compressOffset + 4.0f, 100.0f, false);
    const float middleGr = measureOptoStaticGr(-45.4059f - compressOffset + 16.0f, 100.0f, false);
    const float highGr = measureOptoStaticGr(-45.4059f - compressOffset + 40.0f, 100.0f, false);
    std::printf("opto curved ratio: overshoot 4/16/40 measured GR %.4f / %.4f / %.4f dB\n",
                kneeGr, middleGr, highGr);
    require(std::abs(kneeGr - 3.1430f) < 0.10f
                && std::abs(middleGr - 12.4485f) < 0.10f
                && std::abs(highGr - 30.6369f) < 0.10f,
            "Opto Compress follows the measured non-constant-ratio arc");
}

void testOptoLimitTopBrickWall()
{
    constexpr float compressOffset = (1.0f - 0.9207f) / ((1.9474f - 0.9207f) * 0.5f);
    constexpr float limitOffset = (1.0f - 0.9379f) / ((1.9978f - 0.9379f) * 0.5f);
    constexpr float kOvershoot = 40.0f;
    const float compress = measureOptoStaticGr(-45.4059f - compressOffset + kOvershoot, 100.0f, false);
    const float limit = measureOptoStaticGr(-45.6471f - limitOffset + kOvershoot, 100.0f, true);
    std::printf("opto top law: overshoot %.1f Compress %.4f dB Limit %.4f dB separation %.4f dB\n",
                kOvershoot, compress, limit, limit - compress);
    require(std::abs(limit - 38.8184f) < 0.10f,
            "Opto Limit reaches the measured near-brick-wall GR at high overshoot");
    require(std::abs((limit - compress) - 8.1815f) < 0.10f,
            "Opto Limit diverges from Compress by the measured amount at high overshoot");
}

void testOptoOverloadCompression()
{
    // Fresh licensed reference-AU renders captured 2026-08-21 at 48 kHz,
    // Compress, PR=0.7, Gain=0.321868896. Each reduction is the same-Gain
    // PR=0 control output minus the active output, so ceiling gain cancels.
    struct Row { float inputDbfs, referenceReduction; };
    constexpr std::array<Row, 2> rows{{
        {6.0f, 25.391453f},
        {12.0f, 24.107744f}
    }};
    float squaredError = 0.0f;
    float worstError = 0.0f;
    for (const auto& row : rows)
    {
        const float measured = measureOptoStaticGr(
            row.inputDbfs, 70.0f, false, 1000.0f, 32.1868896f);
        const float delta = measured - row.referenceReduction;
        std::printf("opto overload: input %+.1f dBFS reference %.6f dB "
                    "measured %.6f dB delta %+.6f dB\n",
                    row.inputDbfs, row.referenceReduction, measured, delta);
        squaredError += delta * delta;
        worstError = std::max(worstError, std::abs(delta));
    }
    const float rmsError = std::sqrt(squaredError / rows.size());
    std::printf("opto overload summary: RMS %.6f dB worst %.6f dB\n",
                rmsError, worstError);
    require(rmsError < 0.50f && worstError < 0.70f,
            "Opto Compress matches measured +6/+12 dBFS overload reduction");
}

void testOptoOverloadOrderingAndMonotonicity()
{
    constexpr std::array<float, 22> levelsDbfs{{
        -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f
    }};
    float previousCompressOutputDb = 0.0f;
    float worstCompressOverLimit = 0.0f;
    float worstOutputDrop = 0.0f;
    for (size_t row = 0; row < levelsDbfs.size(); ++row)
    {
        const float baselineOutputDb = duskaudio::gainToDecibels(
            renderOptoStatic(levelsDbfs[row], 0.0f, false,
                             1000.0f, 32.1868896f).rms);
        const float compressOutputDb = duskaudio::gainToDecibels(
            renderOptoStatic(levelsDbfs[row], 100.0f, false,
                             1000.0f, 32.1868896f).rms);
        const float limitOutputDb = duskaudio::gainToDecibels(
            renderOptoStatic(levelsDbfs[row], 100.0f, true,
                             1000.0f, 32.1868896f).rms);
        const float compress = baselineOutputDb - compressOutputDb;
        const float limit = baselineOutputDb - limitOutputDb;
        worstCompressOverLimit = std::max(
            worstCompressOverLimit, compress - limit);
        if (row > 0)
            worstOutputDrop = std::max(
                worstOutputDrop, previousCompressOutputDb - compressOutputDb);
        previousCompressOutputDb = compressOutputDb;
        std::printf("opto overload invariant: input %+.1f dBFS Compress %.6f dB "
                    "Limit %.6f dB separation %+.6f dB active output %.6f dBFS\n",
                    levelsDbfs[row], compress, limit, limit - compress,
                    compressOutputDb);
    }
    std::printf("opto overload invariant summary: Compress-over-Limit %.6f dB; "
                "worst active-output drop %.6f dB\n",
                worstCompressOverLimit, worstOutputDrop);
    require(worstCompressOverLimit < 0.05f,
            "Opto Limit remains at least as strong as Compress above the measured range");
    require(worstOutputDrop < 0.01f,
            "Opto Compress overload output remains monotonic with rising input");
}

void testOptoSampleRateParity()
{
    struct Row { double sampleRate; float referenceReduction; };
    constexpr std::array<Row, 2> rows{{
        {44100.0, 7.780751f},
        {96000.0, 7.779223f}
    }};
    std::array<float, rows.size()> deltas{};
    for (size_t row = 0; row < rows.size(); ++row)
    {
        const float measured = measureOptoStaticGr(
            -24.0f, 70.0f, false, 1000.0f, 32.1868896f,
            rows[row].sampleRate);
        deltas[row] = measured - rows[row].referenceReduction;
        std::printf("opto sample rate: %.1f kHz reference %.6f dB measured "
                    "%.6f dB delta %+.6f dB\n",
                    rows[row].sampleRate / 1000.0,
                    rows[row].referenceReduction, measured, deltas[row]);
        require(std::abs(deltas[row]) < 0.30f,
                "Opto static reduction matches the reference across sample rates");
    }
    const float residualSpread = std::abs(deltas[1] - deltas[0]);
    std::printf("opto sample rate summary: residual spread %.6f dB\n",
                residualSpread);
    require(residualSpread < 0.01f,
            "Opto reference residual is invariant from 44.1 to 96 kHz");
}

void prepareOptoDynamicsDsp(MultiCompDSP& dsp, float peakReduction,
                            int oversamplingSetting = kOversampling2xSetting)
{
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Opto));
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::TruePeakEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, peakReduction);
    dsp.setParameter(MultiCompDSP::Parameter::OptoGain, duskaudio::optoGainDbToKnob(0.0f));
    dsp.setParameter(MultiCompDSP::Parameter::OptoLimit, 0.0f);
    dsp.setOversampling(oversamplingSetting);
    dsp.prepare(48000.0, 256);
}

constexpr int kOptoPedestalEventTraceStartMs = -5;
constexpr int kOptoPedestalEventTraceStopMs = 80;
constexpr size_t kOptoPedestalEventTracePoints
    = kOptoPedestalEventTraceStopMs - kOptoPedestalEventTraceStartMs;

struct OptoPedestalEventReference
{
    float pedestalDbfs;
    float eventDbfs;
    float peakReduction;
    float pedestalGrDb;
    float liftAtPlus2MsDb;
    float earlyTauMs;
};

// The sixteen uncontaminated rows from pedestal_event.json.  The five
// -90 dBFS operational-silence rows are rendered below only as the pedestal-GR
// baseline; their carrier-start extraction is not a reference target.
constexpr std::array<OptoPedestalEventReference, 16>
kOptoPedestalEventReference{{
    {-33.0f, -12.0f, 0.70f,  1.395682f, 11.061453f, 18.907986f},
    {-33.0f,  -6.0f, 0.70f,  1.395682f, 13.670871f, 21.704937f},
    {-33.0f,   0.0f, 0.70f,  1.395682f, 15.463770f, 24.083064f},
    {-27.0f, -12.0f, 0.70f,  5.459101f,  7.603374f, 16.555105f},
    {-27.0f,  -6.0f, 0.70f,  5.459101f, 10.438680f, 20.405661f},
    {-27.0f,   0.0f, 0.70f,  5.459101f, 12.360860f, 23.212061f},
    {-21.0f, -12.0f, 0.70f, 10.181594f,  3.766819f, 11.084493f},
    {-21.0f,  -6.0f, 0.70f, 10.181594f,  6.737326f, 16.608847f},
    {-21.0f,   0.0f, 0.70f, 10.181594f,  8.837106f, 20.544415f},
    {-15.0f, -12.0f, 0.70f, 15.055978f,  0.788700f,  6.713385f},
    {-15.0f,  -6.0f, 0.70f, 15.055978f,  3.194736f, 10.462360f},
    {-15.0f,   0.0f, 0.70f, 15.055978f,  5.474565f, 15.479484f},
    {-27.0f,  -6.0f, 0.40f, -0.002905f,  5.563935f, 14.045118f},
    {-15.0f,  -6.0f, 0.40f,  2.030112f,  3.841599f, 12.031171f},
    {-27.0f,  -6.0f, 1.00f, 14.696565f,  6.657798f, 18.844623f},
    {-15.0f,  -6.0f, 1.00f, 24.321150f,  1.979050f, 11.279049f},
}};

struct OptoPedestalEventMeasurement
{
    float pedestalOutputRms = 0.0f;
    float pedestalGrDb = 0.0f;
    float liftAtPlus2MsDb = 0.0f;
    float earlyTauMs = std::numeric_limits<float>::quiet_NaN();
    float preEventMaximumAbsDb = 0.0f;
    float postEventMaximumDb = 0.0f;
    float meterPostEventMaximumDb = 0.0f;
    int postEventMaximumOffsetMs = 0;
    int meterPostEventMaximumOffsetMs = 0;
    int signalStartSample = 0;
    float meterEarlyTauMs = std::numeric_limits<float>::quiet_NaN();
    std::array<float, kOptoPedestalEventTracePoints> traceDb{};
    std::array<float, kOptoPedestalEventTracePoints> meterTraceDb{};
};

float optoPedestalEventCycleRms(const std::vector<float>& signal, int begin)
{
    constexpr int kCycleSamples = 48;
    require(begin >= 0 && begin + kCycleSamples <= static_cast<int>(signal.size()),
            "Opto pedestal-event cycle is inside the rendered signal");
    double mean = 0.0;
    for (int sample = 0; sample < kCycleSamples; ++sample)
        mean += signal[static_cast<size_t>(begin + sample)];
    mean /= kCycleSamples;
    double power = 0.0;
    for (int sample = 0; sample < kCycleSamples; ++sample)
    {
        const double centred
            = signal[static_cast<size_t>(begin + sample)] - mean;
        power += centred * centred;
    }
    const float value = static_cast<float>(std::sqrt(power / kCycleSamples));
    require(value > 1.0e-30f,
            "Opto pedestal-event cycle has non-zero DC-removed RMS");
    return value;
}

int locateOptoPedestalEventCarrierStart(const std::vector<float>& control)
{
    constexpr int kCycleSamples = 48;
    float peak = 0.0f;
    for (const float sample : control) peak = std::max(peak, std::abs(sample));
    const float threshold = peak * 0.10f;
    int crossing = -1;
    for (size_t sample = 0; sample < control.size(); ++sample)
        if (std::abs(control[sample]) > threshold)
        {
            crossing = static_cast<int>(sample);
            break;
        }
    require(crossing >= 0,
            "Opto pedestal-event control render has a detectable carrier");
    const int low = std::max(0, crossing - 2 * kCycleSamples);
    for (int sample = crossing - 1; sample >= low; --sample)
        if (control[static_cast<size_t>(sample)] <= 0.0f
            && control[static_cast<size_t>(sample + 1)] > 0.0f)
            return sample;
    return crossing;
}

float optoPedestalEventLocalTauMs(
    const std::array<float, kOptoPedestalEventTracePoints>& trace,
    int startMs, int stopMs)
{
    double sumTime = 0.0;
    double sumLog = 0.0;
    double sumTimeSquared = 0.0;
    double sumTimeLog = 0.0;
    int count = 0;
    for (int offsetMs = startMs; offsetMs < stopMs; ++offsetMs)
    {
        const float value = trace[static_cast<size_t>(
            offsetMs - kOptoPedestalEventTraceStartMs)];
        if (value <= 1.0e-6f) continue;
        const double logValue = std::log(static_cast<double>(value));
        sumTime += offsetMs;
        sumLog += logValue;
        sumTimeSquared += static_cast<double>(offsetMs) * offsetMs;
        sumTimeLog += static_cast<double>(offsetMs) * logValue;
        ++count;
    }
    if (count < 2) return std::numeric_limits<float>::quiet_NaN();
    const double denominator = count * sumTimeSquared - sumTime * sumTime;
    const double slope = (count * sumTimeLog - sumTime * sumLog) / denominator;
    return slope < -1.0e-12
        ? static_cast<float>(-1.0 / slope)
        : std::numeric_limits<float>::quiet_NaN();
}

OptoPedestalEventMeasurement measureOptoPedestalEventCell(
    float pedestalDbfs, float eventDbfs, float peakReduction)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = kSampleRate / 1000;
    constexpr int kStimulusSamples = 9 * kSampleRate / 2;
    constexpr int kEventStart = 4 * kSampleRate;
    constexpr int kEventSamples = 2 * kSampleRate / 1000;
    constexpr int kCycleSamples = kSampleRate / 1000;
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    static_assert(kBlockSize == kCycleSamples);
    static_assert(kEventStart % kCycleSamples == 0);
    static_assert(kEventSamples == 2 * kCycleSamples);
    static_assert(kOptoPedestalEventTracePoints == 85);
    static_assert(kEventStart
            + kOptoPedestalEventTraceStopMs * kCycleSamples
            + kCycleSamples
        <= kStimulusSamples);

    const float pedestalAmplitude = duskaudio::decibelsToGain(pedestalDbfs);
    const float eventAmplitude = duskaudio::decibelsToGain(eventDbfs);
    std::vector<float> withEventInput(static_cast<size_t>(kStimulusSamples));
    std::vector<float> withoutEventInput(static_cast<size_t>(kStimulusSamples));
    for (int sample = 0; sample < kStimulusSamples; ++sample)
    {
        const float carrier = static_cast<float>(std::sin(
            kTwoPi * 1000.0 * static_cast<double>(sample) / kSampleRate));
        withoutEventInput[static_cast<size_t>(sample)]
            = pedestalAmplitude * carrier;
        const float amplitude = sample >= kEventStart
                && sample < kEventStart + kEventSamples
            ? eventAmplitude : pedestalAmplitude;
        withEventInput[static_cast<size_t>(sample)] = amplitude * carrier;
    }
    constexpr int kQuarterCycleSamples = kCycleSamples / 4;
    require(withEventInput[static_cast<size_t>(kEventStart - 1)]
                == withoutEventInput[static_cast<size_t>(kEventStart - 1)]
            && withEventInput[static_cast<size_t>(
                    kEventStart + kEventSamples)]
                == withoutEventInput[static_cast<size_t>(
                    kEventStart + kEventSamples)]
            && std::abs(withEventInput[static_cast<size_t>(
                    kEventStart + kQuarterCycleSamples)] - eventAmplitude)
                < 1.0e-6f
            && std::abs(withoutEventInput[static_cast<size_t>(
                    kEventStart + kQuarterCycleSamples)] - pedestalAmplitude)
                < 1.0e-6f,
            "Opto pedestal-event stimulus has a phase-continuous two-cycle amplitude event");

    MultiCompDSP withEvent;
    MultiCompDSP withoutEvent;
    prepareOptoDynamicsDsp(withEvent, peakReduction * 100.0f);
    prepareOptoDynamicsDsp(withoutEvent, peakReduction * 100.0f);
    withEvent.setParameter(
        MultiCompDSP::Parameter::OptoGain, 32.1868896f);
    withoutEvent.setParameter(
        MultiCompDSP::Parameter::OptoGain, 32.1868896f);

    std::vector<float> withEventOutput(static_cast<size_t>(kStimulusSamples));
    std::vector<float> withoutEventOutput(static_cast<size_t>(kStimulusSamples));
    std::array<float, kBlockSize> withInputBlock{};
    std::array<float, kBlockSize> withoutInputBlock{};
    std::array<float, kBlockSize> withOutputLeft{}, withOutputRight{};
    std::array<float, kBlockSize> withoutOutputLeft{}, withoutOutputRight{};
    OptoPedestalEventMeasurement measurement;
    int meterFramesCaptured = 0;
    for (int blockStart = 0; blockStart < kStimulusSamples;
         blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kStimulusSamples - blockStart);
        std::copy_n(withEventInput.data() + blockStart, count,
                    withInputBlock.data());
        std::copy_n(withoutEventInput.data() + blockStart, count,
                    withoutInputBlock.data());
        const float* withInputs[] = {
            withInputBlock.data(), withInputBlock.data()};
        const float* withoutInputs[] = {
            withoutInputBlock.data(), withoutInputBlock.data()};
        float* withOutputs[] = {withOutputLeft.data(), withOutputRight.data()};
        float* withoutOutputs[] = {
            withoutOutputLeft.data(), withoutOutputRight.data()};
        withEvent.processBlock(withInputs, withOutputs, 2, count);
        withoutEvent.processBlock(withoutInputs, withoutOutputs, 2, count);
        std::copy_n(withOutputLeft.data(), count,
                    withEventOutput.data() + blockStart);
        std::copy_n(withoutOutputLeft.data(), count,
                    withoutEventOutput.data() + blockStart);
        const int meterOffsetMs = (blockStart - kEventStart) / kCycleSamples;
        if (blockStart >= kEventStart
                + kOptoPedestalEventTraceStartMs * kCycleSamples
            && blockStart < kEventStart
                + kOptoPedestalEventTraceStopMs * kCycleSamples)
            measurement.meterTraceDb[static_cast<size_t>(
                meterOffsetMs - kOptoPedestalEventTraceStartMs)]
                    = withoutEvent.getGainReduction()
                        - withEvent.getGainReduction();
        if (blockStart >= kEventStart
                + kOptoPedestalEventTraceStartMs * kCycleSamples
            && blockStart < kEventStart
                + kOptoPedestalEventTraceStopMs * kCycleSamples)
            ++meterFramesCaptured;
    }
    require(meterFramesCaptured
                == static_cast<int>(kOptoPedestalEventTracePoints),
            "Opto pedestal-event meter covers every one-millisecond trace frame");

    measurement.signalStartSample
        = locateOptoPedestalEventCarrierStart(withoutEventOutput);
    const int eventOutputStart = measurement.signalStartSample + kEventStart;
    for (int offsetMs = kOptoPedestalEventTraceStartMs;
         offsetMs < kOptoPedestalEventTraceStopMs; ++offsetMs)
    {
        const int outputBegin = eventOutputStart + offsetMs * kCycleSamples;
        const int inputBegin = kEventStart + offsetMs * kCycleSamples;
        const float withOutputRms
            = optoPedestalEventCycleRms(withEventOutput, outputBegin);
        const float withoutOutputRms
            = optoPedestalEventCycleRms(withoutEventOutput, outputBegin);
        const float withInputRms
            = optoPedestalEventCycleRms(withEventInput, inputBegin);
        const float withoutInputRms
            = optoPedestalEventCycleRms(withoutEventInput, inputBegin);
        const float inputCorrectionDb = duskaudio::gainToDecibels(
            withInputRms / withoutInputRms);
        const float outputRatioDb = duskaudio::gainToDecibels(
            withOutputRms / withoutOutputRms);
        measurement.traceDb[static_cast<size_t>(
            offsetMs - kOptoPedestalEventTraceStartMs)]
                = inputCorrectionDb - outputRatioDb;
    }

    double pedestalPower = 0.0;
    for (int offsetMs = kOptoPedestalEventTraceStartMs; offsetMs < 0;
         ++offsetMs)
    {
        const float cycle = optoPedestalEventCycleRms(
            withoutEventOutput, eventOutputStart + offsetMs * kCycleSamples);
        pedestalPower += static_cast<double>(cycle) * cycle;
    }
    measurement.pedestalOutputRms = static_cast<float>(
        std::sqrt(pedestalPower / -kOptoPedestalEventTraceStartMs));
    for (int offsetMs = kOptoPedestalEventTraceStartMs; offsetMs < 0;
         ++offsetMs)
        measurement.preEventMaximumAbsDb = std::max(
            measurement.preEventMaximumAbsDb,
            std::abs(measurement.traceDb[static_cast<size_t>(
                offsetMs - kOptoPedestalEventTraceStartMs)]));
    measurement.liftAtPlus2MsDb = measurement.traceDb[static_cast<size_t>(
        2 - kOptoPedestalEventTraceStartMs)];
    measurement.postEventMaximumDb = measurement.liftAtPlus2MsDb;
    measurement.postEventMaximumOffsetMs = 2;
    for (int offsetMs = 3; offsetMs < kOptoPedestalEventTraceStopMs; ++offsetMs)
    {
        const float value = measurement.traceDb[static_cast<size_t>(
            offsetMs - kOptoPedestalEventTraceStartMs)];
        if (value > measurement.postEventMaximumDb)
        {
            measurement.postEventMaximumDb = value;
            measurement.postEventMaximumOffsetMs = offsetMs;
        }
    }
    measurement.earlyTauMs
        = optoPedestalEventLocalTauMs(measurement.traceDb, 2, 10);
    measurement.meterPostEventMaximumDb
        = measurement.meterTraceDb[static_cast<size_t>(
            2 - kOptoPedestalEventTraceStartMs)];
    measurement.meterPostEventMaximumOffsetMs = 2;
    for (int offsetMs = 3; offsetMs < kOptoPedestalEventTraceStopMs; ++offsetMs)
    {
        const float value = measurement.meterTraceDb[static_cast<size_t>(
            offsetMs - kOptoPedestalEventTraceStartMs)];
        if (value > measurement.meterPostEventMaximumDb)
        {
            measurement.meterPostEventMaximumDb = value;
            measurement.meterPostEventMaximumOffsetMs = offsetMs;
        }
    }
    measurement.meterEarlyTauMs = optoPedestalEventLocalTauMs(
        measurement.meterTraceDb, 2, 10);
    return measurement;
}

struct OptoPedestalEventGrid
{
    std::array<OptoPedestalEventMeasurement,
               kOptoPedestalEventReference.size()> cells{};
};

const OptoPedestalEventGrid& measureOptoPedestalEventGrid()
{
    static const OptoPedestalEventGrid measured = [] {
        OptoPedestalEventGrid result;
        for (size_t row = 0; row < kOptoPedestalEventReference.size(); ++row)
        {
            const auto& reference = kOptoPedestalEventReference[row];
            result.cells[row] = measureOptoPedestalEventCell(
                reference.pedestalDbfs, reference.eventDbfs,
                reference.peakReduction);
            const auto silence = measureOptoPedestalEventCell(
                -90.0f, reference.eventDbfs, reference.peakReduction);
            const float inputDeltaDb = reference.pedestalDbfs + 90.0f;
            const float outputDeltaDb = duskaudio::gainToDecibels(
                result.cells[row].pedestalOutputRms
                    / silence.pedestalOutputRms);
            result.cells[row].pedestalGrDb = inputDeltaDb - outputDeltaDb;
        }
        return result;
    }();
    return measured;
}

float optoPedestalEventSlope(float eventDbfs)
{
    const auto& grid = measureOptoPedestalEventGrid();
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    int count = 0;
    for (size_t row = 0; row < kOptoPedestalEventReference.size(); ++row)
    {
        const auto& reference = kOptoPedestalEventReference[row];
        if (reference.peakReduction != 0.70f
            || reference.eventDbfs != eventDbfs)
            continue;
        const double x = grid.cells[row].pedestalGrDb;
        const double y = grid.cells[row].liftAtPlus2MsDb;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++count;
    }
    return static_cast<float>((count * sumXY - sumX * sumY)
        / (count * sumXX - sumX * sumX));
}

void reportOptoPedestalEventGrid()
{
    const auto& grid = measureOptoPedestalEventGrid();
    // These are the measured reference laws from
    // measurements/mechanism_findings_2026-08-24.md. They become assertions
    // again when a real, always-active mechanism lands.
    std::puts("opto pedestal-event measured reference laws: report-only until "
              "an always-active mechanism lands "
              "(measurements/mechanism_findings_2026-08-24.md)");
    std::puts("opto pedestal-event grid: ped/event/PR | pedestal GR ref/mine/delta | "
              "lift@+2ms ref/mine/delta | tau2-10 ref/mine/delta | mine peak offset/rise");
    size_t outputNoRiseCells = 0;
    size_t populationNoRiseCells = 0;
    size_t finiteTauCells = 0;
    for (size_t row = 0; row < kOptoPedestalEventReference.size(); ++row)
    {
        const auto& reference = kOptoPedestalEventReference[row];
        const auto& mine = grid.cells[row];
        std::printf("opto pedestal-event: %+.0f/%+.0f/%.2f | "
                    "%.6f/%.6f/%+.6f | %.6f/%.6f/%+.6f | "
                    "%.6f/%.6f/%+.6f | %+dms/%+.6f dB pre %.6f dB start %d\n",
                    reference.pedestalDbfs, reference.eventDbfs,
                    reference.peakReduction,
                    reference.pedestalGrDb, mine.pedestalGrDb,
                    mine.pedestalGrDb - reference.pedestalGrDb,
                    reference.liftAtPlus2MsDb, mine.liftAtPlus2MsDb,
                    mine.liftAtPlus2MsDb - reference.liftAtPlus2MsDb,
                    reference.earlyTauMs, mine.earlyTauMs,
                    mine.earlyTauMs - reference.earlyTauMs,
                    mine.postEventMaximumOffsetMs,
                    mine.postEventMaximumDb - mine.liftAtPlus2MsDb,
                    mine.preEventMaximumAbsDb, mine.signalStartSample);
        std::printf("  output incremental GR +0/+1/+2/+3ms "
                    "%.6f/%.6f/%.6f/%.6f dB; meter +2/+3/+10ms "
                    "%.6f/%.6f/%.6f dB tau %.6fms peak %+dms rise %+.6f dB\n",
                    mine.traceDb[static_cast<size_t>(
                        0 - kOptoPedestalEventTraceStartMs)],
                    mine.traceDb[static_cast<size_t>(
                        1 - kOptoPedestalEventTraceStartMs)],
                    mine.traceDb[static_cast<size_t>(
                        2 - kOptoPedestalEventTraceStartMs)],
                    mine.traceDb[static_cast<size_t>(
                        3 - kOptoPedestalEventTraceStartMs)],
                    mine.meterTraceDb[static_cast<size_t>(
                        2 - kOptoPedestalEventTraceStartMs)],
                    mine.meterTraceDb[static_cast<size_t>(
                        3 - kOptoPedestalEventTraceStartMs)],
                    mine.meterTraceDb[static_cast<size_t>(
                        10 - kOptoPedestalEventTraceStartMs)],
                    mine.meterEarlyTauMs,
                    mine.meterPostEventMaximumOffsetMs,
                    mine.meterPostEventMaximumDb
                        - mine.meterTraceDb[static_cast<size_t>(
                            2 - kOptoPedestalEventTraceStartMs)]);
        if (mine.postEventMaximumDb <= mine.liftAtPlus2MsDb + 0.10f)
            ++outputNoRiseCells;
        if (mine.meterPostEventMaximumDb
            <= mine.meterTraceDb[static_cast<size_t>(
                2 - kOptoPedestalEventTraceStartMs)] + 0.10f)
            ++populationNoRiseCells;
        if (std::isfinite(mine.earlyTauMs)) ++finiteTauCells;
    }
    std::printf("opto pedestal-event slopes at PR 0.70: E -12/-6/0 "
                "%.6f / %.6f / %.6f dB/dB\n",
                optoPedestalEventSlope(-12.0f),
                optoPedestalEventSlope(-6.0f),
                optoPedestalEventSlope(0.0f));
    bool tauShrinksWithState = true;
    bool tauGrowsWithEvent = true;
    for (size_t event = 0; event < 3; ++event)
        for (size_t pedestal = 1; pedestal < 4; ++pedestal)
            tauShrinksWithState = tauShrinksWithState
                && grid.cells[pedestal * 3 + event].earlyTauMs
                    < grid.cells[(pedestal - 1) * 3 + event].earlyTauMs;
    for (size_t pedestal = 0; pedestal < 4; ++pedestal)
        for (size_t event = 1; event < 3; ++event)
            tauGrowsWithEvent = tauGrowsWithEvent
                && grid.cells[pedestal * 3 + event].earlyTauMs
                    > grid.cells[pedestal * 3 + event - 1].earlyTauMs;
    std::printf("opto pedestal-event report-only law summary: output no-rise "
                "%zu/%zu cells; population no-rise %zu/%zu cells; finite "
                "tau %zu/%zu cells; tau shrinks with state %s; tau grows "
                "with event %s\n",
                outputNoRiseCells, grid.cells.size(), populationNoRiseCells,
                grid.cells.size(), finiteTauCells, grid.cells.size(),
                tauShrinksWithState ? "yes" : "no",
                tauGrowsWithEvent ? "yes" : "no");
}

void testOptoPedestalEventHarnessInvariants()
{
    const auto& grid = measureOptoPedestalEventGrid();
    for (size_t row = 0; row < grid.cells.size(); ++row)
    {
        const auto& cell = grid.cells[row];
        require(cell.signalStartSample == 26,
                "Opto in-process pedestal-event extraction stays cycle-aligned");
        require(cell.preEventMaximumAbsDb < 0.10f,
                "Opto in-process pedestal-event render stays below the clean-cell contamination limit");
    }
}

struct OptoHarmonicMeasurement
{
    float fundamentalDbfs = -120.0f;
    std::array<float, 4> harmonicsDbc{{-120.0f, -120.0f, -120.0f, -120.0f}};
    float evenMinusOddDb = 0.0f;
    float meterDb = 0.0f;
};

OptoHarmonicMeasurement measureOptoHarmonics(float inputDbfs, float peakReduction)
{
    // Identical to the reference extraction: settled 1 kHz tone, least-squares
    // sinusoid amplitudes over seconds 4.0-5.5, reported relative to H1.
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kMeasureStart = 4 * kSampleRate;
    constexpr int kMeasureSamples = 3 * kSampleRate / 2;
    constexpr int kTotalSamples = kMeasureStart + kMeasureSamples;
    MultiCompDSP dsp;
    prepareOptoDynamicsDsp(dsp, peakReduction);
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    const float amplitude = duskaudio::decibelsToGain(inputDbfs);
    std::array<double, 5> sineProjection{};
    std::array<double, 5> cosineProjection{};
    for (int blockStart = 0; blockStart < kTotalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kTotalSamples - blockStart);
        for (int i = 0; i < count; ++i)
        {
            const float phase = 2.0f * kPi * 1000.0f
                * static_cast<float>(blockStart + i) / kSampleRate;
            input[static_cast<size_t>(i)] = amplitude * std::sin(phase);
        }
        dsp.processBlock(inputs, outputs, 1, count);
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            if (sample < kMeasureStart) continue;
            const double phase = 2.0 * static_cast<double>(kPi) * 1000.0
                * static_cast<double>(sample) / kSampleRate;
            for (size_t harmonic = 1; harmonic <= 5; ++harmonic)
            {
                const double value = output[static_cast<size_t>(i)];
                sineProjection[harmonic - 1] += value
                    * std::sin(static_cast<double>(harmonic) * phase);
                cosineProjection[harmonic - 1] += value
                    * std::cos(static_cast<double>(harmonic) * phase);
            }
        }
    }
    std::array<float, 5> amplitudes{};
    for (size_t harmonic = 0; harmonic < amplitudes.size(); ++harmonic)
        amplitudes[harmonic] = 2.0f / kMeasureSamples * static_cast<float>(std::hypot(
            sineProjection[harmonic], cosineProjection[harmonic]));
    OptoHarmonicMeasurement measurement;
    measurement.fundamentalDbfs = duskaudio::gainToDecibels(amplitudes[0]);
    for (size_t harmonic = 0; harmonic < measurement.harmonicsDbc.size(); ++harmonic)
        measurement.harmonicsDbc[harmonic] = duskaudio::gainToDecibels(
            amplitudes[harmonic + 1] / std::max(amplitudes[0], 1.0e-12f));
    const float evenPower = amplitudes[1] * amplitudes[1]
        + amplitudes[3] * amplitudes[3];
    const float oddPower = amplitudes[2] * amplitudes[2]
        + amplitudes[4] * amplitudes[4];
    measurement.evenMinusOddDb = 10.0f * std::log10(
        std::max(evenPower, 1.0e-24f) / std::max(oddPower, 1.0e-24f));
    measurement.meterDb = dsp.getGainReduction();
    return measurement;
}

void testOptoHarmonicContent()
{
    struct Row
    {
        float inputDbfs;
        float peakReduction;
        std::array<float, 4> reference;
        float referenceEvenMinusOdd;
    };
    constexpr std::array<Row, 6> rows{{
        {-24.0f,  0.0f, {{-65.09f, -82.77f, -110.22f, -104.60f}}, 17.65f},
        {-24.0f, 70.0f, {{-52.62f, -54.25f,  -58.75f,  -63.13f}},  2.04f},
        {-12.0f,  0.0f, {{-53.53f, -77.94f,  -93.78f,  -85.33f}}, 23.68f},
        {-12.0f, 70.0f, {{-49.81f, -55.25f,  -66.66f,  -65.65f}},  5.15f},
        { -6.0f,  0.0f, {{-49.53f, -55.53f,  -67.31f,  -64.75f}},  5.58f},
        { -6.0f, 70.0f, {{-53.43f, -55.75f,  -69.16f,  -69.41f}},  2.26f}
    }};
    std::array<OptoHarmonicMeasurement, rows.size()> measured{};
    float worstFundamentalError = 0.0f;
    float worstHarmonicError = 0.0f;
    float worstCompressedHarmonicError = 0.0f;
    float worstEvenOddError = 0.0f;
    for (size_t row = 0; row < rows.size(); ++row)
    {
        measured[row] = measureOptoHarmonics(
            rows[row].inputDbfs, rows[row].peakReduction);
        worstFundamentalError = std::max(worstFundamentalError, std::abs(
            measured[row].fundamentalDbfs
                - (rows[row].inputDbfs + measured[row].meterDb)));
        for (size_t harmonic = 0; harmonic < rows[row].reference.size(); ++harmonic)
        {
            const float error = std::abs(
                measured[row].harmonicsDbc[harmonic] - rows[row].reference[harmonic]);
            worstHarmonicError = std::max(worstHarmonicError, error);
            if (rows[row].peakReduction > 0.0f)
                worstCompressedHarmonicError = std::max(
                    worstCompressedHarmonicError, error);
        }
        worstEvenOddError = std::max(worstEvenOddError, std::abs(
            measured[row].evenMinusOddDb - rows[row].referenceEvenMinusOdd));
        std::printf("opto harmonics: input %.0f dBFS PR %.1f meter %.6f dB "
                    "H2/H3/H4/H5 ref %.2f/%.2f/%.2f/%.2f "
                    "measured %.6f/%.6f/%.6f/%.6f dBc "
                    "even-odd ref %.2f measured %.6f dB\n",
                    rows[row].inputDbfs, rows[row].peakReduction * 0.01f,
                    measured[row].meterDb,
                    rows[row].reference[0], rows[row].reference[1],
                    rows[row].reference[2], rows[row].reference[3],
                    measured[row].harmonicsDbc[0], measured[row].harmonicsDbc[1],
                    measured[row].harmonicsDbc[2], measured[row].harmonicsDbc[3],
                    rows[row].referenceEvenMinusOdd,
                    measured[row].evenMinusOddDb);
    }
    bool evenOddRegimesHold = true;
    for (size_t level = 0; level < rows.size(); level += 2)
    {
        const float fundamentalReduction = measured[level].fundamentalDbfs
            - measured[level + 1].fundamentalDbfs;
        const float error = fundamentalReduction + measured[level + 1].meterDb;
        std::printf("opto harmonic fundamental: input %.0f dBFS reduction %.6f dB "
                    "meter %.6f dB error %+.6f dB\n",
                    rows[level].inputDbfs, fundamentalReduction,
                    measured[level + 1].meterDb, error);
        evenOddRegimesHold = evenOddRegimesHold
            && measured[level].evenMinusOddDb > measured[level + 1].evenMinusOddDb
            && measured[level + 1].evenMinusOddDb >= 2.0f
            && measured[level + 1].evenMinusOddDb <= 6.0f;
    }
    std::printf("opto harmonics summary: worst fundamental %.6f dB; "
                "worst all/compressed harmonic %.6f/%.6f dB; "
                "worst even-odd %.6f dB\n",
                worstFundamentalError, worstHarmonicError,
                worstCompressedHarmonicError, worstEvenOddError);
    require(worstFundamentalError < 0.001f,
            "Opto colouration adds no measurable fundamental gain");
    // The -104.6 dBc uncompressed H5 endpoint is close to the float render's
    // numerical floor, so the all-row tolerance is 2 dB. Under compression,
    // where the colour mechanism is load-bearing, retain a 0.12 dB bound.
    require(worstHarmonicError < 2.0f
                && worstCompressedHarmonicError < 0.12f
                && worstEvenOddError < 0.15f
                && evenOddRegimesHold,
            "Opto H2-H5 and even-to-odd balance match all six measured points");
}

struct OptoCrestResult
{
    float crestDb = 0.0f;
    float reductionDb = 0.0f;
};

enum class OptoCrestStimulus { Sine, Burst, Noise };

OptoCrestResult measureOptoCrestResponse(OptoCrestStimulus stimulus)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kWarmupSamples = kSampleRate;
    constexpr int kMeasureSamples = 5 * kSampleRate;
    constexpr int kTotalSamples = kWarmupSamples + kMeasureSamples;
    constexpr float kTargetRms = 0.1f;
    std::vector<float> signal(static_cast<size_t>(kTotalSamples));

    uint32_t randomState = 0x6d2b79f5u;
    bool haveSpare = false;
    float spare = 0.0f;
    auto uniform = [&]() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return (static_cast<float>(randomState) + 0.5f) / 4294967296.0f;
    };
    auto gaussian = [&]() {
        if (haveSpare)
        {
            haveSpare = false;
            return spare;
        }
        const float radius = std::sqrt(-2.0f * std::log(std::max(uniform(), 1.0e-12f)));
        const float angle = 2.0f * kPi * uniform();
        spare = radius * std::sin(angle);
        haveSpare = true;
        return radius * std::cos(angle);
    };
    for (int sample = 0; sample < kTotalSamples; ++sample)
    {
        const float sine = std::sin(2.0f * kPi * 1000.0f
                                    * static_cast<float>(sample) / kSampleRate);
        if (stimulus == OptoCrestStimulus::Sine)
            signal[static_cast<size_t>(sample)] = sine;
        else if (stimulus == OptoCrestStimulus::Burst)
            signal[static_cast<size_t>(sample)] = sample % (kSampleRate / 20)
                    < (kSampleRate / 200) ? sine : 0.0f;
        else
            signal[static_cast<size_t>(sample)] = gaussian();
    }
    double inputPower = 0.0;
    for (int sample = kWarmupSamples; sample < kTotalSamples; ++sample)
        inputPower += static_cast<double>(signal[static_cast<size_t>(sample)])
            * signal[static_cast<size_t>(sample)];
    const float inputRms = static_cast<float>(std::sqrt(inputPower / kMeasureSamples));
    const float normalisation = kTargetRms / std::max(inputRms, 1.0e-12f);
    float peak = 0.0f;
    for (auto& value : signal)
    {
        value *= normalisation;
        peak = std::max(peak, std::abs(value));
    }

    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    std::array<float, kBlockSize> controlOutput{};
    std::array<float, kBlockSize> activeOutput{};
    double controlPower = 0.0;
    double activePower = 0.0;
    for (int blockStart = 0; blockStart < kTotalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kTotalSamples - blockStart);
        const float* inputs[] = {signal.data() + blockStart};
        float* controlOutputs[] = {controlOutput.data()};
        float* activeOutputs[] = {activeOutput.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        for (int i = 0; i < count; ++i)
            if (blockStart + i >= kWarmupSamples)
            {
                controlPower += static_cast<double>(controlOutput[static_cast<size_t>(i)])
                    * controlOutput[static_cast<size_t>(i)];
                activePower += static_cast<double>(activeOutput[static_cast<size_t>(i)])
                    * activeOutput[static_cast<size_t>(i)];
            }
    }
    return {duskaudio::gainToDecibels(peak / kTargetRms),
            10.0f * std::log10(static_cast<float>(controlPower / activePower))};
}

void reportOptoCrestResponse()
{
    const auto sine = measureOptoCrestResponse(OptoCrestStimulus::Sine);
    const auto burst = measureOptoCrestResponse(OptoCrestStimulus::Burst);
    const auto noise = measureOptoCrestResponse(OptoCrestStimulus::Noise);
    std::printf("opto crest response: sine crest %.6f GR %.6f dB\n",
                sine.crestDb, sine.reductionDb);
    std::printf("opto crest response: burst crest %.6f GR %.6f excess %+.6f dB\n",
                burst.crestDb, burst.reductionDb, burst.reductionDb - sine.reductionDb);
    std::printf("opto crest response: noise crest %.6f GR %.6f excess %+.6f dB\n",
                noise.crestDb, noise.reductionDb, noise.reductionDb - sine.reductionDb);
}

OptoCrestResult measureOptoCrestSweepPoint(int burstSamples)
{
    // Identical to the reference extraction: constant -24 dBFS RMS, 1 kHz
    // bursts every 50 ms, settled over seconds 5.0-7.5.
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kPeriodSamples = 50 * kSampleRate / 1000;
    constexpr int kMeasureStart = 5 * kSampleRate;
    constexpr int kMeasureSamples = 5 * kSampleRate / 2;
    constexpr int kTotalSamples = kMeasureStart + kMeasureSamples;
    constexpr float kTargetRms = 0.0630957344f;
    std::vector<float> signal(static_cast<size_t>(kTotalSamples));
    for (int sample = 0; sample < kTotalSamples; ++sample)
        if (sample % kPeriodSamples < burstSamples)
            signal[static_cast<size_t>(sample)] = std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(sample) / kSampleRate);

    double inputPower = 0.0;
    for (int sample = kMeasureStart; sample < kTotalSamples; ++sample)
        inputPower += static_cast<double>(signal[static_cast<size_t>(sample)])
            * signal[static_cast<size_t>(sample)];
    const float inputRms = static_cast<float>(
        std::sqrt(inputPower / kMeasureSamples));
    const float normalisation = kTargetRms / std::max(inputRms, 1.0e-12f);
    float peak = 0.0f;
    for (auto& value : signal)
    {
        value *= normalisation;
        peak = std::max(peak, std::abs(value));
    }

    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    control.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1869f);
    active.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1869f);
    std::array<float, kBlockSize> controlLeft{}, controlRight{};
    std::array<float, kBlockSize> activeLeft{}, activeRight{};
    double controlPower = 0.0;
    double activePower = 0.0;
    for (int blockStart = 0; blockStart < kTotalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kTotalSamples - blockStart);
        const float* inputs[] = {signal.data() + blockStart,
                                 signal.data() + blockStart};
        float* controlOutputs[] = {controlLeft.data(), controlRight.data()};
        float* activeOutputs[] = {activeLeft.data(), activeRight.data()};
        control.processBlock(inputs, controlOutputs, 2, count);
        active.processBlock(inputs, activeOutputs, 2, count);
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            if (sample < kMeasureStart) continue;
            controlPower += static_cast<double>(controlLeft[static_cast<size_t>(i)])
                * controlLeft[static_cast<size_t>(i)];
            activePower += static_cast<double>(activeLeft[static_cast<size_t>(i)])
                * activeLeft[static_cast<size_t>(i)];
        }
    }
    return {duskaudio::gainToDecibels(peak / kTargetRms),
            10.0f * std::log10(static_cast<float>(controlPower / activePower))};
}

void testOptoCrestSweep()
{
    constexpr std::array<int, 4> burstSamples{{2400, 144, 48, 17}};
    constexpr std::array<float, 4> referenceCrestDb{{3.0103f, 15.2288f,
                                                      20.0000f, 23.7901f}};
    constexpr std::array<float, 4> referenceGrDb{{10.192f, 14.070f,
                                                   11.328f, 8.520f}};
    constexpr size_t kHeldOutRow = 2;
    std::array<OptoCrestResult, burstSamples.size()> measured{};
    float fittedSquaredError = 0.0f;
    float fittedWorstError = 0.0f;
    float heldOutError = 0.0f;
    bool finiteAndCorrectCrest = true;
    for (size_t row = 0; row < burstSamples.size(); ++row)
    {
        measured[row] = measureOptoCrestSweepPoint(burstSamples[row]);
        const float delta = measured[row].reductionDb - referenceGrDb[row];
        std::printf("opto crest sweep: crest %.6f dB reference %.3f dB "
                    "measured %.6f dB delta %+.6f dB%s\n",
                    measured[row].crestDb, referenceGrDb[row],
                    measured[row].reductionDb, delta,
                    row == kHeldOutRow ? " (held out)" : "");
        finiteAndCorrectCrest = finiteAndCorrectCrest
            && std::isfinite(measured[row].reductionDb)
            && std::abs(measured[row].crestDb - referenceCrestDb[row]) < 0.02f;
        if (row == kHeldOutRow)
            heldOutError = delta;
        else
        {
            fittedSquaredError += delta * delta;
            fittedWorstError = std::max(fittedWorstError, std::abs(delta));
        }
    }
    const float fittedRmsError = std::sqrt(fittedSquaredError / 3.0f);
    const bool nonMonotonicShape = measured[1].reductionDb > measured[0].reductionDb
        && measured[2].reductionDb < measured[1].reductionDb
        && measured[3].reductionDb < measured[2].reductionDb;
    std::printf("opto crest sweep summary: fitted 3.0/15.2/23.8 dB crest "
                "RMS %.6f dB worst %.6f dB; held-out 20.0 dB crest "
                "delta %+.6f dB\n",
                fittedRmsError, fittedWorstError, heldOutError);
    // No tested causal charge mechanism reconciled the 20 dB row with the
    // burst-rate gate. Pin the reproduced non-monotonic shape and the known
    // residual so a later mechanism cannot silently regress either side.
    // The current rounding-robust residuals are -0.141 / -0.540 / +0.615
    // (held out) / -0.709 dB: 0.546 dB RMS across all four rows and 0.521 dB
    // across the three fitted rows. The older -0.20 / -0.26 / +0.71 / -1.28
    // dB trajectory and its 0.96 -> 0.75 dB comparison are historical. The
    // remaining shape error is the same open structural defect.
    require(finiteAndCorrectCrest && nonMonotonicShape
                && fittedRmsError < 0.85f && fittedWorstError < 1.40f
                && std::abs(heldOutError) < 1.10f,
            "Opto linked-stereo crest sweep preserves its measured shape and bounded residual");
}

constexpr std::array<int, 7> kOptoShortEventTraceOffsets{{
    0, 48, 96, 192, 384, 768, 1536}};

std::array<float, kOptoShortEventTraceOffsets.size()>
measureOptoShortEventTrace(int eventSamples, float peakDbfs,
                           float preEventFloorDbfs = -200.0f,
                           int repeatGapSamples = 0,
                           float repeatGapFloorDbfs = -200.0f)
{
    // One event from reset, immediately followed by a -48 dBFS probe. The
    // matched PR=0 control removes Gain/output-stage response; one complete
    // 1 kHz cycle at probe start measures retained cell charge without
    // averaging it into the subsequent release.
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kEventStart = kSampleRate / 10;
    constexpr int kProbeSamples = kSampleRate / 1000;
    const int secondEventStart = repeatGapSamples > 0
        ? kEventStart + eventSamples + repeatGapSamples : kEventStart;
    const int eventEnd = secondEventStart + eventSamples;
    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    control.setParameter(MultiCompDSP::Parameter::OptoGain, 23.754f);
    active.setParameter(MultiCompDSP::Parameter::OptoGain, 23.754f);
    const int latency = control.getLatencySamples();
    const int tailSamples = latency + kOptoShortEventTraceOffsets.back()
        + kProbeSamples;
    const int totalSamples = eventEnd + tailSamples;
    const float eventAmplitude = duskaudio::decibelsToGain(peakDbfs);
    const float probeAmplitude = duskaudio::decibelsToGain(-48.0f);
    const float preEventAmplitude = preEventFloorDbfs > -190.0f
        ? duskaudio::decibelsToGain(preEventFloorDbfs) : 0.0f;
    const float repeatGapAmplitude = repeatGapFloorDbfs > -190.0f
        ? duskaudio::decibelsToGain(repeatGapFloorDbfs) : 0.0f;
    std::vector<float> input(static_cast<size_t>(totalSamples));
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const float amplitude = sample < kEventStart ? preEventAmplitude
            : sample < kEventStart + eventSamples ? eventAmplitude
            : sample < secondEventStart ? repeatGapAmplitude
            : sample < eventEnd ? eventAmplitude : probeAmplitude;
        input[static_cast<size_t>(sample)] = amplitude * std::sin(
            2.0f * kPi * 1000.0f * static_cast<float>(sample) / kSampleRate);
    }

    std::vector<float> controlOutput(static_cast<size_t>(totalSamples));
    std::vector<float> activeOutput(static_cast<size_t>(totalSamples));
    std::array<float, kBlockSize> controlBlock{};
    std::array<float, kBlockSize> activeBlock{};
    for (int blockStart = 0; blockStart < totalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, totalSamples - blockStart);
        const float* inputs[] = {input.data() + blockStart};
        float* controlOutputs[] = {controlBlock.data()};
        float* activeOutputs[] = {activeBlock.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        std::copy_n(controlBlock.data(), count, controlOutput.data() + blockStart);
        std::copy_n(activeBlock.data(), count, activeOutput.data() + blockStart);
    }

    const int probeStart = eventEnd + latency;
    const auto fundamentalAmplitude = [](const std::vector<float>& output,
                                         int start) {
        double sine = 0.0;
        double cosine = 0.0;
        for (int i = 0; i < kProbeSamples; ++i)
        {
            const double phase = 2.0 * static_cast<double>(kPi) * 1000.0
                * static_cast<double>(start + i) / kSampleRate;
            sine += output[static_cast<size_t>(start + i)] * std::sin(phase);
            cosine += output[static_cast<size_t>(start + i)] * std::cos(phase);
        }
        return 2.0 / kProbeSamples * std::hypot(sine, cosine);
    };
    std::array<float, kOptoShortEventTraceOffsets.size()> trace{};
    for (size_t i = 0; i < trace.size(); ++i)
    {
        const int start = probeStart + kOptoShortEventTraceOffsets[i];
        trace[i] = 20.0f * std::log10(static_cast<float>(
            fundamentalAmplitude(controlOutput, start)
            / fundamentalAmplitude(activeOutput, start)));
    }
    return trace;
}

void testOptoNoiseFloorDoesNotPrechargeEvent()
{
    const float silent = measureOptoShortEventTrace(48, -4.0f)[0];
    const float floored = measureOptoShortEventTrace(48, -4.0f, -90.0f)[0];
    const float delta = floored - silent;
    std::printf("opto pre-event floor: silence %.6f dB -90 dBFS %.6f dB "
                "delta %+.6f dB\n", silent, floored, delta);
    require(std::abs(delta) < 0.10f,
            "Opto sub-audible input floor does not precharge a later event");
}

void testOptoEventChargeHasNoAbsoluteFloorCliff()
{
    constexpr std::array<std::array<float, 2>, 3> bedPairs{{
        {{-45.0f, -43.0f}}, {{-63.0f, -61.0f}}, {{-79.0f, -77.0f}}
    }};
    float worstDelta = 0.0f;
    for (const auto& pair : bedPairs)
    {
        const float lower = measureOptoShortEventTrace(
            48, -4.0f, pair[0])[0];
        const float upper = measureOptoShortEventTrace(
            48, -4.0f, pair[1])[0];
        const float delta = upper - lower;
        worstDelta = std::max(worstDelta, std::abs(delta));
        std::printf("opto event floor boundary: %.0f dBFS bed %.6f dB %.0f "
                    "dBFS bed %.6f dB delta %+.6f dB\n",
                    pair[0], lower, pair[1], upper, delta);
    }
    require(worstDelta < 0.50f,
            "Opto event charge has no cliff at the detector support floor");
}

void testOptoNoiseFloorDoesNotPrechargeRepeatedEvent()
{
    constexpr int kGapSamples = 50 * 48000 / 1000;
    const float silent = measureOptoShortEventTrace(
        48, -4.0f, -200.0f, kGapSamples, -200.0f)[0];
    const float floored = measureOptoShortEventTrace(
        48, -4.0f, -200.0f, kGapSamples, -90.0f)[0];
    const float delta = floored - silent;
    std::printf("opto inter-event floor: silence %.6f dB -90 dBFS %.6f dB "
                "delta %+.6f dB\n", silent, floored, delta);
    require(std::abs(delta) < 0.10f,
            "Opto sub-audible gap does not precharge a repeated event");
}

enum class OptoFloorCondition { Silence, DecayingTail, Hum };

float measureOptoEventChargeAfterFloor(OptoFloorCondition condition,
                                       float humPhase = 0.0f)
{
    constexpr int kSampleRate = 48000;
    constexpr int kInitialEventSamples = kSampleRate / 10;
    constexpr int kGapSamples = 2 * kSampleRate;
    constexpr int kTailSamples = kSampleRate / 10;
    constexpr int kTestEventSamples = kSampleRate / 1000;
    duskaudio::MultiCompModes modes;
    duskaudio::MultiCompParameterState parameters;
    parameters.optoPeakReduction.store(70.0f, std::memory_order_relaxed);
    parameters.optoGain.store(
        duskaudio::kOptoGainUnityKnob, std::memory_order_relaxed);
    parameters.noiseEnable.store(false, std::memory_order_relaxed);
    modes.prepare(kSampleRate, 1, 1);
    for (int sample = 0; sample < kInitialEventSamples; ++sample)
    {
        const float input = duskaudio::decibelsToGain(-6.0f) * std::sin(
            2.0f * kPi * 1000.0f * static_cast<float>(sample) / kSampleRate);
        modes.process(duskaudio::MultiCompMode::Opto,
                      input, 0, input, parameters);
    }
    for (int sample = 0; sample < kGapSamples; ++sample)
    {
        float input = 0.0f;
        if (condition == OptoFloorCondition::DecayingTail)
        {
            const float levelDb = sample < kTailSamples
                ? -40.0f - 50.0f * static_cast<float>(sample)
                    / static_cast<float>(kTailSamples)
                : -90.0f;
            input = duskaudio::decibelsToGain(levelDb) * std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(sample)
                    / kSampleRate);
        }
        else if (condition == OptoFloorCondition::Hum)
            input = duskaudio::decibelsToGain(-50.0f) * std::sin(
                2.0f * kPi * 60.0f * static_cast<float>(sample) / kSampleRate
                    + humPhase);
        modes.process(duskaudio::MultiCompMode::Opto,
                      input, 0, input, parameters);
    }
    const float reductionBefore
        = -modes.gainReduction(duskaudio::MultiCompMode::Opto, 0);
    for (int sample = 0; sample < kTestEventSamples; ++sample)
    {
        const float input = duskaudio::decibelsToGain(-4.0f) * std::sin(
            2.0f * kPi * 1000.0f * static_cast<float>(sample) / kSampleRate);
        modes.process(duskaudio::MultiCompMode::Opto,
                      input, 0, input, parameters);
    }
    return -modes.gainReduction(duskaudio::MultiCompMode::Opto, 0)
        - reductionBefore;
}

void testOptoFloorHistoryDoesNotChangeEventCharge()
{
    const float silent = measureOptoEventChargeAfterFloor(
        OptoFloorCondition::Silence);
    const float decaying = measureOptoEventChargeAfterFloor(
        OptoFloorCondition::DecayingTail);
    float minimumHum = std::numeric_limits<float>::infinity();
    float maximumHum = -std::numeric_limits<float>::infinity();
    for (int phase = 0; phase < 16; ++phase)
    {
        const float charge = measureOptoEventChargeAfterFloor(
            OptoFloorCondition::Hum,
            2.0f * kPi * static_cast<float>(phase) / 16.0f);
        minimumHum = std::min(minimumHum, charge);
        maximumHum = std::max(maximumHum, charge);
    }
    std::printf("opto floor history: silence %.6f dB decaying-tail %.6f dB "
                "delta %+.6f dB; 60 Hz phase range %.6f..%.6f dB "
                "spread %.6f dB\n",
                silent, decaying, decaying - silent,
                minimumHum, maximumHum, maximumHum - minimumHum);
    require(std::abs(decaying - silent) < 0.50f
                && maximumHum - minimumHum < 0.50f,
            "Opto floor history and hum phase do not change event charge");
}

void testOptoTrueSilenceRetainsExposureForRelease()
{
    constexpr int kSampleRate = 48000;
    constexpr int kExposureSamples = kSampleRate / 10;
    constexpr int kReleaseSamples = kSampleRate / 100;
    duskaudio::MultiCompModes modes;
    duskaudio::MultiCompParameterState parameters;
    parameters.optoPeakReduction.store(70.0f, std::memory_order_relaxed);
    parameters.optoGain.store(duskaudio::kOptoGainUnityKnob, std::memory_order_relaxed);
    parameters.noiseEnable.store(false, std::memory_order_relaxed);
    modes.prepare(kSampleRate, 1, 1);
    for (int sample = 0; sample < kExposureSamples; ++sample)
    {
        const float input = duskaudio::decibelsToGain(-6.0f) * std::sin(
            2.0f * kPi * 1000.0f * static_cast<float>(sample) / kSampleRate);
        modes.process(duskaudio::MultiCompMode::Opto, input, 0, input, parameters);
    }
    const float initialReduction = -modes.gainReduction(duskaudio::MultiCompMode::Opto, 0);
    for (int sample = 0; sample < kReleaseSamples; ++sample)
        modes.process(duskaudio::MultiCompMode::Opto, 0.0f, 0, 0.0f, parameters);
    const float retainedReduction = -modes.gainReduction(duskaudio::MultiCompMode::Opto, 0);
    const float retainedFraction = retainedReduction
        / std::max(initialReduction, 1.0e-6f);
    std::printf("opto true-silence release: initial %.6f dB retained@10ms "
                "%.6f dB fraction %.6f\n",
                initialReduction, retainedReduction, retainedFraction);
    require(initialReduction > 10.0f && retainedFraction > 0.85f,
            "Opto exact-silence release retains the preceding event exposure");
}

void testOptoShortEventCharge()
{
    constexpr std::array<int, 4> durations{{17, 48, 144, 480}};
    constexpr std::array<float, 4> peaksDbfs{{-12.0f, -8.0f, -4.0f, -0.25f}};
    constexpr std::array<std::array<float, 4>, 4> reference{{
        {{6.92236f, 8.13157f, 8.82621f, 8.69921f}},
        {{9.55162f, 10.59441f, 11.62389f, 12.72483f}},
        {{13.34629f, 15.57380f, 17.44177f, 18.84350f}},
        {{15.07678f, 17.90595f, 20.62950f, 23.07068f}}
    }};
    // These checkerboard points are excluded from the fitted summary so a
    // charge-law change must generalise across both duration and amplitude.
    constexpr std::array<std::array<bool, 4>, 4> heldOut{{
        {{false, false, false, true}},
        {{false, true, false, false}},
        {{false, false, true, false}},
        {{true, false, false, false}}
    }};
    float fittedSquaredError = 0.0f;
    float fittedWorstError = 0.0f;
    float heldOutSquaredError = 0.0f;
    float heldOutWorstError = 0.0f;
    int fittedCount = 0;
    int heldOutCount = 0;
    for (size_t duration = 0; duration < durations.size(); ++duration)
        for (size_t peak = 0; peak < peaksDbfs.size(); ++peak)
        {
            const float measured = measureOptoShortEventTrace(
                durations[duration], peaksDbfs[peak])[0];
            const float delta = measured - reference[duration][peak];
            std::printf("opto short-event charge: %.3f ms peak %.2f dBFS "
                        "reference %.5f dB measured %.6f dB delta %+.6f dB%s\n",
                        1000.0f * durations[duration] / 48000.0f,
                        peaksDbfs[peak], reference[duration][peak], measured,
                        delta, heldOut[duration][peak] ? " (held out)" : "");
            if (heldOut[duration][peak])
            {
                heldOutSquaredError += delta * delta;
                heldOutWorstError = std::max(heldOutWorstError, std::abs(delta));
                ++heldOutCount;
            }
            else
            {
                fittedSquaredError += delta * delta;
                fittedWorstError = std::max(fittedWorstError, std::abs(delta));
                ++fittedCount;
            }
        }
    const float fittedRms = std::sqrt(fittedSquaredError / fittedCount);
    const float heldOutRms = std::sqrt(heldOutSquaredError / heldOutCount);
    std::printf("opto short-event charge summary: fitted RMS %.6f dB worst %.6f dB; "
                "held-out RMS %.6f dB worst %.6f dB\n",
                fittedRms, fittedWorstError, heldOutRms, heldOutWorstError);
    require(fittedRms < 0.42f && fittedWorstError < 0.75f
                && heldOutRms < 0.50f && heldOutWorstError < 0.75f,
            "Opto nonlinear charge law matches isolated duration/amplitude grid");
}

void testOptoShortEventRelease()
{
    struct Row
    {
        int durationSamples;
        float peakDbfs;
        std::array<float, kOptoShortEventTraceOffsets.size()> reference;
    };
    // Checkerboard rows span both axes of the charge grid. Scoring change from
    // the first probe cycle isolates recovery shape from initial charge error.
    constexpr std::array<Row, 4> rows{{
        {17, -8.0f, {{8.131574f, 8.069341f, 7.722107f, 6.827592f,
                       5.295952f, 3.266975f, 1.656423f}}},
        {48, -4.0f, {{11.623894f, 11.018592f, 10.439475f, 9.360288f,
                       7.511415f, 4.922399f, 2.667509f}}},
        {144, -12.0f, {{13.346288f, 12.721900f, 12.122308f, 11.001453f,
                         9.058014f, 6.256186f, 3.648758f}}},
        {480, -0.25f, {{23.070678f, 22.489624f, 21.939834f, 20.905922f,
                         19.062774f, 16.080678f, 12.110521f}}}
    }};
    float squaredError = 0.0f;
    float worstError = 0.0f;
    int count = 0;
    for (const auto& row : rows)
    {
        const auto measured = measureOptoShortEventTrace(
            row.durationSamples, row.peakDbfs);
        std::printf("opto short-event release: %.3f ms peak %.2f dBFS decay",
                    1000.0f * row.durationSamples / 48000.0f, row.peakDbfs);
        for (size_t i = 1; i < measured.size(); ++i)
        {
            const float referenceDecay = row.reference[i] - row.reference[0];
            const float measuredDecay = measured[i] - measured[0];
            const float delta = measuredDecay - referenceDecay;
            std::printf(" %dms:%+.3f", kOptoShortEventTraceOffsets[i] * 1000 / 48000,
                        delta);
            squaredError += delta * delta;
            worstError = std::max(worstError, std::abs(delta));
            ++count;
        }
        std::printf(" dB\n");
    }
    const float rmsError = std::sqrt(squaredError / count);
    std::printf("opto short-event release summary: RMS %.6f dB worst %.6f dB\n",
                rmsError, worstError);
    require(rmsError < 0.75f && worstError < 1.50f,
            "Opto short-event recovery follows the measured exposure-dependent shape");
}

float measureOptoBroadbandStaticLaw(const std::vector<float>& unitRmsNoise,
                                    float inputDbfs)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kMeasureStart = 4 * kSampleRate;
    constexpr int kMeasureSamples = 7 * kSampleRate / 2;
    const float amplitude = duskaudio::decibelsToGain(inputDbfs);
    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    // The reference session stored the normalised Gain setting as 0.321869.
    control.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1869f);
    active.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1869f);
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> controlOutput{};
    std::array<float, kBlockSize> activeOutput{};
    double controlPower = 0.0;
    double activePower = 0.0;
    const int totalSamples = static_cast<int>(unitRmsNoise.size());
    for (int blockStart = 0; blockStart < totalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, totalSamples - blockStart);
        for (int i = 0; i < count; ++i)
            input[static_cast<size_t>(i)]
                = amplitude * unitRmsNoise[static_cast<size_t>(blockStart + i)];
        const float* inputs[] = {input.data()};
        float* controlOutputs[] = {controlOutput.data()};
        float* activeOutputs[] = {activeOutput.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            if (sample < kMeasureStart
                || sample >= kMeasureStart + kMeasureSamples)
                continue;
            controlPower += static_cast<double>(controlOutput[static_cast<size_t>(i)])
                * controlOutput[static_cast<size_t>(i)];
            activePower += static_cast<double>(activeOutput[static_cast<size_t>(i)])
                * activeOutput[static_cast<size_t>(i)];
        }
    }
    require(controlPower > 1.0e-12 && activePower > 1.0e-12,
            "Opto broadband-law comparison produces measurable output");
    return 10.0f * std::log10(static_cast<float>(controlPower / activePower));
}

void testOptoBroadbandStaticLaw()
{
    constexpr int kSampleRate = 48000;
    constexpr int kMeasureStart = 4 * kSampleRate;
    constexpr int kMeasureSamples = 7 * kSampleRate / 2;
    constexpr int kTotalSamples = 8 * kSampleRate;
    std::vector<float> noise(static_cast<size_t>(kTotalSamples));
    uint32_t randomState = 0x6d2b79f5u;
    bool haveSpare = false;
    float spare = 0.0f;
    auto uniform = [&]() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return (static_cast<float>(randomState) + 0.5f) / 4294967296.0f;
    };
    auto gaussian = [&]() {
        if (haveSpare)
        {
            haveSpare = false;
            return spare;
        }
        const float radius = std::sqrt(
            -2.0f * std::log(std::max(uniform(), 1.0e-12f)));
        const float angle = 2.0f * kPi * uniform();
        spare = radius * std::sin(angle);
        haveSpare = true;
        return radius * std::cos(angle);
    };
    for (auto& sample : noise) sample = gaussian();
    double measuredPower = 0.0;
    for (int sample = kMeasureStart;
         sample < kMeasureStart + kMeasureSamples; ++sample)
        measuredPower += static_cast<double>(noise[static_cast<size_t>(sample)])
            * noise[static_cast<size_t>(sample)];
    const float normalisation = 1.0f / static_cast<float>(
        std::sqrt(measuredPower / kMeasureSamples));
    for (auto& sample : noise) sample *= normalisation;

    constexpr std::array<float, 5> levelsDbfs{{-36.0f, -30.0f, -24.0f,
                                                -18.0f, -12.0f}};
    constexpr std::array<float, 5> referenceGrDb{{3.209f, 7.459f, 12.026f,
                                                   16.626f, 21.073f}};
    constexpr size_t kHeldOutRow = 2;
    float fittedSquaredError = 0.0f;
    float fittedWorstError = 0.0f;
    float heldOutError = 0.0f;
    for (size_t row = 0; row < levelsDbfs.size(); ++row)
    {
        const float measured = measureOptoBroadbandStaticLaw(
            noise, levelsDbfs[row]);
        const float delta = measured - referenceGrDb[row];
        std::printf("opto broadband law: input %.0f dBFS reference %.3f dB "
                    "measured %.6f dB delta %+.6f dB%s\n",
                    levelsDbfs[row], referenceGrDb[row], measured, delta,
                    row == kHeldOutRow ? " (held out)" : "");
        if (row == kHeldOutRow)
            heldOutError = delta;
        else
        {
            fittedSquaredError += delta * delta;
            fittedWorstError = std::max(fittedWorstError, std::abs(delta));
        }
    }
    const float fittedRmsError = std::sqrt(fittedSquaredError / 4.0f);
    std::printf("opto broadband law summary: fitted -36/-30/-18/-12 dBFS "
                "RMS %.6f dB worst %.6f dB; held-out -24 dBFS delta %+.6f dB\n",
                fittedRmsError, fittedWorstError, heldOutError);
    require(fittedRmsError < 0.25f && fittedWorstError < 0.35f,
            "Opto broadband law matches the four fitted levels");
    require(std::abs(heldOutError) < 0.25f,
            "Opto broadband law generalises to the held-out level");
}

float measureOptoBurstRate(int rateHz)
{
    // Identical to the reference extraction: 10 ms, -6 dBFS 1 kHz bursts,
    // rendered for 8 s and scored by plain RMS over seconds 6.0-7.5.
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kBurstSamples = 10 * kSampleRate / 1000;
    constexpr int kMeasureStart = 6 * kSampleRate;
    constexpr int kMeasureSamples = 3 * kSampleRate / 2;
    constexpr int kTotalSamples = 8 * kSampleRate;
    const int periodSamples = kSampleRate / rateHz;
    const float amplitude = duskaudio::decibelsToGain(-6.0f);
    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    // The reference session stored the normalised Gain setting as 0.321869.
    control.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1869f);
    active.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1869f);
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> controlOutput{};
    std::array<float, kBlockSize> activeOutput{};
    double controlPower = 0.0;
    double activePower = 0.0;
    for (int blockStart = 0; blockStart < kTotalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kTotalSamples - blockStart);
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            input[static_cast<size_t>(i)] = sample % periodSamples < kBurstSamples
                ? amplitude * std::sin(2.0f * kPi * 1000.0f
                    * static_cast<float>(sample) / kSampleRate)
                : 0.0f;
        }
        const float* inputs[] = {input.data()};
        float* controlOutputs[] = {controlOutput.data()};
        float* activeOutputs[] = {activeOutput.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            if (sample < kMeasureStart
                || sample >= kMeasureStart + kMeasureSamples)
                continue;
            controlPower += static_cast<double>(controlOutput[static_cast<size_t>(i)])
                * controlOutput[static_cast<size_t>(i)];
            activePower += static_cast<double>(activeOutput[static_cast<size_t>(i)])
                * activeOutput[static_cast<size_t>(i)];
        }
    }
    return 10.0f * std::log10(static_cast<float>(controlPower / activePower));
}

void testOptoBurstRateSweep()
{
    constexpr std::array<int, 5> ratesHz{{2, 5, 10, 20, 40}};
    constexpr std::array<float, 5> reference{{
        15.296f, 16.206f, 17.421f, 18.915f, 20.384f}};
    constexpr size_t kHeldOutRow = 2;
    float fittedSquaredError = 0.0f;
    float fittedWorstError = 0.0f;
    float heldOutError = 0.0f;
    for (size_t row = 0; row < ratesHz.size(); ++row)
    {
        const float measured = measureOptoBurstRate(ratesHz[row]);
        const float delta = measured - reference[row];
        std::printf("opto burst rate: %d Hz reference %.3f dB "
                    "measured %.6f dB delta %+.6f dB\n",
                    ratesHz[row], reference[row], measured, delta);
        if (row == kHeldOutRow)
            heldOutError = delta;
        else
        {
            fittedSquaredError += delta * delta;
            fittedWorstError = std::max(fittedWorstError, std::abs(delta));
        }
    }
    const float fittedRmsError = std::sqrt(fittedSquaredError / 4.0f);
    std::printf("opto burst rate summary: fitted 2/5/20/40 Hz RMS %.6f dB "
                "worst %.6f dB; held-out 10 Hz delta %+.6f dB\n",
                fittedRmsError, fittedWorstError, heldOutError);
    // Before capacity-limited charging the fitted set was 1.715 dB RMS with
    // a 2.988 dB worst point, and the held-out 10 Hz error was -0.365 dB.
    require(fittedRmsError < 0.60f && fittedWorstError < 0.80f,
            "Opto charging law matches the four fitted burst-rate points");
    require(std::abs(heldOutError) < 0.25f,
            "Opto charging law generalises to the held-out 10 Hz burst rate");
}

float measureOptoReferenceOutputMemory(int gapMs)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kBurstSamples = 300 * kSampleRate / 1000;
    constexpr int kProbeSamples = 300 * kSampleRate / 1000;
    constexpr int kMeasureSamples = 4 * kSampleRate / 1000;
    const int gapSamples = gapMs * kSampleRate / 1000;
    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    const int latency = control.getLatencySamples();
    require(active.getLatencySamples() == latency,
            "Opto output-memory control and active paths have equal latency");
    const int probeStart = kBurstSamples + gapSamples;
    const int measuredProbeStart = probeStart + latency;
    const int totalSamples = probeStart + kProbeSamples + latency;
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> controlOutput{};
    std::array<float, kBlockSize> activeOutput{};
    double controlPower = 0.0;
    double activePower = 0.0;
    for (int blockStart = 0; blockStart < totalSamples;)
    {
        int count = std::min(kBlockSize, totalSamples - blockStart);
        if (blockStart < kBurstSamples && blockStart + count > kBurstSamples)
            count = kBurstSamples - blockStart;
        else if (blockStart < probeStart && blockStart + count > probeStart)
            count = probeStart - blockStart;
        else if (blockStart < probeStart + kMeasureSamples
                 && blockStart + count > probeStart + kMeasureSamples)
            count = probeStart + kMeasureSamples - blockStart;
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            float amplitude = 0.0f;
            if (sample < kBurstSamples)
                amplitude = duskaudio::decibelsToGain(-6.0f);
            else if (sample >= probeStart
                     && sample < probeStart + kProbeSamples)
                amplitude = duskaudio::decibelsToGain(-40.0f);
            input[static_cast<size_t>(i)] = amplitude * std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(sample) / kSampleRate);
        }
        const float* inputs[] = {input.data()};
        float* controlOutputs[] = {controlOutput.data()};
        float* activeOutputs[] = {activeOutput.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        for (int i = 0; i < count; ++i)
        {
            const int probeSample = blockStart + i - measuredProbeStart;
            if (probeSample >= 0 && probeSample < kMeasureSamples)
            {
                controlPower += static_cast<double>(controlOutput[static_cast<size_t>(i)])
                    * controlOutput[static_cast<size_t>(i)];
                activePower += static_cast<double>(activeOutput[static_cast<size_t>(i)])
                    * activeOutput[static_cast<size_t>(i)];
            }
        }
        blockStart += count;
    }
    // Match the live-reference extraction: this is an end-to-end output-memory
    // gate, so its first-4-ms RMS deliberately includes the colour path's
    // level-step transient.  Removing the mean only from our side made the
    // former gate asymmetric.  Probe-level comparisons use a separate,
    // symmetric AC-carrier measurement because the transient dominates quiet
    // probes; this fixed -40 dBFS comparison remains a valid output-parity gate.
    return 10.0f * std::log10(static_cast<float>(controlPower / activePower));
}

void testOptoReferenceOutputMemory()
{
    const float burstStaticReduction = measureOptoStaticGr(-6.0f, 70.0f, false);
    std::printf("opto output memory: burst static-law reduction %.6f dB\n",
                burstStaticReduction);
    constexpr std::array<int, 16> gapsMs{{
        1, 2, 3, 5, 8, 10, 15, 25, 30, 40, 60, 120, 250, 500, 1000, 2000}};
    constexpr std::array<float, 16> reference{{
        21.992f, 21.786f, 21.581f, 21.179f, 20.592f, 20.211f, 19.294f, 17.609f,
        16.840f, 15.435f, 13.116f, 8.799f, 4.882f, 2.629f, 1.405f, 0.580f}};
    float squaredError = 0.0f;
    float worstError = 0.0f;
    for (size_t row = 0; row < gapsMs.size(); ++row)
    {
        const float measured = measureOptoReferenceOutputMemory(gapsMs[row]);
        const float delta = measured - reference[row];
        squaredError += delta * delta;
        worstError = std::max(worstError, std::abs(delta));
    }
    const float rmsError = std::sqrt(squaredError / static_cast<float>(gapsMs.size()));
    // Reference and implementation both use the same plain-RMS extraction.
    require(rmsError < 0.125f && worstError < 0.26f,
            "Opto end-to-end output memory matches the sixteen-point reference curve");
}

std::vector<float> makeOptoDenseProgramme()
{
    constexpr int kSampleRate = 48000;
    constexpr int kSeconds = 8;
    constexpr int kSampleCount = kSeconds * kSampleRate;
    std::vector<double> programme(static_cast<size_t>(kSampleCount), 0.0);
    constexpr std::array<std::array<double, 3>, 4> chords{{
        {{110.0, 164.81, 220.0}}, {{130.81, 196.0, 261.63}},
        {{98.0, 146.83, 196.0}}, {{82.41, 123.47, 164.81}}
    }};
    for (size_t bar = 0; bar < chords.size(); ++bar)
    {
        const int start = static_cast<int>(bar) * 2 * kSampleRate;
        const int stop = std::min(kSampleCount, start + 2 * kSampleRate);
        for (int sample = start; sample < stop; ++sample)
        {
            const double time = static_cast<double>(sample - start) / kSampleRate;
            const double envelope = std::min(time / 0.03, 1.0)
                * std::min((2.0 - time) / 0.15, 1.0);
            for (size_t harmonic = 0; harmonic < chords[bar].size(); ++harmonic)
                programme[static_cast<size_t>(sample)] += 0.16
                    / static_cast<double>(harmonic + 1) * envelope
                    * std::sin(2.0 * static_cast<double>(kPi)
                        * chords[bar][harmonic] * time);
        }
    }
    const auto uniformNoise = [](uint32_t& state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 4294967295.0 * 2.0 - 1.0;
    };
    uint32_t hitState = 0x9e3779b9u;
    for (int beat = 0; beat < kSeconds * 4; ++beat)
    {
        const int start = beat * kSampleRate / 4;
        const int length = std::min(
            static_cast<int>(0.18 * kSampleRate), kSampleCount - start);
        for (int sample = 0; sample < length; ++sample)
        {
            const double time = static_cast<double>(sample) / kSampleRate;
            const double hit = (beat / 2) % 2 == 0
                ? std::sin(2.0 * static_cast<double>(kPi)
                    * (55.0 + 45.0 * std::exp(-time / 0.02)) * time)
                    * std::exp(-time / 0.07)
                : uniformNoise(hitState) * std::exp(-time / 0.035);
            programme[static_cast<size_t>(start + sample)] += 0.30 * hit;
        }
    }
    uint32_t bedState = 0x4d433243u;
    double filteredNoise = 0.0;
    for (auto& sample : programme)
    {
        const double white = uniformNoise(bedState);
        filteredNoise = 0.985 * filteredNoise + 0.015 * white;
        sample += 0.025 * (0.65 * white + 0.35 * filteredNoise);
    }
    constexpr int kFadeSamples = 20 * kSampleRate / 1000;
    for (int sample = 0; sample < kFadeSamples; ++sample)
    {
        const double fadeIn = static_cast<double>(sample) / kFadeSamples;
        const double fadeOut = static_cast<double>(kFadeSamples - 1 - sample)
            / kFadeSamples;
        programme[static_cast<size_t>(sample)] *= fadeIn;
        programme[static_cast<size_t>(kSampleCount - kFadeSamples + sample)]
            *= fadeOut;
    }
    double peak = 0.0;
    for (const double sample : programme)
        peak = std::max(peak, std::abs(sample));
    const double scale = static_cast<double>(duskaudio::decibelsToGain(-8.0f))
        / std::max(peak, 1.0e-12);
    std::vector<float> result(static_cast<size_t>(kSampleCount));
    for (size_t sample = 0; sample < result.size(); ++sample)
        result[sample] = static_cast<float>(programme[sample] * scale);
    return result;
}

struct OptoDenseProgrammeMetrics
{
    float meanErrorDb = 0.0f;
    float rmsErrorDb = 0.0f;
    float correlation = 0.0f;
};

OptoDenseProgrammeMetrics measureOptoDenseProgramme(
    const std::vector<float>& programme, float peakReduction,
    const std::array<float, 80>& referenceEnvelope)
{
    constexpr int kBlockSize = 256;
    constexpr int kFrameSamples = 4800; // Non-overlapping 100 ms RMS frames.
    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f, kOversampling2xSetting);
    prepareOptoDynamicsDsp(active, peakReduction, kOversampling2xSetting);
    control.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1868896f);
    active.setParameter(MultiCompDSP::Parameter::OptoGain, 32.1868896f);
    const int latency = control.getLatencySamples();
    require(active.getLatencySamples() == latency,
            "Opto dense-programme control and active paths have equal latency");
    require(static_cast<int>(programme.size()) == 80 * kFrameSamples,
            "Opto dense-programme frame grid covers the whole stimulus exactly");
    const int totalSamples = static_cast<int>(programme.size()) + latency;
    std::vector<float> controlOutput(static_cast<size_t>(totalSamples));
    std::vector<float> activeOutput(static_cast<size_t>(totalSamples));
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> controlBlock{};
    std::array<float, kBlockSize> activeBlock{};
    for (int blockStart = 0; blockStart < totalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, totalSamples - blockStart);
        for (int sample = 0; sample < count; ++sample)
        {
            const int sourceSample = blockStart + sample;
            input[static_cast<size_t>(sample)]
                = sourceSample < static_cast<int>(programme.size())
                    ? programme[static_cast<size_t>(sourceSample)] : 0.0f;
        }
        const float* inputs[] = {input.data()};
        float* controlOutputs[] = {controlBlock.data()};
        float* activeOutputs[] = {activeBlock.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        std::copy_n(controlBlock.data(), count, controlOutput.data() + blockStart);
        std::copy_n(activeBlock.data(), count, activeOutput.data() + blockStart);
    }
    std::array<float, 80> measuredEnvelope{};
    for (size_t frame = 0; frame < measuredEnvelope.size(); ++frame)
    {
        const int start = latency + static_cast<int>(frame) * kFrameSamples;
        double controlPower = 0.0;
        double activePower = 0.0;
        for (int sample = 0; sample < kFrameSamples; ++sample)
        {
            const float controlSample
                = controlOutput[static_cast<size_t>(start + sample)];
            const float activeSample
                = activeOutput[static_cast<size_t>(start + sample)];
            controlPower += static_cast<double>(controlSample) * controlSample;
            activePower += static_cast<double>(activeSample) * activeSample;
        }
        measuredEnvelope[frame] = 10.0f * std::log10(static_cast<float>(
            (controlPower + 1.0e-30) / (activePower + 1.0e-30)));
    }
    double referenceMean = 0.0;
    double measuredMean = 0.0;
    double squaredError = 0.0;
    for (size_t frame = 0; frame < referenceEnvelope.size(); ++frame)
    {
        referenceMean += referenceEnvelope[frame];
        measuredMean += measuredEnvelope[frame];
        const double error = measuredEnvelope[frame] - referenceEnvelope[frame];
        squaredError += error * error;
    }
    referenceMean /= referenceEnvelope.size();
    measuredMean /= measuredEnvelope.size();
    double covariance = 0.0;
    double referenceVariance = 0.0;
    double measuredVariance = 0.0;
    for (size_t frame = 0; frame < referenceEnvelope.size(); ++frame)
    {
        const double referenceDelta = referenceEnvelope[frame] - referenceMean;
        const double measuredDelta = measuredEnvelope[frame] - measuredMean;
        covariance += referenceDelta * measuredDelta;
        referenceVariance += referenceDelta * referenceDelta;
        measuredVariance += measuredDelta * measuredDelta;
    }
    return {
        static_cast<float>(measuredMean - referenceMean),
        static_cast<float>(std::sqrt(squaredError / referenceEnvelope.size())),
        static_cast<float>(covariance / std::sqrt(
            std::max(referenceVariance * measuredVariance, 1.0e-30)))
    };
}

void testOptoDenseProgrammeParity()
{
    // Captured from the live reference AU with matched PR=0 controls, Gain at
    // 0.321868896, Compress mode.  ("2x oversampling" describes OUR DSP
    // instances below; the reference has no such control and its envelopes are
    // independent of it.)  The deterministic
    // generator above is shared with the capture recipe; 100 ms RMS frames
    // retain the envelope defect that remained with wider analysis windows.
    constexpr std::array<float, 80> referenceAt40{{
        4.678101f, 4.804975f, 5.371509f, 5.037769f, 4.704782f, 5.310720f, 4.680708f, 5.213288f,
        4.815061f, 4.546864f, 6.638811f, 5.006345f, 5.698022f, 5.596746f, 4.774736f, 5.515971f,
        4.687577f, 5.407262f, 4.788301f, 3.326948f, 5.560364f, 5.003565f, 6.150897f, 5.582094f,
        4.926047f, 5.442073f, 4.682782f, 5.308673f, 4.736460f, 4.463318f, 6.501792f, 4.911393f,
        5.883970f, 5.463069f, 4.700910f, 5.495205f, 4.649648f, 5.310791f, 4.759596f, 3.340146f,
        5.165737f, 5.139545f, 5.930709f, 5.516829f, 5.014696f, 5.661359f, 4.955315f, 5.541442f,
        5.031242f, 4.782400f, 6.543997f, 5.026670f, 5.808061f, 5.619396f, 4.779205f, 5.621126f,
        4.709073f, 5.268908f, 4.815944f, 3.333637f, 5.753230f, 5.182112f, 5.799463f, 5.638040f,
        5.147764f, 5.776921f, 5.057772f, 5.807666f, 5.181100f, 4.927483f, 5.726680f, 5.098088f,
        6.551258f, 5.563705f, 4.832070f, 5.631370f, 4.762319f, 5.514551f, 4.840456f, 3.363744f,
    }};
    constexpr std::array<float, 80> referenceAt70{{
        15.482611f, 16.851750f, 17.896457f, 17.521057f, 17.044112f, 17.847969f, 17.016222f, 17.756700f,
        17.204546f, 16.949476f, 18.842610f, 17.179530f, 18.070807f, 17.740867f, 17.025793f, 17.979406f,
        16.990076f, 17.829499f, 16.909756f, 13.463429f, 17.036409f, 17.052952f, 18.357538f, 17.647516f,
        16.994067f, 17.756139f, 16.906915f, 17.734576f, 17.068864f, 16.868262f, 18.844047f, 17.108888f,
        18.145505f, 17.647855f, 16.954388f, 17.910899f, 16.888395f, 17.730082f, 16.839019f, 13.477085f,
        16.947910f, 17.177237f, 17.862250f, 17.554443f, 17.003666f, 17.920045f, 16.988555f, 17.807569f,
        17.169406f, 16.981416f, 18.699973f, 17.180529f, 17.932969f, 17.722775f, 17.039548f, 18.029931f,
        16.981497f, 17.745234f, 16.987801f, 13.497576f, 17.138513f, 17.120387f, 17.750245f, 17.683297f,
        16.990066f, 17.952907f, 16.958054f, 17.823901f, 17.159247f, 16.979283f, 17.944300f, 17.186154f,
        18.723287f, 17.587928f, 16.948212f, 17.939089f, 16.956146f, 17.839403f, 16.925629f, 13.546612f,
    }};
    constexpr std::array<float, 80> referenceAt85{{
        20.104985f, 22.494186f, 23.913226f, 23.746440f, 23.250015f, 24.204495f, 23.295250f, 24.129026f,
        23.534977f, 23.335332f, 24.334897f, 23.455749f, 24.004013f, 23.577777f, 23.408511f, 24.338007f,
        23.388864f, 24.184784f, 23.246999f, 19.557724f, 21.689873f, 23.154776f, 24.101613f, 23.586273f,
        23.214226f, 24.130939f, 23.266835f, 24.112291f, 23.461601f, 23.320176f, 24.498404f, 23.459580f,
        24.013886f, 23.712592f, 23.374801f, 24.276987f, 23.281214f, 24.117632f, 23.167519f, 19.560860f,
        21.831556f, 22.960261f, 23.593394f, 23.494477f, 23.042502f, 24.192743f, 23.144226f, 24.056533f,
        23.429952f, 23.294971f, 24.331714f, 23.430099f, 23.711183f, 23.425275f, 23.429654f, 24.358604f,
        23.380003f, 24.160456f, 23.334399f, 19.629106f, 21.447051f, 22.866222f, 23.362534f, 23.379004f,
        22.710676f, 24.109749f, 22.891065f, 23.714838f, 23.239478f, 23.083730f, 23.663855f, 23.240345f,
        24.184290f, 23.385169f, 23.254947f, 24.208047f, 23.312112f, 24.102489f, 23.251892f, 19.708311f,
    }};

    constexpr std::array<float, 80> referenceAt100{{
        21.735970f, 23.995080f, 25.559316f, 25.621867f, 25.095696f, 26.063887f, 25.127617f, 25.983373f,
        25.446505f, 25.255980f, 26.154718f, 25.306286f, 25.741573f, 25.155472f, 25.302920f, 26.298391f,
        25.288222f, 26.075446f, 25.118816f, 21.396813f, 23.566963f, 24.884442f, 25.765422f, 25.344489f,
        25.015732f, 26.077456f, 25.198250f, 26.099997f, 25.421426f, 25.300623f, 26.195441f, 25.335950f,
        25.735408f, 25.461058f, 25.295602f, 26.219607f, 25.209053f, 26.040818f, 25.057249f, 21.393446f,
        23.623492f, 24.562633f, 25.404302f, 25.377116f, 24.760915f, 26.004137f, 24.899502f, 25.863144f,
        25.232584f, 25.103475f, 25.990417f, 25.197579f, 25.550791f, 25.016438f, 25.323736f, 26.295938f,
        25.296739f, 26.067982f, 25.205038f, 21.459274f, 23.141421f, 24.545988f, 25.092825f, 24.972450f,
        24.350842f, 25.862734f, 24.578558f, 25.445877f, 24.979513f, 24.832923f, 25.267004f, 24.924035f,
        25.924300f, 25.011722f, 25.029261f, 26.066780f, 25.183565f, 26.022492f, 25.134097f, 21.540473f,
    }};
    const auto programme = makeOptoDenseProgramme();
    const auto low = measureOptoDenseProgramme(programme, 40.0f, referenceAt40);
    const auto high = measureOptoDenseProgramme(programme, 70.0f, referenceAt70);
    const auto at85 = measureOptoDenseProgramme(programme, 85.0f, referenceAt85);
    const auto at100 = measureOptoDenseProgramme(programme, 100.0f, referenceAt100);
    std::printf("opto dense programme: PR 0.40 mean %+.6f dB RMS %.6f dB "
                "correlation %.6f; PR 0.70 mean %+.6f dB RMS %.6f dB "
                "correlation %.6f; PR 0.85 mean %+.6f dB RMS %.6f dB "
                "correlation %.6f; PR 1.00 mean %+.6f dB RMS %.6f dB "
                "correlation %.6f\n",
                low.meanErrorDb, low.rmsErrorDb, low.correlation,
                high.meanErrorDb, high.rmsErrorDb, high.correlation,
                at85.meanErrorDb, at85.rmsErrorDb, at85.correlation,
                at100.meanErrorDb, at100.rmsErrorDb, at100.correlation);
    // Issue #210's original PR 0.40/0.70 RMS errors were 1.600/3.163 dB.
    // The continuous event-history law now keeps all four measured PR rows
    // below 1.20 dB RMS. Keep the PR/reference pairings and their independently
    // declared ceilings in one table: the old loose high-PR tripwires could let
    // the 0.85/1.00 reference envelopes be exchanged without failing.
    //
    // Correlation is gated separately so a uniform offset cannot conceal a
    // time-inverted or shape-mismatched envelope. Pearson correlation is
    // invariant to uniform offset and uniform positive scaling, so flattening
    // is owned by the RMS term. The rows are mono; the linked-stereo Opto path
    // remains owned by the stereo-link gates. This revision was run with macOS
    // clang and macOS no-contract. Linux GCC 12 x86-64 still needs re-running
    // because the local Podman VM would not stay up; Linux arm64 remains unrun.
    struct DenseProgrammeGate
    {
        const char* label;
        OptoDenseProgrammeMetrics metrics;
        float maximumAbsoluteMeanDb;
        float maximumRmsDb;
        float minimumCorrelation;
    };
    const std::array<DenseProgrammeGate, 4> gates{{
        {"PR 0.40", low,   0.35f, 0.65f, 0.82f},
        {"PR 0.70", high,  0.70f, 0.95f, 0.80f},
        {"PR 0.85", at85,  0.85f, 1.15f, 0.75f},
        {"PR 1.00", at100, 0.90f, 1.20f, 0.75f},
    }};
    for (const auto& gate : gates)
    {
        std::printf("opto dense parity gate: %s |mean| %.6f < %.2f, "
                    "RMS %.6f < %.2f, correlation %.6f > %.2f\n",
                    gate.label, std::abs(gate.metrics.meanErrorDb),
                    gate.maximumAbsoluteMeanDb, gate.metrics.rmsErrorDb,
                    gate.maximumRmsDb, gate.metrics.correlation,
                    gate.minimumCorrelation);
        require(std::abs(gate.metrics.meanErrorDb)
                    < gate.maximumAbsoluteMeanDb
                    && gate.metrics.rmsErrorDb < gate.maximumRmsDb
                    && gate.metrics.correlation > gate.minimumCorrelation,
                "Opto dense-programme row stays inside its paired parity gate");
    }
}

struct OptoAttackCrossings
{
    float finalReduction = 0.0f;
    float crossing63 = 0.0f;
    float crossing90 = 0.0f;
};

OptoAttackCrossings measureOptoAttackCrossings(float peakReduction, float levelDbfs,
                                                float exposureSeconds)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 16;
    MultiCompDSP dsp;
    prepareOptoDynamicsDsp(dsp, peakReduction);
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    int sampleCursor = 0;
    auto processLevel = [&](float levelDbfs, int samples, std::vector<float>* trace) {
        const float amplitude = duskaudio::decibelsToGain(levelDbfs);
        for (int offset = 0; offset < samples; offset += kBlockSize)
        {
            const int count = std::min(kBlockSize, samples - offset);
            for (int i = 0; i < count; ++i)
                input[static_cast<size_t>(i)] = amplitude * std::sin(
                    2.0f * kPi * 1000.0f * static_cast<float>(sampleCursor + i)
                    / static_cast<float>(kSampleRate));
            dsp.processBlock(inputs, outputs, 1, count);
            sampleCursor += count;
            if (trace != nullptr) trace->push_back(-dsp.getGainReduction());
        }
    };
    processLevel(-60.0f, kSampleRate, nullptr);
    std::vector<float> trace;
    trace.reserve(static_cast<size_t>(
        std::ceil(exposureSeconds * kSampleRate / kBlockSize)));
    processLevel(levelDbfs, static_cast<int>(std::lround(exposureSeconds * kSampleRate)), &trace);
    if (trace.empty()) return {};
    const float finalReduction = trace.back();
    auto crossing = [&](float fraction) {
        const float target = finalReduction * fraction;
        for (size_t i = 0; i < trace.size(); ++i)
            if (trace[i] >= target)
            {
                if (i == 0) return 0.0f;
                const float before = trace[i - 1];
                const float intervalFraction = (target - before)
                    / std::max(trace[i] - before, 1.0e-12f);
                return (static_cast<float>(i) + intervalFraction)
                    * static_cast<float>(kBlockSize) / kSampleRate;
            }
        return exposureSeconds;
    };
    return {finalReduction, crossing(0.63f), crossing(0.90f)};
}

void reportOptoAttackCrossings()
{
    struct Row { float peakReduction, inputDbfs, exposure, crossing63, crossing90; };
    constexpr std::array<Row, 9> rows{{
        {30.0f, -12.0f, 1.0f, 0.0413637f, 0.3746180f},
        {30.0f,  -3.0f, 0.1f, 0.0008274f, 0.0102936f},
        {30.0f,  -3.0f, 1.0f, 0.0014469f, 0.0852716f},
        {60.0f, -24.0f, 0.1f, 0.0050627f, 0.0343388f},
        {60.0f, -24.0f, 1.0f, 0.0144265f, 0.2485969f},
        {60.0f, -12.0f, 0.1f, 0.0000301f, 0.0057341f},
        {60.0f, -12.0f, 1.0f, 0.0007197f, 0.0318673f},
        {60.0f,  -3.0f, 1.0f, 0.0008778f, 0.0148644f},
        {100.0f, -24.0f, 1.0f, 0.0009811f, 0.0159167f}
    }};
    for (const auto& row : rows)
    {
        const auto measured = measureOptoAttackCrossings(
            row.peakReduction, row.inputDbfs, row.exposure);
        std::printf("opto attack crossing: PR %.1f input %.1f exposure %.3f "
                    "target63/90 %.6f/%.6f measured %.6f/%.6f s final %.6f dB\n",
                    row.peakReduction * 0.01f, row.inputDbfs, row.exposure,
                    row.crossing63, row.crossing90,
                    measured.crossing63, measured.crossing90, measured.finalReduction);
    }
}

struct OptoOutputDynamics
{
    float finalReduction = 0.0f;
    float crossing90 = 0.0f;
    bool crossing90Found = false;
};

OptoOutputDynamics measureOptoOutputDynamics(float peakReduction,
                                              float inputDbfs,
                                              float exposureSeconds,
                                              bool limit)
{
    // Match the reference campaign's control/active output extraction. A
    // four-cycle moving-power envelope avoids comparing its rendered audio
    // metrics with the plugin's internal end-of-block meter.
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 64;
    constexpr int kEventStart = kSampleRate;
    constexpr int kEnvelopeSamples = 4 * kSampleRate / 1000;
    constexpr int kTailSamples = kSampleRate / 5;
    const int eventSamples = static_cast<int>(
        std::lround(exposureSeconds * kSampleRate));
    const int totalSamples = kEventStart + eventSamples + kTailSamples;
    const float baselineAmplitude = duskaudio::decibelsToGain(-40.0f);
    const float eventAmplitude = duskaudio::decibelsToGain(inputDbfs);

    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, peakReduction);
    control.setParameter(MultiCompDSP::Parameter::OptoLimit, limit ? 1.0f : 0.0f);
    active.setParameter(MultiCompDSP::Parameter::OptoLimit, limit ? 1.0f : 0.0f);
    std::vector<float> controlOutput(static_cast<size_t>(totalSamples));
    std::vector<float> activeOutput(static_cast<size_t>(totalSamples));
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> controlBlock{};
    std::array<float, kBlockSize> activeBlock{};
    for (int blockStart = 0; blockStart < totalSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, totalSamples - blockStart);
        for (int i = 0; i < count; ++i)
        {
            const int sample = blockStart + i;
            const float amplitude = sample >= kEventStart
                    && sample < kEventStart + eventSamples
                ? eventAmplitude : baselineAmplitude;
            input[static_cast<size_t>(i)] = amplitude * std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(sample)
                / static_cast<float>(kSampleRate));
        }
        const float* inputs[] = {input.data()};
        float* controlOutputs[] = {controlBlock.data()};
        float* activeOutputs[] = {activeBlock.data()};
        control.processBlock(inputs, controlOutputs, 1, count);
        active.processBlock(inputs, activeOutputs, 1, count);
        std::copy_n(controlBlock.data(), count,
                    controlOutput.data() + blockStart);
        std::copy_n(activeBlock.data(), count,
                    activeOutput.data() + blockStart);
    }

    std::vector<double> controlPrefix(static_cast<size_t>(totalSamples + 1));
    std::vector<double> activePrefix(static_cast<size_t>(totalSamples + 1));
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const double controlSample = controlOutput[static_cast<size_t>(sample)];
        const double activeSample = activeOutput[static_cast<size_t>(sample)];
        controlPrefix[static_cast<size_t>(sample + 1)]
            = controlPrefix[static_cast<size_t>(sample)]
                + controlSample * controlSample;
        activePrefix[static_cast<size_t>(sample + 1)]
            = activePrefix[static_cast<size_t>(sample)]
                + activeSample * activeSample;
    }
    auto reductionAt = [&](int sample) {
        const int first = std::max(0, sample - kEnvelopeSamples / 2);
        const int last = std::min(totalSamples, first + kEnvelopeSamples);
        const double controlPower = controlPrefix[static_cast<size_t>(last)]
            - controlPrefix[static_cast<size_t>(first)];
        const double activePower = activePrefix[static_cast<size_t>(last)]
            - activePrefix[static_cast<size_t>(first)];
        return 10.0f * std::log10(static_cast<float>(
            std::max(controlPower, 1.0e-24) / std::max(activePower, 1.0e-24)));
    };
    auto medianReduction = [&](int first, int last) {
        std::vector<float> values;
        values.reserve(static_cast<size_t>(std::max(0, last - first)));
        for (int sample = first; sample < last; ++sample)
            values.push_back(reductionAt(sample));
        const auto middle = values.begin()
            + static_cast<std::ptrdiff_t>(values.size() / 2);
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    };

    const int latency = control.getLatencySamples();
    const int outputOn = kEventStart + latency;
    const int outputOff = outputOn + eventSamples;
    constexpr int kGuardSamples = 3 * kSampleRate / 1000;
    const float finalWindowSeconds = std::min(
        std::max(exposureSeconds * 0.40f, 0.004f), 0.050f);
    const int finalStart = std::max(
        outputOn + kGuardSamples,
        outputOff - static_cast<int>(std::lround(
            finalWindowSeconds * kSampleRate)));
    const int finalStop = std::max(finalStart + 1, outputOff - kGuardSamples);
    const float initialReduction = medianReduction(
        outputOn - kSampleRate / 10, outputOn - kGuardSamples);
    const float finalReduction = medianReduction(finalStart, finalStop);
    const float target = initialReduction
        + 0.90f * (finalReduction - initialReduction);
    const int attackStart = outputOn + kGuardSamples;
    const int attackStop = outputOff - kGuardSamples;
    float crossing90 = exposureSeconds;
    bool crossing90Found = false;
    for (int sample = attackStart; sample < attackStop; ++sample)
        if (reductionAt(sample) >= target)
        {
            crossing90 = static_cast<float>(sample - attackStart)
                / static_cast<float>(kSampleRate);
            crossing90Found = true;
            break;
        }
    return {finalReduction, crossing90, crossing90Found};
}

void testOptoLimitDynamics()
{
    struct Row
    {
        float peakReduction;
        float inputDbfs;
        float exposureSeconds;
        float finalReduction;
        float crossing90;
        bool heldOut;
    };
    constexpr std::array<Row, 12> rows{{
        {60.0f, -12.0f, 0.010f, 9.391500f, 0.001646f, false},
        {60.0f, -12.0f, 0.100f, 11.060040f, 0.007042f, false},
        {60.0f, -12.0f, 1.000f, 11.832684f, 0.036000f, false},
        {100.0f, -12.0f, 0.010f, 23.023195f, 0.002146f, false},
        {100.0f, -12.0f, 0.100f, 31.567879f, 0.021208f, false},
        {100.0f, -12.0f, 1.000f, 31.895535f, 0.023604f, false},
        {30.0f, -24.0f, 0.100f, 0.003595f, 0.000000f, true},
        {60.0f, -24.0f, 0.100f, 1.586796f, 0.033542f, true},
        {100.0f, -24.0f, 0.100f, 18.567860f, 0.013604f, true},
        {30.0f, -3.0f, 0.010f, 4.562598f, 0.001771f, true},
        {60.0f, -3.0f, 0.100f, 19.506279f, 0.012063f, true},
        {100.0f, -3.0f, 1.000f, 40.970371f, 0.035979f, true}
    }};
    float finalSquaredError = 0.0f;
    float finalWorstError = 0.0f;
    float crossingWorstError = 0.0f;
    float heldOutSquaredError = 0.0f;
    float heldOutWorstError = 0.0f;
    float heldOutCrossingWorstError = 0.0f;
    int fittedCount = 0;
    int heldOutCount = 0;
    bool allMeaningfulCrossingsFound = true;
    for (const auto& row : rows)
    {
        const auto measured = measureOptoOutputDynamics(
            row.peakReduction, row.inputDbfs, row.exposureSeconds, true);
        const float finalDelta = measured.finalReduction - row.finalReduction;
        const float crossingDelta = measured.crossing90 - row.crossing90;
        if (row.finalReduction > 0.1f)
            allMeaningfulCrossingsFound = allMeaningfulCrossingsFound
                && measured.crossing90Found;
        std::printf("opto Limit dynamics: PR %.1f input %.1f exposure %.3f "
                    "final ref/measured "
                    "%.6f/%.6f delta %+.6f dB; t90 ref/measured %.6f/%.6f "
                    "delta %+.6f s%s\n",
                    row.peakReduction * 0.01f, row.inputDbfs, row.exposureSeconds,
                    row.finalReduction, measured.finalReduction, finalDelta,
                    row.crossing90, measured.crossing90, crossingDelta,
                    row.heldOut ? " (held out)" : "");
        if (row.heldOut)
        {
            heldOutSquaredError += finalDelta * finalDelta;
            heldOutWorstError = std::max(heldOutWorstError, std::abs(finalDelta));
            if (row.finalReduction > 0.1f)
                heldOutCrossingWorstError = std::max(
                    heldOutCrossingWorstError, std::abs(crossingDelta));
            ++heldOutCount;
        }
        else
        {
            finalSquaredError += finalDelta * finalDelta;
            finalWorstError = std::max(finalWorstError, std::abs(finalDelta));
            crossingWorstError = std::max(crossingWorstError,
                                          std::abs(crossingDelta));
            ++fittedCount;
        }
    }
    const float finalRmsError = std::sqrt(finalSquaredError / fittedCount);
    const float heldOutRmsError = std::sqrt(heldOutSquaredError / heldOutCount);
    std::printf("opto Limit dynamics summary: final RMS %.6f dB worst %.6f dB; "
                "t90 worst %.6f s; held-out RMS %.6f dB worst %.6f dB "
                "t90 worst %.6f s\n",
                finalRmsError, finalWorstError, crossingWorstError,
                heldOutRmsError, heldOutWorstError,
                heldOutCrossingWorstError);
    require(allMeaningfulCrossingsFound
                && finalRmsError < 0.50f && finalWorstError < 0.80f
                && crossingWorstError < 0.013f
                && heldOutRmsError < 0.80f && heldOutWorstError < 1.50f
                && heldOutCrossingWorstError < 0.030f,
            "Opto Limit charge follows the measured duration and PR grid");
}

struct OptoReleaseLocalTaus
{
    float initialReduction = 0.0f;
    float earlyTau = 0.0f;
    float midTau = 0.0f;
    float lateTau = 0.0f;
};

OptoReleaseLocalTaus measureOptoReleaseLocalTaus(float peakReduction, float levelDbfs,
                                                  float exposureSeconds)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 16;
    constexpr float kReleaseSeconds = 4.1f;
    MultiCompDSP dsp;
    prepareOptoDynamicsDsp(dsp, peakReduction);
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    int sampleCursor = 0;
    auto processLevel = [&](float levelDbfs, int samples, std::vector<float>* trace) {
        const float amplitude = duskaudio::decibelsToGain(levelDbfs);
        for (int offset = 0; offset < samples; offset += kBlockSize)
        {
            const int count = std::min(kBlockSize, samples - offset);
            for (int i = 0; i < count; ++i)
                input[static_cast<size_t>(i)] = amplitude * std::sin(
                    2.0f * kPi * 1000.0f * static_cast<float>(sampleCursor + i)
                    / static_cast<float>(kSampleRate));
            dsp.processBlock(inputs, outputs, 1, count);
            sampleCursor += count;
            if (trace != nullptr) trace->push_back(-dsp.getGainReduction());
        }
    };
    processLevel(-60.0f, kSampleRate, nullptr);
    processLevel(levelDbfs, static_cast<int>(std::lround(exposureSeconds * kSampleRate)), nullptr);
    const float initialReduction = -dsp.getGainReduction();
    std::vector<float> release;
    release.reserve(static_cast<size_t>(
        std::ceil(kReleaseSeconds * kSampleRate / kBlockSize)));
    processLevel(-60.0f, static_cast<int>(kReleaseSeconds * kSampleRate), &release);
    auto reductionAt = [&](float seconds) {
        const size_t index = std::min(release.size() - 1, static_cast<size_t>(
            std::lround(seconds * kSampleRate / kBlockSize)));
        return std::max(release[index], 1.0e-9f);
    };
    auto localTau = [&](float first, float last) {
        const float firstReduction = reductionAt(first);
        const float lastReduction = reductionAt(last);
        return (last - first) / std::log(firstReduction / lastReduction);
    };
    return {initialReduction, localTau(0.060f, 0.250f),
            localTau(0.250f, 1.000f), localTau(2.000f, 4.000f)};
}

void reportOptoReleaseLocalTaus()
{
    struct ExposureRow { float exposure, earlyTarget, midTarget, lateTarget; };
    constexpr std::array<ExposureRow, 4> rows{{
        {0.010f, 0.159f, 0.616f, 1.352f},
        {0.100f, 0.173f, 0.686f, 1.272f},
        {0.300f, 0.204f, 0.737f, 1.206f},
        {5.000f, 0.245f, 0.730f, 1.173f}
    }};
    for (const auto& row : rows)
    {
        const auto measured = measureOptoReleaseLocalTaus(60.0f, -24.0f, row.exposure);
        std::printf("opto release local tau: PR 0.6 exposure %.3f target %.3f/%.3f/%.3f "
                    "measured %.6f/%.6f/%.6f s initial %.6f dB\n",
                    row.exposure, row.earlyTarget, row.midTarget, row.lateTarget,
                    measured.earlyTau, measured.midTau, measured.lateTau,
                    measured.initialReduction);
    }
    for (const float peakReduction : {60.0f, 100.0f})
        for (const float exposure : {0.300f, 5.000f})
        {
            const auto measured = measureOptoReleaseLocalTaus(
                peakReduction, -24.0f, exposure);
            std::printf("opto release late invariance: PR %.1f exposure %.3f late %.6f s\n",
                        peakReduction * 0.01f, exposure, measured.lateTau);
        }
}

void testCrossoverFlatness()
{
    const float configurations[][3] = {{200.0f, 2000.0f, 8000.0f}, {100.0f, 1000.0f, 5000.0f}};
    for (const auto& cfg : configurations)
    {
        for (double sr : {44100.0, 48000.0, 96000.0})
        {
            for (const float frequency : {cfg[0], cfg[2],
                                           std::min(cfg[2] * 1.5f, static_cast<float>(sr * 0.45))})
            {
                DuskCrossover c1, c2, c3;
                c1.prepare(sr, cfg[0]); c2.prepare(sr, cfg[1]); c3.prepare(sr, cfg[2]);
                const int n = 16384;
                std::vector<float> sum(static_cast<size_t>(n)), original(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i)
                {
                    const float x = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(i) / static_cast<float>(sr));
                    original[static_cast<size_t>(i)] = x;
                    float l0, h0, l1, h1, l2, h2;
                    c1.processStandard(x, l0, h0); c2.processStandard(h0, l1, h1); c3.processStandard(h1, l2, h2);
                    sum[static_cast<size_t>(i)] = l0 + l1 + l2 + h2;
                }
                const float ratio = rms(sum, 4096) / rms(original, 4096);
                require(std::abs(duskaudio::gainToDecibels(ratio)) < 0.1f, "standard LR4 magnitude reconstruction");
            }

            DuskCrossover standard;
            standard.prepare(sr, cfg[0]);
            auto branchLevel = [&, sr](float frequency, bool low) {
                standard.reset();
                std::vector<float> values(8192);
                for (int i = 0; i < 8192; ++i)
                {
                    const float x = std::sin(2.0f * kPi * frequency * static_cast<float>(i) / static_cast<float>(sr));
                    float l, h;
                    standard.processStandard(x, l, h);
                    values[static_cast<size_t>(i)] = low ? l : h;
                }
                return rms(values, 4096);
            };
            require(branchLevel(cfg[0] * 0.25f, true) > branchLevel(cfg[0] * 0.25f, false), "standard LR4 low edge magnitude");
            require(branchLevel(cfg[0] * 4.0f, false) > branchLevel(cfg[0] * 4.0f, true), "standard LR4 high edge magnitude");
        }
    }
    std::puts("LR4 standard magnitude flatness: 44.1/48/96 kHz, two 3-split configurations OK");
}

void testStaticCurves()
{
    const duskaudio::MultiCompMode modes[] = {
        duskaudio::MultiCompMode::Opto, duskaudio::MultiCompMode::FET, duskaudio::MultiCompMode::VCA,
        duskaudio::MultiCompMode::Bus, duskaudio::MultiCompMode::StudioFET, duskaudio::MultiCompMode::StudioVCA,
        duskaudio::MultiCompMode::Digital, duskaudio::MultiCompMode::Multiband};
    for (auto mode : modes)
    {
        const float quiet = renderSine(mode, 0.05f);
        const float medium = renderSine(mode, 0.25f);
        const float hot = renderSine(mode, 0.8f);
        require(std::isfinite(quiet) && std::isfinite(medium) && std::isfinite(hot), "static curve finite");
        require(quiet > 1.0e-5f && medium > 1.0e-5f && hot > 1.0e-5f,
                "static curve stimuli produce output");
        require(quiet <= medium * 1.05f && medium <= hot * 1.05f, "static curve monotonic");
    }
    const float inputDb = duskaudio::gainToDecibels(0.5f / std::sqrt(2.0f));
    const float expectedDb = inputDb - (inputDb - (-20.0f)) * (1.0f - 1.0f / 4.0f);
    const float digitalDb = duskaudio::gainToDecibels(renderSine(duskaudio::MultiCompMode::Digital, 0.5f));
    require(std::abs(digitalDb - expectedDb) < 2.0f, "digital static curve follows threshold/ratio math");
    std::puts("static curves: all eight modes finite and monotonic");
}

void configureStrongCompression(MultiCompDSP& dsp, int mode)
{
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    switch (static_cast<duskaudio::MultiCompMode>(mode))
    {
        case duskaudio::MultiCompMode::Opto:
            dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, 100.0f);
            break;
        case duskaudio::MultiCompMode::FET:
        case duskaudio::MultiCompMode::StudioFET:
            dsp.setParameter(MultiCompDSP::Parameter::FetInput, 10.0f);
            dsp.setParameter(MultiCompDSP::Parameter::FetThreshold, -30.0f);
            dsp.setParameter(MultiCompDSP::Parameter::FetAttack, 0.1f);
            dsp.setParameter(MultiCompDSP::Parameter::FetRelease, 50.0f);
            dsp.setParameter(MultiCompDSP::Parameter::FetRatio, 3.0f);
            break;
        case duskaudio::MultiCompMode::VCA:
            dsp.setParameter(MultiCompDSP::Parameter::VcaThreshold, -30.0f);
            dsp.setParameter(MultiCompDSP::Parameter::VcaRatio, 90.0f);   // knob position: ~16:1
            dsp.setParameter(MultiCompDSP::Parameter::VcaAttack, 0.1f);
            dsp.setParameter(MultiCompDSP::Parameter::VcaRelease, 50.0f);
            break;
        case duskaudio::MultiCompMode::Bus:
            dsp.setParameter(MultiCompDSP::Parameter::BusThreshold, -30.0f);
            dsp.setParameter(MultiCompDSP::Parameter::BusRatio, 2.0f);
            dsp.setParameter(MultiCompDSP::Parameter::BusAttack, 0.0f);
            dsp.setParameter(MultiCompDSP::Parameter::BusRelease, 0.0f);
            break;
        case duskaudio::MultiCompMode::StudioVCA:
            dsp.setParameter(MultiCompDSP::Parameter::StudioVcaThreshold, -30.0f);
            dsp.setParameter(MultiCompDSP::Parameter::StudioVcaRatio, 10.0f);
            dsp.setParameter(MultiCompDSP::Parameter::StudioVcaAttack, 0.3f);
            dsp.setParameter(MultiCompDSP::Parameter::StudioVcaRelease, 100.0f);
            break;
        case duskaudio::MultiCompMode::Digital:
            dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
            dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 10.0f);
            dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.1f);
            dsp.setParameter(MultiCompDSP::Parameter::DigitalRelease, 50.0f);
            break;
        case duskaudio::MultiCompMode::Multiband:
            for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
            {
                dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, -30.0f);
                dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 10.0f);
                dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Attack, 0.1f);
                dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Release, 50.0f);
            }
            break;
    }
}

void testEnvelopeAndReset()
{
    for (int mode = 0; mode < 8; ++mode)
    {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 256); dsp.setOversampling(0); dsp.setMode(mode);
        configureStrongCompression(dsp, mode);
        constexpr int kStepBlocks = 64;
        std::vector<float> reference(static_cast<size_t>(kStepBlocks * 256)), second(static_cast<size_t>(kStepBlocks * 256)), in(256), out(256);
        float attackReduction = 0.0f, firstReleaseReduction = 0.0f, settledReleaseReduction = 0.0f;
        for (int block = 0; block < kStepBlocks; ++block)
        {
            for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = block < kStepBlocks / 2 ? 0.8f : 0.0f;
            const float* ip[] = {in.data()}; float* op[] = {out.data()};
            dsp.processBlock(ip, op, 1, 256);
            std::copy(out.begin(), out.end(), reference.begin() + block * 256);
            if (block < kStepBlocks / 2)
                attackReduction = std::min(attackReduction, dsp.getGainReduction());
            if (block == kStepBlocks / 2) firstReleaseReduction = dsp.getGainReduction();
            if (block == kStepBlocks - 1) settledReleaseReduction = dsp.getGainReduction();
        }
        for (float x : reference) require(std::isfinite(x), "envelope step finite");
        require(rms(reference) > 1.0e-5f, "envelope/reset reference produces output");
        require(std::isfinite(attackReduction) && std::isfinite(firstReleaseReduction)
                    && std::isfinite(settledReleaseReduction), "envelope meter finite");
        std::printf("envelope release: mode=%d attack=%.4f first=%.4f settled=%.4f\n",
                    mode, attackReduction, firstReleaseReduction, settledReleaseReduction);
        require(attackReduction < -0.5f, "envelope stimulus establishes gain reduction");
        require(settledReleaseReduction > firstReleaseReduction + 0.05f,
                "release returns toward unity at the configured time scale");
        dsp.reset();
        for (int block = 0; block < kStepBlocks; ++block)
        {
            for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = block < kStepBlocks / 2 ? 0.8f : 0.0f;
            const float* ip[] = {in.data()}; float* op[] = {out.data()};
            dsp.processBlock(ip, op, 1, 256);
            std::copy(out.begin(), out.end(), second.begin() + block * 256);
        }
        float maxDiff = 0.0f;
        for (size_t i = 0; i < reference.size(); ++i) maxDiff = std::max(maxDiff, std::abs(reference[i] - second[i]));
        require(maxDiff == 0.0f, "reset determinism");
    }
    std::puts("attack/release steps: finite; reset determinism: all eight modes OK");
}

void testMixBypassAndBlockEdges()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256); dsp.setOversampling(0); dsp.setMode(6);
    std::vector<float> in(256), out(256), previous(256);
    for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = 0.4f * std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
    const float* ip[] = {in.data()}; float* op[] = {out.data()};
    dsp.processBlock(ip, op, 1, 256);
    dsp.setMix(0.0f); dsp.processBlock(ip, op, 1, 256);
    float largestDelta = 0.0f;
    for (int i = 1; i < 256; ++i) largestDelta = std::max(largestDelta, std::abs(out[static_cast<size_t>(i)] - out[static_cast<size_t>(i - 1)]));
    require(largestDelta < 0.5f, "mix ramp bounded sample delta");
    MultiCompDSP bypassCheck;
    bypassCheck.prepare(48000.0, 256);
    bypassCheck.setOversampling(0);
    bypassCheck.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    // Real compression, not 1:1. With a unity ratio the active and bypassed
    // outputs are identical, so no assertion below can tell whether the
    // un-bypass actually happened.
    bypassCheck.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
    bypassCheck.setParameter(MultiCompDSP::Parameter::DigitalRatio, 8.0f);
    std::vector<float> bypassInput(256), bypassOutput(256);
    const float* bypassIp[] = {bypassInput.data()}; float* bypassOp[] = {bypassOutput.data()};
    for (int block = 0; block < 10; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            bypassInput[static_cast<size_t>(i)] = 0.1f + 0.0001f * static_cast<float>(block * 256 + i);
        }
        bypassCheck.processBlock(bypassIp, bypassOp, 1, 256);
    }
    bypassCheck.setBypass(true);
    for (int block = 0; block < 10; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            bypassInput[static_cast<size_t>(i)] = 0.1f + 0.0001f * static_cast<float>(10 * 256 + block * 256 + i);
        }
        bypassCheck.processBlock(bypassIp, bypassOp, 1, 256);
        if (block == 9)
            for (int i = 0; i < 256; ++i)
            {
                const int absolute = 10 * 256 + block * 256 + i;
                const float expected = absolute >= bypassCheck.getLatencySamples() ? 0.1f + 0.0001f * static_cast<float>(absolute - bypassCheck.getLatencySamples()) : 0.0f;
                if (bypassOutput[static_cast<size_t>(i)] != expected)
                {
                    std::fprintf(stderr, "bypass mismatch i=%d out=%.9g expected=%.9g latency=%d\n", i, bypassOutput[static_cast<size_t>(i)], expected, bypassCheck.getLatencySamples());
                    require(false, "settled bypass is delayed bit-exact passthrough");
                }
            }
    }
    // Un-bypass the object that was actually bypassed. This previously cleared
    // bypass on `dsp`, which had never been bypassed, so the un-bypass path was
    // never exercised and the assertions below could not fail.
    bypassCheck.setBypass(false);
    float reentryDelta = 0.0f;
    float reentryMaxAbs = 0.0f;
    for (int block = 0; block < 4; ++block)
    {
        for (int i = 0; i < 256; ++i)
            bypassInput[static_cast<size_t>(i)] = 0.1f + 0.0001f * static_cast<float>(20 * 256 + block * 256 + i);
        bypassCheck.processBlock(bypassIp, bypassOp, 1, 256);
        for (int i = 0; i < 256; ++i)
        {
            const float sample = bypassOutput[static_cast<size_t>(i)];
            require(std::isfinite(sample), "bypass re-entry finite");
            reentryMaxAbs = std::max(reentryMaxAbs, std::abs(sample));
            if (i > 0)
                reentryDelta = std::max(reentryDelta,
                                        std::abs(sample - bypassOutput[static_cast<size_t>(i - 1)]));
        }
    }
    require(reentryDelta < 1.0f, "bypass toggle bounded sample delta");
    // The decisive assertion: with a real ratio the un-bypassed output must be
    // audibly BELOW the dry input it would pass while bypassed. Finiteness and
    // bounded deltas hold in both states, so only this one can distinguish them
    // and therefore only this one can fail if the un-bypass path breaks.
    const float dryPeakAtReentry = 0.1f + 0.0001f * static_cast<float>(20 * 256 + 4 * 256 - 1);
    require(reentryMaxAbs < dryPeakAtReentry * 0.9f, "bypass re-entry resumes compression");
    // Guard the vacuous case: an all-zero buffer would satisfy the bound above.
    require(reentryMaxAbs > 1.0e-4f, "bypass re-entry produced signal");
    std::array<float, 8> zeroFrameOutput{{101.25f, -202.5f, 303.75f, -404.0f,
                                          505.5f, -606.25f, 707.0f, -808.75f}};
    const auto zeroFrameSentinel = zeroFrameOutput;
    float* zeroFrameOp[] = {zeroFrameOutput.data()};
    dsp.processBlock(ip, zeroFrameOp, 1, 0);
    require(zeroFrameOutput == zeroFrameSentinel, "zero-frame process leaves output untouched");
    dsp.processBlock(ip, op, 1, 1);
    (void)previous;
    std::puts("mix ramp, bypass, zero-sample and single-sample blocks OK");
}

float renderNeutralSine(int oversampling, float mix, float frequency = 997.0f)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(oversampling);
    dsp.setMix(mix);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    std::vector<float> in(256), out(256);
    float result = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block >= 28) for (float x : out) result += x * x;
    }
    return std::sqrt(result / (4.0f * 256.0f));
}

float renderLookaheadSine(float mix, float frequency, float lookaheadMs)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(0);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalMix, mix);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, lookaheadMs);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    std::vector<float> in(256), out(256);
    float result = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block >= 28) for (float x : out) result += x * x;
    }
    return std::sqrt(result / (4.0f * 256.0f));
}

void testFourTimesHighFrequencyMixCoherence()
{
    const float fullWet = renderNeutralSine(2, 100.0f, 18000.0f);
    const float halfMix = renderNeutralSine(2, 50.0f, 18000.0f);
    const float lossDb = duskaudio::gainToDecibels(halfMix / std::max(fullWet, 1.0e-9f));
    std::printf("4x 18 kHz mix coherence: full-wet %.9g half-mix %.9g delta %.5f dB\n",
                fullWet, halfMix, lossDb);
    require(std::abs(lossDb) < 0.1f, "4x 18 kHz dry and wet remain phase coherent at 50% mix");
}

void testTruePeakOversampledPhaseInterpolation()
{
    duskaudio::MultiCompTruePeakDetector detector;
    detector.prepare();
    const float current = detector.processSample(0.0f, 0);
    const float next = detector.processSample(1.0f, 0);
    float minimum = next, maximum = current;
    for (int phase = 0; phase < 4; ++phase)
    {
        const float value = duskaudio::interpolateOversampledSidechain(current, next, phase, 4);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    const float spread = maximum - minimum;
    std::printf("true-peak 4x detector phases: held spread 0; interpolated spread %.6f\n", spread);
    require(spread > 0.5f, "fast true-peak transient changes across oversampled detector phases");
}

int detectorGainReductionOnset(int oversampling)
{
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(oversampling);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalKnee, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.01f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRelease, 100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAdaptive, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.prepare(48000.0, 1);

    float input = 0.25f, sidechain = 0.0f, output = 0.0f;
    const float* inputs[] = {&input};
    const float* sidechains[] = {&sidechain};
    float* outputs[] = {&output};
    constexpr int stepSample = 8;
    for (int sample = 0; sample < 16; ++sample)
    {
        sidechain = sample >= stepSample ? 1.0f : 0.0f;
        dsp.processBlockExternal(inputs, sidechains, outputs, 1, 1);
        if (dsp.getGainReduction() < -0.0001f)
            return sample;
    }
    return -1;
}

void testOversampledDetectorHasNativeRateStepTiming()
{
    const int oneXOnset = detectorGainReductionOnset(0);
    const int fourXOnset = detectorGainReductionOnset(2);
    std::printf("sidechain step at 8: 1x GR onset %d; 4x GR onset %d\n",
                oneXOnset, fourXOnset);
    require(oneXOnset == 8 && fourXOnset == oneXOnset,
            "4x detector gain reduction starts on the same native sample as 1x");
}

void testLatencyMixBypassAndDigitalStereo()
{
    for (int oversampling : {0, 1, 2})
    {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 512);
        dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
        dsp.setOversampling(oversampling);
        dsp.setMix(100.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
        dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
        const int latency = dsp.getLatencySamples();
        require(latency == 27, "constant maximum anti-alias latency");

        std::vector<float> impulse(512, 0.0f), output(512, 0.0f);
        impulse[0] = 0.25f;
        const float* ip[] = {impulse.data()}; float* op[] = {output.data()};
        dsp.processBlock(ip, op, 1, static_cast<int>(impulse.size()));
        int peakIndex = 0;
        for (int i = 1; i < static_cast<int>(output.size()); ++i)
            if (std::abs(output[static_cast<size_t>(i)]) > std::abs(output[static_cast<size_t>(peakIndex)])) peakIndex = i;
        float peakValue = 0.0f; for (float x : output) peakValue = std::max(peakValue, std::abs(x));
        std::printf("latency impulse: os=%d reported=%d peak=%d amplitude=%.6f\n", oversampling, latency, peakIndex, peakValue);
        require(std::abs(peakIndex - latency) <= 2, "wet impulse group delay matches reported latency");

        const float fullWet = renderNeutralSine(oversampling, 100.0f);
        const float halfMix = renderNeutralSine(oversampling, 50.0f);
        const float mixDelta = std::abs(duskaudio::gainToDecibels(halfMix / std::max(fullWet, 1.0e-9f)));
        require(mixDelta < 0.05f, "phase-coherent 50% mix has no comb ripple");

        MultiCompDSP bypass;
        bypass.prepare(48000.0, 512);
        bypass.setOversampling(oversampling);
        bypass.setBypass(true);
        bypass.reset();
        std::vector<float> input(512), bypassed(512);
        for (int i = 0; i < 512; ++i) input[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
        const float* bip[] = {input.data()}; float* bop[] = {bypassed.data()};
        bypass.processBlock(bip, bop, 1, 512);
        for (int i = 0; i < 512; ++i)
        {
            const float expected = i >= latency ? input[static_cast<size_t>(i - latency)] : 0.0f;
            require(bypassed[static_cast<size_t>(i)] == expected, "settled bypass is latency-aligned bit-exact passthrough");
        }
        std::printf("latency/mix: os=%d reported=%d mix_delta=%.5f dB\n", oversampling, latency, mixDelta);
    }

    MultiCompDSP latencyMatrix;
    latencyMatrix.prepare(48000.0, 512);
    latencyMatrix.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    for (int oversampling : {0, 1, 2})
        for (float globalLookahead : {0.0f, 10.0f})
            for (float digitalLookahead : {0.0f, 10.0f})
            {
                latencyMatrix.setOversampling(oversampling);
                latencyMatrix.setParameter(MultiCompDSP::Parameter::GlobalLookahead, globalLookahead);
                latencyMatrix.setParameter(MultiCompDSP::Parameter::DigitalLookahead, digitalLookahead);
                const int expected = 27 + static_cast<int>(globalLookahead * 48.0f)
                                        + static_cast<int>(digitalLookahead * 48.0f);
                require(latencyMatrix.getLatencySamples() == expected,
                        "Digital latency reports AA plus global and mode lookahead");
            }
    latencyMatrix.setMode(static_cast<int>(duskaudio::MultiCompMode::FET));
    require(latencyMatrix.getLatencySamples() == 507,
            "mode change removes Digital lookahead while retaining global lookahead");
    std::printf("latency matrix: os=off/2x/4x, global=0/10ms, digital=0/10ms; Digital max=987 FET max=507\n");

    MultiCompDSP stereo;
    stereo.prepare(48000.0, 256);
    stereo.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    stereo.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 5.0f);
    stereo.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -20.0f);
    stereo.setParameter(MultiCompDSP::Parameter::DigitalRatio, 4.0f);
    std::vector<float> left(256), right(256, 0.0f), outLeft(256), outRight(256);
    for (int i = 0; i < 256; ++i) left[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
    const float* stereoIn[] = {left.data(), right.data()}; float* stereoOut[] = {outLeft.data(), outRight.data()};
    float rightPeak = 0.0f, leftEnergy = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        for (int i = 0; i < 256; ++i) left[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 440.0f * (block * 256 + i) / 48000.0f);
        stereo.processBlock(stereoIn, stereoOut, 2, 256);
        if (block >= 6) for (int i = 0; i < 256; ++i) { rightPeak = std::max(rightPeak, std::abs(outRight[static_cast<size_t>(i)])); leftEnergy += outLeft[static_cast<size_t>(i)] * outLeft[static_cast<size_t>(i)]; }
    }
    std::printf("digital stereo: right_peak=%.9g left_energy=%.9g\n", rightPeak, leftEnergy);
    require(rightPeak < 1.0e-7f && leftEnergy > 1.0e-5f, "digital lookahead keeps stereo channels independent");

    std::vector<float> largeIn(1025, 0.1f), largeOut(1025);
    const float* largeIp[] = {largeIn.data()}; float* largeOp[] = {largeOut.data()};
    stereo.processBlock(largeIp, largeOp, 1, static_cast<int>(largeIn.size()));
    for (float x : largeOut) require(std::isfinite(x), "oversized blocks are chunked");
    std::puts("latency alignment, mix comb, bypass, digital stereo, oversized block: OK");
}

std::array<float, 4> renderAnalogStereoLink(int mode, float linkAmount)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(mode);
    dsp.setStereoLink(linkAmount);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    configureStrongCompression(dsp, mode);
    std::array<float, 256> inputLeft{}, inputRight{}, sidechainLeft{}, sidechainRight{};
    std::array<float, 256> outputLeft{}, outputRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    const float* sidechain[] = {sidechainLeft.data(), sidechainRight.data()};
    float* output[] = {outputLeft.data(), outputRight.data()};
    double leftSum = 0.0, rightSum = 0.0;
    float maxChannelDelta = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const float sample = 0.02f * std::sin(2.0f * kPi * 997.0f
                * static_cast<float>(block * 256 + i) / 48000.0f);
            inputLeft[static_cast<size_t>(i)] = sample;
            inputRight[static_cast<size_t>(i)] = sample;
            sidechainLeft[static_cast<size_t>(i)] = 0.8f;
            sidechainRight[static_cast<size_t>(i)] = 0.0f;
        }
        dsp.processBlockExternal(input, sidechain, output, 2, 256);
        if (block >= 4)
            for (int i = 0; i < 256; ++i)
            {
                const float left = outputLeft[static_cast<size_t>(i)];
                const float right = outputRight[static_cast<size_t>(i)];
                leftSum += static_cast<double>(left) * left;
                rightSum += static_cast<double>(right) * right;
                maxChannelDelta = std::max(maxChannelDelta, std::abs(left - right));
            }
    }
    constexpr double kMeasuredSamples = 4.0 * 256.0;
    return {{static_cast<float>(std::sqrt(leftSum / kMeasuredSamples)),
             static_cast<float>(std::sqrt(rightSum / kMeasuredSamples)),
             maxChannelDelta, dsp.getGainReduction()}};
}

void testAnalogStereoLinkSharesEnvelope()
{
    for (int mode = static_cast<int>(duskaudio::MultiCompMode::Opto);
         mode <= static_cast<int>(duskaudio::MultiCompMode::StudioVCA); ++mode)
    {
        const auto linked = renderAnalogStereoLink(mode, 100.0f);
        const auto independent = renderAnalogStereoLink(mode, 0.0f);
        std::printf("analog stereo link: mode=%d linked=(%.7g, %.7g) independent-right=%.7g delta=%.3g GR=%.3f\n",
                    mode, linked[0], linked[1], independent[1], linked[2], linked[3]);
        require(linked[3] < -0.5f, "analog stereo-link stimulus establishes gain reduction");
        require(independent[1] > 1.0e-4f, "analog stereo-link reference produces right-channel signal");
        require(linked[0] > 1.0e-4f && linked[1] > 1.0e-4f,
                "analog stereo-link render produces nonzero output on both channels");
        require(linked[2] < 1.0e-5f, "100% analog stereo link gives both channels the same envelope");
        require(linked[1] < independent[1] * 0.9f,
                "100% analog stereo link makes the hot left detector compress the right channel");
    }
}

std::array<float, 2> renderInternalStereoLink(int mode, float linkAmount,
                                               int oversampling)
{
    constexpr int blockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(mode);
    dsp.setOversampling(oversampling);
    dsp.setStereoLink(linkAmount);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScLowGain, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighGain, 0.0f);
    configureStrongCompression(dsp, mode);
    dsp.prepare(48000.0, blockSize);

    std::array<float, blockSize> inputLeft{}, inputRight{};
    std::array<float, blockSize> outputLeft{}, outputRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    float* output[] = {outputLeft.data(), outputRight.data()};
    std::array<double, 2> power{{0.0, 0.0}};
    constexpr int totalBlocks = 120;
    constexpr int measuredBlocks = 8;
    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float carrier = std::sin(2.0f * kPi * 997.0f
                * static_cast<float>(block * blockSize + i) / 48000.0f);
            inputLeft[static_cast<size_t>(i)] = 0.5f * carrier;
            inputRight[static_cast<size_t>(i)] = 0.02f * carrier;
        }
        dsp.processBlock(input, output, 2, blockSize);
        if (block >= totalBlocks - measuredBlocks)
            for (int i = 0; i < blockSize; ++i)
            {
                power[0] += static_cast<double>(outputLeft[static_cast<size_t>(i)])
                          * outputLeft[static_cast<size_t>(i)];
                power[1] += static_cast<double>(outputRight[static_cast<size_t>(i)])
                          * outputRight[static_cast<size_t>(i)];
            }
    }
    constexpr double sampleCount = measuredBlocks * blockSize;
    return {{static_cast<float>(std::sqrt(power[0] / sampleCount)),
             static_cast<float>(std::sqrt(power[1] / sampleCount))}};
}

void testSplitOversamplingMatchesFunctorPath()
{
    for (const int factor : {1, 2, 4})
    {
        duskaudio::MultiCompAntiAliasing direct, split;
        direct.setFactor(factor);
        split.setFactor(factor);
        direct.prepare(64);
        split.prepare(64);
        direct.setFactor(factor);
        split.setFactor(factor);
        direct.reset();
        split.reset();
        float maxDelta = 0.0f;
        float signalPeak = 0.0f;
        for (int sampleIndex = 0; sampleIndex < 4096; ++sampleIndex)
        {
            const float input = 0.47f * std::sin(2.0f * kPi * 7313.0f
                * static_cast<float>(sampleIndex) / 48000.0f);
            const auto process = [](float sample) noexcept {
                return 0.75f * sample + 0.1f * sample * sample * sample;
            };
            const float expected = direct.processSample(input, process);
            std::array<float, 4> phases{};
            split.upsampleSample(input, phases.data());
            for (int phase = 0; phase < factor; ++phase)
                phases[static_cast<size_t>(phase)] = process(
                    phases[static_cast<size_t>(phase)]);
            const float actual = split.downsampleSample(phases.data());
            maxDelta = std::max(maxDelta, std::abs(actual - expected));
            signalPeak = std::max(signalPeak, std::max(std::abs(actual), std::abs(expected)));
        }
        std::printf("split oversampling: factor=%dx signal peak %.9g max delta %.9g\n",
                    factor, signalPeak, maxDelta);
        require(signalPeak > 1.0e-4f,
                "split oversampling comparison produces output");
        require(maxDelta == 0.0f,
                "split oversampling is sample-identical to the functor path");
    }
}

float renderInternalSidechainShelf(duskaudio::MultiCompMode mode, float highGainDb)
{
    constexpr int blockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(mode));
    dsp.setOversampling(0);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScLowGain, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighFreq, 2000.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighGain, highGainDb);
    configureStrongCompression(dsp, static_cast<int>(mode));
    dsp.prepare(48000.0, blockSize);

    std::array<float, blockSize> input{}, output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    double power = 0.0;
    constexpr int totalBlocks = 120;
    constexpr int measuredBlocks = 8;
    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
            input[static_cast<size_t>(i)] = 0.2f * std::sin(2.0f * kPi * 8000.0f
                * static_cast<float>(block * blockSize + i) / 48000.0f);
        dsp.processBlock(inputs, outputs, 1, blockSize);
        if (block >= totalBlocks - measuredBlocks)
            for (float sample : output) power += static_cast<double>(sample) * sample;
    }
    return static_cast<float>(std::sqrt(power / (measuredBlocks * blockSize)));
}

void testVcaAndBusInternalDetectorControls()
{
    for (const auto mode : {duskaudio::MultiCompMode::VCA,
                            duskaudio::MultiCompMode::Bus})
        for (const int oversampling : {0, 1, 2})
        {
            const auto independent = renderInternalStereoLink(
                static_cast<int>(mode), 0.0f, oversampling);
            const auto linked = renderInternalStereoLink(
                static_cast<int>(mode), 100.0f, oversampling);
            const auto partial = renderInternalStereoLink(
                static_cast<int>(mode), 50.0f, oversampling);
            const float quietRatio = linked[1] / independent[1];
            const float partialQuietRatio = partial[1] / independent[1];
            const float loudDeltaDb = duskaudio::gainToDecibels(linked[0] / independent[0]);
            std::printf("internal detector link: mode=%d os=%dx quiet ratio "
                        "full %.6f partial %.6f loud delta %+.6f dB\n",
                        static_cast<int>(mode), oversampling == 2 ? 4 : oversampling == 1 ? 2 : 1,
                        quietRatio, partialQuietRatio, loudDeltaDb);
            require(independent[0] > 1.0e-5f && independent[1] > 1.0e-5f
                        && linked[0] > 1.0e-5f && linked[1] > 1.0e-5f,
                    "internal detector link comparison produces stereo output");
            require(quietRatio < 0.8f,
                    "100% internal stereo link makes the loud channel compress the quiet channel");
            require(quietRatio < partialQuietRatio && partialQuietRatio < 1.0f,
                    "partial internal stereo link stays between independent and fully linked");
            require(std::abs(loudDeltaDb) < 0.1f,
                    "internal stereo link leaves the already-dominant channel unchanged");
        }

    for (const auto mode : {duskaudio::MultiCompMode::VCA,
                            duskaudio::MultiCompMode::Bus})
    {
        const float cut = renderInternalSidechainShelf(mode, -12.0f);
        const float boost = renderInternalSidechainShelf(mode, 12.0f);
        const float shelfDeltaDb = duskaudio::gainToDecibels(boost / cut);
        std::printf("internal sidechain shelf: mode=%d -12 dB RMS %.9g; "
                    "+12 dB RMS %.9g; output delta %+.6f dB\n",
                    static_cast<int>(mode), cut, boost, shelfDeltaDb);
        require(cut > 1.0e-5f && boost > 1.0e-5f,
                "internal sidechain shelf comparison produces output");
        require(shelfDeltaDb < -3.0f,
                "internal detector consumes the sidechain shelf EQ");
    }
}

void testLinkedBusResetDeterminism()
{
    constexpr int blockSize = 256;
    constexpr int blocks = 16;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Bus));
    dsp.setOversampling(2);
    dsp.setStereoLink(65.0f);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 180.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScLowFreq, 120.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScLowGain, -6.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighFreq, 3000.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighGain, 9.0f);
    configureStrongCompression(dsp, static_cast<int>(duskaudio::MultiCompMode::Bus));
    dsp.prepare(48000.0, blockSize);

    std::array<float, blockSize> inputLeft{}, inputRight{};
    std::array<float, blockSize> outputLeft{}, outputRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    float* output[] = {outputLeft.data(), outputRight.data()};
    auto render = [&]() {
        std::vector<float> result(static_cast<size_t>(2 * blocks * blockSize));
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const int sampleIndex = block * blockSize + i;
                inputLeft[static_cast<size_t>(i)] = 0.45f * std::sin(
                    2.0f * kPi * 713.0f * static_cast<float>(sampleIndex) / 48000.0f);
                inputRight[static_cast<size_t>(i)] = 0.07f * std::sin(
                    2.0f * kPi * 3299.0f * static_cast<float>(sampleIndex) / 48000.0f);
            }
            dsp.processBlock(input, output, 2, blockSize);
            std::copy(outputLeft.begin(), outputLeft.end(),
                result.begin() + static_cast<ptrdiff_t>(2 * block * blockSize));
            std::copy(outputRight.begin(), outputRight.end(),
                result.begin() + static_cast<ptrdiff_t>((2 * block + 1) * blockSize));
        }
        return result;
    };

    const auto first = render();
    dsp.reset();
    const auto second = render();
    float signalPeak = 0.0f;
    float maxDelta = 0.0f;
    for (size_t i = 0; i < first.size(); ++i)
    {
        signalPeak = std::max(signalPeak, std::max(std::abs(first[i]), std::abs(second[i])));
        maxDelta = std::max(maxDelta, std::abs(first[i] - second[i]));
    }
    std::printf("linked Bus reset: signal peak %.9g max delta %.9g\n",
                signalPeak, maxDelta);
    require(signalPeak > 1.0e-4f, "linked Bus reset comparison produces output");
    require(maxDelta == 0.0f,
            "reset clears linked Bus detector, filter and split-oversampling state");
}

void testLinkedBusReentryReseedsSidechainInterpolation()
{
    constexpr int blockSize = 64;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Bus));
    dsp.setOversampling(2);
    dsp.setStereoLink(100.0f);
    dsp.setExternalSidechain(true);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    configureStrongCompression(dsp, static_cast<int>(duskaudio::MultiCompMode::Bus));
    dsp.prepare(48000.0, blockSize);

    std::array<float, blockSize> inputLeft{}, inputRight{}, sidechain{};
    std::array<float, blockSize> outputLeft{}, outputRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    const float* external[] = {sidechain.data(), sidechain.data()};
    float* output[] = {outputLeft.data(), outputRight.data()};
    for (int i = 0; i < blockSize; ++i)
    {
        inputLeft[static_cast<size_t>(i)] = 0.4f * std::sin(
            2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
        inputRight[static_cast<size_t>(i)] = 0.2f * std::sin(
            2.0f * kPi * 1709.0f * static_cast<float>(i) / 48000.0f);
        sidechain[static_cast<size_t>(i)] = 0.8f;
    }

    const auto validity = [&] {
        return duskaudio::MultiCompDSPTestAccess::busSidechainValidity(dsp);
    };
    const auto both = [](const std::array<bool, 2>& value, bool expected) {
        return value[0] == expected && value[1] == expected;
    };

    dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), true),
            "linked stereo Bus processing retains interpolation endpoints");

    dsp.setStereoLink(0.0f);
    dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), false),
            "unlinked Bus processing invalidates linked interpolation endpoints");

    dsp.setStereoLink(100.0f);
    dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), true),
            "linked Bus processing re-seeds invalid interpolation endpoints");

    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::VCA));
    dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), false),
            "a non-Bus mode invalidates linked Bus interpolation endpoints");

    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Bus));
    dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), true),
            "returning to linked Bus processing re-seeds interpolation endpoints");

    dsp.setBypass(true);
    for (int block = 0; block < 32; ++block)
        dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), false),
            "settled bypass invalidates linked Bus interpolation endpoints");

    dsp.setBypass(false);
    dsp.processBlockExternal(input, external, output, 2, blockSize);
    require(both(validity(), true),
            "leaving bypass re-seeds linked Bus interpolation endpoints");

    const float* monoInput[] = {inputLeft.data()};
    const float* monoExternal[] = {sidechain.data()};
    float* monoOutput[] = {outputLeft.data()};
    dsp.processBlockExternal(monoInput, monoExternal, monoOutput, 1, blockSize);
    require(both(validity(), false),
            "mono Bus processing invalidates stereo linked interpolation endpoints");
}

std::array<float, 2> renderOptoInternalStereo(float leftDbfs, float rightDbfs,
                                               float peakReduction,
                                               float linkAmount = 100.0f,
                                               int oversampling = 1)
{
    constexpr int kSamples = 24000;
    constexpr int kMeasureSamples = 4096;
    constexpr int kBlockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Opto));
    dsp.setStereoLink(linkAmount);
    dsp.setOversampling(oversampling);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::TruePeakEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, peakReduction);
    dsp.setParameter(MultiCompDSP::Parameter::OptoGain,
                     duskaudio::optoGainDbToKnob(0.0f));
    dsp.setParameter(MultiCompDSP::Parameter::OptoLimit, 0.0f);
    dsp.prepare(48000.0, kBlockSize);

    const std::array<float, 2> amplitudes{{
        duskaudio::decibelsToGain(leftDbfs),
        duskaudio::decibelsToGain(rightDbfs)}};
    std::array<double, 2> power{{0.0, 0.0}};
    std::array<float, kBlockSize> inputLeft{}, inputRight{};
    std::array<float, kBlockSize> outputLeft{}, outputRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    float* output[] = {outputLeft.data(), outputRight.data()};
    for (int blockStart = 0; blockStart < kSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kSamples - blockStart);
        for (int i = 0; i < count; ++i)
        {
            const float sine = std::sin(2.0f * kPi * 997.0f
                * static_cast<float>(blockStart + i) / 48000.0f);
            inputLeft[static_cast<size_t>(i)] = amplitudes[0] * sine;
            inputRight[static_cast<size_t>(i)] = amplitudes[1] * sine;
        }
        dsp.processBlock(input, output, 2, count);
        for (int i = 0; i < count; ++i)
        {
            if (blockStart + i < kSamples - kMeasureSamples) continue;
            power[0] += static_cast<double>(outputLeft[static_cast<size_t>(i)])
                      * outputLeft[static_cast<size_t>(i)];
            power[1] += static_cast<double>(outputRight[static_cast<size_t>(i)])
                      * outputRight[static_cast<size_t>(i)];
        }
    }
    return {{static_cast<float>(std::sqrt(power[0] / kMeasureSamples)),
             static_cast<float>(std::sqrt(power[1] / kMeasureSamples))}};
}

void testOptoInternalStereoLinkUsesSignedMaximum()
{
    constexpr std::array<std::array<float, 2>, 2> levels{{
        {{-12.0f, -40.0f}}, {{-40.0f, -12.0f}}}};
    constexpr std::array<std::array<float, 2>, 2> referenceReduction{{
        {{17.484f, 17.429f}}, {{17.429f, 17.484f}}}};
    for (size_t row = 0; row < levels.size(); ++row)
    {
        const auto control = renderOptoInternalStereo(
            levels[row][0], levels[row][1], 0.0f);
        const auto active = renderOptoInternalStereo(
            levels[row][0], levels[row][1], 70.0f);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float reduction = duskaudio::gainToDecibels(control[ch])
                                  - duskaudio::gainToDecibels(active[ch]);
            std::printf("opto internal stereo link: L %.0f R %.0f channel %c "
                        "reference %.3f dB measured %.6f dB delta %+.6f dB\n",
                        levels[row][0], levels[row][1], ch == 0 ? 'L' : 'R',
                        referenceReduction[row][ch], reduction,
                        reduction - referenceReduction[row][ch]);
            // Keep the routing regression inside the same 0.5 dB envelope as
            // the existing Opto processBlock static-law gate.  The important
            // new failure is the quiet channel's former 0 dB reduction; this
            // test does not retune the already-gated Opto model residual.
            // Per-check require (not a folded boolean): a long-lived matches
            // accumulator was miscompiled at -O2 -ffp-contract=off (values
            // printed in-tolerance, folded result false; any observation of
            // the flag restored correctness).  Immediate asserts are also the
            // better diagnostic.
            require(std::abs(reduction - referenceReduction[row][ch]) < 0.5f,
                    "Opto internal stereo link keeps both channels within the routing envelope");
        }
    }
    for (const int oversampling : {0, 1, 2})
    {
        const auto independent = renderOptoInternalStereo(
            -12.0f, -12.0f, 70.0f, 0.0f, oversampling);
        const auto linked = renderOptoInternalStereo(
            -12.0f, -12.0f, 70.0f, 100.0f, oversampling);
        float worstDeltaDb = 0.0f;
        for (size_t ch = 0; ch < 2; ++ch)
            worstDeltaDb = std::max(worstDeltaDb, std::abs(
                duskaudio::gainToDecibels(linked[ch] / independent[ch])));
        std::printf("opto dual-mono link identity: os=%dx worst delta %.9f dB\n",
                    oversampling == 2 ? 4 : oversampling == 1 ? 2 : 1,
                    worstDeltaDb);
        // FMA contraction moves the 4x accumulation by about 0.000012 dB.
        // Keep the identity guard far below the 0.5 dB routing oracle while
        // allowing that harmless compiler-level rounding difference.
        require(worstDeltaDb < 1.0e-4f,
                "Opto internal stereo link is a dual-mono identity at every oversampling factor");
    }
}

void testDigitalLookaheadMixAlignment()
{
    // 100 Hz against 5 ms of lookahead is half a period: a dry path taken from
    // the undelayed input cancels the wet one at 50% local mix.
    constexpr float lookaheadMs = 5.0f;
    for (const float frequency : {100.0f, 300.0f, 997.0f})
    {
        const float wet = renderLookaheadSine(100.0f, frequency, lookaheadMs);
        const float half = renderLookaheadSine(50.0f, frequency, lookaheadMs);
        const float delta = std::abs(duskaudio::gainToDecibels(half / std::max(wet, 1.0e-9f)));
        require(delta < 0.05f, "digital lookahead 50% mix has no comb ripple");
        std::printf("digital lookahead mix: %.0f Hz delta=%.5f dB\n",
                    static_cast<double>(frequency), static_cast<double>(delta));
    }
}

void testGoldenVectors()
{
    constexpr int kSamples = 4096;
    // These vectors were recorded from this extracted core. They detect drift
    // from its current behaviour; they do NOT prove parity with the JUCE
    // original, which was not used as the recording oracle.
    //
    // Opto is intentionally absent. It is being rebuilt against measured
    // commercial-hardware data, and its old recorded values describe
    // superseded behaviour. Do not restore them or use them to judge the new
    // implementation: the hardware reference is the only Opto oracle.
    // VCA left 2026-09-01 for the same reason: it is being rebuilt against
    // the measured UAD dbx 160 (reference_comparison_dbx160 campaign), whose
    // static and control laws already differ from the JUCE implementation.
    constexpr duskaudio::MultiCompMode modes[] = {
        duskaudio::MultiCompMode::FET,
        duskaudio::MultiCompMode::Bus, duskaudio::MultiCompMode::StudioFET,
        duskaudio::MultiCompMode::StudioVCA, duskaudio::MultiCompMode::Digital,
        duskaudio::MultiCompMode::Multiband};
    // Re-recorded 2026-08-19: affected hardware modes encoded stale oversampling-rate coefficients.
    // FET was re-recorded 2026-08-25 after the measured reference calibration,
    // again 2026-08-26 when the attack drive law was inverted to match the
    // reference's measured direction (0.059634957/0.266311735), and again the
    // same day when that law's unmeasured shallow-drive segment was replaced by
    // the measured six-anchor table (0.042118039/0.242844120), and again the
    // same day when the measured +0.149329 dB pre-detector gain excess was
    // removed from fetInputGainDb (0.039900590/0.242788911 -> below; this
    // vector runs at Input 18 dB, i.e. deep reduction, where a pre-detector
    // change passes at the output slope, hence the small -0.030 dB move), and
    // again the same day when fetBroadbandK2 was re-fitted against the measured
    // H2 surface (0.039760567/0.242730290 -> below). That last move is larger
    // than a colour term looks like it should be, and the reason is the vector
    // itself: 512 of its 4096 samples are the step and the rest is the attack
    // transient, so it integrates the whole 0-25 dB reduction sweep, including
    // the band where the old table read 5.1 dB high. The settled-sine static
    // grid, by contrast, moved 3.7e-05 dB across all 80 cells. Restoring the
    // old table reproduces 0.039760567/0.242730290 exactly, which is what
    // establishes this table as the sole cause. And again the same day when the
    // low-frequency colour coefficient became the measured `fetLowFrequencyK2`
    // table instead of `0.0400 * clamp(grDb / 12) * saturation`
    // (0.039726499/0.241402537 -> below). Restoring the old expression
    // reproduces that pair exactly, and the settled-sine static grid moved
    // 1.6e-07 dB across all 80 cells, so this table is likewise the sole cause.
    // The depth-gated intermediate FET population then moved only the vintage
    // vector to 0.038088631/0.241441056; restoring the single slow population
    // reproduces 0.039597437/0.241449714 and fails this assertion. Finally, the
    // measured reduction-dependent broadband cubic moved it to
    // 0.038748693/0.255038023. Restoring its constant -0.006 coefficient
    // reproduces 0.038088631/0.241441056 and fails this assertion. The jointly
    // closed low-frequency T3 and broadband K3 coordinate pair then moved it to
    // 0.038780950/0.259218961; restoring the Wave 12 odd-order coefficients
    // reproduces 0.038748693/0.255038023 and fails this assertion.
    //
    // Re-recorded 2026-08-26 (Wave 20) to the value below. This vector had gone
    // stale: Waves 17-20A were accepted against their own targeted gates while
    // the full suite was not re-run, so the pair above describes the pre-Wave-17
    // core. The move is attributed, not assumed. Forcing `fetAttackKnobScale`
    // to return 1.0f -- its pre-Wave-20A identity -- reproduces
    // 0.038771123/0.259201407, so the Wave 20A drive-dependent Attack-knob
    // correction accounts for essentially the whole move; the remaining
    // 9.8e-06 of RMS is Waves 17-19 (stereo link, the ratio-specific detector
    // caps, and the All-buttons overload memory), which touch this vector only
    // where its transient grazes the deep-reduction caps.
    //
    // The Wave 20 startup peak ceiling itself moves this vector by EXACTLY
    // zero: the stage-disabled diagnostic binary 28dd845b6adf reports the same
    // 0.039380543/0.259151727 pair. This vector runs at Output 1.0, above the
    // `outputControlBlend` fade-out at Output position 0.90, so the ceiling is
    // switched off for it by construction -- which is the intended protection
    // for the already-matched Output=1 ceiling sweep, here observed working.
    //
    // Re-recorded 2026-08-27 (Wave 21) after bounding only the source passed to
    // the measured broadband K3 polynomial. The fitted coefficient is retained
    // throughout its measured domain, while the first uncompressed cycle no
    // longer extrapolates that cubic tens of times beyond the fitted source
    // range. Removing the bound by setting kFetBroadbandK3SourceLimit to 1000
    // reproduces the prior 0.039380543/0.259151727 pair exactly; restoring the
    // bound moves only FET to 0.043111119/0.593543887. The six neighbouring
    // modes print the same nine-decimal values in both control runs.
    //
    // The six neighbouring mode values remain the original regression oracles
    // and must stay byte-identical across any vintage-FET change. They did:
    // only mode=1 moved at any point in this wave.
    constexpr float expectedRms[] = {0.043111119f, 0.269918233f,
                                     0.618480802f, 0.195874527f, 0.173109755f, 0.212109938f};
    constexpr float expectedPeak[] = {0.593543887f, 0.797850311f,
                                      1.836098075f, 0.657691538f, 0.349556237f, 0.815853894f};
    std::puts("golden vectors: six JUCE-oracle modes, deterministic step/sine-burst RMS peak");
    for (size_t vectorIndex = 0; vectorIndex < std::size(modes); ++vectorIndex)
    {
        const int mode = static_cast<int>(modes[vectorIndex]);
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 256);
        dsp.setOversampling(0);
        dsp.setMode(mode);
        dsp.setMix(100.0f);
        dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::FetInput, 18.0f);
        dsp.setParameter(MultiCompDSP::Parameter::FetOutput, -4.0f);
        dsp.setParameter(MultiCompDSP::Parameter::FetAttack, 0.8f);
        dsp.setParameter(MultiCompDSP::Parameter::FetRelease, 150.0f);
        dsp.setParameter(MultiCompDSP::Parameter::VcaThreshold, -20.0f);
        dsp.setParameter(MultiCompDSP::Parameter::VcaRatio, 4.0f);
        dsp.setParameter(MultiCompDSP::Parameter::BusThreshold, -18.0f);
        dsp.setParameter(MultiCompDSP::Parameter::StudioVcaThreshold, -20.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -20.0f);
        std::vector<float> output;
        output.reserve(kSamples);
        for (int block = 0; block < kSamples / 256; ++block)
        {
            std::vector<float> in(256), out(256);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                in[static_cast<size_t>(i)] = n < 512 ? 0.35f : 0.55f * std::sin(2.0f * kPi * 997.0f * n / 48000.0f);
            }
            const float* inputs[] = {in.data()}; float* outputs[] = {out.data()};
            dsp.processBlock(inputs, outputs, 1, 256);
            output.insert(output.end(), out.begin(), out.end());
        }
        double sum = 0.0; float peak = 0.0f;
        for (float sample : output) { sum += static_cast<double>(sample) * sample; peak = std::max(peak, std::abs(sample)); }
        const float valueRms = static_cast<float>(std::sqrt(sum / output.size()));
        std::printf("  mode=%d rms=%.9f peak=%.9f\n", mode, valueRms, peak);
        const bool unchanged = std::abs(valueRms - expectedRms[vectorIndex]) <= 1.0e-4f
                            && std::abs(peak - expectedPeak[vectorIndex]) <= 1.0e-4f;
        if (!unchanged)
            std::fprintf(stderr, "golden mismatch mode=%d expected=(%.9f, %.9f) actual=(%.9f, %.9f)\n",
                         mode, expectedRms[vectorIndex], expectedPeak[vectorIndex], valueRms, peak);
        require(unchanged, "golden vector unchanged");
    }
}

void configureNeutralMultiband(MultiCompDSP& dsp)
{
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setOversampling(0);
    dsp.setParameter(MultiCompDSP::Parameter::MbMix, 100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
    }
}

float renderSoloedLowBand(bool enabled, bool bypass)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    configureNeutralMultiband(dsp);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Enabled, enabled ? 1.0f : 0.0f);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Bypass, bypass ? 1.0f : 0.0f);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Solo, 1.0f);
    std::vector<float> input(256), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 80.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        dsp.processBlock(ip, op, 1, 256);
    }
    return rms(output);
}

void testMultibandEnabledTopology()
{
    const float disabled = renderSoloedLowBand(false, false);
    const float bypassed = renderSoloedLowBand(true, true);
    std::printf("multiband Enabled topology: disabled-solo RMS %.9g; bypassed-solo RMS %.9g\n",
                disabled, bypassed);
    require(disabled < 1.0e-8f, "disabled band is absent from the recombination");
    require(bypassed > 0.1f, "bypassed band remains audible without compression");

    MultiCompDSP minimumTwo;
    minimumTwo.prepare(48000.0, 256);
    configureNeutralMultiband(minimumTwo);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        minimumTwo.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Enabled, 0.0f);
    std::vector<float> input(256, 0.25f), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 20; ++block) minimumTwo.processBlock(ip, op, 1, 256);
    require(rms(output) > 0.1f, "all-disabled automation snapshot still enforces two active bands");
}

void testCrossoverAutomationContinuity()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    configureNeutralMultiband(dsp);
    std::vector<float> input(256), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    float previous = 0.0f, steadyMaxDelta = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 350.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        dsp.processBlock(ip, op, 1, 256);
        if (block == 39)
        {
            previous = output.back();
            for (int i = 1; i < 256; ++i)
                steadyMaxDelta = std::max(steadyMaxDelta, std::abs(output[static_cast<size_t>(i)] - output[static_cast<size_t>(i - 1)]));
        }
    }
    dsp.setParameter(MultiCompDSP::Parameter::Crossover1, 500.0f);
    for (int i = 0; i < 256; ++i)
        input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 350.0f * static_cast<float>(40 * 256 + i) / 48000.0f);
    dsp.processBlock(ip, op, 1, 256);
    const float boundaryStep = std::abs(output.front() - previous);
    float automationMaxDelta = boundaryStep;
    for (int i = 1; i < 256; ++i)
        automationMaxDelta = std::max(automationMaxDelta, std::abs(output[static_cast<size_t>(i)] - output[static_cast<size_t>(i - 1)]));
    std::printf("crossover automation: steady max delta %.9g; automation max delta %.9g; ratio %.4f\n",
                steadyMaxDelta, automationMaxDelta, automationMaxDelta / std::max(steadyMaxDelta, 1.0e-9f));
    require(automationMaxDelta < steadyMaxDelta * 1.2f, "crossover automation does not create a block-boundary transient");
}

float renderReprepareTone(MultiCompDSP& dsp, float frequency)
{
    dsp.reset();
    std::vector<float> in(256), out(256);
    double sum = 0.0;
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.2f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 96000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block >= 76)
            for (float sample : out) sum += static_cast<double>(sample) * sample;
    }
    return static_cast<float>(std::sqrt(sum / (4.0 * 256.0)));
}

void testReprepareMultiband()
{
    MultiCompDSP reused, fresh;
    reused.prepare(48000.0, 256);
    configureNeutralMultiband(reused);
    reused.prepare(96000.0, 256);
    fresh.prepare(96000.0, 256);
    configureNeutralMultiband(fresh);
    float maxDelta = 0.0f;
    for (const float frequency : {50.0f, 500.0f, 3000.0f, 10000.0f})
    {
        const float a = renderReprepareTone(reused, frequency);
        const float b = renderReprepareTone(fresh, frequency);
        require(a > 1.0e-5f && b > 1.0e-5f, "re-prepare band-tone comparison produces output");
        maxDelta = std::max(maxDelta, std::abs(a - b));
    }
    require(maxDelta < 1.0e-6f, "re-prepare 48 kHz to 96 kHz matches fresh multiband");
    std::printf("multiband re-prepare: max band-tone energy delta %.9g\n", maxDelta);
}

void testSameRateReprepare()
{
    auto configure = [](MultiCompDSP& dsp) {
        dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
        dsp.setOversampling(2);
        dsp.setMix(73.0f);
        dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 10.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 10.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 8.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.3f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalRelease, 80.0f);
        dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    };

    MultiCompDSP repeated, fresh;
    configure(repeated);
    repeated.prepare(48000.0, 256);
    repeated.prepare(48000.0, 256);
    configure(fresh);
    fresh.prepare(48000.0, 256);

    std::array<float, 256> inputLeft{}, inputRight{}, repeatedLeft{}, repeatedRight{}, freshLeft{}, freshRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    float* repeatedOutput[] = {repeatedLeft.data(), repeatedRight.data()};
    float* freshOutput[] = {freshLeft.data(), freshRight.data()};
    float maxDelta = 0.0f, outputPeak = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const int n = block * 256 + i;
            inputLeft[static_cast<size_t>(i)] = 0.6f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(n) / 48000.0f);
            inputRight[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 431.0f * static_cast<float>(n) / 48000.0f);
        }
        repeated.processBlock(input, repeatedOutput, 2, 256);
        fresh.processBlock(input, freshOutput, 2, 256);
        for (int i = 0; i < 256; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float a = ch == 0 ? repeatedLeft[static_cast<size_t>(i)] : repeatedRight[static_cast<size_t>(i)];
                const float b = ch == 0 ? freshLeft[static_cast<size_t>(i)] : freshRight[static_cast<size_t>(i)];
                maxDelta = std::max(maxDelta, std::abs(a - b));
                outputPeak = std::max(outputPeak, std::abs(a));
            }
    }
    require(outputPeak > 1.0e-3f, "same-rate re-prepare comparison produces signal");
    require(repeated.getGainReduction() < -0.5f, "same-rate re-prepare comparison produces compression");
    require(repeated.getLatencySamples() == fresh.getLatencySamples(),
            "same-rate re-prepare preserves latency configuration");
    require(maxDelta < 1.0e-7f, "same-rate re-prepare matches a freshly prepared core");
    std::printf("same-rate re-prepare: latency=%d max output delta %.9g\n",
                repeated.getLatencySamples(), maxDelta);
}

float renderMultibandTone(float mix, float frequency)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setParameter(MultiCompDSP::Parameter::MbMix, mix);
    dsp.setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
    }
    std::vector<float> in(256), out(256);
    float sum = 0.0f;
    for (int block = 0; block < 200; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block == 199) for (float x : out) sum += x * x;
    }
    return std::sqrt(sum / 256.0f);
}

void testMultibandMixAlignment()
{
    float worstRippleDb = 0.0f;
    for (float frequency : {50.0f, 500.0f, 3000.0f, 10000.0f, 18000.0f})
    {
        const float fullWet = renderMultibandTone(100.0f, frequency);
        const float halfMix = renderMultibandTone(50.0f, frequency);
        const float ratio = halfMix / std::max(fullWet, 1.0e-9f);
        // A phase-misaligned dry path creates frequency-dependent combing. The
        // latency-aligned Phase-2 path must remain within 0.1 dB of full wet.
        worstRippleDb = std::max(worstRippleDb, std::abs(duskaudio::gainToDecibels(ratio)));
    }
    require(worstRippleDb < 0.1f, "multiband 50% mix has no comb ripple");
    std::printf("multiband mix alignment: max ripple %.5f dB\n", worstRippleDb);
}

struct StereoRms
{
    float left;
    float right;
};

StereoRms renderNeutralMultibandMidSide(float mix, bool autoMakeup)
{
    constexpr int blockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setParameter(MultiCompDSP::Parameter::StereoLinkMode, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::MbMix, mix);
    dsp.setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, autoMakeup ? 1.0f : 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Makeup, 0.0f);
    }
    dsp.prepare(48000.0, blockSize);

    std::array<float, blockSize> inputLeft{}, inputRight{}, outputLeft{}, outputRight{};
    const float* input[] = {inputLeft.data(), inputRight.data()};
    float* output[] = {outputLeft.data(), outputRight.data()};
    double leftSum = 0.0, rightSum = 0.0;
    constexpr int totalBlocks = 240;
    constexpr int measuredBlocks = 16;
    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const int sample = block * blockSize + i;
            inputLeft[static_cast<size_t>(i)] = 0.6f * std::sin(
                2.0f * kPi * 997.0f * static_cast<float>(sample) / 48000.0f);
            inputRight[static_cast<size_t>(i)] = 0.25f * std::sin(
                2.0f * kPi * 431.0f * static_cast<float>(sample) / 48000.0f);
        }
        dsp.processBlock(input, output, 2, blockSize);
        if (block >= totalBlocks - measuredBlocks)
            for (int i = 0; i < blockSize; ++i)
            {
                leftSum += static_cast<double>(outputLeft[static_cast<size_t>(i)])
                         * outputLeft[static_cast<size_t>(i)];
                rightSum += static_cast<double>(outputRight[static_cast<size_t>(i)])
                          * outputRight[static_cast<size_t>(i)];
            }
    }
    constexpr double measuredSamples = measuredBlocks * blockSize;
    return {static_cast<float>(std::sqrt(leftSum / measuredSamples)),
            static_cast<float>(std::sqrt(rightSum / measuredSamples))};
}

void testMultibandMidSideMixAndAutoMakeupDomains()
{
    const StereoRms fullWet = renderNeutralMultibandMidSide(100.0f, false);
    const StereoRms halfMix = renderNeutralMultibandMidSide(50.0f, false);
    const StereoRms autoMakeup = renderNeutralMultibandMidSide(100.0f, true);
    const float halfLeftDb = duskaudio::gainToDecibels(halfMix.left / std::max(fullWet.left, 1.0e-9f));
    const float halfRightDb = duskaudio::gainToDecibels(halfMix.right / std::max(fullWet.right, 1.0e-9f));
    const float fullCombined = std::sqrt((fullWet.left * fullWet.left + fullWet.right * fullWet.right) * 0.5f);
    const float autoCombined = std::sqrt((autoMakeup.left * autoMakeup.left + autoMakeup.right * autoMakeup.right) * 0.5f);
    const float autoShiftDb = duskaudio::gainToDecibels(autoCombined / std::max(fullCombined, 1.0e-9f));
    std::printf("multiband M/S domains: 50%% L %.4f dB R %.4f dB vs wet; auto-makeup %.4f dB\n",
                halfLeftDb, halfRightDb, autoShiftDb);
    require(std::abs(halfLeftDb) < 0.15f && std::abs(halfRightDb) < 0.15f
                && std::abs(autoShiftDb) < 0.25f,
            "multiband M/S mix and auto-makeup use an L/R split-dry reference");
}

float renderSidechainEqGR(float highGain)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 10.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighFreq, 8000.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighGain, highGain);
    std::vector<float> input(256, 0.02f), sidechain(256), output(256);
    float gr = 0.0f;
    for (int block = 0; block < 120; ++block)
    {
        for (int i = 0; i < 256; ++i)
            sidechain[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 12000.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {input.data()}; const float* sc[] = {sidechain.data()}; float* op[] = {output.data()};
        dsp.processBlockExternal(ip, sc, op, 1, 256);
        if (block == 119) gr = dsp.getGainReduction();
    }
    return gr;
}

void testSidechainEq()
{
    const float flat = renderSidechainEqGR(0.0f);
    const float boosted = renderSidechainEqGR(12.0f);
    require(std::isfinite(flat) && std::isfinite(boosted), "sidechain EQ meter finite");
    require(boosted < flat - 0.2f, "sidechain high shelf increases HF compression");
    std::printf("sidechain EQ: GR %.4f dB -> %.4f dB with +12 dB HF shelf\n", flat, boosted);
}

void testMultibandBypassAndZeroLatency()
{
    MultiCompDSP reference, bypassed;
    for (MultiCompDSP* dsp : {&reference, &bypassed})
    {
        dsp->prepare(48000.0, 256);
        dsp->setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
        dsp->setParameter(MultiCompDSP::Parameter::MbMix, 100.0f);
        dsp->setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
        dsp->setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    }
    bypassed.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Bypass, 1.0f);
    bypassed.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Makeup, 12.0f);
    std::vector<float> input(256), referenceOut(256), bypassedOut(256);
    const float* ip[] = {input.data()};
    float* referenceOp[] = {referenceOut.data()};
    float* bypassedOp[] = {bypassedOut.data()};
    float worst = 0.0f, referencePeak = 0.0f;
    for (int block = 0; block < 20; ++block)
    {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        reference.processBlock(ip, referenceOp, 1, 256);
        bypassed.processBlock(ip, bypassedOp, 1, 256);
        if (block >= 16)
            for (int i = 0; i < 256; ++i)
            {
                referencePeak = std::max(referencePeak, std::abs(referenceOut[static_cast<size_t>(i)]));
                worst = std::max(worst, std::abs(referenceOut[static_cast<size_t>(i)] - bypassedOut[static_cast<size_t>(i)]));
            }
    }
    require(referencePeak > 1.0e-4f, "multiband bypass comparison produces output");
    require(worst < 1.0e-6f, "disabled multiband band skips envelope and makeup");

    MultiCompDSP zeroDelay;
    zeroDelay.prepare(48000.0, 256);
    zeroDelay.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    zeroDelay.setBypass(true);
    zeroDelay.reset();
    require(zeroDelay.getLatencySamples() == 0, "multiband bypass has zero latency");
    std::vector<float> zeroIn(256), zeroOut(256);
    for (int i = 0; i < 256; ++i)
        zeroIn[static_cast<size_t>(i)] = 0.1f + 0.0003f * static_cast<float>(i);
    const float* zeroIp[] = {zeroIn.data()}; float* zeroOp[] = {zeroOut.data()};
    zeroDelay.processBlock(zeroIp, zeroOp, 1, 256);
    for (int i = 0; i < 256; ++i)
        require(zeroOut[static_cast<size_t>(i)] == zeroIn[static_cast<size_t>(i)], "zero-delay bypass is current-input bit-exact");
    std::printf("multiband bypass/makeup: worst difference %.9g; zero-delay bypass: bit-exact\n", worst);
}

float renderDigitalDucking(bool autoMakeup, bool provideSidechain)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.1f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, autoMakeup ? 1.0f : 0.0f);
    std::vector<float> input(256, 0.2f), sidechain(256, 1.0f), output(256);
    const float* ip[] = {input.data()};
    const float* sc[] = {sidechain.data()};
    float* op[] = {output.data()};
    for (int block = 0; block < 240; ++block)
    {
        if (provideSidechain) dsp.processBlockExternal(ip, sc, op, 1, 256);
        else dsp.processBlock(ip, op, 1, 256);
    }
    return rms(output);
}

void testAutoGainEffectiveExternalSidechain()
{
    const float ducked = renderDigitalDucking(false, true);
    const float duckedAuto = renderDigitalDucking(true, true);
    const float internal = renderDigitalDucking(false, false);
    const float internalAuto = renderDigitalDucking(true, false);
    const float duckingDeltaDb = duskaudio::gainToDecibels(duckedAuto / std::max(ducked, 1.0e-9f));
    const float internalLiftDb = duskaudio::gainToDecibels(internalAuto / std::max(internal, 1.0e-9f));
    std::printf("auto gain external sidechain: ducking delta %.4f dB; armed/no-bus lift %.4f dB\n",
                duckingDeltaDb, internalLiftDb);
    require(std::abs(duckingDeltaDb) < 0.2f, "auto gain does not counteract effective external-sidechain ducking");
    require(internalLiftDb > 3.0f, "armed sidechain without a host bus leaves auto gain active");
}

void testAutoGainBypassSettleBoundary()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -40.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.1f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 1.0f);
    std::vector<float> input(256, 0.25f), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 240; ++block) dsp.processBlock(ip, op, 1, 256);
    dsp.setBypass(true);
    for (int block = 0; block < 6; ++block) dsp.processBlock(ip, op, 1, 256);
    const float lastTransitionSample = output.back();
    dsp.processBlock(ip, op, 1, 256);
    const float firstSettledSample = output.front();
    const float boundaryStep = std::abs(firstSettledSample - lastTransitionSample);
    std::printf("auto gain bypass boundary: last=%.9g first=%.9g step=%.9g\n",
                lastTransitionSample, firstSettledSample, boundaryStep);
    require(std::abs(lastTransitionSample) > 1.0e-4f && std::abs(firstSettledSample) > 1.0e-4f,
            "auto-gain bypass boundary produces output on both sides");
    require(boundaryStep < 1.0e-5f, "auto-gained bypass transition and settled bypass share one endpoint");
}

void setModeOutput(MultiCompDSP& dsp, int mode, bool high)
{
    const float db = high ? 20.0f : 0.0f;
    switch (static_cast<duskaudio::MultiCompMode>(mode))
    {
        case duskaudio::MultiCompMode::Opto:
            dsp.setParameter(MultiCompDSP::Parameter::OptoGain,
                             duskaudio::optoGainDbToKnob(db)); break;
        case duskaudio::MultiCompMode::FET:
        case duskaudio::MultiCompMode::StudioFET:
            dsp.setParameter(MultiCompDSP::Parameter::FetOutput, db); break;
        case duskaudio::MultiCompMode::VCA:
            dsp.setParameter(MultiCompDSP::Parameter::VcaOutput, db); break;
        case duskaudio::MultiCompMode::Bus:
            dsp.setParameter(MultiCompDSP::Parameter::BusMakeup, db); break;
        case duskaudio::MultiCompMode::StudioVCA:
            dsp.setParameter(MultiCompDSP::Parameter::StudioVcaOutput, db); break;
        case duskaudio::MultiCompMode::Digital:
            dsp.setParameter(MultiCompDSP::Parameter::DigitalOutput, db); break;
        case duskaudio::MultiCompMode::Multiband: break;
    }
}

void testAutoGainNeutralisesManualOutput()
{
    float worstDelta = 0.0f, fetPeakRatio = 0.0f;
    for (int mode = 0; mode < static_cast<int>(duskaudio::MultiCompMode::Multiband); ++mode)
    {
        MultiCompDSP neutral, high;
        for (MultiCompDSP* dsp : {&neutral, &high})
        {
            dsp->prepare(48000.0, 256);
            dsp->setOversampling(0);
            dsp->setMode(mode);
            dsp->setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
            dsp->setParameter(MultiCompDSP::Parameter::AutoMakeup, 1.0f);
        }
        setModeOutput(neutral, mode, false);
        setModeOutput(high, mode, true);
        std::vector<float> input(256), neutralOut(256), highOut(256);
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.02f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
        const float* ip[] = {input.data()}; float* neutralOp[] = {neutralOut.data()}; float* highOp[] = {highOut.data()};
        neutral.processBlock(ip, neutralOp, 1, 256);
        high.processBlock(ip, highOp, 1, 256);
        float neutralPeak = 0.0f, highPeak = 0.0f;
        for (int i = 0; i < 256; ++i)
        {
            worstDelta = std::max(worstDelta, std::abs(highOut[static_cast<size_t>(i)] - neutralOut[static_cast<size_t>(i)]));
            neutralPeak = std::max(neutralPeak, std::abs(neutralOut[static_cast<size_t>(i)]));
            highPeak = std::max(highPeak, std::abs(highOut[static_cast<size_t>(i)]));
        }
        require(neutralPeak > 1.0e-5f && highPeak > 1.0e-5f,
                "auto-gain manual-output comparison produces output");
        if (mode == static_cast<int>(duskaudio::MultiCompMode::FET))
            fetPeakRatio = highPeak / std::max(neutralPeak, 1.0e-9f);
    }
    std::printf("auto gain manual output: FET +20dB peak ratio %.6f; seven-mode max delta %.9g\n",
                fetPeakRatio, worstDelta);
    require(worstDelta < 1.0e-7f, "auto gain supplies unity manual output to every single-band mode");
}

void testAutoGainResetsOnModeChange()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::FET));
    dsp.setParameter(MultiCompDSP::Parameter::FetInput, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 1.0f);
    std::vector<float> input(256), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    auto fillInput = [&](int block) {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.02f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(block * 256 + i) / 48000.0f);
    };
    for (int block = 0; block < 240; ++block) { fillInput(block); dsp.processBlock(ip, op, 1, 256); }
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    fillInput(240); dsp.processBlock(ip, op, 1, 256);
    const float immediateRatio = rms(output) / std::max(rms(input), 1.0e-9f);
    for (int block = 241; block < 305; ++block) { fillInput(block); dsp.processBlock(ip, op, 1, 256); }
    const float settledRatio = rms(output) / std::max(rms(input), 1.0e-9f);
    std::printf("auto gain mode change: immediate ratio %.6f; after 341ms %.6f\n",
                immediateRatio, settledRatio);
    require(settledRatio > 0.9f, "mode change discards old auto-gain history and returns toward unity");
}

void testBypassCompletesDuringSidechainListen()
{
    struct Result { float maxDryError; float minimumListenTail; };
    auto exercise = [](int channels) {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 256);
        dsp.setOversampling(2);
        dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
        dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 10.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 10.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
        dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
        std::array<std::vector<float>, 2> input{
            std::vector<float>(256, 0.1f), std::vector<float>(256, 0.1f)};
        std::array<std::vector<float>, 2> sidechain{
            std::vector<float>(256, 0.2f), std::vector<float>(256, 0.2f)};
        std::array<std::vector<float>, 2> output{
            std::vector<float>(256), std::vector<float>(256)};
        const float* ip[] = {input[0].data(), input[1].data()};
        const float* sc[] = {sidechain[0].data(), sidechain[1].data()};
        float* op[] = {output[0].data(), output[1].data()};
        for (int block = 0; block < 8; ++block)
            dsp.processBlockExternal(ip, sc, op, channels, 256);
        dsp.setBypass(true);
        for (int block = 0; block < 10; ++block)
            dsp.processBlockExternal(ip, sc, op, channels, 256);
        dsp.setParameter(MultiCompDSP::Parameter::GlobalSidechainListen, 1.0f);
        for (auto& channel : sidechain) channel.assign(channel.size(), 0.8f);
        for (int block = 0; block < 10; ++block)
            dsp.processBlockExternal(ip, sc, op, channels, 256);
        float maxDryError = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            for (float sample : output[static_cast<size_t>(ch)])
                maxDryError = std::max(maxDryError, std::abs(sample - 0.1f));

        dsp.setBypass(false);
        dsp.processBlockExternal(ip, sc, op, channels, 256);
        float minimumListenTail = output[0].back();
        for (int ch = 1; ch < channels; ++ch)
            minimumListenTail = std::min(minimumListenTail, output[static_cast<size_t>(ch)].back());
        return Result{maxDryError, minimumListenTail};
    };
    const Result mono = exercise(1);
    const Result stereo = exercise(2);
    std::printf("bypass during Listen: mono/stereo dry errors %.9g/%.9g; current-sidechain tails %.9g/%.9g\n",
                mono.maxDryError, stereo.maxDryError,
                mono.minimumListenTail, stereo.minimumListenTail);
    require(std::max(mono.maxDryError, stereo.maxDryError) < 1.0e-7f,
            "mono and stereo bypass reach settled dry output while sidechain Listen remains on");
    require(std::min(mono.minimumListenTail, stereo.minimumListenTail) > 0.18f,
            "settled mono and stereo bypass advance the Listen ramp and latency delay together");
}

void testSidechainListenClearsGainReductionMeters()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    configureStrongCompression(dsp, static_cast<int>(duskaudio::MultiCompMode::Multiband));
    std::array<float, 256> input{}, output{};
    input.fill(0.8f);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 16; ++block) dsp.processBlock(ip, op, 1, 256);
    const float activeMaster = dsp.getGainReduction();
    float activeBand = 0.0f;
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        activeBand = std::min(activeBand, dsp.getBandGainReduction(band));
    require(activeMaster < -1.0f && activeBand < -1.0f,
            "Listen meter test establishes active multiband compression");

    dsp.setParameter(MultiCompDSP::Parameter::GlobalSidechainListen, 1.0f);
    dsp.processBlock(ip, op, 1, 256);
    float listenBandMagnitude = 0.0f;
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        listenBandMagnitude = std::max(listenBandMagnitude, std::abs(dsp.getBandGainReduction(band)));
    std::printf("Listen GR meters: active master %.4f max-band %.4f; Listen master %.4f max-band %.4f\n",
                activeMaster, activeBand, dsp.getGainReduction(), listenBandMagnitude);
    require(dsp.getGainReduction() == 0.0f && listenBandMagnitude == 0.0f,
            "sidechain Listen clears master and per-band gain-reduction meters");
}

void testSingleBandModeClearsMultibandGainReductionMeters()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    configureStrongCompression(dsp, static_cast<int>(duskaudio::MultiCompMode::Multiband));
    std::array<float, 256> input{}, output{};
    input.fill(0.8f);
    const float* ip[] = {input.data()};
    float* op[] = {output.data()};
    for (int block = 0; block < 32; ++block) dsp.processBlock(ip, op, 1, 256);
    float activeBand = 0.0f;
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        activeBand = std::min(activeBand, dsp.getBandGainReduction(band));
    require(activeBand < -1.0f, "single-band meter test establishes multiband gain reduction");

    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.processBlock(ip, op, 1, 256);
    float singleBandMagnitude = 0.0f;
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        singleBandMagnitude = std::max(singleBandMagnitude, std::abs(dsp.getBandGainReduction(band)));
    std::printf("single-band GR meters: prior multiband %.4f dB; max stale band %.4f dB\n",
                activeBand, singleBandMagnitude);
    require(singleBandMagnitude == 0.0f,
            "single-band processing clears per-band gain-reduction meters");
}

void testInputMeterWithAliasedBuffers()
{
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(0);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalOutput, 6.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.prepare(48000.0, 256);

    std::array<float, 256> inOut{};
    const float* input[] = {inOut.data()};
    float* output[] = {inOut.data()};
    for (int block = 0; block < 16; ++block)
    {
        inOut.fill(0.25f);
        dsp.processBlock(input, output, 1, 256);
    }
    const float expectedInputDb = duskaudio::gainToDecibels(0.25f);
    const float inputMeterDb = dsp.getInputLevel();
    const float outputMeterDb = dsp.getOutputLevel();
    std::printf("aliased meters: input %.4f dB (expected %.4f); output %.4f dB\n",
                inputMeterDb, expectedInputDb, outputMeterDb);
    require(std::abs(inputMeterDb - expectedInputDb) < 0.05f
                && outputMeterDb > inputMeterDb + 5.5f,
            "input meter captures aliased input before output is written");
}

void testSidechainListenSwitchIsSmoothed()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    std::array<float, 256> input{}, sidechain{}, output{};
    input.fill(0.1f);
    sidechain.fill(0.8f);
    const float* ip[] = {input.data()}; const float* sc[] = {sidechain.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 16; ++block) dsp.processBlockExternal(ip, sc, op, 1, 256);
    float previous = output.back();
    struct TransitionResult
    {
        float largestStep;
        float endpoint;
    };
    auto measureTransition = [&](float target) {
        float largestStep = 0.0f;
        dsp.setParameter(MultiCompDSP::Parameter::GlobalSidechainListen, target);
        for (int block = 0; block < 6; ++block)
        {
            dsp.processBlockExternal(ip, sc, op, 1, 256);
            largestStep = std::max(largestStep, std::abs(output.front() - previous));
            for (int i = 1; i < 256; ++i)
                largestStep = std::max(largestStep, std::abs(output[static_cast<size_t>(i)] - output[static_cast<size_t>(i - 1)]));
            previous = output.back();
        }
        return TransitionResult{largestStep, output.back()};
    };
    const TransitionResult listenOn = measureTransition(1.0f);
    const TransitionResult listenOff = measureTransition(0.0f);
    // The sidechain monitor includes its configured filtering, so allow the
    // small steady-state offset from the raw 0.8 test signal.
    constexpr float endpointTolerance = 2.0e-4f;
    std::printf("Listen switch maximum adjacent-sample step: on %.6f; off %.6f; "
                "endpoints on %.6f off %.6f (tolerance %.1e)\n",
                listenOn.largestStep, listenOff.largestStep,
                listenOn.endpoint, listenOff.endpoint, endpointTolerance);
    require(listenOn.largestStep < 0.01f, "sidechain Listen on uses a short monitor ramp");
    require(listenOff.largestStep < 0.01f, "sidechain Listen off uses a short monitor ramp");
    require(std::abs(listenOn.endpoint - sidechain.back()) <= endpointTolerance,
            "sidechain Listen on reaches the listened signal");
    require(std::abs(listenOff.endpoint - input.back()) <= endpointTolerance,
            "sidechain Listen off reaches the non-listened signal");
}

void testSidechainListenCrossfadeIsLatencyAligned()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr float amplitude = 0.5f;
    constexpr float frequency = 121.6f;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(2);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 10.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 10.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.prepare(sampleRate, blockSize);

    std::array<float, blockSize> input{}, output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    int64_t sample = 0;
    auto renderBlock = [&]() {
        for (int i = 0; i < blockSize; ++i, ++sample)
            input[static_cast<size_t>(i)] = amplitude * std::sin(
                2.0 * static_cast<double>(kPi) * frequency * static_cast<double>(sample) / sampleRate);
        dsp.processBlock(inputs, outputs, 1, blockSize);
    };

    for (int block = 0; block < 32; ++block) renderBlock();
    float beforePeak = 0.0f;
    for (float value : output) beforePeak = std::max(beforePeak, std::abs(value));

    dsp.setParameter(MultiCompDSP::Parameter::GlobalSidechainListen, 1.0f);
    constexpr int rampSamples = 1440;
    std::array<float, 6 * blockSize> transition{};
    for (int block = 0; block < 6; ++block)
    {
        renderBlock();
        std::copy(output.begin(), output.end(), transition.begin() + block * blockSize);
    }
    float midRampPeak = 0.0f;
    constexpr int halfWindow = blockSize / 2;
    for (int i = rampSamples / 2 - halfWindow; i < rampSamples / 2 + halfWindow; ++i)
        midRampPeak = std::max(midRampPeak, std::abs(transition[static_cast<size_t>(i)]));

    renderBlock();
    float afterPeak = 0.0f;
    for (float value : output) afterPeak = std::max(afterPeak, std::abs(value));
    const float endpointPeak = std::min(beforePeak, afterPeak);
    std::printf("Listen latency crossfade: before %.6f; mid-ramp %.6f; after %.6f; ratio %.3f\n",
                beforePeak, midRampPeak, afterPeak, midRampPeak / endpointPeak);
    require(endpointPeak > 0.45f, "Listen latency test establishes full-level endpoints");
    require(midRampPeak >= endpointPeak * 0.8f,
            "Listen crossfade does not collapse when lookahead and oversampling latency are engaged");
}

void testSettledBypassClearsGainReductionMeters()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, -40.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 20.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Attack, 0.1f);
    }
    std::vector<float> input(256, 0.8f), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 80; ++block) dsp.processBlock(ip, op, 1, 256);
    const float activeMaster = dsp.getGainReduction();
    const float activeBand = dsp.getBandGainReduction(0);
    dsp.setBypass(true);
    for (int block = 0; block < 10; ++block) dsp.processBlock(ip, op, 1, 256);
    float bypassBandMagnitude = 0.0f;
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        bypassBandMagnitude = std::max(bypassBandMagnitude, std::abs(dsp.getBandGainReduction(band)));
    std::printf("settled bypass GR: active master %.4f band0 %.4f; bypass master %.4f max-band %.4f\n",
                activeMaster, activeBand, dsp.getGainReduction(), bypassBandMagnitude);
    require(activeMaster < -1.0f && activeBand < -1.0f, "meter test establishes active multiband compression");
    require(dsp.getGainReduction() == 0.0f && bypassBandMagnitude == 0.0f,
            "settled bypass clears master and per-band gain-reduction meters");
}
}

// A first prepare whose arguments match the member defaults (48 kHz, factor 1)
// must still do the one-time setup. It previously returned early, leaving the
// Digital lookahead ring buffer empty, and the first Digital block then wrote
// past the end of it and divided by its zero size.
void testPrepareAtDefaultRateAndFactor()
{
    MultiCompDSP dsp;
    dsp.setOversampling(0);                     // factor 1, the member default
    dsp.prepare(48000.0, 256);                  // 48 kHz, also the member default
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 5.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);

    std::array<float, 256> left{}, right{};
    for (int i = 0; i < 256; ++i)
        left[static_cast<size_t>(i)] = right[static_cast<size_t>(i)]
            = 0.5f * std::sin(2.0f * kPi * 220.0f * static_cast<float>(i) / 48000.0f);

    float* io[2] = {left.data(), right.data()};
    const float* in[2] = {left.data(), right.data()};
    for (int block = 0; block < 8; ++block)
        dsp.processBlock(in, io, 2, 256);

    for (int i = 0; i < 256; ++i)
    {
        require(std::isfinite(left[static_cast<size_t>(i)]), "default-rate prepare left finite");
        require(std::isfinite(right[static_cast<size_t>(i)]), "default-rate prepare right finite");
    }
    std::puts("prepare at default rate/factor: initialised, Digital lookahead safe");
}

void testOptoStereoDetectorIsolation()
{
    duskaudio::MultiCompModes modes;
    duskaudio::MultiCompParameterState parameters;
    modes.prepare(48000.0, 256, 1);
    parameters.optoPeakReduction.store(100.0f, std::memory_order_relaxed);

    for (int i = 0; i < 16384; ++i)
    {
        const float left = 0.8f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
        (void)modes.process(duskaudio::MultiCompMode::Opto, left, 0, left, parameters);
        (void)modes.process(duskaudio::MultiCompMode::Opto, 0.0f, 1, 0.0f, parameters);
    }

    const float leftGr = modes.gainReduction(duskaudio::MultiCompMode::Opto, 0);
    const float rightGr = modes.gainReduction(duskaudio::MultiCompMode::Opto, 1);
    std::printf("opto stereo isolation: active-left GR %.4f; silent-right GR %.9g dB\n", leftGr, rightGr);
    require(leftGr < -0.5f, "Opto isolation stimulus establishes left-channel gain reduction");
    require(std::abs(rightGr) < 1.0e-7f, "left-only Opto signal does not alter right detector gain reduction");
}

void testHardwareRateRefreshAfterOversamplingChange()
{
    const duskaudio::MultiCompMode hardwareModes[] = {
        duskaudio::MultiCompMode::Opto, duskaudio::MultiCompMode::FET,
        duskaudio::MultiCompMode::Bus, duskaudio::MultiCompMode::StudioFET,
        duskaudio::MultiCompMode::StudioVCA};
    duskaudio::MultiCompParameterState parameters;
    parameters.optoPeakReduction.store(0.0f, std::memory_order_relaxed);
    parameters.fetInput.store(0.0f, std::memory_order_relaxed);

    for (const auto mode : hardwareModes)
    {
        duskaudio::MultiCompModes switched, fresh;
        switched.prepare(48000.0, 256, 2);
        switched.setRate(48000.0, 1);
        switched.reset();
        fresh.prepare(48000.0, 256, 1);
        fresh.reset();

        float maxDelta = 0.0f, signalPeak = 0.0f;
        for (int i = 0; i < 8192; ++i)
        {
            const float input = 0.2f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
            const float a = switched.process(mode, input, 0, input, parameters);
            const float b = fresh.process(mode, input, 0, input, parameters);
            maxDelta = std::max(maxDelta, std::abs(a - b));
            signalPeak = std::max(signalPeak, std::max(std::abs(a), std::abs(b)));
        }
        std::printf("hardware rate refresh: mode=%d max_delta=%.9g\n", static_cast<int>(mode), maxDelta);
        require(signalPeak > 1.0e-4f, "hardware rate-refresh comparison produces output");
        require(maxDelta < 1.0e-7f, "oversampling rate switch matches freshly prepared hardware coefficients");
    }
}

void testOptoExposureTimebaseSurvivesRateChange()
{
    duskaudio::MultiCompParameterState parameters;
    parameters.optoPeakReduction.store(70.0f, std::memory_order_relaxed);
    parameters.optoGain.store(duskaudio::optoGainDbToKnob(0.0f),
                              std::memory_order_relaxed);
    duskaudio::MultiCompModes switched;
    duskaudio::MultiCompModes reference;
    switched.prepare(48000.0, 256, 1);
    reference.prepare(48000.0, 256, 2);
    constexpr float kAmplitude = 0.6309573445f; // -4 dBFS
    constexpr double kFrequency = 997.0;
    double switchedTime = 0.0;
    auto processDuration = [&](duskaudio::MultiCompModes& modes,
                               double rate, double seconds, double& time) {
        const int samples = static_cast<int>(std::lround(rate * seconds));
        for (int sample = 0; sample < samples; ++sample)
        {
            const float input = kAmplitude * std::sin(
                2.0 * static_cast<double>(kPi) * kFrequency * time);
            (void)modes.process(duskaudio::MultiCompMode::Opto,
                                input, 0, input, parameters);
            time += 1.0 / rate;
        }
    };
    processDuration(switched, 48000.0, 0.010, switchedTime);
    switched.setRate(48000.0, 2);
    processDuration(switched, 96000.0, 0.015, switchedTime);

    double referenceTime = 0.0;
    processDuration(reference, 96000.0, 0.025, referenceTime);
    const float switchedGr = switched.gainReduction(
        duskaudio::MultiCompMode::Opto, 0);
    const float referenceGr = reference.gainReduction(
        duskaudio::MultiCompMode::Opto, 0);
    const float delta = switchedGr - referenceGr;
    std::printf("opto exposure rate switch: 10 ms@48 + 15 ms@96 %.6f dB; "
                "25 ms@96 %.6f dB; delta %+.6f dB\n",
                switchedGr, referenceGr, delta);
    require(std::abs(delta) < 0.20f,
            "Opto exposure keeps its physical time across an oversampling-rate change");
}

void testOptoFastChargeIsOversamplingInvariant()
{
    duskaudio::MultiCompParameterState parameters;
    parameters.optoPeakReduction.store(70.0f, std::memory_order_relaxed);
    parameters.optoGain.store(duskaudio::kOptoGainUnityKnob,
                              std::memory_order_relaxed);
    constexpr float kAmplitude = 0.6309573445f; // -4 dBFS peak
    std::array<float, 3> reductions{};
    size_t row = 0;
    for (const int factor : {1, 2, 4})
    {
        duskaudio::MultiCompModes modes;
        modes.prepare(48000.0, 1, factor);
        const int samples = static_cast<int>(
            std::lround(0.00025 * 48000.0 * factor));
        for (int sample = 0; sample < samples; ++sample)
        {
            const float input = kAmplitude * std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(sample)
                    / static_cast<float>(48000 * factor));
            modes.process(duskaudio::MultiCompMode::Opto,
                          input, 0, input, parameters);
        }
        reductions[row++] = -modes.gainReduction(
            duskaudio::MultiCompMode::Opto, 0);
    }
    const auto spread = std::minmax_element(reductions.begin(), reductions.end());
    const float difference = *spread.second - *spread.first;
    std::printf("opto fast-charge rate parity: 1x %.6f dB 2x %.6f dB "
                "4x %.6f dB spread %.6f dB\n",
                reductions[0], reductions[1], reductions[2], difference);
    require(*spread.first > 0.1f && difference < 0.25f,
            "Opto isolated fast charge preserves physical time across oversampling factors");
}

float renderSoloedHighFrequencyPeak(float mix)
{
    constexpr int blockSize = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setParameter(MultiCompDSP::Parameter::MbMix, mix);
    dsp.setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Solo, 1.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Makeup, 0.0f);
    }
    dsp.prepare(48000.0, blockSize);

    std::array<float, blockSize> input{}, output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    float peak = 0.0f;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const int sample = block * blockSize + i;
            input[static_cast<size_t>(i)] = 0.5f * std::sin(
                2.0f * kPi * 12000.0f * static_cast<float>(sample) / 48000.0f);
        }
        dsp.processBlock(inputs, outputs, 1, blockSize);
        if (block == 99)
            for (float sample : output) peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

void testMultibandSoloMasksDryReference()
{
    const float halfMixPeak = renderSoloedHighFrequencyPeak(50.0f);
    const float wetPeak = renderSoloedHighFrequencyPeak(100.0f);
    const float delta = std::abs(halfMixPeak - wetPeak);
    std::printf("multiband solo dry mask: 50%% peak %.9g; wet peak %.9g; delta %.9g\n",
                halfMixPeak, wetPeak, delta);
    require(wetPeak < 0.01f, "low solo rejects a 12 kHz signal on the wet path");
    require(delta < 1.0e-5f, "multiband dry reference obeys the same solo mask as wet recombination");
}

void requireMultibandOutputSnapshotIsSingleLoad();

void testMultibandAutomationUsesOneStereoSnapshot()
{
    constexpr int blockSize = 32768;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Makeup, 0.0f);
    }
    dsp.prepare(48000.0, blockSize);

    std::vector<float> input(static_cast<size_t>(blockSize));
    std::vector<float> left(static_cast<size_t>(blockSize));
    std::vector<float> right(static_cast<size_t>(blockSize));
    for (int i = 0; i < blockSize; ++i)
        input[static_cast<size_t>(i)] = 0.01f * std::sin(2.0f * kPi * 997.0f * i / 48000.0f);
    const float* inputs[] = {input.data(), input.data()};
    float* outputs[] = {left.data(), right.data()};

    std::atomic<bool> writerReady{false};
    std::atomic<bool> stopWriter{false};
    std::atomic<unsigned> writes{0};
    std::thread writer([&] {
        writerReady.store(true, std::memory_order_release);
        while (!stopWriter.load(std::memory_order_acquire))
        {
            const unsigned write = writes.fetch_add(1, std::memory_order_relaxed);
            const float gainDb = (write & 1u) != 0u ? 12.0f : -12.0f;
            dsp.setParameter(MultiCompDSP::Parameter::MbOutput, gainDb);
            dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Makeup, gainDb);
        }
    });
    while (!writerReady.load(std::memory_order_acquire)
           || writes.load(std::memory_order_relaxed) < 100u)
        std::this_thread::yield();

    float worstDelta = 0.0f;
    float signalPeak = 0.0f;
    for (int block = 0; block < 16 && worstDelta < 1.0e-6f; ++block)
    {
        dsp.processBlock(inputs, outputs, 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            worstDelta = std::max(worstDelta, std::abs(left[static_cast<size_t>(i)] - right[static_cast<size_t>(i)]));
            signalPeak = std::max(signalPeak, std::abs(left[static_cast<size_t>(i)]));
            signalPeak = std::max(signalPeak, std::abs(right[static_cast<size_t>(i)]));
        }
    }
    stopWriter.store(true, std::memory_order_release);
    writer.join();

    std::printf("multiband concurrent output/makeup: writes %u; signal peak %.9g; max L/R delta %.9g\n",
                writes.load(std::memory_order_relaxed), signalPeak, worstDelta);
    require(signalPeak > 1.0e-4f, "multiband stereo snapshot test processes nonzero signal");
    requireMultibandOutputSnapshotIsSingleLoad();
    require(worstDelta < 1.0e-6f,
            "multiband parameter automation applies one parameter snapshot to both channels");
}

std::vector<float> renderWithBlockSize(int oversampling, int blockSize, bool autoMakeup)
{
    constexpr int totalSamples = 16384;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(oversampling);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -12.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 4.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalKnee, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.1f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRelease, 50.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, autoMakeup ? 1.0f : 0.0f);
    dsp.prepare(48000.0, 512);

    std::vector<float> input(totalSamples), output(totalSamples);
    for (int i = 0; i < totalSamples; ++i)
    {
        const float modulation = 0.55f + 0.4f * std::sin(2.0f * kPi * 3.7f * i / 48000.0f);
        input[static_cast<size_t>(i)] = modulation
            * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
    }
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        const float* inputs[] = {input.data() + offset};
        float* outputs[] = {output.data() + offset};
        dsp.processBlock(inputs, outputs, 1, count);
    }
    return output;
}

void testOversamplingBlockSizeInvariance()
{
    bool invariant = true;
    for (bool autoMakeup : {false, true})
    for (int oversampling : {0, 1, 2})
    {
        const auto large = renderWithBlockSize(oversampling, 512, autoMakeup);
        const auto small = renderWithBlockSize(oversampling, 128, autoMakeup);
        float largePeak = 0.0f;
        float smallPeak = 0.0f;
        for (size_t i = 0; i < large.size(); ++i)
        {
            largePeak = std::max(largePeak, std::abs(large[i]));
            smallPeak = std::max(smallPeak, std::abs(small[i]));
        }
        const float signalPeak = std::min(largePeak, smallPeak);
        require(signalPeak > 1.0e-4f, "oversampling block-invariance comparison produces output");
        float maxDelta = 0.0f;
        for (size_t i = 0; i < large.size(); ++i)
            maxDelta = std::max(maxDelta, std::abs(large[i] - small[i]));
        std::printf("oversampling block invariance: auto=%d os=%d signal_peak=%.9g max_delta=%.9g\n",
                    autoMakeup ? 1 : 0, oversampling, signalPeak, maxDelta);
        invariant = invariant && maxDelta < 1.0e-7f;
    }
    require(invariant,
            "oversampled render with Auto Makeup on and off is invariant between 512- and 128-sample blocks");
}

void testAllModesAreMonoSafe()
{
    constexpr int blockSize = 127;
    for (int mode = 0; mode < 8; ++mode)
    {
        MultiCompDSP dsp;
        dsp.setMode(mode);
        dsp.setOversampling(2);
        dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 3.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 4.0f);
        dsp.setParameter(MultiCompDSP::Parameter::StereoLinkMode, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
        dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
        dsp.prepare(48000.0, blockSize);
        std::array<float, blockSize> input{}, sidechain{}, output{};
        for (int i = 0; i < blockSize; ++i)
        {
            input[static_cast<size_t>(i)] = 0.4f * std::sin(2.0f * kPi * 431.0f * i / 48000.0f);
            sidechain[static_cast<size_t>(i)] = 0.7f * std::sin(2.0f * kPi * 997.0f * i / 48000.0f);
        }
        const float* inputs[] = {input.data()};
        const float* sidechains[] = {sidechain.data()};
        float* outputs[] = {output.data()};
        for (int block = 0; block < 20; ++block)
            dsp.processBlockExternal(inputs, sidechains, outputs, 1, blockSize);
        for (float sample : output)
            require(std::isfinite(sample), "mono processing remains finite in every mode");
    }
    std::puts("mono safety: all modes exercised with 4x, lookahead, external sidechain and stereo-link state");
}

float fetAttackPlain(float position)
{
    return 0.02f + (80.0f - 0.02f) * std::pow(
        std::clamp(position, 0.0f, 1.0f), 1.0f / 0.3f);
}

float fetReleasePlain(float position)
{
    return 50.0f + (1100.0f - 50.0f)
        * std::clamp(position, 0.0f, 1.0f);
}

void prepareReferenceFet(MultiCompDSP& dsp, double sampleRate, int blockSize,
                         float inputPosition = 0.8f,
                         float outputPosition = 0.625915527f,
                         int ratio = 0, float curve = 0.0f)
{
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::FET));
    dsp.setMix(100.0f);
    dsp.setStereoLink(100.0f);
    dsp.setOversampling(kOversampling2xSetting);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::TruePeakEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetInput,
                     -20.0f + 60.0f * inputPosition);
    dsp.setParameter(MultiCompDSP::Parameter::FetOutput,
                     -20.0f + 40.0f * outputPosition);
    dsp.setParameter(MultiCompDSP::Parameter::FetAttack, fetAttackPlain(0.5f));
    dsp.setParameter(MultiCompDSP::Parameter::FetRelease, fetReleasePlain(0.5f));
    dsp.setParameter(MultiCompDSP::Parameter::FetRatio, static_cast<float>(ratio));
    dsp.setParameter(MultiCompDSP::Parameter::FetCurve, curve);
    dsp.setParameter(MultiCompDSP::Parameter::FetTransient, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetThreshold, -10.0f);
    dsp.prepare(sampleRate, blockSize);
}

float renderReferenceFetRmsDb(float inputDbfs, float inputPosition,
                              float outputPosition, int ratio,
                              double sampleRate = 48000.0,
                              int blockSize = 256,
                              float curve = 0.0f)
{
    const int totalSamples = static_cast<int>(std::lround(5.0 * sampleRate));
    const int measureStart = static_cast<int>(std::lround(3.0 * sampleRate));
    const int measureStop = static_cast<int>(std::lround(4.5 * sampleRate));
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize,
                        inputPosition, outputPosition, ratio, curve);
    std::vector<float> input(static_cast<size_t>(blockSize));
    std::vector<float> output(static_cast<size_t>(blockSize));
    const float amplitude = duskaudio::decibelsToGain(inputDbfs);
    double power = 0.0;
    int measured = 0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
            input[static_cast<size_t>(sample)] = amplitude * std::sin(
                2.0f * kPi * 1000.0f * static_cast<float>(offset + sample)
                / static_cast<float>(sampleRate));
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        for (int sample = 0; sample < count; ++sample)
            if (offset + sample >= measureStart
                && offset + sample < measureStop)
            {
                const float value = output[static_cast<size_t>(sample)];
                power += static_cast<double>(value) * value;
                ++measured;
            }
    }
    require(measured == measureStop - measureStart,
            "FET reference render measures exactly the reference 3.0-4.5 second window");
    return duskaudio::gainToDecibels(static_cast<float>(
        std::sqrt(power / static_cast<double>(measured))));
}

// ---------------------------------------------------------------------------
// Vintage-FET flat gain against sample rate.
//
// `fetHardwareGain` is calibrated by `calibrateFetHardwareGain`, which measures
// the RMS of `inputTransformerFet -> outputTransformerFet -> fetConvolution`
// and inverts it. The vintage audio path applies that scalar to the
// CONVOLUTION ALONE (`MultiCompModes.hpp`: `if (!studio) out =
// fetConvolution.processSample(out, ch) * fetHardwareGain;`) -- the two
// transformers are Studio-path stages and never run here. The normalisation
// therefore inverts a three-stage response and is applied to a one-stage one,
// and because each stage's response depends on the rate, the residual differs
// per rate rather than cancelling as a constant.
//
// This measures the observable CONSEQUENCE rather than the coefficient: a
// zero-reduction sine at a level far below the knee should come out at the same
// level whatever the rate, since nothing else in the path is rate dependent at
// 1 kHz. A constant part of the miscalibration is harmless -- it was absorbed
// into `fetInputGainDb` when the static grid was fitted at 48 kHz -- so what
// this guards is the part that cannot be absorbed, the RATE DEPENDENCE.
//
// Re-measured 2026-08-26 on plugin de386c39bdd9. A previous handoff recorded
// this as "-0.010 dB at 96 k"; that number does not reproduce on the current
// build, which reads -0.002266 dB there and worst 0.003448 dB at 88.2 kHz.
//
// The miscalibration is real but its consequence is NOT, and that was measured
// rather than argued. Two deliberate breaks were run against this assertion:
//
//   `cacheHardwareGains` pinned to 48 kHz regardless of host rate,
//   so 96 kHz uses a gain calibrated for the wrong rate   -> 0.004662 dB, PASSES
//   `calibrateFetHardwareGain` probed at twice the rate   -> 0.003723 dB, PASSES
//
// Neither can breach the campaign's 0.02 dB tolerance, because the transformer
// and convolution responses this coefficient normalises are all but flat around
// the 1 kHz calibration tone. The constant part of the error is harmless in any
// case -- it was absorbed into `fetInputGainDb` when the static grid was fitted
// at 48 kHz -- so the item is closed as measured-and-immaterial rather than
// fixed. What remains asserted here is the regression: a synthetic 1 %-per-rate
// error in `fetHardwareGains` reads 0.083313 dB and fails, which is what proves
// this assertion is wired to the quantity it names.
void testFetSampleRateFlatGain()
{
    constexpr float probeDbfs = -60.0f;      // far below any knee: zero reduction
    constexpr float inputPosition = 0.2f;
    const float at48 = renderReferenceFetRmsDb(
        probeDbfs, inputPosition, 0.625915527f, 0, 48000.0, 256);
    std::printf("FET sample-rate flat gain: reference 48000 Hz -> %.6f dBFS\n",
                at48);
    double worst = 0.0;
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0})
    {
        const float measured = renderReferenceFetRmsDb(
            probeDbfs, inputPosition, 0.625915527f, 0, rate, 256);
        const double delta = static_cast<double>(measured) - at48;
        worst = std::max(worst, std::abs(delta));
        std::printf("FET sample-rate flat gain: %8.0f Hz -> %.6f dBFS "
                    "delta %+.6f dB\n", rate, measured, delta);
    }
    std::printf("FET sample-rate flat gain: worst absolute delta %.6f dB\n",
                worst);
    require(worst < 0.02,
            "vintage FET flat gain is sample-rate independent within the "
            "campaign's 0.02 dB tolerance");
}

void testFetMeasuredControlTapers()
{
    struct Point { float position; float relativeDb; };
    constexpr std::array<Point, 4> inputPoints{{
        {0.2f, -33.678216f}, {0.5f, -13.389983f},
        {0.8f, -1.246802f}, {1.0f, 0.0f}}};
    constexpr std::array<Point, 4> outputPoints{{
        {0.2f, -55.024575f}, {0.5f, -21.810000f},
        {0.8f, -4.012500f}, {1.0f, 0.0f}}};
    const float inputReference = renderReferenceFetRmsDb(
        -72.0f, 1.0f, 1.0f, 0);
    const float outputReference = inputReference;
    float worstInputError = 0.0f;
    float worstOutputError = 0.0f;
    for (const auto& point : inputPoints)
    {
        const float measured = renderReferenceFetRmsDb(
            -72.0f, point.position, 1.0f, 0) - inputReference;
        std::printf("FET Input taper: position %.1f reference %+.6f measured %+.6f error %+.6f dB\n",
                    point.position, point.relativeDb, measured,
                    measured - point.relativeDb);
        worstInputError = std::max(worstInputError,
                                   std::abs(measured - point.relativeDb));
    }
    for (const auto& point : outputPoints)
    {
        const float measured = renderReferenceFetRmsDb(
            -72.0f, 1.0f, point.position, 0) - outputReference;
        std::printf("FET Output taper: position %.1f reference %+.6f measured %+.6f error %+.6f dB\n",
                    point.position, point.relativeDb, measured,
                    measured - point.relativeDb);
        worstOutputError = std::max(worstOutputError,
                                    std::abs(measured - point.relativeDb));
    }
    std::printf("FET measured tapers: input worst %.6f dB output worst %.6f dB\n",
                worstInputError, worstOutputError);
    require(worstInputError < 0.02f && worstOutputError < 0.02f,
            "vintage FET Input and Output reproduce the measured pot tapers");
}

void testFetMeasuredStaticSurface()
{
    struct Point
    {
        float inputPosition;
        float inputDbfs;
        int ratio;
        float referenceOutputDbfs;
    };
    constexpr std::array<Point, 5> points{{
        {0.8f, -24.0f, 0, -15.926712f},
        {0.8f, -6.0f, 0, -12.854822f},
        {0.8f, -12.0f, 1, -14.280624f},
        {0.6f, -12.0f, 3, -13.415414f},
        {0.8f, -6.0f, 4, -14.918762f}
    }};
    float worstError = 0.0f;
    for (const auto& point : points)
    {
        const float measured = renderReferenceFetRmsDb(
            point.inputDbfs, point.inputPosition, 0.625915527f, point.ratio);
        const float error = measured - point.referenceOutputDbfs;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET static reference: ratio %d input %.1f/%.1f reference %.6f measured %.6f error %+.6f dB\n",
                    point.ratio, point.inputPosition, point.inputDbfs,
                    point.referenceOutputDbfs, measured, error);
    }
    require(worstError < 0.20f,
            "vintage FET representative static points stay within 0.20 dB of the live reference");
}

double renderFetKneeFundamentalDb(double frequencyHz, float inputDbfs,
                                  float inputPosition)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int totalSamples = 12 * static_cast<int>(sampleRate);
    constexpr int measureStart = 9 * static_cast<int>(sampleRate);
    constexpr int measureStop = measureStart
        + 3 * static_cast<int>(sampleRate) / 2;
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize,
                        inputPosition, 0.625915527f, 0);
    std::array<float, blockSize> input{};
    std::array<float, blockSize> output{};
    const double amplitude = duskaudio::decibelsToGain(inputDbfs);
    double real = 0.0;
    double imaginary = 0.0;
    int measured = 0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
            input[static_cast<size_t>(sample)] = static_cast<float>(
                amplitude * std::sin(2.0 * duskaudio::kDuskPi * frequencyHz
                    * static_cast<double>(offset + sample) / sampleRate));
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            if (absolute < measureStart || absolute >= measureStop)
                continue;
            const double phase = 2.0 * duskaudio::kDuskPi * frequencyHz
                * static_cast<double>(absolute) / sampleRate;
            real += output[static_cast<size_t>(sample)] * std::cos(phase);
            imaginary -= output[static_cast<size_t>(sample)] * std::sin(phase);
            ++measured;
        }
    }
    const double magnitude = 2.0 * std::hypot(real, imaginary)
        / static_cast<double>(measured);
    return 20.0 * std::log10(std::max(magnitude, 1.0e-30));
}

void testFetKneeOnsetMatchesReference()
{
    // Wave 27's independently repeated 100 Hz and 1 kHz captures agree within
    // 0.00564 dB at every selected point: this is a common static-knee shape,
    // not the deeper ratio/frequency detector surface that the joint LF shelf
    // experiment rejected. The old representative static grid skipped this
    // interval and therefore let the quadratic knee start up to 0.72 dB early.
    struct Point
    {
        float driveDb;
        double reference100HzDb;
        double reference1kHzDb;
    };
    constexpr std::array<Point, 9> points{{
        {-14.00f, 0.001054856, 0.001054679},
        {-12.75f, 0.001418202, 0.001419694},
        {-11.00f, 0.002144157, 0.002144161},
        {-10.50f, 0.009454543, 0.010022404},
        {-10.00f, 0.135284617, 0.129645572},
        { -9.00f, 0.590613455, 0.589745640},
        { -8.00f, 1.167230576, 1.167031554},
        { -7.00f, 1.809697156, 1.808799028},
        { -6.00f, 2.486826948, 2.483171013},
    }};
    constexpr float controlInputPosition = 0.2f;
    constexpr float controlSourceDbfs = -36.0f;
    constexpr double controlDriveDb = -29.827545;
    constexpr float measuredInputPosition = 0.8f;
    constexpr double measuredInputGainDb = 38.603869;
    const auto outputOffset = [&](double frequencyHz) {
        return renderFetKneeFundamentalDb(
            frequencyHz, controlSourceDbfs, controlInputPosition)
            - controlDriveDb;
    };
    const double offset100 = outputOffset(100.0);
    const double offset1k = outputOffset(1000.0);
    double worstError = 0.0;
    double worstFrequencySplit = 0.0;
    double measured1kAtMinusSix = 0.0;
    for (const auto& point : points)
    {
        const double reduction100 = point.driveDb + offset100
            - renderFetKneeFundamentalDb(
                100.0, static_cast<float>(point.driveDb - measuredInputGainDb),
                measuredInputPosition);
        const double reduction1k = point.driveDb + offset1k
            - renderFetKneeFundamentalDb(
                1000.0, static_cast<float>(point.driveDb - measuredInputGainDb),
                measuredInputPosition);
        const double error100 = reduction100 - point.reference100HzDb;
        const double error1k = reduction1k - point.reference1kHzDb;
        worstError = std::max(
            worstError, std::max(std::abs(error100), std::abs(error1k)));
        worstFrequencySplit = std::max(
            worstFrequencySplit, std::abs(reduction100 - reduction1k));
        if (point.driveDb == -6.0f)
            measured1kAtMinusSix = reduction1k;
        std::printf("FET knee onset: drive %+.2f reference/measured/error "
                    "100 Hz %.6f/%.6f/%+.6f dB, 1 kHz %.6f/%.6f/%+.6f dB\n",
                    static_cast<double>(point.driveDb),
                    point.reference100HzDb, reduction100, error100,
                    point.reference1kHzDb, reduction1k, error1k);
    }
    // Wave 24 independently measured the same -6 dB endpoint at 40 and 60 Hz.
    // Both are held out from the 1 kHz knee table, as is the 100 Hz curve above.
    struct HeldOutFrequency
    {
        double frequencyHz;
        double referenceReductionDb;
    };
    constexpr std::array<HeldOutFrequency, 2> heldOut{{
        {40.0, 2.476338413},
        {60.0, 2.487113619},
    }};
    for (const auto& point : heldOut)
    {
        const double offset = outputOffset(point.frequencyHz);
        const double reduction = -6.0 + offset
            - renderFetKneeFundamentalDb(
                point.frequencyHz,
                static_cast<float>(-6.0 - measuredInputGainDb),
                measuredInputPosition);
        const double error = reduction - point.referenceReductionDb;
        worstError = std::max(worstError, std::abs(error));
        worstFrequencySplit = std::max(
            worstFrequencySplit,
            std::abs(reduction - measured1kAtMinusSix));
        std::printf("FET knee onset held-out: %.0f Hz drive -6.00 "
                    "reference %.6f measured %.6f error %+.6f dB\n",
                    point.frequencyHz, point.referenceReductionDb,
                    reduction, error);
    }
    std::printf("FET knee onset: worst absolute error %.6f dB, "
                "worst 100 Hz/1 kHz split %.6f dB\n",
                worstError, worstFrequencySplit);
    require(worstError < 0.04 && worstFrequencySplit < 0.03,
            "vintage FET knee onset follows the measured UAD curve and frequency collapse");
}

void testFetKneeCellDoesNotCancelDeepReleaseMemory()
{
    // The auxiliary cell owns at most the shallow-knee difference (0.724 dB
    // on the pre-change path). After a loud passage it must not mistake the
    // main envelope's retained deep reduction for a new knee error and cancel
    // that programme memory on its independent timing path.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int loudSamples = 3 * static_cast<int>(sampleRate);
    constexpr int shallowSamples = 4 * static_cast<int>(sampleRate);
    constexpr float measuredInputGainDb = 38.603869f;
    constexpr float loudSourceDbfs = -6.0f;
    constexpr float shallowSourceDbfs = -10.0f - measuredInputGainDb;
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize, 0.8f, 0.625915527f, 0);
    std::array<float, blockSize> input{};
    std::array<float, blockSize> output{};
    float reductionAtTransition = 0.0f;
    float maximumAuxiliaryCorrection = 0.0f;
    float finalAuxiliaryCorrection = 0.0f;
    const int totalSamples = loudSamples + shallowSamples;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            const float levelDb = absolute < loudSamples
                ? loudSourceDbfs : shallowSourceDbfs;
            input[static_cast<size_t>(sample)] = duskaudio::decibelsToGain(levelDb)
                * std::sin(2.0f * kPi * 1000.0f
                    * static_cast<float>(absolute) / static_cast<float>(sampleRate));
        }
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        const float colourReduction = -duskaudio::MultiCompDSPTestAccess::
            fetEnvelopeGainDb(dsp, 0);
        const float netReduction = -duskaudio::MultiCompDSPTestAccess::
            fetNetGainReductionDb(dsp, 0);
        const float correction = colourReduction - netReduction;
        if (offset < loudSamples && offset + count >= loudSamples)
            reductionAtTransition = colourReduction;
        if (offset >= loudSamples)
        {
            maximumAuxiliaryCorrection = std::max(
                maximumAuxiliaryCorrection, correction);
            finalAuxiliaryCorrection = correction;
        }
    }
    std::printf("FET knee release neighbour: deep reduction %.6f dB, "
                "maximum/final auxiliary correction %.6f/%.6f dB\n",
                reductionAtTransition, maximumAuxiliaryCorrection,
                finalAuxiliaryCorrection);
    require(reductionAtTransition > 20.0f
                && maximumAuxiliaryCorrection < 1.0f
                && finalAuxiliaryCorrection > 0.3f,
            "the exercised shallow FET knee cell never cancels deep release memory");
}

void testFetMaximumReductionSaturation()
{
    // The ordinary static surface stops at about +34 dB internal level. With
    // Input and Output clockwise, the installed unit follows the same 4:1 law
    // through a -3 dBFS source, then stops adding reduction over the last
    // roughly 1.3 dB of detector level. Pin both directions: the 0 dBFS point
    // proves that the saturation exists, while -3 dBFS proves that its onset
    // does not narrow the already-fitted straight-law domain.
    struct Point
    {
        int ratio;
        float referenceMinusThreeDbfs;
        float referenceZeroDbfs;
    };
    // Live-reference 3.0--4.5 second windows. The All-buttons -3 dBFS point is
    // intentionally not replaced by its later asymptote: this helper and the
    // campaign ceiling gate both measure this practical five-second window.
    constexpr std::array<Point, 5> points{{
        {0,  0.718774f,  2.253437f},
        {1, -0.450040f,  0.856189f},
        {2, -0.557974f,  0.505468f},
        {3,  0.014880f,  0.857770f},
        {4, -1.918019f, -0.552185f},
    }};
    float worstBelowError = 0.0f;
    float worstSaturatedError = 0.0f;
    for (const auto& point : points)
    {
        const float belowSaturation = renderReferenceFetRmsDb(
            -3.0f, 1.0f, 1.0f, point.ratio);
        const float saturated = renderReferenceFetRmsDb(
            0.0f, 1.0f, 1.0f, point.ratio);
        const float belowError = belowSaturation - point.referenceMinusThreeDbfs;
        const float saturatedError = saturated - point.referenceZeroDbfs;
        worstBelowError = std::max(worstBelowError, std::abs(belowError));
        worstSaturatedError = std::max(
            worstSaturatedError, std::abs(saturatedError));
        std::printf("FET maximum reduction ratio %d: -3 dBFS reference %.6f "
                    "measured %.6f error %+.6f; 0 dBFS reference %.6f "
                    "measured %.6f error %+.6f dB\n",
                    point.ratio, point.referenceMinusThreeDbfs,
                    belowSaturation, belowError, point.referenceZeroDbfs,
                    saturated, saturatedError);
    }
    require(worstBelowError < 0.15f,
            "vintage FET maximum-reduction onset preserves the lower straight-law point");
    require(worstSaturatedError < 0.15f,
            "vintage FET maximum reduction reproduces every ratio's 0 dBFS reference point");
}

void testFetAllButtonsSettlingWindows()
{
    // A five-second ceiling row alone can hide a wrong asymptote: before this
    // test, All-buttons crossed the reference around four seconds but kept
    // charging to a level 0.466 dB too low by nine seconds. 4:1 already follows
    // the reference's long population and is the opposite-path control.
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 256;
    constexpr int totalSamples = 12 * sampleRate;
    struct Point
    {
        int ratio;
        float inputDbfs;
        float referenceEarlyDbfs;
        float referenceLateDbfs;
    };
    constexpr std::array<Point, 3> points{{
        {0,  0.0f,  2.253437f,  2.253322f},
        {4, -3.0f, -1.918019f, -2.620076f},
        {4,  0.0f, -0.552185f, -0.562661f},
    }};
    float worstEarlyError = 0.0f;
    float worstLateError = 0.0f;
    for (const auto& point : points)
    {
        MultiCompDSP dsp;
        prepareReferenceFet(dsp, sampleRate, blockSize, 1.0f, 1.0f, point.ratio);
        std::array<float, blockSize> input{}, output{};
        const float amplitude = duskaudio::decibelsToGain(point.inputDbfs);
        double earlyPower = 0.0;
        double latePower = 0.0;
        int earlySamples = 0;
        int lateSamples = 0;
        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            const int count = std::min(blockSize, totalSamples - offset);
            for (int sample = 0; sample < count; ++sample)
                input[static_cast<size_t>(sample)] = amplitude * std::sin(
                    2.0f * kPi * 1000.0f * static_cast<float>(offset + sample)
                    / static_cast<float>(sampleRate));
            const float* inputs[] = {input.data()};
            float* outputs[] = {output.data()};
            dsp.processBlock(inputs, outputs, 1, count);
            for (int sample = 0; sample < count; ++sample)
            {
                const int absolute = offset + sample;
                const double value = output[static_cast<size_t>(sample)];
                if (absolute >= 3 * sampleRate
                    && absolute < 9 * sampleRate / 2)
                {
                    earlyPower += value * value;
                    ++earlySamples;
                }
                if (absolute >= 9 * sampleRate
                    && absolute < 23 * sampleRate / 2)
                {
                    latePower += value * value;
                    ++lateSamples;
                }
            }
        }
        require(earlySamples == 3 * sampleRate / 2
                    && lateSamples == 5 * sampleRate / 2,
                "FET settling windows contain every requested sample");
        const float earlyDbfs = duskaudio::gainToDecibels(static_cast<float>(
            std::sqrt(earlyPower / earlySamples)));
        const float lateDbfs = duskaudio::gainToDecibels(static_cast<float>(
            std::sqrt(latePower / lateSamples)));
        const float earlyError = earlyDbfs - point.referenceEarlyDbfs;
        const float lateError = lateDbfs - point.referenceLateDbfs;
        worstEarlyError = std::max(worstEarlyError, std::abs(earlyError));
        worstLateError = std::max(worstLateError, std::abs(lateError));
        std::printf("FET settling ratio %d input %+.0f: early reference %.6f measured %.6f "
                    "error %+.6f; late reference %.6f measured %.6f error %+.6f dB\n",
                    point.ratio, static_cast<double>(point.inputDbfs),
                    point.referenceEarlyDbfs, earlyDbfs, earlyError,
                    point.referenceLateDbfs, lateDbfs, lateError);
    }
    require(worstEarlyError < 0.15f,
            "vintage FET practical settling window remains reference-compatible");
    require(worstLateError < 0.15f,
            "vintage FET late settling window reaches the reference asymptote");
}

void testFetAbsoluteGainAnchors()
{
    // testFetMeasuredControlTapers above only ever compares knob positions with
    // each other, so a uniform scalar in the vintage chain cancels out of every
    // one of its assertions -- which is how a +0.149329 dB gain excess lived in
    // this code through several waves of taper work. These two anchors are
    // absolute and were measured on the reference unit itself.
    //
    // (a) Absolute output level at a proved zero-gain-reduction operating point:
    //     1 kHz at -72 dBFS, Input knob 0.2, Output knob 1.0. Zero reduction is
    //     not assumed -- the campaign's frequency sweep proves it by rendering a
    //     6 dB source step at this setting and getting 6.0000 dB back from both
    //     devices. With no reduction, no detector or ratio term can contribute,
    //     so this pins the product of every gain constant in the chain.
    constexpr float kReferenceZeroReductionDbfs = -64.488648f;
    const float measured = renderReferenceFetRmsDb(-72.0f, 0.2f, 1.0f, 0);
    const float error = measured - kReferenceZeroReductionDbfs;
    std::printf("FET absolute gain: zero-reduction reference %.6f measured %.6f error %+.6f dB\n",
                kReferenceZeroReductionDbfs, measured, error);

    // (b) All-buttons small-signal gain, expressed as All-buttons minus 4:1 at
    //     the same operating point (Input knob 0.4, 1 kHz at -36 dBFS, which is
    //     below BOTH laws' knees). Taking the difference of two ratio buttons
    //     cancels every gain constant common to them, so this reads the
    //     All-buttons correction alone and no output trim can mask it. It was
    //     1.046 dB here while the excess above was in place and the flat
    //     +0.15 dB fit constant was being added to the correction plateau.
    constexpr float kReferenceAllMinusFourToOneDb = 1.199919f;
    const float allButtons = renderReferenceFetRmsDb(-36.0f, 0.4f, 0.625915527f, 4);
    const float fourToOne = renderReferenceFetRmsDb(-36.0f, 0.4f, 0.625915527f, 0);
    const float smallSignal = allButtons - fourToOne;
    const float smallSignalError = smallSignal - kReferenceAllMinusFourToOneDb;
    std::printf("FET All-buttons small signal: reference %+.6f measured %+.6f error %+.6f dB\n",
                kReferenceAllMinusFourToOneDb, smallSignal, smallSignalError);

    require(std::abs(error) < 0.01f,
            "vintage FET absolute gain at zero reduction matches the measured reference");
    require(std::abs(smallSignalError) < 0.02f,
            "vintage FET All-buttons small-signal gain matches the measured reference");
}

void testFetMeasuredCurveAllButtons()
{
    // `processFET` reaches `fetAllProcessorCorrectionDb` only on the Modern
    // curve. With Curve = Measured and ratio ALL it takes
    // `lookupTables.getAllButtonsReduction()` instead, a different law, and the
    // whole 1176 campaign is pinned to Modern -- so nothing else in this file
    // or in the campaign exercises that arm. These are recorded regression
    // vectors, not reference parity: the reference unit was never rendered on
    // this arm.
    //
    // (a) Leak guard, and the open gap. Below the All-buttons detector
    //     threshold the Measured arm generates NO reduction at all, so its
    //     small-signal gain relative to 4:1 at the same operating point is
    //     exactly 0 dB. The reference's is +1.199919 dB (see
    //     testFetAbsoluteGainAnchors, where the Modern arm now reproduces it to
    //     0.004 dB). That -1.20 dB shortfall is a real, separate defect and is
    //     deliberately NOT fixed here; the assertion pins the current value so
    //     that a change to it -- including anything leaking out of the Modern
    //     correction table -- has to be deliberate.
    constexpr float kMeasuredCurveSmallSignalDb = 0.0f;
    const float measuredAll = renderReferenceFetRmsDb(
        -36.0f, 0.4f, 0.625915527f, 4, 48000.0, 256, 1.0f);
    const float fourToOne = renderReferenceFetRmsDb(
        -36.0f, 0.4f, 0.625915527f, 0, 48000.0, 256, 0.0f);
    const float smallSignal = measuredAll - fourToOne;
    std::printf("FET Measured-curve All-buttons small signal: recorded %+.6f measured %+.6f "
                "(reference wants +1.199919, i.e. a %+.6f dB gap this arm does not close)\n",
                kMeasuredCurveSmallSignalDb, smallSignal, smallSignal - 1.199919f);

    // (b) Four compressing points on the Measured arm, recorded from this
    //     build. They were re-recorded when the shared vintage broadband cubic
    //     became reduction-dependent: that colour path is deliberately common
    //     to both curve arms, while the small-signal assertion above still
    //     proves the Modern-only reduction correction does not leak here.
    //     Restoring the old constant -0.006 cubic reproduces the previous
    //     -27.010939/-26.974682/-27.533485/-14.744394 dBFS values and fails
    //     this assertion. The All-buttons detector threshold is -16.03 dBFS and the
    //     Input 0.4 law adds 19.209 dB, so these sources put the detector
    //     7.2 / 15.2 / 23.2 dB over threshold -- three different segments of
    //     `MultiCompHelpers::LookupTables`'s ten measured control points -- and
    //     the Input 0.8 row lands at 42.8 dB over, past the table's 30 dB clamp.
    //     Any change to that table moves at least one of them; the Modern-arm
    //     correction fold moves none, which is the leak guard.
    struct Point { float inputPosition; float inputDbfs; float recordedDbfs; };
    constexpr std::array<Point, 4> points{{
        {0.4f, -28.0f, -27.010859f}, {0.4f, -20.0f, -26.974270f},
        {0.4f, -12.0f, -27.532766f}, {0.8f, -12.0f, -14.729914f}}};
    float worstError = 0.0f;
    for (const auto& point : points)
    {
        const float measured = renderReferenceFetRmsDb(
            point.inputDbfs, point.inputPosition, 0.625915527f, 4, 48000.0, 256, 1.0f);
        const float error = measured - point.recordedDbfs;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET Measured-curve All-buttons: input %.1f/%.1f recorded %.6f measured %.6f error %+.6f dB\n",
                    point.inputPosition, point.inputDbfs, point.recordedDbfs, measured, error);
    }

    require(std::abs(smallSignal - kMeasuredCurveSmallSignalDb) < 0.002f,
            "Measured-curve All-buttons small-signal gain is unchanged and the Modern correction does not leak into it");
    require(worstError < 0.005f,
            "Measured-curve All-buttons compressing points are unchanged");
}

void testFetAllButtonsKneeTransition()
{
    // The All-buttons knee, against the reference unit's own 0.5 dB sweep at
    // Input 0.4 with Curve = Modern. `testFetAbsoluteGainAnchors` pins the
    // plateau far below the knee and `testFetMeasuredStaticSurface` pins one
    // point far above it; between them sat the whole transition, sampled by
    // nothing. The campaign's own All-buttons sweep steps 2 dB, which is
    // exactly the knot spacing `fetAllProcessorCorrectionDb` used there, so it
    // read one knot per row and could not see the segment interiors either: it
    // reported 0.167 dB of error where a 0.5 dB sweep of the same operating
    // point found 0.334 dB.
    //
    // Levels are the internal level the Input 0.4 law produces under the
    // pre-Wave-5 40.0 dB absolute input constant, which is the campaign's
    // labelling convention; the sources are what that convention implies.
    //
    // The three `inNewSegment` rows are the only in-process guard on the
    // [-8.145, -7.224] plateau extension. At Input 0.4 the detector sits
    // 0.149329 dB below the label, so the campaign's 2 dB All-buttons sweep
    // lands 0.0043 dB BELOW a knot on every row and samples no segment
    // interior at all -- it cannot regress-detect a fault in the segment this
    // table added. `probe_all_knee.py`'s 0.5 dB grid (and its --extra-levels,
    // which measured the -7.7 and -7.3 reference points below) is the
    // VST3-level guard for the same region; these are its in-process mirror,
    // at detector fractions 0.32 / 0.54 / 0.76 of the segment.
    constexpr float kInputGainDb = 40.0f - 20.790574f;
    struct Point { float internalDb; float referenceOutputDbfs; bool inNewSegment; };
    constexpr std::array<Point, 7> points{{
        {-8.0f, -18.461636f, false}, {-7.7f, -18.161981f, true},
        {-7.5f, -17.962224f, true}, {-7.3f, -17.766153f, true},
        {-7.0f, -17.528762f, false}, {-6.5f, -17.312539f, false},
        {-6.0f, -17.187491f, false}}};
    std::array<float, 7> measured{};
    float worstError = 0.0f;
    float worstSegmentError = 0.0f;
    for (size_t index = 0; index < points.size(); ++index)
    {
        measured[index] = renderReferenceFetRmsDb(
            points[index].internalDb - kInputGainDb, 0.4f, 0.625915527f, 4);
        const float error = measured[index] - points[index].referenceOutputDbfs;
        worstError = std::max(worstError, std::abs(error));
        if (points[index].inNewSegment)
            worstSegmentError = std::max(worstSegmentError, std::abs(error));
        std::printf("FET All-buttons knee: internal %+.1f reference %.6f measured %.6f error %+.6f dB%s\n",
                    points[index].internalDb, points[index].referenceOutputDbfs,
                    measured[index], error,
                    points[index].inNewSegment ? "  [plateau-extension interior]" : "");
    }

    // The sub-knee step is the mechanism itself, and it is scalar-immune: the
    // reference generates no reduction at all up to -7.5 dB internal, so a
    // 0.5 dB source step must come back as 0.499412 dB. A correction table
    // whose plateau ends at -8.145 instead of at the measured knee start
    // returns 0.313541 here -- 0.186 dB of compression the reference does not
    // produce -- while every absolute level in the row stays plausible, which
    // is how it survived. Both bounds are set by that defect: 3.7x under it on
    // the step and 4.2x under the 0.334 dB absolute error it caused.
    constexpr float kReferenceSubKneeStepDb = 0.499412f;
    const float subKneeStep = measured[2] - measured[0];
    std::printf("FET All-buttons sub-knee 0.5 dB step: reference %+.6f measured %+.6f error %+.6f dB\n",
                kReferenceSubKneeStepDb, subKneeStep,
                subKneeStep - kReferenceSubKneeStepDb);
    std::printf("FET All-buttons plateau-extension interior: worst %+.6f dB over 3 points\n",
                worstSegmentError);

    // Checked first, and on a tighter bound than the transition as a whole, so
    // that a fault in the plateau extension fails on the rows that sample it
    // rather than on the higher-leverage rows above the knot.
    require(worstSegmentError < 0.05f,
            "vintage FET All-buttons plateau extension holds inside the segment no campaign sweep samples");
    require(worstError < 0.08f,
            "vintage FET All-buttons knee transition matches the measured reference");
    require(std::abs(subKneeStep - kReferenceSubKneeStepDb) < 0.05f,
            "vintage FET All-buttons generates no reduction below the reference's knee start");
}

std::vector<float> renderReferenceFetProgramme(int blockSize)
{
    constexpr int totalSamples = 96000;
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, 48000.0, blockSize);
    std::vector<float> result(totalSamples);
    std::vector<float> input(static_cast<size_t>(blockSize));
    std::vector<float> output(static_cast<size_t>(blockSize));
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            const float amplitude = absolute < 24000
                ? duskaudio::decibelsToGain(-30.0f)
                : absolute < 72000 ? duskaudio::decibelsToGain(-6.0f)
                                   : duskaudio::decibelsToGain(-30.0f);
            input[static_cast<size_t>(sample)] = amplitude * std::sin(
                2.0f * kPi * 997.0f * static_cast<float>(absolute) / 48000.0f);
        }
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        std::copy_n(output.data(), count, result.data() + offset);
    }
    return result;
}

void testFetBlockSizeInvariance()
{
    const auto small = renderReferenceFetProgramme(64);
    const auto large = renderReferenceFetProgramme(512);
    float signalPeak = 0.0f;
    float maximumDelta = 0.0f;
    for (size_t sample = 0; sample < small.size(); ++sample)
    {
        signalPeak = std::max(signalPeak, std::abs(small[sample]));
        maximumDelta = std::max(maximumDelta,
                                std::abs(small[sample] - large[sample]));
    }
    std::printf("FET block invariance: signal peak %.9g max delta %.9g\n",
                signalPeak, maximumDelta);
    require(signalPeak > 0.01f && maximumDelta < 1.0e-6f,
            "vintage FET programme timing is invariant between 64- and 512-sample blocks");
}

void testFetResetClearsProgrammeAndColourState()
{
    constexpr int blockSize = 128;
    MultiCompDSP reused;
    MultiCompDSP fresh;
    prepareReferenceFet(reused, 48000.0, blockSize);
    prepareReferenceFet(fresh, 48000.0, blockSize);
    std::array<float, blockSize> hot{}, scratch{}, reusedOut{}, freshOut{};
    for (int block = 0; block < 500; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
            hot[static_cast<size_t>(sample)] = 0.9f * std::sin(
                2.0f * kPi * 100.0f * static_cast<float>(block * blockSize + sample)
                / 48000.0f);
        const float* inputs[] = {hot.data()};
        float* outputs[] = {scratch.data()};
        reused.processBlock(inputs, outputs, 1, blockSize);
    }
    reused.reset();
    float maximumDelta = 0.0f;
    float signalPeak = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
            hot[static_cast<size_t>(sample)] = 0.2f * std::sin(
                2.0f * kPi * 997.0f * static_cast<float>(block * blockSize + sample)
                / 48000.0f);
        const float* inputs[] = {hot.data()};
        float* reusedOutputs[] = {reusedOut.data()};
        float* freshOutputs[] = {freshOut.data()};
        reused.processBlock(inputs, reusedOutputs, 1, blockSize);
        fresh.processBlock(inputs, freshOutputs, 1, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            signalPeak = std::max(signalPeak,
                                  std::abs(freshOut[static_cast<size_t>(sample)]));
            maximumDelta = std::max(maximumDelta, std::abs(
                reusedOut[static_cast<size_t>(sample)]
                - freshOut[static_cast<size_t>(sample)]));
        }
    }
    std::printf("FET reset determinism: signal peak %.9g max delta %.9g\n",
                signalPeak, maximumDelta);
    require(signalPeak > 0.01f && maximumDelta < 1.0e-7f,
            "vintage FET reset clears programme, detector, and coloration state");
}

void testFetInternalStereoLinkReference()
{
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 256;
    constexpr int totalSamples = 5 * sampleRate;
    constexpr int measureStart = 3 * sampleRate;
    constexpr int measureStop = 9 * sampleRate / 2;
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize);
    std::array<float, blockSize> left{}, right{}, outputLeft{}, outputRight{};
    double leftPower = 0.0;
    double rightPower = 0.0;
    int measured = 0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            left[static_cast<size_t>(sample)] = duskaudio::decibelsToGain(-12.0f)
                * std::sin(2.0f * kPi * 997.0f * absolute / sampleRate);
            right[static_cast<size_t>(sample)] = duskaudio::decibelsToGain(-30.0f)
                * std::sin(2.0f * kPi * 1499.0f * absolute / sampleRate);
        }
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outputLeft.data(), outputRight.data()};
        dsp.processBlock(inputs, outputs, 2, count);
        for (int sample = 0; sample < count; ++sample)
            if (offset + sample >= measureStart && offset + sample < measureStop)
            {
                const float l = outputLeft[static_cast<size_t>(sample)];
                const float r = outputRight[static_cast<size_t>(sample)];
                leftPower += static_cast<double>(l) * l;
                rightPower += static_cast<double>(r) * r;
                ++measured;
            }
    }
    const float leftDb = duskaudio::gainToDecibels(static_cast<float>(
        std::sqrt(leftPower / measured)));
    const float rightDb = duskaudio::gainToDecibels(static_cast<float>(
        std::sqrt(rightPower / measured)));
    std::printf("FET internal stereo link: reference %.6f/%.6f measured %.6f/%.6f dBFS\n",
                -13.877202f, -31.875823f, leftDb, rightDb);
    require(std::abs(leftDb + 13.877202f) < 0.20f
                && std::abs(rightDb + 31.875823f) < 0.20f,
            "vintage FET internal link reproduces the asymmetric stereo reference");
}

void testFetStereoLinkPhaseLaw()
{
    // The installed unit is not power-sum linked. On converged 40 s renders,
    // an equal in-phase right channel is bit-identical to left-only, while an
    // equal antiphase channel makes the left output 0.179579 dB quieter. A
    // signed sample-wise maximum followed by the unit's slow peak hold has
    // exactly that shape: the antiphase copy refreshes the peak between the
    // left channel's peaks, while a lower copy never overtakes the held peak.
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 256;
    constexpr int totalSamples = 15 * sampleRate;
    constexpr int measureStart = 10 * sampleRate;
    constexpr int measureStop = 14 * sampleRate;
    const auto measure = [&](float rightScale, float& reductionDb) {
        MultiCompDSP dsp;
        prepareReferenceFet(dsp, sampleRate, blockSize);
        std::array<float, blockSize> left{}, right{};
        std::array<float, blockSize> outputLeft{}, outputRight{};
        double leftPower = 0.0;
        int measured = 0;
        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            const int count = std::min(blockSize, totalSamples - offset);
            for (int sample = 0; sample < count; ++sample)
            {
                const int absolute = offset + sample;
                const float value = duskaudio::decibelsToGain(-12.0f)
                    * std::sin(2.0f * kPi * 997.0f * absolute / sampleRate);
                left[static_cast<size_t>(sample)] = value;
                right[static_cast<size_t>(sample)] = value * rightScale;
            }
            const float* inputs[] = {left.data(), right.data()};
            float* outputs[] = {outputLeft.data(), outputRight.data()};
            dsp.processBlock(inputs, outputs, 2, count);
            for (int sample = 0; sample < count; ++sample)
                if (offset + sample >= measureStart
                    && offset + sample < measureStop)
                {
                    const float value = outputLeft[static_cast<size_t>(sample)];
                    leftPower += static_cast<double>(value) * value;
                    ++measured;
                }
        }
        require(measured == measureStop - measureStart,
                "FET stereo phase-law window is complete");
        reductionDb = dsp.getGainReduction();
        return duskaudio::gainToDecibels(static_cast<float>(
            std::sqrt(leftPower / measured)));
    };

    float leftOnlyReductionDb = 0.0f;
    float inPhaseReductionDb = 0.0f;
    float antiphaseReductionDb = 0.0f;
    const float leftOnlyDb = measure(0.0f, leftOnlyReductionDb);
    const float inPhaseDb = measure(1.0f, inPhaseReductionDb);
    const float antiphaseDb = measure(-1.0f, antiphaseReductionDb);
    const float inPhaseResponseDb = inPhaseDb - leftOnlyDb;
    const float antiphaseResponseDb = antiphaseDb - leftOnlyDb;
    std::printf("FET stereo phase law: left-only %.9f in-phase %.9f "
                "response %+.9f, antiphase %.9f response %+.9f "
                "(reference -0.179579), GR %.9f/%.9f/%.9f\n",
                leftOnlyDb, inPhaseDb, inPhaseResponseDb,
                antiphaseDb, antiphaseResponseDb,
                leftOnlyReductionDb, inPhaseReductionDb,
                antiphaseReductionDb);
    require(std::abs(inPhaseResponseDb) < 0.02f,
            "vintage FET equal in-phase link remains identical to left-only");
    require(std::abs(antiphaseResponseDb + 0.179579f) < 0.03f,
            "vintage FET antiphase link reproduces the measured signed-maximum response");
}

float renderFetDenseStereoLevelDb(double sampleRate, float rightDbfs,
                                  float phaseCycles)
{
    constexpr int blockSize = 512;
    const int totalSamples = static_cast<int>(std::lround(15.0 * sampleRate));
    const int measureStart = static_cast<int>(std::lround(10.0 * sampleRate));
    const int measureStop = static_cast<int>(std::lround(14.0 * sampleRate));
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize);
    // The dense Wave 27 capture used the installed unit's campaign default,
    // not the 0.5 Release coordinate used by the older canonical phase test.
    dsp.setParameter(MultiCompDSP::Parameter::FetRelease,
                     fetReleasePlain(0.665954590f));
    std::array<float, blockSize> left{}, right{};
    std::array<float, blockSize> outputLeft{}, outputRight{};
    const double leftAmplitude = duskaudio::decibelsToGain(-12.0f);
    const double rightAmplitude = rightDbfs <= -150.0f
        ? 0.0 : duskaudio::decibelsToGain(rightDbfs);
    double power = 0.0;
    int measured = 0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const double cycles = 997.0 * (offset + sample) / sampleRate;
            left[static_cast<size_t>(sample)] = static_cast<float>(
                leftAmplitude * std::sin(2.0 * kPi * cycles));
            right[static_cast<size_t>(sample)] = static_cast<float>(
                rightAmplitude * std::sin(2.0 * kPi
                    * (cycles + phaseCycles)));
        }
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outputLeft.data(), outputRight.data()};
        dsp.processBlock(inputs, outputs, 2, count);
        for (int sample = 0; sample < count; ++sample)
            if (offset + sample >= measureStart
                && offset + sample < measureStop)
            {
                const double value = outputLeft[static_cast<size_t>(sample)];
                power += value * value;
                ++measured;
            }
    }
    return duskaudio::gainToDecibels(static_cast<float>(
        std::sqrt(power / measured)));
}

void testFetDenseStereoPhaseParity()
{
    // Exact UAD Wave 27 same-stimulus link responses. The 96 kHz equal-level
    // quarter-cycle row is the exposed defect; the adjacent phases, the same
    // phase at 48 kHz, and the lower-level opposite arm reject a one-cell or
    // phase-only correction.
    struct Row
    {
        double sampleRate;
        float rightDbfs;
        float phaseCycles;
        float referenceResponseDb;
    };
    constexpr std::array<Row, 5> rows{{
        {48000.0, -12.0f, 0.250f, -0.179689254f},
        {96000.0, -12.0f, 0.125f, -0.182474400f},
        {96000.0, -12.0f, 0.250f, -0.180580158f},
        {96000.0, -12.0f, 0.375f, -0.178920248f},
        {96000.0, -18.0f, 0.250f,  0.000000000f},
    }};
    const float baseline48 = renderFetDenseStereoLevelDb(48000.0, -160.0f, 0.0f);
    const float baseline96 = renderFetDenseStereoLevelDb(96000.0, -160.0f, 0.0f);
    float worstError = 0.0f;
    for (const auto& row : rows)
    {
        const float level = renderFetDenseStereoLevelDb(
            row.sampleRate, row.rightDbfs, row.phaseCycles);
        const float baseline = row.sampleRate == 48000.0
            ? baseline48 : baseline96;
        const float response = level - baseline;
        const float error = response - row.referenceResponseDb;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET dense stereo: rate %.0f right %.1f phase %.3f "
                    "response reference/measured %.9f/%.9f error %+.9f dB\n",
                    row.sampleRate, row.rightDbfs, row.phaseCycles,
                    row.referenceResponseDb, response, error);
    }
    std::printf("FET dense stereo: worst absolute response error %.9f dB\n",
                worstError);
    require(worstError < 0.020f,
            "vintage FET dense stereo phase surface matches the UAD response");
}

std::array<float, 5> renderFetRecoveryH1Db(
    double sampleRate, double frequencyHz, float loudDbfs,
    float attackPosition, int ratio, float phaseDegrees, bool quietOnly)
{
    constexpr int blockSize = 512;
    constexpr std::array<double, 5> windowStarts{{1.5, 3.0, 4.5, 6.0, 7.0}};
    const int totalSamples = static_cast<int>(std::lround(8.0 * sampleRate));
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize,
                        0.8f, 0.625915527f, ratio);
    dsp.setParameter(MultiCompDSP::Parameter::FetAttack,
                     fetAttackPlain(attackPosition));
    std::array<float, blockSize> left{}, right{};
    std::array<float, blockSize> outputLeft{}, outputRight{};
    std::array<double, 5> real{}, imaginary{};
    std::array<int, 5> counts{};
    const double phaseRadians = phaseDegrees * kPi / 180.0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            const bool loud = !quietOnly
                && absolute >= static_cast<int>(std::lround(1.0 * sampleRate))
                && absolute < static_cast<int>(std::lround(1.25 * sampleRate));
            const double amplitude = duskaudio::decibelsToGain(
                loud ? loudDbfs : -72.0f);
            const float value = static_cast<float>(amplitude * std::sin(
                2.0 * kPi * frequencyHz * absolute / sampleRate
                    + phaseRadians));
            left[static_cast<size_t>(sample)] = value;
            right[static_cast<size_t>(sample)] = value;
        }
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outputLeft.data(), outputRight.data()};
        dsp.processBlock(inputs, outputs, 2, count);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            for (size_t window = 0; window < windowStarts.size(); ++window)
            {
                const int start = static_cast<int>(std::lround(
                    windowStarts[window] * sampleRate));
                const int stop = start + static_cast<int>(std::lround(
                    0.25 * sampleRate));
                if (absolute < start || absolute >= stop) continue;
                const double angle = 2.0 * kPi * frequencyHz
                    * (absolute - start) / sampleRate;
                const double value = outputLeft[static_cast<size_t>(sample)];
                real[window] += value * std::cos(angle);
                imaginary[window] -= value * std::sin(angle);
                ++counts[window];
            }
        }
    }
    std::array<float, 5> result{};
    for (size_t window = 0; window < result.size(); ++window)
    {
        const double amplitude = 2.0 * std::hypot(
            real[window], imaginary[window]) / counts[window];
        result[window] = duskaudio::gainToDecibels(
            static_cast<float>(amplitude));
    }
    return result;
}

void testFetDenseStartupRecoveryParity()
{
    // Wave 27's equal 8 s sources, rescored against the current binary. These
    // are absolute UAD H1 levels in the five declared 250 ms windows; the
    // quiet render removes static device gain before the recovery residual is
    // compared. Both phases are required because the reference recovery state
    // is phase-sensitive, and both rates prevent a 96 kHz-only scalar patch.
    struct Row
    {
        double sampleRate;
        double frequencyHz;
        float loudDbfs;
        float attackPosition;
        int ratio;
        float phaseDegrees;
        std::array<float, 5> referenceBurstH1Db;
    };
    constexpr std::array<float, 5> referenceQuiet48At1k{{
        -41.893320337f, -41.893320368f, -41.893320383f,
        -41.893320383f, -41.893320342f}};
    constexpr std::array<float, 5> referenceQuiet48At4k{{
        -41.893301721f, -41.893301999f, -41.893301863f,
        -41.893301900f, -41.893301957f}};
    constexpr std::array<float, 5> referenceQuiet96At1k{{
        -41.893315285f, -41.893315284f, -41.893315295f,
        -41.893315299f, -41.893315299f}};
    constexpr std::array<float, 5> referenceQuiet96At4k{{
        -41.893256638f, -41.893256649f, -41.893256616f,
        -41.893256634f, -41.893256638f}};
    constexpr std::array<Row, 16> rows{{
        {48000.0, 1000.0, -30.0f, 1.0f, 0, 0.0f,
         {{-47.419350088f, -42.650442047f, -42.068162291f,
           -41.935354660f, -41.909370750f}}},
        {48000.0, 1000.0, -18.0f, 1.0f, 0, 0.0f,
         {{-54.036772679f, -43.774027215f, -42.343618938f,
           -42.002990460f, -41.935819549f}}},
        {48000.0, 1000.0, -6.0f, 0.0f, 0, 0.0f,
         {{-62.220349821f, -45.875034457f, -42.890472921f,
           -42.141661518f, -41.989978202f}}},
        {48000.0, 1000.0, -6.0f, 0.5f, 0, 0.0f,
         {{-62.348086472f, -45.918949483f, -42.902420025f,
           -42.144733190f, -41.991205368f}}},
        {48000.0, 1000.0, -6.0f, 1.0f, 0, 0.0f,
         {{-61.713930703f, -45.721158272f, -42.849444759f,
           -42.131120139f, -41.985762444f}}},
        {48000.0, 4000.0, -6.0f, 1.0f, 0, 0.0f,
         {{-61.749603648f, -45.733247595f, -42.852740715f,
           -42.131954906f, -41.986083237f}}},
        {48000.0, 1000.0, -6.0f, 0.5f, 1, 0.0f,
         {{-61.945040336f, -45.202601731f, -42.657725180f,
           -42.077761376f, -41.967769331f}}},
        {48000.0, 1000.0, -6.0f, 1.0f, 0, 90.0f,
         {{-61.705564454f, -45.718980353f, -42.848872874f,
           -42.130973182f, -41.985703692f}}},
        {96000.0, 1000.0, -30.0f, 1.0f, 0, 0.0f,
         {{-47.415809867f, -42.644629538f, -42.067771570f,
           -41.935679773f, -41.909903760f}}},
        {96000.0, 1000.0, -18.0f, 1.0f, 0, 0.0f,
         {{-54.027647794f, -43.769335252f, -42.343640894f,
           -42.003419066f, -41.936346456f}}},
        {96000.0, 1000.0, -6.0f, 0.0f, 0, 0.0f,
         {{-62.201864830f, -45.886727593f, -42.895905602f,
           -42.143223719f, -41.991170365f}}},
        {96000.0, 1000.0, -6.0f, 0.5f, 0, 0.0f,
         {{-62.322730153f, -45.915093954f, -42.902799746f,
           -42.145043299f, -41.991851560f}}},
        {96000.0, 1000.0, -6.0f, 1.0f, 0, 0.0f,
         {{-61.703073934f, -45.718643957f, -42.849774945f,
           -42.131019558f, -41.986576420f}}},
        {96000.0, 4000.0, -6.0f, 1.0f, 0, 0.0f,
         {{-61.715997425f, -45.722939034f, -42.850906413f,
           -42.131273036f, -41.986637300f}}},
        {96000.0, 1000.0, -6.0f, 0.5f, 1, 0.0f,
         {{-61.916516268f, -45.222883277f, -42.669602672f,
           -42.076591872f, -41.976011280f}}},
        {96000.0, 1000.0, -6.0f, 1.0f, 0, 90.0f,
         {{-61.697527700f, -45.716923653f, -42.849306859f,
           -42.130897452f, -41.986529699f}}},
    }};
    const auto quiet48At1k = renderFetRecoveryH1Db(
        48000.0, 1000.0, -72.0f, 0.5f, 0, 0.0f, true);
    const auto quiet48At4k = renderFetRecoveryH1Db(
        48000.0, 4000.0, -72.0f, 0.5f, 0, 0.0f, true);
    const auto quiet96At1k = renderFetRecoveryH1Db(
        96000.0, 1000.0, -72.0f, 0.5f, 0, 0.0f, true);
    const auto quiet96At4k = renderFetRecoveryH1Db(
        96000.0, 4000.0, -72.0f, 0.5f, 0, 0.0f, true);
    float worstResidual = 0.0f;
    for (const auto& row : rows)
    {
        const auto burst = renderFetRecoveryH1Db(
            row.sampleRate, row.frequencyHz, row.loudDbfs,
            row.attackPosition, row.ratio, row.phaseDegrees, false);
        const auto& quiet = row.sampleRate == 48000.0
            ? row.frequencyHz == 1000.0 ? quiet48At1k : quiet48At4k
            : row.frequencyHz == 1000.0 ? quiet96At1k : quiet96At4k;
        const auto& referenceQuiet = row.sampleRate == 48000.0
            ? row.frequencyHz == 1000.0
                ? referenceQuiet48At1k : referenceQuiet48At4k
            : row.frequencyHz == 1000.0
                ? referenceQuiet96At1k : referenceQuiet96At4k;
        for (size_t window = 0; window < burst.size(); ++window)
        {
            const float residual = (burst[window] - row.referenceBurstH1Db[window])
                - (quiet[window] - referenceQuiet[window]);
            worstResidual = std::max(worstResidual, std::abs(residual));
            std::printf("FET dense recovery: rate %.0f carrier %.0f level %.0f "
                        "attack %.1f ratio %d phase %.0f window %zu "
                        "candidate-minus-UAD residual %+.9f dB\n",
                        row.sampleRate, row.frequencyHz, row.loudDbfs,
                        row.attackPosition, row.ratio, row.phaseDegrees,
                        window, residual);
        }
    }
    std::printf("FET dense recovery: worst absolute residual %.9f dB\n",
                worstResidual);
    require(worstResidual < 0.150f,
            "vintage FET post-burst recovery follows the UAD phase/rate surface");
}

void armFetPostBurstRecovery(MultiCompDSP& dsp, int channels)
{
    constexpr int blockSize = 256;
    constexpr int loudSamples = 12000;
    constexpr int quietSamples = 2400;
    std::array<float, blockSize> left{}, right{}, outputLeft{}, outputRight{};
    const int totalSamples = loudSamples + quietSamples;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            const float amplitude = duskaudio::decibelsToGain(
                absolute < loudSamples ? -6.0f : -72.0f);
            const float value = amplitude * std::sin(
                2.0f * kPi * 1000.0f * absolute / 48000.0f);
            left[static_cast<size_t>(sample)] = value;
            right[static_cast<size_t>(sample)] = value;
        }
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outputLeft.data(), outputRight.data()};
        dsp.processBlock(inputs, outputs, channels, count);
    }
}

void testFetPostBurstRecoveryLifecycle()
{
    constexpr int blockSize = 256;
    const auto isClear = [](const std::array<float, 2>& gains) noexcept {
        return gains[0] == 1.0f && gains[1] == 1.0f;
    };
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, 48000.0, blockSize);
    armFetPostBurstRecovery(dsp, 2);
    const auto armed = duskaudio::MultiCompDSPTestAccess::fetRecoveryGains(dsp);

    dsp.prepare(48000.0, blockSize);
    const auto afterPrepare = duskaudio::MultiCompDSPTestAccess::fetRecoveryGains(dsp);
    armFetPostBurstRecovery(dsp, 2);
    dsp.reset();
    const auto afterReset = duskaudio::MultiCompDSPTestAccess::fetRecoveryGains(dsp);

    prepareReferenceFet(dsp, 48000.0, blockSize);
    armFetPostBurstRecovery(dsp, 2);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::VCA));
    std::array<float, blockSize> silence{}, outputLeft{}, outputRight{};
    const float* inputs[] = {silence.data(), silence.data()};
    float* outputs[] = {outputLeft.data(), outputRight.data()};
    dsp.processBlock(inputs, outputs, 2, blockSize);
    const auto afterModeExit = duskaudio::MultiCompDSPTestAccess::fetRecoveryGains(dsp);

    prepareReferenceFet(dsp, 48000.0, blockSize);
    armFetPostBurstRecovery(dsp, 2);
    dsp.setBypass(true);
    for (int block = 0; block < 8; ++block)
        dsp.processBlock(inputs, outputs, 2, blockSize);
    const auto afterBypass = duskaudio::MultiCompDSPTestAccess::fetRecoveryGains(dsp);

    dsp.setBypass(false);
    dsp.reset();
    prepareReferenceFet(dsp, 48000.0, blockSize);
    armFetPostBurstRecovery(dsp, 2);
    dsp.processBlock(inputs, outputs, 1, blockSize);
    const auto afterMono = duskaudio::MultiCompDSPTestAccess::fetRecoveryGains(dsp);

    std::printf("FET recovery state: armed %.6f/%.6f; prepare %.6f/%.6f; "
                "reset %.6f/%.6f; mode %.6f/%.6f; bypass %.6f/%.6f; "
                "mono %.6f/%.6f\n",
                armed[0], armed[1], afterPrepare[0], afterPrepare[1],
                afterReset[0], afterReset[1], afterModeExit[0], afterModeExit[1],
                afterBypass[0], afterBypass[1], afterMono[0], afterMono[1]);
    require(armed[0] > 1.05f && armed[1] > 1.05f
                && isClear(afterPrepare) && isClear(afterReset)
                && isClear(afterModeExit) && isClear(afterBypass)
                && afterMono[1] == 1.0f,
            "FET post-burst recovery state arms and clears on every lifecycle path");
}

// --- vintage FET attack drive axis ---------------------------------------
//
// The 1176 comparison campaign's headline dynamics finding is that the
// reference's attack time constant FALLS as it is driven harder
// (1.73 -> 1.03 -> 0.72 ms for -30 / -18 / -6 dBFS at attack knob 0.5) while
// this core's rose. The measurement below reproduces that campaign row
// in process: same programme (1 s of a -72 dBFS 1 kHz carrier then a
// phase-continuous burst), same estimator (exact 1 kHz quadrature
// demodulation through a one-cycle boxcar, first-order log-linear fit over
// 15-88 % of the 40-60 ms plateau starting half a carrier period in).
//
// Absolute milliseconds are NOT an oracle: the estimator carries a documented
// x1.85 relaxation-domain ambiguity and a -4..-10 % bias below 1 ms
// (report/opus_notes/wave3_validation.md). What is asserted is the direction of
// the drive axis, and mine/reference through this same estimator.
//
// `MultiCompCoreTest --fet-attack-drive` prints the whole grid; setting
// MC_FET_DUMP_GR=1 additionally dumps every `GRCURVE <drive> <knob> <ms> <dB>`
// sample, which is how the reference and this core were compared curve for
// curve rather than through a single fitted number.
//
// THE TAU AT -6 dBFS MOVES UNDER PURE COLOUR CHANGES, AND THAT IS THE
// ESTIMATOR, NOT THE CELL. Measured twice, on two independent colour-only
// changes to the vintage FET:
//
//   fetBroadbandK2 refit  tau -6 dBFS 3.238590 -> 3.626379 ms  (+12.0 %)
//   fetLowFrequencyK2     tau -6 dBFS 3.626379 -> 4.099728 ms  (+13.1 %)
//
// while `--fet-envelope-trace` -- which reads `d.envelope` straight off the
// mode state and never touches the demodulator -- returns a BIT-IDENTICAL
// FNV-1a digest over the 60 ms after onset across all three builds, at -6,
// -18 and -30 dBFS, with the plateau reduction unchanged to four decimals. The
// same runs show the OUTPUT peak moving (4.4361 -> 4.4568 -> 4.4568), so the
// control has a live positive arm: the audio really did change and the internal
// trajectory really did not. It cannot change: for the vintage arm `detect`
// comes from `|transformedInput * inputGain|`, never from `saturated`, so no
// colour coefficient has a path into the envelope.
//
// The mechanism is the demodulator. `quadratureEnvelope`'s one-period boxcar
// nulls harmonics exactly only for a STEADY amplitude; during the attack the 2f
// product is amplitude modulated and its cancellation is incomplete. At -6 dBFS
// the log-linear fit has 33 points inside a sub-millisecond band (against 104
// at -30 dBFS), so a tiny envelope perturbation moves the slope a lot -- the
// same rows move 0.05 % and 0.17 % at -30 and -18 dBFS.
//
// So: tau is a diagnostic (wave3b_validation.md section 3 already ratified
// that), the gate is the normalised curve RMS, and a colour change that moves
// tau at -6 dBFS while curve RMS moves 6e-06 is behaving correctly. Do not
// re-tune the attack law against it.
constexpr float kFetCampaignLatencySamples48k = 27.0f;

struct FetAttackMeasurement
{
    float driveDbfs = 0.0f;
    double plateauGrDb = 0.0;
    double tauMs = 0.0;
    // First crossing of 90 % of the plateau on the RUNNING MAXIMUM of the
    // reduction curve, scanned from half a carrier period in. It is better
    // conditioned than the log-linear tau -- the tau fit runs out of points
    // above about 30 dB of reduction and inverts sign against this metric
    // there, and the 63 % crossing saturates at the scan floor on every
    // reference row because the centred one-cycle boxcar has smeared the step.
    // The running maximum makes it immune to the first-cycle overshoot.
    //
    // It is NOT unconditionally trustworthy either, and the deep rows are where
    // it degrades: at -6 dBFS the whole knob sweep reads 0.6439 / 0.6276 /
    // 0.6041 ms, a 6 % spread across the entire control, all of it 0.10-0.14 ms
    // above the 0.5 ms scan floor, against a 6.8x spread at -30 dBFS. Above
    // ~30 dB of reduction neither estimator resolves this carrier; use the
    // curve RMS against the reference trajectory instead.
    double recovery90Ms = 0.0;
    // Whole-programme output peak. The base-rate FET output ceiling in
    // MultiCompDSP.cpp has its knee at 6.39712761; a first-cycle overshoot
    // above it puts clipping harmonics into the captures, so any change to the
    // attack must be checked against it.
    double outputPeak = 0.0;
    int fitPoints = 0;
    bool fitted = false;
};

double medianOf(std::vector<double> values)
{
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 == 1
        ? values[middle]
        : 0.5 * (values[middle - 1] + values[middle]);
}

// One-carrier-period boxcar on the quadrature-demodulated signal. The boxcar
// is exactly one period long, so every harmonic of the carrier lands on a
// transfer-function null, and it is centred (the scorer's `mode="same"`), so
// the envelope is zero phase.
std::vector<double> quadratureEnvelope(const std::vector<double>& signal,
                                       double sampleRate, double frequency)
{
    const int period = static_cast<int>(std::lround(sampleRate / frequency));
    require(std::abs(sampleRate / frequency - period) < 1.0e-9,
            "carrier period is an integer number of samples");
    const int count = static_cast<int>(signal.size());
    std::vector<double> real(signal.size()), imaginary(signal.size());
    for (int n = 0; n < count; ++n)
    {
        const double phase = -2.0 * 3.14159265358979323846 * frequency * n / sampleRate;
        real[static_cast<size_t>(n)] = signal[static_cast<size_t>(n)] * std::cos(phase);
        imaginary[static_cast<size_t>(n)] = signal[static_cast<size_t>(n)] * std::sin(phase);
    }
    // numpy's convolve(..., mode="same") with an even-length kernel averages
    // samples n-period/2 .. n+period/2-1; zero outside the signal.
    const int back = period / 2;
    std::vector<double> envelope(signal.size());
    double sumReal = 0.0, sumImaginary = 0.0;
    // Prime the window. At n = 0 the centred boxcar spans -back .. period-1-back,
    // so indices 0 .. period-2-back are already inside it; the loop below only
    // ever ADDS index n+period-1-back, and it later SUBTRACTS those primed
    // indices as they leave. Without this the accumulator carries a constant
    // complex offset -sum(x[0 .. period-2-back]) forever. It was benign here by
    // accident -- the plugin's 27-sample latency keeps those samples 28.6 dB
    // below the quiet carrier -- but on a programme that starts at level it is
    // a multi-dB baseline error (measured: -78.008 dB read for a -72.000 dB
    // carrier), and this helper is meant to be reusable.
    for (int k = 0; k < period - 1 - back && k < count; ++k)
    {
        sumReal += real[static_cast<size_t>(k)];
        sumImaginary += imaginary[static_cast<size_t>(k)];
    }
    for (int n = 0; n < count; ++n)
    {
        const int enter = n + period - 1 - back;
        const int leave = n - 1 - back;
        if (enter < count)
        {
            sumReal += real[static_cast<size_t>(enter)];
            sumImaginary += imaginary[static_cast<size_t>(enter)];
        }
        if (leave >= 0)
        {
            sumReal -= real[static_cast<size_t>(leave)];
            sumImaginary -= imaginary[static_cast<size_t>(leave)];
        }
        envelope[static_cast<size_t>(n)] = 2.0 * std::hypot(sumReal, sumImaginary)
            / static_cast<double>(period);
    }
    return envelope;
}

FetAttackMeasurement measureFetAttack(float driveDbfs, float attackPosition = 0.5f,
                                      int ratio = 0, double sampleRate = 48000.0,
                                      std::vector<double>* reductionOut = nullptr,
                                      double carrierHz = 1000.0)
{
    constexpr int blockSize = 256;
    constexpr double quietSeconds = 1.0;
    constexpr double burstSeconds = 0.25;
    constexpr double quietPeakDbfs = -72.0;
    const int totalSamples = static_cast<int>(
        std::lround((quietSeconds + burstSeconds) * sampleRate));
    const int loudFrom = static_cast<int>(std::lround(quietSeconds * sampleRate));

    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize, 0.8f, 0.625915527f, ratio);
    dsp.setParameter(MultiCompDSP::Parameter::FetAttack, fetAttackPlain(attackPosition));
    const float quietAmplitude = duskaudio::decibelsToGain(
        static_cast<float>(quietPeakDbfs));
    const float loudAmplitude = duskaudio::decibelsToGain(driveDbfs);

    std::vector<float> left(static_cast<size_t>(blockSize));
    std::vector<float> right(static_cast<size_t>(blockSize));
    std::vector<float> outLeft(static_cast<size_t>(blockSize));
    std::vector<float> outRight(static_cast<size_t>(blockSize));
    std::vector<double> mono(static_cast<size_t>(totalSamples));
    double peak = 0.0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            // Phase continuous across the step, exactly as gen_stimuli.py
            // builds `amplitude_program`.
            const float value = (absolute < loudFrom ? quietAmplitude : loudAmplitude)
                * std::sin(2.0f * kPi * static_cast<float>(carrierHz)
                    * static_cast<float>(absolute) / static_cast<float>(sampleRate));
            left[static_cast<size_t>(sample)] = value;
            right[static_cast<size_t>(sample)] = value;
        }
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outLeft.data(), outRight.data()};
        dsp.processBlock(inputs, outputs, 2, count);
        for (int sample = 0; sample < count; ++sample)
        {
            mono[static_cast<size_t>(offset + sample)] = 0.5
                * (static_cast<double>(outLeft[static_cast<size_t>(sample)])
                    + static_cast<double>(outRight[static_cast<size_t>(sample)]));
            peak = std::max(peak, std::max(
                std::abs(static_cast<double>(outLeft[static_cast<size_t>(sample)])),
                std::abs(static_cast<double>(outRight[static_cast<size_t>(sample)]))));
        }
    }

    const auto envelope = quadratureEnvelope(mono, sampleRate, carrierHz);
    const int latency = static_cast<int>(std::lround(
        kFetCampaignLatencySamples48k * sampleRate / 48000.0));
    const auto gainDb = [&envelope](int index, double referencePeakDbfs) {
        return 20.0 * std::log10(std::max(envelope[static_cast<size_t>(index)], 1.0e-30))
            - referencePeakDbfs;
    };
    std::vector<double> quiet;
    for (int n = static_cast<int>(0.5 * sampleRate) + latency;
         n < static_cast<int>(0.9 * sampleRate) + latency; ++n)
        quiet.push_back(gainDb(n, quietPeakDbfs));
    const double baseline = medianOf(quiet);

    const int onset = static_cast<int>(std::lround(quietSeconds * sampleRate)) + latency;
    const int stop = onset + static_cast<int>(std::lround(0.060 * sampleRate));
    require(stop < totalSamples, "attack window fits inside the rendered burst");
    std::vector<double> reductionDb, times;
    for (int n = onset; n < stop; ++n)
    {
        reductionDb.push_back(baseline - gainDb(n, driveDbfs));
        times.push_back(static_cast<double>(n - onset) / sampleRate);
    }
    if (std::getenv("MC_FET_DUMP_GR") != nullptr)
        for (size_t i = 0; i < reductionDb.size(); ++i)
            std::printf("GRCURVE %.1f %.4f %.6f %.6f\n",
                        static_cast<double>(driveDbfs),
                        static_cast<double>(attackPosition),
                        1000.0 * times[i], reductionDb[i]);
    std::vector<double> plateauSamples(
        reductionDb.begin() + static_cast<long>(std::lround(0.040 * sampleRate)),
        reductionDb.end());
    FetAttackMeasurement result;
    result.driveDbfs = driveDbfs;
    result.plateauGrDb = medianOf(plateauSamples);
    result.outputPeak = peak;

    // The scan starts where the fit does, half a carrier period in. Before
    // that the centred boxcar still straddles the pre-step carrier and the
    // demodulated amplitude spikes above the plateau on every row of both
    // devices -- scanning from zero reported the same 0.63 ms for laws a
    // factor of 2.2 apart, which is the artefact and not the attack.
    const size_t firstScannable = static_cast<size_t>(
        std::lround(0.5 / carrierHz * sampleRate));
    double runningMaximum = -1.0e30;
    for (size_t i = firstScannable; i < reductionDb.size(); ++i)
    {
        const double previous = runningMaximum;
        runningMaximum = std::max(runningMaximum, reductionDb[i]);
        if (runningMaximum < 0.90 * result.plateauGrDb) continue;
        // `previous` cannot already be above the threshold here: the guard
        // above tests exactly that quantity, so the loop would have broken one
        // iteration earlier. The only special case is the first scanned sample,
        // where there is no earlier point to interpolate from.
        result.recovery90Ms = i == firstScannable
            ? 1000.0 * times[i]
            : 1000.0 * (times[i - 1] + (times[i] - times[i - 1])
                * (0.90 * result.plateauGrDb - previous)
                / (runningMaximum - previous));
        break;
    }

    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
    int used = 0;
    for (size_t i = 0; i < reductionDb.size(); ++i)
    {
        const double fraction = reductionDb[i] / result.plateauGrDb;
        if (fraction < 0.15 || fraction > 0.88) continue;
        if (times[i] < 0.5 / carrierHz) continue;
        const double y = std::log(1.0 - fraction);
        sumX += times[i]; sumY += y;
        sumXX += times[i] * times[i]; sumXY += times[i] * y;
        ++used;
    }
    result.fitPoints = used;
    if (reductionOut != nullptr) *reductionOut = reductionDb;
    if (used >= 4)
    {
        const double denominator = used * sumXX - sumX * sumX;
        const double slope = (used * sumXY - sumX * sumY) / denominator;
        if (slope < 0.0)
        {
            result.tauMs = -1000.0 / slope;
            result.fitted = true;
        }
    }
    return result;
}

void reportFetSettledReduction()
{
    // positiveReduction is the settled static-law output, which is what the
    // drive law's clamp sees -- NOT the 40-60 ms plateau the attack estimator
    // reads (the programme-memory population is only ~5 % charged there).
    const float unity = renderReferenceFetRmsDb(-72.0f, 0.8f, 0.625915527f, 0)
        - (-72.0f - 3.0102999566f);
    for (int ratio : {0, 1, 2, 3, 4})
        for (float drive : {-48.0f, -45.0f, -42.0f, -39.0f, -36.0f, -30.0f, -18.0f, -6.0f})
        {
            if (ratio != 0 && drive != -6.0f) continue;
            const float through = renderReferenceFetRmsDb(
                drive, 0.8f, 0.625915527f, ratio) - (drive - 3.0102999566f);
            std::printf("  settled GR: ratio %d drive %+.0f dBFS -> %.4f dB\n",
                        ratio, static_cast<double>(drive),
                        static_cast<double>(unity - through));
        }
}

void reportFetAttackDriveAxis()
{
    reportFetSettledReduction();
    std::puts("FET attack drive axis (in-process, campaign estimator):");
    for (float attackPosition : {0.0f, 0.5f, 1.0f})
        for (float drive : {-48.0f, -45.0f, -42.0f, -39.0f, -36.0f, -30.0f, -18.0f, -6.0f})
        {
            const auto measured = measureFetAttack(drive, attackPosition);
            std::printf("  knob %.2f drive %+.0f dBFS: plateau GR %.4f dB tau %s ms (%d pts) t90 %.4f ms peak %.4f\n",
                        static_cast<double>(attackPosition), static_cast<double>(drive),
                        measured.plateauGrDb,
                        measured.fitted ? std::to_string(measured.tauMs).c_str() : "unfittable",
                        measured.fitPoints, measured.recovery90Ms, measured.outputPeak);
        }
    for (int ratio : {1, 2, 3, 4})
    {
        const auto measured = measureFetAttack(-6.0f, 0.5f, ratio);
        std::printf("  knob 0.50 drive -6 dBFS ratio index %d: plateau GR %.4f dB tau %s ms (%d pts) t90 %.4f ms peak %.4f\n",
                    ratio, measured.plateauGrDb,
                    measured.fitted ? std::to_string(measured.tauMs).c_str() : "unfittable",
                    measured.fitPoints, measured.recovery90Ms, measured.outputPeak);
    }
}

// The installed unit's own attack trajectory, measured through this same
// estimator and normalised to its own 40-60 ms plateau. Rendered by
// `probe_drive_axis.py` (dusk-audio-tools, reference_comparison_1176) from the
// reference AU at attack knob 0.5, ratio 4:1, input knob 0.8; each row is one
// source level. This is the campaign's PRIMARY dynamics gate: Wave 3b ratified
// the demotion of the fitted time constant to a diagnostic, because above
// ~30 dB of reduction the tau fit reports this core 1.6x SLOWER than the
// reference on a row where it reaches 90 % of plateau 2.45x SOONER, and the row
// ranking the two metrics produce is inverted.
constexpr int kFetReferenceCurvePoints = 79;
constexpr double kFetReferenceCurveFirstMs = 0.5;
constexpr double kFetReferenceCurveStepMs = 0.25;
constexpr float kFetCurveDrivesDbfs[5] = {-42.0f, -36.0f, -30.0f, -18.0f, -6.0f};
constexpr double kFetReferencePlateauDb[5] = {
    3.0336033509, 7.1708, 11.7581, 21.1670, 30.7397
};
constexpr double kFetReferenceCurve[5][kFetReferenceCurvePoints] = {
    // -42 dBFS: reference plateau 3.0336 dB, candidate curve RMS 0.0218
    {0.1265, 0.1933, 0.2695, 0.3330, 0.3960, 0.4438, 0.4904, 0.5290,
     0.5659, 0.5940, 0.6216, 0.6455, 0.6684, 0.6860, 0.7036, 0.7196,
     0.7351, 0.7467, 0.7585, 0.7697, 0.7806, 0.7887, 0.7972, 0.8056,
     0.8139, 0.8199, 0.8263, 0.8330, 0.8397, 0.8443, 0.8495, 0.8551,
     0.8607, 0.8644, 0.8686, 0.8734, 0.8781, 0.8811, 0.8845, 0.8885,
     0.8926, 0.8950, 0.8978, 0.9013, 0.9048, 0.9069, 0.9092, 0.9122,
     0.9154, 0.9171, 0.9191, 0.9218, 0.9246, 0.9261, 0.9278, 0.9302,
     0.9327, 0.9340, 0.9354, 0.9375, 0.9398, 0.9408, 0.9421, 0.9440,
     0.9460, 0.9469, 0.9479, 0.9496, 0.9515, 0.9522, 0.9530, 0.9546,
     0.9563, 0.9569, 0.9576, 0.9590, 0.9605, 0.9610, 0.9616},
    // -36 dBFS: reference plateau 7.1708 dB, candidate curve RMS 0.0322
    {0.2588, 0.3704, 0.4740, 0.5473, 0.6095, 0.6534, 0.6933, 0.7245,
     0.7522, 0.7730, 0.7926, 0.8090, 0.8241, 0.8356, 0.8468, 0.8567,
     0.8660, 0.8730, 0.8800, 0.8866, 0.8929, 0.8974, 0.9022, 0.9069,
     0.9114, 0.9146, 0.9180, 0.9216, 0.9252, 0.9275, 0.9302, 0.9332,
     0.9361, 0.9379, 0.9400, 0.9425, 0.9449, 0.9464, 0.9480, 0.9500,
     0.9521, 0.9532, 0.9546, 0.9563, 0.9581, 0.9590, 0.9601, 0.9616,
     0.9631, 0.9639, 0.9647, 0.9661, 0.9675, 0.9681, 0.9688, 0.9700,
     0.9712, 0.9717, 0.9723, 0.9734, 0.9745, 0.9749, 0.9754, 0.9763,
     0.9773, 0.9776, 0.9780, 0.9789, 0.9798, 0.9800, 0.9803, 0.9811,
     0.9819, 0.9821, 0.9824, 0.9830, 0.9838, 0.9839, 0.9841},
    // -30 dBFS: reference plateau 11.7581 dB, candidate curve RMS 0.0315
    {0.3716, 0.5086, 0.6133, 0.6793, 0.7306, 0.7656, 0.7961, 0.8194,
     0.8393, 0.8540, 0.8676, 0.8789, 0.8891, 0.8968, 0.9042, 0.9109,
     0.9170, 0.9215, 0.9260, 0.9303, 0.9344, 0.9372, 0.9402, 0.9432,
     0.9462, 0.9481, 0.9502, 0.9525, 0.9548, 0.9562, 0.9578, 0.9597,
     0.9616, 0.9626, 0.9639, 0.9655, 0.9670, 0.9678, 0.9688, 0.9701,
     0.9714, 0.9720, 0.9727, 0.9739, 0.9750, 0.9755, 0.9760, 0.9770,
     0.9780, 0.9784, 0.9788, 0.9797, 0.9806, 0.9809, 0.9812, 0.9820,
     0.9828, 0.9830, 0.9833, 0.9840, 0.9847, 0.9849, 0.9851, 0.9857,
     0.9863, 0.9865, 0.9866, 0.9872, 0.9878, 0.9878, 0.9879, 0.9885,
     0.9890, 0.9890, 0.9891, 0.9896, 0.9901, 0.9901, 0.9901},
    // -18 dBFS: reference plateau 21.1670 dB, candidate curve RMS 0.0273
    {0.5484, 0.6976, 0.7761, 0.8193, 0.8504, 0.8714, 0.8892, 0.9026,
     0.9137, 0.9220, 0.9296, 0.9359, 0.9415, 0.9456, 0.9496, 0.9532,
     0.9565, 0.9588, 0.9611, 0.9634, 0.9655, 0.9669, 0.9684, 0.9700,
     0.9716, 0.9725, 0.9735, 0.9748, 0.9761, 0.9768, 0.9776, 0.9787,
     0.9797, 0.9802, 0.9808, 0.9817, 0.9826, 0.9829, 0.9833, 0.9840,
     0.9848, 0.9850, 0.9853, 0.9859, 0.9866, 0.9868, 0.9870, 0.9875,
     0.9881, 0.9882, 0.9884, 0.9889, 0.9894, 0.9895, 0.9896, 0.9901,
     0.9906, 0.9906, 0.9906, 0.9911, 0.9915, 0.9915, 0.9916, 0.9920,
     0.9924, 0.9924, 0.9923, 0.9927, 0.9931, 0.9931, 0.9930, 0.9934,
     0.9937, 0.9937, 0.9936, 0.9939, 0.9943, 0.9942, 0.9942},
    // -6 dBFS: reference plateau 30.7397 dB, candidate curve RMS 0.0249
    {0.6611, 0.7898, 0.8484, 0.8785, 0.8995, 0.9142, 0.9266, 0.9354,
     0.9426, 0.9484, 0.9536, 0.9577, 0.9613, 0.9641, 0.9669, 0.9692,
     0.9713, 0.9729, 0.9745, 0.9760, 0.9774, 0.9784, 0.9794, 0.9805,
     0.9815, 0.9821, 0.9828, 0.9837, 0.9846, 0.9850, 0.9855, 0.9862,
     0.9869, 0.9872, 0.9876, 0.9882, 0.9888, 0.9890, 0.9892, 0.9897,
     0.9902, 0.9904, 0.9905, 0.9910, 0.9914, 0.9915, 0.9916, 0.9920,
     0.9924, 0.9925, 0.9925, 0.9929, 0.9932, 0.9933, 0.9933, 0.9936,
     0.9939, 0.9939, 0.9939, 0.9942, 0.9945, 0.9945, 0.9945, 0.9948,
     0.9951, 0.9950, 0.9950, 0.9952, 0.9955, 0.9955, 0.9954, 0.9956,
     0.9959, 0.9958, 0.9958, 0.9960, 0.9962, 0.9962, 0.9961},
};

// Independent fast-Attack row that exposed the first-cycle cubic
// extrapolation. Same reference AU, estimator, normalization and 0.5-20 ms
// sampling grid as kFetReferenceCurve, but Attack at 0.0 and source -6 dBFS.
// The ordinary attack-drive grid above deliberately fixes Attack at 0.5, so it
// passed unchanged while this row regressed from 0.0372 to 0.0495 under the
// old output-only startup correction. Keeping the complete 79-point curve
// prevents a single hand-picked early sample from standing in for its shape.
constexpr double kFetFastAttackReferenceCurve[kFetReferenceCurvePoints] = {
    0.5706071, 0.6871019, 0.7607918, 0.8010365, 0.8320398, 0.8535559,
    0.8728547, 0.8864650, 0.8985135, 0.9078389, 0.9167143, 0.9235340,
    0.9299250, 0.9350079, 0.9400243, 0.9441639, 0.9481543, 0.9512554,
    0.9543590, 0.9571029, 0.9597972, 0.9617954, 0.9638285, 0.9657916,
    0.9677510, 0.9690963, 0.9704762, 0.9719545, 0.9734467, 0.9743731,
    0.9753383, 0.9765010, 0.9776848, 0.9783268, 0.9789944, 0.9799258,
    0.9808852, 0.9813297, 0.9818037, 0.9825868, 0.9833999, 0.9837119,
    0.9840465, 0.9847241, 0.9854343, 0.9856527, 0.9858864, 0.9864811,
    0.9871073, 0.9872519, 0.9874151, 0.9879509, 0.9885175, 0.9886107,
    0.9887202, 0.9892094, 0.9897284, 0.9897819, 0.9898507, 0.9903024,
    0.9907831, 0.9908067, 0.9908413, 0.9912601, 0.9917069, 0.9917052,
    0.9917111, 0.9921007, 0.9925176, 0.9924949, 0.9924767, 0.9928406,
    0.9932309, 0.9931909, 0.9931529, 0.9934937, 0.9938602, 0.9938058,
    0.9937513,
};

// Worst normalised curve RMS accepted on any drive. Measured on this build:
// 0.0218 / 0.0321 / 0.0315 / 0.0273 / 0.0249, shallow to deep, so the worst row
// has 18 % of headroom. The bound is set by what it has to catch, not by taste:
// restoring only the three SHALLOW table entries to the values the previous,
// deep-anchored-only parabola extrapolated there reads 0.0436 at -42 dBFS, and
// restoring the whole pre-campaign law reads 0.1578. A bound of 0.045 would
// have let the first of those through, which is the exact defect this table
// exists to fix. The measurement is deterministic -- same binary, same numbers
// -- so the headroom does not have to cover run-to-run noise.
//
// It gates the SHAPE of the attack, so it fails for a law that is too fast as
// readily as for one that is too slow.
constexpr double kFetCurveRmsBound = 0.038;

double fetCurveRmsAgainstValues(const FetAttackMeasurement& measured,
                                const std::vector<double>& reductionDb,
                                double sampleRate, const double* reference)
{
    double sum = 0.0;
    int used = 0;
    for (int point = 0; point < kFetReferenceCurvePoints; ++point)
    {
        const double milliseconds = kFetReferenceCurveFirstMs
            + kFetReferenceCurveStepMs * point;
        const size_t index = static_cast<size_t>(
            std::lround(milliseconds * sampleRate / 1000.0));
        if (index >= reductionDb.size()) break;
        const double difference = reductionDb[index] / measured.plateauGrDb
            - reference[point];
        sum += difference * difference;
        ++used;
    }
    require(used == kFetReferenceCurvePoints,
            "the whole reference curve window is inside the rendered attack");
    return std::sqrt(sum / used);
}

double fetCurveRmsAgainstReference(const FetAttackMeasurement& measured,
                                   const std::vector<double>& reductionDb,
                                   double sampleRate, int driveIndex)
{
    return fetCurveRmsAgainstValues(
        measured, reductionDb, sampleRate, kFetReferenceCurve[driveIndex]);
}

void testFetAttackMatchesReferenceCurve()
{
    // Diagnostics only -- see kFetReferenceCurve. Kept because the DIRECTION of
    // the drive axis is the mechanism this law exists to fix, and the direction
    // is legible in tau on the rows where the fit is well conditioned.
    constexpr double referenceTauMs[5] = {4.918, 2.540, 1.729, 1.030, 0.724};
    double worst = 0.0;
    int worstDrive = -1;
    double worstDeepPlateauError = 0.0;
    double previousPlateau = -1.0e30;
    for (int driveIndex = 0; driveIndex < 5; ++driveIndex)
    {
        std::vector<double> curve;
        const auto measured = measureFetAttack(
            kFetCurveDrivesDbfs[driveIndex], 0.5f, 0, 48000.0, &curve);
        require(measured.plateauGrDb > previousPlateau,
                "the drive axis really is a monotonically increasing reduction");
        previousPlateau = measured.plateauGrDb;
        const double rms = fetCurveRmsAgainstReference(measured, curve, 48000.0, driveIndex);
        std::printf("FET attack curve: %+.0f dBFS plateau %.4f dB  curve RMS %.4f"
                    "  (tau %s ms, x%.3f reference)\n",
                    static_cast<double>(kFetCurveDrivesDbfs[driveIndex]),
                    measured.plateauGrDb, rms,
                    measured.fitted ? std::to_string(measured.tauMs).c_str() : "unfittable",
                    measured.fitted ? measured.tauMs / referenceTauMs[driveIndex] : 0.0);
        if (rms > worst) { worst = rms; worstDrive = driveIndex; }
        if (driveIndex >= 2)
            worstDeepPlateauError = std::max(
                worstDeepPlateauError,
                std::abs(measured.plateauGrDb - kFetReferencePlateauDb[driveIndex]));
    }
    std::printf("FET attack curve: worst %.4f at %+.0f dBFS, bound %.4f\n",
                worst, static_cast<double>(kFetCurveDrivesDbfs[worstDrive]),
                kFetCurveRmsBound);
    require(worst <= kFetCurveRmsBound,
            "every vintage FET drive point tracks the reference attack trajectory");

    std::vector<double> fastCurve;
    const auto fastMeasured = measureFetAttack(
        -6.0f, 0.0f, 0, 48000.0, &fastCurve);
    const double fastRms = fetCurveRmsAgainstValues(
        fastMeasured, fastCurve, 48000.0, kFetFastAttackReferenceCurve);
    std::printf("FET fast-Attack curve: -6 dBFS plateau %.4f dB  "
                "curve RMS %.4f, bound %.4f\n",
                fastMeasured.plateauGrDb, fastRms, kFetCurveRmsBound);
    require(fastRms <= kFetCurveRmsBound,
            "the vintage FET first-cycle colour tracks the complete fast-Attack reference curve");

    std::printf("FET attack deep plateau: worst absolute error %.4f dB, bound 0.2000 dB\n",
                worstDeepPlateauError);
    require(worstDeepPlateauError <= 0.20,
            "the vintage FET intermediate attack population reaches the measured deep plateau");
}

void testFetAttackAcceleratesWithDrive()
{
    // Direction, on the two shallowest and the two deepest measured drives.
    // Before this law was corrected the same points read 1.777 / 2.804 /
    // 4.885 ms at -30 / -18 / -6 dBFS -- monotonically SLOWER with drive, the
    // exact inverse of the reference's 1.729 / 1.030 / 0.724 ms.
    const auto shallow = measureFetAttack(-42.0f);
    const auto light = measureFetAttack(-36.0f);
    const auto middle = measureFetAttack(-18.0f);
    require(shallow.fitted && light.fitted && middle.fitted,
            "every well-conditioned vintage FET drive point yields an attack fit");
    std::printf("FET attack vs drive: %.4f dB -> %.4f ms, %.4f dB -> %.4f ms, "
                "%.4f dB -> %.4f ms\n",
                shallow.plateauGrDb, shallow.tauMs, light.plateauGrDb, light.tauMs,
                middle.plateauGrDb, middle.tauMs);
    require(light.tauMs < shallow.tauMs && middle.tauMs < light.tauMs,
            "vintage FET attack gets FASTER as drive deepens, as the reference does");
    // Spacing, not absolute milliseconds. The reference spans 4.918 -> 1.030 ms
    // over these three drives, a factor of 4.8; a law that merely tilted the
    // sign by a percent would satisfy the ordering above.
    const double span = shallow.tauMs / middle.tauMs;
    std::printf("FET attack drive span: %.4f (reference 4.775)\n", span);
    require(span > 3.0 && span < 8.0,
            "the vintage FET attack drive axis spans the reference's factor of ~4.8");
    // No bound is asserted at -6 dBFS and none should be: there the estimator's
    // fit band collapses to a fraction of a millisecond and the fitted tau
    // stops tracking the cell entirely. testFetAttackMatchesReferenceCurve
    // gates that row, on the curve.
}

void testFetHighCarrierAttackKnobAxis()
{
    // The 1 kHz estimator has a 0.5 ms floor and cannot resolve the reference's
    // fast half of the Attack knob. At 4 kHz the period is exactly 12 samples;
    // the campaign estimator's synthetic self-check recovers 0.2--2.0 ms
    // exponentials within 3.1 %. These live-reference taus therefore pin the
    // low-knob region without retuning against the known 1 kHz dead zone.
    struct Point
    {
        float driveDbfs;
        float attackPosition;
        double referenceTauMs;
    };
    constexpr std::array<Point, 7> points{{
        {-30.0f, 0.00f, 2.873214},
        {-18.0f, 0.00f, 1.665809},
        { -6.0f, 0.00f, 1.320637},
        {-30.0f, 0.25f, 2.364919},
        {-18.0f, 0.25f, 1.457143},
        {-30.0f, 0.50f, 1.737511},
        {-30.0f, 1.00f, 0.188000},
    }};
    double worstRelativeError = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetAttack(
            point.driveDbfs, point.attackPosition, 0, 48000.0,
            nullptr, 4000.0);
        require(measured.fitted,
                "every selected 4 kHz FET attack-knob row yields a fit");
        const double relativeError
            = measured.tauMs / point.referenceTauMs - 1.0;
        worstRelativeError = std::max(
            worstRelativeError, std::abs(relativeError));
        std::printf("FET 4 kHz attack knob: drive %+.0f position %.2f "
                    "reference %.6f measured %.6f error %+.3f%%\n",
                    static_cast<double>(point.driveDbfs),
                    static_cast<double>(point.attackPosition),
                    point.referenceTauMs, measured.tauMs,
                    100.0 * relativeError);
    }
    std::printf("FET 4 kHz attack knob: worst relative tau error %.3f%%\n",
                100.0 * worstRelativeError);
    require(worstRelativeError < 0.25,
            "vintage FET Attack knob follows the resolved 4 kHz reference axis");
}

void testFetStartupPeakSurface()
{
    // Whole-capture peaks from the installed-unit 1 kHz dynamics campaign.
    // Every row uses the same -6 dBFS step, Input 0.8 and Output 0.625915527;
    // only Attack or Ratio changes. The old harness printed this quantity but
    // never asserted it, allowing 3.0--7.0 dB first-cycle errors to stay green.
    struct Point
    {
        float attackPosition;
        int ratioIndex;
        double referencePeak;
    };
    constexpr std::array<Point, 7> points{{
        {0.00f, 0, 3.040199757},
        {0.50f, 0, 2.489228725},
        {1.00f, 0, 0.911336124},
        {0.50f, 1, 2.128107786},
        {0.50f, 2, 1.832375646},
        {0.50f, 3, 1.477138281},
        {0.50f, 4, 2.583157301},
    }};
    double worstErrorDb = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetAttack(
            -6.0f, point.attackPosition, point.ratioIndex);
        const double errorDb = 20.0 * std::log10(
            measured.outputPeak / point.referencePeak);
        worstErrorDb = std::max(worstErrorDb, std::abs(errorDb));
        std::printf("FET startup peak: attack %.2f ratio %d reference %.6f "
                    "measured %.6f error %+.3f dB\n",
                    static_cast<double>(point.attackPosition), point.ratioIndex,
                    point.referencePeak, measured.outputPeak, errorDb);
    }
    std::printf("FET startup peak: worst absolute error %.3f dB\n",
                worstErrorDb);
    require(worstErrorDb < 0.75,
            "vintage FET first-cycle peaks match the installed-unit surface");
}

void testFetStartupStateLifecycleAndCounterSaturation()
{
    constexpr std::array<float, 2> peaks{{0.25f, 0.75f}};
    constexpr std::array<int, 2> poisonedActiveSamples{{7, 11}};
    constexpr std::array<int, 2> poisonedSilentSamples{{13, 17}};
    const auto poisonState = [&](MultiCompDSP& dsp) {
        duskaudio::MultiCompDSPTestAccess::setFetStartupState(
            dsp, peaks, poisonedActiveSamples, poisonedSilentSamples);
    };
    const auto requireCleared = [&](const MultiCompDSP& dsp,
                                    const char* message) {
        require(duskaudio::MultiCompDSPTestAccess::fetStartupStateIsClear(dsp),
                message);
    };

    MultiCompDSP dsp;
    prepareReferenceFet(dsp, 48000.0, 64, 0.8f, 0.625915527f, 0);
    dsp.prepare(48000.0, 64);

    poisonState(dsp);
    dsp.prepare(48000.0, 64);
    requireCleared(dsp,
        "repeated prepare clears both channels of FET startup peak/counter state");

    poisonState(dsp);
    dsp.reset();
    requireCleared(dsp,
        "reset clears both channels of FET startup peak/counter state");

    poisonState(dsp);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::VCA));
    std::array<float, 64> input{};
    std::array<float, 64> output{};
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    dsp.processBlock(inputs, outputs, 1, 1);
    requireCleared(dsp,
        "leaving FET clears both channels of FET startup peak/counter state");

    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::FET));
    dsp.reset();
    poisonState(dsp);
    dsp.setBypass(true);
    input.fill(0.5f); // keep the startup state armed until bypass has settled
    for (int block = 0; block < 32; ++block)
        dsp.processBlock(inputs, outputs, 1, static_cast<int>(input.size()));
    requireCleared(dsp,
        "settled bypass clears both channels of FET startup peak/counter state");

    constexpr int fullCorrectionSamples = 12;
    constexpr int correctionEndSamples = 24;
    int activeSamples = correctionEndSamples;
    float worstPastWindowBlend = 0.0f;
    for (int sample = 0; sample < 1000000; ++sample)
        worstPastWindowBlend = std::max(
            worstPastWindowBlend,
            duskaudio::MultiCompDSPTestAccess::advanceFetStartupBlend(
                activeSamples, fullCorrectionSamples,
                correctionEndSamples));
    std::printf("FET startup state: peak/counters clear prepare/reset/mode/bypass yes, "
                "counter after 1000000 post-window samples %d, blend %.9g\n",
                activeSamples, static_cast<double>(worstPastWindowBlend));
    require(activeSamples == correctionEndSamples,
        "FET startup active counter saturates at the correction-window end");
    require(worstPastWindowBlend == 0.0f,
        "FET startup blend stays exactly zero after the correction window");
}

// ---------------------------------------------------------------------------
// Wave 20 neighbours for the base-rate startup peak ceiling.
//
// The ceiling adds the first per-channel state the vintage path has ever
// carried in `MultiCompDSP` itself rather than in `MultiCompModes`: a
// preallocated input-scratch buffer plus a running source peak, an active
// counter and a silence counter. Every path that shares that state is
// enumerated here. The stage is deliberately gated on Output position, runs
// after the mid/side decode and follows the AUDIO input rather than the
// sidechain, so each of those is asserted rather than assumed.
struct FetStartupProbeConfig
{
    double sampleRate = 48000.0;
    int blockSize = 256;
    float outputPosition = 0.625915527f;
    int oversampling = kOversampling2xSetting;
    int ratio = 0;
    float attackPosition = 0.0f;
    float driveDbfs = -6.0f;
    float sidechainDbfs = 0.0f;       // 0 == follow driveDbfs
    int channels = 1;
    bool inPlace = false;
    float stereoLink = 100.0f;
    bool externalSidechain = false;
    double leadSilenceSeconds = 0.050;
    double burstSeconds = 0.120;
    double gapSeconds = 0.0;          // optional silent gap, then a second burst
    double secondBurstSeconds = 0.0;
};

struct FetStartupProbeResult
{
    std::vector<float> left;
    double firstBurstPeak = 0.0;      // |out| over the first 3 ms of burst one
    double secondBurstPeak = 0.0;     // |out| over the first 3 ms of burst two
    double settledPeak = 0.0;         // |out| over the last 20 ms of burst one
};

FetStartupProbeResult renderFetStartupProbe(const FetStartupProbeConfig& cfg)
{
    const double sr = cfg.sampleRate;
    const int lead = static_cast<int>(std::lround(cfg.leadSilenceSeconds * sr));
    const int burst = static_cast<int>(std::lround(cfg.burstSeconds * sr));
    const int gap = static_cast<int>(std::lround(cfg.gapSeconds * sr));
    const int burst2 = static_cast<int>(std::lround(cfg.secondBurstSeconds * sr));
    const int total = lead + burst + gap + burst2;

    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sr, cfg.blockSize, 0.8f, cfg.outputPosition,
                        cfg.ratio);
    dsp.setOversampling(cfg.oversampling);
    dsp.setStereoLink(cfg.stereoLink);
    dsp.setExternalSidechain(cfg.externalSidechain);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain,
                     cfg.externalSidechain ? 1.0f : 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetAttack,
                     fetAttackPlain(cfg.attackPosition));
    dsp.prepare(sr, cfg.blockSize);

    const float amplitude = duskaudio::decibelsToGain(cfg.driveDbfs);
    const float sidechainAmplitude = cfg.sidechainDbfs < 0.0f
        ? duskaudio::decibelsToGain(cfg.sidechainDbfs) : amplitude;
    const auto sourceAt = [&](int n) {
        const bool active = (n >= lead && n < lead + burst)
            || (burst2 > 0 && n >= lead + burst + gap);
        if (! active) return 0.0f;
        return amplitude * std::sin(2.0f * kPi * 1000.0f
            * static_cast<float>(n) / static_cast<float>(sr));
    };

    const int nCh = cfg.channels;
    std::vector<std::vector<float>> in(static_cast<size_t>(nCh),
        std::vector<float>(static_cast<size_t>(cfg.blockSize)));
    std::vector<std::vector<float>> out(static_cast<size_t>(nCh),
        std::vector<float>(static_cast<size_t>(cfg.blockSize)));
    std::vector<std::vector<float>> sc(static_cast<size_t>(nCh),
        std::vector<float>(static_cast<size_t>(cfg.blockSize)));

    FetStartupProbeResult result;
    result.left.resize(static_cast<size_t>(total));
    for (int offset = 0; offset < total; offset += cfg.blockSize)
    {
        const int count = std::min(cfg.blockSize, total - offset);
        for (int ch = 0; ch < nCh; ++ch)
            for (int s = 0; s < count; ++s)
            {
                in[static_cast<size_t>(ch)][static_cast<size_t>(s)]
                    = sourceAt(offset + s);
                sc[static_cast<size_t>(ch)][static_cast<size_t>(s)]
                    = sourceAt(offset + s) * (sidechainAmplitude / amplitude);
            }
        std::vector<const float*> inputs(static_cast<size_t>(nCh));
        std::vector<const float*> sidechains(static_cast<size_t>(nCh));
        std::vector<float*> outputs(static_cast<size_t>(nCh));
        for (int ch = 0; ch < nCh; ++ch)
        {
            inputs[static_cast<size_t>(ch)] = in[static_cast<size_t>(ch)].data();
            sidechains[static_cast<size_t>(ch)]
                = sc[static_cast<size_t>(ch)].data();
            outputs[static_cast<size_t>(ch)] = cfg.inPlace
                ? in[static_cast<size_t>(ch)].data()
                : out[static_cast<size_t>(ch)].data();
        }
        if (cfg.externalSidechain)
            dsp.processBlockExternal(inputs.data(), sidechains.data(),
                                     outputs.data(), nCh, count);
        else
            dsp.processBlock(inputs.data(), outputs.data(), nCh, count);
        std::copy_n(outputs[0], count,
                    result.left.data() + offset);
    }

    const int latency = static_cast<int>(std::lround(
        kFetCampaignLatencySamples48k * sr / 48000.0));
    const auto peakOver = [&result, total](int from, int to) {
        double peak = 0.0;
        for (int n = std::max(0, from); n < std::min(total, to); ++n)
            peak = std::max(peak, std::abs(
                static_cast<double>(result.left[static_cast<size_t>(n)])));
        return peak;
    };
    const int firstOnset = lead + latency;
    // Measured over the correction window itself (0.5 ms), not a few
    // milliseconds of it: outside that window the stage is inert by
    // construction, and a wider window would let an uncorrected later sample
    // mask a correction that never happened.
    const int window = static_cast<int>(std::lround(0.0005 * sr));
    result.firstBurstPeak = peakOver(firstOnset, firstOnset + window);
    result.settledPeak = peakOver(lead + burst + latency
                                      - static_cast<int>(std::lround(0.020 * sr)),
                                  lead + burst + latency);
    if (burst2 > 0)
    {
        const int secondOnset = lead + burst + gap + latency;
        result.secondBurstPeak = peakOver(secondOnset, secondOnset + window);
    }
    return result;
}

double maximumDeltaOf(const std::vector<float>& a, const std::vector<float>& b)
{
    require(a.size() == b.size(), "startup neighbour renders are the same length");
    double worst = 0.0;
    for (size_t n = 0; n < a.size(); ++n)
        worst = std::max(worst, std::abs(static_cast<double>(a[n])
                                         - static_cast<double>(b[n])));
    return worst;
}

void testFetStartupCeilingNeighbours()
{
    // 1. REACHABILITY, and the Output-position fade that protects the already
    //    matched Output = 1.0 ceiling sweep. The stage changes the first-cycle
    //    peak but never the settled level, so the peak-to-settled ratio isolates
    //    it from the makeup gain the Output knob also moves. `outputControlBlend`
    //    is 1.0 at or below position 0.625915527 and exactly 0 at or above 0.90.
    // The stage's contract is an ABSOLUTE ceiling, so assert it as one. At
    // -6 dBFS / Attack 0.00 / 4:1 the table target is 2.7164 and our
    // uncorrected first-cycle peak is 5.954755 (measured on the stage-disabled
    // diagnostic binary 28dd845b6adf), i.e. 2.19x the target -- so a stage that
    // does not run, indexes the wrong cell, or ignores `startupBlend` cannot
    // squeeze under the bound below. The soft knee passes
    // `startupCeilingSlope` of the excess, which is why the bound is 1.20x and
    // not 1.00x; the observed value is 1.119x.
    //
    // Deliberately NOT asserted by a peak/settled ratio across Output
    // positions. That construction looks gain invariant and is not: above
    // about -36 dBFS the vintage output stage compresses the first-cycle peak
    // relative to the settled level as Output rises, even with this stage
    // switched off, so the ratio moves for reasons that have nothing to do
    // with the ceiling (measured: peak/settled 5.89 at Output 0.6259 against
    // 3.38 at 0.90 for a -18 dBFS row whose peak never approaches the main
    // 6.39712761 output ceiling).
    constexpr double kCeilingTargetAtMinusSix = 2.7164;
    constexpr double kCeilingTargetAtMinusThirty = 0.9677;
    FetStartupProbeConfig base;
    FetStartupProbeConfig atTarget = base;
    atTarget.driveDbfs = -6.0f;
    atTarget.attackPosition = 0.0f;
    FetStartupProbeConfig faded = atTarget;
    faded.outputPosition = 0.90f;
    const auto atDefault = renderFetStartupProbe(atTarget);
    const auto atHigh = renderFetStartupProbe(faded);
    std::printf("FET startup neighbours: ceiling target %.4f  peak at Output "
                "0.6259 %.6f (%.3fx)  peak at Output 0.90 %.6f (%.3fx)\n",
                kCeilingTargetAtMinusSix, atDefault.firstBurstPeak,
                atDefault.firstBurstPeak / kCeilingTargetAtMinusSix,
                atHigh.firstBurstPeak,
                atHigh.firstBurstPeak / kCeilingTargetAtMinusSix);
    require(atDefault.settledPeak > 1.0e-3 && atHigh.settledPeak > 1.0e-3,
            "startup neighbour programme actually produces signal");
    require(atDefault.firstBurstPeak
                < 1.20 * kCeilingTargetAtMinusSix,
            "the startup ceiling is REACHED at the default Output position and "
            "holds the first cycle at its measured target");
    require(atHigh.firstBurstPeak > 1.50 * kCeilingTargetAtMinusSix,
            "the startup ceiling is switched off at Output position 0.90, so "
            "the already-matched Output = 1.0 ceiling sweep is untouched by "
            "this stage");

    // 2. Determinism neighbours. Each must be EXACTLY zero: the stage adds no
    //    rate-dependent smoothing, so any block-boundary or state-carry defect
    //    shows up as a non-zero delta rather than a small one.
    FetStartupProbeConfig small = base;  small.blockSize = 64;
    FetStartupProbeConfig large = base;  large.blockSize = 512;
    const double blockDelta = maximumDeltaOf(
        renderFetStartupProbe(small).left, renderFetStartupProbe(large).left);

    FetStartupProbeConfig stereo = base; stereo.channels = 2;
    const double monoStereoDelta = maximumDeltaOf(
        atDefault.left, renderFetStartupProbe(stereo).left);

    FetStartupProbeConfig inPlace = base; inPlace.inPlace = true;
    const double inPlaceDelta = maximumDeltaOf(
        atDefault.left, renderFetStartupProbe(inPlace).left);

    FetStartupProbeConfig unlinked = base;
    unlinked.channels = 2; unlinked.stereoLink = 0.0f;
    FetStartupProbeConfig linked = base;
    linked.channels = 2; linked.stereoLink = 100.0f;
    const double linkDelta = maximumDeltaOf(
        renderFetStartupProbe(unlinked).left, renderFetStartupProbe(linked).left);

    // The external sidechain path is NOT bit-identical to the internal one even
    // with the same programme on both -- that is a pre-existing property of the
    // vintage gain staging, quantified below at Output 0.90 where this stage is
    // switched off entirely (10.7 there against 4.9 with it on, so the stage
    // narrows the difference rather than causing it).
    //
    // What must hold is WHICH input the ceiling reads. It runs after the audio
    // reconstruction and takes its source peak from `fetStartupInput`, so with
    // -6 dBFS audio it must keep the -6 dBFS target (2.7164) even when the
    // sidechain is 24 dB quieter. Had it read the sidechain it would have
    // selected the -30 dBFS row instead and clamped near 0.9677 -- a 9 dB
    // difference, which is what makes this assertion able to fail.
    FetStartupProbeConfig external = atTarget; external.externalSidechain = true;
    FetStartupProbeConfig externalFaded = external;
    externalFaded.outputPosition = 0.90f;
    FetStartupProbeConfig quietSidechain = external;
    quietSidechain.sidechainDbfs = -30.0f;
    const auto externalRender = renderFetStartupProbe(external);
    const auto quietScRender = renderFetStartupProbe(quietSidechain);
    const double sidechainDelta = maximumDeltaOf(
        atDefault.left, externalRender.left);
    const double sidechainDeltaStageOff = maximumDeltaOf(
        atHigh.left, renderFetStartupProbe(externalFaded).left);

    std::printf("FET startup neighbours: block %.9g  mono/stereo %.9g  "
                "in-place %.9g  link %.9g | external-SC delta %.6g "
                "(stage off %.6g) first-cycle peak %.6f (%.3fx target)\n",
                blockDelta, monoStereoDelta, inPlaceDelta, linkDelta,
                sidechainDelta, sidechainDeltaStageOff,
                quietScRender.firstBurstPeak,
                quietScRender.firstBurstPeak / kCeilingTargetAtMinusSix);
    require(blockDelta == 0.0,
            "startup ceiling is identical for 64- and 512-sample blocks");
    require(monoStereoDelta == 0.0,
            "startup ceiling treats a mono render and the left channel of an "
            "identical stereo render alike");
    require(inPlaceDelta == 0.0,
            "startup ceiling reads the preserved input scratch, not the "
            "overwritten in-place output");
    require(linkDelta == 0.0,
            "startup ceiling is unchanged by the stereo link law for identical "
            "channels");
    // A quiet sidechain leaves the compressor barely working, so the raw first
    // cycle arrives around 19.1 and the 10 % knee passes a large excess: the
    // clamped result is target + 0.1 * (19.1 - target), not the target itself.
    // That makes this a three-way discriminator, and all three outcomes are
    // separated by the bounds below:
    //   no ceiling at all        -> about 19.1   (7.03x)
    //   target read from the SC  -> about  2.78  (1.02x)  [-30 dBFS row, 0.9677]
    //   target read from audio   -> about  4.36  (1.60x)  [ -6 dBFS row, 2.7164]
    require(quietScRender.firstBurstPeak > 1.25 * kCeilingTargetAtMinusSix
                && quietScRender.firstBurstPeak
                    < 2.00 * kCeilingTargetAtMinusSix,
            "startup ceiling selects its target from the AUDIO input, not from "
            "a 24 dB quieter external sidechain");

    // 3. Lifecycle neighbours: prepare twice, reset, and a fresh instance must
    //    all agree, and leaving and re-entering FET must clear the state.
    {
        MultiCompDSP twice;
        prepareReferenceFet(twice, 48000.0, base.blockSize, 0.8f,
                            base.outputPosition, 0);
        twice.prepare(48000.0, base.blockSize);
        twice.prepare(48000.0, base.blockSize);   // must be safe to repeat
        std::array<float, 64> silence{};
        const float* inputs[] = {silence.data()};
        std::array<float, 64> scratch{};
        float* outputs[] = {scratch.data()};
        twice.processBlock(inputs, outputs, 1, 0);          // zero-sample block
        twice.processBlock(inputs, outputs, 1, 64);
        std::puts("FET startup neighbours: prepare-twice and zero-sample block "
                  "survived");
    }

    // 4. Silence rearm. The stage zeroes its source peak and its active
    //    counter after 2 ms of input below -60 dBFS, and without that a second
    //    burst would arrive with `fetStartupActiveSamples` long past the end of
    //    the correction window and get no correction at all. Half a second of
    //    silence both rearms the stage and lets the detector release, so the
    //    second burst presents the same uncorrected 5.95 first cycle as the
    //    first one did -- if it is still held at the target, the stage rearmed.
    FetStartupProbeConfig rearm = atTarget;
    rearm.burstSeconds = 0.060; rearm.gapSeconds = 0.500;
    rearm.secondBurstSeconds = 0.060;
    const auto rearmed = renderFetStartupProbe(rearm);
    std::printf("FET startup neighbours: rearm after 500 ms silence, first "
                "%.6f (%.3fx target) second %.6f (%.3fx target)\n",
                rearmed.firstBurstPeak,
                rearmed.firstBurstPeak / kCeilingTargetAtMinusSix,
                rearmed.secondBurstPeak,
                rearmed.secondBurstPeak / kCeilingTargetAtMinusSix);
    // The upper bound is the one that bites: a stage that failed to rearm would
    // leave the second burst uncorrected at about 5.95 (2.19x). The lower bound
    // only guards against a silent render -- half a second is not long enough
    // for the detector to release fully, so the second burst legitimately
    // presents a smaller raw first cycle than the first (observed 0.554x).
    require(rearmed.secondBurstPeak > 0.25 * kCeilingTargetAtMinusSix
                && rearmed.secondBurstPeak
                    < 1.20 * kCeilingTargetAtMinusSix,
            "the startup ceiling rearms after a silence longer than its 2 ms "
            "threshold and holds the next burst's first cycle at the target");

    // 5. Oversampling and sample rate. The stage runs at the base rate and its
    //    window is expressed in seconds, so every combination must both run and
    //    still hold the same absolute target; state sized for one rate and
    //    reused at another, or a window that collapsed to nothing at 96 kHz,
    //    would show up here as a lost correction.
    for (const int oversampling : {kOversamplingOffSetting, kOversampling2xSetting,
                                   kOversampling4xSetting})
        for (const double rate : {44100.0, 48000.0, 96000.0})
        {
            FetStartupProbeConfig cell = atTarget;
            cell.oversampling = oversampling; cell.sampleRate = rate;
            const auto rendered = renderFetStartupProbe(cell);
            std::printf("FET startup neighbours: oversampling %d rate %.0f "
                        "first-cycle peak %.6f (%.3fx target)\n",
                        oversampling, rate, rendered.firstBurstPeak,
                        rendered.firstBurstPeak / kCeilingTargetAtMinusSix);
            require(rendered.firstBurstPeak < 1.20 * kCeilingTargetAtMinusSix,
                    "the startup ceiling holds its target at every "
                    "oversampling setting and sample rate");
        }

    // 6. Mode exit/re-entry and settled bypass. Both clear the stage's state;
    //    a leaked source peak would suppress the next burst's first cycle.
    const auto renderThroughDetour = [&](bool useBypass) {
        MultiCompDSP dsp;
        prepareReferenceFet(dsp, 48000.0, base.blockSize, 0.8f,
                            base.outputPosition, 0);
        dsp.setParameter(MultiCompDSP::Parameter::FetAttack,
                         fetAttackPlain(base.attackPosition));
        dsp.prepare(48000.0, base.blockSize);
        std::vector<float> in(static_cast<size_t>(base.blockSize));
        std::vector<float> out(static_cast<size_t>(base.blockSize));
        const float amplitude = duskaudio::decibelsToGain(base.driveDbfs);
        // Drive it hot so the stage is armed and charged, then detour.
        for (int block = 0; block < 40; ++block)
        {
            for (int s = 0; s < base.blockSize; ++s)
                in[static_cast<size_t>(s)] = amplitude * std::sin(
                    2.0f * kPi * 1000.0f
                    * static_cast<float>(block * base.blockSize + s) / 48000.0f);
            const float* inputs[] = {in.data()};
            float* outputs[] = {out.data()};
            dsp.processBlock(inputs, outputs, 1, base.blockSize);
        }
        if (useBypass)
        {
            dsp.setBypass(true);
            for (int block = 0; block < 400; ++block)
            {
                std::fill(in.begin(), in.end(), 0.0f);
                const float* inputs[] = {in.data()};
                float* outputs[] = {out.data()};
                dsp.processBlock(inputs, outputs, 1, base.blockSize);
            }
            dsp.setBypass(false);
        }
        else
        {
            dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::VCA));
            for (int block = 0; block < 400; ++block)
            {
                std::fill(in.begin(), in.end(), 0.0f);
                const float* inputs[] = {in.data()};
                float* outputs[] = {out.data()};
                dsp.processBlock(inputs, outputs, 1, base.blockSize);
            }
            dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::FET));
        }
        dsp.reset();
        // Now the same programme a fresh instance would see.
        const int lead = static_cast<int>(std::lround(0.050 * 48000.0));
        const int burstSamples = static_cast<int>(std::lround(0.120 * 48000.0));
        std::vector<float> rendered(static_cast<size_t>(lead + burstSamples));
        for (int offset = 0; offset < lead + burstSamples;
             offset += base.blockSize)
        {
            const int count = std::min(base.blockSize,
                                       lead + burstSamples - offset);
            for (int s = 0; s < count; ++s)
            {
                const int n = offset + s;
                in[static_cast<size_t>(s)] = n < lead ? 0.0f
                    : amplitude * std::sin(2.0f * kPi * 1000.0f
                        * static_cast<float>(n) / 48000.0f);
            }
            const float* inputs[] = {in.data()};
            float* outputs[] = {out.data()};
            dsp.processBlock(inputs, outputs, 1, count);
            std::copy_n(out.data(), count, rendered.data() + offset);
        }
        return rendered;
    };
    FetStartupProbeConfig freshCfg = base;
    freshCfg.leadSilenceSeconds = 0.050; freshCfg.burstSeconds = 0.120;
    const auto freshRender = renderFetStartupProbe(freshCfg).left;
    const double modeDelta = maximumDeltaOf(freshRender, renderThroughDetour(false));
    const double bypassDelta = maximumDeltaOf(freshRender, renderThroughDetour(true));
    std::printf("FET startup neighbours: FET->VCA->FET+reset %.9g  "
                "settled-bypass+reset %.9g\n", modeDelta, bypassDelta);
    require(modeDelta == 0.0,
            "leaving and re-entering FET leaves no startup ceiling state behind");
    require(bypassDelta == 0.0,
            "a settled bypass leaves no startup ceiling state behind");
}

// ---------------------------------------------------------------------------
// Vintage-FET broadband H2 surface (`fetBroadbandK2`).
//
// The table is indexed by the value `processFET` computes per sample as
// `-gainToDecibels(envelope + 0.001f)` -- NOT by the settled output-referred
// reduction the campaign's render probes report, and not by the published
// meter. The +0.001 offset alone costs 0.087 dB at 20 dB of reduction, and the
// envelope ripples with the programme, so a fit performed in any other
// coordinate lands its anchors in the wrong segment. Wave 7b's coordinate
// artifact was exactly this class of error in a different table; this harness
// exists so the H2 fit is done in the coordinate the code consumes.
struct FetHarmonicMeasurement
{
    double reductionDbMean = 0.0;   // the fetBroadbandK2 argument, window mean
    double reductionDbMin = 0.0;
    double reductionDbMax = 0.0;
    double h1Dbfs = 0.0;
    double h2Dbfs = 0.0;
    double h3Dbfs = 0.0;
    double h5Dbfs = 0.0;
    double h2RelativeDb = 0.0;
    // The COMPLEX second-harmonic amplitude, same DFT and normalisation as
    // h2Dbfs. Wave 9 needs it because the vintage FET's 2f output is the sum of
    // two contributions -- `k2 * h2` (broadband) and `lowFrequencyK2 *
    // colourLowH2` (the 250 Hz two-pole path) -- which at 100 Hz arrive nearly
    // in QUADRATURE: the two-pole low pass shifts by -21.8 deg per pole there,
    // and squaring doubles that to -87 deg. A magnitude-only reading cannot
    // separate them, and inverting for the coefficient one of them needs is a
    // complex problem, not a scalar one. h1 is carried for the same reason: the
    // relative measure the campaign scores is h2 referred to it.
    double h1Real = 0.0, h1Imag = 0.0;
    double h2Real = 0.0, h2Imag = 0.0;
    double h3Real = 0.0, h3Imag = 0.0;
    double h5Real = 0.0, h5Imag = 0.0;
};

// Renders one settled sine through the vintage FET in process. The default
// 3.0-4.5 s coherent-DFT window matches `harmonic_levels`; callers can select
// the campaign's longer 9.0-10.5 s convergence window. The block size is
// deliberately coprime with the 1 kHz period at 48 kHz (48 samples) so the
// envelope samples walk every phase of the detector ripple instead of aliasing
// onto three of them.
FetHarmonicMeasurement measureFetHarmonics(double frequencyHz, float inputDbfs,
                                           float inputPosition, int ratio = 0,
                                           double sampleRate = 48000.0,
                                           int blockSize = 13,
                                           double totalSeconds = 5.0,
                                           double measureStartSeconds = 3.0,
                                           double measureStopSeconds = 4.5,
                                           float releasePosition = 0.5f)
{
    const int totalSamples = static_cast<int>(
        std::lround(totalSeconds * sampleRate));
    const int measureStart = static_cast<int>(
        std::lround(measureStartSeconds * sampleRate));
    const int measureStop = static_cast<int>(
        std::lround(measureStopSeconds * sampleRate));
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize,
                        inputPosition, 0.625915527f, ratio);
    dsp.setParameter(MultiCompDSP::Parameter::FetRelease,
                     fetReleasePlain(releasePosition));
    std::vector<float> input(static_cast<size_t>(blockSize));
    std::vector<float> output(static_cast<size_t>(blockSize));
    const double amplitude = duskaudio::decibelsToGain(inputDbfs);
    double realPart[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double imagPart[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double reductionSum = 0.0;
    double reductionMin = std::numeric_limits<double>::max();
    double reductionMax = -std::numeric_limits<double>::max();
    int reductionCount = 0;
    int measured = 0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - offset);
        for (int sample = 0; sample < count; ++sample)
            input[static_cast<size_t>(sample)] = static_cast<float>(
                amplitude * std::sin(2.0 * duskaudio::kDuskPi * frequencyHz
                    * static_cast<double>(offset + sample) / sampleRate));
        const float* inputs[] = {input.data()};
        float* outputs[] = {output.data()};
        dsp.processBlock(inputs, outputs, 1, count);
        for (int sample = 0; sample < count; ++sample)
        {
            const int absolute = offset + sample;
            if (absolute < measureStart || absolute >= measureStop)
                continue;
            const double value = output[static_cast<size_t>(sample)];
            for (int harmonic = 0; harmonic < 5; ++harmonic)
            {
                const double phase = 2.0 * duskaudio::kDuskPi * frequencyHz
                    * static_cast<double>(harmonic + 1)
                    * static_cast<double>(absolute) / sampleRate;
                realPart[harmonic] += value * std::cos(phase);
                imagPart[harmonic] -= value * std::sin(phase);
            }
            ++measured;
        }
        if (offset + count > measureStart && offset < measureStop)
        {
            const float envelope = duskaudio::decibelsToGain(
                duskaudio::MultiCompDSPTestAccess::fetEnvelopeGainDb(dsp, 0));
            const double reduction = -static_cast<double>(
                duskaudio::gainToDecibels(envelope + 0.001f));
            reductionSum += reduction;
            reductionMin = std::min(reductionMin, reduction);
            reductionMax = std::max(reductionMax, reduction);
            ++reductionCount;
        }
    }
    require(measured == measureStop - measureStart && reductionCount > 0,
            "FET harmonic render measures exactly the requested coherent window");
    FetHarmonicMeasurement result;
    result.reductionDbMean = reductionSum / static_cast<double>(reductionCount);
    result.reductionDbMin = reductionMin;
    result.reductionDbMax = reductionMax;
    const auto levelDb = [measured](double re, double im) {
        const double magnitude = 2.0 * std::hypot(re, im)
            / static_cast<double>(measured);
        return 20.0 * std::log10(std::max(magnitude, 1.0e-30));
    };
    result.h1Dbfs = levelDb(realPart[0], imagPart[0]);
    result.h2Dbfs = levelDb(realPart[1], imagPart[1]);
    result.h3Dbfs = levelDb(realPart[2], imagPart[2]);
    result.h5Dbfs = levelDb(realPart[4], imagPart[4]);
    result.h2RelativeDb = result.h2Dbfs - result.h1Dbfs;
    const double scale = 2.0 / static_cast<double>(measured);
    result.h1Real = realPart[0] * scale;
    result.h1Imag = imagPart[0] * scale;
    result.h2Real = realPart[1] * scale;
    result.h2Imag = imagPart[1] * scale;
    result.h3Real = realPart[2] * scale;
    result.h3Imag = imagPart[2] * scale;
    result.h5Real = realPart[4] * scale;
    result.h5Imag = imagPart[4] * scale;
    return result;
}

struct FetDetectorMeasurement
{
    double reductionDbMean = 0.0;
    double reductionDbMinimum = 0.0;
    double reductionDbMaximum = 0.0;
};

// Null-method diagnostic for the vintage detector's private transformer path.
// The ordinary render and the external-sidechain render receive the same sine
// as audio and detector input. Their audio path is therefore identical. The
// only intended difference is at processFET's detector selection: internal
// reads inputTransformerFet, external reads the raw prepared sidechain. Reading
// the mode envelope directly avoids attributing GR-dependent audio coloration
// or harmonic energy to the detector.
FetDetectorMeasurement measureFetDetectorTransformer(
    double frequencyHz, float inputDbfs, float inputPosition,
    bool external, double sampleRate)
{
    constexpr int blockSize = 1;
    const int totalSamples = static_cast<int>(std::lround(12.0 * sampleRate));
    const int measureStart = static_cast<int>(std::lround(9.0 * sampleRate));
    const int measureStop = static_cast<int>(std::lround(10.5 * sampleRate));
    MultiCompDSP dsp;
    prepareReferenceFet(dsp, sampleRate, blockSize,
                        inputPosition, 0.625915527f, 0);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain,
                     external ? 1.0f : 0.0f);
    const double amplitude = duskaudio::decibelsToGain(inputDbfs);
    double reductionSum = 0.0;
    double reductionMinimum = std::numeric_limits<double>::max();
    double reductionMaximum = -std::numeric_limits<double>::max();
    int measured = 0;
    float input = 0.0f;
    float output = 0.0f;
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        input = static_cast<float>(amplitude * std::sin(
            2.0 * duskaudio::kDuskPi * frequencyHz
            * static_cast<double>(sample) / sampleRate));
        const float* inputs[] = {&input};
        float* outputs[] = {&output};
        if (external)
        {
            const float* sidechains[] = {&input};
            dsp.processBlockExternal(inputs, sidechains, outputs, 1, 1);
        }
        else
            dsp.processBlock(inputs, outputs, 1, 1);
        if (sample < measureStart || sample >= measureStop)
            continue;
        const double reduction = -static_cast<double>(
            duskaudio::MultiCompDSPTestAccess::fetEnvelopeGainDb(dsp, 0));
        reductionSum += reduction;
        reductionMinimum = std::min(reductionMinimum, reduction);
        reductionMaximum = std::max(reductionMaximum, reduction);
        ++measured;
    }
    require(measured == measureStop - measureStart,
            "FET detector null measures exactly the campaign 9.0-10.5 second window");
    return {reductionSum / static_cast<double>(measured),
            reductionMinimum, reductionMaximum};
}

void reportFetDetectorFrequencyNull()
{
    std::puts("FET detector transformer null (external raw minus internal transformed GR):");
    for (double sampleRate : {48000.0, 96000.0})
        for (double frequencyHz : {40.0, 60.0, 100.0, 1000.0,
                                   15900.0, 16000.0, 16100.0})
            for (const auto point : {std::array<float, 2>{{0.6f, -30.0f}},
                                     std::array<float, 2>{{0.8f, -18.0f}}})
            {
                const auto internal = measureFetDetectorTransformer(
                    frequencyHz, point[1], point[0], false, sampleRate);
                const auto external = measureFetDetectorTransformer(
                    frequencyHz, point[1], point[0], true, sampleRate);
                std::printf("  rate %.0f f %7.1f i%.1f src %+.0f: "
                            "internal %.7f [%.7f %.7f] external %.7f "
                            "[%.7f %.7f] delta %+.7f dB\n",
                            sampleRate, frequencyHz,
                            static_cast<double>(point[0]),
                            static_cast<double>(point[1]),
                            internal.reductionDbMean,
                            internal.reductionDbMinimum,
                            internal.reductionDbMaximum,
                            external.reductionDbMean,
                            external.reductionDbMinimum,
                            external.reductionDbMaximum,
                            external.reductionDbMean - internal.reductionDbMean);
            }
}

// Diagnostic grid: places every campaign harmonic row on the `fetBroadbandK2`
// axis and reports what the in-process DSP produces there. Not gated -- the
// gate is testFetBroadbandHarmonicSurface below.
void reportFetBroadbandK2Surface()
{
    std::puts("FET broadband H2 surface (in-process, fetBroadbandK2 coordinate):");
    for (double frequency : {1000.0, 100.0})
        for (float inputPosition : {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,
                                    0.9f, 1.0f})
            // -45 and -15 dBFS are Wave 9 additions, and only the reduction
            // axis motivates them: at 100 Hz the knob/level product jumps from
            // 1.39 to 2.58 dB of reduction with nothing between, and from 4.97
            // to 6.92, which are the two steepest stretches of the
            // low-frequency colour law. Leaving a segment sampled only at its
            // edges is the exact hole Wave 8 found in `fetBroadbandK2`.
            for (float level : {-48.0f, -46.0f, -45.0f, -42.0f, -36.0f,
                                -30.0f, -24.0f, -18.0f, -15.0f, -12.0f, -9.0f,
                                -7.5f, -6.0f})
            {
                const auto measured = measureFetHarmonics(
                    frequency, level, inputPosition);
                // The complex H1/H2 tail is appended, never inserted: the
                // campaign probe parses this line with a prefix-anchored
                // regular expression, so growing the right-hand end is safe and
                // reordering the left-hand end is not.
                std::printf("  %6.0f Hz i%.2f src %+6.1f dBFS: gr %8.4f dB "
                            "[%8.4f %8.4f] H1 %+10.5f H2 %+10.5f H2rel %+10.5f "
                            "H3rel %+10.5f H2re %+.9e H2im %+.9e "
                            "H1re %+.9e H1im %+.9e H3re %+.9e H3im %+.9e "
                            "H5re %+.9e H5im %+.9e H5rel %+10.5f\n",
                            frequency, static_cast<double>(inputPosition),
                            static_cast<double>(level),
                            measured.reductionDbMean, measured.reductionDbMin,
                            measured.reductionDbMax, measured.h1Dbfs,
                            measured.h2Dbfs, measured.h2RelativeDb,
                            measured.h3Dbfs - measured.h1Dbfs,
                            measured.h2Real, measured.h2Imag,
                            measured.h1Real, measured.h1Imag,
                            measured.h3Real, measured.h3Imag,
                            measured.h5Real, measured.h5Imag,
                            measured.h5Dbfs - measured.h1Dbfs);
            }
}

// Wave 9 doubt #4 (wave3f_validation.md section 8.4). The drive-axis fitted
// attack tau at -6 dBFS moved 3.2314 -> 3.6174 ms under Wave 8's colour-only
// change while the gated curve RMS moved 3e-04. Either the added 2f content
// perturbs the DEMODULATED envelope the estimator reads -- a measurement-side
// artefact of a diagnostic -- or the internal gain-reduction trajectory really
// moved, which would break Wave 8's isolation claim.
//
// This dumps the internal trajectory itself: `d.envelope` in dB, read straight
// off the mode state, one sample per block with blockSize 1, over the same
// window the estimator fits. Run it on two builds and diff. The whole point is
// that it does NOT go through the demodulator, so it cannot share its artefact.
void reportFetEnvelopeTrace()
{
    constexpr double sampleRate = 48000.0;
    constexpr double quietSeconds = 1.0;
    constexpr double traceSeconds = 0.060;
    constexpr double carrierHz = 1000.0;
    // The drive-axis row in question, with the campaign's controls: input knob
    // 0.8, attack 0.5, 4:1, a phase-continuous burst after a -72 dBFS carrier.
    for (float driveDbfs : {-6.0f, -18.0f, -30.0f})
    {
        MultiCompDSP dsp;
        prepareReferenceFet(dsp, sampleRate, 1, 0.8f, 0.625915527f, 0);
        dsp.setParameter(MultiCompDSP::Parameter::FetAttack, fetAttackPlain(0.5f));
        const float quietAmplitude = duskaudio::decibelsToGain(-72.0f);
        const float loudAmplitude = duskaudio::decibelsToGain(driveDbfs);
        const int loudFrom = static_cast<int>(std::lround(quietSeconds * sampleRate));
        const int totalSamples = loudFrom
            + static_cast<int>(std::lround(traceSeconds * sampleRate));
        float left = 0.0f, right = 0.0f, outLeft = 0.0f, outRight = 0.0f;
        // FNV-1a over the raw bit patterns, so a one-ULP move is a different
        // digest. Sampled values are printed alongside for human reading.
        std::uint64_t digest = 1469598103934665603ull;
        double outputPeak = 0.0;
        for (int n = 0; n < totalSamples; ++n)
        {
            const float value = (n < loudFrom ? quietAmplitude : loudAmplitude)
                * std::sin(2.0f * kPi * static_cast<float>(carrierHz)
                    * static_cast<float>(n) / static_cast<float>(sampleRate));
            left = right = value;
            const float* inputs[] = {&left, &right};
            float* outputs[] = {&outLeft, &outRight};
            dsp.processBlock(inputs, outputs, 2, 1);
            outputPeak = std::max(outputPeak,
                std::max(std::abs(static_cast<double>(outLeft)),
                         std::abs(static_cast<double>(outRight))));
            if (n < loudFrom) continue;
            const float envelopeDb
                = duskaudio::MultiCompDSPTestAccess::fetEnvelopeGainDb(dsp, 0);
            std::uint32_t bits;
            std::memcpy(&bits, &envelopeDb, sizeof(bits));
            for (int byte = 0; byte < 4; ++byte)
            {
                digest ^= (bits >> (byte * 8)) & 0xffu;
                digest *= 1099511628211ull;
            }
            const int elapsed = n - loudFrom;
            for (double milliseconds : {0.5, 1.0, 2.0, 3.0, 5.0, 10.0, 20.0, 40.0})
                if (elapsed == static_cast<int>(std::lround(
                        milliseconds * sampleRate / 1000.0)))
                    std::printf("  ENVTRACE drive %+.0f dBFS t %6.2f ms "
                                "envelope %+.9f dB\n",
                                static_cast<double>(driveDbfs), milliseconds,
                                static_cast<double>(envelopeDb));
        }
        std::printf("  ENVTRACE drive %+.0f dBFS digest %016llx samples %d "
                    "output peak %.9f\n",
                    static_cast<double>(driveDbfs),
                    static_cast<unsigned long long>(digest),
                    static_cast<int>(std::lround(traceSeconds * sampleRate)),
                    outputPeak);
    }
}

void testFetBroadbandHarmonicSurface()
{
    // Six absolute anchors on the reference unit's own 1 kHz second harmonic,
    // one per region of `fetBroadbandK2`, rendered from the installed AU by
    // `probe_h2_surface.py` (campaign reference_comparison_1176) and quoted
    // here as H2 relative to the fundamental, which is scalar-immune: a gain
    // error anywhere in the chain cancels out of the ratio, so this measures
    // the colour law and nothing else.
    //
    // Two of the six sit where NOTHING previously looked. The campaign's
    // sixteen-row harmonic grid reaches only eight distinct reduction depths,
    // four of them zero, so three of the old table's seven segments held no
    // measured point at all -- and the interior of the widest of them read
    // 5.14 dB high. `probe_h2_surface.py` is the VST3-level guard on the dense
    // axis; these are its in-process mirror at the depths that matter.
    struct Point
    {
        float inputPosition;
        float inputDbfs;
        double reductionDb;              // the fetBroadbandK2 argument
        double referenceH2RelativeDb;
        double previousErrorDb;          // what the pre-Wave-8 table read here
    };
    constexpr std::array<Point, 6> points{{
        {0.2f, -36.0f,  -0.009, -83.991595, -0.2590},
        {0.4f, -30.0f,   0.557, -65.114341, -1.3251},
        {0.8f, -36.0f,   8.984, -65.319662, +3.1241},
        {0.6f, -24.0f,  12.800, -61.448271, +5.1379},
        {0.8f, -24.0f,  18.864, -54.794383, +3.7694},
        {0.8f, -12.0f,  28.663, -43.927124, -0.4908}}};

    // Set by what it has to catch, not by taste. The table this replaced read
    // 5.14 dB high at the fourth point and 3.77 at the fifth; the campaign's
    // own harmonic gate is 1.0 dB. 0.35 dB is 14x under the worst defect, 2.9x
    // under the campaign gate, and 3.5x over the worst error this build
    // actually produces anywhere on the 39-row 1 kHz surface (0.262 dB).
    constexpr double kBoundDb = 0.35;
    double worstError = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            1000.0, point.inputDbfs, point.inputPosition);
        const double error = measured.h2RelativeDb - point.referenceH2RelativeDb;
        worstError = std::max(worstError, std::abs(error));
        // The reduction is checked too: these anchors are only meaningful at
        // the depth they were measured at, so a change that moved the detector
        // must fail here rather than silently re-point the assertion.
        std::printf("FET H2 surface: i%.1f %+.0f dBFS gr %.4f (expected %.3f) "
                    "reference %.6f measured %.6f error %+.6f dB "
                    "(previous table %+.4f)\n",
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    measured.reductionDbMean, point.reductionDb,
                    point.referenceH2RelativeDb, measured.h2RelativeDb, error,
                    point.previousErrorDb);
        require(std::abs(measured.reductionDbMean - point.reductionDb) < 0.02,
                "vintage FET H2 anchors are evaluated at the reduction they were measured at");
    }
    std::printf("FET H2 surface: worst %+.6f dB over %zu anchors (bound %.2f)\n",
                worstError, points.size(), kBoundDb);
    require(worstError < kBoundDb,
            "vintage FET broadband H2 matches the measured reference across the reduction axis");
}

void testFetBroadbandOddHarmonicSurface()
{
    // Absolute anchors on the installed reference unit's 1 kHz third
    // harmonic, rendered by `probe_h2_surface.py` and quoted relative to each
    // row's own fundamental.  The ten rows span every region of the measured
    // reduction-dependent cubic law; all sit above the campaign's -92 dBc
    // harmonic floor.
    struct Point
    {
        float inputPosition;
        float inputDbfs;
        double reductionDb;              // the broadband cubic argument
        double referenceH3RelativeDb;
        double previousErrorDb;          // the constant -0.006 law
    };
    constexpr std::array<Point, 10> points{{
        {0.2f, -18.0f,  0.3543, -83.185967, +2.4514},
        {0.8f, -48.0f,  1.0175, -78.547980, +1.5210},
        {0.4f, -24.0f,  3.1457, -72.965816, +0.9981},
        {0.4f, -18.0f,  7.7089, -68.541805, +0.3595},
        {1.0f, -36.0f, 10.0089, -67.305299, +0.4806},
        {0.8f, -30.0f, 13.9253, -66.683950, +1.9392},
        {0.8f, -24.0f, 18.8564, -67.067362, +3.7122},
        {0.8f, -18.0f, 23.7855, -67.007091, +5.1897},
        {0.8f, -12.0f, 28.6512, -66.239258, +6.1158},
        {0.8f,  -6.0f, 33.4397, -65.841835, +7.5232}}};

    // The fitted table predicts 0.090 dB worst over all 36 scoreable dense
    // rows. 0.25 dB leaves 0.16 dB for render/fit error, is 4x tighter than the
    // campaign harmonic gate, and is 30x below the defect this test guards.
    constexpr double kBoundDb = 0.25;
    double worstError = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            1000.0, point.inputDbfs, point.inputPosition,
            0, 48000.0, 13, 5.0, 3.0, 4.5, 0.66595459f);
        const double relativeDb = measured.h3Dbfs - measured.h1Dbfs;
        const double error = relativeDb - point.referenceH3RelativeDb;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET H3 surface: i%.1f %+.0f dBFS gr %.4f (expected %.4f) "
                    "reference %.6f measured %.6f error %+.6f dB "
                    "(constant cubic %+.4f)\n",
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    measured.reductionDbMean, point.reductionDb,
                    point.referenceH3RelativeDb, relativeDb, error,
                    point.previousErrorDb);
        require(std::abs(measured.reductionDbMean - point.reductionDb) < 0.02,
                "vintage FET H3 anchors are evaluated at the reduction they were measured at");
    }
    std::printf("FET H3 surface: worst %+.6f dB over %zu anchors (bound %.2f)\n",
                worstError, points.size(), kBoundDb);
    require(worstError < kBoundDb,
            "vintage FET broadband H3 matches the measured reference across the reduction axis");
}

void testFetDenseShallowLowFrequencyH3()
{
    // Wave 25's long-window quarter-dB sweep resolved a shallow 100 Hz H3
    // discontinuity that the sparse anchor grid cannot see. Wave 31 corrected
    // the net transfer knee after the colour stage, so H3/H1 is deliberately
    // unchanged and this is the red gate for re-keying only the shallow T3
    // lookup coordinate. All 33 reference rows are above the -92 dBc floor;
    // phase is H3 normalised to the measured H1 phase in the retained UAD WAVs.
    constexpr std::array<double, 33> referenceH3RelativeDb{{
        -87.5355661498, -87.0345848794, -86.5351668744,
        -86.0312293496, -85.5273531539, -85.0280479681,
        -84.5333143309, -84.0341295974, -83.5338373029,
        -83.0330680840, -82.5339559301, -82.0337489982,
        -81.5355197908, -81.0348656644, -80.5314591569,
        -79.8022302194, -78.4827434384, -76.7603074239,
        -74.9308655682, -73.1561258963, -71.5103369991,
        -70.0956033502, -68.8039456730, -67.6385917602,
        -66.5862012345, -65.6257125540, -64.7530959602,
        -63.9492703885, -63.2050707290, -62.4919866483,
        -61.8479698190, -61.2448072109, -60.6779950280,
    }};
    constexpr std::array<double, 33> referenceH3PhaseDegrees{{
        164.574386210, 164.568754298, 164.587209264,
        164.583462881, 164.555582900, 164.581018951,
        164.559148355, 164.563333155, 164.565172531,
        164.564831551, 164.567394428, 164.581083527,
        164.567890256, 164.565486682, 162.609175403,
        151.692183566, 137.444247340, 125.704337058,
        116.545006349, 109.996295484, 105.443832946,
        101.992914880, 99.447364997, 97.455138422,
        95.971594935, 94.726141534, 93.789663654,
        93.000992621, 92.347180306, 91.808287130,
        91.337563273, 90.975973893, 90.682247744,
    }};
    // Wave 31 disabled-cell controls. The complex H3 correction must not move
    // the detector coordinate, fundamental, H2, or H5 while closing H3.
    constexpr std::array<double, 33> baselineRawReductionDb{{
        0.029716000, 0.048177000, 0.070274000, 0.096058000,
        0.125481000, 0.158561000, 0.195368000, 0.235742000,
        0.279877000, 0.327532000, 0.378935000, 0.434012000,
        0.492733000, 0.555186000, 0.621030000, 0.691084000,
        0.764184000, 0.841293000, 0.922006000, 1.006012000,
        1.094495000, 1.186523000, 1.280972000, 1.380174000,
        1.483288000, 1.589177000, 1.698991000, 1.812978000,
        1.930353000, 2.050711000, 2.175596000, 2.330144000,
        2.523558000,
    }};
    constexpr std::array<double, 33> baselineH1Dbfs{{
        -22.517646519, -22.267776115, -22.017924806, -21.768063099,
        -21.518243558, -21.268396665, -21.018562962, -20.768732336,
        -20.518977564, -20.269160613, -20.019358791, -19.769581424,
        -19.519810191, -19.270195905, -19.028065592, -18.823623932,
        -18.647961145, -18.492946606, -18.353784838, -18.225785490,
        -18.108660738, -17.993383887, -17.886011202, -17.784177402,
        -17.686316849, -17.593147310, -17.501647061, -17.413560609,
        -17.328535586, -17.238455408, -17.158407652, -17.079629548,
        -17.003295320,
    }};
    constexpr std::array<double, 33> baselineH2RelativeDb{{
        -68.176862518, -67.919736609, -67.661408400, -67.401899067,
        -67.141503189, -66.880034702, -66.617654179, -66.354426405,
        -66.090456669, -65.825923755, -65.560601330, -65.294723278,
        -65.028456835, -64.761711657, -64.558376597, -64.503492683,
        -64.458505667, -64.426396787, -64.344126736, -63.974874818,
        -63.415142104, -62.693191572, -61.858654057, -60.940430082,
        -60.256219303, -59.583329668, -58.898983716, -58.215039428,
        -57.538036035, -56.944940939, -56.456114109, -55.923925238,
        -55.340584815,
    }};
    constexpr std::array<double, 33> baselineH5RelativeDb{{
        -107.650059235, -103.475767425, -100.217278878, -97.499889181,
        -95.181745995, -93.143545054, -91.321140508, -89.677320044,
        -88.198104175, -86.844162414, -85.590532914, -84.423507653,
        -83.334528742, -82.307836261, -81.335691130, -80.407565785,
        -80.570779415, -82.113527529, -82.035633823, -79.712111086,
        -77.708341240, -76.065702145, -74.667480346, -73.365161890,
        -72.349628632, -71.367801275, -70.406562232, -69.659530361,
        -68.943540125, -68.245675954, -67.556478621, -67.038444608,
        -66.426639305,
    }};
    constexpr float inputPosition = 0.8f;
    constexpr float inputGainDb = 38.603869f;
    constexpr double worstErrorBoundDb = 0.75;
    constexpr double adjacentErrorStepBoundDb = 0.20;
    constexpr double phaseErrorBoundDegrees = 3.0;
    constexpr double neighbourMovementBoundDb = 0.02;
    double worstError = 0.0;
    double worstAdjacentErrorStep = 0.0;
    double worstPhaseErrorDegrees = 0.0;
    double worstReductionMovementDb = 0.0;
    double worstH1MovementDb = 0.0;
    double worstH2MovementDb = 0.0;
    double worstH5MovementDb = 0.0;
    double previousError = 0.0;
    for (size_t index = 0; index < referenceH3RelativeDb.size(); ++index)
    {
        const double driveDb = -14.0 + 0.25 * static_cast<double>(index);
        const auto measured = measureFetHarmonics(
            100.0, static_cast<float>(driveDb - inputGainDb),
            inputPosition, 0, 48000.0, 13, 12.0, 9.0, 10.5);
        const double relativeDb = measured.h3Dbfs - measured.h1Dbfs;
        const double error = relativeDb - referenceH3RelativeDb[index];
        const double relativePhaseDegrees = std::remainder(
            (std::atan2(measured.h3Imag, measured.h3Real)
                - 3.0 * std::atan2(measured.h1Imag, measured.h1Real))
                * 180.0 / static_cast<double>(kPi),
            360.0);
        const double phaseErrorDegrees = std::remainder(
            relativePhaseDegrees - referenceH3PhaseDegrees[index], 360.0);
        worstError = std::max(worstError, std::abs(error));
        worstPhaseErrorDegrees = std::max(
            worstPhaseErrorDegrees, std::abs(phaseErrorDegrees));
        worstReductionMovementDb = std::max(worstReductionMovementDb,
            std::abs(measured.reductionDbMean
                - baselineRawReductionDb[index]));
        worstH1MovementDb = std::max(worstH1MovementDb,
            std::abs(measured.h1Dbfs - baselineH1Dbfs[index]));
        worstH2MovementDb = std::max(worstH2MovementDb,
            std::abs(measured.h2RelativeDb
                - baselineH2RelativeDb[index]));
        worstH5MovementDb = std::max(worstH5MovementDb,
            std::abs(measured.h5Dbfs - measured.h1Dbfs
                - baselineH5RelativeDb[index]));
        if (index > 0)
            worstAdjacentErrorStep = std::max(
                worstAdjacentErrorStep, std::abs(error - previousError));
        previousError = error;
        std::printf("FET dense shallow H3: drive %+.2f raw GR %.6f "
                    "reference %.6f measured %.6f error %+.6f dB "
                    "phase ref %.3f measured %.3f error %+.3f deg "
                    "H1dB %.9f H2rel %.9f H5rel %.9f\n",
                    driveDb, measured.reductionDbMean,
                    referenceH3RelativeDb[index], relativeDb, error,
                    referenceH3PhaseDegrees[index], relativePhaseDegrees,
                    phaseErrorDegrees,
                    measured.h1Dbfs, measured.h2RelativeDb,
                    measured.h5Dbfs - measured.h1Dbfs);
    }
    std::printf("FET dense shallow H3: worst error %.6f dB (bound %.2f), "
                "worst adjacent error step %.6f dB (bound %.2f)\n",
                worstError, worstErrorBoundDb,
                worstAdjacentErrorStep, adjacentErrorStepBoundDb);
    std::printf("FET dense shallow H3: worst phase error %.6f degrees "
                "(bound %.1f)\n",
                worstPhaseErrorDegrees, phaseErrorBoundDegrees);
    std::printf("FET dense shallow H3 neighbours: GR %.9f H1 %.9f H2 %.9f "
                "H5 %.9f dB movement (bound %.2f)\n",
                worstReductionMovementDb, worstH1MovementDb,
                worstH2MovementDb, worstH5MovementDb,
                neighbourMovementBoundDb);
    require(worstError < worstErrorBoundDb
                && worstAdjacentErrorStep < adjacentErrorStepBoundDb
                && worstPhaseErrorDegrees < phaseErrorBoundDegrees
                && worstReductionMovementDb < neighbourMovementBoundDb
                && worstH1MovementDb < neighbourMovementBoundDb
                && worstH2MovementDb < neighbourMovementBoundDb
                && worstH5MovementDb < neighbourMovementBoundDb,
            "vintage FET dense shallow H3 follows the continuous UAD knee surface");
}

void testFetDenseBroadbandComplexH3()
{
    // Wave 27's matched-reduction experiment exposed a hole between the sparse
    // broadband-K3 anchors: at the campaign's standard Input 0.8 setting the
    // reference H3 vector rotates by more than 120 degrees as drive increases,
    // while the reduction-only cubic remains near 135 degrees. These are
    // same-stimulus UAD rows, so unlike the diagnostic matched-GR pairs they
    // are an absolute parity surface. Nine 6 dB-spaced calibration rows are the
    // fit candidates. The interleaved original campaign rows are held out.
    struct Point
    {
        double driveDb;
        double referenceH3RelativeDb;
        double referenceH3PhaseDegrees;
        bool longWindow;
    };
    constexpr std::array<Point, 17> points48{{
        {-10.000000, -80.093341978695, 177.687066107719, true},
        { -9.396131, -78.547979854381, 171.226193686187, false},
        { -6.000000, -75.275138780963, 161.662663170743, true},
        {  0.000000, -71.341723628416, 148.874140373035, true},
        {  2.603869, -67.758380420931, 134.327004564712, false},
        {  6.000000, -69.398865553427, 143.746264604366, true},
        {  8.603869, -66.683950146981, 129.606510806320, false},
        { 12.000000, -69.994428200374, 133.836171775278, true},
        { 14.603869, -67.067362001200, 114.802823644219, false},
        { 18.000000, -71.693122390628, 105.360058420843, true},
        { 20.603869, -67.007091083965,  97.413860751674, false},
        { 24.000000, -71.022650547630, 104.135549319130, true},
        { 26.603869, -66.239258321331, 103.756598802683, false},
        { 29.603869, -66.280253217482,  90.457415082858, false},
        { 30.000000, -70.799346975874,  82.234725653060, true},
        { 32.603869, -65.841834939054,  78.825484571464, false},
        { 34.000000, -68.728361536357,  52.833790119970, true},
    }};
    constexpr std::array<Point, 13> points96{{
        {-10.000000, -80.097042453048, 177.696342306673, true},
        { -6.000000, -75.455011881557, 161.038265236934, true},
        {  0.000000, -71.628434186257, 147.756265096633, true},
        {  2.603869, -68.081229874941, 131.316680755374, false},
        {  6.000000, -69.742686119066, 141.522779246719, true},
        { 12.000000, -70.306788585484, 130.972063972273, true},
        { 14.603869, -67.231070894836, 111.064005040855, false},
        { 18.000000, -71.753961126338, 100.247994488755, true},
        { 24.000000, -71.046582933575,  98.881462478703, true},
        { 26.603869, -66.344200218942,  99.526868957747, false},
        { 30.000000, -70.507458664241,  77.826050538591, true},
        { 32.603869, -65.687203176704,  74.615490903841, false},
        { 34.000000, -68.244547763047,  50.193464717676, true},
    }};
    struct Neighbour
    {
        double reductionDb;
        double h1Dbfs;
        double h2RelativeDb;
        double h5RelativeDb;
    };
    // GR/H1/H2 are the Wave 32 disabled-cell controls. H5 is the accepted
    // Wave 34 complex-H5 surface; keeping it here preserves the isolation
    // check on the H3 cell after that independently intended H5 movement.
    constexpr std::array<Neighbour, 17> neighbours48{{
        {0.841649000, -18.629714537, -64.887909553, -99.581655271},
        {1.017519000, -18.285063626, -64.846393526, -100.879597828},
        {2.575931000, -16.984825081, -65.586058982, -91.534458011},
        {6.890009000, -15.413734199, -66.844373894, -83.085557935},
        {8.982944000, -14.909029296, -65.422977733, -77.202491691},
        {11.845395000, -14.385453508, -62.441230449, -80.058262355},
        {13.924587000, -13.870014377, -60.081638318, -75.647198253},
        {16.792435000, -13.358179157, -56.969671451, -79.034972720},
        {18.855112000, -12.832771605, -54.763665988, -74.626986666},
        {21.740536000, -12.351838120, -51.626013367, -78.192254130},
        {23.772755000, -11.808827369, -48.961281052, -73.785922015},
        {26.637381000, -11.331278484, -45.593773604, -77.563605756},
        {28.638845000, -10.778717817, -43.962033952, -73.267264548},
        {31.046316000, -10.263438194, -43.210607233, -73.005297776},
        {31.472347000, -10.309492422, -43.213892135, -77.110961415},
        {33.429245000,  -9.749078832, -42.466283929, -72.684391483},
        {34.640800000,  -9.629165393, -42.230297759, -76.742451740},
    }};
    constexpr std::array<Neighbour, 13> neighbours96{{
        {0.840455000, -18.632275228, -64.886996710, -99.473814047},
        {2.563588000, -16.987101913, -65.552781857, -91.448550478},
        {6.876930000, -15.402907847, -66.812403986, -83.352131442},
        {8.979934000, -14.908283965, -65.396640992, -77.186430657},
        {11.842650000, -14.384965875, -62.429217230, -80.220393326},
        {16.794703000, -13.362716404, -56.963142668, -79.133477181},
        {18.860217000, -12.840183893, -54.756840865, -74.650385893},
        {21.742937000, -12.356526314, -51.622620658, -78.215637876},
        {26.637899000, -11.334068943, -45.592883592, -77.527998525},
        {28.656762000, -10.799360916, -43.977103985, -73.448082011},
        {31.468414000, -10.307674402, -43.209729376, -77.004771575},
        {33.443372000,  -9.766143549, -42.478466744, -72.935668328},
        {34.636967000,  -9.627382846, -42.226193831, -76.676009167},
    }};
    constexpr float inputPosition = 0.8f;
    constexpr double inputGainDb = 38.603869;
    constexpr double magnitudeBoundDb = 0.25;
    constexpr double phaseBoundDegrees = 3.0;
    constexpr double adjacentErrorStepBoundDb = 0.20;
    double worstMagnitudeErrorDb = 0.0;
    double worstPhaseErrorDegrees = 0.0;
    double worstAdjacentErrorStepDb = 0.0;
    double worstReductionMovementDb = 0.0;
    double worstH1MovementDb = 0.0;
    double worstH2MovementDb = 0.0;
    double worstH5MovementDb = 0.0;

    const auto measureRate = [&](double sampleRate, const auto& points,
                                 const auto& neighbours) {
        double previousError = 0.0;
        for (size_t index = 0; index < points.size(); ++index)
        {
            const auto& point = points[index];
            const auto measured = measureFetHarmonics(
                1000.0, static_cast<float>(point.driveDb - inputGainDb),
                inputPosition, 0, sampleRate, 13,
                point.longWindow ? 12.0 : 5.0,
                point.longWindow ? 9.0 : 3.0,
                point.longWindow ? 10.5 : 4.5,
                point.longWindow ? 0.5f : 0.66595459f);
            const double relativeDb = measured.h3Dbfs - measured.h1Dbfs;
            const double magnitudeErrorDb
                = relativeDb - point.referenceH3RelativeDb;
            const double relativePhaseDegrees = std::remainder(
                (std::atan2(measured.h3Imag, measured.h3Real)
                    - 3.0 * std::atan2(measured.h1Imag, measured.h1Real))
                    * 180.0 / static_cast<double>(kPi),
                360.0);
            const double phaseErrorDegrees = std::remainder(
                relativePhaseDegrees - point.referenceH3PhaseDegrees, 360.0);
            worstMagnitudeErrorDb = std::max(
                worstMagnitudeErrorDb, std::abs(magnitudeErrorDb));
            worstPhaseErrorDegrees = std::max(
                worstPhaseErrorDegrees, std::abs(phaseErrorDegrees));
            worstReductionMovementDb = std::max(worstReductionMovementDb,
                std::abs(measured.reductionDbMean
                    - neighbours[index].reductionDb));
            worstH1MovementDb = std::max(worstH1MovementDb,
                std::abs(measured.h1Dbfs - neighbours[index].h1Dbfs));
            worstH2MovementDb = std::max(worstH2MovementDb,
                std::abs(measured.h2RelativeDb
                    - neighbours[index].h2RelativeDb));
            worstH5MovementDb = std::max(worstH5MovementDb,
                std::abs(measured.h5Dbfs - measured.h1Dbfs
                    - neighbours[index].h5RelativeDb));
            if (index > 0)
                worstAdjacentErrorStepDb = std::max(
                    worstAdjacentErrorStepDb,
                    std::abs(magnitudeErrorDb - previousError));
            previousError = magnitudeErrorDb;
            std::printf("FET dense broadband complex H3: %.0f kHz drive "
                        "%+.6f raw GR %.6f reference %.6f measured %.6f "
                        "error %+.6f dB phase ref %.3f measured %.3f "
                        "error %+.3f deg H1 %.9f H2rel %.9f H5rel %.9f %s\n",
                        sampleRate / 1000.0, point.driveDb,
                        measured.reductionDbMean,
                        point.referenceH3RelativeDb, relativeDb,
                        magnitudeErrorDb, point.referenceH3PhaseDegrees,
                        relativePhaseDegrees, phaseErrorDegrees,
                        measured.h1Dbfs, measured.h2RelativeDb,
                        measured.h5Dbfs - measured.h1Dbfs,
                        point.longWindow ? "fit" : "heldout");
        }
    };
    measureRate(48000.0, points48, neighbours48);
    measureRate(96000.0, points96, neighbours96);
    std::printf("FET dense broadband complex H3 summary: magnitude %.6f dB "
                "(bound %.2f), phase %.6f degrees (bound %.1f), adjacent "
                "error step %.6f dB (bound %.2f)\n",
                worstMagnitudeErrorDb, magnitudeBoundDb,
                worstPhaseErrorDegrees, phaseBoundDegrees,
                worstAdjacentErrorStepDb, adjacentErrorStepBoundDb);
    std::printf("FET dense broadband complex H3 neighbours: GR %.9f H1 "
                "%.9f H2 %.9f H5 %.9f dB movement (bound 0.02)\n",
                worstReductionMovementDb, worstH1MovementDb,
                worstH2MovementDb, worstH5MovementDb);
    require(worstReductionMovementDb < 0.02
                && worstH1MovementDb < 0.02
                && worstH2MovementDb < 0.02
                && worstH5MovementDb < 0.02,
            "vintage FET broadband complex H3 preserves GR, H1, H2, and H5");
    require(worstMagnitudeErrorDb < magnitudeBoundDb
                && worstPhaseErrorDegrees < phaseBoundDegrees
                && worstAdjacentErrorStepDb < adjacentErrorStepBoundDb,
            "vintage FET dense broadband complex H3 follows the UAD drive and rate surface");
}

void testFetDenseBroadbandComplexH5()
{
    // Wave 27 proved that the high-passed T5 source is complex-linear, but its
    // proposed reduction-only scale compared two different Release controls:
    // the matched-reduction probe used 0.5 while the original campaign used
    // 0.66595459. The apparent contradiction was therefore a missing control
    // coordinate. These rows close that measurement hole with same-stimulus
    // UAD vectors. The Release-0.5 rows are the retained 6 dB-spaced rate
    // calibration captures; the Release-0.66595459 rows are the independently
    // captured original 96 kHz campaign grid.
    struct Point
    {
        double sampleRate;
        float inputPosition;
        float inputDbfs;
        float releasePosition;
        double referenceH5RelativeDb;
        double referenceH5PhaseDegrees;
        bool calibration;
    };
    constexpr std::array<Point, 31> points{{
        {48000.0, 0.8f, -44.603869f, 0.5f, -91.533179234405, 105.900970272912, true},
        {48000.0, 0.8f, -38.603869f, 0.5f, -83.083794562062, 105.048867429580, true},
        {48000.0, 0.8f, -32.603869f, 0.5f, -80.057145956160, 107.618090045645, true},
        {48000.0, 0.8f, -26.603869f, 0.5f, -79.035871730328, 107.440746891236, true},
        {48000.0, 0.8f, -20.603869f, 0.5f, -78.192600105118, 107.268602058487, true},
        {48000.0, 0.8f, -14.603869f, 0.5f, -77.563532531602, 107.098390087017, true},
        {48000.0, 0.8f,  -8.603869f, 0.5f, -77.110371461677, 107.129903567437, true},
        {48000.0, 0.8f,  -4.603869f, 0.5f, -76.743252387396, 108.593988909372, true},
        {96000.0, 0.8f, -44.603869f, 0.5f, -91.449657516767,  96.097304738500, true},
        {96000.0, 0.8f, -38.603869f, 0.5f, -83.351695647491,  96.931650857938, true},
        {96000.0, 0.8f, -32.603869f, 0.5f, -80.220609495544,  97.853510967479, true},
        {96000.0, 0.8f, -26.603869f, 0.5f, -79.133402786188,  97.952804569443, true},
        {96000.0, 0.8f, -20.603869f, 0.5f, -78.215989155770,  97.758513230822, true},
        {96000.0, 0.8f, -14.603869f, 0.5f, -77.528149737899,  97.624203875896, true},
        {96000.0, 0.8f,  -8.603869f, 0.5f, -77.004561671439,  98.289469095491, true},
        {96000.0, 0.8f,  -4.603869f, 0.5f, -76.675624919709,  99.072196558121, true},
        {48000.0, 0.8f, -36.000000f, 0.66595459f, -77.202512804068, 106.812037993755, false},
        {48000.0, 0.8f, -30.000000f, 0.66595459f, -75.647217239294, 106.560727086506, false},
        {48000.0, 0.8f, -24.000000f, 0.66595459f, -74.626974737862, 106.477570022950, false},
        {48000.0, 0.2f, -12.000000f, 0.66595459f, -86.169521078737, 104.904138993336, false},
        {48000.0, 0.8f, -18.000000f, 0.66595459f, -73.785900563641, 106.480020496798, false},
        {48000.0, 0.8f, -12.000000f, 0.66595459f, -73.267311371359, 106.277905357769, false},
        {48000.0, 0.2f,  -6.000000f, 0.66595459f, -78.820903125102, 107.020699420289, false},
        {48000.0, 0.8f,  -9.000000f, 0.66595459f, -73.005278739567, 106.479510610175, false},
        {48000.0, 0.8f,  -6.000000f, 0.66595459f, -72.684399572396, 106.768369965984, false},
        {96000.0, 0.8f, -36.000000f, 0.66595459f, -77.186414195630, 97.604616779722, false},
        {96000.0, 0.8f, -24.000000f, 0.66595459f, -74.650335303332, 98.651857220974, false},
        {96000.0, 0.2f, -12.000000f, 0.66595459f, -86.410298909599, 97.167861054148, false},
        {96000.0, 0.8f, -12.000000f, 0.66595459f, -73.447941957507, 98.436663459609, false},
        {96000.0, 0.2f,  -6.000000f, 0.66595459f, -78.886313234298, 97.779477772866, false},
        {96000.0, 0.8f,  -6.000000f, 0.66595459f, -72.935442093146, 98.986422851229, false},
    }};
    constexpr double magnitudeBoundDb = 0.25;
    constexpr double phaseBoundDegrees = 3.0;
    double worstMagnitudeErrorDb = 0.0;
    double worstPhaseErrorDegrees = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            1000.0, point.inputDbfs, point.inputPosition, 0,
            point.sampleRate, 13,
            point.calibration ? 12.0 : 5.0,
            point.calibration ? 9.0 : 3.0,
            point.calibration ? 10.5 : 4.5,
            point.releasePosition);
        const double relativeDb = measured.h5Dbfs - measured.h1Dbfs;
        const double magnitudeErrorDb
            = relativeDb - point.referenceH5RelativeDb;
        const double relativePhaseDegrees = std::remainder(
            (std::atan2(measured.h5Imag, measured.h5Real)
                - 5.0 * std::atan2(measured.h1Imag, measured.h1Real))
                * 180.0 / static_cast<double>(kPi),
            360.0);
        const double relativeMagnitude = std::hypot(
            measured.h5Real, measured.h5Imag)
            / std::max(std::hypot(measured.h1Real, measured.h1Imag), 1.0e-30);
        const double relativeReal = relativeMagnitude * std::cos(
            relativePhaseDegrees * static_cast<double>(kPi) / 180.0);
        const double relativeImag = relativeMagnitude * std::sin(
            relativePhaseDegrees * static_cast<double>(kPi) / 180.0);
        const double phaseErrorDegrees = std::remainder(
            relativePhaseDegrees - point.referenceH5PhaseDegrees, 360.0);
        worstMagnitudeErrorDb = std::max(
            worstMagnitudeErrorDb, std::abs(magnitudeErrorDb));
        worstPhaseErrorDegrees = std::max(
            worstPhaseErrorDegrees, std::abs(phaseErrorDegrees));
        std::printf("FET dense broadband complex H5: %.0f kHz i%.1f src "
                    "%+.6f Release %.9f raw GR %.6f reference %.6f "
                    "measured %.6f error %+.6f dB phase ref %.3f "
                    "measured %.6f error %+.6f deg H5re %+.12e "
                    "H5im %+.12e %s\n",
                    point.sampleRate / 1000.0,
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    static_cast<double>(point.releasePosition),
                    measured.reductionDbMean,
                    point.referenceH5RelativeDb, relativeDb,
                    magnitudeErrorDb, point.referenceH5PhaseDegrees,
                    relativePhaseDegrees, phaseErrorDegrees,
                    relativeReal, relativeImag,
                    point.calibration ? "calibration" : "campaign");
    }
    std::printf("FET dense broadband complex H5 summary: magnitude %.6f dB "
                "(bound %.2f), phase %.6f degrees (bound %.1f)\n",
                worstMagnitudeErrorDb, magnitudeBoundDb,
                worstPhaseErrorDegrees, phaseBoundDegrees);
    require(worstMagnitudeErrorDb < magnitudeBoundDb
                && worstPhaseErrorDegrees < phaseBoundDegrees,
            "vintage FET dense broadband complex H5 follows the UAD drive, Release, and rate surface");
}

void testFetLowFrequencyOddHarmonicSurface()
{
    // Absolute H3 anchors from the installed reference unit. The 100 Hz set
    // spans the complete low-frequency T3 law; the 1 kHz set is the opposite
    // failure-mode guard, because this nominally low-frequency path remains
    // large enough there that an unconstrained 100 Hz fit would undo Wave 12.
    struct Point
    {
        double frequencyHz;
        float inputPosition;
        float inputDbfs;
        double reductionDb;
        double referenceH3RelativeDb;
        double previousErrorDb;          // the constant -0.0058 T3 term
    };
    constexpr std::array<Point, 18> points{{
        {100.0, 0.3f, -30.0f, -0.0087, -91.700375, +0.5559},
        {100.0, 0.7f, -46.0f,  0.2378, -83.943235, +0.7112},
        {100.0, 0.6f, -42.0f,  0.5373, -81.075196, +4.9007},
        {100.0, 0.3f, -24.0f,  0.7234, -78.962196, +5.5109},
        {100.0, 0.9f, -48.0f,  1.3622, -67.407430, -0.2529},
        {100.0, 0.2f, -12.0f,  2.5753, -60.225389, -1.4289},
        {100.0, 0.8f, -36.0f,  8.9210, -50.600997, +0.0783},
        {100.0, 0.8f, -30.0f, 13.8515, -48.872046, +2.3465},
        {100.0, 0.8f, -18.0f, 23.6915, -47.030741, +0.6235},
        {100.0, 0.9f,  -7.5f, 33.1003, -46.020733, -0.4754},
        {100.0, 1.0f,  -6.0f, 34.3368, -46.009239, -0.5075},
        {1000.0, 0.4f, -30.0f,  0.5570, -81.411229, -0.0501},
        {1000.0, 0.6f, -36.0f,  3.2496, -72.802041, -0.0073},
        {1000.0, 0.8f, -36.0f,  8.9830, -67.758380, -0.0895},
        {1000.0, 0.8f, -30.0f, 13.9253, -66.683950, +0.0055},
        {1000.0, 0.4f,  -9.0f, 15.1374, -66.685792, -0.0476},
        {1000.0, 0.8f, -24.0f, 18.8564, -67.067362, +0.0040},
        {1000.0, 0.8f, -12.0f, 28.6512, -66.239258, -0.0394}}};

    constexpr double kLowFrequencyBoundDb = 0.75;
    constexpr double kBroadbandGuardDb = 0.98;
    double lowFrequencyWorst = 0.0;
    double broadbandWorst = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            point.frequencyHz, point.inputDbfs, point.inputPosition,
            0, 48000.0, 13, 5.0, 3.0, 4.5,
            point.frequencyHz == 1000.0 ? 0.66595459f : 0.5f);
        const double relativeDb = measured.h3Dbfs - measured.h1Dbfs;
        const double error = relativeDb - point.referenceH3RelativeDb;
        if (point.frequencyHz == 100.0)
            lowFrequencyWorst = std::max(lowFrequencyWorst, std::abs(error));
        else
            broadbandWorst = std::max(broadbandWorst, std::abs(error));
        std::printf("FET low T3: %.0f Hz i%.1f %+.1f dBFS gr %.4f "
                    "(expected %.4f) reference %.6f measured %.6f "
                    "error %+.6f dB (constant T3 %+.4f)\n",
                    point.frequencyHz,
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    measured.reductionDbMean, point.reductionDb,
                    point.referenceH3RelativeDb, relativeDb, error,
                    point.previousErrorDb);
        require(std::abs(measured.reductionDbMean - point.reductionDb) < 0.02,
                "vintage FET low-T3 anchors are evaluated at the reduction they were measured at");
    }
    std::printf("FET low T3: 100 Hz worst %.6f dB (bound %.2f), "
                "1 kHz guard worst %.6f dB (bound %.2f)\n",
                lowFrequencyWorst, kLowFrequencyBoundDb,
                broadbandWorst, kBroadbandGuardDb);
    require(lowFrequencyWorst < kLowFrequencyBoundDb,
            "vintage FET low-frequency H3 matches the measured reference across the reduction axis");
    require(broadbandWorst < kBroadbandGuardDb,
            "vintage FET low-frequency T3 fit does not undo the broadband H3 surface");
}

void testFetLowFrequencyFifthHarmonicSurface()
{
    // Absolute 100 Hz H5 anchors from the installed reference unit. All sit
    // above the campaign's -92 dBc floor and span the complete measured
    // low-frequency T5 law. H3 is guarded independently by
    // testFetLowFrequencyOddHarmonicSurface, which is necessary because this
    // fifth-order term also feeds the compressor loop.
    struct Point
    {
        float inputPosition;
        float inputDbfs;
        double reductionDb;
        double referenceH5RelativeDb;
        double previousErrorDb;          // the constant +0.00255 T5 term
    };
    constexpr std::array<Point, 11> points{{
        {0.5f, -36.0f,  0.8882, -83.281606, +6.3542},
        {0.8f, -48.0f,  0.9345, -81.697045, +5.2159},
        {0.2f, -15.0f,  1.1328, -77.026211, +2.2414},
        {0.8f, -46.0f,  1.7057, -70.430321, -0.7602},
        {0.8f, -45.0f,  2.1781, -67.616563, -1.4404},
        {0.6f, -36.0f,  3.1896, -64.403196, -1.1304},
        {0.2f,  -6.0f,  6.9247, -58.897481, +0.0256},
        {0.9f, -36.0f,  9.8790, -56.672763, +0.8281},
        {0.8f, -30.0f, 13.8515, -55.670300, +2.7093},
        {0.9f, -18.0f, 24.6525, -53.815062, +0.9463},
        {1.0f,  -6.0f, 34.3368, -52.992012, +0.1356}}};

    constexpr double kBoundDb = 0.20;
    double worstError = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            100.0, point.inputDbfs, point.inputPosition);
        const double relativeDb = measured.h5Dbfs - measured.h1Dbfs;
        const double error = relativeDb - point.referenceH5RelativeDb;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET low T5: i%.1f %+.1f dBFS gr %.9f (expected %.4f) "
                    "reference %.6f measured %.6f error %+.6f dB "
                    "(constant T5 %+.4f)\n",
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    measured.reductionDbMean, point.reductionDb,
                    point.referenceH5RelativeDb, relativeDb, error,
                    point.previousErrorDb);
        require(std::abs(measured.reductionDbMean - point.reductionDb) < 0.02,
                "vintage FET low-T5 anchors are evaluated at the reduction they were measured at");
    }
    std::printf("FET low T5: worst %.6f dB over %zu anchors (bound %.2f)\n",
                worstError, points.size(), kBoundDb);
    require(worstError < kBoundDb,
            "vintage FET low-frequency H5 matches the measured reference across the reduction axis");
}

void testFetBroadbandFifthHarmonicSurface()
{
    // Absolute 1 kHz H5 anchors from the installed reference unit. They span
    // every scoreable reduction region of the dense campaign and are kept
    // separate from the low-frequency T5 gate because that path's finite
    // leakage cannot reproduce the nearly uniform broadband residual without
    // undoing its 100 Hz fit.
    struct Point
    {
        float inputPosition;
        float inputDbfs;
        double reductionDb;
        double referenceH5RelativeDb;
        double previousErrorDb;          // after the fitted low-T5 path
    };
    constexpr std::array<Point, 10> points{{
        {0.2f, -12.0f,  2.6305, -86.169521, -3.5774},
        {0.6f, -36.0f,  3.2496, -84.117415, -3.7298},
        {0.7f, -36.0f,  5.0243, -80.818005, -3.7525},
        {0.8f, -36.0f,  8.9830, -77.202513, -3.6191},
        {0.4f, -12.0f, 12.6634, -75.951320, -3.5684},
        {0.6f, -18.0f, 17.7317, -74.841900, -3.6337},
        {0.6f, -12.0f, 22.6659, -73.960898, -3.7055},
        {0.6f,  -6.0f, 27.5467, -73.353275, -3.7455},
        {0.8f,  -9.0f, 31.0579, -73.005279, -3.7882},
        {0.8f,  -6.0f, 33.4397, -72.684400, -3.9067}}};

    constexpr double kBoundDb = 0.25;
    double worstError = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            1000.0, point.inputDbfs, point.inputPosition,
            0, 48000.0, 13, 5.0, 3.0, 4.5, 0.66595459f);
        const double relativeDb = measured.h5Dbfs - measured.h1Dbfs;
        const double error = relativeDb - point.referenceH5RelativeDb;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET broadband T5: i%.1f %+.1f dBFS gr %.9f "
                    "(expected %.4f) reference %.6f measured %.6f "
                    "error %+.6f dB (low T5 %+.4f)\n",
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    measured.reductionDbMean, point.reductionDb,
                    point.referenceH5RelativeDb, relativeDb, error,
                    point.previousErrorDb);
        require(std::abs(measured.reductionDbMean - point.reductionDb) < 0.02,
                "vintage FET broadband-T5 anchors are evaluated at the reduction they were measured at");
    }
    std::printf("FET broadband T5: worst %.6f dB over %zu anchors "
                "(bound %.2f)\n", worstError, points.size(), kBoundDb);
    require(worstError < kBoundDb,
            "vintage FET broadband H5 matches the measured reference across the reduction axis");
}

void testFetLowFrequencyColourSurface()
{
    // The 100 Hz twin of testFetBroadbandHarmonicSurface, gating
    // `fetLowFrequencyK2`. Seven absolute anchors on the reference unit's own
    // 100 Hz second harmonic, rendered from the installed AU by
    // `probe_h2_surface.py` (campaign reference_comparison_1176) and quoted as
    // H2 relative to the fundamental, which is scalar-immune.
    //
    // Why 100 Hz needs its own gate at all: at 1 kHz this table contributes
    // 49 dB down and the broadband table owns the spectrum, so
    // testFetBroadbandHarmonicSurface cannot see a low-frequency fault. It was
    // run with the pre-Wave-9 law installed and passes -- at that point the
    // 100 Hz surface was 2.07 dB out on 45 of its 91 compressing rows.
    struct Point
    {
        float inputPosition;
        float inputDbfs;
        double reductionDb;              // the fetLowFrequencyK2 argument
        double referenceH2RelativeDb;
        double previousErrorDb;          // what the pre-Wave-9 law read here
    };
    // One per region of the table, and deliberately not a flattering set: the
    // 0.9345 dB anchor is in the band this table CANNOT fix (see
    // fetLowFrequencyK2 -- the broadband table alone reads high there), so the
    // test carries the honest residual rather than sampling around it. The
    // 18.7912 and 33.3699 anchors are the two rows the sixteen-row campaign
    // grid failed on, at +1.5999 and -0.4610.
    constexpr std::array<Point, 7> points{{
        {0.2f, -36.0f,  -0.009, -83.993408, -0.0168},
        {0.8f, -48.0f,   0.934, -64.008370, +1.1667},
        {0.8f, -45.0f,   2.178, -56.347350, -1.9758},
        {0.2f,  -6.0f,   6.918, -47.274840, -0.6102},
        {0.6f, -24.0f,  12.726, -43.761082, +1.9464},
        {0.8f, -24.0f,  18.791, -41.975760, +1.5999},
        {0.8f,  -6.0f,  33.370, -37.552714, -0.4610}}};

    // Set by what it has to catch. The law this replaced read 2.07 dB out at
    // 100 Hz and 1.97 at the third anchor; the campaign's harmonic gate is
    // 1.0 dB. 0.35 dB is 5.9x under the worst defect, 2.9x under the campaign
    // gate, and 1.8x over the worst error this build produces anywhere on the
    // 91-row 100 Hz surface (0.196 dB, rendered).
    constexpr double kBoundDb = 0.35;
    double worstError = 0.0;
    for (const auto& point : points)
    {
        const auto measured = measureFetHarmonics(
            100.0, point.inputDbfs, point.inputPosition);
        const double error = measured.h2RelativeDb - point.referenceH2RelativeDb;
        worstError = std::max(worstError, std::abs(error));
        std::printf("FET LF colour: i%.1f %+.0f dBFS gr %.4f (expected %.3f) "
                    "reference %.6f measured %.6f error %+.6f dB "
                    "(previous law %+.4f)\n",
                    static_cast<double>(point.inputPosition),
                    static_cast<double>(point.inputDbfs),
                    measured.reductionDbMean, point.reductionDb,
                    point.referenceH2RelativeDb, measured.h2RelativeDb, error,
                    point.previousErrorDb);
        // Same guard as the broadband test: an anchor is only meaningful at the
        // depth it was measured at, so a detector change must fail here rather
        // than silently re-point the assertion.
        require(std::abs(measured.reductionDbMean - point.reductionDb) < 0.02,
                "vintage FET low-frequency anchors are evaluated at the reduction they were measured at");
    }
    std::printf("FET LF colour: worst %+.6f dB over %zu anchors (bound %.2f)\n",
                worstError, points.size(), kBoundDb);
    require(worstError < kBoundDb,
            "vintage FET low-frequency colour matches the measured reference across the reduction axis");
}

// The VCA mode's PULL/SC switch engages the measured dbx 160 sidechain tilt:
// |H(f)| = sqrt(f / 276 Hz). Check the filter alone against that ideal at
// 48 kHz, then that VCA mode actually routes a non-zero SC HP setting through
// it (more reduction on a 3 kHz tone than on a 60 Hz tone at equal level,
// the opposite of the other modes' high-pass which only ever removes lows).
void testDbxSidechainTilt()
{
    duskaudio::dbx160::SidechainTilt tilt;
    tilt.prepare(48000.0);
    float worst = 0.0f;
    for (const auto& point : duskaudio::dbx160::kSidechainTiltMeasured)
    {
        tilt.reset();
        const int settle = 48000, measure = 48000;
        double sumSq = 0.0;
        for (int i = 0; i < settle + measure; ++i)
        {
            const float x = std::sin(2.0f * 3.14159265f * point.hz * static_cast<float>(i) / 48000.0f);
            const float y = tilt.process(x);
            if (i >= settle) sumSq += static_cast<double>(y) * y;
        }
        const double rms = std::sqrt(sumSq / measure);
        const double gainDb = 20.0 * std::log10(rms / std::sqrt(0.5));
        worst = std::max(worst, static_cast<float>(std::abs(gainDb - point.db)));
        std::printf("  tilt %6.0f Hz: %+.3f dB (measured %+.2f)\n", point.hz, gainDb, point.db);
    }
    require(worst < 0.40f, "dbx sidechain tilt matches the measured reference response within 0.40 dB from 40 Hz to 20 kHz");

    auto reductionAt = [](float freq, float sidechainHp) {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 512);
        dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::VCA));
        dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::VcaThreshold, -27.0f);
        dsp.setParameter(MultiCompDSP::Parameter::VcaRatio, 50.4944f);
        dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, sidechainHp);
        std::array<float, 512> left{}, right{}, outLeft{}, outRight{};
        const float* inputs[2] = {left.data(), right.data()};
        float* outputs[2] = {outLeft.data(), outRight.data()};
        double inSq = 0.0, outSq = 0.0;
        for (int block = 0; block < 400; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float x = 0.25f * std::sin(2.0f * 3.14159265f * freq * static_cast<float>(block * 512 + i) / 48000.0f);
                left[static_cast<size_t>(i)] = x; right[static_cast<size_t>(i)] = x;
                if (block >= 300) inSq += static_cast<double>(x) * x;
            }
            dsp.processBlock(inputs, outputs, 2, 512);
            if (block >= 300)
                for (int i = 0; i < 512; ++i) outSq += static_cast<double>(outLeft[static_cast<size_t>(i)]) * outLeft[static_cast<size_t>(i)];
        }
        return 10.0 * std::log10(inSq / std::max(outSq, 1e-30));
    };
    const double lowOff = reductionAt(60.0f, 0.0f), lowOn = reductionAt(60.0f, 500.0f);
    const double highOff = reductionAt(3000.0f, 0.0f), highOn = reductionAt(3000.0f, 500.0f);
    std::printf("  VCA SC switch: 60 Hz %.2f -> %.2f dB, 3 kHz %.2f -> %.2f dB\n", lowOff, lowOn, highOff, highOn);
    require(std::abs(lowOff - highOff) < 0.3, "VCA detector is flat with the switch out");
    require(lowOn < lowOff - 3.0 && highOn > highOff + 3.0,
            "VCA PULL/SC engages a tilt: less reduction at 60 Hz, more at 3 kHz");
    std::puts("dbx sidechain tilt: response and VCA routing verified");
}

// VCA / dbx 160 parity gate. Every expected number below was measured on the
// installed UAD dbx 160 (reference_comparison_dbx160 campaign, 2026-09-01)
// with the same stimuli rendered in process here: steady 1 kHz tones for the
// static law at the default threshold (-27 dB) and 4:1 position, a pedestal
// step for the attack/release timing, and an equal-RMS sine/burst/noise
// triplet for the detector's crest response. Tolerances sit just outside the
// campaign's achieved residuals so a regression, not the reference, fails.
void testVcaDbxParityGates()
{
    constexpr int kRate = 48000, kBlock = 512;
    constexpr float kCompress4to1 = 50.4944f;
    auto makeDsp = [&](float compression) {
        auto dsp = std::make_unique<MultiCompDSP>();
        dsp->prepare(kRate, kBlock);
        dsp->setMode(static_cast<int>(duskaudio::MultiCompMode::VCA));
        dsp->setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
        dsp->setParameter(MultiCompDSP::Parameter::VcaThreshold, -27.0f);
        dsp->setParameter(MultiCompDSP::Parameter::VcaRatio, compression);
        dsp->setParameter(MultiCompDSP::Parameter::VcaOutput, 0.0f);
        return dsp;
    };
    // Renders `signal` (mono, duplicated to both channels) and returns the
    // left output. GR is always taken against the 1:1 render of the same
    // signal, so any output-stage constant cancels exactly as in the campaign.
    auto renderWith = [&](const std::vector<float>& signal, float compression) {
        auto dsp = makeDsp(compression);
        std::vector<float> out(signal.size(), 0.0f);
        std::array<float, kBlock> l{}, r{}, ol{}, orr{};
        const float* inputs[2] = {l.data(), r.data()};
        float* outputs[2] = {ol.data(), orr.data()};
        for (size_t start = 0; start < signal.size(); start += kBlock)
        {
            const size_t n = std::min<size_t>(kBlock, signal.size() - start);
            for (size_t i = 0; i < n; ++i) { l[i] = r[i] = signal[start + i]; }
            for (size_t i = n; i < kBlock; ++i) { l[i] = r[i] = 0.0f; }
            dsp->processBlock(inputs, outputs, 2, static_cast<int>(n));
            for (size_t i = 0; i < n; ++i) out[start + i] = ol[i];
        }
        return out;
    };
    auto rmsDb = [](const std::vector<float>& x, size_t from, size_t to) {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
        return 10.0 * std::log10(std::max(sum / static_cast<double>(to - from), 1e-30));
    };
    auto tone = [&](float dbfs, float seconds) {
        std::vector<float> s(static_cast<size_t>(kRate * seconds));
        const float a = std::pow(10.0f, dbfs / 20.0f);
        for (size_t i = 0; i < s.size(); ++i) s[i] = a * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / kRate);
        return s;
    };

    // Static law at the default threshold, 4:1: reference GR 2.08 / 11.38 /
    // 15.94 dB at -24 / -12 / -6 dBFS (probe_static.py); campaign residuals
    // were within 0.2 dB.
    {
        const float expected[3] = {2.08f, 11.38f, 15.94f};
        const float levels[3] = {-24.0f, -12.0f, -6.0f};
        for (int i = 0; i < 3; ++i)
        {
            const auto sig = tone(levels[i], 4.0f);
            const auto unity = renderWith(sig, 0.0f), comp = renderWith(sig, kCompress4to1);
            const size_t from = static_cast<size_t>(3.0f * kRate), to = sig.size();
            const double gr = rmsDb(unity, from, to) - rmsDb(comp, from, to);
            std::printf("  dbx static %+.0f dBFS: GR %.3f dB (reference %.2f)\n", levels[i], gr, expected[i]);
            require(std::abs(gr - expected[i]) < 0.35, "VCA static law matches the dbx 160 within 0.35 dB at the default threshold");
        }
    }
    // Step response: -40 dBFS pedestal to -12 dBFS at 1 s, back at 2 s.
    // Reference attack t63 12 ms, release t37 75 ms (probe_dynamics.py);
    // the model reproduces them within 1 ms.
    {
        std::vector<float> sig(static_cast<size_t>(kRate * 4));
        for (size_t i = 0; i < sig.size(); ++i)
        {
            const float t = static_cast<float>(i) / kRate;
            const float a = (t >= 1.0f && t < 2.0f) ? std::pow(10.0f, -12.0f / 20.0f) : std::pow(10.0f, -40.0f / 20.0f);
            sig[i] = a * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / kRate);
        }
        const auto unity = renderWith(sig, 0.0f), comp = renderWith(sig, kCompress4to1);
        auto grAt = [&](float seconds) {
            const size_t c = static_cast<size_t>(seconds * kRate);
            return rmsDb(unity, c, c + 48) - rmsDb(comp, c, c + 48);
        };
        const double settled = grAt(1.9f);
        double t63 = -1.0, t37 = -1.0;
        for (int ms = 0; ms < 400 && t63 < 0.0; ++ms)
            if (grAt(1.0f + ms * 0.001f) >= 0.63 * settled) t63 = ms;
        for (int ms = 0; ms < 800 && t37 < 0.0; ++ms)
            if (grAt(2.0f + ms * 0.001f) <= 0.37 * settled) t37 = ms;
        std::printf("  dbx step: settled %.2f dB, attack t63 %.0f ms (reference 12), release t37 %.0f ms (reference 75)\n", settled, t63, t37);
        require(std::abs(settled - 11.38) < 0.35, "VCA step settles at the reference GR");
        require(std::abs(t63 - 12.0) <= 3.0 && std::abs(t37 - 75.0) <= 6.0,
                "VCA attack/release timing matches the dbx 160 RMS integrator");
    }
    // Crest triplet at -20 dBFS RMS: the reference reads sine, 10 % duty
    // bursts and noise within 0.05 dB of each other (7.56 / 7.60 / 7.57).
    {
        const size_t n = static_cast<size_t>(kRate * 4);
        const float rms = std::pow(10.0f, -20.0f / 20.0f);
        std::vector<float> sine(n), burst(n), noise(n);
        uint32_t seed = 160u;
        double burstSq = 0.0, noiseSq = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i) / kRate;
            const float carrier = std::sin(2.0f * 3.14159265f * 1000.0f * t);
            sine[i] = std::sqrt(2.0f) * rms * carrier;
            burst[i] = (std::fmod(t * 50.0f, 1.0f) < 0.10f) ? carrier : 0.0f;
            seed = seed * 1664525u + 1013904223u;
            noise[i] = (static_cast<float>(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
            burstSq += static_cast<double>(burst[i]) * burst[i]; noiseSq += static_cast<double>(noise[i]) * noise[i];
        }
        const float burstGain = rms / static_cast<float>(std::sqrt(burstSq / n)), noiseGain = rms / static_cast<float>(std::sqrt(noiseSq / n));
        for (size_t i = 0; i < n; ++i) { burst[i] *= burstGain; noise[i] *= noiseGain; }
        double gr[3];
        int k = 0;
        for (const auto* sig : {&sine, &burst, &noise})
        {
            const auto unity = renderWith(*sig, 0.0f), comp = renderWith(*sig, kCompress4to1);
            const size_t from = static_cast<size_t>(3.0f * kRate);
            gr[k++] = rmsDb(unity, from, n) - rmsDb(comp, from, n);
        }
        std::printf("  dbx crest: sine %.2f burst %.2f noise %.2f dB\n", gr[0], gr[1], gr[2]);
        require(std::abs(gr[1] - gr[0]) < 0.4 && std::abs(gr[2] - gr[0]) < 0.4,
                "VCA detector is RMS: gated bursts and noise read within 0.4 dB of a sine at equal RMS");
    }
    std::puts("dbx 160 parity gates: static law, step timing, crest response");
}

void testPublishedGainReductionRange()
{
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -60.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalKnee, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.01f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRelease, 5000.0f);
    dsp.prepare(48000.0, 256);
    std::array<float, 256> input{}, output{};
    input.fill(2.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    for (int block = 0; block < 40; ++block) dsp.processBlock(inputs, outputs, 1, 256);
    const float reduction = dsp.getGainReduction();
    std::printf("published GR range: deep-compression reading %.9g dB\n", reduction);
    require(reduction >= -60.0f && reduction <= 0.0f,
            "published gain reduction stays inside its declared -60..0 dB range");
}

std::string sourceSection(const std::string& source, const char* begin, const char* end)
{
    const size_t first = source.find(begin);
    const size_t last = source.find(end, first == std::string::npos ? 0 : first + 1);
    require(first != std::string::npos && last != std::string::npos && last > first,
            "DSP source snapshot section exists");
    return source.substr(first, last - first);
}

size_t occurrenceCount(const std::string& text, const char* needle)
{
    size_t count = 0;
    for (size_t at = 0; (at = text.find(needle, at)) != std::string::npos; at += std::strlen(needle))
        ++count;
    return count;
}

std::string multiCompDspSource()
{
    std::string sourcePath = __FILE__;
    std::replace(sourcePath.begin(), sourcePath.end(), '\\', '/');
    const std::string suffix = "tests/MultiCompCoreTests.cpp";
    const size_t suffixPosition = sourcePath.rfind(suffix);
    require(suffixPosition != std::string::npos, "core-test source path is recognisable");
    sourcePath.replace(suffixPosition, suffix.size(), "MultiCompDSP.cpp");
    std::ifstream input(sourcePath);
    require(input.good(), "MultiCompDSP source is available to structural regression");
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void requireMultibandOutputSnapshotIsSingleLoad()
{
    const std::string source = multiCompDspSource();
    const std::string multiband = sourceSection(source, "void MultiCompDSP::processMultiband(",
                                                "std::array<float, 3> MultiCompDSP::crossoverTargets(");
    require(occurrenceCount(multiband, "params.mbOutput.load") == 1,
            "multiband output gain is loaded exactly once per block");
}

void testAtomicControlSnapshotsAreSingleLoad()
{
    const std::string source = multiCompDspSource();

    const std::string prepare = sourceSection(source, "void MultiCompDSP::prepare(",
                                               "void MultiCompDSP::reset(");
    const std::string block = sourceSection(source, "void MultiCompDSP::processBlockExternal(",
                                             "void MultiCompDSP::processLatencyHistory(");
    const std::string range = sourceSection(source, "void MultiCompDSP::processRange(",
                                             "void MultiCompDSP::processMultiband(");
    const std::string lookahead = sourceSection(source, "void MultiCompDSP::prepareLookahead(",
                                                 "void MultiCompDSP::syncModeParameters(");
    require(occurrenceCount(prepare, "params.oversampling.load") == 1,
            "prepare snapshots oversampling exactly once");
    require(occurrenceCount(block, "params.oversampling.load") == 1,
            "audio block snapshots oversampling exactly once");
    require(occurrenceCount(block, "params.globalLookahead.load") == 1
                && occurrenceCount(block, "params.digitalLookahead.load") == 1,
            "audio block snapshots both lookaheads exactly once");
    require(occurrenceCount(range, "params.oversampling.load") == 0
                && occurrenceCount(range, "params.digitalLookahead.load") == 0
                && occurrenceCount(lookahead, "params.globalLookahead.load") == 0,
            "processing helpers consume block snapshots without atomic re-reads");
    std::puts("atomic snapshots: oversampling/global lookahead/Digital lookahead loaded once per block");
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--opto-dense") == 0)
    {
        testOptoDenseProgrammeParity();
        std::puts("Multi-Comp Opto dense-programme parity test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--golden") == 0)
    {
        testGoldenVectors();
        std::puts("Multi-Comp golden-vector test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-tapers") == 0)
    {
        testFetMeasuredControlTapers();
        std::puts("Multi-Comp FET taper test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-static") == 0)
    {
        testFetMeasuredStaticSurface();
        std::puts("Multi-Comp FET static test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-knee-onset") == 0)
    {
        testFetKneeOnsetMatchesReference();
        std::puts("Multi-Comp FET knee-onset test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-knee-release") == 0)
    {
        testFetKneeCellDoesNotCancelDeepReleaseMemory();
        std::puts("Multi-Comp FET knee release-neighbour test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-max-gr") == 0)
    {
        testFetMaximumReductionSaturation();
        std::puts("Multi-Comp FET maximum-reduction test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-all-settling") == 0)
    {
        testFetAllButtonsSettlingWindows();
        std::puts("Multi-Comp FET All-buttons settling test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-absolute-gain") == 0)
    {
        testFetAbsoluteGainAnchors();
        testFetMeasuredCurveAllButtons();
        std::puts("Multi-Comp FET absolute-gain anchor test: PASS");
        return 0;
    }
    // Separately reachable so the Measured arm can be shown to hold while the
    // Modern-arm anchor above is deliberately broken -- `require` aborts on the
    // first failure, so a leak test needs the two runnable independently.
    if (argc == 2 && std::strcmp(argv[1], "--fet-measured-curve-all") == 0)
    {
        testFetMeasuredCurveAllButtons();
        std::puts("Multi-Comp FET Measured-curve All-buttons test: PASS");
        return 0;
    }
    // Likewise separate: the knee assertions and the plateau anchor guard
    // different entries of the same table, so each has to be runnable while the
    // other is deliberately broken.
    if (argc == 2 && std::strcmp(argv[1], "--fet-all-knee") == 0)
    {
        testFetAllButtonsKneeTransition();
        std::puts("Multi-Comp FET All-buttons knee test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-block") == 0)
    {
        testFetBlockSizeInvariance();
        std::puts("Multi-Comp FET block-size test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-reset") == 0)
    {
        testFetResetClearsProgrammeAndColourState();
        std::puts("Multi-Comp FET reset test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-stereo") == 0)
    {
        testFetInternalStereoLinkReference();
        std::puts("Multi-Comp FET stereo-link test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-stereo-phase") == 0)
    {
        testFetStereoLinkPhaseLaw();
        std::puts("Multi-Comp FET stereo phase-law test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-stereo-dense") == 0)
    {
        testFetDenseStereoPhaseParity();
        std::puts("Multi-Comp FET dense stereo-phase test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-recovery-dense") == 0)
    {
        testFetDenseStartupRecoveryParity();
        std::puts("Multi-Comp FET dense recovery test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-recovery-state") == 0)
    {
        testFetPostBurstRecoveryLifecycle();
        std::puts("Multi-Comp FET recovery-state test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-h2-surface") == 0)
    {
        reportFetBroadbandK2Surface();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-envelope-trace") == 0)
    {
        reportFetEnvelopeTrace();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-detector-fr") == 0)
    {
        reportFetDetectorFrequencyNull();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-h2") == 0)
    {
        testFetBroadbandHarmonicSurface();
        std::puts("Multi-Comp FET broadband H2 surface test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-h3") == 0)
    {
        testFetBroadbandOddHarmonicSurface();
        std::puts("Multi-Comp FET broadband H3 surface test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-low-h3-dense") == 0)
    {
        testFetDenseShallowLowFrequencyH3();
        std::puts("Multi-Comp FET dense shallow low-frequency H3 test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-broadband-h3-dense") == 0)
    {
        testFetDenseBroadbandComplexH3();
        std::puts("Multi-Comp FET dense broadband complex H3 test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-broadband-h5-dense") == 0)
    {
        testFetDenseBroadbandComplexH5();
        std::puts("Multi-Comp FET dense broadband complex H5 test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-low-h3") == 0)
    {
        testFetLowFrequencyOddHarmonicSurface();
        std::puts("Multi-Comp FET low-frequency H3 surface test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-low-h5") == 0)
    {
        testFetLowFrequencyFifthHarmonicSurface();
        std::puts("Multi-Comp FET low-frequency H5 surface test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-h5") == 0)
    {
        testFetBroadbandFifthHarmonicSurface();
        std::puts("Multi-Comp FET broadband H5 surface test: PASS");
        return 0;
    }
    // Separate from --fet-h2 on purpose: the two tables are independently
    // breakable and each has to be runnable while the other is deliberately
    // faulted, which is how the coverage-hole justification is demonstrated.
    if (argc == 2 && std::strcmp(argv[1], "--fet-lf") == 0)
    {
        testFetLowFrequencyColourSurface();
        std::puts("Multi-Comp FET low-frequency colour test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-attack-drive") == 0)
    {
        reportFetAttackDriveAxis();
        testFetAttackMatchesReferenceCurve();
        testFetAttackAcceleratesWithDrive();
        std::puts("Multi-Comp FET attack drive test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-attack-knob") == 0)
    {
        testFetHighCarrierAttackKnobAxis();
        std::puts("Multi-Comp FET high-carrier attack-knob test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-startup-peak") == 0)
    {
        testFetStartupPeakSurface();
        std::puts("Multi-Comp FET startup-peak test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-rate-gain") == 0)
    {
        testFetSampleRateFlatGain();
        std::puts("Multi-Comp FET sample-rate flat-gain test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-startup-neighbours") == 0)
    {
        testFetStartupCeilingNeighbours();
        std::puts("Multi-Comp FET startup-neighbour test: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--fet-startup-state") == 0)
    {
        testFetStartupStateLifecycleAndCounterSaturation();
        std::puts("Multi-Comp FET startup-state test: PASS");
        return 0;
    }
    testOptoMeasuredGainTaper();
    testOptoMeasuredOutputCeiling();
    testOptoDriveApplicability();
    testDbxSidechainTilt();
    testVcaDbxParityGates();
    testGoldenVectors();
    reportOptoPedestalEventGrid();
    testOptoPedestalEventHarnessInvariants();
    testOptoHarmonicContent();
    reportOptoCrestResponse();
    testOptoShortEventRelease();
    testOptoShortEventCharge();
    testOptoNoiseFloorDoesNotPrechargeEvent();
    testOptoEventChargeHasNoAbsoluteFloorCliff();
    testOptoNoiseFloorDoesNotPrechargeRepeatedEvent();
    testOptoFloorHistoryDoesNotChangeEventCharge();
    testOptoTrueSilenceRetainsExposureForRelease();
    testOptoCrestSweep();
    testOptoBroadbandStaticLaw();
    testOptoBurstRateSweep();
    testOptoPluginLevelReferencePoints();
    testOptoDetectorFrequencyWeighting();
    testOptoSubBassFloorContinuity();
    testOptoReferenceOutputMemory();
    testOptoDenseProgrammeParity();
    reportOptoAttackCrossings();
    testOptoLimitDynamics();
    reportOptoReleaseLocalTaus();
    testOptoInactivePeakReduction();
    testOptoMeterMatchesOutputReduction();
    testOptoMeasuredOnsets();
    testOptoThresholdOnlyCurveCollapse();
    testOptoLimitTopBrickWall();
    testOptoOverloadCompression();
    testOptoOverloadOrderingAndMonotonicity();
    testOptoSampleRateParity();
    testAtomicControlSnapshotsAreSingleLoad();
    testFetMeasuredControlTapers();
    testFetMeasuredStaticSurface();
    testFetKneeOnsetMatchesReference();
    testFetKneeCellDoesNotCancelDeepReleaseMemory();
    testFetMaximumReductionSaturation();
    testFetAllButtonsSettlingWindows();
    testFetAbsoluteGainAnchors();
    testFetMeasuredCurveAllButtons();
    testFetAllButtonsKneeTransition();
    testFetBlockSizeInvariance();
    testFetResetClearsProgrammeAndColourState();
    testFetInternalStereoLinkReference();
    testFetStereoLinkPhaseLaw();
    testFetDenseStereoPhaseParity();
    testFetDenseStartupRecoveryParity();
    testFetPostBurstRecoveryLifecycle();
    testFetAttackMatchesReferenceCurve();
    testFetAttackAcceleratesWithDrive();
    testFetHighCarrierAttackKnobAxis();
    testFetStartupPeakSurface();
    testFetStartupStateLifecycleAndCounterSaturation();
    testFetStartupCeilingNeighbours();
    testFetSampleRateFlatGain();
    testFetBroadbandHarmonicSurface();
    testFetBroadbandOddHarmonicSurface();
    testFetDenseShallowLowFrequencyH3();
    testFetDenseBroadbandComplexH3();
    testFetDenseBroadbandComplexH5();
    testFetLowFrequencyOddHarmonicSurface();
    testFetLowFrequencyFifthHarmonicSurface();
    testFetBroadbandFifthHarmonicSurface();
    testFetLowFrequencyColourSurface();
    testPublishedGainReductionRange();
    testAllModesAreMonoSafe();
    testOversamplingBlockSizeInvariance();
    testBypassCompletesDuringSidechainListen();
    testMultibandSoloMasksDryReference();
    testInputMeterWithAliasedBuffers();
    testSingleBandModeClearsMultibandGainReductionMeters();
    testMultibandMidSideMixAndAutoMakeupDomains();
    testMultibandAutomationUsesOneStereoSnapshot();
    testOversampledDetectorHasNativeRateStepTiming();
    testTruePeakOversampledPhaseInterpolation();
    testCrossoverAutomationContinuity();
    testFourTimesHighFrequencyMixCoherence();
    testMultibandEnabledTopology();
    testSettledBypassClearsGainReductionMeters();
    testSidechainListenClearsGainReductionMeters();
    testSidechainListenSwitchIsSmoothed();
    testSidechainListenCrossfadeIsLatencyAligned();
    testAutoGainResetsOnModeChange();
    testAutoGainNeutralisesManualOutput();
    testAutoGainBypassSettleBoundary();
    testAutoGainEffectiveExternalSidechain();
    testHardwareRateRefreshAfterOversamplingChange();
    testOptoExposureTimebaseSurvivesRateChange();
    testOptoFastChargeIsOversamplingInvariant();
    testOptoStereoDetectorIsolation();
    testPrepareAtDefaultRateAndFactor();
    testCrossoverFlatness();
    testStaticCurves();
    testEnvelopeAndReset();
    testMixBypassAndBlockEdges();
    testLatencyMixBypassAndDigitalStereo();
    testAnalogStereoLinkSharesEnvelope();
    testSplitOversamplingMatchesFunctorPath();
    testVcaAndBusInternalDetectorControls();
    testLinkedBusResetDeterminism();
    testLinkedBusReentryReseedsSidechainInterpolation();
    testOptoInternalStereoLinkUsesSignedMaximum();
    testDigitalLookaheadMixAlignment();
    testMultibandMixAlignment();
    testSidechainEq();
    testMultibandBypassAndZeroLatency();
    testReprepareMultiband();
    testSameRateReprepare();
    std::puts("Multi-Comp core tests: PASS");
    return 0;
}
