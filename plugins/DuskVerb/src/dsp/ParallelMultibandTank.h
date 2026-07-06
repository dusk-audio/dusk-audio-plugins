#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ParallelMultibandTank — DuskVerb's decoupling engine (2026-07-04).
//
// WHY: every recirculating tank in the fleet shares one structural wall —
// a single gain per band inside a shared loop sets level AND decay AND
// early-decay-shape together (the "level-vs-ring" coupling documented across
// the T60/boom/body/piano-band gate families). Tuning one always trades the
// others, on every stimulus (noiseburst, snare, sine, piano).
//
// THIS ENGINE: split the input into 6 complementary bands (subtractive tree of
// 4th-order Butterworth lowpasses — exact reconstruction by telescoping sum;
// upgraded from one-pole 2026-07-06: 6 dB/oct skirts leaked so much that a
// short band flanked by long bands realized its NEIGHBOURS' T60, the pilot's
// band-edge ±12-28% wall), give EACH band its own small recirculating tank
// (4-line FDN,
// Hadamard-4, per-line allpass densifier), and give each band four
// INDEPENDENT controls:
//
//   t60[b]    — the band's decay time (per-line feedback gain)
//   level[b]  — the band's OUTPUT trim (decoupled from decay: applied after
//               the loop, so a long ring no longer forces a hot level)
//   direct[b] — feed-forward band add (front-load / EDT shaping without
//               touching the recirculation)
//   width[b]  — per-band stereo width (M/S at the band output; the anchors'
//               "mono highs over wide broadband" signature)
//
// Low-band lines are LONG and UNMODULATED: the 50-75 Hz sub-skirt smear
// measured on sustained piano is delay-mod sidebands accumulating in the
// tank — bands 0-1 use static delays (no wander), bands 2+ get a gentle
// wander for liveliness where the smear is inaudible.
//
// Allocation only in prepare(); process() is allocation/lock/IO-free.
class ParallelMultibandTank
{
public:
    static constexpr int kBands = 6;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr_ = static_cast<float> (sampleRate);
        const float rr = sr_ / 48000.0f;
        // Per-band base line lengths (samples @48k). Lower bands: long lines
        // (wavelength-appropriate, dense modal spacing at low freq); higher
        // bands: short lines (fast echo density). 4 mutually-prime-ish lengths
        // per band, spread ~1:1.6.
        // 8 lines per band since 2026-07-06 (was 4): the 4-line tanks read
        // "springy/bouncy" by ear (audible 40-150 ms loop periodicity) and
        // thin (hf_tail_kurt ~+17, sparse modes). Doubling the line count
        // doubles modal density and breaks the repetition period.
        static const int base[kBands][kLines] = {
            { 4801, 5231, 5651, 6089, 6607, 7057, 7523, 8093 },   // <120 Hz
            { 3167, 3467, 3767, 4093, 4409, 4759, 5087, 5443 },   // 120-350
            { 2039, 2243, 2477, 2683, 2917, 3137, 3343, 3559 },   // 350-1k
            { 1259, 1381, 1531, 1663, 1811, 1949, 2083, 2221 },   // 1k-3k
            {  769,  853,  941, 1031, 1123, 1213, 1301, 1409 },   // 3k-8k
            {  467,  521,  571,  619,  677,  733,  787,  853 },   // >8k
        };
        for (int b = 0; b < kBands; ++b)
            for (int l = 0; l < kLines; ++l)
            {
                len_[b][l] = std::max (16, static_cast<int> (std::round (base[b][l] * rr * sizeScale_)));
                int cap = 1; while (cap < len_[b][l] + kMaxExc + 4) cap <<= 1;
                buf_[b][l].assign (static_cast<size_t> (cap), 0.0f);
                mask_[b][l] = cap - 1;
                wp_[b][l] = 0;
            }
        // Per-band in-loop allpass densifiers (2 per line, short primes).
        // Third AP stage on the top two bands only (2026-07-06): their static
        // 4-line tanks read spiky (hf_tail_kurt ~28 vs anchor ~13). A third,
        // longer allpass multiplies late echo density where it is missing.
        // 0 = stage absent (allpass() passes through).
        static const int apbase[kBands][kAps] = {
            { 431, 293, 0 }, { 347, 239, 0 }, { 283, 197, 0 },
            { 211, 149, 0 }, { 157, 107, 193 }, { 113, 79, 139 } };
        for (int b = 0; b < kBands; ++b)
            for (int l = 0; l < kLines; ++l)
                for (int a = 0; a < kAps; ++a)
                {
                    if (apbase[b][a] == 0) { apLen_[b][l][a] = 0; apBuf_[b][l][a].assign (8, 0.0f); apMask_[b][l][a] = 7; apWp_[b][l][a] = 0; continue; }
                    const int alen = std::max (8, static_cast<int> (std::round (apbase[b][a] * rr * (1.0f + 0.13f * l))));
                    int cap = 1; while (cap < alen + 4) cap <<= 1;
                    apBuf_[b][l][a].assign (static_cast<size_t> (cap), 0.0f);
                    apMask_[b][l][a] = cap - 1;
                    apLen_[b][l][a]  = alen;
                    apWp_[b][l][a]   = 0;
                }
        // Split filters + LFOs. 4th-order Butterworth lowpass per split point
        // (two cascaded RBJ biquads, Q = 0.5412 / 1.3066). The subtractive tree
        // (band k = LPk − LP(k−1), top = v − LP4) telescopes to the input for
        // ANY lowpass, so reconstruction stays exact; the old single one-pole
        // (6 dB/oct) leaked so badly that a short band flanked by long bands
        // realized the NEIGHBOURS' T60 (measured 2026-07-06: commanded
        // 0.5/0.4/0.3 s at 250/2k/16k read 2.09/1.81/1.93 s = +318..543%).
        // 24 dB/oct skirts push the leak floor low enough that a band's own
        // decay owns the Schroeder fit window.
        static const float xover[kBands - 1] = { 120.0f, 350.0f, 1000.0f, 3000.0f, 8000.0f };
        // stages 0-1: Linkwitz-Riley 4th order = two cascaded Butterworth-2
        // sections (Q = 0.7071) — input split chain;
        // stages 0-3: Butterworth 8th-order quad (output confinement).
        static const float stageQ4[2] = { 0.70710678f, 0.70710678f };
        static const float stageQ8[4] = { 0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f };
        auto designLP = [this] (float fc, int st, BQ& c, bool eighth = false)
        {
            const float q = eighth ? stageQ8[st] : stageQ4[st];
            const float w0 = 6.2831853f * fc / sr_;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float al = sw / (2.0f * q);
            const float a0 = 1.0f + al;
            c.b0 = ((1.0f - cw) * 0.5f) / a0;
            c.b1 = (1.0f - cw) / a0;
            c.b2 = ((1.0f - cw) * 0.5f) / a0;
            c.a1 = (-2.0f * cw) / a0;
            c.a2 = (1.0f - al) / a0;
        };
        auto designHP = [this] (float fc, int st, BQ& c, bool eighth = false)
        {
            const float q = eighth ? stageQ8[st] : stageQ4[st];
            const float w0 = 6.2831853f * fc / sr_;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float al = sw / (2.0f * q);
            const float a0 = 1.0f + al;
            c.b0 = ((1.0f + cw) * 0.5f) / a0;
            c.b1 = -(1.0f + cw) / a0;
            c.b2 = ((1.0f + cw) * 0.5f) / a0;
            c.a1 = (-2.0f * cw) / a0;
            c.a2 = (1.0f - al) / a0;
        };
        for (int k = 0; k < kBands - 1; ++k)
            for (int st = 0; st < 2; ++st)
            {
                designLP (xover[k], st, splitLpBq_[k][st]);
                designHP (xover[k], st, splitHpBq_[k][st]);
            }
        // Tank-output band confinement, 8th-order Butterworth per side. The
        // input split alone is not enough: a band's tank rings whatever leaks
        // IN at that band's (possibly long) T60. Because the leak DECAYS
        // SLOWER than a short band's own energy, it crosses over in time no
        // matter its level — Schroeder then integrates it and the band reads
        // long (measured 2026-07-06 with LP4-only isolation: b0's 250 Hz leak
        // sat only ~12 dB under b1's own tail energy → 250 Hz cmd 0.5 s read
        // 1.9 s). Suppression must make the leak's total ENERGY negligible
        // (≪1%), which needs ~-100 dB combined skirts at one octave past the
        // edge: LP4 input (24 dB/oct) × 8th-order output (48 dB/oct).
        for (int b = 0; b < kBands; ++b)
        {
            int n = 0;
            if (b > 0)
                for (int st = 0; st < 4; ++st) designHP (xover[b - 1], st, outBq_[b][n++], true);
            if (b < kBands - 1)
                for (int st = 0; st < 4; ++st) designLP (xover[b], st, outBq_[b][n++], true);
            outNb_[b] = n;
        }
        lfoPh_ = 0.0f;
        lfoInc_ = 6.2831853f * 0.61f / sr_;   // gentle 0.61 Hz wander (bands 2+ only)
        prepared_ = true;
        clear();
        updateGains();
    }

    void clear()
    {
        for (int b = 0; b < kBands; ++b)
            for (int l = 0; l < kLines; ++l)
            {
                std::fill (buf_[b][l].begin(), buf_[b][l].end(), 0.0f);
                for (int a = 0; a < kAps; ++a) std::fill (apBuf_[b][l][a].begin(), apBuf_[b][l][a].end(), 0.0f);
                damp_[b][l] = 0.0f;
            }
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kBands - 1; ++k)
                for (int st = 0; st < 4; ++st)
                    splitZ_[c][k][st][0] = splitZ_[c][k][st][1] = 0.0f;
            for (int b = 0; b < kBands; ++b)
                for (int f = 0; f < 8; ++f)
                    outZ_[c][b][f][0] = outZ_[c][b][f][1] = 0.0f;
        }
    }

    // ── Per-band config ─────────────────────────────────────────────────────
    void setBand (int b, float t60s, float level, float direct, float width)
    {
        if (b < 0 || b >= kBands) return;
        t60_[b]    = std::clamp (t60s, 0.05f, 30.0f);
        level_[b]  = std::clamp (level, 0.0f, 4.0f);
        direct_[b] = std::clamp (direct, 0.0f, 2.0f);
        width_[b]  = std::clamp (width, 0.0f, 2.0f);
        if (prepared_) updateGains();
    }
    // Decay knob: scales every band's T60 (reference-coupled like the GEQ forks).
    void setDecayScale (float s) { decayScale_ = std::clamp (s, 0.05f, 8.0f); if (prepared_) updateGains(); }
    void setSize (float s01)
    {
        // Size rescales line lengths — allocation-free (buffers reserved at max).
        const float ns = 0.75f + std::clamp (s01, 0.0f, 1.0f) * 0.5f;
        if (std::abs (ns - sizeScale_) < 1e-4f) return;
        sizeScale_ = ns;
        // NOTE: lengths were allocated with sizeScale_ at prepare; changing size
        // here only adjusts gains (length change would need re-prepare). Kept
        // simple for the pilot: Size acts through updateGains' t60 mapping.
        if (prepared_) updateGains();
    }
    void setFreeze (bool f) { frozen_ = f; if (prepared_) updateGains(); }

    // ── Process ─────────────────────────────────────────────────────────────
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (! prepared_) { std::fill (outL, outL + n, 0.0f); std::fill (outR, outR + n, 0.0f); return; }
        for (int s = 0; s < n; ++s)
        {
            lfoPh_ += lfoInc_; if (lfoPh_ > 6.2831853f) lfoPh_ -= 6.2831853f;
            const float wob = std::sin (lfoPh_);

            // Complementary band split, per channel.
            float bandsL[kBands], bandsR[kBands];
            splitBands (inL[s], 0, bandsL);
            splitBands (inR[s], 1, bandsR);

            float oL = 0.0f, oR = 0.0f;
            for (int b = 0; b < kBands; ++b)
            {
                // per-band gentle wander (bands 2+ only — static lows kill the
                // mod-sideband sub-skirt smear).
                const float exc = (b >= 2) ? wob * kExcSamp[b] : 0.0f;

                // 8-line mini-FDN: read (modulated), Hadamard-8, damp+gain, write.
                float x[kLines];
                for (int l = 0; l < kLines; ++l)
                    x[l] = readLine (b, l, (l & 1) ? -exc : exc);
                // inject band input into lines 0/1 (L) and 2/3 (R), sign-alternated
                x[0] += bandsL[b]; x[1] -= bandsL[b];
                x[2] += bandsR[b]; x[3] -= bandsR[b];
                hadamard8 (x);
                for (int l = 0; l < kLines; ++l)
                {
                    // in-loop damping is unnecessary per band (the band IS the
                    // spectral slice); just the decay gain + densifier APs.
                    float v = x[l] * g_[b];
                    v = allpass (b, l, 0, v);
                    v = allpass (b, l, 1, v);
                    v = allpass (b, l, 2, v);
                    writeLine (b, l, v);
                }
                // band output taps: decorrelated L/R combinations across four
                // lines each (incl. two feedback-only lines: denser output),
                // confined to the band's own range (leak-rings-long defect).
                float bl = bandFilter (b, 0, 0.35355339f * (x[0] - x[3] + x[5] - x[6]));
                float br = bandFilter (b, 1, 0.35355339f * (x[2] - x[1] + x[7] - x[4]));
                // feed-forward direct (EDT/front-load, outside the loop —
                // unfiltered: it is already band-split and must stay
                // phase-tight for EDT shaping)
                bl += direct_[b] * bandsL[b];
                br += direct_[b] * bandsR[b];
                // per-band width (M/S) + decoupled output level
                const float m = 0.5f * (bl + br), sd = 0.5f * (bl - br) * width_[b];
                oL += level_[b] * (m + sd);
                oR += level_[b] * (m - sd);
            }
            outL[s] = oL;
            outR[s] = oR;
        }
    }

