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
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DDAF_PATH="$HOME/projects/DAF"
cmake --build plugins/ring-out/daf-plugin/build --target ring-out-clap ring-out-vst3 ring-out-lv2 ring-out-au -j8
ctest --test-dir plugins/ring-out/daf-plugin/build --output-on-failure
auval -v aufx DsRO Dusk
```

The build installs into the user plugin folders unless configured with
`-DDUSK_DAF_INSTALL_LOCAL=OFF`. Identity: AU `aufx DsRO Dusk`, CLAP
`com.duskaudio.ring-out`, LV2 `https://dusk-audio.github.io/plugins/ring-out`.

### Trigger parameters and the CLAP gate (2026-10-07 review)

A trigger is an action, not a setting. The contract is that a press reaches the
plugin exactly once and the parameter reads its default before and after, so the
value a host saves and restores is never a pressed trigger. VST2 and VST3 enforce
that in the wrapper; CLAP left it to the plugin, and on DAF main `b5c242e2` five
clap-validator 0.4.1 tests failed on it. `param-set-events` and
`param-set-no-cookies` compared the same press through `process()` (completed by
the run, read back at its default) with one through `params.flush()` (no run to
complete it, read back pressed), and the three `state-reproducibility-*` tests
saved a pressed value that a reloaded instance rightly read as 0.

The fix is a framework one, in DAF's CLAP wrapper: `get_value` reports a trigger's
default while a pulse is held, and a run with frames completes any pulse by
setting the value back through the plugin's own setter and reporting the change to
the host. A plugin that consumes triggers in `setParameterValue` (Ring Out, which
acts on the press as it arrives) and one that reads the value on its next run both
receive the press exactly once; DAF's `tests/CLAPWrapper.cpp` covers both
consumption points. A block with no frames leaves the pulse pending rather than
dropping it, and a reset made while the host passed no output list is reported by
the next call that has one.

Ring Out commits its own half: `run()` retires the host-visible trigger values, as
DAF documents. The wrapper half is DAF `25d86b00` (dusk-audio/DAF#77), which also
caches a press from the plugin's own UI so its reset reaches the host. Against it,
clap-validator 0.4.1 runs clean (44 tests, 0 failed) on Linux, macOS and Windows;
against `b5c242e2`, the same five tests fail on all three. `daf-plugin/tests/DafClapTriggerTest.cpp` asserts the
five failures in miniature against the built `.clap`, plus the two things a fix
must not trade away: the press still reaches the plugin (a RESET delivered through
`flush()` clears a loaded table) and a trigger stays out of the saved state.

Do not persist triggers, change parameter flags to hide them, or weaken the
validator gate.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| SENSE | Low / High | Low | two detector threshold sets (see below) |
| SETUP | trigger | | arms the engine for 60 s (or disarms it); it switches itself off; arming from the editor resets GLOBAL Q and AMP |
| ADD | trigger | | from a host or controller: a search that ends with one filter placed or deepened, or after 10 s; from the editor: held for as long as the button is down |
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
coefficients, per-channel state, matched peaking sections from
`shared-daf/dsp/DuskFilters.hpp`, so a notch near the top of the band keeps
its analogue bandwidth without oversampling), output gain, output meter. Coefficients
and filter state retain double precision so narrow low-frequency cuts remain cuts
at high sample rates. Each input feeds a Hann-windowed FFT (4096 points below 50 kHz, 8192 to 100 kHz,
16384 above, so the bin stays near 11.7 Hz and the hop near 21 ms) that is both
the display's analyser and the detector's input. The maximum power from either
channel at each bin forms the shared spectrum; opposite-polarity stereo signals
cannot cancel a ring before detection, and a ring in just one channel retains its level.

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
outside the nearest notch widens that one (wider first, deeper once as wide
as the engine goes); a tone far from every filter is left alone, because
carving an unrelated notch deeper would not stop it. Whenever a persistent
ring leaves the engine nothing to do (no row for it, or the filter it falls
in already at the floor) `Status::ringUncovered` is raised while the engine
listens and the editor's status line says so. Each engaged filter and
frequency then gets a short hold-off so the notch can act before it is judged
again.

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
  editor's button shows the engine's state. A controller's ADD trigger starts
  a one-filter search that gives up after 10 s. The editor's ADD is a held
  search over the edit channel: `addstart` on press, `addhold` every half
  second while down (a lease the engine lets lapse after 2 s if the editor
  closes or stops drawing mid-press), `addstop` on release.
