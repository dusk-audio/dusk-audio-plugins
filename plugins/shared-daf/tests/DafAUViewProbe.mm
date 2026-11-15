// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Headless AU Cocoa-view probe for any installed DAF plugin. It instantiates the
// view the way a host does, which is the step auval never performs: auval only
// enumerates the view class, so a plugin whose view fails to build still passes.
//
// It reports the view tree at each stage and, for in-process hosting, reads the
// plugin's own GL front buffer back and writes it to a PNG. A drawn ImGui panel
// yields hundreds of distinct colours; a blank surface yields one or two, which
// is the "black box in the host" symptom expressed as a number. The PNG holds the
// RGB the surface shows and no alpha: the GL surface is opaque and hosts show its
// raw RGB, so a framebuffer alpha below 255 is not coverage and is dropped.
//
//   clang++ -std=c++17 -ObjC++ -Wno-deprecated-declarations \
//     plugins/shared-daf/tests/DafAUViewProbe.mm \
//     -framework AudioToolbox -framework AudioUnit -framework AppKit \
//     -framework Foundation -framework CoreGraphics -framework ImageIO \
//     -framework OpenGL -o /tmp/daf_au_view_probe
//
// Arguments, all optional:
//   [1] [2] [3]  AU type, subtype, manufacturer codes   (default aufx DsMc Dusk)
//   [4]          "oop" to load out of process as a sandboxing host does, any
//                other value for in-process. Out of process the host receives
//                Apple's _RemoteAUv2ViewFactory and an NSRemoteView, so the GL
//                readback is unavailable and an external screencapture of the
//                window id printed as "WINDOWID:" is the only way to see pixels.
//   [5]          seconds to hold the window open, for that screencapture
//   [6]          "WxH" to replay the resize a host performs immediately after
//                creating the view, which is how Logic restores a saved size.
//                With WxH the view is also created with WxH as its preferred
//                size: out of process Apple's remote container sizes the host
//                window to it, in process DAF ignores it.
//                Without it, or with "-", the window takes the size the view
//                reports when it is created, the plugin's own design size, and
//                follows the view if it changes that size itself. In process, a
//                view that reports no size at creation is an error (pass WxH);
//                out of process the remote view reports 1x1 at creation and its
//                real size later, and the window follows it there.
//   [7]          directory for shot_<subtype>.png (default DAF_PROBE_CAPTURE_DIR,
//                then the system temporary directory)
//
// Environment, all optional:
//   DAF_PROBE_PRESET=n          sets kAudioUnitProperty_PresentPreset to factory
//                               preset n
//   DAF_PROBE_SET="id=v,..."    AudioUnitSetParameter for each entry: id is the DAF
//                               parameter index, v the value in host units
//       Both are applied after AudioUnitInitialize and before the view is built,
//       so the view opens on that state (lit switches, engine-specific controls).
//       The preset goes first, so DAF_PROBE_SET overrides the preset's values.
//       Malformed input, a value that is not a finite number within float range,
//       an unknown preset or parameter, a read-only parameter, a value outside the
//       parameter's range, a value the unit does not hold back, or a failing AU
//       call stops the probe with exit status 1.
//   DAF_PROBE_CAPTURE_DIR=dir   capture directory when [7] is not given
//   DAF_PROBE_RENDER            set to any value (even empty or 0): the input
//                               buses are connected before AudioUnitInitialize,
//                               and once the editor is open a sine is rendered
//                               through the plugin; in-process only, ignored out
//                               of process

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <OpenGL/gl.h>
#import <ImageIO/ImageIO.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
OSType fourcc(const char* s)
{
    if (s == nullptr || std::strlen(s) < 4)
        return 0;
    return (static_cast<OSType>(s[0]) << 24) | (static_cast<OSType>(s[1]) << 16)
         | (static_cast<OSType>(s[2]) << 8) | static_cast<OSType>(s[3]);
}

// Feeds every connected input bus, so the plugin runs its real signal path while
// the editor is open. Set DAF_PROBE_RENDER=1 to exercise it.
OSStatus feedInput(void* refCon,
                   AudioUnitRenderActionFlags*,
                   const AudioTimeStamp*,
                   UInt32,
                   UInt32 frames,
                   AudioBufferList* data)
{
    const float level = *static_cast<const float*>(refCon);
    for (UInt32 buffer = 0; buffer < data->mNumberBuffers; ++buffer)
    {
        const AudioBuffer& audioBuffer = data->mBuffers[buffer];
        float* const samples = static_cast<float*>(audioBuffer.mData);
        if (samples == nullptr)
            continue;
        const UInt32 channels = audioBuffer.mNumberChannels;
        if (channels == 0)
            continue;
        const size_t capacity = audioBuffer.mDataByteSize / sizeof(float);
        const size_t sampleCount = std::min(
            capacity, static_cast<size_t>(frames) * channels);
        for (size_t sample = 0; sample < sampleCount; ++sample)
        {
            const UInt32 frame = static_cast<UInt32>(sample / channels);
            samples[sample] = level * std::sin(6.2831853f * 220.0f
                                               * (float) frame / 48000.0f);
        }
    }
    return noErr;
}

