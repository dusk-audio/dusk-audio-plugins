#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <algorithm>
#include <random>

namespace MultiSynthDSP
{

enum class ArpMode
{
    Up,
    Down,
    UpDown,
    DownUp,
    Random,
    Order,   // Played order
    Chord    // Re-triggers all held notes
};

enum class ArpVelocityMode
{
    AsPlayed,
    Fixed,
    AccentPattern
};

enum class ArpAccentPattern
{
    Downbeat,      // Strong on 1
    EveryOther,    // Strong on 1, 3, 5...
    RampUp,        // Increasing velocity
    RampDown       // Decreasing velocity
};

// Rate division relative to tempo
enum class ArpRateDivision
{
    Whole,        // 1/1
    Half,         // 1/2
    Quarter,      // 1/4
    Eighth,       // 1/8
    Sixteenth,    // 1/16
    ThirtySecond, // 1/32
    DottedHalf,
    DottedQuarter,
    DottedEighth,
    DottedSixteenth,
    TripletHalf,
    TripletQuarter,
    TripletEighth,
    TripletSixteenth,
    NumDivisions
};

inline double getBeatsPerStep(ArpRateDivision div)
{
    switch (div)
    {
        case ArpRateDivision::Whole:            return 4.0;
        case ArpRateDivision::Half:             return 2.0;
        case ArpRateDivision::Quarter:          return 1.0;
        case ArpRateDivision::Eighth:           return 0.5;
        case ArpRateDivision::Sixteenth:        return 0.25;
        case ArpRateDivision::ThirtySecond:     return 0.125;
        case ArpRateDivision::DottedHalf:       return 3.0;
        case ArpRateDivision::DottedQuarter:    return 1.5;
        case ArpRateDivision::DottedEighth:     return 0.75;
        case ArpRateDivision::DottedSixteenth:  return 0.375;
        case ArpRateDivision::TripletHalf:      return 4.0 / 3.0;
        case ArpRateDivision::TripletQuarter:   return 2.0 / 3.0;
        case ArpRateDivision::TripletEighth:    return 1.0 / 3.0;
        case ArpRateDivision::TripletSixteenth: return 1.0 / 6.0;
        default: return 1.0;
    }
}

class Arpeggiator
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void setEnabled(bool on) { enabled = on; if (!on) reset(); }
    bool isEnabled() const { return enabled; }

    void setMode(ArpMode m) { mode = m; }
    void setOctaveRange(int range) { octaveRange = juce::jlimit(1, 4, range); }
    void setRate(ArpRateDivision r) { rateDivision = r; }
    void setGate(float g) { gateLength = juce::jlimit(0.01f, 1.0f, g); }
    void setSwing(float s) { swing = juce::jlimit(0.0f, 1.0f, s); }
    void setLatch(bool on) { latch = on; }
    void clearLatch() { if (latch) { heldNotes.clear(); playedOrder.clear(); } }

    void setVelocityMode(ArpVelocityMode m) { velMode = m; }
    void setFixedVelocity(int vel) { fixedVel = juce::jlimit(1, 127, vel); }
    void setAccentPattern(ArpAccentPattern p) { accentPattern = p; }

    int getCurrentStep() const { return currentStep; }
    int getTotalSteps() const { return static_cast<int>(buildPattern().size()); }

    // Call this for each incoming MIDI note on
    void noteOn(int noteNumber, int velocity)
    {
        if (!enabled) return;

        NoteInfo note { noteNumber, velocity };

        // Check if already held
        auto it = std::find_if(heldNotes.begin(), heldNotes.end(),
            [noteNumber](const NoteInfo& n) { return n.note == noteNumber; });

        if (it == heldNotes.end())
        {
            heldNotes.push_back(note);
            playedOrder.push_back(note);
        }
    }

    // Call this for each incoming MIDI note off
    void noteOff(int noteNumber)
    {
        if (!enabled) return;

        if (!latch)
        {
            heldNotes.erase(
                std::remove_if(heldNotes.begin(), heldNotes.end(),
                    [noteNumber](const NoteInfo& n) { return n.note == noteNumber; }),
                heldNotes.end());
            playedOrder.erase(
                std::remove_if(playedOrder.begin(), playedOrder.end(),
                    [noteNumber](const NoteInfo& n) { return n.note == noteNumber; }),
                playedOrder.end());
        }
    }

    // Process a block: generates MIDI output replacing input notes
    // Returns note events as (sampleOffset, noteNumber, velocity, isNoteOn)
    struct ArpEvent
    {
        int sampleOffset;
        int noteNumber;
        int velocity;
        bool isNoteOn;
    };

    std::vector<ArpEvent> processBlock(int numSamples, double bpm, bool transportPlaying)
    {
        std::vector<ArpEvent> events;

        if (!enabled || heldNotes.empty())
        {
            // Send note-off for any currently playing note
            if (lastPlayedNote >= 0)
            {
                events.push_back({ 0, lastPlayedNote, 0, false });
                lastPlayedNote = -1;
            }
            return events;
        }

        double effectiveBpm = (bpm > 0.0 && transportPlaying) ? bpm : 120.0;
        double beatsPerStep = getBeatsPerStep(rateDivision);
        double samplesPerBeat = sr * 60.0 / effectiveBpm;
        double samplesPerStep = samplesPerBeat * beatsPerStep;

        auto pattern = buildPattern();
        if (pattern.empty())
            return events;

        for (int i = 0; i < numSamples; ++i)
        {
            sampleCounter++;

            // Apply swing to even steps
            double effectiveSamplesPerStep = samplesPerStep;
            if (currentStep % 2 == 1 && swing > 0.0f)
                effectiveSamplesPerStep *= (1.0 + static_cast<double>(swing) * 0.5);

            double gateSamples = effectiveSamplesPerStep * static_cast<double>(gateLength);

            // Note on at step start
            if (sampleCounter == 1)
            {
                int patIdx = currentStep % static_cast<int>(pattern.size());
                auto& note = pattern[static_cast<size_t>(patIdx)];

                // Send note off for previous
                if (lastPlayedNote >= 0 && lastPlayedNote != note.note)
                    events.push_back({ i, lastPlayedNote, 0, false });

                int vel = getVelocity(note.velocity, currentStep);
                events.push_back({ i, note.note, vel, true });
                lastPlayedNote = note.note;
            }

            // Note off at gate end
            if (static_cast<double>(sampleCounter) >= gateSamples && lastPlayedNote >= 0)
            {
                events.push_back({ i, lastPlayedNote, 0, false });
                lastPlayedNote = -1;
            }

            // Advance step
            if (static_cast<double>(sampleCounter) >= effectiveSamplesPerStep)
            {
                sampleCounter = 0;
                currentStep++;
                if (currentStep >= static_cast<int>(pattern.size()))
                    currentStep = 0;
            }
        }

        return events;
    }

