#pragma once

#include <algorithm>
#include <cmath>

// TransientDuck — a per-sample [0,1] gain that OPENS on an input onset, HOLDS
// open for the early-reflection window (~150 ms), then closes, and stays shut
// while the input SUSTAINS. Drives the sparse early-reflection bus so the ER
// fires across the whole early field of a hit (attack_time / early_refl_count /
// early_tap — the reflections land at 70-160 ms, so a duck that only opens AT the
// onset and closes within ~10 ms would silence them) but is silent on a sustained
// sine or noise burst — the whole reason a plain additive ER bus cannot be tuned
// onto the anchor: steady ER energy raises the noiseburst, full_check's gain-match
// then rescales the whole spectrum + the ER's unit-energy 1 kHz throughput pumps
// sine1k, and a duck that AMs on noise adds spectral ripple. Hold-then-close fixes
// all three.
//
// Mechanism: smooth |x| → clean amplitude envelope (~3 ms, kills audio ripple); a
// lagging follower; the normalised onset measure (env-slow)/slow crossing a
// THRESHOLD (deadzone rejects noise wander) arms a HOLD counter of holdMs; while
// held the gate target is 1, else floor_; an output slew smooths open/close so the
// gain never jumps at audio rate (a per-sample jump AM-modulates the ER → THD).
// On a sustained noise burst the onset arms the hold ONCE at the start, then the
// sustained tail (where ripple/ss are measured) sits past the hold, ducked.
//
// Deterministic, allocation-free, RT-safe. Reset in prepare()/clear().
class TransientDuck
{
public:
    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        // envCoeff smooths |x| into a clean AMPLITUDE envelope (~3 ms) — long enough
        // to kill audio-rate ripple (a sustained sine's rectified 2f, a noise burst's
        // grain) so they do NOT read as a train of transients, short enough to keep a
        // snare's few-ms onset edge. slowAtk/Rel then LAG that envelope: at an onset
        // the envelope leaps ahead of its lag (duck fires); on sustain the lag catches
        // up (duck → 0).
        envCoeff_ = coeff (3.0);
        slowAtk_  = coeff (40.0);
        slowRel_  = coeff (160.0);
        // Slew the OUTPUT gain (~6 ms) so it never changes at audio rate — a
        // per-sample gain jump amplitude-modulates the ER and shows up as THD on a
        // sustained tone (the sine1k_odd_thd gate caught it). 6 ms is fast enough to
        // open the duck within a hit's early window, slow enough to be inaudible AM.
        gCoeff_ = coeff (6.0);
        holdSamples_ = static_cast<int> (holdMs_ * 0.001 * sr_);
        reset();
    }

    void reset() { env_ = 0.0f; slow_ = 0.0f; gOut_ = floor_; hold_ = 0; }

    void setHoldMs (float ms) { holdMs_ = std::max (0.0f, ms); holdSamples_ = static_cast<int> (holdMs_ * 0.001 * sr_); }

    void setFloor       (float f) { floor_ = std::clamp (f, 0.0f, 1.0f); }
    // Deadzone on the normalised transient measure: sustained NOISE fluctuates the
    // envelope enough to fire a thresholdless duck (→ AM ripple on the pink-noise
    // gate). Only a sharp onset (silence→hit) clears this, so noise stays ducked.
    void setThreshold   (float t) { thresh_ = std::max (0.0f, t); }

    // Advance one sample from the (mono) detector input; return the duck gain.
    float process (float x)
    {
        env_ = envCoeff_ * env_ + (1.0f - envCoeff_) * std::abs (x);   // smoothed amplitude env
        const float c = (env_ > slow_) ? slowAtk_ : slowRel_;
        slow_ = c * slow_ + (1.0f - c) * env_;                          // lagging follower
        const float t = (env_ - slow_) / (slow_ + 1.0e-5f);            // spikes at onset, ~0 on sustain
        if (t > thresh_) hold_ = holdSamples_;                          // onset (past deadzone) arms the window
        const float target = (hold_ > 0) ? 1.0f : floor_;              // hold open across the early field
        if (hold_ > 0) --hold_;
        gOut_ = gCoeff_ * gOut_ + (1.0f - gCoeff_) * target;           // slew open/close → no audio-rate AM
        return gOut_;
    }

private:
    float coeff (float ms) const
    {
        return std::exp (-1.0f / std::max (1.0f, static_cast<float> (ms * 0.001 * sr_)));
    }

    double sr_ = 44100.0;
    float env_ = 0.0f, slow_ = 0.0f, gOut_ = 0.0f;
    float envCoeff_ = 0.0f, slowAtk_ = 0.0f, slowRel_ = 0.0f, gCoeff_ = 0.0f;
    float floor_ = 0.0f;  // minimum ER gain on sustain (0 = silent)
    float thresh_ = 0.35f; // onset trigger: (env-slow)/slow must exceed this (noise reject)
    float holdMs_ = 150.0f; // window held open after a trigger (spans the early-reflection field)
    int   holdSamples_ = 0, hold_ = 0;
};
