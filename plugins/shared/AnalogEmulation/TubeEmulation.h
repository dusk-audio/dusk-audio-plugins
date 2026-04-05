// TubeEmulation.h — Vacuum tube modeling (12AX7, 12AT7, 12BH7, 6SN7)
// Uses Norman Koren's SPICE triode model with load-line solving for per-type
// transfer curves that capture the distinct clipping character of each tube.

#pragma once

#include "DCBlocker.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace AnalogEmulation {

class TubeEmulation
{
public:
    enum class TubeType
    {
        Triode_12AX7,    // High gain (~100), strong 2nd harmonic, sharp asymmetric clip
        Triode_12AT7,    // Medium gain (~60), softer clip, more headroom
        Triode_12BH7,    // Output driver (~16), gentle clip, high linearity
        Triode_6SN7      // Dual triode (~20), warm character, moderate asymmetry
    };

    TubeEmulation()
    {
        updateTubeParameters();  // Also builds the transfer table for default type
    }

    void prepare(double sampleRate, int numChannels = 2)
    {
        this->sampleRate = sampleRate;
        this->numChannels = numChannels;

        // Prepare DC blockers
        for (int ch = 0; ch < 2; ++ch)
            dcBlocker[ch].prepare(sampleRate, 10.0f);

        // Update filter coefficients for new sample rate
        updateCoefficients();
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            millerCapState[ch] = 0.0f;
            gridCurrent[ch] = 0.0f;
            cathodeBypassState[ch] = 0.0f;
            dcBlocker[ch].reset();
        }
    }

    void setTubeType(TubeType type)
    {
        currentType = type;
        updateTubeParameters();
    }

    TubeType getTubeType() const { return currentType; }

    void setDrive(float driveAmount)
    {
        drive = std::clamp(driveAmount, 0.0f, 1.0f);
        // Exponential curve: drive 0→1x, 0.5→4x, 1.0→13x
        // Models the real voltage swing into a tube grid — a 12AX7 sees
        // up to ~40Vpp at the plate, so input must swing hard to clip.
        inputGain = std::exp(drive * 2.565f);  // e^0=1, e^1.28≈3.6, e^2.565≈13
    }

    float getDrive() const { return drive; }

    void setBiasPoint(float bias)
    {
        biasOffset = std::clamp(bias, -1.0f, 1.0f) * 0.2f;
    }

    // Dynamic modulation for guitar volume cleanup (called per-sample from GainStage)
    // biasShift: 0=no change, 1=bias fully centered toward linear region
    void modulateBias (float biasShift)
    {
        dynamicBiasShift = std::clamp (biasShift, 0.0f, 1.0f);
    }

    // gridBoost: 0=no change, higher=grid current threshold rises (less compression)
    void modulateGridThreshold (float gridBoost)
    {
        dynamicGridBoost = std::clamp (gridBoost, 0.0f, 1.0f);
    }

    // Process a single sample through the tube stage
    float processSample(float input, int channel)
    {
        channel = std::clamp(channel, 0, 1);

        // Apply input gain (drive)
        float driven = input * inputGain;

        // Add bias offset — dynamicBiasShift moves bias toward center (cleaner)
        float effectiveBias = biasOffset * (1.0f - dynamicBiasShift);
        float gridVoltage = driven + effectiveBias;

        // Grid current modeling — dynamicGridBoost raises threshold (less compression at low input)
        float effectiveThreshold = gridCurrentThreshold * (1.0f + dynamicGridBoost);
        if (gridVoltage > effectiveThreshold)
        {
            float excess = gridVoltage - gridCurrentThreshold;
            gridCurrent[channel] = excess * gridCurrentCoeff;
            gridVoltage -= gridCurrent[channel];
        }
        else
        {
            gridCurrent[channel] *= gridCurrentDischargeCoeff;
        }

        // Apply plate transfer function (the main tube nonlinearity)
        float plateVoltage = applyPlateTransferFunction(gridVoltage);

        // Cathode bypass capacitor (affects frequency response)
        cathodeBypassState[channel] = cathodeBypassState[channel] * cathodeBypassCoeff
                                    + plateVoltage * (1.0f - cathodeBypassCoeff);
        float cathodeEffect = plateVoltage * (1.0f - cathodeBypassAmount)
                            + cathodeBypassState[channel] * cathodeBypassAmount;

        // Miller capacitance (HF rolloff, more pronounced at higher gains)
        float hfContent = cathodeEffect - millerCapState[channel];
        millerCapState[channel] += hfContent * millerCapCoeff;
        float output = cathodeEffect - hfContent * millerCapEffect * drive;

        // Output scaling to maintain approximate unity gain at low drive
        output *= outputScaling;

        // DC blocking
        return dcBlocker[channel].processSample(output);
    }

    // Block processing
    void processBlock(float* const* channelData, int numSamples)
    {
        for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
        {
            float* data = channelData[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = processSample(data[i], ch);
            }
        }
    }

