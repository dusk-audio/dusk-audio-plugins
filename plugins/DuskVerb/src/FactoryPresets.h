#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Forward declaration — applyEngineConfig() needs to call SixAPTank-specific
// setters on DuskVerbEngine without dragging the engine header into every
// include of FactoryPresets.h.
class DuskVerbEngine;

// Factory presets — 16 hardware-anchored voicings.
// `algorithm` is the engine index (0..3) per AlgorithmConfig.h:
//   0 = Vintage Plate (Dattorro)
//   1 = High Density  (6-AP)
//   2 = Quad Room     (QuadTank, no modulation)
//   3 = Realistic Space (FDN)
//
// IMPORTANT: presets are grouped CONTIGUOUSLY by category — the editor's
// dropdown adds a section heading whenever the category changes, so any
// re-ordering must keep the per-category blocks intact or duplicate
// "Plates" / "Halls" / "Rooms" headers will reappear.
struct FactoryPreset
{
    const char* name;
    const char* category;

    int   algorithm;
    float mix;
    bool  busMode;
    float predelay;
    int   predelaySync;     // 0 = Free, 1..6 = 1/32 .. 1/1
    float decay;
    float size;
    float modDepth;
    float modRate;
    float damping;
    float bassMult;
    float crossover;
    float diffusion;
    float erLevel;
    float erSize;
    float loCut;
    float hiCut;
    float width;
    bool  freeze;
    float gainTrim;
    // Trailing fields use defaults so the existing 16 brace-init literals stay
    // valid — older preset rows that don't include the new fields will pick
    // up the safe defaults below (mono_below=20=bypass, mid=1.0=natural,
    // high_xover=4000Hz=neutral, saturation=0=clean).
    float monoBelow      = 20.0f;
    float midMult        = 1.0f;     // 3-band mid multiplier (1.0 = natural)
    float highCrossover  = 4000.0f;  // mid↔high split (Hz)
    float saturation     = 0.0f;     // 0..1 drive softClip
    bool  gateEnabled    = true;     // NonLinear engine: true = gate active.
                                     // No-op on other engines but written
                                     // anyway so loading a preset always
                                     // sets the toggle to a known state.

