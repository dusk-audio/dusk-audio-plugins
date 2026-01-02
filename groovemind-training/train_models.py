#!/usr/bin/env python3
"""
GrooveMind ML Training Pipeline
Phase 3: Train pattern embedding and humanization models

This script trains two models:
1. Pattern Embedding Model - Maps patterns to a latent space for similarity search
2. Humanization Model - Learns micro-timing and velocity variations from GMD data

Requirements:
    pip install torch numpy mido scikit-learn

Usage:
    python train_models.py --task embeddings   # Train pattern embeddings
    python train_models.py --task humanizer    # Train humanization model
    python train_models.py --task all          # Train both
"""

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import numpy as np

# Check for required packages
try:
    import torch
    import torch.nn as nn
    import torch.optim as optim
    from torch.utils.data import Dataset, DataLoader
except ImportError:
    print("PyTorch not found. Install with: pip install torch")
    sys.exit(1)

try:
    import mido
except ImportError:
    print("mido not found. Install with: pip install mido")
    sys.exit(1)

# Constants
SCRIPT_DIR = Path(__file__).parent
LIBRARY_DIR = SCRIPT_DIR / "library"
GMD_DIR = SCRIPT_DIR / "groove"
OUTPUT_DIR = SCRIPT_DIR / "models"

# MIDI note mappings (General MIDI drum map)
GM_DRUM_MAP = {
    35: 'kick', 36: 'kick',
    37: 'sidestick', 38: 'snare', 39: 'clap', 40: 'snare',
    41: 'tom_low', 42: 'hihat_closed', 43: 'tom_low', 44: 'hihat_pedal',
    45: 'tom_mid', 46: 'hihat_open', 47: 'tom_mid', 48: 'tom_high',
    49: 'crash', 50: 'tom_high', 51: 'ride', 52: 'china',
    53: 'ride_bell', 54: 'tambourine', 55: 'splash', 56: 'cowbell',
    57: 'crash2', 58: 'vibraslap', 59: 'ride2'
}

# Simplified instrument categories for the model
INSTRUMENT_CATEGORIES = ['kick', 'snare', 'hihat', 'tom', 'cymbal', 'other']

def note_to_category(note: int) -> int:
    """Map MIDI note to instrument category index."""
    name = GM_DRUM_MAP.get(note, 'other')
    if 'kick' in name:
        return 0
    elif 'snare' in name or 'sidestick' in name or 'clap' in name:
        return 1
    elif 'hihat' in name:
        return 2
    elif 'tom' in name:
        return 3
    elif any(c in name for c in ['crash', 'ride', 'china', 'splash', 'cymbal']):
        return 4
    else:
        return 5


# =============================================================================
# Data Loading
# =============================================================================

class PatternDataset(Dataset):
    """Dataset for pattern embedding training."""

    def __init__(self, library_path: Path, sequence_length: int = 64):
        self.sequence_length = sequence_length
        self.patterns = []
        self.metadata = []

        # Load index
        index_path = library_path / "index.json"
        if not index_path.exists():
            raise FileNotFoundError(f"Pattern index not found: {index_path}")

        with open(index_path) as f:
            index = json.load(f)

        patterns_dir = library_path / "patterns"

        print(f"Loading patterns from {patterns_dir}...")
        for pattern_meta in index['patterns']:
            midi_path = patterns_dir / f"{pattern_meta['id']}.mid"
            if midi_path.exists():
                try:
                    features = self._extract_features(midi_path, pattern_meta)
                    if features is not None:
                        self.patterns.append(features)
                        self.metadata.append(pattern_meta)
                except Exception as e:
                    print(f"  Error loading {midi_path}: {e}")

        print(f"Loaded {len(self.patterns)} patterns")

    def _extract_features(self, midi_path: Path, meta: dict) -> Optional[np.ndarray]:
        """Extract feature matrix from MIDI file.

        Returns a matrix of shape (sequence_length, num_features) where features are:
        - One-hot instrument category (6)
        - Velocity (1, normalized)
        - Beat position within bar (1, 0-1)
        - Note duration (1, normalized)
        """
        try:
            midi = mido.MidiFile(str(midi_path))
        except Exception:
            return None

        # Get tempo from metadata
        tempo_bpm = meta.get('tempo', {}).get('bpm', 120)
        ticks_per_beat = midi.ticks_per_beat or 480

        # Collect all note events
        events = []
        current_time = 0

        for track in midi.tracks:
            track_time = 0
            for msg in track:
                track_time += msg.time
                if msg.type == 'note_on' and msg.velocity > 0:
                    # Convert ticks to beats
                    beat_pos = track_time / ticks_per_beat
                    events.append({
                        'beat': beat_pos,
                        'note': msg.note,
                        'velocity': msg.velocity / 127.0,
                        'category': note_to_category(msg.note)
                    })

        if not events:
            return None

        # Sort by time
        events.sort(key=lambda x: x['beat'])

        # Create feature matrix
        num_features = 6 + 3  # 6 instrument one-hot + velocity + beat_in_bar + duration_placeholder
        features = np.zeros((self.sequence_length, num_features), dtype=np.float32)

        for i, event in enumerate(events[:self.sequence_length]):
            # One-hot instrument category
            features[i, event['category']] = 1.0
            # Velocity
            features[i, 6] = event['velocity']
            # Beat position within bar (mod 4)
            features[i, 7] = (event['beat'] % 4) / 4.0
            # Duration placeholder (could be computed from note-off)
            features[i, 8] = 0.5

        return features

    def __len__(self):
        return len(self.patterns)

    def __getitem__(self, idx):
        return torch.tensor(self.patterns[idx]), self.metadata[idx]


