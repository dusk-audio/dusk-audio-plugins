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

    // Helper constructor for common initializations
    static HarmonicProfile create(float h2_, float h3_, float evenOddRatio_,
                                   float h4_ = 0.0f, float h5_ = 0.0f,
                                   float h6_ = 0.0f, float h7_ = 0.0f)
    {
        HarmonicProfile hp;
        hp.h2 = h2_;
        hp.h3 = h3_;
        hp.h4 = h4_;
        hp.h5 = h5_;
        hp.h6 = h6_;
        hp.h7 = h7_;
        hp.evenOddRatio = evenOddRatio_;
        return hp;
    }
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

    static TimingProfile create(float atkMin, float atkMax, float relMin, float relMax,
                                 float atkCurve, float relCurve, bool progDep)
    {
        TimingProfile tp;
        tp.attackMinMs = atkMin;
        tp.attackMaxMs = atkMax;
        tp.releaseMinMs = relMin;
        tp.releaseMaxMs = relMax;
        tp.attackCurve = atkCurve;
        tp.releaseCurve = relCurve;
        tp.programDependent = progDep;
        return tp;
    }
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

    static TransformerProfile createActive(float satThresh, float satAmt, float lfSat,
                                            float hfRolloff, float dcBlock,
                                            float h2, float h3, float evenOdd)
    {
        TransformerProfile tp;
        tp.hasTransformer = true;
        tp.saturationThreshold = satThresh;
        tp.saturationAmount = satAmt;
        tp.lowFreqSaturation = lfSat;
        tp.highFreqRolloff = hfRolloff;
        tp.dcBlockingFreq = dcBlock;
        tp.harmonics = HarmonicProfile::create(h2, h3, evenOdd);
        return tp;
    }

    static TransformerProfile createInactive()
    {
        TransformerProfile tp;
        tp.hasTransformer = false;
        return tp;
    }
};

//==============================================================================
// Complete hardware unit profile
struct HardwareUnitProfile
{
    const char* name = nullptr;
    const char* modeledUnit = nullptr;

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
    profile.inputStageHarmonics = HarmonicProfile::create(
        0.025f,   // 2.5% 2nd harmonic (tube warmth)
        0.008f,   // 0.8% 3rd harmonic
        0.75f,    // Even-dominant (tube character)
        0.003f,   // 0.3% 4th harmonic
        0.001f    // 5th harmonic
    );

