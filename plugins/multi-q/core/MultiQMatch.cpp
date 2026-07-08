// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQMatch.cpp — implementation of the framework-free spectrum-match EQ core.
// See MultiQMatch.hpp for the thread model. The learning + correction math is a
// faithful port of EQMatchProcessor.h; juce::dsp::FFT is replaced by FFTr2 and
// juce::dsp::Convolution by the uniformly-partitioned overlap-save convolver.

#include "MultiQMatch.hpp"

#include <algorithm>
#include <cstring>

namespace duskaudio
{

// ---- small helpers ---------------------------------------------------------
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static double besselI0(double x)
{
    double sum = 1.0, term = 1.0, xx = x * x * 0.25;
    for (int k = 1; k < 60; ++k)
    {
        term *= xx / ((double)k * (double)k);
        sum += term;
        if (term < 1e-13 * sum) break;
    }
    return sum;
}

//==============================================================================
// FFTr2
//==============================================================================
void FFTr2::prepare(int size)
{
    n = size;
    rev.assign((size_t)n, 0);
    int logn = 0; while ((1 << logn) < n) ++logn;
    for (int i = 0; i < n; ++i)
    {
        int r = 0, x = i;
        for (int b = 0; b < logn; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
        rev[(size_t)i] = r;
    }
    twR.assign((size_t)(n / 2), 0.0f);
    twI.assign((size_t)(n / 2), 0.0f);
    const double twoPi = 2.0 * kMultiQPi;
    for (int j = 0; j < n / 2; ++j)
    {
        const double a = -twoPi * (double)j / (double)n;
        twR[(size_t)j] = (float)std::cos(a);
        twI[(size_t)j] = (float)std::sin(a);
    }
}

void FFTr2::transform(float* re, float* im, bool inv) const
{
    // bit-reversal permutation
    for (int i = 0; i < n; ++i)
    {
        const int j = rev[(size_t)i];
        if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const int ti = k * step;
                float wr = twR[(size_t)ti];
                float wi = twI[(size_t)ti];
                if (inv) wi = -wi;                    // conjugate twiddle for inverse
                const int a = i + k, b = i + k + half;
                const float vr = re[b] * wr - im[b] * wi;
                const float vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi;
                re[a] += vr;        im[a] += vi;
            }
        }
    }
    if (inv)
    {
        const float s = 1.0f / (float)n;
        for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
    }
}

//==============================================================================
// MultiQMatch — lifecycle
//==============================================================================
void MultiQMatch::ChannelConv::reset()
{
    window.fill(0.f);
    for (auto& p : fdlRe) p.fill(0.f);
    for (auto& p : fdlIm) p.fill(0.f);
    outRing.fill(0.f);
    dryDelay.fill(0.f);
    fdlHead = 0;
    outRead = outWrite = outCount = 0;
    dryPos = 0;
}

void MultiQMatch::prepare(double sr, int /*maxBlockSize*/)
{
    sampleRate = sr > 0 ? sr : 48000.0;

    fftAnalysis.prepare(FFT_SIZE);
    fftGenMin.prepare(1 << 13);   // 8192
    fftConv.prepare(CONV_FFT);

    // Hann window over the analysis frame (matches juce hann).
    for (int i = 0; i < FFT_SIZE; ++i)
        hannWindow[(size_t)i] = 0.5f - 0.5f * (float)std::cos(2.0 * kMultiQPi * (double)i / (double)(FFT_SIZE - 1));

    learningInput.assign((size_t)FFT_SIZE, 0.0f);
    learningInputPos = 0;
    samplesSinceLastFrame = 0;

    genRe.assign((size_t)(1 << 13), 0.0f);
    genIm.assign((size_t)(1 << 13), 0.0f);

    wetGain.reset(sampleRate, 0.01);   // 10 ms crossfade
    wetGain.setCurrentAndTargetValue(0.0f);

    reset();
}

