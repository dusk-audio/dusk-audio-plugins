#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================
// Band type definitions for Multi-Q 8-band parametric EQ
//==============================================================================

enum class BandType
{
    HighPass = 0,    // Band 1: Variable slope HPF
    LowShelf,        // Band 2: Low shelf with Q
    Parametric,      // Bands 3-6: Peaking EQ
    HighShelf,       // Band 7: High shelf with Q
    LowPass,         // Band 8: Variable slope LPF
    Notch,           // Bands 3-6: Narrow rejection (Q-only, no gain)
    BandPass         // Bands 3-6: Bandpass filter (Q-only, no gain)
};

// Filter slope options for HPF/LPF (dB/octave)
enum class FilterSlope
{
    Slope6dB = 0,   // 1st order
    Slope12dB,      // 2nd order
    Slope18dB,      // 3rd order
    Slope24dB,      // 4th order
    Slope36dB,      // 6th order
    Slope48dB,      // 8th order
    Slope72dB,      // 12th order
    Slope96dB       // 16th order
};

// Butterworth Q values for cascaded 2nd-order stages
// Each array contains the Q per stage for the even-order portion of the filter
// Odd-order filters (6, 18 dB/oct) use a 1st-order stage + these values
namespace ButterworthQ
{
    // Q values for cascaded Butterworth stages (standard pole placement)
    constexpr float order2[] = { 0.7071f };                                          // 12 dB/oct
    constexpr float order4[] = { 0.5412f, 1.3066f };                                 // 24 dB/oct
    constexpr float order6[] = { 0.5176f, 0.7071f, 1.9319f };                        // 36 dB/oct
    constexpr float order8[] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };               // 48 dB/oct
    constexpr float order12[] = { 0.5024f, 0.5412f, 0.6313f, 0.7071f, 1.0000f, 1.9319f }; // 72 dB/oct
    constexpr float order16[] = { 0.5006f, 0.5176f, 0.5612f, 0.6013f, 0.7071f, 0.9000f, 1.3066f, 2.5629f }; // 96 dB/oct

    // Get Butterworth Q for a given stage within a cascaded filter
    // Returns the ideal Q for maximally-flat passband response
    // userQ scales the result: 0.707 = flat Butterworth, higher = resonant peak
    inline float getStageQ(int totalSecondOrderStages, int stageIndex, float userQ)
    {
        const float* qValues = nullptr;
        switch (totalSecondOrderStages)
        {
            case 1: qValues = order2; break;
            case 2: qValues = order4; break;
            case 3: qValues = order6; break;
            case 4: qValues = order8; break;
            case 6: qValues = order12; break;
            case 8: qValues = order16; break;
            default: return userQ;  // Fallback for unexpected values
        }

        if (stageIndex < 0 || stageIndex >= totalSecondOrderStages)
            return userQ;

        // Scale Butterworth Q by user's Q relative to the default (0.7071)
        // When userQ == 0.7071, returns the exact Butterworth Q (maximally flat)
        // When userQ > 0.7071, adds resonance proportionally
        float butterworthQ = qValues[stageIndex];
        float qScale = userQ / 0.7071f;
        return butterworthQ * qScale;
    }
}

// Q-Coupling mode for automatic Q adjustment based on gain
enum class QCoupleMode
{
    Off = 0,
    Proportional,       // Scales bandwidth proportionally
    Light,              // Subtle Q adjustment
    Medium,             // Moderate Q adjustment
    Strong,             // Preserves most of perceived bandwidth
    AsymmetricLight,    // Stronger coupling for cuts
    AsymmetricMedium,
    AsymmetricStrong
};

// Analyzer display modes
enum class AnalyzerMode
{
    Peak = 0,
    RMS
};

// Analyzer FFT resolution
enum class AnalyzerResolution
{
    Low = 2048,     // Faster, less detail
    Medium = 4096,  // Default, good balance
    High = 8192     // Maximum detail, more CPU
};

// Display scale mode for EQ graphic
enum class DisplayScaleMode
{
    Linear12dB = 0,   // ±12 dB range
    Linear24dB,       // ±24 dB range (matches gain range)
    Linear30dB,       // ±30 dB range
    Linear60dB,       // ±60 dB range
    Warped            // Logarithmic/non-linear scale
};

// Processing mode
enum class ProcessingMode
{
    Stereo = 0,
    Left,
    Right,
    Mid,
    Side
};

// EQ Type - switches between different EQ algorithms/styles
enum class EQType
{
    Digital = 0,   // Clean digital EQ with optional per-band dynamics (Multi-Q default)
    British,       // 4K EQ style British console EQ
    Tube           // Pultec EQP-1A style tube EQ
};

//==============================================================================
// Band configuration structure
//==============================================================================

