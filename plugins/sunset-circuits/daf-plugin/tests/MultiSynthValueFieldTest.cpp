// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Sunset Circuits' own knobs (the log-taper knob every LOG parameter uses, and
// Prism's operator RATIO knob) and their inline value fields, on the
// production editor: a click inside an open field stays in the field
// (dusk-audio/plugins#337), and everything else the knobs do is unchanged. See
// DuskValueFieldTest.hpp for the cases.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "MultiSynthUI.cpp"
#include "DuskValueFieldTest.hpp"

// The DSP bridge (MultiSynthAccess.hpp) as an out-of-process UI sees it.
// Defined because a weak reference must still resolve when an Apple binary links.
float multiSynthGetOutLevelL(void*) noexcept { return -60.0f; }
float multiSynthGetOutLevelR(void*) noexcept { return -60.0f; }
int multiSynthGetArpStep(void*) noexcept { return -1; }
msynth::MultiSynthDSP* multiSynthGetDSP(void*) noexcept { return nullptr; }
uint32_t multiSynthGetMidiProgramSignal(void*) noexcept { return 0; }

int main()
{
    dusktest::isolateHome(SUNSET_UI_TEST_HOME);
    using dusktest::ValueFieldKnob;

    // FILTER CUTOFF (drawFilter): a LOG parameter, so the log-taper knob.
    ValueFieldKnob cutoff;
    cutoff.name = "FILTER CUTOFF knob";
    cutoff.x = 426.0f; cutoff.y = 234.0f; cutoff.radius = 54.0f; cutoff.aboveField = -25.0f;
    cutoff.outside = ImVec2(503.0f, 280.0f);   // between CUTOFF and RES
    cutoff.parameters = { kParamFilterCutoff };
    cutoff.valueParameter = kParamFilterCutoff;
    cutoff.start = { { kParamFilterCutoff, 500.0f } };
    cutoff.typed = "1000"; cutoff.committed = 1000.0f;
    cutoff.resetModifier = ImGuiMod_Alt;   // Option/Alt+click, on every platform
    cutoff.resetParameter = kParamFilterCutoff; cutoff.resetValue = kParamDefs[kParamFilterCutoff].def;
    cutoff.hover = ValueFieldKnob::Hover::Tooltip;

    // RES: a shared DuskPanel::knob beside it, as the other knob of the case
    // with a field open.
    ValueFieldKnob res;
    res.name = "RES knob";
    res.x = 556.0f; res.y = 232.0f;
    res.parameters = { kParamFilterRes };
    res.valueParameter = kParamFilterRes;

    // Prism (mode 4), operator 1 RATIO (drawPrismOps): the snapping ratio knob,
    // radius 13, so the only knob outside its field is a band above and below it.
    ValueFieldKnob ratio;
    ratio.name = "Prism OP 1 RATIO knob";
    ratio.x = 96.0f; ratio.y = 102.0f; ratio.radius = 13.0f; ratio.aboveField = -12.0f;
    ratio.outside = ImVec2(60.0f, 150.0f);   // the strip's left gutter, beside OP 1's A knob
    ratio.parameters = { kParamOp1Ratio };
    ratio.valueParameter = kParamOp1Ratio;
    ratio.start = { { kParamMode, 4.0f }, { kParamOp1Ratio, 2.0f } };
    ratio.typed = "4"; ratio.committed = 4.0f;
    ratio.resetModifier = ImGuiMod_Alt;
    ratio.resetParameter = kParamOp1Ratio; ratio.resetValue = kParamDefs[kParamOp1Ratio].def;
    ratio.hover = ValueFieldKnob::Hover::Tooltip;

    // Operator 1 FINE, the shared knob beside RATIO.
    ValueFieldKnob fine;
    fine.name = "Prism OP 1 FINE knob";
    fine.x = 150.0f; fine.y = 102.0f;
    fine.parameters = { kParamOp1Ratio + 1 };
    fine.valueParameter = kParamOp1Ratio + 1;

    dusktest::ValueFieldSuite<DAF::MultiSynthUI> suite(1240, 780);
    suite.run(cutoff, res);
    suite.run(ratio, fine);

    // The item-query rule: both knobs read the hover for their tooltip after
    // the button they skip. CUTOFF and OP 1 RATIO are the first items of their
    // layer windows, so a knob with another knob just before it stands in for
    // each: OP 1 A (a time knob, so the log-taper knob) after OP 1 KEY, and
    // OP 2 RATIO after OP 1 R. Operator strips are 80 px apart (drawPrismOps).
    const dusktest::Values prism = { { kParamMode, 4.0f } };
    ValueFieldKnob attack;
    attack.name = "Prism OP 1 A knob";
    attack.x = 96.0f; attack.y = 142.0f;
    attack.start = prism;
    ValueFieldKnob key;
    key.name = "OP 1 KEY";
    key.x = 312.0f; key.y = 102.0f;
    suite.previousItemTooltip(attack, key);

    ValueFieldKnob ratio2;
    ratio2.name = "Prism OP 2 RATIO knob";
    ratio2.x = 96.0f; ratio2.y = 182.0f;
    ratio2.start = prism;
    ValueFieldKnob release;
    release.name = "OP 1 R";
    release.x = 258.0f; release.y = 142.0f;
    suite.previousItemTooltip(ratio2, release);

    // Wheel Mod shows these three values as percentages. Exercise typed
    // entry on the actual overlay and check the value written to the host.
    dusktest::EditorHost<DAF::MultiSynthUI> wheel(1240, 780, {
        { kParamMode, 1.0f }, { kParamWheelSourceMix, 0.18f },
        { kParamWheelUpperLimit, 0.18f }, { kParamWheelLowerLimit, 0.18f }
    });
    wheel.click(870, 522);
    wheel.idle();
    struct WheelKnob { uint32_t id; float x, y; };
    const WheelKnob controls[] = {
        { kParamWheelSourceMix, 525, 369 }, { kParamWheelUpperLimit, 752, 355 },
        { kParamWheelLowerLimit, 752, 481 }
    };
    for (const auto& control : controls)
    {
        const auto start = wheel.ui.hostEvents.size();
        wheel.doubleClick(control.x, control.y);
        wheel.idle();
        wheel.type("25");
        wheel.key(ImGuiKey_Enter);
        wheel.idle();
        float written = -1.0f;
        const bool received = wheel.lastWrite(start, control.id, written);
        dusktest::guards(received && written == 0.25f,
                         std::string(kParamDefs[control.id].name) + " typed 25% wrote " + std::to_string(written));
    }

    return dusktest::summary();
}
