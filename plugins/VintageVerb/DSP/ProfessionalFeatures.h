/*
  ==============================================================================

    ProfessionalFeatures.h - Advanced features for professional reverb

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>

class ProfessionalFeatures
{
public:
    //==============================================================================
    // Reverb Gate with envelope shaping
    //==============================================================================
    class ReverbGate
    {
    public:
        enum class Mode
        {
            Off,
            Gate,           // Traditional gate
            Ducking,        // Duck reverb with input
            Expander,       // Expand reverb tail
            Envelope        // Follow input envelope
        };

        void prepare(double sampleRate);
        void setMode(Mode newMode);
        void setThreshold(float dB);
        void setAttack(float ms);
        void setHold(float ms);
        void setRelease(float ms);
        void setRange(float dB);

        void process(float* reverb, const float* input, int numSamples);

    private:
        Mode mode = Mode::Off;
        double sampleRate = 44100.0;

        float threshold = -40.0f;
        float attack = 1.0f;
        float hold = 10.0f;
        float release = 100.0f;
        float range = -60.0f;

        float currentGain = 0.0f;
        float targetGain = 0.0f;
        int holdCounter = 0;

        juce::dsp::BallisticsFilter<float> envelope;
    };

    //==============================================================================
    // Reverse reverb effect
    //==============================================================================
    class ReverseReverb
    {
    public:
        void prepare(double sampleRate, int maxBlockSize);
        void setAmount(float amount);  // 0-1 blend
        void setLength(float seconds);
        void setPreDelay(float ms);

        void process(float* left, float* right, int numSamples);

    private:
        static constexpr int MAX_REVERSE_SAMPLES = 192000;  // 4 seconds at 48kHz

        std::vector<float> reverseBufferL;
        std::vector<float> reverseBufferR;
        int bufferSize = 0;
        int writePos = 0;
        int readPos = 0;

        float amount = 0.0f;
        float length = 1.0f;
        float preDelay = 0.0f;

        double sampleRate = 44100.0;

        // Envelope for smooth reverse
        std::vector<float> envelope;
        void generateEnvelope();
    };

    //==============================================================================
    // Infinite/Freeze mode with spectral hold
    //==============================================================================
    class SpectralFreeze
    {
    public:
        void prepare(double sampleRate, int fftSize);
        void setFreeze(bool shouldFreeze);
        void setSpectralSmearing(float amount);  // Blur the spectrum over time
        void setDecay(float rate);  // Slow decay even in freeze mode

        void process(float* left, float* right, int numSamples);

    private:
        juce::dsp::FFT fft;
        int fftSize = 2048;
        double sampleRate = 44100.0;

        std::vector<std::complex<float>> spectrumL;
        std::vector<std::complex<float>> spectrumR;
        std::vector<std::complex<float>> frozenSpectrumL;
        std::vector<std::complex<float>> frozenSpectrumR;

        std::vector<float> windowFunction;
        std::vector<float> overlapBufferL;
        std::vector<float> overlapBufferR;

        bool frozen = false;
        float smearing = 0.0f;
        float decay = 0.0f;

        void createWindow();
    };

    //==============================================================================
    // Convolution reverb blend (for realistic spaces)
    //==============================================================================
    class ConvolutionBlend
    {
    public:
        ConvolutionBlend();

        void prepare(double sampleRate, int maxBlockSize);
        void loadImpulseResponse(const float* ir, int irLength);
        void setBlendAmount(float amount);  // Blend with algorithmic reverb

        void process(const float* input, float* output, int numSamples);

    private:
        juce::dsp::Convolution convolution;
        float blendAmount = 0.0f;
        bool irLoaded = false;
    };

    //==============================================================================
    // Modulation matrix for complex routing
    //==============================================================================
    class ModulationMatrix
    {
    public:
        enum class Source
        {
            LFO1, LFO2, LFO3, LFO4,
            EnvelopeFollower,
            Random,
            InputLevel,
            MidiCC
        };

        enum class Destination
        {
            Size, Diffusion, Damping,
            PreDelay, Width, Mix,
            ModDepth, ModRate,
            LowDecay, MidDecay, HighDecay,
            InputFilter, OutputFilter
        };

        void addConnection(Source src, Destination dest, float amount);
        void removeConnection(Source src, Destination dest);
        float getModulation(Destination dest);
        void update();

    private:
        struct Connection
        {
            Source source;
            Destination destination;
            float amount;
            float currentValue;
        };

        std::vector<Connection> connections;
        std::array<float, 8> sourceValues;
        std::array<float, 13> destinationValues;

        // LFOs for modulation
        std::array<juce::dsp::Oscillator<float>, 4> lfos;
        juce::Random randomGen;
    };

    //==============================================================================
    // Sidechain input for ducking/compression
    //==============================================================================
    class SidechainProcessor
    {
    public:
        void prepare(double sampleRate);
        void setSidechainEnabled(bool enabled);
        void setDuckingAmount(float amount);
        void setAttack(float ms);
        void setRelease(float ms);
        void setRatio(float ratio);

        float processSidechain(float reverbSample, float sidechainInput);

    private:
        bool enabled = false;
        float duckingAmount = 0.0f;
        float attack = 1.0f;
        float release = 100.0f;
        float ratio = 4.0f;

        float currentGain = 1.0f;
        juce::dsp::BallisticsFilter<float> detector;
        double sampleRate = 44100.0;
    };

    //==============================================================================
    // Auto-gain compensation
    //==============================================================================
    class AutoGainCompensation
    {
    public:
        void prepare(double sampleRate);
        void setEnabled(bool shouldCompensate);
        void setTargetLevel(float dB);
        void setResponseTime(float seconds);

        void analyze(const float* input, int numSamples);
        float getCompensationGain() const { return compensationGain; }

    private:
        bool enabled = false;
        float targetLevel = -12.0f;
        float responseTime = 1.0f;

        float inputRMS = 0.0f;
        float outputRMS = 0.0f;
        float compensationGain = 1.0f;

        juce::dsp::BallisticsFilter<float> rmsDetector;
        double sampleRate = 44100.0;
    };

    //==============================================================================
    // Vintage analog modeling
    //==============================================================================
    class AnalogModeling
    {
    public:
        void prepare(double sampleRate);

        void setTapeWow(float amount);     // Tape speed variation
        void setTapeFlutter(float amount); // High-frequency variation
        void setNoise(float amount);       // Analog noise floor
        void setSaturation(float amount);  // Tape/tube saturation
        void setCrosstalk(float amount);   // L/R channel bleed

        void process(float& left, float& right);

    private:
        double sampleRate = 44100.0;

        // Wow and flutter
        juce::dsp::Oscillator<float> wowOsc;
        juce::dsp::Oscillator<float> flutterOsc;
        float wowAmount = 0.0f;
        float flutterAmount = 0.0f;

        // Noise generation
        juce::Random noiseGen;
        float noiseAmount = 0.0f;
        juce::dsp::StateVariableTPTFilter<float> noiseFilter;

        // Saturation
        float saturationAmount = 0.0f;

        // Crosstalk
        float crosstalkAmount = 0.0f;
        juce::dsp::IIR::Filter<float> crosstalkFilterL, crosstalkFilterR;

        float saturate(float input);
    };
};