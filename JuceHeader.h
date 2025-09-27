// Minimal JUCE mock for testing
#pragma once

#include <cmath>
#include <cstring>
#include <vector>
#include <iostream>
#include <algorithm>
#include <atomic>
#include <memory>

namespace juce {
    template<typename T>
    T jmin(T a, T b) { return a < b ? a : b; }

    template<typename T>
    T jmax(T a, T b) { return a > b ? a : b; }

    template<typename T>
    T jlimit(T min, T val, T max) {
        if (val < min) return min;
        if (val > max) return max;
        return val;
    }

    inline void ignoreUnused(...) {}

    class String {
        std::string s;
    public:
        String() {}
        String(const char* str) : s(str) {}
        String(int val) : s(std::to_string(val)) {}
        String(float val, int) : s(std::to_string(val)) {}
        const std::string& toStdString() const { return s; }
        bool isEmpty() const { return s.empty(); }
    };

    class SpinLock {
    public:
        class ScopedLockType {
        public:
            ScopedLockType(const SpinLock&) {}
        };
    };

    class ScopedNoDenormals {
    public:
        ScopedNoDenormals() {}
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

        void setSize(int nChannels, int nSamples, bool = false, bool = false, bool = false) {
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

        void clear(int channel, int start, int num) {
            std::fill(channels[channel].begin() + start,
                     channels[channel].begin() + start + num, 0);
        }

        void copyFrom(int destChannel, int destStart, const AudioBuffer& source,
                     int sourceChannel, int sourceStart, int numToCopy) {
            std::memcpy(&channels[destChannel][destStart],
                       &source.channels[sourceChannel][sourceStart],
                       numToCopy * sizeof(T));
        }

        void copyFrom(int destChannel, int destStart, const T* source, int numToCopy) {
            std::memcpy(&channels[destChannel][destStart], source, numToCopy * sizeof(T));
        }

        void addFrom(int destChannel, int destStart, const AudioBuffer& source,
                    int sourceChannel, int sourceStart, int numToCopy, T gain = 1) {
            for (int i = 0; i < numToCopy; ++i) {
                channels[destChannel][destStart + i] +=
                    source.channels[sourceChannel][sourceStart + i] * gain;
            }
        }

        void applyGain(T gain) {
            for (auto& ch : channels) {
                for (auto& sample : ch) {
                    sample *= gain;
                }
            }
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

        void makeCopyOf(const AudioBuffer& other) {
            setSize(other.numChannels, other.numSamples);
            channels = other.channels;
        }
    };

    class MidiBuffer {};

    class AudioParameterFloat {
    public:
        float get() const { return value; }
        void setValueNotifyingHost(float v) { value = v; }
        float convertTo0to1(float v) const { return v; }
    private:
        float value = 0;
    };

    class AudioParameterChoice {
    public:
        int getIndex() const { return index; }
        String getCurrentChoiceName() const { return "Test"; }
        void setValueNotifyingHost(float v) { index = (int)(v * 3); }
    private:
        int index = 0;
    };

    using StringArray = std::vector<String>;
}

#define jassert(x)
#define jassertfalse
#define DBG(x) std::cout << x << std::endl
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)