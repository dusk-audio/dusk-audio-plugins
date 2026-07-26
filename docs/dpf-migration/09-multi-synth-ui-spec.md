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
Three zones, left to right: **nameplate**, **mode rockers**, **preset cluster**. The
rockers used to be pinned at `x306`, which left a ~120 px hole after the nameplate and
squeezed the preset cluster into the last 270 px. They now start immediately after the
nameplate and the freed width goes entirely to the presets, which is what was short.

| Element | Rect / center | Notes |
|---|---|---|
| Chassis header fill | `(0,0)–(1240,54)` | `bg` darkened 10%, 3 px metal top edge, 1.5 px hairline at y=54 |
| Nameplate "SUNSET CIRCUITS" | `(18,8)`, 20 px bold `text` | hit rect `(14,4)–(blockX1+6,50)` |
| Byline "Dusk Audio" | `(20,32)`, 11 px bold **accent** | |
| Version "· vX.Y.Z" | `(verX,33)`, 10 px, `text` @ α140 | `verX = 20 + textW(11,"Dusk Audio") + 7`; **suppressed below `kReadoutMinS`** (§3.1b) — 10 px is <6 device px at the minimum size, the same mush that gate exists for; the tooltip still carries it |
| Mode rockers ×6 | rocker *i* at `x = modeX0 + i*96`, width **90**, `y 10..46` | `modeX0 = blockX1 + 16`, clamped so the row always clears the cluster (§4) |
| Preset ◀ prev | `(792,12)–(826,44)` | w **34** (was 26×28) |
| Preset combo | `(830,12)–(1038,44)` (w **208**) | styled `BeginCombo` (was 126, which clipped "Alien Transmission"); grouped multi-column popup, §8.12 |
| Preset ▶ next | `(1042,12)–(1076,44)` | w **34** |
| BROWSE | `(1082,12)–(1158,44)` | w **76** (was 40); opens the preset browser (§8.10) |
| SAVE | `(1164,12)–(1222,44)` | w **58** (was 36); opens the save modal (§8.11) |

- **The cluster is laid out right-to-left** from `kBarClusterX1 = 1222`, with constant gaps
  (4 px around the combo, 6 px between the buttons). It is the anchored end, so a future
  width change moves the cluster's *left* edge — and, through the clamp, the rockers —
  instead of silently overrunning the panel wall. Every element shares one `12..44` row,
  so the bar reads as a single band rather than three heights.
- **`modeX0` is measured, not guessed**: `textW()` on the title and on the byline+version,
  whichever is wider, so the gap after the nameplate survives a font substitution. It is
  then clamped to `kBarPrevX0 − 14 − (5*96 + 90)`, which is what *guarantees* the two zones
  cannot collide no matter how wide the title measures.
- Rocker labels are drawn at `centre+7` (the LED owns `x0+11`); "MODULAR", the widest,
  is ~46 px at font 12 and ends ~14 px short of the right edge.

BROWSE and SAVE share `barButton()` — chevron chrome with a **10 px** accent label instead
of a triangle, accent border on hover. (9 px was sized for the old 36/40-wide buttons; on
the widened ones it read as a caption floating in a box rather than the button's label.)

**Nameplate tooltip**: `Sunset Circuits v<version>` / `Dusk Audio` / `<W> x <H> · scale
<s>`. The version is single-sourced from the git tag through CMake's `SC_VERSION_STRING`,
so it *is* the build identity; the surface size and scale are the one other thing worth
asking for in a bug report. Deliberately **no** `__DATE__`/`__TIME__` — it would make every
rebuild a different binary and cost the release builds their reproducibility for a line
nobody can act on.

### 1.2 Body — `y 60..542`

> **Layout revision (2026-07, legibility pass):** the oscillator panels were too tall
> for their content while VOICE / CHARACTER was cramped (r7.5 knobs, font-8 labels).
> The lower body ROW (VOICE/CHARACTER, AMP/FILTER ENV, MOD bar, OUTPUT) now ends at
> **`y=542`** (was 518), and the osc panels shrank to feed it: OSC 1 `178→172`, OSC 2
> `300→288` (internals −6), OSC 3/SUB `410→372` (h80, r14 knobs). VOICE / CHARACTER
> grew to `(16,376)–(340,542)` (h166) with **r13 knobs** and **font-10 labels**. The
> layer seam moved `521→545` and the SEQUENCER strip gave back the 24 px at its top
> (§1.3). Upper panels (OSC 1/2 internals, FILTER, LFO 1/2, SCOPE) are otherwise
> unchanged.

**LEFT column — Oscillators / Mixer — `x 16..340`**
| Panel | Rect | Contents |
|---|---|---|
| OSC 1 | `(16,60)–(340,161)` | wave combo, Detune `x60`, PW `x130`, Level `x200`, X-Mod `x285` (Cosmos/Oracle only) |
| OSC 2 | `(16,165)–(340,266)` | wave combo, Semi `x56` (stepped), Detune `x114`, PW `x172`, Level `x230`; Semi/Detune drawn **inert** in Cosmos (§4.1) |
| OSC 3 / SUB | `(16,270)–(340,371)` | **mode-variant** (see §4): Modular→osc3 wave + Level `x118` + FM Amt `x238`; Cosmos/Mono→sub wave + Level `x178`; else dimmed "(not used in this mode)" text at the knob row |

**All three oscillator panels share ONE geometry (§1.2a)** — same height, same rows, same knob
radius — because they are the same widget set. `oscGeom(y0)` is the single source.
| VOICE / CHARACTER | `(16,376)–(340,542)` (h166) | **2 rows** of **r13 knobs** (tick ring reaches R+6.5 → ±19.5) with **font-10 labels**. Knob columns `x=42+45·c` (c=0..4 → 42..222); row centres `y{430,494}`, labels top `y−32`. Row1: Noise/Analog/Vntg/Tune/UniV. Row2: UniDT/UniSP/Porta/Vel/PB. Right-hand column `x=291` (hw45): 4 stacked items (label baseline `centre−19`, comboH9) — OverSmp `y416`, Glide `y452`, Legato LED `y488`, V.Crv `y524`. Clearances (per drawMixerVoice comment): row1 label ink 399..405.75 vs ring top 410.5 = **4.75 px**; row2 ring bottom 513.5 vs inner floor 539 = **25.5 px**; column spacing 45 ≥ ring-Ø 39 + 4. Verified on the Acid silver palette. Prism (mode 4) uses the compact variant, see §4.5. |

#### 1.2a Oscillator panel geometry — one family

The three OSC panels are structurally identical: section title + wave combo on the top row,
then **exactly one** row of knobs. They had drifted to **r20 / r18 / r14** on panels of
**h112 / h112 / h80** — a staircase that encoded nothing. OSC 1 read as the "important"
oscillator purely because its panel happened to be tallest and OSC 3 as an afterthought, and
OSC 3's r14 tick ring already poked 0.5 px through its own panel floor.

**Radius r18, ticks on**, for all three. This is the house standard for a *primary body
control*, not a split-the-difference number: AMP ENV and FILTER ENV — the panels immediately
right of this stack, at the same visual weight — use r18 for their four knobs each, as do the
S&H rate and the mod-matrix amount. The r13/r14 **tickless** family belongs to the bottom
utility strip (FX + sequencer, §1.3) and the r13 VOICE / CHARACTER grid is a dense 10-knob
matrix; neither is the right neighbour to match. The resulting left-column hierarchy reads as
tiers rather than a slope: hero `r54` (cutoff) → primary `r30` (filter row) → **standard `r18`
(oscillators + envelopes)** → dense grid `r13` (voice) → utility `r13/r14` tickless.

**Height.** r18 with its tick ring reaches ±24.5, so a panel needs
`30 (label top: title + combo + gap) + 9 (label ink — measured, see §1.3a) + 4.5 (label → ring
gap) + 49 (ring) + 3 (inner floor) = 95.5 px` minimum, which is exactly what h101 realises —
and more than OSC 3's old h80 could give. Rather than grow the stack (VOICE /
CHARACTER below is documented as needing every one of its 166 px, §1.2), the existing
`60..372` block is split **equally**: three **h101** panels with 4 px gutters. OSC 1 and OSC 2
give up 11 px each, which they had spare; OSC 3 gains 21 px, which it needed. The block ends
at 371, so the gutter before VOICE / CHARACTER is 5 px rather than 4 — which reads as the
group break it actually is.

