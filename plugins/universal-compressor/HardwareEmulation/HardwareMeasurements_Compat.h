/*
  ==============================================================================

    HardwareMeasurements_Compat.h
    Compatibility header - redirects to shared AnalogEmulation library

    This file provides backward compatibility for existing code.
    New code should include the shared library directly:
        #include "../../shared/AnalogEmulation/AnalogEmulation.h"

  ==============================================================================
*/

#pragma once

// Include the shared library
#include "../../shared/AnalogEmulation/HardwareProfiles.h"

// Provide backward-compatible namespace alias
namespace HardwareEmulation {
    using namespace AnalogEmulation;

    // Backward-compatible accessor class
    using HardwareProfiles = AnalogEmulation::HardwareProfileLibrary;
}
