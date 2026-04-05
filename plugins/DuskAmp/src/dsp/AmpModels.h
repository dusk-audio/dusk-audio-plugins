#pragma once

#include "AnalogEmulation/WaveshaperCurves.h"

// ============================================================================
// Amp Type Enum — 3 circuit-simulated amp models
//
// Each model uses a dedicated WDF (Wave Digital Filter) circuit simulation
// with real component values from published schematics.
// ============================================================================

enum class AmpType
{
    FenderDeluxe  = 0,  // Fender Deluxe Reverb 65 — American clean, 6L6, cathode follower
    VoxAC30       = 1,  // Vox AC30 Top Boost — Class A chime, EL84, no NFB
    MarshallPlexi = 2,  // Marshall 1959 Plexi — British crunch, EL34, bright cap
    kNumTypes     = 3
};

static constexpr int kNumAmpTypes = static_cast<int> (AmpType::kNumTypes);

namespace AmpModels
{

// ============================================================================
// Tone Stack Topology Mapping
// ============================================================================

enum class ToneStackTopology
{
    American = 0,  // Fender AB763 tone stack
    British  = 1,  // Marshall JTM45 tone stack
    AC       = 2,  // Vox AC30 Top Boost cut circuit
    Modern   = 3   // (unused — kept for ToneStack enum compatibility)
};

inline ToneStackTopology getToneStackTopology (AmpType type)
{
    switch (type)
    {
        case AmpType::FenderDeluxe:  return ToneStackTopology::American;
        case AmpType::VoxAC30:       return ToneStackTopology::AC;
        case AmpType::MarshallPlexi: return ToneStackTopology::British;
        default:                     return ToneStackTopology::American;
    }
}

// ============================================================================
// Power Amp Configuration
// ============================================================================

struct PowerAmpConfig
{
    AnalogEmulation::WaveshaperCurves::CurveType powerTubeCurve =
        AnalogEmulation::WaveshaperCurves::CurveType::Pentode;

    float nfbRatio = 0.3f;
    float sagAttackMs = 10.0f;
    float sagReleaseMs = 100.0f;
    float maxDriveGain = 4.0f;
    float classABias = 0.0f;
    float transformerHFRolloff = 14000.0f;
    float transformerSatThreshold = 0.75f;
    float outputTransformerGain = 1.0f;

    // Power supply RC model
    float psuCapacitance = 100e-6f;
    float psuChargeR     = 100.0f;
    float psuSagDepth    = 0.3f;

    // Speaker impedance interaction
    float spkrResonanceHz    = 80.0f;
    float spkrResonanceQ     = 2.0f;
    float spkrResonanceZRatio = 5.0f;
    bool  spkrImpedanceEnabled = true;
};

inline PowerAmpConfig getPowerAmpConfig (AmpType type)
{
    PowerAmpConfig c;

    switch (type)
    {
        case AmpType::FenderDeluxe:
            // 6V6GT beam tetrode, GZ34 tube rectifier, NFB from OT secondary
            // Undersized OT causes natural compression and sag
            c.powerTubeCurve = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;  // 6V6 is beam tetrode
            c.nfbRatio = 0.5f;           // 820Ω NFB resistor — moderate feedback
            c.sagAttackMs = 12.0f;
            c.sagReleaseMs = 120.0f;
            c.maxDriveGain = 4.0f;
            c.classABias = 0.0f;         // Class AB fixed bias
            c.transformerHFRolloff = 11000.0f;  // Undersized OT rolls off earlier
            c.transformerSatThreshold = 0.70f;  // Undersized OT saturates earlier
            c.psuCapacitance = 80e-6f;   c.psuChargeR = 120.0f;  c.psuSagDepth = 0.30f;  // GZ34 tube rect
            c.spkrResonanceHz = 80.0f;   c.spkrResonanceQ = 1.8f; c.spkrResonanceZRatio = 4.5f;
            break;

        case AmpType::VoxAC30:
            // 4x EL84 pentodes, GZ34 tube rectifier, NO negative feedback
            // Runs EL84s 40% over plate dissipation limit — hot Class A-ish bias
            // B+ = 345V at plates, 47Ω cathode resistor per tube
            c.powerTubeCurve = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;  // EL84 is pentode
            c.nfbRatio = 0.0f;           // No NFB — THE defining AC30 characteristic
            c.sagAttackMs = 5.0f;
            c.sagReleaseMs = 60.0f;
            c.maxDriveGain = 5.0f;
            c.classABias = 0.15f;        // Hot bias — closer to Class A than AB
            c.transformerHFRolloff = 10000.0f;
            c.transformerSatThreshold = 0.60f;  // EL84s run very hot, saturate early
            c.outputTransformerGain = 1.2f;
            c.psuCapacitance = 40e-6f;   c.psuChargeR = 120.0f;  c.psuSagDepth = 0.45f;  // GZ34 tube rect
            c.spkrResonanceHz = 85.0f;   c.spkrResonanceQ = 3.0f; c.spkrResonanceZRatio = 7.0f;
            break;

        case AmpType::MarshallPlexi:
            // 4x EL34 true pentodes, SOLID STATE rectifier, 100k NFB resistor
            // Presence cap in NFB path: 0.68uF (varies by production run)
            // Choke between plate and screen nodes, 1k screen resistors
            c.powerTubeCurve = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
            c.nfbRatio = 0.25f;          // 100k NFB resistor — moderate feedback
            c.sagAttackMs = 3.0f;        // SS rectifier — fast recovery
            c.sagReleaseMs = 30.0f;      // SS rectifier — fast recovery
            c.maxDriveGain = 5.0f;
            c.classABias = 0.0f;         // Class AB
            c.transformerHFRolloff = 14000.0f;
            c.transformerSatThreshold = 0.70f;
            c.outputTransformerGain = 1.3f;
            c.psuCapacitance = 100e-6f;  c.psuChargeR = 10.0f;   c.psuSagDepth = 0.10f;  // SS rect = tight PSU
            c.spkrResonanceHz = 100.0f;  c.spkrResonanceQ = 2.5f; c.spkrResonanceZRatio = 6.0f;
            break;

        default:
            break;
    }

    return c;
}

// ============================================================================
// Amp Type Names (for parameter choice display)
// ============================================================================

inline const char* getAmpTypeName (AmpType type)
{
    switch (type)
    {
        case AmpType::FenderDeluxe:  return "American Clean";
        case AmpType::VoxAC30:       return "Class A Chime";
        case AmpType::MarshallPlexi: return "British Crunch";
        default:                     return "Unknown";
    }
}

} // namespace AmpModels
