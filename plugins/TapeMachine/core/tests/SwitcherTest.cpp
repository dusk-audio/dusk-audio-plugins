// SwitcherTest: TapeMachineSwitcher, the two paths and the morph between them.
//
// What it holds, in the order of the checks:
//   - Nothing switched: the switcher is one TapeMachineDSP, bit for bit, whatever else is set
//     (knobs every block, tape type, EQ standard, signal path, head width, bypass, Thru).
//   - A switch (the machine, the speed or the calibration; with Gain Link on, a drop of Input
//     beyond 6 dB):
//       before the fade the output is the old path's, to the bit, the block of the wait included;
//       during it, the old path and a path started afresh on the new settings at the fade's
//       first block, mixed at equal gains along a raised cosine;
//       after it, that new path alone, to the bit.
//     On a chord the level never falls under the quieter nor rises over the louder of the two
//     settings by more than a fraction of a dB, and nothing clicks. No sample in a fade is
//     other than a finite number.
//   - A preset that arrives over two to five blocks is one fade, to the whole preset, also
//     when a later part does nothing but change the sign of two settings.
//   - A second switch during a fade, of the deck or a drop of Input, waits for its end and is
//     the next fade, and the incoming path takes nothing of it. reset() during a fade ends it.
//   - What a drop of Input is counted from: the Input a kept path took (a switch taken back is
//     no second fade), not one it did not take (a linked path kept through a rise that is taken
//     back starts no fade), and, when the drop is itself the switch waited for, the Input before
//     it (unlinked to linked with Input 20 dB down is one fade).
//   - The latency reported while a path is kept on its settings is what that path does: in a
//     fade with the Thru path and a second switch asked for.
//   - Bypass or the Thru path switched off together with the deck: no fade, one path to the bit,
//     the latency a single path reports. In a fade, switched on and then off together with the
//     deck: the incoming path takes it, no second fade, the latency a single path reports.
//   - Input, Output and the link are never held back from a path that is unlinked, so a
//     preset whose level settings arrive blocks apart is off for as long as a single path
//     would be, not for the length of a wait and a fade: held against a single path fed the
//     same parts. A path that is linked is kept as it is.
//   - The deck switched on every block: through six other settings in turn, fades follow one
//     another and the programme stays; to another speed and back, nothing happens until it
//     stays.
//   - Input and output in the same memory, mono, a block larger than prepared for.
//   - The meters do not fall at the handover.
//   - Nothing is allocated in processBlock, fades included.
//
//   SwitcherTest            prints failures and a summary
//   SwitcherTest --print    every reading
#include "TapeMachineSwitcher.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <vector>
#if defined (_WIN32)
 #include <malloc.h>
#endif

