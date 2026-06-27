export const meta = {
  name: 'reverse-tail-engine-design',
  description: 'Design a dedicated SPARSE/SHORT gated-reverse TAIL for ReverseRoomEngine (replace the FDN) that hits per-band T60 0.15-0.47s with no mid-decay floor, bright HF, anti-correlated stereo, smooth density — keeping the working swell-ER + gate',
  phases: [
    { title: 'Investigate', detail: 'parallel: anchor tail spec, reusable infra (SparseField/FDN/diffusers), tail-architecture options' },
    { title: 'Verify', detail: 'skeptic refutes the chosen tail vs the FDN mid-floor / metallic-when-short / stereo / CPU' },
    { title: 'Synthesize', detail: 'one architect: concrete C++ spec for the new tail + per-band params + pass thresholds' },
  ],
}

const DSP    = '/home/marc/projects/plugins/plugins/DuskVerb/src/dsp'
const ANCHOR = '/home/marc/projects/dusk-audio-tools/anchors/rendered/lex-reverse-1'
const FCNOW  = '/tmp/fc_now.txt'

const MEASURED = `
ReverseRoomEngine already has a WORKING swell-ER FIR (rising taps → the ~344ms
"reverse" onset) + a WORKING input-keyed gate (envelope-follow dry input →
open/hold(duration-dependent)/hard-release; env_p2p +63dB, tail_t60 0.12s). KEEP
BOTH. What must be REPLACED is the TAIL: currently FDNReverbT<true> (per-octave
GEQ). The FDN tail FAILS because:
- MID-DECAY FLOOR: the FDN can't decay faster than ~0.3-0.45s in the mids even
  with the GEQ commanding 0.02s (delay-line length / modal density floor). The
  anchor wants per-band T60: 63:0.17 125:0.47 250:0.19 500:0.25 1k:0.21 2k:0.18
  4k:0.27 8k:0.19 16k:0.19 (s) and decay-band t30: low100-250:0.48 lowmid250-500:0.21
  mid500-2k:0.47 hi2-8k:0.48. Note the SHAPE: 125Hz long (0.47), mids SHORT (0.18-0.25),
  highs short (0.19). The FDN floors the mids too long.
- TOO DARK: cent_50 DV 4754 vs anchor 7725 Hz; ss air -5dB, ss hi -4dB low. The
  broadband treble multiplier can't add an HF tilt (bloom 2-4k already +3.5 hot).
- WRONG STEREO: stereo_corr DV +0.39 vs anchor -0.139 (anchor is ANTI-correlated /
  very wide). The FDN tail is too correlated.
- TOO LOUD low/mid: low 100-250 +9.5dB, mids +4dB hot.
algo 9 (ReverseRoom) is used by EXACTLY ONE preset → any tail change is bit-null
for the other 17 by construction. n_fail floors ~33-36 on the FDN tail; target ≤19.
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
    `Quantify the EXACT tail requirements for a gated-reverse tail from the lex-reverse-1 anchor. READ the WAVs in ${ANCHOR} (sustained, impulse, noiseburst) with python (soundfile/numpy/scipy via Bash). Measure: (1) per-octave T60 (the engine must hit 0.15-0.47s with the SHAPE: 125Hz long, mids/highs short — confirm/refine my numbers); (2) spectral tilt — the brightness (centroid 7725Hz, the HF/air emphasis vs the 2-4k that must NOT be hot); (3) stereo decorrelation — how anti-correlated (-0.139) + is it broadband or band-dependent; (4) echo/modal DENSITY — is the tail dense (smooth) or audibly sparse/grainy (compute echo density / how many reflections/sec); (5) the per-band LEVEL (the steady-state spectrum). Context: ${MEASURED}. Return structured (numbers = concrete per-band decay s / tilt dB / corr / density values the tail must produce).`,
    { label: 'anchor:tail-spec', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Survey the REUSABLE DSP infra for building a SHORT/BRIGHT/WIDE/controllable reverb tail. READ ${DSP}/SparseEarlyField.h (+ .cpp if present — velvet-noise sparse field: can it be a TAIL with per-band decay + tunable T60 down to 0.15s + L/R decorrelation?), ${DSP}/FDNReverb.h (delay-line lengths — how short can they go via size? why do they floor mid-decay ~0.3s + go resonant when very short?), ${DSP}/EarlyReflections.h + the diffusers (${DSP}/*iffus* or allpass cascades in DenseHallReverb.h), and ${DSP}/ReverseRoomEngine.h/.cpp (what the swell-ER + gate provide; the process() signal flow erBuf → tail). Report which building blocks give: per-band T60 0.15-0.47s with NO mid floor, controllable brightness, L/R decorrelation, smooth density, cheap CPU. Context: ${MEASURED}. Return structured (key_code = the reusable classes/methods + their tunable params).`,
    { label: 'infra:reusable', phase: 'Investigate', schema: FIND }),
  () => agent(
    `Design + compare TAIL ARCHITECTURES for a sparse/short gated reverse (to replace the FDN in ReverseRoomEngine, fed by the swell-ER, cut by the gate). The tail must: hit per-band T60 0.15-0.47s (SHORT mids, no FDN-style ~0.3s floor), be BRIGHT (centroid ~7.7kHz, HF tilt), ANTI-correlated stereo (-0.139, very wide), DENSE enough to sound smooth, cheap CPU. Weigh at least: (A) velvet-noise sparse tail (decaying sparse taps, per-band-filtered, independent L/R); (B) a short-delay FDN purpose-built (much shorter lines than the standard FDN so mids decay fast, + per-octave GEQ + output high-shelf + a decorrelating allpass on one channel); (C) multitap exponential-decay delay with per-band envelopes; (D) parallel multiband short feedback combs. For EACH: can it hit the per-band T60 SHAPE with no mid floor, the brightness, the anti-correlation, the density, without metallic ringing? CPU? Pick ONE and justify. Context: ${MEASURED}. Return structured (approach in summary, the concrete signal flow + params in numbers/key_code, traps in pitfalls).`,
    { label: 'design:tail-arch', phase: 'Investigate', schema: FIND }),
])

