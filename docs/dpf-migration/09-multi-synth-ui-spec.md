# 09 — Multi-Synth UI Specification (Dear ImGui / ImDrawList)

> Product name: **Sunset Circuits** (renamed from Multi-Synth pre-release; slug `sunset-circuits`).
> Filename and internal class/namespace names kept for history; shipping product is Sunset Circuits.

**Companion to** `09-multi-synth.md` (design authority) and `09-multi-synth-inventory.md`
(read-only ground truth). This file is the **implementation blueprint** for
`dpf-plugin/MultiSynthUI.cpp`. It expands the "UI spec" section of `09-multi-synth.md`
into pixel-exact regions, per-mode skins, per-widget behavior, a full tooltip table for
every parameter, an interaction map, rendering/performance notes, and a build order.

An implementer should never have to invent a coordinate, a color, a format string, or a
tooltip: they are all here. Anything genuinely deferred is called out as **[v2]**.

**Hard rules inherited from the playbook and `09-multi-synth.md`:**
- Fixed design space **1240 × 780**, uniformly scaled (tape-echo pattern): `s =
  min(winW/1240, winH/780)`, `org = (0.5*(winW-1240*s), 0.5*(winH-780*s))`, every draw
  goes through `P(x,y) = org + (x,y)*s`.
- **All rendering is custom `ImDrawList`.** The only stock ImGui *rendering* widgets
  permitted are `BeginCombo`/`Selectable` (dropdowns) and `InputText` (the shared inline
  value editor). `ImGui::InvisibleButton` is explicitly permitted for interaction /
  hit-targets (it draws nothing — all visuals are still custom `ImDrawList`), and is used
  pervasively for that purpose in §8.
- Fonts via `duskdpf::loadCrispFontSet` at design sizes `× getScaleFactor()`; bold TTF
  with the shared candidate/fallback list (`DuskImGuiFont.hpp`). Never a bare
  `loadCrispFont` for a UI this size — build a multi-size set (see §9).
- Reuse `duskdpf::DuskPanel` (knob/LED/toggle/text/valueBubble/valueEdit/curvePoint) and
  `duskdpf::RealFFT` from `DuskImGuiWidgets.hpp`. New widgets in this spec are additions
  to a local UI class, not forks of the shared panel, except where §8 says to extend the
  shared panel.
- **No third-party trademarks anywhere** — no Juno/Moog/Prophet/DX/OB/303/… in code,
  labels, tooltips, comments, or preset names. Codenames + generic descriptions only.
- Meters, scope samples, and the live arp/seq step index come **only** through the
  weak-symbol bridge `MultiSynthAccess.hpp` (playbook landmine 2); fall back to output
  params when the symbol is null (split LV2 UI).

---

## 1. Region map (exact pixel rects, design space 1240 × 780)

All rects are `(x0,y0)–(x1,y1)` in **design-space** pixels. Panels are drawn with a 3 px
brushed-metal outer bevel then a recessed inner fill (see §7 draw order). "Center" for a
knob is its pivot; radius is design-space.

### 1.1 Top bar — `(0,0)–(1240,54)`
| Element | Rect / center | Notes |
|---|---|---|
| Chassis header fill | `(0,0)–(1240,54)` | `bg` darkened 10%, 3 px metal top edge, 1.5 px hairline at y=54 |
| Nameplate "MULTI-SYNTH" | text box `(18,10)–(250,44)` | 20 px bold, `text`; sub "Dusk Audio" 13 px right-aligned at x=1222,y=8 |
| Mode rockers ×6 | strip `(300,8)–(940,48)` | rocker *i* at `x=306+i*106`, width **100**, `y 10..46` (§4) |
| Preset ◀ prev | `(952,14)–(978,42)` | LED-button style |
| Preset combo | `(982,14)–(1150,42)` | styled `BeginCombo`, per-mode accent header (§8) |
| Preset ▶ next | `(1154,14)–(1180,42)` | |
| Save ★ | `(1186,14)–(1222,42)` | opens user-preset save [v2 = writes via UserPreset bridge] |

### 1.2 Body — `y 60..518`

> **Layout revision (lower body row):** the four lower-body panels that used to end
> at `y=542` now end at **`y=518`**, freeing 24 px for a taller SEQUENCER strip
> (§1.3). Upper panels (OSC 1/2, FILTER, LFO 1/2, SCOPE) are unchanged, as is the
> OSC 3 / SUB panel (which already ended at `410`). Bone structure is preserved.

**LEFT column — Oscillators / Mixer — `x 16..340`**
| Panel | Rect | Contents |
|---|---|---|
| OSC 1 | `(16,60)–(340,178)` | wave combo, Detune, PW, Level knobs |
| OSC 2 | `(16,182)–(340,300)` | wave combo, Semi (stepped knob), Detune, PW, Level |
| OSC 3 / SUB | `(16,304)–(340,410)` | **mode-variant** (see §4): Modular→osc3 wave+level; Cosmos/Mono→sub wave+level; else dimmed/hidden |
| VOICE / CHARACTER | `(16,414)–(340,518)` | 3 compressed rows (r9 knobs / comboH9 combos, font-8 labels); row centres `y{449,479,508}`, labels `y−19`, divider `y=459`. Noise/Analog/Vintage/Tune + Unison/Porta/Glide + Legato/Vel/V.Crv/PB |

**CENTER column — Filter + Envelopes — `x 348..752`**
| Panel | Rect | Contents |
|---|---|---|
| FILTER | `(348,60)–(752,300)` | curve display `(360,74)–(742,180)`; **oversized cutoff** center `(426,244)` r**54**; Res `(556,232)` r30; Env Amt `(636,232)` r30; HP `(712,232)` r30 (Cosmos only) |
| AMP ENV | `(348,304)–(548,518)` | ADSR display `(356,320)–(540,396)`; knobs A/D/S/R centers `x{380,426,472,518} y=462` r18 (labels `y=424`); Curve combo `(360,492)–(536,516)` |
| FILTER ENV | `(552,304)–(752,518)` | ADSR display `(560,320)–(744,396)`; knobs centers `x{584,630,676,722} y=462` r18 (labels `y=424`); Curve combo `(564,492)–(740,516)` |

