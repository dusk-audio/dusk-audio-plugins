# DuskVerb tuning audit — read full_check as a DIAGNOSIS, not a count

## The rule

**Never trust a gate COUNT. Read gate CONTENT.** "Vocal Hall: 28 fails" told us
nothing for months. The truth — sitting in the full_check output the whole time —
was that ~17 of those fails were ONE broken thing: an octave-T60 table that the
preset's engine doesn't even read (orphaned by the 2026-06-13 DenseHall
migration). Minimizing the aggregate count rewards whack-a-mole and hides
root causes. Group fails into a few CAUSES, each mapped to an audible symptom.

## Before trusting any score, or before a release

```sh
cd ~/projects/plugins
# 1. Source-only, instant, no render — catches engine-migration orphans:
python3 plugins/DuskVerb/tools/tuner/fleet_audit.py --verify-tables   # exit 1 = dormant table

# 2. Commanded-vs-realized octave T60 for live AccurateHall presets (renders):
python3 plugins/DuskVerb/tools/tuner/fleet_audit.py --verify-calibration  # exit 1 = drift > 15%

# 3. Full fleet root-cause audit (ranked broken/tunable/structural):
python3 plugins/DuskVerb/tools/tuner/fleet_audit.py
```

## Root-cause classes

- **broken_calibration** — fixable miscalibration producing many fails:
  inverted/orphaned octave-T60 table, a runaway narrowband spec_L1 spike
  (e.g. +12 dB @ 12.9 kHz), a resonant overshoot (sine1k +14 dB, zero THD),
  a decay-mapping bug (Reverse Taps tail not gated). **Fix these first** —
  they are NOT engine limits. ~35% of the fleet's fails are this class.
- **tunable** — normal knob tilt (bass/air/level/centroid). Quick wins. ~25%.
- **structural** — the real engine walls: FDN early-field/front-load
  (attack, onset_slope, energy_t50/first50), 3-band-damping-vs-9-octave
  coupling (one feedback gain sets BOTH decay AND level per band), ~0 stereo
  width, the 12 kHz shifter-AA ceiling on Shimmer presets. Only ~37%. The
  only part that legitimately needs DSP work.
- **anchor_suspect / artifact** — anchor wrong, OR a full_check measurement
  artifact (peak-aligned tail_t30/t60 + edt/decay fire falsely on
  steady-onset stimuli where the tail sits below the burst peak — trust the
  Schroeder RT60 block there instead).

## Name-keyed config maps: orphan hazard

`setAccurateHallOctaveT60` writes ONLY to `accurateHall_`; `kAccurateHallT60ByName`
is inert on any other engine. `kDattorroOctaveT60ByName` → `dattorro_`/Vintage
only. When a preset migrates engines (algo change), its entries in these maps
become DORMANT — inert config that still looks authoritative and will mislead
the next person who reads it (it misled a diagnosis on 2026-06-16). `--verify-tables`
catches every such orphan from source. After any engine migration: re-route or
DELETE the preset's stale map entries.
