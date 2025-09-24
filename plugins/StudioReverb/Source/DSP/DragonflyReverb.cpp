/*
  ==============================================================================

    DragonflyReverb.cpp
    Implementation using actual Freeverb3 library from Dragonfly
    Based on Dragonfly Reverb by Michael Willis

  ==============================================================================
*/

#include "DragonflyReverb.h"

DragonflyReverb::DragonflyReverb()
{
    // Initialize mix levels to safe values (already in 0-1 range internally)
    dryLevel = 0.8f;     // 80%
    earlyLevel = 0.1f;   // 10%
    lateLevel = 0.2f;    // 20%
    earlySend = 0.2f;    // 20%

    // Initialize early reflections (matching Dragonfly Hall)
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setMuteOnChange(true);  // Mute when size changes to avoid artifacts
    early.setdryr(0);  // Mute dry signal
    early.setwet(0);   // 0dB wet
    early.setwidth(0.8);
    early.setLRDelay(0.3);
    early.setLRCrossApFreq(750, 4);
    early.setDiffusionApFreq(150, 4);

    // Initialize Hall reverb (zrev2)
    hall.setMuteOnChange(true);   // Mute when size changes to avoid artifacts
    hall.setwet(0);    // 0dB
    hall.setdryr(0);   // Mute dry signal
    hall.setwidth(1.0);

    // Initialize Room reverb (progenitor2)
    room.setMuteOnChange(true);   // Mute when size changes to avoid artifacts
    room.setwet(0);    // 0dB
    room.setdryr(0);   // Mute dry signal
    room.setwidth(1.0);

    // Initialize Plate reverb (strev)
    plate.setMuteOnChange(true);   // Mute when size changes to avoid artifacts
    plate.setwet(0);   // 0dB
    plate.setdryr(0);  // Mute dry signal
    plate.setwidth(1.0);

    // Clear all internal buffers
    early.mute();
    hall.mute();
    room.mute();
    plate.mute();
}

void DragonflyReverb::prepare(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    blockSize = samplesPerBlock;

    // Set sample rates for all processors
    early.setSampleRate(sampleRate);
    hall.setSampleRate(sampleRate);
    room.setSampleRate(sampleRate);
    plate.setSampleRate(sampleRate);

    // Force initial size setup
    lastSetSize = -1.0f;  // Force size to be set
    setSize(size);        // This will now properly initialize the delay lines

    // Initialize with current parameters
    updateEarlyReflections();

    switch (currentAlgorithm)
    {
        case Algorithm::Room:
            updateRoomReverb();
            break;
        case Algorithm::Hall:
            updateHallReverb();
            break;
        case Algorithm::Plate:
            updatePlateReverb();
            break;
        default:
            break;
    }

    reset();
}

void DragonflyReverb::reset()
{
    early.mute();
    hall.mute();
    room.mute();
    plate.mute();
}

void DragonflyReverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    switch (currentAlgorithm)
    {
        case Algorithm::Room:
            processRoom(buffer);
            break;
        case Algorithm::Hall:
            processHall(buffer);
            break;
        case Algorithm::Plate:
            processPlate(buffer);
            break;
        case Algorithm::EarlyReflections:
            processEarlyOnly(buffer);
            break;
    }
}

//==============================================================================
// Parameter Updates (matching Dragonfly's exact scaling)

void DragonflyReverb::setSize(float meters)
{
    size = juce::jlimit(10.0f, 60.0f, meters);

    // Only update RSFactor if size has changed significantly
    // This avoids delay artifacts when parameters are being smoothed
    if (std::abs(size - lastSetSize) > 0.5f)  // Only update if changed by more than 0.5 meters
    {
        lastSetSize = size;

        // Update early reflections size
        early.setRSFactor(size / 10.0f);  // Dragonfly scales by 10 for early

        // Update late reverb size based on algorithm
        switch (currentAlgorithm)
        {
            case Algorithm::Hall:
                hall.setRSFactor(size / 80.0f);  // Dragonfly scales by 80 for hall
                break;
            case Algorithm::Room:
                room.setRSFactor(size / 50.0f);  // Smaller scaling for room
                break;
            case Algorithm::Plate:
                plate.setRSFactor(size / 100.0f);
                break;
            default:
                break;
        }

        // Note: With setMuteOnChange(true), the reverb will automatically
        // clear its delay lines to avoid artifacts
    }
}

void DragonflyReverb::setWidth(float percent)
{
    width = juce::jlimit(0.0f, 100.0f, percent);
    float w = width / 100.0f;

    early.setwidth(w);
    hall.setwidth(w);
    room.setwidth(w);
    plate.setwidth(w);
}

