// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// Golden-signal calibration suite for the framework-free spectrum-analyzer
// core (GH #184, protocol in docs/dpf-migration/10-spectrum-analyzer.md).
//
// These tests LOCK the meters' current calibration so the DPF port cannot
// drift it: deterministic fixtures (mt19937 seed 0x5EED), three sample rates,
// fresh prepare()+reset() per case, fixed block feeding, outputs read at the
// same points. Compiled WITHOUT JUCE on purpose; that this file builds is
// itself the framework-freedom gate for core/.
//
// Documented convention this suite locks (do not "fix" silently): the LUFS
// meter averages the two channels' K-weighted mean square ((kL^2+kR^2)*0.5)
// where BS.1770 sums them, so a stereo sine reads ~3 dB lower than the
// ITU calibration would suggest. Changing that is a product decision with a
// changelog entry, not a port detail.

#include "../ChannelRouter.h"
#include "../CorrelationMeter.h"
#include "../FFTProcessor.h"
#include "../KSystemMeter.h"
#include "../LUFSMeter.h"
#include "../TruePeakDetector.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

static void checkNear(float got, float want, float tol, const std::string& what)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: got %.3f want %.3f +/- %.3f",
                  what.c_str(), (double)got, (double)want, (double)tol);
    check(std::abs(got - want) <= tol, buf);
}

// ---- fixtures ---------------------------------------------------------------

static std::vector<float> makeSine(double freq, float amp, double sr, int numSamples,
                                   double phase = 0.0)
{
    std::vector<float> v((size_t)numSamples);
    const double w = 2.0 * 3.14159265358979323846 * freq / sr;
    for (int i = 0; i < numSamples; ++i)
        v[(size_t)i] = amp * (float)std::sin(w * i + phase);
    return v;
}

static std::vector<float> makeNoise(float amp, int numSamples, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v((size_t)numSamples);
    for (auto& s : v) s = amp * dist(rng);
    return v;
}

// Feed a stereo pair through all continuous meters in fixed blocks.
struct MeterBank
{
    LUFSMeter lufs;
    KSystemMeter ksys;
    CorrelationMeter corr;
    TruePeakDetector tp;

    void prepare(double sr)
    {
        lufs.prepare(sr, 2);
        ksys.prepare(sr);
        corr.prepare(sr);
        tp.prepare(sr, 2);
    }

    void feed(const std::vector<float>& L, const std::vector<float>& R, int block)
    {
        const int n = (int)L.size();
        for (int at = 0; at < n; at += block)
        {
            const int len = std::min(block, n - at);
            const float* chans[2] = { L.data() + at, R.data() + at };
            lufs.process(L.data() + at, R.data() + at, len);
            ksys.process(L.data() + at, R.data() + at, len);
            corr.process(L.data() + at, R.data() + at, len);
            tp.process(chans, len);
        }
    }
};

// ---- cases ------------------------------------------------------------------

static void sineCase(double sr, int block)
{
    char tag[64];
    std::snprintf(tag, sizeof(tag), "[sine -18dBFS 1kHz sr=%d block=%d]", (int)sr, block);

    const float amp = std::pow(10.0f, -18.0f / 20.0f);
    const int n = (int)(sr * 10.0);
    auto s = makeSine(1000.0, amp, sr, n);

    MeterBank m;
    m.prepare(sr);
    m.feed(s, s, block);

    // RMS of a sine of amplitude A is A/sqrt(2): -21.01 dBFS. Exponential
    // 300 ms integration converges well inside 10 s.
    checkNear(m.ksys.getRmsDbL(), -21.01f, 0.1f, std::string(tag) + " K RMS L");
    checkNear(m.ksys.getRmsDbR(), -21.01f, 0.1f, std::string(tag) + " K RMS R");
    // K-14: level = RMS dB + 14.
    m.ksys.setType(KSystemMeter::Type::K14);
    checkNear(m.ksys.getKLevelMono(), -21.01f + 14.0f, 0.1f, std::string(tag) + " K-14 level");
    m.ksys.setType(KSystemMeter::Type::K20);
    checkNear(m.ksys.getKLevelMono(), -21.01f + 20.0f, 0.1f, std::string(tag) + " K-20 level");

    // LUFS under the implementation's channel-AVERAGING convention: the
    // -0.691 offset cancels the K-weighting gain near 1 kHz by design, so the
    // reading equals the channel-averaged RMS power: -18 - 3.01 = -21.0 LUFS.
    // (BS.1770's channel SUM would read -18.0; this implementation sits 3 dB
    // below it. Locked deliberately; see the header comment.)
    checkNear(m.lufs.getMomentaryLUFS(), -21.05f, 0.3f, std::string(tag) + " momentary LUFS");
    checkNear(m.lufs.getShortTermLUFS(), -21.05f, 0.3f, std::string(tag) + " short-term LUFS");
    checkNear(m.lufs.getIntegratedLUFS(), m.lufs.getMomentaryLUFS(), 0.3f,
              std::string(tag) + " integrated tracks momentary on steady tone");

    // Identical channels: correlation +1.
    checkNear(m.corr.getCorrelation(), 1.0f, 0.01f, std::string(tag) + " correlation");

    // True peak of a steady sine sits at the crest, within FIR ripple.
    checkNear(m.tp.getMaxTruePeakDB(), -18.0f, 0.35f, std::string(tag) + " true peak dBTP");
}

