#include "NAMProcessor.h"
#include <cmath>
#include <filesystem>

#if DUSKAMP_NAM_SUPPORT
// Force the linker to keep NAM architecture translation units.
// Each .cpp has a static ConfigParserHelper that self-registers its
// architecture (WaveNet, LSTM, ConvNet, Linear) at load time.
// Without explicit references, the linker strips them as "unused".
#include "NAM/wavenet.h"
#include "NAM/lstm.h"
#include "NAM/convnet.h"

// Ensure NAM architecture parsers are registered.
#include "NAM/model_config.h"

static void ensureNAMParsersRegistered()
{
    auto& registry = nam::ConfigParserRegistry::instance();
    // Only register if not already present (idempotent)
    if (!registry.has("WaveNet"))
        registry.registerParser("WaveNet", nam::wavenet::create_config);
    if (!registry.has("LSTM"))
        registry.registerParser("LSTM", nam::lstm::create_config);
    if (!registry.has("ConvNet"))
        registry.registerParser("ConvNet", nam::convnet::create_config);
    if (!registry.has("Linear"))
        registry.registerParser("Linear", nam::linear::create_config);
}
#endif

NAMProcessor::~NAMProcessor()
{
#if DUSKAMP_NAM_SUPPORT
    pendingReady_.store(false);
    pendingClear_.store(false);
    pendingModel_.reset();
    activeModel_.reset();
    retiredModel_.reset();
#endif
}

void NAMProcessor::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    inputBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0);
    outputBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0);

#if DUSKAMP_NAM_SUPPORT
    retiredModel_.reset();
#endif
}

void NAMProcessor::process(float* buffer, int numSamples)
{
    jassert(numSamples <= maxBlockSize_);

#if DUSKAMP_NAM_SUPPORT
    // Handle pending clear (set by clearModel() on message thread)
    if (pendingClear_.load(std::memory_order_acquire))
    {
        retiredModel_ = std::move(activeModel_);
        pendingModel_.reset();
        pendingReady_.store(false, std::memory_order_relaxed);
        pendingClear_.store(false, std::memory_order_release);
    }

    // Swap in pending model if ready (atomic flag — no data race)
    if (pendingReady_.load(std::memory_order_acquire))
    {
        // Retire the old model (deferred delete — safe because we're
        // between process calls on the audio thread)
        retiredModel_ = std::move(activeModel_);
        activeModel_ = std::move(pendingModel_);
        pendingReady_.store(false, std::memory_order_release);
    }

    if (activeModel_ == nullptr)
        return; // Pass through: buffer unchanged

    // Float → double with input gain
    for (int i = 0; i < numSamples; ++i)
        inputBuffer_[static_cast<size_t>(i)] = static_cast<double>(buffer[i]) * static_cast<double>(inputGain_);

    // NAM process() takes double** (pointer-to-pointer, single channel)
    double* inPtr = inputBuffer_.data();
    double* outPtr = outputBuffer_.data();
    activeModel_->process(&inPtr, &outPtr, numSamples);

    // Output gain + optional loudness compensation, double → float
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
        ensureNAMParsersRegistered();

        lastError_ = "parsing...";
        auto path = std::filesystem::path(file.getFullPathName().toStdString());
        auto newModel = nam::get_dsp(path);
        if (newModel == nullptr)
        {
            lastError_ = "get_dsp returned null";
            return false;
        }

        lastError_ = "prewarming...";
        newModel->prewarm();

        pendingModel_ = std::move(newModel);
        pendingReady_.store(true, std::memory_order_release);

        modelName_ = file.getFileNameWithoutExtension();
        modelFile_ = file;
        modelLoaded_.store(true, std::memory_order_release);

        lastError_ = ""; // success
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
    return false;
#endif
}

void NAMProcessor::clearModel()
{
#if DUSKAMP_NAM_SUPPORT
    // Signal the audio thread to safely clear pendingModel_ and activeModel_.
    // Don't touch pendingModel_ directly here — the audio thread may be moving it.
    pendingClear_.store(true, std::memory_order_release);
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
