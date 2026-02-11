#include "PluginEditor.h"

//==============================================================================
SpectrumAnalyzerEditor::SpectrumAnalyzerEditor(SpectrumAnalyzerProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    // Setup components
    setupHeader();
    addAndMakeVisible(spectrumDisplay);
    addAndMakeVisible(meterPanel);
    addAndMakeVisible(toolbar);

    // Add LED meters on the right side
    addAndMakeVisible(outputMeterL);
    addAndMakeVisible(outputMeterR);

    // Setup parameter attachments
    setupAttachments();

    // Initialize display range from parameters
    float minDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MIN);
    float maxDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MAX);
    spectrumDisplay.setDisplayRange(minDB, maxDB);

    // Set initial peak hold state
    bool peakHold = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_PEAK_HOLD) > 0.5f;
    spectrumDisplay.setShowPeakHold(peakHold);

    // Start timer for UI updates (30 Hz)
    startTimerHz(30);

    // Set size
    setSize(900, 600);
}

SpectrumAnalyzerEditor::~SpectrumAnalyzerEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void SpectrumAnalyzerEditor::setupHeader()
{
    // Channel mode selector
    channelModeLabel.setText("Mode:", juce::dontSendNotification);
    channelModeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    addAndMakeVisible(channelModeLabel);

    channelModeCombo.addItem("Stereo", 1);
    channelModeCombo.addItem("Mono", 2);
    channelModeCombo.addItem("Mid", 3);
    channelModeCombo.addItem("Side", 4);
    addAndMakeVisible(channelModeCombo);
}

void SpectrumAnalyzerEditor::setupAttachments()
{
    auto& apvts = audioProcessor.getAPVTS();

    channelModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_CHANNEL_MODE, channelModeCombo);

    fftResolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_FFT_RESOLUTION, toolbar.getFFTResolutionCombo());

    smoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_SMOOTHING, toolbar.getSmoothingSlider());

    slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_SLOPE, toolbar.getSlopeSlider());

    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_DECAY_RATE, toolbar.getDecaySlider());

    peakHoldAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_PEAK_HOLD, toolbar.getPeakHoldButton());

    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, SpectrumAnalyzerProcessor::PARAM_DISPLAY_MIN, toolbar.getRangeSlider());

    // Add listener to update spectrum display when range changes
    toolbar.getRangeSlider().onValueChange = [this]() {
        float minDB = static_cast<float>(toolbar.getRangeSlider().getValue());
        float maxDB = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_DISPLAY_MAX);
        spectrumDisplay.setDisplayRange(minDB, maxDB);
    };
}

//==============================================================================
void SpectrumAnalyzerEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(SpectrumAnalyzerLookAndFeel::Colors::background));

    // Header
    auto bounds = getLocalBounds();
    auto headerArea = bounds.removeFromTop(40);

    g.setColour(juce::Colour(SpectrumAnalyzerLookAndFeel::Colors::panelBg));
    g.fillRect(headerArea);

    // Plugin title
    g.setColour(juce::Colour(SpectrumAnalyzerLookAndFeel::Colors::accent));
    g.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    g.drawText("SPECTRUM ANALYZER", headerArea.reduced(15, 0).removeFromLeft(200),
        juce::Justification::centredLeft);

    // Company name
    g.setColour(juce::Colour(SpectrumAnalyzerLookAndFeel::Colors::textSecondary));
    g.setFont(12.0f);
    g.drawText("Dusk Audio", headerArea.reduced(15, 0),
        juce::Justification::centredRight);

    // Header border
    g.setColour(juce::Colour(SpectrumAnalyzerLookAndFeel::Colors::border));
    g.drawHorizontalLine(headerArea.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    // Store title click area for supporters overlay
    titleClickArea = headerArea.reduced(15, 0).withWidth(200);

    // Draw labels for the right-side meters
    auto contentBounds = getLocalBounds();
    contentBounds.removeFromTop(40);  // Header
    contentBounds.removeFromBottom(35);  // Toolbar
    contentBounds.removeFromBottom(120);  // Meter panel
    auto meterLabelArea = contentBounds.reduced(10).removeFromRight(70);

    // L/R labels at the very bottom
    auto lrLabelArea = meterLabelArea.removeFromBottom(16);
    g.setColour(juce::Colour(0xff888888));
    g.setFont(11.0f);
    g.drawText("L      R", lrLabelArea, juce::Justification::centred);

    // RMS value above L/R labels
    auto rmsArea = meterLabelArea.removeFromBottom(16);
    float rms = audioProcessor.getRmsLevel();
    juce::String rmsStr = rms > -99.0f ? juce::String(rms, 1) + " dB" : "-inf dB";
    g.setFont(10.0f);
    g.drawText("RMS: " + rmsStr, rmsArea, juce::Justification::centred);
}

void SpectrumAnalyzerEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header (40px)
    auto headerArea = bounds.removeFromTop(40);
    auto headerControls = headerArea.reduced(15, 5);
    headerControls.removeFromLeft(220);  // Skip title area

    channelModeLabel.setBounds(headerControls.removeFromLeft(40));
    channelModeCombo.setBounds(headerControls.removeFromLeft(80).reduced(0, 2));

    // Toolbar (35px) at bottom
    toolbar.setBounds(bounds.removeFromBottom(35));

    // Meter panel (120px) at bottom - slightly smaller now that output meters are on right
    meterPanel.setBounds(bounds.removeFromBottom(120));

    // Main content area with spectrum and meters
    auto contentArea = bounds.reduced(10);

    // LED meters on the right side (70px wide total)
    auto meterArea = contentArea.removeFromRight(70);
    meterArea.removeFromTop(5);  // Small top margin
    meterArea.removeFromBottom(35);  // Space for RMS + L/R labels (16 + 16 + 3 padding)

    // Position L and R meters with proper spacing
    int meterWidth = 24;
    int meterSpacing = 10;
    int totalMeterWidth = meterWidth * 2 + meterSpacing;
    int meterStartX = meterArea.getCentreX() - totalMeterWidth / 2;

    outputMeterL.setBounds(meterStartX, meterArea.getY(), meterWidth, meterArea.getHeight());
    outputMeterR.setBounds(meterStartX + meterWidth + meterSpacing, meterArea.getY(), meterWidth, meterArea.getHeight());

    // Spectrum display takes remaining space (with small gap from meters)
    contentArea.removeFromRight(5);
    spectrumDisplay.setBounds(contentArea);

    // Supporters overlay
    if (supportersOverlay)
        supportersOverlay->setBounds(getLocalBounds());
}

//==============================================================================
void SpectrumAnalyzerEditor::timerCallback()
{
    // Process FFT on timer thread
    audioProcessor.getFFTProcessor().processFFT();

    // Update spectrum display if data ready
    if (audioProcessor.getFFTProcessor().isDataReady())
    {
        spectrumDisplay.updateMagnitudes(audioProcessor.getFFTProcessor().getMagnitudes());
        spectrumDisplay.updatePeakHold(audioProcessor.getFFTProcessor().getPeakHold());
        audioProcessor.getFFTProcessor().clearDataReady();
    }

    // Update meters
    updateMeters();

    // Update peak hold visibility
    bool peakHold = *audioProcessor.getAPVTS().getRawParameterValue(SpectrumAnalyzerProcessor::PARAM_PEAK_HOLD) > 0.5f;
    spectrumDisplay.setShowPeakHold(peakHold);
}

void SpectrumAnalyzerEditor::updateMeters()
{
    // Correlation
    meterPanel.setCorrelation(audioProcessor.getCorrelation());

    // True peak
    meterPanel.setTruePeakL(audioProcessor.getTruePeakL());
    meterPanel.setTruePeakR(audioProcessor.getTruePeakR());
    meterPanel.setClipping(audioProcessor.hasClipped());

    // LUFS
    meterPanel.setMomentaryLUFS(audioProcessor.getMomentaryLUFS());
    meterPanel.setShortTermLUFS(audioProcessor.getShortTermLUFS());
    meterPanel.setIntegratedLUFS(audioProcessor.getIntegratedLUFS());
    meterPanel.setLoudnessRange(audioProcessor.getLoudnessRange());

    // Output levels - update both the panel and the right-side LED meters
    float levelL = audioProcessor.getOutputLevelL();
    float levelR = audioProcessor.getOutputLevelR();
    meterPanel.setOutputLevelL(levelL);
    meterPanel.setOutputLevelR(levelR);
    meterPanel.setRmsLevel(audioProcessor.getRmsLevel());

    // Update the LED meters on the right side
    outputMeterL.setLevel(levelL);
    outputMeterR.setLevel(levelR);
}

//==============================================================================
void SpectrumAnalyzerEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
        showSupportersPanel();
}

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
