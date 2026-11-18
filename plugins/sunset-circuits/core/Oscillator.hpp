// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Oscillator.hpp - band-limited (minimum-phase BLEP / polyBLEP) oscillators, sub-oscillator, ring/
// cross modulation helpers, sample & hold, and a pink-noise generator.
//
// Framework-free port of the JUCE OscillatorEngine.h. Every waveform formula is
// carried over verbatim; substantive changes are (1) noise uses a per-instance
// xorshift PRNG (SynthCommon) instead of std::mt19937/random_device, and (2) the
// polyBLEP trailing-edge sign was corrected (the JUCE original's -1 form was a
// bug; see polyBlep). Prepared at the INTERNAL (oversampled) rate by the
// voice — dt = freq / internalRate — so pitch is correct at every OS factor.

#pragma once

#include "SynthCommon.hpp"
#include "MinBlepTable.hpp"

namespace msynth
{

enum class Waveform
{
    Saw = 0,
    Square,
    Triangle,
    Sine,
    Pulse,   // square with variable pulse width
    Noise    // white noise
};

// The two halves of polyBlep(): the part before an edge (on the sample the
// edge follows) and the part after it (on the sample after the edge).
inline float polyBlepBefore(float t, float dt) noexcept
{
    if (t > 1.0f - dt) { const float n = (t - 1.0f) / dt; return n * n + n + n + 1.0f; }
    return 0.0f;
}
inline float polyBlepAfter(float t, float dt) noexcept
{
    if (t < dt) { const float n = t / dt; return n + n - n * n - 1.0f; }
    return 0.0f;
}

// PolyBLEP residual for antialiased edges (adapted from the JUCE original;
// the trailing-edge sign was fixed here — the original returned n*n+n+n-1,
// the corrupted form inherited verbatim, instead of the canonical (n+1)^2).
inline float polyBlep(float t, float dt) noexcept
{
    if (t < dt)
    {
        const float n = t / dt;
        return n + n - n * n - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        const float n = (t - 1.0f) / dt;
        return n * n + n + n + 1.0f;
    }
    return 0.0f;
}

// ========================== MINIMUM-PHASE BLEP (#303) ==========================
// Below kMinBlepMaxRate the oscillator band-limits every step of its waveform
// (the saw's wrap, the square's and pulse's edges, the square inside the
// triangle, and every hard-sync reset) with a minimum-phase BLEP: the residual
// of a 16-sample windowed-sinc step turned minimum phase (MinBlepTable.hpp), so
// all of it lands on the samples AFTER the step and nothing is delayed to apply
// it. Residuals accumulate in a short ring that each sample reads once. The
// 2-point polyBLEP it replaces folded back at -34 to -36 dB (C6 to C7, saw alone,
// 1x) and -55 to -58 dB at 2x; this one reaches the measurement floor (-74 to
// -82 dB) at both. Corners (the sine's and the triangle's at a sync reset) keep
// the 2-point polyBLAMP, which a corner needs far less than a step needs a BLEP.
//
// A minimum-phase kernel realises each step about kMinBlepDelaySamples (2.1
// samples at the oscillator's rate) later than it falls, so the oscillators'
// output is that much later on average; MultiSynthDSP takes it off the latency
// pad (blepDelay()). From kMinBlepMaxRate (4x, 176.4 kHz and up) the pad has no
// room left for it, and the 2-point polyBLEP is already at the floor there
// (-73 dB), so the oscillator keeps it: the same code as before, bit for bit.
class Oscillator
{
public:
    static constexpr double kMinBlepMaxRate = 176400.0;
    // Average delay of the oscillator's output at an internal rate, in samples of
    // that rate (see MINIMUM-PHASE BLEP).
    static double blepDelay(double rate) noexcept { return rate < kMinBlepMaxRate ? kMinBlepDelaySamples : 0.0; }

    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        phase = 0.0f;
        triState = 0.0f; triStatePoly = 0.0f;
        useMinBlep = sampleRate < kMinBlepMaxRate;
        clearResiduals();
        updateTriLeak();
    }

