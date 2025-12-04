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
    // modernLookAndFeel = std::make_unique<ModernLookAndFeel>(); // TODO: Enable when Digital mode is implemented
    
    // Create background texture
    createBackgroundTexture();
    
    // Create meters
    inputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    vuMeter = std::make_unique<VUMeterWithLabel>();
    outputMeter = std::make_unique<LEDMeter>(LEDMeter::Vertical);
    
    addAndMakeVisible(inputMeter.get());
    addAndMakeVisible(vuMeter.get());
    addAndMakeVisible(outputMeter.get());
    
    // Create mode selector - 6 modes matching Logic Pro style
    modeSelector = std::make_unique<juce::ComboBox>("Mode");
    modeSelector->addItem("Vintage Opto (LA-2A)", 1);
    modeSelector->addItem("Vintage FET (1176)", 2);
    modeSelector->addItem("Classic VCA (DBX 160)", 3);
    modeSelector->addItem("Vintage VCA (SSL G)", 4);
    modeSelector->addItem("Studio FET (1176 Rev E)", 5);
    modeSelector->addItem("Studio VCA (Red 3)", 6);
    // Don't set a default - let the attachment handle it
    // Remove listener - the attachment and parameterChanged handle it
    addAndMakeVisible(modeSelector.get());
    
    // Create global controls
    bypassButton = std::make_unique<juce::ToggleButton>("Bypass");
    // Oversample button removed - saturation always runs at 2x internally
    addAndMakeVisible(bypassButton.get());
    
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
    
    // Oversample attachment removed - no longer user-controllable
    
    // Listen to mode changes
    params.addParameterListener("mode", this);
    
    // Set initial mode
    const auto* modeParam = params.getRawParameterValue("mode");
    currentMode = modeParam ? static_cast<int>(*modeParam) : 0;

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
    setSize(700, 500);  // Comfortable size to fit all controls
    setResizable(true, false);  // Allow resizing, no native title bar
}

EnhancedCompressorEditor::~EnhancedCompressorEditor()
{
    processor.getParameters().removeParameterListener("mode", this);
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
    optoPanel.limitSwitch = std::make_unique<juce::ToggleButton>("Compress / Limit");

    // Create labels
    optoPanel.peakReductionLabel = createLabel("PEAK REDUCTION");
    optoPanel.gainLabel = createLabel("GAIN");
    
    // Add to container
    optoPanel.container->addAndMakeVisible(optoPanel.peakReductionKnob.get());
    optoPanel.container->addAndMakeVisible(optoPanel.gainKnob.get());
    // Note: limitSwitch is added to main editor, not container, so it can be in top row
    addChildComponent(optoPanel.limitSwitch.get());  // Add to main editor as child component
    optoPanel.container->addAndMakeVisible(optoPanel.peakReductionLabel.get());
    optoPanel.container->addAndMakeVisible(optoPanel.gainLabel.get());
    
    // Create attachments
    auto& params = processor.getParameters();
    if (params.getRawParameterValue("opto_peak_reduction"))
        optoPanel.peakReductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "opto_peak_reduction", *optoPanel.peakReductionKnob);
    
    if (params.getRawParameterValue("opto_gain"))
        optoPanel.gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "opto_gain", *optoPanel.gainKnob);
    
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
    fetPanel.ratioButtons = std::make_unique<RatioButtonGroup>();
    fetPanel.ratioButtons->addListener(this);

    // Create labels
    fetPanel.inputLabel = createLabel("INPUT");
    fetPanel.outputLabel = createLabel("OUTPUT");
    fetPanel.attackLabel = createLabel("ATTACK");
    fetPanel.releaseLabel = createLabel("RELEASE");
    
    // Add to container
    fetPanel.container->addAndMakeVisible(fetPanel.inputKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.outputKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.attackKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.releaseKnob.get());
    fetPanel.container->addAndMakeVisible(fetPanel.ratioButtons.get());
    fetPanel.container->addAndMakeVisible(fetPanel.inputLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.outputLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.attackLabel.get());
    fetPanel.container->addAndMakeVisible(fetPanel.releaseLabel.get());
    
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
}

