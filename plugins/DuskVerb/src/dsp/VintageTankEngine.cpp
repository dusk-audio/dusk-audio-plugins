#include "VintageTankEngine.h"

#include <algorithm>

namespace DspUtils {

// =============================================================================
// Helpers — small allocation-bearing prepare-time work
// =============================================================================

static int nextPow2 (int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void VintageTankEngine::DelayLine::prepare (int maxLenSamples)
{
    const int sz = nextPow2 (std::max (maxLenSamples + 4, 16));
    buf.assign (static_cast<size_t> (sz), 0.0f);
    mask     = sz - 1;
    writeIdx = 0;
}

void VintageTankEngine::DelayLine::clear()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    writeIdx = 0;
}

void VintageTankEngine::AllPassStatic::prepare (int delayLenSamples)
{
    delay = std::max (1, delayLenSamples);
    line.prepare (delay + 4);
}

void VintageTankEngine::AllPassModulated::prepare (float baseDelaySamples,
                                                    int   maxExcursionSamples)
{
    baseDelay = std::max (1.0f, baseDelaySamples);
    const int totalLen = static_cast<int> (std::ceil (baseDelay))
                       + std::max (maxExcursionSamples, 0) + 2;
    line.prepare (totalLen);
}

void VintageTankEngine::OnePoleLP::setCutoff (float hz, float sampleRate) noexcept
{
    static constexpr float kTwoPi = 6.283185307179586f;
    const float fc = std::clamp (hz, 1.0f, sampleRate * 0.49f);
    a = 1.0f - std::exp (-kTwoPi * fc / sampleRate);
    a = std::clamp (a, 0.0f, 1.0f);
}

void VintageTankEngine::ThreeBandDamper::prepare (float sampleRate) noexcept
{
    lowShelf  .prepare (sampleRate);
    peakingMid.prepare (sampleRate);
    highShelf .prepare (sampleRate);
    applyCoeffs();
}

void VintageTankEngine::ThreeBandDamper::reset() noexcept
{
    lowShelf  .reset();
    peakingMid.reset();
    highShelf .reset();
}

void VintageTankEngine::ThreeBandDamper::applyCoeffs() noexcept
{
    lowShelf  .setShelf (lowFc,  bassDb);
    peakingMid.setBand  (midFc,  midQ, midDb);
    highShelf .setShelf (highFc, trebleDb);
}

void VintageTankEngine::LFO::setRate (float hz, float sampleRate) noexcept
{
    static constexpr float kTwoPi = 6.283185307179586f;
    incr = kTwoPi * std::max (hz, 0.001f) / sampleRate;
}

// =============================================================================
// Engine lifecycle
// =============================================================================

void VintageTankEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = static_cast<float> (sampleRate);

    // Reference tuning derived from the Dattorro 1997 plate / Griesinger
    // hall convention. Lengths quoted at the original 29.761 kHz sample
    // rate; scaled here to the host rate so the room character is
    // sample-rate-independent.
    const float kRefSampleRate = 29761.0f;
    const float scale          = sampleRate_ / kRefSampleRate;

    // ─── 1. Input diffusion cascade — 4 series APs, increasing delay ────
    // Short → longer delays smear each input transient progressively.
    const int inputAPLens[kNumInputAPs] = {
        static_cast<int> (142.0f * scale),
        static_cast<int> (107.0f * scale),
        static_cast<int> (379.0f * scale),
        static_cast<int> (277.0f * scale),
    };
    for (int i = 0; i < kNumInputAPs; ++i)
    {
        inputAP_[i].prepare (inputAPLens[i]);
        inputAP_[i].coef = inputDiffCoef_;
    }

    // ─── 2. Tank branches — asymmetric per channel (prime-ratio bias) ────
    // L vs R delays differ by ~5-8% so the modal density between channels
    // is non-coincident. This is what produces the lateral stereo bloom.
    const float primaryLensRef[kNumChannels]                       = { 4453.0f, 4217.0f };
    const float secondaryLensRef[kNumChannels]                     = { 3720.0f, 3163.0f };
    const float modAPBaseLensRef[kNumChannels][kNumModAPsPerChan]  = {
        {  672.0f, 1800.0f },
        {  908.0f, 2656.0f },
    };