void DragonflyReverb::setPreDelay(float ms)
{
    // Freeverb3 doesn't handle zero predelay well, use 0.1 minimum
    preDelay = juce::jlimit(0.1f, 100.0f, ms < 0.1f ? 0.1f : ms);

    hall.setPreDelay(preDelay);
    room.setPreDelay(preDelay);
    plate.setPreDelay(preDelay);
}

void DragonflyReverb::setDiffuse(float percent)
{
    diffusion = juce::jlimit(0.0f, 100.0f, percent);
    float diff = diffusion / 140.0f;  // Dragonfly scales by 140

    hall.setidiffusion1(diff);
    hall.setapfeedback(diff);

    room.setidiffusion1(diff);
    // progenitor2 doesn't have setapfeedback
    room.setodiffusion1(diff);  // Use output diffusion instead

    plate.setidiffusion1(diff);
    // strev doesn't have setapfeedback
    plate.setidiffusion2(diff * 0.8f);  // Use second input diffusion
}

void DragonflyReverb::setDecay(float seconds)
{
    decay = juce::jlimit(0.1f, 10.0f, seconds);

    hall.setrt60(decay);
    room.setrt60(decay);
    plate.setrt60(decay);
}

void DragonflyReverb::setLowCut(float freq)
{
    lowCut = juce::jlimit(0.0f, 200.0f, freq);

    early.setoutputhpf(lowCut);
    hall.setoutputhpf(lowCut);
    // progenitor2 doesn't have setoutputhpf, uses dccutfreq instead
    room.setdccutfreq(lowCut);
    // strev doesn't have setoutputhpf either
    // We'll handle this through input damping
}

void DragonflyReverb::setHighCut(float freq)
{
    highCut = juce::jlimit(1000.0f, 20000.0f, freq);

    early.setoutputlpf(highCut);
    hall.setoutputlpf(highCut);
    // progenitor2 doesn't have setoutputlpf, uses setoutputdamp instead
    // Convert frequency to damping value (0-1)
    float damp = 1.0f - (freq / 20000.0f);
    room.setoutputdamp(damp);
    plate.setoutputdamp(damp);  // strev also uses setoutputdamp
}

void DragonflyReverb::setLowCrossover(float freq)
{
    lowXover = freq;
    hall.setxover_low(freq);
    // progenitor2 doesn't have setxover_low
    // Use bass bandwidth control instead
    room.setbassbw(freq / 100.0f);  // Scale to reasonable range
}

void DragonflyReverb::setHighCrossover(float freq)
{
    highXover = freq;
    hall.setxover_high(freq);
    // progenitor2 doesn't have setxover_high
    // This parameter is specific to zrev2
}

void DragonflyReverb::setLowMult(float mult)
{
    lowMult = mult;
    hall.setrt60_factor_low(mult);
    // progenitor2 doesn't have setrt60_factor_low
    // Use bass boost instead
    room.setbassboost(mult);
}

void DragonflyReverb::setHighMult(float mult)
{
    highMult = mult;
    hall.setrt60_factor_high(mult);
    // progenitor2 doesn't have setrt60_factor_high
    // Use damping controls instead
    room.setdamp(1.0f - mult);
}

void DragonflyReverb::setSpin(float amount)
{
    spin = amount;
    hall.setspin(amount);
    room.setspin(amount);  // progenitor2 has spin
    plate.setspin(amount); // strev has spin too
}

void DragonflyReverb::setWander(float amount)
{
    wander = amount;
    hall.setwander(amount);
    room.setwander(amount);  // progenitor2 has wander
    plate.setwander(amount); // strev has wander too
}

void DragonflyReverb::setModulation(float percent)
{
    // Hall-specific modulation depth
    // This is already handled by spin/wander
    // Just store the value for reference
    // modulation = percent;
}

void DragonflyReverb::setEarlyDamp(float freq)
{
    // Room-specific early reflection damping
    // Only set if we have a valid frequency
    if (freq > 0.0f && sampleRate > 0.0)
        early.setoutputlpf(freq);
}

void DragonflyReverb::setLateDamp(float freq)
{
    // Room-specific late reverb damping
    // Ensure normalized frequency is in valid range [0, 1]
    if (sampleRate > 0.0)
    {
        float normalized = juce::jlimit(0.0f, 1.0f, static_cast<float>(freq / (sampleRate * 0.5f)));
        room.setdamp(normalized);
    }
}

void DragonflyReverb::setLowBoost(float percent)
{
    // Room-specific low frequency boost
    // idiffusion1 expects a value between 0 and 1
    // Just map the boost percentage to a safe diffusion range
    float diffusionValue = juce::jlimit(0.0f, 0.99f, 0.5f + (percent / 200.0f));
    room.setidiffusion1(diffusionValue);
}

void DragonflyReverb::setBoostFreq(float freq)
{
    // Room-specific boost frequency center
    // This sets the crossover for low frequency treatment
    // Store for future use - would need custom EQ implementation
    // boostFreq = freq;
    juce::ignoreUnused(freq);
}

