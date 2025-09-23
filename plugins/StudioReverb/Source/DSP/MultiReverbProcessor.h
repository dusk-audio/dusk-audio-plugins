#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <memory>

class MultiReverbProcessor
{
public:
    enum class ReverbType
    {
        EarlyReflections = 0,
        Room,
        Plate,
        Hall
    };

    MultiReverbProcessor();
    ~MultiReverbProcessor() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();
    void processBlock(juce::AudioBuffer<float>& buffer);

    // Reverb type
    void setReverbType(ReverbType type) { currentType = type; updateParameters(); }
    ReverbType getReverbType() const { return currentType; }

    // Common parameters
    void setRoomSize(float value);
    void setDamping(float value);
    void setPreDelay(float ms);
    void setDecayTime(float seconds);
    void setDiffusion(float value);
    void setWetLevel(float value);
    void setDryLevel(float value);
    void setWidth(float value);

private:
    ReverbType currentType = ReverbType::Hall;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Parameters
    float roomSize = 0.5f;
    float damping = 0.5f;
    float preDelay = 0.0f;
    float decayTime = 2.0f;
    float diffusion = 0.5f;
    float wetLevel = 0.3f;
    float dryLevel = 0.7f;
    float width = 1.0f;

    // Basic reverb building blocks
    struct DelayLine {
        std::vector<float> buffer;
        int writePos = 0;
        int size = 0;

        void setSize(int newSize) {
            size = newSize;
            buffer.resize(size);
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        float read(int delay) const {
            if (delay >= size || delay < 0) return 0.0f;
            int readPos = (writePos - delay + size) % size;
            return buffer[readPos];
        }

        void write(float sample) {
            buffer[writePos] = sample;
            writePos = (writePos + 1) % size;
        }

        void clear() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
    };

    struct AllpassFilter {
        DelayLine delay;
        float feedback = 0.5f;

        void setSize(int size) {
            delay.setSize(size);
        }

        float process(float input) {
            float delayed = delay.read(delay.size - 1);
            float output = -input + delayed;
            delay.write(input + delayed * feedback);
            return output;
        }

        void clear() {
            delay.clear();
        }
    };

    struct CombFilter {
        DelayLine delay;
        float feedback = 0.5f;
        float damp = 0.5f;
        float filterstore = 0.0f;

        void setSize(int size) {
            delay.setSize(size);
        }

        float process(float input) {
            float output = delay.read(delay.size - 1);
            filterstore = (output * (1.0f - damp)) + (filterstore * damp);
            delay.write(input + filterstore * feedback);
            return output;
        }

        void clear() {
            delay.clear();
            filterstore = 0.0f;
        }
    };

    // Early reflections (simple delay taps)
    struct EarlyReflections {
        static constexpr int numTaps = 8;
        std::array<DelayLine, 2> delays; // Stereo
        const std::array<int, numTaps> baseTapDelays = {67, 113, 183, 229, 307, 383, 461, 521};
        const std::array<float, numTaps> baseTapGains = {0.7f, 0.65f, 0.6f, 0.55f, 0.5f, 0.45f, 0.4f, 0.35f};
        std::array<int, numTaps> tapDelays;
        std::array<float, numTaps> tapGains;

        EarlyReflections() : tapDelays(baseTapDelays), tapGains(baseTapGains) {}

        void prepare(double sampleRate) {
            int maxDelay = 800; // samples at 44.1kHz
            if (sampleRate > 44100) {
                maxDelay = int(maxDelay * sampleRate / 44100.0);
            }
            delays[0].setSize(maxDelay);
            delays[1].setSize(maxDelay);
        }

        void setParameters(float roomSize, float decay, float diffusion) {
            // Adjust tap delays based on room size
            float sizeScale = 0.5f + roomSize * 1.5f;
            // Adjust tap gains based on decay
            float decayScale = 0.3f + decay * 0.7f;

            for (int i = 0; i < numTaps; ++i) {
                tapDelays[i] = int(baseTapDelays[i] * sizeScale);
                tapGains[i] = baseTapGains[i] * decayScale * (1.0f - i * 0.05f * (1.0f - diffusion));
            }
        }

