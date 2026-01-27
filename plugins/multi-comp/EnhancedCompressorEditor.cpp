#include "EnhancedCompressorEditor.h"
#include <cmath>

//==============================================================================
EnhancedCompressorEditor::EnhancedCompressorEditor(UniversalCompressor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Initialize look and feels
    optoLookAndFeel = std::make_unique<OptoLookAndFeel>();
    fetLookAndFeel = std::make_unique<FETLookAndFeel>();
    studioFetLookAndFeel = std::make_unique<StudioFETLookAndFeel>();  // Teal accent for Studio FET
    vcaLookAndFeel = std::make_unique<VCALookAndFeel>();
    busLookAndFeel = std::make_unique<BusLookAndFeel>();
    studioVcaLookAndFeel = std::make_unique<StudioVCALookAndFeel>();
    digitalLookAndFeel = std::make_unique<DigitalLookAndFeel>();
    
    // Create background texture
    createBackgroundTexture();
    
    // Create meters with stereo mode enabled
    inputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    inputMeter->setStereoMode(true);  // Show L/R channels
    vuMeter = std::make_unique<VUMeterWithLabel>();
    outputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    outputMeter->setStereoMode(true);  // Show L/R channels

    addAndMakeVisible(inputMeter.get());
    addAndMakeVisible(vuMeter.get());
    addAndMakeVisible(outputMeter.get());
    
    // Create mode selector - 8 modes matching Logic Pro style
    modeSelector = std::make_unique<juce::ComboBox>("Mode");
    modeSelector->addItem("Vintage Opto", 1);
    modeSelector->addItem("Vintage FET", 2);
    modeSelector->addItem("Classic VCA", 3);
    modeSelector->addItem("Bus Compressor", 4);
    modeSelector->addItem("Studio FET", 5);
    modeSelector->addItem("Studio VCA", 6);
    modeSelector->addItem("Digital", 7);
    modeSelector->addItem("Multiband", 8);
    // Don't set a default - let the attachment handle it
    // Remove listener - the attachment and parameterChanged handle it
    addAndMakeVisible(modeSelector.get());

    // Presets are exposed via DAW's native preset menu (getNumPrograms/setCurrentProgram/getProgramName)

    // Create global controls with full readable labels
    bypassButton = std::make_unique<juce::ToggleButton>("Bypass");
    autoGainButton = std::make_unique<juce::ToggleButton>("Auto Gain");
    sidechainEnableButton = std::make_unique<juce::ToggleButton>("Ext SC");
    sidechainListenButton = std::make_unique<juce::ToggleButton>("SC Listen");
    analogNoiseButton = std::make_unique<juce::ToggleButton>("Analog Noise");

    // Lookahead slider (not shown in header, but kept for parameter)
    lookaheadSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft);
    lookaheadSlider->setRange(0.0, 10.0, 0.1);
    lookaheadSlider->setTextValueSuffix(" ms");
    lookaheadSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 18);

    // Oversampling selector with clear items (Off, 2x, 4x)
    oversamplingSelector = std::make_unique<juce::ComboBox>("Oversampling");
    oversamplingSelector->addItem("Off", 1);
    oversamplingSelector->addItem("2x", 2);
    oversamplingSelector->addItem("4x", 3);
    oversamplingSelector->setSelectedId(2);  // Default to 2x

    // Sidechain HP filter vertical slider (Off to 500Hz)
    sidechainHpSlider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
    sidechainHpSlider->setRange(0.0, 500.0, 1.0);  // 0 = Off, up to 500Hz
    sidechainHpSlider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    sidechainHpSlider->setSkewFactorFromMidPoint(80.0);  // Skew so useful range (20-200Hz) is more accessible
    sidechainHpSlider->setTooltip("Sidechain High-Pass Filter - removes low frequencies from detector to prevent pumping");
    sidechainHpSlider->textFromValueFunction = [](double value) {
        if (value < 1.0)
            return juce::String("Off");
        return juce::String(static_cast<int>(value)) + " Hz";
    };
    sidechainHpSlider->valueFromTextFunction = [](const juce::String& text) {
        if (text.containsIgnoreCase("off"))
            return 0.0;
        return text.getDoubleValue();
    };

    // SC EQ toggle button - use ToggleButton for radio style
    scEqToggleButton = std::make_unique<juce::TextButton>("SC EQ");
    scEqToggleButton->setClickingTogglesState(true);
    scEqToggleButton->setToggleState(false, juce::dontSendNotification);
    scEqToggleButton->onClick = [this]() {
        scEqVisible = scEqToggleButton->getToggleState();
        resized();
    };

    // Sidechain EQ controls (not in header - too complex, keep hidden for now)
    scLowFreqSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft);
    scLowFreqSlider->setRange(60.0, 500.0, 1.0);
    scLowFreqSlider->setTextValueSuffix(" Hz");
    scLowFreqSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 45, 16);
    scLowFreqSlider->setSkewFactorFromMidPoint(150.0);

    scLowGainSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft);
    scLowGainSlider->setRange(-12.0, 12.0, 0.1);
    scLowGainSlider->setTextValueSuffix(" dB");
    scLowGainSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 45, 16);

    scHighFreqSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft);
    scHighFreqSlider->setRange(2000.0, 16000.0, 10.0);
    scHighFreqSlider->setTextValueSuffix(" Hz");
    scHighFreqSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 16);
    scHighFreqSlider->setSkewFactorFromMidPoint(6000.0);

    scHighGainSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft);
    scHighGainSlider->setRange(-12.0, 12.0, 0.1);
    scHighGainSlider->setTextValueSuffix(" dB");
    scHighGainSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 45, 16);

    addAndMakeVisible(bypassButton.get());
    addAndMakeVisible(autoGainButton.get());
    addAndMakeVisible(analogNoiseButton.get());
    addAndMakeVisible(oversamplingSelector.get());
    addAndMakeVisible(sidechainHpSlider.get());
    // Hide SC EQ and sidechain controls - simplify the header
    addChildComponent(sidechainEnableButton.get());
    addChildComponent(sidechainListenButton.get());
    addChildComponent(lookaheadSlider.get());
    addChildComponent(scEqToggleButton.get());
    addChildComponent(scLowFreqSlider.get());
    addChildComponent(scLowGainSlider.get());
    addChildComponent(scHighFreqSlider.get());
    addChildComponent(scHighGainSlider.get());
    
    // Setup mode panels
    setupOptoPanel();
    setupFETPanel();
    setupVCAPanel();
    setupBusPanel();
    setupDigitalPanel();
    setupMultibandPanel();

    // Create parameter attachments
    auto& params = processor.getParameters();
    
    if (params.getRawParameterValue("mode"))
        modeSelectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            params, "mode", *modeSelector);
    
    if (params.getRawParameterValue("bypass"))
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "bypass", *bypassButton);

    if (params.getRawParameterValue("auto_makeup"))
        autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "auto_makeup", *autoGainButton);

    if (params.getRawParameterValue("sidechain_enable"))
        sidechainEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "sidechain_enable", *sidechainEnableButton);

    if (params.getRawParameterValue("global_sidechain_listen"))
        sidechainListenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "global_sidechain_listen", *sidechainListenButton);

    if (params.getRawParameterValue("noise_enable"))
        analogNoiseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "noise_enable", *analogNoiseButton);

    if (params.getRawParameterValue("global_lookahead"))
        lookaheadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "global_lookahead", *lookaheadSlider);

    if (params.getRawParameterValue("oversampling"))
        oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            params, "oversampling", *oversamplingSelector);

    if (params.getRawParameterValue("sidechain_hp"))
        sidechainHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "sidechain_hp", *sidechainHpSlider);

    if (params.getRawParameterValue("sc_low_freq"))
        scLowFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "sc_low_freq", *scLowFreqSlider);

    if (params.getRawParameterValue("sc_low_gain"))
        scLowGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "sc_low_gain", *scLowGainSlider);

    if (params.getRawParameterValue("sc_high_freq"))
        scHighFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "sc_high_freq", *scHighFreqSlider);

    if (params.getRawParameterValue("sc_high_gain"))
        scHighGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "sc_high_gain", *scHighGainSlider);

    // Listen to mode and auto_makeup changes
    params.addParameterListener("mode", this);
    params.addParameterListener("auto_makeup", this);

    // Listen for preset changes (for Bitwig and other hosts that need explicit UI refresh)
    processor.addPresetChangeListener(this);

    // Set initial mode
    const auto* modeParam = params.getRawParameterValue("mode");
    currentMode = modeParam ? static_cast<int>(*modeParam) : 0;

    // Set initial auto-gain state
    const auto* autoMakeupParam = params.getRawParameterValue("auto_makeup");
    updateAutoGainState(autoMakeupParam ? autoMakeupParam->load() > 0.5f : false);

    // Sync combo box to initial mode (add 1 since combo box uses 1-based IDs)
    modeSelector->setSelectedId(currentMode + 1, juce::dontSendNotification);
    updateMode(currentMode);
    
    // Start timer for meter updates
    startTimerHz(30);
    
    // Setup resizing
    constrainer.setMinimumSize(500, 350);  // Minimum size
    constrainer.setMaximumSize(1400, 1000); // Maximum size
    constrainer.setFixedAspectRatio(700.0 / 500.0); // Keep aspect ratio matching default size
    
    // Create resizer component
    resizer = std::make_unique<juce::ResizableCornerComponent>(this, &constrainer);
    addAndMakeVisible(resizer.get());
    resizer->setAlwaysOnTop(true);
    
    // Set initial size - do this last so resized() is called after all components are created
    setSize(750, 500);  // Wider to fit all controls with clear labels
    setResizable(true, false);  // Allow resizing, no native title bar
}

