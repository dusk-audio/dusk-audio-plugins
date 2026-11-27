// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#pragma once

#include "SectionReverbRamp.hpp"
#include <vector>

namespace duskverb { struct SectionReverbConfig; }
namespace duskverb::sectionreverb
{
void auditoriumShapeGains(float shape, float gains[4]);
class AuditoriumTail
{
  public:
    void prepare(const Rate& rate);
    void configure(const SectionReverbConfig& config, bool immediate);
    void reset();
    int nextRandom();
    void filterInput(const float spread[2][32], float filtered[2][32]);
    void processBlock(const float diffused[2][32], float output[2][32], float inputPeak,
                      float previousWetPeak);

  private:
    struct Line
    {
        std::vector<float> data;
        unsigned write = 0;
        float read(unsigned delay) const;
        void put(float value) { data[write] = value; }
        void step()
        {
            if (++write == data.size())
                write = 0;
        }
        void clear();
    };
    struct Section
    {
        Line lines[8], chorus[2];
        float low = 0, high = 0, feedback[32]{};
        unsigned oldDelay[9]{}, newDelay[9]{};
    };
    Section sections[2];
    Rate sr;
    float size = 38, rt = 1.4265f, bass = 1.25f, crossover = 850, cutoff = 9000;
    float definition = 38, rate = 4, depth = 58;
    int chorusType = 0, pendingType = 0;
    float position = 0, randomPos[2][2]{}, randomTarget[2][2]{};
    float period = 1000, nextPeriod = 1000, maxShift = 0, speed = 0;
    bool descending = false, depthChanged = false;
    uint32_t rng = 1;
    Ramp highPole, highFeedthrough, bassPole, gain, bassDelta, density, fade;
    int sizeState = 0;
    bool sizePending = false;
    float dynamics = 1, previousDensity = 1, smoothIn = 0, smoothOut = 0;
    float peakIn = 0, peakOut = 0, peakRatio = 1;
    float attackIn = 0, attackOut = 0, ratioCoeff = 0, release = 0, fall = 0;
    int hold = 0;

    void setGeometry(bool immediate);
    void updateDecay(bool immediate);
    void updateChorusRate();
    void stepSize();
    void updateDynamics(float in, float out);
    void makeChorusIndices(float indices[2][2][32]);
};
} // namespace duskverb::sectionreverb