void DragonflyReverb::setDamping(float freq)
{
    // Plate-specific overall damping
    // Ensure normalized frequency is in valid range [0, 1]
    if (sampleRate > 0.0)
    {
        float normalized = juce::jlimit(0.0f, 1.0f, static_cast<float>(freq / (sampleRate * 0.5f)));
        plate.setdamp(normalized);
    }
}

//==============================================================================
// Update functions for each reverb type

void DragonflyReverb::updateEarlyReflections()
{
    early.setRSFactor(size / 10.0f);
    early.setwidth(width / 100.0f);
    early.setoutputhpf(lowCut);
    early.setoutputlpf(highCut);
}

void DragonflyReverb::updateHallReverb()
{
    hall.setRSFactor(size / 80.0f);
    hall.setwidth(width / 100.0f);
    hall.setPreDelay(preDelay < 0.1f ? 0.1f : preDelay);
    hall.setidiffusion1(diffusion / 140.0f);
    hall.setapfeedback(diffusion / 140.0f);
    hall.setrt60(decay);
    hall.setoutputhpf(lowCut);
    hall.setoutputlpf(highCut);
    hall.setxover_low(lowXover);
    hall.setxover_high(highXover);
    hall.setrt60_factor_low(lowMult);
    hall.setrt60_factor_high(highMult);
    hall.setspin(spin);
    hall.setwander(wander);
}

void DragonflyReverb::updateRoomReverb()
{
    room.setRSFactor(size / 50.0f);
    room.setwidth(width / 100.0f);
    room.setPreDelay(preDelay < 0.1f ? 0.1f : preDelay);
    room.setidiffusion1(diffusion / 140.0f);
    room.setodiffusion1(diffusion / 140.0f);  // progenitor2 uses odiffusion
    room.setrt60(decay);
    room.setdccutfreq(lowCut);  // progenitor2 uses dccutfreq for low cut
    float damp = 1.0f - (highCut / 20000.0f);
    room.setoutputdamp(damp);  // progenitor2 uses damping for high cut
    room.setbassbw(lowXover / 100.0f);  // Scale crossover to bass bandwidth
    room.setbassboost(lowMult);  // Use bass boost for low mult
    room.setdamp(1.0f - highMult);  // Use damping for high mult
    room.setspin(spin);
    room.setwander(wander);
}

void DragonflyReverb::updatePlateReverb()
{
    plate.setRSFactor(size / 100.0f);
    plate.setwidth(width / 100.0f);
    plate.setPreDelay(preDelay < 0.1f ? 0.1f : preDelay);
    plate.setidiffusion1(diffusion / 140.0f);
    plate.setidiffusion2(diffusion / 140.0f * 0.8f);  // strev uses two input diffusions
    plate.setrt60(decay);
    // strev doesn't have setoutputhpf/lpf, use damping instead
    float damp = 1.0f - (highCut / 20000.0f);
    plate.setoutputdamp(damp);
    plate.setspin(spin);
    plate.setwander(wander);
}

//==============================================================================
// Processing functions for each algorithm (matching Dragonfly's signal flow)

void DragonflyReverb::processHall(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2) return;

    float* inputL = buffer.getWritePointer(0);
    float* inputR = buffer.getWritePointer(1);

    // Process in chunks matching BUFFER_SIZE
    int samplesProcessed = 0;
    while (samplesProcessed < numSamples)
    {
        int samplesToProcess = juce::jmin(static_cast<int>(BUFFER_SIZE),
                                         numSamples - samplesProcessed);

        // Clear buffers
        std::memset(earlyOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(earlyOutBuffer[1], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[1], 0, sizeof(float) * samplesToProcess);

        // Process early reflections
        early.processreplace(
            inputL + samplesProcessed,
            inputR + samplesProcessed,
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Prepare late reverb input (dry + early send)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            lateInBuffer[0][i] = inputL[samplesProcessed + i] +
                                 earlyOutBuffer[0][i] * earlySend;
            lateInBuffer[1][i] = inputR[samplesProcessed + i] +
                                 earlyOutBuffer[1][i] * earlySend;
        }

        // Process late reverb
        hall.processreplace(
            lateInBuffer[0],
            lateInBuffer[1],
            lateOutBuffer[0],
            lateOutBuffer[1],
            samplesToProcess
        );

        // Mix output (dry + early + late)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            float outL = inputL[samplesProcessed + i] * dryLevel +
                        earlyOutBuffer[0][i] * earlyLevel +
                        lateOutBuffer[0][i] * lateLevel;

            float outR = inputR[samplesProcessed + i] * dryLevel +
                        earlyOutBuffer[1][i] * earlyLevel +
                        lateOutBuffer[1][i] * lateLevel;

            inputL[samplesProcessed + i] = outL;
            inputR[samplesProcessed + i] = outR;
        }

        samplesProcessed += samplesToProcess;
    }
}

