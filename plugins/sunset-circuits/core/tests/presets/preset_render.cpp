// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// preset_render — offline factory-preset renderer for the Multi-Synth Phase 5
// re-voice + validation sweep.
//
//   preset_render <presetIndex> <out.wav> [key=value ...]
//
// The factory presets are static param-value rows (MultiSynthParams.hpp) and the
// DSP core is framework-free, so this tool includes BOTH directly — no plugin
// hosting, no MIDI file injection. It applies preset N exactly as the DPF shell's
// loadProgram() does (reset-to-default -> baseline -> overrides), then plays a
// mode-appropriate MIDI performance and writes a float32 stereo WAV.
//
// Performance is auto-selected from the preset's Mode + Arp On values:
//   * Acid  (mode 5)                : hold C2, run the pattern sequencer 4 bars.
//   * arpOn set                     : hold a Cmaj triad, let the arp run 4 bars.
//   * poly  (Cosmos/Oracle/Prism)   : Cmaj7 chord (2 bars) + a melodic phrase.
//   * mono/lead (Mono/Modular)      : a legato bass/lead phrase.
// Override with perf=chord|mono|arp|acid|hold|single.
//
// Special keys:
//   perf=<name>     force a performance (see above)
//   note=<n>        root/base MIDI note (defaults per performance)
//   hold=n,n,n      notes for perf=hold (held for the whole render)
//   bars=<n>        performance length in bars (default per performance)
//   tempo=<bpm>     host tempo (default 120)
//   sr=<hz>         sample rate (default 48000)
//   vel=<0..1>      note-on velocity (default 0.9)
//   tail=<sec>      extra silence rendered after the last note-off (default 1.5)
// Any other key=value overrides an engine parameter by name AFTER the preset is
// applied (handy for auditioning a candidate value before baking it into a row).

