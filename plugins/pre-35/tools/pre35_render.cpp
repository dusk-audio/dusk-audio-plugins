// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// pre35_render — offline renderer for the PRE-35 core. WAV in, WAV out, no host,
// no plugin format, no audio device.
//
// This exists for exactly one job: the NULL TEST. The Python reference renderer
// (dusk-audio-tools tools/m35/fit/render_ref.py) is the spec for the whole
// model; this binary is the only way to put the C++ port and that spec through
// the same file and subtract them. Anything the two disagree about is a port bug
// until proven otherwise — the reference does not get "corrected" to match C++.
//
//   pre35_render <in.wav> <out.wav> [key=value ...]
//
//     pad=<0|20|40>     pad switch, by its silkscreened label (default 0)
//     trim=<0..100>     trim knob position in percent (default 0)
//     iron=<0..2>       transformer layer amount, 1 = the measured device
//     noise=<0|1>       add the modelled input-referred floor (default 0)
//     autogain=<0|1>    cancel the modelled taper+pad gain (default 0)
//     outdb=<dB>        output gain (default 0)
//     os=<1..8>         oversampling factor (default 8, matching render_ref)
//     block=<frames>    host block size (default 512). The render must not
//                       depend on it; that is a gate, not a convenience.
//     align=<0|1>       compensate the core's reported latency so the output is
//                       sample-aligned with the input, the way resample_poly's
//                       zero-delay convention leaves the reference (default 1)
//     seed=<n>          noise seed
//
// Output is always 32-bit float WAV at the input's rate and channel count. Each
// channel gets its own Pre35DSP: the M-35 is a mono channel strip, and running
// one instance across an interleaved buffer would cross-contaminate the envelope
// detectors.

#include "Pre35DSP.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

[[noreturn]] void fail(const std::string& msg)
{
    std::fprintf(stderr, "pre35_render: %s\n", msg.c_str());
    std::exit(2);
}

//==============================================================================
// Minimal WAV I/O. Reads PCM 16/24/32 and IEEE float 32/64, including
// WAVE_FORMAT_EXTENSIBLE; writes 32-bit float.

struct Audio
{
    std::vector<std::vector<float>> channels;
    int sampleRate = 0;

    size_t frames() const { return channels.empty() ? 0 : channels[0].size(); }
};

uint32_t rdU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rdU16(const uint8_t* p) { return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)); }

Audio readWav(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (! f)
        fail("cannot open " + path);

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 44)
        fail(path + " is too short to be a WAV");

    std::vector<uint8_t> raw((size_t)size);
    if (std::fread(raw.data(), 1, raw.size(), f) != raw.size())
        fail("short read on " + path);
    std::fclose(f);

    if (std::memcmp(raw.data(), "RIFF", 4) != 0 || std::memcmp(raw.data() + 8, "WAVE", 4) != 0)
        fail(path + " is not a RIFF/WAVE file");

    uint16_t format = 0, numChannels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;

    size_t pos = 12;
    while (pos + 8 <= raw.size())
    {
        const char* id = (const char*)raw.data() + pos;
        const uint32_t sz = rdU32(raw.data() + pos + 4);
        const size_t body = pos + 8;
        if (body + sz > raw.size() && std::memcmp(id, "data", 4) != 0)
            break;

        if (std::memcmp(id, "fmt ", 4) == 0 && sz >= 16)
        {
            format      = rdU16(raw.data() + body);
            numChannels = rdU16(raw.data() + body + 2);
            rate        = rdU32(raw.data() + body + 4);
            bits        = rdU16(raw.data() + body + 14);
            if (format == 0xFFFE && sz >= 40)
                format = rdU16(raw.data() + body + 24);   // extensible sub-format
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            data = raw.data() + body;
            dataBytes = std::min<size_t>(sz, raw.size() - body);
        }

        pos = body + sz + (sz & 1u);
    }

    if (data == nullptr || numChannels == 0 || bits == 0)
        fail(path + " has no usable fmt/data chunk");
    if (format != 1 && format != 3)
        fail(path + " uses an unsupported WAV encoding (only PCM and IEEE float)");

    const size_t bytesPerSample = bits / 8u;
    const size_t frameBytes = bytesPerSample * numChannels;
    const size_t numFrames = frameBytes ? dataBytes / frameBytes : 0;

    Audio a;
    a.sampleRate = (int)rate;
    a.channels.assign(numChannels, std::vector<float>(numFrames, 0.0f));

    for (size_t i = 0; i < numFrames; ++i)
        for (uint16_t c = 0; c < numChannels; ++c)
        {
            const uint8_t* s = data + (i * numChannels + c) * bytesPerSample;
            double v = 0.0;
            if (format == 3 && bits == 32)      { float t;  std::memcpy(&t, s, 4); v = t; }
            else if (format == 3 && bits == 64) { double t; std::memcpy(&t, s, 8); v = t; }
            else if (bits == 16)                { v = (double)(int16_t)rdU16(s) / 32768.0; }
            else if (bits == 24)
            {
                int32_t t = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24);
                v = (double)(t >> 8) / 8388608.0;
            }
            else if (bits == 32)                { v = (double)(int32_t)rdU32(s) / 2147483648.0; }
            else
                fail(path + " has an unsupported bit depth");
            a.channels[c][i] = (float)v;
        }

    return a;
}

