/*
  ==============================================================================

    SpecialAlgorithms.h - Special reverb algorithms (Spring, Gated, Nonlinear)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <random>

//==============================================================================
// Spring Reverb Emulation
//==============================================================================
class SpringReverbEmulation
{
public:
    SpringReverbEmulation();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Spring parameters
    void setSpringCount(int count);  // 1-6 springs
    void setSpringTension(float tension);  // 0-1, affects decay
    void setSpringDamping(float damping);  // High frequency loss
    void setDripAmount(float amount);  // Classic spring "drip" sound
    void setChirpAmount(float amount);  // Metallic chirping
    void setBoing(float amount);  // Spring oscillation

    void process(float* left, float* right, int numSamples);

private:
    // Spring model based on dispersive delay lines
    class SpringModel
    {
    public:
        void prepare(double sampleRate);
        float process(float input);

        void setLength(float ms);
        void setTension(float tension);
        void setDamping(float damping);
        void setDispersion(float amount);

    private:
        // Dispersive allpass chain for frequency-dependent delay
        static constexpr int NUM_ALLPASS = 12;
        std::array<juce::dsp::IIR::Filter<float>, NUM_ALLPASS> allpassFilters;

        // Main delay line
        juce::dsp::DelayLine<float> delayLine{48000};
        float delayTime = 30.0f;

        // Damping filter
        juce::dsp::IIR::Filter<float> dampingFilter;

        // Dispersion parameters
        float dispersionAmount = 0.7f;
        float tensionFactor = 0.5f;

        double sampleRate = 44100.0;

        void updateAllpassCoefficients();
    };

    // Multiple springs for complexity
    static constexpr int MAX_SPRINGS = 6;
    std::array<SpringModel, MAX_SPRINGS> springs;
    int activeSpringCount = 3;

    // Drip effect (transient enhancement)
    class DripEffect
    {
    public:
        void prepare(double sampleRate);
        float process(float input, float amount);

    private:
        juce::dsp::DelayLine<float> dripDelay{1024};
        juce::dsp::IIR::Filter<float> resonantFilter;
        float lastSample = 0.0f;
        juce::Random random;
    };

    DripEffect dripProcessor;

    // Chirp generator (metallic resonances)
    class ChirpGenerator
    {
    public:
        void prepare(double sampleRate);
        float process(float input, float amount);

    private:
        std::array<juce::dsp::IIR::Filter<float>, 4> combFilters;
        float chirpFrequencies[4] = {1000.0f, 1500.0f, 2200.0f, 3100.0f};
        double sampleRate = 44100.0;
    };

    ChirpGenerator chirpGen;

    // Boing effect (spring oscillation)
    juce::dsp::Oscillator<float> boingOscillator;
    float boingAmount = 0.0f;

    // Parameters
    float springTension = 0.5f;
    float springDamping = 0.3f;
    float dripAmount = 0.2f;
    float chirpAmount = 0.1f;

    double sampleRate = 44100.0;

    // Mixing matrix for springs
    std::array<std::array<float, MAX_SPRINGS>, 2> springMixMatrix;
    void initializeMixMatrix();
};

//==============================================================================
// Gated Reverb (80s Style)
//==============================================================================
class GatedReverb
{
public:
    GatedReverb();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Gate parameters
    void setGateThreshold(float dB);
    void setGateTime(float ms);  // How long gate stays open
    void setGateShape(float shape);  // 0 = hard gate, 1 = soft fade
    void setPreDelay(float ms);
    void setDensity(float density);  // Initial reflection density

    // Reverb character
    void setRoomSize(float size);
    void setDiffusion(float diffusion);
    void setBrightness(float brightness);

    // Envelope shaping
    void setAttackTime(float ms);
    void setHoldTime(float ms);
    void setReleaseTime(float ms);

    // Special modes
    enum class GateMode
    {
        Classic,      // Phil Collins drum sound
        Reverse,      // Reverse envelope
        Triggered,    // Trigger from input transients
        Rhythmic,     // Synced to tempo
        Nonlinear     // Nonlinear envelope
    };

    void setGateMode(GateMode mode) { gateMode = mode; }

    void process(float* left, float* right, int numSamples);

private:
    GateMode gateMode = GateMode::Classic;

    // Dense early reflections network
    class DenseReflectionNetwork
    {
    public:
        void prepare(double sampleRate);
        void process(float inputL, float inputR, float& outputL, float& outputR);
        void setDensity(float density);
        void setDiffusion(float diffusion);

    private:
        static constexpr int NUM_DELAYS = 32;
        std::array<juce::dsp::DelayLine<float>, NUM_DELAYS> delays;
        std::array<float, NUM_DELAYS> delayTimes;
        std::array<float, NUM_DELAYS> delayGains;
        std::array<juce::dsp::IIR::Filter<float>, NUM_DELAYS> diffusionFilters;

        float density = 0.8f;
        float diffusion = 0.7f;
        double sampleRate = 44100.0;

        void updateDelayTimes();
    };

    DenseReflectionNetwork reflectionNetwork;

    // Gate envelope
    class GateEnvelope
    {
    public:
        void prepare(double sampleRate);
        void trigger();
        float getNextValue();

        void setAttack(float ms);
        void setHold(float ms);
        void setRelease(float ms);
        void setShape(float shape);

        bool isActive() const { return state != State::Idle; }

    private:
        enum class State { Idle, Attack, Hold, Release };
        State state = State::Idle;

        float attackTime = 1.0f;
        float holdTime = 100.0f;
        float releaseTime = 50.0f;
        float shapeParameter = 0.5f;

        float currentValue = 0.0f;
        float attackIncrement = 0.001f;
        float releaseIncrement = 0.001f;
        int holdCounter = 0;

        double sampleRate = 44100.0;

        float applyShape(float value);
    };

    GateEnvelope gateEnvelope;

    // Transient detector for triggering
    class TransientDetector
    {
    public:
        void prepare(double sampleRate);
        bool detectTransient(float input);
        void setThreshold(float dB);
        void setSensitivity(float sensitivity);

    private:
        float threshold = -20.0f;
        float sensitivity = 0.5f;
        float envelope = 0.0f;
        float prevEnvelope = 0.0f;
        juce::dsp::BallisticsFilter<float> envelopeFollower;
    };

    TransientDetector transientDetector;

    // Reverse buffer for reverse gate mode
    std::vector<float> reverseBufferL, reverseBufferR;
    int reverseBufferSize = 0;
    int reverseWritePos = 0;

    // Parameters
    float gateThreshold = -20.0f;
    float gateTime = 200.0f;
    float gateShape = 0.5f;
    float preDelay = 0.0f;
    float roomSize = 0.7f;
    float brightness = 0.5f;

    double sampleRate = 44100.0;

    // Brightness filter
    juce::dsp::StateVariableTPTFilter<float> brightnessFilterL, brightnessFilterR;
};

//==============================================================================
// Nonlinear Reverb
//==============================================================================
class NonlinearReverb
{
public:
    NonlinearReverb();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Nonlinear characteristics
    void setDistortionAmount(float amount);  // Nonlinear waveshaping
    void setFeedbackDistortion(float amount);  // Distortion in feedback path
    void setCompression(float amount);  // Dynamic compression
    void setExpansion(float amount);  // Dynamic expansion
    void setModulationChaos(float amount);  // Chaotic modulation

    // Reverb parameters
    void setSize(float size);
    void setDecay(float decay);
    void setDiffusion(float diffusion);

    // Special effects
    void setGranularDensity(float density);  // Granular processing
    void setPitchShift(float semitones);  // Pitch shifting in tail
    void setSpectralFreeze(float amount);  // Freeze spectral content
    void setBitCrush(float bits);  // Bit reduction

    void process(float* left, float* right, int numSamples);

private:
    // Nonlinear waveshaping
    class WaveShaper
    {
    public:
        enum class Type
        {
            Tanh,
            Cubic,
            Foldback,
            Asymmetric,
            Chebyshev
        };

        void setType(Type type) { shaperType = type; }
        void setAmount(float amount);
        float process(float input);

    private:
        Type shaperType = Type::Tanh;
        float amount = 0.0f;

        float processTanh(float input);
        float processCubic(float input);
        float processFoldback(float input);
        float processAsymmetric(float input);
        float processChebyshev(float input);
    };

    WaveShaper inputShaper, feedbackShaper;

    // Chaotic modulation system
    class ChaoticModulation
    {
    public:
        void prepare(double sampleRate);
        float getNextValue();
        void setChaos(float amount);

    private:
        // Lorenz attractor
        float x = 0.1f, y = 0.0f, z = 0.0f;
        float sigma = 10.0f;
        float rho = 28.0f;
        float beta = 8.0f / 3.0f;
        float dt = 0.01f;
        float chaosAmount = 0.0f;

        double sampleRate = 44100.0;

        void iterateLorenz();
    };

    ChaoticModulation chaoticMod;

    // Granular processor for density
    class GranularProcessor
    {
    public:
        void prepare(double sampleRate);
        void setDensity(float density);
        void setGrainSize(float ms);
        void setPitchVariation(float semitones);

        float process(float input);

    private:
        static constexpr int MAX_GRAINS = 32;

        struct Grain
        {
            std::vector<float> buffer;
            int readPos = 0;
            int writePos = 0;
            float pitch = 1.0f;
            float amplitude = 0.0f;
            bool active = false;
        };

        std::array<Grain, MAX_GRAINS> grains;
        int nextGrain = 0;
        float density = 0.5f;
        float grainSize = 50.0f;
        float pitchVar = 0.0f;

        juce::Random random;
        double sampleRate = 44100.0;

        void triggerGrain(float input);
    };

    GranularProcessor granular;

    // Spectral processor
    class SpectralProcessor
    {
    public:
        void prepare(double sampleRate, int fftSize);
        void setFreeze(float amount);
        void setSmear(float amount);
        void setBinShift(int shift);

        void process(float* data, int numSamples);

    private:
        juce::dsp::FFT fft{10};  // 1024 point FFT
        std::vector<std::complex<float>> spectrum;
        std::vector<std::complex<float>> frozenSpectrum;
        std::vector<float> window;

        float freezeAmount = 0.0f;
        float smearAmount = 0.0f;
        int binShift = 0;

        void createWindow();
    };

    SpectralProcessor spectral;

    // Bit crusher
    class BitCrusher
    {
    public:
        void setBitDepth(float bits);
        void setSampleRateReduction(float factor);

        float process(float input);

    private:
        float bitDepth = 16.0f;
        float sampleRateReduction = 1.0f;
        float heldSample = 0.0f;
        int sampleCounter = 0;
    };

    BitCrusher bitCrusher;

    // Main reverb network (simplified FDN)
    static constexpr int NUM_DELAYS = 8;
    std::array<juce::dsp::DelayLine<float>, NUM_DELAYS> delays;
    std::array<float, NUM_DELAYS> delayTimes;
    std::array<float, NUM_DELAYS> feedbackGains;

    // Compressor/Expander
    juce::dsp::Compressor<float> compressor;

    // Parameters
    float distortionAmount = 0.0f;
    float feedbackDistortion = 0.0f;
    float compressionAmount = 0.0f;
    float expansionAmount = 0.0f;
    float modulationChaos = 0.0f;
    float size = 0.5f;
    float decay = 0.5f;
    float diffusion = 0.5f;

    double sampleRate = 44100.0;

    void initializeDelays();
};

//==============================================================================
// Algorithm Selector and Manager
//==============================================================================
class AlgorithmManager
{
public:
    enum class Algorithm
    {
        StandardFDN,      // High-quality FDN
        Spring,           // Spring reverb emulation
        Gated,            // 80s gated reverb
        Nonlinear,        // Experimental nonlinear
        Shimmer,          // Pitch-shifted reverb
        Vintage,          // Vintage digital (EMT, AMS)
        Plate,            // Plate reverb emulation
        Hall,             // Concert hall
        Chamber,          // Chamber reverb
        Room,             // Natural room
        Cathedral,        // Large space
        Ambient           // Ambient/experimental
    };

    AlgorithmManager();

    void prepare(double sampleRate, int maxBlockSize);
    void setAlgorithm(Algorithm algo);
    Algorithm getCurrentAlgorithm() const { return currentAlgorithm; }

    void process(float* left, float* right, int numSamples);

    // Get algorithm-specific parameters
    juce::StringArray getParameterNames() const;
    float getParameter(const juce::String& name) const;
    void setParameter(const juce::String& name, float value);

private:
    Algorithm currentAlgorithm = Algorithm::StandardFDN;

    // Algorithm instances
    std::unique_ptr<SpringReverbEmulation> springReverb;
    std::unique_ptr<GatedReverb> gatedReverb;
    std::unique_ptr<NonlinearReverb> nonlinearReverb;

    // Algorithm-specific parameter maps
    std::map<juce::String, float> algorithmParameters;

    void initializeAlgorithms(double sampleRate, int maxBlockSize);
};