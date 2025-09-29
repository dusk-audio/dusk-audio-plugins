# Room Reverb Late Level Fix - Summary

## Problem
The Room reverb algorithm in StudioReverb was producing no output when the Late Level knob was turned up, while Hall, Plate, and Early Reflections algorithms worked correctly.

## Root Cause
The progenitor2 reverb processor (used for Room algorithm) requires specific initialization:
1. Must call `setReverbType(2)` - an undocumented value used by ElephantDSP RoomReverb
2. Must use `setwetr()` for wet level instead of `setwet()`
3. The `getwet()` function returns misleading 0.0 even when working correctly

## Solution Applied

### Key Changes in DragonflyReverb.cpp:

1. **In constructor (line 78):**
```cpp
room.setReverbType(2);  // Use exact value from working RoomReverb implementation
```

2. **In prepare() method (line 152):**
```cpp
room.setReverbType(2);   // MUST be called BEFORE wet/dry settings
room.setwetr(1.0f);       // Raw multiplier = 1.0
room.setdryr(-90.0f);     // Mute dry signal
```

3. **In updateRoomReverb() (lines 459-460):**
```cpp
room.setwetr(1.0f);   // Raw multiplier (getwet() will return 0 but it works)
room.setdryr(-90.0f); // Mute dry signal
```

## Verification
- Room reverb now produces output: ~0.01 average magnitude
- Confirmed with standalone executable testing
- Debug output shows "Output from late reverb: YES"

## Technical Notes
- The magic value `setReverbType(2)` doesn't match any documented FV3_REVTYPE constants
- ElephantDSP RoomReverb source confirms this is the correct value
- The progenitor2 processor has naturally low output (~-40dB)
- Using `setwetr(1.0f)` provides unity gain despite `getwet()` showing 0

## Files Modified
- `/home/marc/projects/plugins/plugins/StudioReverb/Source/DSP/DragonflyReverb.cpp`

## Test Command
```bash
./test_reverb_final.sh  # Validates Room reverb output
```

## Result
✅ Room reverb Late Level now works correctly!