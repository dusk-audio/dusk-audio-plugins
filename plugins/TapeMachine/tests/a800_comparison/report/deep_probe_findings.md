# A800 deep-probe findings — TapeMachine vs UAD (456 / NAB / 15 IPS / Repro)

All 7 categories measured. "mine" = TapeMachine A800 (Swiss800), target = UAD A800 factory defaults.

## 1. Frequency response (from the 18-config matrix)
- Head bump **too high in frequency**: mine 52 Hz vs UAD 32 Hz @15 IPS; and **too weak at 30 IPS** (mine +1.1 dB vs UAD +4.1 dB). Mine's gain speed-scaling is *backwards* — mine weakens the bump at 30 IPS, UAD strengthens it.
- HF **rolls off too early** everywhere (mine −3 dB @15.8 k vs UAD flat to 20 k) — the two stacked HF shelves over-attenuate.
- CCIR less accurate than NAB.

## 1b. Saturation-dependent HF (HF erasure)
- Real tape loses HF when pushed. Mine's HF **rises** when hot (−0.52 → +0.30 dB @10 k); UAD stays flat/slightly down. Mine models no HF erasure.

## 2. Harmonics & IMD — mine far too clean
- Harmonic profile @ −6 dBFS: mine 3f −63 / 5f −80, **even harmonics absent** (−120). UAD 3f −51 / 5f −66 and **real even** (2f −80). Mine ~12 dB less odd, and no amp even-order colour.
- SMPTE IMD: mine **0.59%** vs UAD **3.79%**.

## 3. Transient / compression — mine doesn't compress
- Crest factor (input 27.9 dB): UAD **27.2 dB** (rounds transients), mine **29.0 dB** (slightly sharpens). Mine lacks the program-dependent transient compression.

## 4. Signal path
- Input: both flat (mine +0.09 / UAD 0.00) ✅. Repro: mine −0.52 / UAD −0.31 (close). **Sync too dark**: mine −2.20 vs UAD −0.31 @10 k.

## 5. Bias behaviour — mine's Bias knob is inert
- UAD: under-bias → brighter + **much** more THD (HF +3.8 dB, THD 14.3%); over-bias → darker + less THD (HF −1.7 dB). Correct physical behaviour.
- Mine: HF −0.20 dB and THD 2.25% **at every bias** — the Bias knob does essentially nothing to HF or distortion.

## 6. Crosstalk & noise
- Crosstalk: mine **−46 dB** L→R (its baked 0.005 bleed); UAD none (−240) — for a stereo instance UAD models no adjacent-track bleed.
- Idle noise: mine **none** (−240, noise is signal-dependent); UAD **−82 dBFS** hiss **plus strong mains hum** (−3.3 dB of its noise is 50/60 Hz). Mine models neither idle hiss nor hum.

## 7. Digital benchmarks
- Aliasing (hot HF): mine **−38.5 dB** worst in-band spur vs UAD **−63 dB** — mine aliases more despite 4× OS.
- Phase: UAD rotates far more at HF (−6919° @10 k vs mine −3213°).

---
## Tuning priority (audible impact × tractability)
1. **FR: head bump + HF extension** — rework head-bump freq + speed-gain scaling, reduce HF shelf loss. Shared consts, clear targets. *(tweaks)*
2. **Saturation: more drive + even-order harmonics** — mine too clean; add amp even-harmonic stage + raise odd. *(feature)*
3. **Bias behaviour** — make Bias affect HF + THD like UAD. *(fix)*
4. **Transient compression** — add program-dependent compression. *(feature)*
5. **Idle noise: hiss + hum** — add constant hiss + mains hum. *(feature)*
6. **Sync path darkness**, **saturation-dependent HF loss**. *(tweaks)*
7. **Crosstalk** (reduce mine's bleed), **aliasing** (investigate). *(tweaks/invesigate)*
8. Phase — informational, skip.
