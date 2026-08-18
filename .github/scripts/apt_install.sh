#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One apt front end for every workflow in this repo.
#
# Ubuntu mirrors are the least reliable thing in this CI. Two distinct failure
# shapes have both taken the fleet down, and they need different defences:
#
#   STALL. An unreachable mirror does not fail apt, it hangs it. On run
#   32157020545 five of the eleven juce-compile-check jobs sat in their install
#   step until the 45-minute job timeout killed them: three hours of runner
#   time, reported as "cancelled" with no error to read. A retry loop is no
#   help here, because a call that hangs never comes back to be retried. Hence
#   timeout(1) around every apt invocation.
#
#   HARD FAIL. On run 32176674366 the DPF container's prerequisite step failed
#   outright in 81 seconds. That one WOULD have survived a retry, but the loop
#   only existed in a later step which never ran. Hence retries everywhere,
#   from one place, rather than hand-copied into whichever step someone
#   remembered.
#
# Both incidents were the same mirror. dpf-build.yml forces apt onto
# azure.archive.ubuntu.com, documented at the time because archive.ubuntu.com
# measured 724 B/s from a runner; on 2026-08-18 the polarity was reversed and
# Azure was the sick one. Pinning EITHER mirror is the bug, so after the first
# failed attempt this script switches to the other one and keeps going.
#
# Usage:
#   apt_install.sh pkg [pkg...]   update, then install those packages
#   apt_install.sh --update-only  refresh the lists only (after adding a PPA)
#
# Runs as root in a container and via sudo on a hosted runner, detected rather
# than configured, because the call sites are split between both.
set -uo pipefail

ATTEMPTS=5
UPDATE_TIMEOUT=240
INSTALL_TIMEOUT=600
APT_OPTS=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30)

AZURE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
STOCK_MIRROR="http://archive.ubuntu.com/ubuntu"

if [ "$(id -u)" -eq 0 ]; then
    SUDO=()
else
    SUDO=(sudo)
fi

update_only=0
if [ "${1:-}" = "--update-only" ]; then
    update_only=1
    shift
fi
packages=("$@")

if [ "$update_only" -eq 0 ] && [ ${#packages[@]} -eq 0 ]; then
    echo "apt_install: no packages given" >&2
    exit 2
fi

# Both list formats: 24.04 images ship deb822 .sources files and no
# sources.list, so touching only the latter would silently do nothing there.
mirror_files() {
    local f
    for f in /etc/apt/sources.list \
             /etc/apt/sources.list.d/*.list \
             /etc/apt/sources.list.d/*.sources; do
        [ -f "$f" ] && printf '%s\n' "$f"
    done
}

current_mirror() {
    if mirror_files | xargs -r grep -lq "azure.archive.ubuntu.com" 2>/dev/null; then
        echo azure
    else
        echo stock
    fi
}

# Move to whichever mirror we are NOT on. Called once, after the first failure,
# so a healthy mirror is never abandoned on a transient blip.
switch_mirror() {
    local from to
    if [ "$(current_mirror)" = "azure" ]; then
        from="$AZURE_MIRROR" to="$STOCK_MIRROR"
    else
        from="$STOCK_MIRROR" to="$AZURE_MIRROR"
    fi
    echo "::notice::apt switching mirror to ${to}"
    mirror_files | while read -r f; do
        "${SUDO[@]}" sed -i -e "s|${from}|${to}|g" "$f" || true
    done
    # security.ubuntu.com is a separate host and stalls independently.
    mirror_files | while read -r f; do
        "${SUDO[@]}" sed -i -e "s|http://security.ubuntu.com/ubuntu|${to}|g" "$f" || true
    done
}

attempt() {
    "${SUDO[@]}" timeout "$UPDATE_TIMEOUT" apt-get update "${APT_OPTS[@]}" || return 1
    [ "$update_only" -eq 1 ] && return 0
    "${SUDO[@]}" timeout "$INSTALL_TIMEOUT" env DEBIAN_FRONTEND=noninteractive \
        apt-get install -y --no-install-recommends "${APT_OPTS[@]}" "${packages[@]}"
}

for i in $(seq 1 "$ATTEMPTS"); do
    if attempt; then
        # A non-zero exit is not the only way apt disappoints. Confirm the
        # packages are actually present, so a missing one fails here with a
        # clear message instead of surfacing as a confusing compile or loader
        # error several steps later.
        missing=()
        for pkg in ${packages+"${packages[@]}"}; do
            dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
        done
        if [ ${#missing[@]} -eq 0 ]; then
            exit 0
        fi
        echo "::warning::apt reported success but these are missing: ${missing[*]}"
    fi

    if [ "$i" -eq "$ATTEMPTS" ]; then
        echo "::error::apt failed after ${ATTEMPTS} attempts (mirror: $(current_mirror))"
        exit 1
    fi

    echo "::warning::apt attempt ${i} failed or timed out; retrying in 15s"
    [ "$i" -eq 1 ] && switch_mirror
    sleep 15
done
