#!/usr/bin/env bash
# Build the standalone Acid harness and run every Acid engine gate.
set -e
cd "$(dirname "$0")"
cmake -B build -GNinja >/dev/null
cmake --build build >/dev/null
echo "== acid_test built =="

fail=0
for g in slope scream accent slide seq; do
    echo
    echo "########## ${g}_gate ##########"
    python3 "${g}_gate.py" || fail=1
done

echo
if [ "$fail" -eq 0 ]; then echo "ALL ACID GATES GREEN"; else echo "SOME ACID GATES FAILED"; fi
exit $fail
