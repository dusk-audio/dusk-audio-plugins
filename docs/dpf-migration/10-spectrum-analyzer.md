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

1. **FFT without JUCE**: reuse `FFTr2` (`plugins/multi-q/core/MultiQMatch.hpp:72`),
   the fleet's self-contained radix-2 FFT with precomputed twiddles. Step 1
   of this port PROMOTES it to `plugins/shared-dpf/dsp/DuskFft.hpp` and
   repoints Multi-Q at the shared copy (mechanical include change; Multi-Q's
   A/B harness re-run guards it). Windowing: Hann, verified at
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
   Port them byte-for-byte, then LOCK them with golden-signal tests before
   touching anything else: sine at -18 dBFS/1 kHz (LUFS, K), fully
   correlated/anti-correlated/decorrelated noise (correlation), an
   inter-sample-peak fixture (true peak reads above sample peak), and
   silence (floor values). Compare JUCE vs DPF outputs on identical WAVs;
   the gate is equality to float tolerance, not plausibility.
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
6. Validation checklist from GH #184 (bin/frequency/level checks at
   2048/4096/8192 x 44.1/48/96 kHz; TSan or stress pass on the ring).

Do not start the editor before step 2's gates are green: a meter product
with drifted calibration and a pretty UI is a regression that LOOKS done.