* The editor's SETUP, ADD and RESET buttons all go through the edit channel
  (`setupon` / `setupoff`, `addstart` / `addstop`, `clear`), like every
  table edit; the three trigger parameters are for host automation and
  controller mapping. A trigger written from the editor could be swallowed
  by a host that forwards only control-port changes (an LV2 port already
  sitting at 1). SETUP carries the editor's intent rather than a toggle, so
  a minute that expired between frames turns a stop into a no-op rather than
  a re-arm. The countdowns pause while the plugin is bypassed. Each control's
  phase and countdown share one lock-free atomic word, so expiry from an older
  audio-thread snapshot cannot cancel a fresh start or lease renewal.
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
before, so a preset load crossfades only what changed. Switched-off, flattened
and deleted sections fade their residual output to dry per sample before
retiring their state, preventing both a cutoff click and a stale tail on re-enable.
Bypass runs the filters
warm and crossfades with a 10 ms time constant (bit-exact dry after about
90 ms), so un-bypass has no stale tail and neither edge clicks.

User presets (`~/.config/DuskAudio/RingOut/presets/*.ropreset`) carry the
preset parameters plus a `filters=` line. A load requires all five preset
parameters and the filter table, validates them into a temporary record, and
rejects malformed, duplicate or missing fields without applying a partial preset.

## Real-world listening pass (not yet performed)

The synthetic tests do not establish the detector's false-positive rate on a
real stage. Start with one vocal microphone, one wedge and one monitor send,
with the microphone and wedge in their intended show positions. Keep an
independent console mute within reach. Record the unprocessed mic signal and
the post-plugin send to separate tracks, with identical timestamps; avoid
clipping or limiter activity that would disguise whether the notch stopped a ring.

1. Record the plugin/build revision, host/format, sample rate and block size,
   microphone/wedge models and placement, routing, gain settings and other
   processing. Use SENSE Low, LINK off, default globals, GAIN OUT 0 dB and an
   empty table. With SETUP off, raise the send gradually to the first brief
   repeatable ring, note its frequency and gain, then immediately back down.
2. Return below that onset. Arm SETUP and raise GAIN OUT in approximately 1 dB
   steps, pausing at least two seconds between changes. At each ring, note its
   onset, the first filter placement, frequency/cut/Q, any deepening, and when
   it settles. Back down if a ring keeps growing, clipping begins, or RING NOT
   COVERED appears. Stop after four to six filters; disarm and restore GAIN OUT
   to 0 dB before listening to speech and vocals.
3. Compare filtered and unfiltered onset gain in separate controlled runs,
   returning below onset before changing bypass or clearing the table. Repeat
   each condition three times at the same placement, then vary microphone
   orientation or distance one change at a time. Log headroom gained and tonal
   damage separately; do not infer useful headroom just from the filter count.
4. Test ADD on one repeatable ring: one press must add or deepen one filter,
   release must stop searching, and another press may deepen it again. Confirm
   SETUP timeout and that saving/reopening a session retains the table while
   starting idle. Repeat with mono and stereo instances; for stereo, include a
   ring in either channel and opposite polarity in the recorded input replay.
5. For a false-positive control, replay the dry recording through a separate
   instance whose output does not feed a loudspeaker, then add speech, sung
   sustained vowels, guitar sustain, decaying notes and programme playback.
   A placement or deepening during a segment with no acoustic loop and no
   recorded feedback is a false positive. A recorded real ring replayed with
   the loop disconnected is a detection control, not a false positive. Pure
   sustained musical tones may trigger by design; record those limitations
   separately from complex-programme errors. Run High only after completing Low.

For every event, retain timestamp, input/output peak levels, gain, SENSE,
SETUP/ADD state, filter table before/after, audible outcome and the matching
audio excerpt. Review misses (verified ring, no action), ineffective filters
(action, ring persists), and false positives separately. A useful result needs
repeatable suppression and measured headroom with acceptable tone; any clipping,
unexplained programme cuts or missed sustained rings needs investigation before
calling the listening pass successful. This procedure follows the pre-soundcheck
use and gradual-gain workflow in the
[X-FDBK guide](https://assets.wavescdn.com/pdf/plugins/x-fdbk.pdf), with additional
controls to evaluate Ring Out's detector rather than assume equivalent performance.

## Shared framework pieces added with this plugin

* `plugins/shared-daf/dsp/DuskSpinLock.hpp`: try-lock spinlock for audio/UI handoffs.
* `plugins/shared-daf/dsp/DuskSeqLock.hpp`: single-writer latest-frame publication.
* `plugins/shared-daf/ui/DuskStepperBox.hpp`: numeric read-out with [-]/[+] ends, drag, wheel, typed entry on a caller-defined step grid.
* `plugins/shared-daf/ui/DuskLogFreqAxis.hpp`: log-frequency axis mapping, 1-2-5 grid and console-style labels.
