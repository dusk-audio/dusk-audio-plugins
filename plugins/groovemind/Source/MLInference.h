/*
  ==============================================================================

    MLInference.h
    Lightweight ML inference for GrooveMind

    Provides real-time safe neural network inference for:
    - Humanization model (timing offset prediction)
    - Style classifier (pattern selection)

    Models are loaded from JSON format exported from PyTorch.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <memory>

namespace MLInference
{

//==============================================================================
/**
 * Activation functions
 */
enum class Activation
{
    None,
    ReLU,
    Tanh,
    Sigmoid
};

inline float applyActivation(float x, Activation act)
{
    switch (act)
    {
        case Activation::ReLU:
            return x > 0.0f ? x : 0.0f;
        case Activation::Tanh:
            return std::tanh(x);
        case Activation::Sigmoid:
            return 1.0f / (1.0f + std::exp(-x));
        case Activation::None:
        default:
            return x;
    }
}

//==============================================================================
/**
 * Dense (fully connected) layer
 */
class DenseLayer
{
public:
    DenseLayer() = default;

    void initialize(int inputSize, int outputSize, Activation activation)
    {
        this->inputSize = inputSize;
        this->outputSize = outputSize;
        this->activation = activation;

        weights.resize(inputSize * outputSize);
        bias.resize(outputSize);
        output.resize(outputSize);
    }

    bool loadWeights(const juce::var& layerData)
    {
        if (!layerData.hasProperty("weights") || !layerData.hasProperty("bias"))
            return false;

        auto* weightsArray = layerData["weights"].getArray();
        auto* biasArray = layerData["bias"].getArray();

        if (weightsArray == nullptr || biasArray == nullptr)
            return false;

        if (weightsArray->size() != weights.size() || biasArray->size() != bias.size())
            return false;

        for (int i = 0; i < weights.size(); ++i)
            weights[i] = static_cast<float>((*weightsArray)[i]);

        for (int i = 0; i < bias.size(); ++i)
            bias[i] = static_cast<float>((*biasArray)[i]);

        return true;
    }

    const std::vector<float>& forward(const std::vector<float>& input)
    {
        jassert(input.size() == inputSize);

        for (int o = 0; o < outputSize; ++o)
        {
            float sum = bias[o];
            for (int i = 0; i < inputSize; ++i)
            {
                sum += input[i] * weights[i * outputSize + o];
            }
            output[o] = applyActivation(sum, activation);
        }

        return output;
    }

    // For fixed-size inference (faster, no bounds checking)
    template<int InputSize, int OutputSize>
    void forwardFixed(const std::array<float, InputSize>& input,
                      std::array<float, OutputSize>& out)
    {
        static_assert(InputSize > 0 && OutputSize > 0);

        for (int o = 0; o < OutputSize; ++o)
        {
            float sum = bias[o];
            for (int i = 0; i < InputSize; ++i)
            {
                sum += input[i] * weights[i * OutputSize + o];
            }
            out[o] = applyActivation(sum, activation);
        }
    }

    int getInputSize() const { return inputSize; }
    int getOutputSize() const { return outputSize; }

private:
    int inputSize = 0;
    int outputSize = 0;
    Activation activation = Activation::None;
    std::vector<float> weights;
    std::vector<float> bias;
    std::vector<float> output;
};

//==============================================================================
/**
 * Humanization Model
 *
 * Predicts timing offsets for drum notes based on:
 * - Instrument category (one-hot, 6 dims)
 * - Beat position (1 dim)
 * - Velocity (1 dim)
 * - Previous/next instrument context (6+6 dims)
 *
 * Total input: 20 dimensions
 * Output: 1 dimension (timing offset, -1 to 1, scaled to ms)
 */
class HumanizerModel
{
public:
    static constexpr int INPUT_SIZE = 20;
    static constexpr int HIDDEN_SIZE = 32;
    static constexpr int OUTPUT_SIZE = 1;
    static constexpr float TIMING_SCALE_MS = 50.0f;  // Output * scale = ms offset

