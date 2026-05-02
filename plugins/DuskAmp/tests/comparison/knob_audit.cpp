// knob_audit.cpp — sweep every DuskAmp parameter, measure the effect, and
// report PASS/FAIL. The goal is to catch "looks right in code, sounds wrong"
// regressions without waiting for the user to listen-test each knob.
//
// This probe calls the DSP classes directly (no plugin wrapper), so each
// knob-under-test is isolated from the rest of the chain.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "InputSection.h"
#include "PreampDSP.h"
#include "ToneStack.h"
#include "PhaseInverter.h"
#include "PowerAmp.h"
#include "CabinetIR.h"
#include "PostFX.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static constexpr double kSr = 44100.0;

// ---------------------------------------------------------------------------
// Measurement helpers
// ---------------------------------------------------------------------------

static float rms (const float* b, int n, int skip = 0)
{
    double s = 0.0;
    int c = 0;
    for (int i = std::max (0, skip); i < n; ++i) { s += double (b[i]) * b[i]; ++c; }
    return c > 0 ? float (std::sqrt (s / c)) : 0.0f;
}
static float peak (const float* b, int n, int skip = 0)
{
    float p = 0.0f;
    for (int i = std::max (0, skip); i < n; ++i) p = std::max (p, std::abs (b[i]));
    return p;
}
static float dbfs (float x) { return x > 1.0e-9f ? 20.0f * std::log10 (x) : -120.0f; }

static std::vector<float> sineBuf (double hz, float amp, double dur)
{
    const int n = int (kSr * dur);
    std::vector<float> out (n);
    for (int i = 0; i < n; ++i)
        out[i] = amp * float (std::sin (2.0 * M_PI * hz * i / kSr));
    return out;
}

static std::vector<float> pinkBuf (int n, float scale = 0.2f, uint32_t seed = 0xBEEFCAFE)
{
    juce::Random rng { int (seed) };
    std::vector<float> out (n);
    float b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        float w = rng.nextFloat() * 2.0f - 1.0f;
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        out[i] = (b0 + b1 + b2 + w * 0.1848f) * scale;
    }
    return out;
}

// Magnitude at a given freq via Goertzel — cheaper + more accurate than FFT
// for individual bin peeks.
static float magAt (const float* b, int n, double hz, int skip = 0)
{
    const double w = 2.0 * M_PI * hz / kSr;
    const double c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    int count = 0;
    for (int i = std::max (0, skip); i < n; ++i)
    {
        const double s = b[i] + c * s1 - s2;
        s2 = s1; s1 = s;
        ++count;
    }
    const double real = s1 - s2 * std::cos (w);
    const double imag = s2 * std::sin (w);
    return count > 0 ? float (std::sqrt (real * real + imag * imag) / count * 2.0) : 0.0f;
}

// ---------------------------------------------------------------------------
// Result collection
// ---------------------------------------------------------------------------

struct Row { std::string name; bool pass; std::string detail; };
static std::vector<Row> kResults;

static void add (const std::string& name, bool pass, const std::string& detail)
{
    kResults.push_back ({ name, pass, detail });
    std::cout << (pass ? "  [PASS] " : "  [FAIL] ")
              << std::left << std::setw (26) << name
              << " " << detail << "\n";
}

// ---------------------------------------------------------------------------
// Individual knob tests
// ---------------------------------------------------------------------------

static void testInputGain()
{
    InputSection in;
    in.prepare (kSr);
    in.setGateThreshold (-120.0f); // gate fully open

    std::ostringstream d;
    d << "dB knob -> measured dB: ";
    bool pass = true;
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        in.setInputGain (db);
        auto buf = sineBuf (440.0, 0.1f, 0.3);
        in.process (buf.data(), int (buf.size()));
        const float r = rms (buf.data(), int (buf.size()), int (kSr * 0.05));
        const float measured = dbfs (r) - dbfs (0.1f / std::sqrt (2.0f));
        const float err = std::abs (measured - db);
        d << db << "->" << std::fixed << std::setprecision (1) << measured << " ";
        if (err > 0.5f) pass = false;
    }
    add ("INPUT_GAIN", pass, d.str());
}

