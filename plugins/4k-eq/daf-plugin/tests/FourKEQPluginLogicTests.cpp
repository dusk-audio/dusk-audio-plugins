// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The 4K EQ 2 plugin layer without a host. The band and filter frequency
// parameters are positions on the console's printed dial, played through the
// core's measured dial law, as in 1.0.5:
//
//   [1] every factory preset loads, for each band and filter, the dial
//       position the measured inverse law gives for the frequency it states
//       at its own gain, mode and shape, and that is the dial 1.0.5 loaded
//       (frozen in FourKEQTestReference.hpp);
//   [2] user preset files read and write as 1.0.5's: version 2 files state
//       each frequency as what its dial position plays (effective_hz) or as
//       the position itself (control_hz), and load onto the saved dials;
//       formats 3 and 4, never released, are refused;
//   [3] the response graph draws what plays: the sections the DSP runs after
//       the plugin's dial calls are the ones designCurve() draws from the
//       controls the editor fills.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQPresetRuntime.hpp"
#include "FourKEQTestReference.hpp"
#include "FourKEQUserPresetFile.hpp"

namespace duskaudio
{
struct FourKEQDSPTestAccess
{
    // The band, pair-correction and filter sections the DSP is running.
    static std::array<BiquadCoeffs, 13> running(const FourKEQDSP& dsp) noexcept
    {
        const auto& c = dsp.ch[0];
        return { c.lf.coeffs(), c.lm.coeffs(), c.hm.coeffs(), c.hf.coeffs(),
                 c.lowCorrection1.coeffs(), c.lowCorrection2.coeffs(), c.lowCorrection3.coeffs(),
                 c.highCorrection1.coeffs(), c.highCorrection2.coeffs(), c.highCorrection3.coeffs(),
                 c.hpf1.coeffs(), c.hpf2.coeffs(), c.lpf.coeffs() };
    }
};
} // namespace duskaudio

namespace
{
using duskaudio::FourKEQDSP;
using duskaudio::FourKEQDSPTestAccess;
using duskaudio::BiquadCoeffs;
using Band = FourKEQDSP::Band;

int gChecks = 0, gFailures = 0;

#define CHECK(cond, ...)                                                              \
    do                                                                                \
    {                                                                                 \
        ++gChecks;                                                                    \
        if (!(cond))                                                                  \
        {                                                                             \
            ++gFailures;                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
            std::fprintf(stderr, __VA_ARGS__);                                        \
            std::fputc('\n', stderr);                                                 \
        }                                                                             \
    } while (false)

struct Values
{
    float v[kParamCount];
    Values() { for (uint32_t i = 0; i < kParamCount; ++i) v[i] = kFourKParams[i].def; }
};

struct BandRow { uint32_t freq, gain; int bellSwitch; Band band; };
// bellSwitch -1: always a bell.
constexpr BandRow kBands[4] = {
    { kLfFreq, kLfGain, kLfBell, Band::LF },
    { kLmFreq, kLmGain, -1, Band::LM },
    { kHmFreq, kHmGain, -1, Band::HM },
    { kHfFreq, kHfGain, kHfBell, Band::HF },
};

void testFactoryPresetsLoadTheMeasuredDials()
{
    CHECK(kNumFactoryPresets == 14, "%d factory presets, 1.0.5 had 14", kNumFactoryPresets);
    if (kNumFactoryPresets != 14)
        return;
    int params = 0;
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const FourKEQPreset& p = kFactoryPresets[i];
        Values s;
        bool written[kParamCount] = {};
        forEachFourKEQFactoryPresetParam(i, [&](uint32_t id, float v) { s.v[id] = v; written[id] = true; ++params; });
        const bool black = p.eqType > 0.5f;
        const float stated[4] = { p.lfFreq, p.lmFreq, p.hmFreq, p.hfFreq };
        const float gains[4] = { p.lfGain, p.lmGain, p.hmGain, p.hfGain };
        const bool bells[4] = { p.lfBell > 0.5f, true, true, p.hfBell > 0.5f };
        for (int b = 0; b < 4; ++b)
        {
            const float want = FourKEQDSP::controlForCalibratedEqFrequency(stated[b], gains[b], kBands[b].band, black, bells[b]);
            CHECK(written[kBands[b].freq] && s.v[kBands[b].freq] == want,
                  "%s band %d loads dial %.9g, the measured inverse law gives %.9g for %.0f Hz", p.name, b,
                  (double)s.v[kBands[b].freq], (double)want, (double)stated[b]);
            CHECK(written[kBands[b].gain] && s.v[kBands[b].gain] == gains[b], "%s band %d gain", p.name, b);
        }
        const float hpf = FourKEQDSP::controlForCalibratedFilterFrequency(p.hpfFreq, true, black);
        const float lpf = FourKEQDSP::controlForCalibratedFilterFrequency(p.lpfFreq, false, black);
        CHECK(written[kHpfFreq] && s.v[kHpfFreq] == hpf, "%s HPF loads dial %.9g, not %.9g", p.name,
              (double)s.v[kHpfFreq], (double)hpf);
        CHECK(written[kLpfFreq] && s.v[kLpfFreq] == lpf, "%s LPF loads dial %.9g, not %.9g", p.name,
              (double)s.v[kLpfFreq], (double)lpf);
        CHECK((s.v[kHpfEnabled] > 0.5f) == (p.hpfFreq > 16.5f) && (s.v[kLpfEnabled] > 0.5f) == (p.lpfFreq < 15200.5f),
              "%s switches the wrong filters in", p.name);
        // The dials 1.0.5 loaded, frozen from its build.
        const fourk_test::PresetDials& frozen = fourk_test::kPresetDials105[i];
        CHECK(std::strcmp(frozen.name, p.name) == 0, "preset %d is \"%s\", 1.0.5's was \"%s\"", i, p.name, frozen.name);
        for (int c = 0; c < 6; ++c)
        {
            const uint32_t id = fourk_test::kPresetDialParams[c];
            CHECK(written[id] && fourk_test::isPresetDial105(i, c, s.v[id]), "%s %s loads dial %.9g (%a), 1.0.5 loaded %.9g",
                  p.name, kFourKParams[id].key, (double)s.v[id], (double)s.v[id], (double)frozen.dial[c]);
        }
        for (uint32_t id = 0; id < kParamCount; ++id)
            CHECK(!written[id] || fkIsPresetParam(id), "%s writes parameter %u, which a preset does not own", p.name, id);
        for (uint32_t id = 0; id < kParamCount; ++id)
            CHECK(!written[id] || (s.v[id] >= kFourKParams[id].min && s.v[id] <= kFourKParams[id].max),
                  "%s puts %s at %.9g, outside its range", p.name, kFourKParams[id].key, (double)s.v[id]);
    }
    std::printf("[1] %d factory presets, %d parameter writes: every band and filter on the dial the measured\n"
                "    inverse law gives for its stated frequency, the dial 1.0.5 loaded\n", kNumFactoryPresets, params);
}

