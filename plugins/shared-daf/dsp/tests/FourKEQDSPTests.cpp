// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FourKEQDSPTests.cpp — core tests for the 4K EQ 2 DSP (FourKEQDSP).
//
//   1. At the reference design rate (>= FourKEQDSP::kReferenceDesignRate) the
//      core renders bit-identically to the RBJ-only core it replaced, a frozen
//      copy of which lives in reference/.
//   2. Below it, 1x and 2x at 44.1 and 48 kHz hold the calibration's model
//      (each section as an RBJ biquad at 192 kHz) to within 1 dB per section
//      over 20 Hz..20 kHz: no cramping (dusk-audio-plugins#289).
//   3. The Hz API puts a band, and a filter's -3 dB point, where it is asked
//      to (dusk-audio-plugins#288).
//   4. No NaN, no unstable section and no runaway tail at extreme settings.

#include "FourKEQDSP.hpp"
#include "reference/FourKEQDSPMainRef.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace duskaudio
{
struct FourKEQDSPTestAccess
{
    struct Running
    {
        BiquadCoeffs bands[4];
        std::array<BiquadCoeffs, 3> low, high;
        BiquadCoeffs lpf, hpf1, hpf2;
    };

    static FourKEQDSP::SectionDesigns designs(const FourKEQDSP::CurveControls& c) noexcept
    {
        return FourKEQDSP::designSections(FourKEQDSP::coeffInputsFor(c));
    }

    static Running running(const FourKEQDSP& dsp) noexcept
    {
        const auto& c = dsp.ch[0];
        return { { c.lf.coeffs(), c.lm.coeffs(), c.hm.coeffs(), c.hf.coeffs() },
                 { { c.lowCorrection1.coeffs(), c.lowCorrection2.coeffs(), c.lowCorrection3.coeffs() } },
                 { { c.highCorrection1.coeffs(), c.highCorrection2.coeffs(), c.highCorrection3.coeffs() } },
                 c.lpf.coeffs(), c.hpf1.coeffs(), c.hpf2.coeffs() };
    }
};
} // namespace duskaudio

namespace
{
using duskaudio::BiquadCoeffs;
using duskaudio::FourKEQDSP;
using duskaudio::FourKEQDSPTestAccess;
using RefDSP = duskaudio::fourk_main_ref::FourKEQDSP;
using Band = FourKEQDSP::Band;

constexpr double kPi = 3.14159265358979323846;

int gChecks = 0;
int gFailures = 0;

#define CHECK(cond, ...)                                                          \
    do                                                                            \
    {                                                                             \
        ++gChecks;                                                                \
        if (!(cond))                                                              \
        {                                                                         \
            if (++gFailures <= 60)                                                \
            {                                                                     \
                std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
                std::fprintf(stderr, __VA_ARGS__);                                \
                std::fputc('\n', stderr);                                         \
            }                                                                     \
        }                                                                         \
    } while (false)

bool sameBits(const BiquadCoeffs& a, const BiquadCoeffs& b) noexcept
{
    return std::memcmp(&a, &b, sizeof(BiquadCoeffs)) == 0;
}

double magnitudeDb(const BiquadCoeffs& c, double w) noexcept
{
    duskaudio::Biquad b;
    b.setCoeffs(c);
    return 20.0 * std::log10(std::max(b.magnitude(w), 1.0e-12));
}

bool isStable(const BiquadCoeffs& c) noexcept
{
    // Both roots of z^2 + a1 z + a2 strictly inside the unit circle (Jury),
    // evaluated exactly: 1 + a2 rounded back to float can tie with |a1| for a
    // pair that is inside.
    return std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2)
        && std::isfinite(c.a1) && std::isfinite(c.a2)
        && std::abs(c.a2) < 1.0f && std::abs((double)c.a1) < 1.0 + (double)c.a2;
}

//------------------------------------------------------------------------------
// Settings, applied identically to the live core and the frozen reference
//------------------------------------------------------------------------------
struct Settings
{
    int eqType = 0;
    bool hpfOn = false, lpfOn = false;
    float hpf = 16.0f, lpf = 15201.0f;
    float lfGain = 0.0f, lfFreq = 200.0f;
    float lmGain = 0.0f, lmFreq = 1000.0f, lmQ = 1.5f;
    float hmGain = 0.0f, hmFreq = 3000.0f, hmQ = 1.5f;
    float hfGain = 0.0f, hfFreq = 8000.0f;
    bool lfBell = false, hfBell = false;
    bool hz = false; // live core only: band frequencies go through the Hz API
    bool filtersHz = false; // live core only: HPF/LPF through the Hz API
    float inputDb = 0.0f, outputDb = 0.0f, saturation = 0.0f;
    int oversampling = 2;
    bool ms = false, autoGain = false, bypass = false;
};

template <class Core>
void applyCommon(Core& c, const Settings& s)
{
    c.setEqType(s.eqType);
    c.setHpfEnabled(s.hpfOn); c.setHpfFreq(s.hpf);
    c.setLpfEnabled(s.lpfOn); c.setLpfFreq(s.lpf);
    c.setLfGain(s.lfGain); c.setLfBell(s.lfBell);
    c.setLmGain(s.lmGain); c.setLmQ(s.lmQ);
    c.setHmGain(s.hmGain); c.setHmQ(s.hmQ);
    c.setHfGain(s.hfGain); c.setHfBell(s.hfBell);
    c.setInputGainDb(s.inputDb); c.setOutputGainDb(s.outputDb);
    c.setSaturation(s.saturation);
    c.setOversampling(s.oversampling);
    c.setMsMode(s.ms); c.setAutoGain(s.autoGain); c.setBypass(s.bypass);
}

void apply(RefDSP& c, const Settings& s)
{
    applyCommon(c, s);
    c.setLfFreq(s.lfFreq); c.setLmFreq(s.lmFreq); c.setHmFreq(s.hmFreq); c.setHfFreq(s.hfFreq);
}

void apply(FourKEQDSP& c, const Settings& s)
{
    applyCommon(c, s);
    if (s.hz)
    {
        c.setLfFreqHz(s.lfFreq); c.setLmFreqHz(s.lmFreq);
        c.setHmFreqHz(s.hmFreq); c.setHfFreqHz(s.hfFreq);
    }
    else
    {
        c.setLfFreq(s.lfFreq); c.setLmFreq(s.lmFreq);
        c.setHmFreq(s.hmFreq); c.setHfFreq(s.hfFreq);
    }
    if (s.filtersHz)
    {
        c.setHpfFreqHz(s.hpf);
        c.setLpfFreqHz(s.lpf);
    }
}

template <class Controls>
Controls curveControls(const Settings& s, double hostRate)
{
    Controls c;
    c.baseSampleRate = hostRate;
    c.oversampling = (float)s.oversampling;
    c.black = s.eqType == 1;
    c.hpfEnabled = s.hpfOn; c.hpfFreq = s.hpf;
    c.lpfEnabled = s.lpfOn; c.lpfFreq = s.lpf;
    c.lfGain = s.lfGain; c.lfFreq = s.lfFreq; c.lfBell = s.lfBell ? 1.0f : 0.0f;
    c.lmGain = s.lmGain; c.lmFreq = s.lmFreq; c.lmQ = s.lmQ;
    c.hmGain = s.hmGain; c.hmFreq = s.hmFreq; c.hmQ = s.hmQ;
    c.hfGain = s.hfGain; c.hfFreq = s.hfFreq; c.hfBell = s.hfBell ? 1.0f : 0.0f;
    c.saturation = s.saturation;
    return c;
}

FourKEQDSP::CurveCoeffs liveCurve(const Settings& s, double hostRate)
{
    auto c = curveControls<FourKEQDSP::CurveControls>(s, hostRate);
    c.bandFrequenciesInHz = s.hz;
    c.hpfFreqInHz = c.lpfFreqInHz = s.filtersHz;
    return FourKEQDSP::designCurve(c);
}

RefDSP::CurveCoeffs refCurve(const Settings& s, double hostRate)
{
    return RefDSP::designCurve(curveControls<RefDSP::CurveControls>(s, hostRate));
}

using Design = FourKEQDSP::SectionDesign;

FourKEQDSP::SectionDesigns designsFor(const Settings& s, double hostRate)
{
    auto c = curveControls<FourKEQDSP::CurveControls>(s, hostRate);
    c.bandFrequenciesInHz = s.hz;
    c.hpfFreqInHz = c.lpfFreqInHz = s.filtersHz;
    return FourKEQDSPTestAccess::designs(c);
}

// The model the calibration was fitted in (dusk-audio-tools fit_curves.py,
// MODEL_SAMPLE_RATE = 48 kHz * 4): each section as an RBJ cookbook biquad at
// 192 kHz, here in double precision so the float coefficients the 4x path runs
// are not part of the reference. Result in dB at f.
double modelDb(const Design& d, double f)
{
    constexpr double fs = 192000.0;
    const double A = std::pow(10.0, (double)d.gainDb / 40.0), Q = (double)d.q;
    const double w0 = 2.0 * kPi * std::min((double)d.freq, 0.49 * fs) / fs;
    const double cw = std::cos(w0), al = std::sin(w0) / (2.0 * Q), s2 = 2.0 * std::sqrt(A) * al;
    double b0, b1, b2, a0, a1, a2;
    switch (d.shape)
    {
    case Design::Shape::Peak:
        b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
        break;
    case Design::Shape::HighShelf:
        b0 = A * ((A + 1) + (A - 1) * cw + s2); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s2);
        a0 = (A + 1) - (A - 1) * cw + s2; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s2;
        break;
    case Design::Shape::LowShelf:
        b0 = A * ((A + 1) - (A - 1) * cw + s2); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s2);
        a0 = (A + 1) + (A - 1) * cw + s2; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s2;
        break;
    case Design::Shape::LowPass:
    default:
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
        break;
    }
    const double w = 2.0 * kPi * f / fs;
    const double cr = std::cos(w), ci = std::sin(w), c2r = std::cos(2 * w), c2i = std::sin(2 * w);
    const double nr = b0 + b1 * cr + b2 * c2r, ni = -(b1 * ci + b2 * c2i);
    const double dr = a0 + a1 * cr + a2 * c2r, di = -(a1 * ci + a2 * c2i);
    return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

