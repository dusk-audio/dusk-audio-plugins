#!/usr/bin/env python3
"""Shared cores and headers (the Chord Analyzer model and its snapshot header,
the Spectrum Analyzer core and its FFT) must keep compiling the desktop
adapters that build them."""
import unittest

from select_juce_plugins import ALL_PLUGINS, select


class JuceSelection(unittest.TestCase):
    def test_shared_chord_core_selects_both_desktop_products(self):
        targets, _ = select(["plugins/chord-analyzer/core/ChordAnalyzerCore.cpp"])
        self.assertEqual(targets, ["ChordAnalyzer", "ChordAnalyzerMIDI"])

    def test_shared_snapshot_header_selects_both_chord_products(self):
        targets, _ = select(["plugins/shared-daf/dsp/DuskSeqLock.hpp"])
        self.assertEqual(targets, ["ChordAnalyzer", "ChordAnalyzerMIDI"])

    def test_chord_daf_shell_stays_separate(self):
        targets, _ = select(["plugins/chord-analyzer/daf-plugin/ChordAnalyzerPlugin.cpp"])
        self.assertEqual(targets, [])

    def test_spectrum_core_selects_its_desktop_product(self):
        targets, _ = select(["plugins/spectrum-analyzer/core/LUFSMeter.cpp"])
        self.assertEqual(targets, ["SpectrumAnalyzer"])

    def test_shared_fft_header_selects_the_spectrum_analyzer(self):
        targets, _ = select(["plugins/shared-daf/dsp/DuskFft.hpp"])
        self.assertEqual(targets, ["SpectrumAnalyzer"])

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
