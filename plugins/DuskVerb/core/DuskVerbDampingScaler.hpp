// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DampingScaler: keeps the damping card (Bass, Mid and Treble Multiply, Low and High
// Crossover) live on the engines that do not read it.
//
// Several presets replace the three-band damping with a per-band decay table that
// FactoryPreset::applyEngineConfig installs: the plate octave GEQ (Dattorro and vintage plate),
// the AccurateHall octave GEQ (whose tail the Sparse and Tiled Room composites share), the
// DenseHall octave GEQ and the ParallelMultiband bands. The QuadTank hi-mid and air
// multipliers override Treble above the high crossover, and the FDN inside the Gated and
// Shimmer engines takes its high band only from setAirTrebleMultiply, which the engine's
// Treble setter never reaches. On all of them the knobs were dead.
//
// The knobs act relative to the preset. Its own knob values S (the row through the host round
// trip, so they equal the parameters bit for bit after a recall) describe the table it
// installs; the current values C ask for the per-frequency decay ratio
//
//     r(f) = M(f; C) / M(f; S)
//     log M(f) = (1 - wL) log bass + wL ((1 - wH) log mid + wH log treble)
//     wL = clamp(0.5 + log2(f / lowX), 0, 1),  wH = clamp(0.5 + log2(f / highX), 0, 1)
//
// that is, the three-band multiplier profile with a one-octave crossfade at each crossover.
// Each table T60 is scaled by r at its band's frequency, always from the table as it was right
// after the engine config went in, never from what this scaler pushed since. With C == S
// nothing is pushed beyond what the config installs, so every preset sounds exactly as before.
//
// Cost on the audio thread (48 kHz, measured 2026-10-24; every setter redesigns its GEQ in
// full): one octave band costs about 0.08 ms on the plates (both tanks) and 0.3 ms on
// AccurateHall (16 lines), a whole table 0.7-1.0 ms and 2.9 ms. Those two take new knob values
// at most every 20 ms and then push one changed band per 5 ms of audio, so no block of 5 ms or
// less pays for more than one band, which is less than a Decay knob move already costs on
// AccurateHall (0.5 ms). A whole DenseHall table costs 0.18 ms (0.29 ms at worst): it takes new
// knob values at most every 20 ms too, all bands at once. The ParallelMultiband bands (1 us)
// and the FDN high band (2 us) follow every block. Straight after a config install every band
// is pushed at once, as the install itself was.
//
// Realtime: no allocation, no lock, no I/O. One instance per DuskVerbEngine, used on the audio
// thread or while processing is stopped (construction, prepare), never from two at once.

#pragma once

#include "../src/dsp/DuskVerbEngine.h"
#include "../src/FactoryPresets.h"
#include "DuskVerbAlgorithms.hpp"
#include "DuskVerbDampingProfile.hpp"
#include "DuskVerbParamTable.hpp"

#include <algorithm>
#include <cmath>

namespace duskverb
{

// A preset's damping knobs as the DSP holds them after it is recalled: each row value through
// the host round trip, range clamps included (Drum Plate's 99 Hz crossover arrives as 200 Hz).
inline DampingKnobs storedDampingKnobs (const FactoryPreset& preset) noexcept
{
    const auto dsp = [] (int index, float plain) { return dspFromPlain (paramDesc (index), plain); };
    return { dsp (BassMult, preset.bassMult), dsp (MidMult, preset.midMult), dsp (Damping, preset.damping),
             dsp (Crossover, preset.crossover), dsp (HighCrossover, preset.highCrossover) };
}

class DampingScaler
{
public:
    static constexpr int kOctaves = 9, kPmbBands = 6;
    // Where each table's bands sit: the octave GEQs' 63 Hz..16 kHz, the ParallelMultiband
    // bands (<120 / 120-350 / 350-1k / 1k-3k / 3k-8k / >8k), the QuadTank hi-mid (4-8 kHz) and
    // air (>8 kHz) bands, and the FDN high band.
    static constexpr float kOctaveHz[kOctaves] = { 63.0f, 126.0f, 252.0f, 504.0f, 1008.0f,
                                                   2016.0f, 4032.0f, 8064.0f, 16128.0f };
    static constexpr float kPmbHz[kPmbBands] = { 69.0f, 205.0f, 590.0f, 1730.0f, 4900.0f, 12000.0f };
    static constexpr float kQuadHiMidHz = 5657.0f, kQuadAirHz = 12000.0f, kAirHz = 12000.0f;

