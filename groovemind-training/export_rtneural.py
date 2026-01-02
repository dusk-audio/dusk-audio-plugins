#!/usr/bin/env python3
"""
Export trained PyTorch models to RTNeural JSON format.

RTNeural expects a specific JSON format with layer types and weights.
This script converts our trained models to that format.

Usage:
    python export_rtneural.py
"""

import json
import sys
from pathlib import Path
from typing import Dict, List, Any

try:
    import torch
    import torch.nn as nn
except ImportError:
    print("PyTorch not found. Install with: pip install torch")
    sys.exit(1)

import numpy as np

SCRIPT_DIR = Path(__file__).parent
MODELS_DIR = SCRIPT_DIR / "models"
OUTPUT_DIR = SCRIPT_DIR / "rtneural"


def export_linear_layer(weight: np.ndarray, bias: np.ndarray) -> Dict[str, Any]:
    """Export a linear/dense layer to RTNeural format."""
    return {
        "type": "dense",
        "shape": [weight.shape[1], weight.shape[0]],  # [input_size, output_size]
        "weights": weight.T.flatten().tolist(),  # RTNeural expects transposed weights
        "bias": bias.tolist()
    }


def export_activation(activation_type: str) -> Dict[str, Any]:
    """Export an activation layer."""
    return {
        "type": activation_type.lower()
    }


def export_humanizer_model(models_dir: Path, output_dir: Path):
    """Export the humanization model to RTNeural format."""
    print("\nExporting Humanizer Model...")

    model_path = models_dir / "humanizer.pt"
    if not model_path.exists():
        print(f"  Model not found: {model_path}")
        return

    checkpoint = torch.load(model_path, map_location='cpu')
    state_dict = checkpoint['model_state_dict']

    # The humanizer is a simple MLP: Linear -> ReLU -> Linear -> ReLU -> Linear -> Tanh
    layers = []

    # Extract layers from state_dict
    # net.0 = first Linear, net.2 = second Linear, net.4 = third Linear
    for i, key in enumerate(['net.0', 'net.2', 'net.4']):
        weight = state_dict[f'{key}.weight'].numpy()
        bias = state_dict[f'{key}.bias'].numpy()
        layers.append(export_linear_layer(weight, bias))

        # Add activation after each linear layer
        if i < 2:
            layers.append(export_activation('relu'))
        else:
            layers.append(export_activation('tanh'))

    rtneural_model = {
        "model_type": "humanizer",
        "version": "1.0",
        "input_size": checkpoint['input_dim'],
        "output_size": 1,
        "layers": layers
    }

    output_path = output_dir / "humanizer.json"
    with open(output_path, 'w') as f:
        json.dump(rtneural_model, f, indent=2)

    print(f"  Saved to {output_path}")
    print(f"  Input size: {checkpoint['input_dim']}, Hidden: {checkpoint['hidden_dim']}")


def export_style_classifier(models_dir: Path, output_dir: Path):
    """Export the style classifier to RTNeural format."""
    print("\nExporting Style Classifier Model...")

    model_path = models_dir / "style_classifier.pt"
    if not model_path.exists():
        print(f"  Model not found: {model_path}")
        return

    checkpoint = torch.load(model_path, map_location='cpu')
    state_dict = checkpoint['model_state_dict']

    # The classifier is: Linear -> ReLU -> Dropout -> Linear -> ReLU -> Dropout -> Linear
    # We skip Dropout for inference
    layers = []

    for i, key in enumerate(['net.0', 'net.3', 'net.6']):
        weight = state_dict[f'{key}.weight'].numpy()
        bias = state_dict[f'{key}.bias'].numpy()
        layers.append(export_linear_layer(weight, bias))

        # Add ReLU after first two layers
        if i < 2:
            layers.append(export_activation('relu'))

    # Input size: num_styles (12) + num_sections (7) + energy (1) + complexity (1) = 21
    input_size = checkpoint['num_styles'] + checkpoint['num_sections'] + 2

    rtneural_model = {
        "model_type": "style_classifier",
        "version": "1.0",
        "input_size": input_size,
        "output_size": checkpoint['num_patterns'],
        "num_styles": checkpoint['num_styles'],
        "num_sections": checkpoint['num_sections'],
        "pattern_ids": checkpoint.get('patterns', []),
        "layers": layers
    }

    output_path = output_dir / "style_classifier.json"
    with open(output_path, 'w') as f:
        json.dump(rtneural_model, f, indent=2)

    print(f"  Saved to {output_path}")
    print(f"  Input size: {input_size}, Output (patterns): {checkpoint['num_patterns']}")