// What 1.0.5's editor saved: every sound-shaping parameter, the frequencies
// as what their dials play at the state's mode, gain and shape.
std::string effectiveHzFile(const Values& s)
{
    std::ostringstream f;
    f.imbue(std::locale::classic());
    f << std::setprecision(std::numeric_limits<float>::max_digits10);
    f << "format_version=2\nfrequency_domain=effective_hz\n";
    const bool black = s.v[kEqType] > 0.5f;
    for (uint32_t i = 0; i < kParamCount; ++i)
    {
        if (!fkIsPresetParam(i))
            continue;
        float v = s.v[i];
        if (i == kHpfFreq || i == kLpfFreq)
            v = FourKEQDSP::calibratedFilterFrequency(v, i == kHpfFreq, black);
        for (const BandRow& b : kBands)
            if (i == b.freq)
                v = FourKEQDSP::calibratedEqFrequency(v, s.v[b.gain], b.band, black,
                                                      b.bellSwitch < 0 || s.v[b.bellSwitch] > 0.5f);
        f << kFourKParams[i].key << '=' << v << '\n';
    }
    return f.str();
}

bool read(const std::string& text, float (&out)[kParamCount])
{
    std::istringstream in(text);
    std::string name;
    return fkReadUserPreset(in, name, out);
}