void MultiQMatch::reset()
{
    learningTarget.store(Target::None, std::memory_order_release);
    currentSpectrum.reset();
    referenceSpectrum.reset();
    currentSpectrumBuf.writeBuffer().reset();   currentSpectrumBuf.publish();
    currentSpectrumBuf.writeBuffer().reset();   currentSpectrumBuf.publish();
    referenceSpectrumBuf.writeBuffer().reset(); referenceSpectrumBuf.publish();
    referenceSpectrumBuf.writeBuffer().reset(); referenceSpectrumBuf.publish();
    learningInputPos = 0;
    samplesSinceLastFrame = 0;

    {
        correctionLock.lock();
        correctionCurveDB.fill(0.0f);
        correctionFIR.fill(0.0f);
        correctionLock.unlock();
    }
    correctionValid.store(false, std::memory_order_release);

    irActive.store(-1, std::memory_order_release);
    irWriteToggle = 0;
    convL.reset(); convR.reset();
    hopFill = 0;
    convState.store(ConvState::Inactive, std::memory_order_release);
    wetGain.setCurrentAndTargetValue(0.0f);

    pendingStartCurrent.store(false, std::memory_order_relaxed);
    pendingStartReference.store(false, std::memory_order_relaxed);
    pendingStop.store(false, std::memory_order_relaxed);
    pendingClear.store(false, std::memory_order_relaxed);
    pendingActivate.store(false, std::memory_order_relaxed);
}

//==============================================================================
// Learning (audio thread)
//==============================================================================
void MultiQMatch::audioThreadSync()
{
    // Order matches MultiQ.cpp:511-535 — clear, then stop before start so a
    // simultaneous start always wins.
    if (pendingClear.exchange(false, std::memory_order_acquire))
    {
        doClearNow();
        pendingStartCurrent.store(false, std::memory_order_relaxed);
        pendingStartReference.store(false, std::memory_order_relaxed);
        pendingStop.store(false, std::memory_order_relaxed);
    }
    if (pendingStop.exchange(false, std::memory_order_acquire))
        learningTarget.store(Target::None, std::memory_order_release);
    if (pendingStartCurrent.exchange(false, std::memory_order_acquire))
    {
        currentSpectrum.reset();
        currentSpectrumBuf.writeBuffer().reset(); currentSpectrumBuf.publish();
        currentSpectrumBuf.writeBuffer().reset(); currentSpectrumBuf.publish();
        learningInputPos = 0;
        samplesSinceLastFrame = -(FFT_SIZE - HOP_SIZE);
        learningTarget.store(Target::Current, std::memory_order_release);
    }
    if (pendingStartReference.exchange(false, std::memory_order_acquire))
    {
        referenceSpectrum.reset();
        referenceSpectrumBuf.writeBuffer().reset(); referenceSpectrumBuf.publish();
        referenceSpectrumBuf.writeBuffer().reset(); referenceSpectrumBuf.publish();
        learningInputPos = 0;
        samplesSinceLastFrame = -(FFT_SIZE - HOP_SIZE);
        learningTarget.store(Target::Reference, std::memory_order_release);
    }

    // Adopt a freshly-published correction (Inactive→Active or refresh).
    if (pendingActivate.exchange(false, std::memory_order_acquire))
    {
        if (convState.load(std::memory_order_relaxed) == ConvState::Inactive)
        {
            convL.reset(); convR.reset(); hopFill = 0;
            // seed CONV_HOP samples of latency into each channel's output FIFO
            for (int j = 0; j < CONV_HOP; ++j)
            {
                convL.outRing[(size_t)j] = 0.f; convR.outRing[(size_t)j] = 0.f;
            }
            convL.outWrite = convR.outWrite = CONV_HOP;
            convL.outCount = convR.outCount = CONV_HOP;
            wetGain.setCurrentAndTargetValue(0.0f);
        }
        convState.store(ConvState::Active, std::memory_order_release);
        wetGain.setTargetValue(1.0f);
    }
}

void MultiQMatch::feedLearningBlock(const float* monoSamples, int numSamples)
{
    if (learningTarget.load(std::memory_order_acquire) == Target::None) return;
    for (int i = 0; i < numSamples; ++i) feedSample(monoSamples[i]);
}

void MultiQMatch::feedLearningStereo(const float* L, const float* R, int numSamples)
{
    if (learningTarget.load(std::memory_order_acquire) == Target::None) return;
    if (R != nullptr)
        for (int i = 0; i < numSamples; ++i) feedSample(0.5f * (L[i] + R[i]));
    else
        for (int i = 0; i < numSamples; ++i) feedSample(L[i]);
}

