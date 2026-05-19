#include "DattorroPlateVintage.h"
#include "DspUtils.h"

#include <algorithm>
#include <cstring>

void DattorroPlateVintage::MultiTapDelay::allocate (int requestedMax)
{
    const int size = DspUtils::nextPowerOf2 (std::max (requestedMax + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask     = size - 1;
    writePos = 0;
}

void DattorroPlateVintage::MultiTapDelay::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void DattorroPlateVintage::MultiTapDelay::write (float sample)
{
    buffer[static_cast<size_t> (writePos)] = sample;
    writePos = (writePos + 1) & mask;
}

float DattorroPlateVintage::MultiTapDelay::read (int delaySamples) const
{
    // After write(), writePos points to the NEXT slot. read(0) must
    // return the sample just written → subtract 1 here so tap0=0
    // means "current input sample".
    return buffer[static_cast<size_t> ((writePos - 1 - delaySamples) & mask)];
}

void DattorroPlateVintage::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    tank_.prepare (sampleRate, maxBlockSize);

    const float sr  = static_cast<float> (sampleRate);
    const float rateRatio = sr / 48000.0f;

    // Multi-tap predelay ring — sized for the longest tap plus a
    // sample-rate-scaled headroom factor so 96 kHz / 192 kHz sessions
    // get the same time-domain footprint.
    const int predelayMax = static_cast<int> (kPredelayMaxSamplesAt48k * rateRatio) + 32;
    predelayL_.allocate (predelayMax);
    predelayR_.allocate (predelayMax);
    predelayL_.clear();
    predelayR_.clear();

    // Cache sample-rate-scaled tap positions (constant between
    // prepare() calls — no need to recompute per-block).
    tapSamples_[0] = static_cast<int> (kTap0SamplesAt48k * rateRatio);
    tapSamples_[1] = static_cast<int> (kTap1SamplesAt48k * rateRatio);
    tapSamples_[2] = static_cast<int> (kTap2SamplesAt48k * rateRatio);
    tapSamples_[3] = static_cast<int> (kTap3SamplesAt48k * rateRatio);
    tapSamples_[4] = static_cast<int> (kTap4SamplesAt48k * rateRatio);

    // Pre-allocate scratch buffers with a safety oversize so the audio
    // thread never has to grow them. Hosts that exceed this safety size
    // are processed in chunks (see process()).
    constexpr int kSafetyBlockSize = 8192;
    const int reserve = std::max (maxBlockSize, kSafetyBlockSize);
    tankInL_.assign (static_cast<size_t> (reserve), 0.0f);
    tankInR_.assign (static_cast<size_t> (reserve), 0.0f);

    // Box-cut: 320 Hz, Q=2.0, -3.5 dB. Initial -6 dB Q=1.4 over-cut
    // 250-500 Hz steady-state by 4-6 dB (per measurement); narrower Q
    // + gentler gain hits the box peak without scooping the entire
    // low-mid region. -3 dB-points sit at ~270 Hz and ~380 Hz so the
    // hump is flattened while 200 Hz and 500 Hz stay close to flat.
    boxCut_.design     (320.0f, 2.0f, -3.5f, sr);
    boxCut_.reset();
    // Low-mid trim disabled (gain = 0 dB no-op). Designed only so it
    // resets cleanly; not invoked in process() while disabled.
    lowMidTrim_.design (200.0f, 0.7f,  0.0f, sr);
    lowMidTrim_.reset();
    lowMidTrimEnabled_ = false;

    prepared_ = true;
}

void DattorroPlateVintage::clearBuffers()
{
    tank_.clearBuffers();
    boxCut_.reset();
    lowMidTrim_.reset();
    predelayL_.clear();
    predelayR_.clear();
}

void DattorroPlateVintage::process (const float* inputL, const float* inputR,
                                    float* outputL, float* outputR, int numSamples)
{
    if (! prepared_) return;

    // Chunk the block to whatever scratch capacity we sized at prepare().
    // Audio thread never reallocates — if a host exceeds the safety
    // size, we process in two or more passes instead of growing.
    const int reserveSize = static_cast<int> (tankInL_.size());
    int processed = 0;
    while (processed < numSamples)
    {
        const int chunk = std::min (reserveSize, numSamples - processed);
        const float* const inL  = inputL  + processed;
        const float* const inR  = inputR  + processed;
        float* const       outL = outputL + processed;
        float* const       outR = outputR + processed;

        // ─── Multi-tap input injection ───
        // Spread dry input across 5 staggered predelay taps before the
        // Dattorro tank's intrinsic input diffuser. Pre-tank IR is no
        // longer a single spike; the tank's recirculation presents a
        // 20-110 ms energy plateau. kTapNorm normalises the tap sum to
        // unity DC gain so the tank's saturation calibration is
        // unchanged from the bare-tank case.
        for (int n = 0; n < chunk; ++n)
        {
            predelayL_.write (inL[n]);
            predelayR_.write (inR[n]);

            tankInL_[static_cast<size_t> (n)] = kTapNorm * (
                  kTap0Weight * predelayL_.read (tapSamples_[0])
                + kTap1Weight * predelayL_.read (tapSamples_[1])
                + kTap2Weight * predelayL_.read (tapSamples_[2])
                + kTap3Weight * predelayL_.read (tapSamples_[3])
                + kTap4Weight * predelayL_.read (tapSamples_[4]));
            tankInR_[static_cast<size_t> (n)] = kTapNorm * (
                  kTap0Weight * predelayR_.read (tapSamples_[0])
                + kTap1Weight * predelayR_.read (tapSamples_[1])
                + kTap2Weight * predelayR_.read (tapSamples_[2])
                + kTap3Weight * predelayR_.read (tapSamples_[3])
                + kTap4Weight * predelayR_.read (tapSamples_[4]));
        }

        tank_.process (tankInL_.data(), tankInR_.data(), outL, outR, chunk);

        // Post-tank stage: box-cut EQ + optional low-mid trim + linear
        // M/S widener for stereo-correlation lock.
        //
        // Output mixer: outL = a·L − b·R (mirror on R). Time-invariant
        // matrix → stab is preserved (any per-window correlation
        // pattern transforms uniformly). b nudges broadband corr
        // toward the Lex anchor's slight anti-correlation (≈ −0.07).
        constexpr float kOutMixA = 1.0f;
        constexpr float kOutMixB = 0.02f;
        for (int n = 0; n < chunk; ++n)
        {
            float l = boxCut_.processL (outL[n]);
            float r = boxCut_.processR (outR[n]);
            if (lowMidTrimEnabled_)
            {
                l = lowMidTrim_.processL (l);
                r = lowMidTrim_.processR (r);
            }
            outL[n] = kOutMixA * l - kOutMixB * r;
            outR[n] = kOutMixA * r - kOutMixB * l;
        }

        processed += chunk;
    }
}

void DattorroPlateVintage::setDecayTime         (float v) { tank_.setDecayTime         (v); }
void DattorroPlateVintage::setSize              (float v) { tank_.setSize              (v); }
void DattorroPlateVintage::setBassMultiply      (float v) { tank_.setBassMultiply      (v); }
void DattorroPlateVintage::setMidMultiply       (float v) { tank_.setMidMultiply       (v); }
void DattorroPlateVintage::setTrebleMultiply    (float v) { tank_.setTrebleMultiply    (v); }
void DattorroPlateVintage::setCrossoverFreq     (float v) { tank_.setCrossoverFreq     (v); }
void DattorroPlateVintage::setHighCrossoverFreq (float v) { tank_.setHighCrossoverFreq (v); }
void DattorroPlateVintage::setSaturation        (float v) { tank_.setSaturation        (v); }
void DattorroPlateVintage::setModDepth          (float v) { tank_.setModDepth          (v); }
void DattorroPlateVintage::setModRate           (float v) { tank_.setModRate           (v); }
void DattorroPlateVintage::setTankDiffusion     (float v) { tank_.setTankDiffusion     (v); }
void DattorroPlateVintage::setFreeze            (bool  v) { tank_.setFreeze            (v); }
