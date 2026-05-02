// ToneStack.h — Per-amp parametric 3-band tone stack (low shelf + mid peak +
// high shelf), tuned per amp model to match published frequency-response
// curves. Each knob affects its own band: bass shelves the LF, mid peaks
// the midband, treble shelves the HF.
//
// Earlier revisions used a Yeh/Smith circuit-derived 3rd-order IIR for
// American/British. Those equations are mathematically correct but produce
// uniform-volume-knob behaviour rather than band-specific shaping (R2 and
// R3 dominate every transfer-function coefficient roughly equally, so each
// knob scales the magnitude across the full audio band). For a user-facing
// "tone stack" control the parametric topology is more predictable; per-
// amp tunings preserve the voicing differences between Fender and Marshall.

#pragma once

#include <cmath>
#include <algorithm>

class ToneStack
{
public:
    // Per-amp voicing — each amp has the same topology (low shelf + mid
    // peak + high shelf/peak) but different corner frequencies, Q values
    // and gain ranges. AC additionally has a cathode-follower nonlinearity
    // ahead of the EQ chain (the Vox triode buffer driving the James
    // network) and skips the mid stage (Top Boost has no mid pot).
    enum class Type { American = 0, British = 1, AC = 2 };

    void prepare (double sampleRate);
    void reset();
    void setType (Type type);
    void setBass (float value01);
    void setMid (float value01);     // ignored for AC (Top Boost has no mid)
    void setTreble (float value01);
    void process (float* buffer, int numSamples);

private:
    Type currentType_ = Type::British;
    double sampleRate_ = 44100.0;
    float bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    bool coeffsDirty_ = true;

    // Per-amp voicing: which frequency each band sits at, what its gain
    // range is, and (for the treble band) whether to use a high shelf or
    // a peaking filter. All numbers picked to match published Fender /
    // Marshall / Vox tone-stack response curves.
    struct Voicing
    {
        float bassHz;          // low-shelf corner
        float bassMaxDb;
        float midHz;           // mid peak centre (ignored for AC)
        float midQ;
        float midMaxDb;
        float trebleHz;        // high-shelf corner OR peak centre
        float trebleQ;         // 0 → use high shelf; >0 → use peaking biquad
        float trebleMaxDb;
        bool  hasMid;          // false for AC (Top Boost)
        bool  hasCathodeFollower; // true for AC only
    };

    static Voicing getVoicing (Type type);

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        float processSample (float x)
        {
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        void clear() { x1 = x2 = y1 = y2 = 0; }
    };

    Biquad bassBq_, midBq_, trebleBq_;

    void recomputeCoefficients();
    static void designLowShelf  (Biquad& bq, float fc, float gainDb, double sr);
    static void designHighShelf (Biquad& bq, float fc, float gainDb, double sr);
    static void designPeakingEQ (Biquad& bq, float fc, float gainDb, float q, double sr);
};
