// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Two things about TapeMachine 2's core.
//
// 1. Gain Link is the exact inverse of Input Gain and nothing else. A linked instance must
//    give the bits of an unlinked one whose Output is set to minus Input (the "plain"
//    instance below), in steady programme and through every kind of change. "The bits" is
//    the point: anything that measured the level and corrected it, however gently, would
//    pass a tolerance and still be riding the programme.
//
// 2. A preset-sized change starts from a clean state. A filter that keeps its state while
//    its coefficients jump can put out tens of dB more than the programme; so can the
//    path's memory after bypass or Thru. The state is cleared where that would start, so
//    after such a change nothing is louder than what played before and what plays after,
//    the programme is back within milliseconds, and the cut itself is not a click. A knob
//    gesture is not such a change and must not be treated as one.
//
// `CleanChangeTest --print` lists every measured value next to its bound.
#include "TapeMachineDSP.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace
{
using duskaudio::TapeMachineDSP;

int failures = 0;
bool printValues = false;

// Holds `value <= bound` (or >= with atLeast), and lists it under --print.
void hold (const std::string& what, const char* reading, double value, double bound, bool atLeast = false)
{
    const bool ok = atLeast ? value >= bound : value <= bound;      // false for NaN either way
    if (printValues || ! ok)
        std::printf ("%s %s: %s = %.3f (%s %.3f)\n", ok ? "  ok" : "FAIL", what.c_str(), reading, value,
                     atLeast ? "at least" : "at most", bound);
    if (! ok) ++failures;
}

double toDb (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

// The programme: by default a four-sine chord, the right channel a little lower and without
// the top partial so the two channels are not copies. `level` scales it sample by sample.
// The other two are for what a chord that stops at 3.3 kHz cannot show: treble (the tape's
// level-dependent top end) and a single tone (anything added under it stands out).
enum Programme { chordProgramme, trebleProgramme, toneProgramme };
struct Chord
{
    double rate;
    Programme kind;
    double phase[4] = { 0.0, 0.0, 0.0, 0.0 };
    Chord (double sampleRate, Programme programme) : rate (sampleRate), kind (programme) {}

    void next (float level, float& left, float& right)
    {
        static constexpr double freqs[3][4] = { { 110.0, 440.0, 1200.0, 3300.0 },
                                                { 440.0, 440.0, 9000.0, 13000.0 },
                                                { 1000.0, 1000.0, 1000.0, 1000.0 } };
        const double* freq = freqs[kind];
        static constexpr double twoPi = 6.283185307179586;
        double sum = 0.0, sumNoTop = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            const double v = std::sin (phase[i]);
            sum += v;
            if (i < 3) sumNoTop += v;
            phase[i] += twoPi * freq[i] / rate;
            if (phase[i] > twoPi) phase[i] -= twoPi;
        }
        left  = level * static_cast<float> (sum * 0.25);
        right = level * static_cast<float> (sumNoTop * 0.2);
    }
};

struct Render
{
    double rate = 48000.0;
    std::vector<float> left, right;

    size_t at (double ms) const { return static_cast<size_t> (std::llround (ms * rate / 1000.0)); }

    // Peak, RMS and largest sample-to-sample jump over [from + ms0, from + ms1), in dB.
    double peak (size_t from, double ms0, double ms1) const
    {
        float p = 0.0f;
        for (size_t i = from + at (ms0); i < std::min (from + at (ms1), left.size()); ++i)
            p = std::max (p, std::max (std::abs (left[i]), std::abs (right[i])));
        return toDb (p);
    }
    double rms (size_t from, double ms0, double ms1) const
    {
        double acc = 0.0;
        size_t count = 0;
        for (size_t i = from + at (ms0); i < std::min (from + at (ms1), left.size()); ++i, ++count)
            acc += static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i];
        return toDb (std::sqrt (acc / static_cast<double> (std::max<size_t> (2 * count, 1))));
    }
    double jump (size_t from, double ms0, double ms1) const
    {
        float j = 0.0f;
        for (size_t i = std::max<size_t> (from + at (ms0), 1); i < std::min (from + at (ms1), left.size()); ++i)
            j = std::max (j, std::max (std::abs (left[i] - left[i - 1]), std::abs (right[i] - right[i - 1])));
        return toDb (j);
    }
};

// A scripted run: `setup (dsp, isPlain)` before prepare, `events (dsp, isPlain, pos)` before
// every block, `level (n)` the programme.
struct Script
{
    std::string name;
    double rate;
    int block;
    double seconds;
    std::function<void (TapeMachineDSP&, bool isPlain)> setup;
    std::function<void (TapeMachineDSP&, bool isPlain, size_t pos)> events;
    std::function<float (size_t n)> level;
    int channels = 2;
    Programme programme = chordProgramme;

    std::string label() const
    {
        char text[160];
        std::snprintf (text, sizeof (text), "%s, %.0f Hz, block %d", name.c_str(), rate, block);
        return text;
    }
};

Render render (const Script& sc, bool isPlain)
{
    TapeMachineDSP dsp;
    sc.setup (dsp, isPlain);
    dsp.prepare (sc.rate, sc.block);
    const size_t total = static_cast<size_t> (sc.rate * sc.seconds), block = static_cast<size_t> (sc.block);
    Render out;
    out.rate = sc.rate;
    out.left.resize (total);
    out.right.resize (total);
    std::vector<float> inL (block), inR (block);
    Chord chord (sc.rate, sc.programme);
    for (size_t pos = 0; pos < total; pos += block)
    {
        const size_t n = std::min (block, total - pos);
        sc.events (dsp, isPlain, pos);
        for (size_t i = 0; i < n; ++i)
            chord.next (sc.level (pos + i), inL[i], inR[i]);
        const float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { out.left.data() + pos, out.right.data() + pos };
        dsp.processBlock (ins, outs, sc.channels, static_cast<int> (n));
        if (sc.channels == 1)       // the readings above take both channels
            std::copy (out.left.begin() + static_cast<std::ptrdiff_t> (pos),
                       out.left.begin() + static_cast<std::ptrdiff_t> (pos + n),
                       out.right.begin() + static_cast<std::ptrdiff_t> (pos));
    }
    return out;
}

