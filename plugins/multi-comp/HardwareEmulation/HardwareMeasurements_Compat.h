// HardwareMeasurements_Compat.h — Redirects to shared AnalogEmulation library

#pragma once

#include "../../shared/AnalogEmulation/HardwareProfiles.h"

namespace HardwareEmulation {
    using namespace AnalogEmulation;
    using HardwareProfiles = AnalogEmulation::HardwareProfileLibrary;
}
