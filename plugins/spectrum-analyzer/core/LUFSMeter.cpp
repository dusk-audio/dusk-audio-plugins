#include "LUFSMeter.h"

//==============================================================================
void LUFSMeter::prepare(double sr, int numChannels)
{
    sampleRate = sr;
    channels = numChannels;

    // Initialize K-weighting filters
    initKWeighting(sampleRate);

    // Calculate buffer sizes
    momentarySamples = static_cast<int>(sampleRate * 0.4);    // 400ms
    shortTermSamples = static_cast<int>(sampleRate * 3.0);    // 3s
    blockSamples = static_cast<int>(sampleRate * 0.1);        // 100ms blocks

    // Resize buffers
    momentaryBuffer.resize(momentarySamples, 0.0f);
    shortTermBuffer.resize(shortTermSamples, 0.0f);
    blockBuffer.resize(blockSamples, 0.0f);

    reset();
}

void LUFSMeter::reset()
{
    // Reset filter states
    for (auto& s : highShelfState)
        s = BiquadState{};
    for (auto& s : highPassState)
        s = BiquadState{};

    // Reset buffers
    std::fill(momentaryBuffer.begin(), momentaryBuffer.end(), 0.0f);
    std::fill(shortTermBuffer.begin(), shortTermBuffer.end(), 0.0f);
    std::fill(blockBuffer.begin(), blockBuffer.end(), 0.0f);

    momentaryWritePos = 0;
    shortTermWritePos = 0;
    blockWritePos = 0;

    gatedBlocks.clear();

    momentaryLUFS = -100.0f;
    shortTermLUFS = -100.0f;
    integratedLUFS = -100.0f;
    loudnessRange = 0.0f;
    maxMomentary = -100.0f;
    maxShortTerm = -100.0f;
}

void LUFSMeter::resetIntegrated()
{
    gatedBlocks.clear();
    integratedLUFS = -100.0f;
    loudnessRange = 0.0f;
}

//==============================================================================
void LUFSMeter::initKWeighting(double sr)
{
    // ITU-R BS.1770-4 K-weighting filter coefficients
    // Stage 1: High shelf (+4dB @ 1681Hz, Q=0.707)
    // Stage 2: High-pass (38Hz, 2nd order)

    double fc_shelf = 1681.974450955533;
    double G_shelf = 3.999843853973347;  // +4 dB
    double Q_shelf = 0.7071752369554196;

    double fc_hp = 38.13547087602444;
    double Q_hp = 0.5003270373238773;

    // Pre-warp frequencies
    double K_shelf = std::tan(3.14159265358979323846 * fc_shelf / sr);
    double K_hp = std::tan(3.14159265358979323846 * fc_hp / sr);

    // High shelf coefficients
    double Vh = std::pow(10.0, G_shelf / 20.0);
    double Vb = std::pow(Vh, 0.4996667741545416);

    double a0_shelf = 1.0 + K_shelf / Q_shelf + K_shelf * K_shelf;
    highShelfCoeffs.b0 = static_cast<float>((Vh + Vb * K_shelf / Q_shelf + K_shelf * K_shelf) / a0_shelf);
    highShelfCoeffs.b1 = static_cast<float>(2.0 * (K_shelf * K_shelf - Vh) / a0_shelf);
    highShelfCoeffs.b2 = static_cast<float>((Vh - Vb * K_shelf / Q_shelf + K_shelf * K_shelf) / a0_shelf);
    highShelfCoeffs.a1 = static_cast<float>(2.0 * (K_shelf * K_shelf - 1.0) / a0_shelf);
    highShelfCoeffs.a2 = static_cast<float>((1.0 - K_shelf / Q_shelf + K_shelf * K_shelf) / a0_shelf);

    // High-pass coefficients
    double a0_hp = 1.0 + K_hp / Q_hp + K_hp * K_hp;
    highPassCoeffs.b0 = static_cast<float>(1.0 / a0_hp);
    highPassCoeffs.b1 = static_cast<float>(-2.0 / a0_hp);
    highPassCoeffs.b2 = static_cast<float>(1.0 / a0_hp);
    highPassCoeffs.a1 = static_cast<float>(2.0 * (K_hp * K_hp - 1.0) / a0_hp);
    highPassCoeffs.a2 = static_cast<float>((1.0 - K_hp / Q_hp + K_hp * K_hp) / a0_hp);
}

float LUFSMeter::applyKWeighting(float sample, int channel)
{
    // Stage 1: High shelf
    auto& hsState = highShelfState[channel];
    float hsOut = highShelfCoeffs.b0 * sample
                + highShelfCoeffs.b1 * hsState.x1
                + highShelfCoeffs.b2 * hsState.x2
                - highShelfCoeffs.a1 * hsState.y1
                - highShelfCoeffs.a2 * hsState.y2;

    hsState.x2 = hsState.x1;
    hsState.x1 = sample;
    hsState.y2 = hsState.y1;
    hsState.y1 = hsOut;

    // Stage 2: High-pass
    auto& hpState = highPassState[channel];
    float hpOut = highPassCoeffs.b0 * hsOut
                + highPassCoeffs.b1 * hpState.x1
                + highPassCoeffs.b2 * hpState.x2
                - highPassCoeffs.a1 * hpState.y1
                - highPassCoeffs.a2 * hpState.y2;

    hpState.x2 = hpState.x1;
    hpState.x1 = hsOut;
    hpState.y2 = hpState.y1;
    hpState.y1 = hpOut;

    return hpOut;
}

