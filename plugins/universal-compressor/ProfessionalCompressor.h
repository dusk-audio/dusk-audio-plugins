/*
  ==============================================================================

    ProfessionalCompressor.h - Advanced features for Universal Compressor

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>

//==============================================================================
// Advanced Sidechain Processing
//==============================================================================
class SidechainEQ
{
public:
    SidechainEQ();

    void prepare(double sampleRate);
    void reset();

    // Parametric EQ bands
    struct Band
    {
        enum Type { HighPass, LowShelf, Bell, HighShelf, LowPass };
        Type type = Bell;
        float frequency = 1000.0f;
        float gain = 0.0f;
        float q = 0.7f;
        bool enabled = true;
    };

    void setBand(int index, const Band& band);
    void setTiltAmount(float db);  // Spectral tilt
    void setSidechainListen(bool listen) { sidechainListen = listen; }

    void process(float* buffer, int numSamples);
    float processSample(float input);

private:
    static constexpr int numBands = 4;
    std::array<Band, numBands> bands;
    std::array<juce::dsp::IIR::Filter<float>, numBands> filters;

    juce::dsp::IIR::Filter<float> tiltLowShelf;
    juce::dsp::IIR::Filter<float> tiltHighShelf;

    bool sidechainListen = false;
    float tiltAmount = 0.0f;
    double sampleRate = 44100.0;

    void updateFilterCoefficients(int bandIndex);
};

//==============================================================================
// Lookahead Processing with Latency Compensation
//==============================================================================
class LookaheadProcessor
{
public:
    LookaheadProcessor();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setLookaheadTime(float ms);
    void setEnabled(bool enable) { enabled = enable; }
    int getLatencyInSamples() const;

    void process(float* left, float* right, int numSamples,
                std::function<void(float*, float*, int)> compressionFunc);

private:
    bool enabled = false;
    float lookaheadMs = 5.0f;
    int lookaheadSamples = 0;

    juce::dsp::DelayLine<float> delayLineL{48000};
    juce::dsp::DelayLine<float> delayLineR{48000};

    std::vector<float> lookaheadBufferL;
    std::vector<float> lookaheadBufferR;

    double sampleRate = 44100.0;

    float findPeakInLookahead(const float* buffer, int numSamples);
};

//==============================================================================
// Parallel Compression Processor
//==============================================================================
class ParallelProcessor
{
public:
    ParallelProcessor();

    void prepare(double sampleRate);
    void reset();

    void setMixAmount(float percent);  // 0-100%
    void setParallelFilters(float hpFreq, float lpFreq);
    void setParallelSaturation(float amount);
    void setNewYorkMode(bool enable);  // Classic NY compression

    void processDryWet(const float* dry, const float* compressed,
                      float* output, int numSamples);

private:
    float mixAmount = 0.0f;
    bool newYorkMode = false;

    // Parallel path filters
    juce::dsp::StateVariableTPTFilter<float> highpassL, highpassR;
    juce::dsp::StateVariableTPTFilter<float> lowpassL, lowpassR;

    // Saturation for parallel path
    float saturationAmount = 0.0f;

    double sampleRate = 44100.0;

    float processSaturation(float input);
};

//==============================================================================
// Transient Shaper
//==============================================================================
class TransientShaper
{
public:
    TransientShaper();

    void prepare(double sampleRate);
    void reset();

    void setAttackAmount(float amount);   // -100 to +100
    void setSustainAmount(float amount);  // -100 to +100
    void setSpeed(float ms);             // Detection speed

    float processSample(float input);

private:
    float attackAmount = 0.0f;
    float sustainAmount = 0.0f;
    float detectionSpeed = 10.0f;

    // Envelope followers
    juce::dsp::BallisticsFilter<float> fastEnvelope;
    juce::dsp::BallisticsFilter<float> slowEnvelope;

    float previousSample = 0.0f;
    double sampleRate = 44100.0;
};

//==============================================================================
// Program-Dependent Release
//==============================================================================
class AdaptiveRelease
{
public:
    AdaptiveRelease();

    void setBaseRelease(float ms);
    void setFastRelease(float ms);
    void setAdaptiveAmount(float amount);  // 0-100%

    float calculateRelease(float inputLevel, float grLevel);

private:
    float baseReleaseMs = 100.0f;
    float fastReleaseMs = 10.0f;
    float adaptiveAmount = 0.5f;

    // Crest factor detection
    juce::dsp::BallisticsFilter<float> peakDetector;
    juce::dsp::BallisticsFilter<float> rmsDetector;

    float getCrestFactor();
};

//==============================================================================
// Compression Curve Editor
//==============================================================================
class CompressionCurve
{
public:
    CompressionCurve();

    // Curve points
    struct Point
    {
        float input;   // dB
        float output;  // dB
    };

    void addPoint(float inputDb, float outputDb);
    void removePoint(int index);
    void updatePoint(int index, float inputDb, float outputDb);
    void clearPoints();

    // Get output for input level
    float getOutputForInput(float inputDb) const;

    // Presets
    void loadSoftKnee();
    void loadHardKnee();
    void loadOptical();
    void loadLimiter();
    void loadExpander();
    void loadGate();

    // Get curve for drawing
    std::vector<Point> getCurvePoints(int resolution = 100) const;

private:
    std::vector<Point> userPoints;

    // Interpolation
    float interpolate(float inputDb) const;
    float cubicInterpolation(float x, const Point& p0, const Point& p1,
                            const Point& p2, const Point& p3) const;
};

//==============================================================================
// Multiband Compressor
//==============================================================================
class MultibandCompressor
{
public:
    MultibandCompressor();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Band setup
    void setNumBands(int num);  // 2-5 bands
    void setCrossoverFrequency(int index, float freq);
    void setCrossoverSlope(int slopeDbPerOct);  // 12, 24, 48

    // Per-band compression
    struct BandSettings
    {
        float threshold = 0.0f;
        float ratio = 4.0f;
        float attack = 1.0f;
        float release = 100.0f;
        float makeupGain = 0.0f;
        bool bypassed = false;
        bool solo = false;
    };

    void setBandSettings(int band, const BandSettings& settings);
    BandSettings getBandSettings(int band) const;

    // Processing
    void process(float* left, float* right, int numSamples);

    // Metering
    float getBandLevel(int band) const;
    float getBandGainReduction(int band) const;

private:
    static constexpr int maxBands = 5;
    int numBands = 3;

    // Crossover filters (Linkwitz-Riley)
    class CrossoverFilter
    {
    public:
        void prepare(double sampleRate);
        void setCutoffFrequency(float freq);
        void setSlope(int dbPerOct);
        void split(float input, float& low, float& high);

    private:
        juce::dsp::LinkwitzRileyFilter<float> lowpass;
        juce::dsp::LinkwitzRileyFilter<float> highpass;
    };

    std::array<CrossoverFilter, maxBands - 1> crossovers;

    // Compressors for each band
    class BandCompressor
    {
    public:
        void prepare(double sampleRate);
        void setSettings(const BandSettings& settings);
        float processSample(float input);
        float getGainReduction() const { return gainReduction; }

    private:
        BandSettings settings;
        float envelope = 0.0f;
        float gainReduction = 0.0f;
        double sampleRate = 44100.0;
    };

    std::array<BandCompressor, maxBands> bandCompressors;

    // Band levels for metering
    std::array<std::atomic<float>, maxBands> bandLevels;
    std::array<std::atomic<float>, maxBands> bandGainReductions;

    double sampleRate = 44100.0;
};

//==============================================================================
// Analysis and Metering
//==============================================================================
class CompressorAnalyzer
{
public:
    CompressorAnalyzer();

    void prepare(double sampleRate);

    // Update with audio
    void analyzeInput(const float* buffer, int numSamples);
    void analyzeOutput(const float* buffer, int numSamples);
    void setGainReduction(float gr);

    // Measurements
    float getInputLUFS() const { return inputLUFS; }
    float getOutputLUFS() const { return outputLUFS; }
    float getDynamicRange() const { return dynamicRange; }
    float getCrestFactor() const { return crestFactor; }
    float getPeakLevel() const { return peakLevel; }
    float getRMSLevel() const { return rmsLevel; }

    // Transfer function
    std::vector<std::pair<float, float>> getTransferFunction() const;

    // Gain reduction history
    std::vector<float> getGainReductionHistory(int numPoints) const;

private:
    // LUFS measurement
    juce::dsp::IIR::Filter<float> k_weightingFilter;
    std::vector<float> lufsBuffer;
    float inputLUFS = -23.0f;
    float outputLUFS = -23.0f;

    // Level detection
    float peakLevel = -60.0f;
    float rmsLevel = -60.0f;
    float dynamicRange = 0.0f;
    float crestFactor = 0.0f;

    // History
    std::vector<float> gainReductionHistory;
    static constexpr int maxHistorySize = 1000;

    double sampleRate = 44100.0;

    void updateLUFS(const float* buffer, int numSamples, float& lufs);
};