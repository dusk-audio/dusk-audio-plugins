// GENERATED FILE - do not edit by hand (generate_preset_engines.py)
//
// LINKER FIX: each per-preset .cpp file uses a static-initializer
// PresetEngineRegistrar to register itself with PresetEngineRegistry.
// When the .cpp files are compiled into a static library and nothing
// in the main binary references their symbols, the linker drops the
// .o files entirely and the registrars never run. This stub forces a
// link-time reference to every preset's factory function, keeping all
// 53 .o files in the final binary so their static initializers fire.
//
// DuskVerbEngine::prepare() calls forceLinkPresetEngines() to ensure
// the references are preserved across LTO / dead-strip passes.

#include "PresetEngineBase.h"
#include "PresetEngineRegistry.h"
#include <memory>
#include <mutex>

// Forward declarations for every per-preset factory function. The
// definitions live in <PresetName>Preset.cpp inside each anonymous
// namespace; we expose only the C++ free function to the linker.
std::unique_ptr<PresetEngineBase> createHomestarBladeRunnerPreset();
std::unique_ptr<PresetEngineBase> createPadHallPreset();
std::unique_ptr<PresetEngineBase> createConcertWavePreset();
std::unique_ptr<PresetEngineBase> createHugeSynthHallPreset();
std::unique_ptr<PresetEngineBase> createSmallVocalHallPreset();
std::unique_ptr<PresetEngineBase> createFatSnareHallPreset();
std::unique_ptr<PresetEngineBase> createSnareHallPreset();
std::unique_ptr<PresetEngineBase> createLongSynthHallPreset();
std::unique_ptr<PresetEngineBase> createVeryNiceHallPreset();
std::unique_ptr<PresetEngineBase> createVocalHallPreset();
std::unique_ptr<PresetEngineBase> createDrumPlatePreset();
std::unique_ptr<PresetEngineBase> createFatDrumsPreset();
std::unique_ptr<PresetEngineBase> createLargePlatePreset();
std::unique_ptr<PresetEngineBase> createSteelPlatePreset();
std::unique_ptr<PresetEngineBase> createTightPlatePreset();
std::unique_ptr<PresetEngineBase> createVocalPlatePreset();
std::unique_ptr<PresetEngineBase> createVoxPlatePreset();
std::unique_ptr<PresetEngineBase> createDarkVocalRoomPreset();
std::unique_ptr<PresetEngineBase> createExcitingSnareRoomPreset();
std::unique_ptr<PresetEngineBase> createFatSnareRoomPreset();
std::unique_ptr<PresetEngineBase> createLivelySnareRoomPreset();
std::unique_ptr<PresetEngineBase> createLongDark70sSnareRoomPreset();
std::unique_ptr<PresetEngineBase> createShortDarkSnareRoomPreset();
std::unique_ptr<PresetEngineBase> createAPlatePreset();
std::unique_ptr<PresetEngineBase> createClearChamberPreset();
std::unique_ptr<PresetEngineBase> createFatPlatePreset();
std::unique_ptr<PresetEngineBase> createLargeChamberPreset();
std::unique_ptr<PresetEngineBase> createLargeWoodRoomPreset();
std::unique_ptr<PresetEngineBase> createLiveVoxChamberPreset();
std::unique_ptr<PresetEngineBase> createMediumGatePreset();
std::unique_ptr<PresetEngineBase> createRichChamberPreset();
std::unique_ptr<PresetEngineBase> createSmallChamber1Preset();
std::unique_ptr<PresetEngineBase> createSmallChamber2Preset();
std::unique_ptr<PresetEngineBase> createSnarePlatePreset();
std::unique_ptr<PresetEngineBase> createThinPlatePreset();
std::unique_ptr<PresetEngineBase> createTiledRoomPreset();
std::unique_ptr<PresetEngineBase> createAmbiencePreset();
std::unique_ptr<PresetEngineBase> createAmbiencePlatePreset();
std::unique_ptr<PresetEngineBase> createAmbienceTiledRoomPreset();
std::unique_ptr<PresetEngineBase> createBigAmbienceGatePreset();
std::unique_ptr<PresetEngineBase> createCrossStickRoomPreset();
std::unique_ptr<PresetEngineBase> createDrumAirPreset();
std::unique_ptr<PresetEngineBase> createGatedSnarePreset();
std::unique_ptr<PresetEngineBase> createLargeAmbiencePreset();
std::unique_ptr<PresetEngineBase> createLargeGatedSnarePreset();
std::unique_ptr<PresetEngineBase> createMedAmbiencePreset();
std::unique_ptr<PresetEngineBase> createShortVocalAmbiencePreset();
std::unique_ptr<PresetEngineBase> createSmallAmbiencePreset();
std::unique_ptr<PresetEngineBase> createSmallDrumRoomPreset();
std::unique_ptr<PresetEngineBase> createSnareAmbiencePreset();
std::unique_ptr<PresetEngineBase> createTightAmbienceGatePreset();
std::unique_ptr<PresetEngineBase> createTripHopSnarePreset();
std::unique_ptr<PresetEngineBase> createVerySmallAmbiencePreset();

