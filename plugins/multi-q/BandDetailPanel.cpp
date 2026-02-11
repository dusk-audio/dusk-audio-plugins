#include "BandDetailPanel.h"
#include "MultiQ.h"
#include "F6KnobLookAndFeel.h"

// Static instance of F6 knob look and feel (shared by all instances)
static F6KnobLookAndFeel f6KnobLookAndFeel;

BandDetailPanel::BandDetailPanel(MultiQ& p)
    : processor(p)
{
    setupKnobs();
    updateAttachments();
    updateControlsForBandType();

    // Listen for dynamics enable changes to update opacity
    // Also listen for band enabled changes to update the indicator
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.addParameterListener(ParamIDs::bandDynEnabled(i), this);
        processor.parameters.addParameterListener(ParamIDs::bandEnabled(i), this);
    }
    // Listen for shape changes on bands 2-7 (indices 1-6 have shape selector)
    for (int i = 2; i <= 7; ++i)
        processor.parameters.addParameterListener(ParamIDs::bandShape(i), this);
}

BandDetailPanel::~BandDetailPanel()
{
    // Remove parameter listeners
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.removeParameterListener(ParamIDs::bandDynEnabled(i), this);
        processor.parameters.removeParameterListener(ParamIDs::bandEnabled(i), this);
    }
    for (int i = 2; i <= 7; ++i)
        processor.parameters.removeParameterListener(ParamIDs::bandShape(i), this);

    // Release LookAndFeel references before knobs are destroyed
    if (freqKnob) freqKnob->setLookAndFeel(nullptr);
    if (gainKnob) gainKnob->setLookAndFeel(nullptr);
    if (qKnob) qKnob->setLookAndFeel(nullptr);
    if (thresholdKnob) thresholdKnob->setLookAndFeel(nullptr);
    if (attackKnob) attackKnob->setLookAndFeel(nullptr);
    if (releaseKnob) releaseKnob->setLookAndFeel(nullptr);
    if (rangeKnob) rangeKnob->setLookAndFeel(nullptr);
}

void BandDetailPanel::setupBandButtons()
{
    // No longer using TextButtons - drawing manually in paint()
}

void BandDetailPanel::updateBandButtonColors()
{
    // Update knob arc colors to match selected band
    juce::Colour bandColor = getBandColor(selectedBand);
    freqKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);
    qKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);
    gainKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);

    repaint();
}

