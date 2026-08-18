# Handoff: Spectrum Analyzer → DPF

> Prompt for the executing agent: Read `docs/dpf-migration/00-OVERVIEW.md`
> first. Execute on branch `spectrum-analyzer/dpf-core`. This is the
> lightest DSP port in the fleet (the plugin is a passthrough with meters),
> but it is a METERING product: the deliverable is calibrated numbers, so
> "looks right" is not a gate and golden-signal comparisons are.

## Source inventory

`plugins/spectrum-analyzer/Source/` (~2,300 LOC total):

- `PluginProcessor.*` — APVTS with 10 parameters, parameter listeners that
  push settings into the DSP objects, passthrough processBlock that feeds
  five meters plus peak/RMS atomics. No latency, no bypass parameter.
- `DSP/FFTProcessor.*` — THE JUCE anchor: `juce::dsp::FFT`,
  `juce::dsp::WindowingFunction`, and two `juce::AbstractFifo` (16384) for
  stereo capture. 2048 fixed display bins, log-mapped 20 Hz to 20 kHz;
  smoothing, dB/oct slope, decay, peak-hold with counters at an assumed
  30 Hz refresh.
- `DSP/LUFSMeter.*` — includes `juce_dsp` but uses nothing from it
  (vestigial; drop the include when porting). Otherwise plain C++.
- `DSP/TruePeakDetector.h` — plain C++ 4x polyphase FIR interpolation
  (BS.1770-style true peak). No JUCE.
- `DSP/KSystemMeter.h`, `DSP/CorrelationMeter.h`, `DSP/ChannelRouter.h` —
  plain C++ already. Port verbatim.
- `UI/` — `HeaderBar.h`, `SpectrumDisplay.*`, `MeterPanel.*`,
  `SettingsOverlay.h` + `SpectrumAnalyzerLookAndFeel.h`: JUCE Components,
  rebuilt in Dear ImGui.

Threading model to preserve in spirit: the audio thread only pushes samples
into the FIFO; the EDITOR's 30 Hz timer calls `FFTProcessor::processFFT()`
(`PluginEditor.cpp:151`), so all FFT work already happens off the audio
thread. The port keeps that split via the Multi-Q ring pattern below.

## Frozen parameter surface (10 params, order fixed)

| # | id | type | range / choices | default |
|---|----|------|-----------------|---------|
| 1 | `channelMode` | choice | Stereo, Mono, Mid, Side | Stereo |
| 2 | `fftResolution` | choice | 2048, 4096, 8192 | 4096 |
| 3 | `smoothing` | float | 0 to 1, step 0.01 | 0.5 |
| 4 | `slope` | float | -4.5 to +4.5 dB/oct, step 0.5 | 0.0 |
| 5 | `decayRate` | float | 3 to 60 dB/s, step 1 | 20 |
| 6 | `peakHold` | bool | Off/On | Off |
| 7 | `peakHoldTime` | float | 0.5 to 10 s, step 0.1 | 2.0 |
| 8 | `displayMin` | float | -100 to -30 dB, step 1 | -60 |
| 9 | `displayMax` | float | 0 to +12 dB, step 1 | +6 |
| 10 | `kSystemType` | choice | K-12, K-14, K-20 | K-14 |

Read the exact ids from `PluginProcessor.h`'s `PARAM_*` constants when
building the DPF table; the JUCE layout is the single source of truth, and
the A/B gate below diffs the dumped tables.

State: plain APVTS XML under tag `SpectrumAnalyzerState`, no custom
properties, no file references. The DPF port needs only the standard
parameter snapshot; no state-version migration.

## Identities

- JUCE original: code `SpAn`, manufacturer `Dusk`, product
  "Spectrum Analyzer", bundle `com.DuskAudio.SpectrumAnalyzer`, v1.0.1.
  Stays installed for old sessions, maintenance-only.
- Successor: product **"Spectrum Analyzer 2"**, DPF dir
  `plugins/spectrum-analyzer/dpf-plugin`, base `spectrum_analyzer_2`,
  AU subtype **`DsSp`** (free: fleet uses DsTM/DsFq/DsTE/DsMq/DsSC),
  type `aufx`, LV2 URI
  `https://dusk-audio.github.io/plugins/spectrum-analyzer-2`, CLAP id
  `com.dusk-audio.spectrum-analyzer-2`. Register in dpf-build.yml's plan
  registry with tag prefix `spectrum-analyzer-2-v`.

## The hard parts and their answers

