# Ring Out

Feedback eliminator for ringing out monitors and PA, built on DAF (Dusk Audio
Framework) with a Dear ImGui editor. Modelled on the control surface of the
classic feedback eliminators: one minute of SETUP to find the frequencies that
ring, up to twenty notch filters, global Q and amplitude trims, and a LINK that
trades output gain for cut depth.

```
plugins/ring-out/
  core/        framework-free engine (filters, detector, analyser, meters) + tests
  daf-plugin/  DAF shell, parameters, state, Dear ImGui UI, CMake
```

## Build

```
cmake -S plugins/ring-out/daf-plugin -B plugins/ring-out/daf-plugin/build \
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DDAF_PATH=~/projects/DAF
cmake --build plugins/ring-out/daf-plugin/build --target ring-out-clap ring-out-vst3 ring-out-lv2 ring-out-au -j8
ctest --test-dir plugins/ring-out/daf-plugin/build --output-on-failure
auval -v aufx DsRO Dusk
```

The build installs into the user plugin folders unless configured with
`-DDUSK_DAF_INSTALL_LOCAL=OFF`. Identity: AU `aufx DsRO Dusk`, CLAP
`com.duskaudio.ring-out`, LV2 `https://dusk-audio.github.io/plugins/ring-out`.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| SENSE | Low / High | Low | two detector threshold sets (see below) |
| SETUP | trigger | | arms the engine for 60 s (or disarms it); it switches itself off; arming from the editor resets GLOBAL Q and AMP |
| ADD | trigger | | starts a search that ends with one filter placed or deepened, when the editor's button is released, or after 10 s |
| RESET | trigger | | removes every filter |
| filter CUT | -20 .. 0 dB, 0.1 dB steps | 0 | per filter |
| filter FREQ | 24 Hz .. 20 kHz | 24 Hz | steps of 1 Hz to 500 Hz, 10 Hz to 1 kHz, 100 Hz above; the value itself is continuous |
| filter Q | 0.5 .. 20, 0.1 steps | 2.5 | per filter |
| GLOBAL Q | 0.2 .. 10 | 1.0 | multiplies every filter's Q; red rim when not at default |
| GLOBAL AMP | -24 .. +24 dB | 0 | added to every filter's cut, clamped so a filter never boosts |
| LINK | on / off | off | moving GAIN OUT by x dB moves AMP by -x dB and the other way round |
| GAIN OUT | -24 .. +24 dB | 0 | |
| meters | -80 .. 0 dBFS | | peak hold, click to reset |

NEW adds a filter at 24 Hz / 0 dB / Q 2.5, DEL removes the selected one and
closes the gap, the indicators light for filters that are on, double-clicking an
indicator switches that filter off, the < > buttons and a click on the graph
select a filter.

## Engine

`core/RingOutDSP.{hpp,cpp}`. Signal path: input meter, notch bank (shared
coefficients, per-channel state, RBJ peaking sections from
`shared-daf/dsp/DuskFilters.hpp`), output gain, output meter. The mono sum of
the input feeds a Hann-windowed FFT (4096 points below 50 kHz, 8192 to 100 kHz,
16384 above, so the bin stays near 11.7 Hz and the hop near 21 ms) that is both
the display's analyser and the detector's input.

A spectral peak becomes a feedback candidate when, in one frame, it is

* far above the frame average (PAPR) and above its own neighbourhood outside
  the window main lobe (PNPR),
* without second or third harmonic (PHPR), not itself the harmonic of louder
  content below it, and not a partial of a harmonic series (a fundamental f/k
  with another series member present),
* and above an absolute floor.

Candidates are tracked frame to frame (parabolic interpolation gives the
sub-bin frequency). A track qualifies after N consecutive frames at the same
frequency during which its level never fell more than a tolerance below its own
maximum: feedback grows or holds, a note decays.

| | Low | High |
|---|---|---|
| PAPR / PNPR / PHPR | 24 / 16 / 16 dB | 16 / 10 / 10 dB |
| sub-harmonic margin | 8 dB | 4 dB |
| absolute floor | -62 dBFS | -72 dBFS |
| drop tolerance | 2 dB | 3 dB |
| persistence | 6 frames (about 130 ms) | 4 frames |

Engaging: if an existing filter's bandwidth covers the tone it is deepened by
3 dB (to -20 dB), then widened by a fifth (to Q 0.7), and its centre is nudged
toward the tone; otherwise a new filter is placed at the tone with a cut of 6 dB
plus half the growth observed (up to 12 dB) and a Q between 3 and 8 chosen so
the notch spans at least three bins. With all twenty slots taken, a tone just
outside the nearest notch widens that one; a tone far from every filter is
left alone and reported (`Status::tableFull`), because carving an unrelated
notch deeper would not stop it. Each engaged filter and frequency then gets a
short hold-off so the notch can act before it is judged again.

