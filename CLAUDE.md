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

```text
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
// Load: reverse the process, validate before applying. Parse into a ValueTree,
// TYPE-CHECK every custom property into a temporary first, and only then call
// replaceState — a half-applied state is worse than a rejected one, so bail
// without touching the APVTS if anything is missing or the wrong type.
void setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
        return;

    // getProperty() returns a var: a missing or wrong-typed entry casts to 0
    // silently, so check the type BEFORE converting. hasProperty() alone is not
    // enough — it passes for a String that then reads as 0.0f. Numbers written
    // by setProperty(float) come back as isDouble; isInt covers a whole number
    // that JUCE narrowed on the way out.
    const juce::var customVar = state.getProperty("customProp");
    if (!customVar.isDouble() && !customVar.isInt())
        return;                       // repeat for every required custom property
    const float loadedValue = (float) customVar;

    apvts.replaceState(state);        // only now: every property validated
    value = loadedValue;              // apply the validated temporaries after
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

# Agent Directives

## Code Quality
1.  **Senior dev bar:** Ask "what would a senior, experienced, perfectionist dev reject in code review?" and fix all of it within the code the task touches. Where the architecture is flawed, state is duplicated, or patterns are inconsistent beyond that, say so and propose the structural fix — don't silently widen the task, and don't bundle unrequested refactoring into a bug fix.
2.  **Verification before "done":** A task is complete only when:
    - `cmake --build build --config Release --target <Plugin>_AU -j8` (or the equivalent build check) passes,
    - `./tests/run_plugin_tests.sh --plugin "<Name>" --skip-audio` passes (if applicable),
    - and all resulting errors are fixed.
    If the build system is not configured, state that clearly instead of saying "done." Report the real output — "it builds" without the output is not verification.

## Refactor Hygiene
Before a structural refactor on a large file, strip dead weight first — unused includes, dead functions, commented-out blocks, debug logging — and keep that cleanup separate from the functional change so both diffs review cleanly. (The user makes every commit.)

## Renames and Cross-Cutting Edits
The compiler catches C++ references, but not everything here is compiled. When renaming a function, type, or parameter ID, also check string literals, `plugins/shared/` and `plugins/shared-dpf/` (several plugins compile them), tests, build scripts, and per-format plugin metadata — one grep rarely catches everything.

## Private Tools Repo

Calibration and testing scripts are in `~/projects/dusk-audio-tools/` (private repo).
DuskVerb tuning/analysis scripts live in `~/projects/dusk-audio-tools/tools/duskverb/`
(tuner in `tools/duskverb/tuner/`); GrooveMind training in `training/groovemind/`. The
tuner scripts locate this plugins checkout via `DUSK_PLUGIN_REPO` (default
`~/projects/plugins`); see the tools repo README.

## Scientific Method Overrides (added 2026-07-06, from the Fable 5 DuskVerb session)

These behaviors produced the session's best results. Apply them to ALL diagnostic and DSP work:

1. **Measure before believing.** Any root-cause attribution found in memory, docs, comments, or
   handoffs is a HYPOTHESIS. Run the control experiment before building on it (a wrong THD
   attribution survived weeks until a byte-identical render with the suspect param zeroed
   falsified it in one minute).
2. **A suspiciously clean wrong number is an arithmetic bug, not a tuning problem.** Uniform
   -50%, exactly 2x, all-bands-identical error: grep for hardcoded divisors/counts before
   sweeping any parameter.
3. **One knob per iteration, fresh measurement between.** No claim without a fresh render.
   Never batch-tune conflicting gates blind.
4. **Negative results with root cause are deliverables.** "It didn't work because X, here is the
   evidence" beats a forced marginal win. Document dead ends where the next session will look.
5. **When two gates conflict, stop optimizing.** Ask what physical mechanism produces BOTH
   readings; the answer is usually a missing lever or a measurement artifact, not a trade-off.
6. **State the hypothesis and its falsifying experiment BEFORE editing** anything structural.
7. **Bit-null means byte-compared renders**, not "should be unaffected". Never-worse means
   re-measured baseline on the CURRENT build, not a remembered number.
8. **Agent briefs must carry: mandatory reading list, verification obligations, hard scope
   walls, report format.** One build/render lock holder at a time; file-disjoint agents may
   run parallel. Loose briefs produce slop.

NOTE: an old `plugins/DuskVerb/tests/reference_comparison/` symlink no longer exists;
tuning scripts now live in the private tools repo at
`~/projects/dusk-audio-tools/tools/duskverb/tuner/` (see the tools repo README).

NOTE: the DuskVerb tuner move is DONE (2026-08-07). All 64 scripts now live in
the private tools repo at `~/projects/dusk-audio-tools/tools/duskverb/tuner/`
(the canonical path named above) and are gone from this repo. The previous note here claimed "59 of the 64 are already mirrored in the
private repo" with only `stereo_jnd_audit.py` / `stereo_profile_fit.py`
outstanding -- that was WRONG. The tools repo contained zero DuskVerb tuner
scripts; its 65 Python files were all TapeEcho and TapeMachine. Verify a mirror
exists before deleting anything on the strength of a note like that.

STILL IN THIS REPO and NOT moved, deliberately:
- `tests/duskverb_render/` is a CMake target (`add_subdirectory` in the root
  CMakeLists) and the comparison harness runs the binary it builds, so moving it
  is a build refactor, not a file move.
- `plugins/sunset-circuits/dpf-plugin/tools/` (`gen_params.py`, `lv2_smoke.c`) is
  build-adjacent codegen for that plugin.
- `.github/scripts/dpf_clap_validate.py` and `manuals/build_manuals.py` /
  `preflight.py` belong with the CI and manual pipelines they serve.
