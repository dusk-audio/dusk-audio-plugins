#include "PluginEditor.h"

//==============================================================================
SpectrumAnalyzerEditor::SpectrumAnalyzerEditor(SpectrumAnalyzerProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(headerBar);
    addAndMakeVisible(spectrumDisplay);
    addAndMakeVisible(meterPanel);
    addAndMakeVisible(outputMeterL);
    addAndMakeVisible(outputMeterR);

    // Create settings overlay (hidden by default)
    settingsOverlay = std::make_unique<SettingsOverlay>();
    settingsOverlay->onDismiss = [this]() { hideSettings(); };
    addChildComponent(settingsOverlay.get()); // hidden initially

    // Gear button opens settings
    headerBar.getGearButton().onClick = [this]() { showSettings(); };

    // Title click opens supporters panel
    headerBar.onTitleClick = [this]() { showSupportersPanel(); };

    // Setup parameter attachments
    setupAttachments();

    // Initialize display range from parameters
    float minDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MIN);
    float maxDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MAX);
    spectrumDisplay.setDisplayRange(minDB, maxDB);

    bool peakHold = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_PEAK_HOLD) > 0.5f;
    spectrumDisplay.setShowPeakHold(peakHold);

    startTimerHz(30);

    resizeHelper.initialize(this, &audioProcessor, 900, 600, 720, 480, 3840, 2160, false);
    setResizable(true, false);
    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());
}

SpectrumAnalyzerEditor::~SpectrumAnalyzerEditor()
{
    resizeHelper.saveSize();
    stopTimer();
    setLookAndFeel(nullptr);
}

void SpectrumAnalyzerEditor::setupAttachments()
{
    auto& apvts = audioProcessor.getAPVTS();

    // Header controls
    fftResolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_FFT_RESOLUTION, headerBar.getFFTCombo());

    channelModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_CHANNEL_MODE, headerBar.getChannelCombo());

    // Settings overlay controls
    smoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_SMOOTHING, settingsOverlay->getSmoothingSlider());

    slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_SLOPE, settingsOverlay->getSlopeSlider());

    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_DECAY_RATE, settingsOverlay->getDecaySlider());

    peakHoldAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_PEAK_HOLD, headerBar.getPeakHoldButton());

    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_DISPLAY_MIN, settingsOverlay->getRangeSlider());
}

//==============================================================================
void SpectrumAnalyzerEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(SpectrumAnalyzerLookAndFeel::Colors::background));

    // Draw labels for the right-side meters
    auto contentBounds = getLocalBounds();
    contentBounds.removeFromTop(44);   // Header
    contentBounds.removeFromBottom(6);  // Bottom padding
    contentBounds.removeFromBottom(40); // Meter panel
    auto meterLabelArea = contentBounds.reduced(10).removeFromRight(70);

    // L/R labels at the very bottom
    auto lrLabelArea = meterLabelArea.removeFromBottom(18);
    g.setColour(juce::Colour(0xff888888));
    g.setFont(14.0f);
    g.drawText("L      R", lrLabelArea, juce::Justification::centred);

    // RMS value above L/R labels
    auto rmsArea = meterLabelArea.removeFromBottom(18);
    float rms = audioProcessor.getRmsLevel();
    juce::String rmsStr = rms > -99.0f ? juce::String(rms, 1) + " dB" : "-inf dB";
    g.setFont(14.0f);
    g.drawText("RMS: " + rmsStr, rmsArea, juce::Justification::centred);
}

void SpectrumAnalyzerEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header bar (44px)
    headerBar.setBounds(bounds.removeFromTop(44));

    // Bottom padding to clear host window chrome
    bounds.removeFromBottom(6);

    // Compact meter strip (40px)
    meterPanel.setBounds(bounds.removeFromBottom(40));

    // Main content area with spectrum and meters
    auto contentArea = bounds.reduced(10);

    // LED meters on the right side (70px wide total)
    auto meterArea = contentArea.removeFromRight(70);
    meterArea.removeFromTop(5);
    meterArea.removeFromBottom(35);

    int meterWidth = 24;
    int meterSpacing = 10;
    int totalMeterWidth = meterWidth * 2 + meterSpacing;
    int meterStartX = meterArea.getCentreX() - totalMeterWidth / 2;

    outputMeterL.setBounds(meterStartX, meterArea.getY(), meterWidth, meterArea.getHeight());
    outputMeterR.setBounds(meterStartX + meterWidth + meterSpacing, meterArea.getY(), meterWidth, meterArea.getHeight());

    // Spectrum display takes remaining space
    contentArea.removeFromRight(5);
    spectrumDisplay.setBounds(contentArea);

    // Overlays cover entire editor
    if (settingsOverlay)
        settingsOverlay->setBounds(getLocalBounds());
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());

    resizeHelper.updateResizer();
}

//==============================================================================
void SpectrumAnalyzerEditor::timerCallback()
{
    audioProcessor.getFFTProcessor().processFFT();

    if (audioProcessor.getFFTProcessor().isDataReady())
    {
        spectrumDisplay.updateMagnitudes(audioProcessor.getFFTProcessor().getMagnitudes());
        spectrumDisplay.updatePeakHold(audioProcessor.getFFTProcessor().getPeakHold());
        audioProcessor.getFFTProcessor().clearDataReady();
    }

    updateMeters();

    // Sync display settings from parameters
    float minDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MIN);
    float maxDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MAX);
    spectrumDisplay.setDisplayRange(minDB, maxDB);

    bool peakHold = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_PEAK_HOLD) > 0.5f;
    spectrumDisplay.setShowPeakHold(peakHold);
}

void SpectrumAnalyzerEditor::updateMeters()
{
    meterPanel.setCorrelation(audioProcessor.getCorrelation());
    meterPanel.setTruePeakL(audioProcessor.getTruePeakL());
    meterPanel.setTruePeakR(audioProcessor.getTruePeakR());
    meterPanel.setClipping(audioProcessor.hasClipped());
    meterPanel.setMomentaryLUFS(audioProcessor.getMomentaryLUFS());
    meterPanel.setShortTermLUFS(audioProcessor.getShortTermLUFS());
    meterPanel.setIntegratedLUFS(audioProcessor.getIntegratedLUFS());
    meterPanel.setLoudnessRange(audioProcessor.getLoudnessRange());

    float levelL = audioProcessor.getOutputLevelL();
    float levelR = audioProcessor.getOutputLevelR();
    meterPanel.setOutputLevelL(levelL);
    meterPanel.setOutputLevelR(levelR);
    meterPanel.setRmsLevel(audioProcessor.getRmsLevel());

    outputMeterL.setLevel(levelL);
    outputMeterR.setLevel(levelR);
}

//==============================================================================
void SpectrumAnalyzerEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("Spectrum Analyzer", JucePlugin_VersionString);
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }
    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void SpectrumAnalyzerEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}

void SpectrumAnalyzerEditor::showSettings()
{
    if (settingsOverlay)
    {
        settingsOverlay->setBounds(getLocalBounds());
        settingsOverlay->setVisible(true);
        settingsOverlay->toFront(true);
    }
}

void SpectrumAnalyzerEditor::hideSettings()
{
    if (settingsOverlay)
        settingsOverlay->setVisible(false);
}