// Renders the script linked and plain, holds the linked render to the plain one's bits from
// the first sample to the last, and returns the plain render for the level checks.
Render linkedIsPlain (const Script& sc)
{
    const Render linked = render (sc, false), plain = render (sc, true);
    hold (sc.label(), "plain render peak, dBFS (it must not be silent)", plain.peak (0, 0.0, sc.seconds * 1000.0), -40.0, true);
    // Finite first: two renders with the same NaN in the same place have the same bits, and
    // the level readings would step over it.
    size_t notFinite = 0;
    for (size_t i = 0; i < linked.left.size(); ++i)
        notFinite += ! (std::isfinite (linked.left[i]) && std::isfinite (linked.right[i])
                        && std::isfinite (plain.left[i]) && std::isfinite (plain.right[i]));
    hold (sc.label(), "non-finite samples in the linked and the plain render", static_cast<double> (notFinite), 0.0);
    size_t at = linked.left.size();
    for (size_t i = 0; i < linked.left.size() && at == linked.left.size(); ++i)
        if (std::memcmp (&linked.left[i], &plain.left[i], sizeof (float)) != 0
            || std::memcmp (&linked.right[i], &plain.right[i], sizeof (float)) != 0)
            at = i;
    if (at != linked.left.size())
    {
        ++failures;
        std::printf ("FAIL %s: linked differs from the plain instance at sample %zu (%.9g vs %.9g)\n", sc.label().c_str(),
                     at, static_cast<double> (linked.left[at]), static_cast<double> (plain.left[at]));
    }
    else if (printValues)
        std::printf ("  ok %s: linked is the plain instance, bit for bit\n", sc.label().c_str());
    return plain;
}

// The event lands on the first block boundary at or after `seconds`.
size_t boundary (double seconds, double rate, int block)
{
    const size_t s = static_cast<size_t> (seconds * rate), b = static_cast<size_t> (block);
    return (s + b - 1) / b * b;
}

// --- settings -----------------------------------------------------------------------------
// Two settings about as far apart as the factory presets get: a hot drum-bus one with a
// large low bell and top lift in the reproduce EQ, and a quiet clean one. Without a clean
// start, going from the first to the second peaks about 37 dB over the programme.
void hotBus (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (1); d.setTapeType (1); d.setCalibration (1);
    d.setAutoCal (false); d.setBias (62.0f);
    d.setReproHf (8.7f); d.setReproSubBell (17.5f);
    d.setProgHmfTrim (-14.0f); d.setProgHfTrim (-14.0f);
}
// Two more pairs from the factory presets' range, for what a clean start has to set and not
// ramp: a saturated setting to another with far less drive, and a very low bias to a high one.
void thick (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (1); d.setTapeType (1); d.setCalibration (3);
    d.setAutoCal (false); d.setBias (40.0f);
    d.setReproHf (6.1f); d.setReproSubBell (19.0f);
    d.setProgHmfTrim (-8.0f); d.setProgHfTrim (-8.0f);
}
void lowBias (TapeMachineDSP& d)
{
    d.setTapeMachine (1); d.setTapeSpeed (0); d.setTapeType (1); d.setCalibration (0);
    d.setAutoCal (false); d.setBias (26.0f);
    d.setReproHf (0.8f); d.setReproSubBell (15.0f);
    d.setProgHmfTrim (0.0f); d.setProgHfTrim (-3.0f);
}
void highBias (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (2); d.setTapeType (2); d.setCalibration (3);
    d.setAutoCal (false); d.setBias (64.2f);
    d.setReproHf (1.7f); d.setReproSubBell (10.0f);
    d.setProgHmfTrim (0.0f); d.setProgHfTrim (0.0f);
}
// An American mastering setting: the one on which a drop of Input alone overshoots most.
void americanMaster (TapeMachineDSP& d)
{
    d.setTapeMachine (1); d.setTapeSpeed (1); d.setTapeType (0); d.setCalibration (0); d.setHeadWidth (1);
    d.setAutoCal (true);
    d.setReproLf (0.7f); d.setReproLmf (0.5f); d.setReproHmf (-0.5f); d.setReproHf (2.1f); d.setReproSubBell (18.0f);
}
// The hot bus deck with its reproduce EQ and trims set flat: the same topology, other filters.
void hotBusFlat (TapeMachineDSP& d)
{
    hotBus (d);
    d.setReproHf (0.0f); d.setReproSubBell (0.0f);
    d.setProgHmfTrim (0.0f); d.setProgHfTrim (0.0f);
}
void cleanDeck (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (2); d.setTapeType (2); d.setCalibration (0);
    d.setAutoCal (true); d.setBias (50.0f);
    d.setReproHf (-0.6f); d.setReproSubBell (0.0f);
    d.setProgHmfTrim (0.0f); d.setProgHfTrim (0.0f);
}

// Input Gain on both kinds of instance: the plain one carries the inverse in Output itself,
// the linked one a stored Output of +5 dB that the link must ignore.
void setInput (TapeMachineDSP& d, bool isPlain, float inputDb)
{
    d.setInputGainDb (inputDb);
    d.setOutputGainDb (isPlain ? -inputDb : 5.0f);
}

using Setting = void (*) (TapeMachineDSP&);

// One thing changed on the hot bus setting, and nothing else.
void otherSpeed (TapeMachineDSP& d)        { d.setTapeSpeed (2); }
void otherTape (TapeMachineDSP& d)         { d.setTapeType (3); }
void otherCalibration (TapeMachineDSP& d)  { d.setCalibration (3); }
void otherEqStandard (TapeMachineDSP& d)   { d.setEqStandard (1); }
void inputPath (TapeMachineDSP& d)         { d.setSignalPath (2); }
void otherHeadWidth (TapeMachineDSP& d)    { d.setHeadWidth (2); }
// The Swiss deck has no 3.75 IPS and runs it at 15: from 15 to 3.75 nothing changes there.
void swissAt15 (TapeMachineDSP& d)         { d.setTapeMachine (0); d.setTapeSpeed (1); }
void sameSpeedOnSwiss (TapeMachineDSP& d)  { d.setTapeSpeed (3); }