        void process(float* left, float* right, int numSamples) {
            for (int i = 0; i < numSamples; ++i) {
                delays[0].write(left[i]);
                delays[1].write(right[i]);

                float outL = 0.0f, outR = 0.0f;
                for (int tap = 0; tap < numTaps; ++tap) {
                    outL += delays[0].read(tapDelays[tap]) * tapGains[tap];
                    outR += delays[1].read(tapDelays[tap]) * tapGains[tap];
                }

                left[i] = outL;
                right[i] = outR;
            }
        }

        void clear() {
            delays[0].clear();
            delays[1].clear();
        }
    };

    // Room reverb (Freeverb-style)
    struct RoomReverb {
        static constexpr int numCombs = 8;
        static constexpr int numAllpasses = 4;

        std::array<CombFilter, numCombs> combsL, combsR;
        std::array<AllpassFilter, numAllpasses> allpassesL, allpassesR;

        // Tuning values
        const std::array<int, numCombs> combTuning = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        const std::array<int, numAllpasses> allpassTuning = {556, 441, 341, 225};
        const int stereoSpread = 23;

        float baseFeedback = 0.0f;
        float baseDamp = 0.0f;

        void prepare(double sampleRate) {
            double scaleFactor = sampleRate / 44100.0;

            for (int i = 0; i < numCombs; ++i) {
                combsL[i].setSize(int(combTuning[i] * scaleFactor));
                combsR[i].setSize(int((combTuning[i] + stereoSpread) * scaleFactor));
            }

            for (int i = 0; i < numAllpasses; ++i) {
                allpassesL[i].setSize(int(allpassTuning[i] * scaleFactor));
                allpassesR[i].setSize(int((allpassTuning[i] + stereoSpread) * scaleFactor));
                // Set allpass feedback to standard value
                allpassesL[i].feedback = 0.5f;
                allpassesR[i].feedback = 0.5f;
            }
        }

        void setParameters(float roomSize, float damping) {
            baseFeedback = roomSize * 0.28f + 0.7f;
            baseDamp = damping * 0.4f;

            for (auto& comb : combsL) {
                comb.feedback = baseFeedback;
                comb.damp = baseDamp;
            }
            for (auto& comb : combsR) {
                comb.feedback = baseFeedback;
                comb.damp = baseDamp;
            }
        }

        void setDecayFactor(float decay) {
            // Adjust feedback based on decay time
            float extraFeedback = decay * 0.15f;
            for (auto& comb : combsL) {
                comb.feedback = juce::jlimit(0.0f, 0.98f, baseFeedback + extraFeedback);
            }
            for (auto& comb : combsR) {
                comb.feedback = juce::jlimit(0.0f, 0.98f, baseFeedback + extraFeedback);
            }
        }

        void process(float* left, float* right, int numSamples) {
            for (int i = 0; i < numSamples; ++i) {
                float inputL = (left[i] + right[i]) * 0.5f; // Mix to mono for input
                float inputR = inputL;
                float outL = 0.0f, outR = 0.0f;

                // Comb filters in parallel
                for (auto& comb : combsL) {
                    outL += comb.process(inputL);
                }
                for (auto& comb : combsR) {
                    outR += comb.process(inputR);
                }

                // Allpass filters in series
                for (auto& allpass : allpassesL) {
                    outL = allpass.process(outL);
                }
                for (auto& allpass : allpassesR) {
                    outR = allpass.process(outR);
                }

                left[i] = outL * 0.015f; // Scale down
                right[i] = outR * 0.015f;
            }
        }

        void clear() {
            for (auto& comb : combsL) comb.clear();
            for (auto& comb : combsR) comb.clear();
            for (auto& allpass : allpassesL) allpass.clear();
            for (auto& allpass : allpassesR) allpass.clear();
        }
    };

    // Plate reverb (simplified and stable implementation)
    struct PlateReverb {
        std::array<AllpassFilter, 4> diffusionL, diffusionR;
        std::array<DelayLine, 2> delays;
        std::array<AllpassFilter, 2> modulatedAllpass;
        float feedback = 0.7f;
        float baseDiffusion = 0.7f;
        float damping = 0.0f;
        float filterStoreL = 0.0f;  // Instance variables, not static
        float filterStoreR = 0.0f;

