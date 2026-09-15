#!/usr/bin/env python3
"""Generate a compiled metadata comparison from the actual JUCE declarations.

Keep expressions (including factory defaults) in C++, avoiding a second frozen
copy of the values. Unknown declaration forms fail generation instead of silently
skipping parameters. This does not add JUCE to the framework-free core build.
"""
import pathlib
import re
import sys


def arguments(text):
    parts, start, depth, quoted, escaped = [], 0, 0, False, False
    for i, c in enumerate(text):
        if quoted:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
        elif c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    parts.append(text[start:].strip())
    return parts


source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
source = source.split("DuskVerbProcessor::createParameterLayout()", 1)[1].split("return layout;", 1)[0]
source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
declarations = re.findall(r"layout\.add\s*\(\s*std::make_unique<juce::AudioParameter(\w+)>\s*\((.*?)\)\s*\);", source, re.S)
if len(declarations) != source.count("layout.add") or len(declarations) != 92:
    raise ValueError("Expected all 92 JUCE parameter declarations in order")
rows = []
for kind, declaration in declarations:
    args = arguments(declaration)
    identity = re.fullmatch(r'juce::ParameterID\s*\{\s*("[^"]+")\s*,\s*1\s*\}', args[0])
    if not identity:
        raise ValueError("Unknown parameter identity: " + args[0])
    if kind == "Float" and len(args) == 4:
        match = re.fullmatch(r"juce::NormalisableRange<float>\s*\((.*)\)", args[2], re.S)
        if not match:
            raise ValueError("Unknown range: " + args[2])
        values = arguments(match[1])
        if len(values) not in (2, 3, 4):
            raise ValueError("Unsupported range constructor")
        limits = (values + ["0.0f", "1.0f"][len(values) - 2:])
    elif kind == "Bool" and len(args) == 3:
        limits = ["0.0f", "1.0f", "1.0f", "1.0f"]
    elif kind == "Choice" and len(args) == 4:
        if args[2] == "algorithmNames":
            maximum = "getNumAlgorithms() - 1"
        elif re.fullmatch(r'juce::StringArray\s*\{.*\}', args[2], re.S):
            maximum = str(len(re.findall(r'"[^"\\]*"', args[2])) - 1)
        else:
            raise ValueError("Unknown choice list: " + args[2])
        limits = ["0.0f", maximum, "1.0f", "1.0f"]
    else:
        raise ValueError("Unsupported parameter declaration: " + declaration)
    rows.append("{" + identity[1] + ", " + ", ".join("float(" + v + ")" for v in limits + [args[-1]]) + "}")

pathlib.Path(sys.argv[2]).write_text('''// Generated from PluginProcessor.cpp; do not edit.
#include "DuskVerbParamTable.hpp"
#include "../src/dsp/AlgorithmConfig.h"
#include <cstdio>
#include <cstring>
int main() {
 const auto& fp0 = getFactoryPresets().front();
 struct Expected { const char* id; float min, max, interval, skew, def; };
 const Expected expected[] = {
''' + ",\n".join(rows) + '''
 };
 const auto& table = duskverb::paramTable();
 int failures = 0;
 if (table.size() != std::size(expected)) return 1;
 for (size_t i = 0; i < table.size(); ++i) {
  const auto& a = table[i]; const auto& b = expected[i];
  if (std::strcmp(a.id,b.id) || a.min != b.min || a.max != b.max
      || a.interval != b.interval || a.skew != b.skew || a.def != b.def) {
   std::printf("FAIL JUCE/DAF layout index %zu: %s / %s\\n", i, a.id, b.id); ++failures;
  }
 }
 if (!failures) std::puts("PASS all 92 ordered IDs, ranges, intervals, skews and defaults match JUCE");
 return failures ? 1 : 0;
}
''', encoding="utf-8")
