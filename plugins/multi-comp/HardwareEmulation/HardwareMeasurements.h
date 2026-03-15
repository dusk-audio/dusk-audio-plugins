// HardwareMeasurements.h — Hardware profiles for compressor emulation

#pragma once

#include <array>
#include <cmath>

namespace HardwareEmulation {

struct HarmonicProfile
{
    float h2 = 0.0f;
    float h3 = 0.0f;
    float h4 = 0.0f;
    float h5 = 0.0f;
    float h6 = 0.0f;
    float h7 = 0.0f;
    float evenOddRatio = 0.5f; // 0=all odd, 1=all even

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
    float highFreqRolloff = 20000.0f;  // -3dB Hz, 0=disabled
    float dcBlockingFreq = 10.0f;
    float hysteresisAmount = 0.0f;    // Magnetic hysteresis (0=none, 0.02=subtle iron saturation)
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

struct HardwareUnitProfile
{
    const char* name = nullptr;
    const char* modeledUnit = nullptr;

    HarmonicProfile inputStageHarmonics;
    HarmonicProfile compressionStageHarmonics;
    HarmonicProfile outputStageHarmonics;

    TransformerProfile inputTransformer;
    TransformerProfile outputTransformer;

    FrequencyResponse preCompressionEQ;
    FrequencyResponse postCompressionEQ;

    TimingProfile timing;

    float noiseFloor = -90.0f;         // dBFS
    float headroom = 20.0f;            // dB above 0VU
    float intermodulationDistortion = 0.0f;
};

namespace Profiles {

//------------------------------------------------------------------------------
// Opto compressor (LA-2A style)
inline HardwareUnitProfile createOptoCompressor()
{
    HardwareUnitProfile profile;
    profile.name = "Opto Compressor";
    profile.modeledUnit = "Vintage Opto Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.025f, 0.008f, 0.75f, 0.003f, 0.001f);

    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.015f, 0.003f, 0.85f);

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.035f, 0.012f, 0.70f, 0.004f);

    // Input transformer (UTC A-10) — H2-dominant, iron-core
    profile.inputTransformer = TransformerProfile::createActive(
        0.75f,    // saturationThreshold
        0.06f,    // saturationAmount
        1.05f,    // lowFreqSaturation — gentle LF boost (was 1.15, too much on kicks)
        0.0f,     // HF rolloff disabled (convolution IR handles UTC A-10 rolloff)
        2.0f,     // dcBlockingFreq
        0.012f, 0.004f, 0.75f  // h2, h3, balance — H2 dominant, moderate level
    );

    // Output transformer (UTC A-24) — H2-dominant, lighter than input.
    profile.outputTransformer = TransformerProfile::createActive(
        0.8f,     // saturationThreshold
        0.04f,    // saturationAmount
        1.1f,     // lowFreqSaturation
        0.0f,     // HF rolloff disabled (convolution IR handles UTC A-24 rolloff)
        2.0f,     // dcBlockingFreq
        0.012f, 0.004f, 0.78f  // h2, h3, balance — H2 dominant (even = warm)
    );

    profile.inputTransformer.hysteresisAmount = 0.02f;   // UTC iron-core transformer
    profile.outputTransformer.hysteresisAmount = 0.02f;

    profile.timing = TimingProfile::create(
        10.0f, 10.0f, 60.0f, 5000.0f, 0.3f, 0.8f, true);

    profile.noiseFloor = -70.0f;
    profile.headroom = 18.0f;

    return profile;
}

//------------------------------------------------------------------------------
// FET compressor (1176 style)
inline HardwareUnitProfile createFETCompressor()
{
    HardwareUnitProfile profile;
    profile.name = "FET Compressor";
    profile.modeledUnit = "Vintage FET Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.008f, 0.015f, 0.35f, 0.002f, 0.005f);

    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.012f, 0.025f, 0.30f, 0.0f, 0.008f);

    profile.outputStageHarmonics = HarmonicProfile::create(
        0.006f, 0.010f, 0.40f, 0.0f, 0.003f);

    profile.inputTransformer = TransformerProfile::createActive(
        0.85f, 0.08f, 1.0f, 0.0f, 15.0f,
        0.004f, 0.002f, 0.65f);

    profile.outputTransformer = TransformerProfile::createActive(
        0.9f, 0.05f, 1.0f, 0.0f, 12.0f,
        0.003f, 0.002f, 0.6f);

    profile.inputTransformer.hysteresisAmount = 0.015f;  // Cinemag/Jensen transformer
    profile.outputTransformer.hysteresisAmount = 0.015f;

