#pragma once

#include "DspUtils.h"
#include "TwoBandDamping.h"   // OctaveBandDamping (per-octave GEQ, design out-of-line)
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
        for (int i = 0; i < kN; ++i) loopAP2_[i].alloc (loopAP2Len (i, 1.6f), kLoopExc);
        for (int i = 0; i < kN; ++i) loopAP3_[i].alloc (loopAP3Len (i, 1.6f), kLoopExc);
        // Spin combs — alloc here (sample-rate-dependent length) so a re-prepare
        // at a new SR reallocates; keeps update() allocation-free (audio-thread).
        spinL_.alloc ((int)(0.0061f*sr_), (int)(0.0002f*sr_));
        spinR_.alloc ((int)(0.0049f*sr_), (int)(0.0002f*sr_));
        rebuild();   // sets line_[i].len at the current size (no allocation)

        // Smooth modulation LFOs (sine), detuned per side; spin LFO slower.
        // prepare() seeds the phase but also sets a hardcoded rate — reapply the
        // stored modRate_ afterward so a prior setModRate() (preset/state) survives
        // re-initialization instead of being reset to the 0.7 Hz default.
        lfo1_.prepare (sr_, 0.7f, 0x111u);
        lfo2_.prepare (sr_, 0.83f, 0x222u);
        spin_.prepare (sr_, 0.30f, 0x333u);
        lfo1_.setRate (modRate_);
        lfo2_.setRate (modRate_ * 1.19f);
        for (auto& d : dc_) d.reset();
        prepared_ = true;
        updateLimiterCoeffs();
        clear();
        update();
    }

    void clear()
    {
        for (auto& a : inAPL_) a.clear();
        for (auto& a : inAPR_) a.clear();
        for (int i = 0; i < kN; ++i) { line_[i].clear(); loopAP_[i].clear(); loopAP2_[i].clear(); loopAP3_[i].clear(); lsf_[i] = hsf_[i] = 0.0f; octDamp_[i].reset(); limLp_[i] = limEnv_[i] = 0.0f; }
        spinL_.clear(); spinR_.clear();
        tcL_.reset(); tcR_.reset();
        for (auto& d : dc_) d.reset();   // process() always runs the input DC blockers — clear their history too, else state leaks across instances/sessions
    }

    // ── Setters (universal surface) ──────────────────────────────────────────
    void setDecayTime (float s)      { rt60_ = std::max (0.1f, s); if (prepared_) update(); }
    void setSize (float s)           { float n = 0.55f + std::clamp (s,0.0f,1.0f)*0.9f; if (std::abs(n-size_)>1e-4f){ size_=n; if(prepared_){ rebuild(); update(); } } }
    void setBassMultiply (float m)   { bassMul_ = std::clamp (m, 0.2f, 3.0f); if (prepared_) update(); }   // low-band RT60 factor
    void setTrebleMultiply (float m) { trebMul_ = std::clamp (m, 0.05f, 2.0f); if (prepared_) update(); }   // high-band RT60 factor
    void setMidMultiply (float m)    { midMul_ = std::clamp (m, 0.2f, 3.0f);    if (prepared_) update(); }   // mid-band RT60 factor (was a no-op; now the 3rd damping band)

    // Per-OCTAVE GEQ damping (FORK #2, 2026-06-16). The 3-band shelf above
    // (bass/mid/treble) cannot fit an arbitrary anchor's 9-octave T60 curve, and
    // each band's gain sets BOTH decay AND steady level (the coupling wall →
    // cent dark + T60-16k dies on the DenseHall presets). This sets each ISO
    // octave's T60 independently (Jot/Schlecht accurate-RT, same OctaveBandDamping
    // the AccurateHall FDN uses). band 0..8 = 63 Hz..16 kHz; seconds<=0 → that
    // octave inherits the broadband Decay. All octaves flat → octaveActive_ false
    // → the 3-band path runs (bit-identical for any preset not opting in).
    void setOctaveT60 (int band, float seconds)
    {
        if (band < 0 || band >= 9) return;
        octaveT60_[band] = seconds;
        octaveActive_ = false;
        for (int b = 0; b < 9; ++b) if (octaveT60_[b] > 0.0f) { octaveActive_ = true; break; }
        if (prepared_) update();
    }
    // Decay-knob coupling: reference broadband decay at which the octave curve is
    // realized 1:1; the live Decay knob then scales the whole curve. <=0 → 1.0.
    void setOctaveDecayRef (float seconds) { octaveDecayRef_ = seconds; if (prepared_) update(); }
    // FORK B: opt-in per preset. Default off = bit-identical. Needs octaveActive_.
    void setTonalCorrection (bool enabled) { tonalCorrEnabled_ = enabled; if (prepared_) update(); }
    // LOW ACCUMULATION LIMITER (2026-07-04, piano-gate defect): under sustained
    // low-frequency drive (repeating bass notes) the tank's sub band CHARGES —
    // Vocal Hall's 62 Hz grew +6.8 dB across a 22 s piano stem while the VVV
    // anchor stayed flat (DV's sub EDT ~2x the anchor's; each note tops up what
    // hasn't decayed). A static damping change would break the noiseburst T60
    // gates; instead each line follows its OWN low-band envelope and only when
    // that exceeds the threshold does it attenuate the low band per pass —
    // impulse/noiseburst tails never reach the threshold, a sustained charge
    // does. maxCut 0 → inactive → bit-identical.
    void setLowAccumLimiter (float threshDb, float maxCut, float splitHz)
    {
        limThreshLin_ = std::pow (10.0f, std::clamp (threshDb, -80.0f, 0.0f) / 20.0f);
        limMaxCut_    = std::clamp (maxCut, 0.0f, 0.5f);
        limSplitHz_   = std::clamp (splitHz, 30.0f, 300.0f);
        limActive_    = limMaxCut_ > 1.0e-6f;
        if (prepared_) updateLimiterCoeffs();
    }
    void setCrossoverFreq (float hz)     { lowX_  = std::clamp (hz, 40.0f, 2000.0f);   if (lowX_ > highX_) highX_ = lowX_; if (prepared_) update(); }  // bass↔mid split (keeps lowX_<=highX_)
    void setHighCrossoverFreq (float hz) { highX_ = std::clamp (hz, 800.0f, 14000.0f); if (highX_ < lowX_) lowX_ = highX_; if (prepared_) update(); }  // mid↔high split (keeps lowX_<=highX_)
    void setModDepth (float d)       { modDepth_ = std::clamp (d, 0.0f, 1.0f); }       // scales allpass/delay excursion
    void setModRate (float hz)       { modRate_=std::clamp(hz,0.05f,3.0f); lfo1_.setRate(modRate_); lfo2_.setRate(modRate_*1.19f); }
    void setFreeze (bool f)          { frozen_ = f; if (prepared_) update(); }

    // ── Process ───────────────────────────────────────────────────────────────
    void process (const float* inLp, const float* inRp, float* outLp, float* outRp, int n)
    {
        if (! prepared_) { std::fill (outLp, outLp+n, 0.0f); std::fill (outRp, outRp+n, 0.0f); return; }
        const float md = 0.05f + 0.95f * modDepth_;   // modulation scaler (tiny floor -> not fully static, but no audible chorus at low depth)

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
            // Freeze mutes input injection so the lossless (g=1.0 when frozen) tank
            // HOLDS the existing tail instead of accumulating live input — the UI
            // freeze contract (matches FDNReverb's inputGain=0 when frozen).
            float x[kN];
            const float il = frozen_ ? 0.0f : l, ir = frozen_ ? 0.0f : r;
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
            const float sp = spin_.next() * (0.05f + 0.95f * modDepth_);
            oL = spinL_.process (oL, sp);
            oR = spinR_.process (oR, -sp);

            // FORK B — Jot tonal correction (decouple per-band T60 from LEVEL).
            // Output GEQ (post-tank, non-recursive → no codegen bit-null risk).
            // Flattens per-band steady-state energy so a band's long T60 no longer
            // forces a hot level (the ss-hot/cent-dark coupling on the halls).
            // tcActive_ false → identity → bit-identical. corr = sqrt(Tmin/Tb).
            if (tcActive_)
            {
                oL = tcL_.process (oL, tcCoeffs_);
                oR = tcR_.process (oR, tcCoeffs_);
            }

            outLp[s] = oL;
            outRp[s] = oR;
        }
    }