`oscGeom(y0)` returns the whole row set, so all three panels are laid out by one function and
a knob added to one cannot silently desynchronise it from the others:

| Item | Offset from `y0` | OSC 1 (60) | OSC 2 (165) | OSC 3 (270) |
|---|---|---|---|---|
| panel | `0 .. 101` | 60..161 | 165..266 | 270..371 |
| title / combo top | `+4` | 64 | 169 | 274 |
| combo bottom | `+24` | 84 | 189 | 294 |
| knob label top | `+30` | 90 | 195 | 300 |
| knob centre | `+68` | 128 | 233 | 338 |

Clearances, identical for all three by construction: combo bottom → label top **6.00**,
label ink → ring top **4.50** (ink bottom = label top + 9, measured), ring bottom → inner floor
**5.50**. Horizontally the 49 px ring
gets ≥ 21 px of daylight in OSC 1 (70/85 columns) and 9 px in OSC 2 (58 columns — VOICE /
CHARACTER runs 6, so this is the roomier of the two grids). **Prism (mode 4) is unaffected**:
it replaces the whole left column with the OPERATOR MATRIX and never draws these panels.

**CENTER column — Filter + Envelopes — `x 348..752`**
| Panel | Rect | Contents |
|---|---|---|
| FILTER | `(348,60)–(752,300)` | curve display `(360,74)–(742,180)`; **oversized cutoff** center `(426,244)` r**54**; Res `(556,232)` r30; Env Amt `(636,232)` r30; HP `(712,232)` r30 (Cosmos only) |
| AMP ENV | `(348,304)–(548,542)` | ADSR display `(356,320)–(540,420)`; knobs A/D/S/R centers `x{380,426,472,518} y=474` r18 (labels `y=436`, read-outs `y=500`); Curve combo `(360,514)–(536,538)` |
| FILTER ENV | `(552,304)–(752,542)` | ADSR display `(560,320)–(744,420)`; knobs centers `x{584,630,676,722} y=474` r18 (labels `y=436`, read-outs `y=500`); Curve combo `(564,514)–(740,538)` |

**RIGHT column — LFOs / Mode sub-panel / Scope / Output — `x 760..1224`**
| Panel | Rect | Contents |
|---|---|---|
| LFO 1 | `(760,60)–(1000,190)` | Rate, Fade knobs; Shape combo; Sync toggle |
| LFO 2 | `(760,194)–(1000,324)` | same |
| MODE SUB-PANEL | `(760,328)–(1000,462)` | **morphs per mode** (§4): chorus / poly-mod / ring+sync / S&H / algo thumbnails / acid globals |
| MOD MATRIX bar | `(760,466)–(1000,542)` | "MOD MATRIX" LED-button `(768,474)–(992,534)` (grown, centred mid 504; LED `y504`, text `y496`, count `y514`) → opens overlay (§1.4); shows count of active slots |
| SCOPE | `(1004,60)–(1224,300)` | oscilloscope from ring buffer |
| OUTPUT / VU | `(1004,304)–(1224,542)` | stereo VU bars `y 338..520` (L/R labels `y524`) + Master Vol, Pan, Width knobs |

### 1.3 Bottom strip — `y 548..692`
| Panel | Rect | Contents |
|---|---|---|
| SEQUENCER | `(16,548)–(700,692)` | transport header `y 552..606` (§1.3a) + step lanes. Non-acid step row `y 612..680` (h68, four bar-groups, §8.4). **Acid** expands to 4 lanes (§4.6) |
| FX · Drive | `(708,552)–(834,688)` | enable LED-button, Type combo, Amount, Mix |
| FX · Chorus | `(838,552)–(964,688)` | enable, Rate, Depth, Mix |
| FX · Delay | `(968,552)–(1094,688)` | enable, Sync toggle, Time/Div (context knob), FB, Mix, PingPong+Tape LED-buttons |
| FX · Reverb | `(1098,552)–(1224,688)` | enable, Size, Decay, Damp, Mix, PreDelay |

#### 1.3a Sequencer transport header — solved rhythm

The header is **six groups** — `ARP | MODE | RATE | OCT/GATE/SWING | LATCH | VEL` — right-
aligned on the **`x=692` rule the step lane below also ends on**, with a **single gap `g`
repeated between every adjacent pair, the section title included**. `g` is *solved per
frame*, not hardcoded:

```
titleEnd = 24 + textWidth(11 px bold, title)      // measured through the drawn atlas face
wSum     = 475                                    // sum(groupWidths), below
g        = (692 - titleEnd - wSum) / 6            clamped to [12, 22]
x        = min(titleEnd + g, 692 - wSum - 5*g)    // never cross the right rule
```

The **start clamp is not decoration**. Walking left-to-right from the title only lands on the
692 rule while `g` is the solved value; once the `[12, 22]` clamp engages — a title wide enough
to drive `g` below 12 — the row would march straight through the right wall and into the FX
strip. Real headroom is thin (DejaVu bold `"PATTERN SEQUENCER"` leaves `g` barely above the
floor), and the failure is ugly: measured with a deliberately over-long title, the unclamped
row ends at `x = 789`, i.e. **89 px inside the DRIVE panel**. Clamped, it still ends at 692 and
the title gap absorbs the difference (the title then overlaps the MODE label, which is the
correct trade — a panel boundary is a hard edge, a label collision is not).

with group widths `ARP 52 | MODE 84 | RATE 64 | knobs 123 | LATCH 52 | VEL 100` (sum 475).
The title is ~23 px shorter outside Acid (`"SEQUENCER / ARP"` vs `"PATTERN SEQUENCER"`), so a
fixed start can only balance one of them: the old `x=160` start balanced Acid and left the
other five modes opening on a **45 px hole** while their own inter-group gaps were 6..11 px.
Solving gives `g = 17` (non-acid) / `13` (Acid) — even inside whichever row is on screen.

- **Vertical**: labels top `y552` (font 10, ink to 558.75); combos `y 564..586`; LED-buttons
  `y 563..587`, i.e. **h24 centred on 575, the same centre line as the combos** (they used to
  sit at `558..582`, 5 px high, which read as a misaligned row); knob centres `y578`;
  persistent read-outs ink `599..605.4`.
- **OCT / GATE / SWING** are **r13 and TICKLESS**, matching the FX strip idiom next door.
  Reach is `r+3+1.2 = ±17.2` (value arc + half its stroke) instead of the tick ring's `±20.5`,
  which makes the three read as **one family** — as r14 ringed knobs they differed enough in
  apparent size/weight to look like three different widgets. Centres `kx0 + {0,44,88}`.
- **Ink extents here are MEASURED, not derived from `0.675·size`** — that convention, used by
  older comments elsewhere in this file, is wrong, and `0.810·size` is also low for this atlas.
  Scanned at `s = 1`, a label drawn with its top at `y552` puts ink on rows `555..560`, so the
  ink box ends **9.0 px** below the draw origin, and that holds for every size in this row
  (9.5 / 10 / 11 all snap to neighbouring atlas faces). Consequences, all pixel-verified:

  | | value | clearance |
  |---|---|---|
  | label top `552` → ink bottom | `561` | — |
  | knob centre `hy = 580` → arc top | `562.8` (first ink row 563) | **2.0 px** below the label |
  | Acid: arc bottom | `597.2` (nothing drawn 592..599) | **2.8 px** above the GATE lane at `y600` |
  | read-out top `hy+r+8 = 601` → ink bottom | `610` | **2.0 px** above the non-acid lane at `y612` |

  `hy` was 578, which put the arc top at 560.8 — the label ink **overlapped it by 0.2 px**.
