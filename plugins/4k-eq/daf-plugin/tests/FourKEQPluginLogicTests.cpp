// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The band- and filter-frequency rules of 4K EQ 2 without a host
// (dusk-audio-plugins#288): which of a band's or filter's two parameters wins,
// where the factory presets put each band and filter by the core's definition,
// what the read-out shows as gain moves, and user preset files written before
// and after #288.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "FourKEQBandFrequency.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQPresetRuntime.hpp"
#include "FourKEQUserPresetFile.hpp"

namespace duskaudio
{
struct FourKEQDSPTestAccess
{
    static FourKEQDSP::SectionDesigns designs(const FourKEQDSP::CurveControls& c) noexcept
    {
        return FourKEQDSP::designSections(FourKEQDSP::coeffInputsFor(c));
    }

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
using Band = FourKEQDSP::Band;
using duskaudio::BiquadCoeffs;

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

void testLastWriteWins()
{
    Values s;
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 0u, "a fresh state follows a legacy dial");
    fkStoreParam(s.v, kHmFreq, 5000.0f);
    CHECK(fkBandFollowsLegacyDial(s.v, 2), "a legacy HM dial write did not take the band");
    CHECK(!fkBandFollowsLegacyDial(s.v, 0) && !fkBandFollowsLegacyDial(s.v, 3), "other bands moved");
    CHECK(std::abs(fkBandHz(s.v, 2) - FourKEQDSP::hzForCalibratedEqControl(5000.0f, Band::HM, false, true)) < 1e-3f,
          "a legacy HM band reads %.1f Hz", fkBandHz(s.v, 2));
    fkStoreParam(s.v, kHmHz, 6500.0f);
    CHECK(!fkBandFollowsLegacyDial(s.v, 2) && fkBandHz(s.v, 2) == 6500.0f, "an HM Hz write did not take the band back");
    fkStoreParam(s.v, kLegacyDialBands, 9.0f);
    CHECK(fkBandFollowsLegacyDial(s.v, 0) && fkBandFollowsLegacyDial(s.v, 3) && !fkBandFollowsLegacyDial(s.v, 2),
          "restoring kLegacyDialBands did not select the bands it names");
    fkStoreParam(s.v, kLegacyDialBands, 99.0f);
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 15u, "an out-of-range selector was not clamped");
    fkStoreParam(s.v, kLegacyDialBands, std::nanf(""));
    CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 0u, "a NaN selector is not 'no legacy bands'");

    Values f;
    CHECK(fkLegacyDialFilterBits(f.v[kLegacyDialFilters]) == 0u, "a fresh state follows a legacy filter dial");
    fkStoreParam(f.v, kHpfFreq, 120.0f);
    CHECK(fkFilterFollowsLegacyDial(f.v, 0) && !fkFilterFollowsLegacyDial(f.v, 1),
          "a legacy HPF dial write did not take the HPF alone");
    CHECK(fkLegacyDialBits(f.v[kLegacyDialBands]) == 0u, "a filter write moved a band");
    CHECK(std::abs(fkFilterHz(f.v, 0) - FourKEQDSP::hzForCalibratedFilterControl(120.0f, true, false)) < 1e-3f,
          "a legacy HPF reads %.1f Hz", fkFilterHz(f.v, 0));
    fkStoreParam(f.v, kHmFreq, 5000.0f);
    CHECK(fkFilterFollowsLegacyDial(f.v, 0) && fkBandFollowsLegacyDial(f.v, 2), "a band write moved a filter");
    fkStoreParam(f.v, kHpfHz, 80.0f);
    CHECK(!fkFilterFollowsLegacyDial(f.v, 0) && fkFilterHz(f.v, 0) == 80.0f, "an HPF Hz write did not take the HPF back");
    fkStoreParam(f.v, kLpfFreq, 9000.0f);
    fkStoreParam(f.v, kLegacyDialFilters, 1.0f);
    CHECK(fkFilterFollowsLegacyDial(f.v, 0) && !fkFilterFollowsLegacyDial(f.v, 1),
          "restoring kLegacyDialFilters did not select the filters it names");
    fkStoreParam(f.v, kLegacyDialFilters, 99.0f);
    CHECK(fkLegacyDialFilterBits(f.v[kLegacyDialFilters]) == 3u, "an out-of-range filter selector was not clamped");
    fkStoreParam(f.v, kLegacyDialFilters, std::nanf(""));
    CHECK(fkLegacyDialFilterBits(f.v[kLegacyDialFilters]) == 0u, "a NaN filter selector is not 'no legacy filters'");
}

