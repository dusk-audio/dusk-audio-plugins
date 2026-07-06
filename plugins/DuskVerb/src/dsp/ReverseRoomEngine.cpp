#include "ReverseRoomEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ============================================================================
// ReverseRoomEngine — causal rising-ER onset + dark modulated FDN tail.
// Reverse-engineered from lex-reverse-1 (Lexicon PCM Room "Reverse 1").
// See header for the full architecture and the measured reference spec.
// ============================================================================

void ReverseRoomEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_   = maxBlockSize;

    // Tuning sweep override (init thread only — NEVER in process()). No-op when
    // unset → shipping renders use the hard-coded defaults (bit-null/determinism
    // preserved). Format: "rampMs,slope,floorGain,threshDB,holdMs,closeTauMs".
    if (const char* ov = std::getenv ("DUSKVERB_REVERSE"))
    {
        float rm = rampMs_, sl = slope_, fg = floorGain_, thDb = -60.0f,
              hm = holdMs_, ct = closeTauMs_;
        if (std::sscanf (ov, "%f,%f,%f,%f,%f,%f", &rm, &sl, &fg, &thDb, &hm, &ct) >= 1)
        {
            rampMs_     = std::clamp (rm, 5.0f, 1000.0f);
            slope_      = std::clamp (sl, 0.1f, 4.0f);
            floorGain_  = std::clamp (fg, 0.0f, 1.0f);
            threshLin_  = std::pow (10.0f, std::clamp (thDb, -120.0f, 0.0f) / 20.0f);
            holdMs_     = std::clamp (hm, 0.0f, 4000.0f);
            closeTauMs_ = std::clamp (ct, 0.5f, 200.0f);
        }
    }

    // Velvet-noise FIR tail (replaces the FDN — see VelvetTail.h). Baked defaults
    // = the lex-reverse-1 per-band T60 / level / stereo; DUSKVERB_VELVET overrides
    // for hand-tuning without a rebuild. Feed-forward → no FDN mid-decay floor.
    velvet_.prepare (sampleRate, maxBlockSize);

    // ER ring buffers: must hold the longest tap delay (= rampMs_ at the
    // largest size scale ~1.6x) -> size generously to pow2.
    const int maxRampSamp = static_cast<int> (rampMs_ * 0.001 * sampleRate * 2.5) + 64;
    const int ringSize    = DspUtils::nextPowerOf2 (std::max (maxRampSamp, 1024));
    erL_.ring.assign (static_cast<size_t> (ringSize), 0.0f);
    erR_.ring.assign (static_cast<size_t> (ringSize), 0.0f);
    erL_.mask = erR_.mask = ringSize - 1;
    erL_.writePos = erR_.writePos = 0;

    erBufL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erBufR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    // Gate coeffs — recomputed every prepare (track sample-rate changes).
    // One-pole: y += coeff*(target-y), coeff = 1 - exp(-1/(tau*sr)).
    auto onePole = [sr = sampleRate] (double tauSec) -> float
        { return static_cast<float> (1.0 - std::exp (-1.0 / (tauSec * sr))); };
    envAtkCoeff_    = onePole (0.0015);              // 1.5 ms attack (track body)
    envRelCoeff_    = onePole (0.080);               // 80 ms release (< holdMs, no chatter)
    gateOpenCoeff_  = onePole (0.006);               // ~6 ms open (doesn't reshape the FIR swell)
    gateCloseCoeff_ = onePole (closeTauMs_ * 0.001); // close tau → hard cliff, continuous gain
    holdSamples_    = std::max (1, static_cast<int> (holdMs_ * 0.001 * sampleRate));
    holdMaxSamps_   = std::max (holdSamples_, static_cast<int> (holdMaxMs_ * 0.001 * sampleRate));

    rebuildTaps();
    clearBuffers();   // reset gate state + velvet running/biquad state across a
                      // re-prepare (sample-rate / block-size change) — prepare()
                      // alone left gateState_/gateGain_/envFollow_ + velvet crossover
                      // history stale (only clearBuffers() resets them).
    prepared_ = true;
}

void ReverseRoomEngine::clearBuffers()
{
    velvet_.clear();
    std::fill (erL_.ring.begin(), erL_.ring.end(), 0.0f);
    std::fill (erR_.ring.begin(), erR_.ring.end(), 0.0f);
    erL_.writePos = erR_.writePos = 0;
    std::fill (erBufL_.begin(), erBufL_.end(), 0.0f);
    std::fill (erBufR_.begin(), erBufR_.end(), 0.0f);

    // Gate starts CLOSED (pre-onset silence) — reset across preset swap / unfreeze.
    gateState_        = GateState::Idle;
    envFollow_        = 0.0f;
    gateGain_         = 0.0f;
    holdCounter_      = 0;
    inputActiveSamps_ = 0;
}

