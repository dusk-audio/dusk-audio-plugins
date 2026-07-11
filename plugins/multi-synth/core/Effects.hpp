// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Effects.hpp — post-synth effects chain: Drive -> Chorus -> Delay -> Reverb,
// plus a dispersive spring reverb for the Modular mode.
//
// Framework-free port of the JUCE EffectsEngine.h. Drive, the generic BBD
// chorus, the dual-line vintage BBD chorus (fixed 0.513/0.863 Hz triangle LFOs,
// inverted-phase stereo, ~3 ms BBD lowpass) and the tempo-synced stereo delay
// are carried over verbatim. Two things are NOT ports (mandatory fixes #8):
//
//   * Freeverb    — a from-scratch 8-comb / 4-allpass-per-channel reverb with
//                   the classic public-domain tunings and juce::Reverb's exact
//                   parameter mapping, replacing the juce::Reverb dependency.
//   * SpringReverb — the real dispersive spring tank from the tape-echo core
//                   (long first-order-allpass chain in a modulated feedback
//                   loop), replacing the JUCE "spring" that was just a dark
//                   Freeverb preset.

#pragma once

#include "SynthCommon.hpp"
#include "DuskFilters.hpp"   // duskaudio::OnePoleLP/HP, DCBlocker

#include <algorithm>
#include <array>
#include <vector>

namespace msynth
{

//==============================================================================
// Drive / saturation
enum class DriveType { SoftClip = 0, HardClip, Tube };

class DriveEffect
{
public:
    void prepare(double sampleRate, int) noexcept { sr = (float)sampleRate; }

    void setEnabled(bool on) noexcept { enabled = on; }
    void setDrive(float amount) noexcept { drive = clampf(amount, 0.0f, 1.0f); }
    void setMix(float m) noexcept { mix = clampf(m, 0.0f, 1.0f); }
    void setType(DriveType t) noexcept { type = t; }

    void process(float& left, float& right) noexcept
    {
        if (!enabled || drive < 0.001f) return;
        const float dryL = left, dryR = right;
        const float gain = 1.0f + drive * 10.0f;
        left  = saturate(left  * gain);
        right = saturate(right * gain);
        const float comp = 1.0f / (1.0f + drive * 2.0f);
        left  *= comp; right *= comp;
        left  = dryL * (1.0f - mix) + left  * mix;
        right = dryR * (1.0f - mix) + right * mix;
    }

private:
    float saturate(float x) noexcept
    {
        switch (type)
        {
            case DriveType::SoftClip: return std::tanh(x);
            case DriveType::HardClip: return clampf(x, -1.0f, 1.0f);
            case DriveType::Tube:
                return x >= 0.0f ? 1.0f - std::exp(-x) : -(1.0f - std::exp(x)) * 0.8f;
        }
        return x;
    }

    float sr = 44100.0f;
    bool  enabled = false;
    float drive = 0.0f, mix = 1.0f;
    DriveType type = DriveType::SoftClip;
};

//==============================================================================
// Generic BBD chorus/ensemble
class ChorusEffect
{
public:
    void prepare(double sampleRate, int) noexcept
    {
        sr = (float)sampleRate;
        const int maxDelaySamples = (int)(sr * 0.03f) + 1;
        bufL.assign((size_t)maxDelaySamples, 0.0f);
        bufR.assign((size_t)maxDelaySamples, 0.0f);
        bufSize = maxDelaySamples;
        writePos = 0; lfoPhase = 0.0f;
    }

    void setEnabled(bool on) noexcept { enabled = on; }
    void setRate(float r) noexcept { rate = clampf(r, 0.1f, 10.0f); }
    void setDepth(float d) noexcept { depth = clampf(d, 0.0f, 1.0f); }
    void setMix(float m) noexcept { mix = clampf(m, 0.0f, 1.0f); }
    void setStereo(bool s) noexcept { stereo = s; }

    void process(float& left, float& right) noexcept
    {
        if (!enabled) return;
        const float dryL = left, dryR = right;
        const float lfoL = std::sin(lfoPhase * kTwoPi);
        const float lfoR = stereo ? std::sin((lfoPhase + 0.5f) * kTwoPi) : lfoL;
        const float baseDelay = sr * 0.007f;
        const float modRange  = sr * 0.003f * depth;
        const float delayL = baseDelay + lfoL * modRange;
        const float delayR = baseDelay + lfoR * modRange;

        bufL[(size_t)writePos] = left;
        bufR[(size_t)writePos] = right;
        const float wetL = readDelay(bufL, delayL);
        const float wetR = readDelay(bufR, delayR);

        writePos = (writePos + 1) % bufSize;
        lfoPhase += rate / sr;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        left  = dryL * (1.0f - mix) + wetL * mix;
        right = dryR * (1.0f - mix) + wetR * mix;
    }

