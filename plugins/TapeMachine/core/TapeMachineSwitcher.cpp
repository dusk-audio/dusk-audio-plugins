#include "TapeMachineSwitcher.hpp"

#include <algorithm>
#include <cmath>

namespace duskaudio
{
void TapeMachineSwitcher::prepare (double sampleRate, int maxBlockSize)
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0) sampleRate = 44100.0;
    if (maxBlockSize <= 0) maxBlockSize = 512;
    maxBlock = maxBlockSize;
    incomingL.assign (static_cast<size_t> (maxBlock), 0.0f);
    incomingR.assign (static_cast<size_t> (maxBlock), 0.0f);
    fadeLen = std::max (1, static_cast<int> (kFadeSec * static_cast<float> (sampleRate)));

    // Both paths are sized here; the idle one is restarted on its settings when its turn comes.
    now.copySettingsFrom (*this);
    for (TapeMachineDSP& p : path)
    {
        p.copySettingsFrom (now);
        p.prepare (sampleRate, maxBlock);
    }
    running.store (0, std::memory_order_relaxed);
    // Stored here only: a path's processing latency changes in prepare() alone, since its
    // oversampling factor is pinned (TapeMachineDSP's factorFromChoice). Were the factor to
    // follow the parameter again, this would have to be published per block.
    pathLatency.store (path[0].processingLatencySamples(), std::memory_order_relaxed);
    fades = 0;
    fading = false;
    fadePos = 0;
    pending = false;
    waits = 0;
    follow();
}

// The running path, given the settings that are asked for and reset; a fade is over.
void TapeMachineSwitcher::reset()
{
    TapeMachineDSP& runs = path[running.load (std::memory_order_relaxed)];
    now.copySettingsFrom (*this);
    runs.copySettingsFrom (now);
    runs.reset();
    fading = false;
    fadePos = 0;
    pending = false;
    waits = 0;
    follow();
}

int TapeMachineSwitcher::latencySamples() const noexcept
{
    // As a single path reports it: none while the input is passed through (bypass, Thru),
    // the processing path's otherwise (both paths have the same), from what is asked for.
    // Not so while the newest path is kept on its settings (in the wait, and in a fade while
    // a second switch is asked for): a kept path is processing, since one that passes the
    // input through is never kept (decide()), so the output is delayed whatever is asked.
    // Nothing of a path is read here: a path may be restarted while this is called.
    const bool through = ! newestKept.load (std::memory_order_relaxed) && isPassthrough();
    return through ? 0 : pathLatency.load (std::memory_order_relaxed);
}

// With Gain Link on, Input below what the newest path is on by more than kInputDropDb.
bool TapeMachineSwitcher::isInputDrop() const noexcept
{
    return now.isGainLinked() && runningInputDb - now.inputGainDb() > kInputDropDb;
}

// `now` against what the newest path is on.
bool TapeMachineSwitcher::isSwitch() const noexcept
{
    return now.topologyKey() != runningKey || isInputDrop();
}

// The newest path has been given `now`: that is what later readings are compared with.
void TapeMachineSwitcher::follow() noexcept
{
    runningKey = now.topologyKey();
    runningInputDb = now.inputGainDb();
    newestKept.store (false, std::memory_order_relaxed);
}

// For a path that is kept on its settings (waiting to be faded out, fading out, or coming in
// while a second switch is asked for): if it is unlinked it takes Input, Output and the link
// all the same, whatever is asked for. With the link it becomes a linked path, and is kept
// from then on. Returns whether the path took them.
bool TapeMachineSwitcher::giveLevels (TapeMachineDSP& kept) const noexcept
{
    if (kept.isGainLinked()) return false;
    kept.setInputGainDb (now.inputGainDb());
    kept.setOutputGainDb (now.outputGainDb());
    kept.setAutoComp (now.isGainLinked());
    return true;
}

// The newest path is kept on its settings for this block, the levels of an unlinked one
// excepted. An Input it takes is the Input it is on, and later drops are counted from there:
// otherwise a level it has already been given could pass for a drop, and a switch that was
// taken back for a switch still. Not so when the drop is the switch that is being waited
// for: counted from the new Input there would be no switch left to wait for.
void TapeMachineSwitcher::keep (TapeMachineDSP& newest) noexcept
{
    const bool dropIsTheSwitch = isInputDrop();
    if (giveLevels (newest) && ! dropIsTheSwitch) runningInputDb = now.inputGainDb();
    newestKept.store (true, std::memory_order_relaxed);
}