namespace
{
std::atomic<long> allocations { 0 };
}
void* operator new (std::size_t size)
{
    allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
// The over-aligned forms count as well. (An allocation that went to malloc directly would not be seen.)
// The Windows C runtime has no aligned_alloc: its own pair there, each freed by its own.
namespace
{
void* allocateAligned (std::size_t alignment, std::size_t bytes) noexcept
{
   #if defined (_WIN32)
    return _aligned_malloc (bytes, alignment);
   #else
    return std::aligned_alloc (alignment, bytes);
   #endif
}
void freeAligned (void* p) noexcept
{
   #if defined (_WIN32)
    _aligned_free (p);
   #else
    std::free (p);
   #endif
}
}
void* operator new (std::size_t size, std::align_val_t alignment)
{
    allocations.fetch_add (1, std::memory_order_relaxed);
    const std::size_t a = static_cast<std::size_t> (alignment);
    if (void* p = allocateAligned (a, (std::max<std::size_t> (size, 1) + a - 1) / a * a)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size, std::align_val_t alignment) { return operator new (size, alignment); }
void operator delete (void* p, std::align_val_t) noexcept { freeAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { freeAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { freeAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { freeAligned (p); }

namespace
{
using duskaudio::TapeMachineDSP;
using duskaudio::TapeMachineSettings;
using duskaudio::TapeMachineSwitcher;
using Setting = std::function<void (TapeMachineSettings&)>;

int failures = 0;
bool printAll = false;

void hold (const std::string& what, bool ok, const std::string& reading)
{
    if (! ok) ++failures;
    if (! ok || printAll) std::printf ("%s %s: %s\n", ok ? "  ok" : "FAIL", what.c_str(), reading.c_str());
}
void holdAtMost (const std::string& what, const char* reading, double value, double bound)
{
    char text[200];
    std::snprintf (text, sizeof (text), "%s = %.3g (at most %.3g)", reading, value, bound);
    hold (what, value <= bound, text);
}
void holdAtLeast (const std::string& what, const char* reading, double value, double bound)
{
    char text[200];
    std::snprintf (text, sizeof (text), "%s = %.3g (at least %.3g)", reading, value, bound);
    hold (what, value >= bound, text);
}

const double kRate = 48000.0;

// The programme by sample position: a four-sine chord, the right channel lower and without
// the top partial.
void programme (std::size_t pos, float& left, float& right)
{
    static constexpr double freqs[4] = { 110.0, 440.0, 1200.0, 3300.0 };
    const double t = static_cast<double> (pos) / kRate;
    double all = 0.0, noTop = 0.0;
    for (int k = 0; k < 4; ++k)
    {
        const double v = std::sin (6.283185307179586 * freqs[k] * t);
        all += v;
        if (k < 3) noTop += v;
    }
    left  = static_cast<float> (0.25 * all / 4.0);
    right = static_cast<float> (0.20 * noTop / 3.0);
}

struct Render
{
    std::vector<float> left, right;
    explicit Render (std::size_t n = 0) : left (n), right (n) {}
};

// Runs blocks [first, last) of the programme through `engine`; `events (blockIndex)` before
// each block. The output lands at the blocks' own positions in `out`.
template <class Engine>
void run (Engine& engine, Render& out, int block, std::size_t first, std::size_t last,
          const std::function<void (std::size_t)>& events = {}, int channels = 2)
{
    const std::size_t n = static_cast<std::size_t> (block);
    std::vector<float> inL (n), inR (n);
    for (std::size_t b = first; b < last; ++b)
    {
        if (events) events (b);
        for (std::size_t i = 0; i < n; ++i) programme (b * n + i, inL[i], inR[i]);
        const float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { out.left.data() + b * n, out.right.data() + b * n };
        engine.processBlock (ins, outs, channels, block);
    }
}

std::size_t differing (const Render& a, const Render& b, std::size_t from, std::size_t to, bool rightToo = true)
{
    std::size_t n = 0;
    for (std::size_t i = from; i < to; ++i)
        n += std::memcmp (&a.left[i], &b.left[i], sizeof (float)) != 0
             || (rightToo && std::memcmp (&a.right[i], &b.right[i], sizeof (float)) != 0);
    return n;
}
double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }
double rmsDb (const Render& r, std::size_t from, std::size_t to)
{
    double acc = 0.0;
    for (std::size_t i = from; i < to; ++i) acc += static_cast<double> (r.left[i]) * r.left[i] + static_cast<double> (r.right[i]) * r.right[i];
    return toDb (std::sqrt (acc / static_cast<double> (std::max<std::size_t> (2 * (to - from), 1))));
}
double peakDb (const Render& r, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < to; ++i) p = std::max (p, std::max (std::abs (r.left[i]), std::abs (r.right[i])));
    return toDb (p);
}
double jumpDb (const Render& r, std::size_t from, std::size_t to)
{
    float j = 0.0f;
    for (std::size_t i = std::max<std::size_t> (from, 1); i < to; ++i) j = std::max (j, std::abs (r.left[i] - r.left[i - 1]));
    return toDb (j);
}

// --- settings -----------------------------------------------------------------------------
void swissDeck (TapeMachineSettings& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (1); d.setTapeType (0); d.setCalibration (1);
    d.setInputGainDb (3.0f); d.setWow (0.0f); d.setFlutter (0.0f);
}
void americanDeck (TapeMachineSettings& d)
{
    d.setTapeMachine (1); d.setTapeSpeed (1); d.setTapeType (0); d.setCalibration (1);
    d.setInputGainDb (3.0f); d.setWow (0.0f); d.setFlutter (0.0f); d.setWowFlutterEnabled (false);
}
// A preset-sized change in three parts, the way tmApplyPreset orders them: the deck, then
// Input and bias, then the filters and the reproduce EQ.
void presetDeck (TapeMachineSettings& d)   { d.setTapeMachine (1); d.setTapeSpeed (2); d.setTapeType (1); d.setCalibration (3); d.setHeadWidth (2); }
void presetLevel (TapeMachineSettings& d)  { d.setInputGainDb (-4.0f); d.setAutoCal (false); d.setBias (42.0f); }
void presetTone (TapeMachineSettings& d)   { d.setHighpassHz (40.0f); d.setLowpassHz (14000.0f); d.setReproLf (2.0f); d.setReproHf (-3.0f); d.setReproSubBell (6.0f); }

// Two settings whose Input and Output are 20 dB apart and cancel, linked and unlinked.
void hotUnlinked (TapeMachineSettings& d)   { swissDeck (d); d.setAutoComp (false); d.setInputGainDb (10.9f); d.setOutputGainDb (-10.9f); }
void quietUnlinked (TapeMachineSettings& d) { swissDeck (d); d.setAutoComp (false); d.setInputGainDb (-9.1f); d.setOutputGainDb (9.1f); }
void hotLinked (TapeMachineSettings& d)     { swissDeck (d); d.setInputGainDb (10.9f); d.setOutputGainDb (-10.9f); }
void quietLinked (TapeMachineSettings& d)   { swissDeck (d); d.setInputGainDb (-9.1f); d.setOutputGainDb (9.1f); }

const std::size_t kSwitchBlockAt = 750;     // the block a switch arrives in, at block 64: 1 s

// --- nothing switched ---------------------------------------------------------------------
void checkOnePath (const char* name, const Setting& setup, const std::function<void (TapeMachineSettings&, std::size_t)>& events, int block = 64, int channels = 2)
{
    const std::size_t blocks = static_cast<std::size_t> (kRate * 1.5) / static_cast<std::size_t> (block);
    TapeMachineSwitcher switcher;
    TapeMachineDSP single;
    setup (switcher); setup (single);
    switcher.prepare (kRate, block);
    single.prepare (kRate, block);
    Render ofSwitcher (blocks * static_cast<std::size_t> (block)), ofSingle (ofSwitcher.left.size());
    run (switcher, ofSwitcher, block, 0, blocks, [&] (std::size_t b) { events (switcher, b); }, channels);
    run (single, ofSingle, block, 0, blocks, [&] (std::size_t b) { events (single, b); }, channels);
    hold (name, peakDb (ofSingle, 0, ofSingle.left.size()) > -60.0, "the single path's render is not silent");
    hold (name, differing (ofSwitcher, ofSingle, 0, ofSingle.left.size(), channels == 2) == 0,
          std::to_string (differing (ofSwitcher, ofSingle, 0, ofSingle.left.size(), channels == 2)) + " samples differ from one TapeMachineDSP (must be 0)");
    hold (name, switcher.fadesStarted() == 0, std::to_string (switcher.fadesStarted()) + " fades started (must be 0)");
    hold (name, switcher.latencySamples() == single.latencySamples() && single.latencySamples() > 0, "the reported latency is the single path's");
    // And while the input is passed through: none, as the single path reports it.
    switcher.setBypass (true); single.setBypass (true);
    hold (name, switcher.latencySamples() == 0 && single.latencySamples() == 0, "no latency is reported in bypass");
    switcher.setBypass (false); single.setBypass (false);
    switcher.setSignalPath (3); single.setSignalPath (3);
    hold (name, switcher.latencySamples() == 0 && single.latencySamples() == 0, "no latency is reported on the Thru path");
}

// --- a switch -----------------------------------------------------------------------------
// `parts[k]` is set in block kSwitchBlockAt + k. The fade is expected to start `wait` blocks
// after the first part, on everything set by then.
struct Switched
{
    Render ofSwitcher, ofOld, ofNew, ofNewFromTheStart;
    std::size_t fadeFirst = 0, fadeEnd = 0;     // samples
    int fades = 0;
};
Switched checkSwitch (const char* name, const Setting& before, const std::vector<Setting>& parts, int wait, int block = 64, bool readings = false)
{
    const std::size_t n = static_cast<std::size_t> (block);
    const std::size_t at = kSwitchBlockAt * 64 / n;
    const std::size_t blocks = at + static_cast<std::size_t> (kRate * 1.0) / n;
    const std::size_t total = blocks * n;
    const std::size_t fadeBlock = at + static_cast<std::size_t> (wait);
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const std::string what = std::string (name) + ", block " + std::to_string (block);

    Switched s;
    s.ofSwitcher = Render (total); s.ofOld = Render (total); s.ofNew = Render (total); s.ofNewFromTheStart = Render (total);
    s.fadeFirst = fadeBlock * n; s.fadeEnd = s.fadeFirst + fadeLen;

    const auto partsUpTo = [&] (TapeMachineSettings& d, std::size_t count) { for (std::size_t k = 0; k < std::min (count, parts.size()); ++k) parts[k] (d); };

    TapeMachineSwitcher switcher;
    before (switcher);
    switcher.prepare (kRate, block);
    run (switcher, s.ofSwitcher, block, 0, blocks, [&] (std::size_t b) { if (b >= at && b - at < parts.size()) parts[b - at] (switcher); });
    s.fades = switcher.fadesStarted();

    TapeMachineDSP old;
    before (old);
    old.prepare (kRate, block);
    run (old, s.ofOld, block, 0, blocks);

    // The new path as the switcher starts it: everything set up to the fade's first block,
    // prepared there, and from then on whatever is set later.
    TapeMachineDSP fresh;
    before (fresh);
    partsUpTo (fresh, static_cast<std::size_t> (wait) + 1);
    fresh.prepare (kRate, block);
    run (fresh, s.ofNew, block, fadeBlock, blocks, [&] (std::size_t b) { if (b > fadeBlock && b - at < parts.size()) parts[b - at] (fresh); });

    TapeMachineDSP twin;
    before (twin);
    partsUpTo (twin, parts.size());
    twin.prepare (kRate, block);
    run (twin, s.ofNewFromTheStart, block, 0, blocks);

    hold (what, s.fades == 1, std::to_string (s.fades) + " fades started (must be 1)");
    hold (what, differing (s.ofSwitcher, s.ofOld, 0, s.fadeFirst) == 0,
          std::to_string (differing (s.ofSwitcher, s.ofOld, 0, s.fadeFirst)) + " samples before the fade differ from the old path (must be 0)");
    hold (what, differing (s.ofSwitcher, s.ofNew, s.fadeEnd, total) == 0,
          std::to_string (differing (s.ofSwitcher, s.ofNew, s.fadeEnd, total)) + " samples after the fade differ from a path started afresh at its first block (must be 0)");
    double worst = 0.0, moved = 0.0;
    std::size_t notNumbers = 0;     // a distance that is not a number would not show in `worst`
    for (std::size_t i = s.fadeFirst; i < s.fadeEnd; ++i)
    {
        const float t = static_cast<float> (i - s.fadeFirst) / static_cast<float> (fadeLen);
        const float g = 0.5f - 0.5f * std::cos (3.14159265358979f * t);
        const double offLeft  = std::abs (static_cast<double> (s.ofSwitcher.left[i]) - (s.ofOld.left[i] + g * (s.ofNew.left[i] - s.ofOld.left[i])));
        const double offRight = std::abs (static_cast<double> (s.ofSwitcher.right[i]) - (s.ofOld.right[i] + g * (s.ofNew.right[i] - s.ofOld.right[i])));
        notNumbers += ! std::isfinite (offLeft) || ! std::isfinite (offRight);
        worst = std::max (worst, std::max (offLeft, offRight));
        moved = std::max (moved, std::abs (static_cast<double> (s.ofNew.left[i]) - s.ofOld.left[i]));
    }
    hold (what, notNumbers == 0, std::to_string (notNumbers) + " samples in the fade are not finite, in the switcher or in a path (must be 0)");
    holdAtMost (what, "largest distance, in the fade, from the two paths mixed at equal gains along a raised cosine", worst, 1.0e-6);
    holdAtLeast (what, "largest distance between the two paths in the fade (they must differ, or the mix holds nothing)", moved, 1.0e-3);

    if (readings)
    {
        // Against the two settings held from the start: `ofOld` and `ofNewFromTheStart`.
        const std::size_t latency = 62, zero = s.fadeFirst + latency, ms = static_cast<std::size_t> (kRate / 1000.0);
        double dip = 0.0, bump = 0.0;
        for (std::size_t m = 0; m < 300; ++m)
        {
            const std::size_t a = zero + m * ms, b = a + 5 * ms;
            const double now = rmsDb (s.ofSwitcher, a, b), o = rmsDb (s.ofOld, a, b), nw = rmsDb (s.ofNewFromTheStart, a, b);
            dip = std::min (dip, now - std::min (o, nw));
            bump = std::max (bump, now - std::max (o, nw));
        }
        holdAtLeast (what, "lowest 5 ms under the quieter of the two settings, dB", dip, -0.5);
        holdAtMost (what, "highest 5 ms over the louder of the two settings, dB", bump, 0.2);
        holdAtMost (what, "peak in the 200 ms after the fade's start over the louder of before and settled, dB",
                    peakDb (s.ofSwitcher, zero, zero + 200 * ms) - std::max (peakDb (s.ofSwitcher, zero - 250 * ms, zero), peakDb (s.ofSwitcher, zero + 750 * ms, zero + 900 * ms)), 0.2);
        holdAtMost (what, "largest sample jump in the 200 ms after the fade's start over the largest before and settled, dB",
                    jumpDb (s.ofSwitcher, zero, zero + 200 * ms) - std::max (jumpDb (s.ofSwitcher, zero - 250 * ms, zero), jumpDb (s.ofSwitcher, zero + 750 * ms, zero + 900 * ms)), 0.5);
    }
    return s;
}

// --- a second switch during a fade ---------------------------------------------------------
// Ten blocks into a fade to another calibration: the machine, the speed and the reproduce EQ,
// together. The incoming path takes none of it, the reproduce EQ included.
void checkSecondSwitch()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, second = at + 10, blocks = at + 750, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const std::size_t firstFade = (at + 1) * n, firstEnd = firstFade + fadeLen;
    const char* what = "a second switch ten blocks into a fade";

    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofFirst (total), ofSecond (total);
    int fadesAtFirstEnd = -1;
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) switcher.setCalibration (3);
        if (b == second) { switcher.setTapeMachine (1); switcher.setTapeSpeed (2); switcher.setReproHf (4.0f); }
        if (b * n >= firstEnd && fadesAtFirstEnd < 0) fadesAtFirstEnd = switcher.fadesStarted();
    });
    TapeMachineDSP first;       // the first fade's incoming path, as it was started
    swissDeck (first); first.setCalibration (3);
    first.prepare (kRate, block);
    run (first, ofFirst, block, at + 1, blocks);
    // The second fade starts in the block after the first one's end, one block of waiting later.
    const std::size_t firstEndBlock = (firstEnd + n - 1) / n;
    const std::size_t secondFadeBlock = firstEndBlock + 1;
    TapeMachineDSP last;
    swissDeck (last); last.setCalibration (3); last.setTapeMachine (1); last.setTapeSpeed (2); last.setReproHf (4.0f);
    last.prepare (kRate, block);
    run (last, ofSecond, block, secondFadeBlock, blocks);

    hold (what, fadesAtFirstEnd == 1, std::to_string (fadesAtFirstEnd) + " fades started by the end of the first (must be 1)");
    hold (what, switcher.fadesStarted() == 2, std::to_string (switcher.fadesStarted()) + " fades started in all (must be 2)");
    hold (what, differing (out, ofFirst, firstEnd, secondFadeBlock * n) == 0,
          std::to_string (differing (out, ofFirst, firstEnd, secondFadeBlock * n)) + " samples between the fades differ from the first fade's path as it was started (must be 0)");
    hold (what, differing (out, ofSecond, secondFadeBlock * n + fadeLen, total) == 0,
          std::to_string (differing (out, ofSecond, secondFadeBlock * n + fadeLen, total)) + " samples after the second fade differ from a path started afresh there (must be 0)");
}