// --- checks -------------------------------------------------------------------------------
// A preset-sized change at one second, from one setting and Input Gain to another.
void checkChange (const char* name, double rate, int block, float level,
                  Setting before, float inputBefore, Setting after, float inputAfter,
                  Programme programme = chordProgramme, int channels = 2, double maxOver = 0.5)
{
    const size_t at = boundary (1.0, rate, block);
    Script sc {
        name, rate, block, 2.5,
        [=] (TapeMachineDSP& d, bool isPlain) { before (d); setInput (d, isPlain, inputBefore); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
            if (pos != at) return;
            after (d);
            setInput (d, isPlain, inputAfter);
        },
        [level] (size_t) { return level; } };
    sc.programme = programme;
    sc.channels = channels;
    const Render p = linkedIsPlain (sc);
    const std::string what = sc.label();

    // Nothing after the change is louder than both what played before and what plays after.
    const double allowed = std::max (p.peak (at, -250.0, 0.0), p.peak (at, 1000.0, 1250.0));
    hold (what, "peak in the 250 ms after the change over the louder of before and after, dB",
          p.peak (at, 0.0, 250.0) - allowed, maxOver);
    // The programme is back: from 10 ms on, no 5 ms of it is far under the quieter of the two.
    const double floorLevel = std::min (p.rms (at, -250.0, 0.0), p.rms (at, 1000.0, 1250.0));
    double worst = 0.0;
    for (int ms = 10; ms < 300; ms += 5)
        worst = std::min (worst, p.rms (at, ms, ms + 5) - floorLevel);
    hold (what, "level from 10 ms after the change under the quieter of before and after, dB", worst, -3.0, true);
    // The cut is not a click: no jump between samples far beyond what the programme does.
    hold (what, "largest sample jump in the 5 ms after the change over the largest before, dB",
          p.jump (at, 0.0, 5.0) - std::max (p.jump (at, -250.0, 0.0), p.jump (at, 1000.0, 1250.0)), 3.0);
}

// A switch that does not make the path burst is not a clean start: no dip at all.
void checkLeftAlone (const char* name, double rate, int block, Setting before, float inputBefore,
                     Setting after, float inputAfter)
{
    const size_t at = boundary (1.0, rate, block);
    const Script sc {
        name, rate, block, 2.0,
        [=] (TapeMachineDSP& d, bool isPlain) { before (d); setInput (d, isPlain, inputBefore); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
            if (pos != at) return;
            after (d);
            setInput (d, isPlain, inputAfter);
        },
        [] (size_t) { return 0.25f; } };
    const Render p = linkedIsPlain (sc);
    const double floorLevel = std::min (p.rms (at, -250.0, 0.0), p.rms (at, 500.0, 750.0));
    double worst = 0.0;
    for (int ms = 0; ms < 100; ms += 5)
        worst = std::min (worst, p.rms (at, ms, ms + 5) - floorLevel);
    hold (sc.label(), "level in any 5 ms after it under the quieter of before and after, dB", worst, -1.5, true);
    const double allowed = std::max (p.peak (at, -250.0, 0.0), p.peak (at, 500.0, 750.0));
    hold (sc.label(), "peak in the 250 ms after it over the louder of before and after, dB", p.peak (at, 0.0, 250.0) - allowed, 1.0);
}

// A preset whose parameters reach the core over two blocks. Either the deck first and Input
// one block later, or the deck and Input first and bias and the reproduce EQ one block
// later. Both halves belong to one change.
void checkSplitPreset (bool inputLater, double rate, int block)
{
    const size_t at = boundary (1.0, rate, block), next = at + static_cast<size_t> (block);
    const Script sc {
        inputLater ? "preset over two blocks, Input in the second" : "preset over two blocks, bias and EQ in the second",
        rate, block, 2.5,
        [] (TapeMachineDSP& d, bool isPlain) { hotBus (d); setInput (d, isPlain, 10.9f); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
            if (pos == at)
            {
                d.setTapeMachine (0); d.setTapeSpeed (2); d.setTapeType (2); d.setCalibration (0);
                if (inputLater) cleanDeck (d); else setInput (d, isPlain, -9.1f);
            }
            if (pos == next)
            {
                if (inputLater) setInput (d, isPlain, -9.1f); else cleanDeck (d);
            }
        },
        [] (size_t) { return 0.7f; } };
    const Render p = linkedIsPlain (sc);
    const double allowed = std::max (p.peak (at, -250.0, 0.0), p.peak (at, 1000.0, 1250.0));
    hold (sc.label(), "peak in the 250 ms after the first part over the louder of before and after, dB",
          p.peak (at, 0.0, 250.0) - allowed, 0.5);
    const double floorLevel = std::min (p.rms (at, -250.0, 0.0), p.rms (at, 1000.0, 1250.0));
    double worst = 0.0;
    for (int ms = 10; ms < 300; ms += 5)
        worst = std::min (worst, p.rms (next, ms, ms + 5) - floorLevel);
    hold (sc.label(), "level from 10 ms after the second part under the quieter of before and after, dB", worst, -3.0, true);
}