class HumanizationDataset(Dataset):
    """Dataset for humanization model training using GMD data."""

    def __init__(self, gmd_path: Path):
        self.samples = []

        print(f"Loading GMD data from {gmd_path}...")

        # Walk through GMD directory structure
        for drummer_dir in gmd_path.iterdir():
            if not drummer_dir.is_dir():
                continue

            for session_dir in drummer_dir.iterdir():
                if not session_dir.is_dir():
                    continue

                for midi_file in session_dir.glob("*.mid"):
                    try:
                        samples = self._extract_timing_samples(midi_file)
                        self.samples.extend(samples)
                    except Exception as e:
                        print(f"  Error processing {midi_file}: {e}")

        print(f"Extracted {len(self.samples)} timing samples")

    def _extract_timing_samples(self, midi_path: Path) -> List[dict]:
        """Extract timing deviation samples from MIDI.

        For each note, we extract:
        - Instrument category
        - Quantized beat position (where it "should" be)
        - Actual timing offset in ms (the humanization)
        - Velocity
        - Context (surrounding notes)
        """
        samples = []

        try:
            midi = mido.MidiFile(str(midi_path))
        except Exception:
            return samples

        ticks_per_beat = midi.ticks_per_beat or 480

        # Assume 120 BPM for GMD (they normalize to this)
        ms_per_tick = (60000 / 120) / ticks_per_beat

        # Collect note events
        events = []
        for track in midi.tracks:
            track_time = 0
            for msg in track:
                track_time += msg.time
                if msg.type == 'note_on' and msg.velocity > 0:
                    events.append({
                        'tick': track_time,
                        'beat': track_time / ticks_per_beat,
                        'note': msg.note,
                        'velocity': msg.velocity,
                        'category': note_to_category(msg.note)
                    })

        if not events:
            return samples

        events.sort(key=lambda x: x['tick'])

        # For each event, compute timing offset from quantized position
        for i, event in enumerate(events):
            beat = event['beat']

            # Quantize to 16th notes (0.25 beats)
            quantized_beat = round(beat * 4) / 4
            timing_offset_beats = beat - quantized_beat
            timing_offset_ms = timing_offset_beats * (60000 / 120)  # at 120 BPM

            # Skip events with very large offsets (likely errors)
            if abs(timing_offset_ms) > 100:
                continue

            # Build context: previous and next note categories
            prev_category = events[i-1]['category'] if i > 0 else -1
            next_category = events[i+1]['category'] if i < len(events)-1 else -1

            # Beat position in bar
            beat_in_bar = quantized_beat % 4

            samples.append({
                'category': event['category'],
                'beat_in_bar': beat_in_bar,
                'velocity': event['velocity'] / 127.0,
                'timing_offset_ms': timing_offset_ms,
                'prev_category': prev_category,
                'next_category': next_category
            })

        return samples

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        sample = self.samples[idx]

        # Input features
        features = np.zeros(6 + 1 + 1 + 6 + 6, dtype=np.float32)  # cat + beat + vel + prev_cat + next_cat

        # Current instrument one-hot
        if sample['category'] >= 0:
            features[sample['category']] = 1.0

        # Beat position (normalized to 0-1)
        features[6] = sample['beat_in_bar'] / 4.0

        # Velocity
        features[7] = sample['velocity']

        # Previous category one-hot
        if sample['prev_category'] >= 0:
            features[8 + sample['prev_category']] = 1.0

        # Next category one-hot
        if sample['next_category'] >= 0:
            features[14 + sample['next_category']] = 1.0

        # Target: timing offset (normalized to roughly -1 to 1)
        target = sample['timing_offset_ms'] / 50.0  # 50ms = 1.0

        return torch.tensor(features), torch.tensor([target], dtype=torch.float32)


