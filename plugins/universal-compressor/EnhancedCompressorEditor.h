#pragma once

#include "UniversalCompressor.h"
#include "AnalogLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>

//==============================================================================
class EnhancedCompressorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::ComboBox::Listener,
                                 private RatioButtonGroup::Listener
{
public:
    EnhancedCompressorEditor(UniversalCompressor&);
    ~EnhancedCompressorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void ratioChanged(int ratioIndex) override;

private:
    // Processor reference
    UniversalCompressor& processor;
    
    // Look and feel instances for each mode
    std::unique_ptr<OptoLookAndFeel> optoLookAndFeel;
    std::unique_ptr<FETLookAndFeel> fetLookAndFeel;
    std::unique_ptr<VCALookAndFeel> vcaLookAndFeel;
    std::unique_ptr<BusLookAndFeel> busLookAndFeel;

    // Current active look
    juce::LookAndFeel* currentLookAndFeel = nullptr;
    
    // Meters
    std::unique_ptr<LEDMeter> inputMeter;
    std::unique_ptr<VUMeterWithLabel> vuMeter;
    std::unique_ptr<LEDMeter> outputMeter;
    
    // Mode selector
    std::unique_ptr<juce::ComboBox> modeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeSelectorAttachment;
    
    // Global controls
    std::unique_ptr<juce::ToggleButton> bypassButton;
    // Oversample button removed - saturation always runs at 2x internally
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    
    // Mode-specific panels
    struct OptoPanel
    {
        std::unique_ptr<juce::Component> container;
        std::unique_ptr<juce::Slider> peakReductionKnob;
        std::unique_ptr<juce::Slider> gainKnob;
        std::unique_ptr<juce::ToggleButton> limitSwitch;
        std::unique_ptr<juce::Label> peakReductionLabel;
        std::unique_ptr<juce::Label> gainLabel;
        
        // Attachments
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> peakReductionAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limitAttachment;
    };
    
    struct FETPanel
    {
        std::unique_ptr<juce::Component> container;
        std::unique_ptr<juce::Slider> inputKnob;
        std::unique_ptr<juce::Slider> outputKnob;
        std::unique_ptr<juce::Slider> attackKnob;
        std::unique_ptr<juce::Slider> releaseKnob;
        std::unique_ptr<RatioButtonGroup> ratioButtons;
        std::unique_ptr<juce::Label> inputLabel;
        std::unique_ptr<juce::Label> outputLabel;
        std::unique_ptr<juce::Label> attackLabel;
        std::unique_ptr<juce::Label> releaseLabel;
        
        // Attachments
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    };
    
    struct VCAPanel
    {
        std::unique_ptr<juce::Component> container;
        std::unique_ptr<juce::Slider> thresholdKnob;
        std::unique_ptr<juce::Slider> ratioKnob;
        std::unique_ptr<juce::Slider> attackKnob;
        // DBX 160 has fixed release rate - no release knob
        std::unique_ptr<juce::Slider> outputKnob;
        std::unique_ptr<juce::ToggleButton> overEasyButton;
        std::unique_ptr<juce::Label> thresholdLabel;
        std::unique_ptr<juce::Label> ratioLabel;
        std::unique_ptr<juce::Label> attackLabel;
        // No release label for DBX 160
        std::unique_ptr<juce::Label> outputLabel;
        
        // Attachments
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
        // No release attachment for DBX 160
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> overEasyAttachment;
    };
    
    struct BusPanel
    {
        std::unique_ptr<juce::Component> container;
        std::unique_ptr<juce::Slider> thresholdKnob;
        std::unique_ptr<juce::Slider> ratioKnob;
        std::unique_ptr<juce::ComboBox> attackSelector;
        std::unique_ptr<juce::ComboBox> releaseSelector;
        std::unique_ptr<juce::Slider> makeupKnob;
        std::unique_ptr<juce::Label> thresholdLabel;
        std::unique_ptr<juce::Label> ratioLabel;
        std::unique_ptr<juce::Label> attackLabel;
        std::unique_ptr<juce::Label> releaseLabel;
        std::unique_ptr<juce::Label> makeupLabel;
        
        // Attachments
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attackAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> releaseAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> makeupAttachment;
    };
    
    // Mode panels
    OptoPanel optoPanel;
    FETPanel fetPanel;
    VCAPanel vcaPanel;
    BusPanel busPanel;

    // Current mode
    int currentMode = 0;
    
    // Background texture
    juce::Image backgroundTexture;
    
    // Resizing support
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    float scaleFactor = 1.0f;
    
    // Smoothed level readouts for better readability
    float smoothedInputLevel = -60.0f;
    float smoothedOutputLevel = -60.0f;
    const float levelSmoothingFactor = 0.985f;  // Very high smoothing (0.985 = ~1 second at 30Hz)
    
    // Helper methods
    void setupOptoPanel();
    void setupFETPanel();
    void setupVCAPanel();
    void setupBusPanel();
    void setupDigitalPanel();
    // setupMultibandPanel removed
    
    void updateMode(int newMode);
    void updateMeters();
    void createBackgroundTexture();
    
    std::unique_ptr<juce::Slider> createKnob(const juce::String& name, float min, float max,
                                             float defaultValue, const juce::String& suffix = "");
    std::unique_ptr<juce::Label> createLabel(const juce::String& text, juce::Justification justification = juce::Justification::centred);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnhancedCompressorEditor)
};