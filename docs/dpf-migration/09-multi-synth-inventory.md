# Multi-Synth JUCE → DPF Port: Read-Only Inventory

> Product name: **Sunset Circuits** (renamed from Multi-Synth pre-release; slug `sunset-circuits`).
> Filename and internal class/namespace names kept for history; shipping product is Sunset Circuits.

Plugin: `Multi-Synth` (slug `multi-synth`), 4-mode virtual-analog polysynth. All DSP lives in header-only classes under namespace `MultiSynthDSP`. The two `.cpp` DSP files (`MultiSynthVoice.cpp`, `Arpeggiator.cpp`) are **empty stubs** ("header-only for now; exists for CMake"). This makes the DSP core easy to lift framework-free — the only real coupling is to JUCE utility symbols and `juce::Reverb`.

DPF target pattern (from tape-echo template): a framework-free `TapeEchoDSP` class with `prepare(sr, maxBlock)`, `reset()`, `processBlock(in, out, nCh, nSamples)`, atomic `setX()` setters, `getOutputLevel()`. Params live in a shared `TapeEchoParams.hpp` enum + factory-preset table. Shared DSP primitives in `plugins/shared-dpf/dsp/` (`DuskSmoothed.hpp`, `DuskFilters.hpp`, `DuskOversampler.hpp` with `HalfbandFIR`). The port should mirror this: a `MultiSynthDSP` engine class + `MultiSynthParams.hpp`.

---

## 1. PARAMETER TABLE

APVTS uses `JUCE_FORCE_USE_LEGACY_PARAM_IDS=1`; every param has version hint `1`. ~134 parameters total. Skew below is the `NormalisableRange` skew factor (blank = linear).

### Global / Mode
| ID | Name | Type | Range | Default | Skew | Modes |
|---|---|---|---|---|---|---|
| `mode` | Synth Mode | Choice | Cosmos/Oracle/Mono/Modular | 0 (Cosmos) | — | all |
| `masterTune` | Master Tune | Float | -100..100 cents | 0 | — | all (**inert, see §9**) |
| `masterVol` | Master Volume | Float | -60..6 dB | 0 | — | all |
| `masterPan` | Master Pan | Float | -1..1 | 0 | — | all |
| `stereoWidth` | Stereo Width | Float | 0..1 | 0.5 | — | all |
| `oversampling` | Oversampling | Choice | 1x/2x/4x | 1 (2x) | — | all (**see §2 & §9**) |
| `analogAmt` | Analog | Float | 0..1 | 0.2 | — | all |
| `vintage` | Vintage | Float | 0..1 | 0 | — | all |

### Oscillator 1 / 2 / 3 / Sub / Noise
| ID | Name | Type | Range | Default | Skew | Modes |
|---|---|---|---|---|---|---|
| `osc1Wave` | Osc 1 Wave | Choice | Saw/Square/Triangle/Sine/Pulse | 0 | — | all |
| `osc1Detune` | Osc 1 Detune | Float | -50..50 cents | 0 | — | all |
| `osc1PW` | Osc 1 PW | Float | 0.05..0.95 | 0.5 | — | all |
| `osc1Level` | Osc 1 Level | Float | 0..1 | 1.0 | — | all |
| `osc2Wave` | Osc 2 Wave | Choice | Saw/Square/Triangle/Sine/Pulse | 0 | — | all (Cosmos overrides to Pulse) |
| `osc2Detune` | Osc 2 Detune | Float | -50..50 | 7.0 | — | all |
| `osc2PW` | Osc 2 PW | Float | 0.05..0.95 | 0.5 | — | all |
| `osc2Level` | Osc 2 Level | Float | 0..1 | 0.8 | — | all |
| `osc2Semi` | Osc 2 Semi | Int | -24..24 | 0 | — | all |
| `osc3Wave` | Osc 3 Wave | Choice | Saw/Square/Triangle/Sine | 0 | — | Modular only (UI) |
| `osc3Level` | Osc 3 Level | Float | 0..1 | 0.5 | — | Modular only |
| `subLevel` | Sub Level | Float | 0..1 | 0.5 | — | Cosmos + Mono (UI) |
| `subWave` | Sub Wave | Choice | Square/Sine | 0 | — | Cosmos + Mono |
| `noiseLevel` | Noise Level | Float | 0..1 | 0.0 | — | all |

