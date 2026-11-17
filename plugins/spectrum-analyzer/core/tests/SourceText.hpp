// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// SourceText.hpp - strict helpers the core tests use to read C++ and Markdown
// sources as text (GH #184). Anything unexpected throws SourceError, which a
// test reports as a failure: an unknown form is never skipped silently.
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace source_text
{
struct SourceError
{
    std::string message;
};

[[noreturn]] inline void fail(const std::string& message) { throw SourceError { message }; }

inline std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        fail("cannot open " + path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

inline std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
}

inline std::size_t countOccurrences(const std::string& s, const std::string& token)
{
    std::size_t count = 0;
    for (std::size_t at = s.find(token); at != std::string::npos; at = s.find(token, at + token.size()))
        ++count;
    return count;
}

inline bool isIdentifierChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Removes // and /* */ comments, leaving string literals intact.
inline std::string stripComments(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '"')
        {
            const std::size_t close = src.find('"', i + 1);
            if (close == std::string::npos)
                fail("unterminated string literal");
            out.append(src, i, close - i + 1);
            i = close;
        }
        else if (src.compare(i, 2, "//") == 0)
        {
            i = src.find('\n', i);
            if (i == std::string::npos)
                break;
            out += '\n';
        }
        else if (src.compare(i, 2, "/*") == 0)
        {
            i = src.find("*/", i + 2);
            if (i == std::string::npos)
                fail("unterminated block comment");
            ++i;
            out += ' ';
        }
        else
        {
            out += src[i];
        }
    }
    return out;
}

// Index of the bracket closing the one at `open`, skipping string literals.
inline std::size_t matchingClose(const std::string& s, std::size_t open)
{
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '"')
        {
            i = s.find('"', i + 1);
            if (i == std::string::npos)
                break;
        }
        else if (c == '(' || c == '{' || c == '[')
        {
            ++depth;
        }
        else if (c == ')' || c == '}' || c == ']')
        {
            if (--depth == 0)
                return i;
        }
    }
    fail("unbalanced brackets");
}

// Splits at top-level separators, honouring brackets and string literals.
inline std::vector<std::string> splitTopLevel(const std::string& s, char separator)
{
    std::vector<std::string> parts;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '"')
        {
            i = s.find('"', i + 1);
            if (i == std::string::npos)
                fail("unterminated string literal in argument list");
        }
        else if (c == '(' || c == '{' || c == '[')
            ++depth;
        else if (c == ')' || c == '}' || c == ']')
            --depth;
        else if (c == separator && depth == 0)
        {
            parts.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim(s.substr(start)));
    return parts;
}

// The text strictly inside `head ... close`, e.g. inner("f(a, b)", "f(", ")").
inline std::string inner(const std::string& expression, const std::string& head, const std::string& close)
{
    const std::string e = trim(expression);
    if (!startsWith(e, head) || !endsWith(e, close) || matchingClose(e, head.size() - 1) != e.size() - 1)
        fail("expected " + head + "..." + close + ", found: " + e);
    return e.substr(head.size(), e.size() - head.size() - close.size());
}

// The body (inside the outermost braces) of the one definition that starts
// with `signature`, e.g. "Processor::prepareToPlay(".
inline std::string functionBody(const std::string& code, const std::string& signature)
{
    const std::size_t at = code.find(signature);
    if (at == std::string::npos || code.find(signature, at + 1) != std::string::npos)
        fail("expected exactly one definition of " + signature);
    const std::size_t open = code.find('{', at);
    if (open == std::string::npos || code.find(';', at) < open)
        fail(signature + " is not followed by a body");
    return code.substr(open + 1, matchingClose(code, open) - open - 1);
}

// Every whitespace run becomes one space, so indentation, line endings and
// line breaks do not count.
inline std::string normaliseWhitespace(const std::string& s)
{
    std::string out;
    bool pendingSpace = false;
    for (const char c : s)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
        {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace)
            out += ' ';
        pendingSpace = false;
        out += c;
    }
    return out;
}

inline std::uint64_t fnv1a64(const std::string& s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}
} // namespace source_text
