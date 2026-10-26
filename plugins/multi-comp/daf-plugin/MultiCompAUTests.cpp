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
    float key[2] = {0.8f, 0.8f};   // the sidechain bus's left and right samples (the main bus reads 0.1)
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
            samples[frame] = state.expectedBus == 0 ? 0.1f : state.key[channel == 0 ? 0 : 1];
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

    requireStatus(initialize(interface), "AU initializes in its advertised mono layout");
    storage.numberBuffers = 1;
    requireStatus(render(interface, &flags, &timestamp, 0, frames,
                         reinterpret_cast<AudioBufferList*>(&storage)),
                  "AU renders its mono main path");
    require(callbackStates[0].calls == 3 && callbackStates[1].calls == 1,
            "mono render pulls only the one-channel main bus");
    requireStatus(uninitialize(interface), "AU uninitializes after mono render");

    // A mono instance keyed by the stereo sidechain (DAF_PLUGIN_SIDECHAIN_ON_EXTRA_IO).
    const AURenderCallbackStruct sidechainCallback = {renderInput, &callbackStates[1]};
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainCallback, sizeof(sidechainCallback)),
                  "a mono main accepts the stereo sidechain");
    requireStatus(initialize(interface), "AU initializes as a mono instance keyed by the stereo sidechain");
    requireStatus(render(interface, &flags, &timestamp, 0, frames,
                         reinterpret_cast<AudioBufferList*>(&storage)),
                  "AU renders as a mono instance keyed by the stereo sidechain");
    require(callbackStates[0].calls == 4 && callbackStates[1].calls == 2,
            "mono keyed render pulls the main and the sidechain bus");
    requireStatus(uninitialize(interface), "AU uninitializes after mono keyed render");
    const AURenderCallbackStruct sidechainOff{};
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainOff, sizeof(sidechainOff)),
                  "sidechain disconnects from the mono instance");

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

    // A stereo main keyed by a mono sidechain (2 + 1): the one key channel keys both main channels, each on its own
    // (unlinked, so the left key cannot reach the right channel through the link).
    requireStatus(setParameter(interface, static_cast<AudioUnitParameterID>(multicompp::ParamId::StereoLink),
                               kAudioUnitScope_Global, 0, 0.0f, 0),
                  "stereo link opens for the mono-key probe");
    AudioStreamBasicDescription keyFormat{};
    dataSize = sizeof(keyFormat);
    requireStatus(getProperty(interface, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &keyFormat,
                              &dataSize),
                  "sidechain format reads before narrowing it");
    keyFormat.mChannelsPerFrame = 1;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &keyFormat,
                              sizeof(keyFormat)),
                  "a stereo main takes a mono sidechain");
    requireStatus(initialize(interface), "AU initializes as a stereo instance keyed by a mono sidechain");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "mono-keyed stereo routing probe renders");
    const float monoKeyLeft = std::abs(left[frames - 1]), monoKeyRight = std::abs(right[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after the mono-keyed stereo probe");
    std::printf("AU stereo main, mono sidechain: left %.6f, right %.6f (disconnected %.6f)\n",
                static_cast<double>(monoKeyLeft), static_cast<double>(monoKeyRight),
                static_cast<double>(disconnectedLevel));
    require(monoKeyLeft < disconnectedLevel * 0.8f && monoKeyRight < disconnectedLevel * 0.8f,
            "a mono sidechain keys both channels of a stereo main");
    keyFormat.mChannelsPerFrame = 2;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &keyFormat,
                              sizeof(keyFormat)),
                  "the sidechain returns to stereo");
    requireStatus(setParameter(interface, static_cast<AudioUnitParameterID>(multicompp::ParamId::StereoLink),
                               kAudioUnitScope_Global, 0, 100.0f, 0),
                  "stereo link returns to 100");

    // A host that connects the stereo sidechain first and then narrows the main bus and the output to mono (Logic's
    // own order follows further down): the unit must initialize as a mono instance (it used to fail with
    // kAudioUnitErr_FormatNotSupported) and its detector must hear the stereo key.
    monoFormat.mChannelsPerFrame = 1;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &monoFormat, sizeof(monoFormat)),
                  "main input narrows to mono with the sidechain connected");
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Output, 0, &monoFormat, sizeof(monoFormat)),
                  "output narrows to mono with the sidechain connected");
    storage.numberBuffers = 1;
    requireStatus(initialize(interface), "AU initializes on a mono track with the sidechain connected first");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "mono keyed routing probe renders");
    const float monoConnectedLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after mono keyed routing probe");
    // The mono instance hears the stereo key's mid: a key of 1.6 on the left alone has the same mid as 0.8 on both
    // sides and must render the same samples (its left channel alone would key twice as hard).
    callbackStates[1].key[0] = 1.6f;
    callbackStates[1].key[1] = 0.0f;
    requireStatus(initialize(interface), "AU initializes for the one-sided key on the mono instance");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "mono instance renders keyed by a one-sided stereo key");
    const float oneSidedKeyLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after the one-sided key probe");
    callbackStates[1].key[0] = callbackStates[1].key[1] = 0.8f;
    std::printf("AU mono instance, stereo key: centred %.6f, one-sided with the same mid %.6f\n",
                static_cast<double>(monoConnectedLevel), static_cast<double>(oneSidedKeyLevel));
    require(oneSidedKeyLevel == monoConnectedLevel, "a mono instance hears a stereo key's mid");
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainOff, sizeof(sidechainOff)),
                  "sidechain disconnects for the mono reference");
    requireStatus(initialize(interface), "AU initializes as a plain mono instance");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "mono unkeyed routing probe renders");
    const float monoDisconnectedLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after mono unkeyed routing probe");
    std::printf("AU mono sidechain routing: disconnected %.6f, connected %.6f\n",
                static_cast<double>(monoDisconnectedLevel), static_cast<double>(monoConnectedLevel));
    require(monoConnectedLevel < monoDisconnectedLevel * 0.8f,
            "a mono instance's detector hears the stereo sidechain");
    monoFormat.mChannelsPerFrame = 2;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &monoFormat, sizeof(monoFormat)),
                  "main input returns to stereo after the mono keyed probe");
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Output, 0, &monoFormat, sizeof(monoFormat)),
                  "output returns to stereo after the mono keyed probe");
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainCallback, sizeof(sidechainCallback)),
                  "sidechain reconnects after the mono keyed probe");
    storage.numberBuffers = 2;

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

    // Logic's mono track (traced in Logic 2026-10-26): every input element set to one channel, then the output,
    // initialize, and only then both elements connected. The sidechain element used to refuse the mono format, and
    // Logic abandoned the unit (no editor, no audio).
    const AURenderCallbackStruct mainOff{};
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 0, &mainOff, sizeof(mainOff)),
                  "main callback disconnects before the Logic mono-track sequence");
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainOff, sizeof(sidechainOff)),
                  "sidechain callback disconnects before the Logic mono-track sequence");
    AudioStreamBasicDescription logicMono{};
    dataSize = sizeof(logicMono);
    requireStatus(getProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &logicMono, &dataSize),
                  "main format reads before the Logic mono-track sequence");
    logicMono.mChannelsPerFrame = 1;
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &logicMono, sizeof(logicMono)),
                  "Logic mono track: the main element takes one channel");
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 1, &logicMono, sizeof(logicMono)),
                  "Logic mono track: the sidechain element takes one channel");
    requireStatus(setProperty(interface, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Output, 0, &logicMono, sizeof(logicMono)),
                  "Logic mono track: the output takes one channel");
    requireStatus(initialize(interface), "Logic mono track: the unit initializes");
    const AURenderCallbackStruct mainCallback = {renderInput, &callbackStates[0]};
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 0, &mainCallback, sizeof(mainCallback)),
                  "Logic mono track: the main element connects while initialized");
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainCallback, sizeof(sidechainCallback)),
                  "Logic mono track: the mono sidechain connects while initialized");
    storage.numberBuffers = 1;
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "Logic mono track renders keyed by its mono sidechain");
    const float logicKeyedLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after the keyed Logic mono-track render");
    requireStatus(setProperty(interface, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 1, &sidechainOff, sizeof(sidechainOff)),
                  "Logic mono track: the sidechain disconnects for the reference");
    requireStatus(initialize(interface), "Logic mono track: the unkeyed reference initializes");
    for (int block = 0; block < 200; ++block)
        requireStatus(render(interface, &flags, &timestamp, 0, frames,
                             reinterpret_cast<AudioBufferList*>(&storage)),
                      "Logic mono track renders unkeyed");
    const float logicUnkeyedLevel = std::abs(left[frames - 1]);
    requireStatus(uninitialize(interface), "AU uninitializes after the unkeyed Logic mono-track render");
    std::printf("AU Logic mono track: unkeyed %.6f, keyed by the mono sidechain %.6f\n",
                static_cast<double>(logicUnkeyedLevel), static_cast<double>(logicKeyedLevel));
    require(logicKeyedLevel < logicUnkeyedLevel * 0.8f,
            "on a Logic mono track the detector hears the mono sidechain");
    // The stereo key's mid is the mono key's level: a key of 0.8 on both sides (the mono instance earlier) keys
    // exactly as a mono key of 0.8 here, where a sum of the sides would key 6 dB harder.
    require(logicKeyedLevel == monoConnectedLevel, "a stereo key keys a mono instance by its mid, not its sum");

    requireStatus(interface->Close(interface), "AU interface closes");
    dlclose(image);

    std::puts("AU sidechain buses: stereo dual-bus, disconnected, disabled, mono, mono keyed and Logic mono-track "
              "paths passed");
    return 0;
}
