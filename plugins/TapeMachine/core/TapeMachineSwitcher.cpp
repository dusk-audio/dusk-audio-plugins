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
    // the processing path's otherwise. Both paths have the same.
    return isPassthrough() ? 0 : path[0].processingLatencySamples();
}

// `now` against what the running path is on.
bool TapeMachineSwitcher::isSwitch() const noexcept
{
    return now.topologyKey() != runningKey
        || (now.isGainLinked() && runningInputDb - now.inputGainDb() > kInputDropDb);
}

// The running path has been given `now`: that is what later readings are compared with.
void TapeMachineSwitcher::follow() noexcept
{
    runningKey = now.topologyKey();
    runningInputDb = now.inputGainDb();
}

// For a path that is kept on its settings (waiting to be faded out, fading out, or coming in
// while a second switch is asked for): if it is unlinked it takes Input, Output and the link
// all the same, whatever is asked for. With the link it becomes a linked path, and is kept
// from then on.
void TapeMachineSwitcher::giveLevels (TapeMachineDSP& kept) const noexcept
{
    if (kept.isGainLinked()) return;
    kept.setInputGainDb (now.inputGainDb());
    kept.setOutputGainDb (now.outputGainDb());
    kept.setAutoComp (now.isGainLinked());
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
        // it). The outgoing path keeps its settings throughout. Unlinked levels reach both.
        giveLevels (path[runs]);
        if (! isSwitch())
        {
            path[1 - runs].copySettingsFrom (now);
            follow();
        }
        else
            giveLevels (path[1 - runs]);
        return;
    }

    // While the input is passed through there is nothing to morph: the path takes the
    // settings and starts clean on its way back, as it always did.
    if (! isSwitch() || now.isPassthrough())
    {
        pending = false;
        path[runs].copySettingsFrom (now);
        follow();
        return;
    }

    // A switch. The running path is left on its old settings from here on (unlinked levels
    // excepted). Wait for the block that finds the settings as the block before left them:
    // the rest of a preset may still be on its way.
    giveLevels (path[runs]);
    const uint32_t print = now.everySettingPrint();
    if (! pending)
    {
        pending = true;
        waits = 0;
        seenPrint = print;
        return;
    }
    if (print != seenPrint && waits < kMaxWaits)
    {
        ++waits;
        seenPrint = print;
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
