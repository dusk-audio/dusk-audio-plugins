// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: saved Match EQ data is untrusted. Rejection must preserve all of the
// previous learned state; valid empty states must also remove an old correction.
#include "../core/MultiQMatch.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Match = duskaudio::MultiQMatch;
using Bytes = std::vector<uint8_t>;
constexpr const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int checks = 0, failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

// Independent base64 helpers let the tests corrupt individual MQM1 fields.
Bytes decode(const std::string& text)
{
    Bytes bytes;
    uint32_t bits = 0;
    int count = 0;
    for (char c : text)
    {
        if (c == '=') break;
        const char* found = std::strchr(alphabet, c);
        if (found == nullptr) return {};
        bits = (bits << 6) | static_cast<uint32_t>(found - alphabet);
        count += 6;
        if (count >= 8) { count -= 8; bytes.push_back(static_cast<uint8_t>(bits >> count)); }
    }
    return bytes;
}

std::string encode(const Bytes& bytes)
{
    std::string result;
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const uint32_t word = (static_cast<uint32_t>(bytes[i]) << 16)
            | (i + 1 < bytes.size() ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0u)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0u);
        result += alphabet[(word >> 18) & 63u];
        result += alphabet[(word >> 12) & 63u];
        result += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63u] : '=';
        result += i + 2 < bytes.size() ? alphabet[word & 63u] : '=';
    }
    return result;
}