# =============================================================================
# Models
# =============================================================================

class PatternEmbedder(nn.Module):
    """Encodes drum patterns into fixed-size embeddings for similarity search."""

    def __init__(self, input_dim: int = 9, hidden_dim: int = 64, embedding_dim: int = 32):
        super().__init__()

        # Process sequence with 1D conv layers
        self.conv1 = nn.Conv1d(input_dim, hidden_dim, kernel_size=3, padding=1)
        self.conv2 = nn.Conv1d(hidden_dim, hidden_dim, kernel_size=3, padding=1)
        self.conv3 = nn.Conv1d(hidden_dim, hidden_dim, kernel_size=3, padding=1)

        # Pool and project to embedding
        self.pool = nn.AdaptiveAvgPool1d(1)
        self.fc = nn.Linear(hidden_dim, embedding_dim)

        self.relu = nn.ReLU()

    def forward(self, x):
        # x: (batch, seq_len, features)
        x = x.transpose(1, 2)  # (batch, features, seq_len)

        x = self.relu(self.conv1(x))
        x = self.relu(self.conv2(x))
        x = self.relu(self.conv3(x))

        x = self.pool(x).squeeze(-1)  # (batch, hidden_dim)
        x = self.fc(x)  # (batch, embedding_dim)

        # L2 normalize for cosine similarity
        x = x / (x.norm(dim=1, keepdim=True) + 1e-8)

        return x


class HumanizationModel(nn.Module):
    """Predicts timing offsets for humanization."""

    def __init__(self, input_dim: int = 20, hidden_dim: int = 32):
        super().__init__()

        self.net = nn.Sequential(
            nn.Linear(input_dim, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, 1),
            nn.Tanh()  # Output in [-1, 1] range
        )

    def forward(self, x):
        return self.net(x)


class FillGenerator(nn.Module):
    """LSTM-based fill pattern generator.

    Generates drum fill sequences conditioned on style, energy, and length.
    """

    def __init__(self, num_instruments: int = 6, hidden_dim: int = 64,
                 num_layers: int = 2, embedding_dim: int = 16):
        super().__init__()

        self.num_instruments = num_instruments
        self.hidden_dim = hidden_dim
        self.num_layers = num_layers

        # Embed instrument hits
        self.instrument_embedding = nn.Embedding(num_instruments + 1, embedding_dim)  # +1 for silence

        # Condition embedding (style, energy, length)
        self.condition_dim = 16  # style one-hot (12) + energy (1) + complexity (1) + length (2)
        self.condition_proj = nn.Linear(self.condition_dim, hidden_dim)

        # LSTM for sequence generation
        self.lstm = nn.LSTM(
            input_size=embedding_dim + 1,  # instrument + velocity
            hidden_size=hidden_dim,
            num_layers=num_layers,
            batch_first=True,
            dropout=0.2 if num_layers > 1 else 0
        )

        # Output heads
        self.instrument_head = nn.Linear(hidden_dim, num_instruments + 1)  # Which instrument (or silence)
        self.velocity_head = nn.Linear(hidden_dim, 1)  # Velocity (0-1)
        self.timing_head = nn.Linear(hidden_dim, 1)  # Timing offset (-1 to 1)

    def forward(self, x, velocities, conditions, hidden=None):
        """
        x: (batch, seq_len) instrument indices
        velocities: (batch, seq_len) normalized velocities
        conditions: (batch, condition_dim) style/energy/length conditions
        """
        batch_size = x.size(0)

        # Embed instruments
        inst_emb = self.instrument_embedding(x)  # (batch, seq, emb_dim)

        # Concatenate with velocity
        combined = torch.cat([inst_emb, velocities.unsqueeze(-1)], dim=-1)

        # Initialize hidden state with condition info
        if hidden is None:
            cond_proj = self.condition_proj(conditions)  # (batch, hidden)
            h0 = cond_proj.unsqueeze(0).repeat(self.num_layers, 1, 1)
            c0 = torch.zeros_like(h0)
            hidden = (h0, c0)

        # Run LSTM
        lstm_out, hidden = self.lstm(combined, hidden)

        # Generate outputs
        inst_logits = self.instrument_head(lstm_out)
        velocity_pred = torch.sigmoid(self.velocity_head(lstm_out))
        timing_pred = torch.tanh(self.timing_head(lstm_out))

        return inst_logits, velocity_pred, timing_pred, hidden

    def generate(self, conditions, length: int = 16, temperature: float = 1.0):
        """Generate a fill sequence autoregressively."""
        batch_size = conditions.size(0)
        device = conditions.device

        # Start with silence token
        current_inst = torch.zeros(batch_size, 1, dtype=torch.long, device=device)
        current_vel = torch.zeros(batch_size, 1, device=device)

        generated_insts = []
        generated_vels = []
        generated_timings = []

        hidden = None

        for _ in range(length):
            inst_logits, vel_pred, timing_pred, hidden = self.forward(
                current_inst, current_vel, conditions, hidden
            )

            # Sample next instrument
            inst_probs = torch.softmax(inst_logits[:, -1, :] / temperature, dim=-1)
            next_inst = torch.multinomial(inst_probs, 1)

            generated_insts.append(next_inst)
            generated_vels.append(vel_pred[:, -1, :])
            generated_timings.append(timing_pred[:, -1, :])

            current_inst = next_inst
            current_vel = vel_pred[:, -1, :]

        return (
            torch.cat(generated_insts, dim=1),
            torch.cat(generated_vels, dim=1),
            torch.cat(generated_timings, dim=1)
        )


