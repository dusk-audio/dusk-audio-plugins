# Multi-Comp 2 Opto Rack-Proportion QA

## Evidence

- Source visual truth: `b/uad-la2a-silver-reference.png`, the official 1908 x
  530 UAD LA-2A Silver faceplate supplied as the realism and density reference.
- Initial compact implementation: `b/mc2-opto-compact-preview.png`.
- Revised native implementation: `b/mc2-opto-centered-preview.png`.
- Normalized implementation faceplate: `b/mc2-opto-faceplate-normalized.png`.
- Full-view comparison input: `b/mc2-opto-reference-comparison.png`, with the
  source on top and the revised implementation below in the same image.
- Focused VU comparison input: `b/mc2-opto-vu-comparison.png`, with the source on
  the left and the revised implementation on the right.
- State: Opto mode, Smooth Opto Vocal, Compress, bypass off.
- Native viewport: 1120 x 380 design pixels; captured as a 2464 x 1048 macOS
  window at 2x backing density, including the title bar and capture shadow.
- Density normalization: the 2240 x 620 implementation faceplate was cropped
  from the native capture, downsampled to 1908 x 528 with Lanczos, and padded by
  one pixel above and below to match the source's 1908 x 530 dimensions.

## Findings

No actionable P0, P1, or P2 visual differences remain for the requested rack
proportion, control centering, or VU correction.

- Fonts and typography: the implementation preserves the product's established
  crisp sans hierarchy while following the source's large red faceplate title,
  compact engraved control labels, and restrained meter typography. The Dusk
  wording is intentionally original rather than reproducing the source logo.
- Spacing and layout rhythm: the implementation faceplate is 1120 x 310 design
  pixels, an aspect of 3.613:1 versus the 3.600:1 source. The switch and main
  knob centers sit at 54.8% of faceplate height, matching the source's visual
  center and leaving a narrow, even lower rail instead of a blank lower field.
- Colors and visual tokens: brushed silver, graphite housings, amber meter
  illumination, black engraving, and restrained red accents retain the source's
  hardware palette without copying its branding.
- Image quality and asset fidelity: the native renderer is sharp at 2x, with no
  stretched bitmap, placeholder, halo, or compression artifact. Hardware is
  rendered in the plugin's established DAF style; no third-party logo asset is
  reproduced.
- Copy and content: the face says `LEVELING AMPLIFIER`; `DUSK AUDIO` appears only
  in the common header. Gain, Peak Reduction, Mix, Compress/Limit, GR, and Opto
  Cell labels remain complete and coherent.
- Icons and hardware details: rack slots, fasteners, knob scales, pointer marks,
  switch hardware, and the meter bezel remain aligned and stylistically
  consistent. The common-header arrows and actions are unchanged.
- Accessibility and resilience: active Compress/Limit state uses switch position,
  label brightness, and a red rule rather than color alone. Uniform aspect-locked
  scaling preserves text and hit-target relationships.
- Interaction/state scope: the edited knob and switch hit areas use the same
  coordinates as their visible bodies. Mode-height selection is covered by the
  plugin-layer regression for Opto, fractional automation around the mode
  boundary, non-Opto modes, and Multiband. Browser-console checks are not
  applicable to this native DAF/OpenGL editor.

The full comparison shows the complete rack composition at matched dimensions.
The focused meter comparison was required because the initial compact pass left
the arc radius from the former tall housing, which was difficult to judge at
full-view scale.

## Comparison History

- Initial P1: the previous 1120 x 416 faceplate had a 2.692:1 aspect and retained
  substantially more lower chassis than the 3.600:1 source.
- Fix: reduced the Opto faceplate to 1120 x 310 and the Opto editor to 1120 x 380.
  Added mode-aware geometry so non-Opto modes, especially Multiband, retain the
  original 1120 x 486 canvas.
- Post-fix P2: `b/mc2-opto-compact-preview.png` showed the switch and knobs below
  the faceplate's visual center, while the unchanged 185-pixel VU radius crowded
  the shortened meter's top label and tick arc.
- Fix: moved the switch, Gain, Peak Reduction, and Mix centers up by 45 design
  pixels and recalibrated the VU radius from 185 to 165 design pixels.
- Post-fix evidence: `b/mc2-opto-reference-comparison.png` shows matched rack
  proportion and centered controls; `b/mc2-opto-vu-comparison.png` shows clear
  separation between the VU title, arc, labels, and needle.

## Implementation Checklist

- [x] Match the source-like shallow rack proportion.
- [x] Remove the excessive lower empty field.
- [x] Center the switch and knobs vertically.
- [x] Recalibrate the VU arc for the shorter housing.
- [x] Keep original Dusk branding and copy.
- [x] Preserve the taller canvas for all non-Opto modes.
- [x] Compare source and revised implementation together at equal dimensions.
- [x] Inspect the meter in a focused, equal-size comparison.

## Follow-up Polish

No P3 visual follow-up is required for this scope.

final result: passed

---

