#include "EnhancedCompressorEditor.h"
#include <cmath>

//==============================================================================
EnhancedCompressorEditor::EnhancedCompressorEditor(UniversalCompressor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Initialize look and feels
    optoLookAndFeel = std::make_unique<OptoLookAndFeel>();
    fetLookAndFeel = std::make_unique<FETLookAndFeel>();
    vcaLookAndFeel = std::make_unique<VCALookAndFeel>();
    busLookAndFeel = std::make_unique<BusLookAndFeel>();
    studioVcaLookAndFeel = std::make_unique<StudioVCALookAndFeel>();
    digitalLookAndFeel = std::make_unique<DigitalLookAndFeel>();
    
    // Create background texture
    createBackgroundTexture();
    
    // Create meters
    inputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    vuMeter = std::make_unique<VUMeterWithLabel>();
    outputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    
    addAndMakeVisible(inputMeter.get());
    addAndMakeVisible(vuMeter.get());
    addAndMakeVisible(outputMeter.get());
    
    // Create mode selector - 7 modes matching Logic Pro style
    modeSelector = std::make_unique<juce::ComboBox>("Mode");
    modeSelector->addItem("Vintage Opto", 1);
    modeSelector->addItem("Vintage FET", 2);
    modeSelector->addItem("Classic VCA", 3);
    modeSelector->addItem("Bus Compressor", 4);
    modeSelector->addItem("Studio FET", 5);
    modeSelector->addItem("Studio VCA", 6);
    modeSelector->addItem("Digital", 7);
    // Don't set a default - let the attachment handle it
    // Remove listener - the attachment and parameterChanged handle it
    addAndMakeVisible(modeSelector.get());
    
    // Create global controls with full readable labels
    bypassButton = std::make_unique<juce::ToggleButton>("Bypass");
    autoGainButton = std::make_unique<juce::ToggleButton>("Auto Gain");
    sidechainEnableButton = std::make_unique<juce::ToggleButton>("Ext SC");
    sidechainListenButton = std::make_unique<juce::ToggleButton>("SC Listen");

    // Lookahead slider (not shown in header, but kept for parameter)
    lookaheadSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft);
    lookaheadSlider->setRange(0.0, 10.0, 0.1);
    lookaheadSlider->setTextValueSuffix(" ms");
    lookaheadSlider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 18);

    // Oversampling selector with clear items
    oversamplingSelector = std::make_unique<juce::ComboBox>("Oversampling");
    oversamplingSelector->addItem("2x", 1);
    oversamplingSelector->addItem("4x", 2);
    oversamplingSelector->setSelectedId(1);

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
    addAndMakeVisible(oversamplingSelector.get());
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

    if (params.getRawParameterValue("global_lookahead"))
        lookaheadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "global_lookahead", *lookaheadSlider);

    if (params.getRawParameterValue("oversampling"))
        oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            params, "oversampling", *oversamplingSelector);

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
    if (lookaheadSlider)
        lookaheadSlider->setLookAndFeel(nullptr);
    if (oversamplingSelector)
        oversamplingSelector->setLookAndFeel(nullptr);
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
    optoPanel.peakReductionKnob = createKnob("Peak Reduction", 0, 100, 50, "");
    optoPanel.gainKnob = createKnob("Gain", -20, 20, 0, " dB");
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
    fetPanel.inputKnob = createKnob("Input", 0, 10, 0);
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
    busPanel.thresholdKnob = createKnob("Threshold", -20, 0, -6, " dB");
    busPanel.ratioKnob = createKnob("Ratio", 2, 10, 4, ":1");
    busPanel.makeupKnob = createKnob("Makeup", -10, 20, 0, " dB");
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

    if (params.getRawParameterValue("bus_mix"))
        busPanel.mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "bus_mix", *busPanel.mixKnob);
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

// Multiband panel removed