    void reset() noexcept
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        writePos = 0; lfoPhase = 0.0f;
    }

private:
    float readDelay(const std::vector<float>& buf, float delaySamples) const noexcept
    {
        float rp = (float)writePos - delaySamples;
        if (rp < 0.0f) rp += (float)bufSize;
        const int i0 = (int)rp;
        const int i1 = (i0 + 1) % bufSize;
        const float f = rp - (float)i0;
        return buf[(size_t)i0] * (1.0f - f) + buf[(size_t)i1] * f;
    }

    float sr = 44100.0f;
    bool  enabled = false, stereo = true;
    float rate = 0.8f, depth = 0.5f, mix = 0.5f;
    std::vector<float> bufL, bufR;
    int bufSize = 1, writePos = 0;
    float lfoPhase = 0.0f;
};

//==============================================================================
// Vintage dual-BBD chorus (Cosmos mode). Two lines, fixed triangle LFOs,
// inverted-phase stereo, ~10 kHz BBD rolloff. Verbatim tunings.
enum class CosmosChorusMode { Off = 0, I, II, Both };

class CosmosChorusEffect
{
public:
    void prepare(double sampleRate, int) noexcept
    {
        sr = (float)sampleRate;
        const int maxDelay = (int)(sr * 0.015f) + 1;
        for (int c = 0; c < 2; ++c)
        {
            bufL[c].assign((size_t)maxDelay, 0.0f);
            bufR[c].assign((size_t)maxDelay, 0.0f);
            bufSize[c] = maxDelay; writePos[c] = 0; lfoPhase[c] = 0.0f;
        }
        lpCoeff = std::exp(-kTwoPi * 10000.0f / sr);
        lpStateL = lpStateR = 0.0f;
    }

    void setMode(CosmosChorusMode m) noexcept { mode = m; }

    void process(float& left, float& right) noexcept
    {
        if (mode == CosmosChorusMode::Off) return;
        const float dryL = left, dryR = right;
        float wetL = 0.0f, wetR = 0.0f;
        int numActive = 0;

        for (int c = 0; c < 2; ++c)
        {
            bool active = false;
            if (c == 0 && (mode == CosmosChorusMode::I  || mode == CosmosChorusMode::Both)) active = true;
            if (c == 1 && (mode == CosmosChorusMode::II || mode == CosmosChorusMode::Both)) active = true;
            if (!active) continue;
            ++numActive;

            const float lfo = 2.0f * std::abs(2.0f * (lfoPhase[c] - std::floor(lfoPhase[c] + 0.5f))) - 1.0f;
            const float centerDelay = sr * 0.003f;
            const float modDepth = sr * 0.002f;
            const float delay = centerDelay + lfo * modDepth;

            const float mono = (left + right) * 0.5f;
            bufL[c][(size_t)writePos[c]] = mono;
            bufR[c][(size_t)writePos[c]] = mono;

            float wet = readBuf(bufL[c], bufSize[c], writePos[c], delay);
            wet = wet * (1.0f - lpCoeff) + (c == 0 ? lpStateL : lpStateR) * lpCoeff;
            if (c == 0) lpStateL = wet; else lpStateR = wet;

            wetL += wet;
            wetR -= wet; // inverted for stereo width

            writePos[c] = (writePos[c] + 1) % bufSize[c];
            const float rate = (c == 0) ? 0.513f : 0.863f;
            lfoPhase[c] += rate / sr;
            if (lfoPhase[c] >= 1.0f) lfoPhase[c] -= 1.0f;
        }

        if (numActive > 0)
        {
            const float wetGain = (numActive == 2) ? 0.35f : 0.5f;
            left  = dryL * 0.7f + wetL * wetGain;
            right = dryR * 0.7f + wetR * wetGain;
        }
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill(bufL[c].begin(), bufL[c].end(), 0.0f);
            std::fill(bufR[c].begin(), bufR[c].end(), 0.0f);
            writePos[c] = 0; lfoPhase[c] = 0.0f;
        }
        lpStateL = lpStateR = 0.0f;
    }

