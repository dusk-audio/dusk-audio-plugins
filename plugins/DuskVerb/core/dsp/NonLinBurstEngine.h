// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// NonLinBurstEngine: DuskVerb 2's classic nonlinear program, the gated burst of the early
// 1980s digital reverbs. Linear and time-invariant, like the hardware it answers to: a dense
// burst whose level follows a programmed envelope and then stops, the same at any input level.
//
// Per channel, with its own random seed (the channels are uncorrelated):
//
//   input -> raw delay line, and a one-pole darkened delay line
//         -> K taps: tap k reads the dark line or the raw line; gain = envelope(t_k) / sqrt(density(t_k)) x N(0, 1)
//         -> 2 short allpasses (they multiply the tap density without adding a tail)
//         -> + send x tail (2 input allpasses, 4-line Householder FDN with one-pole damping):
//            the tail is what remains once the taps stop, so it makes the fall
//         -> 2nd-order output lowpass, then the HF damping one-pole (off by default)
//
// The envelope is an energy envelope shared by every length: a rise over the first 60 ms, a
// plateau, a knee shape around the knee, then a straight fall to the floor, where the taps stop.
// The knee shape, the plateau slope, the fall rate and the output level vary with the knee as
// quadratics fitted to the reference. All of it is the reference model in the private tools
// repo (tools/duskverb/nonlin_lab/proto.py), which this mirrors: the same SplitMix64 stream with
// four draws per tap in tap order, so at 48 kHz the tap layout is identical to the model's.
//
// Threading: prepare() allocates and runs off the audio thread. Everything else is audio-thread
// safe: no allocation, no locks. prepare() draws the tap layout once (each tap's time, line and
// random gain, none of which depends on the knee); a knee change then recomputes only the
// envelope gains (about 700 taps per channel at the longest knee, roughly 40 us for both
// channels), and a pre-delay change only the delays.
//
// A new tap set never replaces the old one in a step: the two tap sums crossfade over
// kTapFadeMs. Stepping them zippered a moving knee (sine through a knee swept +-50 ms at
// 0.5 Hz: everything off the tone at -24.5 dB re the output) and tore a moving pre-delay
// apart (-0.4 dB). A change that arrives during a fade waits for it to end, so a sweep is
// followed in kTapFadeMs steps and the tap work at most doubles while one runs.
//
// Once a block's input and output and every filter state are under kZeroBelow (1e-30), all the
// filter states are set to exactly 0 together, so the tail ends at 0 on every CPU. The one-poles
// need it: under flush-to-zero their step c * (x - z) underflows to 0 while z is still a normal
// float, so on x86-64 the 3 kHz darkLp stopped decaying near 3e-38 and Nonlinear Large sounded at
// 4.6e-38 for as long as it ran (on arm64 the step is fused into one multiply-add and z keeps
// decaying). All at once and only then, because one state can sit under 1e-30 while a signal
// still passes the others; the output joins the test because a quiet input says nothing of what
// the rings and lines still hold. The delay lines (the allpasses and the tail's lines) are left
// alone: each pass scales them by less than one, so flush-to-zero ends them at 0 by itself, and
// zeroing single entries would cut near-zero samples out of a tail still ringing above 1e-25.
// The rings hold the input's history and feed nothing back.

#pragma once

