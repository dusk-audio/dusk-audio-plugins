#pragma once

namespace pre35
{

/**
    The mic amp clipping into its supply rails.

    Measured 2026-08-12 (dusk-audio-tools sessions/ch1-rail-20260812): a
    memoryless voltage threshold, identical at 315 Hz, 1 kHz and 5 kHz to 0.03 dB
    of gain and 0.5 dB of third harmonic. No slew limiting, nothing frequency
    dependent, no state. One static curve covers the whole band, and this class
    holds no state for the same reason the amplifier does not.

    The corner is hard, not soft. The hardware is flat to 0.000 dB one dB below
    onset and its third harmonic then jumps 55 dB across that single dB. A
    soft-knee family was fitted first and its knee parameter pinned at whatever
    bound it was given (n = 100, 200, 400, 1000 all fit identically), so it was
    measuring nothing; a plain hard clip reproduces the ladder to 0.017 dB of
    gain and 0.7 dB of h3/h5.

    The two thresholds differ slightly. h2 sits at -43..-49 dBc across every
    clipped level and all three measured frequencies, far too consistent to be
    noise, and a symmetric clipper produces no h2 at all.

    On antialiasing
    ---------------
    Deliberately none, and this is measured rather than assumed. First-order
    ADAA was implemented and then removed: inside the core's 8x oversampling
    (384 kHz) it improved worst-case in-band aliasing by 21 dB for a 15 kHz tone
    driven 6 dB into the rail, and by nothing at all at 5 kHz and 10 kHz, while
    costing -0.117 dB at 20 kHz on *every* signal including those that never
    clip. ADAA's difference quotient is a two-tap average, so that loss is
    unavoidable and permanent. For a unity-gain plugin whose material mostly
    stays clean, a guaranteed HF tilt larger than the response model's own 0.09 dB
    fit error is the worse failure. Naive clipping at 8x keeps worst-case in-band
    aliasing at -56 dBFS, roughly 43 dB below the distortion products that are
    the point of driving it there.

    A related trap, in case anyone tries the obvious hybrid: passing through
    unchanged below threshold and using ADAA only across the corner is *worse
    than either*. It measured 8.9 dB worse than naive, because the two branches
    differ by a half-sample delay and switching between them injects a
    discontinuity at every threshold crossing.
*/
class RailClip
{
public:
    /** Thresholds are magnitudes in the model's internal amp-output units. */
    void setThresholds(double positive, double negative) noexcept
    {
        posLimit = positive;
        negLimit = negative;
    }

    /** Memoryless, so there is nothing to clear. Present for symmetry with the
        other stages, whose reset() the DSP calls in a block. */
    void reset() noexcept {}

    double processSample(double x) const noexcept
    {
        if (x > posLimit)  return posLimit;
        if (x < -negLimit) return -negLimit;
        return x;
    }

private:
    // Effectively bypassed until configured, so a half-built chain passes audio
    // through rather than gating it to silence.
    double posLimit = 1.0e30;
    double negLimit = 1.0e30;
};

} // namespace pre35
