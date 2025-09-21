/*
  ==============================================================================

    ReverbOptimizations.h - Performance optimizations for reverb processing

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <immintrin.h>  // For SIMD
#include <array>

class ReverbOptimizations
{
public:
    // SIMD-optimized FDN processing
    class SIMDDelayNetwork
    {
    public:
        static constexpr int SIMD_WIDTH = 4;  // Process 4 delays at once with SSE

        void process(const float* input, float* output, int numSamples);
        void processAVX(const float* input, float* output, int numSamples);

    private:
        // Aligned buffers for SIMD
        alignas(32) std::array<float, 65536> delayBuffer;
        alignas(32) std::array<float, 16> feedbackCoeffs;
        alignas(32) std::array<float, 16> mixMatrix[16];
    };

    // Denormal prevention
    class DenormalPrevention
    {
    public:
        static inline float processSample(float x)
        {
            // Add tiny DC offset to prevent denormals
            static const float antiDenormal = 1e-24f;
            return x + antiDenormal;
        }

        static void processBlock(float* data, int numSamples)
        {
            const __m128 antiDenormal = _mm_set1_ps(1e-24f);
            for (int i = 0; i < numSamples; i += 4)
            {
                __m128 samples = _mm_load_ps(&data[i]);
                samples = _mm_add_ps(samples, antiDenormal);
                _mm_store_ps(&data[i], samples);
            }
        }
    };

    // Block-based processing for efficiency
    class BlockProcessor
    {
    public:
        static constexpr int BLOCK_SIZE = 32;  // Process in chunks

        template<typename Processor>
        void processInBlocks(Processor& processor, float* data, int totalSamples)
        {
            int remaining = totalSamples;
            int offset = 0;

            while (remaining > 0)
            {
                int toProcess = std::min(remaining, BLOCK_SIZE);
                processor.processBlock(&data[offset], toProcess);
                offset += toProcess;
                remaining -= toProcess;
            }
        }
    };

    // Memory pool for delay lines
    class DelayMemoryPool
    {
    public:
        DelayMemoryPool(size_t totalSize);
        ~DelayMemoryPool();

        float* allocateDelay(size_t samples);
        void reset();

    private:
        std::unique_ptr<float[]> memoryPool;
        size_t poolSize;
        size_t currentOffset = 0;
        std::vector<std::pair<float*, size_t>> allocations;
    };

    // CPU feature detection
    class CPUFeatures
    {
    public:
        static bool hasSSE() { return juce::SystemStats::hasSSE(); }
        static bool hasSSE2() { return juce::SystemStats::hasSSE2(); }
        static bool hasAVX() { return juce::SystemStats::hasAVX(); }
        static bool hasAVX2() { return juce::SystemStats::hasAVX2(); }

        static void printFeatures()
        {
            DBG("CPU Features:");
            DBG("  SSE: " << (hasSSE() ? "Yes" : "No"));
            DBG("  SSE2: " << (hasSSE2() ? "Yes" : "No"));
            DBG("  AVX: " << (hasAVX() ? "Yes" : "No"));
            DBG("  AVX2: " << (hasAVX2() ? "Yes" : "No"));
        }
    };

    // Lookahead limiter for output
    class LookaheadLimiter
    {
    public:
        LookaheadLimiter();

        void prepare(double sampleRate, int maxBlockSize);
        void setThreshold(float dB);
        void setRelease(float ms);

        void process(float* left, float* right, int numSamples);

    private:
        static constexpr int LOOKAHEAD_SAMPLES = 32;

        juce::dsp::DelayLine<float> delayL{1024};
        juce::dsp::DelayLine<float> delayR{1024};

        std::array<float, LOOKAHEAD_SAMPLES> lookaheadBuffer;
        int lookaheadIndex = 0;

        float threshold = 0.95f;
        float releaseTime = 50.0f;
        float currentGain = 1.0f;

        double sampleRate = 44100.0;
    };

    // Multi-threaded processing for dual engines
    class ParallelProcessor
    {
    public:
        ParallelProcessor();

        template<typename ProcessorA, typename ProcessorB>
        void processParallel(ProcessorA& engineA, ProcessorB& engineB,
                           const float* input, float* outputA, float* outputB,
                           int numSamples)
        {
            // Use JUCE's thread pool for parallel processing
            auto job1 = [&] { engineA.process(input, outputA, numSamples); };
            auto job2 = [&] { engineB.process(input, outputB, numSamples); };

            std::thread t1(job1);
            std::thread t2(job2);

            t1.join();
            t2.join();
        }

    private:
        juce::ThreadPool threadPool{2};
    };

    // Quality modes
    enum class QualityMode
    {
        Draft,      // Lowest CPU, reduced quality
        Normal,     // Balanced
        High,       // Higher CPU, better quality
        Ultra       // Maximum quality, highest CPU
    };

    struct QualitySettings
    {
        int numDelayLines;
        int diffuserStages;
        bool useOversampling;
        bool useSIMD;
        int modulationVoices;

        static QualitySettings getSettings(QualityMode mode)
        {
            switch (mode)
            {
                case QualityMode::Draft:
                    return {8, 2, false, false, 1};

                case QualityMode::Normal:
                    return {16, 4, false, true, 2};

                case QualityMode::High:
                    return {24, 6, true, true, 3};

                case QualityMode::Ultra:
                    return {32, 8, true, true, 4};

                default:
                    return {16, 4, false, true, 2};
            }
        }
    };
};