//==============================================================================
void LUFSMeter::process(const float* left, const float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Apply K-weighting
        float kL = applyKWeighting(left[i], 0);
        float kR = applyKWeighting(right[i], 1);

        // Mean square of stereo signal
        float meanSquare = (kL * kL + kR * kR) * 0.5f;

        // Store in momentary buffer (circular)
        momentaryBuffer[momentaryWritePos] = meanSquare;
        momentaryWritePos = (momentaryWritePos + 1) % momentarySamples;

        // Store in short-term buffer (circular)
        shortTermBuffer[shortTermWritePos] = meanSquare;
        shortTermWritePos = (shortTermWritePos + 1) % shortTermSamples;

        // Store in block buffer for integrated measurement
        blockBuffer[blockWritePos] = meanSquare;
        blockWritePos++;

        // When a 100ms block is complete, calculate and store its loudness
        if (blockWritePos >= blockSamples)
        {
            float blockMeanSquare = calculateMeanSquare(blockBuffer);
            float blockLUFS = meanSquareToLUFS(blockMeanSquare);

            // Only store blocks above absolute gate for integrated loudness
            if (blockLUFS > ABSOLUTE_GATE)
            {
                gatedBlocks.push_back(blockLUFS);

                // Limit stored blocks (keep ~10 minutes of data)
                while (gatedBlocks.size() > 6000)
                    gatedBlocks.pop_front();
            }

            blockWritePos = 0;
        }
    }

    // Calculate momentary loudness (400ms)
    float momentaryMS = calculateMeanSquare(momentaryBuffer);
    momentaryLUFS = meanSquareToLUFS(momentaryMS);
    maxMomentary = std::max(maxMomentary, momentaryLUFS);

    // Calculate short-term loudness (3s)
    float shortTermMS = calculateMeanSquare(shortTermBuffer);
    shortTermLUFS = meanSquareToLUFS(shortTermMS);
    maxShortTerm = std::max(maxShortTerm, shortTermLUFS);

    // Update integrated loudness and LRA
    updateIntegratedLoudness();
    updateLoudnessRange();
}

//==============================================================================
float LUFSMeter::calculateMeanSquare(const std::vector<float>& buffer) const
{
    if (buffer.empty()) return 0.0f;

    float sum = 0.0f;
    for (float v : buffer)
        sum += v;

    return sum / static_cast<float>(buffer.size());
}

float LUFSMeter::meanSquareToLUFS(float meanSquare)
{
    // LUFS = -0.691 + 10 * log10(mean_square)
    if (meanSquare < 1e-10f) return -100.0f;
    return -0.691f + 10.0f * std::log10(meanSquare);
}

void LUFSMeter::updateIntegratedLoudness()
{
    if (gatedBlocks.empty())
    {
        integratedLUFS = -100.0f;
        return;
    }

    // Pass 1: Calculate ungated average (blocks already filtered by absolute gate)
    float sum = 0.0f;
    for (float block : gatedBlocks)
        sum += block;
    float ungatedAvg = sum / static_cast<float>(gatedBlocks.size());

    // Pass 2: Apply relative gate (-10 LU below ungated average)
    float relativeThreshold = ungatedAvg + RELATIVE_GATE;

    float gatedSum = 0.0f;
    int gatedCount = 0;
    for (float block : gatedBlocks)
    {
        if (block > relativeThreshold)
        {
            // Convert back to linear for proper averaging
            float linear = std::pow(10.0f, (block + 0.691f) / 10.0f);
            gatedSum += linear;
            gatedCount++;
        }
    }

    if (gatedCount > 0)
    {
        float avgLinear = gatedSum / static_cast<float>(gatedCount);
        integratedLUFS = meanSquareToLUFS(avgLinear);
    }
    else
    {
        integratedLUFS = -100.0f;
    }
}

void LUFSMeter::updateLoudnessRange()
{
    // LRA is based on short-term loudness distribution
    // Use the gated blocks as an approximation

    if (gatedBlocks.size() < 10)
    {
        loudnessRange = 0.0f;
        return;
    }

    // Apply relative gate for LRA calculation
    float sum = 0.0f;
    for (float block : gatedBlocks)
        sum += block;
    float mean = sum / static_cast<float>(gatedBlocks.size());
    float relativeThreshold = mean - 20.0f;  // LRA uses -20 LU gate

    std::vector<float> lraBlocks;
    for (float block : gatedBlocks)
    {
        if (block > relativeThreshold)
            lraBlocks.push_back(block);
    }

    if (lraBlocks.size() < 2)
    {
        loudnessRange = 0.0f;
        return;
    }

    // Sort and find 10th and 95th percentiles
    std::sort(lraBlocks.begin(), lraBlocks.end());

    size_t idx10 = static_cast<size_t>(lraBlocks.size() * 0.10);
    size_t idx95 = static_cast<size_t>(lraBlocks.size() * 0.95);

    idx95 = std::min(idx95, lraBlocks.size() - 1);

    loudnessRange = lraBlocks[idx95] - lraBlocks[idx10];
}
