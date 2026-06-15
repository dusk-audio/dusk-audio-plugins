#include "DattorroPlateVintage.h"

#include <algorithm>

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

    tank_.process (preTankL_.data(), preTankR_.data(), outputL, outputR, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        float l = outputL[n];
        float r = outputR[n];
        l = boxCut_.processL (l);
        r = boxCut_.processR (r);
        l = lowMidTrim_.processL (l);
        r = lowMidTrim_.processR (r);
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
