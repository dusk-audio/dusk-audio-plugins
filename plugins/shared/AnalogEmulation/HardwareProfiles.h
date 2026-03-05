// HardwareProfiles.h — Hardware profiles for analog emulation

#pragma once

#include <array>
#include <cmath>

namespace AnalogEmulation {

struct HarmonicProfile
{
    float h2 = 0.0f;
    float h3 = 0.0f;
    float h4 = 0.0f;
    float h5 = 0.0f;
    float h6 = 0.0f;
    float h7 = 0.0f;
    float evenOddRatio = 0.5f; // 0=all odd, 1=all even

    void scale(float factor)
    {
        h2 *= factor; h3 *= factor; h4 *= factor;
        h5 *= factor; h6 *= factor; h7 *= factor;
    }

    float getTotalHarmonics() const
    {
        return h2 + h3 + h4 + h5 + h6 + h7;
    }

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

struct TimingProfile
{
    float attackMinMs = 0.0f;
    float attackMaxMs = 0.0f;
    float releaseMinMs = 0.0f;
    float releaseMaxMs = 0.0f;
    float attackCurve = 0.0f;     // 0=linear, 1=logarithmic
    float releaseCurve = 0.0f;
    bool programDependent = false;

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

struct TransformerProfile
{
    bool hasTransformer = true;
    float saturationThreshold = 0.8f;
    float saturationAmount = 0.0f;
    float lowFreqSaturation = 1.0f;    // LF saturates more (core physics)
    float highFreqRolloff = 20000.0f;  // -3dB Hz
    float dcBlockingFreq = 10.0f;
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

struct TubeProfile
{
    bool hasTubeStage = false;
    float biasPoint = 0.0f;            // Operating point offset
    float driveAmount = 0.0f;          // Drive level (0-1)
    float gridCurrentThreshold = 0.5f; // Where grid current begins
    float millerCapacitance = 0.0f;    // HF rolloff from Miller effect
    HarmonicProfile harmonics;

    static TubeProfile create(float gridThresh, float h2, float h3, float evenOdd)
    {
        TubeProfile tp;
        tp.hasTubeStage = true;
        tp.gridCurrentThreshold = gridThresh;
        tp.harmonics = HarmonicProfile::create(h2, h3, evenOdd);
        return tp;
    }

    static TubeProfile createInactive()
    {
        TubeProfile tp;
        tp.hasTubeStage = false;
        return tp;
    }
};

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

    static TapeProfile create(const char* machine, const char* tape,
                               float satOnset, float satAmt, float hyst,
                               float bass, float bump, float bumpFreq, float hfRoll,
                               float noise, float wow, float flutter,
                               float h2, float h3, float h4, float evenOdd)
    {
        TapeProfile tp;
        tp.machineName = machine;
        tp.tapeType = tape;
        tp.saturationOnset = satOnset;
        tp.saturationAmount = satAmt;
        tp.hysteresisAmount = hyst;
        tp.bassBoost = bass;
        tp.headBump = bump;
        tp.headBumpFreq = bumpFreq;
        tp.highFreqRolloff = hfRoll;
        tp.noiseFloor = noise;
        tp.wowDepth = wow;
        tp.flutterDepth = flutter;
        tp.harmonics = HarmonicProfile::create(h2, h3, evenOdd, h4);
        return tp;
    }
};

struct HardwareUnitProfile
{
    const char* name = "";
    const char* modeledUnit = "";

    HarmonicProfile inputStageHarmonics;
    HarmonicProfile compressionStageHarmonics;
    HarmonicProfile outputStageHarmonics;

    TransformerProfile inputTransformer;
    TransformerProfile outputTransformer;

    TubeProfile inputTube;
    TubeProfile outputTube;

    FrequencyResponse preCompressionEQ;
    FrequencyResponse postCompressionEQ;

    TimingProfile timing;

