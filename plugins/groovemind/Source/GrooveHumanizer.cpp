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
    // These values are learned from the Groove MIDI Dataset (Phase 3 ML training)
    // Statistics extracted from 446,312 note events across professional drummers
    //
    // Key insight: Real drummers play BEHIND the beat (negative mean = early relative
    // to quantized position, but the median is more "behind" the grid)
    //
    // From timing_stats.json:
    //   kick:   mean=-5.78ms, std=26.80ms, median=-7.29ms, vel_mean=58
    //   snare:  mean=-5.64ms, std=28.14ms, median=-8.33ms, vel_mean=67
    //   hihat:  mean=-10.79ms, std=26.44ms, median=-12.50ms, vel_mean=58
    //   tom:    mean=-6.76ms, std=27.68ms, median=-9.38ms, vel_mean=88
    //   cymbal: mean=-4.54ms, std=25.72ms, median=-6.25ms, vel_mean=74
    //   other:  mean=-6.77ms, std=20.79ms, median=-6.25ms, vel_mean=90

    // Kick drum - slightly behind for groove
    // Notes 35, 36 = Bass Drum
    instrumentTimings[35] = { -5.8f, 26.8f, 0.92f };  // vel 58/63 normalized
    instrumentTimings[36] = { -5.8f, 26.8f, 0.92f };

    // Snare - behind the beat, moderate variation
    // Notes 38, 40 = Snare
    instrumentTimings[38] = { -5.6f, 28.1f, 1.0f };
    instrumentTimings[40] = { -5.6f, 28.1f, 1.0f };

    // Side stick / rim - similar to snare
    instrumentTimings[37] = { -5.6f, 28.1f, 0.85f };

    // Hi-hats - most behind the beat (creates laid-back feel)
    // Notes 42, 44, 46 = Closed, Pedal, Open Hi-hat
    instrumentTimings[42] = { -10.8f, 26.4f, 0.92f };
    instrumentTimings[44] = { -10.8f, 26.4f, 0.85f };
    instrumentTimings[46] = { -10.8f, 26.4f, 0.92f };

    // Ride - slightly behind like cymbals
    // Notes 51, 53, 59 = Ride, Ride Bell, Ride 2
    instrumentTimings[51] = { -4.5f, 25.7f, 0.95f };
    instrumentTimings[53] = { -4.5f, 25.7f, 0.95f };
    instrumentTimings[59] = { -4.5f, 25.7f, 0.95f };

    // Toms - behind the beat, higher velocity (fills)
    // Notes 41, 43, 45, 47, 48, 50 = Low to High Toms
    instrumentTimings[41] = { -6.8f, 27.7f, 1.1f };  // vel 88/80 = 1.1x
    instrumentTimings[43] = { -6.8f, 27.7f, 1.1f };
    instrumentTimings[45] = { -6.8f, 27.7f, 1.1f };
    instrumentTimings[47] = { -6.8f, 27.7f, 1.1f };
    instrumentTimings[48] = { -6.8f, 27.7f, 1.1f };
    instrumentTimings[50] = { -6.8f, 27.7f, 1.1f };

    // Crashes - slightly behind, higher velocity for accents
    // Notes 49, 57, 55, 52 = Crash 1, Crash 2, Splash, China
    instrumentTimings[49] = { -4.5f, 25.7f, 1.05f };
    instrumentTimings[57] = { -4.5f, 25.7f, 1.05f };
    instrumentTimings[55] = { -4.5f, 25.7f, 1.0f };
    instrumentTimings[52] = { -4.5f, 25.7f, 1.0f };
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
bool GrooveHumanizer::loadModel(const juce::File& modelFile)
{
    if (!modelFile.existsAsFile())
    {
        DBG("GrooveHumanizer: Model file not found: " + modelFile.getFullPathName());
        mlModelLoaded = false;
        return false;
    }

    if (humanizerModel.loadFromJSON(modelFile))
    {
        mlModelLoaded = true;
        DBG("GrooveHumanizer: ML model loaded successfully");
        return true;
    }

    DBG("GrooveHumanizer: Failed to load ML model");
    mlModelLoaded = false;
    return false;
}

