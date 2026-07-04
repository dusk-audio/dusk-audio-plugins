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

float FourKEQDSP::preWarp(float freq, double sampleRate) noexcept
{
    const float nyquist = static_cast<float>(sampleRate * 0.5);
    const float omega = kDuskPi * freq / static_cast<float>(sampleRate);
    float warpedFreq = static_cast<float>(sampleRate) / kDuskPi * std::tan(omega);

    if (freq > 3000.0f)
    {
        const float ratio = freq / nyquist;
        float compensation = 1.0f;
        if (ratio < 0.3f)
            compensation = 1.0f + (ratio - 0.136f) * 0.15f;
        else if (ratio < 0.5f)
            compensation = 1.0f + (ratio - 0.3f) * 0.4f;
        else
            compensation = 1.0f + (ratio - 0.5f) * 0.6f;
        warpedFreq = freq * compensation;
    }
    return std::min(warpedFreq, nyquist * 0.99f);
}

BiquadCoeffs FourKEQDSP::consolePeak(double fs, float freq, float q, float gainDb, bool black) noexcept
{
    float consoleQ = q;
    if (black && std::abs(gainDb) > 0.1f)
    {
        const float gf = std::abs(gainDb) / 15.0f;
        consoleQ *= (gainDb > 0.0f) ? (1.0f + gf * 1.2f) : (1.0f + gf * 0.6f);
    }
    consoleQ = clampf(consoleQ, 0.1f, 10.0f);
    return Biquad::peak(fs, freq, gainDb, consoleQ);
}

BiquadCoeffs FourKEQDSP::consoleShelf(double fs, float freq, float q, float gainDb, bool high, bool black) noexcept
{
    float consoleQ = q * (black ? 1.4f : 0.65f);
    return Biquad::shelf(fs, freq, gainDb, consoleQ, high);
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

    // HPF: 1st-order + 2nd-order (Q=0.54) = 18 dB/oct.
    const float hpfFreq = pHpfFreq.load(R);
    const BiquadCoeffs hpf1 = Biquad::firstOrderHighPass(fs, hpfFreq);
    const BiquadCoeffs hpf2 = Biquad::highPass(fs, hpfFreq, 0.54f);

    // LPF: 2nd-order, Q depends on mode; prewarp near Nyquist.
    float lpfFreq = pLpfFreq.load(R);
    if (lpfFreq > fs * 0.3f) lpfFreq = preWarp(lpfFreq, fs);
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

    // HM band: peak; Black proportional Q + 13k range; Brown fixed Q + 7k cap.
    const float hmGain = pHmGain.load(R);
    float hmFreq = pHmFreq.load(R);
    float hmQ = pHmQ.load(R);
    if (black) hmQ = dynamicQ(hmGain, hmQ);
    else if (hmFreq > 7000.0f) hmFreq = 7000.0f;
    float hmProc = hmFreq;
    if (hmFreq > 3000.0f) hmProc = preWarp(hmFreq, fs);
    const BiquadCoeffs hm = consolePeak(fs, hmProc, hmQ, hmGain, black);

    // HF band: shelf (always prewarped), or bell in Black+bell.
    const float hfGain = pHfGain.load(R), hfFreq = pHfFreq.load(R);
    const bool  hfBell = pHfBell.load(R) > 0.5f;
    const float hfWarp = preWarp(hfFreq, fs);
    const BiquadCoeffs hf = (black && hfBell)
        ? consolePeak(fs, hfWarp, 0.7f, hfGain, black)
        : consoleShelf(fs, hfWarp, 0.7f, hfGain, /*high*/true, black);

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
                if (brown) x = cf.allpass.process(x);
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