void BandDetailPanel::setupKnobs()
{
    auto setupRotaryKnob = [this](std::unique_ptr<juce::Slider>& knob) {
        knob = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
        knob->setLookAndFeel(&f6KnobLookAndFeel);
        knob->setColour(juce::Slider::rotarySliderFillColourId, getBandColor(selectedBand));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF404040));
        // Repaint panel when knob value changes so value labels update
        knob->onValueChange = [this]() { repaint(); };
        addAndMakeVisible(knob.get());
    };

    // EQ knobs
    setupRotaryKnob(freqKnob);
    freqKnob->setTooltip("Frequency: Center frequency of this band (20 Hz - 20 kHz)");
    setupRotaryKnob(gainKnob);
    gainKnob->setTooltip("Gain: Boost or cut at this frequency (-24 to +24 dB)");
    setupRotaryKnob(qKnob);
    qKnob->setTooltip("Q: Bandwidth/resonance - higher values = narrower bandwidth (0.1 - 100)");

    // Slope selector for HPF/LPF
    slopeSelector = std::make_unique<juce::ComboBox>();
    slopeSelector->addItem("6 dB/oct", 1);
    slopeSelector->addItem("12 dB/oct", 2);
    slopeSelector->addItem("18 dB/oct", 3);
    slopeSelector->addItem("24 dB/oct", 4);
    slopeSelector->addItem("36 dB/oct", 5);
    slopeSelector->addItem("48 dB/oct", 6);
    slopeSelector->setTooltip("Filter slope: Steeper = sharper cutoff (6-48 dB/octave)");
    addAndMakeVisible(slopeSelector.get());

    // Shape selector for bands 2-7 (different options per band)
    shapeSelector = std::make_unique<juce::ComboBox>();
    // Items are populated dynamically in setSelectedBand() based on band type
    shapeSelector->addItem("Peaking", 1);
    shapeSelector->addItem("Notch", 2);
    shapeSelector->addItem("Band Pass", 3);
    shapeSelector->setTooltip("Filter shape");
    addAndMakeVisible(shapeSelector.get());

    // Per-band channel routing selector
    routingSelector = std::make_unique<juce::ComboBox>();
    routingSelector->addItem("Stereo", 1);
    routingSelector->addItem("Left", 2);
    routingSelector->addItem("Right", 3);
    routingSelector->addItem("Mid", 4);
    routingSelector->addItem("Side", 5);
    routingSelector->setTooltip("Channel routing: Stereo (both), Left/Right (one channel), Mid/Side (M/S processing)");
    addAndMakeVisible(routingSelector.get());

    // Dynamics knobs (use orange for dynamics section)
    auto setupDynKnob = [this](std::unique_ptr<juce::Slider>& knob) {
        knob = std::make_unique<DuskSlider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
        knob->setLookAndFeel(&f6KnobLookAndFeel);
        knob->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFFff8844));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF404040));
        knob->onValueChange = [this]() { repaint(); };
        addAndMakeVisible(knob.get());
    };

    setupDynKnob(thresholdKnob);
    thresholdKnob->setTooltip("Threshold: Level where dynamic gain reduction starts (-60 to +12 dB)");
    setupDynKnob(attackKnob);
    attackKnob->setTooltip("Attack: How fast gain reduction responds to level increases (0.1 - 500 ms)");
    setupDynKnob(releaseKnob);
    releaseKnob->setTooltip("Release: How fast gain returns after level drops (10 - 5000 ms)");
    setupDynKnob(rangeKnob);
    rangeKnob->setTooltip("Range: Maximum amount of dynamic gain reduction (0 - 24 dB)");
    setupDynKnob(ratioKnob);
    ratioKnob->setTooltip("Ratio: Compression ratio (1:1 = no compression, higher = more compression)");

    // Toggle buttons
    dynButton = std::make_unique<juce::TextButton>("DYN");
    dynButton->setClickingTogglesState(true);
    dynButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF353535));
    dynButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF4488ff));
    dynButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
    dynButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    dynButton->setTooltip("Enable per-band dynamics processing (Shortcut: D)");
    addAndMakeVisible(dynButton.get());

    soloButton = std::make_unique<juce::TextButton>("SOLO");
    soloButton->setClickingTogglesState(true);
    soloButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF353535));
    soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF888888));
    soloButton->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    soloButton->setTooltip("Solo this band (mute all other bands) (Shortcut: S)");
    soloButton->onClick = [this]() {
        if (soloButton->getToggleState())
            processor.setSoloedBand(selectedBand);
        else
            processor.setSoloedBand(-1);  // No solo
    };
    addAndMakeVisible(soloButton.get());
}

void BandDetailPanel::setSelectedBand(int bandIndex)
{
    if (bandIndex == selectedBand)
        return;

    selectedBand = juce::jlimit(0, 7, bandIndex);
    updateAttachments();
    updateControlsForBandType();
    updateDynamicsOpacity();
    updateBandButtonColors();

    // Update DYN button color to match selected band
    juce::Colour bandColor = getBandColor(selectedBand);
    dynButton->setColour(juce::TextButton::buttonOnColourId, bandColor);

    // Update SOLO button to reflect if this band is soloed
    bool isSoloed = processor.isBandSoloed(selectedBand);
    soloButton->setToggleState(isSoloed, juce::dontSendNotification);

    // Recalculate layout (ensures knob bounds are set correctly)
    resized();
    repaint();
}