// Build the rising-gain early-reflection tap sets. Deterministic (fixed seed)
// so every render is bit-identical — the optimizer/gates need reproducibility.
void ReverseRoomEngine::rebuildTaps()
{
    const float sr       = static_cast<float> (sampleRate_);
    const int   rampSamp = std::max (8, static_cast<int> (rampMs_ * 0.001f * sr * sizeScale_));   // rise duration
    const int   minTap   = std::max (1, static_cast<int> (0.002f * sr));   // 2 ms floor
    // density 0 -> 16 taps (sparse "Concrete Stairs"), 1 -> kMaxTaps (dense).
    const int   n        = std::clamp (16 + static_cast<int> (density_ * (kMaxTaps - 16)),
                                       16, kMaxTaps);
    numTaps_ = n;

    for (int ch = 0; ch < 2; ++ch)
    {
        ERChannel& e = (ch == 0) ? erL_ : erR_;
        // Independent L/R pseudo-random sequences for wide stereo decorrelation.
        std::uint32_t s = (ch == 0) ? 0x1234567u : 0x89ABCDEu;
        auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); };

        for (int k = 0; k < n; ++k)
        {
            // FULLY RANDOM tap positions (2026-07-04). The previous
            // roughly-even spacing (+-half-slot jitter) left the tap train
            // periodic: ramp/nTaps ~ 2.8-4.2 ms -> a comb resonance the boing
            // gate read as a massive ringing mode (213 Hz at ramp 470,
            // 289 Hz at ramp 310, up to +27 dB over the anchor). Uniform
            // random draws have no period -> no comb; density is preserved
            // (same tap count over the same ramp).
            const float pf = std::clamp (rnd(), 0.0f, 1.0f);
            int p = minTap + static_cast<int> (pf * static_cast<float> (rampSamp - minTap));
            p = std::clamp (p, 1, e.mask);
            e.pos[k] = p;

            // RISING gain from a floor: g = floor + (1-floor)*(t/ramp)^slope.
            // The floor keeps the onset off digital silence (which made an
            // infinite env_p2p cliff); the rising ramp IS the "reverse" swell.
            // (A bimodal dip-then-bump was tried to chase the reference's
            // env_p2p +24.6 secondary swell, but it broke env_shape_l1 for <1dB
            // of env_p2p — the reference's value comes from its mod+tap-comb
            // interaction, not a clean ER bloom. Kept the clean rising ramp:
            // the +60->+15 cliff fix is the win; +15 vs +24.6 is a smooth tail.)
            const float rel = std::clamp (static_cast<float> (p) / static_cast<float> (rampSamp), 0.0f, 1.0f);
            e.gain[k] = floorGain_ + (1.0f - floorGain_) * std::pow (rel, slope_);
        }

        // Bound the summed output level (sqrt(n) for incoherent taps).
        const float norm = 1.0f / std::sqrt (static_cast<float> (n));
        for (int k = 0; k < n; ++k)
            e.gain[k] *= norm;
    }
}

