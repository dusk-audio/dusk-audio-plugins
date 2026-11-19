# Modern faces: port the JUCE layouts (plan, 2026-10-19)

Studio FET, Studio VCA, Digital and Multiband must match the JUCE Multi-Comp editor (Marc,
2026-10-19). Today they are a row of knobs over an empty panel, and Oversampling, External
Sidechain, SC Listen, Analog Noise and SC HP are reachable only from host automation. This plan
says what to build; the measured JUCE spec it rests on is at the end, with file:line citations.

## What stays as it is

- The fleet header row (nameplate, presets, INIT, SAVE, DUSK AUDIO) and the mode tabs, AUTO
  MAKEUP and BYPASS row. The JUCE title band and its header rows are replaced by these, as the
  fleet rule requires ("mockups override the body only").
- Opto, FET, VCA and Bus: their hardware faces are not part of this work.
- Parameter IDs, ranges and state. This is UI only; no session changes.

## What each modern face gets (the JUCE frame)

Shared by Studio FET, Studio VCA and Digital, laid out like JUCE (meters at the sides, VU on top,
controls below):

| Element | JUCE | Binding |
|---|---|---|
| Mode title + tagline | "STUDIO FET COMPRESSOR" / "STUDIO VCA" / "DIGITAL COMPRESSOR"; tagline in the footer | none |
| Toggle row | Analog Noise, Ext SC, SC Listen (Bypass and Auto Gain already live in the mode row) | `noise_enable` (not in Digital), `sidechain_enable`, `global_sidechain_listen` |
| Oversampling | Off / 2x / 4x selector | `oversampling` |
| GR VU | gain reduction, -20..+3, click toggles a GR history graph | master GR meter |
| SC HP | vertical slider, 0..500 Hz | `sidechain_hp` |
| IN / OUT meters | 12-LED stereo bars with peak-held dB readouts | input / output level |

Per mode:

- **Studio FET:** INPUT, OUTPUT, ATTACK, RELEASE, MIX, then a 4:1 / 8:1 / 12:1 / 20:1 / All button
  row bound to `fet_ratio`. JUCE shows no Curve or Transient here (decision 3).
- **Studio VCA:** THRESHOLD, RATIO, ATTACK, RELEASE, OUTPUT, MIX in a dark red panel with a red
  top rule.
- **Digital:** Threshold, Ratio, Knee, Attack, Release / Lookahead, Mix, Output, Adaptive Release.
- **Multiband** (no VU, no SC HP): band enable strip (Low / Lo-Mid / Hi-Mid / High in the band
  colours; at least two stay on), "GAIN REDUCTION" with one 20-segment GR bar per enabled band and
  its solo "S" button, crossover faders between neighbouring bars (log drag, frequency label,
  double-click resets), a "<Band> Band" pill for the selected band, and one knob row: Threshold,
  Ratio, Attack, Release, Makeup for the selected band, then Output and Mix.

## JUCE behaviour not to copy (bugs in the original)

- Ratio buttons never read `fet_ratio` back, so they show 4:1 after a preset or automation. Ours
  follow the parameter.
- The VU arc is drawn with a different angle origin from its ticks and does not sit under them.
- The SC HP "Off" / "N Hz" text and the FET attack microseconds text were silently replaced by the
  attachment; ours show "Off", "80 Hz", "0.20 ms".
- The noise texture hides each mode's background colour; Digital's panel runs 5 px past the window
  bottom; tooltips are defined but never shown.

## Decisions for Marc before building

1. **Height.** (a) Keep the modern modes at 1120x486 and stack like JUCE (VU over controls).
   Closest to the original; the window still resizes on a mode change. (b) Fit them in 1120x380
   by using the width: IN meter, VU, controls, SC HP, OUT meter side by side. No resize on mode
   change, but further from the JUCE look. Multiband needs about 380 of body either way, so (b)
   means a tighter Multiband. Recommendation: (a), since "match the original" is the brief.
2. **Frame controls on the hardware faces.** Oversampling, Ext SC, SC Listen and Analog Noise act
   in Opto, FET, VCA and Bus too, where they stay automation-only. Add a compact strip for them in
   the mode row for every mode? Recommendation: yes, once, in the mode row, so no face hides them.
3. **Studio FET Curve and Transient.** JUCE has neither on the Studio FET face; ours do, and both
   are shared with FET. Drop them from the face (still automatable) to match? Recommendation:
   match JUCE.
4. **Multiband Mix.** JUCE's Multiband Mix knob edits the global `mix`, but our DSP mixes Multiband
   with `mb_mix`. Keep `mb_mix` behind the knob (labelled "Mix"). Recommendation: keep `mb_mix`.

## Build order and verification

