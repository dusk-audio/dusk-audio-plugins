// FourKEQParams.hpp — parameter ids, choice labels and factory presets shared
// by the 4K EQ 2 DPF shell and its ImGui UI. Names / ranges / defaults mirror
// the JUCE FourKEQ::createParameterLayout exactly.

#pragma once

enum ParamId
{
    kHpfFreq = 0, kHpfEnabled,
    kLpfFreq, kLpfEnabled,
    kLfGain, kLfFreq, kLfBell,
    kLmGain, kLmFreq, kLmQ,
    kHmGain, kHmFreq, kHmQ,
    kHfGain, kHfFreq, kHfBell,
    kEqType,        // 0 = Brown (E-series), 1 = Black (G-series)
    kBypass,        // host-designated
    kInputGain, kOutputGain, kSaturation,
    kOversampling,  // 0 = 2x, 1 = 4x
    kMsMode,
    kSpectrumPrePost, // 0 = post-EQ, 1 = pre-EQ (UI analyzer source)
    kAutoGain,
    kNumInputParams,
    // output params (meters) — also read directly via the same-process bridge
    kOutPeakL = kNumInputParams,
    kOutPeakR,
    kParamCount
};

static constexpr const char* kEqTypeLabels[2]      = { "Brown", "Black" };
static constexpr const char* kOversampleLabels[2]  = { "2x", "4x" };

// Factory presets: same values as the JUCE FourKEQPresets. A preset sets the
// tone controls; HPF/LPF are auto-enabled when their frequency departs from the
// neutral 20 Hz / 20 kHz endpoints (JUCE left the enable toggles untouched,
// which made e.g. "Telephone EQ" inert — v2 enables them so presets sound).
struct FourKEQPreset
{
    const char* name;
    const char* category;
    float lfGain, lfFreq, lfBell;
    float lmGain, lmFreq, lmQ;
    float hmGain, hmFreq, hmQ;
    float hfGain, hfFreq, hfBell;
    float hpfFreq, lpfFreq;
    float saturation, outputGain, inputGain, eqType;
};

static constexpr FourKEQPreset kFactoryPresets[] =
{
    { "Vocal Presence", "Vocals",
      3.0f,100.f,0.f, -3.0f,300.f,1.3f, 4.0f,3500.f,0.7f, 2.0f,8000.f,0.f, 80.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Kick Punch", "Drums",
      4.0f,50.f,0.f, -2.5f,200.f,0.8f, 3.0f,2000.f,1.5f, 0.0f,8000.f,0.f, 30.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Snare Crack", "Drums",
      0.0f,100.f,0.f, 4.0f,250.f,0.7f, 5.0f,5000.f,1.2f, 3.0f,8000.f,1.f, 150.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Drum Bus Punch", "Drums",
      4.0f,70.f,0.f, -3.0f,350.f,0.6f, 3.0f,3500.f,1.0f, 2.5f,10000.f,0.f, 20.f,20000.f, 25.f,0.f,0.f,1.f },
    { "Bass Warmth", "Bass",
      4.0f,80.f,0.f, -3.0f,400.f,0.7f, 2.0f,1500.f,0.7f, 0.0f,8000.f,0.f, 20.f,10000.f, 0.f,0.f,0.f,0.f },
    { "Bass Guitar Polish", "Bass",
      5.0f,60.f,0.f, -2.0f,250.f,1.0f, 3.0f,1200.f,0.8f, 2.0f,4500.f,1.f, 35.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Acoustic Guitar", "Guitar",
      -2.0f,100.f,0.f, 2.0f,200.f,0.7f, 3.0f,2500.f,0.9f, 4.0f,12000.f,0.f, 80.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Piano Brilliance", "Keys",
      2.0f,80.f,0.f, -2.5f,500.f,0.8f, 3.0f,2000.f,0.7f, 3.5f,8000.f,0.f, 30.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Bright Mix", "Mix Bus",
      2.0f,60.f,0.f, 0.0f,600.f,0.7f, -2.0f,2500.f,0.8f, 2.5f,10000.f,0.f, 20.f,20000.f, 20.f,0.f,0.f,0.f },
    { "Glue Bus", "Mix Bus",
      2.0f,100.f,0.f, 0.0f,600.f,0.7f, -1.5f,3000.f,0.7f, 2.0f,10000.f,0.f, 20.f,20000.f, 20.f,0.f,0.f,0.f },
    { "Telephone EQ", "Creative",
      0.0f,100.f,0.f, 6.0f,1000.f,1.5f, 0.0f,2000.f,0.7f, 0.0f,8000.f,0.f, 300.f,3000.f, 0.f,0.f,0.f,0.f },
    { "Air & Silk", "Creative",
      0.0f,100.f,0.f, 0.0f,600.f,0.7f, 3.0f,7000.f,0.7f, 4.0f,15000.f,0.f, 20.f,20000.f, 0.f,0.f,0.f,0.f },
    { "Master Sheen", "Mastering",
      0.0f,100.f,0.f, 0.0f,600.f,0.7f, 1.0f,5000.f,0.7f, 1.5f,16000.f,0.f, 20.f,20000.f, 10.f,0.f,0.f,0.f },
    { "Master Bus Sweetening", "Mastering",
      1.0f,50.f,0.f, -1.0f,600.f,0.5f, 0.5f,4000.f,0.6f, 1.5f,15000.f,0.f, 20.f,20000.f, 15.f,-0.5f,0.f,0.f },
};
static constexpr int kNumFactoryPresets = (int)(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));