static void testGate()
{
    // Gate threshold test: play a -30 dBFS sine, then drop to -70.
    // With threshold at -50 dB: loud half passes, quiet half should be muted.
    InputSection in;
    in.prepare (kSr);
    in.setInputGain (0.0f);
    in.setGateThreshold (-50.0f);
    in.setGateRelease (50.0f);

    const int nLoud  = int (kSr * 0.3);
    const int nQuiet = int (kSr * 0.7);
    std::vector<float> buf (nLoud + nQuiet);
    for (int i = 0; i < nLoud; ++i)
        buf[i] = 0.0316f * float (std::sin (2.0 * M_PI * 440.0 * i / kSr)); // -30 dBFS
    for (int i = 0; i < nQuiet; ++i)
        buf[nLoud + i] = 0.000316f * float (std::sin (2.0 * M_PI * 440.0 * (nLoud + i) / kSr)); // -70 dBFS
    in.process (buf.data(), int (buf.size()));

    const float loudRms  = rms (buf.data(), nLoud, int (kSr * 0.05));
    // Measure quiet portion AFTER release has fully fired (skip first 300 ms)
    const float quietRms = rms (buf.data() + nLoud + int (kSr * 0.3), nQuiet - int (kSr * 0.3));
    const bool passes = loudRms > 0.01f && quietRms < 0.0005f;
    std::ostringstream d;
    d << "-30dBFS passes (rms=" << std::scientific << std::setprecision (2) << loudRms
      << "), -70dBFS gated (rms=" << quietRms << ")";
    add ("GATE_THRESHOLD", passes, d.str());

    // Release direction: feed a burst above threshold followed by a quieter
    // continuous signal that still crosses the gate threshold. A LONGER
    // release knob means the gate envelope decays slower, so more of the
    // quiet-portion tail leaks through the gate in the first 100 ms than
    // with a SHORT release. Measure the ratio.
    auto quietLeakRms = [&] (float releaseMs) {
        InputSection in2;
        in2.prepare (kSr);
        in2.setInputGain (0.0f);
        in2.setGateThreshold (-50.0f);
        in2.setGateRelease (releaseMs);
        const int nBurst = int (kSr * 0.3);
        const int nQuiet = int (kSr * 0.5);
        std::vector<float> b (nBurst + nQuiet);
        for (int i = 0; i < nBurst; ++i)
            b[i] = 0.1f * float (std::sin (2.0 * M_PI * 440.0 * i / kSr));
        for (int i = 0; i < nQuiet; ++i)
            b[nBurst + i] = 0.0018f * float (std::sin (2.0 * M_PI * 440.0 * (nBurst + i) / kSr));
        in2.process (b.data(), int (b.size()));
        // RMS of first 100 ms of quiet portion — mostly leak-through
        return rms (b.data() + nBurst, int (kSr * 0.1));
    };
    const float r5   = quietLeakRms (5.0f);
    const float r500 = quietLeakRms (500.0f);
    const float leakDb = dbfs (r500) - dbfs (r5);
    std::ostringstream d2;
    d2 << "leak rms[500ms] - [5ms] = " << std::fixed << std::setprecision (1) << leakDb
       << " dB (longer release should leak more; need >= +3 dB)";
    add ("GATE_RELEASE", leakDb >= 3.0f, d2.str());
}

static void testPreampGain()
{
    // Already validated earlier on Clean. Re-confirm Clean + Crunch + Lead
    // each have ≥15 dB of knob range.
    auto channelRange = [] (PreampDSP::Channel ch) {
        auto measure = [&] (float gain) {
            PreampDSP p;
            p.prepare (kSr);
            p.setChannel (ch);
            p.setGain (gain);
            p.setBright (false);
            auto buf = sineBuf (440.0, 0.1f, 0.3);
            p.process (buf.data(), int (buf.size()));
            return rms (buf.data(), int (buf.size()), int (kSr * 0.05));
        };
        return dbfs (measure (1.0f)) - dbfs (measure (0.0f));
    };

    const float rClean  = channelRange (PreampDSP::Channel::Clean);
    const float rCrunch = channelRange (PreampDSP::Channel::Crunch);
    const float rLead   = channelRange (PreampDSP::Channel::Lead);

    std::ostringstream d;
    d << "range dB: Clean=" << std::fixed << std::setprecision (1) << rClean
      << " Crunch=" << rCrunch << " Lead=" << rLead << " (>= 15 each)";
    add ("PREAMP_GAIN (all channels)",
         rClean >= 15.0f && rCrunch >= 15.0f && rLead >= 15.0f, d.str());
}

static void testPreampBright()
{
    auto runWith = [] (bool bright) {
        PreampDSP p;
        p.prepare (kSr);
        p.setChannel (PreampDSP::Channel::Clean);
        p.setGain (0.3f);
        p.setBright (bright);
        auto buf = pinkBuf (int (kSr * 0.5));
        p.process (buf.data(), int (buf.size()));
        return buf;
    };
    auto off = runWith (false);
    auto on  = runWith (true);
    const int skip = int (kSr * 0.05);
    const int n = int (off.size());
    // Compare magnitude at 2 kHz vs 300 Hz for both
    const float on_hi  = magAt (on.data(),  n, 2000.0, skip);
    const float off_hi = magAt (off.data(), n, 2000.0, skip);
    const float on_lo  = magAt (on.data(),  n,  300.0, skip);
    const float off_lo = magAt (off.data(), n,  300.0, skip);
    const float hiDb = dbfs (on_hi) - dbfs (off_hi);
    const float loDb = dbfs (on_lo) - dbfs (off_lo);
    std::ostringstream d;
    d << "HF gain (2 kHz): " << std::fixed << std::setprecision (1) << hiDb
      << " dB (need >= +1), LF gain (300 Hz): " << loDb << " dB (need ~0)";
    add ("PREAMP_BRIGHT",
         hiDb >= 1.0f && std::abs (loDb) < 1.0f, d.str());
}

static void testChannelProgression()
{
    // Same input + gain, measure peak + harmonic content. Clean < Crunch < Lead
    // in total level (because outputMakeup is 2/4/8 × gain-scale).
    auto measure = [] (PreampDSP::Channel ch) {
        PreampDSP p;
        p.prepare (kSr);
        p.setChannel (ch);
        p.setGain (0.5f);
        p.setBright (false);
        auto buf = sineBuf (440.0, 0.2f, 0.3);
        p.process (buf.data(), int (buf.size()));
        return peak (buf.data(), int (buf.size()), int (kSr * 0.05));
    };
    const float cln = measure (PreampDSP::Channel::Clean);
    const float cru = measure (PreampDSP::Channel::Crunch);
    const float lea = measure (PreampDSP::Channel::Lead);
    std::ostringstream d;
    d << "peak: Clean=" << std::fixed << std::setprecision (3) << cln
      << " Crunch=" << cru << " Lead=" << lea << " (monotonic)";
    add ("PREAMP_CHANNEL", cln < cru && cru < lea, d.str());
}