    const int maxExc = static_cast<int> (std::ceil (modDepthSamples_ * scale)) + 4;

    for (int c = 0; c < kNumChannels; ++c)
    {
        auto& br = branch_[c];
        br.primaryLen   = std::max (4, static_cast<int> (primaryLensRef[c]   * scale));
        br.secondaryLen = std::max (4, static_cast<int> (secondaryLensRef[c] * scale));
        br.primaryDelay  .prepare (br.primaryLen   + 4);
        br.secondaryDelay.prepare (br.secondaryLen + 4);

        for (int m = 0; m < kNumModAPsPerChan; ++m)
        {
            br.modAP[m].prepare (modAPBaseLensRef[c][m] * scale, maxExc);
            br.modAP[m].coef = tankDiffCoef_;
        }

        br.dampingLP.setCutoff (dampingHz_, sampleRate_);
        br.damper.prepare (sampleRate_);
        br.damper.lowFc    = lowCrossover_;
        br.damper.highFc   = hiCutHz_;
        br.damper.midFc    = std::sqrt (lowCrossover_ * hiCutHz_);
        br.damper.bassDb   = 20.0f * std::log10 (std::max (bassMult_,   0.01f));
        br.damper.midDb    = 20.0f * std::log10 (std::max (midMult_,    0.01f));
        br.damper.trebleDb = 20.0f * std::log10 (std::max (trebleMult_, 0.01f));
        br.damper.applyCoeffs();

        // Sparse output taps — pick 3 positions per line at
        // non-uniform fractions so the comb pattern is irregular.
        br.tapPrimary   = { br.primaryLen   / 4,
                            br.primaryLen   / 2,
                            (3 * br.primaryLen)   / 4 };
        br.tapSecondary = { br.secondaryLen / 5,
                            (2 * br.secondaryLen) / 5,
                            (4 * br.secondaryLen) / 5 };
    }

    // ─── 3. LFOs — hardware-matched, line-length-sorted assignment ──────
    // Telemetry on the previous (forward-mapped) build showed the bands
    // were INVERTED — fast LFOs were modulating long lines, slow LFOs
    // were modulating short lines. Frequency content of a modulated delay
    // line is dominated by its base length: short lines → high band,
    // long lines → low band. Sort ModAPs by base delay length and assign
    // accordingly:
    //
    //   shortest node L.modAP[0] (672 ref)  → 7.23 Hz (high band beat)
    //   next     R.modAP[0] (908 ref)       → 4.76 Hz (mid band beat)
    //   next     L.modAP[1] (1800 ref)      → 2.01 Hz (low-mid band beat)
    //   longest  R.modAP[1] (2656 ref)      → 1.56 Hz (bass band beat)
    //
    // setModRate() stays no-op — the modal-beat character is intrinsic to
    // the topology.
    // 2026-05-30 Path B revert: BH-anchor LFO rate alignment + process-loop
    // swap closed 2 of 4 mod gates (mid + high) but bass + lowmid stayed
    // failing because their envelopes are dominated by cross-band LFO
    // interference, not the per-line modulation. Net +2 fails (BH 10 → 12)
    // — vetoed. Hardware seeds restored verbatim. Reference: BH-7 stays
    // the locked deterministic floor at 10 fails.
    static constexpr float kHardwareLFORatesHz[kNumLFOs] = {
        7.23f,   // lfo_[0] → L.modAP[0] (shortest node → high band beat)
        2.01f,   // lfo_[1] → L.modAP[1] (long node → low-mid band beat)
        4.76f,   // lfo_[2] → R.modAP[0] (short node → mid band beat)
        1.56f,   // lfo_[3] → R.modAP[1] (longest node → bass band beat)
    };
    const float scaledDepth = modDepthSamples_ * scale;
    for (int i = 0; i < kNumLFOs; ++i)
    {
        lfo_[i].setRate (kHardwareLFORatesHz[i], sampleRate_);
        lfo_[i].depth = scaledDepth;
        lfo_[i].phase = static_cast<float> (i) * 1.5707963f;   // π/2 stagger
    }

