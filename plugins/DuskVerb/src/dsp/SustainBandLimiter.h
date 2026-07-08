#pragma once

#include <cmath>
#include <algorithm>

// SustainBandLimiter — IN-LOOP, band-configurable, drive-following sustained-energy
// limiter (2026-07-07, the sine1k + piano-buildup surgery).
//
// DV's recirculating tanks accumulate energy on SUSTAINED input far above the commercial
// anchors: a ~1 kHz mode pumps the sine1k steady-state +3..+12 dB hot (10 presets), and the
// low band charges over a 22.6 s piano stem (growth/tail/band gates, ~25 instances). A
// FINITE noiseburst/impulse decays normally — the calibrated T60 voicing is correct — so the
// fix must engage ONLY under sustained drive and release before the input-off decay that the
// per-octave T60 gates measure. The prior attempts each missed one half:
//   - DenseHall LowAccumLimiter: in-loop (right place) but 30-300 Hz / −6 dB max (excess runs
//     300-3000 Hz, needs more) and a 6 s always-on release that drains into the T60 fit.
//   - DynamicLowMidLimiter: right law but POST-tank — cannot reduce the in-tank charge, so
//     the post-input tail (piano tail gate) stayed hot at every threshold.
// This one is in-loop AND input-release-keyed. Adversarial pre-review (2026-07-07) fixed two
// more design traps, encoded here:
//   - LAW A (mid/"sine regulator"): the sine1k gate is whole-file RMS of a 2.0 s tone — any
//     slow attack caps the achievable reduction at −3 dB (first uncut second dominates). So
//     the mid config is FAST (full cut within ~0.3 s) and DEEP, threshold ≈ the anchor's
//     in-band steady level → the loop self-regulates toward the anchor. Safe because the
//     sine's in-band level clears every other stimulus by 8-17 dB (measured).
//   - LAW B (low/"charge limiter"): sustained-pink is HOTTER in 62-300 Hz than the piano on
//     every preset, so no absolute threshold separates them. The separator is DURATION:
//     τ_charge ≈ 6-10 s means pink's 4 s render sees ~1/3 of a ≤3 dB cut while the piano's
//     17-22.6 s late window sees ~full cut — which is exactly the shape the one-directional
//     piano-growth gate needs (a fixed cut cancels in its early/late difference).
// Release is INPUT-KEYED: while the (engine-fed) input is present, release = relSlow (an
// effective hold, so the cut deepens monotonically through a sustain); when the input goes
// silent, relFast (≤100 ms) returns the loop gain to its calibrated value with <±6% error on
// even a 0.6 s T60 fit, while the charge already removed stays removed (smaller tail).
//
// gDyn = 1 − red ≤ 1 on the split band only → per-pass loop gain never increases →
// contractive → BIBO-safe (same argument as the FDN low-band transient shaper). `red` is
// slewed by a fixed 20 ms one-pole so gain motion never AM-modulates the tail (the
// AttackRamp-distortion lesson). All state is POD; config derives coeffs on the message
// thread; maxCutDb <= 0 → active=false → callers skip the block and never advance state →
// bit-null.
struct SustainBandLimiter
{
    // ---- shared per-engine config (message-thread writes; plain members, shaper precedent)
    struct Config
    {
        // user params
        float loHz = 60.0f, hiHz = 300.0f;      // band = [loHz, hiHz] via one-pole LP pair
        float threshDb  = -30.0f;               // dBFS of the in-band in-loop envelope
        float maxCutDb  = 0.0f;                 // 0 → inactive/bit-null
        float atkMs     = 150.0f;               // detector charge time (law A ~100-300 ms; law B seconds)
        float relFastMs = 80.0f;                // input-silent release (≤100 ms, T60 protector)
        float relSlowMs = 4000.0f;              // input-present release (hold-ish)
        // derived
        float loCoeff = 0.0f, hiCoeff = 0.0f;
        float atkCoeff = 0.0f, relFastCoeff = 0.0f, relSlowCoeff = 0.0f, slewCoeff = 0.0f;
        float threshLin = 0.03f, maxCutLin = 0.0f;
        // PeakCut detector normalization: the cut-able residual b = x − f(x) scales with
        // the filter DEPTH ((1−A) at fc), so an un-normalized detector starves itself on
        // shallow cuts (measured: cut 5 never engaged where cut 12 overshot). detNorm
        // rescales the feed to raw in-band level semantics — thresholds become
        // depth-independent.
        float detNorm = 1.0f;
        bool  active = false;

