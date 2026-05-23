#include "LexFigure8Reverb.h"
#include <algorithm>
#include <cmath>

static int nextPow2 (int v)
{
    int r = 1;
    while (r < v) r <<= 1;
    return r;
}

void LexFigure8Reverb::prepare (double sampleRate, int maxBlockSize)
{
    tank.prepare (sampleRate, maxBlockSize);
    tank.setHallScale (true);
    // Phase B 4th-density-AP path tested with multiple delay
    // configurations (691/743 long; 83/89 short). Both regressed
    // spectral_crest (+2.96 / +2.28 vs v5 +1.30) without recovering
    // box_ratio. Default stays at 3 stages. Setter retained for
    // experimentation. Engine 10 + plates unaffected (default 3).
    tank.setDensityStages (3);
    sampleRate_ = sampleRate;

    const int needed = (int) std::ceil (0.032 * sampleRate) + 4;
    const int sz     = nextPow2 (std::max (needed, 64));
    erBuf_.assign ((size_t) sz, 0.0f);
    erBufMask_  = sz - 1;
    erWriteIdx_ = 0;

    tankInL_.assign ((size_t) maxBlockSize, 0.0f);
    tankInR_.assign ((size_t) maxBlockSize, 0.0f);
    erOutL_ .assign ((size_t) maxBlockSize, 0.0f);
    erOutR_ .assign ((size_t) maxBlockSize, 0.0f);
    erGate_ .assign ((size_t) maxBlockSize, 1.0f);

    // Tank pre-delay buffer (max 25 ms headroom)
    const int pdNeeded = (int) std::ceil (0.025 * sampleRate) + 4;
    const int pdSz     = nextPow2 (std::max (pdNeeded, 64));
    tankPreDelayL_.assign ((size_t) pdSz, 0.0f);
    tankPreDelayR_.assign ((size_t) pdSz, 0.0f);
    tankPreDelayMask_  = pdSz - 1;
    tankPreDelayWrite_ = 0;
    tankPreDelaySamp_  = (int) std::round (tankPreDelayMs_ * 0.001f * (float) sampleRate);
    if (tankPreDelaySamp_ < 0) tankPreDelaySamp_ = 0;
    if (tankPreDelaySamp_ > pdSz - 1) tankPreDelaySamp_ = pdSz - 1;

    // Smoke J — edge-triggered click synth buffer (max 0.4 s headroom)
    const int ckNeeded = (int) std::ceil (0.4 * sampleRate) + 4;
    const int ckSz     = nextPow2 (std::max (ckNeeded, 64));
    clickImpulseBuf_.assign ((size_t) ckSz, 0.0f);
    clickImpulseMask_  = ckSz - 1;
    clickImpulseWrite_ = 0;
    for (int i = 0; i < kNumClickDelays; ++i)
        clickDelaySamp_[i] = (int) std::round (kClickDelayMs[i] * 0.001f * sampleRate);
    clickRefractorySamp_      = (int) std::round (clickRefractoryMs_ * 0.001f * sampleRate);
    clickRefractoryCountdown_ = 0;
    clickPrevInput_           = 0.0f;
    clickBurstLenSamp_        = (int) std::round (0.005 * sampleRate);   // 5 ms boxcar
    clickBurstCountdown_      = 0;

    recomputeTapSamples();
    recomputeDuckerCoeffs();
    duckerEnvState_  = 0.0f;
    duckerSlowState_ = 0.0f;
    duckerEnergyState_ = 0.0f;
    duckerGateState_   = 0.0f;
    for (int i = 0; i < kEnergyWindowSize; ++i) duckerEnergyBuf_[i] = 0.0f;
    duckerEnergyIdx_   = 0;
    duckerEnergySum_   = 0.0f;
    prepared_ = true;
}

void LexFigure8Reverb::clearBuffers()
{
    tank.clearBuffers();
    std::fill (erBuf_.begin(), erBuf_.end(), 0.0f);
    erWriteIdx_ = 0;
    std::fill (clickImpulseBuf_.begin(), clickImpulseBuf_.end(), 0.0f);
    clickImpulseWrite_        = 0;
    clickRefractoryCountdown_ = 0;
    clickPrevInput_           = 0.0f;
}

void LexFigure8Reverb::setERTapDelay (int idx, float ms)
{
    if (idx < 0 || idx >= kNumERTaps) return;
    tapDelayMs_[idx] = std::max (0.0f, std::min (30.0f, ms));
    recomputeTapSamples();
}

void LexFigure8Reverb::setERTapGainDb (int idx, float db)
{
    if (idx < 0 || idx >= kNumERTaps) return;
    const float clamped = std::max (-60.0f, std::min (6.0f, db));
    tapGainLin_[idx] = std::pow (10.0f, clamped * 0.05f);
}

void LexFigure8Reverb::setERStereoOffset (float ms)
{
    stereoOffsetMs_ = std::max (-2.0f, std::min (2.0f, ms));
    recomputeTapSamples();
}

