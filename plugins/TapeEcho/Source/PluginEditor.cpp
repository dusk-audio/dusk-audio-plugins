#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../../../shared/LunaVintageLookAndFeel.h"

// VintageKnobLookAndFeel implementation - inherits from LunaVintage
VintageKnobLookAndFeel::VintageKnobLookAndFeel()
{
    // Inherits vintage styling from LunaVintageLookAndFeel
    // Can add TapeEcho-specific customizations here if needed
}

// drawRotarySlider is inherited from LunaVintageLookAndFeel

// TapeEchoEditor implementation
TapeEchoEditor::TapeEchoEditor(TapeEchoProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Unified Luna sizing
    setSize(800, 600);
    setResizable(true, true);
    setResizeLimits(600, 450, 1200, 900);

    setupControls();
    setupLabels();

    addAndMakeVisible(vuMeter);
    addAndMakeVisible(modeSelector);
    addAndMakeVisible(presetSelector);
    addAndMakeVisible(vintageToggle);

    // Setup preset selector
    presetSelector.addItem("User", 1);
    for (size_t i = 0; i < audioProcessor.getFactoryPresets().size(); ++i)
    {
        presetSelector.addItem(audioProcessor.getFactoryPresets()[i].name, i + 2);
    }
    presetSelector.setSelectedId(1);
    presetSelector.addListener(this);

    // Setup vintage toggle
    vintageToggle.setButtonText("Vintage");
    vintageToggle.setToggleState(true, juce::dontSendNotification);
    vintageToggle.onClick = [this]() {
        isVintageMode = vintageToggle.getToggleState();
        updateAppearance();
    };

    // Mode selector callback
    modeSelector.onModeChanged = [this](int mode) {
        auto* param = audioProcessor.apvts.getParameter(TapeEchoProcessor::PARAM_MODE);
        if (param)
        {
            param->setValueNotifyingHost(mode / 11.0f);
        }
    };

    // Create parameter attachments
    repeatRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_REPEAT_RATE, repeatRateKnob);

    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_INTENSITY, intensityKnob);

    echoVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_ECHO_VOLUME, echoVolumeKnob);

    reverbVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_REVERB_VOLUME, reverbVolumeKnob);

    bassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_BASS, bassKnob);

    trebleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_TREBLE, trebleKnob);

    inputVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_INPUT_VOLUME, inputVolumeKnob);

    wowFlutterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_WOW_FLUTTER, wowFlutterSlider);

    tapeAgeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_TAPE_AGE, tapeAgeSlider);

    motorTorqueAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_MOTOR_TORQUE, motorTorqueSlider);

    stereoModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, TapeEchoProcessor::PARAM_STEREO_MODE, stereoModeButton);

    startTimerHz(30);
    updateAppearance();
}

TapeEchoEditor::~TapeEchoEditor()
{
    stopTimer();
}

void TapeEchoEditor::setupControls()
{
    // Main knobs
    repeatRateKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    repeatRateKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    repeatRateKnob.setLookAndFeel(&knobLookAndFeel);
    repeatRateKnob.setRange(50.0, 1000.0);
    repeatRateKnob.setTextValueSuffix(" ms");
    addAndMakeVisible(repeatRateKnob);

    intensityKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    intensityKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    intensityKnob.setLookAndFeel(&knobLookAndFeel);
    intensityKnob.setRange(0.0, 100.0);
    intensityKnob.setTextValueSuffix(" %");
    addAndMakeVisible(intensityKnob);

    echoVolumeKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    echoVolumeKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    echoVolumeKnob.setLookAndFeel(&knobLookAndFeel);
    echoVolumeKnob.setRange(0.0, 100.0);
    echoVolumeKnob.setTextValueSuffix(" %");
    addAndMakeVisible(echoVolumeKnob);

    reverbVolumeKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reverbVolumeKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    reverbVolumeKnob.setLookAndFeel(&knobLookAndFeel);
    reverbVolumeKnob.setRange(0.0, 100.0);
    reverbVolumeKnob.setTextValueSuffix(" %");
    addAndMakeVisible(reverbVolumeKnob);

    bassKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    bassKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    bassKnob.setLookAndFeel(&knobLookAndFeel);
    bassKnob.setRange(-12.0, 12.0);
    bassKnob.setTextValueSuffix(" dB");
    addAndMakeVisible(bassKnob);

    trebleKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    trebleKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    trebleKnob.setLookAndFeel(&knobLookAndFeel);
    trebleKnob.setRange(-12.0, 12.0);
    trebleKnob.setTextValueSuffix(" dB");
    addAndMakeVisible(trebleKnob);

    inputVolumeKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    inputVolumeKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    inputVolumeKnob.setLookAndFeel(&knobLookAndFeel);
    inputVolumeKnob.setRange(0.0, 100.0);
    inputVolumeKnob.setTextValueSuffix(" %");
    addAndMakeVisible(inputVolumeKnob);

    // Extended controls
    wowFlutterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wowFlutterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible(wowFlutterSlider);

    tapeAgeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tapeAgeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible(tapeAgeSlider);

    motorTorqueSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    motorTorqueSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible(motorTorqueSlider);

    stereoModeButton.setButtonText("Stereo");
    addAndMakeVisible(stereoModeButton);
}