// The settling time after a topology change, from each side.
void checkSettling()
{
    const double rate = 48000.0;
    const auto level = [] (size_t) { return 0.7f; };
    const auto setup = [] (TapeMachineDSP& d, bool isPlain) { hotBus (d); setInput (d, isPlain, 10.9f); d.setAutoComp (! isPlain); };
    const auto deckOnly = [] (TapeMachineDSP& d) { d.setTapeMachine (0); d.setTapeSpeed (2); d.setTapeType (2); d.setCalibration (0); };
    // Reads the peak over the louder of before and after, and the level from 10 ms after
    // `from` under the quieter of the two.
    const auto read = [] (const Script& sc, size_t at, size_t from, double maxOver, double minLevel) {
        const Render p = linkedIsPlain (sc);
        const double allowed = std::max (p.peak (at, -250.0, 0.0), p.peak (at, 1000.0, 1250.0));
        hold (sc.label(), "peak in the 250 ms after the first part over the louder of before and after, dB",
              p.peak (at, 0.0, 250.0) - allowed, maxOver);
        const double floorLevel = std::min (p.rms (at, -250.0, 0.0), p.rms (at, 1000.0, 1250.0));
        double worst = 0.0;
        for (int ms = 10; ms < 300; ms += 5)
            worst = std::min (worst, p.rms (from, ms, ms + 5) - floorLevel);
        hold (sc.label(), "level from 10 ms after the last part under the quieter of before and after, dB", worst, minLevel, true);
    };
    {   // The second part two blocks later, 21 ms after the first: still inside.
        const int block = 512;
        const size_t at = boundary (1.0, rate, block), later = at + 2 * static_cast<size_t> (block);
        read ({ "preset over three blocks of 512, Input in the third", rate, block, 2.5, setup,
                [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                    if (pos == at) deckOnly (d);
                    if (pos == later) { setInput (d, isPlain, -9.1f); cleanDeck (d); }
                }, level }, at, later, 0.5, -3.0);
    }
    {   // A host block longer than the settling time: the next block still belongs to it.
        const int block = 4096;
        const size_t at = boundary (1.0, rate, block), later = at + static_cast<size_t> (block);
        read ({ "preset over two blocks of 4096, Input in the second", rate, block, 3.0, setup,
                [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                    if (pos == at) deckOnly (d);
                    if (pos == later) { setInput (d, isPlain, -9.1f); cleanDeck (d); }
                }, level }, at, later, 0.5, -3.0);
    }
    {   // 200 ms after the change the window is long closed: a knob moved then is a knob.
        const int block = 64;
        const size_t at = boundary (1.0, rate, block), later = boundary (1.2, rate, block);
        const Script sc { "a knob moved 200 ms after a machine switch", rate, block, 2.5,
                          [] (TapeMachineDSP& d, bool isPlain) { d.setTapeMachine (0); setInput (d, isPlain, 3.0f); d.setAutoComp (! isPlain); },
                          [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                              if (pos == at) d.setTapeMachine (1);
                              if (pos == later) { setInput (d, isPlain, 1.0f); d.setReproHf (2.0f); }
                          }, level };
        const Render p = linkedIsPlain (sc);
        const double floorLevel = std::min (p.rms (at, 100.0, 190.0), p.rms (at, 1000.0, 1250.0));
        double worst = 0.0;
        for (int ms = 0; ms < 100; ms += 5)
            worst = std::min (worst, p.rms (later, ms, ms + 5) - floorLevel);
        hold (sc.label(), "level in any 5 ms after the knob moved under the quieter of before and after, dB", worst, -1.5, true);
    }
    {   // A parameter the host moves a little on every block (automation) while bypass is
        // switched off, and while the machine is switched: the path must not be held empty.
        const int block = 64;
        const size_t at = boundary (1.0, rate, block), off = boundary (2.0, rate, block);
        for (const bool viaBypass : { true, false })
        {
            const Script sc { viaBypass ? "bypass off while Input is under automation" : "machine switched while Input is under automation",
                              rate, block, 3.5,
                              [] (TapeMachineDSP& d, bool isPlain) { d.setTapeMachine (0); setInput (d, isPlain, 0.0f); d.setAutoComp (! isPlain); },
                              [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                                  setInput (d, isPlain, static_cast<float> (pos) / static_cast<float> (rate));   // 1 dB/s
                                  if (viaBypass && pos == at)  d.setBypass (true);
                                  if (viaBypass && pos == off) d.setBypass (false);
                                  if (! viaBypass && pos == off) d.setTapeMachine (1);
                              }, level };
            const Render p = linkedIsPlain (sc);
            const double reference = viaBypass ? p.rms (at, -250.0, 0.0) : p.rms (off, -250.0, 0.0);
            double worst = 0.0;
            for (int ms = 10; ms < 600; ms += 5)
                worst = std::min (worst, p.rms (off, ms, ms + 5) - reference);
            hold (sc.label(), "level from 10 ms after the event under the level before, dB", worst, -3.0, true);
        }
    }
    {   // After a lone drop of Input nothing is settling: a knob nudged in the next block is a
        // knob. Against the same run without the nudge, the 20 ms after it differ by what a
        // fifth of a dB of reproduce EQ does, not by a path started over.
        const int block = 512;
        const size_t at = boundary (1.0, rate, block), later = at + static_cast<size_t> (block);
        const auto script = [=] (bool withNudge) {
            return Script { "a knob nudged in the block after a lone Input drop", rate, block, 2.5,
                            [] (TapeMachineDSP& d, bool isPlain) { americanMaster (d); setInput (d, isPlain, 2.7f); d.setAutoComp (! isPlain); },
                            [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                                if (pos == at) setInput (d, isPlain, -9.3f);
                                if (withNudge && pos == later) d.setReproHf (2.3f);
                            }, level };
        };
        const Render with = linkedIsPlain (script (true)), without = render (script (false), true);
        double diff = 0.0, ref = 0.0;
        for (size_t i = later; i < later + with.at (20.0); ++i)
        {
            diff += (with.left[i] - without.left[i]) * static_cast<double> (with.left[i] - without.left[i]);
            ref += static_cast<double> (without.left[i]) * without.left[i];
        }
        hold (script (true).label(), "difference from the run without the nudge over the 20 ms after it, dB under the signal",
              10.0 * std::log10 (std::max (diff, 1e-30) / std::max (ref, 1e-30)), -20.0);
    }
    {   // A machine switch, then bypass 5 ms later for a second: what was settling does not
        // outlive the passthrough. A knob nudged in the block after the way back is a knob.
        const int block = 64;
        const size_t at = boundary (1.0, rate, block), on = at + 4 * static_cast<size_t> (block);
        const size_t off = boundary (2.0, rate, block), later = off + static_cast<size_t> (block);
        const auto script = [=] (bool withNudge) {
            return Script { "a knob nudged after a bypass that began inside a settling time", rate, block, 3.5,
                            [] (TapeMachineDSP& d, bool isPlain) { d.setTapeMachine (0); setInput (d, isPlain, 3.0f); d.setAutoComp (! isPlain); },
                            [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                                if (pos == at)  d.setTapeMachine (1);
                                if (pos == on)  d.setBypass (true);
                                if (pos == off) d.setBypass (false);
                                if (withNudge && pos == later) setInput (d, isPlain, 2.0f);
                            }, level };
        };
        const Render with = linkedIsPlain (script (true)), without = render (script (false), true);
        double diff = 0.0, ref = 0.0;
        for (size_t i = later; i < later + with.at (5.0); ++i)
        {
            diff += (with.left[i] - without.left[i]) * static_cast<double> (with.left[i] - without.left[i]);
            ref += static_cast<double> (without.left[i]) * without.left[i];
        }
        hold (script (true).label(), "difference from the run without the nudge over the 5 ms after it, dB under the signal",
              10.0 * std::log10 (std::max (diff, 1e-30) / std::max (ref, 1e-30)), -15.0);
    }
    {   // Two lone drops 300 ms apart: the hold is 100 ms, so each starts clean.
        const int block = 64;
        const size_t at = boundary (1.0, rate, block), later = boundary (1.3, rate, block);
        const Script sc { "two Input drops of 9 dB, 300 ms apart", rate, block, 2.8,
                          [] (TapeMachineDSP& d, bool isPlain) { americanMaster (d); setInput (d, isPlain, 8.7f); d.setAutoComp (! isPlain); },
                          [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                              if (pos == at) setInput (d, isPlain, -0.3f);
                              if (pos == later) setInput (d, isPlain, -9.3f);
                          }, level };
        const Render p = linkedIsPlain (sc);
        const double allowed = std::max (p.peak (later, -100.0, 0.0), p.peak (later, 1000.0, 1250.0));
        hold (sc.label(), "peak in the 250 ms after the second drop over the louder of before and after, dB",
              p.peak (later, 0.0, 250.0) - allowed, 0.5);
    }
}

