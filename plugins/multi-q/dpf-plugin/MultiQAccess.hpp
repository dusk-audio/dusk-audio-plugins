// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQAccess.hpp — UI-side same-process DSP accessor bridge for Multi-Q 2.
// The MultiQDSP core exposes in/out peak meters and a post-processing analyzer
// ring; the UI reads them straight off the live DSP instance through these
// weak-symbol accessors. See DuskAccessBridge.hpp for the single-binary-vs-split-
// LV2 weak-symbol contract and the required UI-side null guard. Strong
// definitions live in MultiQPlugin.cpp. Mirrors FourKEQAccess.hpp.

#pragma once

#include "DuskAccessBridge.hpp"

namespace duskaudio { class SpectrumRing; }

// Linear peak levels (0..~2), ~300 ms release. Pre-processing input + post-
// processing output, per channel.
DUSK_ACCESS_DECL(float, multiQGetInputPeakL);
DUSK_ACCESS_DECL(float, multiQGetInputPeakR);
DUSK_ACCESS_DECL(float, multiQGetOutputPeakL);
DUSK_ACCESS_DECL(float, multiQGetOutputPeakR);

// Pointer to the DSP's lock-free post-processing analyzer ring (null out-of-process).
DUSK_ACCESS_DECL(const duskaudio::SpectrumRing*, multiQGetOutputSpectrum);

// Live per-band dynamic-EQ gain (dB) for the UI's animated response overlay.
// Declared by hand (the DUSK_ACCESS_DECL macro carries only the void* instance
// pointer; this one needs the extra band index). Read-only meter tap.
DUSK_WEAK float multiQGetBandDynGain(void* pluginInstancePointer, int band) noexcept;