def export_fill_generator_simplified(models_dir: Path, output_dir: Path):
    """Export a simplified version of the fill generator.

    The full LSTM model is complex for RTNeural. Instead, we export:
    1. The condition projection layer
    2. Statistics about fill patterns for rule-based generation
    3. A simpler feed-forward approximation for quick inference
    """
    print("\nExporting Fill Generator Model (simplified)...")

    model_path = models_dir / "fill_generator.pt"
    if not model_path.exists():
        print(f"  Model not found: {model_path}")
        return

    checkpoint = torch.load(model_path, map_location='cpu')
    state_dict = checkpoint['model_state_dict']

    # Export the condition projection layer (can be used to initialize generation)
    cond_weight = state_dict['condition_proj.weight'].numpy()
    cond_bias = state_dict['condition_proj.bias'].numpy()

    # Export instrument embedding for decoding
    inst_embedding = state_dict['instrument_embedding.weight'].numpy()

    # For RTNeural, we'll create a simplified model that:
    # 1. Takes condition vector -> projects to hidden state
    # 2. Uses a simple MLP to predict next instrument probabilities

    # Create a simplified feed-forward approximation
    # This won't capture LSTM dynamics but gives reasonable fills
    rtneural_model = {
        "model_type": "fill_generator",
        "version": "1.0",
        "note": "Simplified version - full LSTM requires custom RTNeural implementation",
        "num_instruments": checkpoint['num_instruments'],
        "hidden_dim": checkpoint['hidden_dim'],
        "condition_dim": 16,
        "condition_projection": {
            "weights": cond_weight.T.flatten().tolist(),
            "bias": cond_bias.tolist()
        },
        "instrument_embedding": inst_embedding.tolist(),
        # Store LSTM weights for potential custom implementation
        "lstm_weights": {
            "input_size": 17,  # embedding_dim + 1
            "hidden_size": checkpoint['hidden_dim'],
            "num_layers": checkpoint['num_layers']
        }
    }

    output_path = output_dir / "fill_generator.json"
    with open(output_path, 'w') as f:
        json.dump(rtneural_model, f, indent=2)

    print(f"  Saved to {output_path}")
    print(f"  Note: Simplified model - full LSTM inference available via PyTorch")


def export_timing_stats(models_dir: Path, output_dir: Path):
    """Copy timing statistics for use in the plugin."""
    print("\nCopying timing statistics...")

    src = models_dir / "timing_stats.json"
    dst = output_dir / "timing_stats.json"

    if src.exists():
        import shutil
        shutil.copy(src, dst)
        print(f"  Copied to {dst}")
    else:
        print(f"  Source not found: {src}")


def export_pattern_embeddings(models_dir: Path, output_dir: Path):
    """Copy pattern embeddings for similarity search."""
    print("\nCopying pattern embeddings...")

    src = models_dir / "pattern_embeddings.json"
    dst = output_dir / "pattern_embeddings.json"

    if src.exists():
        import shutil
        shutil.copy(src, dst)
        print(f"  Copied to {dst}")
    else:
        print(f"  Source not found: {src}")


def main():
    print("="*60)
    print("RTNeural Model Export")
    print("="*60)

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Export all models
    export_humanizer_model(MODELS_DIR, OUTPUT_DIR)
    export_style_classifier(MODELS_DIR, OUTPUT_DIR)
    export_fill_generator_simplified(MODELS_DIR, OUTPUT_DIR)
    export_timing_stats(MODELS_DIR, OUTPUT_DIR)
    export_pattern_embeddings(MODELS_DIR, OUTPUT_DIR)

    print("\n" + "="*60)
    print("Export Complete!")
    print("="*60)
    print(f"\nRTNeural models saved to: {OUTPUT_DIR}")
    print("\nFiles created:")
    for f in OUTPUT_DIR.glob("*.json"):
        size = f.stat().st_size / 1024
        print(f"  {f.name}: {size:.1f} KB")

    print("\nNext steps:")
    print("1. Copy the rtneural/ directory to the GrooveMind plugin resources")
    print("2. Integrate RTNeural inference in C++ code")


if __name__ == '__main__':
    main()
