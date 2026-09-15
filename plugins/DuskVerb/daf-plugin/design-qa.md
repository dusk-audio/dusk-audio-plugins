# Selected native UI — 2026-09-12

Final result: passed for the captured Linux/X11 editor and interaction scope below. This is not cross-platform release certification.

## Reference and implementation

Reference: the user's selected Space Focus image, with their subsequent requirement that both LED rails extend through the full body height. Source image: `/home/marc/.codex/generated_images/01a09561-61ea-7dd0-981c-2a34fec1abaf/exec-47bf0479-604c-42f8-a73f-2bcdceeed7c8.png`.

Native captures and test sources/logs: `/home/marc/.cache/duskverb2-implementation-20260912/selected-ui/`.

- `hall-final.png`: default 1200×800, Vocal Plate/Hall.
- `spring-min.png`: minimum 1050×700, Spring labels.
- `gated.png`: Gated labels and visible Gate switch.
- `shimmer-large.png`: 1500×1000, Shimmer Pitch/Feedback.
- `overrides.png`: synced pre-delay and Bus/100% wet indicators.
- `marker-after.png`: host automation retains the preset identity and shows its edited marker.
- `help-final.png`: updated help, including explicit COPY and always-visible Mono Depth.

The source and native default capture were opened together for comparison. Both have a 3:2 aspect ratio; the source is 1536×1024 and the default native capture is 1200×800. Minimum and enlarged captures were separately inspected. Detailed control labels, damping spacing, hero entry, header actions, and help content were inspected in native-resolution captures.

## Findings resolved during implementation

- P2: initial native type remained too small despite extra space. Increased main labels to 18 design px and values to 22; used matching embedded font sizes. Main label/value sizes at minimum are nominally 15.75/19.25 px.
- P2: larger values approached the knob rings, and lower-row labels approached section headings. Increased value/label offsets, moved filter/ER rows, and adjusted lower-row radii.
- P2: Mono Depth depended on a small disclosure caret. It now has a permanent control beside Mono Below.
- P2: clicking an active A/B slot copied implicitly. A/B now switches only; COPY is a separate button with directional tooltip and feedback.
- P2: host automation could change an engine without displaying the edited marker. The UI now reads the authoritative edited status on host parameter feedback. Before/after captures show the missing/restored marker without changing the sound.
- P2: old help described the removed caret and implicit A/B copying; larger help text and updated instructions now fit the modal.

## Fidelity and functional review

- Typography: embedded native fonts, readable labels and values, deliberate tracking and hierarchy; the brand uses the bundled condensed face rather than introducing an additional font dependency.
- Spacing/layout: central Decay/Size and history, input/filter at left, output/ER at right, full-width damping, modulation/macro below. Full-body input/output rails satisfy the user's explicit change to the reference.
- Colors: dark blue/charcoal surfaces, warm labels, lime accents; clear active switches and A/B selection.
- Image quality: live native vector controls and crisp font atlases; no bitmap of the mockup is used as the interface. The reference's baked texture/glow is represented with the existing native control rendering.
- Content: all values come from the real parameters. Mono Depth is 100% in the actual factory preset, not the mockup's illustrative 0%. The live graph is named OUTPUT HISTORY rather than claiming to measure RT60; it is flat and meters are dark when the host supplies silence. Bus remains a binary switch, consistent with its parameter, rather than the mockup's apparent dropdown.

## Interaction evidence

Four native CLAP passes each checked all 24 visible knobs in both drag directions, verified the exact parameter ID and balanced gestures, and typed engine-aware values via the label targets. Hall/default, Spring/minimum, Gated/default and Shimmer/enlarged each passed 73 checks including separate A/B copy: 292 total.

An additional Shimmer pass checked all 24 controls for fine drag, both wheel directions, modifier reset and Escape cancellation, plus A/B: 121 passed. Total: 413 native interaction checks. The harnesses use XTest, a private X server and the actual built CLAP, with host parameter readback.

Earlier exploratory failures from incorrect knob coordinates and an incomplete X11 punctuation mapping in the test driver were diagnosed and corrected; they are not product regression evidence. These broad qualification harnesses live with the artifacts, not in the permanent CTest suite. The existing permanent native drag/resize tests remain enabled and use the new hero coordinates.

No remaining P0/P1/P2 visual defects were found in the captured scope. Real mixed-DPI monitor transitions, Wayland, Windows/macOS native GUI interaction, assistive-technology behavior, and comprehensive DAW session workflows remain outside this pass.

## Filter spacing refinement

Following the user's feedback, widened the input/filter column by 40 design pixels and increased filter control spacing from 85 to 96 pixels. The clear gap between the knob rings grows from approximately 15 to 26 pixels. The center panel is narrower and Decay moves 20 pixels right; fonts, knob sizes, parameter behavior and full-height rails are retained.

Latest captures: `selected-ui/filter-1200.png` and `selected-ui/filter-1050.png` under the artifact directory above. Both were visually inspected; filter labels, readouts and adjacent center controls fit without overlap. Build passed; native drag/resize CTest **2/2 passed** (6.15 s). At both sizes the four filter controls and relocated Decay passed both drag directions, exact parameter identity, typed label entry and balanced gestures; A/B checks also passed (**32 checks total**). This geometry-only refinement was validated locally on Linux.


Input layout refinement (2026-09-12): balanced Pre-delay and Saturation at design x=150/342; moved Sync into a dedicated bottom row, retaining the active-sync free-value cue. No parameter mapping or DSP changes. Native Linux build passed; default/minimum knob drag and typed-entry plus A/B checks passed (14 checks total); UiDrag and Resize passed 2/2 in 5.23 seconds. Default, minimum, and active-sync screenshots inspected without clipping. Evidence: /home/marc/.cache/duskverb2-implementation-20260912/selected-ui/input-*. No additional macOS/Windows qualification for this layout-only refinement.
