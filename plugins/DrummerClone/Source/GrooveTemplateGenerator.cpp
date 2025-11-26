#include "GrooveTemplateGenerator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

GrooveTemplateGenerator::GrooveTemplateGenerator()
{
    prepare(44100.0);
}

void GrooveTemplateGenerator::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

void GrooveTemplateGenerator::reset()
{
    // No persistent state to reset currently
}

GrooveTemplate GrooveTemplateGenerator::generateFromOnsets(const std::vector<double>& onsetTimes,
                                                           double bpm,
                                                           double sr)
{
    GrooveTemplate templ;
    templ.reset();

    if (onsetTimes.size() < 4 || bpm <= 0.0)
        return templ;

    templ.noteCount = static_cast<int>(onsetTimes.size());

    // Determine primary division (8th or 16th notes)
    templ.primaryDivision = determinePrimaryDivision(onsetTimes, bpm);

    // Calculate swing
    templ.swing8 = calculateSwing(onsetTimes, bpm, 8);
    templ.swing16 = calculateSwing(onsetTimes, bpm, 16);

    // Calculate micro-timing offsets
    calculateMicroOffsets(onsetTimes, bpm, templ.microOffset);

    // Calculate syncopation
    templ.syncopation = calculateSyncopation(onsetTimes, bpm);

    // Calculate energy from onset density
    double beatsInWindow = 2.0 * bpm / 60.0;  // 2 seconds of beats
    double onsetsPerBeat = static_cast<double>(onsetTimes.size()) / beatsInWindow;
    templ.density = juce::jlimit(0.0f, 1.0f, static_cast<float>(onsetsPerBeat / 4.0));  // Normalize to ~4 onsets/beat max

    // Energy is derived from density for audio (no velocity info)
    templ.energy = templ.density;

    // Default velocity for audio onsets
    templ.avgVelocity = 90.0f + (templ.energy * 30.0f);  // 90-120 based on energy

    return templ;
}

GrooveTemplate GrooveTemplateGenerator::generateFromMidi(const ExtractedGroove& groove, double bpm)
{
    GrooveTemplate templ;
    templ.reset();

    if (groove.noteCount < 4 || bpm <= 0.0)
        return templ;

    templ.noteCount = groove.noteCount;

    // Determine primary division
    templ.primaryDivision = determinePrimaryDivision(groove.noteOnTimes, bpm);

    // Calculate swing
    templ.swing8 = calculateSwing(groove.noteOnTimes, bpm, 8);
    templ.swing16 = calculateSwing(groove.noteOnTimes, bpm, 16);

    // Calculate micro-timing offsets
    calculateMicroOffsets(groove.noteOnTimes, bpm, templ.microOffset);

    // Calculate syncopation
    templ.syncopation = calculateSyncopation(groove.noteOnTimes, bpm);

    // Use actual velocity data
    templ.avgVelocity = static_cast<float>(groove.averageVelocity);
    templ.velocityRange = static_cast<float>(std::sqrt(groove.velocityVariance));

    // Calculate energy from velocity
    templ.energy = juce::jlimit(0.0f, 1.0f, static_cast<float>(groove.averageVelocity / 127.0));

    // Calculate density
    templ.density = juce::jlimit(0.0f, 1.0f, static_cast<float>(groove.noteDensity / 4.0));

    // Calculate accent pattern from velocities
    calculateAccentPattern(groove.noteOnTimes, groove.velocities, bpm, templ.accentPattern);

    return templ;
}

int GrooveTemplateGenerator::determinePrimaryDivision(const std::vector<double>& hitTimes, double bpm)
{
    if (hitTimes.size() < 2)
        return 16;

    // Calculate intervals between hits
    std::vector<double> intervals;
    for (size_t i = 1; i < hitTimes.size(); ++i)
    {
        intervals.push_back(hitTimes[i] - hitTimes[i - 1]);
    }

    // Calculate beat duration
    double beatDuration = 60.0 / bpm;
    double eighth = beatDuration / 2.0;
    double sixteenth = beatDuration / 4.0;

    // Count how many intervals are closer to 8th vs 16th
    int eighthCount = 0;
    int sixteenthCount = 0;

    for (double interval : intervals)
    {
        double eighthDiff = std::abs(interval - eighth);
        double sixteenthDiff = std::abs(interval - sixteenth);

        // Also check multiples
        double eighthDiff2 = std::abs(interval - eighth * 2);
        double sixteenthDiff2 = std::abs(interval - sixteenth * 2);

        if (std::min(eighthDiff, eighthDiff2) < std::min(sixteenthDiff, sixteenthDiff2))
            eighthCount++;
        else
            sixteenthCount++;
    }

    return (sixteenthCount > eighthCount) ? 16 : 8;
}

