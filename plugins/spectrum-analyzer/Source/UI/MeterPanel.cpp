#include "MeterPanel.h"

//==============================================================================
MeterPanel::MeterPanel()
{
    // Output meters are now in the main editor on the right side
}

void MeterPanel::resized()
{
    auto bounds = getLocalBounds();

    // Three panels now (output meters moved to right side of spectrum)
    int panelWidth = bounds.getWidth() / 3;

    correlationArea = bounds.removeFromLeft(panelWidth).reduced(5);
    truePeakArea = bounds.removeFromLeft(panelWidth).reduced(5);
    lufsArea = bounds.reduced(5);  // LUFS gets remaining space
}

//==============================================================================
void MeterPanel::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Draw each panel (output meters are now on the right side of the spectrum display)
    drawCorrelationMeter(g, correlationArea);
    drawTruePeakMeter(g, truePeakArea);
    drawLUFSMeter(g, lufsArea);
}

//==============================================================================
void MeterPanel::drawCorrelationMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Panel background
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);

    // Title
    g.setColour(juce::Colour(0xff888888));
    g.setFont(10.0f);
    g.drawText("CORRELATION", area.removeFromTop(18), juce::Justification::centred);

    // Value
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(juce::String(correlation, 2), area.removeFromTop(20), juce::Justification::centred);

    // Meter bar area
    auto meterArea = area.reduced(10, 5);
    int meterHeight = 12;
    auto barArea = meterArea.withHeight(meterHeight).withY(meterArea.getCentreY() - meterHeight / 2);

    // Background
    g.setColour(juce::Colour(0xff333333));
    g.fillRoundedRectangle(barArea.toFloat(), 3.0f);

    // Correlation indicator (-1 to +1, center is 0)
    float normalizedCorr = (correlation + 1.0f) * 0.5f;  // 0 to 1
    int indicatorX = static_cast<int>(barArea.getX() + normalizedCorr * barArea.getWidth());

    // Color based on correlation
    juce::Colour corrColor;
    if (correlation > 0.5f)
        corrColor = juce::Colour(0xff00cc00);  // Green - good
    else if (correlation > 0.0f)
        corrColor = juce::Colour(0xffcccc00);  // Yellow - wide
    else
        corrColor = juce::Colour(0xffcc0000);  // Red - out of phase

    // Draw fill from center to indicator
    int centerX = barArea.getCentreX();
    if (indicatorX > centerX)
    {
        g.setColour(corrColor.withAlpha(0.7f));
        g.fillRect(centerX, barArea.getY(), indicatorX - centerX, barArea.getHeight());
    }
    else
    {
        g.setColour(corrColor.withAlpha(0.7f));
        g.fillRect(indicatorX, barArea.getY(), centerX - indicatorX, barArea.getHeight());
    }

    // Draw indicator line
    g.setColour(juce::Colours::white);
    g.drawVerticalLine(indicatorX, static_cast<float>(barArea.getY() - 2),
        static_cast<float>(barArea.getBottom() + 2));

    // Center line
    g.setColour(juce::Colour(0xff666666));
    g.drawVerticalLine(centerX, static_cast<float>(barArea.getY()),
        static_cast<float>(barArea.getBottom()));

    // Labels
    g.setColour(juce::Colour(0xff666666));
    g.setFont(9.0f);
    g.drawText("-1", barArea.getX() - 5, barArea.getBottom() + 2, 20, 12, juce::Justification::centred);
    g.drawText("+1", barArea.getRight() - 15, barArea.getBottom() + 2, 20, 12, juce::Justification::centred);
}

void MeterPanel::drawTruePeakMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Panel background
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);

    // Title
    g.setColour(juce::Colour(0xff888888));
    g.setFont(10.0f);
    g.drawText("TRUE PEAK", area.removeFromTop(18), juce::Justification::centred);

    // Max value
    float maxTP = std::max(truePeakL, truePeakR);
    juce::Colour valueColor = maxTP > -0.1f ? juce::Colour(0xffff4444) : juce::Colours::white;
    g.setColour(valueColor);
    g.setFont(14.0f);
    g.drawText(formatDB(maxTP) + " dBTP", area.removeFromTop(20), juce::Justification::centred);

    // L/R bars
    auto meterArea = area.reduced(15, 5);
    int barHeight = 10;
    int spacing = 4;

    auto barL = meterArea.removeFromTop(barHeight);
    meterArea.removeFromTop(spacing);
    auto barR = meterArea.removeFromTop(barHeight);

    // Draw bars
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& bar = (ch == 0) ? barL : barR;
        float db = (ch == 0) ? truePeakL : truePeakR;

        // Background
        g.setColour(juce::Colour(0xff333333));
        g.fillRoundedRectangle(bar.toFloat(), 2.0f);

        // Level (-60 to 0 dBTP)
        float normalized = juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f);
        normalized = juce::jlimit(0.0f, 1.0f, normalized);

        juce::Colour barColor = db > -0.1f ? juce::Colour(0xffff4444) :
                                db > -6.0f ? juce::Colour(0xffcccc00) :
                                             juce::Colour(0xff00cc00);

        g.setColour(barColor);
        g.fillRoundedRectangle(bar.toFloat().withWidth(bar.getWidth() * normalized), 2.0f);

        // Label
        g.setColour(juce::Colour(0xff888888));
        g.setFont(9.0f);
        g.drawText(ch == 0 ? "L" : "R", bar.getX() - 12, bar.getY(), 10, bar.getHeight(),
            juce::Justification::centredRight);
    }

    // Clip indicator
    area.removeFromTop(5);
    g.setFont(10.0f);
    if (clipping)
    {
        g.setColour(juce::Colour(0xffff4444));
        g.drawText("CLIP!", area.removeFromTop(15), juce::Justification::centred);
    }
    else
    {
        g.setColour(juce::Colour(0xff00cc00));
        g.drawText("OK", area.removeFromTop(15), juce::Justification::centred);
    }
}