void MultiQMatch::feedSample(float s)
{
    const Target target = learningTarget.load(std::memory_order_acquire);
    if (target == Target::None) return;
    learningInput[(size_t)learningInputPos] = s;
    if (++learningInputPos >= FFT_SIZE) learningInputPos = 0;
    if (++samplesSinceLastFrame >= HOP_SIZE)
    {
        samplesSinceLastFrame = 0;
        processLearningFrame(target);
    }
}

void MultiQMatch::processLearningFrame(Target target)
{
    // Gather FFT_SIZE samples from the circular buffer (oldest at learningInputPos).
    const int readPos = learningInputPos;
    for (int i = 0; i < FFT_SIZE; ++i)
    {
        const int src = (readPos + i) % FFT_SIZE;
        learnRe[(size_t)i] = learningInput[(size_t)src] * hannWindow[(size_t)i];
        learnIm[(size_t)i] = 0.0f;
    }
    fftAnalysis.forward(learnRe.data(), learnIm.data());

    LearnedSpectrum& spectrum = (target == Target::Current) ? currentSpectrum : referenceSpectrum;
    spectrum.addFrame(learnRe.data(), learnIm.data());

    // Publish a full snapshot for lock-free UI/message-thread reads.
    if (target == Target::Current) { currentSpectrumBuf.writeBuffer() = currentSpectrum; currentSpectrumBuf.publish(); }
    else                           { referenceSpectrumBuf.writeBuffer() = referenceSpectrum; referenceSpectrumBuf.publish(); }
}

int MultiQMatch::getLearningFrameCount() const
{
    const Target t = learningTarget.load(std::memory_order_acquire);
    if (t == Target::Current)   return currentSpectrumBuf.readBuffer().frameCount;
    if (t == Target::Reference) return referenceSpectrumBuf.readBuffer().frameCount;
    return std::max(currentSpectrumBuf.readBuffer().frameCount,
                    referenceSpectrumBuf.readBuffer().frameCount);
}

void MultiQMatch::getCurrentSpectrumDB(float* out, int n) const
{
    std::array<float, NUM_BINS> tmp{};
    currentSpectrumBuf.readBuffer().getAverageMagnitudeDB(tmp.data());
    for (int k = 0; k < n && k < NUM_BINS; ++k) out[k] = tmp[(size_t)k];
}
void MultiQMatch::getReferenceSpectrumDB(float* out, int n) const
{
    std::array<float, NUM_BINS> tmp{};
    referenceSpectrumBuf.readBuffer().getAverageMagnitudeDB(tmp.data());
    for (int k = 0; k < n && k < NUM_BINS; ++k) out[k] = tmp[(size_t)k];
}
void MultiQMatch::getCorrectionCurveDB(float* out, int n) const
{
    if (!correctionValid.load(std::memory_order_acquire))
    {
        for (int k = 0; k < n && k < NUM_BINS; ++k) out[k] = 0.0f;
        return;
    }
    correctionLock.lock();
    for (int k = 0; k < n && k < NUM_BINS; ++k) out[k] = correctionCurveDB[(size_t)k];
    correctionLock.unlock();
}

