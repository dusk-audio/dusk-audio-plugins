export const meta = {
  name: 'dattorro-room-diffusion-design',
  description: 'Design a fix for the structural "boing" comb resonance in the short-room Dattorro tank (Live Room 211Hz / Medium 333 / Small 963): denser/decorrelated modes that kill the resonance without changing decay/level/character. Weigh Dattorro-redesign vs migrate-to-denser-engine.',
  phases: [
    { title: 'Investigate', detail: 'parallel: boing root-cause in DattorroTank delays, denser-engine option (FDN/velvet), the 3 rooms tuning state' },
    { title: 'Verify', detail: 'skeptic refutes the chosen fix vs decay/level/character preservation + the 3-preset shared cost' },
    { title: 'Synthesize', detail: 'one architect: concrete C++ spec + per-preset migration/tuning plan + pass thresholds' },
  ],
}

const DSP  = '/home/marc/projects/plugins/plugins/DuskVerb/src/dsp'
const PROC = '/home/marc/projects/plugins/plugins/DuskVerb/src/PluginProcessor.cpp'

const MEASURED = `
The 3 short-room presets on algo 0 (DattorroTank) FAIL full_check tail_resonance
("boing") with a strong narrow LOW-mid resonance: Live Room +20dB@211Hz, Medium
Drum +14@333, Small Drum +10@963 (DV peak prominence dB over median, vs anchor ~14
broad). CONFIRMED 2026-06-17 it is a STRUCTURAL comb resonance, not tunable:
- postTankEQ NOTCH fails — crushing 211 hops the peak to 246→316→839 (mode cluster),
  and deeper/wider notches regress the level/decay gates (Live 27→29, Small 23→24,
  Medium 23→36).
- density-jitter (DUSKVERB_DENSJIT/DENS, the 3→6 AP cascade) dents 39→35dB but
  regresses everything (27→36).
- delay MODULATION (Mod Depth 0.6-0.9) barely moves it (39→37.5) and regresses (27→31).
So the 2-delay Dattorro topology, at SHORT room-scale delays + high feedback (for the
~0.4-0.6s decay), produces SPARSE strong comb peaks. The anchor rooms have a SMOOTH
broad tail (dense modes). DattorroTank already has: modulated AP → delay → 3-AP density
cascade → 2-band damping → static AP → delay (per loop, ~8× density vs basic Dattorro),
setDensityDepth (6 APs), setDensityJitter, setModReduction, setDelayScale, setSize.
GOAL: dense/decorrelated short-room modes (no comb resonance, boing within +3dB of
anchor) WITHOUT changing the per-band T60 / level / front-load character (those gates
are already ~OK on these presets after JND calibration). algo 0 is shared by MANY
presets — a DattorroTank change is NOT bit-null; weigh that vs a per-preset path.
`

const FIND = {
  type: 'object', additionalProperties: false,
  required: ['summary', 'numbers', 'key_code', 'pitfalls'],
  properties: {
    summary: { type: 'string' },
    numbers: { type: 'array', items: { type: 'string' } },
    key_code: { type: 'array', items: { type: 'string' } },
    pitfalls: { type: 'array', items: { type: 'string' } },
  },
}

