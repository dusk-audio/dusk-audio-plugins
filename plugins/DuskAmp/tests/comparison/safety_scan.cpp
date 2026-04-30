// safety_scan.cpp — runtime safety + edge-case checks for DuskAmp DSP
// components. Tests properties that bite Alpha testers in DAWs but aren't
// captured by THD or knob-direction audits:
//   - NaN/Inf safety with extreme inputs and knob settings
//   - Sample-rate parametric (44.1, 48, 88.2, 96, 192 kHz)
//   - Reset state purge (no leftover voltage/echo after reset())
//   - No-clicks under audio-rate parameter modulation
//   - Denormal handling (no CPU explosion or NaN on long fade-outs)

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "PreampDSP.h"
#include "ToneStack.h"
#include "PowerAmp.h"
#include "PostFX.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Result collection (mirrors knob_audit.cpp pattern)
// ---------------------------------------------------------------------------

struct Row { std::string name; bool pass; std::string detail; };
static std::vector<Row> kResults;

static void add (const std::string& name, bool pass, const std::string& detail)
{
    kResults.push_back ({ name, pass, detail });
    std::cout << (pass ? "  [PASS] " : "  [FAIL] ")
              << std::left << std::setw (40) << name
              << " " << detail << "\n";
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool allFinite (const float* b, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (b[i])) return false;
    return true;
}

static float maxAbs (const float* b, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; ++i) m = std::max (m, std::abs (b[i]));
    return m;
}

static std::vector<float> sineBuf (double sr, double hz, float amp, double dur)
{
    const int n = int (sr * dur);
    std::vector<float> out (n);
    for (int i = 0; i < n; ++i)
        out[i] = amp * float (std::sin (2.0 * M_PI * hz * i / sr));
    return out;
}

static std::vector<float> noiseBuf (int n, float amp, uint32_t seed)
{
    juce::Random rng { int (seed) };
    std::vector<float> out (n);
    for (int i = 0; i < n; ++i)
        out[i] = (rng.nextFloat() * 2.0f - 1.0f) * amp;
    return out;
}

static std::vector<float> impulseBuf (int n, float amp = 1.0f)
{
    std::vector<float> out (n, 0.0f);
    if (n > 0) out[0] = amp;
    return out;
}

// ---------------------------------------------------------------------------
// C1 — NaN/Inf scan
// ---------------------------------------------------------------------------

static bool runChainExtreme (double sr, PreampDSP::Channel ch,
                             ToneStack::Type tt, PowerAmp::AmpType pa,
                             float gain, float bass, float mid, float treble,
                             float drive, float presence, float resonance,
                             float sag, const std::vector<float>& input)
{
    PreampDSP p;  p.prepare (sr); p.setChannel (ch); p.setGain (gain);
    p.setBright (false);
    ToneStack ts; ts.prepare (sr); ts.setType (tt);
    ts.setBass (bass); ts.setMid (mid); ts.setTreble (treble);
    PowerAmp pp; pp.prepare (sr); pp.setAmpType (pa); pp.setDrive (drive);
    pp.setPresence (presence); pp.setResonance (resonance); pp.setSag (sag);

    std::vector<float> buf (input);
    constexpr int block = 512;
    const int n = int (buf.size());
    for (int off = 0; off < n; off += block)
    {
        const int bs = std::min (block, n - off);
        p.process (buf.data() + off, bs);
        ts.process (buf.data() + off, bs);
        pp.process (buf.data() + off, bs);
    }
    return allFinite (buf.data(), n);
}