void testLegacyDialDefaults()
{
    const float shipped[4] = { 200.f, 1000.f, 3000.f, 8000.f };
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        const float dial = kFourKParams[ids.legacyDial].def;
        const float hz = FourKEQDSP::hzForCalibratedEqControl(dial, ids.band, false, ids.bellSwitch < 0 || b == 3);
        CHECK(std::abs(hz / kFourKParams[ids.hz].def - 1.0f) < 1.0e-4f,
              "band %d legacy dial default %.4f plays %.2f Hz, Hz default %.0f", b, dial, hz, kFourKParams[ids.hz].def);
        CHECK(dial != shipped[b], "band %d legacy dial default is 1.0.5's", b);
    }
    const float shippedFilters[2] = { 16.f, 15201.f };
    for (int f = 0; f < 2; ++f)
    {
        const FourKEQFilterIds& ids = kFourKEQFilters[f];
        const float dial = kFourKParams[ids.legacyDial].def;
        const float hz = FourKEQDSP::hzForCalibratedFilterControl(dial, ids.highPass, false);
        CHECK(std::abs(hz / kFourKParams[ids.hz].def - 1.0f) < 1.0e-4f,
              "filter %d legacy dial default %.4f plays %.2f Hz, Hz default %.0f", f, dial, hz, kFourKParams[ids.hz].def);
        CHECK(dial != shippedFilters[f], "filter %d legacy dial default is 1.0.5's", f);
    }
}

// The Hz a designed band plays, by the core's definition: a bell's centre, a
// shelf's full-boost corner, at the reference gain.
float designedHz(const FourKEQDSP::SectionDesign& d, Band band, bool black, bool bell)
{
    if (bell || band == Band::LM || band == Band::HM)
        return d.freq;
    const float gainDb = FourKEQDSP::calibratedEqGain(FourKEQDSP::kEqReferenceGainDb, band, black, false);
    const float sqrtA = std::pow(10.0f, gainDb / 80.0f);
    return band == Band::HF ? d.freq * sqrtA : d.freq / sqrtA;
}

