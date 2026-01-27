#include "BandStripComponent.h"
#include "MultiQ.h"

//==============================================================================
BandStripComponent::BandStripComponent(MultiQ& proc)
    : processor(proc)
{
    // Setup all 8 band columns
    for (int i = 0; i < 8; ++i)
        setupBandColumn(i);

    // Listen for parameter changes
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.addParameterListener(ParamIDs::bandFreq(i), this);
        processor.parameters.addParameterListener(ParamIDs::bandGain(i), this);
        processor.parameters.addParameterListener(ParamIDs::bandQ(i), this);
        processor.parameters.addParameterListener(ParamIDs::bandEnabled(i), this);
        if (i == 1 || i == 8)
            processor.parameters.addParameterListener(ParamIDs::bandSlope(i), this);
    }
}

BandStripComponent::~BandStripComponent()
{
    // Remove parameter listeners
    for (int i = 1; i <= 8; ++i)
    {
        processor.parameters.removeParameterListener(ParamIDs::bandFreq(i), this);
        processor.parameters.removeParameterListener(ParamIDs::bandGain(i), this);
        processor.parameters.removeParameterListener(ParamIDs::bandQ(i), this);
        processor.parameters.removeParameterListener(ParamIDs::bandEnabled(i), this);
        if (i == 1 || i == 8)
            processor.parameters.removeParameterListener(ParamIDs::bandSlope(i), this);
    }
}

