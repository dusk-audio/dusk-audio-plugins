#!/usr/bin/env bash
# Build the Prism FM harness and run every gate. Self-contained; does not touch
# the sibling core-test build.
set -e
cd "$(dirname "$0")"
cmake -B build -GNinja >/dev/null 2>&1 || cmake -B build >/dev/null
cmake --build build >/dev/null
echo "== fm_test built =="

fail=0
echo
echo "########## sin_gate ##########"
# Pure-numeric gate (C++, no audio): the sinTurns() polynomial contract. Runs
# first so a broken approximation is named directly instead of surfacing as a
# puzzling THD or sideband failure downstream.
./build/sin_test || fail=1

for g in spectrum env feedback; do
    echo
    echo "########## ${g}_gate ##########"
    python3 "${g}_gate.py" || fail=1
done

echo
echo "########## alias_gate (report only) ##########"
python3 alias_gate.py || echo "alias_gate exited nonzero (report-only, not fatal)"

echo
if [ "$fail" -eq 0 ]; then echo "ALL PASS/FAIL GATES GREEN"; else echo "SOME GATES FAILED"; fi
exit $fail
