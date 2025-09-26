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
    DBG("DragonflyReverb constructor called");

    // Initialize mix levels matching Dragonfly defaults exactly
    dryLevel = 1.0f;     // 100% - full dry signal (Dragonfly default)
    earlyLevel = 0.5f;   // 50% - matching Dragonfly Hall
    lateLevel = 0.5f;    // 50% - matching Dragonfly Hall ("Wet" in UI)
    earlySend = 0.20f;   // 20% - exact Dragonfly Hall early send

    DBG("  Initial mix levels: dry=" << dryLevel << ", early=" << earlyLevel << ", late=" << lateLevel);

    // Dragonfly sets a default sample rate in constructor
    const double defaultSampleRate = 44100.0;
    DBG("  Set initial sample rate to " << defaultSampleRate);

    // Initialize input filters for Plate algorithm
    input_lpf_0.mute();
    input_lpf_1.mute();
    input_hpf_0.mute();
    input_hpf_1.mute();

    // Initialize EXACTLY like Dragonfly Hall DSP.cpp constructor
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setMuteOnChange(false);
    early.setdryr(0); // mute dry signal
    early.setwet(0); // 0dB
    early.setwidth(0.8);
    early.setLRDelay(0.3);
    early.setLRCrossApFreq(750, 4);
    early.setDiffusionApFreq(150, 4);
    early.setSampleRate(defaultSampleRate);

    // Initialize late (hall) exactly like Dragonfly
    hall.setMuteOnChange(false);
    hall.setwet(0); // 0dB
    hall.setdryr(0); // mute dry signal
    hall.setwidth(1.0);
    hall.setSampleRate(defaultSampleRate);

    // Initialize room and plate similarly
    room.setMuteOnChange(false);
    room.setwet(0);
    room.setdryr(0);
    room.setSampleRate(defaultSampleRate);

    // Initialize all three plate algorithms like Dragonfly Plate
    plateNrev.setMuteOnChange(false);
    plateNrev.setwet(0);  // 0dB
    plateNrev.setdryr(0);  // mute dry
    plateNrev.setSampleRate(defaultSampleRate);

    plateNrevb.setMuteOnChange(false);
    plateNrevb.setwet(0);  // 0dB
    plateNrevb.setdryr(0);  // mute dry
    plateNrevb.setSampleRate(defaultSampleRate);

    plateStrev.setMuteOnChange(false);
    plateStrev.setwet(0);  // 0dB
    plateStrev.setdryr(0);  // mute dry
    plateStrev.setSampleRate(defaultSampleRate);

    // DON'T mute the processors here! This clears their internal state
    // and they'll remain muted until prepare() is called
    // The processors are already initialized properly above

    DBG("  Constructor complete - reverb engines initialized");
}

