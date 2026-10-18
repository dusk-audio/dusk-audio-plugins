# Opto harmonic calibration, preregistered 2026-09-28

This round first lands the previously scored lab base in the production core:
TFU1 plus G2 plus G3. It then measures and fits the audio-path saturation.

## Frozen map

- Reference: UADx LA-2A Tube Compressor v1.0.8 build 787, stereo linked.
- Render protocol: 48 kHz, block 512, reset and two seconds of silent prerun.
- Tones: 100 Hz, 1 kHz and 5 kHz.
- Input peaks: -40 through 0 dBFS in 4 dB steps.
- Peak Reduction: 0; then 0.35, 0.7 and 1.0 in Compress and Limit.
- Gain: 0.15, pinned 0.25, and 0.35 (normalised host values).
- Measurements: complex H1 through H7 from a coherent steady window. Report
  H2 through H7 magnitude in dBFS and phase relative to H1.
- At 5 kHz, H5 through H7 are above the 48 kHz Nyquist frequency and are
  reported as not representable, not as missing harmonics. The magnitude bar
  applies only to harmonics below Nyquist.
- Fit data: 1 kHz only. The 100 Hz and 5 kHz rows are prediction tests and
  cannot change parameters or model selection.

## Pass bar, frozen before fitting

Every H2 through H7 magnitude must be within 2.0 dB of the UAD wherever that
UAD harmonic is above -100 dBFS, over the entire map. Phase is reported and
used to place mechanisms, but magnitude is the acceptance bar requested for
this round. The landed saturation must not regress the TFU1 + G2 + G3 lab
base's settled, recovery, knee or corrected music scores. The actual built AU,
rendered by an AU host, and the lab base settled-cell outputs must agree within
0.01 dB before fitting begins. Direct core calls are diagnostic only and cannot
satisfy any audio acceptance result in this round.

The owner's exact DAW case is a separate final witness: 1 kHz, matched output
level, Peak Reduction about 35, Compress, light reduction. Both reference and
plugin spectra will be rendered and plotted from the same stimulus.

No sealed-holdout or Untitled material may be opened. No test-map row may be
used for fitting, and no push is authorized.