    HumanizerModel() = default;

    bool loadFromJSON(const juce::File& jsonFile)
    {
        auto jsonText = jsonFile.loadFileAsString();
        auto parsed = juce::JSON::parse(jsonText);

        if (parsed.isVoid())
            return false;

        auto* layers = parsed["layers"].getArray();
        if (layers == nullptr || layers->size() < 5)
            return false;

        // Layer 0: Dense 20 -> 32
        layer1.initialize(INPUT_SIZE, HIDDEN_SIZE, Activation::None);
        if (!layer1.loadWeights((*layers)[0]))
            return false;

        // Layer 1: ReLU (handled by activation in layer1 forward, but we load separately)
        // Layer 2: Dense 32 -> 32
        layer2.initialize(HIDDEN_SIZE, HIDDEN_SIZE, Activation::None);
        if (!layer2.loadWeights((*layers)[2]))
            return false;

        // Layer 3: ReLU
        // Layer 4: Dense 32 -> 1
        outputLayer.initialize(HIDDEN_SIZE, OUTPUT_SIZE, Activation::None);
        if (!outputLayer.loadWeights((*layers)[4]))
            return false;

        isLoaded = true;
        return true;
    }

    bool loadFromBinaryResource(const char* data, int size)
    {
        juce::String jsonText(data, size);
        auto parsed = juce::JSON::parse(jsonText);

        if (parsed.isVoid())
            return false;

        return loadFromVar(parsed);
    }

    bool loadFromVar(const juce::var& parsed)
    {
        auto* layers = parsed["layers"].getArray();
        if (layers == nullptr || layers->size() < 5)
            return false;

        layer1.initialize(INPUT_SIZE, HIDDEN_SIZE, Activation::None);
        if (!layer1.loadWeights((*layers)[0]))
            return false;

        layer2.initialize(HIDDEN_SIZE, HIDDEN_SIZE, Activation::None);
        if (!layer2.loadWeights((*layers)[2]))
            return false;

        outputLayer.initialize(HIDDEN_SIZE, OUTPUT_SIZE, Activation::None);
        if (!outputLayer.loadWeights((*layers)[4]))
            return false;

        isLoaded = true;
        return true;
    }

    /**
     * Predict timing offset in milliseconds
     *
     * @param instrumentCategory 0-5 (kick, snare, hihat, tom, cymbal, other)
     * @param beatPosition 0-1 position within bar
     * @param velocity 0-1 normalized velocity
     * @param prevCategory -1 to 5 (previous instrument, -1 if none)
     * @param nextCategory -1 to 5 (next instrument, -1 if none)
     * @return timing offset in milliseconds
     */
    float predict(int instrumentCategory, float beatPosition, float velocity,
                  int prevCategory, int nextCategory)
    {
        if (!isLoaded)
            return 0.0f;

        // Build input vector
        std::vector<float> input(INPUT_SIZE, 0.0f);

        // Instrument one-hot (0-5)
        if (instrumentCategory >= 0 && instrumentCategory < 6)
            input[instrumentCategory] = 1.0f;

        // Beat position and velocity
        input[6] = beatPosition;
        input[7] = velocity;

        // Previous instrument one-hot (8-13)
        if (prevCategory >= 0 && prevCategory < 6)
            input[8 + prevCategory] = 1.0f;

        // Next instrument one-hot (14-19)
        if (nextCategory >= 0 && nextCategory < 6)
            input[14 + nextCategory] = 1.0f;

        // Forward pass with ReLU activations
        const auto& h1 = layer1.forward(input);
        std::vector<float> h1_relu(h1.size());
        for (size_t i = 0; i < h1.size(); ++i)
            h1_relu[i] = applyActivation(h1[i], Activation::ReLU);

        const auto& h2 = layer2.forward(h1_relu);
        std::vector<float> h2_relu(h2.size());
        for (size_t i = 0; i < h2.size(); ++i)
            h2_relu[i] = applyActivation(h2[i], Activation::ReLU);

        const auto& out = outputLayer.forward(h2_relu);

        // Apply tanh and scale
        float rawOutput = applyActivation(out[0], Activation::Tanh);
        return rawOutput * TIMING_SCALE_MS;
    }

