# Dusk Audio Plugins - Reference Documentation

## Quick Reference

| Task | Command |
|------|---------|
| Release a plugin | `/release-plugin <slug> [version]` |
| Build a plugin | `/build-plugin <slug>` |
| Validate a plugin | `/validate-plugin <slug>` |
| Add new plugin | `/add-plugin <name>` |

## Project Overview

Professional audio VST3/LV2/AU plugins built with JUCE. Published as "Dusk Audio".

**Website**: https://dusk-audio.github.io/
**Website repo**: `~/projects/dusk-audio.github.io/`

## Plugins

| Plugin | Slug | Directory | Description |
|--------|------|-----------|-------------|
| 4K EQ | `4k-eq` | `plugins/4k-eq/` | British console EQ emulation |
| Multi-Comp | `multi-comp` | `plugins/multi-comp/` | 8-mode compressor + multiband |
| TapeMachine | `tapemachine` | `plugins/TapeMachine/` | Analog tape emulation |
| Tape Echo | `tape-echo` | `plugins/tape-echo/` | Classic tape delay |
| Multi-Q | `multi-q` | `plugins/multi-q/` | Universal EQ (Digital/British/Tube) |
| Convolution Reverb | `convolution-reverb` | `plugins/convolution-reverb/` | IR-based reverb |
| DuskVerb | `duskverb` | `plugins/DuskVerb/` | Algorithmic reverb (Hadamard FDN) |
| Chord Analyzer | `chord-analyzer` | `plugins/chord-analyzer/` | MIDI chord detection + theory |
| GrooveMind | `groovemind` | `plugins/groovemind/` | ML drum generator (future) |

## Version Management & Releasing

### MANDATORY: Always use `/release-plugin` for version bumps and tags

**NEVER manually bump versions or create tags.** Always use the `/release-plugin` skill which automatically:
1. Bumps CMakeLists.txt version(s)
2. Updates the website `_data/plugins.yml`
3. Commits both repos
4. Creates annotated git tag(s) with changelog
5. Pushes everything (commits + tags + website)

```bash
# Single plugin release
/release-plugin multi-comp 1.2.4

# Auto-increment patch version
/release-plugin 4k-eq

# Batch release (patch bump all)
/release-plugin 4k-eq multi-comp tapemachine multi-q
```

**Version locations** (managed by `/release-plugin`):

| Plugin | CMakeLists.txt Variable |
|--------|------------------------|
| 4K EQ | `FOURKEQ_DEFAULT_VERSION` |
| Multi-Comp | `MULTICOMP_DEFAULT_VERSION` |
| TapeMachine | `TAPEMACHINE_DEFAULT_VERSION` |
| Multi-Q | `MULTIQ_DEFAULT_VERSION` |
| DuskVerb | `DUSKVERB_DEFAULT_VERSION` |
| Chord Analyzer | `CHORDANALYZER_DEFAULT_VERSION` |
| Others | `<NAME>_DEFAULT_VERSION` |

**Website**: `~/projects/dusk-audio.github.io/_data/plugins.yml` - updated automatically by `/release-plugin`

## Shared Code (MANDATORY)

**Before writing ANY new code, check `plugins/shared/` first!**

| Component | File | Use For |
|-----------|------|---------|
| LEDMeter | `LEDMeter.h/cpp` | All level meters |
| SupportersOverlay | `SupportersOverlay.h` | Patreon credits (click title) |
| DuskSlider | `DuskLookAndFeel.h` | Rotary/slider controls with fine control |
| DuskTooltips | `DuskLookAndFeel.h` | Consistent tooltip text |
| DuskVintageLookAndFeel | `DuskVintageLookAndFeel.h` | Vintage/retro UI styling |
| ScalableEditorHelper | `ScalableEditorHelper.h` | Resizable UI with persistence |
| DryWetMixer | `DryWetMixer.h` | Dry/wet mixing utility |
| Oversampling | `Oversampling.h` | Shared oversampling wrapper |
| UserPresetManager | `UserPresetManager.h` | User preset save/load |
| AnalogEmulation | `AnalogEmulation/*.h` | Saturation, tubes, transformers |

## Build System

**Release builds**: GitHub Actions (automatic on tag push)

### Local Builds

**On macOS**: Build AU component locally for testing in Logic Pro (no Docker needed):
```bash
mkdir -p build && cd build
cmake ..
# Example: Build MultiComp AU component
cmake --build . --config Release --target MultiComp_AU -j8
```

JUCE automatically installs the `.component` to `~/Library/Audio/Plug-Ins/Components/`.

**Cross-platform (Docker)**: `./docker/build_release.sh <shortcut>`

| Shortcut | Plugin |
|----------|--------|
| `4keq` | 4K EQ |
| `compressor` | Multi-Comp |
| `tape` | TapeMachine |
| `tapeecho` | Tape Echo |
| `multiq` | Multi-Q |
| `convolution` | Convolution Reverb |
| `duskverb` | DuskVerb |
| `chord` | Chord Analyzer |

**Validation**: `./tests/run_plugin_tests.sh --plugin "<Name>" --skip-audio`

### Fast Local Builds