// The same sine must read the same regardless of feeding block size.
static void blockInvarianceCase(double sr)
{
    const float amp = std::pow(10.0f, -18.0f / 20.0f);
    const int n = (int)(sr * 10.0);
    auto s = makeSine(1000.0, amp, sr, n);

    float lufs[3], rms[3], corr[3];
    const int blocks[3] = { 512, 64, 483 };
    for (int b = 0; b < 3; ++b)
    {
        MeterBank m;
        m.prepare(sr);
        m.feed(s, s, blocks[b]);
        lufs[b] = m.lufs.getMomentaryLUFS();
        rms[b] = m.ksys.getRmsDbL();
        corr[b] = m.corr.getCorrelation();
    }
    char tag[64];
    std::snprintf(tag, sizeof(tag), "[block-invariance sr=%d]", (int)sr);
    checkNear(lufs[1], lufs[0], 0.05f, std::string(tag) + " LUFS 64 vs 512");
    checkNear(lufs[2], lufs[0], 0.05f, std::string(tag) + " LUFS 483 vs 512");
    checkNear(rms[1], rms[0], 0.05f, std::string(tag) + " RMS 64 vs 512");
    checkNear(rms[2], rms[0], 0.05f, std::string(tag) + " RMS 483 vs 512");
    checkNear(corr[1], corr[0], 0.005f, std::string(tag) + " corr 64 vs 512");
    checkNear(corr[2], corr[0], 0.005f, std::string(tag) + " corr 483 vs 512");
}

static void correlationCases(double sr)
{
    char tag[48];
    std::snprintf(tag, sizeof(tag), "[correlation sr=%d]", (int)sr);
    const int n = (int)(sr * 10.0);

    auto a = makeNoise(0.5f, n, 0x5EED);
    // Anti-correlated: right = -left.
    std::vector<float> neg = a;
    for (auto& x : neg) x = -x;
    // Independent: a different draw from the same seeded generator family.
    auto b = makeNoise(0.5f, n, 0x5EEDu + 1u);

    {
        MeterBank m; m.prepare(sr); m.feed(a, a, 512);
        checkNear(m.corr.getCorrelation(), 1.0f, 0.01f, std::string(tag) + " identical -> +1");
        check(m.corr.getSmoothedCorrelation() > 0.9f, std::string(tag) + " smoothed near +1");
    }
    {
        MeterBank m; m.prepare(sr); m.feed(a, neg, 512);
        checkNear(m.corr.getCorrelation(), -1.0f, 0.01f, std::string(tag) + " inverted -> -1");
    }
    {
        MeterBank m; m.prepare(sr); m.feed(a, b, 512);
        check(std::abs(m.corr.getCorrelation()) < 0.1f, std::string(tag) + " independent -> ~0");
    }
}

static void truePeakIntersampleCase(double sr)
{
    char tag[48];
    std::snprintf(tag, sizeof(tag), "[intersample sr=%d]", (int)sr);

    // Sine at fs/4 with a 45-degree phase offset: every sample lands at
    // +/- A/sqrt(2) while the continuous waveform still reaches A. Sample
    // peak reads ~-3 dB relative to A; a true-peak meter must read near A.
    const float amp = 0.999f;
    const int n = (int)(sr * 2.0);
    auto s = makeSine(sr / 4.0, amp, sr, n, 3.14159265358979323846 / 4.0);

    float samplePeak = 0.0f;
    for (float x : s) samplePeak = std::max(samplePeak, std::abs(x));
    const float samplePeakDb = 20.0f * std::log10(samplePeak);

    MeterBank m;
    m.prepare(sr);
    m.feed(s, s, 512);

    const float tpDb = m.tp.getMaxTruePeakDB();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s TP %.2f dBTP > sample peak %.2f dBFS + 2 dB",
                  tag, (double)tpDb, (double)samplePeakDb);
    check(tpDb > samplePeakDb + 2.0f, buf);
    checkNear(tpDb, 0.0f, 1.2f, std::string(tag) + " TP near true crest");
}

