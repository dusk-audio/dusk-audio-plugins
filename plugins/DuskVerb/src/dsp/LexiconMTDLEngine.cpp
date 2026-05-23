#include "LexiconMTDLEngine.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    int nextPow2 (int v) { int r = 1; while (r < v) r <<= 1; return r; }

    // Normalized 8x8 Hadamard. H × H^T = 8·I → multiply by 1/sqrt(8)
    // for unitary mix. Unitary mix preserves L2 norm of the FDN state
    // vector; this is the stability invariant that lets us push per-line
    // feedback close to 1.0 without runaway.
    constexpr float kHadamardScale = 0.35355339f;   // 1 / sqrt(8)
    constexpr int kHadamard8[8][8] = {
        { +1, +1, +1, +1, +1, +1, +1, +1 },
        { +1, -1, +1, -1, +1, -1, +1, -1 },
        { +1, +1, -1, -1, +1, +1, -1, -1 },
        { +1, -1, -1, +1, +1, -1, -1, +1 },
        { +1, +1, +1, +1, -1, -1, -1, -1 },
        { +1, -1, +1, -1, -1, +1, -1, +1 },
        { +1, +1, -1, -1, -1, -1, +1, +1 },
        { +1, -1, -1, +1, -1, +1, +1, -1 },
    };
}

// =====================================================================
// Prepare — allocate all buffers. Audio thread never allocates.
// =====================================================================
void LexiconMTDLEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    (void) maxBlockSize;

    // ----- Stage 1: ER delay line -----
    // 32 ms headroom covers max tap (9.79 ms) + stereo offset safety.
    const int erNeeded = (int) std::ceil (0.032 * sampleRate) + 4;
    const int erSz     = nextPow2 (std::max (erNeeded, 64));
    erBufL_.assign ((size_t) erSz, 0.0f);
    erBufR_.assign ((size_t) erSz, 0.0f);
    erBufMask_  = erSz - 1;
    erWriteIdx_ = 0;
    for (int i = 0; i < kNumERTaps; ++i)
        erDelaySamp_[i] = (int) std::round (kERDelayMs[i] * 0.001 * sampleRate);
    erStereoOffsetSamp_ = (int) std::round (erStereoOffsetMs_ * 0.001 * sampleRate);

    // ----- Stage 2: FDN delay lines -----
    // Each prime delay rounded up to next pow2 (× 1.5 headroom for any
    // future per-line modulation).
    for (int i = 0; i < kNumFDNLines; ++i)
    {
        const int len = (int) std::ceil ((float) kFDNDelayPrimes[i] * 1.5f);
        const int sz  = nextPow2 (std::max (len, 64));
        fdnBuf_[i].assign ((size_t) sz, 0.0f);
        fdnBufMask_[i]   = sz - 1;
        fdnWriteIdx_[i]  = 0;
        fdnDelaySamp_[i] = kFDNDelayPrimes[i];
        fdnLineState_[i] = 0.0f;
    }

    // v36 — per-line LFO step magnitudes
    for (int i = 0; i < kNumFDNLines; ++i)
    {
        lfoWalkStep_[i] = 2.0f * kLFORatesHz[i] / (float) sampleRate_;
        lfoState_[i]    = 0.0f;
        lfoVelocity_[i] = 0.0f;
        lineModDepthSamp_[i] = lineModDepthMs_[i] * 0.001f * (float) sampleRate_;
    }

    // v37 — Schroeder allpass cascade buffers (4 stages, single mono
    // cascade fed by mean(dryL,dryR); FDN Hadamard re-decorrelates).
    for (int i = 0; i < kNumSchroeder; ++i)
    {
        const int len = (int) std::ceil (kSchroederMs[i] * 0.001 * sampleRate) + 4;
        const int sz  = nextPow2 (std::max (len, 64));
        schroederBuf_[i].assign ((size_t) sz, 0.0f);
        schroederMask_[i]      = sz - 1;
        schroederWrite_[i]     = 0;
        schroederDelaySamp_[i] = (int) std::round (kSchroederMs[i] * 0.001 * sampleRate);
    }

    // v37 — initialize tilt EQ to 0 dB (flat passthrough)
    setTiltDb (0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        tiltLowS1_[ch] = tiltLowS2_[ch] = 0.0f;
        tiltHighS1_[ch] = tiltHighS2_[ch] = 0.0f;
    }

    recomputeDamping();
    recomputeFDNGains();
    prepared_ = true;
}

