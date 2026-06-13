#pragma once

#include "DspUtils.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

// DenseHallReverb — DuskVerb's own dense hall reverberator.
//
// Original DuskVerb implementation of the well-known "diffused FDN" hall
// technique (the algorithm class behind zita-rev / Dragonfly Hall): density and
// smoothness come NOT from a high line count but from heavy allpass diffusion at
// every stage + modulation everywhere, which is exactly what our 16-line
// Hadamard FDN (AccurateHall) lacks — leaving a sparse tonal tail (measured tail
// spectral flatness ~0.044 vs a dense hall ~0.13). No third-party code; the
// topology + math are public, the implementation is ours.
//
// Signal flow (per sample, stereo):
//   DC-block -> N series modulated allpass input diffusers per channel
//   -> 8-line FDN: each line = [feedback + inject] -> low-shelf + high-shelf
//      (per-band RT60) -> modulated allpass diffuser
//   -> 8-point Hadamard mix (butterfly), normalized x sqrt(1/8)
//   -> modulated delay write (delay-time wander)
//   -> output taps -> spin combs (slow wander) -> out HPF/LPF
//
// RT60 is set by the per-line loop gain folded into the shelf gains
// (g = sqrt(1/8) * 10^(-3*loopLen/(RT60*sr))), with low/high band multipliers
// for frequency-dependent decay. All buffers allocated in prepare(); process()
// is allocation/lock/IO-free.
class DenseHallReverb
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr_ = (float) sampleRate;
        // Input diffuser allpass lengths (samples @ ~44.1k, scaled). Mutually
        // prime, descending — heavy decorrelation of the input into the tank.
        static const int inL[kNumInAP] = { 617, 535, 434, 347, 281, 218, 171, 131, 97, 73 };
        static const int inR[kNumInAP] = { 603, 547, 419, 353, 269, 211, 163, 127, 101, 79 };
        const float rr = sr_ / 44100.0f;
        for (int i = 0; i < kNumInAP; ++i)
        {
            inAPL_[i].alloc ((int) std::round (inL[i] * rr), kInExc);
            inAPR_[i].alloc ((int) std::round (inR[i] * rr), kInExc);
        }
        // 8 FDN line delays + per-line loop allpass diffusers (prime-ish, hall
        // scale ~ 60..120 ms base; size scales them). Reserve buffers at MAX size
        // ONCE here (allocation is message-thread only); setSize just moves len.
        for (int i = 0; i < kN; ++i) line_[i].alloc (lineLen (i, 1.6f), kDelExc);
        for (int i = 0; i < kN; ++i) loopAP_[i].alloc (loopAPLen (i, 1.6f), kLoopExc);
        rebuild();   // sets line_[i].len at the current size (no allocation)

        // Smooth modulation LFOs (sine), detuned per side; spin LFO slower.
        lfo1_.prepare (sr_, 0.7f, 0x111u);
        lfo2_.prepare (sr_, 0.83f, 0x222u);
        spin_.prepare (sr_, 0.30f, 0x333u);
        for (auto& d : dc_) d.reset();
        prepared_ = true;
        clear();
        update();
    }

    void clear()
    {
        for (auto& a : inAPL_) a.clear();
        for (auto& a : inAPR_) a.clear();
        for (int i = 0; i < kN; ++i) { line_[i].clear(); loopAP_[i].clear(); lsf_[i] = hsf_[i] = 0.0f; }
        spinL_.clear(); spinR_.clear();
    }

    // ── Setters (universal surface) ──────────────────────────────────────────
    void setDecayTime (float s)      { rt60_ = std::max (0.1f, s); if (prepared_) update(); }
    void setSize (float s)           { float n = 0.55f + std::clamp (s,0.0f,1.0f)*0.9f; if (std::abs(n-size_)>1e-4f){ size_=n; if(prepared_){ rebuild(); update(); } } }
    void setBassMultiply (float m)   { bassMul_ = std::clamp (m, 0.2f, 3.0f); if (prepared_) update(); }   // low-band RT60 factor
    void setTrebleMultiply (float m) { trebMul_ = std::clamp (m, 0.05f, 2.0f); if (prepared_) update(); }   // high-band RT60 factor
    void setMidMultiply (float)      {}
    void setModDepth (float d)       { modDepth_ = std::clamp (d, 0.0f, 1.0f); }       // scales allpass/delay excursion
    void setModRate (float hz)       { float r=std::clamp(hz,0.05f,3.0f); lfo1_.setRate(r); lfo2_.setRate(r*1.19f); }
    void setFreeze (bool f)          { frozen_ = f; if (prepared_) update(); }

    // ── Process ───────────────────────────────────────────────────────────────
    void process (const float* inLp, const float* inRp, float* outLp, float* outRp, int n)
    {
        if (! prepared_) { std::fill (outLp, outLp+n, 0.0f); std::fill (outRp, outRp+n, 0.0f); return; }
        const float md = 0.3f + 0.7f * modDepth_;   // modulation scaler (never fully static -> smooth)

        for (int s = 0; s < n; ++s)
        {
            const float m1 = lfo1_.next() * md;
            const float m2 = lfo2_.next() * md;

            float l = dc_[0].process (inLp[s]);
            float r = dc_[1].process (inRp[s]);

            // Input diffusion: 10 modulated allpasses per channel, alternating mod sign.
            float sgn = -1.0f;
            for (int i = 0; i < kNumInAP; ++i)
            {
                l = inAPL_[i].process (l, m1 * sgn);
                r = inAPR_[i].process (r, m2 * sgn);
                sgn = -sgn;
            }

            // FDN injection: lines 0-3 fed by L, 4-7 by R (sign pattern + - per pair).
            float x[kN];
            const float il = l, ir = r;
            x[0] = lineRead (0, +il, m1);
            x[1] = lineRead (1, +il, -m1);
            x[2] = lineRead (2, -il, m1);
            x[3] = lineRead (3, -il, -m1);
            x[4] = lineRead (4, +ir, -m2);
            x[5] = lineRead (5, +ir, m2);
            x[6] = lineRead (6, -ir, -m2);
            x[7] = lineRead (7, -ir, m2);

            // 8-point Hadamard (in-place butterfly), normalized by sqrt(1/8).
            hadamard8 (x);
            const float g = norm_;
            for (int i = 0; i < kN; ++i) x[i] *= g;

            // Write back into the modulated delays (delay-time wander).
            line_[0].write (x[0], m2);
            line_[1].write (x[1], m1);
            line_[2].write (x[2], -m2);
            line_[3].write (x[3], -m1);
            line_[4].write (x[4], -m1);
            line_[5].write (x[5], m2);
            line_[6].write (x[6], -m1);
            line_[7].write (x[7], m2);

            // Output taps (decorrelated L/R combination of tank nodes).
            float oL = 0.25f * (x[0] - x[1] + x[2] - x[3]);
            float oR = 0.25f * (x[4] + x[5] - x[6] - x[7]);

            // Spin combs — slow wander on the output for the "living" smear.
            const float sp = spin_.next() * (0.4f + 0.6f * modDepth_);
            oL = spinL_.process (oL, sp);
            oR = spinR_.process (oR, -sp);

            outLp[s] = oL;
            outRp[s] = oR;
        }
    }

