#pragma once

#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

// VelvetTail — a SPARSE velvet-noise FIR reverb tail for the gated-reverse engine
// (ReverseRoomEngine, algo 9, the single preset "Reverse Taps"). Replaces the FDN.
// Feed-FORWARD (no recirculation): a band's "T60" is purely its tap-gain envelope
// time constant → NO mid-decay floor and per-band decay / level / brightness /
// stereo all DECOUPLE.
//
//   feed (mono ER output) ─▶ LR2 4-band split (250/500/2000 Hz)
//        each band ─▶ per-band velvet field (signed sparse taps, exp-decay gains)
//        L/R fields: mid/hi INDEPENDENT seeds (corr≈0); sub/lowmid SHARED tap
//        positions with a sign-flip fraction (corr<0 = the anchor's wide bass).
//   Σ bands ─▶ out.  Unit-normalize each field THEN apply per-band level.
//
// (A 9-band PER-OCTAVE variant was tried 2026-06-18 to resolve the 9-octave T60
// shape, but the 8 crossover LPs LEAK into adjacent octaves → a band's measured
// T60 is contaminated by neighbors → mids floor ~0.33 s regardless of commanded
// tau, Newton calibration diverged, and the extra level bands regressed more than
// the cent gain. Net worse (30 vs 26). Reverted to this 4-band version.)
class VelvetTail
{
public:
    static constexpr int kBands = 4;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;
        const int bufLen = DspUtils::nextPowerOf2 (
            static_cast<int> (std::ceil (kMaxTimeMs * 0.001 * sampleRate)) + 8);
        for (int b = 0; b < kBands; ++b)
        {
            ringL_[b].assign (static_cast<size_t> (bufLen), 0.0f);
            ringR_[b].assign (static_cast<size_t> (bufLen), 0.0f);
        }
        mask_ = bufLen - 1;
        writePos_ = 0;
        readEnv();
        designCrossovers();
        // Reset crossover filter state after (re)designing coeffs so a re-prepare
        // (sample-rate / instance reuse) doesn't carry stale delay state into the
        // freshly-built taps. clear() also resets these, but prepare() must stand
        // alone for callers that don't follow it with clear().
        lp250_.reset(); lp500_.reset(); lp2k_.reset();
        prepared_ = true;
        buildTaps();
    }

    // buildTaps() is allocation-free after prepare() (the Field vectors keep their
    // kMaxTaps capacity, so .assign reuses storage), but it runs ~2k exp/rng ops, so
    // every rebuild-triggering setter CHANGE-GUARDS: setGlobalDecayScale/setSizeScale
    // are driven from the live Decay/Size knobs via pushIfChanged on the AUDIO thread,
    // so a no-op call must not rebuild. RT-safe (no heap/lock — Field vectors keep
    // kMaxTaps capacity so .assign reuses storage). CAVEAT: under ACTIVE Decay/Size
    // automation the value changes every block, so buildTaps() (a few thousand
    // std::exp/sqrt) runs per-block — an RT CPU cost, not a correctness bug, and only
    // on the single algo-9 (Reverse) preset. If a future preset automates these hard,
    // defer the rebuild off-thread (double-buffer fieldL_/fieldR_, publish atomically).
    void setBandT60 (int b, float seconds)   { if (b>=0&&b<kBands){ const float v=std::clamp(seconds,0.02f,3.0f); if(std::abs(v-bandT60_[b])<=1e-5f)return; bandT60_[b]=v; if(prepared_)buildTaps(); } }
    void setBand125T60 (float seconds)       { const float v=std::clamp(seconds,0.05f,3.0f); if(std::abs(v-band125T60_)<=1e-5f)return; band125T60_=v; if(prepared_)buildTaps(); }
    void setBandLevelDb (int b, float db)    { if(b>=0&&b<kBands){ const float v=std::pow(10.0f,db/20.0f); if(std::abs(v-bandLevelLin_[b])<=1e-5f)return; bandLevelLin_[b]=v; if(prepared_)buildTaps(); } }
    void setStereoAntiCorr (float frac)      { const float v=std::clamp(frac,0.0f,0.6f); if(std::abs(v-flipFrac_)<=1e-5f)return; flipFrac_=v; if(prepared_)buildTaps(); }
    void setSizeScale (float s)              { const float v=std::clamp(s,0.2f,3.0f); if(std::abs(v-sizeScale_)<=1e-4f)return; sizeScale_=v; if(prepared_)buildTaps(); }
    void setGlobalDecayScale (float s)       { const float v=std::clamp(s,0.1f,8.0f); if(std::abs(v-decayScale_)<=1e-4f)return; decayScale_=v; if(prepared_)buildTaps(); }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (! prepared_) { std::fill (outL, outL+n, 0.0f); std::fill (outR, outR+n, 0.0f); return; }
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (inL[i] + inR[i]);   // mono feed; L/R differ by tap geometry
            const float lo  = lp500_.process (x);
            const float hi  = x - lo;
            const float b0  = lp250_.process (lo);
            const float b1  = lo - b0;
            const float b2  = lp2k_.process (hi);
            const float b3  = hi - b2;
            const float bf[kBands] = { b0, b1, b2, b3 };

            float l = 0.0f, r = 0.0f;
            const int wp = writePos_;
            for (int b = 0; b < kBands; ++b)
            {
                ringL_[b][static_cast<size_t> (wp)] = bf[b];
                ringR_[b][static_cast<size_t> (wp)] = bf[b];
                const float* rl = ringL_[b].data();
                const float* rr = ringR_[b].data();
                const Field& fl = fieldL_[b];
                const Field& fr = fieldR_[b];
                float al = 0.0f, ar = 0.0f;
                const int nl = fl.n, nr = fr.n;
                for (int t = 0; t < nl; ++t) al += fl.gain[t] * rl[(wp - fl.pos[t]) & mask_];
                for (int t = 0; t < nr; ++t) ar += fr.gain[t] * rr[(wp - fr.pos[t]) & mask_];
                l += al; r += ar;
            }
            outL[i] = l; outR[i] = r;
            writePos_ = (wp + 1) & mask_;
        }
    }

    void clear()
    {
        for (int b = 0; b < kBands; ++b)
        {
            std::fill (ringL_[b].begin(), ringL_[b].end(), 0.0f);
            std::fill (ringR_[b].begin(), ringR_[b].end(), 0.0f);
        }
        writePos_ = 0;
        lp500_.reset(); lp250_.reset(); lp2k_.reset();
    }

