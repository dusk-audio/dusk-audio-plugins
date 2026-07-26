export const meta = {
  name: 'duskverb-session-code-review',
  description: 'Adversarial multi-dimension code review of this session\'s uncommitted DuskVerb changes (VelvetTail, gate fork, Dattorro densify, Fork A/B, JND gates) → confirmed punchlist of real issues + fixes',
  phases: [
    { title: 'Review', detail: 'parallel dimensions: RT-safety, bit-null, bounds/lifetime, DSP-correctness, state-reset, cross-file/python' },
    { title: 'Verify', detail: 'adversarially confirm each finding is REAL + the fix is correct (reject false positives)' },
    { title: 'Synthesize', detail: 'one editor: dedup + rank the confirmed punchlist' },
  ],
}

const ROOT = '/home/marc/projects/plugins'
const DSP = `${ROOT}/plugins/DuskVerb/src/dsp`
const SRC = `${ROOT}/plugins/DuskVerb/src`

// the diff under review (agents run `git -C <root> diff` + read the new file).
const SCOPE = `Review ONLY this session's uncommitted changes in the git repo at ${ROOT}.
Get the diff: \`git -C ${ROOT} diff -- plugins/DuskVerb/src\` (tracked) + READ the NEW file
${DSP}/VelvetTail.h (untracked). Changed C++: ${DSP}/VelvetTail.h (NEW), ReverseRoomEngine.{h,cpp}
(input-keyed gate + duration-dependent hold + velvet tail swap), DattorroTank.{h,cpp}
(setDensityRoomFill + setMainLineDetune + density-AP allocation widened to max(room,hall)),
DuskVerbEngine.{h,cpp} (setReflectionTap "Fork A" + setDenseHallTonalCorrection "Fork B" +
Dattorro forwarders), DenseHallReverb.h (Fork B tonal-correction GEQ + cubicHermite reads),
${SRC}/PluginProcessor.cpp (kReflectionByName, kDenseHallTCByName, DensityConfig roomfill/det,
TuningEnv envs), ${SRC}/FactoryPresets.h (Reverse Taps row), ShimmerEngine.h, PluginEditor.cpp.
The .py + .js + .md changes are OUT OF SCOPE except full_check.py (review its JND-tolerance edits
for logic bugs only). The IGNORE list: do NOT report on the .js workflow files.`

const FINDING = {
  type: 'object', additionalProperties: false,
  required: ['findings'],
  properties: {
    findings: { type: 'array', items: {
      type: 'object', additionalProperties: false,
      required: ['file', 'line', 'severity', 'issue', 'fix'],
      properties: {
        file: { type: 'string' },
        line: { type: 'string', description: 'line number or symbol' },
        severity: { type: 'string', enum: ['critical','major','minor'] },
        issue: { type: 'string' },
        fix: { type: 'string', description: 'concrete fix' },
      },
    } },
  },
}