void DragonflyReverb::processRoom(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2) return;


    float* inputL = buffer.getWritePointer(0);
    float* inputR = buffer.getWritePointer(1);

    // Process in chunks
    int samplesProcessed = 0;
    while (samplesProcessed < numSamples)
    {
        int samplesToProcess = juce::jmin(static_cast<int>(BUFFER_SIZE),
                                         numSamples - samplesProcessed);

        // Clear buffers
        std::memset(earlyOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(earlyOutBuffer[1], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[1], 0, sizeof(float) * samplesToProcess);

        // Process early reflections
        early.processreplace(
            inputL + samplesProcessed,
            inputR + samplesProcessed,
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Prepare late reverb input
        for (int i = 0; i < samplesToProcess; ++i)
        {
            lateInBuffer[0][i] = inputL[samplesProcessed + i] +
                                 earlyOutBuffer[0][i] * earlySend;
            lateInBuffer[1][i] = inputR[samplesProcessed + i] +
                                 earlyOutBuffer[1][i] * earlySend;
        }

        // Process late reverb with Room algorithm
        room.processreplace(
            lateInBuffer[0],
            lateInBuffer[1],
            lateOutBuffer[0],
            lateOutBuffer[1],
            samplesToProcess
        );


        // Mix output - Room uses both early and late reverb
        for (int i = 0; i < samplesToProcess; ++i)
        {
            float outL = inputL[samplesProcessed + i] * dryLevel +
                        earlyOutBuffer[0][i] * earlyLevel +
                        lateOutBuffer[0][i] * lateLevel;
            float outR = inputR[samplesProcessed + i] * dryLevel +
                        earlyOutBuffer[1][i] * earlyLevel +
                        lateOutBuffer[1][i] * lateLevel;

            inputL[samplesProcessed + i] = outL;
            inputR[samplesProcessed + i] = outR;
        }

        samplesProcessed += samplesToProcess;
    }
}

void DragonflyReverb::processPlate(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2) return;

    float* inputL = buffer.getWritePointer(0);
    float* inputR = buffer.getWritePointer(1);

    // Process in chunks
    int samplesProcessed = 0;
    while (samplesProcessed < numSamples)
    {
        int samplesToProcess = juce::jmin(static_cast<int>(BUFFER_SIZE),
                                         numSamples - samplesProcessed);

        // Clear buffers
        std::memset(earlyOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(earlyOutBuffer[1], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[1], 0, sizeof(float) * samplesToProcess);

        // For plate, we still process early reflections but don't use them in output
        early.processreplace(
            inputL + samplesProcessed,
            inputR + samplesProcessed,
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Plate gets pure input (no early send for plate reverb)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            lateInBuffer[0][i] = inputL[samplesProcessed + i];
            lateInBuffer[1][i] = inputR[samplesProcessed + i];
        }

        // Process plate reverb
        plate.processreplace(
            lateInBuffer[0],
            lateInBuffer[1],
            lateOutBuffer[0],
            lateOutBuffer[1],
            samplesToProcess
        );

        // Mix output - Plate uses only late reverb
        for (int i = 0; i < samplesToProcess; ++i)
        {
            float outL = inputL[samplesProcessed + i] * dryLevel +
                        lateOutBuffer[0][i] * lateLevel;
            float outR = inputR[samplesProcessed + i] * dryLevel +
                        lateOutBuffer[1][i] * lateLevel;

            inputL[samplesProcessed + i] = outL;
            inputR[samplesProcessed + i] = outR;
        }

        samplesProcessed += samplesToProcess;
    }
}

void DragonflyReverb::processEarlyOnly(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2) return;

    float* inputL = buffer.getWritePointer(0);
    float* inputR = buffer.getWritePointer(1);

    // Process in chunks
    int samplesProcessed = 0;
    while (samplesProcessed < numSamples)
    {
        int samplesToProcess = juce::jmin(static_cast<int>(BUFFER_SIZE),
                                         numSamples - samplesProcessed);

        // Clear buffers
        std::memset(earlyOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(earlyOutBuffer[1], 0, sizeof(float) * samplesToProcess);

        // Process early reflections only
        early.processreplace(
            inputL + samplesProcessed,
            inputR + samplesProcessed,
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Mix output (dry + early only, no late)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            float outL = inputL[samplesProcessed + i] * dryLevel +
                        earlyOutBuffer[0][i] * earlyLevel;
            float outR = inputR[samplesProcessed + i] * dryLevel +
                        earlyOutBuffer[1][i] * earlyLevel;

            inputL[samplesProcessed + i] = outL;
            inputR[samplesProcessed + i] = outR;
        }

        samplesProcessed += samplesToProcess;
    }
}