private:
    static constexpr int kLines  = 8;
    static constexpr int kMaxExc = 16;
    static constexpr float kExcSamp[kBands] = { 0.0f, 0.0f, 1.5f, 2.0f, 0.0f, 0.0f };   // 2026-07-04 b4/b5 4,5 -> 2,1; 2026-07-06 -> 0,0: ANY wander through LINEAR-interp reads is a per-pass HF loss (measured: 8k realized -24% below command even at ±2 samples). exc==0 takes integer reads (fr=0) = zero interp loss; the top bands are noise-like and need no wander. b2/b3 2,3 -> 1.5,2 with the 8-line tanks: 8 modulated lines pushed tail mod-ripple over gate (+1.6/+1.9 dB).

    struct BQ { float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    static float runBq (const BQ& c, float* z, float x)
    {
        const float y = c.b0 * x + z[0];
        z[0] = c.b1 * x - c.a1 * y + z[1];
        z[1] = c.b2 * x - c.a2 * y;
        return y;
    }

    void splitBands (float v, int ch, float* bands)
    {
        // Serial Linkwitz-Riley-4 crossover chain: band k = LP4@xk(rest),
        // rest = HP4@xk(rest). Every band's lower-side rejection is a REAL
        // highpass skirt (cumulative through the chain), not a subtractive
        // cancellation. The earlier LPk−LP(k−1) tree relied on two lowpasses
        // agreeing in PHASE below the band — near their cutoffs they don't,
        // so mid bands leaked ~-12 dB skirts downward and a long band rang
        // audibly inside a short neighbour's octave (2026-07-06 measurement).
        // Sum of bands ≈ allpassed input (LR property) — fine for a reverb
        // feed; the tanks decorrelate phase anyway.
        float rest = v;
        for (int k = 0; k < kBands - 1; ++k)
        {
            float lo = rest, hi = rest;
            for (int st = 0; st < 2; ++st)
            {
                lo = runBq (splitLpBq_[k][st], splitZ_[ch][k][st],     lo);
                hi = runBq (splitHpBq_[k][st], splitZ_[ch][k][st + 2], hi);
            }
            bands[k] = lo;
            rest = hi;
        }
        bands[kBands - 1] = rest;
    }

    float readLine (int b, int l, float exc)
    {
        // linear-interpolated modulated read
        const float rpos = static_cast<float> (wp_[b][l]) - (static_cast<float> (len_[b][l]) + exc);
        const int   i0   = static_cast<int> (std::floor (rpos));
        const float fr   = rpos - static_cast<float> (i0);
        const int   m    = mask_[b][l];
        const float a = buf_[b][l][static_cast<size_t> (i0 & m)];
        const float c = buf_[b][l][static_cast<size_t> ((i0 + 1) & m)];
        return a + fr * (c - a);
    }
    void writeLine (int b, int l, float v)
    {
        buf_[b][l][static_cast<size_t> (wp_[b][l])] = v;
        wp_[b][l] = (wp_[b][l] + 1) & mask_[b][l];
    }
    float allpass (int b, int l, int a, float v)
    {
        if (apLen_[b][l][a] == 0) return v;   // absent stage (lower bands)
        auto& bufr = apBuf_[b][l][a];
        const int m = apMask_[b][l][a];
        int& w = apWp_[b][l][a];
        const float d = bufr[static_cast<size_t> ((w - apLen_[b][l][a]) & m)];
        const float k = kApCoeff[b];
        const float y = d - k * v;
        bufr[static_cast<size_t> (w)] = v + k * y;
        w = (w + 1) & m;
        return y;
    }
    static void hadamard8 (float* x)
    {
        for (int len = 1; len < 8; len <<= 1)
            for (int i = 0; i < 8; i += (len << 1))
                for (int j = i; j < i + len; ++j)
                {
                    const float a = x[j], c = x[j + len];
                    x[j] = a + c; x[j + len] = a - c;
                }
        for (int j = 0; j < 8; ++j) x[j] *= 0.35355339f;   // 1/sqrt(8) — energy-preserving
    }

    void updateGains()
    {
        for (int b = 0; b < kBands; ++b)
        {
            const float t = frozen_ ? 1.0e9f : t60_[b] * decayScale_;
            // mean line length for the band sets the per-pass gain. Size can't
            // change the reserved buffer lengths at runtime (allocation is
            // prepare-only), so it acts HERE: sizeScale_ scales the EFFECTIVE
            // loop length, so a bigger Size lengthens decay + densifies the band.
            // sizeScale_ == 1.0 (default) -> identical to the fixed-length
            // mapping (bit-null for the pilot's baked config).
            // Loop length per pass = delay line + BOTH in-loop allpass
            // densifiers (their group delay is real loop time; omitting them
            // made realized T60 read ~+14% long on every band, measured
            // 2026-07-06 solo-band: cmd 0.5 s → 0.67 s), times an empirical
            // per-band factor: an allpass rings BEYOND its nominal delay
            // (k^n echo tail, k=0.55), and the effect grows with AP-to-line
            // length ratio — largest on the short high bands. Factors measured
            // 2026-07-06 (48 kHz, realized/commanded on an anchor-shaped
            // curve); they are sr-invariant to first order because every
            // length above scales with rr.
            static constexpr float kLoopAdj[kBands] = { 1.03f, 1.03f, 1.10f, 1.06f, 1.22f, 1.33f };
            float meanLen = 0.0f;
            for (int l = 0; l < kLines; ++l)
                meanLen += static_cast<float> (len_[b][l] + apLen_[b][l][0] + apLen_[b][l][1] + apLen_[b][l][2]);
            meanLen *= (1.0f / static_cast<float> (kLines)) * sizeScale_ * kLoopAdj[b];
            g_[b] = std::min (0.9995f, std::pow (10.0f, -3.0f * meanLen / (t * sr_)));
        }
    }

    // Per-band densifier coefficient. Tested 2026-07-06: RAISING k for the
    // upper bands made hf_tail_kurt WORSE (28→31) — higher k rings the AP
    // longer but its k^n echo train is SPARSER late. Keep 0.55 everywhere;
    // density comes from the third AP stage on the top bands instead.
    static constexpr float kApCoeff[kBands] = { 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f };

    float sr_ = 48000.0f, sizeScale_ = 1.0f, decayScale_ = 1.0f;
    bool  prepared_ = false, frozen_ = false;

    float t60_[kBands]    = { 2.0f, 2.2f, 2.0f, 1.6f, 1.2f, 0.8f };
    float level_[kBands]  = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float direct_[kBands] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float width_[kBands]  = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float g_[kBands]      = {};

    std::vector<float> buf_[kBands][kLines];
    int  len_[kBands][kLines] = {}, mask_[kBands][kLines] = {}, wp_[kBands][kLines] = {};
    float damp_[kBands][kLines] = {};
    static constexpr int kAps = 3;
    std::vector<float> apBuf_[kBands][kLines][kAps];
    int  apLen_[kBands][kLines][kAps] = {}, apMask_[kBands][kLines][kAps] = {}, apWp_[kBands][kLines][kAps] = {};

    float bandFilter (int b, int ch, float v)
    {
        for (int f = 0; f < outNb_[b]; ++f)
        {
            const BQ& c = outBq_[b][f];
            float* z = outZ_[ch][b][f];
            const float x = v;
            v = c.b0 * x + z[0];
            z[0] = c.b1 * x - c.a1 * v + z[1];
            z[1] = c.b2 * x - c.a2 * v;
        }
        return v;
    }

    BQ    splitLpBq_[kBands - 1][2];
    BQ    splitHpBq_[kBands - 1][2];
    float splitZ_[2][kBands - 1][4][2] = {};   // [ch][xover][lp0,lp1,hp0,hp1][z]
    BQ    outBq_[kBands][8];
    int   outNb_[kBands] = {};
    float outZ_[2][kBands][8][2] = {};
    float lfoPh_ = 0.0f, lfoInc_ = 0.0f;
};
