# Opto R8 final-harmonic checkpoint — 2026-09-29

## Harmonic scorecard

All measurements in this section are renders from the actual UADx LA-2A and
Multi-Comp 2 Audio Units hosted by the same frozen `duskverb_render` host. The
host ran stereo at 48 kHz in 512-sample blocks, reset each instance, supplied
two seconds of silent pre-roll and compensated the reported latencies (UAD 87
samples, MC-2 67 samples) before comparison. MC-2 was explicitly set to normal
linked stereo, SC HP off, Mix 100% wet, Analog Noise off, generic Distortion
off, Auto Makeup off, True Peak off and Lookahead off.

The preregistered magnitude bar remains H2-H7 within 2 dB wherever the UAD
harmonic is above -100 dBFS. R8 fits the complex residual (magnitude and phase)
jointly at 100 Hz, 1 kHz and 5 kHz over all requested levels, PR values, modes
and Gain values. The correction is the final audio-path operation, after every
gain-law correction, so later gain-computer changes cannot rescale its fitted
absolute harmonics.

| Grid | Passed / eligible | Worst magnitude error | Verdict |
|---|---:|---:|---|
| R7 committed, 100 Hz / 1 kHz / 5 kHz | 1,612 / 1,683 | 11.159 dB | fail |
| R8 fitted, 100 Hz / 1 kHz / 5 kHz | **1,683 / 1,683** | **0.013 dB** | **pass** |
| R8 held out, 300 Hz / 2.5 kHz / 10 kHz | **1,169 / 1,332** | **16.548 dB** | **fail** |

The held-out frequencies were not used by the coefficient generator and were
captured only after the fit was frozen. No coefficient was changed after seeing
them. The prediction therefore remains an honest failure rather than being
folded back into the fitted grid.

The owner's repeatable 1 kHz case remains matched: -16 dBFS peak, Peak
Reduction position 0.35 (display 33), Gain position 0.25 (display 22),
Compress. A +0.003520 dB post-render trim makes the loudness delta 0.000000 dB:

| Harmonic | UAD dBFS | MC-2 dBFS | MC-2 - UAD |
|---:|---:|---:|---:|
| H1 | -15.329 | -15.329 | +0.000 dB |
| H2 | -67.239 | -67.235 | +0.004 dB |
| H3 | -71.691 | -71.687 | +0.004 dB |
| H4 | -91.616 | -91.613 | +0.003 dB |
| H5 | -87.528 | -87.524 | +0.004 dB |
| H6 | -101.855 | -101.853 | +0.002 dB |
| H7 | -95.980 | -95.978 | +0.002 dB |

The fresh 48 kHz/24-bit WAVs, matched table and separate spectrum plots are in
`build-multi-comp-1176/opto-production-harmonics-20260928/checkpoints/r8-final-postfix/one-khz-pr35/`.

### Held-out failure breakdown

The 163 failures break down as follows. Counts are failures, not all eligible
cells.

| Dimension | Failure counts |
|---|---|
| Frequency | 300 Hz: 108; 2.5 kHz: 46; 10 kHz: 9 |
| Harmonic | H2: 12; H3: 5; H4: 16; H5: 105; H6: 0; H7: 25 |
| PR position | 0: 5; 0.35: 83; 0.70: 52; 1.0: 23 |
| Gain position | 0.15: 22; 0.25: 57; 0.35: 84 |
| Mode | Compress: 80; Limit: 83 |
| Input dBFS | -40: 2; -36: 5; -32: 6; -28: 6; -24: 2; -20: 14; -16: 12; -12: 28; -8: 23; -4: 30; 0: 35 |

Only 22 failures have a UAD harmonic above -80 dBFS. Those are marked `YES`
in the complete inventory below. Positive error means MC-2 is louder than the
UAD; negative means quieter.