    float noiseFloor = -90.0f;         // dBFS
    float headroom = 20.0f;            // dB above 0VU
    float intermodulationDistortion = 0.0f; // IMD percentage
};

namespace Profiles {

// Opto compressor
inline HardwareUnitProfile createOptoCompressor()
{
    HardwareUnitProfile profile;
    profile.name = "Opto Compressor";
    profile.modeledUnit = "Vintage Opto Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(0.025f, 0.008f, 0.75f, 0.003f, 0.001f);
    profile.compressionStageHarmonics = HarmonicProfile::create(0.015f, 0.003f, 0.85f);
    profile.outputStageHarmonics = HarmonicProfile::create(0.035f, 0.012f, 0.70f, 0.004f);

    profile.inputTransformer = TransformerProfile::createActive(
        0.75f, 0.15f, 1.3f, 18000.0f, 20.0f, 0.008f, 0.003f, 0.7f);
    profile.outputTransformer = TransformerProfile::createActive(
        0.8f, 0.1f, 1.2f, 16000.0f, 15.0f, 0.006f, 0.002f, 0.75f);

    profile.inputTube = TubeProfile::create(0.4f, 0.025f, 0.008f, 0.75f);
    profile.outputTube = TubeProfile::create(0.5f, 0.035f, 0.012f, 0.70f);

    profile.timing = TimingProfile::create(10.0f, 10.0f, 60.0f, 5000.0f, 0.3f, 0.8f, true);
    profile.noiseFloor = -70.0f;
    profile.headroom = 18.0f;
    return profile;
}

// FET compressor
inline HardwareUnitProfile createFETCompressor()
{
    HardwareUnitProfile profile;
    profile.name = "FET Compressor";
    profile.modeledUnit = "Vintage FET Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.008f, 0.015f, 0.35f, 0.002f, 0.005f
    );

    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.012f, 0.025f, 0.30f, 0.0f, 0.008f
    );

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.006f, 0.010f, 0.40f, 0.0f, 0.003f
    );

    profile.inputTransformer = TransformerProfile::createActive(
        0.85f, 0.08f, 1.15f, 20000.0f, 15.0f,
        0.004f, 0.002f, 0.65f
    );

    profile.outputTransformer = TransformerProfile::createActive(
        0.9f, 0.05f, 1.1f, 22000.0f, 12.0f,
        0.003f, 0.002f, 0.6f
    );

    profile.timing = TimingProfile::create(
        0.02f, 0.8f, 50.0f, 1100.0f, 0.1f, 0.6f, true
    );

    profile.noiseFloor = -80.0f;
    profile.headroom = 24.0f;

    return profile;
}

// Classic VCA compressor
inline HardwareUnitProfile createClassicVCA()
{
    HardwareUnitProfile profile;
    profile.name = "Classic VCA";
    profile.modeledUnit = "Classic VCA Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.003f, 0.002f, 0.55f
    );

    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.0075f, 0.005f, 0.60f
    );

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.002f, 0.001f, 0.65f
    );

    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    profile.timing = TimingProfile::create(
        3.0f, 15.0f, 0.0f, 0.0f, 0.5f, 0.5f, true
    );

    profile.noiseFloor = -85.0f;
    profile.headroom = 21.0f;

    return profile;
}

// Console Bus compressor
inline HardwareUnitProfile createConsoleBus()
{
    HardwareUnitProfile profile;
    profile.name = "Console Bus";
    profile.modeledUnit = "Console Bus Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.004f, 0.008f, 0.35f, 0.0f, 0.003f
    );

    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.006f, 0.012f, 0.40f, 0.0f, 0.004f
    );

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.008f, 0.015f, 0.35f, 0.0f, 0.004f
    );

    profile.inputTransformer = TransformerProfile::createActive(
        0.9f, 0.03f, 1.05f, 22000.0f, 10.0f,
        0.002f, 0.004f, 0.4f
    );

    profile.outputTransformer = TransformerProfile::createActive(
        0.92f, 0.02f, 1.03f, 24000.0f, 8.0f,
        0.002f, 0.003f, 0.45f
    );

    profile.timing = TimingProfile::create(
        0.1f, 30.0f, 100.0f, 1200.0f, 0.2f, 0.5f, false
    );

    profile.noiseFloor = -88.0f;
    profile.headroom = 22.0f;

    return profile;
}

