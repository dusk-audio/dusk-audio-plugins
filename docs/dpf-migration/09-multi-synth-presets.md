# 09 — Multi-Synth: Phase 5 preset re-voice + audit log

> Product name: **Sunset Circuits** (renamed from Multi-Synth pre-release; slug `sunset-circuits`).
> Filename and internal class/namespace names kept for history; shipping product is Sunset Circuits.

Companion to `09-multi-synth.md` (design authority) and `09-multi-synth-inventory.md`
(JUCE inventory). This file is the **per-preset change log** for the Phase 5
re-voice, plus the new preset banks and the validation numbers.

The preset table is generated from `plugins/sunset-circuits/dpf-plugin/tools/gen_params.py`
(the `BASE` baseline + `PRESETS` list) into `MultiSynthParams.hpp`. Do not hand-edit
the generated header; edit the generator and re-run it.

## Tools

- `core/tests/presets/preset_render.cpp` — framework-free offline renderer that
  includes the static preset table + the DSP core directly (no plugin hosting, no
  MIDI file injection). Applies preset N exactly as `loadProgram()` and plays a
  mode-appropriate performance: poly = Cmaj7 chord (2 bars) + melodic phrase; mono
  = legato bass/lead line; arp = hold a Cmaj triad 4 bars; acid = hold C2 and run
  the pattern sequencer 4 bars. `perf=`/`note=`/`hold=`/`bars=`/`tempo=` override.
- `core/tests/presets/preset_audit.py` — renders every preset and reports peak,
  RMS, silence, spectral centroid, reference-segment f0, clip count and DC, with a
  PASS/FAIL against the Phase 5 rules (non-silent, peak ≤ −1 dBFS, no clip, finite).

Run: `cmake --build core/tests/build && python3 core/tests/presets/preset_audit.py`

## Structural change #1 — FourPoleOTA rail-to-DC bug (fixed in core)

Auditing the original 40 exposed a real DSP bug, not just a voicing issue. Every
`FourPoleOTA` cascade stage used `s = tanh(y·k)/tanh(k)`, whose small-signal gain
`k/tanh(k)` is **> 1** for all `k > 0`. That makes each stage's `s = 0` fixed point
unstable (a bistable latch). At low/moderate cutoff the per-stage coefficient
`g = tan(π·fc/sr)` is small, so with little AC to keep the leaky integrator moving
each stage rails to a constant ±1 — **pure DC** — which the output DC blocker then
correctly removes, so the voice **decays to silence** on any sustained note below
~2.5 kHz.

- **Repro (before):** "Velvet Fog" (Cosmos, cutoff 1200, held chord) collapsed to
  −120 dBFS within ~0.4 s. A held saw through Cosmos/Oracle/Mono at cutoff 1200
  measured −52 dBFS at 0.1 s → −120 dBFS by 1 s. Mode 3 (LadderFilter, no such
  normaliser) was unaffected.
- **Fix:** divide by `stageNonlinearity` instead of `tanh(stageNonlinearity)` →
  unity small-signal gain (stable) while keeping the knob a real saturation amount.
  Mirrors the proven LadderFilter / AcidFilter (plain bounded `tanh`, gain ≤ 1).
