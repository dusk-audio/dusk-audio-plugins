// Standalone Apple AudioUnit renderer that loads a host AU + .aupreset
// directly via Apple's APIs. Bypasses JUCE because JUCE's
// setStateInformation does not correctly route to
// kAudioUnitProperty_ClassInfo for non-JUCE AUs (verified by dumping
// post-load param values, which were byte-identical to no-preset state
// even after passing the binary plist of the .aupreset).
//
// Usage:
//   au_native_render <au_subtype> <au_manufacturer> <preset_path> <output_dir> <slug>
// where subtype/manufacturer are 4-char strings (e.g. RVLX AUTR for Arturia).
//
// Renders the same noise-burst signal as duskverb_render so spectral
// analysis is directly comparable.

#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AudioUnit.h>
#import <CoreFoundation/CoreFoundation.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr UInt32 kBlockSize = 256;
constexpr UInt32 kTotalSamples = 6 * 48000;   // 6 seconds

OSStatus checkErr (OSStatus s, const char* what)
{
    if (s != noErr)
        std::fprintf (stderr, "ERROR (%s): osstatus=%d ('%c%c%c%c')\n", what, (int)s,
                      (char)(s>>24)&0xff, (char)(s>>16)&0xff, (char)(s>>8)&0xff, (char)s&0xff);
    return s;
}

UInt32 fourcc (const char* s)
{
    return ((UInt32)s[0] << 24) | ((UInt32)s[1] << 16) | ((UInt32)s[2] << 8) | (UInt32)s[3];
}

// Build the noise-burst signal — MUST match duskverb_render's fillNoiseBurst
// exactly (same pink-noise generator, same per-channel seeds) so the
// Arturia A/B comparison sees the identical input that DuskVerb sees.
// DV uses Voss-McCartney pink noise via JUCE's xorshift Random with seeds
// 0xC0FFEE / 0xBADBEEF — we reimplement both in pure C here so this tool
// doesn't have to link JUCE.
namespace {
    // JUCE's juce::Random uses a 64-bit linear congruential generator
    // (state = state * 0x5DEECE66Dull + 0xB; return high 32 bits).
    struct JuceRandomLike {
        uint64_t state;
        explicit JuceRandomLike (int64_t seed) : state ((uint64_t) seed) {}
        uint32_t nextInt() {
            state = state * 0x5DEECE66DULL + 0xBULL;
            return (uint32_t) (state >> 32);
        }
        float nextFloat() {
            return (float) nextInt() / (float) 0x100000000ULL;   // [0, 1)
        }
    };
}

void fillNoiseBurst (std::vector<float>& bufL, std::vector<float>& bufR)
{
    const UInt32 burstSamples = (UInt32)(0.1 * kSampleRate);
    JuceRandomLike rngL (0xC0FFEE), rngR (0xBADBEEF);
    float bL[3] = {}, bR[3] = {};
    for (UInt32 n = 0; n < kTotalSamples; ++n)
    {
        if (n >= burstSamples)
        {
            bufL[n] = 0.0f;
            bufR[n] = 0.0f;
            continue;
        }
        const float wL = rngL.nextFloat() * 2.0f - 1.0f;
        const float wR = rngR.nextFloat() * 2.0f - 1.0f;
        bL[0] = 0.99765f * bL[0] + wL * 0.0990460f;
        bL[1] = 0.96300f * bL[1] + wL * 0.2965164f;
        bL[2] = 0.57000f * bL[2] + wL * 1.0526913f;
        bR[0] = 0.99765f * bR[0] + wR * 0.0990460f;
        bR[1] = 0.96300f * bR[1] + wR * 0.2965164f;
        bR[2] = 0.57000f * bR[2] + wR * 1.0526913f;
        bufL[n] = 0.1f * (bL[0] + bL[1] + bL[2]);
        bufR[n] = 0.1f * (bR[0] + bR[1] + bR[2]);
    }
}

// Render-callback context that feeds the AU pre-recorded input.
struct RenderCtx
{
    const float* inL;
    const float* inR;
    UInt32 totalFrames;
    UInt32 readPos;
};

