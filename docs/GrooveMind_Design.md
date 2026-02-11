# GrooveMind: ML-Powered Intelligent Drummer

## Vision

GrooveMind is an intelligent MIDI drum pattern generator for Linux users, inspired by Logic Pro's Drummer. Unlike simple pattern generators, GrooveMind uses machine learning models trained on professional drummer recordings to generate contextually appropriate, human-feeling drum patterns.

**Target Users**: Linux audio producers who want Logic Pro Drummer-like functionality in their DAW (Ardour, Reaper, Bitwig, etc.)

---

## Core Philosophy

1. **Real patterns from real drummers** - Not probability-based generation
2. **ML-powered humanization** - Micro-timing and velocity learned from recordings
3. **Context-aware** - Understands verse vs chorus, builds vs drops
4. **Runs locally** - No cloud dependencies, works offline
5. **Open source** - Available to the Linux audio community

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         GrooveMind                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │   Pattern    │     │   Groove     │     │   Context    │    │
│  │   Library    │────▶│  Humanizer   │────▶│   Engine     │    │
│  │  (500+ MIDI) │     │  (RTNeural)  │     │              │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│         │                    │                    │             │
│         │                    │                    │             │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │    Style     │     │    Fill      │     │   Follow     │    │
│  │  Classifier  │     │  Generator   │     │    Mode      │    │
│  │  (TinyNN)    │     │   (LSTM)     │     │  (Enhanced)  │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │   MIDI Output    │
                    │  (To any drum    │
                    │   sampler/kit)   │
                    └──────────────────┘
```

---

## Components

### 1. Pattern Library (Data Layer)

**Source**: Groove MIDI Dataset + curated open-source patterns

| Dataset | Patterns | Hours | License |
|---------|----------|-------|---------|
| Groove MIDI Dataset | 1,150 | 13.6 | CC BY 4.0 |
| Expanded GMD (E-GMD) | 43 drum kits | 444 | CC BY 4.0 |
| Curated additions | 200+ | TBD | Various open |

**Pattern Metadata Schema**:
```json
{
  "id": "rock_verse_medium_01",
  "style": "rock",
  "section": "verse",
  "energy": 0.6,
  "complexity": 0.5,
  "tempo_range": [90, 130],
  "time_signature": "4/4",
  "bars": 4,
  "tags": ["driving", "steady", "eighth-note-hats"],
  "source": "gmd",
  "drummer_id": "drummer_03"
}
```

**Storage**: MIDI files + JSON metadata index

---

### 2. Groove Humanizer (ML Model #1)

**Purpose**: Apply human micro-timing and velocity variation to patterns

**Architecture**: GrooVAE-style Variational Autoencoder
- Input: Quantized MIDI pattern (16th note grid)
- Output: Humanized pattern with micro-timing offsets + velocity curves
- Latent space: Encodes "groove feel" that can be interpolated

**Training Data**: Groove MIDI Dataset (has velocity + micro-timing annotations)

**Model Specs**:
- Size: 2-5 MB
- Inference: <10ms per bar
- Framework: RTNeural (C++, real-time safe)

**Features**:
- Groove intensity slider (0% = quantized, 100% = full human feel)
- Style transfer (apply rock groove to jazz pattern)
- Interpolation between groove styles

---

### 3. Fill Generator (ML Model #2)

**Purpose**: Generate contextually appropriate drum fills

**Architecture**: LSTM sequence model
- Input: Current pattern + section context + energy level
- Output: 1-2 bar fill pattern

**Training**: Fill patterns extracted from GMD (marked transitions)

**Model Specs**:
- Size: 1-2 MB
- Inference: <15ms per fill
- Framework: RTNeural

**Features**:
- Fill complexity control
- Fill length (1 beat, 2 beats, 1 bar, 2 bars)
- Style-appropriate fills (rock fills vs jazz fills)

---

### 4. Style Classifier (ML Model #3)

**Purpose**: Select appropriate patterns based on user parameters

**Architecture**: Small CNN/MLP
- Input: Style, energy, complexity, section parameters
- Output: Pattern rankings/probabilities

**Model Specs**:
- Size: <1 MB
- Inference: <5ms
- Framework: RTNeural

**Features**:
- Smart pattern selection (not random)
- Avoids repetition (tracks history)
- Energy-appropriate choices

---

### 5. Context Engine (Algorithmic)

**Purpose**: Track song position, energy curves, section transitions

**Features**:
- Section detection (via MIDI CC or manual)
- Energy ramping (gradual builds/drops)
- Transition awareness (pre-fill before chorus, etc.)
- Bar counting and phrase alignment

**Implementation**: C++ (no ML needed)

---

### 6. Follow Mode (Hybrid)

**Purpose**: Analyze incoming audio/MIDI and match groove

**Features**:
- Transient detection (onset analysis)
- Tempo tracking
- Groove extraction (micro-timing template)
- Apply detected groove to generated patterns

**Implementation**:
- Core analysis: C++ DSP
- Optional ML enhancement for genre detection

---

## User Interface

```
┌─────────────────────────────────────────────────────────────────┐
│  GrooveMind                                    [Follow] [Export]│
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Style: [Rock    ▼]    Drummer: [Alex - Stadium    ▼]          │
│                                                                  │
│  ┌─────────────────────────────────────┐                        │
│  │                                     │  Section: [Verse  ▼]   │
│  │           XY PAD                    │                        │
│  │                                     │  Energy ════════●═══   │
│  │     Complexity ←───→               │                        │
│  │          ↑                          │  Groove ═══●══════════  │
│  │          │                          │                        │
│  │          ↓                          │  ┌─────────────────┐   │
│  │       Loudness                      │  │ Fill: [Auto  ▼] │   │
│  │                                     │  │ Length: [1 bar] │   │
│  └─────────────────────────────────────┘  └─────────────────┘   │
│                                                                  │
│  Kit: [●] Kick  [●] Snare  [●] Hats  [●] Toms  [●] Cymbals    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Technical Stack