- **Persistent read-outs** (§3.1b): OCT/GATE/SWING (and the Fixed-VEL knob) carry them,
  scale-gated on `readoutsOn()`. **Suppressed in Acid**, which packs four lanes from `y600`
  and has no free band under the knob row — the hover bubble is the read-out there.
- **VEL group is 100 wide in both velocity modes** so switching to Fixed does not reflow the
  row: Fixed spends the width on a 58 px combo plus the value knob (centre `679`, reach
  `696.5`, inside the 700 panel wall); the other modes give all 100 to the combo, which
  `"As Played"` wants (it clipped at 86).

### 1.4 Overlays (drawn last, above everything)
| Overlay | Rect | Contents |
|---|---|---|
| MOD MATRIX | modal `(220,120)–(1020,660)` over a `IM_COL32(0,0,0,150)` full-window scrim | 8 rows × {Source combo, arrow, Dest combo, Amount bipolar bar-knob, clear-row ×}; title + close ✕ `(988,128)–(1012,152)` |
| SAVE USER PRESET | modal `(420,285)–(820,495)` over the same scrim | name `InputText`, inline overwrite / delete confirm, SAVE / CANCEL / DELETE |
| PRESET BROWSER | modal `(48,58)–(1192,740)` over the same scrim | search + mode/bank chips + 4×12 preset grid + APPLY / CLOSE (§8.10) |
| KEYBOARD SHORTCUTS `?` | reuse Multi-Q help card pattern [v2] | optional |

### 1.5 Keyboard — `(16,700)–(1224,780)` (always visible)
- OCT− / OCT+ buttons reserved `x 16..48` (two stacked, `(16,700)–(48,738)` / `(16,742)–(48,780)`).
- **Performance wheels** `x 52..114` (§8.9): pitch bend slot `(54,700)–(80,762)`, mod wheel
  slot `(86,700)–(112,762)`, each with a 2 px bezel and an 8.5 px label at `y 767`.
- Playable keys region `x 118..1224`, **3 octaves** = 21 white keys. White key width
  `w = (1224-118)/21 ≈ 52.67`. White keys full height `y 700..780`; black keys width
  `0.62*w`, height `700..750`, centered on white-key boundaries at the standard C#,D#,F#,G#,A# offsets.
  (The keys gave up their left 66 px to the wheels rather than the design space growing.)
- Base octave from OCT buttons; default lowest key = **MIDI 48 (C3)**, span C3..B5.

### 1.6 ASCII layout diagram

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│ SUNSET CIRCUITS [COSMOS][ORACLE][MONO][MODULAR][PRISM][ACID]  ◀ [ Preset ▾ ] ▶ BROWSE SAVE│ 0..54
│ Dusk Audio · v1.0.0                                                                       │
├───────────────┬───────────────────────────────┬─────────────────────────────────────────┤
│  OSC 1        │        FILTER                  │  LFO 1                 │   SCOPE          │
│  wave det pw  │  ┌────filter-curve────────┐    │  rate fade shape sync  │   /\  /\  /\     │
│  level        │  │                         │   ├────────────────────────┤   \/  \/  \/     │ 60
│───────────────┤  └─────────────────────────┘  │  LFO 2                 │                  │ ..
│  OSC 2        │        ( CUTOFF )   Res EnvA HP│  rate fade shape sync  ├──────────────────┤ 300
│  wave semi ...│         big r54               │├────────────────────────┤   OUTPUT / VU    │
│───────────────┤                               ││  MODE SUB-PANEL        │  ▮▮   vol pan wid │
│ OSC3 / SUB    ├───────────────┬───────────────┤│ (chorus/polymod/algo…) │  ▮▮              │ 292
│ (mode variant)│   AMP ENV     │  FILTER ENV   ││                        │                  │ ..
│───────────────┤  /\___ ADSR   │  /\___ ADSR   │├────────────────────────┤                  │
│ VOICE/CHAR    │  A D S R  crv │  A D S R  crv ││ [   MOD MATRIX   ]     │                  │ ..542
│ (2×5 r13)     │               │               ││                        │                  │
├───────────────┴───────────────┴───────────────┴┴────────────────────┬───┴──────────────────┤
│  SEQUENCER  [1][2][3][4][5][6][7][8][9]..[16]                        │ DRV │ CHO │ DLY │ REV │ 548
│  (Acid: +pitch lane  +accent lane  +slide lane)                      │     │     │     │     │ ..692
├─────────────────────────────────────────────────────────────────────┴─────┴─────┴─────┴─────┤
│OCT│PB MOD│ ▌▐▌▐▌▌▐▌▐▌▐▌  ▌▐▌▐▌▌▐▌▐▌▐▌  ▌▐▌▐▌▌▐▌▐▌▐▌ (3-octave clickable keyboard → MIDI) │ 700..780
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

#### 3.1b Persistent value read-outs
Performance-critical knobs show their value permanently, not only in the hover bubble.
Placement is 9.5 px design-space, dimmed on-panel ink, centred at `cy + r + 8` (the shared
`persistent` flag), except where noted.

| Where | Knobs | Placement |
|---|---|---|
| FILTER | Cutoff (bespoke 10 px accent read-out at `(426,290)`), Res, Env Amt, HP | under knob (y 270) |
| AMP / FILTER ENV | A D S R ×2 | under knob (y 500), 7.6 px above the Curve combo |
| LFO 1 / 2 | Rate, Fade | under knob (`y0+98`) |
| VOICE / CHARACTER | row 2 only — Uni Detune, Uni Spread, Porta, Vel, PB | under knob (y 515); **suppressed in Prism geometry C**, where `yc2=514` would push the ink through the 539 panel floor |
| OUTPUT | Volume | under knob |
| FX DRIVE / CHORUS | Amt, Mix / Rate, Depth, Mix | under knob (y 668 / 658) |
| FX DELAY / REVERB | all knobs | **label-slot read-out on panel hover** (below) |
| MOD MATRIX modal | Amount ×8 | beside the knob at `x 816`, baseline `y+14` — rows are 58 apart, so an under-knob read-out sits almost midway between two knobs and captions the wrong one |
| PRISM operator matrix | Ratio + Level per op | combined `"1.00× 80%"` line in the strip's left gutter, centred `(48, top+51)` at font 8.5 — the op rows have no free band (row 1's would land in row 2's label chips, row 2's in the next strip's divider) |

