// Force factory registration by explicitly including and referencing the
// model implementation files. This ensures the static Helper objects that
// register the factories are not optimized out by the linker.

#include "factory_init.h"
#include "registry.h"

// These includes pull in the translation units that have static factory
// registration. By referencing the Factory functions below, we ensure the
// linker keeps the object files that contain the static Helper registrations.
#include "wavenet.h"
#include "lstm.h"
#include "convnet.h"

// Forward declarations of factory functions from each model type
namespace nam
{
namespace wavenet
{
std::unique_ptr<DSP> Factory(const nlohmann::json&, std::vector<float>&, const double);
}
namespace lstm
{
std::unique_ptr<DSP> Factory(const nlohmann::json&, std::vector<float>&, const double);
}
namespace convnet
{
std::unique_ptr<DSP> Factory(const nlohmann::json&, std::vector<float>&, const double);
}
}

namespace nam
{

// This function forces references to the factory functions, which in turn
// forces the linker to include the translation units containing the static
// Helper objects that register the factories.
void initializeFactories()
{
    // Create volatile function pointers to prevent the compiler from
    // optimizing away these references
    volatile auto wavenetFactory = &nam::wavenet::Factory;
    volatile auto lstmFactory = &nam::lstm::Factory;
    volatile auto convnetFactory = &nam::convnet::Factory;

    // Suppress unused variable warnings
    (void)wavenetFactory;
    (void)lstmFactory;
    (void)convnetFactory;
}

}
