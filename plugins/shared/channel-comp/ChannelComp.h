#pragma once

// ChannelComp — streamlined per-channel mixer compressor for hosts that need
// a lean Opto / FET / VCA path without the bus-grade extras of the full
// UniversalCompressor (no internal oversampling, no sidechain HP/EQ/true-peak/
// transient-shaper/lookahead, no mix wet-dry, no auto-makeup, no bypass-fade,
// no stereo-link). Same per-sample mode DSP as UniversalCompressor — same
// hardware emulation, same character — just stripped of features that would
// run unconditionally in the standard path.
//
// Implementation strategy: own a UniversalCompressor instance and flip its
// minimalProcessingMode flag at construction. processBlock() then takes the
// fast-path branch in UniversalCompressor::processBlock that calls the mode's
// process() per-sample at native rate. The host writes parameter atomics
// directly via getParameters() (lock-free, notification-free) — same pattern
// the donor plugin's audio thread uses internally.
//
// Header-only; sits in the Dusk plugins shared/ tree so any consumer (ADH DAW,
// future internal tools) picks up algorithm changes the moment the donor is
// rebuilt.

#include "../../multi-comp/UniversalCompressor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace dusk
{
class ChannelComp
{
public:
    ChannelComp()
    {
        comp.setMinimalProcessing (true);
    }

    // Match the host's prepareToPlay contract. Channel count is fixed at 1
    // per ChannelComp instance (one per mono channel strip); hosts that need
    // stereo construct a left/right pair.
    void prepare (double sampleRate, int blockSize, int numChannels = 1)
    {
        comp.setPlayConfigDetails (numChannels, numChannels, sampleRate, juce::jmax (1, blockSize));
        comp.prepareToPlay (sampleRate, juce::jmax (1, blockSize));
        midiScratch.clear();
    }

    // RT-safe: wraps host buffer pointers in a juce::AudioBuffer and calls the
    // donor's processBlock. Fast-path branch is taken because the flag was set
    // at construction.
    void processBlock (juce::AudioBuffer<float>& buffer) noexcept
    {
        midiScratch.clear();
        comp.processBlock (buffer, midiScratch);
    }

    // Direct access to the underlying APVTS so hosts can cache parameter
    // atomic pointers and write SI-unit values via std::atomic::store. Same
    // pattern the donor's audio thread uses to read params.
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return comp.getParameters(); }

    // Most recent gain reduction (dB, negative = reduction) for metering.
    float getGainReductionDb() const noexcept { return comp.getGainReduction(); }

    UniversalCompressor& getUnderlying() noexcept { return comp; }

private:
    UniversalCompressor comp;
    juce::MidiBuffer    midiScratch;
};
} // namespace dusk
