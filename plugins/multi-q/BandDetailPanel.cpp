#include "BandDetailPanel.h"
#include "MultiQ.h"
#include "F6KnobLookAndFeel.h"

// Static instance of F6 knob look and feel (shared by all instances)
static F6KnobLookAndFeel f6KnobLookAndFeel;

BandDetailPanel::BandDetailPanel(MultiQ& p)
    : processor(p)
{
    setupKnobs();
    setupMatchControls();
    updateAttachments();
    updateControlsForBandType();

    // Listen for dynamics enable changes to update opacity
    // Also listen for band enabled changes to update the indicator
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.addParameterListener(ParamIDs::bandDynEnabled(i), this);
        processor.parameters.addParameterListener(ParamIDs::bandEnabled(i), this);
    }
}

BandDetailPanel::~BandDetailPanel()
{
    // Remove parameter listeners
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.removeParameterListener(ParamIDs::bandDynEnabled(i), this);
        processor.parameters.removeParameterListener(ParamIDs::bandEnabled(i), this);
    }

    // Release LookAndFeel references before knobs are destroyed
    if (freqKnob) freqKnob->setLookAndFeel(nullptr);
    if (gainKnob) gainKnob->setLookAndFeel(nullptr);
    if (qKnob) qKnob->setLookAndFeel(nullptr);
    if (thresholdKnob) thresholdKnob->setLookAndFeel(nullptr);
    if (attackKnob) attackKnob->setLookAndFeel(nullptr);
    if (releaseKnob) releaseKnob->setLookAndFeel(nullptr);
    if (rangeKnob) rangeKnob->setLookAndFeel(nullptr);
    if (ratioKnob) ratioKnob->setLookAndFeel(nullptr);
}

void BandDetailPanel::setupBandButtons()
{
    // No longer using TextButtons - drawing manually in paint()
}

void BandDetailPanel::updateBandButtonColors()
{
    juce::Colour bandColor = getBandColor(selectedBand);
    freqKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);
    qKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);
    gainKnob->setColour(juce::Slider::rotarySliderFillColourId, bandColor);

    repaint();
}

void BandDetailPanel::setupKnobs()
{
    auto setupRotaryKnob = [this](std::unique_ptr<juce::Slider>& knob) {
        knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
        knob->setLookAndFeel(&f6KnobLookAndFeel);
        knob->setColour(juce::Slider::rotarySliderFillColourId, getBandColor(selectedBand));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF404040));
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
    slopeSelector->addItem("72 dB/oct", 7);
    slopeSelector->addItem("96 dB/oct", 8);
    slopeSelector->setTooltip("Filter slope: Steeper = sharper cutoff (6-96 dB/octave)");
    addAndMakeVisible(slopeSelector.get());

    // Dynamics knobs (use orange for dynamics section)
    auto setupDynKnob = [this](std::unique_ptr<juce::Slider>& knob) {
        knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
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
    ratioKnob->setTooltip("Ratio: Compression ratio (1:1 = no compression, 20:1 = heavy limiting)");

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
        int band = selectedBand.load();
        if (soloButton->getToggleState())
            processor.setSoloedBand(band);
        else
            processor.setSoloedBand(-1);  // No solo
    };
    addAndMakeVisible(soloButton.get());
}