void writeWavFloat(const std::string& path, const Audio& a)
{
    const uint16_t channels = (uint16_t)a.channels.size();
    const uint32_t numFrames = (uint32_t)a.frames();
    const uint16_t blockAlign = (uint16_t)(channels * 4u);
    const uint64_t dataBytes = (uint64_t)numFrames * blockAlign;
    if (dataBytes + 36u > 0xFFFFFFFFull)
        fail("render exceeds the RIFF WAV size limit: " + path);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (! f)
        fail("cannot open " + path + " for writing");

    auto w = [&](const void* p, size_t sz, size_t n) {
        if (std::fwrite(p, sz, n, f) != n)
            fail("short write to " + path);   // disk full must fail, not truncate
    };
    auto u32 = [&](uint32_t v) { w(&v, 4, 1); };
    auto u16 = [&](uint16_t v) { w(&v, 2, 1); };

    w("RIFF", 1, 4); u32((uint32_t)(36u + dataBytes)); w("WAVE", 1, 4);
    w("fmt ", 1, 4); u32(16); u16(3); u16(channels);
    u32((uint32_t)a.sampleRate); u32((uint32_t)a.sampleRate * blockAlign); u16(blockAlign); u16(32);
    w("data", 1, 4); u32((uint32_t)dataBytes);

    std::vector<float> interleaved((size_t)numFrames * channels);
    for (uint32_t i = 0; i < numFrames; ++i)
        for (uint16_t c = 0; c < channels; ++c)
            interleaved[(size_t)i * channels + c] = a.channels[c][i];
    if (numFrames)
        w(interleaved.data(), sizeof(float), interleaved.size());

    std::fclose(f);
}

//==============================================================================
double parseDouble(const std::string& key, const std::string& v)
{
    errno = 0;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0' || errno == ERANGE || ! std::isfinite(d))
        fail("bad value for " + key + "=" + v);
    return d;
}

/** Integers are parsed as integers, not as doubles that are then cast: casting an
    out-of-range double to an integral type is undefined behaviour, and "seed=1e30"
    is exactly the sort of thing a script generates by accident. */
long parseLong(const std::string& key, const std::string& v, long lo, long hi)
{
    errno = 0;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0' || errno == ERANGE || n < lo || n > hi)
        fail("bad value for " + key + "=" + v + " (expected an integer in "
             + std::to_string(lo) + ".." + std::to_string(hi) + ")");
    return n;
}

uint64_t parseUint64(const std::string& key, const std::string& v)
{
    errno = 0;
    char* end = nullptr;
    // strtoull silently wraps a leading '-'; reject it before it becomes 2^64-n.
    if (v.empty() || v[0] == '-')
        fail("bad value for " + key + "=" + v + " (expected a non-negative integer)");
    const unsigned long long n = std::strtoull(v.c_str(), &end, 0);
    if (end == v.c_str() || *end != '\0' || errno == ERANGE)
        fail("bad value for " + key + "=" + v + " (expected a non-negative integer)");
    return (uint64_t)n;
}

int padIndexForLabel(int label)
{
    for (int i = 0; i < pre35::coeffs::kNumPads; ++i)
        if (pre35::coeffs::kPads[i].labelDb == label)
            return i;
    fail("unknown pad " + std::to_string(label) + " (expected 0, 20 or 40)");
}

} // namespace

