#pragma once

#include "MultiCompHelpers.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace duskaudio
{
// Delays the processed output up to the deepest mode's latency, so the latency
// reported to the host stays the same in every mode (MultiCompDSP::
// getHostLatencySamples()). A report that moved with the mode made VST3 and CLAP
// hosts restart the plugin on every mode change, and the AU host never heard of
// it. MultiCompDSP runs one on the wet path of every part of a block it
// processes (setHostLatencyPadding()), and one on Multiband's split-band dry,
// which the other modes feed() with their dry input; its other dry, bypass and
// listen paths reach the host latency in their own delay lines. Allocates in
// prepare() only; process(), feed() and clearChannel() are real-time safe.
class LatencyPad
{
public:
    void prepare(int maximumPadding, int maximumBlock)
    {
        for (auto& line : lines)
            line.assign(static_cast<size_t>(std::max(0, maximumPadding)) + 1, 0.0f);
        scratch.assign(static_cast<size_t>(std::max(1, maximumBlock)), 0.0f);
        write = 0;
    }

    void reset() noexcept
    {
        for (auto& line : lines) std::fill(line.begin(), line.end(), 0.0f);
        write = 0;
    }

    // One channel's history to silence (a channel that stopped being processed).
    void clearChannel(int channel) noexcept
    {
        if (channel >= 0 && channel < static_cast<int>(lines.size()))
            std::fill(lines[static_cast<size_t>(channel)].begin(), lines[static_cast<size_t>(channel)].end(), 0.0f);
    }

    // process() without the output: only the history advances.
    void feed(const float* const* in, int channels, int frames) noexcept
    {
        const int size = static_cast<int>(lines[0].size());
        if (size == 0 || channels <= 0) return;
        const int count = std::min(channels, static_cast<int>(lines.size()));
        int position = write;
        for (int ch = 0; ch < count; ++ch)
        {
            position = write;
            delayLineBlock(lines[static_cast<size_t>(ch)].data(), size, position, 0, in[ch], nullptr, frames);
        }
        write = position;
    }

    // The history is written whatever `padding` is, so a mode change that
    // raises it reads samples that really came before, not silence. Each
    // channel is the per-sample ring (write the sample, read `padding` back) run
    // a block at a time (delayLineBlock).
    void process(float* const* io, int channels, int frames, int padding) noexcept
    {
        const int size = static_cast<int>(lines[0].size());
        if (size == 0 || channels <= 0) return;
        const int delay = std::clamp(padding, 0, size - 1);
        const int count = std::min(channels, static_cast<int>(lines.size()));
        const int chunk = static_cast<int>(scratch.size());
        int position = write;
        for (int ch = 0; ch < count; ++ch)
        {
            position = write;
            for (int offset = 0; offset < frames; offset += chunk)
            {
                const int n = std::min(chunk, frames - offset);
                std::memcpy(scratch.data(), io[ch] + offset, static_cast<size_t>(n) * sizeof(float));
                delayLineBlock(lines[static_cast<size_t>(ch)].data(), size, position, delay, scratch.data(),
                               io[ch] + offset, n);
            }
        }
        write = position;
    }

private:
    std::array<std::vector<float>, 2> lines;
    std::vector<float> scratch;   // one channel's input (delayLineBlock's output must not alias it)
    int write = 0;
};
} // namespace duskaudio
