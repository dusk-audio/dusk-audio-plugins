#!/usr/bin/env bash
# Build the render harness and run every Multi-Synth core gate.
set -e
cd "$(dirname "$0")"
cmake -B build -GNinja >/dev/null
cmake --build build >/dev/null
echo "== render_test built =="

fail=0
for g in pitch env reverb arp acid; do
    echo
    echo "########## ${g}_gate ##########"
    python3 "${g}_gate.py" || fail=1
done

echo
echo "########## alias_gate (report only) ##########"
python3 alias_gate.py

echo
if [ "$fail" -eq 0 ]; then echo "ALL PASS/FAIL GATES GREEN"; else echo "SOME GATES FAILED"; fi
exit $fail
