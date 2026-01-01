/*
  ==============================================================================

    GrooveHumanizer.cpp
    Humanization implementation

  ==============================================================================
*/

#include "GrooveHumanizer.h"

//==============================================================================
GrooveHumanizer::GrooveHumanizer()
{
    initializeDefaultTimings();
}

//==============================================================================
void GrooveHumanizer::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

//==============================================================================
void GrooveHumanizer::initializeDefaultTimings()
{
    // These values are inspired by analysis of the Groove MIDI Dataset
    // Mean timing offset: -7.13ms (slightly behind beat)
    // Std deviation: 28.75ms

    // Kick drum - slightly ahead for drive
    instrumentTimings[35] = { -2.0f, 8.0f, 1.0f };
    instrumentTimings[36] = { -2.0f, 8.0f, 1.0f };

    // Snare - on the beat or slightly behind
    instrumentTimings[38] = { 2.0f, 12.0f, 1.0f };
    instrumentTimings[40] = { 2.0f, 12.0f, 1.0f };

    // Side stick / rim - often slightly ahead
    instrumentTimings[37] = { -3.0f, 8.0f, 0.9f };

    // Hi-hats - tight timing
    instrumentTimings[42] = { 0.0f, 6.0f, 0.95f };
    instrumentTimings[44] = { 0.0f, 5.0f, 0.9f };
    instrumentTimings[46] = { 1.0f, 8.0f, 1.0f };

    // Ride - slightly behind for jazz feel
    instrumentTimings[51] = { 5.0f, 15.0f, 0.9f };
    instrumentTimings[53] = { 3.0f, 12.0f, 0.95f };

    // Toms - follow the beat
    instrumentTimings[41] = { 0.0f, 10.0f, 1.0f };
    instrumentTimings[43] = { 0.0f, 10.0f, 1.0f };
    instrumentTimings[45] = { 0.0f, 10.0f, 1.0f };
    instrumentTimings[47] = { 0.0f, 10.0f, 1.0f };
    instrumentTimings[48] = { 0.0f, 10.0f, 1.0f };
    instrumentTimings[50] = { 0.0f, 10.0f, 1.0f };

    // Crashes - slightly ahead for impact
    instrumentTimings[49] = { -5.0f, 8.0f, 1.05f };
    instrumentTimings[57] = { -5.0f, 8.0f, 1.05f };
}

