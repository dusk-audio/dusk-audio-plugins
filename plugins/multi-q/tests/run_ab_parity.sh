#!/usr/bin/env bash
# run_ab_parity.sh — JUCE-vs-DAF Multi-Q parity harness (one command).
#
# Checks selected DAF port ("Multi-Q 2") paths against the JUCE original:
#   1. Parameter names + order identical (catches silent reorders).
#   2. Choice-driven params resolve on both (params set by display text).
#   3. Digital / bypass at 1x null to the float floor (bit-exact DSP).
#   4. Active Digital / Tube / oversampled paths match in magnitude response
#      (median |Δmag| tolerance) — the correct test where group delays differ.
#
# British uses the upgraded 4K core and needs separate reference/deviation
# evidence. Match, dynamics, limiter, auto-gain and solo also need separate gates.
# These checks alone do not complete #148. The 1x latency-
# reporting difference (JUCE reports constant max-oversampler latency for PDC;
# DAF Digital reports 0) is reported, not gated.
#
# Usage: run_ab_parity.sh [JUCE.clap] [DAF.clap]
# Set CLAP_AB_KEEP=1 to retain successful renders too; failures are always kept.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
JCLAP="${1:-$REPO/build/bin/CLAP/Multi-Q.clap}"
DCLAP="${2:-$REPO/plugins/multi-q/daf-plugin/build/bin/multi-q-2.clap}"
CLAPINC="$REPO/external/clap-juce-extensions/clap-libs/clap/include"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/dusk-multiq-parity.XXXXXX")"
HOST="$WORK/clap_ab_host"
NULL="$HERE/ab_null.py"
fail=0
cleanup() {
    local status=$?
    if (( status != 0 || fail != 0 )) || [[ "${CLAP_AB_KEEP:-0}" == 1 ]]; then
        echo "A/B artifacts: $WORK"
    else
        rm -rf -- "$WORK"
    fi
}
trap cleanup EXIT

echo "== measurement validity controls =="
python3 "$HERE/test_ab_null.py"

echo "== building A/B host =="
g++ -std=c++17 -O2 -I"$CLAPINC" "$HERE/clap_ab_host.cpp" -o "$HOST" -ldl || { echo "host build FAILED"; exit 2; }
for f in "$JCLAP" "$DCLAP"; do [ -f "$f" ] || { echo "missing clap: $f"; exit 2; }; done

echo "== host parameter rejection controls =="
case_index=0
for bad_param in '__Dusk_Parity_Invalid__=0' 'Oversampling=unrecognized' 'Oversampling'; do
    case_status=0
    "$HOST" --clap "$DCLAP" --out "$WORK/invalid_$case_index.wav" --param "$bad_param" \
        > "$WORK/invalid_$case_index.log" 2>&1 || case_status=$?
    if [[ $case_status != 2 || -e "$WORK/invalid_$case_index.wav" ]]; then
        echo "  FAIL  invalid parameter request was not rejected before rendering: $bad_param"
        cat "$WORK/invalid_$case_index.log" >&2
        exit 2
    fi
    case_index=$((case_index + 1))
done
echo "  PASS  unknown names, unknown choices and malformed assignments rejected"

echo "== 1. parameter names + order =="
"$HOST" --clap "$JCLAP" --dump-params > "$WORK/pj.raw" 2> "$WORK/pj.log" || { cat "$WORK/pj.log" >&2; exit 2; }
"$HOST" --clap "$DCLAP" --dump-params > "$WORK/pd.raw" 2> "$WORK/pd.log" || { cat "$WORK/pd.log" >&2; exit 2; }
tail -n +2 "$WORK/pj.raw" | cut -f1,2 > "$WORK/pj.txt"
tail -n +2 "$WORK/pd.raw" | cut -f1,2 > "$WORK/pd.txt"
if diff -q "$WORK/pj.txt" "$WORK/pd.txt" >/dev/null; then
    echo "  PASS  $(wc -l < "$WORK/pj.txt") params identical (name + order)"