    void setFrequency(float freqHz) noexcept { freq = freqHz; dt = freq / sr; }
    // Change rate WITHOUT resetting phase (for oversampling-factor changes). The
    // residuals are cleared when the band-limiting changes kind: an
    // oversampling change runs with the voice faded out (MultiSynthDSP).
    void setSampleRate(double sampleRate) noexcept
    {
        sr = (float)sampleRate; dt = freq / sr; updateTriLeak();
        const bool minBlep = sampleRate < kMinBlepMaxRate;
        if (minBlep != useMinBlep)
        {
            useMinBlep = minBlep; clearResiduals();
            syncCarry = syncSqCarry = 0.0f; prevSynced = false;
        }
    }
    void setWaveform(Waveform w) noexcept
    {
        if (w == waveform) return;
        // The square's residuals belong to the triangle's integrator only.
        if (w == Waveform::Triangle || waveform == Waveform::Triangle)
            for (float& r : sqRes) r = 0.0f;
        waveform = w;
    }
    void setPulseWidth(float pw) noexcept    { pulseWidth = clampf(pw, 0.05f, 0.95f); }
    // For a modulation that may close the pulse altogether (Oracle's filter
    // envelope -> PW A): down to 0, where the pulse is gone and only DC is left.
    void setPulseWidthMayClose(float pw) noexcept { pulseWidth = clampf(pw, 0.0f, 0.95f); }
    // Called per internal sample with a detune that rarely moves: the pow() runs
    // only when it does (bit-compared, see sameBits).
    void setDetune(float cents) noexcept
    {
        if (sameBits(cents, detuneCents)) return;
        detuneCents = cents;
        detuneRatio = std::pow(2.0f, cents / 1200.0f);
    }

    float getPhase() const noexcept { return phase; }
    // The value a modulation route should read for the sample just rendered: the
    // output itself where the polyBLEP is in use, and with the minimum-phase BLEP
    // the polyBLEP form of the same sample. The minimum-phase output is about
    // kMinBlepDelaySamples later than the oscillator's phase; a modulator read
    // from it would move its carrier that much late, which changes an audio-rate
    // FM's sidebands (up to 2.4 dB on the FM factory presets). A synced
    // oscillator hands over its output (no factory preset modulates from one).
    float modulationValue() const noexcept
    {
        if (!modLazy) return modValue;
        // Worked out only when a route asks (most oscillators are no route's
        // source): the polyBLEP form at the phase the sample was rendered at.
        const float q = modPhase, d = modDt;
        switch (waveform)
        {
            case Waveform::Saw:    return 2.0f * q - 1.0f - polyBlep(q, d);
            case Waveform::Square: return (q < 0.5f ? 1.0f : -1.0f) + polyBlep(q, d) - polyBlep(std::fmod(q + 0.5f, 1.0f), d);
            case Waveform::Pulse:  return (q < pulseWidth ? 1.0f : -1.0f) + polyBlep(q, d)
                                        - polyBlep(std::fmod(q + (1.0f - pulseWidth), 1.0f), d);
            case Waveform::Sine:   return std::sin(q * kTwoPi);
            default:               return modValue;
        }
    }
    bool  didCross() const noexcept { return lastCrossed; }
    // Where in the interval after the last processSample() the phase wrapped, as
    // a fraction (0, 1] of that interval. Meaningful when didCross() is true; a
    // hard-sync master hands it to its slave (processSampleSynced).
    float wrapFraction() const noexcept { return lastWrapFrac; }
    void  resetPhase() noexcept
    {
        phase = 0.0f; triState = 0.0f; triStatePoly = 0.0f;
        syncCarry = 0.0f; syncSqCarry = 0.0f; prevSynced = false;
        clearResiduals();
    }

