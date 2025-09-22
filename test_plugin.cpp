#include <iostream>
#include <cmath>
#include <JuceHeader.h>

int main() {
    std::cout << "Testing Studio480 Plugin Parameter System..." << std::endl;

    // Initialize JUCE
    juce::ScopedJuceInitialiser_GUI init;

    // Load the plugin
    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    juce::String errorMessage;
    auto plugin = formatManager.createPluginInstance(
        juce::PluginDescription{
            .name = "Studio 480",
            .pluginFormatName = "VST3",
            .fileOrIdentifier = "/home/marc/.vst3/Studio 480.vst3",
            .uniqueId = 0
        },
        44100.0,
        512,
        errorMessage
    );

    if (!plugin) {
        std::cout << "Failed to load plugin: " << errorMessage << std::endl;
        return 1;
    }

    std::cout << "Plugin loaded successfully!" << std::endl;

    // Check parameters
    auto& params = plugin->getParameters();
    std::cout << "Number of parameters: " << params.size() << std::endl;

    for (int i = 0; i < params.size(); ++i) {
        auto* param = params[i];
        std::cout << "Param " << i << ": "
                  << param->getName(100) << " = "
                  << param->getValue() << std::endl;
    }

    return 0;
}