    void prepare (double sampleRate) noexcept
    {
        holdSamples_    = std::max (1, static_cast<int> (sampleRate * kHoldSeconds));
        samplesPerPush_ = std::max (1, static_cast<int> (sampleRate * kSecondsPerPush));
        holdRemaining_  = 0;
        credit_         = 0;
    }

    // Right after FactoryPreset::applyEngineConfig (`presetConfig`) or
    // DuskVerbEngine::reapplyNeutralEngineConfig ran on `engine`: its tables become the
    // unscaled base, voiced at `stored`. The neutral config leaves the ParallelMultiband bands
    // alone, so their base stays the one captured with the last preset: the engine may still
    // hold this scaler's scaling of it, which the next update() undoes.
    void capture (const DuskVerbEngine& engine, const DampingKnobs& stored, bool presetConfig) noexcept
    {
        stored_ = stored;
        captureTable (dattorro_, engine.isDattorroOctaveT60Active(),
                      [&engine] (int k) { return engine.getDattorroOctaveT60 (k); });
        captureTable (accurateHall_, engine.isAccurateHallOctaveT60Active(),
                      [&engine] (int k) { return engine.getAccurateHallOctaveT60 (k); });
        captureTable (denseHall_, engine.isDenseHallOctaveT60Active(),
                      [&engine] (int k) { return engine.getDenseHallOctaveT60 (k); });
        if (presetConfig || ! havePmb_)
        {
            for (int b = 0; b < kPmbBands; ++b)
            {
                pmb_.t60[b]    = pmbNow_[b] = engine.getPmbBandT60 (b);
                pmb_.level[b]  = engine.getPmbBandLevel (b);
                pmb_.direct[b] = engine.getPmbBandDirect (b);
                pmb_.width[b]  = engine.getPmbBandWidth (b);
            }
            havePmb_ = true;
        }
        flush_ = true;
    }

    // Audio thread, once per block, after the knob pushes: moves the table of the engine
    // `algorithm` (a resolved slot) plays toward the one `knobs` ask for.
    void update (DuskVerbEngine& engine, int algorithm, const DampingKnobs& knobs, int numSamples) noexcept
    {
        const bool flush = flush_;
        flush_ = false;
        holdRemaining_ = std::max (0, holdRemaining_ - numSamples);
        if (flush)
        {
            latched_ = knobs;
            holdRemaining_ = 0;
        }
        else if (knobs != latched_ && holdRemaining_ == 0)
        {
            latched_ = knobs;
            holdRemaining_ = holdSamples_;
        }
        // Band credit for the costly tables: audio time earned, capped at one block (or one
        // band, for blocks shorter than that) so a quiet stretch cannot bank a burst.
        credit_ = std::min (credit_ + numSamples, std::max (numSamples, samplesPerPush_));
        const int budget = flush ? kOctaves : credit_ / samplesPerPush_;

        if (algorithm < 0 || algorithm >= kNumSharedAlgorithms)
            return;                                     // an own engine reads its knobs itself
        switch (getAlgorithmConfig (algorithm).engine)
        {
            case EngineType::Dattorro:
            case EngineType::DattorroVintage:
                spend (flush, stepTable (dattorro_, latched_, budget,
                           [&engine] (int k, float s) { engine.setDattorroOctaveT60 (k, s); }));
                break;
            case EngineType::VintageTank:               // these four render the AccurateHall tail
            case EngineType::AccurateHall:
            case EngineType::SparseField:
            case EngineType::AccurateHall32:
            case EngineType::TiledRoom:
                spend (flush, stepTable (accurateHall_, latched_, budget,
                           [&engine] (int k, float s) { engine.setAccurateHallOctaveT60 (k, s); }));
                break;
            case EngineType::DenseHall:
                stepTable (denseHall_, latched_, kOctaves,
                           [&engine] (int k, float s) { engine.setDenseHallOctaveT60 (k, s); });
                break;
            case EngineType::ParallelMultiband:
                for (int b = 0; b < kPmbBands; ++b)
                {
                    const float want = pmb_.t60[b] * decayRatio (knobs, stored_, kPmbHz[b]);
                    if (want == pmbNow_[b])
                        continue;
                    engine.setPmbBand (b, want, pmb_.level[b], pmb_.direct[b], pmb_.width[b]);
                    pmbNow_[b] = want;
                }
                break;
            case EngineType::NonLinear:
            {
                // Mid Multiply is the gate threshold on this engine, not a decay multiplier.
                DampingKnobs decay = knobs;
                decay.mid = stored_.mid;
                stepAir (nonLinearAir_, decay, [&engine] (float m) { engine.setNonLinearAirTrebleMultiply (m); });
                break;
            }
            case EngineType::Shimmer:
                stepAir (shimmerAir_, knobs, [&engine] (float m) { engine.setShimmerAirTrebleMultiply (m); });
                break;
            default:                                    // the three-band damping reads the knobs
                break;
        }
    }

