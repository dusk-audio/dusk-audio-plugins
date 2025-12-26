/*
  ==============================================================================

    RE-201 Space Echo - Color Palette
    UAD Galaxy-style professional hardware emulation colors
    Copyright (c) 2025 Luna Co. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace RE201Colours
{
    // =========================================================================
    // BRUSHED ALUMINUM FACEPLATE
    // =========================================================================
    const juce::Colour aluminumLight    { 0xFFD8D8D8 };
    const juce::Colour aluminumMid      { 0xFFC0C0C0 };
    const juce::Colour aluminumDark     { 0xFFA0A0A0 };
    const juce::Colour aluminumShadow   { 0xFF808080 };
    const juce::Colour aluminumHighlight{ 0xFFE8E8E8 };

    // =========================================================================
    // BLACK RECESSED FRAME
    // =========================================================================
    const juce::Colour frameBlack       { 0xFF1A1A1A };
    const juce::Colour frameHighlight   { 0xFF3A3A3A };
    const juce::Colour frameShadow      { 0xFF0A0A0A };

    // =========================================================================
    // GREEN CONTROL PANEL (Muted olive like UAD Galaxy)
    // =========================================================================
    const juce::Colour panelGreen       { 0xFF4D6B3D };  // Muted olive green
    const juce::Colour panelGreenDark   { 0xFF3D5A2D };  // Darker olive
    const juce::Colour panelGreenLight  { 0xFF5D7B4D };  // Lighter olive
    const juce::Colour panelGreenShadow { 0xFF2D4A1D };  // Deep olive shadow

    // =========================================================================
    // CHROME KNOBS
    // =========================================================================
    const juce::Colour chromeWhite      { 0xFFF0F0F0 };  // Specular highlight
    const juce::Colour chromeLight      { 0xFFD0D0D0 };
    const juce::Colour chromeMid        { 0xFFB0B0B0 };
    const juce::Colour chromeDark       { 0xFF707070 };
    const juce::Colour chromeEdge       { 0xFF505050 };
    const juce::Colour chromeShadow     { 0xFF404040 };

    // Aliases for compatibility
    const juce::Colour chromeHighlight  { 0xFFF0F0F0 };
    const juce::Colour chromeLow        { 0xFF707070 };
    const juce::Colour chromeRim        { 0xFF606060 };

    // =========================================================================
    // MODE SELECTOR / HEAD SELECT
    // =========================================================================
    const juce::Colour selectorRingCream{ 0xFFE8E8E0 };  // Cream outer ring
    const juce::Colour selectorRingDark { 0xFFD0D0C8 };  // Ring shadow
    const juce::Colour selectorTextDark { 0xFF2A2A2A };  // Text on ring
    const juce::Colour selectorRecess   { 0xFF151515 };  // Dark center recess
    const juce::Colour chickenHead      { 0xFFE0D8C8 };  // Cream knob
    const juce::Colour chickenHeadDark  { 0xFFB0A890 };

    // Legacy selector colors
    const juce::Colour selectorBg       { 0xFF151515 };
    const juce::Colour selectorRing     { 0xFFE8E8E0 };

    // =========================================================================
    // TEXT COLORS
    // =========================================================================
    const juce::Colour textWhite        { 0xFFFFFFFF };
    const juce::Colour textLight        { 0xFFE0E0E0 };
    const juce::Colour textShadow       { 0xFF1A3A1A };  // Shadow on green panel
    const juce::Colour textOnAluminum   { 0xFF2A2A2A };  // Dark text on aluminum

    // Legacy text names
    const juce::Colour labelText        { 0xFFFFFFFF };
    const juce::Colour labelShadow      { 0xFF1A3A1A };
    const juce::Colour headerText       { 0xFF2A2A2A };

    // =========================================================================
    // LEDs AND VU METER
    // =========================================================================
    const juce::Colour ledGreenOn       { 0xFF00DD00 };
    const juce::Colour ledOrangeOn      { 0xFFDDAA00 };
    const juce::Colour ledRedOn         { 0xFFDD0000 };
    const juce::Colour ledRedBright     { 0xFFFF2020 };
    const juce::Colour ledOff           { 0xFF401010 };
    const juce::Colour ledGreenOff      { 0xFF104010 };
    const juce::Colour vuBackground     { 0xFF1A1A1A };
    const juce::Colour vuBezel          { 0xFF2A2A2A };

    // Legacy LED names
    const juce::Colour ledGreen         { 0xFF00DD00 };
    const juce::Colour ledRed           { 0xFFDD0000 };
    const juce::Colour ledGlow          { 0x4400FF00 };

    // =========================================================================
    // VU METER (Legacy cream face style - kept for compatibility)
    // =========================================================================
    const juce::Colour vuFace           { 0xFFF5F0E6 };
    const juce::Colour vuNeedle         { 0xFF1A1A1A };
    const juce::Colour vuScaleGreen     { 0xFF2D5A2D };
    const juce::Colour vuScaleRed       { 0xFFAA3333 };
    const juce::Colour vuGreen          { 0xFF2D5A2D };
    const juce::Colour vuRed            { 0xFFAA3333 };
    const juce::Colour vuText           { 0xFF2A2A2A };
    const juce::Colour vuShadow         { 0xFF3A3A3A };

    // =========================================================================
    // TOGGLE SWITCHES
    // =========================================================================
    const juce::Colour toggleChrome     { 0xFFB8B8B8 };
    const juce::Colour toggleShadow     { 0xFF606060 };
    const juce::Colour togglePlate      { 0xFF2A2A2A };
    const juce::Colour toggleSlot       { 0xFF1A1A1A };

    // =========================================================================
    // SCREWS
    // =========================================================================
    const juce::Colour screwHead        { 0xFFA0A0A0 };
    const juce::Colour screwSlot        { 0xFF404040 };
    const juce::Colour screwHighlight   { 0xFFC0C0C0 };
    const juce::Colour screwShadow      { 0xFF606060 };

    // =========================================================================
    // GENERAL
    // =========================================================================
    const juce::Colour background       { 0xFF1A1A1A };
    const juce::Colour shadow           { 0x80000000 };

    // Accent colors
    const juce::Colour textGreen        { 0xFF8FBC8F };
    const juce::Colour accentGreen      { 0xFF6B8E4E };
    const juce::Colour sectionGreen     { 0xFF1D5A22 };
    const juce::Colour headerDark       { 0xFF1A1A1A };
}
