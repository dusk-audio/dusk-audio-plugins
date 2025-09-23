#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
StudioReverbAudioProcessorEditor::StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make plugin resizable
    setResizable(true, true);
    setResizeLimits(400, 450, 900, 600);
    setSize (700, 500);

    // Set up look and feel
    getLookAndFeel().setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a90e2));
    getLookAndFeel().setColour(juce::Slider::trackColourId, juce::Colour(0xff2c3e50));
    getLookAndFeel().setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));

    // Reverb Type Selector
    addAndMakeVisible(reverbTypeCombo);
    reverbTypeCombo.addItemList({"Early Reflections", "Room", "Plate", "Hall"}, 1);
    reverbTypeCombo.setSelectedId(audioProcessor.reverbType->getIndex() + 1);
    reverbTypeCombo.onChange = [this] {
        audioProcessor.reverbType->setValueNotifyingHost((reverbTypeCombo.getSelectedId() - 1) / 3.0f);
    };

    addAndMakeVisible(reverbTypeLabel);
    reverbTypeLabel.setText("Reverb Type", juce::dontSendNotification);
    reverbTypeLabel.setJustificationType(juce::Justification::centred);
    reverbTypeLabel.attachToComponent(&reverbTypeCombo, false);

    // Room Size
    setupSlider(roomSizeSlider, roomSizeLabel, "Room Size", "%");
    roomSizeSlider.setRange(0.0, 100.0, 0.1);
    roomSizeSlider.setValue(audioProcessor.roomSize->get());
    roomSizeSlider.onValueChange = [this] {
        audioProcessor.roomSize->setValueNotifyingHost(roomSizeSlider.getValue() / 100.0f);
    };

    // Damping
    setupSlider(dampingSlider, dampingLabel, "Damping", "%");
    dampingSlider.setRange(0.0, 100.0, 0.1);
    dampingSlider.setValue(audioProcessor.damping->get());
    dampingSlider.onValueChange = [this] {
        audioProcessor.damping->setValueNotifyingHost(dampingSlider.getValue() / 100.0f);
    };

    // Pre-Delay
    setupSlider(preDelaySlider, preDelayLabel, "Pre-Delay", "ms");
    preDelaySlider.setRange(0.0, 200.0, 0.1);
    preDelaySlider.setValue(audioProcessor.preDelay->get());
    preDelaySlider.onValueChange = [this] {
        audioProcessor.preDelay->setValueNotifyingHost(preDelaySlider.getValue() / 200.0f);
    };

    // Decay Time
    setupSlider(decayTimeSlider, decayTimeLabel, "Decay Time", "s");
    decayTimeSlider.setRange(0.1, 30.0, 0.1);
    decayTimeSlider.setValue(audioProcessor.decayTime->get());
    decayTimeSlider.onValueChange = [this] {
        audioProcessor.decayTime->setValueNotifyingHost((decayTimeSlider.getValue() - 0.1f) / 29.9f);
    };

    // Diffusion
    setupSlider(diffusionSlider, diffusionLabel, "Diffusion", "%");
    diffusionSlider.setRange(0.0, 100.0, 0.1);
    diffusionSlider.setValue(audioProcessor.diffusion->get());
    diffusionSlider.onValueChange = [this] {
        audioProcessor.diffusion->setValueNotifyingHost(diffusionSlider.getValue() / 100.0f);
    };

    // Wet Level
    setupSlider(wetLevelSlider, wetLevelLabel, "Wet Level", "%");
    wetLevelSlider.setRange(0.0, 100.0, 0.1);
    wetLevelSlider.setValue(audioProcessor.wetLevel->get());
    wetLevelSlider.onValueChange = [this] {
        audioProcessor.wetLevel->setValueNotifyingHost(wetLevelSlider.getValue() / 100.0f);
    };

    // Dry Level
    setupSlider(dryLevelSlider, dryLevelLabel, "Dry Level", "%");
    dryLevelSlider.setRange(0.0, 100.0, 0.1);
    dryLevelSlider.setValue(audioProcessor.dryLevel->get());
    dryLevelSlider.onValueChange = [this] {
        audioProcessor.dryLevel->setValueNotifyingHost(dryLevelSlider.getValue() / 100.0f);
    };

    // Width
    setupSlider(widthSlider, widthLabel, "Width", "%");
    widthSlider.setRange(0.0, 100.0, 0.1);
    widthSlider.setValue(audioProcessor.width->get());
    widthSlider.onValueChange = [this] {
        audioProcessor.width->setValueNotifyingHost(widthSlider.getValue() / 100.0f);
    };

    startTimerHz(30);
}

StudioReverbAudioProcessorEditor::~StudioReverbAudioProcessorEditor()
{
}

void StudioReverbAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                                   const juce::String& labelText,
                                                   const juce::String& suffix)
{
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setTextValueSuffix(" " + suffix);

    addAndMakeVisible(label);
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.attachToComponent(&slider, false);
}

//==============================================================================
void StudioReverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark gradient background
    g.fillAll(juce::Colour(0xff1a1a1a));

    juce::ColourGradient gradient(juce::Colour(0xff2c3e50), 0, 0,
                                  juce::Colour(0xff1a1a1a), 0, getHeight(), false);
    g.setGradientFill(gradient);
    g.fillAll();

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("StudioReverb", getLocalBounds().removeFromTop(40),
               juce::Justification::centred, true);

    // Draw section backgrounds
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50);
    bounds.reduce(10, 10);

    // Type selector background
    auto typeArea = bounds.removeFromTop(60);
    g.setColour(juce::Colour(0x20ffffff));
    g.fillRoundedRectangle(typeArea.toFloat(), 5.0f);

    // Parameters background
    bounds.removeFromTop(10);
    g.setColour(juce::Colour(0x20ffffff));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
}

void StudioReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // Title area
    bounds.reduce(20, 20);

    // Reverb Type Selector
    auto typeArea = bounds.removeFromTop(60);
    typeArea.removeFromTop(20); // Label space
    reverbTypeCombo.setBounds(typeArea.reduced(100, 5));

    bounds.removeFromTop(20); // Spacing

    const int sliderSize = 80;
    const int labelHeight = 20;
    const int spacing = 10;

    // First row - Main parameters
    auto row1 = bounds.removeFromTop(sliderSize + labelHeight + 10);
    row1.removeFromTop(labelHeight);
    int totalWidth = (sliderSize * 4) + (spacing * 3);
    int startX = (row1.getWidth() - totalWidth) / 2;

    roomSizeSlider.setBounds(row1.getX() + startX, row1.getY(), sliderSize, sliderSize);
    dampingSlider.setBounds(row1.getX() + startX + sliderSize + spacing, row1.getY(), sliderSize, sliderSize);
    preDelaySlider.setBounds(row1.getX() + startX + (sliderSize + spacing) * 2, row1.getY(), sliderSize, sliderSize);
    decayTimeSlider.setBounds(row1.getX() + startX + (sliderSize + spacing) * 3, row1.getY(), sliderSize, sliderSize);

    // Second row - Mix and modulation
    auto row2 = bounds.removeFromTop(sliderSize + labelHeight + 10);
    row2.removeFromTop(labelHeight);

    diffusionSlider.setBounds(row2.getX() + startX, row2.getY(), sliderSize, sliderSize);
    wetLevelSlider.setBounds(row2.getX() + startX + sliderSize + spacing, row2.getY(), sliderSize, sliderSize);
    dryLevelSlider.setBounds(row2.getX() + startX + (sliderSize + spacing) * 2, row2.getY(), sliderSize, sliderSize);
    widthSlider.setBounds(row2.getX() + startX + (sliderSize + spacing) * 3, row2.getY(), sliderSize, sliderSize);
}

void StudioReverbAudioProcessorEditor::timerCallback()
{
    // Update UI values from processor
    if (reverbTypeCombo.getSelectedId() != audioProcessor.reverbType->getIndex() + 1)
        reverbTypeCombo.setSelectedId(audioProcessor.reverbType->getIndex() + 1, juce::dontSendNotification);

    if (roomSizeSlider.getValue() != audioProcessor.roomSize->get())
        roomSizeSlider.setValue(audioProcessor.roomSize->get(), juce::dontSendNotification);

    if (dampingSlider.getValue() != audioProcessor.damping->get())
        dampingSlider.setValue(audioProcessor.damping->get(), juce::dontSendNotification);

    if (preDelaySlider.getValue() != audioProcessor.preDelay->get())
        preDelaySlider.setValue(audioProcessor.preDelay->get(), juce::dontSendNotification);

    if (decayTimeSlider.getValue() != audioProcessor.decayTime->get())
        decayTimeSlider.setValue(audioProcessor.decayTime->get(), juce::dontSendNotification);

    if (diffusionSlider.getValue() != audioProcessor.diffusion->get())
        diffusionSlider.setValue(audioProcessor.diffusion->get(), juce::dontSendNotification);

    if (wetLevelSlider.getValue() != audioProcessor.wetLevel->get())
        wetLevelSlider.setValue(audioProcessor.wetLevel->get(), juce::dontSendNotification);

    if (dryLevelSlider.getValue() != audioProcessor.dryLevel->get())
        dryLevelSlider.setValue(audioProcessor.dryLevel->get(), juce::dontSendNotification);

    if (widthSlider.getValue() != audioProcessor.width->get())
        widthSlider.setValue(audioProcessor.width->get(), juce::dontSendNotification);
}