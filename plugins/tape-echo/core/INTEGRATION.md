# TapeEchoDSP — Framework Integration Guide

`TapeEchoDSP` is a self-contained, framework-free vintage tape-echo emulation core
(C++17, no JUCE). This guide maps it onto **DAF** and onto a **raw CLAP**
plugin, both of which are first-class citizens on Linux/Wayland.

## Core contract

```cpp
duskaudio::TapeEchoDSP dsp;

// Main thread (allocates):
dsp.prepare(sampleRate, maxBlockSize);
dsp.reset();

// Audio thread (RT-safe: no allocation, no locks, no I/O):
dsp.processBlock(inputs, outputs, numChannels, numSamples);  // in-place OK

// Any thread (atomic stores):
dsp.setMode(1..12); dsp.setRepeatRate(0..1); dsp.setIntensity(0..1); ...
```

Setters are `std::atomic` stores with `memory_order_relaxed`; the audio
thread snapshots them once per block and per-sample smoothers do the rest.
There is no message queue to service and no "parameter changed" callback to
wire up — call the setters from wherever the host delivers values.

`SpringReverb` is a three-spring bidirectional dispersive waveguide. Each spring
has independent outgoing and returning delay lines, a 24-section second-order
allpass cascade on each direction of travel, and frequency-dependent loss at
the driver and pickup reflections. The fitted one-way group delay is flat below
about 1.8 kHz, peaks near +21 ms at 3.6 kHz, and collapses by 4.2 kHz, so an
impulse produces an ascending chirp and each physical round trip produces a
successively more dispersed, darker chirp. The pickup hears the dispersed wave
only. Input/output transducer filters limit the useful tank band to roughly
100 Hz–5 kHz.

The record path carries a repro noise bed at every Tape Age, including **New**:
its amplitude law is `0.25 · exp(1.386 · age)`, i.e. New/Used/Old rise 1:2:4 and
New is quiet (about −124 dBFS before Echo Volume and Output Volume) rather than
digitally silent. This is deliberate and matches the reference cartridges, but
it retires an invariant earlier releases relied on: **a render at Tape Age 0 is
no longer bit-identical to one from a build without the tape-age knob**, so that
comparison can no longer be used as a null control. The bed is still gated by
Power (bypass returns the input untouched) and by Mix = 0 (dry-only output
carries no wet path at all).

## Parameter table

| ID | Name          | Setter            | Range      | Default | Notes |
|----|---------------|-------------------|------------|---------|-------|
| 0  | Mode          | `setMode`         | 1–12 (int, stepped) | 1 | three playback heads plus spring combinations |
| 1  | Repeat Rate   | `setRepeatRate`   | 0–1        | 0.0     | head 1: about 177 ms (slow) to 69 ms (fast); heads 2/3 scale to 337/489 and 131/189 ms |
| 2  | Intensity     | `setIntensity`    | 0–1        | 0.0     | self-oscillates above ~0.75 |
| 3  | Echo Volume   | `setEchoLevel`    | 0–1        | 0.5     | |
| 4  | Reverb Volume | `setReverbLevel`  | 0–1        | 0.0     | only audible in modes 5–12 |
| 5  | Bass          | `setBass`         | −1–+1      | 0.0     | ≈±17 dB shelf; turnover 67.6–154.3 Hz and Q 1.074–0.494 vary with \|parameter\|; echo path only |
| 6  | Treble        | `setTreble`       | −1–+1      | 0.0     | ≈±17 dB shelf; separate boost/cut turnover laws reach ≈991/1441 Hz at half travel and ≈2.85/3.44 kHz at full travel; Q 0.545–0.443; echo path only |
| 7  | Input Volume  | `setInputGain`    | 0–1        | 0.5     | preamp drive / saturation amount |
| 8  | Wow & Flutter | `setWowFlutter`   | 0–1        | 0.0     | transport modulation amount; the whole motion signal (wow, capstan flutter and the stochastic ~6 Hz scrape-flutter band) is scaled by a shared 1 + 1.5·value + 0.20·age multiplier — this knob contributes the 1.5·value term, Tape Age the 0.20·age term (scrape flutter additionally has its own steeper age law, see Tape Age). 0 is NOT still: the intrinsic transport matches the reference (≈0.45 % wow, ≈0.033 % flutter at Tape Age 0) |
| 9  | Dry Level (legacy) | `setDryLevel` | 0–1        | 1.0     | hidden compatibility control; retains old instrument-through automation |
| 10 | Tempo Sync    | (shell-level)     | off/on     | off     | locks the leading active head to a host-tempo division, clamped to the physical motor range |
| 11 | Sync Division (legacy) | (shell-level) | 0–15 (int) | 2 (1/16)| hidden compatibility parameter; preserves the semantic divisions stored by 0.1-series sessions |
| 12 | Tape Age      | `setTapeAge`      | New/Used/Old | Used | stepped cartridge states; every state includes the captured dark tape/electronics floor, while Used/Old progressively add noise, wow, HF loss, level wobble, and splice wear |
| 13 | Bypass        | `setBypass`       | off/on     | off     | host-designated; UI POWER switch, click-free clean passthrough |
| 14 | Record VU     | `getRecordVuLevel` | 0–3 (out) | —       | average-responding record meter after Input Volume, including feedback; 225 ms attack / 200 ms release |
| 15 | Output Volume | `setOutputVolume` | 0–1        | 0.5     | −20 dB to +20 dB; midpoint is unity |
| 16 | Echo Pan      | `setEchoPan`      | 0–1        | 0.5     | 0 = left, 0.5 = center, 1 = right |
| 17 | Reverb Pan    | `setReverbPan`    | 0–1        | 0.5     | 0 = left, 0.5 = center, 1 = right |
| 18 | Input Send    | `setInputSend`    | off/on     | on      | interrupts only the tape-record feed; spring remains live for reverb-only operation |
| 19 | Record Peak   | `getRecordPeakLevel` | 0–3 (out) | —     | transient record-path peak with a 300 ms release |
| 20 | Mix           | `setMix`          | 0–1        | 0.5     | dry/combined-wet crossfade; 0 = dry, 0.5 = both paths at unity, 1 = wet-only |
| 21 | Echo Rate Note | (shell-level)    | 1–11 (int) | 5       | physical tempo-sync detent; its division table follows the leading active playback head, matching Galaxy |

