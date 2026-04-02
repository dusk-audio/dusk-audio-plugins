#include "CabinetIR.h"
#include <juce_audio_formats/juce_audio_formats.h>

CabinetIR::CabinetIR()
    : convolution_ (juce::dsp::Convolution::NonUniform { 256 }, messageQueue_)
{
}

void CabinetIR::prepare (double sampleRate, int maxBlockSize)
{
    currentSpec_.sampleRate = sampleRate;
    currentSpec_.maximumBlockSize = static_cast<juce::uint32> (maxBlockSize);
    currentSpec_.numChannels = 1; // mono cabinet processing

    convolution_.prepare (currentSpec_);

    dryBuffer_.setSize (kMaxChannels, maxBlockSize, false, true, true);

    filtersDirty_ = true;
}

void CabinetIR::process (juce::AudioBuffer<float>& buffer)
{
    if (filtersDirty_)
    {
        // Snapshot current filters as "old" for crossfade
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hiCutFilterOld_[ch] = hiCutFilter_[ch];
            loCutFilterOld_[ch] = loCutFilter_[ch];
        }

        updateFilters();
        filtersDirty_ = false;
        filterCrossfadeRemaining_ = kFilterCrossfadeSamples;
    }

    if (! enabled_ || ! irLoaded_)
        return;

    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();

    // Store dry signal for mix
    if (mix_ < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer_.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // Run convolution
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    convolution_.process (context);

    // Apply post-cab EQ (hi-cut and lo-cut) per channel
    // Crossfade between old and new filter states to prevent clicks
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            if (filterCrossfadeRemaining_ > 0)
            {
                float fade = static_cast<float> (filterCrossfadeRemaining_) / static_cast<float> (kFilterCrossfadeSamples);
                float newOut = hiCutFilter_[ch].process (data[i]);
                float oldOut = hiCutFilterOld_[ch].process (data[i]);
                data[i] = oldOut * fade + newOut * (1.0f - fade);

                newOut = loCutFilter_[ch].process (data[i]);
                oldOut = loCutFilterOld_[ch].process (data[i]);
                data[i] = oldOut * fade + newOut * (1.0f - fade);

                if (ch == numChannels - 1)
                    --filterCrossfadeRemaining_;
            }
            else
            {
                data[i] = hiCutFilter_[ch].process (data[i]);
                data[i] = loCutFilter_[ch].process (data[i]);
            }
        }
    }

    // Apply autogain normalization (before dry/wet mix so it only affects wet)
    if (autoGainEnabled_ && autoGainLinear_ != 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] *= autoGainLinear_;
        }
    }

    // Apply dry/wet mix
    if (mix_ < 1.0f)
    {
        float wet = mix_;
        float dry = 1.0f - mix_;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wetData = buffer.getWritePointer (ch);
            const float* dryData = dryBuffer_.getReadPointer (std::min (ch, dryBuffer_.getNumChannels() - 1));

            for (int i = 0; i < numSamples; ++i)
                wetData[i] = dryData[i] * dry + wetData[i] * wet;
        }
    }
}

void CabinetIR::reset()
{
    convolution_.reset();
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        hiCutFilter_[ch].reset();
        loCutFilter_[ch].reset();
        hiCutFilterOld_[ch].reset();
        loCutFilterOld_[ch].reset();
    }
    filterCrossfadeRemaining_ = 0;
    filtersDirty_ = true;
}

void CabinetIR::loadIR (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    convolution_.loadImpulseResponse (file,
                                       juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::yes,
                                       0);

    loadedFile_ = file;
    loadedFileName_ = file.getFileNameWithoutExtension();
    irLoaded_ = true;
    isFactoryIR_ = false;

    computeAutoGainFromFile (file);
}

void CabinetIR::loadIRFromBinaryData (const void* data, size_t sizeInBytes, const juce::String& name)
{
    convolution_.loadImpulseResponse (data, sizeInBytes,
                                       juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::yes,
                                       0);

    loadedFile_ = juce::File();
    loadedFileName_ = name;
    irLoaded_ = true;
    isFactoryIR_ = true;

    computeAutoGainFromBinaryData (data, sizeInBytes);
}

void CabinetIR::setEnabled (bool on)
{
    enabled_ = on;
}

void CabinetIR::setMix (float mix01)
{
    mix_ = std::clamp (mix01, 0.0f, 1.0f);
}

void CabinetIR::setHiCut (float hz)
{
    hiCutFreq_ = std::clamp (hz, 1000.0f, 20000.0f);
    filtersDirty_ = true;
}

void CabinetIR::setLoCut (float hz)
{
    loCutFreq_ = std::clamp (hz, 20.0f, 500.0f);
    filtersDirty_ = true;
}

void CabinetIR::setAutoGain (bool on)
{
    autoGainEnabled_ = on;
}

