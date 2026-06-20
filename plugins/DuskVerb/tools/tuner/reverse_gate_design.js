export const meta = {
  name: 'reverse-taps-gate-design',
  description: 'Design the gated-reverse fix for ReverseRoomEngine (algo 9, Reverse Taps): precise gate envelope from the anchor + gate-attribution of the 45 fails + adversarially-verified C++ design',
  phases: [
    { title: 'Investigate', detail: 'parallel: anchor envelope spec (5 stimuli), 45-gate attribution, engine integration+history' },
    { title: 'Verify', detail: 'skeptic refutes the gate design vs the prior +60dB-cliff failure, click/zipper, sustained behavior' },
    { title: 'Synthesize', detail: 'one architect: concrete C++ spec for ReverseRoomEngine + per-preset params + pass thresholds' },
  ],
}

const DSP   = '/home/marc/projects/plugins/plugins/DuskVerb/src/dsp'
const FP     = '/home/marc/projects/plugins/plugins/DuskVerb/src/FactoryPresets.h'
const ANCHOR = '/home/marc/projects/dusk-audio-tools/anchors/rendered/lex-reverse-1'
const FC     = '/tmp/fc_reverse.txt'

// what I already measured (so agents don't have to rediscover it):
const MEASURED = `
ANCHOR (lex-reverse-1) is a HARD-GATED REVERSE, confirmed from the WAVs:
- snare: silence(-153dB) to ~100ms, swell UP 100->400ms to PEAK -29dB, fade 400->650ms,
  HARD GATE to -134dB at ~700ms. So ~600ms total event then digital silence.
- sustained pink: swells up then HOLDS at ~-20dB (gate stays OPEN while input present).
- full_check vs current DV: attack_time anch 344ms / DV 36ms; tail_t60 anch 0.094s / DV 5.1s;
  env_p2p anch +72dB / DV +16dB; osc_p2p anch +52dB / DV +15dB; cent_50 anch 7725Hz / DV 2681Hz;
  decay bands anch 0.2-0.48s / DV 1.6-3.3s.
CURRENT ENGINE (ReverseRoomEngine): rising-ER tap FIR (rampMs_=45ms, slope 1.6, floor 0.45)
-> FDN tail (decay 2.0s baseline, preset sets ~6.99s). NO GATE -> tail rings 4.6-5s.
Engine was reverse-engineered from an OLDER anchor (env_p2p +24.6, peak 70ms, t60 4.6s, NO gate).
The current anchor is GATED (+72dB, hard cut) -> engine & anchor MISMATCH. Prior history
(FactoryPresets comment): a NonLinear hard gate was tried + REJECTED as "+60dB silence->blast
cliff wrong" -- but that verdict was against the OLD ungated anchor; the current anchor WANTS
the cliff. algo 9 is used by ONLY this 1 preset -> any engine change is bit-null for the fleet.
`

const FIND = {
  type: 'object', additionalProperties: false,
  required: ['summary', 'numbers', 'key_code', 'pitfalls'],
  properties: {
    summary: { type: 'string' },
    numbers: { type: 'array', items: { type: 'string' }, description: 'concrete params: ms / dB / Hz / sample counts the implementer needs' },
    key_code: { type: 'array', items: { type: 'string' }, description: 'file:line refs + verbatim snippets the fix builds on/changes' },
    pitfalls: { type: 'array', items: { type: 'string' }, description: 'prior failures / artifacts / bit-null / determinism traps' },
  },
}

