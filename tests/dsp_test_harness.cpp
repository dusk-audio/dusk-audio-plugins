/**
 * Dusk Audio - DSP Test Harness
 * ==================================
 * Standalone C++ test harness for audio plugin DSP testing.
 * Can be compiled against plugin source code to test DSP algorithms
 * without loading the full VST3/LV2 wrapper.
 *
 * Compile with:
 *   g++ -std=c++17 -O2 -I../plugins -I/path/to/juce dsp_test_harness.cpp -o dsp_test_harness -lm
 *
 * Usage:
 *   ./dsp_test_harness [--verbose] [--test-name TEST]
 */

#include <cmath>
#include <vector>
#include <complex>
#include <iostream>
#include <iomanip>
#include <string>
#include <functional>
#include <chrono>
#include <random>
#include <fstream>
#include <algorithm>
#include <numeric>

// Portable PI constant (M_PI is a POSIX extension, not available on all platforms)
constexpr double PI = 3.14159265358979323846;

//==============================================================================
// Test Framework
//==============================================================================

class TestResult {
public:
    std::string name;
    bool passed;
    std::string details;
    double value;
    double threshold;
    std::string unit;

    void print() const {
        const char* status = passed ? "\033[32m[PASS]\033[0m" : "\033[31m[FAIL]\033[0m";
        std::cout << status << " " << name;
        if (value != 0.0 || !unit.empty()) {
            std::cout << ": " << std::fixed << std::setprecision(4) << value << " " << unit;
            if (threshold != 0.0) {
                std::cout << " (threshold: " << threshold << " " << unit << ")";
            }
        }
        std::cout << std::endl;
        if (!details.empty() && !passed) {
            std::cout << "       " << details << std::endl;
        }
    }
};

class TestSuite {
public:
    std::string name;
    std::vector<TestResult> results;
    int passed = 0;
    int failed = 0;

    void addResult(const TestResult& result) {
        results.push_back(result);
        if (result.passed) passed++;
        else failed++;
    }

    void printSummary() const {
        std::cout << "\n========================================\n";
        std::cout << "Test Suite: " << name << "\n";
        std::cout << "========================================\n";

        for (const auto& result : results) {
            result.print();
        }

        std::cout << "\n----------------------------------------\n";
        std::cout << "Passed: " << passed << ", Failed: " << failed << "\n";
    }
};

//==============================================================================
// Signal Generation Utilities
//==============================================================================

namespace SignalGen {

std::vector<float> sineWave(float frequency, float sampleRate, float duration, float amplitude = 0.5f) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    std::vector<float> signal(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        signal[i] = amplitude * std::sin(2.0f * static_cast<float>(PI) * frequency * t);
    }

    return signal;
}

std::vector<float> impulse(float sampleRate, float duration, float amplitude = 1.0f) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    std::vector<float> signal(numSamples, 0.0f);
    signal[numSamples / 10] = amplitude;  // Place impulse at 10%
    return signal;
}

std::vector<float> whiteNoise(float sampleRate, float duration, float amplitude = 0.3f) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    std::vector<float> signal(numSamples);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dist(0.0f, amplitude);

    for (size_t i = 0; i < numSamples; ++i) {
        signal[i] = dist(gen);
    }

    return signal;
}

std::vector<float> dcSignal(float sampleRate, float duration, float level = 0.5f) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    return std::vector<float>(numSamples, level);
}

std::vector<float> silence(float sampleRate, float duration) {
    size_t numSamples = static_cast<size_t>(duration * sampleRate);
    return std::vector<float>(numSamples, 0.0f);
}

} // namespace SignalGen

//==============================================================================
// Analysis Utilities
//==============================================================================