// A preset with a lot in its second half: a bright American setting to a worn cassette one,
// the deck, the switches and Input in one block (the first eleven of the preset's
// parameters) and bias, the filters, wow, flutter, noise and the reproduce EQ in the next.
void checkSplitWithEverythingLater()
{
    const double rate = 48000.0; const int block = 64;
    const size_t at = boundary (1.0, rate, block), next = at + static_cast<size_t> (block);
    const Script sc {
        "bright deck to worn cassette over two blocks, bias and filters in the second", rate, block, 2.5,
        [] (TapeMachineDSP& d, bool isPlain) {
            d.setTapeMachine (1); d.setTapeSpeed (0); d.setTapeType (1); d.setCalibration (0); d.setHeadWidth (0);
            d.setCrosstalk (false); d.setAutoCal (false); d.setBias (26.0f);
            d.setHighpassHz (20.0f); d.setLowpassHz (19000.0f); d.setWow (5.0f); d.setFlutter (3.0f);
            d.setReproLf (-3.1f); d.setReproLmf (0.3f); d.setReproHf (0.8f); d.setReproSubBell (15.0f);
            setInput (d, isPlain, -10.2f); d.setAutoComp (! isPlain);
        },
        [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
            if (pos == at)
            {
                d.setTapeMachine (1); d.setTapeSpeed (3); d.setTapeType (2); d.setCalibration (0); d.setHeadWidth (0);
                d.setCrosstalk (true);
                setInput (d, isPlain, 10.9f);
            }
            if (pos == next)
            {
                d.setAutoCal (false); d.setBias (78.0f);
                d.setHighpassHz (34.0f); d.setLowpassHz (10800.0f); d.setLpQ (0.89f); d.setWow (18.0f); d.setFlutter (12.0f);
                d.setReproLf (0.75f); d.setReproLmf (-3.8f); d.setReproHmf (-4.7f); d.setReproHf (-8.3f); d.setReproSubBell (0.0f);
            }
        },
        [] (size_t) { return 0.7f; } };
    const Render p = linkedIsPlain (sc);
    const double allowed = std::max (p.peak (at, -250.0, 0.0), p.peak (at, 1000.0, 1250.0));
    hold (sc.label(), "peak in the 250 ms after the first part over the louder of before and after, dB", p.peak (at, 0.0, 250.0) - allowed, 1.0);
    const double floorLevel = std::min (p.rms (at, -250.0, 0.0), p.rms (at, 1000.0, 1250.0));
    double worst = 0.0;
    for (int ms = 5; ms < 100; ms += 5)
        worst = std::min (worst, p.rms (next, ms, ms + 5) - floorLevel);
    hold (sc.label(), "level from 5 ms after the second part under the quieter of before and after, dB", worst, -3.0, true);
}

// The stored Output is ignored while the link is on, inside a settling time too: two linked
// instances, one of which is sent a new Output in the block after a machine switch.
void checkStoredOutputInsideTheWindow()
{
    const double rate = 48000.0; const int block = 64;
    const size_t at = boundary (1.0, rate, block), next = at + static_cast<size_t> (block);
    const auto script = [=] (bool withOutput) {
        return Script { "a stored Output sent in the block after a machine switch, linked", rate, block, 2.0,
                        [] (TapeMachineDSP& d, bool) { d.setTapeMachine (0); d.setInputGainDb (6.0f); d.setAutoComp (true); },
                        [=] (TapeMachineDSP& d, bool, size_t pos) {
                            if (pos == at) d.setTapeMachine (1);
                            if (withOutput && pos == next) d.setOutputGainDb (-7.0f);
                        },
                        [] (size_t) { return 0.25f; } };
    };
    const Render a = render (script (false), false), b = render (script (true), false);
    size_t differing = 0;
    for (size_t i = 0; i < a.left.size(); ++i)
        differing += std::memcmp (&a.left[i], &b.left[i], sizeof (float)) != 0 || std::memcmp (&a.right[i], &b.right[i], sizeof (float)) != 0;
    hold (script (true).label(), "samples that differ from the render without it", static_cast<double> (differing), 0.0);
}

// Bypass for a second in steady quiet treble, which is how bypass is used to compare. The
// tape's level-dependent top end is where the level followers show: started over at neutral
// they leave the first 50 ms several dB over; they start at their targets instead.
void checkBypassInQuietTreble (Setting setting, float inputDb, const char* name)
{
    const double rate = 48000.0; const int block = 64;
    const size_t on = boundary (1.0, rate, block), off = boundary (2.0, rate, block);
    Script sc {
        name, rate, block, 3.5,
        [=] (TapeMachineDSP& d, bool isPlain) { setting (d); setInput (d, isPlain, inputDb); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool, size_t pos) {
            if (pos == on)  d.setBypass (true);
            if (pos == off) d.setBypass (false);
        },
        [] (size_t) { return 0.02f; } };
    sc.programme = trebleProgramme;
    const Render p = linkedIsPlain (sc);
    const double settled = p.rms (off, 1000.0, 1250.0);
    double highest = -1.0e9, lowest = 0.0;
    for (int ms = 5; ms < 200; ms += 5)
    {
        highest = std::max (highest, p.rms (off, ms, ms + 10) - settled);
        lowest = std::min (lowest, p.rms (off, ms, ms + 10) - settled);
    }
    hold (sc.label(), "highest 10 ms of the 200 ms after the exit over the settled level, dB", highest, 1.0);
    hold (sc.label(), "lowest 10 ms of the 200 ms after the exit under the settled level, dB", lowest, -1.0, true);
}

