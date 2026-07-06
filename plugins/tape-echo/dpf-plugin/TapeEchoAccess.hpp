// TapeEchoAccess.hpp — UI-side accessor for same-process DSP data.
//
// Declared weak: in single-binary formats (CLAP, VST3, JACK standalone) the
// strong definition in TapeEchoPlugin.cpp resolves it; in the LV2 UI, which
// links as a separate .so without the DSP, it resolves to null and the UI
// falls back to the output parameter (LV2 forwards those via port events).

#pragma once

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
float tapeEchoGetOutputLevel(void* pluginInstancePointer) noexcept;
