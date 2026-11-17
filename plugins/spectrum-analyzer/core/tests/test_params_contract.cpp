// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// Descriptor-table contract test (GH #184, migration plan step 1).
//
// Checks core/SpectrumAnalyzerParams.hpp against the two authorities it
// restates, by parsing them instead of holding a third hand-copied table:
//   1. the plan's frozen table in docs/DAF_MIGRATION.md
//      ("| ID | Domain / choices | Step | Default |");
//   2. the shipping JUCE adapter: Source/PluginProcessor.h for the PARAM_* ID
//      strings, Source/PluginProcessor.cpp (createParameterLayout) for kinds,
//      names, choice lists, ranges, intervals and defaults.
// A declaration or table row in an unknown form fails instead of being skipped:
// every juce::ParameterID and juce::AudioParameter use in the source must
// belong to a parsed declaration, and the layout may touch `params` only
// through push_back, begin and end.
// Both sources must hold exactly the descriptor's ten controls in its order.
// Numbers are compared exactly: each side is a decimal literal rounded once to
// float, and no arithmetic touches them.
//
// Usage: SpectrumCoreParamsTest <DAF_MIGRATION.md> <PluginProcessor.h> <PluginProcessor.cpp>

#include "SpectrumAnalyzerParams.hpp"
#include "SourceText.hpp"

#include <cstdio>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace duskaudio::spectrum;
using namespace source_text;

namespace
{
int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok)
        ++failures;
}

// Locale-independent decimal parse of the whole token (an optional trailing 'f'
// is the only suffix accepted). Float parsing rounds once, like a literal.
float parseNumber(std::string token)
{
    token = trim(token);
    if (endsWith(token, "f"))
        token.pop_back();
    const bool wellFormed = !token.empty()
        && token.find_first_not_of("-.0123456789") == std::string::npos
        && token.find('-', 1) == std::string::npos;
    if (!wellFormed)
        fail("not a plain decimal number: '" + token + "'");
    std::istringstream in(token);
    in.imbue(std::locale::classic());
    float value = 0.0f;
    in >> value;
    if (in.fail() || !in.eof())
        fail("not a plain decimal number: '" + token + "'");
    return value;
}

unsigned parseIndex(const std::string& token)
{
    const std::string t = trim(token);
    if (t.empty() || t.size() > 3 || t.find_first_not_of("0123456789") != std::string::npos)
        fail("not a small index: '" + t + "'");
    return static_cast<unsigned>(std::stoul(t));
}

std::string parseStringLiteral(const std::string& token)
{
    const std::string t = trim(token);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"'
        || t.find_first_of("\"\\", 1) != t.size() - 1)
        fail("not a plain string literal: " + t);
    return t.substr(1, t.size() - 2);
}

//==============================================================================
// One control as a source declares it. `kind` is Choice/Toggle/Continuous; the
// plan's table does not tell a toggle from a two-way choice, so plan rows
// report discrete controls as Choice.
struct Declared
{
    std::string id;
    std::string name;   // JUCE only
    ParameterKind kind = ParameterKind::Continuous;
    float minimum = 0.0f, maximum = 0.0f, step = 0.0f, defaultValue = 0.0f;
    std::string unit;   // plan only
    std::vector<std::string> labels;
};

