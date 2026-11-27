// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#include "SectionReverbEngine.h"
#include "TwoPhaseLowpass.hpp"
#include <algorithm>
#include <cmath>

namespace duskverb::sectionreverb
{
namespace
{
float onePoleCoeff(float hz, const Rate& rate)
{
    return hz >= rate.nyquist ? 1.0f : tankPole2x(hz, rate.sr);
}
constexpr float curves[4][9] = {{.702f, .717f, .458f, .107f, .061f, .061f, .061f, .061f, .061f},
                                {0, .140f, .518f, .671f, .610f, .610f, .443f, .229f, .183f},
                                {0, .089f, .177f, .336f, .534f, .549f, .626f, .748f, .754f},
                                {.275f, 0, 0, 0, 0, 0, 0, 0, 0}};
// The first three delay lines have output taps. The remaining five form two nested
// allpasses. The final variable output tap is in the second main line.
constexpr unsigned delays[2][9] = {{142, 33, 3, 47, 172, 43, 53, 61, 10},
                                   {118, 32, 2, 74, 28, 127, 85, 28, 10}};
void setRamp(Ramp &lane, float value, float ms, const Rate &sr, bool snap)
{
    if (snap)
        lane.reset(value);
    else if (lane.target() != value)
        lane.setTarget(value, ms, sr);
}
} // namespace
void auditoriumShapeGains(float shape, float out[4])
{
    const unsigned step = unsigned(std::clamp(std::round(shape), 0.0f, 64.0f));
    const unsigned i = step >> 3, j = std::min(i + 1, 8u);
    const float frac = float(step & 7u) / 8.0f;
    for (unsigned k = 0; k < 4; ++k)
        out[k] = (1.0f - frac) * curves[k][i] + frac * curves[k][j];
}

void AuditoriumTail::Line::clear()
{
    write = 0;
    std::fill(data.begin(), data.end(), 0.0f);
}

float AuditoriumTail::Line::read(unsigned delay) const
{
    const unsigned i = write >= delay ? write - delay : unsigned(data.size()) + write - delay;
    return data[i];
}

void AuditoriumTail::prepare(const Rate& rate)
{
    sr = rate;
    const unsigned chorusLength = (unsigned(256.0f * sr.srScale) + 31u) & ~31u;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int k = 0; k < 8; ++k)
        {
            const unsigned length = unsigned(float(delays[ch][k]) * 20.0f * sr.lenScale + 0.5f) + 64;
            sections[ch].lines[k].data.resize((length + 31u) & ~31u);
        }
        for (auto& line : sections[ch].chorus)
            line.data.resize(chorusLength);
    }
    reset();
}

void AuditoriumTail::reset()
{
    const unsigned chorusLength = static_cast<unsigned>(sections[0].chorus[0].data.size());
    for (auto& section : sections)
    {
        section.low = section.high = 0;
        std::fill_n(section.feedback, 32, 0.0f);
        for (auto& line : section.lines)
            line.clear();
        for (auto& line : section.chorus)
            line.clear();
    }
    sizeState = hold = 0;
    sizePending = false;
    rng = 1;
    position = 0;
    descending = false;
    depthChanged = true;
    chorusType = pendingType;
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 2; ++k)
            randomPos[c][k] = randomTarget[c][k] = 0;
    maxShift = 0.5f * float(chorusLength) * std::min(depth * 0.01f, 0.99f);
    period = 1000;
    updateChorusRate();
    dynamics = previousDensity = peakRatio = 1;
    smoothIn = smoothOut = peakIn = peakOut = 0;
    attackIn = std::pow(.25f, 1.0f / sr.srScale);
    attackOut = std::pow(.15f, 1.0f / sr.srScale);
    ratioCoeff = std::pow(.05f, 1.0f / sr.srScale);
    release = std::pow(.001f, 1.0f / (.5f * sr.sr32));
    fade.reset(1);
    density.reset(1 - definition * .01f);
    highPole.reset(onePoleCoeff(cutoff, sr));
    highFeedthrough.reset(tankOutputFeedthrough(cutoff));
    bassPole.reset(onePoleCoeff(std::clamp(crossover, 20.0f, 10000.0f), sr));
    setGeometry(true);
    updateDecay(true);
}

void AuditoriumTail::setGeometry(bool immediate)
{
    const float scale = size * .25f;
    for (int c = 0; c < 2; ++c)
    {
        auto &s = sections[c];
        for (int k = 0; k < 9; ++k)
        {
            const float d = float(delays[c][k]) * sr.srScale * scale;
            if (k == 8)
            {
                const unsigned write = s.lines[1].write;
                s.newDelay[k] = unsigned(int(write) - int(float(write) - d));
            }
            else
            {
                s.newDelay[k] = unsigned(k < 3 ? d + 0.5f : d);
            }
            if (immediate)
                s.oldDelay[k] = s.newDelay[k];
        }
    }
}