### Filter + Envelopes
| ID | Name | Type | Range | Default | Skew | Modes |
|---|---|---|---|---|---|---|
| `filterCutoff` | Filter Cutoff | Float | 20..20000 Hz | 8000 | 0.3 | all |
| `filterRes` | Filter Resonance | Float | 0..1 | 0.3 | — | all |
| `filterHP` | Filter HP | Float | 20..2000 Hz | 20 | 0.3 | Cosmos only (UI) |
| `filterEnvAmt` | Filter Env Amt | Float | -1..1 | 0.5 | — | all |
| `ampA` `ampD` `ampR` | Amp A/D/R | Float | 0.001..10 s | 0.01/0.2/0.3 | 0.3 | all |
| `ampS` | Amp Sustain | Float | 0..1 | 0.8 | — | all |
| `ampCurve` | Amp Curve | Choice | Linear/Exponential/Logarithmic/Analog RC | 3 (Analog RC) | — | all |
| `filtA` `filtD` `filtR` | Filter A/D/R | Float | 0.001..10 s | 0.01/0.3/0.5 | 0.3 | all |
| `filtS` | Filter Sustain | Float | 0..1 | 0.4 | — | all |
| `filtCurve` | Filter Curve | Choice | (same 4) | 3 | — | all |

### Mode-specific voice params
| ID | Name | Type | Range | Default | Modes |
|---|---|---|---|---|---|
| `crossMod` | Cross Mod | Float | 0..1 | 0 | Cosmos+Oracle (UI) — **DEAD, never read in DSP (§9)** |
| `ringMod` | Ring Mod | Float | 0..1 | 0 | Mono+Modular |
| `hardSync` | Hard Sync | Bool | — | false | Mono+Modular |
| `fmAmount` | FM Amount | Float | 0..1 | 0 | Modular |
| `pmFenvOscA` | PM FEnv→OscA | Float | 0..1 | 0 | Oracle |
| `pmFenvFilt` | PM FEnv→Filt | Float | 0..1 | 0 | Oracle |
| `pmOscBOscA` | PM OscB→OscA | Float | 0..1 | 0 | Oracle |
| `pmOscBPWM` | PM OscB→PW | Float | 0..1 | 0 | Oracle |
| `shRate` | S&H Rate | Float | 0.1..50 Hz | 5.0 (skew 0.3) | Modular (S&H mod source) |
| `cosmosChorus` | Cosmos Chorus | Choice | Off/I/II/I+II | 3 (I+II) | Cosmos only |

### LFO 1 / 2
| ID | Name | Type | Range | Default | Skew |
|---|---|---|---|---|---|
| `lfo1Rate` `lfo2Rate` | LFO Rate | Float | 0.01..50 Hz | 1.0 / 0.5 | 0.3 |
| `lfo1Shape` `lfo2Shape` | LFO Shape | Choice | Sine/Triangle/Square/S&H/Random | 0 | — |
| `lfo1Fade` `lfo2Fade` | LFO Fade In | Float | 0..5 s | 0 | — |
| `lfo1Sync` `lfo2Sync` | LFO Sync | Bool | — | false | — |

### Unison / Portamento / Velocity
| ID | Name | Type | Range | Default | Skew |
|---|---|---|---|---|---|
| `unisonVoices` | Unison Voices | Int | 1..8 | 1 | — (**fake unison, §9**) |
| `unisonDetune` | Unison Detune | Float | 0..50 cents | 10 | — |
| `unisonSpread` | Unison Spread | Float | 0..1 | 1.0 | — |
| `portaTime` | Portamento | Float | 0..2 s | 0 | 0.3 |
| `legato` | Legato | Bool | — | false | — |
| `glideMode` | Glide Mode | Choice | Time/Rate | 0 | — |
| `velSens` | Velocity Sens | Float | 0..1 | 0.7 | — |
| `velCurve` | Vel Curve | Choice | Linear/Soft/Hard/Fixed | 0 | — |
| `pbRange` | PB Range | Int | 1..24 semis | 2 | — |