    // ============================ HARD SYNC (slave) ===========================
    // One sample of a hard-synced oscillator. `syncFrac` is where the master
    // wrapped in the interval this sample starts (its wrapFraction()), or 0 when
    // it did not wrap.
    //
    // The old sync set the phase to 0 before the sample was rendered: up to one
    // sample early, on the internal sample grid (so the reset point jittered by up
    // to a sample from cycle to cycle), and as a bare step. The discontinuity
    // then folded back at full strength, and the oscillator's own polyBLEP read
    // the reset as an ordinary wrap and corrected a step of the wrong size.
    //
    // Here the sample is rendered at the current phase, and the interval up to
    // the next sample is walked as a sequence of events: each value step of the
    // waveform (its own edges, and the reset to phase 0 at syncFrac) gets the two
    // halves of a polyBLEP sized to that step's true height, the half before the
    // step on this sample and the half after it on the next one (syncCarry); a
    // corner the reset cuts into a sine or a triangle gets a polyBLAMP the same
    // way. Then the phase restarts where it would be (1 - syncFrac) of a sample
    // after the reset. For a free-running interval this is the same polyBLEP as
    // processSample(), so a synced oscillator between resets sounds as before.
    float processSampleSynced(float syncFrac) noexcept
    {
        const float d = dt * detuneRatio;
        const bool reset = syncFrac > 0.0f && syncFrac <= 1.0f;
        if (useMinBlep) return processSampleSyncedMinBlep(d, reset ? syncFrac : 0.0f);
        if (waveform == Waveform::Triangle)
            return processTriangleSynced(d, reset ? syncFrac : 0.0f);
        if (waveform == Waveform::Noise)
            return processSample();            // no phase to sync

        float out = stepValue(phase) + syncCarry;
        syncCarry = 0.0f;
        // Sync just switched on: the free sample before this one put the half
        // before its edge on itself and leaves the half after it to this sample.
        if (!prevSynced) out += edgeHalvesAfter(waveform, phase, d);
        prevSynced = true;
        float p = phase;
        walkEdges(waveform, p, d, 0.0f, reset ? syncFrac : 1.0f, out, syncCarry);
        if (reset)
        {
            addStep(stepValue(0.0f) - stepValue(p), syncFrac, out, syncCarry);
            // A sine also turns a corner at the reset: its slope jumps from where
            // the cycle was cut to the slope at phase 0.
            if (waveform == Waveform::Sine)
                addRamp(kTwoPi * d * (1.0f - std::cos(kTwoPi * p)), syncFrac, out, syncCarry);
            p = 0.0f;
            walkEdges(waveform, p, d, syncFrac, 1.0f, out, syncCarry);
        }
        lastCrossed = reset || p < phase;
        phase = p;
        modValue = out; modLazy = false;
        return out;
    }

    void seedNoise(uint32_t s) noexcept { rng.seed(s); }

    // FM depth is calibrated in cycles per sample at 96 kHz (the existing
    // 48 kHz / 2x default). Convert to the current internal rate so the same
    // patch retains its pitch/deviation when sample rate or oversampling changes.
    void applyFM(float fmAmount) noexcept
    {
        phase += fmAmount * (96000.0f / sr);
        // Constant-time wrap. The while loops this replaces ran once per whole
        // cycle of overshoot, so their cost was proportional to |fmAmount| -- a
        // large modulation value would have spun the audio thread. Nothing
        // reaches that today (cross-mod scales the modulator to +-0.03), so this
        // is insurance, and it is bit-identical for every |fmAmount| < 1: floor()
        // subtracts exactly the one cycle a single loop iteration would have.
        phase -= std::floor(phase);
        // floor() leaves NaN as NaN and cannot bring an infinity back in range.
        if (!(phase >= 0.0f && phase < 1.0f)) phase = 0.0f;
    }

