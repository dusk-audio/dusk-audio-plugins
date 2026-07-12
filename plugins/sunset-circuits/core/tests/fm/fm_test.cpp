// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// fm_test — standalone offline renderer for the Prism FM engine (FMEngine.hpp).
//
//   fm_test <algo> <midinote> <seconds> <out.wav> [key=value ...]
//
// Positional:
//   algo      0..7  (Prism algorithm index, see FMAlgorithms.hpp)
//   midinote  MIDI note to play (69 = A440)
//   seconds   render length
//   out.wav   output path (float32 MONO WAV)
//
// Special keys:
//   sr=<hz>            sample rate         (default 48000)
//   vel=<0..1>         note-on velocity    (default 1.0)
//   release=<sec>      call noteOff at time (default: never)
//   fb=<0..1>          op-4 feedback amount
//
// Per-operator keys — op<N><Param>, N = 1..4, Param in
//   Ratio Fine Level Vel KeyScale A D S R
// e.g.  op1Ratio=1 op1Level=1 op2Ratio=14 op2Level=0.8 op2D=0.15
//
// Defaults: every op ratio 1, fine 0, velSens 0, keyScale 0, ADSR
// {0.002,0.4,1,0.3}; op1 level 1 (a lone carrier), ops 2-4 level 0. So an
// unconfigured render on any single-carrier algorithm is a clean sine.

#include "FMEngine.hpp"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
void writeFloatWavMono(const char* path, const std::vector<float>& mono, int sampleRate)
{
    const uint32_t numFrames = (uint32_t)mono.size();
    const uint16_t channels  = 1;
    const uint16_t bits      = 32;
    const uint32_t byteRate  = (uint32_t)sampleRate * channels * (bits / 8);
    const uint16_t blockAlign = channels * (bits / 8);
    const uint32_t dataBytes = numFrames * blockAlign;

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(3 /* IEEE float */); u16(channels);
    u32((uint32_t)sampleRate); u32(byteRate); u16(blockAlign); u16(bits);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    std::fwrite(mono.data(), sizeof(float), mono.size(), f);
    std::fclose(f);
}

// Strict string->double: reject empty, trailing garbage, or non-finite results.
double parseNum(const char* key, const std::string& v)
{
    if (v.empty())
    {
        std::fprintf(stderr, "empty value for key '%s'\n", key);
        std::exit(2);
    }
    const char* start = v.c_str();
    char* end = nullptr;
    const double d = std::strtod(start, &end);
    if (end == start || *end != '\0' || !std::isfinite(d))
    {
        std::fprintf(stderr, "invalid numeric value for key '%s': %s\n", key, v.c_str());
        std::exit(2);
    }
    return d;
}

// Strict string->long: reject empty, trailing garbage, or out-of-range.
long parseInt(const char* key, const std::string& v)
{
    if (v.empty())
    {
        std::fprintf(stderr, "empty integer value for key '%s'\n", key);
        std::exit(2);
    }
    const char* start = v.c_str();
    char* end = nullptr;
    errno = 0;
    const long n = std::strtol(start, &end, 10);
    if (end == start || *end != '\0' || errno == ERANGE)
    {
        std::fprintf(stderr, "invalid integer value for key '%s': %s\n", key, v.c_str());
        std::exit(2);
    }
    return n;
}