void BandDetailPanel::setupMatchControls()
{
    matchCaptureRefButton.setTooltip("Capture current analyzer spectrum as the reference (what you want to sound like)");
    matchCaptureRefButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4a3a));
    matchCaptureRefButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88ccaa));
    matchCaptureRefButton.onClick = [this]() {
        processor.captureMatchReference();
        matchCaptureRefButton.setButtonText("Ref ✓");
        matchCaptureRefButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff44bb66));
        matchCaptureRefButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        if (processor.hasMatchSource())
            matchComputeButton.setEnabled(true);
    };
    matchCaptureRefButton.setVisible(false);
    addAndMakeVisible(matchCaptureRefButton);

    matchCaptureSrcButton.setTooltip("Capture current analyzer spectrum as the source (what your signal sounds like)");
    matchCaptureSrcButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3a4a));
    matchCaptureSrcButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88aacc));
    matchCaptureSrcButton.onClick = [this]() {
        processor.captureMatchSource();
        matchCaptureSrcButton.setButtonText("Src ✓");
        matchCaptureSrcButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4488cc));
        matchCaptureSrcButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        if (processor.hasMatchReference())
            matchComputeButton.setEnabled(true);
    };
    matchCaptureSrcButton.setVisible(false);
    addAndMakeVisible(matchCaptureSrcButton);

    matchComputeButton.setTooltip("Compute and apply EQ match (fits bands 2-7 to match reference)");
    matchComputeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a4030));
    matchComputeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffccaa88));
    matchComputeButton.setEnabled(false);
    matchComputeButton.onClick = [this]() {
        int bandsUsed = processor.computeEQMatch();
        if (bandsUsed > 0)
        {
            processor.applyEQMatch();
            matchComputeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc8844));
            matchComputeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
    };
    matchComputeButton.setVisible(false);
    addAndMakeVisible(matchComputeButton);

    matchClearButton.setTooltip("Clear captured spectra and reset match state");
    matchClearButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a4a4a));
    matchClearButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff999999));
    matchClearButton.onClick = [this]() {
        processor.clearEQMatch();
        matchCaptureRefButton.setButtonText("Capture Ref");
        matchCaptureRefButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4a3a));
        matchCaptureRefButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88ccaa));
        matchCaptureSrcButton.setButtonText("Capture Source");
        matchCaptureSrcButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a3a4a));
        matchCaptureSrcButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff88aacc));
        matchComputeButton.setEnabled(false);
        matchComputeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a4030));
        matchComputeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffccaa88));
    };
    matchClearButton.setVisible(false);
    addAndMakeVisible(matchClearButton);

    matchStrengthSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                          juce::Slider::TextBoxRight);
    matchStrengthSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    matchStrengthSlider->setTooltip("Match strength: how aggressively to match the reference spectrum (0-100%)");
    matchStrengthSlider->setVisible(false);
    addAndMakeVisible(matchStrengthSlider.get());
    matchStrengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, ParamIDs::matchStrength, *matchStrengthSlider);
}

void BandDetailPanel::setMatchMode(bool isMatch)
{
    if (matchMode == isMatch)
        return;

    matchMode = isMatch;

    // Toggle visibility of dynamics vs match controls
    bool showDyn = !matchMode;
    thresholdKnob->setVisible(showDyn);
    attackKnob->setVisible(showDyn);
    releaseKnob->setVisible(showDyn);
    rangeKnob->setVisible(showDyn);
    ratioKnob->setVisible(showDyn);
    dynButton->setVisible(showDyn);
    soloButton->setVisible(showDyn);

    matchCaptureRefButton.setVisible(matchMode);
    matchCaptureSrcButton.setVisible(matchMode);
    matchComputeButton.setVisible(matchMode);
    matchClearButton.setVisible(matchMode);
    matchStrengthSlider->setVisible(matchMode);

    resized();
    repaint();
}

void BandDetailPanel::setSelectedBand(int bandIndex)
{
    if (bandIndex == selectedBand.load())
        return;

    selectedBand.store(juce::jlimit(-1, 7, bandIndex));
    updateAttachments();
    updateControlsForBandType();
    updateDynamicsOpacity();
    updateBandButtonColors();

    int band = selectedBand.load();
    if (band >= 0)
    {
        juce::Colour bandColor = getBandColor(band);
        dynButton->setColour(juce::TextButton::buttonOnColourId, bandColor);

        bool isSoloed = processor.isBandSoloed(band);
        soloButton->setToggleState(isSoloed, juce::dontSendNotification);
    }
    else
    {
        soloButton->setToggleState(false, juce::dontSendNotification);
    }

    // Recalculate layout (ensures knob bounds are set correctly)
    resized();
    repaint();
}