else
    echo "  FAIL  parameter list diverges:"; diff "$WORK/pj.txt" "$WORK/pd.txt" | head || true; fail=1
fi

render() { # out-tag  params...
    local tag="$1"; shift
    "$HOST" --clap "$JCLAP" --out "$WORK/j_$tag.wav" --out-input "$WORK/j_${tag}_input.wav" "$@" 2> "$WORK/j_$tag.log" || { cat "$WORK/j_$tag.log" >&2; exit 2; }
    "$HOST" --clap "$DCLAP" --out "$WORK/d_$tag.wav" --out-input "$WORK/d_${tag}_input.wav" "$@" 2> "$WORK/d_$tag.log" || { cat "$WORK/d_$tag.log" >&2; exit 2; }
    cmp -s "$WORK/j_${tag}_input.wav" "$WORK/d_${tag}_input.wav" || { echo "different input payloads for $tag" >&2; exit 2; }
}
nul()  { python3 "$NULL" "$WORK/j_$1.wav" "$WORK/d_$1.wav" --label "$2" "${@:3}" || fail=1; }

echo "== 2. bit-exact null @ 1x =="
render def  --signal noise
nul def "Digital-defaults" --bit-exact
render byp --signal noise --param "Band 3 Gain=12" --param "Master Gain=6" --param "Bypass=1"
nul byp "Bypass-passthru" --bit-exact
python3 "$NULL" "$WORK/j_byp_input.wav" "$WORK/j_byp.wav" --label "JUCE-bypass-vs-input" --bit-exact || fail=1
python3 "$NULL" "$WORK/d_byp_input.wav" "$WORK/d_byp.wav" --label "DAF-bypass-vs-input" --bit-exact || fail=1

echo "== 3. magnitude parity (active / Tube / oversampled) =="
render digact --signal noise --param "Band 3 Gain=6" --param "Band 5 Gain=-6"
nul digact "Digital-active" --spectral --skip-start 1.0
render os2 --signal noise --param "Oversampling=2x" --param "Band 4 Gain=6" --param "Band 4 Saturation=Tape" --param "Band 4 Sat Drive=0.8"
nul os2 "OS-2x-sat" --spectral --skip-start 1.0
render os4 --signal noise --param "Oversampling=4x" --param "Band 4 Gain=6"
nul os4 "OS-4x-lin" --spectral --skip-start 1.0
render tube --signal noise --param "EQ Type=Tube" --param "Tube EQ LF Boost=5" --param "Tube EQ HF Boost=4"
nul tube "Tube" --spectral --skip-start 1.0

echo "== 4. latency (reported delta, informational) =="
render imp --signal impulse
python3 - "$WORK/j_imp.wav" "$WORK/d_imp.wav" <<'PY'
import sys,struct
def off(p):
    b=open(p,'rb').read();pos=12;d=None;ch=2
    while pos+8<=len(b):
        c=b[pos:pos+4];l=struct.unpack('<I',b[pos+4:pos+8])[0];bo=b[pos+8:pos+8+l]
        if c==b'fmt ':ch=struct.unpack('<HHIIHH',bo[:16])[1]
        if c==b'data':d=bo
        pos+=8+l+(l&1)
    s=struct.unpack('<%df'%(len(d)//4),d);x=list(s[0::ch])
    return next((i for i,v in enumerate(x) if abs(v)>1e-6),-1)
j,dp=off(sys.argv[1]),off(sys.argv[2])
print(f"  JUCE latency={j} smp  DAF latency={dp} smp  (delta {j-dp} — JUCE reports constant max-OS latency for PDC)")
PY

echo
[ $fail -eq 0 ] && echo "RESULT: ALL PARITY GATES PASS" || echo "RESULT: PARITY FAILURES ABOVE"
exit $fail
