#include "DattorroPlateVintage.h"

#include <algorithm>
#include <cmath>

void DattorroPlateVintage::prepare (double sampleRate, int maxBlockSize)
{
    tank_.prepare (sampleRate, maxBlockSize);

    sampleRate_ = static_cast<float> (sampleRate);
    // Post-tank corrective notch (configurable per-preset). Defaults
    // match the original VVP calibration (320 Hz, Q=2.0, -3.5 dB) which
    // flattens the Dattorro tank's intrinsic 200-500 Hz hump.
    boxCut_.design     (boxCutFreqHz_, 2.0f, boxCutGainDb_, sampleRate_);
    lowMidTrim_.design (200.0f, 0.7f,  0.0f, sampleRate_);
    // Pre-tank low-shelf — re-injects bass energy that the post-tank
    // boxCut otherwise removes. Gain defaults to 0 dB (no-op); dark
    // plates with -100% bass deficits enable this via setBassShelfGainDb.
    bassLift_.designLowShelf (bassShelfFreqHz_, 0.7f, bassShelfGainDb_, sampleRate_);
    // HF shelf — PRE-tank pre-emphasis. Boosts HF energy entering the
    // tank so early reflections come out bright; the in-loop structural
    // HF damping attenuates the boost over each modal pass, so the late
    // tail darkens naturally. Shelf gain / freq + struct HF damp Hz are
    // per-preset (set via setHf* APIs). Defaults match the original
    // Vintage Vocal Plate calibration; dark plates configure their own.
    hfLift_.designHighShelf (hfShelfFreqHz_, 0.7f, hfShelfGainDb_, sampleRate_);
    boxCut_.reset();
    lowMidTrim_.reset();
    bassLift_.reset();
    hfLift_.reset();

    preTankL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    preTankR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    // Front-load early-reflection network buffers (see setFrontLoad).
    const int erMax   = std::max (16, static_cast<int> (0.22f * sampleRate_));
    const int tankMax = std::max (16, static_cast<int> (0.16f * sampleRate_));
    erDelayL_.prepare (erMax);   erDelayR_.prepare (erMax);
    tankPreL_.prepare (tankMax); tankPreR_.prepare (tankMax);
    earlyL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    earlyR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    frontPredelaySamp_ = std::max (1, static_cast<int> (frontPredelayMs_ * 0.001f * sampleRate_));
    // Mirror setFrontLoad()'s capacity clamp: if setFrontLoad() ran before prepare(),
    // frontPredelayMs_ may exceed the tank pre-delay ring — saturate to its longest
    // representable delay so readAgo() can't (w-d)&mask wrap-alias to a shorter delay.
    frontPredelaySamp_ = std::min (frontPredelaySamp_, tankPreL_.mask);
    erLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (frontLpHz_, 0.45f * sampleRate_) / sampleRate_);
    erLpZL_ = erLpZR_ = 0.0f;

    // Post-main second-reflection tap ring (~300 ms headroom for a ~143 ms tap).
    const int postMax = std::max (16, static_cast<int> (0.30f * sampleRate_));
    postMainL_.prepare (postMax); postMainR_.prepare (postMax);
    postMainDelayL_ = std::min (std::max (1, static_cast<int> (postMainMs_ * 0.001f * sampleRate_)), postMainL_.mask);
    postMainDelayR_ = std::min (std::max (1, static_cast<int> ((postMainMs_ + 9.0f) * 0.001f * sampleRate_)), postMainR_.mask);
    postMainLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (postMainLpHz_, 0.45f * sampleRate_) / sampleRate_);
    postMainLpZL_ = postMainLpZR_ = 0.0f;

    // Dense early-field combs/allpasses + predelay (generous headroom).
    const float rr = sampleRate_ / 44100.0f;
    for (int c = 0; c < 4; ++c)
    {
        dfComb_[c].prepare (static_cast<int> (kDfCombMs[c] * 0.001f * sampleRate_) + 8);
        dfCombLen_[c] = std::max (1, static_cast<int> (kDfCombMs[c] * 0.001f * sampleRate_));
    }
    dfAp1_.prepare (static_cast<int> (kDfAp1Ms * 0.001f * sampleRate_) + 8);
    dfAp2_.prepare (static_cast<int> (kDfAp2Ms * 0.001f * sampleRate_) + 8);
    dfAp1Len_ = std::max (1, static_cast<int> (kDfAp1Ms * 0.001f * sampleRate_));
    dfAp2Len_ = std::max (1, static_cast<int> (kDfAp2Ms * 0.001f * sampleRate_));
    dfPre_.prepare (static_cast<int> (0.30f * sampleRate_));   // up to 300 ms predelay
    dfApR_.prepare (static_cast<int> (kDfDecorrMs * 0.001f * sampleRate_) + 8);
    dfApRLen_ = std::max (1, static_cast<int> (kDfDecorrMs * 0.001f * sampleRate_));
    dfLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (kDfLpHz, 0.45f * sampleRate_) / sampleRate_);
    dfLpZ_ = 0.0f;
    (void) rr;

    tank_.setStructuralHFDamping (structHfDampHz_);

    prepared_ = true;

    // After prepared_ — setDenseField()'s recompute path is guarded on it, so
    // dfPreSamp_/dfCombG_ would stay uninitialised if called earlier.
    setDenseField (dfGain_, dfPredelayMs_, dfT60Ms_);   // (re)compute feedback + predelay
}

