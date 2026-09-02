---
name: "source-command-build-plugin"
description: "Migrated source command `build-plugin`"
---

# source-command-build-plugin

Use this skill when the user asks to run the migrated source command `build-plugin`.

## Command Template

# Build Plugin

Build Dusk Audio plugins using Docker containerized builds for consistent, distributable binaries.

## Usage

```
/build-plugin [plugin-name]
```

**Arguments:**
- `plugin-name` (optional): Plugin shortcut or slug. If not provided, shows available options.

**Examples:**
- `/build-plugin multiq` - Build Multi-Q plugin
- `/build-plugin 4keq` - Build 4K EQ plugin
- `/build-plugin` - Show available plugins and let user choose

## Instructions

### 1. Determine What to Build

If a plugin name was provided, map it to the build shortcut:

| Plugin Name | Shortcuts | Full Slug |
|-------------|-----------|-----------|
| 4K EQ | `4keq`, `4k-eq` | 4k-eq |
| Multi-Comp | `compressor`, `multi-comp`, `multicomp` | multi-comp |
| TapeMachine | `tape`, `tapemachine` | tapemachine |
| Tape Echo | `tapeecho`, `tape-echo` | tape-echo |
| Multi-Q | `multiq`, `multi-q` | multi-q |
| Convolution Reverb | `convolution`, `convolution-reverb` | convolution-reverb |
| Chord Analyzer | `chord`, `chord-analyzer`, `chordanalyzer` | chord-analyzer |
| Spectrum Analyzer | `spectrum`, `spectrum-analyzer`, `spectrumanalyzer` | spectrum-analyzer |
| DuskVerb | `duskverb`, `dusk-verb` | duskverb |
| DuskAmp | `duskamp`, `dusk-amp`, `amp` | duskamp |
| Sunset Circuits | `sunset`, `sunset-circuits`, `sc` | sunset-circuits |
| GrooveMind | `groovemind` | groovemind |

If no plugin provided, ask the user:
```
Which plugin would you like to build?
- 4keq (4K EQ)
- compressor (Multi-Comp)
- tape (TapeMachine)
- tapeecho (Tape Echo)
- multiq (Multi-Q)
- convolution (Convolution Reverb)
- chord (Chord Analyzer)
- spectrum (Spectrum Analyzer)
- duskverb (DuskVerb)
- duskamp (DuskAmp)
- groovemind (GrooveMind)
- sunset (Sunset Circuits)
- all (Build all plugins)
```

### 2. Check Build Environment

Verify Docker/Podman is available:
```bash
which docker || which podman
```

If neither is available, inform the user they need to install Docker or Podman.

### 3. Run the Build

**Single plugin:**
```bash
./docker/build_release.sh <shortcut>
```

**All JUCE plugins:**
```bash
./docker/build_release.sh
```

The no-argument build does not include Sunset Circuits, which is a standalone
DAF build outside the JUCE graph. For `all`, run the no-argument build first and
then build Sunset separately:

```bash
JUCE_BUILT=(
  "4K EQ" "Multi-Comp" "TapeMachine" "GrooveMind"
  "Convolution Reverb" "Multi-Q" "Tape Echo" "Chord Analyzer"
  "Spectrum Analyzer" "DuskVerb" "DuskAmp"
)

./docker/build_release.sh || { echo "JUCE all-build failed"; exit 1; }
BUILT=("${JUCE_BUILT[@]}")

BUILD_FAILED=()
if ./docker/build_release.sh sunset; then
  BUILT+=("Sunset Circuits")
else
  BUILD_FAILED+=("Sunset Circuits")
fi
```

The fixed JUCE list is valid here because the all-build is a single CMake graph:
an exit status of zero proves every default-enabled target completed in this
run. Keep it synchronized with the root CMake options. Do not include Harmonic
Generator while its source directory is absent and its option is disabled.

Monitor the build output. The script will:
1. Pull/build the Docker image if needed
2. Compile the plugin(s)
3. Output to `release/` directory

### 4. Validate the Build (Automatic)

After a successful build, run pluginval.

**Single plugin:**
```bash
./tests/run_plugin_tests.sh --plugin "<Plugin Name>" --skip-audio
```

