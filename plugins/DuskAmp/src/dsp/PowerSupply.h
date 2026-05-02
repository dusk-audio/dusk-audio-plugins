// PowerSupply.h — RC-model power supply simulating B+ rail sag
//
// Models the voltage drop on the B+ (plate supply) rail when power tubes
// draw current, and its RC-filtered recovery when load decreases.
//
// Physics: filter capacitor C at B+ nominal. Power tubes draw current
// proportional to signal power, discharging C. Rectifier (tube or silicon)
// refills C through its own dynamic resistance. A tube rectifier's larger
// forward resistance gives a longer recharge time constant — the "soft"
// feel of a Fender Deluxe or Vox AC30 under load. Silicon rectifiers
// recover near-instantly, giving the "tight" feel of a Marshall.
//
// Normalized state: V_B+ ∈ [V_floor, 1.0], where 1.0 is nominal plate
// voltage. The value returned by processSample() multiplies power-amp
// drive so that sag compresses signal headroom the way a real supply does.

#pragma once

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class PowerSupply
{
public:
    enum class Type { Silicon, Tube5AR4, TubeGZ34 };

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        ripplePhaseInc_ = static_cast<float> (2.0 * M_PI * 120.0 / sampleRate);
        updateCoeffs();
        reset();
    }

    void reset()
    {
        vBplus_ = 1.0f;
        ripplePhase_ = 0.0f;
    }

    void setType (Type type)
    {
        type_ = type;
        updateCoeffs();
    }

    // User sag knob 0..1. 0 = stiff rail, 1 = full per-type sag depth.
    void setDepth (float depth01)
    {
        depth_ = std::clamp (depth01, 0.0f, 1.0f);
    }

    // Drive one sample, return current normalized B+ ∈ [vFloor, 1.0].
    // audioSample is a proxy for power-tube grid drive — any post-stageGain
    // signal works. |x|² approximates instantaneous power (~current²·R).
    float processSample (float audioSample)
    {
        const float load = std::min (audioSample * audioSample * loadGain_, 1.0f);
        const float vTarget = 1.0f - load * maxSagDepth_ * depth_;

        const float alpha = (vTarget < vBplus_) ? dischargeCoeff_ : rechargeCoeff_;
        vBplus_ += (vTarget - vBplus_) * alpha;

        const float vFloor = 1.0f - maxSagDepth_ * depth_;
        vBplus_ = std::clamp (vBplus_, vFloor, 1.0f);
        if (std::abs (vBplus_) < 1.0e-15f) vBplus_ = 0.0f;

        // 120 Hz ripple — full-wave-rectified mains modulation that real
        // tube amps have on the B+ rail. Magnitude scales with sag depth
        // so that ripple is louder when the supply is loaded (filter caps
        // are draining faster). Per-amp depth: silicon rectifier (Marshall)
        // has the stiffest supply with smallest ripple; tube rectifiers
        // (Fender, Vox) ripple more, especially Class-A Vox which draws
        // continuous high current. Tiny absolute values (0.1-0.5%) keep
        // it subliminal but contributes the "alive" feel of real iron.
        ripplePhase_ += ripplePhaseInc_;
        if (ripplePhase_ > 2.0f * static_cast<float> (M_PI))
            ripplePhase_ -= 2.0f * static_cast<float> (M_PI);
        const float sagFraction = (1.0f - vBplus_) / std::max (maxSagDepth_ * depth_, 1.0e-6f);
        const float rippleAmp   = rippleDepth_ * (0.5f + 0.5f * sagFraction);
        const float ripple      = rippleAmp * std::sin (ripplePhase_);

        return vBplus_ + ripple;
    }

    float getBplus() const { return vBplus_; }

private:
    double sampleRate_ = 44100.0;
    Type type_ = Type::Silicon;
    float depth_ = 1.0f;

    float vBplus_ = 1.0f;
    float dischargeCoeff_ = 0.0f;
    float rechargeCoeff_ = 0.0f;
    float loadGain_ = 1.0f;
    float maxSagDepth_ = 0.1f;

    // 120 Hz mains-ripple state. ripplePhase_ tracks position in the
    // full-wave-rectified sine; rippleDepth_ is per-amp peak amplitude.
    float ripplePhase_     = 0.0f;
    float ripplePhaseInc_  = 0.0f;
    float rippleDepth_     = 0.0f; // 0 = no ripple; 0.001-0.005 = subtle

    void updateCoeffs()
    {
        // Real power-supply physics: when a loud chord is played, the filter
        // cap bleeds down SLOWLY through the power-tube load (discharge τ ≈
        // tens of ms → "bloom" as compression builds over the note). When the
        // note releases, the rectifier QUICKLY refills the cap (recharge τ =
        // R_rect × C, a few ms for tube rectifiers, <1 ms for silicon). The
        // previous values had these inverted — voltage ducked fast and
        // recovered slowly, which felt like a compressor pumping on every
        // transient instead of musical sag.
        //
        // loadGain scales |x|² to load fraction — with the old value of 20+,
        // even a −12 dBFS signal saturated the load calc and triggered full
        // sag on every note. Reduced so only loud, sustained signals pull the
        // supply down meaningfully.
        float dischargeMs, rechargeMs, loadGain, maxDepth, rippleDepth;
        switch (type_)
        {
            case Type::Tube5AR4:  // Fender Deluxe Reverb (5AR4, 32 μF filter)
                dischargeMs = 25.0f;
                rechargeMs  = 5.0f;
                loadGain    = 4.0f;
                maxDepth    = 0.20f;
                rippleDepth = 0.0030f; // 0.3% — moderate tube-rectifier ripple
                break;
            case Type::TubeGZ34:  // Vox AC30 (GZ34, 32 μF, Class A — slower bloom, more depth)
                dischargeMs = 35.0f;
                rechargeMs  = 8.0f;
                loadGain    = 5.0f;
                maxDepth    = 0.25f;
                rippleDepth = 0.0050f; // 0.5% — Class A draws continuous current → biggest ripple
                break;
            case Type::Silicon:   // Marshall Plexi (silicon bridge, 100 μF — stiff supply)
            default:
                dischargeMs = 15.0f;
                rechargeMs  = 1.0f;
                loadGain    = 3.0f;
                maxDepth    = 0.06f;
                rippleDepth = 0.0010f; // 0.1% — silicon bridge + big cap = stiffest, smallest ripple
                break;
        }

        loadGain_    = loadGain;
        maxSagDepth_ = maxDepth;
        rippleDepth_ = rippleDepth;

        if (sampleRate_ > 0.0)
        {
            const float sr = static_cast<float> (sampleRate_);
            dischargeCoeff_ = 1.0f - std::exp (-1000.0f / (dischargeMs * sr));
            rechargeCoeff_  = 1.0f - std::exp (-1000.0f / (rechargeMs  * sr));
        }
    }
};
