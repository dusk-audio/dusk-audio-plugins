# A800 comparison harness — TapeMachine vs UAD Studer A800

Measures TapeMachine's **A800 mode** (`Swiss800`, machine index 0) against the
**UAD/UADx Studer A800** across four dimensions — frequency response, THD /
saturation, wow & flutter, and noise floor — by rendering identical stimuli
through both plugins and analysing the outputs.

## Requirements
- The generic JUCE plugin host must be built:
  ```
  cmake -B build -DBUILD_DUSKVERB_RENDER=ON && cmake --build build --target duskverb_render
  ```
- Both plugins installed as AU:
  - `~/Library/Audio/Plug-Ins/Components/tape_machine_2.component`
  - `/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component`
- **UADx needs the PACE licensing daemon running.** If a render comes back as a
  flat Dirac impulse / pristine sine (no coloration), UADx is bypassing because
  PACE is down (`auth.pace_not_running`). Start it:
  ```
  sudo launchctl bootstrap system /Library/LaunchDaemons/com.paceap.eden.licensed.plist
  sudo launchctl kickstart -k system/com.paceap.eden.licensed
  ```
- Python: `numpy scipy soundfile matplotlib`.

## Run
```
python3 gen_stimuli.py        # -> stimuli/*.wav
./render_ab.sh                # -> renders/{tapemachine,uad}/{15,30}/*.wav
python3 compare_a800.py       # -> report/comparison_<date>.md + PNGs
```
`render_ab.sh 15` renders a single speed. `wow_flutter.py <wav> --json` runs the
W&F measurement standalone.

## Matched operating point
Studer A800 / tape 456 / NAB / Repro / +6 dB cal / unity in-out, 48 kHz,
TapeMachine at 4× oversampling. 15 and 30 IPS passes. Frequency-response curves
are level-matched to 0 dB across 300 Hz–3 kHz before comparison.

## Method notes / caveats
- **Frequency response** and **THD-vs-level** are the trustworthy, precise
  results — clean and physically consistent.
- **Wow & flutter**: headline is a *robust (MAD-based)* pitch deviation from
  Hilbert FM-demodulation, validated to track TapeMachine's Wow knob
  monotonically. The wow/flutter band-split and modulation spectrum are
  **indicative only** — the demod of the oversampled/saturated tape tone throws
  frequent glitches (reported as the `p1–99 span` / `glitch fraction`), so an
  ordinary RMS or band-split is outlier-dominated. See `wow_flutter.py`.
- **Noise floor** uses a silence-in render with tape noise enabled. It is *not*
  level-matched (each at nominal noise setting); compare spectral character.
  Note TapeMachine's noise is **signal-dependent** (modulation noise) with
  essentially no idle hiss, whereas the UAD models constant idle hiss+hum — so
  the silence-in "hiss" pass understates TapeMachine's in-program noise.

## DPF param-setting gotchas (duskverb_render)
- TapeMachine (DPF) choice params take the **integer index** as the value
  (`--param "Tape Machine=0"`); float params interpret the value as
  **normalised 0–1** (`--param "Input Gain=0"` sets −12 dB!). Leave gains at
  their unity defaults; set Wow/Flutter/Noise Amount by raw value (min-anchored).
- UAD params take **display labels** (`--param "IPS=30 IPS"`). Its
  `--dump-params` decodes some fields inconsistently — trust the audio, not the
  dump.

Generated `stimuli/`, `renders/`, and `report/` are git-ignored.
