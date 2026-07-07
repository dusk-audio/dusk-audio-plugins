// TapeEchoDSP.cpp — vintage three-head tape echo emulation core (framework-free C++17).

#include "TapeEchoDSP.hpp"
#include "DuskDenormals.hpp"   // ScopedFlushDenormals (SSE + ARM64 FPCR)

#include <algorithm>

namespace duskaudio
{

namespace
{
    // 4-point, 3rd-order Hermite. Interpolates between x0 and x1 at `frac`.
    inline float hermite(float frac, float xm1, float x0, float x1, float x2) noexcept
    {
        const float c = 0.5f * (x1 - xm1);
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + 0.5f * (x2 - x0);
        const float b = w + a;
        return (((a * frac - b) * frac + c) * frac) + x0;
    }

    inline int nextPowerOfTwo(int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    // Mode matrix: which read heads and whether the spring tank is active.
    struct ModeConfig { float h1, h2, h3, reverb; };

    constexpr ModeConfig kModeTable[TapeEchoDSP::kNumModes] =
    {
        { 1, 0, 0, 0 },   // 1:  Head 1
        { 0, 1, 0, 0 },   // 2:  Head 2
        { 0, 0, 1, 0 },   // 3:  Head 3
        { 0, 1, 1, 0 },   // 4:  Heads 2 + 3
        { 1, 0, 0, 1 },   // 5:  Head 1 + Reverb
        { 0, 1, 0, 1 },   // 6:  Head 2 + Reverb
        { 0, 0, 1, 1 },   // 7:  Head 3 + Reverb
        { 1, 1, 0, 1 },   // 8:  Heads 1 + 2 + Reverb
        { 0, 1, 1, 1 },   // 9:  Heads 2 + 3 + Reverb
        { 1, 0, 1, 1 },   // 10: Heads 1 + 3 + Reverb
        { 1, 1, 1, 1 },   // 11: Heads 1 + 2 + 3 + Reverb
        { 0, 0, 0, 1 },   // 12: Reverb only
    };

    // Per-head playback trims: later heads sit slightly lower and duller on
    // the real unit (longer tape wear path, playback amp voicing).
    constexpr float kHeadTrim[3] = { 1.0f, 0.95f, 0.90f };

    // Wow & flutter depths (fraction of nominal delay time) at full depth.
    // Tuned so the 0.5 default lands near real tape-transport figures
    // (~0.15-0.25% peak pitch deviation); full depth is a musical extreme.
    constexpr float kWowHz       = 0.5f;
    constexpr float kFlutterHz   = 4.1f;
    constexpr float kWowDepth    = 0.0055f;
    constexpr float kFlutterDepth= 0.0012f;
    constexpr float kNoiseDepth  = 0.0028f;

    constexpr float kTwoPi = 6.28318530717958647692f;
}

//==============================================================================
// Oversampled preamp — halfband taps now shared (plugins/shared-dpf/dsp/
// DuskOversampler.hpp, duskaudio::hbtaps::kA/kB; scipy remez, identical values).
//==============================================================================
float TapeEchoDSP::preampOversampled(Channel& ch, float x, float drive) noexcept
{
    // 4x oversampled saturation: upsample (2 cascaded 2x halfbands), shape,
    // decimate through the same filters. Zero-stuffed interpolation needs
    // the x2 gain on each upsampling stage.
    for (int p = 0; p < 2; ++p)
    {
        ch.upA.push(p == 0 ? x : 0.0f);
        const float a = 2.0f * ch.upA.out(hbtaps::kA);
        for (int q = 0; q < 2; ++q)
        {
            ch.upB.push(q == 0 ? a : 0.0f);
            const float b = 2.0f * ch.upB.out(hbtaps::kB);
            ch.downB.push(preampShape(b * drive));
        }
        ch.downA.push(ch.downB.out(hbtaps::kB));
    }
    return ch.downA.out(hbtaps::kA);
}

//==============================================================================
// (ShelfFilter coefficient design moved to shared Biquad::shelfSlope1 — see
//  plugins/shared-dpf/dsp/DuskFilters.hpp. Bit-identical float op-order.)
//==============================================================================

//==============================================================================
// SpringReverb
//==============================================================================
void SpringReverb::Spring::prepare(double fs, float lengthSeconds, float fbAmount,
                                   float lfoHz, float apCoeff)
{
    len = std::max(16, (int)std::lround(lengthSeconds * fs));
    buf.assign((size_t)len + 8, 0.0f);
    writeIdx = 0;
    feedback = fbAmount;
    lfoPhase = 0.0f;
    lfoInc   = kTwoPi * lfoHz / (float)fs;
    // A few samples, rate-scaled — capped so the modulation excursion stays well
    // inside the +8 buffer guard band at any sample rate (6 < 8).
    lfoDepth = std::min(1.5f + 0.00005f * (float)fs, 6.0f);
    for (auto& ap : chain)
        ap.a = apCoeff;
    damping.setCutoff(2800.0f, fs);
    reset();
}

void SpringReverb::Spring::reset()
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    for (auto& ap : chain)
        ap.z = 0.0f;
    damping.reset();
    writeIdx = 0;
}

float SpringReverb::Spring::process(float x) noexcept
{
    // Modulated read position (linear interpolation is plenty: the
    // modulation is a fraction of a millisecond, moving at < 1 Hz).
    lfoPhase += lfoInc;
    if (lfoPhase > kTwoPi)
        lfoPhase -= kTwoPi;

    const float delay = (float)len - 4.0f + lfoDepth * std::sin(lfoPhase);
    const float sz = (float)buf.size();
    // Robust wrap into [0, sz): a single "+= sz" leaves rp negative when delay
    // exceeds sz (possible at high fs if the guard margin is ever outgrown).
    float rp = (float)writeIdx - delay;
    rp -= std::floor(rp / sz) * sz;
    if (rp >= sz) rp -= sz; // fp edge guard

    const int   i0   = (int)rp;
    const float frac = rp - (float)i0;
    const int   i1   = (i0 + 1 < (int)buf.size()) ? i0 + 1 : 0;
    float y = buf[(size_t)i0] + frac * (buf[(size_t)i1] - buf[(size_t)i0]);

    // Dispersive allpass chain: the "boing".
    for (auto& ap : chain)
        y = ap.process(y);

    y = damping.process(y);

    buf[(size_t)writeIdx] = x + feedback * y;
    if (++writeIdx >= (int)buf.size())
        writeIdx = 0;

    return y;
}

void SpringReverb::prepare(double sampleRate, float detune)
{
    // Two unequal springs; slight per-channel detune decorrelates L/R.
    // Feedback sets decay: ~1.4 dB loss per ~40 ms round trip gives the
    // real three-spring tank's ~1.8 s RT60 (HF decays faster via damping).
    springs[0].prepare(sampleRate, 0.0412f * detune, 0.855f, 0.31f, 0.62f);
    springs[1].prepare(sampleRate, 0.0331f * detune, 0.835f, 0.47f, 0.66f);
    inputHP.setCutoff(140.0f, sampleRate);
    inputLP.setCutoff(4200.0f, sampleRate);
    reset();
}

void SpringReverb::reset()
{
    for (auto& s : springs)
        s.reset();
    inputHP.reset();
    inputLP.reset();
    dcBlock.reset();
}

float SpringReverb::process(float in) noexcept
{
    const float voiced = inputLP.process(inputHP.process(in));
    const float wet    = springs[0].process(voiced) + springs[1].process(voiced);
    return dcBlock.process(0.6f * wet);
}

//==============================================================================
// TapeEchoDSP
//==============================================================================
constexpr float TapeEchoDSP::kHeadRatio[3];

void TapeEchoDSP::prepare(double sampleRate, int /*maxBlockSize*/)
{
    fs = sampleRate;

    // Longest possible read: head 3 at the slowest motor speed, plus wow
    // headroom, plus interpolation guard.
    const float maxDelaySec = (kMaxDelayMs * 0.001f) * kHeadRatio[2] * 1.05f;
    const int   needed      = (int)std::ceil(maxDelaySec * fs) + 8;
    const int   tapeLen     = nextPowerOfTwo(needed);
    mask            = tapeLen - 1;
    maxDelaySamples = (float)(tapeLen - 8);
    writeIdx        = 0;

    float detune = 1.0f;
    for (auto& ch : channels)
    {
        ch.tape.assign((size_t)tapeLen, 0.0f);
        ch.recordHP.setCutoff(55.0f, fs);
        ch.recordLP.setCutoff(6200.0f, fs);
        ch.recordLP2.setCutoff(4800.0f, fs);
        ch.spring.prepare(fs, detune);
        detune = 1.013f; // second channel slightly longer springs
    }

    noiseLP.setCutoff(2.0f, fs);
    wowInc     = kTwoPi * kWowHz     / (float)fs;
    flutterInc = kTwoPi * kFlutterHz / (float)fs;
    meterDecayPerSample = std::exp(-1.0f / (0.3f * (float)fs));
    outputPeak.store(0.0f, std::memory_order_relaxed);

    delaySmoother.prepare(fs, 0.35f);        // motor/capstan inertia
    intensitySmoother.prepare(fs, 0.03f);
    for (auto& g : headGain)
        g.prepare(fs, 0.015f);
    reverbSendSmoother.prepare(fs, 0.015f);
    echoLevelSmoother.prepare(fs, 0.02f);
    reverbLevelSmoother.prepare(fs, 0.02f);
    dryLevelSmoother.prepare(fs, 0.02f);
    driveSmoother.prepare(fs, 0.02f);
    wowFlutterSmoother.prepare(fs, 0.05f);
    powerSmoother.prepare(fs, 0.03f);
    ageSmoother.prepare(fs, 0.10f);
    hissVoice.setCutoff(4500.0f, fs);
    hissVoiceR.setCutoff(4500.0f, fs);
    wobbleLP.setCutoff(5.0f, fs);

    // Snap smoothers to current parameter values so prepare() never glides.
    const auto& m = kModeTable[pMode.load(std::memory_order_relaxed) - 1];
    const float t = kMinDelayMs + (1.0f - pRepeatRate.load(std::memory_order_relaxed))
                                  * (kMaxDelayMs - kMinDelayMs);
    delaySmoother.snap(t * 0.001f * (float)fs);
    intensitySmoother.snap(pIntensity.load(std::memory_order_relaxed));
    headGain[0].snap(m.h1);
    headGain[1].snap(m.h2);
    headGain[2].snap(m.h3);
    reverbSendSmoother.snap(m.reverb);
    echoLevelSmoother.snap(pEchoLevel.load(std::memory_order_relaxed));
    reverbLevelSmoother.snap(pReverbLevel.load(std::memory_order_relaxed));
    dryLevelSmoother.snap(pDryLevel.load(std::memory_order_relaxed));
    driveSmoother.snap(pInputGain.load(std::memory_order_relaxed));
    wowFlutterSmoother.snap(pWowFlutter.load(std::memory_order_relaxed));
    powerSmoother.snap(1.0f - pBypass.load(std::memory_order_relaxed));
    ageSmoother.snap(pTapeAge.load(std::memory_order_relaxed));
    lastAge = -1.0f; // force LP2 retune on first block

    lastBass = lastTreble = -999.0f; // force shelf recompute
    reset();
}

void TapeEchoDSP::reset()
{
    for (auto& ch : channels)
    {
        std::fill(ch.tape.begin(), ch.tape.end(), 0.0f);
        ch.preampDC.reset();
        ch.upA.reset(); ch.downA.reset();
        ch.upB.reset(); ch.downB.reset();
        ch.recordHP.reset();
        ch.recordLP.reset();
        ch.recordLP2.reset();
        ch.bassShelf.reset();
        ch.trebleShelf.reset();
        ch.spring.reset();
    }
    noiseLP.reset();
    hissVoice.reset();
    hissVoiceR.reset();
    wobbleLP.reset();
    writeIdx = 0;
    wowPhase = flutterPhase = 0.0f;
}

float TapeEchoDSP::readTape(const std::vector<float>& buf, float delaySamples) const noexcept
{
    const float rp   = (float)writeIdx - delaySamples;
    int         i0   = (int)std::floor(rp);
    const float frac = rp - (float)i0;

    i0 &= mask; // power-of-two wrap (two's complement handles negative i0)
    const int im1 = (i0 - 1) & mask;
    const int i1  = (i0 + 1) & mask;
    const int i2  = (i0 + 2) & mask;

    return hermite(frac, buf[(size_t)im1], buf[(size_t)i0],
                         buf[(size_t)i1],  buf[(size_t)i2]);
}

void TapeEchoDSP::refreshBlockRateControls()
{
    // Snapshot atomics once per block; smoothers handle the rest per sample.
    const auto& m = kModeTable[pMode.load(std::memory_order_relaxed) - 1];
    headGain[0].setTarget(m.h1);
    headGain[1].setTarget(m.h2);
    headGain[2].setTarget(m.h3);
    reverbSendSmoother.setTarget(m.reverb);

    const float rate = pRepeatRate.load(std::memory_order_relaxed);
    const float tMs  = kMinDelayMs + (1.0f - rate) * (kMaxDelayMs - kMinDelayMs);
    delaySmoother.setTarget(tMs * 0.001f * (float)fs);

    intensitySmoother.setTarget(pIntensity.load(std::memory_order_relaxed));
    echoLevelSmoother.setTarget(pEchoLevel.load(std::memory_order_relaxed));
    reverbLevelSmoother.setTarget(pReverbLevel.load(std::memory_order_relaxed));
    dryLevelSmoother.setTarget(pDryLevel.load(std::memory_order_relaxed));
    driveSmoother.setTarget(pInputGain.load(std::memory_order_relaxed));
    wowFlutterSmoother.setTarget(pWowFlutter.load(std::memory_order_relaxed));
    powerSmoother.setTarget(1.0f - pBypass.load(std::memory_order_relaxed));
    ageSmoother.setTarget(pTapeAge.load(std::memory_order_relaxed));

    // worn heads/tape lose top end: retune the second in-loop pole at block
    // rate. age 0 recomputes the original 4.8 kHz coefficient exactly, so a
    // fresh transport stays bit-identical.
    {
        const float age = ageSmoother.value();
        if (age != lastAge)
        {
            lastAge = age;
            const float fc2 = 4800.0f * (1.0f - 0.58f * age);
            for (int c = 0; c < kMaxChannels; ++c)
                channels[(size_t)c].recordLP2.setCutoff(fc2, fs);
        }
    }

    // Echo-path tone shelves: recompute only when the knobs actually moved.
    const float bass   = pBass.load(std::memory_order_relaxed);
    const float treble = pTreble.load(std::memory_order_relaxed);
    if (bass != lastBass || treble != lastTreble)
    {
        lastBass   = bass;
        lastTreble = treble;
        // Configure ALL channels (like the age-retune block above), not just the
        // current numChannels: a later mono→stereo switch without a knob move
        // would otherwise leave channels[1]'s shelves at their passthrough
        // default while channels[0] is shelved — an L/R mismatch.
        for (int c = 0; c < kMaxChannels; ++c)
        {
            channels[(size_t)c].bassShelf.setCoeffs(
                Biquad::shelfSlope1(fs, 100.0f, bass * 12.0f, /*high=*/false));
            channels[(size_t)c].trebleShelf.setCoeffs(
                Biquad::shelfSlope1(fs, 3000.0f, treble * 12.0f, /*high=*/true));
        }
    }
}

void TapeEchoDSP::processBlock(const float* const* inputs, float* const* outputs,
                                int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0 || mask == 0)
        return;