float GrooveTemplateGenerator::calculateSwing(const std::vector<double>& hitTimes,
                                              double bpm, int division)
{
    if (hitTimes.size() < 4)
        return 0.0f;

    double beatDuration = 60.0 / bpm;
    double subdivisionDuration = beatDuration / (division / 4.0);  // Duration of one subdivision

    std::vector<double> upbeatDeviations;

    for (double hitTime : hitTimes)
    {
        // Get position within the beat
        double beatPosition = std::fmod(hitTime, beatDuration);
        int subdivisionIndex = static_cast<int>(std::round(beatPosition / subdivisionDuration));

        // Check if this is an upbeat (odd subdivision)
        if (subdivisionIndex % 2 == 1)
        {
            // Calculate expected position
            double expectedPosition = subdivisionIndex * subdivisionDuration;
            double deviation = beatPosition - expectedPosition;

            // Convert to ratio of subdivision duration
            upbeatDeviations.push_back(deviation / subdivisionDuration);
        }
    }

    if (upbeatDeviations.empty())
        return 0.0f;

    // Calculate average deviation
    double avgDeviation = std::accumulate(upbeatDeviations.begin(), upbeatDeviations.end(), 0.0)
                         / static_cast<double>(upbeatDeviations.size());

    // Clamp to valid swing range (0 to 0.5)
    return juce::jlimit(0.0f, 0.5f, static_cast<float>(avgDeviation));
}

void GrooveTemplateGenerator::calculateMicroOffsets(const std::vector<double>& hitTimes,
                                                    double bpm,
                                                    std::array<float, 32>& offsets)
{
    offsets.fill(0.0f);

    if (hitTimes.empty() || bpm <= 0.0)
        return;

    double beatDuration = 60.0 / bpm;
    double thirtySecondDuration = beatDuration / 8.0;  // 32nd note duration

    // Count and accumulate offsets for each 32nd position
    std::array<double, 32> offsetSums = {0};
    std::array<int, 32> offsetCounts = {0};

    for (double hitTime : hitTimes)
    {
        // Get position within the bar (4 beats)
        double barDuration = beatDuration * 4.0;
        double barPosition = std::fmod(hitTime, barDuration);

        // Find nearest 32nd note position
        int position = static_cast<int>(std::round(barPosition / thirtySecondDuration)) % 32;

        // Calculate deviation in milliseconds
        double expectedTime = position * thirtySecondDuration;
        double deviationMs = (barPosition - expectedTime) * 1000.0;

        offsetSums[position] += deviationMs;
        offsetCounts[position]++;
    }

    // Calculate averages
    for (int i = 0; i < 32; ++i)
    {
        if (offsetCounts[i] > 0)
        {
            offsets[i] = static_cast<float>(offsetSums[i] / offsetCounts[i]);
            // Clamp to reasonable range (±30ms)
            offsets[i] = juce::jlimit(-30.0f, 30.0f, offsets[i]);
        }
    }
}

float GrooveTemplateGenerator::calculateSyncopation(const std::vector<double>& hitTimes, double bpm)
{
    if (hitTimes.empty() || bpm <= 0.0)
        return 0.0f;

    double beatDuration = 60.0 / bpm;

    int onBeatCount = 0;
    int offBeatCount = 0;

    for (double hitTime : hitTimes)
    {
        // Get position within the beat
        double beatPosition = std::fmod(hitTime, beatDuration);
        double normalizedPosition = beatPosition / beatDuration;

        // Check if on strong beat positions (0, 0.25, 0.5, 0.75 with tolerance)
        bool isOnBeat = false;
        for (double strongPos : {0.0, 0.25, 0.5, 0.75})
        {
            if (std::abs(normalizedPosition - strongPos) < 0.1)
            {
                isOnBeat = true;
                break;
            }
        }

        if (isOnBeat)
            onBeatCount++;
        else
            offBeatCount++;
    }

    int total = onBeatCount + offBeatCount;
    if (total == 0)
        return 0.0f;

    // Syncopation is ratio of offbeat hits
    return static_cast<float>(offBeatCount) / static_cast<float>(total);
}