### Arpeggiator (+ 16 step mutes)
| ID | Name | Type | Range | Default |
|---|---|---|---|---|
| `arpOn` | Arp On | Bool | — | false |
| `arpMode` | Arp Mode | Choice | Up/Down/Up-Down/Down-Up/Random/Order/Chord | 0 |
| `arpOctave` | Arp Octave | Int | 1..4 | 1 |
| `arpRate` | Arp Rate | Choice | 14 divisions: 1/1,1/2,1/4,1/8,1/16,1/32,1/2D,1/4D,1/8D,1/16D,1/2T,1/4T,1/8T,1/16T | 3 (1/8) |
| `arpGate` | Arp Gate | Float | 0.01..1 | 0.5 |
| `arpSwing` | Arp Swing | Float | 0..1 | 0 |
| `arpLatch` | Arp Latch | Bool | — | false |
| `arpVelMode` | Arp Vel Mode | Choice | As Played/Fixed/Accent | 0 |
| `arpFixedVel` | Arp Fixed Vel | Int | 1..127 | 100 |
| `arpStep0`..`arpStep15` | Arp Step N | Bool | — | true (**16 params**) |

Note: `arpMode` StringArray has 7 entries; DSP `ArpMode` enum has 7 (Up/Down/UpDown/DownUp/Random/Order/Chord) — order matches, cast valid. AccentPattern (Downbeat/EveryOther/RampUp/RampDown) is **not** exposed as a param (hardcoded to Downbeat).

### Effects — Drive / Chorus / Delay / Reverb
| ID | Name | Type | Range | Default | Skew |
|---|---|---|---|---|---|
| `driveOn` | Drive On | Bool | — | false | — |
| `driveType` | Drive Type | Choice | Soft/Hard/Tube | 0 | — |
| `driveAmt` | Drive Amount | Float | 0..1 | 0.3 | — |
| `driveMix` | Drive Mix | Float | 0..1 | 1.0 | — |
| `chorusOn` | Chorus On | Bool | — | false | — |
| `chorusRate` | Chorus Rate | Float | 0.1..10 Hz | 0.8 | — |
| `chorusDepth` | Chorus Depth | Float | 0..1 | 0.5 | — |
| `chorusMix` | Chorus Mix | Float | 0..1 | 0.5 | — |
| `delayOn` | Delay On | Bool | — | false | — |
| `delaySync` | Delay Sync | Bool | — | true | — |
| `delayTime` | Delay Time | Float | 1..2000 ms | 500 | 0.3 |
| `delayDiv` | Delay Division | Choice | (same 14 as arpRate) | 3 | — |
| `delayFB` | Delay Feedback | Float | 0..0.95 | 0.3 | — |
| `delayMix` | Delay Mix | Float | 0..1 | 0.3 | — |
| `delayPP` | Delay Ping-Pong | Bool | — | false | — |
| `delayTape` | Delay Tape | Bool | — | false | — |
| `reverbOn` | Reverb On | Bool | — | false | — |
| `reverbSize` | Reverb Size | Float | 0..1 | 0.5 | — |
| `reverbDecay` | Reverb Decay | Float | 0.1..20 s | 2.0 | 0.3 |
| `reverbDamp` | Reverb Damping | Float | 0..1 | 0.3 | — |
| `reverbMix` | Reverb Mix | Float | 0..1 | 0.2 | — |
| `reverbPD` | Reverb Pre-Delay | Float | 0..200 ms | 20 | — |

### Mod Matrix — 8 slots × 3 params (24 total)
| ID pattern | Name | Type | Range | Default |
|---|---|---|---|---|
| `modSrc0`..`modSrc7` | Mod N Source | Choice | None/LFO 1/LFO 2/Env 2/Mod Wheel/Aftertouch/Velocity/Key Track/Random/Pitch Bend/S&H (11) | 0 |
| `modDst0`..`modDst7` | Mod N Dest | Choice | None/Osc 1 Pitch/Osc 2 Pitch/Osc 1 PWM/Osc 2 PWM/Filter Cutoff/Filter Res/Amplitude/Pan/LFO 1 Rate/LFO 2 Rate/FX Mix/Unison Det (13) | 0 |
| `modAmt0`..`modAmt7` | Mod N Amount | Float | -1..1 | 0 |

Combo orderings match `ModSource`/`ModDest` enums exactly. **Destinations LFO1Rate, LFO2Rate, FX Mix (EffectsMix), Unison Det are DEAD** — accumulated but never read by any consumer (§9).

---

## 2. DSP INVENTORY (per file)