**RIGHT column — LFOs / Mode sub-panel / Scope / Output — `x 760..1224`**
| Panel | Rect | Contents |
|---|---|---|
| LFO 1 | `(760,60)–(1000,190)` | Rate, Fade knobs; Shape combo; Sync toggle |
| LFO 2 | `(760,194)–(1000,324)` | same |
| MODE SUB-PANEL | `(760,328)–(1000,462)` | **morphs per mode** (§4): chorus / poly-mod / ring+sync / S&H / algo thumbnails / acid globals |
| MOD MATRIX bar | `(760,466)–(1000,518)` | "MOD MATRIX" LED-button `(768,474)–(992,510)` → opens overlay (§1.4); shows count of active slots |
| SCOPE | `(1004,60)–(1224,300)` | oscilloscope from ring buffer |
| OUTPUT / VU | `(1004,304)–(1224,518)` | stereo VU bars `y 338..498` + Master Vol, Pan, Width knobs |

### 1.3 Bottom strip — `y 524..692`
| Panel | Rect | Contents |
|---|---|---|
| SEQUENCER | `(16,524)–(700,692)` | taller transport header `y 528..572` (r15 mini-knobs OCT/GATE/SWING centres `x{398,440,482} y=555`; ARP/LATCH LED-buttons; MODE/RATE/VEL combos `y 540..562`; Fixed-VEL knob `x680` when VEL=Fixed) + step lanes. **Acid** expands to 3 lanes (§4.6) |
| FX · Drive | `(708,552)–(834,688)` | enable LED-button, Type combo, Amount, Mix |
| FX · Chorus | `(838,552)–(964,688)` | enable, Rate, Depth, Mix |
| FX · Delay | `(968,552)–(1094,688)` | enable, Sync toggle, Time/Div (context knob), FB, Mix, PingPong+Tape LED-buttons |
| FX · Reverb | `(1098,552)–(1224,688)` | enable, Size, Decay, Damp, Mix, PreDelay |

### 1.4 Overlays (drawn last, above everything)
| Overlay | Rect | Contents |
|---|---|---|
| MOD MATRIX | modal `(220,120)–(1020,660)` over a `IM_COL32(0,0,0,150)` full-window scrim | 8 rows × {Source combo, arrow, Dest combo, Amount bipolar bar-knob, clear-row ×}; title + close ✕ `(988,128)–(1012,152)` |
| KEYBOARD SHORTCUTS `?` | reuse Multi-Q help card pattern [v2] | optional |

### 1.5 Keyboard — `(16,700)–(1224,780)` (always visible)
- OCT− / OCT+ buttons reserved `x 16..48` (two stacked, `(16,700)–(48,738)` / `(16,742)–(48,780)`).
- Playable keys region `x 52..1224`, **3 octaves** = 21 white keys. White key width
  `w = (1224-52)/21 ≈ 55.81`. White keys full height `y 700..780`; black keys width
  `0.62*w`, height `700..750`, centered on white-key boundaries at the standard C#,D#,F#,G#,A# offsets.
- Base octave from OCT buttons; default lowest key = **MIDI 48 (C3)**, span C3..B5.

### 1.6 ASCII layout diagram

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│ MULTI-SYNTH   [COSMOS][ORACLE][MONO][MODULAR][PRISM][ACID]   ◀ [ Preset ▾ ] ▶  ★  Dusk  │ 0..54
├───────────────┬───────────────────────────────┬─────────────────────────────────────────┤
│  OSC 1        │        FILTER                  │  LFO 1                 │   SCOPE          │
│  wave det pw  │  ┌────filter-curve────────┐    │  rate fade shape sync  │   /\  /\  /\     │
│  level        │  │                         │   ├────────────────────────┤   \/  \/  \/     │ 60
│───────────────┤  └─────────────────────────┘  │  LFO 2                 │                  │ ..
│  OSC 2        │        ( CUTOFF )   Res EnvA HP│  rate fade shape sync  ├──────────────────┤ 300
│  wave semi ...│         big r54               │├────────────────────────┤   OUTPUT / VU    │
│───────────────┤                               ││  MODE SUB-PANEL        │  ▮▮   vol pan wid │
│ OSC3 / SUB    ├───────────────┬───────────────┤│ (chorus/polymod/algo…) │  ▮▮              │ 304
│ (mode variant)│   AMP ENV     │  FILTER ENV   ││                        │                  │ ..
│───────────────┤  /\___ ADSR   │  /\___ ADSR   │├────────────────────────┤                  │ 542
│ VOICE/CHAR    │  A D S R  crv │  A D S R  crv ││ [   MOD MATRIX   ]     │                  │ ..518
├───────────────┴───────────────┴───────────────┴┴────────────────────┬───┴──────────────────┤
│  SEQUENCER  [1][2][3][4][5][6][7][8][9]..[16]                        │ DRV │ CHO │ DLY │ REV │ 524
│  (Acid: +pitch lane  +accent lane  +slide lane)                      │     │     │     │     │ ..692
├─────────────────────────────────────────────────────────────────────┴─────┴─────┴─────┴─────┤
│OCT│ ▌▐▌▐▌▌▐▌▐▌▐▌  ▌▐▌▐▌▌▐▌▐▌▐▌  ▌▐▌▐▌▌▐▌▐▌▐▌   (3-octave clickable keyboard → MIDI notes)   │ 700..780
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Parameter → control binding

Every param from inventory §1, plus the new Prism/Acid params from `09-multi-synth.md`,
maps to exactly one on-screen control. The DPF param enum is generated from the X-macro
list in `MultiSynthParams.hpp`; the UI includes that header and indexes `values[kParamX]`
exactly like `TapeEchoUI`. Total **222 core params** (134 ported + 38 Prism + 2 acid
globals + 48 seq step rows). Controls:

- **Knob** (chrome, `DuskPanel::knob`): all continuous floats + stepped ints (`stepped=true`).
- **Combo** (`BeginCombo`): all Choice params (waves, curves, shapes, modes, drive type,
  arp mode, arp rate, mod src/dst, prism algo has its own diagram widget instead).
- **LED-button** (§8.2): all Bool params (enables, sync, latch, PP, tape, legato, hardSync).
- **Step cell** (§8.4): `arpStep0..15`, and the Acid `seqAccent/seqSlide` toggles.
- **Pitch-lane drag** (§4.6): `seqPitch0..15`.
- **Mode rockers** (§8.1): `mode`.

---

## 3. Widget specifications

### 3.1 Knob (extends `DuskPanel::knob`)
- **Sizes**: oversized cutoff **r54**; section primary r30; ADSR/compact r18; mode/FX
  mini r16. Arc sweep is the shared `knobAngle`: **−135° … +135°** (270° total).