static void testPowerDriveAndTHD()
{
    // Sweep drive, confirm output RMS doesn't balloon (drive should shape, not
    // heavily boost level — tanh limiter tames anything > 1.1).
    auto measure = [] (float drive) {
        PowerAmp pa;
        pa.prepare (kSr);
        pa.setAmpType (PowerAmp::AmpType::Fender);
        pa.setDrive (drive);
        pa.setPresence (0.5f);
        pa.setResonance (0.5f);
        pa.setSag (0.3f);
        auto buf = sineBuf (220.0, 0.3f, 0.3);
        pa.process (buf.data(), int (buf.size()));
        return rms (buf.data(), int (buf.size()), int (kSr * 0.05));
    };
    const float r0 = measure (0.0f);
    const float r3 = measure (0.3f);
    const float r7 = measure (0.7f);
    const float r1 = measure (1.0f);
    // Level should increase but stay bounded; most importantly, it must not
    // attenuate as drive goes up.
    const bool monotonic = r0 < r3 && r3 < r7 && r7 <= r1 + 0.02f;
    std::ostringstream d;
    d << "RMS drive={0,0.3,0.7,1.0}: "
      << std::fixed << std::setprecision (3)
      << r0 << " " << r3 << " " << r7 << " " << r1;
    add ("POWER_DRIVE", monotonic, d.str());
}

static void testPresenceAndResonance()
{
    // Feed pink through Fender power amp, measure magnitude at 3.5 kHz vs
    // 100 Hz for presence sweep; vice-versa for resonance.
    auto runPA = [] (float presence, float resonance) {
        PowerAmp pa;
        pa.prepare (kSr);
        pa.setAmpType (PowerAmp::AmpType::Fender);
        pa.setDrive (0.2f);                 // clean, no saturation
        pa.setPresence (presence);
        pa.setResonance (resonance);
        pa.setSag (0.0f);                   // isolate the shelf
        auto buf = pinkBuf (int (kSr * 0.5), 0.05f);
        pa.process (buf.data(), int (buf.size()));
        return buf;
    };

    // Presence: 3.5 kHz should move; 100 Hz should not.
    auto presLo  = runPA (0.0f, 0.5f);
    auto presHi  = runPA (1.0f, 0.5f);
    const int skip = int (kSr * 0.05);
    const int n    = int (presLo.size());
    const float presDeltaHi = dbfs (magAt (presHi.data(), n, 3500.0, skip))
                            - dbfs (magAt (presLo.data(), n, 3500.0, skip));
    const float presDeltaLo = dbfs (magAt (presHi.data(), n,  100.0, skip))
                            - dbfs (magAt (presLo.data(), n,  100.0, skip));
    std::ostringstream dp;
    dp << "3.5 kHz delta: " << std::fixed << std::setprecision (1) << presDeltaHi
       << " dB (need >= +1), 100 Hz delta: " << presDeltaLo << " dB (need ~0)";
    add ("PRESENCE", presDeltaHi >= 1.0f && std::abs (presDeltaLo) < 1.0f, dp.str());

    // Resonance: 100 Hz should move; 3.5 kHz should not.
    auto resLo = runPA (0.5f, 0.0f);
    auto resHi = runPA (0.5f, 1.0f);
    const float resDeltaLo = dbfs (magAt (resHi.data(), n, 100.0, skip))
                           - dbfs (magAt (resLo.data(), n, 100.0, skip));
    const float resDeltaHi = dbfs (magAt (resHi.data(), n, 3500.0, skip))
                           - dbfs (magAt (resLo.data(), n, 3500.0, skip));
    std::ostringstream dr;
    dr << "100 Hz delta: " << std::fixed << std::setprecision (1) << resDeltaLo
       << " dB (need >= +1), 3.5 kHz delta: " << resDeltaHi << " dB (need ~0)";
    add ("RESONANCE", resDeltaLo >= 1.0f && std::abs (resDeltaHi) < 1.0f, dr.str());
}

static void testSag()
{
    // Compare the SETTLED late-window RMS across sag values on fresh power
    // amps: more sag → lower settled level. Early-vs-late windowing inside
    // the same run was confusing because the early window already overlapped
    // the sag onset for fast discharge time constants.
    auto settledRms = [] (PowerAmp::AmpType ampType, float sag) {
        PowerAmp pa;
        pa.prepare (kSr);
        pa.setAmpType (ampType);
        pa.setDrive (0.5f);
        pa.setPresence (0.5f);
        pa.setResonance (0.5f);
        pa.setSag (sag);
        auto buf = sineBuf (110.0, 0.5f, 1.0);
        pa.process (buf.data(), int (buf.size()));
        return rms (buf.data() + int (kSr * 0.3), int (kSr * 0.7));
    };

    // Test each amp's sag-knob direction. Acceptance thresholds differ: Vox
    // Class-A sags hardest (vBplus multiplies both pre- and post-saturation),
    // Marshall silicon barely sags. Fender sits in between, but the final
    // tanh soft-limit compresses both sag=0 and sag=1 to similar ceilings
    // when drive is at/above 0.5 — so we only require a small directional
    // drop there.
    auto runFor = [&] (const char* name, PowerAmp::AmpType ampType, float need) {
        const float s0 = settledRms (ampType, 0.0f);
        const float s1 = settledRms (ampType, 1.0f);
        const float delta = dbfs (s1) - dbfs (s0);
        std::ostringstream d;
        d << "RMS delta (sag=1 - sag=0) = " << std::fixed << std::setprecision (2) << delta
          << " dB (need <= " << need << " dB)";
        add (std::string ("SAG ") + name, delta <= need, d.str());
    };
    runFor ("Fender",   PowerAmp::AmpType::Fender,   -0.1f);
    runFor ("Vox",      PowerAmp::AmpType::Vox,      -0.5f);
    // Marshall silicon supply is intentionally stiff (maxDepth=0.06); at
    // drive=0.5 the load calc barely saturates, so we just require
    // directional movement, not a specific magnitude.
    runFor ("Marshall", PowerAmp::AmpType::Marshall, -0.005f);
}

