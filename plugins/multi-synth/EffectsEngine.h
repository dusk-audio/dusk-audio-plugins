#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// Post-synth effects chain: Drive -> Chorus -> Delay -> Reverb
namespace MultiSynthDSP
{

//==============================================================================
// Drive/Saturation
enum class DriveType
{
    SoftClip,
    HardClip,
    Tube
};

class DriveEffect
{
public:
    void prepare(double sampleRate, int /*blockSize*/)
    {
        sr = static_cast<float>(sampleRate);
    }

    void setEnabled(bool on) { enabled = on; }
    void setDrive(float amount) { drive = juce::jlimit(0.0f, 1.0f, amount); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setType(DriveType t) { type = t; }

    void process(float& left, float& right)
    {
        if (!enabled || drive < 0.001f) return;

        float dryL = left, dryR = right;
        float gain = 1.0f + drive * 10.0f;

        left = saturate(left * gain);
        right = saturate(right * gain);

        // Compensate output level
        float comp = 1.0f / (1.0f + drive * 2.0f);
        left *= comp;
        right *= comp;

        // Parallel mix
        left = dryL * (1.0f - mix) + left * mix;
        right = dryR * (1.0f - mix) + right * mix;
    }

private:
    float saturate(float x)
    {
        switch (type)
        {
            case DriveType::SoftClip:
                return std::tanh(x);

            case DriveType::HardClip:
                return juce::jlimit(-1.0f, 1.0f, x);

            case DriveType::Tube:
            {
                // Asymmetric tube-style saturation (more even harmonics)
                if (x >= 0.0f)
                    return 1.0f - std::exp(-x);
                else
                    return -(1.0f - std::exp(x)) * 0.8f;
            }
        }
        return x;
    }

    float sr = 44100.0f;
    bool enabled = false;
    float drive = 0.0f;
    float mix = 1.0f;
    DriveType type = DriveType::SoftClip;
};

//==============================================================================
// BBD-style Chorus/Ensemble
class ChorusEffect
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sr = static_cast<float>(sampleRate);
        // Delay buffer: up to 30ms
        int maxDelaySamples = static_cast<int>(sr * 0.03f) + 1;
        delayBufferL.resize(static_cast<size_t>(maxDelaySamples), 0.0f);
        delayBufferR.resize(static_cast<size_t>(maxDelaySamples), 0.0f);
        bufferSize = maxDelaySamples;
        writePos = 0;
        lfoPhase = 0.0f;
        juce::ignoreUnused(blockSize);
    }

    void setEnabled(bool on) { enabled = on; }
    void setRate(float rateHz) { rate = juce::jlimit(0.1f, 10.0f, rateHz); }
    void setDepth(float d) { depth = juce::jlimit(0.0f, 1.0f, d); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setStereo(bool s) { stereo = s; }

    void process(float& left, float& right)
    {
        if (!enabled) return;

        float dryL = left, dryR = right;

        // BBD-style: LFO modulates delay time
        float lfoL = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
        float lfoR = stereo
            ? std::sin((lfoPhase + 0.5f) * juce::MathConstants<float>::twoPi)
            : lfoL;

        // Base delay ~7ms, modulated by depth (up to ~3ms variation)
        float baseDelay = sr * 0.007f;
        float modRange = sr * 0.003f * depth;

        float delayL = baseDelay + lfoL * modRange;
        float delayR = baseDelay + lfoR * modRange;

        // Write to buffer
        delayBufferL[static_cast<size_t>(writePos)] = left;
        delayBufferR[static_cast<size_t>(writePos)] = right;

        // Read with interpolation
        float wetL = readDelay(delayBufferL, delayL);
        float wetR = readDelay(delayBufferR, delayR);

        // Advance write position and LFO
        writePos = (writePos + 1) % bufferSize;
        lfoPhase += rate / sr;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        left = dryL * (1.0f - mix) + wetL * mix;
        right = dryR * (1.0f - mix) + wetR * mix;
    }

    void reset()
    {
        std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
        std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
        writePos = 0;
        lfoPhase = 0.0f;
    }

private:
    float readDelay(const std::vector<float>& buffer, float delaySamples) const
    {
        float readPos = static_cast<float>(writePos) - delaySamples;
        if (readPos < 0.0f) readPos += static_cast<float>(bufferSize);

        int idx0 = static_cast<int>(readPos);
        int idx1 = (idx0 + 1) % bufferSize;
        float frac = readPos - static_cast<float>(idx0);

        return buffer[static_cast<size_t>(idx0)] * (1.0f - frac)
             + buffer[static_cast<size_t>(idx1)] * frac;
    }

    float sr = 44100.0f;
    bool enabled = false;
    bool stereo = true;
    float rate = 0.8f;
    float depth = 0.5f;
    float mix = 0.5f;

    std::vector<float> delayBufferL, delayBufferR;
    int bufferSize = 1;
    int writePos = 0;
    float lfoPhase = 0.0f;
};

//==============================================================================
// Juno-60 Chorus: Two independent BBD delay lines with triangle LFOs
// Chorus I (0.513 Hz), Chorus II (0.863 Hz), or Both simultaneously
enum class JunoChorusMode { Off, I, II, Both };

class JunoChorusEffect
{
public:
    void prepare(double sampleRate, int /*blockSize*/)
    {
        sr = static_cast<float>(sampleRate);
        int maxDelay = static_cast<int>(sr * 0.015f) + 1; // 15ms max
        for (int c = 0; c < 2; ++c)
        {
            bufL[c].resize(static_cast<size_t>(maxDelay), 0.0f);
            bufR[c].resize(static_cast<size_t>(maxDelay), 0.0f);
            bufSize[c] = maxDelay;
            writePos[c] = 0;
            lfoPhase[c] = 0.0f;
        }
        // BBD lowpass: simple one-pole at ~10kHz
        lpCoeff = std::exp(-juce::MathConstants<float>::twoPi * 10000.0f / sr);
        lpStateL = lpStateR = 0.0f;
    }