void EnhancedCompressorEditor::updateMode(int newMode)
{
    currentMode = juce::jlimit(0, 6, newMode);  // 0-6 for 7 modes

    // Hide all panels
    optoPanel.container->setVisible(false);
    fetPanel.container->setVisible(false);
    vcaPanel.container->setVisible(false);
    busPanel.container->setVisible(false);
    if (digitalPanel) digitalPanel->setVisible(false);
    if (studioVcaPanel) studioVcaPanel->setVisible(false);

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

        case 4: // Studio FET - shares FET panel
            fetPanel.container->setVisible(true);
            currentLookAndFeel = fetLookAndFeel.get();  // Use FET look (could customize later)
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
            case 5: // Studio VCA - dark red background
                buttonTextColor = juce::Colour(0xFFD0D0D0);  // Light gray
                break;
            case 6: // Digital - dark blue background
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

        if (oversamplingSelector)
            oversamplingSelector->setLookAndFeel(currentLookAndFeel);

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
    juce::Colour textColor;
    switch (currentMode)
    {
        case 0:
            title = "OPTO COMPRESSOR";
            textColor = juce::Colour(0xFFE8D5B7);  // Warm light color
            break;
        case 1:
            title = "FET COMPRESSOR";
            textColor = juce::Colour(0xFFE0E0E0);  // Light gray (keep)
            break;
        case 2:
            title = "VCA COMPRESSOR";
            textColor = juce::Colour(0xFFDFE6E9);  // Light gray-blue
            break;
        case 3:
            title = "BUS COMPRESSOR";
            textColor = juce::Colour(0xFFECF0F1);  // Light gray (keep)
            break;
        case 4:
            title = "STUDIO FET COMPRESSOR";
            textColor = juce::Colour(0xFFE0E0E0);  // Light gray
            break;
        case 5:
            // Studio VCA panel draws its own title, but we still need visible text color for labels
            title = "";
            textColor = juce::Colour(0xFFCC9999);  // Light red tint matching Studio VCA theme
            break;
        case 6:
            title = "DIGITAL COMPRESSOR";
            textColor = juce::Colour(0xFF00D4FF);  // Cyan
            break;
        default:
            title = "UNIVERSAL COMPRESSOR";
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

    // Draw "Oversampling" label before oversampling dropdown
    if (!osLabelBounds.isEmpty())
    {
        g.setColour(textColor);
        g.setFont(juce::Font(juce::FontOptions(12.0f * scaleFactor).withStyle("Bold")));
        g.drawText("Oversampling", osLabelBounds, juce::Justification::centredRight);
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
    // TOP HEADER - Clean, uniform layout for ALL modes
    // Row: [Mode Selector] [Bypass] [Auto Gain] [Mode Toggle] [Ext SC] [SC Listen] [OS: dropdown]
    // Centered over the VU meter area
    // ========================================================================

    // Header row - below title, single clean row
    auto headerRow = bounds.removeFromTop(60 * scaleFactor).withTrimmedTop(35 * scaleFactor);
    headerRow.reduce(12 * scaleFactor, 2 * scaleFactor);

    const int gap = static_cast<int>(8 * scaleFactor);  // Tighter gaps
    const int controlHeight = static_cast<int>(22 * scaleFactor);  // Slightly taller for readability

    // Calculate total width of all controls to center them
    const int modeSelectorWidth = static_cast<int>(120 * scaleFactor);  // Wider for "Bus Compressor"
    const int toggleWidth = static_cast<int>(70 * scaleFactor);         // "Bypass" button
    const int autoGainWidth = static_cast<int>(85 * scaleFactor);       // "Auto Gain" button
    const int modeToggleWidth = static_cast<int>(70 * scaleFactor);     // Mode-specific toggle
    const int scEnableWidth = static_cast<int>(60 * scaleFactor);       // "Ext SC" button
    const int scListenWidth = static_cast<int>(75 * scaleFactor);       // "SC Listen" button
    const int osLabelWidth = static_cast<int>(78 * scaleFactor);        // "Oversampling" label
    const int osWidth = static_cast<int>(58 * scaleFactor);             // Dropdown for "2x"/"4x" - wider to show full text

    const int totalWidth = modeSelectorWidth + gap + toggleWidth + gap + autoGainWidth + gap
                          + modeToggleWidth + gap + scEnableWidth + gap + scListenWidth + gap + osLabelWidth + osWidth;

    // Center the controls in the header row
    int startX = (headerRow.getWidth() - totalWidth) / 2;
    if (startX < 0) startX = 0;
    headerRow.removeFromLeft(startX);

    // Mode selector dropdown - first in the row
    if (modeSelector)
    {
        auto area = headerRow.removeFromLeft(modeSelectorWidth);
        modeSelector->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromLeft(gap);

    // Bypass toggle - radio button style with full label
    if (bypassButton)
    {
        auto area = headerRow.removeFromLeft(toggleWidth);
        bypassButton->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromLeft(gap);

    // Auto Gain toggle - radio button style with full label
    if (autoGainButton)
    {
        auto area = headerRow.removeFromLeft(autoGainWidth);
        autoGainButton->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromLeft(gap);

    // Mode-specific toggle (Limit for Opto, OverEasy for VCA) - same position for all
    auto modeToggleArea = headerRow.removeFromLeft(modeToggleWidth);
    if (optoPanel.limitSwitch)
    {
        optoPanel.limitSwitch->setVisible(currentMode == 0);
        if (currentMode == 0)
            optoPanel.limitSwitch->setBounds(modeToggleArea.withHeight(controlHeight).withY(modeToggleArea.getCentreY() - controlHeight / 2));
    }
    if (vcaPanel.overEasyButton)
    {
        vcaPanel.overEasyButton->setVisible(currentMode == 2);
        if (currentMode == 2)
            vcaPanel.overEasyButton->setBounds(modeToggleArea.withHeight(controlHeight).withY(modeToggleArea.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromLeft(gap);

    // External Sidechain enable toggle - available for all modes
    if (sidechainEnableButton)
    {
        sidechainEnableButton->setVisible(true);
        auto area = headerRow.removeFromLeft(scEnableWidth);
        sidechainEnableButton->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromLeft(gap);

    // Sidechain Listen toggle - available for all modes
    if (sidechainListenButton)
    {
        sidechainListenButton->setVisible(true);
        auto area = headerRow.removeFromLeft(scListenWidth);
        sidechainListenButton->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }
    headerRow.removeFromLeft(gap);

    // "Oversampling" label area (drawn in paint()) followed by dropdown with small gap
    osLabelBounds = headerRow.removeFromLeft(osLabelWidth).withHeight(controlHeight);
    osLabelBounds = osLabelBounds.withY(headerRow.getY() + (headerRow.getHeight() - controlHeight) / 2);
    headerRow.removeFromLeft(static_cast<int>(4 * scaleFactor));  // Small gap between label and dropdown

    if (oversamplingSelector)
    {
        auto area = headerRow.removeFromLeft(osWidth);
        oversamplingSelector->setBounds(area.withHeight(controlHeight).withY(area.getCentreY() - controlHeight / 2));
    }

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

    // VU Meter at top center - good readable size
    auto vuArea = mainArea.removeFromTop(190 * scaleFactor);  // Increased from 160 to 190
    if (vuMeter)
        vuMeter->setBounds(vuArea.reduced(55 * scaleFactor, 5 * scaleFactor));  // Less horizontal reduction for larger meter

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

        // Use 6-column grid for 4 knobs, skip first and last for centering
        int colWidth = knobRow.getWidth() / 6;
        int skipWidth = colWidth;  // Save for dropdown alignment
        knobRow.removeFromLeft(colWidth);  // Skip first column

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

        // Attack dropdown: centered between Threshold and Ratio (columns 2-3)
        int dropdownWidth = static_cast<int>(80 * scaleFactor);
        int attackCenterX = skipWidth + colWidth;  // Center of Threshold-Ratio gap
        auto attackArea = bottomRow.withX(attackCenterX - dropdownWidth / 2).withWidth(dropdownWidth);
        busPanel.attackLabel->setBounds(attackArea.removeFromTop(stdLabelHeight));
        busPanel.attackSelector->setBounds(attackArea.removeFromTop(static_cast<int>(28 * scaleFactor)));

        // Release dropdown: centered between Makeup and Mix (columns 4-5)
        int releaseCenterX = skipWidth + colWidth * 3;  // Center of Makeup-Mix gap
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

    // Multiband panel removed
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
        float inputDb = processor.getInputLevel();
        inputMeter->setLevel(inputDb);

        // Apply smoothing for internal tracking
        smoothedInputLevel = smoothedInputLevel * levelSmoothingFactor +
                           inputDb * (1.0f - levelSmoothingFactor);
    }

    if (vuMeter)
    {
        vuMeter->setLevel(processor.getGainReduction());
        // Pass GR history for the history graph view
        vuMeter->setGRHistory(processor.getGRHistory(), processor.getGRHistoryWritePos());
    }

    if (outputMeter)
    {
        // LEDMeter expects dB values, not linear
        float outputDb = processor.getOutputLevel();
        outputMeter->setLevel(outputDb);

        // Apply smoothing for internal tracking
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
        const auto* modeParam = processor.getParameters().getRawParameterValue("mode");
        if (modeParam)
        {
            int newMode = static_cast<int>(*modeParam);
            // Update combo box to match (add 1 for 1-based ID)
            modeSelector->setSelectedId(newMode + 1, juce::dontSendNotification);
            updateMode(newMode);
        }
    }
    else if (parameterID == "auto_makeup")
    {
        // Update output knob enabled state based on auto-gain
        juce::MessageManager::callAsync([this, newValue]() {
            updateAutoGainState(newValue > 0.5f);
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
}

//==============================================================================
// Supporters Overlay
//==============================================================================
void EnhancedCompressorEditor::SupportersOverlay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Semi-transparent dark background
    g.setColour(juce::Colour(0xE0101010));
    g.fillAll();

    // Panel background
    auto panelBounds = bounds.reduced(60, 40);
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRoundedRectangle(panelBounds.toFloat(), 12.0f);

    // Panel border
    g.setColour(juce::Colour(0xff404040));
    g.drawRoundedRectangle(panelBounds.toFloat(), 12.0f, 2.0f);

    // Title
    g.setColour(juce::Colour(0xffd4af37));  // Gold color
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("Thank You!", panelBounds.getX(), panelBounds.getY() + 20,
               panelBounds.getWidth(), 30, juce::Justification::centred);

    // Subtitle
    g.setColour(juce::Colour(0xffa0a0a0));
    g.setFont(juce::Font(14.0f));
    g.drawText("To our amazing Patreon supporters",
               panelBounds.getX(), panelBounds.getY() + 55,
               panelBounds.getWidth(), 20, juce::Justification::centred);

    // Divider line
    g.setColour(juce::Colour(0xff404040));
    g.fillRect(panelBounds.getX() + 40, panelBounds.getY() + 90, panelBounds.getWidth() - 80, 1);

    // Supporters list
    auto supportersText = PatreonCredits::getAllBackersFormatted();

    // Text area for supporters
    auto textArea = panelBounds.reduced(40, 0);
    textArea.setY(panelBounds.getY() + 105);
    textArea.setHeight(panelBounds.getHeight() - 170);

    g.setFont(juce::Font(14.0f));
    g.setColour(juce::Colour(0xffd0d0d0));
    g.drawFittedText(supportersText, textArea,
                     juce::Justification::centred, 30);

    // Footer divider
    g.setColour(juce::Colour(0xff404040));
    g.fillRect(panelBounds.getX() + 40, panelBounds.getBottom() - 55, panelBounds.getWidth() - 80, 1);

    // Footer with click-to-close hint
    g.setFont(juce::Font(12.0f));
    g.setColour(juce::Colour(0xff808080));
    g.drawText("Click anywhere to close",
               panelBounds.getX(), panelBounds.getBottom() - 45,
               panelBounds.getWidth(), 20, juce::Justification::centred);

    // Luna Co. Audio credit
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colour(0xff606060));
    g.drawText("Universal Compressor by Luna Co. Audio",
               panelBounds.getX(), panelBounds.getBottom() - 25,
               panelBounds.getWidth(), 18, juce::Justification::centred);
}

void EnhancedCompressorEditor::SupportersOverlay::mouseDown(const juce::MouseEvent&)
{
    if (onDismiss)
        onDismiss();
}

void EnhancedCompressorEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>();
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