### Training Pipeline (Python)
```
Python 3.10+
├── PyTorch 2.x
├── pretty_midi (MIDI parsing)
├── Groove MIDI Dataset
├── Weights & Biases (experiment tracking)
└── Export: PyTorch → ONNX → RTNeural JSON
```

### Plugin Runtime (C++)
```
JUCE 7.x
├── RTNeural (ML inference, real-time safe)
├── MIDI generation
├── Pattern library (embedded or file-based)
└── Standard VST3/LV2/AU formats
```

### Model Deployment
```
Models embedded as binary resources
├── groove_humanizer.json (~3 MB)
├── fill_generator.json (~1.5 MB)
├── style_classifier.json (~0.5 MB)
└── Total: ~5 MB (acceptable for plugin distribution)
```

---

## Development Phases

### Phase 1: Foundation (Weeks 1-4)
- [ ] Set up Python training environment
- [ ] Download and analyze Groove MIDI Dataset
- [ ] Build pattern library with metadata tagging
- [ ] Create JUCE plugin skeleton with basic MIDI output
- [ ] Integrate RTNeural into build system

### Phase 2: Pattern System (Weeks 5-8)
- [ ] Implement pattern library loader
- [ ] Build pattern selection algorithm
- [ ] Create basic UI (style, drummer, XY pad)
- [ ] Test with raw patterns (no ML yet)
- [ ] Implement section/energy controls

### Phase 3: ML Training (Weeks 9-14)
- [ ] Train GrooVAE humanizer model
- [ ] Train fill generator LSTM
- [ ] Train style classifier
- [ ] Validate models offline (Python)
- [ ] Export to RTNeural format

### Phase 4: ML Integration (Weeks 15-18)
- [ ] Integrate humanizer into pattern playback
- [ ] Integrate fill generator
- [ ] Integrate style classifier for pattern selection
- [ ] Background thread for inference
- [ ] Pre-buffering system

### Phase 5: Follow Mode (Weeks 19-22)
- [ ] Implement transient detection
- [ ] Groove extraction from audio
- [ ] Apply extracted groove to patterns
- [ ] Tempo tracking
- [ ] UI for follow mode