- **After:** same held note holds steadily at −39 dBFS for its full duration.
- **Side effect:** correcting the passband to unity gain removed ~8–15 dB of
  spurious boost, so the whole fleet needed level restored (change #2).

Commit: `multi-synth(core): fix FourPoleOTA rail-to-DC bug`.

## Structural change #2 — fleet level restored

`BASE.masterVol` raised **−6 → 0 dB**. With unity-gain filtering the fleet was
15–30 dB too quiet; +6 dB brings it to a usable instrument level. The hottest
preset (Screamer) peaks −6.8 dBFS — still below the −1 dBFS clip rule with margin.

## Re-voice policy

Factory presets store **absolute parameters** (cutoff in Hz, envelope times in
seconds), not played pitches. With the oversampling pitch bug fixed (pitch gate
green at 1×/2×/4×) and envelope/LFO/arp time constants now correct, those absolute
values render at the intended pitch, speed and brightness. Therefore most original
presets already serve their name once the two structural fixes above are in place
— per the design-doc rule "where the old values already serve the intent, leave
them." Each was still rendered and audited; the table records the verdict.

Note on the centroid column: presets with `vintage > 0` (Velvet Fog, Horror Drone,
etc.) add a broadband noise floor, so their centroid over-reads when the tone is
quiet — it is not evidence of a bright tone. Register (f0) and RMS are the reliable
intent proxies for those.

## Original 40 — re-voice verdict

All rendered + audited; all pass (non-silent, peak ≤ −1 dBFS, no clip, pitch-
correct, mode preserved). "level" = affected only by the global +6 dB and the
filter-gain correction; "unchanged" override rows.

| # | Name | Mode | Rows | Reason |
|---|------|------|------|--------|
| 0 | Neon Nights | Cosmos | level | warm DCO pad — register/decay already fit |
| 1 | Glass Highway | Cosmos | level | bright chorus'd arp; arpeggiates (96 onsets) |
| 2 | Velvet Fog | Cosmos | level | soft dark pad; cutoff 1200 correct now it sustains (was the rail-to-DC repro) |
| 3 | Sunset Strip | Cosmos | level | wide detuned pad — fits |
| 4 | Crystal Rain | Cosmos | level | sparkly delayed arp; arpeggiates (89) |
| 5 | Brass Section | Oracle | level | poly-mod brass, filter-env bite — fits |
| 6 | Wooden Keys | Oracle | level | mellow keys — fits |
| 7 | Poly Mod Bells | Oracle | level | bell f0 394, poly-mod inharmonic — fits |
| 8 | Dark Oracle | Oracle | level | dark sustained pad — fits |
| 9 | Stab Machine | Oracle | level | short percussive stab — fits |
| 10 | Pulsing Darkness | Mono | level | pulsing arp bass; arpeggiates (93) |
| 11 | Acid Squelch | Mono | level | resonant squelch line — fits |
| 12 | Screaming Lead | Mono | level | loud driven lead (peak −8.7) — fits |
| 13 | Sub Thunder | Mono | level | sine+sub sub-bass, f0 65 — fits |
| 14 | Sync Sweep | Mono | level | ring-mod sync sweep — fits |
| 15 | Upside Down | **Oracle** | **re-voiced** | flagship (see below) — was Modular |
| 16 | Sci-Fi Computer | Modular | level | random FM arp; arpeggiates (94) |
| 17 | Horror Drone | Modular | level | slow evolving drone — fits |
| 18 | Voltage Ghost | Modular | level | FM+sync sweep drone — fits |
| 19 | Retro Sequence | Modular | level | tape-delay arp; arpeggiates (88) |
| 20 | Midnight Drive | Cosmos | level | driven chorus pad — fits |
| 21 | Starfield | Cosmos | level | octave arp sparkle; arpeggiates (90) |
| 22 | Poly Brass | Oracle | level | poly-mod brass — fits |
| 23 | Glass Bells | Oracle | level | bright bell f0 1486, long decay — fits |
| 24 | Acid Machine | Mono | level | glide acid arp; arpeggiates (50) |
| 25 | Thunder Sub | Mono | level | square-sub sub-bass f0 49 — fits |
| 26 | Voltage Seq | Modular | level | S&H sequence; arpeggiates (80) |
| 27 | Alien Transmission | Modular | level | ring/FM/sync texture — fits |
| 28 | Warm Keys | Cosmos | level | tri+sine warm keys — fits |
| 29 | Analog Strings | Oracle | level | 4-voice unison string pad — fits (real unison) |
| 30 | Wobble Bass | Mono | level | LFO→cutoff wobble bass — fits |
| 31 | Tape Lead | Mono | level | glide tape-delay lead — fits |
| 32 | Drone Machine | Modular | level | huge FM drone — fits |
| 33 | Arp Factory | Cosmos | level | swung up-down arp; arpeggiates (78) |
| 34 | Fat Fifth | Oracle | level | unison fifths — fits |
| 35 | Noise Sweep | Modular | level | filtered-noise sweep — fits |
| 36 | Init Cosmos | Cosmos | level | init |
| 37 | Init Oracle | Oracle | level | init |
| 38 | Init Mono | Mono | level | init |
| 39 | Init Modular | Modular | level | init |

## Flagship — "Upside Down" (#15)

Re-voiced from Modular to **Oracle** per the design-doc mandate:

- two saws detuned 8 cents (`osc1Wave`/`osc2Wave` saw, `osc2Detune` 8);
- arp **Up-Down** 1/8 with **latch** (`arpOn`, `arpMode` 2, `arpRate` 3, `arpLatch`)
  — hold a Cmaj-ish chord and the famous 80s sci-fi title arpeggio plays out of
  the box;
- filter ~2 kHz with slight env (`filterCutoff` 2000, `filterEnvAmt` 0.25);
- subtle slow LFO on cutoff (`lfo1Rate` 0.3 → mod slot 0 → Filter Cutoff, amt 0.15);
- a touch of tempo-synced delay + light reverb.

**Verification** (render 8 bars @ 132 BPM holding C2+E2+G2+B2, `preset_render 15
… perf=hold hold=36,40,43,47 tempo=132 bars=8`):

| Metric | Target | Measured |
|---|---|---|
| Arp grid worst-dev (dry, 1/8 = 227.3 ms) | ≤ ~1 ms | **0.54 ms** |
| Pitch cycle (f0 track) | up-down through the 4 held pitches | **36→40→43→47→43→40** |
| Spectrum centroid | 800–2500 Hz (warm) | **1848 Hz** |
| Peak | non-silent, no clip | −30 dBFS |

Demo render saved as `upside_down_demo.wav`.

## New preset banks (indices 40–53)

All audited: non-silent, no clip, pitch-correct.

**Prism (mode 4, 4-op FM)**
- **Glass Keys** (40) — dual-stack tine e-piano (algo 4). Body = op2 1:1 → op1;
  tine = op4 ratio 14 fast-decay → op3, keyScale −0.35 rolls the tine off up the
  keyboard (Phase-2a calibration). Chorus for width.
- **Solid Bass** (41) — serial FM bass (algo 0) with op4 feedback 0.15 grit +
  filter-env pluck.
- **Crystal Bells** (42) — additive (algo 8) inharmonic partials (1 / 2.76 / 5.4 /
  8.93) with staggered decays (higher partials die first) + long release + reverb.
- **Brass Machine** (43) — serial FM with strong op4 self-feedback growl (0.6) and
  a brass attack swell.

**Acid (mode 5, diode-ladder box + 16-step sequencer)**
- **Silver Squelch** (44) — classic saw, res 0.82, mid env; accents on 1/5/9/13,
  slides into 4/8; rolling 1/16 pattern.
- **Rubber Bass** (45) — square, low res, tight 0.14 s decay → round bounce.
- **Night Crawler** (46) — slow 1/8 dark pattern, heavy 150 ms slide on most steps.
- **Screamer** (47) — near-self-osc res 0.95, maxed accent, drive on.

**Flagship across the remaining modes**
- **Aurora Drift** (48) — huge Cosmos DCO pad, dual chorus (Both), slow swell, wide.
- **Regal Brass** (49) — Oracle self-osc + poly-mod brass, 2-voice unison.
- **Siren Lead** (50) — Mono hard-sync screaming lead, driven, delay, glide.
- **Nebula Static** (51) — Modular sci-fi texture: S&H → cutoff + resonance,
  evolving pad, spring/hall reverb.
- **Glass Cathedral** (52) — Prism dual-stack pad, long swell, chorus + big reverb.
- **Neon Sequence** (53) — Acid + ping-pong tempo-delay groove.

## Audit summary (54/54 pass)

Peak/RMS in dBFS; f0 = loudest-segment fundamental (Hz); DC = dBFS below RMS.

```
  # name                    peak     rms    cent       f0 clip     dc  result
  0 Neon Nights           -24.3   -39.1     686    246.9    0 -105.0  PASS
  … (full table: run preset_audit.py)
 47 Screamer               -6.8   -18.3    3053     63.9    0 -145.2  PASS  (hottest)
 15 Upside Down           -31.0   -41.2    1884    131.4    0 -104.9  PASS
```

- **54 / 54 pass** the Phase 5 rules.
- Peak range −6.8 dBFS (Screamer) … −32.9 dBFS (Glass Highway / Arp Factory).
- 0 clipped samples anywhere; 0 non-finite; every preset audibly non-silent.
- All 9 arp presets among the original 40 produce 50–96 note onsets over 4 bars
  (they arpeggiate); all 5 acid presets run their pattern sequencers.