    // Compression stage: T4B optical cell
    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.015f,   // T4B cell adds subtle harmonics
        0.003f,
        0.85f
    );

    // Output stage: 12AX7/12BH7 tubes
    profile.outputStageHarmonics = HarmonicProfile::create(
        0.035f,   // Output tubes add more warmth
        0.012f,
        0.70f,
        0.004f
    );

    // Input transformer (UTC A-10)
    profile.inputTransformer = TransformerProfile::createActive(
        0.75f,     // saturationThreshold
        0.15f,     // saturationAmount
        1.3f,      // lowFreqSaturation (core saturates more at LF)
        18000.0f,  // highFreqRolloff
        20.0f,     // dcBlockingFreq
        0.008f, 0.003f, 0.7f  // h2, h3, evenOddRatio
    );

    // Output transformer
    profile.outputTransformer = TransformerProfile::createActive(
        0.8f,      // saturationThreshold
        0.1f,      // saturationAmount
        1.2f,      // lowFreqSaturation
        16000.0f,  // highFreqRolloff
        15.0f,     // dcBlockingFreq
        0.006f, 0.002f, 0.75f  // h2, h3, evenOddRatio
    );

    // Timing: T4B optical cell characteristics
    profile.timing = TimingProfile::create(
        10.0f,    // attackMinMs - T4B fast attack
        10.0f,    // attackMaxMs - Fixed (program-dependent)
        60.0f,    // releaseMinMs - Fast release portion
        5000.0f,  // releaseMaxMs - Slow phosphor decay
        0.3f,     // attackCurve
        0.8f,     // releaseCurve - Logarithmic release
        true      // programDependent
    );

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
    profile.inputStageHarmonics = HarmonicProfile::create(
        0.008f,   // FET is cleaner than tubes
        0.015f,   // More odd harmonics (FET character)
        0.35f,    // Odd-dominant
        0.002f,
        0.005f
    );

    // Compression stage: FET gain reduction
    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.012f,
        0.025f,   // FET adds odd harmonics under compression
        0.30f,
        0.0f,
        0.008f
    );

    // Output stage: Class A amplifier
    profile.outputStageHarmonics = HarmonicProfile::create(
        0.006f,
        0.010f,
        0.40f,
        0.0f,
        0.003f
    );

    // Input transformer (UTC O-12)
    profile.inputTransformer = TransformerProfile::createActive(
        0.85f,     // saturationThreshold
        0.08f,     // saturationAmount
        1.15f,     // lowFreqSaturation
        20000.0f,  // highFreqRolloff
        15.0f,     // dcBlockingFreq
        0.004f, 0.002f, 0.65f
    );

    // Output transformer
    profile.outputTransformer = TransformerProfile::createActive(
        0.9f,      // saturationThreshold
        0.05f,     // saturationAmount
        1.1f,      // lowFreqSaturation
        22000.0f,  // highFreqRolloff
        12.0f,     // dcBlockingFreq
        0.003f, 0.002f, 0.6f
    );

    // Timing: Ultra-fast FET response
    profile.timing = TimingProfile::create(
        0.02f,    // attackMinMs - 20 microseconds!
        0.8f,     // attackMaxMs - 800 microseconds
        50.0f,    // releaseMinMs
        1100.0f,  // releaseMaxMs
        0.1f,     // attackCurve - Very fast, nearly linear
        0.6f,     // releaseCurve
        true      // programDependent
    );

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
    profile.inputStageHarmonics = HarmonicProfile::create(
        0.003f,
        0.002f,
        0.55f
    );

    // Compression stage: VCA chip
    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.0075f,  // VCA adds slight 2nd harmonic
        0.005f,
        0.60f
    );

    // Output stage: Clean op-amp
    profile.outputStageHarmonics = HarmonicProfile::create(
        0.002f,
        0.001f,
        0.65f
    );

    // No transformers (DBX 160 is transformerless)
    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    // Timing: Program-dependent
    profile.timing = TimingProfile::create(
        3.0f,     // attackMinMs - Program-dependent attack
        15.0f,    // attackMaxMs
        0.0f,     // releaseMinMs - 120dB/sec release rate
        0.0f,     // releaseMaxMs
        0.5f,     // attackCurve
        0.5f,     // releaseCurve
        true      // programDependent
    );

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
    profile.inputStageHarmonics = HarmonicProfile::create(
        0.004f,
        0.008f,   // SSL is punchy (odd harmonics)
        0.35f,
        0.0f,
        0.003f
    );

    // Compression stage: Quad VCA
    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.006f,
        0.012f,
        0.40f,
        0.0f,
        0.004f
    );

    // Output stage: Console mix bus
    profile.outputStageHarmonics = HarmonicProfile::create(
        0.008f,
        0.015f,
        0.35f,
        0.0f,
        0.004f
    );

    // Input transformer (Marinair style)
    profile.inputTransformer = TransformerProfile::createActive(
        0.9f,      // saturationThreshold
        0.03f,     // saturationAmount
        1.05f,     // lowFreqSaturation
        22000.0f,  // highFreqRolloff
        10.0f,     // dcBlockingFreq
        0.002f, 0.004f, 0.4f
    );

    // Output transformer
    profile.outputTransformer = TransformerProfile::createActive(
        0.92f,     // saturationThreshold
        0.02f,     // saturationAmount
        1.03f,     // lowFreqSaturation
        24000.0f,  // highFreqRolloff
        8.0f,      // dcBlockingFreq
        0.002f, 0.003f, 0.45f
    );

    // Timing: Fixed attack times
    profile.timing = TimingProfile::create(
        0.1f,     // attackMinMs
        30.0f,    // attackMaxMs
        100.0f,   // releaseMinMs
        1200.0f,  // releaseMaxMs - Plus "Auto" mode
        0.2f,     // attackCurve
        0.5f,     // releaseCurve
        false     // programDependent - Fixed times (except Auto)
    );

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
    profile.inputStageHarmonics = HarmonicProfile::create(0.001f, 0.0005f, 0.6f);
    profile.compressionStageHarmonics = HarmonicProfile::create(0.002f, 0.0015f, 0.55f);
    profile.outputStageHarmonics = HarmonicProfile::create(0.001f, 0.0005f, 0.6f);

    // No transformers
    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    profile.timing = TimingProfile::create(
        0.3f,     // attackMinMs
        75.0f,    // attackMaxMs
        50.0f,    // releaseMinMs
        3000.0f,  // releaseMaxMs
        0.4f,     // attackCurve
        0.5f,     // releaseCurve
        false     // programDependent
    );

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

    // Zero harmonics - completely transparent (use defaults)
    // inputStageHarmonics, compressionStageHarmonics, outputStageHarmonics use default values

    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    profile.timing = TimingProfile::create(
        0.01f,    // attackMinMs
        500.0f,   // attackMaxMs
        1.0f,     // releaseMinMs
        5000.0f,  // releaseMaxMs
        0.5f,     // attackCurve
        0.5f,     // releaseCurve
        false     // programDependent
    );

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
