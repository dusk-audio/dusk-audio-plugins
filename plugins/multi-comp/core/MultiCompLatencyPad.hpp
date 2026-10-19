#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace duskaudio
{
// Delays the processed output up to the deepest mode's latency, so the latency
// reported to the host stays the same in every mode (MultiCompDSP::
// getHostLatencySamples()). A report that moved with the mode made VST3 and CLAP
// hosts restart the plugin on every mode change, and the AU host never heard of
// it. Allocates in prepare() only; process() is real-time safe.
class LatencyPad
{
public:
    void prepare(int maximumPadding)
    {
        for (auto& line : lines)
            line.assign(static_cast<size_t>(std::max(0, maximumPadding)) + 1, 0.0f);
        write = 0;
    }

    void reset() noexcept
    {
        for (auto& line : lines) std::fill(line.begin(), line.end(), 0.0f);
        write = 0;
    }

    // The history is written whatever `padding` is, so a mode change that
    // raises it reads samples that really came before, not silence.
    void process(float* const* io, int channels, int frames, int padding) noexcept
    {
        const int size = static_cast<int>(lines[0].size());
        if (size == 0 || channels <= 0) return;
        const int delay = std::clamp(padding, 0, size - 1);
        const int count = std::min(channels, static_cast<int>(lines.size()));
        int position = write;
        for (int ch = 0; ch < count; ++ch)
        {
            auto& line = lines[static_cast<size_t>(ch)];
            position = write;
            for (int i = 0; i < frames; ++i)
            {
                line[static_cast<size_t>(position)] = io[ch][i];
                io[ch][i] = line[static_cast<size_t>((position - delay + size) % size)];
                position = (position + 1) % size;
            }
        }
        write = position;
    }

private:
    std::array<std::vector<float>, 2> lines;
    int write = 0;
};
} // namespace duskaudio