static void testNaNScan()
{
    // Walk a Cartesian product of extreme-but-legal inputs and knob settings,
    // assert every output sample is finite. The post-stage tanh limit should
    // bound peaks, so this catches state-machine bugs that produce inf or NaN
    // even when amplitudes look reasonable.
    constexpr double sr = 44100.0;
    const std::vector<float> testSig = []{
        // 0.5 s of pink-leaning noise + a 1-sample full-scale impulse spike
        auto v = noiseBuf (int (sr * 0.5), 0.7f, 0xDEADBEEF);
        if (! v.empty()) v[100] = 1.0f;
        if (v.size() > 200) v[200] = -1.0f;
        return v;
    }();

    int total = 0, pass = 0;
    for (auto ch : { PreampDSP::Channel::Clean, PreampDSP::Channel::Crunch, PreampDSP::Channel::Lead })
    for (auto tt : { ToneStack::Type::American, ToneStack::Type::British, ToneStack::Type::AC })
    for (auto pa : { PowerAmp::AmpType::Fender, PowerAmp::AmpType::Marshall, PowerAmp::AmpType::Vox })
    for (float knob : { 0.0f, 1.0f })
    {
        ++total;
        if (runChainExtreme (sr, ch, tt, pa, knob, knob, knob, knob,
                             knob, knob, knob, knob, testSig))
            ++pass;
    }
    std::ostringstream d;
    d << pass << "/" << total << " (channel × tonestack × amp × knob-extreme combinations)";
    add ("NaN/Inf scan — full chain", pass == total, d.str());

    // Silence-in test: process pure zeros, output must stay zero (no DC leak,
    // no self-oscillation in feedback-bearing stages).
    {
        std::vector<float> zeros (int (sr * 0.5), 0.0f);
        bool ok = runChainExtreme (sr, PreampDSP::Channel::Crunch,
                                   ToneStack::Type::British, PowerAmp::AmpType::Marshall,
                                   0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, zeros);
        std::ostringstream d2;
        d2 << "all-zero in -> finite out: " << (ok ? "yes" : "NO");
        add ("NaN/Inf scan — silence input", ok, d2.str());
    }

    // Full-scale unipolar DC: signal stays at +0.99 for half a second. Tests
    // any stage that might break under no-zero-crossing input.
    {
        std::vector<float> dc (int (sr * 0.5), 0.99f);
        bool ok = runChainExtreme (sr, PreampDSP::Channel::Lead,
                                   ToneStack::Type::American, PowerAmp::AmpType::Fender,
                                   1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, 0.5f, 1.0f, dc);
        std::ostringstream d2;
        d2 << "+0.99 DC for 500 ms -> finite out: " << (ok ? "yes" : "NO");
        add ("NaN/Inf scan — DC input", ok, d2.str());
    }
}

// ---------------------------------------------------------------------------
// C2 — Sample-rate parametric
// ---------------------------------------------------------------------------

static void testSampleRates()
{
    // Re-prepare each stage at five common DAW sample rates and confirm a
    // simple sine-through-chain produces finite, non-trivial output.
    bool overallPass = true;
    std::ostringstream details;
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        auto sine = sineBuf (sr, 220.0, 0.3f, 0.3);
        bool ok = runChainExtreme (sr, PreampDSP::Channel::Crunch,
                                   ToneStack::Type::British,
                                   PowerAmp::AmpType::Marshall,
                                   0.5f, 0.5f, 0.5f, 0.5f, 0.4f, 0.5f, 0.5f, 0.3f, sine);
        const float pk = maxAbs (sine.data(), int (sine.size()));
        if (! ok || pk < 0.01f || pk > 5.0f) overallPass = false;
        details << int (sr / 1000) << "k=" << std::fixed << std::setprecision (2) << pk
                << (ok ? "" : "(NaN!)") << " ";
    }
    add ("Sample-rate parametric (5 SRs)", overallPass, details.str());
}

// ---------------------------------------------------------------------------
// C4 — Reset state purge
// ---------------------------------------------------------------------------

