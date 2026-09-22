# Multi-Comp core — port notes (scaffold phase)

Framework-free (no-JUCE) port of `UniversalCompressor` (Multi-Comp), scoped to the
four modes Dusk Studio's channel/bus/master strips use: **Opto, FET, VCA, Bus**.
Source of truth: `plugins/multi-comp/multicomp.cpp` + `UniversalCompressor.h`.

Goal: **bit-level A/B match vs the JUCE processor at the parameter subset Dusk
Studio drives** (every other param at its JUCE default). This file is the
designated judgment log; source comments stay sparse.

This phase delivers the scaffold only: top-level flow, shared services, and the
four mode-class *interfaces* (stubbed). The four mode bodies are transcribed next
by parallel agents — see **§Mode contracts** and the interface headers.

---

## Scope (LOCKED)

**In:** Opto (0), FET (1), VCA (2), Bus (3). **Out:** StudioFET, StudioVCA,
Digital, Multiband; the noise generator; the output distortion stage; internal
audio-path oversampling (AntiAliasing 2x/4x); true-peak detector; global +
digital lookahead; sidechain shelf EQ; Mid-Side / Dual-Mono stereo-link modes.

Everything "out" is either never reachable at the Dusk param subset, or is a
verified no-op at its default value — see the default-trace table.

---

## §Oversampling & latency — RESOLVED (reference config locked)

The reference config is the JUCE processor exactly as Dusk Studio's strips
drive it (verified: `DuskStudio/src/dsp/ChannelStrip.cpp` L234–237, `BusStrip.cpp`,
`MasterBus.cpp`, `MasteringChain.cpp`): `setMinimalProcessing(false)`,
`setInternalOversamplingEnabled(false)`, `prepareToPlay(stripRate)`, all APVTS
params at their defaults — in particular `"oversampling"` at its default
**index 1 ("2x")**. Consequences, replicated by the core:

- **Modes prepared at 2× the processing rate.** JUCE `prepareToPlay`
  (multicomp.cpp:5075–5091) primes every mode at `stripRate*2`; runtime takes
  the NON-oversampled path (gate at multicomp.cpp:6407) and processes at
  `stripRate`; `updateSampleRate` only fires on a param *change*, which never
  happens. The 2×-tuned coefficients are the shipped sound. Core:
  `kModePrepareOversampleFactor = 2` (`UniversalCompressorDSP.cpp`); the top
  level passes the doubled rate into `mode.prepare()`, so the mode
  transcriptions need no special handling.
- **Reported latency = 60 base-rate samples, always.** `updateLatencyReport`
  (multicomp.cpp:7004) reports `AntiAliasing::getLatency()` (60) + global
  lookahead (0) + Digital lookahead (0) from `prepareToPlay` onward and never
  changes it in this config (the oversampling-factor change-detect never fires;
  bypass deliberately keeps reporting it — multicomp.cpp:5323). The AA value is
  the 4× FIR-equiripple latency (`lround(59.5) = 60`, rate-independent; JUCE
  half-band equiripple stage orders 118/78 and 50/34 → 98/2 + 42/4 = 59.5; the
  2×-only value would be 49). The runtime **wet** path has zero actual delay;
  the dry-reference paths below are delayed by 60 to line up with the report.
  Core: `getLatencySamples()` returns the same **60**.
- **GR meter is block-delayed.** JUCE prepareToPlay (multicomp.cpp:5137–5143)
  sets `grDelaySamples = min(ceil(60 / samplesPerBlock), 255)` **blocks**
  (≥ 1 always), and the processBlock tail routes the displayed GR through a
  one-value-per-block ring. So the reference's `getGainReduction()` lags the
  audio by that many blocks even on the non-OS path. Core: replicated
  (`kReportedAaLatencySamples = 60`, `grDelayBuffer` ring, delay computed in
  `prepare` from `maxBlock`, advanced one slot per `processBlock` call).
- The A/B harness drives the JUCE reference exactly as the strips do (config
  above, param defaults untouched); the core needs no special configuration.

### Formerly-flagged deviations — RESOLVED (coordinator ruling: replicate)

- **Bypass + 0% mix:** replicated `emitLatencyAlignedDry` (multicomp.cpp:5271–5330)
  — input-history ring fed every block (preWp semantics), dry emitted delayed by
  the constant reported 60; ring sized like the reference (60 + 2×10 ms + block + 1);
  out-of-spec blocks degrade to undelayed passthrough like the reference's guard.
