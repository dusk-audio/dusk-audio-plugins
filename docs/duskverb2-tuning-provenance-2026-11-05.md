# DuskVerb 2 tuning provenance

Experimental measurements retained from the voicing-table comments. These notes are historical evidence, not fresh validation.

```text
// One row per preset with an early field. constexpr std::array and a linear scan, never a map:
// it is read on the audio thread at every preset swap.
//
// Tuned 2026-10-24 against the v1 anchors (tools repo classic_lab: earlyfield_screen.py finds the
// field on the preset's own renders, earlyfield_refine.py refines it with real renders through the
// tuning hook); a row is kept only where full_check's n_fail drops and no sanity gate (pre-onset,
// first arrival, transient definition) is added. Fields: enabled, startMs, windowMs, taps,
// slopeDb, attackMs, inject, directDb, reflDb, reflCount, reflMs, lowpassHz. Reverse Taps has no
// row: a field sounds ahead of its swell (the reference's first arrival is at 104 ms), which moves
// the first arrival.

    // 2026-10-26 Phase C stereo: only the side burst (anti-phase lows, 30 ms from 17 ms, 250 Hz), the
    // reference's opening low reflection; with the tank's lows at x0.8 of its native side
    // (kEnginePatchByName) the tail is no longer anti-phase. 10 -> 9: stereo_corr -0.84 -> -0.01
    // (ref -0.07) with width low kept (-0.92, ref -0.86).

    // 2026-10-26 Phase C attack: an onset burst at 28.9 ms (5.3 ms of velvet pulses, -36 dB) puts the
    // response peak where the reference's is (attack 36.6 -> 24.6 ms vs 22.6); with the octave trim's
    // 16 kHz cut eased (-1.53 -> -1.0 dB) for the gain match's share, 9 -> 7 (attack_time, bloom 4-8k).

// One row per preset with an octave trim: its nine bands' dB, 63 Hz first. Read at every preset
// swap, like the early-field table. Tuned 2026-10-25 on top of the early fields (tools repo
// classic_lab: earlyfield_screen.py --trim models this cascade exactly, trim_refine.py refines with
// real renders through DUSKVERB2_OCTAVETRIM); kept only where full_check's n_fail drops and no
// sanity gate is added.

    // 2026-10-27 Phase C7: re-searched with the re-voiced tank (kEnginePatchByName: new delays, no smear).

    // 2026-10-26 Phase C3 ring: a gentle tilt with the engine patch's densifier and band rows
    // (kEnginePatchByName), the tail's low-mids against its 1-2 kHz.

    // 2026-10-27 Phase C6 (revoice_trim vp_lowend): the low end, 100-250 Hz cold and < 50 Hz hot against
    // the reference: 16 -> 15, ss deep sub + low 100-250 close, body/boom/sine1k/flux nearer, cent_50 opens.

    // 2026-10-27 Phase C9: re-searched with the 1-3 kHz band's mode smear (kEnginePatchByName).

    // 2026-10-27 Phase C5 (with qt_air_mult 0.15, kPresetOverrides): the reference walls at ~8 kHz;
    // ss air +15.9 -> +1.6 dB, T60 16k comparable and in gate, cent_50 -9.7 -> -12.8 %. Opens only
    // bloom 2-4k, +1.49 -> +1.59 against its +1.5 ceiling (6 -> 6, delta_eff -1).

    // 2026-10-25 stereo parity with the VVV fat-snare-room anchor, with Mono Below 40 Hz
    // (kPresetOverrides): noiseburst tail L/R correlation +0.85 -> +0.24 (anchor +0.15), impulse
    // width low/mid/hi +0.15/+0.10/+0.14 -> -0.06/+0.11/+0.07 (anchor +0.05/+0.02/0.00);
    // full_check 20 -> 19. The shared table's 0.55/0.70 low widths were narrowed for the legacy
    // taps, whose undelayed side put a mono input's lows anti-phase.
    // 2026-10-26 Phase C3 ring (the 252 Hz tail resonance): bands 1 and 3 densified at 0.081 / 0.182
    // (at the tank's 0.55 their allpasses bunched the modes: the 1377/1395 and 1789/1809 Hz pairs,
    // the 239-257 Hz cluster), with the band levels and decays around it and the octave trim's tilt
    // (kOctaveTrimByName). Boing 21.9 -> 17.5 dB and its top-3 19.8 -> 16.7 (reference 13.5 / 13.4),
    // body 1-2k and T60 125 Hz closer, no gate opened, the attack untouched (hook_refine.py
    // mdr_ring5 t015). Closing both ring gates took the 1-4 kHz band levels up, which raised a 60 ms
    // tank arrival 2.9 dB over the early one: away from the reference's onset, so not taken.
    // 2026-10-27 Phase C6: mode smear 15 samples at 1 Hz on the 350 Hz-3 kHz bands only (the share row):
    // the tail drifts like the reference's (0.01 -> ~0.4 Hz at 1 kHz, REF 0.68), the whole-tank smear's
    // brightening stays out. 17 -> 15: tail pitch-chorus, body 1-2k, boing top-3 close; body 250-500 opens.

    // 2026-10-27 Phase C5: sparse ER low-pass 8 kHz. The DenseHall composite's broadband sparse ER
    // front is the early field here, and the reference sits 14 dB under it at 12.5-16 kHz through the
    // first 50 ms: ss air +2.8 -> +2.0 dB closes, nothing opens (13 -> 12). Its gain is no lever:
    // the front carries the attack (gain 0..0.3: 13..15).

    // 2026-10-25 tail ring (full_check boing gates): Live Room's 317 Hz Dattorro mode 26.1 dB over
    // its neighbours (reference 19.9) and Black Hole's 1051 Hz shimmer-tank mode 14.6 dB (reference
    // 10.2), smeared. Live Room 16 -> 13, Black Hole 20 -> 17.
    // 2026-10-26 Phase C stereo: the shared width-band row (lows x2.0 / mids x0.6 / highs x0.4 over
    // Width 1.5) splits with a one-pole pair, so the lows' anti-phase leaks up to ~1.6 kHz and runs
    // the whole tail anti-phase. The band stage puts the native side at lows x0.8 / mids x0.8 /
    // highs x1.0 (these gains over Width 1.5); the early anti-phase lows come from the early field's
    // side burst (kEarlyFieldByName). 12 -> 9: width mid, width hi, stereo_corr.

    // 2026-10-26 Phase C top octave: the jittered density allpasses' Hermite reads held T60 16k at
    // 1.03 s against a commanded 6.42 (ref 1.36); jitter 0 frees it, and the octave multipliers
    // re-land 125 Hz / 8k / 16k on the reference (1/3-oct 16k 0.57 -> 1.33 s). 7 -> 5: T60 125,
    // T60 16k, spec_L1 max (9.6 dB @ 12.9k) close; EDT low-mid 250-500 opens (0.19 -> 0.27 s).

    // 2026-10-26 Phase C stereo: the shared width-band row widens the highs x1.35 over Width 1.135,
    // which runs them anti-phase (-0.27 vs the reference's -0.11 in the impulse's first 500 ms); the
    // band stage at a uniform x0.969 (x1.1 of the native side) closes width hi.

    // 2026-10-27 Phase C9: mode smear 10 samples at 1 Hz on the 1-3 kHz band only, with a re-searched
    // trim row (kOctaveTrimByName). The tail was static (pitch-chorus 0.10 Hz against the reference's
    // 0.59); now 0.60. HF-tail texture (kurtosis 20.9 -> 12.0, reference 11.6), tail pitch-chorus and
    // full RMS snare close; T60 1 kHz (+10.5 %), body 125-250 (-1.56 dB) and body 250-500 (-2.37 dB)
    // open (13 -> 13; snare RMS, attack time and piano tail balance nearer). The whole-tank smear halves
    // T60 16 kHz (the cubic-Hermite loss of modulated reads); one on the 350 Hz-3 kHz bands loses level
    // at the 350 Hz band edge (body 250-500 -2.7 dB however the trim is held). 20 samples here reads
    // 1.49 Hz (11 fails, but 2.5 x the reference's drift).

    // 2026-10-27 Phase C11: the Shimmer hall's time-varying feedback matrix replaces its mode smear.
    // The smear (100 samples at 2 Hz, shared config) held the boing at the reference's 10.5 dB but
    // drifted the tail 1.12 Hz (reference 0.31). With the C8 delay set and no smear the hall rings at
    // 14.5-15.3 dB; turning each line pair by up to 1.57 rad at 0.7 Hz takes it back to 10.5 dB (mean
    // over six starts 11.9) at 0.60 Hz of drift. On mono input the turn costs the first second about
    // 3 dB at 20-40 Hz (Lo Cut 18.85 Hz, kPresetOverrides, puts it back). One render reads 6 -> 7: the
    // chorus closes, T60 125 Hz (+9.4 -> +10.8 %) and 8 kHz (+9.9 -> +10.4 %) open, but over six
    // starts those read +6.8 / +9.3 % against the current build's +6.7 / +9.4 %: the draw, not the turn.

    { "79 Vocal Chamber", "mod_depth", 0.647f },   // fleet screen 2026-10-25: 11 -> 7
    { "Black Hole", "hi_cut", 15141.4392f },   // fleet screen 2026-10-25: 27 -> 19
    { "Black Hole", "lo_cut", 34.427399f },   // fleet screen 2026-10-25: 27 -> 19
    { "Black Hole", "size", 0.71922f },   // fleet screen 2026-10-25: 27 -> 19
    { "Deep Blue Day", "mid_mult", 1.02f },   // fleet screen 2026-10-25: 17 -> 11
    { "Medium Drum Room", "lo_cut", 17.499996f },   // fleet screen 2026-10-25: 19 -> 17
    { "Tiled Room", "bass_mult", 1.51685f },   // fleet screen 2026-10-25: 17 -> 14
    { "Vintage Vocal Plate", "hi_cut", 18749.9982f },   // fleet screen 2026-10-25: 21 -> 14
    { "Vocal Hall", "high_crossover", 3452.854f },   // fleet screen 2026-10-25: 14 -> 13
    { "Vocal Plate", "crossover", 220.79024f },   // fleet screen 2026-10-25: 17 -> 15
    { "79 Vocal Chamber", "crossover", 898.375106f },   // fleet screen 2026-10-25: 7 -> 6
    { "Black Hole", "crossover", 227.5569f },   // fleet screen 2026-10-26: 17 -> 16 over the mode smear
    { "79 Vocal Chamber", "edt_lowmid_attack_db", 3.0f },   // fleet screen 2026-10-25: 6 -> 6, diffusion flux 8.6 -> 5.6 and EDT low-mid 3.2 -> 2.4 gate widths
    { "79 Vocal Chamber", "qt_air_mult", 0.15f },   // 2026-10-27 Phase C5 with its band limit (DuskVerbDv2Voicing.hpp): 0.22 -> 0.15, its ~11.5 kHz decay +25 % -> +4 %
    { "Ambience", "edt_midhi_attack_db", 3.0f },   // fleet screen 2026-10-25: 13 -> 12
    { "Deep Blue Day", "tank_level", 1.0976f },   // fleet screen 2026-10-25: 11 -> 10
    { "Drum Plate", "er_boost", 1.33f },   // fleet screen 2026-10-25: 9 -> 9, tail pitch-chorus 2.7 -> 1.9 gate widths
    { "Large Chamber", "tank_level", 0.85f },   // fleet screen 2026-10-25: 15 -> 14
    { "Live Room", "predelay", 15.0f },   // fleet screen 2026-10-25: 16 -> 15
    { "Medium Drum Room", "predelay", 8.1f },   // fleet screen 2026-10-25: 17 -> 16
    { "Reverse Taps", "edt_air_attack_db", 3.0f },   // fleet screen 2026-10-25: 14 -> 11
    { "Reverse Taps", "edt_lowmid_attack_db", 3.0f },   // fleet screen 2026-10-25: 14 -> 11
    { "Tiled Room", "edt_sub_attack_db", -3.0f },   // fleet screen 2026-10-25: 14 -> 13
    { "Vocal Hall", "predelay", 9.5f },   // fleet screen 2026-10-25: 13 -> 12
    { "Ambience", "edt_sub_attack_db", 2.4362f },   // fleet screen 2026-10-25: 12 -> 11
    { "Ambience", "er_decorr", 0.6128f },   // fleet screen 2026-10-25: 12 -> 11
    { "Cathedral Large Hall", "xtalk", 0.1665f },   // fleet screen 2026-10-25: 11 -> 11, snare level 4.1 -> 2.6 gate widths
    { "Drum Plate", "er_size", 0.4143f },   // fleet screen 2026-10-25: 9 -> 7
    { "Drum Plate", "mod_rate", 1.8f },   // fleet screen 2026-10-25: 9 -> 7 at 1.4642; 2026-10-27 1.8 closes tail pitch-chorus (0.07 -> ~0.3 Hz, REF 0.60): 5 -> 4
    { "Small Drum Room", "crossover", 617.7464f },   // fleet screen 2026-10-25: 19 -> 19, stereo_corr 3.7 -> 2.8 gate widths (the four together)
    { "Small Drum Room", "edt_midhi_attack_db", -3.6204f },   // fleet screen 2026-10-25: 19 -> 19, stereo_corr 3.7 -> 2.8 gate widths (the four together)
    { "Small Drum Room", "edt_sub_attack_db", -3.0f },   // 2026-10-27 Phase C5 (decay-curve EDT judge), with the next: 21 -> 19, decay low 100-250 + edt hi 2-8k close, nothing opens
    { "Live Room", "edt_lowmid_attack_db", -3.0f },   // 2026-10-27 Phase C5: 11 -> 9, boom low 0.5-1s + edt low_mid + edt mid close, boom sub 40-100 0.5-1s opens
    { "Large Chamber", "lo_cut", 32.0f },   // 2026-10-27 Phase C5: 40 -> 32 Hz, 17 -> 16, deep-sub 20-40 closes, ss deep sub nearer, nothing opens
    { "Small Drum Room", "hi_cut", 3555.3112f },   // fleet screen 2026-10-25: 19 -> 19, stereo_corr 3.7 -> 2.8 gate widths (the four together)
    { "Small Drum Room", "width", 1.135f },   // fleet screen 2026-10-25: 19 -> 19, stereo_corr 3.7 -> 2.8 gate widths (the four together)
    { "Vintage Gold Plate", "edt_midhi_attack_db", -5.9805f },   // fleet screen 2026-10-25: 10 -> 10
    { "Vintage Gold Plate", "hi_cut", 11298.3925f },   // fleet screen 2026-10-25: 10 -> 10
    { "Vintage Gold Plate", "hi_cut_shelf_db", -12.7646f },   // fleet screen 2026-10-25: 10 -> 10
    { "Vintage Gold Plate", "mod_depth", 0.0122f },   // fleet screen 2026-10-25: 10 -> 10, attack 4.2 -> 2.8 gate widths (the four together)
    { "Vocal Plate", "hi_cut_shelf_db", -5.2241f },   // fleet screen 2026-10-25: 15 -> 14
    { "Deep Blue Day", "lo_cut", 18.8475f },   // 2026-10-27 Phase C11: 26.9 -> 18.85 Hz with the feedback matrix (DuskVerbDv2Voicing.hpp), deep-sub 20-40 -3.3 -> -1.4 dB, nothing else moves
```

