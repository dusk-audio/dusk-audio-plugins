export const meta = {
  name: 'hf-lossless-loop-scope',
  description: 'Scope the HF-lossless loop-interpolation fork (unwalls 16k on DenseHall + Shimmer): investigate current loss, survey fixes, synthesize a concrete plan',
  phases: [
    { title: 'Investigate', detail: 'parallel readers: DenseHall loss, FDN/Shimmer loss, interp primitives, fix-options survey' },
    { title: 'Synthesize', detail: 'one architect: grounded, implementable scope doc + make-or-break + phased plan' },
  ],
}

const ROOT = '/home/marc/projects/plugins'
const DSP = `${ROOT}/plugins/DuskVerb/src/dsp`
const MEM = '/home/marc/.claude/projects/-home-marc-projects-plugins/memory'

const FIND_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['area', 'loss_points', 'key_code', 'notes'],
  properties: {
    area: { type: 'string' },
    loss_points: {
      type: 'array', description: 'each place HF is lost per pass through the loop',
      items: {
        type: 'object', additionalProperties: false,
        required: ['file_line', 'mechanism', 'severity'],
        properties: {
          file_line: { type: 'string', description: 'file:line' },
          mechanism: { type: 'string', description: 'WHY HF is lost here (linear interp, fixed LP, allpass coeff, etc.)' },
          severity: { type: 'string', enum: ['dominant', 'moderate', 'minor'] },
        },
      },
    },
    key_code: { type: 'string', description: 'the exact current read/interp code (verbatim snippet) that would change' },
    notes: { type: 'string', description: 'modulation interaction, per-line vs shared, stability/bit-null observations' },
  },
}

const OPTIONS_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['options', 'recommendation'],
  properties: {
    options: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        required: ['name', 'hf_flatness', 'modulation_safe', 'stability_in_loop', 'cpu_cost', 'transient_phase', 'verdict'],
        properties: {
          name: { type: 'string', description: 'e.g. linear (current), 3rd-order Lagrange, Thiran allpass fractional delay, cubic Hermite, 5th-order Lagrange' },
          hf_flatness: { type: 'string', description: 'magnitude response near nyquist vs the fractional position' },
          modulation_safe: { type: 'string', description: 'behaviour when the fractional delay MOVES (the loop is modulated) — zipper/transient/allpass-coeff-jump' },
          stability_in_loop: { type: 'string', description: 'recursive-feedback stability (allpass transient energy, gain>1 risk)' },
          cpu_cost: { type: 'string', description: 'relative ops/sample vs linear' },
          transient_phase: { type: 'string', description: 'pre-ring / phase distortion / transient smearing' },
          verdict: { type: 'string', enum: ['recommended', 'viable', 'rejected'] },
        },
      },
    },
    recommendation: { type: 'string', description: 'which option for THIS use (modulated recursive reverb loop) + why' },
  },
}

phase('Investigate')
const reads = await parallel([
  () => agent(
    `Investigate where the DenseHall reverb loses HF energy PER PASS through its feedback loop (this is why T60-16k caps at ~0.7s even when commanded 20s). Read ${DSP}/DenseHallReverb.h END-TO-END: the Line delay read + getlast(mod) fractional read, the ModAP modulated allpasses (input + 3 nested loopAP per line), the SpinComb, the modulated-delay interpolation, and the per-line damping. Identify EVERY HF-loss point in the recirculating path (fractional-delay interpolation = a position-dependent lowpass is the prime suspect; also any fixed filter, allpass coeff, DC block). Quote the exact read/interp code. Note modulation interaction (the delay position MOVES). Return structured findings.`,
    { label: 'find:densehall', phase: 'Investigate', schema: FIND_SCHEMA }),
  () => agent(
    `Investigate where the Shimmer engine's FDN hall + feedback loop loses HF per pass (T60-16k 4.3s vs anchor 9.6-11.9s; the +24 voice generates 24kHz but it dies). Read ${DSP}/ShimmerEngine.cpp (GranularPitchShifter::readLinear + process, the feedback delay-line read, the fbLpf) AND ${DSP}/FDNReverb.cpp / FDNReverb.h delay-line reads + per-line damping interpolation. Identify EVERY HF-loss point in the recirculating path. Quote the exact read/interp code (readLinear especially). Note that the feedback LPF + shifter AA are KNOWN limiters — focus on the DELAY-LINE interpolation loss that the octave GEQ couldn't compensate. Return structured findings.`,
    { label: 'find:shimmer-fdn', phase: 'Investigate', schema: FIND_SCHEMA }),
  () => agent(
    `Catalogue the shared fractional-delay / interpolation PRIMITIVES available in this codebase. Read ${DSP}/DspUtils.h fully (interpolation helpers, RandomWalkLFO, any delay-line/allpass templates, nextPowerOf2) + grep for "readLinear", "interp", "getlast", "frac", "lerp", "Lagrange", "allpass", "Thiran" across ${DSP}/. Report what interpolation each engine's delay read currently uses, whether there's any higher-order interpolator already present, and where a shared HF-lossless fractional-delay helper would live. Return structured findings (loss_points = current interpolators in use; key_code = the canonical read).`,
    { label: 'find:primitives', phase: 'Investigate', schema: FIND_SCHEMA }),
  () => agent(
    `Survey fractional-delay interpolation methods to REPLACE linear interpolation in a MODULATED, RECURSIVE reverb feedback loop, where the goal is HF-LOSSLESS readout (flat magnitude near nyquist regardless of fractional position) so a 16kHz tail can sustain. Compare: linear (current baseline), 3rd-order Lagrange, 5th-order Lagrange, cubic Hermite/Catmull-Rom, Thiran 1st/2nd-order allpass fractional delay. For EACH rate: HF magnitude flatness vs fractional position; behaviour when the delay MOVES per-sample (the loops are LFO-modulated — allpass fractional delays have a transient/coeff-jump problem; Lagrange is FIR so modulation-safe); recursive-loop stability (allpass adds transient energy, Lagrange can't exceed unity); CPU ops/sample; transient/phase (pre-ring). Recommend the best for a modulated recursive reverb loop. Also note the codegen/bit-null constraint: per the memory ${MEM}/duskverb_bitnull_codegen_limit.md, adding code to the recursive hot loop perturbs FP codegen → must isolate (separate TU / compile-time template switch / gated). Return structured comparison.`,
    { label: 'survey:interp-options', phase: 'Investigate', schema: OPTIONS_SCHEMA }),
])

