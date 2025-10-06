/*
  ==============================================================================

    HardwarePresets.h - Famous hardware saturation emulations

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

class HardwareSaturation
{
public:
    enum class Mode
    {
        // Tape Machines
        StuderA800,      // Warm, musical tape saturation
        AmpexATR102,     // Classic mastering tape
        TascamPorta,     // Lo-fi cassette character

        // Tubes
        FairchildTube,   // 670 compressor tube stage
        PultecEQP1A,     // EQP-1A tube warmth
        UA610,           // Universal Audio 610 preamp

        // Transistors
        Neve1073,        // Class A transistor saturation
        API2500,         // VCA saturation
        SSL4000E,        // SSL channel strip harmonics

        // Special
        CultureVulture,  // Thermionic Culture Vulture
        Decapitator,     // Soundtoys style saturation
        BlackBox         // Analog Devices HG-2 Black Box
    };

    HardwareSaturation();

    void prepare(double sampleRate);
    void reset();

    void setMode(Mode mode);
    Mode getMode() const { return currentMode; }

    // Main controls
    void setDrive(float amount);      // 0-100%
    void setMix(float wetDry);        // 0-100%
    void setOutput(float gain);       // -12 to +12 dB
    void setTone(float brightness);   // -100 to +100 (dark to bright)

    // Process audio
    float processSample(float input, int channel = 0);

    // Get preset info
    static juce::String getModeName(Mode mode);
    static juce::String getModeDescription(Mode mode);

private:
    // Harmonic profiles for each mode
    struct HarmonicProfile
    {
        float h2 = 0.0f;  // 2nd harmonic (even, warm)
        float h3 = 0.0f;  // 3rd harmonic (odd, aggressive)
        float h4 = 0.0f;  // 4th harmonic
        float h5 = 0.0f;  // 5th harmonic
        float evenOddRatio = 0.5f;  // Balance between even/odd
        float saturationCurve = 0.5f;  // 0=soft, 1=hard
        float lowFreqEmphasis = 0.0f;  // Bass harmonic enhancement
        float highFreqRolloff = 20000.0f;  // High frequency damping
        bool asymmetric = false;  // Asymmetric clipping
        float compressionAmount = 0.0f;  // Soft compression
    };

    Mode currentMode = Mode::StuderA800;
    mutable Mode cachedMode = Mode::StuderA800;
    mutable HarmonicProfile cachedProfile;

    // Parameters
    float drive = 0.5f;
    float mix = 1.0f;
    float output = 1.0f;
    float tone = 0.0f;

    HarmonicProfile getProfileForMode(Mode mode) const;
    const HarmonicProfile& getCachedProfile() const;

    // Processing components
    class TapeEmulation
    {
    public:
        void prepare(double sampleRate);
        void reset();
        float process(float input, const HarmonicProfile& profile);

    private:
        // Hysteresis modeling
        float hysteresisState = 0.0f;
        float previousInput = 0.0f;

        // Bias
        float biasAmount = 0.5f;

        // Lowpass filter state for high frequency rolloff
        float lpState = 0.0f;
        double sampleRate = 48000.0;

        juce::dsp::IIR::Filter<float> preEmphasis;
        juce::dsp::IIR::Filter<float> deEmphasis;
    };

    class TubeEmulation
    {
    public:
        void prepare(double sampleRate);
        void reset();
        float process(float input, const HarmonicProfile& profile);

    private:
        // Tube modeling
        float processTriode(float input);
        float processPentode(float input);

        // Miller capacitance
        float millerCapState = 0.0f;

        // Grid current
        float gridCurrent = 0.0f;
    };

    class TransistorEmulation
    {
    public:
        void prepare(double sampleRate);
        void reset();
        float process(float input, const HarmonicProfile& profile);

    private:
        // Class A/B crossover distortion
        float crossoverDistortion = 0.001f;

        // Slew rate limiting
        float slewRateLimit = 10.0f;  // V/μs
        float previousOutput = 0.0f;
        double sampleRate = 48000.0;
    };

    TapeEmulation tapeEmulation;
    TubeEmulation tubeEmulation;
    TransistorEmulation transistorEmulation;

    // Tone control
    juce::dsp::StateVariableTPTFilter<float> toneFilter;

    // Oversampling for quality
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    double sampleRate = 44100.0;

    // Saturation curves
    float applySaturation(float input, const HarmonicProfile& profile);
    float softClip(float input, float threshold);
    float hardClip(float input, float threshold);
    float tanhSaturation(float input, float amount);
    float asymmetricSaturation(float input, float amount);
};

//==============================================================================
// Preset descriptions and characteristics
//==============================================================================
inline juce::String HardwareSaturation::getModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::StuderA800:     return "Studer A800";
        case Mode::AmpexATR102:    return "Ampex ATR-102";
        case Mode::TascamPorta:    return "Tascam Porta";
        case Mode::FairchildTube:  return "Fairchild 670";
        case Mode::PultecEQP1A:    return "Pultec EQP-1A";
        case Mode::UA610:          return "UA 610";
        case Mode::Neve1073:       return "Neve 1073";
        case Mode::API2500:        return "API 2500";
        case Mode::SSL4000E:       return "SSL 4000E";
        case Mode::CultureVulture: return "Culture Vulture";
        case Mode::Decapitator:    return "Decapitator";
        case Mode::BlackBox:       return "HG-2 Black Box";
        default:                   return "Unknown";
    }
}

inline juce::String HardwareSaturation::getModeDescription(Mode mode)
{
    switch (mode)
    {
        case Mode::StuderA800:
            return "Legendary 2\" tape machine - warm, musical saturation with gentle compression";

        case Mode::AmpexATR102:
            return "Mastering tape deck - transparent with subtle harmonic enhancement";

        case Mode::TascamPorta:
            return "Cassette 4-track - lo-fi character with noise and wobble";

        case Mode::FairchildTube:
            return "Tube compressor - rich even harmonics and smooth compression";

        case Mode::PultecEQP1A:
            return "Tube EQ - warm low-end enhancement and silky highs";

        case Mode::UA610:
            return "Tube preamp - vintage warmth and presence";

        case Mode::Neve1073:
            return "Class A preamp - punchy midrange and musical saturation";

        case Mode::API2500:
            return "VCA compressor - tight, controlled harmonics";

        case Mode::SSL4000E:
            return "Console channel - aggressive, forward character";

        case Mode::CultureVulture:
            return "Tube distortion - from subtle warmth to total destruction";

        case Mode::Decapitator:
            return "Analog saturation - five flavors of analog modeling";

        case Mode::BlackBox:
            return "Tube/transformer - Pentode and triode tube stages";

        default:
            return "";
    }
}

//==============================================================================
// Harmonic profiles for each hardware unit
//==============================================================================
inline HardwareSaturation::HarmonicProfile HardwareSaturation::getProfileForMode(Mode mode) const
{
    HarmonicProfile profile;

    switch (mode)
    {
        case Mode::StuderA800:
            profile.h2 = 0.03f;
            profile.h3 = 0.01f;
            profile.h4 = 0.005f;
            profile.evenOddRatio = 0.7f;  // More even harmonics
            profile.saturationCurve = 0.3f;  // Soft saturation
            profile.lowFreqEmphasis = 0.2f;
            profile.highFreqRolloff = 15000.0f;
            profile.compressionAmount = 0.1f;
            break;

        case Mode::AmpexATR102:
            profile.h2 = 0.02f;
            profile.h3 = 0.008f;
            profile.h4 = 0.003f;
            profile.evenOddRatio = 0.75f;
            profile.saturationCurve = 0.25f;
            profile.highFreqRolloff = 18000.0f;
            profile.compressionAmount = 0.05f;
            break;

        case Mode::TascamPorta:
            profile.h2 = 0.05f;
            profile.h3 = 0.03f;
            profile.h4 = 0.02f;
            profile.h5 = 0.01f;
            profile.evenOddRatio = 0.6f;
            profile.saturationCurve = 0.5f;
            profile.highFreqRolloff = 8000.0f;  // Cassette tape HF loss (was 10kHz)
            profile.compressionAmount = 0.2f;
            break;

        case Mode::FairchildTube:
            profile.h2 = 0.04f;
            profile.h3 = 0.005f;
            profile.evenOddRatio = 0.9f;  // Mostly even harmonics
            profile.saturationCurve = 0.2f;  // Very soft
            profile.lowFreqEmphasis = 0.3f;
            profile.compressionAmount = 0.15f;
            break;

        case Mode::PultecEQP1A:
            profile.h2 = 0.025f;
            profile.h3 = 0.003f;
            profile.evenOddRatio = 0.85f;
            profile.saturationCurve = 0.15f;
            profile.lowFreqEmphasis = 0.4f;  // Famous low-end trick
            break;

        case Mode::UA610:
            profile.h2 = 0.035f;
            profile.h3 = 0.01f;
            profile.h4 = 0.005f;
            profile.evenOddRatio = 0.8f;
            profile.saturationCurve = 0.25f;
            break;

        case Mode::Neve1073:
            profile.h2 = 0.015f;
            profile.h3 = 0.025f;  // More odd harmonics
            profile.h5 = 0.008f;
            profile.evenOddRatio = 0.4f;  // Transistor character
            profile.saturationCurve = 0.6f;  // Harder clipping
            profile.asymmetric = true;
            break;

        case Mode::API2500:
            profile.h2 = 0.01f;
            profile.h3 = 0.02f;
            profile.h5 = 0.005f;
            profile.evenOddRatio = 0.35f;
            profile.saturationCurve = 0.7f;
            profile.compressionAmount = 0.25f;  // Increased for famous "glue" compression (was 0.08)
            break;

        case Mode::SSL4000E:
            profile.h2 = 0.008f;
            profile.h3 = 0.03f;
            profile.h5 = 0.01f;
            profile.evenOddRatio = 0.3f;  // Aggressive odd harmonics
            profile.saturationCurve = 0.8f;  // Hard clipping
            profile.asymmetric = true;
            break;

        case Mode::CultureVulture:
            profile.h2 = 0.08f;   // Increased for more extreme saturation (was 0.06)
            profile.h3 = 0.06f;   // Increased (was 0.04)
            profile.h4 = 0.04f;   // Increased (was 0.03)
            profile.h5 = 0.03f;   // Increased (was 0.02)
            profile.evenOddRatio = 0.6f;
            profile.saturationCurve = 0.65f;  // More aggressive (was 0.4) for "destruction"
            profile.lowFreqEmphasis = 0.3f;   // More bass emphasis (was 0.2)
            break;

        case Mode::Decapitator:
            profile.h2 = 0.04f;
            profile.h3 = 0.035f;
            profile.h4 = 0.02f;
            profile.h5 = 0.015f;
            profile.evenOddRatio = 0.5f;
            profile.saturationCurve = 0.5f;
            profile.asymmetric = true;
            break;

        case Mode::BlackBox:
            profile.h2 = 0.045f;
            profile.h3 = 0.008f;
            profile.h4 = 0.004f;
            profile.evenOddRatio = 0.82f;  // Tube warmth
            profile.saturationCurve = 0.22f;
            profile.lowFreqEmphasis = 0.25f;
            profile.compressionAmount = 0.12f;
            break;
    }

    return profile;
}