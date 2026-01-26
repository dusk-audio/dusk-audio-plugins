#include "NAMProcessor.h"
#include "dsp.h"
#include "get_dsp.h"
#include "factory_init.h"
#include <filesystem>

NAMProcessor::NAMProcessor()
{
    // Initialize NAM factories - required for static library linking
    nam::initializeFactories();
}

NAMProcessor::~NAMProcessor()
{
    namModel.reset();
    modelData.reset();
}

bool NAMProcessor::loadModel(const juce::File& modelFile)
{
    if (!modelFile.existsAsFile())
        return false;

    try
    {
        // Load the model
        auto newModelData = std::make_unique<nam::dspData>();
        std::filesystem::path modelPath(modelFile.getFullPathName().toStdString());
        auto newModel = nam::get_dsp(modelPath, *newModelData);

        if (!newModel)
            return false;

        // Swap in new model (thread-safe)
        modelLoaded.store(false);

        modelData = std::move(newModelData);
        namModel = std::move(newModel);

        // Get model sample rate
        modelSampleRate = namModel->GetExpectedSampleRate();
        if (modelSampleRate <= 0)
            modelSampleRate = 48000.0;

        // Check if resampling needed
        if (currentSampleRate > 0 && std::abs(currentSampleRate - modelSampleRate) > 1.0)
        {
            needsResampling = true;
            resampleRatio = modelSampleRate / currentSampleRate;
        }
        else
        {
            needsResampling = false;
            resampleRatio = 1.0;
        }

        // Extract metadata
        modelName = modelFile.getFileNameWithoutExtension();
        extractModelMetadata();

        // Calculate output normalization based on model loudness
        // NAM models report their loudness relative to a standardized input
        // We compensate to bring output to unity gain
        if (namModel->HasLoudness())
        {
            double loudness = namModel->GetLoudness();
            // Loudness is in dB - negative means quieter than reference
            // Apply inverse gain to normalize output
            outputNormalization = static_cast<float>(std::pow(10.0, -loudness / 20.0));
            // Clamp to reasonable range (max +30dB boost)
            outputNormalization = std::min(outputNormalization, 31.62f);
            DBG("NAM model loudness: " << loudness << " dB, normalization gain: " << outputNormalization);
        }
        else
        {
            outputNormalization = 1.0f;
        }

        // Prepare with current sample rate
        if (currentSampleRate > 0)
        {
            namModel->ResetAndPrewarm(modelSampleRate, maxBlockSize);
        }

        modelLoaded.store(true);
        return true;
    }
    catch (const std::exception& e)
    {
        DBG("NAM load error: " << e.what());
        return false;
    }
}

void NAMProcessor::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = samplesPerBlock;

    // Allocate processing buffers
    inputBuffer.resize(samplesPerBlock * 2);  // Extra space for resampling
    outputBuffer.resize(samplesPerBlock * 2);

    if (namModel)
    {
        // Check if resampling needed
        if (std::abs(currentSampleRate - modelSampleRate) > 1.0)
        {
            needsResampling = true;
            resampleRatio = modelSampleRate / currentSampleRate;
        }
        else
        {
            needsResampling = false;
            resampleRatio = 1.0;
        }

        namModel->ResetAndPrewarm(modelSampleRate, static_cast<int>(samplesPerBlock * resampleRatio) + 1);
    }
}

void NAMProcessor::process(juce::AudioBuffer<float>& buffer)
{
    if (!modelLoaded.load() || !namModel)
        return;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();

    // NAM processes mono - use left channel
    const float* input = buffer.getReadPointer(0);

    // Copy input to processing buffer
    std::copy(input, input + numSamples, inputBuffer.begin());

    // Process through NAM
    // Note: NAM processes in-place from input to output
    if (needsResampling)
    {
        // Simple linear interpolation resampling
        // For production use, a proper resampler would be better
        int resampledSize = static_cast<int>(numSamples * resampleRatio);
        resampledSize = std::min(resampledSize, static_cast<int>(outputBuffer.size()));

        std::vector<float> resampledInput(resampledSize);
        std::vector<float> resampledOutput(resampledSize);

        // Upsample input to model rate
        for (int i = 0; i < resampledSize; ++i)
        {
            float srcIdx = static_cast<float>(i) / static_cast<float>(resampleRatio);
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = std::min(idx0 + 1, numSamples - 1);
            float frac = srcIdx - static_cast<float>(idx0);
            resampledInput[i] = inputBuffer[idx0] * (1.0f - frac) + inputBuffer[idx1] * frac;
        }

        // Process at model sample rate
        // NAM API uses double-pointer for multi-channel support
        float* inputPtr = resampledInput.data();
        float* outputPtr = resampledOutput.data();
        namModel->process(&inputPtr, &outputPtr, resampledSize);

        // Downsample output back to host rate
        for (int i = 0; i < numSamples; ++i)
        {
            float srcIdx = static_cast<float>(i) * static_cast<float>(resampleRatio);
            int idx0 = static_cast<int>(srcIdx);
            int idx1 = std::min(idx0 + 1, resampledSize - 1);
            float frac = srcIdx - static_cast<float>(idx0);
            outputBuffer[i] = resampledOutput[idx0] * (1.0f - frac) + resampledOutput[idx1] * frac;
        }
    }
    else
    {
        // Process directly at native rate
        // NAM API uses double-pointer for multi-channel support
        float* inputPtr = inputBuffer.data();
        float* outputPtr = outputBuffer.data();
        namModel->process(&inputPtr, &outputPtr, numSamples);
    }

    // Apply output normalization based on model loudness
    if (outputNormalization != 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            outputBuffer[i] *= outputNormalization;
        }
    }

    // Copy output to all channels
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* output = buffer.getWritePointer(ch);
        std::copy(outputBuffer.begin(), outputBuffer.begin() + numSamples, output);
    }
}

void NAMProcessor::reset()
{
    if (namModel && currentSampleRate > 0)
    {
        namModel->ResetAndPrewarm(modelSampleRate, maxBlockSize);
    }
}

juce::String NAMProcessor::getModelName() const
{
    return modelName;
}

juce::String NAMProcessor::getModelInfo() const
{
    if (modelGear.isEmpty() && modelTone.isEmpty())
        return "";

    juce::String info;
    if (modelGear.isNotEmpty())
        info += "Gear: " + modelGear;
    if (modelTone.isNotEmpty())
    {
        if (info.isNotEmpty())
            info += " | ";
        info += "Tone: " + modelTone;
    }
    return info;
}

void NAMProcessor::extractModelMetadata()
{
    if (!modelData)
        return;

    try
    {
        // Extract metadata from JSON
        if (modelData->metadata.contains("name"))
        {
            modelName = juce::String(modelData->metadata["name"].get<std::string>());
        }

        if (modelData->metadata.contains("gear"))
        {
            modelGear = juce::String(modelData->metadata["gear"].get<std::string>());
        }

        if (modelData->metadata.contains("tone"))
        {
            modelTone = juce::String(modelData->metadata["tone"].get<std::string>());
        }
    }
    catch (...)
    {
        // Metadata extraction failed - use filename
    }
}
