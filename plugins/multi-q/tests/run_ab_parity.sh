#!/usr/bin/env bash
# run_ab_parity.sh — JUCE-vs-DPF Multi-Q parity harness (one command).
#
# Proves the DPF port ("Multi-Q 2") matches the JUCE original:
#   1. Parameter names + order identical (catches silent reorders).
#   2. Choice-driven params resolve on both (params set by display text).
#   3. Digital / bypass at 1x null to the float floor (bit-exact DSP).
#   4. Active Digital / Tube / oversampled paths match in magnitude response
#      (median |Δmag| tolerance) — the correct test where group delays differ.
#
# Out of scope by decision: British (accepted upgraded 4K core). The 1x latency-
# reporting difference (JUCE reports constant max-oversampler latency for PDC;
# DPF Digital reports 0) is reported, not gated.
#
# Usage: run_ab_parity.sh [JUCE.clap] [DPF.clap]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
JCLAP="${1:-$REPO/build/bin/CLAP/Multi-Q.clap}"
DCLAP="${2:-$REPO/plugins/multi-q/dpf-plugin/build/bin/multi_q_2.clap}"
CLAPINC="$REPO/external/clap-juce-extensions/clap-libs/clap/include"
WORK="$(mktemp -d)"
HOST="$WORK/clap_ab_host"
NULL="$HERE/ab_null.py"
fail=0

echo "== building A/B host =="
g++ -std=c++17 -O2 -I"$CLAPINC" "$HERE/clap_ab_host.cpp" -o "$HOST" -ldl || { echo "host build FAILED"; exit 2; }
for f in "$JCLAP" "$DCLAP"; do [ -f "$f" ] || { echo "missing clap: $f"; exit 2; }; done

echo "== 1. parameter names + order =="
"$HOST" --clap "$JCLAP" --dump-params 2>/dev/null | tail -n +2 | cut -f1,2 > "$WORK/pj.txt"
"$HOST" --clap "$DCLAP" --dump-params 2>/dev/null | tail -n +2 | cut -f1,2 > "$WORK/pd.txt"
if diff -q "$WORK/pj.txt" "$WORK/pd.txt" >/dev/null; then
    echo "  PASS  $(wc -l < "$WORK/pj.txt") params identical (name + order)"
else
    echo "  FAIL  parameter list diverges:"; diff "$WORK/pj.txt" "$WORK/pd.txt" | head; fail=1
fi

render() { # out-tag  params...
    local tag="$1"; shift
    "$HOST" --clap "$JCLAP" --out "$WORK/j_$tag.wav" "$@" 2>/dev/null
    "$HOST" --clap "$DCLAP" --out "$WORK/d_$tag.wav" "$@" 2>/dev/null
}
nul()  { python3 "$NULL" "$WORK/j_$1.wav" "$WORK/d_$1.wav" --label "$2" "${@:3}" || fail=1; }

echo "== 2. bit-exact null @ 1x =="
render def  --signal noise
nul def "Digital-defaults"
render byp  --signal noise --param "Bypass=On"          # DPF accepts On/1 alike
nul byp "Bypass-passthru"

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
print(f"  JUCE latency={j} smp  DPF latency={dp} smp  (delta {j-dp} — JUCE reports constant max-OS latency for PDC)")
PY

echo
[ $fail -eq 0 ] && echo "RESULT: ALL PARITY GATES PASS" || echo "RESULT: PARITY FAILURES ABOVE"
rm -rf "$WORK"
exit $fail