void LexiconMTDLEngine::clearBuffers()
{
    std::fill (erBufL_.begin(), erBufL_.end(), 0.0f);
    std::fill (erBufR_.begin(), erBufR_.end(), 0.0f);
    erWriteIdx_ = 0;
    for (int i = 0; i < kNumFDNLines; ++i)
    {
        std::fill (fdnBuf_[i].begin(), fdnBuf_[i].end(), 0.0f);
        fdnWriteIdx_[i]  = 0;
        fdnLineState_[i] = 0.0f;
        lfoState_[i]     = 0.0f;
        lfoVelocity_[i]  = 0.0f;
    }
    for (int i = 0; i < kNumSchroeder; ++i)
    {
        std::fill (schroederBuf_[i].begin(), schroederBuf_[i].end(), 0.0f);
        schroederWrite_[i] = 0;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        tiltLowS1_[ch] = tiltLowS2_[ch] = 0.0f;
        tiltHighS1_[ch] = tiltHighS2_[ch] = 0.0f;
    }
}

// =====================================================================
// Tuning setters (called off audio thread)
// =====================================================================
void LexiconMTDLEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, std::min (10.0f, seconds));
    recomputeFDNGains();
}

void LexiconMTDLEngine::setBandDampingHz (float hz)
{
    const float clamped = std::max (200.0f, std::min (18000.0f, hz));
    for (int i = 0; i < kNumFDNLines; ++i) dampingHz_[i] = clamped;
    recomputeDamping();
}

void LexiconMTDLEngine::setBandDampingHzAt (int lineIdx, float hz)
{
    if (lineIdx < 0 || lineIdx >= kNumFDNLines) return;
    dampingHz_[lineIdx] = std::max (200.0f, std::min (18000.0f, hz));
    recomputeDamping();
}

void LexiconMTDLEngine::setFDNFeedbackAt (int lineIdx, float scale)
{
    if (lineIdx < 0 || lineIdx >= kNumFDNLines) return;
    fbOverridePerLine_[lineIdx] = std::max (0.0f, std::min (2.0f, scale));
    recomputeFDNGains();
}

void LexiconMTDLEngine::setERTapGainDbAt (int tapIdx, float db)
{
    if (tapIdx < 0 || tapIdx >= kNumERTaps) return;
    const float clamped = std::max (-60.0f, std::min (6.0f, db));
    erTapGainLin_[tapIdx] = std::pow (10.0f, clamped * 0.05f);
}

void LexiconMTDLEngine::setLineModDepthMsAt (int lineIdx, float ms)
{
    if (lineIdx < 0 || lineIdx >= kNumFDNLines) return;
    lineModDepthMs_[lineIdx]   = std::max (0.0f, std::min (5.0f, ms));
    lineModDepthSamp_[lineIdx] = lineModDepthMs_[lineIdx] * 0.001f * (float) sampleRate_;
}

void LexiconMTDLEngine::setSchroederCoeff (float coeff)
{
    schroederCoeff_ = std::max (0.0f, std::min (0.95f, coeff));
}