- **Accent value arc**: the shared knob draws a chrome body + pointer. Multi-Synth adds a
  **mode-accent arc** overlay from −135° to the current angle: `dl->PathArcTo(center,
  R+3, start, cur)` stroked 2.4·s in the live `accent` color (crossfaded, §5). Bipolar
  params (`osc*Detune`, `masterPan`, `filterEnvAmt`, `bass/treble`-style, `op*` fine,
  mod amounts) draw the arc from the **12 o'clock center** to current, so negative fills
  left. Pass a `bipolar` flag to a thin wrapper `knobA()` that draws the arc after the
  shared `knob()` returns.
- **Label + value readout**: label above via `knobLabel`; live value via the shared
  `valueBubble` on hover/drag (`name=` set so hover shows the parameter name, drag shows
  the value). Format strings per type — **table §3.1a**.
- **Double-click** → inline type-entry (shared `valueEdit`). **Ctrl/Cmd-click** →
  reset to default (`kParamDefaults`). **Shift-drag** → fine (0.0008 vs 0.005 range/px,
  shared). **Wheel** → ±2% range (±1 step for stepped). All already in the shared knob.
- **`dispMul/dispAdd`**: use for Hz-in-kHz or normalized→dB display where helpful; most
  params display in native units.

#### 3.1a Value formats
| Param family | fmt | suffix | notes |
|---|---|---|---|
| Frequencies (cutoff, HP, LFO/S&H rate, chorus rate) | `%.0f` <1000 / `%.2f` kHz | ` Hz`/` kHz` | switch to kHz above 1000 via wrapper |
| Cents (detune, tune, unison detune, op fine) | `%+.0f` | ` ct` | bipolar |
| Semitones (osc2 semi, seq pitch, pb range) | `%+.0f`/`%.0f` | ` st` | stepped |
| Times (ADSR, porta, fade, delay time, predelay, slide) | `%.0f` ms <1 s / `%.2f` s | ` ms`/` s` | |
| Normalized 0..1 (levels, res, depth, mix, amounts, accent) | `%.0f` | ` %` | `dispMul=100` |
| Bipolar −1..1 (env amt, pan, mod amt, keyscale) | `%+.0f` | ` %` | `dispMul=100` |
| dB (master vol) | `%+.1f` | ` dB` | |
| Op ratio | `%.2f` | `×` | snap list, see §4.5 |
| Reverb decay | `%.1f` | ` s` | |

### 3.2 Mode rocker (`mode`) — §8.1.

### 3.3 Toggle / LED-button (`DuskPanel::toggle` + LED variant) — §8.2.

### 3.4 Step cell — §8.4.

### 3.5 ADSR display widget
- **Purpose**: draw the amp/filter envelope shape from `A,D,S,R` (+`curve`) so the user
  reads the contour at a glance. **v1 = display only** (knobs edit); draggable handles = **[v2]**.
- **Geometry** inside display rect `(rx0,ry0)–(rx1,ry1)`, width `W`, height `H`:
  - Normalize times against a fixed visual budget so short envelopes stay legible:
    map each stage time `t` (seconds) to width via `w = W_stage_max * (t / (t + tRef))`
    with `tRef = 0.6 s`, `W_stage_max` = 0.30·W for A/D/R and a fixed 0.10·W hold plateau
    for S. (Compressive so 0.01 s and 10 s both render on-panel.)
  - Points (y down): P0 `(rx0, ry1)`; Pa `(rx0+Wa, ry0)` (attack to peak); Pd
    `(Pa.x+Wd, ry1 - S*H)` (decay to sustain level); Ps `(Pd.x+Wsus, Pd.y)` (sustain
    plateau); Pr `(Ps.x+Wr, ry1)` (release to zero).
  - **Curve shaping**: subdivide A/D/R segments into ~12 points each, applying the same
    `applyCurve` used by the DSP (Linear / x² Exp / √x Log / AnalogRC `1−e^(−3t)`) so the
    drawn contour matches what is heard. Attack uses `applyCurve(t)`, decay/release use
    `1−applyCurve(t)`.
  - Draw: filled polygon under the curve at `accent` @ 40 alpha, curve polyline `accent`
    2·s, node dots r2 at Pa/Pd/Pr. Baseline + peak gridlines at 15 alpha.
- **Cost**: ~40 points, recomputed only when any of the 5 params changes (dirty flag).

### 3.6 Filter-curve display
- **Purpose**: magnitude response of the active filter model, updated with cutoff/res/HP.
- **X axis**: log frequency 20 Hz … 20 kHz across the display rect (reuse
  `DuskPanel::curvePoint` mapping). **Y axis**: −24 … +18 dB.
- **Cheap magnitude model** — evaluate an analog prototype at ~180 x-pixels. Let
  `w = f / cutoff` (normalized), `k` = resonance feedback mapped per model, `N` = poles:
  - 4-pole models (Cosmos/Oracle/Mono/Modular): `Hden = (1 + j w)^N + k`, `|H| = 1/|Hden|`,
    with `N=4`. Compute `(1+jw)^4` by squaring `(1+jw)^2 = (1−w²) + j(2w)` once.
  - Acid 3-pole: `N=3`, `Hden = (1+jw)^3 + k`.
  - **Resonance→k** per model, matching FilterEngine `maxFeedback`: Cosmos `k = res*3.0`
    (clamp res≤0.75 → k≤2.25, no self-osc peak); Oracle `k = res*4.2`; Mono `k = res*4.0`;
    Modular `k = res*3.8`; Acid `k = res*3.2`. (These mirror the DSP tuning constants so
    the curve tracks the ear; exact self-osc infinity is clamped to +18 dB for display.)
  - **Cosmos HP**: multiply by first-order high-pass magnitude `|jwh/(1+jwh)|` with
    `wh = f/filterHP`.
  - **Env-amt hint** [optional]: draw a faint ghost curve at `cutoff·2^(filterEnvAmt)` to
    show the envelope's reach.
- Convert `|H|` to dB, clamp to axis, build polyline. Draw filled area under curve at
  `accent` @ 22 alpha + polyline `accent` 2·s. Frequency gridlines at 100/1k/10k with
  faint labels; a small dot marker at the current cutoff on the curve.
- **Cost/caching**: recompute polyline only when `cutoff|res|HP|mode` changes (dirty
  flag); ~180 complex mults, negligible, but never per-frame unconditionally.

### 3.7 Oscilloscope
- **Source**: ring buffer of post-master samples via the access bridge
  `multiSynthGetScope(inst, float* dst, int n)` (fills up to 512, returns count); fall
  back to flat line if the symbol is null.
- **Trigger**: rising zero-crossing search over the first ¼ of the buffer for a stable
  display; if none found, draw from index 0.
- **Draw**: baseline at mid-height; polyline of `min(count, displayW)` points scaled to
  `±0.9·halfHeight` at `accent` 1.6·s. Faint center line + frame. Peak-hold dot [optional].
