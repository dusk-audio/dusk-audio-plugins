/*
  ==============================================================================

    HardwareProfiles.h
    Hardware measurement data structures for analog emulation

    Contains measured characteristics from classic hardware units:
    - Teletronix LA-2A (Opto)
    - UREI 1176 Rev A (FET)
    - DBX 160 (VCA)
    - SSL G-Series Bus Compressor
    - Studer A800 (Tape Machine)
    - Ampex ATR-102 (Tape Machine)
    - Neve 1073 (Preamp)
    - API 512c (Preamp)

    This is the shared library version - all plugins should use this.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cmath>

namespace AnalogEmulation {

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

    // Scale all harmonics by a factor
    void scale(float factor)
    {
        h2 *= factor;
        h3 *= factor;
        h4 *= factor;
        h5 *= factor;
        h6 *= factor;
        h7 *= factor;
    }

    // Get total harmonic content
    float getTotalHarmonics() const
    {
        return h2 + h3 + h4 + h5 + h6 + h7;
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
// Tube stage characteristics
struct TubeProfile
{
    bool hasTubeStage = false;
    float biasPoint = 0.0f;            // Operating point offset
    float driveAmount = 0.0f;          // Drive level (0-1)
    float gridCurrentThreshold = 0.5f; // Where grid current begins
    float millerCapacitance = 0.0f;    // HF rolloff from Miller effect
    HarmonicProfile harmonics;
};

//==============================================================================
// Tape machine characteristics
struct TapeProfile
{
    const char* machineName = "";
    const char* tapeType = "";

    // Saturation characteristics
    float saturationOnset = 0.7f;      // Level where saturation begins
    float saturationAmount = 0.3f;     // Saturation intensity
    float hysteresisAmount = 0.0f;     // Magnetic hysteresis

    // Frequency response
    float bassBoost = 0.0f;            // Low frequency emphasis (dB)
    float headBump = 0.0f;             // Head bump magnitude (dB)
    float headBumpFreq = 80.0f;        // Head bump frequency (Hz)
    float highFreqRolloff = 18000.0f;  // HF rolloff (-3dB point)

    // Noise and modulation
    float noiseFloor = -70.0f;         // dBFS
    float wowDepth = 0.0f;             // Wow modulation depth
    float flutterDepth = 0.0f;         // Flutter modulation depth

    HarmonicProfile harmonics;
};

//==============================================================================
// Complete hardware unit profile
struct HardwareUnitProfile
{
    const char* name = "";
    const char* modeledUnit = "";

    // Stage-specific harmonic profiles
    HarmonicProfile inputStageHarmonics;
    HarmonicProfile compressionStageHarmonics;
    HarmonicProfile outputStageHarmonics;

    // Transformer characteristics
    TransformerProfile inputTransformer;
    TransformerProfile outputTransformer;

    // Tube stages (if applicable)
    TubeProfile inputTube;
    TubeProfile outputTube;

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
// Measured profiles for each hardware type
namespace Profiles {

//------------------------------------------------------------------------------
// LA-2A Opto profile (based on Teletronix measurements)
inline HardwareUnitProfile createLA2A()
{
    HardwareUnitProfile profile;
    profile.name = "LA-2A";
    profile.modeledUnit = "Teletronix LA-2A";

    // Input stage: Tube input (12AX7)
    profile.inputStageHarmonics = {
        .h2 = 0.025f, .h3 = 0.008f, .h4 = 0.003f, .h5 = 0.001f,
        .evenOddRatio = 0.75f
    };

    // Compression stage: T4B optical cell
    profile.compressionStageHarmonics = {
        .h2 = 0.015f, .h3 = 0.003f, .evenOddRatio = 0.85f
    };

    // Output stage: 12AX7/12BH7 tubes
    profile.outputStageHarmonics = {
        .h2 = 0.035f, .h3 = 0.012f, .h4 = 0.004f, .evenOddRatio = 0.70f
    };

    // Input transformer (UTC A-10)
    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.75f,
        .saturationAmount = 0.15f,
        .lowFreqSaturation = 1.3f,
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

    // Tube stages
    profile.inputTube = {
        .hasTubeStage = true,
        .gridCurrentThreshold = 0.4f,
        .harmonics = { .h2 = 0.025f, .h3 = 0.008f, .evenOddRatio = 0.75f }
    };

    profile.outputTube = {
        .hasTubeStage = true,
        .gridCurrentThreshold = 0.5f,
        .harmonics = { .h2 = 0.035f, .h3 = 0.012f, .evenOddRatio = 0.70f }
    };

    // Timing
    profile.timing = {
        .attackMinMs = 10.0f,
        .attackMaxMs = 10.0f,
        .releaseMinMs = 60.0f,
        .releaseMaxMs = 5000.0f,
        .attackCurve = 0.3f,
        .releaseCurve = 0.8f,
        .programDependent = true
    };

    profile.noiseFloor = -70.0f;
    profile.headroom = 18.0f;

    return profile;
}

//------------------------------------------------------------------------------
// 1176 FET profile
inline HardwareUnitProfile createFET1176()
{
    HardwareUnitProfile profile;
    profile.name = "1176";
    profile.modeledUnit = "UREI 1176 Rev A";

    profile.inputStageHarmonics = {
        .h2 = 0.008f, .h3 = 0.015f, .h4 = 0.002f, .h5 = 0.005f,
        .evenOddRatio = 0.35f
    };

    profile.compressionStageHarmonics = {
        .h2 = 0.012f, .h3 = 0.025f, .h5 = 0.008f, .evenOddRatio = 0.30f
    };

    profile.outputStageHarmonics = {
        .h2 = 0.006f, .h3 = 0.010f, .h5 = 0.003f, .evenOddRatio = 0.40f
    };

    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.85f,
        .saturationAmount = 0.08f,
        .lowFreqSaturation = 1.15f,
        .highFreqRolloff = 20000.0f,
        .dcBlockingFreq = 15.0f,
        .harmonics = { .h2 = 0.004f, .h3 = 0.002f, .evenOddRatio = 0.65f }
    };

    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.9f,
        .saturationAmount = 0.05f,
        .lowFreqSaturation = 1.1f,
        .highFreqRolloff = 22000.0f,
        .dcBlockingFreq = 12.0f,
        .harmonics = { .h2 = 0.003f, .h3 = 0.002f, .evenOddRatio = 0.6f }
    };

    profile.timing = {
        .attackMinMs = 0.02f,
        .attackMaxMs = 0.8f,
        .releaseMinMs = 50.0f,
        .releaseMaxMs = 1100.0f,
        .attackCurve = 0.1f,
        .releaseCurve = 0.6f,
        .programDependent = true
    };

    profile.noiseFloor = -80.0f;
    profile.headroom = 24.0f;

    return profile;
}

//------------------------------------------------------------------------------
// DBX 160 VCA profile
inline HardwareUnitProfile createDBX160()
{
    HardwareUnitProfile profile;
    profile.name = "DBX 160";
    profile.modeledUnit = "DBX 160 VCA";

    profile.inputStageHarmonics = {
        .h2 = 0.003f, .h3 = 0.002f, .evenOddRatio = 0.55f
    };

    profile.compressionStageHarmonics = {
        .h2 = 0.0075f, .h3 = 0.005f, .evenOddRatio = 0.60f
    };

    profile.outputStageHarmonics = {
        .h2 = 0.002f, .h3 = 0.001f, .evenOddRatio = 0.65f
    };

    // No transformers
    profile.inputTransformer = { .hasTransformer = false };
    profile.outputTransformer = { .hasTransformer = false };

    profile.timing = {
        .attackMinMs = 3.0f,
        .attackMaxMs = 15.0f,
        .releaseMinMs = 0.0f,
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
inline HardwareUnitProfile createSSLBus()
{
    HardwareUnitProfile profile;
    profile.name = "SSL Bus";
    profile.modeledUnit = "SSL G-Series Bus Compressor";

    profile.inputStageHarmonics = {
        .h2 = 0.004f, .h3 = 0.008f, .h5 = 0.003f, .evenOddRatio = 0.35f
    };

    profile.compressionStageHarmonics = {
        .h2 = 0.006f, .h3 = 0.012f, .h5 = 0.004f, .evenOddRatio = 0.40f
    };

    profile.outputStageHarmonics = {
        .h2 = 0.008f, .h3 = 0.015f, .h5 = 0.004f, .evenOddRatio = 0.35f
    };

    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.9f,
        .saturationAmount = 0.03f,
        .lowFreqSaturation = 1.05f,
        .highFreqRolloff = 22000.0f,
        .dcBlockingFreq = 10.0f,
        .harmonics = { .h2 = 0.002f, .h3 = 0.004f, .evenOddRatio = 0.4f }
    };

    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.92f,
        .saturationAmount = 0.02f,
        .lowFreqSaturation = 1.03f,
        .highFreqRolloff = 24000.0f,
        .dcBlockingFreq = 8.0f,
        .harmonics = { .h2 = 0.002f, .h3 = 0.003f, .evenOddRatio = 0.45f }
    };

    profile.timing = {
        .attackMinMs = 0.1f,
        .attackMaxMs = 30.0f,
        .releaseMinMs = 100.0f,
        .releaseMaxMs = 1200.0f,
        .attackCurve = 0.2f,
        .releaseCurve = 0.5f,
        .programDependent = false
    };

    profile.noiseFloor = -88.0f;
    profile.headroom = 22.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Studer A800 tape machine
inline TapeProfile createStuderA800()
{
    return TapeProfile {
        .machineName = "Studer A800",
        .tapeType = "Ampex 456",
        .saturationOnset = 0.65f,
        .saturationAmount = 0.35f,
        .hysteresisAmount = 0.15f,
        .bassBoost = 1.5f,
        .headBump = 2.0f,
        .headBumpFreq = 80.0f,
        .highFreqRolloff = 16000.0f,
        .noiseFloor = -65.0f,
        .wowDepth = 0.001f,
        .flutterDepth = 0.002f,
        .harmonics = { .h2 = 0.04f, .h3 = 0.02f, .h4 = 0.01f, .evenOddRatio = 0.65f }
    };
}

//------------------------------------------------------------------------------
// Ampex ATR-102 tape machine
inline TapeProfile createAmpexATR102()
{
    return TapeProfile {
        .machineName = "Ampex ATR-102",
        .tapeType = "Ampex 456",
        .saturationOnset = 0.7f,
        .saturationAmount = 0.3f,
        .hysteresisAmount = 0.12f,
        .bassBoost = 1.0f,
        .headBump = 1.5f,
        .headBumpFreq = 100.0f,
        .highFreqRolloff = 18000.0f,
        .noiseFloor = -68.0f,
        .wowDepth = 0.0008f,
        .flutterDepth = 0.0015f,
        .harmonics = { .h2 = 0.035f, .h3 = 0.018f, .h4 = 0.008f, .evenOddRatio = 0.68f }
    };
}

//------------------------------------------------------------------------------
// Neve 1073 preamp
inline HardwareUnitProfile createNeve1073()
{
    HardwareUnitProfile profile;
    profile.name = "Neve 1073";
    profile.modeledUnit = "Neve 1073 Preamp";

    profile.inputStageHarmonics = {
        .h2 = 0.02f, .h3 = 0.008f, .h4 = 0.003f, .evenOddRatio = 0.70f
    };

    profile.outputStageHarmonics = {
        .h2 = 0.025f, .h3 = 0.01f, .h4 = 0.004f, .evenOddRatio = 0.68f
    };

    // Neve transformers are legendary for their character
    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.7f,
        .saturationAmount = 0.2f,
        .lowFreqSaturation = 1.4f,
        .highFreqRolloff = 18000.0f,
        .dcBlockingFreq = 20.0f,
        .harmonics = { .h2 = 0.015f, .h3 = 0.005f, .evenOddRatio = 0.75f }
    };

    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.75f,
        .saturationAmount = 0.15f,
        .lowFreqSaturation = 1.3f,
        .highFreqRolloff = 16000.0f,
        .dcBlockingFreq = 15.0f,
        .harmonics = { .h2 = 0.012f, .h3 = 0.004f, .evenOddRatio = 0.75f }
    };

    profile.noiseFloor = -75.0f;
    profile.headroom = 20.0f;

    return profile;
}

//------------------------------------------------------------------------------
// API 512c preamp
inline HardwareUnitProfile createAPI512c()
{
    HardwareUnitProfile profile;
    profile.name = "API 512c";
    profile.modeledUnit = "API 512c Preamp";

    profile.inputStageHarmonics = {
        .h2 = 0.01f, .h3 = 0.015f, .h5 = 0.005f, .evenOddRatio = 0.40f
    };

    profile.outputStageHarmonics = {
        .h2 = 0.012f, .h3 = 0.018f, .h5 = 0.006f, .evenOddRatio = 0.38f
    };

    // API has more aggressive, punchy transformers
    profile.inputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.8f,
        .saturationAmount = 0.12f,
        .lowFreqSaturation = 1.2f,
        .highFreqRolloff = 20000.0f,
        .dcBlockingFreq = 15.0f,
        .harmonics = { .h2 = 0.006f, .h3 = 0.01f, .evenOddRatio = 0.4f }
    };

    profile.outputTransformer = {
        .hasTransformer = true,
        .saturationThreshold = 0.85f,
        .saturationAmount = 0.08f,
        .lowFreqSaturation = 1.15f,
        .highFreqRolloff = 22000.0f,
        .dcBlockingFreq = 12.0f,
        .harmonics = { .h2 = 0.005f, .h3 = 0.008f, .evenOddRatio = 0.42f }
    };

    profile.noiseFloor = -78.0f;
    profile.headroom = 24.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Clean/Digital (transparent)
inline HardwareUnitProfile createDigital()
{
    HardwareUnitProfile profile;
    profile.name = "Digital";
    profile.modeledUnit = "Transparent Digital";

    // Zero harmonics
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
// Profile accessor class for cached profiles
class HardwareProfileLibrary
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

    static const HardwareUnitProfile& getNeve1073()
    {
        static const HardwareUnitProfile profile = Profiles::createNeve1073();
        return profile;
    }

    static const HardwareUnitProfile& getAPI512c()
    {
        static const HardwareUnitProfile profile = Profiles::createAPI512c();
        return profile;
    }

    static const HardwareUnitProfile& getDigital()
    {
        static const HardwareUnitProfile profile = Profiles::createDigital();
        return profile;
    }

    static const TapeProfile& getStuderA800()
    {
        static const TapeProfile profile = Profiles::createStuderA800();
        return profile;
    }

    static const TapeProfile& getAmpexATR102()
    {
        static const TapeProfile profile = Profiles::createAmpexATR102();
        return profile;
    }
};

} // namespace AnalogEmulation