//==============================================================================
bool GrooveHumanizer::loadTimingStats(const juce::File& statsFile)
{
    if (!statsFile.existsAsFile())
    {
        DBG("GrooveHumanizer: Timing stats file not found");
        return false;
    }

    if (timingStats.loadFromJSON(statsFile))
    {
        DBG("GrooveHumanizer: Timing statistics loaded");
        return true;
    }

    return false;
}

//==============================================================================
int GrooveHumanizer::noteToCategory(int midiNote) const
{
    // Map General MIDI drum notes to categories
    // Category 0: kick, 1: snare, 2: hihat, 3: tom, 4: cymbal, 5: other

    switch (midiNote)
    {
        case 35: case 36:  // Bass drum
            return 0;

        case 37: case 38: case 39: case 40:  // Snare, sidestick, clap
            return 1;

        case 42: case 44: case 46:  // Hi-hat
            return 2;

        case 41: case 43: case 45: case 47: case 48: case 50:  // Toms
            return 3;

        case 49: case 51: case 52: case 53: case 55: case 57: case 59:  // Cymbals
            return 4;

        default:
            return 5;  // Other
    }
}

//==============================================================================
float GrooveHumanizer::calculateMLTimingOffset(int noteNumber, double beatPosition,
                                                 int velocity, int prevNote, int nextNote) const
{
    if (!mlModelLoaded || !useML)
        return 0.0f;

    int category = noteToCategory(noteNumber);
    int prevCategory = prevNote >= 0 ? noteToCategory(prevNote) : -1;
    int nextCategory = nextNote >= 0 ? noteToCategory(nextNote) : -1;

    float beatPos = static_cast<float>(std::fmod(beatPosition, 4.0) / 4.0);  // Normalize to 0-1
    float vel = velocity / 127.0f;

    // Use the ML model for prediction (const_cast needed for mutable model state)
    float offsetMs = const_cast<MLInference::HumanizerModel&>(humanizerModel).predict(
        category, beatPos, vel, prevCategory, nextCategory);

    return offsetMs * grooveAmount;
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

    // Collect note events for context-aware ML inference
    std::vector<std::tuple<int, int, int>> noteEvents;  // samplePos, noteNumber, velocity
    for (const auto metadata : midiBuffer)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            noteEvents.emplace_back(metadata.samplePosition, msg.getNoteNumber(), msg.getVelocity());
    }

    int eventIndex = 0;

    for (const auto metadata : midiBuffer)
    {
        auto msg = metadata.getMessage();
        int samplePos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            // Calculate beat position from sample position
            double beatPosition = (samplePos / sampleRate) * (bpm / 60.0);
            beatPosition = applySwing(beatPosition);

            float offsetMs;

            // Use ML model if available, otherwise fall back to statistical method
            if (mlModelLoaded && useML)
            {
                // Get context (previous and next notes)
                int prevNote = eventIndex > 0 ? std::get<1>(noteEvents[eventIndex - 1]) : -1;
                int nextNote = eventIndex < static_cast<int>(noteEvents.size()) - 1
                               ? std::get<1>(noteEvents[eventIndex + 1]) : -1;

                offsetMs = calculateMLTimingOffset(msg.getNoteNumber(), beatPosition,
                                                    msg.getVelocity(), prevNote, nextNote);
            }
            else
            {
                offsetMs = calculateTimingOffset(msg.getNoteNumber(), beatPosition);
            }

            int offsetSamples = static_cast<int>(offsetMs / msPerSample);

            // Adjust velocity
            int newVelocity = adjustVelocity(msg.getVelocity(), msg.getNoteNumber(), beatPosition);

            // Create adjusted message
            int newSamplePos = juce::jmax(0, samplePos + offsetSamples);
            auto adjustedMsg = juce::MidiMessage::noteOn(msg.getChannel(),
                                                          msg.getNoteNumber(),
                                                          (juce::uint8)newVelocity);
            processedBuffer.addEvent(adjustedMsg, newSamplePos);

            eventIndex++;
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
