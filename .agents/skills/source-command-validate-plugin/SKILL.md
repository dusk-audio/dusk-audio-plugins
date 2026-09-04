---
name: "source-command-validate-plugin"
description: "Migrated source command `validate-plugin`"
---

# source-command-validate-plugin

Use this skill when the user asks to run the migrated source command `validate-plugin`.

## Command Template

# Validate Plugin

Run validation tests on Dusk Audio plugins using pluginval and optional audio analysis.

## Usage

```
/validate-plugin <plugin-name> [--full]
```

**Arguments:**
- `plugin-name`: Plugin name or shortcut (e.g., `multi-q`, `multiq`, `"Multi-Q"`)
- `--full` (optional): Run full audio analysis in addition to pluginval

**Examples:**
- `/validate-plugin multiq` - Quick validation with pluginval
- `/validate-plugin "4K EQ" --full` - Full validation including audio tests

## Instructions

### 1. Determine Plugin Name

Map the input to the correct plugin name for validation:

| Input | Plugin Name |
|-------|-------------|
| `4keq`, `4k-eq`, `"4K EQ"` | "4K EQ" |
| `compressor`, `multi-comp`, `"Multi-Comp"` | "Multi-Comp" |
| `tape`, `tapemachine`, `"TapeMachine"` | "TapeMachine" |
| `tapeecho`, `tape-echo`, `"Tape Echo"` | "Tape Echo" |
| `multiq`, `multi-q`, `"Multi-Q"` | "Multi-Q" |
| `convolution`, `"Convolution Reverb"` | "Convolution Reverb" |
| `chord`, `chord-analyzer`, `"Chord Analyzer"` | "Chord Analyzer" |
| `spectrum`, `spectrum-analyzer`, `spectrumanalyzer`, `"Spectrum Analyzer"` | "Spectrum Analyzer" |
| `duskverb`, `"DuskVerb"` | "DuskVerb" |
| `duskamp`, `dusk-amp`, `amp`, `"DuskAmp"` | "DuskAmp" |
| `sunset`, `sunset-circuits`, `sc`, `"Sunset Circuits"` | "Sunset Circuits" |
| `groovemind`, `groove`, `"GrooveMind"` | "GrooveMind" |

Every plugin that `/build-plugin` can build has a row here; keep the two in sync
so a buildable plugin is never unvalidatable. The right-hand value must match
`get_plugin_name()` in `docker/build_release.sh`, because
`run_plugin_tests.sh --plugin` resolves the bundle by that name.

**Sunset Circuits is not a generic JUCE plugin.** It is the DAF port: the test
script special-cases it (the slug `sunset-circuits` is accepted too), skips the
shared audio analyzer, and instead runs its own offline gate suite at
`plugins/sunset-circuits/core/tests/run_all.sh` plus pluginval at strictness 8
with `--skip-gui-tests`. The gate suite takes several minutes and is what
`--skip-audio` skips for this plugin.

### 2. Quick Validation (Default)

Run pluginval without audio analysis for JUCE plugins:

```bash
./tests/run_plugin_tests.sh --plugin "<Plugin Name>" --skip-audio
```

For Sunset Circuits, omit `--skip-audio` even on the default path. Its
plugin-specific offline DAF gates are the authoritative DSP check, and that flag
would skip them:

```bash
./tests/run_plugin_tests.sh --plugin "Sunset Circuits"
```

This validates:
- Plugin loads correctly
- Parameters work properly
- State save/restore functions
- No crashes during basic operations

### 3. Full Validation (with --full flag)

Run complete test suite including audio analysis:

```bash
./tests/run_plugin_tests.sh --plugin "<Plugin Name>"
```

Additional tests:
- Audio processing validation
- THD measurement
- Frequency response
- Latency measurement

### 4. Interpret Results

**Pluginval strictness levels:**
- Level 1-4: Basic functionality
- Level 5-7: Parameter handling
- Level 8-10: Edge cases and stress tests

**Pass criteria:**
- All levels 1-5 must pass
- Levels 6-10 warnings are acceptable
- Any crash = FAIL

### 5. Report Results

**If validation passes:**
```
Validation PASSED for <Plugin Name>

pluginval: All tests passed
Audio tests: [Passed / Skipped]

The plugin is ready for release.
```

**If validation fails:**
```
Validation FAILED for <Plugin Name>

Failed tests:
<list of failures>

Common fixes:
- Parameter out of range: Check parameter normalization
- State restore failed: Verify getStateInformation/setStateInformation
- Crash on close: Check timer/listener cleanup in destructor
```

## Manual Validation

For debugging, run pluginval directly:

```bash
# Find the plugin
ls ~/.vst3/  # or release/<plugin>/VST3/

# Run pluginval manually
pluginval --validate "<path-to-plugin>.vst3" --strictness-level 5
```

## Audio Analysis (Advanced)

For detailed audio analysis similar to PluginDoctor:

```bash
# Generate test signals
python3 tests/audio_analyzer.py --plugin "<Plugin Name>"

# After processing through DAW, analyze results
python3 tests/audio_analyzer.py --plugin "<Plugin Name>" --analyze
```

## Test Thresholds

| Test | Pass Threshold |
|------|----------------|
| THD @ 1kHz | < 1.0% |
| Noise Floor | < -80 dB |
| Bypass Null | < -100 dB |
| DC Offset | < 0.001 |
