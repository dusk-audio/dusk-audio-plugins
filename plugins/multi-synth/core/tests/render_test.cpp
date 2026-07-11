// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// render_test — offline renderer for the Multi-Synth core validation harness.
//
//   render_test <mode> <midinote> <seconds> <osfactor> <out.wav> [key=value ...]
//
// Positional:
//   mode      0..5   (Cosmos/Oracle/Mono/Modular/Prism/Acid)
//   midinote  MIDI note number to play (e.g. 69 = A440)
//   seconds   render length
//   osfactor  1 | 2 | 4   (oversampling)
//   out.wav   output path (float32 stereo WAV)
//
// key=value overrides any engine parameter by name (see paramIndexForName), plus
// these special keys:
//   vel=<0..1>          note-on velocity (default 1.0)
//   release=<sec>       call noteOff at this time (default: no release)
//   tempo=<bpm>         host tempo (default 120)
//   playing=<0|1>       transport playing flag (default 1)
//   songpos=<beats>     host song position at frame 0, in beats. When set, the
//                       harness acts as the DAW host: it calls setSongPosition
//                       before each processBlock so the arp/acid step clock
//                       phase-locks to the host grid. Unset = free-run (default).
//   hold=n,n,n          extra held notes (played in addition to <midinote>);
//                       useful for arpeggiator/chord tests
//   sr=<hz>             sample rate (default 48000)
//   setat=<sec>:<name>:<value>
//                       schedule a parameter change: at the first block that
//                       starts at/after <sec>, call setParameter(name, value).
//                       Repeatable (pass multiple setat= args) — used to
//                       reproduce preset-switch / arp-toggle stuck-note bugs.

#include "MultiSynthDSP.hpp"

