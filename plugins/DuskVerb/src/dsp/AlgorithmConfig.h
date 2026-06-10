#pragma once

// Engine selection for DuskVerb. Eight DSP engines, exposed both as the
// `algorithm` APVTS choice param and as the late-tank routing in
// DuskVerbEngine. The user can switch engines without touching a preset.
//
// Each FactoryPreset's `algorithm` field is the engine index (0..7); the
// preset's musical parameter values are layered on top.
//
// 2026-05-13 reorder: DattorroVintage moved from index 7 → 1 so the two
// Dattorro-based engines (Plate (Dattorro) + Plate (Dattorro Vintage))
// sit adjacent in the dropdown. Indices 1..6 shifted up by 1. State-load
// migration in PluginProcessor::setStateInformation remaps old saved
// algorithm values to the new layout.
enum class EngineType : int
{
    Dattorro          = 0,  // 2-AP cross-coupled plate (Dattorro 1997).
    DattorroVintage   = 1,  // Dattorro tank + fixed post-EQ for vintage-hardware character (DattorroPlateVintage wrapper).
    SixAPTank         = 2,  // 6-AP density cascade tank (lush halls, dense ambience).
    QuadTank          = 3,  // 4 cross-coupled tanks, 48 taps, no modulation.
    FDN               = 4,  // 16-channel Hadamard feedback delay network.
    Spring            = 5,  // Fender 6G15-style 3-spring tank with dispersion-AP chirp.
    NonLinear         = 6,  // RMX16-NonLin2-style 64-tap feed-forward TDL with envelope shapes.
    Shimmer           = 7,  // 8-channel Hadamard FDN with in-loop granular pitch shifter.
    VintageTank       = 8,  // Griesinger/Lexicon figure-8 modulated AP loop — replaces FDN's
                            // unitary Hadamard scatter with a recirculating tank that builds
                            // modal density + lateral bloom over time. Reference architecture
                            // for vintage hardware reverbs.
    ReverseRoom       = 9,  // Causal rising-gain early-reflection onset + dark modulated FDN
                            // tail. Replicates the Lexicon PCM Room "Reverse 1" (NOT
                            // backwards-convolution — the reference is causal, impulse peaks
                            // ~70ms then decays). The rising-ER "Tap Slope" is the reverse.
    AccurateHall      = 10, // FDN + per-OCTAVE attenuation GEQ in the feedback loop (Jot/
                            // Schlecht accurate-RT control). Independent per-octave T60 — closes
                            // the 9-octave-vs-5-band damping wall the standard FDN can't.
    SparseField       = 11, // Velvet-noise front-loaded sparse early field (to ~220ms) summed
                            // with a reduced AccurateHall octave-GEQ tail. Closes the early-
                            // arrival wall (energy_t50/first50/attack/onset/diffusion_flux) the
                            // back-loading FDN/tank topologies structurally cannot.
};

// Per-engine descriptor surfaced in the algorithm dropdown.
struct AlgorithmConfig
{
    const char* name;
    EngineType  engine;
};

inline int getNumAlgorithms() { return 12; }

inline const AlgorithmConfig& getAlgorithmConfig (int index)
{
    static constexpr AlgorithmConfig kEngines[] = {
        { "Plate (Dattorro)",         EngineType::Dattorro        },
        { "Plate (Dattorro Vintage)", EngineType::DattorroVintage },
        { "High Density (6-AP)",      EngineType::SixAPTank       },
        { "Quad Room (QuadTank)",     EngineType::QuadTank        },
        { "Realistic Space (FDN)",    EngineType::FDN             },
        { "Spring Tank (6G15)",       EngineType::Spring          },
        { "Non-Linear (RMX16)",       EngineType::NonLinear       },
        { "Shimmer (Eno FDN)",        EngineType::Shimmer         },
        { "Vintage Tank (Figure-8)",  EngineType::VintageTank     },
        { "Reverse Room (Lexicon)",   EngineType::ReverseRoom     },
        { "Accurate Hall (FDN-GEQ)",  EngineType::AccurateHall    },
        { "Sparse Field (Early)",     EngineType::SparseField     },
    };
    if (index < 0 || index >= 12)
        index = 0;
    return kEngines[index];
}

// Old-index → new-index migration table for state-load. Values are the
// pre-2026-05-13 enum layout (DattorroVintage was at index 7); used by
// PluginProcessor::setStateInformation when it detects a saved session
// from a pre-reorder build.
inline int migrateLegacyAlgorithmIndex (int oldIndex)
{
    switch (oldIndex)
    {
        case 0: return 0;  // Dattorro stays
        case 1: return 2;  // SixAPTank 1→2
        case 2: return 3;  // QuadTank 2→3
        case 3: return 4;  // FDN 3→4
        case 4: return 5;  // Spring 4→5
        case 5: return 6;  // NonLinear 5→6
        case 6: return 7;  // Shimmer 6→7
        case 7: return 1;  // DattorroVintage (was HighDensityPlate) 7→1
        default: return 0;
    }
}