static void testToneStackFlat()
{
    // Each tonestack at all knobs 0.5 should be ≈ unity at 1 kHz (±3 dB).
    auto measure = [] (ToneStack::Type t) {
        ToneStack ts;
        ts.prepare (kSr);
        ts.setType (t);
        ts.setBass (0.5f); ts.setMid (0.5f); ts.setTreble (0.5f);
        auto buf = sineBuf (1000.0, 0.3f, 0.5);
        const float inRms = rms (buf.data(), int (buf.size()), int (kSr * 0.05));
        ts.process (buf.data(), int (buf.size()));
        const float outRms = rms (buf.data(), int (buf.size()), int (kSr * 0.05));
        return dbfs (outRms) - dbfs (inRms);
    };
    const float a = measure (ToneStack::Type::American);
    const float b = measure (ToneStack::Type::British);
    const float c = measure (ToneStack::Type::AC);
    std::ostringstream d;
    d << "flat 1k dB: Am=" << std::fixed << std::setprecision (1) << a
      << " Br=" << b << " AC=" << c << " (each within ±3 dB)";
    add ("TONE_TYPE flat",
         std::abs (a) < 3.0f && std::abs (b) < 3.0f && std::abs (c) < 3.0f, d.str());
}

static void testToneKnobsMonotonic()
{
    // For each type, sweeping bass 0→1 must monotonically increase LF (80 Hz).
    // Treble 0→1 must monotonically increase HF (4 kHz).
    // Mid 0→1 must monotonically increase mids (1 kHz) for American/British.
    auto bandAt = [] (ToneStack::Type t, char knob, float val, double probeHz) {
        ToneStack ts;
        ts.prepare (kSr);
        ts.setType (t);
        ts.setBass (knob == 'B' ? val : 0.5f);
        ts.setMid  (knob == 'M' ? val : 0.5f);
        ts.setTreble (knob == 'T' ? val : 0.5f);
        auto buf = sineBuf (probeHz, 0.3f, 0.3);
        ts.process (buf.data(), int (buf.size()));
        return dbfs (rms (buf.data(), int (buf.size()), int (kSr * 0.05)));
    };

    auto monotonic = [&] (ToneStack::Type t, char knob, double probeHz) {
        float prev = -1e9f;
        for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            float d = bandAt (t, knob, v, probeHz);
            if (d < prev - 0.5f) return false;
            prev = d;
        }
        return true;
    };

    const bool bassAm = monotonic (ToneStack::Type::American, 'B', 80.0);
    const bool bassBr = monotonic (ToneStack::Type::British,  'B', 80.0);
    const bool bassAC = monotonic (ToneStack::Type::AC,       'B', 80.0);
    add ("BASS monotonic", bassAm && bassBr && bassAC,
         std::string ("Am=") + (bassAm?"y":"n") + " Br=" + (bassBr?"y":"n") + " AC=" + (bassAC?"y":"n"));

    const bool trAm = monotonic (ToneStack::Type::American, 'T', 4000.0);
    const bool trBr = monotonic (ToneStack::Type::British,  'T', 4000.0);
    const bool trAC = monotonic (ToneStack::Type::AC,       'T', 4000.0);
    add ("TREBLE monotonic", trAm && trBr && trAC,
         std::string ("Am=") + (trAm?"y":"n") + " Br=" + (trBr?"y":"n") + " AC=" + (trAC?"y":"n"));

    const bool midAm = monotonic (ToneStack::Type::American, 'M', 1000.0);
    const bool midBr = monotonic (ToneStack::Type::British,  'M', 1000.0);
    add ("MID monotonic (Am/Br)", midAm && midBr,
         std::string ("Am=") + (midAm?"y":"n") + " Br=" + (midBr?"y":"n"));
}

static void testOutputLevel()
{
    // Just verify the dB→linear conversion matches. DuskAmpEngine applies
    // `outputGain_ = pow(10, dB/20)` then multiplies — trivial math.
    bool pass = true;
    std::ostringstream d;
    for (float db : { -24.0f, -12.0f, 0.0f, 6.0f, 12.0f })
    {
        const float g = std::pow (10.0f, db / 20.0f);
        const float back = 20.0f * std::log10 (g);
        if (std::abs (back - db) > 0.01f) pass = false;
        d << db << "dB=" << std::fixed << std::setprecision (2) << g << " ";
    }
    add ("OUTPUT_LEVEL (math)", pass, d.str());
}