private:
    static constexpr int TRANSFER_TABLE_SIZE = 4096;
    std::array<float, TRANSFER_TABLE_SIZE> plateTransferTable {};

    TubeType currentType = TubeType::Triode_12AX7;
    double sampleRate = 44100.0;
    int numChannels = 2;

    // Drive settings
    float drive = 0.0f;
    float inputGain = 1.0f;
    float outputScaling = 1.0f;
    float biasOffset = 0.0f;

    // Dynamic modulation state (set per-sample by GainStage for volume cleanup)
    float dynamicBiasShift = 0.0f;   // 0=no change, 1=bias centered
    float dynamicGridBoost = 0.0f;   // 0=no change, 1=threshold doubled

    // Tube-specific parameters
    float gridCurrentThreshold = 0.5f;
    float gridCurrentCoeff = 0.2f;
    float cathodeBypassCoeff = 0.98f;
    float cathodeBypassAmount = 0.3f;
    float millerCapCoeff = 0.3f;
    float millerCapEffect = 0.1f;

    // Base coefficients (set by tube type, before sample-rate adjustment)
    float cathodeBypassCoeffBase = 0.98f;
    float millerCapCoeffBase = 0.3f;
    float gridCurrentDischargeBase = 0.95f;
    float gridCurrentDischargeCoeff = 0.95f;

    // Per-channel state
    float millerCapState[2] = {0.0f, 0.0f};
    float gridCurrent[2] = {0.0f, 0.0f};
    float cathodeBypassState[2] = {0.0f, 0.0f};
    DCBlocker dcBlocker[2];

    // ========================================================================
    // Koren SPICE triode model parameters
    // ========================================================================

    struct KorenParams
    {
        float mu;       // Amplification factor
        float Ex;       // Plate current exponent (Child-Langmuir ≈ 1.5)
        float Kg1;      // Cathode-grid coupling coefficient
        float Kp;       // Koren plate parameter (controls grid-plate interaction)
        float Kvb;      // Knee voltage parameter
        float Bplus;    // Plate supply voltage (V)
        float Rp;       // Plate load resistor (ohms)
        float VgBias;   // Grid bias voltage at quiescent point (V)
        float vgSwing;  // Grid voltage per unit of normalized input (V)
    };

    static KorenParams getKorenParams (TubeType type)
    {
        switch (type)
        {
            case TubeType::Triode_12AX7:
                // High-mu preamp triode. Sharp asymmetric clipping, dominant 2nd harmonic.
                // Typical circuit: B+=300V, Rp=100k, self-bias Rk=1.5k → Vg≈-1.2V
                return { 100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
                         300.0f, 100000.0f, -1.2f, 1.0f };

            case TubeType::Triode_12AT7:
                // Medium-mu triode. More headroom, softer onset, used as phase inverter.
                // B+=300V, Rp=100k, Rk=2.2k → Vg≈-2.0V
                return { 60.0f, 1.35f, 460.0f, 300.0f, 300.0f,
                         300.0f, 100000.0f, -2.0f, 1.5f };

            case TubeType::Triode_12BH7:
                // Low-mu power driver. Very linear until hard clip, high headroom.
                // B+=250V, Rp=22k, Rk=680 → Vg≈-8V
                return { 16.5f, 1.3f, 160.0f, 84.0f, 300.0f,
                         250.0f, 22000.0f, -8.0f, 4.0f };

            case TubeType::Triode_6SN7:
                // Classic dual triode. Moderate asymmetry, warm even-order harmonics.
                // B+=250V, Rp=47k, Rk=1k → Vg≈-5V
                return { 20.0f, 1.3f, 1180.0f, 115.0f, 300.0f,
                         250.0f, 47000.0f, -5.0f, 3.0f };
        }
        return { 100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
                 300.0f, 100000.0f, -1.2f, 1.0f };
    }

    // Koren triode plate current model:
    //   E1 = (Vp/Kp) * ln(1 + exp(Kp * (1/mu + Vg/sqrt(Kvb + Vp²))))
    //   Ip = (E1^Ex) / Kg1   for E1 > 0, else 0
    static float korenPlateCurrent (float Vg, float Vp, const KorenParams& k)
    {
        float sqrtTerm = std::sqrt (k.Kvb + Vp * Vp);
        float expArg = k.Kp * (1.0f / k.mu + Vg / sqrtTerm);
        // Clamp to prevent overflow in exp()
        expArg = std::clamp (expArg, -80.0f, 80.0f);
        float E1 = (Vp / k.Kp) * std::log (1.0f + std::exp (expArg));
        if (E1 <= 0.0f) return 0.0f;
        return std::pow (E1, k.Ex) / k.Kg1;
    }

    // Solve the DC load line: Vp + Ip(Vg,Vp)*Rp = B+
    // Returns plate voltage at the given grid voltage via Newton-Raphson
    static float solveLoadLine (float Vg, const KorenParams& k)
    {
        float Vp = k.Bplus * 0.5f;  // Initial guess: midpoint

        for (int iter = 0; iter < 20; ++iter)
        {
            float Ip = korenPlateCurrent (Vg, Vp, k);
            float f = Vp + Ip * k.Rp - k.Bplus;

            // Numerical derivative df/dVp
            constexpr float dVp = 0.1f;
            float IpPlus = korenPlateCurrent (Vg, Vp + dVp, k);
            float dfdVp = 1.0f + (IpPlus - Ip) * k.Rp / dVp;

            float step = f / std::max (dfdVp, 0.01f);
            Vp -= step;
            Vp = std::clamp (Vp, 1.0f, k.Bplus);

            if (std::abs (step) < 0.01f) break;
        }
        return Vp;
    }

    // Build per-tube-type transfer table from Koren model + load line
    void initializePlateTransferFunction (TubeType type)
    {
        auto k = getKorenParams (type);

        // Quiescent operating point
        float VpQ = solveLoadLine (k.VgBias, k);

        // Small-signal gain (dVp/dVg at bias point, inverted because Vp drops as Vg rises)
        constexpr float dVg = 0.01f;
        float VpNudge = solveLoadLine (k.VgBias + dVg, k);
        float ssGain = (VpQ - VpNudge) / dVg;  // Positive value (stage inverts)

        // Normalization: output ≈ input for small signals
        float normFactor = (ssGain > 0.1f) ? 1.0f / (ssGain * k.vgSwing) : 1.0f;

        for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i)
        {
            float x = (static_cast<float> (i) / (TRANSFER_TABLE_SIZE - 1)) * 4.0f - 2.0f;
            float Vg = k.VgBias + x * k.vgSwing;
            float Vp = solveLoadLine (Vg, k);

            // Invert (tube stage inverts), center on quiescent, normalize
            plateTransferTable[static_cast<size_t> (i)] = -(Vp - VpQ) * normFactor;
        }
    }

    float applyPlateTransferFunction (float gridVoltage)
    {
        float normalized = (gridVoltage + 2.0f) * 0.25f;
        normalized = std::clamp (normalized, 0.0f, 0.9999f);

        float idx = normalized * (TRANSFER_TABLE_SIZE - 1);
        int i0 = static_cast<int> (idx);
        int i1 = std::min (i0 + 1, TRANSFER_TABLE_SIZE - 1);
        float frac = idx - static_cast<float> (i0);

        return plateTransferTable[static_cast<size_t> (i0)] * (1.0f - frac)
             + plateTransferTable[static_cast<size_t> (i1)] * frac;
    }

    void updateTubeParameters()
    {
        // Rebuild the plate transfer table for this tube type (Koren model)
        initializePlateTransferFunction (currentType);

        switch (currentType)
        {
            case TubeType::Triode_12AX7:
                gridCurrentThreshold = 0.4f;
                gridCurrentCoeff = 0.25f;
                cathodeBypassCoeffBase = 0.98f;
                cathodeBypassAmount = 0.35f;
                millerCapCoeffBase = 0.35f;
                millerCapEffect = 0.12f;
                outputScaling = 0.8f;
                break;

            case TubeType::Triode_12AT7:
                gridCurrentThreshold = 0.5f;
                gridCurrentCoeff = 0.2f;
                cathodeBypassCoeffBase = 0.97f;
                cathodeBypassAmount = 0.3f;
                millerCapCoeffBase = 0.25f;
                millerCapEffect = 0.08f;
                outputScaling = 0.85f;
                break;

            case TubeType::Triode_12BH7:
                gridCurrentThreshold = 0.6f;
                gridCurrentCoeff = 0.15f;
                cathodeBypassCoeffBase = 0.96f;
                cathodeBypassAmount = 0.25f;
                millerCapCoeffBase = 0.2f;
                millerCapEffect = 0.05f;
                outputScaling = 0.9f;
                break;

            case TubeType::Triode_6SN7:
                gridCurrentThreshold = 0.45f;
                gridCurrentCoeff = 0.22f;
                cathodeBypassCoeffBase = 0.975f;
                cathodeBypassAmount = 0.32f;
                millerCapCoeffBase = 0.28f;
                millerCapEffect = 0.1f;
                outputScaling = 0.85f;
                break;
        }

        updateCoefficients();
    }

    void updateCoefficients()
    {
        float rateRatio = 44100.0f / static_cast<float> (sampleRate);
        cathodeBypassCoeff = std::pow (cathodeBypassCoeffBase, rateRatio);
        millerCapCoeff = std::pow (millerCapCoeffBase, rateRatio);
        gridCurrentDischargeCoeff = std::pow (gridCurrentDischargeBase, rateRatio);
    }
};

} // namespace AnalogEmulation