# Multicomp-2 FET Design QA

**Source visual truth**

- User-supplied 1176-style front-panel reference, locally grounded at `build-multi-comp-1176/qa/1176ln-reference.jpg`.
- Source pixels: 2560 × 532.

**Implementation evidence**

- Final hosted-AU screenshot: `build-multi-comp-1176/qa/au-fet-one-db-meter.png`.
- Captured pixels: 2240 × 760 at a 2× backing scale; native editor viewport:
  1120 × 380 design pixels.
- State: FET mode, default FET controls, ratio 4:1, meter fixed to GR.
- Internal product precedent: the shipping Opto face and its `drawOptoMeter`
  renderer in `MultiCompUI.cpp`.

**Normalization and comparison**

- Full-view inspection used the final 2240 × 760 hosted-AU screenshot.
- Focused same-input comparison:
  `build-multi-comp-1176/qa/fet-one-db-comparison.png` (2240 × 310).
- Focused meter comparison:
  `build-multi-comp-1176/qa/fet-one-db-meter-focused.png` (1280 × 390),
  with the hardware reference on the left and final AU meter on the right.
- For the focused comparison, the source face was cropped to 2350 × 430,
  scaled proportionally to 1120 px wide, and vertically padded to 1120 × 310.
  The 2240 × 620 implementation face was cropped from the 2× capture and
  downsampled to 1120 × 310. Neither face was stretched.

**Findings**

- No actionable P0, P1, or P2 visual differences remain.
- Fonts and typography: the compact industrial hierarchy, calibrated scales,
  uppercase labels, and centered rack branding follow the source. Dusk's
  existing crisp font replaces the photographed manufacturer's type intentionally.
- Spacing and layout rhythm: the primary order matches the source—Input,
  Output, stacked Attack/Release, Ratio, VU, Meter. Threshold, Transient, and
  Curve are no longer visible on the vintage face. Mix remains as the only
  small product extension, placed at the lower-right edge.
- Colors and visual tokens: black brushed face, restrained off-white markings, dark switch banks, machined silver controls, and amber VU treatment match the source palette.
- Image quality and asset fidelity: the FET face now calls the exact Opto VU
  renderer rather than maintaining a weaker FET-only approximation. Both large
  and timing knobs use center-aligned metal layers; the prior upper-left gray
  crescent is absent in the final capture. The photographed UA logo and branding
  were intentionally not reproduced; original FET 76 / MC-2 branding is used.
- Meter scale and copy: the shared Opto/FET VU contains 21 marks across its
  0–20 dB gain-reduction span, producing one mark per dB. Five-dB marks remain
  longer and numbered for hierarchy. The redundant `VU LEVEL INDICATOR` legend
  is absent in both final AU captures; `VU`, `GR`, and `GAIN REDUCTION` remain.
- Copy and content: hardware labels and ratio ordering match the source while
  preserving the All-buttons option. Meter +8/+4/Off positions are decorative
  because this plugin exposes only GR metering; GR is visibly fixed as active.

**Comparison history**

1. `fet-before-vst3.png`: P1—generic compressor row, wrong control order, small uniform knobs, no integrated hardware identity.
   Fix: rebuilt the black face, added large gain knobs, stacked timing controls, vertical ratio bank, and amber analogue meter.
2. `fet-pass-1.png` / `fet-pass-2.png`: P1—hardware row was closer, but an
   always-visible MC-2 extension row made the mode taller and unlike the real
   panel. Fix: followed the existing Opto pattern, removed the second row, and
   returned FET to 1120 × 380.
3. `fet-pass-3-opto-layout.png`: P1—Threshold, Transient, and Curve still added
   faceplate clutter; the FET-only VU was visually weaker than the proven Opto
   VU; offset opaque knob disks created gray upper-left crescents.
4. `fet-pass-4-opto-meter.png`: removed those three visible controls, reused the
   Opto VU renderer, and rebuilt each gray knob from concentric metal layers.
   The final source/implementation comparison shows the requested hardware order,
   clean knob silhouettes, shallow rack format, and original Dusk identity.
5. `au-fet-one-db-meter.png` and `au-opto-one-db-meter.png`: reduced the shared
   VU from 41 half-dB marks to 21 one-dB marks and removed its top legend. The
   revised FET/reference comparison shows the calmer scale; the direct AU
   captures confirm that the same renderer is used in both modes.

**Implementation checklist**

- [x] Use exactly one gain-reduction tick per dB in Opto and FET.
- [x] Remove the `VU LEVEL INDICATOR` legend from the shared meter.
- [x] Render and inspect both modes through the AU component.
- [x] Compare the revised FET face with the source at matched dimensions.

**Verification gaps / follow-up polish**

- P3: a manual DAW pass can still judge hover bubbles and very small-label
  readability at the minimum resize scale. The shared knob interaction path and
  FET ratio source contract compile and pass the plugin-layer test, but pointer
  interaction was not automated in the screenshot host.

**Final result**

final result: passed
