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
    updateGridCurrentCoeffs();
    updateOtPeak();
    updateSpkPeak();
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
    gridCharge_ = 0.0f;
    otPeak_.clear();
    spkPeak_.clear();
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
    updateGridCurrentCoeffs();
    updateOtPeak();
    updateSpkPeak();
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
            maxDriveGain_ = 3.5f; // widened drive range — cranked AB763 hits 12-18% THD
            inputScale_ = 1.0f;  // 6V6 lowest Gm — reference sensitivity
            nfbRatio_ = 0.30f;   // ≈ 12 dB (AB763 820Ω / 47Ω)
            nfbLpfFreq_ = 4000.0f; // AB763 5 nF presence cap effective cutoff
            postMakeup_ = 1.0f;  // reference level — Fender is the loudness anchor
            // Loose tanh — the per-amp limiter used to be tight at K=1.1 which
            // smoothed away the 6V6 curve's saturation harmonics ("lifeless").
            // K=2.0 is nearly transparent; chain-end limiter at K=1.6 in the
            // engine catches actual peaks.
            outputLimitK_ = 2.0f;
            gridChargeThreshold_ = 1.5f; // 6V6 has higher grid headroom — milder blocking
            gridChargeAmount_    = 0.18f;
            otPeakFreq_   = 3500.0f;     // Fender 40W OT — modest HF resonance
            otPeakGainDb_ = 1.5f;
            otPeakQ_      = 1.0f;
            // 12" Jensen-style cone: ~95 Hz fundamental. Strong NFB (12 dB)
            // damps the impedance peak heavily — only a small audible bump.
            spkPeakFreq_   = 95.0f;
            spkPeakGainDb_ = 1.5f;
            spkPeakQ_      = 1.5f;
            powerSupply_.setType (PowerSupply::Type::Tube5AR4);
            // Fender 40W OT: darker (8 kHz HF), early saturation, asymmetric.
            // Hysteresis 0.10 — moderate iron mass, audible 3rd-order memory.
            transformer_.setProfile (AnalogEmulation::TransformerProfile::createActive (
                0.72f, 0.14f, 1.30f, 8000.0f, 20.0f, 0.012f, 0.004f, 0.75f, 0.10f));
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
            maxDriveGain_ = 5.0f; // widened — AC30 cranked is genuinely choking
            inputScale_ = 0.7f;  // EL84 higher Gm than 6V6
            nfbRatio_ = 0.0f;    // AC30 has NO negative feedback loop
            nfbLpfFreq_ = 20000.0f; // unused (no NFB) — wide-open sentinel
            postMakeup_ = 1.15f; // +1.2 dB to match Fender's clean RMS at matched user settings
            outputLimitK_ = 2.0f;        // loose — EL84 curve is the saturation source, not tanh
            gridChargeThreshold_ = 1.1f; // EL84 small-tube — lower grid headroom, blocks earlier
            gridChargeAmount_    = 0.20f;
            otPeakFreq_   = 5500.0f;     // Vox 30W OT — small resonance, contributes chime
            otPeakGainDb_ = 1.5f;
            otPeakQ_      = 1.2f;
            // 12" Celestion Blue: ~110 Hz fundamental, low Q. NO global NFB
            // means the impedance peak rings pretty hard — the AC30 LF
            // "bloom" lives here, not in the cathode-bias dynamic alone.
            spkPeakFreq_   = 110.0f;
            spkPeakGainDb_ = 5.0f;
            spkPeakQ_      = 2.0f;
            powerSupply_.setType (PowerSupply::Type::TubeGZ34);
            // Vox 30W OT: darker (9 kHz HF), earliest saturation, asymmetric.
            // Hysteresis 0.07 — smaller iron, lighter memory term.
            transformer_.setProfile (AnalogEmulation::TransformerProfile::createActive (
                0.70f, 0.16f, 1.25f, 9000.0f, 25.0f, 0.013f, 0.004f, 0.78f, 0.07f));
            break;

        case AmpType::Marshall:
        default:
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::EL34_Plexi;
            biasAsymmetry_ = 0.0f;
            pushPullAsymmetry_ = 0.020f; // ~2% — Marshall production pairs run tight
            phaseInvAsymmetry_ = 0.0f;   // LTP PI is balanced
            cathodeBloomAmount_ = 0.0f;  // fixed-bias amp — no cathode bloom
            maxDriveGain_ = 6.0f; // widened — cranked Plexi is screaming, not polite
            inputScale_ = 0.25f; // EL34 highest Gm (~11 mA/V, 3× 6V6); widen drive range
            nfbRatio_ = 0.25f;   // ≈ 10 dB (JTM45/1959 27kΩ / 5kΩ presence)
            nfbLpfFreq_ = 5500.0f; // 1959 presence cap — slightly brighter than Fender
            // +12 dB to match Fender's clean RMS at matched user settings.
            // Marshall's chain attenuates heavily at clean drive (V1A cathode-bypass
            // shelf cuts ~7 dB LF + EL34 inputScale=0.25 cuts another ~12 dB into the
            // waveshaper). Pre-tanh makeup brings clean output up; the loose tanh
            // ceiling at outputLimitK_ lets the EL34_Plexi curve's saturation
            // harmonics through unmolested at cranked drive.
            postMakeup_ = 4.0f;
            outputLimitK_ = 2.0f; // loose — EL34_Plexi curve provides the Plexi bite
            gridChargeThreshold_ = 1.2f; // EL34 grids hard-block on hot input — Plexi "bark"
            gridChargeAmount_    = 0.30f;
            otPeakFreq_   = 4500.0f;     // Marshall Drake OT — pronounced HF peak ("Plexi bite")
            otPeakGainDb_ = 3.0f;
            otPeakQ_      = 1.5f;
            // 4×12 Celestion: ~100 Hz fundamental, sharper than open-back combos
            // because of the closed cab loading. Moderate NFB (10 dB) gives
            // partial damping — audible LF bump but not as pronounced as Vox.
            spkPeakFreq_   = 100.0f;
            spkPeakGainDb_ = 2.5f;
            spkPeakQ_      = 1.8f;
            powerSupply_.setType (PowerSupply::Type::Silicon);
            // Marshall 50W OT: brighter (11 kHz HF), later saturation, more symmetric.
            // Hysteresis 0.12 — biggest iron, most pronounced memory contribution
            // (part of the "Plexi thickness" character at cranked drive).
            transformer_.setProfile (AnalogEmulation::TransformerProfile::createActive (
                0.80f, 0.10f, 1.20f, 11000.0f, 18.0f, 0.008f, 0.006f, 0.55f, 0.12f));
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
        //
        // Grid-current ducking: the previous sample's grid-charge state ducks
        // this sample's input, simulating the coupling-cap RC pulling down the
        // previous stage's plate when the grid drew current. Subtle (-1 to
        // -3 dB transient duck on hot input) but contributes the "ghost note"
        // / sustainer character pure waveshapers can't produce.
        const float duckGain = 1.0f - gridChargeAmount_ * std::min (gridCharge_, 1.0f);

        // NFB: attenuated output subtracted from input, 1-sample delay
        // (at oversampled rate — inaudible).
        float driven = sample * duckGain * kPreampMakeup * inputScale_ * driveGain_
                     - nfbRatio_ * nfbState_;

        // Update grid-charge envelope from the actual grid voltage (= driven).
        // Excess above threshold → grid draws current (fast-attack accumulation);
        // excess below → cap discharges through the next stage's grid resistor
        // (slow release). No-op when amount=0.
        if (gridChargeAmount_ > 0.0f)
        {
            const float excess = std::max (std::abs (driven) - gridChargeThreshold_, 0.0f);
            const float coeff = (excess > gridCharge_) ? gridAttackCoeff_ : gridReleaseCoeff_;
            gridCharge_ += (excess - gridCharge_) * coeff;
            if (gridCharge_ < 1.0e-15f) gridCharge_ = 0.0f;
        }

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

        // --- 5a. Speaker-impedance LF resonance ---
        // Cab impedance peaks at the cone's fundamental (~95-110 Hz), and
        // the OT primary load follows that — small LF bump the cab IR
        // can't deliver alone (IRs are measured at fixed impedance load).
        // Per-amp damping reflects how much the global NFB tames the peak:
        // Vox (no NFB) lets it ring; Fender / Marshall damp it heavily.
        saturated = spkPeak_.process (saturated);

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

