# TapeMachine 2

TapeMachine 2 is Dusk Audio's DPF-based tape processor, modeled against the
UAD Studer A800 and Ampex ATR-102. It replaces the JUCE TapeMachine 1.x line
with a distinct plugin identity, so both versions can coexist in old sessions.

## Highlights

- **Swiss 800** tracking/mix deck modeled against the Studer A800
- **Classic 102** mastering deck modeled against the Ampex ATR-102
- 7.5, 15, and 30 IPS on both decks; 3.75 IPS on Classic 102
- Type 456, GP9, 900, and 250 tape formulations
- Repro, Sync, Input, and Thru signal paths
- NAB and CCIR equalization
- Input drive with linked output compensation
- Manual or automatic bias, four calibration levels, wow, flutter, tape noise,
  high-pass, and low-pass filtering
- Classic 102 head-width, crosstalk, wow/flutter-enable, and transformer controls
- Advanced four-band reproduce-head EQ
- 20 factory presets fitted to matching UAD factory presets
- User preset save/load support

The nonlinear core is permanently tuned at 2× oversampling. A hidden legacy
oversampling parameter remains only so older state layouts round-trip safely;
it is not a user-facing quality control.

## Factory presets

Classic 102:

- Big 456 Master
- Nice 456 Master
- Jazz Vision Master
- Clean 900 Master
- Fat 456 Master
- GP9 Drum Bus
- Massive Bass
- Bright & Sizzly
- Sunbaked Cassette
- Analog Warmth

Swiss 800:

- Classic Rock Crisp
- Modern Rock
- Drum Bus
- Hi-Fi Shine
- Lush Film
- Jazz Warmth
- Thick Saturation
- Hip-Hop Punch
- Vocal Presence
- Old Tape

## Formats and platforms

| Platform | Formats |
| --- | --- |
| macOS 10.15+ | AU, VST3, CLAP, LV2; universal arm64/x86_64 |
| Linux x86_64 | VST3, CLAP, LV2 |

TapeMachine 2 does not currently ship a Windows or Standalone build.

## Build

DPF and DPF-Widgets are expected beside this repository by default. Override
their paths when needed.

```sh
cmake -S plugins/TapeMachine/dpf-plugin \
  -B plugins/TapeMachine/dpf-plugin/build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDPF_PATH=/path/to/DPF \
  -DDPFWIDGETS_PATH=/path/to/DPF-Widgets

cmake --build plugins/TapeMachine/dpf-plugin/build --target \
  tape_machine_2-vst3 tape_machine_2-clap tape_machine_2-lv2
```

On macOS, add `tape_machine_2-au`. Local installation after building is enabled
by default; configure with `-DDUSK_DPF_INSTALL_LOCAL=OFF` for packaging or CI.

## Validation

The macOS AU must pass:

```sh
auval -v aufx DsTM Dusk
```

The proprietary UAD comparison harness is documented in
[`tests/a800_comparison/README.md`](tests/a800_comparison/README.md). It validates
frequency response, THD, and aliasing for all 20 matching factory presets.

## Licensing

TapeMachine 2 is GPL-3.0-or-later. See the repository `LICENSE` and
[`plugins/shared-dpf/THIRD_PARTY_LICENSES.md`](../shared-dpf/THIRD_PARTY_LICENSES.md)
for DPF, DPF-Widgets, and per-format notices.