OSStatus renderInputCallback (void* refCon, AudioUnitRenderActionFlags* /*ioActionFlags*/,
                              const AudioTimeStamp* /*inTimeStamp*/, UInt32 /*inBusNumber*/,
                              UInt32 inNumberFrames, AudioBufferList* ioData)
{
    auto* c = (RenderCtx*) refCon;
    auto* outL = (float*) ioData->mBuffers[0].mData;
    auto* outR = (ioData->mNumberBuffers >= 2) ? (float*) ioData->mBuffers[1].mData : outL;
    for (UInt32 i = 0; i < inNumberFrames; ++i)
    {
        const UInt32 p = c->readPos + i;
        const float l = (p < c->totalFrames) ? c->inL[p] : 0.0f;
        const float r = (p < c->totalFrames) ? c->inR[p] : 0.0f;
        outL[i] = l;
        if (outR != outL) outR[i] = r;
    }
    c->readPos += inNumberFrames;
    return noErr;
}

// Write a stereo float buffer to a 16-bit PCM WAV using ExtAudioFile.
// 16-bit PCM is universally supported and has zero ambiguity in the file
// format spec; ExtAudioFile handles the float→int16 conversion via the
// client/file format split.
bool writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, UInt32 numFrames)
{
    // Client format = interleaved 32-bit float (what we hand to ExtAudioFile)
    AudioStreamBasicDescription clientFmt{};
    clientFmt.mSampleRate       = kSampleRate;
    clientFmt.mFormatID         = kAudioFormatLinearPCM;
    clientFmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    clientFmt.mChannelsPerFrame = 2;
    clientFmt.mBitsPerChannel   = 32;
    clientFmt.mBytesPerFrame    = 8;        // interleaved stereo, 4 bytes/ch
    clientFmt.mFramesPerPacket  = 1;
    clientFmt.mBytesPerPacket   = 8;

    // File format = interleaved 16-bit signed PCM, little-endian
    AudioStreamBasicDescription fileFmt{};
    fileFmt.mSampleRate       = kSampleRate;
    fileFmt.mFormatID         = kAudioFormatLinearPCM;
    fileFmt.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fileFmt.mChannelsPerFrame = 2;
    fileFmt.mBitsPerChannel   = 16;
    fileFmt.mBytesPerFrame    = 4;          // interleaved stereo, 2 bytes/ch
    fileFmt.mFramesPerPacket  = 1;
    fileFmt.mBytesPerPacket   = 4;

    CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path.c_str(), (CFIndex) path.size(), false);
    ExtAudioFileRef file = nullptr;
    OSStatus s = ExtAudioFileCreateWithURL (url, kAudioFileWAVEType, &fileFmt, nullptr, kAudioFileFlags_EraseFile, &file);
    CFRelease (url);
    if (checkErr (s, "ExtAudioFileCreateWithURL")) return false;

    s = ExtAudioFileSetProperty (file, kExtAudioFileProperty_ClientDataFormat, sizeof (clientFmt), &clientFmt);
    if (checkErr (s, "set ClientDataFormat")) { ExtAudioFileDispose (file); return false; }

    // Interleave L/R into a single contiguous buffer.
    std::vector<float> interleaved (numFrames * 2);
    for (UInt32 i = 0; i < numFrames; ++i)
    {
        interleaved[2 * i + 0] = L[i];
        interleaved[2 * i + 1] = R[i];
    }
    AudioBufferList bl;
    bl.mNumberBuffers = 1;
    bl.mBuffers[0].mNumberChannels = 2;
    bl.mBuffers[0].mDataByteSize   = numFrames * 8;
    bl.mBuffers[0].mData           = interleaved.data();

    s = ExtAudioFileWrite (file, numFrames, &bl);
    if (checkErr (s, "ExtAudioFileWrite")) { ExtAudioFileDispose (file); return false; }
    ExtAudioFileDispose (file);
    return true;
}

CFPropertyListRef loadPlist (const char* path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path, (CFIndex) std::strlen (path), false);
    CFReadStreamRef stream = CFReadStreamCreateWithFile (nullptr, url);
    CFRelease (url);
    if (! CFReadStreamOpen (stream)) { CFRelease (stream); return nullptr; }
    CFErrorRef err = nullptr;
    CFPropertyListRef plist = CFPropertyListCreateWithStream (nullptr, stream, 0, kCFPropertyListImmutable, nullptr, &err);
    CFReadStreamClose (stream);
    CFRelease (stream);
    if (! plist && err) CFRelease (err);
    return plist;
}

