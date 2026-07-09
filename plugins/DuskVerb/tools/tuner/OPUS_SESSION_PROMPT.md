# Paste this as the opening prompt for the next Opus 4.8 session

You are continuing the DuskVerb anchor-matching campaign in ~/projects/plugins
(branch 4k-eq/dpf-core; large amount of UNCOMMITTED verified work in the tree, so never
revert it, never commit unless I explicitly ask, and if I do ask, stage DuskVerb + tuner
files selectively because unrelated 4k-eq DPF work shares the tree).

STEP 0, read in order before touching anything:
1. plugins/DuskVerb/tools/tuner/HANDOFF_2026-07-06_opus48.md, the operating picture.
   Start at §8.5 SESSION END-STATE, then §9 changelog (P1-P6 + three agent entries), then
   the rest. Every trap in §7 has already cost days once; do not re-learn them.
2. plugins/DuskVerb/tools/tuner/scoreboard_2026-07-07.md, per-preset failing gates.

STATE SNAPSHOT (numbers below are a 2026-07-07 checkpoint and drift every session; always
re-read the newest scoreboard_2026-07-07.json for the authoritative per-preset counts and the
current fleet total before acting):
- Fleet: 18 anchored presets. Vocal Hall migrated to the new ParallelMultiband engine
  (algo 15) at n_fail 14 and ear-approved as "super close"; Deep Blue Day and Live Room
  retuned; tooling + detection upgraded (fleet_audit --out/prechecks, THD gate fleet-wide,
  whoosh gate, echo-density + autocorr-lag advisories, tune_preset repaired). Later sessions
  have moved several presets further, so trust the scoreboard, not these lines.
- Everything verified: bit-null render diffs for untouched presets, pluginval 1/5/7/10,
  9/9 harness, program-path audits. The verification protocol that made that possible is
  in the handoff; follow it for every change you ship.

MY PENDING EAR VERDICTS (I have the A/B files; ask me before acting on these):
1. Vocal Hall "whoosh" 3-way — if I approve, bake per §8.5 item 1 (kDiffuseERByName row +
   hpHz 1800→2400 + Cathedral bit-null check).
2. Live Room dense-field fill — if I say inaudible, delete its row.
3. Deep Blue Day post-THD-fix pair.

WORK QUEUE (serialize anything that rebuilds/installs ~/.vst3 or renders — one lock):
1. Small Drum Room: ERTAPS bank, taps ≈[70,100,158] ms + kill the DV-only 16.6 ms comb
   (roomfill/det[4] lever). Dense-field is a PROVEN dead end there.
2. Ambience per-band width bakes (W7).
3. Whoosh-gate calibration (tighten toward ±1.0 dB) + transient-ducked diffuse bus.
4. Vocal Plate PMB migration (second one; T60 lever = the octave table, §6.1).
5. Advisory-gate promotion review (echo_density first).
Then the big engine workstreams in §3 order: W3 modal density, W4 HF-kurt, W5 decay shape.

HOW TO WORK (the shape that worked today):
- Diagnose per gate, don't chase counts. Baseline before touching anything
  (fleet_audit.py --preset "<name>"). Env-var sweeps (DUSKVERB_*) before any bake.
  Never-worse on fresh renders. Bit-null proof by byte-comparison for every preset you
  didn't intend to change. pluginval + harness at the end. Ear A/B to me before calling
  anything shipped — my ear is sign-off, not QA.
- When I say "send out agents": parallel agents ONLY on file-disjoint, render-free scopes
  (gates/tooling); exactly one agent may build/render at a time; every agent reads the
  handoff first and appends a dated entry to §9; no agent commits.
- Renders: 100% wet, --program never --preset, gain-match to anchor noiseburst, FLOAT
  subtype, never pedalboard, never unity build. Shimmer presets: native mix=0.5 +
  --long-sine-seconds 15.

Report progress the way the changelog reads: what moved, what number, what file, what
died and why — negative results are deliverables here.