void testUserPresetFiles()
{
    Values saved;
    saved.v[kEqType] = 1.0f;
    saved.v[kLfBell] = 1.0f;
    saved.v[kHpfEnabled] = 1.0f;
    saved.v[kHpfFreq] = 80.0f;
    saved.v[kLpfFreq] = 9000.0f;
    const float dials[4] = { 380.0f, 290.0f, 6600.0f, 2400.0f };
    const float gains[4] = { 6.0f, -3.0f, 9.0f, 4.5f };
    for (int b = 0; b < 4; ++b)
    {
        saved.v[kBands[b].freq] = dials[b];
        saved.v[kBands[b].gain] = gains[b];
    }
    const uint32_t frequencies[6] = { kLfFreq, kLmFreq, kHmFreq, kHfFreq, kHpfFreq, kLpfFreq };

    // As 1.0.5 wrote it, with effective Hz: each frequency loads onto the dial
    // the measured inverse law gives at the file's own mode, gain and shape,
    // whatever order the file lists them in.
    {
        const std::string text = effectiveHzFile(saved);
        float got[kParamCount];
        CHECK(read(text, got), "a version 2 effective_hz file was rejected");
        for (const uint32_t id : frequencies)
        {
            float stated = 0.0f;
            const std::string key = std::string(kFourKParams[id].key) + "=";
            std::istringstream line(text.substr(text.find(key) + key.size()));
            line.imbue(std::locale::classic());
            line >> stated;
            const float want = fkNormalizeParamValue(id, fkControlForCalibratedFrequency(id, stated, saved.v));
            CHECK(got[id] == want, "effective_hz %s reads dial %.9g, the inverse law gives %.9g", kFourKParams[id].key,
                  (double)got[id], (double)want);
            CHECK(std::abs(got[id] / saved.v[id] - 1.0f) < 1.0e-4f, "effective_hz %s reads dial %.2f, saved %.2f",
                  kFourKParams[id].key, (double)got[id], (double)saved.v[id]);
        }
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fkIsPresetParam(i) && !fkIsFrequencyParam(i))
                CHECK(got[i] == saved.v[i], "effective_hz %s reads %.9g, saved %.9g", kFourKParams[i].key,
                      (double)got[i], (double)saved.v[i]);

        // The same file with every frequency line first.
        std::istringstream lines(text);
        std::string line, first, rest;
        while (std::getline(lines, line))
        {
            bool frequency = false;
            for (const uint32_t id : frequencies)
                frequency = frequency || line.rfind(std::string(kFourKParams[id].key) + "=", 0) == 0;
            (frequency ? first : rest) += line + "\n";
        }
        float reordered[kParamCount];
        CHECK(read(first + rest, reordered), "the reordered file was rejected");
        bool same = true;
        for (uint32_t i = 0; i < kParamCount; ++i)
            same = same && reordered[i] == got[i];
        CHECK(same, "a file with its frequencies before its gains and mode reads differently");
    }

    // control_hz, and no domain at all (the unversioned files before version
    // 2): the dial positions themselves.
    for (const char* domain : { "frequency_domain=control_hz\n", "" })
    {
        std::ostringstream f;
        f.imbue(std::locale::classic());
        f << std::setprecision(std::numeric_limits<float>::max_digits10);
        if (*domain != '\0')
            f << "format_version=2\n";
        f << domain;
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fkIsPresetParam(i))
                f << kFourKParams[i].key << '=' << saved.v[i] << '\n';
        float got[kParamCount];
        CHECK(read(f.str(), got), "a %s file was rejected", *domain ? "control_hz" : "domain-less");
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fkIsPresetParam(i))
                CHECK(got[i] == saved.v[i], "%s %s reads %.9g, saved %.9g", *domain ? "control_hz" : "domain-less",
                      kFourKParams[i].key, (double)got[i], (double)saved.v[i]);
    }

    // This build writes what 1.0.5 wrote, byte for byte, and reads it back
    // onto the saved dials.
    {
        std::ostringstream out;
        fkWriteUserPreset(out, saved.v);
        CHECK(out.str() == effectiveHzFile(saved), "this build writes a file 1.0.5 would not have written:\n%s",
              out.str().c_str());
        float back[kParamCount];
        CHECK(read(out.str(), back), "this build cannot read its own file");
        for (const uint32_t id : frequencies)
            CHECK(std::abs(back[id] / saved.v[id] - 1.0f) < 1.0e-4f, "saved and read back, %s reads dial %.2f, saved %.2f",
                  kFourKParams[id].key, (double)back[id], (double)saved.v[id]);
    }

    // Formats that never shipped, an unknown domain and a malformed number.
    float none[kParamCount];
    for (const char* text : { "format_version=3\nhm_freq=5000\n", "format_version=4\nhm_hz=5000\n",
                              "format_version=2\nfrequency_domain=hz\nhm_freq=5000\n",
                              "format_version=2\nhm_freq=5000x\n" })
        CHECK(!read(text, none), "accepted: %s", text);
    std::printf("[2] user presets: version 2 effective_hz and control_hz files and unversioned ones load onto the\n"
                "    saved dials; this build writes 1.0.5's file; formats 3 and 4 are refused\n");
}

