// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// BUS output-stage regression gates: the dry/wet Mix control, and the output
// ceiling under COMPRESSION. Both replay the native reference programme through
// the core BUS path and score it exactly as the capture scripts scored the
// native renders. See MultiCompBusOutputFixtures.hpp for provenance.
#include "../MultiCompDSP.hpp"
#include "MultiCompBusOutputFixtures.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace {
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;
constexpr double pi = 3.14159265358979323846;
constexpr int rate = 48000;
constexpr int segment = 5 * rate;      // one input level per 5 s segment
constexpr int segments = 4;
constexpr int windowOffset = 144000;   // settled part of each segment
constexpr int windowLength = 38400;
constexpr double levelsDb[] = {-30, -18, -6, -0.1};

void require(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Rebuilds the native source programme; verified bit-identical to the
// high-output.f32 the native renders consumed.
std::vector<float> programme() {
    std::vector<float> signal(size_t(segments) * segment);
    for (int i = 0; i < segments * segment; ++i)
        signal[size_t(i)] = float(std::pow(10., levelsDb[i / segment] / 20)
            * std::sin(2 * pi * 997 * (double(i) / rate) + .37));
    return signal;
}

// The native capture settings shared by every row: 10:1, threshold +15,
// attack index 5, release index 3, internal detection, no SC filter, 2x.
void configure(DSP& dsp, int headroom, int makeup, int mix) {
    dsp.setMode(3); dsp.setOversampling(1); dsp.setExternalSidechain(false);
    dsp.setStereoLink(100); dsp.setBypass(false);
    dsp.setParameter(P::NoiseEnable, 0); dsp.setParameter(P::TruePeakEnable, 0);
    dsp.setParameter(P::AutoMakeup, 0); dsp.setParameter(P::GlobalSidechainListen, 0);
    dsp.setParameter(P::BusThreshold, 15); dsp.setParameter(P::BusRatio, 2);
    dsp.setParameter(P::BusAttack, 5); dsp.setParameter(P::BusRelease, 3);
    dsp.setParameter(P::SidechainHP, 0);
    dsp.setParameter(P::BusHeadroom, float(headroom));
    dsp.setParameter(P::BusMakeup, float(makeup));
    dsp.setParameter(P::BusMix, float(mix));
    dsp.prepare(rate, 256);
}

std::vector<float> render(DSP& dsp, const std::vector<float>& main) {
    std::vector<float> result(main.size()); float right[256];
    for (size_t pos = 0; pos < main.size(); pos += 256) {
        const int n = int(std::min(size_t(256), main.size() - pos));
        const float* in[] = {main.data() + pos, main.data() + pos};
        float* out[] = {result.data() + pos, right};
        dsp.processBlock(in, out, 2, n);
    }
    return result;
}

void warm(DSP& dsp) {
    const std::vector<float> silence(5 * rate);
    render(dsp, silence);
}

double bandGain(const std::vector<float>& input, const std::vector<float>& output,
                int index, int latency) {
    const int begin = index * segment + windowOffset;
    double x = 0, y = 0;
    for (int i = 0; i < windowLength; ++i) {
        const double a = input[size_t(begin + i)];
        const double b = output[size_t(begin + i + latency)];
        x += a * a; y += b * b;
    }
    return x > 0 && y > 0 ? 10 * std::log10(y / x) : -INFINITY;
}

double peakOf(const std::vector<float>& audio) {
    double peak = 0;
    for (float value : audio) peak = std::max(peak, double(std::abs(value)));
    return peak;
}

// Native Mix is a plain crossfade between the untouched input and the fully
// processed wet signal, applied last. Makeup, the output ceiling and the
// transformer/convolution colour therefore belong to the wet path only, and
// reordering any of them against the mix breaks one of the four checks here.
void mixParity() {
    const auto input = programme();
    std::vector<std::vector<float>> rendered;
    double worstGain = 0, worstPeak = 0;
    int latency = 0;
    for (const auto& row : busoutput::mix) {
        DSP dsp; configure(dsp, 6, 15, row.mix); warm(dsp);
        const auto output = render(dsp, input);
        latency = dsp.getLatencySamples();
        for (int i = 0; i < segments; ++i) {
            const double value = bandGain(input, output, i, latency);
            const double error = std::abs(value - row.gain[i]);
            worstGain = std::isfinite(error) ? std::max(worstGain, error) : INFINITY;
            std::printf("BUS mix %d HR28 makeup+15 input %.1f dBFS: gain %.6f dB, error %.6f dB\n",
                row.mix, levelsDb[i], value, error);
        }
        const double peak = peakOf(output);
        const double peakError = std::abs(peak / row.peak - 1);
        worstPeak = std::max(worstPeak, peakError);
        std::printf("BUS mix %d peak %.6f against native %.6f: error %.6f%%\n",
            row.mix, peak, row.peak, 100 * peakError);
        rendered.push_back(output);
    }
    // Mix=0 is unity even at HR28 with +15 dB makeup: the native render is
    // sample-identical to its input, and the candidate differs only by its
    // oversampling round trip.
    double unity = busoutput::mixNativeUnityDifference;
    for (size_t i = 0; i + size_t(latency) < input.size(); ++i)
        unity = std::max(unity, double(std::abs(rendered[0][i + size_t(latency)] - input[i])));
    // Mix=50 is half dry plus half wet sample by sample, so nothing downstream
    // of the crossfade re-scales, clips or colours the sum.
    double affine = 0;
    for (size_t i = 0; i < input.size(); ++i)
        affine = std::max(affine, double(std::abs(
            rendered[1][i] - .5 * (rendered[0][i] + rendered[2][i]))));
    std::printf("BUS mix worst %.6f dB, peak %.6f%%, unity difference %.9f, affine difference %.9f\n",
        worstGain, 100 * worstPeak, unity, affine);
    require(worstGain < .2 && worstPeak < .01,
        "BUS dry/wet mix matches native UAD at Mix 0, 50 and 100");
    // 0.002722 measured: a 0.02-sample group-delay residual of the oversampling
    // round trip, not a level error (the four Mix=0 band gains agree with the
    // native 0 dB to 0.000121 dB). Applying makeup or the ceiling to the dry
    // path instead moves this by three orders of magnitude.
    require(unity < 5e-3, "BUS Mix=0 passes the input through at unity, after makeup and the ceiling");
    require(affine < 1e-6, "BUS Mix=50 is exactly half dry plus half wet");
}

// The output ceiling limits an internal magnitude of 3.3 after makeup and
// inside headroom compensation. The saturation matrix only reaches it on a
// silent sidechain; these rows are compressed, where an absent or misplaced
// ceiling costs 2 dB of gain and more than triples the peak at HR28/+15.
void ceilingParity() {
    const auto input = programme();
    double worstGain = 0, worstPeak = 0;
    for (const auto& row : busoutput::ceiling) {
        DSP dsp; configure(dsp, row.headroom, row.makeup, 100); warm(dsp);
        const auto output = render(dsp, input);
        const int latency = dsp.getLatencySamples();
        for (int i = 0; i < segments; ++i) {
            const double value = bandGain(input, output, i, latency);
            const double error = std::abs(value - row.gain[i]);
            worstGain = std::isfinite(error) ? std::max(worstGain, error) : INFINITY;
            std::printf("BUS compressed ceiling HR%d makeup%d input %.1f dBFS: gain %.6f dB, error %.6f dB\n",
                row.headroom * 4 + 4, row.makeup, levelsDb[i], value, error);
        }
        const double peak = peakOf(output);
        const double peakError = std::abs(peak / row.peak - 1);
        worstPeak = std::max(worstPeak, peakError);
        std::printf("BUS compressed ceiling HR%d makeup%d peak %.6f against native %.6f: error %.6f%%\n",
            row.headroom * 4 + 4, row.makeup, peak, row.peak, 100 * peakError);
    }
    std::printf("BUS compressed ceiling worst %.6f dB, peak %.6f%%\n", worstGain, 100 * worstPeak);
    require(worstGain < .35 && worstPeak < .009,
        "BUS compressed output ceiling matches native UAD at both headroom and makeup settings");
}

}
int main(int argc, char** argv) {
    if (argc == 2) {
        if (!std::strcmp(argv[1], "--mix")) { mixParity(); return 0; }
        if (!std::strcmp(argv[1], "--ceiling")) { ceilingParity(); return 0; }
        return 2;
    }
    mixParity(); ceilingParity();
    std::puts("Multi-Comp BUS output stage: PASS");
}