    float processSample() noexcept
    {
        const float effectiveDt = dt * detuneRatio;
        if (useMinBlep) return processSampleMinBlep(effectiveDt);
        float sample = 0.0f;
        // Sync just switched off: the synced sample before this one already put
        // both halves of its edges down (the second in syncCarry / syncSqCarry), so
        // the halves after an edge are taken out here (see edgeHalvesAfter).
        const bool afterSynced = prevSynced;
        prevSynced = false;
        const float owed = afterSynced ? edgeHalvesAfter(waveform, phase, effectiveDt) : 0.0f;

        switch (waveform)
        {
            case Waveform::Saw:
                sample = 2.0f * phase - 1.0f;
                sample -= polyBlep(phase, effectiveDt);
                break;

            case Waveform::Square:
                sample = phase < 0.5f ? 1.0f : -1.0f;
                sample += polyBlep(phase, effectiveDt);
                sample -= polyBlep(std::fmod(phase + 0.5f, 1.0f), effectiveDt);
                break;

            case Waveform::Pulse:
                if (pulseWidth >= kNarrowPulseSamples * effectiveDt)
                {
                    sample = phase < pulseWidth ? 1.0f : -1.0f;
                    sample += polyBlep(phase, effectiveDt);
                    sample -= polyBlep(std::fmod(phase + (1.0f - pulseWidth), 1.0f), effectiveDt);
                }
                else
                {
                    // A pulse narrower than kNarrowPulseSamples (the Poly-Mod routes
                    // can close it) is drawn that wide and scaled to the same area:
                    // the same level and DC, and two polyBLEP edges that no longer
                    // share a sample (sharing one, they folded back at -46 to -56 dB
                    // at 4x).
                    const float w = narrowPulseWidth(effectiveDt);
                    const float g = pulseWidth > 0.0f ? pulseWidth / w : 0.0f;
                    sample = phase < w ? 1.0f : -1.0f;
                    sample += polyBlep(phase, effectiveDt);
                    sample -= polyBlep(std::fmod(phase + (1.0f - w), 1.0f), effectiveDt);
                    sample = g * (sample + 1.0f) - 1.0f;
                }
                break;

            case Waveform::Triangle:
            {
                float sq = phase < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep(phase, effectiveDt);
                sq -= polyBlep(std::fmod(phase + 0.5f, 1.0f), effectiveDt);
                if (afterSynced) sq += syncSqCarry - edgeHalvesAfter(Waveform::Square, phase, effectiveDt);
                // Leaky integrator; leak derived from sr so the DC-block corner
                // (~7 Hz) is constant across oversampling factors (a fixed 0.999
                // per-sample leak would move the corner up with the OS rate).
                // Read BEFORE the update, as the synced triangle reads (it has to,
                // to correct a reset on both sides): a different convention for the
                // two would skip or repeat a sample whenever sync is switched.
                sample = triState;
                triState = triLeak * triState + effectiveDt * sq * 4.0f;
                break;
            }

            case Waveform::Sine:
                sample = std::sin(phase * kTwoPi);
                break;

            case Waveform::Noise:
                sample = rng.nextBipolar();
                break;
        }

        // The half-step a synced render left for this sample (sync was switched off
        // with one pending). Zero on every other path, which then stays untouched.
        if (waveform != Waveform::Triangle) sample -= owed;
        if (syncCarry != 0.0f) { sample += syncCarry; syncCarry = 0.0f; }
        syncSqCarry = 0.0f;

        const float prevPhase = phase;
        phase += effectiveDt;
        // Full wrap via floor handles pathological dt > 1 (extreme freq/OS);
        // for the normal dt < 1 case this is identical to a single subtraction
        // (floor(phase) is exactly 1 when 1 <= phase < 2). (C3)
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        lastCrossed = (prevPhase > phase); // wrapped at least once this sample
        if (lastCrossed)
            lastWrapFrac = clampf((1.0f - prevPhase) / effectiveDt, 0.0f, 1.0f);

        modValue = sample; modLazy = false;
        return sample;
    }

private:
    // Two halves of a polyBLEP for a step of height h at fraction s (0, 1] of the
    // interval after this sample: (h/2)(1-s)^2 on this sample, -(h/2)s^2 on the
    // next. The kernel is polyBlep()'s: for the saw's own wrap (h = -2) these are
    // exactly its pre- and post-wrap terms.
    static void addStep(float h, float s, float& now, float& next) noexcept
    {
        const float a = 1.0f - s;
        now  += 0.5f * h * a * a;
        next -= 0.5f * h * s * s;
    }

    // The same for a corner, where the slope (value per sample) changes by g at
    // s: the polyBLAMP, the polyBLEP integrated, g(1-s)^3/6 now and g s^3/6 next.
    static void addRamp(float g, float s, float& now, float& next) noexcept
    {
        const float a = 1.0f - s;
        now  += g * a * a * a * (1.0f / 6.0f);
        next += g * s * s * s * (1.0f / 6.0f);
    }

    // The halves after an edge that the free path's polyBLEP puts on the sample
    // following each edge of waveform w (phase q at that sample): what a free
    // sample leaves for the next one, and what a free sample after a synced one
    // must not add again.
    float edgeHalvesAfter(Waveform w, float q, float d) const noexcept
    {
        switch (w)
        {
            case Waveform::Saw:    return -polyBlepAfter(q, d);
            case Waveform::Square: return polyBlepAfter(q, d) - polyBlepAfter(std::fmod(q + 0.5f, 1.0f), d);
            case Waveform::Pulse:
            {
                const float w = narrowPulseWidth(d);
                const float g = w > pulseWidth ? (pulseWidth > 0.0f ? pulseWidth / w : 0.0f) : 1.0f;
                return g * (polyBlepAfter(q, d) - polyBlepAfter(std::fmod(q + (1.0f - w), 1.0f), d));
            }
            default:               return 0.0f;
        }
    }

    // The polyBLEP's narrowest pulse, as a fraction of the cycle (see processSample()).
    static constexpr float kNarrowPulseSamples = 2.0f;
    float narrowPulseWidth(float d) const noexcept
    {
        const float wMin = kNarrowPulseSamples * d;
        return pulseWidth < wMin ? (wMin < 0.5f ? wMin : 0.5f) : pulseWidth;
    }

