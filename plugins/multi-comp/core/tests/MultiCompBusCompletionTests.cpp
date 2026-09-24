// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../MultiCompDSP.hpp"
#include "MultiCompBusCompletionFixtures.hpp"
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;
constexpr double pi = 3.14159265358979323846;
constexpr int rate = 48000;
void require(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void configure(DSP& dsp, bool external, int ratio, int os, float hp = 0, int hr = 3, int sampleRate = rate) {
    dsp.setMode(3); dsp.setOversampling(os); dsp.setExternalSidechain(external);
    dsp.setParameter(P::NoiseEnable, 0); dsp.setParameter(P::TruePeakEnable, 0);
    dsp.setParameter(P::AutoMakeup, 0); dsp.setParameter(P::BusMakeup, 0);
    dsp.setParameter(P::BusThreshold, 0); dsp.setParameter(P::BusRatio, float(ratio));
    dsp.setParameter(P::BusAttack, 0); dsp.setParameter(P::BusRelease, 0);
    dsp.setParameter(P::SidechainHP, hp); dsp.setParameter(P::BusHeadroom, float(hr));
    dsp.prepare(sampleRate, 256);
}
std::vector<float> render(DSP& dsp, const std::vector<float>& main,
                         const std::vector<float>& side, bool external, int channels) {
    std::vector<float> result(main.size()); float right[256];
    for (size_t pos = 0; pos < main.size(); pos += 256) {
        const int n = int(std::min(size_t(256), main.size() - pos));
        const float* in[] = {main.data() + pos, main.data() + pos};
        const float* sc[] = {side.data() + pos, side.data() + pos};
        float* out[] = {result.data() + pos, right};
        if (external) dsp.processBlockExternal(in, sc, out, channels, n);
        else dsp.processBlock(in, out, channels, n);
    }
    return result;
}
void warm(DSP& dsp, bool external, int channels, int sampleRate = rate) {
    const std::vector<float> silence(5 * sampleRate);
    render(dsp, silence, silence, external, channels);
}
void audioControlHarmonics() {
    // Archived reference console bus compressor LushDarkHall_sine1k.wav captures:
    // native latency (86 samples) removed, rectangular 1.000--1.500 s window.
    // Match render.cpp Render 3: reset, silent preroll, 2 s at -12 dBFS RMS.
    // This guards the audio-only smoother; feedback must remain unsmoothed.
    struct Reference {
        const char* name;
        float threshold;
        int ratio, release;
        std::array<double, 4> db; // H1 dBFS (peak FFT amplitude), then H3/H5/H7 dBc
    };
    constexpr Reference references[] = {
        {"r1_t-5", -5, 1, 0, {-27.6796, -52.920, -62.360, -70.414}},
        {"ultra-r2-a0-rel3", -15, 2, 3, {-40.6802, -62.166, -70.728, -77.379}}
    };
    std::array<std::array<double, 4>, 2> measured{};
    std::vector<float> tone(2 * rate);
    const double peak = std::sqrt(2.0) * std::pow(10.0, -12.0 / 20.0);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = float(peak * std::sin(2 * pi * 1000 * double(i) / rate));
    for (size_t row = 0; row < measured.size(); ++row) {
        const auto& ref = references[row];
        DSP dsp;
        configure(dsp, false, ref.ratio, 1); // OS index 1 = shipping 2x, HR index 3 = 16 dB
        dsp.setParameter(P::BusThreshold, ref.threshold);
        dsp.setParameter(P::BusRelease, float(ref.release));
        dsp.setParameter(P::BusMix, 100);
        dsp.setParameter(P::Mix, 100);
        dsp.reset();
        warm(dsp, false, 2);
        const auto output = render(dsp, tone, tone, false, 2);
        constexpr int count = rate / 2; // 500 complete cycles; no taper/window correction
        const int start = rate + dsp.getLatencySamples();
        std::array<double, 4> magnitude{};
        for (int h = 0; h < 4; ++h) {
            const int bin = 500 * (2 * h + 1);
            double real = 0, imag = 0;
            for (int i = 0; i < count; ++i) {
                const double phase = 2 * pi * bin * i / count;
                const double sample = output[size_t(start + i)];
                real += sample * std::cos(phase);
                imag -= sample * std::sin(phase);
            }
            magnitude[h] = std::hypot(real, imag);
            measured[row][h] = h == 0
                ? 20 * std::log10(2.0 * magnitude[0] / count)
                : 20 * std::log10(magnitude[h] / magnitude[0]);
            std::printf("BUS audio harmonics %s H%d: ours %.4f native %.4f delta %+.4f %s\n",
                ref.name, 2*h+1, measured[row][h], ref.db[h], measured[row][h]-ref.db[h],
                h == 0 ? "dBFS" : "dBc");
        }
    }
    std::fflush(stdout);
    // Separate checks identify the setting and harmonic. NaN/Inf fail the bounds.
    require(std::abs(measured[0][0] - references[0].db[0]) <= .3, "BUS r1_t-5 H1 within 0.3 dB of native");
    require(std::abs(measured[0][1] - references[0].db[1]) <= 1.0, "BUS r1_t-5 H3 within 1.0 dB of native");
    require(std::abs(measured[0][2] - references[0].db[2]) <= 1.5, "BUS r1_t-5 H5 within 1.5 dB of native");
    require(std::abs(measured[1][0] - references[1].db[0]) <= .3, "BUS ultra-r2-a0-rel3 H1 within 0.3 dB of native");
    require(std::abs(measured[1][1] - references[1].db[1]) <= 1.0, "BUS ultra-r2-a0-rel3 H3 within 1.0 dB of native");
    require(std::abs(measured[1][2] - references[1].db[2]) <= 1.5, "BUS ultra-r2-a0-rel3 H5 within 1.5 dB of native");
    // H7 is diagnostic only: the known native residual is +0.3..+2.5 dB.
}
double gain(const std::vector<float>& input, const std::vector<float>& output,
            int start, int count, int delay) {
    double x = 0, y = 0;
    for (int i = 0; i < count; ++i) {
        x += double(input[size_t(start+i)]) * input[size_t(start+i)];
        y += double(output[size_t(start+i+delay)]) * output[size_t(start+i+delay)];
    }
    return x > 0 && y > 0 ? 10 * std::log10(y/x) : INFINITY;
}
void frequencyParity(bool external) {
    constexpr double frequencies[] = {40,80,200,997,5000,15000,20000};
    std::vector<float> main(28 * rate), side(main.size());
    for (int i = 0; i < int(main.size()); ++i) {
        side[size_t(i)] = float(std::pow(10., -12./20) * std::sin(2*pi*frequencies[i/(4*rate)]*(i%(4*rate))/rate + .61));
        main[size_t(i)] = external ? float(std::pow(10., -36./20) * std::sin(2*pi*997*i/rate + .2)) : side[size_t(i)];
    }
    double worst = 0;
    for (const auto& row : buscompletion::frequency) {
        if (row.external != external) continue;
        for (int os : {0,1,2}) for (int channels : {1,2}) {
            DSP dsp; configure(dsp, external, row.ratio, os, row.hp, row.headroom); warm(dsp, external, channels);
            const auto output = render(dsp, main, side, external, channels);
            for (int f = 0; f < 7; ++f) {
                const double value = gain(main, output, f*4*rate + 120000, 48000, dsp.getLatencySamples());
                const double error = std::abs(value - row.gain[f]);
                worst = std::isfinite(error) ? std::max(worst, error) : INFINITY;
                std::printf("BUS %s frequency %.0f Hz HR%d HP%.0f r%d %dx %dch: %.6f dB error %.6f\n",
                    external ? "external" : "internal", frequencies[f], row.headroom*4+4, row.hp, row.ratio, 1<<os, channels, value, error);
            }
        }
    }
    std::printf("BUS %s frequency worst %.6f dB\n", external ? "external" : "internal", worst);
    require(worst < .35, "BUS detector frequency, filter and headroom match native UAD");
}
void levelParity() {
    constexpr double levels[] = {-48,-36,-24,-18,-12,-6};
    std::vector<float> main(30 * rate), side(main.size());
    for (int i = 0; i < int(main.size()); ++i) {
        side[size_t(i)] = float(std::pow(10., levels[i/(5*rate)]/20) * std::sin(2*pi*1000*i/rate + .37));
        main[size_t(i)] = float(std::pow(10., -36./20) * std::sin(2*pi*997*i/rate + .2));
    }
    double worst = 0;
    for (const auto& row : buscompletion::level) for (int channels : {1,2}) {
        DSP dsp; configure(dsp, true, row.ratio, 1); warm(dsp, true, channels);
        const auto output = render(dsp, main, side, true, channels);
        for (int l = 0; l < 6; ++l) {
            const double value = gain(main, output, l*5*rate + 144000, 38400, dsp.getLatencySamples());
            const double error = std::abs(value-row.gain[l]);
            worst = std::isfinite(error) ? std::max(worst,error) : INFINITY;
            std::printf("BUS independent sidechain r%d %dch level %.0f: gain %.6f, error %.6f dB\n", row.ratio, channels, levels[l], value, error);
        }
    }
    std::printf("BUS external level worst %.6f dB\n", worst);
    require(worst < .2, "BUS independent sidechain follows measured UAD compression law");
}
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
void dynamicsParity() {
    std::vector<float> main(20*rate), side(main.size());
    for (int i = 0; i < int(main.size()); ++i) {
        const double t = double(i)/rate;
        const double db = (t >= 2 && t < 2.09) || (t >= 11 && t < 12.4) ? -18 : t >= 6 && t < 6.35 ? -9 : -54;
        side[size_t(i)] = float(std::pow(10.,db/20)*std::sin(2*pi*1000*t+.37));
        main[size_t(i)] = float(std::pow(10.,-36./20)*std::sin(2*pi*1000*t+.2));
    }
    double worst = 0, squared = 0; int count = 0;
    for (const auto& row : buscompletion::dynamic) {
        DSP dsp; configure(dsp, true, row.ratio, 1);
        dsp.setParameter(P::BusAttack, float(row.attack)); dsp.setParameter(P::BusRelease, float(row.release));
        warm(dsp, true, 2); const auto output = render(dsp, main, side, true, 2);
        double rowWorst = 0;
        for (int i = 0; i < 30; ++i) {
            const double value = 20*std::log10(carrier(output, buscompletion::timesMs[i]*48+dsp.getLatencySamples())[0]/std::pow(10.,-36./20));
            const double error = std::abs(value-row.gain[i]);
            rowWorst = std::isfinite(error) ? std::max(rowWorst,error) : INFINITY;
            squared += error*error; ++count;
        }
        worst = std::max(worst,rowWorst);
        std::printf("BUS external dynamics r%d a%d rel%d: worst %.6f dB\n", row.ratio,row.attack,row.release,rowWorst);
    }
    const double rms = std::sqrt(squared/count);
    std::printf("BUS external dynamics %d points: RMS %.6f, worst %.6f dB\n",count,rms,worst);
    require(worst < .7 && rms < .2, "BUS external fixed and Auto attack/recovery match native UAD");
}
void saturationParity() {
    std::vector<float> main(32*rate), side(main.size(), 0);
    for (int i = 0; i < int(main.size()); ++i)
        main[size_t(i)] = float(std::pow(10.,(-30+3*(i/(2*rate)))/20.) * std::sin(2*pi*1000*i/rate+.37));
    double worstGain = 0, worstPeak = 0, worstEven = 0, worstWeakOdd = 0, worstStrongOdd = 0;
    // Below the ceiling the native adds a weak x - a*x^7 - b*x^6: at 0.868 of
    // the clip its H2/H3 are about -88/-77 dBc. Those are far inside the
    // absolute colour limits, which a bare hard clamp with no such term also
    // passes, so score them in dB wherever the reference sits between the
    // capture floor and the clip. Measured 2026-09-10: worst 0.042 dB. The
    // bound catches a ~3% change of either coefficient; no term misses by 50+ dB.
    double worstSubCeiling = 0; int subCeilingCells = 0;
    for (const auto& row : buscompletion::saturation) {
        DSP dsp; configure(dsp, true, 2, 1, 0, row.headroom);
        dsp.setParameter(P::BusThreshold, 15); dsp.setParameter(P::BusAttack, 5);
        dsp.setParameter(P::BusRelease, 3); dsp.setParameter(P::BusMakeup, float(row.makeup));
        warm(dsp, true, 2); const auto output = render(dsp, main, side, true, 2);
        for (int level = 0; level < 16; ++level) {
            const auto& reference = row.points[level];
            double power = 0, peak = 0, real[3]{}, imag[3]{};
            const int start = level*2*rate+rate+dsp.getLatencySamples();
            constexpr int count = 43200;
            for (int i = 0; i < count; ++i) {
                const double value = output[size_t(start+i)];
                power += value*value; peak = std::max(peak, std::abs(value));
                for (int h = 0; h < 3; ++h) {
                    const double phase = 2*pi*1000*(h+1)*i/rate;
                    real[h] += value*std::cos(phase); imag[h] += value*std::sin(phase);
                }
            }
            const double fundamental = std::hypot(real[0],imag[0]);
            const double even = std::hypot(real[1],imag[1])/fundamental;
            const double odd = std::hypot(real[2],imag[2])/fundamental;
            const double gainError = std::abs(20*std::log10(std::sqrt(power/count)/reference.rms));
            const double peakError = std::abs(peak/reference.peak-1);
            const double evenError = std::abs(even-std::pow(10.,reference.h2/20));
            const double oddDb = 20*std::log10(std::max(odd,1e-30));
            worstGain = std::isfinite(gainError) ? std::max(worstGain,gainError) : INFINITY;
            worstPeak = std::max(worstPeak,peakError); worstEven = std::max(worstEven,evenError);
            if (reference.h3 > -60) worstStrongOdd = std::max(worstStrongOdd,std::abs(oddDb-reference.h3));
            else worstWeakOdd = std::max(worstWeakOdd,std::abs(odd-std::pow(10.,reference.h3/20)));
            if (reference.h3 <= -60 && reference.h3 > -100) {
                const double evenDb = 20*std::log10(std::max(even,1e-30));
                worstSubCeiling = std::max(worstSubCeiling,
                    std::max(std::abs(evenDb-reference.h2),std::abs(oddDb-reference.h3)));
                ++subCeilingCells;
            }
            std::printf("BUS output HR%d makeup%d input%d: gain-error %.6f dB peak-error %.6f%% H2 %.6f H3 %.6f dBc\n",
                row.headroom*4+4,row.makeup,-30+3*level,gainError,100*peakError,20*std::log10(std::max(even,1e-30)),oddDb);
        }
    }
    const bool levelOk = worstGain < .05 && worstPeak < .01;
    const bool colourOk = worstEven < .00005 && worstWeakOdd < .0002 && worstStrongOdd < .3;
    const bool subCeilingOk = subCeilingCells > 0 && worstSubCeiling < .25;
    std::printf("BUS saturation level %s: gain %.6f dB, peak %.6f%%; colour %s: H2 %.9f weak H3 %.9f clipped H3 %.6f dB\n",
        levelOk?"PASS":"FAIL",worstGain,100*worstPeak,colourOk?"PASS":"FAIL",worstEven,worstWeakOdd,worstStrongOdd);
    std::printf("BUS sub-ceiling colour %s: %d cells, worst H2/H3 %.6f dB\n",
        subCeilingOk?"PASS":"FAIL",subCeilingCells,worstSubCeiling);
    require(levelOk && colourOk, "BUS clean path and headroom-dependent saturation match native UAD");
    require(subCeilingOk, "BUS sub-ceiling x^7/x^6 colour matches native UAD in dB");
}

void rateParity() {
    double worst = 0, squared = 0; int count = 0;
    for (const auto& row : buscompletion::rates) {
        const int sr = row.rate;
        std::vector<float> main(16*sr), side(main.size());
        for (int i = 0; i < int(main.size()); ++i) {
            const double t = double(i)/sr;
            const double db = t>=1 && t<1.12 ? -18 : t>=5 && t<5.45 ? -9 : t>=10 && t<11.1 ? -15 : -60;
            side[size_t(i)] = float(std::pow(10.,db/20)*std::sin(2*pi*997*t+.63));
            main[size_t(i)] = float(std::pow(10.,-36./20)*std::sin(2*pi*1000*t+.19));
        }
        DSP dsp; configure(dsp, true, row.ratio, 1, 0, 3, sr);
        dsp.setParameter(P::BusAttack,float(row.attack)); dsp.setParameter(P::BusRelease,float(row.release));
        dsp.setParameter(P::BusThreshold,row.threshold);
        warm(dsp,true,2,sr); const auto output = render(dsp,main,side,true,2);
        double rowWorst = 0;
        for (int i = 0; i < 30; ++i) {
            const double value = 20*std::log10(carrier(output,row.sample[i]+dsp.getLatencySamples(),
                int(std::lround(.004*sr)),1000,sr)[0]/std::pow(10.,-36./20));
            const double error = std::abs(value-row.gain[i]);
            rowWorst = std::isfinite(error)?std::max(rowWorst,error):INFINITY;
            squared += error*error; ++count;
        }
        worst = std::max(worst,rowWorst);
        std::printf("BUS external %d Hz r%d a%d rel%d threshold%.1f: worst %.6f dB\n",sr,row.ratio,row.attack,row.release,row.threshold,rowWorst);
    }
    const double rms = std::sqrt(squared/count);
    std::printf("BUS external rate holdouts %d points: RMS %.6f worst %.6f dB\n",count,rms,worst);
    require(worst < .5 && rms < .25,"BUS sidechain charging and recovery match independent native rates/levels");
}

void filterModeSwitch(bool reverse = false) {
    std::vector<float> main(rate), side(rate);
    for (int i=0;i<rate;++i) {
        main[size_t(i)]=float(.02*std::sin(2*pi*997*i/rate));
        side[size_t(i)]=float(.5*std::sin(2*pi*40*i/rate));
    }
    for (int target : {reverse?3:1,reverse?1:3}) {
        DSP switched, fresh;
        configure(switched,true,1,1,80); configure(fresh,true,1,1,20);
        switched.setMode(target==3?1:3);
        // Force a frequency redesign before testing a same-frequency mode
        // change. Otherwise a broken cache can leave both rigs equally wrong.
        switched.setParameter(P::SidechainHP,200);
        render(switched,main,side,true,2);
        switched.setParameter(P::SidechainHP,80);
        render(switched,main,side,true,2);
        switched.reset(); switched.setMode(target); fresh.setMode(target);
        render(fresh,main,side,true,2);
        fresh.reset(); fresh.setParameter(P::SidechainHP,80);
        const auto actual=render(switched,main,side,true,2), expected=render(fresh,main,side,true,2);
        double peak=0, difference=0;
        for (size_t i=0;i<actual.size();++i) {
            peak=std::max(peak,double(std::abs(expected[i])));
            difference=std::max(difference,double(std::abs(actual[i]-expected[i])));
        }
        std::printf("BUS filter ownership entering mode %d: peak %.9f, difference %.9f\n",target,peak,difference);
        require(peak>.001 && difference<1e-7,"switching BUS filter shape preserves both BUS and sibling filter ownership");
    }
}

void externalResetHistory(bool prepareAgain) {
    DSP active, fresh; configure(active,true,2,0); configure(fresh,true,2,0);
    active.setParameter(P::BusThreshold,15);
    std::vector<float> main(rate), charged(rate,.25f), quiet(rate,0);
    for (int i=0;i<rate;++i) main[size_t(i)]=float(.02*std::cos(2*pi*997*i/rate));
    render(active,main,charged,true,2);
    if (prepareAgain) { active.prepare(rate,256); active.prepare(rate,256); }
    else active.reset();
    active.setParameter(P::BusThreshold,-5); fresh.setParameter(P::BusThreshold,-5);
    const auto actual=render(active,main,quiet,true,2), expected=render(fresh,main,quiet,true,2);
    double peak=0,difference=0;
    for (size_t i=0;i<actual.size();++i) {
        peak=std::max(peak,double(std::abs(expected[i])));
        difference=std::max(difference,double(std::abs(actual[i]-expected[i])));
    }
    std::printf("BUS external charged input %s: peak %.9f, difference %.9f\n",prepareAgain?"prepare":"reset",peak,difference);
    require(peak>.01 && difference<1e-7,"BUS lifecycle clears the virtual sidechain sample before a native-rate restart");
}

}
int main(int argc, char** argv) {
    if (argc == 2) {
        if (!std::strcmp(argv[1], "--external-reset")) { externalResetHistory(false); return 0; }
        if (!std::strcmp(argv[1], "--external-prepare")) { externalResetHistory(true); return 0; }
        if (!std::strcmp(argv[1], "--filter-switch-reverse")) { filterModeSwitch(true); return 0; }
        if (!std::strcmp(argv[1], "--filter-switch")) { filterModeSwitch(); return 0; }
        if (!std::strcmp(argv[1], "--rates")) { rateParity(); return 0; }
        if (!std::strcmp(argv[1], "--saturation")) { saturationParity(); return 0; }
        if (!std::strcmp(argv[1], "--level")) { levelParity(); return 0; }
        if (!std::strcmp(argv[1], "--internal-filter")) { frequencyParity(false); return 0; }
        if (!std::strcmp(argv[1], "--external-frequency")) { frequencyParity(true); return 0; }
        if (!std::strcmp(argv[1], "--dynamics")) { dynamicsParity(); return 0; }
        return 2;
    }
    audioControlHarmonics();
    levelParity(); frequencyParity(false); frequencyParity(true); dynamicsParity(); saturationParity(); rateParity(); filterModeSwitch(); externalResetHistory(false); externalResetHistory(true);
    std::puts("Multi-Comp BUS completion: PASS");
}