- **Cost**: one polyline ≤ 220 points/frame. Cheap.

### 3.8 Stereo VU
- **Source**: `multiSynthGetOutputLevelL/R(inst)` (weak; fall back to `outLevelL/R`
  params). Two vertical bars in the OUTPUT panel, each `~24` wide.
- **Ballistics**: per channel `disp += (target − disp) * (1 − e^(−dt·k))` with **attack
  k≈18, release k≈5** (asymmetric: rise fast, fall slow — VU-ish). Target = linear level.
- **Scale**: map to bar height over **−40 … +6 dB** (`h = (dB+40)/46`). Segment coloring:
  green `< −6 dB`, amber `−6..0`, red `> 0`. Peak-hold tick per channel, decays after
  ~1.2 s. Clip LED per channel latches on `>0.999` for ~0.5 s.

### 3.9 Combo styling (`BeginCombo`)
Copy the tape-echo pattern (its `drawHeader` combo): push `ImGuiCol_FrameBg`
`IM_COL32(38,38,41,255)`, `ImGuiCol_PopupBg` `IM_COL32(24,24,26,255)`, and
`ImGuiCol_Header` = **live mode accent** at reduced alpha so the highlighted item matches
the skin. Set item width to the control rect. Place with `SetCursorScreenPos(P(x,y))`.
For Acid's silver panel, use dark frame `IM_COL32(70,72,78,255)` and light text.

### 3.10 Tooltip system
Every control passes a `tooltip=` string to the shared knob, or (for combos/buttons)
calls `ImGui::SetTooltip("%s", kTip[param])` when `ImGui::IsItemHovered()`. Tooltips live
in one `static constexpr const char* kTooltips[kParamCount]` array indexed by param enum —
see the **full table §6**. Hover delay = ImGui default; no tooltip while dragging.

---

## 4. Per-mode skins and sub-panels

Six modes share the **identical bone structure** (§1) so users keep bearings. What changes
per mode: (a) the **palette** (crossfaded, §5); (b) the **MODE SUB-PANEL** contents; (c)
**inline visibility** of a few controls in the standard panels; (d) section-label accent.

### 4.0 Palettes (exact hex)
Colors are `#RRGGBB`; LED uses `IM_COL32`. `text` is the on-dark-panel ink; Acid also
needs `textOnPanel` because its panel is light.

| Mode | background | panel | accent | text | LED (on) | LED (off) |
|---|---|---|---|---|---|---|
| **Cosmos** | `#14161C` | `#1E2229` | `#E8C89A` | `#EFEAE0` | `#FF4B2E` | `#3A1712` |
| **Oracle** | `#1A130E` | `#241A12` | `#C8A15A` | `#EDE3CE` | `#FFB020` | `#3A2A0E` |
| **Mono** | `#0E0E10` | `#17181B` | `#C0C6CC` | `#E6E8EA` | `#FF3838` | `#3A1414` |
| **Modular** | `#121314` | `#1C1E20` | `#7FC8A9` | `#DDE2E0` | `#66E0A0` | `#123A2A` |
| **Prism** | `#071618` | `#0C2226` | `#2FD9C9` | `#CFEFEA` | `#24E0D0` | `#0E3A38` |
| **Acid** | `#16171A` | `#C8CBD0` | `#FF5A00` | `#EDEFF2` | `#FF2A2A` | `#5A1414` |

Notes:
- **Cosmos** cream accent, red/orange section markers (LED red). Warm-white section labels.
- **Oracle** walnut/black with decorative wood side cheeks (draw two `#2A1C10` vertical
  bands `x 0..14` and `x 1226..1240`, subtle grain lines); amber LEDs, cream text.
- **Mono** black/silver, minimal; the big cutoff knob dominates (accent silver arc).
- **Modular** grey with **decorative patch-jack styling**: draw small jack rings
  (`AddCircle` r6 dark + inner r3) at panel corners and cable strain-reliefs; two or three
  faux patch cables (bezier `AddBezierCubic`) in muted colors purely decorative, never
  interactive. Mint accent.
- **Prism** dark teal "membrane" aesthetic: panels flatter (smaller bevel), labels in a
  lighter teal; the algorithm diagram is the visual hero.
- **Acid** **silver panel** (light `#C8CBD0`) with round colored buttons; text on the
  silver panel uses `textOnPanel = #202226`. Value bubbles keep their light style.

Store as a `Palette` per mode (extend `duskdpf::Palette` with `background`, `panel`,
`textOnPanel`); `panel.setPalette(livePalette)` each frame after crossfade blend.

### 4.1 Cosmos (mode 0) — 6-voice DCO poly
- **Inline**: SUB panel shows `subWave` combo + `subLevel`; FILTER shows the **HP** knob;
  `crossMod` knob visible (Cosmos+Oracle). OSC2 wave forced-display "Pulse" (engine
  overrides), still selectable.
- **MODE SUB-PANEL** = **CHORUS**: three round LED-buttons **I**, **II**, **I+II**
  driving `cosmosChorus` (Off = none lit), laid at `(780,352)`, `(860,352)`, `(940,352)`
  r16; label "BBD CHORUS". A small animated shimmer bar under active buttons.

### 4.2 Oracle (mode 1) — 5-voice poly, poly-mod
- **Inline**: `crossMod` visible; no sub, no HP; filter self-oscillates (res arc reaches
  peak). The OSC3/SUB panel is inactive in this mode and shows a dimmed
  "(not used in this mode)" note.
- **MODE SUB-PANEL** = **POLY-MOD**: four r18 knobs in a 2×2 grid with a routing glyph:
  `pmFenvOscA` (FEnv→OscA freq), `pmOscBOscA` (OscB→OscA freq), `pmOscBPWM` (OscB→PW),
  `pmFenvFilt` (FEnv→Filter). Draw tiny source→dest arrows between labels. Title "POLY-MOD".

### 4.3 Mono (mode 2) — aggressive mono
- **Inline**: SUB panel shows `subWave`+`subLevel`; **the cutoff knob reads as the hero**
  (already r54); `ringMod` knob + `hardSync` LED-button visible.
- **MODE SUB-PANEL** = **RING / SYNC**: `ringMod` r22 knob + `hardSync` LED-button +
  a Sub section reminder; title "RING · SYNC". Silver round buttons.

### 4.4 Modular (mode 3) — semi-modular
- **Inline**: OSC3/SUB panel shows `osc3Wave`+`osc3Level`; `ringMod`, `hardSync`,
  `fmAmount` visible; spring reverb auto-engaged indicator in FX·Reverb (shows "SPRING").
