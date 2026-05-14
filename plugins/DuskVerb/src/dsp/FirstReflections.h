#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// =====================================================================
// FirstReflections — sparse 2-tap specular reflection injector
// =====================================================================
//
// PURPOSE
// -------
// Adds a single delayed copy of the input to each output channel — one
// "left" tap and one "right" tap, each with independent delay + gain +
// HF damping. Sits BEFORE the smooth EarlyReflections + tank cascade so
// the wet output has a sharp specular peak in the first ~10 ms of the
// onset window, before any diffuse-tail buildup.
//
// WHY THIS EXISTS
// ---------------
// The diffuse modules in DuskVerb (ParallelDiffuser for SixAPTank,
// EarlyReflections with 24 dense taps at 8–80 ms, plus the tank cascade
// itself) produce a smooth GRADUAL onset. Measured against the Lexicon
// PCM Native "Concert Hall" preset (anchor for Smooth Concert Hall),
// the structural difference is:
//
//   Lex IR onset  : 0–5 ms    -38 dBrms / -17 dB peak  ← L_Rfl @ 3 ms
//                   5–11 ms   -42 dBrms / -20 dB peak  ← R_Rfl @ 8 ms
//                   11–25 ms  -144 dBrms (SILENT — between specular refs
//                                          and start of diffuse tail)
//                   25 ms+    diffuse tail begins
//
//   DV (pre-fix)  : 0–5 ms    -131 dBrms (SILENT — no direct path)
//                   5+ ms     gradual buildup of dense ParallelDiffuser
//                             reflections — loudest sample around 60 ms
//
// Result: Lex's loudest sample is its FIRST specular reflection; DV's
// loudest sample is the diffuse-tail buildup. d50 (early/late energy
// ratio in dB) reflects this: Lex +3.5 vs DV +0.7 before this module.
//
// FirstReflections closes that gap by injecting the two specular taps
// directly from the raw input. Tap timings (L=3 ms, R=8 ms) and gains
// (L=0 dB, R=-3 to -8 dB) come straight from Lexicon's preset XML for
// "Concert Hall": L_Rfl_Dly=3 ms / R_Rfl_Dly=8 ms / L_Rfl_Gain=0 dB /
// R_Rfl_Gain=-3 dB. Other hall-style presets can dial similar values.
//
// HF DAMPING
// ----------
// Each tap is followed by a 1-pole lowpass to model air absorption /
// surface friction on the specular bounce. Lex's "Rvb Out Freq" knob
// rolls the wet output at 7 kHz; the specular reflections in a real
// hall lose similar high-frequency content over their travel distance.
// Without this, the raw bandwidth-flat taps over-bright the IR by ~5 dB
// at >5 kHz and push the treble_ratio metric above hall norms.
// Default cutoff = 20 kHz (effective bypass for presets that don't opt
// in). Smooth Concert Hall sets 5–12 kHz depending on optimizer pass.
//
// SAFETY
// ------
// Default gains = 0 (linear) → entire module is a no-op (early-bail in
// processAdd). Existing presets that don't add the four new factory-
// preset fields produce byte-identical output to before this module
// existed.
// =====================================================================
class FirstReflections
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = static_cast<float> (sampleRate);
        // 60 ms ring buffer — covers any plausible specular delay (typical
        // halls: 1-30 ms; this allows up to ~50 ms with the 10 ms safety
        // margin against integer-vs-float delay rounding). We never wrap
        // mid-block since we read indexed-back, not block-shifted, so no
        // extra block headroom is needed.
        const int len = static_cast<int> (std::ceil (sampleRate * 0.060));
        bufferL_.assign (static_cast<size_t> (len), 0.0f);
        bufferR_.assign (static_cast<size_t> (len), 0.0f);
        writePos_ = 0;
        // Mask is unused for now — kept as a sentinel in case we later
        // switch to power-of-two buffer + bitmask wrap for the read index
        // (saves one branch per sample). Current modulo-by-add is fine
        // because the buffer is small enough to stay in L1 cache.
        mask_ = static_cast<int> (bufferL_.size()) - 1;
        // Re-design HF lowpass at the new sample rate (coefficient depends
        // on sr) and zero the filter state to prevent denormal hangover.
        setHFCutHz (hfCutHz_);
        hfStateL_ = hfStateR_ = 0.0f;
    }

    void clear()
    {
        std::fill (bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill (bufferR_.begin(), bufferR_.end(), 0.0f);
        writePos_ = 0;
        hfStateL_ = hfStateR_ = 0.0f;
    }

    // ── Tap delay setters ──────────────────────────────────────────────
    // Delay is clamped to [1 sample, bufferLen - 1] so we never read
    // ahead of the write pointer in the same iteration (which would
    // return stale state from the prior buffer wraparound).
    void setLeftDelaySamples  (float s) { leftDelay_  = std::clamp (s, 1.0f, static_cast<float> (bufferL_.size() - 1)); }
    void setRightDelaySamples (float s) { rightDelay_ = std::clamp (s, 1.0f, static_cast<float> (bufferR_.size() - 1)); }
    void setLeftDelayMs  (float ms) { setLeftDelaySamples  (ms * 1.0e-3f * sampleRate_); }
    void setRightDelayMs (float ms) { setRightDelaySamples (ms * 1.0e-3f * sampleRate_); }

    // ── Tap gain setters ───────────────────────────────────────────────
    // -60 dB and below treated as full mute (linear=0) so the processAdd
    // fast-path can bail without computing the buffer write/read. Above
    // -60 dB uses standard dB→linear conversion.
    void setLeftGain  (float linear)  { leftGain_  = linear; }
    void setRightGain (float linear)  { rightGain_ = linear; }
    void setLeftGainDb  (float db) { leftGain_  = (db <= -60.0f) ? 0.0f : std::pow (10.0f, db / 20.0f); }
    void setRightGainDb (float db) { rightGain_ = (db <= -60.0f) ? 0.0f : std::pow (10.0f, db / 20.0f); }

    // ── HF lowpass setter ──────────────────────────────────────────────
    // Single 1-pole lowpass shared across L+R taps (real halls absorb
    // HF similarly on both early reflections; no reason to expose
    // per-channel cutoff complexity to the user). 100 Hz lower bound
    // prevents pathological DC-rejection-style filtering; 20 kHz upper
    // bound is the practical Nyquist ceiling at 44.1/48 kHz host SRs.
    //
    // Filter coefficient math: 1-pole LPF transfer function
    //   y[n] = a · x[n] + (1 - a) · y[n-1]
    // Reformulated as the leaky integrator form used in processAdd:
    //   y[n] += a · (x[n] - y[n-1])
    // The pole moves toward DC as fc → 0. Exact pole placement:
    //   a = 1 - exp(-2π · fc / sr)
    // This is the standard "matched-z" 1-pole; preserves analog cutoff
    // location across SR changes (44.1k vs 48k vs 96k host sample rate).
    void setHFCutHz (float hz)
    {
        hfCutHz_ = std::clamp (hz, 100.0f, 20000.0f);
        const float w = 6.283185307179586f * hfCutHz_ / sampleRate_;
        hfCoeff_ = 1.0f - std::exp (-w);
    }

    // ── Process: add tap output INTO outputL/R (does NOT replace) ─────
    // Sums into the caller's output so this module can be inserted into
    // an existing mix bus without zeroing other contributions. Caller
    // is expected to either pre-clear outputL/R or have other DSP write
    // into them first.
    void processAdd (const float* inputL, const float* inputR,
                     float* outputL, float* outputR, int numSamples)
    {
        // Fast bail when both taps muted — preset hasn't opted in, so
        // skip the entire buffer write + read + filter cycle. Critical
        // because every active engine in DuskVerb instantiates this
        // class regardless of preset; defaults must be free of cost.
        if (leftGain_ == 0.0f && rightGain_ == 0.0f)
            return;

        const int bufLen        = static_cast<int> (bufferL_.size());
        const int leftDelayInt  = static_cast<int> (leftDelay_);
        const int rightDelayInt = static_cast<int> (rightDelay_);

        for (int n = 0; n < numSamples; ++n)
        {
            // Write current input into the L/R ring buffers.
            bufferL_[static_cast<size_t> (writePos_)] = inputL[n];
            bufferR_[static_cast<size_t> (writePos_)] = inputR[n];

            // Read each tap from its own delay offset. Wrap negative
            // indices by adding bufLen — no integer modulo needed since
            // we never read more than one bufLen back.
            int readL = writePos_ - leftDelayInt;
            int readR = writePos_ - rightDelayInt;
            if (readL < 0) readL += bufLen;
            if (readR < 0) readR += bufLen;

            // Apply per-tap gain BEFORE the HF filter — keeps the filter
            // in its linear range and avoids amplifying any quantisation
            // noise from the 1-pole's recursive state.
            float tapL = bufferL_[static_cast<size_t> (readL)] * leftGain_;
            float tapR = bufferR_[static_cast<size_t> (readR)] * rightGain_;

            // Leaky-integrator 1-pole LPF (air absorption). hfCoeff_
            // ranges 0 (full HF cut, output frozen at last value) to 1
            // (full passthrough at fc=Nyquist). At default 20 kHz
            // coeff ≈ 1, so the filter acts as a near-unity passthrough
            // — confirmed inaudible vs no filter for default presets.
            hfStateL_ += hfCoeff_ * (tapL - hfStateL_);
            hfStateR_ += hfCoeff_ * (tapR - hfStateR_);

            outputL[n] += hfStateL_;
            outputR[n] += hfStateR_;

            ++writePos_;
            if (writePos_ >= bufLen) writePos_ = 0;
        }
    }

private:
    // ── Sample-rate / buffer state ─────────────────────────────────────
    float sampleRate_   = 48000.0f;
    std::vector<float> bufferL_, bufferR_;
    int   writePos_   = 0;
    int   mask_       = 0;        // reserved for future pow-2 fast path

    // ── Tap configuration (per-channel) ────────────────────────────────
    // Defaults match Lexicon PCM Native "Concert Hall" specular timings
    // so a class instance is immediately Lex-Concert-Hall-ready once a
    // preset un-mutes the gains.
    float leftDelay_  = 144.0f;   // 3.0 ms @ 48 kHz — Lex L_Rfl_Dly default
    float rightDelay_ = 384.0f;   // 8.0 ms @ 48 kHz — Lex R_Rfl_Dly default
    float leftGain_   = 0.0f;     // muted by default — preset must opt in
    float rightGain_  = 0.0f;

    // ── 1-pole HF lowpass (shared L+R cutoff) ──────────────────────────
    float hfCutHz_    = 20000.0f; // 20 kHz default = effective bypass
    float hfCoeff_    = 1.0f;     // recomputed by setHFCutHz; 1.0 = full passthrough
    float hfStateL_   = 0.0f;     // per-channel filter state (recursive y[n-1])
    float hfStateR_   = 0.0f;
};