#include "../../../shared-daf/dsp/DuskFilters.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace duskverb
{

class NonLinBurstEngine
{
public:
    // Knee: ms after the onset at which the burst sits 10 dB under its plateau.
    static constexpr float kMinKneeMs = 40.0f;
    static constexpr float kMaxKneeMs = 600.0f;
    static constexpr float kMaxPreDelayMs = 250.0f;
    static constexpr int   kMaxTaps = 2048;          // per channel; the longest knee needs 694
    static constexpr float kTapFadeMs = 20.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void clear() noexcept;

    // Applied at the next process(). A moving knee is followed in steps of at least 0.25 ms (so
    // automation does not rebuild the gains every block); a knee that arrives twice in a row is
    // applied exactly, so a setting at rest renders the same whatever path reached it.
    void setKnee (float kneeMs) noexcept;
    void setPreDelay (float ms) noexcept;

    // The knee for a decay time, the time from the input to -60 dB under the plateau. Measured
    // on this engine's output: t60 = 1.0216 * knee + 170.2 ms (within 6 ms, knees 40 to 600).
    static float kneeForDecaySeconds (float seconds) noexcept;

    // HF damping: a one-pole lowpass after the burst, as on the hardware. At or above
    // kHfDampOffHz it is bypassed (the filter keeps running, so switching it in starts from
    // the signal rather than from stale state). The reference's four settings sit at 6750,
    // 4250, 2750 and 2250 Hz.
    static constexpr float kHfDampOffHz = 20000.0f;
    void setHfDamp (float hz) noexcept;

    // The shared Treble Multiply control (0.1-1.5) as the HF damping corner: 1.0 and above is
    // off; below it the corner falls exponentially to 2 kHz at 0.1. The reference's four
    // settings sit at 0.58 (6750 Hz), 0.40 (4250), 0.22 (2750) and 0.15 (2250).
    static float hfDampHzForTreble (float trebleMultiply) noexcept;

    // Adds into nothing: writes outL/outR (which may alias inL/inR).
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    // The current tap set, for the parity tests against the reference model.
    struct Tap
    {
        int   delay = 0;       // samples, pre-delay included
        float gain  = 0.0f;
        bool  dark  = false;   // reads the darkened line
    };
    int tapCount (int channel) const noexcept { return channels_[static_cast<size_t> (channel)].numTaps; }
    const Tap& tap (int channel, int index) const noexcept
    { return channels_[static_cast<size_t> (channel)].taps[static_cast<size_t> (index)]; }

private:

    // Schroeder allpass, canonical form: v = x + g v[n-d], y = -g v + v[n-d].
    struct Allpass
    {
        std::vector<float> buf;
        int   pos = 0;
        float g   = 0.0f;
        void  prepare (int delay, float gain) { buf.assign (static_cast<size_t> (delay), 0.0f); pos = 0; g = gain; }
        void  clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        float process (float x) noexcept
        {
            const float delayed = buf[static_cast<size_t> (pos)];
            const float v = x + g * delayed;
            buf[static_cast<size_t> (pos)] = v;
            if (++pos == static_cast<int> (buf.size())) pos = 0;
            return delayed - g * v;
        }
    };

    struct Tail
    {
        Allpass in[2];
        std::array<std::vector<float>, 4> line;
        std::array<int, 4>   pos {};
        std::array<float, 4> gain {};
        std::array<float, 4> lp {};
        float dampC = 0.0f;
        void  clear() noexcept;
        float process (float x) noexcept;
    };

    // What a tap is apart from the knee and the pre-delay: drawn from the seeded stream once,
    // in prepare(), up to the end of the longest burst. A rebuild for a new knee then
    // evaluates only the envelope.
    struct LayoutTap
    {
        double t        = 0.0;     // ms after the onset
        double densNorm = 1.0;     // sqrt (density (t) / kDensRef), the gain's divisor
        double normal   = 0.0;     // the tap's N(0, 1) draw
        bool   dark     = false;
    };

    struct Channel
    {
        std::vector<float> raw, dark;          // double-written rings, 2 * ringLen
        duskaudio::OnePoleLP darkLp;
        std::vector<LayoutTap> layout;
        std::array<Tap, kMaxTaps> taps {};
        int numTaps = 0;
        std::array<Tap, kMaxTaps> fadeTaps {};     // the set being faded out
        int numFadeTaps = 0;
        Allpass mult[2];
        Tail tail;
        duskaudio::Biquad outLp;
        duskaudio::OnePoleLP hfDamp;
    };

    void updateTaps() noexcept;
    void rebuildGains() noexcept;
    void placeTaps() noexcept;
    void processChannel (Channel& c, const float* in, float* out, int n) noexcept;
    static void sumTaps (const Channel& c, const Tap* taps, int count, int writePos, int ringLen,
                         float* acc, int n) noexcept;

    // Under this a signal or a filter state has decayed out of anything audible (see the header
    // comment).
    static constexpr float kZeroBelow = 1.0e-30f;
    static bool decayed (float s) noexcept { return std::abs (s) < kZeroBelow; }
    // Once a block of `n` samples has run: if its input peak, its output `out` and every filter
    // state of `c` (the two one-poles, given by their last outputs, which are their states; the
    // output lowpass; the tail's damping) are under kZeroBelow, those states all go to exactly 0.
    static void zeroDecayedState (Channel& c, float inputPeak, const float* out, int n,
                                  float darkState, float hfState) noexcept;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    int ringLen_ = 0;
    int writePos_ = 0;
    float darkNorm_ = 1.0f;
    float tailSend_ = 0.0f;
    float kneeMs_ = 150.0f, preDelayMs_ = 0.0f;
    float requestedKneeMs_ = 150.0f;           // the last setKnee() value, for the settle rule
    float hfDampHz_ = kHfDampOffHz;
    bool gainsDirty_ = true, delaysDirty_ = true;
    bool fresh_ = true;                        // cleared since the last update: no old set to fade from
    int fadeLen_ = 1, fadeRemaining_ = 0;      // samples
    std::array<Channel, 2> channels_;
    std::vector<float> acc_, fadeAcc_;
};

} // namespace duskverb
