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
    // Hard runtime guard: inputBuffer_/outputBuffer_ are sized to
    // maxBlockSize_ in prepare(). A host that sends a larger block than it
    // declared (or calls process before prepare) would overrun them. The
    // jassert catches it in debug; this guard prevents the overrun in
    // Release by passing the block through dry rather than corrupting heap.
    jassert(numSamples <= maxBlockSize_);
    if (numSamples > maxBlockSize_ || numSamples <= 0)
        return;

#if DUSKAMP_NAM_SUPPORT
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

    // Loudness normalization to the NAM-standard −18 dB reference. A model's
    // GetLoudness() is its output level (dB) for a standardized input; we
    // bring it to kTargetLoudnessDb so every profile lands at a consistent
    // level — the same target the reference NeuralAmpModeler plugin uses, so
    // a profile sounds equally loud in DuskAmp as in any other NAM host, and
    // lands ≈ the DSP path's nominal output.
    //
    // The prior code used `10^(-loudness/20)` (an implicit 0 dB target),
    // which boosted typical −11..−19 dB profiles by +11..+19 dB — slamming
    // the output limiter (a high-gain capture measured −3.5 dBFS RMS).
    // Models with NO loudness metadata pass through raw (NAM convention —
    // user trims OUTPUT by ear).
    constexpr float kTargetLoudnessDb = -18.0f;
    float loudnessComp = 1.0f;
    if (activeModel_->HasLoudness())
    {
        float dB = static_cast<float>(activeModel_->GetLoudness());
        loudnessComp = std::pow(10.0f, (kTargetLoudnessDb - dB) / 20.0f);
        // Guard against corrupt / absurd loudness metadata: a wildly
        // negative value would otherwise produce a multi-thousand-x boost
        // and slam the chain. Clamp to ±18 dB of make-up — wider than any
        // legit profile (observed loudness range −25.5..−8 dB).
        loudnessComp = std::clamp(loudnessComp, 0.125f, 8.0f);
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

        modelExpectedSR_ = newModel->GetExpectedSampleRate();

        pendingModel_ = std::move(newModel);
        pendingReady_.store(true, std::memory_order_release);

        modelName_ = file.getFileNameWithoutExtension();
        modelFile_ = file;
        modelLoaded_.store(true, std::memory_order_release);

        // Sample-rate mismatch warning. Most NAM models are 48 kHz;
        // running through a 96 k or 44.1 k host produces aliased output
        // because the network's internal time constants assume the
        // training SR. -1 == unknown — skip the warning.
        if (modelExpectedSR_ > 0.0
            && std::abs (modelExpectedSR_ - sampleRate_) > 1.0)
        {
            lastError_ = "model trained at "
                       + juce::String (modelExpectedSR_ / 1000.0, 1)
                       + " kHz; host is "
                       + juce::String (sampleRate_ / 1000.0, 1)
                       + " kHz — output may alias";
        }
        else
        {
            lastError_.clear();
        }
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
    pendingReady_.store(false, std::memory_order_release);
    pendingModel_.reset();
    // Don't reset activeModel_ directly — the audio thread may be using it.
    // It will be retired on next loadModel or prepare.
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
