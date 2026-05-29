#pragma once

#include <juce_core/juce_core.h>

#ifndef DUSKAMP_NO_BUNDLED_IRS
 #include <BinaryData.h>
#endif

namespace DuskAmpDefaults
{
    // A bundled cabinet IR — pointer + size into BinaryData (compile-time
    // embedded, lifetime = process). `displayName` is shown in the editor.
    struct BundledIR
    {
        const char*        data;
        int                size;
        const char*        displayName;
    };

    // Per-amp default cab IR. Returns a BundledIR with data == nullptr if
    // no default exists for that index (caller should skip the load).
    // TONE_TYPE indices:
    //   0 = American (Fender)  → Twin Reverb 1x12, SM57 cap-A
    //   1 = British  (Marshall)→ JCM800 4x12, NT1-A cap-B
    //   2 = AC       (Vox)     → AC30 2x12 Celestion Blue
    inline BundledIR getDefaultIRForToneType (int toneType)
    {
       #ifndef DUSKAMP_NO_BUNDLED_IRS
        switch (toneType)
        {
            case 0:
                return { BinaryData::Twin_Reverb_SM57_A_wav,
                         BinaryData::Twin_Reverb_SM57_A_wavSize,
                         "Twin Reverb (SM57)" };
            case 1:
                return { BinaryData::JCM800_NT1A_B_wav,
                         BinaryData::JCM800_NT1A_B_wavSize,
                         "JCM800 (NT1-A)" };
            case 2:
                return { BinaryData::AC30_Blue_1_wav,
                         BinaryData::AC30_Blue_1_wavSize,
                         "AC30 Blue" };
            default:
                return { nullptr, 0, "" };
        }
       #else
        juce::ignoreUnused (toneType);
        return { nullptr, 0, "" };
       #endif
    }
}
