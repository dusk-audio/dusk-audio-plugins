// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Small, allocation-free, locale-independent text helpers for UI value entry.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace duskdaf::value_text
{

inline bool finiteFloat(float value) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline bool finiteDouble(double value) noexcept
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

inline bool parseDecimal(const char* text, const char*& end, float& result) noexcept
{
    if (text == nullptr) return false;
    const char* p = text;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    bool negative = false;
    if (*p == '+' || *p == '-') { negative = *p == '-'; ++p; }

    double value = 0.0;
    bool digit = false;
    while (*p >= '0' && *p <= '9')
    {
        digit = true;
        value = value * 10.0 + static_cast<double>(*p - '0');
        if (!finiteDouble(value)) return false;
        ++p;
    }
    if (*p == '.')
    {
        ++p;
        double place = 0.1;
        while (*p >= '0' && *p <= '9')
        {
            digit = true;
            value += static_cast<double>(*p - '0') * place;
            place *= 0.1;
            ++p;
        }
    }
    if (!digit) return false;
    if (*p == 'e' || *p == 'E')
    {
        ++p;
        bool expNegative = false;
        if (*p == '+' || *p == '-') { expNegative = *p == '-'; ++p; }
        int exponent = 0;
        bool expDigit = false;
        while (*p >= '0' && *p <= '9')
        {
            expDigit = true;
            if (exponent > 400) return false;
            exponent = exponent * 10 + (*p - '0');
            ++p;
        }
        if (!expDigit) return false;
        value *= std::pow(10.0, expNegative ? -exponent : exponent);
    }
    if (!finiteDouble(value)) return false;
    if (negative) value = -value;
    const float converted = static_cast<float>(value);
    if (!finiteFloat(converted)) return false;
    end = p;
    result = converted;
    return true;
}

inline bool finish(const char* end, const char* expected) noexcept
{
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (expected == nullptr || *expected == '\0') return *end == '\0';
    while (*expected != '\0' && *end == *expected) { ++end; ++expected; }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    return *expected == '\0' && *end == '\0';
}

inline bool suffix(const char* end, const char* a, const char* b = nullptr) noexcept
{
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    const auto matches = [end](const char* wanted) {
        const char* s = wanted;
        const char* q = end;
        while (*s != '\0' && *q == *s) { ++s; ++q; }
        if (*s != '\0') return false;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') ++q;
        return *q == '\0';
    };
    if (matches(a)) return true;
    if (b != nullptr)
    {
        if (matches(b)) return true;
    }
    return false;
}

} // namespace duskdaf::value_text
