void VintageVerbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Title area
    bounds.removeFromTop(60);

    // Main reverb controls section (top)
    auto knobSize = 70;
    auto knobSpacing = 80;
    auto labelHeight = 20;

    // Row 1: Main reverb parameters
    int row1Y = 90;
    mixSlider.setBounds(40, row1Y, knobSize, knobSize);
    sizeSlider.setBounds(40 + knobSpacing, row1Y, knobSize, knobSize);
    attackSlider.setBounds(40 + knobSpacing * 2, row1Y, knobSize, knobSize);
    dampingSlider.setBounds(40 + knobSpacing * 3, row1Y, knobSize, knobSize);
    predelaySlider.setBounds(40 + knobSpacing * 4, row1Y, knobSize, knobSize);
    widthSlider.setBounds(40 + knobSpacing * 5, row1Y, knobSize, knobSize);
    modulationSlider.setBounds(40 + knobSpacing * 6, row1Y, knobSize, knobSize);

    // Visualizer on right side
    reverbDisplay.setBounds(getWidth() - 320, 80, 280, 130);

    // Row 2: EQ controls
    int row2Y = 250;
    bassFreqSlider.setBounds(40, row2Y, knobSize, knobSize);
    bassMulSlider.setBounds(40 + knobSpacing, row2Y, knobSize, knobSize);
    highFreqSlider.setBounds(40 + knobSpacing * 2, row2Y, knobSize, knobSize);
    highMulSlider.setBounds(40 + knobSpacing * 3, row2Y, knobSize, knobSize);

    // Row 3: Advanced controls
    int row3Y = 360;
    densitySlider.setBounds(40, row3Y, knobSize, knobSize);
    diffusionSlider.setBounds(40 + knobSpacing, row3Y, knobSize, knobSize);
    shapeSlider.setBounds(40 + knobSpacing * 2, row3Y, knobSize, knobSize);
    spreadSlider.setBounds(40 + knobSpacing * 3, row3Y, knobSize, knobSize);

    // Mode selectors
    int selectorY = 460;
    reverbModeSelector.setBounds(40, selectorY, 180, 30);
    colorModeSelector.setBounds(240, selectorY, 140, 30);
    routingModeSelector.setBounds(400, selectorY, 140, 30);
    engineMixSlider.setBounds(560, selectorY - 10, 180, 50);

    // Row 4: Filters and output
    int row4Y = 540;
    hpfFreqSlider.setBounds(40, row4Y, knobSize, knobSize);
    lpfFreqSlider.setBounds(40 + knobSpacing, row4Y, knobSize, knobSize);
    tiltGainSlider.setBounds(40 + knobSpacing * 2, row4Y + 15, 160, 45);

    inputGainSlider.setBounds(40 + knobSpacing * 4, row4Y, knobSize, knobSize);
    outputGainSlider.setBounds(40 + knobSpacing * 5, row4Y, knobSize, knobSize);

    // Meters
    int meterX = 40 + knobSpacing * 6 + 30;
    inputMeterL.setBounds(meterX, row4Y, 25, 75);
    inputMeterR.setBounds(meterX + 30, row4Y, 25, 75);
    outputMeterL.setBounds(meterX + 70, row4Y, 25, 75);
    outputMeterR.setBounds(meterX + 100, row4Y, 25, 75);

    // Presets at bottom
    int presetY = getHeight() - 60;
    presetSelector.setBounds(40, presetY, 280, 30);
    savePresetButton.setBounds(340, presetY, 120, 30);
    loadPresetButton.setBounds(470, presetY, 120, 30);
}