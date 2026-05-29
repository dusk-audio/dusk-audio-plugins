#pragma once

namespace DuskAmpParams
{
    static constexpr const char* AMP_MODE        = "amp_mode";
    static constexpr const char* INPUT_GAIN      = "input_gain";       // DSP path
    static constexpr const char* GATE_THRESHOLD  = "gate_threshold";   // DSP path
    static constexpr const char* GATE_RELEASE    = "gate_release";     // DSP path
    static constexpr const char* INPUT_GAIN_NAM     = "input_gain_nam";
    static constexpr const char* GATE_THRESHOLD_NAM = "gate_threshold_nam";
    static constexpr const char* GATE_RELEASE_NAM   = "gate_release_nam";
    static constexpr const char* PREAMP_GAIN     = "preamp_gain";
    static constexpr const char* PREAMP_CHANNEL  = "preamp_channel";
    static constexpr const char* PREAMP_BRIGHT   = "preamp_bright";
    static constexpr const char* TONE_TYPE       = "tone_type";    // DSP path
    static constexpr const char* BASS            = "bass";         // DSP path
    static constexpr const char* MID             = "mid";          // DSP path
    static constexpr const char* TREBLE          = "treble";       // DSP path
    static constexpr const char* TONE_TYPE_NAM   = "tone_type_nam"; // NAM path
    static constexpr const char* BASS_NAM        = "bass_nam";      // NAM path
    static constexpr const char* MID_NAM         = "mid_nam";       // NAM path
    static constexpr const char* TREBLE_NAM      = "treble_nam";    // NAM path
    static constexpr const char* POWER_DRIVE     = "power_drive";
    static constexpr const char* PRESENCE        = "presence";
    static constexpr const char* RESONANCE       = "resonance";
    static constexpr const char* SAG             = "sag";
    static constexpr const char* CAB_ENABLED     = "cab_enabled";
    static constexpr const char* CAB_MIX         = "cab_mix";
    static constexpr const char* CAB_HICUT       = "cab_hicut";
    static constexpr const char* CAB_LOCUT       = "cab_locut";
    static constexpr const char* CAB_NORMALIZE   = "cab_normalize";
    static constexpr const char* DELAY_ENABLED   = "delay_enabled";
    static constexpr const char* DELAY_TIME      = "delay_time";
    static constexpr const char* DELAY_FEEDBACK  = "delay_feedback";
    static constexpr const char* DELAY_MIX       = "delay_mix";
    static constexpr const char* REVERB_ENABLED  = "reverb_enabled";
    static constexpr const char* REVERB_MIX      = "reverb_mix";
    static constexpr const char* REVERB_DECAY    = "reverb_decay";
    static constexpr const char* OUTPUT_LEVEL    = "output_level";       // DSP path
    static constexpr const char* OUTPUT_LEVEL_NAM = "output_level_nam";
    static constexpr const char* OVERSAMPLING    = "oversampling";
    static constexpr const char* BYPASS          = "bypass";
    static constexpr const char* TUNER_REF_HZ    = "tuner_ref_hz"; // A4 reference, 415–466 Hz, default 440
}