#include "MultiSynthDSP.hpp"
#include "MultiSynthParams.hpp"

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
void writeFloatWav(const char* path, const std::vector<float>& interleavedStereo, int sampleRate)
{
    const uint32_t numFrames = (uint32_t)(interleavedStereo.size() / 2);
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

struct NoteEvent { int frame; bool on; int note; float vel; };

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

std::vector<int> parseNoteList(const std::string& val)
{
    std::vector<int> out;
    size_t pos = 0;
    while (pos < val.size())
    {
        size_t comma = val.find(',', pos);
        std::string tok = val.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        // Strict: reject empty/garbage tokens; each note must be a MIDI 0..127.
        const long n = parseInt("hold", tok);
        if (n < 0 || n > 127)
        {
            std::fprintf(stderr, "invalid hold note %ld (want 0..127)\n", n);
            std::exit(2);
        }
        out.push_back((int)n);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: preset_render <presetIndex> <out.wav> [key=value ...]\n");
        return 1;
    }

    const int   presetIndex = (int)parseInt("presetIndex", argv[1]);
    const char* outPath     = argv[2];

    if (presetIndex < 0 || presetIndex >= kNumFactoryPresets)
    {
        std::fprintf(stderr, "preset index %d out of range [0,%d)\n", presetIndex, kNumFactoryPresets);
        return 1;
    }

    double sampleRate = 48000.0;
    double tempo      = 120.0;
    float  vel        = 0.9f;
    double tailSec    = 1.5;
    int    barsArg    = -1;
    int    noteArg    = -1000;
    std::string perf  = "auto";
    std::vector<int> holdNotes;

    struct Override { int idx; float val; };
    std::vector<Override> overrides;

    for (int a = 3; a < argc; ++a)
    {
        std::string kv = argv[a];
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq);
        const std::string val = kv.substr(eq + 1);

        if (key == "perf")  { perf = val; continue; }
        if (key == "note")
        {
            const long n = parseInt("note", val);
            if (n < 0 || n > 127)
            {
                std::fprintf(stderr, "invalid note: %ld (want 0..127)\n", n);
                return 1;
            }
            noteArg = (int)n; continue;
        }
        if (key == "hold")  { holdNotes = parseNoteList(val); continue; }
        if (key == "bars")
        {
            const long b = parseInt("bars", val);
            if (b < 1 || b > 10000)
            {
                std::fprintf(stderr, "invalid bars: %ld (want 1..10000)\n", b);
                return 1;
            }
            barsArg = (int)b; continue;
        }
        if (key == "tempo") { tempo = parseNum("tempo", val); continue; }
        if (key == "sr")    { sampleRate = parseNum("sr", val); continue; }
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
        if (key == "tail")  { tailSec = parseNum("tail", val); continue; }

        const int idx = msynth::MultiSynthDSP::paramIndexForName(key.c_str());
        if (idx < 0) { std::fprintf(stderr, "unknown param: %s\n", key.c_str()); return 1; }
        overrides.push_back({ idx, (float)parseNum(key.c_str(), val) });
    }

    // Validate the performance name (unknown value must error, not fall to mono).
    if (perf != "auto" && perf != "chord" && perf != "mono" && perf != "arp" &&
        perf != "acid" && perf != "hold" && perf != "single")
    {
        std::fprintf(stderr, "unknown perf: %s (want auto|chord|mono|arp|acid|hold|single)\n",
                     perf.c_str());
        return 1;
    }

    // Validate render extents before any prepare/allocation.
    if (!std::isfinite(tempo) || tempo < 20.0 || tempo > 999.0)
    {
        std::fprintf(stderr, "invalid tempo: %g (want 20..999)\n", tempo);
        return 1;
    }
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 768000.0)
    {
        std::fprintf(stderr, "invalid sample rate: %g (want 8000..768000)\n", sampleRate);
        return 1;
    }
    // The DSP runs at the double rate but the WAV header stores (int)sampleRate;
    // require a whole number so the two agree exactly.
    if (sampleRate != std::floor(sampleRate))
    {
        std::fprintf(stderr, "invalid sample rate: %g (must be a whole number)\n", sampleRate);
        return 1;
    }
    if (!std::isfinite(tailSec) || tailSec < 0.0 || tailSec > 600.0)
    {
        std::fprintf(stderr, "invalid tail: %g (want 0..600)\n", tailSec);
        return 1;
    }

    const int blockSize = 128; // fine enough for ~2.7 ms MIDI-event granularity

    msynth::MultiSynthDSP synth;
    synth.prepare(sampleRate, blockSize);

    // Apply the preset exactly as the DPF shell's loadProgram() does.
    for (int i = 0; i < kNumCoreParams; ++i)
        synth.setParameter(i, kParamDefs[i].def);
    for (int r = 0; r < kBaselineRows; ++r)
        synth.setParameter(kPresetBaseline[r].index, kPresetBaseline[r].value);
    const FactoryPreset& pr = kFactoryPresets[presetIndex];
    for (int r = 0; r < pr.nRows; ++r)
        synth.setParameter(pr.rows[r].index, pr.rows[r].value);
    // Post-preset auditioning overrides.
    for (const auto& o : overrides) synth.setParameter(o.idx, o.val);

    synth.setTempo(tempo, true);

    const int mode = (int)(synth.getParameter(msynth::pMode) + 0.5f);
    const bool arpOn = synth.getParameter(msynth::pArpOn) > 0.5f;

    if (perf == "auto")
    {
        if (mode == 5)      perf = "acid";
        else if (arpOn)     perf = "arp";
        else if (mode == 0 || mode == 1 || mode == 4) perf = "chord";
        else                perf = "mono";
    }

    const double secPerBeat = 60.0 / tempo;
    const double secPerBar  = 4.0 * secPerBeat;
    // seconds -> frame, checked: rejects non-finite times or overflow past the
    // stereo float RIFF frame limit (guards bars= abuse driving event times to inf).
    auto S = [&](double sec) -> int {
        const double frames = sec * sampleRate;
        if (!std::isfinite(frames) || frames > 536870907.0)
        {
            std::fprintf(stderr, "event time out of range: %g s -> %g frames\n", sec, frames);
            std::exit(2);
        }
        return (int)frames;
    };

    std::vector<NoteEvent> events;
    double lastOff = 0.0;

    if (perf == "hold")
    {
        if (holdNotes.empty()) holdNotes = { 60, 64, 67 };
        const int bars = barsArg > 0 ? barsArg : 4;
        const double dur = bars * secPerBar;
        for (int n : holdNotes) events.push_back({ S(0.0), true, n, vel });
        for (int n : holdNotes) events.push_back({ S(dur), false, n, 0.0f });
        lastOff = dur;
    }
    else if (perf == "acid")
    {
        const int root = noteArg > -1000 ? noteArg : 36; // C2
        const int bars = barsArg > 0 ? barsArg : 4;
        const double dur = bars * secPerBar;
        events.push_back({ S(0.0), true, root, vel });
        events.push_back({ S(dur), false, root, 0.0f });
        lastOff = dur;
    }
    else if (perf == "arp")
    {
        const int root = noteArg > -1000 ? noteArg : 48; // C3
        const int chord[3] = { root, root + 4, root + 7 }; // Cmaj triad
        const int bars = barsArg > 0 ? barsArg : 4;
        const double dur = bars * secPerBar;
        for (int n : chord) events.push_back({ S(0.0), true, n, vel });
        for (int n : chord) events.push_back({ S(dur), false, n, 0.0f });
        lastOff = dur;
    }
    else if (perf == "chord")
    {
        const int root = noteArg > -1000 ? noteArg : 60; // C4
        const int maj7[4] = { root, root + 4, root + 7, root + 11 }; // Cmaj7
        const int bars = barsArg > 0 ? barsArg : 4;
        // Sustained chord over the first half; a melodic phrase then a closing
        // chord over the second half. Scaled by bars; bars=4 reproduces the
        // original event times exactly (byte-identical render). All second-half
        // events are clamped to the phrase-half boundary so nothing spills past
        // the render window at small bar counts.
        const double chordEnd = 0.5 * bars * secPerBar - 0.1 * secPerBeat;
        for (int n : maj7) events.push_back({ S(0.0), true, n, vel });
        for (int n : maj7) events.push_back({ S(chordEnd), false, n, 0.0f });
        // Melodic phrase (fixed six-eighth line) starting at the half-way point.
        const double t0 = 0.5 * bars * secPerBar;
        const double bound = t0 + bars * secPerBeat; // phrase-half boundary (== cOff)
        double maxOff = 0.0;
        const int phrase[6] = { root + 12, root + 16, root + 19, root + 16, root + 12, root + 7 };
        for (int i = 0; i < 6; ++i)
        {
            const double on = t0 + i * secPerBeat * 0.5;      // eighth notes
            if (on >= bound) continue;                        // note-on past the boundary
            double off = on + secPerBeat * 0.45;
            if (off > bound) off = bound;                     // clamp off to the boundary
            events.push_back({ S(on), true, phrase[i], vel });
            events.push_back({ S(off), false, phrase[i], 0.0f });
            if (off > maxOff) maxOff = off;
        }
        // Closing chord in the last beat of the phrase half (cOn = bound - one beat
        // is always < bound; cOff == bound). At bars=4 this is t0+3..t0+4.
        const double cOn = t0 + (bars - 1) * secPerBeat;
        const double cOff = bound;
        const int close[3] = { root, root + 4, root + 7 };
        for (int n : close) events.push_back({ S(cOn), true, n, vel });
        for (int n : close) events.push_back({ S(cOff), false, n, 0.0f });
        if (cOff > maxOff) maxOff = cOff;
        lastOff = maxOff;
    }
    else if (perf == "single")
    {
        const int n = noteArg > -1000 ? noteArg : 60;
        const int bars = barsArg > 0 ? barsArg : 2;
        const double dur = bars * secPerBar;
        events.push_back({ S(0.0), true, n, vel });
        events.push_back({ S(dur), false, n, 0.0f });
        lastOff = dur;
    }
    else // "mono" legato bass/lead phrase
    {
        const int root = noteArg > -1000 ? noteArg : 36; // C2
        // note, onBeat, lengthBeats. Consecutive notes overlap slightly (legato).
        struct N { int semi; double onBeat; double lenBeats; };
        const N line[7] = {
            { 0,  0.0, 1.1 }, { 12, 1.0, 1.1 }, { 7, 2.0, 1.1 }, { 15, 3.0, 1.6 },
            { 10, 4.5, 1.1 }, { 3, 5.5, 1.1 }, { 0, 6.5, 2.5 },
        };
        // Optional bars= truncation: drop notes that start at/after the limit and
        // clamp every note-off (and lastOff) to it. Default (bars<=0) leaves the
        // phrase untouched -> byte-identical render.
        const double barLimit = barsArg > 0 ? barsArg * secPerBar : 1.0e30;
        for (const N& x : line)
        {
            const double on = x.onBeat * secPerBeat;
            if (on >= barLimit) continue;
            double off = (x.onBeat + x.lenBeats) * secPerBeat;
            if (off > barLimit) off = barLimit;
            events.push_back({ S(on), true, root + x.semi, vel });
            events.push_back({ S(off), false, root + x.semi, 0.0f });
            lastOff = std::max(lastOff, off);
        }
    }

    // Stable sort by frame so same-frame note-offs precede note-ons only if we
    // want retrigger; here order within a frame doesn't matter for distinct notes.
    std::stable_sort(events.begin(), events.end(),
                     [](const NoteEvent& a, const NoteEvent& b) { return a.frame < b.frame; });

    const int totalFrames = S(lastOff + tailSec);
    std::vector<float> interleaved((size_t)totalFrames * 2, 0.0f);
    std::vector<float> bufL((size_t)blockSize), bufR((size_t)blockSize);

    size_t ei = 0;
    for (int pos = 0; pos < totalFrames; pos += blockSize)
    {
        const int n = std::min(blockSize, totalFrames - pos);
        // Apply all events that fall within this block at the block start.
        while (ei < events.size() && events[ei].frame < pos + n)
        {
            const NoteEvent& e = events[ei];
            if (e.on) synth.noteOn(e.note, e.vel);
            else      synth.noteOff(e.note);
            ++ei;
        }
        synth.processBlock(bufL.data(), bufR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            interleaved[(size_t)((pos + i) * 2 + 0)] = bufL[(size_t)i];
            interleaved[(size_t)((pos + i) * 2 + 1)] = bufR[(size_t)i];
        }
    }

    writeFloatWav(outPath, interleaved, (int)sampleRate);
    std::fprintf(stderr, "wrote %s  preset=%d \"%s\" mode=%d perf=%s  (%d frames @ %.0f Hz)\n",
                 outPath, presetIndex, pr.name, mode, perf.c_str(), totalFrames, sampleRate);
    return 0;
}
