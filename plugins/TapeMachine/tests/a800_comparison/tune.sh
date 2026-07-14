#!/usr/bin/env bash
# tune.sh — one tuning iteration: rebuild TapeMachine AU, re-render MINE only
# (UAD reference is frozen), then print the scoreboard.
#   ./tune.sh          # rebuild + render 15 & 30 IPS + score
#   ./tune.sh 15       # single speed
#   ./tune.sh 7.5 15 30  # explicit speed list
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"
BIN="$REPO/build/tests/duskverb_render/duskverb_render"
MINE="$HOME/Library/Audio/Plug-Ins/Components/tape_machine_2.component"
STIM="$HERE/stimuli"; OUT="$HERE/renders/tapemachine"
SPEEDS=("$@"); [ ${#SPEEDS[@]} -eq 0 ] && SPEEDS=(15 30)

echo ">> building tape_machine_2-au ..."
( cd "$REPO/plugins/TapeMachine/dpf-plugin" && cmake --build build --target tape_machine_2-au -j8 >/dev/null )

run(){ "$BIN" "$@" 2>&1 | grep -viE "TODO|^\[dpf\]|Thrift|@info|@warn|Timers"; return "${PIPESTATUS[0]}"; }
render(){ local dest="$1" inp="$2"; shift 2; local t; t="$(mktemp -d)"
  run --au "$MINE" --input-wav "$inp" --slug s --output-dir "$t" --prerun-seconds 2 "$@"
  mv "$t/s_stem.wav" "$dest"; rm -rf "$t"; }

midx(){ case "$1" in 7.5) echo 0;; 15) echo 1;; 30) echo 2;;
  *) echo "unknown tape speed '$1' (want 7.5, 15 or 30)" >&2; exit 1;; esac; }
for spd in "${SPEEDS[@]}"; do
  m="$(midx "$spd")"; mkdir -p "$OUT/$spd"; echo ">> rendering mine @ $spd IPS"
  common=(--param "Tape Machine=0" --param "Tape Speed=$m" --param "Tape Type=0"
          --param "EQ Standard=0" --param "Signal Path=0" --param "Calibration=2"
          --param "Noise Amount=0" --param "Oversampling=2")
  render "$OUT/$spd/sweep.wav"     "$STIM/sweep.wav"     "${common[@]}" --param "Wow=0" --param "Flutter=0"
  render "$OUT/$spd/thd_steps.wav" "$STIM/thd_steps.wav" "${common[@]}" --param "Wow=0" --param "Flutter=0"
  render "$OUT/$spd/wf_3150.wav"   "$STIM/wf_3150.wav"   "${common[@]}" --param "Wow=7" --param "Flutter=3"
done
echo ">> scoring ..."
python3 "$HERE/tune_score.py" "${SPEEDS[@]}"