    clearBuffers();
}

void VintageTankEngine::reset()
{
    clearBuffers();
}

void VintageTankEngine::clearBuffers()
{
    for (auto& ap : inputAP_)
        ap.line.clear();

    for (auto& br : branch_)
    {
        br.primaryDelay  .clear();
        br.secondaryDelay.clear();
        for (auto& mp : br.modAP)
            mp.line.clear();
        br.dampingLP.z = 0.0f;
        br.damper.reset();
    }

    lastBranchOut_[0] = 0.0f;
    lastBranchOut_[1] = 0.0f;
}

// =============================================================================
// Audio-thread process — allocation-free, branch-light, fully inlined
// =============================================================================

void VintageTankEngine::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;
    processBlock (buffer.getWritePointer (0),
                  buffer.getWritePointer (1),
                  numSamples);
}

void VintageTankEngine::processBlock (float* L, float* R, int numSamples)
{
    if (numSamples <= 0 || L == nullptr || R == nullptr)
        return;

    auto& brL = branch_[0];
    auto& brR = branch_[1];

    // Snapshot sizeScale_ once per block — it is updated only from the
    // message thread between blocks. Per-sample reads scale against this.
    const float sz = sizeScale_;
    const int primaryLenL   = std::max (1, static_cast<int> (brL.primaryLen   * sz));
    const int secondaryLenL = std::max (1, static_cast<int> (brL.secondaryLen * sz));
    const int primaryLenR   = std::max (1, static_cast<int> (brR.primaryLen   * sz));
    const int secondaryLenR = std::max (1, static_cast<int> (brR.secondaryLen * sz));
    const std::array<int, kOutputTapsPerLine> tapPriL   {
        std::max (1, static_cast<int> (brL.tapPrimary[0]   * sz)),
        std::max (1, static_cast<int> (brL.tapPrimary[1]   * sz)),
        std::max (1, static_cast<int> (brL.tapPrimary[2]   * sz))
    };
    const std::array<int, kOutputTapsPerLine> tapSecL   {
        std::max (1, static_cast<int> (brL.tapSecondary[0] * sz)),
        std::max (1, static_cast<int> (brL.tapSecondary[1] * sz)),
        std::max (1, static_cast<int> (brL.tapSecondary[2] * sz))
    };
    const std::array<int, kOutputTapsPerLine> tapPriR   {
        std::max (1, static_cast<int> (brR.tapPrimary[0]   * sz)),
        std::max (1, static_cast<int> (brR.tapPrimary[1]   * sz)),
        std::max (1, static_cast<int> (brR.tapPrimary[2]   * sz))
    };
    const std::array<int, kOutputTapsPerLine> tapSecR   {
        std::max (1, static_cast<int> (brR.tapSecondary[0] * sz)),
        std::max (1, static_cast<int> (brR.tapSecondary[1] * sz)),
        std::max (1, static_cast<int> (brR.tapSecondary[2] * sz))
    };

    for (int i = 0; i < numSamples; ++i)
    {
        // ─── 1) Mono-summed input → 4-stage diffusion cascade ────────────
        //
        // Sum L+R to a single mono path for diffusion (the figure-8 tank
        // re-stereoises via asymmetric branch delays + cross-coupling).
        // 4 series APs progressively smear the transient envelope while
        // keeping the magnitude response flat — this is what shatters the
        // first-reflection cluster into the dense ER comb that vintage
        // plates / hardware halls have.
        float diffused = 0.5f * (L[i] + R[i]);
        diffused = inputAP_[0].process (diffused);
        diffused = inputAP_[1].process (diffused);
        diffused = inputAP_[2].process (diffused);
        diffused = inputAP_[3].process (diffused);

        // ─── 2) Snapshot the cross-coupling feedback from the prior block ─
        //
        // Each branch reads the OTHER branch's last-sample output and
        // mixes it with a fraction of its own. crossCoupling_ = 1.0 means
        // pure figure-8 (Lex Hall style); 0.0 means parallel tanks (less
        // lateral bloom). 0.5 is the Griesinger sweet spot.
        const float xR = lastBranchOut_[1] * crossCoupling_
                       + lastBranchOut_[0] * (1.0f - crossCoupling_);
        const float xL = lastBranchOut_[0] * crossCoupling_
                       + lastBranchOut_[1] * (1.0f - crossCoupling_);

        // ─── 3) Left branch loop pass ─────────────────────────────────────
        //
        // Order: mod-AP → primary delay → mod-AP → secondary delay → damp.
        // Damping placed LAST so each recirculation pass loses HF, building
        // the late tail's natural darkening as it bounces around the loop.
        float lin = diffused + decayGain_ * xR;
        lin = brL.modAP[0].process (lin, lfo_[0].tick());

        brL.primaryDelay.write (lin);
        lin = brL.primaryDelay.readInt (primaryLenL);
        brL.primaryDelay.advance();

        lin = brL.modAP[1].process (lin, lfo_[1].tick());

        brL.secondaryDelay.write (lin);
        lin = brL.secondaryDelay.readInt (secondaryLenL);
        brL.secondaryDelay.advance();

        lin = brL.dampingLP.process (lin);
        lin = brL.damper.processL (lin);
        lastBranchOut_[0] = lin;

        // ─── 4) Right branch loop pass (symmetric, asymmetric delays) ────
        float rin = diffused + decayGain_ * xL;
        rin = brR.modAP[0].process (rin, lfo_[2].tick());

        brR.primaryDelay.write (rin);
        rin = brR.primaryDelay.readInt (primaryLenR);
        brR.primaryDelay.advance();

        rin = brR.modAP[1].process (rin, lfo_[3].tick());

        brR.secondaryDelay.write (rin);
        rin = brR.secondaryDelay.readInt (secondaryLenR);
        brR.secondaryDelay.advance();

        rin = brR.dampingLP.process (rin);
        rin = brR.damper.processR (rin);
        lastBranchOut_[1] = rin;

        // ─── 5) Sparse interleaved output taps ────────────────────────────
        //
        // Per Lex / Griesinger convention: each channel's output is built
        // from THIS channel's primary-line taps PLUS cross-taps from the
        // OTHER channel's secondary line. The sign alternation on cross-
        // taps breaks correlation between L and R late tails — that's the
        // wide, decorrelated stereo image hardware reverbs are known for.
        float outL = brL.primaryDelay  .readInt (tapPriL[0])
                   + brL.primaryDelay  .readInt (tapPriL[1])
                   + brL.primaryDelay  .readInt (tapPriL[2])
                   - brR.secondaryDelay.readInt (tapSecR[0])
                   + brR.secondaryDelay.readInt (tapSecR[2]);

        float outR = brR.primaryDelay  .readInt (tapPriR[0])
                   + brR.primaryDelay  .readInt (tapPriR[1])
                   + brR.primaryDelay  .readInt (tapPriR[2])
                   - brL.secondaryDelay.readInt (tapSecL[0])
                   + brL.secondaryDelay.readInt (tapSecL[2]);

        // Tap-sum scale (1.0 here) preserves the L↔R cross-correlation
        // structure between taps. Final wet-exit level is set by the
        // separate outputGain_ multiplier below — this lets the engine
        // glue normalise integrated RMS without disturbing the tap math.
        L[i] = outL * outputGain_;
        R[i] = outR * outputGain_;
    }
}

