#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include <algorithm>

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

    void setMode(ArpMode m) { if (m != mode) patternDirty = true; mode = m; }
    void setOctaveRange(int range) { int r = juce::jlimit(1, 4, range); if (r != octaveRange) patternDirty = true; octaveRange = r; }
    void setRate(ArpRateDivision r) { rateDivision = r; }
    void setGate(float g) { gateLength = juce::jlimit(0.01f, 1.0f, g); }
    void setSwing(float s) { swing = juce::jlimit(0.0f, 1.0f, s); }
    void setLatch(bool on) { latch = on; }
    void clearLatch() { if (latch) { heldNotes.clear(); playedOrder.clear(); patternDirty = true; } }

    void setVelocityMode(ArpVelocityMode m) { velMode = m; }
    void setFixedVelocity(int vel) { fixedVel = juce::jlimit(1, 127, vel); }
    void setAccentPattern(ArpAccentPattern p) { accentPattern = p; }

    // Step mute pattern (16 steps, true = active, false = muted/rest)
    void setStepMute(int step, bool active)
    {
        if (step >= 0 && step < 16)
            stepPattern[static_cast<size_t>(step)] = active;
    }

    bool isStepActive(int step) const
    {
        return stepPattern[static_cast<size_t>(step % 16)];
    }

    int getCurrentStep() const { return currentStep; }
    int getTotalSteps() const { return static_cast<int>(getPattern().size()); }

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
            patternDirty = true;
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
            patternDirty = true;
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

        const auto& pattern = getPattern();
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

                // Check step mute pattern
                bool stepActive = stepPattern[static_cast<size_t>(currentStep % 16)];

                if (stepActive)
                {
                    // Send note off for previous
                    if (lastPlayedNote >= 0 && lastPlayedNote != note.note)
                        events.push_back({ i, lastPlayedNote, 0, false });

                    int vel = getVelocity(note.velocity, currentStep);
                    events.push_back({ i, note.note, vel, true });
                    lastPlayedNote = note.note;
                }
                else
                {
                    // Muted step: send note-off only
                    if (lastPlayedNote >= 0)
                    {
                        events.push_back({ i, lastPlayedNote, 0, false });
                        lastPlayedNote = -1;
                    }
                }
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
                {
                    currentStep = 0;
                    // Random: a completed cycle earns a fresh shuffle on the NEXT
                    // block's getPattern(); the order stays fixed within this cycle
                    // (setting the flag here doesn't invalidate `pattern` mid-block).
                    if (mode == ArpMode::Random)
                        patternDirty = true;
                }
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
        patternDirty = true;
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

    // Cached pattern access. buildPattern() used to allocate a fresh vector on
    // EVERY processBlock() and getTotalSteps() call (audio thread) and, in Random
    // mode, constructed+seeded a std::default_random_engine from the wall clock and
    // re-shuffled every block. Now the pattern is cached and rebuilt only when the
    // held notes / mode / octave range change (patternDirty), reusing the buffer's
    // capacity; Random shuffles with a pre-seeded member juce::Random and re-shuffles
    // only when a cycle completes (see processBlock), so it stays stable per cycle.
    const std::vector<NoteInfo>& getPattern() const
    {
        if (patternDirty)
        {
            rebuildPattern();
            patternDirty = false;
        }
        return cachedPattern;
    }

    void rebuildPattern() const
    {
        cachedPattern.clear(); // keeps capacity — no per-block heap churn

        if (heldNotes.empty())
            return;

        // Sort notes for Up/Down patterns
        auto sorted = heldNotes;
        std::sort(sorted.begin(), sorted.end(),
            [](const NoteInfo& a, const NoteInfo& b) { return a.note < b.note; });

        // Expand across octaves
        for (int oct = 0; oct < octaveRange; ++oct)
            for (auto& n : sorted)
                cachedPattern.push_back({ n.note + oct * 12, n.velocity });

        switch (mode)
        {
            case ArpMode::Up:
                // Already sorted ascending
                break;

            case ArpMode::Down:
                std::reverse(cachedPattern.begin(), cachedPattern.end());
                break;

            case ArpMode::UpDown:
            {
                if (cachedPattern.size() > 1)
                {
                    auto down = cachedPattern;
                    std::reverse(down.begin(), down.end());
                    // Skip first and last to avoid doubles
                    for (size_t i = 1; i < down.size() - 1; ++i)
                        cachedPattern.push_back(down[i]);
                }
                break;
            }

            case ArpMode::DownUp:
            {
                std::reverse(cachedPattern.begin(), cachedPattern.end());
                if (cachedPattern.size() > 1)
                {
                    // Up portion = the same ascending, octave-expanded set.
                    std::vector<NoteInfo> upExpanded;
                    for (int oct = 0; oct < octaveRange; ++oct)
                        for (auto& n : sorted)
                            upExpanded.push_back({ n.note + oct * 12, n.velocity });
                    for (size_t i = 1; i < upExpanded.size() - 1; ++i)
                        cachedPattern.push_back(upExpanded[i]);
                }
                break;
            }

            case ArpMode::Random:
            {
                // Fisher-Yates with the pre-seeded member RNG (no per-call engine
                // construction, no wall-clock seeding on the audio thread).
                for (int i = static_cast<int>(cachedPattern.size()) - 1; i > 0; --i)
                {
                    const int j = rng.nextInt(i + 1);
                    std::swap(cachedPattern[static_cast<size_t>(i)],
                              cachedPattern[static_cast<size_t>(j)]);
                }
                break;
            }

            case ArpMode::Order:
            {
                cachedPattern.clear();
                for (int oct = 0; oct < octaveRange; ++oct)
                    for (auto& n : playedOrder)
                        cachedPattern.push_back({ n.note + oct * 12, n.velocity });
                break;
            }

            case ArpMode::Chord:
            {
                // For chord mode, each step plays all notes - we return them all
                // The processor handles triggering all at once
                break;
            }
        }
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

    std::array<bool, 16> stepPattern {{ true, true, true, true, true, true, true, true,
                                        true, true, true, true, true, true, true, true }};

    std::vector<NoteInfo> heldNotes;
    std::vector<NoteInfo> playedOrder;

    // Cached arp pattern — rebuilt only when held notes / mode / octave change, so
    // processBlock() and getTotalSteps() never allocate per call. mutable so the
    // const accessors (getPattern/getTotalSteps) can lazily rebuild.
    mutable std::vector<NoteInfo> cachedPattern;
    mutable bool patternDirty = true;
    mutable juce::Random rng; // pre-seeded once (default ctor); Random-mode shuffle

    int currentStep = 0;
    long long sampleCounter = 0;
    int lastPlayedNote = -1;
    bool goingUp = true;
};

} // namespace MultiSynthDSP