    // ---- minimum-phase BLEP (see MINIMUM-PHASE BLEP) ----
    static constexpr int kResSize = 32;   // power of two above kMinBlepTaps
    static constexpr int kResMask = kResSize - 1;
    static_assert(kMinBlepTaps < kResSize, "residual ring too small for the BLEP");
    // The saw's ramp is not delayed while its wraps are realised
    // kMinBlepDelaySamples late: that moves its mean up by the ramp's slope times
    // that delay (9 % of full scale on a C7 at 2x). Taking it back off keeps the
    // saw centred, as the polyBLEP saw is, so the filters see the same signal.
    static constexpr float kSawDelayCorrection = 2.0f * (float)kMinBlepDelaySamples;

    void clearResiduals() noexcept
    {
        for (float& r : res) r = 0.0f;
        for (float& r : sqRes) r = 0.0f;
    }
    MSYNTH_INLINE float take(float* ring) noexcept
    {
        float& r = ring[resPos & kResMask];
        const float v = r;
        r = 0.0f;
        return v;
    }
    // A step of height h at fraction s (0, 1] of the interval after the current
    // sample reaches samples resPos + 1 .. resPos + kMinBlepTaps, at j - s
    // samples after it.
    void addMinBlep(float* ring, float h, float s) noexcept
    {
        constexpr int kLast = kMinBlepTaps * kMinBlepPointsPerSample;
        for (int j = 1; j <= kMinBlepTaps; ++j)
        {
            const float q = ((float)j - s) * (float)kMinBlepPointsPerSample;
            const int i = (int)q;
            if (i >= kLast) continue;   // past the kernel: the residual is 0
            const float f = q - (float)i;
            ring[(resPos + j) & kResMask] += h * (kMinBlepResidual[i] + f * (kMinBlepResidual[i + 1] - kMinBlepResidual[i]));
        }
    }
    // walkEdges() with each step going into a residual ring.
    void walkEdgesMinBlep(Waveform w, float& p, float d, float s0, float s1, float* ring) noexcept
    {
        float s = s0, edge = 0.0f, height = 0.0f;
        for (int guard = 0; guard < 16 && d > 0.0f && nextEdge(w, p, edge, height); ++guard)
        {
            if (p + d * (s1 - s) < edge) break;   // no edge before s1: skip the division
            const float at = s + (edge - p) / d;
            if (at > s1) break;
            addMinBlep(ring, height, at);
            s = at;
            p = edge >= 1.0f ? 0.0f : edge;
        }
        p += d * (s1 - s);
        if (p >= 1.0f) p -= std::floor(p);
    }

    MSYNTH_INLINE float processSampleMinBlep(float d) noexcept
    {
        float sample = 0.0f;
        switch (waveform)
        {
            case Waveform::Saw:
                sample = 2.0f * phase - 1.0f - kSawDelayCorrection * d;
                modLazy = true;
                break;
            case Waveform::Square:
                sample = phase < 0.5f ? 1.0f : -1.0f;
                modLazy = true;
                break;
            case Waveform::Pulse:
                sample = phase < pulseWidth ? 1.0f : -1.0f;
                modLazy = true;
                break;
            case Waveform::Triangle:
            {
                // The band-limited square, integrated (see processSample()); the
                // modulation value integrates the polyBLEP square alongside.
                // Read before the update, as everywhere (see processSample()).
                const float naive = phase < 0.5f ? 1.0f : -1.0f;
                const float sq = naive + take(sqRes);
                sample = triState;
                triState = triLeak * triState + d * sq * 4.0f;
                const float sqPoly = naive + polyBlep(phase, d) - polyBlep(std::fmod(phase + 0.5f, 1.0f), d);
                modValue = triStatePoly; modLazy = false;
                triStatePoly = triLeak * triStatePoly + d * sqPoly * 4.0f;
                break;
            }
            case Waveform::Sine:
                // No steps to band-limit; read kMinBlepDelaySamples back so it keeps
                // its place against the other waveforms and the latency pad.
                sample = std::sin((phase - (float)kMinBlepDelaySamples * d) * kTwoPi);
                modLazy = true;
                break;
            case Waveform::Noise:
                sample = rng.nextBipolar();
                modValue = sample; modLazy = false;
                break;
        }
        modPhase = phase; modDt = d;
        sample += take(res);   // steps already past (for the triangle: sync resets)

        const float prevPhase = phase;
        advanceMinBlep(d);
        lastCrossed = prevPhase > phase;
        if (lastCrossed)
            lastWrapFrac = clampf((1.0f - prevPhase) / d, 0.0f, 1.0f);
        ++resPos;
        return sample;
    }