// One non-finite input sample, then a change 100 ms later: the clean start must leave
// nothing of it behind, and that includes what it runs out.
void checkAfterANonFiniteSample (float bad, const char* name)
{
    const double rate = 48000.0; const int block = 64;
    const size_t at = boundary (1.0, rate, block), badAt = at - static_cast<size_t> (0.1 * rate) + 7;
    const Script sc {
        name, rate, block, 2.0,
        [] (TapeMachineDSP& d, bool isPlain) { hotBus (d); setInput (d, isPlain, 6.0f); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool, size_t pos) { if (pos == at) d.setCalibration (3); },
        [=] (size_t n) { return n == badAt ? bad : 0.25f; } };
    const Render p = render (sc, true), l = render (sc, false);
    size_t notFinite = 0;
    for (size_t i = at + p.at (20.0); i < p.left.size(); ++i)
        notFinite += ! (std::isfinite (p.left[i]) && std::isfinite (p.right[i]) && std::isfinite (l.left[i]) && std::isfinite (l.right[i]));
    hold (sc.label(), "non-finite output samples from 20 ms after the change", static_cast<double> (notFinite), 0.0);
    hold (sc.label(), "peak from 20 ms after the change, dBFS (programme, not silence and not a stuck value)",
          p.peak (at, 20.0, 900.0), -30.0, true);
    hold (sc.label(), "peak from 20 ms after the change, dBFS", p.peak (at, 20.0, 900.0), 0.0);
    // And it plays as if the sample had never been there: the same run without it.
    Script clean = sc;
    clean.level = [] (size_t) { return 0.25f; };
    const Render reference = render (clean, true);
    hold (sc.label(), "level half a second after the change against the run without the sample, dB (either way)",
          std::abs (p.rms (at, 500.0, 900.0) - reference.rms (at, 500.0, 900.0)), 0.1);
}

// A 1 kHz tone through a calibration switch. What runs out of the old signal has no net
// area, so nothing is left under the tone: the mean of the 20 ms after the change stays far
// below the tone.
void checkNoThump()
{
    const double rate = 48000.0; const int block = 64;
    const size_t at = boundary (1.0, rate, block);
    Script sc {
        "1 kHz tone through a calibration switch", rate, block, 2.0,
        [] (TapeMachineDSP& d, bool isPlain) { d.setTapeMachine (0); d.setCalibration (0); setInput (d, isPlain, 0.0f); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool, size_t pos) { if (pos == at) d.setCalibration (1); },
        [] (size_t) { return 0.5f; } };
    sc.programme = toneProgramme;
    const Render p = linkedIsPlain (sc);
    double sum = 0.0;
    const size_t n = p.at (20.0);
    for (size_t i = at; i < at + n; ++i)
        sum += p.left[i];
    hold (sc.label(), "mean of the 20 ms after the change against the tone's level, dB",
          toDb (std::abs (sum / static_cast<double> (n))) - p.rms (at, 500.0, 750.0), -40.0);
}

// reset() and a second prepare() in the middle of programme: the linked render is still
// the plain one. (What each does to the audio is not this test's business.)
void checkResetAndPrepare()
{
    const double rate = 48000.0; const int block = 64;
    const size_t first = boundary (0.5, rate, block), second = boundary (1.0, rate, block);
    const Script sc {
        "reset() and prepare() again in running programme", rate, block, 1.5,
        [] (TapeMachineDSP& d, bool isPlain) { hotBus (d); setInput (d, isPlain, 6.0f); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool, size_t pos) {
            if (pos == first)  d.reset();
            if (pos == second) d.prepare (rate, block);
        },
        [] (size_t) { return 0.25f; } };
    linkedIsPlain (sc);
}

// Bypass or Thru switched on in loud programme and off again after it dropped 40 dB. The
// path's memory is the loud programme; none of it may come out on top of the quiet one.
void checkBackFromPassthrough (bool thru, bool drop, double rate, int block, int laterBlocks = 0,
                               Programme programme = chordProgramme, Setting setting = hotBus, float inputDb = 6.0f,
                               double maxOver = 1.0)
{
    const size_t on = boundary (1.0, rate, block), off = boundary (2.0, rate, block) + static_cast<size_t> (laterBlocks * block);
    const size_t quietFrom = on + static_cast<size_t> (0.3 * rate);
    std::string name = thru ? "back from Thru" : "back from bypass";
    name += drop ? " after the programme dropped 40 dB" : " in steady programme";
    if (programme == trebleProgramme) name += ", treble";
    if (programme == toneProgramme) name += ", 1 kHz tone";
    Script sc {
        name, rate, block, 3.5,
        [=] (TapeMachineDSP& d, bool isPlain) { setting (d); setInput (d, isPlain, inputDb); d.setAutoComp (! isPlain); },
        [=] (TapeMachineDSP& d, bool, size_t pos) {
            if (pos == on)  { if (thru) d.setSignalPath (3); else d.setBypass (true); }
            if (pos == off) { if (thru) d.setSignalPath (0); else d.setBypass (false); }
        },
        [=] (size_t n) { return drop && n >= quietFrom ? 0.007f : 0.7f; } };
    sc.programme = programme;
    const Render p = linkedIsPlain (sc);
    const double settled = p.peak (off, 1000.0, 1250.0);
    if (programme == toneProgramme)
    {
        // What runs out of the dry signal has no net area either.
        double sum = 0.0;
        const size_t n = p.at (20.0);
        for (size_t i = off; i < off + n; ++i)
            sum += p.left[i];
        hold (sc.label(), "mean of the 20 ms after the event against the tone's level, dB",
              toDb (std::abs (sum / static_cast<double> (n))) - p.rms (off, 1000.0, 1250.0), -30.0);
    }
    // Louder than both what was playing (the dry signal, which is what runs out) and what settles.
    hold (sc.label(), "peak in the 250 ms after the event over the louder of the dry signal before and the settled peak, dB",
          p.peak (off, 0.0, 250.0) - std::max (settled, p.peak (off, -250.0, 0.0)), maxOver);
    double worst = 0.0;
    for (int ms = 10; ms < 300; ms += 5)
        worst = std::min (worst, p.rms (off, ms, ms + 5) - p.rms (off, 1000.0, 1250.0));
    hold (sc.label(), "level from 10 ms after the event under the settled level, dB", worst, -3.0, true);
    // Against the dry signal just before the exit and the settled one after it, not against
    // the loud processed signal from before the passthrough.
    hold (sc.label(), "largest sample jump in the 5 ms after the event over the largest just before and after, dB",
          p.jump (off, 0.0, 5.0) - std::max (p.jump (off, -250.0, 0.0), p.jump (off, 1000.0, 1250.0)), 3.0);
}

