#!/usr/bin/env python3
"""Shared Chord Analyzer model changes must keep compiling both desktop adapters."""
import unittest

from select_juce_plugins import ALL_PLUGINS, select


class JuceSelection(unittest.TestCase):
    def test_shared_chord_core_selects_both_desktop_products(self):
        targets, _ = select(["plugins/chord-analyzer/core/ChordAnalyzerCore.cpp"])
        self.assertEqual(targets, ["ChordAnalyzer", "ChordAnalyzerMIDI"])

    def test_chord_daf_shell_stays_separate(self):
        targets, _ = select(["plugins/chord-analyzer/daf-plugin/ChordAnalyzerPlugin.cpp"])
        self.assertEqual(targets, [])

    def test_other_daf_only_cores_stay_separate(self):
        targets, _ = select(["plugins/multi-comp/core/MultiCompDSP.cpp",
                             "plugins/multi-q/core/MultiQDSP.cpp"])
        self.assertEqual(targets, [])

    def test_mixed_change_keeps_both_plugin_sets(self):
        targets, _ = select(["plugins/chord-analyzer/core/ChordAnalyzerCore.hpp",
                             "plugins/multi-q/MultiQ.cpp"])
        self.assertEqual(targets, ["MultiQ", "ChordAnalyzer", "ChordAnalyzerMIDI"])

    def test_shared_ui_still_selects_the_fleet(self):
        targets, _ = select(["plugins/shared/DuskLookAndFeel.h"])
        self.assertEqual(targets, ALL_PLUGINS)


if __name__ == "__main__":
    unittest.main()