// The model of every section a setting runs.
double modelSectionsDb(const Settings& s, const FourKEQDSP::SectionDesigns& d, double f)
{
    const bool active[4] = { std::abs(s.lfGain) > 1.0e-6f, std::abs(s.lmGain) > 1.0e-6f,
                             std::abs(s.hmGain) > 1.0e-6f, std::abs(s.hfGain) > 1.0e-6f };
    double db = 0.0;
    for (int i = 0; i < 4; ++i)
        if (active[i]) db += modelDb(d.bands[i], f);
    if (active[0] && active[1])
        for (const auto& c : d.lowCorrection) db += modelDb(c, f);
    if (active[2] && active[3])
        for (const auto& c : d.highCorrection) db += modelDb(c, f);
    if (s.lpfOn)
        db += modelDb(d.lpf, f);
    return db;
}

// Magnitude of the EQ sections alone (bands, pair corrections, LPF): the part
// of the curve the design-rate switch touches.
template <class Curve>
double sectionsDb(const Curve& d, double freq)
{
    const double w = 2.0 * kPi * freq / d.sampleRate;
    double db = 0.0;
    for (int i = 0; i < 4; ++i)
        if (d.hasBand[i])
            db += magnitudeDb(d.bands[i], w);
    if (d.hasLowCorrection)
        for (const auto& c : d.lowCorrection) db += magnitudeDb(c, w);
    if (d.hasHighCorrection)
        for (const auto& c : d.highCorrection) db += magnitudeDb(c, w);
    if (d.hasLpf)
        db += magnitudeDb(d.lpf, w);
    return db;
}

template <class Curve>
double worstModelErrorDb(const Curve& realized, const Settings& s, const FourKEQDSP::SectionDesigns& d)
{
    double worst = 0.0;
    for (int i = 0; i <= 240; ++i)
    {
        const double f = 20.0 * std::pow(1000.0, i / 240.0);
        worst = std::max(worst, std::abs(sectionsDb(realized, f) - modelSectionsDb(s, d, f)));
    }
    return worst;
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------
struct Automation
{
    int block;
    std::function<void(Settings&)> change;
};

// Deterministic stereo programme: broadband noise, low/mid/high partials, one
// near the host Nyquist, periodic impulses, and a silent stretch.
std::array<std::vector<float>, 2> makeProgramme(int samples, double fs)
{
    std::array<std::vector<float>, 2> x { std::vector<float>((size_t)samples), std::vector<float>((size_t)samples) };
    std::uint32_t seed = 0x2468ACEu;
    auto noise = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) & 0xFFFFFF) / (float)0x1000000 * 2.0f - 1.0f;
    };
    const double partials[4] = { 45.0, 1000.0, 9500.0, 0.45 * fs };
    for (int n = 0; n < samples; ++n)
    {
        const double t = n / fs;
        for (int c = 0; c < 2; ++c)
        {
            double v = 0.25 * noise();
            for (int k = 0; k < 4; ++k)
                v += 0.12 / (k + 1) * std::sin(2.0 * kPi * partials[k] * t + 0.7 * c + k);
            if (n % 4001 < 3) v += (n % 2 ? -0.8 : 0.8);
            if (n > samples * 6 / 10 && n < samples * 7 / 10) v = 0.0;
            x[(size_t)c][(size_t)n] = (float)v;
        }
    }
    return x;
}

struct RenderResult
{
    std::array<std::vector<float>, 2> out;
    int latency = 0;
    float outPeakL = 0.0f;
};

template <class Core>
RenderResult render(double fs, int maxBlock, Settings s, const std::vector<Automation>& automation,
                    const std::array<std::vector<float>, 2>& in)
{
    static const int kBlockSizes[] = { 1, 7, 64, 300, 512, 1000, 33, 256, 128 };
    auto core = std::make_unique<Core>();
    apply(*core, s);
    core->prepare(fs, maxBlock);
    core->reset();

    const int total = (int)in[0].size();
    RenderResult r;
    r.out = { std::vector<float>((size_t)total), std::vector<float>((size_t)total) };
    int block = 0;
    for (int offset = 0; offset < total; ++block)
    {
        for (const auto& a : automation)
            if (a.block == block)
            {
                a.change(s);
                apply(*core, s);
            }
        const int n = std::min(kBlockSizes[block % 9], total - offset);
        const float* inputs[2] = { in[0].data() + offset, in[1].data() + offset };
        float* outputs[2] = { r.out[0].data() + offset, r.out[1].data() + offset };
        core->processBlock(inputs, outputs, 2, n);
        offset += n;
    }
    r.latency = core->getLatencySamples();
    r.outPeakL = core->getOutputPeakL();
    return r;
}