class StyleClassifier(nn.Module):
    """Classifies patterns by style and selects appropriate patterns.

    Input: Style parameters (energy, complexity, section)
    Output: Pattern scores/rankings
    """

    def __init__(self, num_styles: int = 12, num_sections: int = 7,
                 hidden_dim: int = 64, num_patterns: int = 500):
        super().__init__()

        # Input: style_onehot (12) + section_onehot (7) + energy (1) + complexity (1)
        input_dim = num_styles + num_sections + 2

        self.net = nn.Sequential(
            nn.Linear(input_dim, hidden_dim),
            nn.ReLU(),
            nn.Dropout(0.2),
            nn.Linear(hidden_dim, hidden_dim),
            nn.ReLU(),
            nn.Dropout(0.2),
            nn.Linear(hidden_dim, num_patterns)
        )

        self.num_styles = num_styles
        self.num_sections = num_sections

    def forward(self, style_idx, section_idx, energy, complexity):
        """
        style_idx: (batch,) style indices
        section_idx: (batch,) section indices
        energy: (batch,) energy values 0-1
        complexity: (batch,) complexity values 0-1
        """
        batch_size = style_idx.size(0)
        device = style_idx.device

        # Create one-hot encodings
        style_onehot = torch.zeros(batch_size, self.num_styles, device=device)
        style_onehot.scatter_(1, style_idx.unsqueeze(1), 1)

        section_onehot = torch.zeros(batch_size, self.num_sections, device=device)
        section_onehot.scatter_(1, section_idx.unsqueeze(1), 1)

        # Concatenate all inputs
        x = torch.cat([
            style_onehot,
            section_onehot,
            energy.unsqueeze(1),
            complexity.unsqueeze(1)
        ], dim=1)

        return self.net(x)


# =============================================================================
# Training Functions
# =============================================================================