void ReverseRoomEngine::process (const float* inL, const float* inR,
                                 float* outL, float* outR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    if (numSamples > maxBlock_)
    {
        // Host exceeded prepare()'s maxBlockSize. Process in maxBlock_-sized chunks
        // (sequential → gate / velvet / ER state stays continuous) rather than
        // truncating to maxBlock_ and dropping the rest of the host block.
        int off = 0;
        while (off < numSamples)
        {
            const int n = std::min (maxBlock_, numSamples - off);
            process (inL + off, inR + off, outL + off, outR + off, n);
            off += n;
        }
        return;
    }

    // ── 1) Rising-ER FIR: shape dry input into a swelling early-reflection
    //        burst (the "reverse" onset). Per channel, allocation-free. ──
    for (int ch = 0; ch < 2; ++ch)
    {
        ERChannel&   e   = (ch == 0) ? erL_ : erR_;
        const float* in  = (ch == 0) ? inL : inR;
        float*       out = (ch == 0) ? erBufL_.data() : erBufR_.data();
        const int    mask = e.mask;
        const int    nt   = numTaps_;
        float*       ring = e.ring.data();
        int          wp   = e.writePos;

        for (int i = 0; i < numSamples; ++i)
        {
            ring[wp] = in[i];
            float acc = 0.0f;
            for (int k = 0; k < nt; ++k)
                acc += e.gain[k] * ring[(wp - e.pos[k]) & mask];
            out[i] = acc;
            wp = (wp + 1) & mask;
        }
        e.writePos = wp;
    }

    // ── 2) Velvet-noise FIR tail (feed-forward) fed the swelling ER output, so
    //        the tail inherits the swell. Per-band exp-decay envelopes set the
    //        per-band T60 with NO recirculation → no FDN mid-decay floor. ──
    velvet_.process (erBufL_.data(), erBufR_.data(), outL, outR, numSamples);

    // ── 3) Input-keyed hard gate (the GATED-reverse signature). Keyed off the
    //        DRY input (inL/inR), applied as ONE smoothed scalar to both output
    //        channels: produces the pre-onset silence, the hold-while-input-present
    //        plateau, and the post-peak hard cliff to ~-90 dBFS that collapses the
    //        FDN tail (t60 5.1s -> ~0.06s). `g` is always SLEWED (never assigned)
    //        so both edges are continuous-valued -> no click. Deterministic
    //        post-multiply -> bit-null for the fleet (algo 9 = this 1 preset). ──
    const float atk    = envAtkCoeff_;
    const float rel    = envRelCoeff_;
    const float thr    = threshLin_;
    const float floorL = gateFloorLin_;
    const float openC  = gateOpenCoeff_;
    const float closeC = gateCloseCoeff_;

    float     env     = envFollow_;
    float     g       = gateGain_;
    GateState st      = gateState_;
    int       hc      = holdCounter_;
    int       iActive = inputActiveSamps_;

    for (int i = 0; i < numSamples; ++i)
    {
        // Dry-input peak-envelope follower (fast attack / slow release).
        const float x = std::fabs (inL[i]) + std::fabs (inR[i]);
        env += ((x > env) ? atk : rel) * (x - env);
        const bool present = (env > thr);

        if (present && iActive < holdMaxSamps_ * 16) ++iActive;   // burst-duration accumulator (capped)
        // Duration-dependent hold: base + holdPerSec per second of input presence,
        // capped. Impulse → ≈base (fast tail); sustained → ceiling (bands ring).
        const int effHold = std::min (holdMaxSamps_,
                                      holdSamples_ + static_cast<int> (holdPerSec_ * 0.001f * iActive));

        // Retriggerable hold state machine.
        switch (st)
        {
            case GateState::Idle:    if (present) { st = GateState::Open; iActive = 1; } break;
            case GateState::Open:    if (! present) { st = GateState::Hold; hc = effHold; } break;
            case GateState::Hold:    if (present) st = GateState::Open;            // retrigger
                                     else if (--hc <= 0) st = GateState::Closing; break;
            case GateState::Closing: if (present) { st = GateState::Open; iActive = 1; } break;  // retrigger
        }

        const float target = (st == GateState::Open || st == GateState::Hold) ? 1.0f : floorL;
        g += ((target > g) ? openC : closeC) * (target - g);   // always slewed → continuous

        outL[i] *= g;
        outR[i] *= g;
    }

    envFollow_        = env;
    gateGain_         = g;
    gateState_        = st;
    holdCounter_      = hc;
    inputActiveSamps_ = iActive;
}

// ── Universal setters ──────────────────────────────────────────────────────
// The velvet tail has no broadband tone/mod/saturation/loop, and the per-band
// tail tone is the engine's baked "Reverse" signature (tuned via the velvet
// defaults + DUSKVERB_VELVET). So the tone/mod setters are NO-OPS — the single
// algo-9 preset never disturbs the calibrated tail through them. Decay maps to a
// global tail-length scale so the Decay knob still does something musical.
void ReverseRoomEngine::setDecayTime (float seconds)   { velvet_.setGlobalDecayScale (std::max (0.05f, seconds) / 0.21f); }
void ReverseRoomEngine::setBassMultiply (float)        {}
void ReverseRoomEngine::setMidMultiply (float)         {}
void ReverseRoomEngine::setTrebleMultiply (float)      {}
void ReverseRoomEngine::setCrossoverFreq (float)       {}
void ReverseRoomEngine::setHighCrossoverFreq (float)   {}
void ReverseRoomEngine::setSaturation (float)          {}
void ReverseRoomEngine::setModDepth (float)            {}
void ReverseRoomEngine::setModRate (float)             {}
void ReverseRoomEngine::setTailSpinDepth (float)       {}
void ReverseRoomEngine::setTailSpinRate (float)        {}
void ReverseRoomEngine::setFreeze (bool)               {}

void ReverseRoomEngine::setSize (float size)
{
    velvet_.setSizeScale (0.6f + 1.0f * std::clamp (size, 0.0f, 1.0f));
    // 0..1 size -> 0.6..1.6x ER onset span (longer onset on bigger rooms).
    const float newScale = 0.6f + 1.0f * std::clamp (size, 0.0f, 1.0f);
    if (std::abs (newScale - sizeScale_) > 1.0e-4f)
    {
        sizeScale_ = newScale;
        if (prepared_) rebuildTaps();
    }
}

void ReverseRoomEngine::setTankDiffusion (float amount)
{
    const float a = std::clamp (amount, 0.0f, 1.0f);
    if (std::abs (a - density_) > 1.0e-4f)
    {
        density_ = a;
        if (prepared_) rebuildTaps();
    }
}
