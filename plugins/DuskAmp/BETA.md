# DuskAmp 0.0.1 — Alpha

Thanks for helping test DuskAmp. This is an **early alpha** — the first
public build, expect rough edges. This document captures what works, what
doesn't, and how to send feedback.

## Status

- **Version:** 0.0.1 (alpha — first release)
- **DSP foundation:** working — 3 amps (American/British/AC), oversampled
  preamp + tone stack + power amp + cabinet IR + delay + spring reverb,
  plus optional NAM hybrid mode.
- **Voicing:** amp models are in active calibration against real-amp
  captures. American (Fender) is close; British/AC have more presence work
  pending. Treat the DSP voicings as a strong starting point, not final.
- **Expected rough edges:** factory preset levels not finally loudness-
  matched; bundled cab IRs are darker than some reference captures (load
  your own IR to taste); no in-editor A/B yet.

## UI (0.0.1 alpha)

DuskAmp's editor is a 1024 × 600 base panel (scalable 70–200 %) built from three regions:

- **Header strip** — DUSKAMP wordmark + accent rule (top-left), preset
  combo + Save / Delete + dirty-state asterisk (centre), A · B snapshot
  pills + TUNER + ×8 oversampling chips (top-right). A/B is session-
  scoped — left-click recalls (captures on first click if empty); right-
  click snapshots the current state into that slot.
- **DSP / NAM tab strip** — centred narrow pill (~280 px) below the
  header, switches the amp engine between the built-in DSP path and a
  user-loaded NAM profile. The selected tab fills solid orange.
- **BROWSERS sidebar** (left, ~210 px wide) — `CABINET IRs` list always
  shown; `AMP MODELS` list appears above it only when NAM mode is
  active. Long file names middle-ellipsize (`Rocksta Reactions Fend...3 45`).
- **Card grid** (right) — three rows of equal-width, edge-touching
  metallic panels:

  | Row | Card 1 | Card 2 | Card 3 |
  |-----|---|---|---|
  | 1   | **AMP** (DSP) / **INPUT** (NAM): IN · GATE · RELEASE · GAIN · Channel · Bright | **TONE**: BASS · MID · TREBLE · ToneType | — |
  | 2   | **POWER**: DRIVE · PRESENCE · RESONANCE · SAG | **CABINET**: CABINET / NORM IR toggles · MIX · HI CUT · LO CUT | — |
  | 3   | **DELAY**: DELAY toggle · TIME · FEEDBACK · MIX | **REVERB**: REVERB toggle · MIX · DECAY | **OUTPUT**: IN-meter · OUTPUT knob · OUT-meter |

  Each card has a tracked uppercase title at top-left with a 1 px
  accent rule underneath. DSP-only controls (GAIN / Channel / Bright)
  hide AND disable when NAM mode is selected, so they can't be nudged
  by accident.

- **Footer strip** — plugin version (left), inline tooltip mirror /
  IR + NAM load status (centre), CPU placeholder (right). The mirror
  surfaces `loading: …` while a NAM model parses on the background
  worker, `FAIL: …` on a load error, and `warn: …` on a sample-rate
  mismatch (e.g. running a 48 kHz NAM model through a 96 kHz host).

- **Misc polish** — DRIVE knob reads `OFF` at zero; effects-section
  toggle buttons use a pointing-hand cursor with tooltip; preset
  combo lights a `*` when the live state diverges from the loaded
  preset; TUNER button turns solid orange while the tuner overlay is
  open. The tuner overlay has an A4 reference editor (415–466 Hz,
  default 440) at the bottom — adjust for 432 Hz or baroque 415; the
  value persists in host session state.

## Bundled cabinet IRs

DuskAmp ships with three cabinet impulse responses bundled in the plugin
binary, one auto-loaded per amp model when you change TONE STACK:

  American (Fender)   Twin Reverb 1x12 + SM57    (Rocksta Reactions)
  British (Marshall)  JCM800 Lead 4x12 + NT1-A   (Rocksta Reactions)
  AC (Vox)            AC30 2x12 + Celestion Blue (Vulterized)

To use your own IR instead, click **CABINET → Browse** and point at any
44.1/48 kHz mono `.wav`. The plugin remembers your override across
amp-model switches until you reload a factory preset.

