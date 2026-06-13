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
    AccurateHall32    = 12, // 32-line variant of AccurateHall (double the feedback lines +
                            // order-32 Hadamard) for high-frequency modal density — kills the
                            // metallic ring of the 16-line FDN. Bright Hall only.
    TiledRoom         = 13, // Sparse tapped-delay ER front-end (front-loads ~69% of energy in
                            // the first 50 ms) welded to a SHORT, frozen, low-pass-terminated
                            // 4-line FDN tail (~1.1 kHz loop -> dark late field). Tight ceramic
                            // tiled room — the front-load + instant-dark character a single-tank
                            // FDN structurally cannot express. Tiled Room only.
    DenseHall         = 14, // Diffused-FDN dense hall (DenseHallReverb): 8-line FDN with heavy
                            // allpass diffusion at every stage + modulation everywhere → a dense,
                            // smooth tail (flatness ~0.16 vs the 16-line FDN's sparse 0.044). The
                            // late field the AccurateHall FDN structurally cannot reach. Welded to
                            // a sparse discrete-ER front for the early field. DuskVerb's own code.
};

// Per-engine descriptor surfaced in the algorithm dropdown.
//
// `visible` curates the dropdown (2026-06-12): the four engines no factory
// preset uses (QuadTank, FDN, VintageTank, SparseField — redundant variants of
// the Dattorro-tank / Hadamard-FDN cores) are hidden so the roster reads as an
// intentional palette of distinct "spaces". The enum, the 14-wide `algorithm`
// choice param, and every factory preset's stored algorithm index are LEFT
// UNCHANGED — hidden engines still load from saved state and still run in the
// DSP switch; they are merely not offered in the editor dropdown.
struct AlgorithmConfig
{
    const char* name;
    EngineType  engine;
    bool        visible;
};

inline int getNumAlgorithms() { return 15; }

inline const AlgorithmConfig& getAlgorithmConfig (int index)
{
    // User-facing engine names. Standardized 2026-06-11: clean musician-facing
    // labels, no internal-algorithm jargon and no trademark references (was
    // Lexicon / RMX16 / Eno / 6G15 / Figure-8 / Dattorro). ENUM ORDER IS FIXED —
    // AudioParameterChoice stores the index, presets store the algorithm index,
    // so these strings are display-only and must NOT be reordered or removed
    // (that would shift indices and break saved state). Rename freely.
    static constexpr AlgorithmConfig kEngines[] = {
        { "Plate",         EngineType::Dattorro,        true  },
        { "Vintage Plate", EngineType::DattorroVintage, true  },
        { "Smooth Plate",  EngineType::SixAPTank,       true  },
        { "Chamber",       EngineType::QuadTank,        true  }, // used by 79 Vocal Chamber
        { "Studio",        EngineType::FDN,             false }, // hidden: no preset; plain Hadamard superseded by Hall
        { "Spring",        EngineType::Spring,          true  },
        { "Gated",         EngineType::NonLinear,       true  },
        { "Shimmer",       EngineType::Shimmer,         true  },
        { "Vintage Hall",  EngineType::VintageTank,     false }, // hidden: no preset; redundant tank
        { "Reverse",       EngineType::ReverseRoom,     true  },
        { "Hall",          EngineType::AccurateHall,    true  },
        { "Sparse",        EngineType::SparseField,     false }, // hidden: no preset; folded into Hall/composite
        { "Concert Hall",  EngineType::AccurateHall32,  false }, // hidden 2026-06-13: Bright Hall migrated to DenseHall; 32-line FDN superseded, no preset uses it
        { "Tiled Room",    EngineType::TiledRoom,       true  },
        { "Dense Hall",    EngineType::DenseHall,       true  },
    };
    if (index < 0 || index >= 15)
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