// Knob gestures run on the smoothers and leave no dip: a 2 dB step, a 4 dB drop, and a
// full sweep of Input at a large host block, up and down, at 4 dB per block. A drop of more
// than 6 dB in one block is the one Input move that starts clean (it would overshoot).
void checkGestures()
{
    const auto noDip = [] (const Script& sc, size_t at, int ms0, int ms1, double bound) {
        const Render p = linkedIsPlain (sc);
        const double floorLevel = std::min (p.rms (at, -250.0, 0.0), p.rms (at, ms1 + 250.0, ms1 + 500.0));
        double worst = 0.0;
        for (int ms = ms0; ms < ms1; ms += 5)
            worst = std::min (worst, p.rms (at, ms, ms + 5) - floorLevel);
        hold (sc.label(), "level in any 5 ms of it under the quieter of before and after, dB", worst, bound, true);
    };
    const auto setup = [] (float input) {
        return [input] (TapeMachineDSP& d, bool isPlain) { d.setTapeMachine (0); setInput (d, isPlain, input); d.setAutoComp (! isPlain); };
    };
    const auto level = [] (size_t) { return 0.25f; };
    {
        const size_t at = boundary (1.0, 48000.0, 64);
        noDip ({ "Input +2 dB in one block", 48000.0, 64, 2.0, setup (0.0f),
                 [=] (TapeMachineDSP& d, bool isPlain, size_t pos) { if (pos == at) setInput (d, isPlain, 2.0f); }, level }, at, 0, 100, -1.0);
        noDip ({ "Input -4 dB in one block", 48000.0, 64, 2.0, setup (4.0f),
                 [=] (TapeMachineDSP& d, bool isPlain, size_t pos) { if (pos == at) setInput (d, isPlain, 0.0f); }, level }, at, 0, 100, -1.0);
    }
    {
        // Input falling 8 dB per block for three blocks of 43 ms: the first is a clean
        // start, the next two fall inside the hold and run on the smoother.
        const double rate = 48000.0; const int block = 2048;
        const size_t at = boundary (1.0, rate, block), b = static_cast<size_t> (block);
        noDip ({ "Input swept +12 to -12 dB at 8 dB per block", rate, block, 3.0, setup (12.0f),
                 [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                     for (size_t k = 0; k < 3; ++k)
                         if (pos == at + k * b) setInput (d, isPlain, 12.0f - 8.0f * static_cast<float> (k + 1));
                 }, level }, at, 20, 200, -3.0);
    }
    for (const float direction : { 1.0f, -1.0f })
    {
        const double rate = 48000.0; const int block = 2048;
        const size_t at = boundary (1.0, rate, block), b = static_cast<size_t> (block);
        const float from = -12.0f * direction;
        noDip ({ direction > 0.0f ? "Input swept -12 to +12 dB at 4 dB per block" : "Input swept +12 to -12 dB at 4 dB per block",
                 rate, block, 3.0, setup (from),
                 [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
                     for (size_t k = 0; k < 6; ++k)
                         if (pos == at + k * b) setInput (d, isPlain, from + direction * 4.0f * static_cast<float> (k + 1));
                 }, level }, at, 0, 300, -3.0);
    }
}

// Programme with everything a level matcher would react to, and no change at all.
void checkSteady (double rate, int block, int channels, int machine, float inputDb)
{
    char name[96];
    std::snprintf (name, sizeof (name), "steady (%d ch, machine %d, input %+.0f dB)", channels, machine, static_cast<double> (inputDb));
    Script sc {
        name, rate, block, 2.0,
        [=] (TapeMachineDSP& d, bool isPlain) { d.setTapeMachine (machine); setInput (d, isPlain, inputDb); d.setAutoComp (! isPlain); },
        [] (TapeMachineDSP&, bool, size_t) {},
        [rate] (size_t n) {
            const double t = static_cast<double> (n) / rate;
            if (t < 0.4) return 0.25f;                          // -12 dBFS
            if (t < 0.8) return 0.02f;                          // -34 dBFS
            if (t < 1.2) return 0.9f;                           //  -1 dBFS
            if (t < 1.4) return 0.0f;
            return std::fmod (t, 0.25) < 0.03 ? 0.7f : 0.0f;    // 30 ms bursts
        } };
    sc.channels = channels;
    linkedIsPlain (sc);
}

// The link switched on, and a preset that arrives from an unlinked state over two blocks
// (settings and Input in one, the link in the next): the linked render is the plain one.
void checkLinkOn (bool withPreset)
{
    const double rate = 48000.0; const int block = 64;
    const size_t at = boundary (1.0, rate, block), next = at + static_cast<size_t> (block);
    const Script sc {
        withPreset ? "preset from an unlinked state over two blocks" : "Gain Link switched on", rate, block, 2.5,
        [] (TapeMachineDSP& d, bool) { hotBus (d); d.setInputGainDb (10.9f); d.setOutputGainDb (-10.9f); d.setAutoComp (false); },
        [=] (TapeMachineDSP& d, bool isPlain, size_t pos) {
            if (withPreset && pos == at) { cleanDeck (d); d.setInputGainDb (-9.1f); d.setOutputGainDb (9.1f); }
            if (pos == next && ! isPlain) d.setAutoComp (true);
        },
        [] (size_t) { return 0.25f; } };
    const Render p = linkedIsPlain (sc);
    const double allowed = std::max (p.peak (at, -250.0, 0.0), p.peak (at, 1000.0, 1250.0));
    hold (sc.label(), "peak in the 250 ms after the event over the louder of before and after, dB",
          p.peak (at, 0.0, 250.0) - allowed, 0.5);
}

void noSetting (TapeMachineDSP&) {}
void american (TapeMachineDSP& d) { d.setTapeMachine (1); }
void swiss (TapeMachineDSP& d) { d.setTapeMachine (0); }
} // namespace

