#include "PresetEngineRegistry.h"

PresetEngineRegistry& PresetEngineRegistry::instance()
{
    static PresetEngineRegistry inst;
    return inst;
}

void PresetEngineRegistry::registerEngine (const std::string& algorithmName, FactoryFn factory)
{
    if (!factory)
        return;
    // Idempotent: skip if already registered (prevents double-registration
    // from PresetEngineRegistrar static initializers + forceLinkPresetEngines).
    if (factories_.find (algorithmName) != factories_.end())
        return;
    factories_[algorithmName] = factory;
}

std::unique_ptr<PresetEngineBase> PresetEngineRegistry::create (const std::string& algorithmName) const
{
    auto it = factories_.find (algorithmName);
    if (it == factories_.end())
        return nullptr;
    return it->second();
}

bool PresetEngineRegistry::has (const std::string& algorithmName) const
{
    return factories_.find (algorithmName) != factories_.end();
}

size_t PresetEngineRegistry::count() const
{
    return factories_.size();
}

std::unordered_map<std::string, std::unique_ptr<PresetEngineBase>>
PresetEngineRegistry::instantiateAll() const
{
    std::unordered_map<std::string, std::unique_ptr<PresetEngineBase>> out;
    out.reserve (factories_.size());
    for (const auto& kv : factories_)
    {
        if (auto e = kv.second())
            out.emplace (kv.first, std::move (e));
    }
    return out;
}
