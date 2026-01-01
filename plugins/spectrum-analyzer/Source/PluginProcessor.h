#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

#include "DSP/FFTProcessor.h"
#include "DSP/LUFSMeter.h"
#include "DSP/KSystemMeter.h"
#include "DSP/TruePeakDetector.h"
#include "DSP/CorrelationMeter.h"
#include "DSP/ChannelRouter.h"

//==============================================================================
class SpectrumAnalyzerProcessor : public juce::AudioProcessor,
                                   public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==========================================================================
    SpectrumAnalyzerProcessor();
    ~SpectrumAnalyzerProcessor() override;

    //==========================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==========================================================================
    // Parameter IDs
    static constexpr const char* PARAM_CHANNEL_MODE = "channelMode";
    static constexpr const char* PARAM_FFT_RESOLUTION = "fftResolution";
    static constexpr const char* PARAM_SMOOTHING = "smoothing";
    static constexpr const char* PARAM_SLOPE = "slope";
    static constexpr const char* PARAM_DECAY_RATE = "decayRate";
    static constexpr const char* PARAM_PEAK_HOLD = "peakHold";
    static constexpr const char* PARAM_PEAK_HOLD_TIME = "peakHoldTime";
    static constexpr const char* PARAM_DISPLAY_MIN = "displayMin";
    static constexpr const char* PARAM_DISPLAY_MAX = "displayMax";
    static constexpr const char* PARAM_K_SYSTEM_TYPE = "kSystemType";

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    //==========================================================================
    // Data access for UI
    FFTProcessor& getFFTProcessor() { return fftProcessor; }
    const FFTProcessor& getFFTProcessor() const { return fftProcessor; }

    float getCorrelation() const { return correlationMeter.getSmoothedCorrelation(); }
    float getTruePeakL() const { return truePeakDetector.getTruePeakDB(0); }
    float getTruePeakR() const { return truePeakDetector.getTruePeakDB(1); }
    bool hasClipped() const { return truePeakDetector.hasClipped(); }

    float getMomentaryLUFS() const { return lufsMeter.getMomentaryLUFS(); }
    float getShortTermLUFS() const { return lufsMeter.getShortTermLUFS(); }
    float getIntegratedLUFS() const { return lufsMeter.getIntegratedLUFS(); }
    float getLoudnessRange() const { return lufsMeter.getLoudnessRange(); }

    float getOutputLevelL() const { return outputLevelL.load(); }
    float getOutputLevelR() const { return outputLevelR.load(); }
    float getRmsLevel() const { return rmsLevel.load(); }

    void resetIntegratedLoudness() { lufsMeter.resetIntegrated(); }
    void resetPeakHold() { truePeakDetector.resetPeakHold(); }

    // Parameter change listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    // DSP components
    FFTProcessor fftProcessor;
    LUFSMeter lufsMeter;
    KSystemMeter kSystemMeter;
    TruePeakDetector truePeakDetector;
    CorrelationMeter correlationMeter;
    ChannelRouter channelRouter;

    // Routing buffers
    std::vector<float> routedL;
    std::vector<float> routedR;

    // Output levels (atomic for thread safety)
    std::atomic<float> outputLevelL{-100.0f};
    std::atomic<float> outputLevelR{-100.0f};
    std::atomic<float> rmsLevel{-100.0f};

    // RMS accumulator
    float rmsAccumL = 0.0f;
    float rmsAccumR = 0.0f;
    float rmsDecay = 0.999f;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerProcessor)
};