void CabinetIR::updateFilters()
{
    double sr = currentSpec_.sampleRate;
    if (sr <= 0.0)
        return;

    static constexpr float kPi = 3.14159265358979323846f;

    // Lowpass (hi-cut) — Audio EQ Cookbook
    {
        float w0 = 2.0f * kPi * hiCutFreq_ / static_cast<float> (sr);
        float cosw0 = std::cos (w0);
        float alpha = std::sin (w0) / (2.0f * 0.707f);
        float a0 = 1.0f + alpha;
        float b0 = ((1.0f - cosw0) * 0.5f) / a0;
        float b1 = (1.0f - cosw0) / a0;
        float a1 = (-2.0f * cosw0) / a0;
        float a2 = (1.0f - alpha) / a0;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hiCutFilter_[ch].b0 = b0;
            hiCutFilter_[ch].b1 = b1;
            hiCutFilter_[ch].b2 = b0;
            hiCutFilter_[ch].a1 = a1;
            hiCutFilter_[ch].a2 = a2;
        }
    }

    // Highpass (lo-cut) — Audio EQ Cookbook
    {
        float w0 = 2.0f * kPi * loCutFreq_ / static_cast<float> (sr);
        float cosw0 = std::cos (w0);
        float alpha = std::sin (w0) / (2.0f * 0.707f);
        float a0 = 1.0f + alpha;
        float b0 = ((1.0f + cosw0) * 0.5f) / a0;
        float b1 = -(1.0f + cosw0) / a0;
        float a1 = (-2.0f * cosw0) / a0;
        float a2 = (1.0f - alpha) / a0;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            loCutFilter_[ch].b0 = b0;
            loCutFilter_[ch].b1 = b1;
            loCutFilter_[ch].b2 = b0;
            loCutFilter_[ch].a1 = a1;
            loCutFilter_[ch].a2 = a2;
        }
    }
}

// =========================================================================
// Autogain: measure IR energy and compute normalization gain
// =========================================================================

void CabinetIR::buildThumbnail (const float* irData, int numSamples)
{
    thumbnail_.fill (0.0f);
    thumbnailReady_ = false;

    if (irData == nullptr || numSamples <= 0)
        return;

    // Downsample to kThumbnailSize peak values
    float maxPeak = 0.0f;
    int samplesPerBin = std::max (1, numSamples / kThumbnailSize);

    for (int bin = 0; bin < kThumbnailSize; ++bin)
    {
        int start = bin * numSamples / kThumbnailSize;
        int end = std::min (start + samplesPerBin, numSamples);
        float peak = 0.0f;
        for (int i = start; i < end; ++i)
            peak = std::max (peak, std::abs (irData[i]));
        thumbnail_[static_cast<size_t> (bin)] = peak;
        maxPeak = std::max (maxPeak, peak);
    }

    // Normalize to 0-1
    if (maxPeak > 1e-6f)
    {
        float invMax = 1.0f / maxPeak;
        for (auto& v : thumbnail_)
            v *= invMax;
    }

    thumbnailReady_ = true;
}

void CabinetIR::computeAutoGain (const float* irData, int numSamples)
{
    if (numSamples <= 0 || irData == nullptr)
    {
        autoGainDB_ = 0.0f;
        autoGainLinear_ = 1.0f;
        return;
    }

    // Compute RMS of the IR (energy-based normalization)
    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sumSq += static_cast<double> (irData[i]) * static_cast<double> (irData[i]);

    double rms = std::sqrt (sumSq / static_cast<double> (numSamples));

    if (rms < 1e-10)
    {
        autoGainDB_ = 0.0f;
        autoGainLinear_ = 1.0f;
        return;
    }

    // Target RMS for cabinet IRs: -18 dBFS is a reasonable reference level.
    // Most well-calibrated IRs have RMS around -18 to -24 dBFS.
    static constexpr double kTargetRMS = 0.126;  // ~-18 dBFS
    double gainLinear = kTargetRMS / rms;

    // Clamp to reasonable range: -12 to +12 dB
    gainLinear = std::clamp (gainLinear, 0.25, 4.0);

    autoGainDB_ = static_cast<float> (20.0 * std::log10 (gainLinear));
    autoGainLinear_ = static_cast<float> (gainLinear);
}

void CabinetIR::computeAutoGainFromFile (const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        autoGainDB_ = 0.0f;
        autoGainLinear_ = 1.0f;
        thumbnailReady_ = false;
        return;
    }

    int numSamples = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> irBuffer (1, numSamples);
    reader->read (&irBuffer, 0, numSamples, 0, true, false);

    computeAutoGain (irBuffer.getReadPointer (0), numSamples);
    buildThumbnail (irBuffer.getReadPointer (0), numSamples);
}

void CabinetIR::computeAutoGainFromBinaryData (const void* data, size_t sizeInBytes)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto stream = std::make_unique<juce::MemoryInputStream> (data, sizeInBytes, false);
    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager.createReaderFor (std::move (stream)));

    if (reader == nullptr)
    {
        autoGainDB_ = 0.0f;
        autoGainLinear_ = 1.0f;
        thumbnailReady_ = false;
        return;
    }

    int numSamples = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> irBuffer (1, numSamples);
    reader->read (&irBuffer, 0, numSamples, 0, true, false);

    computeAutoGain (irBuffer.getReadPointer (0), numSamples);
    buildThumbnail (irBuffer.getReadPointer (0), numSamples);
}