void LexiconMTDLEngine::setTiltDb (float db)
{
    tiltDb_ = std::max (-12.0f, std::min (12.0f, db));

    // RBJ shelving biquads at 1 kHz pivot with Q=0.707 (Butterworth).
    // Low-shelf gets gain = -tilt/2, high-shelf gets gain = +tilt/2.
    // Sum effect: ±tilt dB at extremes, 0 at pivot.
    const float pivotHz = 1000.0f;
    const float sr      = (float) sampleRate_;
    const float w0      = 2.0f * kPi * pivotHz / sr;
    const float cosw0   = std::cos (w0);
    const float sinw0   = std::sin (w0);
    const float Q       = 0.7071068f;
    const float alpha   = sinw0 / (2.0f * Q);

    auto designShelf = [&] (float gainDb, bool isLow, float* b, float* a)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float sqrtA2alpha = 2.0f * std::sqrt (A) * alpha;
        float b0, b1, b2, a0, a1, a2;
        if (isLow)
        {
            b0 =        A * ((A + 1.0f) - (A - 1.0f) * cosw0 + sqrtA2alpha);
            b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
            b2 =        A * ((A + 1.0f) - (A - 1.0f) * cosw0 - sqrtA2alpha);
            a0 =             (A + 1.0f) + (A - 1.0f) * cosw0 + sqrtA2alpha;
            a1 = -2.0f *    ((A - 1.0f) + (A + 1.0f) * cosw0);
            a2 =             (A + 1.0f) + (A - 1.0f) * cosw0 - sqrtA2alpha;
        }
        else
        {
            b0 =        A * ((A + 1.0f) + (A - 1.0f) * cosw0 + sqrtA2alpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
            b2 =        A * ((A + 1.0f) + (A - 1.0f) * cosw0 - sqrtA2alpha);
            a0 =             (A + 1.0f) - (A - 1.0f) * cosw0 + sqrtA2alpha;
            a1 =  2.0f *    ((A - 1.0f) - (A + 1.0f) * cosw0);
            a2 =             (A + 1.0f) - (A - 1.0f) * cosw0 - sqrtA2alpha;
        }
        const float invA0 = 1.0f / a0;
        b[0] = b0 * invA0; b[1] = b1 * invA0; b[2] = b2 * invA0;
        a[0] = a1 * invA0; a[1] = a2 * invA0;
    };

    designShelf (-tiltDb_ * 0.5f, true,  tiltLowB_,  tiltLowA_);
    designShelf (+tiltDb_ * 0.5f, false, tiltHighB_, tiltHighA_);
}

void LexiconMTDLEngine::setFDNFeedbackScale (float scale)
{
    feedbackScale_ = std::max (0.0f, std::min (1.0f, scale));
    recomputeFDNGains();
}

void LexiconMTDLEngine::setERLevel        (float lin) { erLevel_   = std::max (0.0f, lin); }
void LexiconMTDLEngine::setLateLevel      (float lin) { lateLevel_ = std::max (0.0f, lin); }

void LexiconMTDLEngine::setERStereoOffsetMs (float ms)
{
    erStereoOffsetMs_   = std::max (-2.0f, std::min (2.0f, ms));
    erStereoOffsetSamp_ = (int) std::round (erStereoOffsetMs_ * 0.001 * sampleRate_);
}

// =====================================================================
// Coefficient recomputation
// =====================================================================
void LexiconMTDLEngine::recomputeDamping()
{
    // Standard 1-pole LP: y[n] = (1-a)·x[n] + a·y[n-1]
    // a = exp(-2π·fc/sr).  fc closer to sr/2 = brighter (lower a).
    // Per-line: each FDN line gets independent damping cutoff.
    for (int i = 0; i < kNumFDNLines; ++i)
        dampingCoeff_[i] = std::exp (-2.0f * kPi * dampingHz_[i] / (float) sampleRate_);
}

void LexiconMTDLEngine::recomputeFDNGains()
{
    // Per-line feedback gain such that energy decays -60 dB after
    // decayTime_, accounting for each line's circulation period.
    //   g[i] = 10^(-3·D[i] / (T60·sr))
    // feedbackScale_ trims globally for stability margin.
    // v36 — fbOverridePerLine_[i] multiplies on top, letting CMA tune
    // each line's decay independent of damping.
    const float t60sr = decayTime_ * (float) sampleRate_;
    for (int i = 0; i < kNumFDNLines; ++i)
    {
        const float g = std::pow (10.0f, -3.0f * (float) fdnDelaySamp_[i] / t60sr);
        fbGainPerLine_[i] = feedbackScale_ * g * fbOverridePerLine_[i];
    }
}