phase('Review')
const dims = [
  { key: 'rt-safety', p: `AUDIO-THREAD REAL-TIME SAFETY. In every process()/processBlock path of the changed DSP: any heap alloc (new/resize/assign/push_back), lock, std::getenv, file/DBG I/O, or message-thread call? VelvetTail::process, ReverseRoomEngine::process (the gate loop + velvet call), DattorroTank updateDelayLengths/setters. Vectors must be sized in prepare(), never process(). getenv only on init thread. Flag any RT violation. Also: denormals (ScopedNoDenormals coverage / flush) on the new feed-forward velvet + gate.` },
  { key: 'bit-null', p: `BIT-NULL / DEFAULT-OFF correctness. Each new feature claims byte-identical-when-unused: VelvetTail (only in ReverseRoom=algo9, 1 preset); the reflection tap (reflGain 0 → reflActive_ false → skipped); Fork B tonal-correction (tonalCorrEnabled_ false); Dattorro setDensityRoomFill(false)+setMainLineDetune identity {1,1,1,1} (×1.0f exact). VERIFY: are the default values actually identity? Does setMainLineDetune multiply by exactly 1.0f (no float drift)? Does the density-AP allocation change only ENLARGE buffers (not move read/write indices)? CRITICAL also: name-keyed maps (kReflectionByName, kDenseHallTCByName, kDattorroDensityByName) — do they ELSE-RESET to the neutral value for presets NOT in the map (else stale latch across preset swaps)? Check applyEngineConfig calls setReflectionTap/setDattorroDensityRoomFill UNCONDITIONALLY (with the default for unlisted presets), not only inside if(found).` },
  { key: 'bounds', p: `BOUNDS / LIFETIME / OOB. VelvetTail: ring indexing (wp - pos[t]) & mask_ — is mask_ correct (pow2-1), pos[t] ≤ mask_? The Field vectors sized to kMaxTaps. DattorroTank density-AP allocation: the widened baseMax = max(room,hall) — does it cover the realized delay (hall base × rateRatio × scale × jitter + hermite headroom)? The reflBuf_ in DuskVerbEngine (reflDelayL/R clamped to mask). Any read of a vector before prepare() sized it? Off-by-one in the LR2 split or the gate ring.` },
  { key: 'dsp-correct', p: `DSP CORRECTNESS of the new algorithms. (1) ReverseRoom gate state machine: is gateGain_ ALWAYS slewed never assigned (continuous → no click)? retrigger (Hold/Closing→Open) reset cleanly? duration-dependent hold (iActive) capped + reset on burst start? (2) VelvetTail: unit-normalize BEFORE per-band level (else level recouples to tap count)? the LR2 complementary split (hi = x - lo) energy-correct? band-0 bimodal env? (3) Dattorro detune applied to delay1/delay2 only (not AP lines), gBase re-derived after? (4) Fork B DenseHall tonal-correction GEQ design. Flag math errors / wrong order / sign.` },
  { key: 'state-reset', p: `STATE-RESET COVERAGE across ALL clear/prepare/reset paths (the classic preset-swap leak). Every NEW per-instance RT state member must be zeroed in clearBuffers AND not leak across setAlgorithm/freeze/preset-swap. Check: VelvetTail::clear zeroes all rings + filter states (lp250/500/2k z-state)? ReverseRoomEngine::clearBuffers zeroes the gate state (gateState_/envFollow_/gateGain_/holdCounter_/inputActiveSamps_) + reflBuf? DuskVerbEngine reflBuf_/reflLpState reset in clearAllBuffers? DenseHall tonal-correction filter state reset in clear()? DattorroTank: the detune/roomfill don't add RT state (config only) — confirm. Flag any new RT member missing from a reset path.` },
  { key: 'xfile-py', p: `CROSS-FILE CONSISTENCY + the full_check.py JND edits. Every new setter DECLARED in a .h has a DEFINITION in the .cpp with matching signature (DuskVerbEngine setReflectionTap/setDattorroDensityRoomFill/setDattorroMainLineDetune/setDenseHallTonalCorrection; DattorroTank setDensityRoomFill/setMainLineDetune; VelvetTail setters used by ReverseRoom). TuningEnv new fields (dhrefl/roomfill/maindet) initialized in the ctor + getenv names match. The DensityConfig struct new fields default-initialized so existing brace-init entries stay valid. full_check.py: the JND tolerance edits (t60 5→10, ss/band 2→3, stereo 0.05→0.10, body −72 floor-skip) — any logic bug (wrong var, broke a gate's pass/fail, the body floor-skip continue skips the wrong branch)?` },
]
const reviews = await parallel (dims.map (d => () =>
  agent (`${SCOPE}\n\nDIMENSION: ${d.p}\nReport ONLY real issues in the CHANGED lines (not pre-existing code). For each: file, line/symbol, severity, issue, concrete fix. Empty findings array if clean.`,
    { label: `review:${d.key}`, phase: 'Review', schema: FINDING })))

phase('Verify')
const all = reviews.filter(Boolean).flatMap(r => r.findings || [])
const verified = await parallel (all.map (f => () =>
  agent (`Adversarially verify this code-review finding against the ACTUAL current code (read the file at ${ROOT}). Is it REAL (a genuine bug in this session's changed lines), or a FALSE POSITIVE (misread, pre-existing, already-handled, or the "fix" would break something)? Read the surrounding code + any reset/init paths before judging. FINDING: ${JSON.stringify(f)}.
  Return {real:bool, severity:string, why:string, correct_fix:string} — real=false if it's a false positive or the claimed issue doesn't hold; correct_fix = the fix you'd actually apply (may differ from the proposed one) or "" if not real.`,
    { label: `verify:${f.file}:${f.line}`, phase: 'Verify',
      schema: { type:'object', additionalProperties:false, required:['real','severity','why','correct_fix'],
        properties:{ real:{type:'boolean'}, severity:{type:'string'}, why:{type:'string'}, correct_fix:{type:'string'} } } })
    .then(v => ({ ...f, verdict: v }))))

phase('Synthesize')
const confirmed = verified.filter(Boolean).filter(f => f.verdict && f.verdict.real)
const out = await agent (
  `You are the review editor. Below are the CONFIRMED (adversarially-verified-real) findings from a code review of this session's DuskVerb changes. Dedup, rank by severity (critical→major→minor), and produce the final punchlist the implementer will fix. For each: file:line, severity, the issue (one line), and the exact fix. Drop anything the verifier marked not-real. Be concise.
  CONFIRMED FINDINGS: ${JSON.stringify(confirmed)}
  Return ONLY the markdown punchlist (or "CLEAN — no confirmed issues" if empty).`,
  { label: 'synthesize-punchlist', phase: 'Synthesize', effort: 'high' })
return out
