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
float FourKEQDSP::dynamicQ(float gainDb, float baseQ) noexcept
{
    const float absGain = std::abs(gainDb);
    const float scale = (gainDb >= 0.0f) ? 2.0f : 1.5f;
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

// Console peak — analog-matched (Multi-Q). Pre-warps the BANDWIDTH
// (kbw = tan(pi·bw/sr)) with the center via cos(2pi·fc/sr): constant-Q,
// cramping-free at any sample rate. Black mode uses proportional Q.
BiquadCoeffs FourKEQDSP::consolePeak(double fs, float freq, float q, float gainDb, bool black) noexcept
{
    static constexpr double kPi = 3.14159265358979323846;
    double consoleQ = q;
    if (black && std::abs(gainDb) > 0.1f)
    {
        const double gf = std::abs((double)gainDb) / 15.0;
        consoleQ *= (gainDb > 0.0f) ? (1.0 + gf * 1.2) : (1.0 + gf * 0.6);
    }
    consoleQ = std::max(0.1, std::min(consoleQ, 10.0));

    const double fc  = std::max(1.0, std::min((double)freq, fs * 0.4998));
    const double bw  = fc / consoleQ;
    const double kbw = std::tan(kPi * std::min(bw, fs * 0.4998) / fs);
    const double A   = std::pow(10.0, (double)gainDb / 40.0);
    const double cosW = std::cos(2.0 * kPi * fc / fs);

    const double b0 = 1.0 + kbw * A, b2 = 1.0 - kbw * A;
    const double a0 = 1.0 + kbw / A, a2 = 1.0 - kbw / A;
    const double b1 = -2.0 * cosW,   a1 = -2.0 * cosW;
    const double inv = 1.0 / a0;
    return { (float)(b0 * inv), (float)(b1 * inv), (float)(b2 * inv), (float)(a1 * inv), (float)(a2 * inv) };
}

// Console shelf — analog-matched (Multi-Q). RBJ shelf with the S-based alpha,
// deriving cosW/sinW from k = tan(pi·fc/sr) so the turnover lands exactly at fc.
BiquadCoeffs FourKEQDSP::consoleShelf(double fs, float freq, float q, float gainDb, bool high, bool black) noexcept
{
    static constexpr double kPi = 3.14159265358979323846;
    double consoleQ = q * (black ? 1.4 : 0.65);
    consoleQ = std::max(0.01, consoleQ);

    const double fc  = std::max(1.0, std::min((double)freq, fs * 0.4998));
    const double A   = std::pow(10.0, (double)gainDb / 40.0);
    const double sqA = std::sqrt(A);
    const double k   = std::tan(kPi * fc / fs), k2 = k * k;
    const double cosW = (1.0 - k2) / (1.0 + k2), sinW = 2.0 * k / (1.0 + k2);
    const double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / consoleQ - 1.0) + 2.0);

    double b0, b1, b2, a0, a1, a2;
    if (high)
    {
        b0 =  A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * sqA * alpha);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
        b2 =  A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * sqA * alpha);
        a0 = (A + 1.0) - (A - 1.0) * cosW + 2.0 * sqA * alpha;
        a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cosW);
        a2 = (A + 1.0) - (A - 1.0) * cosW - 2.0 * sqA * alpha;
    }
    else
    {
        b0 =  A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * sqA * alpha);
        b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
        b2 =  A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * sqA * alpha);
        a0 = (A + 1.0) + (A - 1.0) * cosW + 2.0 * sqA * alpha;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
        a2 = (A + 1.0) + (A - 1.0) * cosW - 2.0 * sqA * alpha;
    }
    const double inv = 1.0 / a0;
    return { (float)(b0 * inv), (float)(b1 * inv), (float)(b2 * inv), (float)(a1 * inv), (float)(a2 * inv) };
}