struct BandConfig
{
    BandType type;
    juce::Colour color;
    float defaultFreq;
    float minFreq;
    float maxFreq;
    const char* name;
};

// Band colors - vibrant palette for high visibility (Pro-Q style)
namespace BandColors
{
    const juce::Colour Band1_HPF      = juce::Colour(0xFFff5555);  // Red
    const juce::Colour Band2_LowShelf = juce::Colour(0xFFffaa00);  // Orange
    const juce::Colour Band3_Para     = juce::Colour(0xFFffee00);  // Yellow
    const juce::Colour Band4_Para     = juce::Colour(0xFF88ee44);  // Lime
    const juce::Colour Band5_Para     = juce::Colour(0xFF00ccff);  // Cyan
    const juce::Colour Band6_Para     = juce::Colour(0xFF5588ff);  // Blue
    const juce::Colour Band7_HighShelf = juce::Colour(0xFFaa66ff);  // Purple
    const juce::Colour Band8_LPF      = juce::Colour(0xFFff66cc);  // Pink
}

// Default band configurations
inline const std::array<BandConfig, 8> DefaultBandConfigs = {{
    { BandType::HighPass,   BandColors::Band1_HPF,       20.0f,    20.0f, 20000.0f, "HPF" },
    { BandType::LowShelf,   BandColors::Band2_LowShelf, 100.0f,    20.0f, 20000.0f, "Low Shelf" },
    { BandType::Parametric, BandColors::Band3_Para,     200.0f,    20.0f, 20000.0f, "Para 1" },
    { BandType::Parametric, BandColors::Band4_Para,     500.0f,    20.0f, 20000.0f, "Para 2" },
    { BandType::Parametric, BandColors::Band5_Para,    1000.0f,    20.0f, 20000.0f, "Para 3" },
    { BandType::Parametric, BandColors::Band6_Para,    2000.0f,    20.0f, 20000.0f, "Para 4" },
    { BandType::HighShelf,  BandColors::Band7_HighShelf, 4000.0f,   20.0f, 20000.0f, "High Shelf" },
    { BandType::LowPass,    BandColors::Band8_LPF,    20000.0f,    20.0f, 20000.0f, "LPF" }
}};

//==============================================================================
// Q-Coupling utility function
//==============================================================================

inline float getQCoupledValue(float baseQ, float gainDB, QCoupleMode mode)
{
    if (mode == QCoupleMode::Off)
        return baseQ;

    float absGain = std::abs(gainDB);
    float strength = 0.0f;
    bool asymmetric = false;

    switch (mode)
    {
        case QCoupleMode::Off:
            return baseQ;
        case QCoupleMode::Proportional:
            strength = 0.15f;
            break;
        case QCoupleMode::Light:
            strength = 0.05f;
            break;
        case QCoupleMode::Medium:
            strength = 0.10f;
            break;
        case QCoupleMode::Strong:
            strength = 0.20f;
            break;
        case QCoupleMode::AsymmetricLight:
            strength = 0.05f;
            asymmetric = true;
            break;
        case QCoupleMode::AsymmetricMedium:
            strength = 0.10f;
            asymmetric = true;
            break;
        case QCoupleMode::AsymmetricStrong:
            strength = 0.20f;
            asymmetric = true;
            break;
    }

    // Asymmetric: stronger coupling for cuts (negative gain)
    if (asymmetric && gainDB < 0)
        strength *= 1.5f;

    return baseQ * (1.0f + strength * absGain);
}

//==============================================================================
// Parameter ID helpers
//==============================================================================

namespace ParamIDs
{
    // Band parameters (N = 1-8)
    inline juce::String bandEnabled(int bandNum) { return "band" + juce::String(bandNum) + "_enabled"; }
    inline juce::String bandFreq(int bandNum) { return "band" + juce::String(bandNum) + "_freq"; }
    inline juce::String bandGain(int bandNum) { return "band" + juce::String(bandNum) + "_gain"; }
    inline juce::String bandQ(int bandNum) { return "band" + juce::String(bandNum) + "_q"; }
    inline juce::String bandSlope(int bandNum) { return "band" + juce::String(bandNum) + "_slope"; }

    // Global parameters
    const juce::String masterGain = "master_gain";
    const juce::String bypass = "bypass";
    const juce::String hqEnabled = "hq_enabled";
    const juce::String linearPhaseEnabled = "linear_phase_enabled";
    const juce::String linearPhaseLength = "linear_phase_length";  // 0=Short, 1=Medium, 2=Long
    const juce::String processingMode = "processing_mode";
    const juce::String qCoupleMode = "q_couple_mode";
    const juce::String eqType = "eq_type";