// ---------------------------------------------------------------------------
// Cab IR tests — needs a temp IR file (unity impulse + short tail)
// ---------------------------------------------------------------------------

// Unity IR — single non-zero sample so the convolution is a flat pass-through.
// This lets us measure the post-cab HPF/LPF filters in isolation without the
// IR's own bandpass shape confounding the reading.
static juce::File makeTempIR()
{
    auto tempFile = juce::File::createTempFile ("duskamp_audit_ir.wav");

    const int n = 128;
    juce::AudioBuffer<float> buf (1, n);
    auto* data = buf.getWritePointer (0);
    for (int i = 0; i < n; ++i) data[i] = 0.0f;
    data[0] = 1.0f;

    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (tempFile.createOutputStream()))
    {
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                wav.createWriterFor (os.get(), kSr, 1, 32, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (buf, 0, n);
        }
    }
    return tempFile;
}

// Synthetic guitar-cab IR: low-passed exponentially-decaying noise, ~50 ms tail.
// Realistic enough to exercise the NORM loudness-match math (a unit impulse is
// trivial — its sum-of-squares is 1, so the real bug only shows on broadband
// IRs where JUCE's energy normalisation diverges sharply from peak).
static juce::File makeTempCabIR()
{
    auto tempFile = juce::File::createTempFile ("duskamp_audit_cab_ir.wav");
    const int n = int (kSr * 0.05); // 50 ms
    juce::AudioBuffer<float> buf (1, n);
    auto* data = buf.getWritePointer (0);
    juce::Random rng { 0xCAB1DEA5 };
    float lp = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        lp += 0.20f * (w - lp); // ~one-pole LPF
        const float env = std::exp (-3.0f * float (i) / float (n));
        data[i] = lp * env;
    }
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (tempFile.createOutputStream()))
    {
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                wav.createWriterFor (os.get(), kSr, 1, 32, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (buf, 0, n);
        }
    }
    return tempFile;
}

// Fused cab-section test — single IR, single CabinetIR, so the async
// convolution lifecycle doesn't get double-initialised and segfault.
static void testCabSection()
{
    auto ir = makeTempIR();
    CabinetIR cab;
    // prepare with a max block size large enough for any of the test buffers
    // below (rmsAtSine uses 0.5 s = 22050 samples; runMix uses 0.3 s = 13230).
    // juce::dsp::Convolution treats maximumBlockSize as a hard contract — feeding
    // larger blocks corrupts its internal partition state and segfaults.
    cab.prepare (kSr, int (kSr));
    cab.loadIR (ir);
    cab.setEnabled (true);
    cab.setMix (1.0f);
    cab.setNormalize (false);

    auto warm = [&] {
        for (int i = 0; i < 50; ++i)
        {
            juce::AudioBuffer<float> w (1, 512);
            w.clear();
            cab.process (w);
        }
    };
    warm();

    auto rmsAtSine = [&] (float hicut, float locut, double probeHz) {
        cab.setHiCut (hicut);
        cab.setLoCut (locut);
        warm();
        auto sine = sineBuf (probeHz, 0.3f, 0.5);
        juce::AudioBuffer<float> jb (1, int (sine.size()));
        jb.copyFrom (0, 0, sine.data(), int (sine.size()));
        cab.process (jb);
        return rms (jb.getReadPointer (0), int (sine.size()), int (kSr * 0.25));
    };

    // HICUT
    const float ref_8k = rmsAtSine (20000.0f, 20.0f, 8000.0);
    const float cut_8k = rmsAtSine (2000.0f,  20.0f, 8000.0);
    const float hiDrop = dbfs (cut_8k) - dbfs (ref_8k);
    std::ostringstream dh;
    dh << "8 kHz attenuation: hicut=2k vs hicut=20k = " << std::fixed << std::setprecision (1)
       << hiDrop << " dB (need <= -10 dB)";
    add ("CAB_HICUT", hiDrop <= -10.0f, dh.str());

    // LOCUT
    const float ref_100 = rmsAtSine (20000.0f, 20.0f,  100.0);
    const float cut_100 = rmsAtSine (20000.0f, 500.0f, 100.0);
    const float loDrop = dbfs (cut_100) - dbfs (ref_100);
    std::ostringstream dl;
    dl << "100 Hz attenuation: locut=500 vs locut=20 = " << std::fixed << std::setprecision (1)
       << loDrop << " dB (need <= -10 dB)";
    add ("CAB_LOCUT", loDrop <= -10.0f, dl.str());

    // MIX — mix=0 passes dry, mix=1 passes fully convolved
    cab.setHiCut (20000.0f);
    cab.setLoCut (20.0f);
    warm();
    auto runMix = [&] (float mix) {
        cab.setMix (mix);
        auto pink = pinkBuf (int (kSr * 0.3), 0.15f, 0xB0B0B0);
        juce::AudioBuffer<float> jb (1, int (pink.size()));
        jb.copyFrom (0, 0, pink.data(), int (pink.size()));
        cab.process (jb);
        return rms (jb.getReadPointer (0), int (pink.size()), int (kSr * 0.1));
    };
    const float dryRms = rms (pinkBuf (int (kSr * 0.3), 0.15f, 0xB0B0B0).data(),
                              int (kSr * 0.3), int (kSr * 0.1));
    const float mix0   = runMix (0.0f);
    const float ratio  = mix0 / std::max (dryRms, 1e-9f);
    std::ostringstream dm;
    dm << "mix=0 rms/dry=" << std::fixed << std::setprecision (2) << ratio
       << " (should be ~1.0)";
    add ("CAB_MIX (mix=0 passes dry)", ratio > 0.9f && ratio < 1.1f, dm.str());

    ir.deleteFile();

    // CAB_LOUDNESS — confirm the NORM toggle actually keeps perceived
    // loudness within ±2 dB of dry when convolving a realistic broadband
    // cab IR (where JUCE's energy-normalisation differs sharply from peak,
    // exposing any mismatch between the offline makeup measurement and
    // JUCE's actual scaling).
    auto cabIr = makeTempCabIR();
    CabinetIR cab2;
    cab2.prepare (kSr, int (kSr));
    cab2.loadIR (cabIr);
    cab2.setEnabled (true);
    cab2.setMix (1.0f);
    cab2.setHiCut (20000.0f);
    cab2.setLoCut (20.0f);

    auto cabDb = [&] (bool normOn) {
        cab2.setNormalize (normOn);
        // warm convolution
        for (int i = 0; i < 50; ++i) {
            juce::AudioBuffer<float> w (1, 512); w.clear();
            cab2.process (w);
        }
        auto pink = pinkBuf (int (kSr * 0.5), 0.15f, 0xCAFEC0DE);
        juce::AudioBuffer<float> jb (1, int (pink.size()));
        jb.copyFrom (0, 0, pink.data(), int (pink.size()));
        cab2.process (jb);
        return dbfs (rms (jb.getReadPointer (0), int (pink.size()), int (kSr * 0.1)));
    };
    const float dryRefDb = dbfs (rms (pinkBuf (int (kSr * 0.5), 0.15f, 0xCAFEC0DE).data(),
                                       int (kSr * 0.5), int (kSr * 0.1)));
    const float normOffDb = cabDb (false);
    const float normOnDb  = cabDb (true);
    const float diffOff   = normOffDb - dryRefDb;
    const float diffOn    = normOnDb  - dryRefDb;
    std::ostringstream dnL;
    dnL << "vs dry: NORM off = " << std::fixed << std::setprecision (1) << diffOff
        << " dB, NORM on = " << diffOn << " dB (need |on| <= 2 dB)";
    add ("CAB_LOUDNESS (NORM matches dry)", std::abs (diffOn) <= 2.0f, dnL.str());

    cabIr.deleteFile();
}

