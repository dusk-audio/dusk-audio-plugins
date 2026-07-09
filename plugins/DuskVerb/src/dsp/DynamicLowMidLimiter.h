#pragma once

#include <cmath>
#include <algorithm>

// Post-tank dynamic low-mid buildup limiter (2026-07-07, the piano-cluster fix).
//
// DV's recirculating tanks accumulate low-mid energy on a SUSTAINED input (piano stem /
// sustained tone) far above the commercial anchors (+3..+10 dB, measured fleet-wide),
// because the low-mid feedback sits near unity: a FINITE noiseburst decays normally (so the
// noiseburst-tuned per-band T60 gates pass) but a sustained tone charges without bound.
//
// This stage cuts ONLY the low band, ONLY when its envelope SUSTAINS above a threshold set
// BETWEEN the noiseburst low-mid level and the piano buildup — so transients (the slow
// attack ignores them) and the tuned noiseburst/impulse voicing (below threshold) are
// untouched. The gain reduction is slew-limited so it never AM-modulates the tail (the
// AttackRamp-distortion lesson). maxCut 0 → active_ false → the caller skips it → bit-null.
//
// Operates on the summed stereo wet (engine-agnostic), so ONE stage serves every tank
// engine instead of a per-line in-loop limiter in each. Mirrors the DenseHall in-loop
// low-accum limiter's drive-following math, widened (splitHz to 2 kHz, deeper cut) and
// moved post-tank. See [[duskverb_piano_lowmid_buildup_cluster]].
class DynamicLowMidLimiter
{
public:
    void prepare (double sampleRate)
    {
        sr_ = static_cast<float> (sampleRate);
        updateCoeffs();
        clear();
    }

    void clear() { lpL_ = lpR_ = env_ = red_ = 0.0f; }

    // threshDb: envelope threshold (dBFS-ish, on the low-band |amplitude|). maxCut: max
    // fraction of the low band removed (0..0.9 → 0..−20 dB). splitHz: low-band corner.
    // atkMs SLOW (ignore transients), relMs moderate.
    void setParams (float threshDb, float maxCut, float splitHz, float atkMs, float relMs)
    {
        threshLin_ = std::pow (10.0f, std::clamp (threshDb, -90.0f, 0.0f) / 20.0f);
        maxCut_    = std::clamp (maxCut, 0.0f, 0.9f);
        splitHz_   = std::clamp (splitHz, 100.0f, 2000.0f);
        atkMs_     = std::max (atkMs, 1.0f);
        relMs_     = std::max (relMs, 1.0f);
        active_    = maxCut_ > 1.0e-6f;
        if (sr_ > 0.0f) updateCoeffs();
    }

    bool active() const { return active_; }

    // Cut the sustained low-mid on the stereo wet in place. Caller guards on active().
    inline void processSample (float& L, float& R)
    {
        lpL_ += lpCoeff_ * (L - lpL_);
        lpR_ += lpCoeff_ * (R - lpR_);
        const float a = 0.5f * (std::abs (lpL_) + std::abs (lpR_));
        env_ += (a > env_ ? atkCoeff_ : relCoeff_) * (a - env_);
        const float over   = env_ / threshLin_;
        const float target = (over > 1.0f) ? std::min (over - 1.0f, 1.0f) * maxCut_ : 0.0f;
        red_ += gCoeff_ * (target - red_);   // 20 ms slew → no audio-rate AM sidebands
        L -= red_ * lpL_;
        R -= red_ * lpR_;
    }

private:
    void updateCoeffs()
    {
        lpCoeff_  = 1.0f - std::exp (-6.28318530718f * splitHz_ / sr_);
        atkCoeff_ = 1.0f - std::exp (-1.0f / (atkMs_ * 0.001f * sr_));
        relCoeff_ = 1.0f - std::exp (-1.0f / (relMs_ * 0.001f * sr_));
        gCoeff_   = 1.0f - std::exp (-1.0f / (0.020f * sr_));
    }

    float sr_ = 48000.0f, lpCoeff_ = 0.0f, atkCoeff_ = 0.0f, relCoeff_ = 0.0f, gCoeff_ = 0.0f;
    float threshLin_ = 0.01f, maxCut_ = 0.0f, splitHz_ = 800.0f, atkMs_ = 80.0f, relMs_ = 300.0f;
    bool  active_ = false;
    float lpL_ = 0.0f, lpR_ = 0.0f, env_ = 0.0f, red_ = 0.0f;
};
