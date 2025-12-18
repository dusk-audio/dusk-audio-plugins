/*
  ==============================================================================

    ConvolutionEngine.h
    Short-IR convolution for transformer/cabinet coloration

    Uses direct convolution (not FFT) for short impulse responses,
    optimized for low latency with IRs of 32-256 samples.

    Includes synthetic IR generation for transformer characteristics.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace HardwareEmulation {

class ShortConvolution
{
public:
    static constexpr int MAX_IR_LENGTH = 256;

    ShortConvolution() = default;

    void prepare(double sampleRate, int /* maxBlockSize */ = 512)
    {
        this->sampleRate = sampleRate;
        reset();
    }

    void reset()
    {
        inputBuffer.fill(0.0f);
        writePos = 0;
    }

    // Load IR from raw float data
    void loadIR(const float* irData, int length)
    {
        if (irData == nullptr || length <= 0)
            return;

        irLength = std::min(length, MAX_IR_LENGTH);

        for (int i = 0; i < irLength; ++i)
            impulseResponse[i] = irData[i];
        // Normalize IR to prevent level changes
        normalizeIR();

        // Pre-compute reversed IR for convolution
        for (int i = 0; i < irLength; ++i)
            reversedIR[i] = impulseResponse[irLength - 1 - i];
    }

    // Load a synthetic transformer IR based on characteristics
    enum class TransformerType
    {
        LA2A,           // Warm, 16kHz rolloff, subtle 80Hz resonance
        FET_1176,       // Clean, 22kHz rolloff, 100Hz presence
        SSL_Console,    // Punchy, subtle mid presence
        Generic,        // Neutral transformer
        Bypass          // Unity (no coloration)
    };

    void loadTransformerIR(TransformerType type)
    {
        switch (type)
        {
            case TransformerType::LA2A:
                generateTransformerIR(80.0f, 0.5f, 16000.0f, -1.5f, 64);
                break;

            case TransformerType::FET_1176:
                generateTransformerIR(100.0f, 0.3f, 22000.0f, -0.8f, 48);
                break;

            case TransformerType::SSL_Console:
                generateTransformerIR(2500.0f, 0.4f, 20000.0f, -0.5f, 32);
                break;

            case TransformerType::Generic:
                generateTransformerIR(60.0f, 0.3f, 18000.0f, -1.0f, 48);
                break;

            case TransformerType::Bypass:
            default:
                // Unity IR
                irLength = 1;
                impulseResponse[0] = 1.0f;
                reversedIR[0] = 1.0f;
                break;
        }
    }

    // Process a single sample
    float processSample(float input)
    {
        if (irLength <= 1)
            return input * impulseResponse[0];

        // Write input to circular buffer
        inputBuffer[writePos] = input;

        // Direct convolution (optimized for short IRs)
        float output = 0.0f;

        // Unrolled by 4 for better performance
        int i = 0;
        int readPos = writePos;

        // Process 4 samples at a time
        for (; i + 4 <= irLength; i += 4)
        {
            int idx0 = (readPos - i + MAX_IR_LENGTH) % MAX_IR_LENGTH;
            int idx1 = (readPos - i - 1 + MAX_IR_LENGTH) % MAX_IR_LENGTH;
            int idx2 = (readPos - i - 2 + MAX_IR_LENGTH) % MAX_IR_LENGTH;
            int idx3 = (readPos - i - 3 + MAX_IR_LENGTH) % MAX_IR_LENGTH;

            output += inputBuffer[idx0] * reversedIR[i];
            output += inputBuffer[idx1] * reversedIR[i + 1];
            output += inputBuffer[idx2] * reversedIR[i + 2];
            output += inputBuffer[idx3] * reversedIR[i + 3];
        }

        // Handle remaining samples
        for (; i < irLength; ++i)
        {
            int idx = (readPos - i + MAX_IR_LENGTH) % MAX_IR_LENGTH;
            output += inputBuffer[idx] * reversedIR[i];
        }

        // Advance write position
        writePos = (writePos + 1) % MAX_IR_LENGTH;

        return output;
    }

    // Block processing
    void processBlock(float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            data[i] = processSample(data[i]);
        }
    }

    int getLatency() const
    {
        return irLength / 2;  // Group delay approximation
    }

    bool isEnabled() const
    {
        return irLength > 1;
    }

