# Chord Analyzer Headless

This native LV2 build uses the shared framework-free numeric analyzer. It retains
its shipping name, URI and all 19 port indices, so existing headless-host sessions
keep their parameter and output assignments. Its Linux x86_64 and ARM64 packages
are separate from Chord Analyzer 2 and Chord Analyzer MIDI 2.

Extract the archive and copy `LV2/Chord Analyzer Headless.lv2` into your host's
LV2 plugin directory (commonly `~/.lv2` on Linux). Keep all three files in the
bundle together, then rescan plugins in the host. Select **Chord Analyzer
Headless** and route MIDI into and through it. It has no audio ports or editor;
the host displays native output controls. Recording, history and MIDI-file
export belong to the desktop products.

The retained URI is
`https://dusk-audio.github.io/plugins/chord-analyzer-headless`.
The two MIDI atom ports preserve message payloads and frame offsets. Sustain is
tracked separately on each of the 16 MIDI channels. Changing key or suggestion
level refreshes suggestions while the held chord stays unchanged.

| Index | Symbol | Direction | Values |
| --- | --- | --- | --- |
| 0 | `midi_in` | Input | MIDI atom sequence |
| 1 | `midi_out` | Output | MIDI atom sequence |
| 2 | `key_root` | Input | 0–11: C–B |
| 3 | `key_mode` | Input | 0 Major, 1 Minor |
| 4 | `suggestion_level` | Input | 0 Basic, 1 Basic + Intermediate, 2 All |
| 5 | `show_inversions` | Input | 0 Off, 1 On |
| 6 | `detected_root` | Output | 0 empty, 1–12 C–B |
| 7 | `detected_quality` | Output | 0 empty, 1–33 chord qualities |
| 8 | `detected_bass` | Output | 0 empty, 1–12 C–B |
| 9 | `detected_inversion` | Output | 0 empty, 1 root, 2–7 ordinal inversions, 8 slash bass |
| 10 | `respect_sustain` | Input | 0 Off, 1 On |
| 11 / 12 | `suggestion_1_root` / `suggestion_1_quality` | Output | Root / quality |
| 13 / 14 | `suggestion_2_root` / `suggestion_2_quality` | Output | Root / quality |
| 15 / 16 | `suggestion_3_root` / `suggestion_3_quality` | Output | Root / quality |
| 17 / 18 | `suggestion_4_root` / `suggestion_4_quality` | Output | Root / quality |

Suggestion slots use the same root/quality scale points as detection outputs;
unused slots contain zero. The bundle metadata supplies every displayed choice.

To build from the repository root, install a C++17 toolchain, CMake and the public
LV2 SDK headers. Full tests additionally use Python 3, `lv2info` (lilv), `ldd` and
`readelf`. No JUCE, DAF, OpenGL, X server or audio-device library is required.

```sh
cmake -S plugins/chord-analyzer/lv2 -B build-chord-headless -DCMAKE_BUILD_TYPE=Release
cmake --build build-chord-headless -j8
ctest --test-dir build-chord-headless --output-on-failure
python3 plugins/chord-analyzer/lv2/package.py \
  'build-chord-headless/bin/Chord Analyzer Headless.lv2' chord-analyzer-headless-linux.zip
python3 plugins/chord-analyzer/tests/test_chord_headless_bundle.py \
  --archive chord-analyzer-headless-linux.zip
```

If headers are outside the compiler's usual include paths, set
`-DCHORD_ANALYZER_LV2_HEADERS=/path/to/include` (the directory containing
`lv2/core/lv2.h`). Building does not install into the host's plugin folders.
The desktop DAF build also builds and tests this retained LV2 on Linux, and its
instrument release workflow packages headless downloads for both architectures.

The plugin is distributed under GNU GPL v3.0 or later; see `LICENSE`. LV2 SDK
notices are included in `THIRD_PARTY_LICENSES.md`.
