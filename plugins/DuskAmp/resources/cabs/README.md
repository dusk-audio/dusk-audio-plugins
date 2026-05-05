# DuskAmp bundled cabinet IRs

Drop CC0 / explicitly-redistributable WAV impulse responses into this directory. They get embedded into the plugin binary at build time via `juce_add_binary_data` (see `plugins/DuskAmp/CMakeLists.txt`) and are exposed at runtime through `CabinetLibrary` + the `CAB_PRESET` APVTS choice parameter.

## Naming convention

```
<amp-archetype>_<cab>_<mic>_<position>.wav
```

For example:
- `fender_twin_2x12_sm57_oa.wav` — Fender Twin 2×12, SM57 on-axis edge of cone
- `marshall_1960a_v30_sm57_oa.wav` — Marshall 1960A 4×12 V30, SM57 on-axis
- `marshall_1960a_greenback_sm57_off.wav` — Marshall 1960A Greenback, off-axis
- `vox_ac30_2x12_blue_ribbon.wav` — Vox AC30 Blue Alnico, ribbon mic
- `mesa_4x12_v30_57_blend.wav` — Mesa 4×12 V30, multi-mic blend

`CabinetLibrary::registerKnownCabinets()` knows how to map filenames to display names. If you add a new file, add an entry there too.

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