void EnhancedCompressorEditor::setupVCAPanel()
{
    vcaPanel.container = std::make_unique<juce::Component>();
    addChildComponent(vcaPanel.container.get());  // Use addChildComponent so it's initially hidden
    
    // Create controls
    vcaPanel.thresholdKnob = createKnob("Threshold", -40, 0, -12, " dB");
    vcaPanel.ratioKnob = createKnob("Ratio", 1, 20, 4, ":1");
    vcaPanel.attackKnob = createKnob("Attack", 0.1, 100, 1, " ms");
    // DBX 160 has fixed release rate - no release knob needed
    vcaPanel.outputKnob = createKnob("Output", -20, 20, 0, " dB");
    vcaPanel.overEasyButton = std::make_unique<juce::ToggleButton>("Over Easy");

    // Create labels
    vcaPanel.thresholdLabel = createLabel("THRESHOLD");
    vcaPanel.ratioLabel = createLabel("RATIO");
    vcaPanel.attackLabel = createLabel("ATTACK");
    // No release label for DBX 160
    vcaPanel.outputLabel = createLabel("OUTPUT");
    
    // Add to container
    vcaPanel.container->addAndMakeVisible(vcaPanel.thresholdKnob.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.ratioKnob.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.attackKnob.get());
    // No release knob for DBX 160
    vcaPanel.container->addAndMakeVisible(vcaPanel.outputKnob.get());
    // Note: overEasyButton is added to main editor, not container, so it can be in top row
    addChildComponent(vcaPanel.overEasyButton.get());  // Add to main editor as child component
    vcaPanel.container->addAndMakeVisible(vcaPanel.thresholdLabel.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.ratioLabel.get());
    vcaPanel.container->addAndMakeVisible(vcaPanel.attackLabel.get());
    // No release label for DBX 160
    vcaPanel.container->addAndMakeVisible(vcaPanel.outputLabel.get());
    
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
    
    // DBX 160 has fixed release rate - no attachment needed
    
    if (params.getRawParameterValue("vca_output"))
        vcaPanel.outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            params, "vca_output", *vcaPanel.outputKnob);
    
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
    
    // Add to container
    busPanel.container->addAndMakeVisible(busPanel.thresholdKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.ratioKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.attackSelector.get());
    busPanel.container->addAndMakeVisible(busPanel.releaseSelector.get());
    busPanel.container->addAndMakeVisible(busPanel.makeupKnob.get());
    busPanel.container->addAndMakeVisible(busPanel.thresholdLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.ratioLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.attackLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.releaseLabel.get());
    busPanel.container->addAndMakeVisible(busPanel.makeupLabel.get());
    
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
    
}

void EnhancedCompressorEditor::setupDigitalPanel()
{
    // Note: DigitalCompressorPanel is not used in current 6-mode configuration
    // (its parameters are not defined in createParameterLayout)
    // Keeping structure for potential future Digital mode addition

    // Studio VCA Panel (Focusrite Red 3 style)
    studioVcaPanel = std::make_unique<StudioVCAPanel>(processor.getParameters());
    addChildComponent(studioVcaPanel.get());
}

// Multiband panel removed