// A knob moved during a fade, with no second switch: the incoming path takes it, and the
// levels it is compared with from then on are the ones it was given. Input goes down by 4 dB
// ten blocks into a fade and by another 4 dB fifteen blocks later, still in it: two knob
// moves, one fade (8 dB from where the fade started would be a switch).
void checkKnobsInAFade()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 750, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const char* what = "Input down by 4 dB twice in a fade";
    const auto events = [&] (TapeMachineSettings& d, std::size_t b) {
        if (b == at + 10) { d.setInputGainDb (-1.0f); d.setReproHf (4.0f); }
        if (b == at + 25) d.setInputGainDb (-5.0f);
    };
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofNew (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) { if (b == at) switcher.setCalibration (3); events (switcher, b); });
    TapeMachineDSP fresh;
    swissDeck (fresh); fresh.setCalibration (3);
    fresh.prepare (kRate, block);
    run (fresh, ofNew, block, at + 1, blocks, [&] (std::size_t b) { events (fresh, b); });
    hold (what, switcher.fadesStarted() == 1, std::to_string (switcher.fadesStarted()) + " fades started (must be 1)");
    hold (what, differing (out, ofNew, (at + 1) * n + fadeLen, total) == 0,
          std::to_string (differing (out, ofNew, (at + 1) * n + fadeLen, total)) + " samples after the fade differ from the incoming path given the same knob moves (must be 0)");
}

// A level given to the incoming path while a second switch is asked for is the level that
// path is on from then. Unlinked, ten blocks into a fade: another speed, and Input 20 dB
// lower. A block later the speed is taken back and Gain Link turned on. Nothing is left to
// switch then: the incoming path has that Input already, and the link goes to it as any
// setting does. (Counted from the Input the fade started on, it would be a drop of 20 dB
// with the link on, and a second fade.)
void checkSwitchTakenBack()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, second = at + 10, blocks = at + 750, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const char* what = "unlinked, a second switch with Input 20 dB down in a fade, taken back a block later with the link on";
    TapeMachineSwitcher switcher;
    hotUnlinked (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofNew (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) switcher.setCalibration (3);
        if (b == second) { switcher.setTapeSpeed (2); switcher.setInputGainDb (-9.1f); }
        if (b == second + 1) { switcher.setTapeSpeed (1); switcher.setAutoComp (true); }
    });
    TapeMachineDSP fresh;       // the fade's incoming path: Input in the one block, the link in the next
    hotUnlinked (fresh); fresh.setCalibration (3);
    fresh.prepare (kRate, block);
    run (fresh, ofNew, block, at + 1, blocks, [&] (std::size_t b) {
        if (b == second) fresh.setInputGainDb (-9.1f);
        if (b == second + 1) fresh.setAutoComp (true);
    });
    hold (what, switcher.fadesStarted() == 1, std::to_string (switcher.fadesStarted()) + " fades started (must be 1)");
    hold (what, differing (out, ofNew, (at + 1) * n + fadeLen, total) == 0,
          std::to_string (differing (out, ofNew, (at + 1) * n + fadeLen, total)) + " samples after the fade differ from the incoming path given that Input and then the link (must be 0)");
}

// --- what later drops of Input are counted from -----------------------------------------------
// From an unlinked setting to the link on with Input 20 dB lower, nothing else changed, in one
// block: the drop is the switch. The kept path takes the Input with the link (its own clean
// start) and the switch is still there to wait for: one fade, to a path started afresh on the
// new settings at the fade's first block. (Counted from the Input the kept path took, there
// would be no switch left, and no fade.)
void checkUnlinkedToLinkedDrop()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 750, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const char* what = "unlinked to the link on with Input 20 dB down, in one block";
    TapeMachineSwitcher switcher;
    hotUnlinked (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofNew (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) { if (b == at) { switcher.setAutoComp (true); switcher.setInputGainDb (-9.1f); } });
    TapeMachineDSP fresh;       // the fade's incoming path, started a block after the change
    hotUnlinked (fresh); fresh.setAutoComp (true); fresh.setInputGainDb (-9.1f);
    fresh.prepare (kRate, block);
    run (fresh, ofNew, block, at + 1, blocks);
    hold (what, switcher.fadesStarted() == 1, std::to_string (switcher.fadesStarted()) + " fades started (must be 1)");
    hold (what, differing (out, ofNew, (at + 1) * n + fadeLen, total) == 0,
          std::to_string (differing (out, ofNew, (at + 1) * n + fadeLen, total)) + " samples after the fade differ from a path started afresh at its first block (must be 0)");
}

// A linked path kept for a switch takes no Input, so a new Input asked for meanwhile is not
// what it is on. Linked, another speed and Input 10 dB higher in one block, both taken back in
// the next: nothing is left to switch, no fade, and the output is that of a path that was
// never touched. (Counted from the Input that was asked for, the way back would be a drop of
// 10 dB with the link on, and a fade.)
void checkLinkedRiseTakenBack()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 750, total = blocks * n;
    const char* what = "linked, another speed with Input 10 dB up, both taken back a block later";
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofSingle (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) { switcher.setTapeSpeed (2); switcher.setInputGainDb (13.0f); }
        if (b == at + 1) { switcher.setTapeSpeed (1); switcher.setInputGainDb (3.0f); }
    });
    TapeMachineDSP single;
    swissDeck (single);
    single.prepare (kRate, block);
    run (single, ofSingle, block, 0, blocks);
    hold (what, switcher.fadesStarted() == 0, std::to_string (switcher.fadesStarted()) + " fades started (must be 0)");
    hold (what, differing (out, ofSingle, 0, total) == 0,
          std::to_string (differing (out, ofSingle, 0, total)) + " samples differ from a path that was never touched (must be 0)");
}

// --- the latency that is reported while a path is kept on its settings ------------------------
// The Thru path asked for ten blocks into a fade. Alone, it reaches the incoming path in that
// block and no latency is reported from then on, as a single path does. Together with a
// second switch the incoming path keeps what it has, and the output stays the two processing
// paths', delayed, until the fade is over: the latency reported must be a path's until then.
void checkLatencyInAFade (bool withSecondSwitch)
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, second = at + 10, blocks = at + 300, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const std::size_t firstOver = ((at + 1) * n + fadeLen + n - 1) / n;     // the first block after the fade
    const std::string what = std::string ("the Thru path asked for in a fade, ") + (withSecondSwitch ? "with a second switch" : "alone");
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), dry (total);
    for (std::size_t i = 0; i < total; ++i) programme (i, dry.left[i], dry.right[i]);
    int before = -1, inTheFade = -1, atItsEnd = -1, after = -1;
    bool fadingThen = false, fadingAtItsEnd = false, fadingAfter = true;
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) switcher.setCalibration (3);
        if (b == second) { before = switcher.latencySamples(); switcher.setSignalPath (3); if (withSecondSwitch) switcher.setTapeSpeed (2); }
        if (b == second + 1) { inTheFade = switcher.latencySamples(); fadingThen = switcher.isFading(); }
        if (b == firstOver - 1) { atItsEnd = switcher.latencySamples(); fadingAtItsEnd = switcher.isFading(); }
        if (b == firstOver + 1) { after = switcher.latencySamples(); fadingAfter = switcher.isFading(); }
    });
    hold (what, before > 0 && fadingThen && fadingAtItsEnd && ! fadingAfter,
          "a latency of " + std::to_string (before) + " samples before (must be more than 0); a fade is running in the block after and in its last block, none two blocks later");
    hold (what, inTheFade == (withSecondSwitch ? before : 0) && atItsEnd == (withSecondSwitch ? before : 0),
          "latency reported in the fade: " + std::to_string (inTheFade) + " after the block that asked, " + std::to_string (atItsEnd) + " in its last block (must be "
          + (withSecondSwitch ? "the path's, " + std::to_string (before) + ": the output is still delayed)" : "0: the incoming path passes the input through)"));
    hold (what, after == 0, "latency reported once the fade is over: " + std::to_string (after) + " (must be 0)");
    hold (what, differing (out, dry, firstOver * n, total) == 0,
          std::to_string (differing (out, dry, firstOver * n, total)) + " samples after the fade differ from the input (must be 0)");
    if (withSecondSwitch)
        hold (what, differing (out, dry, (second + 1) * n, (second + 2) * n) == n,
              std::to_string (differing (out, dry, (second + 1) * n, (second + 2) * n)) + " of " + std::to_string (n) + " samples differ from the input in the block after it was asked for (must be all: nothing is passed through yet)");
}

