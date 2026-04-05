#include "CabinetIR.h"

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

    dryBuffer_.setSize (1, maxBlockSize, false, true, true);

    filtersDirty_ = true;
}

void CabinetIR::process (juce::AudioBuffer<float>& buffer)
{
    if (filtersDirty_)
    {
        updateFilters();
        filtersDirty_ = false;
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

    // Apply normalization gain (if enabled) and post-cab EQ
    float* data = buffer.getWritePointer (0);
    float gain = normalize_ ? normGain_ : 1.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        data[i] *= gain;
        data[i] = hiCutFilter_.process (data[i]);
        data[i] = loCutFilter_.process (data[i]);
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
    hiCutFilter_.reset();
    loCutFilter_.reset();
    filtersDirty_ = true;
}

void CabinetIR::loadIR (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    // Compute normalization gain from IR file before loading into convolution.
    // This measures the RMS energy of the IR and scales so all IRs produce
    // similar output loudness, regardless of mic distance/type/level.
    normGain_ = 1.0f;
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader != nullptr)
        {
            auto numSamples = static_cast<int> (std::min (reader->lengthInSamples, (juce::int64) 48000));
            juce::AudioBuffer<float> irBuf (1, numSamples);
            reader->read (&irBuf, 0, numSamples, 0, true, false);

            // Measure RMS energy
            float rms = 0.0f;
            const float* data = irBuf.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
                rms += data[i] * data[i];
            rms = std::sqrt (rms / static_cast<float> (numSamples));

            // Target RMS ~0.01 (typical for normalized cab IRs)
            constexpr float targetRMS = 0.01f;
            if (rms > 1e-6f)
                normGain_ = targetRMS / rms;

            // Clamp to reasonable range (don't over-amplify quiet IRs)
            normGain_ = std::clamp (normGain_, 0.1f, 10.0f);
        }
    }

    // Reset convolution state before loading new IR to ensure clean swap.
    convolution_.reset();

    convolution_.loadImpulseResponse (file,
                                       juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::yes,
                                       0);

    loadedFile_ = file;
    loadedFileName_ = file.getFileNameWithoutExtension();
    irLoaded_ = true;
}

void CabinetIR::setNormalize (bool on)
{
    normalize_ = on;
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
        hiCutFilter_.b0 = ((1.0f - cosw0) * 0.5f) / a0;
        hiCutFilter_.b1 = (1.0f - cosw0) / a0;
        hiCutFilter_.b2 = hiCutFilter_.b0;
        hiCutFilter_.a1 = (-2.0f * cosw0) / a0;
        hiCutFilter_.a2 = (1.0f - alpha) / a0;
    }

    // Highpass (lo-cut) — Audio EQ Cookbook
    {
        float w0 = 2.0f * kPi * loCutFreq_ / static_cast<float> (sr);
        float cosw0 = std::cos (w0);
        float alpha = std::sin (w0) / (2.0f * 0.707f);
        float a0 = 1.0f + alpha;
        loCutFilter_.b0 = ((1.0f + cosw0) * 0.5f) / a0;
        loCutFilter_.b1 = -(1.0f + cosw0) / a0;
        loCutFilter_.b2 = loCutFilter_.b0;
        loCutFilter_.a1 = (-2.0f * cosw0) / a0;
        loCutFilter_.a2 = (1.0f - alpha) / a0;
    }
}