void MeterPanel::drawLUFSMeter(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Panel background
    g.setColour(juce::Colour(0xff252525));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);

    // Title
    g.setColour(juce::Colour(0xff888888));
    g.setFont(11.0f);
    g.drawText("LOUDNESS", area.removeFromTop(20), juce::Justification::centred);

    area.reduce(10, 0);
    int rowHeight = 22;

    // Momentary - larger font
    auto row = area.removeFromTop(rowHeight);
    g.setColour(juce::Colour(0xff888888));
    g.setFont(12.0f);
    g.drawText("M:", row.removeFromLeft(30), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(formatLUFS(momentaryLUFS), row, juce::Justification::centredRight);

    // Short-term - larger font
    row = area.removeFromTop(rowHeight);
    g.setColour(juce::Colour(0xff888888));
    g.setFont(12.0f);
    g.drawText("S:", row.removeFromLeft(30), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(formatLUFS(shortTermLUFS), row, juce::Justification::centredRight);

    // Integrated - highlighted and larger
    row = area.removeFromTop(rowHeight);
    g.setColour(juce::Colour(0xff888888));
    g.setFont(12.0f);
    g.drawText("I:", row.removeFromLeft(30), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff00aaff));  // Highlight integrated
    g.setFont(15.0f);
    g.drawText(formatLUFS(integratedLUFS), row, juce::Justification::centredRight);

    // LRA - larger font
    row = area.removeFromTop(rowHeight);
    g.setColour(juce::Colour(0xff888888));
    g.setFont(12.0f);
    g.drawText("LRA:", row.removeFromLeft(40), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    juce::String lraStr = loudnessRange > 0.1f ? juce::String(loudnessRange, 1) + " LU" : "-- LU";
    g.drawText(lraStr, row, juce::Justification::centredRight);
}


//==============================================================================
void MeterPanel::setCorrelation(float c)
{
    correlation = juce::jlimit(-1.0f, 1.0f, c);
    repaint(correlationArea);
}

void MeterPanel::setTruePeakL(float dbTP)
{
    truePeakL = dbTP;
    repaint(truePeakArea);
}

void MeterPanel::setTruePeakR(float dbTP)
{
    truePeakR = dbTP;
    repaint(truePeakArea);
}

void MeterPanel::setClipping(bool clip)
{
    clipping = clip;
    repaint(truePeakArea);
}

void MeterPanel::setMomentaryLUFS(float lufs)
{
    momentaryLUFS = lufs;
    repaint(lufsArea);
}

void MeterPanel::setShortTermLUFS(float lufs)
{
    shortTermLUFS = lufs;
    repaint(lufsArea);
}

void MeterPanel::setIntegratedLUFS(float lufs)
{
    integratedLUFS = lufs;
    repaint(lufsArea);
}

void MeterPanel::setLoudnessRange(float lra)
{
    loudnessRange = lra;
    repaint(lufsArea);
}

void MeterPanel::setOutputLevelL(float db)
{
    outputLevelL = db;
    // LED meters are now in the main editor
}

void MeterPanel::setOutputLevelR(float db)
{
    outputLevelR = db;
    // LED meters are now in the main editor
}

void MeterPanel::setRmsLevel(float db)
{
    rmsLevel = db;
    // RMS display is now in the main editor
}

//==============================================================================
juce::String MeterPanel::formatLUFS(float lufs) const
{
    if (lufs < -99.0f)
        return "-- LUFS";
    return juce::String(lufs, 1) + " LUFS";
}

juce::String MeterPanel::formatDB(float db) const
{
    if (db < -99.0f)
        return "-inf";
    return juce::String(db, 1);
}