### Phase 6: Polish (Weeks 23-26)
- [ ] Performance optimization
- [ ] A/B testing vs other drummers
- [ ] UI polish
- [ ] Documentation
- [ ] Beta testing with Linux audio community

---

## Model Training Details

### GrooVAE Humanizer

**Architecture**:
```
Encoder:
  - Bidirectional LSTM (128 units)
  - Dense → Latent (32 dim)

Decoder:
  - Dense from latent
  - LSTM (128 units)
  - Output: timing offsets + velocities
```

**Training**:
- Input: Quantized patterns (snapped to 16th grid)
- Target: Original patterns with micro-timing
- Loss: MSE on timing + velocity + KL divergence on latent

**Data Augmentation**:
- Tempo scaling
- Velocity scaling
- Style mixing

### Fill Generator

**Architecture**:
```
LSTM:
  - Embedding (drum hits → vectors)
  - LSTM layers (2x128 units)
  - Dense output (next hit probabilities)
```

**Training**:
- Sequence prediction on fill patterns
- Conditioned on: style, energy, length
- Teacher forcing during training

### Style Classifier

**Architecture**:
```
MLP:
  - Input: [style_onehot, energy, complexity, section]
  - Hidden: 64 → 32
  - Output: pattern_scores (softmax over library)
```

**Training**:
- Supervised classification
- Labels from pattern metadata

---

## Performance Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| Pattern inference | <5ms | Happens at bar start |
| Humanizer inference | <10ms | Per-bar, background thread |
| Fill generation | <15ms | Triggered ahead of time |
| Memory footprint | <50 MB | Patterns + models |
| CPU usage | <5% | Background processing |

---

## Comparison: GrooveMind vs Logic Drummer

| Feature | Logic Drummer | GrooveMind |
|---------|--------------|------------|
| Real drummer recordings | Yes (massive library) | Yes (Groove MIDI Dataset) |
| ML humanization | Unknown (proprietary) | Yes (GrooVAE) |
| Follow mode | Yes | Yes |
| Platform | macOS only | Linux (primary), cross-platform |
| Open source | No | Yes |
| Offline | Yes | Yes |
| Pattern count | Thousands | 500+ (growing) |

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| ML models too slow | Medium | High | Pre-buffer, background threads, model optimization |
| Training data insufficient | Low | High | GMD is high quality; supplement with other datasets |
| RTNeural integration issues | Low | Medium | Well-documented, used in production plugins |
| Patterns feel robotic | Medium | High | Focus on humanizer quality, A/B testing |

---

## Success Criteria

1. **Functional**: Generates playable drum patterns in real-time
2. **Musical**: Patterns feel human, not robotic
3. **Context-aware**: Appropriate patterns for verse/chorus/etc.
4. **Performant**: <5% CPU on typical Linux audio workstation
5. **Adopted**: Positive reception from Linux audio community

---

## References

### Datasets
- [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) - Primary training data
- [Expanded Groove MIDI (E-GMD)](https://magenta.withgoogle.com/datasets/e-gmd) - Extended dataset

### Papers
- [GrooVAE: Generating and Controlling Expressive Drum Performances](https://magenta.tensorflow.org/groovae)
- [Learning to Groove with Inverse Sequence Transformations](https://arxiv.org/abs/1905.06118)

### Libraries
- [RTNeural](https://github.com/jatinchowdhury18/RTNeural) - Real-time neural network inference
- [pretty_midi](https://github.com/craffel/pretty-midi) - MIDI parsing for Python

### Existing Projects
- [Magenta Studio](https://magenta.withgoogle.com/studio/) - Ableton plugins using similar tech
- [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) - Example of RTNeural in production

---

## License

GrooveMind will be released under GPLv3, consistent with other Dusk Audio plugins.

Training data (Groove MIDI Dataset) is CC BY 4.0 - attribution required in documentation.

---

*Document Version: 1.0*
*Created: January 2026*
*Author: Dusk Audio*