void TapeEchoEditor::setupLabels()
{
    // Set label font and color
    auto setupLabel = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font("Arial", 11.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour(200, 190, 170));
        addAndMakeVisible(label);
    };

    setupLabel(repeatRateLabel, "RATE");
    setupLabel(intensityLabel, "INTENSITY");
    setupLabel(echoVolumeLabel, "ECHO");
    setupLabel(reverbVolumeLabel, "REVERB");
    setupLabel(bassLabel, "BASS");
    setupLabel(trebleLabel, "TREBLE");
    setupLabel(inputVolumeLabel, "INPUT");

    wowFlutterLabel.setText("WOW/FLUTTER", juce::dontSendNotification);
    wowFlutterLabel.setFont(juce::Font("Arial", 10.0f, juce::Font::bold));
    wowFlutterLabel.setColour(juce::Label::textColourId, juce::Colour(200, 190, 170));
    addAndMakeVisible(wowFlutterLabel);

    tapeAgeLabel.setText("TAPE AGE", juce::dontSendNotification);
    tapeAgeLabel.setFont(juce::Font("Arial", 10.0f, juce::Font::bold));
    tapeAgeLabel.setColour(juce::Label::textColourId, juce::Colour(200, 190, 170));
    addAndMakeVisible(tapeAgeLabel);

    motorTorqueLabel.setText("MOTOR", juce::dontSendNotification);
    motorTorqueLabel.setFont(juce::Font("Arial", 10.0f, juce::Font::bold));
    motorTorqueLabel.setColour(juce::Label::textColourId, juce::Colour(200, 190, 170));
    addAndMakeVisible(motorTorqueLabel);

    presetLabel.setText("PRESET:", juce::dontSendNotification);
    presetLabel.setFont(juce::Font("Arial", 10.0f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(200, 190, 170));
    addAndMakeVisible(presetLabel);
}

void TapeEchoEditor::paint(juce::Graphics& g)
{
    if (isVintageMode)
    {
        // Military green/olive background like classic hardware
        juce::ColourGradient bgGradient(juce::Colour(65, 70, 55),
                                         getLocalBounds().getCentre().toFloat(),
                                         juce::Colour(45, 50, 35),
                                         getLocalBounds().getBottomRight().toFloat(), true);
        g.setGradientFill(bgGradient);
        g.fillAll();

        // Add subtle texture pattern
        g.setColour(juce::Colour(40, 45, 30).withAlpha(0.3f));
        for (int y = 0; y < getHeight(); y += 3)
        {
            g.drawHorizontalLine(y, 0, getWidth());
        }

        // Draw section panels
        auto bounds = getLocalBounds();
        bounds.removeFromTop(50);  // Title space
        bounds.removeFromBottom(25); // Bottom space
        bounds = bounds.reduced(10);

        // Draw recessed panel areas
        g.setColour(juce::Colour(35, 40, 25));
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(25, 30, 18));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 2.0f);
    }
    else
    {
        // Modern clean background
        g.fillAll(juce::Colour(45, 45, 50));
    }

    // Title bar
    auto titleBar = getLocalBounds().removeFromTop(50);
    g.setColour(juce::Colour(30, 35, 20));
    g.fillRect(titleBar);

    // Title text with retro styling
    g.setColour(juce::Colour(200, 190, 170));
    g.setFont(juce::Font("Arial", 22.0f, juce::Font::bold));
    g.drawText("VINTAGE TAPE ECHO", titleBar.reduced(10, 0),
               juce::Justification::centredLeft);

    // Company name on right
    g.setFont(14.0f);
    g.drawText("LUNA CO. AUDIO", titleBar.reduced(10, 0),
               juce::Justification::centredRight);
}

void TapeEchoEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50);  // Title bar space
    bounds.removeFromBottom(25); // Bottom space
    bounds = bounds.reduced(15); // Main panel inset

    // Create three main sections like hardware
    auto topSection = bounds.removeFromTop(200);
    auto middleSection = bounds.removeFromTop(140);
    auto bottomSection = bounds;

    // TOP SECTION: Mode selector, Echo controls, VU meter
    // Left: Mode selector
    auto modePanel = topSection.removeFromLeft(160).reduced(5);
    modeSelector.setBounds(modePanel);

    // Right: VU meter - make it larger
    auto vuPanel = topSection.removeFromRight(180).reduced(5);
    vuMeter.setBounds(vuPanel);

    // Center: Echo controls (3 knobs)
    auto echoPanel = topSection.reduced(5);
    auto knobWidth = 85;
    auto knobSpacing = 10;
    auto labelHeight = 20;

    // Center the knobs in the echo panel
    auto totalWidth = knobWidth * 3 + knobSpacing * 2;
    auto knobsArea = echoPanel.withWidth(totalWidth)
                               .withX(echoPanel.getX() + (echoPanel.getWidth() - totalWidth) / 2);

    // Repeat Rate
    repeatRateLabel.setBounds(knobsArea.getX(), knobsArea.getY(), knobWidth, labelHeight);
    repeatRateKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight));

    knobsArea.removeFromLeft(knobSpacing);

    // Intensity
    intensityLabel.setBounds(knobsArea.getX(), echoPanel.getY(), knobWidth, labelHeight);
    intensityKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight));

    knobsArea.removeFromLeft(knobSpacing);

    // Input Volume
    inputVolumeLabel.setBounds(knobsArea.getX(), echoPanel.getY(), knobWidth, labelHeight);
    inputVolumeKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight));

    // MIDDLE SECTION: Output and Tone controls
    knobWidth = 75;
    totalWidth = knobWidth * 4 + knobSpacing * 3;
    knobsArea = middleSection.withWidth(totalWidth)
                             .withX(middleSection.getX() + (middleSection.getWidth() - totalWidth) / 2);

    // Echo Volume
    echoVolumeLabel.setBounds(knobsArea.getX(), middleSection.getY(), knobWidth, labelHeight);
    echoVolumeKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight).reduced(3));

    knobsArea.removeFromLeft(knobSpacing);

    // Reverb Volume
    reverbVolumeLabel.setBounds(knobsArea.getX(), middleSection.getY(), knobWidth, labelHeight);
    reverbVolumeKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight).reduced(3));

    knobsArea.removeFromLeft(knobSpacing);

    // Bass
    bassLabel.setBounds(knobsArea.getX(), middleSection.getY(), knobWidth, labelHeight);
    bassKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight).reduced(3));

    knobsArea.removeFromLeft(knobSpacing);

    // Treble
    trebleLabel.setBounds(knobsArea.getX(), middleSection.getY(), knobWidth, labelHeight);
    trebleKnob.setBounds(knobsArea.removeFromLeft(knobWidth).withTrimmedTop(labelHeight).reduced(3));

    // BOTTOM SECTION: Extended controls and presets
    auto extendedArea = bottomSection.removeFromTop(40).reduced(10, 5);
    auto sliderHeight = 20;

    // Wow & Flutter
    wowFlutterLabel.setBounds(extendedArea.removeFromLeft(90));
    wowFlutterSlider.setBounds(extendedArea.removeFromLeft(140).withHeight(sliderHeight));
    extendedArea.removeFromLeft(15);

    // Tape Age
    tapeAgeLabel.setBounds(extendedArea.removeFromLeft(70));
    tapeAgeSlider.setBounds(extendedArea.removeFromLeft(140).withHeight(sliderHeight));
    extendedArea.removeFromLeft(15);

    // Motor Torque
    motorTorqueLabel.setBounds(extendedArea.removeFromLeft(90));
    motorTorqueSlider.setBounds(extendedArea.removeFromLeft(140).withHeight(sliderHeight));

    // Preset controls at very bottom
    auto presetArea = bottomSection.removeFromTop(35).reduced(10, 5);
    presetLabel.setBounds(presetArea.removeFromLeft(50));
    presetSelector.setBounds(presetArea.removeFromLeft(180));
    presetArea.removeFromLeft(15);
    stereoModeButton.setBounds(presetArea.removeFromLeft(70));
    presetArea.removeFromLeft(15);
    vintageToggle.setBounds(presetArea.removeFromLeft(70));
}

void TapeEchoEditor::timerCallback()
{
    float level = audioProcessor.getCurrentPeakLevel();
    vuMeter.setLevel(level);

    // Update mode selector
    int mode = static_cast<int>(audioProcessor.apvts.getRawParameterValue(
        TapeEchoProcessor::PARAM_MODE)->load());
    modeSelector.setMode(mode);
}

void TapeEchoEditor::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (comboBox == &presetSelector)
    {
        int selection = presetSelector.getSelectedId();
        if (selection > 1)
        {
            int presetIndex = selection - 2;
            if (presetIndex < audioProcessor.getFactoryPresets().size())
            {
                audioProcessor.loadPreset(audioProcessor.getFactoryPresets()[presetIndex]);
            }
        }
    }
}

void TapeEchoEditor::updateAppearance()
{
    if (isVintageMode)
    {
        // Vintage green military style
        setColour(juce::Label::textColourId, juce::Colour(200, 190, 170));
        presetSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(35, 40, 25));
        presetSelector.setColour(juce::ComboBox::textColourId, juce::Colour(200, 190, 170));
        presetSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(55, 60, 40));

        stereoModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(200, 190, 170));
        stereoModeButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(100, 255, 100));

        vintageToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(200, 190, 170));
        vintageToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(100, 255, 100));
    }
    else
    {
        // Modern style
        setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        presetSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(60, 60, 65));
        presetSelector.setColour(juce::ComboBox::textColourId, juce::Colours::white);
        presetSelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::grey);

        stereoModeButton.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
        stereoModeButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);

        vintageToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
        vintageToggle.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    }

    repaint();
}