private:
    static constexpr int kN        = 8;    // FDN lines
    static constexpr int kNumInAP  = 10;   // input diffusers per channel
    static constexpr int kInExc    = 4;    // input allpass mod excursion (samples) — was 16; cut to kill tail chorus
    static constexpr int kLoopExc  = 6;    // loop allpass mod excursion — was 24
    static constexpr int kDelExc   = 5;    // delay-line mod excursion — was 40 (the main flange culprit)

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
            float d=DspUtils::cubicHermite(buf.data(), mask, i0, fr);   // HF-lossless (was linear)
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
            // HF-lossless: cubicHermite (4-pt) not linear. Linear is a position-
            // dependent LP (−6 dB @16k @fr=0.5) read ~5×/line/pass → compounds →
            // 16k can't sustain. Hermite = the FDN/QuadTank/Dattorro standard.
            return DspUtils::cubicHermite(buf.data(), mask, i0, fr);
        }
        void write(float x,float /*mod*/){ buf[(size_t)w]=x+DspUtils::kDenormalPrevention; w=(w+1)&mask; }
    };

    struct SpinComb { std::vector<float> buf; int mask=0,w=0,len=0; float exc=0;
        void alloc(int n,int e){ len=std::max(1,n); exc=(float)e; int sz=DspUtils::nextPowerOf2(len+e+8); buf.assign((size_t)sz,0.0f); mask=sz-1; w=0; }
        void clear(){ std::fill(buf.begin(),buf.end(),0.0f); w=0; }
        float process(float x,float mod){
            float rp=(float)w-(float)len-exc*mod;
            int i0=(int)std::floor(rp); float fr=rp-i0;
            float d=DspUtils::cubicHermite(buf.data(), mask, i0, fr);   // HF-lossless (was linear)
            // Schroeder allpass (flat magnitude -> NO comb notches). Was a
            // 0.6x+0.4d feed-forward comb at 12 ms = the metallic ring. Allpass
            // form decorrelates L/R + carries the spin modulation without
            // coloring the spectrum.
            float in=x+kSG*d;
            buf[(size_t)w]=in+DspUtils::kDenormalPrevention; w=(w+1)&mask;
            return d-kSG*in;
        }
        static constexpr float kSG=0.7f; };

    // line i: read delayed sample (modulated), high-shelf + low-shelf decay,
    // inject input, run through the per-line loop allpass diffuser.
    float lineRead (int i, float inject, float mod)
    {
        float v = line_[i].getlast (mod) + inject;
        if (octaveActive_)
        {
            // Fork #2: per-octave GEQ damping (9 ISO octaves, independent T60).
            v = octDamp_[i].process (v, octCoeffs_);
        }
        else
        {
            // 3-band damping: split v into low/mid/high via two one-pole lowpasses
            // (at lowX_, highX_), apply each band's own per-pass gain. Mid is the
            // RT60 reference (midMul_=1 -> gMidB_=midG_), so the Decay knob reads
            // true; bass/treble/mid tilt the per-band decay independently.
            lsf_[i] = lsf_[i] + lCoeff_ * (v - lsf_[i]);          // lowpass < lowX_
            hsf_[i] = hsf_[i] + hCoeff_ * (v - hsf_[i]);          // lowpass < highX_
            const float lowB  = lsf_[i];                          // < lowX_
            const float midB  = hsf_[i] - lsf_[i];                // lowX_ .. highX_
            const float highB = v - hsf_[i];                      // > highX_
            v = gLowB_ * lowB + gMidB_ * midB + gHighB_ * highB;
        }
        // LOW ACCUMULATION LIMITER: per-line low-band envelope; above threshold,
        // attenuate the low band per pass (drive-following — impulse tails never
        // reach the threshold, sustained bass charge does). Off = branch skipped.
        if (limActive_)
        {
            limLp_[i] += limLpCoeff_ * (v - limLp_[i]);
            const float low = limLp_[i];
            const float a = std::abs (low);
            limEnv_[i] += (a > limEnv_[i] ? limAtkCoeff_ : limRelCoeff_) * (a - limEnv_[i]);
            const float over = limEnv_[i] / limThreshLin_;
            if (over > 1.0f)
                v -= std::min ((over - 1.0f) * 0.5f, 1.0f) * limMaxCut_ * low;
        }
        // per-line loop allpass diffusers (modulated) — the in-loop density.
        // Two nested allpasses (opposite mod sign) double the echo density.
        v = loopAP_[i].process (v, mod);
        v = loopAP2_[i].process (v, -mod);
        v = loopAP3_[i].process (v, mod);
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
        // True Schroeder diffuser range (<~5 ms). Were 6.8-13.1 ms, which made
        // discrete ~12 ms echoes in the feedback loop (metallic comb + sparse
        // modal beating). Short + mutually-prime -> dense in-loop diffusion.
        static const float baseMs[kN] = { 4.7f, 3.1f, 5.3f, 2.3f, 4.1f, 1.9f, 3.7f, 1.3f };
        return std::max (1, (int) std::round (baseMs[i] * 0.001f * sr_ * size));
    }
    int loopAP2Len (int i, float size) const
    {
        // Second nested diffuser per line (mutually prime vs loopAP) — doubles
        // in-loop echo density to lift the tail toward the anchors' diffuse
        // wash (ed_end ~1.0 -> ~1.5+); kills sparse-mode beating ("watery").
        static const float baseMs[kN] = { 2.9f, 4.3f, 1.7f, 3.7f, 2.1f, 4.9f, 1.5f, 3.3f };
        return std::max (1, (int) std::round (baseMs[i] * 0.001f * sr_ * size));
    }
    int loopAP3Len (int i, float size) const
    {
        // Third nested diffuser — lifts the large-hall steady-state density
        // (long lines recirculate slowly, so 2 nested APs alone don't fill
        // the tail fast enough). Mutually prime vs loopAP/loopAP2.
        static const float baseMs[kN] = { 1.1f, 2.7f, 3.9f, 1.5f, 4.5f, 2.5f, 3.1f, 1.9f };
        return std::max (1, (int) std::round (baseMs[i] * 0.001f * sr_ * size));
    }

    // RT-safe: only re-points the delay length within the already-reserved
    // buffer (no allocation, no buffer clear) — safe to call from setSize on the
    // audio thread. Buffers are alloc'd once in prepare().
    void rebuild()
    {
        // Re-point every size-dependent length within its already-reserved
        // buffer. The loop allpasses recirculate inside the feedback path, so
        // their lengths feed update()'s RT60 math (sumLen); leaving them at the
        // max-size alloc length would desync the actual delay from the decay
        // gains. Clamp to the buffer span (alloc'd at size 1.6 with +exc+8).
        for (int i = 0; i < kN; ++i)
        {
            line_[i].len    = std::min (lineLen   (i, size_), line_[i].mask    - kDelExc  - 4);
            loopAP_[i].len  = std::min (loopAPLen  (i, size_), loopAP_[i].mask  - kLoopExc - 4);
            loopAP2_[i].len = std::min (loopAP2Len (i, size_), loopAP2_[i].mask - kLoopExc - 4);
            loopAP3_[i].len = std::min (loopAP3Len (i, size_), loopAP3_[i].mask - kLoopExc - 4);
        }
    }

    void update()
    {
        norm_ = std::sqrt (1.0f / (float) kN);
        // Per-line broadband decay folded into the mid-band shelf gain:
        // average loop length -> RT60 gain. (Shelf low/high gains add band tilt.)
        // Effective loop length per line = delay + the 3 nested loop allpasses'
        // delays (they recirculate inside the feedback path, so they lengthen
        // the real round-trip and must be counted or the Decay knob runs long).
        float sumLen = 0.0f;
        for (int i = 0; i < kN; ++i)
            sumLen += lineLen(i,size_) + loopAPLen(i,size_) + loopAP2Len(i,size_) + loopAP3Len(i,size_);
        const float meanLen = sumLen / (float) kN;
        const float loopSec = meanLen / sr_;
        const float gMid = frozen_ ? 1.0f : std::pow (10.0f, -3.0f * loopSec / rt60_);
        if (frozen_)
        {
            // True unity hold: freeze MUST be loss-less. The ≤0.999 clamps below
            // cap the non-frozen gain for stability, but applied to gMid=1.0 they
            // bleed ~0.1%/pass → the "frozen" tail slowly decays. Force exact 1.0
            // on every band (and the octave path reads midG_, so it holds too).
            midG_ = gMidB_ = gLowB_ = gHighB_ = 1.0f;
        }
        else
        {
            midG_   = std::clamp (gMid, 0.0f, 0.999f);
            // Per-band per-pass gains from the RT60 multipliers. Mid is the
            // broadband reference (midMul_=1 -> gMidB_=midG_); bass/treble tilt
            // relative to it. Each band's gain sets BOTH its decay and steady
            // level (feedback-damping coupling — inherent to a single-tap loop).
            gMidB_  = std::clamp (std::pow (gMid, 1.0f / std::max (0.2f, midMul_)),  0.0f, 0.999f);
            gLowB_  = std::clamp (std::pow (gMid, 1.0f / std::max (0.2f, bassMul_)), 0.0f, 0.9995f);
            gHighB_ = std::clamp (std::pow (gMid, 1.0f / std::max (0.2f, trebMul_)), 0.0f, 0.999f);
        }
        // Tunable crossover one-pole coeffs (defaults 250 Hz / 3.5 kHz).
        lCoeff_ = 1.0f - std::exp (-6.2831853f * std::min (lowX_,  sr_ * 0.45f) / sr_);
        hCoeff_ = 1.0f - std::exp (-6.2831853f * std::min (highX_, sr_ * 0.45f) / sr_);

        // Per-octave GEQ coeffs (fork #2). Each ISO octave's per-pass loop gain
        // gk = 10^(-3·meanLen/(Tk·sr)) realizes target T60 Tk over the uniform
        // mean loop length. Decay knob scales the curve via octaveDecayRef_. The
        // GEQ is attenuation-only (gk≤0.9999); a flat octave inherits the
        // broadband gMid. designCoeffs runs message-thread (out-of-line, no FDN
        // codegen drift). Only built when active → the 3-band path stays untouched.
        if (octaveActive_)
        {
            static constexpr float kOctaveXoverHz[OctaveBandDamping::kNumShelves] = {
                88.4f, 176.8f, 353.6f, 707.1f, 1414.2f, 2828.4f, 5656.9f, 11313.7f
            };
            const float octaveScale = (octaveDecayRef_ > 0.05f)
                ? std::clamp (rt60_ / octaveDecayRef_, 0.1f, 8.0f) : 1.0f;
            float gOct[OctaveBandDamping::kNumBands];
            for (int k = 0; k < OctaveBandDamping::kNumBands; ++k)
            {
                const float Tk = octaveT60_[k] * octaveScale;
                gOct[k] = frozen_
                    ? midG_   // freeze: hold (near-unity), matches the 3-band path
                    : (Tk > 0.0f)
                        ? std::clamp (std::pow (10.0f, -3.0f * meanLen / (Tk * sr_)), 0.001f, 0.9999f)
                        : midG_;   // flat octave → broadband decay
            }
            octCoeffs_ = OctaveBandDamping::designCoeffs (gOct, kOctaveXoverHz, sr_);
        }

        // FORK B — Jot tonal correction: flatten per-band steady-state ENERGY so a
        // band's long T60 no longer forces a hot LEVEL (the ss-hot/cent-dark
        // coupling). corr_b = sqrt(Tmin/Tb): the shortest-decay octave = unity, the
        // longer (low) octaves get level-trimmed → cuts the hot lows + lifts cent
        // relative. Output GEQ (applied post-tank in process), no loop-stability
        // constraint. Identity unless the preset opts in (tonalCorrEnabled_).
        tcActive_ = octaveActive_ && tonalCorrEnabled_;
        if (tcActive_)
        {
            static constexpr float kXoverHz[OctaveBandDamping::kNumShelves] = {
                88.4f, 176.8f, 353.6f, 707.1f, 1414.2f, 2828.4f, 5656.9f, 11313.7f
            };
            const float oScale = (octaveDecayRef_ > 0.05f)
                ? std::clamp (rt60_ / octaveDecayRef_, 0.1f, 8.0f) : 1.0f;
            float Tb[OctaveBandDamping::kNumBands]; float Tmin = 1.0e9f;
            for (int k = 0; k < OctaveBandDamping::kNumBands; ++k)
            {
                Tb[k] = (octaveT60_[k] > 0.0f) ? octaveT60_[k] * oScale
                                               : std::max (rt60_, 1.0e-3f);
                Tmin = std::min (Tmin, Tb[k]);
            }
            float corr[OctaveBandDamping::kNumBands];
            for (int k = 0; k < OctaveBandDamping::kNumBands; ++k)
                corr[k] = std::sqrt (std::clamp (Tmin / std::max (Tb[k], 1.0e-3f), 0.05f, 1.0f));
            tcCoeffs_ = OctaveBandDamping::designCoeffs (corr, kXoverHz, sr_);
        }
        // (spin combs allocated in prepare() — SR-dependent, audio-thread-safe here)
    }

    float sr_ = 44100.0f, size_ = 1.0f, rt60_ = 2.5f, bassMul_ = 1.0f, midMul_ = 1.0f, trebMul_ = 1.0f, modDepth_ = 0.4f;
    float modRate_ = 0.7f;   // persisted Mod Rate (lfo1_ base; lfo2_ = ×1.19) so prepare() doesn't drop it
    float lowX_ = 250.0f, highX_ = 3500.0f;
    float norm_ = 0.354f, midG_ = 0.0f, gLowB_ = 0.0f, gMidB_ = 0.0f, gHighB_ = 0.0f;
    float lCoeff_ = 0.03f, hCoeff_ = 0.4f;
    bool  frozen_ = false, prepared_ = false;

    DCBlock dc_[2];
    ModAP   inAPL_[kNumInAP], inAPR_[kNumInAP];
    Line    line_[kN];
    ModAP   loopAP_[kN];
    ModAP   loopAP2_[kN];
    ModAP   loopAP3_[kN];
    float   lsf_[kN] {}, hsf_[kN] {};
    SineLFO lfo1_, lfo2_, spin_;
    SpinComb spinL_, spinR_;

    // Per-octave GEQ damping (fork #2). One filter bank per line (shared coeffs,
    // since DenseHall uses a uniform mean loop length for decay — see update()).
    OctaveBandDamping        octDamp_[kN];
    OctaveBandDamping::Coeffs octCoeffs_;
    float octaveT60_[9] {};
    float octaveDecayRef_ = 0.0f;
    bool  octaveActive_   = false;

    // Low accumulation limiter state (setLowAccumLimiter). Per-line one-pole
    // low split + per-line low-band envelope (fast attack, ~1 s release).
    bool  limActive_    = false;
    float limThreshLin_ = 0.01f;     // linear envelope threshold
    float limMaxCut_    = 0.0f;      // max per-pass low-band gain cut (0..0.5)
    float limSplitHz_   = 90.0f;
    float limLpCoeff_   = 0.0f, limAtkCoeff_ = 0.0f, limRelCoeff_ = 0.0f;
    float limLp_[kN]  = {};
    float limEnv_[kN] = {};

    void updateLimiterCoeffs()
    {
        const float sr = sr_;
        limLpCoeff_  = 1.0f - std::exp (-6.2831853f * limSplitHz_ / sr);
        // SLOW time constants — the point is to track the CHARGE, not the note:
        // a 600 ms attack means a noiseburst/impulse can't engage the limiter
        // (T60 gates untouched), and a 6 s release means engagement grows with
        // the accumulated low energy across a sustained passage, so the cut is
        // larger late than early — flattening the charge, not just the level.
        limAtkCoeff_ = 1.0f - std::exp (-1.0f / (0.600f * sr));
        limRelCoeff_ = 1.0f - std::exp (-1.0f / (6.0f   * sr));
    }

    // FORK B — Jot output tonal-correction (stereo). Identity unless opted in.
    OctaveBandDamping        tcL_, tcR_;
    OctaveBandDamping::Coeffs tcCoeffs_;
    bool  tcActive_         = false;
    bool  tonalCorrEnabled_ = false;
};