//==============================================================================
// Correction computation (message thread) — port of EQMatchProcessor::computeCorrection
//==============================================================================
bool MultiQMatch::computeCorrection(float smoothingSemitones, float applyAmount,
                                    float limitBoostDB, float limitCutDB, bool minimumPhase)
{
    const LearnedSpectrum cur = currentSpectrumBuf.readBuffer();
    const LearnedSpectrum ref = referenceSpectrumBuf.readBuffer();
    if (!cur.valid || !ref.valid) return false;

    std::array<float, NUM_BINS> currentDB{}, referenceDB{};
    cur.getAverageMagnitudeDB(currentDB.data());
    ref.getAverageMagnitudeDB(referenceDB.data());

    // Difference curve: reference - current.
    std::array<float, NUM_BINS> diff{};
    double diffSum = 0.0;
    for (int k = 0; k < NUM_BINS; ++k) { diff[(size_t)k] = referenceDB[(size_t)k] - currentDB[(size_t)k]; diffSum += diff[(size_t)k]; }

    // Remove overall level difference (zero-mean).
    float dc = (float)(diffSum / NUM_BINS);
    for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] -= dc;

    // Clamp outliers before smoothing, then re-center.
    for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] = clampf(diff[(size_t)k], -30.0f, 30.0f);
    { double s = 0.0; for (int k = 0; k < NUM_BINS; ++k) s += diff[(size_t)k]; float o = (float)(s / NUM_BINS);
      for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] -= o; }

    if (smoothingSemitones > 0.0f) applyFractionalOctaveSmoothing(diff, smoothingSemitones);

    // Apply amount (-1..+1).
    for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] *= applyAmount;

    // User boost / cut limits.
    if (limitBoostDB > 0.0f) for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] = std::min(diff[(size_t)k], limitBoostDB);
    if (limitCutDB   < 0.0f) for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] = std::max(diff[(size_t)k], limitCutDB);

    // Hard safety limit ±15 dB, then re-center and re-clamp.
    constexpr float HARD = 15.0f;
    for (int k = 0; k < NUM_BINS; ++k) diff[(size_t)k] = clampf(diff[(size_t)k], -HARD, HARD);
    { double s = 0.0; for (int k = 0; k < NUM_BINS; ++k) s += diff[(size_t)k]; float o = (float)(s / NUM_BINS);
      for (int k = 0; k < NUM_BINS; ++k) { diff[(size_t)k] -= o; diff[(size_t)k] = clampf(diff[(size_t)k], -HARD, HARD); } }

    // Store curve for display.
    correctionLock.lock();
    correctionCurveDB = diff;
    correctionLock.unlock();

    lastMinimumPhase = minimumPhase;
    if (minimumPhase) generateMinimumPhaseFIR(diff);
    else              generateLinearPhaseFIR(diff);

    // Sanitise the FIR.
    for (int i = 0; i < FIR_LENGTH; ++i)
        if (!safeIsFinite(correctionFIR[(size_t)i])) correctionFIR[(size_t)i] = 0.0f;

    correctionValid.store(true, std::memory_order_release);
    installFIR();
    return true;
}

void MultiQMatch::applyFractionalOctaveSmoothing(std::array<float, NUM_BINS>& curve, float semitones) const
{
    const float halfOct = (semitones / 12.0f) / 2.0f;
    const float nyquist = (float)(sampleRate * 0.5);
    const float binWidth = nyquist / (float)(NUM_BINS - 1);
    std::array<float, NUM_BINS> sm{};
    const float loMul = std::pow(2.0f, -halfOct), hiMul = std::pow(2.0f, halfOct);
    for (int k = 1; k < NUM_BINS; ++k)
    {
        const float cf = (float)k * binWidth;
        const int lo = std::max(0, (int)(cf * loMul / binWidth));
        const int hi = std::min(NUM_BINS - 1, (int)(cf * hiMul / binWidth));
        float sum = 0.f; int cnt = 0;
        for (int j = lo; j <= hi; ++j) { sum += curve[(size_t)j]; ++cnt; }
        sm[(size_t)k] = cnt > 0 ? sum / (float)cnt : curve[(size_t)k];
    }
    sm[0] = sm[1];
    curve = sm;
}

void MultiQMatch::generateLinearPhaseFIR(const std::array<float, NUM_BINS>& curveDB)
{
    const int N = FFT_SIZE;                 // 4096, genBins == NUM_BINS
    float* re = genRe.data(); float* im = genIm.data();
    for (int i = 0; i < N; ++i) { re[i] = 0.f; im[i] = 0.f; }
    for (int k = 0; k < NUM_BINS; ++k)
        re[k] = std::pow(10.0f, curveDB[(size_t)k] / 20.0f);   // zero-phase magnitude
    // Hermitian upper half (real, even) so the inverse yields a real sequence.
    for (int k = 1; k < NUM_BINS - 1; ++k) { re[N - k] = re[k]; im[N - k] = 0.f; }

    fftAnalysis.inverse(re, im);            // zero-phase impulse (real in re[])

    // Circular shift by N/2 to center + make causal.
    std::array<float, FFT_SIZE> shifted{};
    const int halfLen = N / 2;
    for (int i = 0; i < N; ++i) shifted[(size_t)i] = re[(i + halfLen) % N];

    // Kaiser window (beta 8) over FIR_LENGTH (== N here → startOffset 0).
    const double beta = 8.0, i0b = besselI0(beta);
    const int startOffset = (N - FIR_LENGTH) / 2;
    correctionLock.lock();
    for (int i = 0; i < FIR_LENGTH; ++i)
    {
        const int src = startOffset + i;
        float v = (src >= 0 && src < N) ? shifted[(size_t)src] : 0.f;
        const double t = (2.0 * i / (double)(FIR_LENGTH - 1)) - 1.0;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / i0b;
        correctionFIR[(size_t)i] = v * (float)w;
    }
    correctionLock.unlock();
}

