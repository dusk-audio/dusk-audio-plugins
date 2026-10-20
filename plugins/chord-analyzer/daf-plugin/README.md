# Chord Analyzer DAF successors (in progress, #183)

Configure this directory directly; the shipping JUCE products and native
headless LV2 retain their existing identities and build paths.

Linux builds also need the public LV2 SDK headers for the retained headless
product. Its metadata/package checks use `lv2info` (lilv), `ldd` and `readelf`.

```sh
cmake -S plugins/chord-analyzer/daf-plugin -B build-chord-analyzer-daf \
  -DCMAKE_BUILD_TYPE=Release -DDUSK_DAF_INSTALL_LOCAL=OFF
cmake --build build-chord-analyzer-daf --config Release -j8
# Linux: run the real-editor tests on an isolated display.
xvfb-run -a ctest --test-dir build-chord-analyzer-daf -C Release --output-on-failure
# macOS / Windows:
ctest --test-dir build-chord-analyzer-daf -C Release --output-on-failure
```

Both successors build CLAP, VST3 and monolithic LV2, plus AU on macOS. They share
a Dear ImGui editor with embedded Barlow fonts and a fixed 4:3 resize contract.
The direct-access bridge supplies live chord names, Roman numerals, harmonic
functions, held-note keyboard/staff displays, suggestions, history and recording
state. Host gestures handle all five input controls; input callbacks preserve
local edits while the host acknowledges them and follow host automation.

History selection previews the original voicing with analysis/suggestions in the
current key; Return to live restores incoming notes. Recording survives closing
and reopening the editor. Chord and progression export capture their MIDI bytes
before opening a save dialog. Platforms without a native saving dialog use an
explicit-path form with replacement confirmation and cancellation. Completed
progressions use their recorded tempo, while single voicings retain the shipping
one-bar/120-BPM format. External file dragging still needs integration.

| Product | Artifact | Unique ID | CLAP ID |
| --- | --- | --- | --- |
| Chord Analyzer 2 | `chord-analyzer-2` | `DsCA` | `com.duskaudio.chord-analyzer-2` |
| Chord Analyzer MIDI 2 | `chord-analyzer-midi-2` | `DsCM` | `com.duskaudio.chord-analyzer-midi-2` |

LV2 URIs are `https://dusk-audio.github.io/plugins/` followed by the artifact
name. The instrument retains two audio inputs and silent stereo output, with
a mono configuration; the MIDI effect has no audio ports. Both forward MIDI
bytes and frame offsets unchanged. When host MIDI output fills, analysis still
consumes the remaining input and forwarding resumes on the next block.

Five writable controls keep the desktop symbols, ordering and snapped values.
Five transient detection outputs are read-only and excluded from saved host
parameters. Small choice domains retain their displayed names. The large
`detectedChord` domain uses bounded, allocation-free host text callbacks backed
by the shared formatter/parser. Fractional host probes retain the shipping
nearest-even rounding; malformed labels and insufficient buffers reject without
truncation or numeric fallback. Legacy labels and canonical inverses remain
unchanged throughout the full code domain. Host wrappers own parameter state;
there are no plugin-owned state strings or factory programs at this stage.

The shared `core/ChordMidiExport` library now builds the same format-1,
single-track 480-PPQ clips as the shipping writer. `chordBytes` and
`sessionBytes` accept the legacy and framework-free chord/event structs directly;
the JUCE editor uses a thin file/UUID adapter around these bytes. The standalone
`temporaryChordFile` / `temporarySessionFile` helpers reserve private per-export
directories and keep their files alive for asynchronous DAW imports. Explicit
file writes report failure and truncate replaced destinations. Invalid pitches,
computed timestamps and deltas outside SMF's four-byte VLQ domain reject the
whole clip; nonfinite host tempo falls back to 120 BPM. All of this work belongs
off the audio thread. Both editors use this writer for their MIDI save actions.

`ChordAnalyzerRuntime` owns fixed MIDI state and a 64-cell SPSC transition queue.
A background consumer runs at 20 Hz even with the editor closed. Its formatted
view, history and recorder share one non-audio mutex. Audio never takes that
mutex, allocates, or signals a condition variable. A coherent latest-frame
fallback retains the final transition when the 63 usable queue cells fill.
Longer stalls may coalesce intermediate chords. Consumption is bounded to the
queue boundary captured at the start of a service pass, preventing an offline
producer from holding the model mutex indefinitely.

Record/Stop/Clear and view/session access are non-audio operations. Stop drains
pending transitions before closing the final chord. Deactivation/repreparation
also ends recording against the old clock before clearing held notes, pedals,
cached frames and detection outputs. Completed sessions and history survive;
the elapsed clock and fallback host tempo reset on activation.

The shared DAF CI/release workflow now registers both desktop products, including
their different AU component types (`aumu` / `aumi`), native core/ABI tests and
Linux editor screenshots/MIDI diagnostics. On Linux it also builds/tests the
retained native headless LV2 against the public LV2 SDK, without a JUCE checkout.
Instrument-tag builds own separate headless x86_64/ARM64 archives with the shipping
URI and 19-port ABI; package validation loads the extracted download. See
[the headless guide](../lv2/README.md) for its independent, display-free build.
A multi-product build selects its AU
component by product base, so the MIDI job cannot validate the instrument's AU.

The retained JUCE CLAP also marks all five detection outputs read-only, using
an explicit output-parameter tag and a private copy of its pinned adapter. The
upstream checkout and other products' adapters remain unchanged. All five saved
inputs remain automatable; detection outputs retain their choice/text units and
stay out of saved state. A real-library regression checks the complete legacy
ID/domain set and restores all five nondefault inputs.

Remaining #183 work includes external MIDI-file drag interaction, transactional
state/program acceptance, remaining native/real-DAW UI and MIDI validation,
documentation/release sign-off and tagged downloadable artifacts.
These in-progress products are not grounds to close the ticket.
