# DuskVerb FoilPlateEngine — Session Handoff

## Mission

Build a vintage foil-plate emulation engine (algo 9, `FoilPlateEngine`)
that matches the reference Rich Plate anchor on **every** measured
psychoacoustic metric (RT60 per band, EDT, C80, D50, A-weighted volume,
stereo correlation, stereo correlation stability, modal density). User
mandate: *"all metrics within JND. No exceptions. Either tune or build
a new engine — never settle."*

Engine name avoids brand names ("Lex" / "Lexicon") for trademark
safety — anchor and tooling reference them, code does not.

## Branch / Repo State

- Branch: `87-possible-issues-with-duskverb-FDN`
- 24 commits ahead of `origin/87-possible-issues-with-duskverb-FDN`
- Working tree clean as of handoff
- No PR open

## What's Done (Committed)

| Commit | Title |
|---|---|
| `aa1cce4` | DuskVerb FoilPlateEngine (algo 9): second-gen foil-plate engine — 16 kHz RT60 + stereo_corr_stability now within JND |
| `d7101e1` | DuskVerb FoilPlateEngine: per-band feedback filters + Rich Plate retune → 7/8 RT60 + Volume PERFECT |
| `5e609f5` | DuskVerb FoilPlateEngine: bypass DuskVerbEngine input diffuser → stereo_correlation closes within JND |

### Current Rich Plate measurement (commit `5e609f5`):

```
RT60 per band (Hz):   125    250    500    1k     2k     4k     8k     16k
                      lex:   1.568  1.338  1.298  1.283  1.333  1.323  1.263  1.100
                      dv:    1.545  1.287  1.242  1.224  1.337  1.238  1.224  1.160
                      Δ:    -0.023 -0.051 -0.055 -0.059 +0.005 -0.085 -0.038 +0.060
                                                                  ↑
                                                       4 kHz only off-band; 7/8 within JND

EDT                  1.91 s    1.51 s    Δ −0.39 s  (out of JND)
C80                  −1.06 dB  +1.03 dB  Δ +2.09 dB (out)
D50                  −5.23 dB  −1.72 dB  Δ +3.52 dB (out)
Volume               −43.54    −44.16    Δ −0.62    fractionally out
stereo_correlation   0.039     0.069     Δ +0.030   WITHIN JND (closed this session)
stereo_corr_stab     0.029     0.180     Δ +0.150   out
```

### Architecture (4 pillars, all in code)

1. **OnsetEnvelope** — peak-detect dry input, ramp wet gain 0.30 → 1.0
   with τ = 30 ms after each transient. Pulls C80/D50/EDT toward
   late-energy-dominant target.

2. **Deterministic sine LFOs** — phase accumulator + `std::sin`, L at 0°,
   R at π (180°). Designed to lock stereo_corr_stability low.
   **EXPERIMENTAL FINDING (uncommitted, see below): modulation HURTS
   stab; no modulation lands it at Δ +0.002 PERFECT but breaks 8k/16k
   RT60. Investigate next.**

3. **Flat 2-AP diffuser** at g=0.55 — replaces PlateEngine's 6-AP
   cascade. Eliminates HF compounding loss → 16 kHz RT60 closed.

4. **Per-band LR4 split + 3 parallel BandReverberators** — input split
   upstream of any feedback, 3 independent feedback comb loops with
   per-band LR4 feedback filter (LP at fLow for bass, HP at fHigh for
   treble, broadband for mid). Per-band RT60 fully decoupled. Output
   normalised via `(1 − g)` for stable cross-feed; engine-wide
   `kEngineOutputGain = 3.0` applied OUTSIDE the cross-feed loop to
   restore amplitude.

### Files

- `plugins/DuskVerb/src/dsp/FoilPlateEngine.h` (341 lines)
- `plugins/DuskVerb/src/dsp/FoilPlateEngine.cpp` (~590 lines)
- `plugins/DuskVerb/src/dsp/AlgorithmConfig.h` — algo 9 = `FoilPlate`,
  `getNumAlgorithms()` returns 10
- `plugins/DuskVerb/src/dsp/DuskVerbEngine.h/.cpp` — `foilPlate_`
  member + 15 setter forwarders + dispatch case
- `plugins/DuskVerb/CMakeLists.txt` — adds `FoilPlateEngine.cpp`
- `plugins/DuskVerb/src/FactoryPresets.h` — Rich Plate row migrated
  to algo 9; "Modern Clear Plate" preserved at algo 8 (the 7/8
  PlateEngine snapshot from prior session)