int main (int argc, char** argv)
{
    printValues = argc > 1 && std::strcmp (argv[1], "--print") == 0;

    checkSteady (44100.0, 37, 2, 0, 9.0f);
    checkSteady (48000.0, 512, 2, 1, -6.0f);
    checkSteady (96000.0, 64, 2, 0, 0.0f);
    checkSteady (48000.0, 64, 1, 1, 9.0f);

    // Preset-sized changes: the settings and Input together, the settings alone, a machine
    // switch alone, Input alone, quiet and loud programme, small and large host blocks.
    for (const int block : { 16, 37, 64, 512, 2048 })
        checkChange ("hot bus to clean deck", 48000.0, block, 0.25f, hotBus, 10.9f, cleanDeck, -9.1f);
    checkChange ("hot bus to clean deck", 44100.0, 64, 0.25f, hotBus, 10.9f, cleanDeck, -9.1f);
    checkChange ("hot bus to clean deck", 96000.0, 512, 0.25f, hotBus, 10.9f, cleanDeck, -9.1f);
    checkChange ("hot bus to clean deck, loud programme", 48000.0, 64, 0.7f, hotBus, 10.9f, cleanDeck, -9.1f);
    checkChange ("clean deck to hot bus", 48000.0, 64, 0.25f, cleanDeck, -9.1f, hotBus, 10.9f);
    checkChange ("hot bus to clean deck, Input unchanged", 48000.0, 64, 0.25f, hotBus, 6.0f, cleanDeck, 6.0f);
    checkChange ("Swiss to American", 48000.0, 512, 0.25f, swiss, 3.0f, american, 3.0f);
    // Each setting that makes the path burst, changed alone.
    checkChange ("speed alone", 48000.0, 64, 0.25f, hotBus, 6.0f, otherSpeed, 6.0f);
    checkChange ("calibration alone", 48000.0, 64, 0.7f, hotBus, 6.0f, otherCalibration, 6.0f);
    // The same change in mono, at two more rates, and in quiet treble, where the tape's
    // level-dependent top end would overshoot if the level followers started over.
    checkChange ("hot bus to clean deck, mono", 48000.0, 64, 0.25f, hotBus, 10.9f, cleanDeck, -9.1f, chordProgramme, 1);
    checkChange ("hot bus to clean deck", 88200.0, 256, 0.25f, hotBus, 10.9f, cleanDeck, -9.1f);
    checkChange ("hot bus to clean deck", 192000.0, 1024, 0.25f, hotBus, 10.9f, cleanDeck, -9.1f);
    checkChange ("low bias to high bias, quiet treble", 48000.0, 512, 0.02f, lowBias, -10.2f, highBias, 11.8f, trebleProgramme, 2, 2.0);
    checkChange ("high bias to low bias, quiet treble", 48000.0, 512, 0.02f, highBias, 11.8f, lowBias, -10.2f, trebleProgramme, 2, 2.0);
    // What does not burst is not a clean start.
    checkLeftAlone ("tape type alone", 48000.0, 64, hotBus, 6.0f, otherTape, 6.0f);
    checkLeftAlone ("EQ standard alone", 48000.0, 64, hotBus, 6.0f, otherEqStandard, 6.0f);
    checkLeftAlone ("signal path Repro to Input alone", 48000.0, 64, hotBus, 6.0f, inputPath, 6.0f);
    checkLeftAlone ("head width alone", 48000.0, 64, american, 3.0f, otherHeadWidth, 3.0f);
    checkLeftAlone ("Swiss, 15 to 3.75 IPS (the same speed there)", 48000.0, 64, swissAt15, 3.0f, sameSpeedOnSwiss, 3.0f);
    checkSplitPreset (true, 48000.0, 64);
    checkSplitPreset (false, 48000.0, 64);
    checkSplitWithEverythingLater();
    checkSettling();
    checkStoredOutputInsideTheWindow();
    checkAfterANonFiniteSample (std::numeric_limits<float>::quiet_NaN(), "a NaN sample, then a calibration switch");
    checkAfterANonFiniteSample (std::numeric_limits<float>::infinity(), "an infinite sample, then a calibration switch");
    checkNoThump();
    checkResetAndPrepare();
    checkChange ("hot bus to a thick setting", 48000.0, 512, 0.25f, hotBus, 10.9f, thick, 2.2f);
    checkChange ("low bias to high bias", 48000.0, 64, 0.25f, lowBias, -10.2f, highBias, 11.8f);
    checkChange ("low bias to high bias", 48000.0, 512, 0.25f, lowBias, -10.2f, highBias, 11.8f);
    // A preset on the same deck is not a preset-sized change: the EQ and the trims move
    // through their filters, Input through its smoother, and nothing has to be cleared.
    checkChange ("same deck, EQ set flat, Input 1 dB down", 48000.0, 64, 0.25f, hotBus, 10.9f, hotBusFlat, 9.9f);
    checkChange ("Input +12 to -12 dB alone", 48000.0, 64, 0.25f, swiss, 12.0f, noSetting, -12.0f);
    checkChange ("Input -12 to +12 dB alone", 48000.0, 512, 0.25f, swiss, -12.0f, noSetting, 12.0f);
    checkChange ("Input +9 to 0 dB alone, loud programme", 48000.0, 64, 0.7f, hotBus, 9.0f, noSetting, 0.0f);
    checkChange ("Input +3 to -9 dB alone, loud programme", 48000.0, 64, 0.7f, lowBias, 3.0f, noSetting, -9.0f);
    checkChange ("Input +2.7 to -9.3 dB alone, American master, loud programme", 48000.0, 64, 0.7f, americanMaster, 2.7f, noSetting, -9.3f);

    for (const bool thru : { false, true })
        for (const bool drop : { false, true })
            checkBackFromPassthrough (thru, drop, 48000.0, thru ? 512 : 64);
    checkBackFromPassthrough (false, true, 48000.0, 64, 0, trebleProgramme);
    // Other settings after the drop: the level followers must not carry the loud programme over.
    checkBackFromPassthrough (false, true, 48000.0, 512, 0, chordProgramme, thick, 2.2f);
    checkBackFromPassthrough (false, true, 48000.0, 512, 0, chordProgramme, lowBias, -10.2f);
    checkBackFromPassthrough (false, true, 48000.0, 512, 0, chordProgramme, highBias, 11.8f);
    checkBackFromPassthrough (false, true, 48000.0, 512, 0, chordProgramme, americanMaster, 2.7f);
    // The dry run-out has no net area wherever in the tone the exit falls.
    for (const int later : { 0, 1, 2 })
        checkBackFromPassthrough (false, false, 48000.0, 64, later, toneProgramme);
    checkBypassInQuietTreble (lowBias, -10.2f, "bypass for a second in quiet treble, low-bias setting");
    checkBypassInQuietTreble (thick, 2.2f, "bypass for a second in quiet treble, thick setting");
    // The way back is not a click wherever in the waveform it falls.
    for (const int later : { 1, 2, 3, 5, 8, 13 })
        checkBackFromPassthrough (false, false, 48000.0, 64, later);

    checkGestures();
    checkLinkOn (false);
    checkLinkOn (true);

    if (failures == 0)
        std::printf ("CleanChangeTest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