void DragonflyReverb::prepare(double sr, int samplesPerBlock)
{
    DBG("DragonflyReverb::prepare - Input SampleRate: " << sr << ", Block size: " << samplesPerBlock);

    // Validate input parameters
    if (sr <= 0.0) sr = 44100.0;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;

    sampleRate = sr;
    blockSize = samplesPerBlock;
    DBG("  Final SampleRate: " << sampleRate << ", Block size: " << blockSize);

    // Validate and set buffer size to ensure we don't overflow
    currentBufferSize = juce::jmin(static_cast<size_t>(samplesPerBlock), MAX_BUFFER_SIZE);
    if (currentBufferSize < 1)
        currentBufferSize = DEFAULT_BUFFER_SIZE;
    DBG("  Current buffer size: " << currentBufferSize);

    // Clear all buffers before use with proper size validation
    const size_t totalBufferBytes = sizeof(float) * MAX_BUFFER_SIZE * 2; // 2 channels
    std::memset(earlyOutBuffer, 0, totalBufferBytes);
    std::memset(lateInBuffer, 0, totalBufferBytes);
    std::memset(lateOutBuffer, 0, totalBufferBytes);
    std::memset(filteredInputBuffer, 0, totalBufferBytes);

    // Set sample rates for all processors (like Dragonfly does)
    DBG("  Setting sample rates for all processors...");
    early.setSampleRate(sampleRate);
    hall.setSampleRate(sampleRate);
    room.setSampleRate(sampleRate);
    plateNrev.setSampleRate(sampleRate);
    plateNrevb.setSampleRate(sampleRate);
    plateStrev.setSampleRate(sampleRate);

    // Initialize input filters with sample rate
    // Room algorithm needs these filters
    setInputLPF(20000.0f);  // Default high cut
    setInputHPF(0.0f);       // Default low cut

    // Initialize all processors with current algorithm settings
    // This ensures they're in a known state
    DBG("  Loading early reflection preset...");
    early.loadPresetReflection(FV3_EARLYREF_PRESET_1);
    early.setMuteOnChange(false);  // Don't mute on change - we want continuous audio

    // Force initial size setup
    lastSetSize = -1.0f;  // Force size to be set
    DBG("  Setting initial size: " << size);
    setSize(size);        // This will now properly initialize the delay lines

    // Initialize with current parameters
    DBG("  Updating early reflections...");
    updateEarlyReflections();

    DBG("  Current algorithm: " << static_cast<int>(currentAlgorithm));
    switch (currentAlgorithm)
    {
        case Algorithm::Room:
            DBG("  Updating Room reverb...");
            updateRoomReverb();
            break;
        case Algorithm::Hall:
            DBG("  Updating Hall reverb...");
            updateHallReverb();
            break;
        case Algorithm::Plate:
            DBG("  Updating Plate reverb...");
            updatePlateReverb();
            break;
        default:
            DBG("  Early reflections only");
            break;
    }

    DBG("  Current mix levels - dry=" << dryLevel << ", late=" << lateLevel);
    DBG("  Early level=" << earlyLevel << ", late level=" << lateLevel << ", early send=" << earlySend);
    DBG("  Prepare complete!");

    // Don't call reset() here - it mutes everything!
}

void DragonflyReverb::reset()
{
    early.mute();
    hall.mute();
    room.mute();
    plateNrev.mute();
    plateNrevb.mute();
    plateStrev.mute();
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
        early.setRSFactor(size / 10.0f);  // Dragonfly Hall uses 10 for early

        // Update late reverb size based on algorithm - exact Dragonfly values
        switch (currentAlgorithm)
        {
            case Algorithm::Hall:
                hall.setRSFactor(size / 80.0f);  // Dragonfly Hall uses 80
                break;
            case Algorithm::Room:
                room.setRSFactor(size / 10.0f);  // Dragonfly Room uses 10
                break;
            case Algorithm::Plate:
                // Plate size affects decay time instead of RSFactor
                updatePlateReverb();  // Update plate with new size
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
    plateNrev.setwidth(w);
    plateNrevb.setwidth(w);
    plateStrev.setwidth(w);
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
        plateNrev.setPreDelay(0.0f);
        plateNrevb.setPreDelay(0.0f);
        plateStrev.setPreDelay(0.0f);
    }
    else
    {
        hall.setPreDelay(preDelay);
        room.setPreDelay(preDelay);
        plateNrev.setPreDelay(preDelay);
        plateNrevb.setPreDelay(preDelay);
        plateStrev.setPreDelay(preDelay);
    }
}

void DragonflyReverb::setDiffuse(float percent)
{
    diffusion = juce::jlimit(0.0f, 100.0f, percent);
    float diff = diffusion / 140.0f;  // Dragonfly Hall scales by 140

    hall.setidiffusion1(diff);
    hall.setapfeedback(diff);

    room.setidiffusion1(diff);
    // progenitor2 doesn't have setapfeedback
    room.setodiffusion1(diff);  // Use output diffusion instead

    // Only strev has diffusion settings
    plateStrev.setidiffusion1(diff);
    // strev doesn't have setapfeedback
    plateStrev.setidiffusion2(diff * 0.8f);  // Use second input diffusion
}