def train_embeddings(library_path: Path, output_path: Path, epochs: int = 100):
    """Train pattern embedding model using contrastive learning."""
    print("\n" + "="*60)
    print("Training Pattern Embedding Model")
    print("="*60)

    # Load data
    dataset = PatternDataset(library_path)

    if len(dataset) < 10:
        print("Not enough patterns for training")
        return

    # Create model
    model = PatternEmbedder(input_dim=9, hidden_dim=64, embedding_dim=32)
    optimizer = optim.Adam(model.parameters(), lr=0.001)

    # Contrastive loss: similar patterns should have similar embeddings
    def contrastive_loss(embeddings, metadata, margin=0.5):
        """
        Patterns with same style should be closer than patterns with different styles.
        """
        batch_size = embeddings.size(0)
        if batch_size < 2:
            return torch.tensor(0.0)

        loss = 0.0
        count = 0

        for i in range(batch_size):
            for j in range(i + 1, batch_size):
                dist = (embeddings[i] - embeddings[j]).pow(2).sum().sqrt()

                # Same style = positive pair
                same_style = metadata[i].get('style') == metadata[j].get('style')

                if same_style:
                    # Pull together
                    loss += dist.pow(2)
                else:
                    # Push apart (with margin)
                    loss += torch.clamp(margin - dist, min=0).pow(2)

                count += 1

        return loss / max(count, 1)

    # Custom collate function to handle metadata dicts
    def pattern_collate(batch):
        features = torch.stack([item[0] for item in batch])
        metadata = [item[1] for item in batch]  # Keep as list of dicts
        return features, metadata

    # Training loop
    batch_size = 32
    dataloader = DataLoader(dataset, batch_size=batch_size, shuffle=True, collate_fn=pattern_collate)

    print(f"Training for {epochs} epochs...")
    for epoch in range(epochs):
        total_loss = 0.0
        num_batches = 0

        for features, metadata in dataloader:
            optimizer.zero_grad()

            embeddings = model(features)
            loss = contrastive_loss(embeddings, metadata)

            if loss.item() > 0:
                loss.backward()
                optimizer.step()

            total_loss += loss.item()
            num_batches += 1

        if (epoch + 1) % 10 == 0:
            avg_loss = total_loss / max(num_batches, 1)
            print(f"  Epoch {epoch+1}/{epochs}, Loss: {avg_loss:.4f}")

    # Save model
    output_path.mkdir(parents=True, exist_ok=True)
    model_path = output_path / "pattern_embedder.pt"
    torch.save({
        'model_state_dict': model.state_dict(),
        'input_dim': 9,
        'hidden_dim': 64,
        'embedding_dim': 32
    }, model_path)
    print(f"\nSaved model to {model_path}")

    # Generate embeddings for all patterns
    print("\nGenerating embeddings for all patterns...")
    model.eval()
    embeddings_data = []

    nan_count = 0
    with torch.no_grad():
        for i in range(len(dataset)):
            features, meta = dataset[i]
            embedding = model(features.unsqueeze(0)).squeeze(0).numpy()

            # Check for NaN values and replace with None (becomes null in JSON)
            embedding_list = embedding.tolist()
            has_nan = any(np.isnan(v) for v in embedding_list)
            if has_nan:
                nan_count += 1
                embedding_list = [None if np.isnan(v) else v for v in embedding_list]

            embeddings_data.append({
                'id': meta['id'],
                'style': meta.get('style', ''),
                'embedding': embedding_list
            })

    if nan_count > 0:
        print(f"  WARNING: {nan_count} patterns have NaN embeddings (replaced with null)")
        print(f"  This may indicate training issues - consider retraining with more data")

    embeddings_path = output_path / "pattern_embeddings.json"
    with open(embeddings_path, 'w') as f:
        json.dump(embeddings_data, f, indent=2)
    print(f"Saved embeddings to {embeddings_path}")


def train_humanizer(gmd_path: Path, output_path: Path, epochs: int = 50):
    """Train humanization model on GMD timing data."""
    print("\n" + "="*60)
    print("Training Humanization Model")
    print("="*60)

    # Load data
    dataset = HumanizationDataset(gmd_path)

    if len(dataset) < 100:
        print("Not enough timing samples for training")
        return

    # Create model
    model = HumanizationModel(input_dim=20, hidden_dim=32)
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    criterion = nn.MSELoss()

    # Training loop
    batch_size = 64
    dataloader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

    print(f"Training for {epochs} epochs on {len(dataset)} samples...")
    for epoch in range(epochs):
        total_loss = 0.0
        num_batches = 0

        for features, targets in dataloader:
            optimizer.zero_grad()

            predictions = model(features)
            loss = criterion(predictions, targets)

            loss.backward()
            optimizer.step()

            total_loss += loss.item()
            num_batches += 1

        if (epoch + 1) % 10 == 0:
            avg_loss = total_loss / max(num_batches, 1)
            print(f"  Epoch {epoch+1}/{epochs}, MSE Loss: {avg_loss:.4f}")

    # Save model
    output_path.mkdir(parents=True, exist_ok=True)
    model_path = output_path / "humanizer.pt"
    torch.save({
        'model_state_dict': model.state_dict(),
        'input_dim': 20,
        'hidden_dim': 32
    }, model_path)
    print(f"\nSaved model to {model_path}")

    # Also save statistics for the humanizer
    print("\nComputing timing statistics by instrument...")
    stats = compute_timing_stats(dataset)
    stats_path = output_path / "timing_stats.json"
    with open(stats_path, 'w') as f:
        json.dump(stats, f, indent=2)
    print(f"Saved statistics to {stats_path}")