// =====================================================================
// 8-pt Hadamard mix in-place. O(64) flat — fine for kNumFDNLines=8.
// (For kNumFDNLines=16+ switch to recursive butterfly for O(n log n).)
// =====================================================================
void LexiconMTDLEngine::hadamardMix8 (float* x)
{
    float t[8];
    for (int i = 0; i < 8; ++i)
    {
        float s = 0.0f;
        for (int j = 0; j < 8; ++j)
            s += (float) kHadamard8[i][j] * x[j];
        t[i] = s * kHadamardScale;
    }
    for (int i = 0; i < 8; ++i) x[i] = t[i];
}

// =====================================================================
// process — per-sample loop. NO allocs, NO locks, NO I/O.
// =====================================================================
void LexiconMTDLEngine::process (const float* inL, const float* inR,
                                 float* outL, float* outR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;

    for (int n = 0; n < numSamples; ++n)
    {
        const float dryL = inL[n];
        const float dryR = inR[n];

        // ============================================================
        // STAGE 1 — Feed-Forward ER FIR (LTI-isolated from late tail)
        // ============================================================
        erBufL_[(size_t) erWriteIdx_] = dryL;
        erBufR_[(size_t) erWriteIdx_] = dryR;

        float erOutL = 0.0f, erOutR = 0.0f;
        for (int i = 0; i < kNumERTaps; ++i)
        {
            const int dL = std::max (1, erDelaySamp_[i]);
            const int dR = std::max (1, erDelaySamp_[i] + erStereoOffsetSamp_);
            const int rL = (erWriteIdx_ - dL) & erBufMask_;
            const int rR = (erWriteIdx_ - dR) & erBufMask_;
            erOutL += erBufL_[(size_t) rL] * erTapGainLin_[i];
            erOutR += erBufR_[(size_t) rR] * erTapGainLin_[i];
        }
        erWriteIdx_ = (erWriteIdx_ + 1) & erBufMask_;

        // ============================================================
        // STAGE 2 — FDN read with per-line LFO modulation (v36).
        // Mean-reverting random walk drives a sub-Hz wander on each
        // delay's read position. Fractional 2-tap interpolation.
        // ============================================================
        float lineOut[kNumFDNLines];
        for (int i = 0; i < kNumFDNLines; ++i)
        {
            // xorshift32 PRNG step
            uint32_t s = lfoRng_[i];
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            lfoRng_[i] = s;
            const float w = ((float) (s & 0xFFFFu) * (1.0f / 32768.0f)) - 1.0f;  // [-1, +1]
            // Velocity-based random walk with mean-reverting force
            lfoVelocity_[i] += w * lfoWalkStep_[i] - lfoState_[i] * 0.001f;
            lfoState_[i]    += lfoVelocity_[i];
            if (lfoState_[i] >  1.0f) { lfoState_[i] =  1.0f; lfoVelocity_[i] = 0.0f; }
            if (lfoState_[i] < -1.0f) { lfoState_[i] = -1.0f; lfoVelocity_[i] = 0.0f; }

            const float modDelay = (float) fdnDelaySamp_[i] + lfoState_[i] * lineModDepthSamp_[i];
            int   dInt = (int) modDelay;
            float dFrac = modDelay - (float) dInt;
            if (dInt < 1) { dInt = 1; dFrac = 0.0f; }
            const int idx0 = (fdnWriteIdx_[i] - dInt)     & fdnBufMask_[i];
            const int idx1 = (fdnWriteIdx_[i] - dInt - 1) & fdnBufMask_[i];
            lineOut[i] = fdnBuf_[i][(size_t) idx0] * (1.0f - dFrac)
                       + fdnBuf_[i][(size_t) idx1] * dFrac;
        }

        // Late output tap-sum. Even indices → L, odd → R (hard pan),
        // AND polarity-invert half (lines 2,3,6,7) per v34 stereo fix.
        // Yields strong inter-channel decorrelation that targets the
        // negative stereo_correlation of the Lex Med Hall reference.
        //   L = +l[0] - l[2] + l[4] - l[6]
        //   R = +l[1] - l[3] + l[5] - l[7]
        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < kNumFDNLines; ++i)
        {
            const float sgn = (i & 2) ? -1.0f : +1.0f;
            if (i & 1) lateR += sgn * lineOut[i];
            else       lateL += sgn * lineOut[i];
        }
        // 4 lines per channel — scale by 1/sqrt(4)=0.5 keeps energy sane
        lateL *= 0.5f * lateLevel_;
        lateR *= 0.5f * lateLevel_;

        // ============================================================
        // STAGE 3 — In-loop PER-LINE LP damping (v35 — heterogeneous)
        // Each line has its own dampingCoeff_[i] so CMA can mix sharp
        // (low fc) lines that drive tdc envelope chatter with sustained
        // (high fc) lines that hold rt60.
        // ============================================================
        for (int i = 0; i < kNumFDNLines; ++i)
        {
            const float a_i = dampingCoeff_[i];
            fdnLineState_[i] = (1.0f - a_i) * lineOut[i] + a_i * fdnLineState_[i];
            lineOut[i]       = fdnLineState_[i];
        }

        // ============================================================
        // STAGE 2 — Unitary mix + feedback injection
        // v35: stereo input split. dryL feeds even lines, dryR feeds odd.
        // v37: input passes through Schroeder cascade FIRST (when coeff>0)
        // to multiply modal density before hitting FDN.
        // ============================================================
        hadamardMix8 (lineOut);

        // v37 — Schroeder pre-diffuser. coeff=0 = bypass; coeff > 0
        // engages 4-stage allpass cascade on a mono mix that then
        // splits L/R into even/odd FDN lines.
        float feedL = dryL;
        float feedR = dryR;
        if (schroederCoeff_ > 1.0e-6f)
        {
            const float g = schroederCoeff_;
            float monoIn = 0.5f * (dryL + dryR);
            for (int s = 0; s < kNumSchroeder; ++s)
            {
                const int D    = schroederDelaySamp_[s];
                const int mask = schroederMask_[s];
                const int rd   = (schroederWrite_[s] - D) & mask;
                const float buf_read = schroederBuf_[s][(size_t) rd];
                const float v = monoIn + g * buf_read;
                schroederBuf_[s][(size_t) schroederWrite_[s]] = v;
                schroederWrite_[s] = (schroederWrite_[s] + 1) & mask;
                monoIn = -g * v + buf_read;
            }
            feedL = monoIn;
            feedR = monoIn;
        }

        for (int i = 0; i < kNumFDNLines; ++i)
        {
            const float drySrc = (i & 1) ? feedR : feedL;
            const float write  = drySrc + lineOut[i] * fbGainPerLine_[i];
            fdnBuf_[i][(size_t) fdnWriteIdx_[i]] = write;
            fdnWriteIdx_[i] = (fdnWriteIdx_[i] + 1) & fdnBufMask_[i];
        }

        // ============================================================
        // Final wet mix (ER bypasses tank, sums directly here)
        // v37: post-mix tilt EQ (low-shelf + high-shelf @ 1 kHz pivot)
        //      applied to ER+late combined wet signal.
        // ============================================================
        float wetL = erOutL * erLevel_ + lateL;
        float wetR = erOutR * erLevel_ + lateR;

        if (tiltDb_ > 1.0e-4f || tiltDb_ < -1.0e-4f)
        {
            // Direct-form II Transposed biquad per channel for low-shelf
            // then high-shelf in series.
            auto biquadTDF2 = [] (float x, const float* b, const float* a, float& s1, float& s2)
            {
                const float y = b[0] * x + s1;
                s1 = b[1] * x - a[0] * y + s2;
                s2 = b[2] * x - a[1] * y;
                return y;
            };
            wetL = biquadTDF2 (wetL, tiltLowB_,  tiltLowA_,  tiltLowS1_[0],  tiltLowS2_[0]);
            wetL = biquadTDF2 (wetL, tiltHighB_, tiltHighA_, tiltHighS1_[0], tiltHighS2_[0]);
            wetR = biquadTDF2 (wetR, tiltLowB_,  tiltLowA_,  tiltLowS1_[1],  tiltLowS2_[1]);
            wetR = biquadTDF2 (wetR, tiltHighB_, tiltHighA_, tiltHighS1_[1], tiltHighS2_[1]);
        }

        outL[n] = wetL;
        outR[n] = wetR;
    }
}
