#!/usr/bin/env bash
# Build the standalone Acid harness and run every Acid engine gate.
set -e
cd "$(dirname "$0")"
# Three-step configure; see the long note in ../run_all.sh. A failed Ninja
# configure leaves a CMakeCache.txt pinned to Ninja, so a bare retry dies on the
# poisoned cache (verified: rc=1 then rc=1) and set -e kills the suite.
#   1. in-place Ninja  2. wipe + retry Ninja  3. wipe + default generator
cmake -B build -GNinja >/dev/null 2>&1 \
    || { rm -rf build; cmake -B build -GNinja >/dev/null 2>&1; } \
    || { rm -rf build; cmake -B build >/dev/null; }
cmake --build build -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
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
