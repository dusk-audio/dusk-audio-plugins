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
// these special keys. Every <sec> below is validated against the HALF-OPEN window
// [0, seconds): an event at exactly `seconds` has no block left to fire on, so it
// is rejected rather than silently ignored (see validEventTime).
//   vel=<0..1>          note-on velocity (default 1.0)
//   release=<sec>       call noteOff at this time (default: no release)
//   tempo=<bpm>         host tempo (default 120)
//   playing=<0|1>       transport playing flag (default 1)
//   songpos=<beats>     host song position at frame 0, in beats. When set, the
//                       harness acts as the DAW host: it calls setSongPosition
//                       before each processBlock so the arp/acid/LFO clocks
//                       phase-lock to the host grid. Unset = free-run (default).
//   loopat=<sec>:<beats>
//                       jump the song position back (or forward) to <beats> at
//                       this time and carry on from there — a loop wrap or a
//                       seek. Applied at the first block starting at/after <sec>.
//                       Repeatable. Needs songpos= to have any effect.
//   playat=<sec>        start the transport at this time instead of at frame 0:
//                       until then the host reports "stopped" and parks the
//                       playhead at songpos (a stopped DAW does not advance it),
//                       after it the position rolls from songpos. This is the
//                       free -> host-locked transition, which is the only way to
//                       exercise how the clocks acquire the lock mid-note.
//   hold=n,n,n          extra held notes (played in addition to <midinote>);
//                       useful for arpeggiator/chord tests
//   sr=<hz>             sample rate (default 48000)
//   block=<frames>      host buffer size (default 512, 1..16384). Scheduled
//                       events still fire on the first block starting at/after
//                       their time, so they land on a block boundary — which is
//                       the point: anything the engine defers to a boundary
//                       (parameter snapshots, mode switches) has to behave the
//                       same at every buffer size, and this is the knob that
//                       proves it.
//   setat=<sec>:<name>:<value>
//                       schedule a parameter change: at the first block that
//                       starts at/after <sec>, call setParameter(name, value).
//                       Repeatable (pass multiple setat= args) — used to
//                       reproduce preset-switch / arp-toggle stuck-note bugs.
//                       This is the ONE place a non-finite value is accepted:
//                       nan / inf / -inf / +inf (and anything strtod reads as
//                       one) pass through verbatim, because injecting a bad
//                       parameter write is the whole point of the fault gate.
//                       Every other key stays strictly finite.
//   meters=<path>       append "<sec> <levelL_dB> <levelR_dB>" per rendered
//                       block to <path>, read from getOutputLevelL/R after each
//                       processBlock. The meters are a published observable with
//                       a declared -60 dB floor, and this is the only way a gate
//                       can see them.
//   notifyat=<sec>      call notifyProgramChange() on the same block boundary as
//                       a setat= at the same time, AFTER that block's parameter
//                       writes — i.e. exactly what the shell's loadProgram() and
//                       the UI preset paths do. Repeatable.
//   sustainat=<sec>:<0|1>
//                       sustain pedal (MIDI CC64) up/down at this time, applied on
//                       the first block starting at/after <sec>. Repeatable.
//   panicat=<sec>       call allNotesOff() (MIDI CC123 and the channel-mode
//                       messages that imply it) at this time. Repeatable.
//   soundoffat=<sec>    call allSoundOff() (MIDI CC120) at this time — the same
//                       teardown plus an immediate, bounded voice kill, so a long
//                       release tail does not survive it. Repeatable.
//   polyat=<sec>:<note>:<0..1>
//                       polyphonic key pressure (MIDI 0xA0) for one note.
//                       Repeatable.
//   chanat=<sec>:<0..1> channel pressure (MIDI 0xD0). Repeatable.
//   progat=<sec>:<n>    load factory program <n> at this time, exactly as the DAF
//                       shell's loadProgram() does (every parameter to its
//                       default, then the shared baseline, then the preset's own
//                       rows, then notifyProgramChange). Repeatable. NOTE: this
//                       harness links the CORE only, so it reproduces what
//                       loadProgram DOES, not the shell function itself -- the
//                       shell's MIDI 0xC0 -> loadProgram wiring is covered
//                       host-side by daf-plugin/tools/lv2_smoke.c.

#include "MultiSynthDSP.hpp"
#include "MultiSynthParams.hpp"   // factory preset table (progat=), shell-side

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

