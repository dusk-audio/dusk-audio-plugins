#!/usr/bin/env bash
# Re-optimize the 8 worst-performing Velvet 90 presets (sub-70% match score)
# after DSP improvements (exponential envelopes + ratio-based dynamics).
#
# Usage: ./reoptimize_worst8.sh
#
# Requires:
#   VELVET90_VST3  - path to Velvet 90 VST3 bundle
#   PCM90_IR_BASE  - path to "Lexicon PCM 90 Impulse Set" directory

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# --- Environment ---
export VELVET90_VST3="${VELVET90_VST3:-$HOME/projects/Luna/plugins/build_test/plugins/Velvet90/Velvet90_artefacts/Release/VST3/Velvet 90.vst3}"
export PCM90_IR_BASE="${PCM90_IR_BASE:-$HOME/Downloads/Lexicon PCM 90 Impulse Set}"

if [ ! -d "$VELVET90_VST3" ]; then
    echo "ERROR: Velvet 90 VST3 not found at: $VELVET90_VST3"
    echo "  Build first: cmake --build build_test --target Velvet90_VST3 --config Release -j8"
    exit 1
fi

if [ ! -d "$PCM90_IR_BASE" ]; then
    echo "ERROR: PCM 90 IR base not found at: $PCM90_IR_BASE"
    exit 1
fi

# Suppress BLAS threading (pedalboard uses numpy internally)
export OMP_NUM_THREADS=1
export MKL_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1

OUTPUT_DIR="results_v14_worst8"
MAX_ITER=200
LOG_FILE="v14_worst8.log"

echo "=== Velvet 90 Worst-8 Re-optimization ==="
echo "VST3:    $VELVET90_VST3"
echo "IR Base: $PCM90_IR_BASE"
echo "Output:  $OUTPUT_DIR"
echo "Max iter: $MAX_ITER"
echo ""

# The 8 worst presets (sub-70% in v13)
PRESETS=(
    "Inside-Out"
    "Patterns"
    "Air Pressure"
    "Strange Place"
    "Outdoor PA 2"
    "Coffin"
    "Sm Vocal Amb"
    "Mic Location"
)

mkdir -p "$OUTPUT_DIR"

echo "Starting optimization of ${#PRESETS[@]} presets..." | tee "$LOG_FILE"
echo "Start time: $(date)" | tee -a "$LOG_FILE"
echo "" | tee -a "$LOG_FILE"

# Run each preset sequentially (each gets full CPU for its phases)
for preset in "${PRESETS[@]}"; do
    echo "--- Optimizing: $preset ---" | tee -a "$LOG_FILE"
    python3 batch_optimize_all.py \
        --preset "$preset" \
        --no-resume \
        -o "$OUTPUT_DIR" \
        --max-iter "$MAX_ITER" \
        -j 1 \
        2>&1 | tee -a "$LOG_FILE"
    echo "" | tee -a "$LOG_FILE"
done

echo "=== Done ===" | tee -a "$LOG_FILE"
echo "End time: $(date)" | tee -a "$LOG_FILE"
echo ""
echo "Results in: $OUTPUT_DIR/"
echo "Log: $LOG_FILE"
