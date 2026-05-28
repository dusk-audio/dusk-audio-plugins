#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
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
        // 11 / 40 gates fail.
        { "Vocal Plate",          "Plates",
          4,  0.35f, false, 29.16f, 0,
          0.65f, 0.79f, 0.25f, 0.26f, 0.80f, 0.70f,  384.0f,
          0.44f, 0.25f, 0.76f,  27.0f, 17316.0f, 0.78f, false, -3.11f,
          /* mono */ 20.0f, /* mid */ 0.96f, /* highX */ 8418.0f, /* sat */ 0.18f },
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
        // ── Snare Plate XL (VVV anchor) ────────────────────────────────────
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
        { "Snare Plate XL",       "Plates",
          4,  0.42f, false, 12.0f, 0,
          2.02f, 0.22f, 0.54f, 0.98f, 1.00f, 0.76f,  481.0f,
          0.41f, 0.30f, 0.55f,  24.0f, 10038.0f, 0.92f, false, -3.85f,
          /* mono */ 20.0f, /* mid */ 0.61f, /* highX */ 7265.0f, /* sat */ 0.20f },
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
        { "Bright Hall",          "Halls",
          4,  0.40f, false,  0.0f, 0,
          4.31f, 0.75f, 0.103f, 0.83f, 1.29f, 1.38f,  540.0f,
          0.44f, 0.50f, 0.50f,  20.0f, 11112.0f, 0.99f, false, -2.84f,
          /* mono */ 20.0f, /* mid */ 0.68f, /* highX */ 8344.0f, /* sat */ 0.03f },
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
        // v13 (2026-05-27): staged_tuner.py autonomous 3-stage CMA-ES sweep
        // under --category Halls. 1300 trials, 6 workers.
        //
        // Listening-driven Stage 2 loss upgrades (v12 → v13):
        //   - Added spec_L1 max penalty (catches noiseburst FDN-mode peaks
        //     Stage 3 EQ knobs can't reach). v12 had 7 dB peak @ 320 Hz —
        //     audible as "muddy" on vocals.
        //   - Added asymmetric mud-band term (200-500 Hz): only penalize DV
        //     hotter than Lex, weight 3.0×.
        //   - Widened Low Crossover range [80, 600] → [80, 900] so
        //     optimizer can flee mode resonance frequencies.
        //
        // Result: 13/40 gates fail. Mud closed at the source:
        //   - Low Crossover 246 → 396 Hz (away from 320 Hz mode).
        //   - Mid Multiply 0.74 → 1.13 (mid scoop gone).
        //   - Bass Multiply 1.31 → 1.50 (matches VVV's 1.50× exactly).
        //   - spec_L1 max locus moved 320 Hz → 5120 Hz (out of mud zone).
        //   - ss low-mid Δ -0.73 → +0.05 (dead-on).
        //   - ss mid Δ -0.66 → -0.05 (dead-on).
        //
        // Trade-off: closing mud caused Stage 2 to choose shorter Decay
        // (3.45 s → 2.04 s) — tail_t30 -27 %, tail_t60 -39 %. Loss surface
        // now puts spec_L1 max + mud-band terms in tension with per-band
        // decay length. Stage 2 weight balance may need re-tuning if the
        // tail length is judged audibly more important than mid clarity.
        //
        // ENGINE_CEILINGS["Halls"] = {tail_t60_pct} — keep tail_t60 tagged
        // since FDN per-line damping can't extend tail like VVV Color Mode
        // does. All other v11-era ceilings (cent_50, cent_500, ss_hi,
        // ss_air) are tunable failures, not architectural.
        { "Vocal Hall",           "Halls",
          4,  0.35f, false, 22.0f, 0,
          2.04f, 0.76f, 0.123f, 0.54f, 0.86f, 1.50f,  396.0f,
          0.12f, 0.45f, 0.55f,  33.0f,  5884.0f, 0.88f, false, -1.52f,
          /* mono */ 20.0f, /* mid */ 1.13f, /* highX */ 8042.0f, /* sat */ 0.32f },
        // ── Cathedral (VVV anchor) ─────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "CathedralLargeHall" preset (Reverb Mode
        // = Cathedral, ModDepth 75 %, HighShelf at 6 kHz, HighCut ~7 kHz).
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Halls.
        // 1300 trials. Stage 1 2.73 / Stage 2 60.08 / Stage 3 20.21.
        // 14 / 40 gates fail. Hi Cut settled at 4439 Hz (utilizing the
        // Halls-profile widened floor) — the cathedral-dark character.
        { "Cathedral",            "Halls",
          4,  0.45f, false, 20.88f, 0,
          3.59f, 0.70f, 0.18f, 0.95f, 0.93f, 1.13f,  418.0f,
          0.26f, 0.48f, 0.36f,  22.0f,  4261.0f, 0.89f, false, -7.65f,
          /* mono */ 20.0f, /* mid */ 0.73f, /* highX */ 7773.0f, /* sat */ 0.35f,
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
        { "Blade Runner 224",     "Halls",
          4,  0.45f, false, 25.0f, 0,
          4.99f, 0.48f, 0.35f, 0.64f, 0.93f, 2.08f,  328.0f,
          0.97f, 0.00f, 0.50f, 47.0f, 19723.0f, 1.10f, false, -11.3f,
          /* mono */ 20.0f, /* mid */ 1.94f, /* highX */ 1581.0f, /* sat */ 0.39f },
        // ── Realistic Chamber (VVV anchor) ─────────────────────────────────
        // Engine: QuadTank. Anchor: VVV "79 Vocal Chamber" preset (Reverb
        // Mode = Chamber1979) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Chambers.
        // 1300 trials. Stage 1 loss 1.38. Stage 2 loss 754.07 (HIGH — VVV's
        // Chamber1979 mode has dense modal character QuadTank can't match
        // exactly). Stage 3 loss 278.13. 22 / 40 gates fail — widest
        // architectural gap in the queue so far. DV is its own chamber.
        { "Realistic Chamber",    "Chambers",
          3,  0.30f, false,  8.39f, 0,
          5.05f, 0.44f, 0.20f, 0.50f, 0.56f, 0.71f,  324.0f,
          0.42f, 0.20f, 0.44f,  26.0f, 10060.0f, 0.96f, false, -8.55f,
          /* mono */ 20.0f, /* mid */ 1.14f, /* highX */ 5957.0f, /* sat */ 0.26f,
          /* hiCutShelfGainDb */ -23.5f },
        // ═══════════ CHAMBERS ═══════════
        // ═══════════ ROOMS ═══════════
        // ── Tight Drum Room (VVV anchor) ───────────────────────────────────
        // Engine: QuadTank. Anchor: VVV "Small Drum Room" preset (Reverb
        // Mode = Ambience, ModDepth = 100 %, HighCut low for dark room).
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Rooms.
        // 1300 trials. Stage 1 1.28 / Stage 2 109.58 / Stage 3 60.02.
        // 24 / 40 gates fail — QuadTank vs VVV Ambience reverb mode + heavy
        // modulation is a wider gap than expected.
        { "Tight Drum Room",      "Rooms",
          3,  0.25f, false,  1.18f, 0,
          0.43f, 0.38f, 0.03f, 1.11f, 1.01f, 0.71f,  641.0f,
          0.38f, 0.80f, 0.57f,  37.0f, 10005.0f, 1.04f, false, -2.13f,
          /* mono */ 20.0f, /* mid */ 0.84f, /* highX */ 7586.0f, /* sat */ 0.22f,
          /* hiCutShelfGainDb */ -23.5f },
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