namespace Analysis {

float calculateRMS(const std::vector<float>& signal) {
    if (signal.empty()) return 0.0f;

    float sumSquares = 0.0f;
    for (float sample : signal) {
        sumSquares += sample * sample;
    }
    return std::sqrt(sumSquares / static_cast<float>(signal.size()));
}

float calculatePeak(const std::vector<float>& signal) {
    if (signal.empty()) return 0.0f;

    float peak = 0.0f;
    for (float sample : signal) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

float rmsToDb(float rms) {
    return 20.0f * std::log10(rms + 1e-10f);
}

float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Simple DFT for small signals (for testing purposes)
std::vector<std::complex<float>> dft(const std::vector<float>& signal) {
    size_t N = signal.size();
    std::vector<std::complex<float>> spectrum(N / 2 + 1);

    for (size_t k = 0; k < N / 2 + 1; ++k) {
        std::complex<float> sum(0.0f, 0.0f);
        for (size_t n = 0; n < N; ++n) {
            float angle = 2.0f * static_cast<float>(PI) * k * n / N;
            sum += signal[n] * std::complex<float>(std::cos(angle), -std::sin(angle));
        }
        spectrum[k] = sum;
    }

    return spectrum;
}

float findFundamentalAmplitude(const std::vector<float>& signal, float frequency, float sampleRate) {
    auto spectrum = dft(signal);
    size_t binIndex = static_cast<size_t>(frequency * signal.size() / sampleRate);

    if (binIndex < spectrum.size()) {
        return std::abs(spectrum[binIndex]) * 2.0f / static_cast<float>(signal.size());
    }
    return 0.0f;
}

float calculateTHD(const std::vector<float>& signal, float fundamental, float sampleRate, int numHarmonics = 5) {
    auto spectrum = dft(signal);
    float binResolution = sampleRate / static_cast<float>(signal.size());

    float fundamentalAmp = 0.0f;
    float harmonicPower = 0.0f;

    for (int h = 1; h <= numHarmonics; ++h) {
        float freq = fundamental * h;
        if (freq >= sampleRate / 2) break;

        size_t binIndex = static_cast<size_t>(freq / binResolution);
        if (binIndex < spectrum.size()) {
            float amp = std::abs(spectrum[binIndex]);
            if (h == 1) {
                fundamentalAmp = amp;
            } else {
                harmonicPower += amp * amp;
            }
        }
    }

    if (fundamentalAmp > 0) {
        return 100.0f * std::sqrt(harmonicPower) / fundamentalAmp;
    }
    return 0.0f;
}

// Check if signal contains NaN or Inf values
bool hasInvalidSamples(const std::vector<float>& signal) {
    for (float sample : signal) {
        if (std::isnan(sample) || std::isinf(sample)) {
            return true;
        }
    }
    return false;
}

// Check if signal clips (exceeds ±1.0)
float getClippingRatio(const std::vector<float>& signal) {
    size_t clippedSamples = 0;
    for (float sample : signal) {
        if (std::abs(sample) > 1.0f) {
            clippedSamples++;
        }
    }
    return static_cast<float>(clippedSamples) / static_cast<float>(signal.size());
}

// Null test - returns residual in dB
float nullTest(const std::vector<float>& original, const std::vector<float>& processed) {
    if (original.size() != processed.size()) {
        return 999.0f;  // Can't compare different lengths - return failure value
    }

    std::vector<float> residual(original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        residual[i] = processed[i] - original[i];
    }

    float residualRMS = calculateRMS(residual);
    float originalRMS = calculateRMS(original);

    if (originalRMS > 0) {
        return 20.0f * std::log10(residualRMS / originalRMS + 1e-15f);
    }
    return rmsToDb(residualRMS);
}

} // namespace Analysis

//==============================================================================
// DSP Component Tests
//==============================================================================

namespace DSPTests {

// Test a simple gain stage
TestResult testGainStage(std::function<void(std::vector<float>&, float)> gainFunc, float gain) {
    TestResult result;
    result.name = "Gain Stage (" + std::to_string(gain) + " dB)";

    auto input = SignalGen::sineWave(1000.0f, 48000.0f, 0.1f, 0.5f);
    auto output = input;

    gainFunc(output, gain);

    float inputRMS = Analysis::calculateRMS(input);
    float outputRMS = Analysis::calculateRMS(output);
    float measuredGain = Analysis::rmsToDb(outputRMS) - Analysis::rmsToDb(inputRMS);

    result.value = measuredGain;
    result.threshold = gain;
    result.unit = "dB";
    result.passed = std::abs(measuredGain - gain) < 0.1f;  // Within 0.1 dB
    result.details = "Expected " + std::to_string(gain) + " dB, got " + std::to_string(measuredGain) + " dB";

    return result;
}

// Test for DC offset
TestResult testDCOffset(const std::vector<float>& signal) {
    TestResult result;
    result.name = "DC Offset";

    float dcOffset = std::accumulate(signal.begin(), signal.end(), 0.0f) / static_cast<float>(signal.size());

    result.value = dcOffset;
    result.threshold = 0.001f;
    result.unit = "";
    result.passed = std::abs(dcOffset) < 0.001f;
    result.details = "DC offset should be near zero";

    return result;
}

// Test for NaN/Inf samples
TestResult testSampleValidity(const std::vector<float>& signal) {
    TestResult result;
    result.name = "Sample Validity (no NaN/Inf)";

    bool hasInvalid = Analysis::hasInvalidSamples(signal);

    result.passed = !hasInvalid;
    result.details = hasInvalid ? "Signal contains NaN or Inf values!" : "";

    return result;
}

// Test for clipping
TestResult testClipping(const std::vector<float>& signal) {
    TestResult result;
    result.name = "No Clipping";

    float clippingRatio = Analysis::getClippingRatio(signal);

    result.value = clippingRatio * 100.0f;
    result.threshold = 0.0f;
    result.unit = "%";
    result.passed = clippingRatio == 0.0f;
    result.details = "Signal should not exceed ±1.0";

    return result;
}

// Test noise floor
TestResult testNoiseFloor(std::function<std::vector<float>(const std::vector<float>&)> processFunc) {
    TestResult result;
    result.name = "Noise Floor";

    auto silence = SignalGen::silence(48000.0f, 1.0f);
    auto output = processFunc(silence);

    float noiseFloor = Analysis::rmsToDb(Analysis::calculateRMS(output));

    result.value = noiseFloor;
    result.threshold = -90.0f;
    result.unit = "dB";
    result.passed = noiseFloor < -90.0f;
    result.details = "Self-noise should be below -90 dB";

    return result;
}

// Test THD
TestResult testTHD(std::function<std::vector<float>(const std::vector<float>&)> processFunc, float maxTHD = 1.0f) {
    TestResult result;
    result.name = "Total Harmonic Distortion";

    auto input = SignalGen::sineWave(1000.0f, 48000.0f, 0.5f, 0.5f);
    auto output = processFunc(input);

    float thd = Analysis::calculateTHD(output, 1000.0f, 48000.0f);

    result.value = thd;
    result.threshold = maxTHD;
    result.unit = "%";
    result.passed = thd < maxTHD;
    result.details = "THD at 1kHz";

    return result;
}

// Test bypass null
TestResult testBypassNull(const std::vector<float>& input,
                          std::function<std::vector<float>(const std::vector<float>&, bool)> processFunc) {
    TestResult result;
    result.name = "Bypass Null Test";

    auto bypassed = processFunc(input, true);  // true = bypassed
    float nullDb = Analysis::nullTest(input, bypassed);

    result.value = nullDb;
    result.threshold = -120.0f;
    result.unit = "dB";
    result.passed = nullDb < -120.0f;
    result.details = "Bypass should produce bit-perfect output";

    return result;
}

} // namespace DSPTests

//==============================================================================
// Example: Simple Compressor Test (as template)
//==============================================================================

class SimpleCompressor {
public:
    float threshold = -20.0f;  // dB
    float ratio = 4.0f;
    float attack = 10.0f;      // ms
    float release = 100.0f;    // ms
    float makeupGain = 0.0f;   // dB
    bool bypassed = false;

private:
    float envelope = 0.0f;
    float sampleRate = 48000.0f;

public:
    void prepare(float sr) {
        sampleRate = sr;
        envelope = 0.0f;
    }