void describeView(NSView* view, int depth)
{
    if (view == nil)
        return;
    const NSRect f = [view frame];
    std::printf("%*s%-28s frame=(%.1f,%.1f %.1fx%.1f) hidden=%d layer=%p wantsLayer=%d\n",
                depth * 2, "", [NSStringFromClass([view class]) UTF8String],
                f.origin.x, f.origin.y, f.size.width, f.size.height,
                (int) [view isHidden], (void*) [view layer], (int) [view wantsLayer]);
    for (NSView* sub in [view subviews])
        describeView(sub, depth + 1);
}

NSView* findGLView(NSView* view)
{
    if (view == nil)
        return nil;
    if ([view respondsToSelector:@selector(openGLContext)])
        return view;
    for (NSView* sub in [view subviews])
        if (NSView* const found = findGLView(sub))
            return found;
    return nil;
}

void printUsage(const char* program)
{
    std::fprintf(stderr,
        "usage: %s [type subtype manu] [oop|in] [holdSeconds] [WxH|-] [captureDir]\n"
        "  defaults: aufx DsMc Dusk, in-process, no hold, the view's own size,\n"
        "            DAF_PROBE_CAPTURE_DIR or the system temporary directory\n"
        "  environment, applied before the view is built, preset first:\n"
        "    DAF_PROBE_PRESET=n         factory preset n (kAudioUnitProperty_PresentPreset)\n"
        "    DAF_PROBE_SET=\"id=v,...\"   AudioUnitSetParameter: DAF parameter index = host value\n"
        "  environment, used once the editor is open:\n"
        "    DAF_PROBE_RENDER=<any>     render a sine through the plugin with the editor open\n"
        "                               (set to any value, even 0; in-process only)\n",
        program);
}

// A whole string of decimal digits and nothing else: no sign, no spaces.
bool parseUnsigned(const std::string& text, unsigned long long limit, unsigned long long& out)
{
    if (text.empty() || text.size() > 10)
        return false;
    unsigned long long value = 0;
    for (const char c : text)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
        value = value * 10 + static_cast<unsigned long long>(c - '0');
    }
    if (value > limit)
        return false;
    out = value;
    return true;
}

