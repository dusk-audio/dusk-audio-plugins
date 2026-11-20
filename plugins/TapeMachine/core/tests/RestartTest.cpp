// RestartTest: TapeMachineDSP::restart() leaves the state prepare() leaves, and does not allocate.
//
// An instance runs half a second of programme on one setting, is given another setting and
// restarted, and runs on. From the restart on its output must be, bit for bit, that of an
// instance prepared afresh on the second setting and fed the same samples. Whatever the first
// half second left behind (filter state, delay lines, smoothers on their way, level followers,
// the noise and wow generators' positions, the clean-start bookkeeping) would show as a
// difference. The cases differ in what the first half leaves: a hot or a quiet programme, a
// lowpass in use, a bypass, a clean start still in flight. The meters must read the same at
// the end as well. Every allocation between the call of restart() and the end of the run is
// counted, and there must be none (operator new in all its forms; an allocation that went to
// malloc directly would not be seen).
//
//   RestartTest            prints failures and a summary
//   RestartTest --print    every case
#include "TapeMachineDSP.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

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
// The over-aligned forms count as well.
void* operator new (std::size_t size, std::align_val_t alignment)
{
    allocations.fetch_add (1, std::memory_order_relaxed);
    const std::size_t a = static_cast<std::size_t> (alignment);
    if (void* p = std::aligned_alloc (a, (std::max<std::size_t> (size, 1) + a - 1) / a * a)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size, std::align_val_t alignment) { return operator new (size, alignment); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

namespace
{
using duskaudio::TapeMachineDSP;
using Setting = void (*) (TapeMachineDSP&);

int failures = 0;
bool printAll = false;

void hold (const std::string& what, bool ok, const std::string& reading)
{
    if (! ok) ++failures;
    if (! ok || printAll) std::printf ("%s %s: %s\n", ok ? "  ok" : "FAIL", what.c_str(), reading.c_str());
}

// The programme, by sample position: a wide chord, the right channel lower and without the top.
void programme (std::size_t pos, double rate, float& left, float& right)
{
    static constexpr double freqs[6] = { 55.0, 110.0, 440.0, 1200.0, 3300.0, 9000.0 };
    const double t = static_cast<double> (pos) / rate;
    double all = 0.0, noTop = 0.0;
    for (int k = 0; k < 6; ++k)
    {
        const double v = std::sin (6.283185307179586 * freqs[k] * t);
        all += v;
        if (k < 5) noTop += v;
    }
    left  = static_cast<float> (0.9 * all / 6.0);
    right = static_cast<float> (0.55 * noTop / 5.0);
}

// Settings a restart has something to clear after, and something to set up for.
void hotSwissWithNoise (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (1); d.setTapeType (1); d.setCalibration (3);
    d.setInputGainDb (9.0f); d.setAutoCal (false); d.setBias (35.0f); d.setNoiseAmount (40.0f);
    d.setReproHf (6.0f); d.setReproSubBell (12.0f); d.setProgHfTrim (-8.0f); d.setProgLfTrim (8.0f);
    d.setHighpassHz (60.0f); d.setLowpassHz (9000.0f); d.setLpQ (1.6f);
}
void wobblyAmerican (TapeMachineDSP& d)
{
    d.setTapeMachine (1); d.setTapeSpeed (3); d.setTapeType (3); d.setCalibration (0); d.setHeadWidth (2);
    d.setInputGainDb (-6.0f); d.setAutoCal (true); d.setWow (40.0f); d.setFlutter (30.0f); d.setNoiseAmount (15.0f);
    d.setLevelHfTrim (-6.0f); d.setReproLf (3.0f); d.setAutoComp (false); d.setOutputGainDb (-2.0f);
    d.setLowpassHz (12000.0f);
}
// Input and calibration such that the programme decides how far the level and programme
// trims are in (in on the loud programme, out on the quiet one): their followers have
// something to leave behind.
void warmSwissWithTrims (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (1); d.setTapeType (0); d.setCalibration (3);
    d.setInputGainDb (3.0f); d.setAutoCal (true); d.setWow (0.0f); d.setFlutter (0.0f);
    d.setLevelHmfTrim (-6.0f); d.setLevelHfTrim (-6.0f);
    d.setProgHmfTrim (-8.0f); d.setProgHfTrim (-8.0f); d.setProgLfTrim (8.0f);
}
void cleanSwiss (TapeMachineDSP& d)
{
    d.setTapeMachine (0); d.setTapeSpeed (2); d.setTapeType (2); d.setCalibration (0);
    d.setInputGainDb (0.0f); d.setAutoCal (true); d.setWow (0.0f); d.setFlutter (0.0f);
}
// Back to every default first, so that the second setting does not inherit from the first.
void defaults (TapeMachineDSP& d)
{
    TapeMachineDSP fresh;
    d.copySettingsFrom (fresh);
}

// What the first half does besides running its setting.
struct Before
{
    float level = 1.0f;             // of the first half's programme
    bool bypassAtEnd = false;       // the last eight blocks in bypass
    bool switchAtEnd = false;       // the calibration switched in the last block: a clean start in flight
    bool nudgeAfter = false;        // and, in both instances, Input 2 dB down two blocks after the restart:
                                    // a knob move, unless a settling time were still open from before
    float levelAfter = 1.0f;        // of the second half's programme
    bool switchAtOnce = false;      // in both instances, the calibration switched before the first block
                                    // after the restart: that block is a clean start, and what it lets
                                    // run out must not be the first half's last sample
    bool dropAfter = false;         // in both instances, Input 12 dB down two blocks after the restart:
                                    // a clean start, unless a hold were still counting from before
};

void check (const char* name, Setting first, Setting second, double rate, int block, Before before = {})
{
    const std::size_t blockN = static_cast<std::size_t> (block);
    const std::size_t half = static_cast<std::size_t> (rate * 0.5) / blockN * blockN, total = 2 * half;
    std::vector<float> inL (total), inR (total), outL (total), outR (total), freshL (half), freshR (half);
    for (std::size_t i = 0; i < total; ++i)
    {
        programme (i, rate, inL[i], inR[i]);
        const float level = i < half ? before.level : before.levelAfter;
        inL[i] *= level; inR[i] *= level;
    }

    TapeMachineDSP used, fresh;
    first (used);
    used.prepare (rate, block);
    defaults (fresh); second (fresh);
    fresh.prepare (rate, block);

    // `firstHalf`: the events of the first half; otherwise those after the restart.
    const auto run = [&] (TapeMachineDSP& d, bool firstHalf, const float* l, const float* r, float* ol, float* orr, std::size_t n) {
        for (std::size_t pos = 0; pos < n; pos += blockN)
        {
            if (firstHalf && before.bypassAtEnd && pos == n - 8 * blockN) d.setBypass (true);
            if (firstHalf && before.switchAtEnd && pos == n - blockN)     d.setCalibration (1);
            if (! firstHalf && before.nudgeAfter && pos == 2 * blockN)    d.setInputGainDb (7.0f);
            if (! firstHalf && before.switchAtOnce && pos == 0)           d.setCalibration (0);
            if (! firstHalf && before.dropAfter && pos == 2 * blockN)     d.setInputGainDb (-3.0f);
            const float* ins[2] = { l + pos, r + pos };
            float* outs[2] = { ol + pos, orr + pos };
            d.processBlock (ins, outs, 2, block);
        }
    };
    run (used, true, inL.data(), inR.data(), outL.data(), outR.data(), half);
    defaults (used); second (used);
    const long allocatedBefore = allocations.load (std::memory_order_relaxed);
    used.restart();
    run (used, false, inL.data() + half, inR.data() + half, outL.data() + half, outR.data() + half, half);
    const long allocated = allocations.load (std::memory_order_relaxed) - allocatedBefore;
    run (fresh, false, inL.data() + half, inR.data() + half, freshL.data(), freshR.data(), half);

    char label[160];
    std::snprintf (label, sizeof (label), "%s, %.0f Hz, block %d", name, rate, block);
    std::size_t differing = 0;
    float peak = 0.0f, peakBefore = 0.0f;
    bool finite = true;
    for (std::size_t i = 0; i < half; ++i)
    {
        differing += std::memcmp (&outL[half + i], &freshL[i], sizeof (float)) != 0 || std::memcmp (&outR[half + i], &freshR[i], sizeof (float)) != 0;
        peak = std::max (peak, std::abs (freshL[i]));
        peakBefore = std::max (peakBefore, std::abs (outL[i]));
        finite = finite && std::isfinite (outL[half + i]) && std::isfinite (outR[half + i]) && std::isfinite (freshL[i]);
    }
    hold (label, finite && peak > 1.0e-4f && peakBefore > 1.0e-4f, "both halves are finite and not silent");
    const bool sameMeters = used.getVuL() == fresh.getVuL() && used.getVuR() == fresh.getVuR()
                            && used.getInVuL() == fresh.getInVuL() && used.getInVuR() == fresh.getInVuR()
                            && used.getInPeakL() == fresh.getInPeakL() && used.getInPeakR() == fresh.getInPeakR()
                            && used.getOutPeakL() == fresh.getOutPeakL() && used.getOutPeakR() == fresh.getOutPeakR();
    hold (label, sameMeters && fresh.getVuL() > 0.0f, "the eight meter readings at the end are those of the instance prepared afresh");
    hold (label, differing == 0, std::to_string (differing) + " samples after restart() differ from an instance prepared afresh (must be 0)");
    hold (label, allocated == 0, std::to_string (allocated) + " allocations in restart() and the run after it (must be 0)");
}
} // namespace

int main (int argc, char** argv)
{
    printAll = argc > 1 && std::string (argv[1]) == "--print";

    check ("hot Swiss with noise, then wobbly American", hotSwissWithNoise, wobblyAmerican, 48000.0, 64);
    check ("wobbly American, then hot Swiss with noise", wobblyAmerican, hotSwissWithNoise, 48000.0, 64);
    check ("hot Swiss with noise, then clean Swiss", hotSwissWithNoise, cleanSwiss, 48000.0, 512);
    check ("wobbly American, then the same again", wobblyAmerican, wobblyAmerican, 48000.0, 64);
    check ("hot Swiss with noise, then wobbly American", hotSwissWithNoise, wobblyAmerican, 44100.0, 256);
    check ("wobbly American, then hot Swiss with noise", wobblyAmerican, hotSwissWithNoise, 96000.0, 128);
    check ("hot Swiss with noise, then the same again", hotSwissWithNoise, hotSwissWithNoise, 48000.0, 64);
    check ("clean Swiss on a quiet programme, then hot Swiss with noise", cleanSwiss, hotSwissWithNoise, 48000.0, 64, { 0.02f, false, false, false, 1.0f });
    check ("wobbly American ending in bypass, then hot Swiss with noise", wobblyAmerican, hotSwissWithNoise, 48000.0, 64, { 1.0f, true, false, false, 1.0f });
    check ("hot Swiss with a clean start in flight, then the same with a nudge of Input", hotSwissWithNoise, hotSwissWithNoise, 48000.0, 64, { 1.0f, false, true, true, 1.0f });
    check ("warm Swiss with trims, then the same again", warmSwissWithTrims, warmSwissWithTrims, 48000.0, 64);
    check ("warm Swiss with trims, then the same on a quiet programme", warmSwissWithTrims, warmSwissWithTrims, 48000.0, 64, { 1.0f, false, false, false, 0.02f });
    check ("wobbly American, then hot Swiss with the calibration switched before its first block", wobblyAmerican, hotSwissWithNoise, 48000.0, 64, { 1.0f, false, false, false, 1.0f, true, false });
    check ("clean Swiss, then hot Swiss with a drop of Input two blocks in", cleanSwiss, hotSwissWithNoise, 48000.0, 64, { 1.0f, false, false, false, 1.0f, false, true });

    if (failures == 0) std::printf ("RestartTest: all checks passed\n");
    else               std::printf ("RestartTest: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
