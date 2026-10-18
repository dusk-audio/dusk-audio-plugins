# Opto E2E learned capture: closure record (2026-10-02)

## Why it closed

The pre-registered kill criterion was "C2 misses B4 or B5 → the E2E direction closes". C2 missed
both:

| Bar | C2 result | Physical R11a |
|---|---|---|
| B4, DEV gain-reduction error | 1.035 dB | 0.398 dB |
| B5, 1 kHz witness H2–H5 | −6.7 / −14.7 / −6.6 / −10.3 dB | — |

- B4 paired bootstrap: +0.64 dB, 95% CI [0.42, 0.86].
- B5 was also not closer than the waveform-only loss.
- Owner accepted the verdict on 2026-10-02. No C3 without new mechanism evidence.
- The physical R11a was the default throughout and stays so.

Full reports: `opto-e2e-c1-pilot-2026-10-01.md`, `opto-e2e-c2-pilot-2026-10-02.md`. Plan:
`opto-e2e-learned-plan-2026-10-01.md`.

## What was measured and is reusable

1. **The UAD Opto input element is gated by the channel's own input level.**
   - It switches on near **−25 dBFS input peak**: −24 / −25 / −25 dBFS at PR .25 / .50 / .80, while
     the GR onset moves from −10 to ≤ −40 dBFS.
   - The step is 13–45 dB per harmonic within about 2.6 dB of input. Below it, harmonics equal the
     PR 0 control.
   - Above it, the harmonic ratios are flat with level (−0.4 … +0.2 dB/dB).
   - A linked louder channel does not switch it on (E2).
   - **Post-cell placement is falsified**, both by the switch-on position and by the flat ratios.
   - Data: `build-multi-comp-1176/opto-e2e-20261001/c2/element-probe/` and `c2/gate-probe/`.
2. **The Chebyshev element.** `x + √2·e·Σ a_k (T_k(v) − T_k(0))`, with `v = x/(√2·e)`.
   - On a steady tone it adds pure k-th harmonics at ratio `a_k`, with no leak into the fundamental
     (verified: ΔH1 ≤ 0.005 dB).
   - A power basis `(x/e)^k` leaks odd orders into H1, so harmonic and level objectives fight.
3. **The B4 target lesson.**
   - A frame-envelope (absolute output level) target does not isolate the PR → GR law.
   - The GR metric is PR 0 level error minus compressed-level error. In C2 these were 0.90 and
     1.00 dB RMS, partly cancelling.
   - Dense settings (5,000) halved the frame error, from 1.76 to 0.88 dB, but barely moved GR
     error (1.14 → 1.04 dB).
4. **Smaller facts:**
   - **Gain taper:** the UAD Gain knob is mute below about 0.099 (−214 dBFS out at 0.0845).
   - **Export parity:** the reference is the exact (float64) model; float32 control paths drift
     about 2–5e-6.
   - **Bit-null checks:** compare against a pristine `git archive` build on the current DAF, never
     the installed binary. The DAF update of 10-01 changed the binary but not the audio: 2,410
     stems sample-exact.

## Where things live

**Learned engine source:**
- Removed from the working tree, uncommitted.
- The full C1/C2 state is preserved as one verified patch on top of `94d9cacb`:
  `build-multi-comp-1176/opto-e2e-20261001/parked-c1c2-state/c1c2-learned-engine.patch`. Copies
  of the two headers sit alongside it.
- Applied to a pristine HEAD export, the patch reproduces all 5 files byte for byte.
- The owner creates the parked branch from it.

**Tools:** `dusk-audio-tools/plugins/MultiComp/tests/programme_parity/opto/e2e/`.

**Kept evidence:** under `build-multi-comp-1176/opto-e2e-20261001/`:
- `c2/element-probe/` and `c2/gate-probe/`;
- `installed-backup/` (AU reference binaries `4aea742e` and `f7723433`);
- `parked-c1c2-state/`, `daf-drift/`, `ctest-logs/`;
- every JSON, log and markdown file: corpus manifests, setting lists, scorecards, parity tables,
  histories;
- the DEV-set audio that later Opto batteries need.

**Deletion candidates (owner deletes):**
- `deletion-candidates-files.txt`, with summary `deletion-candidates-summary.txt`: 90.75 GiB in
  25,221 bulky files.
- The build dirs `build-mc2-e2e` (27 MB) and `build-mc2-e2e-off` (56 MB).
- Checked: no report cites a candidate.