//==============================================================================
void BandStripComponent::setupBandColumn(int index)
{
    auto& col = bandColumns[static_cast<size_t>(index)];
    const auto& config = DefaultBandConfigs[static_cast<size_t>(index)];

    col.bandIndex = index;
    col.type = config.type;
    col.color = config.color;

    // Enable button - colored dot that shows band state
    col.enableButton = std::make_unique<juce::TextButton>();
    col.enableButton->setClickingTogglesState(true);
    col.enableButton->setButtonText("");
    // When ON (enabled): bright band color
    col.enableButton->setColour(juce::TextButton::buttonOnColourId, col.color);
    // When OFF (disabled): dim/dark
    col.enableButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF404040));
    // Text colors (not used but set for consistency)
    col.enableButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    col.enableButton->setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
    addAndMakeVisible(col.enableButton.get());

    // Create attachment for enable button
    col.enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, ParamIDs::bandEnabled(index + 1), *col.enableButton);

    // Frequency label
    col.freqLabel = std::make_unique<juce::Label>();
    setupEditableLabel(*col.freqLabel, "Frequency");
    addAndMakeVisible(col.freqLabel.get());

    // Create frequency parameter attachment
    if (auto* param = processor.parameters.getParameter(ParamIDs::bandFreq(index + 1)))
    {
        col.freqAttachment = std::make_unique<juce::ParameterAttachment>(
            *param,
            [this, index](float value) {
                juce::MessageManager::callAsync([this, index, value]() {
                    bandColumns[static_cast<size_t>(index)].freqLabel->setText(
                        formatFrequency(value), juce::dontSendNotification);
                });
            },
            nullptr);
        col.freqAttachment->sendInitialUpdate();
    }

    // Frequency label editing callback
    col.freqLabel->onTextChange = [this, index]() {
        auto& label = *bandColumns[static_cast<size_t>(index)].freqLabel;
        float value = parseFrequency(label.getText());
        if (auto* param = processor.parameters.getParameter(ParamIDs::bandFreq(index + 1)))
        {
            param->setValueNotifyingHost(param->convertTo0to1(value));
        }
    };

    // Gain label (or slope for filter bands)
    bool isFilterBand = (col.type == BandType::HighPass || col.type == BandType::LowPass);

    if (isFilterBand)
    {
        // Slope selector for HPF/LPF
        col.slopeSelector = std::make_unique<juce::ComboBox>();
        col.slopeSelector->addItem("6 dB", 1);
        col.slopeSelector->addItem("12 dB", 2);
        col.slopeSelector->addItem("18 dB", 3);
        col.slopeSelector->addItem("24 dB", 4);
        col.slopeSelector->addItem("36 dB", 5);
        col.slopeSelector->addItem("48 dB", 6);
        col.slopeSelector->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1a1a1a));
        col.slopeSelector->setColour(juce::ComboBox::textColourId, juce::Colour(0xFFa0a0a0));
        col.slopeSelector->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3a3a3a));
        addAndMakeVisible(col.slopeSelector.get());

        col.slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, ParamIDs::bandSlope(index + 1), *col.slopeSelector);
    }
    else
    {
        // Gain label for shelf and parametric bands
        col.gainLabel = std::make_unique<juce::Label>();
        setupEditableLabel(*col.gainLabel, "Gain");
        addAndMakeVisible(col.gainLabel.get());

        // Create gain parameter attachment
        if (auto* param = processor.parameters.getParameter(ParamIDs::bandGain(index + 1)))
        {
            col.gainAttachment = std::make_unique<juce::ParameterAttachment>(
                *param,
                [this, index](float value) {
                    juce::MessageManager::callAsync([this, index, value]() {
                        if (bandColumns[static_cast<size_t>(index)].gainLabel)
                            bandColumns[static_cast<size_t>(index)].gainLabel->setText(
                                formatGain(value), juce::dontSendNotification);
                    });
                },
                nullptr);
            col.gainAttachment->sendInitialUpdate();
        }

        // Gain label editing callback
        col.gainLabel->onTextChange = [this, index]() {
            auto& label = *bandColumns[static_cast<size_t>(index)].gainLabel;
            float value = parseGain(label.getText());
            if (auto* param = processor.parameters.getParameter(ParamIDs::bandGain(index + 1)))
            {
                param->setValueNotifyingHost(param->convertTo0to1(value));
            }
        };
    }

    // Q label
    col.qLabel = std::make_unique<juce::Label>();
    setupEditableLabel(*col.qLabel, "Q");
    addAndMakeVisible(col.qLabel.get());

    // Create Q parameter attachment
    if (auto* param = processor.parameters.getParameter(ParamIDs::bandQ(index + 1)))
    {
        col.qAttachment = std::make_unique<juce::ParameterAttachment>(
            *param,
            [this, index](float value) {
                juce::MessageManager::callAsync([this, index, value]() {
                    bandColumns[static_cast<size_t>(index)].qLabel->setText(
                        formatQ(value), juce::dontSendNotification);
                });
            },
            nullptr);
        col.qAttachment->sendInitialUpdate();
    }

    // Q label editing callback
    col.qLabel->onTextChange = [this, index]() {
        auto& label = *bandColumns[static_cast<size_t>(index)].qLabel;
        float value = parseQ(label.getText());
        if (auto* param = processor.parameters.getParameter(ParamIDs::bandQ(index + 1)))
        {
            param->setValueNotifyingHost(param->convertTo0to1(value));
        }
    };
}

void BandStripComponent::setupEditableLabel(juce::Label& label, const juce::String& tooltip)
{
    label.setEditable(false, true, false);  // single-click=false, double-click=true
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xFFe8e8e8));  // Brighter text for better contrast
    label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF252525));
    label.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    label.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xFF4080ff).withAlpha(0.4f));
    label.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF4080ff));
    label.setTooltip(tooltip);
    label.setFont(juce::Font(juce::FontOptions(13.0f)));  // Increased from 11.0f for better readability
}

//==============================================================================
void BandStripComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xFF1c1c1e));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Draw each band column
    float columnWidth = bounds.getWidth() / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        auto colBounds = bounds.withX(bounds.getX() + i * columnWidth).withWidth(columnWidth);
        drawBandColumn(g, i, colBounds);
    }
}

