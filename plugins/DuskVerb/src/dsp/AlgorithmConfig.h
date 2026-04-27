#pragma once

// Engine selection for DuskVerb. Four raw DSP engines, exposed both as the
// `algorithm` APVTS parameter (4-entry choice) and as the late-tank routing
// in DuskVerbEngine. The user can switch engines without touching a preset.
//
// Each FactoryPreset's `algorithm` field is the engine index (0..3); the
// preset's musical parameter values are layered on top.
enum class EngineType : int
{
    Dattorro       = 0,  // 2-AP cross-coupled vintage plate (Dattorro 1997).
    ModernSpace6AP = 1,  // 6-AP density cascade tank (lush halls, dense ambience).
    QuadTank       = 2,  // 4 cross-coupled tanks, 48 taps, no modulation.
    FDN            = 3,  // 16-channel Hadamard feedback delay network.
    Spring         = 4,  // Fender 6G15-style 3-spring tank with dispersion-AP chirp.
    NonLinear      = 5,  // RMX16-NonLin2-style 64-tap feed-forward TDL with envelope shapes.
    Shimmer        = 6,  // 8-channel Hadamard FDN with in-loop granular pitch shifter (Eno/Lanois).
};

// Per-engine descriptor surfaced in the algorithm dropdown.
struct AlgorithmConfig
{
    const char* name;
    EngineType  engine;
};

// Shimmer (Eno FDN) is currently disabled — the in-loop modulation +
// granular pitch shifter combination has too many open issues to ship
// in this release. The engine code stays compiled so factory presets
// that reference it don't break the build, but it's hidden from the UI
// dropdown and no factory presets target it. Re-enable by bumping this
// to 7 once the cascade artefacts are resolved.
inline int getNumAlgorithms() { return 6; }

inline const AlgorithmConfig& getAlgorithmConfig (int index)
{
    static constexpr AlgorithmConfig kEngines[] = {
        { "Vintage Plate (Dattorro)", EngineType::Dattorro       },
        { "High Density (6-AP)",      EngineType::ModernSpace6AP },
        { "Quad Room (QuadTank)",     EngineType::QuadTank       },
        { "Realistic Space (FDN)",    EngineType::FDN            },
        { "Spring Tank (6G15)",       EngineType::Spring         },
        { "Non-Linear (RMX16)",       EngineType::NonLinear      },
        { "Shimmer (Eno FDN)",        EngineType::Shimmer        },   // hidden — see getNumAlgorithms()
    };
    if (index < 0 || index >= 7)   // bound to FULL array, not dropdown count
        index = 0;
    return kEngines[index];
}
