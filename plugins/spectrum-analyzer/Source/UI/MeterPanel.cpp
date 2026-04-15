#include "MeterPanel.h"

//==============================================================================
void MeterPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10, 0);
    int sectionWidth = bounds.getWidth() / 3;

    correlationArea_ = bounds.removeFromLeft(sectionWidth);
    truePeakArea_ = bounds.removeFromLeft(sectionWidth);
    lufsArea_ = bounds;
}

//==============================================================================
void MeterPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Top border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

    auto cy = getHeight() / 2;

    // --- Correlation ---
    {
        auto area = correlationArea_;

        // "Corr" label
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        g.drawText("Corr", area.removeFromLeft(36), juce::Justification::centredRight);
        area.removeFromLeft(4);

        // "-1" label
        g.setColour(juce::Colour(0xff666666));
        g.setFont(12.0f);
        g.drawText("-1", area.removeFromLeft(16), juce::Justification::centredRight);
        area.removeFromLeft(3);

        // Bar takes most of the space
        auto barRight = area;
        barRight.removeFromRight(16 + 3 + 42 + 4); // reserve for "+1" label + value
        int barW = barRight.getWidth();
        int barH = 10;
        auto barArea = juce::Rectangle<int>(barRight.getX(), cy - barH / 2, barW, barH);

        // Bar background
        g.setColour(juce::Colour(0xff333333));
        g.fillRoundedRectangle(barArea.toFloat(), 3.0f);

        // Fill from center to indicator position
        float normalized = (correlation_ + 1.0f) * 0.5f;
        int centerX = barArea.getCentreX();
        int indicatorX = barArea.getX() + static_cast<int>(normalized * static_cast<float>(barW));

        juce::Colour corrColor = correlation_ > 0.5f  ? juce::Colour(0xff00cc00) :
                                 correlation_ > 0.0f  ? juce::Colour(0xffcccc00) :
                                                         juce::Colour(0xffcc0000);
        g.setColour(corrColor.withAlpha(0.8f));
        if (indicatorX > centerX)
            g.fillRect(centerX, barArea.getY(), indicatorX - centerX, barH);
        else
            g.fillRect(indicatorX, barArea.getY(), centerX - indicatorX, barH);

        // Center line
        g.setColour(juce::Colour(0xff888888));
        g.drawVerticalLine(centerX, static_cast<float>(barArea.getY() - 1),
            static_cast<float>(barArea.getBottom() + 1));

        // Indicator line
        g.setColour(juce::Colours::white);
        g.drawVerticalLine(indicatorX, static_cast<float>(barArea.getY() - 2),
            static_cast<float>(barArea.getBottom() + 2));

        // "+1" label
        auto rightArea = area;
        rightArea.removeFromLeft(barW + 3);
        g.setColour(juce::Colour(0xff666666));
        g.setFont(12.0f);
        g.drawText("+1", rightArea.removeFromLeft(16), juce::Justification::centredLeft);
        rightArea.removeFromLeft(4);

        // Value
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(juce::String(correlation_, 2), rightArea.removeFromLeft(42), juce::Justification::centredLeft);
    }

    // Vertical divider
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawVerticalLine(truePeakArea_.getX() - 5, 8.0f, static_cast<float>(getHeight() - 8));

    // --- True Peak ---
    {
        auto area = truePeakArea_;
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        auto labelArea = area.removeFromLeft(30);
        g.drawText("TP:", labelArea, juce::Justification::centredRight);
        area.removeFromLeft(4);

        float maxTP = std::max(truePeakL_, truePeakR_);
        bool hot = maxTP > -0.1f;

        g.setColour(hot ? juce::Colour(0xffff4444) : juce::Colours::white);
        g.setFont(14.0f);
        auto valArea = area.removeFromLeft(95);
        g.drawText(formatDB(maxTP) + " dBTP", valArea, juce::Justification::centredLeft);
        area.removeFromLeft(6);

        // Clip indicator dot
        int dotSize = 8;
        auto dotArea = juce::Rectangle<int>(area.getX(), cy - dotSize / 2, dotSize, dotSize);
        g.setColour(clipping_ ? juce::Colour(0xffff4444) : juce::Colour(0xff00cc00));
        g.fillEllipse(dotArea.toFloat());
    }

    // Vertical divider
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawVerticalLine(lufsArea_.getX() - 5, 8.0f, static_cast<float>(getHeight() - 8));

    // --- LUFS ---
    {
        auto area = lufsArea_;
        int labelW = 24;
        int valW = 80;
        int spacing = 2;

        // M:
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        g.drawText("M:", area.removeFromLeft(labelW), juce::Justification::centredRight);
        area.removeFromLeft(2);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(formatLUFS(momentaryLUFS_), area.removeFromLeft(valW), juce::Justification::centredLeft);
        area.removeFromLeft(spacing);

        // S:
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        g.drawText("S:", area.removeFromLeft(labelW), juce::Justification::centredRight);
        area.removeFromLeft(2);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(formatLUFS(shortTermLUFS_), area.removeFromLeft(valW), juce::Justification::centredLeft);
        area.removeFromLeft(spacing);

        // I: (highlighted)
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        g.drawText("I:", area.removeFromLeft(labelW), juce::Justification::centredRight);
        area.removeFromLeft(2);
        g.setColour(juce::Colour(0xff00aaff));
        g.setFont(14.0f);
        g.drawText(formatLUFS(integratedLUFS_), area.removeFromLeft(valW), juce::Justification::centredLeft);
        area.removeFromLeft(spacing);

        // LRA:
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        g.drawText("LRA:", area.removeFromLeft(34), juce::Justification::centredRight);
        area.removeFromLeft(2);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        juce::String lraStr = loudnessRange_ > 0.1f ? juce::String(loudnessRange_, 1) + " LU" : "-- LU";
        g.drawText(lraStr, area.removeFromLeft(55), juce::Justification::centredLeft);
    }
}

//==============================================================================
void MeterPanel::setCorrelation(float c)       { correlation_ = juce::jlimit(-1.0f, 1.0f, c); repaint(); }
void MeterPanel::setTruePeakL(float dbTP)      { truePeakL_ = dbTP; repaint(); }
void MeterPanel::setTruePeakR(float dbTP)      { truePeakR_ = dbTP; repaint(); }
void MeterPanel::setClipping(bool clip)        { clipping_ = clip; repaint(); }
void MeterPanel::setMomentaryLUFS(float lufs)  { momentaryLUFS_ = lufs; repaint(); }
void MeterPanel::setShortTermLUFS(float lufs)  { shortTermLUFS_ = lufs; repaint(); }
void MeterPanel::setIntegratedLUFS(float lufs) { integratedLUFS_ = lufs; repaint(); }
void MeterPanel::setLoudnessRange(float lra)   { loudnessRange_ = lra; repaint(); }
void MeterPanel::setOutputLevelL(float)        {}
void MeterPanel::setOutputLevelR(float)        {}
void MeterPanel::setRmsLevel(float)            {}

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