        void prepare(double sampleRate) {
            double scaleFactor = sampleRate / 44100.0;

            // Input diffusion
            diffusionL[0].setSize(int(142 * scaleFactor));
            diffusionL[1].setSize(int(107 * scaleFactor));
            diffusionL[2].setSize(int(379 * scaleFactor));
            diffusionL[3].setSize(int(277 * scaleFactor));

            diffusionR[0].setSize(int(151 * scaleFactor));
            diffusionR[1].setSize(int(101 * scaleFactor));
            diffusionR[2].setSize(int(367 * scaleFactor));
            diffusionR[3].setSize(int(263 * scaleFactor));

            // Main delay lines (plate tank)
            delays[0].setSize(int(3720 * scaleFactor));
            delays[1].setSize(int(3163 * scaleFactor));

            // Modulated allpass for metallic character
            modulatedAllpass[0].setSize(int(672 * scaleFactor));
            modulatedAllpass[1].setSize(int(908 * scaleFactor));
            modulatedAllpass[0].feedback = 0.5f;
            modulatedAllpass[1].feedback = 0.5f;
        }

        void setParameters(float decay, float dampingParam) {
            // More conservative feedback to prevent runaway
            feedback = juce::jlimit(0.0f, 0.88f, decay * 0.85f);
            damping = dampingParam * 0.4f;
            baseDiffusion = 0.625f;

            // Set diffusion allpass feedback
            for (auto& ap : diffusionL) ap.feedback = baseDiffusion;
            for (auto& ap : diffusionR) ap.feedback = baseDiffusion;

            // Update modulated allpass feedback
            modulatedAllpass[0].feedback = juce::jlimit(0.0f, 0.7f, 0.5f - damping * 0.2f);
            modulatedAllpass[1].feedback = juce::jlimit(0.0f, 0.7f, 0.5f - damping * 0.2f);
        }

        void setInputDiffusion(float diffusion) {
            float diff = 0.4f + diffusion * 0.35f;
            for (auto& ap : diffusionL) ap.feedback = juce::jlimit(0.0f, 0.75f, diff);
            for (auto& ap : diffusionR) ap.feedback = juce::jlimit(0.0f, 0.75f, diff);
        }

        void process(float* left, float* right, int numSamples) {
            for (int i = 0; i < numSamples; ++i) {
                float inputL = left[i] * 0.5f;  // Input scaling
                float inputR = right[i] * 0.5f;

                // Input diffusion network
                for (auto& ap : diffusionL) {
                    inputL = ap.process(inputL);
                }
                for (auto& ap : diffusionR) {
                    inputR = ap.process(inputR);
                }

                // Read from multiple tap points for plate characteristics
                float tap1 = delays[0].read(266);
                float tap2 = delays[0].read(1800);
                float tap3 = delays[1].read(1913);
                float tap4 = delays[1].read(1200);

                // Read from end of delay lines for feedback
                float delayL = delays[0].read(delays[0].size - 1);
                float delayR = delays[1].read(delays[1].size - 1);

                // Apply damping (simple lowpass)
                filterStoreL = (delayL * (1.0f - damping)) + (filterStoreL * damping);
                filterStoreR = (delayR * (1.0f - damping)) + (filterStoreR * damping);

                // Apply modulated allpass for metallic character
                float processedL = modulatedAllpass[0].process(filterStoreL);
                float processedR = modulatedAllpass[1].process(filterStoreR);

                // Soft clipping to prevent runaway
                processedL = std::tanh(processedL);
                processedR = std::tanh(processedR);

                // Cross-coupled feedback (reduced cross-coupling)
                delays[0].write(inputL + processedR * feedback * 0.5f + processedL * feedback * 0.5f);
                delays[1].write(inputR + processedL * feedback * 0.5f + processedR * feedback * 0.5f);

                // Mix taps for output (more conservative mixing)
                float outL = (tap1 * 0.3f + tap2 * 0.25f + tap3 * 0.2f) * 0.5f;
                float outR = (tap3 * 0.3f + tap4 * 0.25f + tap1 * 0.2f) * 0.5f;

                // Final output scaling
                left[i] = outL;
                right[i] = outR;
            }
        }