def compute_timing_stats(dataset: HumanizationDataset) -> dict:
    """Compute per-instrument timing statistics."""
    from collections import defaultdict

    timing_by_category = defaultdict(list)
    velocity_by_category = defaultdict(list)

    for sample in dataset.samples:
        cat = INSTRUMENT_CATEGORIES[sample['category']] if sample['category'] >= 0 else 'other'
        timing_by_category[cat].append(sample['timing_offset_ms'])
        velocity_by_category[cat].append(sample['velocity'] * 127)

    stats = {}
    for cat in INSTRUMENT_CATEGORIES:
        if cat in timing_by_category and len(timing_by_category[cat]) > 0:
            timings = timing_by_category[cat]
            velocities = velocity_by_category[cat]
            stats[cat] = {
                'timing_mean_ms': float(np.mean(timings)),
                'timing_std_ms': float(np.std(timings)),
                'timing_median_ms': float(np.median(timings)),
                'velocity_mean': float(np.mean(velocities)),
                'velocity_std': float(np.std(velocities)),
                'sample_count': len(timings)
            }

    return stats


class FillDataset(Dataset):
    """Dataset for fill generator training using GMD fill patterns."""

    def __init__(self, gmd_path: Path, sequence_length: int = 32):
        self.sequence_length = sequence_length
        self.fills = []

        print(f"Loading fill patterns from {gmd_path}...")

        # Walk through GMD and find fill patterns
        for midi_file in gmd_path.rglob("*.mid"):
            filename = midi_file.name.lower()
            # GMD fill patterns have "fill" in the name
            if 'fill' in filename:
                try:
                    fill_data = self._extract_fill(midi_file, filename)
                    if fill_data is not None:
                        self.fills.append(fill_data)
                except Exception as e:
                    pass  # Skip problematic files

        print(f"Loaded {len(self.fills)} fill patterns")

    def _parse_style_from_filename(self, filename: str) -> int:
        """Extract style index from GMD filename."""
        styles = ['rock', 'pop', 'funk', 'soul', 'jazz', 'blues',
                  'hiphop', 'latin', 'afro', 'reggae', 'country', 'punk']
        filename_lower = filename.lower()
        for i, style in enumerate(styles):
            if style in filename_lower:
                return i
        return 0  # Default to rock

    def _extract_fill(self, midi_path: Path, filename: str) -> Optional[dict]:
        """Extract fill sequence from MIDI file."""
        try:
            midi = mido.MidiFile(str(midi_path))
        except Exception:
            return None

        ticks_per_beat = midi.ticks_per_beat or 480

        # Collect note events
        events = []
        for track in midi.tracks:
            track_time = 0
            for msg in track:
                track_time += msg.time
                if msg.type == 'note_on' and msg.velocity > 0:
                    events.append({
                        'tick': track_time,
                        'beat': track_time / ticks_per_beat,
                        'category': note_to_category(msg.note),
                        'velocity': msg.velocity / 127.0
                    })

        if len(events) < 4:  # Need at least a few notes for a fill
            return None

        events.sort(key=lambda x: x['tick'])

        # Convert to sequence (quantized to 16th notes)
        max_beats = 8  # Max 2 bars for a fill
        steps_per_beat = 4  # 16th note resolution
        max_steps = max_beats * steps_per_beat

        instruments = np.zeros(min(self.sequence_length, max_steps), dtype=np.int64)
        velocities = np.zeros(min(self.sequence_length, max_steps), dtype=np.float32)

        for event in events:
            step = int(event['beat'] * steps_per_beat)
            if step < len(instruments):
                instruments[step] = event['category'] + 1  # +1 because 0 is silence
                velocities[step] = event['velocity']

        # Pad if needed
        if len(instruments) < self.sequence_length:
            instruments = np.pad(instruments, (0, self.sequence_length - len(instruments)))
            velocities = np.pad(velocities, (0, self.sequence_length - len(velocities)))

        style_idx = self._parse_style_from_filename(filename)

        return {
            'instruments': instruments,
            'velocities': velocities,
            'style': style_idx,
            'energy': np.mean(velocities[velocities > 0]) if np.any(velocities > 0) else 0.5,
            'complexity': np.sum(instruments > 0) / len(instruments)
        }

    def __len__(self):
        return len(self.fills)

    def __getitem__(self, idx):
        fill = self.fills[idx]

        # Create condition vector
        conditions = np.zeros(16, dtype=np.float32)
        conditions[fill['style']] = 1.0  # Style one-hot (first 12)
        conditions[12] = fill['energy']
        conditions[13] = fill['complexity']
        # Length encoding (14-15)
        fill_length = np.sum(fill['instruments'] > 0)
        conditions[14] = min(fill_length / 16.0, 1.0)  # Short fill indicator
        conditions[15] = min(fill_length / 32.0, 1.0)  # Long fill indicator

        return (
            torch.tensor(fill['instruments']),
            torch.tensor(fill['velocities']),
            torch.tensor(conditions)
        )


