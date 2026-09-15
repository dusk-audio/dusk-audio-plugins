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
#include <limits>

#include <locale.h>
#if defined(__APPLE__)
  #include <xlocale.h>   // strtod_l lives here on macOS
#endif

namespace duskverb
{

// Locale-independent fallback for when the "C" locale cannot be created: plain
// std::strtod would honour the process locale and misread "1.25" under a
// comma-decimal locale. Accepts the same dot-decimal spelling strtod does in the
// "C" locale (leading whitespace, optional sign, digits with an optional '.',
// optional exponent) and sets *end past the consumed text, or to s when nothing
// converts. Hex floats, inf and nan are not accepted; no tuning string uses them.
inline double parseAsciiDouble(const char* s, const char** end) noexcept
{
    const char* p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) ++p;

    bool negative = false;
    if (*p == '+' || *p == '-') negative = (*p++ == '-');

    constexpr int kMaxDigits = 19;  // fits a uint64 without overflow
    unsigned long long mantissa = 0;
    int kept = 0, exp10 = 0;
    bool anyDigits = false;

    for (; *p >= '0' && *p <= '9'; ++p)
    {
        anyDigits = true;
        if (kept < kMaxDigits) { mantissa = mantissa * 10u + unsigned(*p - '0'); if (mantissa != 0) ++kept; }
        else ++exp10;
    }
    if (*p == '.')
    {
        ++p;
        for (; *p >= '0' && *p <= '9'; ++p)
        {
            anyDigits = true;
            if (kept < kMaxDigits) { mantissa = mantissa * 10u + unsigned(*p - '0'); if (mantissa != 0) ++kept; --exp10; }
        }
    }
    if (!anyDigits) { *end = s; return 0.0; }

    if (*p == 'e' || *p == 'E')
    {
        const char* q = p + 1;
        bool expNegative = false;
        if (*q == '+' || *q == '-') expNegative = (*q++ == '-');
        if (*q >= '0' && *q <= '9')
        {
            int e = 0;
            for (; *q >= '0' && *q <= '9'; ++q)
                if (e < 100000) e = e * 10 + (*q - '0');
            exp10 += expNegative ? -e : e;
            p = q;
        }
    }
    *end = p;

    double v = static_cast<double>(mantissa);
    if (v != 0.0)
    {
        // Divide for negative exponents: 1/10^n is exact as a divisor where
        // 0.1^n is not, which keeps short decimals like "1.25" bit-exact.
        const bool shrink = exp10 < 0;
        int n = shrink ? -exp10 : exp10;
        double scale = 1.0, base = 10.0;
        for (; n > 0 && scale < 1e308; n >>= 1, base *= base)
            if (n & 1) scale *= base;
        if (n > 0) scale = std::numeric_limits<double>::infinity();  // |v| is 0/inf as a float anyway
        v = shrink ? v / scale : v * scale;
    }
    return negative ? -v : v;
}

inline float parseFloat(const char* s) noexcept
{
    if (s == nullptr || *s == '\0') return 0.0f;
#if defined(_WIN32)
    static _locale_t cLocale = _create_locale(LC_ALL, "C");
#else
    static locale_t cLocale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(0));
#endif
    const char* end = nullptr;
    double v = 0.0;
    if (cLocale != nullptr)
    {
        char* localeEnd = nullptr;
#if defined(_WIN32)
        v = _strtod_l(s, &localeEnd, cLocale);
#else
        v = strtod_l(s, &localeEnd, cLocale);
#endif
        end = localeEnd;
    }
    else
    {
        v = parseAsciiDouble(s, &end);
    }
    if (end == s) return 0.0f;
    return static_cast<float>(v);
}

inline void primeParseLocale() noexcept { (void)parseFloat("0"); }

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