void MultiQMatch::generateMinimumPhaseFIR(const std::array<float, NUM_BINS>& curveDB)
{
    const int N = 1 << 13;                  // 8192
    const int GEN_BINS = N / 2 + 1;         // 4097
    float* re = genRe.data(); float* im = genIm.data();

    // Step 1: log-magnitude spectrum (real), mapped from NUM_BINS to GEN_BINS.
    for (int i = 0; i < N; ++i) { re[i] = 0.f; im[i] = 0.f; }
    for (int k = 0; k < GEN_BINS; ++k)
    {
        const float frac = (float)k / (float)(GEN_BINS - 1);
        int src = (int)(frac * (float)(NUM_BINS - 1));
        if (src > NUM_BINS - 1) src = NUM_BINS - 1;
        float lin = std::pow(10.0f, curveDB[(size_t)src] / 20.0f);
        lin = std::max(lin, 1e-10f);
        re[k] = std::log(lin);
    }
    for (int k = 1; k < GEN_BINS - 1; ++k) { re[N - k] = re[k]; im[N - k] = 0.f; } // hermitian (real,even)

    // Step 2: inverse → real cepstrum in re[].
    fftGenMin.inverse(re, im);

    // Step 3: causal window of the cepstrum.
    for (int nq = 1; nq < N / 2; ++nq) re[nq] *= 2.0f;
    for (int nq = N / 2 + 1; nq < N; ++nq) re[nq] = 0.0f;

    // Step 4: forward FFT of the (real) cepstrum → complex log spectrum.
    for (int i = 0; i < N; ++i) im[i] = 0.0f;
    fftGenMin.forward(re, im);

    // Step 5: exponentiate → complex minimum-phase H(f); fill hermitian upper half.
    for (int k = 0; k < GEN_BINS; ++k)
    {
        const float r = clampf(re[k], -20.0f, 20.0f);
        const float ci = im[k];
        const float er = std::exp(r);
        re[k] = er * std::cos(ci);
        im[k] = er * std::sin(ci);
    }
    for (int k = 1; k < GEN_BINS - 1; ++k) { re[N - k] = re[k]; im[N - k] = -im[k]; } // conj symmetry

    // Step 6: inverse → minimum-phase impulse (real in re[]).
    fftGenMin.inverse(re, im);

    // Step 7: truncate to FIR_LENGTH, fade out the tail (energy is at the start).
    correctionLock.lock();
    for (int i = 0; i < FIR_LENGTH; ++i) correctionFIR[(size_t)i] = (i < N) ? re[i] : 0.f;
    const int fadeLen = FIR_LENGTH / 4;
    const int fadeStart = FIR_LENGTH - fadeLen;
    for (int i = 0; i < fadeLen; ++i)
    {
        const float t = (float)i / (float)fadeLen;
        const float w = 0.5f * (1.0f + (float)std::cos(kMultiQPi * t));
        correctionFIR[(size_t)(fadeStart + i)] *= w;
    }
    correctionLock.unlock();
}

//==============================================================================
// Convolver: build partition spectra + publish (message thread)
//==============================================================================
void MultiQMatch::installFIR()
{
    const int slot = irWriteToggle;
    irWriteToggle ^= 1;
    IRSpectra& dst = irSpectra[(size_t)slot];

    std::array<float, CONV_FFT> re{}, im{};
    correctionLock.lock();
    for (int k = 0; k < NUM_PART; ++k)
    {
        for (int j = 0; j < CONV_FFT; ++j) { re[(size_t)j] = 0.f; im[(size_t)j] = 0.f; }
        for (int j = 0; j < CONV_HOP; ++j) re[(size_t)j] = correctionFIR[(size_t)(k * CONV_HOP + j)];
        fftConv.forward(re.data(), im.data());
        dst.re[(size_t)k] = re;
        dst.im[(size_t)k] = im;
    }
    correctionLock.unlock();

    irActive.store(slot, std::memory_order_release);
    pendingActivate.store(true, std::memory_order_release);
}

