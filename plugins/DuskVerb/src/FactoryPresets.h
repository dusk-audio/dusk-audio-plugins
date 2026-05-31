#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <map>
#include "dsp/DspUtils.h"

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
    // Phase 1 post-tank high-shelf attenuation depth (dB, [-24, 0]).
    // Placed here (after saturation, before gateEnabled) so SHORT-form
    // preset rows can append it as a single trailing brace-init value
    // without writing out the sixAP + DPV intermediates. Default -12 dB
    // is the universal shelf depth that produced the net-zero gate delta
    // in Phase 1 audit; per-preset overrides via brace-init.
    float hiCutShelfGainDb = -12.0f;

    // Phase 2 modulation topology selector — NOT a struct field. Per-preset
    // mapping lives in PluginProcessor.cpp::FactoryPreset::applyEngineConfig
    // via a static name → topology map. Avoids breaking the FactoryPreset
    // aggregate-initializability that the brace-init preset list relies on.
    bool  gateEnabled    = true;     // NonLinear engine: true = gate active.
                                     // No-op on other engines but written
                                     // anyway so loading a preset always
                                     // sets the toggle to a known state.

    // SixAPTank-specific engine tunables. Defaults match the engine's
    // historical hardcoded constants — so any preset that doesn't override
    // these gets identical sound to before. Black Hole opts in to brighter,
    // denser values for external reference blackhole-character late-tail content.
    float sixAPDensityBaseline = 0.62f;
    float sixAPBloomCeiling    = 0.85f;
    float sixAPBloomStagger[6] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f };
    float sixAPEarlyMix        = 0.5f;
    float sixAPOutputTrim      = 1.3f;

    // In-loop bass-choke HPF cutoff (Hz). Only the legacy HighDensityPlate
    // engine used this; current engines ignore it. 20 Hz = effective
    // bypass. Placed last so existing brace-init preset rows don't need
    // to grow trailing arguments — every row picks up the default.
    float bassChoke            = 20.0f;

    // DattorroPlateVintage (algo 1) per-preset brightness + corrective EQ
    // controls. The only algo-1 factory preset (Vintage Vocal Plate)
    // brace-inits its own DPV values, so these struct defaults are
    // placeholders for any future algo-1 preset only.
    // Non-algo-1 presets ignore these (engine glue forwards to a no-op).
    float dpvHfShelfGainDb     = 22.0f;
    float dpvHfShelfFreqHz     = 6000.0f;
    float dpvStructHfDampHz    = 8000.0f;
    float dpvBoxCutGainDb      = -3.5f;
    float dpvBoxCutFreqHz      = 320.0f;
    float dpvBassShelfGainDb   = 0.0f;
    float dpvBassShelfFreqHz   = 180.0f;

    // FiveBandDamping (Phase 2, FDN/algo-4 only). Sentinel defaults of -1 for
    // the multipliers mean "inherit" (sub → bassMult, hi-mid → Treble/damping)
    // so every existing brace-init preset row — none of which reach these
    // trailing fields — stays bit-for-bit transparent (collapses to the legacy
    // 3-band). crossoverSub/Air default to boundaries that sit between
    // equal-gain bands at the inherited mults. Non-FDN engines ignore all four.
    float subMult      = -1.0f;   // <0 → inherit bassMult (transparent)
    float hiMidMult    = -1.0f;   // <0 → inherit Treble Multiply (transparent)
    float crossoverSub = 120.0f;  // sub ↔ low-mid boundary (Hz)
    float crossoverAir = 8000.0f; // hi-mid ↔ air boundary (Hz)

    // Block 2 feed-forward input energy makeup (dB). 0 = bypass; trailing
    // fields → existing brace-init rows default to bypass, zero edits.
    float inputSubGain = 0.0f;
    float inputMidGain = 0.0f;

    // Phase γ (2026-05-29): per-preset post-tank band-trim region gains —
    // NOT struct fields (would break aggregate-init of every existing
    // brace-init preset row). Per-preset overrides live in PluginProcessor.cpp::
    // FactoryPreset::applyEngineConfig via a static name → 4-float-region
    // map (kPostBandTrimByName). Presets not in the map get an all-zero-
    // dB trim → bit-identical bypass.

    // Post-tank parametric EQ (4 bands × {freqHz, Q, gainDb}) — NOT a struct
    // field for the same reason as Phase 2 modulation topology: adding 12
    // array entries to FactoryPreset would break the aggregate-init of every
    // existing brace-init preset row that doesn't reach into DPV territory.
    // Per-preset overrides live in PluginProcessor.cpp::FactoryPreset::
    // applyEngineConfig via a static name → bands map (kPostTankEQByName).
    // Presets not in the map get an all-zero-gain EQ → bit-identical bypass.

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
        // FiveBandDamping. Base = sentinel-inherit (transparent: sub→bass,
        // hi-mid→treble). Per-preset NON-transparent values live in a
        // name→map — same pattern as kPostTankEQByName — so the fragile
        // aggregate-init rows (these struct fields sit last) stay untouched.
        struct FiveBandOverride { float sub, hiMid, xSub, xAir; };
        static const std::map<juce::String, FiveBandOverride> kFiveBandByName = {
            // Drum Plate (FDN) — direct-scoreboard sweep, 28→26 vs VVV anchor.
            { "Drum Plate", { 0.5349f, 0.8907f, 67.45f, 15219.49f } },
        };
        float fbSub   = subMult   >= 0.0f ? subMult   : bassMult;
        float fbHiMid = hiMidMult >= 0.0f ? hiMidMult : damping;
        float fbXSub  = crossoverSub;
        float fbXAir  = crossoverAir;
        if (auto it = kFiveBandByName.find (juce::String (name)); it != kFiveBandByName.end())
        {
            fbSub  = it->second.sub;   fbHiMid = it->second.hiMid;
            fbXSub = it->second.xSub;  fbXAir  = it->second.xAir;
        }
        setIfExists ("sub_mult",      fbSub);
        setIfExists ("hi_mid_mult",   fbHiMid);
        setIfExists ("crossover_sub", fbXSub);
        setIfExists ("crossover_air", fbXAir);
        // Low-Band Transient Shaper — Phase A: every preset bypasses (depth 0).
        // Phase B will set per-preset depths once the detector is wired.
        setIfExists ("transient_shaper", 0.0f);
        setIfExists ("shaper_time",      120.0f);
        setIfExists ("shaper_xover",     250.0f);
        setIfExists ("shaper_sens",      1.5f);
        // Block 2 input makeup — 0 dB bypass until per-preset tuning sets it.
        setIfExists ("input_sub_gain",   inputSubGain);
        setIfExists ("input_mid_gain",   inputMidGain);
        setIfExists ("saturation", saturation);
        setIfExists ("diffusion", diffusion);
        setIfExists ("er_level",  erLevel);
        setIfExists ("er_size",   erSize);
        setIfExists ("lo_cut",    loCut);
        setIfExists ("hi_cut",    hiCut);
        setIfExists ("width",     width);
        setIfExists ("gain_trim", gainTrim);
        setIfExists ("mono_below", monoBelow);
        // DPV corrective EQ + brightness — only audible when algorithm=1
        // routes through DattorroPlateVintage. Other engines forward to
        // no-op setters; safe to set unconditionally.
        setIfExists ("dpv_hf_shelf_db",        dpvHfShelfGainDb);
        setIfExists ("dpv_hf_shelf_hz",        dpvHfShelfFreqHz);
        setIfExists ("dpv_struct_hf_damp_hz",  dpvStructHfDampHz);
        setIfExists ("dpv_box_cut_db",         dpvBoxCutGainDb);
        setIfExists ("dpv_box_cut_hz",         dpvBoxCutFreqHz);
        setIfExists ("dpv_bass_shelf_db",      dpvBassShelfGainDb);
        setIfExists ("dpv_bass_shelf_hz",      dpvBassShelfFreqHz);
        setIfExists ("hi_cut_shelf_db",        hiCutShelfGainDb);
        // Phase γ post-tank per-band trim — per-preset overrides live in
        // PluginProcessor.cpp::kPostBandTrimByName and are applied via
        // engine.setPostTankBandTrimGainDb from applyEngineConfig. Presets
        // not in that map keep the APVTS default (0 dB) → bit-identical
        // bypass on every region.

        // Phase ζ + η per-preset opt-ins disabled — debug pass identified
        // the root cause: ζ pre-Hadamard peaking at +10.88 dB DOES amplify
        // the impulse response massively (+94 dB at the 1k band, verified
        // 2026-05-29) but is partially capped by softClip on sustained
        // signals like sine1k. Sweep loss measurement included transient-
        // dependent gates that were optimistic about steady-state benefit.
        // Applied as-is, the impulse explosion broke 21+ transient gates
        // (snare RMS / spec_L1 / EDT / boom / etc.) for a +2 dB sine1k
        // gain. Net regression. ζ+η infrastructure remains live for future
        // presets where the impulse/sustained trade-off is favorable.
        //
        // Defensively write APVTS to engine-bypass defaults on every
        // preset apply so a prior preset's user-overridden values don't
        // carry through to the next preset swap.
        setIfExists ("in_loop_peak_hz",    1000.0f);
        setIfExists ("in_loop_peak_q",        2.0f);
        setIfExists ("in_loop_peak_db",       0.0f);
        setIfExists ("bass_shelf_fast_fc",  400.0f);
        setIfExists ("bass_shelf_slow_fc",  200.0f);
        setIfExists ("bass_shelf_fast_db",    0.0f);
        setIfExists ("bass_shelf_slow_db",    0.0f);
        setIfExists ("bass_shelf_transition_ms", 100.0f);
    }

    // Apply engine-specific (non-APVTS) tunables. Currently only the
    // SixAPTank brightness/density fields. Defined out-of-line in
    // PluginProcessor.cpp where DuskVerbEngine's full type is visible.
    void applyEngineConfig (DuskVerbEngine& engine) const;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        // ── Vocal Plate (VVV anchor) ───────────────────────────────────────
        // Engine: FDN. Anchor: Valhalla Vintage Verb "Vocal Plate" preset
        // (Reverb Mode = Plate) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Plates,
        // has_dpv=False (FDN engine, DPV params stripped). 1300 trials.
        //   Stage 1 loss 4.50 (short plate envelope is noisier vs halls)
        //   Stage 2 loss 140.99 (per-band decay tight on plate)
        //   Stage 3 loss 2.13 (cleanest Stage 3 polish to date)
        //
        // Locked 2026-05-30: direct-scoreboard + warm-start FDN sweep,
        // 34 → 24 gate fails vs VVV "Vocal Plate" anchor (verified clean
        // render). Two sweeps (cold→24, warm+fresh-RNG→can't beat 24)
        // converge. Residual 24 is structural — HF T60 overshoot the single
        // trebleMult can't bend (16k wants 0.40s, FDN gives 0.59s) + a sub
        // energy/decay deficit — not closable by uniform FDN damping.
        { "Vocal Plate",          "Plates",
          4,  0.35f, false, 29.16f, 0,
          1.02f, 0.15f, 0.37f, 1.51f, 0.85f, 0.74f,  401.0f,
          0.58f, 0.25f, 0.76f,  81.5f, 16667.0f, 1.02f, false, -1.74f,
          /* mono */ 20.0f, /* mid */ 0.72f, /* highX */ 3817.0f, /* sat */ 0.03f },
        // ═══════════ PLATES ═══════════
        // ── Vintage Vocal Plate ──────────────────────────────────────────────
        // Anchor: vintage rack-style algorithmic reverb "VintagePlate / Vocal Plate"
        // (default factory preset, 01.Vocal Plates / 000.Vocal Plate).
        // Engine: DattorroPlateVintage (algo 1) — Dattorro tank + dedicated
        // pre-tank HF shelf + in-loop structural HF damping + post-tank
        // 320 Hz box cut. No other factory preset uses this engine.
        //
        // 2026-05-24 calibration vs vintage plate hardware anchor via host-saved .fxp loaded
        // through the harness (--load-state on the reference VST2 chunk format
        // — JUCE auto-unwraps the FPCh wrapper). Reference IR rendered
        // at 100 % wet (Mix=1.0 override on top of the fxp).
        //
        // v9 (2026-05-27): staged_tuner.py 3-stage CMA-ES sweep against the
        // Lex VVP .fxp anchor, 1300 trials, 6 workers. Architecture:
        //   Stage 1 (Spatial+Envelope): Size, Diffusion, Width, ModD, ModR,
        //                               Decay seeded from anchor RT60. Loss
        //                               = env_shape_L1 + stereo_gap + osc_p2p
        //   Stage 2 (Temporal+Decay): Decay, Mults, Crossovers, DPV HF Damp.
        //                             Loss = per-band EDT/t30/t60, low_mid×3.
        //   Stage 3 (Spectral+Polish): Lo/Hi Cut, Sat, Trim, DPV shelves
        //                              CLAMPED to [-6,+6] dB (no cheat path).
        // Result: 9 / 19 listening-relevant gates close. Remaining failures
        // are DOCUMENTED ENGINE CEILINGS — DattorroPlateVintage architecture
        // boundaries vs Lexicon Vintage Plate's MTDL topology:
        //   - cent_50  Δ -41 %   DPV can't push 50ms HF persistence to Lex's
        //                        5191 Hz centroid without the +12 dB HF Shelf
        //                        cheat that the v9 architecture forbids.
        //   - edt low_mid -87 %  Lex holds 250-500 Hz at 126 ms early-decay;
        //                        DPV's coupled HF-damping / decay structure
        //                        collapses to 16 ms. No knob bridges this.
        //   - osc P2P Δ -9.4 dB  Lex modulates loop topology (±22 dB envelope
        //                        pumping); DV uses per-line random-walk LFOs
        //                        (±13 dB). Architectural — not a tuner gap.
        // ALL TEMPORAL GATES (tail_t30, tail_t60, per-band decay sub..hi)
        // and STEADY-STATE LOW-MID PASS — the perceptual sweet spot the
        // earlier non-staged sweep was missing.
        // Gain Trim = 6.7 (user's ear-calibrated value, preserved over
        // optimizer's 9.49 — the +2.79 dB difference is the math-vs-perception
        // gap the snare-RMS gate caught on prior calibrations).
        { "Vintage Vocal Plate",  "Plates",
          1,  0.5f,   true,  10.0f, 0,
          0.74f, 0.41f, 0.20f, 0.81f, 0.84f, 1.10f,  436.0f,
          0.37f, 0.00f, 0.30f,  25.7f, 13357.0f, 0.75f, false, 6.7f,
          /* mono */ 20.0f, /* mid */ 1.06f, /* highX */ 7342.0f, /* sat */ 0.20f,
          /* hiCutShelfGainDb */ -12.0f,
          /* gate */ true,
          /* sixAPDensityBaseline */ 0.62f, /* sixAPBloomCeiling */ 0.85f,
          /* sixAPBloomStagger    */ { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f },
          /* sixAPEarlyMix        */ 0.5f,  /* sixAPOutputTrim    */ 1.3f,
          /* bassChoke            */ 20.0f,
          /* dpvHfShelfGainDb     */ 3.25f,       // staged_tuner v9 — Stage 3 polish only
          /* dpvHfShelfFreqHz     */ 4049.0f,
          /* dpvStructHfDampHz    */ 6605.0f,     // moved to Stage 2 — couples to per-band decay
          /* dpvBoxCutGainDb      */ -1.52f,
          /* dpvBoxCutFreqHz      */ 704.0f,
          /* dpvBassShelfGainDb   */ 0.82f,
          /* dpvBassShelfFreqHz   */ 89.7f },
        // ── Drum Plate (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "Drum Plate" preset (Reverb Mode = Plate,
        // HighShelf at max for bright top, HighCut ~6 kHz) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Plates,
        // has_dpv=False (FDN). 1300 trials.
        //   Stage 1 loss 1.05  (subtle envelope)
        //   Stage 2 loss 145.7 (per-band decay tight)
        //   Stage 3 loss 8.33  (clean polish)
        //
        // 12 / 40 gates fail.
        // Locked 2026-05-30: direct-scoreboard + warm-start FiveBand FDN sweep
        // (19-dim), 28→27 vs VVV "Drum Plate". Listening (2026-05-31) flagged a
        // muddy/dark low-mid vs VVV; perceptual-weighted re-sweeps only slid the
        // failures along the FDN's strong↔clear-low Pareto wall. Residual is
        // structural (low-mid modal ring + per-band energy) → Transient Shaper
        // (Phase A plumbing in place, depth 0) targets this in Phase B.
        { "Drum Plate",           "Plates",
          4,  0.42f, false, 12.0f, 0,
          2.263f, 0.337f, 0.373f, 0.119f, 1.296f, 0.723f,  98.99f,
          0.441f, 0.30f, 0.55f,  20.68f, 10078.6f, 0.934f, false, -5.15f,
          /* mono */ 20.0f, /* mid */ 0.690f, /* highX */ 7762.3f, /* sat */ 0.214f },
        // ═══════════ SPRINGS ═══════════
        // ── Surf '63 Spring ──────────────────────────────────────────────────
        // Engine: SpringEngine (algo 4). Reference: Fender 6G15 outboard
        // reverb unit driving a clean amp — Dick Dale "Misirlou" (1962),
        // every surf-rock tremolo-picked lead through 1962-65. Short-spring
        // tank, mild dispersion, classic 4 kHz spring rolloff. The hijacked
        // mod_depth (SPRING LEN) and mod_rate (DRIP) knobs control the
        // characteristic ambient wobble.
        { "Surf '63 Spring",      "Springs",
          5,  0.35f, false,  0.0f, 0,
          1.60f, 0.40f, 0.20f, 1.50f, 1.00f, 0.85f, 1000.0f,
          0.45f, 0.10f, 0.30f,  80.0f,  4000.0f, 1.10f, false, 2.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Bright Hall (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: Valhalla Vintage Verb "Bright Hall" factory
        // preset (Reverb Mode = Bright Hall, Color Mode = now) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Halls.
        // 1300 trials, 6 workers. Anchor t60→knob 3.255 s, sigma 1.04×.
        //   Stage 1: loss 0.72   (Spatial + Mod + Mod Depth clamp [0,0.15])
        //   Stage 2: loss 55.15  (Decay + Mults + Crossovers, low_mid×3)
        //   Stage 3: loss 8.81   (Lo/Hi Cut + Sat + Trim, Hi Cut floor 4kHz)
        //
        // 7 / 40 gates fail — best result in the Halls category:
        //   - ALL 8 ss-band energies pass within ±1.5 dB.
        //   - cent_50 Δ -2.9 %, cent_500 Δ +11.6 % — both pass.
        //   - tail_t30 Δ -4.6 %, tail_t60 Δ -16.4 % (well under ±25 %).
        //   - env_p2p Δ -0.64 (dead-on), stereo_corr Δ +0.06 (dead-on).
        //   - env_shape_L1 2.94 dB (under ±3).
        //
        // Remaining 7 fails:
        //   - spec_L1 mean 3.21 dB (mid-band texture residue)
        //   - spec_L1 max 7.52 dB @ 1016 Hz (FDN mode)
        //   - edt low / edt mid (49 % / 32 % — FDN-vs-VVV EDT structural)
        //   - decay low / decay mid (+47 / -30 % — same structural)
        //   - osc P2P +4.5 dB (mod depth at 0.10 in clamp; FDN per-line LFO
        //     produces slightly heavier envelope ripple than VVV's slow drift)
        // v33 master REVERTED (2026-05-29): sweep was optimized against an
        // engine that had the ModulatedDamping coefficient-lerp instability
        // bug (raw (a1,a2) lerp pulled biquads outside stability triangle
        // twice per LFO cycle → user heard "garbage / mud / boom"). With
        // that bug now fixed via the 32-step lookup table, v33's sweep-
        // optimal params no longer balance — bass +4.25 dB hot at 63-125 Hz,
        // mids/highs -2 to -6 dB cold. Reverting to original v1 factory row
        // (Decay 4.31, Bass 1.38, Treble 1.29, Mid 0.68 — the 16-fail
        // baseline). Future sweeps run against the bug-fixed engine will
        // produce correctly-balanced params.
        // ── Bright Hall: V2.0 baseline on VintageTank (algo 8) ──────────────
        // Decay 5.0s targets VVV anchor 4.73s @ 1k T60 (round-trip math
        // empirically calibrated). 3-band damper voicing:
        //   Bass Mult 1.10 → +0.83 dB low-shelf @ 250 Hz  (lifts 63/125 ✓)
        //   Mid  Mult 1.20 → +1.58 dB peaking @ √(250·4000)=1k (lifts mid T60)
        //   Treble Mult 0.85 → -1.41 dB high-shelf @ 4 kHz (chokes 1k ring)
        //   Hi Cut 4000 Hz → 3-band damper highFc
        // Gain Trim +2.0 dB compensates for outputGain_ default 1.0
        // (previously baked into tap scale 0.74).
        // BH-5 single-axis revert on top of BH-4 (2026-05-30):
        //   lowCrossover 350 → 250 — reverts BH-3's failed bass/mid split
        //                              shift. The raise pushed 500 Hz +
        //                              1 kHz into the mid-band peaking
        //                              region (midFc = sqrt(350 * 7000) =
        //                              1565 Hz) where midMult=1.12 didn't
        //                              reach down far enough → both bands
        //                              flipped cold. Reverting restores
        //                              the BH-2 T60 setup where 8 of 9
        //                              bands were within ±5%. 250 Hz cold
        //                              is structural — not a crossover
        //                              boundary issue.
        // BH-FINAL on VintageTank algo=8 (locked 2026-05-30):
        //   erLevel 0.37 — BH-8 optimal ER profile. Closed env_shape_L1
        //                    + improved edt low 100-250 by 3.3x. BH-9
        //                    proved erLevel 0.37 ↔ 0.39 below measurement
        //                    resolution on sub-bass band → reverted to
        //                    0.37 to lock env_shape + edt low wins.
        //   Diffusion 0.58 — BH-8 nudge held.
        //   Sub-bass <100 -2.03 dB cold accepted as engine signature —
        //   not addressable via current parameter set. gainTrim trade
        //   would push sine1k over gate (zero-sum boundary).
        //
        // Net session: BH 25 → 10 fails (-15).
        { "Bright Hall",          "Halls",
          8,  0.40f, false,  0.0f, 0,
          5.00f, 0.85f, 0.50f, 1.20f, 0.95f, 1.00f,  250.0f,
          0.58f, 0.37f, 0.55f,  20.0f, 7000.0f, 1.00f, false, +1.50f,
          /* mono */ 20.0f, /* mid */ 1.12f, /* highX */ 8000.0f, /* sat */ 0.03f,
          /* hiCutShelfGainDb */ -6.0f },
        // ── Deep Blue (vintage rack reverb) ───────────────────────────────────────────────
        // Engine: SixAPTank. Anchor: vintage rack reverb "Deep Blue" (Bank P0 0.0)
        // — reference hardware's "impossibly massive" Concert Hall preset, the literal
        // first preset in their Hall bank. The 6-AP density cascade matches
        // the PCM's late-tail thickness better than FDN.
        //   RT60 2.63 s   bass_mult 1.10   treble_mult 0.64 (DARK!)
        //   centroid 50ms 5.9 kHz (already dark on the transient)
        //   centroid 1s   3.6 kHz (continues to darken)
        //   shape: REVERSE — energy builds late (the "swelling cathedral" character)
        //   predelay 10 ms
        { "Deep Blue",            "Halls",
          2,  0.45f, false, 10.0f, 0,
          3.00f, 0.85f, 0.15f, 0.40f, 0.65f, 1.10f,  600.0f,
          0.85f, 0.40f, 0.65f,  60.0f,  8500.0f, 1.30f, false, 9.0f,
          /* mono */ 20.0f, /* mid */ 1.10f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ═══════════ HALLS ═══════════
        // ── Vocal Hall (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: Valhalla Vintage Verb "Vocal Hall" factory
        // preset @ 100% wet. Loaded into harness via .vpreset XML param
        // extraction (Concert Hall reverb mode + specific Decay/Size/Bass/
        // HighShelf/etc settings).
        //
        // v15 (2026-05-29): staged_tuner.py sweep with two new asymmetric
        // loss terms wired in response to v14 listening verdict ("boomy"):
        //   - boom_loss      late-window low-band integrated RMS (40-300 Hz
        //                    × 0.5-2 s peak-aligned), cubic-hot 6× / quad-cold.
        //                    v14 audition: +3.17 dB DV-hot in 500ms-1s × sub.
        //   - tail_mod_loss  per-band detrended Hilbert env std (asymmetric
        //                    DV-hot only). v14 mid 1-4k tail ripple std
        //                    4.15 dB vs Lex 0.95 dB — audible slow wobble.
        //
        // Result: 7 / 29 gate fails (was 9). Both audible defects CLOSED:
        //   - boom sub 40-100 500ms-1s:  +3.17 → +1.62 dB  ✓
        //   - ripple mid 1-4k:           +3.20 → +1.27 dB  ✓
        //   - cent_50 / cent_500:        -1.7 % / -2.2 %   ✓
        //   - All 8 SS bands             within ±0.77 dB   ✓
        //   - spec_L1 mean/max           1.38 / 3.75 dB    ✓
        //
        // Optimizer fixed boom + wobble via Decay Time 3.50 → 2.73 s
        // (toward anchor 2.83) + Hi Cut 11569 → 7179 Hz, NOT by lowering
        // Bass Multiply. Less HF feedback + shorter decay window = both
        // axes closed simultaneously.
        //
        // 7 open gates: 3 FDN architectural EDT signature (edt sub / low /
        // hi) shared with all Halls — Phase 3 TimeVaryingDamping target.
        // 2 bass-cold slight overcorrection (boom 100-300 1-2s -2.59 dB).
        // env_p2p +10.22 dB (DV transients punchier — audibility TBD).
        // env_shape_L1 3.27 dB (just over 3.0 gate, JND-marginal).
        // 2026-05-29 revert: v18-manual + v29 PostTankEQ pass produced an
        // audible chorus/wobble + bus-mode garbage on FDN. Rolled VH back
        // to v15 Optuna baseline (HEAD commit 54ef861 / 95558c8 lineage)
        // where the FDN path was stable.
        //
        // Step 1 retune on top of v15 baseline (2026-05-30): bump decay
        // 2.04 → 3.50 s. T60 was cold across every band (-47% bass to
        // -8% air) against /tmp/anchor_vh; the v15 row was tuned against
        // a different anchor with much shorter tail. Extending the decay
        // runway closes the T60 fails + the downstream decay-ratio + EDT
        // + boom + body cascades in one move.
        // Step 4-6 retune on top of Step 1-3 (2026-05-30):
        //   Damping (Treble Multiply)  0.86 → 0.55  — chokes 1-8 kHz tail
        //                                              that overshot +24..+44%
        //                                              after Step 1 decay bump.
        //                                              NOTE: Step 4 was a no-op
        //                                              until the Step 7
        //                                              setAirTrebleMultiply
        //                                              plumbing bug-fix went
        //                                              in (PluginProcessor +
        //                                              DuskVerbEngine).
        //   gainTrim                  -1.52 → -5.0 → -3.0 — Step 6 -5.0 over-
        //                                              corrected and pulled
        //                                              5 ss bands cold; Step 8
        //                                              eases back to the sweet
        //                                              spot between the two.
        // Step 9 retune on top of Step 7-8 (2026-05-30):
        //   Damping (Treble Multiply) 0.55 → 0.75 — relax the cliff that
        //                                            crashed 16 kHz to -24%
        //                                            cold and left a 12.9 kHz
        //                                            spike sticking out.
        //   Mid Multiply              1.13 → 0.85 — actively chokes 250 Hz to
        //                                            4 kHz where the treble
        //                                            damping shelf cannot
        //                                            reach (highCrossover
        //                                            sits at 8 kHz).
        //   Hi Cut                    5884 → 4500 — lowers output rolloff to
        //                                            squash the over-
        //                                            attenuated 12.9 kHz
        //                                            resonant mode at the
        //                                            spec_L1 max slot.
        // Step 10 retune on top of Step 9 (2026-05-30):
        //   midCrossover    396 → 600  — protects 500 Hz band from midMult
        //                                  cut by raising mid-band lower edge.
        //   highCrossover  8042 → 6000 — moves damping shelf knee lower so
        //                                  HF damping reaches into 4-8 kHz
        //                                  without leaving the air band
        //                                  starved.
        //   Damping         0.75 → 0.80 — slight relax now that
        //                                  highCrossover handles more of
        //                                  the upper-mid attenuation.
        //   Hi Cut          4500 → 5500 — recovers air band, centroid,
        //                                  ss hi 5-10k that Step 9 over-
        //                                  cooled.
        //   gainTrim       -3.00 → -2.00 — final broadband level lift
        //                                  toward anchor parity; sine1k
        //                                  was still +3.22 hot so net
        //                                  +1 dB keeps it in gate.
        // Step 11 retune on top of Step 10 (2026-05-30):
        //   Mid Multiply   0.85 → 0.78 — precise trim on 2k/4k/8k overshoot
        //                                  without disturbing 1 kHz node.
        //   gainTrim      -2.00 → -2.50 — corrects 4 hot RMS gates from
        //                                  Step 10 over-correction.
        //   Bass Multiply  1.50 → 1.30 — vents late sub-bass sustained
        //                                  energy without disturbing
        //                                  63/125 Hz initial decay.
        //   Diffusion      0.12 → 0.45 — raises early reflection density
        //                                  to close cold sub/low EDT gates
        //                                  (first 10-15 dB envelope).
        // Step 12 single-axis correction on top of Step 11 (2026-05-30):
        //   Bass Multiply  1.30 → 1.45 — split-the-difference restore of
        //                                  the low-end decay runway. Step 11
        //                                  drop to 1.30 killed 63/125/250/
        //                                  500/1k T60 (5 gates flipped cold)
        //                                  + decay sub -82.9% + body 125-250
        //                                  cold + low band level cold. 1.45
        //                                  recovers bass tails without
        //                                  going back to the 1.50 boom sub
        //                                  hot we had earlier.
        // Step 11 winners locked:
        //   Mid Multiply  0.78  — closed 2k + 4k T60.
        //   Diffusion     0.45  — closed mod bass + mod lowmid.
        //   gainTrim     -2.50  — keeps RMS gates in spec once bass
        //                          tail recovers.
        // Step 13 multi-axis polish on top of Step 12 (2026-05-30):
        //   Bass Multiply 1.45 → 1.40 — works with new 70 Hz PostTankEQ
        //                                 scoop to vent over-sustained sub
        //                                 without disturbing 125/250/500 Hz
        //                                 bands locked at passing.
        //   Mid Multiply  0.78 → 0.82 — pulls 1 kHz tail back from -7.9% to
        //                                 within gate.
        //   Damping       0.80 → 0.78 — slight relax to recover 16 kHz cold
        //                                 (-9.4%) + air band.
        //   Hi Cut        5500 → 6000 — opens air band; resolves cent_500
        //                                 cold (-17.8%) + ss hi 5-10k cold.
        // Step 14 precision polish on top of Step 13 (2026-05-30):
        //   Bass Multiply 1.40 → 1.42 — micro restore for 63 Hz (-12.1%)
        //                                 + 125 Hz (-5.9%) without
        //                                 disturbing higher bands.
        //   erLevel       0.45 → 0.65 — raises early reflection deck to
        //                                 close EDT sub/low/lowmid cold
        //                                 fails (first 10-15 dB envelope).
        //   erSize        0.55 → 0.45 — redistributes early-tap timeline
        //                                 to support new erLevel.
        { "Vocal Hall",           "Halls",
          4,  0.35f, false, 22.0f, 0,
          3.50f, 0.76f, 0.123f, 0.54f, 0.78f, 1.42f,  600.0f,
          0.45f, 0.65f, 0.45f,  33.0f,  6000.0f, 0.88f, false, -2.50f,
          /* mono */ 20.0f, /* mid */ 0.82f, /* highX */ 6000.0f, /* sat */ 0.32f },
        // ── Cathedral (VVV anchor) ─────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "CathedralLargeHall" preset (Reverb Mode
        // = Cathedral, ModDepth 75 %, HighShelf at 6 kHz, HighCut ~7 kHz).
        //
        // Locked 2026-05-30: direct-scoreboard + warm-start FDN sweep,
        // 33 → 27 gate fails vs VVV "CathedralLargeHall" anchor (verified via
        // --program). Ripple + bloom fully green. Residual 27 hits the SAME
        // structural wall as Vocal Plate — HF T60 overshoot the single
        // trebleMult can't bend (8k wants 1.79s, FDN gives 3.13s, +75%) + sub
        // energy/decay deficit. The FDN structural refactor targets exactly
        // this. See memory duskverb_tuning_method.
        { "Cathedral Large Hall", "Halls",
          4,  0.45f, false, 20.88f, 0,
          4.91f, 0.94f, 0.12f, 1.44f, 1.10f, 0.92f,  193.0f,
          0.75f, 0.48f, 0.36f,  64.0f,  3720.0f, 1.03f, false, -7.32f,
          /* mono */ 20.0f, /* mid */ 0.69f, /* highX */ 5960.0f, /* sat */ 0.36f,
          /* hiCutShelfGainDb */ -14.5f },
        // ── Blade Runner 224 ─────────────────────────────────────────────────
        // Anchor: Vangelis on the late-1970s digital hall hardware (Hall A / Constellation) —
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
        //   Initial centroid   12 kHz at -16.7 dB (bright "reference hardware snap")
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
        // 2026-05-25 re-anchor: prior preset used Dattorro (algo 0) at 9 s
        // decay — wrong engine for a Random Hall topology. Migrated to FDN
        // (algo 4) with Lex Random Hall "Large RHall 4" anchor params.
        //
        // Lex Large RHall 4 spec (path: LexRandomHall / 03.Large Halls /
        // 030.Large RHall 4.xml):
        //   Reverb_Time 5.47 s, Size 69 m, Diffusion 100 %, BassRT 1.5×,
        //   Bass_XOV 360 Hz, RT_HiCut 4500 Hz, Spin 2.2 Hz, Wander 22 ms,
        //   Predelay 25 ms.
        //
        // Defining 224 Random Hall character: dense diffusion + extended bass
        // + aggressive HF damp + heavy modulation (Wander 22 ms = LARGE per-
        // line wander, the "Random" in Random Hall).
        // Calibrated 2026-05-25: cent_50 −6.2 % vs Lex Large RHall 4
        // anchor (within strict ±10 %), cent_500 −15.4 % (FDN engine
        // ceiling on late-tail HF retention; Lex Random Hall's multi-tap
        // input carries HF energy further into the 500-1500 ms window
        // than FDN's modal density allows). Treble Multiply at max (1.50),
        // Hi Cut at max (20 kHz), High Crossover at 4500 Hz.
        // BR-9 sub-25 push on top of BR-8 (2026-05-30):
        //   gainTrim -9.40 → -9.50 — snap sine1k +2.08 dB into spec.
        //   damping/bassMult/midMult KEEP.
        //   PTEQ Band 3 -5.0 → -6.0 dB in PluginProcessor.cpp.
        { "Blade Runner 224",     "Halls",
          4,  0.45f, false, 25.0f, 0,
          4.99f, 0.48f, 0.35f, 0.64f, 1.04f, 1.90f,  328.0f,
          0.97f, 0.00f, 0.50f, 47.0f, 19723.0f, 1.10f, false, -9.50f,
          /* mono */ 20.0f, /* mid */ 1.70f, /* highX */ 1581.0f, /* sat */ 0.39f,
          /* hiCutShelfGainDb */ -11.3f },
        // ── 79 Vocal Chamber (VVV anchor) ──────────────────────────────────
        // Engine: QuadTank. Anchor: VVV "79 Vocal Chamber" preset (Reverb
        // Mode = Chamber1979) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Chambers.
        // 1300 trials. Stage 1 loss 1.38. Stage 2 loss 754.07 (HIGH — VVV's
        // Chamber1979 mode has dense modal character QuadTank can't match
        // exactly). Stage 3 loss 278.13. 22 / 40 gates fail — widest
        // architectural gap in the queue so far. DV is its own chamber.
        { "79 Vocal Chamber",     "Chambers",
          3,  0.30f, false,  8.39f, 0,
          5.05f, 0.44f, 0.20f, 0.50f, 0.56f, 0.71f,  324.0f,
          0.42f, 0.20f, 0.44f,  26.0f, 10060.0f, 0.96f, false, -8.55f,
          /* mono */ 20.0f, /* mid */ 1.14f, /* highX */ 5957.0f, /* sat */ 0.26f,
          /* hiCutShelfGainDb */ -23.5f },
        // ═══════════ CHAMBERS ═══════════
        // ═══════════ ROOMS ═══════════
        // ── Small Drum Room (VVV anchor) ───────────────────────────────────
        // Engine: QuadTank. Anchor: VVV "Small Drum Room" preset (Reverb
        // Mode = Ambience, ModDepth = 100 %, HighCut low for dark room).
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Rooms.
        // 1300 trials. Stage 1 1.28 / Stage 2 109.58 / Stage 3 60.02.
        // 24 / 40 gates fail — QuadTank vs VVV Ambience reverb mode + heavy
        // modulation is a wider gap than expected.
        // SDR-2 on top of SDR-1 (2026-05-30):
        //   size       0.25 → 0.10 — Size dominates loopLength in
        //                             gEffTarget = pow(10,-3*L/(decay*sr)).
        //                             SDR-1 missed this lever.
        //   decay      0.18 → 0.10 — paired floor drop; alone insufficient
        //                             at Size=0.25.
        //   modDepth   0.35 → 0.03 — REVERT. QuadTank LFOs modulate AP
        //                             coefficients (phase mod), not
        //                             envelope. Hilbert envelope FFT
        //                             gates can't see it → dead leverage.
        //   modRate    1.50 → 1.11 — REVERT same reason.
        //   hiCutShelf -6.0 — KEEP (air band recovery from SDR-1).
        // SDR-NL1 engine swap on top of SDR-4 (2026-05-30):
        //   algorithm  3 → 6 (QuadTank → NonLinear).
        //               Master engine audit verdict: NonLinear's feed-
        //               forward TDL topology delivered zero modal ripple
        //               (0.00 across all 4 bands) and lowest fail count
        //               (41) for this 56 ms anchor footprint.
        //   gainTrim  -2.13 → +6.87 — uniform +9 dB lift to compensate
        //               for the audit-measured ~9 dB level deficit
        //               (noiseburst -8.55, snare -9.12) on NonLinear.
        //   hiCutShelf -6.0 KEEP — air recovery still applies.
        //   QuadTank-era tunes (size 0.10, decay 0.10, bassMult 0.20,
        //   crossover 300) retained as starting point — may need
        //   NonLinear-specific retune in later steps.
        // SDR-NL3 dual-axis spectral flatten on top of SDR-NL2 (2026-05-30):
        //   gainTrim +3.87 → +6.20 — global lift to bring wings (deep sub
        //                              + air) back into passing range.
        //   PostTankEQ Band 2: 1500 Hz Q=0.5 -3.5 dB scoop (PluginProcessor.cpp)
        //                              counters the mid-bulge so 500 Hz to
        //                              4 kHz stays in spec under the +6.20
        //                              lift. Combined effect: net +6.20 dB
        //                              at wings, net +2.7 dB at mids.
        // SDR-DAT initial baseline (2026-05-30): repaired row after
        // botched harness substitution. Dattorro engine swap +18.5 dB lift.
        // SDR locked at NL7 (NonLinear) — best deterministic floor.
        // Engine swap audit + 8-gen autosweep across NL/Dat:
        //   QuadTank floor: 42 | NL7: 27 | Dat4: 35.
        // NL7 wins for this anchor (56 ms tail target).
        { "Small Drum Room",      "Rooms",
          6,  0.25f, false,  1.18f, 0,
          0.100f, 0.100f, 0.03f, 1.11f, 1.01f, 0.20f,  300.0f,
          0.38f, 0.80f, 0.57f,  37.0f, 10005.0f, 1.04f, false, +5.90f,
          /* mono */ 20.0f, /* mid */ 0.84f, /* highX */ 7586.0f, /* sat */ 0.22f,
          /* hiCutShelfGainDb */ -4.50f },
        // ── Tiled Room (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "Tiled Room" preset (Reverb Mode =
        // Chamber, Size 0.107, EarlyDiffusion 0.35, LateDiffusion 0.5).
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Rooms.
        // 1300 trials. Stage 1 3.79 / Stage 2 578.41 / Stage 3 80.00.
        // 21 / 40 gates fail.
        { "Tiled Room",           "Rooms",
          4,  0.30f, false,  8.20f, 0,
          0.73f, 0.48f, 0.21f, 1.39f, 0.82f, 0.94f,  424.0f,
          0.75f, 0.46f, 0.40f,  20.0f, 10007.0f, 0.85f, false, -2.18f,
          /* mono */ 20.0f, /* mid */ 1.17f, /* highX */ 6356.0f, /* sat */ 0.27f },
        // ── Ambience (VVV anchor) ──────────────────────────────────────────
        // Engine: QuadTank. Anchor: Valhalla Vintage Verb "Ambience" preset
        // (Reverb Mode = Ambience) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Rooms.
        // 1300 trials. Anchor t60→knob 1.80 s. Stage 1: 3.28 (high — short
        // ambient tail makes spatial loss noisy). Stage 2: 85.4. Stage 3: 122.7
        // (large — VVV's Ambience reverb mode has bright modal character that
        // QuadTank topology doesn't match exactly).
        //
        // 17 / 40 gates fail. QuadTank vs VVV Ambience mode is a wider
        // architectural gap than FDN vs VVV Concert Hall. Key failures:
        //   - cent_50 -21 % / cent_500 +72 % (different spectral envelope)
        //   - deep sub +7.9 dB (QuadTank produces more <50 Hz content)
        //   - ss air -6 dB (Hi Cut 10.4 kHz; VVV air band extends higher)
        //   - decay low/low_mid/mid/hi all +30 to +80 % (QuadTank longer)
        //   - osc P2P -10 dB (QuadTank modulation produces less ripple)
        //
        // DV's QuadTank Ambience is its own character — not a VVV clone.
        { "Ambience",             "Rooms",
          3,  0.40f, false,  2.91f, 0,
          0.74f, 0.75f, 0.18f, 1.05f, 1.03f, 1.10f,  793.0f,
          0.70f, 0.89f, 0.56f,  20.0f, 10070.0f, 1.03f, false, 1.28f,
          /* mono */ 20.0f, /* mid */ 1.12f, /* highX */ 4566.0f, /* sat */ 0.25f },
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
        //
        // 2026-05-26 calibration verdict: SHIP AS-IS (engine-ceiling preset).
        // Anchor (VVV 84 Small Room) uses fundamentally different topology —
        // a small-room reverb without the time-domain hard cutoff that defines
        // the 1981 gated character. Spectral / rt60 / env_p2p deltas against
        // the anchor are intentional — the NonLinear engine's static-FIR cliff
        // IS the preset. ENGINE_CEILING entries in verify_preset_vs_anchor.py
        // document this; no further tuning warranted.
        { "1981 Gated Snare",     "Rooms",
          6,  1.00f, false,  0.0f, 0,
          1.67f, 0.96f, 0.22f, 3.00f, 1.05f, 2.18f, 2197.0f,
          0.83f, 0.00f, 0.00f,  20.0f,  4976.0f, 1.14f, false, -16.6f,
          /* mono */ 100.0f, /* mid */ 0.98f, /* highX */ 5302.0f, /* sat */ 0.34f },
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
          6,  0.216f, false,  0.0f, 0,
          2.608f, 0.80f, 0.092f, 0.794f, 0.75f, 1.10f,  500.0f,
          0.50f, 0.00f, 0.30f,  60.0f, 10000.0f, 1.30f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 0.75f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Reverse Taps (vintage rack reverb) ────────────────────────────────────────────
        // Engine: NonLinear (algo 5) in REVERSE mode (diffusion 0.33-0.66
        // selects the reverse envelope). Anchor: vintage rack reverb "Reverse Taps"
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
        //
        // 2026-05-26 calibration verdict: SHIP AS-IS (engine-ceiling preset).
        // Anchor (vintage rack reverb "Reverse Taps") uses true backwards-convolved
        // envelope topology; the NonLinear engine produces a forward-swelling
        // gate-shaped envelope that captures the perceptual "reverse" character
        // without bit-exact backwards convolution. Spectral / rt60 / env_p2p
        // deltas against the anchor are intentional architectural differences.
        // ENGINE_CEILING entries in verify_preset_vs_anchor.py document this.
        { "Reverse Taps",         "Rooms",
          6,  1.00f, false, 30.0f, 0,
          0.44f, 0.70f, 0.13f, 2.81f, 1.15f, 1.90f,  203.0f,
          0.95f, 0.00f, 0.30f,  20.0f, 19158.0f, 1.04f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 0.67f, /* highX */ 7070.0f, /* sat */ 0.02f },
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
          2,  0.45f, false, 45.0f, 0,
          5.50f, 0.90f, 0.25f, 0.35f, 0.45f, 1.50f,  500.0f,
          0.85f, 0.20f, 0.85f,  80.0f,  9000.0f, 1.50f, false, 4.5f,
          /* mono */ 80.0f, /* mid */ 1.20f, /* highX */ 3200.0f, /* sat */ 0.10f },
        // ═══════════ AMBIENT ═══════════
        // ── Black Hole ───────────────────────────────────────────────────────
        // Reference: external reference Shimmer "BlackHole" factory preset — one of
        // the 8 stock presets shipped with external reference. Huge dark deep ambient void;
        // signature external reference sound for cinematic sustains, drones, and synth pads.
        // Tuned against external reference reference render (DBG_BlackHole_*):
        //   • Algorithm 1 (SixAPTank, 6-allpass cascaded diffuser) —
        //     structurally matches external reference blackhole's Schroeder cascaded-allpass
        //     architecture. Algorithm 3 (FDN) had a 30 ms silence gap before
        //     the burst onset that didn't match external reference's continuous early rise.
        //   • Zero pre-delay — external reference blackhole has reverb starting immediately.
        //   • 14 s decay + size 0.95 for the slow tail decay (~4 dB/s).
        //   • Brightness profile (post-engine-tuning iteration):
        //     - damping (treble multiply) = 1.0 — treble decays at same rate
        //       as mid; air persists into deep tail to match external reference's broadband
        //       sustain. Earlier iteration (0.45) had treble decaying 2.2×
        //       faster, killing >6 kHz content by 1-2 s in.
        //     - midMult = 1.10 — mid decay 10% slower; presence band rings
        //       longer, adds "information" to the tail.
        //     - highCrossover = 8 kHz — pushes mid-shelf turnover above the
        //       presence peak; sparkle band stops getting damped early.
        //     - hiCut = 18 kHz (effectively bypassed) — external reference blackhole has
        //       full-bandwidth content above 12 kHz late in the tail; lower
        //       cuts (e.g. 6.5 kHz) collapsed the >12 kHz tail to silence.
        //     - modRate = 0.60 Hz — slight LFO movement adds shimmer; was
        //       0.40 Hz which felt too still.
        // Differs from "Infinite Blackhole" (modern multi-FX-anchored, 18 s, 6-AP
        // with heavy modulation) by being shorter, less modulated, and
        // tuned to external reference's specific factory voicing rather than modern multi-FX's.
        // Engine-config overrides (sixAP*) close the >12 kHz late-tail gap
        // (was 16 dB cold vs external reference) by injecting more bright ParallelDiffuser
        // content directly into the output (kEarlyMix 0.5→0.75) and letting
        // the density cascade ring denser at later stages (kBloomCeiling
        // 0.85→0.92, steeper stagger). These don't affect other SixAPTank
        // presets because they default to the historical hardcoded values.
        { "Black Hole",           "Ambient",
          2,  0.50f, false,   0.0f, 0,
          14.00f, 0.95f, 0.25f, 0.60f, 1.00f, 1.10f,  700.0f,
          0.85f, 0.05f, 0.70f,  60.0f, 18000.0f, 1.40f, false, -2.0f,
          /* mono */ 60.0f, /* mid */ 1.10f, /* highX */ 8000.0f, /* sat */ 0.08f,
          /* hiCutShelfGainDb */ -12.0f,
          /* gate */ true,
          /* sixAPDensityBaseline */ 0.72f,
          /* sixAPBloomCeiling    */ 0.92f,
          /* sixAPBloomStagger    */ { 0.65f, 0.78f, 0.92f, 1.05f, 1.18f, 1.30f },
          /* sixAPEarlyMix        */ 0.75f,
          /* sixAPOutputTrim      */ 1.10f },
        // ── Cascading Heaven ─────────────────────────────────────────────
        // +24 semitones (two-octave stack) at ~57% feedback. No external reference factory
        // direct equivalent — kept as our differentiator. Lower feedback
        // than +12 (cascade builds 4× faster at 4× pitch ratio), longer
        // decay (6 s) for the stacked-octave swell, slightly darker hi-cut
        // (6 kHz) to keep the upper-octave stack from glassing up.
        { "Cascading Heaven",     "Shimmer",
          7,  0.361f, false,  60.0f, 0,
          6.00f, 0.85f, 1.00f, 2.705f, 0.95f, 1.10f,  800.0f,
          0.85f, 0.20f, 0.50f,  60.0f,  6000.0f, 1.40f, false, -3.0f,
          /* mono */ 60.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Deep Blue Day ────────────────────────────────────────────────
        // Reference: external reference Shimmer "DeepBlueDay" preset (named after the
        // Brian Eno track on *Apollo: Atmospheres and Soundtracks*). 80% wet,
        // +12 octave, ~45% feedback, very long sustained tail. Decay 10.3 s
        // + size 100% gives the long sustained character; lower feedback
        // (45%) keeps the cascade gentle so the long reverb dominates over
        // the pitched recirculation.
        // mod_depth 0.5 = +12 st; mod_rate 4.5 Hz maps to feedback ≈ 0.42.
        { "Deep Blue Day",        "Shimmer",
          7,  0.38f, false,  25.0f, 0,
          10.30f, 1.00f, 0.50f, 2.395f, 1.00f, 1.10f,  800.0f,
          0.85f, 0.20f, 0.50f,  60.0f,  7000.0f, 1.30f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.05f },
    };
    return presets;
}
