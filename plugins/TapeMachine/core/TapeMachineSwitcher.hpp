#pragma once

#include "TapeMachineDSP.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace duskaudio
{
//==============================================================================
// TapeMachineSwitcher — the tape machine as the plugin runs it: two paths, and a morph from
// one to the other when the deck is switched.
//
// The reference decks do not cut when a setting is switched: for a few tens of milliseconds
// the old setting and the new one are both there. A single path cannot do that. Its filters
// keep their state while their coefficients jump (the output bursts), or the state is cleared
// (the output has a gap). So there are two paths here. One runs. At a SWITCH the other one is
// restarted on the new settings, from an empty state, and the output goes from the first to
// the second over kFadeSec. The first keeps running on its OLD settings until the fade is
// over, so neither path is handed a change it would have to survive.
//
// A switch is:
//   - a change of machine, speed or calibration;
//   - with Gain Link on, Input dropping by more than kInputDropDb in one block.
// Everything else a host or a preset sets (tape type, EQ standard, signal path, head width,
// other moves of Input, Output, bias, the filters, the reproduce EQ, ...) goes to the
// running path as it always did and is smoothed there.
//
// Gain Link off is the exception that shapes the rest. Unlinked, Input and Output are a
// pair, and a preset sets them one after the other, possibly blocks apart: a path that is
// kept on the Input of one preset and the Output of another is tens of dB off for as long as
// it is kept there. So Input, Output and the link itself are never held back from a path
// that is unlinked: it takes all three in the block they arrive in, also while it waits to
// be faded out, fades out, or comes in while a second switch is asked for, as a single path
// would. (A drop of Input is then the path's own business, as it was before there were
// two.) Once the link is on in such a path it is linked, and a path that is linked needs
// none of this and is kept as it is: its output follows its own Input.
// One consequence: for a preset loaded from an unlinked setting that turns the link on and
// lowers Input by more than kInputDropDb, the morph does not avoid the cut. The kept path
// takes that Input with the link, the drop is its clean start, and the fade to the new path
// follows. Avoiding the cut there would take a kept path that makes no clean start of its
// own, which is a change to the path, not to this class.
// In short: every running path is either linked, or has the levels that are asked for now.
//
// A preset reaches a plugin as a run of single values, and a host may deliver them over more
// than one block. So a switch is not acted on in the block it arrives in: from then on
// nothing reaches the running path (the levels and the link of an unlinked one excepted, see
// above), and the fade starts at the first block that finds the settings as the block before
// left them, which is the next block if nothing else arrives, and kMaxWaits blocks after
// that at the latest. The new path then starts on everything set by then.
// During a fade the incoming path takes what is set, except while a second switch is asked
// for: then it keeps what it has, and the second switch is the next fade.
//
// Each block takes ONE reading of the settings (`now`) and decides and acts on that alone.
//
// What is left to the paths, and which of a path's own clean starts (TapeMachineDSP::
// processBlock) can still happen here:
//   - Bypass and the Thru path: the running path passes the input through and starts clean
//     on its way back, as before. Nothing is morphed while the input is passed through; a
//     change of the deck made then goes to the path and is there when it comes back.
//   - Bypass or Thru switched on DURING a fade reaches the incoming path only: the output is
//     the dry input once the fade is over, not at once.
//   - A path never sees a change of the deck in running programme, nor, with Gain Link on, a
//     drop of Input beyond kInputDropDb: those are switches, and its clean start for them
//     cannot fire. With Gain Link off a drop of Input reaches the paths and can.
//
// A block larger than the one prepared for is worked through in pieces of the prepared size,
// and the wait above counts those pieces.
//
// Threads: the setters are TapeMachineSettings' and may be called from any thread; they
// write the settings that are asked for. prepare() and reset() are called while no block is
// being processed. The meter getters and latencySamples() may be called from any thread.
// processBlock() is the audio thread's.
//==============================================================================
class TapeMachineSwitcher : public TapeMachineSettings
{
public:
    static constexpr float kFadeSec     = 0.06f;    // the reference decks: 30 to 50 ms, up to 95
    static constexpr float kInputDropDb = 6.0f;     // linked, a drop of Input beyond this is a switch
    static constexpr int   kMaxWaits    = 2;        // further blocks to wait for the rest of a preset

    void prepare (double sampleRate, int maxBlockSize);   // may allocate; setup thread only
    void reset();
    void processBlock (const float* const* inputs, float* const* outputs, int nCh, int nSamples) noexcept;
    int  latencySamples() const noexcept;

    float getVuL() const noexcept      { return metered().getVuL(); }
    float getVuR() const noexcept      { return metered().getVuR(); }
    float getInVuL() const noexcept    { return metered().getInVuL(); }
    float getInVuR() const noexcept    { return metered().getInVuR(); }
    float getInPeakL() const noexcept  { return metered().getInPeakL(); }
    float getInPeakR() const noexcept  { return metered().getInPeakR(); }
    float getOutPeakL() const noexcept { return metered().getOutPeakL(); }
    float getOutPeakR() const noexcept { return metered().getOutPeakR(); }

    // For tests, on the audio thread: fades started since prepare(), and whether one is running.
    int  fadesStarted() const noexcept { return fades; }
    bool isFading() const noexcept     { return fading; }

private:
    const TapeMachineDSP& metered() const noexcept { return path[running.load (std::memory_order_relaxed)]; }
    bool isSwitch() const noexcept;
    void follow() noexcept;
    void giveLevels (TapeMachineDSP& kept) const noexcept;
    void decide() noexcept;
    void startFade() noexcept;
    void processChunk (const float* const* inputs, float* const* outputs, int nCh, int nSamples) noexcept;

    TapeMachineDSP path[2];
    std::atomic<int> running { 0 };     // the path that runs, and during a fade the one going out
    TapeMachineSettings now;            // this block's one reading of the settings that are asked for
    std::vector<float> incomingL, incomingR;    // the incoming path's block during a fade [maxBlock]
    int maxBlock = 512;

    bool fading = false;
    int  fadePos = 0, fadeLen = 1;
    int  fades = 0;

    // What the running path (during a fade: the incoming one) is on: the deck it was started
    // on, and the Input it was last given.
    uint32_t runningKey = 0;
    float    runningInputDb = 0.0f;

    // A switch that is waiting for the rest of its preset.
    bool     pending = false;
    int      waits = 0;
    uint32_t seenPrint = 0;
};
} // namespace duskaudio