//==============================================================================
// docs/DAF_MIGRATION.md: the table under the "Frozen product contract" heading.
std::vector<Declared> parsePlanTable(const std::string& markdown)
{
    const std::string header = "| ID | Domain / choices | Step | Default |";
    std::size_t at = markdown.find(header);
    if (at == std::string::npos || markdown.find(header, at + 1) != std::string::npos)
        fail("plan: expected exactly one '" + header + "' table");

    std::vector<std::string> lines;
    std::istringstream in(markdown.substr(at));
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() != '|')
            break;
        lines.push_back(line);
    }
    if (lines.size() < 2 || lines[1] != "| --- | --- | --- | --- |")
        fail("plan: table separator row not found");

    std::vector<Declared> rows;
    for (std::size_t r = 2; r < lines.size(); ++r)
    {
        std::string line = lines[r];
        // U+2212 MINUS SIGN is a sign; U+2013 EN DASH separates the ends of a range.
        replaceAll(line, "\xE2\x88\x92", "-");
        replaceAll(line, "\xE2\x80\x93", "~");
        if (line.size() < 2 || line.back() != '|')
            fail("plan: malformed row: " + lines[r]);
        const auto cells = splitTopLevel(line.substr(1, line.size() - 2), '|');
        if (cells.size() != 4)
            fail("plan: expected 4 cells: " + lines[r]);

        Declared d;
        if (cells[0].size() < 3 || cells[0].front() != '`' || cells[0].back() != '`')
            fail("plan: ID cell is not `code`: " + cells[0]);
        d.id = cells[0].substr(1, cells[0].size() - 2);

        const std::string& domain = cells[1];
        d.step = parseNumber(cells[2]);
        const std::size_t paren = domain.rfind(" (");
        if (endsWith(domain, ")") && paren != std::string::npos)
        {
            // Discrete: "A, B, C (0~2)" or "Off / On (0~1)", default "Label (i)".
            d.kind = ParameterKind::Choice;
            const std::string names = domain.substr(0, paren);
            const auto range = splitTopLevel(domain.substr(paren + 2, domain.size() - paren - 3), '~');
            if (range.size() != 2)
                fail("plan: discrete domain needs (first~last): " + domain);
            const bool slashed = names.find(" / ") != std::string::npos;
            std::string list = names;
            replaceAll(list, slashed ? " / " : ", ", "\x1F");
            if (list.find(slashed ? ", " : " / ") != std::string::npos)
                fail("plan: mixed label separators: " + domain);
            for (const auto& label : splitTopLevel(list, '\x1F'))
                d.labels.push_back(label);
            d.minimum = static_cast<float>(parseIndex(range[0]));
            d.maximum = static_cast<float>(parseIndex(range[1]));

            const std::string& def = cells[3];
            const std::size_t defParen = def.rfind(" (");
            if (!endsWith(def, ")") || defParen == std::string::npos)
                fail("plan: discrete default needs 'Label (index)': " + def);
            const unsigned index = parseIndex(def.substr(defParen + 2, def.size() - defParen - 3));
            if (index >= d.labels.size() || d.labels[index] != def.substr(0, defParen))
                fail("plan: default label does not match its index: " + def);
            d.defaultValue = static_cast<float>(index);
        }
        else
        {
            // Continuous: "lo~hi" with an optional unit after a space.
            d.kind = ParameterKind::Continuous;
            const std::size_t space = domain.find(' ');
            const std::string span = domain.substr(0, space);
            d.unit = space == std::string::npos ? std::string() : trim(domain.substr(space + 1));
            const auto ends = splitTopLevel(span, '~');
            if (ends.size() != 2)
                fail("plan: continuous domain needs lo~hi: " + domain);
            d.minimum = parseNumber(ends[0]);
            d.maximum = parseNumber(ends[1]);
            d.defaultValue = parseNumber(cells[3]);
        }
        rows.push_back(d);
    }
    return rows;
}

//==============================================================================
// Source/PluginProcessor.h: static constexpr const char* PARAM_X = "id";
std::map<std::string, std::string> parseJuceIds(const std::string& header)
{
    const std::string code = stripComments(header);
    const std::string marker = "static constexpr const char* PARAM_";
    std::map<std::string, std::string> ids;
    for (std::size_t at = code.find(marker); at != std::string::npos; at = code.find(marker, at + 1))
    {
        const std::size_t end = code.find(';', at);
        if (end == std::string::npos)
            fail("header: unterminated PARAM_ declaration");
        const std::string decl = code.substr(at + marker.size() - 6, end - at - marker.size() + 6);
        const std::size_t eq = decl.find('=');
        if (eq == std::string::npos)
            fail("header: PARAM_ declaration without '=': " + decl);
        const std::string name = trim(decl.substr(0, eq));
        if (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_") != std::string::npos
            || !ids.emplace(name, parseStringLiteral(decl.substr(eq + 1))).second)
            fail("header: bad or repeated constant: " + name);
    }
    return ids;
}