void BandStripComponent::drawBandColumn(juce::Graphics& g, int index, juce::Rectangle<float> bounds)
{
    const auto& col = bandColumns[static_cast<size_t>(index)];
    const auto& config = DefaultBandConfigs[static_cast<size_t>(index)];
    bool isSelected = (index == selectedBand);
    bool isEnabled = col.enableButton->getToggleState();

    // Column separator
    if (index > 0)
    {
        g.setColour(juce::Colour(0xFF3a3a3a));
        g.drawVerticalLine(static_cast<int>(bounds.getX()), bounds.getY() + 4, bounds.getBottom() - 4);
    }

    // Selection highlight
    if (isSelected)
    {
        drawSelectionHighlight(g, bounds, col.color);
    }

    // Top color accent bar
    auto accentBar = bounds.removeFromTop(3.0f).reduced(4.0f, 0);
    g.setColour(isEnabled ? col.color : col.color.withAlpha(0.2f));
    g.fillRoundedRectangle(accentBar, 1.5f);

    // Band type label - positioned to the right of the enable button (which is at x+4, width 10)
    auto labelBounds = bounds.removeFromTop(18.0f);
    labelBounds = labelBounds.withTrimmedLeft(18.0f);  // Make room for enable button on left
    g.setColour(isEnabled ? juce::Colour(0xFFc0c0c0) : juce::Colour(0xFF606060));
    g.setFont(juce::Font(juce::FontOptions(11.5f).withStyle("Bold")));  // Increased from 9.5f
    g.drawText(juce::String(index + 1) + ":" + config.name, labelBounds,
               juce::Justification::centred, false);
}

void BandStripComponent::drawSelectionHighlight(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour bandColor)
{
    // Subtle background tint
    g.setColour(bandColor.withAlpha(0.08f));
    g.fillRoundedRectangle(bounds.reduced(2.0f), 3.0f);

    // Subtle border glow
    g.setColour(bandColor.withAlpha(0.2f));
    g.drawRoundedRectangle(bounds.reduced(2.0f), 3.0f, 1.0f);
}

//==============================================================================
void BandStripComponent::resized()
{
    auto bounds = getLocalBounds();
    float columnWidth = static_cast<float>(bounds.getWidth()) / 8.0f;

    for (int i = 0; i < 8; ++i)
    {
        auto& col = bandColumns[static_cast<size_t>(i)];
        int colX = static_cast<int>(i * columnWidth);
        int colW = static_cast<int>(columnWidth);

        col.columnBounds = juce::Rectangle<int>(colX, 0, colW, bounds.getHeight());

        // Layout within column (top to bottom)
        // Row 1: Accent bar (3px) + Band label row with enable button
        int y = 6;  // After accent bar
        int padding = 5;
        int elementWidth = colW - padding * 2;
        int rowHeight = 22;  // Increased from 18 for larger fonts

        // Enable button - small dot to the LEFT of where band label is drawn
        int btnSize = 12;  // Slightly larger
        col.enableButton->setBounds(colX + 5, y + 3, btnSize, btnSize);

        // Skip the band label row (drawn in paint() from y~6 to y~24)
        y += 22;  // Increased for larger band label font

        // Frequency label
        col.freqLabel->setBounds(colX + padding, y, elementWidth, rowHeight);
        y += rowHeight + 2;

        // Gain/Slope (middle row)
        if (col.slopeSelector)
        {
            col.slopeSelector->setBounds(colX + padding, y, elementWidth, rowHeight);
        }
        else if (col.gainLabel)
        {
            col.gainLabel->setBounds(colX + padding, y, elementWidth, rowHeight);
        }
        y += rowHeight + 2;

        // Q label
        col.qLabel->setBounds(colX + padding, y, elementWidth, rowHeight);
    }
}

//==============================================================================
void BandStripComponent::mouseDown(const juce::MouseEvent& e)
{
    // Determine which band column was clicked
    for (int i = 0; i < 8; ++i)
    {
        if (bandColumns[static_cast<size_t>(i)].columnBounds.contains(e.getPosition()))
        {
            setSelectedBand(i);
            break;
        }
    }
}

void BandStripComponent::setSelectedBand(int bandIndex)
{
    // Accept -1 (no selection) or valid band index 0-7
    if ((bandIndex == -1 || (bandIndex >= 0 && bandIndex < 8)) && bandIndex != selectedBand)
    {
        selectedBand = bandIndex;
        repaint();

        // Notify callback (only for valid band selection)
        if (onBandSelected && bandIndex >= 0)
            onBandSelected(bandIndex);
    }
}