    void setMode(JunoChorusMode m) { mode = m; }

    void process(float& left, float& right)
    {
        if (mode == JunoChorusMode::Off) return;

        float dryL = left, dryR = right;
        float wetL = 0.0f, wetR = 0.0f;
        int numActive = 0;

        // Process each chorus line that's active
        for (int c = 0; c < 2; ++c)
        {
            bool active = false;
            if (c == 0 && (mode == JunoChorusMode::I || mode == JunoChorusMode::Both)) active = true;
            if (c == 1 && (mode == JunoChorusMode::II || mode == JunoChorusMode::Both)) active = true;
            if (!active) continue;
            numActive++;

            // Triangle LFO (authentic Juno shape)
            float lfo = 2.0f * std::abs(2.0f * (lfoPhase[c] - std::floor(lfoPhase[c] + 0.5f))) - 1.0f;

            // Juno delay range: ~1ms to ~5ms, centered around 3ms
            float centerDelay = sr * 0.003f;
            float modDepth = sr * 0.002f;
            float delay = centerDelay + lfo * modDepth;

            // Write mono input to both channels
            float mono = (left + right) * 0.5f;
            bufL[c][static_cast<size_t>(writePos[c])] = mono;
            bufR[c][static_cast<size_t>(writePos[c])] = mono;

            // Read with interpolation
            float wet = readBuf(bufL[c], bufSize[c], writePos[c], delay);

            // BBD lowpass (chips naturally roll off highs)
            wet = wet * (1.0f - lpCoeff) + (c == 0 ? lpStateL : lpStateR) * lpCoeff;
            if (c == 0) lpStateL = wet; else lpStateR = wet;

            // Juno stereo: L = dry + wet, R = dry - wet (inverted phase creates width)
            wetL += wet;
            wetR -= wet; // Inverted for stereo spread

            writePos[c] = (writePos[c] + 1) % bufSize[c];

            // Advance LFO (Chorus I = 0.513 Hz, Chorus II = 0.863 Hz)
            float rate = (c == 0) ? 0.513f : 0.863f;
            lfoPhase[c] += rate / sr;
            if (lfoPhase[c] >= 1.0f) lfoPhase[c] -= 1.0f;
        }

        if (numActive > 0)
        {
            // Juno-style blend: ~50/50 dry/wet, not additive boost
            // Total output should be unity gain (dry*0.5 + wet*0.5)
            float wetGain = (numActive == 2) ? 0.35f : 0.5f;
            left  = dryL * 0.7f + wetL * wetGain;
            right = dryR * 0.7f + wetR * wetGain;
        }
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill(bufL[c].begin(), bufL[c].end(), 0.0f);
            std::fill(bufR[c].begin(), bufR[c].end(), 0.0f);
            writePos[c] = 0;
            lfoPhase[c] = 0.0f;
        }
        lpStateL = lpStateR = 0.0f;
    }

private:
    float readBuf(const std::vector<float>& buf, int sz, int wp, float delay) const
    {
        float rp = static_cast<float>(wp) - delay;
        if (rp < 0.0f) rp += static_cast<float>(sz);
        int i0 = static_cast<int>(rp);
        int i1 = (i0 + 1) % sz;
        float f = rp - static_cast<float>(i0);
        return buf[static_cast<size_t>(i0)] * (1.0f - f) + buf[static_cast<size_t>(i1)] * f;
    }

    float sr = 44100.0f;
    JunoChorusMode mode = JunoChorusMode::Off;