int FourKEQDSP::chooseFactor(double baseSampleRate, bool want4x) noexcept
{
    if (baseSampleRate >= 176400.0) return 1; // Nyquist already high enough
    if (baseSampleRate >= 88200.0)  return 2; // cap at 2x
    return want4x ? 4 : 2;
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

    curFactor = chooseFactor(baseSampleRate, pOversampling.load(R) >= 0.5f);
    const double osRate = baseSampleRate * curFactor;

    for (auto& o : os) { o.setFactor(curFactor); o.reset(); }
    reportedLatency = (int)std::lround(os[0].latency());

    consoleSat.setSampleRate(osRate);
    consoleSat.reset();
    xfmrLpCoef = 1.0f - std::exp(-kDuskTwoPi * 180.0f / (float)osRate); // LF flux corner

    meterDecay = std::exp(-1.0f / (0.3f * (float)baseSampleRate));
    powerSmoother.prepare(baseSampleRate, 0.03f); // ~30 ms bypass crossfade
    powerSmoother.snap(pBypass.load(R) > 0.5f ? 0.0f : 1.0f);

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

    // LF band: shelf, or bell in Black+bell.
    const float lfGain = pLfGain.load(R), lfFreq = pLfFreq.load(R);
    const bool  lfBell = pLfBell.load(R) > 0.5f;
    const BiquadCoeffs lf = (black && lfBell)
        ? consolePeak(fs, lfFreq, 0.7f, lfGain, black)
        : consoleShelf(fs, lfFreq, 0.7f, lfGain, /*high*/false, black);

    // LM band: peak; Black uses proportional Q.
    const float lmGain = pLmGain.load(R), lmFreq = pLmFreq.load(R);
    float lmQ = pLmQ.load(R);
    if (black) lmQ = dynamicQ(lmGain, lmQ);
    const BiquadCoeffs lm = consolePeak(fs, lmFreq, lmQ, lmGain, black);

    // HM band: peak; Black proportional Q + full range; Brown fixed Q + 7k cap.
    // The analog-matched consolePeak pre-warps fc internally — no fudge here.
    const float hmGain = pHmGain.load(R);
    float hmFreq = pHmFreq.load(R);
    float hmQ = pHmQ.load(R);
    if (black) hmQ = dynamicQ(hmGain, hmQ);
    else if (hmFreq > 7000.0f) hmFreq = 7000.0f;
    const BiquadCoeffs hm = consolePeak(fs, hmFreq, hmQ, hmGain, black);

    // HF band: shelf, or bell in Black+bell (consoleShelf pre-warps fc).
    const float hfGain = pHfGain.load(R), hfFreq = pHfFreq.load(R);
    const bool  hfBell = pHfBell.load(R) > 0.5f;
    const BiquadCoeffs hf = (black && hfBell)
        ? consolePeak(fs, hfFreq, 0.7f, hfGain, black)
        : consoleShelf(fs, hfFreq, 0.7f, hfGain, /*high*/true, black);

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
    const float lfGainDB = pLfGain.load(R), lmGainDB = pLmGain.load(R);
    const float hmGainDB = pHmGain.load(R), hfGainDB = pHfGain.load(R);
    const float lmQ = pLmQ.load(R), hmQ = pHmQ.load(R);
    const bool  lfBell = pLfBell.load(R) > 0.5f, hfBell = pHfBell.load(R) > 0.5f;

    if (!std::isfinite(lfGainDB) || !std::isfinite(lmGainDB) ||
        !std::isfinite(hmGainDB) || !std::isfinite(hfGainDB))
        return 1.0f;

    const float lfBandwidth = lfBell ? 0.3f : 0.5f;
    const float lfEnergy = lfGainDB * lfBandwidth;
    const float lmEnergy = lmGainDB * std::min(0.7f / lmQ, 0.5f);
    const float hmEnergy = hmGainDB * std::min(0.7f / hmQ, 0.5f);
    const float hfBandwidth = hfBell ? 0.3f : 0.6f;
    const float hfEnergy = hfGainDB * hfBandwidth;

    float compensationDB = -(lfEnergy + lmEnergy + hmEnergy + hfEnergy);
    compensationDB = clampf(compensationDB, -12.0f, 12.0f);
    return dbToGain(compensationDB);
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
    const int nS  = std::min(numSamples, maxBlock);

    // Oversampling factor may change with sample rate / user param at block rate.
    const int wantFactor = chooseFactor(baseSampleRate, pOversampling.load(R) >= 0.5f);
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
                if (hpfEn) { x = cf.hpf1.process(x); x = cf.hpf2.process(x); }
                x = cf.lf.process(x);
                x = cf.lm.process(x);
                x = cf.hm.process(x);
                x = cf.hf.process(x);
                if (lpfEn) x = cf.lpf.process(x);
                // Transformer (Brown/E-series only): phase rotation + LF-weighted
                // core saturation (added odd harmonics from an LF flux estimate,
                // scaled by drive — saturation 0 leaves the signal untouched).
                if (brown)
                {
                    x = cf.allpass.process(x);
                    if (satAmt > 0.001f)
                    {
                        float& flux = xfmrFlux[(size_t)c];
                        flux += xfmrLpCoef * (x - flux);
                        const float lfHarm = std::tanh(flux * 1.6f) / 1.6f - flux;
                        x += lfHarm * satAmt * 0.35f;
                    }
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