void DattorroPlateVintage::clearBuffers()
{
    tank_.clearBuffers();
    boxCut_.reset();
    lowMidTrim_.reset();
    bassLift_.reset();
    hfLift_.reset();
    erDelayL_.clear(); erDelayR_.clear();
    tankPreL_.clear(); tankPreR_.clear();
    erLpZL_ = erLpZR_ = 0.0f;
    postMainL_.clear(); postMainR_.clear();
    postMainLpZL_ = postMainLpZR_ = 0.0f;
    for (int c = 0; c < 4; ++c) dfComb_[c].clear();
    dfAp1_.clear(); dfAp2_.clear(); dfPre_.clear(); dfApR_.clear();
    dfLpZ_ = 0.0f;
}

void DattorroPlateVintage::process (const float* inputL, const float* inputR,
                                    float* outputL, float* outputR, int numSamples)
{
    if (! prepared_) return;
    if (static_cast<size_t> (numSamples) > preTankL_.size())
    {
        // Host violated prepare()'s maxBlockSize. Zero output rather than
        // leaving caller buffers untouched (stale tail leak otherwise).
        std::fill_n (outputL, numSamples, 0.0f);
        std::fill_n (outputR, numSamples, 0.0f);
        return;
    }

    // Pre-tank EQ chain: bassLift_ (low-shelf bass injection) → hfLift_
    // (HF pre-emphasis). bassLift_ runs first so the structural HF damper
    // inside the tank attenuates only the boosted top end, leaving the
    // bass injection untouched by the tank's HF damping path.
    for (int n = 0; n < numSamples; ++n)
    {
        const float bl = bassLift_.processL (inputL[n]);
        const float br = bassLift_.processR (inputR[n]);
        preTankL_[static_cast<size_t> (n)] = hfLift_.processL (bl);
        preTankR_[static_cast<size_t> (n)] = hfLift_.processR (br);
    }

    // Front-load network: build a sparse diffused early field from the pre-tank
    // signal and replace the tank's input with a pre-delayed copy so the dense
    // tank arrives AFTER the early build (its delayed onset = the prominent tap).
    // Bypassed (byte-identical) when frontErGain_ == 0.
    const bool front = frontErGain_ > 0.0f;
    if (front)
    {
        int tapL[kErTaps], tapR[kErTaps];
        for (int k = 0; k < kErTaps; ++k)
        {
            tapL[k] = std::max (1, static_cast<int> (kErTapFrac[k] * frontTapMs_ * 0.001f * sampleRate_));
            // R offset ~0.7 ms → L/R decorrelation (stereo_corr) without smearing.
            tapR[k] = std::max (1, tapL[k] + static_cast<int> (0.0007f * sampleRate_));
            // Upper-clamp to ring capacity (avoid (w-d)&mask wrap-aliasing on an
            // oversized tapMs). 2026-06-23 review fix; latent at current tunings.
            tapL[k] = std::min (tapL[k], erDelayL_.mask);
            tapR[k] = std::min (tapR[k], erDelayR_.mask);
        }
        for (int n = 0; n < numSamples; ++n)
        {
            const float il = preTankL_[static_cast<size_t> (n)];
            const float ir = preTankR_[static_cast<size_t> (n)];
            // Band-limited DISCRETE taps (no diffusion — keeps the envelope ripple).
            erDelayL_.write (il);
            erDelayR_.write (ir);
            float el = 0.0f, er = 0.0f;
            for (int k = 0; k < kErTaps; ++k)
            {
                el += kErTapGain[k] * erDelayL_.readAgo (tapL[k]);
                er += kErTapGain[k] * erDelayR_.readAgo (tapR[k]);
            }
            // 1-pole LP — plate early reflections are band-limited, not full-range.
            erLpZL_ += erLpCoeff_ * (el - erLpZL_);
            erLpZR_ += erLpCoeff_ * (er - erLpZR_);
            earlyL_[static_cast<size_t> (n)] = frontErGain_ * erLpZL_;
            earlyR_[static_cast<size_t> (n)] = frontErGain_ * erLpZR_;
            // Pre-delay the tank input so its dense field fills in after the build.
            // CAUTION: this OVERWRITES preTankL_/R_ with the PREDELAYED input. The post-main
            // tap + dense field (post-tank loop) also read preTankL_/R_, so front-load is
            // MUTUALLY EXCLUSIVE with them on a single preset — re-enabling front-load
            // alongside the dense field would feed the tap/field the predelayed copy (wrong
            // tap timing). Today no preset combines them (kDpvFrontLoadByName + kDpvPostMainTapByName
            // are both empty); if that changes, capture the original pre-tank into a separate buffer.
            tankPreL_.write (il); tankPreR_.write (ir);
            preTankL_[static_cast<size_t> (n)] = tankPreL_.readAgo (frontPredelaySamp_);
            preTankR_[static_cast<size_t> (n)] = tankPreR_.readAgo (frontPredelaySamp_);
        }
    }

    tank_.process (preTankL_.data(), preTankR_.data(), outputL, outputR, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        float l = outputL[n];
        float r = outputR[n];
        l = boxCut_.processL (l);
        r = boxCut_.processR (r);
        l = lowMidTrim_.processL (l);
        r = lowMidTrim_.processR (r);
        if (front)
        {
            l += earlyL_[static_cast<size_t> (n)];
            r += earlyR_[static_cast<size_t> (n)];
        }
        // Post-main second reflection tap: a darkened, delayed copy of the
        // pre-tank signal summed POST-tank so it arrives AFTER the main onset —
        // the anchor's ~143 ms second arrival, WITHOUT pre-echo. preTankL_ holds
        // the pre-tank filtered signal (front is off for VVP). gain 0 = bit-null.
        if (postMainActive_)
        {
            const float dl = postMainL_.readAgo (postMainDelayL_);
            const float dr = postMainR_.readAgo (postMainDelayR_);
            // Write silence while frozen so the tap admits no new input and its
            // held content drains out with the frozen tank (mirrors the dense
            // field's dfFrozen_ 0-feed above).
            postMainL_.write (dfFrozen_ ? 0.0f : preTankL_[static_cast<size_t> (n)]);
            postMainR_.write (dfFrozen_ ? 0.0f : preTankR_[static_cast<size_t> (n)]);
            postMainLpZL_ += postMainLpCoeff_ * (dl - postMainLpZL_);
            postMainLpZR_ += postMainLpCoeff_ * (dr - postMainLpZR_);
            l += postMainGain_ * postMainLpZL_;
            r += postMainGain_ * postMainLpZR_;
        }
        // Dense early-field: predelayed mono input → 4 parallel combs (dense
        // diffuse buildup, medium decay) → 2 series allpasses → summed to output.
        // Fills the post-onset 0.1-0.5 s shelf the sparse tank can't (no pre-echo:
        // the predelay starts it after the main onset).
        if (dfActive_)
        {
            // Fed 0 when frozen so the dense field decays out with the held tank
            // (mirrors the algo-0 path in DuskVerbEngine).
            const float xin = dfFrozen_ ? 0.0f
                : 0.5f * (preTankL_[static_cast<size_t> (n)] + preTankR_[static_cast<size_t> (n)]);
            // Read the predelayed tap BEFORE advancing the ring so dfPreSamp_ maps
            // to an exact dfPreSamp_-sample delay (write-then-read was one sample
            // early and aliased a stale slot at zero delay). Fast path for 0.
            const float xp = (dfPreSamp_ == 0) ? xin : dfPre_.readAgo (dfPreSamp_);
            dfPre_.write (xin);
            float s = 0.0f;
            for (int c = 0; c < 4; ++c)
            {
                const float d = dfComb_[c].readAgo (dfCombLen_[c]);
                dfComb_[c].write (xp + dfCombG_[c] * d);
                s += d;
            }
            s *= 0.25f;
            const float a1 = dfAp1_.readAgo (dfAp1Len_); const float y1 = -kDfApG * s  + a1; dfAp1_.write (s  + kDfApG * y1);
            const float a2 = dfAp2_.readAgo (dfAp2Len_); const float y2 = -kDfApG * y1 + a2; dfAp2_.write (y1 + kDfApG * y2);
            // Darken (early reflections are duller) then decorrelate R for stereo.
            dfLpZ_ += dfLpCoeff_ * (y2 - dfLpZ_);
            const float yL = dfLpZ_;
            const float ar = dfApR_.readAgo (dfApRLen_); const float yR = -kDfDecorrG * yL + ar; dfApR_.write (yL + kDfDecorrG * yR);
            l += dfGain_ * yL;
            r += dfGain_ * yR;
        }
        outputL[n] = l;
        outputR[n] = r;
    }
}