- `tests/duskverb_render/render.cpp` — Rich Plate handler points at
  algo 9, divisor 8 → 9 (three call sites)

### Rich Plate current parameters (algo 9)

```cpp
// FactoryPresets.h
{ "Rich Plate",           "Plates",
  9,  0.40f, false,  0.0f, 0,
  1.300f, 0.950f, 0.300f, 1.000f, 0.780f, 1.500f,  200.0f,
  0.500f, 0.00f, 0.30f,  20.0f, 18000.0f, 1.000f, false, 1.200f,
  /* mono */ 20.0f, /* mid */ 0.940f, /* highX */ 9000.0f, /* sat */ 0.000f },
//                                                                  ↑
//   decay 1.3 / mod 0.3 / treble 0.78 / bass 1.50 / xover 200 /
//   diffusion 0.5 / gainTrim +1.2 dB / midMult 0.94 / highX 9000
```

`tests/duskverb_render/render.cpp` Rich Plate handler MUST mirror these
values verbatim — every retune touches both files.

## Session 2 (2026-05-19) — What Was Learned

### Diffuser-bypass fix → stereo_correlation closes

Root cause: `DuskVerbEngine::process` runs a 4-stage Schroeder input
diffuser on tank input. FoilPlate wasn't in the bypass list, so the
impulse was smeared upstream of FoilPlate's own 2-AP diffuser (Pillar
3). That smearing offset the IR peak by +500 samples and bled L/R
correlation. Adding `FoilPlate` to the bypass list:

- `stereo_correlation` Δ +0.058 → +0.030 (CLOSED)
- C80 / D50 each improved ≈ 0.65 dB (still out)
- Vol drifted -0.6 dB

### Mono-tank refactor — attempted, reverted

Hypothesis: dual L/R reverbs with prime-asymmetric delays produce
chaotic modal evolution → `stereo_corr_stability` drift. Real plate
is one surface, sampled at two pickup positions → STABLE cross-corr.

Built: single MonoPath driven by `(L+R)/2`, stereo derived via
polyphase IIR Hilbert pair (Niemitalo 4-AP coefficients, applied as
2nd-order APs via z→z² substitution) + linear M/S widener.

Measured: corr = +0.21 to +0.89 depending on coefficients (Hilbert
designs that I tried lacked sufficient 200 Hz – 9 kHz coverage); stab
= 0.18 to 0.24 (WORSE than the dual-branch baseline). Reverted.

Lesson: even time-invariant linear filters do not lock cross-corr
across windows when the SOURCE spectrum is non-stationary (which
reverb tails always are — HF decays faster than LF, so the spectral
envelope evolves). Hilbert pair would lock corr if its 90° band
covered the full reverb passband; the published Niemitalo 4-AP and
2nd-order-AP coefficient sets I tried in C++ did NOT produce 90°
phase difference in the 200 Hz – 9 kHz band when verified in numpy.
The polyphase IIR Hilbert math is right; the coefficients were
wrong.

If anyone retries the mono-tank approach: design Hilbert pair
COEFFICIENTS NUMERICALLY (scipy elliptic halfband decomposition, or
direct optimisation), and VERIFY the 90° phase span in Python before
committing C++ code. Don't trust coefficient sets from memory or
from web sources without verification.

### Onset-envelope expansion — hold-then-ramp infrastructure added

`OnsetEnvelope::setShape (holdMs, tauSec, minGain)` now supports a
hold period at minGain before the ramp starts. Default `holdMs = 0`
preserves the original single-pole behaviour byte-identically (the
new branch in `process()` is a no-op when hold is zero).

Tuning attempts that closed individual metrics but broke others:

- hold = 80 ms, τ = 30 ms, minGain = 0.05 → **8/8 RT60** (all bands
  within JND!), EDT Δ +0.13 (over). Peak shifts to ~230 ms, which
  re-positions C80/D50 windows past most of the wet energy → these
  metrics go further out, not in.
- minGain = 0.30, τ = 250 ms (single-pole, no hold) → EDT PERFECT
  (Δ −0.001), C80 in JND (Δ −0.86), D50 marginally over (Δ +0.69).
  But RT60 4/8 BROKEN because the long ramp extended into the
  T20 fit region (~ 108–542 ms) and shifted the measured slope.

Fundamental tension: peak-relative metrics (EDT, C80, D50) are
exquisitely sensitive to onset-envelope shape because the metric's
peak-detector latches onto whatever sample is loudest. The
envelope's amplitude shaping moves the loudest sample, which moves
the measurement windows. Single-shape envelopes can satisfy any ONE
metric but not all simultaneously.

