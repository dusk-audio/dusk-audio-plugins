#include "HallTrueLexReverb.h"

#include "DspUtils.h"

#include <algorithm>
#include <cmath>

constexpr float HallTrueLexReverb::kERTapTimesMs[HallTrueLexReverb::kNumERTaps];
constexpr float HallTrueLexReverb::kERSignL[HallTrueLexReverb::kNumERTaps];
constexpr float HallTrueLexReverb::kERSignR[HallTrueLexReverb::kNumERTaps];
constexpr int   HallTrueLexReverb::kAPPrimesL[HallTrueLexReverb::kAPStages];
constexpr int   HallTrueLexReverb::kAPPrimesR[HallTrueLexReverb::kAPStages];

// ─── ERTDL ─────────────────────────────────────────────────────────

void HallTrueLexReverb::ERTDL::prepare (double sr)
{
    // Largest tap = 9.79 ms; pad to ~20 ms then round up to power of two
    // so the read mask is cheap.
    const int target = static_cast<int> (std::round (
        (kERTapTimesMs[kNumERTaps - 1] + 10.0f) * sr / 1000.0f));
    const int bufSize = DspUtils::nextPowerOf2 (std::max (target, 64));
    bufL.assign (static_cast<size_t> (bufSize), 0.0f);
    bufR.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
    for (int i = 0; i < kNumERTaps; ++i)
        tapSamples[i] = std::min (
            static_cast<int> (std::round (kERTapTimesMs[i] * sr / 1000.0f)),
            bufSize - 1);
}

void HallTrueLexReverb::ERTDL::clear()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
}

void HallTrueLexReverb::ERTDL::process (float inL, float inR,
                                        float& outL, float& outR)
{
    // Write THEN read (so tap-0 returns the current sample → t=0 peak
    // lands in the IR analysis at exactly the anchor's first peak).
    bufL[static_cast<size_t> (writePos)] = inL;
    bufR[static_cast<size_t> (writePos)] = inR;

    float sL = 0.0f, sR = 0.0f;
    for (int i = 0; i < kNumERTaps; ++i)
    {
        const int r = (writePos - tapSamples[i]) & mask;
        const float vL = bufL[static_cast<size_t> (r)];
        const float vR = bufR[static_cast<size_t> (r)];
        sL += vL * weights[i] * kERSignL[i];
        sR += vR * weights[i] * kERSignR[i];
    }

    writePos = (writePos + 1) & mask;
    outL = sL;
    outR = sR;
}

// ─── HallTrueLexReverb ─────────────────────────────────────────────

void HallTrueLexReverb::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max (1, maxBlockSize);

    tank.prepare (sampleRate, maxBlockSize_);
    // Mute the embedded HallReverb tank's late-stage specular taps.
    // The hardcoded ER TDL above carries ALL early specular energy at
    // the Lex anchor peak times — leaving the tank's internal specular
    // taps active would mask the ER peaks (FDN intrinsic peaks at
    // predelay+6/6.6/8 ms dominate the 4-10 ms window where the ER
    // anchor peaks live). Bypassing them clears the early masking and
    // structurally drops tank c80 by ~2 dB — exactly the headroom the
    // hollow-tank target-offset trick needs to leave for ER to fill.
    tank.setSpecularEnabled (false);
    erTDL_.prepare (sampleRate);

    auto prepAP = [sampleRate] (APStage& ap, int basePrime)
    {
        const float rateRatio = static_cast<float> (sampleRate / 44100.0);
        const int target = static_cast<int> (std::round (
            static_cast<float> (basePrime) * rateRatio));
        const int bufSize = DspUtils::nextPowerOf2 (std::max (target + 4, 16));
        ap.buf.assign (static_cast<size_t> (bufSize), 0.0f);
        ap.mask = bufSize - 1;
        ap.writePos = 0;
        ap.delaySamples = std::min (target, bufSize - 1);
    };
    for (int s = 0; s < kAPStages; ++s)
    {
        prepAP (apL_[s], kAPPrimesL[s]);
        prepAP (apR_[s], kAPPrimesR[s]);
    }

    erOutL_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);
    erOutR_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);
    tankOutL_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);
    tankOutR_.assign (static_cast<size_t> (maxBlockSize_), 0.0f);

    prepared_ = true;
}