phase('Verify')
const [spec, infra, arch] = reads
const verdict = await agent(
  `Adversarially review the chosen TAIL ARCHITECTURE for the gated-reverse fix before it's built (slow Wine iteration → get it right now). CHOSEN DESIGN: ${JSON.stringify(arch)}. ANCHOR SPEC: ${JSON.stringify(spec)}. REUSABLE INFRA: ${JSON.stringify(infra)}.
  Refute hard on: (1) Does it ACTUALLY beat the FDN mid-decay floor — can the mids genuinely decay in ~0.18-0.25s without the chosen structure ringing/resonating (the FDN went metallic at short Size)? (2) ANTI-correlation -0.139: how exactly does it decorrelate L/R to NEGATIVE correlation (not just 0)? (3) DENSITY: will it sound smooth or grainy/fluttery at the required short decay? (4) BRIGHTNESS: the HF tilt without making it thin/fizzy. (5) It's fed by the swell-ER and cut by the gate — does the tail interact cleanly with both (the gate keys off DRY input, not the tail)? (6) CPU + determinism (fixed-seed, bit-null: algo9=1 preset). (7) Is it actually SIMPLER/more-controllable than just shortening the FDN, or is it over-engineering? For each: viable y/n + concrete fix/param. Return {viable:bool, risks:[...], fixes:[...], decorrelation_method:string, density_note:string, recommended_if_simpler:string}.`,
  { label: 'verify:tail', phase: 'Verify', effort: 'high',
    schema: { type:'object', additionalProperties:false, required:['viable','risks','fixes','decorrelation_method','density_note','recommended_if_simpler'],
      properties:{ viable:{type:'boolean'}, risks:{type:'array',items:{type:'string'}}, fixes:{type:'array',items:{type:'string'}}, decorrelation_method:{type:'string'}, density_note:{type:'string'}, recommended_if_simpler:{type:'string'} } } })

phase('Synthesize')
const out = await agent(
  `DSP architect: write a CONCRETE, implementable C++ spec (markdown) for the new SPARSE/SHORT gated-reverse TAIL in ReverseRoomEngine, replacing the FDN, KEEPING the working swell-ER FIR + input-keyed gate. Handed to the implementer (edits ReverseRoomEngine.{h,cpp}, builds DuskVerb_VST3, renders --program "Reverse Taps", gain-matches to lex-reverse-1, full_check, hand-tunes).
  ANCHOR SPEC: ${JSON.stringify(spec)}
  INFRA: ${JSON.stringify(infra)}
  ARCH: ${JSON.stringify(arch)}
  VERIFY: ${JSON.stringify(verdict)}
  MUST specify: (1) the exact tail DSP (signal flow, structure, the math/coeffs), where it hooks into process() (erBuf → tail → gate), what members replace fdn_; (2) how it hits the per-band T60 SHAPE with no mid floor (the per-band decay mechanism + how to set 0.18-0.47 per octave); (3) the brightness (HF tilt filter + coeffs); (4) the L/R anti-correlation method (concrete); (5) density (taps/sec or diffusion to sound smooth); (6) the per-preset tuning params + an env override (DUSKVERB_* pattern) for hand-tuning without rebuilds; (7) fixed-seed determinism + bit-null (algo9=1 preset); (8) make-or-break tests + pass thresholds (per-band T60 within ±5%, cent_50 toward 7725, stereo_corr toward -0.139, decay bands, no metallic ring) + realistic n_fail target (≤19). Be concrete. Return ONLY the markdown.`,
  { label: 'synthesize-spec', phase: 'Synthesize', effort: 'high' })

return out
