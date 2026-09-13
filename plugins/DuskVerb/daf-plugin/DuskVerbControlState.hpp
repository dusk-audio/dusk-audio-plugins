// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
#pragma once
#include "DuskVerbParams.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace duskverb
{
// Immutable complete recalls and independently stamped automation. A reader
// never waits for a writer. Only non-realtime state publication allocates or
// reclaims nodes; host program loading uses precomputed factory data.
class ControlState
{
public:
    struct Sound
    {
        std::array<float, kNumParams> params{};
        DuskVerbDSP::SixAPBrightnessState sixAP{};
        const FactoryPreset* preset = nullptr;
        uint32_t epoch = 1;
        uint32_t recallEpoch = 1;
        int program = -1;
        bool edited = false;
    };

    ControlState()
    {
        const StateValues initial = makeDefaultState();
        for (int i = 0; i < kNumParams; ++i)
        {
            automation_[i].store(pack(0, initial.params[i]));
        }
        current_.store(new Node(initial, 1));
        for (const auto& preset : getFactoryPresets())
        {
            Factory data;
            data.preset = &preset;
            applyFactoryPresetToHostParameters(preset, [&](int i, float v) {
                data.params[i] = v; data.writes[i] = true;
            });
            data.sixAP.densityBaseline = preset.sixAPDensityBaseline;
            data.sixAP.bloomCeiling = preset.sixAPBloomCeiling;
            data.sixAP.earlyMix = preset.sixAPEarlyMix;
            data.sixAP.outputTrim = preset.sixAPOutputTrim;
            std::copy_n(preset.sixAPBloomStagger, 6, data.sixAP.bloomStagger);
            factories_.push_back(data);
        }
    }
    ~ControlState() { delete current_.load(); }

    void setParameter(int i, float host) noexcept
    {
        const float snapped = snapHostValue(paramDesc(i), host);
        // Hosts echo the values requested by a complete recall. An unchanged
        // echo is not an edit and must not replace the recalled identity.
        if (parameter(i) == snapped) return;
        automation_[i].store(pack(epoch_.load(std::memory_order_seq_cst), snapped), std::memory_order_seq_cst);
    }

    float parameter(int i) const noexcept
    {
        Reader guard(*this);
        const Node* node = current_.load(std::memory_order_seq_cst);
        const uint64_t program = program_.load(std::memory_order_seq_cst);
        return parameterFor(*node, program, i);
    }

    void loadProgram(uint32_t index) noexcept
    {
        if (index >= factories_.size()) return;
        const uint32_t epoch = epoch_.fetch_add(1, std::memory_order_seq_cst) + 1;
        const uint64_t request = (uint64_t(epoch) << 32) | (index + 1);
        uint64_t old = program_.load(std::memory_order_seq_cst);
        while (newer(epoch, uint32_t(old >> 32))
            && !program_.compare_exchange_weak(old, request, std::memory_order_seq_cst)) {}
    }

    // Non-realtime, after complete validation. Concurrent state callers are
    // serialized here; audio and parameter callbacks never acquire this mutex.
    void publish(const StateValues& state)
    {
        std::lock_guard<std::mutex> lock(writer_);
        auto next = std::make_unique<Node>(state, 0);
        // Allocate retirement storage before publishing. If allocation fails,
        // the current sound and its owner remain untouched.
        retired_.emplace_back(nullptr);
        const auto previous = sound();
        next->epoch = epoch_.fetch_add(1, std::memory_order_seq_cst) + 1;
        bool same = previous.params == state.params && previous.preset == next->preset;
        for (int i = 0; i < 10; ++i)
            same = same && *sixApSlot(previous.sixAP, i) == *sixApSlot(state.sixAP, i);
        // Renaming/saving an unchanged sound must not clear its reverb tail.
        next->recallEpoch = same ? previous.recallEpoch : next->epoch;
        retired_.back().reset(current_.exchange(next.release(), std::memory_order_seq_cst));
        // Readers increment BEFORE loading current_. In the single global
        // seq_cst order, readers starting after this check can only see the new
        // node; readers which could hold a retired node prevent reclamation.
        if (readers_.load(std::memory_order_seq_cst) == 0) retired_.clear();
    }

    Sound sound() const noexcept
    {
        Reader guard(*this);
        return soundFor(*current_.load(std::memory_order_seq_cst),
                        program_.load(std::memory_order_seq_cst));
    }

    StateValues snapshot() const
    {
        Reader guard(*this);
        const Node& node = *current_.load(std::memory_order_seq_cst);
        const Sound sound = soundFor(node, program_.load(std::memory_order_seq_cst));
        StateValues state = node.state;
        state.params = sound.params;
        state.sixAP = sound.sixAP;
        state.presetName = sound.preset ? sound.preset->name : "";
        if (newer(sound.epoch, node.epoch)) state.userName.clear();
        state.edited = sound.edited;
        return state;
    }

private:
    struct Node
    {
        StateValues state;
        uint32_t epoch;
        uint32_t recallEpoch;
        const FactoryPreset* preset;
        Node(const StateValues& s, uint32_t e)
            : state(s), epoch(e), recallEpoch(e), preset(factoryPresetByName(s.presetName)) {}
    };
    struct Factory
    {
        std::array<float, kNumParams> params{};
        std::array<bool, kNumParams> writes{};
        DuskVerbDSP::SixAPBrightnessState sixAP{};
        const FactoryPreset* preset = nullptr;
    };
    struct Reader
    {
        const ControlState& owner;
        explicit Reader(const ControlState& o) noexcept : owner(o)
        { owner.readers_.fetch_add(1, std::memory_order_seq_cst); }
        ~Reader() { owner.readers_.fetch_sub(1, std::memory_order_seq_cst); }
    };
    static bool newer(uint32_t a, uint32_t b) noexcept { return int32_t(a - b) > 0; }
    static uint64_t pack(uint32_t epoch, float value) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (uint64_t(epoch) << 32) | bits;
    }
    static float unpack(uint64_t packed) noexcept
    {
        uint32_t bits = uint32_t(packed);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    const Factory* factoryFor(const Node& node, uint64_t program) const noexcept
    {
        const uint32_t index = uint32_t(program);
        return index && index <= factories_.size() && newer(uint32_t(program >> 32), node.epoch)
             ? &factories_[index - 1] : nullptr;
    }
    float parameterFor(const Node& node, uint64_t program, int i) const noexcept
    {
        const Factory* factory = factoryFor(node, program);
        const bool written = factory && factory->writes[i];
        const uint32_t baseEpoch = written ? uint32_t(program >> 32) : node.epoch;
        const uint64_t automation = automation_[i].load(std::memory_order_seq_cst);
        // Equal stamps mean an edit made after this recall began publication.
        if (!newer(baseEpoch, uint32_t(automation >> 32))) return unpack(automation);
        return written ? factory->params[i] : node.state.params[i];
    }
    Sound soundFor(const Node& node, uint64_t program) const noexcept
    {
        Sound result;
        const Factory* factory = factoryFor(node, program);
        result.epoch = factory ? uint32_t(program >> 32) : node.epoch;
        result.recallEpoch = factory ? result.epoch : node.recallEpoch;
        result.sixAP = factory ? factory->sixAP : node.state.sixAP;
        result.preset = factory ? factory->preset : node.preset;
        result.program = -1;
        if (result.preset)
            result.program = int(result.preset - &getFactoryPresets().front());
        result.edited = !factory && node.state.edited;
        for (int i = 0; i < kNumParams; ++i)
        {
            result.params[i] = parameterFor(node, program, i);
            if (i != Bypass && !newer(result.epoch,
                       uint32_t(automation_[i].load(std::memory_order_seq_cst) >> 32)))
                result.edited = true;
        }
        return result;
    }

    static_assert(std::atomic<uint64_t>::is_always_lock_free, "Realtime parameter stamps must be lock-free");
    std::atomic<uint32_t> epoch_{1};
    std::atomic<uint64_t> program_{0};
    std::array<std::atomic<uint64_t>, kNumParams> automation_{};
    std::atomic<Node*> current_{nullptr};
    mutable std::atomic<unsigned> readers_{0};
    std::mutex writer_;
    std::vector<std::unique_ptr<Node>> retired_;
    std::vector<Factory> factories_;
};
} // namespace duskverb
