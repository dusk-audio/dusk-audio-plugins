export const meta = {
  name: 'hall-engine-forks-design',
  description: 'Design the two DenseHall engine forks (early-field discrete reflections + per-band level decoupling) grounded in the actual code + prior failures; adversarially verify; output an implementable spec',
  phases: [
    { title: 'Investigate', detail: 'parallel: early-field infra+prior-fail, AccurateHall tonal-correction to port, DenseHall integration points' },
    { title: 'Verify', detail: 'skeptic refutes each design vs the documented prior failures + coupling/codegen walls' },
    { title: 'Synthesize', detail: 'one architect: concrete implementable spec for both forks + make-or-break + order' },
  ],
}

const DSP = '/home/marc/projects/plugins/plugins/DuskVerb/src/dsp'
const PROC = '/home/marc/projects/plugins/plugins/DuskVerb/src/PluginProcessor.cpp'
const MEM = '/home/marc/.claude/projects/-home-marc-projects-plugins/memory'

const FIND = {
  type: 'object', additionalProperties: false,
  required: ['summary', 'key_code', 'approach', 'pitfalls'],
  properties: {
    summary: { type: 'string' },
    key_code: { type: 'array', items: { type: 'string' }, description: 'file:line refs + verbatim snippets that the fork builds on/changes' },
    approach: { type: 'string', description: 'the concrete DSP approach (signal flow, params, where it hooks in)' },
    pitfalls: { type: 'array', items: { type: 'string' }, description: 'prior failures / bit-null / CPU / coupling traps to avoid' },
  },
}