void testFactoryPresetsStateTheirHz()
{
    std::printf("[2] factory presets, band and filter Hz by the core's definition (was: what the pre-#288 build played)\n");
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const FourKEQPreset& p = kFactoryPresets[i];
        Values s;
        forEachFourKEQFactoryPresetParam(i, [&](uint32_t id, float v) { fkStoreParam(s.v, id, v); });
        const float stated[4] = { p.lfFreq, p.lmFreq, p.hmFreq, p.hfFreq };
        CHECK(fkLegacyDialBits(s.v[kLegacyDialBands]) == 0u, "%s leaves a band on its legacy dial", p.name);

        // Designed at the reference gain, where the definition is exact.
        FourKEQDSP::CurveControls c;
        c.black = p.eqType > 0.5f;
        c.bandFrequenciesInHz = true;
        c.lfBell = p.lfBell; c.hfBell = p.hfBell; c.lmQ = p.lmQ; c.hmQ = p.hmQ;
        c.lfGain = c.lmGain = c.hmGain = c.hfGain = FourKEQDSP::kEqReferenceGainDb;
        c.lfFreq = fkBandHz(s.v, 0); c.lmFreq = fkBandHz(s.v, 1);
        c.hmFreq = fkBandHz(s.v, 2); c.hfFreq = fkBandHz(s.v, 3);
        const auto d = FourKEQDSPTestAccess::designs(c);

        // What the pre-#288 build played: the Hz inverted through the
        // gain-dependent dial law, clamped at the dial's ends.
        float before[4];
        for (int b = 0; b < 4; ++b)
        {
            const FourKEQBandIds& ids = kFourKEQBands[b];
            const bool bell = fkBandIsBell(s.v, b);
            const float dial = FourKEQDSP::controlForCalibratedEqFrequency(
                stated[b], s.v[ids.gain], ids.band, c.black, bell);
            before[b] = FourKEQDSP::hzForCalibratedEqControl(dial, ids.band, c.black, bell);
            const float got = designedHz(d.bands[b], ids.band, c.black, bell);
            CHECK(std::abs(got / stated[b] - 1.0f) < 1.0e-4f, "%s band %d designs %.1f Hz, states %.1f",
                  p.name, b, got, stated[b]);
            CHECK(std::abs(fkBandHz(s.v, b) - stated[b]) < 1.0e-3f, "%s band %d reads %.1f Hz", p.name, b, fkBandHz(s.v, b));
        }
        std::printf("  %-22s %-5s LF %5.0f%s (%4.0f)  LM %5.0f (%5.0f)  HM %5.0f (%5.0f)  HF %5.0f%s (%5.0f)\n",
                    p.name, c.black ? "Black" : "Brown",
                    stated[0], p.lfBell > 0.5f ? "b" : "s", before[0], stated[1], before[1],
                    stated[2], before[2], stated[3], p.hfBell > 0.5f ? "b" : "s", before[3]);

        // The filters: each stated -3 dB point, where the pre-#288 build put
        // the design frequency at the stated number through the dial.
        const float statedFilters[2] = { p.hpfFreq, p.lpfFreq };
        CHECK(fkLegacyDialFilterBits(s.v[kLegacyDialFilters]) == 0u, "%s leaves a filter on its legacy dial", p.name);
        CHECK((s.v[kHpfEnabled] > 0.5f) == (p.hpfFreq > 16.5f) && (s.v[kLpfEnabled] > 0.5f) == (p.lpfFreq < 15200.5f),
              "%s switches the wrong filters in", p.name);
        float beforeFilters[2];
        for (int f = 0; f < 2; ++f)
        {
            const bool highPass = kFourKEQFilters[f].highPass;
            CHECK(std::abs(fkFilterHz(s.v, f) - statedFilters[f]) < 1.0e-3f, "%s filter %d reads %.1f Hz", p.name, f, fkFilterHz(s.v, f));
            const float design = FourKEQDSP::calibratedFilterFrequencyForHz(fkFilterHz(s.v, f), highPass, c.black);
            CHECK(std::abs(design * FourKEQDSP::filterCornerRatio(highPass, c.black) / statedFilters[f] - 1.0f) < 1.0e-5f,
                  "%s filter %d designs %.1f Hz", p.name, f, design);
            beforeFilters[f] = FourKEQDSP::hzForCalibratedFilterControl(
                FourKEQDSP::controlForCalibratedFilterFrequency(statedFilters[f], highPass, c.black), highPass, c.black);
        }
        if (s.v[kHpfEnabled] > 0.5f || s.v[kLpfEnabled] > 0.5f)
            std::printf("  %-22s       HPF %5.0f (%5.1f)  LPF %5.0f (%5.0f)\n", "", statedFilters[0], beforeFilters[0],
                        statedFilters[1], beforeFilters[1]);
    }
}

