# Chord Analyzer DAF successors (in progress, #183)

Configure this directory directly; the shipping JUCE products and native
headless LV2 retain their existing identities and build paths.

```sh
cmake -S plugins/chord-analyzer/daf-plugin -B build-chord-analyzer-daf \
  -DCMAKE_BUILD_TYPE=Release -DDUSK_DAF_INSTALL_LOCAL=OFF
cmake --build build-chord-analyzer-daf -j8
ctest --test-dir build-chord-analyzer-daf --output-on-failure
```

Both successors currently build CLAP, VST3 and LV2, plus AU on macOS. They have
generic host controls; the Dear ImGui editor is not implemented yet.

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
parameters. Small choice domains retain their displayed names; the large
`detectedChord` code domain still needs a bounded, allocation-free custom text
converter before editor/release completion. Host wrappers own parameter state;
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
off the audio thread. The DAF editor/export interaction still needs wiring.

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

Remaining #183 work includes the complete direct-access editor, export/drag
interaction, encoded-label text callbacks, retained
headless successor packaging, transactional state/program acceptance, CI/release
registration, AU/real-DAW validation and tagged downloadable artifacts. These
initial shells are not a completed release or grounds to close the ticket.