void BandDetailPanel::updateAttachments()
{
    // Clear all existing attachments
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    slopeAttachment.reset();
    shapeAttachment.reset();
    routingAttachment.reset();
    dynEnableAttachment.reset();
    threshAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    rangeAttachment.reset();
    ratioAttachment.reset();

    if (selectedBand < 0 || selectedBand >= 8)
        return;

    int bandNum = selectedBand + 1;

    // EQ parameter attachments
    freqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandFreq(bandNum), *freqKnob);

    qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandQ(bandNum), *qKnob);

    BandType type = getBandType(selectedBand);

    if (type == BandType::HighPass || type == BandType::LowPass)
    {
        slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, ParamIDs::bandSlope(bandNum), *slopeSelector);
    }

    // Shape selector for bands 2-7 (indices 1-6)
    bool hasShape = (selectedBand >= 1 && selectedBand <= 6 && selectedBand != 0 && selectedBand != 7);
    if (hasShape)
    {
        // Update shape selector items based on band
        shapeSelector->clear(juce::dontSendNotification);
        if (selectedBand == 1)  // Band 2
        {
            shapeSelector->addItem("Low Shelf", 1);
            shapeSelector->addItem("Peaking", 2);
            shapeSelector->addItem("High Pass", 3);
            shapeSelector->setTooltip("Filter shape: Low Shelf (default), Peaking (bell), High Pass (cut)");
        }
        else if (selectedBand == 6)  // Band 7
        {
            shapeSelector->addItem("High Shelf", 1);
            shapeSelector->addItem("Peaking", 2);
            shapeSelector->addItem("Low Pass", 3);
            shapeSelector->setTooltip("Filter shape: High Shelf (default), Peaking (bell), Low Pass (cut)");
        }
        else  // Parametric bands 3-6
        {
            shapeSelector->addItem("Peaking", 1);
            shapeSelector->addItem("Notch", 2);
            shapeSelector->addItem("Band Pass", 3);
            shapeSelector->addItem("Tilt Shelf", 4);
            shapeSelector->setTooltip("Filter shape: Peaking, Notch, Band Pass, or Tilt Shelf");
        }

        shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, ParamIDs::bandShape(bandNum), *shapeSelector);
    }

    if (type != BandType::HighPass && type != BandType::LowPass)
    {
        // Determine current shape to decide if gain knob should be attached
        int shape = 0;
        if (hasShape)
        {
            auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(bandNum));
            if (shapeParam)
                shape = static_cast<int>(shapeParam->load());
        }

        // For bands 3-6: shape 1=Notch, 2=BandPass (Q-only, no gain)
        // For bands 2 and 7: shape 2=HPF/LPF (no gain), shape 1=Peaking and shape 0=Shelf (both have gain)
        bool isQOnly = false;
        if (selectedBand >= 2 && selectedBand <= 5)
            isQOnly = (shape == 1 || shape == 2);  // Notch or BandPass
        else if (selectedBand == 1 || selectedBand == 6)
            isQOnly = (shape == 2);  // HPF or LPF mode

        if (!isQOnly)
        {
            gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.parameters, ParamIDs::bandGain(bandNum), *gainKnob);
        }
    }

    // Per-band channel routing attachment
    routingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, ParamIDs::bandChannelRouting(bandNum), *routingSelector);

    // Dynamics attachments
    dynEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bandDynEnabled(bandNum), *dynButton);

    threshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynThreshold(bandNum), *thresholdKnob);

    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynAttack(bandNum), *attackKnob);

    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRelease(bandNum), *releaseKnob);

    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRange(bandNum), *rangeKnob);

    ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::bandDynRatio(bandNum), *ratioKnob);
}

void BandDetailPanel::updateControlsForBandType()
{
    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);
    bool hasShape = (selectedBand >= 1 && selectedBand <= 6);

    // Determine current shape
    int shape = 0;
    if (hasShape)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(selectedBand + 1));
        if (shapeParam)
            shape = static_cast<int>(shapeParam->load());
    }

    // For bands 3-6: shape 1=Notch, 2=BandPass (Q-only)
    // For band 2: shape 2=HPF (Q-only)
    // For band 7: shape 2=LPF (Q-only)
    bool isQOnly = false;
    if (selectedBand >= 2 && selectedBand <= 5)
        isQOnly = (shape == 1 || shape == 2);
    else if (selectedBand == 1 || selectedBand == 6)
        isQOnly = (shape == 2);

    // Shape selector: visible for bands 2-7 (not HPF/LPF bands 1 and 8)
    shapeSelector->setVisible(hasShape && !isFilter);

    // Slope selector: visible only for HPF/LPF (band 1 and 8)
    slopeSelector->setVisible(isFilter);

    // Gain knob: visible for shapes with gain, hidden for filters and Q-only shapes
    gainKnob->setVisible(!isFilter && !isQOnly);

    // Ensure the visible control in the third column is on top
    if (isFilter)
    {
        slopeSelector->toFront(false);
        slopeSelector->repaint();
    }
    else if (!isQOnly)
    {
        gainKnob->toFront(false);
        gainKnob->repaint();
    }
}