//==============================================================================
juce::String BandStripComponent::formatFrequency(float freq)
{
    if (freq >= 1000.0f)
        return juce::String(freq / 1000.0f, 2) + " kHz";
    else if (freq >= 100.0f)
        return juce::String(static_cast<int>(freq)) + " Hz";
    else
        return juce::String(freq, 1) + " Hz";
}

juce::String BandStripComponent::formatGain(float gain)
{
    juce::String sign = gain >= 0 ? "+" : "";
    return sign + juce::String(gain, 1) + " dB";
}

juce::String BandStripComponent::formatQ(float q)
{
    return juce::String(q, 2);
}

juce::String BandStripComponent::formatSlope(int slopeIndex)
{
    static const char* slopes[] = {"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"};
    if (slopeIndex >= 0 && slopeIndex < 6)
        return slopes[slopeIndex];
    return "12 dB/oct";
}

juce::String BandStripComponent::getBandTypeName(BandType type)
{
    switch (type)
    {
        case BandType::HighPass:   return "HPF";
        case BandType::LowShelf:   return "LSh";
        case BandType::Parametric: return "Para";
        case BandType::HighShelf:  return "HSh";
        case BandType::LowPass:    return "LPF";
        default:                   return "";
    }
}

//==============================================================================
float BandStripComponent::parseFrequency(const juce::String& text)
{
    juce::String clean = text.trim().toLowerCase();

    // Handle "k" suffix for kHz
    if (clean.endsWithChar('k'))
        return clean.dropLastCharacters(1).getFloatValue() * 1000.0f;

    // Handle explicit "hz" or "khz"
    if (clean.endsWith("khz") || clean.endsWith(" khz"))
        return clean.upToFirstOccurrenceOf("k", false, true).trim().getFloatValue() * 1000.0f;
    if (clean.endsWith("hz") || clean.endsWith(" hz"))
        return clean.upToFirstOccurrenceOf("h", false, true).trim().getFloatValue();

    // Plain number - use heuristic
    float value = clean.getFloatValue();

    // Auto-detect: if value is small (< 20), assume kHz
    if (value > 0 && value < 20)
        return value * 1000.0f;

    return juce::jlimit(20.0f, 20000.0f, value);
}

float BandStripComponent::parseGain(const juce::String& text)
{
    juce::String clean = text.trim().toLowerCase();

    // Remove "db" suffix
    if (clean.endsWith("db") || clean.endsWith(" db"))
        clean = clean.upToFirstOccurrenceOf("d", false, true).trim();

    float value = clean.getFloatValue();
    return juce::jlimit(-24.0f, 24.0f, value);
}

float BandStripComponent::parseQ(const juce::String& text)
{
    float value = text.trim().getFloatValue();
    return juce::jlimit(0.1f, 18.0f, value);
}

//==============================================================================
void BandStripComponent::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    // Extract band number from parameter ID
    if (parameterID.startsWith("band"))
    {
        int bandNum = parameterID.substring(4, 5).getIntValue();
        if (bandNum >= 1 && bandNum <= 8)
        {
            int index = bandNum - 1;
            juce::MessageManager::callAsync([this, index]() {
                updateBandValues(index);
            });
        }
    }
}

void BandStripComponent::updateBandValues(int index)
{
    auto& col = bandColumns[static_cast<size_t>(index)];

    // Update frequency
    if (auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandFreq(index + 1)))
        col.freqLabel->setText(formatFrequency(param->load()), juce::dontSendNotification);

    // Update gain (if applicable)
    if (col.gainLabel)
    {
        if (auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandGain(index + 1)))
            col.gainLabel->setText(formatGain(param->load()), juce::dontSendNotification);
    }

    // Update Q
    if (auto* param = processor.parameters.getRawParameterValue(ParamIDs::bandQ(index + 1)))
        col.qLabel->setText(formatQ(param->load()), juce::dontSendNotification);

    repaint();
}