void PowerAmp::updateGridCurrentCoeffs()
{
    // Fast attack (~1 ms) — grid current draw starts almost instantly when
    // the grid swings positive of cathode. Slow release (~30 ms) — modelling
    // the coupling-cap RC discharge through the next-stage grid resistor.
    // The release time is roughly the time it takes for the previous stage's
    // plate to recover from a transient grid-current pull.
    const float attackMs  = 1.0f;
    const float releaseMs = 30.0f;
    const float sr = static_cast<float> (sampleRate_);
    gridAttackCoeff_  = 1.0f - std::exp (-1000.0f / (attackMs  * sr));
    gridReleaseCoeff_ = 1.0f - std::exp (-1000.0f / (releaseMs * sr));
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

void PowerAmp::updateSpkPeak()
{
    // Same RBJ peaking-EQ form as updateOtPeak, but at LF for the speaker
    // cone fundamental resonance. Per-amp gain captures NFB damping in a
    // single coefficient (real NFB would damp the resonance dynamically;
    // we precompute the post-damping audible peak instead — cheaper +
    // user can't dial NFB at runtime anyway).
    const double A     = std::pow (10.0, static_cast<double> (spkPeakGainDb_) / 40.0);
    const double omega = 2.0 * static_cast<double> (kPi)
                       * static_cast<double> (spkPeakFreq_)
                       / sampleRate_;
    const double cosw  = std::cos (omega);
    const double sinw  = std::sin (omega);
    const double alpha = sinw / (2.0 * static_cast<double> (spkPeakQ_));

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cosw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha / A;

    const double inv = 1.0 / a0;
    spkPeak_.b0 = static_cast<float> (b0 * inv);
    spkPeak_.b1 = static_cast<float> (b1 * inv);
    spkPeak_.b2 = static_cast<float> (b2 * inv);
    spkPeak_.a1 = static_cast<float> (a1 * inv);
    spkPeak_.a2 = static_cast<float> (a2 * inv);
}

