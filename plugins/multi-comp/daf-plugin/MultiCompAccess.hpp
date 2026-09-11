#pragma once
#include "DuskAccessBridge.hpp"
DUSK_ACCESS_DECL(float, multiCompGetGainReduction);
DUSK_WEAK float multiCompGetBandGainReduction(void* pluginInstancePointer, int band) noexcept;
DUSK_ACCESS_DECL(float, multiCompGetInputLevel);
DUSK_ACCESS_DECL(float, multiCompGetOutputLevel);
DUSK_ACCESS_DECL(float, multiCompGetOutputVuLevel);
DUSK_ACCESS_DECL(float, multiCompGetBusFadePosition);
DUSK_ACCESS_DECL(float, multiCompGetBusMeterReading);
