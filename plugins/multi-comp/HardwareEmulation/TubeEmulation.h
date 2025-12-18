/*
  ==============================================================================

    TubeEmulation.h
    Vacuum tube (valve) emulation for audio processing

    Models the non-linear behavior of common audio tubes:
    - 12AX7 (high gain triode, used in preamps)
    - 12AT7 (medium gain triode)
    - 12BH7 (output driver, used in LA-2A)

    Key characteristics modeled:
    - Asymmetric transfer curve (grid current vs cutoff)
    - Miller capacitance (HF rolloff under gain)
    - Cathode bypass (frequency response shaping)
    - Grid current compression (soft limiting on positive excursions)

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace HardwareEmulation {

class TubeEmulation
{
public:
    enum class TubeType
    {
        Triode_12AX7,    // High gain (~100), more 2nd harmonic
        Triode_12AT7,    // Medium gain (~60)
        Triode_12BH7,    // Output driver (~16), LA-2A output stage
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

        // Update filter coefficients for new sample rate
        updateCoefficients();

        reset();
    }

    void reset()
    {
        // Always reset all available channels to avoid stale state
        // (processSample can access any channel 0-1 regardless of numChannels)
        for (int ch = 0; ch < 2; ++ch)
        {
            millerCapState[ch] = 0.0f;
            gridCurrent[ch] = 0.0f;
            cathodeBypassState[ch] = 0.0f;
            dcBlockerX1[ch] = 0.0f;
            dcBlockerY1[ch] = 0.0f;
        }
    }

    void setTubeType(TubeType type)
    {
        currentType = type;
        updateTubeParameters();
    }

    void setDrive(float driveAmount)
    {
        // 0 = unity, 1 = pushed hard
        drive = std::clamp(driveAmount, 0.0f, 1.0f);
        inputGain = 1.0f + drive * 2.0f;  // Up to 3x input gain
    }

    void setBiasPoint(float bias)
    {
        // Adjust operating point (-1 to +1)
        biasOffset = std::clamp(bias, -1.0f, 1.0f) * 0.2f;
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
            gridVoltage -= gridCurrent[channel];  // Grid loading reduces signal
        }
        else
        {
            // Slow discharge of grid current (sample-rate adjusted)
            gridCurrent[channel] *= gridCurrentDischargeCoeff;
        }

        // Apply plate transfer function (the main tube nonlinearity)
        float plateVoltage = applyPlateTransferFunction(gridVoltage);

        // Cathode bypass capacitor (affects frequency response)
        // Low frequencies bypass the cathode resistor, getting more gain
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
        return processDCBlocker(output, channel);
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

    // Per-channel state
    float millerCapState[2] = {0.0f, 0.0f};
    float gridCurrent[2] = {0.0f, 0.0f};
    float cathodeBypassState[2] = {0.0f, 0.0f};
    float dcBlockerX1[2] = {0.0f, 0.0f};
    float dcBlockerY1[2] = {0.0f, 0.0f};
    float dcBlockerCoeff = 0.999f;

    void initializePlateTransferFunction()
    {
        // Model 12AX7 triode plate characteristics
        // Based on Ia/Vg curves from tube datasheets
        // Asymmetric: positive grid clips softer (grid current), negative clips harder (cutoff)

        for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i)
        {
            float vg = (static_cast<float>(i) / (TRANSFER_TABLE_SIZE - 1)) * 4.0f - 2.0f;  // -2 to +2

            if (vg >= 0.0f)
            {
                // Positive grid region - grid current causes soft compression
                // More 2nd harmonic due to asymmetry
                float normalized = vg / (1.0f + vg * 0.4f);
                // Soft clip with 2nd harmonic character
                plateTransferTable[i] = normalized * (1.0f - normalized * 0.12f);
            }
            else
            {
                // Negative grid region - normal amplification transitioning to cutoff
                float absVg = std::abs(vg);

                if (absVg < 0.8f)
                {
                    // Linear region
                    plateTransferTable[i] = vg;
                }
                else if (absVg < 1.5f)
                {
                    // Approaching cutoff - gradual compression
                    float excess = absVg - 0.8f;
                    float compressed = 0.8f + excess * (1.0f - excess * 0.5f);
                    plateTransferTable[i] = -compressed;
                }
                else
                {
                    // Cutoff region - hard clipping
                    float excess = absVg - 1.5f;
                    float clipped = 1.15f + std::tanh(excess * 2.0f) * 0.2f;
                    plateTransferTable[i] = -clipped;
                }
            }
        }
    }

    float applyPlateTransferFunction(float gridVoltage)
    {
        // Map grid voltage to table index
        float normalized = (gridVoltage + 2.0f) * 0.25f;  // Map -2..+2 to 0..1
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
                // High gain, lots of harmonic content
                gridCurrentThreshold = 0.4f;
                gridCurrentCoeff = 0.25f;
                cathodeBypassCoeffBase = 0.98f;
                cathodeBypassAmount = 0.35f;
                millerCapCoeffBase = 0.35f;
                millerCapEffect = 0.12f;
                outputScaling = 0.8f;
                break;

            case TubeType::Triode_12AT7:
                // Medium gain, balanced character
                gridCurrentThreshold = 0.5f;
                gridCurrentCoeff = 0.2f;
                cathodeBypassCoeffBase = 0.97f;
                cathodeBypassAmount = 0.3f;
                millerCapCoeffBase = 0.25f;
                millerCapEffect = 0.08f;
                outputScaling = 0.85f;
                break;

            case TubeType::Triode_12BH7:
                // Output driver (LA-2A), clean with headroom
                gridCurrentThreshold = 0.6f;
                gridCurrentCoeff = 0.15f;
                cathodeBypassCoeffBase = 0.96f;
                cathodeBypassAmount = 0.25f;
                millerCapCoeffBase = 0.2f;
                millerCapEffect = 0.05f;
                outputScaling = 0.9f;
                break;

            case TubeType::Triode_6SN7:
                // Warm, musical character
                gridCurrentThreshold = 0.45f;
                gridCurrentCoeff = 0.22f;
                cathodeBypassCoeffBase = 0.975f;
                cathodeBypassAmount = 0.32f;
                millerCapCoeffBase = 0.28f;
                millerCapEffect = 0.1f;
                outputScaling = 0.85f;
                break;

            default:
                // Default to 12AX7 characteristics
                gridCurrentThreshold = 0.4f;
                gridCurrentCoeff = 0.25f;
                cathodeBypassCoeffBase = 0.98f;
                cathodeBypassAmount = 0.35f;
                millerCapCoeffBase = 0.35f;
                millerCapEffect = 0.12f;
                outputScaling = 0.8f;
                break;
        }

        updateCoefficients();
    }

    // Base coefficients (set by tube type, before sample-rate adjustment)
    float cathodeBypassCoeffBase = 0.98f;
    float millerCapCoeffBase = 0.3f;
    float gridCurrentDischargeBase = 0.95f;  // Base discharge rate at 44.1kHz
    float gridCurrentDischargeCoeff = 0.95f; // Sample-rate adjusted

    void updateCoefficients()
    {
        // DC blocker (10Hz highpass)
        float dcCutoff = 10.0f;
        dcBlockerCoeff = 1.0f - (6.283185f * dcCutoff / static_cast<float>(sampleRate));

        // Adjust time constants for sample rate
        float rateRatio = 44100.0f / static_cast<float>(sampleRate);
        cathodeBypassCoeff = std::pow(cathodeBypassCoeffBase, rateRatio);
        millerCapCoeff = std::pow(millerCapCoeffBase, rateRatio);
        gridCurrentDischargeCoeff = std::pow(gridCurrentDischargeBase, rateRatio);
    }
    float processDCBlocker(float input, int channel)
    {
        float y = input - dcBlockerX1[channel] + dcBlockerCoeff * dcBlockerY1[channel];
        dcBlockerX1[channel] = input;
        dcBlockerY1[channel] = y;
        return y;
    }
};

} // namespace HardwareEmulation