void testCurveDrawsWhatPlays()
{
    // Dial positions inside the knobs' ranges and at their ends, both modes,
    // both shelf/bell switches.
    struct Case { const char* name; int black, lfBell, hfBell; float lf, lm, hm, hf, hpf, lpf; };
    const Case cases[] = {
        { "Brown, both shelves",          0, 0, 0,  90.f,  700.f, 7000.f,  1500.f,  80.f,  9000.f },
        { "Black, LF bell",               1, 1, 0,  33.f,  230.f,  650.f, 16000.f, 350.f,  3000.f },
        { "Black, filters at their ends", 1, 1, 0,  33.f,  230.f, 7000.f,  1500.f,  16.f, 15201.f },
        { "Brown, HF bell",               0, 0, 1, 450.f, 2500.f, 7000.f,  1500.f,  16.f, 12800.f },
    };
    int sections = 0;
    for (const Case& k : cases)
    {
        Values s;
        s.v[kEqType] = (float)k.black;
        s.v[kLfBell] = (float)k.lfBell;
        s.v[kHfBell] = (float)k.hfBell;
        s.v[kLfGain] = 9.f; s.v[kLmGain] = -6.f; s.v[kHmGain] = 9.f; s.v[kHfGain] = -4.5f;
        s.v[kLfFreq] = k.lf; s.v[kLmFreq] = k.lm; s.v[kHmFreq] = k.hm; s.v[kHfFreq] = k.hf;
        s.v[kHpfFreq] = k.hpf; s.v[kLpfFreq] = k.lpf;
        s.v[kHpfEnabled] = s.v[kLpfEnabled] = 1.f;

        // As FourKEQPlugin::setParameterValue() routes these parameters.
        FourKEQDSP dsp;
        dsp.setEqType((int)(s.v[kEqType] + 0.5f));
        dsp.setLfGain(s.v[kLfGain]); dsp.setLfFreq(s.v[kLfFreq]); dsp.setLfBell(s.v[kLfBell] > 0.5f);
        dsp.setLmGain(s.v[kLmGain]); dsp.setLmFreq(s.v[kLmFreq]); dsp.setLmQ(s.v[kLmQ]);
        dsp.setHmGain(s.v[kHmGain]); dsp.setHmFreq(s.v[kHmFreq]); dsp.setHmQ(s.v[kHmQ]);
        dsp.setHfGain(s.v[kHfGain]); dsp.setHfFreq(s.v[kHfFreq]); dsp.setHfBell(s.v[kHfBell] > 0.5f);
        dsp.setHpfFreq(s.v[kHpfFreq]); dsp.setHpfEnabled(s.v[kHpfEnabled] > 0.5f);
        dsp.setLpfFreq(s.v[kLpfFreq]); dsp.setLpfEnabled(s.v[kLpfEnabled] > 0.5f);
        dsp.setOversampling((int)(s.v[kOversampling] + 0.5f));
        dsp.setSaturation(0.0f);
        dsp.prepare(48000.0, 64);
        std::vector<float> buf(64, 0.0f);
        float* io[2] = { buf.data(), buf.data() };
        dsp.processBlock(io, io, 2, 64);
        const auto playing = FourKEQDSPTestAccess::running(dsp);

        // As FourKEQUI::curveControls() fills them: dial positions.
        FourKEQDSP::CurveControls c;
        c.baseSampleRate = 48000.0;
        c.oversampling = s.v[kOversampling];
        c.black = s.v[kEqType] > 0.5f;
        c.hpfEnabled = s.v[kHpfEnabled] > 0.5f;
        c.lpfEnabled = s.v[kLpfEnabled] > 0.5f;
        c.hpfFreq = s.v[kHpfFreq];
        c.lpfFreq = s.v[kLpfFreq];
        c.lfGain = s.v[kLfGain]; c.lfFreq = s.v[kLfFreq]; c.lfBell = s.v[kLfBell];
        c.lmGain = s.v[kLmGain]; c.lmFreq = s.v[kLmFreq]; c.lmQ = s.v[kLmQ];
        c.hmGain = s.v[kHmGain]; c.hmFreq = s.v[kHmFreq]; c.hmQ = s.v[kHmQ];
        c.hfGain = s.v[kHfGain]; c.hfFreq = s.v[kHfFreq]; c.hfBell = s.v[kHfBell];
        c.saturation = 0.0f;
        const auto drawn = FourKEQDSP::designCurve(c);
        const BiquadCoeffs drawnSections[13] = {
            drawn.bands[0], drawn.bands[1], drawn.bands[2], drawn.bands[3],
            drawn.lowCorrection[0], drawn.lowCorrection[1], drawn.lowCorrection[2],
            drawn.highCorrection[0], drawn.highCorrection[1], drawn.highCorrection[2],
            drawn.hpfFirstOrder, drawn.hpf, drawn.lpf };
        for (int i = 0; i < 13; ++i)
        {
            ++sections;
            CHECK(std::memcmp(&playing[(size_t)i], &drawnSections[i], sizeof(BiquadCoeffs)) == 0,
                  "%s: drawn section %d differs from the one playing", k.name, i);
        }
    }
    std::printf("[3] response curve: %d band, pair-correction and filter sections drawn exactly as they play\n", sections);
}
} // namespace

int main()
{
    testFactoryPresetsLoadTheMeasuredDials();
    testUserPresetFiles();
    testCurveDrawsWhatPlays();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
