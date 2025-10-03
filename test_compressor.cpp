#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

int main()
{
    juce::MessageManager::getInstance();

    juce::OwnedArray<juce::PluginDescription> types;
    juce::VST3PluginFormat format;

    juce::String pluginPath = "/home/marc/.vst3/Universal Compressor.vst3";

    format.findAllTypesForFile(types, pluginPath);

    if (types.isEmpty())
    {
        std::cerr << "Failed to find plugin at: " << pluginPath << std::endl;
        return 1;
    }

    std::cout << "Found " << types.size() << " plugin(s)" << std::endl;

    juce::String error;
    auto instance = format.createInstanceFromDescription(*types[0], 44100.0, 512, error);

    if (instance == nullptr)
    {
        std::cerr << "Failed to create plugin instance: " << error << std::endl;
        return 1;
    }

    std::cout << "Plugin loaded successfully!" << std::endl;
    std::cout << "Name: " << instance->getName() << std::endl;
    std::cout << "Preparing to play..." << std::endl;

    instance->prepareToPlay(44100.0, 512);

    std::cout << "Success! Plugin is working." << std::endl;

    return 0;
}