`core/tests/RingOutDSPTest.cpp` covers the step rules, the text forms, notch
accuracy, globals, block-size invariance, detection of a growing tone and its
deepening, rejection of harmonic-rich and decaying tones, ADD and the SETUP
timer, and a simulated acoustic loop (resonance plus 20 ms of delay, 2 dB over
unity) that must run away without processing and settle with SETUP armed.

## State model

The filter table is **plugin state, not parameters**: the engine writes it from
the audio thread, which no host parameter path allows. `RingOutDSP` owns the
table behind a spinlock the audio thread only ever try-locks
(`shared-daf/dsp/DuskSpinLock.hpp`); the spectrum goes to the UI through a
seqlock (`DuskSeqLock.hpp`).

* `filters` (host readable, saved): the table as text,
  `on,freq,cut,Q;on,freq,cut,Q;...` (`RingOutFilterTable.hpp`). With
  `DAF_PLUGIN_WANT_FULL_STATE` the host reads it back through `getState()` when
  it saves, so filters the engine placed survive a reload.
* `edit` (DSP only, never persisted): the UI's command channel, one command per
  `setState`: `set,<slot>,...`, `add,...`, `del,<slot>`, `clear`. `getState()`
  for this key is always empty so a saved session replays nothing. Granular
  commands rather than a table replace mean a filter the engine adds while the
  user drags a control is never clobbered by a stale copy.

Host parameters (`daf-plugin/RingOutParams.hpp`, append-only): Sense, Setup
(trigger), Add (trigger), Reset (trigger), Global Q, Global Amp, Link, Gain
Out, Bypass, and the two meter outputs.

* SETUP and ADD are trigger parameters, like the reference's momentary
  buttons. A trigger is never part of a saved session and `activate()`
  replays nothing for it, so neither a project saved while the detector was
  listening nor a block-size or rate change can re-arm it on a live PA. Each
  SETUP press arms the engine or, while it is listening, disarms it; the
  editor's button shows the engine's state. ADD starts a one-filter search;
  the editor ends it on release through the edit channel (`addstop`), and a
  search nobody ends gives up after 10 s.
* The editor's RESET button goes through the edit channel (`clear`), like
  every other table edit; the Reset trigger parameter is for host automation
  and controller mapping.
* Arming SETUP from the editor also resets GLOBAL Q and AMP, as editor edits
  the host sees. The plugin never rewrites one parameter because another
  moved: that fails AU validation ("Parameter did not retain set value") and
  surprises host automation. Arming from a mapped controller with the editor
  closed therefore leaves the trims where they are.
* LINK is an editor gesture coupling, as on the reference: dragging either
  knob sends both parameters to the host. It is deliberately not applied in
  the processor, where it would fire on session restore (GAIN OUT is restored
  after LINK and would shift the restored AMP) and would turn one automation
  lane into silent writes to a second parameter.
* RESET from a host's process callback never waits: `resetFilters()`
  try-locks and otherwise leaves a request the audio thread honours at its
  next block.

Every table row carries a stable id, and filter *slots* follow ids, not row
numbers: a deleted filter fades out in its slot, the filters that move down a
row keep their slot and their state, a FREQ step glides the same slot, and a
new row ramps in from flat in a free one. There are twice as many slots as
rows (40) so even a whole-table replacement (a preset load) crossfades
cleanly; `setTable()` keeps the id of any row that is the same filter as
before, so a preset load crossfades only what changed. Bypass runs the filters
warm and crossfades to a bit-exact dry path over about 30 ms, so un-bypass
has no stale tail and neither edge clicks.

User presets (`~/.config/DuskAudio/RingOut/presets/*.ropreset`) carry the
preset parameters plus a `filters=` line.

## Shared framework pieces added with this plugin

* `plugins/shared-daf/dsp/DuskSpinLock.hpp`: try-lock spinlock for audio/UI handoffs.
* `plugins/shared-daf/dsp/DuskSeqLock.hpp`: single-writer latest-frame publication.
* `plugins/shared-daf/ui/DuskStepperBox.hpp`: numeric read-out with [-]/[+] ends, drag, wheel, typed entry on a caller-defined step grid.
* `plugins/shared-daf/ui/DuskLogFreqAxis.hpp`: log-frequency axis mapping, 1-2-5 grid and console-style labels.