// A whole string that is one finite number. An overflow comes back as infinity
// and is rejected; an underflow is a finite value near zero and is accepted.
bool parseFloat(const std::string& text, float& out)
{
    if (text.empty() || std::isspace(static_cast<unsigned char>(text[0])))
        return false;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

// argv[6]: "WxH" with both sides positive, or "-" for no host resize.
bool parseHostSize(const char* text, bool& wanted, NSSize& size)
{
    wanted = false;
    if (std::strcmp(text, "-") == 0)
        return true;
    const std::string spec(text);
    const size_t x = spec.find('x');
    unsigned long long width = 0, height = 0;
    if (x == std::string::npos
        || !parseUnsigned(spec.substr(0, x), INT32_MAX, width)
        || !parseUnsigned(spec.substr(x + 1), INT32_MAX, height)
        || width == 0 || height == 0)
        return false;
    wanted = true;
    size = NSMakeSize(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
    return true;
}

std::string utf8(CFStringRef text)
{
    if (text == nullptr)
        return {};
    const char* const bytes = [(__bridge NSString*) text UTF8String];
    return bytes != nullptr ? std::string(bytes) : std::string();
}

// DAF_PROBE_PRESET=n: factory preset n, which must be one the unit lists.
bool applyPresetFromEnvironment(AudioUnit unit)
{
    const char* const text = std::getenv("DAF_PROBE_PRESET");
    if (text == nullptr)
        return true;

    unsigned long long requested = 0;
    if (!parseUnsigned(text, INT32_MAX, requested))
    {
        std::fprintf(stderr,
                     "FAIL: DAF_PROBE_PRESET=\"%s\" is not a factory preset number "
                     "(a non-negative decimal integer)\n", text);
        return false;
    }

    CFArrayRef presets = nullptr;
    UInt32 size = sizeof(presets);
    OSStatus status = AudioUnitGetProperty(unit, kAudioUnitProperty_FactoryPresets,
                                           kAudioUnitScope_Global, 0, &presets, &size);
    if (status != noErr || presets == nullptr || CFArrayGetCount(presets) == 0)
    {
        std::fprintf(stderr,
                     "FAIL: DAF_PROBE_PRESET=%s: the unit lists no factory presets "
                     "(kAudioUnitProperty_FactoryPresets -> %d)\n", text, (int) status);
        if (presets != nullptr)
            CFRelease(presets);
        return false;
    }

    const CFIndex count = CFArrayGetCount(presets);
    const AUPreset* match = nullptr;
    SInt32 lowest = INT32_MAX, highest = INT32_MIN;
    for (CFIndex i = 0; i < count; ++i)
    {
        const AUPreset* const preset =
            static_cast<const AUPreset*>(CFArrayGetValueAtIndex(presets, i));
        if (preset == nullptr)
            continue;
        lowest = std::min(lowest, preset->presetNumber);
        highest = std::max(highest, preset->presetNumber);
        if (preset->presetNumber == static_cast<SInt32>(requested))
            match = preset;
    }
    if (match == nullptr)
    {
        std::fprintf(stderr,
                     "FAIL: DAF_PROBE_PRESET=%s is not one of the unit's %ld factory "
                     "presets (numbers %d to %d)\n",
                     text, (long) count, (int) lowest, (int) highest);
        CFRelease(presets);
        return false;
    }

    AUPreset chosen = *match;
    const std::string name = utf8(chosen.presetName);
    status = AudioUnitSetProperty(unit, kAudioUnitProperty_PresentPreset,
                                  kAudioUnitScope_Global, 0, &chosen, sizeof(chosen));
    CFRelease(presets);
    if (status != noErr)
    {
        std::fprintf(stderr,
                     "FAIL: DAF_PROBE_PRESET=%s: setting kAudioUnitProperty_PresentPreset "
                     "to \"%s\" -> %d\n", text, name.c_str(), (int) status);
        return false;
    }

    AUPreset present{};
    size = sizeof(present);
    status = AudioUnitGetProperty(unit, kAudioUnitProperty_PresentPreset,
                                  kAudioUnitScope_Global, 0, &present, &size);
    if (status == noErr && present.presetName != nullptr)
        CFRelease(present.presetName);
    if (status != noErr || present.presetNumber != chosen.presetNumber)
    {
        std::fprintf(stderr,
                     "FAIL: DAF_PROBE_PRESET=%s: the unit reports preset %d (status %d) "
                     "after being set to it\n",
                     text, (int) present.presetNumber, (int) status);
        return false;
    }
    std::printf("PRESET: %d \"%s\"\n", (int) chosen.presetNumber, name.c_str());
    return true;
}

// DAF_PROBE_SET="id=v,...": every entry is checked against the unit's parameter
// list, writability and range before any is set, so a bad entry changes nothing.
bool applyParametersFromEnvironment(AudioUnit unit)
{
    const char* const text = std::getenv("DAF_PROBE_SET");
    if (text == nullptr)
        return true;

    UInt32 listSize = 0;
    OSStatus status = AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList,
                                               kAudioUnitScope_Global, 0, &listSize, nullptr);
    std::vector<AudioUnitParameterID> ids(
        status == noErr ? listSize / sizeof(AudioUnitParameterID) : 0);
    if (status == noErr && !ids.empty())
        status = AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList,
                                      kAudioUnitScope_Global, 0, ids.data(), &listSize);
    if (status != noErr)
    {
        std::fprintf(stderr, "FAIL: DAF_PROBE_SET: kAudioUnitProperty_ParameterList -> %d\n",
                     (int) status);
        return false;
    }
    ids.resize(listSize / sizeof(AudioUnitParameterID));

    struct Setting { AudioUnitParameterID id; float value, low, high; std::string name; };
    std::vector<Setting> settings;
    const std::string all(text);
    for (size_t start = 0;;)
    {
        const size_t comma = all.find(',', start);
        const std::string entry = all.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        const size_t equals = entry.find('=');
        unsigned long long id = 0;
        float value = 0.0f;
        if (equals == std::string::npos
            || !parseUnsigned(entry.substr(0, equals), UINT32_MAX, id)
            || !parseFloat(entry.substr(equals + 1), value))
        {
            std::fprintf(stderr,
                         "FAIL: DAF_PROBE_SET entry \"%s\" is not id=value with id a DAF "
                         "parameter index and value a finite number within float range, "
                         "in host units (entries separated by commas)\n", entry.c_str());
            return false;
        }
        const AudioUnitParameterID parameter = static_cast<AudioUnitParameterID>(id);
        if (std::find(ids.begin(), ids.end(), parameter) == ids.end())
        {
            std::fprintf(stderr,
                         "FAIL: DAF_PROBE_SET entry \"%s\": the unit has no parameter %u "
                         "(it lists %zu parameters)\n",
                         entry.c_str(), (unsigned) parameter, ids.size());
            return false;
        }
        for (const Setting& earlier : settings)
            if (earlier.id == parameter)
            {
                std::fprintf(stderr, "FAIL: DAF_PROBE_SET sets parameter %u twice\n",
                             (unsigned) parameter);
                return false;
            }

        AudioUnitParameterInfo info{};
        UInt32 infoSize = sizeof(info);
        status = AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo,
                                      kAudioUnitScope_Global, parameter, &info, &infoSize);
        if (status != noErr)
        {
            std::fprintf(stderr,
                         "FAIL: DAF_PROBE_SET: kAudioUnitProperty_ParameterInfo for "
                         "parameter %u -> %d\n", (unsigned) parameter, (int) status);
            return false;
        }
        const bool hasCFName = (info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0
                            && info.cfNameString != nullptr;
        const std::string name = hasCFName
            ? utf8(info.cfNameString)
            : std::string(info.name, strnlen(info.name, sizeof(info.name)));
        if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0)
        {
            if (hasCFName)
                CFRelease(info.cfNameString);
            if (info.unit == kAudioUnitParameterUnit_CustomUnit && info.unitName != nullptr)
                CFRelease(info.unitName);
        }
        if ((info.flags & kAudioUnitParameterFlag_IsWritable) == 0)
        {
            std::fprintf(stderr, "FAIL: DAF_PROBE_SET: parameter %u \"%s\" is read-only\n",
                         (unsigned) parameter, name.c_str());
            return false;
        }
        if (value < info.minValue || value > info.maxValue)
        {
            std::fprintf(stderr,
                         "FAIL: DAF_PROBE_SET: %g is outside parameter %u \"%s\"'s range "
                         "%g to %g\n", (double) value, (unsigned) parameter, name.c_str(),
                         (double) info.minValue, (double) info.maxValue);
            return false;
        }
        settings.push_back({ parameter, value, info.minValue, info.maxValue, name });

        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    for (const Setting& setting : settings)
    {
        status = AudioUnitSetParameter(unit, setting.id, kAudioUnitScope_Global, 0,
                                       setting.value, 0);
        if (status != noErr)
        {
            std::fprintf(stderr,
                         "FAIL: AudioUnitSetParameter(%u \"%s\", %g) -> %d\n",
                         (unsigned) setting.id, setting.name.c_str(),
                         (double) setting.value, (int) status);
            return false;
        }
        AudioUnitParameterValue held = 0.0f;
        status = AudioUnitGetParameter(unit, setting.id, kAudioUnitScope_Global, 0, &held);
        // A float round trip may move the last bit; a clamp, a step or an ignored
        // set moves it further, and then the capture would not show what was asked.
        const float tolerance = 1.0e-5f * std::max(1.0f, setting.high - setting.low);
        if (status != noErr || std::fabs(held - setting.value) > tolerance)
        {
            std::fprintf(stderr,
                         "FAIL: parameter %u \"%s\" holds %g (status %d) after being set "
                         "to %g\n", (unsigned) setting.id, setting.name.c_str(),
                         (double) held, (int) status, (double) setting.value);
            return false;
        }
        std::printf("SET: parameter %u \"%s\" = %g\n",
                    (unsigned) setting.id, setting.name.c_str(), (double) held);
    }
    return true;
}
}

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        const char* typeCode = argc > 1 ? argv[1] : "aufx";
        const char* subCode  = argc > 2 ? argv[2] : "DsMc";
        const char* manuCode = argc > 3 ? argv[3] : "Dusk";

        // AppKit has to be up before any NSView is created, even with no UI session.
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        AudioComponentDescription description{};
        description.componentType         = fourcc(typeCode);
        description.componentSubType      = fourcc(subCode);
        description.componentManufacturer = fourcc(manuCode);
        if (description.componentType == 0 || description.componentSubType == 0
            || description.componentManufacturer == 0)
        {
            std::fprintf(stderr,
                         "FAIL: AU type, subtype, and manufacturer codes must each contain at least four characters\n");
            printUsage(argv[0]);
            return 1;
        }

        // Logic resizes the view it was handed straight after creating it, to the
        // plugin window size saved with the session. argv[6] = "WxH" replays that.
        bool hostResize = false;
        NSSize hostResizeSize = NSZeroSize;
        if (argc > 6 && !parseHostSize(argv[6], hostResize, hostResizeSize))
        {
            std::fprintf(stderr, "FAIL: \"%s\" is not a host size: expected WxH, both "
                                 "positive integers, or - for the view's own size\n", argv[6]);
            printUsage(argv[0]);
            return 1;
        }

        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (component == nullptr)
        {
            std::fprintf(stderr, "FAIL: no registered AU for %s %s %s\n",
                         typeCode, subCode, manuCode);
            return 1;
        }

        CFStringRef nameRef = nullptr;
        AudioComponentCopyName(component, &nameRef);
        std::printf("component: %s\n",
                    nameRef ? [(__bridge NSString*) nameRef UTF8String] : "(unnamed)");
        if (nameRef) CFRelease(nameRef);

        const bool outOfProcess = (argc > 4 && std::strcmp(argv[4], "oop") == 0);
        AudioUnit unit = nullptr;
        OSStatus status = noErr;

        if (outOfProcess)
        {
            // What Logic does when it sandboxes a plugin: the DSP lives in
            // AUHostingServiceXPC and the host talks to a proxy AudioUnit.
            __block AudioUnit produced = nullptr;
            __block OSStatus produceStatus = noErr;
            __block bool done = false;
            AudioComponentInstantiate(
                component, kAudioComponentInstantiation_LoadOutOfProcess,
                ^(AudioComponentInstance instance, OSStatus err) {
                    produced = instance; produceStatus = err; done = true;
                });
            for (int spin = 0; spin < 2000 && !done; ++spin)
                [[NSRunLoop currentRunLoop]
                    runMode:NSDefaultRunLoopMode
                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            unit = produced;
            status = produceStatus;
            std::printf("instantiation: OUT OF PROCESS (done=%d status=%d)\n",
                        (int) done, (int) status);
        }
        else
        {
            status = AudioComponentInstanceNew(component, &unit);
            std::printf("instantiation: in-process\n");
        }

        if (status != noErr || unit == nullptr)
        {
            std::fprintf(stderr, "FAIL: instantiation -> %d\n", (int) status);
            return 1;
        }
        std::printf("instantiated: unit=%p\n", (void*) unit);

        // Connect every input bus before initialising, so the plugin can be driven
        // with real audio while its editor is open.
        const bool doRender = std::getenv("DAF_PROBE_RENDER") != nullptr;
        static float mainLevel = 0.5f, sideLevel = 0.9f;
        UInt32 inputBuses = 0;
        if (doRender && !outOfProcess)
        {
            UInt32 busSize = sizeof(inputBuses);
            AudioUnitGetProperty(unit, kAudioUnitProperty_ElementCount,
                                 kAudioUnitScope_Input, 0, &inputBuses, &busSize);
            for (UInt32 bus = 0; bus < inputBuses; ++bus)
            {
                const AURenderCallbackStruct callback = {
                    feedInput, bus == 0 ? &mainLevel : &sideLevel
                };
                const OSStatus connected =
                    AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                                         kAudioUnitScope_Input, bus,
                                         &callback, sizeof(callback));
                std::printf("input bus %u callback -> %d\n",
                            (unsigned) bus, (int) connected);
            }
        }

        status = AudioUnitInitialize(unit);
        std::printf("AudioUnitInitialize -> %d\n", (int) status);

        // The state to capture, before the view is built: the preset first, so the
        // single parameters can override it.
        if (!applyPresetFromEnvironment(unit) || !applyParametersFromEnvironment(unit))
            return 1;

        // --- the property a host reads to find the view -------------------------
        UInt32 dataSize = 0;
        Boolean writable = false;
        status = AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI,
                                          kAudioUnitScope_Global, 0, &dataSize, &writable);
        if (status != noErr || dataSize < sizeof(AudioUnitCocoaViewInfo))
        {
            std::fprintf(stderr, "FAIL: CocoaUI property info -> %d size=%u\n",
                         (int) status, (unsigned) dataSize);
            return 1;
        }

        AudioUnitCocoaViewInfo* info =
            static_cast<AudioUnitCocoaViewInfo*>(std::calloc(1, dataSize));
        status = AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI,
                                      kAudioUnitScope_Global, 0, info, &dataSize);
        if (status != noErr || dataSize < sizeof(AudioUnitCocoaViewInfo))
        {
            std::fprintf(stderr, "FAIL: CocoaUI property -> %d size=%u\n",
                         (int) status, (unsigned) dataSize);
            return 1;
        }

        const UInt32 classCount =
            1 + (dataSize - sizeof(AudioUnitCocoaViewInfo)) / sizeof(CFStringRef);
        NSURL* const bundleURL = (__bridge NSURL*) info->mCocoaAUViewBundleLocation;
        std::printf("view bundle: %s\n",
                    bundleURL ? [[bundleURL path] UTF8String] : "(null)");
        std::printf("view classes: %u\n", (unsigned) classCount);
        CFStringRef firstViewClass = nullptr;
        for (UInt32 i = 0; i < classCount; ++i)
        {
            if (info->mCocoaAUViewClass[i] == nullptr)
                continue;
            if (firstViewClass == nullptr)
                firstViewClass = info->mCocoaAUViewClass[i];
            std::printf("  [%u] %s\n", (unsigned) i,
                        [(__bridge NSString*) info->mCocoaAUViewClass[i] UTF8String]);
        }

        // --- load the bundle and build the view, exactly as a host does ---------
        if (bundleURL == nil)
        {
            std::fprintf(stderr, "FAIL: CocoaUI property returned a null view bundle URL\n");
            return 1;
        }
        NSBundle* const viewBundle = [NSBundle bundleWithURL:bundleURL];
        if (viewBundle == nil)
        {
            std::fprintf(stderr, "FAIL: NSBundle bundleWithURL returned nil\n");
            return 1;
        }
        if (![viewBundle load] && ![viewBundle isLoaded])
        {
            std::fprintf(stderr, "FAIL: view bundle would not load\n");
            return 1;
        }
        std::printf("view bundle loaded: %s\n", [[viewBundle bundlePath] UTF8String]);

        if (firstViewClass == nullptr)
        {
            std::fprintf(stderr, "FAIL: CocoaUI property returned no view classes\n");
            return 1;
        }
        NSString* const className = (__bridge NSString*) firstViewClass;
        Class factoryClass = [viewBundle classNamed:className];
        if (factoryClass == nil)
            factoryClass = NSClassFromString(className);
        if (factoryClass == nil)
        {
            std::fprintf(stderr, "FAIL: class %s not found in the loaded bundle\n",
                         [className UTF8String]);
            return 1;
        }
        std::printf("factory class resolved: %s\n", [className UTF8String]);

        id<AUCocoaUIBase> factory = [[factoryClass alloc] init];
        std::printf("factory instance: %p interfaceVersion=%u\n",
                    (void*) factory, (unsigned) [factory interfaceVersion]);

        std::printf("--- calling uiViewForAudioUnit ---\n");
        std::fflush(stdout);
        // In process DAF ignores the preferred size (its DAF_UI_DEFAULT_WIDTH and
        // HEIGHT win). Out of process Apple's remote container sizes the host window
        // to it, so it is the replayed host size when WxH is given, and none
        // otherwise, so that the size the view reports is its own.
        NSView* const view = [factory uiViewForAudioUnit:unit
                                                withSize:hostResize ? hostResizeSize
                                                                    : NSZeroSize];
        std::fflush(stdout);
        std::printf("--- returned view=%p ---\n", (void*) view);
        if (view == nil)
        {
            std::fprintf(stderr, "FAIL: uiViewForAudioUnit returned nil\n");
            return 1;
        }

        std::printf("view tree immediately after creation:\n");
        describeView(view, 1);

        // The window takes the size the view reports now, before any host resize,
        // unless argv[6] replays the resize a host performs at this point.
        NSSize hostSize = [view frame].size;
        if (hostResize)
        {
            hostSize = hostResizeSize;
            std::printf("host resize: setFrameSize %.0fx%.0f right after creation\n",
                        hostSize.width, hostSize.height);
            [view setFrameSize:hostSize];
            std::printf("view tree after host resize:\n");
            describeView(view, 1);
        }
        else if (!outOfProcess && (hostSize.width < 1.0 || hostSize.height < 1.0))
        {
            std::fprintf(stderr, "FAIL: the view reports no size of its own (%.1fx%.1f); "
                                 "pass WxH\n", hostSize.width, hostSize.height);
            return 1;
        }
        else
        {
            std::printf("window: the view's own size %.0fx%.0f\n",
                        hostSize.width, hostSize.height);
        }

        // --- put it in a window and let the run loop turn, as a host would ------
        NSWindow* const window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, hostSize.width, hostSize.height)
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        [window setContentView:view];

        // At its own size the window follows the view, as a host does: out of
        // process the remote view reports 1x1 when created and its real size later.
        id frameObserver = nil;
        if (!hostResize)
        {
            [view setPostsFrameChangedNotifications:YES];
            frameObserver = [[NSNotificationCenter defaultCenter]
                addObserverForName:NSViewFrameDidChangeNotification
                            object:view
                             queue:nil
                        usingBlock:^(NSNotification*) {
                const NSSize size = [view frame].size;
                if (!NSEqualSizes(size, [window contentRectForFrameRect:[window frame]].size))
                {
                    std::printf("window: follows the view to %.0fx%.0f\n",
                                size.width, size.height);
                    // Anchored at the origin like the window was created:
                    // setContentSize keeps the top-left fixed, which grows a window
                    // that starts at 1x1 down past the bottom of the screen.
                    [window setFrame:[window frameRectForContentRect:
                                         NSMakeRect(0, 0, size.width, size.height)]
                             display:YES];
                }
            }];
        }

        [window orderFront:nil];
        std::printf("WINDOWID: %ld\n", (long) [window windowNumber]);
        std::fflush(stdout);

        for (int tick = 0; tick < 120; ++tick)
        {
            NSEvent* event = nil;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES]) != nil)
                [NSApp sendEvent:event];
            [[NSRunLoop currentRunLoop]
                runMode:NSDefaultRunLoopMode
             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }

        // Drive audio through the plugin with the editor open, interleaved with the
        // run loop, which is the state a DAW actually keeps it in.
        if (doRender && !outOfProcess)
        {
            constexpr UInt32 frames = 64;
            float left[frames]{}, right[frames]{};
            struct { UInt32 count; AudioBuffer buffers[2]; } output = {
                2, {{1, sizeof(left), left}, {1, sizeof(right), right}}
            };
            AudioTimeStamp timestamp{};
            timestamp.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags renderFlags = 0;
            OSStatus worst = noErr;
            unsigned blocks = 0;

            for (int round = 0; round < 40; ++round)
            {
                for (int block = 0; block < 25; ++block)
                {
                    const OSStatus rendered = AudioUnitRender(
                        unit, &renderFlags, &timestamp, 0, frames,
                        reinterpret_cast<AudioBufferList*>(&output));
                    if (rendered != noErr && worst == noErr)
                        worst = rendered;
                    timestamp.mSampleTime += frames;
                    ++blocks;
                }
                [[NSRunLoop currentRunLoop]
                    runMode:NSDefaultRunLoopMode
                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            }
            std::printf("RENDER: %u blocks over %u input buses, worstStatus=%d, "
                        "lastSample=%.6f\n",
                        blocks, (unsigned) inputBuses, (int) worst,
                        (double) left[frames - 1]);
        }

        std::printf("view tree after ~2.4s of run loop:\n");
        describeView(view, 1);

        const NSRect finalFrame = [view frame];
        std::printf("RESULT: view=%.0fx%.0f subviews=%lu\n",
                    finalFrame.size.width, finalFrame.size.height,
                    (unsigned long) [[view subviews] count]);

        // --- does the GL surface actually paint anything? -----------------------
        // Read back from the plugin's GL front buffer. The surface is opaque, so its
        // RGB bytes are the ones the window server shows for the host: a
        // screencapture of the window holds the same bytes. The tags differ: the PNG
        // is device RGB, which ImageIO writes as sRGB, while the screencapture carries
        // the display's profile (Display P3 on a P3 display), so a colour-managed
        // viewer shows the PNG less saturated than the host window. A capture that
        // is uniformly one colour is the "black box" symptom; many distinct colours
        // is a drawn UI.
        NSView* const glView = findGLView(view);
        bool captureFailed = false;

        if (glView == nil)
        {
            std::printf("CAPTURE: no NSOpenGLView in the tree\n");
        }
        else
        {
            NSOpenGLContext* const glContext = [(NSOpenGLView*) glView openGLContext];
            std::printf("CAPTURE: gl view=%s context=%p\n",
                        [NSStringFromClass([glView class]) UTF8String], (void*) glContext);
            [glContext makeCurrentContext];

            const NSRect backing = [glView convertRectToBacking:[glView bounds]];
            const GLsizei w = (GLsizei) backing.size.width;
            const GLsizei h = (GLsizei) backing.size.height;
            const size_t total = (size_t) w * (size_t) h;
            unsigned char* pixels = static_cast<unsigned char*>(std::calloc(total, 4));
            if (pixels == nullptr)
            {
                std::fprintf(stderr, "FAIL: could not allocate %zu capture pixels\n", total);
                captureFailed = true;
            }
            else
            {
                glReadBuffer(GL_FRONT);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                const GLenum err = glGetError();

                // Distinct colours, quantised to 5 bits per channel, plus mean luma.
                // A drawn ImGui panel has hundreds; a blank surface has one or two.
                static unsigned char seen[32 * 32 * 32];
                std::memset(seen, 0, sizeof(seen));
                unsigned distinct = 0;
                double lumaSum = 0.0;
                for (size_t i = 0; i < total; ++i)
                {
                    const unsigned char* p = pixels + i * 4;
                    const unsigned key = ((p[0] >> 3) << 10)
                        | ((p[1] >> 3) << 5) | (p[2] >> 3);
                    if (!seen[key]) { seen[key] = 1; ++distinct; }
                    lumaSum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                }
                std::printf("CAPTURE: %dx%d glError=0x%04x distinctColours=%u meanLuma=%.2f\n",
                            (int) w, (int) h, (unsigned) err, distinct,
                            total ? lumaSum / (double) total : 0.0);

                // glReadPixels hands back rows bottom-to-top, while CGBitmapContext
                // reads them top-to-bottom, so the PNG came out vertically mirrored.
                // Flip here, after the order-independent colour statistics above.
                const size_t rowBytes = (size_t) w * 4;
                unsigned char* const rowSwap =
                    static_cast<unsigned char*>(std::malloc(rowBytes));
                if (rowSwap != nullptr)
                {
                    for (GLsizei row = 0; row < h / 2; ++row)
                    {
                        unsigned char* const top = pixels + (size_t) row * rowBytes;
                        unsigned char* const bottom =
                            pixels + (size_t) (h - 1 - row) * rowBytes;
                        std::memcpy(rowSwap, top, rowBytes);
                        std::memcpy(top, bottom, rowBytes);
                        std::memcpy(bottom, rowSwap, rowBytes);
                    }
                    std::free(rowSwap);
                }

                const char* captureDirectory = argc > 7 ? argv[7]
                    : std::getenv("DAF_PROBE_CAPTURE_DIR");
                NSString* const outputDirectory =
                    captureDirectory != nullptr && captureDirectory[0] != '\0'
                        ? [NSString stringWithUTF8String:captureDirectory]
                        : NSTemporaryDirectory();
                NSString* const out = [outputDirectory stringByAppendingPathComponent:
                    [NSString stringWithFormat:@"shot_%s.png", subCode]];
                [[NSFileManager defaultManager] removeItemAtPath:out error:nil];

                // The framebuffer alpha is not coverage (43% of Multi-Comp 2's face
                // reads below 255, down to 110): the GL surface is opaque and the host
                // shows its raw RGB. Read as premultiplied, those areas would be divided
                // by alpha towards white, so the alpha byte is skipped and the RGB
                // reaches the PNG unchanged.
                CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
                CGContextRef bmp = space != nullptr ? CGBitmapContextCreate(
                    pixels, (size_t) w, (size_t) h, 8, rowBytes, space,
                    kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big) : nullptr;
                CGImageRef image = bmp != nullptr ? CGBitmapContextCreateImage(bmp) : nullptr;
                CGImageDestinationRef dest = CGImageDestinationCreateWithURL(
                    (__bridge CFURLRef) [NSURL fileURLWithPath:out],
                    CFSTR("public.png"), 1, nullptr);
                if (image == nullptr)
                    std::fprintf(stderr, "FAIL: CGBitmapContextCreateImage failed\n");
                if (dest == nullptr)
                    std::fprintf(stderr,
                                 "FAIL: CGImageDestinationCreateWithURL failed for %s\n",
                                 [out UTF8String]);

                bool pngWritten = false;
                if (dest != nullptr && image != nullptr)
                {
                    CGImageDestinationAddImage(dest, image, nullptr);
                    if (!CGImageDestinationFinalize(dest))
                        std::fprintf(stderr,
                                     "FAIL: CGImageDestinationFinalize failed for %s\n",
                                     [out UTF8String]);
                    else if (![[NSFileManager defaultManager] fileExistsAtPath:out])
                        std::fprintf(stderr, "FAIL: no PNG was produced at %s\n",
                                     [out UTF8String]);
                    else
                    {
                        pngWritten = true;
                        std::printf("CAPTURE written: %s\n", [out UTF8String]);
                    }
                }
                if (!pngWritten)
                    captureFailed = true;
                if (dest) CFRelease(dest);
                if (image) CGImageRelease(image);
                if (bmp) CGContextRelease(bmp);
                if (space) CGColorSpaceRelease(space);
                std::free(pixels);
            }
            [NSOpenGLContext clearCurrentContext];
        }

        // Optional hold so an external screencapture can photograph the window,
        // which is the only way to see what a remote (out-of-process) view paints.
        const int holdSeconds = argc > 5 ? std::atoi(argv[5]) : 0;
        if (holdSeconds > 0)
        {
            std::printf("HOLDING %d s\n", holdSeconds);
            std::fflush(stdout);
            for (int tick = 0; tick < holdSeconds * 50; ++tick)
            {
                NSEvent* event = nil;
                while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                   untilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]
                                                      inMode:NSDefaultRunLoopMode
                                                     dequeue:YES]) != nil)
                    [NSApp sendEvent:event];
                [[NSRunLoop currentRunLoop]
                    runMode:NSDefaultRunLoopMode
                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.015]];
            }
        }

        if (frameObserver != nil)
            [[NSNotificationCenter defaultCenter] removeObserver:frameObserver];
        [window orderOut:nil];
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        std::printf("disposed cleanly\n");
        return captureFailed ? 1 : 0;
    }
}