### OscillatorEngine.h
- **`Oscillator`** — polyBLEP band-limited. Waveforms: Saw (`2·phase−1` − blep), Square (±blep at both edges), Pulse (variable width, blep both edges), Triangle (leaky-integrated square, `triState = 0.999·triState + dt·sq·4`), Sine (`std::sin`), Noise (white via `std::mt19937`/`uniform_real_distribution`). `applyFM()` adds to phase (through-zero-ish, wraps). `hardSync()` zeroes phase. `didCross()` = zero-crossing flag for sync. `setDetune(cents)` → `detuneRatio = 2^(cents/1200)`, multiplies `dt`.
- **`SubOscillator`** — wraps `Oscillator` one octave down (`freq·0.5`).
- **`ringModulate(a,b) = a*b`**; `crossModulate(mod,amt)` (defined, unused).
- **`SampleAndHold`** — phase accumulator at `rateHz/sr`; latches input on wrap.
- **`PinkNoiseGenerator`** — Paul Kellet 7-coefficient filter, own `std::mt19937`.

### FilterEngine.h
Four 4-pole (24 dB/oct) models, all built on **`FourPoleOTA`** (4 cascaded one-pole LP `y = s + g·(in−s)` with per-stage `tanh` nonlinearity, `g = tan(π·fc/sr)`, resonance feedback from 4th pole `tanh(s[3]·feedback)`, input saturation, `bassComp` mix-back, first-order ~5 Hz DC blocker, NaN guards). Tuning knobs `maxFeedback / saturationStrength / stageNonlinearity / bassComp`:
- **Cosmos** (`CosmosFilter`, classic Japanese-poly filter chip style): `FourPoleOTA` LPF (maxFb 3.0 = no self-osc, sat 0.8, stage 0.9, bassComp 0) + **`SimpleHPF`** (first-order, non-resonant). Resonance clamped to 0.75.
- **Oracle** (`OracleFilter`, classic American-poly filter chip style): maxFb 4.2 (self-osc >~0.95), sat 1.2, stage 1.1, bassComp 0.15.
- **Mono** (`MonoFilter`, classic Japanese-mono OTA chip style): maxFb 4.0, sat 1.8, stage 1.5, bassComp 0.2; extra drive `1 + res·1.5`.
- **Modular** (`LadderFilter`, transistor ladder): **separate** implementation — polynomial `g` fit, `tanh` per stage, feedback `res·3.8`, returns `s[3]`; no DC blocker, no bassComp.
- **`SynthFilter`** dispatches by `FilterMode` (== `SynthMode` cast in voice via `setMode(static_cast<FilterMode>(mode))`).

### ModMatrix.h
- **`ModMatrix`** — 8 `ModSlot{source,dest,amount}`. `process(ModulationState&)`: clear dest accumulators, for each active slot `dest[d] += sourceVal · amount`. O(8) per call. `const`, stateless besides slots.
- **`ADSREnvelope`** — phase-based (not sample-accurate exp). Stages Idle/Attack/Decay/Sustain/Release. Attack phase `+= 1/(attackTime·sr)`; curve applied via `applyCurve`. Curves: Linear, Exponential (`x²`), Logarithmic (`√x`), **AnalogRC** (`1−exp(−t/τ)`, τ=1/3 → 95% at phase 1). Decay/Release use `1 − applyCurve(phase)`. Release captures start level.
- **`LFO`** — Sine/Triangle/Square/SampleAndHold/RandomSmooth. Fade-in, one-shot, retrigger. Own `juce::Random`. Note the S&H/RandomSmooth wrap detection is heuristic (`prevPhase > phase + dt`).

### UnisonEngine.h
- **`UnisonEngine`** — up to 8 voices. `recalculate()` spreads detune (`-max..+max` cents) and pan (`-spread..+spread`) linearly across voices. `mixToStereo()` constant-power pan, gain `1/√numVoices`. **Detune is computed but never applied to real oscillators** — see §9.