void testReadoutIgnoresGain()
{
    int cases = 0;
    double oldWorst = 0.0;
    for (int black = 0; black < 2; ++black)
        for (int bell = 0; bell < 2; ++bell)
            for (int b = 0; b < 4; ++b)
                for (int legacy = 0; legacy < 2; ++legacy)
                {
                    const FourKEQBandIds& ids = kFourKEQBands[b];
                    Values s;
                    s.v[kEqType] = (float)black;
                    s.v[kLfBell] = s.v[kHfBell] = (float)bell;
                    const float mid = std::sqrt(kFourKParams[ids.hz].min * kFourKParams[ids.hz].max);
                    fkStoreParam(s.v, legacy ? ids.legacyDial : ids.hz, mid);
                    char first[32];
                    s.v[ids.gain] = -15.0f;
                    std::snprintf(first, sizeof(first), kFourKBandHzFormat, fkBandHz(s.v, b));
                    const float atMinus15 = FourKEQDSP::calibratedEqFrequency(mid, -15.0f, ids.band, black, fkBandIsBell(s.v, b));
                    for (float g = -15.0f; g <= 15.0f; g += 0.5f)
                    {
                        s.v[ids.gain] = g;
                        char text[32];
                        std::snprintf(text, sizeof(text), kFourKBandHzFormat, fkBandHz(s.v, b));
                        CHECK(std::string(text) == first, "band %d %s: read-out %s at %+.1f dB, %s at -15 dB",
                              b, legacy ? "legacy dial" : "Hz", text, g, first);
                        ++cases;
                        const float old = FourKEQDSP::calibratedEqFrequency(mid, g, ids.band, black, fkBandIsBell(s.v, b));
                        oldWorst = std::max(oldWorst, std::abs((double)old / atMinus15 - 1.0));
                    }
                }
    std::printf("[3] read-out across gain: %d settings, one text per band; the pre-#288 read-out moved by up to %.0f%%\n",
                cases, 100.0 * oldWorst);
}

void testCurveDrawsWhatPlays()
{
    // Mixed states included: a legacy dial inside the flat run at the end of
    // its table plays a different pair correction than its Hz equivalent.
    struct Case { const char* name; int black, lfBell, hfBell; uint32_t writes[6]; float values[6]; };
    const Case cases[] = {
        { "all Hz",                  0, 0, 0, { kLfHz, kLmHz, kHmHz, kHfHz, kHpfHz, kLpfHz },
                                              { 90.f, 700.f, 7000.f, 1500.f, 80.f, 9000.f } },
        { "all legacy dial",         1, 1, 0, { kLfFreq, kLmFreq, kHmFreq, kHfFreq, kHpfFreq, kLpfFreq },
                                              { 33.f, 230.f, 650.f, 16000.f, 350.f, 3000.f } },
        { "legacy LF/LM at the ends", 1, 1, 0, { kLfFreq, kLmFreq, kHmHz, kHfHz, kHpfFreq, kLpfHz },
                                              { 33.f, 230.f, 7000.f, 1500.f, 16.f, 15201.f } },
        { "legacy HM/HF",            0, 0, 1, { kLfHz, kLmHz, kHmFreq, kHfFreq, kHpfHz, kLpfFreq },
                                              { 450.f, 2500.f, 7000.f, 1500.f, 16.f, 12800.f } },
    };
    int sections = 0;
    for (const Case& k : cases)
    {
        Values s;
        s.v[kEqType] = (float)k.black;
        s.v[kLfBell] = (float)k.lfBell;
        s.v[kHfBell] = (float)k.hfBell;
        s.v[kLfGain] = 9.f; s.v[kLmGain] = -6.f; s.v[kHmGain] = 9.f; s.v[kHfGain] = -4.5f;
        for (int i = 0; i < 6; ++i)
            fkStoreParam(s.v, k.writes[i], k.values[i]);

        FourKEQDSP dsp;
        dsp.setEqType(k.black); dsp.setLfBell(k.lfBell); dsp.setHfBell(k.hfBell);
        dsp.setLfGain(s.v[kLfGain]); dsp.setLmGain(s.v[kLmGain]);
        dsp.setHmGain(s.v[kHmGain]); dsp.setHfGain(s.v[kHfGain]);
        dsp.setHpfEnabled(true); dsp.setLpfEnabled(true);
        fkApplyBandFrequencies(dsp, s.v);
        fkApplyFilterFrequencies(dsp, s.v);
        dsp.prepare(48000.0, 64);
        std::vector<float> buf(64, 0.0f);
        float* io[2] = { buf.data(), buf.data() };
        dsp.processBlock(io, io, 2, 64);
        const auto playing = FourKEQDSPTestAccess::running(dsp);

        FourKEQDSP::CurveControls c;
        c.baseSampleRate = 48000.0;
        c.oversampling = 2.0f;
        c.black = k.black; c.lfBell = s.v[kLfBell]; c.hfBell = s.v[kHfBell];
        c.lfGain = s.v[kLfGain]; c.lmGain = s.v[kLmGain]; c.hmGain = s.v[kHmGain]; c.hfGain = s.v[kHfGain];
        c.lmQ = s.v[kLmQ]; c.hmQ = s.v[kHmQ];
        c.hpfEnabled = c.lpfEnabled = true;
        fkSetCurveBandFrequencies(c, s.v);
        fkSetCurveFilterFrequencies(c, s.v);
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
    std::printf("[5] response curve: %d band, pair-correction and filter sections drawn exactly as they play\n", sections);
}

std::string oldEffectiveHzFile(const Values& s)
{
    // What the pre-#288 editor saved: every band as the gain-dependent
    // frequency its dial played, under the dial parameter's key.
    std::ostringstream f;
    f.imbue(std::locale::classic());
    f << std::setprecision(9);
    f << "name=Old\nformat_version=2\nfrequency_domain=effective_hz\n";
    const bool black = s.v[kEqType] > 0.5f;
    for (uint32_t i : { (uint32_t)kHpfFreq, (uint32_t)kHpfEnabled, (uint32_t)kLpfFreq, (uint32_t)kLpfEnabled,
                        (uint32_t)kLfGain, (uint32_t)kLfBell, (uint32_t)kLmGain, (uint32_t)kLmQ,
                        (uint32_t)kHmGain, (uint32_t)kHmQ, (uint32_t)kHfGain, (uint32_t)kHfBell,
                        (uint32_t)kEqType, (uint32_t)kInputGain, (uint32_t)kOutputGain, (uint32_t)kAutoGain })
    {
        float v = s.v[i];
        if (i == kHpfFreq || i == kLpfFreq)
            v = FourKEQDSP::calibratedFilterFrequency(v, i == kHpfFreq, black);
        f << kFourKParams[i].key << '=' << v << '\n';
    }
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        f << kFourKParams[ids.legacyDial].key << '='
          << FourKEQDSP::calibratedEqFrequency(s.v[ids.legacyDial], s.v[ids.gain], ids.band, black, fkBandIsBell(s.v, b))
          << '\n';
    }
    return f.str();
}

