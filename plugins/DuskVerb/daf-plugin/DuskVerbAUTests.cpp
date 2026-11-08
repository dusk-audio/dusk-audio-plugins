// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
// Host the actual bundle without installation or a system AU cache change.
#include "DuskVerbParams.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

namespace
{
int checks = 0;
void require(bool ok, const char* message)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
using Entry = void* (*)(const AudioComponentDescription*);
Entry entry = nullptr;
AudioComponentPlugInInterface* factory(const AudioComponentDescription* d)
{ return static_cast<AudioComponentPlugInInterface*>(entry(d)); }
OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32 bus,
               UInt32 frames, AudioBufferList* buffers)
{
    require(bus == 0, "AU pulls the main input bus");
    for (UInt32 c = 0; c < buffers->mNumberBuffers; ++c)
    {
        require(buffers->mBuffers[c].mData != nullptr, "AU supplies input storage");
        auto* samples = static_cast<float*>(buffers->mBuffers[c].mData);
        for (UInt32 n = 0; n < frames; ++n) samples[n] = 0.125f;
    }
    return noErr;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    entry = library ? reinterpret_cast<Entry>(dlsym(library, "PluginAUFactory")) : nullptr;
    require(entry != nullptr, "built AU exports its factory");
    AudioComponentDescription description{kAudioUnitType_Effect, 'DsDv', 'Dusk', 0, 0};
    auto component = AudioComponentRegister(&description, CFSTR("DuskVerb 2 test"), 0x10000, factory);
    require(component != nullptr, "AU registers in this process");
    AudioUnit unit = nullptr;
    require(AudioComponentInstanceNew(component, &unit) == noErr && unit, "AU instantiates");
    const auto set = [&](int id, float value) {
        require(AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, value, 0) == noErr,
                "AU accepts host parameters");
    };
    const auto values = [&] {
        std::array<float, duskverb::kNumParams> result{};
        for (int i = 0; i < duskverb::kNumParams; ++i)
            require(AudioUnitGetParameter(unit, i, kAudioUnitScope_Global, 0, &result[i]) == noErr,
                    "AU exposes each input parameter");
        return result;
    };
    const auto save = [&] {
        CFPropertyListRef state = nullptr; UInt32 size = sizeof(state);
        require(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                    &state, &size) == noErr && state, "AU saves state");
        require(CFGetTypeID(state) == CFDictionaryGetTypeID(), "AU state is a dictionary");
        return state;
    };
    set(duskverb::Mix, 0.23f);
    set(duskverb::Algorithm, 10);
    set(duskverb::Freeze, 1);
    const auto expected = values();
    const auto saved = save();
    set(duskverb::Mix, 0.8f);
    set(duskverb::Freeze, 0);
    require(values() != expected, "state round-trip control changes the AU");
    require(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                &saved, sizeof(saved)) == noErr, "AU restores complete state");
    require(values() == expected, "all AU parameters survive state restore");
    // Corrupt one authoritative state property. No parameter may be partially applied.
    auto invalid = static_cast<CFMutableDictionaryRef>(const_cast<void*>(
        CFPropertyListCreateDeepCopy(nullptr, saved, kCFPropertyListMutableContainersAndLeaves)));
    auto content = static_cast<CFMutableDictionaryRef>(const_cast<void*>(CFDictionaryGetValue(invalid, CFSTR("data"))));
    require(content && CFGetTypeID(content) == CFDictionaryGetTypeID(), "state has DAF content");
    CFDictionarySetValue(content, CFSTR("states"), CFSTR("invalid"));
    CFPropertyListRef bad = invalid;
    require(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                &bad, sizeof(bad)) != noErr, "malformed AU state is rejected");
    require(values() == expected, "rejected AU state preserves every parameter");
    CFRelease(invalid); CFRelease(saved);

    AUChannelInfo layouts[8]{}; UInt32 size = sizeof(layouts);
    require(AudioUnitGetProperty(unit, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global,
                                0, layouts, &size) == noErr, "AU advertises channel layouts");
    bool mono = false, stereo = false;
    for (UInt32 i = 0; i < size / sizeof(AUChannelInfo); ++i)
    { mono |= layouts[i].inChannels == 1 && layouts[i].outChannels == 1;
      stereo |= layouts[i].inChannels == 2 && layouts[i].outChannels == 2; }
    require(mono && stereo, "AU supports mono and stereo inserts");
    for (UInt32 channels : {1u, 2u})
    {
        AudioStreamBasicDescription format{};
        format.mSampleRate = 48000;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float);
        format.mFramesPerPacket = 1; format.mChannelsPerFrame = channels; format.mBitsPerChannel = 32;
        for (auto scope : {kAudioUnitScope_Input, kAudioUnitScope_Output})
            require(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, scope, 0,
                                        &format, sizeof(format)) == noErr, "AU accepts selected channel format");
        AURenderCallbackStruct callback{input, nullptr};
        require(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                    0, &callback, sizeof(callback)) == noErr, "AU connects input callback");
        require(AudioUnitInitialize(unit) == noErr, "AU initializes selected layout");
        float left[256]{}, right[256]{};
        struct { UInt32 count; AudioBuffer buffers[2]; } storage{
            channels, {{1, sizeof(left), left}, {1, sizeof(right), right}}};
        AudioTimeStamp time{}; time.mFlags = kAudioTimeStampSampleTimeValid;
        const auto render = [&] {
            AudioUnitRenderActionFlags flags = 0;
            require(AudioUnitRender(unit, &flags, &time, 0, 256,
                                    reinterpret_cast<AudioBufferList*>(&storage)) == noErr, "AU renders");
            time.mSampleTime += 256;
            for (unsigned i = 0; i < 256; ++i)
                require(std::isfinite(left[i]) && (channels == 1 || std::isfinite(right[i])), "finite AU output");
        };
        const auto tail = [&] {
            Float64 seconds = 0; UInt32 bytes = sizeof(seconds);
            require(AudioUnitGetProperty(unit, kAudioUnitProperty_TailTime, kAudioUnitScope_Global,
                                        0, &seconds, &bytes) == noErr, "AU reports its tail");
            return seconds;
        };
        set(duskverb::Freeze, 1);
        for (int i = 0; i < 20; ++i) render();
        // AU has no infinite sentinel; the DAF adapter documents one hour.
        require(tail() == 3600.0, "AU maps Freeze to its one-hour tail fallback");
        require(AudioUnitReset(unit, kAudioUnitScope_Global, 0) == noErr, "AU reset succeeds");
        render();
        require(tail() == 3600.0, "AU reset retains the Freeze contract");
        set(duskverb::Freeze, 0); render();
        require(tail() >= 30.0 && tail() < 3600.0, "AU unfreeze restores a finite tail");
        set(duskverb::Bypass, 1);
        for (int i = 0; i < 20; ++i) render();
        require(left[255] == 0.125f && (channels == 1 || right[255] == 0.125f),
                "AU bypass routes the selected input layout exactly");
        set(duskverb::Bypass, 0);
        require(AudioUnitUninitialize(unit) == noErr, "AU uninitializes");
    }
    require(AudioComponentInstanceDispose(unit) == noErr, "AU disposes cleanly");
    std::printf("PASS: %d native AU checks\n", checks);
    // Registered factories remain process-owned until exit; retain the library.
    return 0;
}
