# DuskVerb 2 1.0.0 release candidate

Unreleased. DuskVerb 2 has a separate identity from the original DuskVerb; both can remain installed.

- Restores the studio processor interface and follow-up fixes from the missing `ui/duskverb2-design` branch, reconciled with the current DSP, preset bank and Mix Lock.
- 24 factory sounds, including four nonlinear presets, with DuskVerb 2's early reflections, spectral shaping and stereo voicing.
- Consistent VST3 class identity across Windows, Linux and macOS.
- Host tail reporting follows Freeze and long decay settings, and retains the outgoing tail during preset crossfades. AU uses DAF's one-hour fallback for Freeze; CLAP and VST3 use their infinite-tail values.
- Edited post-tank EQ survives host buffer/sample-rate reconfiguration without flattening the factory voicing.
- Preset imports retain the correct engine across legacy XML versions; the editor retains preset identity when reopened and rejects invalid recalls before mirroring them.
- Decay value entry appears at its readout. Enter commits, Escape cancels, and invalid input stays editable.
- Windows builds include the MSVC runtime in the plugin; CI rejects accidental external runtime dependencies.
- The AU plays its reverb in REAPER on macOS. Earlier builds refused the host's latency query, and REAPER then mixed the track dry.
- Native macOS and Windows release jobs now run the core and hosted regression suites. Added VST3 identity and AU state/routing/tail coverage; fixed two MSVC test compilation errors and a Windows test stack overflow exposed by the new gate.

## Windows preview projects

The canonical 1.0 VST3 ID is `2046504473616C63764473446B737544`. Earlier Windows previews used `445046206173636C764473446B737544`. A project containing the old Windows ID may require manual replacement of that preview instance. Export its user preset first, insert 1.0, import the preset, and check routing and automation. Retain the preview until affected projects are migrated. The original DuskVerb identity is unchanged.

## Release acceptance still required

The Multi-Comp follow-up CI is green on all four targets. Fresh-scan REAPER checks
on the available Linux, macOS and Windows machines verify state, automation points
and editor lifecycle. Linux VST3/CLAP/LV2, macOS VST3/CLAP/LV2 and Windows VST3/CLAP
produce wet bounces. The macOS AU played dry in REAPER because DAF refused the
host's latency query. The fix, dusk-audio/DAF#112, is merged. CI builds made
before and after the merge play wet in REAPER, match the VST3 bit for bit without
automation, and bounce the same before and after a save and reload. See the
detailed readiness report.

Fresh CI built the final Windows runtime change on all four targets, and its new
gate confirms that both Windows binaries import only system DLLs. Complete human
listening acceptance before publication. macOS retains the explicitly selected
ad-hoc signing policy. Intel/Catalina and clean Windows 10 runtime compatibility
remain unverified because those machines are not available. No release has been
tagged or published by this change.