void AuditoriumTail::updateDecay(bool immediate)
{
    const float g = SectionReverbEngine::rtGain(static_cast<int>(SectionReverbModel::Auditorium), rt);
    setRamp(gain, g, 40, sr, immediate);
    setRamp(bassDelta, std::pow(g, 1.0f / std::max(.01f, bass)) - g, 40, sr, immediate);
    fall = g > 0 ? -std::log10(g) * 20.0f / (40.0f * size * .25f * 600.0f / 32.0f) : 1;
}

void AuditoriumTail::updateChorusRate()
{
    nextPeriod = rate > 0 ? float(330.0 * sr.srMs * (11.0 - std::round(rate))) : 0;
    if (nextPeriod > 0)
        period = nextPeriod;
    speed = period > 0 ? maxShift * 32.0f / period : 0;
}

void AuditoriumTail::configure(const SectionReverbConfig& config, bool immediate)
{
    if (size != config.sizeMeters || immediate)
    {
        size = config.sizeMeters;
        if (immediate) setGeometry(true);
        else sizePending = true;
    }
    rt = config.reverbTime;
    bass = config.bassRt;
    updateDecay(immediate);
    cutoff = config.rtHiCutHz;
    crossover = config.bassXoverHz;
    definition = config.definition;
    setRamp(highPole, onePoleCoeff(cutoff, sr), 200, sr, immediate);
    setRamp(highFeedthrough, tankOutputFeedthrough(cutoff), 200, sr, immediate);
    setRamp(bassPole, onePoleCoeff(std::clamp(crossover, 20.0f, 10000.0f), sr), 200, sr, immediate);
    setRamp(density, 1.0f - definition * .01f, 200, sr, immediate);
    if (rate != config.chorusRate || immediate)
    {
        rate = config.chorusRate;
        updateChorusRate();
    }
    if (depth != config.chorusDepth || immediate)
    {
        depth = config.chorusDepth;
        maxShift = .5f * float(sections[0].chorus[0].data.size()) * std::min(.99f, depth * .01f);
        speed = period > 0 ? maxShift * 32.0f / period : 0;
        depthChanged = true;
    }
    if (pendingType != config.chorusType || immediate)
    {
        pendingType = config.chorusType;
        if (immediate) chorusType = pendingType;
        setGeometry(true);
    }
}

void AuditoriumTail::filterInput(const float spread[2][32], float filtered[2][32])
{
    const float a = bassPole.current(), h = highPole.current();
    for (int c = 0; c < 2; ++c)
    {
        auto &s = sections[c];
        const auto &feedback = sections[1 - c].feedback;
        for (int n = 0; n < 32; ++n)
        {
            const float direct = feedback[n] * gain.current() + spread[c][n];
            const auto phase = [&] {
                const float low = lowpassPhase(feedback[n], a, s.low);
                const float input = direct + low * bassDelta.current();
                const float high = lowpassPhase(input, h, s.high);
                return high + highFeedthrough.current() * (input - high);
            };
            const float first = phase();
            filtered[c][n] = 0.5f * (first + phase());
        }
    }
    bassPole.advanceBlock();
    highPole.advanceBlock();
    highFeedthrough.advanceBlock();
    gain.advanceBlock();
    bassDelta.advanceBlock();
}

void AuditoriumTail::stepSize()
{
    switch (sizeState)
    {
    case 0:
        if (sizePending)
        {
            sizePending = false;
            setGeometry(false);
            setRamp(fade, 0, 100, sr, false);
            sizeState = 1;
        }
        break;
    case 1:
        if (!fade.ramping())
            sizeState = 2;
        break;
    case 2:
        sizeState = 3;
        break;
    case 3:
        for (auto &s : sections)
            std::copy_n(s.newDelay, 9, s.oldDelay);
        setRamp(fade, 1, 100, sr, false);
        sizeState = 4;
        break;
    case 4:
        if (!fade.ramping())
            sizeState = 0;
        break;
    }
    fade.advanceBlock();
    density.advanceBlock();
}

void AuditoriumTail::updateDynamics(float in, float out)
{
    smoothIn = (1 - attackIn) * smoothIn + attackIn * in;
    smoothOut = (1 - attackOut) * smoothOut + attackOut * std::max(.00031622776f, out);
    peakIn = std::max(peakIn * release, smoothIn);
    peakOut = std::max(peakOut * release, smoothOut);
    const float ratio = peakIn / peakOut;
    peakRatio =
        std::clamp(std::max(ratio, (1 - ratioCoeff) * peakRatio + ratioCoeff * ratio), .25f, 10.0f);
    if (hold)
    {
        --hold;
        dynamics += float(1.0 / (.02 * sr.sr32));
    }
    else if (ratio < .5f * peakRatio)
        dynamics -= fall;
    else if (ratio < .75f * peakRatio)
        dynamics += float(1.0 / (.2 * sr.sr32));
    else
    {
        dynamics += float(1.0 / (.02 * sr.sr32));
        hold = int(.03 * sr.sr32);
    }
    dynamics = std::clamp(dynamics, 0.0f, 1.0f);
}