    void process(std::vector<float>& buffer) {
        if (bypassed) return;

        float attackCoeff = std::exp(-1.0f / (attack * 0.001f * sampleRate));
        float releaseCoeff = std::exp(-1.0f / (release * 0.001f * sampleRate));
        float thresholdLin = Analysis::dbToLinear(threshold);
        float makeupLin = Analysis::dbToLinear(makeupGain);

        for (float& sample : buffer) {
            float inputLevel = std::abs(sample);

            // Envelope follower
            if (inputLevel > envelope) {
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * inputLevel;
            } else {
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevel;
            }

            // Gain calculation
            float gain = 1.0f;
            if (envelope > thresholdLin) {
                float overDb = 20.0f * std::log10(envelope / thresholdLin);
                float reductionDb = overDb * (1.0f - 1.0f / ratio);
                gain = Analysis::dbToLinear(-reductionDb);
            }

            sample *= gain * makeupLin;
        }
    }
};

//==============================================================================
// Main Test Runner
//==============================================================================

void runSimpleCompressorTests() {
    TestSuite suite;
    suite.name = "Simple Compressor DSP Tests";

    SimpleCompressor comp;
    comp.prepare(48000.0f);

    // Lambda wrapper for processing
    auto processFunc = [&comp](const std::vector<float>& input) {
        auto output = input;
        comp.bypassed = false;
        comp.process(output);
        return output;
    };

    auto bypassProcessFunc = [&comp](const std::vector<float>& input, bool bypass) {
        auto output = input;
        comp.bypassed = bypass;
        comp.process(output);
        return output;
    };

    // Test 1: Sample validity
    {
        auto input = SignalGen::sineWave(1000.0f, 48000.0f, 0.5f, 0.9f);
        auto output = processFunc(input);
        suite.addResult(DSPTests::testSampleValidity(output));
    }

    // Test 2: DC offset
    {
        auto input = SignalGen::sineWave(1000.0f, 48000.0f, 0.5f, 0.5f);
        auto output = processFunc(input);
        suite.addResult(DSPTests::testDCOffset(output));
    }

    // Test 3: No clipping with normal input
    {
        comp.makeupGain = 0.0f;
        auto input = SignalGen::sineWave(1000.0f, 48000.0f, 0.5f, 0.5f);
        auto output = processFunc(input);
        suite.addResult(DSPTests::testClipping(output));
    }

    // Test 4: Noise floor
    {
        suite.addResult(DSPTests::testNoiseFloor(processFunc));
    }

    // Test 5: Bypass null test
    {
        auto input = SignalGen::whiteNoise(48000.0f, 0.5f, 0.5f);
        suite.addResult(DSPTests::testBypassNull(input, bypassProcessFunc));
    }

    // Test 6: Compression actually reduces peaks
    {
        TestResult result;
        result.name = "Compression Reduces Peaks";

        comp.threshold = -20.0f;
        comp.ratio = 4.0f;
        comp.makeupGain = 0.0f;
        comp.bypassed = false;

        auto input = SignalGen::sineWave(1000.0f, 48000.0f, 0.5f, 0.9f);  // Hot signal
        auto output = processFunc(input);

        float inputPeak = Analysis::calculatePeak(input);
        float outputPeak = Analysis::calculatePeak(output);

        result.passed = outputPeak < inputPeak;
        result.value = Analysis::rmsToDb(outputPeak) - Analysis::rmsToDb(inputPeak);
        result.unit = "dB reduction";
        result.details = "Compressor should reduce peaks above threshold";
        suite.addResult(result);
    }

    suite.printSummary();
}

//==============================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Dusk Audio - DSP Test Harness\n";
    std::cout << "========================================\n\n";

    // Run example compressor tests
    runSimpleCompressorTests();

    std::cout << "\n----------------------------------------\n";
    std::cout << "To test your own DSP code:\n";
    std::cout << "1. Include your DSP headers\n";
    std::cout << "2. Create test functions following the pattern above\n";
    std::cout << "3. Call them from main()\n";
    std::cout << "----------------------------------------\n";

    return 0;
}
