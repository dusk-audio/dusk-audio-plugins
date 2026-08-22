#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build and run every CrashLog behaviour against the correct implementation,
# then deliberately inject the corresponding defect and require that the same
# assertion rejects it.

set -euo pipefail

if [[ $(uname -s) != Linux ]]; then
    echo "CrashLog behavioral tests require Linux (/proc/self/maps and ELF dlopen semantics)." >&2
    exit 2
fi
if (( BASH_VERSINFO[0] < 4 )); then
    echo "CrashLog behavioral tests require Bash 4 or newer." >&2
    exit 2
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
UTIL_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${CRASHLOG_TEST_BUILD_DIR:-"$SCRIPT_DIR/build"}
CXX=${CXX:-c++}

for tool in "$CXX" perl timeout; do
    command -v "$tool" >/dev/null || { echo "required tool not found: $tool" >&2; exit 2; }
done

mkdir -p "$BUILD_DIR" "$BUILD_DIR/runs" "$BUILD_DIR/mutant-no-pin"

COMMON=( -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -pthread )
DSO_COMMON=( "${COMMON[@]}" -fPIC -fvisibility=hidden -fno-gnu-unique -shared )

"$CXX" "${DSO_COMMON[@]}" -I"$UTIL_DIR" \
    -DCRASHLOG_MODULE_NAME='"Module A"' \
    -DCRASHLOG_MODULE_VERSION='"1.0.0"' \
    "$SCRIPT_DIR/CrashLogModule.cpp" -ldl -o "$BUILD_DIR/libcrashlog_a.so"

"$CXX" "${DSO_COMMON[@]}" -I"$UTIL_DIR" \
    -DCRASHLOG_MODULE_NAME='"Module B"' \
    -DCRASHLOG_MODULE_VERSION='"1.0.0"' \
    "$SCRIPT_DIR/CrashLogModule.cpp" -ldl -o "$BUILD_DIR/libcrashlog_b.so"

# Real source mutant for the DAF-only unload hazard: retain the partial-unlink
# policy but remove the module pin. The probe must see the .so disappear.
cp "$UTIL_DIR/CrashLog.hpp" "$BUILD_DIR/mutant-no-pin/CrashLog.hpp"
perl -0pi -e 's/else if \(! detail::pinContainingModule\(\)\)/else if (true)/' \
    "$BUILD_DIR/mutant-no-pin/CrashLog.hpp"
if grep -Fq 'else if (! detail::pinContainingModule())' \
       "$BUILD_DIR/mutant-no-pin/CrashLog.hpp" \
   || ! grep -Fq 'else if (true)' "$BUILD_DIR/mutant-no-pin/CrashLog.hpp"; then
    echo "failed to create no-pin mutant" >&2
    exit 2
fi
"$CXX" "${DSO_COMMON[@]}" -I"$BUILD_DIR/mutant-no-pin" \
    -DCRASHLOG_MODULE_NAME='"Module A"' \
    -DCRASHLOG_MODULE_VERSION='"1.0.0"' \
    "$SCRIPT_DIR/CrashLogModule.cpp" -ldl -o "$BUILD_DIR/libcrashlog_no_pin.so"

# GNU ld redirects this test DSO's dladdr reference to a wrapper that fails.
# The production header is unchanged and no release-visible bypass exists.
"$CXX" "${DSO_COMMON[@]}" -I"$UTIL_DIR" \
    -DCRASHLOG_TEST_WRAP_DLADDR \
    -DCRASHLOG_MODULE_NAME='"Pin Failure Module"' \
    -DCRASHLOG_MODULE_VERSION='"1.0.0"' \
    "$SCRIPT_DIR/CrashLogModule.cpp" -Wl,--wrap=dladdr -ldl \
    -o "$BUILD_DIR/libcrashlog_pin_failure.so"

"$CXX" "${COMMON[@]}" "$SCRIPT_DIR/CrashLogProbe.cpp" -ldl \
    -o "$BUILD_DIR/crashlog_probe"

