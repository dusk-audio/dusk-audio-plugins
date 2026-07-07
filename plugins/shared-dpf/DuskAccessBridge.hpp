// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DuskAccessBridge.hpp — the same-process DSP-accessor pattern shared by every
// Dusk DPF plugin.
//
// Why this exists
// ---------------
// CLAP never forwards output parameters to the UI and VST3's forwarding is
// laggy, so meters/spectra/scopes are read straight off the DSP instance's
// lock-free atomics on the UI thread. The UI reaches the DSP through a free
// function that takes the DPF getPluginInstancePointer() and returns the live
// value. That function is declared **weak** here and defined **strong** in the
// plugin's DSP translation unit (<Name>Plugin.cpp):
//
//   * single-binary formats (CLAP, VST3, JACK standalone, and MONOLITHIC LV2)
//     link the DSP and UI together, so the strong definition resolves the weak
//     reference everywhere — the UI gets direct access.
//   * the split LV2 UI links as a separate .so WITHOUT the DSP; the weak
//     reference stays null, and the UI must fall back to the output parameter
//     (LV2 forwards those via port events). Always guard the call:
//
//         float v = fallbackFromOutputParam;
//        #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
//         if (myAccessor != nullptr)                    // weak: null in split UI
//             if (void* const inst = getPluginInstancePointer())
//                 v = myAccessor(inst);
//        #endif
//
// Keep the output parameter too, as the generic-UI / out-of-process fallback.
//
// Usage
// -----
//   // In <Name>Access.hpp (UI + DSP both include it):
//   #include "DuskAccessBridge.hpp"
//   DUSK_ACCESS_DECL(float, myGetOutputLevel);
//   // (forward-declare any DSP types the signature needs, then more DECLs)
//
//   // In <Name>Plugin.cpp — the STRONG definition (no DUSK_WEAK):
//   float myGetOutputLevel(void* p) noexcept
//   { return static_cast<MyPlugin*>(p)->dsp.getOutputLevel(); }

#pragma once

// Weak linkage: a null-resolving reference when the strong definition is absent
// (the split LV2 UI). MSVC has no portable equivalent, so direct access there
// would need /alternatename; the Dusk plugins target GCC/Clang.
#if defined(__GNUC__) || defined(__clang__)
  #define DUSK_WEAK __attribute__((weak))
#else
  #define DUSK_WEAK
#endif

// Declare a weak UI-side accessor:  <ret> name(void* pluginInstancePointer).
// The caller appends the semicolon (and provides any needed forward decls).
#define DUSK_ACCESS_DECL(ret, name) \
    DUSK_WEAK ret name(void* pluginInstancePointer) noexcept