        void clear() {
            for (auto& ap : diffusionL) ap.clear();
            for (auto& ap : diffusionR) ap.clear();
            delays[0].clear();
            delays[1].clear();
            modulatedAllpass[0].clear();
            modulatedAllpass[1].clear();
            filterStoreL = 0.0f;
            filterStoreR = 0.0f;
        }
    };

    // Hall reverb (large space)
    struct HallReverb {
        static constexpr int numDelays = 12;
        std::array<CombFilter, numDelays> delaysL, delaysR;
        std::array<AllpassFilter, 4> diffusers;

        const std::array<int, numDelays> delayTimes = {
            1687, 1601, 1491, 1422, 1356, 1277, 1188, 1116, 1009, 901, 797, 687
        };

        float baseFeedback = 0.0f;
        float baseDamp = 0.0f;

        void prepare(double sampleRate) {
            double scaleFactor = sampleRate / 44100.0;

            for (int i = 0; i < numDelays; ++i) {
                delaysL[i].setSize(int(delayTimes[i] * scaleFactor));
                delaysR[i].setSize(int((delayTimes[i] + 31) * scaleFactor));
            }

            diffusers[0].setSize(int(601 * scaleFactor));
            diffusers[1].setSize(int(467 * scaleFactor));
            diffusers[2].setSize(int(379 * scaleFactor));
            diffusers[3].setSize(int(277 * scaleFactor));
        }

        void setParameters(float decay, float damping) {
            baseFeedback = 0.5f + decay * 0.45f;
            baseDamp = damping * 0.5f;

            for (auto& delay : delaysL) {
                delay.feedback = baseFeedback;
                delay.damp = baseDamp;
            }
            for (auto& delay : delaysR) {
                delay.feedback = baseFeedback;
                delay.damp = baseDamp;
            }
        }

        void setDiffusion(float diffusion) {
            float diff = 0.6f + diffusion * 0.35f;
            for (auto& diffuser : diffusers) {
                diffuser.feedback = juce::jlimit(0.0f, 0.9f, diff);
            }
        }

        void setRoomSize(float size) {
            // Adjust feedback based on room size
            float scale = 0.7f + size * 0.6f;
            for (auto& delay : delaysL) {
                delay.feedback = juce::jlimit(0.0f, 0.98f, baseFeedback * scale);
            }
            for (auto& delay : delaysR) {
                delay.feedback = juce::jlimit(0.0f, 0.98f, baseFeedback * scale);
            }
        }

        void process(float* left, float* right, int numSamples) {
            for (int i = 0; i < numSamples; ++i) {
                float inputL = left[i];
                float inputR = right[i];

                // Pre-diffusion
                for (auto& diff : diffusers) {
                    inputL = diff.process(inputL);
                    inputR = diff.process(inputR);
                }

                float outL = 0.0f, outR = 0.0f;

                // Process delays with cross-coupling for spaciousness
                for (int d = 0; d < numDelays; ++d) {
                    // Mix some of the opposite channel for hall width
                    float mixedL = inputL + inputR * 0.3f;
                    float mixedR = inputR + inputL * 0.3f;

                    outL += delaysL[d].process(mixedL);
                    outR += delaysR[d].process(mixedR);
                }

                // Add some cross-feedback for extra spaciousness
                float crossL = outR * 0.2f;
                float crossR = outL * 0.2f;

                left[i] = (outL + crossL) * 0.008f;
                right[i] = (outR + crossR) * 0.008f;
            }
        }

        void clear() {
            for (auto& delay : delaysL) delay.clear();
            for (auto& delay : delaysR) delay.clear();
            for (auto& diff : diffusers) diff.clear();
        }
    };

    // Reverb instances
    EarlyReflections earlyReflections;
    RoomReverb roomReverb;
    PlateReverb plateReverb;
    HallReverb hallReverb;

    // Pre-delay
    DelayLine preDelayL, preDelayR;

    // Processing buffers
    std::vector<float> tempBufferL, tempBufferR;

    void updateParameters();
};