```text
    // 2026-10-27 Phase C7 re-voice. The shared config's mode smear (80 samples at 2.5 Hz, July's cure
    // for a 203-252 Hz tail ring) detuned the whole tail: pitch-chorus 1.71 Hz against the reference's
    // 0.10. Smear off, on a delay set whose tail is diffuse without it (delay_boing_search r026: boing
    // top-1/top-3 14.5/14.4 dB, reference 14.0/12.8), with the octave decay recalibrated on that set
    // (63 Hz x1.457 lifts the 40-100 Hz tail onto the reference's) and a new trim row. 11 -> 10:
    // chorus, boom sub 1-2s, decay sub, sub-bass, cent_500 and sine1k close; T60 125 Hz, piano RMS
    // and band balance, spec_L1 max (6.0 dB @ 202 Hz) and width low open. T60 125 against the boom
    // 80-200 Hz gates is the reference's lumpy EDC there (sparse beating modes): its -5..-25 dB fit
    // reads 1.53 s, its 10 dB crossings 1.28-1.35 s, and no single decay of DuskVerb's smoother tail
    // meets both (x0.86 reads 1.29 s, still 6 dB over the reference at 1-2 s).

                            { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 8000.0f } },   // fleet screen 2026-10-25: octave decay
    { "Tiled Room", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.15f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } } },   // fleet screen 2026-10-25: octave decay
    { "Vintage Vocal Plate", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.15f, 1.0f, 0.85f, 1.0f, 1.0f, 1.15f, 1.0f, 1.0f, 1.0f } } },   // fleet screen 2026-10-25: octave decay
          0.36f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f, 20.0f, 20000.0f, 1.0f, false, -1.0f },  // level match 2026-10-28 (kPresetOverrides): snare +1.0 LU
          0.46f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f, 20.0f, 20000.0f, 1.0f, false, -0.7f },  // level match 2026-10-28 (kPresetOverrides): snare +0.7 LU
```
