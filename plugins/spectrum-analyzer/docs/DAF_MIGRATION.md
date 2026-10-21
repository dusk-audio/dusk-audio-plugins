# Spectrum Analyzer 2: DAF migration

Tracks [#184](https://github.com/dusk-audio/dusk-audio-plugins/issues/184).
This plan is based on the current shipping adapter and `core/`, rather than the
missing historical `docs/dpf-migration` handoff. Use the dusk-audio/DAF fork and
the existing `shared-daf` helpers.

## Frozen product contract

Keep the released Spectrum Analyzer and its `SpAn` identity unchanged. The new
product is **Spectrum Analyzer 2**, with build/asset key `spectrum-analyzer-2`,
brand/manufacturer `Dusk`, unique ID/AU subtype `DsSA`, AU type `aufx`, CLAP ID
`com.duskaudio.spectrumanalyzer2`, and LV2 URI
`https://dusk-audio.github.io/plugins/spectrum-analyzer-2`. These successor IDs
have no current repository collision. Enable DAF's canonical cross-platform
VST3 UID layout for this new product and freeze its emitted processor/controller
IDs in native factory tests before release.

The ten saved controls retain their IDs, meanings, linear ranges, increments,
choice order and defaults from `Source/PluginProcessor.cpp`:

| ID | Domain / choices | Step | Default |
| --- | --- | --- | --- |
| `channelMode` | Stereo, Mono, Mid, Side (0–3) | 1 | Stereo (0) |
| `fftResolution` | 2048, 4096, 8192 (0–2) | 1 | 4096 (1) |
| `smoothing` | 0–1 | 0.01 | 0.5 |
| `slope` | −4.5–4.5 dB/octave | 0.5 | 0 |
| `decayRate` | 3–60 dB/second | 1 | 20 |
| `peakHold` | Off / On (0–1) | 1 | Off (0) |
| `peakHoldTime` | 0.5–10 seconds | 0.1 | 2 |
| `displayMin` | −100–−30 dB | 1 | −60 |
| `displayMax` | 0–12 dB | 1 | 6 |
| `kSystemType` | K-12, K-14, K-20 (0–2) | 1 | K-14 (1) |

Define this contract once in a framework-free descriptor table for native
parameter metadata, UI controls and state keys. Snap discrete values at entry.
In particular, apply the adapter's **Off** peak-hold default; the standalone FFT
class initially enables it.

Native state is versioned and contains the complete ten-control set. Validate
all fields into temporaries, reject non-finite/out-of-range/malformed values,
and commit the validated set transactionally. Parse and format independently
of locale. Analysis buffers, loudness history, clipping latches and reset
commands are transient. Preserve default-only program semantics without adding
a preset bank. Native formats must round-trip controls; importing the old
JUCE-specific binary state is not part of the successor contract.

The editor starts at 900×600 and refreshes at 30 Hz. Its corner-resizer comfort
floor is 720×480; the shared legacy helper separately permits host sizes down to
1×1, with a 3840×2160 ceiling and no fixed aspect ratio. Preserve that distinction
in native resize negotiation. Window-size preferences are separate from project
control state and use the successor's own preference key; restored preferences
are clamped to the comfort bounds as in the legacy helper.

## Existing work and calibration

Reuse `FFTProcessor`, `LUFSMeter`, `KSystemMeter`, `TruePeakDetector`,
`CorrelationMeter`, `ChannelRouter`, and `SpectrumRing`. They are already
framework-free; FFT uses `shared-daf/dsp/DuskFft.hpp`. Do not rewrite these
algorithms or replace their calibration during the port.

Retain Hann normalization, display mapping/smoothing/slope/decay/peak hold,
K-System behavior, true-peak filtering and correlation. LUFS retains its current
channel-averaged energy convention, gating, and up-to-6000 gated-block history.
The golden suite explicitly freezes this behavior, including its difference
from a channel-summed BS.1770 interpretation.

The legacy callback routes only the FFT feed; loudness, K-System, true peak,
correlation and output/RMS meters use the original input. Mono duplicates the
input for analysis. Audio remains unchanged. Preserve this distinction.

## Implementation order and gates

1. **Standalone core contract.** Add a JUCE/DAF-independent CMake test entry,
   freeze the descriptor table against the legacy parameter surface, and register
   the existing golden and ring tests. Capture prepare/reset/re-prepare behavior
   and meter results at fixed sample positions before changing runtime ownership.
2. **Capture → worker → snapshot.** Keep the audio callback to bit-exact
   passthrough and bounded, preallocated stereo capture with sample-clock and
   lifecycle information. Keep the complete existing analysis implementation on
   a worker: LUFS currently appends history and allocates LRA scratch storage.
   The worker owns analysis/settings/reset operations; the UI reads coherent
   snapshots, never live meter objects. Reuse the established analyzer/worker
   seams before adding a helper. Queue overflow/gaps must be explicit and tested,
   not reported as uninterrupted loudness history. Exercise editor closure,
   parameter changes, resets and sample-rate/block-size changes concurrently.
3. **Thin native shell.** Add `daf-plugin/` with the frozen controls, complete
   state, no MIDI, zero latency/tail, stereo and required mono effect topology.
   Probe the shipping adapter's native layouts before declaring extra layouts.
   Gate zero/reduced/oversized blocks, in-place/separate buffers, bit-exact
   passthrough, no audio-thread allocation/FFT/locks, and worker teardown.
4. **Editor and shipping gates.** Port the spectrum graph, header, settings,
   hover readout, meter panel and reset actions using existing Dusk ImGui pieces.
   Add the successor to the shared native build/release matrix. Exercise actual
   parameter/state/program APIs, resize/reopen, UI interactions, pluginval,
   clap-validator and LV2 instantiation on the shipping platforms. Obtain visual
   and DAW acceptance before using the release workflow.

Each slice stays runnable and gets a failure control for the behavior it guards.
Do not widen it into a redesign, a new FFT provider or new loudness behavior.
Keep #184 open until its source, calibration, native and interaction gates are
demonstrated. Coordinate publication with the fleet release criteria in #189.

## Baseline evidence (2026-10-21)

The existing standalone C++ golden suite passes at 44.1/48/96 kHz and all three
FFT resolutions with zero failures. The 10-second ring stress passes with 3,407
blocks read and zero torn reads. The ThreadSanitizer build passes with 3,157
blocks read, zero torn reads and no TSan report. These checks establish the
existing core/ring baseline; they do not prove the future runtime or native port.
