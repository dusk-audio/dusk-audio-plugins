# DuskVerb 2 — 1.0.0 release candidate

Unreleased. DuskVerb 2 has a separate identity from the original DuskVerb; both can remain installed.

- Restores the studio processor interface and follow-up fixes from the missing `ui/duskverb2-design` branch, reconciled with the current DSP, preset bank and Mix Lock.
- 24 factory sounds, including four nonlinear presets, with DuskVerb 2's early reflections, spectral shaping and stereo voicing.
- Consistent VST3 class identity across Windows, Linux and macOS.
- Host tail reporting follows Freeze and long decay settings, and retains the outgoing tail during preset crossfades. AU uses DAF's one-hour fallback for Freeze; CLAP and VST3 use their infinite-tail values.
- Edited post-tank EQ survives host buffer/sample-rate reconfiguration without flattening the factory voicing.
- Preset imports retain the correct engine across legacy XML versions; the editor retains preset identity when reopened and rejects invalid recalls before mirroring them.
- Decay value entry appears at its readout. Enter commits, Escape cancels, and invalid input stays editable.
- Native macOS and Windows release jobs now run the core and hosted regression suites. Added VST3 identity and AU state/routing/tail coverage; fixed two MSVC test compilation errors and a Windows test stack overflow exposed by the new gate.

## Windows preview projects

The canonical 1.0 VST3 ID is `2046504473616C63764473446B737544`. Earlier Windows previews used `445046206173636C764473446B737544`. A project containing the old Windows ID may require manual replacement of that preview instance. Export its user preset first, insert 1.0, import the preset, and check routing and automation. Retain the preview until affected projects are migrated. The original DuskVerb identity is unchanged.

## Release acceptance still required

Run the release workflow on the final commit, including Linux ARM64 and the fleet affected by the additive shared-widget change. Verify the resulting download archives on clean systems and complete real-DAW project reopening, automation, offline bounce and listening checks. macOS bundles follow the existing workflow's ad-hoc signing policy unless Developer ID signing/notarization is enabled. No release has been tagged or published by this change.
