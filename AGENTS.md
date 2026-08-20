# Agent instructions for this repository

OpenAI Codex loads this file automatically; it does not read `CLAUDE.md`.
`CLAUDE.md` remains the fuller reference for humans and for Claude — read it as
well when a task touches anything it covers. **Where the two disagree,
`CLAUDE.md` wins**, and this file should be corrected.

The rules below are the ones whose absence has actually cost real defects.

## Never commit

Never run `git add`, `git commit`, `git push`, `git checkout`, `git switch`,
`git stash`, `git reset` or `git rebase`. The human owns every commit. Leave
finished work uncommitted and describe it in your report.

## The audio thread is real-time

Everything reachable from `processBlock` (JUCE) or `run()` (DPF) is real-time.
There:

- **No allocation.** No `new`, `make_unique`, `push_back`, `resize`,
  `std::string`, `juce::String`, or any container that can grow.
- **No locks.** Use `juce::SpinLock::ScopedTryLockType` and bail out if it is
  held.
- **No I/O.** No file access, no logging, no `DBG()`.
- **No message-thread APIs.**
- Use `juce::ScopedNoDenormals` at the top of every `processBlock`.
- Cache `std::atomic<float>*` from `getRawParameterValue()` in the constructor.
- Metering atomics use `memory_order_relaxed`; state flags use release/acquire.
- Handle `numSamples == 0` with an early return.

These rules govern the audio callback. They do **not** govern test executables or
one-time setup such as `initParameter`, where allocation and printing are normal.
A review finding that applies them to a test file is wrong.

## Buffers and state

- Size every buffer in `prepare()` / `prepareToPlay()`. Clear it in `reset()`.
- **Every new state variable must be cleared in `reset()`.** A filter this
  repository forgot to reset produced a bug that only appeared on Linux, because
  macOS flushed the leftover denormals to zero and hid it.
- `prepare()` may be called repeatedly (rate or block-size changes) and must be
  safe to call twice with identical arguments.

## Fixing without breaking the neighbours

Most regressions here come from changing one path and ignoring the paths that
share its state. Before finishing a fix, enumerate the other paths touching that
state — early-return paths, bypass paths, mono/reduced-channel paths, sibling
parameters, reset paths — and confirm each still holds. Say which you checked.

When a change narrows behaviour, check the opposite failure mode too: narrowing a
preset so it stops clobbering unrelated settings can equally stop it applying the
settings it should.

## Tests must be able to fail

- Break the behaviour an assertion guards, watch it fail, restore it, watch it
  pass. Put both outputs in your report. An assertion never seen to fail is not
  yet a test.
- Watch for assertions that hold in both the passing and failing state:
  comparisons where both values may be zero, smoothness checks that a
  never-executed transition satisfies trivially, masks compared against a
  hand-picked list instead of the complete expected set.
- Test the configuration where the bug can exist, not the convenient one. A
  crossfade comb was invisible because the test ran at minimum latency.

## Parameters

Parameter IDs are `PARAM_*` constants. Define ranges once in a shared table and
drive `initParameter`, the UI mirror and preset keys from that one table, so they
cannot drift.

DPF exposes no taper mechanism the shipping formats honour —
`kParameterIsLogarithmic` is read only by VST2 and LV2 metadata, not by VST3,
CLAP or AU. Non-linear parameters therefore expose a normalised 0..1 host domain
and map internally.

Integer and enum parameters must be snapped where they enter the plugin. Storing
a host's fractional intermediate makes saved state unloadable if the state
decoder rejects non-integral values.

## State

Validate the complete parameter set into temporaries, then commit in one pass. A
half-applied state is worse than a rejected one. Type-check every property before
converting; a missing or wrong-typed entry casts to 0 silently. Parse and format
floats locale-independently in both directions.

## DPF specifics

- Build DPF from the `dusk-audio/DPF` fork, never upstream.
- `setLatency()` from `run()` **is** supported: `DistrhoPlugin.hpp` documents it,
  and the fork's CLAP wrapper latches the change and re-requests restart.
- Every effect must declare `#define DISTRHO_PLUGIN_EXTRA_IO { 1, 1 },` — the
  trailing comma is required — and track the host's choice via `ioChanged()`.
  Without it Logic omits the AU from every mono track's insert menu, and `auval`
  passes either way so it is not a gate.
- Shared UI and DSP code lives in `plugins/shared-dpf/`. Check there before
  writing anything new, and keep changes to shared headers additive — several
  plugins compile them.

## Building and testing a DPF plugin

DPF and DPF-Widgets are sibling checkouts of this repository (`../DPF`,
`../DPF-Widgets`), as JUCE is. CMake finds them automatically in a normal layout;
pass the paths explicitly only when yours differ.

```
cmake -S plugins/<name>/dpf-plugin -B build-<name> -DCMAKE_BUILD_TYPE=Release \
  -DDUSK_DPF_INSTALL_LOCAL=OFF
cmake --build build-<name> -j8
ctest --test-dir build-<name> --output-on-failure
```

On macOS a stale SDL2 entry in an existing cache can break configuration; add
`-U 'SDL2*' -U 'pkgcfg_lib_SDL2*' -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE`
if you hit it.

Report the real output. "It builds" without the output is not verification.

## Measurement work

- Any root cause found in a comment, a document or a handoff is a hypothesis. Run
  the control experiment before building on it.
- A suspiciously clean number — a uniform offset, an exact 2x, an identical value
  across very different conditions — is an arithmetic or measurement bug. Look
  for it before tuning anything.
- When driving a device hard to measure one thing, check the output is not
  saturating. An uncharacterised output ceiling silently corrupted two separate
  conclusions here.
- A negative result with a root cause is a deliverable.