// =============================================================================
// Voicing setters — call from message thread or preset apply only
// =============================================================================

void VintageTankEngine::setDecay (float decaySeconds)
{
    // Knob-honesty calibration (2026-05-31). The round-trip formula below
    // (incl. kGroupDelayCompensation) produced a strongly WARPED map — measured
    // mid-band RT60 ≈ 1.9724·T^0.4331 at nominal size (2.9× too long at 0.5 s,
    // 0.47× at 12 s; the single-point kGroupDelayCompensation tuning only held
    // near 3 s). Invert that measured law UP FRONT so the displayed Decay knob
    // reads true RT60 across the range; the existing formula then maps the
    // pre-inverted value. internal = (R/C)^(1/P).
    decaySeconds = std::clamp (std::pow (std::max (0.05f, decaySeconds) / 1.9724f,
                                         1.0f / 0.4331f), 0.05f, 120.0f);

    // Total round-trip path for a signal in the figure-8 tank:
    //   L branch primary + L secondary + 1-sample cross-coupling
    // → R branch primary + R secondary + 1-sample cross-coupling
    // → back to L. decayGain_ is applied at EACH branch's input mix,
    // so the per-round-trip multiplier is decayGain_². The empirical
    // calibration constant accounts for damping LP losses, AP-coefficient
    // band-shaping, and cross-coupling decorrelation between the
    // asymmetric L/R branches — losses that pure ring-formula math
    // (1.5 × loopSec / RT60) under-estimates.
    //
    // Verified calibration: knob 2.80 s → observed Schroeder T60 @ 1 kHz
    // 2.78 s ± 5% (was 2.63 s under the old single-branch formula).
    //
    // setSize() scales primaryLen / secondaryLen reads at render time,
    // so the effective round-trip path scales with sizeScale_ — feed that
    // into the formula so changing room size doesn't desynchronise the
    // RT60 ↔ knob mapping.
    // Group-delay compensation. The previous round-trip-samples calculation
    // counted only the STATIC delay-line lengths. Inherent sample-domain
    // group delay from ModAP feedback tails, the 1-pole damping LP, and
    // 1-sample cross-coupling registers means the effective per-cycle
    // attenuation is higher than the bare ring-formula predicts, so for
    // any given knob the observed RT60 falls SHORT.
    //
    // To extend the observed RT60 by ratio R, the per-round-trip gain
    // needs to satisfy ln(g_new) = ln(g_old) / R. In this formula:
    //   ln(g) = −kRingExponent × roundTripSec × ln(10) / rt60
    // increasing g_new (longer RT60) requires the exponent to become
    // LESS negative, which means roundTripSec (in the numerator) gets
    // DIVIDED by the compensation factor, not multiplied.
    //
    // Empirical: BH knob=5.0 with no compensation → observed_T60_1k 3.79 s,
    // a uniform ~20 % deficit. 1/1.22 = 0.82× on the round-trip extends
    // the observed RT60 toward the 5.0 s target.
    constexpr float kGroupDelayCompensation = 1.22f;
    const float roundTripSamples = static_cast<float> (branch_[0].primaryLen
                                                     + branch_[0].secondaryLen
                                                     + branch_[1].primaryLen
                                                     + branch_[1].secondaryLen)
                                  * sizeScale_ / kGroupDelayCompensation;
    const float roundTripSec = roundTripSamples / sampleRate_;
    const float rt60 = std::max (decaySeconds, 0.01f);

    constexpr float kRingExponent = 1.5f;
    decayGain_ = std::exp (-kRingExponent * roundTripSec
                            * 2.30258509f / rt60);     // 2.302… = ln(10)
    // Stability cap. 0.98 (vs the old 0.95) allows the optimizer / longer
    // tail knob settings to push the loop closer to self-oscillation
    // without runaway — measured stable margin even at decayGain = 0.97
    // because cross-coupling decorrelation eats ~1 dB / round-trip.
    decayGain_ = std::clamp (decayGain_, 0.0f, 0.98f);
}