- **MODE SUB-PANEL** = **S&H + PATCH**: `shRate` r22 knob (S&H clock), an animated S&H
  staircase mini-scope fed by the S&H LFO value (from bridge or synthesized), plus
  decorative patch jacks/cables framing the panel. Title "SAMPLE & HOLD".

### 4.5 Prism (mode 4) — 4-operator FM
This mode **re-skins the LEFT column** into an **OPERATOR MATRIX** and puts the
**algorithm widget** in the MODE SUB-PANEL. Standard oscillator panels (OSC1/2/3/SUB) are
hidden while Prism is active; MIXER/CHARACTER panel stays (noise/analog/vintage/tune).

- **Operator strips** — 4 stacked rows in `(16,60)–(340,410)`, each row `~86` tall:
  row *i* at `y = 62 + i*87`. Each strip: "OP n" label + LED (carrier lit brighter), and
  compact r16 knobs: **Ratio**, **Fine**, **Level**, **Vel**, **A**, **D**, **S**, **R**
  (8 knobs, centers spaced ~38 px across x 40..330), + Key Scale as a small bipolar
  knob at the far right. Op 4 strip additionally hosts the **Feedback** (`prismFB`) knob.
  - **Ratio** is a **stepped knob** over a snap list `{0.25,0.5,0.75,1,1.5,2,3,4,5,6,7,8,
    9,10,11,12,13,14}` — display `%.2f×`; **Fine** is `±99 ct` bipolar. (Per
    `09-multi-synth.md`, Fine/KeyScale may be trimmed if param budget hurts — if trimmed,
    hide those knobs and the strip uses 8 controls.)
- **MODE SUB-PANEL** = **ALGORITHM** widget (§8.6): a row of **8 clickable thumbnail
  diagrams** (`prismAlgo` selector) above one **large diagram** of the active algorithm,
  with the feedback loop drawn on the feedback op and `prismFB` shown.
- **Inline**: FILTER stays in circuit (presets open it) so FM can be filtered — no hiding.