void LexFigure8Reverb::setTankInputScale (float scale)
{
    tankInputScale_ = std::max (0.0f, std::min (1.0f, scale));
}

void LexFigure8Reverb::setTankPreDelay (float ms)
{
    tankPreDelayMs_ = std::max (0.0f, std::min (25.0f, ms));
    const int bufSz = (int) tankPreDelayL_.size();
    if (bufSz > 1)
    {
        int n = (int) std::round (tankPreDelayMs_ * 0.001f * (float) sampleRate_);
        if (n < 0) n = 0;
        if (n > bufSz - 1) n = bufSz - 1;
        tankPreDelaySamp_ = n;
    }
}

void LexFigure8Reverb::setDuckerThreshold (float thresh)
{
    duckerThreshold_ = std::max (0.0f, std::min (1.0f, thresh));
    duckerActive_ = (duckerDepth_ > 1.0e-4f);
}

void LexFigure8Reverb::setDuckerAttackMs (float ms)
{
    duckerAttackMs_ = std::max (0.01f, std::min (50.0f, ms));
    recomputeDuckerCoeffs();
}

void LexFigure8Reverb::setDuckerReleaseMs (float ms)
{
    duckerReleaseMs_ = std::max (0.1f, std::min (500.0f, ms));
    recomputeDuckerCoeffs();
}

void LexFigure8Reverb::setDuckerDepth (float depth)
{
    duckerDepth_ = std::max (0.0f, std::min (1.0f, depth));
    duckerActive_ = (duckerDepth_ > 1.0e-4f);
}

void LexFigure8Reverb::recomputeDuckerCoeffs()
{
    const float sr = (float) sampleRate_;
    duckerAttackCoeff_  = std::exp (-1.0f / (duckerAttackMs_  * 0.001f * sr));
    duckerReleaseCoeff_ = std::exp (-1.0f / (duckerReleaseMs_ * 0.001f * sr));
    // Slow envelope: ~30 ms time constant. Tracks steady-state level
    // so transient detector subtracts it out → only fast attacks
    // trigger ducking. Noiseburst steady-state → fast ≈ slow → no duck.
    duckerSlowCoeff_ = std::exp (-1.0f / (0.030f * sr));
}

void LexFigure8Reverb::recomputeTapSamples()
{
    for (int i = 0; i < kNumERTaps; ++i)
        tapDelaySamp_[i] = tapDelayMs_[i] * 0.001f * (float) sampleRate_;
    stereoOffsetSamp_ = stereoOffsetMs_ * 0.001f * (float) sampleRate_;
}

