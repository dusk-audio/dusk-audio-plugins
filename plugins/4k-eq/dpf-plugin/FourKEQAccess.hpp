// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQAccess.hpp — UI-side accessors for same-process DSP data (meters +
// spectrum). Declared weak so the split LV2 UI (which links without the DSP)
// resolves them to null and falls back to the output parameters; in the
// single-binary formats (CLAP, VST3, JACK, MONOLITHIC LV2) the strong
// definitions in FourKEQPlugin.cpp resolve everywhere.

#pragma once

namespace duskaudio { class SpectrumRing; }

#if defined(__GNUC__) || defined(__clang__)
  #define DUSK_WEAK __attribute__((weak))
#else
  #define DUSK_WEAK
#endif

// Linear peak levels (0..~2), ~300 ms release.
DUSK_WEAK float fourKEQGetInputPeakL(void* pluginInstancePointer) noexcept;
DUSK_WEAK float fourKEQGetInputPeakR(void* pluginInstancePointer) noexcept;
DUSK_WEAK float fourKEQGetOutputPeakL(void* pluginInstancePointer) noexcept;
DUSK_WEAK float fourKEQGetOutputPeakR(void* pluginInstancePointer) noexcept;

// Pointers to the DSP's lock-free spectrum rings (null when out-of-process).
DUSK_WEAK const duskaudio::SpectrumRing* fourKEQGetPreSpectrum(void* pluginInstancePointer) noexcept;
DUSK_WEAK const duskaudio::SpectrumRing* fourKEQGetPostSpectrum(void* pluginInstancePointer) noexcept;
