#pragma once

#include <JuceHeader.h>

class ModeSelector : public juce::Component
{
public:
    ModeSelector();
    ~ModeSelector() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    void setMode(int newMode);
    int getMode() const { return currentMode; }

    std::function<void(int)> onModeChanged;

private:
    int currentMode = 0;
    static constexpr int numModes = 12;

    juce::Point<float> lastMousePosition;
    float knobAngle = -135.0f;

    juce::Colour knobColour{60, 55, 50};
    juce::Colour markerColour{200, 180, 160};

    void updateAngleFromMode();
    void updateModeFromAngle();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeSelector)
};