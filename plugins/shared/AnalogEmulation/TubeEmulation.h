// TubeEmulation.h — Vacuum tube modeling (12AX7, 12AT7, 12BH7, 6SN7)

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
        Triode_12AX7,    // High gain (~100), more 2nd harmonic
        Triode_12AT7,    // Medium gain (~60)
        Triode_12BH7,    // Output driver (~16), opto compressor output stage
        Triode_6SN7      // Dual triode, warm character
    };

    TubeEmulation()
    {
        initializePlateTransferFunction();
        updateTubeParameters();
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
        inputGain = 1.0f + drive * 2.0f;
    }

    float getDrive() const { return drive; }

    void setBiasPoint(float bias)
    {
        biasOffset = std::clamp(bias, -1.0f, 1.0f) * 0.2f;
    }

    // Reinitialize transfer function using the Koren vacuum tube model with
    // real circuit parameters. This replaces the generic transfer function with
    // a physically accurate curve derived from the tube's plate current equation.
    //
    // Parameters from Norman Koren's improved tube model:
    //   mu    = amplification factor
    //   Kp    = plate voltage influence on cutoff
    //   Kvb   = knee voltage parameter
    //   Ex    = exponent (typically 1.4 for triodes)
    //   Kg1   = grid 1 gain coefficient
    //   Vb    = supply voltage (B+)
    //   Rp    = plate load resistor
    //   Rk    = cathode resistor (0 if bypassed)
    void initializeKorenTransferFunction (float mu, float Kp, float Kvb,
                                          float Ex, float Kg1,
                                          float Vb, float Rp, float Rk)
    {
        // For bypassed cathode (Rk=0), use a small Rk for DC bias stability
        // but allow full AC gain. This prevents the solver from producing
        // physically impossible plate currents.
        float Rk_eff = std::max (Rk, 820.0f);  // minimum ~820 ohms for stability

        // Find quiescent point (no signal) for normalization
        float Ip_q = solveKorenCurrent (0.0f, mu, Kp, Kvb, Ex, Kg1, Vb, Rp, Rk_eff);
        float Vp_q = std::max (Vb - Ip_q * Rp, 0.0f);

        // First pass: compute raw plate voltages to find actual output range
        float rawVp[TRANSFER_TABLE_SIZE];
        float vpMin = Vp_q, vpMax = Vp_q;

        for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i)
        {
            float vNorm = (static_cast<float>(i) / (TRANSFER_TABLE_SIZE - 1)) * 4.0f - 2.0f;
            float Vin = vNorm * 1.5f;  // ±3V grid swing (realistic for 12AX7 preamp)

            float Ip = solveKorenCurrent (Vin, mu, Kp, Kvb, Ex, Kg1, Vb, Rp, Rk_eff);
            float Vp = std::max (Vb - Ip * Rp, 0.0f);  // plate voltage can't go negative
            rawVp[i] = Vp;
            vpMin = std::min (vpMin, Vp);
            vpMax = std::max (vpMax, Vp);
        }

        // Compute small-signal gain for unity-gain normalization.
        // The Koren model produces physical voltages (0-300V range) but we need
        // the LUT to have approximately unity gain near zero so it works with
        // normalized audio. We normalize by the small-signal slope at the
        // operating point rather than the full output range.
        float eps = 0.01f;
        float Ip_eps = solveKorenCurrent (eps, mu, Kp, Kvb, Ex, Kg1, Vb, Rp, Rk_eff);
        float Vp_eps = std::max (Vb - Ip_eps * Rp, 0.0f);
        float slope = std::abs (Vp_eps - Vp_q) / eps;  // dVplate/dVgrid near quiescent
        float inputRange = 1.5f;  // LUT maps vNorm * 1.5 to grid voltage

        // outputScale set so that LUT[center ± small] ≈ ±small (unity gain)
        float outputScale = slope * inputRange;
        outputScale = std::max (outputScale, 1.0f);

        for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i)
            plateTransferTable[i] = std::clamp ((rawVp[i] - Vp_q) / (-outputScale), -1.5f, 1.5f);

        // Force exact zero at the quiescent point (center of LUT) to prevent
        // DC offset from cascaded tube stages when input is silent
        int centerIdx = TRANSFER_TABLE_SIZE / 2;
        plateTransferTable[centerIdx] = 0.0f;
    }

    // Access the plate transfer LUT (for pre-computation / swapping)
    const float* getPlateTransferTable() const { return plateTransferTable.data(); }
    void setPlateTransferTable (const float* data)
    {
        std::copy (data, data + TRANSFER_TABLE_SIZE, plateTransferTable.begin());
    }

    // Process a single sample through the tube stage
    float processSample(float input, int channel)
    {
        channel = std::clamp(channel, 0, 1);

        // Apply input gain (drive)
        float driven = input * inputGain;

        // Add bias offset
        float gridVoltage = driven + biasOffset;

        // Grid current modeling (compression when driven into positive grid)
        if (gridVoltage > gridCurrentThreshold)
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
    std::array<float, TRANSFER_TABLE_SIZE> plateTransferTable;

    TubeType currentType = TubeType::Triode_12AX7;
    double sampleRate = 44100.0;
    int numChannels = 2;

    // Drive settings
    float drive = 0.0f;
    float inputGain = 1.0f;
    float outputScaling = 1.0f;
    float biasOffset = 0.0f;

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

    // Solve Koren tube model for plate current given input voltage
    // Uses fixed-point iteration (8 iterations is sufficient for convergence)
    static float solveKorenCurrent (float Vin, float mu, float Kp, float Kvb,
                                     float Ex, float Kg1,
                                     float Vb, float Rp, float Rk)
    {
        // Defensive clamps for physical validity
        Kvb = std::max (Kvb, 1.0f);
        Kg1 = std::max (Kg1, 1.0f);
        Ex = std::clamp (Ex, 1.0f, 2.0f);

        float Ip = 0.5e-3f;  // initial guess: 0.5 mA

        for (int iter = 0; iter < 8; ++iter)
        {
            float Vp = Vb - Ip * Rp;
            float Vgk = Vin - Ip * Rk;

            if (Vp < 0.1f) Vp = 0.1f;

            float E1inner = Kp * (1.0f / mu + Vgk / std::sqrt (Kvb + Vp * Vp));
            float E1 = (Vp / Kp) * std::log (1.0f + std::exp (std::clamp (E1inner, -20.0f, 20.0f)));
            E1 = std::max (0.0f, E1);

            float IpModel = (std::pow (E1, Ex) / Kg1) * std::atan (Vp / std::sqrt (Kvb));
            IpModel = std::max (0.0f, IpModel);

            // Damped relaxation
            Ip += 0.5f * (IpModel - Ip);
        }

        return std::max (0.0f, Ip);
    }

    void initializePlateTransferFunction()
    {
        for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i)
        {
            float vg = (static_cast<float>(i) / (TRANSFER_TABLE_SIZE - 1)) * 4.0f - 2.0f;

            if (vg >= 0.0f)
            {
                float normalized = vg / (1.0f + vg * 0.4f);
                plateTransferTable[i] = normalized * (1.0f - normalized * 0.12f);
            }
            else
            {
                float absVg = std::abs(vg);

                if (absVg < 0.8f)
                {
                    plateTransferTable[i] = vg;
                }
                else if (absVg < 1.5f)
                {
                    float excess = absVg - 0.8f;
                    float compressed = 0.8f + excess * (1.0f - excess * 0.5f);
                    plateTransferTable[i] = -compressed;
                }
                else
                {
                    float excess = absVg - 1.5f;
                    float clipped = 1.15f + std::tanh(excess * 2.0f) * 0.2f;
                    plateTransferTable[i] = -clipped;
                }
            }
        }
    }

    float applyPlateTransferFunction(float gridVoltage)
    {
        float normalized = (gridVoltage + 2.0f) * 0.25f;
        normalized = std::clamp(normalized, 0.0f, 0.9999f);

        float idx = normalized * (TRANSFER_TABLE_SIZE - 1);
        int i0 = static_cast<int>(idx);
        int i1 = std::min(i0 + 1, TRANSFER_TABLE_SIZE - 1);
        float frac = idx - static_cast<float>(i0);

        return plateTransferTable[i0] * (1.0f - frac) + plateTransferTable[i1] * frac;
    }

    void updateTubeParameters()
    {
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
        float rateRatio = 44100.0f / static_cast<float>(sampleRate);
        cathodeBypassCoeff = std::pow(cathodeBypassCoeffBase, rateRatio);
        millerCapCoeff = std::pow(millerCapCoeffBase, rateRatio);
        gridCurrentDischargeCoeff = std::pow(gridCurrentDischargeBase, rateRatio);
    }
};

} // namespace AnalogEmulation
