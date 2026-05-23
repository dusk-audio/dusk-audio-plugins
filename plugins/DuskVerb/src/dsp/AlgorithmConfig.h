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
    DattorroVintage   = 1,  // Dattorro tank + fixed post-EQ for vintage-Lex character (DattorroPlateVintage wrapper).
    SixAPTank         = 2,  // 6-AP density cascade tank (lush halls, dense ambience).
    QuadTank          = 3,  // 4 cross-coupled tanks, 48 taps, no modulation.
    FDN               = 4,  // 16-channel Hadamard feedback delay network.
    Spring            = 5,  // Fender 6G15-style 3-spring tank with dispersion-AP chirp.
    NonLinear         = 6,  // RMX16-NonLin2-style 64-tap feed-forward TDL with envelope shapes.
    Shimmer           = 7,  // 8-channel Hadamard FDN with in-loop granular pitch shifter.
    Plate             = 8,  // PCM-style foil plate (PlateEngine): 2-AP cross-coupled input + 6-AP density cascade with per-AP RandomWalkLFO jitter, ThreeBandDamping. Built for Lex-Vintage-Plate per-band fit.
    FoilPlate         = 9,  // Second-generation foil plate (FoilPlateEngine): 2-AP flat input + LR4 3-band split + 3 parallel per-band reverberators + onset envelope on wet output + deterministic sine LFOs (anti-correlated L/R phase). Built to close C80/D50/EDT/16kHz/stereo-stability gaps that PlateEngine plateaued on.
    Hall              = 10, // Lex-Hall-anchored hall (HallReverb): multi-tap input injection (FoilPlate Pillar 1) → LR4 3-band split → 3× 8-ch Hadamard FDN sub-tanks (per-band decay / damping / mod) → post-tank linear M/S widener. Built for the LexHall natural-hall family (Med Hall / Large Hall / Vocal Hall anchors). Replaces FDN (algo 4) for hall presets in the Sprint 1 product reset.
    HallRing          = 11, // Griesinger/Carnes sequential ring (RingReverb): 6-stage pre-diffuser → 6-stage modulated delay ring with embedded 3-AP cascades per stage → per-sample feedback to stage 0. Built for the natural-hall family — replaces parallel FDN math with sequential ring math to defeat the modal density Pareto frontier the FDN-based Hall (algo 10) plateaued at 10/19 on.
    HallHybrid        = 12, // Parallel ER + Ring hybrid (HybridHallReverb): 4-tap discrete ER TDL (taps hardcoded at Lex anchor [0/4/7.52/9.79] ms — guarantees peak_locations_ms PASS by construction) mixed against the P15 RingReverb tail. Macro early/late mix axis solves c80/d50; post-mix shelves shape c80_per_octave + bass/treble ratio. Built to break the 10/19 single-topology Pareto ceiling.
    HallTrueLex       = 13, // Composition engine (HallTrueLexReverb): ER TDL (hardcoded Lex anchor [0/4/7.52/9.79] ms — Engine 12 pattern) + Engine 10 8-ch Hadamard FDN tank + 2-stage post-mix Schroeder AP cascade. Independent er_level + tank_level (Fix B). Built to break the 12/19 micro-squeeze ceiling by combining Engine 10's macro c80/d50/decay strengths with Engine 12's guaranteed peak_locations PASS and a phase smearer that attacks spectral_crest + box_ratio without recirculating into the tank.
    HallTrueLex16     = 14, // 16-channel composition (HallTrueLex16Reverb): Engine 14 swaps the 8-ch HallReverb tank used by Engine 13 for the 16-ch FDNReverb (Engine 4's Hadamard FDN). Same ER TDL + post-mix Schroeder AP topology. The 16-ch tank doubles modal density which structurally drops spectral_crest_db ~3 dB and closes the centroid_drift bin3 wall the 8-ch tank could not. No legacy specular taps in the tank — ER TDL is the sole early-energy carrier.
    LexFigure8        = 15, // Lexicon/Dattorro Figure-8 reverberator (LexFigure8Reverb): classic stereo cross-coupled tanks with input AP diffusers + nested AP/delay/LP cascades + Lex spin-and-wander RandomWalkLFO on the modulated APs. Wraps DattorroTank with hall-scale enabled (2× room delays for ~2.4s Lex Med Hall RT60). Built to crack the architectural walls (peak_locations, box_ratio, spectral_crest, centroid_drift bin3) the parallel FDN engines structurally cannot pass.
    LexMTDL           = 16, // Metric-driven Lex Med Hall (LexiconMTDLEngine): Stage 1 FIR ER FIR matrix at exact Lex taps [0/4/7.52/9.79 ms] LTI-isolated from tail. Stage 2 8-line FDN with prime delays + unitary Hadamard mix → discrete recirculating echoes for time_domain_crest. Stage 3 per-line one-pole LP for frequency-dep T60. Built from scratch after Engine 15 Dattorro-figure-8 capped at 15/19 (LTI Pareto wall on tdc proven by 9 smokes).
    LexHybrid         = 17, // Parallel synthesis (v38): runs Engine 15 (Dattorro fig-8, owns spectral wash) + Engine 16 (MTDL, owns temporal chatter) in parallel off same dry input. Macro mix axes balance wash vs chatter. Engine 15 at its 15/19 winner + Engine 16 at its 12/19 winner — internal params locked, only mix axes searched.
};

// Per-engine descriptor surfaced in the algorithm dropdown.
struct AlgorithmConfig
{
    const char* name;
    EngineType  engine;
};

inline int getNumAlgorithms() { return 18; }

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
        { "Plate (Foil)",             EngineType::Plate           },
        { "Plate (Foil II)",          EngineType::FoilPlate       },
        { "Hall (Lex)",               EngineType::Hall            },
        { "Hall (Ring)",              EngineType::HallRing        },
        { "Hall (Hybrid)",            EngineType::HallHybrid      },
        { "Hall (TrueLex)",           EngineType::HallTrueLex     },
        { "Hall (TrueLex 16)",        EngineType::HallTrueLex16   },
        { "Hall (LexFigure8)",        EngineType::LexFigure8      },
        { "Hall (Lex MTDL)",          EngineType::LexMTDL         },
        { "Hall (Lex Hybrid)",        EngineType::LexHybrid       },
    };
    if (index < 0 || index >= 18)
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