### EffectsEngine.h
- **`DriveEffect`** — gain `1+drive·10`, saturate (SoftClip `tanh` / HardClip `jlimit` / Tube asymmetric `1−exp(−x)` pos, `−0.8·(1−exp(x))` neg), output comp `1/(1+drive·2)`, parallel mix.
- **`ChorusEffect`** — generic BBD, single sine LFO, base 7 ms ± 3 ms·depth, 30 ms buffer, linear interp, optional stereo phase offset. (Post-FX chorus, distinct from the vintage dual-BBD chorus.)
- **`JunoChorusEffect`** — **two independent BBD lines**, triangle LFOs at fixed 0.513 Hz (I) / 0.863 Hz (II), ~3 ms ± 2 ms, one-pole 10 kHz BBD lowpass, inverted-phase stereo (L=+wet, R=−wet). Modes Off/I/II/Both. Blend: dry·0.7 + wet·(0.5 or 0.35 for Both).

> **Note on code identifiers:** literal symbols such as `JunoChorusEffect` and `junoChorus` are quoted verbatim from the original JUCE sources this inventory documents; the DPF port renamed them. They are kept as-is here so the inventory stays factually accurate about the JUCE code.
- **`DelayEffect`** — 2 s stereo buffer, tempo-sync via `getBeatsPerStep(ArpRateDivision)`, feedback LPF+HPF one-poles in loop, optional ping-pong (cross-feed), optional tape character (`tanh(x·1.1)`), soft-clamp feedback.
- **`ReverbEffect`** — wraps **`juce::Reverb`** (Freeverb). Pre-delay circular buffer (≤200 ms). Maps decay→roomSize. **This is the one nontrivial external DSP dependency to replace for DPF.**
- **`SpringReverb`** — thin wrapper reusing `ReverbEffect` with fixed small/dark settings (0.3 size, 1.5 decay, 0.6 damp). Auto-enabled in Modular at mix 0.15. **Not a real dispersive spring** (tape-echo has a proper `SpringReverb` in `TapeEchoDSP.hpp` that could be reused/ported instead).
- **`EffectsChain`** — Drive → Chorus → Delay → Reverb → SpringReverb, per-sample.

### Voice / allocation (MultiSynthVoice.h)
- **`SynthVoice::renderSample`** — per-sample: update env params, process amp+filt env, portamento (`SmoothedValue`), analog drift (random walk every 200–500 samples, ±~3.5 cents at max analog), mod-matrix pitch/PWM, per-mode osc processing, mode-normalized mix (÷activeGain if >1), filter (env-modulated cutoff `cutoff·2^envMod`, clamped 20..0.45·sr), amp × velocity × ampMod, NaN guard + clamp ±4.
- **`VoiceAllocator`** — fixed `std::array<SynthVoice, 8>`. `noteOn`: free-voice-first, else **steal-quietest** (releasing voices weighted ×0.1). `renderSample`: sums active voices, gain-comp `1/(1+log2(maxVoices))`, per-voice pan (mod + random offset·analog), unison mix path.
- Per-sample per-voice a fresh `ModulationState` (stack `std::array`, no heap) is built and `matrix.process`'d.

### Oversampling strategy (**critical**)
`prepareToPlay`: `internalSampleRate = sampleRate * 4.0`; **all voices/oscillators/envelopes/LFOs/filters are prepared at this fixed 4× rate**, regardless of the `oversampling` param. Effects + dual-BBD chorus + arp run at native rate.