#include <algorithm>
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
void writeFloatWav(const char* path, const std::vector<float>& interleavedStereo, int sampleRate)
{
    const uint32_t numFrames = (uint32_t)(interleavedStereo.size() / 2);
    // Stereo float RIFF limit: 36 + numFrames*8 must fit in uint32 -> (UINT32_MAX-36)/8.
    if (numFrames > 536870907u)
    {
        std::fprintf(stderr, "render exceeds RIFF WAV frame limit\n");
        std::exit(2);
    }
    const uint16_t channels = 2;
    const uint16_t bits = 32;
    const uint32_t byteRate = (uint32_t)sampleRate * channels * (bits / 8);
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
    std::fwrite(interleavedStereo.data(), sizeof(float), interleavedStereo.size(), f);
    std::fclose(f);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "usage: render_test <mode> <midinote> <seconds> <osfactor> <out.wav> [key=value ...]\n");
        return 1;
    }

    const int   mode     = std::atoi(argv[1]);
    const int   midiNote = std::atoi(argv[2]);
    const double seconds  = std::atof(argv[3]);
    const int   osFactor = std::atoi(argv[4]);
    const char* outPath  = argv[5];

    double sampleRate = 48000.0;
    float  vel = 1.0f;
    double releaseTime = -1.0;
    double tempo = 120.0;
    bool   playing = true;
    double songPosStart = 0.0;
    bool   haveSongPos = false;
    std::vector<int> holdNotes;

    struct Override { int idx; float val; };
    std::vector<Override> overrides;

    // Scheduled parameter changes (setat=<sec>:<name>:<value>). Applied at the
    // first block starting at/after <sec>; multiple allowed.
    struct Scheduled { double time; int idx; float val; };
    std::vector<Scheduled> scheduled;

    for (int a = 6; a < argc; ++a)
    {
        std::string kv = argv[a];
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq);
        const std::string val = kv.substr(eq + 1);

        if (key == "setat")
        {
            // <sec>:<name>:<value>
            const auto c1 = val.find(':');
            const auto c2 = (c1 == std::string::npos) ? std::string::npos : val.find(':', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos)
            {
                std::fprintf(stderr, "bad setat (want <sec>:<name>:<value>): %s\n", val.c_str());
                return 1;
            }
            const double t = std::atof(val.substr(0, c1).c_str());
            const std::string name = val.substr(c1 + 1, c2 - c1 - 1);
            const float v = (float)std::atof(val.substr(c2 + 1).c_str());
            if (!(std::isfinite(t) && t >= 0.0))
            {
                std::fprintf(stderr, "bad setat time: %g\n", t);
                return 1;
            }
            const int sidx = msynth::MultiSynthDSP::paramIndexForName(name.c_str());
            if (sidx < 0) { std::fprintf(stderr, "setat unknown param: %s\n", name.c_str()); return 1; }
            scheduled.push_back({ t, sidx, v });
            continue;
        }
        if (key == "vel")      { vel = (float)std::atof(val.c_str()); continue; }
        if (key == "release")  { releaseTime = std::atof(val.c_str()); continue; }
        if (key == "tempo")    { tempo = std::atof(val.c_str()); continue; }
        if (key == "playing")  { playing = std::atoi(val.c_str()) != 0; continue; }
        if (key == "songpos")  { songPosStart = std::atof(val.c_str()); haveSongPos = true; continue; }
        if (key == "sr")       { sampleRate = std::atof(val.c_str()); continue; }
        if (key == "hold")
        {
            std::string s = val; size_t pos = 0;
            while (pos < s.size())
            {
                size_t comma = s.find(',', pos);
                std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) holdNotes.push_back(std::atoi(tok.c_str()));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            continue;
        }

        const int idx = msynth::MultiSynthDSP::paramIndexForName(key.c_str());
        if (idx < 0) { std::fprintf(stderr, "unknown param: %s\n", key.c_str()); return 1; }
        overrides.push_back({ idx, (float)std::atof(val.c_str()) });
    }

    if (osFactor != 1 && osFactor != 2 && osFactor != 4)
    {
        std::fprintf(stderr, "invalid osfactor: %d (must be 1, 2, or 4)\n", osFactor);
        return 1;
    }

    // Validate render extents (now that sr= override is applied) before any
    // prepare/allocation.
    if (!(std::isfinite(seconds) && seconds > 0.0 && seconds <= 3600.0))
    {
        std::fprintf(stderr, "invalid seconds: %g (want 0 < seconds <= 3600)\n", seconds);
        return 1;
    }
    if (!(std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0))
    {
        std::fprintf(stderr, "invalid sample rate: %g (want 8000..768000)\n", sampleRate);
        return 1;
    }
    const double framesD = seconds * sampleRate;
    // Stereo float RIFF limit: (UINT32_MAX-36)/8 frames.
    if (framesD > 536870907.0)
    {
        std::fprintf(stderr, "render too long: %g frames exceeds RIFF WAV limit\n", framesD);
        return 1;
    }

    const int osIdx = (osFactor == 4) ? 2 : (osFactor == 2 ? 1 : 0);
    const int blockSize = 512;

    msynth::MultiSynthDSP synth;
    synth.prepare(sampleRate, blockSize);
    synth.setParameter(msynth::pMode, (float)mode);
    synth.setParameter(msynth::pOversampling, (float)osIdx);
    for (const auto& o : overrides) synth.setParameter(o.idx, o.val);
    synth.setTempo(tempo, playing);

    // Trigger note(s). noteOn routes to the arp internally when arpOn is set.
    synth.noteOn(midiNote, vel);
    for (int n : holdNotes) synth.noteOn(n, vel);

    const int totalFrames = (int)framesD;
    const int releaseFrame = releaseTime >= 0.0 ? (int)(releaseTime * sampleRate) : -1;

    std::vector<float> interleaved((size_t)totalFrames * 2, 0.0f);
    std::vector<float> bufL((size_t)blockSize), bufR((size_t)blockSize);

    // Frame at which each scheduled change fires (first block starting >= it).
    std::vector<char> schedDone(scheduled.size(), 0);

    bool released = false;
    for (int pos = 0; pos < totalFrames; )
    {
        int n = std::min(blockSize, totalFrames - pos);

        // Apply any scheduled parameter changes whose time has arrived.
        for (size_t s = 0; s < scheduled.size(); ++s)
        {
            if (schedDone[s]) continue;
            const int frame = (int)(scheduled[s].time * sampleRate);
            if (pos >= frame)
            {
                synth.setParameter(scheduled[s].idx, scheduled[s].val);
                schedDone[s] = 1;
            }
        }

        if (!released && releaseFrame >= 0 && pos >= releaseFrame)
        {
            // Sample-exact release: earlier iterations split the block so this
            // one starts precisely on releaseFrame.
            synth.noteOff(midiNote);
            for (int hn : holdNotes) synth.noteOff(hn);
            released = true;
        }
        else if (!released && releaseFrame > pos && releaseFrame < pos + n)
        {
            n = releaseFrame - pos; // shorten so the next iteration starts at releaseFrame
        }

        // Host phase-lock: feed the song position for THIS block's start frame
        // (pos = frames already rendered), so the block-start beat is correct
        // across the release split. Unset songpos leaves the engine free-running.
        if (haveSongPos)
            synth.setSongPosition(songPosStart + (double)pos / sampleRate * tempo / 60.0, true);

        synth.processBlock(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            interleaved[(size_t)((pos + i) * 2 + 0)] = bufL[(size_t)i];
            interleaved[(size_t)((pos + i) * 2 + 1)] = bufR[(size_t)i];
        }
        pos += n;
    }

    writeFloatWav(outPath, interleaved, (int)sampleRate);
    std::fprintf(stderr, "wrote %s  (%d frames @ %.0f Hz, os=%dx)\n", outPath, totalFrames, sampleRate, osFactor);
    return 0;
}