1. Shared frame pieces (VU with history toggle, LED IN/OUT meters, SC HP slider, toggle row,
   Oversampling selector) as reusable drawers, first on Studio FET.
2. Studio VCA and Digital on the same frame.
3. Multiband.
4. Each face: headless screenshots at default and scaled size compared side by side with the JUCE
   render from REAPER on the Windows VM; UiDrag / Resize / plugin-layer tests extended to the new
   controls; REAPER Windows check of mode switching.

## JUCE spec (750x500 default, extracted 2026-10-19)

Files: E = EnhancedCompressorEditor.cpp, P = ModernCompressorPanels.h, A = AnalogLookAndFeel.cpp,
M = multicomp.cpp, D = shared/DuskLookAndFeel.h, L = shared/LEDMeter.cpp. Rectangles (x,y,w,h).

- Layout: hard-coded pixels x scale s = min(W/750, H/500); VU (164,97,422,165); controlArea
  (110,312,530,158); Digital (110,287,530,218); Multiband (110,97,530,388) (E:1128-1530).
- Knobs: label 22, knob 75 square, row 107; metallic body 0xFF4A4A4A, value text 13pt bold white
  under the knob (E:1354-1368, A:125-136, A:306-323).
- Toggles (row y=59, h=22, gap 6, widths Bypass 60, Auto Gain 73, Analog Noise 88, Ext SC 55,
  SC Listen 68). Studio FET ON 0xFF00E5E5..0xFF00A5A5; Studio VCA ON 0xFFDD4444..0xFFAA2222;
  Digital/Multiband ON 0xFF00DDFF..0xFF00AACC (A:145-213, 356-359, 484-487, 516-519).
- Titles/colours/taglines: Studio FET "STUDIO FET COMPRESSOR" 0xFFE0E0E0 "Modern FET | Clean with
  30% Harmonics | Versatile"; Studio VCA "STUDIO VCA" 0xFFCC9999 "RMS Detection | Soft Knee |
  Clean VCA Dynamics"; Digital "DIGITAL COMPRESSOR" 0xFF00D4FF "Transparent | Precise | Zero
  Coloration"; Multiband "MULTI-COMP" 0xFFE0E0E0 "4-Band Multiband Compression" (E:968-1045).
- SC HP: label (616,92,34,16), slider (619,108,28,151), `sidechain_hp` 0..500 Hz skew 0.5
  (E:1073-1078, 1320-1328, M:4605).
- IN/OUT meters: (34,114,32,350) / (684,114,32,350); 12 LEDs -60..+6 dB (0-6 green 0xFF4ade80,
  7-9 yellow 0xFFfde047, 10-11 red 0xFFf87171); attack/release 65 ms, peak hold 1.5 s; dB readout
  peak-held, decays 1.2 dB per update (E:1280-1299, L:31-38, 145-161, 247-264).
- VU: shows GR; -20..+3 dB, 0 dB at 0.87; attack 50 ms, release 150 ms, spring 200 / 0.65; peak
  dot held 2 s; click toggles "LEVEL (click)" / "GR HISTORY (click)" (128 samples, ~4 s, 0..-30 dB)
  (A:550-1122).
- Studio FET knobs x = 125, 231, 337, 443, 549 at y=334; ratio buttons (127+100i, 428, 96, 32),
  selected 0xFF00E5E5 gradient + glow, unselected 0xFF3A3A3A..0xFF252525 (E:1396-1422,
  A:1225-1307).
- Studio VCA panel = controlArea, gradient 0xff2a1518..0xff1a0d0f, 2 px 0xffcc3333 top rule;
  6 columns of 86, knobs y=362 (P:1532-1617).
- Digital: two rows of 102; row 1 x = 125..549 at y=309 (Threshold, Ratio, Knee, Attack,
  Release); row 2 Lookahead, Mix, Output; Adaptive Release toggle (433,436,96,24) (P:234-282).
- Multiband: band colours Low 0xffff6b6b, Lo-Mid 0xffffd93d, Hi-Mid 0xff6bcb77, High 0xff4d96ff;
  enable strip (147+122i, 103, 118, 20); tab (152+122n, 148, 81, 26) + solo (236+122n, 148, 24, 26);
  GR bar (152+122n, 182, 108, 150), 20 segments, colour ramps band -> 0xffffdd00 -> 0xffff8800 ->
  0xffff3333; dB scale 0..-20; crossover faders (239/361/483, 182, 56, 172), log drag,
  thumb 26x18, "N Hz" / "x.x kHz"; "<Band> Band" pill (305,363,140,24); knob row 7 x 75 at y=407,
  55 px knobs; GR ballistics 0.5 rise / 0.15 fall, peak held 30 frames (P:546-1433).
