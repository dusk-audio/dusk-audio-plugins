// SPDX-License-Identifier: GPL-3.0-or-later

// CabinetLibrary.h — registry of bundled cabinet IRs
//
// IRs are embedded into the plugin binary via juce_add_binary_data (see
// plugins/DuskAmp/CMakeLists.txt resources/cabs/ glob). At runtime the
// CAB_PRESET APVTS choice param selects one of the bundled cabinets;
// CabinetLibrary::loadInto() resolves the BinaryData symbol and pushes
// the bytes into a CabinetIR via its memory-load API.
//
// The display-name list (kCabinetCount entries) is the source of truth
// for the choice param — keep it consistent with the actual files in
// resources/cabs/. If a referenced file is missing at build time the
// entry is still listed but loadInto() falls back to "no cab loaded".

#pragma once

#include <juce_core/juce_core.h>

class CabinetIR;

namespace CabinetLibrary
{
    // Stable index into the choice param. Index 0 is reserved for "(none)";
    // user-loaded IRs use a separate path (CabinetIR::loadIR with a file).
    enum CabinetId
    {
        None = 0,
        FenderTwin_2x12_SM57,
        Marshall_1960A_V30_SM57,
        Marshall_1960A_Greenback_OffAxis,
        Vox_AC30_2x12_Ribbon,
        Mesa_4x12_V30_Blend,
        // Add new entries here AND in cabinetEntries() below.
        Count
    };

    struct Entry
    {
        CabinetId   id;
        const char* displayName;   // shown in the host's choice list
        const char* binarySymbol;  // BinaryData symbol stem (no extension)
                                   //   e.g. "fender_twin_2x12_sm57_oa"
                                   // resolves to BinaryData::fender_twin_2x12_sm57_oa_wav
                                   //         and BinaryData::fender_twin_2x12_sm57_oa_wavSize
    };

    /** Returns the static catalog of known cabinets. */
    juce::Span<const Entry> entries();

    /** Number of entries in the choice list, including "(none)". */
    int numChoices();

    /** Display name for a choice index (0..numChoices()-1). */
    juce::String displayNameForChoice (int choiceIndex);

    /** Loads the IR for the given choice into the supplied CabinetIR.
        Choice 0 ("none") is a no-op. If the binary symbol isn't present
        (file wasn't dropped into resources/cabs/ at build time) the call
        is also a no-op — the existing IR stays loaded. Returns true on
        successful load. */
    bool loadInto (CabinetIR& cabinet, int choiceIndex);
}
