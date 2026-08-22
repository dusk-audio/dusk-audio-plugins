#include "../MultiCompDSP.hpp"
#include "../../../shared-daf/dsp/DuskCrossover.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using duskaudio::DuskCrossover;
using duskaudio::MultiCompDSP;

namespace
{
constexpr float kPi = duskaudio::kDuskPi;

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
                                  float frequencyHz = 997.0f)
{
    constexpr int kSamples = 24000;
    constexpr int kMeasureSamples = 4096;
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
    dsp.setParameter(MultiCompDSP::Parameter::OptoGain, duskaudio::optoGainDbToKnob(0.0f));
    dsp.setParameter(MultiCompDSP::Parameter::OptoLimit, limit ? 1.0f : 0.0f);
    dsp.prepare(48000.0, kBlockSize);
    const float amplitude = duskaudio::decibelsToGain(inputDbfs);
    double sum = 0.0;
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    for (int blockStart = 0; blockStart < kSamples; blockStart += kBlockSize)
    {
        const int count = std::min(kBlockSize, kSamples - blockStart);
        for (int i = 0; i < count; ++i)
            input[static_cast<size_t>(i)] = amplitude * std::sin(
                2.0f * kPi * frequencyHz * static_cast<float>(blockStart + i) / 48000.0f);
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
                          float frequencyHz = 997.0f)
{
    // Reference definition: output(PR=0) minus output(PR=x), at identical
    // input level and Gain.  Input-minus-output is intentionally never used.
    const float baseline = renderOptoStatic(inputDbfs, 0.0f, limit, frequencyHz).rms;
    const float reduced = renderOptoStatic(inputDbfs, peakReduction, limit, frequencyHz).rms;
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
    // The broadband offset belongs to the later colouration phase. This gate
    // removes its mean and judges only the measured detector-weighting shape.
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
    std::printf("opto detector weighting summary: mean offset %+.6f dB shape RMS %.6f dB\n",
                meanDelta, shapeRms);
    require(shapeRms < 0.12f,
            "Opto detector weighting shape survives after removing broadband offset");
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

void prepareOptoDynamicsDsp(MultiCompDSP& dsp, float peakReduction)
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
    dsp.prepare(48000.0, 256);
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
    // The pinned residuals are the rounding-robust silence-gate trajectory
    // (identical across platforms and fp-contract modes to <0.001 dB):
    // -0.20 / -0.26 / +0.71 (held out) / -1.28 dB. Against the reference it
    // trades the old macOS-FMA-only trajectory's +1.86 dB held-out error for
    // -1.28 dB on the 23.8 dB crest row; the four-row RMS improves 0.96 ->
    // 0.75 dB. The remaining shape error is the same open structural defect.
    require(finiteAndCorrectCrest && nonMonotonicShape
                && fittedRmsError < 0.85f && fittedWorstError < 1.40f
                && std::abs(heldOutError) < 1.10f,
            "Opto linked-stereo crest sweep preserves its measured shape and bounded residual");
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

float measureOptoDetectorMemory(int gapMs)
{
    constexpr int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr int kBurstSamples = 300 * kSampleRate / 1000;
    constexpr int kProbeSamples = 300 * kSampleRate / 1000;
    constexpr int kMeasureSamples = 4 * kSampleRate / 1000;
    const int gapSamples = gapMs * kSampleRate / 1000;
    const int totalSamples = kBurstSamples + gapSamples + kProbeSamples;
    MultiCompDSP control;
    MultiCompDSP active;
    prepareOptoDynamicsDsp(control, 0.0f);
    prepareOptoDynamicsDsp(active, 70.0f);
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> controlOutput{};
    std::array<float, kBlockSize> activeOutput{};
    double controlPower = 0.0;
    double activePower = 0.0;
    double controlSum = 0.0;
    double activeSum = 0.0;
    for (int blockStart = 0; blockStart < totalSamples;)
    {
        const int probeStart = kBurstSamples + gapSamples;
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
            else if (sample >= kBurstSamples + gapSamples)
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
            const int probeSample = blockStart + i - probeStart;
            if (probeSample >= 0 && probeSample < kMeasureSamples)
            {
                controlPower += static_cast<double>(controlOutput[static_cast<size_t>(i)])
                    * controlOutput[static_cast<size_t>(i)];
                activePower += static_cast<double>(activeOutput[static_cast<size_t>(i)])
                    * activeOutput[static_cast<size_t>(i)];
                controlSum += controlOutput[static_cast<size_t>(i)];
                activeSum += activeOutput[static_cast<size_t>(i)];
            }
        }
        blockStart += count;
    }
    // Remove the window mean so the detector-memory gate measures the probe
    // carrier, not the unrelated colour chain's level-step DC transient.
    const double controlAcPower = controlPower
        - controlSum * controlSum / static_cast<double>(kMeasureSamples);
    const double activeAcPower = activePower
        - activeSum * activeSum / static_cast<double>(kMeasureSamples);
    return 10.0f * std::log10(static_cast<float>(controlAcPower / activeAcPower));
}

void testOptoDetectorMemory()
{
    const float burstStaticReduction = measureOptoStaticGr(-6.0f, 70.0f, false);
    std::printf("opto detector memory: burst static-law reduction %.6f dB\n",
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
        const float measured = measureOptoDetectorMemory(gapsMs[row]);
        const float delta = measured - reference[row];
        std::printf("opto detector memory: gap %d ms reference %.3f dB "
                    "measured %.6f dB delta %+.6f dB\n",
                    gapsMs[row], reference[row], measured, delta);
        squaredError += delta * delta;
        worstError = std::max(worstError, std::abs(delta));
    }
    const float rmsError = std::sqrt(squaredError / static_cast<float>(gapsMs.size()));
    std::printf("opto detector memory: RMS error %.6f dB worst %.6f dB\n",
                rmsError, worstError);
    // The retained 64/185/1174 ms populations match at probe start; the
    // remaining local residual is the same cell decay averaged into the
    // prescribed first 4 ms output probe.  Keep a narrow bound around that
    // measured extraction rather than moving the physical release constants.
    require(rmsError < 0.125f && worstError < 0.26f,
            "Opto three-population memory matches the corrected sixteen-point curve");
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
            dsp.setParameter(MultiCompDSP::Parameter::VcaRatio, 10.0f);
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
    bool matches = true;
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
            matches = matches
                && std::abs(reduction - referenceReduction[row][ch]) < 0.5f;
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
        matches = matches && worstDeltaDb < 1.0e-5f;
    }
    require(matches,
            "Opto internal stereo link uses the signed maximum detector on both channels");
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
    constexpr duskaudio::MultiCompMode modes[] = {
        duskaudio::MultiCompMode::FET, duskaudio::MultiCompMode::VCA,
        duskaudio::MultiCompMode::Bus, duskaudio::MultiCompMode::StudioFET,
        duskaudio::MultiCompMode::StudioVCA, duskaudio::MultiCompMode::Digital,
        duskaudio::MultiCompMode::Multiband};
    // Re-recorded 2026-08-19: affected hardware modes encoded stale oversampling-rate coefficients.
    constexpr float expectedRms[] = {0.610338330f, 0.162082925f, 0.269918233f,
                                     0.618480802f, 0.195874527f, 0.173109755f, 0.212109938f};
    constexpr float expectedPeak[] = {1.912103295f, 0.350156724f, 0.797850311f,
                                      1.836098075f, 0.657691538f, 0.349556237f, 0.815853894f};
    std::puts("golden vectors: seven non-Opto modes, deterministic step/sine-burst RMS peak");
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

int main()
{
    testOptoMeasuredGainTaper();
    testOptoMeasuredOutputCeiling();
    testOptoDriveApplicability();
    testGoldenVectors();
    testOptoHarmonicContent();
    reportOptoCrestResponse();
    testOptoCrestSweep();
    testOptoBroadbandStaticLaw();
    testOptoBurstRateSweep();
    testOptoPluginLevelReferencePoints();
    testOptoDetectorFrequencyWeighting();
    testOptoDetectorMemory();
    reportOptoAttackCrossings();
    reportOptoReleaseLocalTaus();
    testOptoInactivePeakReduction();
    testOptoMeterMatchesOutputReduction();
    testOptoMeasuredOnsets();
    testOptoThresholdOnlyCurveCollapse();
    testOptoLimitTopBrickWall();
    testAtomicControlSnapshotsAreSingleLoad();
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
    testOptoStereoDetectorIsolation();
    testPrepareAtDefaultRateAndFactor();
    testCrossoverFlatness();
    testStaticCurves();
    testEnvelopeAndReset();
    testMixBypassAndBlockEdges();
    testLatencyMixBypassAndDigitalStereo();
    testAnalogStereoLinkSharesEnvelope();
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
