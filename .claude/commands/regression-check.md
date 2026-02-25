# Regression Check

Check staged files (or the full codebase) for known bug patterns from closed GitHub issues.

## Usage

```
/regression-check [--all]
```

**Arguments:**
- `--all` (optional): Check the entire codebase, not just staged files

**Examples:**
- `/regression-check` - Check only staged files for known regressions
- `/regression-check --all` - Scan the entire codebase

## Instructions

Run the regression check script:

```bash
./tests/regression_check.sh $ARGUMENTS
```

### Interpret Results

The script checks for these known bug patterns:

| Check ID | Issue(s) | What It Detects |
|----------|----------|-----------------|
| `DEPRECATED_CONFIGURE_KNOB` | #4, #5, #12, #20 | Deprecated `configureKnob()` usage (causes knob jumping) |
| `MISSING_VST3_AUTO_MANIFEST` | #10, #11 | `juce_add_plugin()` without `VST3_AUTO_MANIFEST FALSE` |
| `LTO_FLAGS_ENABLED` | #10 | Uncommented `juce_recommended_lto_flags` (MSVC crashes) |
| `MULTI_TAG_PUSH` | #32 | `git push --tags` or multiple tags in one push |
| `MISSING_SCALABLE_EDITOR` | #9 | Editor class without `ScalableEditorHelper` |
| `JUCE_DRY_WET_MIXER` | #14, #21 | `juce::dsp::DryWetMixer` (causes comb filtering with IIR) |
| `PRESET_SYNC_MISSING` | #42 | Presets without editor polling `getCurrentProgram` |
| `SIDECHAIN_AUTO_GAIN` | #26 | Sidechain + auto-gain without disable logic |

**If regressions are found (exit 2):** Address each finding before committing. The output includes the file, line, and recommended fix.

**If warnings are found (exit 0):** Review each warning — they may be acceptable in context.

**If all checks pass (exit 0):** Code is clean of known regression patterns.

### Fix Reference

| Check | Fix |
|-------|-----|
| `DEPRECATED_CONFIGURE_KNOB` | Use `DuskSlider` from `shared/DuskLookAndFeel.h` |
| `MISSING_VST3_AUTO_MANIFEST` | Add `VST3_AUTO_MANIFEST FALSE` inside `juce_add_plugin()` |
| `LTO_FLAGS_ENABLED` | Comment out `juce::juce_recommended_lto_flags` |
| `MULTI_TAG_PUSH` | Push tags one at a time: `git push origin <tag>; sleep 2` |
| `MISSING_SCALABLE_EDITOR` | Add `ScalableEditorHelper` member and initialize in constructor |
| `JUCE_DRY_WET_MIXER` | Use `DuskAudio::DryWetMixer` from `shared/DryWetMixer.h` |
| `PRESET_SYNC_MISSING` | Poll `getCurrentProgram()` in `timerCallback`; call `updateHostDisplay()` on changes |
| `SIDECHAIN_AUTO_GAIN` | Guard: `bool autoMakeup = autoMakeupRaw && !extScEnabled;` |

### Adding New Checks

When a new bug is fixed, add a check function to `tests/regression_check.sh` following the existing pattern:
1. Add a comment block with the check ID and issue number
2. Implement a `check_<name>()` function using `fail()` or `warn()`
3. Call it from the "Run all checks" section
4. Update the table above
