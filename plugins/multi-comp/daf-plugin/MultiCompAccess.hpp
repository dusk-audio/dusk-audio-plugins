#pragma once
#include "DuskAccessBridge.hpp"
#include <cstdint>
DUSK_ACCESS_DECL(float, multiCompGetGainReduction);
DUSK_WEAK float multiCompGetBandGainReduction(void* pluginInstancePointer, int band) noexcept;
DUSK_ACCESS_DECL(float, multiCompGetInputLevel);
DUSK_ACCESS_DECL(float, multiCompGetOutputLevel);
DUSK_ACCESS_DECL(float, multiCompGetOutputVuLevel);
DUSK_ACCESS_DECL(float, multiCompGetExternalSidechainActive);
DUSK_ACCESS_DECL(float, multiCompGetGainReductionNeedle);
DUSK_ACCESS_DECL(float, multiCompGetVcaGainReductionNeedle);
DUSK_ACCESS_DECL(float, multiCompGetStudioVcaInputLevel);
DUSK_ACCESS_DECL(float, multiCompGetStudioVcaOutputLevel);
DUSK_WEAK float multiCompGetInputChannelLevel(void* pluginInstancePointer, int channel) noexcept;
DUSK_WEAK float multiCompGetOutputChannelLevel(void* pluginInstancePointer, int channel) noexcept;
DUSK_ACCESS_DECL(float, multiCompGetBusFadePosition);
DUSK_ACCESS_DECL(float, multiCompGetBusMeterReading);

// For a host that builds the plug-in and its editor into its own binary and holds the instance itself.
//
// The faces the editor's mode row offers: bit n of the mask allows mode n (duskaudio::MultiCompMode), and a mask with
// none of the eight set allows them all, the default. With one mode allowed the row draws no tabs. The editor alone
// reads it: the Mode parameter, the state and the DSP take every mode whatever the mask.
DUSK_WEAK void multiCompSetHostFaces(void* pluginInstancePointer, uint32_t modeMask) noexcept;
DUSK_ACCESS_DECL(uint32_t, multiCompGetHostFaces);
// The main layout, 1 or 2 channels (anything else is 2) and no sidechain: what ioChanged(channels, channels) does
// for a format wrapper. run() then reads inputs[0 .. channels - 1] and writes outputs[0 .. channels - 1] only.
// Call it only while the plug-in is deactivated.
DUSK_WEAK void multiCompSetHostChannels(void* pluginInstancePointer, int channels) noexcept;