    ScopedFlushDenormals ftz;

    numChannels = clampInt(numChannels, 1, kMaxChannels);
    refreshBlockRateControls();

    float blockPeak = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        //--- shared per-sample control signals (one motor, one tape) ----------
        wowPhase += wowInc;
        if (wowPhase > kTwoPi)     wowPhase     -= kTwoPi;
        flutterPhase += flutterInc;
        if (flutterPhase > kTwoPi) flutterPhase -= kTwoPi;

        // ~7% of full modulation depth is always present: real tape transports
        // have residual wow even in perfect condition. Knob adds on top; a
        // worn transport (Tape Age) adds more still.
        const float age = ageSmoother.next();
        const float wf  = 0.07f + 0.93f * wowFlutterSmoother.next() + 0.45f * age;
        // slow playback-level wobble (worn pinch roller / dropout precursor);
        // exactly 1.0 at age 0.
        const float wobble = 1.0f + age * 0.11f * wobbleLP.process(ageRand());
        // hiss recorded onto the tape: regenerates with intensity like the
        // hardware. Voiced dark, exactly 0.0 at age 0.
        const float hissL = age * 0.012f * hissVoice.process(ageRand());
        const float hissR = age * 0.012f * hissVoiceR.process(ageRand());
        const float mod = wf * (kWowDepth     * std::sin(wowPhase)
                              + kFlutterDepth * std::sin(flutterPhase)
                              + kNoiseDepth   * noiseLP.process(frand()));

