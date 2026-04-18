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

class PowerSupply
{
public:
    enum class Type { Silicon, Tube5AR4, TubeGZ34 };

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        updateCoeffs();
        reset();
    }

    void reset()
    {
        vBplus_ = 1.0f;
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

        return vBplus_;
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

    void updateCoeffs()
    {
        float dischargeMs, rechargeMs, loadGain, maxDepth;
        switch (type_)
        {
            case Type::Tube5AR4:  // Fender Deluxe Reverb (5AR4 / GZ34-family, 32 μF filter)
                dischargeMs = 8.0f;
                rechargeMs  = 35.0f;
                loadGain    = 20.0f;
                maxDepth    = 0.35f;
                break;
            case Type::TubeGZ34:  // Vox AC30 (GZ34, 32 μF, higher idle current, Class A)
                dischargeMs = 5.0f;
                rechargeMs  = 50.0f;
                loadGain    = 30.0f;
                maxDepth    = 0.40f;
                break;
            case Type::Silicon:   // Marshall Plexi (silicon bridge, 100 μF)
            default:
                dischargeMs = 2.0f;
                rechargeMs  = 6.0f;
                loadGain    = 8.0f;
                maxDepth    = 0.10f;
                break;
        }

        loadGain_ = loadGain;
        maxSagDepth_ = maxDepth;

        if (sampleRate_ > 0.0)
        {
            const float sr = static_cast<float> (sampleRate_);
            dischargeCoeff_ = 1.0f - std::exp (-1000.0f / (dischargeMs * sr));
            rechargeCoeff_  = 1.0f - std::exp (-1000.0f / (rechargeMs  * sr));
        }
    }
};
