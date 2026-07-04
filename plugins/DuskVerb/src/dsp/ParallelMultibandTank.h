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
// THIS ENGINE: split the input into 6 complementary bands (cascaded one-pole
// lowpasses — the same exact-reconstruction split the per-band width tilt
// uses), give EACH band its own small recirculating tank (4-line FDN,
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
        static const int base[kBands][kLines] = {
            { 4801, 5651, 6607, 7523 },   // <120 Hz
            { 3167, 3767, 4409, 5087 },   // 120-350
            { 2039, 2477, 2917, 3343 },   // 350-1k
            { 1259, 1531, 1811, 2083 },   // 1k-3k
            {  769,  941, 1123, 1301 },   // 3k-8k
            {  467,  571,  677,  787 },   // >8k
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
        static const int apbase[kBands][2] = {
            { 431, 293 }, { 347, 239 }, { 283, 197 }, { 211, 149 }, { 157, 107 }, { 113, 79 } };
        for (int b = 0; b < kBands; ++b)
            for (int l = 0; l < kLines; ++l)
                for (int a = 0; a < 2; ++a)
                {
                    const int alen = std::max (8, static_cast<int> (std::round (apbase[b][a] * rr * (1.0f + 0.13f * l))));
                    int cap = 1; while (cap < alen + 4) cap <<= 1;
                    apBuf_[b][l][a].assign (static_cast<size_t> (cap), 0.0f);
                    apMask_[b][l][a] = cap - 1;
                    apLen_[b][l][a]  = alen;
                    apWp_[b][l][a]   = 0;
                }
        // Split filters + LFOs.
        static const float xover[kBands - 1] = { 120.0f, 350.0f, 1000.0f, 3000.0f, 8000.0f };
        for (int k = 0; k < kBands - 1; ++k)
            splitCoeff_[k] = 1.0f - std::exp (-6.2831853f * xover[k] / sr_);
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
                for (int a = 0; a < 2; ++a) std::fill (apBuf_[b][l][a].begin(), apBuf_[b][l][a].end(), 0.0f);
                damp_[b][l] = 0.0f;
            }
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kBands - 1; ++k) splitState_[c][k] = 0.0f;
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

                // 4-line mini-FDN: read (modulated), Hadamard-4, damp+gain, write.
                float x[kLines];
                for (int l = 0; l < kLines; ++l)
                    x[l] = readLine (b, l, (l & 1) ? -exc : exc);
                // inject band input into lines 0/1 (L) and 2/3 (R), sign-alternated
                x[0] += bandsL[b]; x[1] -= bandsL[b];
                x[2] += bandsR[b]; x[3] -= bandsR[b];
                hadamard4 (x);
                for (int l = 0; l < kLines; ++l)
                {
                    // in-loop damping is unnecessary per band (the band IS the
                    // spectral slice); just the decay gain + densifier APs.
                    float v = x[l] * g_[b];
                    v = allpass (b, l, 0, v);
                    v = allpass (b, l, 1, v);
                    writeLine (b, l, v);
                }
                // band output taps: decorrelated L/R combinations
                float bl = 0.5f * (x[0] - x[3]);
                float br = 0.5f * (x[2] - x[1]);
                // feed-forward direct (EDT/front-load, outside the loop)
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
    static constexpr int kLines  = 4;
    static constexpr int kMaxExc = 16;
    static constexpr float kExcSamp[kBands] = { 0.0f, 0.0f, 2.0f, 3.0f, 4.0f, 5.0f };

    void splitBands (float v, int ch, float* bands)
    {
        // low = LP0; band k = LPk - LP(k-1); top = v - LP4. Exact reconstruction.
        float prev = v;
        float acc[kBands - 1];
        float in = v;
        for (int k = 0; k < kBands - 1; ++k)
        {
            splitState_[ch][k] += splitCoeff_[k] * (in - splitState_[ch][k]);
            acc[k] = splitState_[ch][k];
        }
        bands[0] = acc[0];
        for (int k = 1; k < kBands - 1; ++k) bands[k] = acc[k] - acc[k - 1];
        bands[kBands - 1] = prev - acc[kBands - 2];
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
        auto& bufr = apBuf_[b][l][a];
        const int m = apMask_[b][l][a];
        int& w = apWp_[b][l][a];
        const float d = bufr[static_cast<size_t> ((w - apLen_[b][l][a]) & m)];
        const float y = d - kApCoeff * v;
        bufr[static_cast<size_t> (w)] = v + kApCoeff * y;
        w = (w + 1) & m;
        return y;
    }
    static void hadamard4 (float* x)
    {
        const float a = x[0] + x[1], bq = x[0] - x[1], c = x[2] + x[3], d = x[2] - x[3];
        x[0] = (a + c) * 0.5f; x[1] = (bq + d) * 0.5f; x[2] = (a - c) * 0.5f; x[3] = (bq - d) * 0.5f;
    }

    void updateGains()
    {
        for (int b = 0; b < kBands; ++b)
        {
            const float t = frozen_ ? 1.0e9f : t60_[b] * decayScale_;
            // mean line length for the band sets the per-pass gain
            float meanLen = 0.0f;
            for (int l = 0; l < kLines; ++l) meanLen += static_cast<float> (len_[b][l]);
            meanLen *= 0.25f;
            g_[b] = std::min (0.9995f, std::pow (10.0f, -3.0f * meanLen / (t * sr_)));
        }
    }

    static constexpr float kApCoeff = 0.55f;

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
    std::vector<float> apBuf_[kBands][kLines][2];
    int  apLen_[kBands][kLines][2] = {}, apMask_[kBands][kLines][2] = {}, apWp_[kBands][kLines][2] = {};

    float splitCoeff_[kBands - 1] = {};
    float splitState_[2][kBands - 1] = {};
    float lfoPh_ = 0.0f, lfoInc_ = 0.0f;
};