EnhancedCompressorEditor::~EnhancedCompressorEditor()
{
    // Stop timer first to prevent callbacks during destruction
    stopTimer();

    processor.removePresetChangeListener(this);
    processor.getParameters().removeParameterListener("mode", this);
    processor.getParameters().removeParameterListener("auto_makeup", this);

    // Clear look and feel from all components before destruction
    if (bypassButton)
        bypassButton->setLookAndFeel(nullptr);
    if (autoGainButton)
        autoGainButton->setLookAndFeel(nullptr);
    if (sidechainEnableButton)
        sidechainEnableButton->setLookAndFeel(nullptr);
    if (sidechainListenButton)
        sidechainListenButton->setLookAndFeel(nullptr);
    if (analogNoiseButton)
        analogNoiseButton->setLookAndFeel(nullptr);
    if (lookaheadSlider)
        lookaheadSlider->setLookAndFeel(nullptr);
    if (oversamplingSelector)
        oversamplingSelector->setLookAndFeel(nullptr);
    if (sidechainHpSlider)
        sidechainHpSlider->setLookAndFeel(nullptr);
    if (scLowFreqSlider)
        scLowFreqSlider->setLookAndFeel(nullptr);
    if (scLowGainSlider)
        scLowGainSlider->setLookAndFeel(nullptr);
    if (scHighFreqSlider)
        scHighFreqSlider->setLookAndFeel(nullptr);
    if (scHighGainSlider)
        scHighGainSlider->setLookAndFeel(nullptr);
    if (optoPanel.limitSwitch)
        optoPanel.limitSwitch->setLookAndFeel(nullptr);
    if (optoPanel.peakReductionKnob)
        optoPanel.peakReductionKnob->setLookAndFeel(nullptr);
    if (optoPanel.gainKnob)
        optoPanel.gainKnob->setLookAndFeel(nullptr);
    if (optoPanel.mixKnob)
        optoPanel.mixKnob->setLookAndFeel(nullptr);
    if (fetPanel.inputKnob)
        fetPanel.inputKnob->setLookAndFeel(nullptr);
    if (fetPanel.outputKnob)
        fetPanel.outputKnob->setLookAndFeel(nullptr);
    if (fetPanel.attackKnob)
        fetPanel.attackKnob->setLookAndFeel(nullptr);
    if (fetPanel.releaseKnob)
        fetPanel.releaseKnob->setLookAndFeel(nullptr);
    if (vcaPanel.thresholdKnob)
        vcaPanel.thresholdKnob->setLookAndFeel(nullptr);
    if (vcaPanel.ratioKnob)
        vcaPanel.ratioKnob->setLookAndFeel(nullptr);
    if (vcaPanel.attackKnob)
        vcaPanel.attackKnob->setLookAndFeel(nullptr);
    if (vcaPanel.outputKnob)
        vcaPanel.outputKnob->setLookAndFeel(nullptr);
    if (vcaPanel.overEasyButton)
        vcaPanel.overEasyButton->setLookAndFeel(nullptr);
    if (busPanel.thresholdKnob)
        busPanel.thresholdKnob->setLookAndFeel(nullptr);
    if (busPanel.ratioKnob)
        busPanel.ratioKnob->setLookAndFeel(nullptr);
    if (busPanel.attackSelector)
        busPanel.attackSelector->setLookAndFeel(nullptr);
    if (busPanel.releaseSelector)
        busPanel.releaseSelector->setLookAndFeel(nullptr);
    if (busPanel.makeupKnob)
        busPanel.makeupKnob->setLookAndFeel(nullptr);
    if (studioVcaPanel)
        studioVcaPanel->setLookAndFeel(nullptr);
    if (digitalPanel)
        digitalPanel->setLookAndFeel(nullptr);

    setLookAndFeel(nullptr);
}

void EnhancedCompressorEditor::createBackgroundTexture()
{
    backgroundTexture = juce::Image(juce::Image::RGB, 100, 100, true);
    juce::Graphics g(backgroundTexture);
    
    // Create subtle noise texture
    juce::Random random;
    for (int y = 0; y < 100; ++y)
    {
        for (int x = 0; x < 100; ++x)
        {
            auto brightness = 0.02f + random.nextFloat() * 0.03f;
            g.setColour(juce::Colour::fromFloatRGBA(brightness, brightness, brightness, 1.0f));
            g.fillRect(x, y, 1, 1);
        }
    }
}

std::unique_ptr<juce::Slider> EnhancedCompressorEditor::createKnob(const juce::String& name,
                                                                   float min, float max,
                                                                   float defaultValue,
                                                                   const juce::String& suffix)
{
    auto slider = std::make_unique<juce::Slider>(name);
    slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider->setRange(min, max, 0.01);
    slider->setValue(defaultValue);
    slider->setTextValueSuffix(suffix);
    slider->setDoubleClickReturnValue(true, defaultValue);
    return slider;
}

std::unique_ptr<juce::Label> EnhancedCompressorEditor::createLabel(const juce::String& text,
                                                                   juce::Justification justification)
{
    auto label = std::make_unique<juce::Label>(text, text);
    label->setJustificationType(justification);
    // Font will be scaled in resized() based on window size
    label->setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    label->setColour(juce::Label::textColourId, juce::Colours::white);  // Default to white for visibility
    return label;
}

void EnhancedCompressorEditor::setupOptoPanel()
{
    optoPanel.container = std::make_unique<juce::Component>();
    addChildComponent(optoPanel.container.get());  // Use addChildComponent so it's initially hidden

    // Create controls
    optoPanel.peakReductionKnob = createKnob("Peak Reduction", 0, 100, 0, "");  // Default 0 = no compression
    // Opto Gain: 0-100 range, 50 = unity (0dB), maps to -40dB to +40dB internally
    optoPanel.gainKnob = createKnob("Gain", 0, 100, 50, "");
    optoPanel.mixKnob = createKnob("Mix", 0, 100, 100, "%");
    optoPanel.limitSwitch = std::make_unique<juce::ToggleButton>("Limit");

    // Create labels
    optoPanel.peakReductionLabel = createLabel("PEAK REDUCTION");
    optoPanel.gainLabel = createLabel("GAIN");
    optoPanel.mixLabel = createLabel("MIX");

    // Add to container
    optoPanel.container->addAndMakeVisible(optoPanel.peakReductionKnob.get());
    optoPanel.container->addAndMakeVisible(optoPanel.gainKnob.get());
    optoPanel.container->addAndMakeVisible(optoPanel.mixKnob.get());
    // Note: limitSwitch is added to main editor, not container, so it can be in top row
    addChildComponent(optoPanel.limitSwitch.get());  // Add to main editor as child component
    optoPanel.container->addAndMakeVisible(optoPanel.peakReductionLabel.get());
    optoPanel.container->addAndMakeVisible(optoPanel.gainLabel.get());
    optoPanel.container->addAndMakeVisible(optoPanel.mixLabel.get());

    // Create attachments
    auto& params = processor.getParameters();
    if (params.getRawParameterValue("opto_peak_reduction"))
        optoPanel.peakReductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "opto_peak_reduction", *optoPanel.peakReductionKnob);

    if (params.getRawParameterValue("opto_gain"))
        optoPanel.gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "opto_gain", *optoPanel.gainKnob);

    if (params.getRawParameterValue("mix"))
        optoPanel.mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "mix", *optoPanel.mixKnob);

    if (params.getRawParameterValue("opto_limit"))
        optoPanel.limitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "opto_limit", *optoPanel.limitSwitch);
}