    // Advance the phase over the interval after this sample and place its steps.
    // The common case, at most one edge in the interval, is done directly;
    // anything denser (a pulse narrower than a sample, a pathological increment)
    // goes through the general walk.
    MSYNTH_INLINE void advanceMinBlep(float d) noexcept
    {
        const float p0 = phase;
        float p1 = p0 + d;
        switch (waveform)
        {
            case Waveform::Saw:
                if (d >= 1.0f) break;
                if (p1 >= 1.0f) { addMinBlep(res, -2.0f, (1.0f - p0) / d); p1 -= 1.0f; }
                phase = p1;
                return;
            case Waveform::Square:
            case Waveform::Pulse:
            case Waveform::Triangle:
            {
                const float w = waveform == Waveform::Pulse ? pulseWidth : 0.5f;
                if (!(w > d && 1.0f - w > d)) break;
                float* ring = waveform == Waveform::Triangle ? sqRes : res;
                if (p0 < w && p1 >= w)  addMinBlep(ring, -2.0f, (w - p0) / d);
                else if (p1 >= 1.0f) { addMinBlep(ring, 2.0f, (1.0f - p0) / d); p1 -= 1.0f; }
                phase = p1;
                return;
            }
            case Waveform::Sine:
            case Waveform::Noise:
                if (p1 >= 1.0f) p1 -= std::floor(p1);
                phase = p1;
                return;
        }
        float p = p0;
        if (waveform == Waveform::Triangle) walkEdgesMinBlep(Waveform::Square, p, d, 0.0f, 1.0f, sqRes);
        else                                walkEdgesMinBlep(waveform, p, d, 0.0f, 1.0f, res);
        phase = p;
    }

    // processSampleSynced() and processTriangleSynced() with BLEP steps into the
    // rings. The polyBLAMP for a corner still takes its two halves, the second on
    // the next sample's slot of the ring.
    MSYNTH_INLINE float processSampleSyncedMinBlep(float d, float syncFrac) noexcept
    {
        if (waveform == Waveform::Triangle) return processTriangleSyncedMinBlep(d, syncFrac);
        if (waveform == Waveform::Noise) return processSample();

        const bool reset = syncFrac > 0.0f;
        float out = stepValue(phase) + take(res);
        if (waveform == Waveform::Saw) out -= kSawDelayCorrection * d;
        if (!reset && waveform != Waveform::Sine)
        {
            // No reset in this interval (the master wraps once a cycle): the same
            // advance as the free oscillator.
            const float prevPhase = phase;
            advanceMinBlep(d);
            lastCrossed = phase < prevPhase;
            ++resPos;
            modValue = out; modLazy = false;
            return out;
        }
        float& next = res[(resPos + 1) & kResMask];
        float p = phase;
        walkEdgesMinBlep(waveform, p, d, 0.0f, reset ? syncFrac : 1.0f, res);
        if (reset)
        {
            addMinBlep(res, stepValue(0.0f) - stepValue(p), syncFrac);
            if (waveform == Waveform::Sine)
                addRamp(kTwoPi * d * (1.0f - std::cos(kTwoPi * p)), syncFrac, out, next);
            p = 0.0f;
            walkEdgesMinBlep(waveform, p, d, syncFrac, 1.0f, res);
        }
        lastCrossed = reset || p < phase;
        phase = p;
        ++resPos;
        modValue = out; modLazy = false;
        return out;
    }

    float processTriangleSyncedMinBlep(float d, float syncFrac) noexcept
    {
        float out = triState + take(res);
        float& next = res[(resPos + 1) & kResMask];
        float p = phase;
        if (syncFrac <= 0.0f)
        {
            const float sq = (p < 0.5f ? 1.0f : -1.0f) + take(sqRes);
            walkEdgesMinBlep(Waveform::Square, p, d, 0.0f, 1.0f, sqRes);
            triState = triLeak * triState + d * sq * 4.0f;
        }
        else
        {
            for (float& r : sqRes) r = 0.0f;   // they would correct the integrator the reset throws away
            const float atReset = triState + 4.0f * d * squareIntegral(p, d, 0.0f, syncFrac);
            const float levelBefore = p < 0.5f ? 1.0f : -1.0f;
            addMinBlep(res, -atReset, syncFrac);
            addRamp(4.0f * d * (1.0f - levelBefore), syncFrac, out, next);
            p = 0.0f;
            triState = 4.0f * d * squareIntegral(p, d, syncFrac, 1.0f);
        }
        lastCrossed = syncFrac > 0.0f || p < phase;
        phase = p;
        ++resPos;
        modValue = out; modLazy = false;
        triStatePoly = triState;
        return out;
    }

