// PreampModel.h — Amp-specific preamp topologies
//
// Each real amplifier has a unique preamp circuit that defines its character.
// Instead of the old approach (1/2/3 identical 12AX7 stages), each model
// implements the actual gain staging, coupling cap values, cathode bypass
// behavior, and bright cap of the real amp.
//
// All models use the shared TubeEmulation from AnalogEmulation/ but configure
// it with amp-specific parameters.

#pragma once

#include "AnalogEmulation/TubeEmulation.h"
#include "AnalogEmulation/DCBlocker.h"
#include <memory>

enum class AmpType
{
    Fender  = 0,  // Fender Twin Reverb — clean headroom, cathode follower compression
    Marshall = 1, // Marshall Plexi 1959 — cascaded gain, tight bass, aggressive
    Vox     = 2   // Vox AC30 Top Boost — cathode follower sag, chimey breakup
};

class PreampModel
{
public:
    virtual ~PreampModel() = default;

    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    virtual void process (float* buffer, int numSamples) = 0;

    virtual void setGain (float gain01) = 0;
    virtual void setBright (bool on) = 0;

    virtual AmpType getAmpType() const = 0;

    static std::unique_ptr<PreampModel> create (AmpType type);
};

// ============================================================================
// Fender Twin Reverb preamp
//
// V1a (12AX7 gain stage) → cathode follower (V1b, unity-gain buffer with
// compression) → volume control. The cathode follower provides impedance
// buffering and soft compression that gives the Fender its spongy clean feel.
//
// Coupling cap: 22nF (large → warm bass, low rolloff ~30Hz)
// Cathode bypass: 25uF on V1a (bass boost below ~80Hz)
// Bright cap: 120pF across volume pot
// ============================================================================

class FenderPreamp : public PreampModel
{
public:
    void prepare (double sampleRate) override;
    void reset() override;
    void process (float* buffer, int numSamples) override;
    void setGain (float gain01) override;
    void setBright (bool on) override;
    AmpType getAmpType() const override { return AmpType::Fender; }

private:
    AnalogEmulation::TubeEmulation v1a_;  // First gain stage
    AnalogEmulation::DCBlocker dc1_;

    double sampleRate_ = 44100.0;
    float gain_ = 0.5f;
    bool bright_ = false;

    // Cathode follower state (V1b modeled as soft compression)
    float cfEnvelope_ = 0.0f;
    float cfAttackCoeff_ = 0.0f;
    float cfReleaseCoeff_ = 0.0f;

    // Coupling cap HPF (~30Hz — 22nF into ~250k grid resistor)
    float couplingCapState_ = 0.0f;
    float couplingCapCoeff_ = 0.0f;

    // Cathode bypass (boosts low freq gain below ~80Hz)
    float cathodeBypassState_ = 0.0f;
    float cathodeBypassCoeff_ = 0.0f;

    // Bright cap HPF (~1.2kHz for 120pF)
    float brightCapState_ = 0.0f;
    float brightCapCoeff_ = 0.0f;

    void updateCoeffs();
};

// ============================================================================
// Marshall Plexi 1959 preamp
//
// V1a (12AX7) → coupling cap → V1b (12AX7) → tone stack.
// Two cascaded gain stages before the tone stack — this is the classic
// Marshall gain structure. Each stage is driven progressively harder.
//
// Coupling caps: 22nF (V1a→V1b) — tighter bass than Fender
// Cathode bypass V1a: 0.68uF (less bass boost than Fender, ~340Hz)
// Cathode bypass V1b: 0.68uF
// Bright cap: 5nF (aggressive treble boost)
// ============================================================================

class MarshallPreamp : public PreampModel
{
public:
    void prepare (double sampleRate) override;
    void reset() override;
    void process (float* buffer, int numSamples) override;
    void setGain (float gain01) override;
    void setBright (bool on) override;
    AmpType getAmpType() const override { return AmpType::Marshall; }

private:
    AnalogEmulation::TubeEmulation v1a_, v1b_;
    AnalogEmulation::DCBlocker dc1_, dc2_;

    double sampleRate_ = 44100.0;
    float gain_ = 0.5f;
    bool bright_ = false;

    // Coupling caps (~30Hz for 22nF into 1M grid)
    float couplingCapState_[2] = {};
    float couplingCapCoeff_ = 0.0f;

    // Cathode bypass (~340Hz for 0.68uF + 1.5k cathode R)
    float cathodeBypassState_[2] = {};
    float cathodeBypassCoeff_ = 0.0f;

    // Bright cap HPF (~700Hz for 5nF — more aggressive than Fender)
    float brightCapState_ = 0.0f;
    float brightCapCoeff_ = 0.0f;

    void updateCoeffs();
};

// ============================================================================
// Vox AC30 Top Boost preamp
//
// V1a (12AX7) → cathode follower (V1b) → tone circuit → V2a (12AX7).
// The cathode follower between stages gives the AC30 its distinctive spongy
// compression. The second gain stage (V2a) after the tone circuit is unique
// to the Vox — it re-amplifies the tone-shaped signal.
//
// Coupling caps: 10nF (smaller → tighter bass, ~50Hz rolloff)
// Cathode bypass V1a: 25uF (full bass boost)
// No bright cap — the AC30 relies on the Cut control instead
// ============================================================================

class VoxPreamp : public PreampModel
{
public:
    void prepare (double sampleRate) override;
    void reset() override;
    void process (float* buffer, int numSamples) override;
    void setGain (float gain01) override;
    void setBright (bool on) override;
    AmpType getAmpType() const override { return AmpType::Vox; }

private:
    AnalogEmulation::TubeEmulation v1a_, v2a_;
    AnalogEmulation::DCBlocker dc1_, dc2_;

    double sampleRate_ = 44100.0;
    float gain_ = 0.5f;
    bool bright_ = false;  // not used on Vox, but kept for interface compat

    // Cathode follower state
    float cfEnvelope_ = 0.0f;
    float cfAttackCoeff_ = 0.0f;
    float cfReleaseCoeff_ = 0.0f;

    // Coupling caps (~50Hz for 10nF into 470k)
    float couplingCapState_[2] = {};
    float couplingCapCoeff_ = 0.0f;

    // Cathode bypass V1a (~80Hz, full bypass like Fender)
    float cathodeBypassState_ = 0.0f;
    float cathodeBypassCoeff_ = 0.0f;

    void updateCoeffs();
};