//==============================================================================
// 1. Bit identity at the reference design rate
//==============================================================================
void testReferenceRateIsBitIdentical()
{
    struct Rate { double host; int oversampling; };
    // Every combination whose design rate reaches kReferenceDesignRate,
    // including requests the factor cap lowers.
    const Rate rates[] = { { 48000.0, 2 }, { 44100.0, 2 }, { 96000.0, 1 }, { 88200.0, 1 },
                           { 96000.0, 2 }, { 176400.0, 0 }, { 192000.0, 0 }, { 192000.0, 2 } };

    Settings brownShelves;
    brownShelves.hpfOn = true; brownShelves.hpf = 80.0f;
    brownShelves.lpfOn = true; brownShelves.lpf = 12800.0f;
    brownShelves.lfGain = 6.0f; brownShelves.lfFreq = 100.0f;
    brownShelves.lmGain = -4.0f; brownShelves.lmFreq = 400.0f; brownShelves.lmQ = 0.8f;
    brownShelves.hmGain = 9.0f; brownShelves.hmFreq = 6700.0f; brownShelves.hmQ = 2.4f;
    brownShelves.hfGain = 5.0f; brownShelves.hfFreq = 12000.0f;

    Settings blackBells = brownShelves;
    blackBells.eqType = 1;
    blackBells.lfBell = blackBells.hfBell = true;
    blackBells.lfGain = -12.0f; blackBells.lfFreq = 42.0f;
    blackBells.hmGain = -15.0f; blackBells.hmQ = 0.5f;
    blackBells.hfGain = 15.0f; blackBells.hfFreq = 16000.0f;
    blackBells.ms = true; blackBells.autoGain = true; blackBells.saturation = 60.0f;
    blackBells.inputDb = 6.0f;

    Settings hot = brownShelves;
    hot.lfGain = hot.lmGain = hot.hmGain = hot.hfGain = 15.0f;
    hot.lpf = 3000.0f; hot.inputDb = 12.0f; hot.outputDb = -6.0f;

    Settings sparse;
    sparse.eqType = 1;
    sparse.hmGain = 3.0f; sparse.hmFreq = 1150.0f;

    // Moves every coefficient input between blocks, including onto and off
    // 0 dB (section bypass and state reset), across the table ends, the
    // voicing, the bell switches and bypass.
    const std::vector<Automation> automation = {
        { 3, [](Settings& s) { s.hmGain = -7.0f; s.hmFreq = 7000.0f; } },
        { 5, [](Settings& s) { s.lfBell = !s.lfBell; s.lmQ = 3.0f; } },
        { 8, [](Settings& s) { s.hfGain = 0.0f; s.lpf = 15201.0f; } },
        { 11, [](Settings& s) { s.hfGain = -11.0f; s.hfFreq = 1500.0f; s.hfBell = !s.hfBell; } },
        { 14, [](Settings& s) { s.eqType = 1 - s.eqType; } },
        { 17, [](Settings& s) { s.lfGain = 0.0f; s.lmFreq = 2500.0f; } },
        { 20, [](Settings& s) { s.lfGain = 13.5f; s.lfFreq = 450.0f; s.hpf = 350.0f; } },
        { 23, [](Settings& s) { s.lpfOn = !s.lpfOn; s.hpfOn = !s.hpfOn; } },
        { 26, [](Settings& s) { s.autoGain = !s.autoGain; s.hmQ = 1.05f; } },
        { 30, [](Settings& s) { s.bypass = true; } },
        { 36, [](Settings& s) { s.bypass = false; s.hmGain = 15.0f; s.hmFreq = 600.0f; } },
    };

    const Settings programmes[] = { brownShelves, blackBells, hot, sparse };
    const char* names[] = { "brown shelves", "black bells + M/S + auto-gain", "hot", "sparse" };
    int identical = 0, compared = 0;
    for (const Rate& rate : rates)
    {
        const auto in = makeProgramme((int)(rate.host * 0.35), rate.host);
        for (int p = 0; p < 4; ++p)
        {
            Settings s = programmes[p];
            s.oversampling = rate.oversampling;
            const int factor = FourKEQDSP::chooseFactor(rate.host, rate.oversampling);
            CHECK(rate.host * factor >= FourKEQDSP::kReferenceDesignRate, "rate table");
            const RenderResult live = render<FourKEQDSP>(rate.host, 512, s, automation, in);
            const RenderResult ref = render<RefDSP>(rate.host, 512, s, automation, in);
            const bool same = std::memcmp(live.out[0].data(), ref.out[0].data(), live.out[0].size() * sizeof(float)) == 0
                           && std::memcmp(live.out[1].data(), ref.out[1].data(), live.out[1].size() * sizeof(float)) == 0;
            ++compared;
            identical += same ? 1 : 0;
            CHECK(same, "%s at %.0f Hz x%d is not bit-identical to the reference core", names[p], rate.host, factor);
            CHECK(live.latency == ref.latency && std::memcmp(&live.outPeakL, &ref.outPeakL, sizeof(float)) == 0,
                  "%s at %.0f Hz x%d: latency/meter differ", names[p], rate.host, factor);

            // The drawn curve too, section by section.
            const auto a = liveCurve(s, rate.host);
            const auto b = refCurve(s, rate.host);
            bool curveSame = a.sampleRate == b.sampleRate && sameBits(a.lpf, b.lpf) && sameBits(a.hpf, b.hpf)
                          && sameBits(a.hpfFirstOrder, b.hpfFirstOrder) && a.hpfTrimLinear == b.hpfTrimLinear;
            for (int i = 0; i < 4; ++i)
                curveSame = curveSame && a.hasBand[i] == b.hasBand[i] && sameBits(a.bands[i], b.bands[i]);
            for (int i = 0; i < 3; ++i)
                curveSame = curveSame && sameBits(a.lowCorrection[(size_t)i], b.lowCorrection[(size_t)i])
                                      && sameBits(a.highCorrection[(size_t)i], b.highCorrection[(size_t)i]);
            for (int i = 0; i <= 64 && curveSame; ++i)
            {
                const float f = (float)(20.0 * std::pow(1000.0, i / 64.0));
                const float x = FourKEQDSP::curveDbAt(a, f), y = RefDSP::curveDbAt(b, f);
                curveSame = std::memcmp(&x, &y, sizeof(float)) == 0;
            }
            CHECK(curveSame, "%s at %.0f Hz x%d: designCurve differs from the reference core", names[p], rate.host, factor);
        }
    }

    // The public pair-correction designer, over its whole input space.
    bool pairsSame = true;
    for (double fs : { 176400.0, 192000.0 })
        for (int black = 0; black < 2; ++black)
            for (int high = 0; high < 2; ++high)
                for (float g1 : { -15.0f, -6.0f, 4.0f, 15.0f })
                    for (float g2 : { -12.0f, 0.5f, 9.0f })
                        for (float pos : { 0.0f, 0.3f, 0.77f, 1.0f })
                            for (float shape : { 0.0f, 1.0f, 0.5f, 3.0f })
                            {
                                const float f1 = high ? 600.0f + pos * 6400.0f : 30.0f + pos * 420.0f;
                                const float f2 = high ? 1500.0f + (1.0f - pos) * 14500.0f : 200.0f + pos * 2300.0f;
                                const auto a = FourKEQDSP::calibratedPairCorrection(fs, high, black, g1, f1, shape, g2, f2, shape);
                                const auto b = RefDSP::calibratedPairCorrection(fs, high, black, g1, f1, shape, g2, f2, shape);
                                for (int i = 0; i < 3; ++i)
                                    pairsSame = pairsSame && sameBits(a[(size_t)i], b[(size_t)i]);
                            }
    CHECK(pairsSame, "calibratedPairCorrection differs from the reference core at the reference rate");

    // Every dial-API design at the reference rates, section by section: the
    // realization guard (keepPolesInside) must never have touched one.
    long designsCompared = 0, designsDiffering = 0;
    for (const Rate& rate : { Rate{ 44100.0, 2 }, Rate{ 48000.0, 2 }, Rate{ 96000.0, 1 }, Rate{ 192000.0, 0 } })
        for (int black = 0; black < 2; ++black)
            for (int step = 0; step <= 40; ++step)
                for (float g : { -15.0f, -10.5f, -4.5f, -1.5f, 1.5f, 6.0f, 12.0f, 15.0f })
                    for (float q : { 0.5f, 0.95f, 1.5f, 2.25f, 3.0f })
                        for (int bells = 0; bells < 4; ++bells)
                        {
                            const float t = step / 40.0f;
                            Settings s;
                            s.eqType = black;
                            s.oversampling = rate.oversampling;
                            s.lfGain = g; s.lfFreq = 30.0f + 420.0f * t; s.lfBell = bells & 1;
                            s.lmGain = -g; s.lmFreq = 200.0f + 2300.0f * (1.0f - t); s.lmQ = q;
                            s.hmGain = 0.5f * g; s.hmFreq = 600.0f + 6400.0f * t; s.hmQ = 3.5f - q;
                            s.hfGain = -0.7f * g; s.hfFreq = 1500.0f + 14500.0f * (1.0f - t); s.hfBell = bells & 2;
                            s.lpfOn = true; s.lpf = 3000.0f + 12201.0f * t;
                            const auto a = liveCurve(s, rate.host);
                            const auto b = refCurve(s, rate.host);
                            bool same = sameBits(a.lpf, b.lpf);
                            for (int i = 0; i < 4; ++i) same = same && sameBits(a.bands[i], b.bands[i]);
                            for (int i = 0; i < 3; ++i)
                                same = same && sameBits(a.lowCorrection[(size_t)i], b.lowCorrection[(size_t)i])
                                            && sameBits(a.highCorrection[(size_t)i], b.highCorrection[(size_t)i]);
                            ++designsCompared;
                            designsDiffering += same ? 0 : 1;
                        }
    CHECK(designsDiffering == 0, "%ld of %ld dial-API designs differ from the reference core at the reference rate",
          designsDiffering, designsCompared);

    std::printf("[1] reference rate: %d/%d renders bit-identical to main d7aca75f "
                "(%zu rate settings x 4 programmes, 11 automation steps, block sizes 1..1000);\n"
                "    %ld/%ld dial-API section sets bit-identical across the whole dial, gain and Q space\n",
                identical, compared, sizeof(rates) / sizeof(rates[0]),
                designsCompared - designsDiffering, designsCompared);
}

// The frozen reference shares DuskFilters.hpp, DuskOversampler.hpp and
// ConsoleSaturationCore.h with the live core, so [1] cannot see a change to
// them. These aggregates of the 4x sound at 48 kHz, dial API, both voicings,
// can. Aggregates rather than bits, so they hold across compilers and
// architectures; re-measure them only when the 4x sound is meant to change.
// The tolerances sit well above what fused multiply-add moves them (4e-6 RMS,
// 2e-5 peak) and below what a 0.1% change to a peaking band's bandwidth or a
// 1e-4 change to the first halfband tap does (1e-4 RMS and up). The LF shelf
// sits at the top of its dial: lower down its render moves by up to 0.3% with
// fused multiply-add alone.
void testFourTimesSoundIsPinned()
{
    Settings brown;
    brown.hpfOn = true; brown.hpf = 80.0f;
    brown.lpfOn = true; brown.lpf = 12800.0f;
    brown.lfGain = 6.0f; brown.lfFreq = 400.0f;
    brown.lmGain = -4.0f; brown.lmFreq = 400.0f; brown.lmQ = 0.8f;
    brown.hmGain = 9.0f; brown.hmFreq = 6700.0f; brown.hmQ = 2.4f;
    brown.hfGain = 5.0f; brown.hfFreq = 12000.0f;
    brown.inputDb = 6.0f;
    Settings black = brown;
    black.eqType = 1;
    black.hfBell = true; black.hfGain = 12.0f; black.hfFreq = 16000.0f;

    struct Golden { const char* name; Settings s; double peak, rms; };
    const Golden goldens[] = {
        { "Brown", brown, 1.91320848, 0.529754799 },
        { "Black", black, 2.1849072, 1.10863099 },
    };
    const auto in = makeProgramme((int)(48000.0 * 0.35), 48000.0);
    std::printf("[1b] 48 kHz 4x, dial API: peak / RMS against the pinned aggregates\n");
    for (const Golden& g : goldens)
    {
        CHECK(FourKEQDSP::chooseFactor(48000.0, g.s.oversampling) == 4, "the golden render is not at 4x");
        const RenderResult r = render<FourKEQDSP>(48000.0, 512, g.s, {}, in);
        double peak = 0.0, sum = 0.0;
        size_t n = 0;
        for (const auto& channel : r.out)
            for (float v : channel)
            {
                peak = std::max(peak, (double)std::abs(v));
                sum += (double)v * v;
                ++n;
            }
        const double rms = std::sqrt(sum / (double)n);
        CHECK(std::abs(peak / g.peak - 1.0) < 2.0e-4 && std::abs(rms / g.rms - 1.0) < 5.0e-5,
              "%s at 4x: peak %.9g, RMS %.9g; pinned %.9g, %.9g", g.name, peak, rms, g.peak, g.rms);
        std::printf("  %s: peak %.9g (pinned %.9g), RMS %.9g (pinned %.9g)\n", g.name, peak, g.peak, rms, g.rms);
    }
}

//==============================================================================
// 2. No cramping at 1x/2x
//==============================================================================
struct BandCase
{
    const char* name;
    Band band;
    bool bell;
    std::vector<float> dials;   // across the plugin's dial range
    std::vector<float> hz;      // across the same range, through the Hz API
    std::vector<float> qs;
};