    // The naive (uncorrected) value of a stepped waveform at phase q. Sine has no
    // edges of its own but does step when it is reset.
    float stepValue(float q) const noexcept
    {
        switch (waveform)
        {
            case Waveform::Saw:    return 2.0f * q - 1.0f;
            case Waveform::Square: return q < 0.5f ? 1.0f : -1.0f;
            case Waveform::Pulse:  return q < pulseWidth ? 1.0f : -1.0f;
            case Waveform::Sine:   return std::sin(q * kTwoPi);
            default:               return 0.0f;
        }
    }

    // The next value edge of waveform w after phase p, in (p, 1], and its
    // height. False for a waveform without edges.
    bool nextEdge(Waveform w, float p, float& edge, float& height) const noexcept
    {
        switch (w)
        {
            case Waveform::Saw:
                edge = 1.0f; height = -2.0f; return true;
            case Waveform::Square:
                if (p < 0.5f) { edge = 0.5f; height = -2.0f; } else { edge = 1.0f; height = 2.0f; }
                return true;
            case Waveform::Pulse:
                if (pulseWidth <= 0.0f) return false;   // closed: no edges, constant low
                if (p < pulseWidth) { edge = pulseWidth; height = -2.0f; } else { edge = 1.0f; height = 2.0f; }
                return true;
            default:
                return false;
        }
    }

    // Advance phase p through the part [s0, s1) of the interval at increment d,
    // adding both polyBLEP halves for every edge of w crossed on the way. The
    // guard only matters for a pathological increment (several cycles per
    // sample); it bounds the work, and the phase still lands where it should.
    void walkEdges(Waveform w, float& p, float d, float s0, float s1,
                   float& now, float& next) const noexcept
    {
        float s = s0, edge = 0.0f, height = 0.0f;
        for (int guard = 0; guard < 16 && d > 0.0f && nextEdge(w, p, edge, height); ++guard)
        {
            const float at = s + (edge - p) / d;
            if (at > s1) break;
            addStep(height, at, now, next);
            s = at;
            p = edge >= 1.0f ? 0.0f : edge;
        }
        p += d * (s1 - s);
        if (p >= 1.0f) p -= std::floor(p);
    }

    // Exact integral, in samples, of the naive square over the part [s0, s1) of
    // the interval at increment d, starting from phase p (which it advances).
    static float squareIntegral(float& p, float d, float s0, float s1) noexcept
    {
        float acc = 0.0f, s = s0;
        for (int guard = 0; guard < 16 && d > 0.0f; ++guard)
        {
            const float level = p < 0.5f ? 1.0f : -1.0f;
            const float edge  = p < 0.5f ? 0.5f : 1.0f;
            const float at = s + (edge - p) / d;
            if (at >= s1) break;
            acc += level * (at - s);
            s = at;
            p = edge >= 1.0f ? 0.0f : edge;
        }
        acc += (p < 0.5f ? 1.0f : -1.0f) * (s1 - s);
        p += d * (s1 - s);
        if (p >= 1.0f) p -= std::floor(p);
        return acc;
    }

    // Synced triangle: the leaky integrator of the band-limited square, as in
    // processSample(), but read BEFORE this sample's update so that a reset in
    // the interval after it can still be corrected on both sides (the free
    // triangle reads after the update, one sample later).
    //
    // A reset clears the integrator. In that interval the triangle is integrated
    // exactly, piece by piece: up to the reset, to size the step down to 0 (a
    // polyBLEP) and the corner (a polyBLAMP: the slope turns from the square's
    // level just before the reset to rising), then from 0 to the end of the
    // interval with the square restarted high. The square's own edge halves still
    // owed are dropped there: the integrator they would correct is the one the
    // reset throws away.
    float processTriangleSynced(float d, float syncFrac) noexcept
    {
        float out = triState + syncCarry;
        syncCarry = 0.0f;
        const bool afterFree = !prevSynced;
        prevSynced = true;
        float p = phase;
        if (syncFrac <= 0.0f)
        {
            float sq = (p < 0.5f ? 1.0f : -1.0f) + syncSqCarry;
            if (afterFree) sq += edgeHalvesAfter(Waveform::Square, p, d);
            syncSqCarry = 0.0f;
            walkEdges(Waveform::Square, p, d, 0.0f, 1.0f, sq, syncSqCarry);
            triState = triLeak * triState + d * sq * 4.0f;
        }
        else
        {
            syncSqCarry = 0.0f;
            const float atReset = triState + 4.0f * d * squareIntegral(p, d, 0.0f, syncFrac);
            const float levelBefore = p < 0.5f ? 1.0f : -1.0f;
            addStep(-atReset, syncFrac, out, syncCarry);
            addRamp(4.0f * d * (1.0f - levelBefore), syncFrac, out, syncCarry);
            p = 0.0f;
            triState = 4.0f * d * squareIntegral(p, d, syncFrac, 1.0f);
        }
        lastCrossed = syncFrac > 0.0f || p < phase;
        phase = p;
        modValue = out; modLazy = false;
        return out;
    }