// Every `params.<member>(` call in the layout, in order. Only push_back, and
// begin()/end() for the final range, are forms this parser understands.
std::vector<std::size_t> pushBackCalls(const std::string& body)
{
    std::vector<std::size_t> opens;
    const std::string object = "params.";
    for (std::size_t at = body.find(object); at != std::string::npos; at = body.find(object, at + 1))
    {
        if (at > 0 && isIdentifierChar(body[at - 1]))
            continue;   // part of a longer identifier such as "myparams."
        std::size_t end = at + object.size();
        while (end < body.size() && isIdentifierChar(body[end]))
            ++end;
        const std::string member = body.substr(at + object.size(), end - at - object.size());
        std::size_t open = end;
        while (open < body.size() && (body[open] == ' ' || body[open] == '\t' || body[open] == '\r' || body[open] == '\n'))
            ++open;
        if (open >= body.size() || body[open] != '(')
            fail("source: params." + member + " is not a call this test parses");
        if (member == "push_back")
            opens.push_back(open);
        else if ((member == "begin" || member == "end") && body.compare(open, 2, "()") == 0)
            continue;
        else
            fail("source: params." + member + "( is not a form this test parses");
    }
    return opens;
}

// Source/PluginProcessor.cpp: every params.push_back(...) in createParameterLayout.
std::vector<Declared> parseJuceLayout(const std::string& source,
                                      const std::map<std::string, std::string>& ids)
{
    const std::string code = stripComments(source);
    const std::string body = functionBody(code, "SpectrumAnalyzerProcessor::createParameterLayout()");

    std::vector<Declared> params;
    for (const std::size_t paren : pushBackCalls(body))
    {
        const std::string argument = body.substr(paren + 1, matchingClose(body, paren) - paren - 1);

        static const char* const kinds[] = { "Choice", "Bool", "Float" };
        std::string kind;
        for (const char* k : kinds)
            if (startsWith(trim(argument), std::string("std::make_unique<juce::AudioParameter") + k + ">("))
                kind = k;
        if (kind.empty())
            fail("source: unknown parameter declaration: " + argument);
        const auto args = splitTopLevel(
            inner(argument, "std::make_unique<juce::AudioParameter" + kind + ">(", ")"), ',');

        Declared d;
        const auto identity = splitTopLevel(inner(args[0], "juce::ParameterID(", ")"), ',');
        if (identity.size() != 2 || identity[1] != "1" || ids.count(identity[0]) != 1)
            fail("source: unknown parameter identity: " + args[0]);
        d.id = ids.at(identity[0]);
        d.name = parseStringLiteral(args[1]);

        if (kind == "Choice" && args.size() == 4)
        {
            d.kind = ParameterKind::Choice;
            for (const auto& label : splitTopLevel(inner(args[2], "juce::StringArray{", "}"), ','))
                d.labels.push_back(parseStringLiteral(label));
            d.minimum = 0.0f;
            d.maximum = static_cast<float>(d.labels.size()) - 1.0f;
            d.step = 1.0f;
            d.defaultValue = static_cast<float>(parseIndex(args[3]));
        }
        else if (kind == "Bool" && args.size() == 3 && (args[2] == "false" || args[2] == "true"))
        {
            d.kind = ParameterKind::Toggle;
            // No labels are declared: the value text is AudioParameterBool's
            // own (JUCE juce_AudioParameterBool.cpp: v ? "On" : "Off").
            d.labels = { "Off", "On" };
            d.minimum = 0.0f;
            d.maximum = 1.0f;
            d.step = 1.0f;
            d.defaultValue = args[2] == "true" ? 1.0f : 0.0f;
        }
        else if (kind == "Float" && args.size() == 4)
        {
            // The three-argument range (start, end, interval) is linear; a skew
            // argument or a custom mapping would be a form this test rejects.
            d.kind = ParameterKind::Continuous;
            const auto range = splitTopLevel(inner(args[2], "juce::NormalisableRange<float>(", ")"), ',');
            if (range.size() != 3)
                fail("source: expected NormalisableRange<float>(start, end, interval): " + args[2]);
            d.minimum = parseNumber(range[0]);
            d.maximum = parseNumber(range[1]);
            d.step = parseNumber(range[2]);
            d.defaultValue = parseNumber(args[3]);
        }
        else
        {
            fail("source: unsupported " + kind + " declaration: " + argument);
        }
        params.push_back(d);
    }

    // A parameter declared any other way (another container call, a helper, a
    // second layout function, addParameter outside the layout) still names a
    // juce::ParameterID or a juce::AudioParameter class somewhere in the file.
    for (const char* token : { "juce::ParameterID", "juce::AudioParameter" })
    {
        const std::size_t uses = countOccurrences(code, token);
        if (uses != params.size())
            fail("source: " + std::to_string(uses) + " " + token + " uses but " + std::to_string(params.size())
                 + " parsed declarations; a parameter is declared in a form this test does not parse");
    }
    return params;
}