static void testResetPurge()
{
    constexpr double sr = 44100.0;

    // PowerAmp: feed a hot burst, reset(), feed silence, expect silence out.
    {
        PowerAmp pp; pp.prepare (sr); pp.setAmpType (PowerAmp::AmpType::Fender);
        pp.setDrive (0.8f); pp.setPresence (0.5f); pp.setResonance (0.5f); pp.setSag (1.0f);
        auto burst = sineBuf (sr, 110.0, 0.7f, 0.5);
        pp.process (burst.data(), int (burst.size()));
        pp.reset();
        std::vector<float> zeros (int (sr * 0.2), 0.0f);
        pp.process (zeros.data(), int (zeros.size()));
        const float leak = maxAbs (zeros.data() + int (sr * 0.05), int (zeros.size() - sr * 0.05));
        std::ostringstream d;
        d << "leak after reset: " << std::scientific << std::setprecision (2) << leak
          << " (need < 1e-3)";
        add ("Reset purge — PowerAmp", leak < 1e-3f, d.str());
    }

    // PostFX: same trick, but the delay + reverb tail are explicit state.
    {
        PostFX fx; fx.prepare (sr, 512);
        fx.setDelayEnabled (true); fx.setDelayTime (200.0f);
        fx.setDelayFeedback (0.7f); fx.setDelayMix (0.7f);
        fx.setReverbEnabled (true); fx.setReverbMix (0.5f); fx.setReverbDecay (0.7f);
        const int n1 = int (sr * 0.5);
        std::vector<float> L1 (n1, 0.0f), R1 (n1, 0.0f);
        auto noise = noiseBuf (n1, 0.5f, 0xCAFEBABE);
        std::copy (noise.begin(), noise.end(), L1.begin());
        std::copy (noise.begin(), noise.end(), R1.begin());
        constexpr int block = 512;
        for (int off = 0; off < n1; off += block)
        {
            int bs = std::min (block, n1 - off);
            fx.process (L1.data() + off, R1.data() + off, bs);
        }
        fx.reset();
        const int n2 = int (sr * 0.5);
        std::vector<float> L2 (n2, 0.0f), R2 (n2, 0.0f);
        for (int off = 0; off < n2; off += block)
        {
            int bs = std::min (block, n2 - off);
            fx.process (L2.data() + off, R2.data() + off, bs);
        }
        // Skip a tiny initial transient (in case of internal IIR settling).
        const int skip = int (sr * 0.05);
        const float leak = std::max (maxAbs (L2.data() + skip, n2 - skip),
                                      maxAbs (R2.data() + skip, n2 - skip));
        std::ostringstream d;
        d << "leak after reset: " << std::scientific << std::setprecision (2) << leak
          << " (need < 1e-3)";
        add ("Reset purge — PostFX (delay + reverb)", leak < 1e-3f, d.str());
    }
}

// ---------------------------------------------------------------------------
// C5 — Click test under audio-rate parameter modulation
// ---------------------------------------------------------------------------

static void testClickFreeModulation()
{
    // Mirror the plugin's smoothing pattern: PluginProcessor splits each
    // audio block into 32-sample sub-blocks and applies SmoothedValue::skip
    // to advance the smoother before each sub-block. Direct setter calls
    // (without smoothing) WILL produce clicks — that's expected, the
    // smoothing layer is the processor's job. We test the actual user-facing
    // behaviour: rapid host automation through the smoother.
    constexpr double sr = 44100.0;
    constexpr int kSmoothBlock = 32; // matches PluginProcessor::kSmoothingBlockSize

    PreampDSP p; p.prepare (sr); p.setChannel (PreampDSP::Channel::Crunch);
    p.setBright (false);
    PowerAmp pp; pp.prepare (sr); pp.setAmpType (PowerAmp::AmpType::Marshall);
    pp.setPresence (0.5f); pp.setResonance (0.5f); pp.setSag (0.3f);

    juce::SmoothedValue<float> gainSm, driveSm;
    const double rampSec = double (kSmoothBlock) / sr;
    gainSm.reset (sr, rampSec);
    driveSm.reset (sr, rampSec);
    gainSm.setCurrentAndTargetValue (0.5f);
    driveSm.setCurrentAndTargetValue (0.5f);

    auto sine = sineBuf (sr, 220.0, 0.3f, 1.0);
    const int n = int (sine.size());
    juce::Random rng { 0x12345678 };

    // Realistic DAW automation pattern: continuous drift, not random snaps.
    // Targets walk slowly between new endpoints every ~50 ms (2200 samples)
    // — matching slider-drag or LFO-style modulation rates that a user
    // actually generates.
    constexpr int hostBlock = 256;
    constexpr int targetChangeSamples = 2200;
    int samplesUntilTargetChange = 0;
    float gainTarget = 0.5f, driveTarget = 0.5f;

    for (int off = 0; off < n; off += hostBlock)
    {
        const int hb = std::min (hostBlock, n - off);
        if (samplesUntilTargetChange <= 0)
        {
            // Smooth-drift target: ±0.2 swing per change, clamped to [0.1, 0.9].
            gainTarget  = std::clamp (gainTarget  + (rng.nextFloat() - 0.5f) * 0.4f, 0.1f, 0.9f);
            driveTarget = std::clamp (driveTarget + (rng.nextFloat() - 0.5f) * 0.4f, 0.1f, 0.9f);
            gainSm.setTargetValue  (gainTarget);
            driveSm.setTargetValue (driveTarget);
            samplesUntilTargetChange = targetChangeSamples;
        }
        samplesUntilTargetChange -= hb;

        int rem = hb;
        int subOff = off;
        while (rem > 0)
        {
            const int bs = std::min (rem, kSmoothBlock);
            p.setGain  (gainSm.skip (bs));
            pp.setDrive (driveSm.skip (bs));
            p.process (sine.data() + subOff, bs);
            pp.process (sine.data() + subOff, bs);
            subOff += bs;
            rem -= bs;
        }
    }

    // 220 Hz at 0.3 amp gives natural diff ~0.04. Post-stage tanh ±1.1.
    // Under realistic-rate automation through the smoother, sub-block
    // parameter steps should produce diffs well under 0.3.
    float maxJump = 0.0f;
    int skipS = int (sr * 0.1);
    for (int i = skipS + 1; i < n; ++i)
        maxJump = std::max (maxJump, std::abs (sine[i] - sine[i - 1]));
    std::ostringstream d;
    d << "max sample-to-sample jump: " << std::fixed << std::setprecision (3) << maxJump
      << " (need < 0.3, realistic ±0.2 automation drift every 50 ms)";
    add ("No-click on automation (Preamp+PowerAmp)", maxJump < 0.3f, d.str());
}