void LexFigure8Reverb::process (const float* rawInL, const float* rawInR,
                                const float* tankInL_param, const float* tankInR_param,
                                float* outL, float* outR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;

    // Tank input pipeline: pre-delay → static scale → DYNAMIC DUCKER → tank.
    // Phase H: non-LTI ducker. Envelope follower runs on RAW input (rawInL/R)
    // and modulates per-sample gain on the FILTERED tankInL_param/R. ER taps
    // (computed below from rawInL/R) bypass the ducker entirely — when the
    // raw impulse arrives, the envelope spikes → ducker clamps tank → ER
    // fills the early window uncontested. For noiseburst, envelope settles
    // to steady state → release engages → tank receives full energy.
    const bool needScale    = tankInputScale_ < 0.9999f;
    const bool needPreDelay = tankPreDelaySamp_ > 0;
    const bool needDucker   = duckerActive_;
    const float s = needScale ? tankInputScale_ : 1.0f;

    if (needScale || needPreDelay || needDucker)
    {
        if ((int) tankInL_.size() < numSamples)
        {
            tankInL_.assign ((size_t) numSamples, 0.0f);
            tankInR_.assign ((size_t) numSamples, 0.0f);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float srcL = tankInL_param[i];
            float srcR = tankInR_param[i];

            if (needPreDelay)
            {
                const int mask = tankPreDelayMask_;
                const int dly  = tankPreDelaySamp_;
                tankPreDelayL_[(size_t) tankPreDelayWrite_] = srcL;
                tankPreDelayR_[(size_t) tankPreDelayWrite_] = srcR;
                const int readIdx = (tankPreDelayWrite_ - dly) & mask;
                srcL = tankPreDelayL_[(size_t) readIdx];
                srcR = tankPreDelayR_[(size_t) readIdx];
                tankPreDelayWrite_ = (tankPreDelayWrite_ + 1) & mask;
            }

            // v24 Energy-Veto with TRUE 32-tap rolling sum (matches WAV
            // analysis). DV impulse = single 1.0 sample → 32-sum = 1.0.
            // DV noiseburst (pink noise ±0.5) → 32-sum ≈ 4-5 sustained.
            // Veto at 1.5 cleanly isolates impulse, blocks noiseburst.
            float duckGain = 1.0f;
            if (needDucker)
            {
                const float absIn = std::max (std::abs (rawInL[i]), std::abs (rawInR[i]));
                // 32-tap circular buffer rolling sum.
                duckerEnergySum_ -= duckerEnergyBuf_[duckerEnergyIdx_];
                duckerEnergyBuf_[duckerEnergyIdx_] = absIn;
                duckerEnergySum_ += absIn;
                duckerEnergyIdx_ = (duckerEnergyIdx_ + 1) & (kEnergyWindowSize - 1);

                const float energyVeto = 1.5f;
                const bool trigger = (absIn > duckerThreshold_)
                                  && (duckerEnergySum_ < energyVeto);
                if (trigger)
                    duckerGateState_ = 1.0f;
                else if (duckerEnergySum_ > energyVeto)
                    duckerGateState_ = 0.0f;
                else
                    duckerGateState_ *= duckerReleaseCoeff_;
                duckGain = 1.0f - duckerDepth_ * duckerGateState_;
                erGate_[(size_t) i] = duckerGateState_;
            }
            else
            {
                erGate_[(size_t) i] = 1.0f;
            }

            tankInL_[(size_t) i] = srcL * s * duckGain;
            tankInR_[(size_t) i] = srcR * s * duckGain;
        }
        tank.process (tankInL_.data(), tankInR_.data(), outL, outR, numSamples);
    }
    else
    {
        tank.process (tankInL_param, tankInR_param, outL, outR, numSamples);
    }

    // ER pipeline — captures RAW input (rawInL/R, pre-everything). Writes
    // into separate erOutL_/erOutR_ buffers. The engine layer adds these
    // to the final output AFTER its shell filters, so ER taps stay
    // pristine — no filter ringing, no AP cascade pre-ringing, no
    // smoother attenuation.
    const int bufSz = (int) erBuf_.size();
    if (bufSz <= 1) return;

    const float maxDelay = (float) (bufSz - 2);

    for (int n = 0; n < numSamples; ++n)
    {
        const float in = 0.5f * (rawInL[n] + rawInR[n]);
        erBuf_[(size_t) erWriteIdx_] = in;

        float erL = 0.0f;
        float erR = 0.0f;
        for (int i = 0; i < kNumERTaps; ++i)
        {
            float dL = tapDelaySamp_[i];
            float dR = tapDelaySamp_[i] + stereoOffsetSamp_;
            if (dL < 1.0f) dL = 1.0f;
            if (dR < 1.0f) dR = 1.0f;
            if (dL > maxDelay) dL = maxDelay;
            if (dR > maxDelay) dR = maxDelay;

            const int   diL = (int) dL;
            const float frL = dL - (float) diL;
            const int   i0L = (erWriteIdx_ - diL) & erBufMask_;
            const int   i1L = (i0L - 1) & erBufMask_;
            const float vL  = erBuf_[(size_t) i0L] * (1.0f - frL)
                            + erBuf_[(size_t) i1L] * frL;

            const int   diR = (int) dR;
            const float frR = dR - (float) diR;
            const int   i0R = (erWriteIdx_ - diR) & erBufMask_;
            const int   i1R = (i0R - 1) & erBufMask_;
            const float vR  = erBuf_[(size_t) i0R] * (1.0f - frR)
                            + erBuf_[(size_t) i1R] * frR;

            erL += vL * tapGainLin_[i];
            erR += vR * tapGainLin_[i];
        }

        // Apply transient gate to ER output ONLY when ducker active.
        // Without ducker, erGate_ may hold stale values → skip gating.
        const float gate = duckerActive_ ? erGate_[(size_t) n] : 1.0f;

        // Smoke J — edge-triggered click synth with 5ms BURST (boxcar) emission
        // Each trigger starts a 5ms (240-sample) sustained 1.0 burst into the
        // delay buffer. After Hilbert+5ms smoothing in tdc metric, this burst
        // appears as a peak of full amplitude (not amp/240 like a single-sample).
        const float delta = in - clickPrevInput_;
        clickPrevInput_   = in;
        if (clickRefractoryCountdown_ <= 0 && delta >= clickThresh_)
        {
            clickBurstCountdown_      = clickBurstLenSamp_;
            clickRefractoryCountdown_ = clickRefractorySamp_;
        }
        const float burstSig = (clickBurstCountdown_ > 0) ? 1.0f : 0.0f;
        if (clickBurstCountdown_ > 0) --clickBurstCountdown_;
        if (clickRefractoryCountdown_ > 0) --clickRefractoryCountdown_;

        clickImpulseBuf_[(size_t) clickImpulseWrite_] = burstSig;
        float clickSum = 0.0f;
        for (int i = 0; i < kNumClickDelays; ++i)
        {
            const int readIdx = (clickImpulseWrite_ - clickDelaySamp_[i]) & clickImpulseMask_;
            clickSum += clickImpulseBuf_[(size_t) readIdx];
        }
        clickImpulseWrite_ = (clickImpulseWrite_ + 1) & clickImpulseMask_;
        const float clickContrib = clickSum * clickGainLin_;

        erOutL_[(size_t) n] = erL * gate + clickContrib;
        erOutR_[(size_t) n] = erR * gate + clickContrib;   // SAME sign — mono-summable

        erWriteIdx_ = (erWriteIdx_ + 1) & erBufMask_;
    }
}
