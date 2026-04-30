// PowerAmp.cpp — Power amplifier with per-amp-type behavior
//
// Push-pull (Fender/Marshall): signal split into two anti-phase halves,
//   each processed through the waveshaper, then summed differentially.
//   This cancels even harmonics (2nd, 4th, 6th) — the key push-pull signature.
//
// Class A (Vox): single-ended processing with bias offset that pushes the
//   signal into the asymmetric region of the waveshaper, creating even
//   harmonics and earlier compression.

#include "PowerAmp.h"
#include "AnalogEmulation/HardwareProfiles.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PowerAmp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    auto profile = AnalogEmulation::TransformerProfile::createActive (
        0.75f, 0.12f, 1.3f, 14000.0f, 15.0f, 0.01f, 0.005f, 0.7f);

    transformer_.setProfile (profile);
    transformer_.prepare (sampleRate, 1);
    dcBlocker_.prepare (sampleRate, 10.0f);

    powerSupply_.prepare (sampleRate);
    updateAmpTypeParams();
    updatePresenceCoeff();
    updateResonanceCoeff();
    updateNfbLpfCoeff();
    updateCathodeBloomCoeffs();
    updateOtPeak();
    reset();
}

void PowerAmp::reset()
{
    powerSupply_.reset();
    presenceState_ = 0.0f;
    resonanceState_ = 0.0f;
    nfbState_ = 0.0f;
    nfbLpfState_ = 0.0f;
    cathodeEnv_ = 0.0f;
    otPeak_.clear();
    transformer_.reset();
    dcBlocker_.reset();
}

void PowerAmp::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    driveGain_ = 0.8f + drive_ * (maxDriveGain_ - 0.8f);
}

void PowerAmp::setPresence (float value01)
{
    presenceGain_ = std::clamp (value01, 0.0f, 1.0f) * 6.0f;
    updatePresenceCoeff();
}

void PowerAmp::setResonance (float value01)
{
    resonanceGain_ = std::clamp (value01, 0.0f, 1.0f) * 6.0f;
    updateResonanceCoeff();
}

void PowerAmp::setSag (float sag01)
{
    sagAmount_ = std::clamp (sag01, 0.0f, 1.0f);
    powerSupply_.setDepth (sagAmount_);
}

void PowerAmp::setAmpType (AmpType type)
{
    ampType_ = type;
    updateAmpTypeParams();
    driveGain_ = 0.8f + drive_ * (maxDriveGain_ - 0.8f);
    updateNfbLpfCoeff();
    updateCathodeBloomCoeffs();
    updateOtPeak();
}

