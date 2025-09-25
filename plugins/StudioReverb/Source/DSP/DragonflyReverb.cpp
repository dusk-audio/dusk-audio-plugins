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
    // Initialize mix levels for typical reverb sound (matching Dragonfly defaults)
    dryLevel = 0.5f;     // 50% - balanced dry signal
    earlyLevel = 0.3f;   // 30% - more prominent early reflections
    lateLevel = 0.5f;    // 50% - strong late reverb
    earlySend = 0.35f;   // 35% - good blend into late reverb

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

    // Validate and set buffer size to ensure we don't overflow
    currentBufferSize = juce::jmin(static_cast<size_t>(samplesPerBlock), MAX_BUFFER_SIZE);
    if (currentBufferSize < 1)
        currentBufferSize = DEFAULT_BUFFER_SIZE;

    // Clear all buffers before use
    std::memset(earlyOutBuffer, 0, sizeof(earlyOutBuffer));
    std::memset(lateInBuffer, 0, sizeof(lateInBuffer));
    std::memset(lateOutBuffer, 0, sizeof(lateOutBuffer));

    // Set sample rates for all processors
    early.setSampleRate(sampleRate);
    hall.setSampleRate(sampleRate);
    room.setSampleRate(sampleRate);
    plate.setSampleRate(sampleRate);

    // Initialize all processors with current algorithm settings
    // This ensures they're in a known state
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setMuteOnChange(true);

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
    if (std::abs(size - lastSetSize) > 0.1f)  // More responsive to size changes
    {
        lastSetSize = size;

        // Update early reflections size - matching Dragonfly exactly
        early.setRSFactor(size / 33.0f);  // Dragonfly Hall uses 33 for early

        // Update late reverb size based on algorithm - exact Dragonfly values
        switch (currentAlgorithm)
        {
            case Algorithm::Hall:
                hall.setRSFactor(size / 210.0f);  // Dragonfly Hall uses 210
                break;
            case Algorithm::Room:
                room.setRSFactor(size / 140.0f);  // Dragonfly Room uses 140
                break;
            case Algorithm::Plate:
                plate.setRSFactor(size / 340.0f); // Dragonfly Plate uses 340
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
    preDelay = juce::jlimit(0.0f, 100.0f, ms);

    // Allow true zero predelay when user wants it
    // Freeverb3 can handle 0 if we're careful with the implementation
    if (preDelay < 0.001f)  // Essentially zero
    {
        hall.setPreDelay(0.0f);
        room.setPreDelay(0.0f);
        plate.setPreDelay(0.0f);
    }
    else
    {
        hall.setPreDelay(preDelay);
        room.setPreDelay(preDelay);
        plate.setPreDelay(preDelay);
    }
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
    // Match Dragonfly Hall early reflections exactly
    early.setRSFactor(size / 33.0f);  // Corrected scaling
    early.setwidth(width / 100.0f);
    early.setLRDelay(0.3f);  // Stereo spread
    early.setLRCrossApFreq(750, 4);  // Cross AP frequency
    early.setDiffusionApFreq(150, 4);  // Diffusion frequency
    early.setoutputhpf(lowCut);
    early.setoutputlpf(highCut);
    early.setwet(0);  // 0dB wet signal
    early.setdryr(0); // Mute dry in early processor
}

void DragonflyReverb::updateHallReverb()
{
    // Match Dragonfly Hall algorithm parameters exactly
    hall.setRSFactor(size / 210.0f);  // Corrected scaling for Hall
    hall.setwidth(width / 100.0f);
    hall.setPreDelay(preDelay);

    // Diffusion settings
    float diff = diffusion / 100.0f;
    hall.setidiffusion1(diff * 0.75f);
    hall.setapfeedback(diff * 0.5f);
    // zrev2 doesn't have setidiffusion2 or setodiffusion methods

    // Core reverb settings
    hall.setrt60(decay);
    hall.setoutputhpf(lowCut);
    hall.setoutputlpf(highCut);

    // Crossover and frequency-dependent decay
    hall.setxover_low(lowXover);
    hall.setxover_high(highXover);
    hall.setrt60_factor_low(lowMult);
    hall.setrt60_factor_high(highMult);

    // Modulation
    hall.setspin(spin * 0.5f);  // Scaled for subtlety
    hall.setwander(wander * 0.3f);  // Scaled for subtlety

    // Ensure proper wet/dry settings
    hall.setwet(0);  // 0dB
    hall.setdryr(0); // Mute dry in processor
}

void DragonflyReverb::updateRoomReverb()
{
    // Match Dragonfly Room algorithm parameters exactly
    room.setRSFactor(size / 140.0f);  // Corrected scaling for Room
    room.setwidth(width / 100.0f);
    room.setPreDelay(preDelay);

    // Diffusion settings for Progenitor2
    float diff = diffusion / 100.0f;
    room.setidiffusion1(diff * 0.75f);
    room.setodiffusion1(diff * 0.7f);
    // progenitor2 doesn't have setidiffusion2/setodiffusion2

    // Core reverb settings
    room.setrt60(decay);
    room.setdccutfreq(lowCut);  // DC cut for rumble control

    // High frequency damping - properly scaled
    float dampValue = juce::jlimit(0.0f, 0.99f, 1.0f - (highCut / 22000.0f));
    room.setdamp(dampValue);
    room.setoutputdamp(dampValue * 0.5f);  // Less aggressive output damping

    // Bass controls
    room.setbassbw(juce::jlimit(0.0f, 1.0f, lowXover / 500.0f));
    room.setbassboost(juce::jlimit(0.0f, 0.5f, lowMult - 1.0f));

    // Modulation - Room uses less modulation than Hall
    room.setspin(spin * 0.3f);
    room.setwander(wander * 0.2f);

    // Ensure proper wet/dry settings
    room.setwet(0);  // 0dB
    room.setdryr(0); // Mute dry in processor
}

void DragonflyReverb::updatePlateReverb()
{
    // Match Dragonfly Plate algorithm parameters exactly
    plate.setRSFactor(size / 340.0f);  // Corrected scaling for Plate
    plate.setwidth(width / 100.0f);
    plate.setPreDelay(preDelay);

    // Diffusion settings for strev (plate algorithm)
    float diff = diffusion / 100.0f;
    plate.setidiffusion1(diff * 0.7f);
    plate.setidiffusion2(diff * 0.5f);  // Second stage diffusion
    // strev doesn't have setodiffusion methods

    // Core reverb settings
    plate.setrt60(decay);

    // Plate damping - characteristic metallic sound
    float dampValue = juce::jlimit(0.0f, 0.99f, 1.0f - (highCut / 20000.0f));
    plate.setdamp(dampValue * 0.7f);  // Less damping for brighter plate
    plate.setoutputdamp(dampValue * 0.5f);

    // Plate-specific parameters
    // strev doesn't have setbandwidth or settail methods
    // These characteristics are inherent to the algorithm

    // Modulation - plates have characteristic flutter
    plate.setspin(spin * 0.4f);
    plate.setwander(wander * 0.25f);

    // Ensure proper wet/dry settings
    plate.setwet(0);  // 0dB
    plate.setdryr(0); // Mute dry in processor
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

    // Process in chunks matching currentBufferSize (validated in prepare)
    int samplesProcessed = 0;
    while (samplesProcessed < numSamples)
    {
        int samplesToProcess = juce::jmin(static_cast<int>(currentBufferSize),
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
        int samplesToProcess = juce::jmin(static_cast<int>(currentBufferSize),
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
        int samplesToProcess = juce::jmin(static_cast<int>(currentBufferSize),
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
        int samplesToProcess = juce::jmin(static_cast<int>(currentBufferSize),
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