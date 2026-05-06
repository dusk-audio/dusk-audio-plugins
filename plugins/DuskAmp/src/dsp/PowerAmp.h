// SPDX-License-Identifier: GPL-3.0-or-later

// PowerAmp.h — Amp-specific power amplifier with negative feedback loop
//
// The power amp is where "feel" comes from. Three critical differences per amp:
// 1. Power tube type (6L6/EL84/EL34) → different waveshaper + compression
// 2. Negative feedback ratio → controls headroom and distortion character
// 3. Sag behavior → how the B+ supply responds to transients
//
// The negative feedback loop from output transformer secondary back to the
// phase inverter is what makes Presence/Resonance controls feel right.
// Presence = shelf filter IN the feedback path (not feedforward EQ).

#pragma once

#include "PreampModel.h" // for AmpType enum
#include "AnalogEmulation/WaveshaperCurves.h"
#include "AnalogEmulation/TransformerEmulation.h"
#include "AnalogEmulation/DCBlocker.h"

class PowerAmp
{
public:
    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setSag (float sag01);
    void setAmpType (AmpType type);
    void process (float* buffer, int numSamples);

    // Diagnostic-only override of the per-amp isPushPull flag. Intended
    // for the standalone DSP test harness; production code should leave
    // the config-driven default in place.
    void setIsPushPullOverride (bool on) { config_.isPushPull = on; }

private:
    // Per-amp-type configuration
    struct PowerAmpConfig
    {
        AnalogEmulation::WaveshaperCurves::CurveType curveType;
        float nfbRatio;         // Negative feedback ratio (0 = none, 1 = full)
        float sagAttackMs;
        float sagReleaseMs;
        float maxDriveGain;     // Maximum gain multiplier from drive knob
        float biasAsymmetry;    // Class A bias offset (0 = symmetric push-pull)

        // Push-pull (Class AB) vs single-ended (Class A) topology.
        // True for Fender / Marshall: model the phase-inverter + paired-tubes
        // + output-transformer subtraction as out = 0.5 * (f(x) - f(-x)),
        // which cancels even-order harmonics from the asymmetric tube curve
        // and doubles odd-order — the defining sound of Class AB.
        // False for Vox: single tube path keeps even harmonics (Class A).
        bool  isPushPull;

        // Per-amp sag depth coefficient. Total sag reduction =
        // sagAmount * env * sagDepth. Real-amp targets at fully cranked:
        // Fender 5AR4 tube rectifier ~5dB (deep), Marshall solid-state
        // bridge ~1dB (shallow), Vox GZ34 ~3dB (medium). Previously a
        // single hardcoded 0.3 across all amps gave at most -1.4dB —
        // too gentle for vintage tube-rectified character.
        float sagDepth;

        // Transformer profile
        float xfmrSatThreshold;
        float xfmrSatAmount;
        float xfmrLFSat;
        float xfmrHFRolloff;
    };

    static PowerAmpConfig getConfigForAmpType (AmpType type);

    double sampleRate_ = 44100.0;
    AmpType currentType_ = AmpType::Marshall;
    PowerAmpConfig config_;

    // Drive
    float drive_ = 0.3f;
    float driveGain_ = 1.0f;

    // Sag
    float sagAmount_ = 0.3f;
    float sagEnvelope_ = 0.0f;
    float sagAttackCoeff_ = 0.0f;
    float sagReleaseCoeff_ = 0.0f;

    // Presence / Resonance (applied in NFB path)
    float presenceAmount_ = 0.5f;  // 0-1 from user
    float resonanceAmount_ = 0.5f; // 0-1 from user

    // Presence: one-pole HPF in feedback path (~3.5kHz)
    float presenceFilterState_ = 0.0f;
    float presenceFilterCoeff_ = 0.0f;
    static constexpr float kPresenceFreq = 3500.0f;

    // Resonance: one-pole LPF in feedback path (~80Hz)
    float resonanceFilterState_ = 0.0f;
    float resonanceFilterCoeff_ = 0.0f;
    static constexpr float kResonanceFreq = 80.0f;

    // Negative feedback loop state (1-sample delay)
    float previousOutput_ = 0.0f;

    AnalogEmulation::TransformerEmulation transformer_;
    AnalogEmulation::DCBlocker dcBlocker_;

    void updateDriveGain();
    void updateSagCoeffs();
    void updateFilterCoeffs();
};
