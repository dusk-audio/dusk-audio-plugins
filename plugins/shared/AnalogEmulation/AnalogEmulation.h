// AnalogEmulation.h — Umbrella header for shared analog emulation library

#pragma once

#include "DCBlocker.h"
#include "HighFrequencyEstimator.h"
#include "WaveshaperCurves.h"
#include "HardwareProfiles.h"
#include "TubeEmulation.h"
#include "TransformerEmulation.h"

namespace AnalogEmulation {

inline const char* getLibraryVersion() { return "1.0.0"; }

// Call once during plugin init to build lookup tables off the RT thread
inline void initializeLibrary()
{
    (void)getWaveshaperCurves();
}

} // namespace AnalogEmulation