// Starts the idle path on `now`.
void TapeMachineSwitcher::startFade() noexcept
{
    const int from = running.load (std::memory_order_relaxed);
    TapeMachineDSP& next = path[1 - from];
    next.copySettingsFrom (now);
    next.restart();
    next.copyMeterStateFrom (path[from]);
    follow();
    fading = true;
    fadePos = 0;
    pending = false;
    waits = 0;
    ++fades;
}

// What this block does with the settings that are asked for.
void TapeMachineSwitcher::decide() noexcept
{
    now.copySettingsFrom (*this);       // the one reading
    const int runs = running.load (std::memory_order_relaxed);

    if (fading)
    {
        // The incoming path takes what is set, except while a second switch is asked for:
        // then it keeps what it has (half of the next preset would be worse than none of
        // it). Not if it is passing the input through (bypass or Thru switched on in this
        // fade): as below, it takes the switch and starts clean on its way back. The
        // outgoing path keeps its settings throughout. Unlinked levels reach both.
        giveLevels (path[runs]);
        if (! isSwitch() || path[1 - runs].isPassthrough())
        {
            path[1 - runs].copySettingsFrom (now);
            follow();
        }
        else
            keep (path[1 - runs]);
        return;
    }

    // While the input is passed through, and in the block it stops being passed through,
    // there is nothing to morph: the path takes the settings and starts clean on its way
    // back, as it always did. (Kept for a fade instead, the path would go on passing the
    // input through, and the fade would be from the dry input to the delayed new path.)
    if (! isSwitch() || now.isPassthrough() || path[runs].isPassthrough())
    {
        pending = false;
        path[runs].copySettingsFrom (now);
        follow();
        return;
    }

    // A switch. The running path is left on its old settings from here on (unlinked levels
    // excepted). Wait for the block that finds the settings as the block before left them:
    // the rest of a preset may still be on its way.
    keep (path[runs]);
    if (! pending)
    {
        pending = true;
        waits = 0;
        seen.copySettingsFrom (now);
        return;
    }
    if (! now.sameSettingsAs (seen) && waits < kMaxWaits)
    {
        ++waits;
        seen.copySettingsFrom (now);
        return;
    }
    startFade();
}

void TapeMachineSwitcher::processChunk (const float* const* inputs, float* const* outputs, int nCh, int nSamples) noexcept
{
    decide();
    const int runs = running.load (std::memory_order_relaxed);
    if (! fading)
    {
        path[runs].processBlock (inputs, outputs, nCh, nSamples);
        return;
    }

    // The incoming path first, into its own buffers: a host may hand over the same memory
    // for input and output, and the outgoing path writes there.
    float* incoming[2] = { incomingL.data(), incomingR.data() };
    path[1 - runs].processBlock (inputs, incoming, nCh, nSamples);
    path[runs].processBlock (inputs, outputs, nCh, nSamples);

    // Equal gain, a raised cosine: the two paths carry the same programme and are in step
    // (same latency), so their sum at equal gains is the programme again.
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* out = outputs[ch];
        const float* in = incoming[ch];
        for (int i = 0; i < nSamples; ++i)
        {
            const int pos = fadePos + i;
            if (pos >= fadeLen) { out[i] = in[i]; continue; }       // over: the incoming path alone, to the bit
            const float t = static_cast<float> (pos) / static_cast<float> (fadeLen);
            const float g = 0.5f - 0.5f * std::cos (3.14159265358979f * t);
            out[i] += g * (in[i] - out[i]);
        }
    }
    fadePos += nSamples;
    if (fadePos >= fadeLen)
    {
        fading = false;
        running.store (1 - runs, std::memory_order_relaxed);
    }
}

void TapeMachineSwitcher::processBlock (const float* const* inputs, float* const* outputs, int nCh, int nSamples) noexcept
{
    if (nSamples <= 0) return;
    nCh = clampI (nCh, 1, 2);
    if (nSamples <= maxBlock)
    {
        processChunk (inputs, outputs, nCh, nSamples);
        return;
    }
    // A block larger than the one prepared for: in pieces of the prepared size.
    for (int done = 0; done < nSamples; )
    {
        const int n = std::min (nSamples - done, maxBlock);
        const float* in[2] = { inputs[0] + done, nCh > 1 ? inputs[1] + done : inputs[0] + done };
        float* out[2] = { outputs[0] + done, nCh > 1 ? outputs[1] + done : outputs[0] + done };
        processChunk (in, out, nCh, n);
        done += n;
    }
}
} // namespace duskaudio
