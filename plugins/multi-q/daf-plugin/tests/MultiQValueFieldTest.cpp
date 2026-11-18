// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Multi-Q 2's own knobs (the Digital detail-panel knobs, and the British
// stepped filter and console detent knobs) and their inline value fields, on
// the production editor: a click inside an open field stays in the field
// (dusk-audio/plugins#337), and everything else the knobs do is unchanged. See
// DuskValueFieldTest.hpp for the cases.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "MultiQUI.cpp"
#include "DuskValueFieldTest.hpp"

// The DSP bridge (MultiQAccess.hpp) as an out-of-process UI sees it. Defined
// because a weak reference must still resolve when an Apple binary links.
float multiQGetInputPeakL(void*) noexcept { return 0; }
float multiQGetInputPeakR(void*) noexcept { return 0; }
float multiQGetOutputPeakL(void*) noexcept { return 0; }
float multiQGetOutputPeakR(void*) noexcept { return 0; }
const duskaudio::SpectrumRing* multiQGetOutputSpectrum(void*) noexcept { return nullptr; }
const duskaudio::SpectrumRing* multiQGetInputSpectrum(void*) noexcept { return nullptr; }
float multiQGetBandDynGain(void*, int) noexcept { return 0; }
float multiQGetLimiterGR(void*) noexcept { return 0; }
void multiQSetSolo(void*, int, bool) noexcept {}
int multiQGetSoloBand(void*) noexcept { return -1; }
bool multiQGetSoloDelta(void*) noexcept { return false; }
void multiQMatchStartLearnCurrent(void*, bool) noexcept {}
void multiQMatchStartLearnReference(void*, bool) noexcept {}
bool multiQMatchCompute(void*) noexcept { return false; }
void multiQMatchClear(void*) noexcept {}
bool multiQMatchIsLearning(void*) noexcept { return false; }
bool multiQMatchIsLearningCurrent(void*) noexcept { return false; }
bool multiQMatchIsLearningReference(void*) noexcept { return false; }
int multiQMatchFrameCount(void*) noexcept { return 0; }
bool multiQMatchHasCurrent(void*) noexcept { return false; }
bool multiQMatchHasReference(void*) noexcept { return false; }
bool multiQMatchHasCorrection(void*) noexcept { return false; }
void multiQMatchGetCurrentDb(void*, float*, int) noexcept {}
void multiQMatchGetReferenceDb(void*, float*, int) noexcept {}
void multiQMatchGetCorrectionDb(void*, float*, int) noexcept {}
std::string multiQGetUiPresets(void*) noexcept { return {}; }

int main()
{
    dusktest::isolateHome(MULTIQ_UI_TEST_HOME);
    using dusktest::ValueFieldKnob;

    // Digital (the default character): the detail panel of the selected band
    // (band 5, "Mid", by default). drawDetailPanel: knob row centre DPY0 + 58,
    // radius 34, FREQ at x 128, Q at x 206.
    const int band = 4;
    ValueFieldKnob freq;
    freq.name = "Digital FREQ knob";
    freq.x = 128.0f; freq.y = 618.0f; freq.radius = 34.0f; freq.aboveField = -20.0f;
    freq.outside = ImVec2(167.0f, 662.0f);   // under the gap between FREQ and Q
    freq.parameters = { (uint32_t) mqidx::freq(band) };
    freq.valueParameter = (uint32_t) mqidx::freq(band);
    freq.start = { { freq.valueParameter, 500.0f } };
    freq.typed = "2000"; freq.committed = 2000.0f;
    freq.resetModifier = ImGuiMod_Super;   // Cmd (macOS) / Ctrl+click
    freq.resetParameter = freq.valueParameter; freq.resetValue = DAF::mqDef(freq.valueParameter);
    freq.hover = ValueFieldKnob::Hover::Nothing;   // the value is always drawn in the cap

    ValueFieldKnob q = freq;
    q.name = "Digital Q knob";
    q.x = 206.0f;
    q.parameters = { (uint32_t) mqidx::q(band) };
    q.valueParameter = (uint32_t) mqidx::q(band);
    q.start = { { q.valueParameter, 2.0f } };
    q.typed = "4"; q.committed = 4.0f;
    q.resetParameter = q.valueParameter; q.resetValue = DAF::mqDef(q.valueParameter);

    // British: the FourKEQUI layout (drawColumns), graph shown: cY(y) = y.
    const dusktest::Values british = { { kParamEqType, (float) DAF::kBritishModeIndex } };
    ValueFieldKnob hpf;
    hpf.name = "British HPF knob";
    hpf.x = 113.0f; hpf.y = 314.0f; hpf.radius = 28.0f; hpf.aboveField = -18.0f;
    hpf.outside = ImVec2(113.0f, 383.0f);   // between the HPF and LPF knobs
    hpf.parameters = { kParamBritishHpfEnabled, kParamBritishHpfFreq };
    hpf.valueParameter = kParamBritishHpfFreq;
    hpf.start = british;
    hpf.start.push_back({ kParamBritishHpfEnabled, 1.0f });
    hpf.start.push_back({ kParamBritishHpfFreq, 120.0f });   // the 120 detent, mid-travel
    hpf.typed = "200"; hpf.committed = 200.0f;
    hpf.resetModifier = ImGuiMod_Super;   // back to OUT
    hpf.resetParameter = kParamBritishHpfEnabled; hpf.resetValue = 0.0f;

    ValueFieldKnob lfGain;
    lfGain.name = "British LF GAIN knob";
    lfGain.x = 258.5f; lfGain.y = 314.0f; lfGain.radius = 28.0f; lfGain.aboveField = -18.0f;
    lfGain.outside = ImVec2(258.5f, 383.0f);   // between LF GAIN and LF FREQ
    lfGain.parameters = { kParamBritishLfGain };
    lfGain.valueParameter = kParamBritishLfGain;
    lfGain.start = british;
    lfGain.start.push_back({ kParamBritishLfGain, 3.0f });
    lfGain.typed = "6"; lfGain.committed = 6.0f;
    lfGain.resetModifier = ImGuiMod_Super;
    lfGain.resetParameter = kParamBritishLfGain; lfGain.resetValue = DAF::mqDef(kParamBritishLfGain);

    ValueFieldKnob lfFreq = lfGain;
    lfFreq.name = "British LF FREQ knob";
    lfFreq.y = 452.0f;
    lfFreq.parameters = { kParamBritishLfFreq };
    lfFreq.valueParameter = kParamBritishLfFreq;
    lfFreq.start = british;
    lfFreq.start.push_back({ kParamBritishLfFreq, 300.0f });   // a detent away from the 100 Hz default
    lfFreq.typed = "150"; lfFreq.committed = 150.0f;
    lfFreq.resetParameter = kParamBritishLfFreq; lfFreq.resetValue = DAF::mqDef(kParamBritishLfFreq);

    // The design size (DAF_UI_DEFAULT_WIDTH is wider and letterboxes).
    dusktest::ValueFieldSuite<DAF::MultiQUI> suite(960, 680);
    suite.run(freq, q);
    suite.run(q, freq);
    suite.run(hpf, lfGain);
    suite.run(lfGain, hpf);
    suite.run(lfFreq, hpf);

    return dusktest::summary();
}