    // The QuadTank hi-mid or air multiplier to push for parameter value `value`: scaled like
    // a band at `hz` while it overrides Treble, untouched as the -1 "follow Treble" sentinel
    // (Treble then reaches that band natively).
    float quadMultiplier (float value, float hz, const DampingKnobs& knobs) const noexcept
    {
        return value >= 0.0f ? value * decayRatio (knobs, stored_, hz) : value;
    }

private:
    static constexpr double kHoldSeconds    = 0.020;   // the octave tables take new knob values this often
    static constexpr double kSecondsPerPush = 0.005;   // ... and the costly ones one band per this much audio

    struct OctaveTable
    {
        float base[kOctaves] {};
        float now[kOctaves] {};    // what the engine holds
        bool  active = false;
        int   cursor = 0;          // where the next budgeted push starts looking
    };

    template <typename Get>
    static void captureTable (OctaveTable& t, bool active, Get&& get) noexcept
    {
        t.active = active;
        t.cursor = 0;
        for (int k = 0; k < kOctaves; ++k)
            t.base[k] = t.now[k] = get (k);
    }

    // Pushes up to `budget` bands whose wanted T60 differs from what the engine holds, round
    // robin, so a band that keeps changing cannot starve the others. Returns how many it pushed.
    template <typename Push>
    int stepTable (OctaveTable& t, const DampingKnobs& knobs, int budget, Push&& push) noexcept
    {
        if (! t.active)
            return 0;
        const int start = t.cursor;
        int pushed = 0;
        for (int i = 0; i < kOctaves && pushed < budget; ++i)
        {
            const int k = (start + i) % kOctaves;
            const float want = t.base[k] * decayRatio (knobs, stored_, kOctaveHz[k]);
            if (want == t.now[k])
                continue;
            push (k, want);
            t.now[k] = want;
            t.cursor = (k + 1) % kOctaves;
            ++pushed;
        }
        return pushed;
    }

    void spend (bool flush, int pushed) noexcept
    {
        if (! flush)
            credit_ -= pushed * samplesPerPush_;
    }

    // The embedded FDN's high band: 1 (its default, never set by a preset) at the stored knobs.
    template <typename Push>
    void stepAir (float& now, const DampingKnobs& knobs, Push&& push) noexcept
    {
        const float want = knobs == stored_ ? 1.0f
                                            : std::clamp (decayRatio (knobs, stored_, kAirHz), 0.1f, 1.5f);
        if (want == now)
            return;
        push (want);
        now = want;
    }

    DampingKnobs stored_, latched_;
    OctaveTable  dattorro_, accurateHall_, denseHall_;
    struct { float t60[kPmbBands] {}, level[kPmbBands] {}, direct[kPmbBands] {}, width[kPmbBands] {}; } pmb_;
    float pmbNow_[kPmbBands] {};
    bool  havePmb_ = false;
    // The embedded FDNs keep their high band across preset swaps and re-prepares (nothing but
    // this scaler sets it), so what they hold is tracked for the life of the engine.
    float nonLinearAir_ = 1.0f, shimmerAir_ = 1.0f;
    bool  flush_ = false;
    int   holdSamples_ = 960, samplesPerPush_ = 240, holdRemaining_ = 0, credit_ = 0;
};

} // namespace duskverb