template <typename T>
void put(Bytes& bytes, size_t offset, T value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::unique_ptr<Match> fixture()
{
    auto match = std::make_unique<Match>();
    match->prepare(48000.0, 256);
    std::array<float, 16384> current{}, reference{};
    uint32_t random = 0x183721u;
    float filtered = 0.0f;
    for (size_t i = 0; i < current.size(); ++i)
    {
        random = random * 1664525u + 1013904223u;
        current[i] = 0.25f * (static_cast<float>(random >> 8) / 8388608.0f - 1.0f);
        filtered += 0.1f * (current[i] - filtered);
        reference[i] = filtered;
    }
    match->requestStartLearnCurrent(); match->audioThreadSync();
    match->feedLearningBlock(current.data(), static_cast<int>(current.size()));
    match->requestStartLearnReference(); match->audioThreadSync();
    match->feedLearningBlock(reference.data(), static_cast<int>(reference.size()));
    match->requestStopLearning(); match->audioThreadSync();
    check(match->hasCurrentSpectrum() && match->hasReferenceSpectrum(), "fixture really learned both spectra");
    check(match->computeCorrection(3.0f, 1.0f, 12.0f, 12.0f, true), "fixture builds a real correction");
    match->audioThreadSync();
    check(match->getLatencySamples() == Match::CONV_HOP, "fixture activates the correction");
    return match;
}

void invalidStates(const std::string& valid)
{
    // MQM1: magic; five int32s (current-valid, frames, reference-valid,
    // frames, correction-valid); two double spectra; float curve and FIR.
    constexpr size_t currentOffset = 24;
    constexpr size_t referenceOffset = currentOffset + Match::NUM_BINS * sizeof(double);
    constexpr size_t curveOffset = referenceOffset + Match::NUM_BINS * sizeof(double);
    constexpr size_t firOffset = curveOffset + Match::NUM_BINS * sizeof(float);
    const Bytes bytes = decode(valid);
    check(encode(bytes) == valid, "independent codec round-trips the real state");
    std::vector<std::pair<std::string, std::string>> cases;
    auto mutated = [&](const char* label, size_t offset, auto value) {
        Bytes changed = bytes; put(changed, offset, value);
        cases.emplace_back(label, encode(changed));
    };
    mutated("wrong magic", 0, uint8_t{'X'});
    for (size_t offset : {size_t{4}, size_t{12}, size_t{20}})
        mutated("non-boolean presence flag", offset, int32_t{2});
    for (size_t offset : {size_t{8}, size_t{16}})
    {
        mutated("negative frame count", offset, int32_t{-1});
        mutated("valid spectrum with too few frames", offset, int32_t{2});
    }
    for (size_t offset : {currentOffset, referenceOffset})
    {
        mutated("negative spectral power", offset, -1.0);
        mutated("NaN spectral power", offset, std::numeric_limits<double>::quiet_NaN());
        mutated("infinite spectral power", offset, std::numeric_limits<double>::infinity());
    }
    mutated("NaN correction curve", curveOffset, std::numeric_limits<float>::quiet_NaN());
    mutated("infinite FIR", firOffset, std::numeric_limits<float>::infinity());
    Bytes truncated = bytes; truncated.pop_back();
    cases.emplace_back("truncated state", encode(truncated));
    Bytes extra = bytes; extra.push_back(0);
    cases.emplace_back("trailing binary data", encode(extra));
    cases.emplace_back("invalid base64 character", valid.substr(0, 20) + "!" + valid.substr(20));
    cases.emplace_back("data after base64 padding", valid + "AAAA");
    cases.emplace_back("early base64 padding", valid.substr(0, 20) + "=" + valid.substr(20));

    auto target = std::make_unique<Match>(); target->prepare(48000.0, 256);
    for (const auto& item : cases)
    {
        check(target->deserialize(valid), "valid correction loads before a rejection test");
        target->audioThreadSync();
        const std::string before = target->serialize();
        const bool accepted = target->deserialize(item.second);
        check(!accepted, item.first.c_str());
        check(target->serialize() == before, "rejection preserves the complete previous state");
        target->audioThreadSync();
        check(target->hasCorrectionCurve() && target->getLatencySamples() == Match::CONV_HOP,
              "rejection leaves the existing correction active");
    }
}

void restoreEmpty(const std::string& valid)
{
    auto target = std::make_unique<Match>(); target->prepare(48000.0, 256);
    Match empty; empty.prepare(48000.0, 256);
    const std::string emptyState = empty.serialize();
    check(target->deserialize(valid), "valid populated state restores");
    target->audioThreadSync();
    check(target->serialize() == valid, "full valid state round-trips byte for byte");
    const std::string wrapped = " \n" + valid.substr(0, 72) + "\r\n" + valid.substr(72) + "\t";
    check(target->deserialize(wrapped) && target->serialize() == valid,
          "legacy whitespace-wrapped base64 still restores");
    check(target->deserialize(emptyState), "valid empty state restores");
    target->audioThreadSync();
    std::array<float, 256> signal{};
    for (int i = 0; i < 8; ++i) target->process(signal.data(), nullptr, false, static_cast<int>(signal.size()));
    check(!target->hasCorrectionCurve() && target->getLatencySamples() == 0,
          "empty restore removes the old correction and its latency");
    check(target->serialize() == emptyState, "empty restore preserves its exact learned-state content");
    // Cover a second load before audioThreadSync consumed the first activation.
    check(target->deserialize(valid) && target->deserialize(emptyState), "queued activation can be replaced by empty state");
    target->audioThreadSync();
    for (int i = 0; i < 8; ++i) target->process(signal.data(), nullptr, false, static_cast<int>(signal.size()));
    check(target->getLatencySamples() == 0, "a queued old activation cannot revive an empty restore");

    Bytes learnedOnly = decode(valid);
    put(learnedOnly, 20, int32_t{0});
    const std::string withoutCorrection = encode(learnedOnly);
    check(target->deserialize(valid), "correction can be loaded again after a clear");
    target->audioThreadSync();
    check(target->deserialize(withoutCorrection), "a learned-only state can replace the active correction");
    target->audioThreadSync();
    for (int i = 0; i < 8; ++i) target->process(signal.data(), nullptr, false, static_cast<int>(signal.size()));
    check(target->getLatencySamples() == 0 && !target->hasCorrectionCurve()
              && target->hasCurrentSpectrum() && target->hasReferenceSpectrum(),
          "deactivation keeps the incoming learned spectra for future correction");
    check(target->serialize() == withoutCorrection, "learned-only restore preserves all of its incoming content");
}
}

int main()
{
    const auto learned = fixture();
    const std::string valid = learned->serialize();
    invalidStates(valid);
    restoreEmpty(valid);
    std::printf("Multi-Q Match state: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