### Per-channel decorrelation experiments (stab paradox)

Same conclusion as the previous handoff: matched L/R delays didn't
help stab (it stayed at 0.165 vs the dual-branch 0.172); cross-feed
gain = 0 made stab WORSE (0.212); shared LFO phase (0° R) improved
corr (0.075) but stab only dropped to 0.124 — none of these reached
the lex anchor's 0.029.

Confirmed: `stab` is the toughest remaining metric. The handoff's
"chained allpass pair replaces BandReverberator delay" suggestion
hasn't been tried yet.

## What Failed / Open Questions

### Stereo correlation stability paradox

Pillar 2 (deterministic sine LFO, 180° L/R phase offset) was supposed
to lock `stereo_corr_stability` by construction. It didn't. Measured
values:

| Config | mod depth | LFO rates | stab Δ | RT60 |
|---|---|---|---|---|
| Committed | 0.30 | 0.31/0.47/0.71 Hz (slow) | +0.14 (out) | 7/8 |
| Experimental (reverted) | 0.30 | 3.13/4.97/7.31 Hz (fast) | +0.14 (out) | 5/8 |
| Experimental (reverted) | 0.00 | (no mod) | **+0.002 PERFECT** | 5/8 (8k/16k blown out) |

So: zero modulation gives perfect `stab` but breaks treble RT60 because
the static delays produce strong modal ringing in those bands. The
deterministic LFO with phase offset doesn't behave as predicted because
the cross-feed loop interacts with the modulation — anti-correlated L/R
delay reads turn into asymmetric pitch-shifted echoes that drift over
time despite their phases being locked.

**Next-session experiments worth running:**
- Use SAME L/R base delays (drop the prime asymmetry) and rely on
  LFO phase offset alone for decorrelation. Likely jumps stereo
  correlation toward 1.0; check if correlation can be brought back
  with a different mechanism (allpass with opposite-sign coefficients,
  or DC-shifted cross-feed).
- Replace the BandReverberator delay with a **chained pair of allpass
  filters**. Allpass magnitude response is unity → 8k/16k modal energy
  spreads in time without amplitude peaks. No modulation needed.
- Or detune L/R LFO rates by ~0.5 Hz (e.g. L bass at 3.13 Hz, R bass
  at 3.61 Hz) — phase relationship slowly walks but never drifts;
  cross-correlation over 100 ms windows should look stable if the rate
  difference is small enough.

### 4 kHz RT60 stuck

4 kHz sits in the mid band but always reads 0.10 s short relative to
the rest of the mid octaves. Mid band runs broadband (no feedback
filter). The single 541-sample base delay has fundamental at 89 Hz; its
45th harmonic = 4005 Hz, close to a modal null. Likely fix is to give
the mid loop two parallel delays summed, or add a parallel "shelf"
loop that fills the 4 kHz dip.

### C80 / D50 / EDT not landed

OnsetEnvelope helped (C80 +3.85 → +2.74, D50 +4.74 → +4.12, EDT −0.44
→ −0.41) but still well outside JND. Options:
- Lower `kOnsetMinGain` from 0.30 → 0.15 (deeper attenuation at
  transient onset).
- Lengthen τ from 30 ms → 50 ms (slower ramp, more late-energy
  emphasis).
- Pre-tap the wet output (sum bands × 0.3 raw mixed with sum bands ×
  envGain × 0.7) so SOME early energy survives — measurement target
  isn't *zero* early energy, just less of it.

### stereo_correlation +0.058

Fractionally over the 0.05 JND. Easy to close by reducing
`kCrossFeedGain` from 0.62 → 0.55, which lowers L↔R coupling. May
nudge other metrics; verify after the bigger changes above.

## Files To Read Before Coding

1. `plugins/DuskVerb/src/dsp/FoilPlateEngine.h` — entire file. Header
   carries the design rationale comments.
2. `plugins/DuskVerb/src/dsp/FoilPlateEngine.cpp` — entire file (~590
   lines).
3. `plugins/DuskVerb/src/dsp/PlateEngine.h/.cpp` — predecessor engine;
   re-read its bass-extension and density-AP delay-shift sections to
   understand why those mechanisms hit a ceiling here.
4. `plugins/DuskVerb/tools/tuner/metrics.py` — `measure_pair()` is the
   ground truth. `_stereo_corr_stability` and `_modal_density` are
   the metrics with no JND baseline yet — their math matters when you
   propose an architectural fix.

## Reproducing the Measurement Loop

