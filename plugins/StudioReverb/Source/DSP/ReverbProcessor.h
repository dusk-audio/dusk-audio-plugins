#pragma once

#include <JuceHeader.h>

/**
 * Abstract base class for all reverb processors
 * Implements the common interface for Dragonfly reverb algorithms
 */
class ReverbProcessor
{
public:
    ReverbProcessor() = default;
    virtual ~ReverbProcessor() = default;

    // Prepare the processor for playback
    virtual void prepare(double sampleRate, int blockSize) = 0;

    // Reset the processor state
    virtual void reset() = 0;

    // Process audio
    virtual void process(float* leftChannel, float* rightChannel, int numSamples) = 0;

    // Get the reverb tail length in seconds
    virtual double getTailLength() const = 0;

    // Common parameter setters
    virtual void setDecay(float seconds) { decay = seconds; }
    virtual void setPreDelay(float milliseconds) { preDelay = milliseconds; }
    virtual void setDamping(float amount) { damping = amount; }
    virtual void setDiffusion(float amount) { diffusion = amount; }
    virtual void setRoomSize(float size) { roomSize = size; }
    virtual void setModulation(float amount) { modulation = amount; }

    // Mix controls
    virtual void setEarlyMix(float mix) { earlyMix = mix; }
    virtual void setLateMix(float mix) { lateMix = mix; }

    // Filter controls
    virtual void setLowCut(float frequency) { lowCutFreq = frequency; }
    virtual void setHighCut(float frequency) { highCutFreq = frequency; }

    // Get parameter visibility flags for UI
    struct ParameterVisibility
    {
        bool showDecay = true;
        bool showPreDelay = true;
        bool showDamping = true;
        bool showDiffusion = true;
        bool showRoomSize = true;
        bool showModulation = false;
        bool showEarlyMix = false;
        bool showLateMix = false;
        bool showLowCut = true;
        bool showHighCut = true;
    };

    virtual ParameterVisibility getParameterVisibility() const = 0;

    // Get reverb type name for display
    virtual const char* getTypeName() const = 0;

protected:
    // Common parameters
    float decay = 2.0f;
    float preDelay = 10.0f;
    float damping = 0.5f;
    float diffusion = 0.7f;
    float roomSize = 0.5f;
    float modulation = 0.2f;
    float earlyMix = 0.3f;
    float lateMix = 0.7f;
    float lowCutFreq = 20.0f;
    float highCutFreq = 16000.0f;

    // Sample rate
    double sampleRate = 44100.0;
    int blockSize = 512;

    // Helper functions
    static inline float clamp(float value, float min, float max)
    {
        return std::max(min, std::min(max, value));
    }

    static inline float linearInterp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }
};