### 4.6 Acid (mode 5) — bass box + pattern sequencer
- **Skin**: silver panel palette. OSC1 restricted display to Saw/Square in its combo
  (others still allowed per design — don't fight the user; show all, default saw).
- **MODE SUB-PANEL** = **ACID GLOBALS**: `acidAccentAmt` r22 knob + `acidSlideTime` r22
  knob + a big **ACCENT** indicator LED that pulses on accented steps (from bridge step
  index). Title "ACID".
- **SEQUENCER expands to 3 lanes** in `(16,524)–(700,692)` (cells start at `x=62` after a
  left label gutter `x18..58`):
  - **Gate/On lane** (top, y 578..596): the 16 `arpStep*` on/off cells (also used as
    step-mute in other modes).
  - **Pitch lane** (middle, y 600..648, grown for finer drag resolution): 16 vertical
    **drag columns**, value `seqPitch*` ∈ −24..+24 st. Each column: click-drag vertically
    sets pitch; a filled bar from the center (0 st) up/down; numeric `%+d` shown on hover;
    center gridline at 0.
  - **Accent + Slide lanes** (bottom, y 654..688 split into two 16 px rows — accent
    654..670, slide 672..688): 16 small LED-cells each for `seqAccent*` (amber) and
    `seqSlide*` (cyan).
  - The **live step index** from the bridge highlights the current column across all lanes.
- In **modes 0–4** the pitch/accent/slide lanes are **hidden**; the sequencer shows only
  the single on/off row (classic arp step-mutes).

### 4.7 Per-mode visibility matrix (from inventory §4)
| Control | Cosmos | Oracle | Mono | Modular | Prism | Acid |
|---|---|---|---|---|---|---|
| OSC1/2 standard panels | ✓ | ✓ | ✓ | ✓ | **hidden** (op matrix) | ✓ (saw/sq) |
| osc3 wave/level | – | – | – | ✓ | – | – |
| sub wave/level | ✓ | – | ✓ | – | – | – |
| filter HP | ✓ | – | – | – | – | – |
| crossMod | ✓ | ✓ | – | – | – | – |
| poly-mod ×4 | – | ✓ | – | – | – | – |
| ringMod | – | – | ✓ | ✓ | – | – |
| hardSync | – | – | ✓ | ✓ | – | – |
| fmAmount | – | – | – | ✓ | – | – |
| S&H rate | – | – | – | ✓ | – | – |
| chorus I/II/Both | ✓ | – | – | – | – | – |
| operator strips + algo | – | – | – | – | ✓ | – |
| acid globals + 3 lanes | – | – | – | – | – | ✓ |

**Hidden vs disabled**: mode-irrelevant *sub-panel* controls are **hidden** (the sub-panel
morphs). A control that exists but is inert in a mode (e.g. `crossMod` outside
Cosmos/Oracle) is **omitted** from the sub-panel entirely rather than shown greyed —
except the two ENV Curve combos and global controls, which are always live. Params never
disappear from the host param list; only their on-screen widget is conditionally drawn.

---

## 5. Mode-switch crossfade animation
- On `mode` change, capture `fromPalette = current live palette`, set `toPalette =
  palettes[newMode]`, `modeBlend = 0`.
- Each frame advance `modeBlend += dt / 0.28f` (clamp 1) → **280 ms** total; ease with
  smoothstep `e = b*b*(3−2b)`.
- **Interpolate** (linear RGB lerp by `e`): `background`, `panel`, `accent`, `text`,
  `textOnPanel`, `ledOn`, `ledOff`, and section-label color. Push the blended palette into
  `DuskPanel::setPalette` and use the blended `accent` for all arcs/curves/scope this frame.
- **Sub-panel cross-dissolve**: draw the outgoing mode's sub-panel with alpha `1−clamp(e*2,
  0,1)` for the first half (0..140 ms) and the incoming one with alpha `clamp(e*2−1,0,1)`
  for the second half (140..280 ms). The wood cheeks (Oracle) and silver panel (Acid) fade
  their alpha with the same `e`.
- Knob positions and combos **snap** immediately (they reflect params, which changed with
  the preset/mode); only chrome color + sub-panel content animate. No layout motion — the
  bone structure is fixed, so nothing slides.

---

## 6. Tooltip table (all parameters)
One-line, plain, useful. Indexed families (`N`, `n`) share a template with the index
substituted. Store in `kTooltips[kParamCount]`.

### Global / Mode
| Param | Tooltip |
|---|---|
| `mode` | Selects the synth engine and its personality. |
| `masterTune` | Global fine tuning of every voice, in cents. |
| `masterVol` | Overall output level. |
| `masterPan` | Stereo position of the whole instrument. |
| `stereoWidth` | Widens or narrows the stereo image. |
| `oversampling` | Internal oversampling; higher rejects aliasing at more CPU cost. |
| `analogAmt` | Analog character: subtle drift, detune and noise. |
| `vintage` | Age and wear: slow pitch wobble plus faint background hiss. |

### Oscillators
| Param | Tooltip |
|---|---|
| `osc1Wave` / `osc2Wave` / `osc3Wave` | Waveform of oscillator N. |
| `osc1Detune` / `osc2Detune` | Fine detune of oscillator N, in cents. |
| `osc1PW` / `osc2PW` | Pulse width of oscillator N (square and pulse waves). |
| `osc1Level` / `osc2Level` / `osc3Level` | Level of oscillator N in the mix. |
| `osc2Semi` | Coarse tuning of oscillator 2, in semitones. |
| `subLevel` | Level of the sub-oscillator, one octave below oscillator 1. |
| `subWave` | Sub-oscillator waveform. |
| `noiseLevel` | Amount of noise blended into the voice. |

### Filter + Envelopes
| Param | Tooltip |
|---|---|
| `filterCutoff` | Filter cutoff frequency. |
| `filterRes` | Resonance; high settings emphasize the cutoff and can self-oscillate. |
| `filterHP` | High-pass cutoff that thins the low end. |
| `filterEnvAmt` | How far the filter envelope opens or closes the cutoff. |
| `ampA` / `filtA` | Attack time of the N envelope. |
| `ampD` / `filtD` | Decay time of the N envelope. |
| `ampS` / `filtS` | Sustain level of the N envelope. |
| `ampR` / `filtR` | Release time of the N envelope. |
| `ampCurve` / `filtCurve` | Shape of the N envelope segments. |

### Mode-specific voice
| Param | Tooltip |
|---|---|
| `crossMod` | Oscillator 2 modulates oscillator 1 frequency at audio rate. |
| `ringMod` | Ring modulation between oscillators 1 and 2. |
| `hardSync` | Oscillator 2 hard-syncs to oscillator 1 for tearing timbres. |
| `fmAmount` | Linear FM from oscillator 1 into oscillator 2. |
| `pmFenvOscA` | Poly-mod: filter envelope to oscillator 1 pitch. |
| `pmFenvFilt` | Poly-mod: filter envelope added to the filter cutoff. |
| `pmOscBOscA` | Poly-mod: oscillator 2 to oscillator 1 pitch. |
| `pmOscBPWM` | Poly-mod: oscillator 2 to oscillator 1 pulse width. |
| `shRate` | Sample-and-hold clock rate. |
| `cosmosChorus` | Built-in chorus mode: off, I, II, or both. |

### LFO 1 / 2
| Param | Tooltip |
|---|---|
| `lfo1Rate` / `lfo2Rate` | Speed of LFO N. |
| `lfo1Shape` / `lfo2Shape` | Waveform of LFO N. |
| `lfo1Fade` / `lfo2Fade` | Time for LFO N to fade in after a note. |
| `lfo1Sync` / `lfo2Sync` | Lock LFO N speed to host tempo. |

### Unison / Portamento / Velocity
| Param | Tooltip |
|---|---|
| `unisonVoices` | Stacked detuned voices per note. |
| `unisonDetune` | Spread of detuning across unison voices, in cents. |
| `unisonSpread` | Stereo spread of unison voices. |
| `portaTime` | Glide time between notes. |
| `legato` | Glide only when notes overlap. |
| `glideMode` | Glide as a fixed time or a fixed rate. |
| `velSens` | How strongly velocity affects level. |
| `velCurve` | Response curve applied to incoming velocity. |
| `pbRange` | Pitch-bend range, in semitones. |

### Arpeggiator / Sequencer
| Param | Tooltip |
|---|---|
| `arpOn` | Enable the arpeggiator / step sequencer. |
| `arpMode` | Note order the arpeggiator plays. |
| `arpOctave` | Range the arpeggio spans, in octaves. |
| `arpRate` | Step length as a note division. |
| `arpGate` | Length of each step relative to its slot. |
| `arpSwing` | Delays off-beat steps for a swung feel. |
| `arpLatch` | Hold the pattern after keys are released. |
| `arpVelMode` | Velocity source for steps: as played, fixed, or accented. |
| `arpFixedVel` | Velocity used when the mode is fixed. |
| `arpStepN` | Turn step N on or off. |

### FX — Drive / Chorus / Delay / Reverb
| Param | Tooltip |
|---|---|
| `driveOn` | Enable the drive stage. |
| `driveType` | Drive character: soft, hard, or tube. |
| `driveAmt` | Amount of drive. |
| `driveMix` | Blend of driven and clean signal. |
| `chorusOn` | Enable the chorus. |
| `chorusRate` | Chorus modulation speed. |
| `chorusDepth` | Chorus modulation depth. |
| `chorusMix` | Chorus wet/dry blend. |
| `delayOn` | Enable the delay. |
| `delaySync` | Lock delay time to host tempo. |
| `delayTime` | Delay time in milliseconds (when not synced). |
| `delayDiv` | Delay time as a note division (when synced). |
| `delayFB` | Delay feedback amount. |
| `delayMix` | Delay wet/dry blend. |
| `delayPP` | Ping-pong the delay across the stereo field. |
| `delayTape` | Adds tape-style warmth and saturation to the delay. |
| `reverbOn` | Enable the reverb. |
| `reverbSize` | Size of the reverb space. |
| `reverbDecay` | Reverb tail length. |
| `reverbDamp` | High-frequency damping of the tail. |
| `reverbMix` | Reverb wet/dry blend. |
| `reverbPD` | Pre-delay before the reverb begins. |

### Mod Matrix (per slot N = 0..7)
| Param | Tooltip |
|---|---|
| `modSrcN` | Modulation source for slot N. |
| `modDstN` | Modulation destination for slot N. |
| `modAmtN` | Amount and polarity of slot N's modulation. |

### Prism (FM)
| Param | Tooltip |
|---|---|
| `prismAlgo` | Operator routing algorithm. |
| `prismFB` | Feedback on the feedback operator, for growl and edge. |
| `opN Ratio` | Frequency ratio of operator N to the played note. |
| `opN Fine` | Fine detune of operator N, in cents. |
| `opN Level` | Output level of operator N (modulation depth or volume). |
| `opN Vel` | How strongly velocity affects operator N's level. |
| `opN KeyScale` | Level change of operator N across the keyboard. |
| `opN A/D/S/R` | Attack/Decay/Sustain/Release of operator N's envelope. |

### Acid
| Param | Tooltip |
|---|---|
| `acidAccentAmt` | How much accented steps boost level, resonance, and envelope. |
| `acidSlideTime` | Glide time for slid steps. |
| `seqPitchN` | Pitch of step N relative to the held note, in semitones. |
| `seqAccentN` | Accent step N. |
| `seqSlideN` | Slide into step N. |

---

## 7. Panel draw order & chrome
Per frame, after `panel.begin(s, org, font, this)` and palette blend:
1. Full-window black `IM_COL32(6,6,7,255)`; chassis fill `background` over `(0,0)–(1240,780)`.
2. Oracle wood cheeks (if blended in) at the two side bands.
3. Each panel: 3 px metal bevel `AddRectFilled(P(x0−3,y0−3),P(x1+3,y1+3), metal, 8*s)`
   then recessed inner `AddRectFilled(P(x0,y0),P(x1,y1), panel, 6*s)`; section title top-left.
4. Displays (filter curve, ADSR ×2, scope, VU) — polylines from cached buffers.
5. Knobs / combos / LED-buttons / step cells (interactive; ImGui hit-tests in draw order).
6. Keyboard.
7. Overlays (MOD matrix scrim + modal) last so they capture input above the panels.
- **Metal color** = `panel` lightened ~2× toward white, clamped. Bevel radius 8·s.

---

## 8. New widget implementation notes

### 8.1 Mode rocker
- Six backlit rockers (§1.1). Each: rounded rect `AddRectFilled` in `panel` darkened;
  top-lit highlight when selected; a small LED at its left in `ledOn`. Selected rocker
  fills with `accent` @ 30 alpha + accent border; label centered, bold when selected.
- Hit: `InvisibleButton("rocker%d")`; on click set `mode` param
  (`beginEdit/setParam/endEdit`) and kick the crossfade (§5). Stepped-int param.

### 8.2 LED-button (bool toggle with lamp)
- Extend `DuskPanel::toggle` visual: rounded rect + a round LED (`led()`) at left, label
  right. On = accent border + LED lit; off = grey border + dim LED. For Acid use round
  colored buttons (full-circle `AddCircleFilled` in the button color, ring when off).
- Used for every Bool param and the chorus I/II/Both selector (mutually exclusive → sets
  `cosmosChorus` to the matching enum).

### 8.3 Context knob (Delay Time / Div)
- Like tape-echo's repeat-rate: when `delaySync` on, the knob is **stepped** over the 14
  divisions (`delayDiv`, display the division name); when off, continuous `delayTime` ms.

### 8.4 Step cell (sequencer on/off)
- 16 cells across the sequencer lane; cell *i* at `x = x0 + i*cw` (`cw = laneW/16`), gap 2.
  Filled rounded rect; **on** = accent fill + lit; **off** = dark. The **live step** (from
  bridge) draws a bright top border / glow regardless of on/off. `InvisibleButton` toggles
  `arpStep{i}`. Beat groups of 4 get a subtle divider.

### 8.5 Pitch-lane drag (Acid `seqPitch*`)
- Column *i* is an `InvisibleButton` over its slot; on active drag, `value +=
  −MouseDelta.y * (48/laneHeight)` clamped −24..+24, snapped to integer semitone (Shift =
  no snap for scrub feel, re-snap on release). Bar drawn from the 0-center line to value;
  positive up, negative down. Hover shows `%+d st`. Double-click resets to 0.

### 8.6 Algorithm diagram widget (Prism)
- **Single source of truth**: the `struct PrismAlgo { struct Op{ uint8_t gx, gy; bool
  carrier; } ops[4]; struct Edge{ uint8_t from, to; } edges[6]; uint8_t nEdges; uint8_t
  fbOp; }` and its `static const PrismAlgo kPrismAlgos[8]` table live in
  `plugins/sunset-circuits/core/FMAlgorithms.hpp`, which is also the FMEngine algorithm table.
  The UI includes that core header directly and renders from `kPrismAlgos` — it never
  redefines the struct or hardcodes topology separately.
- **Render one diagram** into a rect: place each op as a rounded square (~22 px) at its
  `(gx,gy)` grid cell (grid origin top-left, gy=0 top); draw `edges` as lines
  `from`→`to` with a small arrowhead at the destination; draw a thick **output bus** line
  under all carriers joining to a single node; draw a **feedback loop** (small arc arrow)
  on `fbOp`, its thickness scaled by `prismFB`. Carriers: brighter fill + accent border;
  modulators: dim fill. Op label "1".."4" centered.
- **Thumbnails**: 8 mini-diagrams in a 4×2 grid across the sub-panel top (each ~64×48),
  each an `InvisibleButton` setting `prismAlgo`; active one gets accent border + full
  brightness, others dimmed to ~50%. Below them, the **large** diagram of the active algo.
- **The 8 algorithms** (must match engine; `a→b` = a modulates b; carriers reach output):

| # | Name | Edges | Carriers | Character |
|---|---|---|---|---|
| 1 | Serial | 4→3, 3→2, 2→1 | 1 | Serial stack — brightest, bell/metallic |
| 2 | Stack-2M | 4→2, 3→2, 2→1 | 1 | Two modulators into one — rich, vocal |
| 3 | Branch | 4→2, 4→3, 2→1, 3→1 | 1 | One mod fans into two, both into the carrier |
| 4 | Y-Split | 4→3, 3→1, 3→2 | 1, 2 | Serial mod chain splitting into two carriers |
| 5 | Dual | 2→1, 4→3 | 1, 3 | Two 2-op stacks — classic tine e-piano |
| 6 | Twin+1 | 3→1, 3→2 | 1, 2, 4 | One mod into two carriers plus a clean standalone carrier |
| 7 | Tri+FM | 4→3 | 1, 2, 3 | One modulated tone plus two clean carriers |
| 8 | Additive | (none) | 1, 2, 3, 4 | Additive / organ — four parallel carriers |

  `plugins/sunset-circuits/core/FMAlgorithms.hpp` `kPrismAlgos` is the single source of truth
  for this table — the rows above must mirror it exactly.
  Op 4 is the feedback op in all algorithms (`fbOp = 3`, zero-based). ASCII of a few:
```
  Alg 1 (serial)      Alg 5 (dual stack)       Alg 8 (additive)
     [4]                 [4]   [2]              [4][3][2][1]
      |                   |     |                | | | |
     [3]                 [3]   [1]               =========  (output bus)
      |                   |     |
     [2]                 ======= (output bus)
      |
     [1]
   =====(out)
