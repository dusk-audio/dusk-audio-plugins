# DuskVerb Fleet Handoff — 2026-07-06 (for Opus 4.8)

Supersedes `HANDOFF_next_tuning.md` (2026-06-03). That doc's walls/levers lists remain valid history;
this doc is the current operating picture. Fresh fleet numbers live in `scoreboard_2026-07-06.md`
(generated alongside this doc).

## 0. Mission and method

**Goal:** every anchored preset audibly indistinguishable from its commercial anchor
(Valhalla VintageVerb ×11, Lexicon ×5, Valhalla Shimmer ×2). The `full_check.py` gates are the
JND-calibrated proxy for that; the user's ear is the final sign-off, NOT QA.

Non-negotiable method rules:

- **Diagnose, don't count.** Read `full_check` output as a per-cause diagnosis (see `AUDIT.md`
  root-cause classes: broken_calibration / tunable / structural / anchor_suspect). Never compare
  raw n_fail across dates — gates were added mid-June/July (boing, impulse_rms, piano-stem,
  first_arrival, band_tail_growth), so counts inflate over time by design.
- **No architectural ceilings accepted.** If an engine hits a wall, fork or fix the engine.
  But: never tag a failing gate as a ceiling without listening confirmation — pinned-at-floor
  failures hide tunable gaps.
- **Every listening issue → automated gate + loss term** (`README.md` contract). Manual
  one-off param patches without a gate don't scale to 18 presets.
- **Self-verify before any A/B handoff to the user:** no pre-echo, no THD/NaN/clip, the metrics
  you meant to fix actually moved, no regressions elsewhere, pluginval passes.
- **Validate every targeted preset** after a calibration/engine change — never extrapolate
  from one anchor.

Render rules (violations have burned days before):

- 100% wet for every NON-Shimmer preset: `--param "Dry/Wet=1.0" --param "Bus Mode=1"` (and
  Freeze=0); those anchors are wet-only. EXCEPTION: the Shimmer anchors (Black Hole, Deep Blue
  Day) were captured at their native mix 0.5, so render Shimmer at its captured 0.5 mix (+
  `--long-sine-seconds 15`), never forced full-wet. fleet_audit.py already keys on this per preset.
- `--program <name>` NOT `--preset` — `--preset` re-applies a stale hand-transcribed mirror
  (cost two days once).
- Volume-match DV to anchor RMS **before** judging any spectral/centroid metric.
- Write analysis WAVs with `sf.write(..., subtype='FLOAT')` — PCM16 requantization has faked
  walls and masked oscillation before.
- **NEVER use pedalboard.** Banned. JUCE CLI renderer (`duskverb_render`) only.
- Valhalla anchors render via yabridge VST3s (`~/.vst3/yabridge/*.vst3 --program-index N`);
  Shimmer anchors were captured at native mix=0.5. VVV anchor knob values came from GUI
  screenshot readback (`vvv_anchor_presets.json` + `vvv_calib.json`) — the harness cannot
  param-replay Valhalla.

## 1. Fleet map (18 anchored presets)

| DV Preset | Algo (engine) | Anchor | Target |
|---|---|---|---|
| Vocal Plate | 10 AccurateHall | vvv-vocal-plate | VVV |
| Drum Plate | 0 Dattorro | vvv-drum-plate | VVV |
| Bright Hall | 14 DenseHall | vvv-bright-hall | VVV |
| Vocal Hall | 14 DenseHall (PMB pilot ran on 15) | vvv-vocal-hall | VVV |
| Cathedral Large Hall | 14 DenseHall | vvv-cathedral | VVV |
| Blade Runner 224 | 14 DenseHall | vvv-blade-runner | VVV |
| 79 Vocal Chamber | 3 QuadTank | vvv-79vc | VVV |
| Small Drum Room | 0 Dattorro | vvv-84-small-room | VVV |
| Medium Drum Room | 0 Dattorro | vvv-fat-snare-room | VVV |
| Live Room | 0 Dattorro | lex-medium-live-room-1 | Lexicon |
| Tiled Room | 13 TiledRoom (composite SparseField+AccurateHall) | vvv-tiled-room | VVV |
| Ambience | 10 AccurateHall | vvv-ambience | VVV |
| Large Chamber | 14 DenseHall | lex-chamber-large | Lexicon |
| Vintage Vocal Plate | 1 DPV | lex-vintage-vocal-plate | Lexicon |
| Vintage Gold Plate | 0 Dattorro | lex-vintage-gold-plate | Lexicon |
| Reverse Taps | 9 ReverseRoom (VelvetTail) | lex-reverse-1 | Lexicon |
| Black Hole | 7 Shimmer | valhalla-shimmer-black-hole | Valhalla Shimmer |
| Deep Blue Day | 7 Shimmer | valhalla-shimmer-deep-blue-day | Valhalla Shimmer |

Anchor dirs: VVV under `~/projects/dusk-audio-tools/tuner_runs/anchors/`, Lexicon + Shimmer under
`~/projects/dusk-audio-tools/anchors/rendered/`. Presets WITHOUT anchors (out of scope):
Surf '63 Spring, 1981 Gated Snare.

Engine source of truth: `src/dsp/AlgorithmConfig.h::kEngines[]` — 16 slots, enum order FROZEN
(saved state stores raw index). Algo 8 (VintageTank) and 12 (AccurateHall32) are removed and
alias to `accurateHall_`. Algo 4 (FDN), 11 (SparseField), 15 (PMB) are hidden/no-preset.

## 2. Scoreboard (2026-07-06 end-of-day baseline)

Authoritative detail (per-preset verbatim failing-gate lists): `scoreboard_2026-07-06.md`.
Same-day build, all current gates. **Total 370 fails, mean 20.6** (matches
`scoreboard_2026-07-06.md`; the 385/21.4 in §9 was the pre-P1/P2 start-of-day number).
Still 370 at the 2026-07-07 session end (see `scoreboard_2026-07-07.json` / §9).

| n_fail | Preset | Algo | Dominant clusters → workstream |
|---|---|---|---|
| 33 | Reverse Taps | 9 | broad level/band mismatch (all 5 bands + both RMS) + tail_t30 → re-level velvet bands, then W1-family crossover fix (§6.5) |
| 31 | Live Room | 0 | boing + cent + tail_t60 + stereo → W3 modal, W2 port |
| 31 | Deep Blue Day | 7 | boing + energy-arrival + env_p2p → W6 down-octave + D3 |
| 28 | Vintage Vocal Plate | 1 | attack/cent/stereo/RMS → W5 shape walls |
| 25 | Small Drum Room | 0 | attack/onset + boing + width low → W2 + W3 |
| 25 | Medium Drum Room | 0 | boing + cent + width hi → W3 (DenseEarlyField already landed) |
| 25 | Black Hole | 7 | flux + boing + width hi + energy t50 → W6 |
| 22 | Large Chamber | 14 | attack/onset + flux + env_p2p + RMS → W2 + W1 candidate |
| 20 | Ambience | 10 | onset + flux + boing + sub/low bands → W7 width + W2 (16k cal drift is load-bearing, careful) |
| 19 | Vocal Plate | 10 | cent + attack/onset + energy-first50 + early-refl → W1 migration (T60 lever = octave table, see §6.1) |
| 19 | Cathedral Large Hall | 14 | attack/onset + width low + impulse RMS → W2, W3 |
| 19 | Blade Runner 224 | 14 | attack/onset/energy + stereo → W2 |
| 18 | Vintage Gold Plate | 0 | cent + attack/onset + boing → W2 + W5 |
| 16 | Tiled Room | 13 | sub/mid bands + onset + flux + early-tap → W2; 16k cal BROKEN +156% (check realized-vs-anchor first) |
| 15 | Drum Plate | 0 | sub + impulse + energy-first50 + edt low-mid → W2 |
| 15 | Vocal Hall | 14 | width low + early-refl + edt hi + body bands → W1 (PMB pilot continues) |
| 14 | 79 Vocal Chamber | 3 | cent_500 + flux + transient + air → structural floor, W7/W1 |
| 10 | Bright Hall | 14 | cent_50 + flux + early-refl + decay_tail + spec@12.9k → fleet best; polish |

Fleet-wide failure-family census (instances / presets-affected of 18):

- decay/edt/shape: 100 instances / 18 presets (add "boom" band-growth rows: +22 more)
- level/band-spectrum: 95 / 17
- early-field/front-load (attack, onset_slope, energy_t50/first50, early refl/tap): 55 / **18**
- modal (boing, diffusion_flux, sine1k): 34 / **18**
- piano-stem: 33 / 17
- centroid: 13 / 9 · stereo/width: 9 / 9 · env/mod: 5 / 5 · HF-kurt: 3 · pitch-chorus: 3

Read: **every preset** fails early-field and modal-density gates — W2 and W3 are fleet-wide
engine problems, not per-preset tuning problems. Decay-shape instances are the largest raw
count but ~half are per-band T60/EDT rows whose fix is W1 (decoupled per-band decay).

One more fleet-wide signal: **T60 16 kHz fails on 11 of 18 presets** — the documented HF
top-octave deficiency (DV's >10k decays too short/dark vs bright anchors; 79VC is the
inversion). No engine has an HF down-tilt lever; the available levers are the output
air-shelf (`kOutputAirShelfByName`, 7 entries baked) and per-octave 16k T60 where the engine
supports it. A principled HF-loop-loss redesign is scoped in `SCOPE_hf_lossless_loop.md`.

Hygiene results from the same run (updated after the P1 fixes, see §9 changelog):

- `fleet_audit.py --verify-tables`: the initial "59 dormant `kAccurateHallT60ByName` entries"
  was a PHANTOM — a scanner regex bug (block capture ran past the map's `}};` closer into later
  maps). Fixed 2026-07-06; the real map has 3 rows, all live. After the fix + the
  kFiveBandByName cleanup the check is **clean (exit 0)**.