void EnhancedCompressorEditor::updateMode(int newMode)
{
    currentMode = juce::jlimit(0, 5, newMode);  // 0-5 for 6 modes

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
        case 0: // Vintage Opto (LA-2A)
            optoPanel.container->setVisible(true);
            if (optoPanel.limitSwitch)
                optoPanel.limitSwitch->setVisible(true);
            currentLookAndFeel = optoLookAndFeel.get();
            break;

        case 1: // Vintage FET (1176 Bluestripe)
            fetPanel.container->setVisible(true);
            currentLookAndFeel = fetLookAndFeel.get();
            break;

        case 2: // Classic VCA (DBX 160)
            vcaPanel.container->setVisible(true);
            if (vcaPanel.overEasyButton)
                vcaPanel.overEasyButton->setVisible(true);
            currentLookAndFeel = vcaLookAndFeel.get();
            break;

        case 3: // Vintage VCA / Bus (SSL G)
            busPanel.container->setVisible(true);
            currentLookAndFeel = busLookAndFeel.get();
            break;

        case 4: // Studio FET (1176 Rev E Blackface) - shares FET panel
            fetPanel.container->setVisible(true);
            currentLookAndFeel = fetLookAndFeel.get();  // Use FET look (could customize later)
            break;

        case 5: // Studio VCA (Focusrite Red 3)
            if (studioVcaPanel)
                studioVcaPanel->setVisible(true);
            currentLookAndFeel = busLookAndFeel.get();  // Use Bus look for meters/background
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
            default:
                buttonTextColor = juce::Colour(0xFFE0E0E0);
                break;
        }
        
        if (bypassButton)
        {
            bypassButton->setColour(juce::ToggleButton::textColourId, buttonTextColor);
            bypassButton->setColour(juce::ToggleButton::tickColourId, buttonTextColor);
        }
        // Oversample button removed
        
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
            // No release knob for DBX 160
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
        case 4: bgColor = juce::Colour(0xFF1A1A2E); break; // Digital - modern dark blue
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
            title = "DIGITAL COMPRESSOR";
            textColor = juce::Colour(0xFF00D4FF);  // Cyan
            break;
        default:
            title = "UNIVERSAL COMPRESSOR";
            textColor = juce::Colour(0xFFE0E0E0);
            break;
    }
    
    // Draw title in a smaller area that doesn't overlap with controls
    auto titleBounds = bounds.removeFromTop(35 * scaleFactor).withTrimmedLeft(200 * scaleFactor).withTrimmedRight(200 * scaleFactor);
    g.setColour(textColor);
    // Use the member scaleFactor already calculated in resized()
    g.setFont(juce::Font(juce::FontOptions(20.0f * scaleFactor).withStyle("Bold")));
    g.drawText(title, titleBounds, juce::Justification::centred);
    
    // Draw meter labels with full opacity for better readability
    g.setFont(juce::Font(juce::FontOptions(11.0f * scaleFactor).withStyle("Bold")));
    g.setColour(textColor);
    
    // Center INPUT text over the input meter
    if (inputMeter)
    {
        auto inputBounds = inputMeter->getBounds();
        g.drawText("INPUT", inputBounds.getX() - 10, inputBounds.getY() - 20, 
                   inputBounds.getWidth() + 20, 20, juce::Justification::centred);
        
        // Draw smoothed input level value below the meter for better readability
        juce::String inputText = juce::String(smoothedInputLevel, 1) + " dB";
        g.setFont(juce::Font(juce::FontOptions(10.0f * scaleFactor)));
        g.drawText(inputText, inputBounds.getX() - 10, inputBounds.getBottom(), 
                   inputBounds.getWidth() + 20, 25 * scaleFactor, juce::Justification::centred);
    }
    
    // Center OUTPUT text over the output meter
    if (outputMeter)
    {
        auto outputBounds = outputMeter->getBounds();
        g.setFont(juce::Font(juce::FontOptions(11.0f * scaleFactor).withStyle("Bold")));
        g.drawText("OUTPUT", outputBounds.getX() - 10, outputBounds.getY() - 20, 
                   outputBounds.getWidth() + 20, 20, juce::Justification::centred);
        
        // Draw smoothed output level value below the meter for better readability
        juce::String outputText = juce::String(smoothedOutputLevel, 1) + " dB";
        g.setFont(juce::Font(juce::FontOptions(10.0f * scaleFactor)));
        g.drawText(outputText, outputBounds.getX() - 10, outputBounds.getBottom(), 
                   outputBounds.getWidth() + 20, 25, juce::Justification::centred);
    }
    
    // Draw VU meter label below the VU meter
    // Calculate the same position as in resized() method
    auto vuBounds = getLocalBounds();
    auto vuTopRow = vuBounds.removeFromTop(70 * scaleFactor).withTrimmedTop(35 * scaleFactor);
    auto vuMainArea = vuBounds;
    auto vuLeftMeter = vuMainArea.removeFromLeft(60 * scaleFactor);
    auto vuRightMeter = vuMainArea.removeFromRight(60 * scaleFactor);
    vuMainArea.reduce(20 * scaleFactor, 0);
    auto vuLabelArea = vuMainArea.removeFromTop(190 * scaleFactor + 35 * scaleFactor);
    g.drawText("GAIN REDUCTION", vuLabelArea.removeFromBottom(30 * scaleFactor), juce::Justification::centred);
}

