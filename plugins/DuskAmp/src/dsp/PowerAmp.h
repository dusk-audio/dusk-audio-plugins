// PowerAmp.h — Power amplifier with per-amp-type waveshaper selection
//
// Three key differences per amp type:
//   Fender:  Triode curve (6V6 beam tetrode ≈ triode behavior), heavy NFB
//   Marshall: Pentode curve (EL34), moderate NFB
//   Vox:     EL84 curve, no NFB, Class A bias asymmetry

#pragma once

#include "AnalogEmulation/WaveshaperCurves.h"
#include "AnalogEmulation/TransformerEmulation.h"
#include "AnalogEmulation/DCBlocker.h"
#include "PowerSupply.h"

class PowerAmp
{
public:
    // Amp type affects waveshaper curve and character
    enum class AmpType { Fender = 0, Marshall = 1, Vox = 2 };

    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setSag (float sag01);
    void setAmpType (AmpType type);
    void process (float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    AmpType ampType_ = AmpType::Marshall;

    float drive_ = 0.3f;
    float driveGain_ = 1.0f;
    float sagAmount_ = 0.3f;
    PowerSupply powerSupply_;

    // Per-amp-type settings. stageGain/outputGain used to vary 80/35/8 and
    // 0.5/0.8/4 per amp to patch the preamp's net attenuation — that's now the
    // PreampDSP::outputMakeup_'s job. Here we use a single constant that
    // compensates the ~-30 dB tone-stack loss and represents the phase
    // inverter + power-tube voltage gain, same across all amps.
    AnalogEmulation::WaveshaperCurves::CurveType curveType_
        = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
    float biasAsymmetry_ = 0.0f;  // Class A push-pull bias offset (Vox only)

    // Push-pull bias mismatch: real-world matched pairs sit at 2-3% Ip
    // imbalance even after biasing. Models that as a small even-harmonic
    // injection on top of the otherwise-perfect sign(x)·(f(|x|)−f(0))
    // cancellation. Zero gives mathematically-perfect cancellation; 0.025
    // gives audible warmth without breaking the push-pull character.
    float pushPullAsymmetry_ = 0.0f;

    // Phase-inverter output asymmetry. The cathodyne PI on Vox AC30 sends
    // a slightly smaller signal to one power tube than the other (the
    // anode-output and cathode-output of a cathode-coupled splitter aren't
    // perfectly balanced — typical 3-5% offset). LTP PIs (Fender, Marshall)
    // are nearly perfectly balanced, so this is 0 there.
    float phaseInvAsymmetry_ = 0.0f;

    // Cathode-bias bloom (Vox AC30): the cathode resistor on cathode-biased
    // power amps heats up under sustained heavy playing and shifts the bias
    // more negative, slowly reducing tube gain. Models the famous "AC30
    // bloom" / dynamic compression. 0 disables; ~0.4 gives audible bloom.
    float cathodeBloomAmount_ = 0.0f;
    float cathodeEnv_         = 0.0f;
    float cathodeAttackCoeff_ = 0.0f; // ~500 ms attack
    float cathodeReleaseCoeff_= 0.0f; // ~2 s release

    // Grid current / blocking distortion. Real power-tube grids draw current
    // when the grid voltage swings positive of the cathode (typically when
    // |driven| exceeds gridChargeThreshold_). That current flows back through
    // the coupling-cap RC network and depresses the previous stage's DC
    // operating point — a transient "ducking" of the next several ms of
    // input that recovers slowly. This is the ghost-note / sustainer "tube
    // feel" that pure waveshapers can't replicate. Per-amp threshold and
    // amount: Marshall most aggressive (EL34 grids hard-block on hot input),
    // Vox lowest threshold (EL84 less headroom), Fender mildest.
    float gridChargeThreshold_ = 1.5f; // |driven| above which grid draws current
    float gridChargeAmount_    = 0.0f; // 0 = disabled; 0.2-0.3 audible
    float gridCharge_          = 0.0f; // envelope state
    float gridAttackCoeff_     = 0.0f; // ~1 ms attack — fast, transient detection
    float gridReleaseCoeff_    = 0.0f; // ~30 ms release — coupling-cap RC recovery
    float maxDriveGain_ = 2.5f;   // Maximum drive multiplier (amp-type-dependent character)

    // Per-amp input scaler into the waveshaper. Reflects different tube
    // transconductances: EL34 is ~3× more sensitive (Gm ≈ 11 mA/V) than 6V6
    // (Gm ≈ 4.1 mA/V), so Marshall's Pentode curve saturates earlier at
    // equivalent pre-stage signal. Without this, Marshall would slam the
    // waveshaper at every drive level even with preamp Clean selected.
    float inputScale_ = 1.0f;

    // Post-waveshaper makeup per amp. The Koren-derived curves have slope
    // 0.3-0.5 at the bias-point origin (push-pull reconstruction uses
    // sign(x)·(f(|x|)−f(0)) and cancels even harmonics, which also cancels
    // DC gain), plus NFB subtracts ~2-3 dB, plus Marshall's inputScale=0.25
    // attenuates by -12 dB. Net: the power amp stage currently LOSES ~11 dB
    // at clean signal levels. Real phase-inverter + power-tube stages have
    // NET GAIN (+10 to +20 dB clean). This linear makeup brings clean-level
    // output into the real-amp range without altering harmonic content
    // (applied after the waveshaper so distortion percentages stay the same).
    float postMakeup_ = 1.0f;

    // Per-amp tanh soft-limit ceiling. Push-pull amps with global NFB
    // (Fender/Marshall) need a tighter ceiling so cranked drive doesn't
    // hit the DAW with hard clips. Class A amps without NFB (Vox) need
    // more headroom so the EL84 curve's natural saturation isn't squashed
    // by the limiter — the AC30's character is the curve, not the limit.
    float outputLimitK_ = 1.1f;

    // Global negative feedback loop: output attenuated and subtracted from
    // the phase-inverter input. Fender Deluxe ≈ 12 dB (820 Ω / 47 Ω),
    // Marshall JTM/1959 ≈ 10 dB (27 kΩ / 5 kΩ), Vox AC30 = 0 dB (no NFB).
    // Effects: tightens lows, reduces LF distortion, improves damping.
    // 1-sample delay is at oversampled rate — inaudible.
    float nfbRatio_ = 0.0f;   // 0 = no feedback, higher = more
    float nfbState_ = 0.0f;   // previous output sample (post-waveshaper)

    // Presence-cap in feedback divider: a small cap (5 nF on AB763, similar
    // on Marshall) bypasses the feedback resistor at HF, reducing how much
    // HF is fed back. Less HF feedback ⇒ more HF in output. Modeled as a
    // one-pole LPF on the feedback signal — only the LF/MF content of the
    // output is fed back, HF passes through to the output unaffected.
    float nfbLpfState_ = 0.0f;
    float nfbLpfCoeff_ = 1.0f;     // 1.0 = wide-open, no LPF
    float nfbLpfFreq_  = 5000.0f;  // per-amp presence-cap effective cutoff

    // Phase-inverter + power-tube voltage gain. The tone stack now
    // self-compensates to unity midband (see ToneStack::outputGain_), so this
    // is purely the PI voltage gain — no longer has to undo 30 dB of network
    // loss. Calibrated so Lead-channel cranked drive peaks near ±1.5 into
    // the waveshaper (heavy saturation), Clean-channel clean drive stays
    // near ±0.3 (mild saturation).
    static constexpr float kPreampMakeup = 1.0f;

    // Presence: high shelf in negative feedback
    float presenceFreq_ = 3500.0f;
    float presenceGain_ = 0.0f; // dB

    // Resonance: low shelf in negative feedback
    float resonanceFreq_ = 80.0f;
    float resonanceGain_ = 0.0f; // dB

    // Simple one-pole filters for presence/resonance
    float presenceState_ = 0.0f;
    float presenceCoeff_ = 0.0f;
    float resonanceState_ = 0.0f;
    float resonanceCoeff_ = 0.0f;

    AnalogEmulation::TransformerEmulation transformer_;
    AnalogEmulation::DCBlocker dcBlocker_;

    // Output-transformer HF resonance peak. Real OTs have a leakage-
    // inductance × inter-winding-capacitance resonance that produces a
    // characteristic peak above the audio band — Plexi at ~4-5 kHz with
    // ~+3 dB gives the famous "Plexi bite". Implemented as a peaking biquad
    // applied after the simple OT saturation model in TransformerEmulation.
    struct OtPeak
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float process (float x)
        {
            const float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            if (std::abs (z1) < 1.0e-15f) z1 = 0.0f;
            if (std::abs (z2) < 1.0e-15f) z2 = 0.0f;
            return out;
        }
        void clear() { z1 = z2 = 0.0f; }
    };
    OtPeak otPeak_;
    float  otPeakFreq_   = 5000.0f;
    float  otPeakGainDb_ = 0.0f;
    float  otPeakQ_      = 1.0f;

    // Speaker-impedance-feedback peak. Real guitar cabs have a sharp
    // impedance peak at the cone's fundamental resonance (~80-150 Hz) and
    // the OT's primary load follows that — meaning more voltage swing,
    // hence a small LF gain bump that's NOT in our cab IR (the IR is just
    // a fixed-impedance-load measurement). Implemented as a peaking biquad
    // before the OT HF peak. Per-amp gain reflects how much the global NFB
    // damps the resonance: low NFB (Vox) → strong peak audibly "blooms" at
    // LF; medium NFB (Marshall) → moderate; high NFB (Fender) → minimal.
    OtPeak spkPeak_;
    float  spkPeakFreq_   = 100.0f;
    float  spkPeakGainDb_ = 0.0f;
    float  spkPeakQ_      = 1.5f;

    void updatePresenceCoeff();
    void updateResonanceCoeff();
    void updateNfbLpfCoeff();
    void updateCathodeBloomCoeffs();
    void updateGridCurrentCoeffs();
    void updateOtPeak();
    void updateSpkPeak();
    void updateAmpTypeParams();
};
