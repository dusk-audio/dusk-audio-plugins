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

    // Apply post-cab EQ (hi-cut and lo-cut) — mono processing, then apply
    // the optional loudness-match makeup to the wet signal (only when enabled)
    // so the dry/wet mix still crossfades against an un-boosted dry.
    float* data = buffer.getWritePointer (0);
    const float gain = normalize_ ? normalizeMakeup_ : 1.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        data[i] = hiCutFilter_.process (data[i]);
        data[i] = loCutFilter_.process (data[i]);
        data[i] *= gain;
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

    convolution_.loadImpulseResponse (file,
                                       juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::yes,
                                       0);

    loadedFile_ = file;
    loadedFileName_ = file.getFileNameWithoutExtension();
    irLoaded_ = true;

    // Loudness-match makeup: measure the cab's actual RMS attenuation by
    // running pink noise through a SEPARATE juce::dsp::Convolution instance
    // loaded with the same IR. Doing it through the real JUCE path (rather
    // than re-implementing trim+normalise+convolve offline) guarantees the
    // makeup matches what the live convolution_ in process() actually does.
    measureNormalizeMakeup (file);
}

void CabinetIR::measureNormalizeMakeup (const juce::File& file)
{
    if (currentSpec_.sampleRate <= 0.0)
        return; // not yet prepared — leave makeup at default 1.0

    juce::dsp::ConvolutionMessageQueue mq;
    juce::dsp::Convolution measureConv { juce::dsp::Convolution::NonUniform { 256 }, mq };

    // Same pink noise the audit's CAB_LOUDNESS test uses (so any spectral
    // difference between offline measurement and live test path is ruled out).
    const int kTestLen = static_cast<int> (currentSpec_.sampleRate * 0.5); // 22050 @ 44.1k
    constexpr float kTestScale = 0.15f;

    // Match the live convolution_'s prepare exactly so JUCE's partition
    // layout + zero-latency engine path are identical to what the user's
    // process() will hit at runtime. JUCE's NonUniform behaviour does
    // change with maxBlockSize (different head/tail engine sizing).
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = currentSpec_.sampleRate;
    spec.maximumBlockSize = currentSpec_.maximumBlockSize;
    spec.numChannels      = 1;
    measureConv.prepare (spec);

    // JUCE installs the new engine at the start of process(), not when the
    // background load finishes — getCurrentIRSize() reflects whatever was
    // current the last time process() ran (a fresh prepared instance shows
    // a unit-impulse placeholder of size 1). So we drive zero-buffer
    // process() calls until either the engine swaps (size changes) or we
    // give up.
    const int placeholderSize = measureConv.getCurrentIRSize();
    measureConv.loadImpulseResponse (file,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     0);

    juce::AudioBuffer<float> warmBuf (1, 2048);
    auto irSamples = placeholderSize;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        warmBuf.clear();
        juce::dsp::AudioBlock<float> b (warmBuf);
        juce::dsp::ProcessContextReplacing<float> ctx (b);
        measureConv.process (ctx);
        irSamples = measureConv.getCurrentIRSize();
        if (irSamples != placeholderSize)
            break;
        juce::Thread::sleep (5);
    }
    if (irSamples == placeholderSize)
        return; // load timed out

    std::vector<float> pinkIn (static_cast<size_t> (kTestLen));
    std::vector<float> pinkOut (static_cast<size_t> (kTestLen));
    {
        juce::Random rng { 0xCAFEC0DE };
        float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < kTestLen; ++i)
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99886f * b0 + w * 0.0555179f;
            b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f;
            pinkIn[static_cast<size_t> (i)] = (b0 + b1 + b2 + w * 0.1848f) * kTestScale;
        }
    }

    // Process pink through the FULL pipeline (convolution + post-cab EQ
    // biquads at the user's current cut frequencies). Otherwise the measured
    // ratio reflects only the convolution and misses the few-dB attenuation
    // the user actually hears, leading to under-correction.
    if (filtersDirty_)
    {
        updateFilters();
        filtersDirty_ = false;
    }
    Biquad measHi = hiCutFilter_; // copy coeffs (state starts fresh)
    Biquad measLo = loCutFilter_;
    measHi.reset();
    measLo.reset();

    const int blockSize = std::min (static_cast<int> (currentSpec_.maximumBlockSize), kTestLen);
    juce::AudioBuffer<float> blockBuf (1, blockSize);
    for (int total = 0; total < kTestLen; total += blockSize)
    {
        const int n = std::min (blockSize, kTestLen - total);
        blockBuf.copyFrom (0, 0, pinkIn.data() + total, n);
        juce::dsp::AudioBlock<float> block (blockBuf);
        auto sub = block.getSubBlock (0, static_cast<size_t> (n));
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        measureConv.process (ctx);
        float* p = blockBuf.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            p[i] = measHi.process (p[i]);
            p[i] = measLo.process (p[i]);
        }
        std::copy_n (blockBuf.getReadPointer (0), n, pinkOut.data() + total);
    }

    // Skip the IR-length transient + a small head latency margin so RMS
    // averages over the steady-state portion of the convolution output.
    const int skip = std::min (irSamples + 256, kTestLen / 4);
    double inSumSq = 0.0, outSumSq = 0.0;
    for (int i = skip; i < kTestLen; ++i)
    {
        const double p = pinkIn[static_cast<size_t> (i)];
        const double o = pinkOut[static_cast<size_t> (i)];
        inSumSq  += p * p;
        outSumSq += o * o;
    }
    const double inRms  = std::sqrt (inSumSq);
    const double outRms = std::sqrt (outSumSq);
    if (outRms > 1.0e-9 && inRms > 1.0e-9)
        normalizeMakeup_ = static_cast<float> (inRms / outRms);
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

void CabinetIR::setNormalize (bool on)
{
    normalize_ = on;
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
