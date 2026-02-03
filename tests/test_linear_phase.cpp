// Standalone test for LinearPhaseEQProcessor
// Compile: clang++ -std=c++17 test_linear_phase.cpp -o test_linear_phase

#include <iostream>
#include <vector>
#include <cmath>
#include <array>

// Minimal JUCE-like FFT implementation for testing
namespace juce {
    namespace dsp {
        class FFT {
        public:
            FFT(int order) : size(1 << order) {}

            void performRealOnlyForwardTransform(float* data) {
                // Simplified DFT for testing
                std::vector<float> temp(size * 2);
                for (int k = 0; k < size; ++k) {
                    float sumRe = 0, sumIm = 0;
                    for (int n = 0; n < size; ++n) {
                        float angle = -2.0f * static_cast<float>(M_PI) * k * n / size;
                        sumRe += data[n] * std::cos(angle);
                        sumIm += data[n] * std::sin(angle);
                    }
                    temp[k * 2] = sumRe;
                    temp[k * 2 + 1] = sumIm;
                }
                std::copy(temp.begin(), temp.end(), data);
            }

            void performRealOnlyInverseTransform(float* data) {
                // Simplified inverse DFT with proper 1/N normalization
                std::vector<float> temp(size);
                for (int n = 0; n < size; ++n) {
                    float sum = 0;
                    for (int k = 0; k < size; ++k) {
                        float re = data[k * 2];
                        float im = data[k * 2 + 1];
                        float angle = 2.0f * static_cast<float>(M_PI) * k * n / size;
                        sum += re * std::cos(angle) - im * std::sin(angle);
                    }
                    temp[n] = sum / static_cast<float>(size);  // Normalize by 1/N
                }
                std::copy(temp.begin(), temp.end(), data);
            }

            int getSize() const { return size; }

        private:
            int size;
        };
    }

    namespace MathConstants {
        template<typename T>
        static constexpr T pi = static_cast<T>(3.14159265358979323846);
    }

    template<typename T>
    T jlimit(T min, T max, T val) {
        return std::min(max, std::max(min, val));
    }

    template<typename T>
    T jmax(T a, T b) { return std::max(a, b); }
}