// ---------------------------------------------------------------------------
// C6 — Denormal / fade-out tail
// ---------------------------------------------------------------------------

static void testDenormalFadeout()
{
    constexpr double sr = 44100.0;
    PowerAmp pp; pp.prepare (sr); pp.setAmpType (PowerAmp::AmpType::Fender);
    pp.setDrive (0.6f); pp.setPresence (0.5f); pp.setResonance (0.5f); pp.setSag (0.5f);

    // Generate 2 s of sine that linearly fades to zero in the last 1 s,
    // then 3 s of true silence. Any stage that doesn't flush denormals
    // either NaNs out or pegs CPU; we test the cheap way (NaN check).
    const int n = int (sr * 5.0);
    std::vector<float> buf (n, 0.0f);
    for (int i = 0; i < int (sr * 1.0); ++i)
        buf[i] = 0.5f * float (std::sin (2.0 * M_PI * 220.0 * i / sr));
    for (int i = 0; i < int (sr * 1.0); ++i)
    {
        const float fade = 1.0f - float (i) / float (sr);
        buf[int (sr * 1.0) + i] = 0.5f * fade
                                * float (std::sin (2.0 * M_PI * 220.0 * i / sr));
    }
    constexpr int block = 512;
    for (int off = 0; off < n; off += block)
    {
        const int bs = std::min (block, n - off);
        pp.process (buf.data() + off, bs);
    }
    const bool finite = allFinite (buf.data(), n);
    // Tail should be quiescent; if not, sag voltage or NFB state drifted.
    const float tail = maxAbs (buf.data() + int (sr * 4.5), int (sr * 0.5));
    std::ostringstream d;
    d << "all finite: " << (finite ? "yes" : "NO")
      << ", tail max-abs: " << std::scientific << std::setprecision (2) << tail
      << " (need < 1e-3)";
    add ("Denormal-safe fade-out (PowerAmp)", finite && tail < 1e-3f, d.str());
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::cout << "=== DuskAmp Safety Scan ===\n\n";

    std::cout << "-- C1: NaN/Inf safety --\n";
    testNaNScan();

    std::cout << "\n-- C2: Sample-rate parametric --\n";
    testSampleRates();

    std::cout << "\n-- C4: Reset state purge --\n";
    testResetPurge();

    std::cout << "\n-- C5: No clicks on parameter modulation --\n";
    testClickFreeModulation();

    std::cout << "\n-- C6: Denormal handling --\n";
    testDenormalFadeout();

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