bool renderThroughAU (AudioUnit au,
                      const std::vector<float>& inL, const std::vector<float>& inR,
                      std::vector<float>& outL, std::vector<float>& outR)
{
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = kSampleRate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    = 4;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerPacket   = 4;

    // Set on input bus 0 AND output bus 0
    OSStatus s;
    s = AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,  0, &fmt, sizeof (fmt));
    if (checkErr (s, "set in StreamFormat")) return false;
    s = AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof (fmt));
    if (checkErr (s, "set out StreamFormat")) return false;

    // Set max frames per render
    UInt32 maxFr = kBlockSize;
    s = AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFr, sizeof (maxFr));
    if (checkErr (s, "set MaxFramesPerSlice")) return false;

    // Hook up render callback for input
    RenderCtx ctx { inL.data(), inR.data(), kTotalSamples, 0 };
    AURenderCallbackStruct cb { renderInputCallback, &ctx };
    s = AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof (cb));
    if (checkErr (s, "set RenderCallback")) return false;

    s = AudioUnitInitialize (au);
    if (checkErr (s, "AudioUnitInitialize")) return false;

    AudioBufferList* abl = (AudioBufferList*) malloc (sizeof (AudioBufferList) + sizeof (AudioBuffer));
    if (abl == nullptr)
    {
        std::fprintf (stderr, "ERROR: malloc failed for AudioBufferList\n");
        AudioUnitUninitialize (au);
        return false;
    }
    abl->mNumberBuffers = 2;
    std::vector<float> bufL (kBlockSize), bufR (kBlockSize);
    abl->mBuffers[0].mNumberChannels = 1;
    abl->mBuffers[0].mDataByteSize   = kBlockSize * 4;
    abl->mBuffers[0].mData           = bufL.data();
    abl->mBuffers[1].mNumberChannels = 1;
    abl->mBuffers[1].mDataByteSize   = kBlockSize * 4;
    abl->mBuffers[1].mData           = bufR.data();

    AudioTimeStamp ts{}; ts.mFlags = kAudioTimeStampSampleTimeValid;

    for (UInt32 pos = 0; pos < kTotalSamples; pos += kBlockSize)
    {
        const UInt32 nFrames = std::min (kBlockSize, kTotalSamples - pos);
        ts.mSampleTime = (Float64) pos;
        AudioUnitRenderActionFlags flags = 0;
        s = AudioUnitRender (au, &flags, &ts, 0, nFrames, abl);
        if (checkErr (s, "AudioUnitRender")) { free (abl); return false; }
        for (UInt32 i = 0; i < nFrames; ++i)
        {
            outL[pos + i] = bufL[i];
            outR[pos + i] = bufR[i];
        }
    }
    free (abl);
    AudioUnitUninitialize (au);
    return true;
}

} // namespace