// Studer A800
inline TapeProfile createStuderA800()
{
    return TapeProfile::create(
        "Studer A800", "Ampex 456",
        0.65f, 0.35f, 0.15f,      // saturation onset, amount, hysteresis
        1.5f, 2.0f, 80.0f,        // bass boost, head bump, head bump freq
        16000.0f,                  // HF rolloff
        -65.0f, 0.001f, 0.002f,   // noise, wow, flutter
        0.04f, 0.02f, 0.01f, 0.65f // h2, h3, h4, evenOdd
    );
}

// Ampex ATR-102
inline TapeProfile createAmpexATR102()
{
    return TapeProfile::create(
        "Ampex ATR-102", "Ampex 456",
        0.7f, 0.3f, 0.12f,         // saturation onset, amount, hysteresis
        1.0f, 1.5f, 100.0f,        // bass boost, head bump, head bump freq
        18000.0f,                   // HF rolloff
        -68.0f, 0.0008f, 0.0015f,  // noise, wow, flutter
        0.035f, 0.018f, 0.008f, 0.68f // h2, h3, h4, evenOdd
    );
}

// British console preamp
inline HardwareUnitProfile createBritishConsole()
{
    HardwareUnitProfile profile;
    profile.name = "British Console";
    profile.modeledUnit = "British Console Preamp";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.02f, 0.008f, 0.70f, 0.003f
    );

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.025f, 0.01f, 0.68f, 0.004f
    );

    profile.inputTransformer = TransformerProfile::createActive(
        0.7f, 0.2f, 1.4f, 18000.0f, 20.0f,
        0.015f, 0.005f, 0.75f
    );

    profile.outputTransformer = TransformerProfile::createActive(
        0.75f, 0.15f, 1.3f, 16000.0f, 15.0f,
        0.012f, 0.004f, 0.75f
    );

    profile.noiseFloor = -75.0f;
    profile.headroom = 20.0f;

    return profile;
}

// American console preamp
inline HardwareUnitProfile createAmericanConsole()
{
    HardwareUnitProfile profile;
    profile.name = "American Console";
    profile.modeledUnit = "American Console Preamp";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.01f, 0.015f, 0.40f, 0.0f, 0.005f
    );

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.012f, 0.018f, 0.38f, 0.0f, 0.006f
    );

    profile.inputTransformer = TransformerProfile::createActive(
        0.8f, 0.12f, 1.2f, 20000.0f, 15.0f,
        0.006f, 0.01f, 0.4f
    );

    profile.outputTransformer = TransformerProfile::createActive(
        0.85f, 0.08f, 1.15f, 22000.0f, 12.0f,
        0.005f, 0.008f, 0.42f
    );

    profile.noiseFloor = -78.0f;
    profile.headroom = 24.0f;

    return profile;
}

// Digital (transparent)
inline HardwareUnitProfile createDigital()
{
    HardwareUnitProfile profile;
    profile.name = "Digital";
    profile.modeledUnit = "Transparent Digital";

    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    profile.timing = TimingProfile::create(
        0.01f, 500.0f, 1.0f, 5000.0f, 0.5f, 0.5f, false
    );

    profile.noiseFloor = -120.0f;
    profile.headroom = 30.0f;

    return profile;
}

} // namespace Profiles

class HardwareProfileLibrary
{
public:
    static const HardwareUnitProfile& getOptoCompressor()
    {
        static const HardwareUnitProfile profile = Profiles::createOptoCompressor();
        return profile;
    }

    static const HardwareUnitProfile& getFETCompressor()
    {
        static const HardwareUnitProfile profile = Profiles::createFETCompressor();
        return profile;
    }

    static const HardwareUnitProfile& getClassicVCA()
    {
        static const HardwareUnitProfile profile = Profiles::createClassicVCA();
        return profile;
    }

    static const HardwareUnitProfile& getConsoleBus()
    {
        static const HardwareUnitProfile profile = Profiles::createConsoleBus();
        return profile;
    }

    static const HardwareUnitProfile& getBritishConsole()
    {
        static const HardwareUnitProfile profile = Profiles::createBritishConsole();
        return profile;
    }

    static const HardwareUnitProfile& getAmericanConsole()
    {
        static const HardwareUnitProfile profile = Profiles::createAmericanConsole();
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