void EnhancedCompressorEditor::setupFETPanel()
{
    fetPanel.container = std::make_unique<juce::Component>();
    addChildComponent(fetPanel.container.get());  // Use addChildComponent so it's initially hidden
    
    // Create controls
    // FET Input: drives signal into fixed -10dB threshold (authentic 1176 behavior)
    // Range: -20dB to +40dB, with 0dB default
    fetPanel.inputKnob = createKnob("Input", -20, 40, 0, " dB");
    fetPanel.outputKnob = createKnob("Output", -20, 20, 0, " dB");
    fetPanel.attackKnob = createKnob("Attack", 0.02, 0.8, 0.02, " ms");
    // Custom text display for microseconds
    fetPanel.attackKnob->textFromValueFunction = [](double value) {
        return juce::String(static_cast<int>(value * 1000.0)) + " μs";
    };
    fetPanel.attackKnob->valueFromTextFunction = [](const juce::String& text) {
        return text.getDoubleValue() / 1000.0;
    };
    fetPanel.releaseKnob = createKnob("Release", 50, 1100, 400, " ms");
    fetPanel.mixKnob = createKnob("Mix", 0, 100, 100, "%");
    fetPanel.ratioButtons = std::make_unique<RatioButtonGroup>();
    fetPanel.ratioButtons->addListener(this);

    // Create labels
    fetPanel.inputLabel = createLabel("INPUT");
    fetPanel.outputLabel = createLabel("OUTPUT");
    fetPanel.attackLabel = createLabel("ATTACK");
    fetPanel.releaseLabel = createLabel("RELEASE");
    fetPanel.mixLabel = createLabel("MIX");

    // Add to container
    fetPanel.container->addAndMakeVisible(fetPanel.inputKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.outputKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.attackKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.releaseKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.mixKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.ratioButtons.get());
    fetPanel.container->addAndMakeVisible(fetPanel.inputLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.outputLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.attackLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.releaseLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.mixLabel.get());

    // Create attachments
    auto& params = processor.getParameters();
    if (params.getRawParameterValue("fet_input"))
        fetPanel.inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "fet_input", *fetPanel.inputKnob);

    if (params.getRawParameterValue("fet_output"))
        fetPanel.outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "fet_output", *fetPanel.outputKnob);

    if (params.getRawParameterValue("fet_attack"))
        fetPanel.attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "fet_attack", *fetPanel.attackKnob);

    if (params.getRawParameterValue("fet_release"))
        fetPanel.releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "fet_release", *fetPanel.releaseKnob);

    if (params.getRawParameterValue("mix"))
        fetPanel.mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "mix", *fetPanel.mixKnob);
}

void EnhancedCompressorEditor::setupVCAPanel()
{
    vcaPanel.container = std::make_unique<juce::Component>();
    addChildComponent(vcaPanel.container.get());  // Use addChildComponent so it's initially hidden

    // Create controls - Classic VCA style
    vcaPanel.thresholdKnob = createKnob("Threshold", -38, 12, 0, " dB");  // 10mV to 3V range
    // Classic VCA ratio: 1:1 to infinity (120:1), with 4:1 at 12 o'clock (center)
    // The parameter has skew=0.3 which places 4:1 near the center of rotation
    vcaPanel.ratioKnob = createKnob("Ratio", 1, 120, 4, ":1");
    vcaPanel.ratioKnob->setSkewFactorFromMidPoint(4.0);  // 4:1 at 12 o'clock
    vcaPanel.attackKnob = createKnob("Attack", 0.1, 50, 1, " ms");  // Classic VCA attack range
    // Classic VCA has fixed release rate - no release knob needed
    vcaPanel.outputKnob = createKnob("Output", -20, 20, 0, " dB");
    vcaPanel.mixKnob = createKnob("Mix", 0, 100, 100, "%");
    vcaPanel.overEasyButton = std::make_unique<juce::ToggleButton>("Over Easy");

    // Create labels
    vcaPanel.thresholdLabel = createLabel("THRESHOLD");
    vcaPanel.ratioLabel = createLabel("RATIO");
    vcaPanel.attackLabel = createLabel("ATTACK");
    // No release label for Classic VCA
    vcaPanel.outputLabel = createLabel("OUTPUT");
    vcaPanel.mixLabel = createLabel("MIX");

    // Add to container
    vcaPanel.container->addAndMakeVisible(vcaPanel.thresholdKnob.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.ratioKnob.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.attackKnob.get());
    // No release knob for Classic VCA
    vcaPanel.container->addAndMakeVisible(vcaPanel.outputKnob.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.mixKnob.get());
    // Note: overEasyButton is added to main editor, not container, so it can be in top row
    addChildComponent(vcaPanel.overEasyButton.get());  // Add to main editor as child component
    vcaPanel.container->addAndMakeVisible(vcaPanel.thresholdLabel.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.ratioLabel.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.attackLabel.get());
    // No release label for Classic VCA
    vcaPanel.container->addAndMakeVisible(vcaPanel.outputLabel.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.mixLabel.get());

    // Create attachments
    auto& params = processor.getParameters();
    if (params.getRawParameterValue("vca_threshold"))
        vcaPanel.thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "vca_threshold", *vcaPanel.thresholdKnob);

    if (params.getRawParameterValue("vca_ratio"))
        vcaPanel.ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "vca_ratio", *vcaPanel.ratioKnob);

    if (params.getRawParameterValue("vca_attack"))
        vcaPanel.attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "vca_attack", *vcaPanel.attackKnob);

    // Classic VCA has fixed release rate - no attachment needed

    if (params.getRawParameterValue("vca_output"))
        vcaPanel.outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "vca_output", *vcaPanel.outputKnob);

    if (params.getRawParameterValue("mix"))
        vcaPanel.mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "mix", *vcaPanel.mixKnob);

    if (params.getRawParameterValue("vca_overeasy"))
        vcaPanel.overEasyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            params, "vca_overeasy", *vcaPanel.overEasyButton);
}

void EnhancedCompressorEditor::setupBusPanel()
{
    busPanel.container = std::make_unique<juce::Component>();
    addChildComponent(busPanel.container.get());  // Use addChildComponent so it's initially hidden
    
    // Create controls
    // Bus threshold: -30dB to +15dB range (SSL Bus compressor style)
    busPanel.thresholdKnob = createKnob("Threshold", -30, 15, 0, " dB");
    // Note: Bus ratio uses ComboBox attachment (2:1, 4:1, 10:1) - this knob is not used
    busPanel.ratioKnob = createKnob("Ratio", 2, 10, 4, ":1");  // Placeholder, actual ratio via ComboBox
    busPanel.makeupKnob = createKnob("Makeup", 0, 20, 0, " dB");
    busPanel.mixKnob = createKnob("Mix", 0, 100, 100, "%");
    
    busPanel.attackSelector = std::make_unique<juce::ComboBox>("Attack");
    busPanel.attackSelector->addItem("0.1 ms", 1);
    busPanel.attackSelector->addItem("0.3 ms", 2);
    busPanel.attackSelector->addItem("1 ms", 3);
    busPanel.attackSelector->addItem("3 ms", 4);
    busPanel.attackSelector->addItem("10 ms", 5);
    busPanel.attackSelector->addItem("30 ms", 6);
    busPanel.attackSelector->setSelectedId(3);
    
    busPanel.releaseSelector = std::make_unique<juce::ComboBox>("Release");
    busPanel.releaseSelector->addItem("0.1 s", 1);
    busPanel.releaseSelector->addItem("0.3 s", 2);
    busPanel.releaseSelector->addItem("0.6 s", 3);
    busPanel.releaseSelector->addItem("1.2 s", 4);
    busPanel.releaseSelector->addItem("Auto", 5);
    busPanel.releaseSelector->setSelectedId(2);
    
    
    // Create labels
    busPanel.thresholdLabel = createLabel("THRESHOLD");
    busPanel.ratioLabel = createLabel("RATIO");
    busPanel.attackLabel = createLabel("ATTACK");
    busPanel.releaseLabel = createLabel("RELEASE");
    busPanel.makeupLabel = createLabel("MAKEUP");
    busPanel.mixLabel = createLabel("MIX");
    
    // Add to container
    busPanel.container->addAndMakeVisible(busPanel.thresholdKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.ratioKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.attackSelector.get());
    busPanel.container->addAndMakeVisible(busPanel.releaseSelector.get());
    busPanel.container->addAndMakeVisible(busPanel.makeupKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.mixKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.thresholdLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.ratioLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.attackLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.releaseLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.makeupLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.mixLabel.get());
    
    // Create attachments
    auto& params = processor.getParameters();
    if (params.getRawParameterValue("bus_threshold"))
        busPanel.thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "bus_threshold", *busPanel.thresholdKnob);
    
    if (params.getRawParameterValue("bus_ratio"))
        busPanel.ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "bus_ratio", *busPanel.ratioKnob);
    
    if (params.getRawParameterValue("bus_attack"))
        busPanel.attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            params, "bus_attack", *busPanel.attackSelector);
    
    if (params.getRawParameterValue("bus_release"))
        busPanel.releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            params, "bus_release", *busPanel.releaseSelector);
    
    if (params.getRawParameterValue("bus_makeup"))
        busPanel.makeupAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "bus_makeup", *busPanel.makeupKnob);

    // Use global mix parameter for consistency across all modes
    if (params.getRawParameterValue("mix"))
        busPanel.mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "mix", *busPanel.mixKnob);
}