void VintageTankEngine::setDamping (float hz)
{
    // OnePoleLP provides the baseline broadband damping floor; the
    // 3-band shelf damper sits on top as per-band modifier.
    dampingHz_ = std::clamp (hz, 200.0f, sampleRate_ * 0.49f);
    for (auto& br : branch_)
        br.dampingLP.setCutoff (dampingHz_, sampleRate_);
}

void VintageTankEngine::setModRate (float /*hz*/)
{
    // No-op: VintageTank uses hardware-matched LFO frequencies (1.56,
    // 2.01, 4.76, 7.23 Hz) hardcoded at prepare time. The modal-beat
    // character is intrinsic to the topology, not user-tunable here.
    // Kept as a slot for engine-glue symmetry with the other tanks.
}

void VintageTankEngine::setModDepth (float samples)
{
    modDepthSamples_ = std::clamp (samples, 0.0f, 64.0f);
    const float scale       = sampleRate_ / 29761.0f;
    const float scaledDepth = modDepthSamples_ * scale;
    for (auto& l : lfo_)
        l.depth = scaledDepth;
}

void VintageTankEngine::setInputDiffusion (float coef)
{
    inputDiffCoef_ = std::clamp (coef, 0.0f, 0.85f);
    for (auto& ap : inputAP_)
        ap.coef = inputDiffCoef_;
}