private:
    static constexpr float kMaxTimeMs = 700.0f;
    static constexpr int   kMaxTaps   = 512;

    struct Rng { uint32_t s; float next() { s^=s<<13; s^=s>>17; s^=s<<5; return (s & 0xFFFFFF) / float(0x1000000); } };

    struct Biquad
    {
        float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void reset() { z1=z2=0; }
        float process (float x)
        {
            const float y = b0*x + z1;
            z1 = b1*x - a1*y + z2;
            z2 = b2*x - a2*y;
            return y;
        }
        void lowpass (float fc, float sr)
        {
            const float w = std::tan (3.14159265358979f * std::clamp (fc, 20.0f, 0.45f*sr) / sr);
            const float k = 1.41421356f;
            const float nrm = 1.0f / (1.0f + k*w + w*w);
            b0 = w*w*nrm; b1 = 2.0f*b0; b2 = b0;
            a1 = 2.0f*(w*w - 1.0f)*nrm;
            a2 = (1.0f - k*w + w*w)*nrm;
        }
    };

    struct Field { std::vector<int> pos; std::vector<float> gain; int n = 0; };

    void designCrossovers()
    {
        const float sr = static_cast<float> (sampleRate_);
        lp250_.lowpass (250.0f, sr);
        lp500_.lowpass (500.0f, sr);
        lp2k_ .lowpass (2000.0f, sr);
    }

    static float density (float tMs) { return std::clamp (std::exp (-tMs/40.0f) + 0.55f, 0.0f, 1.0f); }

    float gainEnv (int band, float tMs) const
    {
        if (band == 0)
        {
            const float tauF = (bandT60_[0]   * decayScale_) / 6.91f * 1000.0f;
            const float tauS = (band125T60_    * decayScale_) / 6.91f * 1000.0f;
            return 0.7f*std::exp(-tMs/tauF) + 0.3f*std::exp(-tMs/tauS);
        }
        const float tau = (bandT60_[band] * decayScale_) / 6.91f * 1000.0f;
        return std::exp (-tMs / tau);
    }

    void buildField (int band, uint32_t seed, Field& f)
    {
        const float sr = static_cast<float> (sampleRate_);
        const float cell = cellRateMs_[band] * sizeScale_;
        const float spanMs = std::min (kMaxTimeMs - 5.0f,
                                       (band==0 ? band125T60_ : bandT60_[band]) * decayScale_ * 1.4f * 1000.0f);
        f.pos.assign (kMaxTaps, 0); f.gain.assign (kMaxTaps, 0.0f); f.n = 0;
        Rng rng { seed };
        for (float baseMs = 0.0f; baseMs <= spanMs && f.n < kMaxTaps; baseMs += cell)
        {
            if (rng.next() < density (baseMs))
            {
                const float tMs = baseMs + rng.next() * cell;
                const int d = static_cast<int> (tMs * 0.001f * sr);
                if (d <= 0 || d > mask_) continue;
                const float g = gainEnv (band, tMs);
                if (g < 1.0e-4f) break;
                f.pos[f.n]  = d;
                f.gain[f.n] = (rng.next() < 0.5f ? -1.0f : 1.0f) * g;
                ++f.n;
            }
        }
        normalize (f);
    }

    void buildFieldCorrR (int band, const Field& L, uint32_t seed, Field& f)
    {
        const float sr = static_cast<float> (sampleRate_);
        const float cell = cellRateMs_[band] * sizeScale_;
        f.pos.assign (kMaxTaps, 0); f.gain.assign (kMaxTaps, 0.0f); f.n = 0;
        Rng rng { seed };
        for (int t = 0; t < L.n && f.n < kMaxTaps; ++t)
        {
            int d = L.pos[t];
            float sign = (L.gain[t] >= 0.0f) ? 1.0f : -1.0f;
            if (rng.next() < 0.70f)
            {
                const float jitMs = (rng.next() - 0.5f) * 2.0f * cell;
                d = std::max (1, d + static_cast<int> (jitMs * 0.001f * sr));
                if (d > mask_) d = mask_;
                if (rng.next() < 0.5f) sign = -sign;
            }
            else if (rng.next() < std::min (1.0f, flipFrac_ / 0.30f))  // honest prob (was unclamped → saturated >0.3)
            {
                sign = -sign;
            }
            const float tMs = static_cast<float> (d) / sr * 1000.0f;
            f.pos[f.n]  = d;
            f.gain[f.n] = sign * gainEnv (band, tMs);
            ++f.n;
        }
        normalize (f);
    }

    void normalize (Field& f)
    {
        float e = 0.0f;
        for (int i = 0; i < f.n; ++i) e += f.gain[i]*f.gain[i];
        if (e <= 0.0f) return;
        const float g = 1.0f / std::sqrt (e);
        for (int i = 0; i < f.n; ++i) f.gain[i] *= g;
    }

    void buildTaps()
    {
        for (int b = 0; b < kBands; ++b)
        {
            buildField (b, 0xC0FFEEu + static_cast<uint32_t> (b)*0x9E3779B9u, fieldL_[b]);
            if (b <= 1)
                buildFieldCorrR (b, fieldL_[b], 0x1234567u + static_cast<uint32_t> (b)*0x85EBCA6Bu, fieldR_[b]);
            else
                buildField (b, 0x1234567u + static_cast<uint32_t> (b)*0x85EBCA6Bu, fieldR_[b]);
            const float lv = bandLevelLin_[b];
            for (int i = 0; i < fieldL_[b].n; ++i) fieldL_[b].gain[i] *= lv;
            for (int i = 0; i < fieldR_[b].n; ++i) fieldR_[b].gain[i] *= lv;
        }
    }

    void readEnv()
    {
        // DUSKVERB_VELVET="subT60,125T60,lowmidT60,midT60,hiT60,subDb,lowmidDb,midDb,hiDb,flipFrac"
        if (const char* ov = std::getenv ("DUSKVERB_VELVET"))
        {
            float v[10];
            int k = std::sscanf (ov, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
                &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);
            // Clamp like the setters (a 0/negative env T60 -> tau<=0 -> NaN/inf tap
            // gains; the NaN survives the gainEnv break-guard since NaN<x is false).
            if (k >= 5) { bandT60_[0]=std::clamp(v[0],0.02f,3.0f); band125T60_=std::clamp(v[1],0.05f,3.0f);
                          bandT60_[1]=std::clamp(v[2],0.02f,3.0f); bandT60_[2]=std::clamp(v[3],0.02f,3.0f); bandT60_[3]=std::clamp(v[4],0.02f,3.0f); }
            if (k >= 9) { bandLevelLin_[0]=std::pow(10.0f,v[5]/20.0f); bandLevelLin_[1]=std::pow(10.0f,v[6]/20.0f);
                          bandLevelLin_[2]=std::pow(10.0f,v[7]/20.0f); bandLevelLin_[3]=std::pow(10.0f,v[8]/20.0f); }
            if (k >= 10) flipFrac_ = std::clamp (v[9], 0.0f, 0.6f);
        }
    }

    double sampleRate_ = 48000.0;
    bool   prepared_   = false;
    int    writePos_ = 0, mask_ = 0;
    std::vector<float> ringL_[kBands], ringR_[kBands];
    Field  fieldL_[kBands], fieldR_[kBands];
    Biquad lp250_, lp500_, lp2k_;

    // baked 4-band defaults (anchor lex-reverse-1, hand-tuned 2026-06-17/18).
    float bandT60_[kBands]      = { 0.13f, 0.08f, 0.09f, 0.10f };
    float band125T60_          = 0.55f;
    float bandLevelLin_[kBands] = { 1.413f, 0.794f, 0.501f, 3.548f };  // {+3,-2,-6,+11} dB
    float flipFrac_            = 0.60f;  // in-clamp; ≥0.30 ⇒ always-flip the ~30% else-taps (the validated sound)
    float cellRateMs_[kBands]  = { 0.5f, 0.5f, 0.5f, 0.25f };
    float sizeScale_           = 1.0f;
    float decayScale_          = 1.0f;
};