// The preset rows are written with the SHELL's parameter indices; they only address
// the core correctly because the two tables are 1:1. MultiSynthPlugin.cpp asserts
// that, and so does this harness, so an index drift fails here too rather than
// silently loading a scrambled patch into the gates.
static_assert((int)kParamMode      == (int)msynth::pMode,      "param order drift");
static_assert((int)kParamArpStep0  == (int)msynth::pArpStep0,  "arp step drift");
static_assert((int)kParamModSrc0   == (int)msynth::pModSrc0,   "mod matrix drift");
static_assert((int)kParamSeqSlide0 == (int)msynth::pSeqSlide0, "seq slide drift");
static_assert((int)kParamModOsc3Filter == (int)msynth::pModOsc3Filter,
              "accuracy-upgrade block drift");
static_assert((int)kNumCoreParams  == (int)msynth::kNumParams, "core param count drift");

namespace
{
// Apply a factory program the way the DAF shell's loadProgram() does: every
// parameter to its default (so the result cannot depend on what was loaded
// before), then the shared baseline, then the preset's own rows, and finally the
// explicit program-change signal that makes the smoothers land instead of glide.
void loadFactoryProgram(msynth::MultiSynthDSP& synth, int index) noexcept
{
    for (int i = 0; i < kNumCoreParams; ++i)
        synth.setParameter(i, kParamDefs[i].def);
    for (int r = 0; r < kBaselineRows; ++r)
        synth.setParameter(kPresetBaseline[r].index, kPresetBaseline[r].value);
    const FactoryPreset& pr = kFactoryPresets[index];
    for (int r = 0; r < pr.nRows; ++r)
        synth.setParameter(pr.rows[r].index, pr.rows[r].value);
    synth.notifyProgramChange();
}

// Scheduled events fire on the FIRST BLOCK STARTING AT OR AFTER their time, and
// the render loop stops at `seconds`. An event scheduled at exactly `seconds`
// therefore has no block left to fire on: the harness used to accept it and
// silently do nothing, so a gate written around it measured an untouched render
// and passed. The schedule window is half-open -- [0, seconds) -- and this is
// where that is enforced, for every event key and for release=.
bool validEventTime(double t, double seconds) noexcept
{
    return std::isfinite(t) && t >= 0.0 && t < seconds;
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

// Like parseNum but PERMITS non-finite results (nan / inf / -inf). Used only for
// setat= values: "a bad parameter value must not kill the engine for good" can
// only be gated by actually writing one, and the whole point is that the engine
// survives it. Trailing garbage is still rejected.
double parseNumAllowNonFinite(const char* key, const std::string& v)
{
    if (v.empty())
    {
        std::fprintf(stderr, "empty value for key '%s'\n", key);
        std::exit(2);
    }
    const char* start = v.c_str();
    char* end = nullptr;
    const double d = std::strtod(start, &end);
    if (end == start || *end != '\0')
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
    double playAtTime = -1.0;   // < 0 = transport rolling from frame 0
    std::vector<int> holdNotes;
    int    blockSize = 512;
    std::string meterPath;

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

    // Scheduled program-change notifications (notifyat=<sec>). Fired after the
    // scheduled parameter writes of the same block, mirroring the shell.
    std::vector<double> schedNotify;

    // Scheduled sustain-pedal (CC64) edges and panics (CC123 / CC120).
    struct SchedPedal { double time; bool down; };
    std::vector<SchedPedal> schedPedal;
    std::vector<double>     schedPanic;      // panicat=    -> allNotesOff()
    std::vector<double>     schedSoundOff;   // soundoffat= -> allSoundOff()

    // Scheduled pressure messages: polyphonic key pressure (0xA0, note >= 0) and
    // channel pressure (0xD0, note < 0).
    struct SchedPressure { double time; int note; float value; };
    std::vector<SchedPressure> schedPressure;

    // Scheduled factory-program loads (progat=<sec>:<n>).
    struct SchedProgram { double time; int index; };
    std::vector<SchedProgram> schedProgram;

    // Scheduled song-position jumps (loopat=<sec>:<beats>) — loop wrap / seek.
    struct SchedLoop { double time; double beats; };
    std::vector<SchedLoop> schedLoops;

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
            const float v = (float)parseNumAllowNonFinite("setat.value", val.substr(c2 + 1));
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad setat time: %g (want 0 <= t < %g)\n", t, seconds);
                return 1;
            }
            const int sidx = msynth::MultiSynthDSP::paramIndexForName(name.c_str());
            if (sidx < 0) { std::fprintf(stderr, "setat unknown param: %s\n", name.c_str()); return 1; }
            scheduled.push_back({ t, sidx, v });
            continue;
        }
        if (key == "notifyat")
        {
            const double t = parseNum("notifyat", val);
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad notifyat time: %g (want 0 <= t < %g)\n", t, seconds);
                return 1;
            }
            schedNotify.push_back(t);
            continue;
        }
        if (key == "sustainat")
        {
            // <sec>:<0|1>
            const auto c1 = val.find(':');
            if (c1 == std::string::npos)
            {
                std::fprintf(stderr, "bad sustainat (want <sec>:<0|1>): %s\n", val.c_str());
                return 1;
            }
            const double t = parseNum("sustainat.time", val.substr(0, c1));
            const long d = parseInt("sustainat.down", val.substr(c1 + 1));
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad sustainat time: %g (want 0 <= t < %g)\n", t, seconds);
                return 1;
            }
            if (d != 0 && d != 1)
            {
                std::fprintf(stderr, "bad sustainat state: %ld (want 0 or 1)\n", d);
                return 1;
            }
            schedPedal.push_back({ t, d != 0 });
            continue;
        }
        if (key == "progat")
        {
            // <sec>:<program>
            const auto c1 = val.find(':');
            if (c1 == std::string::npos)
            {
                std::fprintf(stderr, "bad progat (want <sec>:<program>): %s\n", val.c_str());
                return 1;
            }
            const double t = parseNum("progat.time", val.substr(0, c1));
            const long n = parseInt("progat.program", val.substr(c1 + 1));
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad progat time: %g (want 0 <= t < %g)\n", t, seconds);
                return 1;
            }
            if (n < 0 || n >= kNumFactoryPresets)
            {
                std::fprintf(stderr, "bad progat program: %ld (want 0..%d)\n",
                             n, kNumFactoryPresets - 1);
                return 1;
            }
            schedProgram.push_back({ t, (int)n });
            continue;
        }
        if (key == "polyat" || key == "chanat")
        {
            // polyat=<sec>:<note>:<0..1>   chanat=<sec>:<0..1>
            const bool poly = (key == "polyat");
            const auto c1 = val.find(':');
            const auto c2 = (!poly || c1 == std::string::npos)
                          ? std::string::npos : val.find(':', c1 + 1);
            if (c1 == std::string::npos || (poly && c2 == std::string::npos))
            {
                std::fprintf(stderr, "bad %s (want %s): %s\n", key.c_str(),
                             poly ? "<sec>:<note>:<0..1>" : "<sec>:<0..1>", val.c_str());
                return 1;
            }
            const double t = parseNum("pressure.time", val.substr(0, c1));
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad %s time: %g (want 0 <= t < %g)\n", key.c_str(), t, seconds);
                return 1;
            }
            int note = -1;
            if (poly)
            {
                const long nnL = parseInt("polyat.note", val.substr(c1 + 1, c2 - c1 - 1));
                if (nnL < 0 || nnL > 127)
                {
                    std::fprintf(stderr, "bad polyat note: %ld (want 0..127)\n", nnL);
                    return 1;
                }
                note = (int)nnL;
            }
            const double v = parseNum("pressure.value", val.substr((poly ? c2 : c1) + 1));
            if (!(v >= 0.0 && v <= 1.0))
            {
                std::fprintf(stderr, "bad %s value: %g (want 0..1)\n", key.c_str(), v);
                return 1;
            }
            schedPressure.push_back({ t, note, (float)v });
            continue;
        }
        if (key == "panicat" || key == "soundoffat")
        {
            const double t = parseNum(key.c_str(), val);
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad %s time: %g (want 0 <= t < %g)\n",
                             key.c_str(), t, seconds);
                return 1;
            }
            (key == "panicat" ? schedPanic : schedSoundOff).push_back(t);
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
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad %s time: %g (want 0 <= t < %g)\n", key.c_str(), t, seconds);
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
        if (key == "loopat")
        {
            // <sec>:<beats>
            const auto c1 = val.find(':');
            if (c1 == std::string::npos)
            {
                std::fprintf(stderr, "bad loopat (want <sec>:<beats>): %s\n", val.c_str());
                return 1;
            }
            const double t = parseNum("loopat.time", val.substr(0, c1));
            const double b = parseNum("loopat.beats", val.substr(c1 + 1));
            if (!validEventTime(t, seconds))
            {
                std::fprintf(stderr, "bad loopat time: %g (want 0 <= t < %g)\n", t, seconds);
                return 1;
            }
            schedLoops.push_back({ t, b });
            continue;
        }
        if (key == "playat")
        {
            playAtTime = parseNum("playat", val);
            if (!validEventTime(playAtTime, seconds))
            {
                std::fprintf(stderr, "invalid playat: %g (want 0 <= playat < %g)\n", playAtTime, seconds);
                return 1;
            }
            continue;
        }
        if (key == "meters")   { meterPath = val; continue; }
        if (key == "sr")       { sampleRate = parseNum("sr", val); continue; }
        if (key == "block")
        {
            const long b = parseInt("block", val);
            if (b < 1 || b > 16384)
            {
                std::fprintf(stderr, "invalid block: %ld (want 1..16384)\n", b);
                return 1;
            }
            blockSize = (int)b;
            continue;
        }
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
    if (releaseProvided && !validEventTime(releaseTime, seconds))
    {
        std::fprintf(stderr, "invalid release: %g (want 0 <= release < %g)\n", releaseTime, seconds);
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

    msynth::MultiSynthDSP synth;
    synth.prepare(sampleRate, blockSize);
    synth.setParameter(msynth::pMode, (float)mode);
    synth.setParameter(msynth::pOversampling, (float)osIdx);
    for (const auto& o : overrides) synth.setParameter(o.idx, o.val);
    // Transport state as of frame 0, so the notes below are pressed against the
    // same state the first block will render with (playat= means "stopped" here).
    synth.setTempo(tempo, playing && playAtTime <= 0.0);

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
    std::vector<char> schedNotifyDone(schedNotify.size(), 0);
    std::vector<char> schedPedalDone(schedPedal.size(), 0);
    std::vector<char> schedPanicDone(schedPanic.size(), 0);
    std::vector<char> schedSoundOffDone(schedSoundOff.size(), 0);
    std::vector<char> schedPressureDone(schedPressure.size(), 0);
    std::vector<char> schedProgramDone(schedProgram.size(), 0);
    std::vector<char> schedLoopDone(schedLoops.size(), 0);
    std::vector<double> schedLoopShift(schedLoops.size(), 0.0);

    // Optional meter log (meters=<path>): one line per rendered block.
    FILE* meterFile = nullptr;
    if (!meterPath.empty())
    {
        meterFile = std::fopen(meterPath.c_str(), "w");
        if (!meterFile)
        {
            std::fprintf(stderr, "cannot open meter log %s\n", meterPath.c_str());
            return 2;
        }
    }

    bool released = false;
    for (int pos = 0; pos < totalFrames; )
    {
        int n = std::min(blockSize, totalFrames - pos);

        // Factory-program loads run FIRST in the block, so a setat= scheduled at the
        // same time acts as a post-load override (the same ordering preset_render
        // uses for its auditioning overrides). The program's own notifyProgramChange
        // still lands before this block's processBlock, so the snapshot that
        // consumes it sees the finished patch either way.
        for (size_t s = 0; s < schedProgram.size(); ++s)
        {
            if (schedProgramDone[s]) continue;
            if (pos >= (int)(schedProgram[s].time * sampleRate))
            {
                loadFactoryProgram(synth, schedProgram[s].index);
                schedProgramDone[s] = 1;
            }
        }

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

        // Signal a program change AFTER this block's parameter writes, exactly as
        // the shell does once a preset's parameters have all been pushed.
        for (size_t s = 0; s < schedNotify.size(); ++s)
        {
            if (schedNotifyDone[s]) continue;
            if (pos >= (int)(schedNotify[s] * sampleRate))
            {
                synth.notifyProgramChange();
                schedNotifyDone[s] = 1;
            }
        }

        // Sustain-pedal edges come BEFORE this block's note events: a pedal-down
        // scheduled at the same time as a key-up has to already be down for the
        // note-off to be captured, which is exactly the ordering a MIDI stream
        // gives when the pedal event precedes the note event in the buffer.
        for (size_t s = 0; s < schedPedal.size(); ++s)
        {
            if (schedPedalDone[s]) continue;
            if (pos >= (int)(schedPedal[s].time * sampleRate))
            {
                synth.sustainPedal(schedPedal[s].down);
                schedPedalDone[s] = 1;
            }
        }

        // Pressure messages (0xA0 per note / 0xD0 channel-wide).
        for (size_t s = 0; s < schedPressure.size(); ++s)
        {
            if (schedPressureDone[s]) continue;
            if (pos >= (int)(schedPressure[s].time * sampleRate))
            {
                if (schedPressure[s].note >= 0)
                    synth.polyAftertouch(schedPressure[s].note, schedPressure[s].value);
                else
                    synth.aftertouch(schedPressure[s].value);
                schedPressureDone[s] = 1;
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

        // Panic (CC123 / CC120) last, so it overrides every note event of this block.
        for (size_t s = 0; s < schedPanic.size(); ++s)
        {
            if (schedPanicDone[s]) continue;
            if (pos >= (int)(schedPanic[s] * sampleRate))
            {
                synth.allNotesOff();
                schedPanicDone[s] = 1;
            }
        }
        for (size_t s = 0; s < schedSoundOff.size(); ++s)
        {
            if (schedSoundOffDone[s]) continue;
            if (pos >= (int)(schedSoundOff[s] * sampleRate))
            {
                synth.allSoundOff();
                schedSoundOffDone[s] = 1;
            }
        }

        // Transport state for THIS block. With playat= the host reports "stopped"
        // until that time and parks the playhead at songpos, then rolls from it —
        // so the engines see a free -> host-locked transition mid-render. Without
        // it, nowPlaying == playing and rolled == elapsed, exactly as before.
        const double tNow = (double)pos / sampleRate;
        const bool nowPlaying = playing && (playAtTime < 0.0 || tNow >= playAtTime);
        synth.setTempo(tempo, nowPlaying);

        // Host phase-lock: feed the song position for THIS block's start frame
        // (pos = frames already rendered), so the block-start beat is correct
        // across the release split. Unset songpos leaves the engine free-running.
        if (haveSongPos)
        {
            const double rolled = (playAtTime < 0.0) ? tNow
                                                     : (nowPlaying ? tNow - playAtTime : 0.0);
            double songBeat = songPosStart + rolled * tempo / 60.0;
            // Loop wraps / seeks: each fired event shifts every later position by
            // however far the playhead moved, so the timeline stays continuous on
            // either side of the jump. The shift is referenced to the SCHEDULED
            // jump time rather than the block boundary the event happens to fire
            // on (same reasoning as playat=), so the modelled timeline is
            // buffer-size independent even though the engine still sees the jump
            // arrive on a boundary — which is the thing under test.
            double applied = 0.0;
            for (size_t s = 0; s < schedLoops.size(); ++s)
            {
                if (!schedLoopDone[s])
                {
                    if (pos < (int)(schedLoops[s].time * sampleRate)) continue;
                    const double jumpT = schedLoops[s].time;
                    const double rolledAtJump = (playAtTime < 0.0)
                        ? jumpT : (jumpT > playAtTime ? jumpT - playAtTime : 0.0);
                    schedLoopShift[s] = songPosStart + rolledAtJump * tempo / 60.0
                                      - applied - schedLoops[s].beats;
                    schedLoopDone[s] = 1;
                }
                applied += schedLoopShift[s];
            }
            synth.setSongPosition(songBeat - applied, true);
        }

        synth.processBlock(bufL.data(), bufR.data(), n);
        if (meterFile != nullptr)
        {
            // Read the published observables, not the buffer: the point is what
            // the shell hands the host, floor and all.
            if (std::fprintf(meterFile, "%.9g %.9g %.9g\n", (double)pos / sampleRate,
                             (double)synth.getOutputLevelL(),
                             (double)synth.getOutputLevelR()) < 0)
            {
                std::fprintf(stderr, "short write to meter log %s\n", meterPath.c_str());
                std::exit(2);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            interleaved[(size_t)((pos + i) * 2 + 0)] = bufL[(size_t)i];
            interleaved[(size_t)((pos + i) * 2 + 1)] = bufR[(size_t)i];
        }
        pos += n;
    }

    if (meterFile != nullptr && std::fclose(meterFile) != 0)
    {
        std::fprintf(stderr, "error closing meter log %s\n", meterPath.c_str());
        return 2;
    }

    writeFloatWav(outPath, interleaved, (int)sampleRate);
    std::fprintf(stderr, "wrote %s  (%d frames @ %.0f Hz, os=%dx)\n", outPath, totalFrames, sampleRate, osFactor);
    return 0;
}