static void silenceCase(double sr)
{
    char tag[40];
    std::snprintf(tag, sizeof(tag), "[silence sr=%d]", (int)sr);
    const int n = (int)(sr * 30.0);
    std::vector<float> z((size_t)n, 0.0f);

    MeterBank m;
    m.prepare(sr);
    m.feed(z, z, 512);

    checkNear(m.lufs.getMomentaryLUFS(), -100.0f, 0.001f, std::string(tag) + " momentary floor");
    checkNear(m.lufs.getShortTermLUFS(), -100.0f, 0.001f, std::string(tag) + " short-term floor");
    checkNear(m.lufs.getIntegratedLUFS(), -100.0f, 0.001f, std::string(tag) + " integrated floor");
    checkNear(m.ksys.getRmsDbL(), -100.0f, 0.001f, std::string(tag) + " RMS floor");
    checkNear(m.corr.getCorrelation(), 0.0f, 0.001f, std::string(tag) + " correlation floor");
    checkNear(m.tp.getMaxTruePeakDB(), -100.0f, 0.001f, std::string(tag) + " true peak floor");
    check(!m.tp.hasClipped(), std::string(tag) + " no clip flag");
}

static void channelRouterCase()
{
    // Mid/Side algebra on a known pair.
    const int n = 256;
    std::vector<float> L((size_t)n, 0.8f), R((size_t)n, 0.2f), oL((size_t)n), oR((size_t)n);
    ChannelRouter router;

    // Mid/Side use the energy-preserving 1/sqrt(2) convention; Mono uses /2.
    const float invRoot2 = 0.70710678f;
    router.setMode(ChannelRouter::Mode::Mid);
    router.process(L.data(), R.data(), oL.data(), oR.data(), n);
    checkNear(oL[0], 1.0f * invRoot2, 1e-5f, "[router] mid = (L+R)/sqrt2");

    router.setMode(ChannelRouter::Mode::Side);
    router.process(L.data(), R.data(), oL.data(), oR.data(), n);
    checkNear(oL[0], 0.6f * invRoot2, 1e-5f, "[router] side = (L-R)/sqrt2");

    router.setMode(ChannelRouter::Mode::Mono);
    router.process(L.data(), R.data(), oL.data(), oR.data(), n);
    checkNear(oL[0], 0.5f, 1e-6f, "[router] mono = (L+R)/2");
    checkNear(oR[0], oL[0], 1e-6f, "[router] mono feeds both outputs");
}

// Spectrum: an on-bin sine must land in the right display bin at its level.
static void spectrumCase(double sr, FFTProcessor::Resolution res, int fftSize)
{
    char tag[64];
    std::snprintf(tag, sizeof(tag), "[spectrum sr=%d N=%d]", (int)sr, fftSize);

    FFTProcessor fft;
    fft.prepare(sr, 512);
    fft.setResolution(res);
    fft.setSmoothing(0.0f);      // exact reading, no display smoothing
    fft.setSlope(0.0f);
    fft.setPeakHoldEnabled(false);

    // Bin-centered frequency near 1 kHz so windowing scalloping is zero.
    const double binWidth = sr / (double)fftSize;
    const double freq = std::round(1000.0 / binWidth) * binWidth;
    const float amp = std::pow(10.0f, -18.0f / 20.0f);
    auto s = makeSine(freq, amp, sr, fftSize * 4);

    for (int at = 0; at < (int)s.size(); at += 512)
    {
        const int len = std::min(512, (int)s.size() - at);
        fft.pushSamples(s.data() + at, s.data() + at, len);
    }
    fft.processFFT();
    check(fft.isDataReady(), std::string(tag) + " data ready");

    const auto& mags = fft.getMagnitudes();
    int peakBin = 0;
    for (int i = 1; i < FFTProcessor::DISPLAY_BINS; ++i)
        if (mags[(size_t)i] > mags[(size_t)peakBin]) peakBin = i;

    const int wantBin = FFTProcessor::getBinForFrequency((float)freq);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s peak display bin %d near expected %d",
                  tag, peakBin, wantBin);
    check(std::abs(peakBin - wantBin) <= 8, buf);

    // Normalised Hann + the 2/N scaling: an on-bin sine of amplitude A reads
    // A, i.e. -18 dB here.
    checkNear(mags[(size_t)peakBin], -18.0f, 0.6f, std::string(tag) + " peak level dB");
}

// ---- main -------------------------------------------------------------------

int main()
{
    const double rates[3] = { 44100.0, 48000.0, 96000.0 };

    for (double sr : rates)
    {
        sineCase(sr, 512);
        correlationCases(sr);
        truePeakIntersampleCase(sr);
        silenceCase(sr);
        blockInvarianceCase(sr);
    }
    sineCase(48000.0, 64);
    sineCase(48000.0, 483);

    channelRouterCase();

    for (double sr : rates)
    {
        spectrumCase(sr, FFTProcessor::Resolution::Low, 2048);
        spectrumCase(sr, FFTProcessor::Resolution::Medium, 4096);
        spectrumCase(sr, FFTProcessor::Resolution::High, 8192);
    }

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "ALL GOLDEN GATES PASS" : "GOLDEN GATES FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