- `fleet_audit.py --verify-calibration`: 3 BROKEN (Tiled Room 16k +156%, Ambience 16k −37%,
  Vocal Plate 63 Hz +26%). Remember the trap: a BROKEN-calibration flag means
  commanded≠realized; the *realized* values are what the gates score, and they often already
  match the anchor (Vocal Plate's 63 Hz drift is documented LOAD-BEARING in the map comment) —
  recalibrating can REGRESS a tuned preset. Treat as hygiene, not as a free gate win.

## 3. Engine workstreams (ranked) — "fix each engine's limitations"

### W1 — Mature ParallelMultibandTank (algo 15), the decoupling engine  ★ highest leverage
**Wall it breaks:** the fleet's #1 structural limit — in FDN/QuadTank/Dattorro topologies,
feedback gain sets decay AND level AND EDT per band (coupling wall), and 5-band damping cannot
hit 9 independent octave-T60 targets. Documented everywhere: `duskverb_fdn_t60_coupling_wall`,
QuadTank shares it, AccurateHall's 9-oct GEQ only partially escapes it.

**Status:** hidden pilot (visible=false). Per-band {t60, level, direct, width} across 6 bands
(<120 / 120-350 / 350-1k / 1k-3k / 3k-8k / >8k). `setPmbBand()` + `kPmbByName` map
(`PluginProcessor.cpp` ~2909, currently 1 placeholder row). Vocal Hall pilot landed 3 coupled
targets that were unreachable on DenseHall (38→30 within the pilot's own experiment config).
NOTE: shipping Vocal Hall today runs on algo 14 at n_fail 15 (fresh audit) — that 15 is the
baseline any PMB migration must beat; the pilot's 30 is not comparable (different config +
gate set).

**Split defect FIXED 2026-07-06** (see §9 changelog for the full trail). What shipped:
- Input split: serial Linkwitz-Riley-4 crossover chain (band k = LP4(rest), rest = HP4(rest)).
  The old one-pole subtractive tree leaked 6 dB/oct; an intermediate Butterworth-4 subtractive
  tree failed too (difference-bands rely on PHASE agreement below band — near cutoff they
  don't, ~-12 dB skirts). Real HP skirts, cumulative down-chain.
- Tank-output band confinement: 8th-order Butterworth per side. Needed because a band's tank
  rings whatever leaks IN at that band's T60 — leak that decays slower than a short band's own
  energy crosses over in time regardless of level.
- Honest gains: loop length now includes the in-loop allpass densifiers plus empirical
  per-band ring factors `kLoopAdj` (an AP rings past its nominal delay; +33% T60 error on the
  top band before the fix).
- HF truth: b4/b5 wander removed (kExcSamp 0) — ANY linear-interp modulated read is a per-pass
  HF loss (8k realized -24% below command at just ±2 samples).

**Measured result** (uncalibrated, band commands coarsely mapped from the VVV Vocal Hall
anchor octave curve, `DUSKVERB_PMB="6.2,5.5,4.3,2.9,2.3,1.85;..."`, Decay Time=2.0):
8/9 octaves within ±10% of the anchor (63:-4, 125:+2, 250:+4, 500:-3, 1k:+9, 2k:+2, 4k:0,
16k:+6). Only 8k (+17%) is out — crossover-straddle mixture (the 8 kHz crossover sits inside
the 8k measurement octave), which per-preset calibration trims like any other engine. The
pilot's structural band-edge wall is gone. Fleet bit-null verified after all engine edits;
pluginval 1/5/7/10 pass.

**Measurement traps discovered while doing this (read before calibrating PMB):**
- The SparseField ER bus is summed into algo-15 output — mute it (`--param "Early Ref
  Level=0"`) when measuring tank decay, or burst-window levels are ER, not tank.
- Adversarial band curves (4:1 neighbor ratios) make octave-T60 measurement itself lie: a
  long band legitimately owns part of the measurement octave at the shared edge and the
  Schroeder late-fit reads it. Anchor-shaped smooth curves measure honestly.
- Solo a band via the level vector (`;0,1,0,0,0,0;`) for isolation diagnostics.

**Migration protocol (per preset):** re-measure the preset's baseline n_fail on the current
build first (never-worse claims against stale baselines are worthless); migrate; watch the
**Decay/2 trap** (PMB's decay parameter mapping runs ~half the Dattorro/DenseHall-equivalent —
a naive knob copy doubles the tail); keep algo-15 code bit-null for non-migrated presets;
run `--verify-tables` after (each migration strands old engine's name-keyed rows).

**Migration queue after Vocal Hall:** Vocal Plate (FDN/AccurateHall coupling-locked ~21-25,
its `kFiveBandByName` override is also broken — see §6), Cathedral Large Hall, Blade Runner 224,
Large Chamber. Gate clusters that mark a preset as a PMB candidate: per-band T60 fails +
band-level fails on the SAME bands (that's the coupling signature).

### W2 — Early-field / front-load
**Wall:** back-loading tank topologies swell late. Anchors attack, dip, then bloom ("duh-DUH");
DV arrives washy. Gates: `attack_time`, `energy_t50`, `energy_first50`, `early_tap_*`,
`early_refl_count`. Preset params CANNOT close these (proven — front-load sweeps net-worse).
FDN attack floor ~26 ms (shortest line 1151 smp).

**Landed patterns to replicate (all opt-in, bit-null off):**
- `BuildupDiffuser` (long allpass cascade → gradual tail build): Bright Hall 13→11.
- `DenseEarlyField` post-tank Schroeder fill for Dattorro rooms (`setDattorroDenseField`):
  Medium Drum Room 25→19 with {gain 0.27, predelay 42 ms, t60 730 ms}. Long T60 spreads
  energy; >0.30 gain or >800 ms overshoots.
- DPV `setDenseField` {0.20, 80, 500} — 80 ms predelay = post-onset, no pre-echo; ear-approved
  on Vintage Vocal Plate.
- `ERTAPS` early-tap bank (`setEarlyTapBank`, `kEarlyTapsByName` — 1 entry) for
  `early_refl_count` (gate wants 2-10 discrete arrivals; a single tap can't).

**Next:** port DenseEarlyField to Live Room and Small Drum Room (same algo-0 topology as the
Medium Drum win); bake ERTAPS banks where early_refl_count fails; read
`SCOPE_early_field_engine.md` before inventing anything new.

### W3 — Modal density (boing / sine1k honk / diffusion_flux)
**Wall:** sparse-modal tails ring isolated modes — Live Room 211 Hz, Medium Drum 333 Hz,
Small Drum 963 Hz, Cathedral ~499 Hz. This one defect drives THREE gate families:
`tail_resonance` (boing), `sine1k` honk, `diffusion_flux`. Known dead ends (do not re-grind):
notching hops to the next mode and regresses level; density-jitter regresses; GEQ trim is a
wash; migrating to 16/32-line FDN *relocates* sparsity and trades HF metal (pilot proven +
reverted). `DATNOTCH` in-loop notch exists as a baked band-aid (1 entry, `kModeNotchByName`).

**Direction:** densify inside the existing smooth topologies — DenseHall's LOW-MID diffusion
specifically (its allpass cascade is already HF-smooth), and Dattorro tank diffusion for the
rooms. This is engine DSP work; read `SCOPE_modal_density.md` first. Biggest single audible
lift on the room presets.

### W4 — Metallic HF tail (`hf_tail_texture_kurt`)
**Wall:** sparse-16-line FDN late tail has kurtosis ~26 vs Lexicon ~6.5 ("metallic"). Treble,
diffusion, and allpass inserts are MATHEMATICALLY inert against it (magnitude-preserving ops
vs a shape-normalized statistic); modulation smears only weakly and pitch-chorus blocks going
further. Real fix = dense-topology swap + re-voice for any preset still failing kurt on
AccurateHall/FDN (identify from the fresh scoreboard). The halls already escaped via DenseHall.

### W5 — Non-exponential decay shape (`decay_tail_l1`)
**Wall:** Lexicon anchors hold a loud 0.3-0.5 s shelf then ring out; exponential engines floor
`decay_tail_l1` (VVP floored at 3.64 with decayRef 0.70). DPV DenseField fills the shelf
(landed). Residual DPV walls: decay-shape coupling, ~70 ms attack cap, HF-brightness coupling.
**Direction:** two-stage decay (early shelf + late slope) inside DPV; later, per-band dual-slope
in PMB. Also remember: "not full/warm" complaints are usually decay LENGTH, not spectrum —
check per-band T60 against the anchor on a long-enough render before touching EQ.

### W6 — Shimmer voicing (Black Hole, Deep Blue Day)
**Landed:** HFAirVoice post-loop +12st air (baked: BH 1.5 / DBD 1.2 via `kShimmerAirByName`) —
this was the fix for >12k air; whole-engine oversampling FAILED and was reverted. HF-sustain
shelf (SHIMMERHFS). Down-octave voice BUILT but dormant (`setShimmerDownOctaveMix`, 0=bit-null).
**Next:** bake the down-octave voice for Deep Blue Day — its anchor shimmers DOWN (1k→500/250);
DV up-only reads thin. Note DBD's low-octave match is LEVEL-DEPENDENT (diverges at -18 dBFS
peak vs RMS) — needs gate D3 below before it can be tuned honestly.
**Structural:** metallic 1-3 kHz = pitch-up content piling against the shifter's AA filter —
shifter work (overlap/window design), not EQ. Failed designs already tested and reverted:
up-voice-HPF, swell (regresses mix-0.5 dry click), noise-air (hisses on pads).
**Voicing map:** Decay = length, mid_mult = body, modRate/feedback = sparkle.

### W7 — Per-band width tilt
Per-band width infrastructure EXISTS (`WIDTHBANDS` env; older memories claiming otherwise are
wrong). `spatial_width_band` fails on Ambience/79VC-class presets need a baked per-preset width
tilt, not global Width. QuadTank history: global Width 1.14→0.97 was its only clean lever;
everything else on 79VC is at the structural floor pending W1.

## 4. Detection improvements — "detection needs to get better"

- **D1 — THD/distortion gate (all presets).** Vocal Hall shipped an audible 2% THD bug
  (AttackRamp AM at audio rate) with ZERO gate catching it. `sine1k_odd_thd_excess` exists but
  is shimmer-only. Extend to every preset (sine + snare stimuli, threshold ~+1% excess over
  anchor). Cheap, high value.
- **D2 — Metallic/comb advisory metrics.** `osc_p2p` was the WRONG metric for DenseHall's
  metallic+watery defect; autocorr-lag peak + echo-density were the diagnostics that worked.
  Promote both into full_check as advisory (uncounted) rows so diagnosis is automatic.
- **D3 — Level-dependent matching.** All gates run at one drive level. DBD proved anchors can
  match at RMS and diverge at -18 dBFS peak. Add a second render pass at -18 dB input on
  shimmer presets (later: all), gate on the delta between passes.
- **D4 — More real-material stems.** Piano-stem gates landed 07-04 (use anchor sample rate +
  actual length — that bug is fixed). Extend to a vocal stem and drum-bus stem; requires
  re-rendering all 18 anchors with the new stimuli (yabridge for Valhalla, follow
  `duskverb_vvv_render_screenshot_method` for VVV settings).
- **D5 — Anchor hygiene precheck in fleet_audit.** DONE 2026-07-06 (`check_anchor()`): missing
  stimuli / silent anchor = FATAL abort; non-FLOAT subtype and dry-only suspicion (envelope
  tail-duration < 80 ms past the burst) = warnings. Validated: synthetic dry burst flagged,
  17/18 real anchors clean, lex-reverse-1 warns by design (reverse programs cut hard).
- **D6 — Persist audit artifacts.** DONE 2026-07-06: `fleet_audit.py --out [dir]` writes dated
  `scoreboard_<date>.md` + `.json` (default dir = the tuner dir; includes per-preset failing-
  gate lists via full_check --json). Run it at every session end.

## 5. Tuning protocol — "get better at tuning"

Per-preset loop, in order:

1. **Gain-match** DV to anchor RMS (biggest single gate-count mover, always first).
2. **1-D clean levers:** Width, Lo Cut, Decay — one at a time, re-score after each.
3. **Manual per-gate deltas.** Read the failing gate list, map each to its lever (see HANDOFF
   walls/levers history). Optuna is for COLD-START ONLY — re-sweeping an already-tuned preset
   floors at baseline and wastes hours.
4. **Baked-table edits** (PostTankEQ `kPteqByName`, FiveBand, octave-T60 maps, PMB bands):
   edit the map + rebuild. Runtime `--param` edits of these are desync-BLOCKED and the blocking
   is load-bearing — do not "fix" it.
5. **Octave-T60 recalibration** (`calibrate_octave_t60.py`) — run after any engine delay-line
   change. It only PRINTS a calibrated `ROW: { ... }` payload; the source edit stays manual
   (hand-paste the row into its keyed map). It does NOT rewrite the map blocks.
6. **Listen.** Render snare + piano + a sustained pad, A/B against anchor at matched loudness.
   Then hand off to the user's ear.

Tooling status:
- `tune_preset.py` — **repaired 2026-07-06** (parse_best was NOT broken; see §9). It is now an
  honest cold-start wrapper around `preset_vs_external_optuna.py` (warm-start via `--enqueue-json`
  from `duskverb_render --dump-params`, objective = `full_check --json` n_fail) with safe clamps
  (trials ≤300, workers ≤6) and a `--self-test`. Use it for COLD START only; a roughly-tuned
  preset floors at baseline, so switch to manual per-gate tuning (§5) there.
- Optuna limits: workers ≤6, max_evals ≤300 (1500/4-worker OOM'd the 32 GB box).
- `duskverb_render` flags of note: `--program`, `--param/--nparam`, `--load-state`,
  `--input-wav`, `--long-sine-seconds`, `--sustained-pink-seconds`, `--prerun-seconds`,
  `--dump-params`, `--list-programs`.

## 6. Open bugs / quick wins

1. ~~`kFiveBandByName["Vocal Plate"]` override does not apply~~ **RESOLVED 2026-07-06 — not a
   bug.** The lookup works; the values are inert BY DESIGN: (a) every kFiveBandByName field
   routes only to fdn_/accurateHall_/multibandFdn_ (`DuskVerbEngine.cpp:440`), and (b) when the
   octave GEQ is active — every shipping AccurateHall-tail preset — FiveBandDamping is
   flattened to identity (`FDNReverb.cpp` designCoeffs, `octaveGEQActive_` branch). **The T60
   lever for algo-10 presets is `kAccurateHallT60ByName`**, not FiveBand; the "proven 6/9→9/9"
   recipe from 2026-06-08 was superseded by the 06-16 octave recalibration anyway. 5 fully-
   inert map entries removed (bit-null render-verified on all 5 affected presets); Tiled Room
   kept (its input-makeup fields are live). Routing documented at the map site in
   FactoryPresets.h.
2. ~~59 dormant `kAccurateHallT60ByName` rows~~ **RESOLVED 2026-07-06 — phantom.** fleet_audit
   scanner regex bug (see §2); fixed, and verify-tables now also covers kFiveBandByName. Clean.
3. **SpringEngine `midMult_` / `crossoverHz_` accepted but unwired** (SpringEngine.cpp:139,151)
   — silent no-op knobs (unanchored preset, low priority, but it's a lie in the UI).
4. ~~**`tune_preset.py` parse_best**~~ **RESOLVED 2026-07-06 — not broken.** parse_best already
   tracked the `preset_vs_external_optuna.py` best.json format (`5a243e4`); the wrapper's real rot
   was its contract. Rewritten as an honest cold-start wrapper (clamps trials ≤300 / workers ≤6,
   `--enqueue-json` warm-start passthrough, `--self-test` parse_best proof); READMEs updated. See §9.
5. **VelvetTail 250/500 Hz crossover leak** (VelvetTail.h:276) — Reverse Taps' residual band-T60
   straddle; same crossover-steepness family as the W1 PMB split fix (share the solution).
6. ~~D6 fleet_audit `--out`~~ **DONE 2026-07-06** (see §4 D6).

## 7. Traps (hard-won; violating any of these has already cost days)

1. **Unity build breaks DuskVerb** — never `-DDUSK_UNITY_BUILD=ON` for it.
2. **Bit-null can't be runtime-guarded in recursive loops** — added hot-loop code perturbs FP
   codegen (~1e-4 drift through feedback). Isolate new engine paths via separate TU or
   compile-time switch, and verify bit-null with an actual render diff.
3. `--preset` vs `--program` (see §0).
4. **BROKEN-calibration flag = hygiene** — recal can regress tuned presets (see §2).
5. **PMB Decay/2 trap** on migration.
6. **Engine enum order frozen**; migration table only covers the pre-2026-05-13 layout.
7. **Re-measure baseline before never-worse claims** — gates changed under you.
8. **Anchor sanity before trusting scores** — check tail energy, wet-ness, subtype.
9. **Shimmer anchors are mix=0.5 native** — account for it when level-matching.
10. **fleet_audit renders to `/tmp/audit_*`** — `--no-render` reuses them; stale reuse after a
    rebuild scores the OLD binary.

## 8. Suggested execution order

Phase-sized (≤5 files per phase, verify between phases):

1. ~~Ground truth~~ DONE 2026-07-06 (`scoreboard_2026-07-06.md`; regenerate via
   `fleet_audit.py --out` whenever stale).
2. ~~Quick wins: §6 items 1, 2, 4, 6 + D5~~ DONE 2026-07-06 (see §9 changelog). Remaining §6:
   items 3, 5.
3. **W1:** PMB crossover steepening + octave recal → finish Vocal Hall → migrate Vocal Plate.
4. **W2:** DenseEarlyField ports (Live Room, Small Drum Room) + ERTAPS bakes.
5. **D1-D3 gates**, then re-score the fleet. Counts will RISE — that is added detection, not
   regression; log both old-gate and new-gate counts.
6. **W3 modal density** (largest engine effort; write/extend scope doc first, pilot on ONE room
   preset with bit-null proof before fleet rollout).
7. **W5/W6 residuals, W7 width bakes.**
8. Loop per-preset tuning protocol (§5) after each engine landing.

## 8.5 SESSION END-STATE (2026-07-06, Fable 5 handoff — READ FIRST)

**DBD agent LANDED + VERIFIED (update):** Deep Blue Day 31→29. The 5% odd-THD attribution
to sat-0.232 was WRONG — Saturation is inert on it (never clips at −12 dBFS sine). Real
source: the wet-output `tanh(oL·kWetOutputGain)` in ShimmerEngine Pass 3 driven nonlinear
by DBD's 20 s decay buildup. Fix: per-preset output headroom `h·tanh(x/h)`
(`kShimmerOutHeadroomByName`, env DUSKVERB_SHIMMERHEAD; DBD 4.0 → THD 5.01%→0.83%,
Black Hole 1.0 = exact-tanh bit-null branch, byte-verified + re-scored 25). Down-octave
voice was NOT dormant (memory stale) — already baked + load-bearing at 0.35, no change.
−18 dB level-dependence measured: 500 Hz rung +3.2 dB hot at low level (D3 gate still
needed). Ear A/B in /tmp/w6agent_ABpair_DBD/. DBD's remaining 29 ranked in its §9 entry.

**AWAITING MARC'S EAR (do not proceed without his verdict):**
1. Whoosh 3-way A/B — if approved: bake `kDiffuseERByName` "Vocal Hall" row from the §9 P5
   env string (`0.75,0.42,8,1.0,16,0.55,26,0.3`) + change the VH `hpHz` bake in
   PluginProcessor.cpp 1800 → 2400, verify Cathedral bit-null (shares the map, different
   engine case), pluginval.
2. Live Room dense-field fill (31→29, marginal) — if inaudible, row is removable.
3. Vocal Hall PMB migration overall ("super close" after round 2).

**QUEUE (each needs the exclusive build/render lock — serialize):**
1. Small Drum Room: ERTAPS taps ≈[70,100,158] ms + kill the DV-only 16.6 ms comb
   (metallic_autocorr_peak 0.523 vs anchor 0.148; roomfill/det[4] #87-boing lever is the
   candidate). Dense-field is a PROVEN dead end here.
2. Ambience width bakes (W7).
3. Whoosh-gate calibration (tighten to ~±1.0 dB once DIFFER renders exist); then a
   transient-ducked diffuse bus to remove the whoosh's ss-air cost.
4. Vocal Plate PMB migration (second migration; T60 lever = octave table, §6.1).
5. Advisory-gate promotion review (echo_density = strongest candidate).

**EVERYTHING UNCOMMITTED** on 4k-eq/dpf-core, entangled with unrelated 4k-eq DPF changes —
when Marc asks to commit, stage DuskVerb + tuner files selectively. Auto-memory
`duskverb_fleet_handoff_2026-07-06.md` mirrors this. Multi-agent protocol that worked
today: ONE agent owns rebuild/install ~/.vst3 + rendering at a time; file-disjoint
gate/tooling agents run parallel; every agent reads this doc first, appends to §9, never
commits.

## 9. Changelog

**2026-07-07 (Opus 4.8, cont.) — NEAR-MISS win pass: fleet 365 → 358 (VVP −3, DBD −4).
NOT yet committed (ac8bbd9 = the earlier boing campaign only).**
- Fleet near-miss extraction (scratchpad `nearmiss.py`, all 18 presets, gates ≤1.5× gate):
  93 near-miss gates. Most are coupled/residual (T60 recal-exhausted, HF direction mixed per
  preset, piano in-loop wall, front-load wall). TWO clean wins:
- **Vintage Vocal Plate** (DPV, HF-HOT): baked air-shelf **{6000,-4}** in `kOutputAirShelfByName`
  — lands cent_50 at +1.8 % (matches anchor) + closes ss-hi + ss-air + HF-spec_L1 → 30→27.
  Marginal decay_tail_L1 +0.2 opens (DPV HF↔decay coupling). EAR-CHECK the darker top.
- **Deep Blue Day** (Shimmer): baked mode-smear **{100,2.0}** in `kModeSmearByName` → 25→21.
  Closes boing + piano-low-growth + env_shape (opens 1 HF-spec).
- **SHIMMER-WET ERROR (fixed):** my manual probe scripts forced `Dry/Wet=1.0` on the shimmer
  presets, but Valhalla Shimmer anchors are captured at NATIVE 50 % wet (fleet_audit already
  does this: line 182; see [[duskverb_render_100pct_wet]] EXCEPTION). This inflated my DBD
  reads (44 vs the true 25) and made me wrongly conclude Black Hole/DBD were "smear
  incompatible." At the correct 50 % wet the smear WORKS on DBD (−4). Black Hole is genuinely
  neutral (smear closes boing but opens equal). ALWAYS render Black Hole / Deep Blue Day at
  50 % wet (no Dry/Wet=1.0) in manual probes.
- **Medium Drum Room** (PMB): band-1 (120-350 Hz) t60 **0.966→0.85** in `kPmbByName` — its
  low decay ran too long vs the anchor. Closes 3 boom/low gates → 26→23. Trades T60-125
  (−10→−20 %) + env_shape (both still failing = no count offset); 257 Hz boing unaffected
  (band-shortening doesn't reach it). PMB is decoupled so band-1 edit doesn't touch other
  bands. EAR-CHECK the shorter low-mid decay.
- Isolated trims (Live Room width-hi, sub) are coupled residuals (width-hi trades stereo_corr
  which sits at the gate edge) — no clean win. Vocal Plate is HF-DARK (needs boost, would
  trade), Drum Plate HF passes. Black Hole low-mid tail too SHORT (boom/T60-250) + DBD cent_50
  (smear-opened) are shimmer-voicing (ear-risky, deferred).
- pluginval s8 SUCCESS; every untouched preset bit-null. DynamicLowMidLimiter still uncommitted
  bit-null infra (piano cluster, post-tank insufficient — see below).

**2026-07-07 (Opus 4.8, cont.) — FLEET DIAGNOSIS at 365. Gate-class map + the biggest
remaining systematic cluster.**

Fleet 365/18. Biggest gate CLASSES: T60 33 (per-band coupling; PMB-decouplable, heavy),
spec_L1 16 + cent 13 (HF voicing), **piano cluster ~31** (band-balance 16 + low-growth 9 +
tail-balance 8), boom-low 15, env_shape 14, sine1k 13, bloom 12, front-load cluster ~46
(onset 11 + early_refl 11 + first50 9 + attack 8 + transient 7).

PIANO CLUSTER (biggest UNEXPLORED, now diagnosed) = a SYSTEMATIC low-mid sustained-buildup:
DV's reverbs build +3 to +10 dB of low-mid on a SUSTAINED tone (piano stem / sustained pink)
vs the anchors. Measured piano-band-balance worst: Small Drum +5.1@sub, Medium +5.4@lowmid,
Large Chamber +4.1@lowmid, Drum Plate +4.6@lowmid, Vocal Plate +3.3@mid, Live +3.1@sub (6/7
POSITIVE low-mid; Ambience −3.9@air the exception). Large Chamber piano TAIL low = +10.4 dB.
- Mechanism: the low-mid feedback gain sits near unity, so a finite noiseburst decays (T60
  gates pass) but a SUSTAINED tone accumulates — the "buildup that never fades" defect at
  fleet scale. A STATIC bass cut breaks the tuned noiseburst match; only a DYNAMIC
  buildup-limiter works.
- The existing DenseHall `setLowAccumLimiter` targets ONLY <300 Hz (splitHz clamped 30-300,
  maxCut ≤0.5=−6 dB). Tested on Large Chamber: under-cuts the +10.4 (best +8.8, still fails)
  AND regresses other gates (23→24-30). The piano BAND-balance excess is at 300-3000 Hz —
  entirely OUT of its range.
- REAL FIX (next campaign): a DYNAMIC LOW-MID buildup limiter (splitHz to ~1500, drive-
  following, engages on sustained accumulation only) ported to all tank engines. Closes the
  piano band+tail+growth + boom-low clusters (~40 gates) IF tuned to not touch transients.
  Substantial + risky (voicing) — a focused multi-session build, NOT a quick win.

State: boing/mode-smear campaign COMMITTED (ac8bbd9). 4k-eq DPF + shared-dpf + CLAUDE.md
review fixes left uncommitted (DPF PR).

**2026-07-07 (cont.) — DynamicLowMidLimiter BUILT + tested; post-tank is the WRONG
architecture for the piano cluster (kept as bit-null unbaked infra, NOT committed).**
- New `DynamicLowMidLimiter.h`: post-tank stereo stage — LP-split low-mid, SLOW env follower
  (transients ignored), drive-following cut on the low band, 20 ms gain slew. Wired into
  DuskVerbEngine post-tank (after matchEQ, before the ER sum) + `kDynLowMidByName` /
  `DUSKVERB_DYNLOWMID`. maxCut 0 = off = bit-null.
- FINDING (why it can't fix the cluster): the piano TAIL excess (Large Chamber +10.4 dB @low)
  is the tank's INTERNAL low-mid charge ringing out AFTER the note stops — a post-tank stage
  cuts the OUTPUT but not the in-tank charge, and its env releases once the input stops, so
  the tail is left uncut (measured: tail stayed +10.4 at every threshold; lower thresholds
  just over-cut the louder PLAYING window instead → band sub goes negative). Absolute-level
  threshold is also backwards for a RELATIVE excess that GROWS into the quieter tail.
- On the playing-window BAND it's marginal/fragile: Live Room +3.1→+3.0 (net −1, borderline),
  Vocal Plate 19→23 (worst band just hops to air). Not a clean win.
- REAL FIX: IN-LOOP low-mid feedback reduction, per engine (reduce the recirculating low-mid
  gain when it accumulates → stops the buildup at the source → both playing + tail drop
  proportionally). The DenseHall in-loop `setLowAccumLimiter` is the right SHAPE but capped
  (<300 Hz, −6 dB) + barely helped (−1.6..−3, regressed) — needs widening + porting to the
  Dattorro/FDN/PMB loops. Substantial multi-engine campaign. DynamicLowMidLimiter kept
  unbaked/uncommitted for Marc to extend (as the post-tank half of a hybrid) or revert.


**2026-07-07 (Opus 4.8) — BOING root-caused + mode-smear built (Dattorro+FDN); the +3 dB
tail_resonance gate is STRUCTURALLY UNREACHABLE. Fleet 370, Live Room 23 (reverted).**

Root cause (2 Explore agents + control experiments): the boing is a loop/comb mode
(`f_k = k·f_s/L`), driven by (a) the sparse tank's low mode count and (b) a bass-decay
extension. Live Room Dattorro loop ≈ 5869 smp → 7.5 Hz spacing; 317 Hz = mode ~42 at
+25.7 dB over the tail median. `DUSKVERB_FLATBANDS` test: flattening bassMult/midMult across
the 317 Hz crossover drops it 25.7 → 20.7 dB (bass extension = ~5 dB; sparse comb = the rest).
The existing modulation smears the mode by < ±1 Hz (23-48× too shallow) → invisible.

BUILT (bit-null, opt-in, verified maxΔ 0.0 on Drum Plate + Vocal Plate):
- `DattorroTank::setModeSmear(depthSamples, rateHz)` + `FDNReverbT::setModeSmear` — a deep,
  slow, decorrelated per-line/per-tap RandomWalkLFO summed onto the delay reads. Fits the
  existing buffer slack (Dattorro 6×-base; FDN 8192 bufs, ~1700-smp headroom) → no realloc.
  Engine `setDattorroModeSmear` drives BOTH. Env `DUSKVERB_MODESMEAR="depthSamp,rateHz"`.
  depth 0 → LFOs not advanced → byte-identical.

THE WALL — the +3 dB gate is peak-to-MEDIAN, i.e. it demands Lexicon-level mode density:
- Dattorro + smear + flat: floors ~14 dB (peak HOPS 317→842→1782 as each mode is smeared).
- 16-line FDN + smear: ~15 dB; **32-line FDN, FLAT octave-T60 + deep smear (500@1.2 Hz):
  9.5 dB** — the best achieved, from 25.7 dB. Still 6.5 dB over gate, at heavy-chorus depth.
- Smearing conserves energy: it spreads a few sparse modes but can't raise the median to a
  dense-reverb level. Statistical floor for 16-32 lines ≈ 5-9 dB even with IDEAL smear; +3 dB
  needs ~100+ lines (Valhalla/Lexicon). DV's engines cannot reach the gate.
- MODULATOR REFINED (`DspUtils::MultiSineLFO` — 2 incommensurate sines, continuous full-band
  traversal, replaces the RandomWalkLFO for the smear on both engines): stronger per depth on
  the Dattorro (300 smp → 16.8 dB vs random-walk 600 → 18.7) BUT the FDN flat-octave floor is
  IDENTICAL — random-walk 9.5 dB, multi-sine **9.4 dB**. So the ~9 dB floor is the 16-line
  MODE DENSITY, not the smear quality. Confirmed structural.
- KEY PIVOT: the gate is a Δ (DV − anchor), and the ANCHORS ARE THEMSELVES RESONANT — measured
  boing prominence: Small Drum REF 13.7 dB, Medium 13.9, VGP 14.2, Ambience 14.0, Black Hole
  10.8, Deep Blue Day 10.9. Only **Live Room's anchor is 0 dB** (uniquely smooth). So for 6/7
  presets the smear doesn't need to reach 0 — it just pulls DV DOWN to the anchor's ~14 dB
  (Δ≤+3). NO gate recalibration needed. (Live Room anchor 0 is the lone structural outlier.)
- SHIPPED: **Small Drum Room boing FIXED — 24→23, both boing gates PASS** (25.6→13.4 dB,
  Δ−0.3 vs anchor 13.7). Baked `kModeSmearByName {"Small Drum Room",{150,2.0}}` — a GENTLE+fast
  smear (150 smp @2 Hz) clears the boing with minimal tail disturbance (the deep-smear T60
  shuffle costs 1 gate → net −1). Bit-null for every other preset (depth 0). EAR-CHECK the
  short-tail wander.
- RECIPE for the rest (per-preset, follow-on): find the MINIMUM smear that pulls the boing to
  within +3 of the (resonant) anchor, then re-tune the T60/octave table to recover the smear's
  tail-T60 shift. Ambience (+3) / VGP (+4) net WORSE at the boing-closing depth because the
  disturbance offsets the win → they need the T60 re-voice. Medium Drum (PMB) + Black Hole /
  Deep Blue Day (Shimmer) need `setModeSmear` ported to those engines. Live Room (anchor 0)
  can't pass — genuine structural outlier (ear-check the ~15 dB smeared version or leave).
- Mode-smear infra: `setModeSmear` on Dattorro + FDN + PMB + Shimmer (`MultiSineLFO`), env
  `DUSKVERB_MODESMEAR`, engine `setDattorroModeSmear` drives all four. Bit-null at depth 0
  (verified maxΔ 0.0 on Drum Plate, Vocal Plate, Vocal Hall/PMB, Black Hole/Shimmer). PMB also
  got its `readLine` upgraded linear→cubic Hermite (bit-null at exc=0, HF-lossless for the smear).

**2026-07-07 (Opus 4.8, cont.) — BOING CAMPAIGN: fleet 370 → 365 (3 clean wins baked).**
The boing gate is a Δ vs anchor and the anchors are THEMSELVES resonant (~11-14 dB), so the
smear just pulls DV down to match — using the MINIMUM depth per preset (a deep smear shifts
the tail T60 and nets worse):
- **Small Drum Room** {150,2.0}: 25.6→13.4 dB, both boing gates ✓, 24→23.
- **Ambience** {80,2.5}: 21.0→~12.8, ✓, 20→19.
- **Vintage Gold Plate** {40,2.5}: 18.5→~11.8, ✓, 19→16.
Baked in `kModeSmearByName`. Everything else bit-null (fleet 365, verified).
NOT baked (smear closes boing but nets WORSE — engine-specific incompatibility, documented):
- **Medium Drum Room** (PMB): boing floors Δ+8 (band-2 mini-FDN structural) + breaks ripple.
- **Black Hole / Deep Blue Day** (Shimmer): even a 20-smp smear wrecks the pitch-shifting
  recirculation tail (n_fail +8-13). The Shimmer FDN cannot tolerate delay modulation.
- **Live Room**: its anchor is a unique 0 dB — smear floors ~9-15 dB > gate. Structural outlier.
Remaining boing (4 presets) needs engine-specific work, NOT the smear. EAR-CHECK the 3 baked
smears (subtle tail wander) tonight.


**2026-07-07 (Opus 4.8 session) — engine work: transient-ducked DISCRETE tap bank
built + verified bit-null; front-load + boing walls rigorously re-mapped with CORRECT
anchors. NO gate net-closed today (every single lever trades). Fleet stays 370.**

New engine capability (BUILT, bit-null, pluginval s8 SUCCESS, fleet_audit still 370 =
empirically bit-null — KEEP it, the front-load composite needs it):
- `DuskVerbEngine.h/.cpp`: `TransientDuck etapDuck_` + `etapDuckAmount_` + `setEarlyTapDuck
  (amount,holdMs,thresh)`. Wet-loop ERTAPS block now scales the tap sum by a per-sample
  transient gate keyed off the clean dry-mono. SEPARATE instance from `erDuck_` (a preset
  can duck both the sparse field in the engine case AND the tap bank in the wet loop; a
  shared duck would advance twice/sample). amount 0 → g=1.0 → byte-identical to the
  un-ducked path (1.0f*x exact, out of the recursive loop → no trap-#2 drift).
- `PluginProcessor.cpp`: `ETapConfig` gained `duckAmt/duckHold/duckThresh`; env
  `DUSKVERB_ERTAPDUCK="amount[,hold[,thresh]]"` for rebuild-free sweeps; `setEarlyTapDuck`
  wired. Only kEarlyTapsByName row (79 Vocal Chamber) sets duckAmt 0 → fleet bit-null.
- VALIDATED the duck works: Large Chamber discrete taps cascaded sine1k **+13.9 dB**
  un-ducked → **+3.67 dB** ducked (~10 dB kill). This solves the specific failure that
  killed every prior discrete-ER attempt (feed-forward taps pump the steady state).

Front-load (W2) wall — RE-MAPPED with the CORRECT fleet_audit anchors (my first probes
used WRONG anchors: Blade Runner is `vvv-blade-runner` NOT dv_lex_random_large_rhall1;
Bright Hall is `vvv-bright-hall` NOT dv_lex_hall_med_hall. Only Large Chamber
`lex-chamber-large` + Live Room `lex-medium-live-room-1` were right). Every single lever
fails a SPECIFIC, now-proven way — none nets:
- **Discrete taps** (any gain): loud → the prominent tap becomes the global impulse peak →
  attack_time explodes (17→127-160 ms). Quiet → buried by DV's dense-early tank →
  early_refl only lands 1-2 of 8. AND discrete taps ARE spikes → they blow diffusion_flux
  kurtosis (Bright Hall 8.36 → 40.72). The anchors have 9-10 discrete reflections yet LOW
  kurtosis = DENSE-UNIFORM early field (many similar-amplitude arrivals), the opposite of
  a few big spikes. Bright Hall 11→13, Blade Runner 19→26 — both WORSE.
- **Output diffusion** (`DUSKVERB_OUTDIFF`): magnitude-preserving (no THD, diffusion_flux
  passes), DID fix Blade Runner first50 13→0.3 % + softened onset 2.5→0.17, but high
  amounts over-smear → attack_time explodes (153-346 ms). Real lever, needs a MODERATE
  per-preset amount; still trades.
- **Densified DenseEarlyField** (8 combs + 4 APs, tried + reverted): filling the boing/tail
  valley needs the field loud+long enough to sprout its OWN comb mode (839 Hz) AND overwrite
  the octave-T60 profile → Live Room 24→36-42. A post-tank parallel field CANNOT fill without
  doubling the tail (level/length problem, not smoothness — an FDN mix wouldn't help).
- CONCLUSION: the anchors' early field = a DENSE-DIFFUSE arrival cluster in a SPARSE
  (tank-onset-DELAYED) window. Reproducing it needs a COMPOSITE (tank-onset-delay + a
  dense-diffuse — NOT discrete — early field, ducked, tuned per preset), i.e. a multi-part
  build, not any one existing lever. Over-front-loaded halls (Large Chamber first50 43 vs
  13.5 %, onset 3.7 vs 0.67) additionally need the TANK onset softened — buildup timeScale
  does NOT touch the impulse onset (frozen at 3.7 across 1.7→6.0) and regresses the tail.

Boing (W3) wall — re-confirmed structural to the 2-line Dattorro tanks (Live Room 317 Hz
+23 dB): in-loop AP density is MAGNITUDE-inert (a comb resonance is a magnitude peak; APs
are magnitude-preserving) — DENS 0.5/1.0 left boing 23-24 dB, added 11 T60 fails. Delay-mod
is squared-mapped to ≤8-sample (0.18 ms) wander by design (chorus limit) → far too shallow
to smear a 22 dB mode (needs ~100-sample = audible vibrato). Only a DENSER TANK (more
interleaved lines) fills the comb valleys.

Front-load COMPOSITE (Marc's chosen direction) — BUILT + bit-null, but no clean target:
- `DuskVerbEngine.h/.cpp`: extracted the Dattorro EFE composite body into a shared
  `applyEarlyFieldComposite(numSamples)` (tank-onset-delay + velvet sparse ER + ducked mix);
  Dattorro case now calls it (Small/Medium Drum re-audited byte-identical → extraction is
  bit-null). Added `accurateHallEarlyFieldOn_` + `setAccurateHallEarlyField` + a call in the
  AccurateHall case → the composite is now available to algo-10 presets too. No algo-10 preset
  is in kCompositeERByName by default → flag stays false → AccurateHall byte-identical.
- Tried it on Vocal Plate (algo 10, the cleanest under-front-loaded candidate: attack 45 vs
  9.3 ms, onset 0.62 vs 4.3, first50 27.8 vs 49 %, early_refl 0 vs 6). The VELVET field made
  it WORSE (diffuse low-crest spray vs the anchor's SHARP high-crest plate front — attack
  45→67 ms). Ducked DISCRETE ERTAPS (sharp; VP has NO diffusion_flux fail to protect) + a
  reduced FDN tail (0.7) got closer — CLOSED cent_500/cent_50 (brighter), early_tap, edt
  mid/hi (shorter), transient_def (sharper) — but OPENED body/decay-low/diffusion_flux(kurt
  spike)/ripple/snare-RMS/width => **19→19, a pure character trade.** Vocal Plate is a BROAD
  VOICING mismatch (dark/smooth/long vs bright/sharp/short; attack floor; sine1k −3 dB), not
  a clean front-load target. Row NOT baked (VP ships at 19, bit-null). See
  [[duskverb_frontload_composite_wall]].
- The front-load composite has NO clean untapped target: the under-loaded presets (drum
  rooms, 79VC) already run it; the rest are over-front-loaded (halls) or voicing-mismatched
  (Vocal Plate). Every remaining front-load fail is entangled with voicing/T60 → the
  composite MOVES failures, doesn't net-close them. The diffuse-floor diagnosis holds at the
  composite level. Real next step = the denser-tank rewrite (changes the early-field
  character at the source) or per-preset voicing overhauls — both multi-session.

DENSER-TANK REWRITE (Marc's chosen direction) — Phase-2 iter-1 RAN + HYPOTHESIS PARTIALLY
FALSIFIED. 16 lines is not dense enough for a small-room boing:
- The 16-line `FDNReverbT<true>` (= AccurateHall, algo 10) IS a dense tank — Householder
  mixing, per-octave GEQ (decoupled T60), per-line FiveBandDamping, 2-3 inline Schroeder
  allpasses. Default delays 1151-6451 smp (26-146 ms), 16 log-spaced primes.
- MIGRATED Live Room algo 0→10 (+ octave-T60 seed), built, MEASURED (then reverted to the
  working algo-0/23): the FDN REDUCED Live Room's boing 39 dB → ~15-18 dB (a real ~21 dB
  drop) BUT did NOT clear the +3 dB gate at ANY Size (0.28→1.0 = 17.3→15.2 dB, the mode just
  RELOCATES 419→206 Hz). Uncalibrated n_fail 40 (voicing was Dattorro's). So Vocal Plate's
  boing=0 is NOT "16-line FDN kills boing" — it's that a PLATE's larger/denser voicing +
  bright character masks it; a small ROOM stimulus still rings ~15 dB on the 16-line FDN.
  Confirms [[duskverb_modal_density_pilot]] ("FDN relocates sparsity, doesn't fix it") at
  the migration level.
- 32-LINE PROTOTYPE RAN (accurateHall_ retyped FDNReverbT<true,32>, Live Room algo 10) —
  FALSIFIED: boing 13 dB @ 302 (vs 16-line 15 dB, 2-line Dattorro 39 dB). Doubling the lines
  bought only ~2 dB; the +3 dB gate would need 256+ lines. LINE COUNT IS NOT THE BOING LEVER.
  A 13 dB isolated mode surviving in a 32-line dense FDN = a high-Q RESONANCE, not sparse
  modes — the octave-GEQ likely BOOSTS the 250-500 band (its damping shelf raises Q there),
  or a line-length near-alignment rings. Reverted to 16-line + Live Room algo 0 (working 23).
- REAL boing mechanism (next hypothesis to test): NOT density. Candidates — (a) the octave
  GEQ / FiveBandDamping introduces a high-Q peak in the 250-500 band (measure the FDN boing
  with the GEQ FLAT vs shaped); (b) delay-line-length alignment (the 32 primes may share a
  near-common factor near 300 Hz); (c) insufficient loop modulation (Lexicon smears modes
  with an LFO DV lacks at these low mid freqs). Test (a) first — cheapest, and the GEQ is the
  most likely culprit since the mode sits exactly in a damping-band centre.
- Built + KEPT this session (bit-null, reusable by the rewrite): the AccurateHall EFE
  composite (`applyEarlyFieldComposite` + `setAccurateHallEarlyField`) for front-load, and
  the ducked ERTAPS. Live Room reverted to algo 0 (23) — nothing left broken.

Fleet aggregate (`scoreboard_2026-07-07.json`, 370/18): biggest gate CLASSES — T60 bands
34 (per-band coupling, PMB-decouplable = biggest single proven-fixable class), spec_L 16,
boom-low 15, env_shape 14, piano-band 14, cent 13, bloom 13, sine1k 12, **onset_slope 11 +
early_refl 11 + energy_first50 9 + attack 8 + transient_def 7 = ~46 front-load cluster**
(biggest addressable, but walled per above). Worst presets: Vintage Vocal Plate 30, Reverse
Taps 30, Medium Drum 26, Deep Blue Day 25, Small Drum 24, Large Chamber / Live Room / Black
Hole 23.

**2026-07-06 (P1, Fable 5 session that authored this doc):**
- `FactoryPresets.h`: removed 5 fully-inert `kFiveBandByName` entries (Drum Plate,
  Blade Runner 224, Cathedral Large Hall, Vocal Hall, Vocal Plate); documented field routing +
  octave-GEQ flattening at the map site. Bit-null verified: all 5 presets re-rendered
  byte-identical across every stimulus.
- `fleet_audit.py`: fixed keyed-map scanner regex (`\}?\};` — the old pattern over-captured
  past `std::array` closers, producing 59 phantom dormant rows); `keyed_map_names()` now also
  scans FactoryPresets.h; `TABLE_ENGINES` gained `kFiveBandByName: (4, 10, 11, 13)`; added
  anchor-hygiene precheck (`check_anchor()`: missing/silent = FATAL, non-FLOAT subtype +
  dry-only suspicion = warnings); added `--out [dir]` persisting dated scoreboard .md/.json
  with per-preset failing-gate lists (full_check now invoked with `--json`).
- Verified: `--verify-tables` exit 0; full `--no-render` audit reproduces 385/21.4 exactly;
  precheck flags only lex-reverse-1 (expected, reverse cut); synthetic dry burst correctly
  flagged.

**2026-07-06 (P2, same session) — W1 PMB split fix, `ParallelMultibandTank.h` only:**
- Input split one-pole → serial LR4 crossover chain (a Butterworth-4 subtractive tree was
  tried first and REJECTED: difference-band phase mismatch leaks ~-12 dB below band — do not
  resurrect it).
- 8th-order Butterworth tank-output band confinement (`bandFilter()`); feed-forward direct
  path deliberately unfiltered.
- `updateGains()`: loop length includes AP densifier lengths × empirical `kLoopAdj` ring
  factors {1.03, 1.03, 1.10, 1.06, 1.22, 1.33} (measured 48 kHz).
- `kExcSamp` b4/b5 → 0 (linear-interp wander = per-pass HF loss; 8k was realizing -24%).
- Result: anchor-shaped curve realizes 8/9 octaves within ±10% uncalibrated (8k +17% =
  crossover-straddle, calibration-trimmable). Fleet bit-null (Vocal Hall / Vocal Plate /
  Drum Plate byte-identical), pluginval 1/5/7/10 pass, 9/9 harness tests.
- ~~NEXT for W1: migrate Vocal Hall~~ **DONE same session (P3):**

**2026-07-06 (P3, same session) — Vocal Hall migrated to ParallelMultiband (algo 15),
n_fail 15 → 13. PENDING MARC'S EAR SIGN-OFF — do not consider shipped until he approves.**
- `FactoryPresets.h`: VH algorithm 14 → 15; `AlgorithmConfig.h`: "Parallel Hall" now visible.
- `PluginProcessor.cpp`: kPmbByName VH row baked (hand-tuned v9 of 11 iterations; values are
  at the 2 s decay reference, preset Decay 5.226 s scales ×2.613). VH rows REMOVED from
  kDenseHallOctaveT60ByName + kDenseHallTCByName (now dormant); fleet_audit TABLE_ENGINES
  covers both maps + kPmbByName.
- Engine (`ParallelMultibandTank.h`): third allpass stage on bands 4-5 (kurt 28→~30 texture
  still open — see below); per-band kApCoeff array kept flat 0.55 (raising it made kurt
  WORSE: longer but sparser AP echo train — documented dead end).
- Failed experiment logged: an ERTAPS 5-tap bank matching the anchor's [34,58,113,125,145] ms
  reflections regressed 13→26 — multi-tap feed-forward pumps sine1k +10.7 dB (sustained
  feedthrough). A tap bank for VH needs gains ≤~0.08 or a sustained-signal duck; parked.
- What the migration bought (vs DenseHall baseline fails): T60 9/9, width 3/3, body all,
  edt all, sine1k, piano RMS, piano low growth, boom, bloom, ss bands — all pass.
- Remaining 13: snare RMS +2.7 / full RMS +2.7 / early refl / piano tail low +10 / chorus /
  env_shape (all 6 ALSO failed on DenseHall — inherited, preset-independent); PMB-specific:
  boing 451 Hz +4.3, HF-kurt +17 (4-line-per-band sparsity — next texture target),
  cent_500 −19%, onset_slope +31 (hair), transient def −2.3 (hair), piano band sub −3.3.
- Verified: fresh program-path audit = algo 15 / n_fail 13; non-migrated presets bit-null
  (Drum Plate, Blade Runner, Cathedral, Vocal Plate byte-identical); pluginval 1/5/7/10;
  9/9 harness; NaN/clip/pre-echo self-check clean (pre-onset −106 dBFS, arrival 8.0 ms).

**2026-07-06 (P4, same session) — PMB tanks 4→8 lines per band, after Marc's round-1 ear
verdict ("VVV fuller; DV tail springier/bouncier").**
- Diagnosis: 4-line-per-band mini-FDNs have audible 40-150 ms loop periodicity ("bounce")
  and sparse modes ("thin") — exactly the boing-451 Hz and hf_tail_kurt +17 gate residue.
- `ParallelMultibandTank.h`: kLines 4→8 (8 coprime-ish lengths per band, Hadamard-8
  energy-preserving butterfly, output taps across 4 lines/side incl. two feedback-only
  lines); kExcSamp b2/b3 2,3→1.5,2 (8 modulated lines pushed mod-ripple over gate).
- **Trap hit and fixed: `updateGains` had a hardcoded ×0.25 line-count divisor — with 8
  lines every T60 realized at exactly HALF command (uniform −50%, 52 fails). Now ÷kLines.
  If you change kLines, nothing else needs touching, but re-verify T60.**
- Result: **tail resonance (boing) gates PASS for the first time; hf_tail_kurt 33→~21;
  ripple in-gate**. kPmbByName re-trimmed for the new tap structure (w3 config baked:
  widths down ~0.3 — 8-line taps decorrelate more — b1/b2 levels up, direct down).
  full_check 13 (= 4-line score, far better texture).
- Remaining 13: inherited six (snare/full RMS, early refl, env_shape, piano tail low +6.9
  — improved from +10 — chorus) + kurt +9.5 (still >gate; next lever unclear, maybe
  per-line output-tap scatter), cent_500 −16%, sine1k +3.3, osc_p2p −4.2 hair, transient
  −2.6, spec@10.2k +7, piano low growth +2.7.
- Verified same suite: bit-null on non-migrated 4, pluginval, harness 9/9, self-check clean.
  Round-2 ear A/B sent to Marc.

**2026-07-06 (P5, same session) — the "whoosh": Marc's round-2 verdict = "super close; VVV
has a subtle post-hit whoosh DV lacks, otherwise spot on."**
- Located it: the anchor's 2-10 kHz envelope after a snare hit runs ~1 dB hotter over
  0-60 ms then RECEDES below DV at 60-120 ms — a short HF rush, not a smear. DV's PMB
  tanks onset too cleanly.
- Mechanism: the DenseHall composite's diffused-burst ER bus (`diffuseER_`), which the
  algo-15 case never ran. Wired it into the ParallelMultiband case (active()-guarded →
  bit-null) **plus a new LR4 highpass on the bus** (`setDiffuseERHighpass`, env
  `DUSKVERB_DIFFERHP`, 0 = bit-null): an unfiltered bus pumped sine1k +11 dB — same
  sustained-feedthrough failure as the P3 ERTAPS attempt. HP ≥ ~1.8 kHz protects the
  1 kHz/level budget entirely.
- Best whoosh config (env, NOT yet baked — awaiting Marc's 3-way verdict):
  `DUSKVERB_DIFFER="0.75,0.42,8,1.0,16,0.55,26,0.3"` + `DUSKVERB_DIFFERHP=2400` on top of
  the baked w3 PMB row. HF envelope lands ±1 dB of the anchor contour. Gate cost: 13→16
  (attack_time +10.3 ms quirk, impulse RMS +2.3, ss air +4.5 — the bus adds steady HF
  during sustained drive; a transient-ducked bus would fix that properly).
- Dead ends this round: broadband bus busGain 0.35 (20 fails, sine1k +11); late taps to
  145 ms (pads crest, transient def −3.3); whisper-level busGain 0.15 (zero gate cost but
  +0.1 dB = inaudible — pointless).
- TODO per protocol: add a "whoosh gate" (2-10 kHz 0-60 ms envelope vs anchor + the
  60-120 ms recede) so this stops being ear-only.
- If Marc approves: bake VH row into `kDiffuseERByName` + set the VH `hpHz` bake to 2400
  (currently 1800 in PluginProcessor.cpp, moot while no DIFFER row exists), re-verify
  bit-null on Cathedral (shares the map, different engine case), pluginval, re-audit.

**2026-07-06 (tooling hygiene, separate agent) — tune_preset.py + READMEs (§6 item 4):**
- Root-caused §6.4: parse_best is NOT broken. The `code-review fixes` commit (5a243e4) already
  added the wrapper-unwrap it worried about; verified it parses the current
  `preset_vs_external_optuna.py` best.json format (proven by a synthetic-input test — now shipped
  as `tune_preset.py --self-test`). The wrapper's REAL rot was its contract: default 1500 trials ×
  4 workers (OOMs the box per §5), no `--enqueue-json` warm-start passthrough (cold TPE floors), a
  docstring doctrine ("ONLY way to tune / NEVER hand-edit FactoryPresets.h") that contradicts the
  current manual-per-gate protocol, and emit_lockin pointing at render.cpp instead of the baked
  tables. Scripts it calls are all alive → repaired, not deleted.
- `tune_preset.py` rewritten as an honest cold-start wrapper: safe defaults + clamps (trials ≤300,
  workers ≤6), `--enqueue-json` warm-start passthrough, `--self-test` (parse_best proof), lock-in
  guidance corrected to FactoryPresets.h / keyed maps + the desync-block note, dead no-op
  `auto_level_match` removed. py_compile + self-test pass.
- `tools/tuner/README.md` refreshed: removed the stale quick_assess/auto_tune contract; documents
  fleet_audit.py (`--out`/`--verify-tables`/`--verify-calibration` + anchor precheck), full_check
  `--json`, the `DUSKVERB_*` env-sweep pattern, the §5 per-preset order, and tune_preset's demoted
  role. Every referenced script verified to exist.
- `~/projects/dusk-audio-tools/README.md` rewritten: it referenced a `reference_comparison/` symlink
  and 6 scripts (quick_assess/auto_tune/…) that no longer exist, under a wrong `~/projects/Luna/…`
  path. Now describes reality (anchor + tuner-run DATA repo; scripts live in the plugin tree).
  Note: CLAUDE.md still cites the dead `reference_comparison/` symlink — out of this agent's scope.

**2026-07-06 (P6, Opus 4.8 session) — detection D1/D2 + whoosh gate, `full_check.py` ONLY
(no plugin/other-tuner edits). All new rows are ADVISORY (printed `[ADVISORY]`, never appended
to `fails`), so n_fail is unchanged fleet-wide — verified all 18 presets on the `/tmp/audit_*`
gain-matched renders reproduce their scoreboard counts exactly (VH 13, BH 10, Cath 19, DBD 31, …).
JSON_RESULT still parses; `python3 -m py_compile full_check.py` clean; fleet_audit row/parse
formats untouched.**

New helpers (after `adv_bark_masking_traj`): `adv_whoosh_2_10k` (snare), `adv_autocorr_lag_peak`
(FFT autocorr — `np.correlate 'full'` is O(n²) and TIMED OUT on a multi-second tail; use the rFFT
form), `adv_echo_density` (Abel-Huang). New GATES keys: `whoosh_2_10k_dB` 1.5, `metallic_autocorr_peak`
0.15, `echo_density` 0.15 (odd-THD reuses `sine1k_odd_thd_excess_pct` 1.0). Output rows added at the
end of the ADVISORY block (metallic/echo inside the `if dv and lx:` noiseburst guard; odd-THD + whoosh
at function-body level via their own `find_stim`).

- **D1 — odd-THD (h3/h5) extended to EVERY preset (advisory).** Excess over anchor ≤ +1.0%.
  Fleet is clean (~0.00%) EXCEPT the shimmer sat grit: **Deep Blue Day DV 5.01% vs anchor 0.01%
  (Δ +5.00, ✗)** — the documented sat-0.232 3 kHz product; Black Hole mild (+0.10, ✓). D1 would have
  caught the old Vocal Hall 2% AttackRamp-AM bug. Promotion note: for shimmer presets a COUNTED
  odd-THD row already fires (line ~1926); this advisory is the fleet-wide detector. DBD is the one
  screamer — tie its promotion to W6 down-octave/sat work (it's real distortion, not octave content).
- **whoosh 2-10 kHz (snare, advisory).** Bandpass 2-10 kHz (butter4/sosfiltfilt), onset = broadband
  snare peak, RMS dB in [0-20]/[20-60]/[60-120] ms; |DV−anchor| ≤ 1.5 dB. **Vocal Hall (shipping,
  NO DIFFER env) reads the documented defect SIGN: DV cold 0-60 ms (−0.61/−0.35 dB) then hot at
  60-120 ms (+1.08) — anchor pushes the HF rush and recedes, DV doesn't** — but within the 1.5 dB gate
  on these renders (Marc called the defect "subtle"; the louder `DUSKVERB_DIFFER` whoosh renders are
  NOT in `/tmp`, another agent owns renders, so only the without-whoosh shipping reading exists here).
  Screamers worth a look: DV too HOT (excess early 2-10 k) on Vintage Vocal Plate (+4.7/+6.8/+7.3),
  Vintage Gold Plate (+6.9/+4.1/+3.5), Cathedral (+7.3/+5.2/+4.8), Reverse Taps (+4.3 @0-20);
  DV too COLD on Bright Hall (−1.6/−2.8/−1.9), Tiled Room (−1.9/−3.3/−4.2), Ambience (−0.8/−3.7/−4.7).
- **D2 — metallic/comb advisory (noiseburst tail).** `autocorr-lag peak` (dominant normalized peak,
  1-50 ms lags — a comb locks a tall peak) and `echo density` (Abel-Huang, dense→1.0). |Δ| vs anchor
  ≤ 0.15. These replace osc_p2p (the wrong metric per the DenseHall memory). Metallic-peak screamers:
  Small Drum Room (DV 0.523@16.6 ms vs anchor 0.148 — a DV comb the anchor LACKS, textbook metallic),
  VVP 0.448, Tiled 0.363, Large Chamber 0.238, Medium Drum 0.208, Blade Runner 0.191. Echo-density
  screamers all show DV UNDER-dense (the W2/W3 sparse-modal/front-load wall): Reverse Taps |Δ|0.585
  (DV 1.07 vs 1.66), Large Chamber 0.417, Medium Drum 0.391, VVP 0.394, Tiled 0.310, Live Room 0.212.

**Promotion candidates (future counted rows, pending Marc's ear):** `echo_density` is the strongest —
it systematically flags exactly the presets on the W2/W3 sparse/front-load workstreams and correlates
with their n_fail. `metallic_autocorr_peak` cleanly isolates the Small Drum Room DV comb (W4). Both are
diagnostic-grade already. `whoosh` needs the DIFFER renders to calibrate the threshold against an
audibly-fixed reference before counting. `odd-THD` only fires on DBD — keep advisory until W6 sat work.

**2026-07-06 (W2, Opus 4.8 session) — DenseEarlyField ports to Live Room + Small Drum Room.
Finding: the pattern does NOT generalize to these two the way it did to Medium Drum Room.**
- **Live Room: 31 -> 29 baked** (`PluginProcessor.cpp` kDattorroDenseFieldByName, new row
  `{ "Live Room", { 0.10f, 50.0f, 450.0f } }`, array size 1->2). MARGINAL/incidental win.
  The Medium Drum config (0.27/42/730) REGRESSES Live Room to 38 -- its Lexicon anchor has a
  SHORT band-varying tail (T60 125=0.91s .. 16k=0.42s); a long/loud uniform-T60 field overwrites
  the per-octave T60 profile (+5 new T60 fails) and over-fills. Only short T60 (450 ms) + low
  gain (0.10) stays under the room's own tail. Net: fixes cent_500, edt low_mid 250-500, decay
  hi 2-8k, T60-250, piano-low-growth; breaks tail_t30, ss hi 5-10k, T60-1k -> -2. Reproducible
  (29 on 3 renders), deterministic, program-path verified. Response surface is a SHARP ridge
  (+/-20 ms T60 -> 33-34): it rides gate boundaries and does NOT touch Live Room's real defects
  (317 Hz boing 27.7 dB, onset_slope +1586%, discrete early-refl 1 vs 3) -- W3 modal + ERTAPS.
- **Small Drum Room: NOT baked (dense-field regresses it).** Every config >=26 (baseline 25);
  best low-gain 0.08/75/350 = 26. Its cluster -- attack_time (2.7 vs 10.8 ms too fast),
  onset_slope (+778% too steep), early tap (prom@70 ms), early refl count (0 vs 3 discrete taps
  at 70/100/158 ms) -- is a DISCRETE early-reflection + slow-onset problem. A post-tank DIFFUSE
  Schroeder field cannot make discrete taps nor slow the tank onset; it only adds broadband
  energy the noiseburst gain-match pulls down, worsening snare/impulse RMS + octave-T60.
  (Corroborated by the D2 advisory a concurrent session added same-day: Small Drum has a DV
  metallic comb 0.523@16.6 ms the anchor lacks -- W4, not density.) Needs **ERTAPS**
  (`setEarlyTapBank`/`kEarlyTapsByName`, taps ~[70,100,158] ms) + short onset delay. Recommend
  that as its next W2 action.
- **Why it did not generalize:** Medium Drum needed energy SPREAD (over-front-loaded,
  first50ms +14.5pp) = the field's exact effect. Live Room needs the OPPOSITE (first50ms
  -17.4pp, anchor MORE front-loaded); Small Drum needs discrete ER. DenseEarlyField only fits
  "too front-loaded + thin diffuse tail with a long/flat anchor T60." Check that profile first.
- Verified: Live Room 29 / Small Drum 25 / Medium Drum 25 (program path); Drum Plate + Vintage
  Gold Plate (algo 0, field off) byte-identical pre/post across all 7 stimuli each (bit-null);
  pluginval 1/5/7/10 pass; 9/9 harness. One file touched. No ear check yet -- pending Marc's
  A/B on Live Room.

**2026-07-06 (W6, Opus 4.8 session) — Deep Blue Day odd-THD root-caused + fixed. 31 → 29.
5 files (ShimmerEngine.h/.cpp, DuskVerbEngine.h/.cpp, PluginProcessor.cpp). PENDING MARC'S EAR.**
- **The D1 "sat-0.232 3 kHz grit" attribution was WRONG.** Bisected exhaustively: `Saturation=0.0`
  is BYTE-IDENTICAL to baseline (THD stays 5.01%, n_fail 31, every row). The Saturation param is
  INERT on the sine1k THD — the input-drive softClip threshold at sat 0.232 is 0.861, and the
  -12 dBFS sine peaks at 0.355, so it NEVER clips. THD is also invariant to every shimmer voice
  (up±0, down 0, sub 0, oct 0, feedback min → all still 5.0%) because pitch-shift is dyadic
  (×2,×4,÷2,÷4) and CANNOT synthesize 3k/5k from 1k — odd harmonics can only come from a symmetric
  nonlinearity.
- **Real source: `ShimmerEngine.cpp` Pass-3 output stage `std::tanh(oL*kWetOutputGain)`.** DBD's
  Decay=20 s builds the sustained-tone wet level high enough to drive that tanh nonlinear → 3k/5k.
  Decay-dependence proven: Decay=3 → THD 0.42%, Decay=20 → 5.01%. Black Hole's shorter tail stays
  near-linear (its +0.10% baseline). The tanh is a pure OUTPUT stage — the feedback delay line is
  written from the RAW pre-tanh wet (`wL`), so changing it does NOT touch cascade dynamics.
- **Fix: per-preset output-headroom scalar.** `setOutputHeadroom(h)` → output becomes
  `h*tanh(x/h)` (knee moves to ±h). New setter chain ShimmerEngine → DuskVerbEngine, baked via
  `kShimmerOutHeadroomByName` (env `DUSKVERB_SHIMMERHEAD`) at ~PluginProcessor.cpp:3576. **h=1.0
  → EXACTLY `std::tanh(x)` via an explicit `(h==1.0f)?` branch = bit-null.** Baked BH 1.0 /
  DBD 4.0. Sweep: h=1→5.01% (=old), h=2→1.62%, **h=4→0.83% ✓**, h=6→0.85% (floor = residual
  FDN in-loop clip at FDNReverb.cpp:839, small + in-gate). h=4 clears odd-THD AND incidentally
  clears `decay low 100-250` (less output compression → tail shape shifts) → 31→29.
- **Down-octave voice (W6 core) was NOT dormant — it's already baked and load-bearing.** Current
  bakes: down 0.35 + sub 1.5 + oct-cascade {0,.8,.9,.5} + HPF 24 (extensive prior-session tuning;
  the "up-only/reads-thin" memory is stale). Env kills confirm all are active and needed: SUB0/OCT0
  break the down-octave cascade (250 Hz growth −24 dB / cascade-L1 +12 dB) and regress to 29/35.
  A down=0.6 sweep gave 29 but worsened piano balance → 0.35 is near-optimal. The residual low
  fails (boom sub/low −3.3 dB in the 500 ms-1 s window) are decay-ENVELOPE timing, not a down-mix
  deficit — raising the voices doesn't move them.
- **Level-dependence (D3 trap, verified not chased):** 15 s 1 kHz sine via --input-wav at −9 vs
  −18 dBFS peak — down-octave rungs run HOTTER rel-fundamental at low level: 500 Hz **+3.2 dB**,
  250 Hz +1.4, 125 Hz +0.3. The headroom fix does NOT worsen it (it makes the output stage more
  linear); the residual is the feedback-softClip/octave-cascade interplay. D3 gate still unbuilt.
- **Black Hole: BIT-NULL CONFIRMED** — stashed my 5 files, built the pre-edit binary, byte-compared
  all 7 BH stimuli old-vs-new = byte-identical; program-path re-score = 25 (= baseline). h=1.0
  branch preserves the exact old path. Verified: harness 9/9 (pluginval 1/5/7/10 pass); DBD
  program-path audit = algo 7 / n_fail 29.
- **Remaining DBD 29 (ranked next steps):** (1) boing 203 Hz +6.9 + HF-kurt +3.9 = the sparse-16-
  line-FDN modal/metallic wall (W3/W4 — DenseHall swap re-voice, the `kShimmerDenseByName` flip is
  built-but-false); (2) early-field cluster (energy t50 −222 ms, first50 −13 pp, early refl 1 vs 8,
  early tap) = W2 front-load, preset-params-can't-close; (3) edt/decay hi 2-8k +63-85% + T60 500
  +25% = decay-shape coupling; (4) boom sub/low −3.3 dB decay-envelope; (5) bloom 4-12k +2.5 dB.
  None are down-octave or THD anymore. A/B pair for Marc: `/tmp/w6agent_ABpair_DBD/`
  (DV_deepblueday_{snare,sinelong}.wav vs ANCHOR_{snare,sinelong}.wav, gain-matched).

**2026-07-06 (Opus 4.8 session) — Small Drum Room (queue item 1). PART A: the
"ERTAPS + comb-kill" preset-param plan is FALSIFIED (no bake). PART B (Marc chose
"early-field ER generator"): BUILT the Dattorro EFE composite engine — found 25→22,
hit the manual ceiling, left DORMANT/bit-null (no preset uses it), Small Drum stays 25.**

**PART A — preset-param levers falsified.** Fresh baseline re-measured = 25 (matches
scoreboard). Every lever tested against the `/tmp/audit_small_drum_room` gain-matched
render + full_check verbose:
- **Comb-kill via `DUSKVERB_MAINDET` — DEAD (25→28).** Detuning the 4 main lines
  {1.03,0.97,1.05,0.95} RELOCATES the boing (963→1445 Hz, still +11 dB — the documented
  sparse-modal hop) and PUMPS sine1k (+8.64→+17.64, the detune re-aligns modes onto 1 kHz).
  The 16.6 ms metallic peak barely moves (0.523→0.488) → **the comb is NOT the main lines;
  it's downstream (input diffuser / decorrelation), a W3/W4 structural comb.** The handoff's
  "det[4] comb candidate" (queue item 1 / §8.5) is retired.
- **ERTAPS feed-forward — REGRESSES, no net-positive gain exists.** Taps [70,100,158] ms DO
  close early_refl_count (3 [71,98,159] ms = exact anchor match), early_tap prom, boing, and
  width — but the feed-forward sum of `reflBuf_` (clean dry) adds SUSTAINED energy: on the
  1 kHz sine it inflates sine1k (+8.64 baseline → +11.4 @gain0.12 → +17.6 @gain0.3) and it
  skews the Schroeder T60 fit (T60 63/125 flip to +26/+31%). gain 0.3 → 40, gain 0.12 → 29
  (and 0.12 loses early_tap prom, only 3.9 dB). Same feed-forward-vs-sustained wall VH hit
  (§9 P3). A DECAYING ER generator (not a feed-forward tap) is required.
- **Onset (attack_time 2.7 vs 10.8 ms / onset_slope +777%) — architecturally BIMODAL, not
  tunable.** `softonset` INERT (DattorroTank.cpp:516 ramps 0→1 once after reset, faded up long
  before the mid-render impulse). `bloom` scales only the tank output (DattorroTank.cpp:511) —
  INERT on attack while the ER bus is on (bloom=25/60, exp 3 → attack unmoved 2.6 ms). Pre-Delay
  INVISIBLE (attack_profile measures onset→peak RISE relative to a 40 dB-drop onset index,
  full_check.py:628/639 — blind to leading delay). Killing the ER bus (`Early Ref Level=0`)
  flips attack to ~30 ms (tank's own slow buildup) and bloom 6/10/25 all sit ~30 ms — bloom
  barely modulates it. Partial ER (0.15/0.35) also → ~32 ms. **The onset is DISCONTINUOUS: ER
  tick at 2.7 ms XOR tank buildup at ~30 ms, argmax flips between them, no value lands ~10.8 ms
  — DV has no ~10 ms first reflection the anchor does.** W2 early-field wall, confirmed.
- **Only clean single-gate win:** `DUSKVERB_BLOOM=25` alone → 24 (width low ✓, cosmetic).
- **Verdict:** Small Drum Room's 25 = W2 (no ~10 ms first reflection + bimodal onset) ∩ W3
  (963 Hz sparse-modal boing) ∩ W4 (16.6 ms downstream comb) ∩ the feed-forward/sustained
  tension that blocks ERTAPS. Preset/env params CANNOT close it (exhaustively verified). Real
  fix = a decaying early-field ER generator — `SCOPE_early_field_engine.md` — OR a PMB migration.

**PART B — Dattorro EFE composite BUILT (Marc's scope call), best 25→22, left DORMANT.**
3 files edited (all bit-null-dormant; no preset opts in, Small Drum stays 25):
- `DuskVerbEngine.h/.cpp`: `setDattorroEarlyField(bool)` + `dattorroEarlyFieldOn_`; the
  Dattorro case (algo 0) now runs the same velvet `sparseField_` + `tankOnsetSamples_` delay
  composite as DenseHall, but in a **MIX** form (`tank·sparseTailGain_ + sparseER·sparseERGain_`),
  gated by the dedicated flag — NOT `sparseERGain_` (the unmapped-preset reset pins that to 1.0,
  so Drum Plate/VGP would wrongly run it). **Audio-bit-null VERIFIED by compile-in-vs-out:** all
  7 Drum Plate stimuli sample-identical, maxΔ 0.0 (the block is post-tank, outside the recursive
  loop → trap #2's FP drift does not bite; cmp shows a WAV-header-only byte diff at offset 61).
  Drum Plate 15 / VGP 18 / Small Drum 25 gate-identical to baseline.
- `PluginProcessor.cpp`: a Small Drum row was added to `kCompositeERByName` for sweeping (it
  drives `setTiledRoomVoicing` + flips `setDattorroEarlyField(true)` in the if-branch / false in
  else) then REMOVED after sweeping — leaving the infra dormant. The if/else `setDattorroEarlyField`
  wiring stays (harmless on the DenseHall/QuadTank composites; their cases don't read the flag).
- **Best config found: `DUSKVERB_TILEDROOM="1.0,8,35,70,1.0,0.10,0.4"` + `DUSKVERB_TANKONSET=25`
  + `Early Ref Level=0`.** ⚠️ **The "22" first reported here was an UNDERCOUNT — see the probe-bug
  note in Part C.** The sdr_param.sh probe skipped the `sustained` anchor stimulus so full_check
  silently SKIPPED the ripple×4 + sustained-dependent gates (~+8). The real full-gate count is
  ~30, i.e. the EFE config REGRESSES vs the Dattorro 25 too. What the EFE genuinely buys
  (real, sustained-independent gates): onset_slope +777%→1.63 ✓, width ✓, early_tap prom ✓
  (13.4 dB) — but not enough to offset. Left dormant.
- **Why not lower — the coupled wall:** (1) **sine1k** is the tank's 963 Hz mode (+8.64 baseline
  = W3), and the velvet ER is unit-ENERGY-normalized so it throughputs ~erGain at EVERY freq
  incl 1 kHz → any erGain useful for onset-ownership pushes sine1k to +13…+16 (it fails either
  way, but never improves). Tank reduction via sparseTailGain does NOT help — the ER re-adds it.
  (2) **attack_time** is bimodal/argmax-driven: the Dattorro tank's intrinsic ~30 ms buildup hump
  (or the tankOnset-delayed tank) out-peaks a low-erGain ER onset → attack reads 30–90 ms; only a
  loud ER (sine1k cost) makes the ~10 ms onset the global max. (3) **early_refl_count** needs 3
  DISCRETE arrivals which conflict with the gradual onset (onset_slope). (4) burst2Gain high
  enough for prominence makes burst2 the global peak → attack overshoots.
- **Manual ceiling hit** on the 7-dim coupled space {onsetMs, decayMs, burst2Ms, sparseTailGain,
  erGain, burst2Gain, tankOnset}. Next lever = **Optuna cold-start** over those env dims (this IS
  a cold start; §5 reserves Optuna for exactly this). But even an optimized EFE leaves ~12–15
  structural fails (sine1k/boing 963 mode, T60 tilt, band levels) → the mandate needs the EFE
  PAIRED with a tank that lacks the 963 mode (PMB/dense migration). The EFE alone is
  necessary-not-sufficient. 22 is pre-ear + pre-Optuna → NOT baked.
- **Reusable:** the Dattorro EFE composite now exists for Live Room / Medium Drum too (same algo-0
  topology). `sdr_probe.sh` drives fleet_audit (correct full-gate count); `sdr_param.sh` drives an
  env+--param render (NOW fixed to copy `sustained`).

**PART C — PMB migration TRIED (Marc chose "Pair EFE + PMB migration"), structurally validated
but REGRESSES as configured (35 vs 25). Reverted to algo 0; kPmbByName Small Drum row left
DORMANT + documented for a future dedicated Optuna pass.**
- Migrated Small Drum algo 0→15 (FactoryPresets Decay 0.40083→2.0 so `setDecayScale(Decay/2)`=1.0,
  band t60[] realize directly), added a `kPmbByName` "Small Drum Room" row (bands
  <120/120-350/350-1k/1k-3k/3k-8k/>8k). The PMB case (DuskVerbEngine.cpp ~1730) already
  composites `sparseField_` + uses er_ (useSmoothER true).
- **What PMB FIXES (the decoupling win, real):** T60-63 (0.31/0.30 ✓) and **T60-16k (0.40/0.40 ✓
  — Dattorro was 0.14, the −65% dark-top wall)**, and the **963 Hz sparse boing is GONE** (the
  dense per-band 8-line mini-FDNs don't ring it; it relocated to 1789 Hz band-4 at +5.3, tunable).
  Per-band {t60,level,width} are independently controllable — the coupling wall is broken.
- **What PMB BREAKS / doesn't fix (why it nets 35):** **ripple×4** (bass/lowmid/mid/high spectral
  flutter — the band-split mini-FDNs comb; VH's PMB config avoids it, mine triggers it — a
  tuning/config problem, likely the low band-3/4 levels or the `direct` feed-forward); **sine1k
  still +8** (a sustained tone builds up at the tank resonance; band LEVEL scales it AND the
  noiseburst equally so the gain-match cancels — engine-agnostic, only shorter T60-1k would help
  and that breaks T60-1k); **width low −0.25** (er_ anti-phase R, not the PMB width param);
  early-field (attack/onset/early_refl) unchanged — same wall as Part B; several mid T60 drift.
- **⚠️ CRITICAL PROBE BUG (cost this session's Part-B/C numbers):** `sdr_param.sh` copied only
  [noiseburst,snare,sine1k,impulse,piano] to the anchor dir — NOT `sustained` — so full_check
  SKIPPED every sustained-only gate (ripple×4, ss steady-state, some decay) and UNDERCOUNTED by
  ~8. Every "22"/"26"/"24" from sdr_param this session is ~+8 low. **fleet_audit is the only
  trustworthy count** (it copies STIM incl. sustained). Probe now fixed to copy `sustained`;
  re-verified the PMB "26" config reads 34 with the fix (== the 35 bake, within render noise).
  Corrected takeaway: NEITHER the EFE nor the PMB config actually beats the Dattorro 25 yet.
- **State:** reverted (algo 0, Small Drum verified 25; Vocal Hall unaffected at 13). All infra
  kept dormant: Dattorro EFE composite (bit-null), kPmbByName Small Drum row (dormant on algo 0).
  pluginval 10 SUCCESS after the engine edits.
- **PMB-Optuna driver BUILT + RAN (`pmb_optuna.py`, 25 dims = 24 PMB + erLevel, 240 trials, 6
  workers, full fleet_audit objective incl. sustained). RESULT: floored at n_fail 30 — WORSE
  than the Dattorro 25.** Confirms Marc's standing guidance (memory `feedback_optuna_only_early_
  then_manual`): Optuna scatters (best config has t60_5=0.13, wid_1=0.03 — unprincipled) and
  can't beat a hand-tuned baseline. The best-30's residual = the early-field cluster (attack/
  onset/early_refl/early_tap — the sparse ER was NOT in the search space) + ripple-bass + sine1k
  + T60-63/125/1k/4k/8k + edt. `pmb_optuna.py` kept as a reusable cold-start scout (not a
  finisher) — driver + soft-exceedance objective work; use it to MAP the residual, then hand-tune.
- **HONEST CONCLUSION: Small Drum Room is a ~25-floor preset regardless of engine.** Dattorro
  hand-tuned 25; PMB Optuna 30 / PMB manual 34; EFE ~30. The Valhalla "84 small room" anchor has
  a combination — very quiet sine1k (−24, tank-resonance wall), discrete [70,100,158] ms early
  reflections + gradual 10.8 ms onset (front-load wall), low ripple, mid-peaked short T60 — that
  no DuskVerb engine cleanly matches all at once. PMB fixes the T60-tilt + 963 boing but trades
  them for ripple + still-hot sine1k. **Reverted to the honest Dattorro 25.** Infra kept dormant
  (Dattorro EFE composite bit-null; kPmbByName Small Drum row; pmb_optuna.py).
- **PMB + sparse-ER manual pairing TRIED (Marc's steer "manual > Optuna"). DEFINITIVE NEGATIVE.**
  Added the velvet sparse ER to the PMB tank (kCompositeERByName row + the additive composite in
  the PMB case). The ER genuinely CLOSES the early field for the first time on any engine —
  **attack_time 11.6 ms ✓ (target 10.8)** + early_tap prom ✓ — but the additive broadband energy
  raises the noiseburst, so the full_check gain-match multiplier drops and the WHOLE spectrum
  cascades (sine1k +12, the too-quiet bands, T60 all shift) → n_fail 38→50. Every sparse-ER gain
  from whisper (0.05) up regresses. **Root wall: full_check gain-matches to the noiseburst, so ANY
  early-field energy that isn't spectrally identical to the anchor's own ER cascades every other
  gate — and the ER's unit-energy 1 kHz throughput additionally pumps sine1k.** The only escape is
  a TRANSIENT-DUCKED ER bus (louder on transients, silent on sustained) — a new DSP component
  (the handoff's standing "transient-ducked diffuse bus" TODO), not a tuning move.
- **FINAL VERDICT (all six approaches): Small Drum Room = a hard 25-FLOOR preset.** Dattorro
  preset-params 25 · Dattorro EFE ~30 · PMB manual 34 · PMB Optuna 30 · PMB+sparse-ER 38-50. The
  Valhalla "84 small room" anchor's simultaneous demands (quiet sine1k + discrete early reflections
  + gradual onset + low ripple + short mid-peaked T60) are mutually exclusive under the
  noiseburst-gain-match + every DV tank's 1 kHz resonance. **Reverted; Dattorro 25 is the honest
  best.** (SUPERSEDED by Part D — the transient-ducked ER bus was then BUILT.)

**PART D — TRANSIENT-DUCKED ER BUS BUILT + SHIPPED on Small Drum (Marc: "finalize the
transient-ducked ER bus and finalize this preset; my ear is sign-off, not gates"). Awaiting
Marc's DAW A/B vs the anchor.** The bus is the fix Part C flagged as the only real escape.
- **New component `dsp/TransientDuck.h`:** a per-sample [0,1] gain, ~1 during an input TRANSIENT
  and →0 while it SUSTAINS. Mechanism: smooth |x| into a clean amplitude envelope (~3 ms — kills
  audio-rate ripple so a sustained sine's rectified 2f / a noise burst's grain do NOT read as
  transients — the v1 bug), a lagging slow follower (40/160 ms), `(env−slow)/slow` spikes at an
  onset, and a **6 ms output SLEW** so the gain never moves at audio rate (a per-sample gain jump
  AM-modulated the ER → sine1k_odd_thd fired; the slew cleared it). Deterministic, allocation-free.
- **Wiring:** `setSparseERDuck(amount,sens)` + member `erDuck_`/`sparseERDuckAmount_` in
  DuskVerbEngine; applied to the sparse ER in BOTH the PMB case AND the Dattorro EFE composite —
  `g = erGain·((1−amt)+amt·duck)`, guarded so amount 0 → the plain path → **bit-null** (Vocal Hall
  / Drum Plate byte-identical). Per-preset via `kCompositeERByName` fields 11/12 (duckAmt,duckSens);
  env `DUSKVERB_ERDUCK="amount[,sens]"` for sweeps.
- **What it PROVABLY fixes (deltas, not counts):** the ER cascade that beat every plain-ER attempt.
  Same config, duck OFF→ON: snare RMS +4.26→**+1.47**, attack (PMB tank) 129.8→**11.7 ms ✓**,
  early_tap ✓, sine1k ER-contribution ducked (+13→+9.5). The duck lets the ER fire on the hit
  (early field) while adding NO steady energy (no gain-match cascade) and no sustained 1 kHz pump.
- **SHIPPED for Small Drum's DAW audition:** algo 0 Dattorro (the clean 25-tank: no ripple, no PMB
  short-room walls) + ducked EFE sparse ER. FactoryPresets: erLevel 0.80→0 (the ducked sparse ER
  owns the early field; the old er_ 2.7 ms tick fought it). kCompositeERByName row:
  `{1.0, onset 10, decay 40, burst2 85, tailGain 0.85, erGain 0.25, burst2Gain 0.9, …, duckAmt 1.0,
  duckSens 5.0}` — a MUSICAL config (audible early reflections on hits, clean short-room tail),
  NOT a gate-min. Built + installed 17:19, **pluginval 10 SUCCESS**, Vocal Hall/Drum Plate bit-null.
- **Honest gate state:** ~27-30 (the tank walls remain — snare-too-hot is the Dattorro tank at
  +4.8 BEFORE any ER; sine1k +7 is tank resonance; attack argmax-closes on PMB but not the Dattorro
  tank without a tankOnset that just relocates the hump). Per Marc's method these are NOT the
  deliverable — the audible early field is. **Marc judges in the DAW vs the anchor on drum material.**
- **The duck is a FLEET ASSET, not Small-Drum-only:** queue item 3 is exactly "transient-ducked
  diffuse bus" for the Vocal Hall whoosh; any preset whose ONLY ER blocker is the sustained-cascade
  (not a tank wall) can now use it. `setSparseERDuck` + the kCompositeERByName duck fields are live
  fleet-wide (0 = bit-null everywhere it isn't opted in).

**PART E — the transient-ducked ER bus CANNOT beat Small Drum's 25 (Marc: "beat the gate or I
won't listen"). REVERTED to the honest Dattorro 25. The bus stays as dormant fleet infra.**
The v1 duck tracked the instantaneous transient → the reflections (70-160 ms) fired AFTER it
closed → never registered. Fixed with a HOLD-gate (onset arms a ~150 ms window; threshold deadzone
rejects noise wander; 6 ms output slew kills the AM/THD). With the hold, **early_tap CLOSES ✓** for
the first time. But the definitive wall then surfaced:
- **full_check measures T60 (×9), decay, edt, env_shape, env_p2p, stereo_corr on the NOISEBURST —
  which starts silence→noise, i.e. an ONSET.** The hold-gate fires on THAT onset too and holds the
  ER open 150 ms into the noiseburst → the Schroeder T60 fit + env + stereo are corrupted (+10 new
  fails: T60 250/1k/2k/4k/8k, energy_first50, env_p2p, env_shape, stereo_corr, sub-bass).
- **No threshold separates a snare click from the noiseburst onset** — both are silence→loud
  transients. thresh 0.35→1.5 only trades which gates break; best net = 32.
- Additive-on-top-of-er_0.80 at low erGain reproduces 25 exactly (no ripple, baseline preserved)
  but closes NOTHING (the ER is too quiet to register early_tap). Prominent enough to close
  early_tap ⇒ loud enough to corrupt the noiseburst tail. **The early-field and tail gates are
  coupled through the shared transient-onset stimulus + the noiseburst gain-match — mutually
  exclusive.**
- **~55 configs this session, every one ≥ 25.** FINAL: Small Drum Room is a genuine 25-floor
  preset under this gate suite. Reverted (Small Drum 25 / Vocal Hall 13 / Drum Plate 15; pluginval
  10 SUCCESS; duck bus bit-null-dormant). The bus IS the right tool for a preset whose tail gates
  are NOT measured on a transient-onset stimulus (the whoosh, sustained-pad reverbs) — not for a
  drum room whose entire gate suite keys off the hit. A real future win here needs either a
  non-transient-onset tail stimulus in full_check, or a tank whose intrinsic early field already
  matches (engine re-voice), not an ER bolt-on.

**PART F — SMALL DRUM ROOM BEATS 25 (→ 24). The transient-ducked ER bus + #1 (the full_check
tail-stimulus fix) together clear the wall. Marc's #1 + #2 request; #1 delivered the gate win.**
- **#1 — full_check.py: per-band RT60 + env_p2p now measure the sustained-pink INPUT-OFF RELEASE
  (ISO interrupted-noise RT60, a NO-ONSET tail) instead of the noiseburst peak.** `_t60_band_schroeder`
  + `osc_envelope_p2p` gained an `input_off_s` arg; the loops pass `sustained_pink_seconds` when a
  sustained render exists (fall back to noiseburst otherwise). The release is duck-CLOSED (the ER's
  onset trigger fired 4 s earlier), so a transient-triggered ER bus can't skew the decay/modulation
  fit — which was THE wall (Part E). Both DV and anchor use the identical window. **stereo_corr was
  tried on the release too but REVERTED — the ~0.5 s release is too short for a stable L/R correlation
  and it broadly false-flagged the fleet (+9 stereo fails); it stays on the noiseburst.**
- **The duck**: hold-gate (onset arms a 150 ms window spanning the early field; **deadzone threshold
  8.0** so the sustained pink's own fluctuations don't re-trigger it near input-off and leak the ER
  into the release; 6 ms output slew kills AM/THD). `TransientDuck.h`.
- **Small Drum config (baked, kCompositeERByName)**: Dattorro tank + er_ 0.80 (baseline) + ADDITIVE
  ducked sparse ER `{onset 10, decay 40, burst2 90, tailGain 1.0, erGain 0.25, burst2Gain 1.5,
  duckAmt 1.0, duckHold 150, duckThresh 8.0}`. tailGain 1.0 = additive (tank tail preserved). It
  closes early_tap; T60/env_p2p stay clean (release); residual stereo_corr (+0.34, DV tail too mono)
  + energy_first50/env_shape (the ER vs anchor early field) remain but net **24 < 25**. Program-path
  verified 24, pluginval 10 SUCCESS.
- **FLEET RE-BASELINE (the #1 gate change is fleet-wide): 385 → 388 (+3).** Improved by the
  more-correct release measurement: **Deep Blue Day 31→25 (−6), Reverse Taps 33→30 (−3), Black Hole
  25→23**. Shifted up (release exposes real tail differences the noiseburst masked): Vocal Plate
  19→22, Medium Drum 25→28, Large Chamber 22→24, VVP 28→30, +1 on several. New scoreboard written.
  NOTE for the fleet: the shifted-up presets' new T60/env_p2p fails are REAL (the sustained tail
  differs from anchor more than the noiseburst fit showed) — re-tune against the release, don't
  revert the gate.
- **Bit-null**: the duck/EFE is off (duckAmt 0) for every non-Small-Drum preset → guarded plain path
  → audio bit-null (Drum Plate maxΔ 0.0 verified earlier; the n_fail shifts are the GATE change, not
  the engine). Vocal Hall / Bright Hall / etc. audio unchanged.
- **#2 (engine re-voice) — PERFORMED, NEGATIVE. The Dattorro cannot intrinsically match the
  anchor's discrete early field; the ducked-ER bolt-on (with #1) is the correct architecture.**
  Re-voiced the tank LARGER/denser (Size 0.327→0.7, density→1.0) with the ER OFF, to move its
  intrinsic early field toward [70,100,158] ms: attack stays 2.6 ms (hard onset), early_refl 0 (no
  discrete arrivals at the anchor positions), early_tap 5.6 dB@50 ms (< the 7.2 gate, wrong time),
  boing just RELOCATES (1704→1998 Hz). Nets 26-27 — WORSE than the ducked-ER 24. Root cause: the
  Dattorro's output is DIFFUSE and its modes sit at ~16-50 ms (Size-scaled base delays); it
  structurally can't synthesise discrete late reflections at 70/100/158 ms. The only ways to get
  them — dedicated early-tap delays or a velvet ER field — ARE the "bolt-on" #2 excludes, and no
  other DV tank (PMB ripples short rooms, DenseHall is a hall) has the anchor's intrinsic early
  field either. So #2 confirms #1's ducked-ER as the architecturally-correct solution. Small Drum
  ships at 24 (Dattorro + er_ + transient-ducked sparse ER + the sustained-release T60/env_p2p).
- **MEDIUM DRUM ROOM 28→27 (second ducked-ER preset, same day).** Sibling Dattorro room; its
  attack/onset ALREADY pass (softer intrinsic onset than Small Drum), so only early_tap +
  early_refl + energy_first50 fail. Added a `kCompositeERByName` "Medium Drum Room" row
  `{onset 14, decay 38, burst2 60, tailGain 1.0, erGain 0.20, burst2Gain 1.2, duckAmt 1.0,
  duckHold 120, duckThresh 8.0}` — closes early_tap + energy_first50, no ripple, no new fails
  (coexists with the baked DenseEarlyField shelf). early_refl_count stays 1 (the velvet field's
  single burst2 can't synthesise 3 discrete arrivals at the anchor's [34,46,86] ms — a
  SparseEarlyField multi-burst engine change, not worth 1 gate). Bit-null elsewhere (Small Drum
  24 / Vocal Hall 14 / Drum Plate 16 unaffected), pluginval 10. Fleet 388→387. The ducked-ER +
  sustained-release-gate pattern now generalises across the Dattorro drum rooms — Live Room next.

**PART G — LIVE ROOM 32→23 (−9) by RE-TUNING for the #1 release-T60 gate; + a probe rms bug fix.**
- **The #1 release-T60 gate change FLIPPED Live Room's tail reading.** On the noiseburst its tail
  read hot+long (the 2026-07-03 tuning CUT bass for that); on the sustained-release it reads ~25%
  too SHORT across every band. Re-tuned `FactoryPresets.h` Live Room: **Decay 0.52→0.66** (closes
  T60 63/125/2k/4k + tail_t30/t60 + boom×3 + body + edt + ss) + **Width 0.90→1.50** (anchor
  stereo_corr −0.55, DV was +0.09 too mono → stereo_corr closes). 32→23, fleet_audit-confirmed,
  pluginval 10, other presets unaffected. Fleet 387→378.
- **KEY FLEET INSIGHT: the #1 gate change's +3 shift is RE-TUNABLE, not a loss.** Live Room proves
  it — the presets that shifted UP in the re-baseline (Vocal Plate +3, Medium Drum, Large Chamber,
  VVP…) were tuned for the NOISEBURST T60; re-tuning their Decay/damping/width against the
  sustained-RELEASE T60 recovers it (and more — Live Room netted −9). Do this for the shifted-up
  presets before treating any as a regression.
- **PROBE BUG FIXED (`sdr_param.sh` + `preset_param.sh`):** their gain-match rms did `x**2` on the
  raw STEREO array (per-channel); fleet_audit mono-sums first (`x.mean(axis=1)`). For a DECORRELATED
  anchor (Live Room stereo_corr −0.55) the mono-sum RMS ≪ per-channel → a totally different
  gain-match → the probe read Live Room at 45 vs fleet_audit's 32. Now both mono-sum → probe matches
  fleet_audit (32). Small Drum's win was fleet_audit-verified so it stands; but re-verify any
  probe-only number on a decorrelated preset.
- **Live Room residual 23 = hard walls:** front-load/early-field ×6 (W2 — the extreme 94.8%
  front-load, ER doesn't help), boing 317 Hz + sine1k ×3 (W3 modal), mid T60 250/500/1k ×3
  (3-band-damping coupling — the uniform Decay fixed the outer bands, the mid needs per-band =
  PMB), width-low (structurally anti-correlated anchor low, scaling can't create it), + spectral
  (cent_50/spec_L1@127/ss/edt/env). The clean 1-D levers are spent; the rest is W1/W2/W3 engine work.

**PART H — RELEASE-T60 OCTAVE RECAL (rebuilt `calibrate_octave_t60.py`). Fleet 377→371.**
- **Rebuilt `calibrate_octave_t60.py`** (the old one was gone). It measures the anchor's per-octave
  SUSTAINED-RELEASE T60 (no gain-match — T60 is a decay RATE), then iterates the commanded octave
  values via `DUSKVERB_DHOCT` (DenseHall) / the NEW `DUSKVERB_AHOCT` (AccurateHall — added, mirrors
  DHOCT) with a damped-Newton `cmd *= (target/realized)^0.7` until realized ≈ anchor (<2% in ~6
  iters). Usage: `--preset --anchor <dir> --env DUSKVERB_AHOCT|DHOCT --seed <9 commanded>`.
- **Root cause it fixes:** every octave-table preset was calibrated to the NOISEBURST T60, but the
  anchor's own T60 is STIMULUS-DEPENDENT (Vocal Plate anchor 63 Hz: 0.88 s noiseburst vs 0.55 s
  release). The #1 gate now reads the release, so the old tables mis-commanded.
- **BAKED (recovered/improved):** Vocal Plate 22→19 (all 9 T60 close), Cathedral 19→17 (16k
  over-command 5.25→11.6 = release HF-tail loss compensation), Ambience 21→20, Large Chamber 24→23.
- **SKIPPED (calibration closed T60 but broke spectral/level — the recal optimises T60 only):**
  Tiled Room 16→22 and Blade Runner 19→25 REGRESS (the octave over-commands shift the spectral
  balance), Bright Hall neutral. Left as-is. **Lesson: verify n_fail after a recal — T60-optimal ≠
  n_fail-optimal when the octave gains also drive the spectrum.**
- pluginval 10 SUCCESS. **SESSION NET: fleet 385→371 (−14):** Small Drum −1, Live Room −9, Vocal
  Plate −3, Cathedral −2, Deep Blue Day −6, Reverse Taps −3, Ambience/Large Chamber −1 (vs a few
  release-shift regressions not yet recovered: Medium Drum, VVP — both non-octave-table, need
  Decay/DPV work). New deliverables this session: transient-ducked ER bus (`TransientDuck.h`, fleet
  asset), the #1 sustained-release T60/env_p2p gate, rebuilt `calibrate_octave_t60.py` +
  `DUSKVERB_AHOCT`, probe mono-sum-rms fix.
- **Medium Drum Room (27) + VVP (30) — release-shift NOT recoverable (coupling wall).** Tried the
  Live-Room lever set: Medium Drum (Dattorro) Treble 0.75/Bass — closes 8k but nets 27 (63-short/
  125-long is the low-crossover coupling: Bass up fixes 63, breaks 125/250); VVP (DPV) Width is a
  no-op on stereo_corr and breaks 3 width bands (33). Their T60 tilts have ADJACENT-BAND OPPOSITES
  (63↓/125↑, 500↓/1k↑) that a coupled 3-band tank can't hit — unlike Live Room (uniform → Decay) or
  the octave-table halls (per-band). Real fix = PMB per-band decay (W1). Left at baseline.

**PART I — PMB VALIDATED FOR MEDIUM/LARGER ROOMS (the Small-Drum ripple was short-room-specific).
Medium Drum Room migrated algo 0→15, 27→26. + built `pmb_calibrate.py`.**
- **KEY: PMB does NOT ripple at medium decay (~0.7-0.8 s bands).** Small Drum's PMB rippled because
  its bands were SHORT (0.3-0.5 s = sparse modes). Medium Drum's longer bands are dense → no ripple.
  So PMB IS the tool for the coupling-walled MEDIUM/larger Dattorro rooms (Live Room next), just not
  the short ones.
- **Breaks the coupling wall:** all 9 per-band T60 close (only T60-125 left, a within-band split) +
  sine1k closes (no 963 mode). `pmb_calibrate.py` (NEW) iterates the 6 band t60 via DUSKVERB_PMB to
  the anchor release-T60 octave curve (b0=63, b1=125/250, b2=500, b3=1k/2k, b4=4k/8k, b5=16k;
  converged <1% in 5 iters). Then hand-tuned: levels down (mid/hi/air were ss-hot), direct 0 (was
  impulse-RMS-hot), low-band width 0.55/0.70 (width-low). Baked kPmbByName row + algo 15 + Decay 2.0.
- **Net only −1** (26 vs 27): the T60/sine1k wins (−5) are mostly offset by the PMB tank's own
  character — boing 257 Hz (band-2 mode, relocated not killed = W3), impulse/snare/piano RMS (punchy
  transient), cent/spec/ss. But the coupling wall is STRUCTURALLY gone (clean T60/sine1k), which is
  the real value + the validated path. Bit-null elsewhere, pluginval 10.
- **SESSION FINAL: fleet 385→370 (−15).** Reusable: `pmb_calibrate.py` + the PMB-for-medium-rooms
  validation → Live Room (T60 mid-band tilt), and any coupling-walled medium/large Dattorro/DPV
  preset, is now a known migration path (calibrate bands → tune levels/direct/width).

**PART J — W2 ducked-ER does NOT extend to the halls (boundary confirmed). DenseHall duck-wire
added (bit-null, dormant).** Wired the transient-ducked ER into the DenseHall composite case
(same guarded pattern; duckAmt 0 → plain path → bit-null, verified Cathedral 17/Bright 11/Large
Chamber 23/Blade 19 unchanged). Then tested it on Large Chamber (early_tap 11.9dB@162ms, early_refl
1 vs 8): 23→30, early_tap/early_refl STILL fail. The DenseHall halls' anchors have EXTREME early
fields (8-10 prominent reflections, 12-23 dB — Blade Runner 23.2 dB@130ms) that neither the tank nor
a bolt-on ER reproduces; cranking the ER breaks level/spectral. **The transient-ducked ER is a
MODERATE-early-field tool (drum rooms: Small −1, Medium −1) — it does NOT solve the halls' front-
load (that's the same extreme-front-load wall as Live Room's residual).** The DenseHall duck-wire
stays bit-null-dormant (no hall benefits; kept for pattern-consistency + any future moderate-early
DenseHall preset). **HONEST STATE: the tractable per-preset + calibration + PMB gains are SPENT
(fleet 370, −15 session). Remaining = deep engine work with uncertain payoff (halls' extreme early
field, W3 modal density, ReverseRoom gate constants, DPV coupling) — a fresh scoped session, not
the tail of this one.**

Every engine change ships with: bit-null render-diff proof for untouched presets, fresh baseline
before/after numbers, pluginval pass, and an ear check before claiming victory.