phase('Investigate')
const reads = await parallel([
  () => agent(
    `Extract the PRECISE gate-envelope spec for the ReverseRoomEngine fix from the lex-reverse-1 anchor. READ the 5 WAVs in ${ANCHOR} (impulse/snare/noiseburst/sine1k/sustained) with python (soundfile+numpy via Bash) and measure, per stimulus: (1) PREDELAY = silence before onset (ms); (2) SWELL = rise duration onset->peak (ms) + the shape (linear/convex/concave, fit gain~(t/T)^slope, report slope); (3) PEAK time (ms); (4) HOLD = time from input-END to the gate cut (the gate keys off the DRY input — for the impulsive snare the input is ~50ms, so hold = gate-cut-time minus input-end); (5) RELEASE = how steep the final cut is (ms from -3dB to noise floor) — is it a hard cliff or a fast fade?; (6) SUSTAINED behavior: does the level HOLD while pink noise is present, and only cut after it stops? Confirm the gate is INPUT-KEYED not free-running. Also report the target metrics the gate must hit: attack_time, env_p2p, tail_t60, decay-band t30, energy_t50, per stimulus. Context: ${MEASURED}. Return structured (numbers = the concrete ms/dB/slope values).`,
    { label: 'anchor:envelope-spec', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Attribute the 45 failing gates for Reverse Taps to root cause. READ ${FC} (the full full_check gate detail). Bucket EVERY failing gate into: (A) GATE-SHAPE-driven — will be FIXED by adding a long swell + an input-keyed hard gate to the engine (e.g. tail_t30/t60, decay bands, edt bands, attack_time, onset_slope, energy_t50, env_shape_l1, env_p2p, osc_p2p); (B) LEVEL/SPECTRAL — tunable via the FDN/preset params after the gate is in (cent_50, spec_L1, ss-band energy, RMS levels, width); (C) RESIDUAL STRUCTURAL — genuinely hard (comb ripple from discrete taps, tail mod-freq, stereo_corr sign). For each bucket list the exact gate names + count. Give an HONEST estimate: after the gate+swell fix + a level/spectral re-tune, how many of 45 plausibly close, and what's the residual floor? Context: ${MEASURED}. Return structured (numbers = per-bucket counts + the estimated post-fix n_fail).`,
    { label: 'gates:attribution', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Map the INTEGRATION + HISTORY for adding an input-keyed gate to ReverseRoomEngine. READ ${DSP}/ReverseRoomEngine.cpp + .h fully, how ${DSP}/DuskVerbEngine.cpp drives algo 9 (grep reverseRoom_, the process() path, clearBuffers, predelay handling), and the FactoryPresets.h Reverse Taps comment block (around line 1192-1224) for the documented prior gate attempts + why they were rejected. Report: (1) the EXACT injection point for the gate — process() step 2 outputs outL/outR after fdn_.process; (2) the state to add (input envelope follower coeff, gate gain + its smoothing, hold-timer sample counter, state enum); (3) the bit-null / determinism argument (algo9 = 1 preset; fixed-seed taps must stay reproducible); (4) the prior-failure traps to avoid (the +60dB cliff was rejected vs the OLD ungated anchor; comb ripple; mod). (5) Can the swell duration just be rampMs_ + the FDN buildup, or does the FDN's own decay fight the gate? Context: ${MEASURED}. Return structured (key_code = exact hook sites + snippets).`,
    { label: 'engine:integration', phase: 'Investigate', schema: FIND }),
])

phase('Verify')
const [env, gates, integ] = reads
const verdict = await agent(
  `Adversarially review the GATED-REVERSE design for ReverseRoomEngine before it's implemented. The design: extend the rising-ER swell to ~the anchor's swell duration, AND add an INPUT-KEYED gate on the FDN output (envelope-follow the dry input; gate opens with attack, HOLDS while input present + a fixed hold after input stops, then HARD-releases to silence — cutting the 4.6s FDN tail to match the anchor's t60 0.094s + env_p2p +72dB).
  ENVELOPE SPEC: ${JSON.stringify(env)}
  GATE ATTRIBUTION: ${JSON.stringify(gates)}
  INTEGRATION: ${JSON.stringify(integ)}
  Refute hard on: (1) CLICK/ZIPPER — a hard release to silence on a ringing FDN tail will click unless the gate gain is smoothed; what release time avoids the click while still reading as a "hard" cut + hitting env_p2p +72 / t60 0.094? (2) SUSTAINED input must NOT gate off (the anchor holds ~-20dB) — does an input-keyed follower with hold handle both the impulsive snare AND continuous pink correctly? (3) RETRIGGER — a second transient during the hold/release must re-open cleanly. (4) Is the +72dB env_p2p actually achievable without the literal-silence cliff the prior attempt was faulted for, given the current anchor genuinely IS that cliff? (5) determinism + bit-null (1 preset). For each risk: viable yes/no + the concrete fix (param value / smoothing time). Return {viable:bool, risks:[...], fixes:[...], release_ms:number, hold_ms:number, smoothing_note:string}.`,
  { label: 'verify:gate', phase: 'Verify', effort: 'high',
    schema: { type:'object', additionalProperties:false, required:['viable','risks','fixes','release_ms','hold_ms','smoothing_note'],
      properties:{ viable:{type:'boolean'}, risks:{type:'array',items:{type:'string'}}, fixes:{type:'array',items:{type:'string'}}, release_ms:{type:'number'}, hold_ms:{type:'number'}, smoothing_note:{type:'string'} } } })

phase('Synthesize')
const spec = await agent(
  `You are the DSP architect. Write a CONCRETE, implementable C++ spec (markdown) for the ReverseRoomEngine gated-reverse fix, handed to the implementer (who edits ReverseRoomEngine.cpp/.h, builds DuskVerb_VST3, renders via duskverb_render --program "Reverse Taps", gain-matches to lex-reverse-1, runs full_check, iterates).
  ENVELOPE SPEC: ${JSON.stringify(env)}
  GATE ATTRIBUTION: ${JSON.stringify(gates)}
  INTEGRATION: ${JSON.stringify(integ)}
  VERIFY VERDICT: ${JSON.stringify(verdict)}

  MUST specify:
  1. The exact engine changes: new members (envelope follower coeff/state, gate gain + smoothing, hold-sample counter, state machine), the process() step-2 changes (compute dry-input envelope, advance gate, multiply outL/outR), rampMs_/slope changes for the longer swell. Give formulae + the one-pole coeffs.
  2. The gate behavior precisely: attack ms, hold ms, release ms, threshold, how it keys off the DRY input (inL/inR pre-ER), how it handles sustained (hold open) vs impulsive (cut after hold), retrigger.
  3. Concrete START param values for Reverse Taps (rampMs, slope, threshold dB, attack, hold, release) derived from the envelope spec.
  4. bit-null/determinism argument (algo 9 = 1 preset; fixed-seed reproducibility).
  5. Make-or-break tests + pass thresholds: attack_time ~344ms, tail_t60 ~0.094s, env_p2p toward +72, decay bands ~0.2-0.48s, no click on release; and the realistic target n_fail (from the attribution) — honest about the residual structural floor.
  6. Whether any new param should be a per-preset map/env override for tuning (DUSKVERB_* pattern) vs hard-coded engine constants.
  Be concrete. Return ONLY the markdown spec.`,
  { label: 'synthesize-spec', phase: 'Synthesize', effort: 'high' })

return spec