void DattorroPlateVintage::setDecayTime         (float v) { tank_.setDecayTime         (v); }
void DattorroPlateVintage::setSize              (float v) { tank_.setSize              (v); }
void DattorroPlateVintage::setBassMultiply      (float v) { tank_.setBassMultiply      (v); }
void DattorroPlateVintage::setMidMultiply       (float v) { tank_.setMidMultiply       (v); }
void DattorroPlateVintage::setTrebleMultiply    (float v) { tank_.setTrebleMultiply    (v); }
void DattorroPlateVintage::setCrossoverFreq     (float v) { tank_.setCrossoverFreq     (v); }
void DattorroPlateVintage::setHighCrossoverFreq (float v) { tank_.setHighCrossoverFreq (v); }
void DattorroPlateVintage::setSaturation        (float v) { tank_.setSaturation        (v); }
void DattorroPlateVintage::setModDepth          (float v) { tank_.setModDepth          (v); }
void DattorroPlateVintage::setModRate           (float v) { tank_.setModRate           (v); }
void DattorroPlateVintage::setTankDiffusion     (float v) { tank_.setTankDiffusion     (v); }
void DattorroPlateVintage::setDensityDepth      (float v) { tank_.setDensityDepth      (v); }
void DattorroPlateVintage::setModReduction      (float v) { tank_.setModReduction      (v); }
void DattorroPlateVintage::setInputDiffusionScale (float v) { tank_.setInputDiffusionScale (v); }
void DattorroPlateVintage::setSoftOnsetMs       (float v) { tank_.setSoftOnsetMs       (v); }
void DattorroPlateVintage::setOctaveT60      (int b, float v) { tank_.setOctaveT60 (b, v); }
void DattorroPlateVintage::setOctaveDecayRef    (float v) { tank_.setOctaveDecayRef    (v); }
void DattorroPlateVintage::setTonalCorrDb     (int b, float v) { tank_.setTonalCorrDb (b, v); }
void DattorroPlateVintage::setBloomAttackMs     (float v) { tank_.setBloomAttackMs     (v); }
void DattorroPlateVintage::setBloomExp          (float v) { tank_.setBloomExp          (v); }
void DattorroPlateVintage::setFreeze            (bool  v) { dfFrozen_ = v; tank_.setFreeze (v); }