        void set (float lo, float hi, float thrDb, float cutDb,
                  float atk, float relF, float relS, double sampleRate)
        {
            loHz = std::clamp (lo, 10.0f, 18000.0f);
            hiHz = std::clamp (hi, loHz + 1.0f, 20000.0f);
            threshDb  = std::clamp (thrDb, -90.0f, 40.0f);   // positive range = RATIO thresholds (the shimmer regeneration detector)
            maxCutDb  = std::clamp (cutDb, 0.0f, 24.0f);
            atkMs     = std::max (atk, 1.0f);
            relFastMs = std::max (relF, 1.0f);
            relSlowMs = std::max (relS, 1.0f);
            updateCoeffs (sampleRate);
        }

        void updateCoeffs (double sampleRate)
        {
            const float sr = static_cast<float> (sampleRate > 1000.0 ? sampleRate : 48000.0);
            loCoeff      = 1.0f - std::exp (-6.28318530718f * loHz / sr);
            hiCoeff      = 1.0f - std::exp (-6.28318530718f * hiHz / sr);
            atkCoeff     = 1.0f - std::exp (-1.0f / (atkMs     * 0.001f * sr));
            relFastCoeff = 1.0f - std::exp (-1.0f / (relFastMs * 0.001f * sr));
            relSlowCoeff = 1.0f - std::exp (-1.0f / (relSlowMs * 0.001f * sr));
            slewCoeff    = 1.0f - std::exp (-1.0f / (0.020f * sr));   // fixed 20 ms gain slew
            threshLin    = std::pow (10.0f, threshDb / 20.0f);
            // maxCutLin = fraction of the band removed at full drive (e.g. 12 dB → 1-10^-0.6)
            maxCutLin    = 1.0f - std::pow (10.0f, -maxCutDb / 20.0f);
            detNorm      = 1.0f / std::max (maxCutLin, 0.05f);
            active       = maxCutDb > 0.01f;
        }
    };

    // ---- shared per-engine detector (ONE gDyn per band — never per-line: 16 independent
    // gains would break the Hadamard mix's orthonormality → coloration + ripple AM)
    struct Detector
    {
        float env = 0.0f, red = 0.0f;
        void clear() { env = red = 0.0f; }

        // Advance from the summed in-band magnitude of the loop signal; returns the current
        // reduction fraction `red` in [0, maxCutLin]. Call ONCE per engine per sample.
        inline float advance (float bandMagSum, const Config& c, bool inputPresent)
        {
            const float rel = inputPresent ? c.relSlowCoeff : c.relFastCoeff;
            env += (bandMagSum > env ? c.atkCoeff : rel) * (bandMagSum - env);
            const float over   = env / c.threshLin;
            const float target = (over > 1.0f) ? std::min (over - 1.0f, 1.0f) * c.maxCutLin : 0.0f;
            red += c.slewCoeff * (target - red);
            return red;
        }

        // PeakCut variant: red is a 0..1 CROSSFADE toward the designed full-depth
        // filter (depth lives in the filter, not in maxCutLin). STEEP knee (×4):
        // full cut at +1.9 dB over threshold, zero below — needed because the
        // sine-vs-pink in-band separation is only ~5 dB, so a soft (over−1) knee
        // could not fully engage on the tone while ignoring sustained pink. The
        // 20 ms slew still smooths the time-domain transition (no AM).
        inline float advanceUnit (float bandMagSum, const Config& c, bool inputPresent)
        {
            const float rel = inputPresent ? c.relSlowCoeff : c.relFastCoeff;
            env += (bandMagSum > env ? c.atkCoeff : rel) * (bandMagSum - env);
            const float over   = env / c.threshLin;
            const float target = std::clamp ((over - 1.0f) * 4.0f, 0.0f, 1.0f);
            red += c.slewCoeff * (target - red);
            return red;
        }
    };