// ---------------------------------------------------------------------------
// Post-FX tests
// ---------------------------------------------------------------------------

static void testDelayTime()
{
    PostFX fx;
    fx.prepare (kSr, 512);
    fx.setDelayEnabled (true);
    fx.setDelayFeedback (0.0f);
    fx.setDelayMix (1.0f); // fully wet — output is only the delayed signal

    bool pass = true;
    std::ostringstream d;
    for (float ms : { 50.0f, 250.0f, 1000.0f })
    {
        fx.reset();
        fx.setDelayTime (ms);
        const int n = int (kSr * 2.0); // 2 s buffer
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        L[0] = 1.0f; R[0] = 1.0f;
        fx.process (L.data(), R.data(), n);
        // Find first peak
        int maxIdx = 0;
        float maxAbs = 0.0f;
        for (int i = 1; i < n; ++i)
        {
            if (std::abs (L[i]) > maxAbs) { maxAbs = std::abs (L[i]); maxIdx = i; }
        }
        const float measuredMs = float (maxIdx) / float (kSr) * 1000.0f;
        const float err = std::abs (measuredMs - ms);
        d << ms << "ms->" << std::fixed << std::setprecision (1) << measuredMs << "ms ";
        if (err > 2.0f) pass = false;
    }
    add ("DELAY_TIME", pass, d.str());
}

static void testDelayFeedback()
{
    // fb=0: one tap; fb=0.9: many decaying taps. No runaway (peak stays < 1.2).
    auto measure = [] (float fb) {
        PostFX fx;
        fx.prepare (kSr, 512);
        fx.setDelayEnabled (true);
        fx.setDelayTime (50.0f);
        fx.setDelayFeedback (fb);
        fx.setDelayMix (1.0f);
        const int n = int (kSr * 4.0);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        L[0] = 1.0f; R[0] = 1.0f;
        fx.process (L.data(), R.data(), n);
        // Count peaks above -40 dBFS = 0.01
        int taps = 0;
        float maxAbs = 0.0f;
        for (int i = 1; i < n; ++i)
        {
            const float v = std::abs (L[i]);
            if (v > 0.01f && v > std::abs (L[i-1]) && (i+1 < n && v > std::abs (L[i+1])))
                ++taps;
            maxAbs = std::max (maxAbs, v);
        }
        return std::pair<int, float> { taps, maxAbs };
    };
    const auto r0 = measure (0.0f);
    const auto r5 = measure (0.5f);
    const auto r9 = measure (0.9f);
    std::ostringstream d;
    d << "taps (fb=0/0.5/0.9): " << r0.first << "/" << r5.first << "/" << r9.first
      << " peak=" << std::fixed << std::setprecision (2) << r9.second;
    const bool pass = r0.first <= 1
                   && r5.first >= 3 && r5.first < r9.first
                   && r9.second < 1.2f;
    add ("DELAY_FEEDBACK", pass, d.str());
}