void BandDetailPanel::updateDynamicsOpacity()
{
    bool dynEnabled = isDynamicsEnabled();
    float alpha = dynEnabled ? 1.0f : 0.3f;  // 30% opacity when disabled (more obvious disabled state)

    thresholdKnob->setAlpha(alpha);
    attackKnob->setAlpha(alpha);
    releaseKnob->setAlpha(alpha);
    rangeKnob->setAlpha(alpha);
    ratioKnob->setAlpha(alpha);
}

juce::Colour BandDetailPanel::getBandColor(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= 8)
        return juce::Colours::grey;
    return DefaultBandConfigs[static_cast<size_t>(bandIndex)].color;
}

BandType BandDetailPanel::getBandType(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= 8)
        return BandType::Parametric;
    return DefaultBandConfigs[static_cast<size_t>(bandIndex)].type;
}

bool BandDetailPanel::isDynamicsEnabled() const
{
    if (selectedBand < 0 || selectedBand >= 8)
        return false;

    auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandDynEnabled(selectedBand + 1));
    return param != nullptr && param->load() > 0.5f;
}

void BandDetailPanel::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == ParamIDs::bandDynEnabled(selectedBand + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this)]() {
            if (safeThis != nullptr)
            {
                safeThis->updateDynamicsOpacity();
                safeThis->repaint();
            }
        });
    }

    // Repaint when band enabled state changes (for the indicator box)
    if (parameterID == ParamIDs::bandEnabled(selectedBand + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this)]() {
            if (safeThis != nullptr)
                safeThis->repaint();
        });
    }

    // Update controls when shape changes (show/hide gain knob for notch/bandpass)
    // Bands 2-7 (selectedBand 1-6) have shape selectors
    if (selectedBand >= 1 && selectedBand <= 6 &&
        parameterID == ParamIDs::bandShape(selectedBand + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this)]() {
            if (safeThis != nullptr)
            {
                safeThis->updateAttachments();
                safeThis->updateControlsForBandType();
                safeThis->resized();
                safeThis->repaint();
            }
        });
    }
}

juce::Rectangle<int> BandDetailPanel::getBandButtonBounds(int /*index*/) const
{
    // Returns bounds of the band indicator box (must match paint() centering calculation)
    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int btnWidth = 48;

    // Calculate total content width (must match paint() and resized())
    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10
                          + eqColumnsWidth + 10
                          + 12
                          + (knobSize + knobSpacing) * 5 + 6
                          + btnWidth;

    int startX = (getWidth() - totalContentWidth) / 2;
    int bandBoxY = knobY + (knobSize - bandIndicatorSize) / 2;

    return juce::Rectangle<int>(startX, bandBoxY, bandIndicatorSize, bandIndicatorSize);
}

void BandDetailPanel::mouseDown(const juce::MouseEvent& e)
{
    // Check if click is on the band indicator box - toggle band enabled state
    auto bandBox = getBandButtonBounds(selectedBand);
    if (bandBox.contains(e.getPosition()) && selectedBand >= 0 && selectedBand < 8)
    {
        // Toggle the band enabled parameter
        if (auto* param = processor.parameters.getParameter(ParamIDs::bandEnabled(selectedBand + 1)))
        {
            float currentValue = param->getValue();
            param->setValueNotifyingHost(currentValue > 0.5f ? 0.0f : 1.0f);
        }
    }
}

void BandDetailPanel::mouseMove(const juce::MouseEvent& /*e*/)
{
    // No hover effects needed anymore
}

void BandDetailPanel::mouseExit(const juce::MouseEvent&)
{
    // No hover effects needed anymore
}

void BandDetailPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark background for entire panel
    g.setColour(juce::Colour(0xFF1a1a1c));
    g.fillRect(bounds);

    // Top border line
    g.setColour(juce::Colour(0xFF3a3a3a));
    g.drawHorizontalLine(0, 0.0f, bounds.getWidth());

    // Band colors from shared config (DefaultBandConfigs in EQBand.h)


    // ===== CONTROLS AREA =====
    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;  // Larger for better visibility
    int knobY = 26;  // More room for section headers
    int btnWidth = 48;

    // Calculate total content width to center it (must match resized() calculation)
    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10  // Band indicator + gap
                          + eqColumnsWidth + 10  // EQ section + separator gap
                          + 12  // Divider space
                          + (knobSize + knobSpacing) * 5 + 6  // 5 dynamics knobs + gap before buttons
                          + btnWidth;  // DYN and SOLO buttons
    // Center the content
    int startX = (getWidth() - totalContentWidth) / 2;

    bool dynEnabled = isDynamicsEnabled();

    // ===== BAND INDICATOR BOX (left of FREQ) - only show if a band is selected =====
    int bandBoxX = startX;
    int bandBoxY = knobY + (knobSize - bandIndicatorSize) / 2;
    juce::Rectangle<float> bandBox(static_cast<float>(bandBoxX), static_cast<float>(bandBoxY),
                                    static_cast<float>(bandIndicatorSize), static_cast<float>(bandIndicatorSize));

    // Ensure selectedBand is valid
    int bandIdx = juce::jlimit(0, 7, selectedBand);
    juce::Colour bandColor = DefaultBandConfigs[bandIdx].color;

    // Check if band is enabled
    bool bandEnabled = true;
    if (auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandEnabled(bandIdx + 1)))
        bandEnabled = param->load() > 0.5f;

    // Make color more subtle (darker) for better contrast with white text
    // If disabled, desaturate and darken significantly - just a hint of color
    juce::Colour subtleBandColor;
    if (bandEnabled)
        subtleBandColor = bandColor.darker(0.5f);
    else
        subtleBandColor = bandColor.withSaturation(0.15f).darker(0.7f);  // Very desaturated, dark

    // Draw band indicator box with subtle color
    g.setColour(subtleBandColor);
    g.fillRoundedRectangle(bandBox, 8.0f);

    // Draw subtle border in original band color for accent
    g.setColour(bandEnabled ? bandColor.withAlpha(0.6f) : bandColor.withSaturation(0.2f).withAlpha(0.3f));
    g.drawRoundedRectangle(bandBox.reduced(1.0f), 7.0f, 2.0f);

    // Draw band number centered in box (shifted up if showing GR)
    g.setColour(bandEnabled ? juce::Colours::white : juce::Colour(0xFF606060));
    g.setFont(juce::FontOptions(32.0f, juce::Font::bold));

    // Get current gain reduction for this band
    float gainReduction = processor.getDynamicGain(bandIdx);
    bool showGR = dynEnabled && bandEnabled && std::abs(gainReduction) > 0.1f;

    if (showGR)
    {
        // Shift band number up to make room for GR display
        auto numberRect = bandBox.toNearestInt().withTrimmedBottom(18);
        g.drawText(juce::String(bandIdx + 1), numberRect, juce::Justification::centred);

        // Draw GR value below the band number (orange when active)
        juce::Colour grColor = juce::Colour(0xFFff6644);  // Orange-red for GR
        g.setColour(grColor);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));

        auto grRect = bandBox.toNearestInt().withTrimmedTop(38);
        juce::String grText = juce::String(gainReduction, 1) + " dB";
        g.drawText(grText, grRect, juce::Justification::centred);

        // Add subtle glow to border based on GR amount (more GR = more glow)
        float glowIntensity = juce::jlimit(0.0f, 1.0f, std::abs(gainReduction) / 12.0f);
        if (glowIntensity > 0.05f)
        {
            g.setColour(grColor.withAlpha(glowIntensity * 0.5f));
            g.drawRoundedRectangle(bandBox.reduced(0.5f), 8.5f, 3.0f);
        }
    }
    else
    {
        g.drawText(juce::String(bandIdx + 1), bandBox.toNearestInt(), juce::Justification::centred);
    }

    int currentX = startX + bandIndicatorSize + 10;  // Start after band indicator

    // ===== EQ SECTION BACKGROUND =====
    int eqSectionWidth = eqColumnsWidth + 6;  // Match column widths + padding
    juce::Rectangle<float> eqSection(static_cast<float>(currentX - 4), 4.0f,
                                      static_cast<float>(eqSectionWidth), bounds.getHeight() - 8.0f);
    g.setColour(juce::Colour(0xFF222225));
    g.fillRoundedRectangle(eqSection, 4.0f);

    // "EQ" section header
    g.setColour(juce::Colour(0xFF707070));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("EQ", static_cast<int>(eqSection.getX()) + 6, 6, 20, 10, juce::Justification::centredLeft);

    // Calculate where dynamics section starts (for background)
    int eqEndX = currentX + eqSectionWidth + 4;

    // ===== VERTICAL DIVIDER (professional double-line) =====
    int dividerX = eqEndX + 4;
    g.setColour(juce::Colour(0xFF151515));  // Shadow
    g.fillRect(dividerX, knobY - 12, 1, knobSize + 24);
    g.setColour(juce::Colour(0xFF454548));  // Highlight
    g.fillRect(dividerX + 2, knobY - 12, 1, knobSize + 24);

    // ===== DYNAMICS SECTION BACKGROUND =====
    int dynStartX = dividerX + 10;
    int dynSectionWidth = (knobSize + knobSpacing) * 5 + 60;  // 5 knobs + buttons
    juce::Colour dynBgColor = dynEnabled ? juce::Colour(0xFF28231e) : juce::Colour(0xFF1e1e20);
    juce::Rectangle<float> dynSection(static_cast<float>(dynStartX - 4), 4.0f,
                                       static_cast<float>(dynSectionWidth), bounds.getHeight() - 8.0f);
    g.setColour(dynBgColor);
    g.fillRoundedRectangle(dynSection, 4.0f);

    // Note: "DYNAMICS" label is drawn in paintOverChildren() below the knobs
}

