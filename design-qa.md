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