void EnhancedCompressorEditor::setupDigitalPanel()
{
    // Digital Compressor Panel (transparent, modern)
    digitalPanel = std::make_unique<DigitalCompressorPanel>(processor.getParameters());
    addChildComponent(digitalPanel.get());

    // Studio VCA Panel (precision red style)
    studioVcaPanel = std::make_unique<StudioVCAPanel>(processor.getParameters());
    addChildComponent(studioVcaPanel.get());
}

void EnhancedCompressorEditor::setupMultibandPanel()
{
    // Multiband Compressor Panel (4-band dynamics)
    multibandPanel = std::make_unique<MultibandCompressorPanel>(processor.getParameters());
    addChildComponent(multibandPanel.get());
}

void EnhancedCompressorEditor::updateMode(int newMode)
{
    currentMode = juce::jlimit(0, 7, newMode);  // 0-7 for 8 modes

    // Hide all panels
    optoPanel.container->setVisible(false);
    fetPanel.container->setVisible(false);
    vcaPanel.container->setVisible(false);
    busPanel.container->setVisible(false);
    if (digitalPanel) digitalPanel->setVisible(false);
    if (studioVcaPanel) studioVcaPanel->setVisible(false);
    if (multibandPanel) multibandPanel->setVisible(false);

    // Show VU meter by default (will be hidden for multiband)
    if (vuMeter)
        vuMeter->setVisible(true);

    // Show SC HP slider by default (will be hidden for multiband)
    if (sidechainHpSlider)
        sidechainHpSlider->setVisible(true);

    // Hide mode-specific top row buttons by default
    if (optoPanel.limitSwitch)
        optoPanel.limitSwitch->setVisible(false);
    if (vcaPanel.overEasyButton)
        vcaPanel.overEasyButton->setVisible(false);

    // Show and set look for current mode
    switch (currentMode)
    {
        case 0: // Vintage Opto
            optoPanel.container->setVisible(true);
            if (optoPanel.limitSwitch)
                optoPanel.limitSwitch->setVisible(true);
            currentLookAndFeel = optoLookAndFeel.get();
            break;

        case 1: // Vintage FET
            fetPanel.container->setVisible(true);
            currentLookAndFeel = fetLookAndFeel.get();
            break;

        case 2: // Classic VCA
            vcaPanel.container->setVisible(true);
            if (vcaPanel.overEasyButton)
                vcaPanel.overEasyButton->setVisible(true);
            currentLookAndFeel = vcaLookAndFeel.get();
            break;

        case 3: // Bus Compressor
            busPanel.container->setVisible(true);
            currentLookAndFeel = busLookAndFeel.get();
            break;

        case 4: // Studio FET - shares FET panel but uses teal accent
            fetPanel.container->setVisible(true);
            currentLookAndFeel = studioFetLookAndFeel.get();  // Teal accent to differentiate from Vintage FET
            break;

        case 5: // Studio VCA
            if (studioVcaPanel)
            {
                studioVcaPanel->setVisible(true);
                studioVcaPanel->setLookAndFeel(studioVcaLookAndFeel.get());
            }
            currentLookAndFeel = studioVcaLookAndFeel.get();
            break;

        case 6: // Digital (Transparent)
            if (digitalPanel)
            {
                digitalPanel->setVisible(true);
                digitalPanel->setLookAndFeel(digitalLookAndFeel.get());
            }
            currentLookAndFeel = digitalLookAndFeel.get();
            break;

        case 7: // Multiband
            if (multibandPanel)
            {
                multibandPanel->setVisible(true);
                multibandPanel->setLookAndFeel(digitalLookAndFeel.get());  // Use digital look for multiband
            }
            // Hide VU meter for multiband - the panel has its own per-band GR visualization
            if (vuMeter)
                vuMeter->setVisible(false);
            // Hide SC HP slider for multiband - each band has its own sidechain handling
            if (sidechainHpSlider)
                sidechainHpSlider->setVisible(false);
            currentLookAndFeel = digitalLookAndFeel.get();
            break;
    }

    // Apply look and feel to all components
    if (currentLookAndFeel)
    {
        setLookAndFeel(currentLookAndFeel);

        // Set button text colors based on mode for visibility - all light for dark backgrounds
        juce::Colour buttonTextColor;
        switch (currentMode)
        {
            case 0: // Opto - dark brown background
                buttonTextColor = juce::Colour(0xFFE8D5B7);  // Warm light
                break;
            case 1: // FET - black background
                buttonTextColor = juce::Colour(0xFFE0E0E0);  // Light gray
                break;
            case 2: // VCA - dark gray background
                buttonTextColor = juce::Colour(0xFFDFE6E9);  // Light gray-blue
                break;
            case 3: // Bus - dark blue background
                buttonTextColor = juce::Colour(0xFFECF0F1);  // Light gray
                break;
            case 4: // Studio FET - black background with teal accent
                buttonTextColor = juce::Colour(0xFFE0E0E0);  // Light gray
                break;
            case 5: // Studio VCA - dark red background
                buttonTextColor = juce::Colour(0xFFD0D0D0);  // Light gray
                break;
            case 6: // Digital - dark blue background
                buttonTextColor = juce::Colour(0xFFE0E0E0);  // Light gray
                break;
            case 7: // Multiband - dark blue background
                buttonTextColor = juce::Colour(0xFFE0E0E0);  // Light gray
                break;
            default:
                buttonTextColor = juce::Colour(0xFFE0E0E0);
                break;
        }
        
        // Apply look and feel to global toggle buttons so they match current mode
        if (bypassButton)
            bypassButton->setLookAndFeel(currentLookAndFeel);

        if (autoGainButton)
            autoGainButton->setLookAndFeel(currentLookAndFeel);

        if (sidechainEnableButton)
            sidechainEnableButton->setLookAndFeel(currentLookAndFeel);

        if (sidechainListenButton)
            sidechainListenButton->setLookAndFeel(currentLookAndFeel);

        if (lookaheadSlider)
            lookaheadSlider->setLookAndFeel(currentLookAndFeel);

        if (analogNoiseButton)
            analogNoiseButton->setLookAndFeel(currentLookAndFeel);

        if (oversamplingSelector)
            oversamplingSelector->setLookAndFeel(currentLookAndFeel);

        if (sidechainHpSlider)
            sidechainHpSlider->setLookAndFeel(currentLookAndFeel);

        // Sidechain EQ sliders
        if (scLowFreqSlider)
            scLowFreqSlider->setLookAndFeel(currentLookAndFeel);
        if (scLowGainSlider)
            scLowGainSlider->setLookAndFeel(currentLookAndFeel);
        if (scHighFreqSlider)
            scHighFreqSlider->setLookAndFeel(currentLookAndFeel);
        if (scHighGainSlider)
            scHighGainSlider->setLookAndFeel(currentLookAndFeel);

        // Apply to mode-specific components
        if (optoPanel.container->isVisible())
        {
            optoPanel.peakReductionKnob->setLookAndFeel(currentLookAndFeel);
            optoPanel.gainKnob->setLookAndFeel(currentLookAndFeel);
            optoPanel.limitSwitch->setLookAndFeel(currentLookAndFeel);
        }
        else if (fetPanel.container->isVisible())
        {
            fetPanel.inputKnob->setLookAndFeel(currentLookAndFeel);
            fetPanel.outputKnob->setLookAndFeel(currentLookAndFeel);
            fetPanel.attackKnob->setLookAndFeel(currentLookAndFeel);
            fetPanel.releaseKnob->setLookAndFeel(currentLookAndFeel);

            // Set ratio button accent color based on mode
            if (fetPanel.ratioButtons)
            {
                if (currentMode == 4) // Studio FET - teal/cyan
                    fetPanel.ratioButtons->setAccentColor(juce::Colour(0xFF00E5E5));
                else // Vintage FET - amber/orange
                    fetPanel.ratioButtons->setAccentColor(juce::Colour(0xFFFFAA00));
            }
        }
        else if (vcaPanel.container->isVisible())
        {
            vcaPanel.thresholdKnob->setLookAndFeel(currentLookAndFeel);
            vcaPanel.ratioKnob->setLookAndFeel(currentLookAndFeel);
            vcaPanel.attackKnob->setLookAndFeel(currentLookAndFeel);
            // No release knob for Classic VCA
            vcaPanel.outputKnob->setLookAndFeel(currentLookAndFeel);
            vcaPanel.overEasyButton->setLookAndFeel(currentLookAndFeel);
        }
        else if (busPanel.container->isVisible())
        {
            busPanel.thresholdKnob->setLookAndFeel(currentLookAndFeel);
            busPanel.ratioKnob->setLookAndFeel(currentLookAndFeel);
            busPanel.attackSelector->setLookAndFeel(currentLookAndFeel);
            busPanel.releaseSelector->setLookAndFeel(currentLookAndFeel);
            busPanel.makeupKnob->setLookAndFeel(currentLookAndFeel);
        }
    }
    
    // Don't resize window when changing modes - keep consistent 700x500 size
    // All modes should fit within this size
    
    resized();
    repaint();
}

