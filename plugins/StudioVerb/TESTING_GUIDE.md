# StudioVerb Testing Guide
Quick reference for validating audio safety improvements

## Quick Stability Test (5 minutes)

### Test 1: Extreme Settings
1. Load StudioVerb in your DAW
2. Set all parameters to maximum:
   - Algorithm: Hall
   - Size: 1.0
   - Damping: 1.0
   - Predelay: 200ms
   - Mix: 100%
3. Play white noise or pink noise for 30 seconds
4. **Expected**: No clipping, no oscillation, clean decay

### Test 2: Rapid Automation
1. Create automation for all parameters
2. Set automation to change rapidly (every 10-50ms)
3. Play sustained tones or drums
4. **Expected**: No clicks, pops, or zipper noise

### Test 3: Hot Input
1. Generate sine wave at +6dB (peak level ~2.0)
2. Feed into StudioVerb
3. Monitor output levels
4. **Expected**: Output clamped to ±1.0, no distortion

### Test 4: Impulse Response
1. Send single click/impulse
2. Record reverb tail
3. Check for:
   - Smooth decay (no ringing)
   - No infinite sustain
   - Clean cutoff
4. **Expected**: Natural exponential decay

## CPU Performance Test

```bash
# Monitor CPU while playing
top -p $(pgrep -f "Studio Verb")
```

**Expected CPU Usage**:
- Idle: <1%
- Active (44.1kHz): 5-10%
- Active (96kHz): 10-20%

## NaN Detection (Linux)

```bash
# In terminal, while playing:
dmesg | grep -E "nan|inf"
# Should show nothing
```

## Sample Rate Testing

Test at multiple rates in your DAW preferences:
- 44.1 kHz ✓
- 48 kHz ✓
- 88.2 kHz ✓
- 96 kHz ✓
- 192 kHz ✓

All should work without artifacts.

## A/B Comparison

Compare StudioVerb Hall mode against:
- Valhalla VintageVerb (Hall)
- Lexicon PCM reverb
- Any other professional reverb

**Check for**:
- Tonal balance (should be neutral)
- Decay smoothness (no metallic ringing)
- Stereo image (should be wide and natural)
- CPU efficiency (should be competitive)

## Pass Criteria

✅ No clipping at any setting
✅ No oscillation or runaway feedback
✅ No clicks or pops on automation
✅ No CPU spikes or denormal slowdown
✅ Smooth, natural decay
✅ Stable at all sample rates
✅ Consistent sound across sessions

## Fail Indicators

❌ Distortion or clipping
❌ Metallic ringing or "boing" sounds
❌ Clicks on parameter changes
❌ CPU usage spikes
❌ Different sound when reopening project
❌ NaN/Inf errors in logs

## Report Issues

If you find any issues, note:
1. DAW and version
2. Sample rate and buffer size
3. Parameter settings when issue occurred
4. Audio input type (drums, synth, vocals, etc.)
5. Any error messages

---

**Quick Command Reference**:
```bash
# Rebuild plugin
cd /home/marc/projects/Luna/plugins/plugins/StudioVerb
./build.sh

# Check installation
ls -lh ~/.vst3/Studio\ Verb.vst3/
ls -lh ~/.lv2/Studio\ Verb.lv2/

# Monitor CPU
htop -p $(pgrep -f Verb)
```