- **Partial mix (0 < mix < 1):** replicated `mixAtNormalRate`'s tier-2 dry ring
  (shared/DryWetMixer.h L396: `totalDelay = 60`, 256-slot power-of-2 ring, one
  shared write position advanced per sample) — dry is 60-sample-delayed before
  the crossfade, stale/zero ring history after a mix engage included.
- **Bypass → active transition:** replicated the 5 ms crossfade + state resets
  (multicomp.cpp:5536–5564, 6846–6862): fade length `jlimit(64, 2048, sr*0.005)`,
  dry captured per block via the delaySamples==0 memcpy branch (no lookahead in
  scope), fade applied after auto-makeup; `smoothedAutoMakeupGain`/`smoothedGrDb`/
  `primeGrAccumulator` reset on un-bypass; re-bypass cancels an in-progress fade.
  Not enumerated in the ruling but reachable on every comp toggle and required
  for bit-exact post-toggle auto-makeup trajectories.

---

## Default-trace table (param → default → service engaged at defaults → ported)

Params Dusk drives are marked ✎. Defaults from `createParameterLayout`
(multicomp.cpp L4459).

| Param / service        | JUCE default        | Engaged at Dusk settings? | Ported? | Note |
|------------------------|---------------------|---------------------------|---------|------|
| `mode` ✎               | 0 (Opto)            | yes (dispatch)            | yes     | 0..3 only |
| `bypass` ✎             | false               | gate                      | yes     | dry delayed 60 smp + 5 ms un-bypass fade — see §Oversampling resolved |
| `mix` ✎                | 100 %               | crossfade when 0<mix<1    | yes     | dry ring-delayed 60 smp before crossfade — see §Oversampling resolved |
| `auto_makeup` ✎        | 0 (Off)             | when On                   | yes     | GR-based, multiplicative smoother |
| `sidechain_hp` ✎       | 0 Hz (Off)          | when ≥1 Hz                | yes     | `SidechainFilter` |
| `stereo_link` ✎ (impl) | 100 %               | yes (numCh≥2)             | yes     | max-level link |
| `stereo_link_mode`     | 0 (Stereo)          | Stereo path only          | yes(0)  | M/S, Dual-Mono unported (default off) |
| opto_* ✎               | see layout          | yes                       | yes     | gain map `(g-50)*0.8`, jlimit −40..40 |
| fet_* ✎                | see layout          | yes                       | yes     | `fet_transient` NOT driven → 0 |
| vca_* ✎                | see layout          | yes                       | yes     | detector_mode default Adaptive(0) |
| bus_* ✎                | see layout          | yes                       | yes     | ratio choice→{2,4,10}; mix→0..1 |
| `sidechain_enable`     | false               | no (external SC off)      | n/a     | forces `hasExternalSidechain=false` |
| `sc_low_gain`/`sc_high_gain` | 0 dB          | shelf runs but unity      | **no**  | RBJ shelf at 0 dB is exact unity (b==a) — verified numerically; not driven ⇒ skipped |
| `true_peak_enable`     | false               | no                        | no      | detector never called |
| `global_lookahead`     | 0 ms                | no                        | no      | delay line + PDC skipped |
| `distortion_type`      | 0 (Off)             | no                        | no      | `applyDistortion` gate false |
| `distortion_amount`    | 50 %                | no (type Off)             | no      | — |
| `noise_enable`         | true (Dusk forces 0)| no                        | no      | −80 dB analog noise skipped |
| `envelope_curve`       | 0 (Logarithmic)     | inside modes              | (mode)  | consumed by mode DSP, not top level |
| `saturation_mode`      | 0 (Vintage)         | inside modes              | (mode)  | not a top-level param; modes read via ctor/defaults |
| `oversampling`         | 1 (2x)              | prepare-rate + GR delay   | yes (quirk replicated) | modes prepared at 2×; GR meter block-delayed — see §Oversampling |

---

## Substitution table