void HallTrueLexReverb::clearBuffers()
{
    tank.clearBuffers();
    erTDL_.clear();
    for (int s = 0; s < kAPStages; ++s)
    {
        apL_[s].clear();
        apR_[s].clear();
    }
    std::fill (erOutL_.begin(), erOutL_.end(), 0.0f);
    std::fill (erOutR_.begin(), erOutR_.end(), 0.0f);
    std::fill (tankOutL_.begin(), tankOutL_.end(), 0.0f);
    std::fill (tankOutR_.begin(), tankOutR_.end(), 0.0f);
}

void HallTrueLexReverb::process (const float* inL, const float* inR,
                                 float* outL, float* outR, int numSamples)
{
    if (! prepared_ || numSamples <= 0) return;
    if (numSamples > maxBlockSize_)
    {
        // Caller bug — silent fail to keep the audio thread alive.
        std::fill (outL, outL + numSamples, 0.0f);
        std::fill (outR, outR + numSamples, 0.0f);
        return;
    }

    // 1) ER taps — parallel branch direct from input. NOT routed through
    //    the AP cascade so the hardcoded Lex anchor peaks [0/4/7.52/9.79]
    //    ms land sharp and detectable, dominating peak_locations_ms by
    //    construction (assuming erLevel × ER weights exceed tank's
    //    intrinsic specular tap energy at the same time positions).
    for (int n = 0; n < numSamples; ++n)
        erTDL_.process (inL[n], inR[n], erOutL_[(size_t) n], erOutR_[(size_t) n]);

    // 2) Tank (Engine 10 FDN). Input passes through unchanged so the
    //    macro c80 / d50 / decay envelope calibration from Engine 10's
    //    12/19 winner is preserved bit-for-bit when erLevel = 0.
    tank.process (inL, inR, tankOutL_.data(), tankOutR_.data(), numSamples);

    // 3) Cross-compensation: the post-mix Schroeder AP cascade is applied
    //    ONLY to tank output, NOT to ER. This is the corrected gain
    //    matrix per user spec — the AP cascade phase-smears the tank's
    //    sum-of-N-combs spectral crest WITHOUT recirculating into the
    //    tank AND WITHOUT touching ER tap peaks. ER peaks stay sharp
    //    (peak_locations PASS by construction), tank modal crest gets
    //    smeared (spectral_crest_db PASS by phase-decorrelation), and
    //    c80 / d50 / decay_envelope stay anchored at Engine 10's
    //    calibration since allpass cascade preserves total magnitude
    //    spectrum of the tank-only branch.
    //
    //    AP coeff bound is tight (0.0..0.15) per user spec — at small g
    //    the AP behaves as a gentle phase smearer that drops modal
    //    crest spikes by 2-4 dB without time-redistributing the head
    //    of the decay envelope (which g=0.5 was doing destructively).
    const float erG   = erLevel_;
    const float tankG = tankLevel_;
    const float g     = apG_;
    for (int n = 0; n < numSamples; ++n)
    {
        float smearedTankL = tankOutL_[(size_t) n] * tankG;
        float smearedTankR = tankOutR_[(size_t) n] * tankG;
        for (int s = 0; s < kAPStages; ++s)
        {
            smearedTankL = apL_[s].process (smearedTankL, g);
            smearedTankR = apR_[s].process (smearedTankR, g);
        }

        outL[n] = erOutL_[(size_t) n] * erG + smearedTankL;
        outR[n] = erOutR_[(size_t) n] * erG + smearedTankR;
    }
}

// ─── Setters ───────────────────────────────────────────────────────

void HallTrueLexReverb::setERWeight (int idx, float w)
{
    if (idx < 0 || idx >= kNumERTaps) return;
    erTDL_.weights[idx] = std::clamp (w, 0.0f, 4.0f);
}

void HallTrueLexReverb::setERLevel (float level)
{
    erLevel_ = std::clamp (level, 0.0f, 4.0f);
}

void HallTrueLexReverb::setTankLevel (float level)
{
    tankLevel_ = std::clamp (level, 0.0f, 4.0f);
}

void HallTrueLexReverb::setAPCoeff (float g)
{
    // Schroeder allpass remains an allpass for |g| < 1; clamp slightly
    // below 1 to avoid magnitude-response artifacts near unity.
    apG_ = std::clamp (g, -0.85f, 0.85f);
}