void BandDetailPanel::updateAttachments()
{
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    slopeAttachment.reset();
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

    if (type != BandType::HighPass && type != BandType::LowPass)
    {
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, ParamIDs::bandGain(bandNum), *gainKnob);
    }

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

    slopeSelector->setVisible(isFilter);
    gainKnob->setVisible(!isFilter);

    // Ensure the visible control is on top (z-order) and force repaint
    if (isFilter)
    {
        slopeSelector->toFront(false);
        slopeSelector->repaint();
    }
    else
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
    // Cache selectedBand locally to avoid race with GUI thread updates
    int band = selectedBand.load();
    if (band < 0 || band >= 8)
        return;

    if (parameterID == ParamIDs::bandDynEnabled(band + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this), band]() {
            if (safeThis != nullptr && safeThis->selectedBand.load() == band)
            {
                safeThis->updateDynamicsOpacity();
                safeThis->repaint();
            }
        });
    }

    if (parameterID == ParamIDs::bandEnabled(band + 1))
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<BandDetailPanel>(this), band]() {
            if (safeThis != nullptr && safeThis->selectedBand.load() == band)
                safeThis->repaint();
        });
    }
}

juce::Rectangle<int> BandDetailPanel::getBandButtonBounds() const
{
    // Returns bounds of the band indicator box (must match paint() centering calculation)
    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;
    int knobY = 26;
    int btnWidth = 48;

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
    auto bandBox = getBandButtonBounds();
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

    // ===== CONTROLS AREA =====
    int knobSize = 75;
    int knobSpacing = 10;
    int bandIndicatorSize = 65;  // Larger for better visibility
    int knobY = 26;  // More room for section headers
    int btnWidth = 48;

    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10  // Band indicator + gap
                          + eqColumnsWidth + 10  // EQ section + separator gap
                          + 12  // Divider space
                          + (knobSize + knobSpacing) * 5 + 6  // 5 dynamics knobs + button gap
                          + btnWidth;  // Buttons

    // Center the content
    int startX = (getWidth() - totalContentWidth) / 2;

    bool dynEnabled = isDynamicsEnabled();

    // ===== BAND INDICATOR BOX (left of FREQ) - only show if a band is selected =====
    int bandBoxX = startX;
    int bandBoxY = knobY + (knobSize - bandIndicatorSize) / 2;
    juce::Rectangle<float> bandBox(static_cast<float>(bandBoxX), static_cast<float>(bandBoxY),
                                    static_cast<float>(bandIndicatorSize), static_cast<float>(bandIndicatorSize));

    int rawBand = selectedBand.load();
    if (rawBand >= 0 && rawBand < 8)
    {
        int bandIdx = rawBand;
        juce::Colour bandColor = getBandColor(bandIdx);

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
    }
    else
    {
        // No band selected — draw neutral empty indicator
        g.setColour(juce::Colour(0xFF2a2a2c));
        g.fillRoundedRectangle(bandBox, 8.0f);
        g.setColour(juce::Colour(0xFF3a3a3c));
        g.drawRoundedRectangle(bandBox.reduced(1.0f), 7.0f, 1.0f);
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

    int eqEndX = currentX + eqSectionWidth + 4;

    // ===== VERTICAL DIVIDER (professional double-line) =====
    int dividerX = eqEndX + 4;
    g.setColour(juce::Colour(0xFF151515));  // Shadow
    g.fillRect(dividerX, knobY - 12, 1, knobSize + 24);
    g.setColour(juce::Colour(0xFF454548));  // Highlight
    g.fillRect(dividerX + 2, knobY - 12, 1, knobSize + 24);

    // ===== RIGHT SECTION BACKGROUND (dynamics or match) =====
    int rightStartX = dividerX + 10;
    int rightSectionWidth = (knobSize + knobSpacing) * 5 + 60;  // 5 knobs + buttons
    juce::Colour rightBgColor;
    if (matchMode)
        rightBgColor = juce::Colour(0xFF1e2825);  // Teal tint for match section
    else
        rightBgColor = dynEnabled ? juce::Colour(0xFF28231e) : juce::Colour(0xFF1e1e20);
    juce::Rectangle<float> rightSection(static_cast<float>(rightStartX - 4), 4.0f,
                                         static_cast<float>(rightSectionWidth), bounds.getHeight() - 8.0f);
    g.setColour(rightBgColor);
    g.fillRoundedRectangle(rightSection, 4.0f);

    // Note: Section label is drawn in paintOverChildren()
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

    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10  // Band indicator + gap
                          + eqColumnsWidth + 10  // EQ section + separator gap
                          + 12  // Divider space
                          + (knobSize + knobSpacing) * 5 + 6  // 5 dynamics knobs + button gap
                          + btnWidth;  // Buttons

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

    // Third column: GAIN knob (for parametric/shelf) or SLOPE label (for filters)
    if (!isFilter)
    {
        // GAIN knob - same position and size as FREQ and Q
        drawKnobWithLabel(g, gainKnob.get(), "GAIN",
                          formatGain(gainKnob->getValue()),
                          {currentX, knobY, knobSize, knobSize + 20}, false);
    }
    else
    {
        // Draw SLOPE label for filter bands - centered over the knobSize column
        g.setColour(juce::Colour(0xFFb0b0b0));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("SLOPE", currentX, knobY - 14, knobSize, 14,
                   juce::Justification::centred);
    }
    currentX += knobSize + knobSpacing + 10;

    // Skip separator space
    currentX += 12;

    // ===== RIGHT SECTION LABELS (dynamics or match) =====
    int eqSectionWidth = eqColumnsWidth + 6;
    int eqEndX = (startX + bandIndicatorSize + 10) + eqSectionWidth + 4;
    int dividerX = eqEndX + 4;
    int rightStartX = dividerX + 10;
    int rightKnobsWidth = (knobSize + knobSpacing) * 5;

    if (matchMode)
    {
        // "STRENGTH" label to the left of the slider
        g.setColour(juce::Colour(0xFF909090));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        auto sliderBounds = matchStrengthSlider->getBounds();
        g.drawText("STRENGTH", sliderBounds.getX(), sliderBounds.getY() - 14,
                   80, 12, juce::Justification::centredLeft);

        // "MATCH EQ" section label
        g.setColour(juce::Colour(0xFF44aa88));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("MATCH EQ", rightStartX, 6, 60, 10, juce::Justification::centredLeft);
    }
    else
    {
        // THRESHOLD - draw manually with offset to avoid overlapping DYNAMICS header
        {
            float alpha = dynEnabled ? 1.0f : 0.3f;
            g.setColour(juce::Colour(0xFFb0b0b0).withAlpha(alpha));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("THRESH", currentX + 5, knobY - 14, knobSize, 14, juce::Justification::centred);

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
            juce::String ratioStr = juce::String(ratioKnob->getValue(), 1) + ":1";
            drawKnobWithLabel(g, ratioKnob.get(), "RATIO", ratioStr,
                              {currentX, knobY, knobSize, knobSize + 20}, !dynEnabled);
        }

        // "DYNAMICS" section label below knobs
        g.setColour(dynEnabled ? juce::Colour(0xFFff8844) : juce::Colour(0xFF505050));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        int labelY = knobY + knobSize + 4;
        g.drawText("DYNAMICS", rightStartX, labelY, rightKnobsWidth, 14, juce::Justification::centred);
    }
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

    // EQ section: 3 knobs (FREQ, Q, GAIN) - always use knobSize for consistent layout
    // Slope selector overlays the GAIN column when in filter mode
    int eqColumnsWidth = (knobSize + knobSpacing) * 3;
    int totalContentWidth = bandIndicatorSize + 10  // Band indicator + gap
                          + eqColumnsWidth + 10  // EQ section + separator gap
                          + 12  // Divider space
                          + (knobSize + knobSpacing) * 5 + 6  // 5 dynamics knobs + button gap
                          + btnWidth;  // Buttons

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

    // GAIN knob and SLOPE selector share the same column
    // Position both, then control visibility based on band type
    BandType type = getBandType(selectedBand);
    bool isFilter = (type == BandType::HighPass || type == BandType::LowPass);

    // GAIN knob - positioned at the third column
    gainKnob->setBounds(currentX, knobY, knobSize, knobSize);
    gainKnob->setVisible(!isFilter);

    // SLOPE selector - positioned over the same column, centered vertically
    int selectorHeight = 26;
    int selectorY = knobY + (knobSize - selectorHeight) / 2;
    int selectorX = currentX + (knobSize - slopeSelectorWidth) / 2;  // Center the wider dropdown
    slopeSelector->setBounds(selectorX, selectorY, slopeSelectorWidth, selectorHeight);
    slopeSelector->setVisible(isFilter);

    // Ensure the visible control is on top
    if (isFilter)
        slopeSelector->toFront(false);
    else
        gainKnob->toFront(false);

    currentX += knobSize + knobSpacing + 10;

    // Skip separator space
    currentX += 12;

    if (matchMode)
    {
        // Two-row layout for match controls
        // Row 1 (upper): [Capture Ref]  [Capture Source]
        // Row 2 (lower): STRENGTH [====slider====]  [Match]  [Clear]
        int btnHeight = 32;
        int rowGap = 6;
        int totalRows = btnHeight * 2 + rowGap;
        int row1Y = knobY + (knobSize - totalRows) / 2;
        int row2Y = row1Y + btnHeight + rowGap;

        // Row 1: Capture buttons — wider for prominence
        int capBtnWidth = 130;
        int capGap = 10;
        int row1Width = capBtnWidth * 2 + capGap;
        int row1X = currentX + ((knobSize + knobSpacing) * 5 + btnWidth - row1Width) / 2;  // Center in right section
        matchCaptureRefButton.setBounds(row1X, row1Y, capBtnWidth, btnHeight);
        matchCaptureSrcButton.setBounds(row1X + capBtnWidth + capGap, row1Y, capBtnWidth, btnHeight);

        // Row 2: Strength slider + Match + Clear
        int matchBtnWidth = 70;
        int clearBtnWidth = 60;
        int sliderWidth = (knobSize + knobSpacing) * 5 + btnWidth - matchBtnWidth - clearBtnWidth - 20;
        matchStrengthSlider->setBounds(currentX, row2Y, sliderWidth, btnHeight);
        matchComputeButton.setBounds(currentX + sliderWidth + 8, row2Y, matchBtnWidth, btnHeight);
        matchClearButton.setBounds(currentX + sliderWidth + 8 + matchBtnWidth + 6, row2Y, clearBtnWidth, btnHeight);
    }
    else
    {
        // Dynamics knobs + buttons
        thresholdKnob->setBounds(currentX, knobY, knobSize, knobSize);
        currentX += knobSize + knobSpacing;

        attackKnob->setBounds(currentX, knobY, knobSize, knobSize);
        currentX += knobSize + knobSpacing;

        releaseKnob->setBounds(currentX, knobY, knobSize, knobSize);
        currentX += knobSize + knobSpacing;

        rangeKnob->setBounds(currentX, knobY, knobSize, knobSize);
        currentX += knobSize + knobSpacing;

        ratioKnob->setBounds(currentX, knobY, knobSize, knobSize);
        currentX += knobSize + knobSpacing + 6;

        int btnHeight = 22;
        int btnY = knobY + (knobSize - btnHeight * 2 - 4) / 2;

        dynButton->setBounds(currentX, btnY, btnWidth, btnHeight);
        soloButton->setBounds(currentX, btnY + btnHeight + 4, btnWidth, btnHeight);
    }
}
