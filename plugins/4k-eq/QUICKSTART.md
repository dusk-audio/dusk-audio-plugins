# 4K EQ Quick Start Guide

## 5-Minute Getting Started

### Installation
1. Copy `4K EQ.vst3` to your VST3 folder:
   - **Linux**: `~/.vst3/`
   - **macOS**: `~/Library/Audio/Plug-Ins/VST3/`
   - **Windows**: `C:\Program Files\Common Files\VST3\`

2. Rescan plugins in your DAW

3. Load "4K EQ" from your plugin list (under "Luna Co. Audio")

### Your First Mix

#### Scenario 1: Vocals Need Clarity
1. Load **"Vocal Presence"** preset
2. Switch to **Black mode** for surgical precision
3. Adjust the **HMF FREQ** knob to find the sweet spot (2-4 kHz)
4. Fine-tune **HMF GAIN** to taste (+2 to +5 dB typical)
5. Optional: Add 10-20% **DRIVE** for subtle analog warmth

#### Scenario 2: Muddy Bass
1. Load **"Bass Warmth"** preset
2. **LF GAIN**: Boost 50-80 Hz for body (+3 to +6 dB)
3. **LMF GAIN**: Cut 200-400 Hz to remove mud (-2 to -4 dB)
4. **LMF Q**: Increase for narrower cut (1.0-1.5)
5. Use **HPF** to remove ultra-lows below 30-40 Hz

#### Scenario 3: Dull Mix Bus
1. Load **"Mix Bus Glue"** preset
2. **Brown mode** for warm, musical enhancement
3. **HF GAIN**: +1 to +3 dB for air (10-16 kHz)
4. **DRIVE**: 15-30% for cohesive glue
5. Enable **AUTO GAIN** to prevent level changes

## Brown vs Black: When to Use Each

| Situation | Mode | Why |
|-----------|------|-----|
| Mix bus processing | **Brown** | Warm, musical, forgiving |
| Problem frequencies | **Black** | Surgical, precise, proportional Q |
| Adding warmth | **Brown** | More 2nd harmonic character |
| Mastering | **Black** | Clean, transparent, extended range |
| Creative boosts | **Brown** | Broad, musical curves |
| Surgical cuts | **Black** | Narrow, focused, Q increases with gain |

## Key Features Explained

### Auto Gain (Recommended: ON)
- Automatically adjusts output to maintain consistent loudness
- Prevents clipping when boosting multiple bands
- Turn OFF if you want to hear level changes from EQ moves

### Drive (Saturation)
- **0-30%**: Subtle analog warmth and glue
- **30-60%**: Noticeable harmonic enhancement
- **60-100%**: Aggressive saturation and color
- **Tip**: Use 4x oversampling with heavy drive to prevent aliasing

### Oversampling
- **2x** (default): Good balance of quality and CPU efficiency
- **4x**: Maximum quality, use with heavy saturation (doubles CPU)
- Auto-limits to 2x at sample rates >96 kHz

## Common Workflows

### Typical Vocal Chain
```
HPF: 80-100 Hz
LF: +2 dB @ 100 Hz (warmth)
LMF: -2 to -4 dB @ 300 Hz (reduce mud)
HMF: +3 to +5 dB @ 3-4 kHz (presence)
HF: +2 dB @ 10-12 kHz (air)
Drive: 10-20%
Mode: Black (if removing resonances), Brown (if enhancing)
```

### Typical Drum Bus
```
HPF: 40-60 Hz
LF: +3 to +5 dB @ 60-80 Hz (punch)
LMF: -2 to -4 dB @ 300-400 Hz (boxiness)
HMF: +2 to +4 dB @ 3-5 kHz (attack)
HF: +2 dB @ 10 kHz (cymbals)
Drive: 20-30%
Mode: Black for aggressive, Brown for smooth
```

### Typical Mastering
```
HPF: 20-30 Hz (subsonic rumble)
LF: +0.5 to +1 dB @ 60 Hz (subtle weight)
LMF: -0.5 to -1 dB @ 400-600 Hz (slight de-mud)
HMF: +0.5 to +1 dB @ 3-5 kHz (presence)
HF: +1 to +2 dB @ 12-16 kHz (sheen)
Drive: 10-15%
Mode: Black (surgical precision)
Auto Gain: ON
```

## Pro Tips

### DO:
✓ Start with a preset and tweak
✓ Use gentle moves (±3-6 dB is usually enough)
✓ Cut before you boost
✓ Enable Auto Gain to maintain consistent levels
✓ Use Black mode for problem-solving
✓ Use Brown mode for enhancement
✓ Double-click knobs to reset to default
✓ A/B with bypass to ensure improvement

### DON'T:
✗ Boost every band at once (sounds unnatural)
✗ Use extreme Q values without reason
✗ Add drive "just because" (SSL is clean by default)
✗ Forget to check your output level
✗ Use 4x oversampling everywhere (wastes CPU)
✗ Ignore the HPF (remove ultra-lows first)

## Troubleshooting

### "My mix sounds harsh after EQ"
- Reduce **HMF** and **HF** boost amounts
- Switch to **Brown mode** for gentler curves
- Lower the **Q** for broader, more musical moves
- Reduce **Drive** (saturation adds harmonics)

### "Plugin is using too much CPU"
- Use **2x oversampling** instead of 4x
- At 96kHz+, plugin auto-limits to 2x anyway
- Disable **M/S mode** if not needed
- Consider freezing/bouncing tracks with plugins

### "My output is clipping"
- Enable **AUTO GAIN** (compensates automatically)
- Reduce **OUTPUT** gain manually
- Check if you're boosting many bands simultaneously
- Lower individual band **GAIN** amounts

### "I can't hear the saturation"
- Increase **DRIVE** above 30% for noticeable effect
- Try boosting EQ bands first (drive affects post-EQ signal)
- Use **4x oversampling** for cleaner saturation
- Check if bypass is engaged accidentally

## Keyboard Shortcuts

- **Shift + Drag**: Fine adjustment (slower)
- **Ctrl/Cmd + Click**: Reset parameter to default
- **Double-click**: Quick reset to default
- **Mouse wheel**: Adjust value up/down

## Next Steps

1. **Experiment with presets** - Try all 15 on different sources
2. **Learn Brown vs Black** - Compare on same material
3. **Practice subtle moves** - Less is often more
4. **Compare to bypassed** - Always A/B your changes
5. **Read the full README** - Dive deeper into technical details

---

**Quick Reference Card**: Print this page and keep it near your workstation!
