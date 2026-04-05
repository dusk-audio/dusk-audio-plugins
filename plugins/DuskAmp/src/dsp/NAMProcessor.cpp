#include "NAMProcessor.h"
#include <cmath>
#include <filesystem>

#if DUSKAMP_NAM_SUPPORT
#include "NAM/wavenet.h"
#include "NAM/lstm.h"
#include "NAM/convnet.h"
#include "NAM/model_config.h"
#include "NAM/activations.h"

static void ensureNAMInitialized()
{
    static bool initialized = false;
    if (!initialized)
    {
        auto& registry = nam::ConfigParserRegistry::instance();
        if (!registry.has("WaveNet"))
            registry.registerParser("WaveNet", nam::wavenet::create_config);
        if (!registry.has("LSTM"))
            registry.registerParser("LSTM", nam::lstm::create_config);
        if (!registry.has("ConvNet"))
            registry.registerParser("ConvNet", nam::convnet::create_config);
        if (!registry.has("Linear"))
            registry.registerParser("Linear", nam::linear::create_config);
        // Critical: enable fast tanh approximation for WaveNet/LSTM performance.
        nam::activations::Activation::enable_fast_tanh();
        initialized = true;
    }
}
#endif

NAMProcessor::~NAMProcessor()
{
#if DUSKAMP_NAM_SUPPORT
    stagingReady_.store(false);
    stagedModel_.reset();
    activeModel_.reset();
#endif
}

void NAMProcessor::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    inputBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0);
    outputBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0);

#if DUSKAMP_NAM_SUPPORT
    ensureNAMInitialized();
    // If a model is already active, reset it for the new sample rate/block size.
    // This matches the official NAM plugin's OnReset() behavior.
    if (activeModel_)
        activeModel_->ResetAndPrewarm(sampleRate, maxBlockSize);
#endif
}

void NAMProcessor::process(float* buffer, int numSamples)
{
#if DUSKAMP_NAM_SUPPORT
    // Swap in staged model if ready. This mirrors the official NAM plugin's
    // _ApplyDSPStaging() which runs at the top of every ProcessBlock().
    if (stagingReady_.load(std::memory_order_acquire))
    {
        activeModel_ = std::move(stagedModel_);
        stagingReady_.store(false, std::memory_order_release);
    }

    if (activeModel_ == nullptr)
        return; // Pass through: buffer unchanged

    // Float → double with input gain
    for (int i = 0; i < numSamples; ++i)
        inputBuffer_[static_cast<size_t>(i)] = static_cast<double>(buffer[i]) * static_cast<double>(inputGain_);

    // NAM inference — single call with full buffer.
    double* inPtr = inputBuffer_.data();
    double* outPtr = outputBuffer_.data();
    activeModel_->process(&inPtr, &outPtr, numSamples);

    // Output gain + loudness compensation
    float loudnessComp = 1.0f;
    if (activeModel_->HasLoudness())
    {
        float dB = static_cast<float>(activeModel_->GetLoudness());
        loudnessComp = std::pow(10.0f, -dB / 20.0f);
    }

    float totalGain = outputGain_ * loudnessComp;
    for (int i = 0; i < numSamples; ++i)
    {
        float val = static_cast<float>(outputBuffer_[static_cast<size_t>(i)]) * totalGain;
        buffer[i] = std::isfinite(val) ? val : 0.0f;
    }
#else
    juce::ignoreUnused(buffer, numSamples);
#endif
}

void NAMProcessor::reset()
{
    std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0);
    std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0);
}

bool NAMProcessor::loadModel(const juce::File& file)
{
#if DUSKAMP_NAM_SUPPORT
    if (!file.existsAsFile())
    {
        lastError_ = "file not found: " + file.getFullPathName();
        return false;
    }

    try
    {
        ensureNAMInitialized();

        // Load model synchronously on message thread — this matches the official
        // NAM plugin's _StageModel() which does get_dsp + Reset synchronously.
        auto dspPath = std::filesystem::path(file.getFullPathName().toStdString());
        auto newModel = nam::get_dsp(dspPath);
        if (newModel == nullptr)
        {
            lastError_ = "get_dsp returned null";
            return false;
        }

        // Reset and prewarm with the host's actual sample rate and block size.
        // This pre-allocates all internal buffers so no allocation happens
        // during process().
        newModel->ResetAndPrewarm(sampleRate_, maxBlockSize_);

        // Stage for audio thread pickup
        stagedModel_ = std::move(newModel);
        stagingReady_.store(true, std::memory_order_release);

        modelName_ = file.getFileNameWithoutExtension();
        modelFile_ = file;
        modelLoaded_.store(true, std::memory_order_release);
        lastError_ = "";
        return true;
    }
    catch (const std::exception& e)
    {
        lastError_ = juce::String("exception: ") + e.what();
        return false;
    }
    catch (...)
    {
        lastError_ = "unknown exception";
        return false;
    }
#else
    juce::ignoreUnused(file);
    lastError_ = "NAM not compiled";
    return false;
#endif
}

void NAMProcessor::clearModel()
{
#if DUSKAMP_NAM_SUPPORT
    stagingReady_.store(false, std::memory_order_release);
    stagedModel_.reset();
#endif
    modelName_.clear();
    modelFile_ = juce::File();
    modelLoaded_.store(false, std::memory_order_release);
}

bool NAMProcessor::hasModel() const
{
    return modelLoaded_.load(std::memory_order_acquire);
}

juce::String NAMProcessor::getModelName() const { return modelName_; }
juce::File NAMProcessor::getModelFile() const { return modelFile_; }

void NAMProcessor::setInputLevel(float dB)
{
    inputGain_ = std::pow(10.0f, dB / 20.0f);
}

void NAMProcessor::setOutputLevel(float dB)
{
    outputGain_ = std::pow(10.0f, dB / 20.0f);
}