//==============================================================================
// Convolver: process (audio thread)
//==============================================================================
void MultiQMatch::processHop(ChannelConv& c, const IRSpectra* H)
{
    // FFT the sliding window (256 real samples).
    for (int j = 0; j < CONV_FFT; ++j) { convScratchRe[(size_t)j] = c.window[(size_t)j]; convScratchIm[(size_t)j] = 0.f; }
    fftConv.forward(convScratchRe.data(), convScratchIm.data());

    // Store into the frequency-domain delay line.
    for (int j = 0; j < CONV_FFT; ++j) { c.fdlRe[(size_t)c.fdlHead][(size_t)j] = convScratchRe[(size_t)j];
                                          c.fdlIm[(size_t)c.fdlHead][(size_t)j] = convScratchIm[(size_t)j]; }

    if (H != nullptr)
    {
        for (int j = 0; j < CONV_FFT; ++j) { convYRe[(size_t)j] = 0.f; convYIm[(size_t)j] = 0.f; }
        for (int k = 0; k < NUM_PART; ++k)
        {
            const int idx = (c.fdlHead - k + NUM_PART) % NUM_PART;
            const auto& fr = c.fdlRe[(size_t)idx]; const auto& fi = c.fdlIm[(size_t)idx];
            const auto& hr = H->re[(size_t)k];     const auto& hi = H->im[(size_t)k];
            for (int j = 0; j < CONV_FFT; ++j)
            {
                const float ar = fr[(size_t)j], ai = fi[(size_t)j];
                const float br = hr[(size_t)j], bi = hi[(size_t)j];
                convYRe[(size_t)j] += ar * br - ai * bi;
                convYIm[(size_t)j] += ar * bi + ai * br;
            }
        }
        fftConv.inverse(convYRe.data(), convYIm.data());
        // Valid linear-convolution output = last CONV_HOP samples.
        for (int j = 0; j < CONV_HOP; ++j)
        {
            c.outRing[(size_t)c.outWrite] = convYRe[(size_t)(CONV_HOP + j)];
            c.outWrite = (c.outWrite + 1) & (OUT_RING - 1);
            ++c.outCount;
        }
    }
    else
    {
        for (int j = 0; j < CONV_HOP; ++j)
        {
            c.outRing[(size_t)c.outWrite] = 0.f;
            c.outWrite = (c.outWrite + 1) & (OUT_RING - 1);
            ++c.outCount;
        }
    }

    c.fdlHead = (c.fdlHead + 1) % NUM_PART;
    for (int j = 0; j < CONV_HOP; ++j) c.window[(size_t)j] = c.window[(size_t)(CONV_HOP + j)];
}