std::vector<BandCase> bandCases()
{
    return {
        { "LF shelf", Band::LF, false, { 30, 60, 100, 200, 300, 405, 450 }, { 30, 60, 100, 200, 300, 450 }, { 1.5f } },
        { "LF bell",  Band::LF, true,  { 30, 60, 100, 200, 300, 405, 450 }, { 30, 60, 100, 200, 300, 450 }, { 1.5f } },
        { "LM bell",  Band::LM, true,  { 200, 400, 800, 1500, 2000, 2200, 2500 }, { 200, 400, 800, 1500, 2500 }, { 0.5f, 1.5f, 3.0f } },
        { "HM bell",  Band::HM, true,  { 600, 1500, 3000, 5000, 6100, 6400, 7000 }, { 600, 1500, 3000, 5000, 7000 }, { 0.5f, 1.5f, 3.0f } },
        { "HF shelf", Band::HF, false, { 1500, 3000, 6000, 10000, 13000, 15400, 16000 }, { 1500, 3000, 6000, 10000, 16000 }, { 1.5f } },
        { "HF bell",  Band::HF, true,  { 1500, 3000, 6000, 10000, 13000, 15400, 16000 }, { 1500, 3000, 6000, 10000, 16000 }, { 1.5f } },
    };
}

Settings isolated(Band band, bool bell, bool black, float gain, float freq, float q, bool hz)
{
    Settings s;
    s.eqType = black ? 1 : 0;
    s.hz = hz;
    switch (band)
    {
    case Band::LF: s.lfGain = gain; s.lfFreq = freq; s.lfBell = bell; break;
    case Band::LM: s.lmGain = gain; s.lmFreq = freq; s.lmQ = q; break;
    case Band::HM: s.hmGain = gain; s.hmFreq = freq; s.hmQ = q; break;
    case Band::HF: s.hfGain = gain; s.hfFreq = freq; s.hfBell = bell; break;
    }
    return s;
}

struct Worst
{
    double live[3] = { 0.0, 0.0, 0.0 };   // 1x, 2x, 4x
    double before[3] = { 0.0, 0.0, 0.0 }; // main d7aca75f, dial API only
};

void accumulate(Worst& w, Settings s, double host)
{
    const auto designs = designsFor(s, host);
    for (int os = 0; os < 3; ++os)
    {
        s.oversampling = os;
        w.live[os] = std::max(w.live[os], worstModelErrorDb(liveCurve(s, host), s, designs));
        if (!s.hz)
            w.before[os] = std::max(w.before[os], worstModelErrorDb(refCurve(s, host), s, designs));
    }
}

void printRow(const char* label, double host, const Worst& w, bool hasBefore)
{
    if (hasBefore)
        std::printf("    %-24s %5.1f   %5.2f (%5.2f)   %5.2f (%5.2f)   %5.2f\n", label, host / 1000,
                    w.live[0], w.before[0], w.live[1], w.before[1], w.live[2]);
    else
        std::printf("    %-24s %5.1f   %5.2f           %5.2f           %5.2f\n", label, host / 1000,
                    w.live[0], w.live[1], w.live[2]);
}

void testNoCrampingBelowReferenceRate()
{
    constexpr double kTolerance = 1.0;
    constexpr double kStackedTolerance = 1.5;
    double stacked = 0.0;
    std::printf("[2] worst |error| dB, 20 Hz..20 kHz, against the calibration's model (each section as a\n"
                "    double-precision RBJ biquad at 192 kHz); was = main d7aca75f; 4x = the RBJ reference\n"
                "    path, unchanged, whose own error is the float-coefficient limit at 20-30 Hz:\n");
    std::printf("    %-24s %5s   %13s   %13s   %5s\n", "", "host", "1x now (was)", "2x now (was)", "4x");
    const float gains[] = { -15.0f, -12.0f, -6.0f, -3.0f, 3.0f, 6.0f, 12.0f, 15.0f };
    double overall = 0.0;
    for (double host : { 44100.0, 48000.0 })
    {
        for (const BandCase& bc : bandCases())
            for (int black = 0; black < 2; ++black)
                for (int hz = 0; hz < 2; ++hz)
                {
                    Worst w;
                    for (float f : hz ? bc.hz : bc.dials)
                        for (float g : gains)
                            for (float q : bc.qs)
                                accumulate(w, isolated(bc.band, bc.bell, black, g, f, q, hz), host);
                    char label[48];
                    std::snprintf(label, sizeof label, "%s %s%s", black ? "Black" : "Brown", bc.name, hz ? " (Hz)" : "");
                    printRow(label, host, w, !hz);
                    overall = std::max(overall, std::max(w.live[0], w.live[1]));
                    CHECK(w.live[0] <= kTolerance && w.live[1] <= kTolerance,
                          "%s at %.1f kHz: 1x %.2f dB, 2x %.2f dB from the model", label, host / 1000, w.live[0], w.live[1]);
                }

        for (int black = 0; black < 2; ++black)
        {
            Worst w;
            for (float control : { 3000.0f, 5000.0f, 7700.0f, 10000.0f, 12800.0f, 14721.5f, 15201.0f })
            {
                Settings s;
                s.eqType = black;
                s.lpfOn = true;
                s.lpf = control;
                accumulate(w, s, host);
            }
            char label[48];
            std::snprintf(label, sizeof label, "%s LPF", black ? "Black" : "Brown");
            printRow(label, host, w, true);
            overall = std::max(overall, std::max(w.live[0], w.live[1]));
            CHECK(w.live[0] <= kTolerance && w.live[1] <= kTolerance, "%s at %.1f kHz", label, host / 1000);
        }

        // All four bands active: the three-section interaction corrections ride
        // along and must hold their model shape too. Each section holds 1 dB;
        // stacked, two near-Nyquist sections can add (an HF bell and the LPF
        // each about 0.6 dB low around 18 kHz, where the LPF is already cutting),
        // so the stacked limit is 1.5 dB.
        for (int black = 0; black < 2; ++black)
            for (int hz = 0; hz < 2; ++hz)
            {
                Worst w;
                std::uint32_t seed = 12345u + (std::uint32_t)black;
                auto next = [&seed]() {
                    seed = seed * 1664525u + 1013904223u;
                    return (float)((seed >> 8) & 0xFFFF) / 65535.0f;
                };
                for (int k = 0; k < 40; ++k)
                {
                    Settings s;
                    s.eqType = black;
                    s.hz = hz;
                    s.lfGain = -15.0f + 30.0f * next(); s.lfFreq = 30.0f * std::pow(15.0f, next()); s.lfBell = next() > 0.5f;
                    s.lmGain = -15.0f + 30.0f * next(); s.lmFreq = 200.0f * std::pow(12.5f, next()); s.lmQ = 0.5f + 2.5f * next();
                    s.hmGain = -15.0f + 30.0f * next(); s.hmFreq = 600.0f * std::pow(7000.0f / 600.0f, next()); s.hmQ = 0.5f + 2.5f * next();
                    s.hfGain = -15.0f + 30.0f * next(); s.hfFreq = 1500.0f * std::pow(16000.0f / 1500.0f, next()); s.hfBell = next() > 0.5f;
                    s.lpfOn = k % 3 == 0; s.lpf = 3000.0f + 12201.0f * next();
                    accumulate(w, s, host);
                }
                char label[48];
                std::snprintf(label, sizeof label, "%s all bands%s", black ? "Black" : "Brown", hz ? " (Hz)" : "");
                printRow(label, host, w, !hz);
                stacked = std::max(stacked, std::max(w.live[0], w.live[1]));
                CHECK(w.live[0] <= kStackedTolerance && w.live[1] <= kStackedTolerance, "%s at %.1f kHz", label, host / 1000);
            }
    }

    // The two named cases from #289: HM at the top of its range +12 dB, and the
    // LPF at the top of its range, at 20 kHz itself.
    for (double host : { 44100.0, 48000.0 })
        for (int black = 0; black < 2; ++black)
            for (int os = 0; os < 2; ++os)
            {
                Settings hm = isolated(Band::HM, true, black, 12.0f, 7000.0f, 1.5f, true);
                Settings lpf;
                lpf.eqType = black; lpf.lpfOn = true; lpf.lpf = 15201.0f;
                for (Settings* s : { &hm, &lpf })
                {
                    s->oversampling = os;
                    const double err = std::abs(sectionsDb(liveCurve(*s, host), 20000.0)
                                                - modelSectionsDb(*s, designsFor(*s, host), 20000.0));
                    CHECK(err <= kTolerance, "%s %s at %.1f kHz x%d: %.2f dB off at 20 kHz",
                          black ? "Black" : "Brown", s == &hm ? "HM 7 kHz +12 dB" : "LPF top", host / 1000, 1 << os, err);
                }
            }
    std::printf("    worst at 1x/2x: %.2f dB per section (limit %.1f), %.2f dB stacked (limit %.1f)\n",
                overall, kTolerance, stacked, kStackedTolerance);
}

// The audio path runs the curve's coefficients, and a low-level sine through
// the core lands on the curve: the matched design is what plays at 1x.
double sineGainDb(FourKEQDSP& core, double fs, double freq)
{
    const int warm = (int)(fs * 0.25), measure = (int)(fs * 0.5);
    std::vector<float> x((size_t)(warm + measure)), y(x.size());
    for (size_t n = 0; n < x.size(); ++n)
        x[n] = (float)(0.01 * std::sin(2.0 * kPi * freq * (double)n / fs));
    core.reset();
    for (int off = 0; off < (int)x.size(); off += 256)
    {
        const int len = std::min(256, (int)x.size() - off);
        const float* in[2] = { x.data() + off, x.data() + off };
        float* out[2] = { y.data() + off, y.data() + off };
        float tmp[256];
        out[1] = tmp;
        core.processBlock(in, out, 2, len);
    }
    auto amplitude = [&](const std::vector<float>& v) {
        double re = 0.0, im = 0.0;
        for (int n = warm; n < warm + measure; ++n)
        {
            re += v[(size_t)n] * std::cos(2.0 * kPi * freq * n / fs);
            im += v[(size_t)n] * std::sin(2.0 * kPi * freq * n / fs);
        }
        return std::sqrt(re * re + im * im);
    };
    return 20.0 * std::log10(amplitude(y) / amplitude(x));
}