int AuditoriumTail::nextRandom()
{
    rng = rng * 214013u + 2531011u;
    return int((rng >> 16) & 32767u);
}

void AuditoriumTail::makeChorusIndices(float indices[2][2][32])
{
    const auto &line = sections[0].chorus[0];
    const float length = float(line.data.size());
    const float center = float((line.write + line.data.size() / 2) % line.data.size());
    const float limit = (pendingType == chorusType ? maxShift : 0.0f);
    const auto generate = [&](float *buf, float offset, float delta)
    {
        float pos = center + offset;
        if (pos < 0)
            pos += length;
        if (pos >= length)
            pos -= length;
        const float step = 1.0f + delta / 32.0f;
        for (int i = 0; i < 32; ++i)
        {
            buf[i] = pos;
            pos += step;
            if (pos >= length)
                pos -= length;
        }
    };
    bool centered = true;
    if (chorusType == 1)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 2; ++k)
            {
                float &p = randomPos[c][k];
                const float delta = std::clamp(randomTarget[c][k] - p, -speed, speed);
                generate(indices[c][k], p, delta);
                p += delta;
                if (delta == 0.0f)
                {
                    randomTarget[c][k] = float(nextRandom() - 16383) / 16383.0f * limit;
                }
                else
                    centered = false;
            }
    }
    else
    {
        if (depthChanged)
        {
            if (position > limit)
                descending = true;
            if (position < -limit)
                descending = false;
            depthChanged = false;
        }
        float delta;
        if (descending)
            delta = -(position > 0 && position <= speed ? position : std::min(position + limit, speed));
        else
            delta = position < 0 && -position <= speed ? -position : std::min(limit - position, speed);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 2; ++k)
            {
                const float sign = (chorusType == 2 || c == k) ? 1.0f : -1.0f;
                generate(indices[c][k], sign * position, sign * delta);
            }
        position += delta;
        if (descending && position <= -limit)
            descending = false;
        else if (!descending && position >= limit)
            descending = true;
        centered = position == 0;
    }
    if (centered)
    {
        chorusType = pendingType;
        period = nextPeriod;
        speed = period > 0 ? maxShift * 32.0f / period : 0.0f;
    }
}

void AuditoriumTail::processBlock(const float diffused[2][32], float output[2][32], float inputPeak,
                                   float previousWetPeak)
{
    stepSize();
    const float target = dynamics * density.current();
    const float step = (target - previousDensity) * .03125f;
    float lattice[32], value = previousDensity;
    for (float &v : lattice)
    {
        value += step;
        v = value;
    }
    previousDensity = target;
    float indices[2][2][32];
    makeChorusIndices(indices);
    std::fill_n(output[0], 32, 0.0f);
    std::fill_n(output[1], 32, 0.0f);
    const float oldWeight = fade.current(), newWeight = 1.0f - oldWeight;
    for (int c = 0; c < 2; ++c)
    {
        auto &s = sections[c];
        const auto read = [&](int line, int tap)
        {
            if (newWeight == 0.0f)
                return s.lines[line].read(s.oldDelay[tap]);
            return newWeight * s.lines[line].read(s.newDelay[tap]) +
                   oldWeight * s.lines[line].read(s.oldDelay[tap]);
        };
        for (int n = 0; n < 32; ++n)
        {
            const float half = lattice[n] * .5f, quarter = lattice[n] * .25f;
            s.lines[0].put(diffused[c][n]);
            float delayed = read(3, 3);
            float x = read(0, 0) + delayed * half;
            s.lines[1].put(delayed - x * half);
            delayed = read(4, 4);
            x += delayed * quarter;
            s.lines[3].put(delayed - x * quarter);
            const auto chorus = [&](int path, float input)
            {
                auto &line = s.chorus[path];
                line.put(input);
                const float pos = indices[c][path][n];
                const unsigned i = unsigned(pos);
                const float frac = pos - float(i);
                const unsigned next = i + 1 == line.data.size() ? 0 : i + 1;
                return (1 - frac) * line.data[i] + frac * line.data[next];
            };
            s.lines[4].put(chorus(0, x));

            delayed = read(5, 5);
            x = read(1, 1) + delayed * half;
            s.lines[2].put(delayed - x * half);
            delayed = read(6, 6);
            x += delayed * quarter;
            s.lines[5].put(delayed - x * quarter);
            delayed = read(7, 7);
            x += delayed * quarter;
            s.lines[6].put(delayed - x * quarter);
            s.lines[7].put(chorus(1, x));
            s.feedback[n] = read(2, 2);

            output[c][n] += s.lines[0].read(0) * .65625f;
            output[1 - c][n] += s.lines[1].read(0) * .5625f;
            output[c][n] += read(1, 8) * .46875f;
            output[c][n] -= s.lines[2].read(0) * .375f;
            for (auto &line : s.lines)
                line.step();
            for (auto &line : s.chorus)
                line.step();
        }
    }
    updateDynamics(inputPeak, previousWetPeak);
}
} // namespace duskverb::sectionreverb