| JUCE                                                   | Replacement |
|--------------------------------------------------------|-------------|
| `_MM_SET_FLUSH_ZERO_MODE` / `ScopedNoDenormals` / `FloatVectorOperations::disableDenormalisedNumberSupport` | `duskaudio::ScopedFlushDenormals` (DuskDenormals.hpp) |
| `juce::MathConstants<float>::pi`                        | `duskaudio::kPiF` |
| `juce::jlimit / jmin / jmax`                            | `duskaudio::jlimit / jmin / jmax` (JUCE arg order preserved) |
| `juce::Decibels::decibelsToGain` (−100 floor)          | `duskaudio::dbToGain` |
| `juce::Decibels::gainToDecibels` (−100 floor)          | `duskaudio::gainToDb` |
| `juce::SmoothedValue<float, Multiplicative>`           | `duskaudio::MultiplicativeSmoothedValue` (verbatim behavioural port, incl. `approximatelyEqual` early-return) |
| `juce::approximatelyEqual`                             | `duskaudio::approximatelyEqual` (abs=FLT_MIN, rel=FLT_EPSILON) |
| `SIMDHelpers::getPeakLevel`                            | local `getPeakLevel` (identical scalar loop) |
| `SIMDHelpers::applyGain`                              | inline `data[i] *= gain` loop |
| `juce::AudioBuffer<float>` scratch                     | `std::vector<float>[2]` sized in `prepare` |
| `DuskAudio::DryWetMixer` (JUCE-coupled)                | inlined crossfade in `processBlock` — **not** used; see §Oversampling for the dropped dry-delay |
| `juce::dsp::IIR` (SidechainFilter)                     | transcribed transposed-DF-II biquad (RBJ HP, Q=0.707) — already float, no designer swap needed |

`plugins/multi-comp/HardwareEmulation/*.h` are already JUCE-free and are reused
by include (the mode agents include them directly), **not** copied.

---

## Judgment calls

- **Mono handling.** Dusk preps stereo (`setPlayConfigDetails(2,2,…)`) but may
  pass a mono buffer. The core always prepares modes/services for 2 channels
  (superset); mono processing touches only channel 0, whose detector state is
  identical either way → bit-exact. Stereo linking requires `numChannels>=2`, so
  mono skips it exactly as JUCE does.
- **`reset()`.** The JUCE `UniversalCompressor` does not override
  `AudioProcessor::reset()` (it is a no-op), and Dusk calls it once right after
  `prepareToPlay` when state is already fresh — so `reset()` is A/B-irrelevant.
  The core's `reset()` re-zeros DSP/detector/smoother state for hygiene; a mode
  `reset()` method was added to each interface for this (not present in the JUCE
  class — it is the one interface addition, used only by the core lifecycle).
- **SidechainEQ skipped.** At `sc_low_gain = sc_high_gain = 0 dB` an RBJ shelf
  has `b_k == a_k` after normalization, so `y[n] == x[n]` exactly from zero state
  (the `a·x − a·y` terms cancel in float). Dusk never drives the sc-EQ params, so
  omitting the class is bit-exact, not an approximation.
- **Metering.** Input/output/sidechain peak meters and the GR meter are mirrored
  as plain relaxed atomics (Dusk reads `getGainReduction()` etc.). The GR meter's
  block-delay ring IS replicated (see §Oversampling — the reference delays the
  displayed GR by `ceil(60/blockSize)` blocks). GR history (`grHistory`) is a
  pure UI convenience and is not mirrored.
- **Oversized blocks.** Scratch is sized to `maxBlock`. A block larger than that
  yields a dry passthrough + early return (RT-safe degradation), mirroring the
  oversampler's graceful bail; no audio-thread allocation.

---

## §Mode contracts (for the four transcription agents)

Each mode lives in its own header (`OptoCompressor.hpp`, `FETCompressor.hpp`,
`VCACompressor.hpp`, `BusCompressor.hpp`). The **public interface is final** —
the top level calls exactly those signatures. Fill the private state + method
bodies verbatim from the source line range; preserve every constant and
per-sample op order. All four may include `UniversalCompressorServices.hpp`
(Constants, LookupTables, TransientShaper, SidechainFilter) and the JUCE-free
`../HardwareEmulation/*.h`. No JUCE anywhere under `core/`.

| Mode | Source (multicomp.cpp) | Header to fill | Shared services it may use |
|------|------------------------|----------------|----------------------------|
| Opto | L1180–L1636 | `OptoCompressor.hpp` | Constants::T4B_*, HardwareEmulation (Tube/Transformer) |
| FET  | L1637–L2165 | `FETCompressor.hpp`  | `LookupTables*`, `TransientShaper*` (passed by top level), Constants::FET_*, HardwareEmulation |
| VCA  | L2166–L2528 | `VCACompressor.hpp`  | Constants::VCA_* |
| Bus  | L2529–L2957 | `BusCompressor.hpp`  | Constants::BUS_*, HardwareEmulation |

Prepare rate: the top level passes the rate to `mode.prepare()`; agents just
transcribe `prepare(rate, …)` faithfully (do not hardcode any oversampling
assumption — see §Oversampling).

The exact interface each must implement is captured in the header stubs (final
signatures + PORT_PENDING bodies) and repeated in the delivery message.