| # | Hz | Input dBFS | PR | Gain | Mode | Harmonic | UAD dBFS | MC-2 dBFS | Error dB | UAD > -80 |
|---:|---:|---:|---:|---:|:---|---:|---:|---:|---:|:---:|
| 1 | 300 | -40 | 1.00 | 0.35 | Compress | H5 | -99.570 | -103.321 | -3.751 |  |
| 2 | 300 | -40 | 1.00 | 0.35 | Limit | H5 | -99.734 | -102.932 | -3.199 |  |
| 3 | 300 | -36 | 0.70 | 0.25 | Limit | H3 | -95.138 | -91.736 | +3.402 |  |
| 4 | 300 | -36 | 0.70 | 0.35 | Compress | H5 | -99.886 | -102.378 | -2.492 |  |
| 5 | 300 | -36 | 0.70 | 0.35 | Limit | H3 | -88.298 | -84.883 | +3.415 |  |
| 6 | 300 | -36 | 0.70 | 0.35 | Limit | H5 | -99.312 | -94.796 | +4.516 |  |
| 7 | 300 | -36 | 1.00 | 0.35 | Compress | H5 | -97.484 | -102.060 | -4.577 |  |
| 8 | 300 | -32 | 0.70 | 0.25 | Compress | H5 | -98.487 | -101.696 | -3.209 |  |
| 9 | 300 | -32 | 0.70 | 0.25 | Limit | H5 | -98.362 | -101.716 | -3.354 |  |
| 10 | 300 | -32 | 0.70 | 0.35 | Compress | H5 | -91.653 | -94.884 | -3.231 |  |
| 11 | 300 | -32 | 0.70 | 0.35 | Limit | H5 | -91.538 | -94.899 | -3.362 |  |
| 12 | 300 | -32 | 1.00 | 0.35 | Compress | H5 | -96.578 | -102.130 | -5.552 |  |
| 13 | 300 | -32 | 1.00 | 0.35 | Limit | H5 | -97.744 | -104.245 | -6.500 |  |
| 14 | 300 | -28 | 0.70 | 0.25 | Compress | H5 | -94.573 | -98.805 | -4.232 |  |
| 15 | 300 | -28 | 0.70 | 0.35 | Compress | H5 | -87.730 | -91.997 | -4.267 |  |
| 16 | 300 | -28 | 1.00 | 0.35 | Compress | H5 | -96.243 | -101.865 | -5.622 |  |
| 17 | 300 | -28 | 1.00 | 0.35 | Limit | H5 | -98.195 | -105.134 | -6.939 |  |
| 18 | 300 | -24 | 0.70 | 0.25 | Compress | H5 | -96.859 | -102.693 | -5.834 |  |
| 19 | 300 | -24 | 0.70 | 0.35 | Compress | H5 | -90.030 | -95.871 | -5.842 |  |
| 20 | 300 | -20 | 0.35 | 0.25 | Compress | H5 | -94.221 | -96.685 | -2.464 |  |
| 21 | 300 | -20 | 0.35 | 0.25 | Limit | H5 | -93.918 | -96.191 | -2.273 |  |
| 22 | 300 | -20 | 0.35 | 0.35 | Compress | H5 | -88.745 | -91.727 | -2.981 |  |
| 23 | 300 | -20 | 0.35 | 0.35 | Limit | H5 | -88.382 | -91.108 | -2.726 |  |
| 24 | 300 | -20 | 0.70 | 0.15 | Compress | H5 | -99.682 | -104.085 | -4.403 |  |
| 25 | 300 | -20 | 0.70 | 0.25 | Compress | H5 | -89.911 | -94.330 | -4.420 |  |
| 26 | 300 | -20 | 0.70 | 0.25 | Limit | H5 | -91.392 | -107.940 | -16.548 |  |
| 27 | 300 | -20 | 0.70 | 0.35 | Compress | H4 | -93.814 | -95.830 | -2.015 |  |
| 28 | 300 | -20 | 0.70 | 0.35 | Compress | H5 | -83.090 | -87.507 | -4.417 |  |
| 29 | 300 | -20 | 0.70 | 0.35 | Limit | H4 | -96.143 | -98.473 | -2.330 |  |
| 30 | 300 | -20 | 0.70 | 0.35 | Limit | H5 | -84.563 | -101.110 | -16.546 |  |
| 31 | 300 | -16 | 0.35 | 0.15 | Limit | H5 | -91.628 | -93.798 | -2.170 |  |
| 32 | 300 | -16 | 0.35 | 0.25 | Limit | H5 | -82.005 | -84.259 | -2.255 |  |
| 33 | 300 | -16 | 0.35 | 0.35 | Limit | H5 | -75.521 | -77.847 | -2.326 | YES |
| 34 | 300 | -16 | 0.70 | 0.15 | Compress | H5 | -98.839 | -102.899 | -4.059 |  |
| 35 | 300 | -16 | 0.70 | 0.25 | Compress | H5 | -89.082 | -93.147 | -4.065 |  |
| 36 | 300 | -16 | 0.70 | 0.25 | Limit | H5 | -91.525 | -101.160 | -9.635 |  |
| 37 | 300 | -16 | 0.70 | 0.35 | Compress | H5 | -82.264 | -86.326 | -4.061 |  |
| 38 | 300 | -16 | 0.70 | 0.35 | Compress | H7 | -96.071 | -93.859 | +2.212 |  |
| 39 | 300 | -16 | 0.70 | 0.35 | Limit | H5 | -84.698 | -94.323 | -9.625 |  |
| 40 | 300 | -16 | 0.70 | 0.35 | Limit | H7 | -97.782 | -102.275 | -4.493 |  |
| 41 | 300 | -12 | 0.00 | 0.35 | Compress | H4 | -94.533 | -92.499 | +2.034 |  |
| 42 | 300 | -12 | 0.35 | 0.15 | Compress | H5 | -86.947 | -89.580 | -2.633 |  |
| 43 | 300 | -12 | 0.35 | 0.15 | Limit | H5 | -86.943 | -89.890 | -2.947 |  |
| 44 | 300 | -12 | 0.35 | 0.25 | Compress | H5 | -77.345 | -80.082 | -2.736 | YES |
| 45 | 300 | -12 | 0.35 | 0.25 | Limit | H5 | -77.324 | -80.376 | -3.053 | YES |
| 46 | 300 | -12 | 0.35 | 0.35 | Compress | H5 | -70.689 | -73.608 | -2.919 | YES |
| 47 | 300 | -12 | 0.35 | 0.35 | Compress | H7 | -98.119 | -91.445 | +6.674 |  |
| 48 | 300 | -12 | 0.35 | 0.35 | Limit | H5 | -70.665 | -73.889 | -3.225 | YES |
| 49 | 300 | -12 | 0.35 | 0.35 | Limit | H7 | -97.166 | -91.552 | +5.614 |  |
| 50 | 300 | -12 | 0.70 | 0.15 | Compress | H5 | -99.908 | -105.146 | -5.238 |  |
| 51 | 300 | -12 | 0.70 | 0.25 | Compress | H5 | -90.165 | -95.407 | -5.242 |  |
| 52 | 300 | -12 | 0.70 | 0.25 | Limit | H5 | -94.265 | -107.072 | -12.808 |  |
| 53 | 300 | -12 | 0.70 | 0.35 | Compress | H5 | -83.343 | -88.602 | -5.260 |  |
| 54 | 300 | -12 | 0.70 | 0.35 | Limit | H5 | -87.439 | -100.232 | -12.793 |  |
| 55 | 300 | -12 | 0.70 | 0.35 | Limit | H7 | -96.183 | -100.029 | -3.846 |  |
| 56 | 300 | -8 | 0.00 | 0.35 | Compress | H7 | -89.816 | -96.841 | -7.025 |  |
| 57 | 300 | -8 | 0.35 | 0.15 | Compress | H4 | -99.891 | -97.855 | +2.036 |  |
| 58 | 300 | -8 | 0.35 | 0.15 | Compress | H5 | -84.165 | -88.227 | -4.062 |  |
| 59 | 300 | -8 | 0.35 | 0.15 | Limit | H5 | -84.435 | -88.857 | -4.422 |  |
| 60 | 300 | -8 | 0.35 | 0.25 | Compress | H4 | -90.080 | -88.015 | +2.064 |  |
| 61 | 300 | -8 | 0.35 | 0.25 | Compress | H5 | -74.567 | -78.772 | -4.204 | YES |
| 62 | 300 | -8 | 0.35 | 0.25 | Limit | H4 | -91.045 | -88.891 | +2.155 |  |
| 63 | 300 | -8 | 0.35 | 0.25 | Limit | H5 | -74.808 | -79.383 | -4.576 | YES |
| 64 | 300 | -8 | 0.35 | 0.35 | Compress | H5 | -67.902 | -72.464 | -4.562 | YES |
| 65 | 300 | -8 | 0.35 | 0.35 | Limit | H5 | -68.115 | -73.013 | -4.898 | YES |
| 66 | 300 | -8 | 0.70 | 0.25 | Compress | H5 | -91.959 | -99.138 | -7.179 |  |
| 67 | 300 | -8 | 0.70 | 0.25 | Limit | H5 | -98.799 | -107.428 | -8.629 |  |
| 68 | 300 | -8 | 0.70 | 0.35 | Compress | H5 | -85.132 | -92.334 | -7.203 |  |
| 69 | 300 | -8 | 0.70 | 0.35 | Limit | H5 | -91.969 | -100.599 | -8.630 |  |
| 70 | 300 | -4 | 0.00 | 0.25 | Compress | H7 | -98.701 | -105.950 | -7.249 |  |
| 71 | 300 | -4 | 0.35 | 0.15 | Compress | H5 | -82.746 | -87.867 | -5.121 |  |
| 72 | 300 | -4 | 0.35 | 0.15 | Limit | H5 | -83.386 | -89.051 | -5.665 |  |
| 73 | 300 | -4 | 0.35 | 0.25 | Compress | H4 | -91.449 | -88.891 | +2.558 |  |
| 74 | 300 | -4 | 0.35 | 0.25 | Compress | H5 | -73.150 | -78.445 | -5.295 | YES |
| 75 | 300 | -4 | 0.35 | 0.25 | Compress | H7 | -95.510 | -93.202 | +2.308 |  |
| 76 | 300 | -4 | 0.35 | 0.25 | Limit | H4 | -92.971 | -90.149 | +2.822 |  |
| 77 | 300 | -4 | 0.35 | 0.25 | Limit | H5 | -73.764 | -79.601 | -5.837 | YES |
| 78 | 300 | -4 | 0.35 | 0.25 | Limit | H7 | -95.963 | -92.172 | +3.791 |  |
| 79 | 300 | -4 | 0.35 | 0.35 | Compress | H4 | -83.453 | -80.943 | +2.510 |  |
| 80 | 300 | -4 | 0.35 | 0.35 | Compress | H5 | -66.544 | -72.398 | -5.854 | YES |
| 81 | 300 | -4 | 0.35 | 0.35 | Limit | H4 | -84.920 | -82.343 | +2.577 |  |
| 82 | 300 | -4 | 0.35 | 0.35 | Limit | H5 | -67.083 | -73.378 | -6.295 | YES |
| 83 | 300 | -4 | 0.35 | 0.35 | Limit | H7 | -88.712 | -85.731 | +2.981 |  |
| 84 | 300 | -4 | 0.70 | 0.25 | Compress | H5 | -94.193 | -102.236 | -8.043 |  |
| 85 | 300 | -4 | 0.70 | 0.35 | Compress | H5 | -87.377 | -95.380 | -8.003 |  |
| 86 | 300 | -4 | 0.70 | 0.35 | Limit | H5 | -97.513 | -104.286 | -6.774 |  |
| 87 | 300 | -4 | 1.00 | 0.25 | Compress | H5 | -97.525 | -99.941 | -2.416 |  |
| 88 | 300 | -4 | 1.00 | 0.35 | Compress | H5 | -90.707 | -93.133 | -2.426 |  |
| 89 | 300 | 0 | 0.35 | 0.15 | Compress | H5 | -82.118 | -88.637 | -6.519 |  |
| 90 | 300 | 0 | 0.35 | 0.15 | Limit | H5 | -83.309 | -89.818 | -6.509 |  |
| 91 | 300 | 0 | 0.35 | 0.25 | Compress | H4 | -93.245 | -90.156 | +3.089 |  |
| 92 | 300 | 0 | 0.35 | 0.25 | Compress | H5 | -72.531 | -79.229 | -6.698 | YES |
| 93 | 300 | 0 | 0.35 | 0.25 | Compress | H7 | -92.810 | -88.302 | +4.507 |  |
| 94 | 300 | 0 | 0.35 | 0.25 | Limit | H4 | -95.649 | -91.990 | +3.659 |  |
| 95 | 300 | 0 | 0.35 | 0.25 | Limit | H5 | -73.687 | -80.399 | -6.712 | YES |
| 96 | 300 | 0 | 0.35 | 0.25 | Limit | H7 | -93.054 | -89.058 | +3.996 |  |
| 97 | 300 | 0 | 0.35 | 0.35 | Compress | H4 | -84.818 | -81.548 | +3.270 |  |
| 98 | 300 | 0 | 0.35 | 0.35 | Compress | H5 | -66.034 | -72.173 | -6.140 | YES |
| 99 | 300 | 0 | 0.35 | 0.35 | Compress | H7 | -86.114 | -81.598 | +4.516 |  |
| 100 | 300 | 0 | 0.35 | 0.35 | Limit | H4 | -87.102 | -83.839 | +3.263 |  |
| 101 | 300 | 0 | 0.35 | 0.35 | Limit | H5 | -67.032 | -72.626 | -5.594 | YES |
| 102 | 300 | 0 | 0.35 | 0.35 | Limit | H7 | -86.354 | -82.477 | +3.878 |  |
| 103 | 300 | 0 | 0.70 | 0.25 | Compress | H5 | -96.530 | -102.954 | -6.424 |  |
| 104 | 300 | 0 | 0.70 | 0.35 | Compress | H5 | -89.692 | -95.934 | -6.241 |  |
| 105 | 300 | 0 | 1.00 | 0.15 | Limit | H3 | -98.289 | -96.165 | +2.124 |  |
| 106 | 300 | 0 | 1.00 | 0.25 | Limit | H3 | -88.515 | -86.391 | +2.124 |  |
| 107 | 300 | 0 | 1.00 | 0.35 | Limit | H3 | -81.706 | -79.581 | +2.124 |  |
| 108 | 300 | 0 | 1.00 | 0.35 | Limit | H5 | -94.677 | -91.035 | +3.642 |  |
| 109 | 2500 | -20 | 0.70 | 0.25 | Compress | H5 | -98.406 | -95.734 | +2.672 |  |
| 110 | 2500 | -20 | 0.70 | 0.35 | Compress | H5 | -91.598 | -88.919 | +2.679 |  |
| 111 | 2500 | -20 | 0.70 | 0.35 | Limit | H5 | -93.477 | -90.096 | +3.382 |  |
| 112 | 2500 | -16 | 0.70 | 0.25 | Limit | H5 | -98.906 | -96.202 | +2.704 |  |
| 113 | 2500 | -16 | 0.70 | 0.35 | Limit | H5 | -92.113 | -89.392 | +2.720 |  |
| 114 | 2500 | -12 | 0.00 | 0.35 | Compress | H4 | -94.495 | -97.708 | -3.213 |  |
| 115 | 2500 | -12 | 0.35 | 0.15 | Limit | H5 | -98.763 | -96.686 | +2.077 |  |
| 116 | 2500 | -12 | 0.35 | 0.25 | Compress | H5 | -89.821 | -87.651 | +2.170 |  |
| 117 | 2500 | -12 | 0.35 | 0.25 | Compress | H7 | -93.137 | -95.594 | -2.457 |  |
| 118 | 2500 | -12 | 0.35 | 0.25 | Limit | H5 | -90.190 | -87.745 | +2.446 |  |
| 119 | 2500 | -12 | 0.35 | 0.25 | Limit | H7 | -93.545 | -96.134 | -2.589 |  |
| 120 | 2500 | -12 | 0.35 | 0.35 | Compress | H5 | -84.379 | -81.856 | +2.522 |  |
| 121 | 2500 | -12 | 0.35 | 0.35 | Compress | H7 | -86.711 | -89.146 | -2.436 |  |
| 122 | 2500 | -12 | 0.35 | 0.35 | Limit | H5 | -84.601 | -81.813 | +2.788 |  |
| 123 | 2500 | -12 | 0.35 | 0.35 | Limit | H7 | -87.213 | -89.781 | -2.568 |  |
| 124 | 2500 | -12 | 0.70 | 0.25 | Compress | H5 | -99.860 | -96.508 | +3.352 |  |
| 125 | 2500 | -12 | 0.70 | 0.35 | Compress | H5 | -93.049 | -89.705 | +3.344 |  |
| 126 | 2500 | -12 | 0.70 | 0.35 | Limit | H5 | -97.435 | -92.995 | +4.440 |  |
| 127 | 2500 | -8 | 0.35 | 0.25 | Compress | H5 | -91.285 | -85.582 | +5.703 |  |
| 128 | 2500 | -8 | 0.35 | 0.25 | Compress | H7 | -97.441 | -101.843 | -4.401 |  |
| 129 | 2500 | -8 | 0.35 | 0.25 | Limit | H5 | -91.458 | -85.595 | +5.864 |  |
| 130 | 2500 | -8 | 0.35 | 0.25 | Limit | H7 | -98.044 | -102.657 | -4.613 |  |
| 131 | 2500 | -8 | 0.35 | 0.35 | Compress | H5 | -84.947 | -79.844 | +5.103 |  |
| 132 | 2500 | -8 | 0.35 | 0.35 | Compress | H7 | -90.737 | -95.091 | -4.353 |  |
| 133 | 2500 | -8 | 0.35 | 0.35 | Limit | H5 | -84.968 | -79.623 | +5.345 |  |
| 134 | 2500 | -8 | 0.35 | 0.35 | Limit | H7 | -91.496 | -96.006 | -4.510 |  |
| 135 | 2500 | -8 | 0.70 | 0.35 | Compress | H5 | -97.332 | -92.214 | +5.118 |  |
| 136 | 2500 | -4 | 0.35 | 0.15 | Compress | H5 | -98.696 | -92.699 | +5.997 |  |
| 137 | 2500 | -4 | 0.35 | 0.15 | Limit | H5 | -99.078 | -92.982 | +6.096 |  |
| 138 | 2500 | -4 | 0.35 | 0.25 | Compress | H5 | -88.838 | -83.365 | +5.473 |  |
| 139 | 2500 | -4 | 0.35 | 0.25 | Limit | H5 | -89.148 | -83.615 | +5.533 |  |
| 140 | 2500 | -4 | 0.35 | 0.35 | Compress | H5 | -80.629 | -77.679 | +2.950 |  |
| 141 | 2500 | -4 | 0.35 | 0.35 | Limit | H5 | -81.381 | -77.652 | +3.729 |  |
| 142 | 2500 | -4 | 0.35 | 0.35 | Limit | H7 | -96.989 | -94.563 | +2.427 |  |
| 143 | 2500 | 0 | 0.35 | 0.15 | Compress | H5 | -97.000 | -91.343 | +5.658 |  |
| 144 | 2500 | 0 | 0.35 | 0.15 | Limit | H5 | -97.669 | -91.996 | +5.673 |  |
| 145 | 2500 | 0 | 0.35 | 0.25 | Compress | H5 | -86.834 | -81.908 | +4.926 |  |
| 146 | 2500 | 0 | 0.35 | 0.25 | Limit | H5 | -87.460 | -82.558 | +4.903 |  |
| 147 | 2500 | 0 | 0.35 | 0.35 | Compress | H5 | -77.718 | -75.319 | +2.399 | YES |
| 148 | 2500 | 0 | 0.35 | 0.35 | Compress | H7 | -98.591 | -91.782 | +6.810 |  |
| 149 | 2500 | 0 | 0.35 | 0.35 | Limit | H5 | -79.287 | -75.665 | +3.622 | YES |
| 150 | 2500 | 0 | 0.35 | 0.35 | Limit | H7 | -99.404 | -92.690 | +6.715 |  |
| 151 | 2500 | 0 | 1.00 | 0.15 | Limit | H2 | -98.966 | -96.330 | +2.636 |  |
| 152 | 2500 | 0 | 1.00 | 0.25 | Limit | H2 | -89.170 | -86.530 | +2.639 |  |
| 153 | 2500 | 0 | 1.00 | 0.35 | Limit | H2 | -82.242 | -79.625 | +2.617 |  |
| 154 | 2500 | 0 | 1.00 | 0.35 | Limit | H4 | -96.301 | -93.505 | +2.796 |  |
| 155 | 10000 | -28 | 0.70 | 0.35 | Compress | H2 | -95.023 | -92.720 | +2.303 |  |
| 156 | 10000 | -28 | 0.70 | 0.35 | Limit | H2 | -95.714 | -93.606 | +2.107 |  |
| 157 | 10000 | -4 | 0.00 | 0.35 | Compress | H2 | -54.413 | -59.299 | -4.885 | YES |
| 158 | 10000 | -4 | 1.00 | 0.15 | Limit | H2 | -95.036 | -99.614 | -4.577 |  |
| 159 | 10000 | -4 | 1.00 | 0.25 | Limit | H2 | -85.256 | -89.805 | -4.549 |  |
| 160 | 10000 | -4 | 1.00 | 0.35 | Limit | H2 | -78.369 | -82.925 | -4.556 | YES |
| 161 | 10000 | 0 | 1.00 | 0.15 | Limit | H2 | -93.603 | -100.416 | -6.813 |  |
| 162 | 10000 | 0 | 1.00 | 0.25 | Limit | H2 | -83.794 | -90.667 | -6.873 |  |
| 163 | 10000 | 0 | 1.00 | 0.35 | Limit | H2 | -76.906 | -83.714 | -6.809 | YES |

