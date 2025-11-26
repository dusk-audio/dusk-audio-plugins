#include "VariationEngine.h"
#include <cmath>
#include <algorithm>
#include <numeric>

VariationEngine::VariationEngine()
{
    prepare(0);
}

void VariationEngine::prepare(uint32_t seed)
{
    random.setSeed(seed == 0 ? juce::Time::currentTimeMillis() : seed);

    // Initialize LFSR with seed
    lfsrState = static_cast<uint16_t>((seed == 0) ? 0xACE1 : (seed & 0xFFFF));
    if (lfsrState == 0) lfsrState = 0xACE1;  // LFSR can't be zero

    // Initialize Perlin noise
    initPerlin(seed);

    // Clear pattern history
    patternHistory.fill(0);
    historyIndex = 0;
}

void VariationEngine::reset()
{
    prepare(0);
}

void VariationEngine::initPerlin(uint32_t seed)
{
    // Initialize permutation table
    for (int i = 0; i < 256; ++i)
    {
        perlinPermutation[i] = static_cast<uint8_t>(i);
    }

    // Shuffle permutation table
    juce::Random perlinRandom(seed);
    for (int i = 255; i > 0; --i)
    {
        int j = perlinRandom.nextInt(i + 1);
        std::swap(perlinPermutation[i], perlinPermutation[j]);
    }

    // Initialize gradients (1D Perlin, so just -1 or 1)
    for (int i = 0; i < 256; ++i)
    {
        perlinGradients[i] = (perlinRandom.nextFloat() > 0.5f) ? 1.0f : -1.0f;
    }
}

float VariationEngine::fade(float t)
{
    // Smoothstep fade function: 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float VariationEngine::lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

float VariationEngine::perlinNoise(float x)
{
    // 1D Perlin noise
    int xi = static_cast<int>(std::floor(x)) & 255;
    float xf = x - std::floor(x);

    float u = fade(xf);

    int a = perlinPermutation[xi];
    int b = perlinPermutation[(xi + 1) & 255];

    float gradA = perlinGradients[a] * xf;
    float gradB = perlinGradients[b] * (xf - 1.0f);

    return lerp(gradA, gradB, u);
}

float VariationEngine::getEnergyVariation(double barPosition)
{
    // Multi-octave Perlin noise for natural energy drift
    float noise = 0.0f;
    float amplitude = 1.0f;
    float frequency = 0.1f;
    float maxValue = 0.0f;

    // 3 octaves of noise
    for (int i = 0; i < 3; ++i)
    {
        noise += amplitude * perlinNoise(static_cast<float>(barPosition) * frequency);
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    // Normalize to -1 to 1
    noise /= maxValue;

    // Map to energy multiplier range (0.85 to 1.15)
    return 1.0f + (noise * 0.15f);
}

uint16_t VariationEngine::lfsrStep()
{
    // 16-bit LFSR with taps at bits 16, 14, 13, 11 (maximal length)
    uint16_t bit = ((lfsrState >> 0) ^ (lfsrState >> 2) ^ (lfsrState >> 3) ^ (lfsrState >> 5)) & 1;
    lfsrState = (lfsrState >> 1) | (bit << 15);
    return lfsrState;
}

float VariationEngine::nextRandom()
{
    uint16_t value = lfsrStep();
    return static_cast<float>(value) / 65535.0f;
}

bool VariationEngine::wasRecentlyUsed(uint32_t patternHash)
{
    for (int i = 0; i < HISTORY_SIZE; ++i)
    {
        if (patternHistory[i] == patternHash)
            return true;
    }
    return false;
}

void VariationEngine::registerPattern(uint32_t patternHash)
{
    patternHistory[historyIndex] = patternHash;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

uint32_t VariationEngine::hashPattern(const int* notes, const int* velocities, int count)
{
    // Simple CRC32-like hash
    uint32_t hash = 0xFFFFFFFF;

    for (int i = 0; i < count; ++i)
    {
        hash ^= static_cast<uint32_t>(notes[i]);
        for (int j = 0; j < 8; ++j)
        {
            hash = (hash >> 1) ^ (0xEDB88320 & -(hash & 1));
        }

        hash ^= static_cast<uint32_t>(velocities[i]);
        for (int j = 0; j < 8; ++j)
        {
            hash = (hash >> 1) ^ (0xEDB88320 & -(hash & 1));
        }
    }

    return ~hash;
}

float VariationEngine::getVariationProbability(int barPosition)
{
    // Higher variation probability at phrase boundaries
    float prob = 0.1f;  // Base probability

    // Every 2 bars: slight increase
    if (barPosition % 2 == 1)
        prob += 0.1f;

    // Every 4 bars: moderate increase
    if (barPosition % 4 == 3)
        prob += 0.15f;

    // Every 8 bars: significant increase
    if (barPosition % 8 == 7)
        prob += 0.2f;

    // Add some randomness
    prob += (nextRandom() - 0.5f) * 0.1f;

    return juce::jlimit(0.0f, 0.8f, prob);
}

float VariationEngine::getFillProbability(int barsSinceLastFill, float fillHunger)
{
    // Base probability increases with time since last fill
    float timeFactor = std::min(1.0f, static_cast<float>(barsSinceLastFill) / 8.0f);

    // Drummer's fill hunger affects probability
    float hunger = fillHunger * 0.5f;  // Scale to max 0.5 contribution

    // Random factor
    float randomFactor = nextRandom() * 0.2f;

    // Combine factors
    float prob = (timeFactor * 0.4f) + hunger + randomFactor;

    // Phrase boundaries increase fill probability significantly
    if (barsSinceLastFill >= 4)
        prob += 0.15f;
    if (barsSinceLastFill >= 8)
        prob += 0.25f;

    return juce::jlimit(0.0f, 0.9f, prob);
}