void DattorroPlateVintage::setFrontLoad (float erGain, float predelayMs, float tapMs, float lpHz)
{
    frontErGain_     = std::max (0.0f, erGain);
    frontPredelayMs_ = std::max (0.0f, predelayMs);
    frontTapMs_      = std::max (1.0f, tapMs);
    frontLpHz_       = std::max (500.0f, lpHz);
    if (prepared_)
    {
        frontPredelaySamp_ = std::max (1, static_cast<int> (frontPredelayMs_ * 0.001f * sampleRate_));
        // Upper-clamp to the ring capacity: an oversized predelay would wrap via
        // (w-d)&mask and silently alias to a SHORTER delay. Saturate to the longest
        // representable instead. (2026-06-23 review fix; latent — current callers stay small.)
        frontPredelaySamp_ = std::min (frontPredelaySamp_, tankPreL_.mask);
        erLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (frontLpHz_, 0.45f * sampleRate_) / sampleRate_);
    }
}

void DattorroPlateVintage::setPostMainTap (float ms, float gain, float lpHz)
{
    postMainMs_     = std::max (1.0f, ms);
    postMainGain_   = std::clamp (gain, 0.0f, 4.0f);
    postMainLpHz_   = std::clamp (lpHz, 1000.0f, 20000.0f);
    postMainActive_ = postMainGain_ > 1.0e-6f;
    if (prepared_)
    {
        postMainDelayL_ = std::min (std::max (1, static_cast<int> (postMainMs_ * 0.001f * sampleRate_)), postMainL_.mask);
        postMainDelayR_ = std::min (std::max (1, static_cast<int> ((postMainMs_ + 9.0f) * 0.001f * sampleRate_)), postMainR_.mask);
        postMainLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (postMainLpHz_, 0.45f * sampleRate_) / sampleRate_);
    }
}

