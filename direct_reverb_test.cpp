/*
  Direct test of DragonflyReverb engine
  Tests Room reverb without full JUCE framework
*/

#include <iostream>
#include <cmath>
#include <vector>
#include <cstring>
#include <iomanip>

// Mock minimal JUCE classes needed
namespace juce {
    template<typename T>
    T jmin(T a, T b) { return a < b ? a : b; }

    template<typename T>
    T jlimit(T min, T val, T max) {
        if (val < min) return min;
        if (val > max) return max;
        return val;
    }

    class String {
        std::string s;
    public:
        String() {}
        String(const char* str) : s(str) {}
        String(float val, int precision) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.*f", precision, val);
            s = buf;
        }
        const std::string& toStdString() const { return s; }
    };

    template<typename T>
    class AudioBuffer {
        std::vector<std::vector<T>> channels;
        int numChannels;
        int numSamples;

    public:
        AudioBuffer() : numChannels(0), numSamples(0) {}
        AudioBuffer(int nChannels, int nSamples) : numChannels(nChannels), numSamples(nSamples) {
            channels.resize(nChannels);
            for (int i = 0; i < nChannels; ++i) {
                channels[i].resize(nSamples, 0);
            }
        }

        void setSize(int nChannels, int nSamples) {
            numChannels = nChannels;
            numSamples = nSamples;
            channels.resize(nChannels);
            for (int i = 0; i < nChannels; ++i) {
                channels[i].resize(nSamples, 0);
            }
        }

        int getNumChannels() const { return numChannels; }
        int getNumSamples() const { return numSamples; }

        T* getWritePointer(int channel) {
            return channels[channel].data();
        }

        const T* getReadPointer(int channel) const {
            return channels[channel].data();
        }

        void clear() {
            for (auto& ch : channels) {
                std::fill(ch.begin(), ch.end(), 0);
            }
        }

        void copyFrom(int destChannel, int destStart, const AudioBuffer& source,
                     int sourceChannel, int sourceStart, int numToCopy) {
            std::memcpy(&channels[destChannel][destStart],
                       &source.channels[sourceChannel][sourceStart],
                       numToCopy * sizeof(T));
        }

        T getSample(int channel, int sample) const {
            return channels[channel][sample];
        }

        void setSample(int channel, int sample, T value) {
            channels[channel][sample] = value;
        }

        T getMagnitude(int startSample, int numSamplesToCheck) const {
            T maxVal = 0;
            for (int ch = 0; ch < numChannels; ++ch) {
                for (int i = startSample; i < startSample + numSamplesToCheck && i < numSamples; ++i) {
                    maxVal = std::max(maxVal, std::abs(channels[ch][i]));
                }
            }
            return maxVal;
        }
    };
}

// Simplified to avoid assertions
#define jassert(x)
#define jassertfalse
#define DBG(x)
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)

// Include the reverb implementation
#include "plugins/StudioReverb/Source/DSP/DragonflyReverb.cpp"
#include "plugins/StudioReverb/Source/freeverb/earlyref.cpp"
#include "plugins/StudioReverb/Source/freeverb/zrev2.cpp"
#include "plugins/StudioReverb/Source/freeverb/progenitor2.cpp"
#include "plugins/StudioReverb/Source/freeverb/progenitor.cpp"
#include "plugins/StudioReverb/Source/freeverb/revbase.cpp"
#include "plugins/StudioReverb/Source/freeverb/biquad_f.cpp"
#include "plugins/StudioReverb/Source/freeverb/efilter.cpp"
#include "plugins/StudioReverb/Source/freeverb/utils.cpp"

const int SAMPLE_RATE = 44100;
const int BUFFER_SIZE = 512;

float calculateRMS(const float* buffer, int numSamples) {
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / numSamples);
}

bool testReverbAlgorithm(DragonflyReverb::Algorithm algorithm, const char* name) {
    std::cout << "\n=== Testing " << name << " Algorithm ===\n";

    // Create reverb processor
    auto reverb = std::make_unique<DragonflyReverb>();

    // Initialize
    reverb->prepare(SAMPLE_RATE, BUFFER_SIZE);

    // Set algorithm
    reverb->setAlgorithm(algorithm);

    // Configure for maximum reverb
    reverb->setDryLevel(0.0f);    // No dry signal
    reverb->setLateLevel(1.0f);   // Full late reverb
    reverb->setEarlyLevel(0.5f);  // Some early reflections
    reverb->setSize(40.0f);       // Medium-large size
    reverb->setDecay(2.0f);       // 2 second decay
    reverb->setDiffuse(75.0f);    // Good diffusion
    reverb->setPreDelay(10.0f);   // Small predelay

    std::cout << "Settings: Dry=0%, Late=100%, Early=50%, Size=40m, Decay=2s\n";

    // Create test signal
    const int testDuration = SAMPLE_RATE * 2; // 2 seconds
    juce::AudioBuffer<float> buffer(2, testDuration);
    buffer.clear();

    // Add impulse
    buffer.setSample(0, 100, 1.0f);
    buffer.setSample(1, 100, 1.0f);

    // Process in chunks
    int processed = 0;
    while (processed < testDuration) {
        int toProcess = std::min(BUFFER_SIZE, testDuration - processed);

        juce::AudioBuffer<float> chunk(2, toProcess);
        chunk.copyFrom(0, 0, buffer, 0, processed, toProcess);
        chunk.copyFrom(1, 0, buffer, 1, processed, toProcess);

        reverb->processBlock(chunk);

        buffer.copyFrom(0, processed, chunk, 0, 0, toProcess);
        buffer.copyFrom(1, processed, chunk, 1, 0, toProcess);

        processed += toProcess;
    }

    // Analyze reverb tail (skip first 0.5 seconds)
    int tailStart = SAMPLE_RATE / 2;
    int tailLength = SAMPLE_RATE;

    float rmsL = calculateRMS(buffer.getReadPointer(0) + tailStart, tailLength);
    float rmsR = calculateRMS(buffer.getReadPointer(1) + tailStart, tailLength);
    float avgRMS = (rmsL + rmsR) / 2.0f;

    float peak = buffer.getMagnitude(tailStart, tailLength);

    std::cout << "Reverb Tail (0.5s-1.5s):\n";
    std::cout << "  RMS:  L=" << std::fixed << std::setprecision(6) << rmsL
              << ", R=" << rmsR << ", Avg=" << avgRMS << "\n";
    std::cout << "  Peak: " << peak << "\n";

    bool hasReverb = avgRMS > 0.0001f;

    if (hasReverb) {
        std::cout << "✓ " << name << " reverb is producing output!\n";
    } else {
        std::cout << "✗ " << name << " reverb is NOT producing output!\n";
    }

    return hasReverb;
}

int main() {
    std::cout << "Direct DragonflyReverb Engine Test\n";
    std::cout << "===================================\n\n";

    bool allPassed = true;

    allPassed &= testReverbAlgorithm(DragonflyReverb::Algorithm::Room, "Room");
    allPassed &= testReverbAlgorithm(DragonflyReverb::Algorithm::Hall, "Hall");
    allPassed &= testReverbAlgorithm(DragonflyReverb::Algorithm::Plate, "Plate");
    allPassed &= testReverbAlgorithm(DragonflyReverb::Algorithm::EarlyReflections, "Early Reflections");

    std::cout << "\n=== TEST SUMMARY ===\n";
    if (allPassed) {
        std::cout << "✓ All reverb algorithms are working!\n";
        return 0;
    } else {
        std::cout << "✗ Some reverb algorithms are NOT working!\n";
        std::cout << "Check the implementation of failing algorithms.\n";
        return 1;
    }
}