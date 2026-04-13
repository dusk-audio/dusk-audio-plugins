#pragma once

// Abstract interface for per-preset custom reverb engines.
//
// Each of the 53 factory presets has its own concrete implementation
// (e.g. TiledRoomPreset, SteelPlatePreset, etc.) that is a fully isolated
// copy of the shared engine it's based on (FDNReverb / DattorroTank / QuadTank),
// with the AlgorithmConfig values baked in as private compile-time constants.
//
// Modifying one preset's DSP (adding a notch filter, tweaking feedback, etc.)
// touches only its own .cpp file and cannot affect any other preset.
//
// DuskVerbEngine routes audio through the per-preset engine instead of the
// shared engines when a matching entry is found in the PresetEngineRegistry.

class PresetEngineBase
{
public:
    virtual ~PresetEngineBase() = default;

    // Lifecycle
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (const float* inputL, const float* inputR,
                          float* outputL, float* outputR, int numSamples) = 0;
    virtual void clearBuffers() = 0;

    // Runtime setters (called by DuskVerbEngine in response to user parameter changes
    // and runtime override parameters).
    //
    // Not all per-preset engines implement all setters — the default no-op
    // lets engines ignore parameters that don't apply to their underlying DSP.

    virtual void setDecayTime (float seconds)              { (void) seconds; }
    virtual void setBassMultiply (float mult)              { (void) mult; }
    virtual void setTrebleMultiply (float mult)            { (void) mult; }
    virtual void setCrossoverFreq (float hz)               { (void) hz; }
    virtual void setModDepth (float depth)                 { (void) depth; }
    virtual void setModRate (float hz)                     { (void) hz; }
    virtual void setSize (float size)                      { (void) size; }
    virtual void setFreeze (bool frozen)                   { (void) frozen; }
    virtual void setDecayBoost (float boost)               { (void) boost; }
    virtual void setTerminalDecay (float thresholdDB, float factor)
    {
        (void) thresholdDB; (void) factor;
    }
    virtual void setHighCrossoverFreq (float hz)           { (void) hz; }
    virtual void setAirDampingScale (float scale)          { (void) scale; }
    virtual void setNoiseModDepth (float samples)          { (void) samples; }
    virtual void setStructuralHFDamping (float hz)         { (void) hz; }
    virtual void setSizeRange (float minVal, float maxVal)
    {
        (void) minVal; (void) maxVal;
    }
    virtual void setLateGainScale (float scale)            { (void) scale; }

    // Reset overrideable parameters to the values baked during prepare().
    // Called by DuskVerbEngine when an override sentinel fires (< 0) so the
    // preset restores its own calibrated baseline instead of the shared config
    // default. Default no-ops keep the current value, which is correct for
    // presets whose prepare() has already set the baked value and no override
    // was applied since.
    virtual void resetAirDampingToDefault() {}
    virtual void resetHighCrossoverToDefault() {}
    virtual void resetNoiseModToDefault() {}

    // --- Corrective EQ exposure (Fix 4: spectral) ---
    // Returns the number of corrective EQ bands this preset uses (0 = none).
    virtual int getCorrEQBandCount() const { return 0; }

    // Copies the already-computed biquad coefficients for the corrective EQ
    // into the provided arrays (b0, b1, b2, a1, a2). Returns true if
    // coefficients were written, false if no corrective EQ is available.
    // maxBands limits the number of bands copied (caller allocates arrays).
    virtual bool getCorrEQCoeffs (float* b0, float* b1, float* b2,
                                   float* a1, float* a2, int maxBands) const
    {
        (void) b0; (void) b1; (void) b2; (void) a1; (void) a2; (void) maxBands;
        return false;
    }

    // --- Onset envelope table (Fix 1: decay_ratio) ---
    // Returns a VV/DV energy ratio curve applied to the FDN output.
    // Values < 1.0 attenuate DV where it has more body energy than VV.
    // Typical shape: 1.0 at onset, dips to 0.1-0.3 in body, recovers to 1.0.
    // Returns nullptr if this preset uses the default squared linear ramp.
    virtual const float* getOnsetEnvelopeTable() const { return nullptr; }
    virtual int getOnsetEnvelopeTableSize() const { return 0; }
    virtual float getOnsetDurationMs() const { return 0.0f; }

    // Engine metadata (primarily for debugging/logging)
    virtual const char* getPresetName() const = 0;
    virtual const char* getBaseEngineType() const = 0;  // "FDN" / "DattorroTank" / "QuadTank"
};