    // ~7 Hz DC-block corner for the triangle leaky integrator, independent of sr.
    static constexpr float kTriLeakHz = 7.0f;
    void updateTriLeak() noexcept { triLeak = std::exp(-kTwoPi * kTriLeakHz / sr); }

    float sr = 44100.0f;
    float freq = 440.0f;
    float dt = 0.01f;
    float phase = 0.0f;
    float pulseWidth = 0.5f;
    float detuneCents = 0.0f;   // input of detuneRatio: pow(2, 0 / 1200) is exactly 1
    float detuneRatio = 1.0f;
    float triState = 0.0f;
    float triLeak = 0.999f;
    bool  lastCrossed = false;
    float lastWrapFrac = 1.0f;   // see wrapFraction()
    float syncCarry = 0.0f;      // polyBLEP/BLAMP halves a synced render owes the next sample
    float syncSqCarry = 0.0f;    // the polyBLEP halves the synced triangle's square owes
    bool  prevSynced = false;    // the last sample went through processSampleSynced()
    bool  useMinBlep = true;     // set from the rate (see MINIMUM-PHASE BLEP)
    float modValue = 0.0f;       // see modulationValue()
    bool  modLazy = false;       // modulationValue() works it out from modPhase, modDt
    float modPhase = 0.0f, modDt = 0.0f;
    float triStatePoly = 0.0f;   // the triangle's polyBLEP integrator, for modValue
    float res[kResSize] = {};    // BLEP residuals still owed to the output
    float sqRes[kResSize] = {};  // ... and to the triangle's square
    unsigned resPos = 0;
    Waveform waveform = Waveform::Saw;
    Xorshift rng;
};

// Sub-oscillator: one octave below the note.
class SubOscillator
{
public:
    void prepare(double sampleRate) noexcept { osc.prepare(sampleRate); }
    void setSampleRate(double sampleRate) noexcept { osc.setSampleRate(sampleRate); }
    void setFrequency(float freqHz) noexcept { osc.setFrequency(freqHz * 0.5f); }
    void setWaveform(Waveform w) noexcept    { osc.setWaveform(w); }
    void setDetune(float cents) noexcept     { osc.setDetune(cents); }
    float processSample() noexcept           { return osc.processSample(); }
    void resetPhase() noexcept               { osc.resetPhase(); }
    void seedNoise(uint32_t s) noexcept      { osc.seedNoise(s); }

private:
    Oscillator osc;
};

inline float ringModulate(float a, float b) noexcept { return a * b; }

// Sample & Hold: latches its input each time the phase accumulator wraps.
class SampleAndHold
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; phase = 0.0f; }
    // Recompute dt from the stored rate so a sample-rate change (oversampling
    // switch) keeps the same S&H rate in Hz.
    void setSampleRate(double sampleRate) noexcept { sr = (float)sampleRate; dt = rate / sr; }
    void setRate(float rateHz) noexcept      { rate = rateHz; dt = rate / sr; }

    float process(float noiseInput) noexcept
    {
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            heldValue = noiseInput;
        }
        return heldValue;
    }

    void reset() noexcept { phase = 0.0f; heldValue = 0.0f; }

private:
    float sr = 44100.0f;
    float rate = 1.0f;
    float dt = 0.01f;
    float phase = 0.0f;
    float heldValue = 0.0f;
};

// Pink noise — Paul Kellet's refined 7-coefficient method.
class PinkNoiseGenerator
{
public:
    void seed(uint32_t s) noexcept { rng.seed(s); }

    // Cosmos takes the white source before coloration. Advance the same PRNG
    // and pink-filter state in either case so neighbouring modes keep their
    // noise stream and mode changes cannot expose stale filter memory.
    float processSample(bool whiteOutput = false) noexcept
    {
        const float white = rng.nextBipolar();
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return whiteOutput ? white : pink * 0.11f;
    }

    void reset() noexcept { b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f; }

private:
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    Xorshift rng;
};

} // namespace msynth
