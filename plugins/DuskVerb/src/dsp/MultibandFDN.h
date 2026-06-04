#pragma once

#include "FDNReverb.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <vector>

// ===========================================================================
// MultibandFDN — three band-isolated FDNReverb tanks behind a Linkwitz-Riley
// 3-way crossover. Breaks the FDN's structural T60-vs-level coupling: in a
// single FDN a delay line's feedback gain sets BOTH its decay AND its steady-
// state energy, and the Hadamard mix couples bands — so the per-band RT60 curve
// can't be bent without warping the timbre (see memory
// duskverb_fdn_t60_coupling_wall). Splitting the input into Low/Mid/High bands
// and running an INDEPENDENT tank per band makes each band's loop gain local:
// its decay and level move together but no longer contaminate the neighbours.
//
// OPT-IN: DuskVerbEngine routes here only when a preset enables multiband. The
// default path stays the single fdn_ (bit-identical). A 3-tank sum is NOT and
// cannot be bit-null vs one full-band tank — the only coherent bit-null is
// "multiband off → legacy single tank untouched", enforced by the engine.
//
// Crossover tree (LR4 = two cascaded Butterworth 2nd-order, per band edge):
//   low  = LP_lr4(x, lowX)
//   rest = HP_lr4(x, lowX)
//   mid  = LP_lr4(rest, highX)
//   high = HP_lr4(rest, highX)
// low + mid + high reconstructs x to flat magnitude (LR crossovers sum allpass).
// Each band feeds its own tank; the three tank outputs are summed.
// ===========================================================================
class MultibandFDN
{
public:
    // ---- LR4 stage: two cascaded matched Butterworth biquads (TDF-II) ----
    struct LR4
    {
        // Two biquads (b0,b1,b2,a1,a2) + per-channel state (L/R).
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1L = 0, z2L = 0, z3L = 0, z4L = 0;
        float z1R = 0, z2R = 0, z3R = 0, z4R = 0;

        void designLowpass (float fc, float sr)  { design (fc, sr, true);  }
        void designHighpass (float fc, float sr) { design (fc, sr, false); }

        void design (float fc, float sr, bool lowpass)
        {
            // Butterworth Q = 1/sqrt(2); LR4 = the SAME 2nd-order section
            // cascaded twice (this gives the LR 24 dB/oct, −6 dB at fc).
            const float w0 = 2.0f * 3.14159265358979f * std::clamp (fc, 10.0f, sr * 0.49f) / sr;
            const float cosw = std::cos (w0), sinw = std::sin (w0);
            const float alpha = sinw / (2.0f * 0.70710678f);
            const float a0 = 1.0f + alpha;
            if (lowpass)
            {
                b0 = ((1.0f - cosw) * 0.5f) / a0;
                b1 = (1.0f - cosw) / a0;
                b2 = b0;
            }
            else
            {
                b0 = ((1.0f + cosw) * 0.5f) / a0;
                b1 = -(1.0f + cosw) / a0;
                b2 = b0;
            }
            a1 = (-2.0f * cosw) / a0;
            a2 = (1.0f - alpha) / a0;
        }

        void reset()
        {
            z1L = z2L = z3L = z4L = 0;
            z1R = z2R = z3R = z4R = 0;
        }

        inline float processL (float x)
        {
            float y1 = b0 * x + z1L; z1L = b1 * x - a1 * y1 + z2L; z2L = b2 * x - a2 * y1;
            float y2 = b0 * y1 + z3L; z3L = b1 * y1 - a1 * y2 + z4L; z4L = b2 * y1 - a2 * y2;
            return y2;
        }
        inline float processR (float x)
        {
            float y1 = b0 * x + z1R; z1R = b1 * x - a1 * y1 + z2R; z2R = b2 * x - a2 * y1;
            float y2 = b0 * y1 + z3R; z3R = b1 * y1 - a1 * y2 + z4R; z4R = b2 * y1 - a2 * y2;
            return y2;
        }
    };