## Settled and actual-AU/core agreement

The committed R7 checkpoint was fully verified before source changes:

- full CTest: 20/20 pass in 512.01 seconds;
- actual-AU/core agreement: 122/122 settled cells within 0.01 dB, worst
  0.002172 dB;
- actual-AU music: 0.893370 dB overall / 1.220106 dB high PR.

The first R7 parity attempt was invalid because the harness compared the fresh
AU against a stale September 28 base JSON and reported 16/122. The harness now
requires a tag-matched verbose CTest log and refuses to score without exactly
122 freshly parsed core rows. Re-running against the committed R7 executable
produced the valid 122/122 result above.

On final R8, the core settled gate is **122/122**, worst UAD error 0.493227 dB
against the 0.5 dB bar. The final installed AU agrees with those exact core
rows on **122/122 within 0.01 dB**, worst AU-minus-core 0.002168 dB. This closes
the requested verification bridge: the core-executable settled result counts
as the plugin result for this binary.

The final harmonic residual is added after `meterCorrectionDb` has scaled the
existing audio. It arms only after the existing causal sine detector has enough
support, and PR positions at or below 0.10 select the PR-zero table. The first
implementation interpolated the PR-0.1 signal toward the PR-0.35 calibration;
the existing inactive-PR assertion caught it:

`FAIL: Opto PR 0.0 and 0.1 never compress`

After clamping inactive PR to the PR-zero slice, Compress and Limit both
measure 0.000000 dB at PR 0 and PR 0.1, and the complete suite passes. This
also checks the opposite path: active PR still uses the fitted PR surface.

## Music score

These are corrected causal `causal_hp20_v1` gain-reduction errors on the
non-sealed LEWITT VAL corpus, rendered through the actual MC-2 AU. The music
noise floors remain 0.089 dB overall and 0.132 dB high PR.

| Actual-AU path | Overall | High PR | Change from R7 | Interpretation |
|---|---:|---:|---:|---|
| TFU1 + G2 + G3 base | 0.825163 dB | 1.184023 dB | -0.068207 / -0.036083 | better than R7 |
| R6 tone-gated | 0.856163 dB | 1.188709 dB | -0.037207 / -0.031397 | within noise |
| R7 committed | 0.893370 dB | 1.220106 dB | reference | — |
| R8 final | 0.893413 dB | 1.220138 dB | +0.000043 / +0.000032 | no change |

R8 itself does not regress music relative to R7. The larger R7 regression
relative to the base remains and was not repaired in this harmonic-only round.
No sealed holdout or Untitled material was opened.

## Suite and build results

- Release CTest: **20/20 pass**, 0 failures, 526.38 seconds.
- Short-event charge gate: pass. Fitted RMS/worst 0.194691/0.442499 dB;
  held-out RMS/worst 0.314525/0.546488 dB.