void MultiQMatch::process(float* left, float* right, bool isStereo, int numSamples)
{
    if (convState.load(std::memory_order_acquire) == ConvState::Inactive) return; // passthrough
    if (numSamples <= 0) return;

    const int slot = irActive.load(std::memory_order_acquire);
    const IRSpectra* H = (slot >= 0) ? &irSpectra[(size_t)slot] : nullptr;
    const ConvState state = convState.load(std::memory_order_relaxed);
    wetGain.setTargetValue(state == ConvState::Clearing ? 0.0f : 1.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = isStereo ? right[i] : inL;

        // Aligned (128-sample) dry delay.
        const float dryL = convL.dryDelay[(size_t)convL.dryPos]; convL.dryDelay[(size_t)convL.dryPos] = inL;
        convL.dryPos = (convL.dryPos + 1) % CONV_HOP;
        float dryR = dryL;
        if (isStereo) { dryR = convR.dryDelay[(size_t)convR.dryPos]; convR.dryDelay[(size_t)convR.dryPos] = inR;
                        convR.dryPos = (convR.dryPos + 1) % CONV_HOP; }

        // Stage the input into the sliding window's second half.
        convL.window[(size_t)(CONV_HOP + hopFill)] = inL;
        if (isStereo) convR.window[(size_t)(CONV_HOP + hopFill)] = inR;
        ++hopFill;
        if (hopFill >= CONV_HOP)
        {
            processHop(convL, H);
            if (isStereo) processHop(convR, H);
            hopFill = 0;
        }

        // Pull one convolved output sample (FIFO seeded with CONV_HOP zeros).
        float wetL = 0.f, wetR = 0.f;
        if (convL.outCount > 0) { wetL = convL.outRing[(size_t)convL.outRead]; convL.outRead = (convL.outRead + 1) & (OUT_RING - 1); --convL.outCount; }
        if (isStereo) { if (convR.outCount > 0) { wetR = convR.outRing[(size_t)convR.outRead]; convR.outRead = (convR.outRead + 1) & (OUT_RING - 1); --convR.outCount; } }
        else wetR = wetL;

        const float w = wetGain.getNextValue();
        const float dw = 1.0f - w;
        left[i] = dryL * dw + wetL * w;
        if (isStereo) right[i] = dryR * dw + wetR * w;
    }

    // Finalise a clear once the wet path has faded out.
    if (state == ConvState::Clearing && !wetGain.isSmoothing() && wetGain.getCurrentValue() < 1e-6f)
    {
        irActive.store(-1, std::memory_order_release);
        convState.store(ConvState::Inactive, std::memory_order_release);
        convL.reset(); convR.reset(); hopFill = 0;
    }
}

void MultiQMatch::doClearNow()
{
    learningTarget.store(Target::None, std::memory_order_release);
    currentSpectrum.reset();
    referenceSpectrum.reset();
    currentSpectrumBuf.writeBuffer().reset(); currentSpectrumBuf.publish();
    currentSpectrumBuf.writeBuffer().reset(); currentSpectrumBuf.publish();
    referenceSpectrumBuf.writeBuffer().reset(); referenceSpectrumBuf.publish();
    referenceSpectrumBuf.writeBuffer().reset(); referenceSpectrumBuf.publish();

    // correctionValid gates every reader of correctionCurveDB/correctionFIR (UI +
    // serialize), so clearing the flag is enough — leave the arrays untouched so
    // the audio thread stays lock-free (no correctionLock here).
    correctionValid.store(false, std::memory_order_release);

    // Fade the convolver out (or drop immediately if inactive).
    if (convState.load(std::memory_order_relaxed) != ConvState::Inactive)
        convState.store(ConvState::Clearing, std::memory_order_release);
    else
        irActive.store(-1, std::memory_order_release);
}

int MultiQMatch::getLatencySamples() const noexcept
{
    if (convState.load(std::memory_order_acquire) == ConvState::Inactive) return 0;
    int lat = CONV_HOP;
    if (!lastMinimumPhase) lat += FIR_LENGTH / 2;   // linear-phase group delay (unused by the shell)
    return lat;
}

//==============================================================================
// State persistence (message thread)
//==============================================================================
namespace
{
    const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string b64encode(const uint8_t* data, size_t len)
    {
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        size_t i = 0;
        for (; i + 3 <= len; i += 3)
        {
            const uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out += kB64[(n >> 18) & 63]; out += kB64[(n >> 12) & 63];
            out += kB64[(n >> 6) & 63];  out += kB64[n & 63];
        }
        if (len - i == 1)
        {
            const uint32_t n = data[i] << 16;
            out += kB64[(n >> 18) & 63]; out += kB64[(n >> 12) & 63]; out += '='; out += '=';
        }
        else if (len - i == 2)
        {
            const uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
            out += kB64[(n >> 18) & 63]; out += kB64[(n >> 12) & 63]; out += kB64[(n >> 6) & 63]; out += '=';
        }
        return out;
    }

    int b64val(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    std::vector<uint8_t> b64decode(const std::string& s)
    {
        std::vector<uint8_t> out;
        out.reserve(s.size() * 3 / 4);
        int buf = 0, bits = 0;
        for (char c : s)
        {
            if (c == '=' ) break;
            const int v = b64val(c);
            if (v < 0) continue;
            buf = (buf << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((buf >> bits) & 0xFF)); }
        }
        return out;
    }

