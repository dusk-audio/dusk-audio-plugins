# Universal Compressor Unit Tests

## Overview

The Universal Compressor includes comprehensive unit tests to validate DSP accuracy, stability, and functionality across all compressor modes.

## Test Coverage

### Production Readiness Tests
- **Plugin Initialization** - Validates proper setup and configuration
- **Parameter Range Validation** - Ensures all parameters exist and are within valid ranges
- **DSP Stability** - Tests for NaN/Inf with edge cases (silence, DC offset, hot signals)
- **Thread Safety** - Validates atomic meter access from multiple threads
- **Latency Reporting** - Ensures accurate oversampling latency reporting
- **Bypass Functionality** - Verifies bypass passes audio unchanged

### Compression Accuracy Tests
- **Opto Compressor** (LA-2A style) - Validates program-dependent compression behavior
- **FET Compressor** (1176 style) - Tests fixed threshold and ratio accuracy
- **VCA Compressor** (DBX 160 style) - Validates precise threshold/ratio calculations
- **Bus Compressor** (SSL style) - Tests bus compression characteristics
- **Compression Ratios** - Validates that 4:1 ratio produces expected gain reduction

Each test validates:
- Gain reduction occurs when expected
- Gain reduction values are within reasonable ranges
- Output is properly attenuated
- No audio artifacts (NaN/Inf/excessive clipping)

## Building the Tests

### Option 1: CMake Command Line

```bash
cd /home/marc/projects/plugins/build

# Enable tests during configuration
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_UNIVERSAL_COMPRESSOR_TESTS=ON

# Build the test executable
cmake --build . --target UniversalCompressorTests
```

### Option 2: Rebuild Script

Add the test flag to your rebuild script:

```bash
./rebuild_all.sh --with-tests
```

Or manually edit CMakeLists to set `BUILD_UNIVERSAL_COMPRESSOR_TESTS` to `ON`.

## Running the Tests

### Run All Tests

```bash
cd /home/marc/projects/plugins/build/universal-compressor
./UniversalCompressorTests --all
```

### Run Specific Test Category

```bash
# Run only compressor tests
./UniversalCompressorTests --category=Compressor

# Run with verbose output
./UniversalCompressorTests --all --verbose
```

### Expected Output

```
Running test: Universal Compressor Tests...
  Test 1/11: Plugin Initialization
    ✓ Plugin name is correct
    ✓ Plugin does not accept MIDI
    ✓ Plugin has editor
    ✓ Tail length is non-negative

  Test 2/11: Parameter Range Validation
    ✓ Mode parameter exists
    ✓ All essential parameters exist

  Test 3/11: Opto Compressor Gain Reduction
    ✓ Opto mode produces gain reduction on hot signal: -8.3 dB
    ✓ Output is compressed (peak < 1.0): 0.72

  [... additional tests ...]

  Test results: 11/11 tests passed
```

## Test Failure Investigation

If tests fail, check:

1. **Parameter Issues** - Ensure all parameters are properly initialized in `createParameterLayout()`
2. **Sample Rate** - Tests use 48kHz; ensure compressor handles this correctly
3. **Gain Reduction Values** - If GR is out of range, check compressor threshold/ratio calculations
4. **NaN/Inf Errors** - Usually indicates denormal issues or division by zero
5. **Bypass Issues** - Verify bypass early-returns in `processBlock()`

## Adding New Tests

To add additional tests:

1. Edit `UniversalCompressorTests.cpp`
2. Add new test method (e.g., `testNewFeature()`)
3. Call it from `runTest()` with `beginTest("New Feature Test")`
4. Rebuild tests and verify

Example:

```cpp
void testNewFeature()
{
    UniversalCompressor compressor;
    compressor.prepareToPlay(48000.0, 512);

    // Your test code here

    expect(condition, "Test description");
}
```

## Continuous Integration

These tests are designed to run in CI environments:

```bash
# CI-friendly test run (exits with code 0 on pass, non-zero on fail)
./UniversalCompressorTests --all --junit=test-results.xml
```

## Performance Testing

For CPU performance testing, use a profiler instead of unit tests:

```bash
# Linux perf
perf record ./UniversalCompressorTests --all
perf report

# Valgrind callgrind
valgrind --tool=callgrind ./UniversalCompressorTests --all
```

## Notes

- Tests use synthetic signals (sine waves) at known levels
- Real-world listening tests are still essential for subjective quality
- Tests validate **correctness**, not **sound quality**
- Run tests after any DSP modifications to catch regressions