- **Density fallback (`klabelOrValue`)**: FX DELAY and REVERB have no free band under their
  knob rows (DELAY is boxed in by the P-P/TAPE row at y 664; REVERB's row-1 read-out band
  *is* row-2's label band, and row 2 ends 5 px above the panel floor). Those panels swap the
  knob **label** to the value, in accent ink at the same size/weight, while the pointer is
  anywhere in the panel rect — so hovering the panel reads out every value in it at once,
  and the row never reflows. This is preferred over dropping those knobs to hover-only.
  The owning panel is **latched** (`fxReadoutPanel`, re-evaluated only while
  `!IsAnyItemActive()`): a knob drag sweeps the pointer anywhere on screen, so a raw hit
  test flipped the DELAY labels to values while a REVERB knob was being dragged. Latching
  also keeps the panel you grabbed in reading out for the whole gesture.
- **Scale gate (`readoutsOn()`, `s >= 0.72`)**: 9.5 px of design space is <5 device px at the
  620×390 minimum, i.e. unreadable. Below `kReadoutMinS = 0.72` (≈ 893×562) every persistent
  read-out is suppressed and the 12 px hover bubble — drawn on the foreground draw list, so
  it is never clipped — is the only read-out. The bespoke CUTOFF read-out is exempt (it is
  part of the FILTER panel's composition, not a per-knob caption).

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

**`FramePadding.y` is floored at 1 px** wherever it is derived from a row height:
`padY = max(1, (rowH*s − font->FontSize) * 0.5)`. At the 620×390 minimum the atlas hands
back a face whose `FontSize` can exceed `rowH*s`, and the resulting negative padding
collapses the frame (and clips the `InputText` caret) instead of merely tightening it.
Applies to `comboBox()`, the top-bar preset combo and the browser's FIND field alike.

### 3.10 Tooltip system
Every control passes a `tooltip=` string to the shared knob, or (for combos/buttons)
calls `ImGui::SetTooltip("%s", kTip[param])` when `ImGui::IsItemHovered()`. Tooltips live
in one `static constexpr const char* kTooltips[kParamCount]` array indexed by param enum —
see the **full table §6**. Hover delay = ImGui default; no tooltip while dragging.

**On-screen strings are ASCII punctuation only.** The crisp atlas is baked over the
Latin-1 range, so an em dash renders as a `?` box. Use `:` or the middle dot
`"\xC2\xB7"` (which *is* in range, and is what the browser tooltip and footer already
use). Comments in the source are free to use whatever reads best; drawn strings are not.

### 3.11 Host edit-gesture lifecycle (`beginEdit` / `endEdit`)
A `beginEdit` is a promise to the host — "a human is writing this parameter, hold your
latch/recording here" — and only an `endEdit` retracts it. In an immediate-mode UI the
widget that made the promise is not guaranteed to exist on the next frame, so the promise
is **tracked centrally**, in the `duskdpf::ParamHost` overrides, not left to the widget:

- `MultiSynthUI::openEditParam` holds the one open gesture (or `-1`). One slot is enough:
  a drag captures the mouse, and every other path (`setChoice`, wheel, Ctrl-reset,
  double-click reset, `pushParam`) is begin/set/end inside a single call.
- `closeOrphanedEdit()` runs at the **top of `onImGuiDisplay`**, before anything can open
  a new one. Every gesture that spans frames is backed by an ImGui *active item* (that is
  what holds the capture), so "a gesture is open and `IsAnyItemActive()` is false" can
  only mean the owner stopped being submitted — ImGui clears the active id in `NewFrame`
  after one frame without a submission. Running before any widget is what makes the test
  unambiguous: a fresh activation cannot have happened yet.
- `~MultiSynthUI` closes a gesture still open at teardown (window closed mid-drag).

The three ways a widget disappears under an open gesture, all of which leaked before:

| Case | How |
|---|---|
| Mode-conditional widget | Host automation or a MIDI program change writes `mode` mid-drag; the Acid pitch lane / Prism op matrix / every sub-panel knob stops being drawn, and the `IsItemDeactivated()` `endEdit` is in the branch that no longer runs |
| Modal opened | `showMod` / `showBrowse` / `showSaveModal` early-return past the base layers |
| Editor closed | The window is destroyed mid-drag |

Widget-side latches that gate a re-`beginEdit` must fall with the gesture —
`pitchDragging` is cleared by `closeOrphanedEdit()`, or the lane would come back inert and
its next drag would push values with no gesture around them.

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
hidden while Prism is active; the VOICE / CHARACTER panel stays but uses its **compact
variant** `(16,412)–(340,542)` (h130): **r11 knobs** (ring ±17.5), **font-9 labels**, row
centres `y{460,514}` (labels top `y−30`), same 5-column layout `x=42+45·c` and the same
4-item right column at `x291` (items at `y{436,464,492,520}`). Row1 label ink 431..437.075
vs ring top 442.5 = 5.4 px; row2 ring bottom 531.5 vs inner floor 539 = 7.5 px.

- **Operator strips** — 4 stacked rows in `(16,60)–(340,408)`, strip pitch **80**:
  `top = 84 + op*80`, sub-row centres `cy1 = top+18`, `cy2 = top+58`. Each strip: "OP n"
  label + LED (carrier lit brighter) in the left gutter, and **tickless r13 knobs**
  (accent-arc stroke spans R+1.8..R+4.2 → reach ±17.2) in two sub-rows — sub-row 1 (cy1):
  **Ratio · Fine · Level | Vel · Key** (LEVEL|VEL divider `x231`, `top+8..top+32`);
  sub-row 2 (cy2): **A · D · S · R**. Op 4 strip hosts the **Feedback** (`prismFB`) knob
  in the KEY column of sub-row 2. Columns `cxc = {96,150,204,258,312}` (spacing 54 ≥
  arc-reach Ø 34.4 + 4). Knob labels **font 9.5** at `centre−22`, **drawn AFTER the
  knobs**: at mid-range values the accent arc passes through 12 o'clock — through the
  label's bottom ink band — so label-last ordering keeps the letter ink on top (drawing
  labels first lets the arc slice the glyphs illegible). Last strip bottom
  `84+3·80+58+17.2 = 399.2` clears the panel inner floor (405) by ~6 px.
  - **Ratio** is a **stepped knob** over a snap list `{0.25,0.5,0.75,1,1.5,2,3,4,5,6,7,8,
    9,10,11,12,13,14}` — display `%.2f×`; **Fine** is `±99 ct` bipolar. (Per
    `09-multi-synth.md`, Fine/KeyScale may be trimmed if param budget hurts — if trimmed,
    hide those knobs and the strip uses 8 controls.)
- **MODE SUB-PANEL** = **ALGORITHM** widget (§8.6): a row of **8 clickable thumbnail
  diagrams** (`prismAlgo` selector) above one **large diagram** of the active algorithm,
  with the feedback loop drawn on the feedback op and `prismFB` shown.
- **Inline**: FILTER stays in circuit (presets open it) so FM can be filtered — no hiding.

### 4.6 Acid (mode 5) — bass box + pattern sequencer
- **Skin**: silver panel palette. The OSC1 wave combo shows ALL waveforms; Saw is the
  default (the classic acid voice is saw or square, but don't fight the user — every
  wave stays selectable).
- **MODE SUB-PANEL** = **ACID GLOBALS**: `acidAccentAmt` r22 knob + `acidSlideTime` r22
  knob + a big **ACCENT** indicator LED that pulses on accented steps (from bridge step
  index). Title "ACID".
- **SEQUENCER expands to 3 lanes** in `(16,548)–(700,692)` (cells start at `x=62` after a
  left label gutter `x18..58`):
  - **Gate/On lane** (top, y 600..616, h16): the 16 `arpStep*` on/off cells (also used as
    step-mute in other modes).
  - **Pitch lane** (middle, y 620..654, h34): 16 vertical **drag columns**, value
    `seqPitch*` ∈ −24..+24 st. Each column: click-drag vertically sets pitch (drag scale
    `48/(h·s)` picks up the lane height automatically); a filled bar from the center
    (0 st) up/down; numeric `%+d` always shown; center gridline at 0. (h34 is enough —
    the per-cell readout keeps edits precise; the height went to ACC/SLIDE instead.)
  - **Accent + Slide lanes** (bottom, two **h15** rows — accent y 658..673, slide
    y 677..692): 16 cells each for `seqAccent*` (amber) and `seqSlide*` (cyan), same
    dark-cell fill idiom as the gate row (including the §8.4 downbeat split, bevel relief
    and hover outline) plus 4-step group dividers spanning both rows, so the off state
    reads as a lane of real click targets. The Acid lanes keep `groupGap = 0` so all four
    stay column-aligned; the hairline dividers, not daylight, carry the beat grouping.
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
| `lfo1Sync` / `lfo2Sync` | Lock LFO N to the host tempo and song position: the rate scales as rate × BPM/120 (one cycle every 2/rate beats at any tempo) and, while the transport plays, the phase is derived from the song position rather than free-running, so a note-on no longer restarts it. Falls back to the scaled free-run rate when the transport is stopped. |

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
- 16 cells from `x0` to `x1`; `pitch = (x1 - x0 - 3*groupGap)/16`, cell *i* at
  `x0 + i*pitch + (i/4)*groupGap`, pad 2 each side. `InvisibleButton` toggles `arpStep{i}` on
  click (unchanged) and shows its tooltip on hover.
- **Beat rhythm**: the non-acid mute row passes `groupGap = 8`, so the four bars of four are
  separated by **real daylight**. The old row was flush and relied on an `alpha 40` hairline,
  which is invisible against an accent-filled cell — i.e. against the **default all-on
  state**, where all 16 read as one undivided slab. Lanes that must stay column-aligned with
  their neighbours (every Acid lane) pass `groupGap = 0` and keep the hairline instead.
- **Fill**: `on` = accent at alpha **235 on a downbeat** (cells 1/5/9/13), 185 otherwise;
  `off` = `#303238` / `#202226` on the same downbeat split. Downbeats therefore carry a touch
  more ink in **both** states, so the beat grid stays legible whether the pattern is mostly
  lit or mostly muted.
- **Relief** (`cellFace`, shared with the Acid ACC/SLIDE mini-cells): the `panelBox` bevel
  idiom — light top edge + dark bottom edge — with the two **swapped for an off cell**, so a
  muted step reads as a pad pressed *into* the lane and a live one as a pad standing proud of
  it. State is then legible from the relief as well as the fill.
- **Hover**: `alpha 115` white outline. There was no hover affordance at all; nothing told
  you the slabs were targets.
- **Numbers** (`tall` rows only): ink is chosen against the fill via `inkOn()` rather than
  fixed — `live.text` on the pale Cosmos/Mono accents was near-invisible. Muted cells use a
  deliberately dim `#7C8088`, so on/off reads from the numbers too.
- The **live step** (from the bridge) draws a bright `ledOn` border plus a 3 px top bar,
  regardless of on/off.

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
- **Thumbnails**: 8 mini-diagrams in a **4×2 grid** across the sub-panel top, sized to
  fit the 240 px-wide sub-panel: **each 50×38**, 6 px gaps, 8 px side padding
  (`4·50 + 3·6 + 2·8 = 234 ≤ 240` inner width; two rows `2·38 + 6 = 82` inside the
  134 px-tall panel). Thumbnail *k* (row `r=k/4`, col `c=k%4`) top-left at
  `(768 + c·56, 336 + r·44)`. Each is an `InvisibleButton` setting `prismAlgo`; active
  one gets accent border + full brightness, others dimmed to ~50%. Below the two rows
  (from `y≈426`), the **large** diagram of the active algo fills the remaining panel.
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
  `UI::sendNote(0, note, vel)` (DPF, requires `DISTRHO_PLUGIN_WANT_MIDI_INPUT` — this is
  the fleet's first synth, so the shell sets it). On release / drag-off: `sendNote(0,
  prevNote, 0)`. **Glissando**: while dragging, if the hovered key changes, note-off the
  old and note-on the new.
- **Velocity from strike position** (`velFromY`): the click's Y within the key rect maps
  linearly to **30..120** — top edge soft, bottom edge hard. Each key normalizes against
  its OWN height (black keys are 50 design px, white keys 80), so both feel the same. The
  glissando path re-reads the position on the key being entered, so a drag that wanders
  down the keybed crescendos. The top of the range deliberately crosses the engine's Acid
  accent threshold (MIDI velocity > 100, `MultiSynthDSP::kAcidAccentVel`), which the old
  fixed velocity of 100 could never reach — a hard strike now accents like a hardware
  bassline keyboard.
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

### 8.9 Performance wheels (pitch bend + mod)
Two vertical wheels in the keyboard row, left of the keys — the on-screen stand-ins for the
two controllers every hardware synth puts there, and the only way to exercise the `P.Bend`
and `Mod Whl` mod-matrix sources without external hardware.

- **Geometry**: slots `(54,700)–(80,762)` (PB) and `(86,700)–(112,762)` (MOD); 2 px metal
  bezel; labels centred at `y 767`, font 8.5.
- **Render**: recessed slot, a cylinder gradient (lit band across the middle, falling off to
  both rims), and 12 drum ridges at `u = frac(k/12 + phase)` projected as
  `y = mid − halfH·cos(π·u)` so they bunch toward the rims like a real cylinder's. The value
  drives `phase`, so the wheel visibly rolls. An accent bar marks the value; PB also engraves
  centre-detent notches on both slot edges.
- **Interaction**: the pointer maps **absolutely** into the slot (grab-and-bend), not as a
  relative drag — on a 62 px wheel a relative drag would need repeated nudges to reach full
  bend. PB is bipolar and **springs back** to centre on release — an exponential return with
  a **22 ms time constant** (`exp(-dt·45)`), inaudible after **~140 ms** from a full bend —
  glided rather than snapped so the release lands instead of clicking. MOD is unipolar and
  **latches**; wheel-scroll trims it in 5% steps.
- **The spring runs per FRAME, not per draw** (`updateWheels()`, called from
  `onImGuiDisplay` ahead of the modal early-returns). The mod-matrix and save modals replace
  the base layers and return early, so the wheel is not drawn while one is open; driving the
  spring from the widget froze a released bend in the engine for as long as the overlay
  stayed up. `drawWheels()` still pushes at the end of its own frame so a live drag reaches
  the engine with no frame of lag; both pushes are idempotent.
- **Wiring**: straight to `MultiSynthDSP::pitchBend()` / `::modWheel()` through the
  `multiSynthGetDSP` bridge — the same entry points the shell's MIDI handler calls for 0xE0
  and CC 1, both plain relaxed atomic stores, so a UI-thread write is safe. Neither is a host
  parameter (performance state, not patch state): nothing to automate, nothing to save.
  The same atomics are **read back** every frame through `MultiSynthDSP::getPitchBend()` /
  `::getModWheel()` (read-only relaxed loads; no new engine state), which is what lets the
  drawn wheels follow the host and hardware.

#### 8.9a Ownership: who is the wheel this frame

There is **one atomic per wheel**, shared between the shell's MIDI handler and these widgets,
so ownership is resolved every frame in `updateWheels()`:

| Condition | Owner | Behaviour |
|---|---|---|
| widget held (`pbDragging` / `modDragging`) | **local** | **re-assert every frame** — write `*Value` to the engine unconditionally, ignoring the change test |
| otherwise, engine value ≠ last value we pushed | **engine** | an external write (0xE0 / CC 1 from the host or hardware) — adopt it into the widget and redraw at that deflection |
| otherwise | — | nothing to do; the widget already agrees with the engine |

**Both rows are load-bearing, and the first is not optional.** Suppressing adoption while held
is only half of owning the value: `pushWheels()` is change-detected, so a wheel held *still*
leaves `*Value == *Sent` and writes nothing. A host `0xE0` / CC 1 landing in that window would
overwrite the atomic and never be corrected — the screen would show the drag while the engine
sounded the host, which is precisely the divergence adoption exists to kill, inverted. Hence
the unconditional re-assert, so both directions are resolved in the same place.

Adoption runs **before** the spring step, so a bend arriving mid-frame wins that frame rather
than one frame later. On first frame after the editor opens, `*Sent` is 0 and the engine may
hold a latched value, so this same rule **seeds both widgets from the engine** — which is how
a reopened editor draws the mod wheel where the sound actually is.

The two grip flags are **one frame stale** (`drawWheel()` sets them after `updateWheels()`
runs, so they describe the previous frame's grip). Harmless: a grip lasts far longer than a
frame, so authority is asserted — and the spring started — one frame late, ~16 ms.

- **Spring exit condition (derived).** The spring is gated on `pbLocal` — *this UI authored the
  current bend* — not on the old "not held and off centre". It exits when either
  **(a)** the value reaches centre, which is where a released local drag belongs, or
  **(b)** adoption clears `pbLocal` because an external write landed mid-spring, in which case
  the spring abandons the gesture to whoever is now driving.
  The old unconditional rule is a bug once the widget follows MIDI: a bend the host is
  **holding** would be adopted, then immediately sprung, and the decaying UI value would be
  pushed back over the host's — detuning the note the player is still bending. Under the
  `pbLocal` gate a held hardware bend simply stays deflected on screen and untouched in the
  engine.
  Accepted consequence: releasing the on-screen wheel springs to **centre**, not back to a
  bend the host may still be holding. There is one atomic and a held bend is not re-sent, so
  the previous external value is unknowable — this is ordinary last-writer-wins MIDI merge.
- **Teardown** (`~MultiSynthUI`): recentre pitch bend only if `pbLocal` **and the engine still
  agrees the bend is ours** (`getPitchBend() == pbSent`). A bend this UI authored has no widget
  left to release it, so closing mid-drag or mid-spring would leave the engine detuned. A bend
  the *host* is holding is not ours to cancel — zeroing it would snap the pitch of a note still
  being bent just because the editor was closed. `pbLocal` alone does not settle that: the host
  can write `0xE0` *during* a local drag, leaving `pbLocal` true and `pbSent` non-zero while the
  atomic holds the host's value, so the engine has to be asked.
  *Residual hole (accepted):* because a held drag re-asserts every frame, an external write
  during a drag is normally overwritten on the next frame, so by teardown the engine does agree
  and the recentre fires. Only a close inside the one frame between the host's write and our
  next push escapes — ~16 ms, self-correcting on the next message. The gate still earns its
  keep by closing the same window during the **spring**, where nothing re-asserts and adoption
  would need one more frame to clear `pbLocal`.
  The **mod wheel is not reset at all**: it latches like the hardware it stands in for, and the
  old reset existed only to stop a reopened UI drawing 0 against a non-zero engine — a
  workaround that `getModWheel()` makes obsolete.
- **Limits (accepted)**: a split LV2 UI has no bridge, so the wheels neither drive nor follow
  the engine and are drawn inert with an explanatory tooltip.

### 8.10 Preset browser (`showBrowse`)
The combo is a list you scroll; the browser is the library you read. BROWSE (§1.1) opens it
over the same scrim and with the same **replace-panels** rule as the mod matrix — the base
layers are not submitted at all while it is up, and it draws into its own layer window
(`MSbrowse`, `kLayerBrowse`, own draw list and own vertex budget). `updateWheels()` still
runs *before* the early return, so a pitch bend released as the browser opened keeps
springing back (§8.9).

- **Geometry** (design space). Panel `(48,58)–(1192,740)`; 20 px inner margin → content band
  `x 68..1172` (1104 wide).

  | Band | `y` | Contents |
  |---|---|---|
  | title + close ✕ | `66..90` | "PRESET BROWSER" (15 px accent); ✕ `(1156,66)–(1180,90)` |
  | search row | `104..130` | "FIND" label `x68`; `InputText` `(110,104)` w318 with a dim "search preset names" placeholder when empty and unfocused; bank chips `ALL / FACTORY / USER` w84 at `x{908,998,1088}` |
  | mode chips | `140..166` | `ALL` + the six mode names, w96 at `x = 68 + i*102` (→ `68..776`) |
  | grid | `178..678` | 4 columns w270 on a 278 pitch (`4*270 + 3*8 = 1104` exactly); 12 rows h38 on a 42 pitch (`12*42 − 4 = 500 = 678−178`) → **48 cells on screen** |
  | footer | `690..720` | "N of M presets" + hint line; APPLY `(920,690)` and CLOSE `(1052,690)`, both 120×30 |

  Scroll indicator (only when the list is taller than 12 rows) at `x 1178..1183`, thumb
  height/offset = `rows_visible / rows_total`.
- **Cell** (270×38): name at `x0+12`, `y0+14`, font 11. Mode badge
  `(x1−116,y0+11)–(x1−58,y0+27)` filled with that mode's **accent** (§4.0), label font 8 in an
  ink picked per swatch by luminance. Bank tag right-aligned at `x1−12`, font 8: `FACTORY` dim,
  `USER` accent. Cell fill/border are dark in every skin (like the combos) so they read on
  Acid's silver panel. The **loaded** preset gets an accent gutter bar `(x0+2)–(x0+5)` plus an
  accent name; the **cursor** gets a 1.8 px accent border; hover lightens the fill. Hover
  tooltip = `name · MODE · factory|user preset[ (loaded)]`.
  - **The name draw is clipped** to `x0+12 .. nameX1` (`PushClipRect`). Factory names top out
    at 18 chars, but a user name is whatever fitted in `saveNameBuf` (**128**) and
    `panel.text()` never clips — unclipped, a long one overprints the badge and bleeds into
    the next column. An over-long name is cut; the tooltip always has it in full.
  - **Below `kReadoutMinS`** (§3.1b) badge and tag would be 4 device px, the same mush the
    read-out gate exists to suppress, so the row **degrades** instead of disappearing: the
    badge becomes a text-less colour swatch `(x1−30,y0+11)–(x1−12,y0+27)` — the palette is
    itself the mode cue — the bank tag drops, and the name band widens to `x1−36`
    (222 px ≈ 40 chars). BROWSE stays available at every size; the tooltip carries mode and
    bank verbatim regardless of scale.
- **Filters**: substring search is case-insensitive and allocation-free (the needle is
  pre-lowered into a stack buffer and matched in place); mode chips filter on a **derived**
  tag — factory from a walk of each preset's own override table for its `kParamMode` row
  (cached in the ctor), user from the `mode=` line `scpreset::Store::refresh()` parses.
  No tagging metadata beyond mode and bank exists, and none is invented.
- **No per-frame work**: `browseIdx[]` is a fixed member array of combined indices, rebuilt
  **only** when a filter changes (`browseDirty`, so a search edit rebuilds at the top of the
  next frame and the index stays stable for the grid already being submitted). The grid draw
  walks it directly; nothing allocates while the browser is open.
- **Scrolling is by whole ROWS.** ImGui only culls items that are *fully* clipped, so a pixel
  scroll would leave half-cells at the band edges with live hit boxes hanging over the filter
  row. The wheel's fractional deltas are **accumulated** in `browseWheelAcc` and consumed a row
  at a time — rounding per event drops every `|delta| < 0.5`, which reads as a dead grid on
  precision touchpads, the hardware that sends the smallest steps. A partial notch is dropped
  when the pointer leaves the grid. After a wheel scroll the **cursor is clamped back into
  view** (`clampBrowseSelToView()`, keeping its column) — Enter loads whatever the cursor is on
  and a patch load has no undo, so it must never sit off screen. Arrow-key moves do the mirror
  of this and scroll the view to the cursor.
- **Loading** goes through `applyCombined()` — the identical entry point ◀/▶ use, dispatching
  to `applyPreset()` (factory) or the user-store load, so the **U1** limitation is unchanged:
  a DPF UI cannot ask the host to load a program, so the preset's parameters are pushed
  directly, followed by `notifyDspProgramChange()`. Click loads and leaves the browser open
  (the skin changes around you); double-click or APPLY loads and closes; ✕ / CLOSE / Esc /
  a click on the scrim close without loading anything further. The load itself is deferred to
  the end of the frame, after every widget has been submitted.
  **One gesture costs one load**: a double-click is two clicks, so the single-click branch
  stands down once `MouseClickedCount > 1` and the double-click branch only loads if click 1
  did not already land it — otherwise the host sees 222 parameter writes twice for one gesture.
- **`openBrowse()` deliberately does NOT rescan the user directory**: `currentPreset`
  addresses the user bank by index, and a rescan can renumber it, silently repointing both
  the highlight and the save modal's DELETE at a different patch. The store is rescanned only
  where the UI itself changed it (save / delete), which is the combo's contract too.
- **Keyboard**: Up/Down move one row, Left/Right one cell, Enter loads and closes. Up/Down are
  read unconditionally — a single-line `InputText` only takes ownership of
  Left/Right/Enter/Home/End (`imgui_widgets.cpp`, `always_owned_keys`; Up/Down are claimed for
  multiline only) — so you can type into FIND and arrow straight down into the results.
  Left/Right yield to an active field so they still walk the search text. Enter arrives either
  as the `EnterReturnsTrue` return value or, with nothing active, as a plain key press; the
  two paths are mutually exclusive so a preset is never loaded twice on one frame.
  **Enter rebuilds a dirty index first**: an edit only sets `browseDirty` (the rebuild is
  deferred so the index stays stable for the grid already submitted), but Enter acts now, so
  typing a query and hitting Enter in one gesture would otherwise load out of the *pre-edit*
  list — the wrong preset.
- **Esc stages**: it clears a typed query first and only closes the browser once the list is
  unfiltered again. The test is the query as it stood at the **start of the frame**
  (`browseSearchHadText`), not the live buffer: ImGui's `InputText` handles Esc inside its own
  call, reverting the buffer to its focus-time value and clearing the active id, so a field
  that held a query a moment earlier reads as empty and inactive — testing the live buffer
  closes the browser on the very keystroke meant to clear the search.

### 8.11 Save-user-preset modal (`showSaveModal`)
Panel `(420,285)–(820,495)`, same scrim + replace-panels rule as §8.10. Name `InputText`
at `(440,351)` w360; hint line at `y 393`; button row at `y 435` — SAVE `(440)`, CANCEL
`(572)`, DELETE right-aligned `(680)` and only present when a **user** preset is loaded.

- **Name validity is cached, not recomputed per frame.** `sanitize()` plus `exists()`
  (which builds four `fs::path`s inside `presetDir()` and then `stat()`s the folder) ran
  on every frame the modal was up. They are recomputed on `ImGui::IsItemEdited()` and once
  on open (`saveNameDirty`) — the same idiom as the browser's `browseDirty` index.
- **Esc backs out one step at a time**, matching §8.10: overwrite-confirm → delete-confirm
  → clear a non-empty name (caret restored) → close. It used to close the modal outright,
  so Esc mid-typing — the reflex for "no, not that name" — threw away the dialog with the
  name. The name test uses the value captured at the **start of the frame**
  (`saveNameHadText`), for the exact reason `browseSearchHadText` exists.
- **A failed write keeps the modal open** and puts the reason in the hint slot in red,
  outranking both advisories, holding until the name is edited. It used to close
  unconditionally, which made the two failures a player actually meets — a read-only
  preset folder and a full disk — indistinguishable from success, with the patch gone.
  `scpreset::Store::save()` takes an optional `std::string* errOut` and reports three
  distinct sources: `create_directories`' `error_code` (previously discarded outright),
  the `ofstream` open via `errno`, and the final `flush` via `errno` — the last is where a
  full disk usually lands, since the ~222-line body is buffered and only the flush hits
  the wall. `commitDelete()` follows the same contract.

Hint-slot priority, one line, in order: **save/delete error** → "Enter a name to save." →
"Name in use · saving will ask to overwrite."

### 8.12 Preset combo popup — grouped, multi-column
TapeMachine 2's preset combo (`TapeMachineUI.cpp:328`) groups its list with
`ImGui::TextDisabled` category headers in **one** column and lets ImGui scroll it — no
`Columns`, no `BeginTable`, no popup size override, just headers emitted on a category
change. That reads fine at twenty presets; at **54** it is a scroll hunt. So the same idea
is laid out **across**: one group per **mode** — this synth's strongest cue, and the same
grouping the browser's chips use — stacked into as many columns as the window can hold, so
the whole library is on screen at once.

- **Still one ImGui window.** Columns are `BeginGroup` / `SameLine` *inside the combo's own
  popup*, not sibling windows, so the no-overlapping-windows constraint (§8.10) is not in
  play. A combo popup is the one layer this backend *does* composite over the base windows,
  which is why the single-column version worked; nothing here changes that.
- **The popup gets its natural size** because we call `SetNextWindowSizeConstraints`
  ourselves before `BeginCombo`: ImGui only imposes its "8 items tall, one column wide"
  default when no constraint is pending (`imgui.cpp`, `BeginComboPopup`). The height is
  capped at `0.86 × getHeight()` — ImGui clamps a popup's *position* into the viewport,
  never its size, so an uncapped one with a large user bank would hang off the bottom.
- **Column width is measured from the live atlas face**, not from the design size:
  `pickFont()` snaps to the nearest baked size, so at small scales the glyphs are *larger*
  than `12*s` and a design-space width would clip exactly the names this exists to stop
  clipping. Measured once per opening (`IsWindowAppearing`), capped at `0.32 × getWidth()`
  so one 128-char user name cannot push the grid off the window.
- **Column count adapts**: `floor((getWidth() − 2*WindowPadding.x) / (colW + ItemSpacing.x))`,
  clamped to `[1, nGroups]`. Seven 1:1 columns are ~1.1 kpx and sit inside 1240 comfortably;
  at the 620×390 minimum the atlas floor keeps the glyphs near full size while the window
  halves, so the same seven would run off the screen edge.
- **Groups are never split** across columns — a mode's presets are the unit. A greedy
  height balance keeps filling a column until the next group would push it past
  `ceil(totalRows / nCols)`; with the usual one-column-per-group outcome the target never
  binds, and it only does work when the window forces two groups to share.
- Headers are drawn in **that mode's** accent (not the live one), with a hairline rule, so
  the column a preset lives in is identifiable at a glance even mid-crossfade. A non-empty
  user bank adds a 7th `USER` group in the live accent.

Measured: 6 columns / 54 presets, no scrolling, at both 1240×780 and 620×390. 60 user
presets at 620×390 degrades as designed — 6 columns, two mode groups sharing one, and the
USER column scrolling. Vertex cost is in §9.1.

---

## 9. Rendering & performance notes
- **Everything in design-space coords × `s`.** No absolute pixel literals in draws except
  through `P()`.
- **Fonts**: build one `CrispFontSet` with design sizes `{9, 10, 11, 12, 13, 15, 20, 26}`
  `× getScaleFactor()` (playbook §4). `panel.setFontSet(set)`; `pickFont` chooses nearest.
  Fallback to the ImGui default when no TTF is found (shared loader handles it).
- **Window flags**: `NoTitleBar|NoResize|NoMove|NoCollapse|NoScrollbar|NoBackground|
  NoScrollWithMouse` (the last so knob wheel works, per shared knob comment). These
  apply to the fullscreen ImGui surface INSIDE the plugin window — the native window
  itself stays resizable via `setGeometryConstraints` below (`NoResize` here prevents
  ImGui's own drag-corner on the surface, which must always fill the window).
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
  exactly like `TapeEchoUI`'s `tapeEchoGetOutputLevel`. The contract is **per symbol** —
  each accessor is its own weak symbol, resolved or not on its own — so a call site that
  uses two of them must null-check **both**; taking L as proof of R is a null call away
  from a crash, and the pair is meaningless half-filled anyway.
- **No allocations in the frame loop**: fixed `values[kParamCount]`, fixed scratch arrays
  for curve/scope/adsr polylines (members), `snprintf` into stack buffers.

### 9.1 Vertex budget (the reason for the layer windows)
The DPF DearImGui backend has no `VtxOffset` support, so **one window's draw list corrupts
past 65535 vertices** (`ImDrawIdx` is 16-bit). The frame is therefore split into
non-overlapping borderless layer windows — top / left / center / right / bottom, plus a
single modal layer that *replaces* the base panels — each with its own draw list.

Every layer ends through `endLayer(kLayer…)`, which in a `-DMSYNTH_FRAME_PROFILE` build
samples `ImGui::GetWindowDrawList()->VtxBuffer.Size` before `ImGui::End()` and keeps a
running per-layer maximum, printed to stderr with the frame timings every 100 frames.
Re-run it after adding chrome:

```
cmake -S . -B build-vtx -GNinja -DCMAKE_CXX_FLAGS=-DMSYNTH_FRAME_PROFILE
cmake --build build-vtx --target sunset_circuits-jack
# then visit every mode and open both modals; read the MSYNTH_VTX lines
```

Census at 1240×780 / 1860×1170, all six modes visited, both modals opened:

| Layer | max verts | of 65535 | driver |
|---|---|---|---|
| **left (Prism)** | **49 466** | **75.5 %** | 37 chrome knobs in the operator matrix (4 ops × 9 + FB) |
| left (other modes) | 21 428 | 32.7 % | OSC 1/2/3 + VOICE / CHARACTER (max is Modular, which draws two OSC 3 knobs) |
| bottom | 22 094 | 33.7 % | sequencer lanes + FX strip + keyboard + wheels (max is Acid, four lanes) |
| center | 16 508 | 25.2 % | filter curve + 2 ADSR displays + 8 knobs |
| right | 14 672 | 22.4 % | LFOs + sub-panel + scope + VU |
| browse | 10 616 | 16.2 % | 48 preset cells (57 presets listed, scrolled + hovered) |
| modal | 9 692 | 14.8 % | 8 rows of combos/knobs (measured with a combo popup open) |
| **popup** | **2 580** | **3.9 %** | preset combo popup: 6 columns × 54 presets (§8.12) |
| top | 2 142 | 3.3 % | nameplate + 6 rockers + preset cluster |

`popup` is the one census slot that is **not** a layer window — it is the preset combo's own
popup, sampled from inside `drawPresetPopupBody()`. A popup is a separate ImGui window with
its own draw list, so the multi-column grid has its own 16-bit budget and would otherwise
never appear here at all. With a 60-preset user bank (a 7th column, 114 rows) it measures
**4 036** (6.2 %); beyond that the popup scrolls and ImGui culls what is off-screen, so it
is bounded by what fits on the window, not by the size of the library.

**Measure per mode, not per run.** `endLayer()` keeps a running max, so a single sweep that
visits all six modes reports only the heaviest mode for each layer — a per-mode increase in a
lighter mode is invisible. Launch once per mode instead. Deterministic across repeats
(re-measured three times for Acid and twice for Prism, identical each time). Values below are
`-DMSYNTH_FRAME_PROFILE` with every lane row, both OSC knob rows and both wheels hovered:

| mode | left | bottom | Δ left | Δ bottom |
|---|---|---|---|---|
| 0 Cosmos | 19 856 | 20 562 | 0 | −96 |
| 1 Oracle | 20 434 | 20 562 | 0 | −96 |
| 2 Mono | 20 380 | 20 562 | 0 | −96 |
| 3 Modular | **21 428** | 20 634 | 0 | −96 |
| 4 Prism | **49 466** | 20 562 | 0 | −96 |
| 5 Acid | 19 856 | **22 094** | 0 | **+120** |

Δ is against `e997c58`, the commit before the sequencer / wheel / oscillator work, built from a
worktree with the same flags and swept with the same script. Reading them:

- **left is bit-for-bit unchanged in every mode.** Chrome knobs tessellate with fixed segment
  counts, so r20→r18 and r14→r18 cost nothing. Earlier revisions of this table carried 49 754
  (Prism) and 20 626 (other modes); those were older sweeps with lighter hover coverage, *not*
  a regression — the same binary measures 49 466 / 21 428 on both sides of the change.
- **bottom falls 96 in the five non-acid modes** and rises 120 in Acid. Both follow from the
  same two edits: dropping the tick rings from OCT/GATE/SWING removes 33 ring lines, and
  `cellFace` adds two bevel lines per cell. Non-acid draws 16 cells (+32 lines, −33 rings, plus
  a shorter lane and three read-outs) and nets out slightly cheaper; Acid draws 48 cells across
  its four lanes (+96 lines) and nets out dearer. The worst case moved 21 974 → 22 094, i.e.
  33.5 % → 33.7 % of the 16-bit index budget.

> A multi-mode sweep has been seen to report bottom = 22 142 in Acid, ~50 verts above the
> steady-state figure, when Acid is reached by switching modes rather than measured cold.
> Budget against 22 142; the per-mode numbers are the reproducible ones.

The worst case is **not** the mod-matrix modal (14.8 %) but the **Prism operator matrix**,
at ~76 %. That is the layer to watch. Vertex counts are near scale-invariant
(rounded-corner tessellation adds only ~0.4 % going from 1× to 1.5×).

**Headroom, measured rather than estimated.** A control run that added one extra r13 knob
per operator strip moved `left` in Prism **49 466 → 53 338**, i.e. **3 872 for four** →
**≈ 968 vertices per r13 chrome knob** (body + tick ring + accent arc + chip label). The
remaining `65 535 − 49 466 = 16 069` is therefore room for **≈ 16 more knobs** in Prism's
left column — and for far fewer if they arrive with panels, dividers and read-outs
attached. `MScenter` (25.0 %) and `MSright` (20.3 %) are where anything larger belongs.

The `endLayer()` comment in `MultiSynthUI.cpp` used to name the mod-matrix modal as the
worst case, contradicting this table by 5×; it now carries the same numbers, the per-knob
cost, and the per-mode figures, and `drawPrismOps()` carries a pointer back to it.

---

## 10. Interaction map (summary)
- **Knobs**: drag (fine on Shift), wheel, double-click type, Ctrl/Cmd-click reset,
  right-click reset [enable via `rightClickReset`]. Hover → name bubble; drag → value bubble.
- **Combos**: click to open; selecting sets the choice param (begin/set/end edit).
- **LED-buttons / rockers / step cells / pitch columns**: click / drag as in §8.
- **Keyboard**: press → note-on with velocity from the strike position (30..120, §8.7);
  release/drag-off → note-off; drag → glissando;
  OCT± shift base. Host MIDI lights keys.
- **Wheels** (§8.9): drag anywhere in the slot to set the value directly; PB springs back to
  centre on release, MOD latches and also takes wheel-scroll trim.
- **Preset cluster**: ◀/▶ step `currentPreset` and apply (like tape-echo `applyPreset`,
  iterating the static preset table); the combo opens a **mode-grouped multi-column popup**
  showing the whole library at once (§8.12); **SAVE** saves (§8.11); **BROWSE** opens the
  searchable browser (§8.10) — click loads, double-click / APPLY loads and closes,
  Esc / scrim / ✕ / CLOSE close, arrows + Enter navigate.
- **MOD overlay**: MOD MATRIX button toggles; click-scrim, ✕ or **Esc** closes. Esc with a
  Source/Dest dropdown open belongs to the dropdown: ImGui closes that popup itself, in
  `NewFrame` (`NavUpdateCancelRequest`) and without consuming the key, so the overlay tests
  the popup state as it stood at the **top of the frame** (`modPopupWasOpen`) — otherwise
  one keystroke closes both.
- **Save modal**: §8.11 — Esc ladder, cached name validity, visible write failures.
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
14. **Preset browser** (§8.10) — after the modals exist, since it reuses their
    replace-panels pattern and the user-preset store.

---
*Multi-Synth UI spec | 1240×780 fixed design space | Dear ImGui / ImDrawList | Dusk Audio*