//==============================================================================
int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr,
            "usage: pre35_render <in.wav> <out.wav> [key=value ...]\n"
            "  pad=0|20|40  trim=0..100  iron=0..2  noise=0|1  autogain=0|1\n"
            "  outdb=<dB>   os=1..8      block=<frames>  align=0|1  seed=<n>\n");
        return 2;
    }

    const std::string inPath = argv[1];
    const std::string outPath = argv[2];

    int      padLabel = 0;
    double   trim = 0.0, iron = 1.0, outDb = 0.0;
    bool     noise = false, autoGain = false, align = true;
    int      osFactor = 8, block = 512;
    uint64_t seed = 0x50524533ull;

    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos)
            fail("expected key=value, got " + arg);
        const std::string k = arg.substr(0, eq), v = arg.substr(eq + 1);

        if      (k == "pad")      padLabel = (int)parseLong(k, v, -100, 100);
        else if (k == "trim")     trim = parseDouble(k, v);
        else if (k == "iron")     iron = parseDouble(k, v);
        else if (k == "outdb")    outDb = parseDouble(k, v);
        else if (k == "noise")    noise = parseLong(k, v, 0, 1) != 0;
        else if (k == "autogain") autoGain = parseLong(k, v, 0, 1) != 0;
        else if (k == "align")    align = parseLong(k, v, 0, 1) != 0;
        else if (k == "os")       osFactor = (int)parseLong(k, v, 1, pre35::kMaxOversampleFactor);
        else if (k == "block")    block = (int)parseLong(k, v, 1, 65536);
        else if (k == "seed")     seed = parseUint64(k, v);
        else fail("unknown key " + k);
    }

    const int padIndex = padIndexForLabel(padLabel);

    Audio in = readWav(inPath);
    if (in.sampleRate <= 0)
        fail(inPath + " has no sample rate");

    Audio out;
    out.sampleRate = in.sampleRate;
    out.channels.assign(in.channels.size(), std::vector<float>(in.frames(), 0.0f));

    int latency = 0;

    for (size_t c = 0; c < in.channels.size(); ++c)
    {
        pre35::Pre35DSP dsp;
        dsp.setNoiseSeed(seed + (uint64_t)c * 0x9E3779B97F4A7C15ull);
        dsp.setPadIndex(padIndex);
        dsp.setTrimPercent(trim);
        dsp.setIronAmount(iron);
        dsp.setNoiseEnabled(noise);
        dsp.setAutoGain(autoGain);
        dsp.setOutputGainDb(outDb);
        dsp.prepare((double)in.sampleRate, osFactor);
        latency = dsp.latencySamples();

        // Feed `latency` extra zeros so the aligned window is complete, then drop
        // the leading `latency` samples. Anything else compares the C++ against a
        // shifted copy of the Python and calls the delay a response error.
        const size_t pad = align ? (size_t)latency : 0;
        std::vector<float> work(in.frames() + pad, 0.0f);
        std::copy(in.channels[c].begin(), in.channels[c].end(), work.begin());

        for (size_t i = 0; i < work.size(); i += (size_t)block)
            dsp.process(work.data() + i, (int)std::min((size_t)block, work.size() - i));

        std::copy(work.begin() + (long)pad, work.end(), out.channels[c].begin());
    }

    writeWavFloat(outPath, out);

    double peak = 0.0, sumSq = 0.0;
    for (const auto& ch : out.channels)
        for (float v : ch)
        {
            peak = std::max(peak, (double)std::fabs(v));
            sumSq += (double)v * (double)v;
        }
    const size_t total = out.frames() * out.channels.size();
    const double rms = total ? std::sqrt(sumSq / (double)total) : 0.0;
    const double calDb = pre35::gainCalDb(trim, padIndex);

    std::printf("wrote %s  %zu x %zu @ %d Hz\n", outPath.c_str(),
                out.frames(), out.channels.size(), out.sampleRate);
    std::printf("  pad %d  trim %g %%  iron %g  noise %d  autogain %d  outdb %g\n",
                padLabel, trim, iron, (int)noise, (int)autoGain, outDb);
    std::printf("  os %dx  block %d  latency %d smp  aligned %d\n",
                osFactor, block, latency, (int)align);
    std::printf("  model cal gain %.4f dB   peak %.3f dBFS   rms %.3f dBFS\n",
                calDb,
                peak > 0.0 ? 20.0 * std::log10(peak) : -240.0,
                rms  > 0.0 ? 20.0 * std::log10(rms)  : -240.0);
    return 0;
}