1. **FFT without JUCE**: DONE (PR #194). `FFTr2` was promoted VERBATIM from
   `plugins/multi-q/core/MultiQMatch.{hpp,cpp}` to the header-only
   `plugins/shared-dpf/dsp/DuskFft.hpp`: same `duskaudio` namespace, same
   class name, `prepare()` and `transform()` moved with identical
   arithmetic, and the original declaration and definitions were removed
   from the Match files. No build wiring changed, and none should:
   `MultiQMatch.cpp` still holds the entire Match engine and the DPF target
   keeps compiling it; only the FFT left that file. Guards that ran on the
   move: MultiQMatchTest 21/21, JUCE AU + DPF vst3/clap/lv2/au builds, full
   A/B parity harness green. Windowing for THIS port: Hann, verified at
   `FFTProcessor.cpp:216`; port the coefficient formula, including JUCE's
   normalisation, so bin magnitudes match to float tolerance.
2. **Capture FIFO without `juce::AbstractFifo`**: use the Multi-Q analyzer
   ring protocol (`MultiQAccess.hpp` + the rings in `MultiQPlugin.cpp`):
   DSP-side lock-free single-writer ring, UI reads a snapshot each frame and
   runs the FFT on the UI thread. One ring only (post == input; this plugin
   is a passthrough, there is no pre/post distinction). The 2048-display-bin
   log mapping, smoothing, slope, decay, and peak-hold logic move verbatim
   into a framework-free `SpectrumCore` in `plugins/spectrum-analyzer/core/`.
3. **Meter calibration must not move**: LUFS, K-System, correlation,
   true peak, RMS (300 ms integration), and peak are already plain C++.
   Port them byte-for-byte, then LOCK them with the golden-signal protocol
   below before touching anything else. Both builds process the SAME
   generated buffers (not resampled variants), starting from a fresh
   `prepare()` + `reset()`, fed in identical block sizes, with outputs read
   at the same sample offsets. Fixtures are generated, deterministic, and
   stereo unless stated; where noise is used, it is `std::mt19937` seeded
   with 0x5EED so both builds and every CI run see identical samples.

   Run each fixture at 44.1, 48, and 96 kHz, block size 512 (plus one
   repeat of the sine fixture at block 64 and at a ragged 483 to catch
   block-boundary state bugs). Warm-up: discard the first 3 s of readings
   for integrated/short-term LUFS and LRA; everything else reads after 1 s.

   | fixture | observed outputs | expected | tolerance |
   |---|---|---|---|
   | 1 kHz sine, -18 dBFS, 10 s, both channels | momentary/short/integrated LUFS | -18 LUFS (BS.1770 sine calibration) | +/-0.1 LU vs JUCE, +/-0.5 LU absolute |
   | same fixture | K-System K-12/K-14/K-20 level | 0 dB at reference minus 18 | +/-0.1 dB vs JUCE |
   | same fixture | RMS, peak | -18 dBFS RMS +/-0.1; peak -18 dBFS +/-0.01 | vs JUCE: exact float equality |
   | identical noise both channels | correlation (raw + smoothed) | +1.0 | +/-0.01; vs JUCE exact |
   | right = -left noise | correlation | -1.0 | +/-0.01; vs JUCE exact |
   | independent-seed noise per channel | correlation | ~0 (bounded +/-0.1 over 10 s) | vs JUCE exact |
   | inter-sample-peak fixture: +0.5 dBFS true peak from a 0 dBFS-sample 11.025 kHz-at-44.1k phase-offset sine | true peak dB | reads ABOVE sample peak | +/-0.2 dB vs analytic; vs JUCE exact |
   | 30 s silence | every meter | documented floor values (-100 dB conventions, LUFS -inf clamp) | exact |

   "vs JUCE exact" means bitwise-equal floats when the ported code is
   byte-identical and compiled with the same flags; any divergence is a
   port defect until proven a compiler artifact, and an accepted artifact
   loosens that meter's gate to the stated absolute tolerance with a
   written note, never silently.
4. **Editor**: rebuild HeaderBar/SpectrumDisplay/MeterPanel/SettingsOverlay
   in ImGui with DuskImGuiWidgets, uniform top row per the TE2 layout
   convention, DuskSupportersOverlay wired on the title. Hover readout
   (frequency + level under cursor) is part of the product surface, not a
   nicety; keep it.
5. **Passthrough gate**: output must be bit-identical to input at every
   block size, including empty blocks and mono buses. This is the cheapest
   null test in the fleet; make it a unit test, not a manual check.

## Sequencing

1. Promote `FFTr2` to shared-dpf; re-run Multi-Q parity (guard).
2. Extract `core/` (SpectrumCore + the five meters), framework-free; port
   the JUCE processor to consume it unchanged; golden-signal tests green on
   both builds.
3. DPF shell: 10-param table, passthrough, state, rings.
4. ImGui editor.
5. Registry entry + CI matrix; pluginval/clap-validator/LV2 gates.
6. Validation checklist from GH #184: bin/frequency/level checks at
   2048/4096/8192 x 44.1/48/96 kHz, plus TWO separate ring checks:
   - Race detector (required): build the ring's unit test with
     ThreadSanitizer and run it with a writer thread pushing audio-rate
     blocks against a reader thread snapshotting at display rate:
     `clang++ -std=c++17 -O1 -g -fsanitize=thread plugins/spectrum-analyzer/core/tests/ring_tsan.cpp -o ring_tsan && ./ring_tsan`
     Zero TSan reports is the gate. No such harness exists in the repo yet;
     this port adds it (Linux CI or a local Linux/macOS run, either counts,
     but the command must be in the test script, not folklore).
   - Stress (additional, not a substitute): the same pair of threads for
     60 s with randomized block sizes and resolution switches mid-flight;
     gate is no torn snapshot (sentinel pattern check) and no crash.

Do not start the editor before step 2's gates are green: a meter product
with drifted calibration and a pretty UI is a regression that LOOKS done.