    // Analyzer parameters
    const juce::String analyzerEnabled = "analyzer_enabled";
    const juce::String analyzerPrePost = "analyzer_pre_post";  // 0=post, 1=pre
    const juce::String analyzerMode = "analyzer_mode";         // 0=peak, 1=rms
    const juce::String analyzerResolution = "analyzer_resolution";
    const juce::String analyzerSmoothing = "analyzer_smoothing";  // 0=off, 1=light, 2=medium, 3=heavy
    const juce::String analyzerDecay = "analyzer_decay";

    // Display parameters
    const juce::String displayScaleMode = "display_scale_mode";
    const juce::String visualizeMasterGain = "visualize_master_gain";

    // British mode (4K-EQ style) parameters
    const juce::String britishHpfFreq = "british_hpf_freq";
    const juce::String britishHpfEnabled = "british_hpf_enabled";
    const juce::String britishLpfFreq = "british_lpf_freq";
    const juce::String britishLpfEnabled = "british_lpf_enabled";
    const juce::String britishLfGain = "british_lf_gain";
    const juce::String britishLfFreq = "british_lf_freq";
    const juce::String britishLfBell = "british_lf_bell";
    const juce::String britishLmGain = "british_lm_gain";
    const juce::String britishLmFreq = "british_lm_freq";
    const juce::String britishLmQ = "british_lm_q";
    const juce::String britishHmGain = "british_hm_gain";
    const juce::String britishHmFreq = "british_hm_freq";
    const juce::String britishHmQ = "british_hm_q";
    const juce::String britishHfGain = "british_hf_gain";
    const juce::String britishHfFreq = "british_hf_freq";
    const juce::String britishHfBell = "british_hf_bell";
    const juce::String britishMode = "british_mode";  // 0=Brown, 1=Black
    const juce::String britishSaturation = "british_saturation";
    const juce::String britishInputGain = "british_input_gain";
    const juce::String britishOutputGain = "british_output_gain";

    // Pultec (Tube) mode parameters
    const juce::String pultecLfBoostGain = "pultec_lf_boost_gain";
    const juce::String pultecLfBoostFreq = "pultec_lf_boost_freq";
    const juce::String pultecLfAttenGain = "pultec_lf_atten_gain";
    const juce::String pultecHfBoostGain = "pultec_hf_boost_gain";
    const juce::String pultecHfBoostFreq = "pultec_hf_boost_freq";
    const juce::String pultecHfBoostBandwidth = "pultec_hf_boost_bw";
    const juce::String pultecHfAttenGain = "pultec_hf_atten_gain";
    const juce::String pultecHfAttenFreq = "pultec_hf_atten_freq";
    const juce::String pultecInputGain = "pultec_input_gain";
    const juce::String pultecOutputGain = "pultec_output_gain";
    const juce::String pultecTubeDrive = "pultec_tube_drive";

    // Pultec Mid Dip/Peak section parameters
    const juce::String pultecMidEnabled = "pultec_mid_enabled";
    const juce::String pultecMidLowFreq = "pultec_mid_low_freq";
    const juce::String pultecMidLowPeak = "pultec_mid_low_peak";
    const juce::String pultecMidDipFreq = "pultec_mid_dip_freq";
    const juce::String pultecMidDip = "pultec_mid_dip";
    const juce::String pultecMidHighFreq = "pultec_mid_high_freq";
    const juce::String pultecMidHighPeak = "pultec_mid_high_peak";

    // Dynamic mode per-band parameters (N = 1-8)
    inline juce::String bandDynEnabled(int bandNum) { return "band" + juce::String(bandNum) + "_dyn_enabled"; }
    inline juce::String bandDynThreshold(int bandNum) { return "band" + juce::String(bandNum) + "_dyn_threshold"; }
    inline juce::String bandDynAttack(int bandNum) { return "band" + juce::String(bandNum) + "_dyn_attack"; }
    inline juce::String bandDynRelease(int bandNum) { return "band" + juce::String(bandNum) + "_dyn_release"; }
    inline juce::String bandDynRange(int bandNum) { return "band" + juce::String(bandNum) + "_dyn_range"; }
    inline juce::String bandDynRatio(int bandNum) { return "band" + juce::String(bandNum) + "_dyn_ratio"; }

    // Band shape (for parametric bands 3-6 only): 0=Peaking, 1=Notch, 2=BandPass
    inline juce::String bandShape(int bandNum) { return "band" + juce::String(bandNum) + "_shape"; }

    // Per-band channel routing: 0=Global, 1=Stereo, 2=Left, 3=Right, 4=Mid, 5=Side
    inline juce::String bandChannelRouting(int bandNum) { return "band" + juce::String(bandNum) + "_routing"; }

    // Global dynamic mode parameters
    const juce::String dynDetectionMode = "dyn_detection_mode";  // 0=Peak, 1=RMS

    // Auto-gain compensation
    const juce::String autoGainEnabled = "auto_gain_enabled";
}