        // The oversampled preamp delays the tape feed by a fixed group delay;
        // subtract it AFTER the head-ratio scaling so all three heads stay at
        // their exact mechanical times (subtracting from T would scale the
        // compensation by 1.9x/2.75x on the far heads).
        const float t1 = delaySmoother.next() * (1.0f + mod);
        const float d1 = clampF(t1                 - kPreampLatencySamples, 4.0f, maxDelaySamples);
        const float d2 = clampF(t1 * kHeadRatio[1] - kPreampLatencySamples, 4.0f, maxDelaySamples);
        const float d3 = clampF(t1 * kHeadRatio[2] - kPreampLatencySamples, 4.0f, maxDelaySamples);

        const float g1 = headGain[0].next() * kHeadTrim[0];
        const float g2 = headGain[1].next() * kHeadTrim[1];
        const float g3 = headGain[2].next() * kHeadTrim[2];

        // Intensity mapped past unity loop gain: > ~0.75 the loop exceeds
        // unity for small signals and the in-loop tape saturation clamps it
        // into stable, warm self-oscillation.
        const float fbGain    = intensitySmoother.next() * 1.30f;
        const float revSend   = reverbSendSmoother.next();
        // Level knobs use an audio (squared) taper, and the echo bus is
        // trimmed so full Echo Volume lands near dry-signal loudness even
        // with all three heads saturated (the raw head sum peaks ~3x).
        const float echoRaw   = echoLevelSmoother.next();
        const float echoLvl   = echoRaw * echoRaw * 0.55f;
        const float revRaw    = reverbLevelSmoother.next();
        const float revLvl    = revRaw * revRaw * revSend;
        const float dryLvl    = dryLevelSmoother.next();
        const float driveKnob = driveSmoother.next();
        const float drive     = 0.4f + 2.6f * driveKnob * driveKnob; // audio taper: clean low, saturated high
        const float driveComp = 1.0f / softClip(drive);
        const float power     = powerSmoother.next(); // 0 = bypassed

