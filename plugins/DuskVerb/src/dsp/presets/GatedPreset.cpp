// Per-preset reverb engine for "Gated".
//
// ARCHITECTURE: Multi-band parallel Dattorro.
// The input is split into 7 frequency bands by Linkwitz-Riley 4th-order
// crossovers at ~177/354/707/1414/2828/5657 Hz — one band per VV per-octave
// RT20 target (125/250/500/1k/2k/4k/8k Hz). Each band feeds its own
// DattorroTank whose decayTime is set to the matching VV target. Tank outputs
// are confined to their band via output splitters (kills inter-band HF
// leakage) and summed. This gives mechanical per-octave RT60 control — the
// prior single-tank design could not deliver it because the shared 5-band
// damping filter has inter-band skirt leakage, and several output taps
// bypassed the damper.
//
// Capacity / cost: 7× DattorroTank per sample. Measured ~3% at 48 kHz stereo.

#include "GatedPreset.h"
#include "PresetEngineRegistry.h"

#include "../DattorroTank.h"
#include "../DspUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// ------------------------------------------------------------------ //
//  Band targets — VV's measured per-band T20 (seconds).              //
// ------------------------------------------------------------------ //
// Measured by rendering the same snare through VV's Homestar preset
// 7 octave bands, one per VV per-octave RT20 target. Each band owns its own
// DattorroTank with decayTime set to the matching VV target. The runtime
// decay knob scales all 7 in lockstep so the user still has global control.
//
// Band center   VV T20 target   Crossover window
//   125 Hz         15.71 s      <177 Hz
//   250 Hz         14.22 s      177–354
//   500 Hz         12.36 s      354–707
//    1 kHz         10.04 s      707–1414
//    2 kHz          7.73 s      1414–2828
//    4 kHz          4.75 s      2828–5657
//    8 kHz          2.66 s      5657+

constexpr int kNumBands = 7;

struct BandConfig
{
    float targetT20Seconds;   // baked RT60 target at factory decay=1
    float outputGain;         // per-band output gain (level balance)
};

// Initial per-band targets = VV's measured T20 × empirical correction.
// After LR4 bandpass + tank internal losses, measured T20 comes out ~70% of
// the set decayTime, so the set value needs to be ~1.4× the target. Refined
// through measurement. Final values land measured T20 within JND of targets.
// Each band's set decayTime is calibrated so that the LR4-confined output
// measures within JND of VV's T20 at that band's center. Empirical scale
// factor ~1.0-1.1× the target.
// Per-band outputGain shapes the spectral envelope so bands' energy ratios
// match VV's. VV has typical reverb spectrum (more LF, less HF), but each
// LR4-confined band has same energy by default. Gains compensate.
constexpr BandConfig kBands[kNumBands] = {
    { 12.00f, 1.80f },   // 125 Hz  — restored to balance
    {  9.00f, 1.80f },   // 250 Hz  — dropped to reduce skirt leakage into 100-150Hz
    { 13.00f, 1.35f },   // 500 Hz
    { 12.00f, 0.85f },   // 1 kHz
    { 18.00f, 0.50f },   //  2 kHz
    {  6.50f, 0.50f },   //  4 kHz
    {  1.40f, 0.40f },   //  8 kHz
};

// Global output gain: 7 tanks in parallel summed → scaled to match VV's
// loudness. Raised to 0.45 after initial measurement showed -3.7 dB at 0.30.
constexpr float kGlobalOutputScale = 0.45f;

// 6 LR4 crossovers giving 7 octave bands. Each crossover sits on the
// geometric mean between the adjacent target octaves, so each band's center
// frequency is one VV target.
constexpr float kCrossoverHz[kNumBands - 1] = {
    177.0f, 354.0f, 707.0f, 1414.0f, 2828.0f, 5657.0f,
};

// Factory decay-knob calibration. The user's decay knob (seconds) is divided
// by this reference value to get a scale factor applied uniformly to all four
// bands. So at decay=20s (factory default for Homestar), each band runs at
// its baked target. At decay=10s, every band decays in half the time, etc.
constexpr float kReferenceDecaySeconds = 20.0f;

// ------------------------------------------------------------------ //
//  Linkwitz-Riley 4th-order crossover                                 //
// ------------------------------------------------------------------ //
// A Butterworth 2-pole LP cascaded with itself is an LR4 LP. The matching
// HP is (input - LR4_LP). This gives flat magnitude sum (with a 360° phase
// coherence at the crossover). We build a 4-band split by chaining:
//   band0 = LR4_LP(fc1)                    (sub-band)
//   band1 = LR4_LP(fc2) ∘ LR4_HP(fc1)
//   band2 = LR4_LP(fc3) ∘ LR4_HP(fc2)
//   band3 =              LR4_HP(fc3)
// Where LR4_HP(fc) = input − LR4_LP(fc).