void testAudioPathRunsTheCurve()
{
    double worst = 0.0;
    for (int black = 0; black < 2; ++black)
    {
        Settings s = isolated(Band::HM, true, black, 12.0f, 7000.0f, 1.5f, true);
        s.hfGain = 6.0f; s.hfFreq = 12000.0f; // HF shelf: brings in the high pair correction
        s.lpfOn = true; s.lpf = 15201.0f;
        s.oversampling = 0;
        FourKEQDSP core;
        apply(core, s);
        core.prepare(48000.0, 256);
        const auto curve = liveCurve(s, 48000.0);
        for (double f : { 1000.0, 5000.0, 7000.0, 12000.0, 17000.0, 20000.0 })
        {
            const double measured = sineGainDb(core, 48000.0, f);
            const double drawn = FourKEQDSP::curveDbAt(curve, (float)f);
            worst = std::max(worst, std::abs(measured - drawn));
            CHECK(std::abs(measured - drawn) < 0.1, "%s 1x sine at %.0f Hz: %.2f dB, curve %.2f dB",
                  black ? "Black" : "Brown", f, measured, drawn);
        }
        const auto running = FourKEQDSPTestAccess::running(core);
        bool same = sameBits(running.bands[2], curve.bands[2]) && sameBits(running.bands[3], curve.bands[3])
                 && sameBits(running.lpf, curve.lpf);
        for (int i = 0; i < 3; ++i)
            same = same && sameBits(running.high[(size_t)i], curve.highCorrection[(size_t)i]);
        CHECK(same, "%s: running coefficients are not the curve's", black ? "Black" : "Brown");
    }
    std::printf("[2b] 48 kHz 1x render vs designCurve: worst %.3f dB over six sines\n", worst);
}

//==============================================================================
// 3. The Hz API
//==============================================================================
// Frequency of the section's magnitude extremum between lo and hi.
double extremumHz(const BiquadCoeffs& c, double fs, double lo, double hi, bool boost)
{
    auto score = [&](double f) { const double m = magnitudeDb(c, 2.0 * kPi * f / fs); return boost ? m : -m; };
    double best = lo, bestScore = -1.0e300;
    for (int i = 0; i <= 2000; ++i)
    {
        const double f = lo * std::pow(hi / lo, i / 2000.0);
        const double v = score(f);
        if (v > bestScore) { bestScore = v; best = f; }
    }
    double a = best / std::pow(hi / lo, 1.0 / 2000.0), b = best * std::pow(hi / lo, 1.0 / 2000.0);
    for (int i = 0; i < 200; ++i)
    {
        const double m1 = a + (b - a) / 3.0, m2 = b - (b - a) / 3.0;
        (score(m1) < score(m2) ? a : b) = (score(m1) < score(m2) ? m1 : m2);
    }
    return 0.5 * (a + b);
}

int bandIndex(Band b) { return (int)b; }

void testHzApiPlacesBands()
{
    constexpr double kHost = 48000.0;
    const float ref = FourKEQDSP::kEqReferenceGainDb;
    struct End { Band band; bool bell; float hz; const char* name; };
    const End ends[] = {
        { Band::LF, true, 30.0f, "LF bell 30 Hz" },     { Band::LF, true, 450.0f, "LF bell 450 Hz" },
        { Band::LM, true, 200.0f, "LM 200 Hz" },        { Band::LM, true, 2500.0f, "LM 2.5 kHz" },
        { Band::HM, true, 600.0f, "HM 600 Hz" },        { Band::HM, true, 7000.0f, "HM 7 kHz" },
        { Band::HF, true, 1500.0f, "HF bell 1.5 kHz" }, { Band::HF, true, 16000.0f, "HF bell 16 kHz" },
    };
    std::printf("[3] Hz API, 48 kHz host, reference gain %+.1f dB, Q 1.5: requested -> measured peak\n", ref);
    std::printf("    %-22s %8s %9s %14s   %s\n", "", "design", "4x peak", "1x err at req", "dial API at the same number, 4x");
    for (int black = 0; black < 2; ++black)
        for (const End& e : ends)
            for (float q : { 0.5f, 1.5f, 3.0f })
                for (float sign : { 1.0f, -1.0f })
                {
                    Settings s = isolated(e.band, e.bell, black, sign * ref, e.hz, q, true);
                    const int b = bandIndex(e.band);
                    const double design = designsFor(s, kHost).bands[b].freq;
                    CHECK(std::abs(design / e.hz - 1.0) < 1.0e-6, "%s %s: design frequency %.3f",
                          black ? "Black" : "Brown", e.name, design);
                    // 4x: the extremum of the realized section. The RBJ path's
                    // float coefficients move a 30 Hz section at 192 kHz by a
                    // few percent (cos(w0) rounds within ~1e-8 of 1: the
                    // float-coefficient limit noted in #289, the same for the
                    // dial API), and the flat bottom of a broad sub-1 kHz cut
                    // by a few tenths of a percent.
                    s.oversampling = 2;
                    const auto d4 = liveCurve(s, kHost);
                    const double peak4x = extremumHz(d4.bands[b], d4.sampleRate, e.hz * 0.7, e.hz * 1.4, sign > 0);
                    const double tolerance4x = e.hz < 100.0f ? 0.05 : e.hz < 1000.0f ? 5.0e-3 : 1.0e-3;
                    CHECK(std::abs(peak4x / e.hz - 1.0) < tolerance4x, "%s %s Q%.1f %+.1f dB at 4x peaks at %.1f Hz",
                          black ? "Black" : "Brown", e.name, q, sign * ref, peak4x);
                    // 1x: a broad bell's flat top makes its argmax meaningless
                    // at 0.1 dB of error, so check the level AT the request
                    // against the model's centre gain instead: exact while the
                    // centre is the match point (below fs/4), within the
                    // section's accuracy above it.
                    s.oversampling = 0;
                    const auto d1 = liveCurve(s, kHost);
                    const Design& design1 = designsFor(s, kHost).bands[b];
                    const double atRequest = magnitudeDb(d1.bands[b], 2.0 * kPi * e.hz / d1.sampleRate);
                    const double modelAtRequest = modelDb(design1, e.hz);
                    const double tolerance1x = e.hz < 0.25 * kHost ? 0.05 : 1.0;
                    CHECK(std::abs(atRequest - modelAtRequest) < tolerance1x, "%s %s Q%.1f %+.1f dB at 1x: %.3f dB at %.0f Hz, model %.3f",
                          black ? "Black" : "Brown", e.name, q, sign * ref, atRequest, (double)e.hz, modelAtRequest);
                    const double measured[2] = { peak4x, atRequest - modelAtRequest };
                    if (q == 1.5f && sign > 0)
                    {
                        Settings dial = s;
                        dial.hz = false;
                        dial.oversampling = 2;
                        const auto dd = liveCurve(dial, kHost);
                        const double dialPeak = extremumHz(dd.bands[b], dd.sampleRate, 20.0, 30000.0, true);
                        std::printf("    %-5s %-16s %8.1f %9.1f %11.3f dB   %8.1f\n", black ? "Black" : "Brown", e.name,
                                    design, measured[0], measured[1], dialPeak);
                    }
                }

    // Away from the reference gain the band moves by the measured gain law and
    // nothing else: bells stay within -0.5% .. +2.5% of the request.
    for (int black = 0; black < 2; ++black)
        for (float g : { 1.5f, 3.0f, 12.0f, 15.0f, -15.0f })
        {
            Settings s = isolated(Band::HM, true, black, g, 7000.0f, 1.5f, true);
            const auto d = liveCurve(s, kHost);
            const double peak = extremumHz(d.bands[2], d.sampleRate, 5000.0, 9000.0, g > 0);
            const double expected = FourKEQDSP::calibratedEqFrequencyForHz(7000.0f, g, Band::HM, black, true);
            CHECK(std::abs(peak / expected - 1.0) < 1.0e-3 && peak / 7000.0 > 0.995 && peak / 7000.0 < 1.025,
                  "%s HM 7 kHz at %+.1f dB peaks at %.1f Hz (law %.1f)", black ? "Black" : "Brown", g, peak, expected);
        }

    // Shelves: hz is the corner, so the RBJ design (half-gain) frequency sits
    // at hz / sqrt(A) (HF) or hz * sqrt(A) (LF) at the reference gain.
    for (int black = 0; black < 2; ++black)
        for (Band band : { Band::LF, Band::HF })
            for (float hz : band == Band::LF ? std::vector<float>{ 30.0f, 100.0f, 450.0f }
                                             : std::vector<float>{ 1500.0f, 8000.0f, 12000.0f, 16000.0f })
            {
                Settings s = isolated(band, false, black, ref, hz, 1.5f, true);
                const float gainDb = FourKEQDSP::calibratedEqGain(ref, band, black, false);
                const double sqrtA = std::pow(10.0, gainDb / 80.0);
                const double halfGainHz = band == Band::HF ? hz / sqrtA : hz * sqrtA;
                const double design = designsFor(s, kHost).bands[bandIndex(band)].freq;
                CHECK(std::abs(design / halfGainHz - 1.0) < 1.0e-5, "%s shelf %.0f Hz: design %.2f, want %.2f",
                      band == Band::LF ? "LF" : "HF", hz, design, halfGainHz);
                CHECK(std::abs(FourKEQDSP::calibratedEqFrequencyForHz(hz, ref, band, black, false) / halfGainHz - 1.0) < 1.0e-5,
                      "calibratedEqFrequencyForHz disagrees with the corner definition");
                // 4x (RBJ) is exact at its design frequency; the 1x matched
                // shelf is pinned at its lower corner instead, so there it is
                // held to the model within its accuracy.
                for (int os : { 2, 0 })
                {
                    s.oversampling = os;
                    const auto d = liveCurve(s, kHost);
                    const double atHalf = magnitudeDb(d.bands[bandIndex(band)], 2.0 * kPi * halfGainHz / d.sampleRate);
                    const double want = os == 2 ? 0.5 * gainDb : modelDb(designsFor(s, kHost).bands[bandIndex(band)], halfGainHz);
                    const double tolerance = os == 0 ? 0.2 : hz < 100.0f ? 0.1 : 0.02;
                    CHECK(std::abs(atHalf - want) < tolerance, "%s %s shelf %.0f Hz x%d: %.3f dB at %.1f Hz, want %.3f",
                          black ? "Black" : "Brown", band == Band::LF ? "LF" : "HF", hz, 1 << os, atHalf, halfGainHz, want);
                }
            }
}