phase('Investigate')
const reads = await parallel([
  () => agent(
    `Design FORK A — EARLY-FIELD discrete reflections for the DenseHall halls. GOAL: (1) the "duh-duh" = a DISCRETE second reflection at ~90-110 ms post-onset at −3 to −6 dB (measured: the VVV hall anchors have a strong second arrival there; DV decays smoothly through it). (2) front-load fix: the DenseHall tank attacks fast (onset_slope +350%, energy_t50 ~100ms vs anchor ~190-250ms) — the anchor builds GENTLY then has the discrete tap.
    READ + ground in: ${DSP}/EarlyReflections.h/.cpp (the er_ 24-tap 8-80ms stage + setOnsetRiseMs), ${DSP}/SparseEarlyField.h (algo 11 velvet-noise sparse early field), ${DSP}/DenseHallReverb.h (the tank + its 10-stage input diffusion), and how DuskVerbEngine sums er_/tank (grep er_.process, useSmoothER, tankOnset). CRITICAL — read the PRIOR FAILURE: ${MEM}/duskverb_fleet_anchor_tuning_2026-06.md (the tank-onset delay was REVERTED — "100ms tail delay sounds like a literal delay: ER->SILENCE->tail gap; the sparse ER can't bridge it"). Also ${MEM}/duskverb_energy_arrival_gate_and_wall.md.
    Design the approach that DELIVERS the discrete ~100ms tap + gentle build WITHOUT the silence-gap failure. Options to weigh: a dedicated discrete reflection-tap delay (single/dual tap, per-preset time+gain, fed from input, summed to wet); extending er_ with a late prominent tap; reusing SparseEarlyField's velvet taps; tank-onset + a DENSE bridging ER. Pick one, justify vs the prior failure. Return structured.`,
    { label: 'design:early-field', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Design FORK B — per-band LEVEL DECOUPLING for DenseHall (the T60↔level coupling: on DenseHall, matching a band's T60 forces its steady-state LEVEL, so the halls read tail-hot in lows/mids + dark in HF, e.g. Large Chamber ss bands all +2-5 dB, cent_50 −41%). AccurateHall (FDNReverbT<true>) ALREADY SOLVES THIS via setTonalCorrection (a Jot output GEQ that flattens per-band steady-state energy so decay≠level).
    READ + ground in: ${DSP}/FDNReverb.h + FDNReverb.cpp — find setTonalCorrection, the Jot/tonal-correction output GEQ design (how it derives the correction from the octave T60 targets, corr_b = minT60/T60_b style), and how it's applied (output stage, bit-null when off). Then ${DSP}/DenseHallReverb.h (where its per-octave GEQ lives now — the octDamp_ + octCoeffs_ I added; the lineRead damping). Design how to add an equivalent OUTPUT tonal-correction GEQ to DenseHall (post-tank, derived from the same per-octave T60 targets) so a band's LEVEL is flattened while its T60 stays. Note: it must be a post-tank OUTPUT filter (non-recursive → no codegen bit-null risk), default-off = bit-identical. Return structured.`,
    { label: 'design:decoupling', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Map the DenseHall + DuskVerbEngine INTEGRATION points for adding two forks (an early-field discrete-reflection stage + a post-tank tonal-correction GEQ). READ ${DSP}/DuskVerbEngine.cpp (the process() chain: where er_ is summed, the DenseHall case, the output stage, the per-sample loop) + ${DSP}/DuskVerbEngine.h (members, setters) + how per-preset config is applied in ${PROC} (the kXxxByName map pattern, applyEngineConfig, the env-override pattern). Report: exactly WHERE each fork hooks in, the existing per-preset-param mechanism to reuse (kXxxByName map + env override), the bit-null gating pattern (default-off), and the codegen-drift constraint (${MEM}/duskverb_bitnull_codegen_limit.md — recursive-loop edits drift FP; out-of-line/post-tank is safe). Return structured (key_code = the exact hook sites).`,
    { label: 'map:integration', phase: 'Investigate', schema: FIND }),
])

phase('Verify')
const [ef, dc, integ] = reads
const verdicts = await parallel([
  () => agent(
    `Adversarially review FORK A (early-field) design below. The prior tank-onset attempt was REVERTED for sounding like "ER→silence→tap gap, a literal delay." Will THIS design avoid that? Check: does it produce a CONTINUOUS early field (no silence gap) AND a distinct ~100ms tap? Is the discrete tap fed/filtered so it reads as a reflection (not a slap echo)? CPU cost? bit-null when off? Per-preset tunable? Refute if it repeats the prior failure or can't deliver the −3..−6 dB tap. DESIGN: ${JSON.stringify(ef)}. Return a short verdict {viable:bool, risks:[...], fixes:[...]}.`,
    { label: 'verify:early-field', phase: 'Verify', schema: { type:'object', additionalProperties:false, required:['viable','risks','fixes'], properties:{ viable:{type:'boolean'}, risks:{type:'array',items:{type:'string'}}, fixes:{type:'array',items:{type:'string'}} } } }),
  () => agent(
    `Adversarially review FORK B (DenseHall tonal-correction decoupling) design below. Check: is it truly post-tank/non-recursive (no codegen bit-null risk)? Does flattening per-band steady-state energy actually fix the ss-hot + cent-dark WITHOUT killing the (just-fixed) HF sustain or the low body the user wanted? Could it over-flatten a preset already matched (the AccurateHall Phase-1 tonal-correction HURT Ambience +10 when flat — see memory)? Should it be opt-in per preset? Refute if it risks regression. DESIGN: ${JSON.stringify(dc)}. Return {viable:bool, risks:[...], fixes:[...]}.`,
    { label: 'verify:decoupling', phase: 'Verify', schema: { type:'object', additionalProperties:false, required:['viable','risks','fixes'], properties:{ viable:{type:'boolean'}, risks:{type:'array',items:{type:'string'}}, fixes:{type:'array',items:{type:'string'}} } } }),
])

phase('Synthesize')
const spec = await agent(
  `You are the DSP architect. Write a CONCRETE, implementable spec (markdown) for the two DenseHall hall engine forks, grounded in the findings + verified against the prior failures. This will be handed to the implementer (who edits the C++, builds, renders, measures vs the trusted anchors, ear-validates).

FORK A early-field: ${JSON.stringify(ef)}
FORK B decoupling: ${JSON.stringify(dc)}
INTEGRATION: ${JSON.stringify(integ)}
VERIFY verdicts: ${JSON.stringify(verdicts)}

The spec MUST give, for EACH fork:
1. The exact DSP (signal flow, filters, params, formulae) + where it hooks in (file:line).
2. The per-preset param mechanism (kXxxByName map + env override) + bit-null gating (default-off = byte-identical fleet, verified on a non-hall preset).
3. The codegen-bit-null safety argument (post-tank/non-recursive or out-of-line).
4. Make-or-break test + pass/fail thresholds (early-field: the snare envelope shows the ~100ms second arrival at −3..−6 dB + onset_slope/energy_t50 toward anchor, no silence gap, ear; decoupling: Large Chamber ss-hot + cent close WITHOUT losing HF sustain/low body).
5. The implementation ORDER (which fork first, why) + the per-preset values to start (the 5 halls).
Be concrete and honest about CPU + the ear-axes. Return ONLY the markdown.`,
  { label: 'synthesize-spec', phase: 'Synthesize', effort: 'high' })

return spec