void GrooveTemplateGenerator::calculateAccentPattern(const std::vector<double>& hitTimes,
                                                     const std::vector<int>& velocities,
                                                     double bpm,
                                                     std::array<float, 16>& pattern)
{
    // Initialize with default pattern
    pattern = {1.0f, 0.3f, 0.5f, 0.3f, 0.8f, 0.3f, 0.5f, 0.3f,
               0.9f, 0.3f, 0.5f, 0.3f, 0.8f, 0.3f, 0.5f, 0.3f};

    if (hitTimes.size() != velocities.size() || hitTimes.empty() || bpm <= 0.0)
        return;

    double beatDuration = 60.0 / bpm;
    double sixteenthDuration = beatDuration / 4.0;
    double barDuration = beatDuration * 4.0;

    // Accumulate velocities per 16th note position
    std::array<double, 16> velSums = {0};
    std::array<int, 16> velCounts = {0};

    for (size_t i = 0; i < hitTimes.size(); ++i)
    {
        double barPosition = std::fmod(hitTimes[i], barDuration);
        int position = static_cast<int>(std::round(barPosition / sixteenthDuration)) % 16;

        velSums[position] += velocities[i];
        velCounts[position]++;
    }

    // Calculate average velocities and normalize
    double maxVel = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        if (velCounts[i] > 0)
        {
            double avg = velSums[i] / velCounts[i];
            pattern[i] = static_cast<float>(avg);
            maxVel = std::max(maxVel, avg);
        }
    }

    // Normalize to 0-1 range
    if (maxVel > 0.0)
    {
        for (int i = 0; i < 16; ++i)
        {
            pattern[i] = pattern[i] / static_cast<float>(maxVel);
        }
    }
}

GrooveTemplate GrooveTemplateGenerator::blend(const GrooveTemplate& a,
                                              const GrooveTemplate& b,
                                              float blendFactor)
{
    GrooveTemplate result;

    float fa = 1.0f - blendFactor;
    float fb = blendFactor;

    result.swing8 = a.swing8 * fa + b.swing8 * fb;
    result.swing16 = a.swing16 * fa + b.swing16 * fb;
    result.avgVelocity = a.avgVelocity * fa + b.avgVelocity * fb;
    result.velocityRange = a.velocityRange * fa + b.velocityRange * fb;
    result.energy = a.energy * fa + b.energy * fb;
    result.density = a.density * fa + b.density * fb;
    result.syncopation = a.syncopation * fa + b.syncopation * fb;

    // Blend micro-offsets
    for (int i = 0; i < 32; ++i)
    {
        result.microOffset[i] = a.microOffset[i] * fa + b.microOffset[i] * fb;
    }

    // Blend accent pattern
    for (int i = 0; i < 16; ++i)
    {
        result.accentPattern[i] = a.accentPattern[i] * fa + b.accentPattern[i] * fb;
    }

    // Use the more common primary division
    result.primaryDivision = (blendFactor < 0.5f) ? a.primaryDivision : b.primaryDivision;

    result.noteCount = a.noteCount + b.noteCount;

    return result;
}

double GrooveTemplateGenerator::quantizeToGrid(double timeSeconds, double bpm, int division)
{
    double beatDuration = 60.0 / bpm;
    double gridSize = beatDuration / (division / 4.0);
    return std::round(timeSeconds / gridSize) * gridSize;
}

int GrooveTemplateGenerator::getGridPosition(double timeSeconds, double bpm, int division)
{
    double beatDuration = 60.0 / bpm;
    double gridSize = beatDuration / (division / 4.0);
    return static_cast<int>(std::round(timeSeconds / gridSize));
}

double GrooveTemplateGenerator::getDeviationFromGrid(double timeSeconds, double bpm, int division)
{
    double quantized = quantizeToGrid(timeSeconds, bpm, division);
    return (timeSeconds - quantized) * 1000.0;  // Return in milliseconds
}