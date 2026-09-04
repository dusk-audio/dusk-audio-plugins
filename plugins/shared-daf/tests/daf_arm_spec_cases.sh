#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#
# daf_arm_spec_cases.sh — what an output-parameter harness must reject in a
# --set specification.
#
# The harnesses take --set "Parameter Name=0.8" to move a control before the
# run, because a compressor at its default preset does not compress and its
# gain-reduction meters then correctly report nothing. That makes the parsing
# load-bearing: a specification that is quietly misread arms the wrong thing (or
# nothing), the meters report what they should, and the failure that follows
# blames the meters instead of the typo.
#
# strtod is the trap. It reports "no conversion" by leaving its end pointer at
# the start while still returning 0.0, so an empty suffix reads as zero; and it
# parses "nan" and "inf" perfectly happily, neither of which is a normalised
# parameter value. Before this was fixed, --set "Peak Reduction=inf" armed full
# scale and the gate PASSED.
#
#   daf_arm_spec_cases.sh <harness binary> <plugin binary> <a real parameter name>

set -uo pipefail

HARNESS="${1:?usage: daf_arm_spec_cases.sh <harness> <plugin> <param name>}"
PLUGIN="${2:?missing plugin binary}"
PARAM="${3:?missing parameter name}"

failures=0

# Short runs: this is about argument handling, not about the audio.
run_spec() { "$HARNESS" "$PLUGIN" 4 64 --set "$1" 2>&1; }

# A non-zero exit is not enough on its own. A specification that parses to the
# wrong value arms the wrong thing, the run then fails on its own assertions,
# and the harness exits non-zero for a reason that has nothing to do with the
# argument -- which is exactly the confusion this guards. So the rejection has
# to be reported as one: the message must name --set.
check_rejected() {
    local what="$1" status="$2" out="$3"

    if [ "$status" -eq 0 ]; then
        printf '  FAIL  %s was accepted\n' "$what"
        failures=$((failures + 1))
    elif ! printf '%s' "$out" | grep -q -- "--set"; then
        printf '  FAIL  %s failed, but not as a bad specification\n' "$what"
        printf '%s\n' "$out" | sed 's/^/          /' | head -3
        failures=$((failures + 1))
    else
        printf '  ok    %s rejected\n' "$what"
    fi
}

reject() {
    local spec="$1" what="$2" out status=0
    out="$(run_spec "$spec")" || status=$?
    check_rejected "$what (--set \"$spec\")" "$status" "$out"
}

printf '%s\n' "arm specification parsing: $(basename "$HARNESS")"

reject "${PARAM}="      "empty value"
reject "${PARAM}=nan"   "nan"
reject "${PARAM}=inf"   "inf"
reject "${PARAM}=-inf"  "-inf"
reject "${PARAM}=0.5x"  "trailing garbage"
reject "=0.5"           "empty name"
reject "${PARAM}"       "no separator"

# A bare --set with nothing after it. Separate from the cases above because
# there is no specification to pass: the harness has to notice it is at the end
# of argv rather than reading past it, and still say what is wrong.
bare_out=""; bare_status=0
bare_out="$("$HARNESS" "$PLUGIN" 4 64 --set 2>&1)" || bare_status=$?
check_rejected "a bare --set with no specification" "$bare_status" "$bare_out"

# The positive case, so a harness that rejects everything cannot pass this.
if run_spec "${PARAM}=0.8" > /dev/null; then
    printf '  ok    a valid specification is still accepted\n'
else
    printf '  FAIL  a valid specification was rejected: --set "%s=0.8"\n' "$PARAM"
    failures=$((failures + 1))
fi

if [ "$failures" = 0 ]; then
    printf 'all arm specification cases passed\n'
    exit 0
fi
printf '%d arm specification case(s) failed\n' "$failures"
exit 1