//==============================================================================
void GrooveHumanizer::setGrooveAmount(float amount)
{
    grooveAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void GrooveHumanizer::setSwing(float newSwing)
{
    swing = juce::jlimit(0.0f, 1.0f, newSwing);
}

void GrooveHumanizer::setTimingVariation(float ms)
{
    timingVariationMs = juce::jlimit(0.0f, 50.0f, ms);
}

void GrooveHumanizer::setVelocityVariation(float amount)
{
    velocityVariation = juce::jlimit(0.0f, 1.0f, amount);
}

//==============================================================================
void GrooveHumanizer::setGroovePreset(int preset)
{
    switch (preset)
    {
        case 0:  // Tight
            timingVariationMs = 5.0f;
            velocityVariation = 0.1f;
            pushPull = 0.0f;
            break;
        case 1:  // Relaxed
            timingVariationMs = 15.0f;
            velocityVariation = 0.2f;
            pushPull = 0.2f;  // Slightly behind
            break;
        case 2:  // Jazzy
            timingVariationMs = 25.0f;
            velocityVariation = 0.3f;
            pushPull = 0.4f;  // More behind
            swing = 0.5f;
            break;
        case 3:  // Behind the beat
            timingVariationMs = 20.0f;
            velocityVariation = 0.15f;
            pushPull = 0.6f;
            break;
        default:
            break;
    }
}

//==============================================================================
bool GrooveHumanizer::loadModel(const juce::File& /*modelFile*/)
{
    // TODO: Load RTNeural model for ML-based humanization
    // This will be implemented in Phase 4
    mlModelLoaded = false;
    return false;
}

//==============================================================================
double GrooveHumanizer::applySwing(double beatPosition) const
{
    if (swing < 0.01f)
        return beatPosition;

    // Get position within beat (0-1)
    double posInBeat = std::fmod(beatPosition, 1.0);

    // Swing affects off-beats (8th notes between beats)
    // 0.5 in the beat is the off-beat
    if (posInBeat > 0.25 && posInBeat < 0.75)
    {
        // Shift the off-beat later
        double swingAmount = swing * 0.33;  // Max swing: triplet feel
        double adjustment = swingAmount * (0.5 - std::abs(posInBeat - 0.5)) * 2.0;
        return beatPosition + adjustment;
    }

    return beatPosition;
}

//==============================================================================
float GrooveHumanizer::calculateTimingOffset(int noteNumber, double beatPosition) const
{
    float offset = 0.0f;

    // Get instrument-specific timing
    auto it = instrumentTimings.find(noteNumber);
    if (it != instrumentTimings.end())
    {
        offset = it->second.offsetMs;
        offset += (random.nextFloat() * 2.0f - 1.0f) * it->second.variationMs;
    }
    else
    {
        // Default variation for unknown instruments
        offset = (random.nextFloat() * 2.0f - 1.0f) * timingVariationMs;
    }

    // Apply push/pull feel
    offset += pushPull * 10.0f;

    // Beat-dependent adjustments
    double posInBeat = std::fmod(beatPosition, 1.0);

    // Downbeats tend to be more on-time
    if (posInBeat < 0.1 || posInBeat > 0.9)
    {
        offset *= 0.5f;
    }

    // Scale by groove amount
    offset *= grooveAmount;

    return offset;
}

//==============================================================================
int GrooveHumanizer::adjustVelocity(int originalVelocity, int noteNumber,
                                     double beatPosition) const
{
    float velocity = static_cast<float>(originalVelocity);

    // Apply instrument-specific scaling
    auto it = instrumentTimings.find(noteNumber);
    if (it != instrumentTimings.end())
    {
        velocity *= it->second.velocityScale;
    }

    // Add random variation
    float variation = (random.nextFloat() * 2.0f - 1.0f) * velocityVariation * 30.0f;
    velocity += variation * grooveAmount;

    // Beat-based velocity accents
    double posInBeat = std::fmod(beatPosition, 1.0);
    double posInBar = std::fmod(beatPosition, 4.0);

    // Accent on beats 1 and 3 (for 4/4)
    if (posInBar < 0.1 || (posInBar > 1.9 && posInBar < 2.1))
    {
        velocity *= 1.05f;
    }

    // Slightly softer on off-beats
    if (posInBeat > 0.4 && posInBeat < 0.6)
    {
        velocity *= 0.95f;
    }

    return juce::jlimit(1, 127, static_cast<int>(velocity));
}

//==============================================================================
void GrooveHumanizer::process(juce::MidiBuffer& midiBuffer, double bpm)
{
    if (grooveAmount < 0.01f || bpm <= 0)
        return;

    juce::MidiBuffer processedBuffer;
    double msPerSample = 1000.0 / sampleRate;
    double beatsPerMs = bpm / 60000.0;

    for (const auto metadata : midiBuffer)
    {
        auto msg = metadata.getMessage();
        int samplePos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            // Calculate beat position from sample position
            double beatPosition = (samplePos / sampleRate) * (bpm / 60.0);
            beatPosition = applySwing(beatPosition);

            // Calculate timing offset
            float offsetMs = calculateTimingOffset(msg.getNoteNumber(), beatPosition);
            int offsetSamples = static_cast<int>(offsetMs / msPerSample);

            // Adjust velocity
            int newVelocity = adjustVelocity(msg.getVelocity(), msg.getNoteNumber(), beatPosition);

            // Create adjusted message
            int newSamplePos = juce::jmax(0, samplePos + offsetSamples);
            auto adjustedMsg = juce::MidiMessage::noteOn(msg.getChannel(),
                                                          msg.getNoteNumber(),
                                                          (juce::uint8)newVelocity);
            processedBuffer.addEvent(adjustedMsg, newSamplePos);
        }
        else if (msg.isNoteOff())
        {
            // Apply same timing offset to note-offs
            double beatPosition = (samplePos / sampleRate) * (bpm / 60.0);
            float offsetMs = calculateTimingOffset(msg.getNoteNumber(), beatPosition);
            int offsetSamples = static_cast<int>(offsetMs / msPerSample);
            int newSamplePos = juce::jmax(0, samplePos + offsetSamples);

            processedBuffer.addEvent(msg, newSamplePos);
        }
        else
        {
            // Pass through other messages unchanged
            processedBuffer.addEvent(msg, samplePos);
        }
    }

    // Replace original buffer with processed
    midiBuffer.swapWith(processedBuffer);
}
