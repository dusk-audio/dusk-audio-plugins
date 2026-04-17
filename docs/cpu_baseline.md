# DuskAmp CPU baseline

Measured on **macOS / Apple Silicon** at 48000 Hz / 256 samples / stereo. Each preset processes a 30 s synthetic DI; first 64 blocks discarded as warm-up.

Real-time budget at this block size: 5.333 ms per block.

Per-phase CPU gates use the worst preset's p95.

| # | Preset                  | Category | p50 ms | p95 ms | p99 ms | max ms |
|---|-------------------------|----------|--------|--------|--------|--------|
|  0 | Clean                   | Clean    | 0.050  | 0.052  | 0.061  | 0.107  |
|  1 | Sparkle                 | Clean    | 0.050  | 0.052  | 0.056  | 0.098  |
|  2 | Edge of Breakup         | Clean    | 0.048  | 0.052  | 0.060  | 0.161  |
|  3 | Blues Clean             | Clean    | 0.051  | 0.055  | 0.065  | 0.156  |
|  4 | Country Twang           | Clean    | 0.049  | 0.053  | 0.066  | 0.157  |
|  5 | Clean Ambient           | Clean    | 0.048  | 0.051  | 0.068  | 0.245  |
|  6 | Chime                   | Chime    | 0.054  | 0.057  | 0.072  | 0.181  |
|  7 | Jangle                  | Chime    | 0.053  | 0.057  | 0.059  | 0.149  |
|  8 | Chime Crunch            | Chime    | 0.054  | 0.057  | 0.070  | 0.154  |
|  9 | Desert Crunch           | Chime    | 0.056  | 0.059  | 0.070  | 0.154  |
| 10 | Boost Lead              | Chime    | 0.056  | 0.059  | 0.113  | 5.896  |
| 11 | Chime Shimmer           | Chime    | 0.054  | 0.057  | 0.065  | 0.150  |
| 12 | Stack Clean             | Crunch   | 0.056  | 0.106  | 0.225  | 6.545  |
| 13 | Crunch                  | Crunch   | 0.054  | 0.058  | 0.067  | 0.163  |
| 14 | Classic Rock            | Crunch   | 0.055  | 0.058  | 0.069  | 0.152  |
| 15 | Classic Lead            | Crunch   | 0.054  | 0.057  | 0.066  | 0.149  |
| 16 | Hot Rod                 | Crunch   | 0.059  | 0.115  | 0.135  | 3.387  |
| 17 | High Gain               | Crunch   | 0.056  | 0.059  | 0.072  | 0.170  |
| 18 | Shred                   | Crunch   | 0.057  | 0.061  | 0.079  | 0.227  |
| 19 | Sludge                  | Crunch   | 0.057  | 0.060  | 0.077  | 0.153  |
| 20 | Blues Harp              | Genre    | 0.052  | 0.054  | 0.063  | 0.133  |
| 21 | Indie Clean             | Genre    | 0.054  | 0.058  | 0.069  | 0.153  |
| 22 | Ambient Pad             | Genre    | 0.050  | 0.052  | 0.059  | 0.149  |
| 23 | Post-Rock Swell         | Genre    | 0.058  | 0.060  | 0.112  | 0.277  |
| 24 | 80s Arena Rock          | Genre    | 0.055  | 0.058  | 0.109  | 2.779  |
| 25 | Tape Echo Lead          | Genre    | 0.054  | 0.058  | 0.081  | 0.161  |
| 26 | Surf Rock               | Genre    | 0.050  | 0.052  | 0.062  | 0.130  |
| 27 | Metal Rhythm            | Genre    | 0.057  | 0.060  | 0.076  | 7.457  |
| 28 | Doom                    | Genre    | 0.057  | 0.060  | 0.074  | 0.144  |
| 29 | NAM Default             | NAM      | 0.002  | 0.002  | 0.002  | 0.040  |

**Worst preset (p95): Hot Rod at 0.115 ms.**