// Bypass or the Thru path switched off in the same block as a switch of the deck (a preset
// loaded on the Thru path sets the path and the deck together): nothing to morph. The path
// takes it all and starts clean on its way back, as a single path does: the output is that
// of one TapeMachineDSP given the same events, to the bit, with no fade, and the latency
// reported after every block is that path's. (Kept for a fade instead, the running path would
// go on passing the input through, and the fade would be from the dry input to the new path,
// 62 samples later: a comb, about -20 dB at 1 kHz.)
void checkPassthroughEndsWithASwitch (bool thru, int block)
{
    const std::size_t n = static_cast<std::size_t> (block), at = 19200 / n, blocks = at + 24000 / n, total = blocks * n;
    const std::string what = std::string (thru ? "the Thru path switched off together with the machine and the speed"
                                               : "bypass switched off together with the calibration") + ", block " + std::to_string (block);
    const auto events = [thru, at] (TapeMachineSettings& d, std::size_t b) {
        if (b != at) return;
        if (thru) { d.setSignalPath (0); d.setTapeMachine (1); d.setTapeSpeed (2); }
        else      { d.setBypass (false); d.setCalibration (3); }
    };
    const auto before = [thru] (TapeMachineSettings& d) { swissDeck (d); if (thru) d.setSignalPath (3); else d.setBypass (true); };
    TapeMachineSwitcher switcher;
    TapeMachineDSP single;
    before (switcher); before (single);
    switcher.prepare (kRate, block);
    single.prepare (kRate, block);
    Render ofSwitcher (total), ofSingle (total);
    std::vector<int> latSwitcher, latSingle;
    run (switcher, ofSwitcher, block, 0, blocks, [&] (std::size_t b) { latSwitcher.push_back (switcher.latencySamples()); events (switcher, b); });
    run (single, ofSingle, block, 0, blocks, [&] (std::size_t b) { latSingle.push_back (single.latencySamples()); events (single, b); });
    latSwitcher.push_back (switcher.latencySamples()); latSingle.push_back (single.latencySamples());
    std::size_t latencyOff = 0;
    for (std::size_t b = 0; b < latSingle.size(); ++b) latencyOff += latSwitcher[b] != latSingle[b];
    hold (what, peakDb (ofSingle, at * n, total) > -60.0 && latSingle.front() == 0 && latSingle.back() > 0,
          "the single path is passed through before, processes after, and is not silent");
    hold (what, switcher.fadesStarted() == 0, std::to_string (switcher.fadesStarted()) + " fades started (must be 0)");
    hold (what, differing (ofSwitcher, ofSingle, 0, total) == 0,
          std::to_string (differing (ofSwitcher, ofSingle, 0, total)) + " samples differ from one TapeMachineDSP given the same events (must be 0)");
    hold (what, latencyOff == 0, std::to_string (latencyOff) + " blocks after which the latency reported is not the single path's (must be 0)");
}

// The same in a fade: bypass or the Thru path switched on ten blocks into a fade to another
// calibration (it reaches the incoming path), and off again five blocks later together with
// another speed or machine. The incoming path is passing the input through then, and takes
// that switch as a single path would; nothing is kept for a second fade. One fade; after it
// the output is that of a path started at the fade's first block and given the same events,
// to the bit; the latency reported after every block is that of a single path given them.
// (Kept instead, the incoming path would go on passing the input through for the rest of
// the fade, against the delayed outgoing path, with no latency reported.)
void checkPassthroughEndsInAFade (bool thru)
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, on = at + 10, off = at + 15, blocks = at + 300, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const std::string what = std::string (thru ? "the Thru path on in a fade, off five blocks later with the machine"
                                               : "bypass on in a fade, off five blocks later with the speed");
    const auto events = [=] (TapeMachineSettings& d, std::size_t b) {
        if (b == on)  { if (thru) d.setSignalPath (3); else d.setBypass (true); }
        if (b == off) { if (thru) { d.setSignalPath (0); d.setTapeMachine (1); } else { d.setBypass (false); d.setTapeSpeed (2); } }
    };
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    TapeMachineDSP single;      // for the latency: the same events, one path
    swissDeck (single);
    single.prepare (kRate, block);
    Render out (total), ofSingle (total), ofNew (total);
    std::vector<int> latSwitcher, latSingle;
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        latSwitcher.push_back (switcher.latencySamples());
        if (b == at) switcher.setCalibration (3);
        events (switcher, b);
    });
    run (single, ofSingle, block, 0, blocks, [&] (std::size_t b) {
        latSingle.push_back (single.latencySamples());
        if (b == at) single.setCalibration (3);
        events (single, b);
    });
    latSwitcher.push_back (switcher.latencySamples()); latSingle.push_back (single.latencySamples());
    TapeMachineDSP fresh;       // the fade's incoming path, as it was started, given the events
    swissDeck (fresh); fresh.setCalibration (3);
    fresh.prepare (kRate, block);
    run (fresh, ofNew, block, at + 1, blocks, [&] (std::size_t b) { events (fresh, b); });
    std::size_t latencyOff = 0;
    for (std::size_t b = 0; b < latSingle.size(); ++b) latencyOff += latSwitcher[b] != latSingle[b];
    const std::size_t fadeEnd = (at + 1) * n + fadeLen;
    hold (what, switcher.fadesStarted() == 1, std::to_string (switcher.fadesStarted()) + " fades started (must be 1)");
    hold (what, differing (out, ofNew, fadeEnd, total) == 0,
          std::to_string (differing (out, ofNew, fadeEnd, total)) + " samples after the fade differ from the incoming path given the same events (must be 0)");
    hold (what, latencyOff == 0, std::to_string (latencyOff) + " blocks after which the latency reported is not the single path's (must be 0)");
}

// After a preset that used up the waits, a switch that arrives during its fade waits one
// block after the fade's end, like any other: the count starts over with each fade.
void checkWaitsStartOver()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, second = at + 3 + 10, blocks = at + 750, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const std::size_t firstEnd = (at + 3) * n + fadeLen, secondFadeBlock = (firstEnd + n - 1) / n + 1;
    const char* what = "a switch during the fade of a preset that came over four blocks";
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofLast (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) presetDeck (switcher);
        if (b == at + 1) presetLevel (switcher);
        if (b == at + 2) presetTone (switcher);
        if (b == at + 3) switcher.setReproLmf (1.0f);
        if (b == second) switcher.setTapeSpeed (0);
    });
    TapeMachineDSP last;
    swissDeck (last); presetDeck (last); presetLevel (last); presetTone (last); last.setReproLmf (1.0f); last.setTapeSpeed (0);
    last.prepare (kRate, block);
    run (last, ofLast, block, secondFadeBlock, blocks);
    hold (what, switcher.fadesStarted() == 2, std::to_string (switcher.fadesStarted()) + " fades started (must be 2)");
    hold (what, differing (out, ofLast, secondFadeBlock * n + fadeLen, total) == 0,
          std::to_string (differing (out, ofLast, secondFadeBlock * n + fadeLen, total)) + " samples after the second fade differ from a path started afresh one block after the first fade's end (must be 0)");
}

// --- Gain Link off --------------------------------------------------------------------------
// Two settings whose Input and Output are 20 dB apart and cancel: unlinked, a path with the
// Input of the one and the Output of the other is 20 dB off. A preset's parts are set blocks
// apart, and the switcher is read against one TapeMachineDSP given the same parts at the same
// blocks. The single path is off for as long as the parts are apart; the switcher must not be
// off by more, or for longer.
// Where it is, within the bounds below: when a later part switches the deck, a single path
// ends what is left of a mismatch at once, by its clean start, and a kept path here over the
// gain smoother's time. That costs up to 4.3 dB of peak for a tenth of a millisecond (three
// switches of the deck with the levels spread between them, 16-sample blocks; 2.2 dB in the
// two-part deliveries), and from a hot setting to a quiet one the level stays low for some
// milliseconds longer: up to 7.5 dB under the single path's lowest 10 ms. The bounds are
// there to catch what keeping a path on half a pair costs: 15 to 18 dB for 16 to 57 ms.
// Known to go further, and not among the deliveries here: a switch of the deck with Input
// 20 dB down, and Input back one to four blocks later (hot, block 16 or 64) reads up to
// 9.9 dB under the single path's lowest 10 ms. The single path snaps that second Input
// inside the settling window its own clean start for the deck opened; the kept path, which
// is not given the deck, takes it through its gain smoother, and the output is the kept
// path's for most of that time. Removing it needs a change to the path, not to the switcher.
struct Delivery
{
    const char* name;
    Setting before;
    std::vector<std::pair<std::size_t, Setting>> parts;     // blocks after the first part, and what is set there
};
struct Levels { double burst = 0.0, overMs = 0.0, hole = 0.0, shortHole = 0.0; int fades = 0; };

