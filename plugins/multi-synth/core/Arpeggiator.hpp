// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Arpeggiator.hpp — allocation-free tempo-synced arpeggiator (mandatory fix #6).
//
// The JUCE arpeggiator allocated a std::vector of events and rebuilt/sorted (and
// std::shuffle'd with a chrono-seeded engine) its pattern every processBlock.
// This rewrite is fully real-time safe:
//   * held notes / played order / pattern live in fixed std::arrays;
//   * the pattern is rebuilt only when the held-note set or mode/octave changes;
//   * Random uses a member xorshift PRNG to pick a step index (no shuffle, no
//     std::chrono clock call);
//   * stepping is per-sample via advanceSample(): the engine renders sample by
//     sample anyway, so each generated note-on/off is emitted at the exact host
//     sample it falls on (sample-accurate) with no event buffer.
//
// Behaviour (swing on odd steps, gate length, 16-step mute pattern, velocity
// modes, octave expansion, Up/Down/UpDown/DownUp/Order/Chord) is preserved.

#pragma once

#include "SynthCommon.hpp"
#include <algorithm>
#include <array>

namespace msynth
{

enum class ArpMode { Up = 0, Down, UpDown, DownUp, Random, Order, Chord };
enum class ArpVelocityMode { AsPlayed = 0, Fixed, AccentPattern };
enum class ArpAccentPattern { Downbeat = 0, EveryOther, RampUp, RampDown };

class Arpeggiator
{
public:
    static constexpr int kMaxHeld = 16;
    static constexpr int kMaxPattern = kMaxHeld * 4 * 2; // octaves x up-down

    struct StepEvent
    {
        bool noteOffValid = false;
        int  offNote = -1;
        bool noteOnValid = false;
        int  onNote = -1;
        int  onVel = 0;
    };

    void prepare(double sampleRate) noexcept { sr = sampleRate; rng.seed(0xA17E5EEDu); reset(); }

    void setEnabled(bool on) noexcept
    {
        if (on == enabled) return;
        enabled = on;
        // Defensive: preserve any pending note across the disable-reset so that IF
        // advanceSample ever runs while disabled it still emits the note-off (the
        // engine also releases the voice on the arp-off transition — primary path).
        if (!on) { const int pending = lastPlayedNote; reset(); lastPlayedNote = pending; }
    }
    bool isEnabled() const noexcept { return enabled; }

    void setMode(ArpMode m) noexcept { if (m != mode) { mode = m; dirty = true; } }
    void setOctaveRange(int r) noexcept { r = clampi(r, 1, 4); if (r != octaveRange) { octaveRange = r; dirty = true; } }
    void setRate(ArpRateDivision r) noexcept { rateDivision = r; }
    void setGate(float g) noexcept { gateLength = clampf(g, 0.01f, 1.0f); }
    void setSwing(float s) noexcept { swing = clampf(s, 0.0f, 1.0f); }
    void setLatch(bool on) noexcept { latch = on; }
    void clearLatch() noexcept { if (latch) { heldCount = 0; orderCount = 0; dirty = true; } }

    void setVelocityMode(ArpVelocityMode m) noexcept { velMode = m; }
    void setFixedVelocity(int v) noexcept { fixedVel = clampi(v, 1, 127); }
    void setAccentPattern(ArpAccentPattern p) noexcept { accentPattern = p; }

    // true = step plays; the JUCE original called this setStepMute with inverted-sounding naming
    void setStepActive(int step, bool active) noexcept
    {
        if (step >= 0 && step < 16) stepPattern[(size_t)step] = active;
    }
    bool isStepActive(int step) const noexcept { return stepPattern[(size_t)(step % 16)]; }

    int getCurrentStep() const noexcept { return currentStep; }
    int getTotalSteps() const noexcept { return patternSize; }

