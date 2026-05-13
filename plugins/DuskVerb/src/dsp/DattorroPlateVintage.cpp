#include "DattorroPlateVintage.h"

void DattorroPlateVintage::prepare (double sampleRate, int maxBlockSize)
{
    tank_.prepare (sampleRate, maxBlockSize);

    const float sr = static_cast<float> (sampleRate);
    // Box-cut: 320 Hz, Q=2.0, -3.5 dB. Initial -6 dB Q=1.4 over-cut
    // 250-500 Hz steady-state by 4-6 dB (per measurement); narrower Q
    // + gentler gain hits the box peak without scooping the entire
    // low-mid region. -3 dB-points sit at ~270 Hz and ~380 Hz so the
    // hump is flattened while 200 Hz and 500 Hz stay close to flat.
    boxCut_.design     (320.0f, 2.0f, -3.5f, sr);
    // Low-mid trim disabled — the broad shelf was double-cutting low
    // mids on top of the box-cut, pulling 200 Hz down 1 dB further
    // than Lex. Set to 0 dB (no-op) until measurement justifies it.
    lowMidTrim_.design (200.0f, 0.7f,  0.0f, sr);
    boxCut_.reset();
    lowMidTrim_.reset();

    prepared_ = true;
}

void DattorroPlateVintage::clearBuffers()
{
    tank_.clearBuffers();
    boxCut_.reset();
    lowMidTrim_.reset();
}

void DattorroPlateVintage::process (const float* inputL, const float* inputR,
                                    float* outputL, float* outputR, int numSamples)
{
    if (! prepared_) return;
    tank_.process (inputL, inputR, outputL, outputR, numSamples);
    for (int n = 0; n < numSamples; ++n)
    {
        float l = outputL[n];
        float r = outputR[n];
        l = boxCut_.processL (l);
        r = boxCut_.processR (r);
        l = lowMidTrim_.processL (l);
        r = lowMidTrim_.processR (r);
        outputL[n] = l;
        outputR[n] = r;
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