void EnhancedCompressorEditor::paint(juce::Graphics& g)
{
    // Draw background based on current mode - darker, more professional colors
    juce::Colour bgColor;
    switch (currentMode)
    {
        case 0: bgColor = juce::Colour(0xFF3A342D); break; // Opto - dark brown/gray
        case 1: bgColor = juce::Colour(0xFF1A1A1A); break; // FET - black (keep as is)
        case 2: bgColor = juce::Colour(0xFF2D3436); break; // VCA - dark gray
        case 3: bgColor = juce::Colour(0xFF2C3E50); break; // Bus - dark blue (keep as is)
        case 4: bgColor = juce::Colour(0xFF1A1A1A); break; // Studio FET - black (same as FET)
        case 5: bgColor = juce::Colour(0xFF2A1518); break; // Studio VCA - dark red (handled by panel)
        case 6: bgColor = juce::Colour(0xFF1A1A2E); break; // Digital - modern dark blue
        default: bgColor = juce::Colour(0xFF2A2A2A); break;
    }
    
    g.fillAll(bgColor);
    
    // Draw texture overlay
    g.setTiledImageFill(backgroundTexture, 0, 0, 1.0f);
    g.fillAll();
    
    // Draw panel frame
    auto bounds = getLocalBounds();
    g.setColour(bgColor.darker(0.3f));
    g.drawRect(bounds, 2);
    
    // Draw inner bevel
    g.setColour(bgColor.brighter(0.2f));
    g.drawRect(bounds.reduced(2), 1);
    
    // Draw title based on mode - all light text for dark backgrounds
    // Note: Digital (mode 6) and Studio VCA (mode 5) panels draw their own titles
    juce::String title;
    juce::String description;  // Brief description of each compressor type
    juce::Colour textColor;
    switch (currentMode)
    {
        case 0:
            title = "OPTO COMPRESSOR";
            description = "LA-2A Style | Program Dependent | Smooth Compression";
            textColor = juce::Colour(0xFFE8D5B7);  // Warm light color
            break;
        case 1:
            title = "FET COMPRESSOR";
            description = "1176 Style | Fast Attack | Punchy Saturation";
            textColor = juce::Colour(0xFFE0E0E0);  // Light gray (keep)
            break;
        case 2:
            title = "VCA COMPRESSOR";
            description = "DBX 160 Style | Over Easy Knee | Fast Response";
            textColor = juce::Colour(0xFFDFE6E9);  // Light gray-blue
            break;
        case 3:
            title = "BUS COMPRESSOR";
            description = "SSL Style | Mix Bus Glue | Analog Character";
            textColor = juce::Colour(0xFFECF0F1);  // Light gray (keep)
            break;
        case 4:
            title = "STUDIO FET COMPRESSOR";
            description = "Modern FET | Clean with 30% Harmonics | Versatile";
            textColor = juce::Colour(0xFFE0E0E0);  // Light gray
            break;
        case 5:
            // Studio VCA panel draws its own title, but we draw description at bottom
            title = "";
            description = "RMS Detection | Soft Knee | Clean VCA Dynamics";
            textColor = juce::Colour(0xFFCC9999);  // Light red tint matching Studio VCA theme
            break;
        case 6:
            title = "DIGITAL COMPRESSOR";
            description = "Transparent | Precise | Zero Coloration";
            textColor = juce::Colour(0xFF00D4FF);  // Cyan
            break;
        default:
            title = "MULTI-COMP";
            description = "4-Band Multiband Compression";
            textColor = juce::Colour(0xFFE0E0E0);
            break;
    }

    // Draw title in a smaller area that doesn't overlap with controls
    // Skip drawing for modes that handle their own titles
    auto titleBounds = bounds.removeFromTop(35 * scaleFactor).withTrimmedLeft(200 * scaleFactor).withTrimmedRight(200 * scaleFactor);
    if (title.isNotEmpty())
    {
        // Draw subtle glow behind title for emphasis
        g.setColour(textColor.withAlpha(0.15f));
        g.setFont(juce::Font(juce::FontOptions(20.0f * scaleFactor).withStyle("Bold")));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    g.drawText(title, titleBounds.translated(dx, dy), juce::Justification::centred);

        // Draw main title text
        g.setColour(textColor);
        g.drawText(title, titleBounds, juce::Justification::centred);
    }

    // Draw description at bottom of window (consistent position for all modes)
    if (description.isNotEmpty())
    {
        auto descBounds = getLocalBounds().removeFromBottom(static_cast<int>(22 * scaleFactor));
        descBounds = descBounds.withTrimmedLeft(static_cast<int>(60 * scaleFactor))
                               .withTrimmedRight(static_cast<int>(60 * scaleFactor));
        g.setColour(textColor.withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(10.0f * scaleFactor)));
        g.drawText(description, descBounds, juce::Justification::centred);
    }

    // Draw "Oversampling" label before oversampling dropdown
    if (!osLabelBounds.isEmpty())
    {
        g.setColour(textColor);
        g.setFont(juce::Font(juce::FontOptions(12.0f * scaleFactor).withStyle("Bold")));
        g.drawText("Oversampling", osLabelBounds, juce::Justification::centredRight);
    }

    // Draw "SC HP" label above sidechain HP knob (centered)
    if (!scHpLabelBounds.isEmpty())
    {
        g.setColour(textColor);
        g.setFont(juce::Font(juce::FontOptions(11.0f * scaleFactor).withStyle("Bold")));
        g.drawText("SC HP", scHpLabelBounds, juce::Justification::centred);
    }

    // Draw meter labels and values using standard LEDMeterStyle
    if (inputMeter)
    {
        LEDMeterStyle::drawMeterLabels(g, inputMeter->getBounds(), "INPUT", displayedInputLevel, scaleFactor);
    }

    if (outputMeter)
    {
        LEDMeterStyle::drawMeterLabels(g, outputMeter->getBounds(), "OUTPUT", displayedOutputLevel, scaleFactor);
    }

    // Draw VU meter label below the VU meter
    // Calculate the same position as in resized() method
    auto vuBounds = getLocalBounds();
    vuBounds.removeFromTop(60 * scaleFactor);  // Header row
    auto vuMainArea = vuBounds.reduced(20 * scaleFactor, 10 * scaleFactor);
    int meterAreaWidth = static_cast<int>(LEDMeterStyle::meterAreaWidth * scaleFactor);
    vuMainArea.removeFromLeft(meterAreaWidth);
    vuMainArea.removeFromRight(meterAreaWidth);
    vuMainArea.reduce(20 * scaleFactor, 0);
    auto vuArea = vuMainArea.removeFromTop(190 * scaleFactor);  // Match resized() VU size
    auto vuLabelArea = vuMainArea.removeFromTop(25 * scaleFactor);
    g.setColour(textColor);
    g.drawText("GAIN REDUCTION", vuLabelArea, juce::Justification::centred);
}