void PowerAmp::updateAmpTypeParams()
{
    // stageGain/outputGain used to vary per amp to patch preamp attenuation —
    // that work has moved to PreampDSP::outputMakeup_. Here each amp only
    // picks its waveshaper curve, Class-A asymmetry, drive-knob range, and
    // power-supply character. All amps run at the same kPreampMakeup.
    switch (ampType_)
    {
        case AmpType::Fender:
            // 6V6GT beam tetrode curve (Deluxe Reverb). Previously Opto_Tube,
            // which is an optical-compressor asymmetric curve — wrong family.
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Tube6V6;
            biasAsymmetry_ = 0.0f;
            pushPullAsymmetry_ = 0.025f; // 2.5% mismatch — typical AB763 pair
            phaseInvAsymmetry_ = 0.0f;   // LTP PI is balanced
            cathodeBloomAmount_ = 0.0f;  // fixed-bias amp — no cathode bloom
            maxDriveGain_ = 2.0f;
            inputScale_ = 1.0f;  // 6V6 lowest Gm — reference sensitivity
            nfbRatio_ = 0.30f;   // ≈ 12 dB (AB763 820Ω / 47Ω)
            nfbLpfFreq_ = 4000.0f; // AB763 5 nF presence cap effective cutoff
            postMakeup_ = 1.0f;  // PI now provides the driver gain (replaces former 2.5)
            outputLimitK_ = 1.1f;
            otPeakFreq_   = 3500.0f;     // Fender 40W OT — modest HF resonance
            otPeakGainDb_ = 1.5f;
            otPeakQ_      = 1.0f;
            powerSupply_.setType (PowerSupply::Type::Tube5AR4);
            // Fender 40W OT: darker (8 kHz HF), early saturation, asymmetric
            transformer_.setProfile (AnalogEmulation::TransformerProfile::createActive (
                0.72f, 0.14f, 1.30f, 8000.0f, 20.0f, 0.012f, 0.004f, 0.75f));
            break;

        case AmpType::Vox:
            // AC30 Top Boost: 4× EL84 in Class A push-pull (two pairs in
            // parallel, single OT). Modelled as proper push-pull rather than
            // single-ended-with-bias-offset, so even-harmonic cancellation
            // emerges naturally at low/medium drive and breaks down at
            // cranked levels — the actual dynamic that defines AC30 tone.
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::EL84;
            biasAsymmetry_ = 0.0f;       // no static bias offset — proper Class A
            pushPullAsymmetry_ = 0.0f;   // tubes are well-matched
            phaseInvAsymmetry_ = 0.04f;  // cathodyne 4% imbalance: anode/cathode outputs
            cathodeBloomAmount_ = 0.4f;  // AC30 cathode-bias bloom — slow gain reduction
            maxDriveGain_ = 2.5f;
            inputScale_ = 0.7f;  // EL84 higher Gm than 6V6
            nfbRatio_ = 0.0f;    // AC30 has NO negative feedback loop
            nfbLpfFreq_ = 20000.0f; // unused (no NFB) — wide-open sentinel
            postMakeup_ = 1.0f;  // PI now provides the driver gain
            outputLimitK_ = 1.6f;        // loose ceiling — let the EL84 curve drive THD
            otPeakFreq_   = 5500.0f;     // Vox 30W OT — small resonance, contributes chime
            otPeakGainDb_ = 1.5f;
            otPeakQ_      = 1.2f;
            powerSupply_.setType (PowerSupply::Type::TubeGZ34);
            // Vox 30W OT: darker (9 kHz HF), earliest saturation, asymmetric
            transformer_.setProfile (AnalogEmulation::TransformerProfile::createActive (
                0.70f, 0.16f, 1.25f, 9000.0f, 25.0f, 0.013f, 0.004f, 0.78f));
            break;

        case AmpType::Marshall:
        default:
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::EL34_Plexi;
            biasAsymmetry_ = 0.0f;
            pushPullAsymmetry_ = 0.020f; // ~2% — Marshall production pairs run tight
            phaseInvAsymmetry_ = 0.0f;   // LTP PI is balanced
            cathodeBloomAmount_ = 0.0f;  // fixed-bias amp — no cathode bloom
            maxDriveGain_ = 4.0f;
            inputScale_ = 0.25f; // EL34 highest Gm (~11 mA/V, 3× 6V6); widen drive range
            nfbRatio_ = 0.25f;   // ≈ 10 dB (JTM45/1959 27kΩ / 5kΩ presence)
            nfbLpfFreq_ = 5500.0f; // 1959 presence cap — slightly brighter than Fender
            postMakeup_ = 1.0f;  // PI now provides the driver gain (replaces former 4.0)
            outputLimitK_ = 1.1f;
            otPeakFreq_   = 4500.0f;     // Marshall Drake OT — pronounced HF peak ("Plexi bite")
            otPeakGainDb_ = 3.0f;
            otPeakQ_      = 1.5f;
            powerSupply_.setType (PowerSupply::Type::Silicon);
            // Marshall 50W OT: brighter (11 kHz HF), later saturation, more symmetric
            transformer_.setProfile (AnalogEmulation::TransformerProfile::createActive (
                0.80f, 0.10f, 1.20f, 11000.0f, 18.0f, 0.008f, 0.006f, 0.55f));
            break;
    }
}

