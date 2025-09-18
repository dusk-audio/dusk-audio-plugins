# Luna Co. Audio Plugins

A collection of professional audio plugins built with JUCE.

## Plugins

### 4K EQ
A 4-band parametric equalizer with high-pass and low-pass filters, inspired by classic console channel strips.

### Universal Compressor
A versatile dynamics processor with multiple compression styles and advanced sidechain capabilities.

### Harmonic Generator
Add warmth and character with harmonic saturation and enhancement.

## Building

Each plugin can be built independently or all together using the provided build scripts.

### Prerequisites
- CMake 3.15+
- C++17 compatible compiler
- JUCE framework (included as submodule)

### Build All Plugins
```bash
./build_all.sh
```

### Build Individual Plugin
```bash
cd plugins/4k-eq
./build.sh
```

## License

See individual plugin directories for licensing information.