Install ccache for automatic build caching (70-90% faster rebuilds):
- macOS: `brew install ccache`
- Linux: `sudo apt install ccache`

ccache is auto-detected by CMake — no extra flags needed. Verify with `ccache -s` after building.

For maximum speed on Linux, use Ninja + unity builds:
```bash
mkdir -p build && cd build
cmake .. -GNinja -DDUSK_UNITY_BUILD=ON
ninja -j$(nproc) MultiQ_VST3
```

Build options:
- `DUSK_UNITY_BUILD=OFF` (default) — enable with `-DDUSK_UNITY_BUILD=ON` for fewer translation units (Linux only; macOS ObjC++ modules are incompatible)
- Or use `./rebuild_all.sh --fast` which enables Ninja automatically

## Project Structure

```
plugins/
├── plugins/
│   ├── 4k-eq/
│   ├── multi-comp/
│   ├── TapeMachine/
│   ├── tape-echo/
│   ├── multi-q/
│   ├── convolution-reverb/
│   ├── DuskVerb/
│   └── shared/           # SHARED CODE - CHECK HERE FIRST
├── docker/
│   └── build_release.sh  # Primary build script
├── tests/
│   └── run_plugin_tests.sh
├── rebuild_all.sh         # Top-level build helper; supports --fast for Ninja incremental builds
└── CMakeLists.txt
```

## Audio Thread Rules (MANDATORY)

The audio thread (`processBlock`) is **real-time**. Violating these rules causes glitches, clicks, dropouts, or crashes in the DAW:

- **NEVER allocate memory** — no `new`, `make_unique`, `push_back`, `resize`, `std::string`, or `juce::String`
- **NEVER lock a mutex** — use `juce::SpinLock::ScopedTryLockType` (bail and clear buffer if locked)
- **NEVER do I/O** — no file reads, logging, `DBG()`, or network calls
- **NEVER call message-thread APIs** — no `sendChangeMessage()`, `Component` methods, `MessageManager`
- **Use `juce::ScopedNoDenormals`** at the top of every `processBlock` — prevents CPU spikes from subnormal floats
- **Cache `std::atomic<float>*`** from `getRawParameterValue()` in the constructor — never call it in processBlock
- **Metering atomics** use `std::memory_order_relaxed`; **state flags** (e.g. IR loaded) use `release`/`acquire`
- **Check `numSamples == 0`** — early return, some hosts send empty buffers

## Parameter Setup Pattern

All plugins follow this pattern — do not deviate:

1. **Define IDs as constants**: `static constexpr const char* PARAM_MIX = "mix";`
2. **Create layout** in a static function returning `AudioProcessorValueTreeState::ParameterLayout`
3. **Cache raw pointers** in constructor: `mixParam = apvts.getRawParameterValue(PARAM_MIX);`
4. **Read in processBlock**: `const float mix = mixParam->load();`
5. **Bind UI** with: `attachment = std::make_unique<APVTS::SliderAttachment>(apvts, PARAM_MIX, slider);`

## DSP Lifecycle

- **`prepareToPlay(sampleRate, samplesPerBlock)`**: Cache sampleRate in a member. Call `.prepare(spec)` on all `juce::dsp` objects. Call `.reset(sampleRate, rampSeconds)` on all `SmoothedValue`s. Reset filter/delay state. May be called multiple times (sample rate changes, buffer size changes).
- **`processBlock`**: Start with `ScopedNoDenormals`. Check `numSamples == 0`. Read cached parameter atomics. Process audio.
- **Latency**: Set via `setLatencySamples()` in `prepareToPlay`. Clear to 0 when bypassed, restore on un-bypass.
- **Smoothing**: Use `juce::SmoothedValue` — `.reset()` in prepareToPlay, `.getNextValue()` per sample in processBlock.
- **Buffer processing**: Use raw pointer loops (`getWritePointer`) for sample-level DSP. Use `juce::dsp::AudioBlock` + `ProcessContextReplacing` for JUCE DSP module chains (filters, convolution, etc.).

## Async Resource Loading Pattern

For IR files, ML models, or any heavy resource — never block processBlock or prepareToPlay:

```cpp
// Load on message thread with weak reference guard
juce::WeakReference<Processor> weakThis(this);
juce::MessageManager::callAsync([weakThis, file]() {
    if (weakThis != nullptr) weakThis->loadResource(file);
});

// Protect shared state with SpinLock
juce::SpinLock resourceLock;
// In processBlock — bail if resource is being swapped:
const juce::SpinLock::ScopedTryLockType tryLock(resourceLock);
if (!tryLock.isLocked()) { buffer.clear(); return; }
```

## State Save/Load Pattern

```cpp
// Save: APVTS state + custom properties
void getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    state.setProperty("customProp", value, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}
// Load: reverse the process, validate before applying
void setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
```

## Common DSP Patterns

