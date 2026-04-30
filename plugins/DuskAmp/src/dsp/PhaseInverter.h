// PhaseInverter.h — explicit model of the driver stage between tonestack
// and power tubes. In a real amp this is a triode stage with its own
// substantial voltage gain (~+30 dB) and saturation that begins BEFORE the
// power tubes do, contributing the harmonics + dynamic feel of "PI clipping"
// that's distinct from output-tube clipping.
//
// Replaces the static `postMakeup_` linear scalar that was sitting after the
// power-tube waveshaper. Now the gain happens before the waveshaper (where
// it physically belongs) and saturates on its own at heavy drive — the
// missing ingredient for cranked THD reaching real-amp levels.
//
// Two topologies:
//   LongTailPair  — Fender / Marshall: two cross-coupled triodes, ~+30 dB,
//                   moderately symmetric saturation, clipping point sits just
//                   below where the output tubes saturate.
//   Cathodyne     — Vox AC30: single triode split-load, ~unity gain, very
//                   high headroom, mostly transparent.

#pragma once

#include <algorithm>
#include <cmath>

class PhaseInverter
{
public:
    enum class Topology { LongTailPair, Cathodyne };

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
    }

    void reset()
    {
        // Stateless waveshaper — nothing to reset.
    }

    void setTopology (Topology t) { topology_ = t; }

    // Linear-region gain. LTPs in real amps are ~30 dB (×30); we operate in
    // normalized signal space [-1,1] so gain values much smaller still give
    // perceptually-correct level matching.
    void setGain (float gainLinear) { gain_ = gainLinear; }

    // Saturation ceiling. Output is bounded to ±headroom asymptotically.
    // Larger headroom ⇒ more linear region before clipping.
    void setHeadroom (float headroom) { headroom_ = std::max (0.1f, headroom); }

    float processSample (float x) const
    {
        const float driven = x * gain_;
        if (topology_ == Topology::LongTailPair)
        {
            // Symmetric tanh — matches an LTP's near-symmetric clipping to
            // first order. The two anti-phase outputs the LTP feeds into the
            // power tubes are still produced by the PowerAmp's push-pull
            // waveshaper downstream; here we model only the LTP's own
            // saturation, which is symmetric in the differential view.
            return std::tanh (driven / headroom_) * headroom_;
        }
        else
        {
            // Cathodyne: single triode acting as a phase splitter. ~Unity
            // voltage gain, very high effective headroom — barely contributes
            // saturation under normal drive. The wider headroom keeps the
            // tanh in its linear region for typical signal levels, so we
            // don't add character that isn't there in a real cathodyne.
            return std::tanh (driven / (headroom_ * 1.5f)) * (headroom_ * 1.5f);
        }
    }

    void process (float* buf, int numSamples) const
    {
        for (int i = 0; i < numSamples; ++i)
            buf[i] = processSample (buf[i]);
    }

private:
    double sampleRate_ = 44100.0;
    Topology topology_ = Topology::LongTailPair;
    float gain_ = 1.0f;
    float headroom_ = 1.5f;
};