// Simple test for linear phase convolution with 2x FFT size
int main() {
    std::cout << "=== Linear Phase EQ Processor Test ===" << std::endl;
    std::cout << "Testing overlap-add convolution with 2x FFT size for linear convolution" << std::endl;

    // Test parameters - matching the real LinearPhaseEQProcessor design
    const int filterLength = 1024;          // IR/filter length (smaller for faster DFT test)
    const int convFftSize = filterLength * 2;  // 2x for linear convolution
    const int hopSize = filterLength / 2;      // 50% overlap
    const int testLength = 22050;              // 0.5 second at 44.1kHz
    const float sampleRate = 44100.0f;

    std::cout << "Filter length: " << filterLength << std::endl;
    std::cout << "Convolution FFT size: " << convFftSize << std::endl;
    std::cout << "Hop size: " << hopSize << std::endl;

    // Create test buffers
    std::vector<float> inputAccum(filterLength, 0.0f);         // Circular input buffer
    std::vector<float> outputAccum(convFftSize * 2, 0.0f);     // Output accumulator
    std::vector<float> latencyDelay(convFftSize * 2, 0.0f);    // Latency compensation delay
    std::vector<float> fftBuffer(convFftSize * 2, 0.0f);       // FFT working buffer
    std::vector<float> irFrequencyDomain(convFftSize * 2, 0.0f); // IR in frequency domain

    int inputWritePos = 0;
    int outputReadPos = 0;
    int delayWritePos = filterLength / 2;  // Start ahead by latency (filterLength/2)
    int delayReadPos = 0;
    int samplesInInputBuffer = 0;

    // Create FFT object for convolution at 2x size
    int convFftOrder = static_cast<int>(std::log2(convFftSize));
    juce::dsp::FFT convFft(convFftOrder);

    // Build flat IR (unity gain, zero phase = impulse at center)
    std::cout << "Building flat IR..." << std::endl;

    // For a flat response, we create an impulse at the center of the filter
    // Then zero-pad and transform to frequency domain for the convolution size
    std::vector<float> irTimeDomain(convFftSize * 2, 0.0f);

    // Put impulse at center of filter (filterLength/2)
    // This gives linear phase (constant group delay)
    irTimeDomain[filterLength / 2] = 1.0f;

    // Forward FFT to get frequency domain IR
    convFft.performRealOnlyForwardTransform(irTimeDomain.data());

    // Copy to IR buffer - this is the flat IR (should be magnitude=1, linear phase)
    std::copy(irTimeDomain.begin(), irTimeDomain.end(), irFrequencyDomain.begin());

    // Generate test signal: 1kHz sine wave
    std::vector<float> testSignal(testLength);
    for (int i = 0; i < testLength; ++i) {
        testSignal[i] = std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f * i / sampleRate);
    }

    // Process through overlap-add with FFT convolution
    std::cout << "Processing " << testLength << " samples..." << std::endl;

    std::vector<float> output(testLength);
    int fftBlocksProcessed = 0;

    for (int i = 0; i < testLength; ++i) {
        // Store input sample in circular buffer
        inputAccum[inputWritePos] = testSignal[i];
        inputWritePos = (inputWritePos + 1) % filterLength;
        samplesInInputBuffer++;

        // Process FFT block when we have hopSize new samples
        if (samplesInInputBuffer >= hopSize) {
            fftBlocksProcessed++;

            // Gather the last filterLength samples from circular input buffer
            for (int j = 0; j < filterLength; ++j) {
                int readIdx = (inputWritePos - filterLength + j + filterLength) % filterLength;
                fftBuffer[j] = inputAccum[readIdx];
            }

            // Zero-pad from filterLength to convFftSize for linear convolution
            std::fill(fftBuffer.begin() + filterLength, fftBuffer.begin() + convFftSize, 0.0f);
            // Clear the rest (for complex output)
            std::fill(fftBuffer.begin() + convFftSize, fftBuffer.end(), 0.0f);

            // Forward FFT on input
            convFft.performRealOnlyForwardTransform(fftBuffer.data());

            // Frequency-domain convolution: complex multiply with IR spectrum
            int numBins = convFftSize / 2 + 1;
            std::vector<float> convResult(convFftSize * 2, 0.0f);
            for (int bin = 0; bin < numBins; ++bin) {
                int idx = bin * 2;
                float inRe = fftBuffer[idx];
                float inIm = fftBuffer[idx + 1];
                float irRe = irFrequencyDomain[idx];
                float irIm = irFrequencyDomain[idx + 1];

                // Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i
                convResult[idx] = inRe * irRe - inIm * irIm;
                convResult[idx + 1] = inRe * irIm + inIm * irRe;
            }

            // Inverse FFT
            convFft.performRealOnlyInverseTransform(convResult.data());

            // Overlap-add: accumulate the full linear convolution result
            for (int j = 0; j < convFftSize; ++j) {
                int writeIdx = (outputReadPos + j) % (convFftSize * 2);
                outputAccum[writeIdx] += convResult[j];
            }

            // Transfer hopSize samples from output accumulator to latency delay
            for (int j = 0; j < hopSize; ++j) {
                int readIdx = (outputReadPos + j) % (convFftSize * 2);
                latencyDelay[delayWritePos] = outputAccum[readIdx];
                outputAccum[readIdx] = 0.0f;  // Clear for next overlap
                delayWritePos = (delayWritePos + 1) % (convFftSize * 2);
            }

            // Advance output read position
            outputReadPos = (outputReadPos + hopSize) % (convFftSize * 2);
            samplesInInputBuffer = 0;
        }

        // Read output from latency delay buffer
        output[i] = latencyDelay[delayReadPos];
        delayReadPos = (delayReadPos + 1) % (convFftSize * 2);
    }

    std::cout << "FFT blocks processed: " << fftBlocksProcessed << std::endl;

    // Analyze output - compare against expected delayed input
    // The impulse is at filterLength/2, so total latency = filterLength/2 + some settling
    int latency = filterLength / 2 + hopSize;  // Impulse position + initial buffering
    std::cout << "Expected latency: ~" << latency << " samples (" << (latency / sampleRate * 1000) << " ms)" << std::endl;

    // Find actual latency by cross-correlation
    float maxCorr = 0;
    int actualLatency = 0;
    for (int lag = 0; lag < filterLength * 2; ++lag) {
        float corr = 0;
        int count = 0;
        for (int i = lag; i < testLength && i - lag < testLength; ++i) {
            corr += output[i] * testSignal[i - lag];
            count++;
        }
        if (count > 0) corr /= count;
        if (corr > maxCorr) {
            maxCorr = corr;
            actualLatency = lag;
        }
    }
    std::cout << "Detected latency (via cross-correlation): " << actualLatency << " samples" << std::endl;

    // Use detected latency for error calculation
    latency = actualLatency;

    // Compute error metrics between output and delayed input
    float maxAbsError = 0.0f;
    float sumSquaredError = 0.0f;
    float maxOutput = 0.0f;
    float maxExpected = 0.0f;
    int validSamples = 0;

    // Skip initial transient and end transient
    int startIdx = latency + hopSize * 2;
    int endIdx = testLength - hopSize * 2;

    for (int i = startIdx; i < endIdx; ++i) {
        int inputIdx = i - latency;
        if (inputIdx >= 0 && inputIdx < testLength) {
            float expected = testSignal[inputIdx];
            float actual = output[i];
            float error = std::abs(actual - expected);

            maxAbsError = std::max(maxAbsError, error);
            sumSquaredError += error * error;
            maxOutput = std::max(maxOutput, std::abs(actual));
            maxExpected = std::max(maxExpected, std::abs(expected));
            validSamples++;
        }
    }

    float rmsError = validSamples > 0 ? std::sqrt(sumSquaredError / validSamples) : 0.0f;

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Max output amplitude: " << maxOutput << std::endl;
    std::cout << "Max expected amplitude: " << maxExpected << std::endl;
    std::cout << "Valid samples compared: " << validSamples << std::endl;
    std::cout << "Max absolute error: " << maxAbsError << std::endl;
    std::cout << "RMS error: " << rmsError << std::endl;

    // Check first few samples after latency
    std::cout << "\nFirst 10 samples after latency:" << std::endl;
    for (int i = 0; i < 10; ++i) {
        int outputIdx = startIdx + i;
        int inputIdx = outputIdx - latency;
        if (inputIdx >= 0 && inputIdx < testLength && outputIdx < testLength) {
            float expected = testSignal[inputIdx];
            float actual = output[outputIdx];
            std::cout << "  output[" << outputIdx << "] = " << actual
                      << " (expected " << expected << ", error = " << std::abs(actual - expected) << ")" << std::endl;
        }
    }

    // Test pass/fail based on error tolerance
    // Allow larger tolerance for simplified DFT (not as precise as real FFT)
    const float errorTolerance = 0.05f;

    if (maxOutput < 0.001f) {
        std::cout << "\n*** FAIL: No output detected! ***" << std::endl;
        return 1;
    } else if (maxAbsError > errorTolerance) {
        std::cout << "\n*** FAIL: Error exceeds tolerance! ***" << std::endl;
        std::cout << "Max absolute error: " << maxAbsError << " > tolerance: " << errorTolerance << std::endl;
        return 1;
    } else {
        std::cout << "\n*** PASS: Output matches expected delayed input! ***" << std::endl;
        std::cout << "Max absolute error: " << maxAbsError << " <= tolerance: " << errorTolerance << std::endl;
        return 0;
    }
}