private:
    float readBuf(const std::vector<float>& buf, int sz, int wp, float delay) const noexcept
    {
        float rp = (float)wp - delay;
        if (rp < 0.0f) rp += (float)sz;
        const int i0 = (int)rp;
        const int i1 = (i0 + 1) % sz;
        const float f = rp - (float)i0;
        return buf[(size_t)i0] * (1.0f - f) + buf[(size_t)i1] * f;
    }

    float sr = 44100.0f;
    CosmosChorusMode mode = CosmosChorusMode::Off;
    std::vector<float> bufL[2], bufR[2];
    int bufSize[2] = { 1, 1 }, writePos[2] = { 0, 0 };
    float lfoPhase[2] = { 0.0f, 0.0f };
    float lpCoeff = 0.0f, lpStateL = 0.0f, lpStateR = 0.0f;
};

//==============================================================================
// Tempo-synced stereo delay with ping-pong, feedback filtering, tape character.
class DelayEffect
{
public:
    void prepare(double sampleRate, int) noexcept
    {
        sr = (float)sampleRate;
        const int maxSamples = (int)(sr * 2.0f) + 1;
        bufL.assign((size_t)maxSamples, 0.0f);
        bufR.assign((size_t)maxSamples, 0.0f);
        bufSize = maxSamples; writePos = 0;
        fbLPF_L = fbLPF_R = fbHPF_L = fbHPF_R = 0.0f;
    }

    void setEnabled(bool on) noexcept { enabled = on; }
    void setTempoSync(bool s) noexcept { tempoSynced = s; }
    void setTimeMs(float ms) noexcept { delayTimeMs = clampf(ms, 1.0f, 2000.0f); }
    void setSyncDivision(ArpRateDivision d) noexcept { syncDivision = d; }
    void setFeedback(float fb) noexcept { feedback = clampf(fb, 0.0f, 0.95f); }
    void setMix(float m) noexcept { mix = clampf(m, 0.0f, 1.0f); }
    void setPingPong(bool pp) noexcept { pingPong = pp; }
    void setTapeCharacter(bool on) noexcept { tapeChar = on; }

    void process(float& left, float& right, double bpm) noexcept
    {
        if (!enabled) return;
        const float dryL = left, dryR = right;

        float delaySamples;
        if (tempoSynced && bpm > 0.0)
        {
            const double beatsPerStep = getBeatsPerStep(syncDivision);
            const double samplesPerBeat = (double)sr * 60.0 / bpm;
            delaySamples = (float)(samplesPerBeat * beatsPerStep);
        }
        else
        {
            delaySamples = delayTimeMs * sr / 1000.0f;
        }
        delaySamples = clampf(delaySamples, 1.0f, (float)(bufSize - 1));

        const float wetL = readBuf(bufL, delaySamples);
        const float wetR = readBuf(bufR, delaySamples);

        float fbL = applyFeedbackFilter(wetL, true);
        float fbR = applyFeedbackFilter(wetR, false);
        if (tapeChar) { fbL = std::tanh(fbL * 1.1f); fbR = std::tanh(fbR * 1.1f); }

        auto softClamp = [](float x) noexcept {
            if (std::abs(x) > 1.5f) return std::tanh(x * 0.67f) * 1.5f;
            return x;
        };
        if (pingPong)
        {
            bufL[(size_t)writePos] = softClamp(left  + fbR * feedback);
            bufR[(size_t)writePos] = softClamp(right + fbL * feedback);
        }
        else
        {
            bufL[(size_t)writePos] = softClamp(left  + fbL * feedback);
            bufR[(size_t)writePos] = softClamp(right + fbR * feedback);
        }
        writePos = (writePos + 1) % bufSize;

        left  = dryL * (1.0f - mix) + wetL * mix;
        right = dryR * (1.0f - mix) + wetR * mix;
    }

    void reset() noexcept
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        writePos = 0;
        fbLPF_L = fbLPF_R = fbHPF_L = fbHPF_R = 0.0f;
    }