void EnhancedCompressorEditor::resized()
{
    auto bounds = getLocalBounds();
    
    // Calculate scale factor based on window size
    float widthScale = getWidth() / 750.0f;  // Base size is now 750x500
    float heightScale = getHeight() / 500.0f;
    scaleFactor = juce::jmin(widthScale, heightScale);  // Use the smaller scale to maintain proportions
    
    // Position resizer in corner
    if (resizer)
        resizer->setBounds(getWidth() - 16, getHeight() - 16, 16, 16);
    
    // Set up clickable area for title (click to show Patreon supporters)
    titleClickArea = juce::Rectangle<int>(
        static_cast<int>(200 * scaleFactor),
        0,
        static_cast<int>(getWidth() - 400 * scaleFactor),
        static_cast<int>(35 * scaleFactor));

    // ========================================================================
    // TOP HEADER - Aligned with INPUT label (left) to OUTPUT label (right)
    // Row: [Mode] [Bypass] [AutoGain] [AnalogNoise] [ModeToggle] ... [Oversampling dropdown]
    // ========================================================================

    // Header row - below title, single clean row
    // Left margin matches main area (20px) so mode selector aligns with INPUT label
    auto headerRow = bounds.removeFromTop(60 * scaleFactor).withTrimmedTop(35 * scaleFactor);
    headerRow.reduce(20 * scaleFactor, 2 * scaleFactor);

    const int gap = static_cast<int>(10 * scaleFactor);
    const int controlHeight = static_cast<int>(22 * scaleFactor);

    // Control widths
    const int modeSelectorWidth = static_cast<int>(115 * scaleFactor);  // "Bus Compressor"
    const int toggleWidth = static_cast<int>(65 * scaleFactor);         // "Bypass" button
    const int autoGainWidth = static_cast<int>(80 * scaleFactor);       // "Auto Gain" button
    const int analogNoiseWidth = static_cast<int>(95 * scaleFactor);    // "Analog Noise" button
    const int modeToggleWidth = static_cast<int>(70 * scaleFactor);     // "Limit" / "Over Easy"
    const int osLabelWidth = static_cast<int>(80 * scaleFactor);        // "Oversampling" label
    const int osWidth = static_cast<int>(55 * scaleFactor);             // Dropdown for "2x"/"4x"

    // LEFT: Mode selector dropdown (aligned with INPUT label)
    // Add small offset to align with meter center
    headerRow.removeFromLeft(static_cast<int>(8 * scaleFactor));
    if (modeSelector)
    {
        auto area = headerRow.removeFromLeft(modeSelectorWidth);
        modeSelector->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }

    // RIGHT: Oversampling label + dropdown (aligned with OUTPUT label)
    if (oversamplingSelector)
    {
        auto area = headerRow.removeFromRight(osWidth);
        oversamplingSelector->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromRight(static_cast<int>(4 * scaleFactor));  // Small gap
    osLabelBounds = headerRow.removeFromRight(osLabelWidth).withHeight(controlHeight);
    osLabelBounds = osLabelBounds.withY(headerRow.getY() + (headerRow.getHeight() - controlHeight) / 2);

    // CENTER: Calculate total width of center controls and center them in remaining space
    bool isAnalogMode = (currentMode != 6 && currentMode != 7);
    bool showModeToggle = (currentMode == 0 || currentMode == 2);  // Limit for Opto, OverEasy for VCA

    int centerControlsWidth = toggleWidth + gap + autoGainWidth;  // Bypass + Auto Gain
    if (isAnalogMode)
        centerControlsWidth += gap + analogNoiseWidth;  // + Analog Noise
    if (showModeToggle)
        centerControlsWidth += gap + modeToggleWidth;  // + Limit/OverEasy

    int centerStartX = headerRow.getX() + (headerRow.getWidth() - centerControlsWidth) / 2;
    int centerY = headerRow.getCentreY() - controlHeight / 2;

    // Bypass toggle
    if (bypassButton)
    {
        bypassButton->setBounds(centerStartX, centerY, toggleWidth, controlHeight);
        centerStartX += toggleWidth + gap;
    }

    // Auto Gain toggle
    if (autoGainButton)
    {
        autoGainButton->setBounds(centerStartX, centerY, autoGainWidth, controlHeight);
        centerStartX += autoGainWidth + gap;
    }

    // Analog Noise toggle - only visible for analog modes (not Digital=6 or Multiband=7)
    if (analogNoiseButton)
    {
        analogNoiseButton->setVisible(isAnalogMode);
        if (isAnalogMode)
        {
            analogNoiseButton->setBounds(centerStartX, centerY, analogNoiseWidth, controlHeight);
            centerStartX += analogNoiseWidth + gap;
        }
    }

    // Mode-specific toggle (Limit for Opto, OverEasy for VCA)
    if (optoPanel.limitSwitch)
    {
        optoPanel.limitSwitch->setVisible(currentMode == 0);
        if (currentMode == 0)
            optoPanel.limitSwitch->setBounds(centerStartX, centerY, modeToggleWidth, controlHeight);
    }
    if (vcaPanel.overEasyButton)
    {
        vcaPanel.overEasyButton->setVisible(currentMode == 2);
        if (currentMode == 2)
            vcaPanel.overEasyButton->setBounds(centerStartX, centerY, modeToggleWidth, controlHeight);
    }

    // Hide Ext SC and SC Listen - still functional via DAW automation
    if (sidechainEnableButton)
        sidechainEnableButton->setVisible(false);
    if (sidechainListenButton)
        sidechainListenButton->setVisible(false);

    // Hide unused controls (sidechain enable/listen are now shown in header)
    if (lookaheadSlider)
        lookaheadSlider->setVisible(false);
    if (scEqToggleButton)
        scEqToggleButton->setVisible(false);
    if (scLowFreqSlider)
        scLowFreqSlider->setVisible(false);
    if (scLowGainSlider)
        scLowGainSlider->setVisible(false);
    if (scHighFreqSlider)
        scHighFreqSlider->setVisible(false);
    if (scHighGainSlider)
        scHighGainSlider->setVisible(false);

    // Main area
    auto mainArea = bounds.reduced(20 * scaleFactor, 10 * scaleFactor);

    // Use standard meter area width from LEDMeterStyle
    int meterAreaWidth = static_cast<int>(LEDMeterStyle::meterAreaWidth * scaleFactor);
    int meterWidth = static_cast<int>(LEDMeterStyle::standardWidth * scaleFactor);
    int labelSpace = static_cast<int>((LEDMeterStyle::labelHeight + LEDMeterStyle::labelSpacing) * scaleFactor);
    int valueSpace = static_cast<int>((LEDMeterStyle::valueHeight + LEDMeterStyle::labelSpacing) * scaleFactor);

    // Left meter - leave space for labels above and below
    auto leftMeter = mainArea.removeFromLeft(meterAreaWidth);
    leftMeter.removeFromTop(labelSpace);  // Space for "INPUT" label
    if (inputMeter)
    {
        auto meterArea = leftMeter.removeFromTop(leftMeter.getHeight() - valueSpace);
        // Center the meter within the area
        int meterX = meterArea.getX() + (meterArea.getWidth() - meterWidth) / 2;
        inputMeter->setBounds(meterX, meterArea.getY(), meterWidth, meterArea.getHeight());
    }

    // Right meter - leave space for labels above and below
    auto rightMeter = mainArea.removeFromRight(meterAreaWidth);
    rightMeter.removeFromTop(labelSpace);  // Space for "OUTPUT" label
    if (outputMeter)
    {
        auto meterArea = rightMeter.removeFromTop(rightMeter.getHeight() - valueSpace);
        // Center the meter within the area
        int meterX = meterArea.getX() + (meterArea.getWidth() - meterWidth) / 2;
        outputMeter->setBounds(meterX, meterArea.getY(), meterWidth, meterArea.getHeight());
    }
    
    // Center area
    mainArea.reduce(20 * scaleFactor, 0);

    // Presets are exposed via DAW's native preset menu (no UI dropdowns needed)

    // VU Meter at top center with SC HP vertical slider to the right
    auto vuArea = mainArea.removeFromTop(static_cast<int>(190 * scaleFactor));  // More space without preset selectors

    // SC HP slider area on the right side of VU meter (tight against VU)
    const int scHpSliderWidth = static_cast<int>(28 * scaleFactor);   // Narrow vertical slider
    const int scHpAreaWidth = static_cast<int>(34 * scaleFactor);     // Minimal width to sit close to VU
    auto scHpArea = vuArea.removeFromRight(scHpAreaWidth);

    // Position SC HP vertical slider with label above (hidden in multiband mode)
    if (sidechainHpSlider && sidechainHpSlider->isVisible())
    {
        int labelHeight = static_cast<int>(16 * scaleFactor);
        int textBoxHeight = static_cast<int>(18 * scaleFactor);
        int sliderHeight = scHpArea.getHeight() - labelHeight - textBoxHeight - static_cast<int>(8 * scaleFactor);

        // Store label bounds for drawing in paint()
        scHpLabelBounds = juce::Rectangle<int>(
            scHpArea.getX(), scHpArea.getY(),
            scHpArea.getWidth(), labelHeight
        );

        // Position slider below label, centered horizontally
        int sliderX = scHpArea.getX() + (scHpArea.getWidth() - scHpSliderWidth) / 2;
        sidechainHpSlider->setBounds(sliderX, scHpArea.getY() + labelHeight,
                                      scHpSliderWidth, sliderHeight + textBoxHeight);
    }
    else
    {
        // Clear label bounds when slider is hidden (multiband mode)
        scHpLabelBounds = juce::Rectangle<int>();
    }

    // VU meter centered in remaining area
    if (vuMeter)
    {
        // Remove equal space from left to balance the SC HP slider on right
        vuArea.removeFromLeft(scHpAreaWidth);
        vuMeter->setBounds(vuArea.reduced(static_cast<int>(30 * scaleFactor), static_cast<int>(5 * scaleFactor)));
    }

    // Add space for "GAIN REDUCTION" text below VU meter
    mainArea.removeFromTop(25 * scaleFactor);
    
    // Control panel area
    auto controlArea = mainArea.reduced(10 * scaleFactor, 20 * scaleFactor);

    // ========================================================================
    // STANDARDIZED KNOB LAYOUT CONSTANTS
    // All panels use these same values for consistent appearance
    // ========================================================================
    const int stdLabelHeight = static_cast<int>(22 * scaleFactor);
    const int stdKnobSize = static_cast<int>(75 * scaleFactor);  // Fixed knob size for all modes
    const int stdKnobSpacing = static_cast<int>(8 * scaleFactor);
    const int stdKnobRowHeight = stdLabelHeight + stdKnobSize + static_cast<int>(10 * scaleFactor);

    // Helper lambda to layout a single knob with label above
    auto layoutKnob = [&](juce::Slider* knob, juce::Label* label, juce::Rectangle<int> area) {
        if (label)
            label->setBounds(area.removeFromTop(stdLabelHeight));
        if (knob) {
            // Center the knob horizontally in the area
            int knobX = area.getX() + (area.getWidth() - stdKnobSize) / 2;
            knob->setBounds(knobX, area.getY(), stdKnobSize, stdKnobSize);
        }
    };

    // Layout Opto panel - 3 knobs (Peak Reduction, Gain, Mix) centered
    // Uses same knob size as other modes for consistency when switching
    if (optoPanel.container && optoPanel.container->isVisible())
    {
        optoPanel.container->setBounds(controlArea);

        auto optoBounds = optoPanel.container->getLocalBounds();

        // Use standard knob row height for consistent vertical alignment across modes
        auto knobRow = optoBounds.withHeight(stdKnobRowHeight);
        knobRow.setY((optoBounds.getHeight() - stdKnobRowHeight) / 2);

        // Use 3-column grid for 3 knobs centered
        int colWidth = knobRow.getWidth() / 3;

        auto peakArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(optoPanel.peakReductionKnob.get(), optoPanel.peakReductionLabel.get(), peakArea);

        auto gainArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(optoPanel.gainKnob.get(), optoPanel.gainLabel.get(), gainArea);

        auto mixArea = knobRow;
        layoutKnob(optoPanel.mixKnob.get(), optoPanel.mixLabel.get(), mixArea);
    }

    // Layout FET panel - 5 knobs + ratio buttons below
    if (fetPanel.container && fetPanel.container->isVisible())
    {
        fetPanel.container->setBounds(controlArea);

        auto fetBounds = fetPanel.container->getLocalBounds();
        auto knobRow = fetBounds.removeFromTop(stdKnobRowHeight);

        int colWidth = knobRow.getWidth() / 5;

        auto inputArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(fetPanel.inputKnob.get(), fetPanel.inputLabel.get(), inputArea);

        auto outputArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(fetPanel.outputKnob.get(), fetPanel.outputLabel.get(), outputArea);

        auto attackArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(fetPanel.attackKnob.get(), fetPanel.attackLabel.get(), attackArea);

        auto releaseArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(fetPanel.releaseKnob.get(), fetPanel.releaseLabel.get(), releaseArea);

        auto mixArea = knobRow;
        layoutKnob(fetPanel.mixKnob.get(), fetPanel.mixLabel.get(), mixArea);

        // Ratio buttons below knobs
        if (fetPanel.ratioButtons)
            fetPanel.ratioButtons->setBounds(fetBounds.removeFromTop(static_cast<int>(70 * scaleFactor)).reduced(static_cast<int>(15 * scaleFactor), static_cast<int>(2 * scaleFactor)));
    }

    // Layout VCA panel - 5 knobs in one row (no release for Classic VCA)
    if (vcaPanel.container && vcaPanel.container->isVisible())
    {
        vcaPanel.container->setBounds(controlArea);

        auto vcaBounds = vcaPanel.container->getLocalBounds();

        // Center the knob row vertically
        auto knobRow = vcaBounds.withHeight(stdKnobRowHeight);
        knobRow.setY((vcaBounds.getHeight() - stdKnobRowHeight) / 2);

        int colWidth = knobRow.getWidth() / 5;

        auto thresholdBounds = knobRow.removeFromLeft(colWidth);
        layoutKnob(vcaPanel.thresholdKnob.get(), vcaPanel.thresholdLabel.get(), thresholdBounds);

        auto ratioBounds = knobRow.removeFromLeft(colWidth);
        layoutKnob(vcaPanel.ratioKnob.get(), vcaPanel.ratioLabel.get(), ratioBounds);

        auto attackBounds = knobRow.removeFromLeft(colWidth);
        layoutKnob(vcaPanel.attackKnob.get(), vcaPanel.attackLabel.get(), attackBounds);

        auto outputBounds = knobRow.removeFromLeft(colWidth);
        layoutKnob(vcaPanel.outputKnob.get(), vcaPanel.outputLabel.get(), outputBounds);

        auto mixBounds = knobRow;
        layoutKnob(vcaPanel.mixKnob.get(), vcaPanel.mixLabel.get(), mixBounds);
    }

    // Layout Bus panel - 4 knobs on top row, 2 dropdown selectors below (aligned with knob pairs)
    if (busPanel.container && busPanel.container->isVisible())
    {
        // Give Bus panel extra vertical space for the dropdown selectors
        auto busArea = controlArea.withTrimmedBottom(-40 * scaleFactor);
        busPanel.container->setBounds(busArea);

        auto busBounds = busPanel.container->getLocalBounds();

        // Top row: 4 knobs (Threshold, Ratio, Makeup, Mix)
        auto knobRow = busBounds.removeFromTop(stdKnobRowHeight);

        // Use 4-column grid with small margin - spreads knobs to match VU meter width
        int margin = static_cast<int>(10 * scaleFactor);
        knobRow.reduce(margin, 0);
        int colWidth = knobRow.getWidth() / 4;

        auto thresholdArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(busPanel.thresholdKnob.get(), busPanel.thresholdLabel.get(), thresholdArea);

        auto ratioArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(busPanel.ratioKnob.get(), busPanel.ratioLabel.get(), ratioArea);

        auto makeupArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(busPanel.makeupKnob.get(), busPanel.makeupLabel.get(), makeupArea);

        auto mixArea = knobRow.removeFromLeft(colWidth);
        layoutKnob(busPanel.mixKnob.get(), busPanel.mixLabel.get(), mixArea);

        // Bottom row: Attack/Release dropdowns - centered under knob pairs
        busBounds.removeFromTop(static_cast<int>(15 * scaleFactor));  // Spacing
        auto bottomRow = busBounds.removeFromTop(static_cast<int>(55 * scaleFactor));
        bottomRow.reduce(margin, 0);

        // Attack dropdown: centered between Threshold and Ratio
        int dropdownWidth = static_cast<int>(80 * scaleFactor);
        int attackCenterX = margin + colWidth;  // Center between first two knobs
        auto attackArea = bottomRow.withX(attackCenterX - dropdownWidth / 2).withWidth(dropdownWidth);
        busPanel.attackLabel->setBounds(attackArea.removeFromTop(stdLabelHeight));
        busPanel.attackSelector->setBounds(attackArea.removeFromTop(static_cast<int>(28 * scaleFactor)));

        // Release dropdown: centered between Makeup and Mix
        int releaseCenterX = margin + colWidth * 3;  // Center between last two knobs
        auto releaseArea = bottomRow.withX(releaseCenterX - dropdownWidth / 2).withWidth(dropdownWidth);
        busPanel.releaseLabel->setBounds(releaseArea.removeFromTop(stdLabelHeight));
        busPanel.releaseSelector->setBounds(releaseArea.removeFromTop(static_cast<int>(28 * scaleFactor)));
    }

    // Layout Digital panel - needs more vertical space for 2 rows of knobs
    if (digitalPanel && digitalPanel->isVisible())
    {
        digitalPanel->setScaleFactor(scaleFactor);
        // Give Digital panel significantly more vertical space
        auto digitalArea = controlArea.withTrimmedTop(-25 * scaleFactor).withTrimmedBottom(-35 * scaleFactor);
        digitalPanel->setBounds(digitalArea);
    }

    // Layout Studio VCA panel
    if (studioVcaPanel && studioVcaPanel->isVisible())
    {
        studioVcaPanel->setScaleFactor(scaleFactor);
        studioVcaPanel->setBounds(controlArea);
    }

    // Layout Multiband panel - uses full center area since VU meter is hidden
    if (multibandPanel && multibandPanel->isVisible())
    {
        multibandPanel->setScaleFactor(scaleFactor);
        // Since VU meter is hidden in multiband mode, use the full vertical space
        // Start from just below the preset selectors and extend to the bottom
        auto multibandArea = mainArea;
        // Reclaim the VU meter space (vuArea + label space that was removed earlier)
        multibandArea.setY(multibandArea.getY() - static_cast<int>(200 * scaleFactor));  // Reclaim VU area
        multibandArea.setHeight(multibandArea.getHeight() + static_cast<int>(200 * scaleFactor));
        multibandArea.reduce(10 * scaleFactor, 5 * scaleFactor);
        multibandPanel->setBounds(multibandArea);
    }
}

void EnhancedCompressorEditor::timerCallback()
{
    updateMeters();
}

void EnhancedCompressorEditor::updateMeters()
{
    if (inputMeter)
    {
        // LEDMeter expects dB values, not linear
        // Use stereo levels for L/R display
        float inputDbL = processor.getInputLevelL();
        float inputDbR = processor.getInputLevelR();
        inputMeter->setStereoLevels(inputDbL, inputDbR);

        // Apply smoothing for internal tracking (use max for display text)
        float inputDb = std::max(inputDbL, inputDbR);
        smoothedInputLevel = smoothedInputLevel * levelSmoothingFactor +
                           inputDb * (1.0f - levelSmoothingFactor);
    }

    if (vuMeter && vuMeter->isVisible())
    {
        vuMeter->setLevel(processor.getGainReduction());
        // Pass GR history for the history graph view (thread-safe atomic reads)
        vuMeter->setGRHistory(processor);
    }

    // Update multiband per-band GR meters
    if (multibandPanel && multibandPanel->isVisible())
    {
        for (int band = 0; band < 4; ++band)
        {
            multibandPanel->setBandGainReduction(band, processor.getBandGainReduction(band));
        }
    }

    if (outputMeter)
    {
        // LEDMeter expects dB values, not linear
        // Use stereo levels for L/R display
        float outputDbL = processor.getOutputLevelL();
        float outputDbR = processor.getOutputLevelR();
        outputMeter->setStereoLevels(outputDbL, outputDbR);

        // Apply smoothing for internal tracking (use max for display text)
        float outputDb = std::max(outputDbL, outputDbR);
        smoothedOutputLevel = smoothedOutputLevel * levelSmoothingFactor +
                            outputDb * (1.0f - levelSmoothingFactor);
    }

    // Throttle the text display updates to make them more readable
    levelDisplayCounter++;
    if (levelDisplayCounter >= levelDisplayInterval)
    {
        levelDisplayCounter = 0;
        displayedInputLevel = smoothedInputLevel;
        displayedOutputLevel = smoothedOutputLevel;

        // Only repaint when the displayed values actually update
        repaint(inputMeter ? inputMeter->getBounds().expanded(20, 30) : juce::Rectangle<int>());
        repaint(outputMeter ? outputMeter->getBounds().expanded(20, 30) : juce::Rectangle<int>());
    }
}

void EnhancedCompressorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "mode")
    {
        // Must dispatch to message thread - parameterChanged can be called from audio thread
        // during automation, which would crash if we access UI components directly
        // Use SafePointer to prevent accessing destroyed editor
        auto safeThis = juce::Component::SafePointer<EnhancedCompressorEditor>(this);
        juce::MessageManager::callAsync([safeThis, newValue]() {
            if (auto* editor = safeThis.getComponent())
            {
                // Skip if presetChanged already handled this mode update
                // (prevents race condition where stale parameterChanged overwrites correct mode)
                if (editor->ignoreNextModeChange)
                {
                    editor->ignoreNextModeChange = false;
                    return;
                }

                int newMode = static_cast<int>(newValue);
                // Update combo box to match (add 1 for 1-based ID)
                if (editor->modeSelector)
                    editor->modeSelector->setSelectedId(newMode + 1, juce::dontSendNotification);
                editor->updateMode(newMode);
            }
        });
    }
    else if (parameterID == "auto_makeup")
    {
        // Update output knob enabled state based on auto-gain
        // Use SafePointer to prevent accessing destroyed editor
        auto safeThis = juce::Component::SafePointer<EnhancedCompressorEditor>(this);
        juce::MessageManager::callAsync([safeThis, newValue]() {
            if (auto* editor = safeThis.getComponent())
            {
                editor->updateAutoGainState(newValue > 0.5f);
            }
        });
    }
}

void EnhancedCompressorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == modeSelector.get())
    {
        int selectedMode = modeSelector->getSelectedId() - 1;
        updateMode(selectedMode);
    }
}

void EnhancedCompressorEditor::ratioChanged(int ratioIndex)
{
    // Handle FET ratio button changes
    // Map to parameter value if needed
    auto& params = processor.getParameters();
    if (auto* ratioParam = params.getParameter("fet_ratio"))
    {
        float normalizedValue = ratioIndex / 4.0f;
        ratioParam->setValueNotifyingHost(normalizedValue);
    }
}

void EnhancedCompressorEditor::presetChanged(int /*presetIndex*/, int targetMode)
{
    // Called when a preset is loaded via DAW's preset menu
    // Force UI refresh for hosts that don't properly trigger parameter updates (e.g., Bitwig)
    //
    // targetMode is passed directly from the preset definition, so we don't need to read
    // from parameters (which may not have propagated yet if called from non-message thread).

    if (targetMode >= 0)
    {
        // Set flag to prevent parameterChanged from reverting our mode update
        // (there may be a pending async parameterChanged call with the old mode value)
        ignoreNextModeChange = true;

        // Update combo box directly
        if (modeSelector)
            modeSelector->setSelectedId(targetMode + 1, juce::dontSendNotification);

        // Update mode UI
        updateMode(targetMode);
    }

    // Re-read auto-makeup state from parameters (this is typically already propagated)
    auto& params = processor.getParameters();
    const auto* autoMakeupParam = params.getRawParameterValue("auto_makeup");
    if (autoMakeupParam)
        updateAutoGainState(autoMakeupParam->load() > 0.5f);

    // Trigger full repaint to refresh all sliders/knobs
    repaint();
}