void EnhancedCompressorEditor::resized()
{
    auto bounds = getLocalBounds();
    
    // Calculate scale factor based on window size
    float widthScale = getWidth() / 700.0f;  // Base size is now 700x500
    float heightScale = getHeight() / 500.0f;
    scaleFactor = juce::jmin(widthScale, heightScale);  // Use the smaller scale to maintain proportions
    
    // Position resizer in corner
    if (resizer)
        resizer->setBounds(getWidth() - 16, getHeight() - 16, 16, 16);
    
    // Top row - mode selector and global controls (scale all values)
    // Leave space for title
    auto topRow = bounds.removeFromTop(70 * scaleFactor).withTrimmedTop(35 * scaleFactor);
    topRow.reduce(20 * scaleFactor, 5 * scaleFactor);
    
    if (modeSelector)
        modeSelector->setBounds(topRow.removeFromLeft(150 * scaleFactor));
    else
        topRow.removeFromLeft(150 * scaleFactor);
    
    topRow.removeFromLeft(20 * scaleFactor);
    
    if (bypassButton)
        bypassButton->setBounds(topRow.removeFromLeft(120 * scaleFactor));
    else
        topRow.removeFromLeft(120 * scaleFactor);
        
    topRow.removeFromLeft(10 * scaleFactor);
    
    // Oversample button removed - space now available
    
    topRow.removeFromLeft(10);
    
    // Mode-specific buttons in top row
    if (optoPanel.limitSwitch)
    {
        if (currentMode == 0)  // Opto mode
        {
            optoPanel.limitSwitch->setVisible(true);
            optoPanel.limitSwitch->setBounds(topRow.removeFromLeft(150 * scaleFactor));
        }
        else
        {
            optoPanel.limitSwitch->setVisible(false);
        }
    }
    
    // VCA mode Over Easy button in top row
    if (vcaPanel.overEasyButton)
    {
        if (currentMode == 2)  // VCA mode
        {
            vcaPanel.overEasyButton->setVisible(true);
            vcaPanel.overEasyButton->setBounds(topRow.removeFromLeft(120 * scaleFactor));
        }
        else
        {
            vcaPanel.overEasyButton->setVisible(false);
        }
    }
    
    // Main area
    auto mainArea = bounds.reduced(20 * scaleFactor, 10 * scaleFactor);
    
    // Left meter - leave space for labels above and below
    auto leftMeter = mainArea.removeFromLeft(60 * scaleFactor);
    leftMeter.removeFromTop(25 * scaleFactor);  // Space for "INPUT" label
    if (inputMeter)
        inputMeter->setBounds(leftMeter.removeFromTop(leftMeter.getHeight() - 30 * scaleFactor));
    
    // Right meter - leave space for labels above and below
    auto rightMeter = mainArea.removeFromRight(60 * scaleFactor);
    rightMeter.removeFromTop(25 * scaleFactor);  // Space for "OUTPUT" label
    if (outputMeter)
        outputMeter->setBounds(rightMeter.removeFromTop(rightMeter.getHeight() - 30 * scaleFactor));
    
    // Center area
    mainArea.reduce(20 * scaleFactor, 0);
    
    // VU Meter at top center - good readable size
    auto vuArea = mainArea.removeFromTop(190 * scaleFactor);  // Increased from 160 to 190
    if (vuMeter)
        vuMeter->setBounds(vuArea.reduced(55 * scaleFactor, 5 * scaleFactor));  // Less horizontal reduction for larger meter
    
    // Add space for "GAIN REDUCTION" text below VU meter
    mainArea.removeFromTop(35 * scaleFactor);  // Space for the label
    
    // Control panel area
    auto controlArea = mainArea.reduced(10 * scaleFactor, 20 * scaleFactor);
    
    // Layout Opto panel
    if (optoPanel.container && optoPanel.container->isVisible())
    {
        optoPanel.container->setBounds(controlArea);
        
        // Layout within the container's local bounds
        auto optoBounds = optoPanel.container->getLocalBounds();
        auto knobRow = optoBounds.removeFromTop(120 * scaleFactor);
        
        // Center the two knobs horizontally
        auto totalKnobWidth = knobRow.getWidth() * 0.7f;  // Use 70% of width for knobs
        auto knobStartX = (knobRow.getWidth() - totalKnobWidth) / 2;
        knobRow = knobRow.withX(knobStartX).withWidth(totalKnobWidth);
        
        auto peakArea = knobRow.removeFromLeft(knobRow.getWidth() / 2);
        if (optoPanel.peakReductionLabel)
            optoPanel.peakReductionLabel->setBounds(peakArea.removeFromTop(25 * scaleFactor));
        if (optoPanel.peakReductionKnob)
            optoPanel.peakReductionKnob->setBounds(peakArea.reduced(15 * scaleFactor, 0));
        
        auto gainArea = knobRow;
        if (optoPanel.gainLabel)
            optoPanel.gainLabel->setBounds(gainArea.removeFromTop(25 * scaleFactor));
        if (optoPanel.gainKnob)
            optoPanel.gainKnob->setBounds(gainArea.reduced(15 * scaleFactor, 0));
        
        // Compress/Limit switch is now in the top row, not in the panel
    }
    
    // Layout FET panel
    if (fetPanel.container && fetPanel.container->isVisible())
    {
        fetPanel.container->setBounds(controlArea);
        
        // Layout within the container's local bounds
        auto fetBounds = fetPanel.container->getLocalBounds();
        auto topKnobs = fetBounds.removeFromTop(120 * scaleFactor);
        
        auto knobWidth = topKnobs.getWidth() / 4;
        
        auto inputArea = topKnobs.removeFromLeft(knobWidth);
        if (fetPanel.inputLabel)
            fetPanel.inputLabel->setBounds(inputArea.removeFromTop(25 * scaleFactor));
        if (fetPanel.inputKnob)
            fetPanel.inputKnob->setBounds(inputArea.reduced(15 * scaleFactor, 0));
        
        auto outputArea = topKnobs.removeFromLeft(knobWidth);
        if (fetPanel.outputLabel)
            fetPanel.outputLabel->setBounds(outputArea.removeFromTop(25 * scaleFactor));
        if (fetPanel.outputKnob)
            fetPanel.outputKnob->setBounds(outputArea.reduced(15 * scaleFactor, 0));

        auto attackArea = topKnobs.removeFromLeft(knobWidth);
        if (fetPanel.attackLabel)
            fetPanel.attackLabel->setBounds(attackArea.removeFromTop(25 * scaleFactor));
        if (fetPanel.attackKnob)
            fetPanel.attackKnob->setBounds(attackArea.reduced(15 * scaleFactor, 0));

        auto releaseArea = topKnobs;
        if (fetPanel.releaseLabel)
            fetPanel.releaseLabel->setBounds(releaseArea.removeFromTop(25 * scaleFactor));
        if (fetPanel.releaseKnob)
            fetPanel.releaseKnob->setBounds(releaseArea.reduced(15 * scaleFactor, 0));

        if (fetPanel.ratioButtons)
            fetPanel.ratioButtons->setBounds(fetBounds.removeFromTop(90 * scaleFactor).reduced(15 * scaleFactor, 2 * scaleFactor));  // Much larger buttons, minimal reduction
    }
    
    // Layout VCA panel
    if (vcaPanel.container && vcaPanel.container->isVisible())
    {
        vcaPanel.container->setBounds(controlArea);
        
        // Layout within the container's local bounds - 4 knobs in ONE row (no release for DBX 160)
        auto vcaBounds = vcaPanel.container->getLocalBounds();

        // Define consistent knob dimensions for ALL knobs in single row
        const int knobRowHeight = 120 * scaleFactor;
        const int labelHeight = 25 * scaleFactor;
        const int knobReduction = 10 * scaleFactor;  // Less reduction for more space

        // Center the row vertically
        auto knobRow = vcaBounds.withHeight(knobRowHeight);
        knobRow.setY((vcaBounds.getHeight() - knobRowHeight) / 2);

        // Calculate knob width based on 4 knobs in one row (no release)
        auto knobWidth = knobRow.getWidth() / 4;

        // Layout all 4 knobs in order: Threshold, Ratio, Attack, Output

        // Threshold knob
        auto thresholdBounds = knobRow.removeFromLeft(knobWidth);
        vcaPanel.thresholdLabel->setBounds(thresholdBounds.removeFromTop(labelHeight));
        vcaPanel.thresholdKnob->setBounds(thresholdBounds.reduced(knobReduction, 0));

        // Ratio knob
        auto ratioBounds = knobRow.removeFromLeft(knobWidth);
        vcaPanel.ratioLabel->setBounds(ratioBounds.removeFromTop(labelHeight));
        vcaPanel.ratioKnob->setBounds(ratioBounds.reduced(knobReduction, 0));

        // Attack knob
        auto attackBounds = knobRow.removeFromLeft(knobWidth);
        vcaPanel.attackLabel->setBounds(attackBounds.removeFromTop(labelHeight));
        vcaPanel.attackKnob->setBounds(attackBounds.reduced(knobReduction, 0));

        // Output knob
        auto outputBounds = knobRow;  // Use remaining space
        vcaPanel.outputLabel->setBounds(outputBounds.removeFromTop(labelHeight));
        vcaPanel.outputKnob->setBounds(outputBounds.reduced(knobReduction, 0));
        
        // Over Easy button is in the top row, not in the panel
    }
    
    // Layout Bus panel
    if (busPanel.container && busPanel.container->isVisible())
    {
        busPanel.container->setBounds(controlArea);

        // Layout within the container's local bounds
        auto busBounds = busPanel.container->getLocalBounds();

        // Compact layout to fit everything in 700x500 with proper scaling
        auto busTopRow = busBounds.removeFromTop(100 * scaleFactor);
        auto bottomRow = busBounds.removeFromTop(80 * scaleFactor);

        auto controlWidth = busTopRow.getWidth() / 3;

        auto thresholdArea = busTopRow.removeFromLeft(controlWidth);
        busPanel.thresholdLabel->setBounds(thresholdArea.removeFromTop(20 * scaleFactor));
        busPanel.thresholdKnob->setBounds(thresholdArea.reduced(15 * scaleFactor, 0));

        auto ratioArea = busTopRow.removeFromLeft(controlWidth);
        busPanel.ratioLabel->setBounds(ratioArea.removeFromTop(20 * scaleFactor));
        busPanel.ratioKnob->setBounds(ratioArea.reduced(15 * scaleFactor, 0));

        auto makeupArea = busTopRow;
        busPanel.makeupLabel->setBounds(makeupArea.removeFromTop(20 * scaleFactor));
        busPanel.makeupKnob->setBounds(makeupArea.reduced(15 * scaleFactor, 0));

        controlWidth = bottomRow.getWidth() / 2;

        auto attackArea = bottomRow.removeFromLeft(controlWidth);
        busPanel.attackLabel->setBounds(attackArea.removeFromTop(20 * scaleFactor));
        busPanel.attackSelector->setBounds(attackArea.reduced(40 * scaleFactor, 0).removeFromTop(25 * scaleFactor));

        auto releaseArea = bottomRow;
        busPanel.releaseLabel->setBounds(releaseArea.removeFromTop(20 * scaleFactor));
        busPanel.releaseSelector->setBounds(releaseArea.reduced(40 * scaleFactor, 0).removeFromTop(25 * scaleFactor));
    }

    // Layout Digital panel
    if (digitalPanel && digitalPanel->isVisible())
    {
        digitalPanel->setBounds(controlArea);
    }

    // Layout Studio VCA panel
    if (studioVcaPanel && studioVcaPanel->isVisible())
    {
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
        
        // Apply smoothing to the readout value for better readability
        // Use peak hold with slow decay for easier reading
        if (inputDb > smoothedInputLevel)
        {
            // Fast attack for peak capture
            smoothedInputLevel = inputDb;
        }
        else
        {
            // Slow release for easy reading (exponential smoothing)
            smoothedInputLevel = smoothedInputLevel * levelSmoothingFactor + 
                               inputDb * (1.0f - levelSmoothingFactor);
        }
    }
    
    if (vuMeter)
        vuMeter->setLevel(processor.getGainReduction());
    
    if (outputMeter)
    {
        // LEDMeter expects dB values, not linear
        float outputDb = processor.getOutputLevel();
        outputMeter->setLevel(outputDb);
        
        // Apply smoothing to the readout value for better readability
        // Use peak hold with slow decay for easier reading
        if (outputDb > smoothedOutputLevel)
        {
            // Fast attack for peak capture
            smoothedOutputLevel = outputDb;
        }
        else
        {
            // Slow release for easy reading (exponential smoothing)
            smoothedOutputLevel = smoothedOutputLevel * levelSmoothingFactor + 
                                outputDb * (1.0f - levelSmoothingFactor);
        }
    }
    
    // Repaint the area where meter values are displayed
    repaint(inputMeter ? inputMeter->getBounds().expanded(20, 25) : juce::Rectangle<int>());
    repaint(outputMeter ? outputMeter->getBounds().expanded(20, 25) : juce::Rectangle<int>());
}

void EnhancedCompressorEditor::parameterChanged(const juce::String& parameterID, float)
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