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

# The whole point of this script is to fail fast and legibly, so its own worst
# case has to fit inside the caller's job budget -- otherwise a pathological run
# is killed by the JOB timeout and reports as "cancelled" with no error, which is
# the exact failure mode this exists to remove. Callers run under 30-, 40- and
# 45-minute job limits, so the defaults are sized against the smallest:
#
#   3 x (120 update + 240 install) + 2 x 10s sleep = 1100s, about 18 minutes.
#
# Three attempts is enough because the mirror switch happens after the first
# failure, so attempt 2 is already on the other mirror and attempt 3 is margin.
# A caller with a tighter step budget can lower any of these from the workflow.
ATTEMPTS="${APT_ATTEMPTS:-3}"
UPDATE_TIMEOUT="${APT_UPDATE_TIMEOUT:-120}"
INSTALL_TIMEOUT="${APT_INSTALL_TIMEOUT:-240}"
RETRY_SLEEP="${APT_RETRY_SLEEP:-10}"
APT_OPTS=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30)

# Two mirror families, and arm64 is not a footnote: aarch64 Ubuntu serves from
# ports.ubuntu.com/ubuntu-ports, which shares no substring with the x86 archive
# host. The first version of this switched only the archive pair, so on the arm64
# jobs the sed matched nothing, the switch silently did nothing, and every retry
# went back to the same dead mirror. Each family is only ever swapped within
# itself, so a ports source can never be rewritten to an x86 archive.
AZURE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
STOCK_MIRROR="http://archive.ubuntu.com/ubuntu"
AZURE_PORTS="http://azure.ports.ubuntu.com/ubuntu-ports"
STOCK_PORTS="http://ports.ubuntu.com/ubuntu-ports"

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

# Plain loop rather than `xargs -r grep`: -r is a GNU extension, so the xargs
# form silently reported "no match" when this was exercised on macOS and both
# mirror families looked identical. It would have worked on the Linux runners
# and been untestable anywhere else.
sources_match() {
    local f
    while read -r f; do
        grep -q "$1" "$f" 2>/dev/null && return 0
    done < <(mirror_files)
    return 1
}

# Which family this machine serves from: ports on aarch64, archive on x86.
mirror_family() {
    if sources_match "ports\.ubuntu\.com"; then
        echo ports
    else
        echo archive
    fi
}

current_mirror() {
    if sources_match "azure\."; then
        echo azure
    else
        echo stock
    fi
}

# Move to whichever mirror we are NOT on, within this machine's family. Called
# once, after the first failure, so a healthy mirror is never abandoned on a
# transient blip.
switch_mirror() {
    local from to security_to
    if [ "$(mirror_family)" = "ports" ]; then
        if [ "$(current_mirror)" = "azure" ]; then
            from="$AZURE_PORTS" to="$STOCK_PORTS"
        else
            from="$STOCK_PORTS" to="$AZURE_PORTS"
        fi
        # aarch64 serves security from the ports host too, so it moves with it.
        security_to="$to"
    else
        if [ "$(current_mirror)" = "azure" ]; then
            from="$AZURE_MIRROR" to="$STOCK_MIRROR"
        else
            from="$STOCK_MIRROR" to="$AZURE_MIRROR"
        fi
        security_to="$to"
    fi
    echo "::notice::apt switching mirror to ${to}"
    mirror_files | while read -r f; do
        "${SUDO[@]}" sed -i -e "s|${from}|${to}|g" "$f" || true
    done
    # security.ubuntu.com is a separate host and stalls independently. Only ever
    # present on the archive side; on ports the security suites already live on
    # the ports host and were rewritten above.
    mirror_files | while read -r f; do
        "${SUDO[@]}" sed -i -e "s|http://security.ubuntu.com/ubuntu|${security_to}|g" "$f" || true
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

    echo "::warning::apt attempt ${i} failed or timed out; retrying in ${RETRY_SLEEP}s"
    [ "$i" -eq 1 ] && switch_mirror
    sleep "$RETRY_SLEEP"
done
