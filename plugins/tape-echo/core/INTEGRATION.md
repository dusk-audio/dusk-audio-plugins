# TapeEchoDSP — Framework Integration Guide

`TapeEchoDSP` is a self-contained, framework-free vintage tape-echo emulation core
(C++17, no JUCE). This guide maps it onto **DPF** and onto a **raw CLAP**
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

## Parameter table

| ID | Name          | Setter            | Range      | Default | Notes |
|----|---------------|-------------------|------------|---------|-------|
| 0  | Mode Selector | `setMode`         | 1–12 (int, stepped) | 1 | 12-position rotary switch |
| 1  | Repeat Rate   | `setRepeatRate`   | 0–1        | 0.5     | 0 = 177 ms (slow motor), 1 = 69 ms |
| 2  | Intensity     | `setIntensity`    | 0–1        | 0.4     | self-oscillates above ~0.75 |
| 3  | Echo Volume   | `setEchoLevel`    | 0–1        | 0.8     | |
| 4  | Reverb Volume | `setReverbLevel`  | 0–1        | 0.0     | only audible in modes 5–12 |
| 5  | Bass          | `setBass`         | −1–+1      | 0.0     | ±12 dB shelf @ 100 Hz, echo path only |
| 6  | Treble        | `setTreble`       | −1–+1      | 0.0     | ±12 dB shelf @ 3 kHz, echo path only |
| 7  | Input Volume  | `setInputGain`    | 0–1        | 0.5     | preamp drive / saturation amount |
| 8  | Wow & Flutter | `setWowFlutter`   | 0–1        | 0.5     | ~7% residual wow always present (real transports are never perfect); knob adds on top |
| 9  | Dry Level     | `setDryLevel`     | 0–1        | 1.0     | instrument-through level |
| 10 | Tempo Sync    | (shell-level)     | off/on     | off     | locks head-1 time to a host-tempo division, octave-folded into 69–177 ms |
| 11 | Sync Division | (shell-level)     | 0–7 (int)  | 2 (1/16)| 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/8., 1/4 |
| 12 | Tape Age      | `setTapeAge`      | 0–1        | 0.0     | 0 = fresh transport (bit-identical to pre-knob builds); worn: hiss onto tape (regenerates with intensity), extra wow, HF loss, level wobble |
| 13 | Bypass        | `setBypass`       | off/on     | off     | host-designated; UI POWER switch, click-free clean passthrough |
| 14 | Out Level     | `getOutputLevel`  | 0–3 (out)  | —       | peak meter, ~300 ms release; exposed as a host OUTPUT parameter the UI reads through the shell (out-of-process-safe). Single-binary formats may read the DSP peak directly via the weak-symbol access bridge as an optimization, never a requirement |

Tempo sync lives in the plugin shell, not the DSP core: the shell converts
division + host BPM to an equivalent Repeat Rate each block (see
`syncDelayMs` in `TapeEchoParams.hpp`), so the core stays host-agnostic and
the motor-inertia smoother gives tape-style glides on tempo changes.

Level note: with Intensity at maximum and all three heads active, the echo
bus can peak near +9 dBFS during self-oscillation (as on the hardware, which
gets *loud*). Either leave it to the user's Echo Volume, or add a host-side
output trim.

## Option A: DPF (recommended for Linux/Wayland)

DPF builds VST3, CLAP, LV2, and a JACK standalone from one source tree, and
its windowing layer (pugl) works under Wayland (embedded GUIs go through
XWayland in today's hosts, which is the practical norm; the JACK standalone
runs native).

```
tape-echo-dpf/
├── dpf/                     # git submodule: github.com/DISTRHO/DPF
├── plugin/
│   ├── DistrhoPluginInfo.h
│   ├── TapeEchoPlugin.cpp  # DSP wrapper (below)
│   └── TapeEchoUI.cpp      # Dear ImGui UI via DPF's DearImGui wrapper
├── core/                    # this directory, unchanged
└── Makefile / CMakeLists.txt
```

`DistrhoPluginInfo.h` essentials:

```cpp
#define DISTRHO_PLUGIN_NAME  "Tape Echo"
#define DISTRHO_PLUGIN_URI   "https://dusk-audio.github.io/tape-echo"
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

    // DPF calls this from the host's parameter thread; the setters are
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

UI: use DPF's `DearImGui` widget (github.com/DISTRHO/DPF-Widgets,
`opengl/DearImGui.hpp`). Subclass `ImGuiTopLevelWidget`, draw knobs with
`ImGui::SliderFloat`/custom knob code, and push edits through
`setParameterValue(index, v)` + `editParameter(index, true/false)` for
host automation handshakes. State flows host → UI via `parameterChanged()`;
never touch the DSP object from the UI process (DPF may run it out-of-process).

Build: `make` with DPF's `Makefile.plugins.mk`, or CMake via
`dpf_add_plugin(tape_echo TARGETS clap vst3 lv2 jack FILES_DSP ...)`.
Add `core/TapeEchoDSP.cpp` to `FILES_DSP`.

## Option B: raw CLAP

CLAP's threading model matches the core exactly: parameter events arrive
*inside* `process()` on the audio thread, so the atomic setters are called
in-place and the block is optionally split for sample accuracy.

Required extensions: `clap.audio-ports` (1 stereo in, 1 stereo out),
`clap.params` (all input params from the table, mode/division flagged
`CLAP_PARAM_IS_STEPPED`), `clap.state` (serialize every input-parameter
value — including the shell-level `tempo_sync` and `sync_division`, which
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