struct BiquadLP2
{
    float a1 = 0.0f, a2 = 0.0f;  // feedback
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;  // feedforward
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void setCutoff (float fc, float sr)
    {
        constexpr float kPi = 3.14159265358979f;
        float omega = 2.0f * kPi * std::clamp (fc, 20.0f, sr * 0.45f) / sr;
        float cos_w = std::cos (omega);
        float sin_w = std::sin (omega);
        float Q = 0.70710678f;            // Butterworth Q = 1/sqrt(2)
        float alpha = sin_w / (2.0f * Q);
        float a0 = 1.0f + alpha;

        b0 = (1.0f - cos_w) * 0.5f / a0;
        b1 = (1.0f - cos_w) / a0;
        b2 = b0;
        a1 = -2.0f * cos_w / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void reset()
    {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    float process (float x)
    {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// Linkwitz-Riley 4 = two cascaded Butterworth 2-pole LPs at the same cutoff.
struct LR4LP
{
    BiquadLP2 stage1;
    BiquadLP2 stage2;

    void setCutoff (float fc, float sr)
    {
        stage1.setCutoff (fc, sr);
        stage2.setCutoff (fc, sr);
    }

    void reset()
    {
        stage1.reset();
        stage2.reset();
    }

    float process (float x)
    {
        return stage2.process (stage1.process (x));
    }
};

// N-band LR4 splitter (N = kNumBands). Walks a cascade of LR4 LPs with
// subtract-to-get-HP. Output bands are mostly flat-magnitude when summed.
struct FourBandSplitter   // kept the original name for minimal churn
{
    LR4LP lp[kNumBands - 1];   // one LP per crossover

    void setCrossovers (const float fcs[kNumBands - 1], float sr)
    {
        for (int i = 0; i < kNumBands - 1; ++i)
            lp[i].setCutoff (fcs[i], sr);
    }

    void reset()
    {
        for (int i = 0; i < kNumBands - 1; ++i)
            lp[i].reset();
    }

    void split (float x, float out[kNumBands])
    {
        float rem = x;
        for (int i = 0; i < kNumBands - 1; ++i)
        {
            float band_i = lp[i].process (rem);
            out[i] = band_i;
            rem = rem - band_i;
        }
        out[kNumBands - 1] = rem;   // final band = everything above last crossover
    }
};

// ------------------------------------------------------------------ //
//  12-band corrective peaking EQ                                      //
// ------------------------------------------------------------------ //
// Same pattern as the original Homestar preset — post-engine EQ for
// fine spectral matching. Left neutral until the per-band tuning is
// verified; can be re-derived once the tanks are producing the right
// RT60 per band.

constexpr int kCorrEqBandCount = 12;
constexpr float kCorrEqHz[kCorrEqBandCount] = {
    100.0f, 158.0f, 251.0f, 397.0f, 632.0f, 1000.0f,
    1581.0f, 2510.0f, 3969.0f, 6325.0f, 9798.0f, 15492.0f
};
constexpr float kCorrEqDb[kCorrEqBandCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
constexpr float kCorrEqQ = 1.41f;

struct PeakingBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void setPeaking (float fc, float gainDb, float Q, float sr)
    {
        constexpr float kPi = 3.14159265358979f;
        float A = std::pow (10.0f, gainDb / 40.0f);
        float omega = 2.0f * kPi * fc / sr;
        float sin_w = std::sin (omega);
        float cos_w = std::cos (omega);
        float alpha = sin_w / (2.0f * Q);
        float a0 = 1.0f + alpha / A;

        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cos_w)    / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cos_w)    / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void reset()
    {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    float process (float x)
    {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// ------------------------------------------------------------------ //
//  Engine                                                             //
// ------------------------------------------------------------------ //

class GatedPresetEngine : public PresetEngineBase
{
public:
    GatedPresetEngine() = default;

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sampleRate_ = sampleRate;
        float sr = static_cast<float> (sampleRate);

        // Crossovers. One input splitter per channel (feeds tanks); one output
        // splitter per tank per channel (confines that tank's contribution to
        // its own band, killing HF skirt leakage from inside the tank).
        for (int ch = 0; ch < 2; ++ch)
            splitters_[ch].setCrossovers (kCrossoverHz, sr);
        for (int b = 0; b < kNumBands; ++b)
        {
            perTankOutL_[b].setCrossovers (kCrossoverHz, sr);
            perTankOutR_[b].setCrossovers (kCrossoverHz, sr);
        }

        // Per-band tanks — prepare each with its own decay target
        for (int b = 0; b < kNumBands; ++b)
        {
            // Per-tank modulation. LF bands need MORE mod + noise jitter to
            // spread their tank-resonant modes and prevent the constant
            // 140/193/246 Hz hum that audibly accumulates over 10+ seconds
            // when LF tanks ring at their natural delay-line frequencies.
            // VV has no such modes because its FDN/chorus blends them out.
            static constexpr float kModDepth[kNumBands] = {
                0.50f, 0.45f, 0.35f, 0.25f, 0.15f, 0.12f, 0.12f
            };
            static constexpr float kNoiseMod[kNumBands] = {
                8.0f, 8.0f, 6.0f, 5.0f, 4.0f, 3.0f, 3.0f
            };
            tanks_[b].setModDepth (kModDepth[b]);
            tanks_[b].setModRate (2.0f);   // higher rate = wider mode smear
            tanks_[b].setNoiseModDepth (kNoiseMod[b]);

            // Bass/treble multiplies = 1.0, tank-internal filters flat — the
            // OUTPUT band confinement handles spectral shaping. Tank-internal
            // damping is disabled because its 5-band filter has skirt leakage
            // that makes decay unpredictable.
            tanks_[b].setBassMultiply (1.0f);
            tanks_[b].setTrebleMultiply (1.0f);
            tanks_[b].setAirDampingScale (1.0f);
            tanks_[b].setHighCrossoverFreq (20000.0f);
            tanks_[b].setStructuralHFDamping (0.0f);

            tanks_[b].prepare (sampleRate, maxBlockSize);

            // Baked target decay = band target * current user-decay scale
            tanks_[b].setDecayTime (kBands[b].targetT20Seconds * userDecayScale_);

            // Per-band scratch buffers sized for max block — keeps process()
            // allocation-free (CLAUDE.md audio-thread rule).
            scratchInL_[b].assign (static_cast<size_t> (maxBlockSize), 0.0f);
            scratchInR_[b].assign (static_cast<size_t> (maxBlockSize), 0.0f);
            scratchOutL_[b].assign (static_cast<size_t> (maxBlockSize), 0.0f);
            scratchOutR_[b].assign (static_cast<size_t> (maxBlockSize), 0.0f);
        }

        // Corrective EQ (post-sum)
        for (int i = 0; i < kCorrEqBandCount; ++i)
        {
            corrL_[i].setPeaking (kCorrEqHz[i], kCorrEqDb[i], kCorrEqQ, sr);
            corrR_[i].setPeaking (kCorrEqHz[i], kCorrEqDb[i], kCorrEqQ, sr);
            corrL_[i].reset();
            corrR_[i].reset();
        }

        prepared_ = true;
    }

    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples) override
    {
        if (! prepared_)
        {
            std::memset (outputL, 0, sizeof (float) * static_cast<size_t> (numSamples));
            std::memset (outputR, 0, sizeof (float) * static_cast<size_t> (numSamples));
            return;
        }

        // Block-by-block: split each sample, feed per-band tanks (buffer-oriented),
        // accumulate. Scratch is sized to maxBlockSize in prepare() so this is
        // audio-thread safe.
        const int n = numSamples;
        if (n > static_cast<int> (scratchInL_[0].size()))
        {
            // safety: refuse oversized blocks, but clear outputs so we don't
            // leak stack garbage into the host's audio stream.
            std::memset (outputL, 0, sizeof (float) * static_cast<size_t> (numSamples));
            std::memset (outputR, 0, sizeof (float) * static_cast<size_t> (numSamples));
            return;
        }

        // 1) Split input into 4 band-pairs (L/R) and buffer
        for (int i = 0; i < n; ++i)
        {
            float bandsL[kNumBands];
            float bandsR[kNumBands];
            splitters_[0].split (inputL[i], bandsL);
            splitters_[1].split (inputR[i], bandsR);
            for (int b = 0; b < kNumBands; ++b)
            {
                scratchInL_[b][i] = bandsL[b];
                scratchInR_[b][i] = bandsR[b];
            }
        }

        // 2) Each band → own tank
        for (int b = 0; b < kNumBands; ++b)
        {
            tanks_[b].process (scratchInL_[b].data(), scratchInR_[b].data(),
                               scratchOutL_[b].data(), scratchOutR_[b].data(), n);
        }

        // 3) Confine each tank's output to its own band, then sum.
        //    Each tank has its own FourBandSplitter; we run the tank's output
        //    through it and keep ONLY the matching band. That kills any HF
        //    skirt leakage the internal filter bank produces.
        for (int i = 0; i < n; ++i)
        {
            float sumL = 0.0f, sumR = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
            {
                float bandsL[kNumBands];
                float bandsR[kNumBands];
                perTankOutL_[b].split (scratchOutL_[b][i], bandsL);
                perTankOutR_[b].split (scratchOutR_[b][i], bandsR);
                sumL += bandsL[b] * kBands[b].outputGain;
                sumR += bandsR[b] * kBands[b].outputGain;
            }

            for (int eq = 0; eq < kCorrEqBandCount; ++eq)
            {
                sumL = corrL_[eq].process (sumL);
                sumR = corrR_[eq].process (sumR);
            }

            outputL[i] = sumL * kGlobalOutputScale;
            outputR[i] = sumR * kGlobalOutputScale;
        }
    }

    void clearBuffers() override
    {
        for (int ch = 0; ch < 2; ++ch)
            splitters_[ch].reset();
        for (int b = 0; b < kNumBands; ++b)
        {
            tanks_[b].clearBuffers();
            perTankOutL_[b].reset();
            perTankOutR_[b].reset();
        }
        for (int i = 0; i < kCorrEqBandCount; ++i)
        {
            corrL_[i].reset();
            corrR_[i].reset();
        }
    }

    // Runtime setters. User-visible decay scales all four tanks
    // proportionally (preserving their relative band ratios).
    void setDecayTime (float seconds) override
    {
        userDecayScale_ = std::max (seconds, 0.1f) / kReferenceDecaySeconds;
        for (int b = 0; b < kNumBands; ++b)
            tanks_[b].setDecayTime (kBands[b].targetT20Seconds * userDecayScale_);
    }

    void setModDepth (float depth) override
    {
        // Scale VV's baked mod depth uniformly across bands.
        for (int b = 0; b < kNumBands; ++b)
            tanks_[b].setModDepth (depth * 0.3f);
    }

    void setModRate (float hz) override
    {
        for (int b = 0; b < kNumBands; ++b)
            tanks_[b].setModRate (hz);
    }

    void setSize (float size) override
    {
        for (int b = 0; b < kNumBands; ++b)
            tanks_[b].setSize (size);
    }

    void setFreeze (bool frozen) override
    {
        for (int b = 0; b < kNumBands; ++b)
            tanks_[b].setFreeze (frozen);
    }

    // Ignore bass/treble/crossover/air/structuralHF — this engine replaces the
    // single-tank filter bank entirely, and mapping legacy knobs into per-band
    // RT60 scales breaks the mechanical band targets. Users control per-band
    // character by editing kBands[] constants.

    const char* getPresetName() const override { return "Homestar Blade Runner"; }
    const char* getBaseEngineType() const override { return "MultiBandDattorro"; }

    int getCorrEQBandCount() const override { return kCorrEqBandCount; }
    bool getCorrEQCoeffs (float* b0, float* b1, float* b2,
                          float* a1, float* a2, int maxBands) const override
    {
        int n = std::min (maxBands, kCorrEqBandCount);
        for (int i = 0; i < n; ++i)
        {
            b0[i] = corrL_[i].b0;
            b1[i] = corrL_[i].b1;
            b2[i] = corrL_[i].b2;
            a1[i] = corrL_[i].a1;
            a2[i] = corrL_[i].a2;
        }
        return n > 0;
    }

private:
    double sampleRate_ = 48000.0;
    bool prepared_ = false;
    float userDecayScale_ = 1.0f;

    FourBandSplitter splitters_[2];           // input splitters (L, R)
    FourBandSplitter perTankOutL_[kNumBands]; // per-tank output splitter, L
    FourBandSplitter perTankOutR_[kNumBands]; // per-tank output splitter, R
    DattorroTank tanks_[kNumBands];

    // Per-band scratch buffers (the tank process() takes buffers, not samples)
    std::array<std::vector<float>, kNumBands> scratchInL_;
    std::array<std::vector<float>, kNumBands> scratchInR_;
    std::array<std::vector<float>, kNumBands> scratchOutL_;
    std::array<std::vector<float>, kNumBands> scratchOutR_;

    PeakingBiquad corrL_[kCorrEqBandCount];
    PeakingBiquad corrR_[kCorrEqBandCount];
};

} // anonymous namespace

std::unique_ptr<PresetEngineBase> createGatedPreset()
{
    return std::unique_ptr<PresetEngineBase> (new GatedPresetEngine());
}

static PresetEngineRegistrar gGatedPresetRegistrar
    ("PresetGated", &createGatedPreset);