void PowerAmp::process (float* buffer, int numSamples)
{
    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();
    bool isPushPull = (ampType_ != AmpType::Vox);

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // --- 1. Drive + preamp-makeup gain + per-amp tube sensitivity ---
        // kPreampMakeup compensates the tone-stack insertion loss (~30 dB at
        // flat settings) and represents the phase-inverter + power-tube
        // voltage gain. inputScale_ accounts for per-tube-type Gm differences
        // so Marshall's EL34 doesn't slam the waveshaper just because it has
        // 3× the transconductance of Fender's 6V6.
        // NFB: attenuated output subtracted from input, 1-sample delay
        // (at oversampled rate — inaudible).
        float driven = sample * kPreampMakeup * inputScale_ * driveGain_
                     - nfbRatio_ * nfbState_;

        // --- 2. Power supply: RC-model B+ sag driven by power-tube load ---
        // Returns normalized rail voltage in [vFloor, 1.0]. Lower = more sag.
        float vBplus = powerSupply_.processSample (driven);

        // --- 3. Power tube stage ---
        float saturated;

        if (isPushPull)
        {
            // Push-pull: each tube handles one half of the waveform.
            // sign(x)·(f(|x|)−f(0)) gives ideally-matched cancellation
            // (only odd harmonics survive). Real-world tube pairs are never
            // perfectly matched — 2-3% bias mismatch is typical and adds a
            // small but musically important even-harmonic warmth that pure
            // odd cancellation throws away.
            const float sign = (driven >= 0.0f) ? 1.0f : -1.0f;
            const float absDriven = std::abs (driven);
            const float curveAtZero = waveshaper.process (0.0f, curveType_);
            const float oddPart = sign * (waveshaper.process (absDriven, curveType_) - curveAtZero);

            // Even portion: f(x)+f(−x)−2·f(0). For an odd-symmetric curve
            // this is zero; for the slightly-asymmetric tube curves we use,
            // it captures the natural even-harmonic content of the curve
            // after DC removal. Multiplied by the per-amp mismatch ratio.
            float evenPart = 0.0f;
            if (pushPullAsymmetry_ > 0.0f)
            {
                const float fPos = waveshaper.process (driven, curveType_);
                const float fNeg = waveshaper.process (-driven, curveType_);
                evenPart = pushPullAsymmetry_ * 0.5f * (fPos + fNeg - 2.0f * curveAtZero);
            }

            saturated = oddPart + evenPart;
        }
        else
        {
            // Class A push-pull (Vox AC30): both EL84s biased deep into
            // conduction (-8V Vgk on a -12 to 0V grid range), so they stay
            // in the linear region for low/medium drive — the differential
            // sum f(+x) − f(−x) cancels even harmonics there. As drive
            // approaches and exceeds the linear range, one or both tubes
            // start clipping asymmetrically and even harmonics emerge —
            // that's the AC30 dynamic transition that defines its sound.
            //
            // The cathodyne PI feeds slightly different signals to the two
            // tubes (anode-output and cathode-output of a single triode are
            // ~3-5% imbalanced), modelled here by the 1±phaseInvAsymmetry_
            // skew on the upper/lower grid drives.
            const float compDriven = driven * vBplus;
            const float curveAtZero = waveshaper.process (0.0f, curveType_);
            const float upperGrid =  compDriven * (1.0f + phaseInvAsymmetry_);
            const float lowerGrid = -compDriven * (1.0f - phaseInvAsymmetry_);
            const float upperHalf = waveshaper.process (upperGrid, curveType_) - curveAtZero;
            const float lowerHalf = waveshaper.process (lowerGrid, curveType_) - curveAtZero;
            saturated = (upperHalf - lowerHalf) * 0.5f;
        }

        // --- 3a. Cathode-bias bloom (Vox AC30) ---
        // The cathode resistor on cathode-biased power amps heats up under
        // sustained heavy playing, shifting the bias more negative and
        // slowly reducing tube gain (~hundreds of ms attack, ~seconds
        // release). This is the famous AC30 "bloom" / dynamic compression.
        // No-op for fixed-bias amps (Fender, Marshall) where amount = 0.
        if (cathodeBloomAmount_ > 0.0f)
        {
            const float instPower = driven * driven;
            const float coeff = (instPower > cathodeEnv_)
                ? cathodeAttackCoeff_ : cathodeReleaseCoeff_;
            cathodeEnv_ += (instPower - cathodeEnv_) * coeff;
            if (cathodeEnv_ < 1.0e-15f) cathodeEnv_ = 0.0f;
            const float bloomGain = 1.0f - cathodeBloomAmount_
                                  * std::min (cathodeEnv_, 1.0f) * 0.20f;
            saturated *= bloomGain;
        }

        // --- 3b. Supply sag on output ---
        // Real physics: power-tube plate swing is bounded by B+. When the
        // supply sags, maximum output amplitude drops in proportion. Per-amp
        // sag depth and time constants are baked into PowerSupply by ampType.
        saturated *= vBplus;

        // --- 3c. Capture NFB state for next sample ---
        // Apply the presence-cap LPF: only LF/MF content goes into the
        // feedback path, so HF passes through to the output unchallenged.
        // This is the physical effect of the 5 nF cap parallel to the
        // feedback resistor on AB763 / 1959 schematics.
        nfbLpfState_ += nfbLpfCoeff_ * (saturated - nfbLpfState_);
        if (std::abs (nfbLpfState_) < 1e-15f) nfbLpfState_ = 0.0f;
        nfbState_ = nfbLpfState_;

        // --- 5. Presence + Resonance ---
        float hpOut = saturated - presenceState_;
        presenceState_ += hpOut * presenceCoeff_;
        if (std::abs (presenceState_) < 1e-15f) presenceState_ = 0.0f;

        resonanceState_ += (saturated - resonanceState_) * resonanceCoeff_;
        if (std::abs (resonanceState_) < 1e-15f) resonanceState_ = 0.0f;

        saturated += hpOut * (presenceGain_ / 6.0f) * 0.3f;
        saturated += resonanceState_ * (resonanceGain_ / 6.0f) * 0.3f;

        // --- 5. Output transformer ---
        saturated = transformer_.processSample (saturated, 0);

        // --- 5b. OT leakage-inductance HF resonance peak ---
        // Real OTs aren't flat-frequency: leakage inductance × inter-winding
        // capacitance produces a resonance peak above the audio band that
        // contributes to the amp's "edge". Different OTs peak at different
        // frequencies (Plexi: ~4.5 kHz +3 dB, Fender: ~3.5 kHz +1.5 dB,
        // Vox: ~5.5 kHz +1.5 dB).
        saturated = otPeak_.process (saturated);

        // --- 6. DC block ---
        saturated = dcBlocker_.processSample (saturated);

        // --- 7. Post-makeup + soft limit ---
        // Linear makeup brings clean levels to real-amp range. tanh ceiling
        // is per-amp: push-pull amps with NFB use 1.1 (tight, prevents DAW
        // clipping under cranked drive), Vox uses 1.5 (loose — Class A
        // without NFB relies on the EL84 curve's own saturation for
        // character, and a tight limiter would squash it).
        const float y = saturated * postMakeup_;
        buffer[i] = std::tanh (y / outputLimitK_) * outputLimitK_;
    }
}