```

### 8.7 Keyboard widget (playable, → MIDI)
- Draw 21 white keys then black keys on top (§1.5). Track `heldKey` (mouse). On
  `InvisibleButton` press over a key: compute `note = baseMidi + keyIndexToSemitone(i)`;
  `UI::sendNote(0, note, 100)` (DPF, requires `DISTRHO_PLUGIN_WANT_MIDI_INPUT` — this is
  the fleet's first synth, so the shell sets it). On release / drag-off: `sendNote(0,
  prevNote, 0)`. **Glissando**: while dragging, if the hovered key changes, note-off the
  old and note-on the new.
- **Visual feedback**: pressed keys and **incoming MIDI notes from the host** light in
  `accent` — read active notes from the bridge `multiSynthGetActiveNotes` [optional; else
  only local presses light].
- OCT−/OCT+ buttons shift `baseMidi` by 12; show current octave label. The keyboard spans
  21 keys and the top key sits at `baseMidi + keyIndexToSemitone(20)` (= base + 35), so
  `baseMidi` must stay ≤ 127 − 35 = 92 to keep the top key in MIDI range. With ±12 octave
  steps landing on the C3 default the effective ceiling is `baseMidi` 84 (top key 119).
  Clamp `baseMidi` to 12..84.

### 8.8 MOD matrix overlay
- Toggled by the MOD MATRIX bar button; `showMod` bool. When open, draw a full-window
  scrim (`IM_COL32(0,0,0,150)`) as an `InvisibleButton` that closes on click-outside, then
  the modal panel `(220,120)–(1020,660)`. 8 rows, row *r* at `y = 168 + r*58`:
  Source combo `(240,·)–(470,·)`, "→" glyph, Dest combo `(500,·)–(760,·)`, bipolar
  Amount bar-knob `(790,·)` r18, clear-row ✕ `(980,·)`. Title "MODULATION MATRIX" + close.
- Active-slot count on the bar button = slots where src≠None and dst≠None and amt≠0.

---

## 9. Rendering & performance notes
- **Everything in design-space coords × `s`.** No absolute pixel literals in draws except
  through `P()`.
- **Fonts**: build one `CrispFontSet` with design sizes `{9, 10, 11, 12, 13, 15, 20, 26}`
  `× getScaleFactor()` (playbook §4). `panel.setFontSet(set)`; `pickFont` chooses nearest.
  Fallback to the ImGui default when no TTF is found (shared loader handles it).
- **Window flags**: `NoTitleBar|NoResize|NoMove|NoCollapse|NoScrollbar|NoBackground|
  NoScrollWithMouse` (the last so knob wheel works, per shared knob comment).
- **Geometry constraints**: `setGeometryConstraints(1240/2, 780/2, true)` (keep aspect),
  default size 1240×780.
- **Target < 2 ms/frame.** Expensive elements and their caching:
  - **Filter curve**: recompute polyline only on `cutoff|res|HP|mode|envAmt` change (dirty
    flag). ~180 pts.
  - **ADSR displays ×2**: recompute only on A/D/S/R/curve change. ~40 pts each.
  - **Scope**: one fetch + polyline ≤220 pts/frame (cheap; the only unconditional redraw).
  - **VU**: two bars, trivial; ballistics use `ImGui::GetIO().DeltaTime`.
  - **FFT**: not needed for a synth (no analyzer) — do **not** run `RealFFT` per frame.
  - **Algorithm diagram**: static per algo; cache nothing heavy (few dozen primitives).
  - Mode crossfade touches only colors — no re-layout, no re-cache.
- **Bridge reads** (`getOutputLevelL/R`, `getScope`, step index, active notes) are one
  call each per frame, guarded by `#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS` + null check,
  exactly like `TapeEchoUI`'s `tapeEchoGetOutputLevel`.
