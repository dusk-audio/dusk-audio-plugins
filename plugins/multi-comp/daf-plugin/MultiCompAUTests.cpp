#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "MultiCompParams.hpp"

namespace
{
void require(bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void requireStatus(OSStatus status, const char* message)
{
    if (status != noErr)
    {
        std::fprintf(stderr, "FAIL: %s (OSStatus %d)\n", message, static_cast<int>(status));
        std::exit(1);
    }
}

constexpr OSType fourcc(char a, char b, char c, char d)
{
    return (static_cast<OSType>(a) << 24u) | (static_cast<OSType>(b) << 16u)
         | (static_cast<OSType>(c) << 8u) | static_cast<OSType>(d);
}

struct CallbackState
{
    UInt32 expectedBus = 0;
    UInt32 calls = 0;
};

OSStatus renderInput(void* refCon,
                     AudioUnitRenderActionFlags*,
                     const AudioTimeStamp*,
                     UInt32 bus,
                     UInt32 frames,
                     AudioBufferList* data)
{
    CallbackState& state = *static_cast<CallbackState*>(refCon);
    require(bus == state.expectedBus, "input callback receives its destination bus number");
    ++state.calls;

    for (UInt32 channel = 0; channel < data->mNumberBuffers; ++channel)
    {
        require(data->mBuffers[channel].mData != nullptr,
                "AU supplies allocated storage to each input callback");
        float* const samples = static_cast<float*>(data->mBuffers[channel].mData);
        for (UInt32 frame = 0; frame < frames; ++frame)
            samples[frame] = state.expectedBus == 0 ? 0.1f : 0.8f;
    }
    return noErr;
}

struct StereoBufferList
{
    UInt32 numberBuffers;
    AudioBuffer buffers[2];
};
}

int main()
{
    void* const image = dlopen(MULTICOMP_AU_BINARY, RTLD_NOW | RTLD_LOCAL);
    require(image != nullptr, dlerror());

    using Factory = void* (*)(const AudioComponentDescription*);
    Factory const factory = reinterpret_cast<Factory>(dlsym(image, "PluginAUFactory"));
    require(factory != nullptr, "AU bundle exports PluginAUFactory");

    const AudioComponentDescription description = {
        kAudioUnitType_Effect, fourcc('D', 's', 'M', 'c'), fourcc('D', 'u', 's', 'k'), 0, 0
    };
    auto* const interface = static_cast<AudioComponentPlugInInterface*>(factory(&description));
    require(interface != nullptr, "AU factory creates an interface");
    requireStatus(interface->Open(interface, reinterpret_cast<AudioUnit>(interface)),
                  "AU interface opens");

    using GetProperty = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                     AudioUnitElement, void*, UInt32*);
    using SetProperty = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                     AudioUnitElement, const void*, UInt32);
    using Lifecycle = OSStatus (*)(void*);
    using Render = OSStatus (*)(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                                UInt32, UInt32, AudioBufferList*);
    using Reset = OSStatus (*)(void*, AudioUnitScope, AudioUnitElement);
    using SetParameter = OSStatus (*)(void*, AudioUnitParameterID, AudioUnitScope,
                                      AudioUnitElement, AudioUnitParameterValue, UInt32);

    const auto getProperty = reinterpret_cast<GetProperty>(interface->Lookup(kAudioUnitGetPropertySelect));
    const auto setProperty = reinterpret_cast<SetProperty>(interface->Lookup(kAudioUnitSetPropertySelect));
    const auto initialize = reinterpret_cast<Lifecycle>(interface->Lookup(kAudioUnitInitializeSelect));
    const auto uninitialize = reinterpret_cast<Lifecycle>(interface->Lookup(kAudioUnitUninitializeSelect));
    const auto render = reinterpret_cast<Render>(interface->Lookup(kAudioUnitRenderSelect));
    const auto reset = reinterpret_cast<Reset>(interface->Lookup(kAudioUnitResetSelect));
    const auto setParameter = reinterpret_cast<SetParameter>(interface->Lookup(kAudioUnitSetParameterSelect));
    require(getProperty != nullptr && setProperty != nullptr && initialize != nullptr
                && uninitialize != nullptr && render != nullptr && reset != nullptr
                && setParameter != nullptr,
            "AU interface exposes property, lifecycle, and render selectors");

    UInt32 inputBusCount = 0;
    UInt32 dataSize = sizeof(inputBusCount);
    requireStatus(getProperty(interface, kAudioUnitProperty_ElementCount,
                              kAudioUnitScope_Input, 0, &inputBusCount, &dataSize),
                  "AU reports its input element count");
    require(inputBusCount == 2, "AU exposes main and sidechain as separate input buses");

    for (UInt32 bus = 0; bus < inputBusCount; ++bus)
    {
        AudioStreamBasicDescription format{};
        dataSize = sizeof(format);
        requireStatus(getProperty(interface, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, bus, &format, &dataSize),
                      "each input bus exposes a stream format");
        require(format.mChannelsPerFrame == 2, "main and sidechain buses default to stereo");
    }

    AUChannelInfo channelInfo[3]{};
    dataSize = sizeof(channelInfo);
    requireStatus(getProperty(interface, kAudioUnitProperty_SupportedNumChannels,
                              kAudioUnitScope_Global, 0, channelInfo, &dataSize),
                  "AU reports supported main-bus layouts");
    require(dataSize == 2 * sizeof(AUChannelInfo),
            "duplicate aggregate and main-bus channel layouts are removed");
    require(channelInfo[0].inChannels == 2 && channelInfo[0].outChannels == 2
                && channelInfo[1].inChannels == 1 && channelInfo[1].outChannels == 1,
            "AU channel capabilities describe stereo and mono main buses");

    CallbackState callbackStates[2] = {{0, 0}, {1, 0}};
    for (UInt32 bus = 0; bus < inputBusCount; ++bus)
    {
        const AURenderCallbackStruct callback = {renderInput, &callbackStates[bus]};
        requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, bus, &callback, sizeof(callback)),
                      "each input bus accepts an independent render callback");
    }

    requireStatus(initialize(interface), "AU initializes with both input buses connected");

    constexpr UInt32 frames = 64;
    float left[frames]{};
    float right[frames]{};
    StereoBufferList storage = {
        2,
        {{1, sizeof(left), left}, {1, sizeof(right), right}}
    };
    AudioTimeStamp timestamp{};
    timestamp.mSampleTime = 0.0;
    timestamp.mFlags = kAudioTimeStampSampleTimeValid;
    AudioUnitRenderActionFlags flags = 0;
    requireStatus(render(interface, &flags, &timestamp, 0, frames,
                         reinterpret_cast<AudioBufferList*>(&storage)),
                  "AU renders with independent main and sidechain pulls");
    require(callbackStates[0].calls == 1 && callbackStates[1].calls == 1,
            "one render pulls both input buses exactly once");

    requireStatus(uninitialize(interface), "AU uninitializes");

    const AURenderCallbackStruct disconnected{};
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &disconnected, sizeof(disconnected)),
                  "sidechain callback disconnects independently");
    requireStatus(initialize(interface), "AU initializes without a sidechain source");
    requireStatus(render(interface, &flags, &timestamp, 0, frames,
                         reinterpret_cast<AudioBufferList*>(&storage)),
                  "AU renders after the sidechain disconnects");
    require(callbackStates[0].calls == 2 && callbackStates[1].calls == 1,
            "disconnected sidechain is not pulled and the main bus remains active");
    requireStatus(reset(interface, kAudioUnitScope_Input, 1),
                  "reset accepts the sidechain input element");
    requireStatus(uninitialize(interface), "AU uninitializes after disconnected render");

    AudioStreamBasicDescription monoFormat{};
    dataSize = sizeof(monoFormat);
    requireStatus(getProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &monoFormat, &dataSize),
                  "main input format can be read before switching to mono");
    monoFormat.mChannelsPerFrame = 1;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &monoFormat, sizeof(monoFormat)),
                  "main input bus accepts mono");
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Output, 0, &monoFormat, sizeof(monoFormat)),
                  "output bus accepts matching mono");

    const AURenderCallbackStruct sidechainCallback = {renderInput, &callbackStates[1]};
    require(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                        kAudioUnitScope_Input, 1, &sidechainCallback,
                        sizeof(sidechainCallback)) == kAudioUnitErr_FormatNotSupported,
            "unsupported mono-main plus stereo-sidechain layout is rejected");

    requireStatus(initialize(interface), "AU initializes in its advertised mono layout");
    storage.numberBuffers = 1;
    requireStatus(render(interface, &flags, &timestamp, 0, frames,
                         reinterpret_cast<AudioBufferList*>(&storage)),
                  "AU renders its mono main path");
    require(callbackStates[0].calls == 3 && callbackStates[1].calls == 1,
            "mono render pulls only the one-channel main bus");
    requireStatus(uninitialize(interface), "AU uninitializes after mono render");

    monoFormat.mChannelsPerFrame = 2;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &monoFormat, sizeof(monoFormat)),
                  "main input bus returns to stereo");
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Output, 0, &monoFormat, sizeof(monoFormat)),
                  "output bus returns to stereo");
    requireStatus(setParameter(interface,
                               static_cast<AudioUnitParameterID>(multicompp::ParamId::ExternalSidechain),
                               kAudioUnitScope_Global, 0, 1.0f, 0),
                  "external sidechain parameter enables");
    requireStatus(setParameter(interface,
                               static_cast<AudioUnitParameterID>(multicompp::ParamId::OptoPeakReduction),
                               kAudioUnitScope_Global, 0, 80.0f, 0),
                  "opto peak reduction is set for the routing probe");
    requireStatus(setParameter(interface,
                               static_cast<AudioUnitParameterID>(multicompp::ParamId::NoiseEnable),
                               kAudioUnitScope_Global, 0, 0.0f, 0),
                  "analogue noise is disabled for the routing probe");

    storage.numberBuffers = 2;
    requireStatus(initialize(interface), "AU initializes for disconnected routing probe");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "disconnected routing probe renders");
    const float disconnectedLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after disconnected routing probe");

    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainCallback,
                              sizeof(sidechainCallback)),
                  "stereo sidechain reconnects after restoring the stereo main layout");
    requireStatus(initialize(interface), "AU initializes for connected routing probe");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "connected routing probe renders");
    const float connectedLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after connected routing probe");
    std::printf("AU sidechain routing: disconnected %.6f, connected %.6f\n",
                static_cast<double>(disconnectedLevel), static_cast<double>(connectedLevel));
    require(connectedLevel < disconnectedLevel * 0.8f,
            "sidechain bus samples reach the plugin's external detector ports");

    AudioStreamBasicDescription sidechainFormat{};
    dataSize = sizeof(sidechainFormat);
    requireStatus(getProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 1, &sidechainFormat, &dataSize),
                  "sidechain format can be read before disabling its bus");
    sidechainFormat.mChannelsPerFrame = 0;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 1, &sidechainFormat,
                              sizeof(sidechainFormat)),
                  "sidechain bus accepts the disabled zero-channel format");
    const UInt32 sidechainCallsBeforeDisabledRender = callbackStates[1].calls;
    requireStatus(initialize(interface), "AU initializes with its sidechain bus disabled");
    requireStatus(render(interface, &flags, &timestamp, 0, frames,
                         reinterpret_cast<AudioBufferList*>(&storage)),
                  "AU renders with its sidechain bus disabled");
    require(callbackStates[1].calls == sidechainCallsBeforeDisabledRender,
            "disabled sidechain bus is not pulled despite retaining its callback");
    requireStatus(uninitialize(interface), "AU uninitializes after disabled-sidechain render");

    requireStatus(interface->Close(interface), "AU interface closes");
    dlclose(image);

    std::puts("AU sidechain buses: stereo dual-bus, disconnected, disabled, and mono paths passed");
    return 0;
}
