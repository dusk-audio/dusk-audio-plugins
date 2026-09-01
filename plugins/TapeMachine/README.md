# TapeMachine 2

TapeMachine 2 is Dusk Audio's DAF-based tape processor with distinct tracking
and mastering deck models. It replaces the JUCE TapeMachine 1.x line with a
distinct plugin identity, so both versions can coexist in old sessions.

## Highlights

- **Swiss** tracking/mix deck with a clean, extended response
- **American** mastering deck with additional transport and electronics controls
- 7.5, 15, and 30 IPS on both decks; 3.75 IPS on American
- Type 456, GP9, 900, and 250 tape formulations
- Repro, Sync, Input, and Thru signal paths
- NAB and CCIR equalization
- Input drive with linked output compensation
- Manual or automatic bias, four calibration levels, wow, flutter, tape noise,
  high-pass, and low-pass filtering
- American head-width, crosstalk, wow/flutter-enable, and transformer controls
- Advanced four-band reproduce-head EQ
- 20 calibrated factory presets
- User preset save/load support

The nonlinear core is permanently tuned at 2× oversampling. A hidden legacy
oversampling parameter remains only so older state layouts round-trip safely;
it is not a user-facing quality control.

## Factory presets

American:

- Big 456 Master
- Nice 456 Master
- Jazz Vision Master
- Clean 900 Master
- Fat 456 Master
- GP9 Drum Bus
- Bass Bump
- Bright & Sizzly
- Sunbaked Cassette
- Analog Warmth

Swiss:

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

DAF and DAF-Widgets are expected beside this repository by default. Override
their paths when needed.

```sh
cmake -S plugins/TapeMachine/daf-plugin \
  -B plugins/TapeMachine/daf-plugin/build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDAF_PATH=/path/to/DAF \
  -DDAFWIDGETS_PATH=/path/to/DAF-Widgets

cmake --build plugins/TapeMachine/daf-plugin/build --target \
  tapemachine-2-vst3 tapemachine-2-clap tapemachine-2-lv2
```

On macOS, add `tapemachine-2-au`. Local installation after building is enabled
by default; configure with `-DDUSK_DAF_INSTALL_LOCAL=OFF` for packaging or CI.

## Validation

The macOS AU must pass:

```sh
auval -v aufx DsTM Dusk
```

Release validation covers frequency response, harmonic behavior, aliasing,
factory presets, supported formats, and Audio Unit host conformance. Proprietary
comparison tooling and calibration data are maintained outside this repository.

## Licensing

TapeMachine 2 is GPL-3.0-or-later. See the repository `LICENSE` and
[`plugins/shared-daf/THIRD_PARTY_LICENSES.md`](../shared-daf/THIRD_PARTY_LICENSES.md)
for DAF, DAF-Widgets, and per-format notices.