// Peak over the louder of before and settled; time more than 3 dB over that; lowest 10 ms
// under the quieter of before and settled, and the lowest 2 ms (a cut of 2 ms hardly shows
// in 10).
template <class Engine>
Levels deliver (const Delivery& d, int block)
{
    const std::size_t n = static_cast<std::size_t> (block), at = static_cast<std::size_t> (0.5 * kRate) / n, blocks = at + static_cast<std::size_t> (1.3 * kRate) / n;
    Engine engine;
    d.before (engine);
    engine.prepare (kRate, block);
    Render out (blocks * n);
    run (engine, out, block, 0, blocks, [&] (std::size_t b) { for (const auto& part : d.parts) if (b == at + part.first) part.second (engine); });
    const std::size_t z = at * n, ms = static_cast<std::size_t> (kRate / 1000.0);
    const double loud = std::max (peakDb (out, z - 250 * ms, z), peakDb (out, z + 1000 * ms, z + 1250 * ms));
    const double quiet = std::min (rmsDb (out, z - 250 * ms, z), rmsDb (out, z + 1000 * ms, z + 1250 * ms));
    const float limit = static_cast<float> (std::pow (10.0, (loud + 3.0) / 20.0));
    Levels l;
    l.burst = peakDb (out, z, z + 750 * ms) - loud;
    std::size_t over = 0;
    for (std::size_t i = z; i < z + 750 * ms; ++i) over += std::max (std::abs (out.left[i]), std::abs (out.right[i])) > limit;
    l.overMs = 1000.0 * static_cast<double> (over) / kRate;
    for (std::size_t a = z; a + 10 * ms <= z + 750 * ms; a += ms) l.hole = std::min (l.hole, rmsDb (out, a, a + 10 * ms) - quiet);
    for (std::size_t a = z; a + 2 * ms <= z + 750 * ms; a += ms / 4) l.shortHole = std::min (l.shortHole, rmsDb (out, a, a + 2 * ms) - quiet);
    return l;
}
int fadesOf (const Delivery& d, int block)
{
    const std::size_t n = static_cast<std::size_t> (block), at = static_cast<std::size_t> (0.5 * kRate) / n, blocks = at + static_cast<std::size_t> (1.3 * kRate) / n;
    TapeMachineSwitcher switcher;
    d.before (switcher);
    switcher.prepare (kRate, block);
    Render out (blocks * n);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) { for (const auto& part : d.parts) if (b == at + part.first) part.second (switcher); });
    return switcher.fadesStarted();
}

void checkUnlinked()
{
    const Setting calibration = [] (TapeMachineSettings& d) { d.setCalibration (3); };
    const auto input  = [] (float db) { return Setting ([db] (TapeMachineSettings& d) { d.setInputGainDb (db); }); };
    const auto output = [] (float db) { return Setting ([db] (TapeMachineSettings& d) { d.setOutputGainDb (db); }); };
    const auto both   = [] (Setting a, Setting b) { return Setting ([a, b] (TapeMachineSettings& d) { a (d); b (d); }); };
    const Setting linkOff = [] (TapeMachineSettings& d) { d.setAutoComp (false); };
    const Setting linkOn  = [] (TapeMachineSettings& d) { d.setAutoComp (true); };
    const Setting speed   = [] (TapeMachineSettings& d) { d.setTapeSpeed (2); };
    const Setting machine = [] (TapeMachineSettings& d) { d.setTapeMachine (1); };
    const Setting secondPreset = [] (TapeMachineSettings& d) { d.setTapeSpeed (2); d.setInputGainDb (-9.1f); d.setOutputGainDb (9.1f); };
    std::vector<Delivery> deliveries;
    for (const std::size_t gap : { std::size_t { 1 }, std::size_t { 2 }, std::size_t { 3 } })
    {
        deliveries.push_back ({ "Input, then calibration and Output", quietUnlinked, { { 0, input (10.9f) }, { gap, both (calibration, output (-10.9f)) } } });
        deliveries.push_back ({ "Output, then calibration and Input", hotUnlinked, { { 0, output (9.1f) }, { gap, both (calibration, input (-9.1f)) } } });
        deliveries.push_back ({ "Input, then Output, the deck unchanged", quietUnlinked, { { 0, input (10.9f) }, { gap, output (-10.9f) } } });
        deliveries.push_back ({ "calibration, then Input, then Output", quietUnlinked, { { 0, calibration }, { gap, input (10.9f) }, { 2 * gap, output (-10.9f) } } });
        deliveries.push_back ({ "hot to quiet: Input, then calibration and Output", hotUnlinked, { { 0, input (-9.1f) }, { gap, both (calibration, output (9.1f)) } } });
    }
    // The fade starts on half a pair, and the other half arrives while it runs: both paths
    // must take it, the incoming one also while a second switch is asked for.
    deliveries.push_back ({ "calibration and Input, then Output in the fade", quietUnlinked, { { 0, both (calibration, input (10.9f)) }, { 4, output (-10.9f) } } });
    deliveries.push_back ({ "calibration and Input, then the speed, then Output, all in one fade", quietUnlinked,
                            { { 0, both (calibration, input (10.9f)) }, { 3, [] (TapeMachineSettings& d) { d.setTapeSpeed (2); } }, { 5, output (-10.9f) } } });
    deliveries.push_back ({ "linked hot to unlinked quiet: Output, link off, Input", hotLinked, { { 0, output (9.1f) }, { 1, linkOff }, { 2, input (-9.1f) } } });
    deliveries.push_back ({ "linked quiet to unlinked hot: Input, link off, Output", quietLinked, { { 0, input (10.9f) }, { 1, linkOff }, { 2, output (-10.9f) } } });
    // To a linked preset, in the order a preset's values come in (deck, Input, Output, link),
    // split between them: the kept path must not stay unlinked on the new Input.
    deliveries.push_back ({ "unlinked quiet to linked hot: calibration and Input, then Output and link on", quietUnlinked,
                            { { 0, both (calibration, input (10.9f)) }, { 1, both (output (-10.9f), linkOn) } } });
    // The same with the stored Output a linked preset really has: a trim near zero, not the
    // inverse of its Input. Unlinked on that pair the path would be 11 dB loud.
    deliveries.push_back ({ "unlinked quiet to linked hot with a stored Output of 0 dB: calibration and Input, then Output and link on", quietUnlinked,
                            { { 0, both (calibration, input (10.9f)) }, { 1, both (output (0.0f), linkOn) } } });
    deliveries.push_back ({ "unlinked quiet to linked hot: calibration, Input and Output, then link on", quietUnlinked,
                            { { 0, both (calibration, both (input (10.9f), output (-10.9f))) }, { 1, linkOn } } });
    deliveries.push_back ({ "unlinked hot to linked quiet: calibration and Input, then Output and link on", hotUnlinked,
                            { { 0, both (calibration, input (-9.1f)) }, { 1, both (output (9.1f), linkOn) } } });
    deliveries.push_back ({ "unlinked quiet: calibration; in its fade speed and Input, then Output and link on", quietUnlinked,
                            { { 0, calibration }, { 10, both (speed, input (10.9f)) }, { 11, both (output (-10.9f), linkOn) } } });
    deliveries.push_back ({ "unlinked hot: calibration; in its fade speed and Input, then Output and link on", hotUnlinked,
                            { { 0, calibration }, { 10, both (speed, input (-9.1f)) }, { 11, both (output (9.1f), linkOn) } } });
    // An unlinked setting whose Input and Output do not cancel, and nothing but the
    // calibration switched: the kept path stays unlinked on its own Output. (Made linked by
    // mistake it would play 12 dB louder, or 6 dB quieter, while it waits and fades out.)
    for (const float out : { -12.0f, -6.0f, 6.0f })
    {
        static char names[3][64];
        const int k = out < -9.0f ? 0 : out < 0.0f ? 1 : 2;
        std::snprintf (names[k], sizeof (names[k]), "unlinked Input 0 and Output %+.0f dB: the calibration alone", static_cast<double> (out));
        deliveries.push_back ({ names[k], [out] (TapeMachineSettings& d) { swissDeck (d); d.setAutoComp (false); d.setInputGainDb (0.0f); d.setOutputGainDb (out); },
                                { { 0, calibration } } });
    }
    // A whole preset in one block, from an unlinked setting to a linked one whose Input is
    // more than 6 dB lower. The morph does not avoid the cut here: the kept path takes the
    // new Input with the link, and the drop is its own clean start, the cut of 2 ms that a
    // single path makes; the fade to the new path follows. (Leaving the kept path alone would
    // avoid the cut, and would put the path back on half a pair when the same preset comes
    // in two parts.) Held as what it is: the lowest 2 ms is the single path's. Without the
    // cut it would be about 9 to 13 dB higher.
    deliveries.push_back ({ "unlinked hot to linked quiet, the whole preset in one block", hotUnlinked,
                            { { 0, both (calibration, both (input (-9.1f), linkOn)) } } });
    // Three switches of the deck with the levels spread between them.
    deliveries.push_back ({ "hot to quiet: calibration, Output, speed, Input, machine, five blocks apart", hotUnlinked,
                            { { 0, calibration }, { 5, output (9.1f) }, { 10, speed }, { 15, input (-9.1f) }, { 20, machine } } });
    deliveries.push_back ({ "a second preset 10 blocks into a fade", hotUnlinked, { { 0, calibration }, { 10, secondPreset } } });
    deliveries.push_back ({ "a second preset 30 blocks into a fade", hotUnlinked, { { 0, calibration }, { 30, secondPreset } } });

    double worstBurst = -100.0;
    for (const Delivery& d : deliveries)
        for (const int block : { 16, 64, 512 })
        {
            const Levels single = deliver<TapeMachineDSP> (d, block), switcher = deliver<TapeMachineSwitcher> (d, block);
            std::string gaps;
            for (const auto& part : d.parts) gaps += (gaps.empty() ? "" : "/") + std::to_string (part.first);
            const std::string what = std::string ("levels in parts: ") + d.name + " (blocks " + gaps + "), block " + std::to_string (block);
            // A peak that stays under the louder of before and settled is no burst, whichever
            // of the two engines comes closer to it.
            const double excess = std::max (switcher.burst, 0.0) - std::max (single.burst, 0.0);
            // 3 dB, and 5 dB for the one delivery with three switches of the deck, which reads
            // 4.3 dB at 16-sample blocks for a tenth of a millisecond (see the comment above).
            const bool threeSwitches = std::string (d.name).find ("five blocks apart") != std::string::npos;
            holdAtMost (what, "peak over the louder of before and settled, beyond a single path's, dB", excess, threeSwitches ? 5.0 : 3.0);
            holdAtMost (what, "time more than 3 dB over, beyond a single path's, ms", switcher.overMs - single.overMs, 5.0);
            holdAtLeast (what, "lowest 10 ms under the quieter of before and settled, against a single path's, dB", switcher.hole - single.hole, -9.0);
            worstBurst = std::max (worstBurst, excess);
            if (std::string (d.name).find ("the whole preset in one block") != std::string::npos)
                holdAtMost (what, "lowest 2 ms, distance from a single path's (the same cut), dB", std::abs (switcher.shortHole - single.shortHole), 2.0);
            if (std::string (d.name).find ("the calibration alone") != std::string::npos)
                holdAtLeast (what, "lowest 10 ms against a single path's (a morph, so not lower), dB", switcher.hole - single.hole, -0.5);
        }
    // The comparisons hold something: the single path is itself far off in some of them, and
    // the switcher is not simply the single path.
    holdAtLeast ("Gain Link off, all deliveries", "largest burst the switcher shows over a single path's, dB (it differs somewhere)", worstBurst, 0.5);
    hold ("Gain Link off, a second preset 10 blocks into a fade", fadesOf (deliveries[deliveries.size() - 2], 64) == 2, "two fades");
    // Becoming linked with a stored Output that is not the inverse of Input: the kept path
    // must take the link with the levels, or it plays Input +10.9 with Output 0 while it fades
    // out (0.4 dB with the link, 9 dB without). In absolute terms, after the first 10 ms, in
    // which Input is ahead of the rest on any engine.
    for (const Delivery& d : deliveries)
        if (std::string (d.name).find ("stored Output of 0 dB") != std::string::npos)
            for (const int block : { 16, 64 })
            {
                const std::size_t n = static_cast<std::size_t> (block), at = static_cast<std::size_t> (0.5 * kRate) / n, blocks = at + static_cast<std::size_t> (1.3 * kRate) / n;
                TapeMachineSwitcher switcher;
                d.before (switcher);
                switcher.prepare (kRate, block);
                Render out (blocks * n);
                run (switcher, out, block, 0, blocks, [&] (std::size_t b) { for (const auto& part : d.parts) if (b == at + part.first) part.second (switcher); });
                const std::size_t z = at * n, ms = static_cast<std::size_t> (kRate / 1000.0);
                holdAtMost (std::string ("Gain Link off, ") + d.name + ", block " + std::to_string (block),
                            "highest 10 ms from 10 ms after the first part on, over the settled level, dB",
                            [&] { double hi = -200.0; for (std::size_t a = z + 10 * ms; a + 10 * ms <= z + 200 * ms; a += ms) hi = std::max (hi, rmsDb (out, a, a + 10 * ms)); return hi; }()
                                - rmsDb (out, z + 1000 * ms, z + 1250 * ms), 3.0);
            }
    // A second preset during a fade, in absolute terms: nothing over the louder of the two.
    holdAtMost ("Gain Link off, a second preset 10 blocks into a fade", "peak over the louder of before and settled, dB", deliver<TapeMachineSwitcher> (deliveries[deliveries.size() - 2], 64).burst, 0.5);
    holdAtMost ("Gain Link off, a second preset 30 blocks into a fade", "peak over the louder of before and settled, dB", deliver<TapeMachineSwitcher> (deliveries.back(), 64).burst, 0.5);
}