struct OpCfg
{
    float ratio = 1.0f, fine = 0.0f, level = 0.0f, vel = 0.0f, key = 0.0f;
    float a = 0.002f, d = 0.4f, s = 1.0f, r = 0.3f;
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: fm_test <algo> <midinote> <seconds> <out.wav> [key=value ...]\n");
        return 1;
    }

    const long   algoL    = parseInt("algo", argv[1]);
    if (algoL < 0 || algoL > 7)
    {
        std::fprintf(stderr, "invalid algo: %ld (want 0..7)\n", algoL);
        return 1;
    }
    const int    algo     = (int)algoL;
    const long   midiL    = parseInt("midinote", argv[2]);
    if (midiL < 0 || midiL > 127)
    {
        std::fprintf(stderr, "invalid midinote: %ld (want 0..127)\n", midiL);
        return 1;
    }
    const int    midiNote = (int)midiL;
    const double seconds  = parseNum("seconds", argv[3]);
    const char*  outPath  = argv[4];

    double sampleRate  = 48000.0;
    float  velocity    = 1.0f;
    double releaseTime = -1.0;
    bool   releaseProvided = false;
    float  feedback    = 0.0f;

    OpCfg ops[4];
    ops[0].level = 1.0f;   // lone carrier by default

    for (int a = 5; a < argc; ++a)
    {
        std::string kv = argv[a];
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq);
        const double d = parseNum(key.c_str(), kv.substr(eq + 1)); // strict + finite
        const float v = (float)d;

        if (key == "sr")      { sampleRate  = d; continue; }
        if (key == "vel")
        {
            if (v < 0.0f || v > 1.0f) { std::fprintf(stderr, "invalid vel: %g (want 0..1)\n", (double)v); return 1; }
            velocity = v; continue;
        }
        if (key == "release") { releaseTime = d; releaseProvided = true; continue; }
        if (key == "fb")
        {
            if (v < 0.0f || v > 1.0f) { std::fprintf(stderr, "invalid fb: %g (want 0..1)\n", (double)v); return 1; }
            feedback = v; continue;
        }

        // op<N><Param>
        if (key.size() > 3 && key[0] == 'o' && key[1] == 'p' && std::isdigit((unsigned char)key[2]))
        {
            const int n = key[2] - '1';           // '1'..'4' -> 0..3
            const std::string p = key.substr(3);
            if (n < 0 || n > 3) { std::fprintf(stderr, "bad op index: %s\n", key.c_str()); return 1; }
            OpCfg& o = ops[n];
            // Ratio must be > 0; ADSR times must be >= 0. Fine/Level/Vel/KeyScale
            // are finite-only here (FMEngine setters clamp them engine-side).
            if      (p == "Ratio")    { if (!(v > 0.0f)) { std::fprintf(stderr, "invalid %s: %g (want > 0)\n", key.c_str(), (double)v); return 1; } o.ratio = v; }
            else if (p == "Fine")     o.fine  = v;
            else if (p == "Level")    o.level = v;
            else if (p == "Vel")      o.vel   = v;
            else if (p == "KeyScale") o.key   = v;
            else if (p == "A")        { if (v < 0.0f) { std::fprintf(stderr, "invalid %s: %g (want >= 0)\n", key.c_str(), (double)v); return 1; } o.a = v; }
            else if (p == "D")        { if (v < 0.0f) { std::fprintf(stderr, "invalid %s: %g (want >= 0)\n", key.c_str(), (double)v); return 1; } o.d = v; }
            else if (p == "S")        { if (v < 0.0f) { std::fprintf(stderr, "invalid %s: %g (want >= 0)\n", key.c_str(), (double)v); return 1; } o.s = v; }
            else if (p == "R")        { if (v < 0.0f) { std::fprintf(stderr, "invalid %s: %g (want >= 0)\n", key.c_str(), (double)v); return 1; } o.r = v; }
            else { std::fprintf(stderr, "unknown op param: %s\n", key.c_str()); return 1; }
            continue;
        }
        std::fprintf(stderr, "unknown key: %s\n", key.c_str());
        return 1;
    }

    // Validate the render extents now that key overrides (e.g. sr=) are applied.
    if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 3600.0)
    {
        std::fprintf(stderr, "invalid seconds: %g (want 0 < seconds <= 3600)\n", seconds);
        return 1;
    }
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0))
    {
        std::fprintf(stderr, "invalid sample rate: %g (want 8000..768000)\n", sampleRate);
        return 1;
    }
    // The engine rate, the frame math, and the (int) WAV header rate must agree
    // exactly, so require a whole number.
    if (sampleRate != std::floor(sampleRate))
    {
        std::fprintf(stderr, "invalid sample rate: %g (must be a whole number)\n", sampleRate);
        return 1;
    }
    // Release validation: NaN slips past the (releaseTime >= 0.0) gate below (NaN
    // comparisons are false), so reject non-finite explicitly; a finite release
    // point must fall within the render window.
    if (releaseProvided && !std::isfinite(releaseTime))
    {
        std::fprintf(stderr, "invalid release: %g (must be finite)\n", releaseTime);
        return 1;
    }
    // A negative release would otherwise fall through to releaseFrame = -1
    // ("never release") silently; reject it explicitly.
    if (releaseProvided && releaseTime < 0.0)
    {
        std::fprintf(stderr, "invalid release: %g (want >= 0)\n", releaseTime);
        return 1;
    }
    if (releaseProvided && releaseTime >= 0.0 && releaseTime > seconds)
    {
        std::fprintf(stderr, "invalid release: %g (must be <= seconds %g)\n", releaseTime, seconds);
        return 1;
    }

    const float freqHz = 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);

    msynth::FMVoiceEngine eng;
    eng.prepare(sampleRate);
    eng.setAlgorithm(algo);
    eng.setFeedback(feedback);
    for (int i = 0; i < 4; ++i)
    {
        eng.setOpRatio(i, ops[i].ratio);
        eng.setOpFine(i, ops[i].fine);
        eng.setOpLevel(i, ops[i].level);
        eng.setOpVelSens(i, ops[i].vel);
        eng.setOpKeyScale(i, ops[i].key);
        eng.setOpADSR(i, ops[i].a, ops[i].d, ops[i].s, ops[i].r);
    }

    eng.noteOn(freqHz, velocity);

    const double framesD = seconds * sampleRate;
    // Mono float RIFF limit: (UINT32_MAX-36)/4 frames.
    if (framesD > 1073741814.0)
    {
        std::fprintf(stderr, "render too long: %g frames exceeds RIFF WAV limit\n", framesD);
        return 1;
    }
    const int totalFrames  = (int)framesD;
    const int releaseFrame = releaseTime >= 0.0 ? (int)(releaseTime * sampleRate) : -1;

    std::vector<float> mono((size_t)totalFrames, 0.0f);
    bool released = false;
    for (int n = 0; n < totalFrames; ++n)
    {
        if (!released && releaseFrame >= 0 && n >= releaseFrame) { eng.noteOff(); released = true; }
        mono[(size_t)n] = eng.processSample();
    }

    writeFloatWavMono(outPath, mono, (int)sampleRate);
    std::fprintf(stderr, "wrote %s (%d frames @ %.0f Hz, algo=%d, f0=%.2f Hz)\n",
                 outPath, totalFrames, sampleRate, algo, freqHz);
    return 0;
}
