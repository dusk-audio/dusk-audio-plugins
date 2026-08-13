# 4K EQ 2

4K EQ 2 is Dusk Audio's DPF-based British console equalizer. It models the EQ
and filter section of a classic E-series channel strip, including the distinct
Brown and Black circuit revisions, their control laws, band interactions,
native nonlinear residue, and overload headroom.

The DPF build has its own plugin identity, so it can coexist with the original
JUCE 4K-EQ in existing sessions.

## Highlights

- Four-band console EQ with LF, LMF, HMF, and HF sections
- Separately calibrated Brown and Black frequency, gain, Q, shelf, and filter laws
- Constant-Q Brown mid bands and proportional-Q Black mid bands
- LF and HF shelf/bell switching
- Stepped high-pass and low-pass filters with mode-specific responses
- Shared-stage interaction modeling for the LF/LMF and HMF/HF circuit pairs
- Fixed native console nonlinearity driven by the Input control
- Calibrated mode-specific overload rails
- Selectable 1x, 2x, and 4x processing, with safe high-sample-rate limits
- Input and output metering plus switchable pre/post FFT display
- Optional auto-gain compensation
- 14 factory presets and a versioned user-preset library
- Resizable interface and full host automation

4K EQ 2 intentionally has no independent Drive or M/S control. Hidden legacy
parameter slots remain only to preserve automation and session compatibility
with earlier 2.x builds.

## Controls

| Section | Controls |
| --- | --- |
| Filters | HPF: Out or 16 to 350 Hz; LPF: Out or 15.201 to 3 kHz |
| LF | Gain: +/-15 dB; frequency: 30 to 450 Hz; shelf/bell |
| LMF | Gain: +/-15 dB; frequency: 200 Hz to 2.5 kHz; Q: 0.5 to 3.0 |
| HMF | Gain: +/-15 dB; frequency: 600 Hz to 7 kHz; Q: 0.5 to 3.0 |
| HF | Gain: +/-15 dB; frequency: 1.5 to 16 kHz; shelf/bell |
| Master | Input: +/-12 dB; Output: +/-12 dB; Bypass; Auto Gain |
| Header | Presets, oversampling, Brown/Black, graph, FFT, and pre/post analyzer source |

The printed frequency legends reproduce the stepped console dials. Hover,
drag, and typed-entry readouts report the calibrated audible frequency, so the
display agrees with the response graph and FFT in both modes.

## Factory Presets

`INIT` restores the flat default. Factory-preset frequencies below are audible
targets rather than internal control coordinates.

1. **Vocal Presence**: HPF 80 Hz; LF +3 dB at 100 Hz, LMF -3 dB at 300 Hz, HMF +4 dB at 3.5 kHz, HF +2 dB at 8 kHz
2. **Kick Punch**: HPF 30 Hz; LF +4 dB at 50 Hz, LMF -2.5 dB at 200 Hz, HMF +3 dB at 2 kHz
3. **Snare Crack**: HPF 150 Hz; LMF +4 dB at 250 Hz, HMF +5 dB at 5 kHz, HF +3 dB bell at 8 kHz
4. **Drum Bus Punch**: Black mode; LF +4 dB at 70 Hz, LMF -3 dB at 350 Hz, HMF +3 dB at 3.5 kHz, HF +2.5 dB at 10 kHz
5. **Bass Warmth**: LPF 10 kHz; LF +4 dB at 80 Hz, LMF -3 dB at 400 Hz, HMF +2 dB at 1.5 kHz
6. **Bass Guitar Polish**: HPF 35 Hz; LF +5 dB at 60 Hz, LMF -2 dB at 250 Hz, HMF +3 dB at 1.2 kHz, HF +2 dB bell at 4.5 kHz
7. **Acoustic Guitar**: HPF 80 Hz; LF -2 dB at 100 Hz, LMF +2 dB at 200 Hz, HMF +3 dB at 2.5 kHz, HF +4 dB at 12 kHz
8. **Piano Brilliance**: HPF 30 Hz; LF +2 dB at 80 Hz, LMF -2.5 dB at 500 Hz, HMF +3 dB at 2 kHz, HF +3.5 dB at 8 kHz
9. **Bright Mix**: LF +2 dB at 60 Hz, HMF -2 dB at 2.5 kHz, HF +2.5 dB at 10 kHz
10. **Glue Bus**: LF +2 dB at 100 Hz, HMF -1.5 dB at 3 kHz, HF +2 dB at 10 kHz
11. **Telephone EQ**: HPF 300 Hz, LPF 3 kHz, LMF +6 dB at 1 kHz
12. **Air & Silk**: HMF +3 dB at 7 kHz, HF +4 dB at 15 kHz
13. **Master Sheen**: HMF +1 dB at 5 kHz, HF +1.5 dB at 16 kHz
14. **Master Bus Sweetening**: LF +1 dB at 50 Hz, LMF -1 dB at 600 Hz, HMF +0.5 dB at 4 kHz, HF +1.5 dB at 15 kHz, -0.5 dB output

