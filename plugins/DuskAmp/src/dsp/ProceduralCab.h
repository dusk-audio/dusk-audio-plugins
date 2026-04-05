#pragma once

#include "AmpModels.h"
#include <cmath>
#include <algorithm>

// Procedural cabinet simulation that works without loading IR files.
// Models: speaker cone resonance + rolloff, mic position, cabinet body resonance.
// Auto-configures based on amp type but parameters can be overridden.

class ProceduralCab
{
public:
    enum class SpeakerType
    {
        BrightJensen = 0,   // Bright, clear — classic American cleans (5kHz rolloff)
        WarmGreenback = 1,  // Warm midrange push — classic British crunch (4.5kHz rolloff)
        AggressiveV = 2,    // Aggressive upper-mid peak — modern high gain (4kHz rolloff)
        ChimeyBlue = 3,     // Chimey, pronounced resonance — Class A (5.5kHz rolloff)
        kNumSpeakers = 4
    };

    enum class CabBodyType
    {
        Open1x12  = 0,  // Open-back 1x12: bass cut, room sound
        Open2x12  = 1,  // Open-back 2x12: moderate bass
        Closed4x12 = 2, // Closed-back 4x12: tight bass, strong body
        kNumTypes = 3
    };

    void prepare (double sampleRate);
    void reset();

    // Auto-configure based on amp type
    void setAmpType (AmpType type);

    // Manual overrides
    void setSpeakerType (int type);
    void setMicPosition (float position01); // 0 = off-axis (dark), 1 = on-axis (bright)

    void process (float* buffer, int numSamples);

    bool isEnabled() const { return enabled_; }
    void setEnabled (bool on) { enabled_ = on; }

private:
    bool enabled_ = true;
    double sampleRate_ = 44100.0;
    SpeakerType speakerType_ = SpeakerType::WarmGreenback;
    CabBodyType cabBody_ = CabBodyType::Closed4x12;
    float micPosition_ = 0.6f; // Default: slightly off-axis

    // Biquad filter (TDF-II)
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    // Speaker model: resonant peak + HF rolloff (2 cascaded biquads for 4th-order rolloff)
    Biquad speakerResonance_;     // Peaking EQ at cone resonance
    Biquad speakerRolloff1_;      // LPF for HF rolloff (stage 1)
    Biquad speakerRolloff2_;      // LPF for HF rolloff (stage 2, cascaded = 4th order)

    // Speaker breakup (soft clip above threshold)
    float breakupThreshold_ = 0.7f;
    float breakupAmount_ = 0.05f;

    // Mic model: on-axis presence peak, off-axis LPF
    Biquad micPresence_;          // Peaking EQ at ~5.5kHz (on-axis)
    Biquad micDarkening_;         // LPF for off-axis darkening

    // Cabinet body resonance
    Biquad bodyResonance_;        // Peaking EQ at body resonance
    Biquad bodyLowShelf_;         // Low shelf (open-back cuts bass)

    void updateSpeakerCoefficients();
    void updateMicCoefficients();
    void updateBodyCoefficients();

    // Audio EQ Cookbook helpers
    void computePeaking (Biquad& bq, float freq, float gainDB, float Q);
    void computeLPF (Biquad& bq, float freq, float Q);
    void computeLowShelf (Biquad& bq, float freq, float gainDB, float Q);
};