int main (int argc, const char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "Usage: au_native_render <subtype:4chr> <manufacturer:4chr> <preset_path> <output_dir> <slug>\n"
                              "                       [--set <id>=<norm> ...]   (skips setStateInformation if any --set is given)\n");
        return 1;
    }
    const char* subtypeStr      = argv[1];
    const char* manufacturerStr = argv[2];
    const char* presetPath      = argv[3];
    const char* outputDir       = argv[4];
    const char* slug            = argv[5];

    // Optional per-param overrides via --set <id>=<norm>. When ANY override
    // is given, we skip the .aupreset's setStateInformation (because Arturia
    // silently ignores it — verified by post-load param dump showing every
    // value at default after the supposed load). The overrides are applied
    // via AudioUnitSetParameter() on each AU param ID directly, which DOES
    // take effect.
    struct ParamOverride { AudioUnitParameterID id; AudioUnitParameterValue val; };
    std::vector<ParamOverride> overrides;
    for (int i = 6; i + 1 < argc; ++i)
    {
        if (std::strcmp (argv[i], "--set") == 0)
        {
            unsigned id = 0;
            float v = 0;
            if (std::sscanf (argv[i + 1], "%u=%f", &id, &v) == 2)
                overrides.push_back ({ (AudioUnitParameterID) id, (AudioUnitParameterValue) v });
            ++i;
        }
    }

    if (std::strlen (subtypeStr) != 4 || std::strlen (manufacturerStr) != 4)
    {
        std::fprintf (stderr, "subtype and manufacturer must be 4-char codes\n");
        return 1;
    }

    // Try 'aufx' (Audio Effect) first, then 'aumf' (Music Effect).
    // Arturia Rev LX-24 registers as aumf even though it's a reverb.
    AudioComponent comp = nullptr;
    for (const char* typeStr : { "aufx", "aumf", "aumu", "augn" })
    {
        AudioComponentDescription desc{};
        desc.componentType         = fourcc (typeStr);
        desc.componentSubType      = fourcc (subtypeStr);
        desc.componentManufacturer = fourcc (manufacturerStr);
        comp = AudioComponentFindNext (nullptr, &desc);
        if (comp) { std::printf ("Found AU type=%s\n", typeStr); break; }
    }
    if (! comp)
    {
        std::fprintf (stderr, "AudioComponent not found for subtype=%s manufacturer=%s (tried aufx/aumf/aumu/augn)\n", subtypeStr, manufacturerStr);
        return 1;
    }

    AudioUnit au = nullptr;
    OSStatus s = AudioComponentInstanceNew (comp, &au);
    if (checkErr (s, "AudioComponentInstanceNew")) return 1;
    std::printf ("Loaded AU: subtype=%s manufacturer=%s\n", subtypeStr, manufacturerStr);

    // Load the .aupreset as a CFPropertyList and apply via kAudioUnitProperty_ClassInfo.
    CFPropertyListRef plist = loadPlist (presetPath);
    if (! plist)
    {
        std::fprintf (stderr, "Failed to load .aupreset as plist: %s\n", presetPath);
        return 1;
    }
    if (overrides.empty())
    {
        s = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof (plist));
        CFRelease (plist);
        if (checkErr (s, "set ClassInfo")) return 1;
        std::printf ("Applied preset: %s\n", presetPath);
    }
    else
    {
        CFRelease (plist);
        std::printf ("Skipping setStateInformation (Arturia ignores it). Applying %zu manual overrides:\n", overrides.size());
        for (const auto& o : overrides)
        {
            OSStatus os = AudioUnitSetParameter (au, o.id, kAudioUnitScope_Global, 0, o.val, 0);
            std::printf ("  AudioUnitSetParameter id=%u val=%.6f → %s\n",
                         (unsigned) o.id, o.val,
                         os == noErr ? "OK" : "FAILED");
        }
    }

    // Dump all parameter values AFTER preset load — use cfNameString
    // (CFStringRef) which JUCE-wrapped AUs populate, since the C-string
    // info.name field is left blank.
    {
        UInt32 paramListSize = 0;
        Boolean writable = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList,
                                  kAudioUnitScope_Global, 0, &paramListSize, &writable);
        const UInt32 numParams = paramListSize / sizeof (AudioUnitParameterID);
        std::vector<AudioUnitParameterID> ids (numParams);
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList,
                              kAudioUnitScope_Global, 0, ids.data(), &paramListSize);
        std::printf ("--- post-load AU param values (%u total) ---\n", numParams);
        for (UInt32 i = 0; i < numParams; ++i)
        {
            AudioUnitParameterInfo info{};
            UInt32 infoSize = sizeof (info);
            AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Global, ids[i], &info, &infoSize);
            AudioUnitParameterValue v = 0;
            AudioUnitGetParameter (au, ids[i], kAudioUnitScope_Global, 0, &v);
            char nameBuf[128] = {0};
            if (info.flags & kAudioUnitParameterFlag_HasCFNameString && info.cfNameString != nullptr)
            {
                CFStringGetCString (info.cfNameString, nameBuf, sizeof (nameBuf), kCFStringEncodingUTF8);
                CFRelease (info.cfNameString);
            }
            else
            {
                strncpy (nameBuf, info.name, sizeof (nameBuf) - 1);
            }
            std::printf ("  [id=%3u] %-32s = %.6f  (range %.3f..%.3f)\n",
                         (unsigned) ids[i], nameBuf, v, info.minValue, info.maxValue);
        }
    }

    // Render
    std::vector<float> inL (kTotalSamples), inR (kTotalSamples);
    std::vector<float> outL (kTotalSamples, 0.0f), outR (kTotalSamples, 0.0f);
    fillNoiseBurst (inL, inR);
    if (! renderThroughAU (au, inL, inR, outL, outR))
    {
        AudioComponentInstanceDispose (au);
        return 1;
    }

    const std::string outPath = std::string (outputDir) + "/" + slug + "_noiseburst.wav";
    if (! writeWav (outPath, outL, outR, kTotalSamples))
    {
        AudioComponentInstanceDispose (au);
        return 1;
    }
    std::printf ("Wrote %s\n", outPath.c_str());

    AudioComponentInstanceDispose (au);
    return 0;
}
