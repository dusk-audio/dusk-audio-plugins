#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
StudioReverbAudioProcessorEditor::StudioReverbAudioProcessorEditor (StudioReverbAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make plugin resizable
    setResizable(true, true);
    setResizeLimits(400, 300, 800, 600);
    setSize (600, 400);

    // Set up look and feel
    getLookAndFeel().setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a90e2));
    getLookAndFeel().setColour(juce::Slider::trackColourId, juce::Colour(0xff2c3e50));
    getLookAndFeel().setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));

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

    g.setColour(juce::Colour(0x20ffffff));
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
}

void StudioReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(50); // Title area
    bounds.reduce(20, 20);

    const int sliderSize = 100;
    const int labelHeight = 20;
    const int spacing = 10;

    // Center all sliders
    bounds.removeFromTop(50);
    int totalWidth = (sliderSize * 5) + (spacing * 4);
    int startX = (bounds.getWidth() - totalWidth) / 2;

    roomSizeSlider.setBounds(startX, bounds.getY() + labelHeight, sliderSize, sliderSize);
    dampingSlider.setBounds(startX + sliderSize + spacing, bounds.getY() + labelHeight, sliderSize, sliderSize);
    wetLevelSlider.setBounds(startX + (sliderSize + spacing) * 2, bounds.getY() + labelHeight, sliderSize, sliderSize);
    dryLevelSlider.setBounds(startX + (sliderSize + spacing) * 3, bounds.getY() + labelHeight, sliderSize, sliderSize);
    widthSlider.setBounds(startX + (sliderSize + spacing) * 4, bounds.getY() + labelHeight, sliderSize, sliderSize);
}

void StudioReverbAudioProcessorEditor::timerCallback()
{
    // Update UI values from processor
    if (roomSizeSlider.getValue() != audioProcessor.roomSize->get())
        roomSizeSlider.setValue(audioProcessor.roomSize->get(), juce::dontSendNotification);

    if (dampingSlider.getValue() != audioProcessor.damping->get())
        dampingSlider.setValue(audioProcessor.damping->get(), juce::dontSendNotification);

    if (wetLevelSlider.getValue() != audioProcessor.wetLevel->get())
        wetLevelSlider.setValue(audioProcessor.wetLevel->get(), juce::dontSendNotification);

    if (dryLevelSlider.getValue() != audioProcessor.dryLevel->get())
        dryLevelSlider.setValue(audioProcessor.dryLevel->get(), juce::dontSendNotification);

    if (widthSlider.getValue() != audioProcessor.width->get())
        widthSlider.setValue(audioProcessor.width->get(), juce::dontSendNotification);
}