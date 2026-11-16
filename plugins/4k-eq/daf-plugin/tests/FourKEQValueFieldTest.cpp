// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// 4K EQ 2's own knobs (the stepped filter knobs and the console detent knobs)
// and their inline value fields, on the production editor: a click inside an
// open field stays in the field (dusk-audio/plugins#337), and everything else
// the knobs do is unchanged. See DuskValueFieldTest.hpp for the cases.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "FourKEQUI.cpp"
#include "DuskValueFieldTest.hpp"

// The DSP bridge (FourKEQAccess.hpp) as an out-of-process UI sees it. Defined
// because a weak reference must still resolve when an Apple binary links.
float fourKEQGetInputPeakL(void*) noexcept { return 0; }
float fourKEQGetInputPeakR(void*) noexcept { return 0; }
float fourKEQGetOutputPeakL(void*) noexcept { return 0; }
float fourKEQGetOutputPeakR(void*) noexcept { return 0; }
const duskaudio::SpectrumRing* fourKEQGetPreSpectrum(void*) noexcept { return nullptr; }
const duskaudio::SpectrumRing* fourKEQGetPostSpectrum(void*) noexcept { return nullptr; }
uint32_t fourKEQGetLegacyDialBands(void*) noexcept { return 0; }
uint32_t fourKEQGetLegacyDialFilters(void*) noexcept { return 0; }

int main()
{
    dusktest::isolateHome(FOURKEQ_UI_TEST_HOME);
    using dusktest::ValueFieldKnob;

    // Layout (FourKEQUI::drawColumns, graph shown): cY(y) = y - 40, column
    // centres between COL[] = { 40, 186, 331, ... }, knob radius 28.
    ValueFieldKnob hpf;
    hpf.name = "HPF knob";
    hpf.x = 113.0f; hpf.y = 274.0f; hpf.radius = 28.0f; hpf.aboveField = -18.0f;
    hpf.outside = ImVec2(113.0f, 343.0f);   // between the HPF and LPF knobs
    hpf.parameters = { kHpfEnabled, kHpfHz };
    hpf.valueParameter = kHpfHz;
    hpf.start = { { kHpfEnabled, 1.0f }, { kHpfHz, 120.0f } };   // the 120 detent, mid-travel
    hpf.typed = "250"; hpf.committed = 250.0f;
    hpf.resetModifier = ImGuiMod_Super;   // Cmd (macOS) / Ctrl+click: back to OUT
    hpf.resetParameter = kHpfEnabled; hpf.resetValue = 0.0f;

    ValueFieldKnob lfGain;
    lfGain.name = "LF GAIN knob";
    lfGain.x = 258.5f; lfGain.y = 274.0f; lfGain.radius = 28.0f; lfGain.aboveField = -18.0f;
    lfGain.outside = ImVec2(258.5f, 343.0f);   // between LF GAIN and LF FREQ
    lfGain.parameters = { kLfGain };
    lfGain.valueParameter = kLfGain;
    lfGain.start = { { kLfGain, 3.0f } };
    lfGain.typed = "6"; lfGain.committed = 6.0f;
    lfGain.resetModifier = ImGuiMod_Super;
    lfGain.resetParameter = kLfGain; lfGain.resetValue = kFourKParams[kLfGain].def;

    // LF FREQ: a console knob on a frequency parameter, which types and reads Hz.
    ValueFieldKnob lfFreq = lfGain;
    lfFreq.name = "LF FREQ knob";
    lfFreq.y = 412.0f;
    lfFreq.outside = ImVec2(258.5f, 343.0f);
    lfFreq.parameters = { kLfHz };
    lfFreq.valueParameter = kLfHz;
    lfFreq.start = { { kLfHz, 338.0f } };   // a detent away from the 200 Hz default
    lfFreq.typed = "100"; lfFreq.committed = 100.0f;
    lfFreq.resetParameter = kLfHz; lfFreq.resetValue = kFourKParams[kLfHz].def;

    dusktest::ValueFieldSuite<DAF::FourKEQUI> suite(960, 640);
    suite.run(hpf, lfGain);
    suite.run(lfGain, hpf);
    suite.run(lfFreq, hpf);

    return dusktest::summary();
}