    template<typename T> void put(std::vector<uint8_t>& v, const T& x)
    { const uint8_t* p = reinterpret_cast<const uint8_t*>(&x); v.insert(v.end(), p, p + sizeof(T)); }
}

std::string MultiQMatch::serialize() const
{
    const LearnedSpectrum cur = currentSpectrumBuf.readBuffer();
    const LearnedSpectrum ref = referenceSpectrumBuf.readBuffer();

    std::vector<uint8_t> blob;
    blob.reserve(60000);
    put(blob, (uint8_t)'M'); put(blob, (uint8_t)'Q'); put(blob, (uint8_t)'M'); put(blob, (uint8_t)'1');
    put(blob, (int32_t)(cur.valid ? 1 : 0)); put(blob, (int32_t)cur.frameCount);
    put(blob, (int32_t)(ref.valid ? 1 : 0)); put(blob, (int32_t)ref.frameCount);
    put(blob, (int32_t)(correctionValid.load(std::memory_order_acquire) ? 1 : 0));
    for (int k = 0; k < NUM_BINS; ++k) put(blob, cur.powerSum[(size_t)k]);
    for (int k = 0; k < NUM_BINS; ++k) put(blob, ref.powerSum[(size_t)k]);
    correctionLock.lock();
    for (int k = 0; k < NUM_BINS; ++k)  put(blob, correctionCurveDB[(size_t)k]);
    for (int i = 0; i < FIR_LENGTH; ++i) put(blob, correctionFIR[(size_t)i]);
    correctionLock.unlock();

    return b64encode(blob.data(), blob.size());
}

bool MultiQMatch::deserialize(const std::string& base64)
{
    std::vector<uint8_t> blob = b64decode(base64);
    const size_t expected = 4 + 5 * sizeof(int32_t)
                          + (size_t)NUM_BINS * sizeof(double) * 2
                          + (size_t)NUM_BINS * sizeof(float)
                          + (size_t)FIR_LENGTH * sizeof(float);
    if (blob.size() < expected) return false;
    if (!(blob[0] == 'M' && blob[1] == 'Q' && blob[2] == 'M' && blob[3] == '1')) return false;

    size_t off = 4;
    auto getI = [&](int32_t& x) { std::memcpy(&x, blob.data() + off, sizeof(int32_t)); off += sizeof(int32_t); };
    int32_t hasCur, curFrames, hasRef, refFrames, hasCorr;
    getI(hasCur); getI(curFrames); getI(hasRef); getI(refFrames); getI(hasCorr);

    learningTarget.store(Target::None, std::memory_order_release);
    currentSpectrum.reset(); referenceSpectrum.reset();

    for (int k = 0; k < NUM_BINS; ++k) { std::memcpy(&currentSpectrum.powerSum[(size_t)k],   blob.data() + off, sizeof(double)); off += sizeof(double); }
    for (int k = 0; k < NUM_BINS; ++k) { std::memcpy(&referenceSpectrum.powerSum[(size_t)k], blob.data() + off, sizeof(double)); off += sizeof(double); }
    currentSpectrum.frameCount = curFrames;   currentSpectrum.valid = hasCur && curFrames >= 3;
    referenceSpectrum.frameCount = refFrames; referenceSpectrum.valid = hasRef && refFrames >= 3;
    currentSpectrumBuf.writeBuffer() = currentSpectrum;     currentSpectrumBuf.publish();
    referenceSpectrumBuf.writeBuffer() = referenceSpectrum; referenceSpectrumBuf.publish();

    correctionLock.lock();
    for (int k = 0; k < NUM_BINS; ++k)  { std::memcpy(&correctionCurveDB[(size_t)k], blob.data() + off, sizeof(float)); off += sizeof(float); }
    for (int i = 0; i < FIR_LENGTH; ++i) { std::memcpy(&correctionFIR[(size_t)i],    blob.data() + off, sizeof(float)); off += sizeof(float); }
    correctionLock.unlock();

    if (hasCorr)
    {
        correctionValid.store(true, std::memory_order_release);
        installFIR();   // rebuild partition spectra + request activation
    }
    else
    {
        correctionValid.store(false, std::memory_order_release);
    }
    return true;
}

} // namespace duskaudio