```bash
cd ~/projects/plugins/build
cmake --build . --config Release --target DuskVerb_VST3 duskverb_render -j8
cd ..
rm -rf /tmp/dv && build/tests/duskverb_render/duskverb_render "Rich Plate" --slug dv --output-dir /tmp/dv > /dev/null 2>&1
python3 -c "
from pathlib import Path
import sys
sys.path.insert(0, 'plugins/DuskVerb/tools/tuner')
from metrics import measure_pair
r_l = measure_pair(Path('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/rich_plate/lex_richplate_v1_impulse.wav'),
                   Path('/home/marc/projects/dusk-audio-tools/anchors/lex/wavs/rich_plate/lex_richplate_v1_noiseburst.wav'))
r_d = measure_pair(Path('/tmp/dv/dv_impulse.wav'), Path('/tmp/dv/dv_noiseburst.wav'))
def fmt(v):
    if isinstance(v, list): return '['+','.join(f'{x:.3f}' if x else 'None' for x in v)+']'
    return f'{v:.3f}' if v else 'None'
for k in ['rt60_per_band','edt','c80','d50','a_weighted_rms_db','stereo_correlation','stereo_corr_stability']:
    l,d = r_l.get(k), r_d.get(k)
    print(f'{k}: lex={fmt(l)}  dv={fmt(d)}')
    if isinstance(l,list):
        deltas = [(b-a) for a,b in zip(l,d) if a and b]
        within = sum(abs(x) < 0.065 for x in deltas)
        print(f'  delta={fmt(deltas)}  within-5%-JND: {within}/8')
    else:
        print(f'  delta={fmt(d-l)}')
"
```

The Lex anchor lives at `~/projects/dusk-audio-tools/anchors/lex/wavs/rich_plate/`
(persistent — survives `/tmp` wipes).

**JND thresholds the tuner enforces:**
- RT60 per band: ±5 % relative (≈ ±0.065 s at 1.3 s target)
- EDT: ±0.05 s
- C80: ±1.0 dB
- D50: ±1.0 dB
- A-weighted RMS: ±1.0 dB
- stereo_correlation: ±0.05
- stereo_corr_stability: ±0.05

## Critical Constraints (from this session's memory)

- **NEVER USE PEDALBOARD** for rendering. Banned. Use only
  `duskverb_render` (the JUCE-based host harness).
- ALWAYS normalize the volume of DV before tuning psychoacoustic
  values. Then JND every metric. No exceptions.
- Don't use "Lex" or "Lexicon" in code names, namespaces, or new
  user-facing strings. Engine class is `FoilPlateEngine`, namespace
  `foil_plate`, factory algo "Plate (Foil II)".
- Render fixture (`tests/duskverb_render/render.cpp`) MUST mirror
  `FactoryPresets.h` Rich Plate row exactly. Sync both files in the
  same commit when retuning. Both files carry a "TUNING IN FLIGHT"
  marker.
- Commit messages: no `Co-Authored-By: Claude` trailer, no Test Plan
  checklist on PRs, no 🤖 footer.
- Brutal honesty over false optimism — if the engine plateaus, say
  so. Don't claim metrics are within JND that aren't.

## Suggested Plan for Next Session

1. **First 10 min** — rebuild + re-render Rich Plate, confirm
   handoff metrics reproduce: 7/8 RT60, Volume PERFECT, EDT/C80/D50
   /stab still over.
2. **stereo_corr_stability investigation** — try the three
   experiments listed in the "stereo paradox" section. Measure each.
   If allpass-chain BandReverberator wins, implement that and re-run.
3. **C80/D50/EDT push** — lower `kOnsetMinGain` first (smallest risk
   change). Iterate.
4. **4 kHz fix** — only after the above; small effect relative to
   the other three.
5. **stereo_correlation fine-tune** — reduce `kCrossFeedGain`
   incrementally.
6. **Build full audit + per-engine smoke test** — render all other
   factory presets to confirm none regressed.
7. **Commit each milestone separately** — caveman-style commit
   messages, "DuskVerb FoilPlateEngine: ⟨one-line summary of what
   closed and what's left⟩".

## Don't Touch

- `Modern Clear Plate` (algo 8) — the PlateEngine 7/8 snapshot.
  Keep stable as a fallback even if FoilPlate eventually replaces
  every algorithm-9 use.
- `PlateEngine` itself — the AP-shift / pre-delay / bass extension
  work in there is load-bearing for `Modern Clear Plate`.
- Existing factory presets on other engines — they're tuned against
  their own anchors.
