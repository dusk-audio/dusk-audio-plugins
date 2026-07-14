#!/usr/bin/env bash
# render_ab.sh — render the A800 stimulus set through BOTH plugins at matched
# settings, into renders/{tapemachine,uad}/{speed}/{stimulus}.wav.
#
# Matched operating point (Studer A800, tape 456, NAB, Repro, +6 dB cal, unity
# in/out). 15 IPS is the UAD default and set explicitly on TapeMachine; 30 IPS
# is set on both. Wow/Flutter are ZEROED on TapeMachine for the clean-tone
# stimuli (sweep/thd/silence) and left at the shipped defaults (7/3) only for
# the wow&flutter tone — the UAD model's transport flutter is intrinsic and
# always on, so we compare TapeMachine-as-shipped against it.
#
# Usage: ./render_ab.sh            # renders 15 and 30 IPS
#        ./render_ab.sh 15         # single speed
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"
BIN="$REPO/build/tests/duskverb_render/duskverb_render"
MINE="$HOME/Library/Audio/Plug-Ins/Components/tape_machine_2.component"
UAD="/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component"
STIM="$HERE/stimuli"
OUT="$HERE/renders"
PRERUN=2   # seconds to let tape/transport state settle before capture

[ -x "$BIN" ] || { echo "ERROR: renderer not built: $BIN" >&2
  echo "  build it: cmake -B build -DBUILD_DUSKVERB_RENDER=ON && cmake --build build --target duskverb_render" >&2
  exit 1; }
[ -d "$STIM" ] || { echo "ERROR: no stimuli — run gen_stimuli.py first" >&2; exit 1; }

SPEEDS=("$@"); [ ${#SPEEDS[@]} -eq 0 ] && SPEEDS=(15 30)
STIMULI=(sweep thd_steps wf_3150 silence)

# tape-speed choice index for TapeMachine (0=7.5, 1=15, 2=30)
mine_speed_idx() { case "$1" in 15) echo 1;; 30) echo 2;; 7.5) echo 0;; *) echo 1;; esac; }

# quiet the licensing/objc chatter, keep real errors
run() { "$BIN" "$@" 2>&1 | grep -viE "TODO|^\[dpf\]|Thrift|@info|@warn|Timers|^ *$" || true; }

render() {  # render <plugin_path> <dest.wav> <input.wav> <prerun> <params...>
  local plug="$1" dest="$2" inp="$3" pre="$4"; shift 4
  local tmp; tmp="$(mktemp -d)"
  run --au "$plug" --input-wav "$inp" --slug s --output-dir "$tmp" --prerun-seconds "$pre" "$@" >/dev/null
  if [ ! -f "$tmp/s_stem.wav" ]; then echo "  ! render FAILED -> $dest" >&2; rm -rf "$tmp"; return 1; fi
  mv "$tmp/s_stem.wav" "$dest"; rm -rf "$tmp"
}

for spd in "${SPEEDS[@]}"; do
  midx="$(mine_speed_idx "$spd")"
  mkdir -p "$OUT/tapemachine/$spd" "$OUT/uad/$spd"
  echo "=== $spd IPS ==="
  for st in "${STIMULI[@]}"; do
    inp="$STIM/$st.wav"
    # TapeMachine: A800 / 456 / NAB / Repro / +6 cal / 4x OS. wow&flutter only for the W&F tone.
    if [ "$st" = "wf_3150" ]; then wow=7; flut=3; else wow=0; flut=0; fi
    echo "  TapeMachine  $st"
    render "$MINE" "$OUT/tapemachine/$spd/$st.wav" "$inp" "$PRERUN" \
      --param "Tape Machine=0" --param "Tape Speed=$midx" --param "Tape Type=0" \
      --param "EQ Standard=0" --param "Signal Path=0" --param "Calibration=1" \
      --param "Wow=$wow" --param "Flutter=$flut" --param "Noise Amount=0" --param "Oversampling=2"
    # UAD A800: defaults already match (456/NAB/Repro/+6/unity); only IPS varies.
    echo "  UAD A800     $st"
    render "$UAD" "$OUT/uad/$spd/$st.wav" "$inp" "$PRERUN" --param "IPS=$spd IPS"
  done
  # hiss: silence in, tape NOISE enabled on both, to compare the noise character.
  # Not level-matched — each at its nominal noise setting (Noise Amount 50 / Noise On).
  echo "  TapeMachine  hiss"
  render "$MINE" "$OUT/tapemachine/$spd/hiss.wav" "$STIM/silence.wav" "$PRERUN" \
    --param "Tape Machine=0" --param "Tape Speed=$midx" --param "Tape Type=0" \
    --param "EQ Standard=0" --param "Signal Path=0" --param "Calibration=1" \
    --param "Wow=0" --param "Flutter=0" --param "Noise Amount=50" --param "Oversampling=2"
  echo "  UAD A800     hiss"
  render "$UAD" "$OUT/uad/$spd/hiss.wav" "$STIM/silence.wav" "$PRERUN" \
    --param "IPS=$spd IPS" --param "Noise=On"
done
echo "Done. Renders in $OUT/"