def train_fill_generator(gmd_path: Path, output_path: Path, epochs: int = 100):
    """Train the fill generator LSTM model."""
    print("\n" + "="*60)
    print("Training Fill Generator Model")
    print("="*60)

    # Load data
    dataset = FillDataset(gmd_path)

    if len(dataset) < 20:
        print("Not enough fill patterns for training")
        return

    # Create model
    model = FillGenerator(num_instruments=6, hidden_dim=64, num_layers=2)
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    criterion = nn.CrossEntropyLoss()

    # Training loop
    batch_size = 32
    dataloader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

    print(f"Training for {epochs} epochs on {len(dataset)} fills...")
    for epoch in range(epochs):
        total_loss = 0.0
        num_batches = 0

        for instruments, velocities, conditions in dataloader:
            optimizer.zero_grad()

            # Teacher forcing: use actual sequence as input, predict next step
            input_inst = instruments[:, :-1]
            input_vel = velocities[:, :-1]
            target_inst = instruments[:, 1:]

            inst_logits, vel_pred, timing_pred, _ = model(
                input_inst, input_vel, conditions
            )

            # Loss on instrument prediction
            loss = criterion(
                inst_logits.reshape(-1, inst_logits.size(-1)),
                target_inst.reshape(-1)
            )

            loss.backward()
            optimizer.step()

            total_loss += loss.item()
            num_batches += 1

        if (epoch + 1) % 20 == 0:
            avg_loss = total_loss / max(num_batches, 1)
            print(f"  Epoch {epoch+1}/{epochs}, Loss: {avg_loss:.4f}")

    # Save model
    output_path.mkdir(parents=True, exist_ok=True)
    model_path = output_path / "fill_generator.pt"
    torch.save({
        'model_state_dict': model.state_dict(),
        'num_instruments': 6,
        'hidden_dim': 64,
        'num_layers': 2
    }, model_path)
    print(f"\nSaved model to {model_path}")