//==============================================================================
std::string labelList(const std::vector<std::string>& labels)
{
    std::string s;
    for (const auto& l : labels)
        s += (s.empty() ? "" : ", ") + l;
    return s;
}

std::vector<std::string> specLabels(const ParameterSpec& p)
{
    std::vector<std::string> labels;
    for (unsigned i = 0; i < p.numLabels; ++i)
        labels.push_back(p.labels[i]);
    return labels;
}

const char* kindName(ParameterKind k)
{
    switch (k)
    {
        case ParameterKind::Choice:     return "choice";
        case ParameterKind::Toggle:     return "toggle";
        case ParameterKind::Continuous: return "continuous";
    }
    return "?";
}

std::string describe(ParameterKind kind, float lo, float hi, float step, float def,
                     const std::vector<std::string>& labels)
{
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%s [%.9g, %.9g] step %.9g default %.9g",
                  kindName(kind), (double)lo, (double)hi, (double)step, (double)def);
    std::string s = buf;
    if (!labels.empty())
        s += " {" + labelList(labels) + "}";
    return s;
}

void compareRows(const char* source, const std::vector<Declared>& rows, bool planRows)
{
    check(rows.size() == PARAM_COUNT,
          std::string(source) + ": declares " + std::to_string(rows.size()) + " controls, descriptor has 10");
    for (unsigned i = 0; i < PARAM_COUNT && i < rows.size(); ++i)
    {
        const ParameterSpec& p = parameterSpecs[i];
        const Declared& d = rows[i];
        // The plan cannot tell a toggle from a two-way choice; JUCE can.
        const ParameterKind specKind = planRows && p.kind == ParameterKind::Toggle
                                     ? ParameterKind::Choice : p.kind;
        const std::string want = describe(specKind, p.minimum, p.maximum, p.step, p.defaultValue, specLabels(p));
        const std::string got  = describe(d.kind, d.minimum, d.maximum, d.step, d.defaultValue, d.labels);
        const std::string tag = std::string(source) + " #" + std::to_string(i) + " " + p.id;

        check(d.id == p.id, tag + ": ID (" + d.id + ")");
        check(d.kind == specKind && d.minimum == p.minimum && d.maximum == p.maximum
                  && d.step == p.step && d.defaultValue == p.defaultValue && d.labels == specLabels(p),
              tag + ": " + got + (got == want ? "" : "  != descriptor " + want));
        if (planRows)
            check(d.unit == p.unit, tag + ": unit '" + d.unit + "' (descriptor '" + p.unit + "')");
        else
            check(d.name == p.name, tag + ": name '" + d.name + "' (descriptor '" + p.name + "')");
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::printf("usage: %s <DAF_MIGRATION.md> <PluginProcessor.h> <PluginProcessor.cpp>\n", argv[0]);
        return 2;
    }

    try
    {
        compareRows("plan", parsePlanTable(readFile(argv[1])), true);
        const auto ids = parseJuceIds(readFile(argv[2]));
        check(ids.size() == PARAM_COUNT, "JUCE header: " + std::to_string(ids.size()) + " PARAM_ ID constants");
        compareRows("JUCE", parseJuceLayout(readFile(argv[3]), ids), false);
    }
    catch (const SourceError& e)
    {
        check(false, "parse: " + e.message);
    }

    // Lookup by state key, and the one default the plan calls out by name.
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
        check(findParameter(parameterSpecs[i].id) == i,
              std::string("findParameter(\"") + parameterSpecs[i].id + "\") == " + std::to_string(i));
    check(findParameter("") == PARAM_COUNT && findParameter("peakhold") == PARAM_COUNT
              && findParameter("peakHold ") == PARAM_COUNT,
          "findParameter rejects unknown, empty and near-miss keys");
    check(parameterSpecs[PARAM_PEAK_HOLD].kind == ParameterKind::Toggle
              && parameterSpecs[PARAM_PEAK_HOLD].defaultValue == 0.0f,
          "peakHold is a toggle that defaults to Off");

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "DESCRIPTOR CONTRACT HOLDS" : "DESCRIPTOR CONTRACT BROKEN",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
