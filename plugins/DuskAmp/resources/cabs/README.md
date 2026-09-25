# DuskAmp bundled cabinet IRs

Drop CC0 / explicitly-redistributable WAV impulse responses into this directory. They get embedded into the plugin binary at build time via `juce_add_binary_data` (see `plugins/DuskAmp/CMakeLists.txt`) and are exposed at runtime through `CabinetLibrary` + the `CAB_PRESET` APVTS choice parameter.

## Naming convention

```
<region>_<cab-size>_<mic-type>_<position>.wav
```

Names stay generic (no manufacturer, model or mic brand names); the upstream
recording details live in `LICENSES.md` as attribution. For example:
- `american_2x12_dynamic_oa.wav` — American 2×12 combo, dynamic mic on-axis
- `british_4x12_dynamic_oa.wav` — British 4×12 closed-back, dynamic mic on-axis
- `british_4x12_dynamic_off.wav` — British 4×12 closed-back, dynamic mic off-axis
- `british_1x12_condenser_close.wav` — British 1×12 combo, condenser mic close

`CabinetLibrary.cpp` maps each file stem to its display name in `kEntries[]`
(with a matching `CabinetId` in `CabinetLibrary.h`). If you add a new file, add
entries there, append (never reorder) the enum, and allowlist the WAV in the
root `.gitignore`.

## Format requirements

- 24-bit PCM WAV
- 44.1kHz or 48kHz (the convolver resamples; either is fine — 48k preferred for headroom)
- Mono or stereo (the loader takes the left channel either way)
- 100–250 ms duration (longer doesn't help cab IRs and bloats the binary)
- Normalised so the **peak** is around −3 dBFS (avoids overload when stacked with preamp gain)

## Licensing

**Every file dropped here MUST have a LICENSE file alongside it documenting:**
- Original creator / uploader
- Source URL (e.g. soundwoofer.com link)
- License text (CC0 / CC-BY / explicit written permission)
- Date the permission was confirmed

A blanket `LICENSES.md` in this directory listing each file's source is fine. **Do not commit IRs without their licensing documented in this repo.** One DMCA from a cab manufacturer ends the plugin distribution.

## Sources we have permission to use

- **soundwoofer.com** — community library, runtime-fetch model. Bundling specific IRs requires written confirmation from Soundwoofer that *redistribution inside a third-party plugin binary* is OK. Permission to *use* is not the same as permission to redistribute.

## Testing locally without committed IRs

If this directory is empty, the plugin builds and runs but the `CAB_PRESET` choice list will only show "(none)" + any user-loaded IR. Drop one or more WAVs in to exercise the full path; remember to add them to `.gitignore` until licensing for redistribution is confirmed.