// A drop of Input beyond 6 dB during a fade is a second switch as well: the incoming path
// keeps the Input it was started on, and the drop is the next fade.
void checkInputDropInAFade()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, second = at + 10, blocks = at + 750, total = blocks * n;
    const std::size_t fadeLen = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate));
    const std::size_t firstEnd = (at + 1) * n + fadeLen, secondFadeBlock = (firstEnd + n - 1) / n + 1;
    const char* what = "a drop of Input by 9 dB ten blocks into a fade";
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofFirst (total), ofSecond (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) switcher.setCalibration (3);
        if (b == second) switcher.setInputGainDb (-6.0f);
    });
    TapeMachineDSP first;      // the first fade's path: it never sees the drop
    swissDeck (first); first.setCalibration (3);
    first.prepare (kRate, block);
    run (first, ofFirst, block, at + 1, blocks);
    TapeMachineDSP last;
    swissDeck (last); last.setCalibration (3); last.setInputGainDb (-6.0f);
    last.prepare (kRate, block);
    run (last, ofSecond, block, secondFadeBlock, blocks);
    hold (what, switcher.fadesStarted() == 2, std::to_string (switcher.fadesStarted()) + " fades started (must be 2)");
    hold (what, differing (out, ofFirst, firstEnd, secondFadeBlock * n) == 0,
          std::to_string (differing (out, ofFirst, firstEnd, secondFadeBlock * n)) + " samples between the fades differ from the first fade's path, which kept its Input (must be 0)");
    hold (what, differing (out, ofSecond, secondFadeBlock * n + fadeLen, total) == 0,
          std::to_string (differing (out, ofSecond, secondFadeBlock * n + fadeLen, total)) + " samples after the second fade differ from a path started afresh there (must be 0)");
}

// reset() in the middle of a fade ends it: from there on the output is the path that was
// running, given the settings that are asked for and reset.
void checkResetInAFade()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, resetAt = at + 10, blocks = at + 300, total = blocks * n;
    const char* what = "reset() ten blocks into a fade";
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofSingle (total);
    bool fadingBefore = false, fadingAfter = true;
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) switcher.setCalibration (3);
        if (b == resetAt) { fadingBefore = switcher.isFading(); switcher.reset(); fadingAfter = switcher.isFading(); }
    });
    TapeMachineDSP single;      // the path that was running: old settings until the reset
    swissDeck (single);
    single.prepare (kRate, block);
    run (single, ofSingle, block, 0, blocks, [&] (std::size_t b) { if (b == resetAt) { single.setCalibration (3); single.reset(); } });
    hold (what, fadingBefore && ! fadingAfter, "a fade was running before reset() and none after");
    hold (what, switcher.fadesStarted() == 1, std::to_string (switcher.fadesStarted()) + " fades started in all (must be 1: nothing is left to switch to)");
    hold (what, differing (out, ofSingle, resetAt * n, total) == 0,
          std::to_string (differing (out, ofSingle, resetAt * n, total)) + " samples after reset() differ from the running path given the new settings and reset (must be 0)");
}

// reset() in the block after a switch arrived, before its fade has started: there is no fade
// then, since the running path is given the settings that are asked for.
void checkResetInTheWait()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, resetAt = at + 1, blocks = at + 300, total = blocks * n;
    const char* what = "reset() in the wait";
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (total), ofSingle (total);
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b == at) switcher.setCalibration (3);
        if (b == resetAt) switcher.reset();
    });
    TapeMachineDSP single;
    swissDeck (single);
    single.prepare (kRate, block);
    run (single, ofSingle, block, 0, blocks, [&] (std::size_t b) { if (b == resetAt) { single.setCalibration (3); single.reset(); } });
    hold (what, switcher.fadesStarted() == 0, std::to_string (switcher.fadesStarted()) + " fades started (must be 0)");
    hold (what, differing (out, ofSingle, 0, total) == 0,
          std::to_string (differing (out, ofSingle, 0, total)) + " samples differ from the running path given the new settings and reset there (must be 0)");
}