    void noteOn(int noteNumber, int velocity) noexcept
    {
        if (!enabled) return;
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t)i].note == noteNumber) return; // already held
        if (heldCount < kMaxHeld)
        {
            held[(size_t)heldCount++]  = { noteNumber, velocity };
            if (orderCount < kMaxHeld)
                order[(size_t)orderCount++] = { noteNumber, velocity };
            dirty = true;
        }
    }

    void noteOff(int noteNumber) noexcept
    {
        if (!enabled || latch) return;
        removeNote(held, heldCount, noteNumber);
        removeNote(order, orderCount, noteNumber);
        dirty = true;
    }

    // Advance the arp by one host sample; returns note events for this sample.
    //
    // hostLocked = the DAW transport is playing AND a valid song position is
    // available (see MultiSynthDSP::setSongPosition). When locked, the step clock
    // is derived STATELESSLY from songBeat (song position in beats) so steps land
    // on the absolute host beat grid and re-sync on loop-wrap. When not locked
    // (stopped / no song position) the existing free-run counter path is used,
    // byte-for-byte unchanged.
    StepEvent advanceSample(double bpm, bool transportPlaying,
                            double songBeat, bool hostLocked) noexcept
    {
        StepEvent ev;

        if (!enabled || heldCount == 0)
        {
            if (lastPlayedNote >= 0) { ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1; }
            return ev;
        }

        if (dirty) rebuildPattern();
        if (patternSize == 0) return ev;

        // Clean clock switch: kill any sounding note and reset BOTH clocks so the
        // engine cannot leave a stuck note or a stale grid across a lock change.
        if (hostLocked != wasLocked)
        {
            wasLocked = hostLocked;
            sampleCounter = 0;
            lockedInited = false;
            lastFiredGlobalStep = kNoStep;
            prevSongBeat = -1.0e18;
            if (lastPlayedNote >= 0) { ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1; }
            return ev;
        }

        if (hostLocked) return advanceLocked(songBeat);
        return advanceFree(bpm, transportPlaying);
    }

    void reset() noexcept
    {
        currentStep = 0;
        sampleCounter = 0;
        lastPlayedNote = -1;
        wasLocked = false;
        lockedInited = false;
        lastFiredGlobalStep = kNoStep;
        prevSongBeat = -1.0e18;
        if (!latch) { heldCount = 0; orderCount = 0; }
        dirty = true;
    }