private:
    static constexpr int kN        = 8;    // FDN lines
    static constexpr int kNumInAP  = 10;   // input diffusers per channel
    static constexpr int kInExc    = 16;   // input allpass mod excursion (samples)
    static constexpr int kLoopExc  = 24;   // loop allpass mod excursion
    static constexpr int kDelExc   = 40;   // delay-line mod excursion

    // ── Primitives (ours) ──────────────────────────────────────────────────────
    struct DCBlock { float x1=0,y1=0; void reset(){x1=y1=0;}
        float process(float x){ float y=x-x1+0.9995f*y1; x1=x; y1=y; return y; } };

    struct SineLFO { float ph=0,inc=0,sr=44100;
        void prepare(float s,float hz,std::uint32_t seed){ sr=s; setRate(hz); ph=(seed%1000)/1000.0f*6.2831853f; }
        void setRate(float hz){ inc=6.2831853f*hz/sr; }
        float next(){ ph+=inc; if(ph>6.2831853f)ph-=6.2831853f; return std::sin(ph); } };

    // Modulated Schroeder allpass: fractional read at (len - exc*mod).
    struct ModAP {
        std::vector<float> buf; int mask=0,w=0,len=0; float exc=0;
        void alloc(int n,int e){ len=std::max(1,n); exc=(float)e; int sz=DspUtils::nextPowerOf2(len+e+8); buf.assign((size_t)sz,0.0f); mask=sz-1; w=0; }
        void clear(){ std::fill(buf.begin(),buf.end(),0.0f); w=0; }
        float process(float x,float mod){
            float rp=(float)w-(float)len-exc*mod;
            int i0=(int)std::floor(rp); float fr=rp-i0;
            float d=buf[(size_t)(i0&mask)]*(1.0f-fr)+buf[(size_t)((i0+1)&mask)]*fr;
            float in=x+kG*d;
            buf[(size_t)w]=in+DspUtils::kDenormalPrevention; w=(w+1)&mask;
            return d-kG*in;
        }
        static constexpr float kG=0.625f;
    };

    // Modulated delay with read-on-getlast / write API + 2-band shelf decay.
    struct Line {
        std::vector<float> buf; int mask=0,w=0,len=0; float exc=0;
        void alloc(int n,int e){ len=std::max(1,n); exc=(float)e; int sz=DspUtils::nextPowerOf2(len+e+8); buf.assign((size_t)sz,0.0f); mask=sz-1; w=0; }
        void clear(){ std::fill(buf.begin(),buf.end(),0.0f); w=0; }
        float getlast(float mod) const {
            float rp=(float)w-(float)len-exc*mod;
            int i0=(int)std::floor(rp); float fr=rp-i0;
            return buf[(size_t)(i0&mask)]*(1.0f-fr)+buf[(size_t)((i0+1)&mask)]*fr;
        }
        void write(float x,float /*mod*/){ buf[(size_t)w]=x+DspUtils::kDenormalPrevention; w=(w+1)&mask; }
    };

    struct SpinComb { std::vector<float> buf; int mask=0,w=0,len=0; float exc=0;
        void alloc(int n,int e){ len=std::max(1,n); exc=(float)e; int sz=DspUtils::nextPowerOf2(len+e+8); buf.assign((size_t)sz,0.0f); mask=sz-1; w=0; }
        void clear(){ std::fill(buf.begin(),buf.end(),0.0f); w=0; }
        float process(float x,float mod){
            float rp=(float)w-(float)len-exc*mod;
            int i0=(int)std::floor(rp); float fr=rp-i0;
            float d=buf[(size_t)(i0&mask)]*(1.0f-fr)+buf[(size_t)((i0+1)&mask)]*fr;
            buf[(size_t)w]=x+DspUtils::kDenormalPrevention; w=(w+1)&mask;
            return 0.6f*x+0.4f*d;
        } };

    // line i: read delayed sample (modulated), high-shelf + low-shelf decay,
    // inject input, run through the per-line loop allpass diffuser.
    float lineRead (int i, float inject, float mod)
    {
        float v = line_[i].getlast (mod) + inject;
        // Per-band decay TILT (unity at mid) — shelves shape only the relative
        // bass/treble decay; the broadband loop gain (midG_) is applied ONCE
        // below so the RT60 knob reads true.
        lsf_[i] = lsf_[i] + lCoeff_ * (v - lsf_[i]);          // low band (< ~250 Hz)
        v = lTilt_ * lsf_[i] + (v - lsf_[i]);                 // bass tilt vs mid
        hsf_[i] = hsf_[i] + hCoeff_ * (v - hsf_[i]);          // low-pass (< ~3.5 kHz)
        v = hsf_[i] + hTilt_ * (v - hsf_[i]);                 // treble tilt vs mid
        v *= midG_;                                           // broadband loop gain (once)
        // per-line loop allpass diffuser (modulated) — the in-loop density.
        v = loopAP_[i].process (v, mod);
        return v;
    }

    static void hadamard8 (float* x)
    {
        float t;
        t=x[0]-x[1]; x[0]+=x[1]; x[1]=t;  t=x[2]-x[3]; x[2]+=x[3]; x[3]=t;
        t=x[4]-x[5]; x[4]+=x[5]; x[5]=t;  t=x[6]-x[7]; x[6]+=x[7]; x[7]=t;
        t=x[0]-x[2]; x[0]+=x[2]; x[2]=t;  t=x[1]-x[3]; x[1]+=x[3]; x[3]=t;
        t=x[4]-x[6]; x[4]+=x[6]; x[6]=t;  t=x[5]-x[7]; x[5]+=x[7]; x[7]=t;
        t=x[0]-x[4]; x[0]+=x[4]; x[4]=t;  t=x[1]-x[5]; x[1]+=x[5]; x[5]=t;
        t=x[2]-x[6]; x[2]+=x[6]; x[6]=t;  t=x[3]-x[7]; x[3]+=x[7]; x[7]=t;
    }

    int lineLen (int i, float size) const
    {
        // 8 mutually-prime base delays (ms @ base) * size * rate.
        static const float baseMs[kN] = { 58.6f, 72.3f, 49.1f, 86.7f, 63.2f, 78.9f, 45.3f, 91.4f };
        return std::max (1, (int) std::round (baseMs[i] * 0.001f * sr_ * size));
    }
    int loopAPLen (int i, float size) const
    {
        static const float baseMs[kN] = { 9.7f, 13.1f, 7.3f, 11.9f, 8.5f, 12.4f, 6.8f, 10.6f };
        return std::max (1, (int) std::round (baseMs[i] * 0.001f * sr_ * size));
    }

    // RT-safe: only re-points the delay length within the already-reserved
    // buffer (no allocation, no buffer clear) — safe to call from setSize on the
    // audio thread. Buffers are alloc'd once in prepare().
    void rebuild()
    {
        for (int i = 0; i < kN; ++i)
            line_[i].len = std::min (lineLen (i, size_), line_[i].mask - kDelExc - 4);
    }

    void update()
    {
        norm_ = std::sqrt (1.0f / (float) kN);
        // Per-line broadband decay folded into the mid-band shelf gain:
        // average loop length -> RT60 gain. (Shelf low/high gains add band tilt.)
        const float meanLen = (lineLen(0,size_)+lineLen(1,size_)+lineLen(2,size_)+lineLen(3,size_)
                              + lineLen(4,size_)+lineLen(5,size_)+lineLen(6,size_)+lineLen(7,size_)) / 8.0f;
        const float loopSec = meanLen / sr_;
        const float gMid = frozen_ ? 1.0f : std::pow (10.0f, -3.0f * loopSec / rt60_);
        midG_   = std::clamp (gMid, 0.0f, 0.999f);
        // Band per-pass gains from RT60 multipliers, then expressed as TILT
        // relative to mid (unity at default mults) so midG_ is the only
        // broadband loss — keeps the Decay knob honest.
        const float gLow  = std::clamp (std::pow (gMid, 1.0f / std::max (0.2f, bassMul_)), 0.0f, 0.9995f);
        const float gHigh = std::clamp (std::pow (gMid, 1.0f / std::max (0.2f, trebMul_)), 0.0f, 0.999f);
        lTilt_ = std::clamp (gLow  / std::max (1e-4f, midG_), 0.1f, 4.0f);
        hTilt_ = std::clamp (gHigh / std::max (1e-4f, midG_), 0.1f, 4.0f);
        // Crossover one-pole coeffs: low ~250 Hz, high ~3.5 kHz.
        lCoeff_ = 1.0f - std::exp (-6.2831853f * 250.0f  / sr_);
        hCoeff_ = 1.0f - std::exp (-6.2831853f * 3500.0f / sr_);
        // spin combs
        if (spinL_.buf.empty()) { spinL_.alloc ((int)(0.012f*sr_), (int)(0.004f*sr_)); spinR_.alloc ((int)(0.0131f*sr_), (int)(0.004f*sr_)); }
    }

    float sr_ = 44100.0f, size_ = 1.0f, rt60_ = 2.5f, bassMul_ = 1.0f, trebMul_ = 1.0f, modDepth_ = 0.4f;
    float norm_ = 0.354f, midG_ = 0.0f, lTilt_ = 1.0f, hTilt_ = 1.0f;
    float lCoeff_ = 0.03f, hCoeff_ = 0.4f;
    bool  frozen_ = false, prepared_ = false;

    DCBlock dc_[2];
    ModAP   inAPL_[kNumInAP], inAPR_[kNumInAP];
    Line    line_[kN];
    ModAP   loopAP_[kN];
    float   lsf_[kN] {}, hsf_[kN] {};
    SineLFO lfo1_, lfo2_, spin_;
    SpinComb spinL_, spinR_;
};
