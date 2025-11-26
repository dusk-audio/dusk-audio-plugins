#include "GrooveFollower.h"
#include <cmath>
#include <numeric>

GrooveFollower::GrooveFollower()
{
    currentTemplate.reset();
}

void GrooveFollower::reset()
{
    templateBuffer.clear();
    currentTemplate.reset();
    lockPercentage = 0.0f;
    locked = false;
    lastSwing8 = 0.0f;
    lastSwing16 = 0.0f;
    swingConsistency = 0.0f;
}

void GrooveFollower::update(const GrooveTemplate& newGroove)
{
    if (!newGroove.isValid())
        return;

    // Add to buffer
    templateBuffer.push_back(newGroove);
    if (templateBuffer.size() > TEMPLATE_BUFFER_SIZE)
        templateBuffer.pop_front();

    // Apply smoothing
    smoothTemplate(newGroove);

    // Update lock state
    updateLockState();
}

void GrooveFollower::smoothTemplate(const GrooveTemplate& newTemplate)
{
    float alpha = smoothingAlpha;
    float oneMinusAlpha = 1.0f - alpha;

    // If this is the first template, use it directly
    if (currentTemplate.noteCount == 0)
    {
        currentTemplate = newTemplate;
        return;
    }

    // Exponential smoothing for each field
    currentTemplate.swing8 = currentTemplate.swing8 * oneMinusAlpha + newTemplate.swing8 * alpha;
    currentTemplate.swing16 = currentTemplate.swing16 * oneMinusAlpha + newTemplate.swing16 * alpha;
    currentTemplate.avgVelocity = currentTemplate.avgVelocity * oneMinusAlpha + newTemplate.avgVelocity * alpha;
    currentTemplate.velocityRange = currentTemplate.velocityRange * oneMinusAlpha + newTemplate.velocityRange * alpha;
    currentTemplate.energy = currentTemplate.energy * oneMinusAlpha + newTemplate.energy * alpha;
    currentTemplate.density = currentTemplate.density * oneMinusAlpha + newTemplate.density * alpha;
    currentTemplate.syncopation = currentTemplate.syncopation * oneMinusAlpha + newTemplate.syncopation * alpha;

    // Smooth micro-offsets
    for (int i = 0; i < 32; ++i)
    {
        currentTemplate.microOffset[i] = currentTemplate.microOffset[i] * oneMinusAlpha +
                                         newTemplate.microOffset[i] * alpha;
    }

    // Smooth accent pattern
    for (int i = 0; i < 16; ++i)
    {
        currentTemplate.accentPattern[i] = currentTemplate.accentPattern[i] * oneMinusAlpha +
                                           newTemplate.accentPattern[i] * alpha;
    }

    // Primary division: use the new one if it's been consistent
    if (templateBuffer.size() >= 2)
    {
        int divisionVotes8 = 0;
        int divisionVotes16 = 0;
        for (const auto& t : templateBuffer)
        {
            if (t.primaryDivision == 8)
                divisionVotes8++;
            else
                divisionVotes16++;
        }
        currentTemplate.primaryDivision = (divisionVotes16 > divisionVotes8) ? 16 : 8;
    }

    // Accumulate note count
    currentTemplate.noteCount = newTemplate.noteCount;
}

void GrooveFollower::updateLockState()
{
    // Calculate lock percentage based on several factors

    // Factor 1: Note count (more notes = more confidence)
    float noteCountFactor = std::min(1.0f, static_cast<float>(currentTemplate.noteCount) /
                                           static_cast<float>(minNotesForLock * 2));

    // Factor 2: Consistency across buffer
    float consistencyFactor = 0.0f;
    if (templateBuffer.size() >= 2)
    {
        float totalConsistency = 0.0f;
        for (size_t i = 1; i < templateBuffer.size(); ++i)
        {
            totalConsistency += calculateConsistency(templateBuffer[i - 1], templateBuffer[i]);
        }
        consistencyFactor = totalConsistency / static_cast<float>(templateBuffer.size() - 1);
    }
    else if (templateBuffer.size() == 1)
    {
        consistencyFactor = 0.5f;  // Single template, moderate confidence
    }

    // Factor 3: Swing consistency
    if (templateBuffer.size() >= 2)
    {
        float swingDiff = std::abs(currentTemplate.swing16 - lastSwing16);
        swingConsistency = swingConsistency * 0.8f + (1.0f - std::min(1.0f, swingDiff * 5.0f)) * 0.2f;
    }

    lastSwing8 = currentTemplate.swing8;
    lastSwing16 = currentTemplate.swing16;

    // Combine factors
    lockPercentage = (noteCountFactor * 0.4f +
                     consistencyFactor * 0.4f +
                     swingConsistency * 0.2f) * 100.0f;

    // Determine locked state
    locked = (currentTemplate.noteCount >= minNotesForLock) && (lockPercentage > 50.0f);
}

float GrooveFollower::calculateConsistency(const GrooveTemplate& a, const GrooveTemplate& b) const
{
    // Calculate similarity between two templates (0.0 = different, 1.0 = identical)
    float swingDiff = std::abs(a.swing16 - b.swing16) + std::abs(a.swing8 - b.swing8);
    float energyDiff = std::abs(a.energy - b.energy);
    float densityDiff = std::abs(a.density - b.density);
    float syncDiff = std::abs(a.syncopation - b.syncopation);

    // Normalize differences
    swingDiff = std::min(1.0f, swingDiff * 2.0f);
    energyDiff = std::min(1.0f, energyDiff * 2.0f);

    // Calculate overall similarity
    float similarity = 1.0f - (swingDiff * 0.4f + energyDiff * 0.3f +
                               densityDiff * 0.15f + syncDiff * 0.15f);

    return std::max(0.0f, similarity);
}

GrooveTemplate GrooveFollower::getCurrent(double playheadBars) const
{
    // For now, just return the smoothed template
    // In the future, could implement lookahead interpolation based on playhead position
    juce::ignoreUnused(playheadBars);
    return currentTemplate;
}

float GrooveFollower::getLockPercentage() const
{
    return lockPercentage;
}

bool GrooveFollower::isLocked() const
{
    return locked;
}

void GrooveFollower::setSmoothingFactor(float alpha)
{
    smoothingAlpha = juce::jlimit(0.0f, 1.0f, alpha);
}

void GrooveFollower::setMinNotesForLock(int count)
{
    minNotesForLock = std::max(1, count);
}