private:
    float readBuf(const std::vector<float>& buf, float delay) const noexcept
    {
        float rp = (float)writePos - delay;
        if (rp < 0.0f) rp += (float)bufSize;
        const int i0 = (int)rp;
        const int i1 = (i0 + 1) % bufSize;
        const float f = rp - (float)i0;
        return buf[(size_t)i0] * (1.0f - f) + buf[(size_t)i1] * f;
    }

    float applyFeedbackFilter(float sample, bool isLeft) noexcept
    {
        float& lpState = isLeft ? fbLPF_L : fbLPF_R;
        float& hpState = isLeft ? fbHPF_L : fbHPF_R;
        const float lpCoeff = std::exp(-kTwoPi * fbLPFFreq / sr);
        lpState = sample * (1.0f - lpCoeff) + lpState * lpCoeff;
        float out = lpState;
        const float hpCoeff = std::exp(-kTwoPi * fbHPFFreq / sr);
        const float prev = hpState;
        hpState = out;
        out = out - prev * hpCoeff;
        return out;
    }

    float sr = 44100.0f;
    bool  enabled = false, tempoSynced = true, pingPong = false, tapeChar = false;
    float delayTimeMs = 500.0f;
    ArpRateDivision syncDivision = ArpRateDivision::Quarter;
    float feedback = 0.3f, mix = 0.3f, fbLPFFreq = 8000.0f, fbHPFFreq = 80.0f;
    std::vector<float> bufL, bufR;
    int bufSize = 1, writePos = 0;
    float fbLPF_L = 0.0f, fbLPF_R = 0.0f, fbHPF_L = 0.0f, fbHPF_R = 0.0f;
};

//==============================================================================
// Freeverb — classic 8-comb + 4-allpass per channel. Public-domain algorithm
// (Jezar's Freeverb); parameter mapping matches juce::Reverb bit-for-bit so the
// decay->roomSize mapping in ReverbEffect keeps producing the intended tails.
class Freeverb
{
public:
    void setSampleRate(double sampleRate)
    {
        // Comb/allpass lengths scale with the sample rate (juce::Reverb form).
        const double scale = sampleRate / 44100.0;
        static constexpr int kCombL[kNumCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int kAllpL[kNumAllpasses] = { 556, 441, 341, 225 };
        for (int i = 0; i < kNumCombs; ++i)
        {
            combL[i].resize((int)std::round(kCombL[i] * scale));
            combR[i].resize((int)std::round((kCombL[i] + kStereoSpread) * scale));
        }
        for (int i = 0; i < kNumAllpasses; ++i)
        {
            allpassL[i].resize((int)std::round(kAllpL[i] * scale));
            allpassR[i].resize((int)std::round((kAllpL[i] + kStereoSpread) * scale));
            allpassL[i].feedback = 0.5f;
            allpassR[i].feedback = 0.5f;
        }
        reset();
    }

    struct Parameters { float roomSize = 0.5f, damping = 0.5f, wetLevel = 0.33f, dryLevel = 0.4f, width = 1.0f; };

    void setParameters(const Parameters& p)
    {
        const float wetScaled = p.wetLevel * kWetScale;
        wet1 = wetScaled * (p.width * 0.5f + 0.5f);
        wet2 = wetScaled * ((1.0f - p.width) * 0.5f);
        dry  = p.dryLevel * kDryScale;
        const float feedback = p.roomSize * kRoomScale + kRoomOffset;
        const float damp = p.damping * kDampScale;
        for (int i = 0; i < kNumCombs; ++i)
        {
            combL[i].feedback = feedback; combL[i].setDamp(damp);
            combR[i].feedback = feedback; combR[i].setDamp(damp);
        }
    }

    void reset()
    {
        for (int i = 0; i < kNumCombs; ++i) { combL[i].clear(); combR[i].clear(); }
        for (int i = 0; i < kNumAllpasses; ++i) { allpassL[i].clear(); allpassR[i].clear(); }
    }

    // In-place stereo processing (dry mixed per juce::Reverb; callers that do
    // their own wet/dry set dryLevel=0).
    void processStereo(float* left, float* right, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = left[n], inR = right[n];
            const float input = (inL + inR) * kGain;
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < kNumCombs; ++i) { outL += combL[i].process(input); outR += combR[i].process(input); }
            for (int i = 0; i < kNumAllpasses; ++i) { outL = allpassL[i].process(outL); outR = allpassR[i].process(outR); }
            left[n]  = outL * wet1 + outR * wet2 + inL * dry;
            right[n] = outR * wet1 + outL * wet2 + inR * dry;
        }
    }

