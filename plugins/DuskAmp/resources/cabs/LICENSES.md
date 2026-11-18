# Bundled cabinet IR — sources & attribution

All IRs in this directory are sourced from [Soundwoofer.com](https://soundwoofer.com)'s community impulse-response library. Soundwoofer has granted Dusk Audio explicit permission to redistribute these specific files inside the DuskAmp plugin binary.

If you are a Soundwoofer uploader and want a specific IR removed or differently attributed, please open an issue on the [Dusk Audio plugins repo](https://github.com/dusk-audio/dusk-audio-plugins) — we'll respond promptly.

## Files

Bundled files use generic names (region, cabinet size, mic type, position) that match the plugin's cab-preset display names. The upstream recording, cabinet and mic names below are kept verbatim because they are the source attribution.

### `american_2x12_dynamic_oa.wav` ("American 2x12 — Dynamic")

- **Source:** Soundwoofer public catalog
- **Original filename:** `Rocksta Reactions Fender Twin Reverb SM57 A 2 3 3 45.wav`
- **Soundwoofer file ID:** `11101a0d-0525-403c-aa10-5656e0e23180`
- **Uploader:** `soundwoofer` (Soundwoofer's own account)
- **Cabinet:** Fender Twin Reverb (2×12 Jensen)
- **Mic:** Shure SM57, on-axis position
- **Format:** 44.1 kHz, 24-bit, stereo, 143 ms

### `british_4x12_dynamic_oa.wav` ("British 4x12 — Dynamic OA")

- **Source:** Soundwoofer community library
- **Original filename:** `Marshall 1960VB SM57 A 0 0 0.wav`
- **Cabinet:** Marshall 1960VB (4×12 Celestion G12T-75 / V30 mix, depending on year)
- **Mic:** Shure SM57, on-axis cap centre (position A, no offset)
- **Format:** 44.1 kHz, 24-bit, stereo, 143 ms

### `british_4x12_dynamic_off.wav` ("British 4x12 — Dynamic Off")

- **Source:** Soundwoofer community library
- **Original filename:** `Marshall 1960VB SM57 B 5 0 0.wav`
- **Cabinet:** Marshall 1960VB (same as above)
- **Mic:** Shure SM57, off-axis position B with 5 unit offset (warmer, less harsh than on-axis)
- **Format:** 44.1 kHz, 24-bit, stereo, 143 ms

### `british_1x12_dynamic.wav` ("British 1x12 — Dynamic")

- **Source:** Soundwoofer community library
- **Original filename:** `SM57 1.wav` (in the Vox AC15 set)
- **Cabinet:** Vox AC15 1×12 (typically Celestion G12M Greenback or Blue Alnico)
- **Mic:** Shure SM57, take 1
- **Format:** 44.1 kHz, 24-bit, stereo, 42 ms

### `british_1x12_condenser_close.wav` ("British 1x12 — Condenser Close")

- **Source:** Soundwoofer community library
- **Original filename:** `Se4 Close.wav` (in the Vox AC15 set)
- **Cabinet:** Vox AC15 1×12 (same as above)
- **Mic:** sE Electronics sE4 condenser, close-mic position
- **Format:** 44.1 kHz, 24-bit, stereo, 42 ms

## Adding new IRs

Every WAV added to this directory must have a matching entry above with at least:

1. The Soundwoofer (or other) source URL or file ID
2. The original filename
3. The cabinet + mic + position
4. Confirmation of redistribution permission (link to email, license URL, etc.)

If a file's redistribution rights cannot be documented, do not commit it — re-add it to `.gitignore` and treat it as user-loadable only.
