// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// NonLinBurstEngine: DuskVerb 2's classic nonlinear program, a gated burst. Linear and
// time-invariant: a dense burst whose level follows a programmed envelope and then stops,
// the same at any input level.
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
// quadratics fitted to the target responses. All of it is the reference model in the private tools
// repo (tools/duskverb/nonlin_lab/proto.py), which this mirrors: the same SplitMix64 stream with
// four draws per tap in tap order, so at 48 kHz the tap layout is identical to the model's.
//
// Threading: prepare() allocates and runs off the audio thread. Everything else is audio-thread
// safe: no allocation, no locks. prepare() draws the tap layout once (each tap's time, line and
// random gain, none of which depends on the knee); a knee change then recomputes only the
// envelope gains (about 2760 taps per channel at the longest knee, about 45 us for both
// channels on the development machine), and a pre-delay change only the delays.
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
// The rings hold the input's history and feed nothing back. clear() does not wipe them: sized
// for the longest burst they hold 4.2 MB at 48 kHz and 16.5 MB at 192 kHz, too much to zero
// on the audio thread at every preset swap. The taps read only what was written since (sumTaps).

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
    // Decay is honoured up to kMaxDecaySeconds; a longer setting plays the burst of this one.
    static constexpr float kMaxDecaySeconds = 2.5f;
    // kneeForDecaySeconds' law: fitted up to the kLinearTopKneeMs knee, one for one past it.
    // DuskVerbAlgorithms.hpp's kNonLinMaxDecaySeconds mirrors kMaxDecaySeconds for the editor
    // and host text (asserted equal in DuskVerbDSP.cpp).
    static constexpr float kLinearTopKneeMs = 600.0f;
    static constexpr float kLinearTopDecayMs = 1.0216f * kLinearTopKneeMs + 170.2f;
    static constexpr float kMaxKneeMs = kLinearTopKneeMs + (kMaxDecaySeconds * 1000.0f - kLinearTopDecayMs);
    static constexpr float kMaxPreDelayMs = 250.0f;
    static constexpr int   kMaxTaps = 3072;          // per channel; the longest knee needs 2758
    static constexpr float kTapFadeMs = 20.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void clear() noexcept;

    // Applied at the next process(). A moving knee is followed in steps of at least 0.25 ms (so
    // automation does not rebuild the gains every block); a knee that arrives twice in a row is
    // applied exactly, so a setting at rest renders the same whatever path reached it.
    void setKnee (float kneeMs) noexcept;
    void setPreDelay (float ms) noexcept;

    // The knee for a decay time, the time from the input to -60 dB under the plateau, as
    // NonLinBurstEngineTests measures it on this engine's output (48 kHz). Up to the
    // kLinearTopKneeMs knee: t60 = 1.0216 * knee + 170.2 ms, fitted on knees 40 to 600. From
    // Decay 0.27 s to the fit's top (0.783 s) t60 lands 10 ms short to 2 ms over; no knee
    // reaches -60 dB sooner than about 255 ms, so a Decay under 0.27 s plays up to 55 ms over.
    // Past the fit's top the envelope is very nearly the 283.5 ms knee's moved later (Envelope
    // keeps that knee's shape and fall; the plateau's slope is rescaled to keep its total sag),
    // so t60 moves with the knee one for one (slope 1.0006 measured over knees 600 to 2400 ms):
    // the law goes on from the fit's top point with slope 1, and t60 lands 0 to 14 ms short
    // (7 on average, 1 ms steps) from 0.784 to 2.5 s. Past kMaxDecaySeconds: kMaxKneeMs.
    static float kneeForDecaySeconds (float seconds) noexcept;

    // HF damping: a one-pole lowpass after the burst. At or above
    // kHfDampOffHz it is bypassed (the filter keeps running, so switching it in starts from
    // the signal rather than from stale state). The four target settings sit at 6750,
    // 4250, 2750 and 2250 Hz.
    static constexpr float kHfDampOffHz = 20000.0f;
    void setHfDamp (float hz) noexcept;

    // The shared Treble Multiply control (0.1-1.5) as the HF damping corner: 1.0 and above is
    // off; below it the corner falls exponentially to 2 kHz at 0.1. The four target
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
    // `written`: samples written to the rings since the last clear() or prepare(), this block's
    // included, at most ringLen. Nothing older is read.
    static void sumTaps (const Channel& c, const Tap* taps, int count, int writePos, int ringLen,
                         int written, float* acc, int n) noexcept;

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
    int written_ = 0;                          // samples into the rings since clear()/prepare(), at most ringLen_
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