    // Two independent BBD delay lines
    std::vector<float> bufL[2], bufR[2];
    int bufSize[2] = { 1, 1 };
    int writePos[2] = { 0, 0 };
    float lfoPhase[2] = { 0.0f, 0.0f };

    // BBD lowpass
    float lpCoeff = 0.0f;
    float lpStateL = 0.0f, lpStateR = 0.0f;
};

//==============================================================================
// Stereo Delay with ping-pong, feedback filtering, and tape character
class DelayEffect
{
public:
    void prepare(double sampleRate, int /*blockSize*/)
    {
        sr = static_cast<float>(sampleRate);
        // Max 2 seconds delay
        int maxSamples = static_cast<int>(sr * 2.0f) + 1;
        bufferL.resize(static_cast<size_t>(maxSamples), 0.0f);
        bufferR.resize(static_cast<size_t>(maxSamples), 0.0f);
        bufSize = maxSamples;
        writePos = 0;

        // Feedback filters
        fbLPF_L.reset();
        fbLPF_R.reset();
        fbHPF_L.reset();
        fbHPF_R.reset();
    }

    void setEnabled(bool on) { enabled = on; }
    void setTempoSync(bool sync) { tempoSynced = sync; }
    void setTimeMs(float ms) { delayTimeMs = juce::jlimit(1.0f, 2000.0f, ms); }
    void setSyncDivision(ArpRateDivision div) { syncDivision = div; }
    void setFeedback(float fb) { feedback = juce::jlimit(0.0f, 0.95f, fb); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setPingPong(bool pp) { pingPong = pp; }
    void setFeedbackLPF(float freq) { fbLPFFreq = freq; }
    void setFeedbackHPF(float freq) { fbHPFFreq = freq; }
    void setTapeCharacter(bool on) { tapeChar = on; }

    void process(float& left, float& right, double bpm)
    {
        if (!enabled) return;

        float dryL = left, dryR = right;

        // Calculate delay in samples
        float delaySamples;
        if (tempoSynced && bpm > 0.0)
        {
            double beatsPerStep = getBeatsPerStep(syncDivision);
            double samplesPerBeat = static_cast<double>(sr) * 60.0 / bpm;
            delaySamples = static_cast<float>(samplesPerBeat * beatsPerStep);
        }
        else
        {
            delaySamples = delayTimeMs * sr / 1000.0f;
        }

        delaySamples = juce::jlimit(1.0f, static_cast<float>(bufSize - 1), delaySamples);

        // Read from buffer
        float wetL = readBuf(bufferL, delaySamples);
        float wetR = readBuf(bufferR, delaySamples);

        // Apply feedback filtering
        float fbL = applyFeedbackFilter(wetL, true);
        float fbR = applyFeedbackFilter(wetR, false);

        // Tape character: subtle saturation + wow
        if (tapeChar)
        {
            fbL = std::tanh(fbL * 1.1f);
            fbR = std::tanh(fbR * 1.1f);
        }

        // Write to buffer — soft clamp feedback to prevent runaway
        auto softClamp = [](float x) {
            if (std::abs(x) > 1.5f) return std::tanh(x * 0.67f) * 1.5f;
            return x;
        };
        if (pingPong)
        {
            bufferL[static_cast<size_t>(writePos)] = softClamp(left + fbR * feedback);
            bufferR[static_cast<size_t>(writePos)] = softClamp(right + fbL * feedback);
        }
        else
        {
            bufferL[static_cast<size_t>(writePos)] = softClamp(left + fbL * feedback);
            bufferR[static_cast<size_t>(writePos)] = softClamp(right + fbR * feedback);
        }

        writePos = (writePos + 1) % bufSize;

        left = dryL * (1.0f - mix) + wetL * mix;
        right = dryR * (1.0f - mix) + wetR * mix;
    }

    void reset()
    {
        std::fill(bufferL.begin(), bufferL.end(), 0.0f);
        std::fill(bufferR.begin(), bufferR.end(), 0.0f);
        writePos = 0;
    }

private:
    float readBuf(const std::vector<float>& buf, float delay) const
    {
        float rp = static_cast<float>(writePos) - delay;
        if (rp < 0.0f) rp += static_cast<float>(bufSize);
        int i0 = static_cast<int>(rp);
        int i1 = (i0 + 1) % bufSize;
        float f = rp - static_cast<float>(i0);
        return buf[static_cast<size_t>(i0)] * (1.0f - f) + buf[static_cast<size_t>(i1)] * f;
    }

    float applyFeedbackFilter(float sample, bool isLeft)
    {
        // Simple one-pole LPF and HPF for feedback loop
        float& lpState = isLeft ? fbLPF_L.state : fbLPF_R.state;
        float& hpState = isLeft ? fbHPF_L.state : fbHPF_R.state;

        // LPF
        float lpCoeff = std::exp(-juce::MathConstants<float>::twoPi * fbLPFFreq / sr);
        lpState = sample * (1.0f - lpCoeff) + lpState * lpCoeff;
        float out = lpState;

        // HPF
        float hpCoeff = std::exp(-juce::MathConstants<float>::twoPi * fbHPFFreq / sr);
        float prev = hpState;
        hpState = out;
        out = out - prev * hpCoeff;

        return out;
    }

    struct OnePole { float state = 0.0f; void reset() { state = 0.0f; } };

    float sr = 44100.0f;
    bool enabled = false;
    bool tempoSynced = true;
    bool pingPong = false;
    bool tapeChar = false;
    float delayTimeMs = 500.0f;
    ArpRateDivision syncDivision = ArpRateDivision::Quarter;
    float feedback = 0.3f;
    float mix = 0.3f;
    float fbLPFFreq = 8000.0f;
    float fbHPFFreq = 80.0f;

    std::vector<float> bufferL, bufferR;
    int bufSize = 1;
    int writePos = 0;
    OnePole fbLPF_L, fbLPF_R, fbHPF_L, fbHPF_R;
};

//==============================================================================
// Reverb effect using JUCE's built-in Reverb (correct gain staging)
class ReverbEffect
{
public:
    void prepare(double sampleRate, int /*blockSize*/)
    {
        sr = static_cast<float>(sampleRate);
        reverb.setSampleRate(sampleRate);
        reverb.reset();
    }