        //--- per-channel audio path -------------------------------------------
        for (int c = 0; c < numChannels; ++c)
        {
            Channel& ch = channels[(size_t)c];
            const float in = inputs[c][n];

            // FET preamp front-end, saturated at 4x to keep fold-back
            // products out of the echo passband.
            const float pre = ch.preampDC.process(preampOversampled(ch, in, drive)) * driveComp;

            // Three playback heads off the shared tape.
            const float h1 = readTape(ch.tape, d1);
            const float h2 = readTape(ch.tape, d2);
            const float h3 = readTape(ch.tape, d3);
            const float headSum = (h1 * g1 + h2 * g2 + h3 * g3) * wobble;

            // Record chain: program + feedback -> head EQ -> magnetic
            // saturation -> tape. Everything here is inside the loop, so
            // repeats darken and compress cumulatively.
            const float hiss = (c == 0) ? hissL : hissR;
            const float toTape = ch.recordHP.process(
                                     ch.recordLP2.process(
                                         ch.recordLP.process(pre + hiss + fbGain * softClip(headSum))));
            ch.tape[(size_t)writeIdx] = softClip(toTape);

            // Echo output path only: bass/treble shelves (dry and reverb
            // are unaffected, matching the hardware layout).
            const float echoWet = ch.trebleShelf.process(ch.bassShelf.process(headSum));

            // Spring tank is fed from the preamp, as in the original.
            const float rev = ch.spring.process(pre * revSend);

            const float wet = dryLvl * in + echoLvl * echoWet + revLvl * rev;
            // POWER off: clean passthrough; tape and springs keep running so
            // re-engaging brings tails back, like pulling the output jack.
            outputs[c][n] = in + power * (wet - in);

            const float mag = power * (wet < 0.0f ? -wet : wet);
            blockPeak = mag > blockPeak ? mag : blockPeak;
        }

        writeIdx = (writeIdx + 1) & mask;
    }

    const float decayed = outputPeak.load(std::memory_order_relaxed)
                          * std::pow(meterDecayPerSample, (float)numSamples);
    outputPeak.store(blockPeak > decayed ? blockPeak : decayed,
                     std::memory_order_relaxed);
}

} // namespace duskaudio