    // ---- per-voice band splitter (per line / per tank channel). band = lpHi − lpLo is the
    // [loHz, hiHz] region; x − red·band removes `red` of that band only.
    struct Splitter
    {
        float lpLo = 0.0f, lpHi = 0.0f;
        void clear() { lpLo = lpHi = 0.0f; }

        inline float band (float x, const Config& c)
        {
            lpLo += c.loCoeff * (x - lpLo);
            lpHi += c.hiCoeff * (x - lpHi);
            return lpHi - lpLo;
        }
    };

    // ---- deep band cut (wet-duck variant). The one-pole band-SUBTRACT above tops out
    // at ~−5 dB for a narrow band (the split's phase lag fights the subtraction —
    // measured on the 400-1500 Hz sine1k config). For the engine-level wet-input duck we
    // instead crossfade toward a DESIGNED RBJ peaking CUT: y = x − red·(x − f(x)) →
    // red=1 gives the full filter response, and a peaking filter has ZERO phase at its
    // center frequency → full depth at fc, near-unity outside the band. fc = √(lo·hi),
    // Q from the band edges, gain = −maxCutDb. Designed on the message thread; with
    // this variant the Detector's `red` is a 0..1 crossfade (advanceUnit), depth lives
    // in the filter design.
    struct PeakCut
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;   // TDF-II state (one instance per channel)
        void clear() { z1 = z2 = 0.0f; }
        void design (float loHz, float hiHz, float cutDb, double sampleRate)
        {
            const float sr = static_cast<float> (sampleRate > 1000.0 ? sampleRate : 48000.0);
            const float fc = std::sqrt (loHz * hiHz);
            const float bw = std::max (hiHz - loHz, 1.0f);
            const float Q  = std::clamp (fc / bw, 0.20f, 8.0f);
            const float A  = std::pow (10.0f, -std::clamp (cutDb, 0.0f, 24.0f) / 40.0f);
            const float w0 = 6.28318530718f * std::min (fc, 0.45f * sr) / sr;
            const float alpha = std::sin (w0) / (2.0f * Q);
            const float cosw  = std::cos (w0);
            const float a0    = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0;
            b1 = (-2.0f * cosw) / a0;
            b2 = (1.0f - alpha * A) / a0;
            a1 = (-2.0f * cosw) / a0;
            a2 = (1.0f - alpha / A) / a0;
            z1 = z2 = 0.0f;
        }
        // Returns the CUT-ABLE component (x − filtered); caller does x − red·band.
        inline float band (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return x - y;
        }
    };

    // ---- input-presence key (ONE per engine): 5 ms attack / 250 ms release envelope on the
    // engine's dry-fed input, gated at ~−60 dBFS. Piano inter-note content never drops out
    // (the stem has no gaps — measured), pink/noiseburst input-off flips it within ~50 ms so
    // relFast can clear the loop gain before the T60 fit window.
    struct InputKey
    {
        float env = 0.0f;
        float atkCoeff = 0.0f, relCoeff = 0.0f;
        void prepare (double sampleRate)
        {
            const float sr = static_cast<float> (sampleRate > 1000.0 ? sampleRate : 48000.0);
            atkCoeff = 1.0f - std::exp (-1.0f / (0.005f * sr));
            relCoeff = 1.0f - std::exp (-1.0f / (0.250f * sr));
            env = 0.0f;
        }
        void clear() { env = 0.0f; }
        inline bool advance (float inputMag)
        {
            env += (inputMag > env ? atkCoeff : relCoeff) * (inputMag - env);
            return env > 0.001f;   // ~−60 dBFS
        }
    };
};
