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

    // Checked write: a short fwrite (disk full, quota) must fail, not truncate.
    auto w = [&](const void* p, size_t sz, size_t n) {
        if (std::fwrite(p, sz, n, f) != n)
        {
            std::fprintf(stderr, "short write to %s\n", path);
            std::exit(2);
        }
    };
    auto u32 = [&](uint32_t v) { w(&v, 4, 1); };
    auto u16 = [&](uint16_t v) { w(&v, 2, 1); };

    w("RIFF", 1, 4); u32(36 + dataBytes); w("WAVE", 1, 4);
    w("fmt ", 1, 4); u32(16); u16(3 /* IEEE float */); u16(channels);
    u32((uint32_t)sampleRate); u32(byteRate); u16(blockAlign); u16(bits);
    w("data", 1, 4); u32(dataBytes);
    w(interleavedStereo.data(), sizeof(float), interleavedStereo.size());
    // fclose flushes buffered data; an EOF here means a deferred write failed.
    if (std::fclose(f) != 0)
    {
        std::fprintf(stderr, "error closing %s\n", path);
        std::exit(2);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "usage: render_test <mode> <midinote> <seconds> <osfactor> <out.wav> [key=value ...]\n");
        return 1;
    }

    const long  modeL    = parseInt("mode", argv[1]);
    if (modeL < 0 || modeL > 5)
    {
        std::fprintf(stderr, "invalid mode: %ld (want 0..5)\n", modeL);
        return 1;
    }
    const int   mode     = (int)modeL;
    const long  midiL    = parseInt("midinote", argv[2]);
    if (midiL < 0 || midiL > 127)
    {
        std::fprintf(stderr, "invalid midinote: %ld (want 0..127)\n", midiL);
        return 1;
    }
    const int   midiNote = (int)midiL;
    const double seconds  = parseNum("seconds", argv[3]);
    const long  osL      = parseInt("osfactor", argv[4]);
    const int   osFactor = (osL >= INT_MIN && osL <= INT_MAX) ? (int)osL : 0;
    const char* outPath  = argv[5];

    double sampleRate = 48000.0;
    float  vel = 1.0f;
    double releaseTime = -1.0;
    bool   releaseProvided = false;
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

    // Scheduled note events (noteon=<sec>:<note> / noteoff=<sec>:<note>). Fired at
    // the first block starting at/after <sec>; used for retrigger/steal tests.
    struct SchedNote { double time; int note; bool on; };
    std::vector<SchedNote> schedNotes;

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
            const double t = parseNum("setat.time", val.substr(0, c1));
            const std::string name = val.substr(c1 + 1, c2 - c1 - 1);
            const float v = (float)parseNum("setat.value", val.substr(c2 + 1));
            if (!(std::isfinite(t) && t >= 0.0 && t <= seconds))
            {
                std::fprintf(stderr, "bad setat time: %g (want 0 <= t <= %g)\n", t, seconds);
                return 1;
            }
            const int sidx = msynth::MultiSynthDSP::paramIndexForName(name.c_str());
            if (sidx < 0) { std::fprintf(stderr, "setat unknown param: %s\n", name.c_str()); return 1; }
            scheduled.push_back({ t, sidx, v });
            continue;
        }
        if (key == "noteon" || key == "noteoff")
        {
            // <sec>:<note>
            const auto c1 = val.find(':');
            if (c1 == std::string::npos)
            {
                std::fprintf(stderr, "bad %s (want <sec>:<note>): %s\n", key.c_str(), val.c_str());
                return 1;
            }
            const double t = parseNum("noteev.time", val.substr(0, c1));
            const long nnL = parseInt("noteev.note", val.substr(c1 + 1));
            if (!(std::isfinite(t) && t >= 0.0 && t <= seconds))
            {
                std::fprintf(stderr, "bad %s time: %g (want 0 <= t <= %g)\n", key.c_str(), t, seconds);
                return 1;
            }
            if (nnL < 0 || nnL > 127)
            {
                std::fprintf(stderr, "bad %s note: %ld (want 0..127)\n", key.c_str(), nnL);
                return 1;
            }
            schedNotes.push_back({ t, (int)nnL, key == "noteon" });
            continue;
        }
        if (key == "vel")
        {
            vel = (float)parseNum("vel", val);
            if (vel < 0.0f || vel > 1.0f)
            {
                std::fprintf(stderr, "invalid vel: %g (want 0..1)\n", (double)vel);
                return 1;
            }
            continue;
        }
        if (key == "release")  { releaseTime = parseNum("release", val); releaseProvided = true; continue; }
        if (key == "tempo")
        {
            tempo = parseNum("tempo", val);
            if (tempo < 20.0 || tempo > 999.0)
            {
                std::fprintf(stderr, "invalid tempo: %g (want 20..999)\n", tempo);
                return 1;
            }
            continue;
        }
        if (key == "playing")
        {
            const long p = parseInt("playing", val);
            if (p != 0 && p != 1)
            {
                std::fprintf(stderr, "invalid playing: %ld (want 0 or 1)\n", p);
                return 1;
            }
            playing = (p != 0); continue;
        }
        if (key == "songpos")  { songPosStart = parseNum("songpos", val); haveSongPos = true; continue; }
        if (key == "sr")       { sampleRate = parseNum("sr", val); continue; }
        if (key == "hold")
        {
            std::string s = val; size_t pos = 0;
            while (pos < s.size())
            {
                size_t comma = s.find(',', pos);
                std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                const long hn = parseInt("hold", tok);
                if (hn < 0 || hn > 127)
                {
                    std::fprintf(stderr, "invalid hold note %ld (want 0..127)\n", hn);
                    return 1;
                }
                holdNotes.push_back((int)hn);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            continue;
        }

        const int idx = msynth::MultiSynthDSP::paramIndexForName(key.c_str());
        if (idx < 0) { std::fprintf(stderr, "unknown param: %s\n", key.c_str()); return 1; }
        overrides.push_back({ idx, (float)parseNum(key.c_str(), val) });
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
    // The (int)sampleRate WAV-header value, the frame math, and the DSP rate must
    // all agree exactly, so require a whole number.
    if (sampleRate != std::floor(sampleRate))
    {
        std::fprintf(stderr, "invalid sample rate: %g (must be a whole number)\n", sampleRate);
        return 1;
    }
    // A provided release must be finite and within the render window: NaN would
    // otherwise slip past the (releaseTime >= 0.0) gate to "never release", and a
    // huge value overflows the later (int)(releaseTime * sampleRate) cast.
    if (releaseProvided && !(std::isfinite(releaseTime) && releaseTime >= 0.0 && releaseTime <= seconds))
    {
        std::fprintf(stderr, "invalid release: %g (want 0 <= release <= %g)\n", releaseTime, seconds);
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
    std::vector<char> schedNoteDone(schedNotes.size(), 0);

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

        // Apply any scheduled note events whose time has arrived.
        for (size_t s = 0; s < schedNotes.size(); ++s)
        {
            if (schedNoteDone[s]) continue;
            const int frame = (int)(schedNotes[s].time * sampleRate);
            if (pos >= frame)
            {
                if (schedNotes[s].on) synth.noteOn(schedNotes[s].note, vel);
                else                  synth.noteOff(schedNotes[s].note);
                schedNoteDone[s] = 1;
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
