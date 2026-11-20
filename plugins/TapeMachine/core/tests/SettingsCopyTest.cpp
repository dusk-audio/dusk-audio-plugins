// SettingsCopyTest: TapeMachineSettings::copySettingsFrom() carries every setting.
//
// For each setter: an instance A with that one setting changed (on top of whatever the setting
// needs in order to be heard), a plain settings holder that takes A's settings, and an instance
// B that takes the holder's. B's render must be A's, bit for bit; and A's must differ from the
// render without the setting, or the comparison would hold nothing for it.
// Three settings are stored and have no effect on the output (see `stored only`); for those
// the test holds exactly that, so that the day one of them comes alive it turns red here and
// gets a case of its own, and it reads the copied value itself.
//
//   SettingsCopyTest            prints failures and a summary
//   SettingsCopyTest --print    every case
#include "TapeMachineDSP.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
using duskaudio::TapeMachineDSP;
using duskaudio::TapeMachineSettings;
using Setup = std::function<void (TapeMachineDSP&)>;

int failures = 0;
bool printAll = false;

void hold (const std::string& what, bool ok, const char* reading)
{
    if (! ok) ++failures;
    if (! ok || printAll) std::printf ("%s %s: %s\n", ok ? "  ok" : "FAIL", what.c_str(), reading);
}

// A third of a second of a wide chord. Its three partials under 500 Hz peak at -7 dBFS
// together, above the -12 dBFS anchor that the programme trims engage from. The right
// channel is lower and without the top partial, so that crosstalk has something to carry.
std::vector<float> render (TapeMachineDSP& dsp)
{
    const double rate = 48000.0;
    const int block = 64, blocks = 250;
    static constexpr double freqs[6] = { 55.0, 110.0, 440.0, 1200.0, 3300.0, 9000.0 };
    dsp.prepare (rate, block);
    std::vector<float> out (static_cast<size_t> (2 * block * blocks));
    std::vector<float> inL (block), inR (block), outL (block), outR (block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const double t = static_cast<double> (b * block + i) / rate;
            double all = 0.0, noTop = 0.0;
            for (int k = 0; k < 6; ++k)
            {
                const double v = std::sin (6.283185307179586 * freqs[k] * t);
                all += v;
                if (k < 5) noTop += v;
            }
            inL[static_cast<size_t> (i)] = static_cast<float> (0.9 * all / 6.0);
            inR[static_cast<size_t> (i)] = static_cast<float> (0.55 * noTop / 5.0);
        }
        const float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { outL.data(), outR.data() };
        dsp.processBlock (ins, outs, 2, block);
        std::memcpy (out.data() + static_cast<size_t> (2 * b * block), outL.data(), sizeof (float) * static_cast<size_t> (block));
        std::memcpy (out.data() + static_cast<size_t> ((2 * b + 1) * block), outR.data(), sizeof (float) * static_cast<size_t> (block));
    }
    return out;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
bool finiteAndNotSilent (const std::vector<float>& a)
{
    float peak = 0.0f;
    for (const float v : a)
    {
        if (! std::isfinite (v)) return false;
        peak = std::max (peak, std::abs (v));
    }
    return peak > 1.0e-3f;
}

// A through a holder into B.
std::vector<float> renderCopyOf (const TapeMachineDSP& a)
{
    TapeMachineSettings holder;
    holder.copySettingsFrom (a);
    TapeMachineDSP b;
    b.copySettingsFrom (holder);
    return render (b);
}

void american (TapeMachineDSP& d) { d.setTapeMachine (1); }
void nothing (TapeMachineDSP&)    {}

// The three settings no render can show, read where they are stored.
struct Stored : TapeMachineSettings
{
    float saturation() const   { return pSaturation.load (std::memory_order_relaxed); }
    bool  noiseEnabled() const { return pNoiseEnabled.load (std::memory_order_relaxed); }
    int   oversampling() const { return pOversampling.load (std::memory_order_relaxed); }
};

