// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQDSP.cpp — implementation of the framework-free 4K console EQ core.

#include "FourKEQDSP.hpp"

#include <algorithm>
#include <cmath>

namespace duskaudio
{

static inline float dbToGain(float db) noexcept { return std::pow(10.0f, 0.05f * db); }
static inline float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

//==============================================================================
// Static coefficient designers (shared with the UI response curve)
//==============================================================================
// Mid-band Q voicing, applied ONCE (consolePeak no longer scales Q internally).
// Matches the hardware topology confirmed by SSL/Waves docs:
//   E-series (Brown): CONSTANT-Q — bandwidth is fixed regardless of gain
//                     ("as the boost grows the base of the mountain stays put").
//   G-series (Black): PROPORTIONAL-Q — Q narrows as boost/cut increases, so the
//                     perceived energy change stays roughly constant.
// (Earlier this file applied proportional-Q to BOTH — and to Black twice. Both
//  were wrong; this is the single, correct, per-console application.)
float FourKEQDSP::voicedMidQ(float gainDb, float baseQ, bool black) noexcept
{
    if (!black) return clampf(baseQ, 0.5f, 8.0f); // E-series: constant-Q
    const float absGain = std::abs(gainDb);
    const float scale = (gainDb >= 0.0f) ? 2.0f : 1.5f; // G-series: proportional
    const float dq = baseQ * (1.0f + (absGain / 20.0f) * scale);
    return clampf(dq, 0.5f, 8.0f);
}

// Standard bilinear pre-warp (Multi-Q's britishPreWarpFrequency). Retained for
// callers that want an fc estimate; the console coeff builders below pre-warp
// internally, so recomputeCoeffs no longer applies the old piecewise HF fudge.
float FourKEQDSP::preWarp(float freq, double sampleRate) noexcept
{
    const float nyquist = static_cast<float>(sampleRate * 0.5);
    float safeFreq = std::min(freq, nyquist * 0.98f);
    safeFreq = std::max(safeFreq, 1.0f);
    const float omega = kDuskPi * safeFreq / static_cast<float>(sampleRate);
    const float warped = static_cast<float>(sampleRate) / kDuskPi * std::tan(omega);
    return std::min(std::max(warped, 1.0f), nyquist * 0.99f);
}

// mode: 0 = 1x (off), 1 = 2x, 2 = 4x. Capped so the oversampled rate stays sane
// at already-high base rates (>=176.4k -> 1x, >=88.2k -> max 2x).
int FourKEQDSP::chooseFactor(double baseSampleRate, int mode) noexcept
{
    const int req = mode <= 0 ? 1 : (mode == 1 ? 2 : 4);
    const int cap = baseSampleRate >= 176400.0 ? 1 : (baseSampleRate >= 88200.0 ? 2 : 4);
    return req < cap ? req : cap;
}

//==============================================================================
// Lifecycle
//==============================================================================
void FourKEQDSP::prepare(double sampleRate, int maxBlockSize)
{
    baseSampleRate = sampleRate;
    maxBlock = std::max(1, maxBlockSize);

    scratchL.assign((size_t)maxBlock, 0.0f);
    scratchR.assign((size_t)maxBlock, 0.0f);

    curFactor = chooseFactor(baseSampleRate, (int)(pOversampling.load(R) + 0.5f));
    const double osRate = baseSampleRate * curFactor;

    for (auto& o : os) { o.setFactor(curFactor); o.reset(); }
    reportedLatency = (int)std::lround(os[0].latency());

    consoleSat.setSampleRate(osRate);
    consoleSat.reset();
    xfmrLpCoef = 1.0f - std::exp(-kDuskTwoPi * 180.0f / (float)osRate); // LF flux corner

    meterDecay = std::exp(-1.0f / (0.3f * (float)baseSampleRate));
    powerSmoother.prepare(baseSampleRate, 0.03f); // ~30 ms bypass crossfade
    powerSmoother.snap(pBypass.load(R) > 0.5f ? 0.0f : 1.0f);
    lastSmoothedPower.store(powerSmoother.value(), R);

    lastHpfEnabled = pHpfEnabled.load(R) > 0.5f;
    lastLpfEnabled = pLpfEnabled.load(R) > 0.5f;

    recomputeCoeffs(osRate);
    reset();
}

void FourKEQDSP::reset()
{
    for (auto& c : ch) c.reset();
    for (auto& o : os) o.reset();
    consoleSat.reset();
    xfmrFlux[0] = xfmrFlux[1] = 0.0f;
    preRing.reset();
    postRing.reset();
    std::fill(scratchL.begin(), scratchL.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    inPeakL.store(0.f, R); inPeakR.store(0.f, R);
    outPeakL.store(0.f, R); outPeakR.store(0.f, R);
}

//==============================================================================
// Coefficients (both channels share identical coefficients, as in JUCE)
//==============================================================================
void FourKEQDSP::recomputeCoeffs(double fs) noexcept
{
    const bool black = pEqType.load(R) > 0.5f;

    // HPF: 1st-order + 2nd-order (Q=1.0, Butterworth cascade) = 18 dB/oct.
    const float hpfFreq = pHpfFreq.load(R);
    const BiquadCoeffs hpf1 = Biquad::firstOrderHighPass(fs, hpfFreq);
    const BiquadCoeffs hpf2 = Biquad::highPass(fs, hpfFreq, 1.0f);

    // LPF: 2nd-order, Q by mode; lowPass() pre-warps internally, just clamp fc.
    const float lpfFreq = (float)std::max(1.0, std::min((double)pLpfFreq.load(R), fs * 0.4998));
    const float lpfQ = black ? 0.8f : 0.707f;
    const BiquadCoeffs lpf = Biquad::lowPass(fs, lpfFreq, lpfQ);

    // --- Parallel-summing EQ bands (82E242 topology) ----------------------
    // Each band is a fixed-Q band-pass / shelf building block fed the dry EQ
    // input; the processor sums them as dry + sum(K_i * F_i) with K = bandK(G).
    // Interaction, constant-Q (E) and asymmetric cut emerge from the structure.
    const float bellQ = black ? 0.9f : 0.6f;

    // LF: low shelf (first-order Brown / resonant 2nd-order Black), or bell.
    const float lfGain = pLfGain.load(R), lfFreq = pLfFreq.load(R);
    const bool  lfBell = pLfBell.load(R) > 0.5f;
    const BiquadCoeffs lf = lfBell
        ? Biquad::bandPassConstantPeak(fs, lfFreq, bellQ)
        : (black ? Biquad::lowPass(fs, lfFreq, 0.9f)
                 : Biquad::firstOrderLowPass(fs, lfFreq));
    kLf = bandK(lfGain);

    // LM: fixed-Q (Brown) / proportional-Q (Black) band-pass.
    const float lmFreq = pLmFreq.load(R);
    const float lmQ = voicedMidQ(pLmGain.load(R), pLmQ.load(R), black);
    const BiquadCoeffs lm = Biquad::bandPassConstantPeak(fs, lmFreq, lmQ);
    kLm = bandK(pLmGain.load(R));

    // HM: band-pass; Brown caps freq at 7 kHz.
    float hmFreq = pHmFreq.load(R);
    if (!black && hmFreq > 7000.0f) hmFreq = 7000.0f;
    const float hmQ = voicedMidQ(pHmGain.load(R), pHmQ.load(R), black);
    const BiquadCoeffs hm = Biquad::bandPassConstantPeak(fs, hmFreq, hmQ);
    kHm = bandK(pHmGain.load(R));

    // HF: high shelf (first-order Brown / resonant 2nd-order Black), or bell.
    const float hfGain = pHfGain.load(R), hfFreq = pHfFreq.load(R);
    const bool  hfBell = pHfBell.load(R) > 0.5f;
    const BiquadCoeffs hf = hfBell
        ? Biquad::bandPassConstantPeak(fs, hfFreq, bellQ)
        : (black ? Biquad::highPass(fs, hfFreq, 0.9f)
                 : Biquad::firstOrderHighPass(fs, hfFreq));
    kHf = bandK(hfGain);

    // Transformer phase allpass, fixed 200 Hz (Brown only, gated at runtime).
    const BiquadCoeffs ap = Biquad::firstOrderAllPass(fs, 200.0f);

    for (auto& c : ch)
    {
        c.hpf1.setCoeffs(hpf1); c.hpf2.setCoeffs(hpf2);
        c.lf.setCoeffs(lf); c.lm.setCoeffs(lm); c.hm.setCoeffs(hm); c.hf.setCoeffs(hf);
        c.lpf.setCoeffs(lpf); c.allpass.setCoeffs(ap);
    }
}

float FourKEQDSP::calcAutoGainCompensation() const noexcept
{
    // Topology-correct auto-gain: measure the ACTUAL parallel-summed response
    // (reusing the coefficients recomputeCoeffs just built into ch[0]) and undo
    // its pink-weighted (equal-energy-per-octave) RMS level. Works for any band
    // combination because it evaluates the real magnitude — overlapping boosts
    // that sub-add in the summing node are counted once, not double-counted like
    // the old per-band gain*bandwidth heuristic did.
    const double osRate = baseSampleRate * (double)curFactor;
    const bool hpfEn = pHpfEnabled.load(R) > 0.5f;
    const bool lpfEn = pLpfEnabled.load(R) > 0.5f;
    const ChannelFilters& cf = ch[0];

    double sumSq = 0.0; int cnt = 0;
    // log-spaced probes, 25 Hz .. 20 kHz — one every ~1/3 octave (pink weight).
    for (double f = 25.0; f <= 20000.0; f *= 1.2589254) // 2^(1/3)
    {
        const double w = 2.0 * kDuskPi * f / osRate;
        if (w >= kDuskPi) break; // above Nyquist of the OS rate
        std::complex<double> H = 1.0
            + (double)kLf * cf.lf.response(w)
            + (double)kLm * cf.lm.response(w)
            + (double)kHm * cf.hm.response(w)
            + (double)kHf * cf.hf.response(w);
        double mag = std::abs(H);
        if (hpfEn) mag *= cf.hpf1.magnitude(w) * cf.hpf2.magnitude(w);
        if (lpfEn) mag *= cf.lpf.magnitude(w);
        if (std::isfinite(mag)) { sumSq += mag * mag; ++cnt; }
    }
    if (cnt == 0) return 1.0f;
    const double rms = std::sqrt(sumSq / (double)cnt);
    if (!std::isfinite(rms) || rms <= 1e-6) return 1.0f;
    float compDb = clampf(-20.0f * (float)std::log10(rms), -12.0f, 12.0f);
    return dbToGain(compDb);
}

//==============================================================================
// processBlock
//==============================================================================
void FourKEQDSP::processBlock(const float* const* inputs, float* const* outputs,
                              int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    ScopedFlushDenormals ftz;

    const int nCh = std::max(1, std::min(numChannels, kMaxChannels));

    // Chunk oversized host buffers (larger than the prepared maxBlock) so every
    // output sample is written and the scratch buffers never overflow.
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min(numSamples - offset, maxBlock);
        const float* in[kMaxChannels]; float* out[kMaxChannels];
        for (int c = 0; c < nCh; ++c) { in[c] = inputs[c] + offset; out[c] = outputs[c] + offset; }
        processChunk(in, out, nCh, n);
    }
}

void FourKEQDSP::processChunk(const float* const* inputs, float* const* outputs,
                              int nCh, int nS) noexcept
{
    // Oversampling factor may change with sample rate / user param at block rate.
    const int wantFactor = chooseFactor(baseSampleRate, (int)(pOversampling.load(R) + 0.5f));
    if (wantFactor != curFactor)
    {
        curFactor = wantFactor;
        for (auto& o : os) { o.setFactor(curFactor); o.reset(); }
        for (auto& c : ch) c.reset();
        consoleSat.setSampleRate(baseSampleRate * curFactor);
        consoleSat.reset();
        xfmrLpCoef = 1.0f - std::exp(-kDuskTwoPi * 180.0f / (float)(baseSampleRate * curFactor));
        reportedLatency = (int)std::lround(os[0].latency());
    }
    const double osRate = baseSampleRate * curFactor;

    // Console voicing follows the mode; keep the saturator's type in sync.
    const bool black = pEqType.load(R) > 0.5f;
    consoleSat.setConsoleType(black ? ConsoleSaturationCore::ConsoleType::GSeries
                                    : ConsoleSaturationCore::ConsoleType::ESeries);

    recomputeCoeffs(osRate);

    // HPF/LPF re-enable: clear stale state so toggling on does not click.
    const bool hpfEn = pHpfEnabled.load(R) > 0.5f;
    const bool lpfEn = pLpfEnabled.load(R) > 0.5f;
    if (hpfEn && !lastHpfEnabled) for (auto& c : ch) { c.hpf1.reset(); c.hpf2.reset(); }
    if (lpfEn && !lastLpfEnabled) for (auto& c : ch) c.lpf.reset();
    lastHpfEnabled = hpfEn;
    lastLpfEnabled = lpfEn;

    float* wet[kMaxChannels] = { scratchL.data(), scratchR.data() };

    //--- input metering (pre-gain) --------------------------------------------
    float inPk[kMaxChannels] = { 0.f, 0.f };
    for (int c = 0; c < nCh; ++c)
        for (int n = 0; n < nS; ++n)
            inPk[c] = std::max(inPk[c], std::abs(inputs[c][n]));

    //--- input gain -> wet scratch --------------------------------------------
    const float inGain = dbToGain(pInputGain.load(R));
    for (int c = 0; c < nCh; ++c)
        for (int n = 0; n < nS; ++n)
            wet[c][n] = inputs[c][n] * inGain;

    //--- pre-EQ spectrum tap (mono) -------------------------------------------
    for (int n = 0; n < nS; ++n)
        preRing.push(nCh == 2 ? 0.5f * (wet[0][n] + wet[1][n]) : wet[0][n]);

    //--- M/S encode -----------------------------------------------------------
    const bool ms = pMsMode.load(R) > 0.5f && nCh == 2;
    if (ms)
        for (int n = 0; n < nS; ++n)
        {
            const float L = wet[0][n], Rr = wet[1][n];
            wet[0][n] = (L + Rr) * 0.5f;
            wet[1][n] = (L - Rr) * 0.5f;
        }

    //--- oversampled EQ + saturation chain ------------------------------------
    const bool brown = !black;
    const float satAmt = pSaturation.load(R) * 0.01f;
    for (int c = 0; c < nCh; ++c)
    {
        ChannelFilters& cf = ch[(size_t)c];
        Oversampler& o = os[(size_t)c];
        const bool left = (c == 0);
        for (int n = 0; n < nS; ++n)
        {
            wet[c][n] = o.processSample(wet[c][n], [&](float x) noexcept
            {
                // Filters are genuine HP/LP stages, kept in series.
                if (hpfEn) { x = cf.hpf1.process(x); x = cf.hpf2.process(x); }
                // Parallel-summing EQ (82E242): dry + sum of band blocks, each
                // fed the SAME EQ input. All four filters advance every sample.
                const float e = x;
                x = e + kLf * cf.lf.process(e)
                      + kLm * cf.lm.process(e)
                      + kHm * cf.hm.process(e)
                      + kHf * cf.hf.process(e);
                if (lpfEn) x = cf.lpf.process(x);
                // Transformer (Brown/E-series only): phase rotation + LF-weighted
                // core saturation. The iron colours even at unity — a small
                // always-on 2nd/3rd-harmonic term from the LF flux estimate is
                // present regardless of the drive knob, and the drive knob adds
                // more on top. (Real E-series channel path is never bit-clean.)
                if (brown)
                {
                    x = cf.allpass.process(x);
                    float& flux = xfmrFlux[(size_t)c];
                    flux += xfmrLpCoef * (x - flux);
                    const float lfHarm = std::tanh(flux * 1.6f) / 1.6f - flux;
                    // Measured SSL 4000E nominal THD is ~0.0003-0.0007% — nearly
                    // clean; the audible colour comes from DRIVING it. So the
                    // always-on term is only a token analog trace (real iron is
                    // never perfectly linear), with the drive knob adding the
                    // rest. Jensen JT-115K-E core => even/H2-dominant (E path).
                    constexpr float kBaseIron = 0.015f;  // token trace, ~inaudible
                    x += lfHarm * (kBaseIron + satAmt * 0.35f);
                }
                if (satAmt > 0.001f) x = consoleSat.processSample(x, satAmt, left);
                return x;
            });
        }
    }

    //--- crosstalk (-60 dB), non-M/S stereo only ------------------------------
    if (!ms && nCh == 2)
    {
        const float xt = 0.001f;
        for (int n = 0; n < nS; ++n)
        {
            const float L = wet[0][n], Rr = wet[1][n];
            wet[0][n] = L + Rr * xt;
            wet[1][n] = Rr + L * xt;
        }
    }

    //--- M/S decode -----------------------------------------------------------
    if (ms)
        for (int n = 0; n < nS; ++n)
        {
            const float m = wet[0][n], s = wet[1][n];
            wet[0][n] = m + s;
            wet[1][n] = m - s;
        }

    //--- output gain * auto-gain ----------------------------------------------
    const float autoComp = pAutoGain.load(R) > 0.5f ? calcAutoGainCompensation() : 1.0f;
    const float outGain = dbToGain(pOutputGain.load(R)) * autoComp;
    for (int c = 0; c < nCh; ++c)
        for (int n = 0; n < nS; ++n)
            wet[c][n] *= outGain;

    //--- post-EQ spectrum tap (mono) ------------------------------------------
    for (int n = 0; n < nS; ++n)
        postRing.push(nCh == 2 ? 0.5f * (wet[0][n] + wet[1][n]) : wet[0][n]);

    //--- bypass crossfade to bit-exact dry passthrough + output metering ------
    powerSmoother.setTarget(pBypass.load(R) > 0.5f ? 0.0f : 1.0f);
    float outPk[kMaxChannels] = { 0.f, 0.f };
    for (int n = 0; n < nS; ++n)
    {
        const float p = powerSmoother.next();
        for (int c = 0; c < nCh; ++c)
        {
            const float dry = inputs[c][n];
            const float y = dry + p * (wet[c][n] - dry);
            outputs[c][n] = y;
            outPk[c] = std::max(outPk[c], std::abs(y));
        }
    }
    // Publish the settled crossfade level for latency gating (see getLatencySamples).
    lastSmoothedPower.store(powerSmoother.value(), R);

    //--- metering store (peak-hold with ~300 ms release) ----------------------
    const float dk = std::pow(meterDecay, (float)nS);
    auto storePeak = [dk](std::atomic<float>& m, float pk)
    {
        const float decayed = m.load(std::memory_order_relaxed) * dk;
        m.store(pk > decayed ? pk : decayed, std::memory_order_relaxed);
    };
    storePeak(inPeakL, inPk[0]);  storePeak(inPeakR, nCh == 2 ? inPk[1] : inPk[0]);
    storePeak(outPeakL, outPk[0]); storePeak(outPeakR, nCh == 2 ? outPk[1] : outPk[0]);
}

} // namespace duskaudio
