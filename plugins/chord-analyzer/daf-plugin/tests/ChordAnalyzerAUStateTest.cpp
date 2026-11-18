// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Load the built AU directly, without installing it or touching the user's AU cache.
#include "ChordAnalyzerState.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace duskaudio::chord;
namespace
{
int checks = 0, failures = 0;
using FactoryEntry = void* (*)(const AudioComponentDescription*);
FactoryEntry loadedFactory = nullptr;
AudioComponentPlugInInterface* factory(const AudioComponentDescription* description)
{ return static_cast<AudioComponentPlugInInterface*>(loadedFactory(description)); }
void check(bool ok, const char* message)
{ ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } }
OSType code(const char* text)
{
    return (OSType(text[0]) << 24) | (OSType(text[1]) << 16) | (OSType(text[2]) << 8) | OSType(text[3]);
}
CFMutableDictionaryRef copy(CFDictionaryRef original)
{
    return const_cast<CFMutableDictionaryRef>(static_cast<CFDictionaryRef>(
        CFPropertyListCreateDeepCopy(nullptr, original, kCFPropertyListMutableContainersAndLeaves)));
}
CFMutableDictionaryRef data(CFMutableDictionaryRef root)
{
    return const_cast<CFMutableDictionaryRef>(static_cast<CFDictionaryRef>(CFDictionaryGetValue(root, CFSTR("data"))));
}
void snapshot(CFMutableDictionaryRef root, CFTypeRef value, unsigned copies = 1)
{
    const auto states = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    const auto item = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(item, CFSTR("parameters"), value);
    for (unsigned i = 0; i < copies; ++i) CFArrayAppendValue(states, item);
    CFDictionarySetValue(data(root), CFSTR("states"), states);
    CFRelease(item); CFRelease(states);
}
void snapshot(CFMutableDictionaryRef root, const std::string& value)
{
    const auto text = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(value.data()), value.size(), kCFStringEncodingUTF8, false);
    snapshot(root, text); CFRelease(text);
}
std::string snapshotText(CFDictionaryRef root)
{
    const auto content = static_cast<CFDictionaryRef>(CFDictionaryGetValue(root, CFSTR("data")));
    if (!content || CFGetTypeID(content) != CFDictionaryGetTypeID()) return {};
    const auto states = static_cast<CFArrayRef>(CFDictionaryGetValue(content, CFSTR("states")));
    if (!states || CFGetTypeID(states) != CFArrayGetTypeID() || CFArrayGetCount(states) != 1) return {};
    const auto item = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(states, 0));
    if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID() || CFDictionaryGetCount(item) != 1) return {};
    const auto value = static_cast<CFStringRef>(CFDictionaryGetValue(item, CFSTR("parameters")));
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
    std::vector<char> bytes(CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1);
    return CFStringGetCString(value, bytes.data(), bytes.size(), kCFStringEncodingUTF8) ? std::string(bytes.data()) : std::string();
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const bool midiOnly = std::strcmp(argv[2], "1") == 0;
    auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    loadedFactory = library ? reinterpret_cast<FactoryEntry>(dlsym(library, "PluginAUFactory")) : nullptr;
    check(loadedFactory != nullptr, "actual built AU exports its factory");
    if (!loadedFactory) { const auto* error = dlerror(); std::fprintf(stderr, "%s\n", error ? error : "Missing AU factory"); return 1; }
    AudioComponentDescription description{code(midiOnly ? "aumi" : "aumu"), code(midiOnly ? "DsCM" : "DsCA"), code("Dusk"), 0, 0};
    const auto component = AudioComponentRegister(&description, CFSTR("Chord State Test"), 0x100, factory);
    check(component != nullptr, "AU registers in this test process with the correct successor identity");
    AudioUnit unit = nullptr;
    const auto opened = component ? AudioComponentInstanceNew(component, &unit) : OSStatus(-1);
    check(opened == noErr && unit, "native AU host instantiates the built successor");
    if (opened != noErr || !unit) return 1;
    auto values = [&] {
        InputValues result{};
        for (unsigned i = 0; i < inputParameterCount; ++i)
            check(AudioUnitGetParameter(unit, i, kAudioUnitScope_Global, 0, &result[i]) == noErr, "AU input remains readable");
        return result;
    };
    auto set = [&](const InputValues& inputs) {
        for (unsigned i = 0; i < inputParameterCount; ++i)
            check(AudioUnitSetParameter(unit, i, kAudioUnitScope_Global, 0, inputs[i], 0) == noErr, "AU input remains automatable");
    };
    auto save = [&]() -> CFDictionaryRef {
        CFPropertyListRef result = nullptr;
        UInt32 size = sizeof(result);
        const auto status = AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &result, &size);
        bool valid = status == noErr && result && CFGetTypeID(result) == CFDictionaryGetTypeID();
        if (valid)
        {
            const auto content = CFDictionaryGetValue(static_cast<CFDictionaryRef>(result), CFSTR("data"));
            valid = content && CFGetTypeID(content) == CFDictionaryGetTypeID();
        }
        check(valid, "native AU saves a dictionary state");
        if (!valid) { if (result) CFRelease(result); return nullptr; }
        return static_cast<CFDictionaryRef>(result);
    };
    auto load = [&](CFDictionaryRef value) {
        const CFPropertyListRef object = value;
        return AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &object, sizeof(object));
    };
    UInt32 parameterBytes = 0; Boolean writable = false;
    check(AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &parameterBytes, &writable) == noErr
          && parameterBytes == PARAM_COUNT * sizeof(AudioUnitParameterID), "native AU retains exactly ten parameters");
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
    {
        AudioUnitParameterInfo info{}; UInt32 size = sizeof(info);
        check(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, i, &info, &size) == noErr,
              "every retained AU parameter ID is present");
        check(info.minValue == 0 && info.maxValue == parameterSpecs[i].maximum && info.defaultValue == parameterSpecs[i].initial,
              "native AU domains and defaults match the shared parameter table");
        check(bool(info.flags & kAudioUnitParameterFlag_IsWritable) == (i < inputParameterCount), "only five input controls are writable");
        if (info.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString);
    }
    const InputValues defaults{{0,0,2,1,1}}, inputs{{6,1,1,0,0}};
    check(values() == defaults, "fresh native AU has the correct defaults");
    set(inputs);
    const auto saved = save();
    if (!saved) { AudioComponentInstanceDispose(unit); return 1; }
    check(snapshotText(saved) == encodeInputState(inputs), "native AU saves exactly the complete five-input snapshot");
    const auto content = static_cast<CFDictionaryRef>(CFDictionaryGetValue(saved, CFSTR("data")));
    const auto persisted = static_cast<CFArrayRef>(CFDictionaryGetValue(content, CFSTR("params")));
    bool boundary = persisted && CFGetTypeID(persisted) == CFArrayGetTypeID() && CFArrayGetCount(persisted) == inputParameterCount;
    std::array<bool, inputParameterCount> seen{};
    for (CFIndex row = 0; boundary && row < CFArrayGetCount(persisted); ++row)
    {
        const auto item = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(persisted, row));
        if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID() || CFDictionaryGetCount(item) != 1) {boundary = false; break;}
        const void* key = nullptr; const void* value = nullptr;
        CFDictionaryGetKeysAndValues(item, &key, &value);
        unsigned index = 0;
        for (; index < inputParameterCount; ++index)
        {
            const auto expected = CFStringCreateWithCString(nullptr, parameterSpecs[index].symbol, kCFStringEncodingASCII);
            const bool matches = CFEqual(key, expected); CFRelease(expected);
            if (matches) break;
        }
        float number = -1;
        if (index == inputParameterCount || seen[index] || CFGetTypeID(value) != CFNumberGetTypeID()
            || !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberFloat32Type, &number) || number != inputs[index])
        {boundary = false; break;}
        seen[index] = true;
    }
    for (bool present : seen) boundary &= present;
    check(boundary, "native parameter records contain every saved input exactly once and no detection outputs");
    set(defaults);
    const auto initial = save();
    if (!initial) { CFRelease(saved); AudioComponentInstanceDispose(unit); return 1; }
    auto reject = [&](CFMutableDictionaryRef invalid) {
        set(defaults);
        check(load(invalid) == kAudioUnitErr_InvalidPropertyValue,
              "native AU reports a rejected state to the host");
        check(values() == defaults, "malformed native AU recall preserves all five live inputs");
        const auto after = save();
        check(after && CFEqual(initial, after), "malformed AU recall leaves the entire saved state unchanged");
        if (after) CFRelease(after);
        CFRelease(invalid);
    };
    auto invalid = copy(saved); CFDictionaryRemoveValue(data(invalid), CFSTR("states")); reject(invalid);
    invalid = copy(saved); snapshot(invalid, CFSTR(""), 0); reject(invalid);
    invalid = copy(saved); snapshot(invalid, CFSTR("invalid"), 2); reject(invalid);
    invalid = copy(saved); snapshot(invalid, kCFBooleanTrue); reject(invalid);
    invalid = copy(saved); CFDictionarySetValue(data(invalid), CFSTR("states"), CFSTR("wrong type")); reject(invalid);
    for (const auto& payload : std::vector<std::string>{"", "invalid", "ChordAnalyzer2:2\n", encodeInputState(inputs) + "detectedRoot=1\n",
                                                     encodeInputState(inputs) + std::string("\0junk", 5)})
    { invalid = copy(saved); snapshot(invalid, payload); reject(invalid); }
    invalid = copy(saved); snapshot(invalid, encodeInputState(inputs));
    const auto array = static_cast<CFArrayRef>(CFDictionaryGetValue(data(invalid), CFSTR("states")));
    const auto item = const_cast<CFMutableDictionaryRef>(static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(array, 0)));
    const auto retained = CFDictionaryGetValue(item, CFSTR("parameters")); CFRetain(retained);
    const auto keyBytes = std::string("parameters") + std::string("\0junk", 5);
    const auto keyWithNul = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(keyBytes.data()), keyBytes.size(), kCFStringEncodingUTF8, false);
    CFDictionaryRemoveAllValues(item); CFDictionarySetValue(item, keyWithNul, retained);
    CFRelease(keyWithNul); CFRelease(retained); reject(invalid);
    for (unsigned i = 0; i < inputParameterCount; ++i)
    {
        const auto text = encodeInputState(inputs), key = std::string(parameterSpecs[i].symbol) + "=";
        const auto begin = text.find(key), end = text.find('\n', begin);
        invalid = copy(saved); snapshot(invalid, text.substr(0, begin) + text.substr(end + 1)); reject(invalid);
        invalid = copy(saved); snapshot(invalid, text + text.substr(begin, end + 1 - begin)); reject(invalid);
        for (const char* bad : {"0.5", "nan", "-1", "999", "1junk"})
        { invalid = copy(saved); snapshot(invalid, text.substr(0, begin) + key + bad + text.substr(end)); reject(invalid); }
    }
    auto valid = copy(saved);
    CFDictionarySetValue(data(valid), CFSTR("params"), CFSTR("redundant parameters are not authoritative"));
    check(load(valid) == noErr && values() == inputs, "valid snapshot recalls every input despite conflicting redundant parameters");
    CFRelease(valid);
    const auto after = save();
    check(after && CFEqual(saved, after), "native AU save/load/save reproduces the same complete dictionary");
    if (after) CFRelease(after);
    set(defaults);
    check(values() == defaults, "all five AU inputs remain automatable after complete recall");
    CFArrayRef programs = nullptr; UInt32 size = sizeof(programs);
    const auto status = AudioUnitGetProperty(unit, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &programs, &size);
    check(status != noErr || !programs || CFArrayGetCount(programs) == 0, "successor advertises no factory programs");
    if (programs) CFRelease(programs);
    CFRelease(initial); CFRelease(saved);
    check(AudioComponentInstanceDispose(unit) == noErr, "native AU disposes its runtime cleanly");
    // Registration retains the factory address for this process's lifetime.
    std::printf("Chord AU state %s: %d checks, %d failures\n", midiOnly ? "MIDI" : "instrument", checks, failures);
    return failures ? 1 : 0;
}