void DattorroPlateVintage::setDenseField (float gain, float predelayMs, float t60Ms)
{
    dfGain_       = std::clamp (gain, 0.0f, 4.0f);
    dfPredelayMs_ = std::max (0.0f, predelayMs);
    dfT60Ms_      = std::clamp (t60Ms, 50.0f, 2000.0f);
    dfActive_     = dfGain_ > 1.0e-6f;
    if (prepared_)
    {
        dfPreSamp_ = std::min (std::max (0, static_cast<int> (dfPredelayMs_ * 0.001f * sampleRate_)), dfPre_.mask);
        const float t60s = dfT60Ms_ * 0.001f;
        for (int c = 0; c < 4; ++c)
        {
            // Schroeder comb feedback for the target T60: g = 10^(-3·Lsec/T60).
            const float Lsec = dfCombLen_[c] / sampleRate_;
            dfCombG_[c] = std::clamp (std::pow (10.0f, -3.0f * Lsec / t60s), 0.0f, 0.97f);
        }
    }
}

void DattorroPlateVintage::setHfShelfGainDb (float gainDb)
{
    hfShelfGainDb_ = gainDb;
    if (prepared_)
        hfLift_.designHighShelf (hfShelfFreqHz_, 0.7f, hfShelfGainDb_, sampleRate_);
}

void DattorroPlateVintage::setHfShelfFreqHz (float fcHz)
{
    hfShelfFreqHz_ = fcHz;
    if (prepared_)
        hfLift_.designHighShelf (hfShelfFreqHz_, 0.7f, hfShelfGainDb_, sampleRate_);
}

void DattorroPlateVintage::setStructHfDampHz (float hz)
{
    structHfDampHz_ = hz;
    if (prepared_)
        tank_.setStructuralHFDamping (structHfDampHz_);
}

void DattorroPlateVintage::setBoxCutGainDb (float gainDb)
{
    boxCutGainDb_ = gainDb;
    if (prepared_)
        boxCut_.design (boxCutFreqHz_, 2.0f, boxCutGainDb_, sampleRate_);
}

void DattorroPlateVintage::setBoxCutFreqHz (float fcHz)
{
    boxCutFreqHz_ = fcHz;
    if (prepared_)
        boxCut_.design (boxCutFreqHz_, 2.0f, boxCutGainDb_, sampleRate_);
}

void DattorroPlateVintage::setBassShelfGainDb (float gainDb)
{
    bassShelfGainDb_ = gainDb;
    if (prepared_)
        bassLift_.designLowShelf (bassShelfFreqHz_, 0.7f, bassShelfGainDb_, sampleRate_);
}

void DattorroPlateVintage::setBassShelfFreqHz (float fcHz)
{
    bassShelfFreqHz_ = fcHz;
    if (prepared_)
        bassLift_.designLowShelf (bassShelfFreqHz_, 0.7f, bassShelfGainDb_, sampleRate_);
}