private:
    static constexpr int kNumCombs = 8, kNumAllpasses = 4, kStereoSpread = 23;
    static constexpr float kGain = 0.015f, kWetScale = 3.0f, kDryScale = 2.0f;
    static constexpr float kDampScale = 0.4f, kRoomScale = 0.28f, kRoomOffset = 0.7f;

    struct CombFilter
    {
        void resize(int n) { buf.assign((size_t)maxf(1.0f, (float)n), 0.0f); index = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); filterStore = 0.0f; }
        void setDamp(float d) { damp1 = d; damp2 = 1.0f - d; }
        float process(float input) noexcept
        {
            const float output = buf[(size_t)index];
            filterStore = output * damp2 + filterStore * damp1;
            if (isBad(filterStore)) filterStore = 0.0f;
            buf[(size_t)index] = input + filterStore * feedback;
            if (++index >= (int)buf.size()) index = 0;
            return output;
        }
        std::vector<float> buf;
        int index = 0;
        float feedback = 0.5f, filterStore = 0.0f, damp1 = 0.0f, damp2 = 1.0f;
    };

    struct AllpassFilter
    {
        void resize(int n) { buf.assign((size_t)maxf(1.0f, (float)n), 0.0f); index = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
        float process(float input) noexcept
        {
            const float bufout = buf[(size_t)index];
            const float output = -input + bufout;
            buf[(size_t)index] = input + bufout * feedback;
            if (++index >= (int)buf.size()) index = 0;
            return output;
        }
        std::vector<float> buf;
        int index = 0;
        float feedback = 0.5f;
    };

    CombFilter combL[kNumCombs], combR[kNumCombs];
    AllpassFilter allpassL[kNumAllpasses], allpassR[kNumAllpasses];
    float wet1 = 0.0f, wet2 = 0.0f, dry = 0.0f;
};

//==============================================================================
// Reverb effect: pre-delay + Freeverb, with its own wet/dry mix.
class ReverbEffect
{
public:
    void prepare(double sampleRate, int)
    {
        sr = (float)sampleRate;
        reverb.setSampleRate(sampleRate);
        const int maxPreDelay = (int)(sr * 0.2f) + 1;
        preL.assign((size_t)maxPreDelay, 0.0f);
        preR.assign((size_t)maxPreDelay, 0.0f);
        preSize = maxPreDelay; prePos = 0;
        updateParams();
    }

    void setEnabled(bool on) noexcept { enabled = on; }
    void setSize(float s) { roomSize = clampf(s, 0.0f, 1.0f); updateParams(); }
    void setDecay(float d) { decay = clampf(d, 0.1f, 20.0f); updateParams(); }
    void setDamping(float d) { damping = clampf(d, 0.0f, 1.0f); updateParams(); }
    void setMix(float m) { mix = clampf(m, 0.0f, 1.0f); }
    void setPreDelay(float ms) { preDelayMs = clampf(ms, 0.0f, 200.0f); }

    void process(float& left, float& right) noexcept
    {
        if (!enabled) return;
        const float dryL = left, dryR = right;

        if (preDelayMs > 0.1f)
        {
            int pd = (int)(preDelayMs * sr / 1000.0f);
            pd = clampi(pd, 0, preSize - 1);
            preL[(size_t)prePos] = left;
            preR[(size_t)prePos] = right;
            const int readPos = (prePos - pd + preSize) % preSize;
            left  = preL[(size_t)readPos];
            right = preR[(size_t)readPos];
            prePos = (prePos + 1) % preSize;
        }

        reverb.processStereo(&left, &right, 1);
        left  = dryL * (1.0f - mix) + left  * mix;
        right = dryR * (1.0f - mix) + right * mix;
    }

    void reset()
    {
        reverb.reset();
        std::fill(preL.begin(), preL.end(), 0.0f);
        std::fill(preR.begin(), preR.end(), 0.0f);
        prePos = 0;
    }

private:
    void updateParams()
    {
        Freeverb::Parameters p;
        p.roomSize = clampf(roomSize * 0.5f + decay / 40.0f, 0.0f, 1.0f);
        p.damping = damping;
        p.wetLevel = 1.0f;  // we handle wet/dry ourselves
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        reverb.setParameters(p);
    }

    float sr = 44100.0f;
    bool  enabled = false;
    float roomSize = 0.5f, decay = 2.0f, damping = 0.3f, mix = 0.2f, preDelayMs = 0.0f;
    Freeverb reverb;
    std::vector<float> preL, preR;
    int preSize = 1, prePos = 0;
};

