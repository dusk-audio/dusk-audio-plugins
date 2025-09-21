/*
  ==============================================================================

    UpdatedCompressorModes.h - Extended compressor modes preserving analog authenticity

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

// IMPORTANT: Original analog modes remain unchanged for hardware accuracy
enum class CompressorMode : int
{
    Opto = 0,      // LA-2A style optical compressor - UNCHANGED
    FET = 1,       // 1176 style FET compressor - UNCHANGED
    VCA = 2,       // DBX 160 style VCA compressor - UNCHANGED
    Bus = 3,       // SSL Bus style compressor - UNCHANGED
    Digital = 4,   // NEW: Modern digital with lookahead
    Multiband = 5  // NEW: Multiband compression
};

//==============================================================================
// ANALOG MODES - Exact hardware emulation (DO NOT MODIFY)
//==============================================================================

// These classes should remain EXACTLY as they were for authentic analog sound
class OptoCompressor  // LA-2A exact emulation
{
public:
    void prepare(double sampleRate);
    float processSample(float input);

    // LA-2A specific parameters
    void setPeakReduction(float amount);  // The main knob on LA-2A
    void setEmphasis(bool hfEmphasis);    // R37 HF emphasis

    // Optical characteristics - fixed behavior matching hardware
private:
    // T4B optical cell emulation
    float attackTime = 10.0f;        // Fixed 10ms attack
    float releaseTime = 60.0f;       // Initial 60ms release
    float releaseTime2 = 1000.0f;    // Secondary 1-5 second release

    // Program-dependent behavior (not adjustable - hardware behavior)
    float opticalCellMemory = 0.0f;
    float compressionRatio = 3.0f;   // Approximately 3:1 average
};

class FETCompressor  // 1176 exact emulation
{
public:
    void prepare(double sampleRate);
    float processSample(float input);

    // 1176 specific controls matching hardware
    void setInputGain(float db);      // -∞ to +24dB
    void setOutputGain(float db);     // -∞ to +24dB
    void setAttack(int setting);      // 1-7 (20μs to 800μs)
    void setRelease(int setting);     // 1-7 (50ms to 1100ms)
    void setRatio(int buttonIndex);   // 4:1, 8:1, 12:1, 20:1, All-buttons

private:
    // FET characteristics - matching Rev A/D/etc
    float distortionAmount = 0.02f;   // FET harmonic distortion
    bool allButtonsMode = false;      // "British Mode" - aggressive limiting

    // Non-linear attack/release curves from hardware
    float getAttackTime(int setting);
    float getReleaseTime(int setting);
};

class VCACompressor  // DBX 160 exact emulation
{
public:
    void prepare(double sampleRate);
    float processSample(float input);

    // DBX 160 specific controls
    void setThreshold(float db);      // -40 to +20dB
    void setCompressionRatio(float ratio);  // 1:1 to ∞:1
    void setOutputGain(float db);

    // OverEasy characteristic (DBX patent)
    void setOverEasy(bool enabled);

private:
    // VCA characteristics
    float kneeWidth = 10.0f;          // Soft knee transition
    bool overEasyMode = true;

    // DBX RMS detection
    float rmsWindowMs = 10.0f;        // Fixed RMS window
};

class BusCompressor  // SSL Bus compressor exact emulation
{
public:
    void prepare(double sampleRate);
    float processSample(float input);

    // SSL Bus specific controls
    void setThreshold(float db);      // +15 to -15dB
    void setRatio(int setting);       // 2:1, 4:1, 10:1
    void setAttack(int setting);      // 0.1, 0.3, 1, 3, 10, 30ms
    void setRelease(int setting);     // 0.1, 0.3, 0.6, 1.2, Auto
    void setMakeup(float db);

    // SSL specific features
    void setSidechainHPF(float freq); // 0 (off) to 200Hz

private:
    // SSL VCA characteristics
    float feedbackCompression = 0.3f;  // Mix of feedback/feedforward
    bool autoRelease = false;

    // Quad VCA emulation for SSL color
    void processQuadVCA(float& sample);
};

//==============================================================================
// NEW DIGITAL MODE - Modern transparent compression with advanced features
//==============================================================================
class DigitalCompressor
{
public:
    DigitalCompressor();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Modern digital parameters
    void setThreshold(float db);
    void setRatio(float ratio);        // 1:1 to ∞:1 with precision
    void setKnee(float db);           // 0 to 20dB soft knee
    void setAttack(float ms);         // 0.01 to 500ms
    void setRelease(float ms);        // 1 to 5000ms
    void setLookahead(float ms);      // 0 to 10ms

    // Advanced features unique to digital
    void setAdaptiveRelease(bool enabled);
    void setTransientEmphasis(float amount);  // -100 to +100%
    void setRMSWindow(float ms);             // Peak to RMS detection
    void setSidechainEQ(int band, float freq, float gain, float q);
    void setParallelMix(float percent);      // Built-in parallel compression

    // Advanced knee shapes
    enum class KneeType
    {
        Hard,
        Soft,
        Vintage,        // Emulates analog knee
        Parametric      // Custom curve
    };
    void setKneeType(KneeType type);

    // Processing
    void process(float* left, float* right, int numSamples);
    float getGainReduction() const { return currentGainReduction; }

private:
    // Lookahead buffer
    juce::dsp::DelayLine<float> lookaheadDelayL{1024};
    juce::dsp::DelayLine<float> lookaheadDelayR{1024};
    std::vector<float> lookaheadBuffer;

    // Sidechain EQ
    struct SidechainBand
    {
        juce::dsp::IIR::Filter<float> filter;
        float frequency = 1000.0f;
        float gain = 0.0f;
        float q = 0.7f;
        bool enabled = false;
    };
    std::array<SidechainBand, 4> sidechainBands;

    // Envelope detection
    juce::dsp::BallisticsFilter<float> envelope;
    float rmsWindow = 0.0f;  // 0 = peak, >0 = RMS

    // Adaptive release
    bool adaptiveRelease = false;
    float fastRelease = 50.0f;
    float slowRelease = 500.0f;

    // Transient shaping
    float transientEmphasis = 0.0f;
    juce::dsp::BallisticsFilter<float> transientDetector;

    // State
    float currentGainReduction = 0.0f;
    double sampleRate = 44100.0;

    // Gain computer
    float computeGain(float inputLevel);
    float applySoftKnee(float x);
};

//==============================================================================
// NEW MULTIBAND MODE - Frequency-selective compression
//==============================================================================
class MultibandCompressor
{
public:
    MultibandCompressor();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Band configuration
    void setNumBands(int num);  // 2-5 bands
    void setCrossoverFrequency(int index, float freq);

    // Per-band compression settings
    struct BandSettings
    {
        // Compression parameters
        float threshold = -12.0f;
        float ratio = 4.0f;
        float attack = 5.0f;
        float release = 100.0f;
        float knee = 2.0f;
        float makeupGain = 0.0f;

        // Band control
        bool enabled = true;
        bool solo = false;
        bool bypassed = false;

        // Advanced
        float range = -60.0f;      // Maximum gain reduction
        float mix = 100.0f;        // Dry/wet per band
        bool sidechainListen = false;
    };

    void setBandSettings(int band, const BandSettings& settings);
    BandSettings getBandSettings(int band) const;

    // Global controls
    void setGlobalMakeup(float db);
    void setGlobalMix(float percent);
    void setCrossoverSlope(int dbPerOctave);  // 12, 24, 48

    // Linear phase option
    void setLinearPhase(bool enabled);

    // Processing
    void process(float* left, float* right, int numSamples);

    // Metering
    float getBandInputLevel(int band) const;
    float getBandOutputLevel(int band) const;
    float getBandGainReduction(int band) const;

private:
    static constexpr int maxBands = 5;
    int numBands = 3;
    bool linearPhase = false;

    // Crossover network
    class CrossoverNetwork
    {
    public:
        void prepare(double sampleRate);
        void setCrossoverFreq(int index, float freq);
        void setSlope(int dbPerOct);
        void split(float input, float* bandOutputs);
        void recombine(const float* bandInputs, float& output);

    private:
        // Linkwitz-Riley filters for phase-coherent crossover
        std::array<juce::dsp::LinkwitzRileyFilter<float>, maxBands - 1> lowpasses;
        std::array<juce::dsp::LinkwitzRileyFilter<float>, maxBands - 1> highpasses;

        // All-pass filters for phase alignment
        std::array<juce::dsp::IIR::Filter<float>, maxBands> allpasses;
    };

    CrossoverNetwork crossoverL, crossoverR;

    // Per-band compressors
    class BandCompressor
    {
    public:
        void prepare(double sampleRate);
        void setSettings(const BandSettings& settings);
        float processSample(float input);

        float getInputLevel() const { return inputLevel; }
        float getOutputLevel() const { return outputLevel; }
        float getGainReduction() const { return gainReduction; }

    private:
        BandSettings settings;
        juce::dsp::BallisticsFilter<float> envelope;
        float inputLevel = 0.0f;
        float outputLevel = 0.0f;
        float gainReduction = 0.0f;
        double sampleRate = 44100.0;
    };

    std::array<BandCompressor, maxBands> bandCompressors;

    // Band buffers
    std::array<std::vector<float>, maxBands> bandBuffersL;
    std::array<std::vector<float>, maxBands> bandBuffersR;

    // Global
    float globalMakeup = 0.0f;
    float globalMix = 100.0f;
    double sampleRate = 44100.0;

    // Linear phase processing
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<std::complex<float>> fftBuffer;
    void processLinearPhase(float* left, float* right, int numSamples);
};

//==============================================================================
// Sidechain processing enhancement (works with all modes)
//==============================================================================
class UniversalSidechainProcessor
{
public:
    // External sidechain input support
    void setExternalSidechain(const float* left, const float* right, int numSamples);

    // Advanced sidechain filtering
    void setHighpass(float freq);
    void setLowpass(float freq);
    void setTilt(float db);
    void setBandpassFocus(float centerFreq, float q);

    // Sidechain listen mode
    void setSidechainListen(bool enabled);

    // Process sidechain signal
    void processSidechain(float* left, float* right, int numSamples);

private:
    juce::dsp::StateVariableTPTFilter<float> highpassL, highpassR;
    juce::dsp::StateVariableTPTFilter<float> lowpassL, lowpassR;
    juce::dsp::IIR::Filter<float> tiltFilterL, tiltFilterR;
    juce::dsp::IIR::Filter<float> bandpassL, bandpassR;

    bool useExternalSidechain = false;
    bool sidechainListen = false;
};