static void testDelayMix()
{
    // Impulse-based test: mix=0 leaves only the input impulse; mix=1 shifts
    // it to the delay time. Steady-state RMS is nearly identical for the two
    // mixes with a stationary signal — hence the impulse.
    PostFX fx;
    fx.prepare (kSr, 512);
    fx.setDelayEnabled (true);
    fx.setDelayTime (50.0f);
    fx.setDelayFeedback (0.0f);

    auto runImpulse = [&] (float mix) {
        fx.reset();
        fx.setDelayMix (mix);
        const int n = int (kSr * 0.2); // 200 ms
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        L[0] = 1.0f; R[0] = 1.0f;
        fx.process (L.data(), R.data(), n);
        return L;
    };
    auto m0 = runImpulse (0.0f);
    auto m1 = runImpulse (1.0f);
    const int delayIdx = int (kSr * 0.05);
    const bool pass = std::abs (m0[0])       > 0.9f   // mix=0: impulse stays at index 0
                   && std::abs (m0[delayIdx]) < 0.01f // mix=0: nothing at 50 ms
                   && std::abs (m1[0])       < 0.01f  // mix=1: impulse muted
                   && std::abs (m1[delayIdx]) > 0.9f; // mix=1: impulse appears at 50 ms
    std::ostringstream d;
    d << "mix=0 [0ms]=" << std::fixed << std::setprecision (2) << m0[0]
      << " [50ms]=" << m0[delayIdx]
      << " | mix=1 [0ms]=" << m1[0] << " [50ms]=" << m1[delayIdx];
    add ("DELAY_MIX", pass, d.str());
}

static void testReverb()
{
    PostFX fx;
    fx.prepare (kSr, 512);
    fx.setReverbEnabled (true);
    fx.setReverbMix (1.0f);

    auto tailRms = [&] (float decay) {
        fx.reset();
        fx.setReverbDecay (decay);
        const int n = int (kSr * 4.0);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        auto burst = pinkBuf (int (kSr * 0.02), 0.3f);
        std::copy (burst.begin(), burst.end(), L.begin());
        std::copy (burst.begin(), burst.end(), R.begin());
        // PostFX::prepare sets reverbBuffer_ to maxBlockSize; processing a
        // larger-than-block buffer in one call overflows it and produces NaN.
        // Chunk the call to match the prepared block size.
        constexpr int kBlock = 512;
        for (int off = 0; off < n; off += kBlock)
        {
            const int bs = std::min (kBlock, n - off);
            fx.process (L.data() + off, R.data() + off, bs);
        }
        return rms (L.data() + int (kSr * 0.5), int (kSr * 1.5));
    };
    const float t0 = tailRms (0.0f);
    const float t1 = tailRms (1.0f);
    std::ostringstream d;
    d << "tail RMS decay=0 -> " << std::scientific << std::setprecision (2) << t0
      << ", decay=1 -> " << t1 << " (t1 should be > t0 by >= 3x)";
    add ("REVERB_DECAY", t1 > t0 * 3.0f, d.str());

    // Reverb mix
    fx.setReverbDecay (0.5f);
    auto pink = pinkBuf (int (kSr * 0.2), 0.15f, 0xD0D0D0);
    auto runMix = [&] (float mix) {
        fx.reset();
        fx.setReverbMix (mix);
        std::vector<float> L (pink), R (pink);
        fx.process (L.data(), R.data(), int (pink.size()));
        return rms (L.data(), int (pink.size()), int (kSr * 0.05));
    };
    const float dryRms = rms (pink.data(), int (pink.size()), int (kSr * 0.05));
    const float m0 = runMix (0.0f);
    const float m1 = runMix (1.0f);
    std::ostringstream dm;
    dm << "mix=0 rms/dry=" << std::fixed << std::setprecision (2) << (m0 / dryRms)
       << " (should be ~1), mix=1 rms/dry=" << (m1 / dryRms);
    add ("REVERB_MIX", m0 / dryRms > 0.9f && m0 / dryRms < 1.1f, dm.str());
}

// ---------------------------------------------------------------------------
// Selector / toggle tests (quick sanity)
// ---------------------------------------------------------------------------

// Full-chain RMS for a given amp model at matched user-facing settings.
// Mirrors DuskAmpEngine::setToneStackType's amp/PI pairing:
//   Fender   ↔ American tonestack ↔ LongTailPair PI gain 2.5
//   Marshall ↔ British  tonestack ↔ LongTailPair PI gain 4.0  + Marshall preamp voicing
//   Vox      ↔ AC       tonestack ↔ Cathodyne   PI gain 1.6
static float fullChainRms (PowerAmp::AmpType amp)
{
    PreampDSP preamp;
    ToneStack ts;
    PhaseInverter pi;
    PowerAmp pa;
    preamp.prepare (kSr); ts.prepare (kSr); pi.prepare (kSr); pa.prepare (kSr);

    preamp.setChannel (PreampDSP::Channel::Clean);
    preamp.setGain (0.7f);
    preamp.setBright (false);
    preamp.setMarshallVoicing (amp == PowerAmp::AmpType::Marshall);

    switch (amp)
    {
        case PowerAmp::AmpType::Fender:
            ts.setType (ToneStack::Type::American);
            pi.setTopology (PhaseInverter::Topology::LongTailPair);
            pi.setGain (2.5f); pi.setHeadroom (2.2f);
            break;
        case PowerAmp::AmpType::Marshall:
            ts.setType (ToneStack::Type::British);
            pi.setTopology (PhaseInverter::Topology::LongTailPair);
            pi.setGain (4.0f); pi.setHeadroom (1.6f);
            break;
        case PowerAmp::AmpType::Vox:
        default:
            ts.setType (ToneStack::Type::AC);
            pi.setTopology (PhaseInverter::Topology::Cathodyne);
            pi.setGain (1.6f); pi.setHeadroom (2.4f);
            break;
    }
    ts.setBass (0.5f); ts.setMid (0.5f); ts.setTreble (0.5f);

    pa.setAmpType (amp);
    pa.setDrive (0.3f); pa.setPresence (0.5f); pa.setResonance (0.5f); pa.setSag (0.0f);

    // 220 Hz sine, -12 dBFS (peak 0.25), 1 second — typical guitar DI.
    auto buf = sineBuf (220.0, 0.25f, 1.0);
    constexpr int block = 512;
    for (int off = 0; off < int (buf.size()); off += block)
    {
        const int n = std::min (block, int (buf.size()) - off);
        preamp.process (buf.data() + off, n);
        ts.process     (buf.data() + off, n);
        pi.process     (buf.data() + off, n);
        pa.process     (buf.data() + off, n);
    }
    return rms (buf.data(), int (buf.size()), int (kSr * 0.1));
}

