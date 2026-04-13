#pragma once

#include "PresetEngineBase.h"

#include <memory>
#include <string>
#include <unordered_map>

// Registry of per-preset custom reverb engines.
//
// Each preset's .cpp file self-registers at static initialization time via a
// PresetEngineRegistrar instance at file scope. DuskVerbEngine queries the
// registry in applyAlgorithm(): if the current algorithm name has a registered
// factory, the per-preset engine is instantiated and routed instead of the
// shared engine.

class PresetEngineRegistry
{
public:
    using FactoryFn = std::unique_ptr<PresetEngineBase> (*)();

    // Singleton access
    static PresetEngineRegistry& instance();

    // Register a preset engine factory by its algorithm name (e.g. "PresetTiledRoom").
    // Called automatically at static init by PresetEngineRegistrar instances.
    void registerEngine (const std::string& algorithmName, FactoryFn factory);

    // Look up and construct a preset engine by algorithm name.
    // Returns nullptr if no preset engine is registered.
    // NOTE: allocates — do NOT call from the audio thread.
    std::unique_ptr<PresetEngineBase> create (const std::string& algorithmName) const;

    // True if a per-preset engine exists for this algorithm name.
    bool has (const std::string& algorithmName) const;

    // Number of registered preset engines (for debugging).
    size_t count() const;

    // Construct an instance of every registered preset engine and return
    // ownership in a name-keyed map. Intended to be called once from
    // prepareToPlay() so that the audio thread can later look up an
    // already-constructed engine without allocating. The caller is
    // responsible for invoking prepare() on each instance.
    std::unordered_map<std::string, std::unique_ptr<PresetEngineBase>> instantiateAll() const;

    // Iterate over all registered factory names (read-only access for callers
    // that need to know what's available without owning the instances).
    const std::unordered_map<std::string, FactoryFn>& factories() const { return factories_; }

private:
    PresetEngineRegistry() = default;
    std::unordered_map<std::string, FactoryFn> factories_;
};

// RAII helper: an instance of this type at file scope in a preset's .cpp file
// calls registerEngine() at static initialization time.
struct PresetEngineRegistrar
{
    PresetEngineRegistrar (const char* algorithmName, PresetEngineRegistry::FactoryFn factory)
    {
        PresetEngineRegistry::instance().registerEngine (algorithmName, factory);
    }
};