All factory presets disable Auto Gain on recall. Oversampling and analyzer
preferences are machine-level settings and are not changed by preset recall.

## Formats and Platforms

| Platform | Formats |
| --- | --- |
| macOS 10.15+ | AU, VST3, CLAP, LV2; universal arm64/x86_64 |
| Linux x86_64 and arm64 | VST3, CLAP, LV2 |
| Windows x86_64 | VST3, CLAP |

4K EQ 2 does not ship a Standalone build.

## Build

DPF and DPF-Widgets are expected beside this repository by default. Their
locations can be overridden at configure time.

```sh
cmake -S plugins/4k-eq/dpf-plugin \
  -B plugins/4k-eq/dpf-plugin/build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDPF_PATH=/path/to/DPF \
  -DDPFWIDGETS_PATH=/path/to/DPF-Widgets

cmake --build plugins/4k-eq/dpf-plugin/build --target \
  four_k_eq_2-vst3 four_k_eq_2-clap four_k_eq_2-lv2
```

The target list above is for Linux. On Windows, build only `four_k_eq_2-vst3`
and `four_k_eq_2-clap`. On macOS, use the Linux list and add
`four_k_eq_2-au`. Local installation after building is enabled by default;
configure with `-DDUSK_DPF_INSTALL_LOCAL=OFF` for packaging or CI.

## Installation

### Linux

```text
VST3: ~/.vst3/four_k_eq_2.vst3
CLAP: ~/.clap/four_k_eq_2.clap
LV2:  ~/.lv2/four_k_eq_2.lv2
```

### macOS

```text
AU:   ~/Library/Audio/Plug-Ins/Components/four_k_eq_2.component
VST3: ~/Library/Audio/Plug-Ins/VST3/four_k_eq_2.vst3
CLAP: ~/Library/Audio/Plug-Ins/CLAP/four_k_eq_2.clap
LV2:  ~/Library/Audio/Plug-Ins/LV2/four_k_eq_2.lv2
```

### Windows

```text
VST3: C:\Program Files\Common Files\VST3\four_k_eq_2.vst3
CLAP: C:\Program Files\Common Files\CLAP\four_k_eq_2.clap
```

## Validation

Release CI builds every supported platform and validates VST3, CLAP, LV2, and
AU metadata and loading. The macOS AU must also pass:

```sh
auval -v aufx DsFq Dusk
```

Calibration tools and proprietary reference captures are maintained in the
separate `dusk-audio-tools` repository.

## Licensing

4K EQ 2 is GPL-3.0-or-later. See the repository `LICENSE` and
[`plugins/shared-dpf/THIRD_PARTY_LICENSES.md`](../shared-dpf/THIRD_PARTY_LICENSES.md)
for DPF, DPF-Widgets, and per-format notices.

This is an independent emulation inspired by classic British console EQs. It
is not affiliated with or endorsed by any hardware or software manufacturer.
