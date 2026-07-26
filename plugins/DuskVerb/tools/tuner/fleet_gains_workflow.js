export const meta = {
  name: 'fleet-gains-audit',
  description: 'Diagnose every DuskVerb preset: recoverable gains vs structural walls, with concrete levers + adversarial verification',
  phases: [
    { title: 'Diagnose', detail: 'one agent per preset: classify each gate-fail cluster, propose a lever' },
    { title: 'Verify', detail: 'skeptic refutes each recoverable claim against known walls + prior attempts' },
    { title: 'Synthesize', detail: 'rank fleet by verified recoverable gain + fleet-wide patterns' },
  ],
}

const MEM = '/home/marc/.claude/projects/-home-marc-projects-plugins/memory'
const ROOT = '/home/marc/projects/plugins'
let A
if (typeof args === 'string') {
  try {
    A = JSON.parse(args)
  } catch (e) {
    throw new Error(`fleet-gains-audit: args is not valid JSON (${e.message}); got: ${args.slice(0, 200)}`)
  }
} else {
  A = args
}
const PRESETS = (A && A.presets) ? A.presets : A   // [{name, slug, algo, n_fail}]
if (!Array.isArray(PRESETS)) throw new Error('args.presets missing; got: ' + JSON.stringify(args).slice(0, 200))

const DIAG_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['name', 'algo', 'n_fail', 'clusters'],
  properties: {
    name: { type: 'string' },
    algo: { type: 'integer' },
    n_fail: { type: 'integer' },
    clusters: {
      type: 'array',
      description: 'Each cluster groups gate-fails sharing ONE root cause.',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['gates', 'root_cause', 'classification', 'lever', 'est_gain_fails', 'confidence'],
        properties: {
          gates: { type: 'array', items: { type: 'string' }, description: 'gate names in this cluster, e.g. ["T60 16k","T60 8k"]' },
          root_cause: { type: 'string', description: 'one-line mechanism' },
          classification: { type: 'string', enum: ['RECOVERABLE', 'STRUCTURAL', 'HARNESS_ARTIFACT'] },
          lever: { type: 'string', description: 'concrete fix: exact param/field/code change. "" if structural/artifact.' },
          est_gain_fails: { type: 'integer', description: 'estimated # of fails this lever closes' },
          confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
        },
      },
    },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['gates', 'still_recoverable', 'reason', 'already_tried'],
  properties: {
    gates: { type: 'array', items: { type: 'string' } },
    still_recoverable: { type: 'boolean', description: 'true ONLY if the lever survives skeptical review' },
    already_tried: { type: 'boolean', description: 'true if memory/comments show this lever was tried and walled' },
    reason: { type: 'string' },
  },
}

const SYNTH_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['ranked_gains', 'fleet_patterns', 'structural_walls', 'headline'],
  properties: {
    headline: { type: 'string', description: '2-3 sentence bottom line for the user' },
    ranked_gains: {
      type: 'array',
      description: 'verified recoverable gains, highest total fails-closed first',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['preset', 'lever', 'est_gain_fails', 'effort', 'confidence'],
        properties: {
          preset: { type: 'string' },
          lever: { type: 'string' },
          est_gain_fails: { type: 'integer' },
          effort: { type: 'string', enum: ['trivial', 'preset-tune', 'engine-change', 'engine-fork'] },
          confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
        },
      },
    },
    fleet_patterns: {
      type: 'array',
      description: 'defects spanning many presets with one shared fix (e.g. boing, impulse_rms)',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['pattern', 'presets_affected', 'shared_fix', 'est_total_gain'],
        properties: {
          pattern: { type: 'string' },
          presets_affected: { type: 'array', items: { type: 'string' } },
          shared_fix: { type: 'string' },
          est_total_gain: { type: 'integer' },
        },
      },
    },
    structural_walls: {
      type: 'array',
      description: 'confirmed ceilings — do NOT re-tune; need an engine fork',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['preset', 'wall', 'fork_needed'],
        properties: {
          preset: { type: 'string' },
          wall: { type: 'string' },
          fork_needed: { type: 'string' },
        },
      },
    },
  },
}

