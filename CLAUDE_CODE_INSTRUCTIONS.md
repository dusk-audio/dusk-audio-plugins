# TapeMachine Aliasing Fix - Instructions for Claude Code

## CRITICAL: Your Previous Fixes Did Not Work

The spectrum analyzer shows aliasing peaks at -40 to -60 dB. Your test results claiming -98 dB to -162 dB are **not measuring actual plugin output**. You are calculating theoretical filter attenuation, not running real audio through the compiled plugin.

## What You Must NOT Do

1. **DO NOT reduce saturation on high frequencies** - The "HF Content Detector" approach makes the plugin sound dull. This is unacceptable. Remove it.

2. **DO NOT pre-filter the input** - Lowpassing before saturation removes brightness from the source material.

3. **DO NOT run theoretical tests** - Calculating expected filter attenuation mathematically is useless. You must process actual audio and measure the actual output.

## What You MUST Do

### Step 1: Audit the Signal Flow

Find and list EVERY nonlinear processing stage in the plugin:
- Saturation curves
- Hysteresis modeling  
- Bias circuit
- Tape compression
- Any waveshaping
- Any lookup tables

Each of these MUST run at the oversampled rate.

### Step 2: Verify Oversampling Is Actually Happening

Add debug output that prints:
```cpp
std::cout << "Processing at sample rate: " << currentSampleRate << std::endl;
std::cout << "Oversampling factor: " << oversamplingFactor << std::endl;
std::cout << "Effective rate for saturation: " << effectiveRate << std::endl;
```

Confirm that when HQ is set to 4x, saturation is actually running at 176.4kHz (not 44.1kHz).

### Step 3: Check the Anti-Aliasing Filter

The anti-aliasing filter must:
- Run at the oversampled rate (176.4kHz for 4x)
- Have cutoff at approximately 19-20kHz (original Nyquist)
- Be at least 8th order (48dB/octave) or higher
- Apply AFTER all nonlinear processing but BEFORE decimation

### Step 4: Check for Processing After Downsampling

Any nonlinear processing that happens AFTER downsampling will create aliasing that cannot be removed. Common culprits:
- Noise generator (if generating at base rate)
- Output soft-clipping or limiting
- "Character" or "color" stages
- Metering that feeds back into signal path

### Step 5: Implement Real Testing

Use the RealAliasingTest class from TapeMachineAliasingFix.h or write equivalent:

```cpp
// This is what a REAL test looks like
void runRealAliasingTest() {
    // 1. Create test buffer
    const int numSamples = 32768;
    std::vector<float> buffer(numSamples);
    
    // 2. Generate 18.2kHz sine at +8.3dB
    float freq = 18200.0f;
    float gain = std::pow(10.0f, 8.3f / 20.0f);
    for (int i = 0; i < numSamples; i++) {
        buffer[i] = std::sin(2.0f * M_PI * freq * i / 44100.0f) * gain;
    }
    
    // 3. Process through ACTUAL PLUGIN
    myPlugin.processBlock(buffer.data(), numSamples);
    
    // 4. FFT the ACTUAL OUTPUT
    auto spectrum = computeFFT(buffer);
    
    // 5. Check for aliased harmonics
    // H2 at 36.4kHz aliases to 7.7kHz (44.1 - 36.4 = 7.7)
    // H3 at 54.6kHz aliases to 10.5kHz (54.6 - 44.1 = 10.5)
    // etc.
    
    float alias7700 = spectrum[binFor(7700)];
    float alias10500 = spectrum[binFor(10500)];
    
    // 6. Report MEASURED values
    std::cout << "Alias at 7.7kHz: " << alias7700 << " dB" << std::endl;
    std::cout << "Alias at 10.5kHz: " << alias10500 << " dB" << std::endl;
    
    // 7. FAIL if above threshold
    if (alias7700 > -80.0f || alias10500 > -80.0f) {
        std::cout << "TEST FAILED - Aliasing detected!" << std::endl;
    }
}
```

## Test Parameters (From User's Setup)

- **Test frequency:** 18.2 kHz
- **Input gain:** +8.3 dB  
- **Bias:** 40%
- **Wow:** 7%
- **Flutter:** 3%
- **Noise:** 5%
- **Machine:** Swiss 800
- **Speed:** 30 IPS
- **Tape Type:** 456
- **HQ Mode:** 4x

## Expected Alias Locations (at 44.1kHz)

| Harmonic | Frequency | Aliases To |
|----------|-----------|------------|
| H2 | 36.4 kHz | 7.7 kHz |
| H3 | 54.6 kHz | 10.5 kHz |
| H4 | 72.8 kHz | 15.7 kHz |
| H5 | 91.0 kHz | 2.9 kHz |

These are the frequencies where you should see peaks in the spectrum if aliasing is occurring.

## Success Criteria

The fix is successful when:

1. An 18.2kHz sine wave processed through the plugin shows NO peaks above -80dB at the alias frequencies listed above
2. The spectrum analyzer shows a clean rolloff above ~15kHz with no sharp spikes
3. The plugin maintains full saturation capability on all frequencies (no HF reduction tricks)
4. The brightness/air of the input signal is preserved

## Reference Implementation

See `TapeMachineAliasingFix.h` for:
- `AntiAliasingFilter` - Proper 8th-order Chebyshev filter
- `ProperOversampler` - Correct oversampling with AA filtering
- `RealAliasingTest` - Test that measures actual output
- `SignalFlowDebugger` - Tool to find where aliasing is introduced