bool sameDesign(const Design& a, const Design& b, double tolerance)
{
    auto near = [tolerance](double x, double y) { return std::abs(x - y) <= tolerance * std::max(1.0, std::abs(y)); };
    return a.shape == b.shape && near(a.freq, b.freq) && near(a.gainDb, b.gainDb) && near(a.q, b.q);
}

void testHzApiMigratesDialPositions()
{
    // hzForCalibratedEqControl(dial) through the Hz API designs the section the
    // dial API designs at dial, at every gain, across the whole dial range.
    int bands = 0, bandMismatches = 0;
    for (const BandCase& bc : bandCases())
        for (int black = 0; black < 2; ++black)
            for (float dialPos = 0.0f; dialPos <= 1.0001f; dialPos += 0.02f)
            {
                const float lo = bc.dials.front(), hi = bc.dials.back();
                const float dial = lo * std::pow(hi / lo, dialPos);
                const float hz = FourKEQDSP::hzForCalibratedEqControl(dial, bc.band, black, bc.bell);
                for (float g : { -15.0f, -6.0f, 3.0f, 7.5f, 12.0f })
                    for (float q : bc.qs)
                    {
                        const auto a = designsFor(isolated(bc.band, bc.bell, black, g, dial, q, false), 48000.0);
                        const auto b = designsFor(isolated(bc.band, bc.bell, black, g, hz, q, true), 48000.0);
                        ++bands;
                        if (!sameDesign(b.bands[bandIndex(bc.band)], a.bands[bandIndex(bc.band)], 2.0e-5))
                        {
                            ++bandMismatches;
                            const Design& x = b.bands[bandIndex(bc.band)];
                            const Design& y = a.bands[bandIndex(bc.band)];
                            CHECK(false, "%s %s dial %.1f -> %.2f Hz, %+.1f dB: Hz API f %.3f g %.3f q %.4f, dial API f %.3f g %.3f q %.4f",
                                  black ? "Black" : "Brown", bc.name, dial, hz, g, x.freq, x.gainDb, x.q, y.freq, y.gainDb, y.q);
                        }
                    }
            }

    // With both pair members active the interaction model sees the same inputs
    // too, except inside the flat runs at the table ends, where the dial API
    // has several positions for one frequency and the Hz API takes the inner
    // one. Compare away from them.
    int pairs = 0, pairMismatches = 0;
    for (int black = 0; black < 2; ++black)
        for (float t : { 0.1f, 0.3f, 0.5f, 0.7f, 0.85f })
            for (int bell = 0; bell < 2; ++bell)
            {
                Settings s;
                s.eqType = black;
                s.hmGain = 9.0f; s.hmFreq = 600.0f * std::pow(7000.0f / 600.0f, t); s.hmQ = 2.0f;
                s.hfGain = -6.0f; s.hfFreq = 1500.0f * std::pow(16000.0f / 1500.0f, 1.0f - t); s.hfBell = bell;
                s.lfGain = 5.0f; s.lfFreq = 30.0f * std::pow(15.0f, t); s.lfBell = !bell;
                s.lmGain = -8.0f; s.lmFreq = 200.0f * std::pow(12.5f, t); s.lmQ = 0.7f;
                Settings h = s;
                h.hz = true;
                h.lfFreq = FourKEQDSP::hzForCalibratedEqControl(s.lfFreq, Band::LF, black, s.lfBell);
                h.lmFreq = FourKEQDSP::hzForCalibratedEqControl(s.lmFreq, Band::LM, black, true);
                h.hmFreq = FourKEQDSP::hzForCalibratedEqControl(s.hmFreq, Band::HM, black, true);
                h.hfFreq = FourKEQDSP::hzForCalibratedEqControl(s.hfFreq, Band::HF, black, s.hfBell);
                const auto a = designsFor(s, 48000.0), b = designsFor(h, 48000.0);
                for (int i = 0; i < 3; ++i)
                {
                    pairs += 2;
                    const bool low = sameDesign(b.lowCorrection[(size_t)i], a.lowCorrection[(size_t)i], 1.0e-4);
                    const bool high = sameDesign(b.highCorrection[(size_t)i], a.highCorrection[(size_t)i], 1.0e-4);
                    pairMismatches += (low ? 0 : 1) + (high ? 0 : 1);
                    CHECK(low && high, "%s t %.2f: pair correction %d differs between the Hz and dial APIs",
                          black ? "Black" : "Brown", t, i);
                }
            }
    std::printf("[3b] dial -> Hz migration: %d/%d band designs and %d/%d pair-correction designs identical "
                "(to float rounding)\n", bands - bandMismatches, bands, pairs - pairMismatches, pairs);
}

void testHzApiIsContinuous()
{
    // Sweep the requested Hz through each table end in 0.2% steps with both
    // pair members active; no step may jump.
    double worstStep = 0.0;
    for (int black = 0; black < 2; ++black)
        for (const BandCase& bc : bandCases())
        {
            const float lo = bc.hz.front() * 0.6f, hi = bc.hz.back() * 1.5f;
            double previous[6] = {};
            bool first = true;
            for (float hz = lo; hz <= hi; hz *= 1.002f)
            {
                Settings s;
                s.eqType = black;
                s.hz = true;
                s.oversampling = 0;
                s.lfGain = 9.0f; s.lfFreq = 100.0f; s.lfBell = bc.band == Band::LF && bc.bell;
                s.lmGain = -9.0f; s.lmFreq = 800.0f;
                s.hmGain = 9.0f; s.hmFreq = 3000.0f;
                s.hfGain = -9.0f; s.hfFreq = 8000.0f; s.hfBell = bc.band == Band::HF && bc.bell;
                switch (bc.band)
                {
                case Band::LF: s.lfFreq = hz; break;
                case Band::LM: s.lmFreq = hz; break;
                case Band::HM: s.hmFreq = hz; break;
                case Band::HF: s.hfFreq = hz; break;
                }
                const auto d = liveCurve(s, 48000.0);
                const double probes[6] = { 40.0, 150.0, 600.0, 2500.0, 9000.0, 18000.0 };
                for (int i = 0; i < 6; ++i)
                {
                    const double v = sectionsDb(d, probes[i]);
                    if (!first)
                        worstStep = std::max(worstStep, std::abs(v - previous[i]));
                    previous[i] = v;
                }
                first = false;
            }
        }
    CHECK(worstStep < 0.15, "Hz sweep through the table ends jumps by %.3f dB in one 0.2%% step", worstStep);
    std::printf("[3c] Hz sweeps through every table end, 0.2%% steps, pairs active: largest step %.3f dB\n", worstStep);
}

void testLastFrequencySetterWins()
{
    Settings s = isolated(Band::HM, true, false, 9.0f, 5000.0f, 1.5f, true);
    s.oversampling = 0;
    FourKEQDSP viaHzThenDial, dialOnly;
    apply(viaHzThenDial, s);
    Settings d = s;
    d.hz = false;
    d.hmFreq = 2100.0f;
    apply(dialOnly, d);
    viaHzThenDial.prepare(48000.0, 64);
    dialOnly.prepare(48000.0, 64);
    std::vector<float> buf(64, 0.0f);
    float* io[2] = { buf.data(), buf.data() };
    viaHzThenDial.processBlock(io, io, 2, 64);
    const BiquadCoeffs hzCoeffs = FourKEQDSPTestAccess::running(viaHzThenDial).bands[2];
    viaHzThenDial.setHmFreq(2100.0f);
    viaHzThenDial.processBlock(io, io, 2, 64);
    dialOnly.processBlock(io, io, 2, 64);
    CHECK(!sameBits(hzCoeffs, FourKEQDSPTestAccess::running(viaHzThenDial).bands[2]), "setHmFreq did not take over");
    CHECK(sameBits(FourKEQDSPTestAccess::running(viaHzThenDial).bands[2], FourKEQDSPTestAccess::running(dialOnly).bands[2]),
          "after setHmFreq the band is not the dial API's");

    // A re-prepare at a new rate redesigns even with no parameter change.
    viaHzThenDial.prepare(96000.0, 64);
    viaHzThenDial.processBlock(io, io, 2, 64);
    d.oversampling = 0;
    CHECK(sameBits(FourKEQDSPTestAccess::running(viaHzThenDial).bands[2], liveCurve(d, 96000.0).bands[2]),
          "coefficients not redesigned after prepare() at a new rate");
}