- **No allocations in the frame loop**: fixed `values[kParamCount]`, fixed scratch arrays
  for curve/scope/adsr polylines (members), `snprintf` into stack buffers.

---

## 10. Interaction map (summary)
- **Knobs**: drag (fine on Shift), wheel, double-click type, Ctrl/Cmd-click reset,
  right-click reset [enable via `rightClickReset`]. Hover → name bubble; drag → value bubble.
- **Combos**: click to open; selecting sets the choice param (begin/set/end edit).
- **LED-buttons / rockers / step cells / pitch columns**: click / drag as in §8.
- **Keyboard**: press → note-on 100; release/drag-off → note-off; drag → glissando;
  OCT± shift base. Host MIDI lights keys.
- **Preset browser**: ◀/▶ step `currentPreset` and apply (like tape-echo `applyPreset`,
  iterating the static preset table); combo jumps directly; ★ saves [v2 bridge].
- **MOD overlay**: MOD MATRIX button toggles; click-scrim or ✕ closes.
- **Mode switch**: rockers set `mode`, kick 280 ms crossfade; sub-panels dissolve; per-mode
  visibility (§4.7) applies immediately to which widgets are drawn.
- **Hidden vs disabled**: mode-irrelevant sub-panel controls are hidden; globally-relevant
  controls stay live in every mode; no host param is ever removed.

---

## 11. Build order (incremental bring-up)
Implement in this order so each step is verifiable in the Xvfb UI sweep:
1. **Skeleton**: fixed design space + scale/origin, chassis fill, all panel bevels/labels,
   font set. No live controls. Verify layout matches §1 at several window sizes.
2. **Shared knob + combo + LED-button** wired to `values[]` and the host (copy tape-echo
   plumbing: `ParamHost`, `parameterChanged`, `kParamDefaults`). Bring up OSC/FILTER/ENV/
   FX standard controls first — the majority of params.
3. **Preset browser** (prev/next/combo) over the static preset table.
4. **Filter-curve display** (cached) — first custom display; validates the dirty-flag pattern.
5. **ADSR displays ×2** (cached).
6. **Scope + VU** via the access bridge (with null fallback).
7. **Keyboard** (sendNote) — makes the plugin playable from the UI.
8. **Sequencer lane** (step cells) + arp panel.
9. **Mode rockers + palettes + crossfade** + per-mode visibility switching and the simple
   sub-panels (Cosmos chorus, Oracle poly-mod, Mono ring/sync, Modular S&H).
10. **MOD matrix overlay**.
11. **Prism operator matrix + algorithm diagram widget** (most complex; shares the algo
    descriptor with the engine).
12. **Acid silver skin + 3-lane sequencer** (pitch drag, accent/slide cells).
13. **Tooltips** pass over every control; final Xvfb screenshot sweep + readout checks.

---
*Multi-Synth UI spec | 1240×780 fixed design space | Dear ImGui / ImDrawList | Dusk Audio*
