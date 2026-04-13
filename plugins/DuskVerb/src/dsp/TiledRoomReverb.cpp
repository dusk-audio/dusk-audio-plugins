#include "TiledRoomReverb.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void TiledRoomReverb::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    int maxMod = static_cast<int> (std::ceil (32.0 * sampleRate / 44100.0));
    float maxScale = sizeRangeMax_;

    // Allocate Stage A (L group: short delays, fast onset)
    for (int ch = 0; ch < kStageSize; ++ch)
    {
        int maxSamples = static_cast<int> (std::ceil (static_cast<float> (kStageADelays[ch])
                       * rateRatio * maxScale)) + maxMod + 4;
        int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
        delayA_[ch].buffer.assign (static_cast<size_t> (bufSize), 0.0f);
        delayA_[ch].mask = bufSize - 1;
        delayA_[ch].writePos = 0;
        dampA_[ch].reset();
    }

    // Allocate Stage B (R group: long delays, sustained tail)
    for (int ch = 0; ch < kStageSize; ++ch)
    {
        int maxSamples = static_cast<int> (std::ceil (static_cast<float> (kStageBDelays[ch])
                       * rateRatio * maxScale)) + maxMod + 4;
        int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
        delayB_[ch].buffer.assign (static_cast<size_t> (bufSize), 0.0f);
        delayB_[ch].mask = bufSize - 1;
        delayB_[ch].writePos = 0;
        dampB_[ch].reset();
    }

    for (int ch = 0; ch < N; ++ch)
    {
        lfoPhase_[ch] = kTwoPi * static_cast<float> (ch) / static_cast<float> (N);
        lfoPRNG_[ch] = 0x12345678u + static_cast<uint32_t> (ch * 7919);
        noiseState_[ch] = 0xDEADBEEFu + static_cast<uint32_t> (ch * 6271);
        dcX1_[ch] = 0.0f;
        dcY1_[ch] = 0.0f;
    }
    currentRMS_ = 0.0f;
    peakRMS_ = 0.0f;

    prepared_ = true;
    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();

    // Recompute modDepthSamples_ from cached raw value for new sample rate
    setModDepth (lastModDepthRaw_);
}