void EnhancedCompressorEditor::updateAutoGainState(bool autoGainEnabled)
{
    // When auto-gain is enabled, disable output/makeup/gain knobs since auto-gain controls them
    const float disabledAlpha = 0.4f;
    const float enabledAlpha = 1.0f;

    // Opto mode - Gain knob
    if (optoPanel.gainKnob)
    {
        optoPanel.gainKnob->setEnabled(!autoGainEnabled);
        optoPanel.gainKnob->setAlpha(autoGainEnabled ? disabledAlpha : enabledAlpha);
    }

    // FET mode - Output knob
    if (fetPanel.outputKnob)
    {
        fetPanel.outputKnob->setEnabled(!autoGainEnabled);
        fetPanel.outputKnob->setAlpha(autoGainEnabled ? disabledAlpha : enabledAlpha);
    }

    // VCA mode - Output knob
    if (vcaPanel.outputKnob)
    {
        vcaPanel.outputKnob->setEnabled(!autoGainEnabled);
        vcaPanel.outputKnob->setAlpha(autoGainEnabled ? disabledAlpha : enabledAlpha);
    }

    // Bus mode - Makeup knob
    if (busPanel.makeupKnob)
    {
        busPanel.makeupKnob->setEnabled(!autoGainEnabled);
        busPanel.makeupKnob->setAlpha(autoGainEnabled ? disabledAlpha : enabledAlpha);
    }

    // Studio VCA panel - handled internally by the panel
    if (studioVcaPanel)
        studioVcaPanel->setAutoGainEnabled(autoGainEnabled);

    // Digital panel - output knob
    if (digitalPanel)
        digitalPanel->setAutoGainEnabled(autoGainEnabled);

    // Multiband panel - global output knob
    if (multibandPanel)
        multibandPanel->setAutoGainEnabled(autoGainEnabled);
}

//==============================================================================
// Supporters Overlay (uses shared SupportersOverlay component)
//==============================================================================
void EnhancedCompressorEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("Multi-Comp");
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }
    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->setVisible(true);
    supportersOverlay->toFront(true);
}

void EnhancedCompressorEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}

void EnhancedCompressorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
    {
        showSupportersPanel();
    }
}

// Presets are now exposed via DAW's native preset menu (getNumPrograms/setCurrentProgram/getProgramName)