void BandDetailPanel::paintOverChildren(juce::Graphics& g)
{
    // Draw knob labels and values ON TOP of the slider components
    // This is called after all child components have painted

    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int btnWidth = 48;

    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);
    bool dynEnabled = isDynamicsEnabled();

    // Calculate total content width to center it (must match resized() calculation)
    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10  // Band indicator + gap
                          + eqColumnsWidth + 10  // EQ section + separator gap
                          + 12  // Divider space
                          + (knobSize + knobSpacing) * 5 + 6  // 5 dynamics knobs + gap before buttons
                          + btnWidth;  // DYN and SOLO buttons

    // Center the content
    int startX = (getWidth() - totalContentWidth) / 2;

    // Format values for display WITH UNITS
    auto formatFreq = [](double val) {
        if (val >= 10000.0) return juce::String(val / 1000.0, 1) + " kHz";
        if (val >= 1000.0) return juce::String(val / 1000.0, 2) + " kHz";
        return juce::String(static_cast<int>(val)) + " Hz";
    };

    auto formatGain = [](double val) {
        juce::String sign = val >= 0 ? "+" : "";
        return sign + juce::String(val, 1) + " dB";
    };

    auto formatQ = [](double val) { return juce::String(val, 2); };

    auto formatMs = [](double val) {
        if (val >= 1000.0) return juce::String(val / 1000.0, 1) + " s";
        return juce::String(static_cast<int>(val)) + " ms";
    };

    auto formatDb = [](double val) { return juce::String(static_cast<int>(val)) + " dB"; };

    int currentX = startX + bandIndicatorSize + 10;  // Start after band indicator

    // FREQ
    drawKnobWithLabel(g, freqKnob.get(), "FREQ",
                      formatFreq(freqKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, false);
    currentX += knobSize + knobSpacing;

    // Q
    drawKnobWithLabel(g, qKnob.get(), "Q",
                      formatQ(qKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, false);
    currentX += knobSize + knobSpacing;

    // Third column: GAIN knob (for parametric/shelf), SLOPE label (for filters), or SHAPE label (for notch/bandpass)
    {
        bool isParametric = (selectedBand >= 2 && selectedBand <= 5);
        int currentShape = 0;
        if (isParametric)
        {
            auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(selectedBand + 1));
            if (shapeParam)
                currentShape = static_cast<int>(shapeParam->load());
        }
        bool isNotchOrBandPass = isParametric && (currentShape == 1 || currentShape == 2);

        if (isFilter)
        {
            // Draw SLOPE label for filter bands - centered over the knobSize column
            g.setColour(juce::Colour(0xFFb0b0b0));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("SLOPE", currentX, knobY - 14, knobSize, 14,
                       juce::Justification::centred);
        }
        else if (isNotchOrBandPass)
        {
            // Draw SHAPE label for notch/bandpass - centered over the column
            g.setColour(juce::Colour(0xFFb0b0b0));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("SHAPE", currentX, knobY - 14, knobSize, 14,
                       juce::Justification::centred);
        }
        else
        {
            // GAIN knob - same position and size as FREQ and Q
            drawKnobWithLabel(g, gainKnob.get(), "GAIN",
                              formatGain(gainKnob->getValue()),
                              {currentX, knobY, knobSize, knobSize + 20}, false);
        }
    }
    currentX += knobSize + knobSpacing + 10;

    // Skip separator space
    currentX += 12;

    // THRESHOLD - draw manually with offset to avoid overlapping DYNAMICS header
    // DYNAMICS header is at x=366, so THRESH label must start further right
    {
        float alpha = dynEnabled ? 1.0f : 0.3f;
        // Label above knob - starts 10px right of currentX to clear DYNAMICS header
        g.setColour(juce::Colour(0xFFb0b0b0).withAlpha(alpha));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("THRESH", currentX + 5, knobY - 14, knobSize, 14, juce::Justification::centred);

        // Value inside knob
        g.setColour(juce::Colour(0xFFe8e0d8).withAlpha(alpha));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        float centreX = currentX + knobSize / 2.0f;
        float centreY = knobY + knobSize / 2.0f;
        juce::Rectangle<int> valueRect(static_cast<int>(centreX - 35), static_cast<int>(centreY - 7), 70, 14);
        g.drawText(formatDb(thresholdKnob->getValue()), valueRect, juce::Justification::centred);
    }
    currentX += knobSize + knobSpacing;

    // ATTACK
    drawKnobWithLabel(g, attackKnob.get(), "ATTACK",
                      formatMs(attackKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
    currentX += knobSize + knobSpacing;

    // RELEASE
    drawKnobWithLabel(g, releaseKnob.get(), "RELEASE",
                      formatMs(releaseKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
    currentX += knobSize + knobSpacing;

    // RANGE
    drawKnobWithLabel(g, rangeKnob.get(), "RANGE",
                      formatDb(rangeKnob->getValue()),
                      {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
    currentX += knobSize + knobSpacing;

    // RATIO
    {
        auto formatRatio = [](double val) {
            if (val >= 99.5) return juce::String("Inf:1");
            return juce::String(val, 1) + ":1";
        };
        drawKnobWithLabel(g, ratioKnob.get(), "RATIO",
                          formatRatio(ratioKnob->getValue()),
                          {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
    }

    // ===== DYNAMICS SECTION LABEL (centered below the 4 dynamics knobs) =====
    // Calculate dynSection position (must match paint())
    int eqSectionWidth = eqColumnsWidth + 6;
    int eqEndX = (startX + bandIndicatorSize + 10) + eqSectionWidth + 4;
    int dividerX = eqEndX + 4;
    int dynStartX = dividerX + 10;
    int dynKnobsWidth = (knobSize + knobSpacing) * 5;  // Width of just the 5 knobs

    // Draw "DYNAMICS" label below knobs, centered over the 5 knobs (not the buttons)
    g.setColour(dynEnabled ? juce::Colour(0xFFff8844) : juce::Colour(0xFF505050));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    int labelY = knobY + knobSize + 4;  // Below the knobs
    g.drawText("DYNAMICS", dynStartX, labelY, dynKnobsWidth, 14, juce::Justification::centred);
}

void BandDetailPanel::drawKnobWithLabel(juce::Graphics& g, juce::Slider* knob,
                                         const juce::String& label, const juce::String& value,
                                         juce::Rectangle<int> bounds, bool dimmed)
{
    float alpha = dimmed ? 0.3f : 1.0f;
    int knobSize = 75;  // Match British mode knob size

    // Label above knob
    g.setColour(juce::Colour(0xFFb0b0b0).withAlpha(alpha));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(label, bounds.getX() - 10, bounds.getY() - 16, bounds.getWidth() + 20, 14,
               juce::Justification::centred);

    // F6 Style: Value INSIDE the knob center (on the tan/brown area)
    // Calculate center of the knob
    float centreX = bounds.getX() + knobSize / 2.0f;
    float centreY = bounds.getY() + knobSize / 2.0f;

    // Draw value text centered in the knob
    g.setColour(juce::Colour(0xFFe8e0d8).withAlpha(alpha));  // Light tan color for contrast
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));

    // Use a rectangle centered on the knob for the value text
    juce::Rectangle<int> valueRect(
        static_cast<int>(centreX - 35),
        static_cast<int>(centreY - 7),
        70, 14
    );
    g.drawText(value, valueRect, juce::Justification::centred);

    (void)knob;  // Unused for now
}

void BandDetailPanel::resized()
{
    // ===== CONTROLS AREA (no selector row - just knobs) =====
    int knobSize = 75;
    int knobSpacing = 10;         // Match paint() constants
    int bandIndicatorSize = 65;   // Larger for better visibility
    int knobY = 26;  // More room for section headers
    int btnWidth = 48;
    int slopeSelectorWidth = 95;  // Wider to show full "12 dB/oct" text

    // Calculate total content width to center it
    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    // Slope selector overlays the GAIN column when in filter mode
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10  // Band indicator + gap
                          + eqColumnsWidth + 10  // EQ section + separator gap
                          + 12  // Divider space
                          + (knobSize + knobSpacing) * 5 + 6  // 5 dynamics knobs + gap before buttons
                          + btnWidth;  // DYN and SOLO buttons

    // Center the content
    int startX = (getWidth() - totalContentWidth) / 2;

    // Band indicator takes space on the left (drawn in paint, no component)
    int currentX = startX + bandIndicatorSize + 10;  // Match paint() layout

    // FREQ knob
    freqKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    // Q knob
    qKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    // GAIN knob, SLOPE selector, and SHAPE selector share the same column
    // Position all, then control visibility based on band type and shape
    // Use same logic as updateControlsForBandType() for consistency
    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);
    bool hasShape = (selectedBand >= 1 && selectedBand <= 6);

    // Determine current shape for bands with shape selector
    int currentShape = 0;
    if (hasShape)
    {
        auto* shapeParam = processor.parameters.getRawParameterValue(ParamIDs::bandShape(selectedBand + 1));
        if (shapeParam)
            currentShape = static_cast<int>(shapeParam->load());
    }

    // For bands 3-6: shape 1=Notch, 2=BandPass (Q-only)
    // For band 2: shape 2=HPF (Q-only)
    // For band 7: shape 2=LPF (Q-only)
    bool isQOnly = false;
    if (selectedBand >= 2 && selectedBand <= 5)
        isQOnly = (currentShape == 1 || currentShape == 2);
    else if (selectedBand == 1 || selectedBand == 6)
        isQOnly = (currentShape == 2);

    // GAIN knob - positioned at the third column
    gainKnob->setBounds(currentX, knobY, knobSize, knobSize);
    gainKnob->setVisible(!isFilter && !isQOnly);

    // SLOPE selector - positioned over the same column, centered vertically
    int selectorHeight = 26;
    int selectorY = knobY + (knobSize - selectorHeight) / 2;
    int selectorX = currentX + (knobSize - slopeSelectorWidth) / 2;  // Center the wider dropdown
    slopeSelector->setBounds(selectorX, selectorY, slopeSelectorWidth, selectorHeight);
    slopeSelector->setVisible(isFilter);

    // SHAPE selector - positioned above the band indicator square on the left
    int shapeSelectorWidth = 80;
    int shapeSelectorHeight = 22;
    if (isQOnly)
    {
        // When gain is hidden, center the shape selector in the column
        int shapeSelectorX = currentX + (knobSize - shapeSelectorWidth) / 2;
        shapeSelector->setBounds(shapeSelectorX, selectorY, shapeSelectorWidth, selectorHeight);
    }
    else
    {
        // Position above the band indicator square (top-left of panel)
        shapeSelector->setBounds(startX, 5, shapeSelectorWidth, shapeSelectorHeight);
    }
    shapeSelector->setVisible(hasShape && !isFilter);

    // Ensure the visible control is on top
    if (isFilter)
        slopeSelector->toFront(false);
    else if (!isQOnly)
        gainKnob->toFront(false);

    currentX += knobSize + knobSpacing + 10;

    // Skip separator space
    currentX += 12;

    // THRESHOLD knob
    thresholdKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    // ATTACK knob
    attackKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    // RELEASE knob
    releaseKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    // RANGE knob
    rangeKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing;

    // RATIO knob
    ratioKnob->setBounds(currentX, knobY, knobSize, knobSize);
    currentX += knobSize + knobSpacing + 6;

    // DYN and SOLO buttons (stacked vertically on right)
    int btnHeight = 22;
    int btnY = knobY + (knobSize - btnHeight * 2 - 4) / 2;

    dynButton->setBounds(currentX, btnY, btnWidth, btnHeight);
    soloButton->setBounds(currentX, btnY + btnHeight + 4, btnWidth, btnHeight);

    // Per-band routing selector - positioned below the band indicator box
    int routingSelectorWidth = 65;
    int bandBoxY = knobY + (knobSize - bandIndicatorSize) / 2;
    int routingY = bandBoxY + bandIndicatorSize + 4;
    routingSelector->setBounds(startX, routingY, routingSelectorWidth, 18);
}