`processBlock`: `osFactor` = 1/2/4 from the param (default **2**). For each output sample it renders `osFactor` internal voice samples, sums them, and multiplies by `1/osFactor` (a plain box-average — **no half-band/FIR decimation filter**, unlike tape-echo's `HalfbandFIR`).

**The bug:** oscillator phase advance per output sample = `osFactor · freq/(4·hostRate)`. Correct pitch requires this to equal `freq/hostRate`, i.e. `osFactor == 4`. At the default 2×, every voice plays **exactly one octave low** and all envelopes/LFOs run at half speed; at 1× two octaves low / quarter speed. Only at 4× is pitch/timing correct. See §9 — top porting concern.

### Latency
No `setLatencySamples()` anywhere → reported latency 0. `getTailLengthSeconds()` = 2.0. Box-average decimation adds no algorithmic latency.

---

## 3. JUCE DEPENDENCY MAP (for replacement planning)

Utility symbols recur everywhere and map to trivial free functions: `juce::jlimit`, `juce::jmax`, `juce::MathConstants<float>::{pi,twoPi,halfPi}`.

- **OscillatorEngine.h**: `jlimit`, `MathConstants` (twoPi, pi). STL: `mt19937`, `uniform_real_distribution`, `random_device`, `pow/fmod/sin/floor/abs`.
- **FilterEngine.h**: `jlimit`, `MathConstants` (twoPi, pi). STL: `tan/tanh/isnan/isinf`.
- **ModMatrix.h**: `jlimit`, `jmax`, `jassert`, `juce::Random` (in LFO). STL: `array`, `sqrt/exp/sin/sqrt/floor/abs`.
- **UnisonEngine.h**: `jlimit`, `MathConstants::pi`. STL: `array`, `sqrt/cos/sin`.
- **MultiSynthVoice.h**: `juce::SmoothedValue<float>`, `juce::Random`, `juce::MidiMessage::getMidiNoteInHertz`, `jlimit`, `jmax`, `MathConstants::halfPi`. STL: `array`, `pow/log2/abs/sqrt/cos/sin/isnan/isinf`. → `SmoothedValue` maps to shared-dpf `DuskSmoothed.hpp`; `getMidiNoteInHertz` = `440·2^((n−69)/12)`; `Random` → any PRNG.
- **EffectsEngine.h**: `jlimit`, `MathConstants::twoPi`, `juce::ignoreUnused`, and **`juce::Reverb` + `juce::Reverb::Parameters`** (the only heavyweight dep — Freeverb). STL: `vector`, `fill`, `tanh/exp/sin/abs/floor/fmod`.
- **Arpeggiator.h**: `jlimit`, `juce::String` (only in `ParamIDs` helpers, not core), includes `juce_audio_processors` but core logic is pure STL: `vector/array/sort/reverse/shuffle/default_random_engine/chrono/find_if/remove_if`.
- **MultiSynth.h/.cpp**: full `juce::AudioProcessor`, `AudioProcessorValueTreeState`, `AudioParameterChoice/Float/Int/Bool`, `ParameterID`, `NormalisableRange`, `StringArray`, `AudioBuffer<float>`, `MidiBuffer`/`MidiMessage`, `ScopedNoDenormals`, `Decibels`, `PlayHead`/`getPosition`/`getBpm`/`getIsPlaying`, `XmlElement`/`MemoryBlock`/`ValueTree`, `std::atomic`. → all replaced by the DPF `Plugin` shell + `MultiSynthParams.hpp`.
- **Editor**: `juce_gui_basics` entirely (see §8) — replaced by a DPF/DGL UI.

Replacement priority: (1) `juce::Reverb` (write/port a Freeverb, or reuse the tape-echo spring for Modular); (2) `juce::SmoothedValue` → `DuskSmoothed`; (3) proper decimation via `DuskOversampler::HalfbandFIR`; everything else is trivial inlines.

---

## 4. MODE BEHAVIOR MATRIX

| Aspect | Cosmos | Oracle | Mono | Modular |
|---|---|---|---|---|
| Poly (`setMaxVoices`) | 6 | 5 | 1 | 2 (duophonic) |
| Osc config | 1 DCO: osc1=Saw + osc2 forced Pulse @ same freq + sub | osc1+osc2 (+ poly-mod) | osc1+osc2 + sub | osc1+osc2+osc3 |
| Filter | Japanese-poly LPF+HPF, res≤0.75, no self-osc | American-poly LPF, self-osc | Japanese-mono OTA LPF, fat/driven | transistor ladder |
| Sub-osc | yes (`subLevel`/`subWave`) | no | yes | no (osc3 instead) |
| Special DSP | chorus I/II/Both (`junoChorus`) | poly-mod: FEnv→OscA freq, OscB→OscA freq, OscB→PW, FEnv→Filter | hard sync, ring mod | osc3, hard sync, FM (osc1→osc2), ring mod, S&H mod source, auto spring reverb (mix 0.15) |
| Cross-mod | forced off (comment) | not used | n/a | n/a |
| Mix normalization | osc1+osc2+sub+noise | osc1+osc2+sub+noise | osc1+osc2+sub+noise | osc1+osc2+osc3+noise |

**Editor visibility (`updateModeVisibility`, MultiSynthEditor.cpp:459)** — shown only when:
- osc3 wave/level → Modular
- sub wave/level → Cosmos ∨ Mono
- `cosmosChorus` → Cosmos
- filterHP → Cosmos
- 4 poly-mod sliders → Oracle
- crossMod → Cosmos ∨ Oracle (but DSP-dead)
- ringMod → Mono ∨ Modular
- fmAmount → Modular
- hardSync → Mono ∨ Modular

Mode also swaps the active `LookAndFeel` (Cosmos/Oracle/Mono/Modular) with distinct accent colors and per-mode `paint*` routines.

---

## 5. FACTORY PRESETS (40, name → mode)

Applied procedurally in `applyFactoryPreset` (sets host params, not a data table). Order = `kFactoryPresetNames`.

| # | Name | Mode | | # | Name | Mode |
|---|---|---|---|---|---|---|
| 0 | Neon Nights | Cosmos | | 20 | Midnight Drive | Cosmos |
| 1 | Glass Highway | Cosmos | | 21 | Starfield | Cosmos |
| 2 | Velvet Fog | Cosmos | | 22 | Prophet Brass | Oracle |
| 3 | Sunset Strip | Cosmos | | 23 | Glass Bells | Oracle |
| 4 | Crystal Rain | Cosmos | | 24 | Acid Machine | Mono |
| 5 | Brass Section | Oracle | | 25 | Thunder Sub | Mono |
| 6 | Wooden Keys | Oracle | | 26 | Voltage Seq | Modular |
| 7 | Poly Mod Bells | Oracle | | 27 | Alien Transmission | Modular |
| 8 | Dark Prophet | Oracle | | 28 | Warm Keys | Cosmos |
| 9 | Stab Machine | Oracle | | 29 | Analog Strings | Oracle |
| 10 | Pulsing Darkness | Mono | | 30 | Wobble Bass | Mono |
| 11 | Acid Squelch | Mono | | 31 | Tape Lead | Mono |
| 12 | Screaming Lead | Mono | | 32 | Drone Machine | Modular |
| 13 | Sub Thunder | Mono | | 33 | Arp Factory | Cosmos |
| 14 | Sync Sweep | Mono | | 34 | Fat Fifth | Oracle |
| 15 | Upside Down | Modular | | 35 | Noise Sweep | Modular |
| 16 | Sci-Fi Computer | Modular | | 36 | Init Cosmos | Cosmos |
| 17 | Horror Drone | Modular | | 37 | Init Oracle | Oracle |
| 18 | Voltage Ghost | Modular | | 38 | Init Mono | Mono |
| 19 | Retro Sequence | Modular | | 39 | Init Modular | Modular |

Every preset first resets a common baseline (masterVol −6 dB, oversampling→2×, unison 1, FX off, etc.) then overrides. Presets set `oversampling`→1 (2×) — meaning **all factory presets ship at the buggy default OS rate** (§9). For DPF, convert to a static preset table (mirror tape-echo's `kFactoryPresets` value-array form).

---

## 6. MIDI / PERFORMANCE

- **Note on/off**: routed to arp if `arpOn`, else directly to `VoiceAllocator`. Velocity `getFloatVelocity()`.
- **Mod wheel** (CC1): stored `modWheelValue` [0..1], feeds mod-matrix `ModWheel` source + `displayModWheel`.
- **Aftertouch**: channel pressure only (no poly AT) → `Aftertouch` mod source.
- **Pitch bend**: `(value−8192)/8192 · pbRange` → stored, feeds **only** mod-matrix `PitchBend` source + display. **Does not bend oscillator pitch directly** (§9).
- **All-notes-off / all-sound-off**: `voiceAllocator.allNotesOff()` + `arpeggiator.reset()`.
- **Transport**: `getPlayHead()->getPosition()` → `currentBPM`, `transportPlaying`. Used by arp and tempo-sync delay/LFO.
- **Arp transport sync**: `Arpeggiator::processBlock(numSamples, bpm, transportPlaying)` steps an internal `sampleCounter` (samplesPerStep from BPM × division), swing on odd steps, gate length, 16-step mute pattern, per-step velocity/accent. **Not** locked to host PPQ — free-runs from its own counter; 120 BPM fallback when stopped. Emits `ArpEvent`s consumed same block. LFO "sync" is a crude `rate ·= bpm/120` scale, not true beat-locking.

---

## 7. STATE / PROGRAMS

- `getStateInformation`: `apvts.copyState()` → XML → binary. No custom properties.
- `setStateInformation`: parse XML, tag-check, `replaceState`. No validation/migration.
- **Programs**: 40, `setCurrentProgram` drives `applyFactoryPreset` via `setValueNotifyingHost`. `currentProgram` not persisted (only resulting param values). `changeProgramName` no-op.
- DPF note: factory presets should become DPF `Program`s or an internal table.

---

## 8. EDITOR

- **Window**: default 1000×800, resizable+persistent via `ScalableEditorHelper`. Layout constants: margin 8, knob 70, small knob 55, top bar 38, meter width 24.
- **Structure**: top bar (mode selector, preset combo, Save/Del, oversampling combo, MOD button) + sections {oscillators, filter, envelopes, scopeArea, metersArea, lfo, character, arp, drive, chorus, delay, reverb}. `juce::Timer` drives meters/scope/MIDI-activity/mode refresh.
- **Per-mode LookAndFeel**: base + Cosmos/Oracle/Mono/Modular subclasses, each with `colors.accent`; swapped on mode change; separate `paintCosmos/Oracle/Mono/Modular`.
- **Custom displays**: `WaveformDisplay` (scope from `processor.scopeBuffer`, 512 ring), `FilterResponseDisplay` (magnitude curve), `ModMatrixOverlay` (popup, 8 rows), `SupportersOverlay`, `LEDMeter` ×2.
- **Widgets**: shared `DuskSlider`; `arpStepButtons[16]`; label per knob; attachment vectors.
- **Preset mgmt**: shared `UserPresetManager`.
- Editor is cleanly separated from DSP.

---

## 9. RED FLAGS

**Correctness / probable bugs:**

1. **Oversampling scheme detunes the synth at any setting except 4×.** Voices prepared at fixed `sampleRate·4`, but processBlock renders only `osFactor` (1/2/4) internal samples per output sample. Default 2× → everything an octave low and half-speed; 1× → two octaves low. Only 4× correct. All 40 factory presets ship at 2×. **#1 item to resolve in the port.** Decimation is a box-average (no anti-alias FIR); use `DuskOversampler::HalfbandFIR`.

2. **Master Tune and MIDI pitch bend do nothing by default.** `effectivePitchBend = pitchBend + masterTune/100` feeds only the mod-matrix `PitchBend` source; oscillator base frequency is `portaFreq` only. Standard behavior expects both to always affect pitch.

3. **Unison detune is fake.** Applied as `sample·(1 + detune·0.0001)` — amplitude scale of the same rendered sample, not separate detuned oscillators. Real unison needs per-unison-voice oscillators.

4. **Cross Mod is dead.** Param read into `voiceParams`, shown in UI (Cosmos/Oracle), never used in `renderSample`.

5. **Four mod-matrix destinations dead**: `LFO1Rate`, `LFO2Rate`, `EffectsMix`, `UnisonDetune` accumulated but never read.

**Audio-thread rule violations:**

6. **Arpeggiator allocates on the audio thread.** `processBlock` returns `std::vector<ArpEvent>` and calls `buildPattern()` (allocates, `std::sort`, `std::reverse`) every block while enabled. `ArpMode::Random` constructs `std::default_random_engine` seeded from `std::chrono::system_clock::now()` (clock syscall) + `std::shuffle` every block. Rework: preallocated buffers, no per-block heap.

7. **Per-block string-keyed param lookups**: `updateVoiceParameters()` calls `apvts.getRawParameterValue(id)` for ~130 params every block.

8. **Per-sample param lookups**: `VINTAGE` and `STEREO_WIDTH` read via `getRawParameterValue()` inside the per-sample loop (MultiSynth.cpp:726, 749).

9. **`rand()` in render loop** for vintage noise floor (MultiSynth.cpp:737) — replace with member PRNG.

**Dead code / cruft:**

10. **`AnalogEmulation` is a dead dependency** — included + `initializeLibrary()` called, no class used. Drop.
11. **`MultiSynthVoice.cpp` / `Arpeggiator.cpp` empty stubs.**
12. **Two chorus implementations** (generic `ChorusEffect` + `JunoChorusEffect`) — intentional, carry both.
13. **`SpringReverb` isn't a spring** — Freeverb with dark settings; tape-echo core has a real dispersive spring to reuse.
14. Minor: `Oscillator::syncPhase`, `crossModulate()`, `Arpeggiator::goingUp` unused. `scopeBuffer` read unsynchronized (benign). No `setLatencySamples` (fine, 0 latency).