    void prepare (double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        low_.prepare (sampleRate, maxBlockSize);
        mid_.prepare (sampleRate, maxBlockSize);
        high_.prepare (sampleRate, maxBlockSize);

        const size_t n = static_cast<size_t> (maxBlockSize);
        loL_.assign (n, 0); loR_.assign (n, 0);
        miL_.assign (n, 0); miR_.assign (n, 0);
        hiL_.assign (n, 0); hiR_.assign (n, 0);
        oL_.assign (n, 0);  oR_.assign (n, 0);

        designCrossovers();
    }

    void setCrossovers (float lowHz, float highHz)
    {
        lowXover_  = lowHz;
        highXover_ = highHz;
        if (sampleRate_ > 0.0) designCrossovers();
    }

    // Apply a configuration callback to all three tanks (broadcast the common
    // FDN setters), so DuskVerbEngine can keep its single forwarding site.
    template <typename Fn>
    void forEachTank (Fn&& fn) { fn (low_); fn (mid_); fn (high_); }

    FDNReverb& lowTank()  { return low_; }
    FDNReverb& midTank()  { return mid_; }
    FDNReverb& highTank() { return high_; }

    void clearBuffers()
    {
        low_.clearBuffers(); mid_.clearBuffers(); high_.clearBuffers();
        lpLow_.reset(); hpLow_.reset(); lpHigh_.reset(); hpHigh_.reset();
    }

    void setFreeze (bool f) { low_.setFreeze (f); mid_.setFreeze (f); high_.setFreeze (f); }

    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples)
    {
        const size_t N = static_cast<size_t> (numSamples);
        if (loL_.size() < N) return;   // defensive (prepare sizes to maxBlock)

        // ---- Phase-coherent LR4 3-way split ----
        for (int i = 0; i < numSamples; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            // low = LP(x, lowX); rest = HP(x, lowX)
            const float loLv = lpLow_.processL (xl);
            const float loRv = lpLow_.processR (xr);
            const float reLv = hpLow_.processL (xl);
            const float reRv = hpLow_.processR (xr);
            // mid = LP(rest, highX); high = HP(rest, highX)
            const float miLv = lpHigh_.processL (reLv);
            const float miRv = lpHigh_.processR (reRv);
            const float hiLv = hpHigh_.processL (reLv);
            const float hiRv = hpHigh_.processR (reRv);

            loL_[(size_t) i] = loLv; loR_[(size_t) i] = loRv;
            miL_[(size_t) i] = miLv; miR_[(size_t) i] = miRv;
            hiL_[(size_t) i] = hiLv; hiR_[(size_t) i] = hiRv;
        }

        // ---- Three isolated tanks ----
        low_.process  (loL_.data(), loR_.data(), outL,        outR,        numSamples);
        mid_.process  (miL_.data(), miR_.data(), oL_.data(),  oR_.data(),  numSamples);
        // sum mid into outL/outR
        for (int i = 0; i < numSamples; ++i) { outL[i] += oL_[(size_t) i]; outR[i] += oR_[(size_t) i]; }
        high_.process (hiL_.data(), hiR_.data(), oL_.data(),  oR_.data(),  numSamples);
        for (int i = 0; i < numSamples; ++i) { outL[i] += oL_[(size_t) i]; outR[i] += oR_[(size_t) i]; }
    }

private:
    void designCrossovers()
    {
        const float sr = static_cast<float> (sampleRate_);
        lpLow_.designLowpass  (lowXover_,  sr);
        hpLow_.designHighpass (lowXover_,  sr);
        lpHigh_.designLowpass (highXover_, sr);
        hpHigh_.designHighpass(highXover_, sr);
    }

    double sampleRate_ = 0.0;
    float  lowXover_  = 300.0f;
    float  highXover_ = 5000.0f;

    FDNReverb low_, mid_, high_;

    // One LR4 per split edge; each LR4 carries independent L and R state, so a
    // single instance handles both channels via processL/processR.
    LR4 lpLow_, hpLow_;     // low-crossover edge (LP + complementary HP)
    LR4 lpHigh_, hpHigh_;   // high-crossover edge

    std::vector<float> loL_, loR_, miL_, miR_, hiL_, hiR_, oL_, oR_;
};
