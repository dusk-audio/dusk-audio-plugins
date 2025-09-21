/*
  ==============================================================================

    VisualFeedback.h - Professional visual feedback and analysis components

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <complex>

//==============================================================================
// Real-time Spectrum Analyzer
//==============================================================================
class SpectrumAnalyzer : public juce::Component,
                         public juce::Timer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer();

    void pushSample(float sample);
    void pushBuffer(const float* data, int numSamples);

    // Display settings
    void setFrequencyRange(float minHz, float maxHz);
    void setDecibelRange(float mindB, float maxdB);
    void setFFTSize(int size);
    void setAveraging(float amount);  // 0 = no averaging, 1 = infinite
    void setShowPeakHold(bool show) { showPeakHold = show; }
    void setShowGrid(bool show) { showGrid = show; }

    // Visual modes
    enum class DisplayMode
    {
        Line,
        FilledCurve,
        Bars,
        Waterfall,
        Spectrogram
    };

    void setDisplayMode(DisplayMode mode) { displayMode = mode; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    // FFT
    static constexpr int maxFFTOrder = 13;  // 8192 samples
    int fftOrder = 11;  // 2048 samples default
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    // Buffers
    std::array<float, 1 << maxFFTOrder> fftData;
    std::array<float, 1 << maxFFTOrder> fifo;
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    // Spectrum data
    std::array<float, 1 << (maxFFTOrder - 1)> spectrum;
    std::array<float, 1 << (maxFFTOrder - 1)> smoothedSpectrum;
    std::array<float, 1 << (maxFFTOrder - 1)> peakHold;

    // Waterfall/Spectrogram history
    std::vector<std::vector<float>> spectrogramData;
    static constexpr int maxSpectrogramHistory = 100;

    // Display settings
    DisplayMode displayMode = DisplayMode::FilledCurve;
    float minFrequency = 20.0f;
    float maxFrequency = 20000.0f;
    float mindB = -60.0f;
    float maxdB = 0.0f;
    float averaging = 0.5f;
    bool showPeakHold = true;
    bool showGrid = true;

    // Drawing
    juce::Path spectrumPath;
    juce::ColourGradient gradient;

    void drawFrame(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawSpectrum(juce::Graphics& g);
    void drawWaterfall(juce::Graphics& g);
    void processFFT();

    float mapFrequencyToX(float freq) const;
    float mapMagnitudeToY(float mag) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};

//==============================================================================
// Reverb Tail Visualizer
//==============================================================================
class ReverbTailVisualizer : public juce::Component,
                             public juce::Timer
{
public:
    ReverbTailVisualizer();

    void pushReverbSample(float left, float right);
    void setDecayTime(float seconds);
    void setEnergyDistribution(const std::array<float, 8>& bands);

    // Display modes
    enum class ViewMode
    {
        Waveform,      // Traditional waveform
        EnergyDecay,   // Energy over time
        FrequencyDecay, // Frequency bands over time
        Polar,         // Circular/polar display
        ThreeD         // 3D waterfall
    };

    void setViewMode(ViewMode mode) { viewMode = mode; }
    void setColorScheme(int scheme) { colorScheme = scheme; }

    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    ViewMode viewMode = ViewMode::EnergyDecay;
    int colorScheme = 0;

    // Tail analysis
    std::vector<float> tailBuffer;
    std::vector<float> energyEnvelope;
    std::array<std::vector<float>, 8> frequencyBands;
    static constexpr int maxTailSamples = 192000;  // 4 seconds at 48kHz

    float currentDecayTime = 2.0f;
    float currentEnergy = 0.0f;
    float peakEnergy = 0.0f;

    // Drawing
    void drawWaveform(juce::Graphics& g);
    void drawEnergyDecay(juce::Graphics& g);
    void drawFrequencyDecay(juce::Graphics& g);
    void drawPolarView(juce::Graphics& g);
    void draw3DView(juce::Graphics& g);

    // Analysis
    void analyzeDecay();
    float calculateRT60() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbTailVisualizer)
};

//==============================================================================
// 3D Space Display
//==============================================================================
class Space3DDisplay : public juce::Component,
                      public juce::OpenGLRenderer,
                      public juce::Timer
{
public:
    Space3DDisplay();
    ~Space3DDisplay();

    // Room parameters
    void setRoomSize(float width, float height, float depth);
    void setListenerPosition(float x, float y, float z);
    void setSourcePosition(float x, float y, float z);
    void setEarlyReflections(const std::vector<juce::Point3D<float>>& reflections);

    // Visualization
    void setShowRoomBoundaries(bool show) { showBoundaries = show; }
    void setShowReflections(bool show) { showReflections = show; }
    void setShowDiffuseField(bool show) { showDiffuseField = show; }
    void setRotation(float azimuth, float elevation);
    void setZoom(float zoom);

    // OpenGL
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void timerCallback() override;

private:
    juce::OpenGLContext openGLContext;

    // Room geometry
    juce::Vector3D<float> roomDimensions{10.0f, 3.0f, 15.0f};
    juce::Point3D<float> listenerPos{5.0f, 1.5f, 10.0f};
    juce::Point3D<float> sourcePos{5.0f, 1.5f, 5.0f};
    std::vector<juce::Point3D<float>> earlyReflectionPoints;

    // Display settings
    bool showBoundaries = true;
    bool showReflections = true;
    bool showDiffuseField = false;

    // Camera
    float cameraAzimuth = 45.0f;
    float cameraElevation = 30.0f;
    float cameraZoom = 1.0f;
    juce::Point<float> lastMousePos;

    // OpenGL resources
    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;

    void createShaders();
    void drawRoom();
    void drawListener();
    void drawSource();
    void drawReflections();
    void drawDiffuseField();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Space3DDisplay)
};

//==============================================================================
// Level Meters with Peak Hold
//==============================================================================
class StereoLevelMeter : public juce::Component,
                         public juce::Timer
{
public:
    StereoLevelMeter();

    void setLevel(int channel, float level);
    void setMode(bool showPeak, bool showRMS, bool showLUFS);
    void setRange(float mindB, float maxdB);
    void setDecay(float decayRate);
    void setPeakHoldTime(float seconds);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    std::array<float, 2> currentLevels{0.0f, 0.0f};
    std::array<float, 2> peakLevels{0.0f, 0.0f};
    std::array<float, 2> rmsLevels{0.0f, 0.0f};
    std::array<float, 2> peakHolds{-100.0f, -100.0f};
    std::array<int, 2> peakHoldCounters{0, 0};

    float mindB = -60.0f;
    float maxdB = 0.0f;
    float decayRate = 0.95f;
    int peakHoldSamples = 44100 * 2;  // 2 seconds

    bool showPeak = true;
    bool showRMS = true;
    bool showLUFS = false;

    // LUFS measurement
    juce::dsp::IIR::Filter<float> k_filter[2];
    std::vector<float> lufsBuffer;

    void drawMeter(juce::Graphics& g, int channel, juce::Rectangle<float> bounds);
    float dBToNormalized(float dB) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoLevelMeter)
};

//==============================================================================
// Modulation Visualizer
//==============================================================================
class ModulationVisualizer : public juce::Component,
                            public juce::Timer
{
public:
    ModulationVisualizer();

    // Add modulation sources to visualize
    void addLFO(const juce::String& name, float frequency, float phase);
    void addEnvelope(const juce::String& name, float attack, float decay, float sustain, float release);
    void updateModulation(const juce::String& name, float value);

    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    struct ModSource
    {
        juce::String name;
        std::vector<float> history;
        float currentValue = 0.0f;
        juce::Colour color;
    };

    std::vector<ModSource> sources;
    static constexpr int historySize = 128;

    void drawModulation(juce::Graphics& g, const ModSource& source, juce::Rectangle<float> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationVisualizer)
};

//==============================================================================
// Parameter Animation Display
//==============================================================================
class ParameterAnimationDisplay : public juce::Component
{
public:
    ParameterAnimationDisplay(juce::AudioProcessorValueTreeState& apvts);

    void setParameter(const juce::String& paramID);
    void startRecording();
    void stopRecording();
    void startPlayback();
    void stopPlayback();

    void paint(juce::Graphics& g) override;

private:
    juce::AudioProcessorValueTreeState& parameters;
    juce::String currentParam;

    struct AnimationPoint
    {
        float time;
        float value;
    };

    std::vector<AnimationPoint> animationData;
    bool recording = false;
    bool playing = false;
    float currentTime = 0.0f;

    void drawCurve(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterAnimationDisplay)
};