Mix uses a unity-overlap balance law so the 50% default reproduces the
previous parallel dry-plus-wet output exactly. Below 50% the dry path remains
at unity while the combined echo/reverb bus fades in; above 50% the wet bus
remains at unity while the dry path fades out. This keeps old sessions and
factory-program calibration intact while providing exact dry-only and wet-only
endpoints.

Tempo sync lives in the plugin shell, not the DSP core. The shell maps the
1–11 Echo Rate Note detent through the selected mode's leading-head table,
converts that division plus host BPM into the leading active head's delay each
block (see `syncDelayMs` in `TapeEchoParams.hpp`), derives head-1 time from the
selected mode, and clamps the motor to its measured range. Changing Head Select
therefore keeps the physical detent fixed while changing its note assignment,
as on Galaxy. The hidden Sync Division parameter remains an exact compatibility
path for old sessions. The core stays host-agnostic and its motor-inertia
smoother supplies tape-style glides on tempo changes.

Level note: with Intensity at maximum and all three heads active, the echo
bus can peak near +9 dBFS during self-oscillation (as on the hardware, which
gets *loud*). Either leave it to the user's Echo Volume, or add a host-side
output trim.

## Option A: DAF (recommended for Linux/Wayland)

DAF builds VST3, CLAP, LV2, and a JACK standalone from one source tree, and
its windowing layer (pugl) works under Wayland (embedded GUIs go through
XWayland in today's hosts, which is the practical norm; the JACK standalone
runs native).

```
tape-echo-daf/
├── daf/                     # git submodule: github.com/dusk-audio/DAF (our fork)
├── plugin/
│   ├── DistrhoPluginInfo.h
│   ├── TapeEchoPlugin.cpp  # DSP wrapper (below)
│   └── TapeEchoUI.cpp      # Dear ImGui UI via DAF's DearImGui wrapper
├── core/                    # this directory, unchanged
└── Makefile / CMakeLists.txt
```

`DistrhoPluginInfo.h` essentials:

```cpp
#define DISTRHO_PLUGIN_NAME  "Tape Echo 2"
#define DISTRHO_PLUGIN_URI   "https://dusk-audio.github.io/plugins/tape-echo"
#define DISTRHO_PLUGIN_CLAP_ID "com.duskaudio.tape-echo"
#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_HAS_UI      1
#define DISTRHO_PLUGIN_IS_RT_SAFE  1
```

Plugin class — the entire wrapper is ~100 lines:

```cpp
class TapeEchoPlugin : public Plugin
{
public:
    TapeEchoPlugin() : Plugin(kParamCount, 0, 0) {}

protected:
    void initParameter(uint32_t index, Parameter& p) override
    {
        switch (index) {
        case kParamMode:
            p.hints = kParameterIsAutomatable | kParameterIsInteger;
            p.name = "Mode"; p.symbol = "mode";
            p.ranges.def = 1; p.ranges.min = 1; p.ranges.max = 12;
            break;
        case kParamRepeatRate:
            p.hints = kParameterIsAutomatable;
            p.name = "Repeat Rate"; p.symbol = "repeat_rate";
            p.ranges.def = 0.5f; p.ranges.min = 0.0f; p.ranges.max = 1.0f;
            break;
        // ... remaining rows straight from the table above
        }
    }

    // DAF calls this from the host's parameter thread; the setters are
    // atomic, so forward directly — no locking, no deferral.
    void setParameterValue(uint32_t index, float value) override
    {
        values[index] = value;
        switch (index) {
        case kParamMode:       dsp.setMode((int)value);      break;
        case kParamRepeatRate: dsp.setRepeatRate(value);     break;
        // ...
        }
    }
    float getParameterValue(uint32_t index) const override { return values[index]; }

    void activate() override
    {
        dsp.prepare(getSampleRate(), getBufferSize());
    }
    void sampleRateChanged(double sr) override { dsp.prepare(sr, getBufferSize()); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp.processBlock(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, (int)frames);
    }

private:
    duskaudio::TapeEchoDSP dsp;
    float values[kParamCount] = { /* defaults */ };
};
```

UI: use DAF's `DearImGui` widget (github.com/dusk-audio/DAF-Widgets,
`opengl/DearImGui.hpp`). Subclass `ImGuiTopLevelWidget`, draw knobs with
`ImGui::SliderFloat`/custom knob code, and push edits through
`setParameterValue(index, v)` + `editParameter(index, true/false)` for
host automation handshakes. State flows host → UI via `parameterChanged()`;
never touch the DSP object from the UI process (DAF may run it out-of-process).

Build: `make` with DAF's `Makefile.plugins.mk`, or CMake via
`dpf_add_plugin(tape_echo TARGETS clap vst3 lv2 jack FILES_DSP ...)`.
Add `core/TapeEchoDSP.cpp` to `FILES_DSP`.

## Option B: raw CLAP

CLAP's threading model matches the core exactly: parameter events arrive
*inside* `process()` on the audio thread, so the atomic setters are called
in-place and the block is optionally split for sample accuracy.

Required extensions: `clap.audio-ports` (1 stereo in, 1 stereo out),
`clap.params` (all input params from the table, mode/division flagged
`CLAP_PARAM_IS_STEPPED`), `clap.state` (serialize every input-parameter
value — including the shell-level `tempo_sync`, compatibility
`sync_division`, and physical `echo_rate_note`, which
must round-trip with project state even though the DSP core never sees
them), `clap.gui`.

```cpp
static clap_process_status process(const clap_plugin* plugin,
                                   const clap_process* p)
{
    auto* self = (TapeEchoClap*)plugin->plugin_data;

    // 1. Drain parameter events (optionally split the block at event.time
    //    for sample-accurate automation; block-rate is fine to start).
    const uint32_t nev = p->in_events->size(p->in_events);
    for (uint32_t i = 0; i < nev; ++i) {
        const clap_event_header* h = p->in_events->get(p->in_events, i);
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
            auto* ev = (const clap_event_param_value*)h;
            self->applyParam(ev->param_id, ev->value);  // calls dsp.setX()
        }
    }

    // 2. Render.
    self->dsp.processBlock(p->audio_inputs[0].data32,
                           p->audio_outputs[0].data32,
                           (int)p->audio_inputs[0].channel_count,
                           (int)p->frames_count);
    return CLAP_PROCESS_CONTINUE;
}
```

`clap_plugin.activate(sr, minFrames, maxFrames)` → `dsp.prepare(sr, maxFrames)`;
`start_processing`/`reset` → `dsp.reset()`.

GUI under Wayland: implement `clap.gui` advertising both
`CLAP_WINDOW_API_X11` and `CLAP_WINDOW_API_WAYLAND`. Reality check: as of
2026, most Linux hosts (REAPER, Bitwig, Qtractor) still embed plugin GUIs
via X11/XWayland; Wayland has no cross-process surface-embedding protocol
hosts agree on. Ship X11 embedding as the baseline plus
`is_api_supported(WAYLAND) == true` with a floating toplevel (wl_surface +
xdg_toplevel + EGL + ImGui) for Wayland-native hosts. GLFW or SDL3 with the
Wayland backend gets you the floating window in ~50 lines.

## Porting notes from the JUCE version

| JUCE construct | Replacement in core |
|---|---|
| `juce::AudioBuffer<float>` | raw `float* const*` channel pointers |
| `juce::LinearSmoothedValue` | `duskaudio::SmoothedValue` (one-pole) |
| `juce::ScopedNoDenormals` | `ScopedFlushDenormals` (SSE FTZ/DAZ) in processBlock |
| `apvts.getRawParameterValue()` | `std::atomic<float>` members + setters |
| `juce::dsp::IIR` shelves | `ShelfFilter` (RBJ biquad, TDF2) |
| `prepareToPlay` | `prepare` |

The old `plugins/tape-echo/Source/DSP/` JUCE processor can be retired once a
shell is chosen; nothing in `core/` references it.
