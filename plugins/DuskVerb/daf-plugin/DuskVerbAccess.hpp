// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbAccess.hpp — the same-process DSP accessors the ImGui UI reads.
// See plugins/shared-daf/DuskAccessBridge.hpp for the weak-linkage contract and
// the mandatory null guard (the split LV2 UI links without the DSP).
//
// Everything the editor needs that is NOT a parameter lives here:
//   * four peak level meters, in dB, matching the JUCE editor's LED meters
//   * the processing load, for the CPU read-out
//   * the complete sound snapshot and its revision, so the preset name in the
//     top bar follows a program change or session restore made by the host
//     rather than by the UI, and the A/B slots that outlive the editor.
//   * output history: peak dB sampled at 15 Hz on the audio thread, independent
//     of host block size and editor frame rate.
//
// Parameter values are NOT here: the UI reads and writes those through DAF's
// own parameter API (editParameter/setParameterValue), as every other Dusk DAF
// plugin does.
//
// NOTE for the UI: unlike the other Dusk DAF plugins there is NO output-parameter
// fallback for the meters (DuskVerbParams.hpp explains why they were removed).
// The plugin ships MONOLITHIC, so these weak symbols always resolve in every
// format we build -- but still null-check them, because a null bridge must draw
// idle meters rather than crash.

#pragma once

#include "DuskAccessBridge.hpp"
#include "DuskVerbParams.hpp"

namespace duskverb
{
// A/B survives editor recreation for this plugin instance. The active complete
// sound and displayed identity additionally travel in host session state.
struct EditorState
{
    StateValues slots[2];
    bool valid[2] = {false, false};
    int activeSlot = 0;
};
}

DUSK_WEAK bool duskVerbReadSnapshot(void*, duskverb::StateValues&);
DUSK_WEAK duskverb::EditorState* duskVerbEditorState(void*) noexcept;
DUSK_WEAK uint64_t duskVerbStateRevision(void*) noexcept;

DUSK_ACCESS_DECL(float, duskVerbGetInputLevelL);
DUSK_ACCESS_DECL(float, duskVerbGetInputLevelR);
DUSK_ACCESS_DECL(float, duskVerbGetOutputLevelL);
DUSK_ACCESS_DECL(float, duskVerbGetOutputLevelR);

// Processing load as a FRACTION of real time: the wall-clock duration of run()
// divided by the duration of the block it produced, smoothed with a slow
// one-pole. 0.041 means the editor prints "CPU 4.1%". It is what this plugin
// instance costs, not what the host's whole graph costs, and it is a rough
// guide rather than a measurement -- a block boundary that lands on a
// scheduling hiccup inflates one sample of it, which is exactly what the
// smoothing is for. Returns 0 when the bridge is unavailable.
DUSK_ACCESS_DECL(float, duskVerbGetCpuLoad);

// Copies up to `maxCount` frames of output-peak dB into `dest`, oldest first,
// and returns how many were written (0 when the bridge is unavailable). The
// ring holds duskverb::DuskVerbDSP::kTailHistorySize (256) frames.
DUSK_WEAK int duskVerbGetTailHistory(void* pluginInstancePointer,
                                     float* dest, int maxCount) noexcept;