// `needs` is what the setting needs around it to be heard, where it needs something; `set`
// is the setting itself.
void live (const char* name, const Setup& needs, const Setup& set)
{
    TapeMachineDSP a, without;
    needs (a); set (a);
    needs (without);
    const std::vector<float> ofA = render (a), ofCopy = renderCopyOf (a), ofWithout = render (without);
    hold (name, finiteAndNotSilent (ofA), "the render with the setting is finite and not silent");
    hold (name, ! same (ofA, ofWithout), "the setting changes the render (or this case holds nothing)");
    hold (name, same (ofA, ofCopy), "the copy renders the same bits");
}

void storedOnly (const char* name, const Setup& set)
{
    TapeMachineDSP a, without;
    set (a);
    hold (name, same (render (a), render (without)), "stored only: no effect on the render (if it has one now, give it a case)");
}
} // namespace

int main (int argc, char** argv)
{
    printAll = argc > 1 && std::string (argv[1]) == "--print";

    live ("setTapeMachine", nothing, american);
    live ("setTapeSpeed", nothing, [] (TapeMachineDSP& d) { d.setTapeSpeed (2); });
    live ("setTapeType", nothing, [] (TapeMachineDSP& d) { d.setTapeType (2); });
    live ("setSignalPath", nothing, [] (TapeMachineDSP& d) { d.setSignalPath (2); });
    live ("setEqStandard", nothing, [] (TapeMachineDSP& d) { d.setEqStandard (1); });
    live ("setInputGainDb", nothing, [] (TapeMachineDSP& d) { d.setInputGainDb (6.0f); });
    live ("setBias", [] (TapeMachineDSP& d) { d.setAutoCal (false); }, [] (TapeMachineDSP& d) { d.setBias (30.0f); });
    live ("setCalibration", nothing, [] (TapeMachineDSP& d) { d.setCalibration (3); });
    live ("setAutoCal", [] (TapeMachineDSP& d) { d.setBias (30.0f); }, [] (TapeMachineDSP& d) { d.setAutoCal (false); });
    live ("setHighpassHz", nothing, [] (TapeMachineDSP& d) { d.setHighpassHz (200.0f); });
    live ("setLowpassHz", nothing, [] (TapeMachineDSP& d) { d.setLowpassHz (5000.0f); });
    live ("setNoiseAmount", nothing, [] (TapeMachineDSP& d) { d.setNoiseAmount (40.0f); });
    live ("setWow", nothing, [] (TapeMachineDSP& d) { d.setWow (40.0f); });
    live ("setFlutter", nothing, [] (TapeMachineDSP& d) { d.setFlutter (30.0f); });
    live ("setOutputGainDb", [] (TapeMachineDSP& d) { d.setAutoComp (false); }, [] (TapeMachineDSP& d) { d.setOutputGainDb (-4.0f); });
    live ("setAutoComp", [] (TapeMachineDSP& d) { d.setInputGainDb (6.0f); d.setOutputGainDb (2.0f); }, [] (TapeMachineDSP& d) { d.setAutoComp (false); });
    live ("setHeadWidth", american, [] (TapeMachineDSP& d) { d.setHeadWidth (2); });
    live ("setCrosstalk", american, [] (TapeMachineDSP& d) { d.setCrosstalk (false); });
    live ("setWowFlutterEnabled", american, [] (TapeMachineDSP& d) { d.setWowFlutterEnabled (false); });
    live ("setTransformer", american, [] (TapeMachineDSP& d) { d.setTransformer (false); });
    live ("setReproLf", nothing, [] (TapeMachineDSP& d) { d.setReproLf (4.0f); });
    live ("setReproLmf", nothing, [] (TapeMachineDSP& d) { d.setReproLmf (4.0f); });
    live ("setReproHmf", nothing, [] (TapeMachineDSP& d) { d.setReproHmf (4.0f); });
    live ("setReproHf", nothing, [] (TapeMachineDSP& d) { d.setReproHf (4.0f); });
    live ("setReproSubBell", nothing, [] (TapeMachineDSP& d) { d.setReproSubBell (6.0f); });
    live ("setLevelHmfTrim", nothing, [] (TapeMachineDSP& d) { d.setLevelHmfTrim (-6.0f); });
    live ("setLevelHfTrim", nothing, [] (TapeMachineDSP& d) { d.setLevelHfTrim (-6.0f); });
    live ("setLpQ", [] (TapeMachineDSP& d) { d.setLowpassHz (6000.0f); }, [] (TapeMachineDSP& d) { d.setLpQ (2.0f); });
    live ("setProgHmfTrim", nothing, [] (TapeMachineDSP& d) { d.setProgHmfTrim (-8.0f); });
    live ("setProgHfTrim", nothing, [] (TapeMachineDSP& d) { d.setProgHfTrim (-8.0f); });
    live ("setProgLfTrim", nothing, [] (TapeMachineDSP& d) { d.setProgLfTrim (8.0f); });
    live ("setBypass", nothing, [] (TapeMachineDSP& d) { d.setBypass (true); });

    storedOnly ("setSaturation", [] (TapeMachineDSP& d) { d.setSaturation (60.0f); });
    storedOnly ("setNoiseEnabled", [] (TapeMachineDSP& d) { d.setNoiseEnabled (true); });
    storedOnly ("setOversampling", [] (TapeMachineDSP& d) { d.setOversampling (2); });
    {
        TapeMachineDSP a;
        a.setSaturation (60.0f); a.setNoiseEnabled (true); a.setOversampling (2);
        Stored copy;
        hold ("the stored-only settings", copy.saturation() == 4.0f && ! copy.noiseEnabled() && copy.oversampling() == 1,
              "a holder starts at the defaults (4 %, off, choice 1)");
        copy.copySettingsFrom (a);
        hold ("the stored-only settings", copy.saturation() == 60.0f && copy.noiseEnabled() && copy.oversampling() == 2,
              "the copy holds the values that were set (60 %, on, choice 2)");
        // And back, so that a value taken from another setting that happens to be on, or at
        // the same number, by default does not pass for the right one.
        const TapeMachineDSP untouched;
        copy.copySettingsFrom (untouched);
        hold ("the stored-only settings", copy.saturation() == 4.0f && ! copy.noiseEnabled() && copy.oversampling() == 1,
              "a copy of an untouched instance holds the defaults again (4 %, off, choice 1)");
    }

    // Everything at once. 33 settings are off their defaults; Wow & Flutter stays on and
    // Bypass off (the defaults), since either would take other settings out of the render.
    {
        TapeMachineDSP a;
        a.setTapeMachine (1); a.setTapeSpeed (0); a.setTapeType (3); a.setSignalPath (1); a.setEqStandard (1);
        a.setInputGainDb (7.5f); a.setSaturation (60.0f); a.setBias (35.0f); a.setCalibration (2); a.setAutoCal (false);
        a.setHighpassHz (60.0f); a.setLowpassHz (9000.0f); a.setNoiseAmount (20.0f); a.setNoiseEnabled (true);
        a.setWow (20.0f); a.setFlutter (12.0f); a.setOutputGainDb (-3.0f); a.setAutoComp (false); a.setOversampling (2);
        a.setHeadWidth (0); a.setCrosstalk (false); a.setWowFlutterEnabled (true); a.setTransformer (false);
        a.setReproLf (2.0f); a.setReproLmf (-1.5f); a.setReproHmf (1.0f); a.setReproHf (-2.5f); a.setReproSubBell (5.0f);
        a.setLevelHmfTrim (-4.0f); a.setLevelHfTrim (3.0f); a.setLpQ (1.4f);
        a.setProgHmfTrim (-5.0f); a.setProgHfTrim (4.0f); a.setProgLfTrim (6.0f); a.setBypass (false);
        const std::vector<float> ofA = render (a);
        hold ("every setting at once", finiteAndNotSilent (ofA), "the render is finite and not silent");
        hold ("every setting at once", same (ofA, renderCopyOf (a)), "the copy renders the same bits");
    }

    if (failures == 0) std::printf ("SettingsCopyTest: all checks passed\n");
    else               std::printf ("SettingsCopyTest: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