phase('Investigate')
const reads = await parallel([
  () => agent(
    `Find the ROOT of the boing comb resonance in DattorroTank. READ ${DSP}/DattorroTank.h + .cpp fully: the delay-line lengths (base delays, setDelayScale/setSize/setSizeRange), the feedback/decay gain path (setDecayTime → loop gain), the allpass diffusers + density cascade, the damping. Compute WHY a short-room config (Live Room ~0.5s decay) yields a +39dB narrow peak at ~211Hz: which delay length's comb fundamental/harmonic lands there, how high the round-trip gain is at that freq, why the diffusers don't smooth it. Then design the IN-DATTORRO fix that adds dense/decorrelated modes at short scale WITHOUT changing decay/level: options — INCOMMENSURATE / prime-ratio delay lengths (decorrelate comb peaks), MORE delay lines, more/longer nested allpasses, a short parallel diffuser. Which kills a +20dB comb peak? Context: ${MEASURED}. Return structured (key_code = the exact delay/feedback code + the change sites; numbers = the delay lengths + the resonant-freq math).`,
    { label: 'root:dattorro-combs', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Evaluate MIGRATING the 3 short rooms OFF the 2-delay Dattorro to a DENSER-mode engine that has NO sparse comb resonance. READ ${DSP}/DuskVerbEngine.h (engine list) + the candidates: FDNReverbT (16-line FDN, algo 10 AccurateHall — 16 incommensurate lines = far denser modes than 2-delay Dattorro) and ${DSP}/VelvetTail.h (the new feed-forward velvet tail — dense by construction, no recirculation = NO comb resonance at all, per-band T60 control). For EACH: would a short ROOM (0.4-0.6s, needs an EARLY/dense field + a smooth short tail) sound right on it, can it hit the rooms' per-band T60 + front-load, and what's the per-preset migration cost (re-tune)? A room needs early reflections + a dense tail — does velvet (pure tail, no ER structure) or a 16-line FDN serve a ROOM better? Context: ${MEASURED}. Return structured (recommendation in summary).`,
    { label: 'option:denser-engine', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Report the current tuning state of the 3 rooms so the fix doesn't regress what's working. READ ${PROC} for Live Room / Small Drum Room / Medium Drum Room: their algorithm, baked params, any kDattorroOctaveT60ByName / kPostTankEQByName / density / topology entries. From the diagnosis (boing aside) their OTHER fails after JND calibration: Live Room 27 (boing, cent dark -40%, T60 mids -20-28%, width low way off -0.847 vs +0.027, onset +335%); Small Drum 23 (boing, onset +778%, ss air +12 too bright, T60 spread); Medium Drum 23 (boing, decay/edt all SHORT -23 to -61%, energy too front-loaded +33pp, T60 mids short). Which of these would a denser tank HELP (the boing) vs leave (cent/onset/width)? Is the boing-fix enough to get any of the 3 to <=19, or do they need more? Context: ${MEASURED}. Return structured.`,
    { label: 'state:3-rooms', phase: 'Investigate', schema: FIND }),
])

phase('Verify')
const [root, denser, state] = reads
const verdict = await agent(
  `Adversarially decide the boing fix: IN-DATTORRO densify (incommensurate delays / more lines / more diffusion) vs MIGRATE the 3 rooms to a denser engine (16-line FDN or VelvetTail). ROOT: ${JSON.stringify(root)}. DENSER-ENGINE: ${JSON.stringify(denser)}. ROOM STATE: ${JSON.stringify(state)}.
  Refute each: (1) IN-DATTORRO — does decorrelating/adding delays actually kill a +20dB comb WITHOUT lengthening the room or changing the per-band T60/level (the gates already ~OK)? And algo 0 is shared by MANY presets — how is the change gated so the OTHER Dattorro presets stay bit-null (a per-preset flag / a short-room-only branch)? (2) MIGRATE — does a room (needs early reflections + a dense short tail) actually sound right on velvet (pure tail) or a 16-line FDN, and is the re-tune cost worth it for 3 presets? (3) Will the boing-fix alone get them ≤19, or are cent/onset/width the real blockers (making the boing-fork low-ROI)? Pick ONE path. Return {path:string, viable:bool, risks:[...], bit_null_gating:string, gets_to_19:string}.`,
  { label: 'verify:path', phase: 'Verify', effort: 'high',
    schema: { type:'object', additionalProperties:false, required:['path','viable','risks','bit_null_gating','gets_to_19'],
      properties:{ path:{type:'string'}, viable:{type:'boolean'}, risks:{type:'array',items:{type:'string'}}, bit_null_gating:{type:'string'}, gets_to_19:{type:'string'} } } })

phase('Synthesize')
const out = await agent(
  `DSP architect: write a CONCRETE C++ spec (markdown) for the chosen boing fix, handed to the implementer (edits the engine, builds DuskVerb_VST3, renders the 3 rooms --program, gain-matches, full_check, hand-tunes). ROOT: ${JSON.stringify(root)}. DENSER: ${JSON.stringify(denser)}. STATE: ${JSON.stringify(state)}. VERDICT: ${JSON.stringify(verdict)}.
  MUST specify: (1) the exact DSP change + file:line hook sites; (2) how it kills the +20dB comb without changing per-band T60/level/decay; (3) the bit-null gating so the OTHER algo-0 presets stay byte-identical (per-preset flag or short-room branch + an env override for tuning); (4) per-preset values/migration for Live Room, Small Drum, Medium Drum; (5) make-or-break tests + thresholds (boing ≤+3dB, no regression on T60/decay/level/onset, fleet bit-null on the non-room Dattorro presets) + honest per-room n_fail target (does the boing-fix get them ≤19, or what else is needed). Be concrete. Return ONLY the markdown.`,
  { label: 'synthesize-spec', phase: 'Synthesize', effort: 'high' })

return out
