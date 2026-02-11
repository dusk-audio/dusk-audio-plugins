/*
  ==============================================================================

    AnalogEmulation.h
    Shared Analog Emulation Library for Dusk Audio Plugins

    This library provides reusable analog hardware emulation components:
    - Transformer saturation modeling
    - Vacuum tube emulation (12AX7, 12AT7, 12BH7, 6SN7)
    - Waveshaper lookup tables (LA-2A, 1176, DBX, SSL, Transformer, Tape)
    - Hardware profiles (measured characteristics from classic equipment)
    - DC blocking filters
    - High-frequency content estimation

    Usage:
    ------
    Include this header to get access to all analog emulation components:

        #include "../shared/AnalogEmulation/AnalogEmulation.h"

        // Use waveshaper curves
        auto& curves = AnalogEmulation::getWaveshaperCurves();
        float saturated = curves.process(input, AnalogEmulation::WaveshaperCurves::CurveType::Tape);

        // Use tube emulation
        AnalogEmulation::TubeEmulation tube;
        tube.prepare(sampleRate, 2);
        tube.setTubeType(AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
        float output = tube.processSample(input, channel);

        // Use transformer emulation
        AnalogEmulation::TransformerEmulation transformer;
        transformer.prepare(sampleRate, 2);
        transformer.setProfile(AnalogEmulation::HardwareProfileLibrary::getNeve1073().inputTransformer);
        float output = transformer.processSample(input, channel);

        // Use hardware profiles
        auto& la2a = AnalogEmulation::HardwareProfileLibrary::getLA2A();
        auto& studer = AnalogEmulation::HardwareProfileLibrary::getStuderA800();

        // Use DC blocker
        AnalogEmulation::DCBlocker dcBlocker;
        dcBlocker.prepare(sampleRate, 5.0f);  // 5Hz cutoff
        float output = dcBlocker.processSample(input);

  ==============================================================================
*/

#pragma once

// Core utilities
#include "DCBlocker.h"
#include "HighFrequencyEstimator.h"

// Waveshaper lookup tables
#include "WaveshaperCurves.h"

// Hardware profiles and measurements
#include "HardwareProfiles.h"

// Emulation processors
#include "TubeEmulation.h"
#include "TransformerEmulation.h"

namespace AnalogEmulation {

/**
 * Get library version information
 */
inline const char* getLibraryVersion()
{
    return "1.0.0";
}

/**
 * Initialize all singleton resources.
 * Call this once during plugin initialization (e.g., in prepareToPlay)
 * to ensure lookup tables are built before real-time processing.
 */
inline void initializeLibrary()
{
    // Force initialization of waveshaper lookup tables
    (void)getWaveshaperCurves();
}

} // namespace AnalogEmulation
