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
    erLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (frontLpHz_, 0.45f * sampleRate_) / sampleRate_);
    erLpZL_ = erLpZR_ = 0.0f;

    tank_.setStructuralHFDamping (structHfDampHz_);

    prepared_ = true;
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
void DattorroPlateVintage::setFreeze            (bool  v) { tank_.setFreeze            (v); }

void DattorroPlateVintage::setFrontLoad (float erGain, float predelayMs, float tapMs, float lpHz)
{
    frontErGain_     = std::max (0.0f, erGain);
    frontPredelayMs_ = std::max (0.0f, predelayMs);
    frontTapMs_      = std::max (1.0f, tapMs);
    frontLpHz_       = std::max (500.0f, lpHz);
    if (prepared_)
    {
        frontPredelaySamp_ = std::max (1, static_cast<int> (frontPredelayMs_ * 0.001f * sampleRate_));
        erLpCoeff_ = 1.0f - std::exp (-6.283185307f * std::min (frontLpHz_, 0.45f * sampleRate_) / sampleRate_);
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