// --- the deck switched on every block -------------------------------------------------------
// `awayAndBack`: between another speed and the running one, so that every second block asks
// for what is running, and no block finds the settings as the one before left them.
// Otherwise through six combinations of speed and calibration in turn, none of them the
// running one at the start: whichever a fade lands on comes round again only five blocks
// later, long after the wait for the next fade is over.
void checkEveryBlock (bool awayAndBack)
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 750;
    const char* what = awayAndBack ? "the speed switched to 30 IPS and back to 15 on alternate blocks for half a second"
                                   : "speed and calibration switched through six settings, one per block, for half a second";
    static constexpr int cycle[6][2] = { { 0, 0 }, { 2, 2 }, { 0, 3 }, { 2, 0 }, { 0, 2 }, { 2, 3 } };
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (blocks * n);
    int fadesDuring = 0;
    run (switcher, out, block, 0, blocks, [&] (std::size_t b) {
        if (b >= at && b < at + 375)
        {
            if (awayAndBack) switcher.setTapeSpeed (b % 2 == 0 ? 2 : 1);
            else { switcher.setTapeSpeed (cycle[b % 6][0]); switcher.setCalibration (cycle[b % 6][1]); }
        }
        if (b == at + 374) fadesDuring = switcher.fadesStarted();
    });
    bool finite = true;
    for (const float v : out.left) finite = finite && std::isfinite (v);
    hold (what, finite, "the output is finite");
    if (awayAndBack)
        hold (what, fadesDuring == 0 && switcher.fadesStarted() == 1,
              std::to_string (fadesDuring) + " fades while it went back and forth (must be 0), " + std::to_string (switcher.fadesStarted()) + " once it stayed (must be 1)");
    else
        hold (what, fadesDuring >= 4 && fadesDuring <= 12, std::to_string (fadesDuring) + " fades started (one after another: 4 to 12 in half a second)");
    holdAtLeast (what, "level of that half second against the half second before, dB", rmsDb (out, at * n, (at + 375) * n) - rmsDb (out, (at - 375) * n, at * n), -1.5);
    double lowest = 0.0;
    const std::size_t ms = static_cast<std::size_t> (kRate / 1000.0);
    for (std::size_t m = 0; m < 500; m += 5)
        lowest = std::min (lowest, rmsDb (out, at * n + m * ms, at * n + (m + 5) * ms) - rmsDb (out, (at - 375) * n, at * n));
    holdAtLeast (what, "lowest 5 ms of it against the level before, dB", lowest, -3.0);
}

// --- same memory, mono, a large block --------------------------------------------------------
void checkBuffers()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 400, total = blocks * n;
    // In place: the output is written where the input is read.
    {
        TapeMachineSwitcher apart, inPlace;
        swissDeck (apart); swissDeck (inPlace);
        apart.prepare (kRate, block); inPlace.prepare (kRate, block);
        Render ofApart (total), ofInPlace (total);
        run (apart, ofApart, block, 0, blocks, [&] (std::size_t b) { if (b == at) apart.setTapeMachine (1); });
        for (std::size_t b = 0; b < blocks; ++b)
        {
            if (b == at) inPlace.setTapeMachine (1);
            for (std::size_t i = 0; i < n; ++i) programme (b * n + i, ofInPlace.left[b * n + i], ofInPlace.right[b * n + i]);
            const float* ins[2] = { ofInPlace.left.data() + b * n, ofInPlace.right.data() + b * n };
            float* outs[2] = { ofInPlace.left.data() + b * n, ofInPlace.right.data() + b * n };
            inPlace.processBlock (ins, outs, 2, block);
        }
        hold ("input and output in the same memory, through a switch", apart.fadesStarted() == 1 && differing (ofApart, ofInPlace, 0, total) == 0,
              std::to_string (differing (ofApart, ofInPlace, 0, total)) + " samples differ from the run with separate buffers (must be 0)");
    }
    // Mono through a switch: the left channel of the stereo run of the same left input.
    {
        const char* what = "mono through a switch";
        TapeMachineSwitcher mono;
        swissDeck (mono);
        mono.prepare (kRate, block);
        Render out (total);
        run (mono, out, block, 0, blocks, [&] (std::size_t b) { if (b == at) mono.setCalibration (3); }, 1);
        bool finite = true;
        for (const float v : out.left) finite = finite && std::isfinite (v);
        hold (what, finite && mono.fadesStarted() == 1, "finite, one fade");
        holdAtLeast (what, "level after the switch against before, dB", rmsDb (Render (out), (at + 200) * n, (at + 400) * n) - rmsDb (out, (at - 200) * n, at * n), -3.0);
    }
    // A block four times the prepared size, with the switch at a multiple of four: the same,
    // to the bit, as the prepared size four times. (The large block is worked through in
    // pieces of the prepared size, so the wait and the fade fall where they fall there.)
    {
        const std::size_t at4 = 748, blocks4 = 1148, total4 = blocks4 * n;
        TapeMachineSwitcher small, large;
        swissDeck (small); swissDeck (large);
        small.prepare (kRate, block); large.prepare (kRate, block);
        Render ofSmall (total4), ofLarge (total4);
        run (small, ofSmall, block, 0, blocks4, [&] (std::size_t b) { if (b == at4) small.setTapeSpeed (2); });
        run (large, ofLarge, 4 * block, 0, blocks4 / 4, [&] (std::size_t b) { if (b == at4 / 4) large.setTapeSpeed (2); });
        hold ("a block four times the prepared size, through a switch", large.fadesStarted() == 1 && peakDb (ofLarge, 0, total4) > -60.0, "one fade, not silent");
        hold ("a block four times the prepared size, through a switch", differing (ofSmall, ofLarge, 0, total4) == 0,
              std::to_string (differing (ofSmall, ofLarge, 0, total4)) + " samples differ from the run at the prepared size (must be 0)");
    }
    // A block that is not a multiple of the prepared size (200 against 64): three pieces of
    // 64 and one of 8. With nothing switched that is one path fed the same pieces.
    {
        const std::size_t callSize = 200, calls = 360;
        TapeMachineSwitcher switcher;
        TapeMachineDSP single;
        swissDeck (switcher); swissDeck (single);
        switcher.prepare (kRate, block); single.prepare (kRate, block);
        Render ofSwitcher (calls * callSize), ofSingle (calls * callSize);
        std::vector<float> inL (callSize), inR (callSize);
        for (std::size_t c = 0; c < calls; ++c)
        {
            if (c == 100) { switcher.setReproHf (3.0f); single.setReproHf (3.0f); }
            for (std::size_t i = 0; i < callSize; ++i) programme (c * callSize + i, inL[i], inR[i]);
            const float* ins[2] = { inL.data(), inR.data() };
            float* outs[2] = { ofSwitcher.left.data() + c * callSize, ofSwitcher.right.data() + c * callSize };
            switcher.processBlock (ins, outs, 2, static_cast<int> (callSize));
            for (std::size_t done = 0; done < callSize; done += 64)
            {
                const int piece = static_cast<int> (std::min<std::size_t> (64, callSize - done));
                const float* pieceIn[2] = { inL.data() + done, inR.data() + done };
                float* pieceOut[2] = { ofSingle.left.data() + c * callSize + done, ofSingle.right.data() + c * callSize + done };
                single.processBlock (pieceIn, pieceOut, 2, piece);
            }
        }
        hold ("a block of 200 against 64 prepared", peakDb (ofSingle, 0, calls * callSize) > -60.0 && differing (ofSwitcher, ofSingle, 0, calls * callSize) == 0,
              std::to_string (differing (ofSwitcher, ofSingle, 0, calls * callSize)) + " samples differ from one path fed pieces of 64, 64, 64 and 8 (must be 0)");
    }
}

// --- the meters at the handover ---------------------------------------------------------------
void checkMeters()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 200;
    const std::size_t fadeBlocks = static_cast<std::size_t> (TapeMachineSwitcher::kFadeSec * static_cast<float> (kRate)) / n + 2;
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    Render out (blocks * n);
    float before = 0.0f, lowest = 1.0e9f, lowestIn = 1.0e9f, beforeIn = 0.0f;
    std::vector<float> inL (n), inR (n);
    for (std::size_t b = 0; b < blocks; ++b)
    {
        if (b == at) switcher.setCalibration (3);
        for (std::size_t i = 0; i < n; ++i) programme (b * n + i, inL[i], inR[i]);
        const float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { out.left.data() + b * n, out.right.data() + b * n };
        switcher.processBlock (ins, outs, 2, block);
        if (b == at - 1) { before = switcher.getVuL(); beforeIn = switcher.getInVuL(); }
        if (b >= at && b < at + fadeBlocks + 20) { lowest = std::min (lowest, switcher.getVuL()); lowestIn = std::min (lowestIn, switcher.getInVuL()); }
    }
    holdAtLeast ("the meters through a switch", "lowest output VU reading from the switch to 20 blocks after the fade, against the reading before, dB", toDb (lowest) - toDb (before), -1.0);
    holdAtLeast ("the meters through a switch", "lowest input VU reading over the same time, against the reading before, dB", toDb (lowestIn) - toDb (beforeIn), -1.0);
    hold ("the meters through a switch", before > 1.0e-3f, "the reading before is not zero");
}