// The filters, drawn alone: the HPF at the base rate without its flat trim,
// the LPF at the curve's rate. In dB at freq.
double hpfDb(const FourKEQDSP::CurveCoeffs& d, double freq)
{
    const double w = 2.0 * kPi * freq / d.baseSampleRate;
    return magnitudeDb(d.hpf, w) + (d.hasHpfFirstOrder ? magnitudeDb(d.hpfFirstOrder, w) : 0.0);
}

double lpfDb(const FourKEQDSP::CurveCoeffs& d, double freq)
{
    return magnitudeDb(d.lpf, 2.0 * kPi * freq / d.sampleRate);
}

// Where a filter's response crosses half power between lo and hi, where it
// crosses once.
template <class Response>
double halfPowerHz(Response db, double lo, double hi)
{
    const double halfPower = -10.0 * std::log10(2.0);
    const bool risesThrough = db(lo) < halfPower;
    for (int i = 0; i < 80; ++i)
    {
        const double mid = std::sqrt(lo * hi);
        ((db(mid) < halfPower) == risesThrough ? lo : hi) = mid;
    }
    return std::sqrt(lo * hi);
}

void testHzApiPlacesFilters()
{
    std::printf("[3d] filter Hz API: requested -> measured -3 dB point, worst relative error; 1x LPF above\n"
                "    a quarter of the rate: worst level at the request, against the model's -3.01 dB\n");
    std::printf("    %-10s %5s   %9s   %9s   %9s   %s\n", "", "host", "4x", "1x", "1x high", "dial API at 80, 4x");
    const double halfPowerDb = -10.0 * std::log10(2.0);
    double worst4x = 0.0, worst1x = 0.0, worstHighDb = 0.0;
    for (double host : { 44100.0, 48000.0 })
        for (int black = 0; black < 2; ++black)
            for (int highPass = 0; highPass < 2; ++highPass)
            {
                const std::vector<float> requests = highPass
                    ? std::vector<float>{ 16.0f, 20.0f, 30.0f, 80.0f, 150.0f, 300.0f, 350.0f }
                    : std::vector<float>{ 3000.0f, 5000.0f, 10000.0f, 15201.0f, 20000.0f };
                double worst[2] = { 0.0, 0.0 }, worstHigh = 0.0;
                double dialAt80 = 0.0;
                for (float hz : requests)
                    for (int os : { 2, 0 })
                    {
                        Settings s;
                        s.eqType = black;
                        s.oversampling = os;
                        s.filtersHz = true;
                        (highPass ? s.hpfOn : s.lpfOn) = true;
                        (highPass ? s.hpf : s.lpf) = hz;
                        const auto d = liveCurve(s, host);
                        const double measured = highPass
                            ? halfPowerHz([&d](double f) { return hpfDb(d, f); }, hz * 0.25, hz * 1.3)
                            : halfPowerHz([&d](double f) { return lpfDb(d, f); }, hz * 0.5,
                                          std::min(hz * 1.5, 0.4999 * d.sampleRate));
                        const double err = std::abs(measured / hz - 1.0);
                        // The LPF at 1x is the matched design, which holds the
                        // analog curve to its accuracy near Nyquist: above a
                        // quarter of the rate, where its match point moves off
                        // the corner, it is held to the model's level there
                        // instead. The HPF runs at the base rate whatever the
                        // factor; below 50 Hz its float coefficients move it
                        // by a few tenths of a percent, as they do the dial
                        // API's.
                        if (!highPass && os == 0 && hz >= 0.25 * host)
                        {
                            const double level = std::abs(lpfDb(d, hz) - halfPowerDb);
                            worstHigh = std::max(worstHigh, level);
                            CHECK(level < 1.0, "%s LPF %.0f Hz at %.1f kHz x1: %.3f dB at the request",
                                  black ? "Black" : "Brown", hz, host / 1000, lpfDb(d, hz));
                        }
                        else
                        {
                            worst[os == 2 ? 0 : 1] = std::max(worst[os == 2 ? 0 : 1], err);
                            const double tolerance = highPass ? 5.0e-3 : 2.0e-4;
                            CHECK(err < tolerance, "%s %s %.0f Hz at %.1f kHz x%d: -3 dB at %.2f Hz",
                                  black ? "Black" : "Brown", highPass ? "HPF" : "LPF", hz, host / 1000, 1 << os, measured);
                        }
                        if (highPass && hz == 80.0f && os == 2)
                        {
                            Settings dial = s;
                            dial.filtersHz = false;
                            const auto dd = liveCurve(dial, host);
                            dialAt80 = halfPowerHz([&dd](double f) { return hpfDb(dd, f); }, 5.0, 200.0);
                        }
                    }
                worst4x = std::max(worst4x, worst[0]);
                worst1x = std::max(worst1x, worst[1]);
                worstHighDb = std::max(worstHighDb, worstHigh);
                char label[24];
                std::snprintf(label, sizeof label, "%s %s", black ? "Black" : "Brown", highPass ? "HPF" : "LPF");
                if (highPass)
                    std::printf("    %-10s %5.1f   %8.4f%%   %8.4f%%   %9s   %.1f Hz\n", label, host / 1000,
                                100.0 * worst[0], 100.0 * worst[1], "", dialAt80);
                else
                    std::printf("    %-10s %5.1f   %8.4f%%   %8.4f%%   %6.3f dB\n", label, host / 1000,
                                100.0 * worst[0], 100.0 * worst[1], worstHigh);
            }

    // The design frequency is the requested Hz over the filter's corner
    // ratio, which the prototype's own half-power point defines.
    for (int black = 0; black < 2; ++black)
        for (int highPass = 0; highPass < 2; ++highPass)
        {
            const double q = FourKEQDSP::calibratedFilterQ(highPass, black);
            auto analogDb = [&](double w) {
                const double x = w * w;
                if (!highPass)
                    return -10.0 * std::log10((1.0 - x) * (1.0 - x) + x / (q * q));
                double db = 10.0 * std::log10(x * x / ((1.0 - x) * (1.0 - x) + x / (q * q)));
                if (black)
                    db += 10.0 * std::log10(x / (x + 0.96134252 * 0.96134252));
                return db;
            };
            const double corner = halfPowerHz(analogDb, 0.5, 1.5);
            CHECK(std::abs(FourKEQDSP::filterCornerRatio(highPass, black) / corner - 1.0) < 1.0e-6,
                  "%s %s corner ratio %.8f, prototype %.8f", black ? "Black" : "Brown", highPass ? "HPF" : "LPF",
                  (double)FourKEQDSP::filterCornerRatio(highPass, black), corner);
            CHECK(std::abs(FourKEQDSP::calibratedFilterFrequencyForHz(1000.0f, highPass, black)
                           * FourKEQDSP::filterCornerRatio(highPass, black) / 1000.0 - 1.0) < 1.0e-6,
                  "calibratedFilterFrequencyForHz disagrees with the corner ratio");
        }
    std::printf("    worst: 4x %.4f%%, 1x %.4f%%, 1x LPF high %.3f dB\n", 100.0 * worst4x, 100.0 * worst1x, worstHighDb);
}

void testHzApiMigratesFilterDials()
{
    // hzForCalibratedFilterControl(dial) through the Hz API plays the filter the
    // dial API plays at dial, over the whole dial. Where the measured table is
    // flat several dial positions share one corner and the Hz API reads the
    // trim at the first of them: the largest such step is Brown's 27.8..30 run
    // at 13.6 Hz, 0.0017 dB.
    double worstDb = 0.0;
    int filters = 0;
    for (int black = 0; black < 2; ++black)
        for (int highPass = 0; highPass < 2; ++highPass)
        {
            const float lo = highPass ? 16.0f : 3000.0f, hi = highPass ? 350.0f : 15201.0f;
            for (int step = 0; step <= 200; ++step)
            {
                const float dial = lo * std::pow(hi / lo, step / 200.0f);
                const float hz = FourKEQDSP::hzForCalibratedFilterControl(dial, highPass, black);
                Settings s;
                s.eqType = black;
                s.oversampling = 0;
                (highPass ? s.hpfOn : s.lpfOn) = true;
                (highPass ? s.hpf : s.lpf) = dial;
                Settings h = s;
                h.filtersHz = true;
                (highPass ? h.hpf : h.lpf) = hz;
                ++filters;
                const double designDial = highPass ? FourKEQDSP::calibratedFilterFrequency(dial, true, black)
                                                   : designsFor(s, 48000.0).lpf.freq;
                const double designHz = highPass ? FourKEQDSP::calibratedFilterFrequencyForHz(hz, true, black)
                                                 : designsFor(h, 48000.0).lpf.freq;
                CHECK(std::abs(designHz / designDial - 1.0) < 1.0e-6, "%s %s dial %.1f -> %.2f Hz: design %.4f, dial API %.4f",
                      black ? "Black" : "Brown", highPass ? "HPF" : "LPF", dial, hz, designHz, designDial);
                const auto a = liveCurve(s, 48000.0), b = liveCurve(h, 48000.0);
                for (int i = 0; i <= 60; ++i)
                {
                    const float f = (float)(10.0 * std::pow(2000.0, i / 60.0));
                    worstDb = std::max(worstDb, (double)std::abs(FourKEQDSP::curveDbAt(a, f) - FourKEQDSP::curveDbAt(b, f)));
                }
            }
        }
    CHECK(worstDb < 0.002, "a migrated filter dial plays up to %.4f dB away from the dial API", worstDb);
    std::printf("[3e] dial -> Hz filter migration: %d dials over both filters and voicings, "
                "worst %.4f dB from the dial API\n", filters, worstDb);
}