void PowerAmp::updatePresenceCoeff()
{
    float w = 2.0f * kPi * presenceFreq_ / static_cast<float> (sampleRate_);
    presenceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateResonanceCoeff()
{
    float w = 2.0f * kPi * resonanceFreq_ / static_cast<float> (sampleRate_);
    resonanceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateNfbLpfCoeff()
{
    const float w = 2.0f * kPi * nfbLpfFreq_ / static_cast<float> (sampleRate_);
    nfbLpfCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateCathodeBloomCoeffs()
{
    // Slow envelope — the cathode resistor's thermal/electrical settling
    // time. Values chosen so a sustained chord blooms over ~half a second
    // and recovers cleanly between notes within a couple of seconds.
    const float attackMs  = 500.0f;
    const float releaseMs = 2000.0f;
    const float sr = static_cast<float> (sampleRate_);
    cathodeAttackCoeff_  = 1.0f - std::exp (-1000.0f / (attackMs  * sr));
    cathodeReleaseCoeff_ = 1.0f - std::exp (-1000.0f / (releaseMs * sr));
}

void PowerAmp::updateOtPeak()
{
    // RBJ peaking-EQ biquad — designs in place into otPeak_'s coefficients.
    const double A     = std::pow (10.0, static_cast<double> (otPeakGainDb_) / 40.0);
    const double omega = 2.0 * static_cast<double> (kPi)
                       * static_cast<double> (otPeakFreq_)
                       / sampleRate_;
    const double cosw  = std::cos (omega);
    const double sinw  = std::sin (omega);
    const double alpha = sinw / (2.0 * static_cast<double> (otPeakQ_));

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cosw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha / A;

    const double inv = 1.0 / a0;
    otPeak_.b0 = static_cast<float> (b0 * inv);
    otPeak_.b1 = static_cast<float> (b1 * inv);
    otPeak_.b2 = static_cast<float> (b2 * inv);
    otPeak_.a1 = static_cast<float> (a1 * inv);
    otPeak_.a2 = static_cast<float> (a2 * inv);
}