void TiledRoomReverb::process (const float* inputL, const float* inputR,
                                float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float noiseJitter = noiseModDepth_ * rateRatio;

    for (int i = 0; i < numSamples; ++i)
    {
        float inL = frozen_ ? 0.0f : inputL[i];
        float inR = frozen_ ? 0.0f : inputR[i];

        // ============================================================
        // STAGE A: 8 short delays processing L input
        // ============================================================
        float outA[kStageSize];
        for (int ch = 0; ch < kStageSize; ++ch)
        {
            auto& dl = delayA_[ch];
            float lfo = std::sin (lfoPhase_[ch]) * modDepthSamples_;
            float jitter = nextDrift (noiseState_[ch]) * noiseJitter;
            float readDelay = delayLenA_[ch] + lfo + jitter;
            readDelay = std::max (readDelay, 1.0f);

            float readPos = static_cast<float> (dl.writePos) - readDelay;
            int intIdx = static_cast<int> (std::floor (readPos));
            float frac = readPos - static_cast<float> (intIdx);
            outA[ch] = DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac);

            float drift = nextDrift (lfoPRNG_[ch]) * lfoPhaseInc_[ch] * 0.08f;
            lfoPhase_[ch] += lfoPhaseInc_[ch] + drift;
            if (lfoPhase_[ch] >= kTwoPi) lfoPhase_[ch] -= kTwoPi;
            else if (lfoPhase_[ch] < 0.0f) lfoPhase_[ch] += kTwoPi;
        }

        // Stage A: neighbour-pair feedback via orthogonal rotation
        // (energy-preserving: cos²θ + sin²θ = 1 regardless of coupling)
        float fbA[kStageSize];
        const float theta = stereoCoupling_ * 1.5707963f;  // 0→π/2
        const float cosT = std::cos (theta);
        const float sinT = std::sin (theta);
        for (int ch = 0; ch < kStageSize; ch += 2)
        {
            float a = outA[ch], b = outA[ch + 1];
            fbA[ch]     = cosT * a - sinT * b;
            fbA[ch + 1] = sinT * a + cosT * b;
        }

        // Stage A: damp + DC block + L input → write
        for (int ch = 0; ch < kStageSize; ++ch)
        {
            float filtered = frozen_ ? fbA[ch] : dampA_[ch].process (fbA[ch]);
            float dcOut = filtered - dcX1_[ch] + kDCCoeff * dcY1_[ch];
            dcX1_[ch] = filtered; dcY1_[ch] = dcOut; filtered = dcOut;

            float inputGain = frozen_ ? 0.0f : 0.25f;
            float polarity = (ch & 1) ? -1.0f : 1.0f;
            float bias = ((delayA_[ch].writePos ^ ch) & 1)
                        ? DspUtils::kDenormalPrevention : -DspUtils::kDenormalPrevention;
            delayA_[ch].buffer[static_cast<size_t> (delayA_[ch].writePos)] =
                filtered + inL * polarity * inputGain + bias;
            delayA_[ch].writePos = (delayA_[ch].writePos + 1) & delayA_[ch].mask;
        }

        // ============================================================
        // STAGE B: 8 long delays processing R input
        // ============================================================
        float outB[kStageSize];
        for (int ch = 0; ch < kStageSize; ++ch)
        {
            int lfoIdx = ch + kStageSize;
            auto& dl = delayB_[ch];
            float lfo = std::sin (lfoPhase_[lfoIdx]) * modDepthSamples_;
            float jitter = nextDrift (noiseState_[lfoIdx]) * noiseJitter;
            float readDelay = delayLenB_[ch] + lfo + jitter;
            readDelay = std::max (readDelay, 1.0f);

            float readPos = static_cast<float> (dl.writePos) - readDelay;
            int intIdx = static_cast<int> (std::floor (readPos));
            float frac = readPos - static_cast<float> (intIdx);
            outB[ch] = DspUtils::cubicHermite (dl.buffer.data(), dl.mask, intIdx, frac);

            float drift = nextDrift (lfoPRNG_[lfoIdx]) * lfoPhaseInc_[lfoIdx] * 0.08f;
            lfoPhase_[lfoIdx] += lfoPhaseInc_[lfoIdx] + drift;
            if (lfoPhase_[lfoIdx] >= kTwoPi) lfoPhase_[lfoIdx] -= kTwoPi;
            else if (lfoPhase_[lfoIdx] < 0.0f) lfoPhase_[lfoIdx] += kTwoPi;
        }

        // Terminal decay on Stage B — skipped when frozen
        if (terminalDecayFactor_ < 1.0f && ! frozen_)
        {
            float energy = 0.0f;
            for (int ch = 0; ch < kStageSize; ++ch)
                energy += outB[ch] * outB[ch];
            energy *= (1.0f / static_cast<float> (kStageSize));
            currentRMS_ = currentRMS_ * 0.9995f + energy * 0.0005f;
            if (currentRMS_ > peakRMS_) peakRMS_ = currentRMS_;
            else peakRMS_ *= 0.99999f;
            float rmsDB = 10.0f * std::log10 (std::max (currentRMS_, 1e-20f));
            float peakDB = 10.0f * std::log10 (std::max (peakRMS_, 1e-20f));
            if ((peakDB - rmsDB > -terminalDecayThresholdDB_) && peakRMS_ > 1e-12f)
                for (int ch = 0; ch < kStageSize; ++ch)
                    outB[ch] *= terminalDecayFactor_;
        }

        // Stage B: neighbour-pair feedback via orthogonal rotation
        float fbB[kStageSize];
        for (int ch = 0; ch < kStageSize; ch += 2)
        {
            float a = outB[ch], b = outB[ch + 1];
            fbB[ch]     = cosT * a - sinT * b;
            fbB[ch + 1] = sinT * a + cosT * b;
        }

        // Stage B: damp + DC block + R input + Stage A serial feed → write
        for (int ch = 0; ch < kStageSize; ++ch)
        {
            int dcIdx = ch + kStageSize;
            float filtered = frozen_ ? fbB[ch] : dampB_[ch].process (fbB[ch]);
            float dcOut = filtered - dcX1_[dcIdx] + kDCCoeff * dcY1_[dcIdx];
            dcX1_[dcIdx] = filtered; dcY1_[dcIdx] = dcOut; filtered = dcOut;

            float serialIn = outA[ch] * kSerialFeedLevel;
            float inputGain = frozen_ ? 0.0f : 0.25f;
            float polarity = (ch & 1) ? -1.0f : 1.0f;
            float bias = ((delayB_[ch].writePos ^ ch) & 1)
                        ? DspUtils::kDenormalPrevention : -DspUtils::kDenormalPrevention;
            delayB_[ch].buffer[static_cast<size_t> (delayB_[ch].writePos)] =
                filtered + serialIn + inR * polarity * inputGain + bias;
            delayB_[ch].writePos = (delayB_[ch].writePos + 1) & delayB_[ch].mask;
        }

        // ============================================================
        // OUTPUT: L sums Stage A, R sums Stage B (independent groups)
        // Each stage processes a different input AND has independent feedback,
        // giving natural L/R independence without needing cross-mixing.
        // ============================================================
        float outL = 0.0f, outR = 0.0f;
        for (int t = 0; t < kStageSize; ++t)
        {
            outL += outA[t] * kLeftSigns[t];
            outR += outB[t] * kRightSigns[t];
        }

        constexpr float kOutputScale = 1.0f / static_cast<float> (kStageSize);
        outputL[i] = std::clamp (outL * kOutputScale, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (outR * kOutputScale, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
void TiledRoomReverb::setDecayTime (float seconds) { decayTime_ = std::clamp (seconds, 0.2f, 600.0f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setBassMultiply (float mult) { bassMultiply_ = std::clamp (mult, 0.5f, 2.5f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setTrebleMultiply (float mult) { trebleMultiply_ = std::clamp (mult, 0.1f, 1.5f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setCrossoverFreq (float hz) { crossoverFreq_ = std::clamp (hz, 200.0f, 4000.0f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setHighCrossoverFreq (float hz) { highCrossoverFreq_ = std::clamp (hz, 1000.0f, 20000.0f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setAirDampingScale (float scale) { airDampingScale_ = std::max (scale, 0.01f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setModDepth (float depth) { lastModDepthRaw_ = depth; float clamped = std::min (depth, 2.0f); modDepthSamples_ = clamped * 16.0f * static_cast<float> (sampleRate_ / kBaseSampleRate); }
void TiledRoomReverb::setModRate (float hz) { modRateHz_ = hz; if (prepared_) updateLFORates(); }
void TiledRoomReverb::setSize (float size) { sizeParam_ = std::clamp (size, 0.0f, 1.0f); if (prepared_) { updateDelayLengths(); updateDecayCoefficients(); } }
void TiledRoomReverb::setFreeze (bool frozen) { frozen_ = frozen; }
void TiledRoomReverb::setDecayBoost (float boost) { decayBoost_ = std::clamp (boost, 0.3f, 2.0f); if (prepared_) updateDecayCoefficients(); }
void TiledRoomReverb::setTerminalDecay (float thresholdDB, float factor) { terminalDecayThresholdDB_ = thresholdDB; terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f); }
void TiledRoomReverb::setStereoCoupling (float amount) { stereoCoupling_ = std::clamp (amount, 0.0f, 0.75f); }

void TiledRoomReverb::clearBuffers()
{
    for (int ch = 0; ch < kStageSize; ++ch)
    {
        std::fill (delayA_[ch].buffer.begin(), delayA_[ch].buffer.end(), 0.0f);
        delayA_[ch].writePos = 0; dampA_[ch].reset();
        std::fill (delayB_[ch].buffer.begin(), delayB_[ch].buffer.end(), 0.0f);
        delayB_[ch].writePos = 0; dampB_[ch].reset();
    }
    for (int ch = 0; ch < N; ++ch) { dcX1_[ch] = dcY1_[ch] = 0.0f; }
    peakRMS_ = currentRMS_ = 0.0f;

    // Reset modulation and noise state for deterministic restart
    for (int ch = 0; ch < N; ++ch)
    {
        lfoPhase_[ch] = kTwoPi * static_cast<float> (ch) / static_cast<float> (N);
        lfoPRNG_[ch] = 0x12345678u + static_cast<uint32_t> (ch * 7919);
        noiseState_[ch] = 0xDEADBEEFu + static_cast<uint32_t> (ch * 6271);
    }
}

// -----------------------------------------------------------------------
void TiledRoomReverb::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    for (int ch = 0; ch < kStageSize; ++ch)
    {
        delayLenA_[ch] = static_cast<float> (kStageADelays[ch]) * rateRatio * sizeScale;
        delayLenB_[ch] = static_cast<float> (kStageBDelays[ch]) * rateRatio * sizeScale;
    }
}

void TiledRoomReverb::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowXoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highXoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    for (int ch = 0; ch < kStageSize; ++ch)
    {
        float gBase = std::pow (10.0f, -3.0f * delayLenA_[ch] / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);
        float gLow = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        float gMid = std::clamp (std::pow (gBase, 1.0f / trebleMultiply_), 0.001f, 0.9999f);
        float gAir = std::clamp (std::pow (gBase, 1.0f / (trebleMultiply_ * airDampingScale_)), 0.001f, 0.9999f);
        dampA_[ch].setCoefficients (gLow, gMid, gAir, lowXoverCoeff, highXoverCoeff);
    }

    for (int ch = 0; ch < kStageSize; ++ch)
    {
        float gBase = std::pow (10.0f, -3.0f * delayLenB_[ch] / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);
        float gLow = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        float gMid = std::clamp (std::pow (gBase, 1.0f / trebleMultiply_), 0.001f, 0.9999f);
        float gAir = std::clamp (std::pow (gBase, 1.0f / (trebleMultiply_ * airDampingScale_)), 0.001f, 0.9999f);
        dampB_[ch].setCoefficients (gLow, gMid, gAir, lowXoverCoeff, highXoverCoeff);
    }
}

void TiledRoomReverb::updateLFORates()
{
    float sr = static_cast<float> (sampleRate_);
    static constexpr float kRateMult[N] = {
        0.83f, 1.00f, 1.17f, 0.91f, 1.07f, 0.87f, 1.13f, 0.95f,
        1.21f, 0.89f, 1.09f, 0.97f, 1.15f, 0.85f, 1.03f, 1.31f
    };
    for (int ch = 0; ch < N; ++ch)
        lfoPhaseInc_[ch] = kTwoPi * modRateHz_ * kRateMult[ch] / sr;
}
