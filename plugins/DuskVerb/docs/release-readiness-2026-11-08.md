# DuskVerb 2 — 1.0 readiness implementation

This change set contains the release fixes and the missing studio-processor UI.
It is not a published release; the remaining acceptance work is listed below.

## Source and UI provenance

- Base: `main`, `b86f3e7707653ef6cf7252971c726dc952b0177d`.
- Missing UI branch: `ui/duskverb2-design`, `5305202bac1bd9e9768f4935745676290db903c6`.
- The branch has 12 commits absent from main, including `d9969648` (studio processor face), and is 59 commits behind main. Its UI and follow-up fixes were reconciled into the current source, preserving the newer DSP, 24 presets, reserved algorithm slots, Mix Lock and instance-owned A/B state.
- Build framework: `dusk-audio/DAF`, `68b07f25c9b9f2d1d014764615148806c7ce97e3`, in isolated checkouts. The existing sibling DAF checkout was not modified.
- Release configuration, tuning hooks off, local plugin installation off.

## Corrected behavior

- VST3 exposes the same canonical class ID on Windows, Linux and macOS. Windows preview migration instructions accompany the change.
- Tail reporting follows applied Freeze, supported engines, long decay/damping and outgoing preset fades. AU uses the framework's one-hour frozen-tail fallback.
- Edited post-tank EQ survives repeated preparation and rate/block changes while untouched factory voicing remains intact.
- The graphite face and amber display replace the older UI. Output history is sampled at 15 Hz independently of host block size; mono tracks meter their actual output. History and partial peaks clear on reset.
- Decay entry appears at the amber readout. Shared entry handling retains typed text on re-click, balances host gestures, and handles Enter, invalid input, Escape and field switching.
- A complete host state restore cancels unfinished entry for the outgoing sound; ordinary parameter automation preserves the entry.
- Preset identity survives editor reopening. Imported legacy XML retains the correct engine, accepts JUCE's bare default parameters, and rejects invalid names and malformed recalls transactionally.
- New nonlinear sounds remain selectable; unavailable controls dim and unsupported Freeze/Gate switches disappear. Mix Lock remains available below Bus.
- Native macOS/Windows CI now runs DuskVerb's tests. Fixed MSVC lambda compilation failures and a test DSP instance exceeding Windows' default stack.
- Manual, screenshots, release notes and the local unpublished website draft reflect the current interface and 24-preset bank.

## Regression evidence

Actual failing controls and restored results, retained under `/tmp`:

| Regression | Failing control | Corrected result |
|---|---|---|
| Windows VST3 identity | `13 checks, 1 failures`; old ID `445046206173636C764473446B737544` | Expected public ID `2046504473616C63764473446B737544` |
| CLAP frozen-tail contract | Original binary: 5 failures in 9,232 checks; frozen output still audible after 32 seconds | Integrated hosted suite |
| VST3 frozen-tail contract | Original binary: 6 failures | Integrated hosted suite |
| Post-tank EQ reprepare | Restore step removed: `8 check(s) FAILED` | `DuskVerbCoreTests: all checks passed` |
| Entry position | Original shared widget: 2 position assertions failed | Shared knob suite passes at multiple scales |
| Output-history bridge | Feed disconnected: `36 check(s) FAILED` | `DuskVerbCoreTests: all checks passed` |
| Host restore during value entry | `FAIL: host restore cancels unfinished entry for the previous preset` (real CLAP editor) | The same permanent UI gate passes, including the opposite automation case |

Neighboring paths checked: zero and variable blocks, mono/stereo/mono-to-stereo,
bypass, supported and unsupported Freeze, reset, repeated prepare, rate changes,
queued/live preset recalls, outgoing crossfades, factory versus unnamed snapshots,
factory EQ versus explicitly edited gains, Mix Lock on/off, INIT and A/B.

## Integrated-build evidence

- Linux x86_64: **all 31 registered tests pass across the core/host and display-enabled runs**. The broad run passed 29 tests and its resize test was blocked by sandbox X11 access (601.18 seconds). After the final editor-state fix, the five affected hosted/resize/drag checks pass with an isolated display (18.49 seconds), including the expanded permanent UI regression.
- Windows x64/MSVC: **27/27 tests passed, 580.08 seconds**, including native VST3 identity, CLAP state/tail and the repaired test stack allocation. After the final UI-only adjustment, all three affected CLAP/VST3 hosted checks passed again (3.00 seconds).
- AddressSanitizer, UndefinedBehaviorSanitizer and leak detection: **`DuskVerbCoreTests: all checks passed`, exit 0**. LeakSanitizer required a run outside the sandbox to inspect threads.

- macOS universal x86_64/arm64 build: **30/30 tests passed, 69.96 seconds**, executed natively on arm64. Includes native AU state/routing/reset/tail and VST3 identity checks. Bundle signature verification passed; this is not Developer ID notarization. After the final UI-only adjustment, all three AU/CLAP/VST3 hosted checks passed again (0.77 seconds).
- Real Linux editor: typed `2.5s` reports `2.50 s` to the host. Both drag phases produced 12 edits, including a focus change. Factory navigation reaches Nonlinear Huge; Bus/Mix Lock, INIT and help pass real-pointer checks.
- Audio preservation: **192 renders** (24 presets × impulse/noise/snare/sustained synthetic inputs × 48/96 kHz) compare byte-for-byte equal to the pre-fix baseline. Maximum peak **0.237914**, observed C++ `new`/`new[]` allocations during processing **0**. This does not instrument every possible allocator or lock.
- CLAP validator: **42 tests run, 34 passed, 0 failed, 0 warnings, 8 skipped**, using the existing preset-discovery exclusions. DAF has no CLAP preset-discovery implementation; that path is not claimed as covered.
- VST3 pluginval: **strictness 8, SUCCESS**, GUI tests skipped; editor interactions were checked separately.
- `actionlint`, manual preflight and `git diff --check` pass. The rebuilt three-page PDF was visually inspected.

## Validation artifacts and logs

Local review artifacts are in `/tmp/duskverb2-release-artifacts/`, including the
manual, corrected screenshot and explicitly labelled validation binaries. These
are not public release downloads. Build and test logs use the
`/tmp/duskverb2-integrated-*` prefix; final editor tests use
`/tmp/duskverb2-ui-state-{red,green}.log`. The native source snapshots are isolated
under `/tmp/duskverb2-readiness-20261108` on macOS and
`C:/tmp/duskverb2-readiness-20261108` on Windows.

The existing website draft, screenshot and PDF in
`/home/marc/projects/dusk-audio.github.io` were updated locally; `published: false`
was retained. No plugin release tag has been created.

## Remaining release acceptance

Run the release workflow on the final commit, including Linux ARM64 and
other plugins consuming the shared widget headers. The local Linux build is a
validation build, not proof of the release image's minimum glibc compatibility.
The macOS build contains both architectures, but x86_64 runtime and Catalina
compatibility were not exercised; the installed SDK warns about the old deployment
target. Windows testing uses the current VM, not a clean Windows 10 machine.

Complete real-DAW project save/reopen, editor lifecycle, automation, offline bounce,
clean install/rescan and listening acceptance on the packaged downloads. The
existing release workflow uses ad-hoc macOS signatures unless Developer ID signing
and notarization are enabled. No signing-policy change or publication was made.

The audio comparison establishes preservation of the existing voicing; it is not
new reference-anchor calibration or a listening sign-off. Private tuner
`full_check`/`fleet_audit` and clean-system download acceptance remain unclaimed.