// --- no allocation -----------------------------------------------------------------------------
void checkNoAllocation()
{
    const int block = 64;
    const std::size_t n = 64, at = kSwitchBlockAt, blocks = at + 400;
    TapeMachineSwitcher switcher;
    swissDeck (switcher);
    switcher.prepare (kRate, block);
    std::vector<float> inL (n), inR (n), outL (n), outR (n);
    const long before = allocations.load (std::memory_order_relaxed);
    for (std::size_t b = 0; b < blocks; ++b)
    {
        if (b == at) { switcher.setTapeMachine (1); switcher.setInputGainDb (-6.0f); }
        if (b == at + 100) switcher.setBypass (true);
        if (b == at + 150) switcher.setBypass (false);
        if (b == at + 200) switcher.setTapeSpeed (2);
        for (std::size_t i = 0; i < n; ++i) programme (b * n + i, inL[i], inR[i]);
        const float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { outL.data(), outR.data() };
        switcher.processBlock (ins, outs, 2, block);
    }
    const long allocated = allocations.load (std::memory_order_relaxed) - before;
    hold ("processing through two switches and a bypass", allocated == 0 && switcher.fadesStarted() == 2,
          std::to_string (allocated) + " allocations (must be 0), " + std::to_string (switcher.fadesStarted()) + " fades (must be 2)");
}
} // namespace

int main (int argc, char** argv)
{
    printAll = argc > 1 && std::string (argv[1]) == "--print";

    const auto nothing = [] (TapeMachineSettings&, std::size_t) {};
    checkOnePath ("steady, Swiss", swissDeck, nothing);
    checkOnePath ("steady, American, block 512", americanDeck, nothing, 512);
    checkOnePath ("steady, mono", swissDeck, nothing, 64, 1);
    checkOnePath ("knobs on every block", swissDeck, [] (TapeMachineSettings& d, std::size_t b) {
        const float t = static_cast<float> (b % 200) / 200.0f;
        d.setInputGainDb (3.0f + 4.0f * t); d.setBias (40.0f + 20.0f * t); d.setHighpassHz (20.0f + 100.0f * t);
        d.setLowpassHz (20000.0f - 8000.0f * t); d.setReproHf (6.0f * t - 3.0f); d.setNoiseAmount (20.0f * t); d.setAutoCal (b % 400 < 200);
    });
    checkOnePath ("tape type, EQ standard, signal path and head width switched", americanDeck, [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 300) d.setTapeType (3);
        if (b == 500) d.setEqStandard (1);
        if (b == 700) d.setSignalPath (2);
        if (b == 900) d.setHeadWidth (2);
    });
    // Three steps of 4 dB down are three knob moves, though they add up to 12 dB: what counts
    // is the drop from where the running path is, not from where it started. Up is never a switch.
    checkOnePath ("Input down by 4 dB three times, then up by 21 dB", swissDeck, [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 300) d.setInputGainDb (-1.0f);
        if (b == 500) d.setInputGainDb (-5.0f);
        if (b == 700) d.setInputGainDb (-9.0f);
        if (b == 900) d.setInputGainDb (12.0f);
    });
    // Unlinked, the levels are knobs whatever their size: a drop of Input is not a switch.
    checkOnePath ("Gain Link off: Input down by 9 dB, Output up by 9, Input up by 12", [] (TapeMachineSettings& d) { swissDeck (d); d.setAutoComp (false); },
                  [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 300) d.setInputGainDb (-6.0f);
        if (b == 500) d.setOutputGainDb (9.0f);
        if (b == 700) d.setInputGainDb (6.0f);
    });
    checkOnePath ("Input down by exactly 6 dB", swissDeck, [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 500) d.setInputGainDb (-3.0f);
    });
    checkOnePath ("bypass and Thru, with the deck switched while passed through", swissDeck, [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 300) d.setBypass (true);
        if (b == 350) { d.setTapeMachine (1); d.setCalibration (3); }
        if (b == 500) d.setBypass (false);
        if (b == 700) d.setSignalPath (3);
        if (b == 750) d.setTapeSpeed (2);
        if (b == 900) d.setSignalPath (0);
    });
    // Into bypass or the Thru path in the same block as a switch of the deck: the path takes it
    // all and passes the input through, as a single path does.
    checkOnePath ("bypass and Thru, each switched on together with the deck", swissDeck, [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 300) { d.setBypass (true); d.setCalibration (3); }
        if (b == 500) d.setBypass (false);
        if (b == 700) { d.setSignalPath (3); d.setTapeMachine (1); }
        if (b == 900) d.setSignalPath (0);
    });
    checkOnePath ("the Swiss deck switched from 15 to 3.75 IPS (the same speed there)", swissDeck, [] (TapeMachineSettings& d, std::size_t b) {
        if (b == 500) d.setTapeSpeed (3);
    });

    const Setting toAmerican = [] (TapeMachineSettings& d) { d.setTapeMachine (1); };
    const Setting toThirty   = [] (TapeMachineSettings& d) { d.setTapeSpeed (2); };
    const Setting toPlusNine = [] (TapeMachineSettings& d) { d.setCalibration (3); };
    const Setting inputDown  = [] (TapeMachineSettings& d) { d.setInputGainDb (-6.0f); };
    checkSwitch ("calibration +6 to +9", swissDeck, { toPlusNine }, 1, 64, true);
    checkSwitch ("speed 15 to 30 IPS", swissDeck, { toThirty }, 1, 64, true);
    checkSwitch ("Swiss to American", swissDeck, { toAmerican }, 1, 64, true);
    checkSwitch ("Input down by 9 dB", swissDeck, { inputDown }, 1, 64, true);
    checkSwitch ("calibration +6 to +9", americanDeck, { toPlusNine }, 1, 512, true);
    checkSwitch ("speed 15 to 30 IPS", americanDeck, { toThirty }, 1, 2048, true);
    checkSwitch ("a preset in one block", swissDeck, { [] (TapeMachineSettings& d) { presetDeck (d); presetLevel (d); presetTone (d); } }, 1);
    checkSwitch ("a preset over two blocks", swissDeck, { presetDeck, [] (TapeMachineSettings& d) { presetLevel (d); presetTone (d); } }, 2);
    checkSwitch ("a preset over three blocks", swissDeck, { presetDeck, presetLevel, presetTone }, 3);
    checkSwitch ("a preset over two blocks", swissDeck, { presetDeck, [] (TapeMachineSettings& d) { presetLevel (d); presetTone (d); } }, 2, 512);
    // Four parts: the fade starts in the block of the fourth, on all four. A fifth comes
    // after its start, and the incoming path takes it then.
    checkSwitch ("a preset over four blocks", swissDeck, { presetDeck, presetLevel, [] (TapeMachineSettings& d) { d.setHighpassHz (40.0f); },
                                                           [] (TapeMachineSettings& d) { d.setReproHf (-3.0f); } }, 3);
    checkSwitch ("a preset over five blocks", swissDeck, { presetDeck, presetLevel, [] (TapeMachineSettings& d) { d.setHighpassHz (40.0f); },
                                                           [] (TapeMachineSettings& d) { d.setReproHf (-3.0f); },
                                                           [] (TapeMachineSettings& d) { d.setReproLf (2.0f); } }, 3);
    // A path that is linked is kept as it is through the wait and the fade: the levels of the
    // new setting are not for it.
    checkSwitch ("linked to unlinked, with the calibration", hotLinked,
                 { [] (TapeMachineSettings& d) { d.setCalibration (3); d.setAutoComp (false); d.setInputGainDb (-9.1f); d.setOutputGainDb (9.1f); } }, 1);
    // The second part changes the sign of two settings and nothing else: it is a part like any
    // other, the fade waits for the block after it and starts on it.
    checkSwitch ("a preset over two blocks, the second two changes of sign",
                 [] (TapeMachineSettings& d) { swissDeck (d); d.setReproLf (3.0f); d.setReproLmf (2.0f); },
                 { toThirty, [] (TapeMachineSettings& d) { d.setReproLf (-3.0f); d.setReproLmf (-2.0f); } }, 2);
    checkSecondSwitch();
    checkKnobsInAFade();
    checkSwitchTakenBack();
    checkUnlinkedToLinkedDrop();
    checkLinkedRiseTakenBack();
    checkLatencyInAFade (false);
    checkLatencyInAFade (true);
    checkPassthroughEndsWithASwitch (false, 64);
    checkPassthroughEndsWithASwitch (false, 512);
    checkPassthroughEndsWithASwitch (true, 64);
    checkPassthroughEndsWithASwitch (true, 512);
    checkPassthroughEndsInAFade (false);
    checkPassthroughEndsInAFade (true);
    checkWaitsStartOver();
    checkUnlinked();
    checkInputDropInAFade();
    checkResetInAFade();
    checkResetInTheWait();
    checkEveryBlock (false);
    checkEveryBlock (true);
    checkBuffers();
    checkMeters();
    checkNoAllocation();

    if (failures == 0) std::printf ("SwitcherTest: all checks passed\n");
    else               std::printf ("SwitcherTest: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
