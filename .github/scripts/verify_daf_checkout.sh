#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#
# Fail unless a DAF checkout is exactly the commit the run resolved.
#
#   verify_daf_checkout.sh DIR EXPECTED_SHA
#
# Runs after every actions/checkout of dusk-audio/DAF. An empty `ref:` (a job
# missing `needs:` on the job that resolves .github/daf-ref, or a typo in the
# output name) makes actions/checkout silently take DAF's default branch at job
# time; this turns that into a failure that names the problem.
set -euo pipefail

dir="${1:?usage: verify_daf_checkout.sh DIR EXPECTED_SHA}"
expected="${2:-}"
if ! [[ "$expected" =~ ^[0-9a-f]{40}$ ]]; then
    echo "::error::DAF checkout at $dir: expected a resolved 40-character SHA, got '${expected}'. Does this job need the job that resolves .github/daf-ref?"
    exit 1
fi
head="$(git -C "$dir" rev-parse HEAD)"
if [ "$head" != "$expected" ]; then
    echo "::error::DAF checkout at $dir is $head, but this run resolved $expected"
    exit 1
fi
echo "DAF at $dir is $head"
