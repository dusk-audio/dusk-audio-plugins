#pragma once

#include "AnalogEmulation/TubeEmulation.h"
#include "AnalogEmulation/DCBlocker.h"

class PreampDSP
{
public:
    enum class Channel { Clean = 0, Crunch = 1, Lead = 2 };

    void prepare (double sampleRate);
    void reset();
    void setGain (float gain01);
    void setChannel (Channel ch);
    void setBright (bool on);
    void setMarshallVoicing (bool on);   // V1A cathode-bypass-cap LF cut + jumper LM mix
    void process (float* buffer, int numSamples);

private:
    // 3 cascaded 12AX7 stages (Lead uses all 3, Crunch uses 2, Clean uses 1)
    static constexpr int kMaxStages = 3;
    AnalogEmulation::TubeEmulation stages_[kMaxStages];
    AnalogEmulation::DCBlocker interStageDC_[kMaxStages];

    Channel currentChannel_ = Channel::Crunch;
    float gain_ = 0.5f;
    bool bright_ = false;
    int numActiveStages_ = 2;
    double sampleRate_ = 44100.0;

    // Per-channel output makeup so Clean / Crunch / Lead deliver monotonically
    // rising level into the tone stack. Replaces the per-amp stageGain fudge in
    // PowerAmp that used to compensate for the preamp's net attenuation.
    float outputMakeup_ = 1.0f;

    // Coupling cap HPF between stages (simple one-pole)
    float couplingCapState_[kMaxStages] = {};
    float couplingCapCoeff_ = 0.995f;

    // Per-stage pre-emphasis + de-emphasis pair around the waveshaper. Aiken
    // trick: HPF-shelf before clipping pulls treble UP into saturation so HF
    // content survives the nonlinearity; complementary LPF-shelf after pulls
    // the boosted treble back DOWN to flat. Net effect on the dry path = flat;
    // on the distorted path = HF content reaches the user instead of being
    // folded into mud by the symmetric clip. This is the single biggest lever
    // for "tube saturated" vs "fizzy waveshaper" character.
    float preEmphHpfState_[kMaxStages] = {};   // HPF state per stage
    float deEmphLpfState_[kMaxStages] = {};    // LPF state per stage
    float preEmphCoeff_ = 0.0f;                // HPF feedback coeff (~400 Hz)
    float deEmphCoeff_ = 0.0f;                 // LPF coeff (mirror of preEmph)
    float preEmphMix_ = 0.35f;                 // amount of HPF mixed into stage input
    // Per-stage DC bias offset — real triode grids sit at -1.5 V to -2 V
    // relative to the cathode, so the operating point is asymmetric. Even
    // harmonics are generated AT ALL gain levels (not just at clipping),
    // which is part of why real preamps sound warm at "clean" settings.
    // Values halved from initial v1 — earlier values pushed too hard,
    // produced overload at Lead-channel drive levels.
    float stageDcOffset_[kMaxStages] = { 0.02f, 0.012f, 0.008f };

    // Bright cap boost (treble boost when bright switch on)
    float brightBoostState_ = 0.0f;
    float brightBoostCoeff_ = 0.0f;

    // Marshall V1A cathode-bypass cap (~7 dB low-shelf cut at 285 Hz) and
    // channel-jumper LM mix (50% Channel-II low-mid added to bright path,
    // simulating the Hendrix-trick of bridging both inputs).
    bool  marshallVoicing_      = false;
    float cathodeShelfState_    = 0.0f;
    float cathodeShelfCoeff_    = 0.0f;
    float jumperLpfState_       = 0.0f;
    float jumperLpfCoeff_       = 0.0f;

    // Dynamic-cathode-cap envelope follower. Real V1A bypass caps slowly
    // charge with sustained signal level; while charging, less LF is
    // shunted (cap is closer to "open" → bigger shelf cut); once charged,
    // the cap is closer to a short and the shelf flattens. Audible as
    // "compression on transients, bloom on sustain". Slow attack/release
    // (~50 ms / 200 ms) match real RC time constants of the bypass cap
    // through the cathode resistor + grid resistor of the next stage.
    float cathodeEnv_              = 0.0f;
    float cathodeEnvAttackCoeff_   = 0.0f;
    float cathodeEnvReleaseCoeff_  = 0.0f;

    void updateGainStaging();
    void updateCouplingCapCoeff();
    void updateBrightCoeff();
    void updateMarshallVoicingCoeffs();
    void updateCathodeEnvCoeffs();
    void updatePreEmphCoeffs();
};