    // SixAPTank-specific engine tunables. Defaults match the engine's
    // historical hardcoded constants — so any preset that doesn't override
    // these gets identical sound to before. Black Hole opts in to brighter,
    // denser values for VS BlackHole-character late-tail content.
    float sixAPDensityBaseline = 0.62f;
    float sixAPBloomCeiling    = 0.85f;
    float sixAPBloomStagger[6] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f };
    float sixAPEarlyMix        = 0.5f;
    float sixAPOutputTrim      = 1.3f;

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        auto setIfExists = [&apvts] (const juce::String& id, float v) {
            if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        if (auto* p = apvts.getParameter ("algorithm"))
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (algorithm)));
        if (auto* p = apvts.getParameter ("predelay_sync"))
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (predelaySync)));
        if (auto* p = apvts.getParameter ("bus_mode"))     p->setValueNotifyingHost (busMode     ? 1.0f : 0.0f);
        if (auto* p = apvts.getParameter ("freeze"))       p->setValueNotifyingHost (freeze      ? 1.0f : 0.0f);
        if (auto* p = apvts.getParameter ("gate_enabled")) p->setValueNotifyingHost (gateEnabled ? 1.0f : 0.0f);
        setIfExists ("mix",       mix);
        setIfExists ("predelay",  predelay);
        setIfExists ("decay",     decay);
        setIfExists ("size",      size);
        setIfExists ("mod_depth", modDepth);
        setIfExists ("mod_rate",  modRate);
        setIfExists ("damping",   damping);
        setIfExists ("bass_mult", bassMult);
        setIfExists ("mid_mult",  midMult);
        setIfExists ("crossover", crossover);
        setIfExists ("high_crossover", highCrossover);
        setIfExists ("saturation", saturation);
        setIfExists ("diffusion", diffusion);
        setIfExists ("er_level",  erLevel);
        setIfExists ("er_size",   erSize);
        setIfExists ("lo_cut",    loCut);
        setIfExists ("hi_cut",    hiCut);
        setIfExists ("width",     width);
        setIfExists ("gain_trim", gainTrim);
        setIfExists ("mono_below", monoBelow);
    }

    // Apply engine-specific (non-APVTS) tunables. Currently only the
    // SixAPTank brightness/density fields. Defined out-of-line in
    // PluginProcessor.cpp where DuskVerbEngine's full type is visible.
    void applyEngineConfig (DuskVerbEngine& engine) const;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        // ── Vocal Plate (PCM 90) ─────────────────────────────────────────────
        // Engine: Dattorro. Anchor: PCM 90 "Vocal Plate" (Bank P2 1.0). Short
        // plate with notoriously LOW diffusion — keeps consonants intact.
        //   RT60 0.88 s   bass_mult 0.99 (flat)   treble_mult 0.83 (some HF damp)
        //   centroid 50ms 11.0 kHz   diffusion-proxy 0.67 (LOW — vocal-clear)
        //   predelay 4 ms
        // Distinct from "Vintage Vocal Plate" (EMT-140 anchor): this is the
        // Lexicon PCM digital-plate sound — brighter, shorter, less diffused.
        { "Vocal Plate",          "Plates",
          0,  0.35f, false,  4.0f, 0,
          0.95f, 0.45f, 0.05f, 0.50f, 0.85f, 1.00f,  700.0f,
          0.55f, 0.00f, 0.30f, 100.0f, 11000.0f, 1.10f, false, 16.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4500.0f, /* sat */ 0.10f },
        // ── Rich Plate (PCM 90) ──────────────────────────────────────────────
        // Engine: Dattorro. Anchor: Lexicon PCM 90 "Rich Plate" (Bank P2 0.1)
        // — the industry-standard bright + diffuse Lexicon plate. Measured
        // against the PCM 90 IR set:
        //   RT60 1.52 s   bass_mult 0.98 (flat)   treble_mult 0.96 (almost flat)
        //   centroid 50ms 10.8 kHz   diffusion-proxy 2.04 (dense)   predelay 0
        // The flat per-band decay + bright top is the "Rich" character.
        { "Rich Plate",           "Plates",
          0,  0.40f, false,  0.0f, 0,
          1.60f, 0.55f, 0.10f, 0.45f, 0.95f, 1.00f,  600.0f,
          0.85f, 0.00f, 0.30f,  80.0f, 14000.0f, 1.10f, false, 14.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Gold Plate (PCM 90) ──────────────────────────────────────────────
        // Engine: Dattorro. Anchor: PCM 90 "Gold Plate" (Bank P2 0.2). Long,
        // smooth, classic Lexicon plate.
        //   RT60 1.76 s   bass_mult 0.85   treble_mult 0.84 (uniformly slightly damped)
        //   centroid 50ms 10.5 kHz   diffusion-proxy 4.47 (very dense)   predelay 0
        // Higher diffusion + slight bass-and-treble roll-off vs Rich Plate
        // gives the smoother gold-anodized character. Tiny mod for a long
        // tail that doesn't glass up.
        { "Gold Plate",           "Plates",
          0,  0.30f, false,  0.0f, 0,
          1.96f, 0.357f, 0.12f, 0.35f, 1.00f, 0.55f,  600.0f,
          0.80f, 0.00f, 0.00f, 200.0f, 20000.0f, 1.15f, false, 16.0f,
          /* mono */ 20.0f, /* mid */ 0.80f, /* highX */ 3000.0f, /* sat */ 0.00f },
        // ── Fat Pop Plate ────────────────────────────────────────────────────
        // Anchor: Lexicon 480L "Fat Plate" pop-vocal setting.
        // 480L "Fat Plate" actually used 40-55 % modulation depth (0-99
        // scale). Bumped from 0.25 to 0.35 — 0.25 was too tame for the
        // signature 80s/90s pop-vocal "fat wall". Crossover 480 Hz matches
        // documented 400-550 Hz range.
        // ER zeroed: 480L "Fat Plate" added density and bass weight to the
        // plate algorithm but kept the same plate signal flow — no ER section.
        // Tightened against VVV Fat Plate render comparison:
        //   • decay 2.80 → 2.10 — Dattorro broadband-RMS measured 3.72s vs
        //     UI 2.80 (+33% off!). Bumping UI decay DOWN so measured lands
        //     near the perceived 2.8s.
        //   • hi_cut 8500 → 14000 — VVV Fat Plate is much brighter (-3.4 dB
        //     at 8 kHz vs DV's -10 dB). Pop plates need air for snare snap.
        //   • bassMult 1.45 → 1.10 — DV had +2.3 dB bass body vs VVV's
        //     -0.2 dB. Tighter mix bass, less mud on busy material.
        { "Fat Pop Plate",        "Plates",
          0,  0.40f, false, 18.0f, 0,
          2.10f, 0.55f, 0.35f, 0.85f, 0.55f, 1.10f,  480.0f,
          0.85f, 0.00f, 0.40f, 50.0f, 14000.0f, 1.30f, false, 13.0f,
          /* mono */ 20.0f, /* mid */ 1.20f, /* highX */ 4500.0f, /* sat */ 0.30f },
        // ── Modulated Plate ──────────────────────────────────────────────────
        // Anchor: Lexicon PCM-70 / PCM-80 "Plate" with chorus depth.
        // Mod 0.40 — recalibrated after the all-engine depth normalisation
        // (FDN's per-sample excursion was 4× before, now ×16, so the old
        // 0.65 became over-saturated chorus). 0.40 still carries the
        // signature PCM-series swirl on top of FDN density.
        // ER zeroed: PCM-70/80 plate algorithms layer chorus on the plate
        // tank itself — no ER stage. Keep the FDN density doing the work.
        { "Modulated Plate",      "Plates",
          3,  0.40f, false,  8.0f, 0,
          2.40f, 0.50f, 0.40f, 1.40f, 0.85f, 1.00f, 1300.0f,
          0.80f, 0.00f, 0.45f, 70.0f, 14000.0f, 1.20f, false, 0.5f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 4500.0f, /* sat */ 0.25f },
        // ═══════════ PLATES ═══════════
        // ── Vintage Vocal Plate ──────────────────────────────────────────────
        // Anchor: EMT 140 steel plate (1957), medium damper, vocal channel.
        // Acoustic plate — no LFO, intrinsic bass roll-off, top capped ~9.5 k
        // by the steel. The ABBA / Dancing-Queen lead-vocal sound.
        // name, cat, algo, mix, bus, predelay, sync,
        // decay, size, modD, modR, damp, bass, xover,
        // diff, erLv, erSz, loCut, hiCut, width, freeze, trim
        // Predelay 18 ms ≈ Abbey Road STEED tape-slap before the plate
        // (engineers historically padded the 2-3 ms physical pickup delay).
        // ER zeroed: a real EMT 140 has no early reflections — it's a 2D
        // steel plate with sample-zero diffuse buildup, no walls to bounce off.
        // Tightened against VVV Vocal Plate / Vox Plate render comparison:
        //   • decay 2.40 → 3.20 — Dattorro broadband-RMS measures ~18 % short
        //     of UI on this preset (bassMult 0.75 + trebleMult 0.65 weighted
        //     toward fast treble). 2.90 closed it to -11 % (right at the
        //     audible JND boundary); 3.20 lands measured at the user-
        //     perceived 2.4 s for honest DECAY-knob calibration.
        //   • hi_cut 9500 → 14000 Hz — opens the air band to compete with
        //     VVV Vocal Plate's bright character (their HighCut = 20 kHz).
        //     Still darker than VVV, preserves "EMT 140 steel" colouration.
        //   • bassMult 0.85 → 0.75 — cleaner vocal-mix bass, matching the
        //     ~3 dB bass cut that VVV Vocal Plate engineers in.
        // mod_depth 0.00 → 0.05 + rate 0.50 → 0.40 — even acoustic plates had
        // microscopic delay-line wobble to prevent metallic ringing on
        // sustained vocal notes. 0.05 is a whisper (≈ ±1 cent at 44.1k);
        // not a chorus effect, just enough to keep the long vocal sustain
        // from glassing up.
        { "Vintage Vocal Plate",  "Plates",
          0,  0.32f, false, 18.0f, 0,
          3.20f, 0.50f, 0.05f, 0.40f, 0.65f, 0.75f,  800.0f,
          0.85f, 0.00f, 0.30f, 90.0f, 14000.0f, 1.10f, false, 12.5f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 5000.0f, /* sat */ 0.20f },
        // ── Snare Plate XL ───────────────────────────────────────────────────
        // Long-decay plate for '80s big-snare/tom slap. Engine matched to
        // VVV's DrumPlate / FatPlate architecture (forensic L/R-correlation
        // analysis confirmed both VVV plates use FDN, not Dattorro). FDN
        // gives the modern smooth-plate alternative to the Dattorro plates'
        // vintage character. Bright top + controlled bass + a touch of
        // saturation evoke the JBL-tape-into-EMT-140 era.
        { "Snare Plate XL",       "Plates",
          3,  0.42f, false, 12.0f, 0,
          4.50f, 0.65f, 0.15f, 0.50f, 0.85f, 0.65f,  600.0f,
          0.75f, 0.30f, 0.55f, 180.0f, 14000.0f, 1.30f, false, 1.0f,
          /* mono */ 20.0f, /* mid */ 1.05f, /* highX */ 5000.0f, /* sat */ 0.20f },
        // ═══════════ SPRINGS ═══════════
        // ── Surf '63 Spring ──────────────────────────────────────────────────
        // Engine: SpringEngine (algo 4). Reference: Fender 6G15 outboard
        // reverb unit driving a clean amp — Dick Dale "Misirlou" (1962),
        // every surf-rock tremolo-picked lead through 1962-65. Short-spring
        // tank, mild dispersion, classic 4 kHz spring rolloff. The hijacked
        // mod_depth (SPRING LEN) and mod_rate (DRIP) knobs control the
        // characteristic ambient wobble.
        { "Surf '63 Spring",      "Springs",
          4,  0.35f, false,  0.0f, 0,
          1.60f, 0.40f, 0.20f, 1.50f, 1.00f, 0.85f, 1000.0f,
          0.45f, 0.10f, 0.30f,  80.0f,  4000.0f, 1.10f, false, 2.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Tank Drip ────────────────────────────────────────────────────────
        // Engine: SpringEngine. Reference: Twin Reverb / Pro Reverb onboard
        // tank cranked — heavy chirp, longer springs, darker top, pronounced
        // "boing" on transients. The diffusion knob (CHIRP) at 0.85 gives
        // the full Fender-on-eleven boing character.
        { "Tank Drip",            "Springs",
          4,  0.40f, false,  0.0f, 0,
          2.20f, 0.65f, 0.30f, 0.80f, 0.70f, 1.10f, 1000.0f,
          0.85f, 0.10f, 0.30f, 100.0f,  3000.0f, 1.20f, false, 2.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.20f },
        // ── Utility Hall (PCM 90) ────────────────────────────────────────────
        // Engine: FDN. Anchor: PCM 90 "Utility Hall" (Bank P0 2.9) — designed
        // to sit invisibly behind a mix. Despite the user-facing description
        // being "dark", the IR measurement reveals it's actually balanced
        // with bright initial transient and slight HF-persistent tail —
        // the "invisible" character comes from the FAST bass decay (kills
        // the bottom-end mud) plus a moderate hi_cut that takes the edge
        // off without killing air.
        //   RT60 1.06 s   bass_mult 0.76 (bass dies fast)   treble_mult 1.12
        //   centroid 50ms 10.5 kHz   centroid 1s 10.6 kHz (bright sustained)
        //   diffusion-proxy 2.07 (dense)   predelay 1 ms
        { "Utility Hall",         "Halls",
          3,  0.40f, false,  1.0f, 0,
          1.10f, 0.55f, 0.08f, 0.45f, 1.10f, 0.75f, 1000.0f,
          0.75f, 0.50f, 0.50f, 100.0f,  8000.0f, 1.10f, false, 2.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4500.0f, /* sat */ 0.05f },
        // ── Bright Studio Hall ───────────────────────────────────────────────
        // Fills the bright-hall gap. Pop/rock vocals and acoustic instruments
        // — the rest of the Halls roster skews dark (Lush Dark Hall, Cathedral,
        // Blade Runner, Vocal Hall). FDN engine matches VVV's VocalHall
        // architecture (forensic L/R-correlation analysis confirmed FDN).
        // Articulate top, present mids, ER tap density on the higher side
        // for that "I can hear the room shape" quality on pop arrangements.
        { "Bright Studio Hall",   "Halls",
          3,  0.40f, false, 18.0f, 0,
          1.80f, 0.55f, 0.10f, 0.55f, 0.85f, 1.05f,  400.0f,
          0.65f, 0.40f, 0.50f, 120.0f, 14000.0f, 1.30f, false, 1.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 5500.0f, /* sat */ 0.05f },
        // ── Bright Hall (PCM 90) ─────────────────────────────────────────────
        // Engine: FDN. Anchor: PCM 90 "Bright Hall" (Bank P0 2.8) — Lexicon
        // "sizzle" character, treble actually rings LONGER than mid.
        //   RT60 1.69 s   bass_mult 0.98 (flat)   treble_mult 1.16 (HF persists!)
        //   centroid 50ms 10.9 kHz   centroid 1s 12.0 kHz (gets brighter late!)
        //   diffusion-proxy 0.69 (low — sparkly, not smeared)
        // The treble_mult > 1.0 is the key — this preset's identity is
        // brighter-than-mid HF persistence. FDN engine handles that better
        // than the Dattorro tank's bass-favouring damping curve.
        { "Bright Hall",          "Halls",
          3,  0.40f, false,  0.0f, 0,
          1.80f, 0.65f, 0.12f, 0.50f, 1.20f, 1.00f, 1000.0f,
          0.75f, 0.50f, 0.50f,  80.0f, 18000.0f, 1.20f, false, 1.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 6000.0f, /* sat */ 0.05f },
        // ── Smooth Concert Hall ──────────────────────────────────────────────
        // Anchor: Lexicon 480L "Smooth Hall" no-mod variant + Bricasti M7 hall.
        // 5 % LFO at 0.6 Hz is sub-audible vibrato but breaks the static
        // phase-locks that make a perfectly-deterministic tank ring metallically.
        // High diffusion (0.85) smooths modal grain; mild bass bloom (×1.2).
        { "Smooth Concert Hall",  "Halls",
          2,  0.35f, false, 28.0f, 0,
          2.60f, 0.65f, 0.05f, 0.60f, 0.75f, 1.20f,  900.0f,
          0.85f, 0.45f, 0.65f, 60.0f, 13000.0f, 1.25f, false, -0.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4500.0f, /* sat */ 0.10f },
        // ── Blade Runner Concert (PCM 90) ────────────────────────────────────
        // Engine: SixAPTank. Anchor: PCM 90 "Concert Hall" (Bank P0,
        // preset 574) — the purest vanilla form of the algorithm Vangelis
        // used. A SHORTER, DARKER companion to "Blade Runner 224", tuned to
        // the actual Lexicon hardware spec rather than the user-extended
        // Arturia LX-24 saved preset.
        //
        // Direct port of the IR measurements:
        //   RT60 3.09 s   bass_mult 1.25   treble_mult 0.52 (the exact
        //   Lexicon "two-stage decay" — bass rings 25 % longer than mid,
        //   treble HALF as long: that's the dark-cinematic-cathedral
        //   character measured straight off the PCM 90)
        //   centroid 50ms 8.1 kHz → 1s 3.1 kHz (the iconic darkening tail)
        //   diffusion-proxy 0.76   predelay 5 ms
        //
        // Use this when you want the historically-accurate Lexicon Concert
        // Hall sound — closer to the studio reference Vangelis would've
        // dialled in. "Blade Runner 224" remains the long-decay extended
        // version for sustained-pad cinematic use.
        { "Blade Runner Concert", "Halls",
          1,  0.45f, false,  5.0f, 0,
          3.00f, 0.85f, 0.10f, 0.40f, 0.52f, 1.25f,  700.0f,
          0.85f, 0.45f, 0.55f,  60.0f,  8000.0f, 1.20f, false, 8.5f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Deep Blue (PCM 90) ───────────────────────────────────────────────
        // Engine: SixAPTank. Anchor: PCM 90 "Deep Blue" (Bank P0 0.0)
        // — Lexicon's "impossibly massive" Concert Hall preset, the literal
        // first preset in their Hall bank. The 6-AP density cascade matches
        // the PCM's late-tail thickness better than FDN.
        //   RT60 2.63 s   bass_mult 1.10   treble_mult 0.64 (DARK!)
        //   centroid 50ms 5.9 kHz (already dark on the transient)
        //   centroid 1s   3.6 kHz (continues to darken)
        //   shape: REVERSE — energy builds late (the "swelling cathedral" character)
        //   predelay 10 ms
        { "Deep Blue",            "Halls",
          1,  0.45f, false, 10.0f, 0,
          3.00f, 0.85f, 0.15f, 0.40f, 0.65f, 1.10f,  600.0f,
          0.85f, 0.40f, 0.65f,  60.0f,  8500.0f, 1.30f, false, 9.0f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ═══════════ HALLS ═══════════
        // ── Lush Dark Hall ───────────────────────────────────────────────────
        // Anchor: Lexicon 480L "Hall A" warm dark variant.
        // Mod 0.22 / 0.55 Hz — the Lexicon signature random hall, post the
        // all-engine depth normalisation (6-AP was ×8 samples, now ×16,
        // so 0.45 is too much; 0.22 reproduces the original audible amount).
        // Hi-cut 8 k matches documented 480L 6-8 kHz HF cap; crossover
        // 550 Hz matches the 480L's 400-550 Hz bass band.
        // Tightened against VVV 84 Lush Vocal render comparison:
        //   • decay 3.80 → 3.30 — VVV measured 3.26s; ours was 4.11s with
        //     6-AP storage compensation. Brings perceived RT60 in line.
        //   • lo_cut 50 → 150 Hz — VVV cuts bass aggressively (-6.9 dB at
        //     125 Hz) for clean vocal-mix character. Ours was +2.4 dB bass
        //     body — too muddy on busy mixes.
        // Migrated from 6-AP (algo 1) → FDN (algo 3) on 2026-04-26 — same
        // reason as Cathedral (FDN eliminates the 6-AP loop-period flutter
        // on a smooth scoring-stage tail).
        //
        // Tuning for FDN dark scoring stage character:
        //   • mod_depth 0.22 → 0.12: FDN doesn't need wobble for smoothness.
        //   • damping 0.55 → 0.35: more aggressive treble damping = darker.
        //   • high_crossover 3500 → 2700: damping bites into upper mids.
        //   • hi_cut 8000 → 7000: enforces dark scoring-stage character.
        //   • width 1.30 → 1.40: wider stereo spread on FDN's decorrelated
        //     channels (still leaves headroom under the 1.50 max).
        { "Lush Dark Hall",       "Halls",
          3,  0.40f, false, 35.0f, 0,
          3.30f, 0.75f, 0.12f, 0.55f, 0.35f, 1.40f,  550.0f,
          0.75f, 0.25f, 0.55f, 150.0f, 7000.0f, 1.40f, false, -0.5f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 2700.0f, /* sat */ 0.20f },
        // ── Vocal Hall ───────────────────────────────────────────────────────
        // Anchor: Bricasti M7 "Vocal Hall".
        // Mid-size hall (2.2 s) with subtle modulation (0.20 / 0.7 Hz) for
        // movement without chorus. Lo-cut at 100 Hz keeps low rumble out of
        // the vocal mix. Diffusion 0.78 — smooth but not glassy.
        // Tightened against VVV Vocal Hall render comparison:
        //   • decay 2.20 → 3.50 — VVV's Vocal Hall measured 4.64s; ours was
        //     2.27s. Vocal halls in M7/Lex catalogues live at 3-5s, not 2s.
        //   • hi_cut 12500 → 9000 — VVV is much darker (-11.6 dB at 8 kHz
        //     vs our -3.7 dB). Tames sibilance on vocal returns.
        { "Vocal Hall",           "Halls",
          3,  0.35f, false, 22.0f, 0,
          3.50f, 0.55f, 0.20f, 0.70f, 0.70f, 1.15f, 1000.0f,
          0.78f, 0.45f, 0.55f, 100.0f,  9000.0f, 1.15f, false, -1.5f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Cathedral ────────────────────────────────────────────────────────
        // Anchor: Lexicon 224 "Concert Hall A" Notre-Dame setting (~6.5 s).
        // Migrated from 6-AP (algo 1) → FDN (algo 3) on 2026-04-26 because
        // the 6-AP figure-8 loop produces a 145-180 ms recirculation period
        // that smears only ~15 ms wide — leaving 130 ms gaps the brain
        // perceives as discrete delay echoes (verified by autocorrelation,
        // strength 0.90 at 7.83 ms lag). FDN's 16-channel Hadamard mixing
        // produces no audible loop period (L/R correlation ≈ 0 by
        // construction) — exactly what VVV uses for their hall presets.
        //
        // Tuning for FDN's strengths:
        //   • mod_depth 0.20 → 0.15: FDN tail is already smooth, less
        //     modulation needed to break periodicity.
        //   • damping 0.55 → 0.40: darker treble for true cathedral darkness.
        //   • high_crossover 3000 → 2400: damping curve extends down into
        //     upper mids (Notre-Dame is dark across the whole top half).
        //   • hi_cut 10000 → 8500: enforces 224-era hardware bandwidth cap.
        //   • width 1.35 → 1.50: maximum stereo spread (FDN's decorrelated
        //     channels can take more width without phase coherence loss).
        //   • Predelay 30 ms preserved (Valhalla cathedral convention).
        { "Cathedral",            "Halls",
          3,  0.45f, false, 30.0f, 0,
          6.50f, 0.95f, 0.15f, 0.45f, 0.40f, 1.30f,  750.0f,
          0.78f, 0.30f, 0.85f, 60.0f,  8500.0f, 1.50f, false, -3.5f,
          /* mono */ 20.0f, /* mid */ 1.20f, /* highX */ 2400.0f, /* sat */ 0.15f },
        // ── Blade Runner 224 ─────────────────────────────────────────────────
        // Anchor: Vangelis on the Lexicon 224 (Hall A / Constellation) —
        // "Tears in Rain" / "Memories of Green". Validated against the
        // Arturia Rev LX-24 "Large Hall" preset rendered through the same
        // noise-burst test signal.
        //
        // Architecture: Dattorro 2-AP (figure-8 cross-coupled topology, the
        // closest historical match to the 224's hardware tank).
        //
        // Reference targets from Arturia LX-24 measurement (BladeRunner
        // user preset rendered via Apple-native AU API on 2026-04-27):
        //   RT60               5.45 s
        //   Initial centroid   12 kHz at -16.7 dB (bright "Lexicon snap")
        //   Settled centroid   ~1.2 kHz by 1.5 s
        //   LR correlation     +0.00 (essentially mono-centered, natural)
        //
        // Validated 2026-04-27 against Arturia LX-24 BladeRunner preset
        // (rendered via direct AudioUnitSetParameter — Arturia's
        // setStateInformation is a no-op for non-JUCE state, verified by
        // post-load param dump). The actual user-tuned target measures:
        //   RT60 9.73 s, initial 12 kHz/-15.7 dB, LR mean ~0,
        //   LR stddev 0.028, mid-tail centroid 1700-2700 Hz.
        //
        // Tuning history:
        //   • Original (pre-2026-04-27): decay 10 / modDepth 0.35 / width 1.40
        //       — RT60 9.3 s but width=1.40 produced -0.33 LR anti-correlation
        //         (audible static phasiness)
        //   • Mis-targeted round (chased Arturia DEFAULTS, not user preset):
        //       decay → 4 / mod → 0.03 / damping → 1.0 / diffusion → 0.50
        //       — RT60 collapsed to 5 s, way short of true target
        //   • This round (chases REAL user-tuned Arturia):
        //       decay → 9 / diffusion → 0.85 (smoother sustained tail)
        //       Keeping width=1.0 (phasiness fix) + mod 0.03 / 0.45 Hz (tight
        //       LR jitter) + Hi Cut 10 kHz / damping 1.0 (HF preserved) +
        //       erLevel 1.0 (bright ER burst) + highX 5 kHz.
        //
        // Current measurements vs target:
        //   metric        ours    arturia
        //   RT60          9.17s   9.73s     (within 6%)        ✓
        //   LR mean       -0.02   -0.01     (matched)          ✓
        //   spec.flux     224     222       (within 1%)        ✓
        //   centroid 250  3805    3786 Hz   (matched)          ✓
        //   centroid 500  3625    3688 Hz   (matched)          ✓
        //   LR stddev     0.066   0.028     (2.3× too noisy)   — needs tank
        //                                                        modulation
        //                                                        DSP work
        //   initial cent  7.7kHz  12 kHz    (4.5 kHz darker)   — needs ER
        //                                                        engine HF
        //                                                        biasing
        //   initial RMS   -22dB   -16dB     (7 dB quieter)     — likely DV's
        //                                                        Dry/Wet curve
        //   tail @ 5s     -68dB   -76dB     (8 dB hotter)      — could trim
        //                                                        gain ~3 dB
        { "Blade Runner 224",     "Halls",
          0,  0.45f, false, 24.0f, 0,
          9.00f, 1.00f, 0.03f, 0.45f, 0.50f, 2.00f,  800.0f,
          0.85f, 1.00f, 1.00f, 60.0f,  6500.0f, 1.00f, false, 7.0f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 5000.0f, /* sat */ 0.00f },
        // ── Realistic Chamber ────────────────────────────────────────────────
        // Anchor: Bricasti M7 "Chamber" preset.
        // Clean and high-diffusion (0.85), almost no modulation (0.10 / 0.8 Hz),
        // full top-end response, prominent realistic ER pattern. Sounds like
        // a real room.
        { "Realistic Chamber",    "Chambers",
          3,  0.30f, false, 14.0f, 0,
          1.40f, 0.50f, 0.10f, 0.80f, 0.85f, 1.10f, 1100.0f,
          0.85f, 0.65f, 0.50f, 60.0f, 14000.0f, 1.10f, false, 2.0f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 5000.0f, /* sat */ 0.05f },
        // ═══════════ CHAMBERS ═══════════
        // ── Wood Chamber ─────────────────────────────────────────────────────
        // Anchor: Capitol Records / Abbey Road live wooden chambers (1950s-60s).
        // Warm wood-panel resonance — bass-mult 1.2, treble-mult 0.65 darken
        // the top while sustaining low-mids. 5 % LFO at 0.6 Hz suppresses
        // QuadTank metallic ring without audible vibrato.
        // Tightened against VVV 79 Acoustic Chamber render comparison:
        //   • decay 1.60 → 2.30 — VVV measured 2.54s; ours was 1.70s.
        //     Live wooden chambers (Capitol, Abbey Road) ran 2-3s typical.
        //   • lo_cut 70 → 150 Hz — cleaner room bass, matches VVV's bass
        //     cut (-6.9 dB at 125 Hz vs ours -2.1 dB).
        { "Wood Chamber",         "Chambers",
          2,  0.30f, false, 18.0f, 0,
          2.30f, 0.40f, 0.05f, 0.60f, 0.65f, 1.20f,  850.0f,
          0.80f, 0.55f, 0.45f, 150.0f, 11500.0f, 1.15f, false, 0.5f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 4000.0f, /* sat */ 0.20f },
        // ── 80s Non-Lin Drum ─────────────────────────────────────────────────
        // Anchor: AMS RMX16 "NonLin 2" (Phil Collins / Genesis snare).
        // Brutally tight (0.3 s), zero modulation, MAXED diffusion, MASSIVE
        // early-reflection wall (0.85). Zero predelay — the room slams onto
        // the transient instantly. Hi-cut at 13 k preserves the snap.
        // Tightened against VVV Gated Snare render comparison:
        //   • mod_depth 0.05 → 0.20 — VVV's gated character runs σ 3.46 dB,
        //     ours was 0.76 dB. The "80s gated drum" sound IS modulated
        //     (RMX16 nonlin had heavy chorus on the tail).
        //   • hi_cut 13000 → 8000 — VVV is much darker (-10 dB at 8 kHz vs
        //     ours -3.9 dB). 80s gated snare is a DARK sound — the cut
        //     keeps the fizz under the snare crack.
        { "80s Non-Lin Drum",     "Rooms",
          2,  0.30f, false,  0.0f, 0,
          0.30f, 0.15f, 0.20f, 1.00f, 0.95f, 0.85f, 1500.0f,
          1.00f, 0.85f, 0.20f, 120.0f,  8000.0f, 1.20f, false, 4.0f,
          /* mono */ 20.0f, /* mid */ 0.80f, /* highX */ 3500.0f, /* sat */ 0.40f },
        // ── Vocal Booth ──────────────────────────────────────────────────────
        // Sub-second tight close-mic room for intimate vocals / podcasts /
        // dialogue. Engine matched to VVV's 84SmallRoom architecture
        // (forensic L/R-correlation analysis: VVV uses FDN for small rooms).
        // Short predelay + dense ER + 0.4 s decay gives the close-mic-with-
        // walls character without the boxy ringing of a literal-physical
        // simulation. If subjective listening reveals the FDN smoothness
        // robs the percussive-slap feel we'd want for a real wooden booth,
        // pivot to QuadTank algo=2 with same parameter values.
        { "Vocal Booth",          "Rooms",
          3,  0.30f, false,  2.0f, 0,
          0.40f, 0.20f, 0.05f, 0.40f, 0.80f, 0.95f,  800.0f,
          0.65f, 0.55f, 0.20f, 120.0f, 12000.0f, 1.00f, false, 4.0f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4500.0f, /* sat */ 0.05f },
        // ═══════════ ROOMS ═══════════
        // ── Tight Drum Room ──────────────────────────────────────────────────
        // Anchor: AMS RMX16 "Ambience" / classic small wooden booth.
        // QuadTank delivers the tight character of small spaces. Strong ER
        // (0.65) defines the room; the 0.5 s late tail is almost incidental.
        // 5 % LFO at 0.6 Hz breaks tank phase-locks — required for QuadTank
        // to avoid the metallic ring on short decays.
        // Mod 0.10 (was 0.05): same modal-locking-margin bump as Bright
        // Drum Plate; QuadTank measured σ 1.12 dB at 0.05 → just above floor.
        { "Tight Drum Room",      "Rooms",
          2,  0.25f, false,  4.0f, 0,
          0.50f, 0.20f, 0.10f, 0.60f, 0.95f, 0.95f, 1500.0f,
          0.65f, 0.65f, 0.30f, 100.0f, 14000.0f, 1.05f, false, 4.0f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4500.0f, /* sat */ 0.10f },
        // ── Studio Room ──────────────────────────────────────────────────────
        // Anchor: Quantec QRS "Studio Room" (1982) — first true room simulator.
        // Dense realistic early reflections, very high diffusion (0.85), no
        // modulation, wide stereo from natural decorrelation. 0.9 s decay
        // emulates a controlled tracking room.
        // Tightened against VVV 84 Small Room render comparison:
        //   • decay 0.90 → 0.60 — VVV's tight studio rooms live at 0.4-0.6s.
        //     Ours at 0.9s read more like a "medium room" than "studio room".
        //   • hi_cut 14500 → 9000 — VVV is much darker (-10 dB at 8 kHz vs
        //     ours -1.9 dB). Tight rooms in pro work are darker not brighter.
        { "Studio Room",          "Rooms",
          3,  0.30f, false,  8.0f, 0,
          0.60f, 0.30f, 0.00f, 1.00f, 0.85f, 1.05f, 1300.0f,
          0.85f, 0.60f, 0.40f, 80.0f,  9000.0f, 1.05f, false, 4.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 5000.0f, /* sat */ 0.05f },
        // ── Ambience (PCM 90) ────────────────────────────────────────────────
        // Engine: QuadTank. Anchor: PCM 90 "Ambience" (Bank P1) — the famous
        // Lexicon 300L algorithm imported into the PCM 90. The QuadTank's
        // 4-tank early-reflection clusters match the Ambience character
        // better than the diffuser-driven Dattorro / FDN engines.
        //   RT60 0.54 s   bass_mult 0.89   treble_mult 1.08 (slight HF persist)
        //   centroid 50ms 11.2 kHz   centroid 1s 10.7 kHz (stays bright)
        //   diffusion-proxy 2.11 (DENSE early field — the Ambience signature)
        //   shape: GATED (cliff envelope at the natural-room cutoff)
        //   predelay 1 ms
        // High ER level (0.70) recreates the dense early field. Modulation
        // kept very low — Ambience character is about precise spatial
        // imaging, not warble.
        { "Ambience",             "Rooms",
          2,  0.40f, false,  1.0f, 0,
          0.60f, 0.40f, 0.05f, 0.45f, 1.05f, 0.90f,  900.0f,
          0.65f, 0.70f, 0.50f, 100.0f, 14000.0f, 1.20f, false, 3.5f,
          /* mono */ 20.0f, /* mid */ 1.05f, /* highX */ 5000.0f, /* sat */ 0.10f },
        // ── Drum Room (PCM 90) ───────────────────────────────────────────────
        // Engine: QuadTank. Anchor: PCM 90 "Drum Room" (Bank P1) — physical
        // weight for acoustic drums without washing them out.
        //   RT60 0.56 s   bass_mult 1.12 (slight bass body)   treble_mult 0.93
        //   centroid 50ms 11.6 kHz (bright drum transients pass through)
        //   diffusion-proxy 1.07   shape: GATED   predelay 0
        // Distinct from "Tight Drum Room" (FDN-based, longer): this is the
        // shorter, more present PCM-style drum room with heavy ER cluster.
        { "PCM Drum Room",        "Rooms",
          2,  0.40f, false,  0.0f, 0,
          0.60f, 0.35f, 0.10f, 0.50f, 0.90f, 1.10f,  900.0f,
          0.70f, 0.75f, 0.40f, 100.0f, 12000.0f, 1.15f, false, 4.0f,
          /* mono */ 20.0f, /* mid */ 1.05f, /* highX */ 5000.0f, /* sat */ 0.10f },
        // ── 1981 Gated Snare ─────────────────────────────────────────────────
        // Engine: NonLinear v6 (algo 5) — TRUE STATIC FIR. The envelope
        // (attack ramp → flat plateau → mathematical cliff) is baked into
        // the per-tap gains, so EVERY input sample is convolved with the
        // same fixed-shape FIR. No trigger, no envelope follower — this is
        // the AMS RMX16 NonLin algorithm exactly as Hugh Padgham used it
        // on Phil Collins's "In The Air Tonight" (1981).
        //
        // FIR ENVELOPE PARAMETERS (re-purposed UI knobs on this engine —
        // see PluginEditor::applyEngineAccent for the visible labels):
        //   ATTACK    mod_depth 0.15  →   3 ms ramp up
        //   HOLD      decay 0.150     → 150 ms flat plateau
        //   RELEASE   mod_rate 2.30   →  15 ms hard cliff to silence
        //   DENSITY   diffusion 0.50  → 50% tap jitter / L-R decorrelation
        //
        // Other voicing:
        //   bass_mult 1.00       → neutral output gain
        //   hi_cut 14000         → bright snare crack passes through
        //   width 1.40           → wide stereo (per-tap L/R decorrelation)
        //   mono < 100 Hz        → tight bass focus
        //   saturation 0.20      → analog crunch (RMX16 had a noisy front-end)
        // v7: REAL hall + sidechain noise gate (Townhouse Studios technique).
        //   HALL: decay 1.5 s, size 0.70, bass 1.0, treble 0.80 (slight darkening)
        //   GATE: threshold -32 dB (mid 0.75), attack 1 ms (mod_depth 0.0),
        //         hold 150 ms (diffusion 0.30), release 210 ms (mod_rate 1.117)
        //   This is the "In The Air Tonight" Phil Collins/Padgham/Townhouse sound:
        //   thick hall bloom for 150 ms then a longer fade to silence.
        { "1981 Gated Snare",     "Rooms",
          5,  1.00f, false,  0.0f, 0,
          1.50f, 0.70f, 0.00f, 1.117f, 0.80f, 1.00f,  500.0f,
          0.30f, 0.00f, 0.00f,  60.0f, 14000.0f, 1.40f, false, 0.0f,
          /* mono */ 100.0f, /* mid */ 0.75f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── In The Air Tonight ──────────────────────────────────────────────
        // Engine: NonLinearEngine v7 (algo 5). Reference: Hugh Padgham's
        // Townhouse Studios technique on Phil Collins's "In The Air Tonight"
        // (1981) — the iconic gated snare sound. Big hall + sidechain
        // noise gate. Tuned by ear:
        //   HALL: decay 2.61 s, size 0.80, bass 1.10, treble 0.75 (slight darkness)
        //   GATE: enabled, threshold via mid (0.75), attack 8 ms (mod_depth 0.092),
        //         hold 250 ms (diffusion 0.50), release 150 ms (mod_rate 0.794)
        //   Mix 22 % — light send/return blend, dry-forward.
        { "In The Air Tonight",   "Rooms",
          5,  0.216f, false,  0.0f, 0,
          2.608f, 0.80f, 0.092f, 0.794f, 0.75f, 1.10f,  500.0f,
          0.50f, 0.00f, 0.30f,  60.0f, 10000.0f, 1.30f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 0.75f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Reverse Taps (PCM 90) ────────────────────────────────────────────
        // Engine: NonLinear (algo 5) in REVERSE mode (diffusion 0.33-0.66
        // selects the reverse envelope). Anchor: PCM 90 "Reverse Taps"
        // (Bank P3, Post). Inverse algorithm — energy SWELLS UPWARD before
        // a hard cutoff.
        //   RT60 0.95 s (gate window length)   bass_mult 1.01 (flat)
        //   centroid 50ms 4.1 kHz   centroid 1s 1.7 kHz (darkens dramatically)
        //   shape: REVERSE — peak energy at end of window
        //   predelay 189 ms in the IR (the swell start) — but our preset's
        //   predelay slot is for the small offset before the reverse build
        //   begins; the engine handles the swell shape itself.
        //   LR correlation -0.21 (slightly anti-correlated for stereo width)
        // Most ambient of the gated presets — long swell + long fade.
        // v7: REAL hall + gate. The most ambient of the gated presets —
        // long hall + slow attack + max hold + long release = a swelling
        // wash that fades naturally rather than a hard cliff.
        //   HALL: decay 3.0 s, size 0.85, bass 1.0, treble 0.70
        //   GATE: threshold -32 dB (mid 0.75), attack 25 ms (mod_depth 0.49),
        //         hold 500 ms (diffusion 1.0, max), release 1500 ms (mod_rate 7.52)
        { "Reverse Taps",         "Rooms",
          5,  1.00f, false, 30.0f, 0,
          3.00f, 0.85f, 0.49f, 7.52f, 0.70f, 1.00f,  500.0f,
          1.00f, 0.00f, 0.30f,  80.0f,  8000.0f, 1.30f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 0.75f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Mobius Pad ───────────────────────────────────────────────────────
        // Named after the Möbius Twist DSP (sign-inverted cross-feedback —
        // see SixAPTankEngine.cpp). Showcases the 6-AP engine's new
        // Möbius Twist + Bloom + Stereo Bind architecture. 5.5 s decay
        // positioned between Ambient Swell (8 s) and Infinite Blackhole
        // (18 s) so it stays playable for synth players who want infinite
        // stereo width without committing to film-score-length tails.
        // Perpetually anti-correlated stereo (won't collapse to mono on
        // long sustains), volume blooms 200-300 ms into the tail. For
        // sustained synth pads, ambient guitar swells, evolving textures.
        // (ASCII name — keeps shell + UTF-8 toolchain matching reliable.)
        { "Mobius Pad",           "Ambient",
          1,  0.45f, false, 45.0f, 0,
          5.50f, 0.90f, 0.40f, 0.35f, 0.45f, 1.50f,  500.0f,
          0.85f, 0.20f, 0.85f,  80.0f,  9000.0f, 1.50f, false, 4.5f,
          /* mono */ 80.0f, /* mid */ 1.20f, /* highX */ 3200.0f, /* sat */ 0.10f },
        // ═══════════ AMBIENT ═══════════
        // ── Ambient Swell ────────────────────────────────────────────────────
        // Anchor: Lexicon 480L "Random Hall" toward infinite reverb.
        // 8 s decay, deep slow modulation, big predelay — pushes the wet far
        // behind the dry. Bus mode ON so users route a pre-fader send into
        // 100 % wet without re-balancing the mix. 80 Hz MONO BELOW keeps the
        // deep tail from spreading bass across the stereo image.
        // Mod 0.28 / 0.40 Hz — recalibrated after depth normalisation.
        // Tightened against VVV 84 Replicant render comparison:
        //   • hi_cut 8500 → 5500 — VVV is much darker (-10.7 dB at 8 kHz vs
        //     ours -4.5 dB). Ambient/cinematic reverbs need this darkness
        //     to sit behind the source rather than fighting it.
        //   • lo_cut 80 → 150 — keeps cinematic low end clean instead of
        //     building up muddy bass over the long tail.
        { "Ambient Swell",        "Ambient",
          1,  0.50f, false, 60.0f, 0,
          8.00f, 0.92f, 0.28f, 0.40f, 0.60f, 1.50f,  600.0f,
          0.80f, 0.10f, 0.75f, 150.0f, 5500.0f, 1.45f, false, 4.0f,
          /* mono */ 80.0f, /* mid */ 1.20f, /* highX */ 3500.0f, /* sat */ 0.15f },
        // ── Black Hole ───────────────────────────────────────────────────────
        // Reference: Valhalla Shimmer "BlackHole" factory preset — one of
        // the 8 stock presets shipped with VS. Huge dark deep ambient void;
        // signature VS sound for cinematic sustains, drones, and synth pads.
        // Tuned against VS reference render (DBG_BlackHole_*):
        //   • Algorithm 1 (SixAPTank, 6-allpass cascaded diffuser) —
        //     structurally matches VS BlackHole's Schroeder cascaded-allpass
        //     architecture. Algorithm 3 (FDN) had a 30 ms silence gap before
        //     the burst onset that didn't match VS's continuous early rise.
        //   • Zero pre-delay — VS BlackHole has reverb starting immediately.
        //   • 14 s decay + size 0.95 for the slow tail decay (~4 dB/s).
        //   • Brightness profile (post-engine-tuning iteration):
        //     - damping (treble multiply) = 1.0 — treble decays at same rate
        //       as mid; air persists into deep tail to match VS's broadband
        //       sustain. Earlier iteration (0.45) had treble decaying 2.2×
        //       faster, killing >6 kHz content by 1-2 s in.
        //     - midMult = 1.10 — mid decay 10% slower; presence band rings
        //       longer, adds "information" to the tail.
        //     - highCrossover = 8 kHz — pushes mid-shelf turnover above the
        //       presence peak; sparkle band stops getting damped early.
        //     - hiCut = 18 kHz (effectively bypassed) — VS BlackHole has
        //       full-bandwidth content above 12 kHz late in the tail; lower
        //       cuts (e.g. 6.5 kHz) collapsed the >12 kHz tail to silence.
        //     - modRate = 0.60 Hz — slight LFO movement adds shimmer; was
        //       0.40 Hz which felt too still.
        // Differs from "Infinite Blackhole" (Eventide-anchored, 18 s, 6-AP
        // with heavy modulation) by being shorter, less modulated, and
        // tuned to VS's specific factory voicing rather than Eventide's.
        // Engine-config overrides (sixAP*) close the >12 kHz late-tail gap
        // (was 16 dB cold vs VS) by injecting more bright ParallelDiffuser
        // content directly into the output (kEarlyMix 0.5→0.75) and letting
        // the density cascade ring denser at later stages (kBloomCeiling
        // 0.85→0.92, steeper stagger). These don't affect other SixAPTank
        // presets because they default to the historical hardcoded values.
        { "Black Hole",           "Ambient",
          1,  0.50f, false,   0.0f, 0,
          14.00f, 0.95f, 0.35f, 0.60f, 1.00f, 1.10f,  700.0f,
          0.85f, 0.05f, 0.70f,  60.0f, 18000.0f, 1.40f, false, -2.0f,
          /* mono */ 60.0f, /* mid */ 1.10f, /* highX */ 8000.0f, /* sat */ 0.08f,
          /* gate */ true,
          /* sixAPDensityBaseline */ 0.72f,
          /* sixAPBloomCeiling    */ 0.92f,
          /* sixAPBloomStagger    */ { 0.65f, 0.78f, 0.92f, 1.05f, 1.18f, 1.30f },
          /* sixAPEarlyMix        */ 0.75f,
          /* sixAPOutputTrim      */ 1.10f },
        // ── Infinite Blackhole ───────────────────────────────────────────────
        // Anchor: Eventide H8000 / Blackhole effect.
        // 18 s decay at maximum size + 6-AP density + heavy modulation
        // (75 % / 0.3 Hz) creates an evolving infinite space. 120 ms predelay
        // separates the dry source from the void. Hi-cut at 7.5 k keeps the
        // tail dark and atmospheric. Bus mode ON for send/return use.
        // 100 Hz MONO BELOW prevents the gigantic tail from smearing low-end
        // stereo. Mod 0.35 / 0.30 Hz aligns with documented Blackhole
        // factory-preset 25-40 % depth at 0.3-0.8 Hz.
        // Pre-delay 120 → 85 ms. 120 ms = 1/8 note slapback at 125 BPM —
        // long enough to feel like a separate musical event (rhythmic
        // hiccup) before the 18 s tail begins. 85 ms still gives the dry
        // transient room to breathe but lets the wall-of-sound attach to
        // the source more naturally.
        { "Infinite Blackhole",   "Ambient",
          1,  0.55f, false, 85.0f, 0,
          18.00f, 1.00f, 0.35f, 0.30f, 0.55f, 1.60f,  550.0f,
          0.90f, 0.05f, 0.80f, 100.0f,  7500.0f, 1.50f, false, -1.0f,
          /* mono */ 100.0f, /* mid */ 1.30f, /* highX */ 3000.0f, /* sat */ 0.25f },
        // ── Cascading Heaven ─────────────────────────────────────────────
        // +24 semitones (two-octave stack) at ~57% feedback. No VS factory
        // direct equivalent — kept as our differentiator. Lower feedback
        // than +12 (cascade builds 4× faster at 4× pitch ratio), longer
        // decay (6 s) for the stacked-octave swell, slightly darker hi-cut
        // (6 kHz) to keep the upper-octave stack from glassing up.
        { "Cascading Heaven",     "Shimmer",
          6,  0.361f, false,  60.0f, 0,
          6.00f, 0.85f, 1.00f, 2.705f, 0.95f, 1.10f,  800.0f,
          0.85f, 0.20f, 0.50f,  60.0f,  6000.0f, 1.40f, false, -3.0f,
          /* mono */ 60.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Deep Blue Day ────────────────────────────────────────────────
        // Reference: Valhalla Shimmer "DeepBlueDay" preset (named after the
        // Brian Eno track on *Apollo: Atmospheres and Soundtracks*). 80% wet,
        // +12 octave, ~45% feedback, very long sustained tail. Decay 10.3 s
        // + size 100% gives the long sustained character; lower feedback
        // (45%) keeps the cascade gentle so the long reverb dominates over
        // the pitched recirculation.
        // mod_depth 0.5 = +12 st; mod_rate 4.5 Hz maps to feedback ≈ 0.42.
        { "Deep Blue Day",        "Shimmer",
          6,  0.38f, false,  25.0f, 0,
          10.30f, 1.00f, 0.50f, 2.395f, 1.00f, 1.10f,  800.0f,
          0.85f, 0.20f, 0.50f,  60.0f,  7000.0f, 1.30f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.05f },
    };
    return presets;
}