    void reset()
    {
        currentStep = 0;
        sampleCounter = 0;
        lastPlayedNote = -1;
        goingUp = true;
        if (!latch)
        {
            heldNotes.clear();
            playedOrder.clear();
        }
    }

private:
    struct NoteInfo
    {
        int note = 60;
        int velocity = 100;
    };

    std::vector<NoteInfo> buildPattern() const
    {
        if (heldNotes.empty())
            return {};

        // Sort notes for Up/Down patterns
        auto sorted = heldNotes;
        std::sort(sorted.begin(), sorted.end(),
            [](const NoteInfo& a, const NoteInfo& b) { return a.note < b.note; });

        std::vector<NoteInfo> pattern;

        // Expand across octaves
        for (int oct = 0; oct < octaveRange; ++oct)
        {
            for (auto& n : sorted)
                pattern.push_back({ n.note + oct * 12, n.velocity });
        }

        switch (mode)
        {
            case ArpMode::Up:
                // Already sorted ascending
                break;

            case ArpMode::Down:
                std::reverse(pattern.begin(), pattern.end());
                break;

            case ArpMode::UpDown:
            {
                if (pattern.size() > 1)
                {
                    auto down = pattern;
                    std::reverse(down.begin(), down.end());
                    // Skip first and last to avoid doubles
                    for (size_t i = 1; i < down.size() - 1; ++i)
                        pattern.push_back(down[i]);
                }
                break;
            }

            case ArpMode::DownUp:
            {
                std::reverse(pattern.begin(), pattern.end());
                if (pattern.size() > 1)
                {
                    auto up = heldNotes;
                    std::sort(up.begin(), up.end(),
                        [](const NoteInfo& a, const NoteInfo& b) { return a.note < b.note; });
                    // Expand octaves for up portion
                    std::vector<NoteInfo> upExpanded;
                    for (int oct = 0; oct < octaveRange; ++oct)
                        for (auto& n : up)
                            upExpanded.push_back({ n.note + oct * 12, n.velocity });
                    for (size_t i = 1; i < upExpanded.size() - 1; ++i)
                        pattern.push_back(upExpanded[i]);
                }
                break;
            }

            case ArpMode::Random:
            {
                // Shuffle pattern
                auto rng = std::default_random_engine(
                    static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
                std::shuffle(pattern.begin(), pattern.end(), rng);
                break;
            }

            case ArpMode::Order:
            {
                pattern.clear();
                for (int oct = 0; oct < octaveRange; ++oct)
                    for (auto& n : playedOrder)
                        pattern.push_back({ n.note + oct * 12, n.velocity });
                break;
            }

            case ArpMode::Chord:
            {
                // For chord mode, each step plays all notes - we return them all
                // The processor handles triggering all at once
                break;
            }
        }

        return pattern;
    }

    int getVelocity(int originalVel, int step) const
    {
        switch (velMode)
        {
            case ArpVelocityMode::AsPlayed:
                return originalVel;

            case ArpVelocityMode::Fixed:
                return fixedVel;

            case ArpVelocityMode::AccentPattern:
            {
                float accent = 0.7f;
                switch (accentPattern)
                {
                    case ArpAccentPattern::Downbeat:
                        accent = (step % 4 == 0) ? 1.0f : 0.6f;
                        break;
                    case ArpAccentPattern::EveryOther:
                        accent = (step % 2 == 0) ? 1.0f : 0.6f;
                        break;
                    case ArpAccentPattern::RampUp:
                        accent = 0.4f + 0.6f * (static_cast<float>(step % 8) / 7.0f);
                        break;
                    case ArpAccentPattern::RampDown:
                        accent = 1.0f - 0.6f * (static_cast<float>(step % 8) / 7.0f);
                        break;
                }
                return juce::jlimit(1, 127, static_cast<int>(127.0f * accent));
            }
        }
        return originalVel;
    }

    double sr = 44100.0;
    bool enabled = false;
    ArpMode mode = ArpMode::Up;
    int octaveRange = 1;
    ArpRateDivision rateDivision = ArpRateDivision::Eighth;
    float gateLength = 0.5f;
    float swing = 0.0f;
    bool latch = false;

    ArpVelocityMode velMode = ArpVelocityMode::AsPlayed;
    int fixedVel = 100;
    ArpAccentPattern accentPattern = ArpAccentPattern::Downbeat;

    std::vector<NoteInfo> heldNotes;
    std::vector<NoteInfo> playedOrder;

    int currentStep = 0;
    long long sampleCounter = 0;
    int lastPlayedNote = -1;
    bool goingUp = true;
};

} // namespace MultiSynthDSP
