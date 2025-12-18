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
    LowPass          // Band 8: Variable slope LPF
};

// Filter slope options for HPF/LPF (dB/octave)
enum class FilterSlope
{
    Slope6dB = 0,   // 1st order
    Slope12dB,      // 2nd order
    Slope18dB,      // 3rd order
    Slope24dB,      // 4th order
    Slope36dB,      // 6th order
    Slope48dB       // 8th order
};

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
    Digital = 0,   // Clean digital EQ (Multi-Q default)
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

// Band colors matching Logic Pro Channel EQ
namespace BandColors
{
    const juce::Colour Band1_HPF     = juce::Colour(0xFFff4444);  // Red
    const juce::Colour Band2_LowShelf = juce::Colour(0xFFff8844);  // Orange
    const juce::Colour Band3_Para    = juce::Colour(0xFFffcc44);  // Yellow
    const juce::Colour Band4_Para    = juce::Colour(0xFF44cc44);  // Green
    const juce::Colour Band5_Para    = juce::Colour(0xFF44cccc);  // Aqua/Cyan
    const juce::Colour Band6_Para    = juce::Colour(0xFF4488ff);  // Blue
    const juce::Colour Band7_HighShelf = juce::Colour(0xFFaa44ff);  // Purple
    const juce::Colour Band8_LPF     = juce::Colour(0xFFff44aa);  // Pink
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
    const juce::String processingMode = "processing_mode";
    const juce::String qCoupleMode = "q_couple_mode";
    const juce::String eqType = "eq_type";

    // Analyzer parameters
    const juce::String analyzerEnabled = "analyzer_enabled";
    const juce::String analyzerPrePost = "analyzer_pre_post";  // 0=post, 1=pre
    const juce::String analyzerMode = "analyzer_mode";         // 0=peak, 1=rms
    const juce::String analyzerResolution = "analyzer_resolution";
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
}