private:
    // Sentinel for "no step fired yet"; every real global step index is >= 0.
    static constexpr long long kNoStep = -1;

    // ------------------------------------------------------------------ free-run
    // Original counter-based clock (transport stopped or no host song position).
    // Behaviour is unchanged from before the host-lock feature.
    StepEvent advanceFree(double bpm, bool transportPlaying) noexcept
    {
        StepEvent ev;

        // C7: free-run tempo no longer requires the transport to be playing — a
        // stopped-transport audition still steps at the host BPM (render-proven:
        // 160 BPM was playing 250 ms steps instead of 187.5). 120 remains the
        // fallback only for an invalid (<=0) BPM.
        (void)transportPlaying;
        const double effBpm = (bpm > 0.0) ? bpm : 120.0;
        const double samplesPerStep = sr * 60.0 / effBpm * getBeatsPerStep(rateDivision);

        ++sampleCounter;

        // Symmetric swing: the even step (leading into the offbeat) lengthens and
        // the odd step shortens by the same amount, so each pair still lasts two
        // grid steps and downbeats stay on the tempo grid.
        double effStep = samplesPerStep;
        if (swing > 0.0f)
            effStep *= (currentStep % 2 == 0) ? (1.0 + (double)swing * 0.5)
                                              : (1.0 - (double)swing * 0.5);
        const double gateSamples = effStep * (double)gateLength;

        if (sampleCounter == 1)
        {
            const int patIdx = (mode == ArpMode::Random) ? rng.nextInt(patternSize)
                                                         : (currentStep % patternSize);
            const NoteInfo note = pattern[(size_t)patIdx];
            const bool stepActive = stepPattern[(size_t)(currentStep % 16)];

            if (stepActive)
            {
                // C6: always release the pending note before a new note-on, even
                // when it is the same pitch (consecutive same-pitch steps at gate
                // 1.0 used to stack voices with no release). C1's seeded retrigger
                // keeps the immediate re-attack click-free.
                if (lastPlayedNote >= 0)
                { ev.noteOffValid = true; ev.offNote = lastPlayedNote; }
                ev.noteOnValid = true;
                ev.onNote = note.note;
                ev.onVel = getVelocity(note.velocity, currentStep);
                lastPlayedNote = note.note;
            }
            else if (lastPlayedNote >= 0)
            {
                ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1;
            }
        }

        if ((double)sampleCounter >= gateSamples && lastPlayedNote >= 0 && !ev.noteOnValid)
        {
            ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1;
        }

        if ((double)sampleCounter >= effStep)
        {
            sampleCounter = 0;
            if (++currentStep >= patternSize) currentStep = 0;
        }

        return ev;
    }

    // -------------------------------------------------------------- host-locked
    // Stateless grid clock. Step k spans [k*bps, (k+1)*bps) in song beats. With
    // swing s the even step onset stays on the grid (k*bps) and the odd step
    // onset is delayed to k*bps + s*0.5*bps, its span shortened to (1-s*0.5)*bps,
    // exactly reproducing the free-run swing intervals while pinning even onsets
    // to the host grid. Notes fire once per NOT-YET-FIRED step; a backward jump
    // in songBeat (loop wrap) clears the fired marker so the grid re-fires.
    StepEvent advanceLocked(double songBeat) noexcept
    {
        StepEvent ev;

        // Loop-wrap: song position regressed -> let the grid re-fire from here.
        if (songBeat < prevSongBeat - 1.0e-9) lastFiredGlobalStep = kNoStep;
        prevSongBeat = songBeat;

        // Before the transport reaches beat 0: silent (no negative-modulo grid).
        if (songBeat < 0.0)
        {
            if (lastPlayedNote >= 0) { ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1; }
            return ev;
        }

        const double bps = getBeatsPerStep(rateDivision);
        const long long globalStep = (long long)std::floor(songBeat / bps);

        // Quantized start: notes pressed mid-step wait for the NEXT boundary by
        // marking the current step already fired (strict hard-sync quantize).
        if (!lockedInited) { lastFiredGlobalStep = globalStep; lockedInited = true; }

        // Fire the step's note-on once, at/after its (swing-adjusted) onset.
        if (globalStep != lastFiredGlobalStep)
        {
            const double swingOff  = (globalStep & 1) ? (double)swing * 0.5 * bps : 0.0;
            const double onsetBeat = (double)globalStep * bps + swingOff;
            if (songBeat >= onsetBeat)
            {
                // C8: a forward seek can land past this step's own gate-off beat;
                // firing the note-on there produces a blip. Skip the note-on in
                // that case, but mark the step fired either way so the grid moves on.
                const double durB = bps * ((globalStep & 1) ? (1.0 - (double)swing * 0.5)
                                                            : (1.0 + (double)swing * 0.5));
                const bool pastGate = songBeat >= onsetBeat + (double)gateLength * durB;

                const int idx = (int)(globalStep % patternSize);
                currentStep = idx;
                const int patIdx = (mode == ArpMode::Random) ? rng.nextInt(patternSize) : idx;
                const NoteInfo note = pattern[(size_t)patIdx];
                const bool stepActive = stepPattern[(size_t)(idx % 16)] && !pastGate;

                if (stepActive)
                {
                    // C6: always release the pending note before the new note-on.
                    if (lastPlayedNote >= 0)
                    { ev.noteOffValid = true; ev.offNote = lastPlayedNote; }
                    ev.noteOnValid = true;
                    ev.onNote = note.note;
                    ev.onVel = getVelocity(note.velocity, idx);
                    lastPlayedNote = note.note;
                }
                else if (lastPlayedNote >= 0)
                {
                    ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1;
                }
                lastFiredGlobalStep = globalStep;
            }
        }

        // Gate-off for the sounding note (belongs to lastFiredGlobalStep).
        if (lastPlayedNote >= 0 && !ev.noteOnValid && lastFiredGlobalStep >= 0)
        {
            const long long g = lastFiredGlobalStep;
            const double swOff = (g & 1) ? (double)swing * 0.5 * bps : 0.0;
            const double onB   = (double)g * bps + swOff;
            const double durB  = bps * ((g & 1) ? (1.0 - (double)swing * 0.5)
                                                : (1.0 + (double)swing * 0.5));
            if (songBeat >= onB + (double)gateLength * durB)
            {
                ev.noteOffValid = true; ev.offNote = lastPlayedNote; lastPlayedNote = -1;
            }
        }

        return ev;
    }

    struct NoteInfo { int note = 60; int velocity = 100; };

    static void removeNote(std::array<NoteInfo, kMaxHeld>& arr, int& count, int noteNumber) noexcept
    {
        int w = 0;
        for (int r = 0; r < count; ++r)
            if (arr[(size_t)r].note != noteNumber)
                arr[(size_t)w++] = arr[(size_t)r];
        count = w;
    }

    void pushPattern(const NoteInfo& n) noexcept
    {
        if (patternSize < kMaxPattern) pattern[(size_t)patternSize++] = n;
    }

    void rebuildPattern() noexcept
    {
        dirty = false;
        patternSize = 0;
        if (heldCount == 0) return;

        // Sorted-ascending base, expanded across octaves.
        std::array<NoteInfo, kMaxHeld> sorted = held;
        std::sort(sorted.begin(), sorted.begin() + heldCount,
                  [](const NoteInfo& a, const NoteInfo& b) { return a.note < b.note; });

        const int baseStart = 0;
        for (int oct = 0; oct < octaveRange; ++oct)
            for (int i = 0; i < heldCount; ++i)
                pushPattern({ sorted[(size_t)i].note + oct * 12, sorted[(size_t)i].velocity });
        const int baseEnd = patternSize; // [baseStart, baseEnd) is the ascending run

        switch (mode)
        {
            case ArpMode::Up:
            case ArpMode::Random:
            case ArpMode::Chord: // JUCE Chord triggered one note per step == Up
                break;

            case ArpMode::Down:
                std::reverse(pattern.begin() + baseStart, pattern.begin() + baseEnd);
                break;

            case ArpMode::UpDown:
            {
                const int n = baseEnd - baseStart;
                if (n > 1)
                    for (int i = n - 2; i >= 1; --i) // skip first and last
                        pushPattern(pattern[(size_t)(baseStart + i)]);
                break;
            }

            case ArpMode::DownUp:
            {
                const int n = baseEnd - baseStart;
                std::reverse(pattern.begin() + baseStart, pattern.begin() + baseEnd);
                if (n > 1)
                    for (int i = 1; i <= n - 2; ++i) // ascending, skip first and last
                        pushPattern({ sorted[(size_t)(i % heldCount)].note + (i / heldCount) * 12,
                                      sorted[(size_t)(i % heldCount)].velocity });
                break;
            }

            case ArpMode::Order:
            {
                patternSize = 0;
                for (int oct = 0; oct < octaveRange; ++oct)
                    for (int i = 0; i < orderCount; ++i)
                        pushPattern({ order[(size_t)i].note + oct * 12, order[(size_t)i].velocity });
                break;
            }
        }
    }

    int getVelocity(int originalVel, int step) const noexcept
    {
        switch (velMode)
        {
            case ArpVelocityMode::AsPlayed: return originalVel;
            case ArpVelocityMode::Fixed:    return fixedVel;
            case ArpVelocityMode::AccentPattern:
            {
                float accent = 0.7f;
                switch (accentPattern)
                {
                    case ArpAccentPattern::Downbeat:   accent = (step % 4 == 0) ? 1.0f : 0.6f; break;
                    case ArpAccentPattern::EveryOther: accent = (step % 2 == 0) ? 1.0f : 0.6f; break;
                    case ArpAccentPattern::RampUp:     accent = 0.4f + 0.6f * ((float)(step % 8) / 7.0f); break;
                    case ArpAccentPattern::RampDown:   accent = 1.0f - 0.6f * ((float)(step % 8) / 7.0f); break;
                }
                return clampi((int)(127.0f * accent), 1, 127);
            }
        }
        return originalVel;
    }

    double sr = 44100.0;
    bool enabled = false;
    ArpMode mode = ArpMode::Up;
    int octaveRange = 1;
    ArpRateDivision rateDivision = ArpRateDivision::Eighth;
    float gateLength = 0.5f, swing = 0.0f;
    bool latch = false;

    ArpVelocityMode velMode = ArpVelocityMode::AsPlayed;
    int fixedVel = 100;
    ArpAccentPattern accentPattern = ArpAccentPattern::Downbeat;

    std::array<bool, 16> stepPattern { { true, true, true, true, true, true, true, true,
                                         true, true, true, true, true, true, true, true } };

    std::array<NoteInfo, kMaxHeld>   held {};
    std::array<NoteInfo, kMaxHeld>   order {};
    std::array<NoteInfo, kMaxPattern> pattern {};
    int heldCount = 0, orderCount = 0, patternSize = 0;
    bool dirty = true;

    int currentStep = 0;
    long long sampleCounter = 0;
    int lastPlayedNote = -1;
    Xorshift rng;

    // Host-locked (song-position) clock state.
    bool      wasLocked = false;          // previous lock state (transition guard)
    bool      lockedInited = false;       // quantized-start done for this lock span
    long long lastFiredGlobalStep = kNoStep;
    double    prevSongBeat = -1.0e18;     // for backward-jump (loop-wrap) detection
};

} // namespace msynth