export CRASHLOG_TEST_MODULE_A="$BUILD_DIR/libcrashlog_a.so"
export CRASHLOG_TEST_MODULE_B="$BUILD_DIR/libcrashlog_b.so"
export CRASHLOG_TEST_MODULE_NO_PIN="$BUILD_DIR/libcrashlog_no_pin.so"
export CRASHLOG_TEST_MODULE_PIN_FAILURE="$BUILD_DIR/libcrashlog_pin_failure.so"

CASES=(
    host-handler-still-runs
    no-sigkill
    uninstall-restores
    chained
    refcount
    reentrant
    sig-ign-resumable
    sig-ign-bounded-no-spin
    concurrent-threads
    reinstall-after-partial-uninstall
    partial-unlink
    siglongjmp-recovery
    full-registry
    scoped
)

declare -A MUTATION=(
    [host-handler-still-runs]="replace saved host disposition with SIG_DFL"
    [no-sigkill]="reintroduce kill(getpid(), SIGKILL)"
    [uninstall-restores]="omit the final uninstall"
    [chained]="omit the first DAF binary from the chain"
    [refcount]="omit the second registration"
    [reentrant]="clear the same-thread guard before recursive entry"
    [sig-ign-resumable]="treat resumable SIG_IGN as SIG_DFL"
    [sig-ign-bounded-no-spin]="return to a synchronous SIGSEGV instruction forever"
    [concurrent-threads]="collapse all thread IDs to one global guard identity"
    [reinstall-after-partial-uninstall]="falsely mark a restored signal as still installed"
    [partial-unlink]="remove the DAF module pin from a copied header"
    [siglongjmp-recovery]="leak the guard slot across siglongjmp"
    [full-registry]="make only 15 of 16 fixed slots reachable"
    [scoped]="leak the scoped registration instead of destroying it"
)

# Every defect is translated by the probe into a failed assertion (status 1),
# including fatal-signal mutants run in a child process. Any other status,
# including setup failure 2 or timeout 124, is inconclusive.

CORRECT_LOG="$BUILD_DIR/correct.tsv"
MUTANT_LOG="$BUILD_DIR/mutants.tsv"
: > "$CORRECT_LOG"
: > "$MUTANT_LOG"

correct_passed=0
mutants_rejected=0
mutants_inconclusive=0

for name in "${CASES[@]}"; do
    run_home=$(mktemp -d "$BUILD_DIR/runs/correct-${name}.XXXXXX")
    if timeout 15s "$BUILD_DIR/crashlog_probe" --case "$name" --home "$run_home" \
        >>"$CORRECT_LOG" 2>&1; then
        ((correct_passed += 1))
        printf 'correct PASS\t%s\n' "$name"
    else
        status=$?
        printf 'correct FAIL\t%s\tstatus=%s\n' "$name" "$status"
        tail -20 "$CORRECT_LOG"
    fi
done

for name in "${CASES[@]}"; do
    run_home=$(mktemp -d "$BUILD_DIR/runs/mutant-${name}.XXXXXX")
    printf '%s\t%s\t' "$name" "${MUTATION[$name]}" >>"$MUTANT_LOG"
    if timeout 15s "$BUILD_DIR/crashlog_probe" --case "$name" --home "$run_home" --mutant \
        >>"$MUTANT_LOG" 2>&1; then
        printf 'mutant SURVIVED\t%s\n' "$name"
    else
        status=$?
        if (( status == 1 )); then
            ((mutants_rejected += 1))
            printf 'mutant REJECTED\t%s\tstatus=%s\n' "$name" "$status"
        else
            ((mutants_inconclusive += 1))
            printf 'mutant INCONCLUSIVE\t%s\tstatus=%s\texpected=1\n' "$name" "$status"
        fi
    fi
done

printf 'SUMMARY\tcorrect=%d/%d\tmutants-rejected=%d/%d\tmutants-inconclusive=%d\n' \
    "$correct_passed" "${#CASES[@]}" "$mutants_rejected" "${#CASES[@]}" \
    "$mutants_inconclusive"

if (( correct_passed != ${#CASES[@]} || mutants_rejected != ${#CASES[@]} )); then
    echo "correct log: $CORRECT_LOG" >&2
    echo "mutant log:  $MUTANT_LOG" >&2
    exit 1
fi
