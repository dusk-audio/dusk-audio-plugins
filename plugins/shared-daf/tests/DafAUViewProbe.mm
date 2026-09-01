// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Headless AU Cocoa-view probe for any installed DAF plugin. It instantiates the
// view the way a host does, which is the step auval never performs: auval only
// enumerates the view class, so a plugin whose view fails to build still passes.
//
// It reports the view tree at each stage and, for in-process hosting, reads the
// plugin's own GL front buffer back and writes it to a PNG. A drawn ImGui panel
// yields hundreds of distinct colours; a blank surface yields one or two, which
// is the "black box in the host" symptom expressed as a number.
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
//                creating the view, which is how Logic restores a saved size
//   [7]          directory for shot_<subtype>.png (default DAF_PROBE_CAPTURE_DIR,
//                then the system temporary directory)

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
#include <algorithm>

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
        NSView* const view = [factory uiViewForAudioUnit:unit
                                                withSize:NSMakeSize(1120, 380)];
        std::fflush(stdout);
        std::printf("--- returned view=%p ---\n", (void*) view);
        if (view == nil)
        {
            std::fprintf(stderr, "FAIL: uiViewForAudioUnit returned nil\n");
            return 1;
        }

        std::printf("view tree immediately after creation:\n");
        describeView(view, 1);

        // Logic resizes the view it was handed straight after creating it, to the
        // plugin window size saved with the session. argv[6] = "WxH" replays that.
        NSSize hostSize = NSMakeSize(1120, 380);
        if (argc > 6)
        {
            int hw = 0, hh = 0;
            if (std::sscanf(argv[6], "%dx%d", &hw, &hh) == 2 && hw > 0 && hh > 0)
            {
                hostSize = NSMakeSize(hw, hh);
                std::printf("host resize: setFrameSize %dx%d right after creation\n", hw, hh);
                [view setFrameSize:hostSize];
                std::printf("view tree after host resize:\n");
                describeView(view, 1);
            }
        }

        // --- put it in a window and let the run loop turn, as a host would ------
        NSWindow* const window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, hostSize.width, hostSize.height)
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        [window setContentView:view];
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
        // Captured through the window server, so this sees exactly what a host
        // would composite. A capture that is uniformly one colour is the "black
        // box" symptom; a capture with many distinct colours is a drawn UI.
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

                CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
                CGContextRef bmp = space != nullptr ? CGBitmapContextCreate(
                    pixels, (size_t) w, (size_t) h, 8, rowBytes, space,
                    kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big) : nullptr;
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

        [window orderOut:nil];
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        std::printf("disposed cleanly\n");
        return captureFailed ? 1 : 0;
    }
}