def train_style_classifier(library_path: Path, output_path: Path, epochs: int = 100):
    """Train the style classifier model."""
    print("\n" + "="*60)
    print("Training Style Classifier Model")
    print("="*60)

    # Load pattern library index
    index_path = library_path / "index.json"
    if not index_path.exists():
        print(f"Pattern library index not found: {index_path}")
        return

    with open(index_path) as f:
        index = json.load(f)

    patterns = index.get('patterns', [])
    if len(patterns) < 20:
        print("Not enough patterns for training")
        return

    # Create style/section mappings
    styles = ['rock', 'pop', 'funk', 'soul', 'jazz', 'blues',
              'hiphop', 'rnb', 'electronic', 'latin', 'country', 'punk']
    sections = ['intro', 'verse', 'pre-chorus', 'chorus', 'bridge', 'breakdown', 'outro']

    style_to_idx = {s: i for i, s in enumerate(styles)}
    section_to_idx = {s: i for i, s in enumerate(sections)}

    # Build training data: for each pattern, create samples of (style, section, energy, complexity) -> pattern_idx
    samples = []
    for i, pattern in enumerate(patterns):
        style = pattern.get('style', 'rock').lower()
        section = pattern.get('section', 'verse').lower()
        energy = pattern.get('energy', 0.5)
        complexity = pattern.get('complexity', 0.5)

        style_idx = style_to_idx.get(style, 0)
        section_idx = section_to_idx.get(section, 1)

        samples.append({
            'style_idx': style_idx,
            'section_idx': section_idx,
            'energy': energy,
            'complexity': complexity,
            'pattern_idx': i
        })

    print(f"Created {len(samples)} training samples from {len(patterns)} patterns")

    # Create model
    num_patterns = len(patterns)
    model = StyleClassifier(num_styles=12, num_sections=7, hidden_dim=64, num_patterns=num_patterns)
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    criterion = nn.CrossEntropyLoss()

    # Training loop with data augmentation
    print(f"Training for {epochs} epochs...")
    for epoch in range(epochs):
        total_loss = 0.0
        num_batches = 0

        # Shuffle samples
        np.random.shuffle(samples)

        # Process in batches
        batch_size = 32
        for batch_start in range(0, len(samples), batch_size):
            batch = samples[batch_start:batch_start + batch_size]

            optimizer.zero_grad()

            # Prepare batch tensors
            style_idx = torch.tensor([s['style_idx'] for s in batch], dtype=torch.long)
            section_idx = torch.tensor([s['section_idx'] for s in batch], dtype=torch.long)
            energy = torch.tensor([s['energy'] for s in batch], dtype=torch.float32)
            complexity = torch.tensor([s['complexity'] for s in batch], dtype=torch.float32)
            targets = torch.tensor([s['pattern_idx'] for s in batch], dtype=torch.long)

            # Add noise for augmentation
            energy = energy + torch.randn_like(energy) * 0.1
            energy = torch.clamp(energy, 0, 1)
            complexity = complexity + torch.randn_like(complexity) * 0.1
            complexity = torch.clamp(complexity, 0, 1)

            # Forward pass
            logits = model(style_idx, section_idx, energy, complexity)
            loss = criterion(logits, targets)

            loss.backward()
            optimizer.step()

            total_loss += loss.item()
            num_batches += 1

        if (epoch + 1) % 20 == 0:
            avg_loss = total_loss / max(num_batches, 1)
            print(f"  Epoch {epoch+1}/{epochs}, Loss: {avg_loss:.4f}")

    # Save model
    output_path.mkdir(parents=True, exist_ok=True)
    model_path = output_path / "style_classifier.pt"
    torch.save({
        'model_state_dict': model.state_dict(),
        'num_styles': 12,
        'num_sections': 7,
        'hidden_dim': 64,
        'num_patterns': num_patterns,
        'patterns': [p['id'] for p in patterns]  # Save pattern ID list for lookup
    }, model_path)
    print(f"\nSaved model to {model_path}")


# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description='GrooveMind ML Training Pipeline')
    parser.add_argument('--task', choices=['embeddings', 'humanizer', 'fills', 'classifier', 'all'],
                        default='all', help='Which model(s) to train')
    parser.add_argument('--epochs-embed', type=int, default=100,
                        help='Epochs for embedding model')
    parser.add_argument('--epochs-humanize', type=int, default=50,
                        help='Epochs for humanization model')
    parser.add_argument('--epochs-fills', type=int, default=100,
                        help='Epochs for fill generator model')
    parser.add_argument('--epochs-classifier', type=int, default=100,
                        help='Epochs for style classifier model')
    args = parser.parse_args()

    # Check paths
    if not LIBRARY_DIR.exists():
        print(f"Error: Pattern library not found at {LIBRARY_DIR}")
        print("Run build_pattern_library.py first!")
        sys.exit(1)

    if not GMD_DIR.exists():
        print(f"Error: GMD data not found at {GMD_DIR}")
        print("Download and extract the Groove MIDI Dataset first!")
        sys.exit(1)

    # Train models
    if args.task in ['embeddings', 'all']:
        train_embeddings(LIBRARY_DIR, OUTPUT_DIR, epochs=args.epochs_embed)

    if args.task in ['humanizer', 'all']:
        train_humanizer(GMD_DIR, OUTPUT_DIR, epochs=args.epochs_humanize)

    if args.task in ['fills', 'all']:
        train_fill_generator(GMD_DIR, OUTPUT_DIR, epochs=args.epochs_fills)

    if args.task in ['classifier', 'all']:
        train_style_classifier(LIBRARY_DIR, OUTPUT_DIR, epochs=args.epochs_classifier)

    print("\n" + "="*60)
    print("Training Complete!")
    print("="*60)
    print(f"\nModels saved to: {OUTPUT_DIR}")
    print("\nNext steps:")
    print("1. Run export_rtneural.py to convert models to RTNeural JSON format")
    print("2. Integrate into the GrooveMind plugin")


if __name__ == '__main__':
    main()