## Install

DuskAmp ships **unsigned** in this alpha. Your OS will warn you; the
bypass steps below are safe — you're approving software you just
downloaded from a known location.

### macOS (AU + VST3)

1. Unzip the macOS artifact.
2. Copy `DuskAmp.component` → `~/Library/Audio/Plug-Ins/Components/`
3. Copy `DuskAmp.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
4. Strip the Gatekeeper quarantine flag:
   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/DuskAmp.component
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/DuskAmp.vst3
   ```
5. Rescan plugins in Logic Pro / Ableton / Reaper / etc.

### Windows (VST3)

1. Unzip the Windows artifact.
2. Copy `DuskAmp.vst3` → `C:\Program Files\Common Files\VST3\`
3. SmartScreen may block first launch — click "More info" → "Run anyway".
4. Rescan plugins in your DAW.

### Linux (VST3 + LV2)

1. Unzip the linux or linux-arm64 artifact (match your CPU).
2. Copy `DuskAmp.vst3` → `~/.vst3/`
3. Copy `DuskAmp.lv2` → `~/.lv2/`
4. Restart your DAW.

Glibc compatibility: x86_64 build requires glibc ≥ 2.31 (Ubuntu 20.04+);
arm64 build requires glibc ≥ 2.35 (Ubuntu 22.04+).

## NAM hybrid mode

DuskAmp can run as a pure DSP amp or as a NAM-driven amp inside the same
plugin chain. Toggle in the **MODE** selector at the top of the editor.

- NAM is **enabled** in this alpha build.
- The plugin does **not** ship NAM model files; load your own `.nam`
  profile via the NAM browser panel.
- NAM profile sources: see [tonehunt.org](https://tonehunt.org/) or any
  Steven Atkinson-format `.nam` file.

If NAM mode is selected without a loaded model, the plugin falls back to
DSP path with no audio dropout.

**Loudness:** profiles that carry loudness metadata (most modern captures)
are normalized to the NAM-standard −18 dB reference, so they sit at a
consistent level and match other NAM hosts. A minority of older profiles
ship without loudness metadata — those pass through at their raw captured
level (often quieter), so trim the NAM-mode OUTPUT knob to taste.

## Known issues / non-blockers

- Factory presets (3 amps × 3 channels = 9) are loudness-matched on the
  distorted tier (Crunch + Lead) to within ±1 dB integrated RMS against a
  reference DI. The Clean tier sits 1.5–5 dB below the distorted median
  because clean guitar transients are peak-bound — raising OUTPUT to close
  the gap would clip. If you want Cleans to sit at distorted level, push
  the host fader on a clean track.
- A/B comparison is session-only (state lives in memory; closing the
  plugin instance discards both slots). The offline `duskamp_di_render`
  tool also remains available for objective A/B against external bounces.
- No preset browser or signal-chain reordering. Both planned for 1.0+.
- macOS arm64 binary not separately optimized — the universal macOS
  build is fat (x86_64 + arm64).
- Tone-stack mid-knob behaved oddly in the α.4 internal builds; that bug
  is fixed in this alpha. Report any tone-stack regressions you hear.

## Reporting bugs

Please open issues at:

  **https://github.com/dusk-audio/dusk-audio-plugins/issues**

Useful info to include:

- Plugin format used (AU / VST3 / LV2 / Standalone)
- DAW + version (Logic 11.x / Ableton 12 / Reaper 7.x / …)
- OS + CPU (macOS 14 Sonoma M2 / Windows 11 x86_64 / …)
- Audio interface sample rate + buffer size
- Steps to reproduce
- If the issue is sonic: a short DI recording (mono wav) + a screenshot
  of your knob positions

For DSP-quality reports, the offline `duskamp_di_render` standalone in
`plugins/DuskAmp/tests/comparison/` can render the same DI through your
config so we can A/B against reference recordings.

## License

DuskAmp itself: GNU GPL v3.0 (see `LICENSE` in this archive).
Third-party components: see `THIRDPARTY-LICENSES.txt`.
Source: https://github.com/dusk-audio/dusk-audio-plugins
