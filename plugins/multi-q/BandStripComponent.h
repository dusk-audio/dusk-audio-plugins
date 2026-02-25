#pragma once

#include <JuceHeader.h>
#include "EQBand.h"

class MultiQ;

//==============================================================================
/**
    BandStripComponent - Eventide SplitEQ-style horizontal band display

    Shows all 8 EQ bands in a horizontal strip with click-to-edit text values
    for frequency, gain, and Q parameters.
*/
class BandStripComponent : public juce::Component,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit BandStripComponent(MultiQ& processor);
    ~BandStripComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Selection management
    void setSelectedBand(int bandIndex);
    int getSelectedBand() const { return selectedBand; }

    // Callback when user clicks a band column
    std::function<void(int)> onBandSelected;

private:
    MultiQ& processor;

    //==========================================================================
    // Band column structure
    struct BandColumn
    {
        int bandIndex = 0;
        BandType type = BandType::Parametric;
        juce::Colour color;

        // Enable toggle
        std::unique_ptr<juce::TextButton> enableButton;

        // Editable value labels
        std::unique_ptr<juce::Label> freqLabel;
        std::unique_ptr<juce::Label> gainLabel;   // For parametric/shelf bands
        std::unique_ptr<juce::Label> qLabel;

        // Slope selector for filter bands (HPF/LPF)
        std::unique_ptr<juce::ComboBox> slopeSelector;

        // Parameter attachments
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
        std::unique_ptr<juce::ParameterAttachment> freqAttachment;
        std::unique_ptr<juce::ParameterAttachment> gainAttachment;
        std::unique_ptr<juce::ParameterAttachment> qAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAttachment;

        // Bounds for click detection
        juce::Rectangle<int> columnBounds;
    };

    std::array<BandColumn, 8> bandColumns;
    int selectedBand = 0;  // Default to first band

    //==========================================================================
    // Setup methods
    void setupBandColumn(int index);
    void setupEditableLabel(juce::Label& label, const juce::String& tooltip);
    void updateBandValues(int index);

    //==========================================================================
    // Value formatting
    static juce::String formatFrequency(float freq);
    static juce::String formatGain(float gain);
    static juce::String formatQ(float q);
    static juce::String formatSlope(int slopeIndex);
    static juce::String getBandTypeName(BandType type);

    //==========================================================================
    // Value parsing
    static float parseFrequency(const juce::String& text);
    static float parseGain(const juce::String& text);
    static float parseQ(const juce::String& text);

    //==========================================================================
    // Drawing helpers
    void drawBandColumn(juce::Graphics& g, int index, juce::Rectangle<float> bounds);
    void drawSelectionHighlight(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour bandColor);

    //==========================================================================
    // Parameter listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandStripComponent)
};