    profile.timing = TimingProfile::create(
        0.02f, 0.8f, 50.0f, 1100.0f, 0.1f, 0.6f, true);

    profile.noiseFloor = -80.0f;
    profile.headroom = 24.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Classic VCA compressor (dbx 160 style)
inline HardwareUnitProfile createClassicVCA()
{
    HardwareUnitProfile profile;
    profile.name = "Classic VCA";
    profile.modeledUnit = "Classic VCA Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(0.003f, 0.002f, 0.55f);
    profile.compressionStageHarmonics = HarmonicProfile::create(0.0075f, 0.005f, 0.60f);
    profile.outputStageHarmonics = HarmonicProfile::create(0.002f, 0.001f, 0.65f);

    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    profile.timing = TimingProfile::create(
        3.0f, 15.0f, 100.0f, 1000.0f, 0.5f, 0.5f, true);

    profile.noiseFloor = -85.0f;
    profile.headroom = 21.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Console Bus compressor (SSL style)
inline HardwareUnitProfile createConsoleBus()
{
    HardwareUnitProfile profile;
    profile.name = "Console Bus";
    profile.modeledUnit = "Console Bus Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(
        0.004f, 0.008f, 0.35f, 0.0f, 0.003f);
    profile.compressionStageHarmonics = HarmonicProfile::create(
        0.006f, 0.012f, 0.40f, 0.0f, 0.004f);
    profile.outputStageHarmonics = HarmonicProfile::create(
        0.008f, 0.015f, 0.35f, 0.0f, 0.004f);

    // SSL G-Bus: clean VCA, flat response
    profile.inputTransformer = TransformerProfile::createActive(
        0.9f, 0.03f, 1.0f, 100000.0f, 10.0f,
        0.002f, 0.004f, 0.4f);

    profile.outputTransformer = TransformerProfile::createActive(
        0.92f, 0.02f, 1.0f, 100000.0f, 8.0f,
        0.002f, 0.003f, 0.45f);

    profile.inputTransformer.hysteresisAmount = 0.008f;  // SSL active transformer (minimal)
    profile.outputTransformer.hysteresisAmount = 0.008f;

    profile.timing = TimingProfile::create(
        0.1f, 30.0f, 100.0f, 1200.0f, 0.2f, 0.5f, false);

    profile.noiseFloor = -88.0f;
    profile.headroom = 22.0f;

    return profile;
}

//------------------------------------------------------------------------------
// Studio FET (cleaner FET variant, 30% harmonic content)
inline HardwareUnitProfile createStudioFET()
{
    HardwareUnitProfile profile = createFETCompressor();
    profile.name = "Studio FET";
    profile.modeledUnit = "Clean FET Compressor";

    auto scale = [](HarmonicProfile& hp, float factor) {
        hp.h2 *= factor; hp.h3 *= factor; hp.h4 *= factor;
        hp.h5 *= factor; hp.h6 *= factor; hp.h7 *= factor;
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
// Studio VCA (modern, minimal coloration)
inline HardwareUnitProfile createStudioVCA()
{
    HardwareUnitProfile profile;
    profile.name = "Studio VCA";
    profile.modeledUnit = "Modern VCA Compressor";

    profile.inputStageHarmonics = HarmonicProfile::create(0.001f, 0.0005f, 0.6f);
    profile.compressionStageHarmonics = HarmonicProfile::create(0.002f, 0.0015f, 0.55f);
    profile.outputStageHarmonics = HarmonicProfile::create(0.001f, 0.0005f, 0.6f);

    // Subtle modern transformer coloration (API 2500 / Neve 33609 style)
    profile.inputTransformer = TransformerProfile::createActive(
        0.95f, 0.01f, 1.0f, 0.0f, 10.0f,
        0.001f, 0.001f, 0.55f);

    profile.outputTransformer = TransformerProfile::createActive(
        0.92f, 0.015f, 1.0f, 0.0f, 8.0f,
        0.001f, 0.001f, 0.55f);

    profile.timing = TimingProfile::create(
        0.3f, 75.0f, 50.0f, 3000.0f, 0.4f, 0.5f, false);

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

    profile.inputTransformer = TransformerProfile::createInactive();
    profile.outputTransformer = TransformerProfile::createInactive();

    profile.timing = TimingProfile::create(
        0.01f, 500.0f, 1.0f, 5000.0f, 0.5f, 0.5f, false);

    profile.noiseFloor = -120.0f;
    profile.headroom = 30.0f;
    return profile;
}

} // namespace Profiles

//------------------------------------------------------------------------------
class HardwareProfiles
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