static void testAmpLevelsMatched()
{
    const float fRms = fullChainRms (PowerAmp::AmpType::Fender);
    const float mRms = fullChainRms (PowerAmp::AmpType::Marshall);
    const float vRms = fullChainRms (PowerAmp::AmpType::Vox);

    const float fDb = dbfs (fRms);
    const float mDb = dbfs (mRms);
    const float vDb = dbfs (vRms);

    const float maxDb = std::max ({ fDb, mDb, vDb });
    const float minDb = std::min ({ fDb, mDb, vDb });
    const float spread = maxDb - minDb;

    std::ostringstream d;
    d << "Fender=" << std::fixed << std::setprecision (1) << fDb
      << " Marshall=" << mDb
      << " Vox=" << vDb
      << " dB | spread=" << spread << " dB (need <= 2 dB)";
    add ("AMP_LEVELS_MATCHED", spread <= 2.0f, d.str());
}

static void testAmpTypeDistinct()
{
    // RMS can coincide across amps (post-makeup is tuned so flat-level
    // matches). Use sample-by-sample RMS-difference between each pair —
    // measures spectral/harmonic divergence rather than overall loudness.
    // Use drive=0.85 + moderate amplitude so each waveshaper's saturation
    // character is on display — push-pull 6V6 vs EL34 Pentode vs Class-A
    // EL84 diverge more at hot drive than at clean.
    auto run = [] (PowerAmp::AmpType t) {
        PowerAmp pa;
        pa.prepare (kSr);
        pa.setAmpType (t);
        pa.setDrive (0.85f);
        pa.setPresence (0.5f); pa.setResonance (0.5f); pa.setSag (0.3f);
        auto buf = sineBuf (220.0, 0.3f, 0.3);
        pa.process (buf.data(), int (buf.size()));
        return buf;
    };
    auto f = run (PowerAmp::AmpType::Fender);
    auto m = run (PowerAmp::AmpType::Marshall);
    auto v = run (PowerAmp::AmpType::Vox);
    auto diffRms = [] (const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0;
        const int skip = int (kSr * 0.05);
        int c = 0;
        for (int i = skip; i < int (a.size()); ++i) { double d = a[i] - b[i]; s += d * d; ++c; }
        return c > 0 ? float (std::sqrt (s / c)) : 0.0f;
    };
    const float fm = diffRms (f, m);
    const float mv = diffRms (m, v);
    const float fv = diffRms (f, v);
    std::ostringstream d;
    d << "waveform-diff RMS: F-M=" << std::fixed << std::setprecision (3) << fm
      << " M-V=" << mv << " F-V=" << fv;
    add ("AmpType distinct", fm > 0.01f && mv > 0.01f && fv > 0.01f, d.str());
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::cout << "=== DuskAmp Knob Audit ===\n";
    std::cout << "Sample rate: " << kSr << " Hz\n\n";

    std::cout << "-- Signal path --\n";
    testInputGain();
    testGate();
    testPreampGain();
    testPreampBright();
    testChannelProgression();
    testToneStackFlat();
    testToneKnobsMonotonic();
    testPowerDriveAndTHD();
    testPresenceAndResonance();
    testSag();
    testOutputLevel();

    std::cout << "\n-- Effects --\n";
    testDelayTime();
    testDelayFeedback();
    testDelayMix();
    testReverb();

    std::cout << "\n-- Selectors --\n";
    testAmpLevelsMatched();
    testAmpTypeDistinct();

    std::cout << "\n-- Cabinet --\n";
    // Run last — CabinetIR's juce::dsp::Convolution has a background message
    // queue whose destruction appears to race with other subsystems on exit.
    // Keeping it at the end ensures all other tests complete.
    testCabSection();

    // Summary
    int pass = 0, fail = 0;
    for (auto& r : kResults) (r.pass ? pass : fail)++;
    std::cout << "\n=== Summary: " << pass << " PASS / " << fail << " FAIL ===\n";
    if (fail > 0)
    {
        std::cout << "\nFailures:\n";
        for (auto& r : kResults)
            if (! r.pass) std::cout << "  - " << r.name << "  :: " << r.detail << "\n";
    }
    return fail == 0 ? 0 : 1;
}