//==============================================================================
// Dispersive spring reverb (Modular mode). Ported from the tape-echo core:
// each spring is a feedback delay loop with a long chain of first-order
// allpasses (falling group delay -> the downward "boing" chirp) plus in-loop
// damping and slow delay modulation.
class DispersiveSpring
{
public:
    void prepare(double sampleRate, float detune)
    {
        springs[0].prepare(sampleRate, 0.0412f * detune, 0.855f, 0.31f, 0.62f);
        springs[1].prepare(sampleRate, 0.0331f * detune, 0.835f, 0.47f, 0.66f);
        inputHP.setCutoff(140.0f, sampleRate);
        inputLP.setCutoff(4200.0f, sampleRate);
        dcBlock.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& s : springs) s.reset();
        inputHP.reset(); inputLP.reset(); dcBlock.reset();
    }

    float process(float in) noexcept
    {
        const float voiced = inputLP.process(inputHP.process(in));
        const float wet = springs[0].process(voiced) + springs[1].process(voiced);
        return dcBlock.process(0.6f * wet);
    }

private:
    struct Allpass1
    {
        float a = 0.63f, z = 0.0f;
        float process(float x) noexcept { const float y = -a * x + z; z = x + a * y; return y; }
    };

    static constexpr int kNumAllpasses = 36;

    struct Spring
    {
        std::vector<float> buf;
        int len = 0, writeIdx = 0;
        float feedback = 0.0f, lfoPhase = 0.0f, lfoInc = 0.0f, lfoDepth = 0.0f;
        std::array<Allpass1, kNumAllpasses> chain;
        duskaudio::OnePoleLP damping;

        void prepare(double fs, float lengthSeconds, float fbAmount, float lfoHz, float apCoeff)
        {
            len = (int)maxf(16.0f, std::round((float)(lengthSeconds * fs)));
            buf.assign((size_t)len + 8, 0.0f);
            writeIdx = 0; feedback = fbAmount; lfoPhase = 0.0f;
            lfoInc = kTwoPi * lfoHz / (float)fs;
            lfoDepth = std::min(1.5f + 0.00005f * (float)fs, 6.0f);
            for (auto& ap : chain) ap.a = apCoeff;
            damping.setCutoff(2800.0f, fs);
            reset();
        }
        void reset()
        {
            std::fill(buf.begin(), buf.end(), 0.0f);
            for (auto& ap : chain) ap.z = 0.0f;
            damping.reset(); writeIdx = 0;
        }
        float process(float x) noexcept
        {
            lfoPhase += lfoInc;
            if (lfoPhase > kTwoPi) lfoPhase -= kTwoPi;
            const float delay = (float)len - 4.0f + lfoDepth * std::sin(lfoPhase);
            const float sz = (float)buf.size();
            float rp = (float)writeIdx - delay;
            rp -= std::floor(rp / sz) * sz;
            if (rp >= sz) rp -= sz;
            const int i0 = (int)rp;
            const float frac = rp - (float)i0;
            const int i1 = (i0 + 1 < (int)buf.size()) ? i0 + 1 : 0;
            float y = buf[(size_t)i0] + frac * (buf[(size_t)i1] - buf[(size_t)i0]);
            for (auto& ap : chain) y = ap.process(y);
            y = damping.process(y);
            buf[(size_t)writeIdx] = x + feedback * y;
            if (++writeIdx >= (int)buf.size()) writeIdx = 0;
            return y;
        }
    };

    std::array<Spring, 2> springs;
    duskaudio::OnePoleHP inputHP;
    duskaudio::OnePoleLP inputLP;
    duskaudio::DCBlocker dcBlock;
};

// Stereo wrapper used by the Modular mode (auto-enabled at low mix).
class SpringReverbFX
{
public:
    void prepare(double sampleRate, int)
    {
        springL.prepare(sampleRate, 1.0f);
        springR.prepare(sampleRate, 1.013f);
    }
    void setEnabled(bool on) noexcept { enabled = on; }
    void setMix(float m) noexcept { mix = clampf(m, 0.0f, 1.0f); }
    void process(float& left, float& right) noexcept
    {
        if (!enabled) return;
        const float wl = springL.process(left);
        const float wr = springR.process(right);
        left  = left  * (1.0f - mix) + wl * mix;
        right = right * (1.0f - mix) + wr * mix;
    }
    void reset() { springL.reset(); springR.reset(); }
private:
    bool enabled = false;
    float mix = 0.15f;
    DispersiveSpring springL, springR;
};

//==============================================================================
// Complete chain: Drive -> Chorus -> Delay -> Reverb -> SpringReverb.
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

    void process(float& left, float& right, double bpm) noexcept
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
    SpringReverbFX springReverb;
};

} // namespace msynth
