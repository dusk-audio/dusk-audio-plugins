# Universal Compressor - Analog Authenticity Guide

## Overview
The Universal Compressor features 6 modes: 4 authentic analog emulations + 2 modern digital modes.

---

## ANALOG MODES (Modes 0-3) - DO NOT MODIFY

These modes must remain faithful to the original hardware units they emulate. Any changes would compromise the authentic analog sound.

### Mode 0: OPTO (LA-2A)
**Hardware Reference**: Teletronix LA-2A Leveling Amplifier

**Fixed Characteristics** (matching real hardware):
- **Attack Time**: 10ms (fixed by T4B optical cell)
- **Release Time**: 60ms to 5 seconds (program-dependent, dual-stage)
- **Ratio**: ~3:1 average (varies with program material)
- **Knee**: Very soft (optical characteristic)

**Controls** (as on real LA-2A):
- Peak Reduction (main compression knob)
- Gain (makeup gain)
- Emphasis (HF emphasis for sidechain)
- Compress/Limit switch

**Unique Behavior**:
- Memory effect from optical cell
- Frequency-dependent compression
- Natural pumping on bass-heavy material
- Warm tube harmonics (12AX7 and 12BH7)

---

### Mode 1: FET (1176)
**Hardware Reference**: UREI 1176 Limiting Amplifier

**Fixed Characteristics** (matching real hardware):
- **Attack Time**: 20μs to 800μs (7 positions)
- **Release Time**: 50ms to 1100ms (7 positions)
- **Ratio**: 4:1, 8:1, 12:1, 20:1, All-buttons (∞:1)
- **Distortion**: ~0.02% THD (FET color)

**Controls** (as on real 1176):
- Input (drives into compression)
- Output (makeup gain)
- Attack (7-position)
- Release (7-position)
- Ratio buttons (can press all for "British mode")

**Unique Behavior**:
- Non-linear attack/release curves
- Program-dependent behavior
- Aggressive FET saturation
- "All-buttons" mode for extreme limiting
- Feedback topology compression

---

### Mode 2: VCA (DBX 160)
**Hardware Reference**: DBX 160 Compressor/Limiter

**Fixed Characteristics** (matching real hardware):
- **Detection**: True RMS with 10ms window
- **Knee**: OverEasy™ soft knee (patented)
- **Ratio**: 1:1 to ∞:1 (continuous)
- **Distortion**: <0.01% (very clean VCA)

**Controls** (as on real DBX 160):
- Threshold (-40 to +20dB)
- Compression Ratio
- Output Gain
- OverEasy switch

**Unique Behavior**:
- Transparent compression
- True RMS detection
- Gradual knee transition
- Hard-knee gate mode
- Predictable, musical compression

---

### Mode 3: BUS (SSL Bus)
**Hardware Reference**: SSL G Series Bus Compressor

**Fixed Characteristics** (matching real hardware):
- **Attack Time**: 0.1, 0.3, 1, 3, 10, 30ms (switched)
- **Release Time**: 0.1, 0.3, 0.6, 1.2 seconds, Auto
- **Ratio**: 2:1, 4:1, 10:1 (switched)
- **Detection**: Quad VCA with feedback/feedforward mix

**Controls** (as on real SSL Bus):
- Threshold (+15 to -15dB)
- Ratio (3 positions)
- Attack (6 positions)
- Release (5 positions including Auto)
- Makeup Gain
- Sidechain HPF (up to 200Hz)

**Unique Behavior**:
- "Glue" compression characteristic
- Auto-release adapts to program
- Subtle harmonic enhancement
- Mix bus cohesion
- Punchy transient response

---

## DIGITAL MODES (Modes 4-5) - Modern Features

### Mode 4: DIGITAL
**Modern transparent compression with advanced features**

**Unique Features**:
- **Lookahead**: 0-10ms for brick-wall limiting
- **Precise Control**: 0.01ms attack, continuous parameters
- **Sidechain EQ**: 4-band parametric
- **Adaptive Release**: Program-dependent timing
- **Transient Shaping**: ±100% emphasis
- **Built-in Parallel**: 0-100% wet/dry mix
- **Advanced Knee**: Multiple curve types

**Use Cases**:
- Mastering-grade compression
- Transparent level control
- Surgical dynamics processing
- Modern production techniques

---

### Mode 5: MULTIBAND
**Frequency-selective compression**

**Unique Features**:
- **2-5 Bands**: Adjustable band count
- **Per-Band Control**: Independent compression settings
- **Linear Phase Option**: Zero phase distortion
- **Linkwitz-Riley Crossovers**: 12/24/48 dB/oct
- **Band Solo/Bypass**: For precise setup
- **Per-Band Mix**: Individual wet/dry

**Use Cases**:
- Mastering
- De-essing (high-frequency compression)
- Bass control
- Mix bus processing
- Broadcast/streaming optimization

---

## Important Implementation Notes

### For Analog Modes (0-3):
✅ **DO**: Preserve all nonlinear behavior
✅ **DO**: Keep program-dependent characteristics
✅ **DO**: Maintain harmonic distortion profiles
✅ **DO**: Respect hardware control ranges

❌ **DON'T**: Add modern features (lookahead, etc.)
❌ **DON'T**: "Improve" the response curves
❌ **DON'T**: Remove limitations of hardware
❌ **DON'T**: Change attack/release ballistics

### For Digital Modes (4-5):
✅ **DO**: Use modern DSP techniques
✅ **DO**: Provide precise control
✅ **DO**: Include convenience features
✅ **DO**: Optimize for transparency

---

## Testing Analog Authenticity

To verify analog modes match hardware:

1. **Null Test**: Process same material through plugin and hardware
2. **Harmonic Analysis**: Compare THD and harmonic profiles
3. **Transient Response**: Match attack/release curves
4. **Program Dependency**: Verify dynamic behavior
5. **Frequency Response**: Check for characteristic colorations

---

## User Guide Summary

**When to use each mode:**

- **OPTO**: Vocals, bass, smooth leveling
- **FET**: Drums, aggressive compression, parallel
- **VCA**: Clean compression, mastering, broadcast
- **BUS**: Mix bus, drum bus, "glue"
- **DIGITAL**: Mastering, transparent control, limiting
- **MULTIBAND**: Mastering, problem-solving, streaming

---

*Note: The analog modes (0-3) are meticulously modeled to match the original hardware units. Any modifications to these modes would compromise the authentic analog sound that users expect.*