- Release AU target: built and installed successfully.
- Apple `auval -v aufx DsMc Dusk`: **PASS**, including mono/stereo render,
  scheduled parameters and 512-frame rendering.
- Built and installed AU binary SHA-256:
  `b971036805c9bab11f9ed51c480bd56422dcfec10ff32b6d9cc057010ce218fc`.
- Actual-AU fitted map: 1,683/1,683 pass; worst 0.013457 dB.
- Actual-AU held-out map: 1,169/1,332 pass; worst 16.548089 dB.
- Actual-AU/core settled agreement: 122/122 pass; worst 0.002168 dB.

## Verdict and uncommitted files

R8 fixes the gain-law/calibration ordering defect, passes the complete fitted
harmonic map, preserves the owner's 1 kHz case, preserves all 122 settled cells
and leaves music unchanged relative to R7. It does **not** pass the new-frequency
prediction check: the log-frequency interpolation/extrapolation is not a valid
physical frequency law, especially for 300 Hz H5. This round is therefore a
measured improvement and a held-out negative result, not a claim of harmonic
parity at arbitrary frequencies.

Leave these files uncommitted for owner review:

- `plugins/multi-comp/core/MultiCompModes.hpp`
- `plugins/multi-comp/core/MultiCompOptoFinalHarmonics.hpp`
- `plugins/multi-comp/tests/fit_opto_final_harmonics.py`
- `plugins/multi-comp/tests/opto_au_harmonic_map.py`
- `plugins/multi-comp/docs/opto-r8-final-harmonics-2026-09-29.md`

No commit, branch operation or push was performed.