void DragonflyReverb::setDecay(float seconds)
{
    decay = juce::jlimit(0.1f, 10.0f, seconds);

    hall.setrt60(decay);
    room.setrt60(decay);
    plateNrev.setrt60(decay);
    plateNrevb.setrt60(decay);
    plateStrev.setrt60(decay);
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

    // Update input HPF for algorithms that need it (Room, Plate)
    setInputHPF(freq);
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
    // Only strev has setoutputdamp
    plateStrev.setoutputdamp(damp);

    // Update input LPF for algorithms that need it (Room, Plate)
    setInputLPF(freq);
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
    // Only strev has spin
    plateStrev.setspin(amount);
}

void DragonflyReverb::setWander(float amount)
{
    wander = amount;
    hall.setwander(amount);
    room.setwander(amount);  // progenitor2 has wander
    // Only strev has wander
    plateStrev.setwander(amount);
}

void DragonflyReverb::setModulation(float percent)
{
    // Hall-specific modulation depth - match Dragonfly Hall exactly
    float mod = percent == 0.0f ? 0.001f : percent / 100.0f;
    hall.setspinfactor(mod);
    hall.setlfofactor(mod);
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

void DragonflyReverb::setBoostLPF(float freq)
{
    // Room-specific boost LPF - Dragonfly Room uses setdamp2
    room.setdamp2(freq);
}

void DragonflyReverb::setDamping(float freq)
{
    // Plate-specific overall damping - match Dragonfly exactly
    // Dragonfly extends nrev/nrevb with custom setDampLpf, but we use standard setdamp

    // Convert frequency to normalized damping value for nrev/nrevb
    // Higher frequency = less damping (more highs pass through)
    float dampValue = 1.0f - (freq / 20000.0f);  // Normalize to 0-1 range

    // nrev and nrevb use setdamp (our version doesn't have setDampLpf)
    plateNrev.setdamp(dampValue);
    plateNrevb.setdamp(dampValue);

    // strev uses frequency directly for setdamp
    plateStrev.setdamp(freq);
    plateStrev.setoutputdamp(std::max(freq * 2.0f, 16000.0f));
}

//==============================================================================
// Update functions for each reverb type

void DragonflyReverb::updateEarlyReflections()
{
    // Match Dragonfly early reflections exactly
    early.setRSFactor(size / 10.0f);  // Dragonfly uses 10 for early

    // Width scaling depends on algorithm!
    if (currentAlgorithm == Algorithm::Room)
        early.setwidth(width / 120.0f);  // Room early uses /120
    else
        early.setwidth(width / 100.0f);  // Hall early uses /100

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
    hall.setRSFactor(size / 80.0f);  // Dragonfly Hall uses 80
    hall.setwidth(width / 100.0f);
    hall.setPreDelay(preDelay);

    // Diffusion settings - match Dragonfly Hall exactly
    float diff = diffusion / 140.0f;
    hall.setidiffusion1(diff);
    hall.setapfeedback(diff);
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

    // Modulation - match Dragonfly Hall exactly
    hall.setspin(spin);
    hall.setwander(wander);

    // Note: setspinfactor and setlfofactor are handled by setModulation()

    // Ensure proper wet/dry settings
    hall.setwet(0);  // 0dB
    hall.setdryr(0); // Mute dry in processor
}

void DragonflyReverb::updateRoomReverb()
{
    // Match Dragonfly Room algorithm parameters exactly
    room.setRSFactor(size / 10.0f);  // Dragonfly Room uses 10
    room.setwidth(width / 100.0f);   // Room late uses 100, NOT 120!
    room.setPreDelay(preDelay);

    // Diffusion settings for Progenitor2 - match Dragonfly Room
    float diff = diffusion / 120.0f;  // Room uses 120
    room.setidiffusion1(diff);
    room.setodiffusion1(diff);
    // progenitor2 doesn't have setidiffusion2/setodiffusion2

    // Core reverb settings
    room.setrt60(decay);
    room.setdccutfreq(lowCut);  // DC cut for rumble control

    // High frequency damping - match Dragonfly Room exactly
    // Dragonfly passes direct values to setdamp and setoutputdamp
    room.setdamp(highCut);
    room.setoutputdamp(highCut);

    // Bass boost - complex formula from Dragonfly Room
    // boost / 20.0 / pow(decay, 1.5) * (size / 10.0)
    float boostValue = lowMult / 20.0f / std::pow(decay, 1.5f) * (size / 10.0f);
    room.setbassboost(boostValue);

    // Note: setbassbw is not used in Dragonfly Room
    // Instead, setdamp2 is used for boost LPF parameter
    room.setdamp2(lowXover);  // Dragonfly uses setdamp2 for boost LPF

    // Modulation - match Dragonfly Room exactly
    room.setspin(spin);
    room.setspin2(std::sqrt(100.0f - (10.0f - spin) * (10.0f - spin)) / 2.0f);
    room.setwander(wander / 200.0f + 0.1f);
    room.setwander2(wander / 200.0f + 0.1f);

    // Ensure proper wet/dry settings
    room.setwet(0);  // 0dB
    room.setdryr(0); // Mute dry in processor
}

void DragonflyReverb::updatePlateReverb()
{
    // Match Dragonfly Plate algorithm parameters exactly
    // All three algorithms share most parameters

    // Common parameters for all plate algorithms
    float scaledWidth = width / 120.0f;  // Dragonfly Plate uses /120 for width

    // Update nrev algorithm
    plateNrev.setwidth(scaledWidth);
    plateNrev.setPreDelay(preDelay);
    plateNrev.setrt60(decay);
    plateNrev.setwet(0);  // 0dB
    plateNrev.setdryr(0); // Mute dry in processor

    // Update nrevb algorithm (used by Dark Plate preset)
    plateNrevb.setwidth(scaledWidth);
    plateNrevb.setPreDelay(preDelay);
    plateNrevb.setrt60(decay);
    plateNrevb.setwet(0);  // 0dB
    plateNrevb.setdryr(0); // Mute dry in processor

    // Update strev algorithm (Tank)
    plateStrev.setwidth(scaledWidth);
    plateStrev.setPreDelay(preDelay);
    plateStrev.setrt60(decay);
    // Note: setdamp should use the dampen parameter, not highCut!
    // highCut is for input filtering, dampen is for internal damping
    // This is handled in setDamping() which is called separately
    plateStrev.setspin(spin);  // strev has modulation
    plateStrev.setwander(wander);
    plateStrev.setwet(0);  // 0dB
    plateStrev.setdryr(0); // Mute dry in processor

    // Note: nrev and nrevb don't have spin/wander methods
    // But they DO have damping that needs to be set!
}

//==============================================================================
// Processing functions for each algorithm (matching Dragonfly's signal flow)

void DragonflyReverb::processHall(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    static int debugCounter = 0;
    debugCounter++;

    // Debug every 100 blocks
    bool debug = (debugCounter % 100 == 0);

    if (debug)
    {
        DBG("\n=== processHall - Frame " << debugCounter << " ===");
        DBG("  Samples: " << numSamples << ", Channels: " << numChannels);
        DBG("  Mix levels - dry=" << dryLevel << ", early=" << earlyLevel << ", late=" << lateLevel);
        DBG("  Early send: " << earlySend);
        float inputMag = buffer.getMagnitude(0, numSamples);
        DBG("  Input magnitude: " << inputMag);

        // Check if input has any signal
        if (inputMag < 0.0001f)
        {
            DBG("  WARNING: No input signal detected!");
        }
    }

    if (numChannels < 2 || numSamples <= 0) return;

    // Ensure pointers are valid
    float* inputL = buffer.getWritePointer(0);
    float* inputR = buffer.getWritePointer(1);
    if (!inputL || !inputR) return;

    // Process in chunks matching currentBufferSize (validated in prepare)
    int samplesProcessed = 0;
    while (samplesProcessed < numSamples)
    {
        int samplesToProcess = juce::jmin(static_cast<int>(currentBufferSize),
                                         numSamples - samplesProcessed);

        // Additional bounds check for safety
        samplesToProcess = juce::jmin(samplesToProcess, static_cast<int>(MAX_BUFFER_SIZE));
        if (samplesToProcess <= 0) break;

        // Clear buffers safely with size validation
        const size_t bytesToClear = sizeof(float) * static_cast<size_t>(samplesToProcess);
        const size_t maxBytes = sizeof(float) * MAX_BUFFER_SIZE;
        const size_t safeBytesToClear = juce::jmin(bytesToClear, maxBytes);

        std::memset(earlyOutBuffer[0], 0, safeBytesToClear);
        std::memset(earlyOutBuffer[1], 0, safeBytesToClear);
        std::memset(lateOutBuffer[0], 0, safeBytesToClear);
        std::memset(lateOutBuffer[1], 0, safeBytesToClear);

        // Process early reflections
        early.processreplace(
            inputL + samplesProcessed,
            inputR + samplesProcessed,
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Debug early output
        if (debug && samplesProcessed == 0)
        {
            float earlyMag = 0.0f;
            for (int i = 0; i < juce::jmin(10, samplesToProcess); ++i)
            {
                earlyMag += std::abs(earlyOutBuffer[0][i]) + std::abs(earlyOutBuffer[1][i]);
            }
            DBG("  Early output magnitude (first 10 samples): " << earlyMag);
            if (earlyMag < 0.0001f)
            {
                DBG("  WARNING: Early reflections producing no output!");
            }
        }

        // Prepare late reverb input (dry + early send)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            lateInBuffer[0][i] = inputL[samplesProcessed + i] +
                                 earlyOutBuffer[0][i] * earlySend;
            lateInBuffer[1][i] = inputR[samplesProcessed + i] +
                                 earlyOutBuffer[1][i] * earlySend;
        }

        // Debug late input
        if (debug && samplesProcessed == 0)
        {
            float lateInMag = 0.0f;
            for (int i = 0; i < samplesToProcess; ++i)
            {
                lateInMag += std::abs(lateInBuffer[0][i]) + std::abs(lateInBuffer[1][i]);
            }
            DBG("  Late input magnitude: " << lateInMag);
        }

        // Process late reverb
        hall.processreplace(
            lateInBuffer[0],
            lateInBuffer[1],
            lateOutBuffer[0],
            lateOutBuffer[1],
            samplesToProcess
        );

        // Debug late output
        if (debug && samplesProcessed == 0)
        {
            float lateOutMag = 0.0f;
            for (int i = 0; i < juce::jmin(10, samplesToProcess); ++i)
            {
                lateOutMag += std::abs(lateOutBuffer[0][i]) + std::abs(lateOutBuffer[1][i]);
            }
            DBG("  Late output magnitude (first 10 samples): " << lateOutMag);
            if (lateOutMag < 0.0001f)
            {
                DBG("  WARNING: Late reverb producing no output!");
                DBG("  Hall reverb state: wet=" << hall.getwet() << ", dry=" << hall.getdryr());
            }
        }

        // Mix output (dry + early + late)
        float mixedMag = 0.0f;
        for (int i = 0; i < samplesToProcess; ++i)
        {
            float dryL = inputL[samplesProcessed + i];
            float dryR = inputR[samplesProcessed + i];

            float outL = dryL * dryLevel +
                        earlyOutBuffer[0][i] * earlyLevel +
                        lateOutBuffer[0][i] * lateLevel;

            float outR = dryR * dryLevel +
                        earlyOutBuffer[1][i] * earlyLevel +
                        lateOutBuffer[1][i] * lateLevel;

            inputL[samplesProcessed + i] = outL;
            inputR[samplesProcessed + i] = outR;

            if (debug && i < 10)
            {
                mixedMag += std::abs(outL) + std::abs(outR);
            }
        }

        if (debug && samplesProcessed == 0)
        {
            DBG("  Mixed output magnitude (first 10 samples): " << mixedMag);

            // Check final output
            if (mixedMag < 0.0001f)
            {
                DBG("  ERROR: No output after mixing!");
                DBG("  Check: dryLevel=" << dryLevel << " should give dry signal");
                DBG("  Check: lateLevel=" << lateLevel << " should give reverb");
            }
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

        // Dragonfly Room processes FILTERED input for early reflections!
        for (int i = 0; i < samplesToProcess; ++i)
        {
            filteredInputBuffer[0][i] = input_lpf_0.process(input_hpf_0.process(inputL[samplesProcessed + i]));
            filteredInputBuffer[1][i] = input_lpf_1.process(input_hpf_1.process(inputR[samplesProcessed + i]));
        }

        // Process early reflections with filtered input
        early.processreplace(
            filteredInputBuffer[0],
            filteredInputBuffer[1],
            earlyOutBuffer[0],
            earlyOutBuffer[1],
            samplesToProcess
        );

        // Prepare late reverb input - use filtered input + early send
        for (int i = 0; i < samplesToProcess; ++i)
        {
            lateInBuffer[0][i] = filteredInputBuffer[0][i] +
                                 earlyOutBuffer[0][i] * earlySend;
            lateInBuffer[1][i] = filteredInputBuffer[1][i] +
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
        std::memset(lateOutBuffer[0], 0, sizeof(float) * samplesToProcess);
        std::memset(lateOutBuffer[1], 0, sizeof(float) * samplesToProcess);

        // Dragonfly Plate processes filtered input (matching Dragonfly exactly)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            filteredInputBuffer[0][i] = input_lpf_0.process(input_hpf_0.process(inputL[samplesProcessed + i]));
            filteredInputBuffer[1][i] = input_lpf_1.process(input_hpf_1.process(inputR[samplesProcessed + i]));
        }

        // Process plate reverb with filtered input using selected algorithm
        // Match Dragonfly Plate's algorithm selection
        switch (plateAlgorithm)
        {
            case PlateAlgorithm::Simple:
                plateNrev.processreplace(
                    filteredInputBuffer[0],
                    filteredInputBuffer[1],
                    lateOutBuffer[0],
                    lateOutBuffer[1],
                    samplesToProcess
                );
                break;

            case PlateAlgorithm::Nested:  // Dark Plate uses this
                plateNrevb.processreplace(
                    filteredInputBuffer[0],
                    filteredInputBuffer[1],
                    lateOutBuffer[0],
                    lateOutBuffer[1],
                    samplesToProcess
                );
                break;

            case PlateAlgorithm::Tank:
                plateStrev.processreplace(
                    filteredInputBuffer[0],
                    filteredInputBuffer[1],
                    lateOutBuffer[0],
                    lateOutBuffer[1],
                    samplesToProcess
                );
                break;
        }

        // Mix output - Plate uses only late reverb (no early)
        // In Dragonfly, "Wet Level" always corresponds to the late reverb level
        for (int i = 0; i < samplesToProcess; ++i)
        {
            float outL = inputL[samplesProcessed + i] * dryLevel +
                        lateOutBuffer[0][i] * lateLevel;  // Use lateLevel (which is set from wetLevel in UI)
            float outR = inputR[samplesProcessed + i] * dryLevel +
                        lateOutBuffer[1][i] * lateLevel;  // Use lateLevel (which is set from wetLevel in UI)

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

//==============================================================================
// Input filter helpers (matching Dragonfly Plate)

void DragonflyReverb::setInputLPF(float freq)
{
    if (freq < 0)
        freq = 0;
    else if (freq > sampleRate / 2.0)
        freq = sampleRate / 2.0;

    input_lpf_0.setLPF_BW(freq, sampleRate);
    input_lpf_1.setLPF_BW(freq, sampleRate);
}

void DragonflyReverb::setInputHPF(float freq)
{
    if (freq < 0)
        freq = 0;
    else if (freq > sampleRate / 2.0)
        freq = sampleRate / 2.0;

    input_hpf_0.setHPF_BW(freq, sampleRate);
    input_hpf_1.setHPF_BW(freq, sampleRate);
}