private:
    std::array<float, MAX_IR_LENGTH> impulseResponse{1.0f};  // Unity impulse at [0]
    std::array<float, MAX_IR_LENGTH> reversedIR{1.0f};       // Unity impulse at [0]
    std::array<float, MAX_IR_LENGTH> inputBuffer{};

    int irLength = 1;
    int writePos = 0;
    double sampleRate = 44100.0;
    void normalizeIR()
    {
        // Calculate sum of absolute values
        float sum = 0.0f;
        for (int i = 0; i < irLength; ++i)
            sum += std::abs(impulseResponse[i]);

        // Normalize to unity gain
        if (sum > 0.001f)
        {
            float scale = 1.0f / sum;
            for (int i = 0; i < irLength; ++i)
                impulseResponse[i] *= scale;
        }
    }

    // Generate a synthetic transformer IR from frequency response parameters
    void generateTransformerIR(float resonanceFreq, float resonanceAmount,
                               float rolloffFreq, float rolloffDb,
                               int length)
    {
        irLength = std::min(length, MAX_IR_LENGTH);

        // Start with impulse
        impulseResponse.fill(0.0f);
        impulseResponse[0] = 1.0f;

        // Apply resonance (subtle peak)
        if (resonanceAmount > 0.0f && resonanceFreq > 0.0f)
        {
            applyResonance(resonanceFreq, resonanceAmount);
        }

        // Apply HF rolloff
        if (rolloffFreq > 0.0f && rolloffFreq < sampleRate / 2.0f)
        {
            applyLowpass(rolloffFreq, rolloffDb);
        }

        // Normalize
        normalizeIR();

        // Copy to reversed array
        for (int i = 0; i < irLength; ++i)
            reversedIR[i] = impulseResponse[irLength - 1 - i];
    }

    void applyResonance(float freq, float amount)
    {
        // Simple resonant filter applied to IR
        float w0 = 2.0f * 3.14159f * freq / static_cast<float>(sampleRate);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        
        // Skip resonance for extreme frequencies where sinw0 approaches zero
        if (std::abs(sinw0) < 1e-6f)
            return;

        // Bandwidth for resonance
        float bw = 1.0f;  // 1 octave
        float alpha = sinw0 * std::sinh((std::log(2.0f) / 2.0f) * bw * w0 / sinw0);

        // Peaking EQ coefficients
        float A = std::pow(10.0f, amount * 3.0f / 40.0f);  // amount dB boost

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cosw0;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha / A;

        // Normalize
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 /= a0; a2 /= a0;

        // Apply filter to IR
        std::array<float, MAX_IR_LENGTH> temp{};
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        for (int i = 0; i < irLength; ++i)
        {
            float x = impulseResponse[i];
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

            temp[i] = y;

            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
        }

        std::copy(temp.begin(), temp.begin() + irLength, impulseResponse.begin());
    }
    void applyLowpass(float freq, float db)
    {
        // One-pole lowpass applied iteratively for smooth rolloff
        // Each pass adds ~0.75dB additional rolloff at cutoff frequency
        float w = 2.0f * 3.14159f * freq / static_cast<float>(sampleRate);
        float coeff = w / (w + 1.0f);

        // Number of filter passes based on rolloff steepness
        // -0.5dB -> 1 pass, -1.0dB -> 1 pass, -1.5dB -> 2 passes
        int numPasses = std::max(1, static_cast<int>(std::abs(db) / 0.75f + 0.5f));

        for (int pass = 0; pass < numPasses; ++pass)
        {
            float state = 0.0f;
            for (int i = 0; i < irLength; ++i)
            {
                state += coeff * (impulseResponse[i] - state);
                impulseResponse[i] = state;
            }
        }
    }
};

//==============================================================================
// Stereo convolution wrapper
class StereoConvolution
{
public:
    void prepare(double sampleRate, int maxBlockSize = 512)
    {
        left.prepare(sampleRate, maxBlockSize);
        right.prepare(sampleRate, maxBlockSize);
    }

    void reset()
    {
        left.reset();
        right.reset();
    }

    void loadTransformerIR(ShortConvolution::TransformerType type)
    {
        left.loadTransformerIR(type);
        right.loadTransformerIR(type);
    }

    void processStereo(float& leftSample, float& rightSample)
    {
        leftSample = left.processSample(leftSample);
        rightSample = right.processSample(rightSample);
    }

    void processBlock(float* const* channelData, int numChannels, int numSamples)
    {
        if (numChannels >= 1)
            left.processBlock(channelData[0], numSamples);
        if (numChannels >= 2)
            right.processBlock(channelData[1], numSamples);
    }

    int getLatency() const { return left.getLatency(); }
    bool isEnabled() const { return left.isEnabled(); }

private:
    ShortConvolution left;
    ShortConvolution right;
};

} // namespace HardwareEmulation