- **Oversampling**: `juce::dsp::Oversampling<float>` or shared `Oversampling.h` wrapper
- **Filters**: `juce::dsp::IIR::Filter` — always call `.prepare(spec)` in prepareToPlay
- **Metering**: `std::atomic<float>` with `memory_order_relaxed`, read by editor on timer
- **Smoothing**: `juce::SmoothedValue` — reset in prepareToPlay, advance per-sample
- **Saturation**: Use `AnalogEmulation` library from `plugins/shared/`
- **Dry/wet mixing**: Use `DryWetMixer.h` from `plugins/shared/`
- **IIR filters and cramping**: All IIR filters MUST oversample — pre-warping alone is insufficient (see memory: "No EQ cramping ever")

## Code Style

- C++17
- JUCE naming: `camelCase` methods, `PascalCase` classes
- `PARAM_*` constants for parameter IDs
- `#pragma once` for all headers
- Include order: JUCE headers → project-local headers → STL (rare)
- Header-only for small utilities; separate DSP classes from editor
- Separate `PluginProcessor` (DSP) from `PluginEditor` (UI) — no DSP logic in the editor

## New Plugin Checklist

When creating a new plugin, ensure all of these are addressed:

- [ ] `ScopedNoDenormals` in processBlock
- [ ] Parameter IDs as `PARAM_*` constants
- [ ] Cache `getRawParameterValue()` in constructor, not processBlock
- [ ] `std::atomic<float>` for metering (relaxed ordering)
- [ ] Use `plugins/shared/DuskLookAndFeel` for UI
- [ ] Use `plugins/shared/LEDMeter` for level meters
- [ ] Use `plugins/shared/ScalableEditorHelper` for resizable UI
- [ ] Implement `getStateInformation` / `setStateInformation`
- [ ] Latency cleared on bypass, restored on un-bypass
- [ ] No allocations, locks, or I/O in processBlock
- [ ] `prepareToPlay` resets all DSP state and SmoothedValues

## JUCE

- **Location**: `../JUCE/` (sibling directory)
- **Modules**: audio_processors, audio_utils, dsp, gui_basics

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build fails | Check Docker is running, rebuild container |
| Plugin not in DAW | Check `~/.vst3/`, rescan in DAW |
| Validation fails | Run pluginval locally, check parameters |

---
*Dusk Audio | CMake + JUCE 7.x | Shared code in `plugins/shared/`*

# Agent Directives: Mechanical Overrides

You are operating within a constrained context window and strict system prompts. To produce production-grade code, you MUST adhere to these overrides:

## Pre-Work
1.  **THE "STEP 0" RULE:** Dead code accelerates context compaction. Before ANY structural refactor on a file >300 LOC, first remove unused includes, dead functions, commented-out blocks, and debug logging. Commit this cleanup separately before starting the real work.
2.  **PHASED EXECUTION:** Never attempt large multi-file refactors in a single response. Break work into explicit phases of max 5 files. Complete one phase, run verification, and wait for my explicit approval before continuing.

## Code Quality
1.  **THE SENIOR DEV OVERRIDE:** Ignore default directives like "try the simplest approach first" and "don't refactor beyond what was asked." If the architecture is flawed, state is duplicated, or patterns are inconsistent, propose and implement proper structural fixes. Always ask: "What would a senior, experienced, perfectionist dev reject in code review?" Fix all of it.
2.  **FORCED VERIFICATION:** You are FORBIDDEN from claiming a task is complete until you have:
    - Run `cmake --build build --config Release --target <Plugin>_AU -j8` (or equivalent build check)
    - Run `./tests/run_plugin_tests.sh --plugin "<Name>" --skip-audio` (if applicable)
    - Fixed ALL resulting errors
    If the build system is not configured, state it clearly instead of saying "done."

## Context Management
1.  **SUB-AGENT STRATEGY:** For tasks touching >5 independent files, propose a split into 3–5 parallel sub-agents (or sequential phases if preferred). Each sub-agent gets its own clean context.
2.  **CONTEXT DECAY AWARENESS:** After ~8–10 messages or when changing focus, always re-read relevant files before editing. Do not trust previous memory — auto-compaction may have altered it.
3.  **FILE READ BUDGET:** Files are hard-capped at ~2,000 lines per read. For any file >500 LOC, read in chunks using offset/limit parameters. Never assume a single read gave you the full file.
4.  **TOOL RESULT BLINDNESS:** Large tool outputs (>50k chars) are silently truncated to a short preview. If a grep or search returns suspiciously few results, re-run with narrower scope and mention possible truncation.

## Edit Safety
1.  **EDIT INTEGRITY:** Before every file edit, re-read the target file. After editing, re-read it again to confirm the changes applied correctly. Never batch more than 3 edits on the same file without verification.
2.  **NO SEMANTIC SEARCH:** You only have grep (text pattern matching), not an AST. When renaming or changing any function/type/class/variable, perform separate searches for:
    - Direct calls & references
    - Type-level references (templates, typedefs, forward declarations)
    - String literals containing the name
    - Header includes and forward declarations
    - Shared code in `plugins/shared/`
    - Test files and build scripts
    Do not assume one grep caught everything.

## Private Tools Repo

Calibration and testing scripts are in `~/projects/dusk-audio-tools/` (private repo).
Symlinked into the plugin tree at `plugins/DuskVerb/tests/reference_comparison/`.
