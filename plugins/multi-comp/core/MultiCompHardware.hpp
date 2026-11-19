// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Core-owned hardware stage boundary. The emulation headers are framework-free
// and are reused unchanged because these are the exact stages called by the
// JUCE Opto, FET, Bus, Studio FET, and Studio VCA paths.
#pragma once

#include "../HardwareEmulation/ConvolutionEngine.h"
#include "../HardwareEmulation/HardwareMeasurements.h"
#include "../HardwareEmulation/TransformerEmulation.h"
#include "../HardwareEmulation/TubeEmulation.h"