// POSIX single-quote shell escape: wrap in single quotes, close/escape/reopen any
// embedded quote. Keeps preset names/slugs with spaces or metacharacters from
// malforming the commands the diagnosing agent runs. Pass a value already wrapped
// in double quotes (e.g. '"'+name+'"') when a literal `"Name"` grep target is needed.
const sh = (s) => `'` + String(s).replace(/'/g, `'\\''`) + `'`

function diagnosePrompt(p) {
  return `You are diagnosing DuskVerb factory preset "${p.name}" (engine algo ${p.algo}, currently ${p.n_fail} full_check gate-fails) for RECOVERABLE gains vs STRUCTURAL walls. The Valhalla/Lexicon anchors are TRUSTED (re-rendered + ear-confirmed 2026-06-16).

STEP 1 — get the per-gate breakdown (renders are already cached, do NOT re-render):
  python3 ${ROOT}/plugins/DuskVerb/tools/tuner/full_check.py /tmp/audit_${sh(p.slug)} /tmp/audit_${sh(p.slug)}_a --name ${sh(p.name)}
This prints every gate DV-vs-anchor with Δ and pass/fail.

STEP 2 — read the preset's source row + its engine:
  - FactoryPresets row: grep -n ${sh('"' + p.name + '"')} ${ROOT}/plugins/DuskVerb/src/FactoryPresets.h  (read the row + its comments — prior tuning notes live there)
  - The engine for algo ${p.algo}: find it in ${ROOT}/plugins/DuskVerb/src/dsp/ (AlgorithmConfig.h maps algo->engine). Read how the relevant params drive the failing gates.

STEP 3 — check what was already TRIED + which gates are KNOWN walls:
  grep -rl . ${MEM}/  then read the relevant DuskVerb memory files (especially: duskverb_fdn_t60_coupling_wall, duskverb_quadtank_coupling_wall, duskverb_shimmer_12k_aa_ceiling, duskverb_mdr_dattorro_campaign, duskverb_energy_arrival_gate_and_wall, duskverb_vocal_plate_fdn_floor, duskverb_densehall_metallic_comb_fix, duskverb_accurate_hall_engine, duskverb_ear_vs_gates_spectral_shape, feedback_no_premature_ceilings, feedback_diagnose_dont_count_gates). Also read ${MEM}/MEMORY.md index.

Then CLUSTER the failing gates by shared root cause. For each cluster decide:
  - RECOVERABLE: a concrete untried lever (a FactoryPresets field, a calibration table, a per-preset EQ) plausibly closes it WITHOUT regressing others. Give the EXACT change.
  - STRUCTURAL: a known engine wall (coupling, HF-loss, modal sparsity, early-field) already proven unmovable by preset params. Cite the memory/comment.
  - HARNESS_ARTIFACT: a known full_check measurement bug (e.g. peak-align on slow-onset pads → tail_t30/t60/edt anchor reads ~0.01-0.06s; the "boing" octave confusion). Not a real defect.

Be brutally honest and specific. Do NOT label something recoverable to be optimistic, nor structural to avoid work — both mislead. est_gain_fails must be realistic. Return ONLY the structured object.`
}

function verifyPrompt(p, cluster) {
  return `Adversarial review. For DuskVerb preset "${p.name}" (algo ${p.algo}), a diagnosis claims this gate cluster is RECOVERABLE:
  gates: ${JSON.stringify(cluster.gates)}
  root_cause: ${cluster.root_cause}
  proposed lever: ${cluster.lever}
  est gain: ${cluster.est_gain_fails} fails

Your job is to REFUTE it. Default to still_recoverable=false unless the lever clearly survives. Check:
  1. Has this lever ALREADY been tried and walled? grep the memory dir ${MEM}/ AND the FactoryPresets row comments (grep -n ${sh('"' + p.name + '"')} ${ROOT}/plugins/DuskVerb/src/FactoryPresets.h, read its trailing comments) for evidence the param was swept / pinned / reverted. Set already_tried=true if so.
  2. Does the lever fight a documented coupling (e.g. FDN gain==decay==level: shortening one band darkens it; raising HF T60 needs net loop gain the GEQ can't give)? If the fix trades one gate for another, it's NOT a real gain.
  3. Is the engine for algo ${p.algo} actually capable of the change, or does the param saturate?
Only confirm still_recoverable=true when the lever is genuinely untried AND mechanically sound AND non-regressing. Return ONLY the structured verdict.`
}

phase('Diagnose')
const diagnosed = await pipeline(
  PRESETS,
  p => agent(diagnosePrompt(p), { label: `diag:${p.name}`, phase: 'Diagnose', schema: DIAG_SCHEMA }),
  (d, p) => {
    const claims = (d.clusters || []).filter(c => c.classification === 'RECOVERABLE')
    if (!claims.length) return { ...d, verified_claims: [] }
    return parallel(claims.map(c => () =>
      agent(verifyPrompt(p, c), { label: `verify:${p.name}`, phase: 'Verify', schema: VERDICT_SCHEMA })
        .then(v => ({ ...c, verdict: v }))
        .catch(e => {
          const msg = (e && e.message) ? e.message : String(e)
          log(`verify failed for "${p.name}" [${(c.gates || []).join(', ')}]: ${msg}`)
          return { ...c, verdict: null, verify_error: msg }
        })
    )).then(verified => ({ ...d, verified_claims: verified.filter(Boolean) }))
  }
)

phase('Synthesize')
const clean = diagnosed.filter(Boolean)
const synthPrompt = `You are synthesizing a fleet-wide GAINS report for DuskVerb (18 presets) for the user's master listen tonight. Below is per-preset diagnosis with adversarially-verified recoverable claims. A claim is a REAL gain only if its verdict.still_recoverable === true.

DATA:
${JSON.stringify(clean, null, 1)}

Produce:
  - ranked_gains: every claim with verdict.still_recoverable===true, highest est_gain_fails first. Tag effort honestly (trivial = one field/level trim; preset-tune = a few fields + re-measure; engine-change = code in one engine; engine-fork = new DSP structure). Drop anything verify rejected.
  - fleet_patterns: defects spanning MANY presets with ONE shared fix (the scoreboard flagged boing on 8/18 = modal sparsity, and impulse_rms transient-loudness on ~10). Aggregate them; est_total_gain across the affected presets.
  - structural_walls: confirmed ceilings (shimmer HF-sustain, FDN coupling, etc.) — name the fork needed.
  - headline: the honest bottom line — where the real fleet gains are and roughly how many fails are recoverable vs walled.
Return ONLY the structured object.`

const report = await agent(synthPrompt, { label: 'synthesize', schema: SYNTH_SCHEMA, effort: 'high' })
return report