    void setEnabled(bool on) { enabled = on; }
    void setSize(float s) { roomSize = juce::jlimit(0.0f, 1.0f, s); updateParams(); }
    void setDecay(float d) { decay = juce::jlimit(0.1f, 20.0f, d); updateParams(); }
    void setDamping(float d) { damping = juce::jlimit(0.0f, 1.0f, d); updateParams(); }
    void setMix(float m) { mix = juce::jlimit(0.0f, 1.0f, m); updateParams(); }
    void setPreDelay(float /*ms*/) { /* pre-delay not supported by juce::Reverb */ }

    void process(float& left, float& right)
    {
        if (!enabled) return;

        float dryL = left, dryR = right;

        reverb.processStereo(&left, &right, 1);

        // Wet/dry mix
        left = dryL * (1.0f - mix) + left * mix;
        right = dryR * (1.0f - mix) + right * mix;
    }

    void reset() { reverb.reset(); }

private:
    void updateParams()
    {
        juce::Reverb::Parameters p;
        // Map decay (0.1-20s) to room size (0-1)
        p.roomSize = juce::jlimit(0.0f, 1.0f, roomSize * 0.5f + decay / 40.0f);
        p.damping = damping;
        p.wetLevel = 1.0f;  // We handle wet/dry mix ourselves
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        p.freezeMode = 0.0f;
        reverb.setParameters(p);
    }

    float sr = 44100.0f;
    bool enabled = false;
    float roomSize = 0.5f;
    float decay = 2.0f;
    float damping = 0.3f;
    float mix = 0.2f;

    juce::Reverb reverb;
};

//==============================================================================
// Spring reverb emulation (for Modular mode) — uses JUCE reverb with spring character
class SpringReverb
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        reverb.prepare(sampleRate, blockSize);
        reverb.setEnabled(true);
        reverb.setSize(0.3f);
        reverb.setDecay(1.5f);
        reverb.setDamping(0.6f);
    }

    void setEnabled(bool on) { enabled = on; }
    void setMix(float m) { reverb.setMix(m); }

    void process(float& left, float& right)
    {
        if (!enabled) return;
        reverb.process(left, right);
    }

    void reset() { reverb.reset(); }

private:
    bool enabled = false;
    ReverbEffect reverb;
};

//==============================================================================
// Complete effects chain: Drive -> Chorus -> Delay -> Reverb
class EffectsChain
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        drive.prepare(sampleRate, blockSize);
        chorus.prepare(sampleRate, blockSize);
        delay.prepare(sampleRate, blockSize);
        reverb.prepare(sampleRate, blockSize);
        springReverb.prepare(sampleRate, blockSize);
    }

    void process(float& left, float& right, double bpm)
    {
        drive.process(left, right);
        chorus.process(left, right);
        delay.process(left, right, bpm);
        reverb.process(left, right);
        springReverb.process(left, right);
    }

    void reset()
    {
        chorus.reset();
        delay.reset();
        reverb.reset();
        springReverb.reset();
    }

    DriveEffect drive;
    ChorusEffect chorus;
    DelayEffect delay;
    ReverbEffect reverb;
    SpringReverb springReverb;
};

} // namespace MultiSynthDSP
