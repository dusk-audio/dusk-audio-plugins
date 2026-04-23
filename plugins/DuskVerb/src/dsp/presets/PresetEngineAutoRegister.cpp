// LINKER FIX: each per-preset .cpp file uses a static-initializer
// PresetEngineRegistrar to register itself with PresetEngineRegistry.
// When the .cpp files are compiled into a static library and nothing
// in the main binary references their symbols, the linker drops the
// .o files entirely and the registrars never run. This stub forces a
// link-time reference to every preset's factory function, keeping all
// .o files in the final binary so their static initializers fire.
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
std::unique_ptr<PresetEngineBase> createGatedPreset();
std::unique_ptr<PresetEngineBase> createCathedralPreset();
std::unique_ptr<PresetEngineBase> createLargeHallPreset();
std::unique_ptr<PresetEngineBase> createMediumHallPreset();
std::unique_ptr<PresetEngineBase> createVocalHallPreset();
std::unique_ptr<PresetEngineBase> createDrumPlatePreset();
std::unique_ptr<PresetEngineBase> createRichPlatePreset();
std::unique_ptr<PresetEngineBase> createVocalPlatePreset();
std::unique_ptr<PresetEngineBase> createDarkChamberPreset();
std::unique_ptr<PresetEngineBase> createLiveRoomPreset();
std::unique_ptr<PresetEngineBase> createStudioRoomPreset();
std::unique_ptr<PresetEngineBase> createBrightChamberPreset();
std::unique_ptr<PresetEngineBase> createDarkPlatePreset();
std::unique_ptr<PresetEngineBase> createLiveChamberPreset();
std::unique_ptr<PresetEngineBase> createReversePreset();
std::unique_ptr<PresetEngineBase> createSmallHallPreset();
std::unique_ptr<PresetEngineBase> createDrumChamberPreset();
std::unique_ptr<PresetEngineBase> createBrightPlatePreset();
std::unique_ptr<PresetEngineBase> createModulatedPreset();
std::unique_ptr<PresetEngineBase> createShimmerPreset();
std::unique_ptr<PresetEngineBase> createInfinitePreset();
std::unique_ptr<PresetEngineBase> createVocalChamberPreset();
std::unique_ptr<PresetEngineBase> createDrumRoomPreset();
std::unique_ptr<PresetEngineBase> createTightRoomPreset();
std::unique_ptr<PresetEngineBase> createVocalBoothPreset();

// Direct registration: bypass the static-init PresetEngineRegistrar
// approach (which is unreliable when the preset .cpp files are
// compiled into a static library — the linker drops them and the
// static initializers never run). DuskVerbEngine::prepare() calls
// forceLinkPresetEngines() once at plugin load, and we explicitly
// register every factory here. This guarantees the registry is
// populated regardless of static-init order or LTO behavior.
void forceLinkPresetEngines()
{
    static std::once_flag once;
    std::call_once (once, [] {
        auto& reg = PresetEngineRegistry::instance();
        reg.registerEngine ("PresetGated", &createGatedPreset);
        reg.registerEngine ("PresetCathedral", &createCathedralPreset);
        reg.registerEngine ("PresetLargeHall", &createLargeHallPreset);
        reg.registerEngine ("PresetMediumHall", &createMediumHallPreset);
        reg.registerEngine ("PresetVocalHall", &createVocalHallPreset);
        reg.registerEngine ("PresetDrumPlate", &createDrumPlatePreset);
        reg.registerEngine ("PresetRichPlate", &createRichPlatePreset);
        reg.registerEngine ("PresetVocalPlate", &createVocalPlatePreset);
        reg.registerEngine ("PresetDarkChamber", &createDarkChamberPreset);
        reg.registerEngine ("PresetLiveRoom", &createLiveRoomPreset);
        reg.registerEngine ("PresetStudioRoom", &createStudioRoomPreset);
        reg.registerEngine ("PresetBrightChamber", &createBrightChamberPreset);
        reg.registerEngine ("PresetDarkPlate", &createDarkPlatePreset);
        reg.registerEngine ("PresetLiveChamber", &createLiveChamberPreset);
        reg.registerEngine ("PresetReverse", &createReversePreset);
        reg.registerEngine ("PresetSmallHall", &createSmallHallPreset);
        reg.registerEngine ("PresetDrumChamber", &createDrumChamberPreset);
        reg.registerEngine ("PresetBrightPlate", &createBrightPlatePreset);
        reg.registerEngine ("PresetModulated", &createModulatedPreset);
        reg.registerEngine ("PresetShimmer", &createShimmerPreset);
        reg.registerEngine ("PresetInfinite", &createInfinitePreset);
        reg.registerEngine ("PresetVocalChamber", &createVocalChamberPreset);
        reg.registerEngine ("PresetDrumRoom", &createDrumRoomPreset);
        reg.registerEngine ("PresetTightRoom", &createTightRoomPreset);
        reg.registerEngine ("PresetVocalBooth", &createVocalBoothPreset);
    });
}