void testLastFilterSetterWins()
{
    Settings s;
    s.oversampling = 0;
    s.hpfOn = s.lpfOn = true;
    s.hpf = 80.0f; s.lpf = 9000.0f;
    s.filtersHz = true;
    FourKEQDSP viaHzThenDial, dialOnly;
    apply(viaHzThenDial, s);
    Settings d = s;
    d.filtersHz = false;
    apply(dialOnly, d);
    viaHzThenDial.prepare(48000.0, 64);
    dialOnly.prepare(48000.0, 64);
    std::vector<float> buf(64, 0.0f);
    float* io[2] = { buf.data(), buf.data() };
    viaHzThenDial.processBlock(io, io, 2, 64);
    const auto hz = FourKEQDSPTestAccess::running(viaHzThenDial);
    CHECK(sameBits(hz.hpf2, liveCurve(s, 48000.0).hpf) && sameBits(hz.lpf, liveCurve(s, 48000.0).lpf),
          "the Hz API's running filters are not the curve's");
    viaHzThenDial.setHpfFreq(80.0f);
    viaHzThenDial.setLpfFreq(9000.0f);
    viaHzThenDial.processBlock(io, io, 2, 64);
    dialOnly.processBlock(io, io, 2, 64);
    const auto after = FourKEQDSPTestAccess::running(viaHzThenDial), dial = FourKEQDSPTestAccess::running(dialOnly);
    CHECK(!sameBits(hz.hpf2, after.hpf2) && !sameBits(hz.lpf, after.lpf), "the dial setters did not take the filters over");
    CHECK(sameBits(after.hpf2, dial.hpf2) && sameBits(after.lpf, dial.lpf), "after the dial setters the filters are not the dial API's");
}

//==============================================================================
// 4. Extremes
//==============================================================================
void testEveryDesignedSectionIsStable()
{
    long sections = 0, unstable = 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (double host : { 1234.57, 8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int os = 0; os < 3; ++os)
            for (int black = 0; black < 2; ++black)
                for (int hz = 0; hz < 2; ++hz)
                    for (float g : { -15.0f, -9.0f, -3.0f, -0.5f, 0.5f, 3.0f, 9.0f, 15.0f, 40.0f })
                        for (float q : { 0.0f, 0.5f, 1.5f, 3.0f, 50.0f, nan })
                            for (float f : { nan, 0.0f, 1.0f, 20.0f, 30.0f, 100.0f, 450.0f, 2500.0f, 7000.0f,
                                             16000.0f, 21000.0f, 30000.0f, 1.0e5f, inf })
                            {
                                Settings s;
                                s.eqType = black;
                                s.hz = hz;
                                s.oversampling = os;
                                s.lfGain = g; s.lfFreq = f; s.lfBell = q > 1.0f;
                                s.lmGain = -g; s.lmFreq = f; s.lmQ = q;
                                s.hmGain = g; s.hmFreq = f; s.hmQ = q;
                                s.hfGain = -g; s.hfFreq = f; s.hfBell = q < 1.0f;
                                s.lpfOn = true; s.lpf = f;
                                s.hpfOn = hz; s.hpf = f; // the dial API's HPF: see highPassSection
                                s.filtersHz = hz;
                                const auto d = liveCurve(s, host);
                                const BiquadCoeffs* all[] = { &d.bands[0], &d.bands[1], &d.bands[2], &d.bands[3], &d.lpf,
                                                              &d.hpf, &d.hpfFirstOrder,
                                                              &d.lowCorrection[0], &d.lowCorrection[1], &d.lowCorrection[2],
                                                              &d.highCorrection[0], &d.highCorrection[1], &d.highCorrection[2] };
                                for (const BiquadCoeffs* c : all)
                                {
                                    ++sections;
                                    if (!isStable(*c))
                                    {
                                        ++unstable;
                                        CHECK(false, "unstable section at %.2f Hz x%d g %.1f q %.2f f %.1f hz-api %d: "
                                                     "%g %g %g %g %g", host, 1 << os, g, q, f, hz,
                                              c->b0, c->b1, c->b2, c->a1, c->a2);
                                    }
                                }
                            }
    std::printf("[4] %ld designed sections over rates 1.2k..192k x 1/2/4x, every band shape and both filters, "
                "gains to +-40 dB, Q 0..50, frequencies 0..inf and NaN: %ld unstable or non-finite\n", sections, unstable);
}

void testExtremeRendersStayFiniteAndDecay()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<Settings> extremes;
    {
        Settings s; // everything up, Hz API at nonsense frequencies
        s.hz = true;
        s.filtersHz = true;
        s.lfGain = s.lmGain = s.hmGain = s.hfGain = 15.0f;
        s.lfFreq = 1.0f; s.lmFreq = 1.0e6f; s.hmFreq = nan; s.hfFreq = 0.0f;
        s.lmQ = 0.0f; s.hmQ = 100.0f;
        s.lpfOn = true; s.lpf = 0.0f; s.hpfOn = true; s.hpf = 350.0f;
        extremes.push_back(s);
    }
    {
        Settings s; // everything down, dial API outside its range
        s.eqType = 1;
        s.lfGain = s.lmGain = s.hmGain = s.hfGain = -15.0f;
        s.lfFreq = 0.0f; s.lmFreq = 1.0e9f; s.hmFreq = nan; s.hfFreq = -5.0f;
        s.lmQ = nan; s.hmQ = 0.0f; s.lfBell = s.hfBell = true;
        s.lpfOn = true; s.lpf = 1.0e9f;
        extremes.push_back(s);
    }
    {
        Settings s; // range ends through the Hz API, maximum interaction, driven hard
        s.hz = true;
        s.filtersHz = true;
        s.hpfOn = true; s.hpf = 16.0f;
        s.lpfOn = true; s.lpf = 15201.0f;
        s.lfGain = 15.0f; s.lfFreq = 30.0f; s.lfBell = true;
        s.lmGain = -15.0f; s.lmFreq = 200.0f; s.lmQ = 3.0f;
        s.hmGain = 15.0f; s.hmFreq = 7000.0f; s.hmQ = 3.0f;
        s.hfGain = 15.0f; s.hfFreq = 16000.0f;
        s.inputDb = 12.0f; s.autoGain = true; s.ms = true;
        extremes.push_back(s);
    }
    {
        Settings s; // the flat image Dusk Studio pushes from a bypassed strip
        s.lfFreq = s.lmFreq = s.hmFreq = s.hfFreq = 0.0f;
        s.lmQ = s.hmQ = 0.0f;
        s.lpf = s.hpf = 0.0f;
        s.saturation = 22.0f;
        extremes.push_back(s);
    }

    int renders = 0;
    for (double host : { 1234.57, 8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int os = 0; os < 3; ++os)
            for (const Settings& e : extremes)
            {
                Settings s = e;
                s.oversampling = os;
                const int loud = (int)(host * 0.2), silent = (int)(host * 1.0);
                std::array<std::vector<float>, 2> in { std::vector<float>((size_t)(loud + silent), 0.0f),
                                                       std::vector<float>((size_t)(loud + silent), 0.0f) };
                std::uint32_t seed = 99u;
                for (int n = 0; n < loud; ++n)
                    for (int c = 0; c < 2; ++c)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        in[(size_t)c][(size_t)n] = n % 1000 == 0 ? 1.0f
                            : (float)((seed >> 8) & 0xFFFFFF) / (float)0x1000000 * 2.0f - 1.0f;
                    }
                const RenderResult r = render<FourKEQDSP>(host, 256, s, {}, in);
                ++renders;
                bool finite = true, bounded = true, subnormal = false;
                double firstSilent = 0.0, lastSilent = 0.0;
                const int window = std::max(1, (int)(host * 0.05));
                for (int c = 0; c < 2; ++c)
                    for (size_t n = 0; n < r.out[(size_t)c].size(); ++n)
                    {
                        const float v = r.out[(size_t)c][n];
                        finite = finite && std::isfinite(v);
                        bounded = bounded && std::abs(v) < 8.0f;
                        subnormal = subnormal || std::fpclassify(v) == FP_SUBNORMAL;
                        const int i = (int)n;
                        if (i >= loud + window && i < loud + 2 * window)
                            firstSilent = std::max(firstSilent, (double)std::abs(v));
                        if (i >= loud + silent - window)
                            lastSilent = std::max(lastSilent, (double)std::abs(v));
                    }
                CHECK(finite && bounded && !subnormal, "extreme %d at %.2f Hz x%d: finite %d bounded %d subnormal %d",
                      (int)(&e - extremes.data()), host, 1 << os, finite, bounded, subnormal);
                CHECK(lastSilent <= std::max(1.0e-4, 0.5 * firstSilent),
                      "extreme %d at %.2f Hz x%d: tail %.3g after %.3g, not decaying",
                      (int)(&e - extremes.data()), host, 1 << os, lastSilent, firstSilent);
            }
    std::printf("[4b] %d extreme renders (4 settings x 7 rates x 1/2/4x): finite, bounded, no subnormal output, "
                "tails decaying\n", renders);
}
} // namespace

int main()
{
    testReferenceRateIsBitIdentical();
    testFourTimesSoundIsPinned();
    testNoCrampingBelowReferenceRate();
    testAudioPathRunsTheCurve();
    testHzApiPlacesBands();
    testHzApiMigratesDialPositions();
    testHzApiIsContinuous();
    testLastFrequencySetterWins();
    testHzApiPlacesFilters();
    testHzApiMigratesFilterDials();
    testLastFilterSetterWins();
    testEveryDesignedSectionIsStable();
    testExtremeRendersStayFiniteAndDecay();
    if (gFailures > 0)
    {
        std::fprintf(stderr, "FourKEQDSP core tests: %d of %d checks FAILED\n", gFailures, gChecks);
        return 1;
    }
    std::printf("FourKEQDSP core tests: all %d checks passed\n", gChecks);
    return 0;
}
