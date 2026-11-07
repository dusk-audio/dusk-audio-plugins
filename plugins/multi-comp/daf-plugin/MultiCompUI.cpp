// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 Dear ImGui UI.  The controls mirror EnhancedCompressorEditor and
// ModernCompressorPanels; the DSP and parameter ownership remain in the shell/core.

#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVcaMeterGeometry.hpp"
#include "MultiCompVintageMeterGeometry.hpp"
#include "MultiCompVintageMeterControls.hpp"
#include "../core/MultiCompVcaLaw.hpp"
#include "../core/MultiCompStudioVcaLaw.hpp"
#include "../../shared-daf/ui/DuskValueText.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

namespace multicompp::ui_detail
{
inline int choiceIndex(float hostValue, int count) noexcept
{
    return std::clamp(static_cast<int>(std::round(hostValue)), 0, count - 1);
}

// Every mode shares one canvas, so a mode change never resizes the window: a mode whose canvas differed from the
// opening size showed up shrunken in Logic (#240). 1120 x 380, the default and the minimum (owner 2026-11-05: one
// size for every mode, the original one; its 1120 x 308 face area has the proportions of the reference leveling
// amplifier's own window). Design px are screen px at that size, so the readability floor (legends 16 px, values
// 17 px) is met in design px.
inline constexpr float kDesignWidth = 1120.0f;
inline constexpr float kDesignHeight = 380.0f;

// The mode row under the header: the eight mode tabs, then AUTO MAKEUP, BYPASS and SETTINGS (16 px captions).
struct ModeRowLayout
{
    static constexpr float y0 = 46.0f, y1 = 70.0f;
    static constexpr float tabLeft = 16.0f, tabWidth = 92.0f, tabGap = 4.0f;
    static constexpr float autoMakeupX0 = 790.0f, autoMakeupX1 = 912.0f;
    static constexpr float bypassX0 = 918.0f, bypassX1 = 1004.0f;
    static constexpr float settingsX0 = 1010.0f, settingsX1 = kDesignWidth - 16.0f;
    static constexpr float tabX0(int mode) noexcept { return tabLeft + static_cast<float>(mode) * (tabWidth + tabGap); }
    static constexpr float centreY = 0.5f * (y0 + y1);
};

// The face area under the mode row (canvas y).
inline constexpr float kFaceAreaTop = 72.0f;

// The settings drawer over the face area (canvas coordinates; text y is its top). Row A: SIDECHAIN, PROCESSING.
// Row B: STEREO, DRIVE, LOOKAHEAD, FET BLACK.
struct SettingsDrawerLayout
{
    static constexpr float x0 = 16.0f, y0 = kFaceAreaTop + 4.0f, x1 = kDesignWidth - 16.0f, y1 = kDesignHeight - 4.0f;
    static constexpr float legendSize = 16.0f, valueSize = 17.0f, knobRadius = 22.0f;
    static constexpr float buttonH = 28.0f, segmentGap = 6.0f;
    static constexpr float titleY = y0 + 6.0f;                                // SETTINGS, and CLOSE beside it
    static constexpr float closeX0 = x1 - 112.0f, closeX1 = x1 - 16.0f;
    static constexpr float rowA = 114.0f, rowB = 244.0f;                       // the two rows' group headings
    static constexpr float rowPitch = 32.0f;                                   // between a group's button rows
    static constexpr float rowAButtonsY = rowA + 28.0f, rowAKnobY = rowA + 76.0f;
    static constexpr float rowBButtonsY = rowB + 28.0f, rowBKnobY = rowB + 76.0f;
    // Row A: SIDECHAIN (the key's buttons, then the SC EQ knobs) and PROCESSING (a toggle, then its segments)
    static constexpr float sidechainX0 = 40.0f, sidechainX1 = 560.0f, keyX0 = 40.0f, keyX1 = 200.0f;
    static constexpr float lowFreqX = 262.0f, lowGainX = 352.0f, highFreqX = 442.0f, highGainX = 528.0f;
    static constexpr float processingX0 = 590.0f, processingX1 = x1 - 24.0f;
    static constexpr float processingToggleX0 = 598.0f, processingToggleX1 = 742.0f;
    static constexpr float processingSegmentsX = 750.0f, processingSegmentW = 64.0f;
    // Row B
    static constexpr float stereoX0 = 40.0f, stereoX1 = 290.0f, linkX = 84.0f, linkModeX = 150.0f;
    static constexpr float linkModeW = 130.0f;
    static constexpr float driveX0 = 310.0f, driveX1 = 540.0f, driveGridX = 318.0f, driveW = 64.0f;
    static constexpr float amountX = 500.0f;
    static constexpr float lookaheadX0 = 560.0f, lookaheadX1 = 740.0f, lookaheadX = 650.0f;
    static constexpr float fetX0 = 760.0f, fetX1 = x1 - 24.0f, thresholdX = 810.0f, transientX = 906.0f;
    static constexpr float curveX = 970.0f, curveW = 92.0f;
    // The centre of segment `i` of a vertical group at (x, y) of `perColumn` buttons a column (0: one column).
    static constexpr float segmentCentreX(float x, float width, int i, int perColumn) noexcept
    {
        return x + static_cast<float>(perColumn > 0 ? i / perColumn : 0) * (width + segmentGap) + 0.5f * width;
    }
    static constexpr float segmentCentreY(float y, int i, int perColumn) noexcept
    {
        return y + static_cast<float>(perColumn > 0 ? i % perColumn : i) * (buttonH + segmentGap) + 0.5f * buttonH;
    }
};

// OPTO 2A, laid out on the 1120 x 380 canvas at the readability floor (owner direction 2026-11-05: legends 16 px,
// values 17 px, the hero legends 20 and the nameplate 24, design px = screen px at the minimum window). Canvas
// coordinates; text y is the top of the text.
struct OptoFaceplateLayout
{
    static constexpr float left = 0.0f;
    static constexpr float right = kDesignWidth;
    static constexpr float top = kFaceAreaTop + 1.0f;
    static constexpr float bottom = kDesignHeight - 2.0f;
    static constexpr float ear = 40.0f;        // each rack ear
    static constexpr float dialY = 228.0f;     // GAIN and PEAK REDUCTION, their centres
    static constexpr float dialRadius = 48.0f;
    static constexpr float ringLabelOffset = 29.0f;   // the numerals' centres, past the dial's edge
    static constexpr float legendDrop = 40.0f;        // the hero legend's top, below the dial's edge
    static constexpr float gainX = 306.0f;
    static constexpr float peakX = 814.0f;
    static constexpr float meterX = 431.0f, meterY = 104.0f, meterW = 258.0f, meterH = 146.0f;   // the VU
    static constexpr float meterSwitchY = 284.0f;   // GR / +10 / +4, their text
    static constexpr float switchX = 140.0f, switchY = 228.0f;   // LIMIT / COMPRESS, the toggle's pivot, level with the dials
    static constexpr float trimX = 994.0f, scHpY = 152.0f, mixY = 264.0f, trimRadius = 15.0f;   // set screws
    static constexpr float brandX = 68.0f, brandY = 97.0f;   // LEVELING AMPLIFIER
};

// FET BLACK and FET BLUE on the 1120 x 380 canvas at the readability floor (canvas coordinates; text y is its top).
// FET BLUE follows its reference unit's arrangement (owner 2026-11-05): HR and MIX trims between INPUT and OUTPUT,
// and a blue meter section.
struct FetFaceplateLayout
{
    static constexpr float left = 0.0f, right = kDesignWidth, top = kFaceAreaTop + 1.0f, bottom = kDesignHeight - 2.0f;
    static constexpr float ear = 36.0f;
    static constexpr float dialY = 228.0f, dialRadius = 44.0f;             // INPUT and OUTPUT
    static constexpr float ringLabelOffset = 28.0f;                       // their numerals' centres, past the edge
    static constexpr float legendDrop = 40.0f;                            // INPUT / OUTPUT's top, below the edge
    static constexpr float blackInputX = 148.0f, blackOutputX = 356.0f;
    static constexpr float blueInputX = 132.0f, blueOutputX = 372.0f;
    static constexpr float blueTrimX = 252.0f, blueHeadroomY = 168.0f, blueMixY = 290.0f;   // HR and MIX
    static constexpr float trimRadius = 13.0f;
    static constexpr float timingX = 512.0f;                              // ATTACK over RELEASE
    static constexpr float attackY = 158.0f, releaseY = 296.0f, timingRadius = 20.0f;
    static constexpr float blackRatioX = 600.0f, blueRatioX = 594.0f;     // the RATIO column's buttons
    static constexpr float buttonsY = 124.0f;                             // RATIO and METER columns, first button
    static constexpr float meterX = 664.0f, meterW = 252.0f, meterH = 150.0f;   // the VU
    static constexpr float blackMeterY = 124.0f, blueMeterY = 112.0f;
    static constexpr float blueSectionX0 = 650.0f, blueSectionX1 = 930.0f;
    static constexpr float meterButtonsX = 948.0f;                        // the METER column's buttons
    static constexpr float blackMixX = 976.0f, blackMixY = 310.0f;
};

// The VCA face on the 1120 x 380 canvas at the readability floor (canvas coordinates; text y is its top), in its
// reference unit's arrangement: THRESHOLD with its BELOW / ABOVE jewels and PULL/SC, COMPRESSION, OUTPUT GAIN, the
// meter keys under the last two, and the meter (VcaMeterRect) between its model line and badge, MIX beside the badge.
// The reference's own ~2.5:1 proportions were tried on a tall canvas (2026-09-01) and abandoned: the AU wrapper's
// resize path leaves the GL view at half size / the host keeps the old window (#240), so a mode whose canvas differs
// from the opening size shows up shrunken in Logic.
struct VcaFaceplateLayout
{
    static constexpr float left = 0.0f, right = kDesignWidth, top = kFaceAreaTop + 1.0f, bottom = kDesignHeight - 2.0f;
    static constexpr float cheek = 44.0f, rail = 16.0f;                   // the walnut cheeks, the aluminium rails
    static constexpr float titleY = 95.0f;                                // THRESHOLD, COMPRESSION, OUTPUT GAIN
    static constexpr float thresholdX = 178.0f, compressionX = 385.0f, outputX = 595.0f;
    static constexpr float knobY = 212.0f, knobRadius = 34.0f, compressionRadius = 42.0f;
    static constexpr float ringLabelOffset = 26.0f;                       // the numerals' anchors, past the edge
    static constexpr float jewelY = 133.0f, jewelRadius = 7.0f;            // BELOW and ABOVE, their centres
    static constexpr float belowX = thresholdX - 36.0f, aboveX = thresholdX + 36.0f;
    static constexpr float pullLegendY = 268.0f, pullY = 316.0f;           // PULL/SC, and its switch's centre
    // The meter keys, centred between COMPRESSION and OUTPUT GAIN, far enough apart that each legend reads as its
    // own key's (at an 8 px gap two neighbouring legends ran together).
    static constexpr float keyW = 104.0f, keyGap = 22.0f;
    static constexpr float keysX = 0.5f * (compressionX + outputX) - 0.5f * (3.0f * keyW + 2.0f * keyGap);
    static constexpr float keyLegendY = 274.0f, keyY0 = 296.0f, keyY1 = 322.0f;
    static constexpr float keyBracketY = 345.0f;                           // the METER bracket's line
    static constexpr float modelY = 96.0f, seriesY = 118.0f;               // above the meter: VCA 60, REFERENCE SERIES
    static constexpr float badgeY = 296.0f, nameplateY = 320.0f;           // below it: MC-2, COMPRESSOR/LIMITER
    static constexpr float mixX = 1034.0f, mixY = 306.0f, mixRadius = 13.0f;
};

// The BUS face on the 1120 x 380 canvas at the readability floor (canvas coordinates; text y is its top): a frame
// round the face whose left column carries the name on its top edge, the meter, the IN switch and the series line on
// its bottom edge; two rows of controls to its right. Only THRESHOLD and MAKE-UP can hold a value beyond their
// travel (older sessions), both in the first row, and the line under its legends is kept for that read-out.
struct BusFaceplateLayout
{
    static constexpr float left = 0.0f, right = kDesignWidth, top = kFaceAreaTop + 1.0f, bottom = kDesignHeight - 2.0f;
    static constexpr float ear = 44.0f;                                   // the rack ears, either side of the plate
    static constexpr float frameX0 = 69.0f, frameX1 = 1051.0f, frameY0 = 92.0f, frameY1 = 362.0f;
    static constexpr float meterX0 = 91.0f, meterY0 = 110.0f, meterX1 = 344.0f, meterY1 = 256.0f;
    static constexpr float columnX = 0.5f * (meterX0 + meterX1);         // the left column's centre
    static constexpr float titleY = 81.0f;                                // BUS COMPRESSOR (22), on the frame's top
    static constexpr float seriesY = frameY1 - 7.0f;                      // STEREO BUS COMPRESSION (13), on its bottom
    static constexpr float inY = 301.0f;                                  // the IN switch's centre
    // MAKE-UP's last numeral and SC FILTER's first sit at one height: those two columns are further apart.
    static constexpr float col0 = 409.0f, col1 = 536.0f, col2 = 663.0f, col3 = 810.0f, col4 = 949.0f;
    static constexpr float row1Y = 152.0f, row2Y = 290.0f;
    static constexpr float knobRadius = 23.0f, dotRadius = 32.0f;         // the knobs and their scale's dots
    static constexpr float labelRadius = 46.0f;                           // the numerals' anchors
    static constexpr float legendDrop = 44.0f;                            // a legend's top, below its control's centre
    static constexpr float readoutDrop = legendDrop + 18.0f;              // a beyond-travel value, under the legend
    static constexpr float trimRadius = 13.0f;                            // HR
};

// The Studio VCA face on the 1120 x 380 canvas at the readability floor (canvas coordinates; text y is its top), in
// its reference unit's arrangement: the engraved name over INPUT, RATIO over ATTACK and THRESHOLD over RELEASE with
// AUTO RELEASE between them, the I/O meter with its switch and the gain reduction meter on the S-curve, MAKE UP GAIN
// over DRY/WET. The meters are larger than the reference's (bezel 90, was 72) so their numerals reach 14.
struct StudioVcaFaceplateLayout
{
    static constexpr float left = 0.0f, right = kDesignWidth, top = kFaceAreaTop + 1.0f, bottom = kDesignHeight - 2.0f;
    static constexpr float row1Y = 148.0f, row2Y = 283.0f;                     // the dials' centres
    static constexpr float inputX = 86.0f, ratioX = 240.0f, thresholdX = 398.0f, makeupX = 1013.0f;
    static constexpr float dialRadius = 25.0f, mixRadius = 22.0f;
    static constexpr float numeralOffset = 20.0f;                               // the numerals' anchors, past the edge
    static constexpr float legendDrop = 46.0f;                                  // a legend's top, below its dial
    static constexpr float nameX = inputX, nameY = 96.0f;                      // STUDIO (20) over VCA (40)
    static constexpr float lampX = 308.0f, lampY = 206.0f;                      // AUTO RELEASE, its legend under it
    static constexpr float meterRadius = 90.0f;                                 // the round meters' bezels
    static constexpr float ioX = 568.0f, ioY = 168.0f, switchY = 284.0f;        // the I/O meter and its switch
    static constexpr float grX = 838.0f, grY = 234.0f;                          // the gain reduction meter
};

// The Digital and Multiband faces on the 1120 x 380 canvas at the readability floor (canvas coordinates; a text y is
// its top). Both carry the title row, the INPUT and OUTPUT meters at the rack's edges and the foot line; Digital puts
// the gain reduction display, the side-chain high-pass and two rows of knobs between the meters, Multiband the band
// meters and the selected band's knobs. A knob's name (16) and value (17) clear its ring of ticks (radius + 8).
struct ModernFaceLayout
{
    static constexpr float top = kFaceAreaTop + 1.0f, bottom = kDesignHeight - 2.0f;   // the rack
    static constexpr float ear = 32.0f;                                                   // its ears
    static constexpr float titleY = 81.0f, ruleY = 102.0f;          // the name (16), the rule under the title row
    static constexpr float printX0 = 111.0f, printX1 = 1009.0f;     // MC-2 and the series line (13), the rule's ends
    static constexpr float descriptionY = 358.0f;                   // the foot line (13)
    // INPUT and OUTPUT: the title (16), the LEDs and the readout (17)
    static constexpr float meterTitleY = 106.0f, meterTop = 126.0f, meterBottom = 312.0f, meterReadoutY = 316.0f;
    static constexpr float inputMeterX = 43.0f, outputMeterX = 1037.0f, meterW = 40.0f;
    static constexpr float knobRing = 8.0f, knobTextGap = 2.0f;
    static constexpr float nameTop(float y, float r) noexcept { return y - r - knobRing - knobTextGap - 16.0f; }
    static constexpr float valueTop(float y, float r) noexcept { return y + r + knobRing + knobTextGap; }
    // Digital: the display with its caption band, the side-chain high-pass, and the knobs in two rows of five
    static constexpr float vuX0 = 112.0f, vuX1 = 452.0f, vuTop = 110.0f, vuBottom = 268.0f, captionBottom = 292.0f;
    static constexpr float grLegendY = 298.0f;                      // GAIN REDUCTION
    static constexpr float scHpX = 492.0f, scHpTrackTop = 132.0f, scHpTrackBottom = 270.0f;
    static constexpr float digitalDividerX = 533.0f;
    static constexpr float digitalKnobRadius = 27.0f, digitalRow1Y = 161.0f, digitalRow2Y = 288.0f;
    static constexpr float digitalColumnX(int i) noexcept { return 582.0f + 94.0f * static_cast<float>(i); }
    // Multiband: the enable strip, each band's tab with B and S, its meter and the crossover faders between them
    static constexpr float bandsX0 = 144.0f, bandsX1 = 600.0f;
    static constexpr float enableY0 = 108.0f, enableY1 = 134.0f, tabY0 = 138.0f, tabY1 = 164.0f;
    static constexpr float bandSwitchW = 18.0f, bandSwitchGap = 3.0f;
    static constexpr float bandMeterTop = 170.0f, bandMeterBottom = 298.0f, faderBottom = 326.0f;
    static constexpr float bandLegendY = 330.0f;                    // GAIN REDUCTION
    static constexpr float multibandDividerX = 620.0f;
    static constexpr float bandTagY = 106.0f, bandTagRuleY = 127.0f;   // LOW BAND (16) and its rule
    static constexpr float multibandKnobRadius = 22.0f, multibandRow1Y = 180.0f, multibandRow2Y = 294.0f;
    static constexpr float multibandKnobX(float column) noexcept { return 812.0f + 92.0f * column; }
    // The centre of the slot of `slot` of `slots` shown bands, and the boundary to its right.
    static constexpr float bandSlotWidth(int slots) noexcept { return (bandsX1 - bandsX0) / static_cast<float>(slots); }
    static constexpr float bandBoundaryX(int slot, int slots) noexcept
    {
        return bandsX0 + static_cast<float>(slot + 1) * bandSlotWidth(slots);
    }
};

// VCA-compressor-style threshold lamps: BELOW lights while the programme sits under the
// threshold, ABOVE from the threshold up. One boolean keeps the pair mutually
// exclusive by construction.
inline bool vcaSignalAboveThreshold(float inputDb, float thresholdDb) noexcept
{
    if (!std::isfinite(inputDb) || !std::isfinite(thresholdDb)) return false;
    return inputDb >= thresholdDb;
}

// The VCA face's meter selector is display-only hardware behaviour (the
// reference unit's three meter keys), so it is UI state, not a host parameter.
// Its words are the other faces': GR on the key and GAIN REDUCTION on the
// meter (owner 2026-11-06), where the reference says GAIN CHANGE.
inline constexpr int kVcaMeterSourceCount = 3;

// The meter's caption under DECIBELS: whichever source the needle shows.
inline const char* vcaMeterSourceLabel(int source) noexcept
{
    switch (source)
    {
        case 0: return "INPUT";
        case 1: return "OUTPUT";
        default: return "GAIN REDUCTION";
    }
}

// The legend over each meter key.
inline const char* vcaMeterKeyLegend(int source) noexcept
{
    return source == 0 || source == 1 ? vcaMeterSourceLabel(source) : "GR";
}

// Mode toolbar captions, set in capitals like every other legend on the
// panel. The host-facing enum labels stay kModes.
inline constexpr std::array<const char*, 8> kModeTabLabels{{
    "OPTO", "FET BLACK", "VCA", "BUS", "FET BLUE", "STUDIO VCA", "DIGITAL", "MULTIBAND"}};
static_assert(kModeTabLabels.size() == sizeof(kModes) / sizeof(kModes[0]),
              "one toolbar caption per mode");

inline float vcaSidechainSwitchTarget(bool currentlyEngaged,
                                      float currentHostValue,
                                      float& lastEngagedHostValue,
                                      float minimumHostValue,
                                      float maximumHostValue) noexcept
{
    if (currentlyEngaged)
    {
        lastEngagedHostValue = std::clamp(
            currentHostValue, minimumHostValue, maximumHostValue);
        return minimumHostValue;
    }
    return std::clamp(lastEngagedHostValue, minimumHostValue, maximumHostValue);
}

inline float optoMeterNeedleAngle(float gainReductionDb) noexcept
{
    return vintageVuNeedleAngle(gainReductionDb);
}

inline float optoMeterDisplayValue(float gainReductionDb) noexcept
{
    // Only a finite guard. The Opto and FET needles' movement runs in the plugin
    // on the audio clock (MultiCompGainReductionNeedle.hpp, a VU movement measured
    // against both references). (A UI-side one-pole lag of 300 ms once stood here:
    // a VU reaches 99% in 300 ms, so that pole returned the needle about four
    // times too late.)
    return std::isfinite(gainReductionDb) ? gainReductionDb : 0.0f;
}

inline constexpr const char* optoKnobValueSuffix() noexcept { return ""; }

inline bool fetTimingUsesDialReadout(uint32_t parameter) noexcept
{
    return parameter == static_cast<uint32_t>(ParamId::FetAttack)
        || parameter == static_cast<uint32_t>(ParamId::FetRelease);
}

// Vintage FET timing follows the reference unit's numbered 1-7 controls. Keep
// the legacy host ranges intact for automation/state compatibility and convert
// only the faceplate readout, gesture domain, and pointer position.
inline float fetTimingDialValue(float hostValue, uint32_t parameter) noexcept
{
    if (!fetTimingUsesDialReadout(parameter)) return hostValue;
    const auto& descriptor = kParams[parameter];
    const float minimum = hostMin(descriptor);
    const float range = hostMax(descriptor) - minimum;
    const float position = std::clamp((hostValue - minimum) / range, 0.0f, 1.0f);
    return 1.0f + 6.0f * position;
}

inline float fetTimingHostValue(float dialValue, uint32_t parameter) noexcept
{
    if (!fetTimingUsesDialReadout(parameter)) return dialValue;
    const auto& descriptor = kParams[parameter];
    const float position = std::clamp((dialValue - 1.0f) / 6.0f, 0.0f, 1.0f);
    return hostMin(descriptor)
        + position * (hostMax(descriptor) - hostMin(descriptor));
}

// FET BLUE's Attack has the reference's SLO detent at its counter-clockwise stop: the loop takes its slower attack
// and raised threshold (FetVoice::sloAttackScales, sloLoadDb) while the attack position is 0, i.e. while the plain
// value the DSP receives sits on the descriptor's minimum. The face reads the core's own position law
// (duskaudio::fetAttackPositionFromMilliseconds), so it names SLO exactly where the DSP engages it.
// The reference names that stop SLO in its own readout. The face prints SLO there, and its readout and typed entry
// name the detent.
inline constexpr const char* kFetSloText = "SLO";

inline bool fetAttackAtSlo(float plainMilliseconds) noexcept
{
    return duskaudio::fetAttackPositionFromMilliseconds(plainMilliseconds) <= 0.0f;
}

// Typed entry on FET BLUE's Attack, returning the host value: SLO (SLO, slo or Slo) selects the detent; a number is a
// dial reading as the face shows it (1-7, FET BLACK's conversion). Anything else is refused.
inline bool fetSloAttackTypedHost(const char* text, float& host) noexcept
{
    namespace entry = duskdaf::value_text;
    const auto attack = static_cast<uint32_t>(ParamId::FetAttack);
    if (entry::suffix(text, kFetSloText, "slo") || entry::suffix(text, "Slo"))
    {
        host = hostMin(kParams[attack]);
        return true;
    }
    const char* end = nullptr;
    float dial = 0.0f;
    if (!entry::parseDecimal(text, end, dial) || !entry::finish(end, "")) return false;
    host = fetTimingHostValue(dial, attack);
    return true;
}

inline bool optoKnobUsesPlainDomainDrag(uint32_t parameter) noexcept
{
    return parameter == static_cast<uint32_t>(ParamId::SidechainHP);
}

inline const char* optoModeLabel(float hostValue) noexcept
{
    return choiceIndex(hostValue, 2) == 0 ? "COMPRESS" : "LIMIT";
}

inline float optoMeterReadoutAmount(float gainReductionDb) noexcept
{
    return std::clamp(std::max(0.0f, -gainReductionDb), 0.0f, 99.9f);
}

template <size_t N>
inline int loadProgramIntoMirror(uint32_t index, std::array<float, N>& values)
{
    if (index >= kFactoryPresets.size()) return -1;
    applyFactoryPresetToHostParameters(index,
        [&values](int parameterIndex, float hostValue)
        {
            if (parameterIndex >= 0 && static_cast<size_t>(parameterIndex) < values.size())
                values[static_cast<size_t>(parameterIndex)] = hostValue;
        });
    return static_cast<int>(index);
}

inline bool selectionOwnsParam(int currentFactoryPreset, bool userPresetActive,
                               bool defaultsActive, uint32_t parameterIndex) noexcept
{
    if (isDisplayParameter(static_cast<int>(parameterIndex))) return false;
    if (currentFactoryPreset >= 0)
        return presetOwnsParam(currentFactoryPreset, parameterIndex);
    if (userPresetActive || defaultsActive)
        return parameterIndex != static_cast<uint32_t>(ParamId::Bypass);
    return false;
}

// Mirrors the plugin's ordered crossover set, so raising one handle also shows
// the DSP pushing its neighbours. skipIndex excludes the handle the user is
// dragging: only the AU wrapper applies a UI parameter write synchronously
// (CLAP queues it to the next process() call, LV2 to an atom port), so reading
// that one back mid-gesture returns the pre-edit value and drags the handle
// backwards under the mouse. Its neighbours have no such pending write.
template <size_t N, typename ReadParameter>
inline void refreshCrossoverMirror(std::array<float, N>& values, ReadParameter readParameter,
                                   uint32_t skipIndex = ~uint32_t(0))
{
    const auto x1 = static_cast<uint32_t>(ParamId::Crossover1);
    const auto x3 = static_cast<uint32_t>(ParamId::Crossover3);
    if (x3 >= N) return;
    for (uint32_t index = x1; index <= x3; ++index)
        if (index != skipIndex)
            values[index] = readParameter(index);
}

// A host may set parameters before it creates the editor, and DPF does not
// replay those earlier parameterChanged() calls to a newly constructed UI.
// Seed the mirror once from the live plugin so the first frame represents the
// state the host actually opened rather than the compile-time defaults.
template <size_t N, typename ReadParameter>
inline void refreshParameterMirror(std::array<float, N>& values,
                                   ReadParameter readParameter,
                                   uint32_t parameterCount)
{
    const uint32_t count = std::min(parameterCount, static_cast<uint32_t>(N));
    for (uint32_t index = 0; index < count; ++index)
        values[index] = readParameter(index);
}
// Studio VCA face dials: the reference panel's knob travel (0..1) for a control's plain value, and back. Ratio and
// Attack follow the reference's own dial laws (MultiCompStudioVcaLaw.hpp); the others are linear. The host ranges are
// wider than the panel's (they keep older sessions' values: threshold up to +20 dB, ratio from 1:1, attack to 75 ms,
// make-up from -20 dB), so such a value pins the dial at the end it lies beyond; typed entry still reaches it.
inline float studioVcaDialPosition(uint32_t parameter, float plain) noexcept
{
    namespace law = duskaudio::studioVcaLaw;
    const auto linear = [plain](float lo, float hi) { return std::clamp((plain - lo) / (hi - lo), 0.0f, 1.0f); };
    switch (static_cast<ParamId>(parameter))
    {
        case ParamId::StudioVcaThreshold: return linear(-50.0f, -10.0f);
        case ParamId::StudioVcaRatio: return law::knobForRatio(plain);
        case ParamId::StudioVcaAttack: return law::knobForAttackMs(plain);
        case ParamId::StudioVcaRelease: return linear(100.0f, 4000.0f);
        case ParamId::StudioVcaOutput: return linear(0.0f, 40.0f);
        case ParamId::StudioVcaInput: return linear(-18.0f, 18.0f);
        case ParamId::Mix: return linear(0.0f, 100.0f);
        default: return 0.0f;
    }
}

inline float studioVcaDialPlain(uint32_t parameter, float position) noexcept
{
    namespace law = duskaudio::studioVcaLaw;
    const float t = std::clamp(position, 0.0f, 1.0f);
    const auto linear = [t](float lo, float hi) { return lo + (hi - lo) * t; };
    switch (static_cast<ParamId>(parameter))
    {
        case ParamId::StudioVcaThreshold: return linear(-50.0f, -10.0f);
        case ParamId::StudioVcaRatio: return law::ratioForKnob(t);
        case ParamId::StudioVcaAttack: return law::attackMsForKnob(t);
        case ParamId::StudioVcaRelease: return linear(100.0f, 4000.0f);
        case ParamId::StudioVcaOutput: return linear(0.0f, 40.0f);
        case ParamId::StudioVcaInput: return linear(-18.0f, 18.0f);
        case ParamId::Mix: return linear(0.0f, 100.0f);
        default: return 0.0f;
    }
}

// A value typed into a Studio VCA dial, in the control's plain unit (Release in ms). The unit may be left out, as the
// readout prints it, or named: dB (or db), :1, ms, %, and for Release s (the dial's unit, also a bare number's) or ms.
inline bool studioVcaTypedPlain(const char* text, uint32_t parameter, float& plain) noexcept
{
    namespace entry = duskdaf::value_text;
    const char* end = nullptr;
    float typed = 0.0f;
    if (!entry::parseDecimal(text, end, typed)) return false;
    const bool bare = entry::finish(end, "");
    plain = typed;
    switch (static_cast<ParamId>(parameter))
    {
        case ParamId::StudioVcaRelease:
            if (entry::suffix(end, "ms")) return true;
            plain = typed * 1000.0f;
            return bare || entry::suffix(end, "s");
        case ParamId::StudioVcaRatio: return bare || entry::suffix(end, ":1");
        case ParamId::StudioVcaAttack: return bare || entry::suffix(end, "ms");
        case ParamId::Mix: return bare || entry::suffix(end, "%");
        default: return bare || entry::suffix(end, "dB", "db");
    }
}

// The Studio VCA face's needles, as the reference's sweep (measured 2026-10-26): +-30 degrees from straight up over
// minDb..maxDb, linear in dB (the I/O meter -60..0, 1 degree per dB; gain reduction -20..0, 3 degrees per dB),
// pinned at the ends; a non-finite reading rests at the left.
inline float studioVcaNeedleDegrees(float db, float minDb, float maxDb) noexcept
{
    const float t = std::isfinite(db) ? std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f) : 0.0f;
    return -30.0f + 60.0f * t;
}

// The Digital and Multiband faces follow the original JUCE editor (EnhancedCompressorEditor, ModernCompressorPanels.h,
// AnalogLookAndFeel.cpp). Their moving parts keep its timing, transcribed here frame-rate independent.

// The number under an INPUT / OUTPUT ladder (EnhancedCompressorEditor::updateMeters): every 0.2 s it jumps up to the
// loudest reading since the last update, or else falls 1.2 dB (6 dB/s), never below -60 dB.
struct LevelReadout
{
    float shownDb = -60.0f, peakDb = -60.0f, clock = 0.0f;
};

inline void advanceLevelReadout(LevelReadout& readout, float db, float dtSeconds) noexcept
{
    if (std::isfinite(db)) readout.peakDb = std::max(readout.peakDb, db);
    readout.clock += std::clamp(dtSeconds, 0.0f, 0.1f);
    if (readout.clock < 0.2f) return;
    readout.clock -= 0.2f;
    readout.shownDb = std::max(readout.peakDb > readout.shownDb ? readout.peakDb : readout.shownDb - 1.2f, -60.0f);
    readout.peakDb = -60.0f;
}

// The Digital face's gain-reduction VU (AnalogVUMeter::timerCallback, 60 Hz steps): -20..+3 dB over 0..1, 50 ms
// attack and 150 ms release blended with a slightly underdamped spring (about 1 % overshoot), and a peak marker that
// holds the deepest reading for 2 s.
struct VuNeedle
{
    float position = 20.0f / 23.0f, velocity = 0.0f, pending = 0.0f;
    float peakDb = 0.0f, peakHold = 0.0f, peakPosition = 20.0f / 23.0f;
};

inline float vuNeedlePositionForDb(float db) noexcept
{
    const float clamped = std::isfinite(db) ? std::clamp(db, -20.0f, 3.0f) : 0.0f;
    return (clamped + 20.0f) / 23.0f;
}

inline void advanceVuNeedle(VuNeedle& needle, float grDb, float dtSeconds) noexcept
{
    constexpr float kStep = 1.0f / 60.0f;
    const float target = std::isfinite(grDb) ? grDb : 0.0f;
    if (target < needle.peakDb)
    {
        needle.peakDb = target;
        needle.peakHold = 2.0f;
    }
    needle.pending = std::min(needle.pending + std::clamp(dtSeconds, 0.0f, 0.25f), 0.25f);
    while (needle.pending >= kStep)
    {
        needle.pending -= kStep;
        const float targetPosition = vuNeedlePositionForDb(std::abs(target) < 0.001f ? 0.0f : target);
        const float displacement = targetPosition - needle.position;
        const float timeConstantMs = displacement < 0.0f ? 50.0f : 150.0f;
        const float coefficient = 1.0f - std::exp(-1000.0f * kStep / timeConstantMs);
        const float acceleration = displacement * 200.0f - needle.velocity * 0.65f * 2.0f * std::sqrt(200.0f);
        needle.velocity += acceleration * kStep;
        needle.position += needle.velocity * kStep;
        needle.position += coefficient * (targetPosition - needle.position) * 0.4f;
        needle.position = std::clamp(needle.position, 0.0f, 1.0f);
        if (std::abs(needle.velocity) < 0.0005f && std::abs(displacement) < 0.001f)
            needle.velocity = 0.0f;
        if (needle.peakHold > 0.0f)
        {
            needle.peakHold -= kStep;
            if (needle.peakHold <= 0.0f) needle.peakDb = target;
        }
        needle.peakPosition = vuNeedlePositionForDb(needle.peakDb);
    }
}

// A Multiband band's gain-reduction ladder (MultibandCompressorPanel::setBandGainReduction, 30 Hz steps): half way to
// a deeper reading per step, 15 % back per step, and a peak that holds for 30 steps (1 s) before falling 5 % per step.
struct BandGrMeter
{
    float shownDb = 0.0f, peakDb = 0.0f, pending = 0.0f;
    int peakHoldSteps = 0;
};

inline void advanceBandGrMeter(BandGrMeter& meter, float grDb, float dtSeconds) noexcept
{
    constexpr float kStep = 1.0f / 30.0f;
    const float target = std::isfinite(grDb) ? grDb : 0.0f;
    meter.pending = std::min(meter.pending + std::clamp(dtSeconds, 0.0f, 0.25f), 0.25f);
    while (meter.pending >= kStep)
    {
        meter.pending -= kStep;
        meter.shownDb += (target - meter.shownDb) * (target < meter.shownDb ? 0.5f : 0.15f);
        if (-meter.shownDb > -meter.peakDb)
        {
            meter.peakDb = meter.shownDb;
            meter.peakHoldSteps = 30;
        }
        else if (meter.peakHoldSteps > 0)
            --meter.peakHoldSteps;
        else
            meter.peakDb *= 0.95f;
    }
}

// A Multiband crossover fader's travel (CrossoverFader): the frequency's place between the control's limits on a
// logarithmic axis, 0 at the lowest frequency, and back.
inline float crossoverFaderPosition(float hz, float minHz, float maxHz) noexcept
{
    const float lo = std::max(0.1f, minHz), hi = std::max(lo + 0.1f, maxHz);
    return std::log(std::clamp(hz, lo, hi) / lo) / std::log(hi / lo);
}

inline float crossoverFaderFrequency(float position, float minHz, float maxHz) noexcept
{
    const float lo = std::max(0.1f, minHz), hi = std::max(lo + 0.1f, maxHz);
    return lo * std::pow(hi / lo, std::clamp(position, 0.0f, 1.0f));
}

// The bands the Multiband face shows: the enabled ones, repaired to at least two the way the DSP repairs its
// snapshot (MultiCompDSP's multiband block adds the lowest disabled bands), so the face never shows fewer bands
// than are playing. Bit b set = band b shown.
inline unsigned multibandShownBands(const bool enabled[4]) noexcept
{
    unsigned mask = 0;
    int count = 0;
    for (int band = 0; band < 4; ++band)
        if (enabled[band]) { mask |= 1u << band; ++count; }
    for (int band = 0; band < 4 && count < 2; ++band)
        if ((mask & (1u << band)) == 0) { mask |= 1u << band; ++count; }
    return mask;
}

} // namespace multicompp::ui_detail

#ifndef MULTICOMP_UI_LOGIC_TEST

#ifndef MULTICOMP_UI_TEST_HOST
#include "DafUI.hpp"
#endif
#include "MultiCompAccess.hpp"
#include "MultiCompVersion.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskKnobRing.hpp"
#include "DuskLedMeter.hpp"
#include "DuskVuMeter.hpp"
#include "MultiCompGainReductionNeedle.hpp"
#include "MultiCompVcaMeter.hpp"
#include "MultiCompVintageMeter.hpp"
#include "DuskSupportersOverlay.hpp"
#include "DuskUserPresetStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

DUSK_WEAK float multiCompGetParameterValue(void* pluginInstancePointer,
                                            uint32_t index) noexcept;

START_NAMESPACE_DAF

namespace
{
constexpr float kDesignW = multicompp::ui_detail::kDesignWidth;
constexpr float kDesignH = multicompp::ui_detail::kDesignHeight;
constexpr float kHeaderH = 44.0f;   // the title row
// The mode row under the header, and the face area under it (design coordinates).
constexpr float kModeRowY0 = multicompp::ui_detail::ModeRowLayout::y0;
constexpr float kModeRowY1 = multicompp::ui_detail::ModeRowLayout::y1;
constexpr float kFaceTop = multicompp::ui_detail::kFaceAreaTop;
constexpr float kUiPi = duskdaf::DuskPanel::kPi;
// Set to true only while collecting a host geometry measurement.
constexpr bool kGeometryDiagnosticEnabled = false;

constexpr ImU32 kPanel = IM_COL32(35, 37, 43, 255);
constexpr ImU32 kPanelRaised = IM_COL32(43, 46, 54, 255);
constexpr ImU32 kLine = IM_COL32(75, 79, 90, 255);
constexpr ImU32 kText = IM_COL32(232, 234, 238, 255);
constexpr ImU32 kDim = IM_COL32(162, 168, 180, 255);
constexpr ImU32 kAccent = IM_COL32(65, 194, 220, 255);
constexpr ImU32 kHeaderGreen = IM_COL32(76, 103, 48, 255);
constexpr ImU32 kHeaderGreenDark = IM_COL32(39, 57, 24, 255);
constexpr ImU32 kOptoInk = IM_COL32(31, 32, 31, 255);
constexpr ImU32 kOptoRed = IM_COL32(151, 25, 31, 255);
constexpr ImU32 kVcaInk = IM_COL32(228, 229, 224, 255);
constexpr ImU32 kVcaInkDim = IM_COL32(168, 170, 166, 255);
// Silkscreen and small functional accents on the Digital / Multiband rack faces.
constexpr ImU32 kModernText = IM_COL32(228, 229, 224, 255);
constexpr ImU32 kDigitalCyan = IM_COL32(121, 185, 191, 255);
constexpr ImU32 kBandColors[4] = {
    IM_COL32(205, 127, 117, 255), IM_COL32(210, 187, 116, 255),
    IM_COL32(137, 177, 141, 255), IM_COL32(127, 162, 196, 255)
};

// Parameter indices are aliases of the single table order in MultiCompParams.hpp.
using ParamId = multicompp::ParamId;
constexpr uint32_t P_MODE = static_cast<uint32_t>(ParamId::Mode), P_BYPASS = static_cast<uint32_t>(ParamId::Bypass), P_MIX = static_cast<uint32_t>(ParamId::Mix);
constexpr uint32_t P_SC_HP = static_cast<uint32_t>(ParamId::SidechainHP);
constexpr uint32_t P_AUTO_MAKEUP = static_cast<uint32_t>(ParamId::AutoMakeup);
#define MC_PID(name) static_cast<uint32_t>(ParamId::name)
constexpr uint32_t P_OPTO_PEAK = MC_PID(OptoPeakReduction), P_OPTO_GAIN = MC_PID(OptoGain), P_OPTO_LIMIT = MC_PID(OptoLimit);
constexpr uint32_t P_OPTO_METER = MC_PID(OptoMeter), P_FET_METER = MC_PID(FetMeter);
constexpr uint32_t P_FET_IN = MC_PID(FetInput), P_FET_OUT = MC_PID(FetOutput), P_FET_ATTACK = MC_PID(FetAttack), P_FET_RELEASE = MC_PID(FetRelease);
constexpr uint32_t P_FET_RATIO = MC_PID(FetRatio), P_FET_CURVE = MC_PID(FetCurve), P_FET_TRANSIENT = MC_PID(FetTransient), P_FET_THRESHOLD = MC_PID(FetThreshold);
constexpr uint32_t P_FET_BLUE_RATIO = MC_PID(FetBlueRatio), P_FET_BLUE_SC = MC_PID(FetBlueScFilter), P_FET_BLUE_HEADROOM = MC_PID(FetBlueHeadroom);
constexpr uint32_t P_VCA_THRESHOLD = MC_PID(VcaThreshold), P_VCA_RATIO = MC_PID(VcaRatio), P_VCA_ATTACK = MC_PID(VcaAttack), P_VCA_RELEASE = MC_PID(VcaRelease);
constexpr uint32_t P_VCA_OUT = MC_PID(VcaOutput), P_VCA_OVER_EASY = MC_PID(VcaOverEasy), P_VCA_DETECTOR = MC_PID(VcaClassicDetector);
constexpr uint32_t P_BUS_THRESHOLD = MC_PID(BusThreshold), P_BUS_RATIO = MC_PID(BusRatio), P_BUS_ATTACK = MC_PID(BusAttack), P_BUS_RELEASE = MC_PID(BusRelease);
constexpr uint32_t P_BUS_MAKEUP = MC_PID(BusMakeup), P_BUS_MIX = MC_PID(BusMix);
constexpr uint32_t P_BUS_HEADROOM = MC_PID(BusHeadroom), P_BUS_FADE_RATE = MC_PID(BusFadeRate);
constexpr uint32_t P_BUS_FADE = MC_PID(BusFade), P_BUS_SC = MC_PID(SidechainHP);
constexpr uint32_t P_SVCA_THRESHOLD = MC_PID(StudioVcaThreshold), P_SVCA_RATIO = MC_PID(StudioVcaRatio), P_SVCA_ATTACK = MC_PID(StudioVcaAttack);
constexpr uint32_t P_SVCA_RELEASE = MC_PID(StudioVcaRelease), P_SVCA_OUT = MC_PID(StudioVcaOutput);
constexpr uint32_t P_SVCA_INPUT = MC_PID(StudioVcaInput), P_SVCA_AUTO = MC_PID(StudioVcaAutoRelease);
constexpr uint32_t P_SVCA_METER = MC_PID(StudioVcaMeter);
constexpr uint32_t P_DIG_THRESHOLD = MC_PID(DigitalThreshold), P_DIG_RATIO = MC_PID(DigitalRatio), P_DIG_KNEE = MC_PID(DigitalKnee), P_DIG_ATTACK = MC_PID(DigitalAttack);
constexpr uint32_t P_DIG_RELEASE = MC_PID(DigitalRelease), P_DIG_LOOK = MC_PID(DigitalLookahead), P_DIG_MIX = MC_PID(DigitalMix), P_DIG_OUT = MC_PID(DigitalOutput);
constexpr uint32_t P_DIG_ADAPT = MC_PID(DigitalAdaptive), P_X1 = MC_PID(Crossover1), P_X2 = MC_PID(Crossover2), P_X3 = MC_PID(Crossover3);
constexpr uint32_t P_MB_MIX = MC_PID(MbMix), P_MB_OUT = MC_PID(MbOutput);
#undef MC_PID

constexpr uint32_t kMeterMaster = static_cast<uint32_t>(multicompp::kMeterMaster);
constexpr uint32_t kMeterBand0 = static_cast<uint32_t>(multicompp::kMeterBand0);

const char* bandName(int band)
{
    static const char* names[] = {"LOW", "LO-MID", "HI-MID", "HIGH"};
    return names[band < 0 ? 0 : band > 3 ? 3 : band];
}

// The original editor's colour arithmetic (juce::Colour), for the Digital and Multiband faces.
ImU32 colourWithAlpha(ImU32 colour, float alpha)
{
    const auto a = static_cast<unsigned>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    return (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 colourBrighter(ImU32 colour, float amount)
{
    const float k = 1.0f / (1.0f + amount);
    const auto channel = [&](int shift) {
        const float v = static_cast<float>((colour >> shift) & 0xFFu);
        return static_cast<unsigned>(255.0f - k * (255.0f - v)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
         | (colour & IM_COL32_A_MASK);
}

ImU32 colourDarker(ImU32 colour, float amount)
{
    const float k = 1.0f / (1.0f + amount);
    const auto channel = [&](int shift) {
        return static_cast<unsigned>(k * static_cast<float>((colour >> shift) & 0xFFu)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
         | (colour & IM_COL32_A_MASK);
}

ImU32 colourMix(ImU32 from, ImU32 to, float t)
{
    const float k = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [&](int shift) {
        const float a = static_cast<float>((from >> shift) & 0xFFu), b = static_cast<float>((to >> shift) & 0xFFu);
        return static_cast<unsigned>(std::lround(a + (b - a) * k)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
         | channel(IM_COL32_A_SHIFT);
}

// Recolours the vertices drawn since `firstVertex` with the linear gradient from c0 at p0 to c1 at p1 (a
// juce::ColourGradient), keeping each vertex's own alpha as coverage so anti-aliased edges stay soft.
void shadeLinear(ImDrawList* dl, int firstVertex, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(c0), b = ImGui::ColorConvertU32ToFloat4(c1);
    const ImVec2 axis(p1.x - p0.x, p1.y - p0.y);
    const float lengthSquared = axis.x * axis.x + axis.y * axis.y;
    for (int i = firstVertex; i < dl->VtxBuffer.Size; ++i)
    {
        ImDrawVert& vertex = dl->VtxBuffer[i];
        const float t = lengthSquared > 0.0f
            ? std::clamp(((vertex.pos.x - p0.x) * axis.x + (vertex.pos.y - p0.y) * axis.y) / lengthSquared, 0.0f, 1.0f)
            : 0.0f;
        const float coverage = static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f;
        vertex.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                                           a.z + (b.z - a.z) * t, (a.w + (b.w - a.w) * t) * coverage));
    }
}

void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    const int first = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
    shadeLinear(dl, first, p0, p1, c0, c1);
}

void gradientCircle(ImDrawList* dl, ImVec2 c, float r, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    const int first = dl->VtxBuffer.Size;
    dl->AddCircleFilled(c, r, IM_COL32_WHITE, 64);
    shadeLinear(dl, first, p0, p1, c0, c1);
}
}

class MultiCompUI final : public UI, public duskdaf::ParamHost
{
public:
    MultiCompUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            values[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });

        setGeometryConstraints(static_cast<uint32_t>(kDesignW), static_cast<uint32_t>(kDesignH), true);
        // The type scale (owner direction 2026-11-05): 16 legends, 17 values, 20 sections, 14 the family's version
        // line and meter scales, 13 small print, 22 / 24 the header's brand and title, 30 face titles.
        static const float kFontSizes[] = {9.f, 11.f, 13.f, 14.f, 16.f, 17.f, 20.f, 22.f, 24.f, 30.f};
        fontSet = duskdaf::loadCrispFontSet(kFontSizes, 10, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        panel.setValueBubbleTextSize(17.0f);   // the readability floor's value size
        vcaLastScHp = hostValueForPlain(P_SC_HP, kVcaPullScFirstHz);
        scanUserPresets();
    }

    ~MultiCompUI() override { finishTrackedEdits(true); }

    void beginEdit(uint32_t idx) override
    {
        if (drawingTrackedControl)
        {
            trackedEditsOpen[idx] = true;
            trackedEditOwners[idx] = drawingTrackedId;
            trackedEditsDrawn[idx] = true;
        }
        editParameter(idx, true);
    }
    void endEdit(uint32_t idx) override
    {
        if (drawingTrackedControl)
        {
            // A hidden/disabled control may already have closed its gesture.
            if (!trackedEditsOpen[idx]) return;
            trackedEditsOpen[idx] = false;
        }
        editParameter(idx, false);
    }
    void setParam(uint32_t idx, float value) override
    {
        if (!multicompp::isControlParameter(static_cast<int>(idx))) return;
        clearPresetSelectionForEdit(idx);
        values[idx] = value;
        setParameterValue(idx, value);
        if (idx >= P_X1 && idx <= P_X3) refreshCrossoverMirror();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values.size()) values[index] = value;
        if (index >= P_X1 && index <= P_X3) refreshCrossoverMirror();
        if (multicompp::isControlParameter(static_cast<int>(index))
            && index != P_BYPASS)
            syncPresetSelection();
    }

    // A restored state arrives as its text. With the plugin reachable the controls are read back from it instead, so
    // the face shows what it runs: DAF's LV2 wrapper applies the ports a host restores with the state over it on the
    // first run, so the two can differ.
    void stateChanged(const char* key, const char* state) override
    {
        if (key == nullptr || state == nullptr || std::strcmp(key, "parameters") != 0) return;
        void* const instance = multiCompGetParameterValue != nullptr ? getPluginInstancePointer() : nullptr;
        if (instance != nullptr)
        {
            for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
                values[static_cast<size_t>(i)] = multiCompGetParameterValue(instance, static_cast<uint32_t>(i));
        }
        else
        {
            multicompp::StateValues decoded{};
            if (!multicompp::decodeState(state, decoded)) return;
            for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
                values[static_cast<size_t>(i)] = decoded[static_cast<size_t>(i)];
        }
        syncPresetSelection();
        repaint();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = multicompp::ui_detail::loadProgramIntoMirror(index, values);
        currentUserName.clear();
        currentUserPath.clear();
        defaultsActive = currentPreset < 0 && matchesDefaults();
    }

    void uiIdle() override
    {
        seedParameterMirror();
        refreshCrossoverMirror();
        repaint();
    }

    void onImGuiDisplay() override
    {
        seedParameterMirror();
        trackedEditsDrawn.fill(false);

        const float winW = static_cast<float>(getWidth());
        const float winH = static_cast<float>(getHeight());
        const float scale = std::min(winW / kDesignW, winH / kDesignH);
        const ImVec2 origin((winW - kDesignW * scale) * 0.5f,
                            (winH - kDesignH * scale) * 0.5f);
        if (kGeometryDiagnosticEnabled && !geometryDiagnosticLogged)
        {
            GLint viewport[4]{};
            glGetIntegerv(GL_VIEWPORT, viewport);
            std::fprintf(stderr,
                         "[MultiCompUI geometry] width=%u height=%u scaleFactor=%.3f "
                         "design=%.0fx%.0f scale=%.6f origin=(%.3f,%.3f) "
                         "glViewport=(%d,%d %dx%d)\n",
                         static_cast<unsigned>(getWidth()),
                         static_cast<unsigned>(getHeight()),
                         static_cast<double>(getScaleFactor()),
                         static_cast<double>(kDesignW), static_cast<double>(kDesignH),
                         static_cast<double>(scale), static_cast<double>(origin.x),
                         static_cast<double>(origin.y), viewport[0], viewport[1],
                         viewport[2], viewport[3]);
            std::fflush(stderr);
            geometryDiagnosticLogged = true;
        }
        panel.begin(scale, origin, labelFont, this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        // NoBringToFrontOnFocus: a click on the header or the mode row must not raise the face above the settings
        // drawer's window (it would hide the drawer and take its pointer).
        ImGui::Begin("##MultiComp2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(21, 22, 25, 255));
        // Treat the supporters panel as a modal: the panel stays visible beneath
        // its scrim but takes no clicks, wheel or drags (a click meant to close
        // the overlay used to land on INIT, a mode button or a knob). Snapshot
        // before drawHeader(), where a title click can open it.
        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);
        drawModeRow(dl);

        // The settings drawer is modal to the face under it: the face stays drawn but takes no input, and the
        // drawer is a window of its own above it (below), so no face item under the pointer can take the hover
        // from the drawer's controls (disabled ImGui items still claim it where they overlap).
        const bool drawerOpen = showSettings && !modalOpen;
        if (drawerOpen)
            ImGui::BeginDisabled();
        drawModePanel(dl);
        if (drawerOpen)
            ImGui::EndDisabled();
        if (modalOpen)
            ImGui::EndDisabled();

        // Overlays and the resize grip live in the design canvas.

        if (showSupporters)
            duskdaf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "Multi-Comp 2", MULTICOMP2_VERSION_STRING,
                                           &supporters);

        // The design size is the minimum. With the drawer open the grip is the drawer's (drawSettingsDrawer).
        duskdaf::ResizeGripState grip;
        if (!drawerOpen)
            grip = panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH, 1.0f);
        ImGui::End();
        if (drawerOpen)
        {
            // From below the mode row to the window's own corner, letterboxed or not: the drawer covers the face, and
            // the grip in that corner goes with it.
            const float drawerTop = panel.P(0.0f, kFaceTop).y;
            ImGui::SetNextWindowPos(ImVec2(0.0f, drawerTop));
            ImGui::SetNextWindowSize(ImVec2(winW, winH - drawerTop));
            ImGui::Begin("##MultiComp2Settings", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoSavedSettings);
            grip = drawSettingsDrawer(ImGui::GetWindowDrawList(), winW, winH);
            ImGui::End();
        }
        // After every window: a control drawn in either one keeps its gesture this frame.
        finishTrackedEdits(false);
        // What owned Escape this frame, for the drawer's next one: with keyboard navigation on (DAF turns it on),
        // ImGui's own Escape handling cancels a held control or closes a popup inside NewFrame, before the drawer
        // sees the key.
        escapeOwnedLastFrame = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()
            || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        ImGui::PopStyleVar(2);
        // A host can service the request synchronously, so never resize from
        // inside the active ImGui window.
        if (grip.resized)
            setSize(grip.width, grip.height);
    }

private:
    std::array<float, multicompp::kTotalParamCount> values{};
    // Every knob opts in (beginTrackedControl), and so do Digital/Multiband's own
    // drag controls. A host mode/band/Auto Makeup change can remove a held widget
    // before ImGui reports deactivation; finishTrackedEdits then closes its gesture.
    // A gesture belongs to the widget that opened it (its ImGui ID): another
    // face's control for the same parameter does not hold it open.
    std::array<bool, multicompp::kTotalParamCount> trackedEditsOpen{}, trackedEditsDrawn{};
    std::array<ImGuiID, multicompp::kTotalParamCount> trackedEditOwners{};
    bool drawingTrackedControl = false;
    ImGuiID drawingTrackedId = 0;
    duskdaf::DuskPanel panel;
    ImDrawListSplitter optoKnobSplitter;
    ImDrawListSplitter fetKnobSplitter;
    ImDrawListSplitter vcaKnobSplitter;
    // Display-only hardware behaviour (the meter keys INPUT / OUTPUT / GR):
    // UI state, deliberately not a host parameter.
    int vcaMeterSource = 2;
    float vcaVuNeedle = 0.0f;
    // The cutoff PULL/SC restores when it goes in (host value; any 1 Hz or more engages it). Until the switch has
    // been in during this editor's life, 20 Hz: it engages the switch, and as the sidechain high-pass the Opto, Bus
    // and Digital faces share it is close to transparent there (the range's top, 500 Hz, was the old first value).
    static constexpr float kVcaPullScFirstHz = 20.0f;
    float vcaLastScHp = 0.0f;
    float busVuNeedle = 0.0f;
    ImDrawListSplitter busKnobSplitter;
    ImDrawListSplitter studioVcaKnobSplitter;
    duskdaf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    duskdaf::SupportersOverlay supporters;
    bool showSupporters = false;
    bool showSettings = false;   // the settings drawer (drawSettingsDrawer)
    bool escapeOwnedLastFrame = false;   // a typed value, a held control or a popup had Escape last frame
    bool geometryDiagnosticLogged = false;
    bool parameterMirrorSeeded = false;
    static constexpr uint32_t kNoCrossover = ~uint32_t(0);
    // The crossover handle under an active drag. Owns both the open automation
    // gesture and the mirror's skip, so the two cannot disagree.
    uint32_t draggedCrossover = kNoCrossover;
    // The held crossover's place on its logarithmic travel, moved by the drag.
    float crossoverDragPosition = 0.0f;
    // Digital and Multiband display state (UI only, never host parameters).
    int facedMode = -1;
    duskdaf::LedLadder inputLadder, outputLadder;
    multicompp::ui_detail::LevelReadout inputReadout, outputReadout;
    multicompp::ui_detail::VuNeedle digitalVu;
    bool digitalShowsHistory = false;
    std::array<float, 128> grHistory{};
    int grHistoryWrite = 0;
    float grHistoryClock = 0.0f;
    bool sidechainHpReset = false;
    int selectedBand = 0;
    std::array<multicompp::ui_detail::BandGrMeter, 4> bandGrMeters{};
    int currentPreset = -1;
    bool defaultsActive = true;
    std::string currentUserName;
    std::string currentUserPath;
    struct UserPreset
    {
        std::string name;
        std::string path;
        multicompp::StateValues values{};
    };
    std::vector<UserPreset> userPresets;
    char saveBuf[64] = {};
    bool saveFailed = false;

    float value(uint32_t p) const { return values[p]; }

    // A drawn control holds its parameter's open gesture only if it is the widget
    // that opened it; `drawn` false (a disabled control) holds nothing.
    void markTrackedDrawn(uint32_t p, ImGuiID widget, bool drawn) noexcept
    {
        if (drawn && trackedEditsOpen[p] && trackedEditOwners[p] == widget) trackedEditsDrawn[p] = true;
    }
    // Around a knob's draw, in the ID scope the knob itself uses: `id` is its
    // widget id, so a gesture it opens is recorded as its own.
    void beginTrackedControl(uint32_t p, const char* id, bool drawn) noexcept
    {
        drawingTrackedId = ImGui::GetID(id);
        drawingTrackedControl = true;
        markTrackedDrawn(p, drawingTrackedId, drawn);
    }
    void endTrackedControl() noexcept { drawingTrackedControl = false; }

    void finishTrackedEdits(bool closeAll)
    {
        for (size_t p = 0; p < trackedEditsOpen.size(); ++p)
            if (trackedEditsOpen[p] && (closeAll || !trackedEditsDrawn[p]))
            {
                editParameter(static_cast<uint32_t>(p), false);
                trackedEditsOpen[p] = false;
                if (p == draggedCrossover) draggedCrossover = kNoCrossover;
                if (p == P_SC_HP) sidechainHpReset = false;
            }
    }

    // The external key the detector actually uses: requested, and connected. The split/out-of-process fallback has
    // only the requested key source; monolithic formats can mirror the detector the core actually uses.
    bool externalKeyInUse() const
    {
        if (values[static_cast<uint32_t>(ParamId::ExternalSidechain)] <= 0.5f) return false;
        void* const instance = getPluginInstancePointer();
        return instance == nullptr || multiCompGetExternalSidechainActive == nullptr
            || multiCompGetExternalSidechainActive(instance) > 0.5f;
    }

    bool modernManualOutputAvailable() const
    {
        return values[P_AUTO_MAKEUP] <= 0.5f || externalKeyInUse();
    }

    void clearPresetSelection(bool atDefaults)
    {
        currentPreset = -1;
        currentUserName.clear();
        currentUserPath.clear();
        defaultsActive = atDefaults;
    }

    void clearPresetSelectionForEdit(uint32_t parameterIndex)
    {
        if (multicompp::ui_detail::selectionOwnsParam(
                currentPreset, !currentUserName.empty(), defaultsActive,
                parameterIndex))
            clearPresetSelection(false);
    }

    bool parameterMatches(int index, float expected) const
    {
        return multicompp::hostValueMatches(index, values[static_cast<size_t>(index)], expected);
    }

    bool matchesDefaults() const
    {
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        {
            if (i == static_cast<int>(P_BYPASS) || multicompp::isDisplayParameter(i)) continue;
            const float expected = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) {
                    return multicompp::hostDefault(d);
                });
            if (!parameterMatches(i, expected)) return false;
        }
        return true;
    }

    int deriveFactoryPreset() const
    {
        return multicompp::matchingFactoryPreset(
            [this](int index) { return values[static_cast<size_t>(index)]; });
    }

    int deriveUserPreset() const
    {
        for (size_t presetIndex = 0; presetIndex < userPresets.size(); ++presetIndex)
        {
            bool matches = true;
            for (int i = 0; i < multicompp::kTotalParamCount && matches; i = multicompp::nextControlParameter(i))
                if (i != static_cast<int>(P_BYPASS) && !multicompp::isDisplayParameter(i)
                    && !parameterMatches(
                        i, userPresets[presetIndex].values[static_cast<size_t>(i)]))
                    matches = false;
            if (matches) return static_cast<int>(presetIndex);
        }
        return -1;
    }

    void syncPresetSelection()
    {
        if (matchesDefaults())
        {
            clearPresetSelection(true);
            return;
        }
        const int factory = deriveFactoryPreset();
        if (factory >= 0)
        {
            currentPreset = factory;
            currentUserName.clear();
            currentUserPath.clear();
            defaultsActive = false;
            return;
        }
        const int user = deriveUserPreset();
        if (user >= 0)
        {
            currentPreset = -1;
            currentUserName = userPresets[static_cast<size_t>(user)].name;
            currentUserPath = userPresets[static_cast<size_t>(user)].path;
            defaultsActive = false;
            return;
        }
        clearPresetSelection(false);
    }

    void refreshCrossoverMirror()
    {
        if (multiCompGetParameterValue == nullptr) return;
        void* const instance = getPluginInstancePointer();
        if (instance == nullptr) return;
        multicompp::ui_detail::refreshCrossoverMirror(values,
            [instance](uint32_t index) { return multiCompGetParameterValue(instance, index); },
            draggedCrossover);
    }

    void seedParameterMirror()
    {
        if (parameterMirrorSeeded || multiCompGetParameterValue == nullptr) return;
        void* const instance = getPluginInstancePointer();
        if (instance == nullptr) return;
        multicompp::ui_detail::refreshParameterMirror(values,
            [instance](uint32_t index) { return multiCompGetParameterValue(instance, index); },
            static_cast<uint32_t>(multicompp::kTotalParamCount));
        parameterMirrorSeeded = true;
        syncPresetSelection();
    }

    void setValue(uint32_t p, float v)
    {
        clearPresetSelectionForEdit(p);
        editParameter(p, true);
        const float hostValue = hostValueForPlain(p, v);
        values[p] = hostValue;
        setParameterValue(p, hostValue);
        editParameter(p, false);
    }

    void setHostValue(uint32_t p, float hostValue)
    {
        clearPresetSelectionForEdit(p);
        editParameter(p, true);
        values[p] = hostValue;
        setParameterValue(p, hostValue);
        editParameter(p, false);
        if (p >= P_X1 && p <= P_X3) refreshCrossoverMirror();
    }

    bool combo(const char* id, uint32_t p, const char* const* labels, int count,
               float x, float y, float w, const char* caption)
    {
        panel.text(ImGui::GetWindowDrawList(), x, y, 9.5f, kDim, caption, 0, true);
        const int selected = multicompp::ui_detail::choiceIndex(value(p), count);
        const ImVec2 p0 = panel.P(x - w * 0.5f, y + 16);
        const ImVec2 p1 = panel.P(x + w * 0.5f, y + 43);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(24, 25, 29, 255), 3 * panel.scale());
        dl->AddRect(p0, p1, kLine, 3 * panel.scale(), 0, panel.scale());
        panel.text(dl, x, y + 23, 10, kText, labels[selected], 0);
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemWidth(p1.x - p0.x);
        char hiddenId[64];
        std::snprintf(hiddenId, sizeof(hiddenId), "##%s", id);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        bool changed = false;
        const bool open = ImGui::BeginCombo(hiddenId, labels[selected], ImGuiComboFlags_NoArrowButton);
        ImGui::PopStyleColor(4);
        if (open)
        {
            for (int i = 0; i < count; ++i)
            {
                const bool isSelected = i == selected;
                if (ImGui::Selectable(labels[i], isSelected))
                {
                    setValue(p, static_cast<float>(i));
                    changed = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool headerChevron(ImDrawList* dl, const char* id, float cx, bool left)
    {
        constexpr float cy = 22.0f;
        constexpr float halfH = 12.5f;
        const ImVec2 b0 = panel.P(cx - 9.5f, cy - halfH);
        const ImVec2 b1 = panel.P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hovered ? IM_COL32(54, 54, 58, 255)
                                          : IM_COL32(38, 38, 41, 255),
                          2.0f * panel.scale());
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * panel.scale(), 0,
                    panel.scale());
        const ImVec2 center = panel.P(cx, cy);
        const float d = 4.3f * panel.scale();
        const ImU32 ink = hovered ? kText : kDim;
        if (left)
            dl->AddTriangleFilled(ImVec2(center.x + d * 0.5f, center.y - d),
                                  ImVec2(center.x + d * 0.5f, center.y + d),
                                  ImVec2(center.x - d * 0.7f, center.y), ink);
        else
            dl->AddTriangleFilled(ImVec2(center.x - d * 0.5f, center.y - d),
                                  ImVec2(center.x - d * 0.5f, center.y + d),
                                  ImVec2(center.x + d * 0.7f, center.y), ink);
        return ImGui::IsItemClicked();
    }

    bool headerTextButton(ImDrawList* dl, const char* id, float x0, float x1,
                          const char* label)
    {
        constexpr float y0 = 9.5f, y1 = 34.5f;
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hovered ? IM_COL32(54, 54, 58, 255)
                                          : IM_COL32(38, 38, 41, 255),
                          2.0f * panel.scale());
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * panel.scale(), 0,
                    panel.scale());
        panel.text(dl, 0.5f * (x0 + x1), 16.0f, 16.0f,
                   hovered ? kText : kDim, label, 0, true);
        return ImGui::IsItemClicked();
    }

    const char* presetPreview() const
    {
        if (currentPreset >= 0
            && currentPreset < static_cast<int>(multicompp::kFactoryPresets.size()))
            return multicompp::kFactoryPresets[static_cast<size_t>(currentPreset)].name;
        if (!currentUserName.empty()) return currentUserName.c_str();
        return defaultsActive ? "Default" : "Custom";
    }

    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, kHeaderH),
                          IM_COL32(14, 14, 15, 255));
        dl->AddRectFilledMultiColor(panel.P(0, 0), panel.P(kDesignW, 4),
                                    IM_COL32(205, 203, 197, 255),
                                    IM_COL32(155, 154, 151, 255),
                                    IM_COL32(91, 91, 91, 255),
                                    IM_COL32(118, 118, 116, 255));
        dl->AddLine(panel.P(0, kHeaderH), panel.P(kDesignW, kHeaderH),
                    IM_COL32(91, 90, 88, 255), 1.2f * panel.scale());

        constexpr float plateX0 = 28.0f, plateX1 = 358.0f;
        constexpr float plateY0 = 6.0f, plateY1 = 38.0f;
        dl->AddRectFilledMultiColor(panel.P(plateX0, plateY0), panel.P(plateX1, plateY1),
                                    IM_COL32(37, 37, 38, 255),
                                    IM_COL32(29, 29, 30, 255),
                                    IM_COL32(16, 16, 17, 255),
                                    IM_COL32(19, 19, 20, 255));
        dl->AddRect(panel.P(plateX0, plateY0), panel.P(plateX1, plateY1),
                    IM_COL32(185, 184, 180, 220), 3.5f * panel.scale(), 0,
                    1.2f * panel.scale());
        dl->AddLine(panel.P(40, 35), panel.P(348, 35),
                    IM_COL32(116, 145, 75, 210), 1.2f * panel.scale());
        panel.text(dl, 42, 8, 24, kText, "MULTI-COMP", -1, true);
        panel.text(dl, 244, 12, 20, kText, "MC-2", 0, true);
        panel.text(dl, 346, 16, 14, kDim, "v" MULTICOMP2_VERSION_STRING, 1);
        panel.text(dl, kDesignW - 34, 10, 22, kText, "DUSK AUDIO", 1, true);

        ImGui::SetCursorScreenPos(panel.P(plateX0, plateY0));
        if (ImGui::InvisibleButton("##mc_titlecredits",
                                   ImVec2((plateX1 - plateX0) * panel.scale(),
                                          (plateY1 - plateY0) * panel.scale())))
        {
            supporters.resetInteraction();
            showSupporters = true;
            showSettings = false;
        }

        if (headerChevron(dl, "##mc_presetprev", 390.5f, true)) stepPreset(-1);
        if (headerChevron(dl, "##mc_presetnext", 737.5f, false)) stepPreset(1);

        constexpr float bandY0 = 9.5f, bandY1 = 34.5f;
        constexpr float bandH = bandY1 - bandY0;
        ImGui::SetCursorScreenPos(panel.P(404, bandY0));
        ImGui::SetNextItemWidth(320.0f * panel.scale());
        ImFont* presetFont = fontSet.pick(17.0f * panel.scale());
        if (presetFont != nullptr) ImGui::PushFont(presetFont);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(7.0f * panel.scale(),
                                   std::max(0.0f, 0.5f * (bandH * panel.scale()
                                                         - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(76, 103, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(92, 124, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(62, 85, 39, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kHeaderGreenDark);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kHeaderGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginCombo("##mc_factory", presetPreview()))
        {
            for (size_t i = 0; i < multicompp::kFactoryPresets.size(); ++i)
            {
                const bool selected = static_cast<int>(i) == currentPreset;
                if (ImGui::Selectable(multicompp::kFactoryPresets[i].name, selected))
                {
                    applyPreset(static_cast<int>(i));
                    ImGui::CloseCurrentPopup();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            if (!userPresets.empty())
            {
                ImGui::SeparatorText("User");
                for (size_t i = 0; i < userPresets.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const auto& preset = userPresets[i];
                    if (ImGui::Selectable(preset.name.c_str(),
                                          currentPreset < 0
                                              && preset.path == currentUserPath))
                    {
                        loadUserPreset(preset);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(11);
        ImGui::PopStyleVar();
        if (presetFont != nullptr) ImGui::PopFont();

        if (headerTextButton(dl, "##mc_init", 756, 820, "INIT")) initDefaults();
        if (headerTextButton(dl, "##mc_save", 828, 892, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Multi-Comp Preset");
        }
        drawSaveModal();
    }

    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(40, 40, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 50, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(56, 56, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kHeaderGreenDark);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kHeaderGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        if (ImGui::BeginPopupModal("Save Multi-Comp Preset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool appearing = ImGui::IsWindowAppearing();
            if (appearing)
                saveFailed = false;
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(260.0f * panel.scale());
            if (appearing) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText(
                "##mc_savename", saveBuf, sizeof(saveBuf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool save = ImGui::Button("Save") || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (save && saveBuf[0] != '\0')
            {
                if (saveUserPreset(saveBuf))
                {
                    saveFailed = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    saveFailed = true;
                }
            }
            if (saveFailed)
                ImGui::TextColored(ImVec4(0.90f, 0.42f, 0.35f, 1.0f),
                                   "Could not save. Try a different name.");
            if (cancel)
            {
                saveFailed = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(9);
    }

    void stepPreset(int direction)
    {
        int index = currentPreset < 0 ? (direction < 0 ? 0 : -1) : currentPreset;
        index += direction;
        index = std::clamp(index, 0,
                           static_cast<int>(multicompp::kFactoryPresets.size()) - 1);
        applyPreset(index);
    }

    void initDefaults()
    {
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        {
            if (i == static_cast<int>(P_BYPASS)) continue;
            const float hostDefault = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) {
                    return multicompp::hostDefault(d);
                });
            setHostValue(static_cast<uint32_t>(i), hostDefault);
        }
        clearPresetSelection(true);
    }

    std::filesystem::path configDir() const
    {
        return duskdaf::userPresetDirectory("MultiComp2");
    }

    static bool readUserPresetFile(const std::filesystem::path& path, UserPreset& preset)
    {
        std::ifstream input(path);
        if (!input) return false;
        std::string line;
        std::string encoded;
        while (std::getline(input, line))
        {
            if (line.compare(0, 5, "name=") == 0)
                preset.name = line.substr(5);
            else if (line.compare(0, 6, "state=") == 0)
                encoded = line.substr(6);
        }
        if (preset.name.empty() || encoded.empty()
            || !multicompp::decodeState(encoded, preset.values))
            return false;
        preset.path = path.string();
        return true;
    }

    void scanUserPresets()
    {
        duskdaf::scanUserPresets(
            configDir(), ".mcpreset", userPresets,
            [](const std::filesystem::path& path, UserPreset& preset) {
                return readUserPresetFile(path, preset);
            });
    }

    bool saveUserPreset(const char* rawName)
    {
        multicompp::StateValues snapshot{};
        std::copy_n(values.begin(), snapshot.size(), snapshot.begin());
        snapshot[P_BYPASS] = multicompp::hostDefault(
            multicompp::kParams[static_cast<size_t>(P_BYPASS)]);
        const auto saved = duskdaf::writeUserPreset(
            configDir(), ".mcpreset", rawName,
            [&snapshot](std::ostream& output) {
                output << "state=" << multicompp::encodeState(snapshot) << '\n';
            },
            [](const std::filesystem::path& path) {
                UserPreset preset;
                return readUserPresetFile(path, preset) ? preset.name : std::string();
            }, true); // This editor retains its existing Save-as-replace behavior.
        if (!saved) return false;

        scanUserPresets();
        currentPreset = -1;
        currentUserName = saved.name;
        currentUserPath = saved.path;
        defaultsActive = false;
        return true;
    }

    void loadUserPreset(const UserPreset& preset)
    {
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        {
            if (i == static_cast<int>(P_BYPASS)) continue;
            setHostValue(static_cast<uint32_t>(i),
                         preset.values[static_cast<size_t>(i)]);
        }
        currentPreset = -1;
        currentUserName = preset.name;
        currentUserPath = preset.path;
        defaultsActive = false;
    }

    // The mode row under the header, in design coordinates: the eight mode tabs, then AUTO MAKEUP, BYPASS and
    // SETTINGS (the drawer). 16 px captions, the legend floor at the minimum window.
    void drawModeRow(ImDrawList* dl)
    {
        constexpr int modeCount = static_cast<int>(
            sizeof(multicompp::kModes) / sizeof(multicompp::kModes[0]));
        static_assert(modeCount == 8,
                      "the mode row layout must be revisited when modes change");
        using Row = multicompp::ui_detail::ModeRowLayout;
        const int selected = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        for (int mode = 0; mode < modeCount; ++mode)
        {
            const float x0 = Row::tabX0(mode);
            char id[32];
            std::snprintf(id, sizeof(id), "##mc_mode_button_%d", mode);
            const bool active = mode == selected;
            if (rowTab(dl, id, x0, x0 + Row::tabWidth,
                       multicompp::ui_detail::kModeTabLabels[static_cast<size_t>(mode)], active))
            {
                // A mode chosen shows its face: the settings drawer closes (owner 2026-11-05), the lit tab too.
                showSettings = false;
                if (!active) setValue(P_MODE, static_cast<float>(mode));
            }
        }
        // Five factory presets engage Auto Makeup, which bypasses each face's
        // manual makeup / output control; it needs a switch the user can see.
        modeRowToggle(dl, "mc_auto_makeup", P_AUTO_MAKEUP, Row::autoMakeupX0, Row::autoMakeupX1, "AUTO MAKEUP");
        modeRowToggle(dl, "mc_bypass", P_BYPASS, Row::bypassX0, Row::bypassX1, "BYPASS");
        if (rowTab(dl, "##mc_settings", Row::settingsX0, Row::settingsX1, "SETTINGS", showSettings))
            showSettings = !showSettings;
    }

    // A mode-row tab (or a button in its style, y0..y1): lit while `active`, a 16 px caption. True when clicked.
    bool rowTab(ImDrawList* dl, const char* id, float x0, float x1, const char* label, bool active,
                float y0 = kModeRowY0, float y1 = kModeRowY1)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        const ImU32 fill = active ? kHeaderGreenDark : hovered ? kPanelRaised : IM_COL32(24, 25, 29, 255);
        const ImU32 outline = active ? IM_COL32(103, 132, 75, 255) : kLine;
        dl->AddRectFilled(b0, b1, fill, 2.0f * panel.scale());
        dl->AddRect(b0, b1, outline, 2.0f * panel.scale(), 0, panel.scale());
        if (active)
            dl->AddRectFilled(panel.P(x0 + 2.0f, y1 - 2.0f), panel.P(x1 - 2.0f, y1), kHeaderGreen);
        textCentred(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1), 16.0f, active || hovered ? kText : kDim, label, true);
        return clicked;
    }

    // The shared toggle with its label centred in the button. The shared
    // widget sets its text right of centre and high to clear the LED, which
    // these wide buttons do not need.
    void modeRowToggle(ImDrawList* dl, const char* id, uint32_t p, float x0, float x1,
                       const char* label)
    {
        panel.toggle(id, p, x0, kModeRowY0, x1, kModeRowY1, values[p], "");
        const bool on = values[p] > 0.5f;
        const duskdaf::Palette palette{};
        textCentred(dl, 0.5f * (x0 + x1) + 5.0f, 0.5f * (kModeRowY0 + kModeRowY1), 16.0f,
                    on ? palette.white : palette.whiteDim, label, on);
    }

    // ---- Settings drawer ------------------------------------------------------------------------------------------
    // The parameters no face carries (owner direction 2026-11-05), over the face area and modal to it: SETTINGS in
    // the mode row opens and closes it, and so do its CLOSE button, a click on the face area outside it and Escape. Legends at 16 px and
    // values at 17 px, the readability floor at the minimum window. A control the current mode ignores stays where
    // it is, greyed, so the drawer reads the same in every mode.
    struct DrawerTheme
    {
        static constexpr ImU32 scrim = IM_COL32(8, 9, 10, 205);
        static constexpr ImU32 panel = IM_COL32(30, 32, 36, 252);
        static constexpr ImU32 edge = IM_COL32(88, 92, 102, 255);
        static constexpr ImU32 rule = IM_COL32(64, 68, 76, 255);
        static constexpr ImU32 legend = IM_COL32(214, 217, 222, 255);
        static constexpr ImU32 segment = IM_COL32(24, 25, 29, 255);
        static constexpr ImU32 segmentHover = IM_COL32(43, 46, 54, 255);
        static constexpr ImU32 segmentOn = IM_COL32(39, 57, 24, 255);      // the mode row's lit tab
        static constexpr ImU32 segmentOnEdge = IM_COL32(103, 132, 75, 255);
    };
    using DrawerLayout = multicompp::ui_detail::SettingsDrawerLayout;

    static constexpr uint32_t pid(ParamId id) noexcept { return static_cast<uint32_t>(id); }

    // A group heading with its rule, from x0 to x1 at y.
    void drawerHeading(ImDrawList* dl, float x0, float x1, float y, const char* title)
    {
        panel.text(dl, x0, y, DrawerLayout::legendSize, DrawerTheme::legend, title, -1, true);
        dl->AddLine(panel.P(x0, y + 22.0f), panel.P(x1, y + 22.0f), DrawerTheme::rule, panel.scale());
    }

    // The drawer's knobs: the metallic knob with its 16 px legend above and the 17 px value below, in the host's
    // own text for the value (the units and kHz rule the host shows).
    void drawerKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y, const char* legend, bool applicable,
                    const char* inapplicableNote = nullptr)
    {
        char text[48] = {};
        multicompp::resolveParameter(static_cast<int>(p),
            [&](const multicompp::Param& d) {
                if (multicompp::hasSkew(d))
                    multicompp::formatTaperedText(d, values[p], text, sizeof(text));
                else
                {
                    const std::string_view unit(d.unit);
                    char number[32] = {};
                    if (multicompp::formatFixed(number, sizeof(number),
                                                static_cast<double>(multicompp::hostToPlain(d, values[p])),
                                                unit.empty() || unit == "%" ? 0 : 1) < 0)
                        return;
                    if (unit.empty()) std::snprintf(text, sizeof(text), "%s", number);
                    else if (unit == "%") std::snprintf(text, sizeof(text), "%s %%", number);
                    else std::snprintf(text, sizeof(text), "%s %s", number, d.unit);
                }
            },
            [](const multicompp::BandParam&, int) {});
        // A control the mode ignores may say so where its value goes (`inapplicableNote`).
        modernKnob(dl, id, p, x, y, DrawerLayout::knobRadius, legend, y - 42.0f, DrawerLayout::legendSize,
                   DrawerTheme::legend, 0, "", y + 28.0f, applicable, DrawerLayout::valueSize,
                   !applicable && inapplicableNote != nullptr ? inapplicableNote : text);
    }

    // A row of exclusive buttons for a stepped parameter (labels in step order); the lit one is its value. A press
    // on the lit one writes nothing.
    // `perColumn` (vertical only): that many buttons a column, the next column to the right (DRIVE's 2 x 2).
    void drawerSegments(ImDrawList* dl, const char* id, uint32_t p, float x0, float y0, float width,
                        const char* const* labels, int count, bool vertical, bool applicable, int perColumn = 0)
    {
        const int selected = multicompp::ui_detail::choiceIndex(values[p], count);
        constexpr float gap = DrawerLayout::segmentGap;
        const float opacity = applicable ? 1.0f : 0.4f;
        if (!applicable) ImGui::BeginDisabled();
        for (int i = 0; i < count; ++i)
        {
            const float bx0 = vertical ? DrawerLayout::segmentCentreX(x0, width, i, perColumn) - 0.5f * width
                                       : x0 + static_cast<float>(i) * (width + gap);
            const float by0 = vertical ? DrawerLayout::segmentCentreY(y0, i, perColumn) - 0.5f * DrawerLayout::buttonH
                                       : y0;
            const ImVec2 b0 = panel.P(bx0, by0), b1 = panel.P(bx0 + width, by0 + DrawerLayout::buttonH);
            ImGui::PushID(id);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(b0);
            const bool clicked = ImGui::InvisibleButton("##segment", ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            ImGui::PopID();
            const bool lit = i == selected;
            dl->AddRectFilled(b0, b1, colourWithAlpha(lit ? DrawerTheme::segmentOn
                                                          : hovered ? DrawerTheme::segmentHover : DrawerTheme::segment,
                                                      opacity), 2.0f * panel.scale());
            dl->AddRect(b0, b1, colourWithAlpha(lit ? DrawerTheme::segmentOnEdge : DrawerTheme::edge, opacity),
                        2.0f * panel.scale(), 0, panel.scale());
            textCentred(dl, bx0 + 0.5f * width, by0 + 0.5f * DrawerLayout::buttonH, DrawerLayout::legendSize,
                        colourWithAlpha(lit || hovered ? kText : kDim, opacity), labels[i], true);
            if (clicked && !lit) setValue(p, static_cast<float>(i));
        }
        if (!applicable) ImGui::EndDisabled();
    }

    // Draws the drawer and the resize grip in its window (winW x winH: the editor window), and returns the grip's
    // state.
    duskdaf::ResizeGripState drawSettingsDrawer(ImDrawList* dl, float winW, float winH)
    {
        using L = DrawerLayout;
        const float s = panel.scale();
        const int mode = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        const bool digitalModes = mode == 6 || mode == 7;
        const bool fetBlack = mode == 1;

        // Scrim over the face area, then the panel. The scrim is submitted last so the panel's own items keep
        // the pointer; a click that reaches it closes the drawer.
        dl->AddRectFilled(panel.P(0, kFaceTop), panel.P(kDesignW, kDesignH), DrawerTheme::scrim);
        dl->AddRectFilled(panel.P(L::x0, L::y0), panel.P(L::x1, L::y1), DrawerTheme::panel, 4.0f * s);
        dl->AddRect(panel.P(L::x0, L::y0), panel.P(L::x1, L::y1), DrawerTheme::edge, 4.0f * s, 0, s);
        panel.text(dl, L::sidechainX0, L::titleY, 20.0f, kText, "SETTINGS", -1, true);
        if (rowTab(dl, "##mc_settings_close", L::closeX0, L::closeX1, "CLOSE", false, L::titleY,
                   L::titleY + L::buttonH))
            showSettings = false;

        // Row A: SIDECHAIN (the detector's input), PROCESSING.
        drawerHeading(dl, L::sidechainX0, L::sidechainX1, L::rowA, "SIDECHAIN");
        const float row1 = L::rowAButtonsY, row2 = row1 + L::rowPitch, row3 = row2 + L::rowPitch;
        illuminatedToggle(dl, "##set_ext_sc", pid(ParamId::ExternalSidechain), L::keyX0, row1, L::keyX1,
                          row1 + L::buttonH, "EXTERNAL KEY", L::legendSize);
        illuminatedToggle(dl, "##set_sc_listen", pid(ParamId::GlobalSidechainListen), L::keyX0, row2, L::keyX1,
                          row2 + L::buttonH, "SC LISTEN", L::legendSize);
        drawerKnob(dl, "##set_sc_low_freq", pid(ParamId::ScLowFreq), L::lowFreqX, L::rowAKnobY, "LOW FREQ", true);
        drawerKnob(dl, "##set_sc_low_gain", pid(ParamId::ScLowGain), L::lowGainX, L::rowAKnobY, "LOW GAIN", true);
        drawerKnob(dl, "##set_sc_high_freq", pid(ParamId::ScHighFreq), L::highFreqX, L::rowAKnobY, "HIGH FREQ", true);
        drawerKnob(dl, "##set_sc_high_gain", pid(ParamId::ScHighGain), L::highGainX, L::rowAKnobY, "HIGH GAIN", true);

        drawerHeading(dl, L::processingX0, L::processingX1, L::rowA, "PROCESSING");
        // Studio VCA runs at the host rate at every setting and Multiband runs no oversampler.
        panel.text(dl, L::processingToggleX0, row1 + 6.0f, L::legendSize, colourWithAlpha(DrawerTheme::legend,
                   mode == 5 || mode == 7 ? 0.4f : 1.0f), "OVERSAMPLING", -1, true);
        static constexpr const char* kOversamplingShort[3] = {"OFF", "2X", "4X"};
        drawerSegments(dl, "##set_os", pid(ParamId::Oversampling), L::processingSegmentsX, row1,
                       L::processingSegmentW, kOversamplingShort, 3, false, mode != 5 && mode != 7);
        // Multiband reads True Peak only on an external key it actually has (MultiCompDSP: the detector feed).
        const bool truePeakApplies = mode != 7 || externalKeyInUse();
        illuminatedToggle(dl, "##set_tp", pid(ParamId::TruePeakEnable), L::processingToggleX0, row2,
                          L::processingToggleX1, row2 + L::buttonH, "TRUE PEAK", L::legendSize, truePeakApplies);
        static constexpr const char* kTruePeakShort[2] = {"4X", "8X"};
        drawerSegments(dl, "##set_tp_q", pid(ParamId::TruePeakQuality), L::processingSegmentsX, row2,
                       L::processingSegmentW, kTruePeakShort, 2, false,
                       truePeakApplies && values[pid(ParamId::TruePeakEnable)] > 0.5f);
        // Digital and Multiband add no noise (MultiCompDSP: addNoise).
        illuminatedToggle(dl, "##set_noise", pid(ParamId::NoiseEnable), L::processingToggleX0, row3,
                          L::processingToggleX1, row3 + L::buttonH, "ANALOG NOISE", L::legendSize, !digitalModes);

        // Row B: STEREO, DRIVE, LOOKAHEAD, FET BLACK.
        drawerHeading(dl, L::stereoX0, L::stereoX1, L::rowB, "STEREO");
        drawerKnob(dl, "##set_link", pid(ParamId::StereoLink), L::linkX, L::rowBKnobY, "LINK", mode != 7);
        static constexpr const char* kLinkShort[3] = {"STEREO", "MID-SIDE", "DUAL MONO"};
        drawerSegments(dl, "##set_link_mode", pid(ParamId::StereoLinkMode), L::linkModeX, L::rowBButtonsY,
                       L::linkModeW, kLinkShort, 3, true, true);

        drawerHeading(dl, L::driveX0, L::driveX1, L::rowB, "DRIVE");
        static constexpr const char* kDriveShort[4] = {"OFF", "SOFT", "HARD", "CLIP"};
        drawerSegments(dl, "##set_drive", pid(ParamId::Distortion), L::driveGridX, L::rowBButtonsY, L::driveW,
                       kDriveShort, 4, true, true, 2);
        drawerKnob(dl, "##set_drive_amt", pid(ParamId::DistortionAmount), L::amountX, L::rowBKnobY, "AMOUNT",
                   values[pid(ParamId::Distortion)] > 0.5f);

        drawerHeading(dl, L::lookaheadX0, L::lookaheadX1, L::rowB, "LOOKAHEAD");
        drawerKnob(dl, "##set_lookahead", pid(ParamId::GlobalLookahead), L::lookaheadX, L::rowBKnobY, "TIME",
                   digitalModes, "DIGITAL, MULTIBAND");

        drawerHeading(dl, L::fetX0, L::fetX1, L::rowB, "FET BLACK");
        drawerKnob(dl, "##set_fet_threshold", pid(ParamId::FetThreshold), L::thresholdX, L::rowBKnobY, "THRESHOLD",
                   fetBlack);
        drawerKnob(dl, "##set_fet_transient", pid(ParamId::FetTransient), L::transientX, L::rowBKnobY, "TRANSIENT",
                   fetBlack);
        // Curve shapes the All buttons' law only (MultiCompModes: the measured extension at ratio All).
        static constexpr const char* kCurveShort[2] = {"MODERN", "MEASURED"};
        drawerSegments(dl, "##set_fet_curve", pid(ParamId::FetCurve), L::curveX, L::rowBButtonsY, L::curveW,
                       kCurveShort, 2, true,
                       fetBlack && multicompp::ui_detail::choiceIndex(values[pid(ParamId::FetRatio)], 5) == 4);

        // The resize grip, then the panel's background, then the scrim: the first item under the pointer takes it, so
        // the grip keeps its corner, the panel keeps the pointer from the scrim, and the scrim closes the drawer.
        const duskdaf::ResizeGripState grip = panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH, 1.0f);
        ImGui::SetCursorScreenPos(panel.P(L::x0, L::y0));
        ImGui::InvisibleButton("##settings_panel", ImVec2((L::x1 - L::x0) * s, (L::y1 - L::y0) * s));
        ImGui::SetCursorScreenPos(panel.P(0, kFaceTop));
        if (ImGui::InvisibleButton("##settings_scrim", ImVec2(kDesignW * s, (kDesignH - kFaceTop) * s)))
            showSettings = false;
        // Escape closes the drawer unless something else owned it last frame: a typed value being entered, a held
        // control or an open popup (the preset list, the save dialog) take it first.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escapeOwnedLastFrame)
            showSettings = false;
        return grip;
    }

    void knob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
              const char* label, const char* fmt, const char* suffix,
              bool applicable = true)
    {
        if (!applicable) ImGui::BeginDisabled();
        beginTrackedControl(p, id, applicable);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, 25, values[p],
                   hostDefaultValue(p), false, true,
                   fmt, suffix, kPanelRaised, false, true, nullptr, false, 1.0f, 0.0f,
                   nullptr, false, nullptr, false, 0.0f, 0.0f, false, false, 9.5f,
                   false, false, &knobHostToPlain, &knobPlainToHost, this);
        endTrackedControl();
        if (!applicable)
        {
            ImGui::EndDisabled();
            dl->AddCircleFilled(panel.P(x, y), 33.0f * panel.scale(),
                                IM_COL32(21, 22, 25, 145), 48);
        }
        panel.text(dl, x, y + 49, 9.5f, applicable ? kText : kDim, label, 0, true);
    }

    float hostDefaultValue(uint32_t p) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
    }

    float hostMinimum(uint32_t p) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [](const multicompp::Param& d) { return multicompp::hostMin(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostMin(d); });
    }

    float hostMaximum(uint32_t p) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [](const multicompp::Param& d) { return multicompp::hostMax(d); },
            [](const multicompp::BandParam& d, int) { return multicompp::hostMax(d); });
    }

    float plainValueForHost(uint32_t p, float host) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [host](const multicompp::Param& d) { return multicompp::hostToPlain(d, host); },
            [host](const multicompp::BandParam& d, int) { return multicompp::hostToPlain(d, host); });
    }

    float hostValueForPlain(uint32_t p, float plain) const
    {
        return multicompp::resolveParameter(static_cast<int>(p),
            [plain](const multicompp::Param& d) { return multicompp::plainToHost(d, plain); },
            [plain](const multicompp::BandParam& d, int) { return multicompp::plainToHost(d, plain); });
    }

    static float knobHostToPlain(float host, uint32_t p, void* context)
    {
        const float plain = static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
        if (multicompp::usesOptoReferenceDisplay(p))
            return multicompp::optoReferenceDisplayValue(plain);
        return p == P_BUS_HEADROOM || p == P_FET_BLUE_HEADROOM ? 4.0f + 4.0f * plain : plain;
    }

    static float knobPlainToHost(float plain, uint32_t p, void* context)
    {
        if (multicompp::usesOptoReferenceDisplay(p))
            plain = multicompp::optoReferenceHostValue(plain);
        if (p == P_BUS_HEADROOM || p == P_FET_BLUE_HEADROOM) plain = std::round((plain - 4.0f) / 4.0f);
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
    }

    static float vcaCompressionHostToRatio(float host, uint32_t p, void* context)
    {
        const float position = static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
        return duskaudio::vcaLaw::compressRatio(position);
    }

    static float vcaCompressionRatioToHost(float ratio, uint32_t p, void* context)
    {
        const float position = duskaudio::vcaLaw::compressPosition(ratio);
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, position);
    }

    static float fetTimingHostToDial(float host, uint32_t p, void*)
    {
        return multicompp::ui_detail::fetTimingDialValue(host, p);
    }

    static float fetTimingDialToHost(float dial, uint32_t p, void*)
    {
        return multicompp::ui_detail::fetTimingHostValue(dial, p);
    }

    // Typed entry on FET BLUE's Attack: SLO or a dial reading (fetSloAttackTypedHost).
    static bool fetSloAttackParseText(const char* text, uint32_t, void*, float& host)
    {
        return multicompp::ui_detail::fetSloAttackTypedHost(text, host);
    }

    // The face of the current mode, laid out on the canvas in the face area under the mode row (each face's layout
    // is in ui_detail).
    void drawModePanel(ImDrawList* dl)
    {
        const int mode = multicompp::ui_detail::choiceIndex(value(P_MODE), 8);
        if (mode != facedMode)
        {
            facedMode = mode;
            resetModernFaces();
        }
        // Every face carries its own gain-reduction metering: the hardware
        // faces' analogue meters, Digital's VU and Multiband's band meters.
        // Deliberately exhaustive: adding a ninth mode must force a UI decision.
        switch (mode)
        {
            case 0: drawOpto(dl); break;
            case 1: drawFet(dl, false); break;
            case 2: drawVca(dl); break;
            case 3: drawBus(dl); break;
            case 4: drawFet(dl, true); break;
            case 5: drawStudioVca(dl); break;
            case 6: drawDigital(dl); break;
            case 7: drawMultiband(dl); break;
        }
    }

    // OPTO 2A's colours by role (kOptoInk and kOptoRed are its legend ink and red), sampled from the face as it
    // was drawn before the tokens; the legends keep kOptoInk.
    struct OptoTheme
    {
        // The rack panel: frame, ears, brushed plate, edges and mounting slots
        static constexpr ImU32 dropShadow = IM_COL32(0, 0, 0, 145);
        static constexpr ImU32 rackFrame = IM_COL32(21, 23, 24, 255);
        static constexpr ImU32 ear = IM_COL32(143, 147, 147, 255);
        static constexpr ImU32 plateTopLeft = IM_COL32(218, 220, 218, 255);
        static constexpr ImU32 plateTopRight = IM_COL32(211, 213, 211, 255);
        static constexpr ImU32 plateBottomRight = IM_COL32(199, 201, 199, 255);
        static constexpr ImU32 plateBottomLeft = IM_COL32(207, 209, 207, 255);
        static constexpr ImU32 brushLight = IM_COL32(255, 255, 252, 5);
        static constexpr ImU32 brushDark = IM_COL32(74, 78, 78, 4);
        static constexpr ImU32 earSeam = IM_COL32(54, 58, 59, 210);
        static constexpr ImU32 earSeamHighlight = IM_COL32(255, 255, 255, 115);
        static constexpr ImU32 plateTopEdge = IM_COL32(255, 255, 255, 160);
        static constexpr ImU32 plateBottomEdge = IM_COL32(23, 25, 26, 220);
        static constexpr ImU32 rackSlot = IM_COL32(7, 8, 8, 255);
        static constexpr ImU32 rackSlotHighlight = IM_COL32(255, 255, 255, 38);
        // Panel screws (drawOptoScrew; the VCA face uses them too)
        static constexpr ImU32 screwShadow = IM_COL32(0, 0, 0, 105);
        static constexpr ImU32 screwRim = IM_COL32(86, 90, 90, 255);
        static constexpr ImU32 screwHead = IM_COL32(206, 208, 205, 255);
        static constexpr ImU32 screwHeadEdge = IM_COL32(60, 63, 63, 255);
        static constexpr ImU32 screwSlot = IM_COL32(57, 59, 58, 255);
        static constexpr ImU32 screwGlint = IM_COL32(255, 255, 255, 170);
        // The SC HP and MIX set screws
        static constexpr ImU32 setScrewShadow = IM_COL32(0, 0, 0, 90);
        static constexpr ImU32 setScrewNut = IM_COL32(116, 119, 117, 255);
        static constexpr ImU32 setScrewHead = IM_COL32(210, 212, 208, 255);
        static constexpr ImU32 setScrewEdge = IM_COL32(57, 59, 58, 255);
        static constexpr ImU32 setScrewSlot = IM_COL32(47, 49, 48, 255);
        // The GAIN and PEAK REDUCTION knobs
        static constexpr ImU32 knobShadow = IM_COL32(0, 0, 0, 105);
        static constexpr ImU32 knobSkirt = IM_COL32(22, 24, 23, 255);
        static constexpr ImU32 knobCap = IM_COL32(37, 39, 37, 255);
        static constexpr ImU32 knobCapHighlight = IM_COL32(112, 117, 110, 180);
        static constexpr ImU32 knobCapShade = IM_COL32(5, 7, 6, 220);
        static constexpr ImU32 knobBar = IM_COL32(14, 16, 15, 255);
        static constexpr ImU32 knobBarEdge = IM_COL32(98, 103, 96, 180);
        static constexpr ImU32 knobPointer = IM_COL32(242, 243, 233, 255);
        // The LIMIT / COMPRESS toggle
        static constexpr ImU32 toggleNutShadow = IM_COL32(0, 0, 0, 115);
        static constexpr ImU32 toggleNut = IM_COL32(121, 124, 121, 255);
        static constexpr ImU32 toggleBushing = IM_COL32(49, 51, 50, 255);
        static constexpr ImU32 toggleBushingRim = IM_COL32(225, 226, 221, 190);
        static constexpr ImU32 toggleLeverShadow = IM_COL32(0, 0, 0, 130);
        static constexpr ImU32 toggleLever = IM_COL32(163, 165, 160, 255);
        static constexpr ImU32 toggleLeverHighlight = IM_COL32(245, 245, 238, 215);
        static constexpr ImU32 toggleTip = IM_COL32(180, 182, 176, 255);
        static constexpr ImU32 toggleTipEdge = IM_COL32(48, 50, 49, 255);
    };

    void drawOpto(ImDrawList* dl)
    {
        using Layout = multicompp::ui_detail::OptoFaceplateLayout;
        const float left = Layout::left;
        const float right = Layout::right;
        const float top = Layout::top;
        const float bottom = Layout::bottom;

        // The face follows the same control contract as the flat Opto panel,
        // but uses physical rack construction and hardware depth. Keep all
        // branding original to Dusk rather than reproducing a reference unit.
        dl->AddRectFilled(panel.P(left + 5, top + 8), panel.P(right + 5, bottom + 8),
                          OptoTheme::dropShadow, 4.0f * panel.scale());
        dl->AddRectFilled(panel.P(left, top), panel.P(right, bottom),
                          OptoTheme::rackFrame, 3.0f * panel.scale());

        constexpr float earWidth = Layout::ear;
        const float faceLeft = left + earWidth;
        const float faceRight = right - earWidth;
        dl->AddRectFilled(panel.P(left + 3, top + 3), panel.P(faceLeft, bottom - 3),
                          OptoTheme::ear, 2.0f * panel.scale());
        dl->AddRectFilled(panel.P(faceRight, top + 3), panel.P(right - 3, bottom - 3),
                          OptoTheme::ear, 2.0f * panel.scale());
        dl->AddRectFilledMultiColor(panel.P(faceLeft, top + 3),
                                    panel.P(faceRight, bottom - 3),
                                    OptoTheme::plateTopLeft,
                                    OptoTheme::plateTopRight,
                                    OptoTheme::plateBottomRight,
                                    OptoTheme::plateBottomLeft);

        // Deterministic one-pixel strokes keep the panel feeling like brushed
        // aluminium without requiring a resolution-specific bitmap texture.
        for (int row = static_cast<int>(top + 5.0f);
             row < static_cast<int>(bottom - 4.0f); row += 3)
        {
            const int variation = (row * 37) % 5;
            const ImU32 shade = variation < 2
                ? OptoTheme::brushLight
                : OptoTheme::brushDark;
            dl->AddLine(panel.P(left + 5.0f, static_cast<float>(row)),
                        panel.P(right - 5.0f, static_cast<float>(row)),
                        shade, panel.scale());
        }
        dl->AddLine(panel.P(faceLeft, top + 4), panel.P(faceLeft, bottom - 4),
                    OptoTheme::earSeam, 1.5f * panel.scale());
        dl->AddLine(panel.P(faceRight, top + 4), panel.P(faceRight, bottom - 4),
                    OptoTheme::earSeam, 1.5f * panel.scale());
        dl->AddLine(panel.P(faceLeft + 1, top + 5), panel.P(faceLeft + 1, bottom - 5),
                    OptoTheme::earSeamHighlight, panel.scale());
        dl->AddLine(panel.P(left + 4, top + 4), panel.P(right - 4, top + 4),
                    OptoTheme::plateTopEdge, 1.2f * panel.scale());
        dl->AddLine(panel.P(left + 4, bottom - 4), panel.P(right - 4, bottom - 4),
                    OptoTheme::plateBottomEdge, 2.0f * panel.scale());

        // Rack-ear slots are intentionally clipped by the panel edge, like a
        // photographed 19-inch unit sitting in a dark rack.
        for (const float slotY : {top + 72.0f, bottom - 72.0f})
        {
            dl->AddRectFilled(panel.P(left - 7.0f, slotY - 9.0f),
                              panel.P(left + 23.0f, slotY + 9.0f),
                              OptoTheme::rackSlot, 9.0f * panel.scale());
            dl->AddRectFilled(panel.P(right - 23.0f, slotY - 9.0f),
                              panel.P(right + 7.0f, slotY + 9.0f),
                              OptoTheme::rackSlot, 9.0f * panel.scale());
            dl->AddLine(panel.P(left - 2.0f, slotY - 6.0f),
                        panel.P(left + 19.0f, slotY - 6.0f),
                        OptoTheme::rackSlotHighlight, panel.scale());
            dl->AddLine(panel.P(right - 19.0f, slotY - 6.0f),
                        panel.P(right + 2.0f, slotY - 6.0f),
                        OptoTheme::rackSlotHighlight, panel.scale());
        }

        const float centre = 0.5f * (left + right);
        for (const ImVec2 screw : {ImVec2(faceLeft + 28, top + 16),
                                   ImVec2(faceRight - 28, top + 16),
                                   ImVec2(faceLeft + 28, bottom - 16),
                                   ImVec2(centre, bottom - 16),
                                   ImVec2(faceRight - 28, bottom - 16)})
            drawOptoScrew(dl, screw.x, screw.y, 8.0f);

        // The nameplate (24) with its rule, the series under it (13, silk), the model badge at the top (20).
        constexpr const char* nameplate = "LEVELING AMPLIFIER";
        panel.text(dl, Layout::brandX, Layout::brandY, 24.0f, kOptoRed, nameplate, -1, true);
        const float nameplateSize = 24.0f * panel.scale();
        const float nameplateWidth = panel.pickFont(nameplateSize)->CalcTextSizeA(
            nameplateSize, FLT_MAX, 0.0f, nameplate).x / panel.scale();
        dl->AddLine(panel.P(Layout::brandX, Layout::brandY + 30),
                    panel.P(Layout::brandX + nameplateWidth, Layout::brandY + 30), kOptoRed, 2.0f * panel.scale());
        spacedText(dl, Layout::brandX, Layout::brandY + 38, 13.0f, kOptoInk, "MC-2 OPTICAL", -1, 0.12f);
        spacedText(dl, centre, top + 6, 20.0f, kOptoInk, "OPTO 2A", 0, 0.10f);

        optoModeSwitch(dl, Layout::switchX, Layout::switchY);
        optoKnob(dl, "opto_gain", P_OPTO_GAIN, Layout::gainX, Layout::dialY, Layout::dialRadius, "GAIN");
        multicompp::ui_detail::VintageMeterScale meterScale;
        meterScale.detail = Layout::meterW / 238.0f;
        meterScale.tickLabel = 14.0f;
        meterScale.sublabel = 13.0f;
        meterScale.percent = 13.0f;
        meterScale.vu = 16.0f;
        drawOptoMeter(dl, Layout::meterX, Layout::meterY, Layout::meterW, Layout::meterH,
                      multicompp::ui_detail::optoMeterDisplayValue(needleGainReduction()), meterScale);
        optoKnob(dl, "opto_peak", P_OPTO_PEAK, Layout::peakX, Layout::dialY, Layout::dialRadius, "PEAK REDUCTION");
        optoKnob(dl, "opto_sc_hp", P_SC_HP, Layout::trimX, Layout::scHpY, Layout::trimRadius,
                 "SC HP", false, "%.0f", " Hz");
        optoKnob(dl, "opto_mix", P_MIX, Layout::trimX, Layout::mixY, Layout::trimRadius, "MIX", false);
        drawOptoMeterSwitch(dl, Layout::meterX + 0.5f * Layout::meterW, Layout::meterSwitchY);
    }

    void drawOptoScrew(ImDrawList* dl, float x, float y, float radius)
    {
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        dl->AddCircleFilled(ImVec2(center.x + 2.2f * scale, center.y + 3.2f * scale),
                            radius * scale, OptoTheme::screwShadow, 28);
        dl->AddCircleFilled(center, radius * scale,
                            OptoTheme::screwRim, 28);
        dl->AddCircleFilled(ImVec2(center.x - 0.8f * scale, center.y - 1.0f * scale),
                            (radius - 1.2f) * scale,
                            OptoTheme::screwHead, 28);
        dl->AddCircle(center, (radius - 1.2f) * scale,
                      OptoTheme::screwHeadEdge, 28, 0.9f * scale);
        const float slot = radius * 0.55f * scale;
        const ImU32 slotDark = OptoTheme::screwSlot;
        dl->AddLine(ImVec2(center.x - slot, center.y - slot),
                    ImVec2(center.x + slot, center.y + slot),
                    slotDark, 1.3f * scale);
        dl->AddLine(ImVec2(center.x + slot, center.y - slot),
                    ImVec2(center.x - slot, center.y + slot),
                    slotDark, 1.3f * scale);
        dl->AddCircleFilled(ImVec2(center.x - radius * 0.30f * scale,
                                   center.y - radius * 0.34f * scale),
                            1.15f * scale, OptoTheme::screwGlint, 12);
    }

    void optoKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                  float radius, const char* label, bool numberedScale = true,
                  const char* format = "%.0f",
                  const char* suffix = multicompp::ui_detail::optoKnobValueSuffix())
    {
        // Put the shared gesture/editor layer above the custom body while
        // letting it mutate the value first, so the pointer and readout update
        // in the same frame. The active-only bubble exposes shift-fine values.
        optoKnobSplitter.Split(dl, 2);
        optoKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this,
                   multicompp::ui_detail::optoKnobUsesPlainDomainDrag(p));
        endTrackedControl();
        optoKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = std::clamp((plain - minimum)
            / std::max(maximum - minimum, 1.0e-6f), 0.0f, 1.0f);
        const ImVec2 center = panel.P(x, y);
        const float scaledRadius = radius * panel.scale();

        if (!numberedScale)
        {
            // The original hardware did not have Mix. Treat our required Mix
            // control as a deliberately small set-screw so it does not compete
            // with the two primary controls.
            std::array<ImVec2, 6> nut{};
            for (int point = 0; point < 6; ++point)
            {
                const float angle = kUiPi / 6.0f + static_cast<float>(point)
                    * kUiPi / 3.0f;
                nut[static_cast<size_t>(point)] = ImVec2(
                    center.x + std::cos(angle) * scaledRadius * 1.22f,
                    center.y + std::sin(angle) * scaledRadius * 1.22f);
            }
            std::array<ImVec2, 6> shadow = nut;
            for (auto& point : shadow)
            {
                point.x += 2.0f * panel.scale();
                point.y += 3.0f * panel.scale();
            }
            dl->AddConvexPolyFilled(shadow.data(), 6, OptoTheme::setScrewShadow);
            dl->AddConvexPolyFilled(nut.data(), 6, OptoTheme::setScrewNut);
            dl->AddCircleFilled(center, scaledRadius,
                                OptoTheme::setScrewHead, 32);
            dl->AddCircle(center, scaledRadius, OptoTheme::setScrewEdge,
                          32, 1.2f * panel.scale());
            const float slotAngle = duskdaf::DuskPanel::knobAngle(t);
            const ImVec2 slot(std::sin(slotAngle), -std::cos(slotAngle));
            dl->AddLine(ImVec2(center.x - slot.x * scaledRadius * 0.63f,
                               center.y - slot.y * scaledRadius * 0.63f),
                        ImVec2(center.x + slot.x * scaledRadius * 0.63f,
                               center.y + slot.y * scaledRadius * 0.63f),
                        OptoTheme::setScrewSlot, 2.4f * panel.scale());
            // Legend (16) above, the travel marks (16) either side, and SC HP's cutoff (17) under it.
            panel.text(dl, x, y - radius * 1.22f - 26.0f, 16.0f, kOptoInk, label, 0, true);
            panel.text(dl, x - radius - 27.0f, y - 9.0f, 16.0f, kOptoInk, "-", 0, true);
            panel.text(dl, x + radius + 27.0f, y - 9.0f, 16.0f, kOptoInk, "+", 0, true);
            if (p == P_SC_HP)
            {
                char cutoff[20];
                if (plain < 1.0f)
                    std::snprintf(cutoff, sizeof(cutoff), "OFF");
                else
                    std::snprintf(cutoff, sizeof(cutoff), "%.0f Hz",
                                  static_cast<double>(plain));
                panel.text(dl, x, y + radius * 1.22f + 8.0f, 17.0f, kOptoInk, cutoff, 0, true);
            }
            optoKnobSplitter.Merge(dl);
            return;
        }

        // The dial's numerals at the legend size (16): the end stops and every other ten, every mark kept (the
        // readability floor's rule for a dial that cannot carry every numeral at 16).
        constexpr std::array<const char*, 11> scaleLabels{{
            "0", nullptr, "20", nullptr, "40", nullptr,
            "60", nullptr, "80", nullptr, "100"}};
        // Three tiers of mark, so the styles differ only by how far in they
        // start; the placement comes from the shared ring either way.
        duskdaf::KnobRingStyle ring;
        ring.markOuter = 15.0f;   ring.minorOuter = 15.0f;
        ring.markInner = 5.0f;    ring.markThickness = 1.8f;
        ring.minorThickness = 1.0f;
        ring.markColor = kOptoInk;
        ring.labelOffset = multicompp::ui_detail::OptoFaceplateLayout::ringLabelOffset;
        ring.labelNudgeY = -9.0f; ring.labelSize = 16.0f;
        ring.labelColor = kOptoInk;
        ring.labelCentred = true;
        duskdaf::KnobRingStyle medium = ring;   medium.minorInner = 8.0f;
        duskdaf::KnobRingStyle fine = ring;     fine.minorInner = 10.0f;

        for (int tick = 0; tick <= 50; ++tick)
        {
            const bool isMajor = tick % 5 == 0;
            const bool isMedium = tick % 5 == 2;
            duskdaf::drawKnobRingMark(
                panel, dl, x, y, radius, static_cast<float>(tick) / 50.0f,
                isMajor ? scaleLabels[static_cast<size_t>(tick / 5)] : nullptr,
                isMajor ? ring : (isMedium ? medium : fine), !isMajor);
        }

        const ImVec2 shadowCenter(center.x + 3.0f * panel.scale(),
                                  center.y + 5.0f * panel.scale());
        dl->AddCircleFilled(shadowCenter, scaledRadius * 1.01f,
                            OptoTheme::knobShadow, 56);
        // The scallops turn with the pointer, so the whole body reads as one
        // turning part; the shadow and the highlight arcs below stay put
        // because the light does not move.
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        std::array<ImVec2, 96> rim{};
        for (int i = 0; i < 96; ++i)
        {
            const float local = 2.0f * kUiPi * i / 96.0f;
            const float a = local + pointerAngle;
            const float rr = scaledRadius * (0.96f + 0.035f * std::cos(local * 16.0f));
            rim[static_cast<size_t>(i)] = ImVec2(center.x + std::sin(a) * rr,
                                                 center.y - std::cos(a) * rr);
        }
        // The scalloped outline is star-convex, not convex. A shared-vertex fan
        // fills its recesses without the internal anti-aliasing seams of wedges.
        dl->PrimReserve(96 * 3, 97);
        const unsigned int rimBase = dl->_VtxCurrentIdx;
        const ImVec2 rimUv = ImGui::GetIO().Fonts->TexUvWhitePixel;
        dl->PrimWriteVtx(center, rimUv, OptoTheme::knobSkirt);
        for (const auto& point : rim)
            dl->PrimWriteVtx(point, rimUv, OptoTheme::knobSkirt);
        for (int i = 0; i < 96; ++i)
        {
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(rimBase));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(rimBase + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(rimBase + 1 + (i + 1) % 96));
        }
        dl->AddCircleFilled(center, scaledRadius * 0.86f, OptoTheme::knobCap, 64);
        dl->PathArcTo(center, scaledRadius * 0.86f, -2.8f, -0.35f, 32);
        dl->PathStroke(OptoTheme::knobCapHighlight, 0, 1.2f * panel.scale());
        dl->PathArcTo(center, scaledRadius * 0.88f, 0.2f, 2.8f, 32);
        dl->PathStroke(OptoTheme::knobCapShade, 0, 2.0f * panel.scale());
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        const ImVec2 across(pointer.y, -pointer.x);
        const auto ridge = [&](float along, float side) {
            return ImVec2(center.x + scaledRadius * (pointer.x * along + across.x * side),
                          center.y + scaledRadius * (pointer.y * along + across.y * side));
        };
        dl->AddQuadFilled(ridge(-0.79f, -0.17f), ridge(0.91f, -0.10f),
                          ridge(0.91f, 0.10f), ridge(-0.79f, 0.17f), OptoTheme::knobBar);
        dl->AddLine(ridge(-0.68f, -0.12f), ridge(0.87f, -0.08f),
                    OptoTheme::knobBarEdge, 1.5f * panel.scale());
        dl->AddLine(ridge(0.48f, 0.0f), ridge(0.93f, 0.0f),
                    OptoTheme::knobPointer, 1.8f * panel.scale());
        panel.text(dl, x, y + radius + multicompp::ui_detail::OptoFaceplateLayout::legendDrop, 20.0f,
                   kOptoInk, label, 0, true);
        optoKnobSplitter.Merge(dl);
    }

    void optoModeSwitch(ImDrawList* dl, float x, float y)
    {
        const ImVec2 p0 = panel.P(x - 52.0f, y - 70.0f);
        const ImVec2 p1 = panel.P(x + 52.0f, y + 70.0f);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##opto_comp_limit", ImVec2(p1.x - p0.x, p1.y - p0.y));
        if (ImGui::IsItemClicked())
            setValue(P_OPTO_LIMIT, values[P_OPTO_LIMIT] > 0.5f ? 0.0f : 1.0f);

        const bool limit = multicompp::ui_detail::choiceIndex(
            values[P_OPTO_LIMIT], 2) == 1;

        panel.text(dl, x, y - 66, 16.0f, kOptoInk, multicompp::ui_detail::optoModeLabel(1), 0, limit);
        panel.text(dl, x, y + 48, 16.0f, kOptoInk, multicompp::ui_detail::optoModeLabel(0), 0, !limit);

        const ImVec2 base = panel.P(x, y);
        std::array<ImVec2, 6> nut{};
        for (int point = 0; point < 6; ++point)
        {
            const float angle = kUiPi / 6.0f + static_cast<float>(point)
                * kUiPi / 3.0f;
            nut[static_cast<size_t>(point)] = ImVec2(
                base.x + std::cos(angle) * 17.0f * panel.scale(),
                base.y + std::sin(angle) * 17.0f * panel.scale());
        }
        std::array<ImVec2, 6> nutShadow = nut;
        for (auto& point : nutShadow)
        {
            point.x += 2.0f * panel.scale();
            point.y += 4.0f * panel.scale();
        }
        dl->AddConvexPolyFilled(nutShadow.data(), 6, OptoTheme::toggleNutShadow);
        dl->AddConvexPolyFilled(nut.data(), 6, OptoTheme::toggleNut);
        dl->AddCircleFilled(base, 11.0f * panel.scale(),
                            OptoTheme::toggleBushing, 32);
        dl->AddCircle(base, 11.0f * panel.scale(),
                      OptoTheme::toggleBushingRim, 32, panel.scale());
        const float leverY = y + (limit ? -22.0f : 22.0f);
        const float leverX = x + (limit ? -4.0f : 5.0f);
        dl->AddLine(panel.P(x + 2.5f, y + 4.0f),
                    panel.P(leverX + 2.5f, leverY + 5.0f),
                    OptoTheme::toggleLeverShadow, 8.0f * panel.scale());
        dl->AddLine(base, panel.P(leverX, leverY),
                    OptoTheme::toggleLever, 6.0f * panel.scale());
        dl->AddLine(panel.P(x - 2.0f, y - 2.0f),
                    panel.P(leverX - 2.0f, leverY - 2.0f),
                    OptoTheme::toggleLeverHighlight, 2.5f * panel.scale());
        dl->AddCircleFilled(panel.P(leverX, leverY), 6.0f * panel.scale(),
                            OptoTheme::toggleTip, 28);
        dl->AddCircle(panel.P(leverX, leverY), 6.0f * panel.scale(),
                      OptoTheme::toggleTipEdge, 28, 1.0f * panel.scale());
    }

    void drawOptoMeter(ImDrawList* dl, float x, float y, float w, float h, float gr,
                       const multicompp::ui_detail::VintageMeterScale& scale = {})
    {
        const bool opto = multicompp::ui_detail::choiceIndex(values[P_MODE], 8) == 0;
        const int choice = multicompp::ui_detail::vintageMeterChoice(values[opto ? P_OPTO_METER : P_FET_METER]);
        const bool powered = opto || values[P_BYPASS] < 0.5f;
        void* const instance = getPluginInstancePointer();
        const float output = instance != nullptr && multiCompGetOutputVuLevel != nullptr
            ? multiCompGetOutputVuLevel(instance) : -120.0f;
        const char* label = !powered ? "OFF" : choice == 0 ? "GAIN REDUCTION"
                          : choice == 2 ? "OUTPUT +4" : opto ? "OUTPUT +10" : "OUTPUT +8";
        multicompp::ui_detail::drawVintageMeter(panel, dl, x, y, w, h,
            multicompp::ui_detail::vintageMeterDb(opto, choice, gr, output), opto, label, powered, scale);
    }

    void drawOptoMeterSwitch(ImDrawList* dl, float x, float y)
    {
        const int selected = multicompp::ui_detail::vintageMeterChoice(values[P_OPTO_METER]);
        constexpr std::array<const char*, 3> labels{{"GR", "+10", "+4"}};
        panel.text(dl, x, y - 24, 16.0f, kOptoInk, "METER", 0, true);
        for (int i = 0; i < 3; ++i)
        {
            const float cx = x + (i - 1) * 64.0f;
            ImGui::SetCursorScreenPos(panel.P(cx - 26, y - 4));
            ImGui::PushID(i);
            ImGui::InvisibleButton("##opto_meter", ImVec2(52 * panel.scale(), 30 * panel.scale()));
            // The lit button again writes nothing (no undo step, no automation point).
            if (ImGui::IsItemClicked() && selected != i) setValue(P_OPTO_METER, static_cast<float>(i));
            ImGui::PopID();
            panel.text(dl, cx, y, 16.0f, kOptoInk, labels[static_cast<size_t>(i)], 0, selected == i);
            if (selected == i)
                dl->AddLine(panel.P(cx - 16, y + 22), panel.P(cx + 16, y + 22), kOptoRed, 2.0f * panel.scale());
        }
    }

    // FET BLACK's and FET BLUE's face colours by role, sampled from the face as it was drawn before the tokens (the shading of the knob cap's facets is computed).
    struct FetTheme
    {
        // The rack panel: plate, edge, grain, ears, slots and screws
        static constexpr ImU32 dropShadow = IM_COL32(0, 0, 0, 150);
        static constexpr ImU32 plateTopLeft = IM_COL32(27, 28, 27, 255);
        static constexpr ImU32 plateTopRight = IM_COL32(17, 18, 18, 255);
        static constexpr ImU32 plateBottomRight = IM_COL32(7, 8, 8, 255);
        static constexpr ImU32 plateBottomLeft = IM_COL32(13, 14, 14, 255);
        static constexpr ImU32 plateEdge = IM_COL32(114, 116, 111, 255);
        static constexpr ImU32 grainLight = IM_COL32(255, 255, 255, 3);
        static constexpr ImU32 grainDark = IM_COL32(0, 0, 0, 5);
        static constexpr ImU32 ear = IM_COL32(35, 36, 35, 255);
        static constexpr ImU32 earSeam = IM_COL32(104, 106, 101, 180);
        static constexpr ImU32 rackSlot = IM_COL32(2, 3, 3, 255);
        static constexpr ImU32 screwHead = IM_COL32(130, 132, 127, 255);
        static constexpr ImU32 screwSlot = IM_COL32(38, 39, 38, 255);
        // FET BLUE's blue meter section (its reference unit's colour; our own lettering)
        static constexpr ImU32 blueSectionTop = IM_COL32(30, 112, 182, 255);
        static constexpr ImU32 blueSectionBottom = IM_COL32(18, 84, 146, 255);
        static constexpr ImU32 blueSectionEdge = IM_COL32(10, 46, 82, 255);
        static constexpr ImU32 blueSectionHighlight = IM_COL32(150, 196, 236, 110);
        static constexpr ImU32 blueInk = IM_COL32(240, 245, 250, 255);
        // The badge and nameplate
        static constexpr ImU32 badgeInk = IM_COL32(234, 234, 221, 255);
        static constexpr ImU32 badgeSeries = IM_COL32(177, 179, 170, 255);
        static constexpr ImU32 badgeNameplate = IM_COL32(202, 204, 194, 255);
        // The RATIO and METER columns
        static constexpr ImU32 columnLegend = IM_COL32(230, 231, 219, 255);
        static constexpr ImU32 buttonShadow = IM_COL32(0, 0, 0, 130);
        static constexpr ImU32 buttonIdle = IM_COL32(31, 31, 29, 255);
        static constexpr ImU32 buttonHover = IM_COL32(52, 52, 49, 255);
        static constexpr ImU32 buttonLit = IM_COL32(24, 24, 23, 255);
        static constexpr ImU32 buttonEdge = IM_COL32(91, 92, 87, 255);
        static constexpr ImU32 lamp = IM_COL32(226, 151, 61, 255);
        static constexpr ImU32 lampRed = IM_COL32(160, 48, 34, 255);
        static constexpr ImU32 buttonLabel = IM_COL32(228, 228, 216, 255);
        static constexpr ImU32 buttonLabelLit = IM_COL32(246, 224, 177, 255);
        static constexpr ImU32 meterButtonShadow = IM_COL32(0, 0, 0, 145);
        static constexpr ImU32 meterButton = IM_COL32(29, 29, 28, 255);
        static constexpr ImU32 meterButtonEdge = IM_COL32(87, 88, 84, 255);
        static constexpr ImU32 meterButtonLabel = IM_COL32(227, 227, 215, 255);
        // The knobs: scale, skirt, collar, cap and pointer
        static constexpr ImU32 ringMark = IM_COL32(229, 230, 219, 240);
        static constexpr ImU32 ringLabel = IM_COL32(232, 232, 220, 255);
        static constexpr ImU32 ringDot = IM_COL32(230, 230, 218, 255);
        static constexpr ImU32 knobShadow = IM_COL32(0, 0, 0, 150);
        static constexpr ImU32 skirtEdge = IM_COL32(66, 67, 66, 255);
        static constexpr ImU32 skirt = IM_COL32(32, 34, 32, 255);
        static constexpr ImU32 skirtInner = IM_COL32(24, 26, 24, 255);
        static constexpr ImU32 skirtCore = IM_COL32(23, 25, 23, 255);
        static constexpr ImU32 skirtHighlight = IM_COL32(158, 162, 153, 80);
        static constexpr ImU32 skirtShade = IM_COL32(24, 25, 24, 110);
        static constexpr ImU32 skirtRim = IM_COL32(31, 32, 31, 255);
        static constexpr ImU32 collar = IM_COL32(20, 20, 20, 255);
        static constexpr ImU32 knurl = IM_COL32(75, 79, 71, 100);
        static constexpr ImU32 capBase = IM_COL32(111, 112, 110, 255);
        static constexpr ImU32 capCentre = IM_COL32(181, 182, 179, 255);
        static constexpr ImU32 capHighlight = IM_COL32(255, 255, 251, 95);
        static constexpr ImU32 capRim = IM_COL32(44, 45, 44, 255);
        static constexpr ImU32 pointer = IM_COL32(229, 230, 218, 255);
        static constexpr ImU32 knobLegend = IM_COL32(235, 235, 222, 255);
    };

    // The trim set screws (trimScrewKnob: FET's Mix and FET BLUE's Headroom, VCA's Mix).
    struct TrimScrewTheme
    {
        // Nut, head, face, slot and the legend with its travel marks
        static constexpr ImU32 shadow = IM_COL32(0, 0, 0, 120);
        static constexpr ImU32 nut = IM_COL32(94, 96, 93, 255);
        static constexpr ImU32 head = IM_COL32(116, 118, 115, 255);
        static constexpr ImU32 face = IM_COL32(190, 192, 188, 255);
        static constexpr ImU32 highlight = IM_COL32(247, 248, 242, 80);
        static constexpr ImU32 rim = IM_COL32(43, 44, 43, 255);
        static constexpr ImU32 slot = IM_COL32(42, 43, 42, 255);
        static constexpr ImU32 legend = IM_COL32(225, 226, 215, 255);
        static constexpr ImU32 minus = IM_COL32(157, 160, 152, 255);
        static constexpr ImU32 plus = IM_COL32(210, 212, 201, 255);
    };

    // FET BLACK's face, and FET BLUE's (`blue`): the same rack panel, INPUT, OUTPUT, ATTACK and RELEASE. FET BLUE
    // has its own ratio buttons with the side-chain filter's, HR and MIX between INPUT and OUTPUT and a blue meter
    // section, as its reference unit does.
    void drawFet(ImDrawList* dl, bool blue)
    {
        using Layout = multicompp::ui_detail::FetFaceplateLayout;
        constexpr float left = Layout::left, right = Layout::right;
        constexpr float top = Layout::top, bottom = Layout::bottom, ear = Layout::ear;
        const float s = panel.scale();
        dl->AddRectFilled(panel.P(left + 5, top + 7), panel.P(right + 5, bottom + 7), FetTheme::dropShadow, 4.0f * s);
        dl->AddRectFilledMultiColor(panel.P(left, top), panel.P(right, bottom), FetTheme::plateTopLeft,
                                    FetTheme::plateTopRight, FetTheme::plateBottomRight, FetTheme::plateBottomLeft);
        dl->AddRect(panel.P(left, top), panel.P(right, bottom), FetTheme::plateEdge, 4.0f * s, 0, 1.3f * s);
        for (int row = static_cast<int>(top + 4); row < static_cast<int>(bottom); row += 4)
            dl->AddLine(panel.P(left + 3, static_cast<float>(row)), panel.P(right - 3, static_cast<float>(row)),
                        row % 8 == 0 ? FetTheme::grainLight : FetTheme::grainDark, s);
        // Rack ears and mounting slots, the same construction as the Opto face.
        dl->AddRectFilled(panel.P(left, top), panel.P(left + ear, bottom), FetTheme::ear);
        dl->AddRectFilled(panel.P(right - ear, top), panel.P(right, bottom), FetTheme::ear);
        dl->AddLine(panel.P(left + ear, top), panel.P(left + ear, bottom), FetTheme::earSeam, s);
        dl->AddLine(panel.P(right - ear, top), panel.P(right - ear, bottom), FetTheme::earSeam, s);
        for (const float slotY : {top + 62.0f, bottom - 62.0f})
        {
            dl->AddRectFilled(panel.P(-9.0f, slotY - 9.0f), panel.P(23.0f, slotY + 9.0f), FetTheme::rackSlot, 8.0f * s);
            dl->AddRectFilled(panel.P(right - 23.0f, slotY - 9.0f), panel.P(right + 9.0f, slotY + 9.0f),
                              FetTheme::rackSlot, 8.0f * s);
        }
        for (const ImVec2 screw : {ImVec2(left + 73, top + 18), ImVec2(right - 73, top + 18),
                                   ImVec2(left + 73, bottom - 18), ImVec2(right - 73, bottom - 18)})
        {
            dl->AddCircleFilled(panel.P(screw.x, screw.y), 6.5f * s, FetTheme::screwHead, 24);
            dl->AddLine(panel.P(screw.x - 3.5f, screw.y), panel.P(screw.x + 3.5f, screw.y), FetTheme::screwSlot, s);
        }

        // The reference unit's reading order: the two gain controls, the timing pair, the ratio buttons, the VU
        // and the meter buttons.
        const float inputX = blue ? Layout::blueInputX : Layout::blackInputX;
        const float outputX = blue ? Layout::blueOutputX : Layout::blackOutputX;
        const float timingX = Layout::timingX;
        fetKnob(dl, "fet_in", P_FET_IN, inputX, Layout::dialY, Layout::dialRadius, "INPUT", true, "%.1f", " dB");
        fetKnob(dl, "fet_out", P_FET_OUT, outputX, Layout::dialY, Layout::dialRadius, "OUTPUT", true, "%.1f", " dB");
        fetKnob(dl, "fet_att", P_FET_ATTACK, timingX, Layout::attackY, Layout::timingRadius, "ATTACK", false, "%.2f",
                "", blue);
        fetKnob(dl, "fet_rel", P_FET_RELEASE, timingX, Layout::releaseY, Layout::timingRadius, "RELEASE", false,
                "%.2f", "");

        multicompp::ui_detail::VintageMeterScale meterScale;
        meterScale.detail = Layout::meterW / 238.0f;
        meterScale.tickLabel = 14.0f;
        meterScale.sublabel = 13.0f;
        meterScale.percent = 13.0f;
        meterScale.vu = 16.0f;
        const float meterCentre = Layout::meterX + 0.5f * Layout::meterW;
        const float reading = multicompp::ui_detail::optoMeterDisplayValue(needleGainReduction());
        if (blue)
        {
            trimScrewKnob(dl, "fet_blue_hr", P_FET_BLUE_HEADROOM, Layout::blueTrimX, Layout::blueHeadroomY,
                          Layout::trimRadius, "HR", "%.0f", " dB", false, true);
            trimScrewKnob(dl, "fet_mix", P_MIX, Layout::blueTrimX, Layout::blueMixY, Layout::trimRadius,
                          "MIX", "%.0f", "%", true, true);
            drawFetBlueRatioButtons(dl, Layout::blueRatioX, Layout::buttonsY);
            // The blue meter section, top to bottom: the series mark, the VU, the nameplate.
            const ImVec2 b0 = panel.P(Layout::blueSectionX0, top + 4), b1 = panel.P(Layout::blueSectionX1, bottom - 4);
            dl->AddRectFilledMultiColor(b0, b1, FetTheme::blueSectionTop, FetTheme::blueSectionTop,
                                        FetTheme::blueSectionBottom, FetTheme::blueSectionBottom);
            dl->AddRect(b0, b1, FetTheme::blueSectionEdge, 2.0f * s, 0, 1.5f * s);
            dl->AddLine(ImVec2(b0.x + s, b0.y + s), ImVec2(b1.x - s, b0.y + s), FetTheme::blueSectionHighlight, s);
            spacedText(dl, meterCentre, top + 10, 16.0f, FetTheme::blueInk, "MC-2", 0, 0.16f);
            drawOptoMeter(dl, Layout::meterX, Layout::blueMeterY, Layout::meterW, Layout::meterH, reading, meterScale);
            spacedText(dl, meterCentre, Layout::blueMeterY + Layout::meterH + 16, 16.0f, FetTheme::blueInk,
                       "LIMITING AMPLIFIER", 0, 0.12f);
            panel.text(dl, meterCentre, Layout::blueMeterY + Layout::meterH + 38, 30.0f, FetTheme::blueInk,
                       "FET BLUE", 0, true);
        }
        else
        {
            drawFetRatioButtons(dl, Layout::blackRatioX, Layout::buttonsY);
            panel.text(dl, meterCentre, top + 6, 20.0f, FetTheme::badgeInk, "FET 76", 0, true);
            panel.text(dl, meterCentre, top + 28, 13.0f, FetTheme::badgeSeries, "REFERENCE SERIES", 0, true);
            drawOptoMeter(dl, Layout::meterX, Layout::blackMeterY, Layout::meterW, Layout::meterH, reading,
                          meterScale);
            panel.text(dl, meterCentre, Layout::blackMeterY + Layout::meterH + 10, 20.0f, FetTheme::badgeInk,
                       "MC-2", 0, true);
            panel.text(dl, meterCentre, Layout::blackMeterY + Layout::meterH + 34, 16.0f, FetTheme::badgeNameplate,
                       "LIMITING AMPLIFIER", 0, true);
            // Under the meter buttons, labelled below like the face's other knobs so it does not read as a fifth
            // meter row.
            trimScrewKnob(dl, "fet_mix", P_MIX, Layout::blackMixX, Layout::blackMixY, Layout::trimRadius,
                          "MIX", "%.0f", "%", true, true);
        }
        drawFetMeterSwitch(dl, Layout::meterButtonsX, Layout::buttonsY);
    }

    // FET BLUE's ratio column: 20:1, 8:1, 4:1, 2:1 from the top, pressed alone or two together
    // (ui_detail::fetBlueRatioAfterPress), then the side-chain filter's button where FET BLACK has All.
    void drawFetBlueRatioButtons(ImDrawList* dl, float x, float y)
    {
        const int position = multicompp::ui_detail::choiceIndex(value(P_FET_BLUE_RATIO), 11);
        const int lit = multicompp::ui_detail::fetBlueRatioButtons(position);
        panel.text(dl, x + 17, y - 26, 16.0f, FetTheme::columnLegend, "RATIO", 0, true);
        constexpr std::array<int, 4> visualButton{{3, 2, 1, 0}};
        constexpr std::array<const char*, 5> visualLabels{{"20", "8", "4", "2", "SC"}};
        for (int row = 0; row < 5; ++row)
        {
            const bool filterRow = row == 4;
            const float y0 = y + static_cast<float>(row) * 32.0f;
            const ImVec2 p0 = panel.P(x, y0);
            const ImVec2 p1 = panel.P(x + 34.0f, y0 + 26.0f);
            char id[40];
            std::snprintf(id, sizeof(id), "##fet_blue_ratio_button_%d", row);
            ImGui::SetCursorScreenPos(p0);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(p1.x - p0.x, p1.y - p0.y));
            const int button = filterRow ? -1 : visualButton[static_cast<size_t>(row)];
            const bool active = filterRow ? value(P_FET_BLUE_SC) >= 0.5f : (lit & (1 << button)) != 0;
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f), panel.P(x + 37.0f, y0 + 29.0f),
                              FetTheme::buttonShadow, 1.5f * panel.scale());
            dl->AddRectFilled(p0, p1,
                              active ? FetTheme::buttonLit
                                     : ImGui::IsItemHovered() ? FetTheme::buttonHover : FetTheme::buttonIdle,
                              1.5f * panel.scale());
            dl->AddRect(p0, p1, FetTheme::buttonEdge, 1.5f * panel.scale(), 0, panel.scale());
            if (active)
                dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f), panel.P(x + 5.0f, y0 + 23.0f),
                                  filterRow ? FetTheme::lampRed : FetTheme::lamp);
            panel.text(dl, x - 8.0f, y0 + 5.0f, 16.0f,
                       active ? FetTheme::buttonLabelLit : FetTheme::buttonLabel,
                       visualLabels[static_cast<size_t>(row)], 1, true);
            if (!clicked) continue;
            if (filterRow)
                setValue(P_FET_BLUE_SC, active ? 0.0f : 1.0f);
            else
                setValue(P_FET_BLUE_RATIO,
                         static_cast<float>(multicompp::ui_detail::fetBlueRatioAfterPress(position, button)));
        }
    }

    // sloDetent: FET BLUE's Attack, whose counter-clockwise stop is the SLO detent (ui_detail::fetAttackAtSlo).
    void fetKnob(ImDrawList* dl, const char* id, uint32_t p,
                 float x, float y, float radius, const char* label,
                 bool gainScale, const char* format, const char* suffix,
                 bool sloDetent = false)
    {
        // Reuse the shared gesture and type-entry contract, but draw the body
        // here so FET mode gets the reference unit's silver skirt and knurling.
        fetKnobSplitter.Split(dl, 2);
        fetKnobSplitter.SetCurrentChannel(dl, 1);
        const bool timingDial = multicompp::ui_detail::fetTimingUsesDialReadout(p);
        const bool atSlo = sloDetent && multicompp::ui_detail::fetAttackAtSlo(plainValueForHost(p, values[p]));
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   atSlo ? multicompp::ui_detail::kFetSloText : nullptr,
                   false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   timingDial ? &fetTimingHostToDial : &knobHostToPlain,
                   timingDial ? &fetTimingDialToHost : &knobPlainToHost,
                   this, true, sloDetent ? &fetSloAttackParseText : nullptr);
        endTrackedControl();
        fetKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = timingDial
            ? std::clamp((multicompp::ui_detail::fetTimingDialValue(values[p], p) - 1.0f)
                             / 6.0f,
                         0.0f, 1.0f)
            : std::clamp((plain - minimum)
                             / std::max(maximum - minimum, 1.0e-6f),
                         0.0f, 1.0f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;

        if (gainScale)
        {
            // Printed as on the reference faceplate. The audio law's
            // counter-clockwise stop is finite (Input -41.95 dB relative),
            // but the silkscreen reads INF.
            constexpr std::array<const char*, 9> labels{{
                "INF", "48", "36", "30", "24", "18", "12", "6", "0"}};
            duskdaf::KnobRingStyle ring;
            // Every numeral at the legend size (16): the arc has room for all nine.
            ring.markInner = 5.0f;   ring.markOuter = 12.0f;  ring.markThickness = 1.6f;
            ring.minorInner = 8.0f;  ring.minorOuter = 12.0f; ring.minorThickness = 1.0f;
            ring.markColor = FetTheme::ringMark;
            ring.labelOffset = multicompp::ui_detail::FetFaceplateLayout::ringLabelOffset;
            ring.labelNudgeY = -9.0f; ring.labelSize = 16.0f;
            ring.labelColor = FetTheme::ringLabel;
            ring.labelCentred = true;
            for (int tick = 0; tick <= 16; ++tick)
            {
                const bool major = tick % 2 == 0;
                duskdaf::drawKnobRingMark(
                    panel, dl, x, y, radius, static_cast<float>(tick) / 16.0f,
                    major ? labels[static_cast<size_t>(tick / 2)] : nullptr,
                    ring, !major);
            }
        }
        else
        {
            // FET BLUE's Attack: the stop is the SLO detent, as the reference names it.
            const std::array<const char*, 4> labels{{
                sloDetent ? multicompp::ui_detail::kFetSloText : "1", "3", "5", "7"}};
            duskdaf::KnobRingStyle ring;
            ring.dotMarks = true;
            ring.markOuter = 9.0f;   ring.minorOuter = 9.0f;
            ring.dotRadius = 2.0f;   ring.minorDotRadius = 1.2f;
            ring.dotSegments = 10;
            ring.markColor = FetTheme::ringDot;
            ring.labelOffset = 20.0f; ring.labelNudgeY = -9.0f; ring.labelSize = 16.0f;
            ring.labelColor = FetTheme::ringLabel;
            ring.labelCentred = true;
            for (int tick = 0; tick <= 6; ++tick)
            {
                const bool major = tick % 2 == 0;
                duskdaf::drawKnobRingMark(
                    panel, dl, x, y, radius, static_cast<float>(tick) / 6.0f,
                    major ? labels[static_cast<size_t>(tick / 2)] : nullptr,
                    ring, !major);
            }
        }

        // Soft shadow, concentric spun-metal skirt, knurled black collar and
        // machined cap. Keep every structural circle on the exact center; the
        // previous offset 91% disk read as a second gray knob at upper left.
        dl->AddCircleFilled(ImVec2(center.x + 3.0f * scale, center.y + 4.0f * scale),
                            r * 1.01f, FetTheme::knobShadow, 56);
        dl->AddCircleFilled(center, r, FetTheme::skirtEdge, 56);
        dl->AddCircleFilled(center, r * 0.96f, FetTheme::skirt, 56);
        dl->AddCircleFilled(center, r * 0.83f, FetTheme::skirtInner, 56);
        dl->AddCircleFilled(center, r * 0.75f, FetTheme::skirtCore, 56);
        dl->PathArcTo(center, r * 0.90f, -2.55f, -0.55f, 30);
        dl->PathStroke(FetTheme::skirtHighlight, 0, 1.5f * scale);
        dl->PathArcTo(center, r * 0.91f, 0.55f, 2.55f, 30);
        dl->PathStroke(FetTheme::skirtShade, 0, 1.8f * scale);
        dl->AddCircle(center, r, FetTheme::skirtRim, 56, 1.3f * scale);
        const float collarR = r * 0.69f;
        // The knurling turns with the pointer; the skirt and cap highlights
        // are lighting and stay put.
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        dl->AddCircleFilled(center, collarR, FetTheme::collar, 52);
        for (int notch = 0; notch < 28; ++notch)
        {
            const float angle = pointerAngle
                + 2.0f * kUiPi * static_cast<float>(notch) / 28.0f;
            const ImVec2 direction(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(center.x + direction.x * collarR * 0.78f,
                               center.y + direction.y * collarR * 0.78f),
                        ImVec2(center.x + direction.x * collarR,
                               center.y + direction.y * collarR),
                        FetTheme::knurl, 1.0f * scale);
        }
        const float capR = r * 0.49f;
        dl->AddCircleFilled(center, capR, FetTheme::capBase, 48);
        constexpr int facets = 96;
        // Interpolate the metal highlight across one mesh. Anti-aliasing each
        // triangle separately would expose bright spokes between the facets.
        dl->PrimReserve(facets * 3, facets + 1);
        const unsigned int capBase = dl->_VtxCurrentIdx;
        const ImVec2 capUv = ImGui::GetIO().Fonts->TexUvWhitePixel;
        dl->PrimWriteVtx(center, capUv, FetTheme::capCentre);
        for (int i = 0; i < facets; ++i)
        {
            const float a = i * 2.0f * kUiPi / facets;
            const int shade = static_cast<int>(176 + 58 * std::cos(2 * a - 0.7f));
            dl->PrimWriteVtx(
                ImVec2(center.x + std::cos(a) * capR * 0.95f, center.y + std::sin(a) * capR * 0.95f),
                capUv, IM_COL32(shade, shade + 1, shade - 2, 255));
        }
        for (int i = 0; i < facets; ++i)
        {
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(capBase));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(capBase + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(capBase + 1 + (i + 1) % facets));
        }
        dl->PathArcTo(center, capR * 0.78f, -2.50f, -0.65f, 22);
        dl->PathStroke(FetTheme::capHighlight, 0, 1.2f * scale);
        dl->AddCircle(center, capR, FetTheme::capRim, 48, 1.1f * scale);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * r * 0.72f,
                           center.y + pointer.y * r * 0.72f),
                    ImVec2(center.x + pointer.x * r * 0.94f,
                           center.y + pointer.y * r * 0.94f),
                    FetTheme::pointer, 2.0f * scale);

        // INPUT and OUTPUT (20) under their dials; ATTACK (16) above its dial and RELEASE (16) under it, clear of
        // their numerals.
        panel.text(dl, x, gainScale ? y + radius + multicompp::ui_detail::FetFaceplateLayout::legendDrop
                                    : y + (label[0] == 'A' ? -radius - 39.0f : radius + 21.0f),
                   gainScale ? 20.0f : 16.0f,
                   FetTheme::knobLegend, label, 0, true);
        fetKnobSplitter.Merge(dl);
    }

    // floorText: the legend and travel marks at the readability floor (16 px, the faces laid out on the canvas);
    // false keeps the VCA face's own small lettering.
    void trimScrewKnob(ImDrawList* dl, const char* id, uint32_t p,
                     float x, float y, float radius, const char* label,
                     const char* format, const char* suffix, bool labelBelow = false, bool floorText = false)
    {
        // The Opto face established the fleet contract for non-reference
        // controls: keep the full parameter available as a small set-screw trim
        // without letting it compete with the hardware's primary controls.
        fetKnobSplitter.Split(dl, 2);
        fetKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 8.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, true);
        endTrackedControl();
        fetKnobSplitter.SetCurrentChannel(dl, 0);

        const float plain = plainValueForHost(p, values[p]);
        const float minimum = plainValueForHost(p, hostMinimum(p));
        const float maximum = plainValueForHost(p, hostMaximum(p));
        const float t = std::clamp((plain - minimum)
            / std::max(maximum - minimum, 1.0e-6f), 0.0f, 1.0f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;

        std::array<ImVec2, 6> nut{};
        for (int point = 0; point < 6; ++point)
        {
            const float angle = kUiPi / 6.0f + static_cast<float>(point)
                * kUiPi / 3.0f;
            nut[static_cast<size_t>(point)] = ImVec2(
                center.x + std::cos(angle) * r * 1.18f,
                center.y + std::sin(angle) * r * 1.18f);
        }
        std::array<ImVec2, 6> shadow = nut;
        for (auto& point : shadow)
        {
            point.x += 2.0f * scale;
            point.y += 3.0f * scale;
        }
        dl->AddConvexPolyFilled(shadow.data(), 6, TrimScrewTheme::shadow);
        dl->AddConvexPolyFilled(nut.data(), 6, TrimScrewTheme::nut);
        dl->AddCircleFilled(center, r, TrimScrewTheme::head, 32);
        dl->AddCircleFilled(center, r * 0.84f,
                            TrimScrewTheme::face, 32);
        dl->PathArcTo(center, r * 0.70f, -2.45f, -0.75f, 18);
        dl->PathStroke(TrimScrewTheme::highlight, 0, panel.scale());
        dl->AddCircle(center, r, TrimScrewTheme::rim, 32, 1.1f * scale);
        const float angle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(angle), -std::cos(angle));
        dl->AddLine(ImVec2(center.x - pointer.x * r * 0.58f,
                           center.y - pointer.y * r * 0.58f),
                    ImVec2(center.x + pointer.x * r * 0.58f,
                           center.y + pointer.y * r * 0.58f),
                    TrimScrewTheme::slot, 2.0f * scale);
        if (floorText)
        {
            const float nut = radius * 1.18f;
            panel.text(dl, x, labelBelow ? y + nut + 5.0f : y - nut - 21.0f, 16.0f,
                       TrimScrewTheme::legend, label, 0, true);
            panel.text(dl, x - nut - 15.0f, y - 9.0f, 16.0f, TrimScrewTheme::minus, "-", 0, true);
            panel.text(dl, x + nut + 15.0f, y - 9.0f, 16.0f, TrimScrewTheme::plus, "+", 0, true);
        }
        else
        {
            panel.text(dl, x, labelBelow ? y + radius + 9.0f : y - 38.0f, 7.5f,
                       TrimScrewTheme::legend, label, 0, true);
            panel.text(dl, x - 27.0f, y - 5.0f, 10.0f,
                       TrimScrewTheme::minus, "-", 0, true);
            panel.text(dl, x + 27.0f, y - 5.0f, 10.0f,
                       TrimScrewTheme::plus, "+", 0, true);
        }
        fetKnobSplitter.Merge(dl);
    }

    void drawFetRatioButtons(ImDrawList* dl, float x, float y)
    {
        const int selected = multicompp::ui_detail::choiceIndex(value(P_FET_RATIO), 5);
        panel.text(dl, x + 17, y - 26, 16.0f, FetTheme::columnLegend,
                   "RATIO", 0, true);
        constexpr std::array<int, 5> visualOrder{{3, 2, 1, 0, 4}};
        constexpr std::array<const char*, 5> visualLabels{{
            "20", "12", "8", "4", "ALL"}};
        for (int row = 0; row < 5; ++row)
        {
            const int index = visualOrder[static_cast<size_t>(row)];
            const float y0 = y + static_cast<float>(row) * 32.0f;
            const ImVec2 p0 = panel.P(x, y0);
            const ImVec2 p1 = panel.P(x + 34.0f, y0 + 26.0f);
            char id[32];
            std::snprintf(id, sizeof(id), "##fet_ratio_button_%d", index);
            ImGui::SetCursorScreenPos(p0);
            const bool clicked = ImGui::InvisibleButton(
                id, ImVec2(p1.x - p0.x, p1.y - p0.y));
            const bool active = index == selected;
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                              panel.P(x + 37.0f, y0 + 29.0f),
                              FetTheme::buttonShadow, 1.5f * panel.scale());
            dl->AddRectFilled(p0, p1,
                              active ? FetTheme::buttonLit
                                     : ImGui::IsItemHovered()
                                         ? FetTheme::buttonHover
                                         : FetTheme::buttonIdle,
                              1.5f * panel.scale());
            dl->AddRect(p0, p1, FetTheme::buttonEdge,
                        1.5f * panel.scale(), 0, panel.scale());
            if (active)
                dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                                  panel.P(x + 5.0f, y0 + 23.0f),
                                  index == 4 ? FetTheme::lampRed
                                             : FetTheme::lamp);
            panel.text(dl, x - 8.0f, y0 + 5.0f, 16.0f,
                       active ? FetTheme::buttonLabelLit
                              : FetTheme::buttonLabel,
                       visualLabels[static_cast<size_t>(row)], 1, true);
            if (clicked && !active) setValue(P_FET_RATIO, static_cast<float>(index));
        }
    }

    void drawFetMeterSwitch(ImDrawList* dl, float x, float y)
    {
        const int selected = multicompp::ui_detail::fetMeterButton(values[P_FET_METER], values[P_BYPASS] >= 0.5f);
        panel.text(dl, x + 26.0f, y - 26.0f, 16.0f,
                   FetTheme::columnLegend, "METER", 0, true);
        constexpr std::array<const char*, 4> labels{{"GR", "+8", "+4", "OFF"}};
        for (int row = 0; row < 4; ++row)
        {
            const float y0 = y + static_cast<float>(row) * 36.0f;
            ImGui::SetCursorScreenPos(panel.P(x, y0));
            ImGui::PushID(row);
            ImGui::InvisibleButton("##fet_meter", ImVec2(72 * panel.scale(), 32 * panel.scale()));
            if (ImGui::IsItemClicked())
                multicompp::ui_detail::selectFetMeterButton(row, [this](uint32_t p) { return values[p]; },
                                                            [this](uint32_t p, float v) { setValue(p, v); });
            ImGui::PopID();
            dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                              panel.P(x + 29.0f, y0 + 33.0f),
                              FetTheme::meterButtonShadow, 1.5f * panel.scale());
            dl->AddRectFilled(panel.P(x, y0), panel.P(x + 26.0f, y0 + 30.0f),
                              FetTheme::meterButton, 1.5f * panel.scale());
            dl->AddRect(panel.P(x, y0), panel.P(x + 26.0f, y0 + 30.0f),
                        FetTheme::meterButtonEdge, 1.5f * panel.scale(), 0,
                        panel.scale());
            if (row == selected)
                dl->AddRectFilled(panel.P(x + 2.0f, y0 + 3.0f),
                                  panel.P(x + 5.0f, y0 + 27.0f),
                                  FetTheme::lamp);
            panel.text(dl, x + 34.0f, y0 + 7.0f, 16.0f,
                       row == selected ? FetTheme::buttonLabelLit
                                : FetTheme::meterButtonLabel,
                       labels[static_cast<size_t>(row)], -1, true);
        }
    }

    // Silkscreen lettering with letter-spacing, which ImGui text lacks: each
    // glyph is placed by its own advance plus a tracking gap. align: -1 left,
    // 0 centred, 1 right.
    void spacedText(ImDrawList* dl, float x, float y, float size, ImU32 col,
                    const char* text, int align, float tracking = 0.16f)
    {
        duskdaf::spacedText(panel, dl, x, y, size, col, text, align, tracking);
    }

    // The VCA face's colours by role (kVcaInk and kVcaInkDim are its legend ink, lit and unlit), sampled from the
    // face as it was drawn before the tokens; the legends keep kVcaInk.
    struct VcaTheme
    {
        // The case: matte charcoal face with speckle, aluminium rails, walnut cheeks and the edge
        static constexpr ImU32 dropShadow = IM_COL32(0, 0, 0, 150);
        static constexpr ImU32 faceTop = IM_COL32(44, 44, 45, 255);
        static constexpr ImU32 faceBottomRight = IM_COL32(24, 24, 25, 255);
        static constexpr ImU32 faceBottomLeft = IM_COL32(26, 26, 27, 255);
        static constexpr ImU32 speckleLight = IM_COL32(255, 255, 255, 9);
        static constexpr ImU32 speckleDark = IM_COL32(0, 0, 0, 40);
        static constexpr ImU32 railTop = IM_COL32(190, 192, 192, 255);
        static constexpr ImU32 railBottom = IM_COL32(118, 120, 120, 255);
        static constexpr ImU32 railSeam = IM_COL32(6, 6, 6, 255);
        static constexpr ImU32 railSeamHighlight = IM_COL32(90, 90, 92, 120);
        static constexpr ImU32 cheekTopLeft = IM_COL32(128, 78, 40, 255);
        static constexpr ImU32 cheekTopRight = IM_COL32(112, 66, 32, 255);
        static constexpr ImU32 cheekBottomRight = IM_COL32(72, 40, 18, 255);
        static constexpr ImU32 cheekBottomLeft = IM_COL32(84, 48, 22, 255);
        static constexpr ImU32 cheekGrainLight = IM_COL32(168, 108, 58, 80);
        static constexpr ImU32 cheekGrainDark = IM_COL32(34, 16, 4, 120);
        static constexpr ImU32 cheekVignette = IM_COL32(0, 0, 0, 90);
        static constexpr ImU32 cheekSeam = IM_COL32(4, 3, 2, 255);
        static constexpr ImU32 edge = IM_COL32(18, 18, 18, 255);
        static constexpr ImU32 clear = IM_COL32(0, 0, 0, 0);
        // Brushing, on the rails and the knob domes
        static constexpr ImU32 brushLight = IM_COL32(255, 255, 255, 40);
        static constexpr ImU32 brushDark = IM_COL32(0, 0, 0, 28);
        // The threshold jewels: amber BELOW, red ABOVE
        static constexpr ImU32 jewelBezel = IM_COL32(52, 52, 54, 255);
        static constexpr ImU32 jewelHighlight = IM_COL32(255, 248, 225, 220);
        static constexpr ImU32 jewelBelow = IM_COL32(240, 205, 70, 255);
        static constexpr ImU32 jewelBelowGlow = IM_COL32(240, 205, 70, 80);
        static constexpr ImU32 jewelAbove = IM_COL32(220, 62, 46, 255);
        static constexpr ImU32 jewelAboveGlow = IM_COL32(220, 62, 46, 80);
        // The meter's surround and the badge
        static constexpr ImU32 meterSurround = IM_COL32(40, 40, 42, 255);
        static constexpr ImU32 badgeInk = IM_COL32(240, 240, 236, 255);
        // The black bezels of the PULL/SC switch and the meter keys, the switch's silver bar and the keycaps
        static constexpr ImU32 bezel = IM_COL32(8, 8, 8, 255);
        static constexpr ImU32 switchBezelEdge = IM_COL32(60, 62, 60, 255);
        static constexpr ImU32 switchHover = IM_COL32(255, 255, 255, 14);
        static constexpr ImU32 barShadow = IM_COL32(0, 0, 0, 140);
        static constexpr ImU32 barTop = IM_COL32(222, 224, 222, 255);
        static constexpr ImU32 barBottom = IM_COL32(158, 160, 158, 255);
        static constexpr ImU32 barHighlight = IM_COL32(250, 250, 250, 180);
        static constexpr ImU32 barEdge = IM_COL32(36, 37, 36, 255);
        static constexpr ImU32 keyShadow = IM_COL32(0, 0, 0, 130);
        static constexpr ImU32 keyBezelEdge = IM_COL32(58, 60, 58, 255);
        static constexpr ImU32 keyTop = IM_COL32(226, 228, 226, 255);
        static constexpr ImU32 keyBottom = IM_COL32(164, 166, 164, 255);
        static constexpr ImU32 keyLitTop = IM_COL32(150, 152, 150, 255);
        static constexpr ImU32 keyLitBottom = IM_COL32(104, 106, 104, 255);
        static constexpr ImU32 keyLitShade = IM_COL32(0, 0, 0, 110);
        static constexpr ImU32 keyHighlight = IM_COL32(250, 251, 250, 200);
        static constexpr ImU32 keyHover = IM_COL32(255, 255, 255, 22);
        static constexpr ImU32 keyEdge = IM_COL32(40, 41, 40, 255);
        // The knobs: minor marks, shadow, serrated skirt, silver dome and pointer
        static constexpr ImU32 ringMinor = IM_COL32(214, 215, 210, 220);
        static constexpr ImU32 knobShadow = IM_COL32(0, 0, 0, 160);
        static constexpr ImU32 skirt = IM_COL32(24, 24, 24, 255);
        static constexpr ImU32 toothLight = IM_COL32(76, 77, 76, 255);
        static constexpr ImU32 toothDark = IM_COL32(10, 10, 10, 255);
        static constexpr ImU32 capBase = IM_COL32(150, 152, 150, 255);
        static constexpr ImU32 capDome = IM_COL32(204, 206, 204, 255);
        static constexpr ImU32 capHighlight = IM_COL32(255, 255, 255, 34);
        static constexpr ImU32 capShade = IM_COL32(60, 62, 60, 170);
        static constexpr ImU32 capRim = IM_COL32(88, 90, 88, 255);
        static constexpr ImU32 pointer = IM_COL32(28, 29, 28, 255);
    };

    void drawVca(ImDrawList* dl)
    {
        // VCA compressor face for the VCA mode, in the reference panel's arrangement (VcaFaceplateLayout). Only
        // the reference's front-panel controls are drawn; attack, release, Over Easy and the detector selector
        // have no reference-unit equivalent and stay host-visible parameters without faceplate controls. No
        // third-party marks.
        using Layout = multicompp::ui_detail::VcaFaceplateLayout;
        constexpr float left = Layout::left, right = Layout::right;
        constexpr float top = Layout::top, bottom = Layout::bottom;
        constexpr float cheek = Layout::cheek, rail = Layout::rail;
        const float s = panel.scale();
        dl->AddRectFilled(panel.P(left + 5, top + 7), panel.P(right + 5, bottom + 7),
                          VcaTheme::dropShadow, 4.0f * s);
        // Matte charcoal face with a faint speckle and a gentle vertical fall-off.
        dl->AddRectFilledMultiColor(panel.P(left, top), panel.P(right, bottom),
                                    VcaTheme::faceTop, VcaTheme::faceTop,
                                    VcaTheme::faceBottomRight, VcaTheme::faceBottomLeft);
        {
            // Between the cheeks and the rails, clear of their seams.
            constexpr auto speckleW = static_cast<uint32_t>(right - left - 2.0f * cheek);
            constexpr auto speckleH = static_cast<uint32_t>(bottom - top - 2.0f * rail - 2.0f);
            uint32_t seed = 0x1600u;
            for (int i = 0; i < 120; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float px = left + cheek + static_cast<float>((seed >> 8) % speckleW);
                seed = seed * 1664525u + 1013904223u;
                const float py = top + rail + static_cast<float>((seed >> 8) % speckleH);
                dl->AddRectFilled(panel.P(px, py), panel.P(px + 1.2f, py + 1.2f),
                                  (i & 3) ? VcaTheme::speckleLight : VcaTheme::speckleDark);
            }
        }
        // Brushed aluminium rails with a dark seam against the face.
        for (const bool topRail : {true, false})
        {
            const float y0 = topRail ? top : bottom - rail;
            const float y1 = topRail ? top + rail : bottom;
            dl->AddRectFilledMultiColor(panel.P(left, y0), panel.P(right, y1),
                                        VcaTheme::railTop, VcaTheme::railTop,
                                        VcaTheme::railBottom, VcaTheme::railBottom);
            for (int row = static_cast<int>(y0 + 1); row < static_cast<int>(y1); row += 2)
                dl->AddLine(panel.P(left, static_cast<float>(row)), panel.P(right, static_cast<float>(row)),
                            (row * 7) % 5 == 0 ? VcaTheme::brushLight : VcaTheme::brushDark, s);
            dl->AddLine(panel.P(left, topRail ? y1 : y0), panel.P(right, topRail ? y1 : y0),
                        VcaTheme::railSeam, 1.6f * s);
            dl->AddLine(panel.P(left, topRail ? y1 + 1.5f : y0 - 1.5f), panel.P(right, topRail ? y1 + 1.5f : y0 - 1.5f),
                        VcaTheme::railSeamHighlight, s);
        }
        // Walnut cheeks: warm gradient, deterministic grain, a soft vignette
        // towards the face, and a dark seam.
        for (const bool leftCheek : {true, false})
        {
            const float x0 = leftCheek ? left : right - cheek;
            const float x1 = leftCheek ? left + cheek : right;
            dl->AddRectFilledMultiColor(panel.P(x0, top), panel.P(x1, bottom),
                                        VcaTheme::cheekTopLeft, VcaTheme::cheekTopRight,
                                        VcaTheme::cheekBottomRight, VcaTheme::cheekBottomLeft);
            for (int grain = 0; grain < 14; ++grain)
            {
                const float gx = x0 + 2.5f + static_cast<float>(grain) * (cheek - 5.0f) / 13.0f;
                const float wobble = 2.0f * std::sin(static_cast<float>(grain) * 1.9f);
                dl->AddLine(panel.P(gx + wobble, top + 2.0f), panel.P(gx - wobble, bottom - 2.0f),
                            grain % 3 == 0 ? VcaTheme::cheekGrainLight : VcaTheme::cheekGrainDark, 1.1f * s);
            }
            dl->AddRectFilledMultiColor(panel.P(leftCheek ? x1 - 10.0f : x0, top), panel.P(leftCheek ? x1 : x0 + 10.0f, bottom),
                                        leftCheek ? VcaTheme::clear : VcaTheme::cheekVignette,
                                        leftCheek ? VcaTheme::cheekVignette : VcaTheme::clear,
                                        leftCheek ? VcaTheme::cheekVignette : VcaTheme::clear,
                                        leftCheek ? VcaTheme::clear : VcaTheme::cheekVignette);
            dl->AddLine(panel.P(leftCheek ? x1 : x0, top), panel.P(leftCheek ? x1 : x0, bottom),
                        VcaTheme::cheekSeam, 1.8f * s);
        }
        dl->AddRect(panel.P(left, top), panel.P(right, bottom), VcaTheme::edge, 4.0f * s, 0, 1.3f * s);

        // Section titles, the hero legends' size (20) as on the Opto and FET faces.
        spacedText(dl, Layout::thresholdX, Layout::titleY, 20.0f, kVcaInk, "THRESHOLD", 0);
        spacedText(dl, Layout::compressionX, Layout::titleY, 20.0f, kVcaInk, "COMPRESSION", 0);
        spacedText(dl, Layout::outputX, Layout::titleY, 20.0f, kVcaInk, "OUTPUT GAIN", 0);

        // Threshold lamps: BELOW (amber) under threshold, ABOVE (red) at or
        // over it, from the DSP's published input peak level; labels sit
        // outboard of the lamps like the reference.
        const float inputDb = levelMeterDb(false);
        const float thresholdDb = plainValueForHost(P_VCA_THRESHOLD, values[P_VCA_THRESHOLD]);
        const bool above = multicompp::ui_detail::vcaSignalAboveThreshold(inputDb, thresholdDb);
        constexpr float jewelLegendGap = 10.0f, jewelLegendY = Layout::jewelY - 8.0f;
        spacedText(dl, Layout::belowX - Layout::jewelRadius - jewelLegendGap, jewelLegendY, 16.0f, kVcaInk, "BELOW", 1);
        // The reference's two jewels: amber below the threshold, red above.
        duskdaf::DuskPanel::LedStyle jewel;
        jewel.bezelColor = VcaTheme::jewelBezel;
        jewel.rimRadiusAdd = 1.2f;
        jewel.glowRadiusAdd = 7.0f;
        jewel.dropShadowRadiusAdd = 3.0f;
        jewel.highlightScale = 1.5f;
        jewel.highlightColor = VcaTheme::jewelHighlight;
        jewel.offFromOnColor = true;
        jewel.offHighlight = true;

        jewel.onColor = VcaTheme::jewelBelow;
        jewel.glowColor = VcaTheme::jewelBelowGlow;
        panel.led(dl, Layout::belowX, Layout::jewelY, !above, Layout::jewelRadius, jewel);

        jewel.onColor = VcaTheme::jewelAbove;
        jewel.glowColor = VcaTheme::jewelAboveGlow;
        panel.led(dl, Layout::aboveX, Layout::jewelY, above, Layout::jewelRadius, jewel);
        spacedText(dl, Layout::aboveX + Layout::jewelRadius + jewelLegendGap, jewelLegendY, 16.0f, kVcaInk, "ABOVE", -1);

        {
            // The reference's ring artwork: 10 mV .. 3 V at the hardware's
            // angles. Our threshold law follows the reference's knob positions,
            // so the same angles are honest; the values passed are the dB the
            // linear -55..0 law places at those angles (t = 0, .25, .40, .60,
            // .75, 1). The read-out bubble keeps the real dB threshold.
            constexpr std::array<float, 6> majors{{-55.0f, -41.25f, -33.0f, -22.0f, -13.75f, 0.0f}};
            constexpr std::array<const char*, 6> labels{{"10 mV", "30 mV", "100 mV", "300 mV", "1 V", "3 V"}};
            constexpr std::array<float, 4> minors{{-48.0f, -27.5f, -17.5f, -6.5f}};
            vcaKnob(dl, "vca_thr", P_VCA_THRESHOLD, Layout::thresholdX, Layout::knobY, Layout::knobRadius,
                    "THRESHOLD", "%.1f", " dB", majors.data(), labels.data(),
                    static_cast<int>(majors.size()), minors.data(), static_cast<int>(minors.size()));
        }
        {
            // Position knob; ring marks placed by the measured ratio law and the
            // read-out shows the applied ratio (MultiCompVcaLaw.hpp).
            constexpr std::array<float, 9> ratios{{1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f,
                                                   std::numeric_limits<float>::infinity()}};
            constexpr std::array<const char*, 9> labels{{"1", "1.5", "2", "3", "4", "6", "10", "20", "INF"}};
            std::array<float, 9> majors{};
            for (size_t i = 0; i < ratios.size(); ++i)
                majors[i] = duskaudio::vcaLaw::compressPosition(ratios[i]);
            vcaKnob(dl, "vca_ratio", P_VCA_RATIO, Layout::compressionX, Layout::knobY, Layout::compressionRadius,
                    "COMPRESSION", "%.1f", ":1", majors.data(), labels.data(),
                    static_cast<int>(majors.size()), nullptr, 0,
                    &vcaCompressionHostToRatio, &vcaCompressionRatioToHost);
        }
        {
            constexpr std::array<float, 5> majors{{-20.0f, -10.0f, 0.0f, 10.0f, 20.0f}};
            constexpr std::array<const char*, 5> labels{{"-20", "-10", "0", "+10", "+20"}};
            constexpr std::array<float, 4> minors{{-15.0f, -5.0f, 5.0f, 15.0f}};
            vcaKnob(dl, "vca_out", P_VCA_OUT, Layout::outputX, Layout::knobY, Layout::knobRadius,
                    "OUTPUT GAIN", "%.1f", " dB", majors.data(), labels.data(),
                    static_cast<int>(majors.size()), minors.data(), static_cast<int>(minors.size()));
        }
        // PULL/SC: printed under the threshold knob like the reference; the
        // switch itself takes the POWER button's place (global bypass covers
        // power).
        spacedText(dl, Layout::thresholdX, Layout::pullLegendY, 16.0f, kVcaInk, "PULL/SC", 0);
        vcaPullSwitch(dl, Layout::thresholdX, Layout::pullY);

        drawVcaMeterButtons(dl);

        // Meter: the reference's amber-lit window with blue scale, on the
        // shared needle meter. One -40..+20 dB scale reads level (0 VU =
        // -12 dBFS, fleet calibration) or gain reduction per the selected key.
        const auto scale = multicompp::ui_detail::vcaMeterScale(
            multicompp::ui_detail::vcaMeterSourceLabel(vcaMeterSource));
        // Gain change reads the plugin's needle movement (MultiCompGainReductionNeedle.hpp),
        // so the widget's cosmetic pole is off for it.
        auto style = multicompp::ui_detail::vcaMeterStyle();
        style.smoothNeedle = vcaMeterSource != 2 || !hasNeedles();
        const float meterDb = vcaMeterSource == 0 ? inputDb + 12.0f
                            : vcaMeterSource == 1 ? levelMeterDb(true) + 12.0f
                            : vcaNeedleGainReduction();
        using Meter = multicompp::ui_detail::VcaMeterRect;
        duskdaf::drawVuMeter(panel, dl, Meter::x0, Meter::y0, Meter::x1, Meter::y1, meterDb,
                             vcaVuNeedle, scale, VcaTheme::meterSurround, style);

        // Model line above the meter and badge below it, at the FET face's sizes; the MIX trim is the same set
        // screw as Opto and FET, in the column right of the meter, level with the badge.
        constexpr float meterCentre = 0.5f * (Meter::x0 + Meter::x1);
        spacedText(dl, meterCentre, Layout::modelY, 20.0f, VcaTheme::badgeInk, "VCA 60", 0, 0.12f);
        spacedText(dl, meterCentre, Layout::seriesY, 13.0f, kVcaInk, "REFERENCE SERIES", 0, 0.12f);
        spacedText(dl, meterCentre, Layout::badgeY, 20.0f, VcaTheme::badgeInk, "MC-2", 0, 0.12f);
        spacedText(dl, meterCentre, Layout::nameplateY, 16.0f, kVcaInk, "COMPRESSOR/LIMITER", 0, 0.12f);
        trimScrewKnob(dl, "vca_mix", P_MIX, Layout::mixX, Layout::mixY, Layout::mixRadius, "MIX", "%.0f", "%", true,
                      true);
    }

    void vcaPullSwitch(ImDrawList* dl, float x, float y)
    {
        const bool on = plainValueForHost(P_SC_HP, values[P_SC_HP]) >= 1.0f;   // same OFF rule as optoKnob
        if (on) vcaLastScHp = values[P_SC_HP];
        const ImVec2 p0 = panel.P(x - 40.0f, y - 16.0f);
        const ImVec2 p1 = panel.P(x + 40.0f, y + 16.0f);
        ImGui::SetCursorScreenPos(p0);
        const bool clicked = ImGui::InvisibleButton("##vca_pull_sc", ImVec2(p1.x - p0.x, p1.y - p0.y));
        const float s = panel.scale();
        // Black bezel like the reference's push buttons, holding a sliding
        // silver bar that sits right when the filter is in.
        dl->AddRectFilled(panel.P(x - 40.0f, y - 15.0f), panel.P(x + 40.0f, y + 15.0f),
                          VcaTheme::bezel, 3.0f * s);
        dl->AddRect(panel.P(x - 40.0f, y - 15.0f), panel.P(x + 40.0f, y + 15.0f),
                    VcaTheme::switchBezelEdge, 3.0f * s, 0, s);
        const float barX = on ? x + 17.0f : x - 17.0f;
        dl->AddRectFilled(panel.P(barX - 16.0f + 1.5f, y - 9.0f + 2.0f), panel.P(barX + 16.0f + 1.5f, y + 9.0f + 2.0f),
                          VcaTheme::barShadow, 2.5f * s);
        dl->AddRectFilledMultiColor(panel.P(barX - 16.0f, y - 9.0f), panel.P(barX + 16.0f, y + 9.0f),
                                    VcaTheme::barTop, VcaTheme::barTop,
                                    VcaTheme::barBottom, VcaTheme::barBottom);
        dl->AddLine(panel.P(barX - 14.0f, y - 7.5f), panel.P(barX + 14.0f, y - 7.5f), VcaTheme::barHighlight, 1.2f * s);
        dl->AddRect(panel.P(barX - 16.0f, y - 9.0f), panel.P(barX + 16.0f, y + 9.0f), VcaTheme::barEdge, 2.5f * s, 0, s);
        if (ImGui::IsItemHovered())
            dl->AddRectFilled(panel.P(x - 40.0f, y - 15.0f), panel.P(x + 40.0f, y + 15.0f), VcaTheme::switchHover, 3.0f * s);
        spacedText(dl, x - 50.0f, y - 8.0f, 16.0f, on ? kVcaInkDim : kVcaInk, "OUT", 1);
        spacedText(dl, x + 50.0f, y - 8.0f, 16.0f, on ? kVcaInk : kVcaInkDim, "IN", -1);
        if (clicked)
            setHostValue(P_SC_HP, multicompp::ui_detail::vcaSidechainSwitchTarget(
                on, values[P_SC_HP], vcaLastScHp,
                hostMinimum(P_SC_HP), hostMaximum(P_SC_HP)));
    }

    void vcaKnob(ImDrawList* dl, const char* id, uint32_t p,
                 float x, float y, float radius, const char* label,
                 const char* format, const char* suffix,
                 const float* majorValues, const char* const* majorLabels,
                 int majorCount, const float* minorValues, int minorCount,
                 float (*toDisplay)(float, uint32_t, void*) = &knobHostToPlain,
                 float (*fromDisplay)(float, uint32_t, void*) = &knobPlainToHost)
    {
        // Shared gesture/readout contract; the reference's silver domed knob
        // on a black serrated skirt is drawn here. Ring marks are placed
        // through the parameter's own host law, so the printed ring and the
        // pointer agree by construction; the read-out can use a different
        // law (Compression shows the applied ratio for its position).
        const bool dragInDisplay = toDisplay == &knobHostToPlain;
        vcaKnobSplitter.Split(dl, 2);
        vcaKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, true,
                   nullptr, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   toDisplay, fromDisplay, this, dragInDisplay);
        endTrackedControl();
        vcaKnobSplitter.SetCurrentChannel(dl, 0);

        const float hostMinV = hostMinimum(p);
        const float hostRange = std::max(hostMaximum(p) - hostMinV, 1.0e-6f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;
        const auto ringT = [&](float plain) {
            return std::clamp((hostValueForPlain(p, plain) - hostMinV) / hostRange, 0.0f, 1.0f);
        };
        for (int i = 0; i < minorCount; ++i)
        {
            const float angle = duskdaf::DuskPanel::knobAngle(ringT(minorValues[i]));
            const ImVec2 d(std::sin(angle), -std::cos(angle));
            dl->AddLine(ImVec2(center.x + d.x * (r + 8.0f * scale), center.y + d.y * (r + 8.0f * scale)),
                        ImVec2(center.x + d.x * (r + 13.0f * scale), center.y + d.y * (r + 13.0f * scale)),
                        VcaTheme::ringMinor, 1.1f * scale);
        }
        {
            duskdaf::KnobRingStyle ring;
            ring.markInner = 7.0f;
            ring.markOuter = 15.0f;
            ring.markThickness = 1.7f;
            ring.markColor = kVcaInk;
            // Numerals at the legend size (16). Those left of the dial end at their mark and those right of it
            // start there, so the widest ("300 mV") stay clear of the ticks.
            ring.labelOffset = multicompp::ui_detail::VcaFaceplateLayout::ringLabelOffset;
            ring.labelNudgeY = -9.0f;
            ring.labelSize = 16.0f;
            ring.labelColor = kVcaInk;
            ring.labelCentred = false;
            ring.labelTracking = 0.06f;
            duskdaf::drawKnobRing(panel, dl, x, y, radius,
                                  majorValues, majorLabels, majorCount, nullptr, 0,
                                  ringT, ring);
        }

        // Drop shadow, black serrated skirt, silver dome with radial brushing
        // and a top-left highlight, dark pointer line to the cap edge. The
        // serrations and brushing turn with the pointer; the shadow and
        // highlights are lighting and stay put.
        const float t = std::clamp((values[p] - hostMinV) / hostRange, 0.0f, 1.0f);
        const float pointerAngle = duskdaf::DuskPanel::knobAngle(t);
        dl->AddCircleFilled(ImVec2(center.x + 3.0f * scale, center.y + 4.5f * scale), r * 1.03f, VcaTheme::knobShadow, 64);
        dl->AddCircleFilled(center, r, VcaTheme::skirt, 64);
        for (int tooth = 0; tooth < 72; ++tooth)
        {
            const float a = pointerAngle + 2.0f * kUiPi * static_cast<float>(tooth) / 72.0f;
            const ImVec2 d(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(center.x + d.x * r * 0.90f, center.y + d.y * r * 0.90f),
                        ImVec2(center.x + d.x * r, center.y + d.y * r),
                        (tooth & 1) ? VcaTheme::toothLight : VcaTheme::toothDark, 1.6f * scale);
        }
        const float capR = r * 0.86f;
        dl->AddCircleFilled(center, capR, VcaTheme::capBase, 64);
        dl->AddCircleFilled(ImVec2(center.x, center.y - capR * 0.06f), capR * 0.94f, VcaTheme::capDome, 64);
        for (int spoke = 0; spoke < 96; ++spoke)
        {
            const float a = pointerAngle + 2.0f * kUiPi * static_cast<float>(spoke) / 96.0f;
            const ImVec2 d(std::cos(a), std::sin(a));
            const int shade = (spoke * 7) % 5;
            dl->AddLine(ImVec2(center.x + d.x * capR * 0.18f, center.y + d.y * capR * 0.18f),
                        ImVec2(center.x + d.x * capR * 0.93f, center.y + d.y * capR * 0.93f),
                        shade == 0 ? VcaTheme::brushLight : shade == 2 ? VcaTheme::brushDark : VcaTheme::clear,
                        0.9f * scale);
        }
        dl->AddCircleFilled(ImVec2(center.x - capR * 0.28f, center.y - capR * 0.32f), capR * 0.42f, VcaTheme::capHighlight, 40);
        dl->PathArcTo(center, capR * 0.97f, 0.35f, 2.75f, 30);
        dl->PathStroke(VcaTheme::capShade, 0, 1.6f * scale);
        dl->AddCircle(center, capR, VcaTheme::capRim, 64, 1.1f * scale);
        const ImVec2 pointer(std::sin(pointerAngle), -std::cos(pointerAngle));
        dl->AddLine(ImVec2(center.x + pointer.x * capR * 0.12f, center.y + pointer.y * capR * 0.12f),
                    ImVec2(center.x + pointer.x * capR * 0.97f, center.y + pointer.y * capR * 0.97f),
                    VcaTheme::pointer, 2.6f * scale);
        vcaKnobSplitter.Merge(dl);
    }

    void drawVcaMeterButtons(ImDrawList* dl)
    {
        // Reference layout: source labels above three silver keycaps in black
        // bezels, a square-cornered bracket with METER beneath. Selection is
        // display-only UI state.
        using Layout = multicompp::ui_detail::VcaFaceplateLayout;
        constexpr int count = multicompp::ui_detail::kVcaMeterSourceCount;
        for (int i = 0; i < count; ++i)
        {
            const float bx0 = Layout::keysX + static_cast<float>(i) * (Layout::keyW + Layout::keyGap);
            const float bx1 = bx0 + Layout::keyW;
            const float by0 = Layout::keyY0, by1 = Layout::keyY1;
            // One line each at the legend size; the bezel (by0 - 4) is drawn after it and clears it.
            spacedText(dl, 0.5f * (bx0 + bx1), Layout::keyLegendY, 16.0f, kVcaInk,
                       multicompp::ui_detail::vcaMeterKeyLegend(i), 0, 0.10f);
            char id[32];
            std::snprintf(id, sizeof(id), "##vca_meter_source_%d", i);
            const ImVec2 b0 = panel.P(bx0, by0), b1 = panel.P(bx1, by1);
            ImGui::SetCursorScreenPos(b0);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            const bool active = i == vcaMeterSource;
            const float s = panel.scale();
            // Black bezel with a soft outer shadow, then the keycap.
            dl->AddRectFilled(panel.P(bx0 - 4.0f + 2.0f, by0 - 4.0f + 3.0f), panel.P(bx1 + 4.0f + 2.0f, by1 + 4.0f + 3.0f),
                              VcaTheme::keyShadow, 3.0f * s);
            dl->AddRectFilled(panel.P(bx0 - 4.0f, by0 - 4.0f), panel.P(bx1 + 4.0f, by1 + 4.0f), VcaTheme::bezel, 3.0f * s);
            dl->AddRect(panel.P(bx0 - 4.0f, by0 - 4.0f), panel.P(bx1 + 4.0f, by1 + 4.0f), VcaTheme::keyBezelEdge, 3.0f * s, 0, s);
            const ImU32 capHi = active ? VcaTheme::keyLitTop : VcaTheme::keyTop;
            const ImU32 capLo = active ? VcaTheme::keyLitBottom : VcaTheme::keyBottom;
            dl->AddRectFilledMultiColor(b0, b1, capHi, capHi, capLo, capLo);
            if (active)
                dl->AddRectFilledMultiColor(b0, panel.P(bx1, by0 + 8.0f), VcaTheme::keyLitShade, VcaTheme::keyLitShade,
                                            VcaTheme::clear, VcaTheme::clear);
            else
                dl->AddLine(panel.P(bx0 + 3.0f, by0 + 2.5f), panel.P(bx1 - 3.0f, by0 + 2.5f), VcaTheme::keyHighlight, 1.3f * s);
            if (ImGui::IsItemHovered() && !active)
                dl->AddRectFilled(b0, b1, VcaTheme::keyHover, 2.0f * s);
            dl->AddRect(b0, b1, VcaTheme::keyEdge, 2.0f * s, 0, s);
            if (clicked) vcaMeterSource = i;
        }
        // Square-ended bracket with METER in the gap.
        constexpr float bx0 = Layout::keysX - 4.0f;
        constexpr float bx1 = Layout::keysX + count * Layout::keyW + (count - 1) * Layout::keyGap + 4.0f;
        constexpr float byy = Layout::keyBracketY;
        const float s = panel.scale();
        constexpr float gap = 36.0f;
        constexpr float mid = 0.5f * (bx0 + bx1);
        dl->AddLine(panel.P(bx0, byy), panel.P(mid - gap, byy), kVcaInk, 2.0f * s);
        dl->AddLine(panel.P(mid + gap, byy), panel.P(bx1, byy), kVcaInk, 2.0f * s);
        dl->AddLine(panel.P(bx0, byy - 9.0f), panel.P(bx0, byy), kVcaInk, 2.0f * s);
        dl->AddLine(panel.P(bx1, byy - 9.0f), panel.P(bx1, byy), kVcaInk, 2.0f * s);
        spacedText(dl, mid, byy - 8.0f, 16.0f, kVcaInk, "METER", 0);
    }

    float levelMeterDb(bool output) const
    {
        void* instance = getPluginInstancePointer();
        if (instance != nullptr)
        {
            if (output && multiCompGetOutputLevel != nullptr)
                return multiCompGetOutputLevel(instance);
            if (!output && multiCompGetInputLevel != nullptr)
                return multiCompGetInputLevel(instance);
        }
        return -60.0f;
    }

    // The BUS face's colours by role, sampled from the face as it was drawn before the tokens.
    struct BusTheme
    {
        // The panel: rack frame, powder-coated plate and its grain, screws, legend ink and the footer plate
        static constexpr ImU32 rack = IM_COL32(43, 47, 45, 255);
        static constexpr ImU32 plateTopLeft = IM_COL32(94, 100, 97, 255);
        static constexpr ImU32 plateTopRight = IM_COL32(77, 83, 80, 255);
        static constexpr ImU32 plateBottomRight = IM_COL32(49, 54, 52, 255);
        static constexpr ImU32 plateBottomLeft = IM_COL32(66, 71, 68, 255);
        static constexpr ImU32 grainLight = IM_COL32(232, 236, 230, 23);
        static constexpr ImU32 grainDark = IM_COL32(0, 0, 0, 30);
        static constexpr ImU32 screwHead = IM_COL32(26, 29, 26, 255);
        static constexpr ImU32 screwRim = IM_COL32(142, 147, 140, 255);
        static constexpr ImU32 screwSlot = IM_COL32(138, 143, 135, 255);
        static constexpr ImU32 ink = IM_COL32(235, 237, 233, 255);
        static constexpr ImU32 footerPlate = IM_COL32(57, 62, 59, 255);
        // The meter
        static constexpr ImU32 meterSurround = IM_COL32(119, 124, 115, 255);
        static constexpr ImU32 meterBezelLight = IM_COL32(80, 83, 79, 255);
        static constexpr ImU32 meterBezelDark = IM_COL32(14, 17, 14, 255);
        static constexpr ImU32 meterBezelLine = IM_COL32(15, 17, 15, 255);
        static constexpr ImU32 meterLip = IM_COL32(22, 24, 22, 255);
        static constexpr ImU32 meterFace = IM_COL32(26, 28, 26, 255);
        static constexpr ImU32 meterFaceTopTint = IM_COL32(69, 72, 68, 100);
        static constexpr ImU32 meterFaceBottomTint = IM_COL32(12, 14, 12, 80);
        static constexpr ImU32 meterInk = IM_COL32(188, 194, 184, 255);
        static constexpr ImU32 meterNeedle = IM_COL32(244, 246, 239, 255);
        // The knobs: scale dots and numerals, shadow, ribbed skirt, cap, pointer and legend
        static constexpr ImU32 scaleDot = IM_COL32(218, 220, 218, 255);
        static constexpr ImU32 scaleLabel = IM_COL32(224, 226, 223, 255);
        static constexpr ImU32 knobShadow = IM_COL32(0, 0, 0, 110);
        static constexpr ImU32 skirt = IM_COL32(86, 89, 87, 255);
        static constexpr ImU32 rib = IM_COL32(144, 148, 144, 255);           // the skirt's ribs and the cap
        static constexpr ImU32 capRing = IM_COL32(192, 196, 191, 180);
        static constexpr ImU32 capFace = IM_COL32(165, 169, 164, 255);
        static constexpr ImU32 pointer = IM_COL32(20, 23, 21, 255);
        static constexpr ImU32 knobLegend = IM_COL32(239, 240, 236, 255);
        // The IN and FADE buttons: well, frame, lamp, lens and caption
        static constexpr ImU32 buttonWell = IM_COL32(18, 20, 18, 255);
        static constexpr ImU32 buttonEdge = IM_COL32(107, 110, 104, 255);
        static constexpr ImU32 buttonFrameTopLeft = IM_COL32(177, 180, 168, 255);
        static constexpr ImU32 buttonFrameTopRight = IM_COL32(93, 96, 85, 255);
        static constexpr ImU32 buttonFrameBottomRight = IM_COL32(27, 29, 25, 255);
        static constexpr ImU32 buttonFrameBottomLeft = IM_COL32(81, 84, 75, 255);
        static constexpr ImU32 lampLit = IM_COL32(246, 209, 102, 255);
        static constexpr ImU32 lamp = IM_COL32(90, 95, 88, 255);
        static constexpr ImU32 lensLitTop = IM_COL32(255, 255, 241, 255);
        static constexpr ImU32 lensLitBottom = IM_COL32(255, 245, 189, 255);
        static constexpr ImU32 lensTopLeft = IM_COL32(206, 210, 201, 255);
        static constexpr ImU32 lensTopRight = IM_COL32(196, 200, 191, 255);
        static constexpr ImU32 lensBottomRight = IM_COL32(145, 151, 141, 255);
        static constexpr ImU32 lensBottomLeft = IM_COL32(153, 158, 149, 255);
        static constexpr ImU32 buttonLabel = IM_COL32(46, 48, 39, 255);
        // The HR set screw (busTrim): shadow, nut, head, face, highlight and slot
        static constexpr ImU32 trimShadow = IM_COL32(0, 0, 0, 140);
        static constexpr ImU32 trimNut = IM_COL32(28, 28, 28, 255);
        static constexpr ImU32 trimNutEdge = IM_COL32(70, 71, 70, 255);
        static constexpr ImU32 trimHead = IM_COL32(158, 160, 158, 255);
        static constexpr ImU32 trimFace = IM_COL32(206, 208, 206, 255);
        static constexpr ImU32 trimHighlight = IM_COL32(250, 251, 248, 120);
        static constexpr ImU32 trimSlot = IM_COL32(34, 35, 34, 255);
    };

    void busKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                 const char* label, const char* const* labels, int count,
                 bool discrete, const char* suffix = "")
    {
        using Layout = multicompp::ui_detail::BusFaceplateLayout;
        const float s = panel.scale(), radius = Layout::knobRadius;
        // Preserve the wider legacy host ranges for existing sessions while the
        // face and its gestures follow the reference's +/-15 and 0..15 dB travel.
        const float lo = p == P_BUS_THRESHOLD ? -15.0f : hostMinimum(p);
        const float hi = p == P_BUS_MAKEUP ? 15.0f : hostMaximum(p);
        busKnobSplitter.Split(dl, 2);
        busKnobSplitter.SetCurrentChannel(dl, 1);
        const int choice = multicompp::ui_detail::choiceIndex(values[p], count);
        const char* choiceText = p == P_BUS_ATTACK ? multicompp::kBusAttack[choice]
            : p == P_BUS_RELEASE ? multicompp::kBusRelease[choice]
            : p == P_BUS_RATIO ? multicompp::kBusRatios[choice] : nullptr;
        beginTrackedControl(p, id, true);
        panel.knob(id, p, lo, hi, x, y, radius, values[p], hostDefaultValue(p),
                   discrete, false, discrete ? "%.0f" : "%.1f", suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label, !discrete,
                   choiceText, false, 0.0f, 0.0f, false, true, 10.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, !discrete);
        endTrackedControl();
        // These are switches. Offer their actual timing/ratio labels instead of
        // a numeric editor that would interpret a typed value as an enum index.
        if (discrete)
        {
            ImGui::PushID(id);
            if (ImGui::BeginPopupContextItem("bus_switch"))
            {
                const char* const* choices = p == P_BUS_ATTACK ? multicompp::kBusAttack
                    : p == P_BUS_RELEASE ? multicompp::kBusRelease : multicompp::kBusRatios;
                for (int i = 0; i < count; ++i)
                    if (ImGui::MenuItem(choices[i], nullptr, i == choice))
                        setHostValue(p, static_cast<float>(i));
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        busKnobSplitter.SetCurrentChannel(dl, 0);
        // Switches use the narrow sweep printed on the console knobs. The
        // continuously variable controls use the full 270 degree travel.
        const float half = discrete ? 65.0f : 135.0f;
        const auto angle = [half](float t) { return (-half + 2.0f * half * t) * kUiPi / 180.0f; };
        const int dots = discrete ? count : 9;
        for (int i = 0; i < dots; ++i)
        {
            const float a = angle(static_cast<float>(i) / (dots - 1));
            dl->AddCircleFilled(panel.P(x + Layout::dotRadius * std::sin(a), y - Layout::dotRadius * std::cos(a)),
                                 2.1f * s, BusTheme::scaleDot, 12);
        }
        for (int i = 0; i < count; ++i)
        {
            float t = static_cast<float>(i) / (count - 1);
            if (p == P_BUS_FADE_RATE)
            {
                static constexpr float rates[] = {1, 3, 5, 10, 20, 40, 60};
                t = (hostValueForPlain(p, rates[i]) - lo) / (hi - lo);
            }
            else if (p == P_BUS_SC && i == 1)
                t = (hostValueForPlain(p, 80.0f) - lo) / (hi - lo);
            // At the legend size (16); those left of the knob end at their anchor and those right of it start
            // there, so the wider ones clear the dots.
            const float a = angle(t), dx = std::sin(a);
            panel.text(dl, x + Layout::labelRadius * dx, y - Layout::labelRadius * std::cos(a) - 8.0f, 16.0f,
                       BusTheme::scaleLabel, labels[i], dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0), true);
        }
        const ImVec2 c = panel.P(x, y);
        // The skirt ribs turn with the pointer, so the whole knob reads as
        // one turning part; the drop shadow stays put.
        const float t = std::clamp((values[p] - lo) / std::max(hi - lo, 0.001f), 0.0f, 1.0f);
        const float a = angle(t);
        dl->AddCircleFilled(panel.P(x + 3, y + 4), 26.0f * s, BusTheme::knobShadow, 48);
        dl->AddCircleFilled(c, 25.0f * s, BusTheme::skirt, 48);
        for (int i = 0; i < 16; ++i)
        {
            const float rib = a + i * 2.0f * kUiPi / 16.0f;
            dl->AddLine(panel.P(x + 21.0f * std::sin(rib), y - 21.0f * std::cos(rib)),
                        panel.P(x + 24.5f * std::sin(rib), y - 24.5f * std::cos(rib)),
                        BusTheme::rib, 3.0f * s);
        }
        dl->AddCircleFilled(c, 21.0f * s, BusTheme::rib, 48);
        dl->AddCircle(c, 19.5f * s, BusTheme::capRing, 48, 1.2f * s);
        dl->AddCircleFilled(c, 18.0f * s, BusTheme::capFace, 48);
        dl->AddLine(panel.P(x + 3.0f * std::sin(a), y - 3.0f * std::cos(a)),
                    panel.P(x + 20.0f * std::sin(a), y - 20.0f * std::cos(a)),
                    BusTheme::pointer, 2.4f * s);
        panel.text(dl, x, y + Layout::legendDrop, 16.0f, BusTheme::knobLegend, label, 0, true);
        if (values[p] < lo || values[p] > hi)
        {
            char readout[40];
            std::snprintf(readout, sizeof(readout), "%.1f%s", plainValueForHost(p, values[p]), suffix);
            panel.text(dl, x, y + Layout::readoutDrop, 17.0f, BusTheme::knobLegend, readout, 0, true);
        }
        busKnobSplitter.Merge(dl);
    }

    void busButton(ImDrawList* dl, const char* id, uint32_t p, float x, float y,
                   const char* label, bool inverted = false)
    {
        const float s = panel.scale();
        bool on = (values[p] >= 0.5f) != inverted;
        if (p == P_BUS_FADE && multiCompGetBusFadePosition != nullptr)
        {
            const float position = multiCompGetBusFadePosition(getPluginInstancePointer());
            const bool moving = on ? position < 1.0f : position > 0.0f;
            if (moving) on = std::fmod(ImGui::GetTime(), 0.6) < 0.3;
        }
        ImGui::SetCursorScreenPos(panel.P(x - 29, y - 29));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(58 * s, 58 * s));
        dl->AddRectFilled(panel.P(x - 33, y - 33), panel.P(x + 33, y + 33), BusTheme::buttonWell);
        dl->AddRect(panel.P(x - 33, y - 33), panel.P(x + 33, y + 33), BusTheme::buttonEdge, 0, 0, 2 * s);
        dl->AddRectFilledMultiColor(panel.P(x - 28, y - 28), panel.P(x + 28, y + 28),
                                    BusTheme::buttonFrameTopLeft, BusTheme::buttonFrameTopRight,
                                    BusTheme::buttonFrameBottomRight, BusTheme::buttonFrameBottomLeft);
        dl->AddRectFilled(panel.P(x - 23, y - 23), panel.P(x + 23, y + 23),
                          on ? BusTheme::lampLit : BusTheme::lamp);
        dl->AddRectFilledMultiColor(panel.P(x - 20, y - 20), panel.P(x + 20, y + 20),
                                    on ? BusTheme::lensLitTop : BusTheme::lensTopLeft,
                                    on ? BusTheme::lensLitTop : BusTheme::lensTopRight,
                                    on ? BusTheme::lensLitBottom : BusTheme::lensBottomRight,
                                    on ? BusTheme::lensLitBottom : BusTheme::lensBottomLeft);
        panel.text(dl, x, y - 9, 16.0f, BusTheme::buttonLabel, label, 0, true);
        if (clicked) setValue(p, values[p] >= 0.5f ? 0.0f : 1.0f);
    }

    // The BUS face's HR set screw (once the VCA face's MIX). Same gesture
    // contract as fetTrimKnob; label optional, under it like the face's knobs.
    void busTrim(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float radius,
                 const char* label, const char* format, const char* suffix)
    {
        busKnobSplitter.Split(dl, 2);
        busKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius,
                   values[p], hostDefaultValue(p), false, false, format, suffix,
                   0, true, false, nullptr, false, 1.0f, 0.0f, label ? label : "MIX", true,
                   nullptr, false, 0.0f, 0.0f, false, true, 8.0f, true, false,
                   &knobHostToPlain, &knobPlainToHost, this, true);
        endTrackedControl();
        busKnobSplitter.SetCurrentChannel(dl, 0);
        const float hostMinV = hostMinimum(p);
        const float hostRange = std::max(hostMaximum(p) - hostMinV, 1.0e-6f);
        const float t = std::clamp((values[p] - hostMinV) / hostRange, 0.0f, 1.0f);
        const float scale = panel.scale();
        const ImVec2 center = panel.P(x, y);
        const float r = radius * scale;
        dl->AddCircleFilled(ImVec2(center.x + 1.5f * scale, center.y + 2.5f * scale), r * 1.35f, BusTheme::trimShadow, 32);
        dl->AddCircleFilled(center, r * 1.3f, BusTheme::trimNut, 32);
        dl->AddCircle(center, r * 1.3f, BusTheme::trimNutEdge, 32, scale);
        dl->AddCircleFilled(center, r, BusTheme::trimHead, 32);
        dl->AddCircleFilled(center, r * 0.8f, BusTheme::trimFace, 32);
        dl->PathArcTo(center, r * 0.62f, -2.45f, -0.75f, 18);
        dl->PathStroke(BusTheme::trimHighlight, 0, scale);
        const float angle = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pointer(std::sin(angle), -std::cos(angle));
        dl->AddLine(ImVec2(center.x - pointer.x * r * 0.62f, center.y - pointer.y * r * 0.62f),
                    ImVec2(center.x + pointer.x * r * 0.62f, center.y + pointer.y * r * 0.62f),
                    BusTheme::trimSlot, 1.8f * scale);
        if (label != nullptr)
            panel.text(dl, x, y + multicompp::ui_detail::BusFaceplateLayout::legendDrop, 16.0f, BusTheme::knobLegend,
                       label, 0, true);
        busKnobSplitter.Merge(dl);
    }

    void drawBus(ImDrawList* dl)
    {
        using Layout = multicompp::ui_detail::BusFaceplateLayout;
        const float s = panel.scale();
        dl->AddRectFilled(panel.P(Layout::left, Layout::top), panel.P(Layout::right, Layout::bottom), BusTheme::rack);
        constexpr float plateX0 = Layout::left + Layout::ear, plateX1 = Layout::right - Layout::ear;
        constexpr float plateY0 = Layout::top + 2.0f, plateY1 = Layout::bottom - 3.0f;
        dl->AddRectFilledMultiColor(panel.P(plateX0, plateY0), panel.P(plateX1, plateY1),
                                    BusTheme::plateTopLeft, BusTheme::plateTopRight,
                                    BusTheme::plateBottomRight, BusTheme::plateBottomLeft);
        // Fine deterministic powder-coat grain; remains static while meters move.
        // 1500 quads (6k vertices) keeps the whole panel well inside ImGui's
        // 16-bit index space; 9000 pushed the draw list toward the limit.
        uint32_t grain = 0x534c4742u;
        for (int i = 0; i < 1500; ++i)
        {
            grain = grain * 1664525u + 1013904223u;
            const float x = plateX0 + 2.0f + static_cast<float>(grain & 65535u) * ((plateX1 - plateX0 - 4.0f) / 65535.0f);
            grain = grain * 1664525u + 1013904223u;
            const float y = plateY0 + 1.0f + static_cast<float>(grain & 65535u) * ((plateY1 - plateY0 - 3.0f) / 65535.0f);
            dl->AddRectFilled(panel.P(x, y), panel.P(x + 0.65f, y + 0.65f),
                              (grain & 65536u) ? BusTheme::grainLight : BusTheme::grainDark);
        }
        for (float x : {0.5f * Layout::ear, Layout::right - 0.5f * Layout::ear - 1.0f})
            for (float y : {Layout::top + 18.0f, Layout::bottom - 18.0f})
            {
                dl->AddCircleFilled(panel.P(x, y), 6.0f * s, BusTheme::screwHead, 24);
                dl->AddCircle(panel.P(x, y), 6.0f * s, BusTheme::screwRim, 24, s);
                dl->AddLine(panel.P(x - 3, y + 3), panel.P(x + 3, y - 3), BusTheme::screwSlot, 1.4f * s);
            }
        // The silk-screened frame, its top edge carrying the name over the meter and its bottom edge the series line
        // under the IN switch, each on a plate that breaks the line.
        dl->AddRect(panel.P(Layout::frameX0, Layout::frameY0), panel.P(Layout::frameX1, Layout::frameY1),
                    BusTheme::ink, 6 * s, 0, 1.8f * s);
        const auto edgeLabel = [&](float y, float size, const char* text) {
            const float px = size * s;
            const float halfWidth = 0.5f * panel.pickFont(px)->CalcTextSizeA(px, FLT_MAX, 0.0f, text).x / s + 12.0f;
            dl->AddRectFilled(panel.P(Layout::columnX - halfWidth, y - 2.0f),
                              panel.P(Layout::columnX + halfWidth, y + size + 2.0f), BusTheme::footerPlate);
            panel.text(dl, Layout::columnX, y, size, BusTheme::ink, text, 0, true);
        };
        edgeLabel(Layout::titleY, 22.0f, "BUS COMPRESSOR");
        edgeLabel(Layout::seriesY, 13.0f, "STEREO BUS COMPRESSION");

        static constexpr duskdaf::VuTick ticks[] = {
            {0, "0", true}, {4, "4", true}, {8, "8", true},
            {12, "12", true}, {16, "16", true}, {20, "20", true}};
        duskdaf::VuScaleConfig meterScale;
        meterScale.ticks = ticks; meterScale.tickCount = 6;
        meterScale.minDb = 0; meterScale.maxDb = 20; meterScale.redFromDb = 100;
        meterScale.legend = "dB"; meterScale.sublabel = "COMPRESSION";
        duskdaf::VuStyle meterStyle;
        meterStyle.bezelLight = BusTheme::meterBezelLight;
        meterStyle.bezelDark = BusTheme::meterBezelDark;
        meterStyle.bezelLine = BusTheme::meterBezelLine;
        meterStyle.lip = BusTheme::meterLip;
        meterStyle.faceBase = BusTheme::meterFace;
        meterStyle.faceTopTint = BusTheme::meterFaceTopTint;
        meterStyle.faceBottomTint = BusTheme::meterFaceBottomTint;
        meterStyle.ink = BusTheme::meterInk;
        meterStyle.sublabelColor = meterStyle.ink;
        meterStyle.needle = BusTheme::meterNeedle;
        meterStyle.overloadArc = false; meterStyle.cornerMarks = false;
        meterStyle.pivotBelowFace = 15; meterStyle.sweepHalfAngleDeg = 45;
        meterStyle.smoothNeedle = false;
        meterStyle.maximumNeedleDeflection = 1.1f;
        meterScale.deflection = [](float db, const duskdaf::VuScaleConfig&) {
            return std::clamp(db / 20.0f, 0.0f, 1.1f);
        };
        auto* instance = getPluginInstancePointer();
        const float busReading = instance && multiCompGetBusMeterReading != nullptr
            ? multiCompGetBusMeterReading(instance) : -meter(kMeterMaster);
        // The scale's numerals at 14, dB at the legend size (16) and COMPRESSION at 13 (the readability floor).
        meterStyle.radiusTopInset = 22; meterStyle.tickLabelRadiusFrac = 0.99f;
        meterStyle.majorTickLength = -17; meterStyle.tickLabelSize = 14;
        // The legend high enough that the needle at rest clears COMPRESSION under it.
        meterStyle.legendOffset = 0.62f; meterStyle.legendSize = 16; meterStyle.sublabelSize = 13;
        duskdaf::drawVuMeter(panel, dl, Layout::meterX0, Layout::meterY0, Layout::meterX1, Layout::meterY1,
                             busReading, busVuNeedle, meterScale,
                             BusTheme::meterSurround, meterStyle);
        busButton(dl, "bus_in", P_BYPASS, Layout::columnX, Layout::inY, "IN", true);

        static constexpr const char* threshold[] = {"-15", "0", "+15"};
        static constexpr const char* makeup[] = {"0", "7.5", "+15"};
        static constexpr const char* ratios[] = {"2", "4", "10"};
        static constexpr const char* attacks[] = {".1", ".3", "1", "3", "10", "30"};
        static constexpr const char* releases[] = {".1", ".3", ".6", "1.2", "Auto"};
        static constexpr const char* sc[] = {"OFF", "80", "500"};
        static constexpr const char* mix[] = {"0", "50", "100"};
        static constexpr const char* rates[] = {"1", "3", "5", "10", "20", "40", "60"};
        constexpr float row1 = Layout::row1Y, row2 = Layout::row2Y;
        busKnob(dl, "bus_thr", P_BUS_THRESHOLD, Layout::col0, row1, "THRESHOLD", threshold, 3, false, " dB");
        busKnob(dl, "bus_ratio", P_BUS_RATIO, Layout::col1, row1, "RATIO", ratios, 3, true, ":1");
        busKnob(dl, "bus_makeup", P_BUS_MAKEUP, Layout::col2, row1, "MAKE-UP", makeup, 3, false, " dB");
        busKnob(dl, "bus_sc", P_BUS_SC, Layout::col3, row1, "SC FILTER Hz", sc, 3, false, " Hz");
        busKnob(dl, "bus_mix", P_BUS_MIX, Layout::col4, row1, "MIX %", mix, 3, false, "%");
        busKnob(dl, "bus_attack", P_BUS_ATTACK, Layout::col0, row2, "ATTACK ms", attacks, 6, true);
        busKnob(dl, "bus_release", P_BUS_RELEASE, Layout::col1, row2, "RELEASE s", releases, 5, true);
        busTrim(dl, "bus_hr", P_BUS_HEADROOM, Layout::col2, row2, Layout::trimRadius, "HR dB", "%.0f", " dB");
        busKnob(dl, "bus_rate", P_BUS_FADE_RATE, Layout::col3, row2, "RATE s", rates, 7, false, " s");
        busButton(dl, "bus_fade", P_BUS_FADE, Layout::col4, row2, "FADE");
        panel.text(dl, Layout::col4, row2 + Layout::legendDrop, 16.0f, BusTheme::ink, "AUTO FADE", 0, true);
    }

    // Studio VCA face: the reference studio VCA compressor's panel, laid out from its own proportions (its 1776 x
    // 568 px panel onto the 1120 x 310 face: x 0.631 and y 0.546 per px; round parts use the y scale). The dials
    // follow the reference's travel and printed scales (ui_detail::studioVcaDialPosition); the meters are its I/O
    // meter (INPUT/OUTPUT switch, threshold mark on INPUT) and gain reduction meter. Label STUDIO VCA, no marks.
    struct StudioVcaMark { float plain; const char* label; };

    // The Studio VCA face's colours by role, sampled from the face as it was drawn before the tokens.
    struct StudioVcaTheme
    {
        // The red brushed panel, its edges, rack-ear slots and the engraved label; the S-curve
        static constexpr ImU32 panelTopLeft = IM_COL32(214, 46, 38, 255);
        static constexpr ImU32 panelTopRight = IM_COL32(196, 38, 32, 255);
        static constexpr ImU32 panelBottomRight = IM_COL32(150, 24, 22, 255);
        static constexpr ImU32 panelBottomLeft = IM_COL32(168, 30, 26, 255);
        static constexpr ImU32 brushLight = IM_COL32(255, 170, 160, 255);   // each hairline sets its own alpha
        static constexpr ImU32 brushDark = IM_COL32(60, 0, 0, 255);
        static constexpr ImU32 topEdge = IM_COL32(255, 150, 135, 150);
        static constexpr ImU32 bottomEdge = IM_COL32(70, 6, 6, 220);
        static constexpr ImU32 earSlot = IM_COL32(28, 4, 4, 255);
        static constexpr ImU32 earSlotRim = IM_COL32(255, 130, 115, 90);
        static constexpr ImU32 engraveHighlight = IM_COL32(255, 135, 120, 120);
        static constexpr ImU32 engraveShadow = IM_COL32(85, 8, 8, 230);
        static constexpr ImU32 engraveFace = IM_COL32(176, 28, 25, 255);
        static constexpr ImU32 curveHighlight = IM_COL32(255, 120, 105, 70);
        static constexpr ImU32 curve = IM_COL32(70, 6, 6, 230);
        static constexpr ImU32 ink = IM_COL32(246, 232, 226, 255);          // legends, numerals and scale dots
        // The dials: well, chrome bands and highlights, grip bar and its index cup
        static constexpr ImU32 wellRim = IM_COL32(255, 128, 112, 140);
        static constexpr ImU32 wellDark = IM_COL32(22, 5, 7, 255);
        static constexpr ImU32 wellLight = IM_COL32(58, 15, 17, 255);
        static constexpr ImU32 chromePanelShaded = IM_COL32(146, 126, 121, 255);
        static constexpr ImU32 chromePanelLit = IM_COL32(186, 166, 161, 255);
        static constexpr ImU32 chromeHorizonShaded = IM_COL32(126, 120, 118, 255);
        static constexpr ImU32 chromeHorizonLit = IM_COL32(196, 190, 187, 255);
        static constexpr ImU32 chromeSilverShaded = IM_COL32(198, 193, 190, 255);
        static constexpr ImU32 chromeSilverLit = IM_COL32(222, 218, 215, 255);
        static constexpr ImU32 chromeHalo = IM_COL32(255, 255, 255, 60);
        static constexpr ImU32 chromeHighlight = IM_COL32(251, 251, 249, 240);
        static constexpr ImU32 chromeRimLit = IM_COL32(255, 250, 246, 150);
        static constexpr ImU32 chromeRimShaded = IM_COL32(60, 30, 28, 150);
        static constexpr ImU32 barShadow = IM_COL32(24, 10, 8, 80);
        static constexpr ImU32 barShaded = IM_COL32(106, 100, 90, 255);
        static constexpr ImU32 barLit = IM_COL32(136, 130, 119, 255);
        static constexpr ImU32 barEdgeLitFaint = IM_COL32(240, 236, 226, 70);
        static constexpr ImU32 barEdgeLit = IM_COL32(240, 236, 226, 240);
        static constexpr ImU32 barEdgeShadedFaint = IM_COL32(28, 24, 20, 70);
        static constexpr ImU32 barEdgeShaded = IM_COL32(28, 24, 20, 220);
        static constexpr ImU32 cupGroove = IM_COL32(40, 32, 26, 150);
        static constexpr ImU32 cup = IM_COL32(246, 238, 216, 255);
        // The AUTO RELEASE lamp and the INPUT/OUTPUT switch
        static constexpr ImU32 lampGlow = IM_COL32(255, 70, 50, 70);
        static constexpr ImU32 lampWell = IM_COL32(52, 4, 4, 255);
        static constexpr ImU32 lampLitHigh = IM_COL32(255, 150, 130, 255);
        static constexpr ImU32 lampLitLow = IM_COL32(220, 20, 16, 255);
        static constexpr ImU32 lampHigh = IM_COL32(170, 40, 36, 255);
        static constexpr ImU32 lampLow = IM_COL32(92, 10, 10, 255);
        static constexpr ImU32 lampSpecularLit = IM_COL32(255, 235, 225, 200);
        static constexpr ImU32 lampSpecular = IM_COL32(255, 160, 150, 70);
        static constexpr ImU32 switchWell = IM_COL32(24, 4, 4, 255);
        static constexpr ImU32 switchRim = IM_COL32(255, 120, 105, 80);
        static constexpr ImU32 ballLit = IM_COL32(255, 196, 120, 255);
        static constexpr ImU32 ballShade = IM_COL32(200, 92, 24, 255);
        // The round meters: shadow, bezel, window, scale, needles and glass
        static constexpr ImU32 meterShadow = IM_COL32(60, 0, 0, 150);
        static constexpr ImU32 bezelLit = IM_COL32(150, 24, 22, 255);
        static constexpr ImU32 bezelShade = IM_COL32(70, 6, 6, 255);
        static constexpr ImU32 bezelRing = IM_COL32(255, 120, 105, 60);
        static constexpr ImU32 faceRim = IM_COL32(16, 10, 8, 255);
        static constexpr ImU32 faceDark = IM_COL32(38, 26, 20, 255);
        static constexpr ImU32 window = IM_COL32(236, 204, 150, 255);
        static constexpr ImU32 windowGlow = IM_COL32(255, 196, 110, 60);
        static constexpr ImU32 scaleInk = IM_COL32(30, 20, 12, 255);
        static constexpr ImU32 thresholdMark = IM_COL32(214, 30, 24, 255);
        static constexpr ImU32 needle = IM_COL32(16, 14, 12, 255);
        static constexpr ImU32 glassTopLeft = IM_COL32(255, 255, 255, 40);
        static constexpr ImU32 glassTopRight = IM_COL32(255, 255, 255, 10);
        static constexpr ImU32 glassBottomRight = IM_COL32(255, 255, 255, 0);
        static constexpr ImU32 glassBottomLeft = IM_COL32(255, 255, 255, 20);
    };

    void drawStudioVca(ImDrawList* dl)
    {
        using Layout = multicompp::ui_detail::StudioVcaFaceplateLayout;
        drawStudioVcaPanel(dl);
        const float s = panel.scale();
        // The engraved S-curve across the meter bay, from under the I/O meter's switch to above the gain reduction
        // meter, behind both meters.
        constexpr float curveY0 = Layout::bottom - 26.0f, curveY1 = Layout::top + 19.0f;
        constexpr float curveX0 = Layout::ioX - 30.0f, curveX1 = Layout::grX + 43.0f;   // clear of OUTPUT
        dl->AddBezierCubic(panel.P(curveX0 + 1, curveY0 + 1), panel.P(curveX0 + 201, curveY0 + 1),
                           panel.P(curveX1 - 195, curveY1 + 1), panel.P(curveX1 + 1, curveY1 + 1),
                           StudioVcaTheme::curveHighlight, 2.6f * s, 48);
        dl->AddBezierCubic(panel.P(curveX0, curveY0), panel.P(curveX0 + 200, curveY0),
                           panel.P(curveX1 - 196, curveY1), panel.P(curveX1, curveY1),
                           StudioVcaTheme::curve, 1.7f * s, 48);

        static constexpr StudioVcaMark inputMarks[] = {{-18, "-18"}, {-12, "-12"}, {-6, "-6"}, {0, "0"},
                                                       {6, "+6"}, {12, "+12"}, {18, "+18"}};
        static constexpr StudioVcaMark ratioMarks[] = {{1.5f, "1.5"}, {2, "2"}, {3, "3"}, {5, "5"},
                                                       {10, "10"}, {50, nullptr}};   // nullptr: the infinity mark
        static constexpr StudioVcaMark thresholdMarks[] = {{-50, "-50"}, {-40, "-40"}, {-30, "-30"},
                                                           {-20, "-20"}, {-10, "-10"}};
        static constexpr StudioVcaMark attackMarks[] = {{0.1805f, "FAST"}, {70.6865f, "SLOW"}};
        static constexpr StudioVcaMark releaseMarks[] = {{100, "0.1"}, {1000, "1"}, {2000, "2"}, {3000, "3"},
                                                         {4000, "4"}};
        static constexpr StudioVcaMark makeupMarks[] = {{0, "0"}, {10, "10"}, {20, "20"}, {30, "30"}, {40, "40"}};
        static constexpr StudioVcaMark mixMarks[] = {{0, "0"}, {100, "100"}};
        constexpr float row1 = Layout::row1Y, row2 = Layout::row2Y, r = Layout::dialRadius;
        studioVcaKnob(dl, "sv_in", P_SVCA_INPUT, Layout::inputX, row2, r, "INPUT", inputMarks, 7);
        studioVcaKnob(dl, "sv_ratio", P_SVCA_RATIO, Layout::ratioX, row1, r, "RATIO", ratioMarks, 6);
        studioVcaKnob(dl, "sv_att", P_SVCA_ATTACK, Layout::ratioX, row2, r, "ATTACK", attackMarks, 2);
        studioVcaKnob(dl, "sv_thr", P_SVCA_THRESHOLD, Layout::thresholdX, row1, r, "THRESHOLD", thresholdMarks, 5);
        studioVcaKnob(dl, "sv_rel", P_SVCA_RELEASE, Layout::thresholdX, row2, r, "RELEASE", releaseMarks, 5);
        studioVcaKnob(dl, "sv_out", P_SVCA_OUT, Layout::makeupX, row1, r, "MAKE UP GAIN", makeupMarks, 5);
        studioVcaKnob(dl, "sv_mix", P_MIX, Layout::makeupX, row2, Layout::mixRadius, "DRY/WET", mixMarks, 2);
        // The legends at 16, one line each, level across a row.
        const auto legend = [&](float x, float y, const char* text) {
            panel.text(dl, x, y + Layout::legendDrop, 16.0f, StudioVcaTheme::ink, text, 0, true);
        };
        legend(Layout::ratioX, row1, "RATIO");
        legend(Layout::thresholdX, row1, "THRESHOLD");
        legend(Layout::makeupX, row1, "MAKE UP GAIN");
        legend(Layout::inputX, row2, "INPUT");
        legend(Layout::ratioX, row2, "ATTACK");
        legend(Layout::thresholdX, row2, "RELEASE");
        legend(Layout::makeupX, row2, "DRY/WET");
        legend(Layout::grX, row2, "GAIN REDUCTION");

        studioVcaLamp(dl, "sv_auto", P_SVCA_AUTO, Layout::lampX, Layout::lampY);
        // Two lines, as the reference prints it: on one the legend ran into RELEASE's numerals.
        panel.text(dl, Layout::lampX, Layout::lampY + 20.0f, 16.0f, StudioVcaTheme::ink, "AUTO", 0, true);
        panel.text(dl, Layout::lampX, Layout::lampY + 36.0f, 16.0f, StudioVcaTheme::ink, "RELEASE", 0, true);

        // I/O meter: the input after the Input control, or the output; on INPUT a red mark shows the threshold.
        // Every mark has its tick and every other its numeral: all of them at 14 overlap on the 60 degree scale.
        static constexpr StudioVcaMark ioScale[] = {{-60, "-60"}, {-50, nullptr}, {-40, "-40"}, {-30, nullptr},
                                                    {-20, "-20"}, {-10, nullptr}, {0, "0"}};
        static constexpr StudioVcaMark grScale[] = {{-20, "-20"}, {-15, nullptr}, {-10, "-10"}, {-5, nullptr},
                                                    {0, "0"}};
        void* const instance = getPluginInstancePointer();
        const bool outputMeter = values[P_SVCA_METER] >= 0.5f;
        const auto level = outputMeter ? multiCompGetStudioVcaOutputLevel : multiCompGetStudioVcaInputLevel;
        const float ioDb = instance != nullptr && level != nullptr ? level(instance) : -120.0f;
        const float threshold = plainValueForHost(P_SVCA_THRESHOLD, values[P_SVCA_THRESHOLD]);
        studioVcaMeter(dl, Layout::ioX, Layout::ioY, ioScale, 7, -60.0f, 0.0f, ioDb,
                       outputMeter ? std::numeric_limits<float>::quiet_NaN() : threshold);
        studioVcaSwitch(dl, "sv_meter", P_SVCA_METER, Layout::ioX, Layout::switchY);
        studioVcaMeter(dl, Layout::grX, Layout::grY, grScale, 5, -20.0f, 0.0f, meter(kMeterMaster),
                       std::numeric_limits<float>::quiet_NaN());
    }

    // Red brushed panel with rack ears and the engraved STUDIO VCA label.
    void drawStudioVcaPanel(ImDrawList* dl)
    {
        using Layout = multicompp::ui_detail::StudioVcaFaceplateLayout;
        constexpr float left = Layout::left, right = Layout::right, top = Layout::top, bottom = Layout::bottom;
        const float s = panel.scale();
        dl->AddRectFilledMultiColor(panel.P(left, top), panel.P(right, bottom),
                                    StudioVcaTheme::panelTopLeft, StudioVcaTheme::panelTopRight,
                                    StudioVcaTheme::panelBottomRight, StudioVcaTheme::panelBottomLeft);
        // Horizontal brushing: deterministic hairlines, lighter and darker.
        uint32_t grain = 0x53564341u;
        for (float y = top + 1.0f; y < bottom - 1.0f; y += 1.5f)
        {
            grain = grain * 1664525u + 1013904223u;
            const int shade = static_cast<int>((grain >> 24) & 31u);
            dl->AddLine(panel.P(left, y), panel.P(right, y),
                        (grain & 1u) ? colourWithAlpha(StudioVcaTheme::brushLight, static_cast<float>(shade / 2) / 255.0f) : colourWithAlpha(StudioVcaTheme::brushDark, static_cast<float>(shade) / 255.0f), 0.8f * s);
        }
        dl->AddLine(panel.P(left, top + 1.0f), panel.P(right, top + 1.0f), StudioVcaTheme::topEdge, 1.5f * s);
        dl->AddLine(panel.P(left, bottom - 1.0f), panel.P(right, bottom - 1.0f), StudioVcaTheme::bottomEdge, 1.5f * s);
        // Rack ears: a slot in each corner.
        for (float x : {left + 16.0f, right - 16.0f})
            for (float y : {top + 9.0f, bottom - 10.0f})
            {
                dl->AddRectFilled(panel.P(x - 11, y - 5.5f), panel.P(x + 11, y + 5.5f), StudioVcaTheme::earSlot,
                                  5.5f * s);
                dl->AddRect(panel.P(x - 11, y - 5.5f), panel.P(x + 11, y + 5.5f), StudioVcaTheme::earSlotRim,
                            5.5f * s, 0, 0.8f * s);
            }
        // Engraved label where the reference prints its model numeral: highlight, shadow, then the face.
        struct Layer { float dx, dy; ImU32 colour; };
        static constexpr Layer layers[] = {{-0.8f, -0.8f, StudioVcaTheme::engraveHighlight},
                                           {1.4f, 1.6f, StudioVcaTheme::engraveShadow},
                                           {0.0f, 0.0f, StudioVcaTheme::engraveFace}};
        for (const Layer& layer : layers)
        {
            panel.text(dl, Layout::nameX + layer.dx, Layout::nameY + layer.dy, 20.0f, layer.colour, "STUDIO", 0, true);
            panel.text(dl, Layout::nameX + layer.dx, Layout::nameY + 24.0f + layer.dy, 40.0f, layer.colour, "VCA", 0,
                       true);
        }
    }

    // A Studio VCA dial as the reference draws it: a polished chrome knob in a dark recessed well, crossed along its
    // pointer by a flat grip bar with a cream index cup near the pointer end, and a cream dot at each scale mark with
    // its numeral further out. The light is fixed at the upper right: the chrome's reflections and the well stay put
    // while the bar turns, and the bar's lit and shaded edges follow the light.
    void studioVcaKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float radius,
                       const char* name, const StudioVcaMark* marks, int markCount)
    {
        char readout[32];
        studioVcaReadout(p, plainValueForHost(p, values[p]), readout, sizeof(readout));
        studioVcaKnobSplitter.Split(dl, 2);
        studioVcaKnobSplitter.SetCurrentChannel(dl, 1);
        beginTrackedControl(p, id, true);
        panel.knob(id, p, hostMinimum(p), hostMaximum(p), x, y, radius, values[p], hostDefaultValue(p),
                   false, false, "%.3f", "", 0, true, false, nullptr, false, 1.0f, 0.0f, name, true,
                   readout, false, 0.0f, 0.0f, false, true, 9.5f, true, false,
                   &studioVcaHostToDial, &studioVcaDialToHost, this, true, &studioVcaParseText);
        endTrackedControl();
        studioVcaKnobSplitter.SetCurrentChannel(dl, 0);
        const float s = panel.scale();
        const ImVec2 c = panel.P(x, y);
        const float r = radius * s;
        for (int i = 0; i < markCount; ++i)
        {
            const float angle = duskdaf::DuskPanel::knobAngle(multicompp::ui_detail::studioVcaDialPosition(p, marks[i].plain));
            dl->AddCircleFilled(ImVec2(c.x + std::sin(angle) * r * 1.21f, c.y - std::cos(angle) * r * 1.21f),
                                0.032f * r, StudioVcaTheme::ink, 12);
            // The numeral further out at the legend size (16): those left of the dial end at their anchor and those
            // right of it start there, so the widest clear their dots.
            constexpr float offset = multicompp::ui_detail::StudioVcaFaceplateLayout::numeralOffset;
            const float dx = std::sin(angle);
            const float lx = x + dx * (radius + offset);
            const float ly = y - std::cos(angle) * (radius + offset) - 8.0f;
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            if (marks[i].label != nullptr)
                panel.text(dl, lx, ly, 16.0f, StudioVcaTheme::ink, marks[i].label, align, false);
            else
                studioVcaInfinityMark(dl, lx + 8.0f * static_cast<float>(-align), ly + 8.0f);
        }
        studioVcaWell(dl, c, r);
        studioVcaChrome(dl, c, r);
        const float angle = duskdaf::DuskPanel::knobAngle(
            multicompp::ui_detail::studioVcaDialPosition(p, plainValueForHost(p, values[p])));
        studioVcaGripBar(dl, c, r, ImVec2(std::sin(angle), -std::cos(angle)), s);
        studioVcaKnobSplitter.Merge(dl);
    }

    // The Studio VCA face's light: a unit vector toward the upper right (screen axes, y down).
    static constexpr float kSvcaLightX = 0.70710678f, kSvcaLightY = -0.70710678f;

    // The point `distance` from q along the light (screen pixels; negative: away from it).
    static ImVec2 studioVcaTowardLight(ImVec2 q, float distance)
    {
        return ImVec2(q.x + distance * kSvcaLightX, q.y + distance * kSvcaLightY);
    }

    static constexpr int kSvcaRound = 64;   // points on a Studio VCA dial's round outlines

    // An ellipse round `centre` with semi-axes `along` the light and `across` it, as kSvcaRound points, clockwise on
    // screen as an anti-aliased fill wants them.
    static void studioVcaOval(ImVec2 centre, float along, float across, ImVec2* out)
    {
        for (int i = 0; i < kSvcaRound; ++i)
        {
            const float phi = 2.0f * duskdaf::DuskPanel::kPi * static_cast<float>(i) / kSvcaRound;
            const float a = along * std::cos(phi), b = across * std::sin(phi);
            out[i] = ImVec2(centre.x + a * kSvcaLightX - b * kSvcaLightY, centre.y + a * kSvcaLightY + b * kSvcaLightX);
        }
    }

    // The recess the dial sits in: near-black maroon, wider and darker toward the upper left, its lower right lip
    // catching the light.
    static void studioVcaWell(ImDrawList* dl, ImVec2 c, float r)
    {
        const ImVec2 centre(c.x - 0.018f * r, c.y - 0.018f * r);
        const float radius = 1.085f * r;
        dl->AddCircleFilled(ImVec2(centre.x + 0.035f * r, centre.y + 0.035f * r), radius,
                            StudioVcaTheme::wellRim, kSvcaRound);
        const float diagonal = 0.70710678f * radius;
        gradientCircle(dl, centre, radius, ImVec2(centre.x - diagonal, centre.y - diagonal),
                       ImVec2(centre.x + diagonal, centre.y + diagonal), StudioVcaTheme::wellDark,
                       StudioVcaTheme::wellLight);
    }

    // The polished chrome top, a shallow dome mirroring its surroundings under the fixed light: the red panel as a
    // brownish grey toward the lower left, then light silver bands, each a disc set further toward the light so its
    // edge is a hard curved reflection line, a large near-white highlight in the upper right, and the polished edge
    // lit toward the light. None of it turns with the dial.
    static void studioVcaChrome(ImDrawList* dl, ImVec2 c, float r)
    {
        // Each band's offset toward the light and radius, in knob radii, and its colours at the edges away from and
        // toward the light.
        struct Band { float offset, radius; ImU32 shaded, lit; };
        static constexpr Band bands[] = {
            {0.0f, 1.0f, StudioVcaTheme::chromePanelShaded, StudioVcaTheme::chromePanelLit},    // the panel, mirrored
            {0.12f, 0.86f, StudioVcaTheme::chromeHorizonShaded, StudioVcaTheme::chromeHorizonLit},  // the dark horizon band
            {0.2f, 0.78f, StudioVcaTheme::chromeSilverShaded, StudioVcaTheme::chromeSilverLit}};  // the light silver
        for (const Band& band : bands)
        {
            const ImVec2 centre = studioVcaTowardLight(c, band.offset * r);
            gradientCircle(dl, centre, band.radius * r, studioVcaTowardLight(centre, -band.radius * r),
                           studioVcaTowardLight(centre, band.radius * r), band.shaded, band.lit);
        }
        ImVec2 outline[kSvcaRound];
        const ImVec2 spot = studioVcaTowardLight(c, 0.48f * r);
        studioVcaOval(spot, 0.36f * r, 0.6f * r, outline);
        dl->AddConvexPolyFilled(outline, kSvcaRound, StudioVcaTheme::chromeHalo);
        studioVcaOval(spot, 0.26f * r, 0.48f * r, outline);
        dl->AddConvexPolyFilled(outline, kSvcaRound, StudioVcaTheme::chromeHighlight);
        constexpr float upperRight = -0.25f * duskdaf::DuskPanel::kPi;   // ImGui arc angles: clockwise from +x
        dl->PathArcTo(c, 0.98f * r, upperRight - 1.0f, upperRight + 1.0f, 24);
        dl->PathStroke(StudioVcaTheme::chromeRimLit, ImDrawFlags_None, 0.035f * r);
        dl->PathArcTo(c, 0.98f * r, upperRight + duskdaf::DuskPanel::kPi - 1.0f,
                      upperRight + duskdaf::DuskPanel::kPi + 1.0f, 24);
        dl->PathStroke(StudioVcaTheme::chromeRimShaded, ImDrawFlags_None, 0.035f * r);
    }

    // The flat grip bar across the full diameter along the pointer d: its shadow on the chrome, an olive grey top
    // lighter toward the light, a bright line on the edge facing the light and a dark one opposite, and the index.
    static void studioVcaGripBar(ImDrawList* dl, ImVec2 c, float r, ImVec2 d, float s)
    {
        const ImVec2 n(-d.y, d.x);
        const float facing = n.x * kSvcaLightX + n.y * kSvcaLightY;   // how squarely the +n edge faces the light
        const float half = 0.30f * r, reach = 0.985f * r;
        constexpr int arcPoints = 8;
        ImVec2 outline[2 * arcPoints];
        int count = studioVcaSlab(c, reach, d, -0.09f * r * facing, half, outline, arcPoints);
        dl->AddConvexPolyFilled(outline, count, StudioVcaTheme::barShadow);
        count = studioVcaSlab(c, reach, d, 0.0f, half, outline, arcPoints);
        const int topVertices = dl->VtxBuffer.Size;
        dl->AddConvexPolyFilled(outline, count, IM_COL32_WHITE);
        shadeLinear(dl, topVertices, studioVcaTowardLight(c, -r), studioVcaTowardLight(c, r),
                    StudioVcaTheme::barShaded, StudioVcaTheme::barLit);
        const auto edge = [&](float side, ImU32 colour, float width) {
            const float k = side * (half - 0.5f * width);
            const float along = std::sqrt(reach * reach - k * k) - 0.5f * width;
            dl->AddLine(ImVec2(c.x + n.x * k - d.x * along, c.y + n.y * k - d.y * along),
                        ImVec2(c.x + n.x * k + d.x * along, c.y + n.y * k + d.y * along), colour, width);
        };
        const float litSide = facing >= 0.0f ? 1.0f : -1.0f, strength = std::fabs(facing);
        edge(litSide, colourMix(StudioVcaTheme::barEdgeLitFaint, StudioVcaTheme::barEdgeLit, strength), 0.9f * s);
        edge(-litSide, colourMix(StudioVcaTheme::barEdgeShadedFaint, StudioVcaTheme::barEdgeShaded, strength), 1.1f * s);
        studioVcaIndexCup(dl, c, r, d, s);
    }

    // The pointer index: a small cream cup engraved near the bar's pointer end, open toward the rim.
    static void studioVcaIndexCup(ImDrawList* dl, ImVec2 c, float r, ImVec2 d, float s)
    {
        const ImVec2 n(-d.y, d.x);
        const float bowl = 0.065f * r, leg = 0.05f * r, at = 0.74f * r;
        const auto point = [&](float along, float side) {
            return ImVec2(c.x + d.x * along + n.x * side, c.y + d.y * along + n.y * side);
        };
        constexpr int arcPoints = 9;
        ImVec2 cup[arcPoints + 2];
        cup[0] = point(at + leg, bowl);
        for (int i = 0; i < arcPoints; ++i)
        {
            const float beta = duskdaf::DuskPanel::kPi * static_cast<float>(i) / (arcPoints - 1);
            cup[i + 1] = point(at - bowl * std::sin(beta), bowl * std::cos(beta));
        }
        cup[arcPoints + 1] = point(at + leg, -bowl);
        ImVec2 groove[arcPoints + 2];   // the engraving's shaded wall, on its side toward the light
        for (int i = 0; i < arcPoints + 2; ++i)
            groove[i] = ImVec2(cup[i].x + 0.5f * s * kSvcaLightX, cup[i].y + 0.5f * s * kSvcaLightY);
        dl->AddPolyline(groove, arcPoints + 2, StudioVcaTheme::cupGroove, ImDrawFlags_None, s);
        dl->AddPolyline(cup, arcPoints + 2, StudioVcaTheme::cup, ImDrawFlags_None, s);
    }

    // The part of the disc (c, r) between the lines at offset - halfWidth and offset + halfWidth along n, which is d
    // turned a quarter clockwise: the grip bar (d its axis) and its shadow. Writes 2 * arcPoints points, clockwise on
    // screen as an anti-aliased fill wants them, and returns that count. Both lines must cut the disc
    // (|offset| + halfWidth < r).
    static int studioVcaSlab(ImVec2 c, float r, ImVec2 d, float offset, float halfWidth, ImVec2* out, int arcPoints)
    {
        const ImVec2 n(-d.y, d.x);
        const float low = std::asin((offset - halfWidth) / r), high = std::asin((offset + halfWidth) / r);
        int count = 0;
        const auto arc = [&](float from, float to) {
            for (int i = 0; i < arcPoints; ++i)
            {
                const float phi = from + (to - from) * static_cast<float>(i) / static_cast<float>(arcPoints - 1);
                out[count++] = ImVec2(c.x + r * (std::cos(phi) * d.x + std::sin(phi) * n.x),
                                      c.y + r * (std::cos(phi) * d.y + std::sin(phi) * n.y));
            }
        };
        arc(low, high);
        arc(duskdaf::DuskPanel::kPi - high, duskdaf::DuskPanel::kPi - low);
        return count;
    }

    // The infinity sign the font lacks, centred on (x, y).
    void studioVcaInfinityMark(ImDrawList* dl, float x, float y)
    {
        const float s = panel.scale();
        constexpr int points = 32;
        ImVec2 path[points];
        for (int i = 0; i < points; ++i)
        {
            const float t = 2.0f * duskdaf::DuskPanel::kPi * static_cast<float>(i) / points;
            const float k = 1.0f + std::sin(t) * std::sin(t);
            path[i] = panel.P(x + 8.0f * std::cos(t) / k, y + 8.0f * std::sin(t) * std::cos(t) / k);
        }
        dl->AddPolyline(path, points, StudioVcaTheme::ink, ImDrawFlags_Closed, 1.8f * s);
    }

    // The AUTO RELEASE lamp button.
    void studioVcaLamp(ImDrawList* dl, const char* id, uint32_t p, float x, float y)
    {
        const float s = panel.scale();
        const bool on = values[p] >= 0.5f;
        ImGui::SetCursorScreenPos(panel.P(x - 13, y - 13));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(26 * s, 26 * s));
        const ImVec2 c = panel.P(x, y);
        if (on) dl->AddCircleFilled(c, 17.0f * s, StudioVcaTheme::lampGlow, 32);
        dl->AddCircleFilled(c, 13.0f * s, StudioVcaTheme::lampWell, 32);
        gradientDisc(dl, c, 11.0f * s, -0.6f, on ? StudioVcaTheme::lampLitHigh : StudioVcaTheme::lampHigh,
                     on ? StudioVcaTheme::lampLitLow : StudioVcaTheme::lampLow);
        dl->AddCircleFilled(ImVec2(c.x - 3.5f * s, c.y - 3.5f * s), 3.0f * s,
                            on ? StudioVcaTheme::lampSpecularLit : StudioVcaTheme::lampSpecular, 16);
        if (clicked) setValue(p, on ? 0.0f : 1.0f);
    }

    // The INPUT/OUTPUT meter switch: a pill with the lit ball on the selected side.
    void studioVcaSwitch(ImDrawList* dl, const char* id, uint32_t p, float x, float y)
    {
        const float s = panel.scale();
        const bool output = values[p] >= 0.5f;
        ImGui::SetCursorScreenPos(panel.P(x - 26, y - 12));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(52 * s, 24 * s));
        dl->AddRectFilled(panel.P(x - 26, y - 12), panel.P(x + 26, y + 12), StudioVcaTheme::switchWell, 12.0f * s);
        dl->AddRect(panel.P(x - 26, y - 12), panel.P(x + 26, y + 12), StudioVcaTheme::switchRim, 12.0f * s, 0, s);
        const ImVec2 ball = panel.P(output ? x + 13 : x - 13, y);
        gradientDisc(dl, ball, 9.5f * s, -0.6f, StudioVcaTheme::ballLit, StudioVcaTheme::ballShade);
        panel.text(dl, x - 34, y - 8, 16.0f, StudioVcaTheme::ink, "INPUT", 1, true);
        panel.text(dl, x + 34, y - 8, 16.0f, StudioVcaTheme::ink, "OUTPUT", -1, true);
        if (clicked) setValue(p, output ? 0.0f : 1.0f);
    }

    // One of the face's round meters, the reference's geometry scaled to the layout's bezel: the needle pivots below
    // the window centre and sweeps +-30 degrees over minDb..maxDb (ui_detail::studioVcaNeedleDegrees); the dark lower
    // half hides its foot. Ticks and numerals (14) stand outside the scale's arc, beyond the needle's reach; the end
    // numerals reach inward from their anchors so they stay in the window, and a mark without a label is a tick alone.
    // markDb (NaN = none) draws the red threshold mark.
    void studioVcaMeter(ImDrawList* dl, float cx, float cy, const StudioVcaMark* scale, int count,
                        float minDb, float maxDb, float valueDb, float markDb)
    {
        const float s = panel.scale();
        constexpr float k = multicompp::ui_detail::StudioVcaFaceplateLayout::meterRadius / 72.0f;
        constexpr float bezel = 72.0f * k, face = 64.0f * k, split = 8.0f * k, pivotDrop = 61.0f * k;
        constexpr float arc = 92.0f * k;   // arc top at 0.49 face
        constexpr float tick = 4.0f * k, numeralSize = 14.0f, numeralRadius = arc + tick + 2.0f + 0.5f * numeralSize;
        const ImVec2 c = panel.P(cx, cy);
        dl->AddCircleFilled(ImVec2(c.x + 2.0f * s, c.y + 3.0f * s), (bezel + 2.0f) * s, StudioVcaTheme::meterShadow, 64);
        gradientDisc(dl, c, bezel * s, -0.5f, StudioVcaTheme::bezelLit, StudioVcaTheme::bezelShade, 64);
        dl->AddCircle(c, (bezel - 3.0f) * s, StudioVcaTheme::bezelRing, 64, s);
        dl->AddCircleFilled(c, (face + 2.0f) * s, StudioVcaTheme::faceRim, 64);
        // Cream window above the split line, dark below it.
        dl->AddCircleFilled(c, face * s, StudioVcaTheme::faceDark, 64);
        constexpr int segments = 48;
        ImVec2 window[segments + 1];
        const float limit = duskdaf::DuskPanel::kPi - std::acos(split / face);   // from straight up to the split
        for (int i = 0; i <= segments; ++i)
        {
            const float a = -limit + 2.0f * limit * static_cast<float>(i) / segments;
            window[i] = panel.P(cx + face * std::sin(a), cy - face * std::cos(a));
        }
        dl->AddConvexPolyFilled(window, segments + 1, StudioVcaTheme::window);
        dl->PushClipRect(panel.P(cx - face, cy - face), panel.P(cx + face, cy + split), true);
        dl->AddCircleFilled(panel.P(cx, cy - 6.0f * k), 30.0f * k * s, StudioVcaTheme::windowGlow, 32);   // lamp glow
        dl->PopClipRect();
        // Scale: an arc about the pivot, ticks and numerals (design coordinates).
        const float px = cx, py = cy + pivotDrop;
        const auto at = [&](float degrees, float radius) {
            const float a = degrees * duskdaf::DuskPanel::kPi / 180.0f;
            return panel.P(px + std::sin(a) * radius, py - std::cos(a) * radius);
        };
        dl->PathArcTo(panel.P(px, py), arc * s, (-90.0f - 30.0f) * duskdaf::DuskPanel::kPi / 180.0f,
                      (-90.0f + 30.0f) * duskdaf::DuskPanel::kPi / 180.0f, 32);
        dl->PathStroke(StudioVcaTheme::scaleInk, 0, 1.6f * s);
        for (int i = 0; i < count; ++i)
        {
            const float degrees = multicompp::ui_detail::studioVcaNeedleDegrees(scale[i].plain, minDb, maxDb);
            dl->AddLine(at(degrees, arc), at(degrees, arc + tick), StudioVcaTheme::scaleInk, 1.4f * s);
            if (scale[i].label == nullptr)
                continue;
            const float a = degrees * duskdaf::DuskPanel::kPi / 180.0f, dx = std::sin(a);
            panel.text(dl, px + dx * numeralRadius, py - std::cos(a) * numeralRadius - 0.5f * numeralSize,
                       numeralSize, StudioVcaTheme::scaleInk, scale[i].label, dx < -0.25f ? -1 : (dx > 0.25f ? 1 : 0),
                       false);
        }
        // VU in the window's middle, under the arc: at the right, where the reference prints it, the gain reduction
        // meter's needle crossed it at rest.
        panel.text(dl, cx, cy - 13.0f * k - 8.0f, 16.0f, StudioVcaTheme::scaleInk, "VU", 0, true);
        // Needles, clipped to the window; the window's lower edge is where the dark half hides their feet.
        dl->PushClipRect(panel.P(cx - face, cy - face), panel.P(cx + face, cy + split), true);
        if (std::isfinite(markDb))
            dl->AddLine(panel.P(px, py), at(multicompp::ui_detail::studioVcaNeedleDegrees(markDb, minDb, maxDb), arc - 3.0f),
                        StudioVcaTheme::thresholdMark, 1.6f * s);
        dl->AddLine(panel.P(px, py), at(multicompp::ui_detail::studioVcaNeedleDegrees(valueDb, minDb, maxDb), arc - 3.0f),
                    StudioVcaTheme::needle, 1.8f * s);
        dl->PopClipRect();
        // Glass: a soft reflection band on the right.
        dl->AddRectFilledMultiColor(panel.P(cx + 22.0f * k, cy - 58.0f * k), panel.P(cx + 44.0f * k, cy + split),
                                    StudioVcaTheme::glassTopLeft, StudioVcaTheme::glassTopRight,
                                    StudioVcaTheme::glassBottomRight, StudioVcaTheme::glassBottomLeft);
    }

    // A disc lit along `angle` (radians from straight up): `lit` on that side, `shade` opposite.
    static void gradientDisc(ImDrawList* dl, ImVec2 c, float r, float angle, ImU32 lit, ImU32 shade,
                             int segments = 48)
    {
        const ImVec4 a = ImGui::ColorConvertU32ToFloat4(lit), b = ImGui::ColorConvertU32ToFloat4(shade);
        const auto mix = [&](float t) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t,
                                                         b.z + (a.z - b.z) * t, b.w + (a.w - b.w) * t));
        };
        const ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
        const ImVec2 dir(std::sin(angle), -std::cos(angle));
        dl->PrimReserve(segments * 3, segments + 1);
        const unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(c, uv, mix(0.5f));
        for (int i = 0; i < segments; ++i)
        {
            const float t = 2.0f * duskdaf::DuskPanel::kPi * static_cast<float>(i) / segments;
            const ImVec2 p(c.x + r * std::cos(t), c.y + r * std::sin(t));
            dl->PrimWriteVtx(p, uv, mix(0.5f + 0.5f * ((p.x - c.x) * dir.x + (p.y - c.y) * dir.y) / r));
        }
        for (int i = 0; i < segments; ++i)
        {
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % segments));
        }
    }

    // The dial's value as the reference prints it (Release in seconds), with locale-free decimals, so the typed-
    // entry field it seeds parses back (studioVcaParseText).
    static void studioVcaReadout(uint32_t p, float plain, char* text, size_t size)
    {
        const auto fixed = [&](float v, int decimals, const char* unit) {
            const float scale = decimals == 2 ? 100.0f : decimals == 1 ? 10.0f : 1.0f;
            const long scaled = std::lround(std::fabs(v) * scale);
            const long whole = static_cast<long>(scaled / static_cast<long>(scale));
            const long part = scaled % static_cast<long>(scale);
            const char* sign = v < 0.0f && scaled != 0 ? "-" : "";
            if (decimals == 0) std::snprintf(text, size, "%s%ld%s", sign, whole, unit);
            else std::snprintf(text, size, decimals == 2 ? "%s%ld.%02ld%s" : "%s%ld.%01ld%s", sign, whole, part, unit);
        };
        if (p == P_SVCA_RATIO) fixed(plain, 1, ":1");
        else if (p == P_SVCA_ATTACK) fixed(plain, 2, " ms");
        else if (p == P_SVCA_RELEASE) fixed(plain * 0.001f, 2, " s");
        else if (p == P_MIX) fixed(plain, 0, "%");
        else fixed(plain, 1, " dB");
    }

    static float studioVcaHostToDial(float host, uint32_t p, void* context)
    {
        const float plain = static_cast<MultiCompUI*>(context)->plainValueForHost(p, host);
        return multicompp::ui_detail::studioVcaDialPosition(p, plain);
    }

    static float studioVcaDialToHost(float position, uint32_t p, void* context)
    {
        const float plain = multicompp::ui_detail::studioVcaDialPlain(p, position);
        return static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
    }

    // Typed entry in the readout's units (Release in seconds), with or without the unit; any value of the
    // control's full range, including those beyond the dial's travel.
    static bool studioVcaParseText(const char* text, uint32_t p, void* context, float& host)
    {
        float plain = 0.0f;
        if (!multicompp::ui_detail::studioVcaTypedPlain(text, p, plain)) return false;
        host = static_cast<MultiCompUI*>(context)->hostValueForPlain(p, plain);
        return true;
    }

    // ---- Digital and Multiband rack faces -------------------------------------------------------------------
    // Both carry the JUCE editor's controls, meters and drawing (EnhancedCompressorEditor, ModernCompressorPanels.h,
    // AnalogLookAndFeel.cpp) on the canvas every mode shares: the INPUT and OUTPUT meters keep their distance from the
    // window edges, and what the original stacked between them sits side by side (Digital: the gain-reduction
    // display, the side-chain high-pass, the two rows of knobs; Multiband: the band meters and the selected band's
    // knobs). Positions come from ui_detail::ModernFaceLayout.

    // The Digital and Multiband faces' colours by role (kModernText, kDigitalCyan, kDim and kBandColors are their
    // lettering and accents), sampled from the faces as they were drawn before the tokens.
    struct ModernTheme
    {
        // The rack: body, plate and its grain, ear seams, edges and the printed rules
        static constexpr ImU32 rack = IM_COL32(37, 39, 38, 255);
        static constexpr ImU32 plateTop = IM_COL32(48, 51, 50, 255);
        static constexpr ImU32 plateBottom = IM_COL32(24, 27, 26, 255);
        static constexpr ImU32 grainLight = IM_COL32(255, 255, 255, 3);
        static constexpr ImU32 grainDark = IM_COL32(0, 0, 0, 8);
        static constexpr ImU32 earSeam = IM_COL32(8, 10, 9, 255);
        static constexpr ImU32 earSeamHighlight = IM_COL32(107, 112, 108, 80);
        static constexpr ImU32 topEdge = IM_COL32(123, 129, 124, 150);
        static constexpr ImU32 bottomEdge = IM_COL32(6, 8, 7, 255);
        static constexpr ImU32 titleRule = IM_COL32(120, 128, 122, 100);
        static constexpr ImU32 sectionRule = IM_COL32(121, 130, 122, 100);
        // The INPUT / OUTPUT meters and their LEDs
        static constexpr ImU32 meterWell = IM_COL32(0x0A, 0x0A, 0x0A, 255);
        static constexpr ImU32 meterShade = IM_COL32(0, 0, 0, 153);
        static constexpr ImU32 meterFrame = IM_COL32(0x2A, 0x2A, 0x2A, 255);
        static constexpr ImU32 channelLegend = IM_COL32(128, 128, 128, 153);
        static constexpr ImU32 meterReadout = IM_COL32(0xCC, 0xCC, 0xCC, 255);
        static constexpr ImU32 ledHighlight = IM_COL32(255, 255, 255, 89);
        static constexpr ImU32 ledPeakHighlight = IM_COL32(255, 255, 255, 51);
        static constexpr ImU32 ledUnlitEdge = IM_COL32(0, 0, 0, 102);
        static constexpr ImU32 ledUnlitHighlight = IM_COL32(255, 255, 255, 13);
        static constexpr ImU32 clearWhite = IM_COL32(255, 255, 255, 0);
        static constexpr ImU32 clearBlack = IM_COL32(0, 0, 0, 0);
        // The knobs and their values
        static constexpr ImU32 knobShadow = IM_COL32(0, 0, 0, 150);
        static constexpr ImU32 knobBase = IM_COL32(12, 14, 13, 255);
        static constexpr ImU32 toothLight = IM_COL32(83, 87, 83, 255);
        static constexpr ImU32 toothDark = IM_COL32(20, 23, 21, 255);
        static constexpr ImU32 capLight = IM_COL32(226, 229, 220, 255);
        static constexpr ImU32 capDark = IM_COL32(127, 133, 126, 255);
        static constexpr ImU32 capRim = IM_COL32(227, 230, 222, 190);
        static constexpr ImU32 pointer = IM_COL32(29, 33, 29, 255);
        static constexpr ImU32 ringMajor = IM_COL32(193, 199, 189, 220);
        static constexpr ImU32 ringMinor = IM_COL32(151, 159, 149, 180);
        static constexpr ImU32 value = IM_COL32(255, 255, 255, 255);
        // The illuminated toggle (also the settings drawer's) and the band switches
        static constexpr ImU32 toggleWell = IM_COL32(9, 12, 10, 255);
        static constexpr ImU32 toggleDown = IM_COL32(39, 44, 41, 255);
        static constexpr ImU32 toggleTop = IM_COL32(76, 83, 77, 255);
        static constexpr ImU32 toggleBottom = IM_COL32(31, 36, 32, 255);
        static constexpr ImU32 toggleEdge = IM_COL32(117, 125, 116, 255);
        static constexpr ImU32 lampWell = IM_COL32(10, 13, 10, 255);
        static constexpr ImU32 lampOff = IM_COL32(60, 77, 67, 255);
        static constexpr ImU32 buttonDown = IM_COL32(34, 40, 35, 255);
        static constexpr ImU32 buttonHover = IM_COL32(88, 96, 86, 255);
        static constexpr ImU32 buttonCap = IM_COL32(65, 73, 65, 255);
        static constexpr ImU32 buttonBottom = IM_COL32(26, 32, 27, 255);
        static constexpr ImU32 buttonEdge = IM_COL32(102, 113, 101, 190);
        // The Digital face's VU
        static constexpr ImU32 vuBezelTop = IM_COL32(101, 109, 99, 255);
        static constexpr ImU32 vuBezelBottom = IM_COL32(22, 27, 23, 255);
        static constexpr ImU32 vuSurround = IM_COL32(0x3A, 0x3A, 0x3A, 255);
        static constexpr ImU32 vuFaceTop = IM_COL32(234, 222, 187, 255);
        static constexpr ImU32 vuFaceBottom = IM_COL32(211, 193, 146, 255);
        static constexpr ImU32 vuArc = IM_COL32(0x1A, 0x1A, 0x1A, 179);
        static constexpr ImU32 vuHot = IM_COL32(0xD4, 0x2C, 0x2C, 255);
        static constexpr ImU32 vuInk = IM_COL32(0x2A, 0x2A, 0x2A, 255);
        static constexpr ImU32 vuPercent = IM_COL32(0x60, 0x60, 0x60, 255);
        static constexpr ImU32 vuNeedle = IM_COL32(0, 0, 0, 255);
        static constexpr ImU32 vuPeak = IM_COL32(0xFF, 0x33, 0x33, 255);
        static constexpr ImU32 vuPeakLine = IM_COL32(0xFF, 0x33, 0x33, 0x60);
        static constexpr ImU32 vuGlass = IM_COL32(255, 255, 255, 0x20);
        static constexpr ImU32 captionShade = IM_COL32(0, 0, 0, 0x30);
        // The Digital face's gain reduction history
        static constexpr ImU32 historyFrame = IM_COL32(0x60, 0x60, 0x60, 255);
        static constexpr ImU32 historyTop = IM_COL32(0x1A, 0x1A, 0x1E, 255);
        static constexpr ImU32 historyBottom = IM_COL32(0x10, 0x10, 0x14, 255);
        static constexpr ImU32 historyZero = IM_COL32(0x50, 0x50, 0x50, 255);
        static constexpr ImU32 historyGrid = IM_COL32(0x2A, 0x2A, 0x2E, 255);
        static constexpr ImU32 historyZeroLabel = IM_COL32(0xAA, 0xAA, 0xAA, 255);
        static constexpr ImU32 historyLabel = IM_COL32(0x70, 0x70, 0x70, 255);
        static constexpr ImU32 historyFillTop = IM_COL32(0x00, 0xCC, 0x77, 230);
        static constexpr ImU32 historyFillBottom = IM_COL32(0x00, 0x33, 0x22, 179);
        static constexpr ImU32 historyTraceGlow = IM_COL32(0x00, 0xFF, 0x88, 0x30);
        static constexpr ImU32 historyTrace = IM_COL32(0x00, 0xFF, 0x88, 255);
        static constexpr ImU32 historyNow = IM_COL32(0xFF, 0xAA, 0x00, 255);
        static constexpr ImU32 historyDeepestLine = IM_COL32(0xFF, 0x66, 0x66, 0x80);
        static constexpr ImU32 historyDeepest = IM_COL32(0xFF, 0x66, 0x66, 255);
        static constexpr ImU32 historyTitleBack = IM_COL32(0x1A, 0x1A, 0x1A, 0xDD);
        static constexpr ImU32 historyTitle = IM_COL32(255, 255, 255, 255);
        // The Digital face's side-chain high-pass slider
        static constexpr ImU32 sliderSlot = IM_COL32(8, 11, 9, 255);
        static constexpr ImU32 sliderScale = IM_COL32(98, 107, 97, 150);
        static constexpr ImU32 thumbTop = IM_COL32(207, 213, 202, 255);
        static constexpr ImU32 thumbBottom = IM_COL32(110, 119, 108, 255);
        static constexpr ImU32 thumbLine = IM_COL32(32, 39, 32, 255);
        // The Multiband face: band meters, crossover faders, scale, enable strip and the B / S switches
        static constexpr ImU32 bandMeterTop = IM_COL32(0x0C, 0x0C, 0x0C, 255);
        static constexpr ImU32 bandMeterBottom = IM_COL32(0x18, 0x18, 0x18, 255);
        static constexpr ImU32 bandMeterEdge = IM_COL32(0, 0, 0, 128);
        static constexpr ImU32 bandYellow = IM_COL32(0xFF, 0xDD, 0x00, 255);
        static constexpr ImU32 bandOrange = IM_COL32(0xFF, 0x88, 0x00, 255);
        static constexpr ImU32 bandRed = IM_COL32(0xFF, 0x33, 0x33, 255);
        static constexpr ImU32 bandPeakGlow = IM_COL32(255, 255, 255, 77);
        static constexpr ImU32 bandPeak = IM_COL32(255, 255, 255, 255);
        static constexpr ImU32 bandUnlit = IM_COL32(0x24, 0x24, 0x29, 255);
        static constexpr ImU32 bandUnlitHighlight = IM_COL32(0x30, 0x30, 0x36, 255);
        static constexpr ImU32 pillTop = IM_COL32(0x0A, 0x0A, 0x0A, 255);
        static constexpr ImU32 pillBottom = IM_COL32(0x1A, 0x1A, 0x1A, 255);
        static constexpr ImU32 valueShadow = IM_COL32(0, 0, 0, 128);
        static constexpr ImU32 faderTrack = IM_COL32(0x1A, 0x1A, 0x1A, 255);
        static constexpr ImU32 gripShadow = IM_COL32(0, 0, 0, 102);
        static constexpr ImU32 grip = IM_COL32(0x40, 0x40, 0x50, 255);
        static constexpr ImU32 gripLine = IM_COL32(255, 255, 255, 128);
        static constexpr ImU32 gripEdge = IM_COL32(255, 255, 255, 77);
        static constexpr ImU32 faderLabel = IM_COL32(255, 255, 255, 230);
        static constexpr ImU32 bandOff = IM_COL32(0x2A, 0x2A, 0x2A, 255);
        static constexpr ImU32 scaleTick = IM_COL32(255, 255, 255, 128);
        static constexpr ImU32 scaleLabel = IM_COL32(255, 255, 255, 179);
        static constexpr ImU32 bypassLit = IM_COL32(0xFF, 0x8C, 0x42, 255);
        static constexpr ImU32 soloLit = IM_COL32(0xFF, 0xCC, 0x00, 255);
        static constexpr ImU32 switchOff = IM_COL32(0x3A, 0x3A, 0x3A, 255);
        static constexpr ImU32 switchLegend = IM_COL32(0xAA, 0xAA, 0xAA, 255);
        static constexpr ImU32 switchLegendLit = IM_COL32(0, 0, 0, 255);
    };

    // A line of text centred on (x, centreY).
    void textCentred(ImDrawList* dl, float x, float centreY, float size, ImU32 colour, const char* text, bool bold)
    {
        panel.text(dl, x, centreY - 0.5f * size, size, colour, text, 0, bold);
    }

    // Same full-width rack, mounting ears and printed lettering as the hardware faces.
    // Kept local to these two modes so their finish does not alter the reference panels.
    void drawModernBody(ImDrawList* dl, const char* title, const char* description)
    {
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        const float s = panel.scale();
        constexpr float top = Layout::top, bottom = Layout::bottom, width = kDesignW, ear = Layout::ear;
        dl->AddRectFilled(panel.P(0, top), panel.P(width, bottom), ModernTheme::rack);
        gradientRect(dl, panel.P(ear, top + 1), panel.P(width - ear, bottom - 1), 0,
                     panel.P(ear, top), panel.P(ear, bottom), ModernTheme::plateTop, ModernTheme::plateBottom);
        for (int row = 1; top + 2.0f * static_cast<float>(row) < bottom - 1.0f; ++row)
        {
            const float y = top + 2.0f * static_cast<float>(row);
            dl->AddLine(panel.P(ear + 1, y), panel.P(width - ear - 1, y),
                        row % 3 == 0 ? ModernTheme::grainLight : ModernTheme::grainDark, s);
        }
        for (float x : {ear, width - ear})
        {
            dl->AddLine(panel.P(x, top), panel.P(x, bottom), ModernTheme::earSeam, 1.5f * s);
            dl->AddLine(panel.P(x + 1, top), panel.P(x + 1, bottom), ModernTheme::earSeamHighlight, s);
        }
        dl->AddLine(panel.P(0, top + 1), panel.P(width, top + 1), ModernTheme::topEdge, s);
        dl->AddLine(panel.P(0, bottom - 2), panel.P(width, bottom - 2), ModernTheme::bottomEdge, 2 * s);
        for (float x : {0.5f * ear + 1.0f, width - 0.5f * ear - 1.0f})
            for (float y : {top + 20.0f, bottom - 21.0f})
                drawOptoScrew(dl, x, y, 5.0f);
        // The name at the legend size (16); MC-2, the series and the foot line are small print (13).
        duskdaf::spacedText(panel, dl, width * 0.5f, Layout::titleY, 16.0f, kModernText, title, 0, 0.12f);
        panel.text(dl, Layout::printX0, Layout::titleY + 2.0f, 13.0f, kDim, "MC-2", -1, true);
        panel.text(dl, Layout::printX1, Layout::titleY + 2.0f, 13.0f, kDim, "PRECISION SERIES", 1, true);
        dl->AddLine(panel.P(Layout::printX0, Layout::ruleY), panel.P(Layout::printX1, Layout::ruleY),
                    ModernTheme::titleRule, s);
        duskdaf::spacedText(panel, dl, width * 0.5f, Layout::descriptionY, 13.0f, kDim, description, 0, 0.12f);
    }

    // One LED of the INPUT/OUTPUT ladders (LEDMeter::drawLEDSegment).
    void drawLedSegment(ImDrawList* dl, float x0, float y0, float x1, float y1, bool lit, bool peak,
                        const duskdaf::LedLadderColors& colours)
    {
        const float s = panel.scale();
        const float h = y1 - y0;
        const ImVec2 a = panel.P(x0, y0), b = panel.P(x1, y1);
        if (lit)
        {
            dl->AddRectFilled(panel.P(x0 - 6, y0 - 6), panel.P(x1 + 6, y1 + 6), colourWithAlpha(colours.glow, 0.25f),
                              4.0f * s);
            dl->AddRectFilled(panel.P(x0 - 4, y0 - 4), panel.P(x1 + 4, y1 + 4), colourWithAlpha(colours.glow, 0.4f),
                              3.0f * s);
            gradientRect(dl, a, b, 2.0f * s, a, ImVec2(a.x, b.y), colourBrighter(colours.lit, 0.2f),
                         colourDarker(colours.lit, 0.3f));
            const ImVec2 h0 = panel.P(x0 + 1, y0 + 1), h1 = panel.P(x1 - 1, y0 + 1 + 0.4f * h);
            gradientRect(dl, h0, h1, 1.5f * s, h0, ImVec2(h0.x, h1.y), ModernTheme::ledHighlight,
                         ModernTheme::clearWhite);
        }
        else if (peak)
        {
            dl->AddRectFilled(panel.P(x0 - 2, y0 - 2), panel.P(x1 + 2, y1 + 2), colourWithAlpha(colours.glow, 0.2f),
                              3.0f * s);
            const ImU32 body = colourMix(colours.lit, colours.unlit, 0.3f);
            gradientRect(dl, a, b, 2.0f * s, a, ImVec2(a.x, b.y), colourBrighter(body, 0.1f), colourDarker(body, 0.2f));
            dl->AddRectFilled(panel.P(x0 + 1, y0 + 1), panel.P(x1 - 1, y0 + 1 + 0.4f * h), ModernTheme::ledPeakHighlight,
                              1.5f * s);
        }
        else
        {
            gradientRect(dl, a, b, 2.0f * s, a, ImVec2(a.x, b.y), colourBrighter(colours.unlit, 0.15f),
                         colourDarker(colours.unlit, 0.1f));
            dl->AddRect(panel.P(x0 + 0.5f, y0 + 0.5f), panel.P(x1 - 0.5f, y1 - 0.5f), ModernTheme::ledUnlitEdge, 2.0f * s,
                        0, 0.5f * s);
            dl->AddRectFilled(panel.P(x0 + 1, y0 + 1), panel.P(x1 - 1, y0 + 1 + 0.3f * h), ModernTheme::ledUnlitHighlight,
                              1.5f * s);
        }
    }

    // One INPUT/OUTPUT meter (LEDMeter in stereo, with LEDMeterStyle's title and readout): a recessed frame, two
    // twelve-LED columns over -60..+6 dB with L/R legends, the title above and the peak readout below.
    void drawLevelMeter(ImDrawList* dl, float x0, const duskdaf::LedLadder& ladder, const char* title, float readoutDb)
    {
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        const float s = panel.scale();
        const float x1 = x0 + Layout::meterW, y0 = Layout::meterTop, y1 = Layout::meterBottom;
        panel.text(dl, 0.5f * (x0 + x1), Layout::meterTitleY, 16.0f, kModernText, title, 0, true);
        const ImVec2 a = panel.P(x0, y0), b = panel.P(x1, y1);
        dl->AddRectFilled(a, b, ModernTheme::meterWell, 4.0f * s);
        gradientRect(dl, a, panel.P(x1, y0 + 15.0f), 4.0f * s, a, panel.P(x0, y0 + 10.0f), ModernTheme::meterShade,
                     ModernTheme::clearBlack);
        dl->AddRect(a, b, ModernTheme::meterFrame, 4.0f * s, 0, 1.0f * s);
        static constexpr int kLeds = 12;   // static: the lambda below reads it uncaptured (MSVC C3493)
        constexpr float kGap = 2.0f;
        const float columnWidth = (40.0f - 2.0f) * 0.5f;
        const duskdaf::LedLadderStyle style;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float cx0 = x0 + static_cast<float>(ch) * (columnWidth + 2.0f);
            // The L / R legend (13, small print) under each column.
            const float lx0 = cx0 + 3.0f, lx1 = cx0 + columnWidth - 3.0f, ly0 = y0 + 3.0f, ly1 = y1 - 16.0f - 3.0f;
            const float ledHeight = ((ly1 - ly0) - (kLeds - 1) * kGap) / kLeds;
            const auto ledsFor = [](float db) {
                return static_cast<int>(std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f) * static_cast<float>(kLeds));
            };
            const int lit = ledsFor(ladder.displayDb(ch)), peak = ledsFor(ladder.peakDb(ch)) - 1;
            for (int i = 0; i < kLeds; ++i)
            {
                const float top = ly1 - static_cast<float>(i + 1) * (ledHeight + kGap) + kGap;
                drawLedSegment(dl, lx0, top, lx1, top + ledHeight, i < lit, i == peak && i >= lit,
                               duskdaf::ledLadderColors(static_cast<float>(i) / kLeds, style));
            }
            textCentred(dl, cx0 + 0.5f * columnWidth, y1 - 10.0f, 13.0f, ModernTheme::channelLegend,
                        ch == 0 ? "L" : "R", false);
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(readoutDb));
        panel.text(dl, 0.5f * (x0 + x1), Layout::meterReadoutY, 17.0f, ModernTheme::meterReadout, text, 0, true);
    }

    // The INPUT and OUTPUT meters of both faces: per-channel block peaks from the plugin, the shared LED ladder's
    // ballistics (the original meter's), and the readout's peak-then-fall behaviour.
    void drawLevelMeters(ImDrawList* dl, float dt)
    {
        float in[2] = {-60.0f, -60.0f}, out[2] = {-60.0f, -60.0f};
        void* const instance = getPluginInstancePointer();
        if (instance != nullptr && multiCompGetInputChannelLevel != nullptr && multiCompGetOutputChannelLevel != nullptr)
            for (int ch = 0; ch < 2; ++ch)
            {
                in[ch] = multiCompGetInputChannelLevel(instance, ch);
                out[ch] = multiCompGetOutputChannelLevel(instance, ch);
            }
        inputLadder.update(in[0], in[1], dt);
        outputLadder.update(out[0], out[1], dt);
        multicompp::ui_detail::advanceLevelReadout(inputReadout, std::max(in[0], in[1]), dt);
        multicompp::ui_detail::advanceLevelReadout(outputReadout, std::max(out[0], out[1]), dt);
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        drawLevelMeter(dl, Layout::inputMeterX, inputLadder, "INPUT", inputReadout.shownDb);
        drawLevelMeter(dl, Layout::outputMeterX, outputLadder, "OUTPUT", outputReadout.shownDb);
    }

    // Silver cap on a serrated black skirt, with the shared gesture layer's exact pointer travel.
    // Auto Makeup dims the complete control, including its printed scale.
    void drawMetallicKnob(ImDrawList* dl, float x, float y, float r, float position, float opacity)
    {
        const float s = panel.scale();
        const auto fade = [opacity](ImU32 colour) {
            return colourWithAlpha(colour, opacity * static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f);
        };
        const ImVec2 c = panel.P(x, y);
        dl->AddCircleFilled(panel.P(x + 2, y + 3), (r + 2) * s, fade(ModernTheme::knobShadow), 64);
        dl->AddCircleFilled(c, (r + 1) * s, fade(ModernTheme::knobBase), 64);
        const float angle = duskdaf::DuskPanel::knobAngle(std::clamp(position, 0.0f, 1.0f));
        for (int tooth = 0; tooth < 48; ++tooth)
        {
            const float a = angle + 2 * kUiPi * static_cast<float>(tooth) / 48;
            dl->AddLine(panel.P(x + r * 0.85f * std::sin(a), y - r * 0.85f * std::cos(a)),
                        panel.P(x + r * std::sin(a), y - r * std::cos(a)),
                        fade(tooth % 2 ? ModernTheme::toothLight : ModernTheme::toothDark), 1.4f * s);
        }
        const float cap = r * 0.81f;
        gradientCircle(dl, c, cap * s, panel.P(x - cap, y - cap), panel.P(x + cap, y + cap),
                       fade(ModernTheme::capLight), fade(ModernTheme::capDark));
        dl->AddCircle(c, cap * s, fade(ModernTheme::capRim), 64, s);
        dl->AddLine(panel.P(x + cap * 0.20f * std::sin(angle), y - cap * 0.20f * std::cos(angle)),
                    panel.P(x + cap * 0.92f * std::sin(angle), y - cap * 0.92f * std::cos(angle)),
                    fade(ModernTheme::pointer), 2.0f * s);
        duskdaf::KnobRingStyle ring;
        ring.markInner = 4; ring.markOuter = 8;
        ring.markColor = fade(ModernTheme::ringMajor);
        ring.minorColor = fade(ModernTheme::ringMinor);
        ring.minorInner = 5; ring.minorOuter = 7;
        for (int i = 0; i < 11; ++i)
            duskdaf::drawKnobRingMark(panel, dl, x, y, r, static_cast<float>(i) / 10, nullptr, ring,
                                     i != 0 && i != 5 && i != 10);
    }

    // A Digital or Multiband control: the metallic knob, its name above and its value below (the original slider's
    // text box: white, bold, the parameter's decimals and suffix). A control Auto Makeup overrides is dimmed and
    // locked, as in the original.
    void modernKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float r, const char* name,
                    float nameCentreY, float nameSize, ImU32 nameColour, int decimals, const char* suffix,
                    float valueCentreY, bool applicable = true, float valueSize = 13.0f,
                    const char* valueText = nullptr)
    {
        const float lo = hostMinimum(p), hi = hostMaximum(p);
        const float opacity = applicable ? 1.0f : 0.4f;
        // Drawn before the widget, so a typed-value editor it opens lands on top.
        drawMetallicKnob(dl, x, y, r, hi > lo ? (values[p] - lo) / (hi - lo) : 0.0f, opacity);
        if (!applicable) ImGui::BeginDisabled();
        // The selected band's knobs reuse their captions. Give each parameter
        // its own active ID so a disappearing band cannot redirect a held drag.
        ImGui::PushID(static_cast<int>(p));
        beginTrackedControl(p, id, applicable);
        // Made inapplicable while held (Auto Makeup or the external key switched by automation), the knob is not
        // submitted this frame: ImGui drops an active item it no longer sees, while a disabled one stays active,
        // and writing, as long as the pointer is off it; its gesture closes with the frame (finishTrackedEdits).
        const bool heldWhileInapplicable = !applicable && trackedEditsOpen[p] && trackedEditOwners[p] == drawingTrackedId;
        if (!heldWhileInapplicable)
            panel.knob(id, p, lo, hi, x, y, r, values[p], hostDefaultValue(p), false, false, "%.2f", suffix, 0, true,
                       false, nullptr, false, 1.0f, 0.0f, nullptr, false, nullptr, true, 0.0f, 0.0f, false, false, 9.5f,
                       false, false, &knobHostToPlain, &knobPlainToHost, this);
        endTrackedControl();
        ImGui::PopID();
        if (!applicable) ImGui::EndDisabled();
        textCentred(dl, x, nameCentreY, nameSize, colourWithAlpha(nameColour, opacity * static_cast<float>(
                        (nameColour >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f), name, true);
        char text[48];
        if (valueText == nullptr)
            std::snprintf(text, sizeof(text), "%.*f%s", decimals,
                          static_cast<double>(knobHostToPlain(values[p], p, this)), suffix);
        textCentred(dl, x, valueCentreY, valueSize, colourWithAlpha(ModernTheme::value, opacity),
                    valueText != nullptr ? valueText : text, true);
    }

    // A Digital or Multiband face's knob: its name at the legend size (16) above its ring of ticks and its value at
    // the value size (17) below it (ModernFaceLayout).
    void faceKnob(ImDrawList* dl, const char* id, uint32_t p, float x, float y, float r, const char* name,
                  int decimals, const char* suffix, bool applicable)
    {
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        modernKnob(dl, id, p, x, y, r, name, Layout::nameTop(y, r) + 8.0f, 16.0f, kModernText, decimals, suffix,
                   Layout::valueTop(y, r) + 8.5f, applicable, 17.0f);
    }

    // Recessed hardware switch with a separate status lamp, matching the rack's silkscreen.
    void illuminatedToggle(ImDrawList* dl, const char* id, uint32_t p, float x0, float y0, float x1, float y1,
                           const char* label, float labelSize = 16.0f, bool enabled = true)
    {
        const float s = panel.scale();
        const ImVec2 hit0 = panel.P(x0, y0), hit1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(hit0);
        if (!enabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton(id, ImVec2(hit1.x - hit0.x, hit1.y - hit0.y)))
            setHostValue(p, values[p] > 0.5f ? 0.0f : 1.0f);
        if (!enabled) ImGui::EndDisabled();
        const bool on = values[p] > 0.5f, down = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(hit0, hit1, ModernTheme::toggleWell, 2 * s);
        const ImVec2 i0 = panel.P(x0 + 2, y0 + 2), i1 = panel.P(x1 - 2, y1 - 2);
        const ImU32 topColour = down ? ModernTheme::toggleDown : ModernTheme::toggleTop;
        gradientRect(dl, i0, i1, s, i0, ImVec2(i0.x, i1.y), topColour, ModernTheme::toggleBottom);
        dl->AddRect(hit0, hit1, hovered ? kModernText : ModernTheme::toggleEdge, 2 * s, 0, s);
        const float cy = 0.5f * (y0 + y1);
        dl->AddCircleFilled(panel.P(x0 + 13, cy), 4 * s, ModernTheme::lampWell, 16);
        dl->AddCircleFilled(panel.P(x0 + 13, cy), 2.6f * s, on ? kDigitalCyan : ModernTheme::lampOff, 16);
        textCentred(dl, 0.5f * (x0 + x1) + 5, cy, labelSize, colourWithAlpha(kModernText, enabled ? 1.0f : 0.4f),
                    label, true);
    }

    // The Digital face's gain-reduction VU (AnalogVUMeter::paint) in the rectangle given, needle from digitalVu.
    void drawDigitalVu(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        const float s = panel.scale();
        const float sf = std::max(0.5f, std::min((x1 - x0) / 400.0f, (y1 - y0) / 250.0f));
        gradientRect(dl, panel.P(x0, y0), panel.P(x1, y1), 3 * s, panel.P(x0, y0), panel.P(x0, y1),
                     ModernTheme::vuBezelTop, ModernTheme::vuBezelBottom);
        const float ix0 = x0 + 6, iy0 = y0 + 6, ix1 = x1 - 6, iy1 = y1 - 6;
        dl->AddRectFilled(panel.P(ix0, iy0), panel.P(ix1, iy1), ModernTheme::vuSurround, 2.0f * sf * s);
        const float fx0 = ix0 + 3 * sf, fy0 = iy0 + 3 * sf, fx1 = ix1 - 3 * sf, fy1 = iy1 - 3 * sf;
        gradientRect(dl, panel.P(fx0, fy0), panel.P(fx1, fy1), 2 * sf * s, panel.P(fx0, fy0), panel.P(fx0, fy1),
                     ModernTheme::vuFaceTop, ModernTheme::vuFaceBottom);
        dl->PushClipRect(panel.P(fx0, fy0), panel.P(fx1, fy1), true);
        const float cx = 0.5f * (fx0 + fx1), pivotY = fy1 - 3 * sf;
        const float length = std::min((fx1 - fx0) * 0.49f, (fy1 - fy0) * 0.88f);
        constexpr float kStart = -2.7f, kEnd = -0.44f;   // screen angles, the scale's left and right ends
        const auto at = [&](float angle, float radius) {
            return panel.P(cx + radius * std::cos(angle), pivotY + radius * std::sin(angle));
        };
        // The original strokes this arc with JUCE's clockwise-from-up arc convention while every tick, label and the
        // needle use these screen angles, which leaves it on the left of the face; it belongs under the ticks.
        dl->PathClear();
        for (int i = 0; i <= 48; ++i)
            dl->PathLineTo(at(kStart + (kEnd - kStart) * static_cast<float>(i) / 48.0f, length * 0.95f));
        dl->PathStroke(ModernTheme::vuArc, 0, 2.0f * sf * s);
        static constexpr float kMarks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
        for (const float db : kMarks)
        {
            const float angle = kStart + (db + 20.0f) / 23.0f * (kEnd - kStart);
            const bool major = db != 2.0f;
            const ImU32 ink = db >= 0.0f ? ModernTheme::vuHot : ModernTheme::vuInk;
            dl->AddLine(at(angle, length * 0.95f), at(angle, length * 0.95f + (major ? 10.0f : 6.0f) * sf), ink,
                        (major ? 2.0f : 1.0f) * sf * s);
            // Numerals at 14 (the meter scale's size). -2 and -1 keep their ticks only: at 14 they overlap -3 and 0.
            if (db > 0.0f || db == -2.0f || db == -1.0f) continue;
            const float radius = length * 0.72f;
            const float labelY = std::max(pivotY + radius * std::sin(angle), fy0 + 5 * sf + 7.5f * sf);
            char text[8];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(db));
            textCentred(dl, cx + radius * std::cos(angle), labelY, 14.0f, ink, text, false);
        }
        for (const int percent : {0, 100})
        {
            const float angle = kStart + static_cast<float>(percent) / 100.0f * (kEnd - kStart);
            const float radius = length * 1.15f;
            textCentred(dl, cx + radius * std::cos(angle), pivotY + radius * std::sin(angle) + 5 * sf,
                        13.0f, ModernTheme::vuPercent, percent == 0 ? "0%" : "100%", false);
        }
        textCentred(dl, cx, pivotY - length * 0.4f + 10 * sf, std::max(18.0f, 24.0f * sf), ModernTheme::vuInk,
                    "VU", false);
        dl->AddLine(panel.P(cx, pivotY), at(kStart + digitalVu.position * (kEnd - kStart), length * 0.96f),
                    ModernTheme::vuNeedle, 1.5f * sf * s);
        if (digitalVu.peakPosition < digitalVu.position - 0.02f)
        {
            const ImVec2 mark = at(kStart + digitalVu.peakPosition * (kEnd - kStart), length * 0.92f);
            dl->AddCircleFilled(mark, 2.0f * sf * s, ModernTheme::vuPeak, 16);
            dl->AddLine(panel.P(cx, pivotY), mark, ModernTheme::vuPeakLine, 0.5f * sf * s);
        }
        dl->AddCircleFilled(panel.P(cx, pivotY), 3.0f * sf * s, ModernTheme::vuNeedle, 16);
        dl->PopClipRect();
        // Glass: a faint highlight over the top fifth.
        const float gx0 = ix0 + sf, gy0 = iy0 + sf, gx1 = ix1 - sf, gy1 = iy1 - sf;
        const ImVec2 h0 = panel.P(gx0 + 10 * sf, gy0 + 5 * sf), h1 = panel.P(gx1 - 10 * sf, gy0 + (gy1 - gy0) * 0.2f - 5 * sf);
        gradientRect(dl, h0, h1, 3.0f * sf * s, h0, ImVec2(h0.x, h1.y), ModernTheme::vuGlass,
                     ModernTheme::clearWhite);
    }

    // The Digital face's gain-reduction history (GRHistoryGraph::paint): the last 128 readings at 30 Hz (about 4 s)
    // as a filled trace over 0..-30 dB, with the deepest reading marked.
    void drawGrHistory(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        const float s = panel.scale();
        const float sf = std::max(0.5f, std::min((x1 - x0) / 400.0f, (y1 - y0) / 250.0f));
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y1), ModernTheme::historyFrame, 4.0f * sf * s);
        const float ix0 = x0 + 2 * sf, iy0 = y0 + 2 * sf, ix1 = x1 - 2 * sf, iy1 = y1 - 2 * sf;
        gradientRect(dl, panel.P(ix0, iy0), panel.P(ix1, iy1), 3.0f * sf * s, panel.P(ix0, iy0), panel.P(ix0, iy1),
                     ModernTheme::historyTop, ModernTheme::historyBottom);
        // The scale's and the time legends' numerals at 14 (a meter scale), the title at 16.
        constexpr float labelSize = 14.0f;
        const float gx0 = ix0 + 34.0f, gy0 = iy0 + 10.0f, gx1 = ix1 - 10.0f, gy1 = iy1 - 22.0f;
        const auto rowOf = [&](float db) { return gy0 + std::clamp(-db / 30.0f, 0.0f, 1.0f) * (gy1 - gy0); };
        static constexpr float kRows[] = {0, -6, -12, -18, -24, -30};
        for (const float db : kRows)
        {
            const float y = rowOf(db);
            dl->AddRectFilled(panel.P(gx0, y), panel.P(gx1, y + 1),
                              db == 0.0f ? ModernTheme::historyZero : ModernTheme::historyGrid);
            char text[8];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(db));
            panel.text(dl, gx0 - 5.0f, y - 0.5f * labelSize, labelSize,
                       db == 0.0f ? ModernTheme::historyZeroLabel : ModernTheme::historyLabel, text, 1);
        }
        for (int i = 1; i < 4; ++i)
        {
            const float x = gx0 + (gx1 - gx0) * static_cast<float>(i) / 4.0f;
            dl->AddRectFilled(panel.P(x, gy0), panel.P(x + 1, gy1), ModernTheme::historyGrid);
        }
        constexpr int kCount = static_cast<int>(std::tuple_size<decltype(grHistory)>::value);
        ImVec2 trace[kCount];
        float deepest = 0.0f;
        for (int i = 0; i < kCount; ++i)
        {
            const float gr = grHistory[static_cast<size_t>((grHistoryWrite + i) % kCount)];
            deepest = std::min(deepest, gr);
            trace[i] = panel.P(gx0 + (gx1 - gx0) * static_cast<float>(i) / (kCount - 1), rowOf(gr));
        }
        // The fill hangs from the 0 dB line: one trapezoid per step (each convex), shaded as one gradient, without
        // anti-aliased fringes that would seam the joins.
        const ImDrawListFlags flags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        const int firstVertex = dl->VtxBuffer.Size;
        const float zeroRow = panel.P(0, gy0).y;
        for (int i = 0; i + 1 < kCount; ++i)
        {
            const ImVec2 step[4] = {ImVec2(trace[i].x, zeroRow), ImVec2(trace[i + 1].x, zeroRow), trace[i + 1], trace[i]};
            dl->AddConvexPolyFilled(step, 4, IM_COL32_WHITE);
        }
        dl->Flags = flags;
        shadeLinear(dl, firstVertex, panel.P(gx0, gy0), panel.P(gx0, gy1), ModernTheme::historyFillTop,
                    ModernTheme::historyFillBottom);
        dl->AddPolyline(trace, kCount, ModernTheme::historyTraceGlow, 0, 4.0f * sf * s);
        dl->AddPolyline(trace, kCount, ModernTheme::historyTrace, 0, 1.5f * sf * s);
        dl->AddRectFilled(panel.P(gx1, gy0), panel.P(gx1 + 1, gy1), ModernTheme::historyNow);
        if (deepest < -0.5f)
        {
            const float y = rowOf(deepest);
            dl->AddRectFilled(panel.P(gx0, y), panel.P(gx1, y + 1), ModernTheme::historyDeepestLine);
            char text[16];
            std::snprintf(text, sizeof(text), "%.1fdB", static_cast<double>(deepest));
            panel.text(dl, gx1 - 2 * sf, y - 6 * sf - 0.5f * labelSize, labelSize, ModernTheme::historyDeepest, text,
                       1, true);
        }
        // Time legends. The original also prints "~4 sec" over the same corner as "now"; the two overlap there.
        const float legendTop = gy1 + 3.0f;
        panel.text(dl, gx0, legendTop, labelSize, ModernTheme::historyLabel, "-4s", -1);
        panel.text(dl, 0.5f * (gx0 + gx1), legendTop, labelSize, ModernTheme::historyLabel, "-2s", 0);
        panel.text(dl, gx1, legendTop, labelSize, ModernTheme::historyLabel, "now", 1);
        const float tx0 = gx0 + (gx1 - gx0) * 0.2f, tx1 = gx0 + (gx1 - gx0) * 0.8f, ty0 = gy0 + 4.0f;
        const float ty1 = ty0 + 22.0f;
        dl->AddRectFilled(panel.P(tx0, ty0), panel.P(tx1, ty1), ModernTheme::historyTitleBack, 3.0f * s);
        textCentred(dl, 0.5f * (tx0 + tx1), 0.5f * (ty0 + ty1), 16.0f, ModernTheme::historyTitle,
                    "GR HISTORY", true);
    }

    // The sidechain high-pass as the original's vertical slider beside the VU (a LookAndFeel_V4 linear slider):
    // click or drag along the track; a double-click returns the default.
    void sidechainHpSlider(ImDrawList* dl, float x, float trackTop, float trackBottom)
    {
        const float s = panel.scale();
        const uint32_t p = P_SC_HP;
        const ImGuiID widget = ImGui::GetID("##dig_sc_hp");
        markTrackedDrawn(p, widget, true);
        const float lo = hostMinimum(p), hi = hostMaximum(p);
        ImGui::SetCursorScreenPos(panel.P(x - 14.0f, trackTop - 8.0f));
        ImGui::InvisibleButton("##dig_sc_hp", ImVec2(28.0f * s, (trackBottom - trackTop + 16.0f) * s));
        if (ImGui::IsItemActivated())
        {
            trackedEditsOpen[p] = true;
            trackedEditOwners[p] = widget;
            trackedEditsDrawn[p] = true;
            editParameter(p, true);
            sidechainHpReset = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (sidechainHpReset) setParam(p, hostDefaultValue(p));
        }
        if (ImGui::IsItemActive() && !sidechainHpReset && hi > lo)
        {
            const float mouseY = (ImGui::GetIO().MousePos.y - panel.P(0, 0).y) / s;
            const float position = std::clamp((trackBottom - mouseY) / (trackBottom - trackTop), 0.0f, 1.0f);
            const float host = lo + position * (hi - lo);
            if (host != values[p]) setParam(p, host);
        }
        if (ImGui::IsItemDeactivated())
        {
            if (trackedEditsOpen[p]) editParameter(p, false);
            trackedEditsOpen[p] = false;
            sidechainHpReset = false;
        }
        const float position = hi > lo ? std::clamp((values[p] - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        const float thumbY = trackBottom - position * (trackBottom - trackTop);
        dl->AddRectFilled(panel.P(x - 3, trackTop - 3), panel.P(x + 3, trackBottom + 3), ModernTheme::sliderSlot, s);
        dl->AddLine(panel.P(x + 4, trackTop), panel.P(x + 4, trackBottom), ModernTheme::sliderScale, s);
        const ImVec2 t0 = panel.P(x - 10, thumbY - 5), t1 = panel.P(x + 10, thumbY + 5);
        gradientRect(dl, t0, t1, s, t0, ImVec2(t0.x, t1.y), ModernTheme::thumbTop, ModernTheme::thumbBottom);
        dl->AddLine(panel.P(x - 7, thumbY), panel.P(x + 7, thumbY), ModernTheme::thumbLine, s);
        textCentred(dl, x, trackTop - 18.0f, 16.0f, kModernText, "SC HP", true);
        const float hz = knobHostToPlain(values[p], p, this);
        char text[16];
        if (hz < 1.0f)
            std::snprintf(text, sizeof(text), "OFF");
        else
            std::snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(hz));
        textCentred(dl, x, trackBottom + 20.0f, 17.0f, ModernTheme::value, text, true);
    }

    void drawDigital(ImDrawList* dl)
    {
        const float s = panel.scale();
        const float dt = ImGui::GetIO().DeltaTime;
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        drawModernBody(dl, "DIGITAL COMPRESSOR", "PRECISION DYNAMICS");
        drawLevelMeters(dl, dt);
        dl->AddLine(panel.P(Layout::digitalDividerX, Layout::vuTop), panel.P(Layout::digitalDividerX, Layout::captionBottom + 30.0f),
                    ModernTheme::sectionRule, s);

        // Gain reduction: the VU, or its 4-second history; a click on either (or the caption) swaps them.
        const float gr = meter(kMeterMaster);
        multicompp::ui_detail::advanceVuNeedle(digitalVu, gr, dt);
        grHistoryClock = std::min(grHistoryClock + std::max(dt, 0.0f), 0.25f);
        for (; grHistoryClock >= 1.0f / 30.0f; grHistoryClock -= 1.0f / 30.0f)
        {
            grHistory[static_cast<size_t>(grHistoryWrite)] = std::isfinite(gr) ? gr : 0.0f;
            grHistoryWrite = (grHistoryWrite + 1) % static_cast<int>(grHistory.size());
        }
        // Left of centre: the display, its caption and the side-chain high-pass beside it.
        constexpr float vuX0 = Layout::vuX0, vuX1 = Layout::vuX1, vuTop = Layout::vuTop, vuBottom = Layout::vuBottom;
        constexpr float captionBottom = Layout::captionBottom;
        ImGui::SetCursorScreenPos(panel.P(vuX0, vuTop));
        if (ImGui::InvisibleButton("##dig_vu", ImVec2((vuX1 - vuX0) * s, (captionBottom - vuTop) * s)))
            digitalShowsHistory = !digitalShowsHistory;
        if (digitalShowsHistory)
            drawGrHistory(dl, vuX0, vuTop, vuX1, vuBottom);
        else
            drawDigitalVu(dl, vuX0, vuTop, vuX1, vuBottom);
        dl->AddRectFilled(panel.P(vuX0 + 2, vuBottom + 2), panel.P(vuX1 - 2, captionBottom - 2),
                          ModernTheme::captionShade, 3.0f * s);
        const char* caption = digitalShowsHistory ? "HISTORY / CLICK FOR VU" : "VU / CLICK FOR HISTORY";
        constexpr float captionY = 0.5f * (vuBottom + captionBottom);
        textCentred(dl, 0.5f * (vuX0 + vuX1), captionY, 16.0f, kDim, caption, true);
        duskdaf::spacedText(panel, dl, 0.5f * (vuX0 + vuX1), Layout::grLegendY, 16.0f, kModernText, "GAIN REDUCTION", 0,
                            0.10f);
        sidechainHpSlider(dl, Layout::scHpX, Layout::scHpTrackTop, Layout::scHpTrackBottom);

        // Right of it, the original's two rows of knobs: five columns 94 apart.
        constexpr float r = Layout::digitalKnobRadius, row1 = Layout::digitalRow1Y, row2 = Layout::digitalRow2Y;
        const auto column = [](int i) { return Layout::digitalColumnX(i); };
        const auto knobAt = [&](const char* id, uint32_t p, int col, float row, const char* name, int decimals,
                                const char* suffix, bool applicable) {
            faceKnob(dl, id, p, column(col), row, r, name, decimals, suffix, applicable);
        };
        knobAt("dig_thr", P_DIG_THRESHOLD, 0, row1, "THRESHOLD", 1, " dB", true);
        knobAt("dig_ratio", P_DIG_RATIO, 1, row1, "RATIO", 1, ":1", true);
        knobAt("dig_knee", P_DIG_KNEE, 2, row1, "KNEE", 1, " dB", true);
        knobAt("dig_att", P_DIG_ATTACK, 3, row1, "ATTACK", 2, " ms", true);
        knobAt("dig_rel", P_DIG_RELEASE, 4, row1, "RELEASE", 0, " ms", true);
        knobAt("dig_look", P_DIG_LOOK, 0, row2, "LOOKAHEAD", 1, " ms", true);
        knobAt("dig_mix", P_DIG_MIX, 1, row2, "MIX", 0, " %", true);
        knobAt("dig_out", P_DIG_OUT, 2, row2, "OUTPUT", 1, " dB", modernManualOutputAvailable());
        illuminatedToggle(dl, "##dig_adapt", P_DIG_ADAPT, column(3) - 40.0f, row2 - 15.0f, column(4) + 40.0f,
                          row2 + 15.0f, "ADAPTIVE RELEASE");
    }

    // Compact rack switches. A colored underline identifies an active band / bypass / solo;
    // neutral caps and printed legends keep color from overwhelming the faceplate.
    bool bandButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1, const char* label,
                    ImU32 fill, ImU32 textColour, const char* tooltip, bool active, bool enabled = true)
    {
        const float s = panel.scale();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        if (!enabled) ImGui::BeginDisabled();
        ImGui::SetCursorScreenPos(b0);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool down = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tooltip);
        if (!enabled) ImGui::EndDisabled();
        const ImVec2 f0 = panel.P(x0 + 0.5f, y0 + 0.5f), f1 = panel.P(x1 - 0.5f, y1 - 0.5f);
        const ImU32 cap = down ? ModernTheme::buttonDown : hovered ? ModernTheme::buttonHover : ModernTheme::buttonCap;
        gradientRect(dl, f0, f1, 1.5f * s, f0, ImVec2(f0.x, f1.y), cap, ModernTheme::buttonBottom);
        dl->AddRect(f0, f1, active ? fill : ModernTheme::buttonEdge, 1.5f * s, 0, s);
        if (active)
            dl->AddRectFilled(panel.P(x0 + 3, y1 - 3), panel.P(x1 - 3, y1 - 1), fill);
        const ImU32 ink = active ? kModernText : textColour;
        textCentred(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 1, std::min(16.0f, (y1 - y0) - 10.0f),
                    enabled ? ink : colourWithAlpha(ink, 0.55f), label, true);
        return clicked && enabled;
    }

    // One band's gain-reduction meter (MultibandCompressorPanel::drawBandMeter): twenty LEDs lit from the top over
    // 0..-20 dB, warming from the band's colour to red, a white peak, a value pill, the border bright when selected.
    void drawBandMeter(ImDrawList* dl, int band, float x0, float y0, float x1, float y1, bool selected)
    {
        const float s = panel.scale();
        const ImU32 colour = kBandColors[band];
        const auto& m = bandGrMeters[static_cast<size_t>(band)];
        if (selected)
            dl->AddRectFilled(panel.P(x0 - 4, y0 - 4), panel.P(x1 + 4, y1 + 4), colourWithAlpha(colour, 0.2f), 6.0f * s);
        const ImVec2 a = panel.P(x0, y0), b = panel.P(x1, y1);
        gradientRect(dl, a, b, 5.0f * s, a, ImVec2(a.x, b.y), ModernTheme::bandMeterTop,
                     ModernTheme::bandMeterBottom);
        dl->AddRect(panel.P(x0 + 1, y0 + 1), panel.P(x1 - 1, y1 - 1), ModernTheme::bandMeterEdge, 4.0f * s, 0, 1.0f * s);
        static constexpr int kSegments = 20;   // static: read uncaptured by segmentsFor (MSVC C3493)
        constexpr float kGap = 1.5f;
        const float mx0 = x0 + 4, mx1 = x1 - 4, my0 = y0 + 4, my1 = y1 - 4;
        const float segmentHeight = ((my1 - my0) - (kSegments - 1) * kGap) / kSegments;
        const auto segmentsFor = [](float db) {
            return static_cast<int>(std::clamp(-db / 20.0f, 0.0f, 1.0f) * static_cast<float>(kSegments));
        };
        const int lit = segmentsFor(m.shownDb), peak = segmentsFor(m.peakDb);
        for (int seg = 0; seg < kSegments; ++seg)
        {
            const float top = my0 + static_cast<float>(seg) * (segmentHeight + kGap);
            const ImVec2 s0 = panel.P(mx0, top), s1 = panel.P(mx1, top + segmentHeight);
            if (seg < lit)
            {
                const float intensity = static_cast<float>(seg) / kSegments;
                const ImU32 c = intensity < 0.4f ? colourBrighter(colour, 0.2f)
                    : intensity < 0.6f ? colourMix(colour, ModernTheme::bandYellow, (intensity - 0.4f) * 5.0f)
                    : intensity < 0.8f ? colourMix(ModernTheme::bandYellow, ModernTheme::bandOrange, (intensity - 0.6f) * 5.0f)
                    : colourMix(ModernTheme::bandOrange, ModernTheme::bandRed, (intensity - 0.8f) * 5.0f);
                gradientRect(dl, s0, s1, 2.0f * s, s0, ImVec2(s0.x, s1.y), colourBrighter(c, 0.4f), colourDarker(c, 0.1f));
                dl->AddRectFilled(panel.P(mx0 + 2, top + 1), panel.P(mx1 - 2, top + 2.5f),
                                  colourWithAlpha(colourBrighter(c, 0.7f), 0.4f));
            }
            else if (seg == peak && peak > 0)
            {
                dl->AddRectFilled(panel.P(mx0 - 1, top - 1), panel.P(mx1 + 1, top + segmentHeight + 1),
                                  ModernTheme::bandPeakGlow, 3.0f * s);
                dl->AddRectFilled(s0, s1, ModernTheme::bandPeak, 2.0f * s);
            }
            else
            {
                dl->AddRectFilled(s0, s1, ModernTheme::bandUnlit, 2.0f * s);
                dl->AddRectFilled(panel.P(mx0 + 2, top + 1), panel.P(mx1 - 2, top + 2), ModernTheme::bandUnlitHighlight);
            }
        }
        dl->AddRect(a, b, colourWithAlpha(colour, selected ? 1.0f : 0.5f), 5.0f * s, 0, (selected ? 2.5f : 1.5f) * s);
        const float px0 = x0 + 6, px1 = x1 - 6, py0 = y1 - 26, py1 = y1 - 4;
        const ImVec2 p0 = panel.P(px0, py0), p1 = panel.P(px1, py1);
        gradientRect(dl, p0, p1, 4.0f * s, p0, ImVec2(p0.x, p1.y), ModernTheme::pillTop,
                     ModernTheme::pillBottom);
        dl->AddRect(p0, p1, colourWithAlpha(colour, 0.5f), 4.0f * s, 0, 1.0f * s);
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(m.shownDb));
        const float cx = 0.5f * (px0 + px1), cy = 0.5f * (py0 + py1);
        textCentred(dl, cx + 1, cy + 1, 17.0f, ModernTheme::valueShadow, text, true);
        textCentred(dl, cx, cy, 17.0f, ModernTheme::value, text, true);
    }

    // A crossover handle between two band meters (CrossoverFader): a track in the two bands' colours and a grip on
    // a logarithmic frequency axis. Drag up to raise the frequency (the whole travel spans the control's range);
    // a double-click returns the default.
    void crossoverFader(ImDrawList* dl, uint32_t p, float centreX, float top, float bottom, ImU32 lowColour,
                        ImU32 highColour)
    {
        const float s = panel.scale();
        const float minHz = plainValueForHost(p, hostMinimum(p)), maxHz = plainValueForHost(p, hostMaximum(p));
        const float trackTop = top, trackBottom = bottom - 22.0f;
        ImGui::PushID(static_cast<int>(p));
        const ImGuiID widget = ImGui::GetID("##crossover");
        markTrackedDrawn(p, widget, true);
        ImGui::SetCursorScreenPos(panel.P(centreX - 35.0f, top));
        ImGui::InvisibleButton("##crossover", ImVec2(70.0f * s, (bottom - top) * s));
        ImGui::PopID();
        if (ImGui::IsItemActivated())
        {
            trackedEditsOpen[p] = true;
            trackedEditOwners[p] = widget;
            trackedEditsDrawn[p] = true;
            editParameter(p, true);
            draggedCrossover = p;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) setParam(p, hostDefaultValue(p));
            crossoverDragPosition = multicompp::ui_detail::crossoverFaderPosition(
                plainValueForHost(p, values[p]), minHz, maxHz);
        }
        const float deltaY = ImGui::GetIO().MouseDelta.y / s;
        if (ImGui::IsItemActive() && deltaY != 0.0f)
        {
            // CrossoverFader::mouseDrag: the travel's height in pixels spans the whole logarithmic range.
            crossoverDragPosition = std::clamp(crossoverDragPosition - deltaY / (trackBottom - (trackTop + 4.0f)),
                                               0.0f, 1.0f);
            setParam(p, hostValueForPlain(p, multicompp::ui_detail::crossoverFaderFrequency(crossoverDragPosition,
                                                                                            minHz, maxHz)));
        }
        if (ImGui::IsItemDeactivated())
        {
            if (trackedEditsOpen[p]) editParameter(p, false);
            trackedEditsOpen[p] = false;
            // Released: the pending write has had the gesture to land, and the
            // next refresh adopts whatever ordering the DSP settled on.
            if (draggedCrossover == p) draggedCrossover = kNoCrossover;
        }
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        dl->AddRectFilled(panel.P(centreX - 6, trackTop), panel.P(centreX + 6, trackBottom),
                          ModernTheme::faderTrack, 4.0f * s);
        const ImVec2 g0 = panel.P(centreX - 4, trackTop + 2), g1 = panel.P(centreX + 4, trackBottom - 2);
        gradientRect(dl, g0, g1, 3.0f * s, g0, ImVec2(g0.x, g1.y), colourWithAlpha(highColour, 0.3f),
                     colourWithAlpha(lowColour, 0.3f));
        const float hz = plainValueForHost(p, values[p]);
        // The grip's travel is inset by half its height, so it stays inside the fader at either end.
        const float travelTop = trackTop + 9.0f, travelBottom = trackBottom - 9.0f;
        const float gripY = travelBottom - multicompp::ui_detail::crossoverFaderPosition(hz, minHz, maxHz)
                                               * (travelBottom - travelTop);
        const float gx0 = centreX - 13.0f, gy0 = gripY - 9.0f;
        dl->AddRectFilled(panel.P(gx0 + 1, gy0 + 2), panel.P(gx0 + 27, gy0 + 20), ModernTheme::gripShadow, 4.0f * s);
        const ImU32 grip = hot ? kDigitalCyan : ModernTheme::grip;
        const ImVec2 t0 = panel.P(gx0, gy0), t1 = panel.P(gx0 + 26, gy0 + 18);
        gradientRect(dl, t0, t1, 4.0f * s, t0, ImVec2(t0.x, t1.y), colourBrighter(grip, 0.3f), colourDarker(grip, 0.2f));
        for (int i = -1; i <= 1; ++i)
        {
            const float y = gripY + 4.0f * static_cast<float>(i);
            dl->AddRectFilled(panel.P(gx0 + 6, y), panel.P(gx0 + 20, y + 1), ModernTheme::gripLine);
        }
        dl->AddRect(t0, t1, hot ? kDigitalCyan : ModernTheme::gripEdge, 4.0f * s, 0, 1.5f * s);
        if (hot)
        {
            const ImU32 arrow = colourWithAlpha(kDigitalCyan, 0.8f);
            dl->AddTriangleFilled(panel.P(centreX, trackTop - 2), panel.P(centreX + 5, trackTop + 5),
                                  panel.P(centreX - 5, trackTop + 5), arrow);
            dl->AddTriangleFilled(panel.P(centreX, trackBottom + 2), panel.P(centreX - 5, trackBottom - 5),
                                  panel.P(centreX + 5, trackBottom - 5), arrow);
        }
        char text[24];
        if (hz >= 1000.0f)
            std::snprintf(text, sizeof(text), "%.1f kHz", static_cast<double>(hz / 1000.0f));
        else
            std::snprintf(text, sizeof(text), "%d Hz", static_cast<int>(hz));
        textCentred(dl, centreX, bottom - 10.0f, 17.0f, hot ? kDigitalCyan : ModernTheme::faderLabel, text, true);
    }

    void drawMultiband(ImDrawList* dl)
    {
        const float s = panel.scale();
        const float dt = ImGui::GetIO().DeltaTime;
        using Layout = multicompp::ui_detail::ModernFaceLayout;
        drawModernBody(dl, "MULTIBAND COMPRESSOR", "FOUR-BAND DYNAMICS");
        drawLevelMeters(dl, dt);
        dl->AddLine(panel.P(Layout::multibandDividerX, Layout::enableY0), panel.P(Layout::multibandDividerX, Layout::faderBottom),
                    ModernTheme::sectionRule, s);

        bool enabled[4] = {};
        int enabledCount = 0;
        for (int b = 0; b < 4; ++b)
        {
            enabled[b] = values[static_cast<size_t>(multicompp::kBandBase + b * 8 + 7)] > 0.5f;
            enabledCount += enabled[b] ? 1 : 0;
            multicompp::ui_detail::advanceBandGrMeter(bandGrMeters[static_cast<size_t>(b)],
                                                     meter(kMeterBand0 + static_cast<uint32_t>(b)), dt);
        }
        const unsigned shown = multicompp::ui_detail::multibandShownBands(enabled);
        int slots[4] = {};
        int slotCount = 0;
        for (int b = 0; b < 4; ++b)
            if ((shown & (1u << b)) != 0) slots[slotCount++] = b;
        if ((shown & (1u << selectedBand)) == 0) selectedBand = slots[0];

        // Left, the bands. Enable strip: all four bands at fixed quarters, so a hidden band stays reachable; the last
        // two enabled bands are locked on.
        constexpr float sectionLeft = Layout::bandsX0, sectionRight = Layout::bandsX1;
        constexpr float stripWidth = (sectionRight - sectionLeft) / 4.0f;
        for (int b = 0; b < 4; ++b)
        {
            const uint32_t enable = static_cast<uint32_t>(multicompp::kBandBase + b * 8 + 7);
            const bool locked = enabled[b] && enabledCount <= 2;
            char tooltip[64];
            if (locked)
                std::snprintf(tooltip, sizeof(tooltip), "Multiband requires at least 2 enabled bands");
            else
                std::snprintf(tooltip, sizeof(tooltip), "Enable / disable %s band", bandName(b));
            ImGui::PushID(b);
            const float x0 = sectionLeft + static_cast<float>(b) * stripWidth;
            if (bandButton(dl, "##mb_enable", x0 + 2.5f, Layout::enableY0, x0 + stripWidth - 2.5f, Layout::enableY1,
                               bandName(b), enabled[b] ? kBandColors[b] : ModernTheme::bandOff,
                               kModernText, tooltip, enabled[b], !locked))
            {
                // Clear Solo first, while its control is still visible. Host
                // automation/restored disabled Solo is also ignored by the DSP.
                const uint32_t solo = enable - 1;
                if (enabled[b] && values[solo] > 0.5f) setHostValue(solo, 0.0f);
                setHostValue(enable, enabled[b] ? 0.0f : 1.0f);
            }
            ImGui::PopID();
        }
        panel.text(dl, 0.5f * (sectionLeft + sectionRight), Layout::bandLegendY, 16.0f, kDim, "GAIN REDUCTION", 0,
                   true);

        // dB scale beside the meters, its numerals at 14 (a meter scale); -3 keeps its tick only, as at 14 it meets 0.
        constexpr float meterTop = Layout::bandMeterTop, meterBottom = Layout::bandMeterBottom;
        constexpr float faderBottom = Layout::faderBottom;
        for (const float db : {0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f})
        {
            const float y = meterTop + (-db / 20.0f) * (meterBottom - meterTop);
            dl->AddRectFilled(panel.P(sectionLeft - 11, y), panel.P(sectionLeft - 5, y + 1), ModernTheme::scaleTick);
            if (db == -3.0f) continue;
            char text[8];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(db));
            panel.text(dl, sectionLeft - 13, y - 7.0f, 14.0f, ModernTheme::scaleLabel, text, 1);
        }

        // Band slots: only the bands shown share the width.
        const float slotWidth = (sectionRight - sectionLeft) / static_cast<float>(slotCount);
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const int band = slots[slot];
            const float sx = sectionLeft + static_cast<float>(slot) * slotWidth;
            dl->AddRectFilled(panel.P(sx + 1, Layout::tabY0 - 2.0f), panel.P(sx + slotWidth - 1, faderBottom),
                              colourWithAlpha(kBandColors[band], band == selectedBand ? 0.06f : 0.02f));
        }
        for (int slot = 0; slot < slotCount; ++slot)
        {
            const int band = slots[slot];
            const uint32_t base = static_cast<uint32_t>(multicompp::kBandBase + band * 8);
            const float sx = sectionLeft + static_cast<float>(slot) * slotWidth;
            const float mx0 = sx + 8.0f, mx1 = sx + slotWidth - 8.0f;
            constexpr float buttonTop = Layout::tabY0, buttonBottom = Layout::tabY1;
            constexpr float smallButton = Layout::bandSwitchW, gap = Layout::bandSwitchGap;
            const float tabRight = mx1 - 2.0f * (smallButton + gap);
            ImGui::PushID(band);
            if (bandButton(dl, "##mb_tab", mx0, buttonTop, tabRight, buttonBottom, bandName(band),
                               band == selectedBand ? kBandColors[band] : colourWithAlpha(kBandColors[band], 0.3f),
                               kModernText, nullptr, band == selectedBand))
                selectedBand = band;
            char tooltip[48];
            const bool bypassed = values[base + 5] > 0.5f, soloed = values[base + 6] > 0.5f;
            std::snprintf(tooltip, sizeof(tooltip), "Bypass %s band", bandName(band));
            if (bandButton(dl, "##mb_bypass", tabRight + gap, buttonTop, tabRight + gap + smallButton,
                               buttonBottom, "B",
                               bypassed ? ModernTheme::bypassLit : ModernTheme::switchOff,
                               bypassed ? ModernTheme::switchLegendLit : ModernTheme::switchLegend, tooltip, bypassed))
                setHostValue(base + 5, bypassed ? 0.0f : 1.0f);
            std::snprintf(tooltip, sizeof(tooltip), "Solo %s band", bandName(band));
            if (bandButton(dl, "##mb_solo", mx1 - smallButton, buttonTop, mx1, buttonBottom, "S",
                               soloed ? ModernTheme::soloLit : ModernTheme::switchOff,
                               soloed ? ModernTheme::switchLegendLit : ModernTheme::switchLegend, tooltip, soloed))
                setHostValue(base + 6, soloed ? 0.0f : 1.0f);
            ImGui::PopID();
            drawBandMeter(dl, band, mx0, meterTop, mx1, meterBottom, band == selectedBand);
        }
        // A crossover handle on each boundary between neighbouring shown bands: the crossover below the upper band.
        for (int slot = 0; slot + 1 < slotCount; ++slot)
        {
            const int boundary = slots[slot + 1] - 1;
            crossoverFader(dl, P_X1 + static_cast<uint32_t>(boundary),
                           Layout::bandBoundaryX(slot, slotCount), meterTop, faderBottom,
                           kBandColors[boundary], kBandColors[boundary + 1]);
        }

        // Right, the selected band: its tab over its knobs.
        const ImU32 bandColour = kBandColors[selectedBand];
        constexpr float knobsCentre = Layout::multibandKnobX(0.0f);
        constexpr float tagLeft = Layout::multibandKnobX(-1.75f), tagRight = Layout::multibandKnobX(1.75f);
        dl->AddLine(panel.P(tagLeft, Layout::bandTagRuleY), panel.P(tagRight, Layout::bandTagRuleY),
                    colourWithAlpha(bandColour, 0.6f), s);
        char tag[24];
        std::snprintf(tag, sizeof(tag), "%s BAND", bandName(selectedBand));
        panel.text(dl, knobsCentre, Layout::bandTagY, 16.0f, bandColour, tag, 0, true);

        // Seven knobs: the selected band's four timing and level controls, then its makeup with the multiband output
        // and mix, arranged in two rows with the same controls as Digital.
        const uint32_t base = static_cast<uint32_t>(multicompp::kBandBase + selectedBand * 8);
        constexpr float knobR = Layout::multibandKnobRadius, row1 = Layout::multibandRow1Y, row2 = Layout::multibandRow2Y;
        const auto knobAt = [&](const char* id, uint32_t p, float column, float row, const char* name, int decimals,
                                const char* suffix, bool applicable) {
            faceKnob(dl, id, p, Layout::multibandKnobX(column), row, knobR, name, decimals, suffix, applicable);
        };
        knobAt("mb_thr", base, -1.5f, row1, "THRESHOLD", 1, " dB", true);
        knobAt("mb_ratio", base + 1, -0.5f, row1, "RATIO", 1, ":1", true);
        knobAt("mb_att", base + 2, 0.5f, row1, "ATTACK", 1, " ms", true);
        knobAt("mb_rel", base + 3, 1.5f, row1, "RELEASE", 0, " ms", true);
        knobAt("mb_makeup", base + 4, -1.0f, row2, "MAKE-UP", 1, " dB", true);
        knobAt("mb_out", P_MB_OUT, 0.0f, row2, "OUTPUT", 1, " dB", modernManualOutputAvailable());
        knobAt("mb_mix", P_MB_MIX, 1.0f, row2, "MIX", 0, "%", true);
    }

    // Clears the Digital and Multiband faces' meters when one of them comes into view, so nothing replays from the
    // last time it was shown.
    void resetModernFaces()
    {
        inputLadder.reset();
        outputLadder.reset();
        inputReadout = {};
        outputReadout = {};
        digitalVu = {};
        grHistory.fill(0.0f);
        grHistoryWrite = 0;
        grHistoryClock = 0.0f;
        bandGrMeters = {};
    }

    float meter(uint32_t p) const
    {
        if (p >= values.size()) return 0.0f;
        void* instance = getPluginInstancePointer();
        if (instance != nullptr)
        {
            if (p == kMeterMaster && multiCompGetGainReduction != nullptr) return multiCompGetGainReduction(instance);
            if (p >= kMeterBand0 && p <= kMeterBand0 + 3 && multiCompGetBandGainReduction != nullptr)
                return multiCompGetBandGainReduction(instance, static_cast<int>(p - kMeterBand0));
        }
        return values[p];
    }

    // The Opto and FET needles' VU movement (MultiCompGainReductionNeedle.hpp).
    // A UI without the plugin instance (split LV2) draws the raw reading instead.
    float needleGainReduction() const
    {
        void* const instance = getPluginInstancePointer();
        return instance != nullptr && multiCompGetGainReductionNeedle != nullptr
            ? multiCompGetGainReductionNeedle(instance) : meter(kMeterMaster);
    }

    // The needles live in the plugin; a UI without the instance (split LV2) has none.
    bool hasNeedles() const
    {
        return getPluginInstancePointer() != nullptr && multiCompGetVcaGainReductionNeedle != nullptr
            && multiCompGetGainReductionNeedle != nullptr;
    }

    // The VCA face's gain-change needle (kVcaNeedle). Without the instance the raw
    // reading still goes through the dial's law, so the face reads the same scale.
    float vcaNeedleGainReduction() const
    {
        void* const instance = getPluginInstancePointer();
        return instance != nullptr && multiCompGetVcaGainReductionNeedle != nullptr
            ? multiCompGetVcaGainReductionNeedle(instance)
            : static_cast<float>(multicompp::dialReading(multicompp::kVcaNeedle, meter(kMeterMaster)));
    }

    void drawMeter(ImDrawList* dl, float x, float y, float w, float h, float gr, ImU32 color, const char* label)
    {
        dl->AddRectFilled(panel.P(x, y), panel.P(x + w, y + h), IM_COL32(12, 13, 16, 255), 3 * panel.scale());
        const float amount = std::clamp(-gr / 20.0f, 0.0f, 1.0f);
        constexpr int segments = 16;
        const float gap = 2.0f;
        const float sh = (h - gap * (segments - 1) - 8) / segments;
        for (int i = 0; i < segments; ++i)
        {
            const float sy = y + h - 5 - (i + 1) * sh - i * gap;
            const bool lit = static_cast<float>(i) < amount * segments;
            const ImU32 col = lit ? (i > 12 ? IM_COL32(236, 72, 64, 255) : i > 8 ? IM_COL32(241, 184, 66, 255) : color)
                                  : IM_COL32(43, 45, 51, 255);
            dl->AddRectFilled(panel.P(x + 4, sy), panel.P(x + w - 4, sy + sh), col, 1.5f * panel.scale());
        }
        panel.text(dl, x + w * 0.5f, y + h + 5, 9, kDim, label, 0, true);
        char text[24]; std::snprintf(text, sizeof(text), "%.1f dB", static_cast<double>(gr));
        panel.text(dl, x + w * 0.5f, y + h + 18, 9, kText, text, 0);
    }

    void applyPreset(int index)
    {
        if (index < 0 || index >= static_cast<int>(multicompp::kFactoryPresets.size())) return;
        multicompp::applyFactoryPresetToHostParameters(
            static_cast<uint32_t>(index),
            [this](int parameterIndex, float hostValue)
            {
                setHostValue(static_cast<uint32_t>(parameterIndex), hostValue);
            });
        currentPreset = index;
        currentUserName.clear();
        currentUserPath.clear();
        defaultsActive = false;
    }
};

UI* createUI() { return new MultiCompUI(); }

END_NAMESPACE_DAF

#endif // MULTICOMP_UI_LOGIC_TEST