void VintageTankEngine::setTankDiffusion (float coef)
{
    tankDiffCoef_ = std::clamp (coef, 0.0f, 0.85f);
    for (auto& br : branch_)
        for (auto& mp : br.modAP)
            mp.coef = tankDiffCoef_;
}

void VintageTankEngine::setCrossCoupling (float amount)
{
    crossCoupling_ = std::clamp (amount, 0.0f, 1.0f);
}

void VintageTankEngine::setSize (float normalized)
{
    // 0..1 input → 0.25..1.00 effective room size. Buffers stay full
    // length so the change is allocation-free and audio-thread safe.
    sizeScale_ = 0.25f + 0.75f * std::clamp (normalized, 0.0f, 1.0f);
}

// ─── 3-band damping setters ──────────────────────────────────────────────────

static inline float multToDb (float m)
{
    return 20.0f * std::log10 (std::max (m, 0.01f));
}

void VintageTankEngine::setBassMultiply (float multiplier)
{
    bassMult_ = std::clamp (multiplier, 0.10f, 4.0f);
    const float gainDb = multToDb (bassMult_);
    for (auto& br : branch_)
    {
        br.damper.bassDb = gainDb;
        br.damper.applyCoeffs();
    }
}

void VintageTankEngine::setMidMultiply (float multiplier)
{
    midMult_ = std::clamp (multiplier, 0.10f, 4.0f);
    const float gainDb = multToDb (midMult_);
    for (auto& br : branch_)
    {
        br.damper.midDb = gainDb;
        br.damper.applyCoeffs();
    }
}

void VintageTankEngine::setTrebleMultiply (float multiplier)
{
    trebleMult_ = std::clamp (multiplier, 0.10f, 4.0f);
    const float gainDb = multToDb (trebleMult_);
    for (auto& br : branch_)
    {
        br.damper.trebleDb = gainDb;
        br.damper.applyCoeffs();
    }
}

void VintageTankEngine::setHiCut (float hz)
{
    hiCutHz_ = std::clamp (hz, 1000.0f, 20000.0f);
    dampingHz_ = hiCutHz_;
    for (auto& br : branch_)
    {
        // OnePoleLP cutoff tracks Hi Cut — provides baseline broadband
        // damping floor that the 3-band shelves modulate around.
        br.dampingLP.setCutoff (dampingHz_, sampleRate_);
        // 3-band damper high-shelf corner also tracks Hi Cut so the
        // Treble Multiply shelf operates at the same band the LP is
        // attenuating, giving a unified HF damping character.
        br.damper.highFc = hiCutHz_;
        br.damper.midFc  = std::sqrt (lowCrossover_ * hiCutHz_);
        br.damper.applyCoeffs();
    }
}

void VintageTankEngine::setLowCrossover (float hz)
{
    lowCrossover_ = std::clamp (hz, 50.0f, 1000.0f);
    for (auto& br : branch_)
    {
        br.damper.lowFc = lowCrossover_;
        br.damper.midFc = std::sqrt (lowCrossover_ * hiCutHz_);
        br.damper.applyCoeffs();
    }
}

// ─── Post-tank output gain ───────────────────────────────────────────────────

void VintageTankEngine::setOutputGain (float linear)
{
    outputGain_ = std::clamp (linear, 0.0f, 8.0f);
}

// ─── Per-LFO rate exposure (un-hardcode the modulation matrix) ───────────────

void VintageTankEngine::setLFORate (int index, float hz)
{
    if (index < 0 || index >= kNumLFOs) return;
    lfo_[index].setRate (std::clamp (hz, 0.05f, 25.0f), sampleRate_);
}

} // namespace DspUtils
