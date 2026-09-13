// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbTextParse.hpp — framework-free replacements for the two juce::String
// facilities the DUSKVERB_* tuning-sweep overrides in
// FactoryPreset::applyEngineConfig() used to depend on:
//
//   juce::String(s).getFloatValue()                  -> parseFloat(s)
//   juce::StringArray t; t.addTokens(s, ",", "")     -> Tokens t(s, ',')
//
// Both are consumed from applyEngineConfig(), which runs on the AUDIO thread
// (performPresetSwap). juce::StringArray heap-allocates; Tokens does not — it
// is a fixed-capacity split into an inline buffer, which is strictly safer than
// what it replaces. An over-long override string is truncated rather than
// allocating; the sweep strings are a handful of CSV floats.
//
// parseFloat() goes through strtod_l with a "C" locale, which is exactly what
// juce::CharacterFunctions::readDoubleValue() does after normalising the digits
// it accepts, so a sweep string parses to the same float in both builds. (JUCE
// additionally truncates past 18 significant digits and rejects a few spellings
// strtod accepts, e.g. hex floats; no tuning string uses either.)

#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <locale.h>
#if defined(__APPLE__)
  #include <xlocale.h>   // strtod_l lives here on macOS
#endif

namespace duskverb
{

inline float parseFloat(const char* s) noexcept
{
    if (s == nullptr || *s == '\0') return 0.0f;
#if defined(_WIN32)
    static _locale_t cLocale = _create_locale(LC_ALL, "C");
    char* end = nullptr;
    const double v = _strtod_l(s, &end, cLocale);
#else
    static locale_t cLocale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(0));
    char* end = nullptr;
    const double v = strtod_l(s, &end, cLocale);
#endif
    if (end == s) return 0.0f;
    return static_cast<float>(v);
}

inline int parseInt(const char* s) noexcept
{
    if (s == nullptr) return 0;
    return static_cast<int>(std::strtol(s, nullptr, 10));
}

// juce::StringArray::addTokens(text, oneBreakChar, "") semantics:
//   - an empty input yields zero tokens
//   - otherwise the token count is (number of break characters + 1); empty
//     tokens between adjacent separators are kept
// Capacity: the widest sweep string in use is DUSKVERB_DIFFER, which carries
// 2 header values plus up to 24 time/gain pairs = 50 tokens; DUSKVERB_FDN_DELAYS
// carries 32. 96 tokens / 1 KB of text leaves room and still fits comfortably on
// an audio-thread stack (the juce::StringArray it replaces heap-allocated).
// Beyond the cap the split TRUNCATES: every caller either checks an exact
// t.size() (so a truncated override is ignored outright) or reads a fixed
// prefix, so the failure mode is "the override does not apply", never a
// misparse.
class Tokens
{
public:
    static constexpr int kMaxTokens = 96;
    static constexpr int kBufSize   = 1024;

    Tokens() = default;
    Tokens(const char* text, char breakChar) noexcept { split(text, breakChar); }

    void split(const char* text, char breakChar) noexcept
    {
        count_ = 0;
        used_  = 0;
        buf_[0] = '\0';
        if (text == nullptr || *text == '\0') return;

        int tokenStart = 0;
        for (const char* p = text;; ++p)
        {
            const char c = *p;
            if (c == breakChar || c == '\0')
            {
                if (count_ < kMaxTokens)
                {
                    offset_[count_] = tokenStart;
                    ++count_;
                }
                if (used_ < kBufSize) buf_[used_++] = '\0';
                // Once the buffer is full used_ == kBufSize; a token starting
                // there would index past buf_. Park it on the final byte, which
                // is always NUL (see below), so the token reads as empty and the
                // truncated override still fails its caller's size/value check.
                tokenStart = used_ < kBufSize ? used_ : kBufSize - 1;
                if (c == '\0') break;
                continue;
            }
            if (used_ < kBufSize - 1) buf_[used_++] = c;
        }
        buf_[kBufSize - 1] = '\0';
    }

    int size() const noexcept { return count_; }

    // NUL-terminated view of token i; an empty string when i is out of range,
    // which matches juce::StringArray::operator[] returning an empty String.
    const char* operator[](int i) const noexcept
    {
        if (i < 0 || i >= count_) return "";
        return &buf_[offset_[i]];
    }

    float getFloatValue(int i) const noexcept { return parseFloat((*this)[i]); }
    int   getIntValue  (int i) const noexcept { return parseInt((*this)[i]); }

private:
    char buf_[kBufSize] {};
    int  offset_[kMaxTokens] {};
    int  count_ = 0;
    int  used_  = 0;
};

} // namespace duskverb