void testUserPresetFiles()
{
    Values old;
    old.v[kEqType] = 1.0f;
    old.v[kHfBell] = 0.0f;
    const float dials[4] = { 380.0f, 290.0f, 6600.0f, 2400.0f };
    const float gains[4] = { 6.0f, -3.0f, 9.0f, 4.5f };
    for (int b = 0; b < 4; ++b)
    {
        old.v[kFourKEQBands[b].legacyDial] = dials[b];
        old.v[kFourKEQBands[b].gain] = gains[b];
    }
    for (const char* domain : { "effective", "control" })
    {
        std::string text = oldEffectiveHzFile(old);
        if (domain[0] == 'c')
        {
            std::ostringstream f;
            f << "name=Old\nformat_version=2\nfrequency_domain=control_hz\neq_type=1\n";
            for (int b = 0; b < 4; ++b)
                f << kFourKParams[kFourKEQBands[b].legacyDial].key << '=' << dials[b] << '\n'
                  << kFourKParams[kFourKEQBands[b].gain].key << '=' << gains[b] << '\n';
            text = f.str();
        }
        std::istringstream in(text);
        std::string name;
        float read[kParamCount];
        CHECK(fkReadUserPreset(in, name, read), "a format 2 (%s_hz) preset was rejected", domain);
        CHECK(fkLegacyDialBits(read[kLegacyDialBands]) == 15u, "a format 2 (%s_hz) preset left a band off its dial", domain);
        for (int b = 0; b < 4; ++b)
            CHECK(std::abs(read[kFourKEQBands[b].legacyDial] / dials[b] - 1.0f) < 1.0e-4f,
                  "format 2 (%s_hz) band %d reads dial %.2f, saved %.2f", domain, b,
                  read[kFourKEQBands[b].legacyDial], dials[b]);

        // Saved again by this build, and read back: the same dials.
        std::ostringstream again;
        fkWriteUserPreset(again, read);
        std::istringstream back(again.str());
        float reread[kParamCount];
        CHECK(fkReadUserPreset(back, name, reread), "a re-saved format 2 preset was rejected");
        for (int b = 0; b < 4; ++b)
            CHECK(fkBandFollowsLegacyDial(reread, b)
                      && reread[kFourKEQBands[b].legacyDial] == read[kFourKEQBands[b].legacyDial],
                  "re-saved band %d did not keep its dial", b);
    }

    Values now;
    fkStoreParam(now.v, kHmHz, 7000.0f);
    fkStoreParam(now.v, kHfHz, 1500.0f);
    fkStoreParam(now.v, kLfFreq, 400.0f);
    std::ostringstream out;
    fkWriteUserPreset(out, now.v);
    std::istringstream in(out.str());
    std::string name;
    float read[kParamCount];
    CHECK(fkReadUserPreset(in, name, read), "a format 4 preset was rejected");
    CHECK(read[kHmHz] == 7000.0f && read[kHfHz] == 1500.0f && !fkBandFollowsLegacyDial(read, 2),
          "format 4 lost its Hz bands");
    CHECK(fkBandFollowsLegacyDial(read, 0) && read[kLfFreq] == 400.0f, "format 4 lost its legacy LF dial");

    // Filters: a format 3 file stored them as dial positions, written as their
    // design frequency; they load onto those dials.
    Values three;
    three.v[kEqType] = 1.0f;
    fkStoreParam(three.v, kHpfFreq, 120.0f);
    fkStoreParam(three.v, kLpfFreq, 8800.0f);
    fkStoreParam(three.v, kHmHz, 5000.0f);
    std::ostringstream v3;
    v3.imbue(std::locale::classic());
    v3 << std::setprecision(9) << "name=Three\nformat_version=3\nfrequency_domain=effective_hz\neq_type=1\n"
       << "hpf_freq=" << FourKEQDSP::calibratedFilterFrequency(120.0f, true, true) << '\n'
       << "lpf_freq=" << FourKEQDSP::calibratedFilterFrequency(8800.0f, false, true) << '\n'
       << "hpf_hz=300\nhm_hz=5000\n";
    std::istringstream in3(v3.str());
    float read3[kParamCount];
    CHECK(fkReadUserPreset(in3, name, read3), "a format 3 preset was rejected");
    CHECK(fkLegacyDialFilterBits(read3[kLegacyDialFilters]) == 3u, "a format 3 preset left a filter off its dial");
    CHECK(std::abs(read3[kHpfFreq] / 120.0f - 1.0f) < 1.0e-4f && std::abs(read3[kLpfFreq] / 8800.0f - 1.0f) < 1.0e-4f,
          "format 3 filters read dials %.2f / %.1f", read3[kHpfFreq], read3[kLpfFreq]);
    CHECK(read3[kHmHz] == 5000.0f && !fkBandFollowsLegacyDial(read3, 2), "format 3 lost its Hz band");

    Values four;
    fkStoreParam(four.v, kHpfHz, 80.0f);
    fkStoreParam(four.v, kLpfFreq, 12800.0f);
    std::ostringstream out4;
    fkWriteUserPreset(out4, four.v);
    CHECK(out4.str().find("format_version=4") != std::string::npos, "this build does not write format 4");
    std::istringstream in4(out4.str());
    float read4[kParamCount];
    CHECK(fkReadUserPreset(in4, name, read4), "a format 4 preset was rejected");
    CHECK(read4[kHpfHz] == 80.0f && !fkFilterFollowsLegacyDial(read4, 0), "format 4 lost its Hz HPF");
    CHECK(fkFilterFollowsLegacyDial(read4, 1) && std::abs(read4[kLpfFreq] / 12800.0f - 1.0f) < 1.0e-4f,
          "format 4 lost its legacy LPF dial (%.1f)", read4[kLpfFreq]);
    std::printf("[4] user presets: format 2 (effective_hz, control_hz) loads onto the saved dials; format 3's filters\n"
                "    load onto theirs; format 4 round-trips Hz and legacy bands and filters\n");
}
} // namespace

int main()
{
    std::printf("[1] last write wins\n");
    testLastWriteWins();
    testLegacyDialDefaults();
    testFactoryPresetsStateTheirHz();
    testReadoutIgnoresGain();
    testUserPresetFiles();
    testCurveDrawsWhatPlays();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