// Direct registration: bypass the static-init PresetEngineRegistrar
// approach (which is unreliable when the preset .cpp files are
// compiled into a static library — the linker drops them and the
// static initializers never run). DuskVerbEngine::prepare() calls
// forceLinkPresetEngines() once at plugin load, and we explicitly
// register every factory here. This guarantees the registry is
// populated regardless of static-init order or LTO behavior.
void forceLinkPresetEngines()
{
    static std::once_flag flag;
    std::call_once (flag, []()
    {
    auto& reg = PresetEngineRegistry::instance();
    reg.registerEngine ("PresetHomestarBladeRunner", &createHomestarBladeRunnerPreset);
    reg.registerEngine ("PresetPadHall", &createPadHallPreset);
    reg.registerEngine ("PresetConcertWave", &createConcertWavePreset);
    reg.registerEngine ("PresetHugeSynthHall", &createHugeSynthHallPreset);
    reg.registerEngine ("PresetSmallVocalHall", &createSmallVocalHallPreset);
    reg.registerEngine ("PresetFatSnareHall", &createFatSnareHallPreset);
    reg.registerEngine ("PresetSnareHall", &createSnareHallPreset);
    reg.registerEngine ("PresetLongSynthHall", &createLongSynthHallPreset);
    reg.registerEngine ("PresetVeryNiceHall", &createVeryNiceHallPreset);
    reg.registerEngine ("PresetVocalHall", &createVocalHallPreset);
    reg.registerEngine ("PresetDrumPlate", &createDrumPlatePreset);
    reg.registerEngine ("PresetFatDrums", &createFatDrumsPreset);
    reg.registerEngine ("PresetLargePlate", &createLargePlatePreset);
    reg.registerEngine ("PresetSteelPlate", &createSteelPlatePreset);
    reg.registerEngine ("PresetTightPlate", &createTightPlatePreset);
    reg.registerEngine ("PresetVocalPlate", &createVocalPlatePreset);
    reg.registerEngine ("PresetVoxPlate", &createVoxPlatePreset);
    reg.registerEngine ("PresetDarkVocalRoom", &createDarkVocalRoomPreset);
    reg.registerEngine ("PresetExcitingSnareRoom", &createExcitingSnareRoomPreset);
    reg.registerEngine ("PresetFatSnareRoom", &createFatSnareRoomPreset);
    reg.registerEngine ("PresetLivelySnareRoom", &createLivelySnareRoomPreset);
    reg.registerEngine ("PresetLongDark70sSnareRoom", &createLongDark70sSnareRoomPreset);
    reg.registerEngine ("PresetShortDarkSnareRoom", &createShortDarkSnareRoomPreset);
    reg.registerEngine ("PresetAPlate", &createAPlatePreset);
    reg.registerEngine ("PresetClearChamber", &createClearChamberPreset);
    reg.registerEngine ("PresetFatPlate", &createFatPlatePreset);
    reg.registerEngine ("PresetLargeChamber", &createLargeChamberPreset);
    reg.registerEngine ("PresetLargeWoodRoom", &createLargeWoodRoomPreset);
    reg.registerEngine ("PresetLiveVoxChamber", &createLiveVoxChamberPreset);
    reg.registerEngine ("PresetMediumGate", &createMediumGatePreset);
    reg.registerEngine ("PresetRichChamber", &createRichChamberPreset);
    reg.registerEngine ("PresetSmallChamber1", &createSmallChamber1Preset);
    reg.registerEngine ("PresetSmallChamber2", &createSmallChamber2Preset);
    reg.registerEngine ("PresetSnarePlate", &createSnarePlatePreset);
    reg.registerEngine ("PresetThinPlate", &createThinPlatePreset);
    reg.registerEngine ("PresetTiledRoom", &createTiledRoomPreset);
    reg.registerEngine ("PresetAmbience", &createAmbiencePreset);
    reg.registerEngine ("PresetAmbiencePlate", &createAmbiencePlatePreset);
    reg.registerEngine ("PresetAmbienceTiledRoom", &createAmbienceTiledRoomPreset);
    reg.registerEngine ("PresetBigAmbienceGate", &createBigAmbienceGatePreset);
    reg.registerEngine ("PresetCrossStickRoom", &createCrossStickRoomPreset);
    reg.registerEngine ("PresetDrumAir", &createDrumAirPreset);
    reg.registerEngine ("PresetGatedSnare", &createGatedSnarePreset);
    reg.registerEngine ("PresetLargeAmbience", &createLargeAmbiencePreset);
    reg.registerEngine ("PresetLargeGatedSnare", &createLargeGatedSnarePreset);
    reg.registerEngine ("PresetMedAmbience", &createMedAmbiencePreset);
    reg.registerEngine ("PresetShortVocalAmbience", &createShortVocalAmbiencePreset);
    reg.registerEngine ("PresetSmallAmbience", &createSmallAmbiencePreset);
    reg.registerEngine ("PresetSmallDrumRoom", &createSmallDrumRoomPreset);
    reg.registerEngine ("PresetSnareAmbience", &createSnareAmbiencePreset);
    reg.registerEngine ("PresetTightAmbienceGate", &createTightAmbienceGatePreset);
    reg.registerEngine ("PresetTripHopSnare", &createTripHopSnarePreset);
    reg.registerEngine ("PresetVerySmallAmbience", &createVerySmallAmbiencePreset);
    });
}
