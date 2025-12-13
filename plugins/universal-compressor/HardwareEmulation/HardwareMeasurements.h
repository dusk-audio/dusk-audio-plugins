/*
  ==============================================================================

    HardwareMeasurements.h
    Hardware measurement data structures for compressor emulation

    Contains measured characteristics from classic hardware units:
    - Teletronix LA-2A (Opto)
    - UREI 1176 Rev A (FET)
    - DBX 160 (VCA)
    - SSL G-Series Bus Compressor

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>

namespace HardwareEmulation {

//==============================================================================
// Harmonic profile from hardware measurements
struct HarmonicProfile
{
    float h2 = 0.0f;          // 2nd harmonic (even, warm)
    float h3 = 0.0f;          // 3rd harmonic (odd, aggressive)
    float h4 = 0.0f;          // 4th harmonic (even)
    float h5 = 0.0f;          // 5th harmonic (odd)
    float h6 = 0.0f;          // 6th harmonic (even)
    float h7 = 0.0f;          // 7th harmonic (odd)
    float evenOddRatio = 0.5f; // Balance: 0=all odd, 1=all even
};

//==============================================================================
// Timing characteristics measured from hardware
struct TimingProfile
{
    float attackMinMs = 0.0f;     // Fastest attack
    float attackMaxMs = 0.0f;     // Slowest attack
    float releaseMinMs = 0.0f;    // Fastest release
    float releaseMaxMs = 0.0f;    // Slowest release
    float attackCurve = 0.0f;     // 0=linear, 1=logarithmic
    float releaseCurve = 0.0f;    // 0=linear, 1=logarithmic
    bool programDependent = false; // Adaptive timing
};

//==============================================================================
// Frequency response deviations from flat
struct FrequencyResponse
{
    float lowShelfFreq = 100.0f;
    float lowShelfGain = 0.0f;    // dB
    float highShelfFreq = 10000.0f;
    float highShelfGain = 0.0f;   // dB
    float resonanceFreq = 0.0f;   // 0 = no resonance
    float resonanceQ = 0.707f;
    float resonanceGain = 0.0f;   // dB
};

//==============================================================================
// Transformer characteristics
struct TransformerProfile
{
    bool hasTransformer = true;
    float saturationThreshold = 0.8f;  // Level where saturation begins (0-1)
    float saturationAmount = 0.0f;     // 0-1 saturation intensity
    float lowFreqSaturation = 1.0f;    // LF saturation multiplier (transformers saturate more at LF)
    float highFreqRolloff = 20000.0f;  // -3dB point in Hz
    float dcBlockingFreq = 10.0f;      // Hz
    HarmonicProfile harmonics;
};

//==============================================================================
// Complete hardware unit profile
struct HardwareUnitProfile
{
    const char* name;
    const char* modeledUnit;

    // Stage-specific harmonic profiles
    HarmonicProfile inputStageHarmonics;
    HarmonicProfile compressionStageHarmonics;
    HarmonicProfile outputStageHarmonics;

    // Transformer characteristics
    TransformerProfile inputTransformer;
    TransformerProfile outputTransformer;

    // Frequency response shaping
    FrequencyResponse preCompressionEQ;
    FrequencyResponse postCompressionEQ;

    // Timing characteristics
    TimingProfile timing;

    // General specs
    float noiseFloor = -90.0f;         // dBFS
    float headroom = 20.0f;            // dB above 0VU
    float intermodulationDistortion = 0.0f; // IMD percentage
};

//==============================================================================
// Measured profiles for each compressor type
namespace Profiles {

//------------------------------------------------------------------------------
// LA-2A Opto profile (based on Teletronix measurements)
// Characteristics: Warm, smooth, program-dependent, tube coloration
inline HardwareUnitProfile createLA2A()
{
    HardwareUnitProfile profile;
    profile.name = "LA-2A";
    profile.modeledUnit = "Teletronix LA-2A";

    // Input stage: Tube input (12AX7)
    profile.inputStageHarmonics = {
        .h2 = 0.025f,   // 2.5% 2nd harmonic (tube warmth)
        .h3 = 0.008f,   // 0.8% 3rd harmonic
        .h4 = 0.003f,   // 0.3% 4th harmonic
        .h5 = 0.001f,
        .evenOddRatio = 0.75f  // Even-dominant (tube character)
    };

    // Compression stage: T4B optical cell
    profile.compressionStageHarmonics = {
        .h2 = 0.015f,   // T4B cell adds subtle harmonics
        .h3 = 0.003f,
        .evenOddRatio = 0.85f
    };

    // Output stage: 12AX7/12BH7 tubes
    profile.outputStageHarmonics = {
        .h2 = 0.035f,   // Output tubes add more warmth
        .h3 = 0.012f,
        .h4 = 0.004f,
        .evenOddRatio = 0.70f
    };

    // Input transformer (UTC A-10)
    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.75f,
        .saturationAmount = 0.15f,
        .lowFreqSaturation = 1.3f,   // Core saturates more at LF
        .highFreqRolloff = 18000.0f,
        .dcBlockingFreq = 20.0f,
        .harmonics = { .h2 = 0.008f, .h3 = 0.003f, .evenOddRatio = 0.7f }
    };

    // Output transformer
    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.8f,
        .saturationAmount = 0.1f,
        .lowFreqSaturation = 1.2f,
        .highFreqRolloff = 16000.0f,
        .dcBlockingFreq = 15.0f,
        .harmonics = { .h2 = 0.006f, .h3 = 0.002f, .evenOddRatio = 0.75f }
    };

    // Timing: T4B optical cell characteristics
    profile.timing = {
        .attackMinMs = 10.0f,    // T4B fast attack
        .attackMaxMs = 10.0f,    // Fixed (program-dependent)
        .releaseMinMs = 60.0f,   // Fast release portion
        .releaseMaxMs = 5000.0f, // Slow phosphor decay
        .attackCurve = 0.3f,
        .releaseCurve = 0.8f,    // Logarithmic release
        .programDependent = true
    };

    profile.noiseFloor = -70.0f;  // Tube noise
    profile.headroom = 18.0f;

    return profile;
}

//------------------------------------------------------------------------------
// 1176 FET profile (Rev A "Bluestripe")
// Characteristics: Fast, punchy, aggressive, FET coloration
inline HardwareUnitProfile createFET1176()
{
    HardwareUnitProfile profile;
    profile.name = "1176";
    profile.modeledUnit = "UREI 1176 Rev A";

    // Input stage: FET amplifier
    profile.inputStageHarmonics = {
        .h2 = 0.008f,   // FET is cleaner than tubes
        .h3 = 0.015f,   // More odd harmonics (FET character)
        .h4 = 0.002f,
        .h5 = 0.005f,
        .evenOddRatio = 0.35f  // Odd-dominant
    };

    // Compression stage: FET gain reduction
    profile.compressionStageHarmonics = {
        .h2 = 0.012f,
        .h3 = 0.025f,   // FET adds odd harmonics under compression
        .h5 = 0.008f,
        .evenOddRatio = 0.30f
    };

    // Output stage: Class A amplifier
    profile.outputStageHarmonics = {
        .h2 = 0.006f,
        .h3 = 0.010f,
        .h5 = 0.003f,
        .evenOddRatio = 0.40f
    };

    // Input transformer (UTC O-12)
    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.85f,
        .saturationAmount = 0.08f,
        .lowFreqSaturation = 1.15f,
        .highFreqRolloff = 20000.0f,
        .dcBlockingFreq = 15.0f,
        .harmonics = { .h2 = 0.004f, .h3 = 0.002f, .evenOddRatio = 0.65f }
    };

    // Output transformer
    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.9f,
        .saturationAmount = 0.05f,
        .lowFreqSaturation = 1.1f,
        .highFreqRolloff = 22000.0f,
        .dcBlockingFreq = 12.0f,
        .harmonics = { .h2 = 0.003f, .h3 = 0.002f, .evenOddRatio = 0.6f }
    };

    // Timing: Ultra-fast FET response
    profile.timing = {
        .attackMinMs = 0.02f,    // 20 microseconds!
        .attackMaxMs = 0.8f,     // 800 microseconds
        .releaseMinMs = 50.0f,
        .releaseMaxMs = 1100.0f,
        .attackCurve = 0.1f,     // Very fast, nearly linear
        .releaseCurve = 0.6f,
        .programDependent = true
    };

    profile.noiseFloor = -80.0f;
    profile.headroom = 24.0f;

    return profile;
}

//------------------------------------------------------------------------------
// DBX 160 VCA profile
// Characteristics: Clean, transparent, precise, "OverEasy" knee
inline HardwareUnitProfile createDBX160()
{
    HardwareUnitProfile profile;
    profile.name = "DBX 160";
    profile.modeledUnit = "DBX 160 VCA";

    // Input stage: Op-amp (very clean)
    profile.inputStageHarmonics = {
        .h2 = 0.003f,
        .h3 = 0.002f,
        .evenOddRatio = 0.55f
    };

    // Compression stage: VCA chip
    profile.compressionStageHarmonics = {
        .h2 = 0.0075f,  // VCA adds slight 2nd harmonic
        .h3 = 0.005f,
        .evenOddRatio = 0.60f
    };

    // Output stage: Clean op-amp
    profile.outputStageHarmonics = {
        .h2 = 0.002f,
        .h3 = 0.001f,
        .evenOddRatio = 0.65f
    };

    // No transformers (DBX 160 is transformerless)
    profile.inputTransformer = { .hasTransformer = false };
    profile.outputTransformer = { .hasTransformer = false };

    // Timing: Program-dependent
    profile.timing = {
        .attackMinMs = 3.0f,     // Program-dependent attack
        .attackMaxMs = 15.0f,
        .releaseMinMs = 0.0f,    // 120dB/sec release rate
        .releaseMaxMs = 0.0f,
        .attackCurve = 0.5f,
        .releaseCurve = 0.5f,
        .programDependent = true
    };

    profile.noiseFloor = -85.0f;
    profile.headroom = 21.0f;

    return profile;
}

//------------------------------------------------------------------------------
// SSL G-Series Bus Compressor
// Characteristics: Glue, punch, console sound
inline HardwareUnitProfile createSSLBus()
{
    HardwareUnitProfile profile;
    profile.name = "SSL Bus";
    profile.modeledUnit = "SSL G-Series Bus Compressor";

    // Input stage: Console electronics
    profile.inputStageHarmonics = {
        .h2 = 0.004f,
        .h3 = 0.008f,   // SSL is punchy (odd harmonics)
        .h5 = 0.003f,
        .evenOddRatio = 0.35f
    };

    // Compression stage: Quad VCA
    profile.compressionStageHarmonics = {
        .h2 = 0.006f,
        .h3 = 0.012f,
        .h5 = 0.004f,
        .evenOddRatio = 0.40f
    };

    // Output stage: Console mix bus
    profile.outputStageHarmonics = {
        .h2 = 0.008f,
        .h3 = 0.015f,
        .h5 = 0.004f,
        .evenOddRatio = 0.35f
    };

    // Input transformer (Marinair style)
    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.9f,
        .saturationAmount = 0.03f,
        .lowFreqSaturation = 1.05f,
        .highFreqRolloff = 22000.0f,
        .dcBlockingFreq = 10.0f,
        .harmonics = { .h2 = 0.002f, .h3 = 0.004f, .evenOddRatio = 0.4f }
    };

    // Output transformer
    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.92f,
        .saturationAmount = 0.02f,
        .lowFreqSaturation = 1.03f,
        .highFreqRolloff = 24000.0f,
        .dcBlockingFreq = 8.0f,
        .harmonics = { .h2 = 0.002f, .h3 = 0.003f, .evenOddRatio = 0.45f }
    };

    // Timing: Fixed attack times
    profile.timing = {
        .attackMinMs = 0.1f,
        .attackMaxMs = 30.0f,
        .releaseMinMs = 100.0f,
        .releaseMaxMs = 1200.0f,  // Plus "Auto" mode
        .attackCurve = 0.2f,
        .releaseCurve = 0.5f,
        .programDependent = false  // Fixed times (except Auto)
    };

    profile.noiseFloor = -88.0f;
    profile.headroom = 22.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Studio FET (cleaner 1176 variant)
inline HardwareUnitProfile createStudioFET()
{
    HardwareUnitProfile profile = createFET1176();
    profile.name = "Studio FET";
    profile.modeledUnit = "Clean FET Compressor";

    // 30% of vintage harmonic content
    auto scale = [](HarmonicProfile& hp, float factor) {
        hp.h2 *= factor;
        hp.h3 *= factor;
        hp.h4 *= factor;
        hp.h5 *= factor;
        hp.h6 *= factor;
        hp.h7 *= factor;
    };

    scale(profile.inputStageHarmonics, 0.3f);
    scale(profile.compressionStageHarmonics, 0.3f);
    scale(profile.outputStageHarmonics, 0.3f);
    scale(profile.inputTransformer.harmonics, 0.3f);
    scale(profile.outputTransformer.harmonics, 0.3f);

    profile.noiseFloor = -90.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Studio VCA (modern clean VCA)
inline HardwareUnitProfile createStudioVCA()
{
    HardwareUnitProfile profile;
    profile.name = "Studio VCA";
    profile.modeledUnit = "Modern VCA Compressor";

    // Very clean - minimal harmonics
    profile.inputStageHarmonics = { .h2 = 0.001f, .h3 = 0.0005f, .evenOddRatio = 0.6f };
    profile.compressionStageHarmonics = { .h2 = 0.002f, .h3 = 0.0015f, .evenOddRatio = 0.55f };
    profile.outputStageHarmonics = { .h2 = 0.001f, .h3 = 0.0005f, .evenOddRatio = 0.6f };

    // No transformers
    profile.inputTransformer = { .hasTransformer = false };
    profile.outputTransformer = { .hasTransformer = false };

    profile.timing = {
        .attackMinMs = 0.3f,
        .attackMaxMs = 75.0f,
        .releaseMinMs = 50.0f,
        .releaseMaxMs = 3000.0f,
        .attackCurve = 0.4f,
        .releaseCurve = 0.5f,
        .programDependent = false
    };

    profile.noiseFloor = -95.0f;
    profile.headroom = 24.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Digital (transparent)
inline HardwareUnitProfile createDigital()
{
    HardwareUnitProfile profile;
    profile.name = "Digital";
    profile.modeledUnit = "Transparent Digital Compressor";

    // Zero harmonics - completely transparent
    profile.inputStageHarmonics = {};
    profile.compressionStageHarmonics = {};
    profile.outputStageHarmonics = {};

    profile.inputTransformer = { .hasTransformer = false };
    profile.outputTransformer = { .hasTransformer = false };

    profile.timing = {
        .attackMinMs = 0.01f,
        .attackMaxMs = 500.0f,
        .releaseMinMs = 1.0f,
        .releaseMaxMs = 5000.0f,
        .attackCurve = 0.5f,
        .releaseCurve = 0.5f,
        .programDependent = false
    };

    profile.noiseFloor = -120.0f;
    profile.headroom = 30.0f;

    return profile;
}

} // namespace Profiles

//==============================================================================
// Profile accessor
class HardwareProfiles
{
public:
    static const HardwareUnitProfile& getLA2A()
    {
        static const HardwareUnitProfile profile = Profiles::createLA2A();
        return profile;
    }

    static const HardwareUnitProfile& getFET1176()
    {
        static const HardwareUnitProfile profile = Profiles::createFET1176();
        return profile;
    }

    static const HardwareUnitProfile& getDBX160()
    {
        static const HardwareUnitProfile profile = Profiles::createDBX160();
        return profile;
    }

    static const HardwareUnitProfile& getSSLBus()
    {
        static const HardwareUnitProfile profile = Profiles::createSSLBus();
        return profile;
    }

    static const HardwareUnitProfile& getStudioFET()
    {
        static const HardwareUnitProfile profile = Profiles::createStudioFET();
        return profile;
    }

    static const HardwareUnitProfile& getStudioVCA()
    {
        static const HardwareUnitProfile profile = Profiles::createStudioVCA();
        return profile;
    }

    static const HardwareUnitProfile& getDigital()
    {
        static const HardwareUnitProfile profile = Profiles::createDigital();
        return profile;
    }
};

} // namespace HardwareEmulation