    bool loaded() const { return isLoaded; }

private:
    DenseLayer layer1;
    DenseLayer layer2;
    DenseLayer outputLayer;
    bool isLoaded = false;
};

//==============================================================================
/**
 * Style Classifier Model
 *
 * Selects appropriate patterns based on:
 * - Style (one-hot, 12 dims)
 * - Section (one-hot, 7 dims)
 * - Energy (1 dim)
 * - Complexity (1 dim)
 *
 * Total input: 21 dimensions
 * Output: Pattern scores (num_patterns dimensions)
 */
class StyleClassifierModel
{
public:
    static constexpr int NUM_STYLES = 12;
    static constexpr int NUM_SECTIONS = 7;
    static constexpr int INPUT_SIZE = NUM_STYLES + NUM_SECTIONS + 2;  // 21
    static constexpr int HIDDEN_SIZE = 64;

    StyleClassifierModel() = default;

    bool loadFromJSON(const juce::File& jsonFile)
    {
        auto jsonText = jsonFile.loadFileAsString();
        auto parsed = juce::JSON::parse(jsonText);

        if (parsed.isVoid())
            return false;

        return loadFromVar(parsed);
    }

    bool loadFromVar(const juce::var& parsed)
    {
        numPatterns = static_cast<int>(parsed["output_size"]);
        if (numPatterns <= 0)
            return false;

        auto* layers = parsed["layers"].getArray();
        if (layers == nullptr || layers->size() < 5)
            return false;

        // Load pattern IDs
        auto* patternIds = parsed["pattern_ids"].getArray();
        if (patternIds != nullptr)
        {
            patternIdList.clear();
            for (const auto& id : *patternIds)
                patternIdList.push_back(id.toString());
        }

        // Layer 0: Dense 21 -> 64
        layer1.initialize(INPUT_SIZE, HIDDEN_SIZE, Activation::None);
        if (!layer1.loadWeights((*layers)[0]))
            return false;

        // Layer 2: Dense 64 -> 64
        layer2.initialize(HIDDEN_SIZE, HIDDEN_SIZE, Activation::None);
        if (!layer2.loadWeights((*layers)[2]))
            return false;

        // Layer 4: Dense 64 -> num_patterns
        outputLayer.initialize(HIDDEN_SIZE, numPatterns, Activation::None);
        if (!outputLayer.loadWeights((*layers)[4]))
            return false;

        isLoaded = true;
        return true;
    }

    /**
     * Get top pattern recommendations
     *
     * @param styleIndex 0-11 style index
     * @param sectionIndex 0-6 section index
     * @param energy 0-1 energy level
     * @param complexity 0-1 complexity level
     * @param topK number of patterns to return
     * @return vector of (pattern_index, score) pairs, sorted by score descending
     */
    std::vector<std::pair<int, float>> predict(int styleIndex, int sectionIndex,
                                                float energy, float complexity,
                                                int topK = 5)
    {
        std::vector<std::pair<int, float>> result;

        if (!isLoaded)
            return result;

        // Build input vector
        std::vector<float> input(INPUT_SIZE, 0.0f);

        // Style one-hot
        if (styleIndex >= 0 && styleIndex < NUM_STYLES)
            input[styleIndex] = 1.0f;

        // Section one-hot
        if (sectionIndex >= 0 && sectionIndex < NUM_SECTIONS)
            input[NUM_STYLES + sectionIndex] = 1.0f;

        // Energy and complexity
        input[NUM_STYLES + NUM_SECTIONS] = energy;
        input[NUM_STYLES + NUM_SECTIONS + 1] = complexity;

        // Forward pass
        const auto& h1 = layer1.forward(input);
        std::vector<float> h1_relu(h1.size());
        for (size_t i = 0; i < h1.size(); ++i)
            h1_relu[i] = applyActivation(h1[i], Activation::ReLU);

        const auto& h2 = layer2.forward(h1_relu);
        std::vector<float> h2_relu(h2.size());
        for (size_t i = 0; i < h2.size(); ++i)
            h2_relu[i] = applyActivation(h2[i], Activation::ReLU);

        const auto& scores = outputLayer.forward(h2_relu);

        // Find top-K patterns
        std::vector<std::pair<int, float>> allScores;
        for (int i = 0; i < numPatterns; ++i)
            allScores.emplace_back(i, scores[i]);

        std::partial_sort(allScores.begin(),
                          allScores.begin() + std::min(topK, numPatterns),
                          allScores.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });

        for (int i = 0; i < std::min(topK, numPatterns); ++i)
            result.push_back(allScores[i]);

        return result;
    }

    /**
     * Get pattern ID by index
     */
    juce::String getPatternId(int index) const
    {
        if (index >= 0 && index < patternIdList.size())
            return patternIdList[index];
        return {};
    }

    int getNumPatterns() const { return numPatterns; }
    bool loaded() const { return isLoaded; }

private:
    DenseLayer layer1;
    DenseLayer layer2;
    DenseLayer outputLayer;
    int numPatterns = 0;
    std::vector<juce::String> patternIdList;
    bool isLoaded = false;
};

//==============================================================================
/**
 * Timing Statistics from GMD
 * Provides per-instrument timing characteristics learned from real drummers
 */
struct TimingStats
{
    float meanMs = 0.0f;
    float stdMs = 20.0f;
    float medianMs = 0.0f;
    float velocityMean = 80.0f;
    float velocityStd = 20.0f;
    int sampleCount = 0;
};

class TimingStatsLibrary
{
public:
    bool loadFromJSON(const juce::File& jsonFile)
    {
        auto jsonText = jsonFile.loadFileAsString();
        auto parsed = juce::JSON::parse(jsonText);

        if (parsed.isVoid())
            return false;

        return loadFromVar(parsed);
    }

    bool loadFromVar(const juce::var& parsed)
    {
        if (parsed.isVoid())
            return false;

        // Parse each instrument category
        auto names = parsed.getDynamicObject()->getProperties();
        for (const auto& prop : names)
        {
            auto name = prop.name.toString();
            auto& stats = prop.value;

            int catIndex = getCategoryIndex(name);
            if (catIndex >= 0 && catIndex < 6)
            {
                categoryStats[catIndex].meanMs = static_cast<float>(stats["timing_mean_ms"]);
                categoryStats[catIndex].stdMs = static_cast<float>(stats["timing_std_ms"]);
                categoryStats[catIndex].medianMs = static_cast<float>(stats["timing_median_ms"]);
                categoryStats[catIndex].velocityMean = static_cast<float>(stats["velocity_mean"]);
                categoryStats[catIndex].velocityStd = static_cast<float>(stats["velocity_std"]);
                categoryStats[catIndex].sampleCount = static_cast<int>(stats["sample_count"]);
            }
        }

        isLoaded = true;
        return true;
    }

    const TimingStats& getStats(int categoryIndex) const
    {
        if (categoryIndex >= 0 && categoryIndex < 6)
            return categoryStats[categoryIndex];
        return categoryStats[5];  // Return 'other' as default
    }

    bool loaded() const { return isLoaded; }

private:
    int getCategoryIndex(const juce::String& name) const
    {
        if (name == "kick") return 0;
        if (name == "snare") return 1;
        if (name == "hihat") return 2;
        if (name == "tom") return 3;
        if (name == "cymbal") return 4;
        if (name == "other") return 5;
        return -1;
    }

    std::array<TimingStats, 6> categoryStats;
    bool isLoaded = false;
};

}  // namespace MLInference