`--skip-audio` makes this a **pluginval-only** check. For most plugins that is
the intended quick gate. **Sunset Circuits is the exception:** `--skip-audio`
also skips its offline DAF gate suite, which is the authoritative DSP check for
that plugin, leaving only pluginval. Run it without the flag so the gates
actually execute (several minutes):

```bash
./tests/run_plugin_tests.sh --plugin "Sunset Circuits"
```

If you deliberately want the fast path for Sunset Circuits, say so explicitly in
the report ("pluginval only, DSP gates skipped") rather than reporting it as a
plain pass.

**All plugins:** there is no single
`<Plugin Name>` to pass. Run pluginval once PER successfully built plugin and
report each result separately; a single invocation would silently validate one
plugin and leave the rest unchecked:

Validate ONLY the `BUILT` list populated by the build block above:

```bash
[ ${#BUILT[@]} -gt 0 ] || { echo "nothing built; skipping validation"; exit 1; }

FAILED=()
for name in "${BUILT[@]}"; do
  echo "== $name =="
  # Sunset Circuits needs its gate suite, so no --skip-audio for that one.
  if [ "$name" = "Sunset Circuits" ]; then
    ./tests/run_plugin_tests.sh --plugin "$name" || FAILED+=("$name")
  else
    ./tests/run_plugin_tests.sh --plugin "$name" --skip-audio || FAILED+=("$name")
  fi
done

echo "validated ${#BUILT[@]}, failed ${#FAILED[@]}: ${FAILED[*]:-none}"
[ ${#BUILD_FAILED[@]} -eq 0 ] && [ ${#FAILED[@]} -eq 0 ] \
  || { echo "release build/validation failed"; exit 1; }
```

Report the pass/fail verdict for each plugin separately rather than a single
combined status, and name any plugin that was skipped because its build failed.

Plugin name mapping for validation. Every shortcut advertised in step 1 has a
row here; keep the two tables in sync, because a shortcut that builds but has no
validation name silently skips validation:

| Shortcut | Plugin Name for Validation |
|----------|---------------------------|
| 4keq | "4K EQ" |
| compressor | "Multi-Comp" |
| tape | "TapeMachine" |
| tapeecho | "Tape Echo" |
| multiq | "Multi-Q" |
| convolution | "Convolution Reverb" |
| chord | "Chord Analyzer" |
| spectrum | "Spectrum Analyzer" |
| duskverb | "DuskVerb" |
| duskamp | "DuskAmp" |
| sunset | "Sunset Circuits" |
| groovemind | "GrooveMind" |

`run_plugin_tests.sh --plugin` resolves a plugin by its BUNDLE name, so these
must match `get_plugin_name()` in `docker/build_release.sh` exactly.
"Sunset Circuits" is special-cased inside the test script (it also accepts the
slug `sunset-circuits`) and runs its own offline gate suite plus pluginval at
strictness 8, which takes several minutes.

### 5. Report Results

**If build succeeds:**
```
Build completed successfully!

Plugin: <Plugin Name>
Output: release/<plugin-slug>/
  - VST3: <plugin>.vst3
  - LV2: <plugin>.lv2

Validation: PASSED (pluginval)
```

**If build fails:**
1. Show the error output
2. Suggest common fixes:
   - Missing include: Check file paths
   - Undefined reference: Add source to CMakeLists.txt
   - Compiler error: Check syntax in recent changes

**If validation fails:**
```
Build completed but validation FAILED.

The plugin compiled but pluginval found issues:
<error output>

This should be fixed before release.
```

## Build Output Location

All builds output to `release/` directory:
```
release/
├── <plugin-slug>/
│   ├── VST3/
│   │   └── <Plugin>.vst3/
│   └── LV2/
│       └── <Plugin>.lv2/
```

## Local Development Alternative

For quick local testing (macOS/Linux only, not for releases):
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target <PluginTarget>_All -j8
```

Build targets:
- `FourKEQ_All`, `MultiComp_All`, `TapeMachine_All`, `TapeEcho_All`
- `MultiQ_All`, `ConvolutionReverb_All`, `ChordAnalyzer_All`, `SpectrumAnalyzer_All`
- `DuskVerb_All`, `DuskAmp_All`, `GrooveMind_All`

Sunset Circuits is configured from `plugins/sunset-circuits/daf-plugin` and does
not have a target in the root JUCE graph.

**Note:** Local builds may not be compatible across Linux distributions due to glibc version differences. Use Docker builds for releases.