const [densehall, shimmer, primitives, options] = reads

phase('Synthesize')
const scope = await agent(
  `You are the DSP architect. Write a COMPLETE, implementable scope document (GitHub markdown) for the "HF-lossless loop interpolation" fork for DuskVerb. Goal: unwall T60-16k on BOTH the DenseHall presets (Vocal Hall/Cathedral/Bright Hall/Blade Runner/Large Chamber) AND the Shimmer (Black Hole/Deep Blue Day) — both share an HF-lossy recirculating loop (linear fractional-delay interpolation = a position-dependent lowpass) that the attenuation-only per-octave GEQ cannot compensate. Once 16k can sustain, the already-built (dormant) DenseHall octave GEQ + the Shimmer brightening become the shaping tools.

GROUND IT in these investigation findings (do not invent code — cite the real file:line):
DENSEHALL LOSS: ${JSON.stringify(densehall)}
SHIMMER/FDN LOSS: ${JSON.stringify(shimmer)}
PRIMITIVES: ${JSON.stringify(primitives)}
INTERP OPTIONS: ${JSON.stringify(options)}

The doc MUST contain:
1. **Root cause** — quantify the per-pass HF loss of linear interpolation (cite the read code) and why it caps 16k (compounds over ~N passes in a long tail). Why the octave GEQ can't fix it (attenuation-only, gain≤0.9999).
2. **The fix** — the chosen interpolator (from the survey's recommendation), exactly WHERE it replaces linear (every cited read site, both engines + shared primitive), and the new shared helper's signature/location.
3. **Bit-null / codegen strategy** — how to add it without drifting the fleet (the recursive-loop codegen-drift constraint). Compile-time template switch vs runtime gate vs separate TU. Which presets opt in. How to PROVE bit-null for non-opted presets.
4. **Make-or-break test (FIRST, before mass rollout)** — on Cathedral (DenseHall) + Black Hole (Shimmer): does T60-16k rise toward the anchor (Cathedral 1.63s, Black Hole 9.6s) WITHOUT the metallic ring returning (gate proxy = diffusion_flux/tail kurtosis stayed clean at 0.16 in fork #2) AND within a sane CPU budget? Define pass/fail thresholds.
5. **Risks** — metallic ring (ear axis — user listens), CPU (per-line, per-sample, ×8-16 lines ×N allpasses), transient smearing/pre-ring, stability under modulation, the shifter AA still capping the shimmer top octave (12kHz) independently of this fix.
6. **Phased plan** — make-or-break → shared helper → DenseHall rollout + recalibrate octave GEQ → Shimmer rollout → per-preset re-tune. Each phase with a measurable gate.
7. **Interaction** — how this unlocks the dormant DenseHall octave GEQ (calibrated curves already saved) + the Shimmer brightening (staged for A/B).

Be concrete and honest about effort and the CPU risk. Return ONLY the markdown document text.`,
  { label: 'synthesize-scope', phase: 'Synthesize', effort: 'high' })

return scope
